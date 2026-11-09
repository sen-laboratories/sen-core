/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "CoreSettings.h"

#include "SenConfigHandler.h"
#include "../ui/FileTypesLink.h"

#include <sen/Sen.h>
#include <sen/SenMessagePath.h>
#include <sen/SenSettingsForm.h>

#include <Entry.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Path.h>
#include <String.h>

#include <stdlib.h>

namespace form = sen::settings::form;

namespace sen {
namespace core {

static const char kKeyField[] = "SEN:modifierKey";
static const char kFileTypesField[] = "SEN:useFileTypes";
static const char kAkitaField[] = "akita";
static const char kLibraryNameField[] = "SEN:libraryName";
static const char kAssociateContextField[] = "SEN:associateCurrentContext";
static const char kExtractIntoField[] = "SEN:extractEntitiesInto";
static const char kEnrichOnDemandField[] = "SEN:enrichOnDemand";
static const char kAskIfSeveralField[] = "SEN:askIfSeveral";
static const char kMaxEntitiesField[] = "SEN:maxEntities";


/** A file of Akita or its command: in the folder of the system or of the user. */
static bool
AkitaFile(directory_which system, directory_which user, const char* name, BPath* found = NULL)
{
	for (directory_which which : {system, user}) {
		BPath path;
		if (find_directory(which, &path) == B_OK && path.Append(name) == B_OK && BEntry(path.Path()).Exists()) {
			if (found != NULL)
				*found = path;
			return true;
		}
	}
	return false;
}


static bool
AkitaInstalled()
{
	return AkitaFile(B_SYSTEM_APPS_DIRECTORY, B_USER_APPS_DIRECTORY, "Akita/Akita");
}


/** Whether Akita replaces Tracker at login (the default of its package: until the file of the user that undoes it is there). */
static bool
AkitaEnabled()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK || path.Append("launch/akita") != B_OK)
		return true;
	return !BEntry(path.Path()).Exists();
}


struct KeyChoice {
	const char*	label;
	int32		key;
};

/** the other keys that can be the SEN key by choice (the standard shortcut key of Haiku is the first of the choices) */
static const KeyChoice kOtherKeys[] = {
	{"Option", B_OPTION_KEY},
	{"Control", B_CONTROL_KEY},
	{"Shift", B_SHIFT_KEY},
};


/** the key of the keyboard that is the standard shortcut key of Haiku in the key map that is active (the Alt key in the default one of a PC keyboard); empty if it is not known */
static BString
StandardKeyName()
{
	BString name;
	uint32 code = 0;
	if (get_modifier_key(B_LEFT_COMMAND_KEY, &code) == B_OK) {
		switch (code) {
			case 0x5d:
			case 0x5f:	name = "Alt key"; break;
			case 0x5c:
			case 0x60:	name = "Control key"; break;
			case 0x66:
			case 0x67:	name = "Windows key, Command on a Mac keyboard"; break;
			case 0x68:	name = "Menu key"; break;
			case 0x4b:
			case 0x56:	name = "Shift key"; break;
			default:	break;
		}
	}
	return name;
}


/** the help of the SEN key (a tooltip): what the key does, and which keys of the keyboard the choices are (the labels of the choices are short) */
static BString
KeyHelp()
{
	BString help("Hold this key to relate files by dragging, and to open the graph from the knot and from Akita.\n\n");
	const BString standard = StandardKeyName();
	help << "Standard shortcut key of Haiku: the key that has the shortcuts of the menus";
	if (!standard.IsEmpty())
		help << " (" << standard << " with your key map)";
	help << ".\nOption: the Windows key on a PC keyboard.";
	return help;
}


static bool
IsKnownKey(int32 key)
{
	if (key == B_COMMAND_KEY)
		return true;
	for (const KeyChoice& choice : kOtherKeys) {
		if (choice.key == key)
			return true;
	}
	return false;
}


/** The name of the Library folder as it is set (default: Library). */
static std::string
LibraryName(const BMessage& stored)
{
	const char* name = stored.GetString(sen::key::kLibraryName, "");
	return name[0] != '\0' ? name : sen::config::kLibraryDir;
}


/** A name that is one folder in the home folder: not empty, no slash or colon, not . or .., no blanks at the ends. */
static bool
IsFolderName(const char* name)
{
	BString text(name);
	if (text.IsEmpty() || text == "." || text == ".." || text.FindFirst('/') >= 0 || text.FindFirst(':') >= 0)
		return false;
	BString trimmed(text);
	trimmed.Trim();
	return trimmed == text;
}


