/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenSettingsWindow.h"

#include "../relations/QueryUtil.h"

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>
#include <sen/SenSettingsFormHost.h>

#include <AppFileInfo.h>
#include <Button.h>
#include <ControlLook.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <MessageRunner.h>
#include <OutlineListView.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextView.h>

#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

namespace form = sen::settings::form;

namespace sen {
namespace settings {

namespace {

enum {
	kMsgSelect		= 'SSse',	// a node of the tree was selected
	kMsgReady		= 'SSrd',	// what a request thread got: serial, result, reply
	kMsgRevert		= 'SSrv',
	kMsgDefaults	= 'SSdf',
	kMsgOpenFile	= 'SSof',
	kMsgTimeout		= 'SStm'	// nothing came for the request: serial
};

const bigtime_t kAnswerTimeout = 8000000;		// a plugin that has to be started may take a while
const bigtime_t kStartTimeout = 12000000;	// (a plugin that was started for the request answers to the window, which waits this long)

}	// namespace


/** What a node of the tree stands for. */
struct SettingsWindow::Target {
	bool		core = false;
	BString		signature;
	BString		name;
};


namespace {

class NodeItem : public BStringItem {
public:
	NodeItem(const char* label, uint32 level, bool selectable, bool core, const char* signature)
		:
		BStringItem(label, level, true),
		fSelectable(selectable),
		fCore(core),
		fSignature(signature)
	{
	}

	bool			IsSelectable() const { return fSelectable; }
	bool			IsCore() const { return fCore; }
	const BString&	Signature() const { return fSignature; }

private:
	bool		fSelectable;
	bool		fCore;
	BString		fSignature;
};


/** What a request thread needs; the thread owns it. */
struct Request {
	BMessenger	window;
	int32		serial;
	bool		core;
	BString		signature;
};


/**
 * Asks for the settings: of the server, or of a plugin that is running. A plugin that is not is started with the request, which carries where to send the answer
 * (the roster delivers a message that nobody waits for a reply to). The window gets kMsgReady either way.
 */
int32
RunRequest(void* data)
{
	Request* request = static_cast<Request*>(data);
	BMessage ready(kMsgReady);
	ready.AddInt32("serial", request->serial);

	status_t result = B_OK;
	BMessage reply;
	if (request->core) {
		BMessenger server(sen::kServerSignature);
		BMessage ask(sen::cmd::kCoreSettings);
		result = server.IsValid() ? server.SendMessage(&ask, &reply, kAnswerTimeout, kAnswerTimeout) : B_NAME_NOT_FOUND;
	} else {
		BMessenger plugin(request->signature.String());
		BMessage ask(sensei::cmd::kSettings);
		if (plugin.IsValid()) {
			result = plugin.SendMessage(&ask, &reply, kAnswerTimeout, kAnswerTimeout);
		} else {
			// started for the request: its answer comes to the window, which is told with a message of its own
			ask.AddMessenger(sensei::key::kReplyTo, request->window);
			ask.AddInt32("serial", request->serial);
			result = be_roster->Launch(request->signature.String(), &ask);
			if (result == B_OK) {
				delete request;
				return 0;		// (kSettingsReply comes to the window)
			}
		}
	}
	ready.AddInt32(form::kFieldResult, result);
	ready.AddMessage("reply", &reply);
	request->window.SendMessage(&ready);
	delete request;
	return 0;
}


/** The name of a plugin as it shows: the short description of its version, else the name of its file. */
BString
PluginName(const entry_ref& ref, BString* signature)
{
	BFile file(&ref, B_READ_ONLY);
	BAppFileInfo info(&file);
	char text[B_MIME_TYPE_LENGTH];
	signature->SetTo("");
	if (info.InitCheck() == B_OK && info.GetSignature(text) == B_OK)
		signature->SetTo(text);
	version_info version;
	if (info.InitCheck() == B_OK && info.GetVersionInfo(&version, B_APP_VERSION_KIND) == B_OK && version.short_info[0] != '\0')
		return BString(version.short_info);
	return BString(ref.name);
}


BString
Capitalized(const char* word)
{
	BString text(word);
	text.CapitalizeEachWord();
	return text;
}

}	// namespace


SettingsWindow::SettingsWindow()
	:
	BWindow(BRect(140, 140, 780, 580), "SEN Settings", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE | B_CLOSE_ON_ESCAPE),
	fTree(NULL),
	fPane(NULL),
	fForm(NULL),
	fStatus(NULL),
	fRevert(NULL),
	fDefaults(NULL),
	fNote(NULL),
	fCore(true),
	fHasDefaults(false),
	fModified(false),
	fWaiting(false),
	fSerial(0),
	fTimeout(NULL)
{
	fTree = new BOutlineListView("tree", B_SINGLE_SELECTION_LIST);
	fTree->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* treeScroll = new BScrollView("tree scroll", fTree, 0, false, true, B_FANCY_BORDER);
	treeScroll->SetExplicitMinSize(BSize(be_plain_font->StringWidth("M") * 16, B_SIZE_UNSET));
	treeScroll->SetExplicitPreferredSize(BSize(be_plain_font->StringWidth("M") * 22, B_SIZE_UNSET));

	// the form is loaded from Shoji; without it there is a note in the pane
	// (SEN_FORM_SIGNATURE: the application that has the form, for testing a build of Shoji that is not the installed one)
	status_t error = B_OK;
	const char* formSignature = getenv("SEN_FORM_SIGNATURE");
	fForm = form::Instantiate("settings form", &error, formSignature != NULL && formSignature[0] != '\0' ? formSignature : form::kShojiSignature);
	fPane = new BGroupView("pane", B_VERTICAL, B_USE_DEFAULT_SPACING);
	fPane->GroupLayout()->SetInsets(B_USE_WINDOW_INSETS);
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);
	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fDefaults = new BButton("defaults", "Defaults", new BMessage(kMsgDefaults));

