/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2020-2026 SEN Labs e.U.
 */

#pragma once

#include <Application.h>
#include <File.h>
#include <Message.h>
#include <Mime.h>
#include <ObjectList.h>
#include <Query.h>
#include <StringList.h>

#include <string>
#include <vector>

#include <sen/Sensei.h>


namespace sen { class AttrSnapshot; }
class SenConfigHandler;

class RelationHandler : public BHandler {

public:
        /** @brief Give every installed relation type that has no preferred app the navigator that declares it, or else the one for all
         *  relations (the supertype), see AssignPreferredNavigator. Done when the server starts. */
        void        AssignMissingNavigators();
        RelationHandler();
        /** the settings of the server: the current context and whether it counts as sticky (SEN:associateCurrentContext) */
        void        SetConfig(SenConfigHandler* config) { fConfig = config; }

        status_t    AddRelation             (const BMessage* message, BMessage* reply);
        status_t    GetCompatibleRelations  (const BMessage* message, BMessage* reply);
        status_t    GetCompatibleTargetTypes(const BString&  relationType, bool withConfigs, BMessage* reply);
        status_t    GetRelationsOfType      (const BMessage* message, BMessage* reply);
        status_t    GetAllRelations         (const BMessage* message, BMessage* reply);
        /** the same for several files (SEN:sourceRef, repeated), see RelationHandler.cpp: what the files have, as far as it fits all of them */
        status_t    GetAllRelationsOfSelection      (const BMessage* message, const std::vector<entry_ref>& sources, BMessage* reply);
        status_t    GetRelationsOfTypeOfSelection   (const BMessage* message, const std::vector<entry_ref>& sources, BMessage* reply);
        status_t    GetSelfRelations        (const BMessage* message, BMessage* reply);
        status_t    GetSelfRelationsOfType  (const BMessage* message, BMessage* reply);
        /** remove a relation (and its opposite direction), see RelationWrite.cpp */
        status_t    RemoveRelation          (const BMessage* message, BMessage* reply);
        /** change the properties of a relation, or move it to another target or type */
        status_t    UpdateRelation          (const BMessage* message, BMessage* reply);
        /** remove all relations of a file (of one type if given) */
        status_t    RemoveAllRelations      (const BMessage* message, BMessage* reply);
        /** put files (refs) into the sticky classifications, see RelationSticky.cpp */
        status_t    ApplySticky             (const BMessage* message, BMessage* reply);
        /** the classifications that are sticky now (of the contexts only the current one), and their SEN:IDs */
        status_t    StickyClassifications   (std::vector<entry_ref>* result);
        status_t    StickyClassificationIds (std::vector<std::string>* ids);

        /** @return a new TSID, see sen::id::New() */
        BString     GenerateId();
        status_t    GetOrCreateId           (const entry_ref* ref, char* id, bool createIfMissing = false);
        status_t    QueryForUniqueSenId     (const char* sourceId, entry_ref* ref);
        /** @brief All files that carry a `SEN:ID`, on all volumes: more than one means copies. */
        status_t    QueryAllForSenId        (const char* senId, std::vector<entry_ref>* refs);
        status_t    QueryForTargetsById     (const char* sourceId, BMessage* idToRef);

