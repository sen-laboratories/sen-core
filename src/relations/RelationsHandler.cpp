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
                } else {
                    LOG("resolving compatible relations...\n");
                    result = GetCompatibleRelations(message, reply);
                }
            }
            if (status != B_OK) {
                BString error("failed to resolve compatible relations for type '");
                        error << relationType << "'";
                reply->AddString("error", error.String());
            }
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

    if (result == B_OK) {
        LOG("RelationsHandler sending successful reply '%s' with message:\n", strerror(result));
    } else {
        ERROR("RelationsHandler encountered an error while processing the request: %s\n", strerror(result));
    }

    reply->AddInt32 ("status", result);
    reply->AddString("result", strerror(result));
    reply->PrintToStream();

    message->SendReply(reply);
}

status_t RelationsHandler::AddRelation(const BMessage* message, BMessage* reply)
{
    status_t  status;
    entry_ref srcRef;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &srcRef))  != B_OK) {
        return status;
    }

    BString relationTypeStr;
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &relationTypeStr))  != B_OK) {
        return status;
    }
    const char* relationType = relationTypeStr.String();

    entry_ref targetRef;
    if ((status = GetMessageParameter(message, SEN_RELATION_TARGET_REF, NULL, &targetRef))  != B_OK) {
        return status;
    }

    // get relation config
    BMessage relationConf;
    status = GetRelationMimeConfig(relationType, &relationConf);
    if (status != B_OK) {
        LOG("failed to get relation config for type %s: %s\n", relationType, strerror(status));

        BString error("failed to get relation config for type '");
                error << relationType << "'";

        reply->AddString("error", error.String());

        return status;  // bail out
    }

    LOG("got relation config:\n");
    relationConf.PrintToStream();

    // special case for relations from normal to meta entities (used for classification): here we don't link back
    // as to not overload the SEN:TO targetId attribute. The targets are then resolved via back-Query.
    // exception: relations between meta entities only, e.g. Concept hierarchies: here we allow bidirectional linking.
    bool linkToTarget = true;

    // relations are bidirectional by default (makes sense in 95% of cases)
    if (! relationConf.GetBool(SEN_RELATION_IS_BIDIR, true)) {
        LOG("relation is unidirectional, checking for meta types...\n");
        BString srcType;
        status = GetTypeForRef(&srcRef, &srcType);
        if (status != B_OK) {
            return status;
        }

        if (srcType.StartsWith(SEN_META_SUPERTYPE)) {
            // allow back linking *between* meta entities to form classification networks (nerd mode)
            BString targetType;
            status = GetTypeForRef(&targetRef, &targetType);
            if (status != B_OK) {
                return status;
            }

            if (! targetType.StartsWith(SEN_META_SUPERTYPE)) {
                linkToTarget = false;
                LOG("source is META entity but target is NOT, storing relation info without linking back to targets.\n");
            }
        }
    } else {
        LOG("relation is bidirectional.\n");
    }

    // prepare new relation properties with properties from message received
    BMessage properties(*message);

    // remove internal SEN properties
    properties.RemoveData(SEN_RELATION_SOURCE_REF);
    properties.RemoveData(SEN_RELATION_TARGET_REF);
    properties.RemoveData(SEN_RELATION_TYPE);
    properties.RemoveData(SEN_RELATION_NAME);
    properties.RemoveData(SEN_ID_TO_REF_MAP);

    // will hold all outgoing relations (for bidirectional / meta-non-meta relations) and their properties, or
    // just an empty placeholder in case of unidirectional / Association relations (META)
    BMessage relations;

    if (linkToTarget) {
        LOG("adding relation with link to target...\n");

        // get existing relations of the given type from the source file
        status = ReadRelationsOfType(&srcRef, relationType, &relations);
        if (status != B_OK) {
            ERROR("failed to read relations of type %s from file %s\n", relationType, srcRef.name);
            return B_ERROR;
        } else if (relations.IsEmpty()) {
            LOG("creating new relation %s for file %s\n", relationType, srcRef.name);
        } else {
            LOG("got relations for type %s and file %s:\n", relationType, srcRef.name);
            relations.PrintToStream();
        }

        // prepare target
        char targetId[SEN_ID_LEN];
        status = GetOrCreateId(&targetRef, targetId, true);
        if (status != B_OK) {
            return status;
        }

        // we allow multipe relations of the same type to the same target
        // (e.g. a note for the same text referencing different locations in the referenced text).
        // Hence, we have a Message with targetId as *key* pointing to 1-N messages with relation properties
        // for that target.
        // We need to check if a src->target relation with the same properties already exists and only
        // add a new mapping when no existing targetId->property message has been found.
        BMessage oldProperties;
        int index = 0;

        while (relations.FindMessage(targetId, index, &oldProperties) == B_OK) {
            LOG("got existing relation properties at index %d:\n", index);
            oldProperties.PrintToStream();

            // get existing relation properties for target if available and skip if already there
            if (oldProperties.HasSameData(properties)) {
                LOG("skipping add relation %s for target %s, already there with same properties:\n",
                        relationType, targetId);
                oldProperties.PrintToStream();

                reply->what = SEN_RESULT_RELATIONS;
                reply->AddString("status", BString("relation with same properties already exists"));

                return B_OK;    // done
            }
            index++;
        }
        if (index > 0) {
            LOG("adding new properties for existing relation %s at index %d\n", relationType, index);
        } else {
            LOG("adding new target %s for relation %s\n", targetId, relationType);
        }
        // add new relation properties for target
        relations.AddMessage(targetId, &properties);

        LOG("new relation properties for relation %s to target %s [%s]:\n", relationType, targetRef.name, targetId);
        relations.PrintToStream();

        status = WriteRelation(&srcRef, targetId, relationType, &relations, linkToTarget);
        if (status == B_OK) {
            LOG("created relation %s from source %s to target %s [%s] with properties:\n",
                    relationType, srcRef.name, targetRef.name, targetId);

            reply->AddString("detail", BString("created relation '") << relationType << "' from "
                << srcRef.name << " -> " <<  targetRef.name << " [" << targetId << "]");
        } else {
            reply->AddString("detail", BString("failed to create relation '") << relationType << "' from "
                << srcRef.name << " -> " <<  targetRef.name << " [" << targetId << "]");
        }
    } else { // if linkToTargets
        LOG("adding shallow relation with source-only config...\n");

        status = WriteRelation(&srcRef, NULL, relationType, &relations, linkToTarget);
        if (status == B_OK) {
            LOG("created relation %s from source %s to target ID %s with properties:\n",
                    relationType, srcRef.name, targetRef.name);

            reply->AddString("detail", BString("created shallow relation '") << relationType << "' from "
                << srcRef.name << " -> " << targetRef.name);
        } else {
            reply->AddString("detail", BString("failed to create relation '") << relationType << "' from "
                << srcRef.name << " -> " <<  targetRef.name);
        }
    }

    return status;
}

