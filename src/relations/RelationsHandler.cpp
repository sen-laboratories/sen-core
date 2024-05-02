/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "RelationsHandler.h"
#include "../Sen.h"

#include <fs_attr.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <stdio.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

RelationsHandler::RelationsHandler()
    : BHandler("SenRelationsHandler")
{
}

RelationsHandler::~RelationsHandler()
{
}

void RelationsHandler::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;

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
            LOG("RelationsHandler: unkown message received, handing over to parent: %u\n", message->what);
            BHandler::MessageReceived(message);
            return;
        }
    }
    LOG("RelationsHandler sending reply %d\n", result);
   	reply->AddInt32("result", result);
	message->SendReply(reply);
}

status_t RelationsHandler::AddRelation(BMessage* message, BMessage* reply)
{
	const char* source = new char[B_FILE_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_SOURCE, &source)   != B_OK) {
		return B_BAD_VALUE;
	}
	const char* relation = new char[B_ATTR_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	const char* target = new char[B_FILE_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_TARGET, &target)   != B_OK) {
		return B_BAD_VALUE;
	}

	const char* srcId = GetOrCreateId(source);
	if (srcId == NULL) {
		return B_ERROR;
	}

	// get existing relations of the given type from the source file
	BMessage* relations = ReadRelationsOfType(source, relation);
	if (relations == NULL) {
		ERROR("failed to read relations of type %s from file %s\n", relation, source);
		return B_ERROR;
	} else if (relations->IsEmpty()) {
        DEBUG("creating new relation %s for file %s\n", relation, source);
    }
    DEBUG("got relations for type %s and file %s:\n", relation, source);
    relations->PrintToStream();

    // prepare target
	const char* targetId = GetOrCreateId(target);
	if (targetId == NULL) {
		return B_ERROR;
	}

    // prepare new relation properties with properties from message received
    BMessage properties(*message);

    // remove internal SEN properties
	properties.RemoveData(SEN_RELATION_SOURCE);
	properties.RemoveData(SEN_RELATION_TARGET);
	properties.RemoveData(SEN_RELATION_NAME);

    // we allow multipe relations of the same type to the same target
    // (e.g. a note for the same text referencing different locations in the referenced text).
    // Hence, we have a Message with targetId as key pointing to 1-N messages with relation properties for that target.
    // since there is no method BMessage::FindMessages() to collect them all, we need to iterate.
    BMessage oldProperties;
    int index = 0;
    while (relations->FindMessage(targetId, index, &oldProperties) == B_OK) {
        DEBUG("got existing relation properties at index %d:\n", index);
        oldProperties.PrintToStream();

        // get existing relation properties for target if available and skip if already there
        if (oldProperties.HasSameData(properties)) {
            DEBUG("skipping add relation %s for target %s, already there:\n", relation, targetId);
            oldProperties.PrintToStream();

            reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("statusMessage", BString("relation with same properties already exists"));

            return B_OK;
        }
        index++;
    }
    // add new relation with properties
    relations->AddMessage(targetId, &properties);
    DEBUG("new relation properties for relation %s and target %s:\n", relation, target);
    properties.PrintToStream();

    // write new relation to designated attribute
    const char* attrName = GetAttributeNameForRelation(relation);
    DEBUG("writing new relation into attribute %s of file %s\n", attrName, source);

    BNode node(source); // has been checked already at least once here

    ssize_t msgSize = relations->FlattenedSize();
    char msgBuffer[msgSize];
    status_t flatten_status = relations->Flatten(msgBuffer, msgSize);

    if (flatten_status != B_OK) {
        ERROR("failed to store relation properties for relation %s in file %s\n", relation, source);
            reply->what = SEN_RESULT_RELATIONS;
            reply->AddString("statusMessage", BString("failed to create relation '") << relation << "' from "
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
            reply->AddString("statusMessage", BString("failed to create relation '") << relation << "' from "
            << source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]: \n"
            << writeSizeStatus);
        return B_ERROR;
    }

    DEBUG("created relation from src %s to target %s with properties:\n", srcId, targetId);
    relations->PrintToStream();

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("created relation '") << relation << "' from "
		<< source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]");

	return B_OK;
}

status_t RelationsHandler::GetAllRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
    bool withProperties = message->GetBool("withProperties");

    BStringList* relationNames = ReadRelationNames(source.String());
    if (relationNames == NULL) {
        return B_ERROR;
    }
    if (withProperties) {
        // add all properties of all relations found above and add to result per type for lookup
        for (int i = 0; i < relationNames->CountStrings(); i++) {
            BString relation = relationNames->StringAt(i);
            DEBUG("adding properties of relation %s...\n", relation.String());

            BMessage* relations = ReadRelationsOfType(source.String(), relation.String());
            if (relations == NULL) {
                return B_ERROR;
            }
            reply->AddMessage(relation.String(), relations);
        }
    }
    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings("relations", *relationNames);
    reply->AddString("statusMessage", BString("got ")
        << relationNames->CountStrings() << " relation(s) from " << source);

	return B_OK;
}

