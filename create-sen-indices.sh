#!/bin/bash
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
#
# Creates the BFS indices that SEN relies on, on every mounted volume that can have them, and reindexes existing files.
#
# Without these indices, queries for the attributes silently find nothing even though the attributes are present on
# disk. BFS inserts a file into an index only from the moment that BOTH the index and the attribute exist: an attribute
# written before its index was created stays invisible to queries until its file is reindexed, hence the second step.
#
# The ontology installer (sen-oni) and the SEN server create the same indices (the list comes from the ontology), this
# script is for repairing a system and for the post-install step of the package. Safe to re-run.
#
# Usage: create-sen-indices.sh [volume-path...]
#   volume-path   the mount points to index; defaults to all mounted volumes
#
# The attributes (all strings, the first predicate of every SEN query):
#   SEN:ID     the unique identifier of an object              ("SEN:ID == <id>")
#   SEN:TO     the targets of the normal relations, chunked    ("SEN:TO == '*<id>*'")
#   SEN:META   the targets of the meta relations (same)        ("SEN:META == '*<id>*'")
#   META:TYPE  the semantic type; plugins carry the plugin type here, so they are found by it
# The feature flags of plugins (SEN:plugin:*) are 16 bit values: BFS cannot index those, the server reads them.

set -e

ATTRS="SEN:ID SEN:TO SEN:META META:TYPE"

if [ $# -gt 0 ]; then
    VOLUMES="$@"
else
    VOLUMES=$(df | sed -n 's/^ *Mounted at: *//p')
fi

for volume in $VOLUMES; do
    echo "Volume $volume ..."
    for attr in $ATTRS; do
        # fails harmlessly on volumes that cannot be indexed (read-only, no attributes) and when the index exists
        mkindex -d "$volume" -t string -v "$attr" 2>&1 | grep -v "File exists" || true
    done

    echo "  reindexing existing files under $volume ..."
    for attr in $ATTRS; do
        reindex -r "$attr" "$volume" > /dev/null 2>&1 || true
    done
done

echo "Done."
