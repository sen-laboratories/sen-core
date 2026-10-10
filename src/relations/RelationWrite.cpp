/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

/**
 * @file RelationWrite.cpp
 * @brief Adding, changing and removing relations, and finding the relations that point to a file.
 *
 * A relation of a type is stored in the attribute `SEN:REL:<type>` of its source (see RelationSets.h), and the `SEN:ID` of
 * its target is in the list `SEN:TO` of the source (`SEN:META` for meta relations: classification and context, see
 * sen::mime::kAssociationRelation). Relations are bidirectional by default: the target stores the opposite direction
 * (same properties, the label of the opposite direction) and lists the source. Unidirectional relations (associations)
 * are stored with the file only, so that a label does not hold a link to everything it labels; what is labelled with
 * it is found with a query for the id in `SEN:META`.
 *
 * Every operation changes attributes of two files. It is all-or-nothing: what is changed is captured first
 * (AttrSnapshot) and put back if a step fails.
 */

#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <String.h>

#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include <sen/Sen.h>

#include "AttrSnapshot.h"
#include "IdListAttr.h"
#include "QueryUtil.h"
#include "RelationHandler.h"
#include "RelationSets.h"
#include "../server/Reply.h"

namespace {

/** Meta relations (classification and context) list their targets in SEN:META, all others in SEN:TO. */
bool
IsMetaRelation(const char* relationType)
{
    return strcmp(relationType, sen::mime::kAssociationRelation) == 0;
}

const char*
ListAttrFor(const char* relationType)
{
    return IsMetaRelation(relationType) ? sen::attr::kMeta : sen::attr::kTo;
}

/** the names of all attributes of a list, to capture them */
std::vector<std::string>
ListAttrNames(const char* base)
{
    std::vector<std::string> names;
    for (size_t i = 0; i < sen::idlist::kMaxChunks; i++)
        names.push_back(sen::idlist::ChunkName(base, i));
    return names;
}

bool
IsClassification(const BString& mimeType)
{
    return mimeType.StartsWith(sen::mime::kClassificationPrefix);
}

}   // namespace

//
// reading and writing the relations of a file
//

status_t RelationHandler::ReadRelationMessage(const entry_ref& ref, const char* relationType, BMessage* relations)
{
    relations->MakeEmpty();
    return ReadRelationsOfType(&ref, relationType, relations);
}

status_t RelationHandler::StoreRelationMessage(const entry_ref& ref, const char* relationType,
                                               const BMessage& relations, sen::AttrSnapshot* tx)
{
    BNode node(&ref);
    status_t status = node.InitCheck();
    if (status != B_OK)
        return status;

    BString attrName;
    GetAttributeNameForRelation(relationType, &attrName);
    tx->Capture(ref, {attrName.String()});

    // a file without relations of the type does not keep an empty attribute
    if (sen::relation::IsEmpty(relations)) {
        status = node.RemoveAttr(attrName.String());
        return (status == B_ENTRY_NOT_FOUND || status == B_NAME_NOT_FOUND) ? B_OK : status;
    }

    ssize_t size = relations.FlattenedSize();
    std::vector<char> buffer(size);
    status = relations.Flatten(buffer.data(), size);
    if (status != B_OK) {
        spdlog::error("failed to flatten relation {} of file {}: {}", relationType, ref.name, strerror(status));
        return status;
    }

    ssize_t written = node.WriteAttr(attrName.String(), B_MESSAGE_TYPE, 0, buffer.data(), size);
    if (written != size) {
        spdlog::error("failed to store relation {} for file {}: {}", relationType, ref.name,
            written < 0 ? strerror(written) : "short write");
        return written < 0 ? written : B_ERROR;
    }
    return B_OK;
}

