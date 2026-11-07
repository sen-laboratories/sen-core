/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Dynamic relations between files (a relation type with `SEN:dynamic` that is not a self relation: the spatial ones): the relations are stored nowhere, the server asks the navigator
 * plugin that is the preferred application of the type (sensei::cmd::kResolve) when they are asked for, and answers like for a stored relation. The plugin here is a stub
 * (DynamicStub.cpp) with fixed content, started by dynamic-relations.sh. Runs against the sen_server under test.
 */

#include <Directory.h>
#include <MimeType.h>
#include <Path.h>
#include <FindDirectory.h>
#include <string.h>

#include <map>
#include <set>
#include <thread>
#include <vector>

#include <sen/SenOntoSpatial.h>

#include "TestHarness.h"
#include "TestSupport.h"

namespace {

const char* const kDynamicType = "relation/x-vnd.sen-labs.relation.test.dynamic";
const char* const kUnansweredType = "relation/x-vnd.sen-labs.relation.test.dynamic-unanswered";
const char* const kSourceFileType = "text/x-vnd.sen-test-place";
const char* const kStubSignature = "application/x-vnd.sen-plugin.sen-labs.test-dynamic-stub";

/** a relation type like those of the ontologies: dynamic, starting at files of one type, ending at places (an entity type, so the server gives the Library folder) */
void
InstallType(const char* typeName, const char* preferredApp)
{
	BMimeType type(typeName);
	type.Delete();
	type.Install();
	if (preferredApp != NULL)
		type.SetPreferredApp(preferredApp);

	BMessage label;
	label.what = 'SCrd';
	label.AddString(sen::attr::kRelationLabel, "within");
	BMessage inverse;
	inverse.what = 'SCrd';
	inverse.AddString(sen::attr::kRelationLabel, "contains");
	BMessage config;
	config.AddBool(sen::conf::kDynamic, true);
	config.AddMessage(sen::conf::kRelation, &label);
	config.AddMessage(sen::conf::kInverse, &inverse);
	config.AddString(sen::conf::kSourceTypes, kSourceFileType);
	config.AddString(sen::conf::kTargetTypes, sen::onto::spatial::mime::kPlace);
	config.AddString(sen::conf::kGroup, "Test");

	// the server reads the configuration from the attribute of the type in the MIME database of the user
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("mime_db");
	path.Append(typeName);
	BNode node(path.Path());
	ssize_t size = config.FlattenedSize();
	std::vector<char> buffer(size);
	config.Flatten(buffer.data(), size);
	node.WriteAttr(sen::attr::kRelationConfig, B_MESSAGE_TYPE, 0, buffer.data(), size);
}

void
RemoveType(const char* typeName)
{
	BMimeType(typeName).Delete();
}

/** the names of the relation types of the answer to kRelationsGetAll */
std::set<std::string>
TypesOf(const BMessage& reply)
{
	std::set<std::string> types;
	const char* type;
	for (int32 index = 0; reply.FindString(sen::key::kRelations, index, &type) == B_OK; index++)
		types.insert(type);
	return types;
}

struct Answer {
	BMessage reply;
	/** target name -> label */
	std::map<std::string, std::string> labels;
	std::map<std::string, BMessage> sets;
	std::map<std::string, entry_ref> refs;
};

Answer
Get(const entry_ref& source, const char* type)
{
	Answer answer;
	BMessage message(sen::cmd::kRelationsGet);
	message.AddRef(sen::key::kSourceRef, &source);
	message.AddString(sen::key::kRelationType, type);
	message.AddBool(sen::key::kIdToRefMap, true);
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid() || server.SendMessage(&message, &answer.reply, 60000000, 60000000) != B_OK)
		return answer;
	BMessage relations, idToRef;
	answer.reply.FindMessage(sen::key::kRelations, &relations);
	answer.reply.FindMessage(sen::key::kIdToRefMap, &idToRef);
	char* id;
	type_code code;
	for (int32 index = 0; relations.GetInfo(B_MESSAGE_TYPE, index, &id, &code) == B_OK; index++) {
		BMessage set;
		entry_ref ref;
		if (relations.FindMessage(id, 0, &set) == B_OK && idToRef.FindRef(id, &ref) == B_OK) {
			answer.labels[ref.name] = set.GetString(sen::attr::kRelationLabel, "");
			answer.sets[ref.name] = set;
			answer.refs[ref.name] = ref;
		}
	}
	return answer;
}

}	// namespace


