/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "SettingsWindow.h"

#include "../config/SenConfigHandler.h"

#include <sen/Sen.h>

#include <CheckBox.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>

namespace sen {
namespace ui {

static const uint32 kSetKey = 'SWky';
static const uint32 kSetAkita = 'SWak';
static const uint32 kSetFileTypes = 'SWft';
static const uint32 kActivate = 'SWac';


struct KeyChoice {
	const char*	label;
	int32		key;
};

static const KeyChoice kKeys[] = {
	{"Command (the Apple key, Alt on a PC keyboard)", B_COMMAND_KEY},
	{"Option (the Windows key on a PC keyboard)", B_OPTION_KEY},
	{"Control", B_CONTROL_KEY},
	{"Shift", B_SHIFT_KEY},
};


SettingsWindow::SettingsWindow(SenConfigHandler* config)
	:
	BWindow(BRect(160, 160, 600, 400), "SEN Settings", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fConfig(config)
{
	BMessage settings;
	config->GetConfig(&settings);
	int32 current = settings.GetInt32(sen::key::kModifierKey, B_COMMAND_KEY);
	bool useAkita = settings.GetBool(sen::key::kUseAkita, false);
	bool useFileTypes = settings.GetBool(sen::key::kUseSenFileTypes, true);

	BPopUpMenu* menu = new BPopUpMenu("key");
	for (const KeyChoice& choice : kKeys) {
		BMessage* message = new BMessage(kSetKey);
		message->AddInt32("key", choice.key);
		BMenuItem* item = new BMenuItem(choice.label, message);
		item->SetMarked(choice.key == current);
		menu->AddItem(item);
	}
	menu->SetTargetForItems(this);
	fKeyField = new BMenuField("keyfield", "SEN key:", menu);

	BStringView* keyHelp = new BStringView("keyhelp", "Drag files onto others with this key pressed to relate them.");
	keyHelp->SetFont(be_plain_font);

	fAkita = new BCheckBox("akita", "Use Akita instead of Tracker", new BMessage(kSetAkita));
	fAkita->SetValue(useAkita ? B_CONTROL_ON : B_CONTROL_OFF);
	BStringView* akitaHelp = new BStringView("akitahelp", "Takes effect when the system is started the next time.");

	fFileTypes = new BCheckBox("filetypes", "Use the FileTypes of SEN instead of the one of Haiku", new BMessage(kSetFileTypes));
	fFileTypes->SetValue(useFileTypes ? B_CONTROL_ON : B_CONTROL_OFF);
	BStringView* fileTypesHelp = new BStringView("filetypeshelp", "Where SEN shows a type and its attributes (the Ontologies menu).");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fKeyField)
		.Add(keyHelp)
		.AddStrut(B_USE_BIG_SPACING)
		.Add(fAkita)
		.Add(akitaHelp)
		.AddStrut(B_USE_BIG_SPACING)
		.Add(fFileTypes)
		.Add(fileTypesHelp)
	.End();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSetKey:
		{
			BMessage change;
			change.AddInt32(sen::key::kModifierKey, message->GetInt32("key", B_COMMAND_KEY));
			fConfig->UpdateSettings(&change);
			break;
		}
		case kSetAkita:
		{
			BMessage change;
			change.AddBool(sen::key::kUseAkita, fAkita->Value() == B_CONTROL_ON);
			fConfig->UpdateSettings(&change);
			break;
		}
		case kSetFileTypes:
		{
			BMessage change;
			change.AddBool(sen::key::kUseSenFileTypes, fFileTypes->Value() == B_CONTROL_ON);
			fConfig->UpdateSettings(&change);
			break;
		}
		case kActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}

}	// namespace ui
}	// namespace sen