status_t RelationHandler::ChangeTargetList(const entry_ref& ref, const char* relationType, const char* targetId,
                                           bool add, sen::AttrSnapshot* tx)
{
    BNode node(&ref);
    status_t status = node.InitCheck();
    if (status != B_OK)
        return status;

    const char* base = ListAttrFor(relationType);
    tx->Capture(ref, ListAttrNames(base));

    status = add ? sen::AddToIdList(node, base, targetId) : sen::RemoveFromIdList(node, base, targetId);
    if (status == B_BUFFER_OVERFLOW) {
        spdlog::error("the list {} of file {} is full ({} targets)", base, ref.name, sen::idlist::kMaxIds);
    }
    return status;
}

//
// add
//

bool RelationHandler::ShouldWriteInverse(const BMessage& relationConfig, const BString& sourceType, const BString& targetType)
{
    // relations are bidirectional by default (makes sense in 95% of cases). Unidirectional ones (associations) do not
    // overload the meta entities with links to everything they label: those are found by query. Between two
    // classification entities they stay bidirectional, to form classification networks.
    return relationConfig.GetBool(sen::conf::kBidirectional, true)
        || (IsClassification(sourceType) && IsClassification(targetType));
}

BMessage RelationHandler::InverseProperties(const BMessage& relationConfig, const BMessage& properties)
{
    BMessage inverse(properties);
    inverse.RemoveName(sen::key::kRelationId);

    BMessage inverseConfig;
    if (relationConfig.FindMessage(sen::conf::kInverse, &inverseConfig) == B_OK) {
        const char* label = inverseConfig.GetString(sen::attr::kRelationLabel, "");
        if (label[0] != '\0') {
            inverse.RemoveName(sen::attr::kRelationLabel);
            inverse.AddString(sen::attr::kRelationLabel, label);
        }
    }
    return inverse;
}

status_t RelationHandler::AddRelationTx(const entry_ref& source, const entry_ref& target, const char* relationType,
                                        const BMessage& properties, const BMessage& relationConfig,
                                        sen::AttrSnapshot* tx, BString* relationId, bool* created,
                                        const BMessage* inverseProperties)
{
    *created = false;

    BString sourceType, targetType;
    entry_ref sourceCopy(source), targetCopy(target);
    GetTypeForRef(&sourceCopy, &sourceType);
    GetTypeForRef(&targetCopy, &targetType);

    char sourceId[sen::id::kLength], targetId[sen::id::kLength];
    status_t status = GetOrCreateId(&source, sourceId, true);
    if (status == B_OK)
        status = GetOrCreateId(&target, targetId, true);
    if (status != B_OK)
        return status;

    // the relation itself, stored with its source
    BMessage forward;
    status = ReadRelationMessage(source, relationType, &forward);
    if (status != B_OK)
        return status;

    // Note: we allow several relations of the same type to the same target (e.g. a note for the same text referencing
    // different places in the referenced text), see RelationSets.h
    status = sen::relation::AddSet(&forward, targetId, properties, relationId);
    if (status == B_NAME_IN_USE) {
        spdlog::debug("relation {} to {} with the same properties exists already.", relationType, targetId);
        return B_OK;
    }
    if (status != B_OK)
        return status;

    status = StoreRelationMessage(source, relationType, forward, tx);
    if (status == B_OK)
        status = ChangeTargetList(source, relationType, targetId, true, tx);
    if (status != B_OK)
        return status;

    // the opposite direction, stored with the target, in step with the relation (same position, same relation id)
    if (ShouldWriteInverse(relationConfig, sourceType, targetType) && ! (source == target)) {
        BMessage inverse;
        status = ReadRelationMessage(target, relationType, &inverse);
        if (status != B_OK)
            return status;

        BString inverseId;
        // the opposite direction gets what the relation says about it (its own label...), over what follows from the relation
        BMessage inverseSet = InverseProperties(relationConfig, properties);
        if (inverseProperties != NULL) {
            char* name;
            type_code type;
            for (int32 i = 0; inverseProperties->GetInfo(B_ANY_TYPE, i, &name, &type) == B_OK; i++) {
                const void* data;
                ssize_t size;
                if (inverseProperties->FindData(name, type, &data, &size) == B_OK) {
                    inverseSet.RemoveName(name);
                    inverseSet.AddData(name, type, data, size);
                }
            }
        }
        status = sen::relation::AddSet(&inverse, sourceId, inverseSet, &inverseId);
        if (status != B_OK && status != B_NAME_IN_USE)
            return status;

        int32 sets = sen::relation::CountSets(forward, targetId);
        if (sets == sen::relation::CountSets(inverse, sourceId)) {
            for (int32 i = 0; i < sets; i++) {
                BMessage set;
                if (sen::relation::GetSet(forward, targetId, i, &set) == B_OK)
                    sen::relation::SetRelationId(&inverse, sourceId, i, set.GetString(sen::key::kRelationId, ""));
            }
        } else {
            spdlog::warn("the opposite direction of relation {} between {} and {} is not in step with it ({} vs {} sets)",
                relationType, sourceId, targetId, sets, sen::relation::CountSets(inverse, sourceId));
        }

        status = StoreRelationMessage(target, relationType, inverse, tx);
        if (status == B_OK)
            status = ChangeTargetList(target, relationType, sourceId, true, tx);
        if (status != B_OK)
            return status;
    }

    *created = true;
    return B_OK;
}

