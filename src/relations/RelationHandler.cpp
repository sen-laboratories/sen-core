/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2020-2026 SEN Labs e.U.
 */

#include <AppFileInfo.h>
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
#include "QueryUtil.h"
#include "RelationSets.h"
#include "../server/Reply.h"
#include <sen/Sen.h>
#include <spdlog/spdlog.h>

RelationHandler::RelationHandler()
    : BHandler("SenRelationHandler")
{
}

RelationHandler::~RelationHandler()
{
}

void RelationHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage(sen::cmd::kReplyRelations);
    status_t result = B_OK;

    spdlog::info("RelationHandler got message:");
    message->PrintToStream();

    // optionally get relation configs
    bool withConfigs = message->GetBool(sen::key::kWithConfigs);

    switch(message->what)
    {
        case sen::cmd::kRelationsGet:
        {
            result = GetRelationsOfType(message, reply);
            break;
        }
        case sen::cmd::kRelationsGetAll:
        {
            result = GetAllRelations(message, reply);
            break;
        }
        case sen::cmd::kRelationsGetSelf:
        {
            result = GetSelfRelationsOfType(message, reply);
            break;
        }
        case sen::cmd::kRelationsGetAllSelf:
        {
            result = GetSelfRelations(message, reply);
            break;
        }
        case sen::cmd::kRelationsGetCompatible:
        {
            // special case for associations, here we go straight to target types
            BString relationType;
            result = message->FindString(sen::key::kRelationType, &relationType);

            if (result == B_OK || result == B_NAME_NOT_FOUND) {     // e.g. for templates, search compatible relations
                if (relationType == sen::mime::kAssociationRelation) {      //      in that case, relationType is empty
                    spdlog::info("resolving compatible targets...");
                    result = GetCompatibleTargetTypes(relationType, withConfigs, reply);
                } else {
                    spdlog::info("resolving compatible relations...");
                    relationType = "<any>";
                    result = GetCompatibleRelations(message, reply);
                }
            }
            if (result != B_OK) {
                BString error("failed to resolve compatible relations for type '");
                        error << relationType << "': " << strerror(result);
                reply->AddString(sen::key::kDetail, error.String());
            }
            break;
        }
        case sen::cmd::kRelationsGetCompatibleTypes:    // used e.g. for "New Related" Templates
        {
            BString relationType;
            result = message->FindString(sen::key::kRelationType, &relationType);
            if (result == B_OK) {
                result = GetCompatibleTargetTypes(relationType, withConfigs, reply);
            }
            break;
        }
        case sen::cmd::kRelationAdd:
        {
            result = AddRelation(message, reply);
            break;
        }
        case sen::cmd::kRelationRemove:
        {
            result = RemoveRelation(message, reply);
            break;
        }
        case sen::cmd::kRelationUpdate:
        {
            result = UpdateRelation(message, reply);
            break;
        }
        case sen::cmd::kRelationsRemoveAll:
        {
            result = RemoveAllRelations(message, reply);
            break;
        }
        default:
        {
            spdlog::info("RelationHandler: unkown message received: {}", message->what);
            reply->AddString(sen::key::kDetail, "cannot handle this message.");
        }
    }

    if (result == B_OK) {
        spdlog::info("RelationHandler sending successful reply with message:");
    } else {
        spdlog::error("RelationHandler encountered an error while processing the request: {}", strerror(result));
    }

    sen::reply::Finish(reply, result);
    reply->PrintToStream();

    message->SendReply(reply);
}

status_t RelationHandler::GetAllRelations(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &sourceRef)) != B_OK) {
        return status;
    }

    bool withProperties = message->GetBool(sen::key::kWithProperties);
    bool withConfigs    = message->GetBool(sen::key::kWithConfigs, true);

    BStringList relationNames;
    status = ReadRelationNames(&sourceRef, &relationNames);
    if (relationNames.IsEmpty()) {
        return status;
    }

    if (withProperties) {
        // add all properties of all relations found above and add to result per type for lookup
        for (int i = 0; i < relationNames.CountStrings(); i++) {
            BString relation = relationNames.StringAt(i);
            spdlog::info("adding properties of relation {}...", relation.String());

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
            reply->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);
        }
    }

    reply->what = sen::cmd::kReplyRelations;
    reply->AddStrings(sen::key::kRelations, relationNames);
    reply->AddInt32(sen::key::kCount, relationNames.CountStrings());

    reply->AddString(sen::key::kDetail, BString("got ")
        << relationNames.CountStrings() << " relation(s) from " << sourceRef.name);

    return status;
}

