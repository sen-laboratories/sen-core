/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include <cassert>

#include <AppFileInfo.h>
#include <fs_attr.h>
#include <Looper.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>
#include <VolumeRoster.h>
#include <Volume.h>

#include "RelationHandler.h"
#include "QueryUtil.h"
#include "../server/Reply.h"
#include <sen/Sen.h>
#include <sen/Sensei.h>
#include <spdlog/spdlog.h>

status_t RelationHandler::GetSelfRelations(const BMessage* message, BMessage* reply) {
	entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, sen::key::kSourceRef, nullptr, &sourceRef))  != B_OK) {
		return status;
	}

    BString sourceTypeString = GetMimeTypeForRef(&sourceRef);
    const char* sourceType = sourceTypeString.String();

    // query for all compatible extractors and return their generated collected output type
    spdlog::info("query for extractors to handle file type {}", sourceType);
    BMessage pluginConfig;

    status = GetPluginsForTypeAndFeature(sourceType, sensei::feature::kExtract, &pluginConfig);
    if (status != B_OK) {
        return status;
    }

    spdlog::info("got types/plugins config for source type {}:", sourceType);
    pluginConfig.PrintToStream();

    reply->what = sensei::cmd::kResult;
    reply->AddMessage(sensei::key::kPluginConfig, new BMessage(pluginConfig));

    // transparently add type mappings as relations for consistent uniform handling from outside (e.g. Tracker)
    BStringList relationTypes;

    BMessage typeMappings;
    status = pluginConfig.FindMessage(sensei::key::kTypeMapping, &typeMappings);
    if (status != B_OK) {
        spdlog::error("could not find expected type mappings, aborting: {}", strerror(status));
        if (status != B_NAME_NOT_FOUND)
            return status;
        else
            return B_OK;    // empty
    }

    // add all types (values) as relations
    for (int t = 0; t < typeMappings.CountNames(B_STRING_TYPE); t++) {
        char *alias;
        BString relationTypeName;

        status = typeMappings.GetInfo(B_STRING_TYPE, t, &alias, NULL);
        if (status == B_OK)
            status = typeMappings.FindString(alias, t, &relationTypeName);
        if (status != B_OK) {
            spdlog::error("failed to get type mapping #{}: {}", t, strerror(status));
            continue;
        }
        relationTypes.Add(relationTypeName);
    }

    BString defaultType = pluginConfig.GetString(sensei::key::kDefaultType, "");
    if (! defaultType.IsEmpty())
        relationTypes.Add(defaultType);

    reply->AddStrings(sen::key::kRelations, relationTypes);

    // get relation configs (always needed for self relations)
    BMessage relationConfigs;
    status = GetRelationConfigs(&relationTypes, &relationConfigs);
    if (status == B_OK) {
        reply->AddMessage(sen::key::kRelationConfigMap, &relationConfigs);
    }

    return status;
}

