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

		BMessage launch(B_REFS_RECEIVED);
		launch.AddRef("refs", &ref);
		launch.AddBool("wipe", wipe);
		if (automatic)
			launch.AddBool("auto", true);
		result = be_roster->Launch(&plugin, &launch);
		if (result == B_OK) {
			spdlog::info("enrich: '{}' (type '{}') goes to plugin '{}'", ref.name, type.String(), plugin.name);
			started++;
		} else {
			spdlog::error("enrich: could not start plugin '{}' for '{}': {}", plugin.name, ref.name, strerror(result));
		}
	}

	if (files == 0) {
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
		*detail = "no plugin handles the type of the file";
		return B_NOT_SUPPORTED;
	}
	*detail = BString() << started << " plugin(s) started";
	return B_OK;
}

}	// namespace enrich
}	// namespace sen
