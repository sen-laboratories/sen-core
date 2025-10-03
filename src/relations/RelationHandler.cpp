/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <cassert>
#include <fs_attr.h>
#include <FindDirectory.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <stdio.h>
#include <string>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include "RelationHandler.h"
#include "Sen.h"

RelationHandler::RelationHandler()
    : BHandler("SenRelationHandler")
{
    tsidGenerator = new IceDustGenerator();
}

RelationHandler::~RelationHandler()
{
}

void RelationHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage();
    status_t result = B_OK;

    LOG("RelationHandler got message:\n");
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
            result = message->FindString(SEN_RELATION_TYPE, &relationType);

            if (result == B_OK || result == B_NAME_NOT_FOUND) {     // e.g. for templates, search compatible relations
                if (relationType == SEN_ASSOC_RELATION_TYPE) {      //      in that case, relationType is empty
                    LOG("resolving association targets...\n");
                    result = GetCompatibleTargetTypes(relationType, reply);
                } else {
                    LOG("resolving compatible relations...\n");
                    relationType = "<any>";
                    result = GetCompatibleRelations(message, reply);
                }
            }
            if (result != B_OK) {
                BString error("failed to resolve compatible relations for type '");
                        error << relationType << "': " << strerror(result);
                reply->AddString("error", error.String());
            }
            break;
        }
        case SEN_RELATIONS_GET_COMPATIBLE_TYPES:
        {
            BString relationType;
            result = message->FindString(SEN_RELATION_TYPE, &relationType);
            if (result == B_OK)
                result = GetCompatibleTargetTypes(relationType, reply);
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
            LOG("RelationHandler: unkown message received: %u\n", message->what);
            reply->AddString("error", "cannot handle this message.");
        }
    }

    if (result == B_OK) {
        LOG("RelationHandler sending successful reply with message:\n");
    } else {
        ERROR("RelationHandler encountered an error while processing the request: %s\n", strerror(result));
    }

    reply->AddInt32 ("status", result);
    reply->AddString("result", strerror(result));
    reply->PrintToStream();

    message->SendReply(reply);
}

