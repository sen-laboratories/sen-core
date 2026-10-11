/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Window.h>

class BCheckBox;
class BMenuField;
class SenConfigHandler;

namespace sen {
namespace ui {

/**
 * The window of the Settings item of the Deskbar menu. For now two settings: the modifier key of the SEN mode of Tracker (dragging files with
 * it pressed relates them), and whether Akita is to be used instead of Tracker. They are saved when they are changed, in the settings of SEN.
 */
class SettingsWindow : public BWindow {
public:
					SettingsWindow(SenConfigHandler* config);

	virtual void	MessageReceived(BMessage* message);

private:
	SenConfigHandler*	fConfig;
	BMenuField*			fKeyField;
	BCheckBox*			fAkita;
};

}	// namespace ui
}	// namespace sen
