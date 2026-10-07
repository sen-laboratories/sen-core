/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */

#include <sen/Sen.h>
#include <sen/SenOntoCore.h>
#include "SenServer.h"
#include "../relations/RelationHandler.h"

#include <errno.h>
#include <stdio.h>

#include <fs_index.h>

#include <AppFileInfo.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MimeType.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <VolumeRoster.h>
#include <Volume.h>
#include <spdlog/spdlog.h>


int main(int argc, char* argv[])
{
	spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
	if (const char* level = getenv("SEN_LOG_LEVEL"))
		spdlog::set_level(spdlog::level::from_str(level));

	SenServer* app = new(std::nothrow) SenServer();
    status_t status = app->InitCheck();
	if (status != B_OK) {
        fprintf(stderr, "failed to start SEN Server: %s\n", strerror(status));
		return status;
    }

	app->Run();

	delete app;
	return 0;
}

SenServer::SenServer() : BApplication(sen::kServerSignature)
{
	// setup feature-specific handlers for initializing SEN modules and later redirecting messages appropriately
    relationHandler  = new RelationHandler();
    senConfigHandler = new SenConfigHandler();

	// watch all mounted volumes for new entries to ensure that copies get their own SEN:ID, and be told about volumes that
	// are mounted later
	BVolumeRoster volRoster;
	BVolume volume;
	while (volRoster.GetNextVolume(&volume) == B_OK)
		WatchVolume(volume.Device());

	watch_node(NULL, B_WATCH_MOUNT, this);
}

void SenServer::WatchVolume(dev_t device)
{
	BVolume volume(device);
	if (volume.InitCheck() != B_OK || ! volume.KnowsQuery() || ! volume.KnowsAttr() || volume.IsReadOnly())
		return;

	EnsureIndices(device);

	status_t result = watch_volume(device, B_WATCH_NAME, this);
	if (result != B_OK) {
		spdlog::error("failed to watch volume {}: {}", (int)device, strerror(result));
	}
}

void SenServer::EnsureIndices(dev_t device)
{
	for (unsigned i = 0; i < sen::onto::core::kIndexCount; i++) {
		const sen::onto::core::Index& index = sen::onto::core::kIndices[i];
		if (fs_create_index(device, index.name, index.type, 0) == 0) {
			spdlog::info("created index {} on volume {}", index.name, (int)device);
		} else if (errno != B_FILE_EXISTS) {
			spdlog::error("failed to create index {} on volume {}: {}", index.name, (int)device, strerror(errno));
		}
	}
}

SenServer::~SenServer()
{
    spdlog::info("Goodbye:)");
    stop_watching(this);
}

void SenServer::ReadyToRun()
{
    status_t status = senConfigHandler->Init();
    if (status != B_OK) {
        // critical, abort
        spdlog::info("critical error, aborting.");
        Quit();
    }

    BApplication::ReadyToRun();
}

