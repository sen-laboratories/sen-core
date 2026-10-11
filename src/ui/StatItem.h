/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <ListItem.h>
#include <OutlineListView.h>
#include <String.h>

namespace sen {
namespace ui {

/**
 * A row of the outline lists of the windows: a text, and optionally a second text (the MIME type, in a lighter colour) and a number on the
 * right. The tree (the indentation and the arrows to open and close) is drawn by the BOutlineListView.
 */
class StatItem : public BListItem {
public:
	StatItem(uint32 level, const BString& text, const BString& detail = BString(), const BString& number = BString(),
		bool bold = false);

	virtual void	DrawItem(BView* owner, BRect frame, bool complete = false);
	virtual void	Update(BView* owner, const BFont* font);

private:
	BString			fText;
	BString			fDetail;
	BString			fNumber;
	bool			fBold;
	float			fBaseline;
};

/** Open all the branches of an outline list. */
void ExpandAllItems(BOutlineListView* list);

}	// namespace ui
}	// namespace sen