status_t RelationHandler::AddRelation(const BMessage* message, BMessage* reply)
{
    entry_ref source, target;
    status_t status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &source);
    if (status != B_OK)
        return status;

    BString relationTypeStr;
    if ((status = GetMessageParameter(message, sen::key::kRelationType, &relationTypeStr)) != B_OK)
        return status;
    const char* relationType = relationTypeStr.String();

    if ((status = GetMessageParameter(message, sen::key::kTargetRef, NULL, &target)) != B_OK)
        return status;

    BMessage relationConfig;
    if ((status = GetRelationConfig(relationType, &relationConfig)) != B_OK) {
        spdlog::error("failed to get relation config for type {}: {}", relationType, strerror(status));
        sen::reply::SetStatus(reply, sen::status::kErrUnknownRelationType);
        sen::reply::SetDetail(reply, (BString("unknown relation type '") << relationType << "'").String());
        return status;
    }

    // properties are optional
    BMessage properties;
    message->FindMessage(sen::key::kRelationProperties, &properties);

    // associations are stored with the file that is classified, never with the classification entity: if a label is
    // dropped on a file, it is the file that gets the relation to the label
    if (IsMetaRelation(relationType)) {
        BString sourceType, targetType;
        entry_ref sourceCopy(source), targetCopy(target);
        GetTypeForRef(&sourceCopy, &sourceType);
        GetTypeForRef(&targetCopy, &targetType);
        if (IsClassification(sourceType) && ! IsClassification(targetType))
            std::swap(source, target);
    }

    sen::AttrSnapshot tx;
    BString relationId;
    bool created = false;
    BMessage inverseProperties;
    bool hasInverseProperties = message->FindMessage(sen::key::kInverseProperties, &inverseProperties) == B_OK;
    status = AddRelationTx(source, target, relationType, properties, relationConfig, &tx, &relationId, &created,
        hasInverseProperties ? &inverseProperties : NULL);

    if (status != B_OK) {
        tx.Restore();
        BString detail("failed to create relation '");
        detail << relationType << "' from " << source.name << " to " << target.name << ": " << strerror(status);
        sen::reply::SetDetail(reply, detail.String());
        return status;
    }

    if (! relationId.IsEmpty())
        reply->AddString(sen::key::kRelationId, relationId);

    BString detail(created ? "created relation '" : "relation '");
    detail << relationType << "' from " << source.name << " to " << target.name << (created ? "" : " exists already");
    sen::reply::SetDetail(reply, detail.String());
    sen::reply::SetStatus(reply, created ? sen::status::kCreated : sen::status::kOk);
    reply->what = sen::cmd::kReplyRelations;

    return B_OK;
}

//
// remove
//

