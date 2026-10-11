/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Bitmap.h>

namespace sen {
namespace ui {

/** The knot of SEN Labs as a 64x64 bitmap with alpha (see KnotIcon.h); the caller owns it. NULL if it could not be made. */
BBitmap* NewKnotBitmap();

}	// namespace ui
}	// namespace sen
