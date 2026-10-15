/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "FileTypesLink.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>
#include <SymLink.h>

#include <spdlog/spdlog.h>

#include <unistd.h>

namespace sen {
namespace ui {

static const char* const kProgram = "SEN FileTypes";
static const char* const kLinkName = "FileTypes";

void
ApplyFileTypesLink(bool wanted)
{
	BPath program;
	if (find_directory(B_SYSTEM_PREFERENCES_DIRECTORY, &program) != B_OK || program.Append(kProgram) != B_OK)
		return;
	bool installed = BEntry(program.Path()).Exists();

	BPath folder;
	if (find_directory(B_USER_DESKBAR_DIRECTORY, &folder) != B_OK || folder.Append("Preferences") != B_OK)
		return;
	BPath link(folder);
	if (link.Append(kLinkName) != B_OK)
		return;

	BSymLink existing(link.Path());
	char target[B_PATH_NAME_LENGTH] = "";
	bool ours = existing.InitCheck() == B_OK && existing.IsSymLink() && existing.ReadLink(target, sizeof(target) - 1) > 0
		&& BString(target).FindFirst(kProgram) >= 0;
	// (a link that is the user's own is none of our business)
	if (existing.InitCheck() == B_OK && !ours)
		return;

	if (wanted && installed) {
		if (ours)
			return;
		if (create_directory(folder.Path(), 0755) != B_OK)
			return;
		if (symlink(program.Path(), link.Path()) != 0)
			spdlog::warn("could not link the FileTypes of SEN into the Preferences menu of the Deskbar");
		else
			spdlog::info("the FileTypes of SEN is in the Preferences menu of the Deskbar");
		return;
	}

	if (ours) {
		unlink(link.Path());
		// (the folder goes if it is empty: it was made for the link)
		rmdir(folder.Path());
	}
}

}	// namespace ui
}	// namespace sen
