/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SEN_DEFS_H
#define _SEN_DEFS_H

 const char* SEN_SERVER_SIGNATURE
        = "application/x-vnd.crashandburn.sen-semantic_server";

enum {
		//
		// Requests
		//

		// core	
		SEN_CORE_INFO 						= 'SCvs',
		SEN_CORE_STATUS 					= 'SCst',
		
		// relations
		SEN_RELATIONS_GET				= 'SCrg',
		SEN_RELATIONS_GETALL			= 'SCra',
		SEN_RELATIONS_NEW				= 'SCrn',
		SEN_RELATIONS_REMOVE			= 'SCrr',
		// e.g. on deleting a file
		SEN_RELATIONS_REMOVEALL	= 'SCrp',		// for Purge
		
		// Replies
		SEN_RESULT_INFO				= 'SCri',
		SEN_RESULT_STATUS			= 'SCrs',
		SEN_RESULT_RELATIONS		= 'SCre'
};

#endif // _SEN_DEFS_H
