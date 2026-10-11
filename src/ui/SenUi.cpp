/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenUi.h"

#include "AboutWindow.h"
#include "CheckWindow.h"
#include "InfoWindow.h"
#include "QueryFiles.h"
#include "SettingsWindow.h"

#include "../config/SenConfigHandler.h"
#include "../relations/IdListAttr.h"
#include "../relations/RelationHandler.h"

#include <sen/Sen.h>

#include <Alert.h>
#include <Application.h>
#include <Invoker.h>
#include <Message.h>
#include <MimeType.h>
#include <Window.h>

#include <spdlog/spdlog.h>

namespace sen {
namespace ui {

static const uint32 kQuitAnswer = 'SUqa';


SenUi::SenUi(SenConfigHandler* config, RelationHandler* relations)
	:
	fConfig(config),
	fRelations(relations)
{
}


bool
SenUi::Handle(BMessage* message)
{
	switch (message->what) {
		case sen::cmd::kUiShowInfo:
			_Show(&fInfo, &SenUi::_MakeInfo, 'IWac');
			return true;
		case sen::cmd::kUiShowCheck:
			_Show(&fCheck, &SenUi::_MakeCheck, 'CWac');
			return true;
		case sen::cmd::kUiShowSettings:
			_Show(&fSettings, &SenUi::_MakeSettings, 'SWac');
			return true;
		case sen::cmd::kUiShowAbout:
			_Show(&fAbout, &SenUi::_MakeAbout, 'ABac');
			return true;
		case sen::cmd::kUiOpenObjects:
			_OpenObjects();
			return true;
		case sen::cmd::kUiOpenClassification:
			_OpenClassification(message);
			return true;
		case sen::cmd::kUiOpenClassified:
			_OpenClassified(message);
			return true;
		case sen::cmd::kUiSetContext:
			_SetContext(message);
			return true;
		case sen::cmd::kUiRequestQuit:
			_AskToQuit();
			return true;
		case kQuitAnswer:
			// the second button is "Quit"
			if (message->GetInt32("which", 0) == 1)
				be_app->PostMessage(B_QUIT_REQUESTED);
			return true;
	}
	return false;
}


void
SenUi::CloseWindows()
{
	BMessenger* windows[] = {&fInfo, &fCheck, &fSettings, &fAbout};
	for (BMessenger* window : windows) {
		if (window->IsValid())
			window->SendMessage(B_QUIT_REQUESTED);
	}
}


// ---- windows

void
SenUi::_Show(BMessenger* window, BWindow* (SenUi::*make)(), uint32 activate)
{
	// one window of a kind: an open one is brought to the front
	if (window->IsValid() && window->SendMessage(activate) == B_OK)
		return;

	BWindow* created = (this->*make)();
	*window = BMessenger(created);
	created->Show();
}


BWindow* SenUi::_MakeInfo() { return new InfoWindow(); }
BWindow* SenUi::_MakeCheck() { return new CheckWindow(); }
BWindow* SenUi::_MakeSettings() { return new SettingsWindow(fConfig); }
BWindow* SenUi::_MakeAbout() { return new AboutWindow(); }


// ---- queries

void
SenUi::_Report(const char* what, status_t result)
{
	if (result != B_OK) {
		spdlog::error("could not {}: {}", what, strerror(result));
		BAlert* alert = new BAlert("SEN", (BString("Could not ") << what << ": " << strerror(result)).String(), "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
	}
}


void
SenUi::_OpenObjects()
{
	entry_ref query;
	status_t result = MakeQueryFile("SEN objects", (BString(sen::attr::kId) << "==*").String(), &query);
	if (result == B_OK)
		result = OpenInTracker(query);
	_Report("open the SEN objects", result);
}


/** the name of a type for people, in the plural: the short description of the type with an s (a quick hack as in SenConfigHandler) */
static BString
PluralName(const char* type)
{
	char shortName[B_MIME_TYPE_LENGTH];
	BString name = BMimeType(type).GetShortDescription(shortName) == B_OK && shortName[0] != '\0' ? shortName : type;
	if (name.EndsWith("s") || name.EndsWith("x"))
		name << "es";
	else if (name.EndsWith("y") && name.Length() > 1)
		name.Truncate(name.Length() - 1).Append("ies");
	else
		name << "s";
	return name;
}


void
SenUi::_OpenClassification(const BMessage* message)
{
	const char* type = message->GetString(sen::key::kType, "");
	if (type[0] == '\0')
		return;

	entry_ref query;
	BString predicate("BEOS:TYPE==\"");
	predicate << type << "\"";
	status_t result = MakeQueryFile(PluralName(type).String(), predicate.String(), &query);
	if (result == B_OK)
		result = OpenInTracker(query);
	_Report("open the classification", result);
}


void
SenUi::_OpenClassified(const BMessage* message)
{
	entry_ref entity;
	if (message->FindRef("refs", &entity) != B_OK)
		return;

	// the files that have the entity (a label, a topic...) as a target: of an association (SEN:META), or any other relation (SEN:TO)
	char id[sen::id::kLength];
	status_t result = fRelations->GetOrCreateId(&entity, id, true);
	if (result == B_OK) {
		BString predicate("(");
		predicate << sen::idlist::ContainsPredicate(sen::attr::kTo, id).c_str() << ") || ("
			<< sen::idlist::ContainsPredicate(sen::attr::kMeta, id).c_str() << ")";
		entry_ref query;
		BString name("Files: ");
		name << entity.name;
		result = MakeQueryFile(name.String(), predicate.String(), &query);
		if (result == B_OK)
			result = OpenInTracker(query);
	}
	_Report("open the files of the classification", result);
}


void
SenUi::_SetContext(const BMessage* message)
{
	const char* name = message->GetString(sen::key::kContext, "");
	if (name[0] == '\0')
		return;
	// (for now the context is only remembered; what it changes comes with the global context)
	BMessage change;
	change.AddString(sen::key::kCurrentContext, name);
	fConfig->UpdateSettings(&change);
}


// ---- quit

void
SenUi::_AskToQuit()
{
	BAlert* alert = new BAlert("SEN",
		"Quit the SEN server?\n\n"
		"Without it, files that are copied keep the SEN:ID of the original, which breaks the lookup of relations and targets by their ID.",
		"Cancel", "Quit SEN", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	// (not blocking: the server answers requests of other programs meanwhile)
	alert->Go(new BInvoker(new BMessage(kQuitAnswer), be_app));
}

}	// namespace ui
}	// namespace sen
