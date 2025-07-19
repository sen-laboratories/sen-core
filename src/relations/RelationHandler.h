/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#pragma once

#include <Application.h>
#include <File.h>
#include <Message.h>
#include <ObjectList.h>
#include <Query.h>
#include <StringList.h>

#include "IceDustGenerator.h"
#include "Sensei.h"

class RelationHandler : public BHandler {

public:
        RelationHandler();

        status_t                    AddRelation             (const BMessage* message, BMessage* reply);
        status_t                    GetCompatibleRelations  (const BMessage* message, BMessage* reply);
        status_t                    GetCompatibleTargetTypes(const BString&  relationType, BMessage* reply);
        status_t                    GetRelationsOfType      (const BMessage* message, BMessage* reply);
        status_t                    GetAllRelations         (const BMessage* message, BMessage* reply);
        status_t                    GetSelfRelations        (const BMessage* message, BMessage* reply);
        status_t                    GetSelfRelationsOfType  (const BMessage* message, BMessage* reply);
        status_t                    RemoveRelation          (const BMessage* message, BMessage* reply);
        // delete all relations of a given type, e.g. when a related file is deleted
        status_t                    RemoveAllRelations      (const BMessage* message, BMessage* reply);

        const char*                 GenerateId();
        status_t                    GetOrCreateId           (const entry_ref* ref, char* id, bool createIfMissing = false);
        status_t                    QueryForUniqueSenId     (const char* sourceId, entry_ref* ref);
        status_t                    QueryForTargetsById     (const char* sourceId, BMessage* idToRef);

        const char*                 GetMimeTypeForRef       (const entry_ref* ref);
        status_t                    ResolveInverseRelations (const entry_ref* sourceRef, BMessage* reply, const char* relationType = NULL);
        status_t                    ResolveSelfRelationsWithPlugin(const char* pluginSig,
                                                                   const entry_ref* sourceRef, BMessage* reply);

virtual
        void MessageReceived(BMessage* message);
        ~RelationHandler();

protected:
        status_t            GetPluginsForTypeAndFeature(const char* mimeType, const char* feature, BMessage* outputTypesToPlugins);
        status_t            GetPluginConfig(const char* pluginSig, entry_ref* pluginRef,
                                            const char* mimeType, BMessage* pluginConfig);
        status_t            AddTypesToPluginsConfig(BMessage *pluginConfig);

private:
        status_t            ReadRelationsOfType(const entry_ref* ref, const char* relationType, BMessage* relations,
                                                BMessage* idToRefMap = NULL, BStringList* targetIds = NULL);
        status_t            ReadRelationNames(const entry_ref* ref, BStringList* relations);
        status_t            ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs);
        status_t            ResolveRelationPropertyTargetIds(const BMessage* relationProperties, BStringList* ids);
        // write/delete
        status_t            WriteRelation(const entry_ref *srcRef, const char* targetId,
                                          const char *relationType, const BMessage* properties);
        status_t            RemoveRelationForTypeAndTarget(const entry_ref *ref, const char *relationType, const char *targetId);
        status_t            RemoveAllRelations(const entry_ref *ref);

        // helper methods
        status_t            GetSubtype(const BString* type, BString* subtype);
        status_t            GetTypeForRef(entry_ref* ref, BString* mimeType);
        status_t            GetMessageParameter(const BMessage* message, const char* param,
                                BString* buffer = NULL, entry_ref* ref = NULL,
                                bool mandatory = true, bool stripSuperType = true);
        void                GetAttributeNameForRelation(const char* relationType, char* attrName);
        status_t            AddRelationTargetIdAttr(BNode& node, const char* targetId, const BString& relationType);
        status_t            GetRelationMimeConfig(const char* mimeType, BMessage* relationConfig);

        IceDustGenerator*   tsidGenerator;
};
