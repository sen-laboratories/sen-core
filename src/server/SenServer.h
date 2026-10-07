/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include "../config/SenConfigHandler.h"
#include "../relations/RelationHandler.h"

#include <Application.h>
#include <File.h>

class SenServer : public BApplication {

public:		SenServer();
virtual		~SenServer();

virtual	void ReadyToRun();
virtual	void MessageReceived(BMessage* message);

private:
    /**
     * @brief Remove the attributes that make a file an object of the SEN graph: its `SEN:ID`, the targets of its
     * relations (`SEN:TO*`, `SEN:META*`) and the relations themselves (`SEN:REL:*`). Used for copies, which are new objects.
     * Other SEN attributes (e.g. annotations) are about the content and stay.
     * @return the number of attributes removed, or an error code.
     */
    int32               RemoveIdentityAttrs(BNode* node);
    /** @brief Watch a volume for new entries (copies must not share a `SEN:ID`) and make sure it has the SEN indices. */
    void                WatchVolume(dev_t device);
    /** @brief Create the BFS indices that SEN queries need on a volume; existing ones are left alone. */
    void                EnsureIndices(dev_t device);

    RelationHandler*    relationHandler;
    SenConfigHandler*   senConfigHandler;
};
