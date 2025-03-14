/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

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
            reply->AddString("cause", "cannot handle this message.");
        }
    }
    LOG("RelationsHandler sending reply %s\n", strerror(result));
   	reply->AddInt32("resultCode", result);
   	reply->AddString("result", strerror(result));

    reply->PrintToStream();

	message->SendReply(reply);
}

status_t RelationsHandler::GetMessageParameter(
    const BMessage* message, BMessage* reply,
    const char* param, BString* buffer,
    bool mandatory, bool stripSuperType) {

    if (mandatory && (message->FindString(param, buffer) != B_OK)) {
        ERROR("failed to read required param '%s'.\n", param);
        reply->AddString("cause", (new BString("missing required parameter "))->Append(param).String());
		return B_BAD_VALUE;
	}
    // remove Relation supertype for relation params for internal handling
    if (stripSuperType && BString(param) == SEN_RELATION_TYPE) {
        buffer = StripSuperType(buffer);
    }
    return B_OK;
}

BString* RelationsHandler::StripSuperType(BString* mimeType) {
    if (mimeType->StartsWith(SEN_RELATION_SUPERTYPE "/")) {
        mimeType->Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
    }
    return mimeType;
}

status_t RelationsHandler::AddRelation(const BMessage* message, BMessage* reply)
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

	BString targetParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_TARGET, &targetParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* target = targetParam.String();

	const char* srcId = GetOrCreateId(source, true);
	if (srcId == NULL) {
		return B_ERROR;
	}

	// get existing relations of the given type from the source file
	BMessage* relations = ReadRelationsOfType(source, relation, reply);
	if (relations == NULL) {
		ERROR("failed to read relations of type %s from file %s\n", relation, source);
		return B_ERROR;
	} else if (relations->IsEmpty()) {
        LOG("creating new relation %s for file %s\n", relation, source);
    }
    LOG("got relations for type %s and file %s:\n", relation, source);
    relations->PrintToStream();

    // prepare target
	const char* targetId = GetOrCreateId(target, true);
	if (targetId == NULL) {
		return B_ERROR;
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
    while (relations->FindMessage(targetId, index, &oldProperties) == B_OK) {
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
        relations->AddString(SEN_TO_ATTR, BString(targetId));
    }
    // add new relation properties for target
    relations->AddMessage(BString(targetId).String(), &properties);
    LOG("new relation properties for relation %s and target %s:\n", relation, target);
    properties.PrintToStream();

    // write new relation to designated attribute
    const char* attrName = GetAttributeNameForRelation(relation);
    LOG("writing new relation into attribute %s of file %s\n", attrName, source);

    BNode node(source); // has been checked already at least once here

    ssize_t msgSize = relations->FlattenedSize();
    char msgBuffer[msgSize];
    status_t flatten_status = relations->Flatten(msgBuffer, msgSize);

    if (flatten_status != B_OK) {
        ERROR("failed to store relation properties for relation %s in file %s\n", relation, source);
            reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("status", BString("failed to create relation '") << relation << "' from "
            << source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]: \n"
            << flatten_status);
        return B_ERROR;
    }

    ssize_t writeSizeStatus = node.WriteAttr(
            attrName,
            B_MESSAGE_TYPE,
            0,
            msgBuffer,
            msgSize);

    if (writeSizeStatus <= 0) {
        ERROR("failed to store relation %s for file %s\n", relation, source);
        	reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("status", BString("failed to create relation '") << relation << "' from "
            << source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]: \n"
            << writeSizeStatus);
        return B_ERROR;
    }

    LOG("created relation %s from src ID %s to target ID %s with properties:\n", relation, srcId, targetId);
    relations->PrintToStream();

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("status", BString("created relation '") << relation << "' from "
		<< source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]");

	return B_OK;
}

status_t RelationsHandler::GetAllRelations(const BMessage* message, BMessage* reply)
{
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();
    bool withProperties = message->GetBool("properties");

    BStringList* relationNames = ReadRelationNames(source);
    if (relationNames == NULL) {
        return B_ERROR;
    }
    if (withProperties) {
        // add all properties of all relations found above and add to result per type for lookup
        for (int i = 0; i < relationNames->CountStrings(); i++) {
            BString relation = relationNames->StringAt(i);
            LOG("adding properties of relation %s...\n", relation.String());

            BMessage* relations = ReadRelationsOfType(source, relation.String(), reply);
            if (relations == NULL) {
                return B_ERROR;
            }
            reply->AddMessage(relation.String(), relations);
        }
    }
    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings(SEN_RELATIONS, *relationNames);
    reply->AddString(SEN_ID_ATTR, GetOrCreateId(source));
    reply->AddString("status", BString("got ")
        << relationNames->CountStrings() << " relation(s) from " << source);

	return B_OK;
}

status_t RelationsHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
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

    BMessage* relations = ReadRelationsOfType(source, relation, reply);
    if (relations == NULL) {
        reply->AddString("cause", "failed to retrieve relations of given type.");
        return B_ERROR;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage(SEN_RELATIONS, relations);
    reply->AddString("status", BString("retrieved ") << relations->CountNames(B_STRING_TYPE)
        << " relations from " << source);

    reply->PrintToStream();
	return B_OK;
}

