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

private:
		/*status_t					_ValidateRelations(boolean repair);

		status_t					_FindRelations(const BFile& file);

		status_t					_AddRelation(const BFile& sourceFile, const BString& relationAttrName, const BFile& targetFile);
		status_t					_RemoveRelation(const BFile& file, const BString& relationAttrName);*/
};


#endif // _SEMANTIC_SERVER_H
