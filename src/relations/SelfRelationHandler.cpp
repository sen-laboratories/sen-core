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
        reply->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfigs);
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
    BMessage relationConfig;

    status = GetRelationConfig(relationType, &relationConfig);
    if (status != B_OK) {
        LOG("failed to get relation config for type %s: %s\n", relationType, strerror(status));
        return status;
    }

    // add to reply
    BMessage configMap;
    configMap.AddMessage(relationType, &relationConfig);
    reply->AddMessage(SEN_RELATION_CONFIG_MAP, &relationConfig);

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

    LOG(("* got plugin config:\n"));
    pluginConfig.PrintToStream();

    // client may send the desired plugin signature already, saving us the hassle
    BString pluginTypeParam;

	if (GetMessageParameter(message, SENSEI_PLUGIN_KEY, &pluginTypeParam, NULL, true, false)  == B_OK) {
        const char* pluginSig = pluginTypeParam.String();
        LOG("got plugin signature %s, jumping to launch plugin.\n", pluginSig);
		return ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, &pluginConfig, reply);
	}

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

    result = ResolveSelfRelationsWithPlugin(pluginSig, &sourceRef, &pluginConfig, reply);
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
    const BMessage* pluginConfig,
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
    BMessage   pluginReply;

    result = pluginMessenger.SendMessage(&refsMsg, &pluginReply);

    // check result from communication
    if (result != B_OK) {
        ERROR("failed to communicate with plugin %s: %s\n", pluginSig, strerror(result));
        pluginReply.PrintToStream();
        return result;
    }

    // check actual plugin result
    result = pluginReply.GetInt32("result", B_OK);
    if (result != B_OK) {
        ERROR("error in plugin execution: %s\n", strerror(result));
        pluginReply.PrintToStream();
        return result;
    }

    // remove plugin result
    pluginReply.RemoveName(SENSEI_RESULT);

    // convert to common relation properties mapped to MIME type attribute names, using the type_mapping
    // provided by SENSEI (both technically optional but usually needed and encouraged)
    BMessage typeMapping;
    pluginConfig->FindMessage(SENSEI_TYPE_MAPPING, &typeMapping);

    // same for attributes (e.g. page -> SEN:REL:page)
    BMessage attrMapping;
    pluginConfig->FindMessage(SENSEI_ATTR_MAPPING, &attrMapping);

    // replace the placeholder "self" ID with the sourceRef's inode, we simply add it as mapping here
    BString srcId;
    GetInodeForRef(sourceRef, &srcId);

    //TODO: map inode in valueMap or just as param?

    // plugins may use an abstract item shortcut
    attrMapping.AddString(SENSEI_ITEM, SEN_RELATIONS);

    // add unique node ID to all nested nodes for easier tracking (e.g. Tracker selected node->relation folder)
    BMessage pluginReplyTransformed;
    result = TransformPluginResult(&pluginReply, &typeMapping, &attrMapping, &pluginReplyTransformed);

    if (result != B_OK) {
        ERROR("could not transform plugin result: %s\nResult so far:\n", strerror(result));
        pluginReplyTransformed.PrintToStream();
        return result;
    }

    reply->what = SENSEI_MESSAGE_RESULT;
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
        LOG("  - skipping empty sub message.\n");
        return B_OK;
    }

    // get number of data members in this item message
    fieldCount = itemMsg->CountNames(B_ANY_TYPE);

    // get cardinality of items in this message (format requires a label and same count for all fields)
    status = itemMsg->GetInfo(B_ANY_TYPE, 0, NULL, NULL, &itemCount);
    if (status != B_OK) {
        ERROR("could not inspect message: %s\n", strerror(status));
        return status;
    }

    LOG("processing itemMsg with %d data members and cardinality of %d.\n", fieldCount, itemCount);

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
                ERROR("error inspecting item %d, field %d: %s\n", item, field, strerror(status));
                return status;
            }
            if (itemCount != elementCount) {
                ERROR("  ? invalid/unsupported message format: non-uniform item count at field %s, %d vs. %d (current).",
                        fieldName, itemCount, elementCount);
                return B_BAD_VALUE;
            }

            LOG("processing item %02d / %02d, field %s\t(%02d / %02d).\n",
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
                    LOG("  > processing sub item...\n");

                    // and recurse to enrich sub item
                    status = TransformPluginResult(&childMsg, typeMapping, attrMapping, &childResult);
                    LOG("status after recursion: %s\n", strerror(status));

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
                ERROR("  x failed to process item %d, field '%s' ['%s']: %s\n",
                        item, fieldName, commonName, strerror(status));
                return status;
            }
        }  // field loop

        if (status == B_OK) {
            LOG("* got %d nested and %d flat properties\n", nestedProperties, flatProperties);

            // enrich IF plugin has requested an inode as target
            if (propertiesMsg.HasString(SEN_TO_INO)) {
                const char* itemId = GenerateId();
                status = propertiesMsg.AddString(SEN_TO_INO, itemId);
            }

            // enrich IF plugin has not added its own item ID
            if (! propertiesMsg.HasString(SENSEI_ITEM_ID)) {
                const char* itemId = GenerateId();
                status = propertiesMsg.AddString(SENSEI_ITEM_ID, itemId);
            }

            if (status == B_OK) {
                if (nestedProperties > 0 && flatProperties == 0) {
                    // ommit empty intermediary nodes when there are just sub nodes at this level
                    status = itemResult->Append(propertiesMsg);
                } else {
                    status = itemResult->AddMessage(SEN_RELATIONS, &propertiesMsg);
                }
            }
            if (status != B_OK) {
                ERROR("  x failed to add properties to result: %s\n", strerror(status));
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
		LOG("WARNING: could not get inode for srcRef %s: %s\n", srcRef->name, strerror(result) );
		// fall back
		*inode << srcRef->device << "_" << srcRef->directory << "_" << srcRef->name;
        result = B_OK;
	}

	return result;
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