status_t RelationHandler::GetTargetIdParameter(const BMessage* message, BString* targetId)
{
    // the target is given by its entry_ref, or by its ID if it is gone (a relation to a target that does not exist any more)
    if (message->HasRef(sen::key::kTargetRef)) {
        entry_ref target;
        status_t status = GetMessageParameter(message, sen::key::kTargetRef, NULL, &target);
        if (status != B_OK)
            return status;

        char id[sen::id::kLength];
        status = GetOrCreateId(&target, id, false);
        if (status != B_OK)
            return status;
        targetId->SetTo(id);
        return B_OK;
    }

    return GetMessageParameter(message, sen::key::kTargetId, targetId);
}

/** a relation that is read-only (the property SEN:REL:readonly) can be changed only with the override of the sender */
static bool
IsReadOnly(const BMessage& properties)
{
    return properties.GetBool(sen::attr::kRelationReadOnly, false);
}

status_t RelationHandler::RemoveRelationTx(const entry_ref& source, const char* relationType, const char* targetId,
                                           const char* relationId, bool allSets, sen::AttrSnapshot* tx,
                                           BString* removedRelationId, bool allowReadOnly)
{
    BMessage forward;
    status_t status = ReadRelationMessage(source, relationType, &forward);
    if (status != B_OK)
        return status;

    int32 index = 0;
    if (! allSets) {
        status = sen::relation::FindSet(forward, targetId, relationId, &index);
        if (status != B_OK)
            return status;

        BMessage removed;
        if (sen::relation::GetSet(forward, targetId, index, &removed) == B_OK) {
            if (IsReadOnly(removed) && !allowReadOnly)
                return B_NOT_ALLOWED;
            removedRelationId->SetTo(removed.GetString(sen::key::kRelationId, ""));
        }
        status = sen::relation::RemoveSet(&forward, targetId, index);
    } else {
        int32 sets = sen::relation::CountSets(forward, targetId);
        if (sets == 0)
            return B_NAME_NOT_FOUND;
        for (int32 set = 0; set < sets && !allowReadOnly; set++) {
            BMessage each;
            if (sen::relation::GetSet(forward, targetId, set, &each) == B_OK && IsReadOnly(each))
                return B_NOT_ALLOWED;
        }
        status = forward.RemoveName(targetId);
    }
    if (status != B_OK)
        return status;

    status = StoreRelationMessage(source, relationType, forward, tx);
    if (status == B_OK && sen::relation::CountSets(forward, targetId) == 0)
        status = ChangeTargetList(source, relationType, targetId, false, tx);
    if (status != B_OK)
        return status;

    // the opposite direction at the target, if there is one: the same relation (same relation id, or the only one)
    entry_ref target;
    char sourceId[sen::id::kLength];
    if (QueryForUniqueSenId(targetId, &target) != B_OK || target == source
            || GetOrCreateId(&source, sourceId, false) != B_OK) {
        return B_OK;    // the target is gone, or there is nothing to keep in step
    }

    BMessage inverse;
    if (ReadRelationMessage(target, relationType, &inverse) != B_OK || sen::relation::CountSets(inverse, sourceId) == 0)
        return B_OK;    // unidirectional, or never written

    if (allSets) {
        inverse.RemoveName(sourceId);
    } else {
        int32 inverseIndex = -1;
        if (sen::relation::FindSet(inverse, sourceId, removedRelationId->String(), &inverseIndex) != B_OK)
            return B_OK;
        status = sen::relation::RemoveSet(&inverse, sourceId, inverseIndex);
        if (status != B_OK)
            return status;
    }

    status = StoreRelationMessage(target, relationType, inverse, tx);
    if (status == B_OK && sen::relation::CountSets(inverse, sourceId) == 0)
        status = ChangeTargetList(target, relationType, sourceId, false, tx);
    return status;
}

