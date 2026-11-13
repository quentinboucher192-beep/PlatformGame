"""Pyralis decor atlas (ASSET_SPEC section 9 format): sprites/decor/pyralis_decor.png + .json.

Volcanic forge props: lava vents, basalt columns, obsidian spire, anvil, brazier, chains,
lava fall, fire plant, slag, gear wreck, skull rock, ember crystal, steam pipe, burnt banner,
ash rocks. Each painter returns an RGBA array (animated sprites: frames laid horizontally).
"""
import math

import numpy as np

from decor import build_atlas
from decor_nature import blob_mask, prism
from helpers import (bezier, col, ellipse_mask, line_pts, new, outline, paste, path_pixels,
                     poly_mask, put, rng, save_json, save_png, shade_mask, thick_path_mask)
from sprites import stamp

OP = "#0d0607"
BAS = ["#0e0809", "#1e1416", "#2d2022", "#41302f", "#58403b", "#78584e", "#9a7a6a"]
ASH = ["#3a2c2c", "#5a4846", "#7e6a64", "#a8938a", "#cbb8ac"]
LAVA = ["#b3201a", "#ff4d1e", "#ff8a3d", "#ffc56b", "#fff1c2"]
EMB = "#6e1a12"
IRON = ["#0c0a0c", "#181417", "#262024", "#383034", "#51464a", "#726468", "#9a8a8c"]
RUST = ["#3a1e14", "#6e3b2a", "#a0583a", "#d08a5a", "#e8b088"]
OBS_P = ["#05030a", "#0d0714", "#140b1d", "#24143a", "#5b2a6e", "#c99af0"]
LAVA_P = ["#3a0c0a", "#7a1a12", "#b3201a", "#ff4d1e", "#ff8a3d", "#ffc56b"]
CLOTH = ["#1a0c0e", "#2e1216", "#4a1a1c", "#6a2420", "#8a3424"]
SMOKE = [(58, 44, 46, 200), (88, 70, 70, 170), (120, 100, 96, 130)]


def ash_top(img, m, depth=2, ember_every=0, seed=0):
    """Ash crust on the upper surface of a mask (like mossy_top for Sylvanox)."""
    h, w = m.shape
    for x in range(w):
        ys = np.where(m[:, x])[0]
        if not len(ys):
            continue
        y0 = ys.min()
        for d in range(depth):
            if y0 + d < h and m[y0 + d, x]:
                img[y0 + d, x] = col(ASH[3] if d == 0 else (ASH[2] if d == 1 else ASH[1]))
        if ember_every and (x * 7 + seed) % ember_every == 0 and y0 + 1 < h and m[y0 + 1, x]:
            img[y0 + 1, x] = col(LAVA[1])


