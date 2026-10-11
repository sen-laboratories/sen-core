/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Window.h>

namespace sen {
namespace ui {

/** The window of the About item of the Deskbar menu: the knot, the name and version, the copyright. */
class AboutWindow : public BWindow {
public:
					AboutWindow();

	virtual void	MessageReceived(BMessage* message);
};

}	// namespace ui
}	// namespace sen