// ---- tests

TEST(ADynamicRelationIsOfferedForTheFilesItStartsAt)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-offered", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	entry_ref other = f.Create("other", "text/plain");

	BMessage forPlace = Send(sen::cmd::kRelationsGetAll, &place, NULL);
	CHECK(TypesOf(forPlace).count(kDynamicType) == 1);
	// (nothing is stored at the file: the type is there because it starts at the files of this type)
	CHECK(!HasAttr(place, "SEN:REL:test.dynamic"));

	BMessage forOther = Send(sen::cmd::kRelationsGetAll, &other, NULL);
	CHECK(TypesOf(forOther).count(kDynamicType) == 0);
	RemoveType(kDynamicType);
}

TEST(TheTargetsAreWhatThePluginAnswers)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-targets", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	f.Create("target-a", "text/plain");
	f.Create("target-b", "text/plain");
	f.Create("target-c", "text/plain");

	Answer answer = Get(place, kDynamicType);
	CHECK_EQ(answer.labels.size(), (size_t) 3);
	// the label of the direction comes from the plugin: within, contains, within
	CHECK_STR(answer.labels["target-a"], "within");
	CHECK_STR(answer.labels["target-b"], "contains");
	CHECK_STR(answer.labels["target-c"], "within");
	CHECK_STR(answer.sets["target-a"].GetString(sen::attr::kRelationKind, ""), "STUB");
	// read-only: it is worked out, nobody can change it
	CHECK(answer.sets["target-a"].GetBool(sen::attr::kRelationReadOnly, false));
	// a target that does not exist is left out
	CHECK(answer.labels.count("target-that-is-not-there") == 0);
	// every target has a SEN:ID now, and the answer has the ref of the target for it
	for (const char* name : {"target-a", "target-b", "target-c"})
		CHECK(!IdOf(answer.refs[name]).empty());
	CHECK_EQ(answer.reply.GetInt32("count", -1), 3);
	RemoveType(kDynamicType);
}

TEST(NothingIsStoredAnywhere)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-nothing-stored", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	entry_ref target = f.Create("target-a", "text/plain");
	Get(place, kDynamicType);

	// neither the source nor the target has the relation stored (the type is not in their attribute of relations), and the source has no list of targets
	CHECK(Stored(place, kDynamicType).IsEmpty());
	CHECK(Stored(target, kDynamicType).IsEmpty());
	CHECK(List(place, sen::attr::kTo).empty());
	CHECK(List(target, sen::attr::kTo).empty());
	RemoveType(kDynamicType);
}

TEST(AskedAgainTheAnswerIsTheOneOfThatMoment)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-moment", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	f.Create("target-a", "text/plain");
	CHECK_EQ(Get(place, kDynamicType).labels.size(), (size_t) 1);
	// a file that appears is a target at once, one that goes is not any more: nothing was kept
	entry_ref added = f.Create("target-b", "text/plain");
	CHECK_EQ(Get(place, kDynamicType).labels.size(), (size_t) 2);
	BEntry(&added).Remove();
	CHECK_EQ(Get(place, kDynamicType).labels.size(), (size_t) 1);
	RemoveType(kDynamicType);
}

TEST(AFileWithNoTargetsHasNoRelationsAndNoError)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-none", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	BMessage reply = Send(sen::cmd::kRelationsGet, &place, kDynamicType);
	CHECK_EQ(reply.GetInt32(sen::key::kResult, -1), (int32) B_OK);
	Answer answer = Get(place, kDynamicType);
	CHECK(answer.labels.empty());
	RemoveType(kDynamicType);
}

TEST(ThePluginIsGivenTheLibraryFolders)
{
	// the server waits for the plugin, so the plugin cannot ask it for the Library: it is in the request (sensei::key::kLibraries), a folder for each entity type of the relation
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-libraries", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	f.Create("target-a", "text/plain");
	Answer answer = Get(place, kDynamicType);
	std::string note = answer.sets["target-a"].GetString("SEN:REL:Note", "");
	CHECK(note.find("libraries=1") != std::string::npos);
	CHECK(note.find("folder=Places") != std::string::npos);		// (the folder of the type Place in the Library of the current context)
	RemoveType(kDynamicType);
}

