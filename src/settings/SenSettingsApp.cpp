/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SenSettingsWindow.h"

#include <sen/SenMimeTypes.h>

#include <Application.h>
#include <Entry.h>
#include <Path.h>
#include <String.h>
#include <string.h>
#include <Window.h>

/**
 * sen_settings: the application of the settings window of SEN. It is a single launch application that the Settings item of the Deskbar menu starts (the server does
 * it): a second start brings the window to the front. The window is here and not in the server so that a fault in the form of Shoji cannot take the server down.
 */
class SettingsApp : public BApplication {
public:
	SettingsApp() : BApplication(sen::kSettingsSignature), fWindow(NULL), fHasPlugin(false) {}

	/** `sen_settings --plugin <file>`: shows the settings of that plugin file instead of the installed copy of its signature (for the developer of a plugin);
	 *  `--node core|plugins` starts with that node selected (Core is the first) */
	virtual void ArgvReceived(int32 argc, char** argv)
	{
		for (int32 i = 1; i + 1 < argc; i++) {
			if (strcmp(argv[i], "--plugin") == 0 && get_ref_for_path(argv[i + 1], &fPlugin) == B_OK)
				fHasPlugin = true;
			if (strcmp(argv[i], "--node") == 0)
				fNode = argv[i + 1];
		}
	}

	virtual void ReadyToRun()
	{
		fWindow = new sen::settings::SettingsWindow(fHasPlugin ? &fPlugin : NULL, fNode.IsEmpty() ? NULL : fNode.String());
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
	entry_ref	fPlugin;
	bool		fHasPlugin;
	BString		fNode;
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
