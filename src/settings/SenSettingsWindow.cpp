/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenSettingsWindow.h"

#include "../relations/QueryUtil.h"

#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <sen/SenMessagePath.h>
#include <sen/SenOntoCore.h>
#include <sen/SenSettingsForm.h>
#include <sen/SenSettingsFormHost.h>

#include <AppFileInfo.h>
#include <Bitmap.h>
#include <Button.h>
#include <IconUtils.h>
#include <ControlLook.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <Node.h>
#include <MimeType.h>
#include <MessageRunner.h>
#include <NodeInfo.h>
#include <OutlineListView.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextView.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <set>
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
	bool		core = false;		///< the server answers (its own settings, or what all plugins share)
	BString		scope;			///< for the server: sen::key::kScopeCore or kScopePlugins
	BString		signature;
	BString		name;
	entry_ref	file;			///< of the plugin: what is started (its signature may be that of another copy)
};


/** The note in the pane where the form is not (no settings, reading, an error): its text in the middle of the whole pane, which it fills. */
class NoteView : public BView {
public:
	NoteView(const char* name)
		:
		BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
		SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}

	void SetText(const char* text)
	{
		fText = text;
		InvalidateLayout();
		Invalidate();
	}

	const char* Text() const { return fText.String(); }

	virtual BSize MinSize()
	{
		return BSize(be_plain_font->StringWidth("M") * 8, _TextHeight());
	}

	virtual BSize PreferredSize()
	{
		return BSize(StringWidth(fText.String()) + 2 * be_control_look->DefaultItemSpacing(), _TextHeight());
	}

	virtual void Draw(BRect)
	{
		font_height height;
		GetFontHeight(&height);
		BRect bounds = Bounds();
		// (a text that is wider than the pane is cut at the end, not at both sides)
		BString shown(fText);
		const float room = bounds.Width() - 2 * be_control_look->DefaultItemSpacing();
		if (StringWidth(shown.String()) > room)
			TruncateString(&shown, B_TRUNCATE_END, room);
		const float width = StringWidth(shown.String());
		MovePenTo(BPoint(floorf((bounds.Width() - width) / 2), floorf(bounds.Height() / 2 - (height.ascent + height.descent) / 2 + height.ascent)));
		DrawString(shown.String());
	}

private:
	float _TextHeight()
	{
		font_height height;
		GetFontHeight(&height);
		return ceilf(height.ascent + height.descent + height.leading) + 2 * be_control_look->DefaultItemSpacing();
	}

	BString		fText;
};


namespace {

/** The icon of a file (an application: its vector icon from its resources), as a bitmap of the size; NULL if it has none. */
BBitmap*
IconOf(const entry_ref& file, int size)
{
	BBitmap* icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (icon->InitCheck() == B_OK && BNodeInfo::GetTrackerIcon(&file, icon, (icon_size)size) == B_OK)
		return icon;
	delete icon;
	return NULL;
}


/** The icon of a MIME type (its vector icon, as the ontology gives it), as a bitmap of the size; NULL if the type has none. */
BBitmap*
IconOfType(const char* mimeType, int size)
{
	BMimeType type(mimeType);
	if (type.InitCheck() != B_OK)
		return NULL;
	uint8* data = NULL;
	size_t dataSize = 0;
	BBitmap* icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (icon->InitCheck() == B_OK && type.GetIcon(&data, &dataSize) == B_OK) {
		const status_t result = BIconUtils::GetVectorIcon(data, dataSize, icon);
		delete[] data;
		if (result == B_OK)
			return icon;
	}
	delete icon;
	return NULL;
}


/**
 * A node of the tree. One that has a file (Core: the server, a plugin: its application) shows the icon of that file before its name, the icon that its resources have; the
 * nodes that are headings have none. The icon is as high as the text needs, a little more.
 */
class NodeItem : public BStringItem {
public:
	/** @param scope what the server answers for (sen::key::kScopeCore, kScopePlugins), NULL for a plugin or a heading */
	NodeItem(const char* label, uint32 level, bool selectable, const char* scope, const char* signature, const entry_ref* file = NULL, const char* iconType = NULL)
		:
		BStringItem(label, level, true),
		fSelectable(selectable),
		fScope(scope != NULL ? scope : ""),
		fSignature(signature),
		fIcon(NULL)
	{
		// the icon of a file (the application), else that of a MIME type (the heading of the plugins: the type of a plugin)
		font_height height;
		be_plain_font->GetHeight(&height);
		const int size = (int)ceilf(height.ascent + height.descent + height.leading) + 2;
		if (file != NULL) {
			fFile = *file;
			fIcon = IconOf(fFile, size);
		} else if (iconType != NULL) {
			fIcon = IconOfType(iconType, size);
		}
	}

