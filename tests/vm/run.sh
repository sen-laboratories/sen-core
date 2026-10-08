#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Mac side: runs a test script of this folder in the Haiku VM, detached, polls for the result and prints it.
# usage: run.sh [e2e.sh]      (build and install sen-core, sen-oni and sensei first, see sento/tools/sen-vm.sh)
set -e
SCRIPT=${1:-e2e.sh}
PORT=${SEN_VM_PORT:-2222}; USER=${SEN_VM_USER:-user}; HOST=${SEN_VM_HOST:-localhost}
cd "$(dirname "$0")"
scp -q -P $PORT "$SCRIPT" $USER@$HOST:/tmp/sen-test.sh
# settings of the Mac environment that the test script understands are passed on (e.g. SERVER to test another build)
PASS=""
for name in SERVER SRC TRACKER BUILD RUN TEST_DIR SENTO_TESTS FILES; do
	eval "value=\${$name}"
	[ -n "$value" ] && PASS="$PASS $name='$value'"
done
ssh -f -p $PORT $USER@$HOST "chmod +x /tmp/sen-test.sh; $PASS DONE=/tmp/sen-test.done OUT=/tmp/sen-test.out /tmp/sen-test.sh >/dev/null 2>&1 </dev/null &" </dev/null
i=0
while [ $i -lt $(( ${SEN_VM_WAIT:-180} / 3 )) ]; do
	sleep 3; i=$((i+1))
	ssh -o ServerAliveInterval=5 -p $PORT $USER@$HOST 'test -f /tmp/sen-test.done' </dev/null && break
done
ssh -p $PORT $USER@$HOST 'cat /tmp/sen-test.out' </dev/null | tee /tmp/sen-test.result
! grep -q '^FAIL' /tmp/sen-test.result