status_t RelationHandler::AddRelation(const BMessage* message, BMessage* reply)
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
    status = GetRelationConfig(relationType, &relationConf);
    if (status != B_OK) {
        LOG("failed to get relation config for type %s: %s\n", relationType, strerror(status));

        BString error("failed to get relation config for type '");
                error << relationType << "'";

        reply->AddString("error", error.String());

        return status;  // bail out
    }

    LOG("got relation config:\n");
    relationConf.PrintToStream();

    // special case for relations to classification entities (used for associations): here we don't link back
    // as to not overload the SEN:TO targetId attribute. The targets are then resolved via back-Query.
    // exception: relations between 2 association entities, e.g. Concept hierarchies: here we allow bidirectional linking.
    bool linkToTarget = true;

    // relations are bidirectional by default (makes sense in 95% of cases)
    if (! relationConf.GetBool(SEN_RELATION_IS_BIDIR, true)) {
        LOG("relation is unidirectional, checking for meta types...\n");
        BString srcType;
        status = GetTypeForRef(&srcRef, &srcType);
        if (status != B_OK) {
            return status;
        }

        if (srcType.StartsWith(SEN_CLASS_SUPERTYPE)) {
            // allow back linking *between* classification entities to form classification networks (aka nerd mode)
            BString targetType;
            status = GetTypeForRef(&targetRef, &targetType);
            if (status != B_OK) {
                return status;
            }

            if (! targetType.StartsWith(SEN_CLASS_SUPERTYPE)) {
                linkToTarget = false;
                LOG("source is META entity but target is NOT, storing relation info without linking back to targets.\n");
            }
        }
    } else {
        LOG("relation is bidirectional.\n");
    }

    // prepare new relation properties with properties from message received
    BMessage newProperties;
    // we take what we get but don't check as properties are optional
    message->FindMessage(SEN_RELATION_PROPERTIES, &newProperties);

    // check for existing properties with same key/values
    BMessage existingRelations;

    if (linkToTarget) {
        LOG("* adding relation %s with link to target...\n", relationType);

        // get existing relations of the given type from the source file
        status = ReadRelationsOfType(&srcRef, relationType, &existingRelations);
        if (status != B_OK) {
            ERROR("failed to read relations of type %s from file %s\n", relationType, srcRef.name);
            return B_ERROR;
        } else if (existingRelations.IsEmpty()) {
            LOG("creating new relation %s for file %s\n", relationType, srcRef.name);
        } else {
            LOG("adding new properties to existing relation %s and file %s.\n", relationType, srcRef.name);
        }

        // prepare target
        char targetId[SEN_ID_LEN];
        status = GetOrCreateId(&targetRef, targetId, true);
        if (status != B_OK) {
            return status;
        }

        // Note: we allow multipe relations of the same type to the same target
        // (e.g. a note for the same text referencing different locations in the referenced text).
        // Hence, we have a Message with targetId as *key* pointing to 1-N messages with relation properties
        // for that target.

        // We need to check if a src->target relation with the same properties already exists and only
        // add a new mapping when no existing targetId->property message has been found.
        BMessage existingProperties;
        int index = 0;

        while ((status = existingRelations.FindMessage(targetId, index, &existingProperties)) == B_OK) {
            // bail out if new properties for particular relation and target are the same as existing ones
            if (existingProperties.HasSameData(newProperties)) {
                LOG("skipping add relation %s for target %s with same properties:\n", relationType, targetId);
                existingProperties.PrintToStream();

                reply->what = SEN_RESULT_RELATIONS;
                reply->AddString("status", BString("relation with same properties already exists"));

                return B_OK;    // done
            }
            index++;
        }

        if (status != B_OK) {
            if (status != B_NAME_NOT_FOUND) {
                ERROR("error reading properties of existing relation %s from file %s: %s",
                    relationType, srcRef.name, strerror(status));
                return status;
            } else {
                index = -1; // no existing properties for target found, OK
            }
        }

        if (index >= 0) {
            LOG("  > adding new properties to existing relation %s and target %s at index %d\n",
                relationType, targetId, index);
        } else {
            LOG("  > creating new properties for target %s [%s] for relation %s\n", targetRef.name, targetId, relationType);
        }

        // add new relation properties for target to any existing relations
        existingRelations.AddMessage(targetId, &newProperties);
        existingRelations.PrintToStream();

        status = WriteRelation(&srcRef, targetId, relationType, &existingRelations);

        if (status == B_OK) {
            LOG("* created relation %s from source %s to target %s [%s].\n",
                    relationType, srcRef.name, targetRef.name, targetId);

            reply->AddString("detail", BString("created relation '") << relationType << "' from "
                << srcRef.name << " -> " <<  targetRef.name << " [" << targetId << "]");
        } else {
            reply->AddString("detail", BString("failed to create relation '") << relationType << "' from "
                << srcRef.name << " -> " <<  targetRef.name << " [" << targetId << "]");
        }

        // write inverse relation if it doesn't already exist
        LOG("  > checking for inverse relations of type %s...\n", relationType);

        BMessage inverseRelationsReply;
        status = ResolveInverseRelations(&targetRef, &inverseRelationsReply, relationType);

        if (status == B_OK) {
            // bail out if back link already exists
            BMessage inverseRelations;
            status = inverseRelationsReply.FindMessage(SEN_RELATIONS, &inverseRelations);
            if (! inverseRelations.IsEmpty()) {
                // done
                LOG("  > backlink already exists, skipping.\n");
                return status;
            }

            // now we need the ID of the original source for linking back to it
            char srcId[SEN_ID_LEN];
            status = GetOrCreateId(&srcRef, srcId, false);

            if (status == B_OK) {
                LOG("* linking back inverse relation from target %s [%s] -> source %s [%s].\n",
                    targetRef.name, targetId, srcRef.name, srcId);

                // get inverse relation properties (e.g. suitable label)
                BMessage inverseConfig;
                status = relationConf.FindMessage(SEN_RELATION_CONFIG_INVERSE, &inverseConfig);

                // todo: separate config from properties
                inverseRelations.AddMessage(srcId, &inverseConfig);

                if (status == B_OK || status == B_NAME_NOT_FOUND) {  // optional
                    // write inverse relations with swapped src/target
                    status = WriteRelation(&targetRef, srcId, relationType, &inverseRelations);
                }
             }

        }
    } else { // if linkToTarget
        LOG("adding shallow relation with source-only config...\n");

        // add empty relations message for consistency
        status = WriteRelation(&srcRef, NULL, relationType, &existingRelations);

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

status_t RelationHandler::WriteRelation(const entry_ref *srcRef,  const char* targetId,
                                        const char *relationType, const BMessage* properties)
{
    char srcId[SEN_ID_LEN];
    status_t status = GetOrCreateId(srcRef, srcId, true);
    if (status != B_OK) {
        return status;
    }

    // write new relation to designated attribute
    BString attrName;
    GetAttributeNameForRelation(relationType, &attrName);
    LOG("writing new relation '%s' from %s [%s] -> %s into attribute '%s'...\n",
        relationType, srcRef->name, srcId, targetId, attrName.String());

    BNode node(srcRef); // has been checked already at least once here

    ssize_t msgSize = properties->FlattenedSize();
    char msgBuffer[msgSize];
    status_t flatten_status = properties->Flatten(msgBuffer, msgSize);

    if (flatten_status != B_OK) {
        ERROR("failed to store relation properties for relation %s in file %s\n", relationType, srcRef->name);
        return flatten_status;
    }

    // only now that all is clean, write relation to disk
    if (targetId) {
        LOG("adding relation target attr with targetId %s...\n", targetId);
        status = AddRelationTargetIdAttr(node, targetId, relationType);

        if (status != B_OK) {
            ERROR("failed to store targetId %s in file attrs of %s: %s\n", targetId, srcRef->name, strerror(status));
            return status;
        }
    }

    // write complete relation config into target attribute with the canonical relation type name
    // Note: we also write relation config when not linking to a target, currently unused and empty.
    ssize_t result = node.WriteAttr(
        attrName.String(),
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

status_t RelationHandler::GetAllRelations(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef)) != B_OK) {
        return status;
    }

    bool withProperties = message->GetBool(SEN_MSG_PROPERTIES);
    bool withConfigs    = message->GetBool(SEN_MSG_CONFIGS, true);

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

    if (withConfigs) {
        // get relation configs and store keyed by type
        BMessage relationConfigs;
        status = GetRelationConfigs(&relationNames, &relationConfigs);
        if (status == B_OK) {
            reply->AddMessage(SEN_RELATION_CONFIG, &relationConfigs);
        }
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings(SEN_RELATIONS, relationNames);
    reply->AddInt32(SEN_MSG_COUNT, relationNames.CountStrings());

    reply->AddString("status", BString("got ")
        << relationNames.CountStrings() << " relation(s) from " << sourceRef.name);

    return status;
}

status_t RelationHandler::GetCompatibleRelations(const BMessage* message, BMessage* reply)
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

    char mimeType[B_MIME_TYPE_LENGTH];
    nodeInfo.GetType(mimeType);
    LOG("searching for relations compatible with %s...\n", mimeType);

    BMessage relationTypes;
    status = BMimeType::GetInstalledTypes(SEN_RELATION_SUPERTYPE, &relationTypes);
    if (status != B_OK) {
        ERROR("could not get installed MIME types: %s\n", strerror(status));
        return status;
    }

    LOG("found relations:\n");
    relationTypes.PrintToStream();

    BStringList types;
    relationTypes.FindStrings("types", &types);

    // optionally get relation configs
    bool withConfigs = message->GetBool(SEN_MSG_CONFIGS, true);

    if (withConfigs) {
        BMessage relationConfigs;
        status = GetRelationConfigs(&types, &relationConfigs);
        if (status == B_OK) {
            reply->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
        }
    }

    // todo: filter out relations that exclude this type
    reply->what = SEN_RESULT_RELATIONS;
    reply->AddStrings(SEN_RELATIONS, types);
    reply->AddString("status", BString("got ")
                    << types.CountStrings() << " relation(s) from " << sourceRef.name);

    return status;
}

