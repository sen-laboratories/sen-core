/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
 
#include "RelationsHandler.h"
#include "../Sen.h"

#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <stdio.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

RelationsHandler::RelationsHandler()
    : BHandler("SenRelationsHandler")
{
}

RelationsHandler::~RelationsHandler()
{
}

void RelationsHandler::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;

    LOG("in SEN RelationsHandler::MessageReceived\n");

    switch(message->what)
    {
		case SEN_RELATIONS_ADD:
		{
			result = AddRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_GET:
		{
			result = GetRelations(message, reply);
			break;
		}
		case SEN_RELATIONS_GET_TARGETS:
		{
			result = GetTargetsForRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_REMOVE:
		{
			result = RemoveRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_REMOVEALL:
		{
			result = RemoveAllRelations(message, reply);
			break;
		}
        default:
        {
            LOG("RelationsHandler: unkown message received, handing over to parent: %lu\n", message->what);
            BHandler::MessageReceived(message); 
            return;
        }
    }
    LOG("RelationsHandler sending reply %d", result);
   	reply->AddInt32("result", result);
	message->SendReply(reply);
}

status_t RelationsHandler::AddRelation(const BMessage* message, BMessage* reply)
{
	const char* source = new char[B_FILE_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_SOURCE, &source)   != B_OK) {
		return B_BAD_VALUE;
	}
	const char* relation = new char[B_ATTR_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	const char* target = new char[B_FILE_NAME_LENGTH];
	if (message->FindString(SEN_RELATION_TARGET, &target)   != B_OK) {
		return B_BAD_VALUE;
	}
	
	const char* srcId  	 = GetIdForFile(source);
	const char* targetId = GetIdForFile(target);
	 
	if (srcId == NULL || targetId == NULL) {
		return B_ERROR;
	}
	
	// get existing relation IDs for source file
	BStringList* ids = ReadRelationIdsFromFile(source, relation);
	if (ids == NULL) {
		ERROR("failed to read relation ids from %s for relation %s!\n", source, relation);
		return B_ERROR;
	}
	// write source File ID
	if (WriteIdToFile(source, srcId) != B_OK) {
		ERROR("failed to write source ID attr %s to file %s.\n", srcId, source);
		return B_ERROR;
	}
	// update target IDs with new target if not already exists
	if (! ids->HasString(targetId)) {
		ids->Add(targetId);
		WriteRelationIdsToFile(source, relation, ids);
	}
	
	// store target ID in target file to make target reachable via relation
	if (WriteIdToFile(target, targetId) != B_OK) {
		ERROR("failed to write target ID attr %s to file %s.\n", targetId, target);
		return B_ERROR;
	}
	
	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("created relation '") << relation << "' from "
		<< source << " [" << srcId << "] -> " <<  target << " [" << targetId << "]");
	
	return B_OK;
}

status_t RelationsHandler::GetRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	BStringList* relations = ReadRelationsFromAttrs(source);
	reply->AddStrings("relations", *relations);
	reply->AddString("statusMessage", BString("got ") << relations->CountStrings() << " relations for " << source);
	
	return B_OK;
}

status_t RelationsHandler::GetTargetsForRelation(const BMessage* message, BMessage* reply)
{	
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}
	BString relation;
	if (message->FindString(SEN_RELATION_NAME, &relation) != B_OK) {
		return B_BAD_VALUE;
	}
	
	BObjectList<BEntry>* targetEntries = ResolveRelationTargets(ReadRelationIdsFromFile(source, relation));
	LOG("Adding %d targetEntries to result message:\n", targetEntries->CountItems());
	targetEntries->EachElement(AddTargetToMessage, reply);

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("found ") << targetEntries->CountItems()
		<< " targets for relation '" << relation << "' from '" << source << "'");
		
	return B_OK;
}

status_t RelationsHandler::RemoveRelation(const BMessage* message, BMessage* reply)
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

status_t RelationsHandler::RemoveAllRelations(const BMessage* message, BMessage* reply)
{
	BString source;
	if (message->FindString(SEN_RELATION_SOURCE, &source) != B_OK) {
		return B_BAD_VALUE;
	}

	reply->what = SEN_RESULT_RELATIONS;
	reply->AddString("statusMessage", BString("removed all relations from ") << source);
	
	return B_OK;
}

/*
 * private methods
 */

const char* RelationsHandler::GetRelationAttributeName(const char* relation) {
	BString* relationAttr = new BString(relation);
	if (! relationAttr->StartsWith(SEN_ATTRIBUTES_PREFIX)) {
		relationAttr->Prepend(SEN_ATTRIBUTES_PREFIX);
	}
	return relationAttr->String();
}

