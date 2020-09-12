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
		BStringList* 			ReadRelationsFromAttrs(const char* path);
		BStringList* 			ReadRelationIdsFromFile(const char *path, const char* relation);
		BObjectList<BEntry>*	ResolveRelationTargets(BStringList* ids);
		const char*		 		GetIdForFile(const char *path);
		// write
		status_t 				WriteIdToFile(const char *path, const char *id);
		status_t				WriteRelationIds(const char *path, const char* relation, BStringList* ids);
		
		// helper methods
		static bool AppendIdToString(const BString& id, void* result);
		static bool QueryForId(const BString& id, void* targets);
		static bool AddRelationToMessage(const BString& relation, void* message);
		static BEntry* AddTargetToMessage(BEntry* entry, void* message);
};

#endif // _RELATION_SERVICE_H
