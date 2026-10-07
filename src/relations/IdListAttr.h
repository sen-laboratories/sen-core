/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Node.h>
#include <String.h>

#include <string>
#include <vector>

#include <sen/Sen.h>

/**
 * @file IdListAttr.h
 * @brief Reading and writing the chunked id lists (`SEN:TO`, `SEN:META`) of a node, see sen::idlist.
 */
namespace sen {

/**
 * @brief Read the ids of a list from all its attributes.
 * @param node the file
 * @param base the attribute of the first chunk (sen::attr::kTo or sen::attr::kMeta)
 * @param ids  receives the ids, in the order of the attributes; empty if the file has none
 * @return B_OK (also if there is no such attribute), or the error of reading
 */
inline status_t
ReadIdList(BNode& node, const char* base, std::vector<std::string>* ids)
{
    ids->clear();
    for (size_t i = 0; i < idlist::kMaxChunks; i++) {
        BString value;
        status_t status = node.ReadAttrString(idlist::ChunkName(base, i).c_str(), &value);
        if (status == B_ENTRY_NOT_FOUND)
            continue;
        if (status != B_OK)
            return status;
        for (const std::string& id : idlist::Split(value.String()))
            idlist::Add(ids, id);
    }
    return B_OK;
}

/**
 * @brief Write a list of ids to its attributes, and remove the attributes it does not need any more.
 * @return B_OK, B_BUFFER_OVERFLOW if the list is too long (more than sen::idlist::kMaxIds), or the error of writing
 */
inline status_t
WriteIdList(BNode& node, const char* base, const std::vector<std::string>& ids)
{
    std::vector<std::string> chunks;
    if (! idlist::Chunk(ids, &chunks))
        return B_BUFFER_OVERFLOW;

    for (size_t i = 0; i < chunks.size(); i++) {
        BString value(chunks[i].c_str());
        status_t status = node.WriteAttrString(idlist::ChunkName(base, i).c_str(), &value);
        if (status != B_OK)
            return status;
    }
    for (size_t i = chunks.size(); i < idlist::kMaxChunks; i++)
        node.RemoveAttr(idlist::ChunkName(base, i).c_str());   // not there is fine

    return B_OK;
}

/** @brief Add an id to the list of a file (no duplicates). @return B_OK or B_BUFFER_OVERFLOW if the list is full. */
inline status_t
AddToIdList(BNode& node, const char* base, const char* id)
{
    std::vector<std::string> ids;
    status_t status = ReadIdList(node, base, &ids);
    if (status != B_OK)
        return status;
    if (! idlist::Add(&ids, id))
        return B_OK;
    return WriteIdList(node, base, ids);
}

/** @brief Remove an id from the list of a file. @return B_OK, also if it was not in the list. */
inline status_t
RemoveFromIdList(BNode& node, const char* base, const char* id)
{
    std::vector<std::string> ids;
    status_t status = ReadIdList(node, base, &ids);
    if (status != B_OK)
        return status;
    if (! idlist::Remove(&ids, id))
        return B_OK;
    return WriteIdList(node, base, ids);
}

}   // namespace sen
