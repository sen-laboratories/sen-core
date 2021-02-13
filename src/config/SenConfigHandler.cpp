/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "SenConfigHandler.h"
#include "../Sen.h"

#include <Directory.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <Message.h>
#include <Path.h>

SenConfigHandler::SenConfigHandler()
    : BHandler("SenConfigHandler")
{
    BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
    {
        ERROR("could not find user settings directory, falling back to working directory.");
		path.SetTo("./config");
    }
	settingsDir = new BDirectory(path.Path());
	path.Append("sen");
	if (!settingsDir->Contains(path.Path()))
		settingsDir->CreateDirectory(path.Path(), NULL);

	settingsDir->SetTo(path.Path());
    
    LOG("SEN Config: using settings directory %s\n", path.Path());    
}

SenConfigHandler::~SenConfigHandler()
{
}

void SenConfigHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage();
	status_t result = B_OK;

    LOG("in SEN ConfigHandler::MessageReceived\n");
    
    switch(message->what)
    {
        case SEN_CORE_INIT:
        {
            bool flagClean = message->FindBool("clean", false);
            if (flagClean)
            {
                LOG("cleaning old configuration" B_UTF8_ELLIPSIS);
            }
            
            LOG("initialising SEN configuration" B_UTF8_ELLIPSIS);
            
            result = InitConfig();
            if (result == B_OK)
            {
                LOG("SEN configuration initialised successfully.");
            }
            else
            {
                ERROR("failed to initialise configuration, please inspect log and retry: %d.\n", result);
            }
            break;
        }
        default:
        {
            LOG("SenConfigHandler: unknown message received, handing over to parent: %lu\n", message->what);
            BHandler::MessageReceived(message);
            return;
        }
    }
    LOG("SEN ConfigHandler sending reply %d", result);
    reply->AddInt32("result", result);
	message->SendReply(reply);
}

status_t SenConfigHandler::InitConfig(bool clean)
{
    status_t result = InitRelationType(clean);
    if (result == B_OK)
        result = InitRelations(clean);
        if (result == B_OK)
            result = InitIndices(clean);
    
    return result;
}

status_t SenConfigHandler::InitIndices(bool clean)
{
    // see https://github.com/grexe/haiku/blob/master/src/bin/lsindex.cpp
    return B_OK;
}

status_t SenConfigHandler::InitRelationType(bool clean)
{
    LOG("setting up SEN Relation Type" B_UTF8_ELLIPSIS);
    
    BMimeType mime(SEN_CONFIG_RELATION_TYPE_NAME);
    if (clean && mime.IsInstalled()) {
        mime.Delete();
    }
    status_t result = mime.Install();
    if (result != B_OK) {
        return result;
    }
    mime.SetShortDescription("SEN Relation Definition");
    mime.SetLongDescription("Configures a relation for the SEN framework");
    mime.SetPreferredApp(SEN_SERVER_SIGNATURE);
    
    BMessage attrMsg;

    //TODO: move to method
    
    // active flag
    attrMsg.AddString("attr:public_name", "Enabled");
    attrMsg.AddString("attr:name", "META:enabled");
    attrMsg.AddInt32("attr:type", B_BOOL_TYPE);
    
    attrMsg.AddBool("attr:viewable", true);
    attrMsg.AddBool("attr:editable", true);
    attrMsg.AddInt32("attr:width", 78);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    // abstract or concrete instance flag -> leaf of relation hierarchy with targets
    attrMsg.AddString("attr:public_name", "Abstract");
    attrMsg.AddString("attr:name", "META:abstract");
    attrMsg.AddInt32("attr:type", B_BOOL_TYPE);
    
    attrMsg.AddBool("attr:viewable", true);
    attrMsg.AddBool("attr:editable", true);
    attrMsg.AddInt32("attr:width", 86);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    // internal meta-relation to parent relation
    attrMsg.AddString("attr:public_name", "Parent of");
    attrMsg.AddString("attr:name", "META:parentOf");
    attrMsg.AddInt32("attr:type", B_STRING_TYPE);

    // internal meta-relation to parent relation
    attrMsg.AddString("attr:public_name", "Child of");
    attrMsg.AddString("attr:name", "META:childOf");
    attrMsg.AddInt32("attr:type", B_STRING_TYPE);
    
    attrMsg.AddBool("attr:viewable", false);
    attrMsg.AddBool("attr:editable", false);
    attrMsg.AddInt32("attr:width", 0);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    // internal meta-relation to inverse relation
    attrMsg.AddString("attr:public_name", "Inverse of");
    attrMsg.AddString("attr:name", "META:inverseOf");
    attrMsg.AddInt32("attr:type", B_STRING_TYPE);
    
    attrMsg.AddBool("attr:viewable", false);
    attrMsg.AddBool("attr:editable", false);
    attrMsg.AddInt32("attr:width", 0);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    // relation formula for resolving targets
    attrMsg.AddString("attr:public_name", "Formula");
    attrMsg.AddString("attr:name", "META:formula");
    attrMsg.AddInt32("attr:type", B_STRING_TYPE);
    
    attrMsg.AddBool("attr:viewable", true);
    attrMsg.AddBool("attr:editable", true);
    attrMsg.AddInt32("attr:width", 160);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    // configuration (as BMessage) for valid sources, etc.)
    attrMsg.AddString("attr:public_name", "Configuration");
    attrMsg.AddString("attr:name", "META:config");
    attrMsg.AddInt32("attr:type", B_MESSAGE_TYPE);
    
    attrMsg.AddBool("attr:viewable", false);
    attrMsg.AddBool("attr:editable", false);
    attrMsg.AddInt32("attr:width", 120);
    attrMsg.AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg.AddBool("attr:extra", false);

    mime.SetAttrInfo(&attrMsg);
    
    return mime.InitCheck();
}