	virtual ~NodeItem() { delete fIcon; }

	virtual void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, font);
		if (fIcon != NULL) {
			SetHeight(std::max(Height(), fIcon->Bounds().Height() + 3));
			SetWidth(Width() + fIcon->Bounds().Width() + 1 + kIconGap);
		}
		// (what the outline list view puts before the name: the indent of the level and the latch, see BOutlineListView::LatchRect)
		SetWidth(Width() + OutlineLevel() * be_control_look->DefaultItemSpacing() + be_plain_font->Size());
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		if (fIcon == NULL) {
			BStringItem::DrawItem(owner, frame, complete);
			return;
		}

		// as BStringItem does it: the background, then the name; the icon comes before the name
		const rgb_color low = owner->LowColor();
		if (IsSelected() || complete) {
			owner->SetLowColor(IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR) : owner->ViewColor());
			owner->FillRect(frame, B_SOLID_LOW);
		}
		owner->SetDrawingMode(B_OP_ALPHA);
		owner->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		owner->DrawBitmap(fIcon, BPoint(frame.left + 2, frame.top + (frame.Height() - fIcon->Bounds().Height()) / 2));
		owner->SetDrawingMode(B_OP_COPY);

		font_height height;
		owner->GetFontHeight(&height);
		owner->SetHighColor(IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR) : ui_color(B_LIST_ITEM_TEXT_COLOR));
		owner->MovePenTo(frame.left + 2 + fIcon->Bounds().Width() + 1 + kIconGap,
			frame.top + (frame.Height() - (height.ascent + height.descent)) / 2 + height.ascent);
		owner->DrawString(Text());
		owner->SetLowColor(low);
	}

	const entry_ref&	File() const { return fFile; }

	bool			IsSelectable() const { return fSelectable; }
	bool			IsCore() const { return !fScope.IsEmpty(); }
	const BString&	Scope() const { return fScope; }
	const BString&	Signature() const { return fSignature; }

private:
	static constexpr float	kIconGap = 4;

	bool		fSelectable;
	BString		fScope;
	BString		fSignature;
	entry_ref	fFile;
	BBitmap*	fIcon;
};


/** Whether a plugin has settings that the window can show: its attribute `SEN:plugin:hasSettings` is there and not false. A plugin without the attribute is not listed (the plugins that
 *  have no settings are the older ones, which have no attribute). */
bool
HasSettings(const entry_ref& ref)
{
	BNode node(&ref);
	attr_info info;
	if (node.InitCheck() != B_OK || node.GetAttrInfo(sensei::kHasSettingsAttr, &info) != B_OK || info.size < 1 || info.size > 4)
		return false;
	uint8 value[4] = {0, 0, 0, 0};
	if (node.ReadAttr(sensei::kHasSettingsAttr, info.type, 0, value, info.size) != info.size)
		return false;
	for (off_t i = 0; i < info.size; i++) {
		if (value[i] != 0)
			return true;
	}
	return false;
}


/** What a request thread needs; the thread owns it. */
struct Request {
	BMessenger	window;
	int32		serial;
	BString		scope;
};


