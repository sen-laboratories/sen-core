#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Dynamic relations between files (DynamicRelationTest.cpp), against the server under test ($SERVER; run.sh passes it on). The navigator plugin that the server asks is a stub
# (DynamicStub.cpp) that this script builds, with the resources that make it a plugin the roster can find by its signature and that name the relation type that it answers (the server asks only a plugin that does). Started by run.sh.
TEST_DIR=${TEST_DIR:-/Develop/test}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
SRC=${SRC:-/Develop/SEN/sen-core/tests/vm}
HEADERS=/boot/home/config/non-packaged/develop/headers
OUT=${OUT:-/tmp/sen-dynamic.out}
DONE=${DONE:-/tmp/sen-dynamic.done}
STUBDIR=${STUBDIR:-$TEST_DIR/_dynamic-stub}
rm -f "$DONE" "$OUT"
export TEST_DIR

{
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1   # (a service: it would be started again, and answer instead of the one under test)
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2

echo "== building the stub plugin and the test"
rm -rf "$STUBDIR"; mkdir -p "$STUBDIR"
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -o "$STUBDIR/DynamicStub" $SRC/DynamicStub.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
cat > "$STUBDIR/stub.rdef" <<RDEF
resource app_signature "application/x-vnd.sen-plugin.sen-labs.test-dynamic-stub";
resource app_flags B_MULTIPLE_LAUNCH | B_BACKGROUND_APP;
resource file_types message {
	"types" = "relation/x-vnd.sen-labs.relation.test.dynamic"
};
RDEF
rc -o "$STUBDIR/stub.rsrc" "$STUBDIR/stub.rdef" && xres -o "$STUBDIR/DynamicStub" "$STUBDIR/stub.rsrc" && mimeset -f "$STUBDIR/DynamicStub"
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -I${SENTO_TESTS:-/Develop/SEN/sento/tests} -o /tmp/dynamic-relation-test $SRC/DynamicRelationTest.cpp -lbe -lpthread 2>&1 | grep -E "error|Error" | head -20
[ -x "$STUBDIR/DynamicStub" ] && [ -x /tmp/dynamic-relation-test ] || { echo "FAIL the stub or the test did not compile"; touch "$DONE"; exit 1; }

SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4

echo "== dynamic relation tests"
/tmp/dynamic-relation-test
echo "exit $?"

for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
rm -rf "$STUBDIR"
launch_roster start x-vnd.sen-labs.sen-server > /dev/null 2>&1
} > "$OUT" 2>&1
touch "$DONE"
