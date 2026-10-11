#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Converts a simple SVG (filled paths with solid colours, nested groups and matrix transforms; no strokes or gradients) to a Haiku Vector Icon
# (HVIF), written as the hex of an rdef resource, so that the icon of a program can be made from its SVG without Icon-O-Matic.
#
# Usage: svg2hvif.py <icon.svg> [--rdef] [--no-fit]    prints the bytes as hex (a resource "vector_icon" for an rdef with --rdef)
#
# The icon is 64x64 units. What is drawn is scaled to fill it (with a margin of 2 units, centred); with --no-fit the viewBox of the SVG is scaled to it. Every path of the SVG is a shape, in the order of the document, with its colour
# (and fill-opacity); a path of several subpaths (holes) is a shape of several paths. Curves are cubic Beziers (c, C), lines (l, L, h, H, v, V).

import re
import sys
import xml.etree.ElementTree as ET

NS = '{http://www.w3.org/2000/svg}'


def parse_transform(text):
    """a, b, c, d, e, f of the matrices in a transform attribute, multiplied (left to right)"""
    m = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', text or ''):
        v = [float(x) for x in re.split(r'[\s,]+', args.strip()) if x]
        if name == 'matrix':
            t = tuple(v)
        elif name == 'translate':
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == 'scale':
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        else:
            raise ValueError('transform not supported: ' + name)
        m = multiply(m, t)
    return m


def multiply(m, n):
    """the matrix that does n first, then m (SVG: m is the parent)"""
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return (a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D, a * E + c * F + e, b * E + d * F + f)


def apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


def style_of(element):
    style = {}
    for part in (element.get('style') or '').split(';'):
        if ':' in part:
            k, v = part.split(':', 1)
            style[k.strip()] = v.strip()
    for key in ('fill', 'fill-opacity', 'opacity'):
        if element.get(key) is not None:
            style.setdefault(key, element.get(key))
    return style


def color_of(style):
    fill = style.get('fill', '#000000')
    if fill == 'none':
        return None
    if fill.startswith('url('):
        raise ValueError('gradients are not supported: ' + fill)
    if len(fill) == 4:
        fill = '#' + ''.join(ch * 2 for ch in fill[1:])
    r, g, b = int(fill[1:3], 16), int(fill[3:5], 16), int(fill[5:7], 16)
    alpha = float(style.get('fill-opacity', 1)) * float(style.get('opacity', 1))
    return (r, g, b, max(0, min(255, round(alpha * 255))))


def tokens(d):
    return re.findall(r'[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', d)


