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

// used in file types
#define SEN_RELATION_SUPERTYPE "relation/"

// used in messages
#define SEN_RELATION_SOURCE "SEN:source"
#define SEN_RELATION_NAME   "SEN:relation"
#define SEN_RELATION_TARGET "SEN:target"

#define SEN_RELATIONS_GET           'SCrg'
#define SEN_RELATIONS_GET_ALL       'SCrl'
#define SEN_RELATION_ADD	        'SCra'
#define SEN_RELATION_REMOVE	    	'SCrr'
#define SEN_RELATIONS_REMOVE_ALL    'SCrd'

class RelationsHandler : public BHandler {

public:
		RelationsHandler();

		status_t					AddRelation (BMessage* message, BMessage* reply);
		status_t		    		GetRelationsOfType  (const BMessage* message, BMessage* reply);
		status_t					GetAllRelations     (const BMessage* message, BMessage* reply);
		status_t					RemoveRelation      (const BMessage* message, BMessage* reply);
        // delete all relations of a given type, e.g. when a related file is deleted
		status_t					RemoveAllRelations  (const BMessage* message, BMessage* reply);
		static int32                QueryForId(const BString& id, BEntry* result);

virtual
        void MessageReceived(BMessage* message);
		~RelationsHandler();

private:
		BMessage*               ReadRelationsOfType(const char* path, const char* relationType);
		BStringList*            ReadRelationNames(const char* path);
		status_t            	ResolveRelationTargets(BStringList* ids, BObjectList<BEntry*> *result);
		const char*		 		GetOrCreateId(const char *path);
		// write/delete
		status_t				WriteRelationToFile(const char *path, const char *relationType, const BMessage* relationConfig);
		status_t                RemoveRelationForTypeAndTargetFromFile(const char* path, const char *relationType, const char *targetId);
		status_t                RemoveAllRelationsFromFile(const char* path);

		// helper methods
        const char*             GenerateId(BNode* node);
        const char*             GetAttributeNameForRelation(BString relationType);
		static bool             AddOrUpdateRelationTarget(const char* relationType, BMessage* newRelationTarget, BMessage* existingRelation);
};

#endif // _RELATION_SERVICE_H
