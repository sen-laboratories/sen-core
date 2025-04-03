/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <cassert>
#include <fs_attr.h>
#include <Node.h>
#include <Path.h>
#include <stdio.h>
#include <string>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include "RelationsHandler.h"
#include "../Sen.h"

RelationsHandler::RelationsHandler()
    : BHandler("SenRelationsHandler")
{
    tsidGenerator = new IceDustGenerator();
}

RelationsHandler::~RelationsHandler()
{
}

void RelationsHandler::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;
    LOG("RelationsHandler got message:\n");
    message->PrintToStream();

    switch(message->what)
    {
		case SEN_RELATIONS_GET:
		{
			result = GetRelationsOfType(message, reply);
			break;
		}
        case SEN_RELATIONS_GET_ALL:
		{
			result = GetAllRelations(message, reply);
			break;
		}
		case SEN_RELATIONS_GET_SELF:
		{
			result = GetSelfRelationsOfType(message, reply);
			break;
		}
        case SEN_RELATIONS_GET_ALL_SELF:
		{
			result = GetSelfRelations(message, reply);
			break;
		}
		case SEN_RELATION_ADD:
		{
			result = AddRelation(message, reply);

            if (result == B_OK) {
                // check if relation is bidirectional and add reverse relation
                BMessage relationConf;
                status_t status = GetRelationMimeConfig(reply->GetString(SEN_RELATION_TYPE), &relationConf);

                // relations are bidirectional by default (makes sense in 90% of cases)
                if (status == B_OK && relationConf.GetBool(SEN_RELATION_IS_BIDIR, true)) {
                    LOG("got relation config:\n");
                    relationConf.PrintToStream();

                    // clone source message and and build reverse relation config
                    BMessage reverseRelation(*message);

                    entry_ref srcRef, targetRef;
                    reply->FindRef(SEN_RELATION_SOURCE, &srcRef);
                    reply->FindRef(SEN_RELATION_TARGET, &targetRef);

                    // remove any existing source/target and add swapped refs for reverse relation
                    reverseRelation.RemoveData(SEN_RELATION_SOURCE);
                    reverseRelation.RemoveData(SEN_RELATION_TARGET);
                    reverseRelation.AddRef(SEN_RELATION_SOURCE, &targetRef);
                    reverseRelation.AddRef(SEN_RELATION_TARGET, &srcRef);

                    // get reverse properties from config (may be empty)
                    BMessage reverseConf;
                    status = relationConf.FindMessage(SEN_RELATION_CONFIG_REVERSE, &reverseConf);
                    if (status == B_OK && !reverseConf.IsEmpty()) {
                        LOG("got reverse relation config:\n");
                        reverseConf.PrintToStream();

                        // add or replace properties with reverse relation properties
                        reverseRelation.Append(reverseConf);

                        LOG("reverse relation is now:\n");
                        reverseRelation.PrintToStream();
                    }

                    BMessage replyReverse;
                    status = AddRelation(&reverseRelation, &replyReverse);

                    if (status == B_OK) {
                        reply->AddMessage("reply_reverse", &replyReverse);
                    }
                    reply->AddString("result_reverse", strerror(status));
                }
            }
			break;
		}
		case SEN_RELATION_REMOVE:
		{
			result = RemoveRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_REMOVE_ALL:
		{
			result = RemoveAllRelations(message, reply);
			break;
		}
        default:
        {
            LOG("RelationsHandler: unkown message received: %u\n", message->what);
            reply->AddString("error", "cannot handle this message.");
        }
    }
    LOG("RelationsHandler sending reply %s\n", strerror(result));

    reply->AddString("result", strerror(result));
    reply->PrintToStream();

	message->SendReply(reply);
}

