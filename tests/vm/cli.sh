#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The command line of the server (--version, --info, --types, --check, --help) in a Haiku VM (started by run.sh). The options print and exit,
# they need neither a running server nor start one.

SERVER=${SERVER:-/Develop/SEN/sen-core/bin/sen_server}
OUT=${OUT:-/tmp/sen-cli.out}
DONE=${DONE:-/tmp/sen-cli.done}
rm -f "$DONE" "$OUT"

pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; FAILED=$((FAILED + 1)); }
FAILED=0

# check NAME 'command' EXPECTED-EXIT PATTERN : the exit code and a line of the output
check() {
	name=$1; command=$2; expected=$3; pattern=$4
	output=$(eval "$command" 2>&1); code=$?
	[ "$code" = "$expected" ] || { fail "$name: exit $code, expected $expected"; return; }
	echo "$output" | grep -Eq -e "$pattern" || { fail "$name: no line matching '$pattern' in:
$output"; return; }
	pass "$name"
}

{
check "VersionHasNameVersionDateAndBuild" "$SERVER --version" 0 '^.+ [0-9]+\.[0-9]+\.[0-9]+.* \([0-9]{4}-[0-9]{2}-[0-9]{2}, build [0-9]+\)$'
check "InfoCountsObjectsTypesAndRelations" "$SERVER --info" 0 '^Objects +[0-9]+'
check "InfoNamesTheTypes" "$SERVER --info" 0 '^Types +[0-9]+ entity .+ [0-9]+ meta'
check "InfoNamesTheRelations" "$SERVER --info" 0 '^Relations +[0-9]+'
check "TypesShowsATreePerSupertype" "$SERVER --types" 0 '^relation  [0-9]+ types? .+ [0-9]+ files?'
check "TypesListsTheBranchesWithTheNumberOfFiles" "$SERVER --types" 0 '^(├|└)── .*  [0-9]+$'
# (0 or 1, depending on what the VM holds: what is tested is that it says what it checked)
CHECK_CODE=$($SERVER --check >/dev/null 2>&1; echo $?)
check "CheckSaysWhatItChecked" "$SERVER --check" "$CHECK_CODE" '^Checked [0-9]+ SEN objects? on [0-9]+ volumes?'
check "HelpListsTheOptions" "$SERVER --help" 0 '  --check '
check "AnUnknownOptionIsAnError" "$SERVER --nonsense" 2 "unknown option '--nonsense'"
echo "$FAILED failure(s)"
} > "$OUT" 2>&1
touch "$DONE"
