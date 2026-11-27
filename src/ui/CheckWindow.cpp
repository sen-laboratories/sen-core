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

#include <sen/Sen.h>

#include <vector>

namespace sen {
namespace ui {

static const uint32 kRunCheck = 'CWck';
static const uint32 kChecked = 'CWcd';
static const uint32 kActivate = 'CWac';
static const uint32 kRepair = 'CWrp';
static const uint32 kRepaired = 'CWrr';


CheckWindow::CheckWindow()
	:
	BWindow(BRect(140, 140, 760, 560), "SEN Check & Repair", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fChecking(false),
	fRepairing(false),
	fWorker(new Worker("check SEN"))
{
	fList = new BOutlineListView("list", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	scroll->SetExplicitMinSize(BSize(560, 260));
	fStatus = new BStringView("status", "");
	fCheck = new BButton("check", "Check again", new BMessage(kRunCheck));
	fRepair = new BButton("repair", "Repair", new BMessage(kRepair));
	fRepair->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(fRepair)
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
		case kRepair:
			_Repair();
			break;
		case kRepaired:
		{
			status_t status = message->GetInt32("status", B_ERROR);
			fRepairNote = "";
			if (status == B_OK)
				fRepairNote << stats::Plural(message->GetInt32(sen::key::kIndicesRepaired, 0), "index", "indices") << " made again ("
					<< stats::Plural(message->GetInt32(sen::key::kAttributesRewritten, 0), "attribute", "attributes") << " written again), "
					<< stats::Plural(message->GetInt32(sen::key::kDanglingRemoved, 0), "reference", "references") << " to deleted files removed. ";
			else
				fRepairNote << "The repair did not succeed: " << message->GetString(sen::key::kDetail, strerror(status)) << " ";
			fRepairing = false;
			_Check();
			break;
		}
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


/** what the worker does for the repair: it asks the server (the message of the script) and waits for the answer, which comes when the repair is done */
static BMessage*
Repair()
{
	BMessage request(sen::cmd::kRepairIndices);
	BMessage reply;
	// (a repair of a large volume can take minutes)
	status_t status = BMessenger(sen::kServerSignature).SendMessage(&request, &reply, 30 * 60 * 1000000LL, 30 * 60 * 1000000LL);

	BMessage* done = new BMessage(kRepaired);
	if (status == B_OK)
		status = reply.GetInt32(sen::key::kResult, B_ERROR);
	done->AddInt32("status", status);
	// (the report only for a repair that did not succeed: it says what is still wrong)
	done->AddString(sen::key::kDetail, reply.GetString(sen::key::kDetail, strerror(status)));
	done->AddInt32(sen::key::kIndicesRepaired, reply.GetInt32(sen::key::kIndicesRepaired, 0));
	done->AddInt32(sen::key::kAttributesRewritten, reply.GetInt32(sen::key::kAttributesRewritten, 0));
	done->AddInt32(sen::key::kDanglingRemoved, reply.GetInt32(sen::key::kDanglingRemoved, 0));
	return done;
}


void
CheckWindow::_Repair()
{
	if (fChecking || fRepairing)
		return;
	fRepairing = true;
	fCheck->SetEnabled(false);
	fRepair->SetEnabled(false);
	fStatus->SetText("Repairing" B_UTF8_ELLIPSIS);
	if (fWorker->Post(Repair, BMessenger(this)) != B_OK) {
		fRepairing = false;
		fCheck->SetEnabled(true);
	}
}


void
CheckWindow::_Check()
{
	if (fChecking)
		return;
	fChecking = true;
	fCheck->SetEnabled(false);
	fRepair->SetEnabled(false);
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

	BString status(fRepairNote);
	fRepairNote = "";
	status << "Checked ";
	status << stats::Plural(result->objects, "SEN object", "SEN objects") << " on " << stats::Plural(result->volumes, "volume", "volumes")
		<< ": ";
	if (result->findings.empty())
		status << "no problems found";
	else
		status << stats::Plural((int32)result->findings.size(), "problem", "problems") << " found";
	fStatus->SetText(status.String());

	fRepair->SetEnabled(result->damagedIndices > 0 || result->danglingReferences > 0);
	delete result;
	fChecking = false;
	fCheck->SetEnabled(true);
}

}	// namespace ui
}	// namespace sen