void SenServer::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result;

	switch (message->what) {
		case sen::cmd::kCoreInfo:
		{
		 	result = B_OK;
		 	reply->what = sen::cmd::kReplyInfo;
		 	// get info from resource
            app_info appInfo;
            be_app->GetAppInfo(&appInfo);

            BFile file(&appInfo.ref, B_READ_ONLY);
            BAppFileInfo appFileInfo(&file);

            if (appFileInfo.InitCheck() == B_OK)
            {
                version_info versionInfo;
                if (appFileInfo.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) == B_OK) {
                    BString info(versionInfo.short_info);
                    BString version;
                    version << versionInfo.major << "." << versionInfo.middle << "." << versionInfo.minor;
                    info << " " << version;

                    reply->AddString("result", info.String());
                    reply->AddString("shortDescription", versionInfo.short_info);
                    reply->AddString("longDescription", versionInfo.long_info);
                    reply->AddString("version", version);
                    reply->AddInt32("versionMajor", versionInfo.major);
                    reply->AddInt32("versionMiddle", versionInfo.middle);
                    reply->AddInt32("versionVariety", versionInfo.variety);
                    reply->AddInt32("versionInternal", versionInfo.internal);
                    break;
                }
            }
            reply->AddString("result", "Error retrieving appInfo from resource!");
		 	break;
		}
		case sen::cmd::kCoreStatus:
		{
		 	result = B_OK;
		 	reply->what = sen::cmd::kReplyStatus;

		 	reply->AddString("status", "operational");
		 	reply->AddBool("healthy", true);

		 	break;
		}
        case sen::cmd::kCoreTest:
		{
            result = B_OK;
            reply->what = sen::cmd::kCoreTest;

            spdlog::info("TSID test...");
            BPath path;
            if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK)
            {
                spdlog::error("could not find user settings directory, falling back to /tmp.");
                path.SetTo("/tmp");
            }
            path.Append("sen");
            BDirectory outputDir;
            result = outputDir.CreateDirectory(path.Path(), NULL);
            if (result != B_OK && result != B_FILE_EXISTS) {
                spdlog::error("failed to set up test directory: {}", strerror(result));
                break;
            }
            outputDir.SetTo(path.Path());
            BFile file;
            int32 numFiles = message->GetInt32("count", 1000);

            // create some temp files and ensure they are unique
            for (int32 i = 0; i < numFiles; i++) {
                BString tsidString = relationHandler->GenerateId();
                const char* tsid = tsidString.String();
                spdlog::info("TSID: {}", tsid);
                result = file.SetTo(&outputDir, tsid, B_CREATE_FILE);
                if (result == B_OK) {
                    result = file.Flush();
                } else {
                    if (result == B_FILE_EXISTS) {
                        spdlog::error("test FAILED, ID {} not unique!", tsid);
                    } else {
                        spdlog::error("aborting test, internal error: {}", strerror(result));
                    }
                    break;
                }
                file.Unset();
            }
            reply->AddBool("testPassed", result == B_OK);
            break;
        }
        case sen::cmd::kQueryRefForId:
        {
            result = B_OK;
            BString id;
            entry_ref ref;

            // TODO: support arrays like in sen::cmd::kQueryIdForRef, needs slight refactoring
            if ((result = message->FindString(sen::attr::kId, &id)) == B_OK) {
                if ((result = relationHandler->QueryForUniqueSenId(id.String(), &ref)) == B_OK) {
                    reply->AddRef("ref", new entry_ref(ref.device, ref.directory, ref.name));
                }
            }
            break;
        }
        case sen::cmd::kQueryIdForRef:
        {
            type_code   type;
            int32       count;

            result = message->GetInfo("refs", &type, &count);
            if (result != B_OK || type != B_REF_TYPE) {
                spdlog::error("unexpected type / missing refs parameter!");
                result = B_BAD_VALUE;
                break;
            }

            char        id[sen::id::kLength];
            entry_ref   ref;
            bool        createIfMissing = message->GetBool("createIfMissing");

            for (int i = 0; i < count; i++) {
                if ((result = message->FindRef("refs", i, &ref)) == B_OK) {
                    result = relationHandler->GetOrCreateId(&ref, id, createIfMissing);
                    if (result == B_OK) {
                        reply->AddString("ids", id);
                    }
                } // ignore not found or failed queries, caller will get valid result or no result
            }
            break;
        }
        case B_NODE_MONITOR:
        {
            result = B_OK;
            int32 opcode;

            if (message->FindInt32("opcode", &opcode) == B_OK) {
                switch (opcode) {
                    case B_DEVICE_MOUNTED: {
                        dev_t device;
                        if (message->FindInt32("new device", &device) == B_OK) {
                            spdlog::info("volume {} mounted", (int)device);
                            WatchVolume(device);
                        }
                        break;
                    }
                    case B_ENTRY_CREATED: {
                        entry_ref ref;
                        BString name;

                        message->FindString("name", &name);
                        message->FindInt32("device", &ref.device);
                        message->FindInt64("directory", &ref.directory);
                        ref.set_name(name);

                        BNode node(&ref);
                        BPath path(&ref);

                        // only files that already have an ID can be copies
                        char id[sen::id::kLength];
                        result = relationHandler->GetOrCreateId(&ref, id);
                        if (result != B_OK) {
                            break;
                        }

                        // the ID is queried on all volumes: a second file with the same ID means that this entry is a copy
                        entry_ref existingEntry;
                        result = relationHandler->QueryForUniqueSenId(id, &existingEntry);
                        if (result == B_DUPLICATE_REPLY) {
                            spdlog::info("SEN:ID {} of {} exists already, this is a copy: removing its identity.", id, path.Path());

                            int32 attrCount = RemoveIdentityAttrs(&node);
                            if (attrCount >= 0) {
                                spdlog::info("removed {} attribute(s) from file {}", attrCount, path.Path());
                            } else  {
                                spdlog::error("failed to remove attributes from node {}: {}", path.Path(), strerror(attrCount));
                            }
                        } else {
                            spdlog::info("ignoring possible move of {}, SEN:ID {} is still unique.", name.String(), id);
                        }
                        break;
                    }
                }
            }
            break;
        }
        // Config - redirect to SenConfigHandler, except for trivial case
        case sen::cmd::kConfigGet:
        {
            senConfigHandler->GetConfig(reply);
            break;
        }
        case sen::cmd::kClassificationAdd:
        case sen::cmd::kClassificationGet:
        case sen::cmd::kClassificationFind:	// fallthrough
        {
            senConfigHandler->MessageReceived(message);
            return; // done
        }
        // Relations - redirect to separate RelationsHndler
        case sen::cmd::kRelationsGet:
        case sen::cmd::kRelationsGetAll:
        case sen::cmd::kRelationsGetSelf:
        case sen::cmd::kRelationsGetAllSelf:
        case sen::cmd::kRelationsGetCompatible:
        case sen::cmd::kRelationsGetCompatibleTypes:
		case sen::cmd::kRelationAdd:
		case sen::cmd::kRelationRemove:
		case sen::cmd::kRelationsRemoveAll: // fallthrough
        {
            relationHandler->MessageReceived(message);
            return; // done
        }
		default:
		{
            result = B_UNSUPPORTED;
            spdlog::info("SEN Server: unknown message '{}' received." B_UTF8_ELLIPSIS, message->what);
		}
	}

	reply->AddInt32("resultCode", result);
	reply->AddString("result", strerror(result));

	message->SendReply(reply);
}

int32 SenServer::RemoveIdentityAttrs(BNode* node) {
    // collect first: removing while iterating would skip attributes
    std::vector<BString> names;
    char attrName[B_ATTR_NAME_LENGTH];
    node->RewindAttrs();
    while (node->GetNextAttrName(attrName) == B_OK) {
        BString name(attrName);
        if (name == sen::attr::kId
                || name.StartsWith(sen::attr::kTo)
                || name.StartsWith(sen::attr::kMeta)
                || name.StartsWith(sen::attr::kRelationPrefix)) {
            names.push_back(name);
        }
    }

    int32 removed = 0;
    for (const BString& name : names) {
        status_t result = node->RemoveAttr(name.String());
        if (result != B_OK) {
            spdlog::error("failed to remove SEN attribute {}: {}", name.String(), strerror(result));
            return result;
        }
        spdlog::info("removed SEN attribute {}", name.String());
        removed++;
    }
    return removed;
}
