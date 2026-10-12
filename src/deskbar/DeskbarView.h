/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <View.h>

class BBitmap;
class BMenu;
class BPopUpMenu;

namespace sen {
namespace deskbar {

/** The name of the item (and of its view): the Deskbar finds it by it. */
extern const char* const kItemName;

/**
 * The item of SEN in the Deskbar, like the one of the mail server: the knot, which opens the query with all SEN objects (the knowledge graph as
 * a flat list), and a menu (secondary mouse button) for the context, the classifications and the windows of the server.
 *
 * The view is a replicant: it lives in the Deskbar, loaded from the executable of the server (see instantiate_deskbar_item), and does nothing
 * but show the menu and send messages to the server, which shows the windows and makes the queries.
 */
class DeskbarView : public BView {
public:
						DeskbarView(BRect frame);
						DeskbarView(BMessage* archive);
	virtual				~DeskbarView();

	static DeskbarView*	Instantiate(BMessage* archive);
	virtual	status_t	Archive(BMessage* archive, bool deep = true) const;

	virtual void		AttachedToWindow();
	virtual void		Draw(BRect updateRect);
	virtual void		MouseDown(BPoint where);
	virtual void		MouseUp(BPoint where);
	virtual void		MessageReceived(BMessage* message);

private:
	void				_Init();
	BPopUpMenu*			_BuildMenu();
	void				_AddContextMenu(BMenu* menu);
	void				_AddClassificationMenus(BMenu* menu);
	void				_AddOntologyMenu(BMenu* menu);
	void				_Send(uint32 command);

	BBitmap*			fKnot;
	int32				fLastButtons;
};

}	// namespace deskbar
}	// namespace sen
