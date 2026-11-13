"""Organic decor sprites: mushrooms, ferns, flowers, grass, vines, lantern pods, crystals,
rocks, roots, floating rock. Each function returns an RGBA array (frames laid horizontally
for animated sprites)."""
import math

import numpy as np

from helpers import (bezier, col, ellipse_mask, line_pts, new, outline, outline_selout, paste,
                     path_pixels, poly_mask, put, rng, shade_mask, thick_path_mask, value_noise)

MOSS = ["#0f3b3a", "#135c50", "#1f8a6a", "#35b583"]
CY = ["#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
VI = ["#6a2bb8", "#b04cff", "#e08aff", "#f4d4ff"]
STONE_R = ["#111827", "#1d2b3a", "#2a3e4f", "#3b5466", "#577286", "#7d97a8"]
BARK_R = ["#120c18", "#221827", "#362538", "#4d3549", "#6d4d62", "#906f82"]
STEM_R = ["#1d2b3a", "#3b5466", "#577286", "#7d97a8", "#a9c4cf"]
CYANCAP_R = ["#0f3b3a", "#135c50", "#1f8a6a", "#1fb8b0", "#39f0d8", "#8ffff0"]
VIOCAP_R = ["#2d1b52", "#43286f", "#6a2bb8", "#b04cff", "#e08aff", "#f4d4ff"]
VROCK_R = ["#0d0918", "#181029", "#22173b", "#2f1f52", "#43296f", "#5c3a8f"]


# ---------------------------------------------------------------------------
# mushrooms
# ---------------------------------------------------------------------------
def mushroom_layer(w, h, cx, by, height, cap_rx, cap_ry, cap_ramp, bend=0.0, stem_r=(2.2, 1.6),
                   gill=CY[1], spots=(), cap_flat=0.35):
    """Return (stem_img, cap_img) layers (not outlined)."""
    top = (cx + bend, by - height)
    pts = bezier((cx, by), (cx - bend * 0.6, by - height * 0.5), top, n=max(8, int(height * 2)))
    radii = [stem_r[0] + (stem_r[1] - stem_r[0]) * i / (len(pts) - 1) for i in range(len(pts))]
    sm = thick_path_mask(w, h, pts, radii)
    sm[int(by):, :] = False
    stem = shade_mask(sm, STEM_R, sigma=1.0, ambient=0.5, contrast=0.9)
    ccx, ccy = top
    cm = ellipse_mask(w, h, ccx, ccy, cap_rx, cap_ry)
    yy = np.arange(h)[:, None]
    cm &= yy <= ccy + cap_ry * cap_flat
    cap = shade_mask(cm, cap_ramp, sigma=max(1.0, cap_rx / 4), ambient=0.5, contrast=1.0)
    # gills: bottom row(s) of the cap glow
    for x in range(w):
        ys = np.where(cm[:, x])[0]
        if len(ys):
            yb = ys.max()
            cap[yb, x] = col(gill)
            if yb - 1 >= ys.min() and x % 2 == 0:
                cap[yb - 1, x] = col(cap_ramp[1])
    # rim highlight on top-left
    for (sx, sy, c) in spots:
        put(cap, ccx + sx, ccy + sy, c)
    return stem, cap


def mushroom_small():
    img = new(16, 16)
    s, c = mushroom_layer(16, 16, 7.5, 16, 8, 5.6, 4.2, CYANCAP_R, bend=0.5,
                          stem_r=(1.6, 1.3), spots=[(-2, -2, CY[3]), (1, -3, CY[2]), (3, -1, CY[2])])
    paste(img, s, 0, 0)
    paste(img, c, 0, 0)
    s2, c2 = mushroom_layer(16, 16, 12.5, 16, 4, 2.6, 2.0, VIOCAP_R, bend=0.6, stem_r=(1.0, 0.9),
                            gill=VI[2], spots=[(-1, -1, VI[3])])
    paste(img, s2, 0, 0)
    paste(img, c2, 0, 0)
    outline_selout(img)
    return img


def mushroom_tall():
    w, h = 32, 48
    img = new(w, h)
    s, c = mushroom_layer(w, h, 15.5, 48, 31, 14.6, 9.5, CYANCAP_R, bend=1.8, stem_r=(3.2, 2.2),
                          spots=[(-6, -4, CY[3]), (-3, -6, CY[2]), (2, -5, CY[3]), (6, -3, CY[2]),
                                 (-9, -1, CY[2]), (9, -1, CY[3]), (0, -2, CY[2]), (-4, -1, CY[3]),
                                 (4, -7, CY[2])])
    # stem ring (annulus) below the cap
    paste(img, s, 0, 0)
    for x in range(12, 21):
        if img[25, x, 3]:
            put(img, x, 25, STEM_R[4] if x < 16 else STEM_R[2])
            put(img, x, 26, STEM_R[1])
    # glowing dots running down the stem
    for (x, y) in [(15, 30), (16, 35), (15, 40)]:
        put(img, x, y, CY[1])
    paste(img, c, 0, 0)
    # spots get a 1px soft halo of the glow colour
    for (x, y) in [(11, 13), (14, 11), (19, 12), (23, 14)]:
        put(img, x, y, CY[3])
    # moss at the foot
    for (x, y, cc) in [(11, 47, MOSS[2]), (12, 46, MOSS[3]), (13, 47, MOSS[1]), (18, 47, MOSS[2]),
                       (19, 46, MOSS[2]), (20, 47, MOSS[1]), (12, 47, MOSS[2]), (19, 47, MOSS[1])]:
        put(img, x, y, cc)
    outline_selout(img)
    return img


def mushroom_cluster():
    w, h = 32, 24
    img = new(w, h)
    layers = [
        (21.5, 24, 15, 7.2, 5.0, VIOCAP_R, -1.2, (2.0, 1.6), VI[2],
         [(-3, -2, VI[3]), (1, -3, VI[3]), (3, -1, VI[2])]),
        (10.5, 24, 11, 6.0, 4.4, CYANCAP_R, 1.0, (1.7, 1.4), CY[1],
         [(-2, -2, CY[3]), (2, -2, CY[2])]),
        (4.5, 24, 5, 3.2, 2.4, CYANCAP_R, 0.6, (1.1, 1.0), CY[1], [(-1, -1, CY[3])]),
        (28.5, 24, 4, 2.8, 2.1, VIOCAP_R, -0.6, (1.0, 0.9), VI[2], [(0, -1, VI[3])]),
        (15.5, 24, 3, 2.4, 1.8, CYANCAP_R, 0.4, (0.9, 0.8), CY[1], []),
    ]
    for (cx, by, ht, rx, ry, ramp, bend, sr, gill, spots) in layers:
        s, c = mushroom_layer(w, h, cx, by, ht, rx, ry, ramp, bend, sr, gill, spots)
        paste(img, s, 0, 0)
        paste(img, c, 0, 0)
    for x in range(1, 31):
        if not img[23, x, 3] and x % 3:
            put(img, x, 23, MOSS[1] if x % 2 else MOSS[2])
    outline_selout(img)
    return img


# ---------------------------------------------------------------------------
# ferns, flowers, grass
# ---------------------------------------------------------------------------
def frond(img, base, ctrl, tip, curl=1, leaf_every=2, tip_glow=True):
    pts = path_pixels(bezier(base, ctrl, tip, n=24))
    n = len(pts)
    for i, (x, y) in enumerate(pts):
        t = i / max(1, n - 1)
        put(img, x, y, MOSS[1] if t < 0.4 else MOSS[2])
        if 2 <= i < n - 2 and i % leaf_every == 0:
            # leaflets on both sides, perpendicular-ish
            px, py = pts[min(n - 1, i + 1)]
            dx, dy = px - x, py - y
            for s in (-1, 1):
                lx, ly = x + s * (-dy if dy else 0) - (1 if dy == 0 else 0) * 0, y + s * dx
                if dy == 0:
                    lx, ly = x, y + s
                put(img, lx, ly, MOSS[3] if s < 0 else MOSS[2])
    if tip_glow:
        x, y = pts[-1]
        put(img, x, y, CY[2])
        if n > 2:
            put(img, *pts[-2], CY[1])
            put(img, *pts[-3], CY[0])


def glow_fern():
    img = new(24, 24)
    b = (12, 23)
    frond(img, b, (5, 18), (1, 7))
    frond(img, b, (8, 12), (6, 2))
    frond(img, b, (13, 10), (14, 1))
    frond(img, b, (18, 13), (21, 4))
    frond(img, b, (19, 19), (23, 13))
    frond(img, b, (6, 21), (2, 15), leaf_every=3)
    for x in range(8, 17):
        put(img, x, 23, MOSS[1] if x % 2 else MOSS[2])
    outline_selout(img)
    return img


def glow_flowers():
    w, fw = 16, 4
    out = new(w * fw, 16)
    pulse = [VI[0], VI[1], VI[2], VI[1]]
    pulse_c = [CY[0], CY[1], CY[2], CY[1]]
    for f in range(fw):
        img = new(16, 16)
        sway = [0, 1, 1, 0][f]
        stems = [(4, 15, 4 + sway, 6, "v"), (8, 15, 9 - sway, 3, "c"), (12, 15, 12 + sway, 7, "v")]
        for (bx, by, tx, ty, kind) in stems:
            for (x, y) in path_pixels(bezier((bx, by), (bx, (by + ty) / 2), (tx, ty), 12)):
                put(img, x, y, MOSS[2] if y < 12 else MOSS[1])
            ring = pulse if kind == "v" else pulse_c
            core = ring[(f + (1 if kind == "c" else 0)) % 4]
            hi = VI[3] if kind == "v" else CY[3]
            # flower head: plus-shaped petals with bright core
            for (dx, dy) in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
                put(img, tx + dx, ty - 1 + dy, core)
            put(img, tx, ty - 1, hi)
            put(img, tx - 1, ty - 2, ring[2] if f in (1, 2) else ring[1])
        # leaves
        for (x, y, c) in [(3, 12, MOSS[3]), (5, 13, MOSS[2]), (11, 12, MOSS[3]), (13, 13, MOSS[2]),
                          (7, 14, MOSS[2]), (9, 13, MOSS[3])]:
            put(img, x, y, c)
        for x in range(2, 15):
            if not img[15, x, 3]:
                put(img, x, 15, MOSS[1])
        outline_selout(img)
        out[:, f * w:(f + 1) * w] = img
    return out


def grass_tuft():
    img = new(16, 12)
    blades = [(3, 11, 1, 4), (5, 11, 4, 1), (7, 11, 8, 3), (8, 11, 10, 0), (10, 11, 12, 4),
              (12, 11, 14, 6), (6, 11, 5, 5), (9, 11, 9, 6)]
    for i, (bx, by, tx, ty) in enumerate(blades):
        pts = path_pixels(bezier((bx, by), (bx, (by + ty) / 2), (tx, ty), 12))
        for j, (x, y) in enumerate(pts):
            t = j / max(1, len(pts) - 1)
            put(img, x, y, MOSS[1] if t < 0.3 else (MOSS[2] if t < 0.75 else MOSS[3]))
        if i == 3:
            put(img, tx, ty, CY[2])
            put(img, tx, ty + 1, CY[1])
    for x in range(3, 13):
        put(img, x, 11, MOSS[1])
    outline_selout(img)
    return img


# ---------------------------------------------------------------------------
# hanging things
# ---------------------------------------------------------------------------
def hanging_vine(h, seed, pods):
    w = 16
    img = new(w, h)
    pts = []
    for y in range(h - 1):
        x = 8 + 2.2 * math.sin(y / 7.0 + seed) * min(1.0, y / 10.0)
        pts.append((x, y))
    pix = path_pixels(pts)
    for i, (x, y) in enumerate(pix):
        put(img, x, y, MOSS[1] if i % 5 else MOSS[0])
        put(img, x + 1, y, MOSS[2] if i % 3 else MOSS[1])
    # leaves alternate sides every ~4px
    side = 1
    for y in range(3, h - 3, 4):
        x = int(round(8 + 2.2 * math.sin(y / 7.0 + seed) * min(1.0, y / 10.0)))
        if side > 0:
            leaf = [(2, 0, MOSS[3]), (3, 0, MOSS[2]), (3, 1, MOSS[2]), (4, 1, MOSS[1])]
        else:
            leaf = [(-1, 0, MOSS[3]), (-2, 0, MOSS[3]), (-2, 1, MOSS[2]), (-3, 1, MOSS[1])]
        for dx, dy, c in leaf:
            put(img, x + dx, y + dy, c)
        side = -side
    for py in pods:
        x = int(round(8 + 2.2 * math.sin(py / 7.0 + seed) * min(1.0, py / 10.0)))
        for dx, dy, c in [(0, 0, CY[1]), (1, 0, CY[0]), (0, 1, CY[0]), (-1, 0, CY[2]),
                          (0, -1, CY[2]), (-1, -1, CY[3])]:
            put(img, x + dx - 1, py + dy, c)
    # curled tip
    x, y = pix[-1]
    put(img, x + 1, y - 1, MOSS[3])
    outline_selout(img)
    img[0, :] = np.where(img[0, :, 3:4] > 0, img[0, :], 0)
    return img


def hanging_vine_short():
    return hanging_vine(32, 1, pods=[20])


def hanging_vine_long():
    return hanging_vine(64, 4, pods=[22, 46])


def lantern_pod():
    fw, h, n = 16, 32, 6
    out = new(fw * n, h)
    glow = [0.0, 0.12, 0.25, 0.32, 0.25, 0.12]
    for f in range(n):
        img = new(fw, h)
        sway = math.sin(2 * math.pi * f / n) * 1.2
        # stem from the ceiling
        stem = path_pixels(bezier((8, 0), (8, 8), (8 + sway, 14), 16))
        for (x, y) in stem:
            put(img, x, y, MOSS[1])
        for (x, y) in stem[::5]:
            put(img, x + 1, y, MOSS[2])
        cx, cy = 8 + sway, 21
        m = ellipse_mask(fw, h, cx, cy, 4.3, 5.6)
        m &= ~ellipse_mask(fw, h, cx, cy + 7.5, 2.0, 2.4)
        tone = 0.4 + glow[f]
        pod = shade_mask(m, CYANCAP_R, sigma=1.4, ambient=tone, contrast=0.9)
        # inner light core
        core = ellipse_mask(fw, h, cx - 0.5, cy + 0.5, 1.8 + glow[f] * 3, 2.4 + glow[f] * 3)
        pod[core & m] = col(CY[2])
        hot = ellipse_mask(fw, h, cx - 0.5, cy + 0.5, 0.9, 1.2)
        pod[hot & m] = col(CY[3])
        # shell ribs
        for y in range(16, 27):
            for x in range(fw):
                if m[y, x] and (abs(x - cx + 2.3) < 0.5 or abs(x - cx - 2.3) < 0.5) and not core[y, x]:
                    pod[y, x] = col(CYANCAP_R[1])
        paste(img, pod, 0, 0)
        # leafy cap
        for dx, dy, c in [(-2, -6, MOSS[2]), (-1, -6, MOSS[3]), (0, -6, MOSS[2]), (1, -6, MOSS[1]),
                          (2, -5, MOSS[1]), (-3, -5, MOSS[2]), (0, -7, MOSS[1])]:
            put(img, cx + dx, cy + dy, c)
        outline_selout(img)
        img[0, :] = 0
        put(img, 8, 0, MOSS[1])
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# crystals
# ---------------------------------------------------------------------------
def prism(w, h, bx, by, height, width, lean, ramp=VIOCAP_R):
    """One crystal prism (polygon) with three faces. Returns RGBA layer."""
    tx = bx + lean
    tip = height * 0.22 + width * 0.3
    pts = [(bx - width / 2, by), (bx + width / 2, by), (tx + width / 2, by - height + tip),
           (tx, by - height), (tx - width / 2, by - height + tip)]
    m = poly_mask(w, h, pts)
    img = new(w, h)
    for y in range(h):
        for x in range(w):
            if not m[y, x]:
                continue
            t = (by - y) / max(1, height)
            cxl = bx + lean * t
            rel = (x + 0.5 - cxl) / max(1.0, width / 2)
            if rel < -0.35:
                c = ramp[4]
            elif rel < 0.15:
                c = ramp[3]
            else:
                c = ramp[2]
            if y < by - height + tip + 1 and rel < 0.15:
                c = ramp[4] if rel >= -0.35 else ramp[5]
            if abs(rel + 0.35) < 0.18 and t > 0.25:
                c = ramp[5]
            if t < 0.12:
                c = ramp[1] if rel > 0 else ramp[2]
            img[y, x] = col(c)
    return img


def crystal_cluster():
    w, h = 24, 24
    img = new(w, h)
    for (bx, by, ht, wd, ln) in [(6, 24, 11, 5, -3), (17, 24, 13, 5, 3), (11.5, 24, 21, 7, 0.5),
                                 (14, 24, 8, 4, 1)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln), 0, 0)
    # rocky base
    for x in range(3, 21):
        put(img, x, 23, VROCK_R[3] if x % 3 else VROCK_R[4])
        if 6 < x < 17:
            put(img, x, 22, VROCK_R[3])
    outline_selout(img)
    return img