status_t RelationHandler::RemoveRelation(const BMessage* message, BMessage* reply)
{
    entry_ref source;
    status_t status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &source);
    if (status != B_OK)
        return status;

    BString relationType;
    if ((status = GetMessageParameter(message, sen::key::kRelationType, &relationType)) != B_OK)
        return status;

    BString targetId;
    if ((status = GetTargetIdParameter(message, &targetId)) != B_OK)
        return status;

    // without a relation id all relations of the type to the target are meant if there is more than one
    BString relationId;
    message->FindString(sen::key::kRelationId, &relationId);
    bool allSets = message->GetBool(sen::key::kAllRelations, false);

    sen::AttrSnapshot tx;
    BString removedId;
    status = RemoveRelationTx(source, relationType.String(), targetId.String(), relationId.String(), allSets, &tx, &removedId,
        message->GetBool(sen::key::kOverride, false));

    if (status != B_OK) {
        tx.Restore();
        if (status == B_BAD_VALUE)
            sen::reply::SetStatus(reply, sen::status::kErrAmbiguousRelation);
        else if (status == B_NAME_NOT_FOUND)
            sen::reply::SetStatus(reply, sen::status::kErrRelationNotFound);
        else if (status == B_NOT_ALLOWED)
            sen::reply::SetStatus(reply, sen::status::kErrForbidden);
        BString detail("failed to remove relation '");
        detail << relationType << "' from " << source.name << " to " << targetId << ": " << strerror(status);
        sen::reply::SetDetail(reply, detail.String());
        return status;
    }

    reply->what = sen::cmd::kReplyRelations;
    sen::reply::SetDetail(reply, (BString("removed relation '") << relationType << "' from " << source.name
        << " to " << targetId).String());
    return B_OK;
}

status_t RelationHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
    entry_ref source;
    status_t status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &source);
    if (status != B_OK)
        return status;

    // all types, or one
    BStringList types;
    BString onlyType;
    if (message->FindString(sen::key::kRelationType, &onlyType) == B_OK)
        types.Add(onlyType);
    else
        ReadRelationNames(&source, &types);     // its result is the end of the attributes, not an error

    sen::AttrSnapshot tx;
    int32 removed = 0;
    bool staleOnly = message->GetBool(sen::key::kStaleOnly, false);

    for (int32 t = 0; t < types.CountStrings() && status == B_OK; t++) {
        const BString& type = types.StringAt(t);

        BMessage relations;
        status = ReadRelationMessage(source, type.String(), &relations);
        if (status != B_OK)
            break;

        // collect first: removing changes the message
        std::vector<std::string> targets;
        char* name;
        type_code typeCode;
        for (int32 i = 0; relations.GetInfo(B_MESSAGE_TYPE, i, &name, &typeCode) == B_OK; i++)
            targets.push_back(name);

        for (const std::string& targetId : targets) {
            // only what is stale, if asked: what is read-only (those of the ontologies) or points to a file that is gone, not the relations
            // that users made to files that are there
            if (staleOnly) {
                int32 sets = sen::relation::CountSets(relations, targetId.c_str());
                bool allReadOnly = sets > 0;
                for (int32 set = 0; set < sets && allReadOnly; set++) {
                    BMessage each;
                    allReadOnly = sen::relation::GetSet(relations, targetId.c_str(), set, &each) == B_OK && IsReadOnly(each);
                }
                entry_ref targetRef;
                bool gone = QueryForUniqueSenId(targetId.c_str(), &targetRef) != B_OK;
                if (!allReadOnly && !gone)
                    continue;
            }

            BString removedId;
            status = RemoveRelationTx(source, type.String(), targetId.c_str(), "", true, &tx, &removedId,
                staleOnly || message->GetBool(sen::key::kOverride, false));
            if (status == B_NOT_ALLOWED) {
                status = B_OK;      // read-only relations stay
                continue;
            }
            if (status != B_OK)
                break;
            removed++;
        }
    }

    if (status != B_OK) {
        tx.Restore();
        sen::reply::SetDetail(reply, (BString("failed to remove the relations of ") << source.name << ": " << strerror(status)).String());
        return status;
    }

    reply->what = sen::cmd::kReplyRelations;
    reply->AddInt32(sen::key::kCount, removed);
    sen::reply::SetDetail(reply, (BString("removed ") << removed << " relation target(s) of " << source.name).String());
    return B_OK;
}

//
// update
//

