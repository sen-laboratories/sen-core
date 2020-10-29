/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "../Sen.h"

#include <Entry.h>
#include <Roster.h>
#include <String.h>

SenServer::SenServer()
	:
	BApplication(SEN_SERVER_SIGNATURE)
{
	/*
     * setup handlers
     */
    relationsHandler = new RelationsHandler();
    senConfigHandler = new SenConfigHandler();
    
    AddHandler(relationsHandler);
    AddHandler(senConfigHandler);
    
    // setup handler chain
    SetNextHandler(relationsHandler);
    relationsHandler->SetNextHandler(senConfigHandler);
    
    LOG("SEN server: registered %d service handlers.\n", CountHandlers());
    
	// setup node watcher for volume to keep relations up to date on creation/deletion
	// https://www.haiku-os.org/docs/api/NodeMonitor_8h.html#a24336df118e76f00bd15b89fa863d299
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
		 	reply->AddString("info", "SEN Core v0.0.0-proto1");
			
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
		default:
		{
            LOG("SEN Server: unknown message '%lu', passing on to services in Handler chain" B_UTF8_ELLIPSIS "\n", message->what);
            BHandler::MessageReceived(message);
            return;
		}
	}

	LOG("SEN server sending result %d", result);
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
