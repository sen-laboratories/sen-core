/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <String.h>

#include <sen/Sen.h>
#include <Query.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <spdlog/spdlog.h>

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
 * @brief Run a query on every mounted volume that supports queries and collect what it finds.
 *
 * A volume on which the query cannot be run (not supported, or no index for the attribute) is logged and skipped:
 * one volume must not hide the results of the others.
 *
 * @param predicate the query predicate, see BQuery::SetPredicate
 * @param refs      receives the entries found, in the order of the volumes
 * @return B_OK if the query could be run on at least one volume, else the last error
 */
inline status_t
QueryAllVolumes(const char* predicate, std::vector<entry_ref>* refs)
{
    BVolumeRoster roster;
    BVolume volume;
    status_t lastError = B_ERROR;
    bool anyVolume = false;

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
        entry_ref ref;
        while (query.GetNextRef(&ref) == B_OK)
            refs->push_back(ref);
    }

    return anyVolume ? B_OK : lastError;
}

/**
 * @brief Find the plugins that declare a feature, on all mounted volumes.
 *
 * The feature flags (`SEN:plugin:<feature>`, int32) are not indexed. A BFS query needs only one indexed attribute, so
 * the plugin type (`META:TYPE`, indexed) must come first in the predicate; the flag is then compared on those files.
 *
 * @param feature one of sensei::feature
 * @param plugins receives the plugin files
 * @return B_OK, or the error of the query
 */
inline status_t
FindPlugins(const char* feature, std::vector<entry_ref>* plugins)
{
    BString predicate;
    predicate << sen::attr::kType << "==" << sen::mime::kPlugin << " && " << sensei::kFeatureAttrPrefix << ":" << feature << "==1";

    return QueryAllVolumes(predicate.String(), plugins);
}

}   // namespace sen
