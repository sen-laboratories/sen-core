#!/bin/bash
#
# Creates the BFS filesystem indices SEN relies on for BQuery-based relation
# resolution and plugin discovery. Without these, BQuery predicates against
# these attributes silently fail to find matches even though the attributes
# are present on disk.
#
# Safe to re-run: mkindex is a no-op (exit 0) if an index already exists.
#
# Usage: create-sen-indices.sh [volume-path]
#   volume-path defaults to /boot (the boot volume), matching the current
#   assumption in sen-core's relation queries (see RelationHandler.cpp).

set -e

VOLUME="${1:-/boot}"

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

echo "Creating SEN indices on volume $VOLUME ..."

for attr in $STRING_ATTRS; do
    mkindex -d "$VOLUME" -t string -v "$attr"
done

for attr in $INT_ATTRS; do
    mkindex -d "$VOLUME" -t int -v "$attr"
done

echo "Done."
