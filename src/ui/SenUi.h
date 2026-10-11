/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Messenger.h>

class BMessage;
class BWindow;
class RelationHandler;
class SenConfigHandler;

namespace sen {
namespace ui {

/**
 * What the Deskbar menu of SEN does: the item in the Deskbar (see DeskbarView) sends messages to the server (the Deskbar is another team and
 * is not to hold up windows or queries), and this shows the windows (Info, Check, Settings, About), makes the query files that Tracker opens
 * and asks before the server quits.
 */
class SenUi {
public:
					SenUi(SenConfigHandler* config, RelationHandler* relations);

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
	void			_SetContext(const BMessage* message);
	void			_AskToQuit();
	void			_Report(const char* what, status_t result);

	SenConfigHandler*	fConfig;
	RelationHandler*	fRelations;
	BMessenger			fInfo;
	BMessenger			fCheck;
	BMessenger			fSettings;
	BMessenger			fAbout;
};

}	// namespace ui
}	// namespace sen