        /** @return the MIME type of a file, guessed if it has none; empty if there is none */
        BString     GetMimeTypeForRef       (const entry_ref* ref);
        /**
         * @return the types that a file is looked up by: its technical type (the MIME type, `application/pdf`), then its semantic type (`META:TYPE`, `entity/...`, `document/...`) if it has
         * one that is another. The types of a relation (SEN:sourceTypes...) and of a plugin (its file types) are matched against both: whatever they name is meant.
         */
        std::vector<BString> GetTypesForRef (const entry_ref* ref);
        status_t    ResolveSelfRelationsWithPlugin(const char* pluginSig, const entry_ref* sourceRef,
                                                   const BMessage* pluginConfig,
                                                   BMessage* reply);
        /**
         * @brief The files that a dynamic relation (not a self relation: the spatial ones) relates a file to, asked of the navigator plugin that handles the type.
         *
         * The relations of that type are not stored anywhere: the plugin works them out when they are asked for (sensei::cmd::kResolve, see Sensei.h) and the answer is
         * made like that of a stored relation: a set of properties for each target in @p relations, the refs of the targets in @p idToRefMap. A target gets a `SEN:ID` if it has none.
         * @return B_OK (also if nothing is found), B_NAME_NOT_FOUND if no navigator handles the type, or the error of the plugin
         */
        status_t    ResolveDynamicRelationsWithPlugin(const entry_ref* sourceRef, const char* relationType, BMessage* relations, BMessage* idToRefMap);
        /**
         * @brief The Library folders of the entity types that a relation is between or stands for (the source types, the target types, `SEN:materialiseAs`): a field for each type that
         * holds the folder as a ref. A plugin that makes entities is given them (sensei::key::kLibraries): it cannot ask the server, which waits for its answer.
         */
        void        GetLibrariesFor(const char* relationType, BMessage* libraries);
        /**
         * @brief Makes the entities that the items of a relation with `SEN:materialiseAs` stand for (sen::cmd::kRelationsMaterialise), by the plugin of the relation (sensei::cmd::kMaterialise).
         * The reply has a sensei::key::kTargets message for each item that has an entity (its ref and `SEN:ID`, the item).
         * @return B_NOT_SUPPORTED if the relation has no `SEN:materialiseAs`
         */
        status_t    MaterialiseItems(const BMessage* message, BMessage* reply);
        /** takes away the stored relations from @p source to entities of the same item (all the properties in @p item are those of the set) that are gone (not found, or in the Trash), except the one to @p keepTargetId */
        void        RemoveStaleSets(const entry_ref& source, const char* relationType, const char* keepTargetId, const BMessage& item);

/**
 * The requests that need a plugin are handed to the channel of that plugin (src/plugins/PluginChannels.h) and answered from its thread; the others are handled at once.
 */
virtual
        void MessageReceived(BMessage* message);
        ~RelationHandler();

protected:
        /** handles a request and sends the reply (in the thread that calls it) */
        void        HandleRequest(BMessage* message);
        /** the signature of the plugin that a request needs, or the name of the channel for the plugins of a kind if it is not known yet; empty if the request needs no plugin */
        BString     PluginSignatureFor(const BMessage* message);
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
        SenConfigHandler*   fConfig = NULL;
        status_t    ReadRelationsOfType(const entry_ref* ref, const char* relationType, BMessage* relations,
                                                BMessage* idToRefMap = NULL, BStringList* targetIds = NULL);
        status_t    ReadRelationNames(const entry_ref* ref, BStringList* relations);
        status_t    ResolveRelationTargets(BStringList* ids, BMessage *idsToRefs);
        /** the name to show for a target: its title, the short description of a MIME type, else its file name */
        static BString DisplayNameOf(const entry_ref& ref);
        status_t    ResolveRelationPropertyTargetIds(const BMessage* relationProperties, BStringList* ids);

        // write/delete (RelationWrite.cpp); every operation is all-or-nothing, see sen::AttrSnapshot
        status_t    ReadRelationMessage(const entry_ref& ref, const char* relationType, BMessage* relations);
        status_t    StoreRelationMessage(const entry_ref& ref, const char* relationType, const BMessage& relations,
                                         sen::AttrSnapshot* tx);
        status_t    ChangeTargetList(const entry_ref& ref, const char* relationType, const char* targetId, bool add,
                                     sen::AttrSnapshot* tx);
        status_t    AddRelationTx(const entry_ref& source, const entry_ref& target, const char* relationType,
                                  const BMessage& properties, const BMessage& relationConfig, sen::AttrSnapshot* tx,
                                  BString* relationId, bool* created, const BMessage* inverseProperties = NULL);
        status_t    RemoveRelationTx(const entry_ref& source, const char* relationType, const char* targetId,
                                     const char* relationId, bool allSets, sen::AttrSnapshot* tx, BString* removedRelationId,
                                     bool allowReadOnly = false);
        status_t    GetTargetIdParameter(const BMessage* message, BString* targetId);
        bool        ShouldWriteInverse(const BMessage& relationConfig, const BString& sourceType, const BString& targetType);
        BMessage    InverseProperties(const BMessage& relationConfig, const BMessage& properties);
        /** the relations of unidirectional types that point to a file, found by query for its ID in SEN:META / SEN:TO */
        status_t    ResolveReverseRelations(const entry_ref* ref, const char* relationType, const BMessage& relationConfig,
                                            BMessage* relations, BMessage* idToRefMap);
        /** whether a file of the ID of this one lists it as a target of a relation of the type (what ResolveReverseRelations() would find) */
        bool        HasReverseRelations(const entry_ref* ref, const char* relationType);

        // helper methods
        status_t    GetSubtype(const BString* type, BString* subtype);
        status_t    GetTypeForRef(entry_ref* ref, BString* mimeType);
        status_t    GetInodeForRef(const entry_ref* srcRef, BString* inode);
        status_t    GetMessageParameter(const BMessage* message, const char* param,
                                        BString* buffer = NULL, entry_ref* ref = NULL,
                                        bool mandatory = true);
        void        GetAttributeNameForRelation(const char* relationType, BString* attrName);

};
