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

class RelationsHandler : public BHandler {

public:
		RelationsHandler();

		status_t					AddRelation            (const BMessage* message, BMessage* reply);
		status_t		    		GetRelationsOfType     (const BMessage* message, BMessage* reply);
		status_t					GetAllRelations        (const BMessage* message, BMessage* reply);
        status_t                    GetSelfRelations       (const BMessage* message, BMessage* reply);
        status_t                    GetSelfRelationsOfType (const BMessage* message, BMessage* reply);
		status_t					RemoveRelation         (const BMessage* message, BMessage* reply);
        // delete all relations of a given type, e.g. when a related file is deleted
		status_t					RemoveAllRelations     (const BMessage* message, BMessage* reply);

        const char*                 GenerateId();
		status_t    	     		GetOrCreateId          (const entry_ref* ref, char* id, bool createIfMissing = false);
		status_t                    QueryById              (const char* id, BEntry* entry);

        const char*                 GetMimeTypeForRef      (const entry_ref* ref);
        status_t                    ResolveSelfRelationsWithPlugin(const char* pluginSig,
                                        const entry_ref* sourceRef, BMessage* reply);

virtual
        void MessageReceived(BMessage* message);
		~RelationsHandler();

protected:
        status_t            GetPluginsForType(const char* mimeType, BMessage* outputTypesToPlugins);
        status_t            GetPluginConfig(const char* pluginSig, entry_ref* pluginRef,
                                            const char* mimeType, BMessage* pluginConfig);
        status_t            AddTypesToPluginsConfig(BMessage *pluginConfig);

private:
		status_t            ReadRelationsOfType(const entry_ref* ref, const char* relationType, BMessage* relations);
		BStringList*        ReadRelationNames(const entry_ref* ref);
		status_t            ResolveRelationTargets(BStringList* ids, BObjectList<BEntry> *result);

		// write/delete
		status_t			WriteRelation(const entry_ref *ref, const char *relationType,
                                const BMessage* relationConfig);
		status_t            RemoveRelationForTypeAndTarget(const entry_ref *ref,
                                const char *relationType, const char *targetId);
		status_t            RemoveAllRelations(const entry_ref *ref);

		// helper methods
        BString*            StripSuperType(BString* type);
        status_t            GetMessageParameter(const BMessage* message, BMessage* reply, const char* param,
                                BString* buffer = NULL, entry_ref* ref = NULL,
                                bool mandatory = true, bool stripSuperType = true);
        status_t            GetPathOrRef(const BMessage* message, BMessage* reply, const char* param, entry_ref* ref);
        const char*         GetAttributeNameForRelation(const char* relationType);
		//static bool         AddOrUpdateRelationTarget(const char* relationType,
        //                        BMessage* newRelationTarget, BMessage* existingRelation);

        status_t            GetRelationMimeConfig(const char* mimeType, BMessage* relationConfig);
        IceDustGenerator*   tsidGenerator;
};