def crystal_big():
    w, h = 32, 48
    img = new(w, h)
    base = ellipse_mask(w, h, 16, 47, 13, 5)
    paste(img, shade_mask(base, VROCK_R, sigma=1.5, ambient=0.55), 0, 0)
    for (bx, by, ht, wd, ln) in [(7, 45, 18, 6, -5), (25, 45, 22, 6, 5), (11, 44, 30, 7, -2),
                                 (21, 44, 33, 7, 2), (16, 45, 46, 9, 0)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln), 0, 0)
    for (x, y) in [(16, 10), (12, 22), (21, 18)]:
        put(img, x, y, VI[3])
    outline_selout(img)
    return img


# ---------------------------------------------------------------------------
# rocks, roots, floating rock
# ---------------------------------------------------------------------------
def blob_mask(w, h, cx, cy, rx, ry, seed, rough=0.18, flat_bottom=None):
    yy, xx = np.mgrid[0:h, 0:w]
    ang = np.arctan2(yy + 0.5 - cy, xx + 0.5 - cx)
    r = rng(seed)
    ph = r.random(4) * 6.28
    rr = 1 + rough * (0.6 * np.sin(3 * ang + ph[0]) + 0.4 * np.sin(5 * ang + ph[1])
                      + 0.3 * np.sin(7 * ang + ph[2]))
    d = np.sqrt(((xx + 0.5 - cx) / rx) ** 2 + ((yy + 0.5 - cy) / ry) ** 2)
    m = d <= rr
    if flat_bottom is not None:
        m &= yy <= flat_bottom
    return m


