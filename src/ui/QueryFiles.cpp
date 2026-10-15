/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "QueryFiles.h"

#include <sen/Sen.h>

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>

namespace sen {
namespace ui {

static const char* kTrackerSignature = "application/x-vnd.Be-TRAK";
static const char* kQueryMimeType = "application/x-vnd.Be-query";


status_t
MakeQueryFile(const char* name, const char* predicate, entry_ref* ref)
{
	BPath path;
	status_t result = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (result == B_OK)
		result = path.Append("sen");
	if (result == B_OK)
		result = path.Append(sen::config::kQueriesDir);
	if (result == B_OK)
		result = create_directory(path.Path(), 0755);
	if (result != B_OK)
		return result;

	BString fileName(name);
	fileName.ReplaceAll('/', '-');
	if (fileName.IsEmpty() || fileName[0] == '.')
		fileName.Prepend("_");

	BDirectory folder(path.Path());
	BFile file;
	result = folder.CreateFile(fileName.String(), &file, false);
	if (result == B_FILE_EXISTS) {
		BEntry old(&folder, fileName.String());
		old.Remove();
		result = folder.CreateFile(fileName.String(), &file, false);
	}
	if (result != B_OK)
		return result;

	// what the query window of Tracker reads: the predicate, and how to show it again for editing (by formula)
	BString query(predicate);
	file.WriteAttrString("_trk/qrystr", &query);
	file.WriteAttrString("_trk/qryinitstr", &query);
	int32 mode = 'Fbyq';
	file.WriteAttr("_trk/qryinitmode", B_INT32_TYPE, 0, &mode, sizeof(int32));
	BNodeInfo(&file).SetType(kQueryMimeType);

	BEntry entry(&folder, fileName.String());
	return entry.GetRef(ref);
}


status_t
OpenInTracker(const entry_ref& ref)
{
	BMessenger tracker(kTrackerSignature);
	if (!tracker.IsValid())
		return B_NOT_INITIALIZED;

	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &ref);
	return tracker.SendMessage(&open);
}


status_t
OpenRelationsInTracker(const entry_ref& file, const char* relationType)
{
	BMessenger tracker(kTrackerSignature);
	if (!tracker.IsValid())
		return B_NOT_INITIALIZED;

	BMessage open(sen::cmd::kOpenRelationsOfType);
	open.AddRef(sen::key::kSourceRef, &file);
	open.AddString(sen::key::kRelationType, relationType);
	BMessage reply;
	status_t result = tracker.SendMessage(&open, &reply, 5000000, 5000000);
	if (result != B_OK)
		return result;
	// (a Tracker that does not know the message says so)
	if (reply.what == B_MESSAGE_NOT_UNDERSTOOD)
		return B_NOT_SUPPORTED;
	return reply.GetInt32(sen::key::kResult, B_OK);
}


status_t
ShowInTracker(const entry_ref& ref)
{
	BMessenger tracker(kTrackerSignature);
	if (!tracker.IsValid())
		return B_NOT_INITIALIZED;

	// the folder of the file, with the file selected (Tracker does this for the file in the data: nodeRefToSelect)
	BEntry entry(&ref, true);
	BEntry parent;
	entry_ref parentRef;
	node_ref node;
	status_t result = entry.InitCheck();
	if (result == B_OK)
		result = entry.GetParent(&parent);
	if (result == B_OK)
		result = parent.GetRef(&parentRef);
	if (result == B_OK)
		result = entry.GetNodeRef(&node);
	if (result != B_OK)
		return result;

	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &parentRef);
	open.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
	return tracker.SendMessage(&open);
}

}	// namespace ui
}	// namespace sen
