"""Glacior decor atlas (ASSET_SPEC section 9 format): sprites/decor/glacior_decor.png + .json.

Frozen-sanctuary props: ice spires, icicles, snow drift, frozen tree, snowy pine, glowing ice
crystals, frozen statue, aurora lamp, frost flower, ice arch, frozen machine, frost banner,
frozen bones, snowy rocks, floating ice. Each painter returns an RGBA array (animated sprites:
frames laid horizontally).
"""
import math

import numpy as np

from decor import build_atlas
from decor_nature import blob_mask, prism
from helpers import (bezier, col, ellipse_mask, lightness, line_pts, new, outline, paste,
                     path_pixels, poly_mask, put, rng, save_json, save_png, shade_mask,
                     thick_path_mask, value_noise)
from sprites import stamp

OG = "#0a1428"
ICE_P = ["#0c1d3c", "#1f4f8f", "#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]
ICE_R = ["#0c1d3c", "#163463", "#1f4f8f", "#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]
GLOW_P = ["#0a2a3a", "#1a6a7a", "#2fb8c8", "#7ff6ff", "#c8fcff", "#ffffff"]
SNOW = ["#5f7aa0", "#7d93b5", "#9fb4d2", "#c9d8ea", "#dfe9f5", "#ffffff"]
ROCK = ["#0f1626", "#1b253b", "#29364f", "#3b4a66", "#536380", "#7486a2", "#98a8c0"]
STEEL = ["#141a26", "#232d3e", "#3d4a5e", "#5c6b80", "#8a9ab0", "#b8c6d6"]
PINE = ["#06161a", "#0c2428", "#143a3a", "#1f5048", "#2e6858"]
WOOD = ["#141018", "#2a2230", "#4a3a48", "#6a5a68", "#8a7a88"]
CLOTH = ["#0a1428", "#13264a", "#1f3a66", "#2a4f80", "#3a6aa0"]
BONE = ["#3a4a64", "#6a7e9a", "#9fb0c8", "#c9d8ea", "#eef4fb"]
FROST = ["#2fb8c8", "#7ff6ff", "#d6fdff"]
AUR = ["#5dffb0", "#b48cff"]


def snow_top(img, m, depth=3, seed=0, sparkle=0):
    """Snow cap on the upper surface of a mask (lit white -> blue shadow below)."""
    h, w = m.shape
    r = rng(seed)
    for x in range(w):
        ys = np.where(m[:, x])[0]
        if not len(ys):
            continue
        y0 = ys.min()
        d = depth + (1 if r.random() < 0.35 else 0) - (1 if r.random() < 0.2 else 0)
        for k in range(d):
            if y0 + k < h and m[y0 + k, x]:
                c = SNOW[5] if k == 0 else (SNOW[4] if k < d - 1 else SNOW[2])
                if k == 0 and x > w * 0.6:
                    c = SNOW[4]
                img[y0 + k, x] = col(c)
        if sparkle and (x * 5 + seed) % sparkle == 0:
            img[y0, x] = col(FROST[2])


def icicle(img, x, y0, ln, wd=1.5):
    """Tapered icicle hanging from (x, y0): light left side, darker right, white tip."""
    for y in range(int(y0), int(y0 + ln)):
        t = (y - y0) / max(1, ln)
        half = wd * (1 - t) + 0.3
        for xx in range(int(math.floor(x - half)), int(math.ceil(x + half)) + 1):
            if abs(xx + 0.5 - x - 0.5) <= half + 0.2:
                u = (xx + 0.5 - (x - half)) / (2 * half + 0.01)
                c = ICE_P[4] if u < 0.4 else (ICE_P[3] if u < 0.75 else ICE_P[2])
                put(img, xx, y, c)
    put(img, int(round(x)), int(y0 + ln - 1), ICE_P[5])


# ---------------------------------------------------------------------------
# ice spire, icicles, snow drift
# ---------------------------------------------------------------------------
def ice_spire():
    w, h = 24, 56
    img = new(w, h)
    for (bx, by, ht, wd, ln) in [(6, 55, 20, 6, -4), (18, 55, 28, 6, 4), (12, 55, 54, 9, 0.5),
                                 (15, 55, 14, 5, 2), (4, 55, 9, 4, -2)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln, ramp=ICE_P), 0, 0)
    # inner refraction lines + frost sparkle
    for (x, y) in line_pts(10, 14, 9, 36):
        if img[y, x, 3]:
            put(img, x, y, ICE_P[5] if y < 22 else ICE_P[4])
    for (x, y) in [(14, 30), (7, 44), (19, 40)]:
        put(img, x, y, FROST[2])
    for x in range(1, 23):
        put(img, x, 55, SNOW[4] if x % 3 else SNOW[3])
        if 3 < x < 20:
            put(img, x, 54, SNOW[5] if x % 4 else SNOW[4])
    outline(img, OG)
    return img


def _icicle_cluster(w, h, specs):
    img = new(w, h)
    # icy/snowy bar they hang from
    for x in range(1, w - 1):
        put(img, x, 0, SNOW[5] if x % 3 else SNOW[4])
        put(img, x, 1, SNOW[3] if x % 2 else ICE_P[4])
        put(img, x, 2, ICE_P[3])
    for (x, ln, wd) in specs:
        icicle(img, x, 2, ln, wd)
    outline(img, OG)
    img[0, :] = np.where(img[0, :, 3:4] > 0, img[0, :], 0)
    return img


def icicles():
    return _icicle_cluster(16, 24, [(3, 9, 1.3), (6, 17, 1.8), (9, 12, 1.5), (12, 21, 1.9),
                                    (14, 7, 1.0)])


def icicles_long():
    return _icicle_cluster(24, 48, [(3, 18, 1.6), (6, 34, 2.2), (10, 22, 1.8), (13, 45, 2.6),
                                    (17, 28, 2.0), (20, 14, 1.4), (22, 24, 1.5)])


def snow_drift():
    w, h = 48, 16
    m = np.zeros((h, w), bool)
    prof = [15 - 9 * math.exp(-((x - 18) / 11.0) ** 2) - 5 * math.exp(-((x - 35) / 8.0) ** 2)
            - 1.2 * math.sin(x * 0.7) * 0.5 for x in range(w)]
    for x in range(w):
        m[int(round(prof[x])):, x] = True
    lit = lightness(m, sigma=2.0)
    img = new(w, h)
    for y in range(h):
        for x in range(w):
            if m[y, x]:
                f = 3.6 + 1.6 * lit[y, x] - (y - prof[x]) * 0.12
                i = int(np.clip(round(f + (0.25 if (x + y) % 2 else 0)), 1, 5))
                img[y, x] = col(SNOW[i])
    for (x, y) in [(14, 7), (22, 8), (33, 11), (9, 11)]:
        put(img, x, y, FROST[2])
    outline(img, OG)
    img[15, :] = np.where(img[15, :, 3:4] > 0, col(SNOW[2]), 0)
    return img


# ---------------------------------------------------------------------------
# trees
# ---------------------------------------------------------------------------
def frozen_tree():
    w, h = 32, 48
    img = new(w, h)
    m = np.zeros((h, w), bool)
    branches = [((15, 47), (14, 30), (16, 12), 3.2, 1.6),
                ((15, 30), (8, 24), (3, 14), 1.8, 0.7),
                ((16, 25), (24, 20), (28, 9), 1.8, 0.7),
                ((16, 16), (12, 10), (10, 3), 1.3, 0.6),
                ((15, 38), (22, 34), (26, 28), 1.4, 0.6),
                ((7, 22), (5, 22), (2, 19), 0.9, 0.5),
                ((25, 17), (27, 15), (30, 15), 0.9, 0.5)]
    for (a, b, c, r0, r1) in branches:
        pts = bezier(a, b, c, 30)
        m |= thick_path_mask(w, h, pts, [r0 + (r1 - r0) * i / 29 for i in range(30)])
    lay = shade_mask(m, WOOD, sigma=1.0, ambient=0.45, contrast=1.1, dither_strength=0.2)
    paste(img, lay, 0, 0)
    # ice glaze on the upper side of every branch + icicles under the big ones
    up = m & ~np.roll(m, 1, axis=0)
    img[up] = col(ICE_P[4])
    up2 = m & ~np.roll(m, 2, axis=0) & ~up
    img[up2 & (np.arange(w)[None, :] % 2 == 0)] = col(ICE_P[3])
    for (x, y, ln) in [(6, 22, 4), (9, 26, 3), (22, 22, 5), (25, 19, 3), (19, 35, 3), (12, 13, 3)]:
        icicle(img, x, y, ln, 0.8)
    # snow mound at the foot
    mound = ellipse_mask(w, h, 15.5, 48, 11, 4)
    for y in range(h):
        for x in range(w):
            if mound[y, x]:
                img[y, x] = col(SNOW[5] if y < 46 else SNOW[3])
    outline(img, OG)
    return img


def snow_pine():
    w, h = 32, 56
    img = new(w, h)
    r = rng(901)
    m = np.zeros((h, w), bool)
    tiers = 6
    tier_masks = []
    for t in range(tiers):
        u0 = t / tiers
        ty = 2 + 46 * u0
        by = 2 + 46 * (u0 + 1.4 / tiers)
        half = 15 * (0.25 + 0.75 * (t + 1) / tiers) * (0.9 + 0.15 * r.random())
        pts = [(16, ty), (16 + half * 0.5, ty + (by - ty) * 0.5), (16 + half, by + 1),
               (16 + half * 0.6, by - 1), (16, by), (16 - half * 0.6, by - 1), (16 - half, by + 1),
               (16 - half * 0.5, ty + (by - ty) * 0.5)]
        tm = poly_mask(w, h, pts)
        tier_masks.append(tm)
        m |= tm
    trunk = np.zeros((h, w), bool)
    trunk[48:55, 15:18] = True
    paste(img, shade_mask(trunk, WOOD, sigma=0.8, ambient=0.5, contrast=1.0), 0, 0)
    lay = shade_mask(m, PINE, sigma=1.4, ambient=0.5, contrast=1.1, dither_strength=0.25)
    paste(img, lay, 0, 0)
    # snow resting on each tier: a 2-3 px band on the visible upper slope, clumps at the tips
    above = np.zeros((h, w), bool)
    for t, tm in enumerate(tier_masks):
        vis = tm & ~above
        top1 = vis & ~np.roll(vis, 1, axis=0)
        top2 = vis & np.roll(top1, 1, axis=0)
        top3 = vis & np.roll(top2, 1, axis=0)
        img[top1] = col(SNOW[5])
        img[top2] = col(SNOW[4])
        xs = np.arange(w)[None, :]
        img[top3 & (xs < 16) & ((xs + np.arange(h)[:, None]) % 2 == 0)] = col(SNOW[3])
        img[top2 & (xs > 18)] = col(SNOW[3])
        above |= tm
    # snow clumps drooping off the tier tips
    for t in range(1, tiers):
        u0 = t / tiers
        by = int(2 + 46 * (u0 + 1.4 / tiers))
        for x in range(w):
            if tier_masks[t][min(h - 1, by), x] and (x < 6 or x > 25):
                put(img, x, by + 1, SNOW[4])
    put(img, 16, 1, SNOW[5])
    put(img, 16, 0, SNOW[4])
    for x in range(6, 27):
        put(img, x, 55, SNOW[4] if x % 3 else SNOW[3])
        if 9 < x < 23:
            put(img, x, 54, SNOW[5])
    outline(img, OG)
    return img


# ---------------------------------------------------------------------------
# glowing crystals
# ---------------------------------------------------------------------------
def ice_crystal():
    w, h = 16, 24
    img = new(w, h)
    for (bx, by, ht, wd, ln) in [(4, 23, 10, 4, -2), (12, 23, 12, 4, 2), (8, 23, 21, 6, 0.5),
                                 (10, 23, 7, 3, 1)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln, ramp=GLOW_P), 0, 0)
    for (x, y) in [(8, 5), (7, 11), (11, 15)]:
        put(img, x, y, "#ffffff")
    for x in range(2, 15):
        put(img, x, 23, SNOW[4] if x % 3 else SNOW[3])
    outline(img, OG)
    return img