status_t RelationsHandler::WriteRelation(const entry_ref *srcRef,  const char* targetId,
                                         const char *relationType, const BMessage* properties, bool linkToTarget)
{
    // sanity check arguments
    if (linkToTarget && targetId == NULL) {
        ERROR("WriteRelation expects a targetId for linking to target.\n");
        return B_BAD_VALUE;
    }

    char srcId[SEN_ID_LEN];
    status_t status = GetOrCreateId(srcRef, srcId, true);
    if (status != B_OK) {
        return status;
    }

    // write new relation to designated attribute
    const char* attrName = GetAttributeNameForRelation(relationType);
    LOG("writing new relation %s into attribute %s of file %s\n", relationType, attrName, srcRef->name);

    BNode node(srcRef); // has been checked already at least once here

    ssize_t msgSize = properties->FlattenedSize();
    char msgBuffer[msgSize];
    status_t flatten_status = properties->Flatten(msgBuffer, msgSize);

    if (flatten_status != B_OK) {
        ERROR("failed to store relation properties for relation %s in file %s\n", relationType, srcRef->name);
        return flatten_status;
    }

    // only now that all is clean, write relation to disk
    if (linkToTarget) {
        LOG("adding relation target attr...\n");
        status = AddRelationTargetIdAttr(node, targetId, relationType);
        if (status != B_OK) {
            ERROR("failed to store targetId %s in file attrs of %s: %s\n", targetId, srcRef->name, strerror(status));
            return status;
        }
    }

    // write complete relation config into target attribute under the SEN-ified relation type name
    ssize_t result = node.WriteAttr(
        attrName,
        B_MESSAGE_TYPE,
        0,
        msgBuffer,
        msgSize);

    if (result <= 0) {
        ERROR("failed to store relation %s for file %s: %s\n", relationType, srcRef->name, strerror(result));
        return result;
    }

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
    reply->AddString(SEN_MSG_FILTER, "compatible");
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

    // strip possible relation supertype
    if (! relationAttributeName.StartsWith(SEN_RELATION_SUPERTYPE)) {
        relationAttributeName.RemoveFirst(SEN_RELATION_SUPERTYPE "/");
    }
    if (! relationAttributeName.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
        relationAttributeName.Prepend(SEN_RELATION_ATTR_PREFIX);
    }

    return (new BString(relationAttributeName))->String();
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

status_t RelationsHandler::GetTypeForRef(entry_ref* ref, BString* typeName)
{
    BNode srcNode(ref);
    status_t status = srcNode.InitCheck();
    if (status != B_OK) {
        ERROR("could not get source node for ref %s: %s\n", ref->name, strerror(status));
        return status;
    }

    BNodeInfo srcInfo(&srcNode);
    char srcType[B_MIME_TYPE_LENGTH];

    status = srcInfo.GetType(srcType);
    if (status != B_OK) {
        ERROR("could not get type info for ref %s: %s\n",
                ref->name, strerror(status));
        return status;
    }

    typeName->SetTo(srcType);
    return B_OK;
}
