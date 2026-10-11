#!/bin/bash
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Creates the command "sen", a small wrapper around the SEN server, so that its commands are easy to type:
#
#   sen              start the server (in this terminal, like sen_server)
#   sen start        start the server, detached
#   sen stop         quit the running server
#   sen version      the same as sen_server --version
#   sen info         --info:    an overview of SEN objects, types and relations
#   sen types        --types:   the types and relations as a tree, with the number of files
#   sen check        --check:   look for inconsistencies
#   sen help         the options
#   sen --check      (any option of sen_server works, too)
#
# Used by hand and as the post-install step of the package. Safe to re-run: the wrapper is made new.
#
# Usage: create-sen-wrapper.sh [<sen_server> [<folder for the command> [<path of the server when installed>]]]
#   <sen_server>   the server to start; default: bin/sen_server next to this script, else the one in the PATH
#   <folder>       where the command goes, which has to be in the PATH; default: the folder for programs that were not installed
#                  as a package (~/config/non-packaged/bin). The package puts it in its own bin folder.
#   <installed>    the path that the command starts, if it is not the one of <sen_server>: the package is made (and the command with it)
#                  before the server is installed, e.g. /boot/system/bin/sen_server.

set -e

HERE="$(cd "$(dirname "$0")" && pwd)"

SERVER="$1"
if [ -z "$SERVER" ]; then
    if [ -x "$HERE/bin/sen_server" ]; then
        SERVER="$HERE/bin/sen_server"
    else
        SERVER="$(command -v sen_server || true)"
    fi
fi
if [ -z "$SERVER" ] || [ ! -x "$SERVER" ]; then
    echo "sen_server not found: pass its path as the first argument." >&2
    exit 1
fi
# the wrapper names the server by its full path, wherever the wrapper is called from
SERVER="$(cd "$(dirname "$SERVER")" && pwd)/$(basename "$SERVER")"
[ -n "$3" ] && SERVER="$3"

FOLDER="$2"
if [ -z "$FOLDER" ]; then
    FOLDER="$(finddir B_USER_NONPACKAGED_BIN_DIRECTORY)"
fi
mkdir -p "$FOLDER"

COMMAND="$FOLDER/sen"
cat > "$COMMAND" <<WRAPPER
#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Wrapper for the SEN server, made by create-sen-wrapper.sh.

SERVER="$SERVER"
SIGNATURE=application/x-vnd.sen-labs.sen-server

case "\$1" in
    version|info|types|check|help)
        option="--\$1"
        shift
        exec "\$SERVER" "\$option" "\$@"
        ;;
    start)
        shift
        "\$SERVER" "\$@" > /dev/null 2>&1 < /dev/null &
        ;;
    stop)
        exec hey "\$SIGNATURE" quit
        ;;
    *)
        exec "\$SERVER" "\$@"
        ;;
esac
WRAPPER
chmod +x "$COMMAND"

echo "created $COMMAND (starts $SERVER)"
case ":$PATH:" in
    *":$FOLDER:"*) ;;
    *) echo "note: $FOLDER is not in the PATH" >&2 ;;
esac
