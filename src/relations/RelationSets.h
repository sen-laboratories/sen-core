/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <String.h>

/**
 * @file RelationSets.h
 * @brief The relations of one type of a file: target ID -> one or more sets of properties.
 *
 * The attribute `SEN:REL:<type>` of a file holds a message with a field per target, named by the `SEN:ID` of the target.
 * Each field is an array of messages, one per relation of this type to that target (a note can refer to several pages of
 * one document), a "set of properties" (page, label, ...). The sets are in the order in which they were added.
 *
 * A single relation is identified by source, type and target. Only when there are several sets for the same target each
 * set also has a relation id (`SEN:relationId`, a TSID), added when the second set is created and removed again when only
 * one is left. A relation and its opposite direction are kept in step: the same index, the same relation id.
 *
 * Nothing here touches the file system: the functions work on the message only.
 */
namespace sen {
namespace relation {

/** @return how many property sets the relations have for the target. */
int32 CountSets(const BMessage& relations, const char* targetId);

/** @return true if there is no target in the relations. */
bool IsEmpty(const BMessage& relations);

/** @return the number of targets in the relations. */
int32 CountTargets(const BMessage& relations);

/**
 * @brief Add a set of properties for a target.
 *
 * A set with the same properties (the relation id does not count) is not added twice. When the target has a set already,
 * all its sets get a relation id (also the ones that were added before).
 *
 * @param relations  the relations of one type of a file
 * @param targetId   the `SEN:ID` of the target
 * @param properties the properties of the new relation
 * @param relationId receives the relation id of the new set, empty if it is the only one of the target
 * @return B_OK if added, B_NAME_IN_USE if the same set exists (relationId is then the one of that set)
 */
status_t AddSet(BMessage* relations, const char* targetId, const BMessage& properties, BString* relationId);

/**
 * @brief Find the set of one relation.
 *
 * @param relationId the relation id, or empty if the target has only one set
 * @param index      receives the position of the set
 * @return B_OK, B_NAME_NOT_FOUND if there is no such relation, B_BAD_VALUE if no relation id was given but there are
 *         several sets (the relation is ambiguous)
 */
status_t FindSet(const BMessage& relations, const char* targetId, const char* relationId, int32* index);

/** @brief Read the set at an index (with its relation id, if it has one). */
status_t GetSet(const BMessage& relations, const char* targetId, int32 index, BMessage* properties);

/**
 * @brief Replace the properties of the set at an index; it keeps its relation id.
 */
status_t ReplaceSet(BMessage* relations, const char* targetId, int32 index, const BMessage& properties);

/**
 * @brief Remove the set at an index. When it was the last of the target the target is removed from the relations; when
 * only one is left, it loses its relation id (it is unique again).
 */
status_t RemoveSet(BMessage* relations, const char* targetId, int32 index);

/** @brief Give the set at an index the relation id (used to keep the opposite direction in step). */
status_t SetRelationId(BMessage* relations, const char* targetId, int32 index, const char* relationId);

}   // namespace relation
}   // namespace sen
