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
#include <NodeInfo.h>
#include <Path.h>

// TODO: separate general config from relations config into own class, use mkdir -p style func
SenConfigHandler::SenConfigHandler()
    : BHandler("SenConfigHandler")
{
    BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
    {
        ERROR("could not find user settings directory, falling back to working directory.\n");
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
            bool flagClean = true; //message->FindBool("clean", false);
            if (flagClean)
            {
                LOG("cleaning old configuration" B_UTF8_ELLIPSIS "\n");
            }
            
            LOG("initialising SEN configuration" B_UTF8_ELLIPSIS "\n");
            
            result = InitConfig(flagClean);
            if (result == B_OK)
            {
                LOG("SEN configuration initialised successfully.\n");
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
    LOG("SEN ConfigHandler sending reply %d\n", result);
    reply->AddInt32("result", result);
	message->SendReply(reply);
}

status_t SenConfigHandler::InitConfig(bool clean)
{
    status_t result = InitRelationType(clean);
    if (result == B_OK) {
        result = InitRelations(clean);
        if (result == B_OK) {
            result = InitIndices(clean);
            if (result != B_OK) {
                ERROR("failed to setup indices: %d\n", result);
            }
        } else {
            ERROR("There were errors in creating relation config, please check the logs.\n");
        }
    } else {
        ERROR("failed to setup relation file type.");
    }
    
    return result;
}

status_t SenConfigHandler::InitIndices(bool clean)
{
    // see https://github.com/grexe/haiku/blob/master/src/bin/lsindex.cpp
    return B_OK;
}

status_t SenConfigHandler::InitRelationType(bool clean)
{
    LOG("setting up SEN Relation Type '" SEN_CONFIG_RELATION_TYPE_NAME "'" B_UTF8_ELLIPSIS "\n");
    
    BMimeType mime(SEN_CONFIG_RELATION_TYPE_NAME);
    
    if (status_t status = mime.InitCheck() != B_OK) {
        LOG("error setting MIME Type!\n");
        return status;
    }

    if (clean && mime.IsInstalled()) {
        LOG("replacing old MIME type" B_UTF8_ELLIPSIS "\n");
        if (status_t status = mime.Delete() != B_OK) {
            LOG("failed to delete MIME type " SEN_CONFIG_RELATION_TYPE_NAME "\n");
            return status;
        }
    }
    
    BMessage* attrMsg = new BMessage();
    
    // whether relation is active (e.g. shown in Tracker, evaluated at runtime, etc.)
    AddMimeInfo(attrMsg, "enabled", "Enabled", B_BOOL_TYPE, true, false);

    // relation description, may be used for display purposes (think Tracker Relations menu)
    AddMimeInfo(attrMsg, "displayName", "Display name", B_STRING_TYPE, true, true);
        
    // specialization of another relation (e.g. restricting MIME types for source/target)
    AddMimeInfo(attrMsg, "childOf", "Child of", B_STRING_TYPE, true, false);

    // internal meta-relation to inverse relation
    AddMimeInfo(attrMsg, "inverseOf", "Inverse of", B_STRING_TYPE, true, false);

    // used for relation configuration like applicable MIME types
    AddMimeInfo(attrMsg, "config", "Configuration", B_MESSAGE_TYPE, false, false);

    // supported relation properties
    AddMimeInfo(attrMsg, "properties", "Properties", B_MESSAGE_TYPE, false, false);
    
    mime.SetShortDescription("SEN Relation Definition");
    mime.SetLongDescription("Configures a relation for the SEN framework");
    mime.SetPreferredApp(SEN_SERVER_SIGNATURE);
    mime.SetAttrInfo(attrMsg);

    status_t result = mime.Install();
    // buggy? always returns an error
    if (result != B_OK) {
        LOG("(ignoring) internal error setting up MIME Type: %ld", result);
     //   return result;
        result = B_OK;
    }

    LOG("successfully created Relation FileType " SEN_CONFIG_RELATION_TYPE_NAME "\n");
    
    return result;
}

void SenConfigHandler::AddMimeInfo(BMessage* attrMsg, const char* name, const char* displayName, int32 type, bool viewable, bool editable)
{
    attrMsg->AddString("attr:public_name", displayName);
    attrMsg->AddString("attr:name", name);
    attrMsg->AddInt32("attr:type", type);
    
    attrMsg->AddBool("attr:viewable", viewable);
    attrMsg->AddBool("attr:editable", editable);
    attrMsg->AddInt32("attr:width", (type == B_BOOL_TYPE ? 76 : 128));
    attrMsg->AddInt32("attr:alignment", B_ALIGN_LEFT);
    attrMsg->AddBool("attr:extra", false);
}

status_t SenConfigHandler::InitRelations(bool clean)
{
    relationsDir = new BDirectory();
    status_t relationsDirStatus = settingsDir->CreateDirectory("relations", relationsDir);
    
    if (relationsDirStatus != B_OK) {
        if (relationsDirStatus == B_FILE_EXISTS) {
            if (!clean) {
                ERROR("configuration already exists, skipping - override with `clean=true`.");
                return relationsDirStatus;
            } else {
                LOG("removing old relations" B_UTF8_ELLIPSIS "\n");
                
                BEntry entry;
                relationsDir->SetTo(settingsDir, "relations");
                while (relationsDir->GetNextEntry(&entry) == B_OK) {
                    if (entry.Remove() != B_OK) {
                        char* name = new char[B_FILE_NAME_LENGTH];
                        entry.GetName(name);
                        ERROR("failed to remove relation %s\n", name);
                        delete name;
                    }
                }
            }
        } else {
            ERROR("failed to access relations config dir: %ld", relationsDirStatus);
            return relationsDirStatus;
        }
    }

    BPath path(relationsDir);
    if (status_t status = path.InitCheck() != B_OK) {
        ERROR("failed to access relations config dir: %ld\n", status);
    }
    LOG("setting up relations in %s\n", path.Path());
    
    /*
     * create relation configuration
     * properties are inherited but can be erased with an empty property
     */
    BMessage* configMsg = new BMessage(SEN_CONFIG_RELATION_MSG);
    BMessage* propsMsg = new BMessage(SEN_CONFIG_RELATION_PROPERTIES_MSG);
    
    status_t configOk = B_OK;
        
    /*
     * generic relations
     */
    
    // root relationship, most generic type
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "*/*");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "*/*");
    
    InitPropertiesMsg(propsMsg);
    
    // idiosynchratic relationship
    configOk = CreateRelation("relatedTo", "related to", "relatedTo", NULL,
                              "most generic relationship to be used when no specific relation is applicable.",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
    
    // hierarchical top->down
    configOk = CreateRelation("parentOf", "parent of", "childOf", NULL,
                              "hierarchical relationship, superordinate side",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // hierarchical bottom->up
    configOk = CreateRelation("childOf", "child of", "parentOf", NULL,
                              "hierarchical relationship, subordinate side",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // similarity
    configOk = CreateRelation("similarTo", "similar to", "similarTo", NULL,
                              "similarity relation, reflexive.",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    /*
     * relations with additional location property.
     * child relations may define more specific properties and reference them here (e.g. @page )
     */
    propsMsg->AddString("location", "xref (page, offset), WebAnnotation, anchor," B_UTF8_ELLIPSIS);
    
    // reference
    configOk = CreateRelation("references", "references", "referencedBy", NULL,
                              "general reference to anther entity with optional location specifier (book page, movie location, web anchor).",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    configOk = CreateRelation("referencedBy", "referenced by", "references", NULL,
                              "general back-reference to anther entity with optional location specifier (book page, line, movie location, web anchor).",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
    
    // generic contains
    configOk = CreateRelation("contains", "contains", "containedBy", NULL, "generic part/whole relationship, parent end",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // generic containedBy
    configOk = CreateRelation("containedBy", "contained by", "contains", NULL, "generic part/whole relationship, child end", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
    
    // specialization of "contains" relation for geographic locations
    configMsg->MakeEmpty();
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "application/gml+xml");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "application/vnd.google-earth.kml+xml");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "application/vnd.google-earth.kmz");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "*/*");
    
    configOk = CreateRelation("placeOf", "place of", "locatedIn", "contains", "locational relation that is home of a target entity",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // specialization of "containedBy" relation for geographic locations
    configMsg->MakeEmpty();
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "*/*");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "application/gml+xml");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "application/vnd.google-earth.kml+xml");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "application/vnd.google-earth.kmz");
    
    configOk = CreateRelation("locatedIn", "located in", "placeOf", "containedBy", "locational relation pointing to the source location",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // specialization of "contains" relation for source includes
    configMsg->MakeEmpty();
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "text/x-source-code");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "text/x-source-code");
    
    configOk = CreateRelation("includes", "includes", "includedBy", "contains", "source code include relation",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
    
    // specialization of "contains" relation for source includes
    configMsg->MakeEmpty();
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "text/x-source-code");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "text/x-source-code");
    
    configOk = CreateRelation("includes", "includes", "includedBy", "contains", "source code include relation",
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // source includedBy
    propsMsg->AddString("line", "line number");     // location in referencing source code file
    configOk = CreateRelation("includedBy", "included by", "includes", "containedBy", "source code including this target", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // textual annotations
    configMsg->MakeEmpty();
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "text/*");     // annotations must be some kind of text
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "*/*");

    propsMsg->AddString("page", "page number");

    configOk = CreateRelation("annotates", "annotates", "annotatedBy", "references", "annotation for another entity", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
    
    // annotatedBy
    configMsg->MakeEmpty();
    InitPropertiesMsg(propsMsg);
    
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "*/*");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "text/*");     // inverse relation has it the other way around

    configOk = CreateRelation("annotatedBy", "annotated by", "annotates", "referencedBy", "points to an annotation for the source", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }
   
    // temporal relations
    configMsg->MakeEmpty();    
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "*/*");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "text/calendar");

    configOk = CreateRelation("existsAt", "exists at", "timeOf", NULL, "temporal relation that relates an entity to a period of or point in time", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    configMsg->MakeEmpty();    
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_SOURCE_TYPES, "text/calendar");
    configMsg->AddString(SEN_CONFIG_RELATION_MSG_TARGET_TYPES, "*/*");

    configOk = CreateRelation("timeOf", "time of", "existsAt", NULL, "temporal relation that relates a period of or point in time to a thing", 
                              clean, true, configMsg, propsMsg);
    if (configOk != B_OK) {
        return configOk;
    }

    // done, clean up
    delete configMsg;
    delete propsMsg;
    
    return B_OK;
}

