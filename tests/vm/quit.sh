#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The quit path of the server: asked to quit it ends within seconds (the Deskbar is asked with a time limit, and a watchdog ends a server that is stuck), in a Haiku VM (started by run.sh).
# Runs a server of its own without a Deskbar item (SEN_NO_DESKBAR), so that the Deskbar of the machine is not touched.

SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
OUT=${OUT:-/tmp/sen-quit.out}
DONE=${DONE:-/tmp/sen-quit.done}
rm -f "$DONE" "$OUT"
FAILED=0
ok() { echo "ok   $1"; }
fail() { echo "FAIL $1"; FAILED=$((FAILED+1)); }
running() { ps | grep -q "[s]en_server"; }

{
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
stopper=$!
sleep 3
kill -9 $stopper 2>/dev/null
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2

for round in 1 2 3; do
	SEN_NO_DESKBAR=1 SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
	sleep 4
	running && ok "round $round: the server runs" || fail "round $round: the server runs"
	START=$(date +%s)
	hey application/x-vnd.sen-labs.sen-server quit > /dev/null 2>&1
	i=0
	while running && [ $i -lt 40 ]; do sleep 0.5; i=$((i+1)); done
	SECONDS_TAKEN=$(( $(date +%s) - START ))
	echo "   quit took about $SECONDS_TAKEN s"
	running && fail "round $round: the server quit" || ok "round $round: the server quit"
	[ $SECONDS_TAKEN -lt 8 ] && ok "round $round: within 8 seconds" || fail "round $round: within 8 seconds"
	grep -q "leaving" /tmp/sen_server.log && fail "round $round: the watchdog did not have to end it" || ok "round $round: the watchdog did not have to end it"
	grep -q "Goodbye" /tmp/sen_server.log && ok "round $round: the orderly end ran" || fail "round $round: the orderly end ran"
	for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
done

launch_roster start x-vnd.sen-labs.sen-server > /dev/null 2>&1 &
echo "$FAILED failure(s)"
} > "$OUT" 2>&1
touch "$DONE"
