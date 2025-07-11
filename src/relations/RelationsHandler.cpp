/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <cassert>
#include <fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <stdio.h>
#include <string>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include "RelationsHandler.h"
#include "Sen.h"

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
        case SEN_RELATIONS_GET_COMPATIBLE:
        {
            // special case for associations, here we go straight to target types
            BString relationType;
            status_t status = message->FindString(SEN_RELATION_TYPE, &relationType);
            if (status == B_OK) {
                 if (relationType == SEN_LABEL_RELATION_TYPE) {
                    LOG("resolving association targets...\n");
                    result = GetCompatibleTargetTypes(message, reply);
                    break;
                 }
            }
            result = GetCompatibleRelations(message, reply);
            break;
        }
        case SEN_RELATIONS_GET_COMPATIBLE_TYPES:
        {
            result = GetCompatibleTargetTypes(message, reply);
            break;
        }
        case SEN_RELATION_ADD:
        {
            result = AddRelation(message, reply);

            if (result == B_OK) {
                // check if relation is bidirectional and add reverse relation
                BMessage relationConf;
                BString  relationType;

                status_t status = reply->FindString(SEN_RELATION_TYPE, &relationType);
                if (status == B_OK) {
                     status = GetRelationMimeConfig(relationType.String(), &relationConf);
                }
                if (status != B_OK) {
                    LOG("failed to get relation config for type %s: %s\n",
                        relationType.String(), strerror(status));
                    result = status;
                    break;  // bail out
                }

                LOG("got relation config:\n");
                relationConf.PrintToStream();

                // relations are bidirectional by default (makes sense in 90% of cases)
                if (relationConf.GetBool(SEN_RELATION_IS_BIDIR, true)) {
                    LOG("relation is bidirectional.\n");

                    // clone source message and and build reverse relation config
                    BMessage reverseRelation(*message);

                    entry_ref srcRef, targetRef;
                    message->FindRef(SEN_RELATION_SOURCE_REF, &srcRef);
                    message->FindRef(SEN_RELATION_TARGET_REF, &targetRef);

                    // remove any existing source/target and add swapped refs for reverse relation
                    reverseRelation.RemoveData(SEN_RELATION_SOURCE_REF);
                    reverseRelation.RemoveData(SEN_RELATION_TARGET_REF);
                    // swap source and target
                    reverseRelation.AddRef(SEN_RELATION_SOURCE_REF, &targetRef);
                    reverseRelation.AddRef(SEN_RELATION_TARGET_REF, &srcRef);

                    // get optional reverse properties from config
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
                    } else {
                        LOG("failed to add reverse relation for type %s: %s\n",
                            relationType.String(), strerror(status));
                    }
                    reply->AddString("result_reverse", strerror(status));
                } else {
                    LOG("added UNIDIRECTIONAL relation, skipping reverse relation.\n");
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
    LOG("RelationsHandler sending reply %s with message:\n", strerror(result));

    reply->AddString("result", strerror(result));
    reply->PrintToStream();

    message->SendReply(reply);
}

status_t RelationsHandler::AddRelation(const BMessage* message, BMessage* reply)
{
    status_t  status;
    entry_ref sourceRef;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef))  != B_OK) {
        return status;
    }

    BString relationType;
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &relationType))  != B_OK) {
        return status;
    }
    const char* relation = relationType.String();

    entry_ref targetRef;
    if ((status = GetMessageParameter(message, SEN_RELATION_TARGET_REF, NULL, &targetRef))  != B_OK) {
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
    properties.RemoveData(SEN_RELATION_SOURCE_REF);
    properties.RemoveData(SEN_RELATION_TARGET_REF);
    properties.RemoveData(SEN_RELATION_TYPE);
    properties.RemoveData(SEN_RELATION_NAME);
    properties.RemoveData(SEN_ID_TO_REF_MAP);

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
    }
    // add new relation properties for target
    relations.AddMessage(targetId, &properties);

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

    // only now that all is clean, write relation to disk
    status = AddRelationTargetIdAttr(node, targetId, relationType);
    if (status != B_OK) {
        ERROR("failed to store targetId %s in file attrs of %s: %s\n", targetId, sourceRef.name, strerror(status));
        return status;
    }

    // write complete relation config into target attribute
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

        return result;
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

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef)) != B_OK) {
        return status;
    }

    bool withProperties = message->GetBool("properties");

    BStringList relationNames;
    status = ReadRelationNames(&sourceRef, &relationNames);
    if (relationNames.IsEmpty()) {
        return status;
    }

    if (withProperties) {
        // add all properties of all relations found above and add to result per type for lookup
        for (int i = 0; i < relationNames.CountStrings(); i++) {
            BString relation = relationNames.StringAt(i);
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
    reply->AddStrings(SEN_RELATIONS, relationNames);

    // ensure source has a SEN:ID if it has relations
    // todo: is this needed? would be quite an inconsistency...
    if (status == B_OK && relationNames.CountStrings() > 0) {
        char senId[SEN_ID_LEN];
        status = GetOrCreateId(&sourceRef, senId);
        reply->AddString(SEN_ID_ATTR, (new BString(senId))->String());
    }

    reply->AddString("status", BString("got ")
        << relationNames.CountStrings() << " relation(s) from " << sourceRef.name);

    return status;
}

status_t RelationsHandler::GetCompatibleRelations(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef)) != B_OK) {
        return status;
    }

    BNodeInfo nodeInfo(new BNode(&sourceRef));
    status = nodeInfo.InitCheck();
    if (status != B_OK) {
        ERROR("could not resolve entryRef '%s': %s\n", sourceRef.name, strerror(status));
        return status;
    }

    char mimeType[B_ATTR_NAME_LENGTH];
    nodeInfo.GetType(mimeType);
    LOG("searching for relations compatible with %s...\n", mimeType);

    BMessage relationTypes;
    BMimeType::GetInstalledTypes(SEN_RELATION_SUPERTYPE, &relationTypes);

    LOG("found relations:\n");
    relationTypes.PrintToStream();

    BStringList types;
    relationTypes.FindStrings("types", &types);

    // todo: filter out relations that exclude this type
    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings(SEN_RELATIONS, types);
    reply->AddString("status", BString("got ")
                    << types.CountStrings() << " relation(s) from " << sourceRef.name);

    return status;
}

