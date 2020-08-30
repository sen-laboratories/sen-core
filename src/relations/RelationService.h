/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _RELATION_SERVICE_H
#define _RELATION_SERVICE_H

#include <File.h>
#include <Message.h>

class RelationService {

const char* SEN_RELATION_SOURCE = "source";
const char* SEN_RELATION_NAME   = "relation";
const char* SEN_RELATION_TARGET = "target";

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
		status_t					_GetAllRelationsForFile	(const BFile& file);
		status_t					_GetRelationForFile		(const BFile& file);
		status_t					_WriteRelationToFile	(const BFile& file);
};

#endif // _RELATION_SERVICE_H