/** Asks the server for its settings (it answers at once); the window gets kMsgReady. A plugin is asked without a thread, see SettingsWindow::_Request(). */
int32
RunRequest(void* data)
{
	Request* request = static_cast<Request*>(data);
	BMessage ready(kMsgReady);
	ready.AddInt32("serial", request->serial);

	BMessage reply;
	BMessenger server(sen::kServerSignature);
	BMessage ask(sen::cmd::kCoreSettings);
	ask.AddString(sen::key::kScope, request->scope);
	status_t result = server.IsValid() ? server.SendMessage(&ask, &reply, kAnswerTimeout, kAnswerTimeout) : B_NAME_NOT_FOUND;
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


SettingsWindow::SettingsWindow(const entry_ref* pluginFile, const char* node)
	:
	BWindow(BRect(140, 140, 780, 580), "SEN Settings", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE | B_CLOSE_ON_ESCAPE),
	fTree(NULL),
	fPane(NULL),
	fForm(NULL),
	fStatus(NULL),
	fRevert(NULL),
	fDefaults(NULL),
	fNote(NULL),
	fFirstSelect(1),
	fCore(true),
	fHasDefaults(false),
	fModified(false),
	fWaiting(false),
	fStartedTeam(-1),
	fSerial(0),
	fTimeout(NULL)
{
	fTree = new BOutlineListView("tree", B_SINGLE_SELECTION_LIST);
	fTree->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* treeScroll = new BScrollView("tree scroll", fTree, 0, false, true, B_FANCY_BORDER);

	// the form is loaded from Shoji; without it there is a note in the pane
	// (SEN_FORM_SIGNATURE: the application that has the form, for testing a build of Shoji that is not the installed one)
	status_t error = B_OK;
	const char* formSignature = getenv("SEN_FORM_SIGNATURE");
	fForm = form::Instantiate("settings form", &error, formSignature != NULL && formSignature[0] != '\0' ? formSignature : form::kShojiSignature);
	fPane = new BGroupView("pane", B_VERTICAL, B_USE_DEFAULT_SPACING);
	fPane->GroupLayout()->SetInsets(B_USE_WINDOW_INSETS);
	// (the pane takes what the window gives it: a form of one option must not make the window as small as itself)
	fPane->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	if (fForm != NULL)
		fForm->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);
	fRevert = new BButton("revert", "Revert", new BMessage(kMsgRevert));
	fDefaults = new BButton("defaults", "Defaults", new BMessage(kMsgDefaults));

	BGroupView* buttons = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	buttons->GroupLayout()->AddView(fStatus);
	buttons->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
	buttons->GroupLayout()->AddView(fDefaults);
	buttons->GroupLayout()->AddView(fRevert);

	fNote = new NoteView("note");

	if (fForm != NULL)
		fPane->GroupLayout()->AddView(fForm);
	fPane->GroupLayout()->AddView(fNote);
	fPane->GroupLayout()->AddView(buttons);
	fNote->Hide();

	// the tree keeps the width that its widest item needs (weight 0), the pane takes the rest and grows with the window; a divider is between them
	_BuildTree(pluginFile, node);
	treeScroll->SetExplicitMinSize(BSize(_TreeWidth() + B_V_SCROLL_BAR_WIDTH + 2 * be_control_look->DefaultItemSpacing(), B_SIZE_UNSET));
	BSplitView* split = new BSplitView(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING);
	split->AddChild(treeScroll, 0.0f);
	split->AddChild(fPane, 1.0f);
	split->SetCollapsible(false);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0).Add(split).End();
	fTree->Select(fFirstSelect);

	// tall enough for all the nodes of the tree, but not more than fits comfortably on the screen
	float treeHeight = 0;
	for (int32 i = 0; i < fTree->FullListCountItems(); i++)
		treeHeight += fTree->FullListItemAt(i)->Height();
	const BRect screen = BScreen(this).Frame();
	const float comfortable = floorf(screen.Height() * 0.8f);
	const float wanted = treeHeight + 2 * be_control_look->DefaultItemSpacing() + 6;
	ResizeTo(Bounds().Width(), std::min(std::max(Bounds().Height(), wanted), comfortable));
	BRect frame = Frame();
	if (frame.bottom > screen.bottom - 20)
		MoveTo(frame.left, std::max(screen.top + 30, screen.bottom - 20 - frame.Height()));
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
	for (auto& session : fSessions) {
		if (session.second.IsValid())
			session.second.SendMessage(sensei::cmd::kSettingsClose);
	}
	return true;
}


