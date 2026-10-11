/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Window.h>

class BButton;
class BOutlineListView;
class BStringView;

namespace sen {
namespace ui {

/**
 * The window of the Info item of the Deskbar menu: the overview of SEN as a tree (what the command line shows with --info and --types): the
 * objects per volume, and the meta types, entities, classifications and relations that are installed, with the number of files of each. The
 * statistics are gathered in a thread of their own, as the queries over all volumes can take a while.
 */
class InfoWindow : public BWindow {
public:
					InfoWindow();

	virtual void	MessageReceived(BMessage* message);

private:
	void			_Gather();
	void			_Fill(BMessage* result);
	void			_Clear();

	static int32	_GatherEntry(void* data);

	BOutlineListView*	fList;
	BStringView*		fStatus;
	BButton*			fRefresh;
	bool				fGathering;
};

}	// namespace ui
}	// namespace sen
