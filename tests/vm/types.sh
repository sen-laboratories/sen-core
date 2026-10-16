#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# The tests of the registry of the semantic types (TypesTest.cpp), in a Haiku VM (started by run.sh): compiles the entries of the papers ontology into the
# non-packaged data folder of the user, where the registry reads them like those of a package, and removes them again.
HEADERS=/boot/home/config/non-packaged/develop/headers
SRC=${SRC:-/Develop/SEN/sen-core/tests/vm}
ONI=${ONI:-$SRC/../../../sen-oni}
OUT=${OUT:-/tmp/sen-types.out}
DONE=${DONE:-/tmp/sen-types.done}
rm -f "$DONE" "$OUT"
{
folder=$(finddir B_USER_NONPACKAGED_DATA_DIRECTORY)/sen/ontologies/papers/types
mkdir -p "$folder"
for rdef in $ONI/ontologies/papers/types/*.rdef; do
    rc -o "$folder/$(basename "$rdef" .rdef).rsrc" "$rdef"
done
echo "== compiling the test"
g++ -std=c++20 -Wall -Wno-multichar -I$HEADERS -I${SENTO_TESTS:-/Develop/SEN/sento/tests} -o /tmp/types-test $SRC/TypesTest.cpp -lbe 2>&1 | grep -E "error|Error" | head -20
[ -x /tmp/types-test ] || { echo "FAIL the test did not compile"; touch "$DONE"; exit 1; }
echo "== type registry tests"
/tmp/types-test
echo "exit $?"
rm -rf "$(finddir B_USER_NONPACKAGED_DATA_DIRECTORY)/sen/ontologies/papers"
} > "$OUT" 2>&1
touch "$DONE"
