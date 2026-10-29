#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Reads the graph of the objects (the data of the graph window) and checks it, see GraphDataTest.cpp. Does not start or stop the server.
# Needs the SEN headers installed (sento: install.sh) and spdlog. GRAPH_DUMP=1 prints the nodes and relations.
SRC=${SRC:-$(dirname "$0")}
HEADERS=/boot/home/config/non-packaged/develop/headers
g++ -std=c++20 -Wall -Wno-multichar -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -I$HEADERS -o /tmp/graph-data-test $SRC/GraphDataTest.cpp $SRC/../../src/graph/GraphData.cpp -lbe -lspdlog -lfmt 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/graph-data-test ] || { echo "FAIL the test did not compile"; exit 1; }
/tmp/graph-data-test ${GRAPH_DUMP:+dump}
