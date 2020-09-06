/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
 
#ifndef _SEN_DEFS_H
#define _SEN_DEFS_H

#define SEN_SERVER_SIGNATURE "application/x-vnd.crashandburn.sen-semantic_server"

// to allow easy declaration AND reuse across sources
enum {
		//
		// Requests
		//

		// core	
		SEN_CORE_INFO 				= 'SCin',
		SEN_CORE_STATUS 			= 'SCst',
		SEN_CORE_CHECK				= 'SCck',		// validate and repair relation attributes
		
		// relations
		SEN_RELATIONS_GET			= 'SCrg',
		SEN_RELATIONS_GET_TARGETS	= 'SCrt',
		SEN_RELATIONS_ADD			= 'SCra',
		SEN_RELATIONS_REMOVE		= 'SCrr',
		// e.g. on deleting a file
		SEN_RELATIONS_REMOVEALL		= 'SCrp',		// for Purge

		// Message Replies
		SEN_RESULT_INFO				= 'SCri',
		SEN_RESULT_STATUS			= 'SCrs'
};

#endif // _SEN_DEFS_H
