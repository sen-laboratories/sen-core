/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <sen/Sen.h>
#include "SenServer.h"
#include "../relations/RelationHandler.h"

#include <stdio.h>

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

	// see also https://www.haiku-os.org/legacy-docs/bebook/BQuery_Overview.html#id611851
    BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

    // watch for move (rename) and copy operations to ensure our SEN ID stays unique.
    watch_volume(bootVolume.Device(), B_WATCH_NAME, this);
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
                    case B_ENTRY_CREATED: {
                        entry_ref ref;
                        BString name;

                        message->FindString("name", &name);
                        message->FindInt32("device", &ref.device);
                        message->FindInt64("directory", &ref.directory);
                        ref.set_name(name);

                        BNode node(&ref);
                        BPath path(&ref);

                        char id[sen::id::kLength];
                        result = relationHandler->GetOrCreateId(&ref, id);
                        if (result != B_OK) {
                            break;
                        }

                        entry_ref existingEntry;

                        if ((result = relationHandler->QueryForUniqueSenId(id, &existingEntry)) == B_OK) {
                            BNode existingNode(&existingEntry);
                            if (existingNode == node) {
                                spdlog::info("SEN:ID {} refers to same node {}, nothing to do.",
                                    id, node.Dup());
                                break;
                            }
                            // delete all SEN attributes of copy
                            spdlog::info("found SEN:ID {} with exising node {}, removing attributes from copy...",
                                id, path.Path());

                            int32 attrCount = RemoveSenAttrs(&node);
                            if (attrCount >= 0) {
                                spdlog::info("removed {} attribute(s) from file {}", attrCount, path.Path());
                            } else  {
                                spdlog::error("failed to remove attributes from node {}: {}", path.Path(), strerror(result));
                            }
                        } else {
                            spdlog::info("ignoring possible move of {}, SEN:ID {} is still unique.",
                                name.String(), id);
                        }
                        break;
                    }
                    break;
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

int32 SenServer::RemoveSenAttrs(BNode* node) {
    char attrName[B_ATTR_NAME_LENGTH];
    int attrCount = 0;
    status_t result;

    while ((result = node->GetNextAttrName(attrName)) >= 0) {
        if (result < 0) {
            spdlog::error("failed to get next attribute from file: {}, " "possible SEN attributes left!", result);
            break;
        }
        if (BString(attrName).StartsWith(sen::attr::kPrefix)) {
            spdlog::info("checking SEN attribute {}...", attrName);
            result = node->RemoveAttr(attrName);
            if (result != B_OK) {
                spdlog::error("failed to remove SEN attribute {}: {}",
                        attrName, strerror(result));
                break;
            } else {
                spdlog::info("removed SEN attribute {}", attrName);
                attrCount++;
            }
        }
    }
    if (result == B_OK) {
        return attrCount;
    } else {
        return result;
    }
}
