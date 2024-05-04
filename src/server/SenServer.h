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
#include "../relations/RelationsHandler.h"

#include <Application.h>
#include <File.h>
#include <unordered_map>

using namespace std;

class SenServer : public BApplication {

public:		SenServer();
virtual		~SenServer();

virtual	void MessageReceived(BMessage* message);

private:
            RelationsHandler*       relationsHandler;
            SenConfigHandler*       senConfigHandler;

            BQuery*                 liveIdQuery;
            unordered_map<const char*, const entry_ref*> *nodeCache;
};


#endif // _SEMANTIC_SERVER_H