def mossy_top(img, m, depth=2, glow_every=0, seed=0):
    h, w = m.shape
    for x in range(w):
        ys = np.where(m[:, x])[0]
        if not len(ys):
            continue
        y0 = ys.min()
        for d in range(depth):
            if y0 + d < h and m[y0 + d, x]:
                img[y0 + d, x] = col(MOSS[3] if d == 0 else (MOSS[2] if d == 1 else MOSS[1]))
        if glow_every and (x * 7 + seed) % glow_every == 0:
            img[y0, x] = col(CY[1])


def rock_small():
    w, h = 16, 12
    m = blob_mask(w, h, 8, 8, 6.6, 5.2, 11, flat_bottom=11)
    img = shade_mask(m, STONE_R, sigma=1.2, ambient=0.5, contrast=1.0, dither_strength=0.2)
    mossy_top(img, m, 1)
    put(img, 6, 6, STONE_R[5])
    outline(img)
    return img


def rock_big():
    w, h = 32, 24
    m = blob_mask(w, h, 16, 16, 14, 10.5, 23, rough=0.12, flat_bottom=23)
    m2 = blob_mask(w, h, 24, 19, 6, 5, 5, flat_bottom=23)
    m |= m2
    img = shade_mask(m, STONE_R, sigma=2.0, ambient=0.5, contrast=1.0, dither_strength=0.2)
    # crack + glowing vein
    for (x, y) in line_pts(12, 8, 16, 15):
        put(img, x, y, STONE_R[1])
    for (x, y) in line_pts(16, 15, 15, 19):
        put(img, x, y, STONE_R[1])
    for (x, y, c) in [(13, 9, CY[0]), (14, 11, CY[1]), (15, 13, CY[0])]:
        put(img, x, y, c)
    mossy_top(img, m, 2, glow_every=5, seed=2)
    outline(img)
    return img


