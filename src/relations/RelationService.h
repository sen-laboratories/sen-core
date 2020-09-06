/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _RELATION_SERVICE_H
#define _RELATION_SERVICE_H

#include <File.h>
#include <Message.h>
#include <ObjectList.h>
#include <StringList.h>

class RelationService {

#define SEN_RELATION_SOURCE "source"
#define SEN_RELATION_NAME   "relation"
#define SEN_RELATION_TARGET "target"

#define SEN_ATTRIBUTES_PREFIX		"SEN:"
#define SEN_RELATION_ID				SEN_ATTRIBUTES_PREFIX "_id"
#define SEN_RELATION_ID_SEPARATOR	","

// Message Replies
enum {
	SEN_RESULT_RELATIONS		= 'SCre'
};

public:
		RelationService();
		status_t					AddRelation				(const BMessage* message, BMessage* reply);
		status_t					GetRelations			(const BMessage* message, BMessage* reply);
		status_t					GetTargetsForRelation	(const BMessage* message, BMessage* reply);
		status_t					RemoveRelation			(const BMessage* message, BMessage* reply);		
		status_t					RemoveAllRelations		(const BMessage* message, BMessage* reply);		

virtual
		~RelationService();

private:
		BStringList* 			GetRelationsFromAttrs(const char* path);
		BStringList* 			GetRelationIds(const char *path, const char* relation);
		BObjectList<BEntry>*	ResolveRelationTargets(BStringList* ids);
		const char*		 		GetIdForFile(const char *path);
		
		// helper methods
		static bool QueryForId(const BString& id, void* targets);
		static bool AddRelationToMessage(const BString& relation, void* message);
		static BEntry* AddTargetToMessage(BEntry* entry, void* message);

};

#endif // _RELATION_SERVICE_H
