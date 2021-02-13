/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SEN_CONFIG_HANDLER_H
#define _SEN_CONFIG_HANDLER_H

#include <Application.h>

#define SEN_CONFIG_RELATION_TYPE_NAME   "application/x-vnd.sen-labs.sen-relation"
#define SEN_CONFIG_MSG                  'SCrc'
#define SEN_CONFIG_MSG_DISPLAY_NAME     "displayName"
#define SEN_CONFIG_MSG_SOURCE_TYPES         "sourceMimeTypes"
#define SEN_CONFIG_MSG_TARGET_TYPES         "targetMimeTypes"

class SenConfigHandler : public BHandler {

public:
                SenConfigHandler();
virtual         ~SenConfigHandler();

virtual	void	MessageReceived(BMessage* message);

private:
			status_t			InitConfig(bool clean=false);
            
			status_t			InitIndices(bool clean);
			status_t			InitRelationType(bool clean);
			status_t			InitRelations(bool clean);
            
            BDirectory*         settingsDir;
            
            status_t            CreateRelation(const char* name, const char* displayName, bool clean, bool enabled, bool abstract, 
                                               const char* inverseOf, const char* childOf, const char* formula,
                                               const BMessage* config);
};

#endif // _SEN_CONFIG_HANDLER_H