status_t RelationHandler::GetSelfRelationsOfType (const BMessage* message, BMessage* reply) {
    entry_ref sourceRef;
	status_t  status;

    if ((status = GetMessageParameter(message, sen::key::kSourceRef, nullptr, &sourceRef))  != B_OK) {
		return status;
	}

    BString sourceMimeTypeString = GetMimeTypeForRef(&sourceRef);
    if (sourceMimeTypeString.IsEmpty()) {
        return B_ERROR;
    }
    const char* sourceMimeType = sourceMimeTypeString.String();

    BString relationTypeParam;
    // relation type for self relations is one of the possible output types of compatible extractors.
	if (GetMessageParameter(message, sen::key::kRelationType, &relationTypeParam, NULL, true)  != B_OK) {
		return B_BAD_VALUE;
	}
    const char* relationType = relationTypeParam.String();

    // retrieve relation config from MIME DB in the filesystem
    BMessage relationConfig;

    status = GetRelationConfig(relationType, &relationConfig);
    if (status != B_OK) {
        spdlog::info("failed to get relation config for type {}: {}", relationType, strerror(status));
        return status;
    }

    // add to reply
    BMessage configMap;
    configMap.AddMessage(relationType, &relationConfig);
    reply->AddMessage(sen::key::kRelationConfigMap, &configMap);

    status_t result;
    bool clientHasConfig = false;

    // sender MAY send an existing plugin config for this type from a previous call to save re-query
    BMessage pluginConfig;

    result = message->FindMessage(sensei::key::kPluginConfig, &pluginConfig);
    if (result != B_OK) {
        if (result == B_NAME_NOT_FOUND) {
            spdlog::info("fresh query for suitable plugins for relation type {}...", relationType);

            result = GetPluginsForTypeAndFeature(sourceMimeType, sensei::feature::kExtract, &pluginConfig);
            if (result != B_OK) {
                return result;  // already handled, just pass on
            }
            spdlog::info("got fresh plugin config:");
         } else {
            spdlog::error("couldn't look up plugins from message: {}", strerror(result));
            return result;
         }
    } else {
        spdlog::info("got existing plugin config for relation type {}:", relationType);
        clientHasConfig = true;
    }

    spdlog::info("* got plugin config:");
    pluginConfig.PrintToStream();
    // TODO: merge optional relation config provided in plugin config into MIME relation config

    // client may send the desired plugin signature already, saving us the hassle
    BString pluginTypeParam;

	if (GetMessageParameter(message, sensei::key::kPlugin, &pluginTypeParam, NULL, true)  == B_OK) {
        const char* pluginSig = pluginTypeParam.String();
        spdlog::info("got plugin signature {}, jumping to launch plugin.", pluginSig);
		return ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, &pluginConfig, reply);
	}

    // get type->plugin map
    BMessage typeToPlugins;

    result = pluginConfig.FindMessage(sensei::key::kTypesPlugins, &typeToPlugins);
    if (result != B_OK) {
        spdlog::error("failed to look up type->plugin map for relation type {}: {}", relationType, strerror(result));
        return result;
    }

    // the plugin that handles this type of file (the map is keyed by the MIME type of the source, see GetPluginConfig)
    // todo: check for 1:N mappings, shouldn't happen, we assume 1:1
    BString pluginType;
    result = typeToPlugins.FindString(sourceMimeType, &pluginType);
    if (result != B_OK) {
        spdlog::error("failed to look up the plugin for file type {} (relation type {}): {}", sourceMimeType, relationType,
            strerror(result));
        return result;
    }

    const char* pluginSig = pluginType.String();

    result = ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, &pluginConfig, reply);
    if (result != B_OK) {
        spdlog::error("failed to resolve relations of type {} with plugin {}: {}", relationType, pluginSig, strerror(result));
        return result;
    }

    // send back current plugin config if client did not have it
    if (!clientHasConfig) {
        reply->Append(pluginConfig);
    }

    return result;
}

namespace {

// Receives a plugin's reply in a port owned by sen_server. A synchronous
// BMessenger::SendMessage() with reply hands ownership of the reply port to the
// target's team, so a plugin that quits right after replying (as they all do)
// takes the port, and the not yet read reply, down with it - sen_server then
// fails with B_BAD_PORT_ID. This is easy to hit on a single CPU where the reader
// is often not scheduled before the plugin has exited.
class PluginReplyCollector : public BLooper {
public:
    PluginReplyCollector()
        :
        BLooper("sen plugin reply"),
        fSem(create_sem(0, "sen plugin reply"))
    {
    }

    virtual ~PluginReplyCollector()
    {
        delete_sem(fSem);
    }

    virtual void MessageReceived(BMessage* message)
    {
        fReply = *message;
        release_sem(fSem);
    }

    status_t WaitForReply(BMessage* reply, team_id pluginTeam)
    {
        for (;;) {
            status_t status = acquire_sem_etc(fSem, 1, B_RELATIVE_TIMEOUT, 250000);
            if (status == B_TIMED_OUT) {
                team_info info;
                if (get_team_info(pluginTeam, &info) == B_OK)
                    continue;

                // plugin is gone: give a reply that is still in flight some time to arrive
                status = acquire_sem_etc(fSem, 1, B_RELATIVE_TIMEOUT, 1000000);
                if (status == B_TIMED_OUT)
                    return B_BAD_TEAM_ID;
            }
            if (status == B_OK)
                *reply = fReply;

            return status;
        }
    }

private:
    sem_id   fSem;
    BMessage fReply;
};

}   // namespace

