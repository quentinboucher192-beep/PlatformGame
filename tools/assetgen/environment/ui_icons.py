"""UI icon sheets (ASSET_SPEC section 11):
  ui/icons_powers.png 320x32  dash, wave, shield, impulse, teleport, glide, ground_pound,
                              charge_shot, grapple, triple_jump (cells 5-9: ui_icons_v2.py),
                              time_slow, magnet, air_dash, plasma_bomb (cells 10-13: ui_icons_v3.py)
  ui/icons_medals.png 160x32  none, bronze, silver, gold, platinum
  ui/icons_hud.png    176x16  heart_full, heart_empty, energy, shard, relic, skull, clock,
                              enemy, star, lock, check
"""
import math

import numpy as np

from helpers import (P, apply_ramp, bayer, col, new, outline, poly_mask, put, save_png, shade_mask,
                     thick_path_mask)
from sprites import stamp

O = P.OUTLINE
CY = ["#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
VI = ["#6a2bb8", "#b04cff", "#e08aff", "#f4d4ff"]
AM = ["#c8741f", "#ffb347", "#ffd98a", "#fff1c8"]


def grid(n):
    yy, xx = np.mgrid[0:n, 0:n]
    return xx + 0.5, yy + 0.5


# ---------------------------------------------------------------------------
# power icons
# ---------------------------------------------------------------------------
def badge(accent_dark, accent):
    img = new(32, 32)
    x, y = grid(32)
    d = np.hypot(x - 16, y - 16)
    diag = (x - 16) + (y - 16)
    fill = d <= 12.6
    t = np.clip(0.5 - diag / 40.0, 0, 1)
    body = apply_ramp(t, ["#0b0f1e", "#0f1528", "#141d38", "#1a2747"], bayer(32, 32), 0.7)
    img[fill] = body[fill]
    inner = (d > 12.6) & (d <= 13.6)
    img[inner] = col("#0b0f1e")
    rim = (d > 13.6) & (d <= 15.0)
    img[rim] = col(accent_dark)
    img[rim & (diag < -6)] = col(accent)
    img[rim & (diag > 12)] = col("#1a2747")
    img[(d > 15.0) & (d <= 15.9)] = col(O)
    return img


def glow_halo(img, sym, c):
    """Dim 1px halo around a symbol mask, inside the badge."""
    h = np.zeros_like(sym)
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        h |= np.roll(np.roll(sym, dy, 0), dx, 1)
    h &= ~sym
    x, y = grid(32)
    h &= np.hypot(x - 16, y - 16) <= 12.6
    img[h] = col(c)


def paint_symbol(img, m, ramp, light=(-1.0, -0.8)):
    """Shade a symbol mask with a 4-tone ramp (lit top-left)."""
    lay = shade_mask(m, ramp, sigma=1.0, ambient=0.62, contrast=0.9, light=light,
                     dither_strength=0.3)
    img[m] = lay[m]


def icon_dash():
    img = badge("#1f6a7a", "#39f0d8")
    m = poly_mask(32, 32, [(10, 8), (15, 8), (22, 16), (15, 24), (10, 24), (17, 16)])
    m2 = poly_mask(32, 32, [(17, 10), (21, 10), (26, 16), (21, 22), (17, 22), (22, 16)])
    lines = np.zeros((32, 32), bool)
    for (x0, x1, yy) in [(5, 10, 12), (3, 9, 16), (5, 10, 20)]:
        lines[yy, x0:x1] = True
    glow_halo(img, m | m2 | lines, "#123f4a")
    paint_symbol(img, m, [CY[0], CY[1], CY[2], CY[3]])
    paint_symbol(img, m2, ["#178f8a", CY[0], CY[1], CY[2]])
    for (x0, x1, yy) in [(5, 10, 12), (3, 9, 16), (5, 10, 20)]:
        for x in range(x0, x1):
            put(img, x, yy, CY[0] if x < x0 + 2 else CY[1])
    return img


def icon_wave():
    img = badge("#1f6a7a", "#39f0d8")
    x, y = grid(32)
    a = np.hypot(x - 15, y - 16) <= 10.5
    b = np.hypot(x - 10.5, y - 16) <= 10.0
    cres = a & ~b
    a2 = np.hypot(x - 9, y - 16) <= 7.5
    b2 = np.hypot(x - 5.8, y - 16) <= 7.3
    cres2 = a2 & ~b2 & (np.hypot(x - 16, y - 16) <= 12.4)
    glow_halo(img, cres | cres2, "#123f4a")
    paint_symbol(img, cres, [CY[0], CY[1], CY[2], CY[3]])
    paint_symbol(img, cres2, ["#178f8a", CY[0], CY[1], CY[1]])
    for (px, py, c) in [(20, 7, CY[2]), (22, 25, CY[1]), (7, 9, CY[0]), (8, 23, CY[0])]:
        put(img, px, py, c)
    return img


def icon_shield():
    img = badge("#1f4a7a", "#5ab8e8")
    x, y = grid(32)
    ang = np.arctan2(y - 16, x - 16)
    # hexagon radius in polar form
    k = np.cos(math.pi / 6) / np.cos(((ang + math.pi / 6) % (math.pi / 3)) - math.pi / 6)
    r = np.hypot(x - 16, y - 16)
    hexo = r <= 10.5 * k
    hexi = r <= 8.2 * k
    ring = hexo & ~hexi
    core = r <= 4.2 * k
    glow_halo(img, hexo, "#123f4a")
    img[hexi] = col("#123a4a")
    inner = hexi & ~(r <= 7.2 * k)
    img[inner & ((x - 16) + (y - 16) < 0)] = col("#1a5a6a")
    paint_symbol(img, ring, [CY[0], CY[1], CY[2], CY[3]])
    paint_symbol(img, core, [CY[0], CY[1], CY[2], CY[3]])
    # highlight glint on the bubble
    for (px, py) in [(11, 11), (12, 10), (13, 10)]:
        put(img, px, py, CY[3])
    return img


def icon_impulse():
    img = badge("#7a4a1a", "#ffb347")
    x, y = grid(32)
    r = np.hypot(x - 16, y - 16)
    ang = np.degrees(np.arctan2(y - 16, x - 16)) % 360
    orb = r <= 3.8
    ring = (r > 6.6) & (r <= 8.0)
    outer = (r > 10.3) & (r <= 11.6) & ((ang % 90) > 18) & ((ang % 90) < 72)
    rays = (r > 3.8) & (r <= 6.0) & (((ang + 22.5) % 45) < 9)
    sym = orb | ring | outer | rays
    glow_halo(img, sym, "#4a2a12")
    paint_symbol(img, ring | outer, [AM[0], AM[0], AM[1], AM[2]])
    img[rays] = col(AM[1])
    paint_symbol(img, orb, [AM[1], AM[2], AM[3], "#ffffff"])
    return img


def icon_teleport():
    img = badge("#4a2a7a", "#b04cff")
    m = np.zeros((32, 32), bool)
    for arm in (0, math.pi):
        pts = []
        for i in range(60):
            th = i / 59 * 2.6 * math.pi
            rr = 1.5 + th * 1.05
            pts.append((16 + rr * math.cos(th + arm), 16 + rr * math.sin(th + arm)))
        m |= thick_path_mask(32, 32, pts, [0.6 + 0.9 * i / 59 for i in range(60)])
    x, y = grid(32)
    m &= np.hypot(x - 16, y - 16) <= 12.2
    glow_halo(img, m, "#2a1650")
    paint_symbol(img, m, [VI[0], VI[1], VI[2], VI[3]])
    for (px, py, c) in [(16, 16, "#ffffff"), (15, 16, VI[3]), (24, 8, VI[2]), (7, 23, VI[2]),
                        (25, 22, VI[1]), (8, 9, VI[1])]:
        put(img, px, py, c)
    return img


# ---------------------------------------------------------------------------
# medals
# ---------------------------------------------------------------------------
MEDAL_RAMPS = {
    "bronze": ["#3a1e10", "#6e3a1c", "#a35a28", "#c8741f", "#e8a060", "#ffd0a0"],
    "silver": ["#2a3e4f", "#5d7189", "#8fa3b8", "#c3d0de", "#e8eef5", "#ffffff"],
    "gold": ["#4a2a08", "#8a5a10", "#c8901a", "#ffc83a", "#ffe58a", "#fff6d0"],
    "platinum": ["#1f3550", "#3f7090", "#7fbccc", "#bfeaee", "#e6fffb", "#ffffff"],
}
RIBBON = {"bronze": "#a3123a", "silver": "#2e4a73", "gold": "#1f8a6a", "platinum": "#6a2bb8"}


def star_mask(n, cx, cy, r_out, r_in, rot=-90):
    pts = []
    for i in range(10):
        a = math.radians(rot + i * 36)
        rr = r_out if i % 2 == 0 else r_in
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return poly_mask(n, n, pts)


def medal(kind):
    img = new(32, 32)
    x, y = grid(32)
    cx, cy, R = 16, 19.5, 10.4
    ribbon_l = poly_mask(32, 32, [(7, 0), (13, 0), (17, 11), (12, 12)])
    ribbon_r = poly_mask(32, 32, [(19, 0), (25, 0), (20, 12), (15, 11)])
    disc = np.hypot(x - cx, y - cy) <= R
    if kind == "none":
        # empty dashed outline
        ring = disc & ~(np.hypot(x - cx, y - cy) <= R - 1.2)
        ang = np.degrees(np.arctan2(y - cy, x - cx))
        dash = ring & ((ang.astype(int) // 20) % 2 == 0)
        img[dash] = col("#3b5466")
        for m in (ribbon_l, ribbon_r):
            edge = m & ~(np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1))
            img[edge & ~disc] = col("#2a3e4f")
        st = star_mask(32, cx, cy, 5.5, 2.4)
        st_e = st & ~(np.roll(st, 1, 0) & np.roll(st, -1, 0) & np.roll(st, 1, 1) & np.roll(st, -1, 1))
        img[st_e] = col("#2a3e4f")
        return img
    ramp = MEDAL_RAMPS[kind]
    rb = RIBBON[kind]
    # ribbons with a centre stripe
    for m in (ribbon_l, ribbon_r):
        img[m] = col(rb)
    for yy in range(0, 12):
        put(img, 10 + yy * 0.33, yy, ramp[3])
        put(img, 22 - yy * 0.33, yy, ramp[2])
    # dark under-ribbon shading
    for xx in range(13, 20):
        put(img, xx, 11, "#0b0f1e")
    # medal disc
    face = shade_mask(disc, ramp, sigma=2.0, ambient=0.55, contrast=1.0, dither_strength=0.35)
    img[disc] = face[disc]
    rim = disc & ~(np.hypot(x - cx, y - cy) <= R - 1.6)
    diag = (x - cx) + (y - cy)
    img[rim & (diag < 0)] = col(ramp[4])
    img[rim & (diag >= 0)] = col(ramp[1])
    inner = (np.hypot(x - cx, y - cy) <= R - 1.6) & ~(np.hypot(x - cx, y - cy) <= R - 2.6)
    img[inner & (diag < 0)] = col(ramp[2])
    img[inner & (diag >= 0)] = col(ramp[4])
    st = star_mask(32, cx, cy, 5.6, 2.5)
    st_l = shade_mask(st, ramp[2:], sigma=0.8, ambient=0.6, contrast=1.0, dither_strength=0.2)
    img[st] = st_l[st]
    # emboss: dark lower-right edge, bright upper-left edge
    for yy in range(32):
        for xx in range(32):
            if not st[yy, xx]:
                continue
            if not st[min(31, yy + 1), xx] or not st[yy, min(31, xx + 1)]:
                img[yy, xx] = col(ramp[1])
            elif not st[max(0, yy - 1), xx] or not st[yy, max(0, xx - 1)]:
                img[yy, xx] = col(ramp[5])
    put(img, cx - 1, cy - 4, ramp[5])
    if kind == "platinum":
        put(img, cx, cy, CY[2])
        put(img, cx - 1, cy, CY[3])
        for (px, py) in [(8, 13), (25, 25)]:
            put(img, px, py, "#ffffff")
    # glint
    put(img, cx - 6, cy - 5, ramp[5])
    put(img, cx - 5, cy - 6, ramp[5])
    outline(img, O)
    return img


# ---------------------------------------------------------------------------
# HUD icons 16x16 (hand-made, symmetric ones mirrored from a left half)
# ---------------------------------------------------------------------------
def sym(half, right_sub=None):
    right_sub = right_sub or {}
    rows = []
    for r in half:
        rr = "".join(right_sub.get(c, c) for c in r[::-1])
        rows.append(r + rr)
    return rows


HEART_HALF = [
    "........",
    "........",
    "...ooo..",
    "..oPRRo.",
    ".oPWRRRo",
    ".oPRRRRR",
    ".oRRRRRR",
    ".oRRRRRR",
    "..oRRRRR",
    "...oRRRR",
    "....oRRR",
    ".....oRR",
    "......oR",
    ".......o",
    "........",
    "........",
]

SKULL_HALF = [
    "........",
    "........",
    ".....ooo",
    "...ooWSS",
    "..oWWSSS",
    "..oWSSSS",
    ".oWSSSSS",
    ".oSooSSS",
    ".oSooSSS",
    ".oSSSSSo",
    "..oSSSSo",
    "...oSSSS",
    "...oSoSo",
    "...ooooo",
    "........",
    "........",
]

ENEMY_HALF = [
    "........",
    "..o.....",
    "..oo....",
    "..oho...",
    "...ohooo",
    "...oDDDD",
    "..oDDDDD",
    ".oDRRDDD",
    ".oDDERDD",
    ".oDDDDDD",
    ".oDDDDDD",
    "..oDYDYD",
    "..oDoDoD",
    "...ooooo",
    "........",
    "........",
]

LOCK_HALF = [
    "........",
    "......oo",
    ".....oMM",
    "....oMoo",
    "....oMo.",
    "....oMo.",
    "..oooooo",
    "..ohhhhh",
    "..ohMMMM",
    "..ohMMMa",
    "..ohMMMA",
    "..ohMMMa",
    "..oNNNNN",
    "..oooooo",
    "........",
    "........",
]

BOLT = [
    "................",
    "........ooooo...",
    ".......owWWCo...",
    "......owWCCo....",
    ".....owCCCo.....",
    "....owCCCo......",
    "...owCCCCooooo..",
    "..owCCCCCCCCCo..",
    "..oooooCCCCCo...",
    "......oCCCCo....",
    ".....oCCCco.....",
    "....oCCcco......",
    "...oCcco........",
    "..oCco..........",
    "..oco...........",
    "..oo............",
]

SHARD = [
    "................",
    ".......oo.......",
    "......owCo......",
    ".....owWCco.....",
    "....owWCCcco....",
    "...owWCCCccco...",
    "..owWCCCCcccco..",
    "..oCCCCCCcccdo..",
    "...oCCCCcccdo...",
    "....oCCCccdo....",
    ".....oCccdo.....",
    "......ocdo......",
    ".......oo.......",
    "................",
    "................",
    "................",
]

HUD_LEG = {
    "o": O, "R": "#ff4d6d", "r": "#a3123a", "P": "#ff9ab0", "W": "#ffe0e8",
    "S": "#c3d0de", "s": "#6c819a",
    "D": "#3a1a30", "h": "#a3123a", "E": "#ffd98a", "Y": "#e8eef5",
    "M": "#9fb3c8", "N": "#45556a", "a": AM[0], "A": AM[2],
    "C": CY[1], "c": CY[0], "w": CY[2], "d": "#0f6a66",
}


def hud_from(rows, leg_over=None):
    leg = dict(HUD_LEG)
    if leg_over:
        leg.update(leg_over)
    img = new(16, 16)
    stamp(img, rows, leg, 0, 0)
    return img


def heart_full():
    rows = sym(HEART_HALF, {"P": "R", "W": "R"})
    img = hud_from(rows)
    # shadow on the lower right
    for (x, y) in [(13, 6), (13, 7), (12, 8), (11, 9), (10, 10), (9, 11), (12, 7), (11, 8)]:
        if img[y, x, 3] and tuple(img[y, x]) == col("#ff4d6d"):
            put(img, x, y, "#a3123a")
    return img


def heart_empty():
    rows = sym(HEART_HALF, {"P": "R", "W": "R"})
    img = hud_from(rows, {"R": "#2a1424", "P": "#4a1a30", "W": "#5a2038"})
    # inner rim
    for y in range(16):
        for x in range(16):
            if tuple(img[y, x]) == col("#2a1424"):
                nb = [img[y + dy, x + dx] for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0))
                      if 0 <= y + dy < 16 and 0 <= x + dx < 16]
                if any(tuple(n) == col(O) for n in nb):
                    put(img, x, y, "#6a1a34")
    return img


