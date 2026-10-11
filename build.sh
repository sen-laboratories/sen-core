#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the SEN server (bin/sen_server) as a release: optimised, without the debugging code. Arguments are passed on to make.
#   ./build.sh          the release build, for package.sh
#   ./build.sh debug    a build with debugging code and symbols (make with its default settings)
set -e
cd "$(dirname "$0")"

JOBS=$(getarch >/dev/null 2>&1 && nproc 2>/dev/null || echo 2)

if [ "$1" = debug ]; then
	shift
	exec make -j"$JOBS" "$@"
fi
exec make -j"$JOBS" OPTIMIZE=FULL SYMBOLS=FALSE DEBUGGER=FALSE "$@"
