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
	if (message->FindString(SEN_RELATION_SOURCE, &source)   != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME,   &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	BString target;
	if (message->FindString(SEN_RELATION_TARGET, &target)   != B_OK) {
		return B_BAD_VALUE;
	}
	
	const char* srcId  	 = GetIdForFile(source);
	const char* targetId = GetIdForFile(target);
	
	LOG("creating relation from \"%s\"(\"%d\") -> \"%s\"(\"%d\")\n", source, srcId, target, targetId);
	
	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("created relation ") << relation << " from "
		<< source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]");
	
	return B_OK;
}

status_t RelationService::GetRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}

	BStringList* relations = GetRelationsFromAttrs(source);
	LOG("Adding to result message:\n");
	relations->DoForEach(AddRelationToMessage, reply);
	
	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("got ") << relations->CountStrings() << " relations for " << source);
	
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
	
	BObjectList<BEntry>* targetEntries = ResolveRelationTargets(GetRelationIds(source, relation));
	LOG("Adding to result message:\n");
	targetEntries->EachElement(AddTargetToMessage, reply);

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("found ") << targetEntries->CountItems()
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
BStringList* RelationService::GetRelationIds(const char *path, const char* relation)
{
	BString relationTargets;
	BNode	node;
	
	node.ReadAttrString(relation, &relationTargets);
	
	BStringList ids;
	relationTargets.Split(SEN_RELATION_ID_SEPARATOR, true, ids);
	
	return new BStringList(ids);
}

BStringList* RelationService::GetRelationsFromAttrs(const char* path)
{
	BStringList* result = new BStringList();
	
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return result;
	
	char *attrName = new char[B_ATTR_NAME_LENGTH];
	
	BString relation;
	while (node.GetNextAttrName(attrName) == B_OK) {
		relation = BString(attrName);
		if (relation.StartsWith(SEN_ATTRIBUTES_PREFIX)) {
			result->Add(relation.Remove(0, 4));	// TODO: replace with real Relation name, for now we just cut the PREFIX
		}
	}
	delete attrName;
	
	return result;
}

/**
 * we use the inode as a stable file/dir reference (just like filesystem links, they have to stay on the same device)
 */
const char* RelationService::GetIdForFile(const char *path)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
	 return NULL;

	node_ref nodeInfo;
	node.GetNodeRef(&nodeInfo);
	
	return (BString("") << nodeInfo.node).String();
}

BObjectList<BEntry>* RelationService::ResolveRelationTargets(BStringList* ids)
{
	BObjectList<BEntry>* targets = new BObjectList<BEntry>();
	ids->DoForEach(QueryForId, targets);
	
	return targets;
}

bool RelationService::QueryForId(const BString& id, void* result)
{
	BString predicate(BString(SEN_RELATION_ID) << " == " << id);
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

bool RelationService::AddRelationToMessage(const BString& relation, void* message)
{
	reinterpret_cast<BMessage*>(message)->AddString("relations", relation);
	return true;
}

BEntry* RelationService::AddTargetToMessage(BEntry* entry, void* message)
{
	BPath path;
	entry->GetPath(&path);
	LOG("\t%s\n",path.Path());
	reinterpret_cast<BMessage*>(message)->AddString("targets", path.Path());
	
	return NULL;
}
