#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Performance of the lookup in the chunked target lists (perf/QueryPerf.cpp), in a Haiku VM (started by run.sh; give it time:
# SEN_VM_WAIT=900 run.sh perf.sh). Files are only created in a temporary folder on the volume of $TEST_DIR.
TEST_DIR=${TEST_DIR:-/Develop/test}
FILES=${FILES:-5000}
HEADERS=/boot/home/config/non-packaged/develop/headers
SRC=${SRC:-/Develop/SEN/sen-core/tests/vm/perf}
OUT=${OUT:-/tmp/sen-perf.out}
DONE=${DONE:-/tmp/sen-perf.done}
rm -f "$DONE" "$OUT"
{
g++ -std=c++20 -O2 -Wno-multichar -I$HEADERS -o /tmp/queryperf $SRC/QueryPerf.cpp -lbe 2>&1 | grep -E "error" | head
[ -x /tmp/queryperf ] || { echo "FAIL did not compile"; touch "$DONE"; exit 1; }
for profile in sparse dense; do
	/tmp/queryperf $TEST_DIR/_perf $FILES $profile
done
} > "$OUT" 2>&1
touch "$DONE"
