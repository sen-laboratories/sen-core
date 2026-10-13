/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */

/**
 * @file RelationSticky.cpp
 * @brief Sticky classifications: what is made new is put into them automatically.
 *
 * A classification (label, topic, project...) that has the attribute SEN:sticky (the user locks it with Shift+click in the menu of SEN) and the current
 * context are applied to every file that the file manager has made
 * (a new file, a new related file) and to every relation that is made: the file gets a relation of the association type to it, the relation lists the
 * classification in its properties (key::kClassifications), so that they can be filtered by it later. Of the contexts only the current one counts
 * (the attribute is not looked at: choosing a context is what makes it stick).
 */

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>

#include <spdlog/spdlog.h>

#include <sen/Sen.h>

#include "AttrSnapshot.h"
#include "QueryUtil.h"
#include "RelationHandler.h"
#include "../server/Reply.h"

namespace {

/** The name of the current context: the one that is set in the settings of SEN, else the global one. */
BString
CurrentContext()
{
    BString current(sen::config::kContextGlobal);
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK && path.Append("sen") == B_OK
            && path.Append(sen::config::kSettingsFile) == B_OK) {
        BFile file(path.Path(), B_READ_ONLY);
        BMessage settings;
        if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK)
            current = settings.GetString(sen::key::kCurrentContext, sen::config::kContextGlobal);
    }
    return current;
}

/** whether a file is in the Trash (a classification that was deleted is not applied any more) */
bool
InTrash(const entry_ref& ref)
{
    BPath trash, path(&ref);
    if (find_directory(B_TRASH_DIRECTORY, &trash, false, NULL) != B_OK)
        return false;
    BString where(path.Path());
    return where.StartsWith(trash.Path());
}

}   // namespace

status_t RelationHandler::StickyClassifications(std::vector<entry_ref>* result)
{
    std::vector<entry_ref> candidates;
    BString predicate("BEOS:TYPE==\"");
    predicate << sen::mime::kClassificationPrefix << "*\"";
    status_t status = sen::QueryAllVolumes(predicate.String(), &candidates);
    if (status != B_OK)
        return status;

    BString current = CurrentContext();
    for (const entry_ref& candidate : candidates) {
        BNode node(&candidate);
        if (node.InitCheck() != B_OK || InTrash(candidate))
            continue;

        // the current context sticks by being the current one (not the global one, which is every file); the others when they were locked
        // (Shift+click in the menu of SEN), which sets their attribute
        entry_ref copy(candidate);
        if (GetMimeTypeForRef(&copy) == sen::mime::kContext) {
            if (current == candidate.name && current != sen::config::kContextGlobal)
                result->push_back(candidate);
            continue;
        }
        bool sticky = false;
        if (node.ReadAttr(sen::attr::kSticky, B_BOOL_TYPE, 0, &sticky, sizeof(sticky)) == sizeof(sticky) && sticky)
            result->push_back(candidate);
    }
    return B_OK;
}

status_t RelationHandler::StickyClassificationIds(std::vector<std::string>* ids)
{
    std::vector<entry_ref> sticky;
    status_t status = StickyClassifications(&sticky);
    if (status != B_OK)
        return status;
    for (const entry_ref& entity : sticky) {
        char id[sen::id::kLength];
        if (GetOrCreateId(&entity, id, true) == B_OK)
            ids->push_back(id);
    }
    return B_OK;
}

status_t RelationHandler::ApplySticky(const BMessage* message, BMessage* reply)
{
    std::vector<entry_ref> sticky;
    status_t status = StickyClassifications(&sticky);
    if (status != B_OK)
        return status;

    int32 applied = 0;
    if (!sticky.empty()) {
        BMessage config;
        status = GetRelationConfig(sen::mime::kAssociationRelation, &config);
        if (status != B_OK)
            return status;

        sen::AttrSnapshot tx;
        entry_ref file;
        for (int32 index = 0; message->FindRef(sen::key::kSourceRef, index, &file) == B_OK && status == B_OK; index++) {
            for (const entry_ref& entity : sticky) {
                if (file == entity)
                    continue;
                BString relationId;
                bool created = false;
                BMessage properties;
                status = AddRelationTx(file, entity, sen::mime::kAssociationRelation, properties, config, &tx, &relationId, &created, NULL);
                if (status != B_OK)
                    break;
                if (created)
                    applied++;
            }
        }
        if (status != B_OK) {
            tx.Restore();
            sen::reply::SetDetail(reply, (BString("could not apply the sticky classifications: ") << strerror(status)).String());
            return status;
        }
    }

    reply->what = sen::cmd::kReplyRelations;
    reply->AddInt32(sen::key::kCount, applied);
    sen::reply::SetDetail(reply, (BString("applied ") << applied << " sticky classification(s)").String());
    return B_OK;
}
