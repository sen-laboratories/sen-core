/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * Tests of the relation folders of the SENryu Tracker (TrackerSenRelations, RelationFolders) with a live sen_server: stored
 * relations are shown as files, and what the user does with the files (delete, edit, drop, move) changes the relations.
 * The pose views only report what happens to the files; here the test reports it, the code under test is the same.
 * Started by tracker-folders.sh in a Haiku VM.
 */

#include <FindDirectory.h>
#include <OS.h>

#include "TestHarness.h"
#include "TestSupport.h"

#include "TrackerSenRelations.h"
#include "RelationFolders.h"

static const char* kContains = "relation/x-vnd.sen-labs.relation.contains";     // dynamic: not editable
static const char* kAuthorship = "relation/x-vnd.sen-labs.relation.authorship";  // stored, bidirectional

static node_ref
NodeOf(const entry_ref& ref)
{
	node_ref node;
	BEntry(&ref).GetNodeRef(&node);
	return node;
}

static entry_ref
RefOf(const BString& dir, const char* name)
{
	BPath path(dir.String());
	path.Append(name);
	entry_ref ref;
	BEntry(path.Path()).GetRef(&ref);
	return ref;
}

static bool
Exists(const entry_ref& ref)
{
	return BEntry(&ref).Exists();
}

static void
Relate(const entry_ref& source, const entry_ref& target, const BMessage* properties = NULL, const char* type = kReference)
{
	BMessage reply = Send(sen::cmd::kRelationAdd, &source, type, &target, properties);
	if (SenStatus(reply) < 200 || SenStatus(reply) >= 300)
		printf("  could not relate: status %d\n", (int) SenStatus(reply));
}

/** The watcher works in a thread of its own: wait (up to five seconds) until what the user did has arrived at the server. */
template<typename Condition>
static bool
WaitFor(Condition condition)
{
	for (int i = 0; i < 50; i++) {
		if (condition())
			return true;
		snooze(100000);
	}
	return condition();
}

/** the relation files are written by the Tracker; what it writes is not an edit by the user */
static void
LetTheFilesSettle()
{
	snooze(1700000);
}

static void
Shell(const BString& command)
{
	system(command.String());
}

// ---- stored relations are shown as files

TEST(StoredRelationsAreShownAsFilesNamedAfterTheirTargets)
{
	Fixture f("show", 3);
	BMessage page = Page(3);
	Relate(f.refs[0], f.refs[1], &page);
	Relate(f.refs[0], f.refs[2]);
	RelationFolders::Instance().Clear();

	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir), (status_t) B_OK);

	BPath path(&typeDir);
	CHECK(BString(path.Path()).FindFirst(view) >= 0);       // below the folder of this view
	entry_ref one = RefOf(path.Path(), "file1"), two = RefOf(path.Path(), "file2");
	CHECK(Exists(one));
	CHECK(Exists(two));

	// the relation file knows both ends, and its properties are attributes: sortable columns
	BNode node(&one);
	BString source, target;
	node.ReadAttrString(sen::attr::kRelationSource, &source);
	node.ReadAttrString(sen::attr::kRelationTarget, &target);
	CHECK_STR(source.String(), IdOf(f.refs[0]));
	CHECK_STR(target.String(), IdOf(f.refs[1]));
	int32 pageRead = 0;
	CHECK_EQ(node.ReadAttr("schema:pageStart", B_INT32_TYPE, 0, &pageRead, sizeof(pageRead)), (ssize_t) sizeof(pageRead));
	CHECK_EQ(pageRead, 3);
	// not as SEN:TO: the file would be taken for a file that links to the target
	CHECK(!HasAttr(one, sen::attr::kTo));

	CHECK(RelationFolders::Instance().IsRelationFolder(NodeOf(typeDir)));
	CHECK(RelationFolders::Instance().IsRelationFile(NodeOf(one)));
}