def ice_crystal_big():
    w, h = 32, 48
    img = new(w, h)
    base = ellipse_mask(w, h, 16, 47, 13, 5)
    paste(img, shade_mask(base, ROCK[1:], sigma=1.5, ambient=0.55), 0, 0)
    for (bx, by, ht, wd, ln) in [(7, 45, 18, 6, -5), (25, 45, 22, 6, 5), (11, 44, 30, 7, -2),
                                 (21, 44, 33, 7, 2), (16, 45, 46, 9, 0)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln, ramp=GLOW_P), 0, 0)
    for (x, y) in [(16, 10), (12, 22), (21, 18), (15, 30)]:
        put(img, x, y, "#ffffff")
    snow_top(img, base & ~(img[..., 3] > 0) | (base & (np.arange(h)[:, None] >= 45)), 1, seed=3)
    outline(img, OG)
    return img


# ---------------------------------------------------------------------------
# frozen statue
# ---------------------------------------------------------------------------
def frozen_statue():
    w, h = 32, 48
    yy, xx = np.mgrid[0:h, 0:w]
    head = ellipse_mask(w, h, 15, 9, 4.5, 5)
    hood = poly_mask(w, h, [(9, 9), (15, 1), (21, 9), (22, 16), (8, 16)])
    robe = poly_mask(w, h, [(8, 14), (22, 14), (25, 40), (5, 40)])
    arm = thick_path_mask(w, h, bezier((20, 17), (24, 22), (21, 26), 12), 2.0)
    staff = (xx >= 25) & (xx <= 26) & (yy >= 4) & (yy <= 40)
    plinth = (yy >= 40) & (yy <= 46) & (xx >= 4) & (xx <= 27)
    m = head | hood | robe | arm | staff | plinth
    img = shade_mask(m, ROCK[1:], sigma=1.6, ambient=0.5, contrast=1.2, dither_strength=0.25)
    # robe folds + face shadow + glowing frost eyes
    for (x0, y0, x1, y1) in [(12, 18, 10, 39), (16, 20, 16, 39), (20, 22, 22, 39)]:
        for (x, y) in line_pts(x0, y0, x1, y1):
            put(img, x, y, ROCK[1])
    stamp(img, ["kkkkk", "kCkCk", "kkkkk"], {"k": ROCK[0], "C": FROST[1]}, 13, 8)
    # staff head: a frost crystal
    stamp(img, [".w.", "wCw", ".c."], {"w": FROST[2], "C": FROST[1], "c": FROST[0]}, 24, 1)
    # ice encasing the lower body: statue seen through tinted ice, glassy outer crust
    crust = poly_mask(w, h, [(2, 46), (3, 30), (7, 22), (13, 26), (20, 21), (27, 27), (29, 46)])
    ice = new(w, h)
    for y in range(h):
        for x in range(w):
            if not crust[y, x]:
                continue
            if m[y, x]:
                c = np.array(img[y, x, :3], np.float32) * 0.45 + np.array(col(ICE_P[2])[:3]) * 0.55
                ice[y, x] = (int(c[0]), int(c[1]), int(c[2]), 255)
            else:
                ice[y, x] = col(ICE_P[3] if (x + y) % 5 else ICE_P[4])
    edge = crust & ~np.roll(crust, 1, axis=0)
    ice[edge] = col(ICE_P[5])
    left = crust & ~np.roll(crust, 1, axis=1)
    ice[left & ~edge] = col(ICE_P[4])
    for (x, y) in line_pts(6, 29, 10, 42) + line_pts(22, 26, 25, 33):
        if crust[y, x]:
            ice[y, x] = col(ICE_P[4])
    paste(img, ice, 0, 0)
    snow_top(img, m & ~crust & (yy < 12), 1, seed=5)
    for x in range(2, 30):
        put(img, x, 46, SNOW[4] if x % 3 else SNOW[3])
        put(img, x, 47, SNOW[3])
    outline(img, OG)
    return img


