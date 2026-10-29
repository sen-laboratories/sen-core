/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <sen/SenStripeDialog.h>

namespace sen {
namespace ui {

/**
 * The window of the About item of the Deskbar menu: the dialog with the stripe that all About windows of SEN have (sento: SenStripeDialog.h), with the logo, the
 * name and the full version, the copyright, a link to the website and a button for support.
 */
class AboutWindow : public AboutDialog {
public:
					AboutWindow();

	virtual void	MessageReceived(BMessage* message);
};

}	// namespace ui
}	// namespace sen
