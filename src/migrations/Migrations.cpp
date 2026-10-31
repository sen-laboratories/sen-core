/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Migrations.h"

#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <String.h>
#include <fs_attr.h>

#include <string.h>

#include <spdlog/spdlog.h>

#include <sen/Sen.h>

#include "../relations/QueryUtil.h"

namespace sen {
namespace migration {

namespace {

/** the attribute that holds the relations of a type: SEN:REL:<the type without relation/> */
BString
RelationAttribute(const char* type)
{
	BString attribute(sen::attr::kRelationPrefix);
	attribute << (strncmp(type, sen::mime::kRelationPrefix, strlen(sen::mime::kRelationPrefix)) == 0 ? type + strlen(sen::mime::kRelationPrefix) : type);
	return attribute;
}

/** reads a message attribute; false if there is none, or it is no message */
bool
ReadMessage(BNode& node, const char* name, BMessage* message)
{
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.type != B_MESSAGE_TYPE || info.size <= 0)
		return false;
	std::vector<char> buffer(info.size);
	return node.ReadAttr(name, B_MESSAGE_TYPE, 0, buffer.data(), info.size) == info.size && message->Unflatten(buffer.data()) == B_OK;
}

bool
WriteMessage(BNode& node, const char* name, const BMessage& message)
{
	ssize_t size = message.FlattenedSize();
	std::vector<char> buffer(size);
	return message.Flatten(buffer.data(), size) == B_OK && node.WriteAttr(name, B_MESSAGE_TYPE, 0, buffer.data(), size) == size;
}

/** the targets of the relations of the old type are added to those of the new one: a target that is there already stays as it is */
void
Merge(const BMessage& moving, BMessage* into)
{
	char* target;
	type_code type;
	int32 count;
	for (int32 index = 0; moving.GetInfo(B_MESSAGE_TYPE, index, &target, &type, &count) == B_OK; index++) {
		if (into->HasMessage(target))
			continue;
		// (there can be several relations to the same target, with different properties)
		for (int32 each = 0; each < count; each++) {
			BMessage properties;
			if (moving.FindMessage(target, each, &properties) == B_OK)
				into->AddMessage(target, &properties);
		}
	}
}

}	// namespace


Counts
RenameRelationTypes(const std::vector<Rename>& renames, bool dryRun)
{
	Counts counts;

	std::vector<entry_ref> objects;
	status_t result = sen::QueryAllVolumes((BString(sen::attr::kId) << "==*").String(), &objects);
	if (result != B_OK) {
		// (a volume that has no index for SEN:ID has no SEN objects that this could find: a query that cannot run anywhere is a failure)
		spdlog::error("migration: the SEN objects could not be looked up: {}", strerror(result));
		counts.failed++;
		return counts;
	}

	for (const entry_ref& object : objects) {
		BNode node(&object);
		if (node.InitCheck() != B_OK)
			continue;

		for (const Rename& rename : renames) {
			BString from = RelationAttribute(rename.from), to = RelationAttribute(rename.to);
			BMessage moving;
			if (!ReadMessage(node, from.String(), &moving))
				continue;
			if (dryRun) {
				counts.changed++;
				continue;
			}

			BMessage moved;
			if (!ReadMessage(node, to.String(), &moved))
				moved.MakeEmpty();
			Merge(moving, &moved);

			if (!WriteMessage(node, to.String(), moved)) {
				spdlog::error("migration: could not write {} of {}", to.String(), object.name);
				counts.failed++;
				continue;
			}
			// (the old one goes only after the new one is there: a step that stops now is run again)
			if (node.RemoveAttr(from.String()) != B_OK) {
				spdlog::error("migration: could not remove {} of {}", from.String(), object.name);
				counts.failed++;
				continue;
			}
			spdlog::info("migration: the relations of {} are of type {} now ({})", object.name, rename.to, from.String());
			counts.changed++;
		}
	}
	return counts;
}


namespace {

/** 1: the base of the personal relations was called Knows (relation.person.knows), it is PersonalRelation (relation.person) */
Counts
PersonalRelation(bool dryRun)
{
	return RenameRelationTypes({
		{"relation/x-vnd.sen-labs.relation.person.knows", "relation/x-vnd.sen-labs.relation.person"},
	}, dryRun);
}

}	// namespace


const std::vector<Step>&
Steps()
{
	static const std::vector<Step> steps = {
		{1, "personal-relation", "the relation type person.knows is person (PersonalRelation) now", PersonalRelation},
	};
	return steps;
}


VersionStore
SettingsFileStore()
{
	auto path = []() {
		BPath settings;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) != B_OK || settings.Append("sen") != B_OK || settings.Append(sen::config::kSettingsFile) != B_OK)
			settings.Unset();
		return settings;
	};

	VersionStore store;
	store.read = [path]() -> int32 {
		BPath settings = path();
		BFile file(settings.Path(), B_READ_ONLY);
		BMessage message;
		return settings.InitCheck() == B_OK && file.InitCheck() == B_OK && message.Unflatten(&file) == B_OK ? message.GetInt32(kDataVersionKey, 0) : 0;
	};
	store.write = [path](int32 version) -> status_t {
		BPath settings = path();
		if (settings.InitCheck() != B_OK)
			return B_ERROR;
		BMessage message;
		{
			BFile file(settings.Path(), B_READ_ONLY);
			if (file.InitCheck() == B_OK)
				message.Unflatten(&file);
		}
		message.RemoveName(kDataVersionKey);
		message.AddInt32(kDataVersionKey, version);
		BFile file(settings.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		status_t result = file.InitCheck();
		return result == B_OK ? message.Flatten(&file) : result;
	};
	return store;
}


std::vector<Report>
Run(const VersionStore& store, bool dryRun)
{
	std::vector<Report> reports;
	int32 stored = store.read ? store.read() : 0;
	bool stopped = false;

	for (const Step& step : Steps()) {
		Report report = {&step, Counts(), step.version <= stored, false, false};
		if (!report.wasDone && !stopped) {
			spdlog::info("migration {} ({}){}", step.version, step.name, dryRun ? ", counting only" : "");
			report.counts = step.run(dryRun);
			report.ran = true;
			if (report.counts.failed > 0) {
				spdlog::error("migration {} ({}): {} could not be changed, it is run again at the next start", step.version, step.name,
					(int) report.counts.failed);
				stopped = true;
			} else if (!dryRun) {
				report.recorded = store.write && store.write(step.version) == B_OK;
				if (!report.recorded) {
					spdlog::error("migration {} ({}) is done, but could not be recorded", step.version, step.name);
					stopped = true;
				}
				stored = step.version;
			}
		}
		reports.push_back(report);
	}
	return reports;
}

}	// namespace migration
}	// namespace sen
