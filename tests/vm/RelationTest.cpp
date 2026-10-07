/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Tests of the relation commands of sen_server, made from the outside: a client sends the messages and checks the
 * attributes of the files afterwards. Needs a running sen_server (started by relations.sh), the installed core ontology
 * and a Haiku to run on. Every test works in its own folder below $TEST_DIR/_senrel and removes it.
 */

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <fs_attr.h>

#include <stdlib.h>

#include <string>
#include <vector>

#include <sen/Sen.h>

#include "TestHarness.h"
#include "../../src/relations/IdListAttr.h"

static const char* kReference = sen::mime::kReferenceRelation;
static const char* kAssociation = sen::mime::kAssociationRelation;
static const char* kLabelType = "classification/x-vnd.sen-labs.entity.label";

// ---- helpers

static BString
TestDir()
{
	const char* dir = getenv("TEST_DIR");
	return BString(dir != NULL ? dir : "/Develop/test") << "/_senrel";
}

/** A folder for one test with files that can be related. */
struct Fixture {
	BString		dir;
	std::vector<entry_ref> refs;

	Fixture(const char* name, int fileCount)
	{
		dir = TestDir();
		dir << "/" << name;
		BString command("rm -rf '");
		command << dir << "' && mkdir -p '" << dir << "'";
		system(command.String());

		for (int i = 0; i < fileCount; i++)
			refs.push_back(Create(BString("file") << i));
	}

	~Fixture()
	{
		BString command("rm -rf '");
		command << dir << "'";
		system(command.String());
	}

	entry_ref Create(const char* name, const char* mimeType = NULL)
	{
		BString path(dir);
		path << "/" << name;
		BFile file(path.String(), B_CREATE_FILE | B_READ_WRITE);
		if (mimeType != NULL) {
			BNodeInfo info(&file);
			info.SetType(mimeType);
		}
		BEntry entry(path.String());
		entry_ref ref;
		entry.GetRef(&ref);
		return ref;
	}
};

static std::string
IdOf(const entry_ref& ref)
{
	BNode node(&ref);
	BString id;
	node.ReadAttrString(sen::attr::kId, &id);
	return id.String();
}

static std::vector<std::string>
List(const entry_ref& ref, const char* base)
{
	BNode node(&ref);
	std::vector<std::string> ids;
	sen::ReadIdList(node, base, &ids);
	return ids;
}

/** the relations of a type stored with a file, empty if there are none */
static BMessage
Stored(const entry_ref& ref, const char* type)
{
	BString attr(sen::attr::kRelationPrefix);
	attr << (type + strlen(sen::mime::kRelationPrefix));

	BMessage relations;
	BNode node(&ref);
	attr_info info;
	if (node.GetAttrInfo(attr.String(), &info) != B_OK)
		return relations;
	std::vector<char> buffer(info.size);
	node.ReadAttr(attr.String(), B_MESSAGE_TYPE, 0, buffer.data(), info.size);
	relations.Unflatten(buffer.data());
	return relations;
}

static int32
Sets(const entry_ref& ref, const char* type, const std::string& targetId)
{
	BMessage relations = Stored(ref, type);
	type_code code;
	int32 count = 0;
	if (relations.GetInfo(targetId.c_str(), &code, &count) != B_OK)
		return 0;
	return count;
}

static bool
HasAttr(const entry_ref& ref, const char* name)
{
	BNode node(&ref);
	attr_info info;
	return node.GetAttrInfo(name, &info) == B_OK;
}

static BMessage
Send(uint32 what, const entry_ref* source, const char* type, const entry_ref* target = NULL,
	const BMessage* properties = NULL)
{
	BMessage message(what);
	if (source != NULL)
		message.AddRef(sen::key::kSourceRef, source);
	if (type != NULL)
		message.AddString(sen::key::kRelationType, type);
	if (target != NULL)
		message.AddRef(sen::key::kTargetRef, target);
	if (properties != NULL)
		message.AddMessage(sen::key::kRelationProperties, properties);

	BMessage reply;
	BMessenger server(sen::kServerSignature);
	if (!server.IsValid() || server.SendMessage(&message, &reply, 10000000, 10000000) != B_OK)
		reply.AddInt32(sen::key::kStatus, -1);
	return reply;
}

