#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Live test in a Haiku VM: sen_server runs as a service, relations are made on real files through it, and the SENryu Tracker
# (built by sento/tools/tracker-vm-build.sh) runs next to it: it must start, open a window and not crash. Takes a screenshot to
# $OUT_DIR. The system Tracker is stopped for the test and started again at the end (the VM's desktop needs it).
# Needs: built sen_server, core ontology installed, a Tracker build in /Develop/haiku-senryu, a PDF in $TEST_DIR.
# Started by run.sh live.sh (results are collected from $OUT).

TEST_DIR=${TEST_DIR:-/Develop/test}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
BUILD=${BUILD:-/Develop/haiku-senryu/generated/objects/haiku/x86_64/release}
RUN=/Develop/akita-run
LIVE=$TEST_DIR/_live
OUT=${OUT:-/tmp/sen-live.out}
DONE=${DONE:-/tmp/sen-live.done}
OUT_DIR=/tmp/sen-live
SIG=application/x-vnd.sen-labs.sen-server
PASSED=0; FAILED=0
rm -f "$DONE" "$OUT"; rm -rf "$OUT_DIR"; mkdir -p "$OUT_DIR"

check() { if eval "$2" >/dev/null 2>&1; then echo "ok   $1"; PASSED=$((PASSED+1)); else echo "FAIL $1"; FAILED=$((FAILED+1)); fi; }

# the screen blanker blanks screenshots; the loop ends with this script
( while true; do for p in $(ps | grep "[s]creen_blanker" | awk '{print $(NF-3)}'); do kill -9 $p; done; sleep 2; done ) &
BLANK=$!

{
echo "== start the server as a service"
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
SEN_LOG_LEVEL=info $SERVER > $OUT_DIR/server.log 2>&1 &
sleep 4
check "server answers" "hey $SIG SCst | grep -q operational"

echo "== relations on real files"
rm -rf $LIVE; mkdir -p $LIVE; cd $LIVE
cp $TEST_DIR/bm1.pdf paper.pdf
printf '# Notes\n\nabout the paper\n' > notes.md
printf 'a text\n' > extra.txt
hey $SIG SRad with refs=$LIVE/notes.md and SEN:relationType=relation/x-vnd.sen-labs.relation.reference and SEN:targetRef=$LIVE/paper.pdf > $OUT_DIR/add1.txt 2>&1
hey $SIG SRad with refs=$LIVE/notes.md and SEN:relationType=relation/x-vnd.sen-labs.relation.reference and SEN:targetRef=$LIVE/extra.txt > $OUT_DIR/add2.txt 2>&1
check "adding a relation is answered with status 201" "grep -q 'status.*201' $OUT_DIR/add1.txt"
check "the source lists both targets" "[ \$(catattr SEN:TO notes.md | tr ',' '\n' | wc -l) -ge 2 ]"
check "the target has the opposite direction" "listattr paper.pdf | grep -q 'SEN:REL:x-vnd.sen-labs.relation.reference'"
hey $SIG SRga with refs=$LIVE/notes.md and SEN:withProperties=true > $OUT_DIR/get.txt 2>&1
check "all relations of the source are returned" "grep -q 'relation/x-vnd.sen-labs.relation.reference' $OUT_DIR/get.txt"
hey $SIG SRsa with refs=$LIVE/paper.pdf > $OUT_DIR/self.txt 2>&1
check "contained relations of the PDF come from the extractor plugin" "grep -q 'relation.docref' $OUT_DIR/self.txt"

echo "== the SENryu Tracker next to the server"
mkdir -p $RUN/lib
cp $BUILD/kits/tracker/libtracker.so $RUN/lib/ && cp $BUILD/apps/tracker/Tracker $RUN/Akita
check "the Tracker build is there" "[ -x $RUN/Akita ]"
launch_roster stop x-vnd.be-trak > /dev/null 2>&1
sleep 2
export LIBRARY_PATH=%A/lib:/boot/home/config/non-packaged/lib:/boot/home/config/lib:/boot/system/non-packaged/lib:/boot/system/lib
( cd $RUN && ./Akita $LIVE > $OUT_DIR/tracker.log 2>&1 & )
sleep 8
check "the Tracker is running" "ps | grep -q '[A]kita'"
screenshot -s -f png $OUT_DIR/window.png
check "a screenshot was taken" "[ -s $OUT_DIR/window.png ]"
# the server and the Tracker keep working together
hey $SIG SRga with refs=$LIVE/notes.md > $OUT_DIR/get2.txt 2>&1
check "the server still answers" "grep -q 'relation/x-vnd.sen-labs.relation.reference' $OUT_DIR/get2.txt"
check "the Tracker did not crash" "ps | grep -q '[A]kita' && ! ls ~/Desktop/Akita*.report"

echo "== clean up"
for p in $(ps | grep "[A]kita" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 1
launch_roster start x-vnd.be-trak > /dev/null 2>&1 || /boot/system/Tracker > /dev/null 2>&1 &
sleep 3
check "the system Tracker is back" "ps | grep -q '[T]racker'"
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
rm -rf $LIVE
echo "$PASSED passed, $FAILED failed"
} > "$OUT" 2>&1
kill $BLANK 2>/dev/null
touch "$DONE"
