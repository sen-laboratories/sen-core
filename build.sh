#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the SEN server (bin/sen_server) and the application of the settings window (bin/sen_settings) as a release: optimised, without the debugging code. Arguments are passed on to make.
#   ./build.sh          the release build, for package.sh
#   ./build.sh debug    a build with debugging code and symbols (make with its default settings)
set -e
cd "$(dirname "$0")"

JOBS=$(getarch >/dev/null 2>&1 && nproc 2>/dev/null || echo 2)

if [ "$1" = debug ]; then
	shift
	make -j"$JOBS" "$@"
	exec make -f Makefile.settings -j"$JOBS" "$@"
fi
make -j"$JOBS" OPTIMIZE=FULL SYMBOLS=FALSE DEBUGGER=FALSE "$@"
exec make -f Makefile.settings -j"$JOBS" OPTIMIZE=FULL SYMBOLS=FALSE DEBUGGER=FALSE "$@"
