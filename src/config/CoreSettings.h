/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <SupportDefs.h>

#include <string>
#include <vector>

class SenConfigHandler;

namespace sen {
namespace core {

/**
 * The settings of the server as the settings window shows them (sen_settings, with the form of Shoji, see SenSettingsForm.h): what the server answers to
 * kCoreSettings and kCoreSettingsSet.
 *
 * The fields are the SEN key (`SEN:modifierKey`), whether the FileTypes of SEN is used (`SEN:useFileTypes`) and, if Akita is installed, whether it takes the place of
 * Tracker at login (`akita`: not a setting of the server but a file of the launch daemon, which the command `akita` makes), and the name of the Library folder
 * (`SEN:libraryName`), and whether what is made new belongs to the current context (`SEN:associateCurrentContext`, default on).
 *
 * The settings that all plugins share (the node *Plugins* of the window; the request has `SEN:scope` = `plugins`) are the same thing for another set of fields:
 * where plugins put the entities that they find (`SEN:extractEntitiesInto`: `files` or `attributes`), whether the user is asked which one is meant if a lookup finds several
 * answers (`SEN:askIfSeveral`) and the most entities of a type that are made of a file (`SEN:maxEntities`, 0 to 100). Plugins read them with sen::plugins::LoadSharedSettings().
 */

/** The settings as they are now, the schema (labels, help, the choices of the key), and the defaults. */
void DescribeSettings(SenConfigHandler* config, BMessage* settings, BMessage* schema, BMessage* defaults);

/**
 * Changes settings: every field of `changes` is a value under its name (the whole message after a revert or defaults). Valid values are applied and stored, the others
 * are left; the names that were refused are in `refused`. B_OK if all were applied, else the first error.
 */
status_t ApplySettings(SenConfigHandler* config, const BMessage& changes, std::vector<std::string>* refused);

/** The settings that all plugins share (see above), as DescribeSettings() describes the settings of the server. */
void DescribePluginSettings(SenConfigHandler* config, BMessage* settings, BMessage* schema, BMessage* defaults);

/** As ApplySettings(), for the settings that all plugins share. */
status_t ApplyPluginSettings(SenConfigHandler* config, const BMessage& changes, std::vector<std::string>* refused);

}	// namespace core
}	// namespace sen
