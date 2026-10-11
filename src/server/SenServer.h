/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include "../config/SenConfigHandler.h"
#include "../relations/RelationHandler.h"
#include "../ui/SenUi.h"

#include <Application.h>
#include <MessageRunner.h>
#include <File.h>
#include <Node.h>

#include <vector>

class SenServer : public BApplication {

public:		SenServer();
virtual		~SenServer();

virtual	void ReadyToRun();
virtual	void MessageReceived(BMessage* message);
virtual	bool QuitRequested();

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

    /** @brief Put the item of SEN in the Deskbar (the knot with the menu), like the mail server; and take it out again when the server quits. */
    void                InstallDeskbarItem();
    void                RemoveDeskbarItem();

    /**
     * @brief Have the installer sen_oni make the ontologies in the MIME database match the ontology packages: soon (a package
     * is activated some time after its file arrived: the update is repeated @a count times). Runs one update at a time, in its own thread.
     */
    void                ScheduleOntologyUpdate(bigtime_t delay, int32 count);
    void                StartOntologyUpdate();
    bool                IsPackagesDir(dev_t device, ino_t directory) const;

    RelationHandler*    relationHandler;
    SenConfigHandler*   senConfigHandler;
    sen::ui::SenUi*     fUi;
    /** @brief While the Deskbar is not there yet (at the start of a session): tries again now and then, to add the item of SEN. */
    BMessageRunner*     fDeskbarRetry;
    int32               fDeskbarTries;

    /** @brief The folders of the packages (system and user): new and removed packages can bring or take ontologies. */
    std::vector<node_ref> fPackagesDirs;
    BMessageRunner*     fOntologyTimer;
    bool                fOntologyUpdating;
    /** @brief Packages changed while an update was running: another one follows. */
    bool                fOntologyPending;
};
