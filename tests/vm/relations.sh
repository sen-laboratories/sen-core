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
# (a service: it would be started again, and answer instead of the one under test. In the background: a server that does not quit leaves launch_roster waiting for good)
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
stopper=$!
sleep 3
kill -9 $stopper 2>/dev/null
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2

echo "== compiling the test"
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -I${SENTO_TESTS:-/Develop/SEN/sento/tests} -o /tmp/relation-test $SRC/RelationTest.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/relation-test ] || { echo "FAIL the test did not compile"; touch "$DONE"; exit 1; }

SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4

echo "== relation tests"
/tmp/relation-test
echo "exit $?"

echo "== references to files that are gone: found by the check, removed by the repair"
# (a process of its own that has ended when the check runs: what a test process holds open looks like a damaged index entry to it)
SETUP=$(/tmp/relation-test dangling-setup)
GONE=$(echo "$SETUP" | awk '{print $1}')
HOLDER=$(echo "$SETUP" | cut -d' ' -f2-)
echo "   deleted file $GONE, still referenced by $HOLDER"
sleep 3
$SERVER --check > /tmp/rel-check.txt 2>&1
cat /tmp/rel-check.txt
grep -q "deleted file" /tmp/rel-check.txt && echo "ok   the check finds the references to the deleted file" || echo "FAIL the check finds the references to the deleted file"
$SERVER --repair > /tmp/rel-repair.txt 2>&1
cat /tmp/rel-repair.txt
grep -q "to deleted files removed" /tmp/rel-repair.txt && echo "ok   the repair removes them" || echo "FAIL the repair removes them"
listattr "$HOLDER" | grep -q "SEN:TO" && echo "FAIL the holder still lists targets" || echo "ok   the holder has no targets any more"
$SERVER --check > /tmp/rel-check.txt 2>&1
grep -q "deleted file" /tmp/rel-check.txt && echo "FAIL the check is clean after the repair" || echo "ok   the check is clean after the repair"
rm -rf "$TEST_DIR/_senrel"

for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
launch_roster start x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
} > "$OUT" 2>&1
touch "$DONE"