	BGroupView* buttons = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	buttons->GroupLayout()->AddView(fStatus);
	buttons->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	buttons->GroupLayout()->AddView(fDefaults);
	buttons->GroupLayout()->AddView(fRevert);

	fNote = new BStringView("note", "");
	fNote->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	if (fForm != NULL)
		fPane->GroupLayout()->AddView(fForm);
	fPane->GroupLayout()->AddView(fNote);
	fPane->GroupLayout()->AddView(buttons);
	fNote->Hide();

	BSplitView* split = new BSplitView(B_HORIZONTAL, 0);
	split->AddChild(treeScroll, 0.25f);
	split->AddChild(fPane, 0.75f);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(split).End();

	_BuildTree();
	if (fForm == NULL) {
		BString text;
		text << "The settings form needs Shoji (the package shoji) in a version that has it"
			<< (error == B_NOT_SUPPORTED ? ": the Shoji that is installed is too old." : ": it is not installed.");
		_ShowNote(text.String(), true);
	}
	_UpdateButtons();
}


SettingsWindow::~SettingsWindow()
{
	delete fTimeout;
}


bool
SettingsWindow::QuitRequested()
{
	// the plugins that have a settings session are told that it is over (a plugin that was started for it ends)
	for (const std::string& signature : fOpened) {
		BMessenger plugin(signature.c_str());
		if (plugin.IsValid())
			plugin.SendMessage(sensei::cmd::kSettingsClose);
	}
	return true;
}


// Core, and a node for every feature that plugins have, with the plugins that have it (a plugin is under each of its features).
void
SettingsWindow::_BuildTree()
{
	fTree->AddItem(new NodeItem("Core", 0, true, true, ""));
	fTree->AddItem(new NodeItem("Plugins", 0, false, false, ""));

	for (const char* feature : {sensei::feature::kExtract, sensei::feature::kEnrich, sensei::feature::kIdentify, sensei::feature::kNavigate, sensei::feature::kSearch}) {
		std::vector<entry_ref> plugins;
		if (sen::FindPlugins(feature, &plugins) != B_OK || plugins.empty())
			continue;

		std::map<std::string, std::string> byName;		// (sorted by the name that is shown)
		for (const entry_ref& ref : plugins) {
			BString signature;
			BString name = PluginName(ref, &signature);
			if (!signature.IsEmpty())
				byName[name.String()] = signature.String();
		}
		if (byName.empty())
			continue;
		fTree->AddItem(new NodeItem(Capitalized(feature).String(), 1, false, false, ""));
		for (const auto& plugin : byName)
			fTree->AddItem(new NodeItem(plugin.first.c_str(), 2, true, false, plugin.second.c_str()));
	}

	fTree->Select(0);
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelect:
		{
			NodeItem* item = dynamic_cast<NodeItem*>(fTree->ItemAt(fTree->CurrentSelection()));
			if (item == NULL)
				break;
			if (!item->IsSelectable()) {
				// a heading: the first plugin below it, or nothing
				break;
			}
			Target target;
			target.core = item->IsCore();
			target.signature = item->Signature();
			target.name = item->Text();
			_Select(&target);
			break;
		}
		case kMsgReady:
		case sensei::cmd::kSettingsReply:
			_Ready(message);
			break;
		case form::kChanged:
			_Changed(message);
			break;
		case form::kRefused:
		{
			BString text("Refused: ");
			text << message->GetString(form::kFieldPath, "") << " (" << message->GetString(form::kFieldDetail, "") << ")";
			_Status(text.String());
			break;
		}
		case kMsgRevert:
			if (fForm != NULL)
				form::Revert(fForm);
			break;
		case kMsgDefaults:
			if (fForm != NULL)
				form::Defaults(fForm);
			break;
		case kMsgTimeout:
			if (fWaiting && message->GetInt32("serial", 0) == fSerial) {
				fWaiting = false;
				BString text(fName);
				text << " does not answer.";
				_ShowNote(text.String());
				_Status("");
			}
			break;
		case kMsgOpenFile:
		{
			BPath path;
			if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append("sen") == B_OK && path.Append(sen::config::kSettingsFile) == B_OK) {
				entry_ref ref;
				if (get_ref_for_path(path.Path(), &ref) == B_OK)
					be_roster->Launch(&ref);
			}
			break;
		}
		default:
			// the answer to a change that was made: sent with this window as the one to reply to
			if (message->IsReply())
				_Answered(message);
			else
				BWindow::MessageReceived(message);
			break;
	}
}


