/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <AppFileInfo.h>
#include <fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include "RelationsHandler.h"
#include "../Sen.h"

status_t RelationsHandler::GetSelfRelations(const BMessage* message, BMessage* reply) {
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();
    status_t result;

    // get MIME type of source ref
    BNode sourceNode(source);
    if ((result = sourceNode.InitCheck()) != B_OK) {
        ERROR("could not initialize source node %s !", source);
        return result;
    }
    BNodeInfo sourceInfo(&sourceNode);
    if ((result = sourceInfo.InitCheck()) != B_OK) {
        ERROR("could not initialize source node info for %s !", source);
        return result;
    }
    char sourceType[B_MIME_TYPE_LENGTH];
    if ((result = sourceInfo.GetType(sourceType)) != B_OK) {
        ERROR("could not get MIME type for source node %s !", source);
        return result;
    }

    // query for all compatible extractors and return their generated output type
    LOG("query for extractors to handle file type %s\n", sourceType);
    BEntry pluginEntry;
    if ((result = SearchPluginsOfType(sourceType, &pluginEntry)) != B_OK) {
        return result;
    }
    entry_ref pluginRef;
    if ((result = pluginEntry.GetRef(&pluginRef)) != B_OK) {
        return result;
    }
    entry_ref* sourceRef = new entry_ref;
    BEntry sourceEntry(source);
    if ((result = sourceEntry.GetRef(sourceRef)) != B_OK) {
        return result;
    }
    // execute plugin and return result
    if ((result = be_roster->Launch(&pluginRef)) != B_OK) {
        ERROR("failed to launch plugin %s: %s\n", pluginEntry.Name(), strerror(result));
        return result;
    }

    LOG("getting app signature of plugin %s\n", pluginEntry.Name());

    BFile pluginFile(&pluginEntry, B_READ_ONLY);
    if ((result = pluginFile.InitCheck()) != B_OK || !(pluginFile.IsFile())) {
        ERROR("failed to get appInfo of plugin file %s: %s\n", source, strerror(result));
        return result;
    }
    BAppFileInfo pluginInfo(&pluginFile);
    char pluginAppSig[B_MIME_TYPE_LENGTH];
    if ((result = pluginInfo.GetSignature(pluginAppSig)) != B_OK) {
        ERROR("failed to get app signature of plugin file %s: %s\n", source, strerror(result));
        return result;
    }
    LOG("got plugin app signature: %s\n", pluginAppSig);

    // build refs received for plugin as input param
    BMessage refsMsg(B_REFS_RECEIVED);
    refsMsg.AddRef("refs", sourceRef);

    LOG("Sending refs to plugin %s:\n", pluginEntry.Name());
    refsMsg.PrintToStream();
/*
    int tries = 0;
    while (tries < 10 && ! be_roster->IsRunning(pluginAppSig)) {
        LOG("waiting for plugin %s to be ready (%d of 10...\n", pluginAppSig, tries);
        snooze(10000);
        tries++;
    }
*/
    BMessenger pluginMessenger(pluginAppSig);
    if ((result = pluginMessenger.SendMessage(&refsMsg, reply)) != B_OK) {
        ERROR("failed to communicate with plugin %s: %s\n", pluginEntry.Name(), strerror(result));
        reply->PrintToStream();
        return result;
    }

    LOG("received reply from plugin %s:\n", pluginEntry.Name());
    reply->PrintToStream();

    return B_OK;
}

status_t RelationsHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
    BString relationType;
	if (GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationType)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relation = relationType.String();

    return B_OK;
}

status_t RelationsHandler::SearchPluginsOfType(const char* mimeType, BEntry* entry) {
	BString predicate("SEN:TYPE==meta/x-vnd.sen-meta.plugin && SENSEI:TYPE==extractor");
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

	if (status_t result = query.Fetch() != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            DEBUG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        }
        // something else went wrong
        ERROR("could not execute query for suitable SENSEI extractors: %s\n", strerror(result));
        return result;
    }
    if (status_t result = query.GetNextEntry(entry) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            DEBUG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        }
        // something else went wrong
        ERROR("error resolving extractor query for %s: %s\n", mimeType, strerror(result));
        return result;
    }
    entry_ref ref;
    if (query.GetNextRef(&ref) == B_OK) {
        // todo: how to handle multiple suitable plugins? currently the first one found wins
        LOG("SENSEI: ambiguous extractor plugin configuration for type %s, taking first.\n", mimeType);
    }
    BPath path;
    entry->GetPath(&path);
    LOG("found entry with path %s\n", path.Path());
    query.Clear();

    return B_OK;
}