status_t RelationHandler::GetCompatibleTargetTypes(const BString& relationType, BMessage* reply)
{
    LOG("searching for types compatible with relation %s...\n", relationType.String());
    BMessage targetTypes;
    status_t status;

    // associations are meta relations and handled slightly differently, here we always take the meta/ types only
    if ((relationType == SEN_ASSOC_RELATION_TYPE) || (relationType.StartsWith(SEN_CLASS_SUPERTYPE "/")) ) {
        LOG("resolving compatible association types...\n");

        status = BMimeType::GetInstalledTypes(SEN_CLASS_SUPERTYPE, &targetTypes);

        if (status != B_OK) {
            ERROR("error getting installed types from MIME db, falling back to any type: %s\n",
                strerror(status));
        }
    } else {
        LOG("using available template types allowed by relation.\n");
        // todo: filter out targets excluded by relation type
    }

    BStringList types;
    targetTypes.FindStrings("types", &types);

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddString(SEN_MSG_FILTER, "compatible");
    reply->AddStrings(SEN_RELATION_COMPATIBLE_TYPES, types);
    reply->AddString("status", BString("got ") << types.CountStrings()
                 << " compatible target(s) for " << relationType.String());

    return status;
}

status_t RelationHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, NULL, &sourceRef))  != B_OK) {
        return status;
    }

		BString relationTypeStr;
    if ((status = GetMessageParameter(message, SEN_RELATION_TYPE, &relationTypeStr))  != B_OK) {
        return status;
    }
    const char *relationType = relationTypeStr.String();

    // filled in id_to_ref map if it was passed in
    BMessage idToRefMap;
    bool returnIdToRefMap = message->GetBool(SEN_ID_TO_REF_MAP, false);

    // for single relations, config is mandatory as we need it below
    BMessage relationConfig;
    BStringList types;
    types.Add(relationType);

    // currently there will be only 1 type but to be consistent, we use the collection variant
    // also later, n-ary relations will need more than 1 config.
    status = GetRelationConfigs(&types, &relationConfig);
    if (status == B_OK) {
        reply->AddMessage(SEN_RELATION_CONFIG, &relationConfig);
    }

    BMessage relations;
    status = ReadRelationsOfType(&sourceRef, relationType,
                                 &relations, returnIdToRefMap ? &idToRefMap : NULL, NULL);
    int32 numberOfRelations = relations.CountNames(B_MESSAGE_TYPE);

    if (status == B_OK) {
        if ( (numberOfRelations == 0) && (! relationConfig.GetBool(SEN_RELATION_IS_BIDIR, true)) ) {
            // if we get no result, we may be at the other end of a unary relation, then we need to fetch in reverse
            status = ResolveInverseRelations(&sourceRef, &relations, relationType);
        }
    }
    if (status != B_OK) {
        BString error("failed to retrieve relations of type ");
                error << relationType;
        reply->AddString("cause", strerror(status));

        return status;
    }

    reply->what = SEN_RESULT_RELATIONS;
    reply->AddMessage(SEN_RELATIONS, &relations);

    // hand back filled in id_to_ref map if it was passed in
    if (returnIdToRefMap) {
        reply->AddMessage(SEN_ID_TO_REF_MAP, &idToRefMap);
    }

    reply->AddInt32("count", numberOfRelations);
    reply->AddString("status", BString("retrieved ") << numberOfRelations
                 << " relations from " << sourceRef.name);

    reply->PrintToStream();

    return B_OK;
}

