#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Repairs the BFS indices of SEN that are damaged: an index whose query does not find files that have the attribute, or stops at an entry of a file that is
# gone. It happens after a crash or a forced stop of the machine (the syslog has "inode ... is already deleted" and "IndexIteratorGetEntry: Bad data"), and
# relations of the files that the index does not find are invisible: no error, they are just not there.
#
# What it does is the Check & Repair item of the SEN menu: it sends sen::cmd::kRepairIndices to the SEN server (sen_server --repair; without a server
# running the command repairs by itself). For every volume the indices of SEN (SEN:ID, SEN:TO, SEN:META and their chunks) are compared with the files; a damaged
# index is removed (rmindex), created again (mkindex) and the attribute of the files is written again so that BFS puts it into the index (reindex), all with the API
# of Haiku. Indices that are in order are not touched. Safe to re-run.
#
# Usage: repair-sen-indices.sh [--force] [--deep]
#   --force   make all indices of SEN again, also those that are in order
#   --deep    write the attribute of EVERY file of the volume, not only of the SEN objects (slow: a minute or more on a large volume; it is done anyway when the index of
#             SEN:ID itself is damaged, as the objects cannot be found through it then)
# Exit status: 0 if the indices are in order afterwards, 1 if not (the report says what is still wrong), 2 for a wrong argument.
#
# (The same by hand, per volume and index:  rmindex -d <volume> SEN:TO;  mkindex -d <volume> -t string SEN:TO;  reindex -r SEN:TO <folder>)

for argument in "$@"; do
    case "$argument" in
        --force|--deep) ;;
        *) echo "usage: repair-sen-indices.sh [--force] [--deep]" >&2; exit 2 ;;
    esac
done

# the server of the package (for the system, then for the user), else the one that is in the PATH or next to this script
for candidate in /boot/system/bin/sen_server "$(finddir B_USER_BIN_DIRECTORY 2>/dev/null)/sen_server" "$(command -v sen_server)" "$(dirname "$0")/bin/sen_server"; do
    if [ -x "$candidate" ]; then
        exec "$candidate" --repair "$@"
    fi
done

echo "sen_server not found." >&2
exit 1