def roots():
    w, h = 48, 24
    img = new(w, h)
    paths = [
        ((24, 0), (16, 8), (2, 22), 3.6, 1.0),
        ((25, 0), (30, 10), (46, 22), 3.4, 1.0),
        ((24, 2), (20, 14), (16, 23), 3.0, 1.3),
        ((26, 2), (29, 14), (32, 23), 2.6, 1.2),
        ((23, 4), (12, 10), (9, 20), 2.0, 0.8),
    ]
    m = np.zeros((h, w), bool)
    vein_pts = []
    for (a, b, c, r0, r1) in paths:
        pts = bezier(a, b, c, 40)
        rad = [r0 + (r1 - r0) * i / 39 for i in range(40)]
        m |= thick_path_mask(w, h, pts, rad)
        vein_pts.append(pts)
    img = shade_mask(m, BARK_R, sigma=1.4, ambient=0.48, contrast=1.1)
    # bark grain along each root + cyan veins on two of them
    for k, pts in enumerate(vein_pts):
        pix = path_pixels(pts)
        for i, (x, y) in enumerate(pix):
            if 0 <= y < h and 0 <= x < w and m[y, x]:
                if k in (0, 1) and 6 < i < len(pix) - 6 and i % 9 < 5:
                    img[y, x] = col(CY[0] if i % 9 in (0, 4) else CY[1])
                elif i % 4 == 0:
                    img[y, x] = col(BARK_R[2])
    mossy_top(img, m & (np.arange(h)[:, None] < 6), 1)
    outline(img)
    return img