status_t RelationsHandler::GetMessageParameter(
    const BMessage* message, BMessage* reply,
    const char* param, BString* buffer, entry_ref* ref,
    bool mandatory, bool stripSuperType)
{
    status_t status;

    // first check the value for mandatory parameters exist
    const void *data;
    ssize_t     size;
    type_code   type;

    status = message->FindData(param, B_ANY_TYPE, &data, &size);

    if (mandatory && (status != B_OK || size <= 1)) { // also check for empty String values below
        // check for empty string
        status = message->GetInfo(param, &type);
        if (status != B_OK) {
            ERROR("failed to parse argument %s: %s\n", param, strerror(status));
            return status;
        }
        if (status != B_OK || type == B_STRING_TYPE) {  // empty string has 1 NULL byte
            BString error;
            error << "missing required parameter " << param;
            ERROR("%s\n", error.String());
            reply->AddString("error", error);
            return B_BAD_VALUE;
        }
	}

    // then parse parameter
    BString paramStr(param);

    // handle source path / ref and always return back entry_ref
    if (paramStr == SEN_RELATION_SOURCE || paramStr == SEN_RELATION_TARGET) {
        status = GetPathOrRef(message, reply, param, ref);
    } else {
        status = message->FindString(param, buffer);
        if (status != B_OK) {
            BString error;
            error << "failed to parse parameter " << param << ": " << strerror(status);

            ERROR("%s\n", error.String());
            reply->AddString("error", error);
        }

        if (stripSuperType && BString(param) == SEN_RELATION_TYPE) {
            // remove Relation supertype for relation params for internal handling
            buffer = StripSuperType(buffer);
        }

        // add to reply for possible internal use
        if (status == B_OK && buffer != NULL) {
            reply->AddString(param, BString(*buffer));
        }
    }

    return status;
}

status_t RelationsHandler::GetPathOrRef(const BMessage* message, BMessage *reply, const char* param, entry_ref* ref)
{
    status_t status;
    BString refParam(param);
    refParam.Append("Ref");     // by convention, ref params have a "Ref" suffix in SEN

    if (message->HasString(param)) {
        // get path string
        BString pathStr;
        status = message->FindString(param, &pathStr);
        if (status != B_OK) {
            ERROR("could not parse path string from param '%s': %s\n", param, strerror(status));
            return status;
        }

        // and translate to ref
        BEntry entry(pathStr.String());
        if (status == B_OK) status = entry.InitCheck();
        if (status == B_OK) status = entry.GetRef(ref);
        if (status == B_OK) {
            // add ref to reply for possible later use internally
            reply->AddRef(refParam.String(), new entry_ref(ref->device, ref->directory, ref->name));
        }
    } else {
        if (message->HasRef(refParam.String()) && ref != NULL) {
            status = message->FindRef(refParam.String(), ref);
        } else {
            ERROR("invalid arguments for param '%s': either String or Ref is needed!\n", param);
            return B_BAD_VALUE;
        }
        // add path for use in external non-native clients
        BPath path(ref);
        status = reply->AddString(param, path.Path());
    }

    LOG("got ref for param %s: %s\n", param, ref->name);

    return status;
}

BString* RelationsHandler::StripSuperType(BString* mimeType) {
    if (mimeType->StartsWith(SEN_RELATION_SUPERTYPE "/")) {
        mimeType->Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
    }
    return mimeType;
}

