/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>
#include <Node.h>
#include <String.h>
#include <fs_attr.h>

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
 * @brief Does a plugin declare a feature? The flag is the 16 bit attribute `SEN:plugin:<feature>`.
 *
 * BFS cannot index 16 bit values, so plugins are found by their type (indexed) and filtered with this. Plugins that were
 * built with a 32 bit flag are understood, too.
 *
 * @param plugin  the plugin file
 * @param feature one of sensei::feature
 * @return true if the flag is set (greater than 0)
 */
inline bool
PluginHasFeature(const entry_ref& plugin, const char* feature)
{
    BNode node(&plugin);
    BString attrName;
    attrName << sensei::kFeatureAttrPrefix << ":" << feature;

    attr_info info;
    if (node.InitCheck() != B_OK || node.GetAttrInfo(attrName.String(), &info) != B_OK)
        return false;

    if (info.type == B_INT16_TYPE) {
        int16 value = 0;
        return node.ReadAttr(attrName.String(), B_INT16_TYPE, 0, &value, sizeof(value)) == (ssize_t) sizeof(value) && value > 0;
    }
    if (info.type == B_INT32_TYPE) {
        int32 value = 0;
        return node.ReadAttr(attrName.String(), B_INT32_TYPE, 0, &value, sizeof(value)) == (ssize_t) sizeof(value) && value > 0;
    }
    return false;
}

/**
 * @brief Find the plugins that declare a feature, on all mounted volumes.
 * @param feature one of sensei::feature
 * @param plugins receives the plugin files
 * @return B_OK, or the error of the query
 */
inline status_t
FindPlugins(const char* feature, std::vector<entry_ref>* plugins)
{
    BString predicate;
    predicate << sen::attr::kType << "==" << sen::mime::kPlugin;

    std::vector<entry_ref> all;
    status_t result = QueryAllVolumes(predicate.String(), &all);
    if (result != B_OK)
        return result;

    for (const entry_ref& plugin : all) {
        if (PluginHasFeature(plugin, feature))
            plugins->push_back(plugin);
    }
    return B_OK;
}

}   // namespace sen