# ---------------------------------------------------------------------------
# aurora lamp (6 frames), frost flower (4 frames)
# ---------------------------------------------------------------------------
AUR_CYCLE = [("#5dffb0", "#b8ffe0", "#2a8a6e"), ("#5df0c8", "#c0fff0", "#2a8a80"),
             ("#7fd8ff", "#d6f4ff", "#2a6a9a"), ("#b48cff", "#e0ccff", "#5a3a9a"),
             ("#7fd8ff", "#d6f4ff", "#2a6a9a"), ("#5df0c8", "#c0fff0", "#2a8a80")]


def aurora_lamp():
    fw, h, n = 16, 40, 6
    out = new(fw * n, h)
    for f in range(n):
        img = new(fw, h)
        # post with a base and a frosted collar
        for y in range(14, 38):
            put(img, 7, y, STEEL[4])
            put(img, 8, y, STEEL[2])
        for y in (24, 25):
            put(img, 6, y, STEEL[3])
            put(img, 9, y, STEEL[2])
        stamp(img, ["..mMMm..", ".mMNNMm.", "mMNNNNMm"], {"m": STEEL[1], "M": STEEL[3],
                                                         "N": STEEL[4]}, 4, 37)
        # lantern cage
        cage = ["..oooo..", ".oMNNMo.", "oM....Mo", "oM....Mo", "oM....Mo", "oM....Mo",
                ".oMNNMo.", "..oMMo.."]
        stamp(img, cage, {"o": STEEL[1], "M": STEEL[3], "N": STEEL[5]}, 4, 3)
        core, hi, dim = AUR_CYCLE[f]
        for y in range(5, 9):
            for x in range(6, 10):
                put(img, x, y, core if (x, y) not in ((6, 5), (9, 8)) else dim)
        put(img, 7, 6, hi)
        put(img, 8, 6, hi if f % 2 == 0 else core)
        put(img, 7, 7, hi)
        # finial + frost on top
        put(img, 7, 1, STEEL[4])
        put(img, 8, 1, STEEL[3])
        put(img, 7, 2, SNOW[5])
        put(img, 8, 2, SNOW[4])
        for y in range(10, 14):
            put(img, 7 + (y % 2), y, STEEL[3])
        outline(img, OG)
        # light leaks out of the cage sides (glowing, no outline)
        for (x, y) in [(5, 6), (5, 7), (10, 6), (10, 7)]:
            put(img, x, y, dim)
        out[:, f * fw:(f + 1) * fw] = img
    return out


