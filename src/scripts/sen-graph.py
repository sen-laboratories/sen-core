#!/usr/bin/env python3
# sen-graph.py — visualize SEN relations as SVG via graphviz
# usage: python3 sen-graph.py [output.svg]

import sys, subprocess, os

DOTFILE = '/tmp/sen-graph.dot'

def query(predicate):
    r = subprocess.run(['query', predicate], capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip()]

def catattr(attr, path):
    try:
        r = subprocess.run(['catattr', '-r', attr, path],
                           capture_output=True, text=True, timeout=2)
        return r.stdout.strip() if r.returncode == 0 else None
    except Exception:
        return None

def main():
    outsvg = sys.argv[1] if len(sys.argv) > 1 else '/tmp/sen-graph.svg'

    print('querying SEN:ID index...')
    paths = query('SEN:ID=="*"')
    print(f'found {len(paths)} SEN nodes')

    if not paths:
        print('no results — is the SEN:ID attribute indexed?')
        sys.exit(1)

    nodes = {}   # id -> label
    edges = []   # (src_id, tgt_id)

    for path in paths:
        if not os.path.isfile(path):
            print(f'  [?] skipping non-file: {path}')
            continue
        sen_id = catattr('SEN:ID', path)
        if not sen_id:
            continue
        fname = os.path.basename(path)
        label = catattr('META:name', path) or fname
        nodes[sen_id] = label
        print(f'  [+] {fname}  id={sen_id}')

        sen_to = catattr('SEN:TO', path)
        if sen_to:
            for tgt in (t.strip() for t in sen_to.split(',') if t.strip()):
                edges.append((sen_id, tgt))
                print(f'       -> {tgt}')

    def q(s):   # dot-safe quoting, no mangling needed
        return '"' + s.replace('"', '\\"') + '"'

    lines = [
        'digraph SEN {',
        '  graph [bgcolor="#0a0a0a" fontname="Calibri"]',
        '  node  [shape=box style="filled,rounded" fillcolor="#112222"'
        '         fontcolor="#00C8C8" color="#00C8C8" fontname="Calibri" fontsize=11]',
        '  edge  [color="#5B89C8" fontcolor="#888888" fontname="Calibri" fontsize=9]',
    ]

    for sen_id, label in nodes.items():
        lines.append(f'  {q(sen_id)} [label={q(label)}]')

    for src, tgt in edges:
        if tgt not in nodes:
            lines.append(f'  {q(tgt)} [label="?" style=dashed'
                         f' fillcolor="#1a0a0a" fontcolor="#444444"]')
        lines.append(f'  {q(src)} -> {q(tgt)}')

    lines.append('}')

    with open(DOTFILE, 'w') as f:
        f.write('\n'.join(lines))
    print(f'dot written to {DOTFILE}')

    subprocess.run(['dot', '-Tsvg', '-o', outsvg, DOTFILE], check=True)
    print(f'written: {outsvg}')

if __name__ == '__main__':
    main()
