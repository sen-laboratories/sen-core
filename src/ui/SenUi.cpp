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
#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <stdlib.h>
#include <Invoker.h>
#include <Message.h>
#include <MimeType.h>
#include <Window.h>

#include <spdlog/spdlog.h>

namespace sen {
namespace ui {

static const uint32 kQuitAnswer = 'SUqa';
/** the server was asked to stop and is still there: it quits (it is not a service of the launch daemon) */
static const uint32 kQuitFallback = 'SUqf';


SenUi::~SenUi()
{
	delete fQuitFallback;
}


SenUi::SenUi(SenConfigHandler* config, RelationHandler* relations)
	:
	fConfig(config),
	fRelations(relations),
	fQuitFallback(NULL)
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
				_Quit();
			return true;
		case sen::cmd::kUiRequestRestart:
			_Restart();
			return true;
		case kQuitFallback:
			be_app->PostMessage(B_QUIT_REQUESTED);
			return true;
		case sen::cmd::kUiOpenOntologies:
			_OpenOntologies();
			return true;
		case sen::cmd::kUiOpenOntology:
			_OpenOntology(message);
			return true;
		case sen::cmd::kUiOpenType:
			_OpenType(message);
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

	// with the Shift key held it is made sticky, or not any more: what is made new is put into a sticky classification (see RelationSticky.cpp)
	if (message->GetBool(sen::key::kShift, false)) {
		BNode node(&entity);
		bool sticky = false;
		if (node.ReadAttr(sen::attr::kSticky, B_BOOL_TYPE, 0, &sticky, sizeof(sticky)) != sizeof(sticky))
			sticky = false;
		sticky = !sticky;
		ssize_t written = node.WriteAttr(sen::attr::kSticky, B_BOOL_TYPE, 0, &sticky, sizeof(sticky));
		_Report(sticky ? "make the classification sticky" : "make the classification not sticky", written == (ssize_t) sizeof(sticky) ? B_OK : (written < 0 ? (status_t) written : B_ERROR));
		return;
	}

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



/** the text in single quotes for a shell */
static BString
Quoted(const char* text)
{
	BString quoted(text);
	quoted.ReplaceAll("'", "'\\''");
	return BString("'") << quoted << "'";
}


/** The server is a service of the launch daemon, which starts it again when it quits: it has to be stopped as a service. The daemon asks the server to
 *  quit, so this does not wait for it: the command runs on its own. A server that was started by hand is no service, it quits by itself after a moment. */
void
SenUi::_Quit()
{
	BString command("launch_roster stop ");
	command << Quoted(sen::kServerService) << " >/dev/null 2>&1 &";
	system(command.String());

	delete fQuitFallback;
	fQuitFallback = new BMessageRunner(be_app, new BMessage(kQuitFallback), 2000000, 1);
}


/** Restarts the server: it quits, and is started again. As a service of the launch daemon (the server of a package) it is started again by the
 *  daemon, which does it when a service quits (stopping the service with launch_roster would not: it stays idle). One that was started by hand, from a
 *  build, is not: a command of its own, which outlives this server, looks after two seconds whether a server is there, and starts the program that
 *  this is if not. */
void
SenUi::_Restart()
{
	app_info info;
	BString program;
	if (be_app->GetAppInfo(&info) == B_OK)
		program = BPath(&info.ref).Path();

	// (a service is started again by the launch daemon at once: after two seconds it is there. One that was stopped is started, else the program.)
	BString script("sleep 2; hey \"$1\" SCst >/dev/null 2>&1 && exit 0; launch_roster start \"$3\" >/dev/null 2>&1; sleep 1; "
		"hey \"$1\" SCst >/dev/null 2>&1 && exit 0; \"$2\" >/dev/null 2>&1");
	BString command("sh -c ");
	command << Quoted(script.String()) << " sh " << Quoted(sen::kServerSignature) << " " << Quoted(program.String()) << " "
		<< Quoted(sen::kServerService) << " >/dev/null 2>&1 &";
	system(command.String());

	be_app->PostMessage(B_QUIT_REQUESTED);
}


// ---- ontologies

void
SenUi::_OpenOntologies()
{
	BPath path;
	status_t result = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (result == B_OK)
		result = path.Append("sen");
	if (result == B_OK)
		result = path.Append(sen::config::kOntologiesDir);
	if (result == B_OK)
		result = create_directory(path.Path(), 0755);

	entry_ref folder;
	if (result == B_OK)
		result = get_ref_for_path(path.Path(), &folder);
	if (result == B_OK)
		result = OpenInTracker(folder);
	_Report("open the ontologies", result);
}


void
SenUi::_OpenOntology(const BMessage* message)
{
	entry_ref ontology;
	if (message->FindRef("refs", &ontology) != B_OK)
		return;
	_Report("show the ontology", ShowInTracker(ontology));
}


/** A type of an ontology (and one of its attributes) in FileTypes: the one of SEN if it is there and wanted (a setting; it is asked by a message), else the one of Haiku,
 *  started for the type (it cannot be asked for an attribute). */
void
SenUi::_OpenType(const BMessage* message)
{
	const char* type = message->GetString(sen::key::kType, "");
	if (type[0] == '\0')
		return;

	bool useSen = true;
	BMessage settings;
	if (fConfig->GetConfig(&settings) == B_OK)
		useSen = settings.GetBool(sen::key::kUseSenFileTypes, true);

	status_t result;
	entry_ref senFileTypes;
	if (useSen && be_roster->FindApp(sen::kFileTypesSignature, &senFileTypes) == B_OK) {
		BMessenger messenger(sen::kFileTypesSignature);
		if (!messenger.IsValid()) {
			be_roster->Launch(sen::kFileTypesSignature);
			for (int tries = 0; tries < 50 && !messenger.IsValid(); tries++) {
				snooze(100000);
				messenger = BMessenger(sen::kFileTypesSignature);
			}
		}
		BMessage open(sen::cmd::kOpenMimeAttribute);
		open.AddString(sen::key::kMimeType, type);
		const char* attribute = message->GetString(sen::key::kAttributeName, "");
		if (attribute[0] != '\0')
			open.AddString(sen::key::kAttributeName, attribute);
		result = messenger.SendMessage(&open);
	} else {
		// (the roster puts the program in front of the arguments itself; one that runs is only brought to the front)
		const char* arguments[] = {"-type", type};
		result = be_roster->Launch(sen::kHaikuFileTypesSignature, 2, arguments);
		if (result == B_ALREADY_RUNNING)
			result = B_OK;
	}
	_Report("show the type", result);
}

}	// namespace ui
}	// namespace sen
