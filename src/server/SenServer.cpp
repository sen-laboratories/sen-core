/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "Sen.h"
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


int main(int argc, char* argv[])
{
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

SenServer::SenServer() : BApplication(SEN_SERVER_SIGNATURE)
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
    LOG("Goodbye:)\n");
    stop_watching(this);
}

void SenServer::ReadyToRun()
{
    status_t status = senConfigHandler->Init();
    if (status != B_OK) {
        // critical, abort
        LOG("critical error, aborting.\n");
        Quit();
    }

    BApplication::ReadyToRun();
}

void SenServer::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result;

	switch (message->what) {
		case SEN_CORE_INFO:
		{
		 	result = B_OK;
		 	reply->what = SEN_RESULT_INFO;
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
		case SEN_CORE_STATUS:
		{
		 	result = B_OK;
		 	reply->what = SEN_RESULT_STATUS;

		 	reply->AddString("status", "operational");
		 	reply->AddBool("healthy", true);

		 	break;
		}
        case SEN_CORE_TEST:
		{
            result = B_OK;
            reply->what = SEN_CORE_TEST;

            LOG("TSID test...");
            BPath path;
            if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &path) != B_OK)
            {
                ERROR("could not find user settings directory, falling back to /tmp.\n");
                path.SetTo("/tmp");
            }
            path.Append("sen");
            BDirectory outputDir;
            result = outputDir.CreateDirectory(path.Path(), NULL);
            if (result != B_OK && result != B_FILE_EXISTS) {
                ERROR("failed to set up test directory: %s\n", strerror(result));
                break;
            }
            outputDir.SetTo(path.Path());
            BFile file;
            int32 numFiles = message->GetInt32("count", 1000);

            // create some temp files and ensure they are unique
            for (int32 i = 0; i < numFiles; i++) {
                const char* tsid = relationHandler->GenerateId();
                LOG("TSID: %s\n", tsid);
                result = file.SetTo(&outputDir, tsid, B_CREATE_FILE);
                if (result == B_OK) {
                    result = file.Flush();
                } else {
                    if (result == B_FILE_EXISTS) {
                        ERROR("test FAILED, ID %s not unique!\n", tsid);
                    } else {
                        ERROR("aborting test, internal error: %s\n", strerror(result));
                    }
                    break;
                }
                file.Unset();
            }
            reply->AddBool("testPassed", result == B_OK);
            break;
        }
        case SEN_QUERY_ID:
        {
            result = B_OK;
            BString id;
            entry_ref ref;

            if ((result = message->FindString(SEN_ID_ATTR, &id)) == B_OK) {
                if ((result = relationHandler->QueryForUniqueSenId(id.String(), &ref)) == B_OK) {
                    reply->AddRef("ref", new entry_ref(ref.device, ref.directory, ref.name));
                }
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

                        char id[SEN_ID_LEN];
                        result = relationHandler->GetOrCreateId(&ref, id);
                        if (result != B_OK) {
                            break;
                        }

                        entry_ref existingEntry;

                        if ((result = relationHandler->QueryForUniqueSenId(id, &existingEntry)) == B_OK) {
                            BNode existingNode(&existingEntry);
                            if (existingNode == node) {
                                LOG("SEN:ID %s refers to same node %d, nothing to do.",
                                    id, node.Dup());
                                break;
                            }
                            // delete all SEN attributes of copy
                            LOG("found SEN:ID %s with exising node %s, removing attributes from copy...\n",
                                id, path.Path());

                            int32 attrCount = RemoveSenAttrs(&node);
                            if (attrCount >= 0) {
                                LOG("removed %d attribute(s) from file %s\n", attrCount, path.Path());
                            } else  {
                                ERROR("failed to remove attributes from node %s: %s\n", path.Path(), strerror(result));
                            }
                        } else {
                            LOG("ignoring possible move of %s, SEN:ID %s is still unique.\n",
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
        case SEN_CONFIG_GET:
        {
            senConfigHandler->GetConfig(reply);
            break;
        }
        case SEN_CONFIG_CLASS_ADD:
        case SEN_CONFIG_CLASS_GET: 
        case SEN_CONFIG_CLASS_FIND:	// fallthrough
        {
            senConfigHandler->MessageReceived(message);
            return; // done
        }
        // Relations - redirect to separate RelationsHndler
        case SEN_RELATIONS_GET:
        case SEN_RELATIONS_GET_ALL:
        case SEN_RELATIONS_GET_SELF:
        case SEN_RELATIONS_GET_ALL_SELF:
        case SEN_RELATIONS_GET_COMPATIBLE:
        case SEN_RELATIONS_GET_COMPATIBLE_TYPES:
		case SEN_RELATION_ADD:
		case SEN_RELATION_REMOVE:
		case SEN_RELATIONS_REMOVE_ALL: // fallthrough
        {
            relationHandler->MessageReceived(message);
            return; // done
        }
		default:
		{
            result = B_UNSUPPORTED;
            LOG("SEN Server: unknown message '%u' received." B_UTF8_ELLIPSIS "\n", message->what);
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
            ERROR("failed to get next attribute from file: %u, "
                  "possible SEN attributes left!\n", result);
            break;
        }
        if (BString(attrName).StartsWith(SEN_ATTR_PREFIX)) {
            LOG("checking SEN attribute %s...\n", attrName);
            result = node->RemoveAttr(attrName);
            if (result != B_OK) {
                ERROR("failed to remove SEN attribute %s: %s\n",
                        attrName, strerror(result));
                break;
            } else {
                LOG("removed SEN attribute %s\n", attrName);
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