void
SettingsWindow::_Select(const Target* target)
{
	fCore = target->core;
	fSignature = target->signature;
	fName = target->name;
	fModified = false;
	fHasDefaults = false;
	_Status("");
	_Request(*target);
	_UpdateButtons();
}


void
SettingsWindow::_Request(const Target& target)
{
	if (fForm == NULL)
		return;
	_Status("Reading the settings...");

	fWaiting = true;
	delete fTimeout;
	BMessage timeout(kMsgTimeout);
	timeout.AddInt32("serial", fSerial + 1);
	fTimeout = new BMessageRunner(BMessenger(this), &timeout, kStartTimeout, 1);

	Request* request = new Request();
	request->window = BMessenger(this);
	request->serial = ++fSerial;
	request->core = target.core;
	request->signature = target.signature;
	if (!target.core)
		fOpened.insert(target.signature.String());
	thread_id thread = spawn_thread(RunRequest, "settings request", B_NORMAL_PRIORITY, request);
	if (thread < 0 || resume_thread(thread) != B_OK)
		delete request;
}


// What a request got: the settings go into the form.
void
SettingsWindow::_Ready(BMessage* message)
{
	if (message->GetInt32("serial", 0) != fSerial)
		return;		// (of an older request: another item was chosen since)
	fWaiting = false;

	status_t result = B_OK;
	BMessage reply;
	if (message->what == kMsgReady) {
		result = message->GetInt32(form::kFieldResult, B_ERROR);
		message->FindMessage("reply", &reply);
		if (result == B_OK)
			result = reply.GetInt32(form::kFieldResult, B_OK);
	} else {
		reply = *message;
		result = reply.GetInt32(form::kFieldResult, B_OK);
	}

	BMessage settings;
	if (result != B_OK || reply.FindMessage(form::kFieldSettings, &settings) != B_OK) {
		BString text(fName);
		text << ": " << (result == B_OK || result == B_NOT_SUPPORTED || result == B_UNSUPPORTED ? "has no settings that can be set here."
			: result == B_TIMED_OUT ? "does not answer." : strerror(result));
		_ShowNote(text.String());
		_Status("");
		return;
	}

	fSettings = settings;
	fSchema.MakeEmpty();
	fDefaultsMessage.MakeEmpty();
	const bool hasSchema = reply.FindMessage(form::kFieldSchema, &fSchema) == B_OK;
	fHasDefaults = reply.FindMessage(form::kFieldDefaults, &fDefaultsMessage) == B_OK;
	form::SetData(fForm, settings, hasSchema ? &fSchema : NULL, fHasDefaults ? &fDefaultsMessage : NULL, BMessenger(this), true, fName.String());
	fNote->Hide();
	if (fForm->IsHidden())
		fForm->Show();
	_Status("");
	_UpdateButtons();
}


