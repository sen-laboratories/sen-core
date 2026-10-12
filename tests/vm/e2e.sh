#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# End-to-end test of sen_server in a Haiku VM (started by run.sh, which copies it to /tmp and polls for $DONE).
# Needs the built sen_server (bin/), the installed core ontology (sen-oni), the built plugins (sensei) and a PDF in $TEST_DIR.
# Prints "ok"/"FAIL" per check and "N passed, M failed". Attributes of files are only changed in a temporary folder.

TEST_DIR=${TEST_DIR:-/Develop/test}
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

mkdir -p "$TEST_DIR"
# two small PDFs that do not depend on any test data: one with bookmarks (an outline), one without
PDFS=$TEST_DIR/_pdf; rm -rf $PDFS; mkdir -p $PDFS
cat > $PDFS/bookmarks.pdf <<'PDF'
%PDF-1.4
1 0 obj << /Type /Catalog /Pages 2 0 R /Outlines 5 0 R >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >> endobj
3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >> endobj
4 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >> endobj
5 0 obj << /Type /Outlines /First 6 0 R /Last 7 0 R /Count 2 >> endobj
6 0 obj << /Title (Chapter One) /Parent 5 0 R /Next 7 0 R /Dest [3 0 R /Fit] >> endobj
7 0 obj << /Title (Chapter Two) /Parent 5 0 R /Prev 6 0 R /Dest [4 0 R /Fit] >> endobj
trailer << /Root 1 0 R /Size 8 >>
%%EOF
PDF
cat > $PDFS/plain.pdf <<'PDF'
%PDF-1.4
1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj
3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >> endobj
trailer << /Root 1 0 R /Size 4 >>
%%EOF
PDF
PDF=$PDFS/bookmarks.pdf

echo "== server"
check "server answers the status request" "hey $SIG SCst | grep -q operational"
check "ID generation test passes (unique TSIDs)" "hey $SIG SCts with count=100 | grep -q 'testPassed.*TRUE'"

echo "== plugins and relations of a PDF"
hey $SIG SRsa with refs=$PDF > /tmp/sen-e2e.reply 2>&1
check "plugin found on any volume, config for the PDF extractor returned" "grep -q 'application/x-vnd.sen-plugin.sen-labs.pdf-extractor' /tmp/sen-e2e.reply"
check "docref relation type returned" "grep -q 'relation/x-vnd.sen-labs.relation.docref' /tmp/sen-e2e.reply"
check "relation is dynamic and contained (SEN:dynamic, SEN:self)" "grep -q 'SEN:dynamic.*TRUE' /tmp/sen-e2e.reply && grep -q 'SEN:self.*TRUE' /tmp/sen-e2e.reply"
check "page is mapped to schema:pageStart" "grep -q 'schema:pageStart' /tmp/sen-e2e.reply"
hey $SIG SRsg with refs=$PDFS/bookmarks.pdf and SEN:relationType=relation/x-vnd.sen-labs.relation.docref > /tmp/sen-e2e.bookmarks 2>&1
check "the bookmarks of a PDF are its contained relations, in order" "grep -q 'Chapter One' /tmp/sen-e2e.bookmarks && grep -q 'Chapter Two' /tmp/sen-e2e.bookmarks && grep -n 'Chapter' /tmp/sen-e2e.bookmarks | head -1 | grep -q One"
check "a bookmark knows its page" "grep -q 'schema:pageStart.*(0x00000002)' /tmp/sen-e2e.bookmarks"
hey $SIG SRsg with refs=$PDFS/plain.pdf and SEN:relationType=relation/x-vnd.sen-labs.relation.docref > /tmp/sen-e2e.plain 2>&1
check "a PDF without bookmarks has no contained relations: an empty answer, not an error" "grep -q 'status.*204' /tmp/sen-e2e.plain && grep -q '\"result\".*0x00000000' /tmp/sen-e2e.plain && ! grep -q 'SEN:relations' /tmp/sen-e2e.plain"
check "a request without refs does not crash the server" "hey $SIG SRsa; ps | grep -q '[s]en_server'"

