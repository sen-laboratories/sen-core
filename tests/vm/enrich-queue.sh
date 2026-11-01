#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The queue of the enrichment (EnrichQueueTest.cpp) against a freshly started sen_server: stops the one that runs and starts SERVER (default
# /Develop/SEN/sen-core/bin/sen_server), without the Deskbar. Needs BERT installed and a network. Start the packaged server again afterwards (sen start).
SRC=${SRC:-$(dirname "$0")}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
HEADERS=/boot/home/config/non-packaged/develop/headers
launch_roster stop x-vnd.sen-labs.sen-server > /dev/null 2>&1
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
rm -f /tmp/enrich-queue-test
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -o /tmp/enrich-queue-test $SRC/EnrichQueueTest.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/enrich-queue-test ] || { echo "FAIL the test did not compile"; exit 1; }
SEN_NO_DESKBAR=1 SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server-enrich.log 2>&1 &
sleep 4
/tmp/enrich-queue-test
status=$?
grep "enrich:" /tmp/sen_server-enrich.log | cut -c1-150
kill -9 $(ps | grep "[s]en_server" | awk '{print $(NF-3)}') 2>/dev/null
exit $status