def floating_rock():
    w, h = 48, 40
    # top ellipse + tapering body
    prof = value_noise(w, 1, 6, 1, seed=31, wrap_x=False)[0]
    m = np.zeros((h, w), bool)
    for x in range(w):
        dx = abs(x + 0.5 - 24) / 21.0
        if dx > 1:
            continue
        top = 8 + int(round(2.5 * dx * dx + prof[x] * 1.5))
        bot = 9 + int(round((1 - dx ** 1.4) * 22 + prof[x] * 3))
        m[top:bot, x] = True
    img = shade_mask(m, STONE_R, sigma=2.2, ambient=0.42, contrast=1.1)
    # strata lines
    for y in (16, 22, 27):
        for x in range(w):
            if m[y, x] and (x + y) % 5:
                img[y, x] = col(STONE_R[1])
                if m[y - 1, x]:
                    img[y - 1, x] = col(STONE_R[3]) if x < 24 and (x % 3 == 0) else img[y - 1, x]
    mossy_top(img, m, 3, glow_every=6, seed=1)
    # moss drapes over the rim
    for x, n in [(5, 3), (9, 2), (36, 4), (41, 2), (20, 2)]:
        ys = np.where(m[:, x])[0]
        for d in range(n):
            put(img, x, ys.min() + 3 + d, MOSS[1] if d < n - 1 else CY[0])
    # hanging roots
    for (x0, ln) in [(14, 8), (30, 6), (22, 5)]:
        ys = np.where(m[:, x0])[0]
        y0 = ys.max()
        for i in range(ln):
            put(img, x0 + (1 if i > ln // 2 else 0), y0 + i, BARK_R[3] if i % 2 else BARK_R[2])
    # glowing crystals under the rock
    for (bx, by, ht, wd, ln) in [(24, 27, 9, 4, 0), (19, 24, 6, 3, -1), (29, 24, 6, 3, 1)]:
        p = prism(w, h, bx, by, ht, wd, ln)
        p = p[::-1].copy()      # pointing down
        shift = 2 * by - h + 1
        pp = new(w, h)
        if shift >= 0:
            pp[shift:] = p[:h - shift]
        else:
            pp[:h + shift] = p[-shift:]
        paste(img, pp, 0, 0)
    # tufts on top
    for (x, c) in [(10, MOSS[3]), (11, MOSS[2]), (27, MOSS[3]), (33, MOSS[2]), (34, MOSS[3])]:
        ys = np.where(m[:, x])[0]
        put(img, x, ys.min() - 1, c)
    put(img, 27, int(np.where(m[:, 27])[0].min()) - 2, CY[2])
    outline_selout(img, strength=0.2)
    return img
