/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Tests of the relation commands of sen_server, made from the outside: a client sends the messages and checks the
 * attributes of the files afterwards. Needs a running sen_server (started by relations.sh), the installed core ontology
 * and a Haiku to run on. Every test works in its own folder below $TEST_DIR/_senrel and removes it.
 */

#include <MimeType.h>
#include <string.h>

#include "TestHarness.h"
#include "TestSupport.h"

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

// ---- the relations that a new relation can be of

static bool
OffersRelation(const entry_ref& ref, const char* type)
{
	BMessage reply = Send(sen::cmd::kRelationsGetCompatible, &ref, NULL);
	const char* offered;
	for (int32 index = 0; reply.FindString(sen::key::kRelations, index, &offered) == B_OK; index++) {
		if (strcmp(offered, type) == 0)
			return true;
	}
	return false;
}

TEST(OnlyRelationsThatFitTheTypeOfTheFileAreOffered)
{
	Fixture f("compatible", 1);
	const char* kDocumentReference = "relation/x-vnd.sen-labs.relation.docref";
	const char* kTransitionTo = "relation/x-vnd.sen-labs.relation.music.transition.to";

	BNode node(&f.refs[0]);
	BNodeInfo info(&node);
	CHECK(info.SetType("text/plain") == B_OK);

	// a generic reference is possible from any file
	CHECK(OffersRelation(f.refs[0], kReference));
	// what is resolved at run time (a plugin finds it in the document) is not created by hand
	CHECK(!OffersRelation(f.refs[0], kDocumentReference));
	// a relation for songs is not offered at a text
	CHECK(!OffersRelation(f.refs[0], kTransitionTo));

	// but at an audio file, if the music ontology is installed
	CHECK(info.SetType("audio/x-wav") == B_OK);
	CHECK(OffersRelation(f.refs[0], kReference));
	if (BMimeType(kTransitionTo).IsInstalled())
		CHECK(OffersRelation(f.refs[0], kTransitionTo));
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
