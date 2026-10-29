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
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <TranslationUtils.h>
#include <Url.h>
#include <View.h>

namespace sen {
namespace ui {

static const uint32 kActivate = 'ABac';
static const uint32 kSupport = 'ABsp';
static const float kLogoSize = 128;
// the text of the dialog starts a little inside its view (BTextView): the link is aligned with it
static const float kTextInset = 4;

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
			*width = kTextInset + StringWidth(fText.String());
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
		BPoint start(kTextInset, ceilf(fontHeight.ascent));
		DrawString(fText.String(), start);
		float width = StringWidth(fText.String());
		StrokeLine(BPoint(start.x, start.y + 2), BPoint(start.x + width, start.y + 2));
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


/** The logo of sen-core (a resource of the program), scaled to the height that the stripe shows it in; NULL if there is none. */
static BBitmap*
_Logo()
{
	BBitmap* logo = BTranslationUtils::GetBitmap("about-logo");
	BBitmap* scaled = ScaleDown(logo, kLogoSize);
	delete logo;
	return scaled;
}


AboutWindow::AboutWindow()
	:
	AboutDialog("SEN", stats::VersionOnly().String(), "Semantic Extensions Native",
		"Copyright " B_UTF8_COPYRIGHT " 2020-2026 SEN Labs e.U.\nMIT License", _Logo())
{
	AddContent(new LinkView("web", "sen-labs.org", kWebsite));
	AddButton("Support this project", new BMessage(kSupport));
	CenterOnScreen();	// (the window is shown by SenUi, which does not centre it)
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
			AboutDialog::MessageReceived(message);
	}
}

}	// namespace ui
}	// namespace sen
