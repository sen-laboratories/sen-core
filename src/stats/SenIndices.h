/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

/**
 * @file SenIndices.h
 * @brief The BFS indices of SEN: finding out whether they are in order, and making them again when they are not.
 *
 * SEN finds its objects and relations only through queries: `SEN:ID`, `SEN:TO` and `SEN:META` (and the chunks `SEN:TO:1`..., `SEN:META:1`...) are
 * indices, and an index that is damaged makes files invisible without any error. It happens after a crash or a forced stop of the machine: BFS keeps
 * an entry of a file that is gone ("inode ... is already deleted", "IndexIteratorGetEntry: Bad data" in the syslog), the query stops at that entry
 * and everything behind it is lost, or an attribute that was written is not in the index at all.
 *
 * Both kinds are found by comparing an index with the files: the SEN objects (the files with a `SEN:ID`, which the index of `SEN:ID` finds) that carry
 * the attribute must all be found by the query for it, and nothing else may be found. The repair is what the shell commands do by hand: remove the
 * index (`rmindex`), create it again (`mkindex`) and write the attribute of every file again so that BFS puts it into the index (`reindex`). It uses
 * the API of Haiku for all of it (fs_remove_index, fs_create_index, BNode attributes). Shared by the check (SenStats.h), the windows of the Deskbar
 * menu and the command message sen::cmd::kRepairIndices (which the script repair-sen-indices.sh sends).
 */

#include "SenStats.h"

#include <String.h>
#include <SupportDefs.h>

#include <functional>
#include <vector>

namespace sen {
namespace stats {

/** What the comparison of one index of SEN with the files of a volume found. */
struct IndexState {
	dev_t		device;
	BString		volume;
	BString		attribute;		///< e.g. "SEN:TO"
	uint32		type;			///< the type of the index (B_STRING_TYPE for all of SEN's)
	bool		compared;		///< false: not compared with the files (not a string index, no SEN:ID index to find the objects); only the query errors count
	int32		expected;		///< SEN objects that carry the attribute
	int32		found;			///< files that the query for the attribute finds
	int32		missing;		///< carry the attribute, but the query does not find them
	int32		stale;			///< found, but they do not carry the attribute (any more)
	int32		unreadable;		///< entries of the index that the query could not read

	bool Damaged() const { return missing > 0 || stale > 0 || unreadable > 0; }
	/** "SEN:TO on Haiku: the query finds 146 of 245 files that have the attribute, 3 entries of deleted files"; empty if not damaged. */
	BString Describe() const;
};

/** All indices of SEN (the names that start with "SEN:") of a volume, compared with its files. Needs the index for SEN:ID to find the objects. */
std::vector<IndexState> CheckIndices(const Volume& volume);

struct RepairResult {
	int32					volumes;			///< volumes that were looked at
	int32					indicesDamaged;		///< indices that were found damaged (all that were made again, with force)
	int32					indicesRepaired;	///< indices that were made again and are in order now
	int32					filesScanned;		///< files that were opened to write attributes again
	int32					attributesRewritten;
	int32					attributesLost;		///< attributes that were removed and could not be written again (their values are in the log): counted as failures
	int32					failures;			///< indices that could not be made again or are still damaged
	std::vector<BString>	notes;				///< the report, one line each, in the order of the work

	RepairResult() : volumes(0), indicesDamaged(0), indicesRepaired(0), filesScanned(0), attributesRewritten(0), attributesLost(0), failures(0) {}
	/** The notes as one text, a line each. */
	BString Report() const;
};

/**
 * Make the damaged indices of SEN on all volumes that SEN works with again: remove, create, write the attribute of the files again, then compare again.
 *
 * Only the files that SEN could have relations on are reindexed: the SEN objects (found through SEN:ID), which takes seconds. With @p deep every file of the volume is
 * opened (minutes on a large volume); that is what is done anyway when the index of SEN:ID itself is damaged, as the objects cannot be found through it then.
 *
 * The files are written while the server and other programs may write them: an attribute is read and written again at once, so that the window for a change of another
 * program is small, but a repair is best run when nothing is being enriched. One repair at a time.
 *
 * @param result   what was found and done (may be NULL)
 * @param force    make all indices of SEN again, also those that are in order
 * @param deep     write the attributes of all files of the volumes (found through the name index) instead of the SEN objects
 * @param progress called with a line of text now and then (from the thread that repairs; may be empty)
 * @return B_OK if every index that was damaged is in order now (also when none was), B_BUSY if a repair runs already, else B_ERROR
 */
status_t RepairIndices(RepairResult* result, bool force = false, bool deep = false, const std::function<void(const BString&)>& progress = nullptr);

}	// namespace stats
}	// namespace sen