status_t SenConfigHandler::CreateRelation(const char* name, const char* displayName, const char* inverseOf, const char* childOf, const char* description,
                                          bool clean, bool enabled, const BMessage* config, const BMessage* configProps) {
    
    DEBUG("creating relation %s in %s\n", name, BPath(relationsDir).Path());

    BFile relation;
    status_t relationStatus = relationsDir->CreateFile(name, &relation, !clean);
    if (relationStatus != B_OK) {
        ERROR("failed to create relation '%s': %ld\n", name, relationStatus);
        // bail out, configuration must be all valid
        return relationStatus;
    }
   
    // set type
    //relation.WriteAttrString("BEOS:TYPE", new BString(SEN_CONFIG_RELATION_TYPE_NAME));
    BNodeInfo relationNodeInfo(&relation);
    relationNodeInfo.SetType(SEN_CONFIG_RELATION_TYPE_NAME);
    
    // set attributes
    relation.WriteAttrString(SEN_ATTRIBUTES_PREFIX "displayName", new BString(displayName));
    relation.WriteAttrString(SEN_ATTRIBUTES_PREFIX "description", new BString(description));
    relation.WriteAttr(SEN_ATTRIBUTES_PREFIX "enabled", B_BOOL_TYPE, 0, &enabled, 1);
    
    if (inverseOf != NULL) {
        relation.WriteAttrString(SEN_ATTRIBUTES_PREFIX "inverseOf", new BString(inverseOf));
    }
    if (childOf != NULL) {
        relation.WriteAttrString(SEN_ATTRIBUTES_PREFIX "childOf", new BString(childOf));
    }
    
    // set configuration and properties message
    ssize_t msgSize = config->FlattenedSize();
    char* msgBuffer = new char[msgSize];
    config->Flatten(msgBuffer, msgSize);
    relation.WriteAttr(SEN_ATTRIBUTES_PREFIX "config", B_MESSAGE_TYPE, 0, msgBuffer, msgSize);
    delete msgBuffer;
    
    msgSize = configProps->FlattenedSize();
    msgBuffer = new char[msgSize];
    configProps->Flatten(msgBuffer, msgSize);
    relation.WriteAttr(SEN_ATTRIBUTES_PREFIX "properties", B_MESSAGE_TYPE, 0, msgBuffer, msgSize);
    
    delete msgBuffer;
    
    if (status_t status = relation.Sync() != B_OK) {
        ERROR("error creating relation file!");
        return status;
    } else {
        LOG("created relation %s.\n", name);
        return B_OK;
    }
}

void SenConfigHandler::InitPropertiesMsg(BMessage *propsMsg) {
    propsMsg->MakeEmpty();
    propsMsg->AddString("role", "depending on related entities, may be similar, same as, family relation," B_UTF8_ELLIPSIS);
    propsMsg->AddString("label", "descriptive label for annotating the relation.");
}
