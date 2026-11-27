#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Check & Repair of the indices (SenIndices.cpp, sen_server --check / --repair, the message kRepairIndices) against a freshly started sen_server, in a Haiku VM (started by run.sh).
# The index SEN:TO of the test volume is made empty (what a damaged index looks like to a query: files that have the attribute are not found), then checked and repaired.
# The volume must have SEN objects with relations (default /Develop, the second volume). Needs the built sen_server.

VOLUME=${VOLUME:-/Develop}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
OUT=${OUT:-/tmp/sen-indices.out}
DONE=${DONE:-/tmp/sen-indices.done}
rm -f "$DONE" "$OUT"
FAILED=0

ok() { echo "ok   $1"; }
fail() { echo "FAIL $1"; FAILED=$((FAILED+1)); }
count() { query -v "$VOLUME" "$1==*" 2>/dev/null | wc -l | tr -d ' '; }

{
# (a service: it would be started again, and answer instead of the one under test. In the background: a server that does not quit leaves launch_roster waiting for good)
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
stopper=$!
sleep 3
kill -9 $stopper 2>/dev/null
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2

SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4

BEFORE=$(count SEN:TO)
echo "== SEN:TO finds $BEFORE files on $VOLUME"
[ "$BEFORE" -gt 0 ] && ok "the volume has files with SEN:TO to test with" || fail "the volume has files with SEN:TO to test with"

echo "== healthy: check finds nothing, repair touches nothing"
$SERVER --check > /tmp/idx-check.txt 2>&1
cat /tmp/idx-check.txt
grep -q "damaged" /tmp/idx-check.txt && fail "a healthy index is reported as damaged" || ok "a healthy index is not reported"
$SERVER --repair > /tmp/idx-repair.txt 2>&1
RC=$?
cat /tmp/idx-repair.txt
echo "   exit $RC"
[ $RC -eq 0 ] && ok "repair of healthy indices succeeds" || fail "repair of healthy indices succeeds"
grep -q "0 index" /tmp/idx-repair.txt || grep -q "All indices of SEN are in order" /tmp/idx-repair.txt && ok "nothing was made again" || fail "nothing was made again"
[ "$(count SEN:TO)" = "$BEFORE" ] && ok "the query still finds all files" || fail "the query still finds all files"

echo "== damaged: SEN:TO is empty (the files have the attribute, the index does not know them)"
rmindex -d $VOLUME SEN:TO
mkindex -d $VOLUME -t string SEN:TO
[ "$(count SEN:TO)" = "0" ] && ok "the damaged index finds nothing" || fail "the damaged index finds nothing"
$SERVER --check > /tmp/idx-check.txt 2>&1
cat /tmp/idx-check.txt
grep -q "SEN:TO on" /tmp/idx-check.txt && ok "check names the damaged index" || fail "check names the damaged index"
grep -q "index of SEN on volume\|indices of SEN on volume" /tmp/idx-check.txt && ok "check says what is damaged" || fail "check says what is damaged"

echo "== repair through the server (the message kRepairIndices)"
$SERVER --repair > /tmp/idx-repair.txt 2>&1
RC=$?
cat /tmp/idx-repair.txt
echo "   exit $RC"
[ $RC -eq 0 ] && ok "repair succeeds" || fail "repair succeeds"
grep -q "the SEN server is not running" /tmp/idx-repair.txt && fail "the server answered" || ok "the server answered (not repaired in the command)"
AFTER=$(count SEN:TO)
echo "   SEN:TO finds $AFTER files, before $BEFORE"
[ "$AFTER" = "$BEFORE" ] && ok "all files are found again" || fail "all files are found again"
$SERVER --check > /tmp/idx-check.txt 2>&1
grep -q "SEN:TO on" /tmp/idx-check.txt && fail "check finds the index in order" || ok "check finds the index in order"

echo "== force: all indices again, also those in order"
$SERVER --repair --force > /tmp/idx-repair.txt 2>&1
RC=$?
cat /tmp/idx-repair.txt
echo "   exit $RC"
[ $RC -eq 0 ] && ok "repair --force succeeds" || fail "repair --force succeeds"
[ "$(count SEN:TO)" = "$BEFORE" ] && ok "nothing was lost by --force" || fail "nothing was lost by --force"

echo "== a wrong argument"
$SERVER --repair --nonsense > /dev/null 2>&1
[ $? -eq 2 ] && ok "an unknown argument is refused" || fail "an unknown argument is refused"

echo "== the server still answers"
hey application/x-vnd.sen-labs.sen-server SCst > /dev/null 2>&1 && ok "the server answers after the repair" || fail "the server answers after the repair"

echo "== server log"; grep -i "repair\|damaged\|index" /tmp/sen_server.log | tail -20
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
launch_roster start x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
echo "$FAILED test(s) failed"
} > "$OUT" 2>&1
touch "$DONE"
