/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <SupportDefs.h>

#include <functional>
#include <vector>

/**
 * @file Migrations.h
 * @brief Changes to the data of the users that a new version of SEN needs (renamed relation types, ...), as numbered steps.
 *
 * What SEN keeps in the attributes of the files of a user outlives the programs that wrote it: when a version renames a relation type or a type of
 * file, the files of the people who had it stay as they are. A migration is a step that makes them what the new version expects. The steps are
 * numbered and kept in the order in which they were made (a number is never used again), the number of the last step that was done is the data
 * version, which is kept in the settings of SEN (SEN:dataVersion). The server runs the steps that are still to do when it starts. A step is
 *
 *  - idempotent: what it has changed it does not change again, so a step that was interrupted is simply run again;
 *  - safe: it changes only what an older version of SEN made (a relation type that is gone, say); it never changes anything that other programs
 *    keep (the attributes of People, BePDF...) and asks the user first for anything that would replace data that the user may have made (that is
 *    for a step of its own, with its own question: none of these steps asks, and none needs to);
 *  - complete or failed: it counts what it changed and what it could not change; a step with failures is not recorded as done, it is tried again
 *    at the next start and stops the ones after it.
 *
 * `sen_server --migrate` shows and runs the steps without starting the server (`--dry-run`: only counts). Packages cannot do this work: a package
 * script is run once for the system, not for each user, and cannot know where the files of a user are.
 */
namespace sen {
namespace migration {

/** the setting that holds the number of the last step that was done */
inline constexpr char kDataVersionKey[] = "SEN:dataVersion";

/** what a step did (or, in a dry run, would do) */
struct Counts {
	int32	changed = 0;
	int32	failed = 0;
};

/** a relation type that has another name now: the types are named as MIME types ("relation/x-vnd.sen-labs.relation.person") */
struct Rename {
	const char*	from;
	const char*	to;
};

/**
 * The relations of the old types are moved to the new types in all SEN objects (files with a SEN:ID) of all volumes. Relations that are stored under the
 * new type already are kept, those of the old type are added to them (a target that is there is not added a second time).
 * @param dryRun count what would be changed, change nothing
 */
Counts RenameRelationTypes(const std::vector<Rename>& renames, bool dryRun);

/** one step: its number, a short name, what it does in a line, and the work */
struct Step {
	int32		version;
	const char*	name;
	const char*	what;
	Counts		(*run)(bool dryRun);
};

/** the steps in the order in which they are run */
const std::vector<Step>& Steps();

/** where the data version is kept: the server uses its settings, the command line the file of the settings */
struct VersionStore {
	std::function<int32()>		read;	// 0 if none
	std::function<status_t(int32)>	write;
};

/** the settings file of the user (sen/sen.settings) read and written directly, for the command line when the server is not running */
VersionStore SettingsFileStore();

/** what happened to a step */
struct Report {
	const Step*	step;
	Counts		counts;
	bool		wasDone;	// done by an earlier run (nothing was done now)
	bool		ran;		// run now (in a dry run: counted)
	bool		recorded;	// done, and recorded as done
};

/**
 * Runs the steps that are not done yet, in order. A step with failures stops the run (the later steps may need it). The version is recorded after each
 * step that is complete, not in a dry run.
 * @return a report for each step
 */
std::vector<Report> Run(const VersionStore& store, bool dryRun);

}	// namespace migration
}	// namespace sen
