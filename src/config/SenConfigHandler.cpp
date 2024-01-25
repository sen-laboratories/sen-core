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
            bool flagClean = message->FindBool("clean", false);
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
            LOG("SenConfigHandler: unknown message received, handing over to parent: %u\n", message->what);
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
    status_t result = InitRelationTypes(clean);
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

status_t SenConfigHandler::InitRelationTypes(bool clean)
{
    LOG("setting up SEN Relation Types" B_UTF8_ELLIPSIS "\n");
    
    BMimeType mime(SEN_CONFIG_RELATION_SUPERTYPE_NAME);
    
    if (status_t status = mime.InitCheck() != B_OK) {
        LOG("error setting MIME Type!\n");
        return status;
    }

    if (mime.IsInstalled()) {
		// test
		BMessage attrMsg;
		mime.GetAttrInfo(&attrMsg);
		LOG("SEN RelationType version is %s\n", attrMsg.GetString("SEN:version"));
		// test_end

		if (clean) {
			LOG("replacing old MIME type" B_UTF8_ELLIPSIS "\n");
			if (status_t status = mime.Delete() != B_OK) {
				LOG("failed to delete MIME type " SEN_CONFIG_RELATION_SUPERTYPE_NAME "\n");
				return status;
			}
        }
    }

    if (status_t result = mime.Install() != B_OK) {
		LOG("failed to install MIME type " SEN_CONFIG_RELATION_SUPERTYPE_NAME " %u\n", result);
        return result;
    }
    
    BMessage* attrMsg = new BMessage();
	
	// *** SEN core properties
	
	// easy way to disable/enable relations, may be context related
    AddMimeInfo(attrMsg, "SEN:enabled", "Enabled?", B_BOOL_TYPE, true, true);
	
    // plural term to be used for relation targets, used in Tracker Relations view
    AddMimeInfo(attrMsg, "SEN:pluralName", "Plural form", B_STRING_TYPE, true, true);
        
    // internal meta-relation to inverse relation, referenced by internal type name
    AddMimeInfo(attrMsg, "SEN:inverseOf", "Inverse of", B_STRING_TYPE, true, false);

    // internal properties used for relation configuration
    AddMimeInfo(attrMsg, "SEN:targetsIncluded", "Applicable targets", B_STRING_TYPE, false, false);
    AddMimeInfo(attrMsg, "SEN:targetsExcluded", "Excluded targets", B_STRING_TYPE, false, false);
    AddMimeInfo(attrMsg, "SEN:supportsSelfRef", "Supports self-referencing?", B_BOOL_TYPE, false, false);

	// version of this relation type def, for later upgrade/migration possibility
    AddMimeInfo(attrMsg, "SEN:version", "Version", B_STRING_TYPE, false, false);
	// whether this relation was generated by SEN or a supporting app, false = manual (user)
    AddMimeInfo(attrMsg, "SEN:generated", "generated?", B_BOOL_TYPE, false, false);
	// UNIX timestamp when the relation was generated, if at all, used for caching/updating the relation
	AddMimeInfo(attrMsg, "SEN:generatedTime", "generated at", B_TIME_TYPE, false, false);
	// MIME type signature of the app that generated the relation
    AddMimeInfo(attrMsg, "SEN:provenance", "Provenance", B_STRING_TYPE, false, false);
	// whether this relation is to be updated upon resolving
    AddMimeInfo(attrMsg, "SEN:dynamic", "dynamic?", B_BOOL_TYPE, false, false);
	// used for n-ary relations: points to SEN:id of the target, holds [id] in src
    AddMimeInfo(attrMsg, "SEN:to", "n-ary relation target", B_STRING_TYPE, false, false);

	// *** SEN core properties exposed to user, may be modified through UI
	
	// level of confidence in percentage from 0..100, may be used for generated relations or also manual ones
	AddMimeInfo(attrMsg, "SEN:confidencePercentage", "confidence %", B_INT8_TYPE, true, true);
	// sensitive (or sen:sitive?;) relations may be hidden from queries or display, depending on context
	AddMimeInfo(attrMsg, "SEN:sensitive", "sensitive?", B_BOOL_TYPE, true, true);
    
	// *** user facing properties
	
	// SEN context specific to this relation
	AddMimeInfo(attrMsg, "SEN:context", "Context", B_STRING_TYPE, true, true);
    // to express this relation to be bound to a specific aspect (level, pov)
	AddMimeInfo(attrMsg, "SEN:aspect", "Aspect", B_STRING_TYPE, true, true);
	// custom comment
	AddMimeInfo(attrMsg, "SEN:comment", "Comment", B_STRING_TYPE, true, true);
	// used for display
	AddMimeInfo(attrMsg, "SEN:label", "Label", B_STRING_TYPE, true, true);
   	// role of the target that is part of this relation
	AddMimeInfo(attrMsg, "SEN:role", "Role", B_STRING_TYPE, true, true);
	// relations may be temporary/ephereal, allowing users to model time bounded relations
	AddMimeInfo(attrMsg, "SEN:validFrom", "valid from", B_TIME_TYPE, true, true);
	AddMimeInfo(attrMsg, "SEN:validTo", "valid to", B_TIME_TYPE, true, true);

    mime.SetShortDescription("SEN Relation Definition");
    mime.SetLongDescription("Configures a relation and its core properties for SEN");
	// allow the relation to be opened in Tracker and handing over logic to SEN
    mime.SetPreferredApp(SEN_SERVER_SIGNATURE);
    mime.SetAttrInfo(attrMsg);

    LOG("successfully created Relation FileType " SEN_CONFIG_RELATION_SUPERTYPE_NAME "\n");    
    return B_OK;
}

void SenConfigHandler::AddMimeInfo(BMessage* attrMsg, const char* name, const char* displayName, int32 type, bool viewable, bool editable)
{
    attrMsg->AddString("attr:name", name);
    attrMsg->AddString("attr:public_name", displayName);
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
                        delete[] name;
                    }
                }
            }
        } else {
            ERROR("failed to access relations config dir: %u", relationsDirStatus);
            return relationsDirStatus;
        }
    }

    BPath path(relationsDir);
    if (status_t status = path.InitCheck() != B_OK) {
        ERROR("failed to access relations config dir: %u\n", status);
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
        ERROR("failed to create relation '%s': %u\n", name, relationStatus);
        // bail out, configuration must be all valid
        return relationStatus;
    }
   
    // set type
    //relation.WriteAttrString("BEOS:TYPE", new BString(SEN_CONFIG_RELATION_TYPE_NAME));
    BNodeInfo relationNodeInfo(&relation);
    relationNodeInfo.SetType(SEN_CONFIG_RELATION_SUPERTYPE_NAME);
    
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
    delete[] msgBuffer;
    
    msgSize = configProps->FlattenedSize();
    msgBuffer = new char[msgSize];
    configProps->Flatten(msgBuffer, msgSize);
    relation.WriteAttr(SEN_ATTRIBUTES_PREFIX "properties", B_MESSAGE_TYPE, 0, msgBuffer, msgSize);
    
    delete[] msgBuffer;
    
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
