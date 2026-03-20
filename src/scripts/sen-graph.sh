#!/bin/bash
# sen-graph.sh — visualize SEN relations as SVG via graphviz
# usage: sen-graph.sh [root-path] [output.svg]

OUTSVG="${2:-sen-graph.svg}"
DOTFILE=$(mktemp /tmp/sen-XXXXXX.dot)

echo "querying SEN:ID index..."

{
    echo 'digraph SEN {'
    echo '  graph [bgcolor="#0a0a0a" fontname="Calibri" overlap=false splines=true]'
    echo '  node  [shape=box style="filled,rounded" fillcolor="#112222"'
    echo '         fontcolor="#00C8C8" color="#00C8C8" fontname="Calibri" fontsize=11]'
    echo '  edge  [color="#5B89C8" fontcolor="#888888" fontname="Calibri" fontsize=9]'

    query 'SEN:ID=="*"' | while IFS= read -r path; do
        [ -z "$path" ] && continue

        sen_id=$(catattr -r SEN:ID "$path" 2>/dev/null)
        [ -z "$sen_id" ] && continue

        # basename works even for metadata-only files
        fname=$(basename "$path")
        label=$(catattr -r META:name "$path" 2>/dev/null)
        [ -z "$label" ] && label="$fname"
        label="${label//\"/\'}"   # escape quotes

        echo "  [+] $fname  id=$sen_id" >&2
        echo "  \"$sen_id\" [label=\"$label\"]"

        sen_to=$(catattr -r SEN:TO "$path" 2>/dev/null)
        [ -z "$sen_to" ] && continue

        IFS=',' read -ra targets <<< "$sen_to"
        for tgt in "${targets[@]}"; do
            tgt="${tgt// /}"
            [ -z "$tgt" ] && continue
            echo "       -> $tgt" >&2
            echo "  \"$sen_id\" -> \"$tgt\""
        done
    done

    echo '}'
} > "$DOTFILE"

echo "generating $OUTSVG..."
# fdp gives force-directed layout, much better than flat dot for graphs
fdp -Tsvg -o "$OUTSVG" "$DOTFILE" && rm "$DOTFILE"
echo "done: $OUTSVG"