static int32
SenStatus(const BMessage& reply)
{
	return reply.GetInt32(sen::key::kStatus, -1);
}

static BMessage
Page(int32 page)
{
	BMessage properties;
	properties.AddInt32("schema:pageStart", page);
	return properties;
}

// ---- tests

TEST(RepliesCarryTheEnvelope)
{
	Fixture f("envelope", 2);
	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	CHECK(reply.HasInt32(sen::key::kStatus));
	CHECK(reply.HasInt32(sen::key::kResult));
	CHECK(reply.HasString(sen::key::kDetail));
	CHECK_EQ(reply.GetInt32(sen::key::kApiVersion, 0), sen::kApiVersion);
	CHECK_EQ(reply.GetInt32(sen::key::kResult, -1), (int32) B_OK);
}

TEST(AddStoresTheRelationAndItsOppositeDirection)
{
	Fixture f("add", 2);
	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kCreated);

	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]);
	CHECK(!a.empty() && !b.empty());
	CHECK(sen::id::IsValid(a.c_str()));

	// forward relation with a, listing b
	CHECK_EQ(Sets(f.refs[0], kReference, b), 1);
	CHECK(sen::idlist::Contains(List(f.refs[0], sen::attr::kTo), b));
	// the opposite direction with b, listing a, with the label of the opposite direction
	CHECK_EQ(Sets(f.refs[1], kReference, a), 1);
	CHECK(sen::idlist::Contains(List(f.refs[1], sen::attr::kTo), a));
	BMessage inverse = Stored(f.refs[1], kReference);
	BMessage set;
	CHECK(inverse.FindMessage(a.c_str(), &set) == B_OK);
	CHECK_STR(set.GetString(sen::attr::kRelationLabel, ""), "referenced by");
	// no relation id while it is the only relation to the target
	CHECK(!set.HasString(sen::key::kRelationId));
}

TEST(AddingTheSameRelationAgainChangesNothing)
{
	Fixture f("idempotent", 2);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);
	CHECK_EQ(Sets(f.refs[0], kReference, IdOf(f.refs[1])), 1);
	CHECK_EQ(Sets(f.refs[1], kReference, IdOf(f.refs[0])), 1);
	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), 1u);
}

TEST(SeveralRelationsToOneTargetGetRelationIdsInStepWithTheirOppositeDirection)
{
	Fixture f("several", 2);
	std::string b;
	BMessage one = Page(3), two = Page(9);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &one);
	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &two);
	CHECK_EQ(SenStatus(reply), sen::status::kCreated);
	CHECK(reply.HasString(sen::key::kRelationId));

	b = IdOf(f.refs[1]);
	std::string a = IdOf(f.refs[0]);
	CHECK_EQ(Sets(f.refs[0], kReference, b), 2);
	CHECK_EQ(Sets(f.refs[1], kReference, a), 2);
	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), 1u);   // one target, listed once

	BMessage forward = Stored(f.refs[0], kReference), inverse = Stored(f.refs[1], kReference);
	for (int32 i = 0; i < 2; i++) {
		BMessage x, y;
		CHECK(forward.FindMessage(b.c_str(), i, &x) == B_OK);
		CHECK(inverse.FindMessage(a.c_str(), i, &y) == B_OK);
		CHECK(x.HasString(sen::key::kRelationId));
		CHECK_STR(x.GetString(sen::key::kRelationId, "x"), y.GetString(sen::key::kRelationId, "y"));
		CHECK_EQ(x.GetInt32("schema:pageStart", 0), y.GetInt32("schema:pageStart", -1));
	}
}

