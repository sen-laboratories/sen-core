/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Entry.h>

namespace sen {
namespace ui {

/**
 * Make a query file for Tracker in the queries folder of the SEN settings (~/config/settings/sen/queries): opened, it shows the files that the
 * predicate finds on all volumes. The file is made new every time, so the predicate is always the current one.
 *
 * @param name the name of the file, which is the title of the window of the results
 * @param predicate the query, see BQuery::SetPredicate
 * @param columnsType optional MIME type whose attributes are the columns of the results (the attribute SEN:columnsType of the file, read by Akita)
 */
status_t MakeQueryFile(const char* name, const char* predicate, entry_ref* ref, const char* columnsType = NULL);

/** Ask Tracker to open the file (a query shows its results in a window). */
status_t OpenInTracker(const entry_ref& ref);

/**
 * Ask Tracker (Akita) to open the folder of the relations of a type of a file: the files that a classification has, for the association relation. A
 * Tracker that does not know the request (stock Tracker) gives B_NOT_SUPPORTED.
 */
status_t OpenRelationsInTracker(const entry_ref& file, const char* relationType);

/** Ask Tracker to show the file in its folder, selected. */
status_t ShowInTracker(const entry_ref& ref);

}	// namespace ui
}	// namespace sen