// The form tells a change: it goes to where the settings came from (a value alone, or the whole message after a revert or defaults).
void
SettingsWindow::_Changed(BMessage* message)
{
	BMessage settings;
	if (message->FindMessage(form::kFieldSettings, &settings) != B_OK)
		return;
	fSettings = settings;
	fModified = true;

	BMessage changes;
	const char* path;
	if (message->FindString(form::kFieldPath, &path) == B_OK)
		sen::settings::CopyValue(settings, path, &changes, path);
	else
		changes = settings;

	BMessage set(fCore ? sen::cmd::kCoreSettingsSet : sensei::cmd::kSettingsSet);
	set.AddMessage(form::kFieldSettings, &changes);
	BMessenger target(fCore ? sen::kServerSignature : fSignature.String());
	if (!target.IsValid() || target.SendMessage(&set, this) != B_OK)
		_Status("The settings could not be sent.");
	_UpdateButtons();
}


// What the owner of the settings says to a change: why a value was refused, and the settings as it now has them (shown if they are not what the form has).
void
SettingsWindow::_Answered(BMessage* message)
{
	const status_t result = message->GetInt32(form::kFieldResult, B_OK);
	const char* detail = message->GetString(sen::key::kDetail, "");
	if (result != B_OK)
		_Status(detail[0] != '\0' ? detail : strerror(result));
	else
		_Status("");

	BMessage settings;
	if (message->FindMessage(form::kFieldSettings, &settings) == B_OK && !settings.HasSameData(fSettings, true, true)) {
		fSettings = settings;
		form::SetData(fForm, settings, fSchema.IsEmpty() ? NULL : &fSchema, fHasDefaults ? &fDefaultsMessage : NULL, BMessenger(this), true, fName.String());
	}
}


void
SettingsWindow::_ShowNote(const char* text, bool offerFile)
{
	fNote->SetText(text);
	if (fNote->IsHidden())
		fNote->Show();
	if (fForm != NULL && !fForm->IsHidden())
		fForm->Hide();
	if (offerFile) {
		BButton* open = new BButton("open", "Open the settings file", new BMessage(kMsgOpenFile));
		open->SetTarget(this);
		fPane->GroupLayout()->AddView(open, 1);
	}
}


void
SettingsWindow::_Status(const char* text)
{
	fStatus->SetText(text);
}


void
SettingsWindow::_UpdateButtons()
{
	fRevert->SetEnabled(fForm != NULL && fModified);
	fDefaults->SetEnabled(fForm != NULL && fHasDefaults);
}

}	// namespace settings
}	// namespace sen
