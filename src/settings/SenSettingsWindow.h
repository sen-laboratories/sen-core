/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <map>
#include <string>

class BButton;
class BGroupView;
class BMessageRunner;
class BOutlineListView;
class BStringView;

namespace sen {
namespace settings {

class NoteView;

/**
 * The window of the settings of SEN and of its plugins (docs/spec-settings-and-message-template.md in sento, section 6): a tree of what has settings on the left (SEN > Core,
 * Plugins for what all plugins share, and below it the plugins under the features that they have), and the form of the selected item on the right. The form is the view of Shoji for a message, loaded from there
 * as a replicant (SenSettingsFormHost.h): this window does not link it, and talks to it with messages. The settings come from the server (Core) or from the plugin as
 * messages, and what the user changes goes back to where it came from: this window never reads or writes a settings file.
 */
class SettingsWindow : public BWindow {
public:
					/** @param pluginFile a plugin to show, instead of the copy that is installed (for the developer of a plugin)
					 *  @param node the node to select at first, `core` (default), `plugins` or `location` */
					SettingsWindow(const entry_ref* pluginFile = NULL, const char* node = NULL);
	virtual			~SettingsWindow();

	virtual void	MessageReceived(BMessage* message);
	virtual void	DispatchMessage(BMessage* message, BHandler* handler);
	virtual bool	QuitRequested();

private:
	struct Target;

	void			_BuildTree(const entry_ref* pluginFile, const char* node);
	float			_TreeWidth();
	void			_SelectBelow(int32 fullIndex);
	void			_Select(const Target* target);
	void			_Request(const Target& target);
	void			_Ready(BMessage* message);
	void			_Changed(BMessage* message);
	void			_Action(BMessage* message);
	void			_SendChanges(const BMessage& changes);
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
	NoteView*			fNote;
	int32				fFirstSelect;		///< the node that is selected when the window is shown

	BString				fSignature;			///< of what is shown: empty for Core
	BString				fPage;				///< of an owner with several parts of its settings: the one that is shown (empty: the first)
	BString				fName;
	bool				fCore;				///< what is shown comes from the server (Core, or what all plugins share: see fScope)
	BString				fScope;
	bool				fHasDefaults;
	bool				fModified;
	team_id				fStartedTeam;		///< of the plugin that was started for the request that is waited for
	bool				fWaiting;			///< a request is out (a plugin that was started answers to the window, so there is a time limit)
	int32				fSerial;			///< of the request that is waited for: an answer to an older one is dropped
	BMessageRunner*		fTimeout;
	BMessage			fSettings;			///< as the form has them now (the last change)
	BMessage			fSchema;
	BMessage			fDefaultsMessage;
	std::map<std::string, BMessenger> fSessions;	///< by signature: the instance of a plugin that has a settings session of this window (told when it closes)
};

}	// namespace settings
}	// namespace sen