/**
 * Whether a relation can be created at a file of this type. A relation that is resolved at run time (dynamic) is never created
 * by hand. A relation can name the types of files that it can start at (SEN:sourceTypes, without: any file) and the types that
 * it cannot start at (SEN:excludeSourceTypes, which wins). A type is a MIME type or the start of one, e.g. a supertype
 * ("audio"), like the filters for the templates of a new file (see TemplateUtils in Tracker).
 */
static bool IsCompatibleWithSource(const BMessage& relationConfig, const char* sourceType)
{
    if (relationConfig.GetBool(sen::conf::kDynamic, false))
        return false;

    auto matches = [&](const char* field, int32* count) {
        bool found = false;
        const char* pattern;
        *count = 0;
        for (int32 index = 0; relationConfig.FindString(field, index, &pattern) == B_OK; index++) {
            (*count)++;
            found = found || BString(sourceType).IStartsWith(pattern);
        }
        return found;
    };

    int32 includes, excludes;
    bool included = matches(sen::conf::kSourceTypes, &includes);
    bool excluded = matches(sen::conf::kExcludeSourceTypes, &excludes);

    return (includes == 0 || included) && !excluded;
}

status_t RelationHandler::GetCompatibleRelations(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &sourceRef)) != B_OK) {
        return status;
    }

    BNode sourceNode(&sourceRef);
    BNodeInfo nodeInfo(&sourceNode);
    status = nodeInfo.InitCheck();
    if (status != B_OK) {
        spdlog::error("could not resolve entryRef '{}': {}", sourceRef.name, strerror(status));
        return status;
    }

    char mimeType[B_MIME_TYPE_LENGTH];
    if (nodeInfo.GetType(mimeType) != B_OK || mimeType[0] == '\0') {
        // a file without a type: what the MIME database makes of its name and content
        BMimeType guessed;
        if (BMimeType::GuessMimeType(&sourceRef, &guessed) == B_OK)
            strlcpy(mimeType, guessed.Type(), sizeof(mimeType));
        else
            strlcpy(mimeType, "application/octet-stream", sizeof(mimeType));
    }
    spdlog::info("searching for relations compatible with {}...", mimeType);

    BMessage relationTypes;
    status = BMimeType::GetInstalledTypes(sen::mime::kRelationSupertype, &relationTypes);
    if (status != B_OK) {
        spdlog::error("could not get installed MIME types: {}", strerror(status));
        return status;
    }

    BStringList installed;
    relationTypes.FindStrings("types", &installed);  // as per MimeType API spec

    // the configs tell which relations fit this file (and are handed on if wanted)
    BMessage allConfigs;
    status_t configStatus = GetRelationConfigs(&installed, &allConfigs);
    if (configStatus != B_OK) {
        spdlog::error("could not get relation configs for compatible relations: {}", strerror(configStatus));
    }

    BStringList types;
    BMessage relationConfigs;
    for (int32 index = 0; index < installed.CountStrings(); index++) {
        const BString& type = installed.StringAt(index);
        BMessage config;
        // a relation without a config (not registered yet) has no restrictions
        if (allConfigs.FindMessage(type.String(), &config) == B_OK && !IsCompatibleWithSource(config, mimeType)) {
            spdlog::debug("relation {} is not compatible with {}.", type.String(), mimeType);
            continue;
        }
        types.Add(type);
        if (!config.IsEmpty())
            relationConfigs.AddMessage(type.String(), &config);
    }

    // optionally get relation configs
    if (message->GetBool(sen::key::kWithConfigs, true) && configStatus == B_OK)
        reply->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);

    reply->what = sen::cmd::kReplyRelations;
    reply->AddStrings(sen::key::kRelations, types);
    reply->AddString(sen::key::kDetail, BString("got ")
                    << types.CountStrings() << " relation(s) compatible with " << mimeType << " from " << sourceRef.name);

    return B_OK;
}

