/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <cassert>

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

#include "RelationHandler.h"
#include "Sen.h"
#include "Sensei.h"

status_t RelationHandler::GetSelfRelations(const BMessage* message, BMessage* reply) {
	entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, nullptr, &sourceRef))  != B_OK) {
		return status;
	}

    const char* sourceType = GetMimeTypeForRef(&sourceRef);

    // query for all compatible extractors and return their generated collected output type
    LOG("query for extractors to handle file type %s\n", sourceType);
    BMessage pluginConfig;

    status = GetPluginsForTypeAndFeature(sourceType, SENSEI_FEATURE_EXTRACT, &pluginConfig);
    if (status != B_OK) {
        return status;
    }

    LOG("got types/plugins config for source type %s:\n", sourceType);
    pluginConfig.PrintToStream();

    reply->what = SENSEI_MESSAGE_RESULT;
    reply->AddMessage(SENSEI_PLUGIN_CONFIG_KEY, new BMessage(pluginConfig));

    // transparently add type mappings as relations for consistent uniform handling from outside (e.g. Tracker)
    BStringList relationTypes;

    BMessage typeMappings;
    status = pluginConfig.FindMessage(SENSEI_TYPE_MAPPING, &typeMappings);
    if (status != B_OK) {
        ERROR("could not find expected type mappings, aborting.\n");
        return status;
    }

    // add all types (values) as relations
    for (int t = 0; t < typeMappings.CountNames(B_STRING_TYPE); t++) {
        char *alias;
        BString relationTypeName;

        status = typeMappings.GetInfo(B_STRING_TYPE, t, &alias, NULL);
        if (status == B_OK)
            status = typeMappings.FindString(alias, t, &relationTypeName);
        if (status != B_OK) {
            ERROR("failed to get type mapping #%d: %s\n", t, strerror(status));
            continue;
        }
        relationTypes.Add(relationTypeName);
    }

    BString defaultType = pluginConfig.GetString(SENSEI_DEFAULT_TYPE_KEY, "");
    if (! defaultType.IsEmpty())
        relationTypes.Add(defaultType);

    reply->AddStrings(SEN_RELATIONS, relationTypes);

    // get relation configs (always needed for self relations)
    BMessage relationConfigs;
    status = GetRelationConfigs(&relationTypes, &relationConfigs);
    if (status == B_OK) {
        reply->AddMessage(SEN_RELATION_CONFIG, &relationConfigs);
    }

    return status;
}