def frost_flower():
    fw, h, n = 16, 16, 4
    out = new(fw * n, h)
    pulse = [FROST[0], FROST[1], FROST[2], FROST[1]]
    for f in range(n):
        img = new(fw, h)
        for (x, y) in path_pixels(bezier((8, 15), (7, 11), (8, 7), 10)):
            put(img, x, y, "#2a5a6a")
        for (x, y, c) in [(6, 12, "#3a7a8a"), (5, 11, "#5aa8b8"), (10, 13, "#3a7a8a"),
                          (11, 12, "#5aa8b8"), (9, 10, "#3a7a8a")]:
            put(img, x, y, c)
        # crystalline petals (6-point star), pulsing core
        petal = pulse[f]
        for (dx, dy) in [(0, -3), (0, 3), (-3, -1), (3, -1), (-3, 2), (3, 2), (-1, -2), (1, -2),
                         (-2, 0), (2, 0), (-1, 2), (1, 2)]:
            put(img, 8 + dx, 6 + dy, petal if abs(dx) + abs(dy) > 2 else FROST[1])
        put(img, 8, 6, "#ffffff")
        put(img, 7, 6, FROST[2])
        put(img, 9, 6, FROST[2] if f in (1, 2) else FROST[1])
        for x in range(4, 13):
            put(img, x, 15, SNOW[4] if x % 2 else SNOW[3])
        outline(img, OG)
        # bright petal tips are emissive: no outline at the tips
        for (x, y) in [(8, 2), (8, 10)]:
            if f in (1, 2):
                put(img, x, y, "#1f5a6a")
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# ice arch, frozen machine, frost banner, frozen bones
# ---------------------------------------------------------------------------
def ice_arch():
    w, h = 80, 64
    cx, cy, R, r = 40.0, 40.0, 39.5, 27.0
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    dist = np.sqrt(dx * dx + dy * dy)
    ring = (dist <= R) & (dist >= r) & (yy + 0.5 <= cy)
    legs = (yy + 0.5 > cy) & (((xx >= 1) & (xx <= 12)) | ((xx >= 67) & (xx <= 78)))
    base = (yy >= 57) & (((xx >= 0) & (xx <= 15)) | ((xx >= 64) & (xx <= 79)))
    m = ring | legs | base
    lit = lightness(m, sigma=2.0)
    img = new(w, h)
    ang = np.degrees(np.arctan2(-dy, dx))
    for y in range(h):
        for x in range(w):
            if not m[y, x]:
                continue
            f = 3.0 + 1.6 * lit[y, x]
            if ring[y, x]:
                seg = (ang[y, x] + 7) / 26.0          # crystalline facets along the ring
                fr = seg - math.floor(seg)
                if fr < 0.05:
                    f = 1.5
                elif fr < 0.4:
                    f += 0.7
                if dist[y, x] < r + 2.2:
                    f -= 1.0
                if dist[y, x] > R - 1.5:
                    f += 0.8
            else:
                if (x + 2 * y) % 13 == 0:
                    f += 1.0                          # refraction streaks
            i = int(np.clip(round(f + (0.3 if (x + y) % 2 else 0)), 1, 6))
            img[y, x] = col(ICE_R[i])
    # glowing frost rune on the keystone
    stamp(img, ["..c..", ".cCc.", "cCwCc", ".cCc.", "..c.."],
          {"c": FROST[0], "C": FROST[1], "w": "#ffffff"}, 38, 4)
    # snow along the top
    snow_top(img, m & (yy < 40), 2, seed=7, sparkle=9)
    # icicles hanging from the intrados
    for (vx, ln) in [(18, 7), (24, 12), (31, 6), (49, 9), (56, 13), (62, 5)]:
        ys = np.where(ring[:, vx])[0]
        if len(ys):
            icicle(img, vx, ys.max() + 1, ln, 1.2)
    for x in range(w):
        if base[63, x]:
            put(img, x, 63, SNOW[3])
            put(img, x, 62, SNOW[4])
    outline(img, OG)
    return img


