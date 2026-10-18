/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <MessageRunner.h>
#include <Messenger.h>

class BMessage;
class BWindow;
class RelationHandler;
class SenConfigHandler;

namespace sen {
namespace ui {

/**
 * What the Deskbar menu of SEN does: the item in the Deskbar (see DeskbarView) sends messages to the server (the Deskbar is another team and
 * is not to hold up windows or queries), and this shows the windows (Info, Check, Settings, About), makes the query files that Tracker opens,
 * shows the ontologies and asks before the server quits (or restarts it).
 */
class SenUi {
public:
					SenUi(SenConfigHandler* config, RelationHandler* relations);
					~SenUi();

	/** Handle a message of the menu; false if it is not one. */
	bool			Handle(BMessage* message);

	/** Close the windows. */
	void			CloseWindows();

private:
	void			_Show(BMessenger* window, BWindow* (SenUi::*make)(), uint32 activate);
	BWindow*		_MakeInfo();
	BWindow*		_MakeCheck();
	BWindow*		_MakeSettings();
	BWindow*		_MakeAbout();

	void			_OpenObjects();
	void			_OpenClassification(const BMessage* message);
	void			_OpenClassified(const BMessage* message);
	void			_OpenPlugins(const BMessage* message);
	void			_SetContext(const BMessage* message);
	void			_AskToQuit();
	void			_Quit();
	void			_Restart();
	void			_OpenContexts();
	void			_OpenOntologies();
	void			_OpenOntology(const BMessage* message);
	void			_OpenType(const BMessage* message);
	void			_Report(const char* what, status_t result);

	SenConfigHandler*	fConfig;
	RelationHandler*	fRelations;
	BMessenger			fInfo;
	BMessenger			fCheck;
	BMessenger			fSettings;
	BMessenger			fAbout;
	/** @brief When the server was asked to stop but is no service of the launch daemon, it quits by itself after a moment. */
	BMessageRunner*		fQuitFallback;
};

}	// namespace ui
}	// namespace sen
