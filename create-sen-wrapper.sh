#!/bin/bash
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Creates the command "sen", a small wrapper around the SEN server, so that its commands are easy to type:
#
#   sen              start the server (in this terminal, like sen_server)
#   sen start        start the server, detached (the packaged one, if there is one)
#   sen stop         stop the server (as a service of the launch daemon, which would start it again if it just quit)
#   sen restart      stop the server and start it again
#   sen version      the same as sen_server --version
#   sen info         --info:    an overview of SEN objects, types and relations
#   sen types        --types:   the types and relations as a tree, with the number of files
#   sen check        --check:   look for inconsistencies
#   sen migrate      --migrate: the steps that make the data of the user what this version expects (sen migrate --dry-run: only counts)
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

CONFIGURED="$SERVER"
SIGNATURE=application/x-vnd.sen-labs.sen-server
SERVICE=x-vnd.sen-labs.sen-server

# The server to start: the one of the package (for the system, then for the user), only then the one that this wrapper was made for. A server is not
# looked up by its signature: that finds any version of it, also an old build.
server()
{
    for candidate in /boot/system/bin/sen_server "\$(finddir B_USER_BIN_DIRECTORY)/sen_server"; do
        if [ -x "\$candidate" ]; then
            echo "\$candidate"
            return
        fi
    done
    echo "\$CONFIGURED"
}

running()
{
    hey "\$SIGNATURE" SCst >/dev/null 2>&1
}

# the team of the running server (empty if there is none)
team()
{
    ps | grep "[s]en_server" | awk '{print \$(NF-3)}' | head -1
}

# whether the server is a service of the launch daemon that runs (which starts it again when it quits)
service_running()
{
    launch_roster list 2>/dev/null | grep "\$SERVICE" | grep -q running
}

# wait until the server is (up, or else) gone; seconds (checked five times a second)
wait_for()
{
    i=0
    while [ \$i -lt \$((\$2 * 5)) ]; do
        if [ "\$1" = up ] && running; then return 0; fi
        if [ "\$1" = gone ] && ! running; then return 0; fi
        sleep 0.2
        i=\$((i + 1))
    done
    return 1
}

stop_server()
{
    # As a service of the launch daemon the server is started again when it quits: it is stopped as a service. One that was started by hand is no
    # service, it is asked to quit.
    launch_roster stop "\$SERVICE" >/dev/null 2>&1
    wait_for gone 3 && return 0
    hey "\$SIGNATURE" quit >/dev/null 2>&1
    wait_for gone 10
}

case "\$1" in
    version|info|types|check|migrate|help)
        option="--\$1"
        shift
        exec "\$(server)" "\$option" "\$@"
        ;;
    start)
        shift
        if running; then
            echo "the SEN server is running already."
            exit 0
        fi
        "\$(server)" "\$@" > /dev/null 2>&1 < /dev/null &
        ;;
    stop)
        stop_server
        ;;
    restart)
        shift
        # Quitting is enough for a service that runs: the launch daemon starts it again at once. A service that was stopped is started, and a
        # server that is no service (or none at all) is started here.
        old="\$(team)"
        service=no
        service_running && service=yes
        if [ -n "\$old" ]; then
            hey "\$SIGNATURE" quit >/dev/null 2>&1
            i=0
            while [ \$i -lt 50 ] && [ "\$(team)" = "\$old" ]; do
                sleep 0.2
                i=\$((i + 1))
            done
        fi
        if [ "\$service" = yes ] && wait_for up 5; then
            :
        else
            launch_roster start "\$SERVICE" >/dev/null 2>&1
            if ! wait_for up 2; then
                "\$(server)" "\$@" > /dev/null 2>&1 < /dev/null &
            fi
        fi
        ;;
    *)
        exec "\$(server)" "\$@"
        ;;
esac
WRAPPER
chmod +x "$COMMAND"

echo "created $COMMAND (starts $SERVER)"
case ":$PATH:" in
    *":$FOLDER:"*) ;;
    *) echo "note: $FOLDER is not in the PATH" >&2 ;;
esac
