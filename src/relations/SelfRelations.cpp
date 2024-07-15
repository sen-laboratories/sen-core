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

    const char* sourceType = GetMimeTypeForPath(source);

    // query for all compatible extractors and return their generated collected output type
    LOG("query for extractors to handle file type %s\n", sourceType);
    BMessage typeToPlugins;

    if ((result = GetPluginsForType(sourceType, &typeToPlugins)) != B_OK) {
        return result;
    }
    DEBUG("got types/plugins config for source type %s:\n", sourceType);
    typeToPlugins.PrintToStream();
/*
    // add relations as flat list for easier consumption
    int32 plugins = typeToPlugins.CountNames(B_REF_TYPE);
    for (int index = 0; index < plugins; index++) {
        char *pluginSig[B_MIME_TYPE_LENGTH];
        result = typeToPlugins.GetInfo(B_REF_TYPE, index, pluginSig, NULL, NULL);
        if (result != B_OK || *pluginSig == NULL) {
            ERROR("failed to parse output types for source type %s: %s\n", sourceType, strerror(result));
            return result;
        }
        DEBUG("adding output types for plugin %s\n", *pluginSig);
        result = GetPluginOutputConfig(*pluginSig, &typeToPlugins);
        if (result != B_OK) {
            ERROR("failed to get output config for source type %s and plugin %s: %s\n", sourceType, *pluginSig, strerror(result));
            break;
        }
    }
*/
    reply->what = SENSEI_MESSAGE_RESULT;
    reply->Append(typeToPlugins);

    return result;
}

status_t RelationsHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
	BString sourceParam;
	if (GetMessageParameter(message, reply, SEN_RELATION_SOURCE, &sourceParam)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* source = sourceParam.String();
    const char* sourceMimeType = GetMimeTypeForPath(source);
    if (sourceMimeType == NULL) {
        return B_ERROR;
    }

    BString relationTypeParam;
    // relation type for self relations is one of the possible output types of compatible extractors.
	if (GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationTypeParam, true, false)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relationType = relationTypeParam.String();

    status_t result;

    // sender MAY send a list of already gathered plugins for this type to save re-query
    BMessage typeToPlugins;
    result = message->FindMessage(SENSEI_SELF_TYPE_MAPPINGS_KEY, &typeToPlugins);
    if (result != B_OK) {
        if (result == B_NAME_NOT_FOUND) {
            LOG("fresh query for suitable plugins for type %s...\n", relationType);

            result = GetPluginsForType(sourceMimeType, &typeToPlugins);
            if (result != B_OK) {
                return result;  // already handled, just pass on
            }
         } else {
            ERROR("couldn't look up plugins from message: %s", strerror(result));
            return result;
         }
    }
    LOG("got plugin config:\n");
    typeToPlugins.PrintToStream();

    // filter for plugins that generate the requested relationType
    // todo: check for 1:N mappings, shouldn't happen, we assume 1:1
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
    reply->what = SENSEI_MESSAGE_RESULT;
    reply->Append(typeToPlugins);
    reply->PrintToStream();

    return B_OK;
}

status_t RelationsHandler::GetPluginsForType(const char* mimeType, BMessage* outputTypesToPlugins) {
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
    while ((result = query.GetNextEntry(&entry)) == B_OK) {
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

        // filter for supported input type
        if (pluginInfo.IsSupportedType(mimeType)) {
            // get supported output types and add to lookup map accordingly
            // todo: there may be more plugins per type, supporting different aspects and
            // returning different output type - later we need to detect and handle overlaps!
            DEBUG("Adding extractor plugin %s for handling type %s\n", pluginAppSig, mimeType);

            entry_ref ref;
            entry.GetRef(&ref);
            result = GetPluginOutputConfig(pluginAppSig, &ref, outputTypesToPlugins);
            if (result != B_OK){
                ERROR("skipping compatible extractor plugin %s due to error.\n", pluginAppSig);
                // better luck next time?
                continue;
            }

            pluginCount++;
        } else {
            DEBUG("extractor plugin %s does not support type %s\n", pluginAppSig, mimeType);
        }
    } // while

    if (result == B_ENTRY_NOT_FOUND) {  // expected, just check if we found someting
        if (pluginCount == 0) {
            DEBUG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        } else {
            DEBUG("found %u suitable plugins.\n", pluginCount);
            DEBUG("plugin output map is:\n");
            outputTypesToPlugins->PrintToStream();
        }
    } else {
        // something else went wrong
        ERROR("error resolving extractor query for %s: %s\n", mimeType, strerror(result));
        return result;
    }

    query.Clear();

    return B_OK;
}