def energy():
    return hud_from(BOLT, {"W": CY[3]})


def shard():
    return hud_from(SHARD, {"W": CY[3]})


def relic():
    img = new(16, 16)
    x, y = grid(16)
    r = np.hypot(x - 8, y - 9)
    disc = r <= 6.0
    lay = shade_mask(disc, ["#4a2a08", "#8a5a10", AM[0], AM[1], AM[2], AM[3]], sigma=1.0,
                     ambient=0.55, contrast=1.0, dither_strength=0.2)
    img[disc] = lay[disc]
    ring = (r > 3.2) & (r <= 4.2)
    img[ring & ((x - 8) + (y - 9) > 0)] = col("#8a5a10")
    img[ring & ((x - 8) + (y - 9) <= 0)] = col(AM[0])
    gem = r <= 2.3
    img[gem] = col(CY[0])
    put(img, 7, 8, CY[3])
    put(img, 8, 8, CY[1])
    put(img, 7, 9, CY[1])
    # bail on top + side spikes (ancient sun-amulet)
    for (px, py, c) in [(7, 2, AM[1]), (8, 2, AM[0]), (7, 1, AM[2]), (8, 1, AM[1]),
                        (1, 9, AM[1]), (14, 9, AM[0]), (8, 15, AM[0]), (7, 15, AM[1])]:
        put(img, px, py, c)
    outline(img, O)
    return img


