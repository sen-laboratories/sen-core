/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * The migration steps (src/migrations/Migrations.h): renaming a relation type in the files that have it (also where the new type is stored already), that a dry
 * run changes nothing, that a second run has nothing left to do, that the data version is recorded step by step, and (with the server running) the move of a
 * real Knows relation of an older version to PersonalRelation. Haiku only: the
 * files are made in TEST_DIR (default /Develop/test), a BFS volume with the index for SEN:ID.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include <Directory.h>
#include <File.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <fs_attr.h>

#include <sen/Sen.h>
#include <sen/SenOntoPeople.h>

#include "../../src/migrations/Migrations.h"

static int failed = 0;

#define CHECK(condition, message) \
	do { \
		if (condition) \
			printf("ok   %s\n", message); \
		else { \
			printf("FAIL %s\n", message); \
			failed++; \
		} \
	} while (0)

static const char* kOld = "relation/x-vnd.sen-labs.relation.test.migration-old";
static const char* kNew = "relation/x-vnd.sen-labs.relation.test.migration-new";
static const char* kOldAttr = "SEN:REL:x-vnd.sen-labs.relation.test.migration-old";
static const char* kNewAttr = "SEN:REL:x-vnd.sen-labs.relation.test.migration-new";

static BString dir;

static void
Write(BNode& node, const char* attribute, const BMessage& message)
{
	ssize_t size = message.FlattenedSize();
	char* buffer = new char[size];
	message.Flatten(buffer, size);
	node.WriteAttr(attribute, B_MESSAGE_TYPE, 0, buffer, size);
	delete[] buffer;
}

/** a SEN object (it has a SEN:ID, which is what the migration looks for) with relations of a type to the given targets, each with a Note */
static BString
MakeObject(const char* name, const char* id, const char* attribute, std::initializer_list<const char*> targets)
{
	BString path(dir);
	path << "/" << name;
	BFile file(path.String(), B_CREATE_FILE | B_READ_WRITE);
	BNode node(path.String());
	BString identifier(id);
	node.WriteAttrString(sen::attr::kId, &identifier);
	if (attribute != NULL) {
		BMessage relations;
		for (const char* target : targets) {
			BMessage properties;
			properties.AddString("SEN:REL:Note", BString("note of ") << target << " in " << name);
			relations.AddMessage(target, &properties);
		}
		Write(node, attribute, relations);
	}
	return path;
}

static bool
Read(const BString& path, const char* attribute, BMessage* relations)
{
	BNode node(path.String());
	attr_info info;
	if (node.GetAttrInfo(attribute, &info) != B_OK)
		return false;
	char* buffer = new char[info.size];
	node.ReadAttr(attribute, B_MESSAGE_TYPE, 0, buffer, info.size);
	bool ok = relations->Unflatten(buffer) == B_OK;
	delete[] buffer;
	return ok;
}

static bool
Has(const BString& path, const char* attribute)
{
	BNode node(path.String());
	attr_info info;
	return node.GetAttrInfo(attribute, &info) == B_OK;
}

// ---- helpers for the part with the server

/** a person file with a type (it has no SEN:ID: the server gives it one when it relates it) */
static entry_ref
MakePerson(const char* name)
{
	BString path(dir);
	path << "/" << name;
	BFile file(path.String(), B_CREATE_FILE | B_READ_WRITE);
	BNodeInfo(&file).SetType(sen::onto::people::mime::kPerson);
	entry_ref ref;
	BEntry(path.String()).GetRef(&ref);
	return ref;
}

static BString
PathOf(const entry_ref& ref)
{
	return BString(BPath(&ref).Path());
}

static std::string
IdOf(const entry_ref& ref)
{
	BNode node(&ref);
	BString id;
	node.ReadAttrString(sen::attr::kId, &id);
	return id.String();
}

/** the relations of a type that a file stores (SEN:REL:<type>): the targets by their SEN:ID, each with its properties */
static BMessage
Stored(const entry_ref& ref, const char* type)
{
	BString attribute(sen::attr::kRelationPrefix);
	attribute << (type + strlen(sen::mime::kRelationPrefix));
	BMessage relations;
	if (!Read(PathOf(ref), attribute.String(), &relations))
		relations.MakeEmpty();
	return relations;
}

/** how many relations of a type the file has to a target */
static int32
Sets(const entry_ref& ref, const char* type, const std::string& targetId)
{
	BMessage relations = Stored(ref, type);
	type_code code;
	int32 count = 0;
	return relations.GetInfo(targetId.c_str(), &code, &count) == B_OK ? count : 0;
}

static BMessage
Send(uint32 what, const entry_ref* source, const char* type, const entry_ref* target = NULL)
{
	BMessage message(what);
	message.AddRef(sen::key::kSourceRef, source);
	message.AddString(sen::key::kRelationType, type);
	if (target != NULL)
		message.AddRef(sen::key::kTargetRef, target);
	BMessage reply;
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid() || server.SendMessage(&message, &reply, 10000000, 10000000) != B_OK)
		reply.AddInt32(sen::key::kStatus, -1);
	return reply;
}