TEST(RemovingAnAmbiguousRelationIsRefusedAndOneCanBeRemovedByItsId)
{
	Fixture f("remove", 2);
	BMessage one = Page(3), two = Page(9);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &one);
	BMessage added = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &two);
	std::string b = IdOf(f.refs[1]), a = IdOf(f.refs[0]);

	BMessage reply = Send(sen::cmd::kRelationRemove, &f.refs[0], kReference, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kErrAmbiguousRelation);
	CHECK_EQ(Sets(f.refs[0], kReference, b), 2);       // nothing changed

	BMessage remove(sen::cmd::kRelationRemove);
	remove.AddRef(sen::key::kSourceRef, &f.refs[0]);
	remove.AddString(sen::key::kRelationType, kReference);
	remove.AddRef(sen::key::kTargetRef, &f.refs[1]);
	remove.AddString(sen::key::kRelationId, added.GetString(sen::key::kRelationId, ""));
	BMessage removed;
	BMessenger(sen::kServerSignature).SendMessage(&remove, &removed, 10000000, 10000000);
	CHECK_EQ(SenStatus(removed), sen::status::kOk);

	CHECK_EQ(Sets(f.refs[0], kReference, b), 1);
	CHECK_EQ(Sets(f.refs[1], kReference, a), 1);
	// the one that is left is unique again: no relation id
	BMessage left, set;
	left = Stored(f.refs[0], kReference);
	CHECK(left.FindMessage(b.c_str(), &set) == B_OK);
	CHECK(!set.HasString(sen::key::kRelationId));
	CHECK_EQ(set.GetInt32("schema:pageStart", 0), 3);
}

TEST(RemovingTheLastRelationRemovesTheTargetAndBothDirectionsCompletely)
{
	Fixture f("removelast", 2);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	BMessage reply = Send(sen::cmd::kRelationRemove, &f.refs[0], kReference, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);

	CHECK(!HasAttr(f.refs[0], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	CHECK(!HasAttr(f.refs[1], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	CHECK(List(f.refs[0], sen::attr::kTo).empty());
	CHECK(List(f.refs[1], sen::attr::kTo).empty());
	CHECK(!HasAttr(f.refs[0], sen::attr::kTo));

	// removing what is not there says so
	reply = Send(sen::cmd::kRelationRemove, &f.refs[0], kReference, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kErrRelationNotFound);
}

TEST(UpdateChangesThePropertiesOnBothSides)
{
	Fixture f("update", 2);
	BMessage three = Page(3);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &three);

	BMessage nine = Page(9);
	BMessage reply = Send(sen::cmd::kRelationUpdate, &f.refs[0], kReference, &f.refs[1], &nine);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);

	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]);
	BMessage forward = Stored(f.refs[0], kReference), inverse = Stored(f.refs[1], kReference), x, y;
	CHECK(forward.FindMessage(b.c_str(), &x) == B_OK);
	CHECK(inverse.FindMessage(a.c_str(), &y) == B_OK);
	CHECK_EQ(x.GetInt32("schema:pageStart", 0), 9);
	CHECK_EQ(y.GetInt32("schema:pageStart", 0), 9);
	CHECK_STR(y.GetString(sen::attr::kRelationLabel, ""), "referenced by");    // the opposite direction keeps its label
}

