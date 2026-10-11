/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "AboutWindow.h"

#include "../stats/SenStats.h"

#include <math.h>

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Cursor.h>
#include <Entry.h>
#include <Font.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <Roster.h>
#include <StringView.h>
#include <TranslationUtils.h>
#include <Url.h>
#include <View.h>

namespace sen {
namespace ui {

static const uint32 kActivate = 'ABac';
static const uint32 kSupport = 'ABsp';
static const float kLogoSize = 144;

static const char* const kWebsite = "https://sen-labs.org";
// the page for donations: a page of the documentation (the button of Liberapay is a script, which needs a page), else the page of Liberapay
static const char* const kDonatePage = "https://liberapay.com/sen-labs/donate";


/** The support page of the documentation: next to the program in the folder that it is built in (docs/), or where the package puts it. */
static bool
FindSupportPage(BPath* page)
{
	BPath candidate;

	app_info info;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK) {
		BPath program(&info.ref);
		if (program.GetParent(&candidate) == B_OK && candidate.GetParent(&candidate) == B_OK
				&& candidate.Append("docs/support.html") == B_OK && BEntry(candidate.Path()).Exists()) {
			*page = candidate;
			return true;
		}
	}

	static const char* const kPackaged[] = {"packages/sen_core/support.html", "sen_core/support.html"};
	for (const char* relative : kPackaged) {
		if (find_directory(B_SYSTEM_DOCUMENTATION_DIRECTORY, &candidate) == B_OK && candidate.Append(relative) == B_OK
				&& BEntry(candidate.Path()).Exists()) {
			*page = candidate;
			return true;
		}
	}
	return false;
}


/** Open a page with the preferred web browser. */
static void
OpenPage(const char* url)
{
	BUrl address(url, true);
	address.OpenWithPreferredApplication();
}


/** A link: the text in the colour of links, underlined; a click opens the page in the web browser. */
class LinkView : public BView {
public:
	LinkView(const char* name, const char* text, const char* url)
		:
		BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fText(text),
		fUrl(url)
	{
		SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_UNSET));
	}

	virtual void AttachedToWindow()
	{
		AdoptParentColors();
		if (ViewUIColor() == B_NO_COLOR)
			SetLowColor(ViewColor());
		else
			SetLowUIColor(ViewUIColor());
		SetHighUIColor(B_LINK_TEXT_COLOR);
	}

	virtual void GetPreferredSize(float* width, float* height)
	{
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		if (width != NULL)
			*width = StringWidth(fText.String());
		if (height != NULL)
			*height = ceilf(fontHeight.ascent + fontHeight.descent + fontHeight.leading) + 2;
	}

	virtual BSize MinSize() { float w, h; GetPreferredSize(&w, &h); return BSize(w, h); }
	virtual BSize MaxSize() { float w, h; GetPreferredSize(&w, &h); return BSize(w, h); }
	virtual BSize PreferredSize() { float w, h; GetPreferredSize(&w, &h); return BSize(w, h); }

	virtual void Draw(BRect)
	{
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		BPoint start(0, ceilf(fontHeight.ascent));
		DrawString(fText.String(), start);
		float width = StringWidth(fText.String());
		StrokeLine(BPoint(0, start.y + 2), BPoint(width, start.y + 2));
	}

	virtual void MouseMoved(BPoint, uint32 transit, const BMessage*)
	{
		if (transit == B_ENTERED_VIEW) {
			BCursor hand(B_CURSOR_ID_FOLLOW_LINK);
			SetViewCursor(&hand);
		} else if (transit == B_EXITED_VIEW)
			SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
	}

	virtual void MouseDown(BPoint)
	{
		OpenPage(fUrl.String());
	}

private:
	BString	fText;
	BString	fUrl;
};


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
			.Add(new LinkView("web", "sen-labs.org", kWebsite))
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("support", "Support this project", new BMessage(kSupport)))
		.End()
	.End();
}


void
AboutWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kActivate:
			Activate();
			break;
		case kSupport:
		{
			BPath page;
			if (FindSupportPage(&page)) {
				BUrl address(page);
				address.OpenWithPreferredApplication();
			} else
				OpenPage(kDonatePage);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}

}	// namespace ui
}	// namespace sen