// SEN (Core), and Plugins: the settings that all plugins share, with a node for every feature that plugins have below it and the plugins that have it (a plugin is under
// each of its features). A plugin is started by its file, the one that was found: its signature can be that of more than one copy. A file that was given
// (`sen_settings --plugin <file>`, for a developer of a plugin) replaces the file of the plugin with its signature, or is a node of its own.
void
SettingsWindow::_BuildTree(const entry_ref* pluginFile, const char* node)
{
	// (Core is the server: its icon)
	entry_ref server;
	const bool hasServer = be_roster->FindApp(sen::kServerSignature, &server) == B_OK;
	fTree->AddItem(new NodeItem("SEN", 0, false, NULL, "", hasServer ? &server : NULL));
	fTree->AddItem(new NodeItem("Core", 1, true, sen::key::kScopeCore, "", hasServer ? &server : NULL));
	fTree->AddItem(new NodeItem("Plugins", 0, true, sen::key::kScopePlugins, "", NULL, sen::onto::core::mime::kPlugin));

	BString givenSignature;
	BString givenName;
	if (pluginFile != NULL)
		givenName = PluginName(*pluginFile, &givenSignature);
	bool givenShown = false;
	int32 select = 1;		// (Core)

	struct Plugin {
		std::string		signature;
		entry_ref		file;
	};
	for (const char* feature : {sensei::feature::kExtract, sensei::feature::kEnrich, sensei::feature::kIdentify, sensei::feature::kNavigate, sensei::feature::kSearch}) {
		std::vector<entry_ref> plugins;
		if (sen::FindPlugins(feature, &plugins) != B_OK || plugins.empty())
			continue;

		std::map<std::string, Plugin> byName;		// (sorted by the name that is shown)
		std::set<std::string> seen;
		for (const entry_ref& ref : plugins) {
			BString signature;
			BString name = PluginName(ref, &signature);
			if (signature.IsEmpty() || !seen.insert(signature.String()).second)
				continue;
			// a plugin that does not say that it has settings is not listed (unless it is the one that was given: a developer wants to see it)
			if (!(pluginFile != NULL && signature == givenSignature) && !HasSettings(ref))
				continue;
			Plugin plugin;
			plugin.signature = signature.String();
			plugin.file = (pluginFile != NULL && signature == givenSignature) ? *pluginFile : ref;
			if (pluginFile != NULL && signature == givenSignature)
				givenShown = true;
			byName[name.String()] = plugin;
		}
		if (byName.empty())
			continue;
		fTree->AddItem(new NodeItem(Capitalized(feature).String(), 1, false, NULL, ""));
		for (const auto& plugin : byName) {
			if (pluginFile != NULL && plugin.second.signature == givenSignature.String() && select == 1)
				select = fTree->CountItems();
			fTree->AddItem(new NodeItem(plugin.first.c_str(), 2, true, NULL, plugin.second.signature.c_str(), &plugin.second.file));
		}
	}

	if (pluginFile != NULL && !givenShown) {
		fTree->AddItem(new NodeItem("From a file", 1, false, NULL, ""));
		select = fTree->CountItems();
		fTree->AddItem(new NodeItem(givenName.String(), 2, true, NULL, givenSignature.String(), pluginFile));
	}
	if (node != NULL && strcmp(node, "plugins") == 0 && pluginFile == NULL)
		select = 2;
	// (selected when the window is built: the message of the selection needs the form)
	fFirstSelect = select;
}


// The width that the items of the tree need: the widest one, with the indent of its level, the latch and its icon.
float
SettingsWindow::_TreeWidth()
{
	float widest = 0;
	for (int32 i = 0; i < fTree->FullListCountItems(); i++) {
		BListItem* item = fTree->FullListItemAt(i);
		item->Update(fTree, be_plain_font);
		widest = std::max(widest, item->Width());
	}
	return widest;
}


