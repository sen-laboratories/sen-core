/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

#include "Library.h"

#include <sen/Sen.h>

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <Node.h>
#include <String.h>

#include <string.h>
#include <sys/stat.h>

namespace sen {
namespace library {

// rwxr-xr-x (a mode: B_READ_WRITE is a flag of an open file, not this)
static const mode_t kFolderMode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;


/** The text of an attribute of a node, empty if it has none. */
static std::string
StringAttribute(const BPath& path, const char* attribute)
{
	BNode node(path.Path());
	BString value;
	if (node.InitCheck() == B_OK && node.ReadAttrString(attribute, &value) == B_OK)
		return value.String();
	return std::string();
}


/** A name that is one folder: no slash, no leading or trailing blanks. */
static std::string
FolderName(std::string name)
{
	for (char& c : name) {
		if (c == '/' || c == ':')
			c = '-';
	}
	const size_t first = name.find_first_not_of(" \t");
	const size_t last = name.find_last_not_of(" \t");
	if (first == std::string::npos || name == "." || name == "..")
		return std::string();
	return name.substr(first, last - first + 1);
}


std::string
FolderNameOf(const char* mimeType)
{
	if (mimeType == NULL || strncmp(mimeType, "entity/", 7) != 0 || mimeType[7] == '\0')
		return std::string();
	BMimeType type(mimeType);
	if (type.InitCheck() != B_OK || !type.IsValid())
		return std::string();

	// the plural of the type (the slot `plural` of the root class Entity of the ontology) is an attribute of the entry of the type in the MIME database: the user's, then the system's
	std::string name;
	for (directory_which which : {B_USER_SETTINGS_DIRECTORY, B_SYSTEM_DATA_DIRECTORY, B_USER_DATA_DIRECTORY}) {
		BPath path;
		if (find_directory(which, &path) != B_OK || path.Append("mime_db") != B_OK || path.Append(mimeType) != B_OK)
			continue;
		name = FolderName(StringAttribute(path, sen::attr::kPlural));
		if (!name.empty())
			return name;
	}

	// no plural: the short description of the type, else the last part of its name
	char description[B_MIME_TYPE_LENGTH];
	if (type.GetShortDescription(description) == B_OK)
		name = FolderName(description);
	if (name.empty())
		name = FolderName(strrchr(mimeType, '/') + 1);
	return name;
}


/** The Library of a context: what the folder of the context says, else the place that is made from the name. */
static status_t
LibraryOf(const std::string& context, const std::string& libraryName, BPath* library)
{
	BPath contextFolder;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &contextFolder) != B_OK || contextFolder.Append("sen") != B_OK
			|| contextFolder.Append(sen::config::kContextsDir) != B_OK || contextFolder.Append(context.c_str()) != B_OK)
		return B_ERROR;
	BEntry entry(contextFolder.Path());
	if (!entry.Exists() || !entry.IsDirectory())
		return B_ENTRY_NOT_FOUND;

	BPath home;
	if (find_directory(B_USER_DIRECTORY, &home) != B_OK)
		return B_ERROR;

	// a context that names its Library: an absolute path, or one in the home folder
	std::string named = StringAttribute(contextFolder, sen::attr::kLibraryPath);
	if (named.compare(0, 2, "~/") == 0)
		named = std::string(home.Path()) + "/" + named.substr(2);
	if (!named.empty() && named[0] == '/') {
		library->SetTo(named.c_str());
		return library->InitCheck();
	}

	library->SetTo(home.Path(), libraryName.c_str());
	if (context != sen::config::kContextDefault)
		library->Append(sen::config::kLibraryContextsDir);
	if (context != sen::config::kContextDefault)
		library->Append(FolderName(context).c_str());
	return library->InitCheck();
}


status_t
Resolve(const BMessage& settings, const char* context, const char* type, bool create, BPath* folder, std::string* contextUsed)
{
	std::string name = context != NULL ? context : "";
	if (name.empty())
		name = settings.GetString(sen::key::kCurrentContext, sen::config::kContextDefault);
	if (name.empty() || FolderName(name) != name)
		return B_BAD_VALUE;

	// the name of the Library folder in the home folder is a setting (a name that is not one folder is not used)
	std::string libraryName = FolderName(settings.GetString(sen::key::kLibraryName, sen::config::kLibraryDir));
	if (libraryName.empty())
		libraryName = sen::config::kLibraryDir;

	status_t result = LibraryOf(name, libraryName, folder);
	if (result != B_OK)
		return result;
	if (contextUsed != NULL)
		*contextUsed = name;

	if (type != NULL && type[0] != '\0') {
		const std::string typeFolder = FolderNameOf(type);
		if (typeFolder.empty())
			return B_BAD_VALUE;
		result = folder->Append(typeFolder.c_str());
		if (result != B_OK)
			return result;
	}

	if (create && !BEntry(folder->Path()).Exists()) {
		result = create_directory(folder->Path(), kFolderMode);
		if (result != B_OK && result != B_FILE_EXISTS)
			return result;
	}
	return B_OK;
}

}	// namespace library
}	// namespace sen
