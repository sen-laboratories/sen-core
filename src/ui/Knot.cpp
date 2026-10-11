/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Knot.h"

#include "../deskbar/KnotIcon.h"

#include <string.h>

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

	// (B_RGBA32 is B, G, R, A in memory, like the array)
	uint8* bits = (uint8*)bitmap->Bits();
	for (int32 y = 0; y < size; y++)
		memcpy(bits + y * bitmap->BytesPerRow(), icon::kKnotBgra + y * size * 4, size * 4);
	return bitmap;
}

}	// namespace ui
}	// namespace sen