//
// private methods
//

status_t RelationHandler::ReadRelationsOfType(
    const entry_ref* sourceRef,
    const char* relationType,
    BMessage* relations,
    BMessage* idToRefMap,
    BStringList* targetIds)
{
    BNode node(sourceRef);
    status_t status;

    if ((status = node.InitCheck()) != B_OK) {
        ERROR("failed to initialize node for ref %s: %s\n", sourceRef->name, strerror(status));
        return status;
    }

    // read relation config as message from respective relation attribute
    BString attrName;
    GetAttributeNameForRelation(relationType, &attrName);
    LOG("checking file '%s' for relation %s in atttribute %s\n", sourceRef->name, relationType, attrName.String());

    attr_info attrInfo;
    if ((status = node.GetAttrInfo(attrName.String(), &attrInfo)) != B_OK) {
        // if attribute not found, e.g. new relation, this is OK, else it's a real ERROR
        if (status != B_ENTRY_NOT_FOUND) {
            ERROR("failed to get attribute info for ref %s: %s\n", sourceRef->name, strerror(status));
            return status;
        }
        LOG("no existing relation of type %s found.\n", relationType);
        return B_OK;
    }

    // read relation properties message
    char* relation_attr_value = new char[attrInfo.size + 1];
    ssize_t result = node.ReadAttr(
            attrName.String(),
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

    // optionally add targetIds list
    if (targetIds != NULL) {
        status = ResolveRelationPropertyTargetIds(&relationProperties, targetIds);

        if (result == B_OK) {
            const char* ids = targetIds->Join(",").String();
            LOG("got ids: %s\n", ids);
        } else {
            ERROR("failed to resolve relation target IDs for relation %s of file %s: %s\n",
                relationType, sourceRef->name, strerror(result));

            return status;
        }
    }

    // optionally add target refs
    if (idToRefMap != NULL) {
        // targetIds might have not been requested but we need them here now
        if (targetIds != NULL) {
            status = ResolveRelationTargets(targetIds, idToRefMap);
        } else {
            BStringList tids;
            status = ResolveRelationPropertyTargetIds(&relationProperties, &tids);
            if (status == B_OK) {
                status = ResolveRelationTargets(&tids, idToRefMap);
            }
        }

        if (status == B_OK) {
            LOG("got %d unique relation targets for type %s and file %s, resolving entries...\n",
                idToRefMap->CountNames(B_REF_TYPE), relationType, sourceRef->name);
        } else {
            ERROR("failed to resolve relation target refs for relation %s of file %s.\n", relationType, sourceRef->name);
            return status;
        }
    }

    // add properties associated with a given targetId (nested messages for each relation to the same target)
    relations->Append(relationProperties);

    return status;
}

status_t RelationHandler::RemoveRelation(const BMessage* message, BMessage* reply)
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

status_t RelationHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
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

status_t RelationHandler::ReadRelationNames(const entry_ref* ref, BStringList* relations)
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
        // is it a SEN relation?
        if (relationAttr.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
            // add full SEN relation name (=supertype + attribute name) without the SEN:REL prefix
            relations->Add(BString(SEN_RELATION_SUPERTYPE "/")
                           .Append(
                                relationAttr.Remove(0, SEN_RELATION_ATTR_PREFIX_LEN)
                           )
                           .String());
        }
    }

    return result;
}