BStringList* RelationsHandler::ReadRelationIdsFromFile(const char *path, const char* relation)
{
	BString relationTargets;
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return NULL;
	
	const char* relationAttr = GetRelationAttributeName(relation);
	node.ReadAttrString(relationAttr, &relationTargets);
	
	DEBUG("got relation targets %s for file %s\n", relationTargets.String(), path);
	
	BStringList* ids = new BStringList();
	relationTargets.Split(SEN_FILE_ID_SEPARATOR, true, *ids);
	
	DEBUG("added %d strings to target list.\n", ids->CountStrings());
	return ids;
}

status_t RelationsHandler::WriteRelationIdsToFile(const char *path, const char* relation, BStringList* ids)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return B_BAD_VALUE;
	
	BString *idStr = new BString();
	ids->DoForEach(AppendIdToString, idStr);
	
	const BString* allIds = new BString(idStr->RemoveLast(SEN_FILE_ID_SEPARATOR));
	const char* relationAttr = GetRelationAttributeName(relation);

	DEBUG("writing target ids '%s' for relation '%s' to file %s", allIds->String(), relationAttr, path);

	return node.WriteAttrString(relationAttr, allIds);
}

BStringList* RelationsHandler::ReadRelationsFromAttrs(const char* path)
{
	BStringList* result = new BStringList();
	
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return result;
	
	char *attrName = new char[B_ATTR_NAME_LENGTH];
	
	BString relation;
	while (node.GetNextAttrName(attrName) == B_OK) {
		relation = BString(attrName);
		if (relation.StartsWith(SEN_ATTRIBUTES_PREFIX) && ! (relation == SEN_FILE_ID) ) {
			result->Add(relation.Remove(0, 4));	// TODO: replace with real Relation name from RelationConfig, for now we just cut the PREFIX
		}
	}
	delete attrName;
	
	return result;
}

/**
 * we use the inode as a stable file/dir reference (just like filesystem links, they have to stay on the same device)
 */
const char* RelationsHandler::GetIdForFile(const char *path)
{
	BNode node(path);
	if (node.InitCheck() != B_OK) {
		ERROR("failed to initialize node for path %s\n", path);
		return NULL;
	}

	node_ref nodeInfo;
	node.GetNodeRef(&nodeInfo);
	
	char* id=new char[sizeof(ino_t)];
	if (snprintf(id, sizeof(id), "%d", nodeInfo.node) >= 0) {
		DEBUG("got ID %s for path %s\n", id, path);
		return id;
	} else {
		ERROR("failed to copy ID from node_info for path %s\n", path);
		return NULL;
	}
}

status_t RelationsHandler::WriteIdToFile(const char *path, const char *id)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return B_BAD_VALUE;
	
	return node.WriteAttrString(SEN_FILE_ID, new BString(id));
}

BObjectList<BEntry>* RelationsHandler::ResolveRelationTargets(BStringList* ids)
{
	DEBUG("resolving ids from list with %d targets...\n", ids->CountStrings())
	BObjectList<BEntry>* targets = new BObjectList<BEntry>();
	//ids->DoForEach(QueryForId, targets);
	while (!ids->IsEmpty()) {
		BString id = ids->First();
		QueryForId(id, targets);
		ids->Remove(id);
	}
	return targets;
}

bool RelationsHandler::AppendIdToString(const BString& id, void* result) {
	reinterpret_cast<BString*>(result)->Append(id).Append(SEN_FILE_ID_SEPARATOR);
	return true;
}

bool RelationsHandler::QueryForId(const BString& id, void* result)
{
	LOG("query for id %s\n", id);

	BString predicate(BString(SEN_FILE_ID) << " == " << id);
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

bool RelationsHandler::AddRelationToMessage(const BString& relation, void* message)
{
	// TODO: change to entry_refs with RelationTypes from config when ready -> additional icons e.g. in Tracker.SEN
	reinterpret_cast<BMessage*>(message)->AddString("relations", relation);
	return true;
}

/**
 * add the entry_ref of the target to the message under standard "refs" property
 */
BEntry* RelationsHandler::AddTargetToMessage(BEntry* entry, void* message)
{
	entry_ref *ref = new entry_ref;
	entry->GetRef(ref);
	reinterpret_cast<BMessage*>(message)->AddRef("refs", ref);	
	return NULL;
}
