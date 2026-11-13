"""Static / tile-like object sprites (no JSON): moving & crumble platforms
(3-slice), spikes, laser beam segments and the three liquids."""
import math

import numpy as np

import materials as M
from common.palette import (ACID, AMBER, CYAN, DANGER, MOSS, OUTLINE, SKY, STONE, SUIT, VIOLET, hex_rgba)
from raster import Canvas, Mat, put
from sheet import save_png

TAU = 2 * math.pi


def _strip_shape(cv, y0, y1, mat, cap_round=4.0, bevel=1.5, base=0.6):
    """A 48px wide slab from y0 to y1 with rounded ends (caps)."""
    pts = [(1.0, y0 + cap_round * 0.5), (1.0 + cap_round * 0.6, y0), (47.0 - cap_round * 0.6, y0),
           (47.0, y0 + cap_round * 0.5), (47.0, y1 - cap_round * 0.6), (47.0 - cap_round, y1),
           (1.0 + cap_round, y1), (1.0, y1 - cap_round * 0.6)]
    cv.poly(pts, mat, bevel=bevel, base=base)


def moving_platform():
    img = np.zeros((32, 48, 4), np.uint8)
    for row, lit in ((0, False), (1, True)):
        cv = Canvas(48, 16)
        cv.part()
        _strip_shape(cv, 0.0, 9.5, M.METAL, cap_round=3.0, bevel=1.4, base=0.6)
        # stone top plate (walkable surface: first 2 rows)
        cv.part()
        cv.poly([(1.0, 1.5), (2.5, 0.0), (45.5, 0.0), (47.0, 1.5), (47.0, 3.5), (1.0, 3.5)], M.STONE_M, bevel=1.0,
                base=0.66)
        # trim band
        cv.part(line=False)
        cv.line((2, 6), (45, 6), M.STONE_DARK, inten=0.2, decal=True)
        # amber lights every 8 px (period 16 in the middle cell)
        lm = M.AMBER_GLOW
        for x in (5, 13, 21, 29, 37, 43):
            cv.px_set(x, 7, lm, inten=0.95 if lit else 0.2, decal=True)
            cv.px_set(x + 1, 7, lm, inten=0.7 if lit else 0.1, decal=True)
        # rivets
        for x in (9, 17, 25, 33, 41):
            cv.px_set(x, 4, M.STONE_DARK, inten=0.2, decal=True)
        # underside emitter in the middle of each cell
        cv.part()
        for cx in (8, 24, 40):
            cv.poly([(cx - 3, 9.0), (cx + 3, 9.0), (cx + 2, 11.0), (cx - 2, 11.0)], M.METAL, flat=0.3)
        part = cv.render()
        # glow under the platform (soft, semi-transparent, x-periodic)
        glow = np.zeros((16, 48, 4), np.uint8)
        col = AMBER[1] if lit else CYAN[1]
        for y in range(11, 16):
            for x in range(48):
                k = 1.0 - (y - 11) / 5.0
                d = abs(((x + 0.5) % 16) - 8) / 8.0
                a = (0.55 if lit else 0.32) * k * (1.0 - 0.6 * d)
                if x < 3 or x > 44:
                    a *= 0.4
                if a > 0.04:
                    glow[y, x] = (col[0], col[1], col[2], int(255 * a))
        # bright emitter pixels
        for cx in (8, 24, 40):
            for dx in (-1, 0):
                put(part, cx + dx, 11, AMBER[2] if lit else CYAN[2])
        sel = part[:, :, 3] > 0
        glow[sel] = part[sel]
        img[row * 16:(row + 1) * 16] = glow
    save_png(img, "sprites/objects/moving_platform.png")


