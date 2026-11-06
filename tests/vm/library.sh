#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The Library of a context (LibraryTest.cpp), against the server under test ($SERVER; run.sh passes it on). Started by run.sh.
SRC=${SRC:-$(dirname "$0")}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/sen-library.out}
DONE=${DONE:-/tmp/sen-library.done}
rm -f "$DONE" "$OUT"
{
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
$SERVER > /tmp/sen_server.log 2>&1 &
sleep 4
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -o /tmp/library-test $SRC/LibraryTest.cpp -lbe 2>&1 | grep -E "error" | head -20
if [ -x /tmp/library-test ]; then /tmp/library-test; else echo "FAIL the test did not compile"; fi
# the command line asks the server, and does not make anything
path=$($SERVER --library entity/x-vnd.sen-labs.entity.person 2>&1)
case "$path" in /boot/home/Library/*) echo "ok   --library prints the folder of a type: $path" ;; *) echo "FAIL --library: $path" ;; esac
path=$($SERVER --library --context default 2>&1)
[ "$path" = /boot/home/Library ] && echo "ok   --library --context default prints the Library" || echo "FAIL --library --context default: $path"
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
} > "$OUT" 2>&1
touch "$DONE"
