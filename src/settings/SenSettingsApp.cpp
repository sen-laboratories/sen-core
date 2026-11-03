/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenSettingsWindow.h"

#include <sen/SenMimeTypes.h>

#include <Application.h>
#include <Window.h>

/**
 * sen_settings: the application of the settings window of SEN. It is a single launch application that the Settings item of the Deskbar menu starts (the server does
 * it): a second start brings the window to the front. The window is here and not in the server so that a fault in the form of Shoji cannot take the server down.
 */
class SettingsApp : public BApplication {
public:
	SettingsApp() : BApplication(sen::kSettingsSignature), fWindow(NULL) {}

	virtual void ReadyToRun()
	{
		fWindow = new sen::settings::SettingsWindow();
		fWindow->Show();
	}

	virtual void MessageReceived(BMessage* message)
	{
		// started again without anything to do: the window comes to the front
		if (message->what == B_SILENT_RELAUNCH && fWindow != NULL && fWindow->Lock()) {
			fWindow->Activate();
			fWindow->Unlock();
			return;
		}
		BApplication::MessageReceived(message);
	}

private:
	BWindow*	fWindow;
};


int
main()
{
	SettingsApp app;
	if (app.InitCheck() != B_OK)
		return 1;
	app.Run();
	return 0;
}