def glow_line(img, pts, cols):
    for i, (x, y) in enumerate(pts):
        if 0 <= y < img.shape[0] and 0 <= x < img.shape[1] and img[y, x, 3]:
            img[y, x] = col(cols[min(len(cols) - 1, i * len(cols) // max(1, len(pts)))])


# ---------------------------------------------------------------------------
# lava vent (6 frames)
# ---------------------------------------------------------------------------
def lava_vent():
    fw, h, n = 32, 24, 6
    out = new(fw * n, h)
    cone = poly_mask(fw, h, [(1, 24), (4, 20), (9, 13), (11, 9), (21, 9), (23, 13), (28, 20),
                             (31, 24)])
    cone |= blob_mask(fw, h, 16, 21, 15, 4.5, 5, flat_bottom=23)
    base = shade_mask(cone, BAS[1:], sigma=1.6, ambient=0.5, contrast=1.2, dither_strength=0.12)
    ash_top(base, cone, 2)
    # crater rim + bowl
    for x in range(11, 22):
        put(base, x, 9, ASH[4] if x < 16 else ASH[3])
    bowl = ellipse_mask(fw, h, 16, 11.2, 4.8, 1.8)
    for f in range(n):
        img = base.copy()
        img[bowl] = col(LAVA[1])
        for x in range(11, 22):
            if bowl[11, x]:
                put(img, x, 11, LAVA[2])
        # bubbles cycling in the crater
        bx = [13, 15, 17, 19, 14, 18][f]
        put(img, bx, 10, LAVA[3])
        put(img, bx, 11, LAVA[4] if f % 2 == 0 else LAVA[3])
        put(img, (bx + 3) % 6 + 13, 11, LAVA[3])
        # flows down the cone front (flicker)
        for k, path in enumerate([[(14, 13), (13, 15), (13, 17), (12, 19), (11, 21)],
                                  [(19, 13), (20, 15), (21, 17), (22, 19)]]):
            for i, (x, y) in enumerate(path):
                c = LAVA[2] if i < 2 else (LAVA[1] if i < 4 else LAVA[0])
                if (i + f + k) % 5 == 0:
                    c = LAVA[3]
                put(img, x, y, c)
        outline(img, OP)
        # lit rim above the crater has no outline (glowing)
        for x in range(12, 21):
            if img[8, x, 3] and tuple(img[8, x][:3]) == tuple(col(OP)[:3]):
                img[8, x] = col("#5a1c10")
        # rising sparks
        for s in range(4):
            phase = (f + s * 1.5) % n
            sx = 16 + int(round(math.sin(s * 2.1 + phase * 0.9) * 3))
            sy = 7 - int(phase * 1.3) - s
            if sy >= 0:
                put(img, sx, sy, LAVA[3] if phase < 3 else LAVA[1])
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# basalt columns
# ---------------------------------------------------------------------------
def _hex_col(img, x0, x1, top, bottom, cracks=(), broken=None, glow_joint=None):
    """One hexagonal basalt prism seen from the front: 3 facets + slanted top face."""
    w = x1 - x0 + 1
    for x in range(x0, x1 + 1):
        u = (x - x0) / max(1, w - 1)
        if u < 0.3:
            f = 5
        elif u < 0.72:
            f = 4
        else:
            f = 2
        tt = top if broken is None else top + broken[x - x0]
        for y in range(tt, bottom):
            v = f
            if y in cracks:
                v = 1
            elif y - 1 in cracks:
                v = min(6, f + 1)
            if (x + y) % 9 == 0 and v > 2:
                v -= 1
            img[y, x] = col(BAS[v])
        # top face (ash) / broken surface
        if broken is None:
            put(img, x, top - 1, ASH[3] if u < 0.5 else ASH[2])
            if 0.15 < u < 0.85:
                put(img, x, top - 2, ASH[4] if u < 0.5 else ASH[3])
        else:
            put(img, x, tt, BAS[6] if u < 0.4 else BAS[5])
    if glow_joint is not None:
        jx, y0, y1 = glow_joint
        for y in range(y0, y1):
            put(img, jx, y, LAVA[1] if (y - y0) % 4 else LAVA[2])


def basalt_column():
    w, h = 24, 64
    img = new(w, h)
    _hex_col(img, 1, 7, 20, 62, cracks=(31, 47))
    _hex_col(img, 15, 22, 32, 62, cracks=(44, 55))
    _hex_col(img, 8, 15, 3, 62, cracks=(14, 27, 40, 52))
    # joints between columns (dark) with a glowing seam
    for y in range(20, 62):
        put(img, 8, y, BAS[1])
    for y in range(32, 62):
        put(img, 15, y, BAS[1])
    for y in range(36, 46):
        put(img, 15, y, LAVA[1] if y % 3 else LAVA[2])
    put(img, 15, 35, EMB)
    put(img, 15, 46, EMB)
    # rubble + ash at the foot
    for (x, y, c) in [(0, 62, BAS[4]), (0, 63, BAS[3]), (1, 63, ASH[2]), (6, 63, ASH[1]),
                      (12, 63, ASH[2]), (18, 63, ASH[1]), (22, 63, BAS[3]), (23, 63, BAS[2]),
                      (23, 62, BAS[4])]:
        put(img, x, y, c)
    for x in range(1, 23):
        put(img, x, 62, BAS[2] if x % 3 else ASH[1])
        put(img, x, 63, ASH[1] if x % 2 else ASH[2])
    outline(img, OP)
    return img


def basalt_column_broken():
    w, h = 24, 40
    img = new(w, h)
    _hex_col(img, 2, 8, 12, 38, cracks=(24,), broken=[2, 0, 1, 3, 4, 2, 5])
    _hex_col(img, 9, 16, 4, 38, cracks=(19, 30), broken=[5, 3, 0, 1, 2, 6, 4, 7])
    _hex_col(img, 17, 22, 22, 38, cracks=(31,), broken=[1, 0, 2, 3, 1, 2])
    for y in range(12, 38):
        put(img, 9, y, BAS[1])
    for y in range(22, 38):
        put(img, 17, y, BAS[1])
    for y in range(26, 33):
        put(img, 9, y, LAVA[1] if y % 3 else LAVA[2])
    # fallen chunk lying at the foot
    chunk = poly_mask(w, h, [(15, 39), (17, 35), (23, 34), (24, 38), (23, 40)])
    lay = shade_mask(chunk, BAS[1:], sigma=0.9, ambient=0.55, contrast=1.1, dither_strength=0.2)
    paste(img, lay, 0, 0)
    for x in range(1, 23):
        put(img, x, 38, BAS[2] if x % 3 else ASH[1])
        put(img, x, 39, ASH[1] if x % 2 else ASH[2])
    outline(img, OP)
    return img


# ---------------------------------------------------------------------------
# obsidian spire
# ---------------------------------------------------------------------------
def obsidian_spire():
    w, h = 24, 48
    img = new(w, h)
    for (bx, by, ht, wd, ln) in [(6, 47, 17, 6, -4), (18, 47, 24, 6, 4), (12, 47, 46, 9, 0.5),
                                 (15, 47, 12, 5, 2)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln, ramp=OBS_P), 0, 0)
    # sharp specular streaks + a warm reflection
    for (x, y) in line_pts(10, 10, 9, 30):
        if img[y, x, 3]:
            put(img, x, y, "#f2e2ff" if y < 18 else OBS_P[5])
    for (x, y) in [(14, 26), (14, 27), (15, 33)]:
        put(img, x, y, "#7a2a1e")
    put(img, 15, 34, LAVA[0])
    for x in range(2, 23):
        put(img, x, 47, BAS[2] if x % 3 else ASH[1])
    outline(img, "#05030a")
    return img


# ---------------------------------------------------------------------------
# forge anvil + brazier
# ---------------------------------------------------------------------------
def forge_anvil():
    w, h = 32, 24
    img = new(w, h)
    # stone plinth
    plinth = poly_mask(w, h, [(7, 24), (9, 17), (23, 17), (25, 24)])
    paste(img, shade_mask(plinth, BAS[1:], sigma=1.2, ambient=0.5, contrast=1.0,
                          dither_strength=0.2), 0, 0)
    # anvil: horn (left), face, waist, feet
    body = poly_mask(w, h, [(1, 6), (7, 5), (27, 5), (28, 9), (22, 10), (20, 13), (23, 17),
                            (9, 17), (12, 13), (10, 10), (6, 9)])
    lay = shade_mask(body, IRON[1:], sigma=1.4, ambient=0.5, contrast=1.1, dither_strength=0.25)
    paste(img, lay, 0, 0)
    for x in range(3, 28):
        if img[5, x, 3]:
            put(img, x, 5, IRON[6] if x < 18 else IRON[5])
    for x in range(8, 28):
        if img[6, x, 3]:
            put(img, x, 6, IRON[5] if x < 18 else IRON[4])
    # hot ingot lying on the face
    stamp(img, ["ooooooo", "oyWyaRo"], {"o": OP, "y": LAVA[3], "W": LAVA[4], "a": LAVA[2],
                                        "R": LAVA[1]}, 15, 3)
    # glowing rune on the waist
    stamp(img, ["a.a", ".R.", "a.a"], {"a": EMB, "R": LAVA[1]}, 15, 11)
    for x in range(7, 26):
        put(img, x, 23, ASH[1] if x % 2 else ASH[2])
    outline(img, OP)
    # the ingot glows: replace its outline above with a warm halo
    for x in range(15, 22):
        if tuple(img[2, x][:3]) == tuple(col(OP)[:3]):
            img[2, x] = col("#5a1c10")
    return img


def flame(img, cx, base_y, height, width, f, seed=0):
    """Layered flame tongues (outer red -> core white), animated by frame f."""
    r = rng(seed + f * 13)
    layers = [(1.0, LAVA[0]), (0.78, LAVA[1]), (0.55, LAVA[2]), (0.34, LAVA[3]), (0.16, LAVA[4])]
    for scale, c in layers:
        hh = height * scale + (1 if scale > 0.5 else 0)
        ww = width * (0.4 + 0.6 * scale)
        for y in range(int(base_y - hh), int(base_y) + 1):
            t = min(1.0, max(0.0, (base_y - y) / max(1.0, hh)))   # 0 at base, 1 at the tip
            wob = math.sin(t * 5.0 + f * 1.3 + seed) * 1.2 * t
            half = ww / 2 * (1 - t) ** 0.7
            for x in range(int(cx - half + wob - 0.5), int(cx + half + wob + 1.5)):
                if abs(x - cx - wob) <= half + 0.3:
                    put(img, x, y, c)
    # loose tongue / spark
    sx = int(cx + (r.random() - 0.5) * width)
    sy = int(base_y - height - 1 - r.random() * 3)
    put(img, sx, sy, LAVA[2])


def forge_brazier():
    fw, h, n = 24, 32, 6
    out = new(fw * n, h)
    for f in range(n):
        img = new(fw, h)
        # tripod legs + stem
        for (x0, x1) in [(12, 5), (12, 19)]:
            for (x, y) in line_pts(x0, 21, x1, 31):
                put(img, x, y, IRON[4])
        for y in range(20, 29):
            put(img, 11, y, IRON[5])
            put(img, 12, y, IRON[3])
        # bowl
        bowl = ellipse_mask(fw, h, 12, 15.5, 9.5, 5.5) & (np.arange(h)[:, None] >= 15)
        lay = shade_mask(bowl, IRON[1:], sigma=1.2, ambient=0.5, contrast=1.1, dither_strength=0.2)
        paste(img, lay, 0, 0)
        for x in range(3, 22):
            put(img, x, 15, RUST[3] if x < 12 else RUST[2])
        for x in (5, 9, 13, 17):
            put(img, x, 18, LAVA[1])            # glowing vents on the bowl
        # coals
        for x in range(5, 20):
            put(img, x, 14, [LAVA[0], LAVA[1], LAVA[2]][(x + f) % 3])
        outline(img, OP)
        flame(img, 12, 13, 12 + (f % 3), 11, f, seed=3)
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# chains
# ---------------------------------------------------------------------------
def _chain(img, x, y0, y1):
    k = 0
    y = y0
    while y < y1:
        if k % 2 == 0:
            # face-on link: 3 wide x 5 tall ring
            for (dx, dy, c) in [(-1, 0, IRON[5]), (0, 0, IRON[6]), (1, 0, IRON[4]),
                                (-1, 1, IRON[5]), (1, 1, IRON[3]), (-1, 2, IRON[4]),
                                (1, 2, IRON[3]), (-1, 3, IRON[4]), (1, 3, IRON[2]),
                                (-1, 4, IRON[3]), (0, 4, IRON[2]), (1, 4, IRON[2])]:
                if y + dy < y1:
                    put(img, x + dx, y + dy, c)
            if (y // 9) % 3 == 1:
                put(img, x + 1, y + 2, RUST[2])
        else:
            for dy in range(4):
                if y + dy < y1:
                    put(img, x, y + dy, IRON[5] if dy < 2 else IRON[4])
        y += 4
        k += 1
    return y


def hanging_chain():
    w, h = 16, 32
    img = new(w, h)
    _chain(img, 8, 0, 31)
    outline(img, OP)
    img[0, :] = np.where(img[0, :, 3:4] > 0, img[0, :], 0)
    return img


def hanging_chain_long():
    w, h = 16, 64
    img = new(w, h)
    end = _chain(img, 8, 0, 50)
    # hook
    hook = bezier((8, 50), (8, 56), (11, 58), 12)
    hook += bezier((11, 58), (13, 61), (8, 62), 10)
    hook += bezier((8, 62), (4, 61), (4, 57), 10)
    m = thick_path_mask(w, h, hook, 1.0)
    lay = shade_mask(m, IRON[2:], sigma=0.8, ambient=0.55, contrast=1.0, dither_strength=0.2)
    paste(img, lay, 0, 0)
    put(img, 4, 56, IRON[6])
    put(img, 5, 55, IRON[5])
    del end
    outline(img, OP)
    img[0, :] = np.where(img[0, :, 3:4] > 0, img[0, :], 0)
    return img


# ---------------------------------------------------------------------------
# lava fall (6 frames, hangs from a ledge: pivot top)
# ---------------------------------------------------------------------------
def lava_fall():
    fw, h, n = 24, 64, 6
    out = new(fw * n, h)
    for f in range(n):
        img = new(fw, h)
        # rock lip at the top
        lip = poly_mask(fw, h, [(0, 0), (24, 0), (23, 3), (17, 5), (7, 5), (1, 3)])
        paste(img, shade_mask(lip, BAS[1:], sigma=0.9, ambient=0.55, contrast=1.0,
                              dither_strength=0.2), 0, 0)
        for y in range(4, 58):
            t = (y - 4) / 54
            half = 3.0 + 1.6 * t
            wob = math.sin(y / 7.0) * 0.6
            cx = 12 + wob
            for x in range(int(cx - half - 1), int(cx + half + 2)):
                e = abs(x + 0.5 - cx) / half
                if e > 1.15:
                    continue
                # streaks moving down (texture offset by frame)
                s = (x * 7 + (y - f * 3) * 1) % 11
                if e > 0.9:
                    c = LAVA[0]
                elif e > 0.6:
                    c = LAVA[1] if s < 7 else LAVA[2]
                else:
                    c = LAVA[2] if s < 6 else LAVA[3]
                    if s == 0:
                        c = LAVA[4]
                put(img, x, y, c)
        # splash pool + droplets
        pool = ellipse_mask(fw, h, 12, 61.5, 11, 3.2)
        for y in range(56, 64):
            for x in range(fw):
                if pool[y, x]:
                    d = math.hypot((x - 12) / 11, (y - 61.5) / 3.2)
                    img[y, x] = col(LAVA[3] if d < 0.4 else LAVA[2] if d < 0.75 else LAVA[1])
        for k in range(6):
            a = math.pi * (0.15 + 0.7 * ((k + f * 0.5) % 6) / 5)
            rr = 4 + ((f + k) % 3) * 2
            put(img, int(12 + math.cos(a) * rr * (1 if k % 2 else -1)), int(58 - math.sin(a) * rr * 0.8),
                LAVA[2] if k % 2 else LAVA[3])
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# fire plant (4 frames)
# ---------------------------------------------------------------------------
def fire_plant():
    fw, h, n = 16, 24, 4
    out = new(fw * n, h)
    stems = [((8, 23), (6, 14), (5, 8)), ((8, 23), (11, 15), (12, 6)), ((8, 23), (8, 16), (8, 3))]
    for f in range(n):
        img = new(fw, h)
        tips = []
        for k, (a, b, c) in enumerate(stems):
            pts = path_pixels(bezier(a, b, (c[0] + [0, 1, 0, -1][(f + k) % 4] * 0.5, c[1]), 14))
            for i, (x, y) in enumerate(pts):
                put(img, x, y, "#2a1a18" if i % 3 else "#46282a")
            tips.append(pts[-1])
            # thorny leaves
            mx, my = pts[len(pts) // 2]
            put(img, mx - 1, my, "#46282a")
            put(img, mx + 1, my + 1, "#5e3426")
        # base bulb
        for (x, y, c) in [(7, 22, "#46282a"), (8, 22, "#5e3426"), (9, 22, "#46282a"),
                          (6, 23, "#2a1a18"), (10, 23, "#2a1a18"), (7, 23, "#46282a"),
                          (8, 23, "#46282a"), (9, 23, "#46282a")]:
            put(img, x, y, c)
        outline(img, OP)
        # flame buds at the tips (flicker)
        for k, (x, y) in enumerate(tips):
            ph = (f + k) % 4
            big = ph in (1, 2)
            flame(img, x, y + 1, 4 + (1 if big else 0), 3 + (1 if big else 0), f + k, seed=k)
        out[:, f * fw:(f + 1) * fw] = img
    return out


# ---------------------------------------------------------------------------
# slag pile, gear wreck, skull rock
# ---------------------------------------------------------------------------
def slag_pile():
    w, h = 32, 16
    m = blob_mask(w, h, 16, 14, 14, 9, 61, rough=0.25, flat_bottom=15)
    m |= blob_mask(w, h, 7, 13, 6, 5, 62, rough=0.3, flat_bottom=15)
    m |= blob_mask(w, h, 25, 13, 6, 4.5, 63, rough=0.3, flat_bottom=15)
    img = shade_mask(m, IRON[1:], sigma=1.4, ambient=0.48, contrast=1.1, dither_strength=0.3)
    r = rng(64)
    # rust lumps + glowing bits
    for k in range(14):
        x, y = int(3 + r.random() * 26), int(6 + r.random() * 9)
        if m[y, x]:
            put(img, x, y, RUST[1 + k % 3])
    for (x, y, c) in [(12, 10, LAVA[1]), (13, 10, LAVA[2]), (13, 11, LAVA[0]), (20, 12, LAVA[1]),
                      (6, 12, EMB), (24, 9, LAVA[0])]:
        put(img, x, y, c)
    for x in range(w):
        if m[15, x]:
            put(img, x, 15, ASH[1] if x % 2 else ASH[0])
    outline(img, OP)
    return img


def gear_wreck():
    w, h = 48, 32
    img = new(w, h)
    cx, cy, R = 24, 25, 21
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    d = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    teeth = (np.cos(ang * 12) > 0.15) & (d > R * 0.82) & (d <= R)
    # broken teeth (missing sectors)
    teeth &= ~((ang > -2.2) & (ang < -1.85)) & ~((ang > -0.75) & (ang < -0.5))
    ring = (d <= R * 0.84) & (d >= R * 0.62)
    hub = d <= R * 0.24
    spokes = np.zeros_like(ring)
    for k in range(6):
        a = k / 6 * 2 * math.pi + 0.2
        along = dx * math.cos(a) + dy * math.sin(a)
        perp = np.abs(-dx * math.sin(a) + dy * math.cos(a))
        spokes |= (along > 0) & (along < R * 0.64) & (perp < 1.6)
    m = (teeth | ring | hub | spokes) & (yy <= 27)
    lay = shade_mask(m, IRON[1:], sigma=1.6, ambient=0.5, contrast=1.1, dither_strength=0.25)
    paste(img, lay, 0, 0)
    r = rng(71)
    for k in range(40):
        x, y = int(r.random() * w), int(r.random() * 28)
        if m[y, x] and r.random() < 0.6:
            put(img, x, y, RUST[1 + k % 3])
    # hub bolt glowing (residual heat)
    stamp(img, [".a.", "aRa", ".a."], {"a": EMB, "R": LAVA[1]}, cx - 1, cy - 1)
    # half buried in ash / slag
    mound = ellipse_mask(w, h, 24, 33, 26, 7)
    for y in range(h):
        for x in range(w):
            if mound[y, x]:
                img[y, x] = col(ASH[2] if y <= 27 else (ASH[1] if y <= 29 else ASH[0]))
    for x in range(2, 46, 5):
        put(img, x, 27, ASH[3])
    outline(img, OP)
    return img


def skull_rock():
    w, h = 32, 32
    m = ellipse_mask(w, h, 16, 14, 13, 12) | poly_mask(w, h, [(6, 18), (26, 18), (24, 29), (8, 29)])
    m |= blob_mask(w, h, 16, 29, 15, 4, 81, flat_bottom=31)
    img = shade_mask(m, BAS[1:], sigma=2.2, ambient=0.5, contrast=1.15, dither_strength=0.3)
    # eye sockets with ember light deep inside
    for (ex, ey) in [(10, 14), (21, 14)]:
        sock = ellipse_mask(w, h, ex + 0.5, ey + 0.5, 3.6, 3.2)
        img[sock] = col(BAS[0])
        put(img, ex, ey + 1, EMB)
        put(img, ex + 1, ey + 1, LAVA[0])
        put(img, ex + 1, ey, EMB)
    # nasal cavity + teeth
    stamp(img, [".k.", "kkk", "k.k"], {"k": BAS[0]}, 14, 18)
    for x in range(9, 24, 3):
        for y in (23, 24):
            put(img, x, y, BAS[1])
        put(img, x + 1, 23, BAS[5])
    # cracks
    for (x, y) in line_pts(7, 6, 11, 9) + line_pts(22, 3, 20, 8):
        put(img, x, y, BAS[1])
    ash_top(img, m, 2)
    outline(img, OP)
    return img


# ---------------------------------------------------------------------------
# ember crystal, steam pipe, burnt banner, ash rocks
# ---------------------------------------------------------------------------
def ember_crystal():
    w, h = 16, 24
    img = new(w, h)
    for (bx, by, ht, wd, ln) in [(4, 23, 10, 4, -2), (12, 23, 12, 4, 2), (8, 23, 21, 6, 0.5),
                                 (10, 23, 7, 3, 1)]:
        paste(img, prism(w, h, bx, by, ht, wd, ln, ramp=LAVA_P), 0, 0)
    for (x, y) in [(8, 5), (7, 10), (11, 14)]:
        put(img, x, y, LAVA[4])
    for x in range(2, 15):
        put(img, x, 23, BAS[3] if x % 3 else ASH[1])
        if 4 < x < 12:
            put(img, x, 22, BAS[3])
    outline(img, OP)
    return img


def steam_pipe():
    w, h = 32, 32
    img = new(w, h)
    # vertical pipe from the ground, elbow to the right, broken mouth
    m = np.zeros((h, w), bool)
    m[12:31, 7:14] = True
    m |= ellipse_mask(w, h, 13, 12, 6.5, 6.5) & (np.arange(w)[None, :] >= 10) & (np.arange(h)[:, None] <= 15)
    m[6:13, 13:22] = True
    lay = shade_mask(m, IRON[1:], sigma=1.4, ambient=0.5, contrast=1.15, dither_strength=0.25)
    paste(img, lay, 0, 0)
    # flanges + rust
    for y in range(5, 14):
        put(img, 21, y, IRON[5])
        put(img, 22, y, IRON[3])
    for x in range(6, 15):
        put(img, x, 22, IRON[5])
        put(img, x, 23, IRON[2])
    for (x, y) in [(9, 26), (10, 18), (16, 8), (11, 28)]:
        put(img, x, y, RUST[2])
        put(img, x, y + 1, RUST[1])
    # valve wheel
    stamp(img, [".ooo.", "oTYTo", "oYrYo", "oTYTo", ".ooo."],
          {"o": OP, "T": RUST[2], "Y": RUST[3], "r": LAVA[0]}, 3, 15)
    # ground
    for x in range(4, 18):
        put(img, x, 31, ASH[1] if x % 2 else ASH[2])
    outline(img, OP)
    # hot mouth + steam puff (translucent)
    for y in range(7, 12):
        put(img, 23, y, LAVA[1] if y in (8, 10) else EMB)
    puffs = [(26, 7, 2.6, 0), (28, 4, 3.0, 1), (29, 1, 2.4, 2)]
    for (px, py, pr, k) in puffs:
        pm = ellipse_mask(w, h, px, py, pr, pr * 0.85)
        for y in range(h):
            for x in range(w):
                if pm[y, x] and img[y, x, 3] == 0:
                    img[y, x] = SMOKE[min(2, k + (1 if y < py else 0))]
    return img


def burnt_banner():
    w, h = 24, 48
    img = new(w, h)
    for y in range(4, 46):
        put(img, 4, y, IRON[5])
        put(img, 5, y, IRON[3])
    stamp(img, [".a.", "aRy", ".a."], {"a": LAVA[0], "R": LAVA[1], "y": LAVA[3]}, 3, 1)
    for x in range(3, 22):
        put(img, x, 6, IRON[5])
        put(img, x, 7, IRON[3])
    bottom = [36, 31, 38, 33, 28, 35, 30, 26, 32, 37, 29, 34, 38, 31, 27]
    holes = {(9, 20), (10, 20), (9, 21), (16, 26), (17, 26), (17, 27), (12, 14)}
    for i, x in enumerate(range(6, 21)):
        fold = math.sin((x - 6) / 14 * math.pi * 3) * 0.9
        for y in range(8, bottom[i]):
            if (x, y) in holes:
                continue
            f = 2.2 + fold - (y - 8) / 50.0
            idx = int(np.clip(round(f + (0.3 if (x + y) % 2 else 0)), 0, 4))
            img[y, x] = col(CLOTH[idx])
        # charred, glowing torn edge
        put(img, x, bottom[i] - 1, LAVA[0] if i % 3 else LAVA[1])
        put(img, x, bottom[i] - 2, "#2a1012" if i % 2 else CLOTH[1])
        if i % 4 == 1:
            put(img, x, bottom[i], LAVA[2])
    # glowing rims of the burnt holes
    for (x, y) in holes:
        for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + dx, y + dy) not in holes and img[y + dy, x + dx, 3]:
                put(img, x + dx, y + dy, EMB)
    # emblem: an anvil and a flame (faded gold)
    stamp(img, ["...t...", "..tat..", ".tayat.", ".taya.t", "t.ayyat", "ta.yy.t", ".tayyat",
                "..ttt.."], {"a": LAVA[2], "y": LAVA[3], "t": RUST[2]}, 10, 11)
    for x in range(6, 21):
        put(img, x, 8, CLOTH[4])
        put(img, x, 9, RUST[1] if x % 2 else RUST[2])
    for (x, y, c) in [(2, 46, BAS[3]), (3, 46, BAS[4]), (6, 46, BAS[3]), (7, 47, BAS[2]),
                      (2, 47, ASH[1]), (3, 47, ASH[2]), (4, 47, ASH[1]), (5, 47, ASH[2]),
                      (6, 47, ASH[1]), (1, 47, ASH[0]), (4, 46, IRON[3]), (5, 46, IRON[2])]:
        put(img, x, y, c)
    outline(img, OP)
    return img


def ash_rock_small():
    w, h = 16, 12
    m = blob_mask(w, h, 8, 8, 6.6, 5.2, 91, flat_bottom=11)
    img = shade_mask(m, BAS[1:], sigma=1.2, ambient=0.5, contrast=1.1, dither_strength=0.2)
    ash_top(img, m, 1)
    put(img, 6, 6, BAS[6])
    put(img, 10, 9, EMB)
    outline(img, OP)
    return img


def ash_rock_big():
    w, h = 32, 24
    m = blob_mask(w, h, 16, 16, 14, 10.5, 93, rough=0.14, flat_bottom=23)
    m |= blob_mask(w, h, 25, 19, 6, 5, 94, flat_bottom=23)
    img = shade_mask(m, BAS[1:], sigma=2.0, ambient=0.5, contrast=1.1, dither_strength=0.2)
    pts = line_pts(11, 9, 15, 15) + line_pts(15, 15, 14, 20)
    for (x, y) in pts:
        put(img, x, y, BAS[0])
    for (x, y, c) in [(12, 10, LAVA[0]), (13, 12, LAVA[1]), (14, 14, LAVA[2]), (15, 16, LAVA[1]),
                      (14, 18, LAVA[0])]:
        put(img, x, y, c)
    ash_top(img, m, 2, ember_every=11, seed=3)
    outline(img, OP)
    return img


# name: (painter, frames, fps, pivot, glow)  glow = (colour, radius, (ox, oy))
SPRITES = {
    "lava_vent": (lava_vent, 6, 8, "bottom", ("#ff8a3d", 36, (0, -14))),
    "basalt_column": (basalt_column, 1, 0, "bottom", ("#ff4d1e", 12, (3, -24))),
    "basalt_column_broken": (basalt_column_broken, 1, 0, "bottom", None),
    "obsidian_spire": (obsidian_spire, 1, 0, "bottom", None),
    "forge_anvil": (forge_anvil, 1, 0, "bottom", ("#ff8a3d", 16, (3, -20))),
    "forge_brazier": (forge_brazier, 6, 10, "bottom", ("#ff8a3d", 44, (0, -22))),
    "hanging_chain": (hanging_chain, 1, 0, "top", None),
    "hanging_chain_long": (hanging_chain_long, 1, 0, "top", None),
    "lava_fall": (lava_fall, 6, 10, "top", ("#ff8a3d", 48, (0, 34))),
    "fire_plant": (fire_plant, 4, 8, "bottom", ("#ff8a3d", 20, (0, -16))),
    "slag_pile": (slag_pile, 1, 0, "bottom", None),
    "gear_wreck": (gear_wreck, 1, 0, "bottom", None),
    "skull_rock": (skull_rock, 1, 0, "bottom", ("#ff4d1e", 14, (0, -17))),
    "ember_crystal": (ember_crystal, 1, 0, "bottom", ("#ff8a3d", 26, (0, -11))),
    "steam_pipe": (steam_pipe, 1, 0, "bottom", None),
    "burnt_banner": (burnt_banner, 1, 0, "bottom", None),
    "ash_rock_small": (ash_rock_small, 1, 0, "bottom", None),
    "ash_rock_big": (ash_rock_big, 1, 0, "bottom", None),
}


def build():
    return build_atlas(SPRITES, "sprites/decor/pyralis_decor.png")


def generate():
    atlas, meta = build()
    return [save_png(atlas, "sprites/decor/pyralis_decor.png"),
            save_json(meta, "sprites/decor/pyralis_decor.json")]