static int32
StatusOf(const BMessage& reply)
{
	return reply.GetInt32(sen::key::kStatus, -1);
}

/**
 * A relation as the versions before beta 4 stored it: Knows (relation.person.knows), the base of the personal relations. The server makes a real relation
 * between two people (a Friend: the people have the type of the ontology, the relation is written by the server, in both directions). It is stored under the
 * name that Knows had, with the labels that Knows has: that is what a file of a user has. The migration then moves it to PersonalRelation (relation.person),
 * and the server finds it there. Needs the server (it is not started here) and the people ontology; without the server this is skipped.
 */
static void
CheckKnowsOfOlderVersions()
{
	const char* kFriendType = sen::onto::people::mime::kFriend;
	const char* kKnowsType = "relation/x-vnd.sen-labs.relation.person.knows";
	const char* kPersonalType = sen::onto::people::mime::kPersonalRelation;
	const char* kFriendAttr = "SEN:REL:x-vnd.sen-labs.relation.person.friend";
	const char* kKnowsAttr = "SEN:REL:x-vnd.sen-labs.relation.person.knows";

	if (!BMessenger(sen::kServerSignature).IsValid()) {
		printf("     (the SEN server is not running: a relation that it wrote is not checked)\n");
		return;
	}

	BString command("rm -rf '");
	command << dir << "' && mkdir -p '" << dir << "'";
	system(command.String());
	entry_ref ada = MakePerson("Ada"), bob = MakePerson("Bob");

	BMessage reply = Send(sen::cmd::kRelationAdd, &ada, kFriendType, &bob);
	CHECK(StatusOf(reply) == sen::status::kCreated, "the server relates two people (a Friend)");
	std::string adaId = IdOf(ada), bobId = IdOf(bob);
	CHECK(!adaId.empty() && !bobId.empty() && Sets(ada, kFriendType, bobId) == 1 && Sets(bob, kFriendType, adaId) == 1,
		"the relation is stored with both people");

	// stored under the old name, with the labels of Knows: "knows" at the source, "known by" at the target
	struct { entry_ref* person; const std::string* other; const char* label; } people[] = {{&ada, &bobId, "knows"}, {&bob, &adaId, "known by"}};
	for (auto& each : people) {
		BMessage relations = Stored(*each.person, kFriendType), old, properties;
		relations.FindMessage(each.other->c_str(), &properties);
		properties.RemoveName(sen::attr::kRelationLabel);
		properties.AddString(sen::attr::kRelationLabel, each.label);
		old.AddMessage(each.other->c_str(), &properties);
		BNode node(each.person);
		Write(node, kKnowsAttr, old);
		node.RemoveAttr(kFriendAttr);
	}
	CHECK(Sets(ada, kKnowsType, bobId) == 1 && Sets(ada, kFriendType, bobId) == 0, "the people have Knows now, as in a file of an older version");
	CHECK(Stored(ada, kPersonalType).IsEmpty(), "and they have no PersonalRelation yet");

	int32 version = 0;
	sen::migration::VersionStore store;
	store.read = [&]() { return version; };
	store.write = [&](int32 value) { version = value; return B_OK; };
	std::vector<sen::migration::Report> reports = sen::migration::Run(store, false);
	CHECK(!reports.empty() && reports[0].ran && reports[0].counts.failed == 0 && reports[0].counts.changed >= 2,
		"the migration moves the relations of both people");

	CHECK(Sets(ada, kPersonalType, bobId) == 1 && Sets(bob, kPersonalType, adaId) == 1, "they are PersonalRelations");
	CHECK(!Has(PathOf(ada), kKnowsAttr) && !Has(PathOf(bob), kKnowsAttr), "the old attribute is gone");
	BMessage properties;
	Stored(ada, kPersonalType).FindMessage(bobId.c_str(), &properties);
	CHECK(strcmp(properties.GetString(sen::attr::kRelationLabel, ""), "knows") == 0, "Ada knows Bob");
	Stored(bob, kPersonalType).FindMessage(adaId.c_str(), &properties);
	CHECK(strcmp(properties.GetString(sen::attr::kRelationLabel, ""), "known by") == 0, "Bob is known by Ada");

	// the server finds it as it finds any relation of the type, and can remove it
	BMessage found = Send(sen::cmd::kRelationsGet, &ada, kPersonalType), relations;
	CHECK(StatusOf(found) == sen::status::kOk && found.FindMessage(sen::key::kRelations, &relations) == B_OK && relations.HasMessage(bobId.c_str()),
		"the server finds Bob as the person that Ada knows");
	CHECK(StatusOf(Send(sen::cmd::kRelationRemove, &ada, kPersonalType, &bob)) == sen::status::kOk, "and removes the relation");
	CHECK(Sets(ada, kPersonalType, bobId) == 0 && Sets(bob, kPersonalType, adaId) == 0, "from both people");

	command.SetTo("rm -rf '");
	command << dir << "'";
	system(command.String());
}

