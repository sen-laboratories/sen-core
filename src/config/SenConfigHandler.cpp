/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "SenConfigHandler.h"
#include <sen/Sen.h>

#include <AppFileInfo.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Resources.h>
#include <spdlog/spdlog.h>

SenConfigHandler::SenConfigHandler()
    : BHandler("SenConfigHandler")
{
    fSettingsDir = new BDirectory();
    fSettingsMsg = new BMessage();
}

SenConfigHandler::~SenConfigHandler()
{
    delete fSettingsDir;
    delete fSettingsMsg;
}

// incrementally build up a consistent configuration
status_t SenConfigHandler::Init()
{
    status_t status = LoadSettings(fSettingsMsg);

    if (status != B_OK) {
        spdlog::error("failed to read settings: {}", strerror(status));
    }

    return status;
}

status_t SenConfigHandler::LoadSettings(BMessage* settingsMessage)
{
    BPath path;
    status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK) {
        spdlog::error("could not find user settings directory: {}", strerror(status));
		return status;
    }

    path.Append("sen");
    // set global settings directory here
    fSettingsDir->SetTo(path.Path());

    // read or create and init settings file
    BEntry settingsDirEntry(path.Path());
    BEntry settingsFileEntry(fSettingsDir, "sen.settings");

    if (! settingsDirEntry.Exists() || ! settingsFileEntry.Exists()) {
        status = InitDefaultSettings(&path, settingsMessage);
        if (status == B_OK) {
            spdlog::info("successfully initialized settings.");
            status = SaveSettings(settingsMessage);
        } else {
            spdlog::error("failed to initialize settings: {}", strerror(status));
            return status;
        }
    } else {
        spdlog::info("reading exising settings from {}" B_UTF8_ELLIPSIS, path.Path());
        BFile settingsFile(&settingsFileEntry, B_READ_WRITE);

        status = settingsFile.InitCheck();
        if (status == B_OK) {
            status = settingsMessage->Unflatten(&settingsFile);
        }
        if (status != B_OK) {
            spdlog::error("could not retrieve settings: {}", strerror(status));
            return status;
        }
    }
    spdlog::info("successfully retrieved settings:");
    settingsMessage->PrintToStream();

    return status;
}

status_t SenConfigHandler::InitDefaultSettings(BPath* settingsPath, BMessage* settingsMessage)
{
    status_t status;

    spdlog::info("setting up default settings in {}" B_UTF8_ELLIPSIS, settingsPath->Path());

    // checking and building sen settings directories incrementally
    BEntry settingsDirEntry(settingsPath->Path());
    BDirectory settingsDir(&settingsDirEntry);

	if (! settingsDirEntry.Exists())
		status = settingsDir.CreateDirectory(settingsPath->Path(), NULL);

	if (status != B_OK) {
        spdlog::error("could not access settings path '{}': {}", settingsPath->Path(), strerror(status));
        return status;
    }

    BPath path(*settingsPath);    // working path for setting up directories

    settingsMessage->AddString(sen::key::kConfigPath, path.Path());
    settingsDir.SetTo(path.Path());

    // set up context directories
    path.Append(sen::config::kContextsDir);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status != B_OK) {
            spdlog::error("failed to set up context base path: {}", strerror(status));
            return status;
        }
    }
    settingsDir.SetTo(path.Path());
    settingsMessage->AddString(sen::key::kContextPath, path.Path());

    entry_ref contextBaseRef;
    settingsDirEntry.GetRef(&contextBaseRef);
    settingsMessage->AddRef(sen::key::kContextPathRef, &contextBaseRef);

    // create initial global context as default
    // Note: we do this manually here since CreateContext() rightfully relies on setup to be completed.
    // set up context directories
    path.Append(sen::config::kContextGlobal);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status == B_OK) {
            // set SEN Context FileType
            BDirectory contextDir(path.Path());
            BNodeInfo contextDirInfo(&contextDir);

            status = contextDirInfo.SetType(sen::mime::kContext);
        }
        if (status != B_OK) {
            spdlog::error("failed to set up global context: {}", strerror(status));
            return status;
        }
    }
    settingsDir.SetTo(path.Path());

    // set up default classification directory in global context setup above
    path.Append(sen::config::kClassificationsDir);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status != B_OK) {
            spdlog::error("failed to set up classification base path: {}", strerror(status));
            return status;
        }
    }

    settingsMessage->AddString(sen::key::kClassificationPath, path.Path());
    entry_ref classBaseRef;
    settingsDirEntry.GetRef(&classBaseRef);
    settingsMessage->AddRef(sen::key::kClassificationPathRef, &classBaseRef);

    return status;
}

status_t SenConfigHandler::SaveSettings(const BMessage* message)
{
    BFile settingsFile(fSettingsDir, "sen.settings", B_CREATE_FILE | B_ERASE_FILE | B_READ_WRITE);

    status_t status = message->Flatten(&settingsFile);
    if (status == B_OK) {
        // set new Haiku Archived Message FileType
        BAppFileInfo fileInfo(&settingsFile);
        status = fileInfo.SetType("application/x-vnd.Haiku-BMessage");
    } else {
        spdlog::error("failed to write default settings to file: {}", strerror(status));
    }

    return status;
}

void SenConfigHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage();
	status_t status = B_OK;

    spdlog::info("in SEN ConfigHandler::MessageReceived");
    message->PrintToStream();

    // for now, we always need these same parameters for context
    // if optional context is empty, use global default context
    const char* context = message->GetString(sen::key::kContext, sen::config::kContextGlobal);
    const BString name = message->GetString(sen::key::kName, "");
    const BString type = message->GetString(sen::key::kType, "");

    switch(message->what)
    {
        case sen::cmd::kClassificationAdd:
            status = AddClassification(context, name, type, reply);
            break;
        case sen::cmd::kClassificationGet:
            status = GetClassification(context, name.String(), type.String(), reply);
            break;
        case sen::cmd::kClassificationFind:
            status = FindClassification(context, &name, &type, reply);
            break;
        default:
            spdlog::info("SenConfigHandler: unknown config message received.");
    }

    reply->AddInt32("result", status);

    spdlog::info("SEN ConfigHandler sending reply:");
    reply->PrintToStream();

	message->SendReply(reply);
}

status_t SenConfigHandler::GetConfig(BMessage* settingsMsg)
{
    if (settingsMsg == NULL || fSettingsMsg->IsEmpty())
        return B_NOT_INITIALIZED;

    return settingsMsg->Append(*fSettingsMsg);
}

status_t SenConfigHandler::FindContextByName(const char* name, BMessage *reply)
{
    entry_ref contextRef;
    status_t status = GetContextDir(name, &contextRef);
    if (status == B_OK)
        reply->AddRef("refs", &contextRef);

    // todo: add context config msg and relations from context file attrs

    return status;
}

status_t SenConfigHandler::AddClassification(const char* context, const char* name, const char* type, BMessage* reply)
{
    // get context path
    entry_ref classDirRef;
    status_t status = GetClassificationDir(context, type, &classDirRef, true);

    BPath classPath(&classDirRef);
    status = classPath.Append(name);

    BFile classFile(classPath.Path(), B_CREATE_FILE);
    if (status == B_OK)
        status = classFile.InitCheck();

    // must not exist already
    if (status != B_OK) {
        spdlog::error("could not create classification entity '{}' of type '{}' in context '{}': {}",
              name, type, context, strerror(status));
    } else {
        BNodeInfo classInfo(&classFile);
        status = classInfo.SetType(type);

        if (status == B_OK) {
            entry_ref classFileRef;
            BEntry classEntry(classPath.Path());

            status = classEntry.GetRef(&classFileRef);
            if (status == B_OK) {
                status = reply->AddRef("refs", &classFileRef);
            }
        }
        if (status != B_OK) {
            spdlog::error("could not set type of new classification '{}' of type '{}' in context '{}': {}",
                name, type, context, strerror(status));
        }
    }
    return status;
}

status_t SenConfigHandler::GetClassification(const char* context, const char* name, const char* type, BMessage* reply)
{
    entry_ref classtRef;
    status_t status = GetClassificationDir(context, type, &classtRef, false);

    BPath classPath(&classtRef);
    classPath.Append(name);

    BFile classFile(classPath.Path(), B_READ_ONLY);
    status = classFile.InitCheck();

    if (status != B_OK) {
        spdlog::error("could not read classification entity '{}' of type '{}' in context '{}': {}",
              name, type, context, strerror(status));
    } else {
        entry_ref classFileRef;
        BEntry classEntry(classPath.Path());

        status = classEntry.GetRef(&classFileRef);
        if (status == B_OK)
            status = reply->AddRef("refs", &classFileRef);
    }
    return status;
}

status_t SenConfigHandler::FindClassification(
    const char* context,
    const BString* name,
    const BString* type,
    BMessage *reply)
{
    entry_ref classtRef;
    status_t status = GetClassificationDir(context, type->String(), &classtRef, false);

    spdlog::info("searching for classification with name {} and type {}...",
        name->IsEmpty() ? "*" : name->String(),
        type->IsEmpty() ? "*" : type->String());

    if (status == B_OK) {
        BDirectory classDir(&classtRef);
        BEntry     classEntry;
        BNode      classNode;
        BNodeInfo  classNodeInfo;
        bool       includeRef;
        entry_ref  classRef;
        char       classType[B_MIME_TYPE_LENGTH];

        if (classDir.InitCheck() == B_OK) {
            // iterate directory and filter by optional params name and type
            while ((status = classDir.GetNextEntry(&classEntry)) == B_OK) {
                // apply optional filters, be optimistic:)
                includeRef = true;

                // filter by optional name
                if (!name->IsEmpty() && *name != classEntry.Name())
                    includeRef = false;

                // filter by optional type
                status = classNode.SetTo(&classEntry);
                if (status == B_OK)
                    status = classNodeInfo.SetTo(&classNode);
                if (status == B_OK)
                    status = classNodeInfo.GetType(classType);
                if (status != B_OK) {
                    spdlog::error("  > skipping entry '{}', error resolving node(info/type): {}.",
                            classEntry.Name(), strerror(status));
                    continue;
                }

                if (!type->IsEmpty() && *type != classType)
                    includeRef = false;

                if (includeRef) {
                    spdlog::info("found matching classification entity {}, addding to list.", classEntry.Name());
                    status = classEntry.GetRef(&classRef);
                    if (status == B_OK) {
                        // add refs and types separately under common names so they can be easier consumed
                        reply->AddString("types", classType);
                        reply->AddRef("refs", &classRef);
                    }
                }
            } // while
            // check for errors besides the obvious B_ENTRY_NOT_FOUND
            if (status != B_ENTRY_NOT_FOUND) {
                spdlog::error("search encountered an error, result may be incomplete.");
            } else {
                status = B_OK;
            }
        }
    }

    if (status != B_OK) {
        reply->AddString("detail", strerror(status));
    }

    return status;
}