// private methods

BMessage* RelationsHandler::ReadRelationsOfType(const char* path, const char* relationType, BMessage* reply)
{
    BNode node(path);
	if (node.InitCheck() != B_OK) {
		ERROR("failed to initialize node for path %s\n", path);
		return NULL;
	}
    // read relation config as message from respective relation attribute
    attr_info attrInfo;
    const char* attrName = GetAttributeNameForRelation(relationType);
    LOG("checking for relation %s in atttribute %s\n", relationType, attrName);

    if (node.GetAttrInfo(attrName, &attrInfo) != B_OK) { // also if attribute not found, e.g. new relation
        return new BMessage();
    }
    char* relation_attr_value = new char[attrInfo.size + 1];
    ssize_t result = node.ReadAttr(
            attrName,
            B_MESSAGE_TYPE,
            0,
            relation_attr_value,
            attrInfo.size);

    if (result == 0) {
        LOG("no relations of type %s found for path %s.\n", relationType, path);
        return new BMessage();
    } else if (result < 0) {
        ERROR("failed to read relation %s of file %s.\n", relationType, path);
        return NULL;
    }

    BMessage *resultMsg = new BMessage();
    resultMsg->Unflatten(relation_attr_value);

    // add target refs - todo: maybe add parm to toggle this when not needed
    BStringList ids;
    BObjectList<BEntry> entries;
    resultMsg->FindStrings(SEN_TO_ATTR, &ids);

    if (ResolveRelationTargets(&ids, &entries) == B_OK) {
        LOG("got %d relation targets for type %s and file %s, resolving entries...\n",
            entries.CountItems(), relationType, path);

        for (int32 i = 0; i < entries.CountItems(); i++) {
            entry_ref ref;
            BEntry entry = *entries.ItemAt(i);
            if (entry.InitCheck() == B_OK) {
                if (entry.GetRef(&ref) == B_OK) {
                    reply->AddRef("refs", &ref);
                } else {
                    ERROR("failed to resolve ref for target %s of relation %s and file %s.\n",
                        ids.StringAt(i).String(), relationType, path);
                    return NULL;
                }
            }
        }
    } else {
        ERROR("failed to read relation targets for relation %s of file %s.\n", relationType, path);
        return NULL;
    }

    LOG("read %d relation(s) of type %s:\n", resultMsg->CountNames(B_MESSAGE_TYPE), relationType);
    return resultMsg;
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

BStringList* RelationsHandler::ReadRelationNames(const char* path)
{
	BStringList* result = new BStringList();

	BNode node(path);
	if (node.InitCheck() != B_OK) {
        ERROR("failed to read from %s\n", path);
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

const char* RelationsHandler::GenerateId() {
    return (new std::string(std::to_string(tsidGenerator->generate())) )->c_str();
}

/**
 * we use the inode as a stable file/dir reference
 * (for now, just like filesystem links, they have to stay on the same device)
 */
const char* RelationsHandler::GetOrCreateId(const char *path, bool createIfMissing)
{
	BNode node(path);
	if (node.InitCheck() != B_OK) {
		ERROR("failed to initialize node for path %s\n", path);
		return NULL;
	}

    BString id;
    status_t result = node.ReadAttrString(SEN_ID_ATTR, &id);
    if (result == B_ENTRY_NOT_FOUND) {
        if (!createIfMissing) {
            return NULL;
        }
        id = GenerateId();
        if (id != NULL) {
            LOG("generated new ID %s for path %s\n", id.String(), path);
            if (node.WriteAttrString(SEN_ID_ATTR, &id) != B_OK) {
                ERROR("failed to create ID for path %s\n", path);
                return NULL;
            }
            return id.String();
        } else {
            ERROR("failed to create ID for path %s\n", path);
            return NULL;
        }
    } else if (result != B_OK && result < 0) {
        ERROR("failed to read ID from path %s\n", path);
        return NULL;
    }
    LOG("got existing ID %s for path %s\n", id.String(), path);

    return (new BString(id))->String();
}

const char* RelationsHandler::GetAttributeNameForRelation(const char* relationType) {
    BString relationAttributeName(relationType);
    if (! relationAttributeName.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
        return relationAttributeName.Prepend(SEN_RELATION_ATTR_PREFIX).String();
    }
    return relationAttributeName.String();
}

status_t RelationsHandler::QueryById(const char* id, BEntry* entry)
{
	LOG("query for id %s\n", id);

	BString predicate(BString(SEN_ID_ATTR) << "==" << id);
	// TODO: all relation queries currently assume we never leave the boot volume
    // TODO: integrate ID generator from TSID source
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

	if (status_t result = query.Fetch() != B_OK) {
        ERROR("could not execute query for SEN:ID %s: %s\n", id, strerror(result));
        return result;
    }
    if (status_t result = query.GetNextEntry(entry) != B_OK) {
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
