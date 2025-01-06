/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _RELATIONS_HANDLER_H
#define _RELATIONS_HANDLER_H

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
		const char*		     		GetOrCreateId          (const char *path, bool createIfMissing = false);
		status_t                    QueryById              (const char* id, BEntry* entry);

        const char*                 GetMimeTypeForPath     (const char* path);
        status_t                    ResolveSelfRelationsWithPlugin(const char* pluginSig, const char* source, BMessage* reply);

virtual
        void MessageReceived(BMessage* message);
		~RelationsHandler();

protected:
        status_t            GetPluginsForType(const char* mimeType, BMessage* outputTypesToPlugins);
        status_t            GetPluginConfig(const char* pluginSig, entry_ref* pluginRef,
                                            const char* mimeType, BMessage* pluginConfig);
        status_t            AddTypesToPluginsConfig(BMessage *pluginConfig);

private:
		BMessage*           ReadRelationsOfType(const char* path, const char* relationType, BMessage* reply);
		BStringList*        ReadRelationNames(const char* path);
		status_t            ResolveRelationTargets(BStringList* ids, BObjectList<BEntry> *result);
		// write/delete
		status_t			WriteRelationToFile(const char *path, const char *relationType,
                                const BMessage* relationConfig);
		status_t            RemoveRelationForTypeAndTargetFromFile(const char* path,
                                const char *relationType, const char *targetId);
		status_t            RemoveAllRelationsFromFile(const char* path);

		// helper methods
        BString*            StripSuperType(BString* type);
        status_t            GetMessageParameter(const BMessage* message, BMessage* reply, const char* param,
                                BString* buffer, bool mandatory = true, bool stripSuperType = true);
        const char*         GetAttributeNameForRelation(const char* relationType);
		static bool         AddOrUpdateRelationTarget(const char* relationType,
                                BMessage* newRelationTarget, BMessage* existingRelation);

        IceDustGenerator*   tsidGenerator;
};

#endif // _RELATION_SERVICE_H
