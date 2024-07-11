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
		const char*		     		GetOrCreateId          (const char *path, bool createIfMissing = false);
		status_t                    QueryById              (const char* id, BEntry* entry);

virtual
        void MessageReceived(BMessage* message);
		~RelationsHandler();

protected:
        status_t            SearchPluginsForType(const char* mimeType, BMessage* typesPlugins);
        status_t            GetSenseiOutputTypes(const BEntry* src, BStringList* outputTypes);

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
        const char*         GenerateId(BNode* node);
        const char*         GetAttributeNameForRelation(const char* relationType);
		static bool         AddOrUpdateRelationTarget(const char* relationType,
                                BMessage* newRelationTarget, BMessage* existingRelation);
};

#endif // _RELATION_SERVICE_H