TEST(EveryViewHasItsOwnFolder)
{
	Fixture f("views", 2);
	Relate(f.refs[0], f.refs[1]);
	BString first, second;
	TrackerSenRelations::NewViewId(&first);
	TrackerSenRelations::NewViewId(&second);
	CHECK(first != second);
	CHECK(sen::id::IsValid(first.String()));

	entry_ref one, two;
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], first.String(), kReference, &one), (status_t) B_OK);
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], second.String(), kReference, &two), (status_t) B_OK);
	CHECK(!(one == two));
	CHECK(Exists(RefOf(BString(BPath(&one).Path()), "file1")));     // the first view is untouched by the second
	CHECK(Exists(RefOf(BString(BPath(&two).Path()), "file1")));
}

TEST(RelationsToAMissingFileAreMarked)
{
	Fixture f("missing", 2);
	Relate(f.refs[0], f.refs[1]);
	BEntry(&f.refs[1]).Remove();

	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir), (status_t) B_OK);

	BDirectory dir(&typeDir);
	BEntry entry;
	CHECK(dir.GetNextEntry(&entry) == B_OK);
	entry_ref file;
	entry.GetRef(&file);
	CHECK(BString(file.name).FindFirst("(missing)") >= 0);
	BNode node(&file);
	bool missing = false;
	CHECK_EQ(node.ReadAttr(sen::key::kTargetMissing, B_BOOL_TYPE, 0, &missing, sizeof(missing)), (ssize_t) sizeof(missing));
	CHECK(missing);
}

TEST(RelationsOfPluginsAreNotEditable)
{
	Fixture f("dynamic", 1);
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kContains, &typeDir), (status_t) B_NOT_SUPPORTED);
	// unknown files and folders are none of our business
	CHECK(!RelationFolders::Instance().EntryRemoved(NodeOf(f.refs[0])));
	CHECK(!RelationFolders::Instance().AttributesChanged(NodeOf(f.refs[0]), "schema:pageStart"));
	BMessage drop(B_SIMPLE_DATA);
	drop.AddRef("refs", &f.refs[0]);
	CHECK(!RelationFolders::Instance().HandleDrop(drop, NodeOf(f.refs[0])));
}

// ---- what the user does with the files

TEST(DeletingAFileRemovesTheRelation)
{
	Fixture f("delete", 3);
	Relate(f.refs[0], f.refs[1]);
	Relate(f.refs[0], f.refs[2]);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);

	// anyone deletes the file: the node monitor of the folder tells
	entry_ref file = RefOf(BPath(&typeDir).Path(), "file1");
	node_ref node = NodeOf(file);
	BEntry(&file).Remove();

	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]), c = IdOf(f.refs[2]);
	CHECK(WaitFor([&]() { return Sets(f.refs[0], kReference, b) == 0; }));
	CHECK(WaitFor([&]() { return Sets(f.refs[1], kReference, a) == 0; }));      // the opposite direction, too
	CHECK_EQ(Sets(f.refs[0], kReference, c), 1);                                // the other relation is untouched
	CHECK(!RelationFolders::Instance().IsRelationFile(node));
}

TEST(MovingAFileOutOfTheFolderRemovesTheRelation)
{
	Fixture f("trash", 2);
	Relate(f.refs[0], f.refs[1]);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);

	entry_ref file = RefOf(BPath(&typeDir).Path(), "file1");
	Shell(BString("mv '") << BPath(&file).Path() << "' '" << f.dir << "/moved-away'");      // Trash or any other folder
	CHECK(WaitFor([&]() { return Sets(f.refs[0], kReference, IdOf(f.refs[1])) == 0; }));
}

TEST(RenamingAFileIsNotAnEditOfTheRelation)
{
	Fixture f("rename", 2);
	Relate(f.refs[0], f.refs[1]);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);

	entry_ref file = RefOf(BPath(&typeDir).Path(), "file1");
	Shell(BString("mv '") << BPath(&file).Path() << "' '" << BPath(&typeDir).Path() << "/a new name'");
	snooze(1000000);
	CHECK_EQ(Sets(f.refs[0], kReference, IdOf(f.refs[1])), 1);      // the relation is still there

	// and the file is still the relation: deleting it under its new name removes it
	Shell(BString("rm '") << BPath(&typeDir).Path() << "/a new name'");
	CHECK(WaitFor([&]() { return Sets(f.refs[0], kReference, IdOf(f.refs[1])) == 0; }));
}