def frozen_machine():
    w, h = 48, 40
    img = new(w, h)
    yy, xx = np.mgrid[0:h, 0:w]
    body = (xx >= 6) & (xx <= 33) & (yy >= 10) & (yy <= 37)
    tower = (xx >= 34) & (xx <= 43) & (yy >= 4) & (yy <= 37)
    pipe = (xx >= 1) & (xx <= 6) & (yy >= 24) & (yy <= 30)
    m = body | tower | pipe
    lay = shade_mask(m, STEEL, sigma=1.6, ambient=0.5, contrast=1.1, dither_strength=0.2)
    paste(img, lay, 0, 0)
    # panel seams + rivets
    for y in (10, 22, 37):
        for x in range(6, 34):
            put(img, x, y, STEEL[1] if y != 10 else STEEL[4])
    for x in (6, 20, 33):
        for y in range(10, 38):
            put(img, x, y, STEEL[1] if x != 6 else STEEL[4])
    for (x, y) in [(8, 12), (31, 12), (8, 35), (31, 35), (36, 6), (41, 6)]:
        put(img, x, y, STEEL[5])
    # dim cyan screen with frozen text + status lights
    for y in range(13, 20):
        for x in range(9, 19):
            put(img, x, y, "#0a2030")
    for (y, row) in zip(range(14, 19), ["CC.C.CCC", "........", "C.CC..C.", "........", "CCC.C.C."]):
        for i, ch in enumerate(row):
            if ch == "C":
                put(img, 10 + i, y, FROST[0] if (i + y) % 3 else FROST[1])
    for i, x in enumerate((23, 26, 29)):
        put(img, x, 15, FROST[1] if i == 1 else "#1a6a7a")
    # vent grille on the tower
    for y in range(9, 30, 3):
        for x in range(36, 42):
            put(img, x, y, STEEL[0])
    # ice crust + snow on top, icicles from the edges
    crust = np.zeros((h, w), bool)
    crust[8:12, 5:35] = True
    crust[2:6, 33:45] = True
    crust |= ellipse_mask(w, h, 4, 37, 6, 9) & (yy >= 30)
    crust |= ellipse_mask(w, h, 44, 36, 5, 10) & (yy >= 28)
    for y in range(h):
        for x in range(w):
            if crust[y, x]:
                img[y, x] = col(ICE_P[3] if (x + y) % 4 else ICE_P[4])
    snow_top(img, m | crust, 2, seed=11, sparkle=7)
    for (x, ln) in [(9, 5), (14, 3), (22, 6), (28, 4), (36, 4), (40, 7)]:
        y0 = 12 if x < 34 else 6
        icicle(img, x, y0, ln, 1.0)
    for x in range(0, 48):
        put(img, x, 39, SNOW[3])
        if 2 < x < 46:
            put(img, x, 38, SNOW[4] if x % 3 else SNOW[5])
    outline(img, OG)
    return img


