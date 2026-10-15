/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

namespace sen {
namespace ui {

/**
 * The FileTypes of SEN in the place of the one of Haiku in the Preferences menu of the Deskbar: the entries of that menu are the links in the folder
 * deskbar/menu/Preferences, and the one of the user (a link called FileTypes in ~/config/settings/deskbar/menu/Preferences) is the first of the
 * folders that make the menu. The link is made when the FileTypes of SEN is installed and wanted (the setting), and taken away when it is not
 * (only the link that points to the FileTypes of SEN). Nothing of Haiku is touched.
 */
void ApplyFileTypesLink(bool wanted);

}	// namespace ui
}	// namespace sen
