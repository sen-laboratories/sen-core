#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# End-to-end test of sen_server in a Haiku VM (started by run.sh, which copies it to /tmp and polls for $DONE).
# Needs the built sen_server (bin/), the installed core ontology (sen-oni), the built plugins (sensei) and a PDF in $TEST_DIR.
# Prints "ok"/"FAIL" per check and "N passed, M failed". Attributes of files are only changed in a temporary folder.

TEST_DIR=${TEST_DIR:-/Develop/test}
PDF=${PDF:-$TEST_DIR/bm1.pdf}
SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
SIG=application/x-vnd.sen-labs.sen-server
OUT=${OUT:-/tmp/sen-e2e.out}
DONE=${DONE:-/tmp/sen-e2e.done}
PASSED=0; FAILED=0
rm -f "$DONE" "$OUT"

check() {	# check <name> <command that succeeds when the check passes>
	if eval "$2" >/dev/null 2>&1; then echo "ok   $1"; PASSED=$((PASSED+1)); else echo "FAIL $1"; FAILED=$((FAILED+1)); fi
}

{
# a stale instance would answer instead of the one under test
for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
sleep 2
SEN_LOG_LEVEL=info $SERVER > /tmp/sen_server.log 2>&1 &
sleep 4

echo "== server"
check "server answers the status request" "hey $SIG SCst | grep -q operational"
check "ID generation test passes (unique TSIDs)" "hey $SIG SCts with count=100 | grep -q 'testPassed.*TRUE'"

echo "== plugins and relations of a PDF"
hey $SIG SRsa with refs=$PDF > /tmp/sen-e2e.reply 2>&1
check "plugin found on any volume, config for the PDF extractor returned" "grep -q 'application/x-vnd.sen-labs.PdfExtractor' /tmp/sen-e2e.reply"
check "docref relation type returned" "grep -q 'relation/x-vnd.sen-labs.relation.docref' /tmp/sen-e2e.reply"
check "relation is dynamic and contained (SEN:dynamic, SEN:self)" "grep -q 'SEN:dynamic.*TRUE' /tmp/sen-e2e.reply && grep -q 'SEN:self.*TRUE' /tmp/sen-e2e.reply"
check "page is mapped to schema:pageStart" "grep -q 'schema:pageStart' /tmp/sen-e2e.reply"
check "a request without refs does not crash the server" "hey $SIG SRsa; ps | grep -q '[s]en_server'"

echo "== copies are new objects"
D=$TEST_DIR/_sentmp; rm -rf $D; mkdir -p $D; cd $D
touch orig
addattr -t string SEN:ID 02TESTTESTTST orig
addattr -t string SEN:TO 02OTHEROTHERX orig
addattr -t string SEN:annotations keepme orig
cp -a orig copy
sleep 3
check "the original keeps its identity" "listattr orig | grep -q SEN:ID && listattr orig | grep -q SEN:TO"
check "the copy lost SEN:ID" "! listattr copy | grep -q 'SEN:ID'"
check "the copy lost the relation targets" "! listattr copy | grep -q 'SEN:TO'"
check "the copy keeps content metadata" "listattr copy | grep -q SEN:annotations"
cd /; rm -rf $D

for p in $(ps | grep "[s]en_server" | awk '{print $(NF-3)}'); do kill -9 $p; done
echo "$PASSED passed, $FAILED failed"
} > "$OUT" 2>&1
touch "$DONE"
