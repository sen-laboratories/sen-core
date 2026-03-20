#!/usr/bin/env python3
# sen-graph.py — visualize SEN relations as SVG via graphviz
# usage: python3 sen-graph.py <root-dir> [output.svg]

import os, sys, subprocess, tempfile

SKIP_DIRS = {'/boot/system', '/boot/home/config', '/dev', '/pipe',
             '/proc', '/tmp', '/bin', '/sbin'}

def catattr(attr, path):
    try:
        r = subprocess.run(['catattr', '-r', attr, path],
                           capture_output=True, text=True, timeout=2)
        return r.stdout.strip() if r.returncode == 0 else None
    except Exception:
        return None

def collect_nodes(root):
    nodes = {}
    for dirpath, dirs, files in os.walk(root):
        # prune dirs in-place to avoid deep/system trees
        dirs[:] = [d for d in dirs
                   if os.path.join(dirpath, d) not in SKIP_DIRS
                   and not d.startswith('.')]

        for fname in files:
            path = os.path.join(dirpath, fname)
            print(f'  checking {path}', end='\r', flush=True)
            sen_id = catattr('SEN:ID', path)
            if not sen_id:
                continue
            sen_to  = catattr('SEN:TO',    path) or ''
            label   = catattr('META:name', path) or fname
            print(f'  [SEN] {path}  id={sen_id}  to={sen_to or "-"}')
            nodes[sen_id] = {
                'name':  fname,
                'label': label,
                'path':  path,
                'to':    [t.strip() for t in sen_to.split(',') if t.strip()],
            }
    print()  # clear \r line
    return nodes

def to_dot(nodes):
    lines = ['digraph SEN {',
             '  graph [bgcolor="#0a0a0a" fontname="Calibri"]',
             '  node  [shape=box style="filled,rounded" fillcolor="#112222"'
             '         fontcolor="#00C8C8" color="#00C8C8" fontname="Calibri" fontsize=11]',
             '  edge  [color="#5B89C8" fontcolor="#888888" fontname="Calibri" fontsize=9]']

    for sen_id, node in nodes.items():
        safe_id = sen_id.replace('-', '_').replace('.', '_')
        lbl = node['label'].replace('"', '\\"')
        lines.append(f'  "{safe_id}" [label="{lbl}"]')

    for sen_id, node in nodes.items():
        safe_src = sen_id.replace('-', '_').replace('.', '_')
        for target_id in node['to']:
            if target_id in nodes:
                safe_tgt = target_id.replace('-', '_').replace('.', '_')
                lines.append(f'  "{safe_src}" -> "{safe_tgt}"')
            else:
                safe_tgt = target_id.replace('-', '_').replace('.', '_')
                lines.append(f'  "{safe_tgt}" [label="?" style=dashed'
                             f' fillcolor="#1a0a0a" fontcolor="#444444"]')
                lines.append(f'  "{safe_src}" -> "{safe_tgt}"')

    lines.append('}')
    return '\n'.join(lines)

def main():
    root   = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~')
    outsvg = sys.argv[2] if len(sys.argv) > 2 else 'sen-graph.svg'

    print(f'scanning {root}...')
    nodes = collect_nodes(root)
    print(f'found {len(nodes)} SEN nodes')

    if not nodes:
        print('no SEN:ID attributes found — is this a BFS volume?')
        sys.exit(1)

    dot = to_dot(nodes)

    with tempfile.NamedTemporaryFile('w', suffix='.dot', delete=False) as f:
        f.write(dot)
        dotfile = f.name

    subprocess.run(['dot', '-Tsvg', '-o', outsvg, dotfile], check=True)
    os.unlink(dotfile)
    print(f'written: {outsvg}')

if __name__ == '__main__':
    main()