void
DescribeSettings(SenConfigHandler* config, BMessage* settings, BMessage* schema, BMessage* defaults)
{
	BMessage stored;
	config->GetConfig(&stored);
	int32 key = stored.GetInt32(sen::key::kModifierKey, B_COMMAND_KEY);
	// (a value that is none of the keys of the menu counts as the standard one, as in SenKey.h)
	if (!IsKnownKey(key))
		key = B_COMMAND_KEY;

	settings->MakeEmpty();
	settings->AddInt32(kKeyField, key);
	settings->AddBool(kFileTypesField, stored.GetBool(sen::key::kUseSenFileTypes, true));
	settings->AddString(kLibraryNameField, LibraryName(stored).c_str());
	settings->AddBool(kAssociateContextField, stored.GetBool(sen::key::kAssociateContext, true));
	const bool akita = AkitaInstalled();
	if (akita)
		settings->AddBool(kAkitaField, AkitaEnabled());

	defaults->MakeEmpty();
	defaults->AddInt32(kKeyField, B_COMMAND_KEY);
	defaults->AddBool(kFileTypesField, true);
	defaults->AddString(kLibraryNameField, sen::config::kLibraryDir);
	defaults->AddBool(kAssociateContextField, true);
	if (akita)
		defaults->AddBool(kAkitaField, true);

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);

	BMessage keyField;
	keyField.AddString(form::kAttrLabel, "SEN key");
	keyField.AddString(form::kAttrHelp, KeyHelp().String());
	keyField.AddString(form::kAttrKind, form::kKindChoice);
	keyField.AddInt32(form::kAttrOrder, 0);
	keyField.AddString(form::kAttrChoices, std::to_string((int)B_COMMAND_KEY).c_str());
	keyField.AddString(form::kAttrChoiceLabels, "Standard shortcut key of Haiku");
	for (const KeyChoice& choice : kOtherKeys) {
		keyField.AddString(form::kAttrChoices, std::to_string((int)choice.key).c_str());
		keyField.AddString(form::kAttrChoiceLabels, choice.label);
	}
	schema->AddMessage(kKeyField, &keyField);

	BMessage akitaField;
	akitaField.AddString(form::kAttrLabel, "Use Akita instead of Tracker");
	akitaField.AddString(form::kAttrHelp, "Takes effect at the next login (the command akita switches at once).");
	akitaField.AddInt32(form::kAttrOrder, 1);
	schema->AddMessage(kAkitaField, &akitaField);

	BMessage fileTypes;
	fileTypes.AddString(form::kAttrLabel, "Use the FileTypes of SEN instead of the one of Haiku");
	fileTypes.AddString(form::kAttrHelp, "Where SEN shows a type and its attributes (the Ontologies menu), and in the Preferences menu of the Deskbar.");
	fileTypes.AddInt32(form::kAttrOrder, 2);
	schema->AddMessage(kFileTypesField, &fileTypes);

	BMessage libraryName;
	libraryName.AddString(form::kAttrLabel, "Library name");
	libraryName.AddString(form::kAttrHelp, "The folder in your home folder where SEN and its plugins put the entities they make (people, books, movies...). "
		"A folder that exists is not moved.");
	libraryName.AddInt32(form::kAttrOrder, 3);
	schema->AddMessage(kLibraryNameField, &libraryName);

	BMessage associate;
	associate.AddString(form::kAttrLabel, "New objects belong to the current context");
	associate.AddString(form::kAttrHelp, "What is made new (a file, a relation, the people of a book or a movie that a plugin makes) gets an association relation to the context that is "
		"checked in the menu of SEN, as for a context that is locked with the Shift key. Off: only what is locked is applied. The default context is every file and is never applied.");
	associate.AddInt32(form::kAttrOrder, 4);
	schema->AddMessage(kAssociateContextField, &associate);
}


