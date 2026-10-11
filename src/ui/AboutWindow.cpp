/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "AboutWindow.h"

#include "Knot.h"
#include "../stats/SenStats.h"

#include <Font.h>
#include <LayoutBuilder.h>
#include <StringView.h>

namespace sen {
namespace ui {

static const uint32 kActivate = 'ABac';


AboutWindow::AboutWindow()
	:
	BWindow(BRect(180, 180, 520, 330), "About SEN", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	BStringView* name = new BStringView("name", "SEN");
	BFont big(be_bold_font);
	big.SetSize(big.Size() * 1.6f);
	name->SetFont(&big);

	BString version("Server ");
	version << stats::VersionOnly();

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_BIG_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new KnotView("knot", 96))
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.Add(name)
			.Add(new BStringView("tagline", "Semantic ExteNsions for Haiku"))
			.Add(new BStringView("version", version.String()))
			.AddStrut(B_USE_DEFAULT_SPACING)
			.Add(new BStringView("copyright", "\xc2\xa9 2020-2026 SEN Labs e.U."))
			.Add(new BStringView("license", "MIT License"))
			.Add(new BStringView("web", "github.com/sen-laboratories"))
		.End()
	.End();
}


void
AboutWindow::MessageReceived(BMessage* message)
{
	if (message->what == kActivate)
		Activate();
	else
		BWindow::MessageReceived(message);
}

}	// namespace ui
}	// namespace sen
