/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 SEN Labs e.U.
 */
#pragma once

#include <Message.h>
#include <String.h>

namespace sen {
namespace enrich {

/**
 * Handles sen::cmd::kEnrichRefs: for each file the enrichment plugin that handles its type is started (the plugin enriches the file, asks the user if it has several answers and tells what
 * it did with a notification). A client does not look for plugins, so that the choice is the same for all of them: the file types that a plugin declares are what counts, a plugin for
 * papers does not get a book.
 * @param request the message: the files as `refs`, `wipe` (default true) and `auto` (default false) are passed on to the plugin
 * @param detail receives a sentence for the reply
 * The plugins are started one after the other (each when the one before it has ended), so that a plugin that asks the user is not asked for two files at once.
 * @return B_OK if a plugin is going to be started for at least one file, B_NOT_SUPPORTED if no plugin handles any of the files, B_BAD_VALUE if there is no file
 */
status_t Handle(const BMessage& request, BString* detail);

}	// namespace enrich
}	// namespace sen