status_t
ApplySettings(SenConfigHandler* config, const BMessage& changes, std::vector<std::string>* refused)
{
	BMessage current, schema, defaults;
	DescribeSettings(config, &current, &schema, &defaults);

	// the values through the types of the settings: a wrong type or an unknown field is refused, the rest is made
	BMessage updated(current);
	status_t first = sen::settings::ApplyChanges(&updated, changes, refused);

	int32 key = 0;
	if (updated.FindInt32(kKeyField, &key) == B_OK && key != current.GetInt32(kKeyField, B_COMMAND_KEY)) {
		if (IsKnownKey(key)) {
			BMessage change;
			change.AddInt32(sen::key::kModifierKey, key);
			config->UpdateSettings(&change);
		} else {
			if (refused != NULL)
				refused->push_back(kKeyField);
			if (first == B_OK)
				first = B_BAD_VALUE;
		}
	}

	const bool fileTypes = updated.GetBool(kFileTypesField, true);
	if (fileTypes != current.GetBool(kFileTypesField, true)) {
		BMessage change;
		change.AddBool(sen::key::kUseSenFileTypes, fileTypes);
		config->UpdateSettings(&change);
		sen::ui::ApplyFileTypesLink(fileTypes);
	}

	const BString libraryName = updated.GetString(kLibraryNameField, "");
	if (libraryName != current.GetString(kLibraryNameField, "")) {
		if (IsFolderName(libraryName.String())) {
			BMessage change;
			change.AddString(sen::key::kLibraryName, libraryName.String());
			config->UpdateSettings(&change);
		} else {
			if (refused != NULL)
				refused->push_back(kLibraryNameField);
			if (first == B_OK)
				first = B_BAD_VALUE;
		}
	}

	const bool associate = updated.GetBool(kAssociateContextField, true);
	if (associate != current.GetBool(kAssociateContextField, true)) {
		BMessage change;
		change.AddBool(sen::key::kAssociateContext, associate);
		config->UpdateSettings(&change);
	}

	if (current.HasBool(kAkitaField) && updated.GetBool(kAkitaField, true) != current.GetBool(kAkitaField, true)) {
		// (the command does it: a file of the user for the launch daemon, which undoes what the package of Akita does)
		BPath command;
		AkitaFile(B_SYSTEM_BIN_DIRECTORY, B_USER_BIN_DIRECTORY, "akita", &command);
		BString line(command.Path() != NULL ? command.Path() : "akita");
		line << (updated.GetBool(kAkitaField, true) ? " enable" : " disable") << " >/dev/null 2>&1";
		system(line.String());
	}
	return first;
}


/** the number of entities that is set: from 0 to the limit, as a number (the form gives a number or a text) */
static bool
IsEntityCount(int32 count)
{
	return count >= 0 && count <= sen::key::kMaxEntitiesLimit;
}


void
DescribePluginSettings(SenConfigHandler* config, BMessage* settings, BMessage* schema, BMessage* defaults)
{
	BMessage stored;
	config->GetConfig(&stored);
	BString into = stored.GetString(sen::key::kExtractInto, sen::key::kExtractIntoFiles);
	if (into != sen::key::kExtractIntoAttributes)
		into = sen::key::kExtractIntoFiles;
	int32 most = stored.GetInt32(sen::key::kMaxEntities, sen::key::kMaxEntitiesDefault);
	if (!IsEntityCount(most))
		most = sen::key::kMaxEntitiesDefault;

	settings->MakeEmpty();
	settings->AddString(kExtractIntoField, into.String());
	settings->AddBool(kAskIfSeveralField, stored.GetBool(sen::key::kAskIfSeveral, true));
	settings->AddInt32(kMaxEntitiesField, most);
	defaults->MakeEmpty();
	defaults->AddString(kExtractIntoField, sen::key::kExtractIntoFiles);
	defaults->AddBool(kAskIfSeveralField, true);
	defaults->AddInt32(kMaxEntitiesField, sen::key::kMaxEntitiesDefault);

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);

	BMessage extract;
	extract.AddString(form::kAttrLabel, "Extract entities into");
	extract.AddString(form::kAttrHelp, "What plugins find in a file (the authors of a book, the director and cast of a movie): a file of its own for each, related to the file, "
		"or only the names in the attributes of the file itself.");
	extract.AddString(form::kAttrKind, form::kKindRadio);
	extract.AddInt32(form::kAttrOrder, 0);
	extract.AddString(form::kAttrChoices, sen::key::kExtractIntoFiles);
	extract.AddString(form::kAttrChoiceLabels, "Files");
	extract.AddString(form::kAttrChoices, sen::key::kExtractIntoAttributes);
	extract.AddString(form::kAttrChoiceLabels, "Attributes");
	schema->AddMessage(kExtractIntoField, &extract);

	BMessage ask;
	ask.AddString(form::kAttrLabel, "Ask which one if several are found");
	ask.AddString(form::kAttrHelp, "A lookup (a book by its title, a movie, a paper) may have several answers. On: a window lists them and you choose. Off: the best one is taken.");
	ask.AddInt32(form::kAttrOrder, 1);
	schema->AddMessage(kAskIfSeveralField, &ask);

	BMessage maximum;
	maximum.AddString(form::kAttrLabel, "Max. number of entities to extract");
	maximum.AddString(form::kAttrHelp, "The most entities of one type that a plugin makes of one file (the first authors of a long list, the first actors of the credits). None: no entities are made. All: no limit.");
	maximum.AddInt32(form::kAttrOrder, 2);
	maximum.AddDouble(form::kAttrMin, 0);
	maximum.AddString(form::kAttrKind, form::kKindSlider);
	maximum.AddString(form::kAttrMinLabel, "None");
	maximum.AddString(form::kAttrMaxLabel, "All");
	maximum.AddDouble(form::kAttrMax, sen::key::kMaxEntitiesLimit);
	schema->AddMessage(kMaxEntitiesField, &maximum);
}


