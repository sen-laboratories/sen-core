#!/usr/bin/env python3
# sen-graph.py — visualize SEN relations as SVG via graphviz
# usage: python3 sen-graph.py [output.svg]

import sys, subprocess, tempfile, os

def query(predicate):
    r = subprocess.run(['query', predicate], capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip()]

def catattr(attr, path):
    r = subprocess.run(['catattr', '-r', attr, path],
                       capture_output=True, text=True, timeout=2)
    return r.stdout.strip() if r.returncode == 0 else None

def main():
    outsvg = sys.argv[1] if len(sys.argv) > 1 else 'sen-graph.svg'

    print('querying SEN:ID index...')
    paths = query('SEN:ID=="*"')
    print(f'found {len(paths)} SEN nodes')

    if not paths:
        print('no results — is the SEN:ID attribute indexed?')
        sys.exit(1)

    # id -> label
    nodes = {}
    # (src_id, tgt_id) edges
    edges = []

    for path in paths:
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

    # dot output
    def safe(s):
        return s.replace('-', '_').replace('.', '_')

    lines = [
        'digraph SEN {',
        '  graph [bgcolor="#0a0a0a" fontname="Calibri"]',
        '  node  [shape=box style="filled,rounded" fillcolor="#112222"'
        '         fontcolor="#00C8C8" color="#00C8C8" fontname="Calibri" fontsize=11]',
        '  edge  [color="#5B89C8" fontcolor="#888888" fontname="Calibri" fontsize=9]',
    ]

    for sen_id, label in nodes.items():
        lines.append(f'  "{safe(sen_id)}" [label="{label.replace(chr(34), chr(39))}"]')

    for src, tgt in edges:
        if tgt not in nodes:
            lines.append(f'  "{safe(tgt)}" [label="?" style=dashed'
                         f' fillcolor="#1a0a0a" fontcolor="#444444"]')
        lines.append(f'  "{safe(src)}" -> "{safe(tgt)}"')

    lines.append('}')
    dot = '\n'.join(lines)

    with tempfile.NamedTemporaryFile('w', suffix='.dot', delete=False) as f:
        f.write(dot)
        dotfile = f.name

    subprocess.run(['dot', '-Tsvg', '-o', outsvg, dotfile], check=True)
    os.unlink(dotfile)
    print(f'written: {outsvg}')

if __name__ == '__main__':
    main()
