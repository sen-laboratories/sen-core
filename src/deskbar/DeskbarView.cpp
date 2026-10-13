/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "DeskbarView.h"

#include "../ui/Knot.h"

#include <sen/Sen.h>
#include <sen/SenLookup.h>
#include <sen/SenOntoCore.h>

#include <Bitmap.h>
#include <Menu.h>
#include <MimeType.h>
#include <NodeInfo.h>
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


bool
DeskbarView::IsServerCommand(uint32 command)
{
	switch (command) {
		case sen::cmd::kUiShowInfo:
		case sen::cmd::kUiShowCheck:
		case sen::cmd::kUiShowSettings:
		case sen::cmd::kUiShowAbout:
		case sen::cmd::kUiRequestQuit:
		case sen::cmd::kUiRequestRestart:
		case sen::cmd::kUiOpenOntologies:
		case sen::cmd::kUiOpenOntology:
		case sen::cmd::kUiOpenType:
		case sen::cmd::kUiUpdateOntologies:
		case sen::cmd::kUiOpenObjects:
		case sen::cmd::kUiOpenClassification:
		case sen::cmd::kUiOpenClassified:
		case sen::cmd::kUiSetContext:
			return true;
	}
	return false;
}


void
DeskbarView::MessageReceived(BMessage* message)
{
	if (!IsServerCommand(message->what)) {
		BView::MessageReceived(message);
		return;
	}

	if (message->what == sen::cmd::kUiOpenClassified) {
		// with the Shift key the classification sticks (or does not any more) instead of opening: the server is told how the key was held now
		BMessage chosen(*message);
		chosen.AddBool(sen::key::kShift, (modifiers() & B_SHIFT_KEY) != 0);
		BMessenger(sen::kServerSignature).SendMessage(&chosen);
		return;
	}

	// the server does it
	BMessenger(sen::kServerSignature).SendMessage(message);
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


static const float kIconSize = 16;
static const float kIconPadding = 4;


/** A menu item with a small icon in front of its label, as the menus of Tracker have them. The icon is the item's. */
class IconItem : public BMenuItem {
public:
	IconItem(const char* label, BMessage* message, BBitmap* icon)
		:
		BMenuItem(label, message),
		fIcon(icon)
	{
	}

	/** (a menu that can be chosen itself: the message is sent when the item is clicked, as in the menus of Tracker) */
	IconItem(BMenu* submenu, BMessage* message, BBitmap* icon)
		:
		BMenuItem(submenu, message),
		fIcon(icon)
	{
	}

	virtual ~IconItem()
	{
		delete fIcon;
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		if (fIcon != NULL) {
			*width += kIconSize + kIconPadding;
			*height = std::max(*height, kIconSize + 2);
		}
	}

	virtual void DrawContent()
	{
		if (fIcon != NULL && Menu() != NULL) {
			BPoint where = ContentLocation();
			BRect frame = Frame();
			where.y = frame.top + (frame.Height() - kIconSize) / 2;
			Menu()->SetDrawingMode(B_OP_ALPHA);
			Menu()->DrawBitmap(fIcon, where);
			Menu()->SetDrawingMode(B_OP_COPY);
			Menu()->MovePenBy(kIconSize + kIconPadding, 0);
		}
		BMenuItem::DrawContent();
	}

private:
	BBitmap*	fIcon;
};


/** the icon of a file or folder, as Tracker shows it; NULL if there is none */
static BBitmap*
IconOfEntry(const entry_ref& ref)
{
	BBitmap* icon = new BBitmap(BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
	if (BNodeInfo::GetTrackerIcon(&ref, icon, B_MINI_ICON) != B_OK) {
		delete icon;
		return NULL;
	}
	return icon;
}


/** the icon of a MIME type (that of its supertype if it has none of its own); NULL if there is none */
static BBitmap*
IconOfType(const char* type)
{
	BBitmap* icon = new BBitmap(BRect(0, 0, kIconSize - 1, kIconSize - 1), B_RGBA32);
	BMimeType mime(type);
	if (mime.GetIcon(icon, B_MINI_ICON) == B_OK)
		return icon;
	BMimeType super;
	if (mime.GetSupertype(&super) == B_OK && super.GetIcon(icon, B_MINI_ICON) == B_OK)
		return icon;
	delete icon;
	return NULL;
}


/** The contexts that are set up: the folders in the contexts folder of the settings (the global one first), with their icon; the current one is marked. */
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
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
	auto global = std::find(names.begin(), names.end(), sen::config::kContextGlobal);
	if (global != names.end())
		names.erase(global);
	names.insert(names.begin(), sen::config::kContextGlobal);

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
		BBitmap* icon = NULL;
		BPath contextPath(path);
		entry_ref contextRef;
		if (contextPath.Append(name.c_str()) == B_OK && get_ref_for_path(contextPath.Path(), &contextRef) == B_OK)
			icon = IconOfEntry(contextRef);
		if (icon == NULL)
			icon = IconOfType(sen::mime::kContext);
		BMenuItem* item = new IconItem(name.c_str(), message, icon);
		item->SetMarked(current == name.c_str());
		contexts->AddItem(item);
	}
	contexts->SetTargetForItems(this);
	menu->AddItem(new IconItem(contexts, NULL, IconOfType(sen::mime::kContext)));
}


