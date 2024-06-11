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
// core functionality
#define SEN_QUERY_ID 			    'SCqi'

// Actions for communication with clients
#define SEN_ACTION_CMD		"SEN:action"

// relations
// used in file types
#define SEN_RELATION_SUPERTYPE "relation"
#define SEN_RELATION_SOURCE    "SEN:source"
#define SEN_RELATION_SOURCE_ID "SEN:sourceId"
#define SEN_RELATION_TARGET_ID "SEN:targetId"
// used for relations in messages
#define SEN_RELATIONS "SEN:relations"
// short name of the relation type
#define SEN_RELATION_NAME   "SEN:relationName"
// label used for a particular relation
#define SEN_RELATION_LABEL  "SEN:relationLabel"
// unique relation MIME type
#define SEN_RELATION_TYPE   "SEN:relationType"
#define SEN_RELATION_TARGET "SEN:target"
#define SEN_RELATION_PROPERTIES "SEN:relationProps"

// messages
#define SEN_RELATIONS_GET           'SRgo'
#define SEN_RELATIONS_GET_ALL       'SRga'
#define SEN_RELATION_ADD	        'SRad'
#define SEN_RELATION_REMOVE	    	'SRrm'
#define SEN_RELATIONS_REMOVE_ALL    'SRra'

// Tracker integration
#define SEN_OPEN_RELATION_VIEW		    'STor'
#define SEN_OPEN_RELATION_TARGET_VIEW	'STot'

// todo: set to 13 for TSID later
#define SEN_ID_LEN					32

// used on any linked file
#define SEN_ATTR_PREFIX     		"SEN:"
#define SEN_ID_ATTR        			SEN_ATTR_PREFIX "ID"
#define SEN_TO_ATTR        			SEN_ATTR_PREFIX "TO"
#define SEN_META_ATTR               SEN_ATTR_PREFIX "META"
#define SEN_RELATION_ATTR_PREFIX    SEN_ATTR_PREFIX "REL:"
// used only for ad-hoc created relation files pointing to the target
#define SEN_RELATION_SOURCE_ATTR    SEN_RELATION_ATTR_PREFIX "ID"
#define SEN_RELATION_TARGET_ATTR    SEN_RELATION_ATTR_PREFIX "TO"

// Message Replies
#define SEN_RESULT_INFO				'SCri'
#define SEN_RESULT_STATUS			'SCrs'
#define SEN_RESULT_RELATIONS		'SCre'

#endif // _SEN_DEFS_H