//TODO: move to separate class and support dynamic relations via plugins for all relation types!
status_t RelationHandler::ResolveSelfRelationsWithPlugin(
    const char* pluginSig,
    const entry_ref* sourceRef,
    const BMessage* pluginConfig,
    BMessage* reply)
{
    spdlog::info("got plugin app signature: {}", pluginSig);

    // execute plugin and return result
    // plugins are B_MULTIPLE_LAUNCH and quit right after replying, so talk to exactly
    // the instance we launch here: resolving the messenger by signature alone may pick
    // a previous instance that is already shutting down (B_BAD_PORT_ID).
    team_id pluginTeam = -1;
    status_t result = be_roster->Launch(pluginSig, (BMessage*)NULL, &pluginTeam);
    if (result != B_OK) {
        spdlog::error("failed to launch plugin {}: {}", pluginSig, strerror(result));
        return result;
    }

    // build refs received for plugin as input param
    BEntry sourceEntry(sourceRef);
    result = sourceEntry.InitCheck();

    if (result != B_OK) {
        spdlog::error("failed to get ref for path {}: {}", sourceRef->name, strerror(result));
        return result;
    }

    BMessage refsMsg(B_REFS_RECEIVED);
    refsMsg.AddRef("refs", sourceRef);
    refsMsg.AddBool(sen::conf::kSelf, true);

    spdlog::info("Sending refs to plugin {}:", pluginSig);
    refsMsg.PrintToStream();

    BMessenger pluginMessenger(pluginSig, pluginTeam);
    BMessage   pluginReply;

    PluginReplyCollector* collector = new PluginReplyCollector();
    collector->Run();

    result = pluginMessenger.SendMessage(&refsMsg, BMessenger(collector));
    if (result == B_OK)
        result = collector->WaitForReply(&pluginReply, pluginTeam);

    if (collector->Lock())
        collector->Quit();

    // check result from communication
    if (result != B_OK) {
        spdlog::error("failed to communicate with plugin {}: {}", pluginSig, strerror(result));
        pluginReply.PrintToStream();
        return result;
    }

    // check actual plugin result
    result = pluginReply.GetInt32("result", B_OK);
    if (result != B_OK) {
        spdlog::error("error in plugin execution: {}", strerror(result));
        pluginReply.PrintToStream();
        return result;
    }

    if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("reply of plugin {}:", pluginSig);
        pluginReply.PrintToStream();
    }

    // remove plugin result code
    pluginReply.RemoveName(sensei::key::kResult);

    // convert to common relation properties mapped to MIME type attribute names, using the type_mapping
    // provided by SENSEI (both technically optional but usually needed and encouraged)
    BMessage typeMapping;
    pluginConfig->FindMessage(sensei::key::kTypeMapping, &typeMapping);

    // same for attributes (e.g. page -> SEN:REL:page)
    BMessage attrMapping;
    pluginConfig->FindMessage(sensei::key::kAttrMapping, &attrMapping);

    // replace the placeholder "self" ID with the sourceRef's inode, we simply add it as mapping here
    BString srcId;
    GetInodeForRef(sourceRef, &srcId);

    // plugins may use an abstract item shortcut
    attrMapping.AddString(sensei::key::kItem, sen::key::kRelations);
    attrMapping.AddString(sensei::key::kTo,   sen::attr::kTo);

    // add unique node ID to all nested nodes for easier tracking (e.g. Tracker selected node->relation folder)
    BMessage pluginReplyTransformed;
    // pass in root node if possible (convention)
    BMessage rootNode;

    result = pluginReply.FindMessage(sensei::key::kItem, &rootNode);
    if (result != B_OK) {
        result = pluginReply.FindMessage(sen::key::kRelations, &rootNode);
    }

    if (result == B_NAME_NOT_FOUND) {
        // the plugin found nothing (a document without bookmarks, a source file without includes): that is an empty
        // result, not an error
        spdlog::info("plugin {} found no relations in {}.", pluginSig, sourceRef->name);
        reply->what = sensei::cmd::kResult;
        reply->AddRef("refs", sourceRef);
        sen::reply::SetStatus(reply, sen::status::kNoContent);
        sen::reply::SetDetail(reply, "no relations found");
        return B_OK;
    }

    if (result == B_OK)
        result = TransformPluginResult(&rootNode, &typeMapping, &attrMapping, &pluginReplyTransformed);

    if (result != B_OK) {
        spdlog::error("could not transform plugin result: {}\nResult so far:", strerror(result));
        pluginReplyTransformed.PrintToStream();
        return result;
    }

    reply->what = sensei::cmd::kResult;
    reply->AddRef("refs", sourceRef);
    reply->Append(pluginReplyTransformed);

    return B_OK;
}