status_t RelationHandler::UpdateRelation(const BMessage* message, BMessage* reply)
{
    entry_ref source;
    status_t status = GetMessageParameter(message, sen::key::kSourceRef, NULL, &source);
    if (status != B_OK)
        return status;

    BString relationTypeStr;
    if ((status = GetMessageParameter(message, sen::key::kRelationType, &relationTypeStr)) != B_OK)
        return status;
    const char* relationType = relationTypeStr.String();

    BString targetId;
    if ((status = GetTargetIdParameter(message, &targetId)) != B_OK)
        return status;

    BString relationId;
    message->FindString(sen::key::kRelationId, &relationId);

    BMessage relationConfig;
    if ((status = GetRelationConfig(relationType, &relationConfig)) != B_OK) {
        sen::reply::SetStatus(reply, sen::status::kErrUnknownRelationType);
        return status;
    }

    sen::AttrSnapshot tx;

    BMessage newProperties;
    bool hasProperties = message->FindMessage(sen::key::kRelationProperties, &newProperties) == B_OK;

    // 1. new properties: replace them in the relation and its opposite direction
    BMessage forward;
    int32 index = 0;
    status = ReadRelationMessage(source, relationType, &forward);
    if (status == B_OK)
        status = sen::relation::FindSet(forward, targetId.String(), relationId.String(), &index);

    BMessage current;
    if (status == B_OK)
        status = sen::relation::GetSet(forward, targetId.String(), index, &current);
    if (status == B_OK)
        relationId.SetTo(current.GetString(sen::key::kRelationId, ""));
    if (status == B_OK && IsReadOnly(current) && !message->GetBool(sen::key::kOverride, false))
        status = B_NOT_ALLOWED;

    if (status == B_OK && hasProperties) {
        status = sen::relation::ReplaceSet(&forward, targetId.String(), index, newProperties);
        if (status == B_OK)
            status = StoreRelationMessage(source, relationType, forward, &tx);

        entry_ref target;
        char sourceId[sen::id::kLength];
        if (status == B_OK && QueryForUniqueSenId(targetId.String(), &target) == B_OK && ! (target == source)
                && GetOrCreateId(&source, sourceId, false) == B_OK) {
            BMessage inverse;
            int32 inverseIndex = -1;
            if (ReadRelationMessage(target, relationType, &inverse) == B_OK
                    && sen::relation::FindSet(inverse, sourceId, relationId.String(), &inverseIndex) == B_OK) {
                BMessage existing;
                sen::relation::GetSet(inverse, sourceId, inverseIndex, &existing);

                // the opposite direction keeps its own label
                BMessage inverseProperties = InverseProperties(relationConfig, newProperties);
                const char* label = existing.GetString(sen::attr::kRelationLabel, "");
                if (label[0] != '\0') {
                    inverseProperties.RemoveName(sen::attr::kRelationLabel);
                    inverseProperties.AddString(sen::attr::kRelationLabel, label);
                }
                status = sen::relation::ReplaceSet(&inverse, sourceId, inverseIndex, inverseProperties);
                if (status == B_OK)
                    status = StoreRelationMessage(target, relationType, inverse, &tx);
            }
        }
    }

    // 2. a new target or a new type: the relation moves (added at the new place, then removed from the old one)
    entry_ref newTarget;
    BString newTypeStr;
    bool hasNewTarget = message->FindRef(sen::key::kNewTargetRef, &newTarget) == B_OK;
    bool hasNewType = message->FindString(sen::key::kNewRelationType, &newTypeStr) == B_OK;

    if (status == B_OK && (hasNewTarget || hasNewType)) {
        const char* newType = hasNewType ? newTypeStr.String() : relationType;

        entry_ref oldTarget;
        if (! hasNewTarget && QueryForUniqueSenId(targetId.String(), &oldTarget) != B_OK) {
            status = B_ENTRY_NOT_FOUND;     // the target is gone and no new one given
        } else {
            entry_ref moveTo = hasNewTarget ? newTarget : oldTarget;
            char moveToId[sen::id::kLength];
            bool same = GetOrCreateId(&moveTo, moveToId, false) == B_OK && targetId == moveToId
                && strcmp(newType, relationType) == 0;

            if (! same) {
                BMessage newConfig;
                status = GetRelationConfig(newType, &newConfig);

                // carry the properties of the relation along (the updated ones)
                BMessage moved;
                if (status == B_OK)
                    status = ReadRelationMessage(source, relationType, &forward);
                if (status == B_OK)
                    status = sen::relation::FindSet(forward, targetId.String(), relationId.String(), &index);
                if (status == B_OK)
                    status = sen::relation::GetSet(forward, targetId.String(), index, &moved);
                if (status == B_OK)
                    moved.RemoveName(sen::key::kRelationId);

                BString addedId, removedId;
                bool created = false;
                if (status == B_OK)
                    status = AddRelationTx(source, moveTo, newType, moved, newConfig, &tx, &addedId, &created);
                if (status == B_OK)
                    status = RemoveRelationTx(source, relationType, targetId.String(), relationId.String(), false, &tx, &removedId,
                        true);      // the check was done above
                if (status == B_OK && ! addedId.IsEmpty())
                    reply->AddString(sen::key::kRelationId, addedId);
            }
        }
    }

    if (status != B_OK) {
        tx.Restore();
        if (status == B_BAD_VALUE)
            sen::reply::SetStatus(reply, sen::status::kErrAmbiguousRelation);
        else if (status == B_NAME_NOT_FOUND)
            sen::reply::SetStatus(reply, sen::status::kErrRelationNotFound);
        else if (status == B_NOT_ALLOWED)
            sen::reply::SetStatus(reply, sen::status::kErrForbidden);
        sen::reply::SetDetail(reply, (BString("failed to update relation '") << relationType << "' of " << source.name
            << ": " << strerror(status)).String());
        return status;
    }

    reply->what = sen::cmd::kReplyRelations;
    sen::reply::SetDetail(reply, (BString("updated relation '") << relationType << "' of " << source.name).String());
    return B_OK;
}

