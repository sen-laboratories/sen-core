/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SEN_CONFIG_HANDLER_H
#define _SEN_CONFIG_HANDLER_H

#include <Application.h>

#define SEN_CONFIG_RELATION_TYPE_NAME "application/x-vnd.sen-relation"

class SenConfigHandler : public BHandler {

public:
                SenConfigHandler();
virtual         ~SenConfigHandler();

virtual	void	MessageReceived(BMessage* message);

private:
			status_t			InitConfig();
            
			status_t			InitIndices();
			status_t			InitRelationType();
			status_t			InitRelations();
            
            BDirectory*         settingsDir;
};

#endif // _SEN_CONFIG_HANDLER_H
