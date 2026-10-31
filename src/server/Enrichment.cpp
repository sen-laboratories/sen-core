/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "Enrichment.h"

#include <AppFileInfo.h>
#include <File.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Notification.h>
#include <Roster.h>

#include <OS.h>

#include <vector>

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <spdlog/spdlog.h>

#include "../relations/QueryUtil.h"

namespace sen {
namespace enrich {

/** the type of a file as it is stored, else as it is guessed (empty if there is none) */
static BString
TypeOf(const entry_ref& ref)
{
	char type[B_MIME_TYPE_LENGTH];
	type[0] = '\0';
	BNode node(&ref);
	if (node.InitCheck() == B_OK)
		BNodeInfo(&node).GetType(type);
	if (type[0] == '\0') {
		BMimeType guessed;
		if (BMimeType::GuessMimeType(&ref, &guessed) == B_OK && guessed.Type() != NULL)
			strlcpy(type, guessed.Type(), sizeof(type));
	}
	return BString(type);
}

/** the first plugin of those that enrich that says that it handles the type */
static bool
PluginFor(const std::vector<entry_ref>& plugins, const BString& type, entry_ref* found)
{
	for (const entry_ref& plugin : plugins) {
		BFile file(&plugin, B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;
		BAppFileInfo info(&file);
		if (info.InitCheck() == B_OK && info.IsSupportedType(type.String())) {
			*found = plugin;
			return true;
		}
	}
	return false;
}

/** a file and the plugin that enriches it */
struct Job {
	entry_ref	file;
	entry_ref	plugin;
};

/** the files of one request, in the order in which they were given */
struct Queue {
	std::vector<Job>	jobs;
	bool				wipe;
	bool				automatic;
};

/** the longest that a plugin is waited for (one that asks the user may take its time) */
static const bigtime_t kLongestWait = 15 * 60 * 1000000LL;

/**
 * The plugins are started one after the other, each one when the one before it is done: a plugin may ask the user (BERT chooses between several books), and
 * several windows at once, for different files, cannot be answered. The thread ends with the last plugin.
 */
static int32
RunQueue(void* data)
{
	Queue* queue = static_cast<Queue*>(data);
	for (const Job& job : queue->jobs) {
		BMessage launch(B_REFS_RECEIVED);
		launch.AddRef("refs", &job.file);
		launch.AddBool("wipe", queue->wipe);
		if (queue->automatic)
			launch.AddBool("auto", true);

		team_id team = -1;
		status_t result = be_roster->Launch(&job.plugin, &launch, &team);
		if (result != B_OK && result != B_ALREADY_RUNNING) {
			spdlog::error("enrich: could not start plugin '{}' for '{}': {}", job.plugin.name, job.file.name, strerror(result));
			continue;
		}
		spdlog::info("enrich: '{}' goes to plugin '{}'", job.file.name, job.plugin.name);

		// (a plugin that was running already, as a single launch application, is waited for as well)
		bigtime_t deadline = system_time() + kLongestWait;
		app_info info;
		while (team >= 0 && be_roster->GetRunningAppInfo(team, &info) == B_OK && system_time() < deadline)
			snooze(200000);
	}
	delete queue;
	return 0;
}

status_t
Handle(const BMessage& request, BString* detail)
{
	std::vector<entry_ref> plugins;
	status_t result = sen::FindPlugins(sensei::feature::kEnrich, &plugins);
	if (result != B_OK) {
		spdlog::error("enrich: could not look for the plugins: {}", strerror(result));
		*detail = "could not look for the plugins";
		return result;
	}

	const bool wipe = request.GetBool("wipe", true);
	const bool automatic = request.GetBool("auto", false);

	Queue* queue = new Queue;
	queue->wipe = wipe;
	queue->automatic = automatic;

	int32 started = 0, unhandled = 0, files = 0;
	BString unhandledNames, unhandledType;
	entry_ref ref;
	for (int32 i = 0; request.FindRef("refs", i, &ref) == B_OK; i++) {
		files++;
		const BString type = TypeOf(ref);
		entry_ref plugin;
		if (type.IsEmpty() || !PluginFor(plugins, type, &plugin)) {
			spdlog::info("enrich: no plugin handles '{}' (type '{}')", ref.name, type.String());
			unhandled++;
			if (!unhandledNames.IsEmpty())
				unhandledNames << ", ";
			unhandledNames << ref.name;
			unhandledType = type;
			continue;
		}

		Job job;
		job.file = ref;
		job.plugin = plugin;
		queue->jobs.push_back(job);
		spdlog::info("enrich: '{}' (type '{}') is for plugin '{}'", ref.name, type.String(), plugin.name);
		started++;
	}

	if (files == 0) {
		delete queue;
		*detail = "no file to enrich";
		return B_BAD_VALUE;
	}

	// said once, not a dialog: nothing is held up and nothing stays on the screen
	if (unhandled > 0) {
		BString text;
		if (unhandled == 1 && !unhandledType.IsEmpty())
			text << "No plugin enriches a file of the type " << unhandledType << ": " << unhandledNames << ".";
		else
			text << "No plugin enriches " << unhandledNames << ".";
		BNotification notification(B_INFORMATION_NOTIFICATION);
		notification.SetGroup("SEN");
		notification.SetTitle("Nothing to enrich with");
		notification.SetContent(text.String());
		notification.Send();
	}

	if (started == 0) {
		delete queue;
		*detail = "no plugin handles the type of the file";
		return B_NOT_SUPPORTED;
	}

	// one after the other, in a thread of its own: the server is not held up
	thread_id thread = spawn_thread(RunQueue, "sen enrich queue", B_NORMAL_PRIORITY, queue);
	if (thread < 0) {
		delete queue;
		*detail = "could not start the plugins";
		return thread;
	}
	resume_thread(thread);
	*detail = BString() << started << " file(s) will be enriched, one after the other";
	return B_OK;
}

}	// namespace enrich
}	// namespace sen