status_t RelationHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
    entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, SEN_RELATION_SOURCE_REF, nullptr, &sourceRef))  != B_OK) {
		return status;
	}

    const char* sourceMimeType = GetMimeTypeForRef(&sourceRef);
    if (sourceMimeType == NULL) {
        return B_ERROR;
    }

    BString relationTypeParam;
    // relation type for self relations is one of the possible output types of compatible extractors.
	if (GetMessageParameter(message, SEN_RELATION_TYPE, &relationTypeParam, NULL, true, false)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relationType = relationTypeParam.String();

    // retrieve relation config from MIME DB in the filesystem
    BMessage relationConfigs;
    BStringList types;
    types.Add(relationType);

    status = GetRelationConfigs(&types, &relationConfigs);
    if (status != B_OK) {
        LOG("failed to get relation config for type %s: %s\n", relationType, strerror(status));
        return status;
    }

    // add to reply
    reply->AddMessage(SEN_RELATION_CONFIG, &relationConfigs);

    BString pluginTypeParam;
    // client may send the desired plugin signature already, saving us the hassle
	if (GetMessageParameter(message, SENSEI_PLUGIN_KEY, &pluginTypeParam, NULL, true, false)  == B_OK) {
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

status_t RelationHandler::ResolveSelfRelationsWithPlugin(
    const char* pluginSig,
    const entry_ref* sourceRef,
    BMessage* reply)
{
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

    // add unique node ID to all nested nodes for easier tracking (e.g. Tracker selected node->relation folder)
    result = AddItemIdToPluginResult(reply);

    reply->what = SENSEI_MESSAGE_RESULT;
    reply->AddRef("refs", sourceRef);

    return B_OK;
}

status_t RelationHandler::AddItemIdToPluginResult(BMessage* pluginReply)
{
    int32      count;
    type_code  type;
    status_t   status;

    status = pluginReply->GetInfo(SENSEI_ITEM, &type, &count);
    if (status != B_OK) {
        if (status == B_NAME_NOT_FOUND) {
            return B_OK;    // done, no items at this level
        }
        // else it's an error
        ERROR("could not inspect message: %s\n", strerror(status));
        return status;
    }
    if (type != B_MESSAGE_TYPE) {
        ERROR("unexpected plugin reply, %s has to be of type BMessage!\n", SENSEI_ITEM);
        return B_BAD_VALUE;
    }
    if (count == 0) {
        LOG("BOGUS: reached empty item node, skipping.\n");
        return B_OK;    // no items, but then we would fail above already actually - done (at this level)
    }

    BMessage itemMsg;
    BString  itemId;

    // add a unique Snowflake ID for all items in this level
    for (int32 item = 0; item < count; item++) {
        itemMsg.MakeEmpty();
        status = pluginReply->FindMessage(SENSEI_ITEM, item, &itemMsg);

        if (status == B_OK) {
            // enrich IF plugin has not added its own ID at current index
            if (! pluginReply->HasString(SENSEI_ITEM_ID, item)) {
                if (itemMsg.IsEmpty()) {    // ignore empty fillers for ID generation
                    itemId = "";            // but still add empty ID to keep structure intact!
                } else {
                    itemId = GenerateId();  // only generate for items with content
                }
                pluginReply->AddString(SENSEI_ITEM_ID, itemId);
            }

            // and recurse to enrich sub item
            status = AddItemIdToPluginResult(&itemMsg);

            if (status == B_OK) {
                status = pluginReply->ReplaceMessage(SENSEI_ITEM, item, &itemMsg);
            }
        }
        if (status != B_OK) {
            ERROR("error handling item %d: %s\n", item, strerror(status));
            return status;
        }
    }

    return status;
}

status_t RelationHandler::GetPluginsForTypeAndFeature(
    const char* mimeType,
    const char* feature,
    BMessage* pluginConfig)
{
    BString predicate(SEN_TYPE "==" SENSEI_PLUGIN_TYPE " && " SENSEI_PLUGIN_FEATURE_ATTR ":");
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

status_t RelationHandler::GetPluginConfig(
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

    // retrieve type and attribute mapping config from plugin resources
    BMessage typeMappings;
    BMessage attrMappings;

    result = GetAttrMessage(&node, SENSEI_TYPE_MAPPING, &typeMappings);
    if (result == B_OK)
        result = GetAttrMessage(&node, SENSEI_ATTR_MAPPING, &attrMappings);

    if (result != B_OK) {
        return result;
    }

    // add mapping from plugin's output types to the plugin signature so we can resolve the relation later.
    BMessage typesToPlugins;
    typesToPlugins.AddString(pluginMimeType, pluginSig);
    pluginConfig->AddMessage(SENSEI_TYPES_PLUGINS_KEY, &typesToPlugins);

    // store default type separately if available for easier access and remove from individual type mappings
    BString defaultType;
    result = typeMappings.FindString(SENSEI_DEFAULT_TYPE, &defaultType);

    if (result == B_OK) {
        pluginConfig->AddString(SENSEI_DEFAULT_TYPE_KEY, defaultType);
        // and remove from type mappings
        typeMappings.RemoveData(SENSEI_DEFAULT_TYPE);
    }

    // add default attribute mappings if not specified otherwise
    if (!attrMappings.HasString(SENSEI_LABEL))
        attrMappings.AddString(SENSEI_LABEL, "SEN:REL:Label");

    // add mapping configs to plugin config
    pluginConfig->AddMessage(SENSEI_TYPE_MAPPING, &typeMappings);
    pluginConfig->AddMessage(SENSEI_ATTR_MAPPING, &attrMappings);

    return result;
}

status_t RelationHandler::GetAttrMessage(const BNode* node, const char* name, BMessage* attrMessage)
{
    attr_info attrInfo;
    status_t  result;

    if ((result = node->GetAttrInfo(name, &attrInfo)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            ERROR("expected plugin config attribute '%s' not found in plugin.\n", name);
        } else {
            ERROR("error getting plugin config for '%s' from attribute info for plugin: %s\n",
                name, strerror(result));
        }
        return result;
    }

    char* attrValue = new char[attrInfo.size + 1];
    result = node->ReadAttr(
            name,
            B_MESSAGE_TYPE,
            0,
            attrValue,
            attrInfo.size);

    if (result == 0) {
        ERROR("no %s config found for plugin.\n", name);
        return B_ENTRY_NOT_FOUND;
    } else if (result < 0) {
        ERROR("failed to read mappings from attribute %s of plugin: %s\n", name, strerror(result));
        return result;
    }

    return attrMessage->Unflatten(attrValue);
}

const char* RelationHandler::GetMimeTypeForRef(const entry_ref *ref) {
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
