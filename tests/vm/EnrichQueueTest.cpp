/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Enriching several files: one request with the files of two books, and the plugins of BERT must run one after the other (never two of them at the same time),
 * also for a second request that comes while the first is not done. Needs a running SEN server, the book plugin (BERT) and a network (OpenLibrary): without
 * the answer of OpenLibrary the files are not enriched, which is reported (not a failure), the order is checked all the same. Haiku only. The files are
 * made in TEST_DIR (default /Develop/test).
 */
#include <stdio.h>
#include <stdlib.h>

#include <Message.h>
#include <List.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <File.h>
#include <Entry.h>
#include <OS.h>
#include <Roster.h>
#include <String.h>
#include <fs_attr.h>

#include <algorithm>

#include <sen/Sen.h>

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

static const char* kBertSignature = "application/x-vnd.sen-plugin.sen-labs.book-enricher";

static entry_ref
MakeBook(const BString& dir, const char* title)
{
	BString path(dir);
	path << "/" << title;
	BFile file(path.String(), B_CREATE_FILE | B_READ_WRITE);
	BNodeInfo(&file).SetType("entity/x-vnd.sen-labs.entity.book");
	BNode node(path.String());
	BString value(title);
	node.WriteAttrString("dc:title", &value);
	entry_ref ref;
	BEntry(path.String()).GetRef(&ref);
	return ref;
}

static bool
IsEnriched(const entry_ref& ref)
{
	BNode node(&ref);
	attr_info info;
	return node.GetAttrInfo("dc:creator", &info) == B_OK;
}

int
main()
{
	if (!BMessenger(sen::kServerSignature).IsValid()) {
		printf("     (the SEN server is not running: not checked)\n");
		return 0;
	}

	const char* base = getenv("TEST_DIR");
	BString dir(base != NULL ? base : "/Develop/test");
	dir << "/_senenrich";
	BString command("rm -rf '");
	command << dir << "' && mkdir -p '" << dir << "'";
	system(command.String());

	entry_ref books[3] = {MakeBook(dir, "The Pragmatic Programmer"), MakeBook(dir, "Clean Code"), MakeBook(dir, "Refactoring")};

	// one request with two files, and a second one for the third that comes at once: the one queue has all three
	BMessage first(sen::cmd::kEnrichRefs);
	first.AddRef("refs", &books[0]);
	first.AddRef("refs", &books[1]);
	first.AddBool("auto", true);
	BMessage second(sen::cmd::kEnrichRefs);
	second.AddRef("refs", &books[2]);
	second.AddBool("auto", true);
	BMessage reply;
	CHECK(BMessenger(sen::kServerSignature).SendMessage(&first, &reply, 10000000, 10000000) == B_OK && reply.GetInt32("status", -1) == 200,
		"the request for two files is accepted");
	CHECK(BMessenger(sen::kServerSignature).SendMessage(&second, &reply, 10000000, 10000000) == B_OK && reply.GetInt32("status", -1) == 200,
		"and the second one, while the first is not done");

	// watch the plugins: how many run at once, until all three files are enriched (or it takes too long)
	int32 most = 0, seen = 0;
	bigtime_t deadline = system_time() + 180 * 1000000LL;
	while (system_time() < deadline) {
		BList teams;
		be_roster->GetAppList(kBertSignature, &teams);
		most = std::max(most, teams.CountItems());
		if (teams.CountItems() > 0)
			seen++;
		if (IsEnriched(books[0]) && IsEnriched(books[1]) && IsEnriched(books[2]) && teams.CountItems() == 0)
			break;
		snooze(50000);
	}

	CHECK(seen > 0, "the plugin was started");
	CHECK(most == 1, "never more than one plugin at the same time");
	int32 enriched = IsEnriched(books[0]) + IsEnriched(books[1]) + IsEnriched(books[2]);
	if (enriched == 3)
		printf("ok   all three books are enriched\n");
	else
		printf("     (%d of 3 books enriched: OpenLibrary not reachable?)\n", (int) enriched);

	command.SetTo("rm -rf '");
	command << dir << "'";
	system(command.String());

	printf("%s\n", failed == 0 ? "all ok" : "FAILED");
	return failed == 0 ? 0 : 1;
}