//
// the relations that point to a file
//

status_t RelationHandler::ResolveReverseRelations(const entry_ref* ref, const char* relationType,
                                                  const BMessage& relationConfig, BMessage* relations, BMessage* idToRef)
{
    char sourceId[sen::id::kLength];
    status_t status = GetOrCreateId(ref, sourceId, false);
    if (status == B_ENTRY_NOT_FOUND || status == B_NAME_NOT_FOUND)
        return B_OK;    // no ID: nothing can point to the file
    if (status != B_OK)
        return status;

    BString predicate("(");
    predicate << sen::idlist::ContainsPredicate(ListAttrFor(relationType), sourceId).c_str() << ")";

    std::vector<entry_ref> files;
    status = sen::QueryAllVolumes(predicate.String(), &files);
    if (status != B_OK)
        return status;

    BMessage inverseConfig;
    relationConfig.FindMessage(sen::conf::kInverse, &inverseConfig);
    const char* inverseLabel = inverseConfig.GetString(sen::attr::kRelationLabel, "");

    for (const entry_ref& file : files) {
        if (file == *ref)
            continue;

        char fileId[sen::id::kLength];
        BMessage stored;
        if (GetOrCreateId(&file, fileId, false) != B_OK || ReadRelationMessage(file, relationType, &stored) != B_OK)
            continue;

        int32 sets = sen::relation::CountSets(stored, sourceId);
        for (int32 i = 0; i < sets; i++) {
            BMessage set;
            if (sen::relation::GetSet(stored, sourceId, i, &set) != B_OK)
                continue;
            if (inverseLabel[0] != '\0') {
                set.RemoveName(sen::attr::kRelationLabel);
                set.AddString(sen::attr::kRelationLabel, inverseLabel);
            }
            relations->AddMessage(fileId, &set);
        }
        if (sets > 0 && idToRef != NULL)
            idToRef->AddRef(fileId, new entry_ref(file));
    }

    return B_OK;
}
