#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The relation tests (RelationTest.cpp) against a freshly started sen_server, in a Haiku VM (started by run.sh).
# Needs the built sen_server and the installed core ontology (sen-oni); compiles the test with the SEN headers.

TEST_DIR=${TEST_DIR:-/Develop/test}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
SRC=${SRC:-/Develop/SEN/sen-core/tests/vm}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/sen-relations.out}
DONE=${DONE:-/tmp/sen-relations.done}
rm -f "$DONE" "$OUT"
export TEST_DIR

{
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2

echo "== compiling the test"
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -I/Develop/SEN/sento/tests -o /tmp/relation-test $SRC/RelationTest.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/relation-test ] || { echo "FAIL the test did not compile"; touch "$DONE"; exit 1; }

SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4

echo "== relation tests"
/tmp/relation-test
echo "exit $?"

for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
} > "$OUT" 2>&1
touch "$DONE"