def skull():
    return hud_from(sym(SKULL_HALF, {"W": "S"}), {"W": "#e8eef5"})


def clock():
    img = new(16, 16)
    x, y = grid(16)
    r = np.hypot(x - 8, y - 8)
    face = r <= 6.4
    lay = shade_mask(face, ["#6c819a", "#9fb3c8", "#c3d0de", "#e8eef5"], sigma=1.0, ambient=0.6,
                     contrast=0.8, dither_strength=0.2)
    img[face] = lay[face]
    rim = face & (r > 5.3)
    img[rim] = col("#2a3e4f")
    img[rim & ((x - 8) + (y - 8) < -3)] = col("#577286")
    for (px, py) in [(8, 3), (13, 8), (8, 13), (3, 8)]:
        put(img, px - (1 if px == 8 else 0), py, "#2a3e4f")
    for (px, py) in [(7, 5), (7, 6), (7, 7), (8, 7), (9, 7), (10, 7)]:
        put(img, px, py, O)
    put(img, 7, 4, CY[0])
    put(img, 7, 7, CY[1])
    # top knob
    put(img, 7, 0, "#9fb3c8")
    put(img, 8, 0, "#6c819a")
    put(img, 7, 1, "#577286")
    put(img, 8, 1, "#577286")
    outline(img, O)
    return img


