/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#include "RelationSets.h"

#include <sen/Sen.h>

namespace sen {
namespace relation {

namespace {

/** the properties without the relation id: what makes two sets the same */
BMessage
WithoutId(const BMessage& set)
{
    BMessage copy(set);
    copy.RemoveName(key::kRelationId);
    return copy;
}

void
StoreId(BMessage* relations, const char* targetId, int32 index, const char* relationId)
{
    BMessage set;
    if (relations->FindMessage(targetId, index, &set) != B_OK)
        return;

    set.RemoveName(key::kRelationId);
    if (relationId != NULL && relationId[0] != '\0')
        set.AddString(key::kRelationId, relationId);
    relations->ReplaceMessage(targetId, index, &set);
}

}   // namespace

int32
CountSets(const BMessage& relations, const char* targetId)
{
    type_code type;
    int32 count = 0;
    if (relations.GetInfo(targetId, &type, &count) != B_OK || type != B_MESSAGE_TYPE)
        return 0;
    return count;
}

int32
CountTargets(const BMessage& relations)
{
    return relations.CountNames(B_MESSAGE_TYPE);
}

bool
IsEmpty(const BMessage& relations)
{
    return CountTargets(relations) == 0;
}

status_t
AddSet(BMessage* relations, const char* targetId, const BMessage& properties, BString* relationId)
{
    relationId->SetTo("");

    const BMessage wanted = WithoutId(properties);
    int32 count = CountSets(*relations, targetId);

    for (int32 i = 0; i < count; i++) {
        BMessage existing;
        if (relations->FindMessage(targetId, i, &existing) != B_OK)
            continue;
        if (WithoutId(existing).HasSameData(wanted)) {
            relationId->SetTo(existing.GetString(key::kRelationId, ""));
            return B_NAME_IN_USE;
        }
    }

    BMessage added(wanted);
    if (count >= 1) {
        // several relations of one type between the same two files: each needs an identity now
        for (int32 i = 0; i < count; i++) {
            BMessage existing;
            if (relations->FindMessage(targetId, i, &existing) == B_OK && ! existing.HasString(key::kRelationId))
                StoreId(relations, targetId, i, id::New().c_str());
        }
        relationId->SetTo(id::New().c_str());
        added.AddString(key::kRelationId, relationId->String());
    }

    return relations->AddMessage(targetId, &added);
}

status_t
FindSet(const BMessage& relations, const char* targetId, const char* relationId, int32* index)
{
    int32 count = CountSets(relations, targetId);
    if (count == 0)
        return B_NAME_NOT_FOUND;

    if (relationId == NULL || relationId[0] == '\0') {
        if (count > 1)
            return B_BAD_VALUE;
        *index = 0;
        return B_OK;
    }

    for (int32 i = 0; i < count; i++) {
        BMessage set;
        if (relations.FindMessage(targetId, i, &set) == B_OK && strcmp(set.GetString(key::kRelationId, ""), relationId) == 0) {
            *index = i;
            return B_OK;
        }
    }
    return B_NAME_NOT_FOUND;
}

status_t
GetSet(const BMessage& relations, const char* targetId, int32 index, BMessage* properties)
{
    return relations.FindMessage(targetId, index, properties);
}

status_t
ReplaceSet(BMessage* relations, const char* targetId, int32 index, const BMessage& properties)
{
    BMessage existing;
    status_t status = relations->FindMessage(targetId, index, &existing);
    if (status != B_OK)
        return status;

    BMessage replacement = WithoutId(properties);
    const char* relationId = NULL;
    if (existing.FindString(key::kRelationId, &relationId) == B_OK)
        replacement.AddString(key::kRelationId, relationId);

    return relations->ReplaceMessage(targetId, index, &replacement);
}

status_t
RemoveSet(BMessage* relations, const char* targetId, int32 index)
{
    int32 count = CountSets(*relations, targetId);
    if (index < 0 || index >= count)
        return B_BAD_INDEX;

    status_t status = relations->RemoveData(targetId, index);
    if (status != B_OK)
        return status;

    // a relation that is the only one of its target is identified by source, type and target again
    if (count == 2)
        StoreId(relations, targetId, 0, NULL);

    return B_OK;
}

status_t
SetRelationId(BMessage* relations, const char* targetId, int32 index, const char* relationId)
{
    if (index < 0 || index >= CountSets(*relations, targetId))
        return B_BAD_INDEX;
    StoreId(relations, targetId, index, relationId);
    return B_OK;
}

}   // namespace relation
}   // namespace sen