//
// helper methods
//

status_t SenConfigHandler::GetContextDir(const char* context, entry_ref* ref)
{
    entry_ref contextRef;
    status_t status = fSettingsMsg->FindRef(sen::key::kContextPathRef, &contextRef);

    BPath contextPath(&contextRef);
    if (status == B_OK && contextPath.InitCheck() == B_OK) {
        status = contextPath.Append(context);
    }
    status = contextPath.InitCheck();
    if (status == B_OK) {
        spdlog::info("found context dir {} for context {}.", contextPath.Path(), context);
        status = BEntry(contextPath.Path()).GetRef(ref);
    } else {
        spdlog::error("failed to get dir for context {}: {}", context, strerror(status));
    }
    return status;
}

status_t SenConfigHandler::GetClassificationDir(const char* context, const char* type, entry_ref* ref, bool create)
{
    entry_ref contextRef;

    status_t status = GetContextDir(context, &contextRef);
    if (status == B_OK) {
        BPath classPathBase(&contextRef);

        if (status == B_OK && classPathBase.InitCheck() == B_OK) {
            classPathBase.Append(sen::config::kClassificationsDir);

            // use MIME type for grouping classifications by type and context
            BMimeType mimeClass(type);
            status = mimeClass.InitCheck();

            if (status == B_OK) {
                // for valid MIME types, check we only get a classification type and then use just the subtype
                BString typeName(type);
                if (! typeName.StartsWith(sen::mime::kClassificationPrefix)) {
                    spdlog::error("unsupported type for classification: {}", type);
                    return B_BAD_VALUE;
                }
                BPath classPath(classPathBase);
                status = classPath.InitCheck();

                if (status == B_OK) {
                    // API does not have BMimeType.Subtype() sadly
                    typeName.RemoveFirst(sen::mime::kClassificationPrefix);
                    classPath.Append(typeName.String());

                    status = classPath.InitCheck();
                    if (status == B_OK) {
                        spdlog::info("found classifications dir '{}' for context '{}' and type '{}'.",
                            classPath.Path(), context, type);

                        BEntry classEntry(classPath.Path());

                        if (create && ! classEntry.Exists()) {
                            spdlog::info("creating new classification directory '{}'.", classPath.Path());

                            BDirectory classDir(classPathBase.Path());
                            status = classDir.CreateDirectory(classPath.Leaf(), NULL);

                            if (status == B_OK) {
                                // write a friendly name for use in UI's like Tracker;
                                // real name is the full and unique context MIME type.
                                classDir.SetTo(classPath.Path());

                                char shortName[B_MIME_TYPE_LENGTH];
                                BString folderName;

                                status = mimeClass.GetShortDescription(shortName);
                                if (status == B_OK) {
                                    folderName = shortName;
                                    folderName.Append("s"); // quick hack, todo: move to MIME Type config
                                } else {
                                    spdlog::error("failed to get short description for type {}, falling back to type name: {}.",
                                          typeName.String(), strerror(status));
                                    folderName = typeName;
                                }

                                status = classDir.WriteAttrString(sen::attr::kFolderName, &folderName);
                            }
                        }

                        if (status == B_OK) {
                            status = classEntry.GetRef(ref);
                        }
                    }
                }
            }
        }
    }
    if (status != B_OK) {
        spdlog::error("failed to get dir for classification with context '{}' and type '{}': {}",
                context, type, strerror(status));
    }
    return status;

}

status_t SenConfigHandler::CreateContext(const char* name, entry_ref* ref)
{
    entry_ref contextRef;
    status_t status = GetContextDir(name, &contextRef);

    BFile contextFile(&contextRef, B_CREATE_FILE);
    status = contextFile.InitCheck();

    // esp. must not exist already
    if (status != B_OK) {
        spdlog::error("could not create directory for context '{}': {}",
              name, strerror(status));
        return status;
    }

    BNodeInfo contextInfo(&contextFile);
    status = contextInfo.SetType(sen::mime::kContext);

    // optionally return the ref to the newly created context
    if (status == B_OK && ref != NULL) {
        *ref = contextRef;
    }
    return status;
}
