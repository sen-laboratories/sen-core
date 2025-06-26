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
#include "Sen.h"
#include "Sensei.h"

status_t RelationsHandler::GetSelfRelations(const BMessage* message, BMessage* reply) {
	entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, reply, SEN_RELATION_SOURCE, nullptr, &sourceRef))  != B_OK) {
        ERROR("failed to parse source: %s\n", strerror(status));
		return status;
	}

    const char* sourceType = GetMimeTypeForRef(&sourceRef);

    // query for all compatible extractors and return their generated collected output type
    LOG("query for extractors to handle file type %s\n", sourceType);
    BMessage pluginConfig;

    if ((status = GetPluginsForTypeAndFeature(sourceType, SENSEI_FEATURE_EXTRACT, &pluginConfig)) != B_OK) {
        return status;
    }
    LOG("got types/plugins config for source type %s:\n", sourceType);
    pluginConfig.PrintToStream();

    reply->what = SENSEI_MESSAGE_RESULT;
    reply->AddMessage(SENSEI_PLUGIN_CONFIG_KEY, new BMessage(pluginConfig));

    return status;
}

status_t RelationsHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
    entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, reply, SEN_RELATION_SOURCE, NULL, &sourceRef))  != B_OK) {
		return status;
	}

    const char* sourceMimeType = GetMimeTypeForRef(&sourceRef);
    if (sourceMimeType == NULL) {
        return B_ERROR;
    }

    BString relationTypeParam;
    // relation type for self relations is one of the possible output types of compatible extractors.
	if (GetMessageParameter(message, reply, SEN_RELATION_TYPE, &relationTypeParam, NULL, true, false)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relationType = relationTypeParam.String();

    BString pluginTypeParam;
    // client may send the desired plugin signature already, saving us the hassle
	if (GetMessageParameter(message, reply, SENSEI_PLUGIN_KEY, &pluginTypeParam, NULL, true, false)  == B_OK) {
        const char* pluginSig = pluginTypeParam.String();
        LOG("got plugin signature %s, jumping to launch plugin.\n", pluginSig);
		return ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, reply);
	}

    status_t result;
    bool clientHasConfig = false;

    // sender MAY send an existing plugin config for this type from a previous call to save re-query
    BMessage pluginConfig;
    result = message->FindMessage(SENSEI_PLUGIN_CONFIG_KEY, &pluginConfig);
    if (result != B_OK) {
        if (result == B_NAME_NOT_FOUND) {
            LOG("fresh query for suitable plugins for relation type %s...\n", relationType);

            result = GetPluginsForTypeAndFeature(sourceMimeType, SENSEI_FEATURE_EXTRACT, &pluginConfig);
            if (result != B_OK) {
                return result;  // already handled, just pass on
            }
            LOG("got fresh plugin config:\n");
         } else {
            ERROR("couldn't look up plugins from message: %s", strerror(result));
            return result;
         }
    } else {
        LOG("got existing plugin config for relation type %s:\n", relationType);
        clientHasConfig = true;
    }
    pluginConfig.PrintToStream();

    // get type->plugin map
    BMessage typeToPlugins;
    result = pluginConfig.FindMessage(SENSEI_TYPES_PLUGINS_KEY, &typeToPlugins);
    if (result != B_OK) {
        ERROR("failed to look up type->plugin map for relation type %s: %s\n", relationType, strerror(result));
        return result;
    }

    // filter for plugins that generate the requested relationType
    // todo: check for 1:N mappings, shouldn't happen, we assume 1:1
    BString pluginType;
    result = typeToPlugins.FindString(relationType, &pluginType);
    if (result != B_OK) {
        ERROR("failed to look up plugin signature for relation type %s: %s\n", relationType, strerror(result));
        return result;
    }
    const char* pluginSig = pluginType.String();

    result = ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, reply);
    if (result != B_OK) {
        ERROR("failed to resolve relations of type %s with plugin %s: %s\n", relationType, pluginSig, strerror(result));
        return result;
    }
    // send back current plugin config if client did not have it
    if (!clientHasConfig) {
        reply->Append(pluginConfig);
    }
    return result;
}

status_t RelationsHandler::ResolveSelfRelationsWithPlugin(const char* pluginSig, const entry_ref* sourceRef, BMessage* reply) {
    LOG("got plugin app signature: %s\n", pluginSig);

    // execute plugin and return result
    status_t result = be_roster->Launch(pluginSig);
    if (result != B_OK) {
        ERROR("failed to launch plugin %s: %s\n", pluginSig, strerror(result));
        return result;
    }

    // build refs received for plugin as input param
    BEntry sourceEntry(sourceRef);
    result = sourceEntry.InitCheck();

    if (result != B_OK) {
        ERROR("failed to get ref for path %s: %s\n", sourceRef->name, strerror(result));
        return result;
    }
    BMessage refsMsg(B_REFS_RECEIVED);
    refsMsg.AddRef("refs", sourceRef);

    LOG("Sending refs to plugin %s:\n", pluginSig);
    refsMsg.PrintToStream();

    BMessenger pluginMessenger(pluginSig);
    result = pluginMessenger.SendMessage(&refsMsg, reply);
    if (result != B_OK) {
        ERROR("failed to communicate with plugin %s: %s\n", pluginSig, strerror(result));
        reply->PrintToStream();
        return result;
    }

    reply->what = SENSEI_MESSAGE_RESULT;
    reply->AddRef("refs", sourceRef);

    return B_OK;
}

