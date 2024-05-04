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
                        node.ReadAttrString(SEN_ID_ATTR, &senId);
                        if (! senId.IsEmpty()) {
                            DEBUG("checking node %s for match with existing ID %s...\n",
                                name.String(), senId.String());

                            BEntry existingEntry;
                            if (relationsHandler->QueryForId(senId, &existingEntry)) {
                                BPath path;
                                existingEntry.GetPath(&path);
                                // delete all SEN attributes of copy
                                DEBUG("found senId %s with exising node %s, removing attributes from copy...",
                                    senId.String(), path.Path());

                                char attrName[B_ATTR_NAME_LENGTH];
                                while (node.GetNextAttrName(attrName) != B_ENTRY_NOT_FOUND) {
                                    if (BString(attrName).StartsWith(SEN_ATTRIBUTES_PREFIX)) {
                                        if (node.RemoveAttr(attrName) != B_OK) {
                                            ERROR("failed to remove SEN attribute %s from node %s\n",
                                                attrName, name.String());
                                        } else {
                                            DEBUG("removed SEN attribute %s from node %s\n", attrName, name.String());
                                        }
                                    }
                                }
                            } else {
                                DEBUG("ignoring possible move of %s, SEN:ID %s is still unique.",
                                    name.String(), senId.String());
                            }
                        }
                        break;
                    }
                    case B_ENTRY_REMOVED:
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
                        node.ReadAttrString(SEN_ID_ATTR, &senId);
                        if (! senId.IsEmpty()) {
                            DEBUG("checking if deleted node %s with ID %s is still referenced...\n",
                                name.String(), senId.String());

                            // TODO: we might need SEN:TO with cached targetIds after all
                        } else {
                            DEBUG("all good, node %s with ID %s can be deleted.",
                                name.String(), senId.String());
                        }
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
