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
#include "../Sensei.h"

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

    // query for all compatible extractors and return their generated collected output type
    LOG("query for extractors to handle file type %s\n", sourceType);
    BMessage* typesToPlugins = new BMessage(SENSEI_MESSAGE_TYPE);

    if ((result = SearchPluginsForType(sourceType, typesToPlugins)) != B_OK) {
        return result;
    }
    DEBUG("got %u output types for source type %s:\n", typesToPlugins->CountNames(B_STRING_TYPE), sourceType);
    typesToPlugins->PrintToStream();

    reply->AddMessage(SENSEI_SELF_RELATIONS_KEY, typesToPlugins);

    return B_OK;
}

status_t RelationsHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();

    BString relationTypeParam;
    // relation type for self relations is one of the possible output types of compatible extractors.
	if (GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationTypeParam, true, false)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relationType = relationTypeParam.String();

    status_t result;

    // sender MAY send a list of already gathered plugins for this type to save re-query
    BMessage typeToPlugins;
    result = message->FindMessage(SENSEI_SELF_PLUGINS_KEY, &typeToPlugins);
    if (result != B_OK) {
        if (result == B_NAME_NOT_FOUND) {
            LOG("fresh query for suitable plugins for type %s...\n", relationType);
            result = SearchPluginsForType(relationType, &typeToPlugins);
            if (result != B_OK) {
                return result;  // already handled, just pass on
            }
         } else {
            ERROR("couldn't look up plugins from message: %s", strerror(result));
            return result;
         }
    }
    LOG("got type->plugins map:\n");
    typeToPlugins.PrintToStream();

    // todo: perform some magic finding the right plugin, for now we assume to have a clean 1:1 mapping and 1st wins

    BString pluginType;
    result = typeToPlugins.FindString(relationType, &pluginType);
    if (result != B_OK) {
        ERROR("failed to look up plugin signature for relation type %s: %s\n", relationType, strerror(result));
        return result;
    }
    const char* pluginSig = pluginType.String();
    LOG("got plugin MIME-Type / signature: %s\n", pluginSig);

    // execute plugin and return result
    result = be_roster->Launch(pluginSig);
    if (result != B_OK) {
        ERROR("failed to launch plugin %s: %s\n", pluginSig, strerror(result));
        return result;
    }

    // build refs received for plugin as input param
    BEntry sourceEntry(source);
    entry_ref* sourceRef = new entry_ref;
    result = sourceEntry.InitCheck();
    if (result == B_OK) result = sourceEntry.GetRef(sourceRef);
    if (result != B_OK) {
        ERROR("failed to get ref for path %s: %s\n", source, strerror(result));
        return result;
    }
    BMessage refsMsg(B_REFS_RECEIVED);
    refsMsg.AddRef("refs", sourceRef);

    LOG("Sending refs to plugin %s:\n", pluginSig);
    refsMsg.PrintToStream();

    BMessenger pluginMessenger(pluginSig);
    if ((result = pluginMessenger.SendMessage(&refsMsg, reply)) != B_OK) {
        ERROR("failed to communicate with plugin %s: %s\n", pluginSig, strerror(result));
        reply->PrintToStream();
        return result;
    }

    LOG("received reply from plugin %s:\n", pluginSig);
    reply->PrintToStream();

    return B_OK;
}

status_t RelationsHandler::SearchPluginsForType(const char* mimeType, BMessage* outputTypesPlugins) {
	BString predicate("SEN:TYPE==meta/x-vnd.sen-meta.plugin && SENSEI:TYPE==extractor");
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

    status_t result;
	if ((result = query.Fetch()) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            DEBUG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        }
        // something else went wrong
        ERROR("could not execute query for suitable SENSEI extractors: %s\n", strerror(result));
        return result;
    }
    BEntry entry;
    int32 pluginCount = 0;
    while ((result = query.GetNextEntry(&entry)) == B_OK) { // todo: we get duplicate results with same path?!
        BPath path;
        entry.GetPath(&path);
        LOG("found plugin with path %s\n", path.Path());

        // get MIME-Type == application_signature of plugin to use as key later
        LOG("getting app signature of plugin %s\n", entry.Name());

        BFile pluginFile(&entry, B_READ_ONLY);
        if ((result = pluginFile.InitCheck()) != B_OK || !(pluginFile.IsFile())) {
            ERROR("failed to get appInfo of plugin file %s: %s\n", entry.Name(), strerror(result));
            return result;
        }
        BAppFileInfo pluginInfo(&pluginFile);
        char pluginAppSig[B_MIME_TYPE_LENGTH];
        if ((result = pluginInfo.GetSignature(pluginAppSig)) != B_OK) {
            ERROR("failed to get app signature of plugin file %s: %s\n", entry.Name(), strerror(result));
            return result;
        }
        LOG("got plugin app signature: %s\n", pluginAppSig);

        // todo: filter for supported input type

        // get supported output types of plugin
        BStringList outputTypes;
        result = GetSenseiOutputTypes(&entry, &outputTypes);
        if (result != B_OK) {
            ERROR("could not resolve short description for plugin %s: %s\n", path.Path(), strerror(result));
            return result;
        }
        // may be empty if there is no plugin for the given type
        for (int i = 0; i < outputTypes.CountStrings() ; i++) {
            // outputTypes may occur more than once in this list.
            // this is no problem since we add the values to the
            // same key in the result message, creating a list
            // of multiple plugins that can handle that type.
            outputTypesPlugins->AddString(outputTypes.StringAt(i), pluginAppSig);
        }
        pluginCount++;
    } // while

    if (result == B_ENTRY_NOT_FOUND) {  // expected, just check if we found someting
        if (pluginCount == 0) {
            DEBUG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        } else {
            DEBUG("found %u suitable plugins.\n", pluginCount);
        }
    } else {
        // something else went wrong
        ERROR("error resolving extractor query for %s: %s\n", mimeType, strerror(result));
        return result;
    }

    query.Clear();

    return B_OK;
}

status_t RelationsHandler::GetSenseiOutputTypes(const BEntry* src, BStringList* outputTypes) {
    BNode node(src);
    status_t result;
    if ((result = node.InitCheck()) != B_OK) {
        return result;
    }

    attr_info attrInfo;
    if ((result = node.GetAttrInfo(SENSEI_OUTPUT_MAPPING, &attrInfo)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            ERROR("expected plugin attribute not found: %s\n", SENSEI_OUTPUT_MAPPING);
        } else {
            ERROR("error getting attribute info from node %s: %s\n", src->Name(), strerror(result));
        }
        return result;
    }

    char* attrValue = new char[attrInfo.size + 1];
    result = node.ReadAttr(
            SENSEI_OUTPUT_MAPPING,
            B_MESSAGE_TYPE,
            0,
            attrValue,
            attrInfo.size);

    if (result == 0) {
        ERROR("no output types found in file.\n");
        return B_ENTRY_NOT_FOUND;
    } else if (result < 0) {
        ERROR("failed to read mappings from attribute %s of file: %s\n", SENSEI_OUTPUT_MAPPING, strerror(result));
        return result;
    }

    BMessage outputMapping;
    outputMapping.Unflatten(attrValue);

    // todo: iterate through mapping and retrieve all types, for now we just expect one type and take the default type

    BString outputType;
    result = outputMapping.FindString("_default", &outputType);
    if (result == B_OK) {
        outputTypes->Add(outputType);
    } else {
        ERROR("no default mapping found.\n");
    }
    return result;
}