status_t RelationHandler::GetCompatibleTargetTypes(const BString& relationType, bool withConfigs, BMessage* reply)
{
    spdlog::info("searching for types compatible with relation {}...", relationType.String());
    BMessage targetTypes;
    status_t status;

    // associations are meta relations and handled slightly differently, here we always take the meta/ types only
    if ((relationType == sen::mime::kAssociationRelation) || (relationType.StartsWith(sen::mime::kClassificationPrefix)) ) {
        spdlog::info("resolving compatible association types...");

        status = BMimeType::GetInstalledTypes(sen::mime::kClassificationSupertype, &targetTypes);

        if (status != B_OK) {
            spdlog::error("error getting installed types from MIME db, falling back to any type: {}",
                strerror(status));
        }
    } else {
        spdlog::info("using available template types allowed by relation.");
        // todo: filter out targets excluded by relation type
    }

    BStringList types;
    targetTypes.FindStrings("types", &types);   // as per MimeType API spec

    if (withConfigs) {
        BMessage relationConfigs;
        status = GetRelationConfigs(&types, &relationConfigs);
        if (status == B_OK) {
            reply->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);
        } else {
            spdlog::error("could not get relation configs for compatible relations: {}", strerror(status));
        }
    }

    reply->what = sen::cmd::kReplyRelations;
    reply->AddString(sen::key::kFilter, sen::filter::kCompatible);
    reply->AddStrings(sen::key::kTargetType, types);
    reply->AddString(sen::key::kDetail, BString("got ") << types.CountStrings()
                 << " compatible target(s) for " << relationType.String());

    return status;
}

