/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Bitmap.h>
#include <View.h>

namespace sen {
namespace ui {

/** The knot of SEN Labs as a 64x64 bitmap with alpha (see KnotIcon.h); the caller owns it. NULL if it could not be made. */
BBitmap* NewKnotBitmap();

/** A view that draws the knot in the size it is given. */
class KnotView : public BView {
public:
	KnotView(const char* name, float size);
	virtual ~KnotView();

	virtual void	Draw(BRect updateRect);
	virtual void	AttachedToWindow();

private:
	BBitmap*		fKnot;
};

}	// namespace ui
}	// namespace sen
