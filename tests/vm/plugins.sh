#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Checks that the server's plugin lookup (sen::FindPlugins) finds the SENSEI plugins by their signatures, built or installed from packages.
# Does not start or stop the server. Needs the SEN headers installed (sento: install.sh) and spdlog.
SRC=${SRC:-$(dirname "$0")}
HEADERS=/boot/home/config/non-packaged/develop/headers
g++ -std=c++20 -Wall -Wno-multichar -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -I$HEADERS -o /tmp/find-plugins-test $SRC/FindPluginsTest.cpp -lbe -lspdlog -lfmt 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/find-plugins-test ] || { echo "FAIL the test did not compile"; exit 1; }
/tmp/find-plugins-test \
	extract=SenMarkdownExtractor,SenMimeExtractor,SenPdfExtractor,SenCodeExtractor \
	navigate=SenPdfNavigator,SenReferenceNavigator,SenRelationNavigator,SenTextNavigator \
	enrich=bert