status_t RelationHandler::TransformPluginResult(
    const BMessage *itemMsg,
    const BMessage *typeMapping,    // TODO: not yet handled here
    const BMessage *attrMapping,
    BMessage       *itemResult)
{
    BMessage     propertiesMsg, childMsg, childResult;
    int32        fieldCount, elementCount, itemCount;
    type_code    type;
    status_t     status;

    // skip processing empty messages at any level
    if (itemMsg->IsEmpty()) {
        spdlog::info("  - skipping empty sub message.");
        return B_OK;
    }

    // get number of data members in this item message
    fieldCount = itemMsg->CountNames(B_ANY_TYPE);
    char *itemMsgName;

    // get cardinality of fields in this item message (format requires a label and same count for all fields)
    status = itemMsg->GetInfo(sensei::key::kLabel, &type, &itemCount);
    if (status != B_OK) {
        if (status == B_NAME_NOT_FOUND || type != B_STRING_TYPE) {
            spdlog::error("could not find expected LABEL in item message of plugin result.");
            return B_OK;    // fail gracefully
        }

        spdlog::error("could not inspect message: {}", strerror(status));
        return status;
    }

    spdlog::info("processing itemMsg with {} data members and cardinality of {}.", fieldCount, itemCount);

    for (int32 item = 0; item < itemCount; item++) {
        // prepare for new item properties
        propertiesMsg.MakeEmpty();
        int32 flatProperties = 0, nestedProperties = 0;

        for (int32 field = 0; field < fieldCount; field++) {
            char*        fieldName;
            type_code    fieldType;
            const void*  data;
            ssize_t	     size;

            status = itemMsg->GetInfo(B_ANY_TYPE, field, &fieldName, &fieldType, &elementCount);
            if (status == B_OK)
                status = itemMsg->FindData(fieldName, fieldType, item, &data, &size);

            if (status != B_OK) {
                spdlog::error("error inspecting item {}, field {}: {}", item, field, strerror(status));
                return status;
            }
            if (itemCount != elementCount) {
                spdlog::error("  ? invalid/unsupported message format: non-uniform item count at field {}, {} vs. {} (current).",
                        fieldName, itemCount, elementCount);
                return B_BAD_VALUE;
            }

            spdlog::info("processing item {:02} / {:02}, field {}\t({:02} / {:02}).",
                item + 1, itemCount, fieldName, field + 1, fieldCount);

            // map property name to common attribute name as per attribute map
            // default is to keep the name if no mapping was defined.
            // for self relations, the pseudo "SELF" ID maps to the inode (passed in by caller this way)
            const char *commonName = attrMapping->GetString(fieldName, fieldName);

            if (fieldType == B_MESSAGE_TYPE) {
                childMsg.MakeEmpty();
                childResult.MakeEmpty();

                status = itemMsg->FindMessage(fieldName, item, &childMsg);

                // only process B_MESSAGE_TYPE entries here
                if (status == B_OK) {
                    spdlog::info("  > processing sub item...");

                    // and recurse to enrich sub item
                    status = TransformPluginResult(&childMsg, typeMapping, attrMapping, &childResult);

                    if (status == B_OK) {
                        // ommit empty child nodes
                        if (! childResult.IsEmpty()) {
                            status = propertiesMsg.AddMessage(commonName, &childResult);
                            nestedProperties++;
                        }
                    }
                }
            } else  {
                // add flat property
                if (item == 0) {    // a little optimization since we know how many items we will add
                    status = propertiesMsg.AddData(commonName, fieldType, data, size, true, itemCount);
                } else {
                    status = propertiesMsg.AddData(commonName, fieldType, data, size);
                }

                if (status == B_OK) {
                    flatProperties++;
                }
            }

            if (status != B_OK) {
                spdlog::error("  x failed to process item {}, field '{}' ['{}']: {}",
                        item, fieldName, commonName, strerror(status));
                return status;
            }
        }  // field loop

        if (status == B_OK) {
            spdlog::info("* got {} nested and {} flat properties", nestedProperties, flatProperties);

            // possibly enrich IF item contains an ID
            const char* itemId = propertiesMsg.GetString(sensei::key::kItemId);

            if (itemId == NULL || strlen(itemId) == 0) {
                propertiesMsg.RemoveName(sensei::key::kItemId);
                status = propertiesMsg.AddString(sen::key::kItemId, GenerateId() );
            }

            if (nestedProperties > 0 && flatProperties == 0) {
                // ommit empty intermediary nodes when there are just sub nodes at this level
                status = itemResult->Append(propertiesMsg);
            } else {
                status = itemResult->AddMessage(sen::key::kRelations, &propertiesMsg);
            }

            if (status != B_OK) {
                spdlog::error("  x failed to add properties to result: {}", strerror(status));
            }
        }
    }  // item loop

    return status;
}