status_t RelationsHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relation = new char[B_ATTR_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}

    BMessage* relations = ReadRelationsOfType(source.String(), relation);
    if (relations == NULL) {
        return B_ERROR;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage("result", relations);
    reply->AddString("statusMessage", BString("retrieved ") << relations->CountNames(B_STRING_TYPE)
        << " relations from " << source);

	return B_OK;
}

// private methods

BMessage* RelationsHandler::ReadRelationsOfType(const char* path, const char* relationType)
{
    BNode node(path);
	if (node.InitCheck() != B_OK) {
		ERROR("failed to initialize node for path %s\n", path);
		return NULL;
	}
    // read relation config as message from respective relation attribute
    attr_info attrInfo;
    const char* attrName = GetAttributeNameForRelation(relationType);
    BMessage *relationMsg = new BMessage();

    DEBUG("checking for relation %s in atttribute %s\n", relationType, attrName);

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
        DEBUG("no relations of type %s found for path %s.\n", relationType, path);
        return new BMessage();
    } else if (result < 0) {
        ERROR("failed to read relation %s of file %s.\n", relationType, path);
        return NULL;
    }

    relationMsg->Unflatten(relation_attr_value);
    DEBUG("read %d relations of type %s:\n", relationMsg->CountNames(B_MESSAGE_TYPE), relationType);
    relationMsg->PrintToStream();

    return relationMsg;
}

status_t RelationsHandler::RemoveRelation(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("removed relation ") << relation << " from " << source);

	return B_OK;
}

status_t RelationsHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("removed all relations from ") << source);

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

	BString relation;
    int relation_prefix_len = BString(SEN_RELATION_ATTR_PREFIX).Length();

	while (node.GetNextAttrName(attrName) == B_OK) {
		relation = BString(attrName);
		if (relation.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
			result->Add(relation.Remove(0, relation_prefix_len));
		}
	}

	return result;
}

BObjectList<BEntry>* RelationsHandler::ResolveRelationTargets(BStringList* ids)
{
	DEBUG("resolving ids from list with %d targets...\n", ids->CountStrings())
	BObjectList<BEntry>* targets = new BObjectList<BEntry>();

	while (!ids->IsEmpty()) {
		BString id = ids->First();
		QueryForId(id, targets);
		ids->Remove(id);
	}
	return targets;
}

/**
 * we use the inode as a stable file/dir reference
 * (for now, just like filesystem links, they have to stay on the same device)
 */
const char* RelationsHandler::GetOrCreateId(const char *path)
{
    DEBUG("init node for path %s\n", path);

	BNode node(path);
	if (node.InitCheck() != B_OK) {
		ERROR("failed to initialize node for path %s\n", path);
		return NULL;
	}

    BString id;
    ssize_t result = node.ReadAttrString(SEN_ID_ATTR, &id);
    if (result == B_ENTRY_NOT_FOUND) {
        id = GenerateId(&node);
        if (id != NULL) {
            DEBUG("generated new ID %s for path %s\n", id.String(), path);
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
    DEBUG("got existing ID %s for path %s\n", id.String(), path);

    return (new BString(id))->String();
}

const char* RelationsHandler::GenerateId(BNode* node) {
	node_ref nodeInfo;
	node->GetNodeRef(&nodeInfo);

	char* id = new char[sizeof(ino_t)];
	if (snprintf(id, sizeof(id), "%ld", nodeInfo.node) >= 0) {
		return id;
	} else {
		return NULL;
	}
}

const char* RelationsHandler::GetAttributeNameForRelation(BString relationType) {
    // cut supertype "relation" if present
    if (relationType.StartsWith(SEN_RELATION_SUPERTYPE)) {
        relationType.Remove(0, BString(SEN_RELATION_ATTR_PREFIX).Length());
    }
    return BString(SEN_RELATION_ATTR_PREFIX).Append(relationType).String();
}

bool RelationsHandler::QueryForId(const BString& id, void* result)
{
	LOG("query for id %s\n", id.String());

	BString predicate(BString(SEN_ID_ATTR) << " == " << id);
	// all relation queries currently assume we never leave the boot volume
    // todo: integrate ID generator from TSID source
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

	if (query.Fetch() == B_OK)
	{
		LOG("Results of query \"%s\":\n", predicate.String());
		BEntry* entry = new BEntry();

		// if not while becaues result should always only contain a single id (inode)
		// else, the relation references are corrupt and need to be repaired.
		if (query.GetNextEntry(entry) == B_OK)
		{
			BPath path;
			entry->GetPath(&path);
			LOG("\t%s\n", path.Path());

			reinterpret_cast<BObjectList<BEntry>*>(result)->AddItem(entry);
		}
		else
			ERROR("error resolving id %s\n", id.String());
	}
	return true;
}
