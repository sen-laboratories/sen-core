/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <set>
#include <string>

class BButton;
class BGroupView;
class BMessageRunner;
class BOutlineListView;
class BStringView;

namespace sen {
namespace settings {

/**
 * The window of the settings of SEN and of its plugins (docs/spec-settings-and-message-template.md in sento, section 6): a tree of what has settings on the left (Core,
 * and the plugins under the features that they have), and the form of the selected item on the right. The form is the view of Shoji for a message, loaded from there
 * as a replicant (SenSettingsFormHost.h): this window does not link it, and talks to it with messages. The settings come from the server (Core) or from the plugin as
 * messages, and what the user changes goes back to where it came from: this window never reads or writes a settings file.
 */
class SettingsWindow : public BWindow {
public:
					SettingsWindow();
	virtual			~SettingsWindow();

	virtual void	MessageReceived(BMessage* message);
	virtual bool	QuitRequested();

private:
	struct Target;

	void			_BuildTree();
	void			_Select(const Target* target);
	void			_Request(const Target& target);
	void			_Ready(BMessage* message);
	void			_Changed(BMessage* message);
	void			_Answered(BMessage* message);
	void			_ShowNote(const char* text, bool offerFile = false);
	void			_Status(const char* text);
	void			_UpdateButtons();

	BOutlineListView*	fTree;
	BGroupView*			fPane;
	BView*				fForm;
	BStringView*		fStatus;
	BButton*			fRevert;
	BButton*			fDefaults;
	BStringView*		fNote;

	BString				fSignature;			///< of what is shown: empty for Core
	BString				fName;
	bool				fCore;
	bool				fHasDefaults;
	bool				fModified;
	bool				fWaiting;			///< a request is out (a plugin that was started answers to the window, so there is a time limit)
	int32				fSerial;			///< of the request that is waited for: an answer to an older one is dropped
	BMessageRunner*		fTimeout;
	BMessage			fSettings;			///< as the form has them now (the last change)
	BMessage			fSchema;
	BMessage			fDefaultsMessage;
	std::set<std::string> fOpened;			///< the plugins that have a settings session: told when this window closes
};

}	// namespace settings
}	// namespace sen
