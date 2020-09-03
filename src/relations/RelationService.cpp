/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
 
#include "RelationService.h"

#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <stdio.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

# define LOG(x...)			printf(x);
# define ERROR(x...)		fprintf(stderr, x);

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
	
	BObjectList<BEntry> targetEntries = GetRelationTargets(source, relation);
	LOG("Adding to result message:\n");
	targetEntries.EachElement(AddToMessage, reply);

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("found ") << targetEntries.CountItems()
		<< " targets for relation " << relation << " from " << source);
		
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

BObjectList<BEntry> RelationService::GetRelationTargets(const char *path, const char *relation)
{
	BNode node(path);
	BObjectList<BEntry> result;
	
	if (node.InitCheck() != B_OK)
		 return result;

	BString relationTargets;
	node.ReadAttrString(relation, &relationTargets);
	
	result = ResolveIds(relationTargets);
	return result;
}

BObjectList<BEntry> RelationService::ResolveIds(const BString& idsStr)
{
	LOG("resolving targets for file IDs %s...\n", idsStr.String());
	BObjectList<BEntry> targets;
	BStringList ids;
	
	if (idsStr.Split(",", true, ids)) {
		ids.DoForEach(QueryForId, &targets);
	}
	
	return targets;
}

bool RelationService::QueryForId(const BString& id, void* result)
{
	BString predicate(BString("SEN:_id == ") << id);
	// all relation queries currently assume we never leave the boot volume
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());
	
	if (query.Fetch() == B_OK)
	{
		LOG("Results of query \"%s\":\n", predicate.String());
		BEntry* entry = new BEntry();
		
		// if not while becaues result should always only contain a single id (inode)
		// else, the relation references are corrupt and need to be repaired.
		if (query.GetNextEntry(entry) == B_OK)
		{
			BPath path;
			entry->GetPath(&path);
			LOG("\t%s\n", path.Path());
			
			reinterpret_cast<BObjectList<BEntry>*>(result)->AddItem(entry);
		}
		else
			ERROR("error resolving id %s", id);
	}
	
	return true;
}

BEntry* RelationService::AddToMessage(BEntry* entry, void* message)
{
	BPath path;
	entry->GetPath(&path);
	LOG("\t%s\n",path.Path());
	reinterpret_cast<BMessage*>(message)->AddString("targets", path.Path());
	
	return NULL;
}