def frost_banner():
    w, h = 24, 48
    img = new(w, h)
    for y in range(4, 46):
        put(img, 4, y, STEEL[4])
        put(img, 5, y, STEEL[2])
    stamp(img, [".c.", "cWc", ".c."], {"c": FROST[0], "W": FROST[2]}, 3, 1)
    for x in range(3, 22):
        put(img, x, 6, STEEL[4])
        put(img, x, 7, STEEL[2])
        put(img, x, 5, SNOW[5] if x % 3 else SNOW[4])
    bottom = [38, 37, 39, 36, 35, 38, 37, 34, 36, 39, 37, 35, 38, 36, 37]
    for i, x in enumerate(range(6, 21)):
        fold = math.sin((x - 6) / 14 * math.pi * 3) * 0.9
        for y in range(8, bottom[i]):
            f = 2.4 + fold - (y - 8) / 60.0
            idx = int(np.clip(round(f + (0.3 if (x + y) % 2 else 0)), 0, 4))
            img[y, x] = col(CLOTH[idx])
        # frozen hem: ice crust along the bottom edge
        put(img, x, bottom[i] - 1, ICE_P[3])
        put(img, x, bottom[i] - 2, ICE_P[2] if i % 2 else CLOTH[3])
    for (x, ln) in [(8, 4), (11, 6), (15, 3), (18, 5)]:
        icicle(img, x, bottom[x - 6], ln, 0.9)
    # hem + frost-cyan snowflake emblem
    for x in range(6, 21):
        put(img, x, 8, CLOTH[4])
        put(img, x, 9, FROST[0] if x % 2 else STEEL[3])
    stamp(img, ["...c...", "c..C..c", ".c.C.c.", "..CWC..", ".c.C.c.", "c..C..c", "...c..."],
          {"c": FROST[0], "C": FROST[1], "W": "#ffffff"}, 10, 14)
    # frost on the cloth
    for (x, y) in [(7, 12), (19, 16), (8, 27), (18, 30), (13, 33)]:
        put(img, x, y, SNOW[3])
    for (x, y, c) in [(2, 46, ROCK[3]), (3, 46, ROCK[4]), (6, 46, ROCK[3]), (7, 47, SNOW[3]),
                      (2, 47, SNOW[3]), (3, 47, SNOW[4]), (4, 47, SNOW[5]), (5, 47, SNOW[4]),
                      (6, 47, SNOW[3]), (1, 47, SNOW[2]), (4, 46, SNOW[5]), (5, 46, SNOW[4])]:
        put(img, x, y, c)
    outline(img, OG)
    return img


