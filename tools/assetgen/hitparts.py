#!/usr/bin/env python3
"""2.76.0: the hit parts of the bosses, drawn from their sprites (docs/HITPARTS.md).

For every boss of data/enemies (an enemy with a « boss » block: classic bosses, the gigaboss of the
campaign and of Asterion, their « échos »), every sheet it is drawn with (its own and each phase's),
every cell a clip of that sheet shows: its named parts, each a union of a few CONVEX polygons whose
vertices are pixel corners of the frame (integers, origin at the frame's top left; the game places
them as it places the frame: (vertex − pivot) × scale, mirrored when it faces left).

* profile « dragon » (data/enemies/<id>.json « parts »: {"profile": "dragon"}, Pyrrhax): head,
  neck, body, left wing (toward us), right wing (behind, when it shows), tail, legs, claws and its
  breath (the fire of the sprite), found from the alpha channel, the colours of its membranes and of
  its fire, and geodesic distances along the drawing from its snout and from its tail's tip;
* profile « regions » (« parts »: {"profile": "regions", "regions": [{"name", "poly"}, …]}): named polygons
  drawn on its idle image; each pixel of each frame goes to the first that holds it, else to the nearest
  centre (docs/HITBOX_PARTS.md);
* profile « silhouette » (every other boss): its body (« corps »), the drawing itself without the
  sparks and dust around it.

Each part is cut into convex pieces until they hug the drawing (the area of a piece's polygon at
most 1,25 × its pixels, 6 pieces per part at most), 10 vertices per piece at most. A phase sheet
whose cells have exactly the masks of the boss's own sheet only names it (« sameAs »); one whose
masks differ takes the parts of the same cell of the own sheet pixel by pixel (nearest part).

Writes assets/hitparts/<sheet>.json. Deterministic: the same sheets give the same bytes
(`--check` compares without writing; tests/test_hitparts.cpp reads them back).
Usage: python3 tools/assetgen/hitparts.py [--check] [--only <enemy id>]"""
import heapq
import json
import os
import sys
from collections import deque

import numpy as np
from PIL import Image

ROOT = os.environ.get('COSMIC_ROOT') or os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'assets', 'hitparts')
if '--out' in sys.argv:  # a trial elsewhere (the comparison of --check stays on assets/hitparts)
    OUT = os.path.abspath(sys.argv[sys.argv.index('--out') + 1])
ALPHA = 100
FILL = 1.25        # a piece's polygon may be 25 % larger than its pixels, else it is cut again
MAX_PIECES = 6     # per part
MAX_VERTICES = 10  # per piece
DRAGON = ['tête', 'cou', 'corps', 'aile gauche', 'aile droite', 'queue', 'pattes', 'griffes', 'souffle']
SILHOUETTE = ['corps']


# ------------------------------------------------------------------ pixel sets as runs
def runs_of(mask):
    """Rows of horizontal runs [(y, x0, x1 exclusive)], top to bottom, left to right."""
    out = []
    for y in range(mask.shape[0]):
        row = mask[y]
        if not row.any():
            continue
        d = np.diff(np.concatenate(([0], row.view(np.int8), [0])))
        starts = np.flatnonzero(d == 1)
        ends = np.flatnonzero(d == -1)
        out.extend((y, int(a), int(b)) for a, b in zip(starts, ends))
    return out


