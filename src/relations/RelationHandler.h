/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#pragma once

#include <Application.h>
#include <File.h>
#include <Message.h>
#include <Mime.h>
#include <ObjectList.h>
#include <Query.h>
#include <StringList.h>

#include <vector>

#include <sen/Sensei.h>


namespace sen { class AttrSnapshot; }

class RelationHandler : public BHandler {

public:
        RelationHandler();

        status_t    AddRelation             (const BMessage* message, BMessage* reply);
        status_t    GetCompatibleRelations  (const BMessage* message, BMessage* reply);
        status_t    GetCompatibleTargetTypes(const BString&  relationType, bool withConfigs, BMessage* reply);
        status_t    GetRelationsOfType      (const BMessage* message, BMessage* reply);
        status_t    GetAllRelations         (const BMessage* message, BMessage* reply);
        status_t    GetSelfRelations        (const BMessage* message, BMessage* reply);
        status_t    GetSelfRelationsOfType  (const BMessage* message, BMessage* reply);
        /** remove a relation (and its opposite direction), see RelationWrite.cpp */
        status_t    RemoveRelation          (const BMessage* message, BMessage* reply);
        /** change the properties of a relation, or move it to another target or type */
        status_t    UpdateRelation          (const BMessage* message, BMessage* reply);
        /** remove all relations of a file (of one type if given) */
        status_t    RemoveAllRelations      (const BMessage* message, BMessage* reply);

        /** @return a new TSID, see sen::id::New() */
        BString     GenerateId();
        status_t    GetOrCreateId           (const entry_ref* ref, char* id, bool createIfMissing = false);
        status_t    QueryForUniqueSenId     (const char* sourceId, entry_ref* ref);
        /** @brief All files that carry a `SEN:ID`, on all volumes: more than one means copies. */
        status_t    QueryAllForSenId        (const char* senId, std::vector<entry_ref>* refs);
        status_t    QueryForTargetsById     (const char* sourceId, BMessage* idToRef);

        const char* GetMimeTypeForRef       (const entry_ref* ref);
        status_t    ResolveSelfRelationsWithPlugin(const char* pluginSig, const entry_ref* sourceRef,
                                                   const BMessage* pluginConfig,
                                                   BMessage* reply);

virtual
        void MessageReceived(BMessage* message);
        ~RelationHandler();

protected:
        status_t    GetPluginsForTypeAndFeature(const char* mimeType, const char* feature, BMessage* outputTypesToPlugins);
        status_t    GetPluginConfig(const char* pluginSig, entry_ref* pluginRef,
                                            const char* mimeType, BMessage* pluginConfig);
        /**
         * collect relation configs from input types and store in provides message keyed by type.
         *
         * @param  types            relation mime types to query
         * @param  relationConfigs  result message that will hold all configs found.
         * @return B_OK or the error of the last failed API call.
         */
        status_t    GetRelationConfigs(const BStringList* types, BMessage* relationConfigs);
        /**
         * single param version.
         * @see #GetRelationConfigs
         */
        status_t    GetRelationConfig(const char* mimeType, BMessage* relationConfig);
        /**
         * finds an installed navigator plugin (SEN:plugin:navigate == 1) whose
         * declared file_types include relationType, and assigns it as that
         * relation type's preferred app - needed for self relations to be
         * openable at all, see TTracker::HandleSenMessage's self-relation branch.
         */
        status_t    AssignPreferredNavigator(BMimeType* relationType);
        status_t    GetAttrMessage(const BNode* node, const char* name, BMessage* attrMessage);
        status_t    AddTypesToPluginsConfig(BMessage *pluginConfig);
        status_t    TransformPluginResult(const BMessage *pluginReply,
                                          const BMessage* typeMapping,    // TODO: not yet handled here
                                          const BMessage* attrMapping,
                                          BMessage *pluginResult);

private:
        status_t    ReadRelationsOfType(const entry_ref* ref, const char* relationType, BMessage* relations,
                                                BMessage* idToRefMap = NULL, BStringList* targetIds = NULL);
        status_t    ReadRelationNames(const entry_ref* ref, BStringList* relations);
        status_t    ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs);
        status_t    ResolveRelationPropertyTargetIds(const BMessage* relationProperties, BStringList* ids);

        // write/delete (RelationWrite.cpp); every operation is all-or-nothing, see sen::AttrSnapshot
        status_t    ReadRelationMessage(const entry_ref& ref, const char* relationType, BMessage* relations);
        status_t    StoreRelationMessage(const entry_ref& ref, const char* relationType, const BMessage& relations,
                                         sen::AttrSnapshot* tx);
        status_t    ChangeTargetList(const entry_ref& ref, const char* relationType, const char* targetId, bool add,
                                     sen::AttrSnapshot* tx);
        status_t    AddRelationTx(const entry_ref& source, const entry_ref& target, const char* relationType,
                                  const BMessage& properties, const BMessage& relationConfig, sen::AttrSnapshot* tx,
                                  BString* relationId, bool* created);
        status_t    RemoveRelationTx(const entry_ref& source, const char* relationType, const char* targetId,
                                     const char* relationId, bool allSets, sen::AttrSnapshot* tx, BString* removedRelationId);
        status_t    GetTargetIdParameter(const BMessage* message, BString* targetId);
        bool        ShouldWriteInverse(const BMessage& relationConfig, const BString& sourceType, const BString& targetType);
        BMessage    InverseProperties(const BMessage& relationConfig, const BMessage& properties);
        /** the relations of unidirectional types that point to a file, found by query for its ID in SEN:META / SEN:TO */
        status_t    ResolveReverseRelations(const entry_ref* ref, const char* relationType, const BMessage& relationConfig,
                                            BMessage* relations, BMessage* idToRefMap);

        // helper methods
        status_t    GetSubtype(const BString* type, BString* subtype);
        status_t    GetTypeForRef(entry_ref* ref, BString* mimeType);
        status_t    GetInodeForRef(const entry_ref* srcRef, BString* inode);
        status_t    GetMessageParameter(const BMessage* message, const char* param,
                                        BString* buffer = NULL, entry_ref* ref = NULL,
                                        bool mandatory = true);
        void        GetAttributeNameForRelation(const char* relationType, BString* attrName);

};