TEST(UpdateMovesARelationToAnotherTarget)
{
	Fixture f("move", 3);
	BMessage page = Page(5);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1], &page);

	BMessage move(sen::cmd::kRelationUpdate);
	move.AddRef(sen::key::kSourceRef, &f.refs[0]);
	move.AddString(sen::key::kRelationType, kReference);
	move.AddRef(sen::key::kTargetRef, &f.refs[1]);
	move.AddRef(sen::key::kNewTargetRef, &f.refs[2]);
	BMessage reply;
	BMessenger(sen::kServerSignature).SendMessage(&move, &reply, 10000000, 10000000);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);

	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]), c = IdOf(f.refs[2]);
	CHECK_EQ(Sets(f.refs[0], kReference, b), 0);
	CHECK_EQ(Sets(f.refs[0], kReference, c), 1);
	CHECK(!sen::idlist::Contains(List(f.refs[0], sen::attr::kTo), b));
	CHECK(sen::idlist::Contains(List(f.refs[0], sen::attr::kTo), c));
	CHECK_EQ(Sets(f.refs[1], kReference, a), 0);        // the old target does not know the source any more
	CHECK_EQ(Sets(f.refs[2], kReference, a), 1);        // the new one does
	BMessage forward = Stored(f.refs[0], kReference), set;
	CHECK(forward.FindMessage(c.c_str(), &set) == B_OK);
	CHECK_EQ(set.GetInt32("schema:pageStart", 0), 5);    // the properties moved along
}

TEST(AssociationsAreStoredWithTheFileInSenMetaAndNotWithTheLabel)
{
	Fixture f("assoc", 2);
	entry_ref label = f.Create("label", kLabelType);
	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kAssociation, &label);
	CHECK_EQ(SenStatus(reply), sen::status::kCreated);

	std::string fileId = IdOf(f.refs[0]), labelId = IdOf(label);
	CHECK(sen::idlist::Contains(List(f.refs[0], sen::attr::kMeta), labelId));
	CHECK(List(f.refs[0], sen::attr::kTo).empty());                       // not with the normal relations
	CHECK(!HasAttr(label, "SEN:REL:x-vnd.sen-labs.relation.association"));  // the label holds nothing
	CHECK(List(label, sen::attr::kMeta).empty());
	CHECK(List(label, sen::attr::kTo).empty());

	// a label dropped on a file: still stored with the file
	reply = Send(sen::cmd::kRelationAdd, &label, kAssociation, &f.refs[1]);
	CHECK_EQ(SenStatus(reply), sen::status::kCreated);
	CHECK(sen::idlist::Contains(List(f.refs[1], sen::attr::kMeta), labelId));
	CHECK(!HasAttr(label, "SEN:REL:x-vnd.sen-labs.relation.association"));

	// what has the label is found by query for its id (reverse resolution)
	BMessage ask(sen::cmd::kRelationsGet);
	ask.AddRef(sen::key::kSourceRef, &label);
	ask.AddString(sen::key::kRelationType, kAssociation);
	BMessage found;
	BMessenger(sen::kServerSignature).SendMessage(&ask, &found, 10000000, 10000000);
	CHECK_EQ(SenStatus(found), sen::status::kOk);
	BMessage relations;
	CHECK(found.FindMessage(sen::key::kRelations, &relations) == B_OK);
	CHECK(relations.HasMessage(fileId.c_str()));
	CHECK(relations.HasMessage(IdOf(f.refs[1]).c_str()));
	CHECK_EQ(relations.CountNames(B_MESSAGE_TYPE), 2);

	// removing the association from the file
	reply = Send(sen::cmd::kRelationRemove, &f.refs[0], kAssociation, &label);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);
	CHECK(List(f.refs[0], sen::attr::kMeta).empty());
	CHECK(!HasAttr(f.refs[0], sen::attr::kMeta));
}

