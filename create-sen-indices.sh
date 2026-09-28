#!/bin/bash
#
# Creates the BFS filesystem indices SEN relies on for BQuery-based relation
# resolution and plugin discovery. Without these, BQuery predicates against
# these attributes silently fail to find matches even though the attributes
# are present on disk.
#
# Safe to re-run: mkindex is a no-op (exit 0) if an index already exists.
#
# Also reindexes existing files under rebuild-path: BFS only inserts a file
# into an index going forward from when BOTH the index and the attribute
# exist - an attribute written before its index was created is invisible to
# queries until its file is explicitly reindexed. This matters whenever SEN
# indices are (re-)created on a filesystem that already has SEN-tagged
# files (e.g. after an upgrade, or a repair) - on a genuinely fresh install
# there won't be any such files yet, so this step is just a harmless no-op.
#
# Usage: create-sen-indices.sh [volume-path] [rebuild-path]
#   volume-path   defaults to /boot (the boot volume), matching the current
#                 assumption in sen-core's relation queries (see RelationHandler.cpp).
#   rebuild-path  defaults to $HOME; the directory tree to reindex existing
#                 SEN-tagged files under.

set -e

VOLUME="${1:-/boot}"
REBUILD_PATH="${2:-$HOME}"

# attribute -> type, derived from actual BQuery predicates in sen-core
# (RelationHandler.cpp, SelfRelationHandler.cpp) and senryu (PoseViewSen.cpp):
#
#   SEN:ID                - unique object-identifier for the relation source
#                           (QueryForUniqueSenId: "SEN:ID == <id>")
#   SEN:TO                - list of target IDs on the source, for reverse
#                           relation lookup (QueryForTargetsById: "SEN:TO == '*<id>*'")
#   SEN:TYPE              - semantic file type, as opposed to the more
#                           technical BEOS:TYPE MIME type
#                           (GetPluginsForTypeAndFeature: "SEN:TYPE == <plugin-type>")
#   SEN:plugin:extract    - plugin feature flags, one index per feature since
#   SEN:plugin:enrich       each is queried as its own attribute
#   SEN:plugin:identify     ("SEN:plugin:<feature> == 1")
#   SEN:plugin:navigate
#   SEN:plugin:search       (defined in Sensei.h, not yet used by a plugin,
#                           included for forward compatibility)

STRING_ATTRS="SEN:ID SEN:TO SEN:TYPE"
INT_ATTRS="SEN:plugin:extract SEN:plugin:enrich SEN:plugin:identify SEN:plugin:navigate SEN:plugin:search"
ALL_ATTRS="$STRING_ATTRS $INT_ATTRS"

echo "Creating SEN indices on volume $VOLUME ..."

for attr in $STRING_ATTRS; do
    mkindex -d "$VOLUME" -t string -v "$attr"
done

for attr in $INT_ATTRS; do
    mkindex -d "$VOLUME" -t int -v "$attr"
done

echo "Reindexing existing SEN-tagged files under $REBUILD_PATH ..."

for attr in $ALL_ATTRS; do
    reindex -rv "$attr" "$REBUILD_PATH"
done

echo "Done."
