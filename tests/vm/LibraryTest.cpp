/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * The Library of a context (kContextLibrary, SenLibrary.h): the default place of the default context and of another one, the folder of a type (named by the type, by its
 * `SEN:PLURAL`, else by its description), a context that names its Library, the refusals, and that nothing is made when it is not asked for. Runs against the sen_server that is
 * running (library.sh starts the one under test); the contexts, types and folders that it makes are removed.
 */

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>

#include <sen/Sen.h>
#include <sen/SenLibrary.h>

static int sFailures = 0;
static int sChecks = 0;

#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			sFailures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

static std::string
Home()
{
	BPath home;
	find_directory(B_USER_DIRECTORY, &home);
	return home.Path();
}

static BPath
ContextFolder(const char* name)
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("sen");
	path.Append(sen::config::kContextsDir);
	path.Append(name);
	return path;
}

static status_t
Ask(const char* context, const char* type, bool create, BMessage* reply)
{
	BMessage request(sen::cmd::kContextLibrary);
	if (context != NULL)
		request.AddString(sen::key::kContext, context);
	if (type != NULL)
		request.AddString(sen::key::kType, type);
	request.AddBool(sen::key::kCreate, create);
	status_t result = BMessenger(sen::kServerSignature).SendMessage(&request, reply, 5000000, 5000000);
	return result != B_OK ? result : (status_t)reply->GetInt32(sen::key::kResult, -1);
}

static bool
Exists(const std::string& path)
{
	return BEntry(path.c_str()).Exists();
}

static void
RemoveTree(const std::string& path)
{
	std::string command = "rm -rf '" + path + "'";
	system(command.c_str());
}