status_t RelationHandler::GetPluginsForTypeAndFeature(
    const char* mimeType,
    const char* feature,
    BMessage* pluginConfig)
{
    spdlog::info("  > looking for {} plugins", feature);

    std::vector<entry_ref> plugins;
    status_t result = sen::FindPlugins(feature, &plugins);
    if (result != B_OK) {
        spdlog::error("could not execute query for suitable SENSEI extractors: {}", strerror(result));
        return result;
    }

    int32 pluginCount = 0;
    for (const entry_ref& pluginRef : plugins) {
        BEntry entry(&pluginRef);
        BPath path(&pluginRef);
        spdlog::info("found plugin with path {}", path.Path());

        // get MIME-Type == application_signature of plugin to use as key later
        BFile pluginFile(&entry, B_READ_ONLY);
        if ((result = pluginFile.InitCheck()) != B_OK || !(pluginFile.IsFile())) {
            spdlog::error("failed to get appInfo of plugin file {}: {}", entry.Name(), strerror(result));
            return result;
        }
        BAppFileInfo pluginInfo(&pluginFile);
        char pluginAppSig[B_MIME_TYPE_LENGTH];
        if ((result = pluginInfo.GetSignature(pluginAppSig)) != B_OK) {
            spdlog::error("failed to get app signature of plugin file {}: {}", entry.Name(), strerror(result));
            return result;
        }
        spdlog::info("got plugin app signature: {}", pluginAppSig);

        // filter for supported input type
        if (pluginInfo.IsSupportedType(mimeType)) {
            // get supported output types and add to lookup map accordingly
            // todo: there may be more plugins per type, supporting different aspects and
            // returning different output type - later we need to detect and handle overlaps!
            spdlog::info("Adding extractor plugin {} for handling type {}", pluginAppSig, mimeType);

            entry_ref ref(pluginRef);
            result = GetPluginConfig(pluginAppSig, &ref, mimeType, pluginConfig);
            if (result != B_OK){
                spdlog::error("skipping compatible extractor plugin {} due to error: {}.", pluginAppSig, strerror(result));
                // better luck next time?
                continue;
            }

            pluginCount++;
        } else {
            spdlog::info("extractor plugin {} does not support type {}", pluginAppSig, mimeType);
        }
    }

    if (pluginCount == 0) {
        spdlog::info("no matching extractor found for type {}", mimeType);
        return B_OK;
    }

    spdlog::info("found {} suitable plugins.", pluginCount);
    spdlog::info("plugin output map is:");
    pluginConfig->PrintToStream();

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

    result = GetAttrMessage(&node, sensei::key::kTypeMapping, &typeMappings);
    if (result == B_OK)
        result = GetAttrMessage(&node, sensei::key::kAttrMapping, &attrMappings);

    if (result != B_OK) {
        return result;
    }

    // add mapping from plugin's output types to the plugin signature so we can resolve the relation later.
    BMessage typesToPlugins;
    typesToPlugins.AddString(pluginMimeType, pluginSig);
    pluginConfig->AddMessage(sensei::key::kTypesPlugins, &typesToPlugins);

    // store default type separately if available for easier access and remove from individual type mappings
    BString defaultType;
    result = typeMappings.FindString(sensei::key::kDefault, &defaultType);

    if (result == B_OK) {
        pluginConfig->AddString(sensei::key::kDefaultType, defaultType);
        // and remove from type mappings
        typeMappings.RemoveData(sensei::key::kDefault);
    }

    // add default attribute mappings if not specified otherwise
    if (!attrMappings.HasString(sensei::key::kLabel))
        attrMappings.AddString(sensei::key::kLabel, "SEN:REL:Label");

    // add mapping configs to plugin config
    pluginConfig->AddMessage(sensei::key::kTypeMapping, &typeMappings);
    pluginConfig->AddMessage(sensei::key::kAttrMapping, &attrMappings);

    return result;
}

