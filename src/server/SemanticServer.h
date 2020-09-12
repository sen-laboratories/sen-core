/**
 * SEN Semantic Server
 * 
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SEMANTIC_SERVER_H
#define _SEMANTIC_SERVER_H

#include <Application.h>
#include <File.h>

class SemanticServer : public BApplication {
public:
									SemanticServer();
virtual								~SemanticServer();

virtual	void						MessageReceived(BMessage* message);

};


#endif // _SEMANTIC_SERVER_H
