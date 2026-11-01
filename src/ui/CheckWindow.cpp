/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CheckWindow.h"

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

static const uint32 kRunCheck = 'CWck';
static const uint32 kChecked = 'CWcd';
static const uint32 kActivate = 'CWac';


CheckWindow::CheckWindow()
	:
	BWindow(BRect(140, 140, 760, 560), "SEN Check", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fChecking(false),
	fWorker(new Worker("check SEN"))
{
	fList = new BOutlineListView("list", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	scroll->SetExplicitMinSize(BSize(560, 260));
	fStatus = new BStringView("status", "");
	fCheck = new BButton("check", "Check again", new BMessage(kRunCheck));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(fCheck)
		.End()
	.End();

	_Check();
}


CheckWindow::~CheckWindow()
{
	// (the worker quits when the check that it is running is done: the window does not wait for it)
	fWorker->Finish();
}


void
CheckWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kRunCheck:
			_Check();
			break;
		case kChecked:
			_Fill(message);
			break;
		case kActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


/** what the worker does: the check (the queries over all volumes), the result goes to the window as a pointer */
static BMessage*
Check()
{
	stats::CheckResult* result = new stats::CheckResult(stats::RunCheck());

	BMessage* done = new BMessage(kChecked);
	done->AddPointer("result", result);
	return done;
}


void
CheckWindow::_Check()
{
	if (fChecking)
		return;
	fChecking = true;
	fCheck->SetEnabled(false);
	fStatus->SetText("Checking" B_UTF8_ELLIPSIS);
	_Clear();

	// (the result of a window that is gone is freed here)
	status_t result = fWorker->Post(Check, BMessenger(this), [](BMessage* message) {
		stats::CheckResult* checked = NULL;
		if (message->FindPointer("result", (void**)&checked) == B_OK)
			delete checked;
	});
	if (result != B_OK) {
		fChecking = false;
		fCheck->SetEnabled(true);
	}
}


void
CheckWindow::_Clear()
{
	std::vector<BListItem*> items;
	for (int32 index = 0; index < fList->FullListCountItems(); index++)
		items.push_back(fList->FullListItemAt(index));
	fList->MakeEmpty();
	for (BListItem* item : items)
		delete item;
}


void
CheckWindow::_Fill(BMessage* message)
{
	stats::CheckResult* result = NULL;
	if (message->FindPointer("result", (void**)&result) != B_OK || result == NULL)
		return;

	_Clear();
	for (const stats::Finding& finding : result->findings) {
		fList->AddItem(new StatItem(0, finding.summary, BString(), BString(), true));
		for (const BString& detail : finding.details)
			fList->AddItem(new StatItem(1, detail));
	}
	ExpandAllItems(fList);

	BString status("Checked ");
	status << stats::Plural(result->objects, "SEN object", "SEN objects") << " on " << stats::Plural(result->volumes, "volume", "volumes")
		<< ": ";
	if (result->findings.empty())
		status << "no problems found";
	else
		status << stats::Plural((int32)result->findings.size(), "problem", "problems") << " found";
	fStatus->SetText(status.String());

	delete result;
	fChecking = false;
	fCheck->SetEnabled(true);
}

}	// namespace ui
}	// namespace sen