int
main()
{
	const char* base = getenv("TEST_DIR");
	dir = BString(base != NULL ? base : "/Develop/test") << "/_senmigration";
	BString command("rm -rf '");
	command << dir << "' && mkdir -p '" << dir << "'";
	system(command.String());

	// A has relations of the old type, B has relations of both types (one target in both), C has neither, D has the old type but is no SEN object
	BString a = MakeObject("A", "MIGTEST00001", kOldAttr, {"TARGET00001", "TARGET00002"});
	BString b = MakeObject("B", "MIGTEST00002", kOldAttr, {"TARGET00002", "TARGET00003"});
	BNode bNode(b.String());
	{
		BMessage existing;
		BMessage properties;
		properties.AddString("SEN:REL:Note", "the one that was there");
		existing.AddMessage("TARGET00002", &properties);
		Write(bNode, kNewAttr, existing);
	}
	BString c = MakeObject("C", "MIGTEST00003", NULL, {});
	BString d;
	{
		d << dir << "/D";
		BFile file(d.String(), B_CREATE_FILE | B_READ_WRITE);
		BNode node(d.String());
		BMessage relations, empty;
		relations.AddMessage("TARGET00001", &empty);
		Write(node, kOldAttr, relations);
	}

	std::vector<sen::migration::Rename> renames = {{kOld, kNew}};

	sen::migration::Counts counts = sen::migration::RenameRelationTypes(renames, true);
	CHECK(counts.failed == 0 && counts.changed == 2, "a dry run counts the two objects that have the old type");
	CHECK(Has(a, kOldAttr) && !Has(a, kNewAttr) && Has(b, kOldAttr), "a dry run changes nothing");

	counts = sen::migration::RenameRelationTypes(renames, false);
	CHECK(counts.failed == 0 && counts.changed == 2, "the run changes the two objects");

	BMessage moved;
	CHECK(!Has(a, kOldAttr) && Read(a, kNewAttr, &moved), "the relations of A are of the new type, the old attribute is gone");
	CHECK(moved.HasMessage("TARGET00001") && moved.HasMessage("TARGET00002"), "with both targets");
	BMessage property;
	moved.FindMessage("TARGET00001", &property);
	CHECK(strcmp(property.GetString("SEN:REL:Note", ""), "note of TARGET00001 in A") == 0, "and the properties of the relation");

	BMessage merged;
	CHECK(!Has(b, kOldAttr) && Read(b, kNewAttr, &merged), "B has the relations of the new type only");
	CHECK(merged.HasMessage("TARGET00002") && merged.HasMessage("TARGET00003"), "the targets of both are there");
	merged.FindMessage("TARGET00002", &property);
	CHECK(strcmp(property.GetString("SEN:REL:Note", ""), "the one that was there") == 0, "a target that was there already keeps its relation");
	CHECK(!Has(c, kOldAttr) && !Has(c, kNewAttr), "an object without the old type is left alone");
	CHECK(Has(d, kOldAttr) && !Has(d, kNewAttr), "a file that is no SEN object is left alone");

	counts = sen::migration::RenameRelationTypes(renames, false);
	CHECK(counts.failed == 0 && counts.changed == 0, "a second run has nothing to do");

	// the steps and the data version
	int32 version = 0;
	int32 writes = 0;
	sen::migration::VersionStore store;
	store.read = [&]() { return version; };
	store.write = [&](int32 value) { version = value; writes++; return B_OK; };

	int32 last = sen::migration::Steps().back().version;
	std::vector<sen::migration::Report> reports = sen::migration::Run(store, true);
	CHECK(reports.size() == sen::migration::Steps().size() && version == 0 && writes == 0, "a dry run of the steps records nothing");
	bool allPending = true;
	for (const auto& report : reports)
		allPending = allPending && report.ran && !report.wasDone;
	CHECK(allPending, "all steps are to do for data that was never migrated");

	reports = sen::migration::Run(store, false);
	bool allRecorded = true;
	for (const auto& report : reports)
		allRecorded = allRecorded && report.ran && report.recorded && report.counts.failed == 0;
	CHECK(allRecorded && version == last, "the steps run and the version is recorded");
	CHECK(writes == (int32) sen::migration::Steps().size(), "after each step");

	reports = sen::migration::Run(store, false);
	bool allDone = true;
	for (const auto& report : reports)
		allDone = allDone && report.wasDone && !report.ran;
	CHECK(allDone && writes == (int32) sen::migration::Steps().size(), "a second run finds all done and does nothing");

	command.SetTo("rm -rf '");
	command << dir << "'";
	system(command.String());

	CheckKnowsOfOlderVersions();

	printf("%s\n", failed == 0 ? "all ok" : "FAILED");
	return failed == 0 ? 0 : 1;
}