status_t RelationsHandler::AddRelation(const BMessage* message, BMessage* reply)
{
    status_t status;
	entry_ref sourceRef;

	if ((status = GetMessageParameter(message, reply, SEN_RELATION_SOURCE, NULL, &sourceRef))  != B_OK) {
		return status;
	}

	BString relationType;
	if ((status = GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationType))  != B_OK) {
		return status;
	}
    const char* relation = relationType.String();

    entry_ref targetRef;
	if ((status = GetMessageParameter(message, reply, SEN_RELATION_TARGET, NULL, &targetRef))  != B_OK) {
		return status;
	}

	char srcId[SEN_ID_LEN];
    status = GetOrCreateId(&sourceRef, srcId, true);
	if (status != B_OK) {
		return status;
	}

	// get existing relations of the given type from the source file
	BMessage relations;
    status = ReadRelationsOfType(&sourceRef, relation, &relations);
	if (status != B_OK) {
		ERROR("failed to read relations of type %s from file %s\n", relation, sourceRef.name);
		return B_ERROR;
	} else if (relations.IsEmpty()) {
        LOG("creating new relation %s for file %s\n", relation, sourceRef.name);
    } else {
        LOG("got relations for type %s and file %s:\n", relation, sourceRef.name);
        relations.PrintToStream();
    }

    // prepare target
	char targetId[SEN_ID_LEN];
    status = GetOrCreateId(&targetRef, targetId, true);
	if (status != B_OK) {
		return status;
	}

    // prepare new relation properties with properties from message received
    BMessage properties(*message);

    // remove internal SEN properties
	properties.RemoveData(SEN_RELATION_SOURCE);
	properties.RemoveData(SEN_RELATION_TARGET);
	properties.RemoveData(SEN_RELATION_TYPE);

    // we allow multipe relations of the same type to the same target
    // (e.g. a note for the same text referencing different locations in the referenced text).
    // Hence, we have a Message with targetId as key pointing to 1-N messages with relation properties
    // for that target.
    // Since there is no method BMessage::FindMessages() to collect them all, we need to iterate.
    BMessage oldProperties;
    int index = 0;
    while (relations.FindMessage(targetId, index, &oldProperties) == B_OK) {
        LOG("got existing relation properties at index %d:\n", index);
        oldProperties.PrintToStream();

        // get existing relation properties for target if available and skip if already there
        if (oldProperties.HasSameData(properties)) {
            LOG("skipping add relation %s for target %s, already there:\n", relation, targetId);
            oldProperties.PrintToStream();

            reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("status", BString("relation with same properties already exists"));

            return B_OK;
        }
        index++;
    }
    if (index > 0) {
        LOG("adding new properties for existing relation %s\n", relation);
    } else {
        LOG("adding new target %s for relation %s\n", targetId, relation);
        relations.AddString(SEN_TO_ATTR, BString(targetId));
    }
    // add new relation properties for target
    relations.AddMessage(BString(targetId).String(), &properties);

    LOG("new relation properties for relation %s and target %s:\n", relation, targetRef.name);
    properties.PrintToStream();

    // write new relation to designated attribute
    const char* attrName = GetAttributeNameForRelation(relation);
    LOG("writing new relation into attribute %s of file %s\n", attrName, sourceRef.name);

    BNode node(&sourceRef); // has been checked already at least once here

    ssize_t msgSize = relations.FlattenedSize();
    char msgBuffer[msgSize];
    status_t flatten_status = relations.Flatten(msgBuffer, msgSize);

    if (flatten_status != B_OK) {
        ERROR("failed to store relation properties for relation %s in file %s\n", relation, sourceRef.name);
            reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("status", BString("failed to create relation '") << relation << "' from "
            << sourceRef.name << " [" << srcId << "] -> " <<  targetRef.name << " [" << targetId << "]: \n"
            << flatten_status);

        return B_ERROR;
    }

    ssize_t result = node.WriteAttr(
            attrName,
            B_MESSAGE_TYPE,
            0,
            msgBuffer,
            msgSize);

    if (result <= 0) {
        ERROR("failed to store relation %s for file %s: %s\n", relation, sourceRef.name, strerror(result));

        reply->what = SEN_RESULT_RELATIONS;
        reply->AddString("status", BString("failed to create relation '") << relation << "' from "
            << sourceRef.name << " [" << srcId << "] -> " <<  targetRef.name << " [" << targetId << "]: \n"
            << strerror(result));

        return B_ERROR;
    }

    LOG("created relation %s from src ID %s to target ID %s with properties:\n", relation, srcId, targetId);
    relations.PrintToStream();

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("status", BString("created relation '") << relation << "' from "
		<< sourceRef.name << " [" << srcId << "] -> " <<  targetRef.name << " [" << targetId << "]");

	return B_OK;
}

status_t RelationsHandler::GetAllRelations(const BMessage* message, BMessage* reply)
{
	entry_ref sourceRef;
    status_t  status;

	if ((status = GetMessageParameter(message, reply, SEN_RELATION_SOURCE, NULL, &sourceRef))  != B_OK) {
		return status;
	}
    bool withProperties = message->GetBool("properties");

    BStringList* relationNames = ReadRelationNames(&sourceRef);
    if (relationNames == NULL) {
        return B_ERROR;
    }
    if (withProperties) {
        // add all properties of all relations found above and add to result per type for lookup
        for (int i = 0; i < relationNames->CountStrings(); i++) {
            BString relation = relationNames->StringAt(i);
            LOG("adding properties of relation %s...\n", relation.String());

            BMessage relations;
            status = ReadRelationsOfType(&sourceRef, relation.String(), &relations);
            if (status != B_OK) {
                return status;
            }
            reply->AddMessage(relation.String(), new BMessage(relations));
        }
    }
    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings(SEN_RELATIONS, *relationNames);

    char senId[SEN_ID_LEN];
    status = GetOrCreateId(&sourceRef, senId);
    if (status == B_OK) {
        reply->AddString(SEN_ID_ATTR, senId);
        reply->AddString("status", BString("got ")
            << relationNames->CountStrings() << " relation(s) from " << sourceRef.name);
    }

	return status;
}

