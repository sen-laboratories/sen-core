/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "InfoWindow.h"

#include "StatItem.h"
#include "../stats/SenStats.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StringView.h>

#include <vector>

namespace sen {
namespace ui {

static const uint32 kGather = 'IWga';
static const uint32 kGathered = 'IWgd';
static const uint32 kActivate = 'IWac';


struct GatherResult {
	stats::Overview					overview;
	std::vector<stats::TypeGroup>	groups;
	std::vector<BString>			notes;
};


InfoWindow::InfoWindow()
	:
	BWindow(BRect(120, 120, 820, 660), "SEN Info", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fGathering(false)
{
	fList = new BOutlineListView("list", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	scroll->SetExplicitMinSize(BSize(640, 420));
	fStatus = new BStringView("status", "");
	fRefresh = new BButton("refresh", "Refresh", new BMessage(kGather));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(fRefresh)
		.End()
	.End();

	_Gather();
}


void
InfoWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kGather:
			_Gather();
			break;
		case kGathered:
			_Fill(message);
			break;
		case kActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


int32
InfoWindow::_GatherEntry(void* data)
{
	BMessenger* target = (BMessenger*)data;
	GatherResult* result = new GatherResult;
	result->overview = stats::GatherOverview();
	result->groups = stats::GatherTypes(&result->notes);

	BMessage done(kGathered);
	done.AddPointer("result", result);
	if (target->SendMessage(&done) != B_OK)
		delete result;	// (the window is gone)
	delete target;
	return 0;
}


void
InfoWindow::_Gather()
{
	if (fGathering)
		return;
	fGathering = true;
	fRefresh->SetEnabled(false);
	fStatus->SetText("Counting" B_UTF8_ELLIPSIS);

	thread_id thread = spawn_thread(_GatherEntry, "gather SEN info", B_LOW_PRIORITY, new BMessenger(this));
	if (thread >= 0)
		resume_thread(thread);
	else {
		fGathering = false;
		fRefresh->SetEnabled(true);
	}
}


void
InfoWindow::_Clear()
{
	// (the items of the whole tree, also the ones below an item that is closed; MakeEmpty() does not delete them)
	std::vector<BListItem*> items;
	for (int32 index = 0; index < fList->FullListCountItems(); index++)
		items.push_back(fList->FullListItemAt(index));
	fList->MakeEmpty();
	for (BListItem* item : items)
		delete item;
}


static BString
Number(int32 count)
{
	BString text;
	text << count;
	return text;
}


static const char*
GroupName(const BString& supertype)
{
	if (supertype == "meta")
		return "Meta types";
	if (supertype == "entity")
		return "Entities";
	if (supertype == "classification")
		return "Classifications";
	return "Relations";
}


static void
AddNode(BOutlineListView* list, const stats::TypeGroup& group, size_t index, uint32 level)
{
	const stats::TypeNode& node = group.types[index];
	list->AddItem(new StatItem(level, node.name, node.subtype, Number(node.files)));
	for (size_t child : node.children)
		AddNode(list, group, child, level + 1);
}


void
InfoWindow::_Fill(BMessage* message)
{
	GatherResult* result = NULL;
	if (message->FindPointer("result", (void**)&result) != B_OK || result == NULL)
		return;

	_Clear();

	const stats::Overview& overview = result->overview;
	fList->AddItem(new StatItem(0, overview.version, BString(), BString(), true));
	fList->AddItem(new StatItem(0, "Server", BString(), overview.serverRunning ? "running" : "not running"));
	fList->AddItem(new StatItem(0, "SEN objects", BString(), Number(overview.objects), true));
	for (const stats::VolumeObjects& volume : overview.volumes) {
		BString note;
		if (!volume.indexed)
			note = "not counted, no index for SEN:ID";
		fList->AddItem(new StatItem(1, volume.name, note, volume.indexed ? Number(volume.objects) : BString()));
	}
	fList->AddItem(new StatItem(0, "Types", BString(), BString(), true));

	for (const stats::TypeGroup& group : result->groups) {
		BString title(GroupName(group.supertype));
		fList->AddItem(new StatItem(1, title, BString(), BString(), true));
		for (size_t root : group.roots)
			AddNode(fList, group, root, 2);
	}
	ExpandAllItems(fList);

	BString status(stats::TypesSummary(overview));
	for (const BString& name : result->notes)
		status << "  " B_UTF8_BULLET "  " << name << ": only files with a SEN:ID counted";
	fStatus->SetText(status.String());

	delete result;
	fGathering = false;
	fRefresh->SetEnabled(true);
}

}	// namespace ui
}	// namespace sen
