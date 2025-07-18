/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "SenConfigHandler.h"
#include "Sen.h"

#include <AppFileInfo.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Resources.h>

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
        ERROR("failed to read settings: %s\n", strerror(status));
    } else {
        LOG("got settings:\n");
        fSettingsMsg->PrintToStream();
    }

    return status;
}

status_t SenConfigHandler::LoadSettings(BMessage* message)
{
    BPath path;
    status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK) {
        ERROR("could not find user settings directory: %s\n", strerror(status));
		return status;
    }

    path.Append("sen");
    // set global settings directory here
    fSettingsDir->SetTo(path.Path());

    // read or create and init settings file
    BEntry settingsDirEntry(path.Path());
    BEntry settingsFileEntry(fSettingsDir, "sen.settings");

    if (! settingsDirEntry.Exists() || ! settingsFileEntry.Exists()) {
        status = InitDefaultSettings(&path, fSettingsMsg);
    } else {
        BFile settingsFile(&settingsFileEntry, B_READ_WRITE);
        status = settingsFile.InitCheck();
        if (status == B_OK) {
            status = fSettingsMsg->Unflatten(&settingsFile);
        }
        if (status != B_OK) {
            ERROR("could not unflatten settings message: %s\n", strerror(status));
        }
    }

    return status;
}

status_t SenConfigHandler::InitDefaultSettings(BPath* settingsPath, BMessage* message)
{
    status_t status;

    LOG("setting up default settings in %s" B_UTF8_ELLIPSIS "\n", settingsPath->Path());

    // checking and building sen settings directories incrementally
    BEntry settingsDirEntry(settingsPath->Path());
    BPath path;     // working path for setting up directories
    settingsPath->GetParent(&path); // user settings home

    BDirectory settingsDir(path.Path());
    path.Append("sen");

	if (! settingsDirEntry.Exists())
		status = settingsDir.CreateDirectory(path.Leaf(), NULL);

	if (status != B_OK) {
        ERROR("could not access settings path '%s': %s\n", path.Path(), strerror(status));
        return status;
    }

    message->AddString(SEN_CONFIG_PATH, path.Path());
    settingsDir.SetTo(path.Path());

    // set up context directories
    path.Append(SEN_CONFIG_CONTEXT_PATH_NAME);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status != B_OK) {
            ERROR("failed to set up context base path: %s\n", strerror(status));
            return status;
        }
    }
    settingsDir.SetTo(path.Path());
    message->AddString(SEN_CONFIG_CONTEXT_BASE_PATH, path.Path());

    entry_ref contextBaseRef;
    settingsDirEntry.GetRef(&contextBaseRef);
    message->AddRef(SEN_CONFIG_CONTEXT_BASE_PATH_REF, &contextBaseRef);

    // create initial global context as default
    // Note: we do this manually here since CreateContext() rightfully relies on setup to be completed.
    // set up context directories
    path.Append(SEN_CONFIG_CONTEXT_GLOBAL);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status == B_OK) {
            // set SEN Context FileType
            BDirectory contextDir(path.Path());
            BNodeInfo contextDirInfo(&contextDir);

            status = contextDirInfo.SetType(SEN_CONTEXT_TYPE);
        }
        if (status != B_OK) {
            ERROR("failed to set up global context: %s\n", strerror(status));
            return status;
        }
    }
    settingsDir.SetTo(path.Path());

    // set up default classification directory in global context setup above
    path.Append(SEN_CONFIG_CLASS_PATH_NAME);
    settingsDirEntry.SetTo(path.Path());

    if (! settingsDirEntry.Exists()) {
        status = settingsDir.CreateDirectory(path.Leaf(), NULL);
        if (status != B_OK) {
            ERROR("failed to set up classification base path: %s\n", strerror(status));
            return status;
        }
    }

    message->AddString(SEN_CONFIG_CLASS_BASE_PATH, path.Path());
    entry_ref classBaseRef;
    settingsDirEntry.GetRef(&classBaseRef);
    message->AddRef(SEN_CONFIG_CLASS_BASE_PATH_REF, &classBaseRef);

    return status;
}

status_t SenConfigHandler::SaveSettings(const BMessage* message)
{
    BFile settingsFile(fSettingsDir, "sen.settings", B_READ_WRITE);

    status_t status = message->Flatten(&settingsFile);
    if (status == B_OK) {
        // set new Haiku Archived Message FileType
        BAppFileInfo fileInfo(&settingsFile);
        status = fileInfo.SetType("application/x-vnd.Haiku-BMessage");
    } else {
        ERROR("failed to write default settings to file: %s\n", strerror(status));
    }

    return status;
}

void SenConfigHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage();
	status_t status = B_OK;

    LOG("in SEN ConfigHandler::MessageReceived\n");
    message->PrintToStream();

    // for now, we always need these same parameters for context
    // if optional context is empty, use global default context
    const char* context = message->GetString(SEN_MSG_CONTEXT, SEN_CONFIG_CONTEXT_GLOBAL);
    const char* name = message->GetString(SEN_MSG_NAME, "");
    const char* type = message->GetString(SEN_MSG_TYPE, "");

    switch(message->what)
    {
        case SEN_CONFIG_CLASS_ADD:
            status = AddClassification(context, name, type, reply);
            break;
        case SEN_CONFIG_CLASS_GET:
            status = GetClassification(context, name, type, reply);
            break;
        default:
            LOG("SenConfigHandler: unknown config message received.\n");
    }

    reply->AddInt32("result", status);

    LOG("SEN ConfigHandler sending reply:\n");
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
        ERROR("could not create classification entity '%s' of type '%s' in context '%s': %s\n",
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
            ERROR("could not set type of new classification '%s' of type '%s' in context '%s': %s\n",
                name, type, context, strerror(status));
        }
    }
    return status;
}

status_t SenConfigHandler::GetClassification(const char* context, const char* name, const char* type, BMessage* reply)
{
    // get context path
    entry_ref contextRef;
    status_t status = GetContextDir(context, &contextRef);

    BPath classPath(&contextRef);
    classPath.Append(name);

    BFile classFile(classPath.Path(), B_READ_ONLY);
    status = classFile.InitCheck();

    if (status != B_OK) {
        ERROR("could not read classification entity '%s' of type '%s' in context '%s': %s",
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

//
// helper methods
//

status_t SenConfigHandler::GetContextDir(const char* context, entry_ref* ref)
{
    entry_ref contextRef;
    status_t status = fSettingsMsg->FindRef(SEN_CONFIG_CONTEXT_BASE_PATH_REF, &contextRef);

    BPath contextPath(&contextRef);
    if (status == B_OK && contextPath.InitCheck() == B_OK) {
        status = contextPath.Append(context);
    }
    status = contextPath.InitCheck();
    if (status == B_OK) {
        LOG("found context dir %s for context %s.\n", contextPath.Path(), context);
        status = BEntry(contextPath.Path()).GetRef(ref);
    } else {
        ERROR("failed to get dir for context %s: %s\n", context, strerror(status));
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
            classPathBase.Append(SEN_CONFIG_CLASS_PATH_NAME);

            // use MIME type for grouping classifications by type and context
            BMimeType mimeClass(type);
            status = mimeClass.InitCheck();

            if (status == B_OK) {
                // for valid MIME types, check we only get a meta type for classification and then use just the subtype
                BString typeName(type);
                if (! typeName.StartsWith(SEN_META_SUPERTYPE "/")) {
                    ERROR("unsupported type for classification: %s\n", type);
                    return B_BAD_VALUE;
                }
                BPath classPath(classPathBase);
                status = classPath.InitCheck();

                if (status == B_OK) {
                    // API does not have BMimeType.Subtype() sadly
                    typeName.RemoveFirst(SEN_META_SUPERTYPE "/");
                    classPath.Append(typeName.String());

                    status = classPath.InitCheck();
                    if (status == B_OK) {
                        LOG("found classifications dir '%s' for context '%s' and type '%s'.\n",
                            classPath.Path(), context, type);

                        BEntry classEntry(classPath.Path());

                        if (create && ! classEntry.Exists()) {
                            LOG("creating new classification directory '%s'.\n", classPath.Path());

                            BDirectory classDir(classPathBase.Path());
                            status = classDir.CreateDirectory(classPath.Leaf(), NULL);
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
        ERROR("failed to get dir for classification with context '%s' and type '%s': %s\n",
                context, type, strerror(status));
    }
    return status;

}

status_t SenConfigHandler::CreateContext(const char* name, entry_ref* ref)
{
    entry_ref contextRef;
    status_t status = GetContextDir(name, &contextRef);

    BFile classFile(&contextRef, B_CREATE_FILE);
    status = classFile.InitCheck();

    // esp. must not exist already
    if (status != B_OK) {
        ERROR("could not create context '%s' with type '%s': %s",
              name, strerror(status));
        return status;
    }

    BNodeInfo contextInfo(&classFile);
    status = contextInfo.SetType(SEN_CONTEXT_TYPE);
    // optionally return the ref to the newly created context
    if (status == B_OK && ref != NULL) {
        *ref = contextRef;
    }
    return status;
}
