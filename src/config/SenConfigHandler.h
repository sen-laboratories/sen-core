/*
 * Copyright 2023, Gregor B. Rosenauer <gregor.rosenauer@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _SEN_CONFIG_HANDLER_H
#define _SEN_CONFIG_HANDLER_H

#include <Application.h>

class SenConfigHandler : public BHandler {

public:
                SenConfigHandler();
virtual         ~SenConfigHandler();

virtual	void	MessageReceived(BMessage* message);

private:
            BDirectory*         settingsDir;
};

#endif // _SEN_CONFIG_HANDLER_H
