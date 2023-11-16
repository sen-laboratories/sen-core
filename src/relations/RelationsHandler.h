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
#include <StringList.h>

#define SEN_RELATION_SOURCE "source"
#define SEN_RELATION_NAME   "relation"
#define SEN_RELATION_TARGET "target"

#define SEN_RELATIONS_GET			'SCrg'
#define SEN_RELATIONS_GET_TARGETS	'SCrt'
#define SEN_RELATIONS_ADD			'SCra'
#define SEN_RELATIONS_REMOVE		'SCrr'
#define SEN_RELATIONS_REMOVEALL		'SCrd'

class RelationsHandler : public BHandler {

public:
		RelationsHandler();
        
		status_t					AddRelation				(const BMessage* message, BMessage* reply);
		status_t					GetRelations			(const BMessage* message, BMessage* reply);
		status_t					GetTargetsForRelation	(const BMessage* message, BMessage* reply);
		status_t					RemoveRelation			(const BMessage* message, BMessage* reply);		
        // delete all relations of a given type, e.g. when a related file is deleted
		status_t					RemoveAllRelations		(const BMessage* message, BMessage* reply);		

virtual
        void MessageReceived(BMessage* message);
		~RelationsHandler();

private:
		BStringList* 			ReadRelationsFromAttrs(const char* path);
		BStringList* 			ReadRelationIdsFromFile(const char *path, const char* relation);
		BObjectList<BEntry>*	ResolveRelationTargets(BStringList* ids);
		const char*		 		GetIdForFile(const char *path);
		// write
		status_t 				WriteIdToFile(const char *path, const char *id);
		status_t				WriteRelationIdsToFile(const char *path, const char* relation, BStringList* ids);
		
		// helper methods
		static bool             AppendIdToString(const BString& id, void* result);
		static bool             QueryForId(const BString& id, void* targets);
		static const char*      GetRelationAttributeName(const char* relation);
		static bool             AddRelationToMessage(const BString& relation, void* message);
		static BEntry*          AddTargetToMessage(BEntry* entry, void* message);
};

#endif // _RELATION_SERVICE_H
