#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The tests of the relation folders of the SENryu Tracker (TrackerFoldersTest.cpp) against a freshly started sen_server, in a
# Haiku VM (started by run.sh). Compiles the Tracker sources of the relation folders together with the test; they come from the
# tree that sen-vm.sh syncs to /Develop/SEN/senryu.
TEST_DIR=${TEST_DIR:-/Develop/test}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
SRC=${SRC:-/Develop/SEN/sen-core/tests/vm}
TRACKER=${TRACKER:-/Develop/SEN/senryu/src/kits/tracker}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/sen-tracker-folders.out}
DONE=${DONE:-/tmp/sen-tracker-folders.done}
rm -f "$DONE" "$OUT" /tmp/tracker-folders-test
export TEST_DIR
{
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
echo "== compiling"
g++ -std=c++20 -Wno-multichar -I$HEADERS -I/Develop/SEN/sento/tests -I$SRC -I$TRACKER -o /tmp/tracker-folders-test \
	$SRC/TrackerFoldersTest.cpp $TRACKER/TrackerSenRelations.cpp $TRACKER/RelationFolders.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/tracker-folders-test ] || { echo "FAIL the test did not compile"; touch "$DONE"; exit 1; }
SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4
echo "== tracker relation folders"
/tmp/tracker-folders-test 2>&1 | grep -vE "^(adding|skipping|writing|got |\\*|  \\*|  >|  o|  -|BMessage|\\t|\\}|  x WARN)"
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
} > "$OUT" 2>&1
touch "$DONE"