status_t RelationHandler::GetRelationsOfType(const BMessage* message, BMessage* reply)
{
    entry_ref sourceRef;
    status_t  status;

    if ((status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &sourceRef))  != B_OK) {
        return status;
    }

    BString relationTypeStr;
    if ((status = GetMessageParameter(message, sen::key::kRelationType, &relationTypeStr))  != B_OK) {
        return status;
    }
    const char *relationType = relationTypeStr.String();

    // filled in id_to_ref map if it was passed in
    BMessage idToRefMap;
    bool returnIdToRefMap = message->GetBool(sen::key::kIdToRefMap, false);

    BStringList types;
    types.Add(relationType);

    // contains configs for all compatible relations found
    BMessage relationConfigMap;
    // config for the requested relation
    BMessage relationConfig;

    // currently there will be only 1 type but to be consistent, we use the collection variant
    // also later, n-ary relations might need more than 1 config.
    status = GetRelationConfigs(&types, &relationConfigMap);
    if (status == B_OK) {
        reply->AddMessage(sen::key::kRelationConfigMap, &relationConfigMap);
        status = relationConfigMap.FindMessage(relationType, &relationConfig);
    }

    BMessage relations;
    // the targets are resolved always: a target that cannot be resolved is marked as missing (dangling relation)
    BMessage resolved;
    status = ReadRelationsOfType(&sourceRef, relationType, &relations, &resolved, NULL);
    if (status == B_OK) {
        char* targetName;
        type_code targetType;
        for (int32 i = 0; relations.GetInfo(B_MESSAGE_TYPE, i, &targetName, &targetType) == B_OK; i++) {
            if (resolved.HasRef(targetName))
                continue;
            int32 sets = sen::relation::CountSets(relations, targetName);
            for (int32 s = 0; s < sets; s++) {
                BMessage set;
                if (sen::relation::GetSet(relations, targetName, s, &set) == B_OK) {
                    set.RemoveName(sen::key::kTargetMissing);
                    set.AddBool(sen::key::kTargetMissing, true);
                    relations.ReplaceMessage(targetName, s, &set);
                }
            }
        }
        if (returnIdToRefMap)
            idToRefMap = resolved;
    }

    int32 numberOfRelations = relations.CountNames(B_MESSAGE_TYPE);

    if (status == B_OK) {
        // unidirectional relations (associations) are not stored with their target: what points to this file is found by query
        if (! relationConfig.GetBool(sen::conf::kBidirectional, true)) {
            status = ResolveReverseRelations(&sourceRef, relationType, relationConfig, &relations,
                                             returnIdToRefMap ? &idToRefMap : NULL);
        }
    }

    if (status != B_OK) {
        BString error("failed to retrieve relations of type ");
                error << relationType;
        reply->AddString(sen::key::kDetail, strerror(status));

        return status;
    }

    reply->what = sen::cmd::kReplyRelations;
    reply->AddMessage(sen::key::kRelations, &relations);

    // the ID of the source, so clients can name it without a second request
    char sourceId[sen::id::kLength];
    if (GetOrCreateId(&sourceRef, sourceId, false) == B_OK)
        reply->AddString(sen::key::kSourceId, sourceId);

    // hand back filled in id_to_ref map if it was passed in, with the names to show for the targets
    if (returnIdToRefMap) {
        reply->AddMessage(sen::key::kIdToRefMap, &idToRefMap);

        BMessage idToNameMap;
        char* targetId;
        type_code targetType;
        entry_ref targetRef;
        for (int32 i = 0; idToRefMap.GetInfo(B_REF_TYPE, i, &targetId, &targetType) == B_OK; i++) {
            if (idToRefMap.FindRef(targetId, &targetRef) == B_OK)
                idToNameMap.AddString(targetId, DisplayNameOf(targetRef));
        }
        reply->AddMessage(sen::key::kIdToNameMap, &idToNameMap);
    }

    reply->AddInt32("count", numberOfRelations);
    reply->AddString(sen::key::kDetail, BString("retrieved ") << numberOfRelations
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
        spdlog::error("failed to initialize node for ref {}: {}", sourceRef->name, strerror(status));
        return status;
    }

    // read relation config as message from respective relation attribute
    BString attrName;
    GetAttributeNameForRelation(relationType, &attrName);
    spdlog::info("checking file '{}' for relation {} in atttribute {}", sourceRef->name, relationType, attrName.String());

    attr_info attrInfo;
    if ((status = node.GetAttrInfo(attrName.String(), &attrInfo)) != B_OK) {
        // if attribute not found, e.g. new relation, this is OK, else it's a real ERROR
        if (status != B_ENTRY_NOT_FOUND) {
            spdlog::error("failed to get attribute info for ref {}: {}", sourceRef->name, strerror(status));
            return status;
        }
        spdlog::info("no existing relation of type {} found.", relationType);
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
        spdlog::info("no relations of type {} found for path {}.", relationType, sourceRef->name);
        return B_OK;
    } else if (result < 0) {    // result is an error code
        spdlog::error("failed to read relation {} of file {}: {}", relationType, sourceRef->name, strerror(result));
        return result;
    }

    BMessage relationProperties;
    relationProperties.Unflatten(relation_attr_value);

    // optionally add targetIds list
    if (targetIds != NULL) {
        status = ResolveRelationPropertyTargetIds(&relationProperties, targetIds);

        if (result == B_OK) {
            const char* ids = targetIds->Join(",").String();
            spdlog::info("got ids: {}", ids);
        } else {
            spdlog::error("failed to resolve relation target IDs for relation {} of file {}: {}",
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
            spdlog::info("got {} unique relation targets for type {} and file {}, resolving entries...",
                idToRefMap->CountNames(B_REF_TYPE), relationType, sourceRef->name);
        } else {
            spdlog::error("failed to resolve relation target refs for relation {} of file {}.", relationType, sourceRef->name);
            return status;
        }
    }

    // add properties associated with a given targetId (nested messages for each relation to the same target)
    relations->Append(relationProperties);

    return status;
}

/*
 * private methods
 */