status_t RelationsHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
{
	entry_ref sourceRef;
    status_t  status;

	if ((status = GetMessageParameter(message, reply, SEN_RELATION_SOURCE, NULL, &sourceRef))  != B_OK) {
		return status;
	}

	BString relationType;
	if ((status = GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationType))  != B_OK) {
		return status;
	}

    BMessage relations;
    status = ReadRelationsOfType(&sourceRef, relationType.String(), &relations);
    if (status != B_OK) {
        reply->AddString("error", "failed to retrieve relations of given type.");
        reply->AddString("cause", strerror(status));
        return status;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage(SEN_RELATIONS, new BMessage(relations));
    reply->AddString("status", BString("retrieved ") << relations.CountNames(B_REF_TYPE)
                 << " relations from " << sourceRef.name);

    reply->PrintToStream();

	return B_OK;
}

// private methods

status_t RelationsHandler::ReadRelationsOfType(const entry_ref* sourceRef, const char* relationType, BMessage* relations)
{
    BNode node(sourceRef);
    status_t status;

	if ((status = node.InitCheck()) != B_OK) {
		ERROR("failed to initialize node for ref %s: %s\n", sourceRef->name, strerror(status));
		return status;
	}
    // read relation config as message from respective relation attribute
    attr_info attrInfo;
    const char* attrName = GetAttributeNameForRelation(relationType);
    LOG("checking for relation %s in atttribute %s\n", relationType, attrName);

    if ((status = node.GetAttrInfo(attrName, &attrInfo)) != B_OK) {
        // if attribute not found, e.g. new relation, this is OK, else it's a real ERROR
        if (status != B_ENTRY_NOT_FOUND) {
            ERROR("failed to get attribute info for ref %s: %s\n", sourceRef->name, strerror(status));
            return status;
        }
        return B_OK;
    }
    // read relation property message
    char* relation_attr_value = new char[attrInfo.size + 1];
    ssize_t result = node.ReadAttr(
            attrName,
            B_MESSAGE_TYPE,
            0,
            relation_attr_value,
            attrInfo.size);

    if (result == 0) {          // result is bytes read
        LOG("no relations of type %s found for path %s.\n", relationType, sourceRef->name);
        return B_OK;
    } else if (result < 0) {    // result is an error code
        ERROR("failed to read relation %s of file %s: %s\n", relationType, sourceRef->name, strerror(result));
        return result;
    }

    BMessage *resultMsg = new BMessage();
    resultMsg->Unflatten(relation_attr_value);

    // add target refs - todo: maybe add parm to toggle this when not needed
    BStringList ids;
    BObjectList<BEntry> entries;
    resultMsg->FindStrings(SEN_TO_ATTR, &ids);

    if ((status = ResolveRelationTargets(&ids, &entries)) == B_OK) {
        LOG("got %d relation targets for type %s and file %s, resolving entries...\n",
            entries.CountItems(), relationType, sourceRef->name);

        for (int32 i = 0; i < entries.CountItems(); i++) {
            entry_ref ref;
            BEntry entry = *entries.ItemAt(i);
            if ((status = entry.InitCheck()) == B_OK) {
                if ((status = entry.GetRef(&ref)) == B_OK) {
                    relations->AddRef("refs", &ref);
                } else {
                    ERROR("failed to resolve ref for target %s of relation %s and file %s: %s.\n",
                        ids.StringAt(i).String(), relationType, sourceRef->name, strerror(status));
                    return status;
                }
            }
        }
    } else {
        ERROR("failed to read relation targets for relation %s of file %s.\n", relationType, sourceRef->name);
        return status;
    }

    LOG("read %d relation(s) of type %s:\n", resultMsg->CountNames(B_MESSAGE_TYPE), relationType);
    return status;
}

status_t RelationsHandler::RemoveRelation(const BMessage* message, BMessage* reply)
{
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();

	BString relationType;
	if (GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationType)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relation = relationType.String();

    // todo: implement!

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("status", BString("removed relation ") << relation << " from " << source);

	return B_OK;
}

status_t RelationsHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("status", BString("removed all relations from ") << source);

	return B_OK;
}

/*
 * private methods
 */

BStringList* RelationsHandler::ReadRelationNames(const entry_ref* ref)
{
	BStringList* result = new BStringList();

	BNode node(ref);
	if (node.InitCheck() != B_OK) {
        ERROR("failed to read from %s\n", ref->name);
		return result;
    }

	char *attrName = new char[B_ATTR_NAME_LENGTH];
    int relation_prefix_len = BString(SEN_RELATION_ATTR_PREFIX).Length();

	while (node.GetNextAttrName(attrName) == B_OK) {
		BString relationAttr(attrName);
		if (relationAttr.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
            // relation name is supertype + attribute name without SEN prefixes
			result->Add(BString(SEN_RELATION_SUPERTYPE "/")
                        .Append(relationAttr.Remove(0, relation_prefix_len).String()));
		}
	}

	return result;
}