def components(mask):
    """8-connected pieces of a mask, largest first (ties: the topmost, then leftmost), as masks."""
    runs = runs_of(mask)
    parent = list(range(len(runs)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    by_row = {}
    for i, (y, a, b) in enumerate(runs):
        by_row.setdefault(y, []).append(i)
    for i, (y, a, b) in enumerate(runs):
        for j in by_row.get(y - 1, []):
            _, c, d = runs[j]
            if c <= b and a <= d:  # 8-connected: touching diagonally counts
                ri, rj = find(i), find(j)
                if ri != rj:
                    parent[max(ri, rj)] = min(ri, rj)
    groups = {}
    for i in range(len(runs)):
        groups.setdefault(find(i), []).append(runs[i])
    out = []
    for g in groups.values():
        m = np.zeros_like(mask)
        for y, a, b in g:
            m[y, a:b] = True
        out.append((int(m.sum()), g[0][0], g[0][1], m))
    out.sort(key=lambda t: (-t[0], t[1], t[2]))
    return [t[3] for t in out]


# ------------------------------------------------------------------ convex pieces
def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


def area(poly):
    s = 0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return abs(s) / 2.0


def simplify(poly):
    """At most MAX_VERTICES: removes the vertex whose corner holds the least area (Visvalingam)."""
    poly = list(poly)
    while len(poly) > MAX_VERTICES:
        best, bi = None, 0
        for i in range(len(poly)):
            a = area([poly[i - 1], poly[i], poly[(i + 1) % len(poly)]])
            if best is None or a < best:
                best, bi = a, i
        poly.pop(bi)
    return poly


def mask_hull(m):
    pts = []
    for y, a, b in runs_of(m):
        pts += [(a, y), (b, y), (a, y + 1), (b, y + 1)]
    return hull(pts)


def pieces(m):
    """The mask as convex pieces hugging it: the piece with the most empty space is cut in two along its
    longest axis through its centre, until each is full enough or the part has MAX_PIECES."""
    todo = []
    out = []
    order = 0

    def push(pm):
        nonlocal order
        n = int(pm.sum())
        if n < 4:
            return
        h = mask_hull(pm)
        if len(h) < 3:
            return
        empty = area(h) - n
        order += 1
        heapq.heappush(todo, (-empty, order, pm, h, n))

    for c in components(m):
        push(c)
    while todo:
        neg, _, pm, h, n = heapq.heappop(todo)
        if area(h) <= FILL * n or len(out) + len(todo) + 1 >= MAX_PIECES or n < 40:
            out.append(h)
            continue
        # Integers only (the same bytes with any numpy): across its longer side, at its median pixel.
        ys, xs = np.nonzero(pm)
        coord = xs if int(xs.max()) - int(xs.min()) >= int(ys.max()) - int(ys.min()) else ys
        cut = int(np.sort(coord)[len(coord) // 2])
        left = coord < cut
        if not left.any() or left.all():
            left = coord <= cut
        a = np.zeros_like(pm)
        b = np.zeros_like(pm)
        a[ys[left], xs[left]] = True
        b[ys[~left], xs[~left]] = True
        if not a.any() or not b.any():
            out.append(h)
            continue
        for half in (a, b):
            for c in components(half):
                push(c)
    out = [simplify(p) for p in out]
    out.sort(key=lambda p: (min(q[1] for q in p), min(q[0] for q in p)))
    return out


# ------------------------------------------------------------------ profiles
def body_of(mask):
    """The drawing without the sparks, dust and embers around it: its largest piece, and the pieces of
    at least 2 % of it touching its frame's box."""
    comps = components(mask)
    if not comps:
        return mask
    main = comps[0].copy()
    n = main.sum()
    ys, xs = np.nonzero(main)
    y0, y1, x0, x1 = ys.min() - 4, ys.max() + 4, xs.min() - 4, xs.max() + 4
    for c in comps[1:]:
        if c.sum() < 0.02 * n:
            continue
        cy, cx = np.nonzero(c)
        if cy.min() >= y0 and cy.max() <= y1 and cx.min() >= x0 and cx.max() <= x1:
            main |= c
    return main


def silhouette(rgba):
    mask = rgba[:, :, 3] > ALPHA
    return {'corps': body_of(mask)}


def geodesic(pix, start):
    d = {start: 0}
    q = deque([start])
    while q:
        c = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (c[0] + dx, c[1] + dy)
            if n in pix and n not in d:
                d[n] = d[c] + 1
                q.append(n)
    return d


def dilate(m, r):
    out = m.copy()
    for _ in range(r):
        o = out.copy()
        o[1:, :] |= out[:-1, :]
        o[:-1, :] |= out[1:, :]
        o[:, 1:] |= out[:, :-1]
        o[:, :-1] |= out[:, 1:]
        out = o
    return out


def erode(m, r):
    return ~dilate(~m, r)


def dragon(rgba, pivot):
    """Pyrrhax (a dragon in profile facing right): its parts from the alpha channel, its colours and
    geodesic distances along the drawing."""
    r, g, b, a = (rgba[:, :, k].astype(int) for k in range(4))
    mask = a > ALPHA
    hot = mask & (((r > 150) & (b < 150) & (r - b > 80)) | ((r >= 245) & (g >= 200) & (b >= 170)) | ((r > 70) & (g * 2 < r) & (b * 3 < r)))
    membrane = mask & ~hot & (r - g >= 8) & (b - r <= 18) & (r < 120)
    wing = erode(dilate(membrane, 2), 2) & mask
    px0 = int(round(pivot[0]))
    wings = [c for c in components(wing) if c.sum() > 150 and int(np.nonzero(c)[1].sum()) < (px0 + 30) * int(c.sum())]
    wingmask = np.zeros_like(mask)
    for c in wings[:2]:
        wingmask |= c
    comps = components(mask & ~wingmask)
    if not comps:
        return {}
    main = comps[0]
    loose = comps[1:]
    plain = main & ~hot
    ys, xs = np.nonzero(plain)
    k = np.lexsort((ys, -xs))[0]
    snout = (int(xs[k]), int(ys[k]))
    fire = np.zeros_like(mask)
    for c in components(hot & main) + [c for c in loose if c.sum() > 6 and (c & hot).sum() * 2 > c.sum()]:
        cy, cx = np.nonzero(c)
        if c.sum() > 10 and cx.max() > snout[0] and int(cx.sum()) > (snout[0] - 20) * int(c.sum()):
            fire |= c
    body = main & ~fire
    ys, xs = np.nonzero(body & ~hot)
    k = np.lexsort((ys, -xs))[0]
    snout = (int(xs[k]), int(ys[k]))
    ys, xs = np.nonzero(body)
    k = np.lexsort((ys, xs))[0]
    tip = (int(xs[k]), int(ys[k]))
    pix = set(zip(xs.tolist(), ys.tolist()))
    ds, dt = geodesic(pix, snout), geodesic(pix, tip)
    lab = {}
    for p in pix:
        s = ds.get(p, 10 ** 6)
        t = dt.get(p, 10 ** 6)
        lab[p] = 'tête' if s <= 44 else 'cou' if s <= 78 else 'queue' if t <= 100 else 'reste'
    rest = np.zeros_like(mask)
    for (x, y), l in lab.items():
        if l == 'reste':
            rest[y, x] = True
    core = dilate(erode(rest, 6), 6) & rest
    cys = np.nonzero(core)[0]
    legs = np.zeros_like(mask)
    claws = np.zeros_like(mask)
    for c in components(rest & ~core):
        yy, xx = np.nonzero(c)
        if c.sum() < 25 or (len(cys) and int(yy.sum()) * len(cys) < int(cys.sum()) * len(yy)):
            core |= c
            continue
        bottom = yy.max()
        claws[yy[yy >= bottom - 7], xx[yy >= bottom - 7]] = True
        legs[yy[yy < bottom - 7], xx[yy < bottom - 7]] = True
    parts = {name: np.zeros_like(mask) for name in DRAGON}
    for (x, y), l in lab.items():
        if l in ('tête', 'cou', 'queue'):
            parts[l][y, x] = True
    parts['corps'] = core
    parts['pattes'] = legs
    parts['griffes'] = claws
    if wings:
        parts['aile gauche'] = wings[0]
    if len(wings) > 1:
        parts['aile droite'] = wings[1]
    parts['souffle'] = fire
    return parts


def regions(rgba, regs):
    """Profile « regions » (data/enemies/<id>.json « parts »: {"profile": "regions", "regions": [{"name": ...,
    "poly": [x0, y0, x1, y1, …]}, …]}, polygons in the frame's pixels, drawn on its idle image): each pixel of
    the drawing goes to the first region that holds it, else to the nearest region's centre (integers)."""
    from PIL import ImageDraw
    body = body_of(rgba[:, :, 3] > ALPHA)
    h, w = body.shape
    label = np.full((h, w), -1, dtype=np.int32)
    centres = []
    for i, r in enumerate(regs):
        pts = [(int(r['poly'][k]), int(r['poly'][k + 1])) for k in range(0, len(r['poly']) - 1, 2)]
        im = Image.new('L', (w, h), 0)
        ImageDraw.Draw(im).polygon(pts, fill=255)
        m = np.array(im) > 0
        label[m & body & (label < 0)] = i
        cx = sum(p[0] for p in pts) // max(1, len(pts))
        cy = sum(p[1] for p in pts) // max(1, len(pts))
        centres.append((cx, cy))
    ys, xs = np.nonzero(body & (label < 0))
    if len(ys) and centres:
        d = np.stack([(xs - cx) ** 2 + (ys - cy) ** 2 for cx, cy in centres])
        label[ys, xs] = np.argmin(d, axis=0)
    return {r['name']: body & (label == i) for i, r in enumerate(regs)}


def transfer(base_parts, mask, names):
    """A cell whose mask differs from the own sheet's: each pixel takes the part of the nearest pixel
    of the same cell of the own sheet (6 px at most; beyond, a spark: left out)."""
    label = np.full(mask.shape, -1, dtype=np.int16)
    for i, n in enumerate(names):
        if n in base_parts:
            label[base_parts[n]] = i
    for _ in range(6):
        grown = label.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.full_like(label, -1)
            ys = slice(max(dy, 0), label.shape[0] + min(dy, 0))
            yd = slice(max(-dy, 0), label.shape[0] + min(-dy, 0))
            xs = slice(max(dx, 0), label.shape[1] + min(dx, 0))
            xd = slice(max(-dx, 0), label.shape[1] + min(-dx, 0))
            sh[ys, xs] = label[yd, xd]
            take = (grown < 0) & (sh >= 0)
            grown[take] = sh[take]
        label = grown
    return {n: mask & (label == i) for i, n in enumerate(names)}


# ------------------------------------------------------------------ sheets
def cells_of(anim):
    out = set()
    for c in anim['clips'].values():
        for i in range(max(1, c.get('count', 1))):
            out.add((c.get('row', 0), c.get('start', 0) + i))
    return sorted(out)


def load_sheet(rel):
    a = json.load(open(os.path.join(ROOT, 'assets', rel), encoding='utf-8'))
    a.setdefault('pivot', [a['frameWidth'] * 0.5, a['frameHeight']])
    img = np.array(Image.open(os.path.join(ROOT, 'assets', a['texture'])).convert('RGBA'))
    return a, img


def cell(img, a, rc):
    r, c = rc
    fw, fh = a['frameWidth'], a['frameHeight']
    out = np.zeros((fh, fw, 4), dtype=img.dtype)
    part = img[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw]
    out[:part.shape[0], :part.shape[1]] = part
    return out


def sheet_name(rel):
    return os.path.splitext(os.path.basename(rel))[0]


def encode(rel, a, profile, names, cells, same=None):
    lines = ['{', '  "format": "cosmic.hitparts",', '  "version": 1,', f'  "sheet": {json.dumps(rel)},', f'  "profile": {json.dumps(profile)},',
             f'  "frameWidth": {a["frameWidth"]},', f'  "frameHeight": {a["frameHeight"]},',
             f'  "pivot": {json.dumps([round(float(v), 2) for v in a["pivot"]])},']
    if same:
        lines.append(f'  "sameAs": {json.dumps(same)}')
        lines.append('}')
        return '\n'.join(lines) + '\n'
    lines.append('  "parts": ' + json.dumps(names, ensure_ascii=False) + ',')
    lines.append('  "cells": {')
    body = []
    for rc, parts in cells:
        items = []
        for i, n in enumerate(names):
            for poly in parts.get(n, []):
                items.append([i, [v for p in poly for v in p]])
        body.append(f'    "{rc[0]},{rc[1]}": ' + json.dumps(items, separators=(',', ':')))
    lines.append(',\n'.join(body))
    lines.append('  }')
    lines.append('}')
    return '\n'.join(lines) + '\n'


def bosses(only=None):
    out = []
    for f in sorted(os.listdir(os.path.join(ROOT, 'data', 'enemies'))):
        if not f.endswith('.json'):
            continue
        d = json.load(open(os.path.join(ROOT, 'data', 'enemies', f), encoding='utf-8'))
        if 'boss' not in d or (only and d['id'] != only):
            continue
        sheets = [d['animation']]
        for p in d['boss'].get('phases', []):
            if p.get('animation') and p['animation'] not in sheets:
                sheets.append(p['animation'])
        profile = d.get('parts', {}).get('profile') or 'silhouette'
        out.append((d['id'], profile, sheets, d.get('parts', {}).get('regions', [])))
    return out


def build(only=None):
    """{relative output path: text} for every sheet of every boss (a sheet shared by several bosses
    once, with the profile of the first that names one other than the silhouette)."""
    plan = {}
    for eid, profile, sheets, regs in bosses(only):
        own = sheets[0]
        for rel in sheets:
            cur = plan.get(rel)
            if cur is None or (cur[0] == 'silhouette' and profile != 'silhouette'):
                plan[rel] = (profile, own if rel != own else None, regs)
    out = {}
    cache = {}
    names_of = {'dragon': DRAGON, 'silhouette': SILHOUETTE}
    for rel in sorted(plan, key=lambda r: (plan[r][1] is not None, r)):
        profile, own, regs = plan[rel]
        a, img = load_sheet(rel)
        names = [r['name'] for r in regs] if profile == 'regions' else names_of[profile]
        cells = cells_of(a)
        base = cache.get(own) if own else None
        if base and base[1] == cells and all(np.array_equal(cell(img, a, rc)[:, :, 3] > ALPHA, base[2][rc]) for rc in cells):
            text = encode(rel, a, profile, names, [], same=os.path.basename(own))
        else:
            res = []
            masks = {}
            labels = {}
            for rc in cells:
                rgba = cell(img, a, rc)
                masks[rc] = rgba[:, :, 3] > ALPHA
                if base and rc in base[3] and profile == 'dragon':
                    parts = transfer(base[3][rc], masks[rc], names)
                elif profile == 'dragon':
                    parts = dragon(rgba, a['pivot'])
                elif profile == 'regions':
                    parts = regions(rgba, regs)
                else:
                    parts = silhouette(rgba)
                labels[rc] = parts
                res.append((rc, {n: pieces(m) for n, m in parts.items() if m.any()}))
            cache[rel] = (a, cells, masks, labels)
            text = encode(rel, a, profile, names, res)
        out[os.path.join('assets', 'hitparts', sheet_name(rel) + '.json')] = text
    return out


def main():
    check = '--check' in sys.argv
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    files = build(only)
    stale = []
    for rel, text in sorted(files.items()):
        path = os.path.join(OUT, os.path.basename(rel))
        old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
        if old != text:
            stale.append(rel)
            if not check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                open(path, 'w', encoding='utf-8', newline='\n').write(text)
    if not only:
        known = {os.path.basename(r) for r in files}
        for f in sorted(os.listdir(OUT)) if os.path.isdir(OUT) else []:
            if f.endswith('.json') and f not in known:
                stale.append(os.path.join('assets', 'hitparts', f))
                if not check:
                    os.remove(os.path.join(OUT, f))
    size = sum(len(t.encode('utf-8')) for t in files.values())
    if check:
        if stale:
            print('Parties de frappe à régénérer (python3 tools/assetgen/hitparts.py) :')
            for s in stale:
                print('  ' + s)
            sys.exit(1)
        print(f'assets/hitparts : {len(files)} feuilles à jour ({size // 1024} Ko).')
    else:
        print(f'assets/hitparts : {len(files)} feuilles, {len(stale)} écrites ({size // 1024} Ko).')


if __name__ == '__main__':
    main()