def subpaths(d, matrix):
    """the subpaths of a path as (points, closed): a point is [x, y, xin, yin, xout, yout], in the units after the matrix"""
    toks = tokens(d)
    i = 0
    cmd = None
    x = y = 0.0
    start = (0.0, 0.0)
    result = []
    points = None

    def tx(px, py):
        return apply(matrix, px, py)

    def number():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    def new_point(px, py):
        qx, qy = tx(px, py)
        return [qx, qy, qx, qy, qx, qy]

    def finish(closed):
        nonlocal points
        if points:
            if closed and len(points) > 1 and abs(points[0][0] - points[-1][0]) < 1e-6 and abs(points[0][1] - points[-1][1]) < 1e-6:
                # the last point is the first one again: they are one point, with the handles of both
                points[0][2], points[0][3] = points[-1][2], points[-1][3]
                points.pop()
            result.append((points, closed))
        points = None

    while i < len(toks):
        if re.match(r'[A-Za-z]', toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in 'Zz':
                finish(True)
                x, y = start
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            nx, ny = number(), number()
            if rel:
                nx, ny = x + nx, y + ny
            finish(False)
            x, y = nx, ny
            start = (x, y)
            points = [new_point(x, y)]
            cmd = 'l' if rel else 'L'	# more pairs are lines
        elif c == 'L':
            nx, ny = number(), number()
            if rel:
                nx, ny = x + nx, y + ny
            x, y = nx, ny
            points.append(new_point(x, y))
        elif c == 'H':
            nx = number()
            x = x + nx if rel else nx
            points.append(new_point(x, y))
        elif c == 'V':
            ny = number()
            y = y + ny if rel else ny
            points.append(new_point(x, y))
        elif c == 'C':
            c1 = (number(), number())
            c2 = (number(), number())
            p = (number(), number())
            if rel:
                c1 = (x + c1[0], y + c1[1])
                c2 = (x + c2[0], y + c2[1])
                p = (x + p[0], y + p[1])
            out = tx(*c1)
            points[-1][4], points[-1][5] = out
            point = new_point(*p)
            point[2], point[3] = tx(*c2)
            points.append(point)
            x, y = p
        else:
            raise ValueError('path command not supported: ' + cmd)
    finish(False)
    return result


def coord(value):
    n = int(round((value + 128.0) * 102.0))
    if not 0 <= n < 0x8000:
        raise ValueError('coordinate out of range: %s' % value)
    return bytes([0x80 | (n >> 8), n & 0xFF])


def drawn_paths(element, matrix):
    """(path element, matrix) of all the visible filled paths, in the order of the document"""
    for child in element:
        tag = child.tag.replace(NS, '')
        if tag in ('defs', 'metadata', 'title') or 'namedview' in tag:
            continue
        m = multiply(matrix, parse_transform(child.get('transform')))
        if tag == 'g':
            if (child.get('style') or '').find('display:none') >= 0:
                continue
            yield from drawn_paths(child, m)
        elif tag == 'path':
            color = color_of(style_of(child))
            if color is not None and color[3] != 0:
                yield child, m, color


def convert(svg_file, fit=True):
    root = ET.parse(svg_file).getroot()
    box = [float(v) for v in re.split(r'[\s,]+', root.get('viewBox').strip())]
    scale = 64.0 / max(box[2], box[3])
    base = (scale, 0, 0, scale, -box[0] * scale, -box[1] * scale)

    if fit:
        # the bounds of what is drawn (the points and the handles of the curves)
        xs, ys = [], []
        for path, m, _ in drawn_paths(root, base):
            for points, _closed in subpaths(path.get('d'), m):
                for p in points:
                    xs += [p[0], p[2], p[4]]
                    ys += [p[1], p[3], p[5]]
        width, height = max(xs) - min(xs), max(ys) - min(ys)
        factor = 60.0 / max(width, height)
        offset_x = 32.0 - (min(xs) + max(xs)) / 2 * factor
        offset_y = 32.0 - (min(ys) + max(ys)) / 2 * factor
        base = multiply((factor, 0, 0, factor, offset_x, offset_y), base)

    styles = []		# colours
    paths = []		# (points, closed)
    shapes = []		# (style index, [path indices])

    def walk(element, matrix):
        for child in element:
            tag = child.tag.replace(NS, '')
            if tag in ('defs', 'metadata', 'title', 'namedview') or 'namedview' in tag:
                continue
            m = multiply(matrix, parse_transform(child.get('transform')))
            if tag == 'g':
                if (child.get('style') or '').find('display:none') >= 0:
                    continue
                walk(child, m)
            elif tag == 'path':
                color = color_of(style_of(child))
                if color is None or color[3] == 0:
                    continue
                if color not in styles:
                    styles.append(color)
                indices = []
                for points, closed in subpaths(child.get('d'), m):
                    if len(points) < 2:
                        continue
                    paths.append((points, closed))
                    indices.append(len(paths) - 1)
                if indices:
                    shapes.append((styles.index(color), indices))

    walk(root, base)

    out = bytearray(b'ncif')
    out.append(len(styles))
    for r, g, b, a in styles:
        out += bytes([1, r, g, b, a])	# solid colour with alpha
    out.append(len(paths))
    for points, closed in paths:
        out.append(0x02 if closed else 0x00)	# closed, curves for every point
        out.append(len(points))
        for p in points:
            for v in p:
                out += coord(v)
    out.append(len(shapes))
    for style, indices in shapes:
        out += bytes([10, style, len(indices)] + indices + [0])	# a shape of paths, no transform, no transformers
    if len(styles) > 255 or len(paths) > 255 or len(shapes) > 255:
        raise ValueError('too many styles, paths or shapes')
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = convert(sys.argv[1], fit='--no-fit' not in sys.argv)
    if '--rdef' in sys.argv:
        print('resource vector_icon array {')
        for i in range(0, len(data), 32):
            print('\t$"%s"' % data[i:i + 32].hex().upper())
        print('};')
    else:
        print(data.hex())
    return 0


if __name__ == '__main__':
    sys.exit(main())