TEST(AFolderThatIsNotARelationFolderIsNotWatched)
{
	Fixture f("other", 2);
	Relate(f.refs[0], f.refs[1]);
	RelationFolders::Instance().Clear();
	// a file of the relation sits in a normal folder: deleting a normal file changes no relation
	entry_ref normal = f.Create("normal");
	BEntry(&normal).Remove();
	snooze(500000);
	CHECK_EQ(Sets(f.refs[0], kReference, IdOf(f.refs[1])), 1);
}

TEST(EditingAnAttributeChangesTheProperties)
{
	Fixture f("edit", 2);
	BMessage page = Page(3);
	Relate(f.refs[0], f.refs[1], &page);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);
	entry_ref file = RefOf(BPath(&typeDir).Path(), "file1");
	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]);

	// what the file system reports while the Tracker writes the file is not an edit
	LetTheFilesSettle();
	BMessage x, y;
	BMessage unchanged = Stored(f.refs[0], kReference);
	CHECK(unchanged.FindMessage(b.c_str(), &x) == B_OK);
	CHECK_EQ(x.GetInt32("schema:pageStart", 0), 3);

	// the user edits a column: the node monitor of the folder tells, no window needed
	BNode node(&file);
	int32 nine = 9;
	node.WriteAttr("schema:pageStart", B_INT32_TYPE, 0, &nine, sizeof(nine));

	CHECK(WaitFor([&]() {
		BMessage forward = Stored(f.refs[0], kReference), set;
		return forward.FindMessage(b.c_str(), &set) == B_OK && set.GetInt32("schema:pageStart", 0) == 9;
	}));
	BMessage inverse = Stored(f.refs[1], kReference);
	CHECK(inverse.FindMessage(a.c_str(), &y) == B_OK);
	CHECK_EQ(y.GetInt32("schema:pageStart", 0), 9);                         // the opposite direction, too

	// the identity of the relation is not a property
	BString other("x");
	node.WriteAttrString(sen::attr::kRelationTarget, &other);
	snooze(800000);
	CHECK_EQ(Sets(f.refs[0], kReference, b), 1);
}

TEST(AFileThatIsAddedLaterIsWatchedToo)
{
	Fixture f("later", 3);
	Relate(f.refs[0], f.refs[1]);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);

	// a relation is added by dropping a file; its file did not exist when the folder was watched
	BMessage drop(B_SIMPLE_DATA);
	drop.AddRef("refs", &f.refs[2]);
	CHECK(RelationFolders::Instance().HandleDrop(drop, NodeOf(typeDir)));
	LetTheFilesSettle();

	entry_ref file = RefOf(BPath(&typeDir).Path(), "file2");
	CHECK(Exists(file));
	BNode node(&file);
	int32 seven = 7;
	node.WriteAttr("schema:pageStart", B_INT32_TYPE, 0, &seven, sizeof(seven));

	std::string c = IdOf(f.refs[2]);
	CHECK(WaitFor([&]() {
		BMessage forward = Stored(f.refs[0], kReference), set;
		return forward.FindMessage(c.c_str(), &set) == B_OK && set.GetInt32("schema:pageStart", 0) == 7;
	}));

	BEntry(&file).Remove();
	CHECK(WaitFor([&]() { return Sets(f.refs[0], kReference, c) == 0; }));
}

TEST(DroppingAFileCreatesARelationAndItsFile)
{
	Fixture f("drop", 3);
	Relate(f.refs[0], f.refs[1]);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref typeDir;
	TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &typeDir);

	BMessage drop(B_SIMPLE_DATA);
	drop.AddRef("refs", &f.refs[2]);
	CHECK(RelationFolders::Instance().HandleDrop(drop, NodeOf(typeDir)));      // ours: Tracker must not copy the file

	std::string a = IdOf(f.refs[0]), c = IdOf(f.refs[2]);
	CHECK_EQ(Sets(f.refs[0], kReference, c), 1);
	CHECK_EQ(Sets(f.refs[2], kReference, a), 1);
	entry_ref file = RefOf(BPath(&typeDir).Path(), "file2");
	CHECK(Exists(file));
	CHECK(RelationFolders::Instance().IsRelationFile(NodeOf(file)));       // and it is editable like the others

	// dropping it again does not make another relation, nor another file
	CHECK(RelationFolders::Instance().HandleDrop(drop, NodeOf(typeDir)));
	CHECK_EQ(Sets(f.refs[0], kReference, c), 1);
	CHECK(!Exists(RefOf(BPath(&typeDir).Path(), "file2 #2")));
}

