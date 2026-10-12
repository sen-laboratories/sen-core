#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Prints the menu of the SEN item in the Deskbar as it is built now, and checks its order (MenuTest.cpp). Does not touch the server.
SRC=${SRC:-$(dirname "$0")}
SENCORE=$SRC/../..
HEADERS=/boot/home/config/non-packaged/develop/headers
g++ -std=c++20 -Wall -Wno-multichar -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -I$HEADERS -o /tmp/menu-test $SRC/MenuTest.cpp $SENCORE/src/deskbar/DeskbarView.cpp \
	$SENCORE/src/ui/Knot.cpp -lbe -ltranslation 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/menu-test ] || { echo "FAIL the test did not compile"; exit 1; }
/tmp/menu-test
