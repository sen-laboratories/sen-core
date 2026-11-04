/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <Path.h>
#include <SupportDefs.h>

#include <string>

namespace sen {
namespace library {

/**
 * The Library of a context: the folder where the entities that are made new go (see SenLibrary.h, sen::cmd::kContextLibrary).
 *
 * The Library of the default context is `~/Library`, that of another context `~/Library/Contexts/<name>`; a context can name another folder in the attribute
 * `SEN:libraryPath` of its folder (an absolute path, or one that starts with `~/`). The folder of an entity type in it is named by the resource `SEN:LIBRARY` of the type (the annotation
 * `library` of the ontology), else by the short description of the type.
 */

/** The name of the folder for an entity type: `People`. Empty for a type that is not an entity type (`entity/...`). */
std::string FolderNameOf(const char* mimeType);

/**
 * Finds (and makes) the Library of a context, or the folder of a type in it.
 * @param settings the settings of SEN (the current context is in them)
 * @param context the name of the context, NULL or empty: the current one
 * @param type an entity type, NULL: the Library itself
 * @param create make the folders that are missing
 * @param folder gets the path
 * @param contextUsed gets the name of the context
 * @return B_OK, B_ENTRY_NOT_FOUND (no such context), B_BAD_VALUE (not an entity type), or the error of making the folders
 */
status_t Resolve(const BMessage& settings, const char* context, const char* type, bool create, BPath* folder, std::string* contextUsed);

}	// namespace library
}	// namespace sen