int main()
{
	BApplication app("application/x-vnd.sen-labs.library-test");
	const std::string home = Home();
	const std::string library = home + "/Library";
	const bool hadLibrary = Exists(library);
	const std::string person = "entity/x-vnd.sen-labs.entity.person";

	// a context to make the folders in (the folder of a context is what makes it one)
	BMessage added;
	BMessage add(sen::cmd::kClassificationAdd);
	add.AddString(sen::key::kName, "libtest");
	add.AddString(sen::key::kType, "classification/x-vnd.sen-labs.entity.context");
	CHECK(BMessenger(sen::kServerSignature).SendMessage(&add, &added, 5000000, 5000000) == B_OK);
	CHECK(Exists(ContextFolder("libtest").Path()));

	// the default context: ~/Library; its folder for a type
	BMessage reply;
	CHECK(Ask("default", NULL, true, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")) == library);
	CHECK(std::string(reply.GetString(sen::key::kContext, "")) == "default");
	entry_ref ref;
	CHECK(reply.FindRef("refs", &ref) == B_OK && BPath(&ref).Path() == library);
	reply.MakeEmpty();
	// (a folder that was there stays; one that this test makes goes again)
	CHECK(Ask("default", person.c_str(), false, &reply) == B_OK);
	const bool hadPeople = Exists(reply.GetString(sen::key::kLibraryPath, ""));
	reply.MakeEmpty();
	CHECK(Ask("default", person.c_str(), true, &reply) == B_OK);
	const std::string peopleFolder = reply.GetString(sen::key::kLibraryPath, "");
	// (the installed ontology names it, `People`; an older one has the description of the type, `Person`; none the end of the name of the type)
	CHECK(peopleFolder == library + "/People" || peopleFolder == library + "/Person" || peopleFolder == library + "/x-vnd.sen-labs.entity.person");
	CHECK(Exists(peopleFolder));

	// another context: ~/Library/<name>
	reply.MakeEmpty();
	CHECK(Ask("libtest", person.c_str(), true, &reply) == B_OK);
	const std::string mine = library + "/libtest";
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")).compare(0, mine.size() + 1, mine + "/") == 0);
	CHECK(std::string(reply.GetString(sen::key::kContext, "")) == "libtest");
	CHECK(Exists(reply.GetString(sen::key::kLibraryPath, "")));

	// the same through the header that plugins use
	entry_ref folder;
	BString used;
	CHECK(sen::library::FolderFor(person.c_str(), &folder, "libtest", true, &used) == B_OK);
	CHECK(BPath(&folder).Path() == std::string(reply.GetString(sen::key::kLibraryPath, "")) && used == "libtest");
	CHECK(sen::library::FolderFor(NULL, &folder, "libtest") == B_OK && BPath(&folder).Path() == mine);

	// the current context, when none is named: some context that exists
	reply.MakeEmpty();
	CHECK(Ask(NULL, person.c_str(), true, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kContext, "")) != "");

	// the folder of a type: the plural of the type (SEN:PLURAL) (a type of the test, with the attribute), else its description, else the end of its name
	BMimeType gadget("entity/x-vnd.sen-test.gadget");
	CHECK(gadget.Install() == B_OK);
	gadget.SetShortDescription("Gadget");
	BPath mimeFile;
	find_directory(B_USER_SETTINGS_DIRECTORY, &mimeFile);
	mimeFile.Append("mime_db/entity/x-vnd.sen-test.gadget");
	BNode mimeNode(mimeFile.Path());
	CHECK(mimeNode.InitCheck() == B_OK);
	reply.MakeEmpty();
	CHECK(Ask("libtest", "entity/x-vnd.sen-test.gadget", true, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")) == mine + "/Gadget");
	const std::string gadgets = "Gadgets";
	mimeNode.WriteAttr(sen::attr::kPlural, B_STRING_TYPE, 0, gadgets.c_str(), gadgets.size() + 1);
	reply.MakeEmpty();
	CHECK(Ask("libtest", "entity/x-vnd.sen-test.gadget", true, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")) == mine + "/Gadgets");
	reply.MakeEmpty();
	CHECK(Ask("libtest", "entity/x-vnd.sen-test.unknown", true, &reply) == B_OK);			// (not installed: the end of its name)
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")) == mine + "/x-vnd.sen-test.unknown");

	// a context that names its Library
	BNode contextNode(ContextFolder("libtest").Path());
	const std::string elsewhere = "~/Library/Elsewhere-libtest";
	contextNode.WriteAttr(sen::attr::kLibraryPath, B_STRING_TYPE, 0, elsewhere.c_str(), elsewhere.size() + 1);
	reply.MakeEmpty();
	CHECK(Ask("libtest", person.c_str(), true, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")).compare(0, library.size() + 18, library + "/Elsewhere-libtest") == 0);
	contextNode.RemoveAttr(sen::attr::kLibraryPath);

	// nothing is made when it is not asked for: the path is the answer
	reply.MakeEmpty();
	CHECK(Ask("libtest", "entity/x-vnd.sen-test.nocreate", false, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")) == mine + "/x-vnd.sen-test.nocreate");
	CHECK(!Exists(mine + "/x-vnd.sen-test.nocreate"));
	if (reply.HasRef("refs"))
		reply.PrintToStream();
	CHECK(!reply.HasRef("refs"));

	// the refusals
	reply.MakeEmpty();
	CHECK(Ask("libtest", "application/pdf", true, &reply) == B_BAD_VALUE);					// (not an entity type)
	CHECK(Ask("libtest", "entity/", true, &reply) == B_BAD_VALUE);
	CHECK(Ask("no such context", person.c_str(), true, &reply) == B_ENTRY_NOT_FOUND);
	CHECK(Ask("../outside", person.c_str(), true, &reply) == B_BAD_VALUE);
	CHECK(!Exists(library + "/no such context"));

	// the name of the Library folder is a setting (kCoreSettingsSet); a name that is not one folder is refused and the Library stays where it was
	auto setName = [](const char* name, BMessage* answer) {
		BMessage changes;
		changes.AddString("SEN:libraryName", name);
		BMessage request(sen::cmd::kCoreSettingsSet);
		request.AddString(sen::key::kScope, sen::key::kScopeCore);
		request.AddMessage("settings", &changes);
		return BMessenger(sen::kServerSignature).SendMessage(&request, answer, 5000000, 5000000);
	};
	BMessage answer;
	CHECK(setName("LibraryZz", &answer) == B_OK && answer.GetInt32(sen::key::kResult, -1) == B_OK);
	reply.MakeEmpty();
	CHECK(Ask("default", person.c_str(), false, &reply) == B_OK);
	CHECK(std::string(reply.GetString(sen::key::kLibraryPath, "")).compare(0, home.size() + 11, home + "/LibraryZz/") == 0);
	reply.MakeEmpty();
	CHECK(Ask("libtest", NULL, false, &reply) == B_OK && std::string(reply.GetString(sen::key::kLibraryPath, "")) == home + "/LibraryZz/libtest");
	answer.MakeEmpty();
	CHECK(setName("a/b", &answer) == B_OK && answer.GetInt32(sen::key::kResult, -1) == B_BAD_VALUE);
	reply.MakeEmpty();
	CHECK(Ask("default", NULL, false, &reply) == B_OK && std::string(reply.GetString(sen::key::kLibraryPath, "")) == home + "/LibraryZz");
	answer.MakeEmpty();
	CHECK(setName("Library", &answer) == B_OK && answer.GetInt32(sen::key::kResult, -1) == B_OK);			// (the default name again)
	reply.MakeEmpty();
	CHECK(Ask("default", NULL, false, &reply) == B_OK && std::string(reply.GetString(sen::key::kLibraryPath, "")) == library);
	CHECK(!Exists(home + "/LibraryZz"));																	// (nothing was made: the path was asked for only)

	// put everything back
	gadget.Delete();
	if (!hadPeople)
		RemoveTree(peopleFolder);
	RemoveTree(mine);
	RemoveTree(home + "/Library/Elsewhere-libtest");
	RemoveTree(ContextFolder("libtest").Path());
	if (!hadLibrary) {
		RemoveTree(library);
	} else {
		// (the folder of the type in the default Library is left if it was there: only what was made here goes)
	}

	printf("%d checks, %d failure(s)\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
