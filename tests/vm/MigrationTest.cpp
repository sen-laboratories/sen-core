/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * The migration steps (src/migrations/Migrations.h): renaming a relation type in the files that have it (also where the new type is stored already), that a dry
 * run changes nothing, that a second run has nothing left to do, and that the data version is recorded step by step. Does not need the server. Haiku only: the
 * files are made in TEST_DIR (default /Develop/test), a BFS volume with the index for SEN:ID.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include <Directory.h>
#include <File.h>
#include <Node.h>
#include <fs_attr.h>

#include <sen/Sen.h>

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

	printf("%s\n", failed == 0 ? "all ok" : "FAILED");
	return failed == 0 ? 0 : 1;
}
