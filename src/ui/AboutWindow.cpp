/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "AboutWindow.h"

#include "../stats/SenStats.h"

#include <Bitmap.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TranslationUtils.h>
#include <View.h>

namespace sen {
namespace ui {

static const uint32 kActivate = 'ABac';
static const float kLogoSize = 144;


/** Draws a bitmap in a fixed size; it is not the logo that decides how large the window is. */
class LogoView : public BView {
public:
	LogoView(const char* name, BBitmap* bitmap, float size)
		:
		BView(name, B_WILL_DRAW),
		fBitmap(bitmap)
	{
		SetExplicitMinSize(BSize(size, size));
		SetExplicitMaxSize(BSize(size, size));
		SetExplicitPreferredSize(BSize(size, size));
	}

	virtual ~LogoView() { delete fBitmap; }

	virtual void AttachedToWindow() { AdoptParentColors(); }

	virtual void Draw(BRect)
	{
		if (fBitmap == NULL)
			return;
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds(), B_FILTER_BITMAP_BILINEAR);
		SetDrawingMode(B_OP_COPY);
	}

private:
	BBitmap*	fBitmap;
};


AboutWindow::AboutWindow()
	:
	BWindow(BRect(180, 180, 560, 400), "About SEN", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE)
{
	BStringView* name = new BStringView("name", "SEN");
	BFont big(be_bold_font);
	big.SetSize(big.Size() * 1.8f);
	name->SetFont(&big);

	BString version("Server ");
	version << stats::VersionOnly();

	// the logo (a resource of the program) next to the middle block of the text, the two centred on the same line; the copyright below
	BBitmap* logo = BTranslationUtils::GetBitmap("about-logo");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_BIG_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
			.Add(new LogoView("logo", logo, kLogoSize))
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.AddGlue()
				.Add(name)
				.Add(new BStringView("tagline", "Semantic Extensions Native"))
				.Add(new BStringView("version", version.String()))
				.AddGlue()
			.End()
		.End()
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
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