status_t RelationHandler::GetAttrMessage(const BNode* node, const char* name, BMessage* attrMessage)
{
    attr_info attrInfo;
    status_t  result;

    if ((result = node->GetAttrInfo(name, &attrInfo)) != B_OK) {
        if (result == B_ENTRY_NOT_FOUND) {
            spdlog::error("expected plugin config attribute '{}' not found in plugin.", name);
        } else {
            spdlog::error("error getting plugin config for '{}' from attribute info for plugin: {}",
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
        spdlog::error("no {} config found for plugin.", name);
        return B_ENTRY_NOT_FOUND;
    } else if (result < 0) {
        spdlog::error("failed to read mappings from attribute {} of plugin: {}", name, strerror(result));
        return result;
    }

    return attrMessage->Unflatten(attrValue);
}

status_t RelationHandler::GetInodeForRef(const entry_ref* srcRef, BString* inode)
{
	// get inode as folder ID instead of SEN:ID, no need to create one for now
	BEntry srcEntry(srcRef);
	status_t result = srcEntry.InitCheck();

	if (result == B_OK) {
		struct stat srcStat;
		result = srcEntry.GetStat(&srcStat);

		if (result == B_OK) {
			*inode << srcStat.st_ino;
		}
	}
	if (result != B_OK) {
		spdlog::info("WARNING: could not get inode for srcRef {}: {}", srcRef->name, strerror(result) );
		// fall back
		*inode << srcRef->device << "_" << srcRef->directory << "_" << srcRef->name;
        result = B_OK;
	}

	return result;
}

BString RelationHandler::GetMimeTypeForRef(const entry_ref *ref) {
    BNode sourceNode(ref);
    status_t result;
    if ((result = sourceNode.InitCheck()) != B_OK) {
        spdlog::error("could not initialize source node {}: {}", ref->name, strerror(result));
        return BString();
    }
    BNodeInfo sourceInfo(&sourceNode);
    if ((result = sourceInfo.InitCheck()) != B_OK) {
        spdlog::error("could not initialize source node info for {}: {}", ref->name, strerror(result));
        return BString();
    }

    char sourceType[B_MIME_TYPE_LENGTH];
    if (sourceInfo.GetType(sourceType) == B_OK && sourceType[0] != '\0')
        return BString(sourceType);

    // a file without a type (made by a program that did not set it, or from the command line): guess it from the name and
    // the content, the way Tracker does
    BMimeType guessed;
    if (BMimeType::GuessMimeType(ref, &guessed) == B_OK && guessed.InitCheck() == B_OK) {
        spdlog::info("{} has no MIME type, guessed {}", ref->name, guessed.Type());
        return BString(guessed.Type());
    }

    spdlog::error("could not get MIME type for source node {}", ref->name);
    return BString();
}
