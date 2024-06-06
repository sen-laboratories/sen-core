/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "../Sen.h"
#include "../relations/RelationsHandler.h"

#include <AppFileInfo.h>
#include <Entry.h>
#include <MimeType.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <VolumeRoster.h>
#include <Volume.h>
#include <String.h>

SenServer::SenServer() : BApplication(SEN_SERVER_SIGNATURE)
{
	// setup feature-specific handlers for redirecting messages appropriately
    relationsHandler = new RelationsHandler();
    Looper()->AddHandler(relationsHandler);
    senConfigHandler = new SenConfigHandler();
    Looper()->AddHandler(senConfigHandler);

	// see also https://www.haiku-os.org/legacy-docs/bebook/BQuery_Overview.html#id611851
    BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);
    // watch for move (rename) and copy operations to ensure our SEN ID stays unique.
    watch_volume(bootVolume.Device(), B_WATCH_NAME, this);
}

SenServer::~SenServer()
{
    DEBUG("Goodbye:)\n");
    stop_watching(this);
}

void SenServer::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;

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
        case B_NODE_MONITOR:
        {
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

                        const char *id = relationsHandler->GetOrCreateId(path.Path());
                        if (id == NULL) {
                            break;
                        }

                        BString senId(id);
                        BEntry existingEntry;

                        if (relationsHandler->QueryForId(senId, &existingEntry) == B_OK) {
                            BNode existingNode(&existingEntry);
                            if (existingNode == node) {
                                DEBUG("SEN:ID %s refers to same node %d, nothing to do.",
                                    senId.String(), node.Dup());
                                break;
                            }
                            // delete all SEN attributes of copy
                            DEBUG("found SEN:ID %s with exising node %s, removing attributes from copy...\n",
                                senId.String(), path.Path());

                            char attrName[B_ATTR_NAME_LENGTH];
                            int attrCount = 0;

                            while (status_t result = node.GetNextAttrName(attrName) >= 0) {
                                if (result < 0) {
                                    ERROR("failed to get next attribute from file %s: %u, "
                                            "possible SEN attributes left!\n", name.String(), result);
                                    break;
                                }
                                if (BString(attrName).StartsWith(SEN_ATTRIBUTES_PREFIX)) {
                                    DEBUG("checking SEN attribute %s of node %s\n", attrName, name.String());
                                    if (node.RemoveAttr(attrName) != B_OK) {
                                        ERROR("failed to remove SEN attribute %s from node %s\n",
                                            attrName, name.String());
                                    } else {
                                        DEBUG("removed SEN attribute %s from node %s\n", attrName, name.String());
                                        attrCount++;
                                    }
                                }
                            }
                            DEBUG("removed %d attribute(s) from node %s\n", attrCount, path.Path());
                        } else {
                            DEBUG("ignoring possible move of %s, SEN:ID %s is still unique.\n",
                                name.String(), senId.String());
                        }
                        break;
                    }
                    break;
                }
            }
            break;
        }

        // Relations - fallthrough: handle all in separate handler
        case SEN_RELATIONS_GET:
        case SEN_RELATIONS_GET_ALL:
		case SEN_RELATION_ADD:
		case SEN_RELATION_REMOVE:
		case SEN_RELATIONS_REMOVE_ALL:
        {
            if (PostMessage(message, relationsHandler) != B_OK) {
                ERROR("failed to forward message %u to RelationHandler!\n", message->what);
            }
            return;
        }
		default:
		{
            LOG("SEN Server: unknown message '%u' received." B_UTF8_ELLIPSIS "\n", message->what);
		}
	}
	LOG("SEN server sending result %d\n", result);
	reply->AddInt32("result", result);
	message->SendReply(reply);
}

int main(int argc, char* argv[])
{
	SenServer* app = new(std::nothrow) SenServer();
	if (app == NULL)
		return 1;

	app->Run();
	delete app;
	return 0;
}
