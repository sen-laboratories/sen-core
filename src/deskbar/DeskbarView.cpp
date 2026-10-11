/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "DeskbarView.h"

#include "../ui/Knot.h"

#include <sen/Sen.h>

#include <Bitmap.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <Roster.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>
#include <vector>

namespace sen {
namespace deskbar {

const char* const kItemName = "sen_server";

/** the most entities of a classification that are listed in its menu; the rest is in the query that opens with the first item */
static const int32 kMaxEntities = 30;


extern "C" _EXPORT BView* instantiate_deskbar_item(float maxWidth, float maxHeight);


BView*
instantiate_deskbar_item(float /*maxWidth*/, float maxHeight)
{
	return new DeskbarView(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


DeskbarView::DeskbarView(BRect frame)
	:
	BView(frame, kItemName, B_FOLLOW_NONE, B_WILL_DRAW),
	fKnot(NULL),
	fLastButtons(0)
{
	_Init();
}


DeskbarView::DeskbarView(BMessage* archive)
	:
	BView(archive),
	fKnot(NULL),
	fLastButtons(0)
{
	_Init();
}


DeskbarView::~DeskbarView()
{
	delete fKnot;
}


void
DeskbarView::_Init()
{
	fKnot = ui::NewKnotBitmap();
}


DeskbarView*
DeskbarView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "sen::deskbar::DeskbarView"))
		return NULL;
	return new DeskbarView(archive);
}


status_t
DeskbarView::Archive(BMessage* archive, bool deep) const
{
	status_t result = BView::Archive(archive, deep);
	if (result == B_OK)
		result = archive->AddString("add_on", sen::kServerSignature);
	if (result == B_OK)
		result = archive->AddString("class", "sen::deskbar::DeskbarView");
	return result;
}


void
DeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
	if (ViewUIColor() == B_NO_COLOR)
		SetLowColor(ViewColor());
	else
		SetLowUIColor(ViewUIColor());
}


void
DeskbarView::Draw(BRect)
{
	if (fKnot == NULL)
		return;

	// the knot, a little smaller than the height of the Deskbar, in the middle
	BRect bounds(Bounds());
	float size = std::min(bounds.Width(), bounds.Height());
	BRect target(0, 0, size - 1, size - 1);
	target.OffsetTo(bounds.left + (bounds.Width() - size) / 2, bounds.top + (bounds.Height() - size) / 2);

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmap(fKnot, fKnot->Bounds(), target, B_FILTER_BITMAP_BILINEAR);
	SetDrawingMode(B_OP_COPY);
}


void
DeskbarView::_Send(uint32 command)
{
	BMessage message(command);
	BMessenger(sen::kServerSignature).SendMessage(&message);
}


void
DeskbarView::MouseDown(BPoint where)
{
	fLastButtons = Looper()->CurrentMessage()->GetInt32("buttons", 0);

	if ((fLastButtons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		ConvertToScreen(&where);
		BPopUpMenu* menu = _BuildMenu();
		menu->SetAsyncAutoDestruct(true);
		menu->Go(where, true, true, BRect(where.x - 2, where.y - 2, where.x + 2, where.y + 2), true);
	}
}


void
DeskbarView::MouseUp(BPoint)
{
	// the knot opens the knowledge graph as a list: the query with all the files that have a SEN:ID
	if ((fLastButtons & B_PRIMARY_MOUSE_BUTTON) != 0)
		_Send(sen::cmd::kUiOpenObjects);
	fLastButtons = 0;
}


void
DeskbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case sen::cmd::kUiShowInfo:
		case sen::cmd::kUiShowCheck:
		case sen::cmd::kUiShowSettings:
		case sen::cmd::kUiShowAbout:
		case sen::cmd::kUiRequestQuit:
		case sen::cmd::kUiOpenObjects:
		case sen::cmd::kUiOpenClassification:
		case sen::cmd::kUiOpenClassified:
		case sen::cmd::kUiSetContext:
			// the server does it
			BMessenger(sen::kServerSignature).SendMessage(message);
			break;
		default:
			BView::MessageReceived(message);
	}
}


// ---- the menu

static BPath
SettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
		path.Append("sen");
	return path;
}


