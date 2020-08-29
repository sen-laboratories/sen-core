/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SemanticServer.h"
#include "../SenDefs.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <new>

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
	BMessage reply;
	status_t result = B_UNSUPPORTED;

	switch (message->what) {
		case SEN_CORE_INFO:
		 {
		 	// TODO: get from resource
		 	result = B_OK;
		 	reply.what = SEN_RESULT_INFO;
		 	reply.AddString("info", "SEN Core v0.0.0-proto1");
			
		 	break;
		 }
		case SEN_CORE_STATUS:
		 {
		 	result = B_OK;
		 	reply.what = SEN_RESULT_STATUS;
		 	
		 	reply.AddString("status", "operational");
		 	reply.AddBool("healthy", true);
		 	
		 	break;
		 }
		case SEN_RELATIONS_GET:
		{
			result = B_OK;
			reply.what = SEN_RESULT_RELATIONS;
			
			BString source;
			if (message->FindString("source", &source) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}
			BString relation;
			if (message->FindString("relation", &relation) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}
			//TODO: if file found
			if (result == B_OK) {
				//TODO: add relations to reply
				reply.AddString("relations", "foo,bar,baz");
				break;
			}
		}
		default:
		{
			printf("unknown message received: %" B_PRIu32 " \"%.4s\"\n",
				message->what, (const char*)&message->what);
			break;
		}
	}

	reply.AddInt32("result", result);
	message->SendReply(&reply);
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