def frozen_bones():
    w, h = 48, 24
    img = new(w, h)
    for i, bx in enumerate((22, 28, 34, 40)):
        ht = [14, 15, 13, 10][i]
        pts = bezier((bx, 21), (bx - 6, 21 - ht), (bx + 3, 21 - ht - 2), 24)
        m = thick_path_mask(w, h, pts, 1.3)
        paste(img, shade_mask(m, BONE, sigma=0.8, ambient=0.55, contrast=1.0), 0, 0)
        # frost on top of each rib + tiny icicle
        tx, ty = int(round(pts[16][0])), int(round(pts[16][1]))
        put(img, tx, ty - 1, SNOW[5])
        icicle(img, int(round(pts[20][0])), int(round(pts[20][1])) + 1, 2 + i % 2, 0.6)
    for x in range(16, 46):
        put(img, x, 20, BONE[2] if x % 3 else BONE[1])
        put(img, x, 19, BONE[3] if x % 3 else BONE[2])
    # horned skull
    sk = ellipse_mask(w, h, 11, 15, 9.5, 7) | poly_mask(w, h, [(3, 15), (0, 21), (8, 21)])
    horn = thick_path_mask(w, h, bezier((8, 9), (6, 2), (14, 1), 20), [2.2 - i * 0.09 for i in range(20)])
    m = sk | horn
    paste(img, shade_mask(m, BONE, sigma=1.6, ambient=0.55, contrast=1.1), 0, 0)
    stamp(img, [".oo.", "oCco", ".oo."], {"o": BONE[0], "c": FROST[0], "C": FROST[1]}, 11, 12)
    stamp(img, ["oo"], {"o": BONE[0]}, 3, 16)
    for x in range(4, 16, 2):
        put(img, x, 20, BONE[4])
        put(img, x + 1, 20, BONE[1])
    snow_top(img, m, 1, seed=13)
    # buried in a snow bank
    bank = ellipse_mask(w, h, 24, 25, 25, 4.5)
    for y in range(h):
        for x in range(w):
            if bank[y, x]:
                img[y, x] = col(SNOW[5] if y <= 21 else (SNOW[4] if y <= 22 else SNOW[3]))
    outline(img, OG)
    return img


# ---------------------------------------------------------------------------
# snowy rocks, floating ice
# ---------------------------------------------------------------------------
def snow_rock_small():
    w, h = 16, 12
    m = blob_mask(w, h, 8, 8, 6.6, 5.2, 111, flat_bottom=11)
    img = shade_mask(m, ROCK[1:], sigma=1.2, ambient=0.5, contrast=1.1, dither_strength=0.2)
    snow_top(img, m, 2, seed=1)
    outline(img, OG)
    return img


def snow_rock_big():
    w, h = 32, 24
    m = blob_mask(w, h, 16, 16, 14, 10.5, 113, rough=0.14, flat_bottom=23)
    m |= blob_mask(w, h, 25, 19, 6, 5, 114, flat_bottom=23)
    img = shade_mask(m, ROCK[1:], sigma=2.0, ambient=0.5, contrast=1.1, dither_strength=0.2)
    for (x, y) in line_pts(12, 10, 16, 16) + line_pts(16, 16, 15, 20):
        put(img, x, y, ROCK[0])
    # small ice crystals growing from the crack
    stamp(img, ["..w", ".wj", "wjI", "oIo"], {"w": ICE_P[5], "j": ICE_P[4], "I": ICE_P[3],
                                             "o": OG}, 16, 12)
    snow_top(img, m, 3, seed=2, sparkle=8)
    outline(img, OG)
    return img