status_t RelationHandler::ResolveRelationPropertyTargetIds(const BMessage* relationProperties, BStringList* ids)
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

status_t RelationHandler::ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs)
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

status_t RelationHandler::ResolveInverseRelations(const entry_ref* sourceRef, BMessage* reply, const char* relationType)
{
    char sourceId[SEN_ID_LEN];
    BMessage idToRef;
    BMessage inverseRelations;

    status_t status = GetOrCreateId(sourceRef, sourceId, true);

    if (status != B_OK) {
        ERROR("failed to get inverse relation targets for sourceId %s: %s\n", sourceId, strerror(status));
        // not enough info for reply message, bail out
        return status;
    }

    // filter for optional relationType to narrow down result to specific relation type
    if (relationType != NULL) {
        status = ReadRelationsOfType(sourceRef, relationType, &inverseRelations, &idToRef);
        if (status == B_OK) {
            reply->AddMessage(SEN_RELATIONS, &inverseRelations);
        }
    } else {
        // get all inverse relations
        status = QueryForTargetsById(sourceId, &idToRef);
    }

    reply->what = SEN_RESULT_RELATIONS;
    // add resolved sourceId to speed up further relation calls
    reply->AddString(SEN_RELATION_SOURCE_ID, sourceId);
    reply->AddMessage(SEN_ID_TO_REF_MAP, &idToRef);
    reply->AddString("status", BString("got ") << idToRef.CountNames(B_REF_TYPE)
                  << " inverse target(s) for " << sourceId);

    LOG("sending reply for inverse relations for type %s::\n", relationType != NULL ? relationType : "ALL");
    reply->PrintToStream();

    return status;
}