status_t RelationsHandler::GetCompatibleTargetTypes(const BMessage* message, BMessage* reply)
{
    BString  sourceType;
    status_t status;

    // TODO: check relation config for type restrictions
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &sourceType, NULL, true, false)) != B_OK) {
        ERROR("required parameter %s is missing, aborting.\n", SEN_RELATION_TYPE);
        return status;
    }

    LOG("searching for types compatible with relation %s...\n", sourceType.String());
    BMessage targetTypes;

    // associations are meta relations and handled slightly differently, here we always take the meta/ types only
    if ((sourceType == SEN_LABEL_RELATION_TYPE) || (sourceType.StartsWith(SEN_META_SUPERTYPE "/")) ) {
        LOG("resolving meta types for association...\n");

        status = BMimeType::GetInstalledTypes(SEN_META_SUPERTYPE, &targetTypes);

        if (status != B_OK) {
            ERROR("error getting installed types from MIME db, falling back to any type: %s\n",
                strerror(status));
        }
    } else {
        LOG("using available template types allowed by relation.\n");
    }
    BStringList types;
    targetTypes.FindStrings("types", &types);

    // todo: filter out targets excluded by relation type

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddString("filter", "compatible");
    reply->AddStrings(SEN_RELATION_COMPATIBLE_TYPES, types);
    reply->AddString("status", BString("got ") << types.CountStrings()
                 << " compatible target(s) for " << sourceType.String());

    return status;
}

