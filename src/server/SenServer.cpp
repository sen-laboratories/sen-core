/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "../Sen.h"

#include <Entry.h>
#include <NodeMonitor.h>
#include <VolumeRoster.h>
#include <Volume.h>
#include <String.h>

using namespace std;

SenServer::SenServer() : BApplication(SEN_SERVER_SIGNATURE)
{
	// setup handlers
    relationsHandler = new RelationsHandler();
    senConfigHandler = new SenConfigHandler();

    AddHandler(relationsHandler);
    AddHandler(senConfigHandler);

    // setup handler chain
    SetNextHandler(relationsHandler);
    relationsHandler->SetNextHandler(senConfigHandler);

	// setup live query to keep SEN:ID unique when copying files
	// https://www.haiku-os.org/legacy-docs/bebook/BQuery_Overview.html#id611851
    BVolumeRoster volRoster;
	BVolume bootVolume;
	volRoster.GetBootVolume(&bootVolume);

	BString predicate(BString(SEN_ID_ATTR) << "==**");

    liveIdQuery = new BQuery();
	liveIdQuery->SetVolume(&bootVolume);
	liveIdQuery->SetPredicate(predicate.String());
    liveIdQuery->SetTarget(this);

    if (liveIdQuery->Fetch() != B_OK) {
        ERROR("failed to initialize live query for %s!\n", SEN_ID_ATTR);
    } else {
        LOG("set up query handler for %s\n", SEN_ID_ATTR);
    }
    nodeCache = new unordered_map<const char*, const entry_ref*>();
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
        case B_QUERY_UPDATE:
        {
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) == B_OK) {
                switch (opcode) {
                    case B_ENTRY_CREATED:
                    case B_ENTRY_REMOVED:   // handle these 2 together to check for move vs copy node
                    {
                        int32 statFields;
                        entry_ref ref;
                        BString name;

                        message->FindString("name", &name);
                        message->FindInt32("device", &ref.device);
                        message->FindInt64("directory", &ref.directory);

                        ref.set_name(name);
                        BNode node(&ref);

                        BString senId;
                        if (opcode == B_ENTRY_CREATED) {
                            DEBUG("node %s created.", name.String());
                        } else if (opcode == B_ENTRY_REMOVED) {
                            DEBUG("node %s removed.", name.String());
                        }
                        DEBUG("checking node %s for existing SEN attributes...\n", name.String());

                        node.ReadAttrString(SEN_ID_ATTR, &senId);
                        if (! senId.IsEmpty()) {
                            if (opcode == B_ENTRY_CREATED) {
                                DEBUG("Storing reference to node for senId %s for possible removal...\n", senId.String());
                                nodeCache->insert({senId.String(), &ref});
                            } else if (opcode == B_ENTRY_REMOVED) {
                                DEBUG("checking removed node %s for match with existing ID %s to identify copy vs. move...\n",
                                    name.String(), senId.String());
                                if (nodeCache->find(senId.String()) != nodeCache->end()) {
                                    // remove key
                                    nodeCache->erase(senId.String());
                                    // delete all SEN attributes of copy
                                    DEBUG("found senId %s with exising node, removing attributes (simulation)...", senId.String());
                                    /*
                                    char attrName[B_ATTR_NAME_LENGTH];
                                    while (node.GetNextAttrName(attrName) != B_ENTRY_NOT_FOUND) {
                                        if (BString(attrName).StartsWith(SEN_ATTRIBUTES_PREFIX)) {
                                            if (node.RemoveAttr(attrName) != B_OK) {
                                                ERROR("failed to remove SEN attribute %s from node %s\n",
                                                    attrName, name.String());
                                            } else {
                                                LOG("removed SEN attribute %s from node %s\n",
                                                    attrName, name.String());
                                            }
                                        }
                                    }}*/
                                }
                            }
                        }
                        break;
                    }
                }
                result = B_OK;
            }
            break;
        }
		default:
		{
            LOG("SEN Server: unknown message '%u', passing on to services in Handler chain" B_UTF8_ELLIPSIS "\n", message->what);
            BHandler::MessageReceived(message);
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