status_t RelationsHandler::GetPluginOutputConfig(const char* pluginSig, entry_ref* pluginRef, BMessage* pluginOutputConfig) {
    BNode node(pluginRef);
    status_t result;

    if ((result = node.InitCheck()) != B_OK) {
        return result;
    }

    attr_info attrInfo;
    if ((result = node.GetAttrInfo(SENSEI_OUTPUT_MAPPING, &attrInfo)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            ERROR("expected plugin attribute not found in plugin %s: %s\n", pluginSig, SENSEI_OUTPUT_MAPPING);
        } else {
            ERROR("error getting attribute info from plugin %s: %s\n", pluginSig, strerror(result));
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
        ERROR("no output types found in plugin.\n");
        return B_ENTRY_NOT_FOUND;
    } else if (result < 0) {
        ERROR("failed to read mappings from attribute %s of plugin: %s\n", SENSEI_OUTPUT_MAPPING, strerror(result));
        return result;
    }

    // retrieve type map for client to map types of result
    BMessage outputMappings;
    outputMappings.Unflatten(attrValue);

    // store default type separately if available for easier access
    BString defaultType;
    result = outputMappings.FindString(SENSEI_SELF_DEFAULT_TYPE, &defaultType);
    if (result == B_OK) {
        pluginOutputConfig->AddString(SENSEI_SELF_DEFAULT_TYPE_KEY, defaultType);
        // also add actual type to result output type mapping so it will be found by the actual type name later
        pluginOutputConfig->AddString(defaultType, pluginSig);
        // and remove from type mappings
        outputMappings.RemoveData(SENSEI_SELF_DEFAULT_TYPE);
    }

    // add to reply msg
    pluginOutputConfig->AddMessage(SENSEI_SELF_TYPE_MAPPINGS_KEY, new BMessage(outputMappings));

    // add mapping from plugin sig to output types generated by this plugin (usually relations) for filtering
    char *typeName[B_MIME_TYPE_LENGTH];
    int32 itemCount = outputMappings.CountNames(B_STRING_TYPE);
    for (int32 index = 0; index < itemCount; index++) {
        result = outputMappings.GetInfo(B_STRING_TYPE, index, typeName, NULL, NULL);
        if (result != B_OK || *typeName == NULL) {
            ERROR("failed to parse output types of plugin %s (%u types added): %s\n", pluginSig, index, strerror(result));
            return result;
        }
        DEBUG("adding output type %s with associated plugin %s\n", *typeName, pluginSig);
        pluginOutputConfig->AddString(*typeName, pluginSig);
    }
    return result;
}

const char* RelationsHandler::GetMimeTypeForPath(const char* source) {
    BNode sourceNode(source);
    status_t result;
    if ((result = sourceNode.InitCheck()) != B_OK) {
        ERROR("could not initialize source node %s !", source);
        return NULL;
    }
    BNodeInfo sourceInfo(&sourceNode);
    if ((result = sourceInfo.InitCheck()) != B_OK) {
        ERROR("could not initialize source node info for %s !", source);
        return NULL;
    }
    char sourceType[B_MIME_TYPE_LENGTH];
    if ((result = sourceInfo.GetType(sourceType)) != B_OK) {
        ERROR("could not get MIME type for source node %s !", source);
        return NULL;
    }

    return (new BString(sourceType))->String();
}