echo "== includes of a source file"
C=$TEST_DIR/_code; rm -rf $C; mkdir -p $C
printf '#include <stdio.h>\n#include "local.h"\n// #include <commented/out.h>\n#include <no/such/header.h>\n\nint main() { return 0; }\n' > $C/sample.cpp
printf '#include <string.h>\n' > $C/local.h
hey $SIG SRsg with refs=$C/sample.cpp and SEN:relationType=relation/x-vnd.sen-labs.relation.sourcecode.include > /tmp/sen-e2e.code 2>&1
check "the includes of the file are found" "grep -q 'local.h' /tmp/sen-e2e.code && grep -q 'stdio.h' /tmp/sen-e2e.code"
check "a header is resolved to where it was found" "grep -q 'SEN:REL:includePath.*/stdio.h' /tmp/sen-e2e.code && grep -q 'includePath.*_code/local.h' /tmp/sen-e2e.code"
check "a missing header is found, as written" "grep -q 'includePath.*\"no/such/header.h\"' /tmp/sen-e2e.code"
check "angle brackets and quotes are told apart" "[ \$(grep -c 'includeGlobal.*TRUE' /tmp/sen-e2e.code) -eq 2 ] && [ \$(grep -c 'includeGlobal.*FALSE' /tmp/sen-e2e.code) -eq 1 ]"
check "the lines of the directives are given" "grep -q 'be:line.*(0x00000001)' /tmp/sen-e2e.code && grep -q 'be:line.*(0x00000004)' /tmp/sen-e2e.code"
check "comments and the includes of headers are not reported" "! grep -q 'commented/out.h' /tmp/sen-e2e.code && ! grep -q 'string.h' /tmp/sen-e2e.code"
# the harder cases: comments, switched off code, strings that look like directives, spacing, headers in the folders of the kits
printf '/* #include <block_comment.h> */\n#if 0\n#include <disabled.h>\n#endif\n  #  include <vector>\nconst char* s = "#include <in_string.h>";\nconst char* r = R"x(\n#include <in_raw.h>\n)x";\n#include <Application.h> // trailing\n#if 1\n#include <enabled.h>\n#else\n#include <else_of_one.h>\n#endif\n#include "local.h"\n' > $C/hard.cpp
hey $SIG SRsg with refs=$C/hard.cpp and SEN:relationType=relation/x-vnd.sen-labs.relation.sourcecode.include > /tmp/sen-e2e.hard 2>&1
check "includes in comments, strings, raw strings and #if 0 are not found" "! grep -q 'block_comment.h\|disabled.h\|in_string.h\|in_raw.h\|else_of_one.h' /tmp/sen-e2e.hard"
check "spaces after the # and #if 1 are understood" "grep -q 'vector' /tmp/sen-e2e.hard && grep -q 'enabled.h' /tmp/sen-e2e.hard"
check "a BeAPI header is found in the folder of its kit" "grep -q 'includePath.*headers/os/app/Application.h' /tmp/sen-e2e.hard"
check "the line of a directive after multi-line strings is right" "grep -q 'be:line.*: 10 ' /tmp/sen-e2e.hard"

