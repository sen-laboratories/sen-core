/**
 * SEN Semantic Server
 *
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SEN_SERVER_H
#define _SEN_SERVER_H

#include "../config/SenConfigHandler.h"
#include "../relations/RelationHandler.h"

#include <Application.h>
#include <File.h>

class SenServer : public BApplication {

public:		SenServer();
virtual		~SenServer();

virtual	void ReadyToRun();
virtual	void MessageReceived(BMessage* message);

private:
    int32               RemoveSenAttrs(BNode* node);

    RelationHandler*    relationHandler;
    SenConfigHandler*   senConfigHandler;
};

#endif // _SEMANTIC_SERVER_H
