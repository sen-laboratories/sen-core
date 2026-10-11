/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "StatItem.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <View.h>

namespace sen {
namespace ui {

StatItem::StatItem(uint32 level, const BString& text, const BString& detail, const BString& number, bool bold)
	:
	BListItem(level),
	fText(text),
	fDetail(detail),
	fNumber(number),
	fBold(bold),
	fBaseline(0)
{
}


void
StatItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);
	font_height height;
	font->GetHeight(&height);
	fBaseline = height.ascent;
	SetHeight(height.ascent + height.descent + height.leading + 4);
}


void
StatItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color background = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR) : ui_color(B_LIST_BACKGROUND_COLOR);
	rgb_color text = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR) : ui_color(B_LIST_ITEM_TEXT_COLOR);

	if (IsSelected() || complete) {
		owner->SetHighColor(background);
		owner->FillRect(frame);
	}

	BFont font;
	owner->GetFont(&font);
	if (fBold)
		font.SetFace(B_BOLD_FACE);
	else
		font.SetFace(B_REGULAR_FACE);
	owner->SetFont(&font);

	float y = frame.top + fBaseline + 2;
	float right = owner->Bounds().right - 12;

	// the number on the right, the detail (the MIME type) in a column of its own, the text at the left
	if (!fNumber.IsEmpty()) {
		owner->SetHighColor(text);
		owner->DrawString(fNumber.String(), BPoint(right - owner->StringWidth(fNumber.String()), y));
	}
	if (!fDetail.IsEmpty()) {
		rgb_color light = tint_color(text, IsSelected() ? B_DARKEN_1_TINT : B_LIGHTEN_1_TINT);
		owner->SetHighColor(light);
		float x = owner->Bounds().Width() * 0.45f;
		if (x < frame.left + 40)
			x = frame.left + 40;
		// (not over the number)
		float numberWidth = fNumber.IsEmpty() ? 0 : owner->StringWidth(fNumber.String()) + 16;
		float room = right - numberWidth - x;
		BString shown(fDetail);
		if (room > 20)
			owner->TruncateString(&shown, B_TRUNCATE_MIDDLE, room);
		else
			shown.SetTo("");
		owner->DrawString(shown.String(), BPoint(x, y));
	}
	owner->SetHighColor(text);
	owner->DrawString(fText.String(), BPoint(frame.left + 4, y));
}

void
ExpandAllItems(BOutlineListView* list)
{
	// (from the end: opening a branch adds its items to the list)
	for (int32 index = list->FullListCountItems() - 1; index >= 0; index--) {
		BListItem* item = list->FullListItemAt(index);
		if (item != NULL && list->CountItemsUnder(item, true) > 0)
			list->Expand(item);
	}
}

}	// namespace ui
}	// namespace sen
