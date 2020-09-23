/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SenServer.h"
#include "../relations/RelationService.h"
#include "../Sen.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <stdio.h>

SemanticServer::SemanticServer()
	:
	BApplication(SEN_SERVER_SIGNATURE)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory settingsDir(path.Path());
	path.Append("sen");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("server");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("relations");

	//_ReadRelationsConfig();
	 
	// setup node watcher for volume to keep relations up to date on creation/deletion
	// https://www.haiku-os.org/docs/api/NodeMonitor_8h.html#a24336df118e76f00bd15b89fa863d299
}

SemanticServer::~SemanticServer()
{
}

void SemanticServer::MessageReceived(BMessage* message)
{
	BMessage* reply = new BMessage();
	status_t result = B_UNSUPPORTED;
	RelationService relationService;

	switch (message->what) {
		case SEN_CORE_INFO:
		 {
		 	// TODO: get from resource
		 	result = B_OK;
		 	reply->what = SEN_RESULT_INFO;
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
		case SEN_RELATIONS_ADD:
		{
			result = relationService.AddRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_GET:
		{
			result = relationService.GetRelations(message, reply);
			break;
		}
		case SEN_RELATIONS_GET_TARGETS:
		{
			result = relationService.GetTargetsForRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_REMOVE:
		{
			result = relationService.RemoveRelation(message, reply);
			break;
		}
		case SEN_RELATIONS_REMOVEALL:
		{
			result = relationService.RemoveAllRelations(message, reply);
			break;
		}
		default:
		{
			printf("unknown message received: %" B_PRIu32 " \"%.4s\"\n",
				message->what, (const char*)&message->what);
			break;
		}
	}

	reply->AddInt32("result", result);
	message->SendReply(reply);
}

int main(int argc, char* argv[])
{
	SemanticServer* app = new(std::nothrow) SemanticServer();
	if (app == NULL)
		return 1;

	app->Run();
	delete app;
	return 0;
}