def crumble_platform():
    img = np.zeros((48, 48, 4), np.uint8)
    rng = np.random.default_rng(31)
    # x-periodic (16 px) bottom profile so the middle slice tiles
    prof = [9.5 + 2.5 * abs(math.sin(TAU * x / 16 * 1.0 + 0.7)) + (1.0 if x % 16 in (5, 6, 11) else 0) for x in range(48)]
    for stage in range(3):
        cv = Canvas(48, 16)
        cv.part()
        pts = [(1.0, 2.0), (2.5, 0.5), (45.5, 0.5), (47.0, 2.0)]
        for x in range(46, 1, -1):
            pts.append((x + 0.5, prof[x] if 3 < x < 44 else prof[x] - 2))
        pts.append((1.0, 7.0))
        cv.poly(pts, M.STONE_M, bevel=1.6, base=0.6)
        # moss lip on top
        cv.part(line=False)
        for x in range(1, 47):
            h = 1.5 + 0.8 * math.sin(TAU * x / 16 * 2)
            for y in range(0, int(h) + 1):
                cv.px_set(x, y, M.MOSS_M, inten=0.85 if y == 0 else 0.55, decal=True)
            if x % 16 in (3, 10) and stage < 2:
                cv.px_set(x, 2 + int(h), M.MOSS_M, inten=0.4, decal=True)
        # glowing moss dots
        for x in (6, 22, 38):
            cv.px_set(x, 1, Mat([CYAN[2]], emissive=True, name="gm"), inten=0.9, decal=True)
        # block seams (periodic)
        for x in (12, 28, 44):
            cv.line((x, 3), (x - 1, 9), M.STONE_DARK, inten=0.2, decal=True)
        # cracks per stage
        crack = Mat([OUTLINE], name="crk")
        if stage >= 1:
            for x0 in (4, 20, 36):
                cv.line((x0, 3), (x0 + 3, 7), crack, decal=True)
                cv.line((x0 + 3, 7), (x0 + 2, 10), crack, decal=True)
                cv.line((x0 + 8, 4), (x0 + 10, 8), crack, decal=True)
        if stage >= 2:
            for x0 in (0, 16, 32):
                cv.line((x0 + 13, 2), (x0 + 15, 6), crack, decal=True)
                cv.line((x0 + 15, 6), (x0 + 13, 11), crack, decal=True)
                cv.line((x0 + 6, 9), (x0 + 9, 12), crack, decal=True)
        part = cv.render()
        if stage >= 2:
            # chunks breaking off underneath + falling pebbles
            for x0 in (0, 16, 32):
                for (x, y) in ((x0 + 7, 13), (x0 + 12, 14)):
                    if part[y, x, 3] == 0:
                        part[y, x] = STONE[3]
                        if y + 1 < 16:
                            part[y + 1, x] = OUTLINE
        img[stage * 16:(stage + 1) * 16] = part
    save_png(img, "sprites/objects/crumble_platform.png")
    crumble_variants(np.array(img) if not isinstance(img, np.ndarray) else img)


# Planet variants of the crumbling slab: exact palette swaps of the Sylvanox version.
CRUMBLE_VARIANTS = {
    "basalt": {(125, 151, 168): (140, 100, 90), (87, 114, 134): (90, 60, 52), (59, 84, 102): (64, 40, 36),
               (42, 62, 79): (44, 26, 26), (29, 43, 58): (30, 17, 19), (11, 15, 30): (20, 9, 11),
               (19, 92, 80): (179, 32, 26), (53, 181, 131): (255, 138, 61), (143, 255, 240): (255, 241, 194)},
    "ice": {(125, 151, 168): (238, 250, 255), (87, 114, 134): (168, 228, 255), (59, 84, 102): (95, 176, 255),
            (42, 62, 79): (47, 111, 179), (29, 43, 58): (31, 58, 102), (11, 15, 30): (10, 20, 40),
            (19, 92, 80): (201, 216, 234), (53, 181, 131): (255, 255, 255), (143, 255, 240): (127, 246, 255)},
}


def crumble_variants(base):
    base = np.asarray(base)
    for style, mapping in CRUMBLE_VARIANTS.items():
        out = base.copy()
        for (r, g, b), (nr, ng, nb) in mapping.items():
            m = (base[..., 0] == r) & (base[..., 1] == g) & (base[..., 2] == b) & (base[..., 3] > 0)
            out[m, 0], out[m, 1], out[m, 2] = nr, ng, nb
        save_png(out, f"sprites/objects/crumble_platform_{style}.png")


def spikes():
    W, H = 32, 16
    cv = Canvas(W, H)
    crystal = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], name="spike", thresholds=[0.84, 0.62, 0.36])
    tip = Mat([hex_rgba('#ffffff'), hex_rgba('#f6dcff')], name="tip", thresholds=[0.5])
    variants = [
        [(3.5, 5.5, 2.7), (8.0, 1.0, 3.1), (12.5, 4.5, 2.7)],
        [(2.5, 7.0, 2.3), (6.5, 2.5, 2.7), (10.5, 4.0, 2.6), (14.0, 8.0, 2.0)],
    ]
    for v, spk in enumerate(variants):
        ox = v * 16
        for (x, ty, w) in spk:
            cv.part()
            cv.poly([(ox + x - w, 14.5), (ox + x, ty), (ox + x + w, 14.5)], crystal, bevel=1.2, base=0.55)
            cv.part(line=False)
            cv.px_set(ox + x - 0.5, ty + 1.0, tip, inten=0.9, decal=True)
        # rock base (full width => tileable)
        cv.part()
        cv.poly([(ox + 0.0, 13.0), (ox + 16.0, 13.0), (ox + 16.0, 16.0), (ox + 0.0, 16.0)], M.STONE_DARK, bevel=1.0,
                base=0.6)
    img = cv.render(outline=False)
    # outline only around the spikes (not along the tile's side edges)
    from raster import outline_rgba
    top = img.copy()
    top[13:] = 0
    o = outline_rgba(top.copy())
    sel = (o[:, :, 3] > 0) & (img[:, :, 3] == 0)
    img[sel] = o[sel]
    img[12, :] = np.where(img[12, :, 3:4] == 0, np.array(OUTLINE, np.uint8), img[12, :])
    save_png(img, "sprites/objects/spikes.png")