status_t RelationsHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    // todo: move to refs for input and ouput
    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef))  != B_OK) {
        return status;
    }

    BString relationType;
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &relationType))  != B_OK) {
        return status;
    }

    // check config if relation is bidirectional
    BMessage relationConf;
    status = GetRelationMimeConfig(relationType.String(), &relationConf);
    if (status != B_OK) {
        LOG("failed to get relation config for type %s: %s\n",
            relationType.String(), strerror(status));
        return status;
    }

    BMessage relations;
    status = ReadRelationsOfType(&sourceRef, relationType.String(), &relations);
    int32 numberOfRelations = relations.GetInt32("count", 0);

    if (status == B_OK) {
        if ( (numberOfRelations == 0) && (! relationConf.GetBool(SEN_RELATION_IS_BIDIR, true)) ) {
            // if we get no result, we may be at the other end of a unary relation, then we need to fetch in reverse
            status = ResolveInverseRelations(&sourceRef, &relations, relationType.String());
        }
    }
    if (status != B_OK) {
        BString error("failed to retrieve relations of type ");
                error << relationType;
        reply->AddString("error", error.String());
        reply->AddString("cause", strerror(status));

        return status;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage(SEN_RELATIONS, new BMessage(relations));
    reply->AddString("status", BString("retrieved ") << numberOfRelations
                 << " relations from " << sourceRef.name);

    reply->PrintToStream();

    return B_OK;
}

//
// private methods
//

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

    BMessage relationProperties;
    relationProperties.Unflatten(relation_attr_value);

    BStringList targetIds;
    result = ResolveRelationPropertyTargetIds(&relationProperties, &targetIds);

    if (result == B_OK) {
        const char* ids = targetIds.Join(",").String();
        LOG("got ids: %s\n", ids);
    } else {
        ERROR("failed to resolve relation target IDs for relation %s of file %s: %s\n",
            relationType, sourceRef->name, strerror(result));

        return status;
    }

    // add target refs
    BMessage idToRefMap;
    if ((status = ResolveRelationTargets(&targetIds, &idToRefMap)) == B_OK) {
        LOG("got %d unique relation targets for type %s and file %s, resolving entries...\n",
            idToRefMap.CountNames(B_REF_TYPE), relationType, sourceRef->name);

        // add relation count for quick check
        relations->AddInt32("count", targetIds.CountStrings());

        // add target IDs for quick lookup
        relations->AddStrings(SEN_TO_ATTR, targetIds);

        // add refs associated with targetIds
        relations->AddMessage(SEN_ID_TO_REF_MAP, new BMessage(idToRefMap));

        // add properties associated with a given targetId (nested messages for each relation to that target)
        relations->AddMessage("properties", new BMessage(relationProperties));
    } else {
        ERROR("failed to resolve relation target refs for relation %s of file %s.\n", relationType, sourceRef->name);
        return status;
    }

    return status;
}

status_t RelationsHandler::RemoveRelation(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
  status_t  status;

    BString sourceParam;
    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef))  != B_OK) {
        return status;
    }

    BString relationType;
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &relationType))  != B_OK) {
        return status;
    }
  const char* relation = relationType.String();

  // todo: implement!

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddString("status", BString("removed relation ") << relation << " from " << &sourceRef.name);

    return B_OK;
}

status_t RelationsHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
  status_t  status;

    if (status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef)  != B_OK) {
        return status;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddString("status", BString("removed all relations from ") << &sourceRef.name);

    return B_OK;
}

/*
 * private methods
 */

status_t RelationsHandler::ReadRelationNames(const entry_ref* ref, BStringList* relations)
{
    BNode node(ref);
  status_t result;

    if ((result = node.InitCheck()) != B_OK) {
        ERROR("failed to read from %s\n", ref->name);
        return result;
    }

    char attrName[B_ATTR_NAME_LENGTH];
    BString relationAttr;

    while (node.GetNextAttrName(attrName) == B_OK) {
        relationAttr = attrName;
        if (relationAttr.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
            // relation name is supertype + attribute name without SEN prefixes
            relations->Add(BString(SEN_RELATION_SUPERTYPE "/")
                           .Append(relationAttr.Remove(0, SEN_RELATION_ATTR_PREFIX_LEN).String()));
        }
    }

    return result;
}

status_t RelationsHandler::ResolveRelationPropertyTargetIds(const BMessage* relationProperties, BStringList* ids)
{
    char*       idKey;
    type_code   typeCode;
    int32       propCount;
    status_t    result = B_OK;

    LOG(("extracting targetIds from relation properties:\n"));
    relationProperties->PrintToStream();

    for (int i = 0; i < relationProperties->CountNames(B_MESSAGE_TYPE); i++){
        result = relationProperties->GetInfo(B_MESSAGE_TYPE, i, &idKey, &typeCode, &propCount);
        if (result == B_OK && typeCode == B_MESSAGE_TYPE) {
            ids->Add(BString(idKey));
        }
    }

    return result;
}