/** The contexts that are set up: the folders in the contexts folder of the settings, the global one first. */
void
DeskbarView::_AddContextMenu(BMenu* menu)
{
	BPath path = SettingsPath();
	std::vector<std::string> names;
	if (path.InitCheck() == B_OK && path.Append(sen::config::kContextsDir) == B_OK) {
		BDirectory directory(path.Path());
		entry_ref ref;
		while (directory.GetNextRef(&ref) == B_OK) {
			BEntry entry(&ref, true);
			if (entry.IsDirectory())
				names.push_back(ref.name);
		}
	}
	std::sort(names.begin(), names.end());
	if (std::find(names.begin(), names.end(), sen::config::kContextGlobal) == names.end())
		names.insert(names.begin(), sen::config::kContextGlobal);
	else {
		names.erase(std::find(names.begin(), names.end(), sen::config::kContextGlobal));
		names.insert(names.begin(), sen::config::kContextGlobal);
	}

	// the current one (the global one if none was chosen)
	BString current(sen::config::kContextGlobal);
	BPath settingsPath = SettingsPath();
	if (settingsPath.InitCheck() == B_OK && settingsPath.Append(sen::config::kSettingsFile) == B_OK) {
		BFile file(settingsPath.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK)
			current = settings.GetString(sen::key::kCurrentContext, sen::config::kContextGlobal);
	}

	BMenu* contexts = new BMenu("Context");
	for (const std::string& name : names) {
		BMessage* message = new BMessage(sen::cmd::kUiSetContext);
		message->AddString(sen::key::kContext, name.c_str());
		BMenuItem* item = new BMenuItem(name.c_str(), message);
		item->SetMarked(current == name.c_str());
		contexts->AddItem(item);
	}
	contexts->SetTargetForItems(this);
	menu->AddItem(contexts);
}


/** A menu for every type of classification (labels, topics, projects...): the first item opens a query with all of them, then the entities. */
void
DeskbarView::_AddClassificationMenus(BMenu* menu)
{
	BMessage installed;
	if (BMimeType::GetInstalledTypes(sen::mime::kClassificationSupertype, &installed) != B_OK)
		return;

	std::vector<std::string> types;
	const char* type;
	for (int32 index = 0; installed.FindString("types", index, &type) == B_OK; index++) {
		// (contexts are special, they have a menu of their own)
		if (strcmp(type, sen::mime::kContext) != 0)
			types.push_back(type);
	}
	std::sort(types.begin(), types.end());

	for (const std::string& classification : types) {
		char shortName[B_MIME_TYPE_LENGTH];
		BString name = BMimeType(classification.c_str()).GetShortDescription(shortName) == B_OK && shortName[0] != '\0' ? shortName
			: classification.c_str();
		// (the plural, as in the names of the folders: a quick hack until the MIME type has it)
		if (name.EndsWith("s") || name.EndsWith("x"))
			name << "es";
		else if (name.EndsWith("y") && name.Length() > 1)
			name.Truncate(name.Length() - 1).Append("ies");
		else
			name << "s";

		BMenu* submenu = new BMenu(name.String());
		BString all("Show all");
		all << B_UTF8_ELLIPSIS;
		BMessage* showAll = new BMessage(sen::cmd::kUiOpenClassification);
		showAll->AddString(sen::key::kType, classification.c_str());
		submenu->AddItem(new BMenuItem(all.String(), showAll));

		// the entities of the type, on all volumes
		std::vector<entry_ref> entities;
		BVolumeRoster roster;
		BVolume volume;
		while (roster.GetNextVolume(&volume) == B_OK) {
			if (!volume.KnowsQuery() || volume.IsReadOnly())
				continue;
			BQuery query;
			BString predicate("BEOS:TYPE==\"");
			predicate << classification.c_str() << "\"";
			if (query.SetVolume(&volume) != B_OK || query.SetPredicate(predicate.String()) != B_OK || query.Fetch() != B_OK)
				continue;
			entry_ref ref;
			while (query.GetNextRef(&ref) == B_OK)
				entities.push_back(ref);
		}
		std::sort(entities.begin(), entities.end(), [](const entry_ref& a, const entry_ref& b) { return strcasecmp(a.name, b.name) < 0; });

		if (!entities.empty())
			submenu->AddSeparatorItem();
		int32 listed = 0;
		for (const entry_ref& entity : entities) {
			if (listed++ >= kMaxEntities)
				break;
			// choosing one shows the files that it classifies
			BMessage* classified = new BMessage(sen::cmd::kUiOpenClassified);
			classified->AddRef("refs", &entity);
			submenu->AddItem(new BMenuItem(entity.name, classified));
		}
		submenu->SetTargetForItems(this);
		menu->AddItem(submenu);
	}
}


BPopUpMenu*
DeskbarView::_BuildMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("", false, false);
	menu->SetFont(be_plain_font);

	_AddContextMenu(menu);
	menu->AddSeparatorItem();

	int32 before = menu->CountItems();
	_AddClassificationMenus(menu);
	if (menu->CountItems() > before)
		menu->AddSeparatorItem();

	BString ellipsis(B_UTF8_ELLIPSIS);
	menu->AddItem(new BMenuItem((BString("Info") << ellipsis).String(), new BMessage(sen::cmd::kUiShowInfo)));
	menu->AddItem(new BMenuItem((BString("Check") << ellipsis).String(), new BMessage(sen::cmd::kUiShowCheck)));
	menu->AddItem(new BMenuItem((BString("Settings") << ellipsis).String(), new BMessage(sen::cmd::kUiShowSettings)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem((BString("About") << ellipsis).String(), new BMessage(sen::cmd::kUiShowAbout)));
	menu->AddItem(new BMenuItem("Quit", new BMessage(sen::cmd::kUiRequestQuit)));

	menu->SetTargetForItems(this);
	return menu;
}

}	// namespace deskbar
}	// namespace sen