status_t RelationsHandler::ResolveRelationTargets(BStringList* ids, BObjectList<BEntry> *result)
{
	LOG("resolving ids from list with %d targets...\n", ids->CountStrings())

    for (int i = 0; i < ids->CountStrings(); i++) {
        BEntry entry;
		if (QueryById(ids->StringAt(i).String(), &entry) == B_OK) {
            LOG("adding entry %s.\n", entry.Name());
            result->AddItem(new BEntry(entry));
        } else {
            return B_ERROR;
        }
	}

	return B_OK;
}

//
// utility functions
//
const char* RelationsHandler::GenerateId() {
    return (new std::string(std::to_string(tsidGenerator->generate())) )->c_str();
}

/**
 * we use the inode as a stable file/dir reference
 * (for now, just like filesystem links, they have to stay on the same device)
 */
status_t RelationsHandler::GetOrCreateId(const entry_ref *ref, char* id, bool createIfMissing)
{
    status_t result;
	BNode node(ref);

	if ((result = node.InitCheck()) != B_OK) {
		ERROR("failed to initialize node for path %s: %s\n", ref->name, strerror(result));
		return result;
	}

    BString idStr;
    result = node.ReadAttrString(SEN_ID_ATTR, &idStr);
    if (result == B_ENTRY_NOT_FOUND) {
        if (! createIfMissing) {
            return result;
        }
        // todo: use try/catch!
        strncpy(id, GenerateId(), SEN_ID_LEN);

        if (id != NULL) {
            LOG("generated new ID %s for path %s\n", id, ref->name);
            if ((result = node.WriteAttrString(SEN_ID_ATTR, new BString(id))) != B_OK) {
                ERROR("failed to create ID for path %s: %s\n", ref->name, strerror(result));
                return result;
            }
            return B_OK;
        } else {
            ERROR("failed to create ID for path %s\n", ref->name);
            return B_ERROR;
        }
    } else if (result != B_OK) {
        ERROR("failed to read ID from path %s: %s\n", ref->name, strerror(result));
        return result;
    } else {
        strncpy(id, idStr.String(), SEN_ID_LEN);
        LOG("got existing ID %s for path %s\n", id, ref->name);
    }
    return B_OK;
}

const char* RelationsHandler::GetAttributeNameForRelation(const char* relationType)
{
    BString relationAttributeName(relationType);
    if (! relationAttributeName.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
        return relationAttributeName.Prepend(SEN_RELATION_ATTR_PREFIX).String();
    }
    return relationAttributeName.String();
}

status_t RelationsHandler::QueryById(const char* id, BEntry* entry)
{
    status_t result;
	LOG("query for id %s\n", id);

	BString predicate(BString(SEN_ID_ATTR) << "==" << id);
	// TODO: all relation queries currently assume we never leave the boot volume
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

	if ((result = query.Fetch()) != B_OK) {
        ERROR("could not execute query for SEN:ID %s: %s\n", id, strerror(result));
        return result;
    }
    if ((result = query.GetNextEntry(entry)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            LOG("no matching file found for ID %s\n", id);
            return B_OK;
        }
        // something other went wrong
        ERROR("error resolving id %s: %s\n", id, strerror(result));
        return result;
    }
    else {
        entry_ref ref;
        if (query.GetNextRef(&ref) == B_OK) {
            // this should never happen as the SEN:ID MUST be unique!
            ERROR("Critical error SEN:ID %s is NOT unique!\n", id);
            return B_DUPLICATE_REPLY;
        }
        BPath path;
        entry->GetPath(&path);
        LOG("found entry with path %s\n", path.Path());
        query.Clear();
    }
    return B_OK;
}

status_t RelationsHandler::GetRelationMimeConfig(const char* mimeType, BMessage* relationConfig)
{
    BString relation(mimeType);
    if (!relation.StartsWith(SEN_RELATION_SUPERTYPE))
        relation.Prepend(SEN_RELATION_SUPERTYPE "/");

    BMimeType relationType(relation);
    status_t result = relationType.InitCheck();
    if (result == B_OK) {
        result = relationType.GetAttrInfo(relationConfig);
    }
    if (result != B_OK) {
        ERROR("could not get relation config for type %s: %s\n", mimeType, strerror(result));
    }

    return result;
}
