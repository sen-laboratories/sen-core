/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#include "SenConfigHandler.h"
#include "Sen.h"

#include <Directory.h>
#include <FindDirectory.h>
#include <Mime.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Resources.h>

SenConfigHandler::SenConfigHandler()
    : BHandler("SenConfigHandler")
{
    BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
    {
        ERROR("could not find user settings directory, falling back to working directory.\n");
		path.SetTo("./config");
    }
	settingsDir = new BDirectory(path.Path());
	path.Append("sen");
	if (!settingsDir->Contains(path.Path()))
		settingsDir->CreateDirectory(path.Path(), NULL);

	settingsDir->SetTo(path.Path());

    LOG("SEN Config: using settings directory %s\n", path.Path());
}

SenConfigHandler::~SenConfigHandler()
{
}

void SenConfigHandler::MessageReceived(BMessage* message)
{
    BMessage* reply = new BMessage();
	status_t result = B_OK;

    LOG("in SEN ConfigHandler::MessageReceived\n");

    switch(message->what)
    {
        default:
        {
            LOG("SenConfigHandler: currently only left a stub\n");
        }
    }
    LOG("SEN ConfigHandler sending reply %d\n", result);
    reply->AddInt32("result", result);
	message->SendReply(reply);
}
