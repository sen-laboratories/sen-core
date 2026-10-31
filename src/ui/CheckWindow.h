/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Window.h>

#include "Worker.h"

class BButton;
class BOutlineListView;
class BStringView;

namespace sen {
namespace ui {

/**
 * The window of the Check item of the Deskbar menu: the consistency check (what the command line does with --check): duplicate SEN:IDs, files
 * of types that are not installed, volumes without an index. A problem is a line, with the files that it is about below it. The check is run by a
 * worker (Worker.h).
 */
class CheckWindow : public BWindow {
public:
					CheckWindow();
	virtual			~CheckWindow();

	virtual void	MessageReceived(BMessage* message);

private:
	void			_Check();
	void			_Fill(BMessage* result);
	void			_Clear();

	BOutlineListView*	fList;
	BStringView*		fStatus;
	BButton*			fCheck;
	bool				fChecking;
	Worker*				fWorker;
};

}	// namespace ui
}	// namespace sen