// todo: extend to handle flavours/features like search or identify
status_t RelationsHandler::GetPluginsForTypeAndFeature(const char* mimeType, const char* feature, BMessage* pluginConfig) {
    BString predicate("SEN:TYPE==" SENSEI_PLUGIN_TYPE " && " SENSEI_PLUGIN_FEATURE_ATTR);
            predicate << feature << "==1";
	BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BQuery query;
	query.SetVolume(&bootVolume);
	query.SetPredicate(predicate.String());

    status_t result;
	if ((result = query.Fetch()) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            LOG("no matching extractor found for type %s, query was: %s\n", mimeType, predicate.String());
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
            LOG("Adding extractor plugin %s for handling type %s\n", pluginAppSig, mimeType);

            entry_ref ref;
            entry.GetRef(&ref);
            result = GetPluginConfig(pluginAppSig, &ref, mimeType, pluginConfig);
            if (result != B_OK){
                ERROR("skipping compatible extractor plugin %s due to error: %s.\n", pluginAppSig, strerror(result));
                // better luck next time?
                continue;
            }

            pluginCount++;
        } else {
            LOG("extractor plugin %s does not support type %s\n", pluginAppSig, mimeType);
        }
    } // while

    if (result == B_ENTRY_NOT_FOUND) {  // expected, just check if we found someting
        if (pluginCount == 0) {
            LOG("no matching extractor found for type %s\n", mimeType);
            return B_OK;
        } else {
            LOG("found %u suitable plugins.\n", pluginCount);
            LOG("plugin output map is:\n");
            pluginConfig->PrintToStream();
        }
    } else {
        // something else went wrong
        ERROR("error resolving extractor query for %s: %s\n", mimeType, strerror(result));
        return result;
    }

    query.Clear();
    return B_OK;
}

status_t RelationsHandler::GetPluginConfig(
    const char* pluginSig,
    entry_ref* pluginRef,
    const char* pluginMimeType,
    BMessage* pluginConfig)
{
    BNode node(pluginRef);
    status_t result;

    if ((result = node.InitCheck()) != B_OK) {
        return result;
    }

    attr_info attrInfo;
    if ((result = node.GetAttrInfo(SENSEI_TYPE_MAPPING, &attrInfo)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            ERROR("expected plugin attribute not found in plugin %s: %s\n", pluginSig, SENSEI_TYPE_MAPPING);
        } else {
            ERROR("error getting attribute info from plugin %s: %s\n", pluginSig, strerror(result));
        }
        return result;
    }

    char* attrValue = new char[attrInfo.size + 1];
    result = node.ReadAttr(
            SENSEI_TYPE_MAPPING,
            B_MESSAGE_TYPE,
            0,
            attrValue,
            attrInfo.size);

    if (result == 0) {
        ERROR("no output types found in plugin.\n");
        return B_ENTRY_NOT_FOUND;
    } else if (result < 0) {
        ERROR("failed to read mappings from attribute %s of plugin: %s\n", SENSEI_TYPE_MAPPING, strerror(result));
        return result;
    }

    // retrieve type map for client to map types of result
    BMessage typeMappings;
    typeMappings.Unflatten(attrValue);

    // add mapping from plugin's output types to the plugin signature so we can resolve the relation later.
    BMessage typesToPlugins;
    typesToPlugins.AddString(pluginMimeType, pluginSig);
    pluginConfig->AddMessage(SENSEI_TYPES_PLUGINS_KEY, new BMessage(typesToPlugins));

    // store default type separately if available for easier access
    BString defaultType;

    result = typeMappings.FindString(SENSEI_DEFAULT_TYPE, &defaultType);
    if (result == B_OK) {
        pluginConfig->AddString(SENSEI_DEFAULT_TYPE_KEY, defaultType);
        // and remove from type mappings
        typeMappings.RemoveData(SENSEI_DEFAULT_TYPE);
    }

    // add to reply msg
    pluginConfig->AddMessage(SENSEI_TYPE_MAPPING, new BMessage(typeMappings));
    return result;
}

const char* RelationsHandler::GetMimeTypeForRef(const entry_ref *ref) {
    BNode sourceNode(ref);
    status_t result;
    if ((result = sourceNode.InitCheck()) != B_OK) {
        ERROR("could not initialize source node %s: %s\n", ref->name, strerror(result));
        return NULL;
    }
    BNodeInfo sourceInfo(&sourceNode);
    if ((result = sourceInfo.InitCheck()) != B_OK) {
        ERROR("could not initialize source node info for %s: %s\n", ref->name, strerror(result));
        return NULL;
    }
    char sourceType[B_MIME_TYPE_LENGTH];
    if ((result = sourceInfo.GetType(sourceType)) != B_OK) {
        ERROR("could not get MIME type for source node %s: %s\n", ref->name, strerror(result));
        return NULL;
    }

    return (new BString(sourceType))->String();
}
