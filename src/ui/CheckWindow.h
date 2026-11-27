/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <String.h>
#include <Window.h>

#include "Worker.h"

class BButton;
class BOutlineListView;
class BStringView;

namespace sen {
namespace ui {

/**
 * The window of the Check & Repair item of the Deskbar menu: the consistency check (what the command line does with --check): duplicate SEN:IDs, files
 * of types that are not installed, volumes without an index, damaged indices. A problem is a line, with the files that it is about below it. The check is run by a
 * worker (Worker.h). "Repair" (enabled when indices are damaged) asks the server to make them again with sen::cmd::kRepairIndices, the message that
 * the command line and the script repair-sen-indices.sh send, then checks again.
 */
class CheckWindow : public BWindow {
public:
					CheckWindow();
	virtual			~CheckWindow();

	virtual void	MessageReceived(BMessage* message);

private:
	void			_Check();
	void			_Repair();
	void			_Fill(BMessage* result);
	void			_Clear();

	BOutlineListView*	fList;
	BStringView*		fStatus;
	BButton*			fCheck;
	BButton*			fRepair;
	bool				fChecking;
	bool				fRepairing;
	BString				fRepairNote;	///< what the last repair did, shown with the result of the check after it
	Worker*				fWorker;
};

}	// namespace ui
}	// namespace sen