status_t RelationsHandler::ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs)
{
    LOG("resolving ids from list with %d targets...\n", ids->CountStrings())

    entry_ref ref;
    status_t status;
    for (int i = 0; i < ids->CountStrings(); i++) {
        BString senId = ids->StringAt(i);
        if ((status = QueryForUniqueSenId(senId.String(), &ref)) == B_OK) {
            idsToRefs->AddRef(senId, new entry_ref(ref.device, ref.directory, ref.name));
        } else {
            if (status == B_ENTRY_NOT_FOUND) {
                LOG("ignoring stale target reference with ID %s.\n", senId.String());
                continue;
            } else {
                return B_ERROR;
            }
        }
    }

    return B_OK;
}

status_t RelationsHandler::ResolveInverseRelations(const entry_ref* sourceRef, BMessage* reply, const char* relationType)
{
    char sourceId[SEN_ID_LEN];
    // query for any file with a SEN:TO that contains the sourceId
    BMessage idToRef;

    status_t status = GetOrCreateId(sourceRef, sourceId, true);
    if (status == B_OK)
        status = QueryForTargetsById(sourceId, &idToRef);

    if (status != B_OK) {
        ERROR("failed to get inverse relation targets for sourceId %s: %s\n", sourceId, strerror(status));
    }
    LOG("got inverse id->target map:\n");
    idToRef.PrintToStream();

    // todo: implement filtering for relationType if there are multiple inverse relations to the source

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage(SEN_ID_TO_REF_MAP, &idToRef);
    reply->AddString("status", BString("got ") << idToRef.CountNames(B_REF_TYPE)
                  << " inverse target(s) for " << sourceId);

    return status;
}

/** adds new targetId to existing IDs stored in SEN:TO / SEN:META for quick search,
    and then again as targetId keys for associated relation properties).
 */
status_t RelationsHandler::AddRelationTargetIdAttr(BNode& node, const char* targetId, const BString& relationType)
{
    BString targetIds;
    status_t status = node.ReadAttrString(SEN_TO_ATTR, &targetIds);
    if (targetIds.FindFirst(targetId) < 0) {
        if (! targetIds.IsEmpty())
            targetIds.Append(",");
        targetIds.Append(targetId);
    }

    return node.WriteAttrString(SEN_TO_ATTR, &targetIds);

}

//
// utility functions
//
status_t RelationsHandler::GetMessageParameter(
    const BMessage* message,
    const char* param,
    BString* buffer,
    entry_ref* ref,     // todo: make this a BEntryList or a vector<entry_ref*>
    bool mandatory,
    bool stripSuperType)
{
    status_t status;

    // first check the value for mandatory parameters exist
    const void *data;
    ssize_t     size;
    type_code   type;
    int32       count;

    // then parse parameter
    status = message->GetInfo(param,    &type, &count);

    if (status == B_OK)
        status = message->FindData(param, type, &data, &size);

    // possibly support int32 later

    if (mandatory && status != B_OK) {
        BString error;
        if (status != B_NAME_NOT_FOUND)
            error << "could not read message parameter " << param << ": " << strerror(status);
        else
            error << "missing required parameter " << param;

        ERROR("%s\n", error.String());

        return status;
    }

    switch (type) {
        case B_STRING_TYPE: {
            status = message->FindString(param, buffer);
            if (status == B_OK && stripSuperType/* && BString(param) == SEN_RELATION_TYPE*/) {
                // remove Relation supertype for relation params for internal handling
                buffer = StripSuperType(buffer);
            }
            break;
        }
        case B_REF_TYPE:
            status = message->FindRef(param, ref);
            break;
        default: {
            status = B_NOT_SUPPORTED;
        }
    }

    if (status != B_OK) {
        *buffer << "failed to parse parameter " << param << ": " << strerror(status);
        ERROR("failed to get parameter %s: %s\n", param, buffer->String());
    }

    return status;
}

