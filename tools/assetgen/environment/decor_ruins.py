"""Ruin / tech decor sprites: pillars, arch, statue head, tech panel, holo glyph, broken pipe,
torn banner, ancient bones."""
import math

import numpy as np

from helpers import (P, bezier, col, ellipse_mask, line_pts, new, outline, paste, path_pixels,
                     poly_mask, put, rect, shade_mask, thick_path_mask, lightness)
from sprites import stamp

O = P.OUTLINE
MOSS = ["#0f3b3a", "#135c50", "#1f8a6a", "#35b583"]
CY = ["#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
VI = ["#6a2bb8", "#b04cff", "#e08aff", "#f4d4ff"]
AM = ["#c8741f", "#ffb347", "#ffd98a"]
STONE_R = ["#111827", "#1d2b3a", "#2a3e4f", "#3b5466", "#577286", "#7d97a8"]
METAL_R = ["#0e131d", "#1a2230", "#2c3846", "#45556a", "#66798f", "#9fb3c8"]
BONE_R = ["#22303f", "#4f6478", "#7d93a6", "#a3b6c6", "#c3d0de"]
CLOTH_R = ["#121a33", "#1a2747", "#22365a", "#2e4a73", "#3e6290"]


def ramp_px(ramp, i):
    return col(ramp[max(0, min(len(ramp) - 1, int(i)))])


def cyl_tone(x, x0, x1, lo=1.2, hi=4.6):
    """Ramp index across a vertical cylinder lit from the left."""
    u = (x + 0.5 - x0) / max(1.0, (x1 - x0 + 1))
    # light peaks at ~30% from the left edge, falls to the right
    v = math.cos((u - 0.3) * math.pi * 0.95)
    return lo + (hi - lo) * max(0.0, v) if u > 0.3 else hi - (hi - lo) * 0.25 * (0.3 - u) / 0.3


def dither_idx(f, x, y, band=0.18):
    """Round f to an index; only values within `band` of the midpoint get a checker dither,
    so flat areas stay clean and only transitions are dithered."""
    lo = math.floor(f)
    fr = f - lo
    if fr < 0.5 - band:
        return int(lo)
    if fr > 0.5 + band:
        return int(lo) + 1
    return int(lo) + ((x + y) & 1)


# ---------------------------------------------------------------------------
# pillars
# ---------------------------------------------------------------------------
def _pillar(h, top_profile=None, runes=True, seed=0):
    w = 24
    img = new(w, h)
    plinth_top = h - 8
    shaft = (5, 18)
    cap_rows = (5, 11)

    def put_i(x, y, f):
        img[y, x] = ramp_px(STONE_R, dither_idx(f, x, y))

    # plinth
    for y in range(plinth_top, h):
        x0, x1 = (2, 21) if y < plinth_top + 3 else (1, 22)
        for x in range(x0, x1 + 1):
            f = cyl_tone(x, x0, x1, 1.0, 3.9)
            if y in (plinth_top, plinth_top + 3):
                f += 1.0
            if y == h - 1:
                f -= 1.0
            put_i(x, y, f)
    # shaft
    for x in range(shaft[0], shaft[1] + 1):
        ytop = cap_rows[1] + 1 if top_profile is None else top_profile[x - shaft[0]]
        for y in range(ytop, plinth_top):
            f = cyl_tone(x, shaft[0], shaft[1], 1.0, 4.2)
            if (x - shaft[0]) % 3 == 2:
                f -= 1.2           # fluting groove
            elif (x - shaft[0]) % 3 == 0:
                f += 0.4
            if y in (24, 38) and y > ytop:
                f = 1.0
            elif y in (25, 39) and y > ytop + 1:
                f += 0.8
            if y == plinth_top - 1:
                f -= 0.8
            if top_profile is not None and y == ytop:
                f = 4.6 if x < 14 else 3.4  # broken surface catches light
            put_i(x, y, f)
    # capital
    if top_profile is None:
        for y in range(cap_rows[0], cap_rows[1] + 1):
            x0, x1 = (2, 21) if y <= 7 else (3, 20)
            if y == cap_rows[1]:
                x0, x1 = 4, 19
            for x in range(x0, x1 + 1):
                f = cyl_tone(x, x0, x1, 1.0, 4.2)
                if y == cap_rows[0]:
                    f += 1.0
                if y == 7:
                    f -= 1.5
                put_i(x, y, f)
        # moss blanket on top of the capital
        for x in range(2, 22):
            put(img, x, 4, MOSS[3] if x % 5 else CY[1])
            if x < 15 or x % 2:
                put(img, x, 5, MOSS[2])
        for x, n in [(2, 4), (3, 2), (8, 2), (17, 3), (21, 2)]:
            for d in range(n):
                put(img, x, 6 + d, MOSS[1] if d < n - 1 else CY[0])
        for x in (5, 6, 13, 19):
            put(img, x, 3, MOSS[2])
        put(img, 13, 2, CY[2])
    else:
        for x in range(shaft[0], shaft[1] + 1):
            yt = top_profile[x - shaft[0]]
            if x % 3 != 1:
                put(img, x, yt, MOSS[3] if x < 14 else MOSS[2])
            if x % 4 == 0:
                put(img, x, yt + 1, MOSS[1])
        put(img, 8, top_profile[3] - 1, MOSS[2])
        put(img, 9, top_profile[4] - 1, CY[1])
    # glowing rune band
    if runes:
        ry = 30 if h > 50 else max(top_profile) + 6 if top_profile is not None else 20
        stamp(img, ["c.c.C..c",
                    "CcC.cwc.",
                    "c.c.C..c"], {"c": CY[0], "C": CY[1], "w": CY[2]}, 6, ry)
    # cracks
    k = h / 64.0
    for (x, y) in line_pts(15, 42 * k, 13, 47 * k) + line_pts(13, 47 * k, 14, 50 * k):
        if 0 <= y < h and img[y, x, 3]:
            put(img, x, y, STONE_R[1])
    # vine wrapping the shaft
    y0 = 14 if top_profile is None else max(top_profile) + 2
    vine = path_pixels(bezier((19, y0), (4, (y0 + plinth_top) / 2), (19, plinth_top - 6), 30))
    for i, (x, y) in enumerate(vine):
        if img[y, x, 3] and (x <= 12 or i % 2 == 0):
            put(img, x, y, MOSS[2] if i % 3 else MOSS[1])
            if i % 6 == 0:
                put(img, x - 1, y, MOSS[3])
    outline(img)
    return img


def ruin_pillar():
    return _pillar(64)


def ruin_pillar_broken():
    prof = [14, 12, 9, 7, 8, 10, 13, 11, 9, 12, 15, 17, 16, 18]
    img = _pillar(48, top_profile=prof)
    # rubble on the plinth
    for (x, y, c) in [(1, 39, STONE_R[3]), (2, 39, STONE_R[2]), (2, 38, STONE_R[4]),
                      (20, 39, STONE_R[3]), (21, 39, STONE_R[2]), (21, 38, STONE_R[3])]:
        put(img, x, y, c)
    outline(img)
    return img


# ---------------------------------------------------------------------------
# arch
# ---------------------------------------------------------------------------
def ruin_arch():
    w, h = 80, 64
    cx, cy, R, r = 40.0, 40.0, 39.5, 26.5
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    dist = np.sqrt(dx * dx + dy * dy)
    ring = (dist <= R) & (dist >= r) & (yy + 0.5 <= cy)
    legs = (yy + 0.5 > cy) & (((xx >= 1) & (xx <= 13)) | ((xx >= 66) & (xx <= 78)))
    base = (yy >= 58) & (((xx >= 0) & (xx <= 15)) | ((xx >= 64) & (xx <= 79)))
    m = ring | legs | base
    lit = lightness(m, sigma=2.0)
    img = new(w, h)
    ang = np.degrees(np.arctan2(-dy, dx))      # 0 = right, 90 = up
    for y in range(h):
        for x in range(w):
            if not m[y, x]:
                continue
            f = 2.9 + 1.5 * lit[y, x]
            if ring[y, x]:
                a = ang[y, x]
                seg = (a + 10) / 20.0         # voussoirs every 20 degrees, keystone centred at 90
                fr = seg - math.floor(seg)
                if fr < 0.06:
                    f = 1.0
                elif fr < 0.14:
                    f += 0.8
                if dist[y, x] < r + 2.2:
                    f -= 1.0                   # intrados in shadow
                if dist[y, x] > R - 1.5:
                    f += 0.6
            else:
                row = (y - 40) % 8
                xo = 0 if ((y - 40) // 8) % 2 == 0 else 4
                if row == 7:
                    f = 1.0
                elif row == 0:
                    f += 0.7
                if (x + xo) % 8 == 0 and not base[y, x]:
                    f = 1.2
                if base[y, x]:
                    f = 2.9 + 1.2 * lit[y, x] + (1.0 if y == 58 else 0)
            img[y, x] = ramp_px(STONE_R, dither_idx(f, x, y, 0.08))
    # keystone with glowing rune
    for y in range(0, 14):
        for x in range(34, 46):
            if ring[y, x] and abs(ang[y, x] - 90) < 9:
                img[y, x] = ramp_px(STONE_R, dither_idx(3.3 + 1.2 * lit[y, x], x, y))
    stamp(img, [".ccc.", "cwCwc", ".cWc.", "..c.."], {"c": CY[0], "C": CY[1], "w": CY[2],
                                                       "W": CY[3]}, 38, 4)
    # moss along the top of the arch and on the plinths
    for x in range(w):
        ys = np.where(m[:, x])[0]
        if not len(ys):
            continue
        y0 = ys.min()
        if y0 < 40 and (x < 30 or x > 48 or x % 3):
            put(img, x, y0, MOSS[3] if x % 7 else CY[1])
            if x % 2 == 0:
                put(img, x, y0 + 1, MOSS[2])
    # hanging vines from the intrados
    for (vx, ln, pod) in [(22, 12, True), (31, 7, False), (55, 10, True), (60, 5, False)]:
        ys = np.where(ring[:, vx])[0]
        y0 = ys.max() + 1
        for i in range(ln):
            xo = 1 if (i // 3) % 2 else 0
            put(img, vx + xo, y0 + i, MOSS[2] if i % 2 else MOSS[1])
            if i % 4 == 2:
                put(img, vx + xo - 1, y0 + i, MOSS[3])
        if pod:
            put(img, vx, y0 + ln, CY[1])
            put(img, vx + 1, y0 + ln, CY[0])
            put(img, vx, y0 + ln + 1, CY[0])
            put(img, vx - 1, y0 + ln - 1, CY[2])
    # cracks / missing chip
    for (x, y) in line_pts(62, 18, 66, 26) + line_pts(66, 26, 65, 31):
        if m[y, x]:
            put(img, x, y, STONE_R[0])
    outline(img)
    return img


# ---------------------------------------------------------------------------
# statue head
# ---------------------------------------------------------------------------
def statue_head():
    w, h = 40, 40
    yy, xx = np.mgrid[0:h, 0:w]
    face = ellipse_mask(w, h, 20, 23, 12.5, 15)
    crown = poly_mask(w, h, [(7, 14), (11, 4), (16, 7), (20, 1), (24, 7), (29, 4), (33, 14)])
    ears = ellipse_mask(w, h, 7.5, 22, 2.5, 5) | ellipse_mask(w, h, 32.5, 22, 2.5, 5)
    m = (face | crown | ears) & (yy <= 36)
    img = shade_mask(m, STONE_R, sigma=2.6, ambient=0.5, contrast=1.15)
    # crown band
    for x in range(8, 33):
        if m[13, x]:
            put(img, x, 13, STONE_R[1])
        if m[12, x]:
            put(img, x, 12, STONE_R[4] if x < 22 else STONE_R[3])
    stamp(img, ["a.A.a", ".AyA.", "a.A.a"], {"a": AM[0], "A": AM[1], "y": AM[2]}, 18, 7)
    # brow ridge + glowing eyes
    for x in range(11, 30):
        if x not in (19, 20, 21):
            put(img, x, 18, STONE_R[4] if x < 20 else STONE_R[3])
            put(img, x, 19, STONE_R[1])
    stamp(img, ["oCCwo", ".occo"], {"o": STONE_R[0], "c": CY[0], "C": CY[1], "w": CY[2]}, 12, 20)
    stamp(img, ["owCCo", ".occo"], {"o": STONE_R[0], "c": CY[0], "C": CY[1], "w": CY[2]}, 23, 20)
    # nose
    for y in range(20, 28):
        put(img, 20, y, STONE_R[4])
        put(img, 21, y, STONE_R[2])
    put(img, 19, 27, STONE_R[1])
    put(img, 20, 28, STONE_R[1])
    put(img, 21, 28, STONE_R[1])
    put(img, 22, 27, STONE_R[1])
    # mouth
    for x in range(16, 26):
        put(img, x, 31, STONE_R[1])
    for x in range(17, 25):
        put(img, x, 32, STONE_R[3])
    # tear-like glowing streak from the right eye + crack
    put(img, 26, 23, CY[0])
    for (x, y) in line_pts(13, 5, 15, 12) + line_pts(15, 12, 13, 16):
        if m[y, x]:
            put(img, x, y, STONE_R[1])
    # moss over the crown
    for x in range(w):
        ys = np.where(m[:, x])[0]
        if len(ys) and x < 27:
            put(img, x, ys.min(), MOSS[3] if x % 6 else CY[1])
            if x % 2:
                put(img, x, ys.min() + 1, MOSS[2])
    for x, n in [(8, 6), (9, 3), (14, 2)]:
        ys = np.where(m[:, x])[0]
        for d in range(n):
            put(img, x, ys.min() + 2 + d, MOSS[1] if d < n - 1 else CY[0])
    # buried: ground mound at the bottom
    mound = ellipse_mask(w, h, 20, 40, 19, 5)
    for y in range(h):
        for x in range(w):
            if mound[y, x]:
                c = MOSS[2] if y <= 36 else MOSS[1]
                if y >= 38:
                    c = MOSS[0]
                img[y, x] = col(c)
    for x in range(2, 38, 4):
        put(img, x, 35, MOSS[3])
    outline(img)
    return img


# ---------------------------------------------------------------------------
# tech panel (4 frames) and holo glyph (6 frames)
# ---------------------------------------------------------------------------
SCREEN_TEXT = [
    "AA.A.AAA.A",
    "..........",
    "A.AAA..AA.",
    "..........",
    "AAA.A.A.AA",
    "..........",
    ".AA.AAA.A.",
    "..........",
]


def tech_panel():
    fw, h, n = 24, 24, 4
    out = new(fw * n, h)
    for f in range(n):
        img = new(fw, h)
        # body
        for y in range(3, 22):
            for x in range(2, 22):
                fv = cyl_tone(x, 2, 21, 1.6, 4.4)
                if y == 3:
                    fv += 1
                if y == 21:
                    fv -= 1
                img[y, x] = ramp_px(METAL_R, dither_idx(fv, x, y))
        # slanted top
        for x in range(4, 20):
            put(img, x, 2, METAL_R[4] if x < 12 else METAL_R[3])
        # feet
        for x in (4, 5, 18, 19):
            put(img, x, 22, METAL_R[2])
            put(img, x, 23, METAL_R[1])
        # screen
        rect(img, 5, 6, 14, 9, METAL_R[0])
        for y in range(9 - 2):
            row = SCREEN_TEXT[(y + f * 2) % len(SCREEN_TEXT)]
            for i, ch in enumerate(row):
                if ch == "A":
                    put(img, 7 + i, 7 + y, AM[1] if (i + y + f) % 4 else AM[2])
        # screen bezel highlight
        for x in range(5, 19):
            put(img, x, 5, METAL_R[1])
            put(img, x, 15, METAL_R[4])
        # indicator lights
        for i, lx in enumerate((6, 9, 12, 15)):
            c = AM[2] if i == f else (AM[0] if (i + 1) % 4 != f else AM[1])
            put(img, lx, 18, c)
            put(img, lx + 1, 18, METAL_R[1])
        put(img, 18, 18, CY[1] if f % 2 == 0 else CY[0])
        # moss + crack
        for (x, y, c) in [(2, 3, MOSS[3]), (3, 3, MOSS[2]), (4, 2, MOSS[2]), (2, 4, MOSS[1]),
                          (2, 5, MOSS[1]), (20, 21, MOSS[2]), (21, 20, MOSS[1])]:
            put(img, x, y, c)
        for (x, y) in line_pts(19, 4, 17, 9):
            put(img, x, y, METAL_R[1])
        outline(img)
        out[:, f * fw:(f + 1) * fw] = img
    return out


GLYPH = [
    "....ccccc....",
    "..cc.....cc..",
    ".c....C....c.",
    ".c...CwC...c.",
    "c....C.C....c",
    "c..CCC.CCC..c",
    "c...C.W.C...c",
    "c..CCC.CCC..c",
    "c....C.C....c",
    ".c...CwC...c.",
    ".c....C....c.",
    "..cc.....cc..",
    "....ccccc....",
]


def holo_glyph():
    fw, h, n = 24, 32, 6
    out = new(fw * n, h)
    gm = {"c": CY[0], "C": CY[1], "w": CY[2], "W": CY[3]}
    for f in range(n):
        img = new(fw, h)
        # projector base
        for y in range(26, 32):
            half = 4 + (y - 26)
            for x in range(12 - half, 12 + half):
                fv = cyl_tone(x, 12 - half, 12 + half - 1, 1.4, 4.4)
                if y == 26:
                    fv += 1
                img[y, x] = ramp_px(METAL_R, dither_idx(fv, x, y))
        for x in range(9, 15):
            put(img, x, 26, CY[2] if 10 <= x <= 13 else CY[0])
        put(img, 8, 29, AM[1] if f % 3 == 0 else AM[0])
        put(img, 15, 29, AM[1] if f % 3 == 1 else AM[0])
        # light cone (sparse dither, translucent)
        for y in range(15, 26):
            spread = (26 - y) * 0.45
            for x in range(int(12 - 2 - spread), int(12 + 2 + spread)):
                if (x + y + f) % 3 == 0 and 0 <= x < fw:
                    img[y, x] = col(CY[0], 110)
        # rotating glyph: horizontal scale = |cos|
        th = math.pi * f / n
        sx = abs(math.cos(th))
        bob = [0, 0, -1, -1, -1, 0][f]
        gy0 = 3 + bob
        for gy, row in enumerate(GLYPH):
            for gx, ch in enumerate(row):
                if ch not in gm:
                    continue
                X = int(math.floor(12 + (gx - 6) * sx + 0.5))
                c = gm[ch]
                if gy % 2 == 1 and ch == "c":
                    c = "#178f8a"
                put(img, X, gy0 + gy, c)
        # scan flicker: dim one line per frame
        sl = gy0 + (f * 2) % 13
        for x in range(fw):
            if img[sl, x, 3] == 255 and sl < 16:
                img[sl, x, 3] = 150
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# broken pipe, torn banner, ancient bones
# ---------------------------------------------------------------------------
def pipe_broken():
    w, h = 48, 24
    img = new(w, h)
    top, bot = 6, 21
    brk = [41, 43, 42, 44, 45, 43, 42, 44, 46, 44, 43, 41, 42, 44, 43, 42]  # broken end per row
    for y in range(top, bot + 1):
        v = (y - top) / (bot - top)
        fv = 4.6 - 3.6 * abs(v - 0.28) / 0.72 if v > 0.28 else 4.6 - (0.28 - v) * 3
        for x in range(4, brk[y - top]):
            f = fv
            if x in (10, 11, 30, 31):        # flanges
                f += 0.8 if x in (10, 30) else -0.6
            img[y, x] = ramp_px(METAL_R, dither_idx(f, x, y, 0.06))
    # flange rims stick out 1px
    for fx in (10, 11, 30, 31):
        put(img, fx, top - 1, METAL_R[4] if fx in (10, 30) else METAL_R[3])
        put(img, fx, bot + 1, METAL_R[1])
    # rivets on flanges
    for fx in (10, 30):
        for y in (8, 12, 16, 20):
            put(img, fx, y, METAL_R[5])
    # amber light band
    for y in range(top + 1, bot):
        put(img, 20, y, AM[1] if 9 <= y <= 13 else AM[0])
        put(img, 21, y, AM[0] if y > 13 else AM[1])
    put(img, 20, 10, AM[2])
    put(img, 20, 11, AM[2])
    # left end cap (lit disc)
    cap = ellipse_mask(w, h, 4.5, (top + bot + 1) / 2, 3, 8)
    for y in range(h):
        for x in range(w):
            if cap[y, x]:
                img[y, x] = ramp_px(METAL_R, dither_idx(4.2 - (y - top) * 0.15 - x * 0.1, x, y))
    put(img, 4, 13, METAL_R[1])
    put(img, 4, 14, METAL_R[1])
    # broken interior at the right end
    for y in range(top + 2, bot - 1):
        e = brk[y - top] - 1
        for k in range(2, 4):
            put(img, e - k + 2, y, METAL_R[0] if k == 3 else METAL_R[1])
    # leak glow dripping from the break
    for (x, y, c) in [(41, 22, CY[0]), (42, 23, CY[1]), (40, 23, CY[0]), (44, 20, CY[1])]:
        put(img, x, y, c)
    # moss on top + vine
    for x in range(5, 40):
        if x % 7 not in (0, 1):
            put(img, x, top - 1 if x not in (10, 11, 30, 31) else top - 2, MOSS[3] if x % 9 else CY[1])
        if x % 3 == 0 and x not in (10, 11, 30, 31):
            put(img, x, top, MOSS[2])
    for i, (x, y) in enumerate(path_pixels(bezier((14, 6), (17, 14), (24, 21), 16))):
        put(img, x, y, MOSS[1] if i % 2 else MOSS[2])
    outline(img)
    return img


def banner_torn():
    w, h = 24, 48
    img = new(w, h)
    # pole
    for y in range(4, 46):
        put(img, 4, y, METAL_R[4])
        put(img, 5, y, METAL_R[2])
    # finial
    stamp(img, [".a.", "aAy", ".a."], {"a": AM[0], "A": AM[1], "y": AM[2]}, 3, 1)
    # crossbar
    for x in range(3, 22):
        put(img, x, 6, METAL_R[4])
        put(img, x, 7, METAL_R[2])
    put(img, 21, 5, METAL_R[3])
    # cloth with folds and torn bottom
    bottom = [40, 38, 41, 36, 33, 37, 39, 35, 30, 34, 38, 41, 39, 35, 32]
    for i, x in enumerate(range(6, 21)):
        fold = math.sin((x - 6) / 14 * math.pi * 3) * 0.9
        for y in range(8, bottom[i]):
            f = 2.4 + fold - (y - 8) / 60.0
            if x in (6, 20):
                c = col(AM[0]) if (y % 6) else col(AM[1])
                img[y, x] = c
                continue
            img[y, x] = ramp_px(CLOTH_R, dither_idx(f, x, y))
        # frayed threads
        if i % 3 == 1:
            put(img, x, bottom[i], CLOTH_R[1])
    # top hem
    for x in range(6, 21):
        put(img, x, 8, CLOTH_R[4])
        put(img, x, 9, AM[0] if x % 2 else AM[1])
    # tear hole
    for (x, y) in [(15, 27), (16, 27), (15, 28), (16, 28), (17, 28), (16, 29)]:
        img[y, x] = 0
    # emblem: cyan eye-sigil
    stamp(img, ["..ccc..", ".c...c.", "c..W..c", "c.CwC.c", "c..C..c", ".c...c.", "..ccc.."],
          {"c": CY[0], "C": CY[1], "w": CY[2], "W": CY[3]}, 10, 13)
    # base stones + moss
    for (x, y, c) in [(2, 46, STONE_R[3]), (3, 46, STONE_R[4]), (6, 46, STONE_R[3]),
                      (7, 47, STONE_R[2]), (2, 47, STONE_R[2]), (3, 47, STONE_R[3]),
                      (4, 47, MOSS[2]), (5, 47, MOSS[1]), (4, 46, MOSS[3]), (5, 45, MOSS[2]),
                      (6, 47, STONE_R[2]), (1, 47, MOSS[1])]:
        put(img, x, y, c)
    put(img, 4, 45, METAL_R[3])
    outline(img)
    return img


def bones_ancient():
    w, h = 48, 24
    img = new(w, h)
    # ribs: arcs from a spine on the ground
    for i, bx in enumerate((22, 28, 34, 40)):
        ht = [14, 15, 13, 10][i]
        pts = bezier((bx, 21), (bx - 6, 21 - ht), (bx + 3, 21 - ht - 2), 24)
        m = thick_path_mask(w, h, pts, 1.3)
        lay = shade_mask(m, BONE_R, sigma=0.8, ambient=0.55, contrast=1.0)
        paste(img, lay, 0, 0)
    # spine
    for x in range(16, 46):
        put(img, x, 20, BONE_R[2] if x % 3 else BONE_R[1])
        put(img, x, 19, BONE_R[3] if x % 3 else BONE_R[2])
    # skull: big horned head
    sk = ellipse_mask(w, h, 11, 15, 9.5, 7) | poly_mask(w, h, [(3, 15), (0, 21), (8, 21)])
    horn = thick_path_mask(w, h, bezier((8, 9), (6, 2), (14, 1), 20), [2.2 - i * 0.09 for i in range(20)])
    m = sk | horn
    lay = shade_mask(m, BONE_R, sigma=1.6, ambient=0.55, contrast=1.1)
    paste(img, lay, 0, 0)
    # eye socket with faint glow, nostril, teeth
    stamp(img, [".oo.", "oCco", ".oo."], {"o": BONE_R[0], "c": CY[0], "C": CY[1]}, 11, 12)
    stamp(img, ["oo"], {"o": BONE_R[0]}, 3, 16)
    for x in range(4, 16, 2):
        put(img, x, 20, BONE_R[4])
        put(img, x + 1, 20, BONE_R[1])
    # half buried: dirt and moss at the bottom
    for x in range(w):
        put(img, x, 22, MOSS[1] if x % 4 else MOSS[2]) if img[21, x, 3] or img[20, x, 3] else None
        put(img, x, 23, MOSS[0]) if img[21, x, 3] or img[20, x, 3] else None
    for x in (6, 18, 25, 33, 41):
        put(img, x, 21, MOSS[3])
    outline(img)
    return img
