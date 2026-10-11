/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Knot.h"

#include "../deskbar/KnotIcon.h"

#include <InterfaceDefs.h>

namespace sen {
namespace ui {

BBitmap*
NewKnotBitmap()
{
	const int32 size = icon::kKnotSize;
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}

	// (not premultiplied: B_RGBA32 is B, G, R, A in memory)
	uint8* bits = (uint8*)bitmap->Bits();
	for (int32 y = 0; y < size; y++) {
		uint8* row = bits + y * bitmap->BytesPerRow();
		for (int32 x = 0; x < size; x++) {
			row[x * 4 + 0] = icon::kKnotColor[2];
			row[x * 4 + 1] = icon::kKnotColor[1];
			row[x * 4 + 2] = icon::kKnotColor[0];
			row[x * 4 + 3] = icon::kKnotAlpha[y * size + x];
		}
	}
	return bitmap;
}


KnotView::KnotView(const char* name, float size)
	:
	BView(name, B_WILL_DRAW),
	fKnot(NewKnotBitmap())
{
	SetExplicitMinSize(BSize(size, size));
	SetExplicitMaxSize(BSize(size, size));
	SetExplicitPreferredSize(BSize(size, size));
}


KnotView::~KnotView()
{
	delete fKnot;
}


void
KnotView::AttachedToWindow()
{
	AdoptParentColors();
}


void
KnotView::Draw(BRect)
{
	if (fKnot == NULL)
		return;
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmap(fKnot, fKnot->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);
	SetDrawingMode(B_OP_COPY);
}

}	// namespace ui
}	// namespace sen