def laser_beam():
    W, H = 64, 8
    img = np.zeros((H, W, 4), np.uint8)
    for f in range(4):
        for x in range(16):
            wob = math.sin(TAU * x / 16 * 2 + TAU * f / 4)
            for y in range(H):
                d = abs(y + 0.5 - 4.0)
                if d < 0.6 + 0.2 * (wob > 0.5):
                    c = (255, 255, 255, 255)
                elif d < 1.6:
                    c = hex_rgba('#ffd0dc')
                elif d < 2.6:
                    c = DANGER[1]
                elif d < 3.4 + 0.6 * wob:
                    c = (DANGER[0][0], DANGER[0][1], DANGER[0][2], 150)
                else:
                    continue
                img[y, f * 16 + x] = c
        # travelling sparkle
        sx = (f * 4 + 2) % 16
        img[2, f * 16 + sx] = hex_rgba('#ffd0dc')
        img[5, f * 16 + (sx + 8) % 16] = hex_rgba('#ffd0dc')
    save_png(img, "sprites/objects/laser_beam.png")


# --------------------------------------------------------------- liquids
LIQUIDS = {
    "acid": dict(crest=ACID[3], foam=ACID[2], body=[ACID[2], ACID[1], ACID[0]], alpha=235,
                 bubbles=ACID[3], glow=True),
    "water": dict(crest=CYAN[2], foam=CYAN[0], body=[MOSS[2], MOSS[1], MOSS[0]], alpha=200,
                  bubbles=CYAN[1], glow=False),
    "lava": dict(crest=AMBER[2], foam=AMBER[1], body=[AMBER[1], DANGER[2], hex_rgba('#d9442a')],
                 alpha=255, bubbles=AMBER[2], glow=True),
}


# drifting current streaks (x0, y, length, direction): 2 px per frame -> loops in 8 frames
STREAKS = [(2, 2, 4, 1, 0), (9, 6, 3, -1, 2), (5, 9, 5, 1, 0), (12, 12, 3, 1, 0), (0, 14, 4, -1, 2),
           (8, 4, 2, -1, 2)]


def _liquid_px(kind, x, y, t, depth):
    L = LIQUIDS[kind]
    ramp = L["body"]
    c = ramp[1]
    for (x0, sy, ln, d, tone) in STREAKS:
        if y != sy:
            continue
        xs = (x - x0 - 2 * t * d) % 16
        if xs < ln:
            c = ramp[tone]
    return (c[0], c[1], c[2], L["alpha"])


def liquid(kind):
    L = LIQUIDS[kind]
    img = np.zeros((32, 128, 4), np.uint8)
    rng = np.random.default_rng({"acid": 1, "water": 2, "lava": 3}[kind])
    bubbles = [(int(rng.integers(0, 16)), int(rng.integers(0, 16))) for _ in range(3)]
    for f in range(8):
        ox = f * 16
        # surface tile
        for x in range(16):
            crest = 3.0 + 1.2 * math.sin(TAU * x / 16 - TAU * f / 8) + 0.6 * math.sin(TAU * 2 * x / 16 + TAU * f / 8 * 2)
            cy = int(round(crest))
            for y in range(16):
                if y < cy:
                    continue
                if y == cy:
                    c = L["crest"]
                    img[y, ox + x] = (c[0], c[1], c[2], 255 if kind != "water" else 235)
                elif y == cy + 1:
                    c = L["foam"]
                    img[y, ox + x] = (c[0], c[1], c[2], max(L["alpha"], 220))
                elif y == cy + 2 and (x + f) % 3 != 0:
                    c = L["body"][0]
                    img[y, ox + x] = (c[0], c[1], c[2], L["alpha"])
                else:
                    img[y, ox + x] = _liquid_px(kind, x, y, f, 0.0)
        # body tile
        for x in range(16):
            for y in range(16):
                img[16 + y, ox + x] = _liquid_px(kind, x, y, f, 0.0)
        # rising bubbles (2 px per frame -> loops in 8 frames), in both tiles
        for (bx, by) in bubbles:
            y = (by - 2 * f) % 16
            for base in (0, 16):
                if base == 0 and y < 7:
                    continue
                c = L["bubbles"]
                img[base + y, ox + bx] = (c[0], c[1], c[2], 255)
        # lava crust flecks / acid glints drifting with the surface
        if kind == "lava":
            for k in range(2):
                x = (5 + k * 8 + f * 2) % 16
                img[16 + (3 + k * 7) % 16, ox + x] = (122, 26, 18, 255)
    save_png(img, f"sprites/objects/liquid_{kind}.png")


def build():
    moving_platform()
    crumble_platform()
    spikes()
    laser_beam()
    for k in ("acid", "water", "lava"):
        liquid(k)