// The first node after the one at the index (of the full list) that can be selected: what a heading, or a node that has no settings, shows instead.
void
SettingsWindow::_SelectBelow(int32 fullIndex)
{
	for (int32 i = fullIndex + 1; i < fTree->FullListCountItems(); i++) {
		NodeItem* item = dynamic_cast<NodeItem*>(fTree->FullListItemAt(i));
		if (item == NULL || !item->IsSelectable())
			continue;
		BListItem* super = fTree->Superitem(item);
		if (super != NULL && !super->IsExpanded())
			fTree->Expand(super);
		fTree->Select(fTree->IndexOf(item));
		return;
	}
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
				// a heading shows the node below it (the selection of the list says what the pane shows)
				_SelectBelow(fTree->FullListIndexOf(item));
				break;
			}
			Target target;
			target.core = item->IsCore();
				target.scope = item->Scope();
			target.signature = item->Signature();
			target.name = item->Text();
			target.file = item->File();
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
				// (a plugin that does not know the request stays as an application with no work: it is asked to end)
				if (fStartedTeam >= 0)
					BMessenger(NULL, fStartedTeam).SendMessage(B_QUIT_REQUESTED);
				fStartedTeam = -1;
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
	fScope = target->scope;
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
	if (target.core) {
		_Status("Reading the settings...");
	} else {
		// a plugin may take a while to start: the form of the one before is not left standing
		BString text("Reading the settings of ");
		text << target.name << "...";
		_ShowNote(text.String());
		_Status("");
	}

	fWaiting = true;
	delete fTimeout;
	BMessage timeout(kMsgTimeout);
	timeout.AddInt32("serial", fSerial + 1);
	fTimeout = new BMessageRunner(BMessenger(this), &timeout, kStartTimeout, 1);
	const int32 serial = ++fSerial;

	if (target.core) {
		Request* request = new Request();
		request->window = BMessenger(this);
		request->serial = serial;
			request->scope = target.scope;
		thread_id thread = spawn_thread(RunRequest, "settings request", B_NORMAL_PRIORITY, request);
		if (thread < 0 || resume_thread(thread) != B_OK)
			delete request;
		return;
	}

	// A plugin: its answer comes to the window as kSettingsReply (a plugin that is started for the request has nobody that waits for a reply). A plugin that is not started
	// is started with the request; one that has a session of ours is asked there (many plugins are multiple launch applications: the instance that has our session is the
	// one to talk to, whatever else runs).
	BMessage ask(sensei::cmd::kSettings);
	ask.AddMessenger(sensei::key::kReplyTo, BMessenger(this));
	ask.AddInt32("serial", serial);
	std::map<std::string, BMessenger>::iterator session = fSessions.find(target.signature.String());
	status_t result;
	if (session != fSessions.end() && session->second.IsValid())
		result = session->second.SendMessage(&ask);
	else
		result = be_roster->Launch(&target.file, &ask, &fStartedTeam);
	if (result != B_OK) {
		fWaiting = false;
		BString text(fName);
		text << " could not be started: " << strerror(result);
		_ShowNote(text.String());
		_Status("");
	}
}


// What a request got: the settings go into the form.
void
SettingsWindow::_Ready(BMessage* message)
{
	if (message->GetInt32("serial", 0) != fSerial) {
		// (of an older request: another item was chosen since; a plugin that was started for it has no use for the session)
		if (message->what == sensei::cmd::kSettingsReply && message->ReturnAddress().IsValid() && fSessions.find(message->GetString(sensei::key::kPlugin, "")) == fSessions.end())
			message->ReturnAddress().SendMessage(sensei::cmd::kSettingsClose);
		return;
	}
	fWaiting = false;
	fStartedTeam = -1;
	if (message->what == sensei::cmd::kSettingsReply && message->ReturnAddress().IsValid())
		fSessions[fSignature.String()] = message->ReturnAddress();

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
		const bool none = result == B_OK || result == B_NOT_SUPPORTED || result == B_UNSUPPORTED;
		if (none && fCore) {
			// (a node of SEN that has no settings: the node below it)
			NodeItem* current = dynamic_cast<NodeItem*>(fTree->ItemAt(fTree->CurrentSelection()));
			if (current != NULL) {
				_SelectBelow(fTree->FullListIndexOf(current));
				return;
			}
		}
		BString text(fName);
		if (none)
			text << " has no settings.";
		else
			text << ": " << (result == B_TIMED_OUT ? "does not answer." : strerror(result));
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
	if (fCore)
		set.AddString(sen::key::kScope, fScope);
	set.AddMessage(form::kFieldSettings, &changes);
	BMessenger target;
	if (fCore) {
		target = BMessenger(sen::kServerSignature);
	} else {
		std::map<std::string, BMessenger>::iterator session = fSessions.find(fSignature.String());
		if (session != fSessions.end())
			target = session->second;
	}
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