// adds new targetId to existing IDs stored in SEN:TO for quick search and possible back linking.
status_t RelationHandler::AddRelationTargetIdAttr(BNode& node, const char* targetId, const BString& relationType)
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
status_t RelationHandler::GetMessageParameter(
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
            if (status == B_OK && stripSuperType) {
                // mainly used for relation params to use only subtype for further processing
                BString subtype;
                status = GetSubtype(buffer, &subtype);
                if (status == B_OK)
                    buffer->SetTo(subtype);
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

status_t RelationHandler::GetRelationConfigs(const BStringList* relations, BMessage* relationConfigs) {
    status_t status = B_OK;

    for (int i = 0; i < relations->CountStrings(); i++) {
        BString type = relations->StringAt(i);
        BMessage relationConf;

        status = GetRelationConfig(type.String(), &relationConf);

        LOG("got relation config for type %s:\n", type.String());
        relationConf.PrintToStream();

        if (status == B_OK) {
            status = relationConfigs->AddMessage(type.String(), &relationConf);
        } else {
            ERROR("failed to get relation config for type %s: %s\n", type.String(), strerror(status));
            continue;
        }
    }

    LOG("collected relation configs in msg:\n");
    relationConfigs->PrintToStream();

    return status;
}

status_t RelationHandler::GetRelationConfig(const char* mimeType, BMessage* relationConfig)
{
    BString relation(mimeType);

    if (!relation.StartsWith(SEN_RELATION_SUPERTYPE "/"))
        relation.Prepend(SEN_RELATION_SUPERTYPE "/");

    BMimeType relationType(relation);
    BMessage relationInfo;

    status_t result = relationType.InitCheck();
    if (result == B_OK) {
        // we need to get this from the MIME DB directly as it is not part of
        // the MimeType but stored as a custom attribute in the file system.
        BPath path;
        result = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
        if (result != B_OK) {
            ERROR("could not find user settings directory: %s\n", strerror(result));
            return result;
        }

        path.Append("mime_db");
        path.Append(mimeType);

        BNode mimeNode(path.Path());
        if (mimeNode.InitCheck() != B_OK) {
            ERROR( "error accessing MIME type file at '%s': %s\n", path.Path(), strerror(result));
            return result;
        }

        // FIXME: we need to take into account the default relation config from the supertype!
        //        BMessage::Append() will not overwrite existing properties but append them,
        //        but we need a real merge with overwriting config from super in subtypes!
        attr_info attrInfo;
        result = mimeNode.GetAttrInfo(SEN_RELATION_CONFIG_ATTR, &attrInfo);

        if (result != B_OK) {
            // this attribute is optional for relation subtypes, just add defaults
            if (result == B_ENTRY_NOT_FOUND) {
                LOG("no relation config found for type %s, using defaults.\n", mimeType);

                // quick hack to add defaults here, see above
                relationInfo.AddBool(SEN_RELATION_IS_BIDIR, true);
                relationInfo.AddBool(SEN_RELATION_IS_DYNAMIC, false);
                relationInfo.AddBool(SEN_RELATION_IS_SELF, false);

                result = B_OK;  // we fixed it:)
            } else {
                ERROR("could not get attrInfo for sen relation config for type %s: %s", mimeType, strerror(result));
                return result;
            }
        } else {
            // read config msg from fs attr
            char buffer[attrInfo.size];

            size_t sizeResult = mimeNode.ReadAttr(
                SEN_RELATION_CONFIG_ATTR, B_MESSAGE_TYPE, 0, buffer, attrInfo.size);

            if (sizeResult < attrInfo.size) {
                if (sizeResult < 0)
                    result = sizeResult;
                else
                    result = B_ERROR;

                ERROR("error reading SEN:CONFIG attribute from MIME type file '%s': %s\n", path.Path(), strerror(result));
                return result;
            }

            // materialize the flattened message
            result = relationInfo.Unflatten(buffer);
        }
    }

    if (result != B_OK) {
        ERROR("could not get relation config for type %s: %s\n", mimeType, strerror(result));
    }

    // get base attributes last (not to be overwritten by Unflatten above:)
    char shortName[B_MIME_TYPE_LENGTH];
    result = relationType.GetShortDescription(shortName);

    if (result != B_OK) {
        ERROR("could not get short name for MIME type %s, falling back to type name: %s\n", mimeType, strerror(result));
        return result;
    }

    relationInfo.AddString(SEN_RELATION_NAME, shortName);
    LOG("local relationInfo:\n");
    relationInfo.PrintToStream();

    relationConfig->Append(relationInfo);

    return result;
}

status_t RelationHandler::GetSubtype(const BString* mimeTypeStr, BString* subType) {
    BMimeType mimeType(mimeTypeStr->String());

    // MIME type will be invalid if only subtype is given, unless it is *only* a supertype (handled below)
    status_t status = mimeType.InitCheck();
    if (status == B_OK) {
        if (mimeType.IsSupertypeOnly()) {
            subType->SetTo("");     // only supertype, empty subtype
            return B_OK;
        }
        // else, extract subtype
        BMimeType superType;
        status = mimeType.GetSupertype(&superType);
        if (status == B_OK) {
            subType->SetTo(mimeType.Type());
            subType->RemoveFirst(superType.Type());
            subType->RemoveFirst("/");
        }
    } else {
        // check if we got a valid subtype or something is off
        BString testTypeStr("test/");
        testTypeStr.Prepend(mimeTypeStr->String());

        BMimeType testType(testTypeStr.String());
        status = testType.InitCheck();
        if (status == B_OK) {
            subType->SetTo(*mimeTypeStr);   // take valid subtype
        }
    }
    // error from above due to processing or we really just got a subtype, so no change needed
    return status;
}

//
// ID handling
//
const char* RelationHandler::GenerateId() {
    return (new std::string(std::to_string(tsidGenerator->generate())) )->c_str();
}

/**
 * retrieve existing SEN:ID from entry, or generate a new one if not existing.
 */
status_t RelationHandler::GetOrCreateId(const entry_ref *ref, char* id, bool createIfMissing)
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

status_t RelationHandler::QueryForUniqueSenId(const char* sourceId, entry_ref* refFound)
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
//       e.g. for inverse relations with Classification entities!
status_t RelationHandler::QueryForTargetsById(const char* sourceId, BMessage* idToRef)
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
void RelationHandler::GetAttributeNameForRelation(const char* relationType, BString* attrName)
{
    BString attrNameStr(relationType);

    // strip possible relation supertype
    if (attrNameStr.StartsWith(SEN_RELATION_SUPERTYPE "/")) {
        attrNameStr.RemoveFirst(SEN_RELATION_SUPERTYPE "/");
    }
    // add SEN:REL prefix if not there already
    if (! attrNameStr.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
        attrNameStr.Prepend(SEN_RELATION_ATTR_PREFIX);
    }

    *attrName = attrNameStr;
}

status_t RelationHandler::GetTypeForRef(entry_ref* ref, BString* typeName)
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
