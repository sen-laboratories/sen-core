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
            DEBUG("query_update received.\n");
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

                        DEBUG("ENTRY_CREATED: node %s...\n", name.String());

                        BString senId;
                        node.ReadAttrString(SEN_ID_ATTR, &senId);
                        if (! senId.IsEmpty()) {
                            // watch new node for attribute changes to trigger enrichment
                            // todo: handle this via message sending to Enricher,not here
                            DEBUG("adding node attribute watcher for %s.\n", name.String());
                            node_ref nref;
                            node.GetNodeRef(&nref);
                            watch_node(&nref, B_WATCH_ATTR, this);

                            DEBUG("checking node %s for match with existing ID %s...\n",
                                name.String(), senId.String());

                            BEntry existingEntry;
                            if (relationsHandler->QueryForId(senId, &existingEntry) > 1) {
                                BPath path;
                                existingEntry.GetPath(&path);
                                // delete all SEN attributes of copy
                                DEBUG("found senId %s with exising node %s, removing attributes from copy...",
                                    senId.String(), path.Path());

                                char attrName[B_ATTR_NAME_LENGTH];
                                int attrCount = 0;
                                while (node.GetNextAttrName(attrName) != B_ENTRY_NOT_FOUND) {
                                    if (BString(attrName).StartsWith(SEN_ATTRIBUTES_PREFIX)) {
                                        if (node.RemoveAttr(attrName) != B_OK) {
                                            ERROR("failed to remove SEN attribute %s from node %s\n",
                                                attrName, name.String());
                                        } else {
                                            DEBUG("removed SEN attribute %s from node %s\n", attrName, name.String());
                                            attrCount++;
                                        }
                                    }
                                }
                                DEBUG("removed %d attributes from node %s\n", attrCount, path.Path());
                            } else {
                                DEBUG("ignoring possible move of %s, SEN:ID %s is still unique.",
                                    name.String(), senId.String());
                            }
                        }
                        break;
                    }
                    case B_ENTRY_REMOVED:
                    {
                        entry_ref ref;
                        BString name;

                        message->FindString("name", &name);
                        message->FindInt32("device", &ref.device);
                        message->FindInt64("directory", &ref.directory);

                        ref.set_name(name);
                        BNode node(&ref);

                        DEBUG("ENTRY_REMOVED: node %s...\n", name.String());

                        BString senId;
                        node.ReadAttrString(SEN_ID_ATTR, &senId);
                        if (! senId.IsEmpty()) {
                            DEBUG("checking if deleted node %s with ID %s is still referenced...\n",
                                name.String(), senId.String());

                            // get all SEN:REL attributes and check for orphaned relations!
                            int attrCount = 0;
                            char attrName[B_ATTR_NAME_LENGTH];

                            while (node.GetNextAttrName(attrName) != B_ENTRY_NOT_FOUND) {
                                BString attribute(attrName);
                                if (attribute.StartsWith(SEN_ATTRIBUTES_PREFIX)) {
                                    if (attribute.StartsWith(SEN_RELATION_ATTR_PREFIX)) {
                                        // todo: get targets and remove this nodes' ID from targets
                                        int relation_prefix_len = BString(SEN_RELATION_ATTR_PREFIX).Length();
                                        BString relation(attribute.Remove(0, relation_prefix_len));
                                        DEBUG("removing relation %s\n", relation.String());
                                    }
                                    attrCount++;
                                }
                            }
                            if (attrCount > 0) {
                                DEBUG("deleted %d SEN attribute(s) from node %s.\n", attrCount, name.String());
                            } else {
                                DEBUG("node %s with ID %s had no relations, deleted.\n",
                                    name.String(), senId.String());
                            }
                        }
                        break;
                    }
                }
                result = B_OK;
            }
            break;
        }
        case B_NODE_MONITOR:
        {
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) == B_OK) {
                entry_ref ref;
                BString name;
                BString attrName;
                int32   cause;      // todo: look up enum

                message->FindString("name", &name);
                message->FindString("attr", &attrName);
                message->FindInt32("cause", &cause);
                message->FindInt32("device", &ref.device);
                message->FindInt64("directory", &ref.directory);

                ref.set_name(name);
                BNode node(&ref);

                switch (opcode) {
                    case B_ATTR_CREATED:      // handle both the same way
                    case B_ATTR_CHANGED:
                    {

                        BString attrNameCause(attrName << " with cause " << cause
                            << (opcode == B_ATTR_CREATED ? " created" : " changed"));
                        DEBUG("attribute %s for node %s\n", attrName.String(), name.String());
                        break;
                    }
                    default:
                    {
                        DEBUG("ignoring node monitor message for node %s with opcode %d\n", name.String(), opcode);
                    }
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