BString* RelationsHandler::StripSuperType(BString* mimeType) {
    if (mimeType->StartsWith(SEN_RELATION_SUPERTYPE "/")) {
        mimeType->Remove(0, sizeof(SEN_RELATION_SUPERTYPE));
    }
    return mimeType;
}

//
// ID handling
//
const char* RelationsHandler::GenerateId() {
    return (new std::string(std::to_string(tsidGenerator->generate())) )->c_str();
}

/**
 * retrieve existing SEN:ID from entry, or generate a new one if not existing.
 */
status_t RelationsHandler::GetOrCreateId(const entry_ref *ref, char* id, bool createIfMissing)
{
    status_t result;
    BNode node(ref);

    // make sure to always initialize target ID so it is empty in case of error
    *id = '\0';

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

status_t RelationsHandler::QueryForUniqueSenId(const char* sourceId, entry_ref* refFound)
{
    BString predicate(BString(SEN_ID_ATTR) << "==" << sourceId);
    // TODO: all relation queries currently assume we never leave the boot volume
    BVolumeRoster volRoster;
    BVolume bootVolume;
    volRoster.GetBootVolume(&bootVolume);

    BQuery query;
    query.SetVolume(&bootVolume);
    query.SetPredicate(predicate.String());

    status_t result;
    if ((result = query.Fetch()) != B_OK) {
        ERROR("could not execute query for %s == %s: %s\n", SEN_ID_ATTR, sourceId, strerror(result));
        return result;
    }

    if ((result = query.GetNextRef(refFound)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            LOG("no matching file found for ID %s\n", sourceId);
        } else {
            // something other went wrong
            ERROR("error resolving id %s: %s\n", sourceId, strerror(result));
        }
        return result;
    }

    entry_ref ref;
    if (query.GetNextRef(&ref) == B_OK) {
        // this should never happen as the SEN:ID MUST be unique!
        ERROR("Critical error SEN:ID %s is NOT unique!\n", sourceId);
        return B_DUPLICATE_REPLY;
    }
    LOG("found entry %s\n", refFound->name);
    query.Clear();

    return B_OK;
}

// used to resolve inverse relations where we need to go from target->source
// todo: offer a live query (passing around a dest messenger) when querying large number of targets,
//       e.g. for reverse relations with Classification entities!
status_t RelationsHandler::QueryForTargetsById(const char* sourceId, BMessage* idToRef)
{
    status_t result;
    LOG("query for inverse relation targets with sourceId %s\n", sourceId);

    // query for files with a SEN:TO attr containing our sourceId
    BString predicate(BString(SEN_TO_ATTR) << "== '*" << sourceId << "*'");
    // TODO: all relation queries currently assume we never leave the boot volume
    BVolumeRoster volRoster;
    BVolume bootVolume;
    volRoster.GetBootVolume(&bootVolume);

    BQuery query;
    query.SetVolume(&bootVolume);
    query.SetPredicate(predicate.String());

    if ((result = query.Fetch()) != B_OK) {
        ERROR("could not execute query for %s == %s: %s\n", SEN_TO_ATTR, sourceId, strerror(result));
        return result;
    }

    entry_ref refFound;
    while (result == B_OK) {
        result = query.GetNextRef(&refFound);
        if (result == B_OK) {
            char senId[SEN_ID_LEN];
            result = GetOrCreateId(&refFound, senId);
            if (result == B_OK) {
                idToRef->AddRef(senId, new entry_ref(refFound));
            } else {
                // unexpected error, abort
                ERROR("error resolving SEN:ID for entry %s, aborting: %s\n",
                    refFound.name, strerror(result));
                return result;
            }
        }
    }
    // done, check result
    if (result == B_ENTRY_NOT_FOUND) {  // expected
        return B_OK;
    } else {
        // something other went wrong
        ERROR("error resolving id %s: %s\n", sourceId, strerror(result));
        return result;
    }
}

//
// Relation helpers
//
const char* RelationsHandler::GetAttributeNameForRelation(const char* relationType)
{
    BString relationAttributeName(relationType);
    if (! relationAttributeName.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
        return relationAttributeName.Prepend(SEN_RELATION_ATTR_PREFIX).String();
    }
    return relationAttributeName.String();
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