status_t SenConfigHandler::InitRelations(bool clean)
{
    BDirectory relationsDir;
    status_t relationsDirStatus = settingsDir->CreateDirectory("relations", &relationsDir);
    if (relationsDirStatus != B_OK) {
        if (relationsDirStatus == B_FILE_EXISTS) {
            if (!clean) {
                ERROR("configuration already exists, skipping - override with `clean=true`.");
                return relationsDirStatus;
            } else {
                BEntry entry;
                while (relationsDir.GetNextEntry(&entry) == B_OK) {
                    if (entry.Remove() != B_OK) {
                        const char* name = new char[B_FILE_NAME_LENGTH];
                        ERROR("failed to remove relation %s\n", name);
                    }
                }
            }
        } else {
            ERROR("failed to access relations config: %ld", relationsDirStatus);
        }
    }
    //
    // create relations
    //
    BMessage* configMsg = new BMessage(SEN_CONFIG_MSG);
    bool configOk;
    
    // same (base relation)
    // configOk = B_OK && CreateRelation("same", clean, true, true, "same", NULL, NULL, configMsg);  // empty config for now here
    
    // same image size -> LATER
    /*
    configMsg->MakeEmpty();
    LOG("msg type: %ld", configMsg->what);
    configMsg->AddString(SEN_CONFIG_MSG_DISPLAY_NAME, "image size");
    configMsg.AddString(SEN_CONFIG_MSG_SOURCE_TYPES, "image");
    configMsg.AddString(SEN_CONFIG_MSG_TARGET_TYPES, "image");
    relationStatus = CreateRelation("sameImageSize", clean, true, true, "sameImageSize", "", "", configMsg);
    */
    
    // TODO: add placeholder for querying all indexed attributes of SOURCE so we don't need a relation for every possible attribute 
    // NEEDS: Index API -> Haiku git code
    
    // LATER: similar
    // NEEDS: formulae
    // relationStatus = CreateRelation("similar", clean, true, true, "similar", "", "", new BMessage(SEN_CONFIG_MSG));
    
    // parentOf/childOf
    configMsg->MakeEmpty();
    LOG("empty msg type: %ld", configMsg->what);
    configMsg.AddString(SEN_CONFIG_MSG_SOURCE_TYPES, "*/*");
    configMsg.AddString(SEN_CONFIG_MSG_TARGET_TYPES, "*/*");
    configOk = B_OK && CreateRelation("parentOf", "parent of", clean, true, false, "childOf", NULL, NULL, configMsg);  // empty config for now here
    
    // includes
    
    BMessage* configMsg = new BMessage(SEN_CONFIG_MSG);
    configMsg->AddAll
    configMsg.AddString(SEN_CONFIG_MSG_SOURCE_TYPES, "*/*");
    configMsg.AddString(SEN_CONFIG_MSG_TARGET_TYPES, "*/*");
    
    return B_OK;
}

status_t SenConfigHandler::CreateRelation(const char* name, const char* displayName, bool clean, bool enabled, bool abstract,
                                          const char* inverseOf, const char* childOf, const char* formula,
                                          const BMessage* config) {
    BFile relation;
    status_t relationStatus = relationsDir.CreateFile(name, &relation, !clean);
    if (relationStatus != B_OK) {
        ERROR("failed to create relation '%s': %ld\n", name, relationStatus);
        // bail out, configuration must be all valid
        return relationStatus;
    }
    // set type
    relation.WriteAttrString("BEOS:TYPE", new BString(SEN_CONFIG_RELATION_TYPE_NAME));
    
    // set attributes
    relation.WriteAttr("META:displayName", new BString(displayName));
    relation.WriteAttr("META:enabled", B_BOOL_TYPE, 0, enabled ? "true" : "false", 1);
    relation.WriteAttr("META:abstract", B_BOOL_TYPE, 0, abstract ? "true" : "false", 1);
    relation.WriteAttrString("META:inverseOf", new BString(inverseOf));
    relation.WriteAttrString("META:childOf", new BString(childOf));
    
    // set configuration
    relation.WriteAttr("META:config", configMsg);
}