/** The types that an ontology provides (the targets of its relations that are types of the MIME database of the user), as MIME types. */
static std::vector<std::string>
TypesOfOntology(const entry_ref& ontology)
{
	std::vector<std::string> types;
	BPath mimeDb;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &mimeDb) != B_OK || mimeDb.Append("mime_db") != B_OK)
		return types;
	const size_t prefix = strlen(mimeDb.Path()) + 1;

	std::vector<entry_ref> targets;
	if (sen::lookup::Targets(ontology, &targets) != B_OK)
		return types;
	for (const entry_ref& target : targets) {
		BPath path(&target);
		if (path.InitCheck() == B_OK && strncmp(path.Path(), mimeDb.Path(), prefix - 1) == 0 && strlen(path.Path()) > prefix)
			types.push_back(path.Path() + prefix);
	}
	return types;
}


/** where a type goes in the list of the types of an ontology: the entities, the relations, the classifications, the meta types, the rest */
static int
TypeGroup(const std::string& type)
{
	static const char* const kGroups[] = {"entity/", "relation/", "classification/", "meta/"};
	for (int i = 0; i < 4; i++) {
		if (type.compare(0, strlen(kGroups[i]), kGroups[i]) == 0)
			return i;
	}
	return 4;
}


/**
 * The menu of a type of an ontology: choosing it opens the type in FileTypes, its items are the attributes (the attribute info of the type), each
 * of which opens the attribute in FileTypes. A type without attributes is an item.
 */
static BMenuItem*
TypeItem(const std::string& type, const std::string& name)
{
	BMessage info;
	BMimeType(type.c_str()).GetAttrInfo(&info);

	BMessage* show = new BMessage(sen::cmd::kUiOpenType);
	show->AddString(sen::key::kType, type.c_str());

	const char* attributeName;
	if (info.FindString("attr:name", 0, &attributeName) != B_OK)
		return new IconItem(name.c_str(), show, IconOfType(type.c_str()));

	BMenu* menu = new BMenu(name.c_str());
	const char* publicName;
	for (int32 index = 0; info.FindString("attr:name", index, &attributeName) == B_OK; index++) {
		if (info.FindString("attr:public_name", index, &publicName) != B_OK || publicName[0] == '\0')
			publicName = attributeName;
		BMessage* open = new BMessage(sen::cmd::kUiOpenType);
		open->AddString(sen::key::kType, type.c_str());
		open->AddString(sen::key::kAttributeName, attributeName);
		menu->AddItem(new IconItem(publicName, open, IconOfType(sen::onto::core::mime::kMimeAttribute)));
	}
	return new IconItem(menu, show, IconOfType(type.c_str()));
}


/**
 * The ontologies are configuration, not what people work with: their menu is next to the settings. It opens the folder with them and brings them up to
 * date with the packages, and has an item for each installed ontology (a file for each in the settings, with its icon): its menu shows it in its folder,
 * and then its types, which show their attributes, as when the ontology is opened down to its relations.
 */
