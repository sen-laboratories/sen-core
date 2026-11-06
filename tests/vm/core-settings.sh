#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The settings of the server for the settings window (CoreSettingsTest.cpp), against the server under test ($SERVER; run.sh passes it on). Started by run.sh.
SRC=${SRC:-$(dirname "$0")}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/sen-core-settings.out}
DONE=${DONE:-/tmp/sen-core-settings.done}
rm -f "$DONE" "$OUT"
{
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
$SERVER > /tmp/sen_server.log 2>&1 &
sleep 4
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -o /tmp/core-settings-test $SRC/CoreSettingsTest.cpp -lbe 2>&1 | grep -E "error" | head -20
if [ -x /tmp/core-settings-test ]; then /tmp/core-settings-test; else echo "FAIL the test did not compile"; fi
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
} > "$OUT" 2>&1
touch "$DONE"
