/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
 
#include "RelationService.h"

#include <String.h>

RelationService::RelationService()
{
}

RelationService::~RelationService()
{
}

status_t RelationService::AddRelation(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	BString target;
	if (message->FindString(SEN_RELATION_TARGET, &target) != B_OK) {
		return B_BAD_VALUE;
	}
	
	// TODO file attribute linking magic

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("created relation ") << relation << " from " << source << " -> "<<  target);
	
	return B_OK;
}

status_t RelationService::GetRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	
	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("get relations for ")  << source);
	
	return B_OK;
}

status_t RelationService::GetTargetsForRelation(const BMessage* message, BMessage* reply)
{	
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	
	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("get targets for relation ") << relation << " from " << source);
	
	return B_OK;
}

status_t RelationService::RemoveRelation(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("removed relation ") << relation << " from " << source);
	
	return B_OK;
}

status_t RelationService::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("removed all relations from ") << source);
	
	return B_OK;
}

// private methods

status_t	_GetAllRelationsForFile(const BFile& file) { return B_OK; }
status_t	_GetRelationForFile(const BFile& file) { return B_OK; }
status_t	_WriteRelationToFile(const BFile& file) { return B_OK; }