void
DeskbarView::_AddOntologyMenu(BMenu* menu)
{
	BMenu* ontologies = new BMenu("Ontologies");
	ontologies->AddItem(new BMenuItem("Update", new BMessage(sen::cmd::kUiUpdateOntologies)));

	BPath path = SettingsPath();
	std::vector<entry_ref> installed;
	if (path.InitCheck() == B_OK && path.Append(sen::config::kOntologiesDir) == B_OK) {
		BDirectory directory(path.Path());
		entry_ref ref;
		while (directory.GetNextRef(&ref) == B_OK) {
			BEntry entry(&ref, true);
			if (entry.IsFile())
				installed.push_back(ref);
		}
	}
	std::sort(installed.begin(), installed.end(), [](const entry_ref& a, const entry_ref& b) { return strcasecmp(a.name, b.name) < 0; });
	if (!installed.empty())
		ontologies->AddSeparatorItem();

	for (const entry_ref& ref : installed) {
		BMenu* ontology = new BMenu(ref.name);
		BMessage* show = new BMessage(sen::cmd::kUiOpenOntology);
		show->AddRef("refs", &ref);

		// its types, with the names that people read (the short description of the type)
		std::vector<std::pair<std::string, std::string>> named;
		for (const std::string& type : TypesOfOntology(ref)) {
			char shortName[B_MIME_TYPE_LENGTH];
			std::string name = BMimeType(type.c_str()).GetShortDescription(shortName) == B_OK && shortName[0] != '\0'
				? shortName : type.substr(type.find('/') + 1);
			named.push_back({type, name});
		}
		std::sort(named.begin(), named.end(), [](const auto& a, const auto& b) {
			int groupA = TypeGroup(a.first), groupB = TypeGroup(b.first);
			return groupA != groupB ? groupA < groupB : strcasecmp(a.second.c_str(), b.second.c_str()) < 0;
		});
		int lastGroup = -1;
		for (const auto& type : named) {
			int group = TypeGroup(type.first);
			if (group != lastGroup && ontology->CountItems() > 0)
				ontology->AddSeparatorItem();
			lastGroup = group;
			ontology->AddItem(TypeItem(type.first, type.second));
		}
		ontology->SetTargetForItems(this);
		ontologies->AddItem(new IconItem(ontology, show, IconOfEntry(ref)));
	}
	ontologies->SetTargetForItems(this);
	// (choosing the menu itself opens the folder of the ontologies)
	menu->AddItem(new BMenuItem(ontologies, new BMessage(sen::cmd::kUiOpenOntologies)));
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
		// (choosing the menu itself opens a query with all of them)
		BMessage* showAll = new BMessage(sen::cmd::kUiOpenClassification);
		showAll->AddString(sen::key::kType, classification.c_str());

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

		int32 listed = 0;
		for (const entry_ref& entity : entities) {
			if (listed++ >= kMaxEntities)
				break;
			// choosing one shows the files that it classifies; with the Shift key it sticks (what is made new is put into it: the check mark)
			BMessage* classified = new BMessage(sen::cmd::kUiOpenClassified);
			classified->AddRef("refs", &entity);
			BMenuItem* item = new BMenuItem(entity.name, classified);
			bool sticky = false;
			BNode node(&entity);
			if (node.ReadAttr(sen::attr::kSticky, B_BOOL_TYPE, 0, &sticky, sizeof(sticky)) == sizeof(sticky))
				item->SetMarked(sticky);
			submenu->AddItem(item);
		}
		submenu->SetTargetForItems(this);
		menu->AddItem(new BMenuItem(submenu, showAll));
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
	// (the ontologies are configuration, not what people work with: next to the settings)
	_AddOntologyMenu(menu);
	menu->AddItem(new BMenuItem((BString("Settings") << ellipsis).String(), new BMessage(sen::cmd::kUiShowSettings)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem((BString("About") << ellipsis).String(), new BMessage(sen::cmd::kUiShowAbout)));
	menu->AddItem(new BMenuItem("Restart", new BMessage(sen::cmd::kUiRequestRestart)));
	menu->AddItem(new BMenuItem("Quit", new BMessage(sen::cmd::kUiRequestQuit)));

	menu->SetTargetForItems(this);
	return menu;
}

}	// namespace deskbar
}	// namespace sen
