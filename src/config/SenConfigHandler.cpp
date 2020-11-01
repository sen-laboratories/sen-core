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
    
    LOG("\nSEN Config: using settings directory %s\n", path.Path());    
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
    // write relations
    //TODO: setup builtin config map and create relations from there!
    BFile relation;
    status_t relationStatus = relationsDir.CreateFile("same", &relation, !clean);
    if (relationStatus == B_OK) {
        // set type
        relation.WriteAttrString("BEOS:TYPE", new BString(SEN_CONFIG_RELATION_TYPE_NAME));
        // set attributes
        relation.WriteAttr("META:enabled", B_BOOL_TYPE, 0, "true", 1);
        relation.WriteAttr("META:abstract", B_BOOL_TYPE, 0, "true", 1);
        relation.WriteAttrString("META:inverseOf", new BString("same"));
        // set configuration
        //TODO
    } else {
        ERROR("failed to create relation: %ld\n", relationStatus);
        // bail out, configuration must be all valid
        return relationStatus;
    }
    return B_OK;
}