TEST(ListsOfTargetsContinueInTheNextAttributeAndAreFindableByQuery)
{
	int count = (int) sen::id::kMaxPerAttribute + 4;
	Fixture f("chunks", count + 1);
	for (int i = 1; i <= count; i++)
		CHECK_EQ(SenStatus(Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[i])), sen::status::kCreated);

	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), (size_t) count);
	CHECK(HasAttr(f.refs[0], "SEN:TO"));
	CHECK(HasAttr(f.refs[0], "SEN:TO:1"));
	BNode node(&f.refs[0]);
	BString first;
	node.ReadAttrString("SEN:TO", &first);
	CHECK(first.Length() <= 255);       // what BFS indexes

	// the last target is in the second attribute: a query still finds its source
	BMessage ask(sen::cmd::kRelationsGetAll);
	ask.AddRef(sen::key::kSourceRef, &f.refs[count]);
	BMessage reply;
	BMessenger(sen::kServerSignature).SendMessage(&ask, &reply, 10000000, 10000000);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);

	// removing one rewrites the lists
	CHECK_EQ(SenStatus(Send(sen::cmd::kRelationRemove, &f.refs[0], kReference, &f.refs[1])), sen::status::kOk);
	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), (size_t) count - 1);
}

TEST(AFullListIsRefusedAndNothingIsChanged)
{
	int full = (int) sen::idlist::kMaxIds;
	Fixture f("full", full + 2);
	for (int i = 1; i <= full; i++)
		Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[i]);
	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), (size_t) full);

	BMessage reply = Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[full + 1]);
	CHECK_EQ(SenStatus(reply), sen::status::kErrTooManyTargets);
	CHECK_EQ(List(f.refs[0], sen::attr::kTo).size(), (size_t) full);
	CHECK_EQ(Sets(f.refs[0], kReference, IdOf(f.refs[full + 1])), 0);
	// rolled back: the target did not keep a half written opposite direction
	CHECK(!HasAttr(f.refs[full + 1], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	CHECK(List(f.refs[full + 1], sen::attr::kTo).empty());
}

TEST(RemoveAllTakesTheRelationsOfAFileAwayFromBothSides)
{
	Fixture f("removeall", 4);
	for (int i = 1; i < 4; i++)
		Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[i]);
	std::string a = IdOf(f.refs[0]);

	BMessage reply = Send(sen::cmd::kRelationsRemoveAll, &f.refs[0], NULL);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);
	CHECK(!HasAttr(f.refs[0], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	CHECK(List(f.refs[0], sen::attr::kTo).empty());
	for (int i = 1; i < 4; i++) {
		CHECK_EQ(Sets(f.refs[i], kReference, a), 0);
		CHECK(!HasAttr(f.refs[i], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	}
	CHECK(!IdOf(f.refs[0]).empty());    // the file keeps its identity
}

TEST(ARelationToAFileThatIsGoneCanBeRemovedById)
{
	Fixture f("dangling", 2);
	Send(sen::cmd::kRelationAdd, &f.refs[0], kReference, &f.refs[1]);
	std::string b = IdOf(f.refs[1]);

	BPath gone(&f.refs[1]);
	BEntry(gone.Path()).Remove();

	BMessage remove(sen::cmd::kRelationRemove);
	remove.AddRef(sen::key::kSourceRef, &f.refs[0]);
	remove.AddString(sen::key::kRelationType, kReference);
	remove.AddString(sen::key::kTargetId, b.c_str());
	BMessage reply;
	BMessenger(sen::kServerSignature).SendMessage(&remove, &reply, 10000000, 10000000);
	CHECK_EQ(SenStatus(reply), sen::status::kOk);
	CHECK(!HasAttr(f.refs[0], "SEN:REL:x-vnd.sen-labs.relation.reference"));
	CHECK(List(f.refs[0], sen::attr::kTo).empty());
}

TEST(MessagesWithoutTheRequiredParametersAreAnswered)
{
	BMessage reply = Send(sen::cmd::kRelationAdd, NULL, NULL);
	CHECK(reply.HasInt32(sen::key::kStatus));
	CHECK(SenStatus(reply) >= 400);
	reply = Send(sen::cmd::kRelationRemove, NULL, NULL);
	CHECK(SenStatus(reply) >= 400);
	reply = Send(sen::cmd::kRelationUpdate, NULL, NULL);
	CHECK(SenStatus(reply) >= 400);
	// the server is still there
	CHECK(BMessenger(sen::kServerSignature).IsValid());
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