def enemy():
    return hud_from(sym(ENEMY_HALF, {"E": "R"}), {"R": "#ff4d6d", "E": "#ffd98a"})


def star():
    img = new(16, 16)
    st = star_mask(16, 8, 8.6, 7.2, 3.0)
    lay = shade_mask(st, ["#8a5a10", "#c8901a", "#ffc83a", "#ffe58a", "#fff6d0"], sigma=0.9,
                     ambient=0.55, contrast=1.0, dither_strength=0.2)
    img[st] = lay[st]
    put(img, 7, 5, "#fff6d0")
    outline(img, O)
    return img


def lock():
    return hud_from(sym(LOCK_HALF, {"h": "M", "a": "a", "A": "A"}), {"h": "#e8eef5"})


def check():
    img = new(16, 16)
    m = thick_path_mask(16, 16, [(3 + i * 0.25, 8.5 + i * 0.25) for i in range(15)], 1.5)
    m |= thick_path_mask(16, 16, [(6.5 + i * 0.4, 12 - i * 0.53) for i in range(17)], 1.5)
    lay = shade_mask(m, ["#135c50", "#1f8a6a", "#35b583", CY[1], CY[2]], sigma=0.8, ambient=0.6,
                     contrast=1.0, dither_strength=0.2)
    img[m] = lay[m]
    outline(img, O)
    return img


HUD = [heart_full, heart_empty, energy, shard, relic, skull, clock, enemy, star, lock, check]


def generate():
    from ui_icons_v2 import ICONS as V2
    from ui_icons_v3 import ICONS as V3
    icons = [icon_dash, icon_wave, icon_shield, icon_impulse, icon_teleport] + list(V2) + list(V3)
    powers = new(32 * len(icons), 32)
    for i, fn in enumerate(icons):
        powers[:, i * 32:(i + 1) * 32] = fn()
    medals = new(160, 32)
    for i, k in enumerate(["none", "bronze", "silver", "gold", "platinum"]):
        medals[:, i * 32:(i + 1) * 32] = medal(k)
    hud = new(176, 16)
    for i, fn in enumerate(HUD):
        hud[:, i * 16:(i + 1) * 16] = fn()
    return [save_png(powers, "ui/icons_powers.png"), save_png(medals, "ui/icons_medals.png"),
            save_png(hud, "ui/icons_hud.png")]