def ice_floating():
    w, h = 48, 40
    prof = value_noise(w, 1, 6, 1, seed=121, wrap_x=False)[0]
    m = np.zeros((h, w), bool)
    for x in range(w):
        dxx = abs(x + 0.5 - 24) / 21.0
        if dxx > 1:
            continue
        top = 9 + int(round(2.5 * dxx * dxx + prof[x] * 1.5))
        bot = 10 + int(round((1 - dxx ** 1.3) * 20 + prof[x] * 3))
        m[top:bot, x] = True
    # rock core (upper part) and clear ice below
    rock = m & (np.arange(h)[:, None] < 17)
    ice = m & ~rock
    img = shade_mask(rock, ROCK[1:], sigma=2.0, ambient=0.45, contrast=1.1)
    ice_l = shade_mask(ice, ICE_R[1:], sigma=2.0, ambient=0.5, contrast=1.1, dither_strength=0.25)
    paste(img, ice_l, 0, 0)
    for (x0, y0, x1, y1) in [(14, 19, 18, 26), (30, 18, 27, 25)]:
        for (x, y) in line_pts(x0, y0, x1, y1):
            if ice[y, x]:
                put(img, x, y, ICE_P[5])
    snow_top(img, m, 3, seed=4, sparkle=6)
    # icicles under the ice + glowing frost crystal at the keel
    for (x, ln) in [(13, 4), (17, 6), (21, 3), (28, 5), (33, 4)]:
        ys = np.where(m[:, x])[0]
        icicle(img, x, ys.max() + 1, ln, 1.0)
    for (bx, by, ht, wd, ln) in [(24, 29, 9, 4, 0), (20, 27, 5, 3, -1)]:
        p = prism(w, h, bx, by, ht, wd, ln, ramp=GLOW_P)[::-1].copy()
        shift = 2 * by - h + 1
        pp = new(w, h)
        pp[shift:] = p[:h - shift]
        paste(img, pp, 0, 0)
    outline(img, OG)
    return img


# name: (painter, frames, fps, pivot, glow)  glow = (colour, radius, (ox, oy))
SPRITES = {
    "ice_spire": (ice_spire, 1, 0, "bottom", None),
    "icicles": (icicles, 1, 0, "top", None),
    "icicles_long": (icicles_long, 1, 0, "top", None),
    "snow_drift": (snow_drift, 1, 0, "bottom", None),
    "frozen_tree": (frozen_tree, 1, 0, "bottom", None),
    "snow_pine": (snow_pine, 1, 0, "bottom", None),
    "ice_crystal": (ice_crystal, 1, 0, "bottom", ("#7ff6ff", 24, (0, -11))),
    "ice_crystal_big": (ice_crystal_big, 1, 0, "bottom", ("#7ff6ff", 44, (0, -24))),
    "frozen_statue": (frozen_statue, 1, 0, "bottom", ("#7ff6ff", 14, (0, -38))),
    "aurora_lamp": (aurora_lamp, 6, 4, "bottom", ("#5dffb0", 40, (0, -33))),
    "frost_flower": (frost_flower, 4, 6, "bottom", ("#7ff6ff", 16, (0, -10))),
    "ice_arch": (ice_arch, 1, 0, "bottom", ("#7ff6ff", 18, (0, -58))),
    "frozen_machine": (frozen_machine, 1, 0, "bottom", ("#7ff6ff", 16, (-10, -24))),
    "frost_banner": (frost_banner, 1, 0, "bottom", None),
    "frozen_bones": (frozen_bones, 1, 0, "bottom", None),
    "snow_rock_small": (snow_rock_small, 1, 0, "bottom", None),
    "snow_rock_big": (snow_rock_big, 1, 0, "bottom", None),
    "ice_floating": (ice_floating, 1, 0, "center", ("#7ff6ff", 30, (0, 10))),
}


def build():
    return build_atlas(SPRITES, "sprites/decor/glacior_decor.png")


def generate():
    atlas, meta = build()
    return [save_png(atlas, "sprites/decor/glacior_decor.png"),
            save_json(meta, "sprites/decor/glacior_decor.json")]