TEST(DroppingOnTheTopLevelCreatesAGenericRelation)
{
	Fixture f("toplevel", 2);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);

	// the folder of all types, as the Tracker makes it
	BPath root;
	find_directory(B_SYSTEM_TEMP_DIRECTORY, &root);
	root.Append("sen");
	root.Append(view.String());
	root.Append("relation");
	create_directory(root.Path(), 0777);
	RelationFolders::FolderInfo folder;
	BEntry rootEntry(root.Path());
	rootEntry.GetRef(&folder.ref);
	rootEntry.GetNodeRef(&folder.node);
	folder.sourceRef = f.refs[0];
	folder.sourceId = IdOf(f.refs[0]).c_str();
	folder.viewId = view;
	RelationFolders::Instance().RegisterFolder(folder);

	BMessage drop(B_SIMPLE_DATA);
	drop.AddRef("refs", &f.refs[1]);
	CHECK(RelationFolders::Instance().HandleDrop(drop, folder.node));

	CHECK_EQ(Sets(f.refs[0], kReference, IdOf(f.refs[1])), 1);
	entry_ref file = RefOf(BString(root.Path()) << "/x-vnd.sen-labs.relation.reference", "file1");
	CHECK(Exists(file));
	CHECK(RelationFolders::Instance().IsRelationFile(NodeOf(file)));
}

TEST(MovingARelationFileToAnotherTypeChangesTheType)
{
	Fixture f("move", 2);
	BMessage page = Page(4);
	Relate(f.refs[0], f.refs[1], &page);
	RelationFolders::Instance().Clear();
	BString view;
	TrackerSenRelations::NewViewId(&view);
	entry_ref referenceDir, authorshipDir;
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kReference, &referenceDir), (status_t) B_OK);
	CHECK_EQ(TrackerSenRelations::MaterializeType(f.refs[0], view.String(), kAuthorship, &authorshipDir), (status_t) B_OK);

	entry_ref file = RefOf(BPath(&referenceDir).Path(), "file1");
	BMessage drop(B_SIMPLE_DATA);
	drop.AddRef("refs", &file);
	CHECK(RelationFolders::Instance().HandleDrop(drop, NodeOf(authorshipDir)));

	std::string a = IdOf(f.refs[0]), b = IdOf(f.refs[1]);
	CHECK_EQ(Sets(f.refs[0], kReference, b), 0);
	CHECK_EQ(Sets(f.refs[1], kReference, a), 0);
	CHECK_EQ(Sets(f.refs[0], kAuthorship, b), 1);
	CHECK_EQ(Sets(f.refs[1], kAuthorship, a), 1);
	BMessage moved = Stored(f.refs[0], kAuthorship), set;
	CHECK(moved.FindMessage(b.c_str(), &set) == B_OK);
	CHECK_EQ(set.GetInt32("schema:pageStart", 0), 4);       // the properties came along

	// the file is in the folder of its new type, is of that type, and is registered there
	CHECK(!Exists(file));
	entry_ref now = RefOf(BPath(&authorshipDir).Path(), "file1");
	CHECK(Exists(now));
	BNode node(&now);
	char type[B_MIME_TYPE_LENGTH];
	BNodeInfo(&node).GetType(type);
	CHECK_STR(type, kAuthorship);
	CHECK(RelationFolders::Instance().IsRelationFile(NodeOf(now)));

	// and the pose view's report of the same move is not a deletion
	CHECK(RelationFolders::Instance().EntryMoved(NodeOf(now), NodeOf(authorshipDir)));
	CHECK_EQ(Sets(f.refs[0], kAuthorship, b), 1);
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
	system("rm -rf /system/cache/tmp/sen");
	printf("%d test(s), %d failure(s)\n", (int) testing::Cases().size(), testing::Failures());
	return testing::Failures() == 0 ? 0 : 1;
}