echo "== imports of a Python file"
printf 'import os, sys as system\nfrom collections import (OrderedDict,\n    defaultdict)  # import in_comment\n"""import in_docstring"""\nfrom . import sibling\nfrom .pkg.mod import thing\nif True: import json\nx = "import in_string"; import re\n' > $C/pyfile.py
printf 'VALUE = 1\n' > $C/sibling.py
hey $SIG SRsg with refs=$C/pyfile.py and SEN:relationType=relation/x-vnd.sen-labs.relation.sourcecode.include > /tmp/sen-e2e.py 2>&1
check "the imports are found" "grep -q '\"os\"' /tmp/sen-e2e.py && grep -q 'collections' /tmp/sen-e2e.py && grep -q 'json' /tmp/sen-e2e.py && grep -q '\"re\"' /tmp/sen-e2e.py && grep -q 'sibling' /tmp/sen-e2e.py"
check "imports in comments, docstrings and strings are not found" "! grep -q 'in_comment\|in_docstring\|in_string' /tmp/sen-e2e.py"
check "a relative import is found in the project, one that is missing is kept as written" "grep -q 'includePath.*_code/sibling.py' /tmp/sen-e2e.py && grep -q 'includePath.*\"\\.pkg\\.mod\"' /tmp/sen-e2e.py"
check "a module of the standard library is found in the library folders of Python" "grep -q 'includePath.*python3.*collections/__init__.py' /tmp/sen-e2e.py"
check "relative imports are not global, the others are" "[ \$(grep -c 'includeGlobal.*TRUE' /tmp/sen-e2e.py) -eq 5 ] && [ \$(grep -c 'includeGlobal.*FALSE' /tmp/sen-e2e.py) -eq 2 ]"
check "the lines of the imports are given" "grep -q 'be:line.*: 5 ' /tmp/sen-e2e.py && grep -q 'be:line.*: 8 ' /tmp/sen-e2e.py"

echo "== imports of a Java file, language by the semantic type"
J=$C/jt/com/acme; mkdir -p $J/util
printf '/* import in.comment; */\npackage com.acme;\n\nimport java.util.List;\nimport com.acme.util.Helper;\nimport static com.acme.util.Helper.run;\nimport com.acme.model.*;\n// import in.line.comment;\n\npublic class App {\n    String s = "import in.string;";\n    String t = """\n        import in.textblock;\n        """;\n}\n' > $J/App.java
printf 'package com.acme.util;\npublic class Helper {}\n' > $J/util/Helper.java
hey $SIG SRsg with refs=$J/App.java and SEN:relationType=relation/x-vnd.sen-labs.relation.sourcecode.include > /tmp/sen-e2e.java 2>&1
check "the imports are found, also the class of a static import and a whole package" "grep -q 'java.util.List' /tmp/sen-e2e.java && grep -q 'com.acme.util.Helper' /tmp/sen-e2e.java && grep -q 'com.acme.model' /tmp/sen-e2e.java"
check "imports in comments, strings and text blocks are not found" "! grep -q 'in\\.comment\|in\\.line\|in\\.string\|in\\.textblock' /tmp/sen-e2e.java"
check "a class of the project is found by its package, a library class is not" "[ \$(grep -c 'includePath.*com/acme/util/Helper.java' /tmp/sen-e2e.java) -eq 2 ] && ! grep -q 'includePath.*java/util/List.java' /tmp/sen-e2e.java"
check "project classes are not global, libraries and packages are" "[ \$(grep -c 'includeGlobal.*TRUE' /tmp/sen-e2e.java) -eq 2 ] && [ \$(grep -c 'includeGlobal.*FALSE' /tmp/sen-e2e.java) -eq 2 ]"
check "the lines of the imports are given" "grep -q 'be:line.*: 4 ' /tmp/sen-e2e.java && grep -q 'be:line.*: 7 ' /tmp/sen-e2e.java"
# a C header by its name that holds Python: the semantic type decides
printf 'import json\n#include <not_c.h>\n' > $C/weird.h
# (a file with META:TYPE and without BEOS:TYPE is taken for an entry of the MIME database by Haiku: give it its type, as Tracker does)
addattr -t mime BEOS:TYPE text/x-source-code $C/weird.h
addattr -t mime META:TYPE text/x-python $C/weird.h
hey $SIG SRsg with refs=$C/weird.h and SEN:relationType=relation/x-vnd.sen-labs.relation.sourcecode.include > /tmp/sen-e2e.weird 2>&1
check "the semantic type (META:TYPE) has precedence over the extension" "grep -q 'json' /tmp/sen-e2e.weird && ! grep -q 'not_c.h' /tmp/sen-e2e.weird"
rm -rf $C $PDFS

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
