/*
 * Copyright 2023, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SEN_CONFIG_HANDLER_H
#define _SEN_CONFIG_HANDLER_H

#include <Application.h>

#define SEN_CONFIG_RELATION_SUPERTYPE_NAME     "relation"
#define SEN_CONFIG_RELATION_MSG                'SCrc'
#define SEN_CONFIG_RELATION_PROPERTIES_MSG     'SCrp'
#define SEN_CONFIG_RELATION_MSG_SOURCE_TYPES   "sourceMimeTypes"
#define SEN_CONFIG_RELATION_MSG_TARGET_TYPES   "targetMimeTypes"

class SenConfigHandler : public BHandler {

public:
                SenConfigHandler();
virtual         ~SenConfigHandler();

virtual	void	MessageReceived(BMessage* message);

private:
			status_t			InitConfig(bool clean=false);
            void                AddMimeInfo(BMessage* attrMsg, const char* name, const char* displayName, int32 type, bool viewable, bool editable);
			status_t			InitIndices(bool clean);
			status_t			InitRelationTypes(bool clean);
			status_t			InitRelations(bool clean);
            
            BDirectory*         settingsDir;
            BDirectory*         relationsDir;
            
            status_t            CreateRelation(const char* name, const char* displayName, const char* inverseOf, const char* childOf, const char* description,
                                               bool clean, bool enabled, const BMessage* config, const BMessage* configProps);
            
            void                InitPropertiesMsg(BMessage *propMsg);
};

#endif // _SEN_CONFIG_HANDLER_H
