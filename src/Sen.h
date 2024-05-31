/**
 * @author Gregor Rosenauer <gregor.rosenauer@gmail.com>
 * All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _SEN_H
#define _SEN_H

#include <stdio.h>

#define SEN_SERVER_SIGNATURE "application/x-vnd.sen-labs.sen-server"

// simple logging, todo: integrate simple but more standard logging
#define DEBUG(x...)		printf(x);
#define LOG(x...)		printf(x);
#define ERROR(x...)		fprintf(stderr, x);

// core
#define SEN_CORE_INFO 				'SCin'
#define SEN_CORE_STATUS 			'SCst'
// todo: obsolete
#define SEN_CORE_INSTALL			'SCis'
// validate and repair config
#define SEN_CORE_CHECK				'SCck'

// relations
#define SEN_RELATION_SOURCE "SEN:source"
#define SEN_RELATION_NAME   "SEN:relation"
#define SEN_RELATION_TARGET "SEN:target"

#define SEN_RELATIONS_GET           'SCrg'
#define SEN_RELATIONS_GET_ALL       'SCrl'
#define SEN_RELATION_ADD	        'SCra'
#define SEN_RELATION_REMOVE	    	'SCrr'
#define SEN_RELATIONS_REMOVE_ALL    'SCrd'

#define SEN_ATTRIBUTES_PREFIX		"SEN:"
// used on any linked file
#define SEN_ID_ATTR        			SEN_ATTRIBUTES_PREFIX "ID"
#define SEN_TO_ATTR        			SEN_ATTRIBUTES_PREFIX "TO"
#define SEN_META_ATTR               SEN_ATTRIBUTES_PREFIX "META"
#define SEN_RELATION_ATTR_PREFIX    SEN_ATTRIBUTES_PREFIX "REL:"
// used only for ad-hoc created relation files pointing to the target
#define SEN_RELATION_SRC_ATTR       SEN_RELATION_ATTR "ID"
#define SEN_RELATION_TARGET_ATTR    SEN_RELATION_ATTR "TO"

// Message Replies
#define SEN_RESULT_INFO				'SCri'
#define SEN_RESULT_STATUS			'SCrs'
#define SEN_RESULT_RELATIONS		'SCre'

#endif // _SEN_DEFS_H
