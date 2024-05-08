/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "../Sen.h"

#include <Entry.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <VolumeRoster.h>
#include <Volume.h>
#include <String.h>

using namespace std;

SenServer::SenServer() : BApplication(SEN_SERVER_SIGNATURE)
{
	// setup handlers
    relationsHandler = new RelationsHandler();
    senConfigHandler = new SenConfigHandler();

	// see also https://www.haiku-os.org/legacy-docs/bebook/BQuery_Overview.html#id611851
    BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);
    watch_volume(bootVolume.Device(), B_WATCH_NAME | B_WATCH_STAT | B_WATCH_ATTR, this);

    nodesExcludedFromWatch = new unordered_map<const char*, const entry_ref*>();
}

SenServer::~SenServer()
{
}

void SenServer::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;

    LOG("in SEN Server::MessageReceived\n");

	switch (message->what) {
		case SEN_CORE_INFO:
		{
		 	result = B_OK;
		 	reply->what = SEN_RESULT_INFO;
		 	// TODO: get info from resource
		 	reply->AddString("info", "SEN Core v0.2.0-proto2");

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
                    case B_ENTRY_CREATED:
                    {
                        entry_ref ref;
                        BString name;

                        message->FindString("name", &name);
                        message->FindInt32("device", &ref.device);
                        message->FindInt64("directory", &ref.directory);
                        ref.set_name(name);

                        BNode node(&ref);
                        BPath path(&ref);

                        DEBUG("NodeMonitor::B_ENTRY_CREATED: %s\n", path.Path());
                        const char *id = relationsHandler->GetOrCreateId(path.Path());
                        if (id == NULL) {
                            break;
                        }

                        BString senId(id);
                        BEntry existingEntry;
                        if (relationsHandler->QueryForId(senId, &existingEntry) == 1) {
                            BNode existingNode(&existingEntry);
                            if (existingNode == node) {
                                DEBUG("senId %s refers to same node %d, nothing to do.",
                                    senId.String(), node.Dup());
                                break;
                            }
                            // exclude node from monitoring so it doesn't trigger ATTR_REMOVED handling
                            nodesExcludedFromWatch->insert({senId.String(), &ref});

                            // delete all SEN attributes of copy
                            DEBUG("found senId %s with exising node %s, removing attributes from copy...\n",
                                senId.String(), path.Path());

                            char attrName[B_ATTR_NAME_LENGTH];
                            int attrCount = 0;

                            while (status_t result = node.GetNextAttrName(attrName) >= 0) {
                                if (result < 0) {
                                    ERROR("failed to get next attribute from file %s: %u, possible SEN attributes left!\n", name.String(), result);
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

                            // remove node from excluded nodes to free up space
                            nodesExcludedFromWatch->erase(senId.String());
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
		default:
		{
            LOG("SEN Server: unknown message '%u' received." B_UTF8_ELLIPSIS "\n", message->what);
            return;
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
