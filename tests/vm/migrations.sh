#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The migration steps, see MigrationTest.cpp. Does not start or stop the server. Needs the SEN headers installed (sento: install.sh) and spdlog.
# The files are made in TEST_DIR (default /Develop/test), a BFS volume with the index for SEN:ID.
SRC=${SRC:-$(dirname "$0")}
HEADERS=/boot/home/config/non-packaged/develop/headers
rm -f /tmp/migration-test
g++ -std=c++20 -Wall -Wno-multichar -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -I$HEADERS -o /tmp/migration-test $SRC/MigrationTest.cpp $SRC/../../src/migrations/Migrations.cpp -lbe -lspdlog -lfmt 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/migration-test ] || { echo "FAIL the test did not compile"; exit 1; }
/tmp/migration-test