status_t RelationHandler::ReadRelationNames(const entry_ref* ref, BStringList* relations)
{
    BNode node(ref);
    status_t result;

    if ((result = node.InitCheck()) != B_OK) {
        spdlog::error("failed to read from {}", ref->name);
        return result;
    }

    char attrName[B_ATTR_NAME_LENGTH];
    BString relationAttr;

    while (node.GetNextAttrName(attrName) == B_OK) {
        relationAttr = attrName;
        // is it a SEN relation?
        if (relationAttr.StartsWith(sen::attr::kRelationPrefix)) {
            // add full SEN relation name (=supertype + attribute name) without the SEN:REL prefix
            relations->Add(BString(sen::mime::kRelationPrefix)
                           .Append(
                                relationAttr.Remove(0, sen::attr::kRelationPrefixLength)
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

    spdlog::info("extracting targetIds from relation properties:");
    relationProperties->PrintToStream();

    for (int i = 0; i < relationProperties->CountNames(B_MESSAGE_TYPE); i++){
        result = relationProperties->GetInfo(B_MESSAGE_TYPE, i, &idKey, &typeCode, &propCount);
        if (result == B_OK && typeCode == B_MESSAGE_TYPE) {
            ids->Add(BString(idKey));
        }
    }

    return result;
}

/**
 * The name to show for a file in a menu or a folder of relations: its title (dc:title) where it has one; for the type of a
 * MIME database (a file there), the short description of the type, not the name of the type; else the name of the file.
 */
BString RelationHandler::DisplayNameOf(const entry_ref& ref)
{
    BNode node(&ref);
    BString name;
    if (node.InitCheck() == B_OK) {
        for (const char* attribute : {"dc:title", "META:S:DESC"}) {
            if (node.ReadAttrString(attribute, &name) == B_OK && !name.IsEmpty())
                return name;
        }
    }
    return BString(ref.name);
}

status_t RelationHandler::ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs)
{
    spdlog::info("resolving ids from list with {} targets...", ids->CountStrings());

    entry_ref ref;
    status_t status;
    for (int i = 0; i < ids->CountStrings(); i++) {
        BString senId = ids->StringAt(i);
        if ((status = QueryForUniqueSenId(senId.String(), &ref)) == B_OK) {
            idsToRefs->AddRef(senId, new entry_ref(ref.device, ref.directory, ref.name));
        } else {
            if (status == B_ENTRY_NOT_FOUND) {
                spdlog::info("ignoring stale target reference with ID {}.", senId.String());
                continue;
            } else {
                return B_ERROR;
            }
        }
    }

    return B_OK;
}

//
// utility functions
//
status_t RelationHandler::GetMessageParameter(
    const BMessage* message,
    const char* param,
    BString* buffer,
    entry_ref* ref,     // todo: make this a BEntryList or a vector<entry_ref*>
    bool mandatory)
{
    type_code type;
    int32     count;

    status_t status = message->GetInfo(param, &type, &count);
    if (status != B_OK) {
        if (mandatory) {
            if (status == B_NAME_NOT_FOUND)
                spdlog::error("missing required parameter {}", param);
            else
                spdlog::error("could not read message parameter {}: {}", param, strerror(status));
        }
        return status;
    }

    // possibly support int32 later
    switch (type) {
        case B_STRING_TYPE: {
            const char* value = NULL;
            status = message->FindString(param, &value);
            if (status != B_OK)
                break;

            if (buffer != NULL) {
                buffer->SetTo(value);
            } else if (ref != NULL) {
                // a path (e.g. from a script) where an entry_ref is wanted
                BEntry entry(value, true);
                status = entry.InitCheck();
                if (status == B_OK)
                    status = entry.GetRef(ref);
            } else {
                status = B_BAD_VALUE;
            }
            break;
        }
        case B_REF_TYPE: {
            if (ref == NULL) {
                status = B_BAD_VALUE;
                break;
            }
            status = message->FindRef(param, ref);
            if (status == B_OK) {
                // a symbolic link is the same object as its target: relations are found, shown and written the same way
                BEntry entry(ref, true);
                if (entry.InitCheck() == B_OK)
                    entry.GetRef(ref);
            }
            break;
        }
        default:
            status = B_NOT_SUPPORTED;
    }

    if (status != B_OK)
        spdlog::error("failed to get parameter {}: {}", param, strerror(status));

    return status;
}

status_t RelationHandler::GetRelationConfigs(const BStringList* relations, BMessage* relationConfigs) {
    status_t status = B_OK;

    for (int i = 0; i < relations->CountStrings(); i++) {
        BString relation = relations->StringAt(i);
        BMessage relationConf;

        status = GetRelationConfig(relation.String(), &relationConf);

        spdlog::info("got relation config for type {}:", relation.String());
        relationConf.PrintToStream();

        if (status == B_OK) {
            status = relationConfigs->AddMessage(relation.String(), &relationConf);
        } else {
            spdlog::error("failed to get relation config for type {}: {}", relation.String(), strerror(status));
            continue;
        }
    }

    spdlog::info("collected relation configs in msg:");
    relationConfigs->PrintToStream();

    return status;
}

status_t RelationHandler::GetRelationConfig(const char* mimeType, BMessage* relationConfig)
{
    BString relation(mimeType);
    BMimeType relationType(relation);
    BMessage relationInfo;

    status_t result = relationType.InitCheck();
    if (result == B_OK && ! relationType.IsValid()) {
        result = B_BAD_VALUE;
    }
    if (result == B_OK) {
        // we need to get this from the MIME DB directly as it is not part of
        // the MimeType but stored as a custom attribute in the file system.
        BPath path;
        result = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
        if (result != B_OK) {
            spdlog::error("could not find user settings directory: {}", strerror(result));
            return result;
        }

        path.Append("mime_db");
        path.Append(relation.String());

        BNode mimeNode(path.Path());
        status_t mimeNodeStatus = mimeNode.InitCheck();

        if (mimeNodeStatus != B_OK) {
            // relation subtype was never registered in the MIME DB - this
            // happens for any dynamically defined relation type (e.g. one
            // declared by a plugin's SEN:type_mapping) that has never been
            // used before. Register it now: this is also what makes the
            // type show up under BMimeType::GetInstalledTypes(sen::mime::kRelationSupertype, ...),
            // which GetCompatibleRelations() relies on to enumerate relation
            // types at all - without it, that call fails with B_NAME_NOT_FOUND
            // because the "relation" supertype itself was never created.
            status_t installStatus = relationType.Install();
            if (installStatus != B_OK) {
                spdlog::error("could not install relation type {} in MIME DB: {}",
                    mimeType, strerror(installStatus));
            } else {
                // also assign a preferred app so self relations can be opened:
                // TTracker::HandleSenMessage's self-relation branch looks up
                // the relation type's preferred app via BMimeType::GetPreferredApp()
                // to find the navigator to launch (see TrackerSen.cpp). Nothing
                // else ever sets this, even though navigator plugins already
                // declare the relation types they support via their own
                // file_types resource (e.g. SenTextNavigator declares support
                // for this exact type) - so without this, GetPreferredApp()
                // fails and the self-relation click silently aborts.
                status_t navStatus = AssignPreferredNavigator(&relationType);
                if (navStatus != B_OK) {
                    spdlog::info("no installed navigator plugin declares support for relation type {}: {}",
                        mimeType, strerror(navStatus));
                }
            }
            spdlog::info("no MIME type file found at '{}' ({}), using defaults for type {}.",
                path.Path(), strerror(mimeNodeStatus), mimeType);

            relationInfo.AddBool(sen::conf::kBidirectional, true);
            relationInfo.AddBool(sen::conf::kDynamic, false);
            relationInfo.AddBool(sen::conf::kSelf, false);
        } else {
            // FIXME: we need to take into account the default relation config from the supertype!
            //        BMessage::Append() will not overwrite existing properties but append them,
            //        but we need a real merge with overwriting config from super in subtypes!
            attr_info attrInfo;
            result = mimeNode.GetAttrInfo(sen::attr::kRelationConfig, &attrInfo);

            if (result != B_OK) {
                // this attribute is optional for relation subtypes, just add defaults
                if (result == B_ENTRY_NOT_FOUND) {
                    spdlog::info("no relation config found for type {}, using defaults.", mimeType);

                    // quick hack to add defaults here, see above
                    relationInfo.AddBool(sen::conf::kBidirectional, true);
                    relationInfo.AddBool(sen::conf::kDynamic, false);
                    relationInfo.AddBool(sen::conf::kSelf, false);

                    result = B_OK;  // we fixed it:)
                } else {
                    spdlog::error("could not get attrInfo for sen relation config for type {}: {}", mimeType, strerror(result));
                    return result;
                }
            } else {
                // read config msg from fs attr
                char buffer[attrInfo.size];

                size_t sizeResult = mimeNode.ReadAttr(
                    sen::attr::kRelationConfig, B_MESSAGE_TYPE, 0, buffer, attrInfo.size);

                if (sizeResult < attrInfo.size) {
                    if (sizeResult < 0)
                        result = sizeResult;
                    else
                        result = B_ERROR;

                    spdlog::error("error reading SEN:CONFIG attribute from MIME type file '{}': {}", path.Path(), strerror(result));
                    return result;
                }

                // materialize the flattened message
                result = relationInfo.Unflatten(buffer);
            }
        }
    }

    if (result != B_OK) {
        spdlog::error("could not get relation config for type {}: {}", mimeType, strerror(result));
    }

    // get base attributes last (not to be overwritten by Unflatten above:)
    char shortName[B_MIME_TYPE_LENGTH];
    result = relationType.GetShortDescription(shortName);

    if (result != B_OK) {
        // relation type has no short description installed in the MIME DB
        // (e.g. never registered via BMimeType::Install()) - fall back to
        // the type name itself rather than losing the config gathered above.
        spdlog::error("could not get short name for MIME type {}, falling back to type name: {}", mimeType, strerror(result));
        strlcpy(shortName, mimeType, sizeof(shortName));
        result = B_OK;
    }

    relationInfo.AddString(sen::key::kRelationName, shortName);
    spdlog::info("local relationInfo:");
    relationInfo.PrintToStream();

    relationConfig->Append(relationInfo);

    return result;
}

status_t RelationHandler::AssignPreferredNavigator(BMimeType* relationType)
{
    std::vector<entry_ref> plugins;
    status_t result = sen::FindPlugins(sensei::feature::kNavigate, &plugins);
    if (result != B_OK) {
        return result;
    }

    for (const entry_ref& pluginRef : plugins) {
        BFile pluginFile(&pluginRef, B_READ_ONLY);
        BAppFileInfo appInfo(&pluginFile);
        if (appInfo.InitCheck() != B_OK) {
            continue;
        }

        BMessage supportedTypes;
        if (appInfo.GetSupportedTypes(&supportedTypes) != B_OK) {
            continue;
        }

        BString type;
        for (int i = 0; supportedTypes.FindString("types", i, &type) == B_OK; i++) {
            if (type == relationType->Type()) {
                char signature[B_MIME_TYPE_LENGTH];
                if (appInfo.GetSignature(signature) == B_OK) {
                    spdlog::info("assigning {} as preferred app for relation type {} (navigator {})",
                        signature, relationType->Type(), pluginRef.name);
                    return relationType->SetPreferredApp(signature);
                }
            }
        }
    }

    return B_NAME_NOT_FOUND;
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
BString RelationHandler::GenerateId() {
    char text[sen::id::kLength];
    sen::id::New(text);
    return BString(text);
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
        spdlog::error("failed to initialize node for path {}: {}", ref->name, strerror(result));
        return result;
    }

    BString idStr;
    result = node.ReadAttrString(sen::attr::kId, &idStr);
    if (result == B_ENTRY_NOT_FOUND) {
        if (! createIfMissing) {
            return result;
        }
        strlcpy(id, GenerateId().String(), sen::id::kLength);

        if (*id != '\0') {
            spdlog::info("generated new ID {} for path {}", id, ref->name);
            if ((result = node.WriteAttrString(sen::attr::kId, new BString(id))) != B_OK) {
                spdlog::error("failed to create ID for path {}: {}", ref->name, strerror(result));
                return result;
            }
            return B_OK;
        } else {
            spdlog::error("failed to create ID for path {}", ref->name);
            return B_ERROR;
        }
    } else if (result != B_OK) {
        spdlog::error("failed to read ID from path {}: {}", ref->name, strerror(result));
        return result;
    } else {
        strncpy(id, idStr.String(), sen::id::kLength);
        spdlog::info("got existing ID {} for path {}", id, ref->name);
    }
    return B_OK;
}

status_t RelationHandler::QueryAllForSenId(const char* senId, std::vector<entry_ref>* refs)
{
    BString predicate(BString(sen::attr::kId) << "==" << senId);
    return sen::QueryAllVolumes(predicate.String(), refs);
}

status_t RelationHandler::QueryForUniqueSenId(const char* sourceId, entry_ref* refFound)
{
    BString predicate(BString(sen::attr::kId) << "==" << sourceId);

    std::vector<entry_ref> refs;
    status_t result = sen::QueryAllVolumes(predicate.String(), &refs);
    if (result != B_OK) {
        spdlog::error("could not execute query for {} == {}: {}", sen::attr::kId, sourceId, strerror(result));
        return result;
    }

    if (refs.empty()) {
        spdlog::info("no matching file found for ID {}", sourceId);
        return B_ENTRY_NOT_FOUND;
    }

    if (refs.size() > 1) {
        // this should never happen as the SEN:ID MUST be unique!
        spdlog::error("Critical error SEN:ID {} is NOT unique, found {} files!", sourceId, refs.size());
        return B_DUPLICATE_REPLY;
    }

    *refFound = refs.front();
    spdlog::info("found entry {}", refFound->name);

    return B_OK;
}

// used to resolve inverse relations where we need to go from target->source
// todo: offer a live query (passing around a dest messenger) when querying large number of targets,
//       e.g. for inverse relations with Classification entities!
status_t RelationHandler::QueryForTargetsById(const char* sourceId, BMessage* idToRef)
{
    spdlog::info("query for inverse relation targets with sourceId {}", sourceId);

    // query for files with a SEN:TO attr containing our sourceId
    BString predicate("(");
    predicate << sen::idlist::ContainsPredicate(sen::attr::kTo, sourceId).c_str() << ") || ("
              << sen::idlist::ContainsPredicate(sen::attr::kMeta, sourceId).c_str() << ")";

    std::vector<entry_ref> refs;
    status_t result = sen::QueryAllVolumes(predicate.String(), &refs);
    if (result != B_OK) {
        spdlog::error("could not execute query for the targets of {}: {}", sourceId, strerror(result));
        return result;
    }

    for (const entry_ref& refFound : refs) {
        char senId[sen::id::kLength];
        result = GetOrCreateId(&refFound, senId);
        if (result != B_OK) {
            // unexpected error, abort
            spdlog::error("error resolving SEN:ID for entry {}, aborting: {}", refFound.name, strerror(result));
            return result;
        }
        idToRef->AddRef(senId, new entry_ref(refFound));
    }

    return B_OK;
}

//
// Relation helpers
//
void RelationHandler::GetAttributeNameForRelation(const char* relationType, BString* attrName)
{
    BString attrNameStr(relationType);

    // strip possible relation supertype
    if (attrNameStr.StartsWith(sen::mime::kRelationPrefix)) {
        attrNameStr.RemoveFirst(sen::mime::kRelationPrefix);
    }
    // add SEN:REL prefix if not there already
    if (! attrNameStr.StartsWith(sen::attr::kRelationPrefix)) {
        attrNameStr.Prepend(sen::attr::kRelationPrefix);
    }

    *attrName = attrNameStr;
}

status_t RelationHandler::GetTypeForRef(entry_ref* ref, BString* typeName)
{
    BNode srcNode(ref);
    status_t status = srcNode.InitCheck();
    if (status != B_OK) {
        spdlog::error("could not get source node for ref {}: {}", ref->name, strerror(status));
        return status;
    }

    BNodeInfo srcInfo(&srcNode);
    char srcType[B_MIME_TYPE_LENGTH];

    status = srcInfo.GetType(srcType);
    if (status == B_ENTRY_NOT_FOUND) {      // no type yet: not an error
        typeName->SetTo("");
        return B_OK;
    }
    if (status != B_OK) {
        spdlog::error("could not get type info for ref {}: {}",
                ref->name, strerror(status));
        return status;
    }

    typeName->SetTo(srcType);
    return B_OK;
}