TEST(ATypeWithoutANavigatorFindsNothingAndIsNoError)
{
	InstallType(kUnansweredType, NULL);
	Fixture f("dyn-unanswered", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	f.Create("target-a", "text/plain");
	BMessage reply = Send(sen::cmd::kRelationsGet, &place, kUnansweredType);
	CHECK_EQ(reply.GetInt32(sen::key::kResult, -1), (int32) B_OK);
	CHECK(Get(place, kUnansweredType).labels.empty());
	RemoveType(kUnansweredType);
}

TEST(ADynamicRelationCannotBeCreatedByHand)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-not-created", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	entry_ref target = f.Create("target-a", "text/plain");

	// it is not among the relations that can be created at the file (New related...), and adding one is refused
	BMessage compatible(sen::cmd::kRelationsGetCompatible);
	compatible.AddRef(sen::key::kSourceRef, &place);
	BMessage reply;
	BMessenger server(sen::kServerSignature);
	server.SendMessage(&compatible, &reply, 10000000, 10000000);
	CHECK(TypesOf(reply).count(kDynamicType) == 0);

	BMessage added = Send(sen::cmd::kRelationAdd, &place, kDynamicType, &target);
	CHECK(SenStatus(added) >= 400 || added.GetInt32(sen::key::kResult, B_OK) != B_OK);
	CHECK(Stored(place, kDynamicType).IsEmpty());
	RemoveType(kDynamicType);
}

TEST(AStoredRelationIsNotAffectedByTheDynamicOnes)
{
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-stored", 2);
	// a stored relation between two files of the type that the dynamic one starts at: it is stored, and listed, as always
	entry_ref a = f.Create("a", kSourceFileType);
	entry_ref b = f.Create("b", kSourceFileType);
	BMessage added = Send(sen::cmd::kRelationAdd, &a, kReference, &b);
	CHECK_EQ(SenStatus(added), sen::status::kCreated);
	BMessage all = Send(sen::cmd::kRelationsGetAll, &a, NULL);
	std::set<std::string> types = TypesOf(all);
	CHECK(types.count(kReference) == 1);
	CHECK(types.count(kDynamicType) == 1);
	CHECK_EQ(Sets(a, kReference, IdOf(b)), 1);
	RemoveType(kDynamicType);
}

TEST(TheServerIsNotHeldUpWhileANavigatorWorks)
{
	// the navigator takes four seconds (the folder has a file "slow"); the server asks it from the channel of that plugin, and answers other requests at the same time
	InstallType(kDynamicType, kStubSignature);
	Fixture f("dyn-channel", 0);
	entry_ref place = f.Create("place", kSourceFileType);
	f.Create("target-a", "text/plain");
	f.Create("slow", "text/plain");

	Answer slow;
	bigtime_t slowTime = 0;
	std::thread asker([&]() {
		bigtime_t start = system_time();
		slow = Get(place, kDynamicType);
		slowTime = system_time() - start;
	});
	snooze(1000000);	// (the navigator is at work now)

	bigtime_t start = system_time();
	BMessage all = Send(sen::cmd::kRelationsGetAll, &place, NULL);
	bigtime_t quick = system_time() - start;
	CHECK_EQ(all.GetInt32(sen::key::kResult, -1), (int32) B_OK);
	CHECK(quick < 1500000);			// (not after the navigator: it has three more seconds)

	// a message of the kind that a plugin sends meanwhile (a new file is put into the context) is handled as well
	start = system_time();
	BMessage sticky = Send(sen::cmd::kClassificationApplySticky, &place, NULL);
	CHECK(system_time() - start < 1500000);

	asker.join();
	CHECK(slowTime >= 3500000);		// (the test did overlap with the work of the navigator)
	CHECK_EQ(slow.labels.size(), (size_t) 1);
	RemoveType(kDynamicType);
}

int
main()
{
	system((BString("mkdir -p '") << TestDir() << "'").String());
	for (auto& testCase : testing::Cases()) {
		int before = testing::Failures();
		testCase.body();
		printf("%s %s\n", testing::Failures() == before ? "ok  " : "FAIL", testCase.name);
	}
	system((BString("rm -rf '") << TestDir() << "'").String());
	printf("%d test(s), %d failure(s)\n", (int) testing::Cases().size(), testing::Failures());
	return testing::Failures() == 0 ? 0 : 1;
}
