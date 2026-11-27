/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Node.h>
#include <fs_attr.h>
#include <String.h>

#include <sen/Sen.h>
#include <Query.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <spdlog/spdlog.h>

#include <functional>
#include <vector>

/**
 * @file QueryUtil.h
 * @brief Queries across all mounted volumes.
 *
 * A BQuery works on one volume only (BQuery::SetVolume). SEN's objects can be on any mounted volume, so queries for
 * `SEN:ID`, `SEN:TO`, plugins and so on have to ask all of them.
 */
namespace sen {

/**
 * @brief Read the entries of a query that was fetched, and do not stop at an entry that cannot be read.
 *
 * An index can hold entries of files that are gone (after a crash or a forced stop of the machine BFS logs "inode ... is already deleted" and
 * "IndexIteratorGetEntry: Bad data"): GetNextRef() then fails for that entry instead of ending the list, and a loop that ends at the first
 * failure loses every entry behind it without a word. Such an entry is counted and skipped; only the end of the list (B_ENTRY_NOT_FOUND) ends the
 * loop, or many failures in a row (the iterator does not move on: the rest of the index is not readable). Check & Repair of the SEN menu makes
 * the index again (sen::stats::RepairIndices).
 *
 * @param query  a query on which Fetch() succeeded
 * @param each   called with every entry that was read
 * @param errors receives the number of entries that could not be read (may be NULL)
 * @return the number of entries read
 */
inline int32
ReadQuery(BQuery& query, const std::function<void(const entry_ref&)>& each, int32* errors = NULL)
{
    static const int32 kMaxFailuresInARow = 20;

    int32 count = 0;
    int32 failed = 0;
    int32 inARow = 0;
    entry_ref ref;
    for (;;) {
        status_t result = query.GetNextRef(&ref);
        if (result == B_OK) {
            inARow = 0;
            count++;
            each(ref);
            continue;
        }
        if (result == B_ENTRY_NOT_FOUND)
            break;
        failed++;
        if (++inARow >= kMaxFailuresInARow)
            break;
    }
    if (errors != NULL)
        *errors = failed;
    return count;
}

/**
 * @brief Run a query on every mounted volume that supports queries and collect what it finds.
 *
 * A volume on which the query cannot be run (not supported, or no index for the attribute) is logged and skipped:
 * one volume must not hide the results of the others. Entries of an index that cannot be read are skipped, too (see ReadQuery), and logged.
 *
 * @param predicate  the query predicate, see BQuery::SetPredicate
 * @param refs       receives the entries found, in the order of the volumes
 * @param unreadable receives the number of index entries that could not be read, on all volumes (may be NULL): not 0 means that the answer may be
 *                   incomplete, the index is damaged
 * @return B_OK if the query could be run on at least one volume, else the last error
 */
inline status_t
QueryAllVolumes(const char* predicate, std::vector<entry_ref>* refs, int32* unreadable = NULL)
{
    BVolumeRoster roster;
    BVolume volume;
    status_t lastError = B_ERROR;
    bool anyVolume = false;
    if (unreadable != NULL)
        *unreadable = 0;

    while (roster.GetNextVolume(&volume) == B_OK) {
        if (! volume.KnowsQuery())
            continue;

        BQuery query;
        status_t result = query.SetVolume(&volume);
        if (result == B_OK)
            result = query.SetPredicate(predicate);
        if (result == B_OK)
            result = query.Fetch();

        if (result != B_OK) {
            char name[B_FILE_NAME_LENGTH];
            volume.GetName(name);
            spdlog::warn("query '{}' failed on volume {}: {}", predicate, name, strerror(result));
            lastError = result;
            continue;
        }

        anyVolume = true;
        int32 failed = 0;
        ReadQuery(query, [refs](const entry_ref& ref) { refs->push_back(ref); }, &failed);
        if (failed > 0) {
            char name[B_FILE_NAME_LENGTH];
            volume.GetName(name);
            spdlog::warn("query '{}' on volume {}: {} index entries could not be read and were skipped, the index is damaged (Check & Repair in the SEN menu repairs it)",
                predicate, name, failed);
            if (unreadable != NULL)
                *unreadable += failed;
        }
    }

    return anyVolume ? B_OK : lastError;
}

/**
 * @brief Find the plugins that declare a feature, on all mounted volumes.
 *
 * The signature of a plugin starts with sensei::kSignaturePrefix, and `BEOS:APP_SIG` is the one attribute that can be queried
 * everywhere: it is indexed on BFS volumes and on packagefs, where the files of installed packages are (packagefs has no other
 * indices than name, size, last modified and the signature, and cannot create any, so a query for `META:TYPE` would not find a
 * packaged plugin). The candidates are then checked by reading their attributes: the plugin type (`META:TYPE`) and the feature
 * flag (`SEN:plugin:<feature>`, a number that is not 0).
 *
 * @param feature one of sensei::feature
 * @param plugins receives the plugin files
 * @return B_OK, or the error of the query
 */
inline status_t
FindPlugins(const char* feature, std::vector<entry_ref>* plugins)
{
    BString predicate;
    predicate << "BEOS:APP_SIG==" << sensei::kSignaturePattern;

    std::vector<entry_ref> candidates;
    status_t result = QueryAllVolumes(predicate.String(), &candidates);
    if (result != B_OK)
        return result;

    BString featureAttr;
    featureAttr << sensei::kFeatureAttrPrefix << ":" << feature;

    for (const entry_ref& candidate : candidates) {
        BNode node(&candidate);
        if (node.InitCheck() != B_OK)
            continue;

        BString type;
        if (node.ReadAttrString(sen::attr::kType, &type) != B_OK || type != sen::mime::kPlugin)
            continue;

        // the flag is a number of 16 or 32 bit
        attr_info info;
        int32 flag = 0;
        if (node.GetAttrInfo(featureAttr.String(), &info) != B_OK || info.size < 1 || info.size > (off_t) sizeof(flag)
                || node.ReadAttr(featureAttr.String(), info.type, 0, &flag, info.size) != info.size || flag == 0)
            continue;

        plugins->push_back(candidate);
    }
    return B_OK;
}

}   // namespace sen