status_t
ApplyPluginSettings(SenConfigHandler* config, const BMessage& changes, std::vector<std::string>* refused)
{
	BMessage current, schema, defaults;
	DescribePluginSettings(config, &current, &schema, &defaults);

	BMessage updated(current);
	status_t first = sen::settings::ApplyChanges(&updated, changes, refused);

	const BString into = updated.GetString(kExtractIntoField, "");
	if (into != current.GetString(kExtractIntoField, "")) {
		if (into == sen::key::kExtractIntoFiles || into == sen::key::kExtractIntoAttributes) {
			BMessage change;
			change.AddString(sen::key::kExtractInto, into.String());
			config->UpdateSettings(&change);
		} else {
			if (refused != NULL)
				refused->push_back(kExtractIntoField);
			if (first == B_OK)
				first = B_BAD_VALUE;
		}
	}

	const bool ask = updated.GetBool(kAskIfSeveralField, true);
	if (ask != current.GetBool(kAskIfSeveralField, true)) {
		BMessage change;
		change.AddBool(sen::key::kAskIfSeveral, ask);
		config->UpdateSettings(&change);
	}

	const int32 most = updated.GetInt32(kMaxEntitiesField, sen::key::kMaxEntitiesDefault);
	if (most != current.GetInt32(kMaxEntitiesField, sen::key::kMaxEntitiesDefault)) {
		if (IsEntityCount(most)) {
			BMessage change;
			change.AddInt32(sen::key::kMaxEntities, most);
			config->UpdateSettings(&change);
		} else {
			if (refused != NULL)
				refused->push_back(kMaxEntitiesField);
			if (first == B_OK)
				first = B_BAD_VALUE;
		}
	}
	return first;
}

void
DescribeEnrichSettings(SenConfigHandler* config, BMessage* settings, BMessage* schema, BMessage* defaults)
{
	BMessage stored;
	config->GetConfig(&stored);

	settings->MakeEmpty();
	settings->AddBool(kEnrichOnDemandField, stored.GetBool(sen::key::kEnrichOnDemand, true));
	defaults->MakeEmpty();
	defaults->AddBool(kEnrichOnDemandField, true);

	schema->MakeEmpty();
	schema->AddInt32(form::kSchemaVersion, 1);

	BMessage onDemand;
	onDemand.AddString(form::kAttrLabel, "Enrich automatically on demand");
	onDemand.AddString(form::kAttrHelp, "When you open a relation of a place that has no position yet (the things nearby), the place is enriched first (its address is looked up with "
		"OpenStreetMap and Wikidata) and the relation is worked out afterwards. Off: you are told that the place has no position yet, and you enrich it yourself.");
	onDemand.AddInt32(form::kAttrOrder, 0);
	schema->AddMessage(kEnrichOnDemandField, &onDemand);
}


status_t
ApplyEnrichSettings(SenConfigHandler* config, const BMessage& changes, std::vector<std::string>* refused)
{
	BMessage current, schema, defaults;
	DescribeEnrichSettings(config, &current, &schema, &defaults);

	BMessage updated(current);
	status_t first = sen::settings::ApplyChanges(&updated, changes, refused);

	const bool onDemand = updated.GetBool(kEnrichOnDemandField, true);
	if (onDemand != current.GetBool(kEnrichOnDemandField, true)) {
		BMessage change;
		change.AddBool(sen::key::kEnrichOnDemand, onDemand);
		config->UpdateSettings(&change);
	}
	return first;
}

}	// namespace core
}	// namespace sen
