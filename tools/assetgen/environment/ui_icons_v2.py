"""Power icons added with the v2 abilities (ui/icons_powers.png cells 5-9), drawn in the same
badge style as ui_icons.py: glide, ground_pound, charge_shot, grapple, triple_jump."""
import math

import numpy as np

from helpers import col, poly_mask, put, thick_path_mask
from ui_icons import badge, glow_halo, grid, paint_symbol

MINT = ["#1f8a6a", "#35b583", "#5dffb0", "#d0ffe8"]
EMBER = ["#b8421e", "#ff7a45", "#ffb08a", "#fff1e0"]
SKY = ["#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]
AM = ["#c8741f", "#ffb347", "#ffd98a", "#fff1c8"]
LIME = ["#2f7a1a", "#4fbf26", "#9dff3a", "#e4ffb0"]


def _inside(m):
    x, y = grid(32)
    return m & (np.hypot(x - 16, y - 16) <= 12.4)


def icon_glide():
    img = badge("#1f6a4a", "#5dffb0")
    # delta glider wing seen from the front-below, with membrane ribs
    wing = poly_mask(32, 32, [(5, 17), (16, 8), (27, 17), (24, 18), (16, 13), (8, 18)])
    keel = thick_path_mask(32, 32, [(16, 9), (16, 15)], 0.9)
    wind = np.zeros((32, 32), bool)
    for (x0, x1, yy) in [(8, 13, 21), (12, 20, 23), (19, 24, 21)]:
        wind[yy, x0:x1] = True
    sym = _inside(wing | keel | wind)
    glow_halo(img, sym, "#12402e")
    paint_symbol(img, _inside(wing), MINT)
    for (x0, y0, x1, y1) in [(16, 9, 9, 17), (16, 9, 23, 17)]:
        for i in range(8):
            t = i / 7
            put(img, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, MINT[1])
    paint_symbol(img, _inside(keel), ["#1f8a6a", "#1f8a6a", "#35b583", "#5dffb0"])
    for (x0, x1, yy) in [(8, 13, 21), (12, 20, 23), (19, 24, 21)]:
        for x in range(x0, x1):
            put(img, x, yy, MINT[2] if x < x0 + 2 else MINT[1])
    put(img, 16, 8, MINT[3])
    return img


def icon_ground_pound():
    img = badge("#7a2a1a", "#ff8a3d")
    arrow = poly_mask(32, 32, [(13, 6), (19, 6), (19, 14), (23, 14), (16, 21), (9, 14), (13, 14)])
    ground = np.zeros((32, 32), bool)
    ground[24, 8:25] = True
    ground[25, 10:23] = True
    burst = np.zeros((32, 32), bool)
    for (x0, y0, x1, y1) in [(12, 22, 8, 19), (20, 22, 24, 19), (10, 24, 6, 23), (22, 24, 26, 23)]:
        burst |= thick_path_mask(32, 32, [(x0, y0), (x1, y1)], 0.7)
    sym = _inside(arrow | ground | burst)
    glow_halo(img, sym, "#4a1a10")
    paint_symbol(img, _inside(arrow), EMBER)
    for y in range(7, 13):
        put(img, 14, y, EMBER[3])
    img[_inside(ground)] = col(EMBER[0])
    for x in range(9, 24):
        put(img, x, 24, EMBER[1] if abs(x - 16) > 3 else EMBER[2])
    img[_inside(burst)] = col(EMBER[2])
    for (x, y) in [(8, 19), (24, 19), (16, 22)]:
        put(img, x, y, EMBER[3])
    return img


def icon_charge_shot():
    img = badge("#1a4a8a", "#5fb0ff")
    x, y = grid(32)
    cx, cy = 17, 16
    r = np.hypot(x - cx, y - cy)
    ang = np.degrees(np.arctan2(y - cy, x - cx)) % 360
    orb = r <= 4.6
    ring = (r > 6.6) & (r <= 7.9) & ~((ang > 150) & (ang < 210))
    outer = (r > 10.0) & (r <= 11.2) & (((ang + 15) % 60) < 30) & ~((ang > 135) & (ang < 225))
    trail = np.zeros((32, 32), bool)
    for (yy, x0) in [(14, 5), (16, 3), (18, 5)]:
        trail[yy, x0:cx - 6] = True
    sym = _inside(orb | ring | outer | trail)
    glow_halo(img, sym, "#13305a")
    paint_symbol(img, _inside(ring | outer), SKY)
    paint_symbol(img, _inside(orb), ["#5fb0ff", "#a8e4ff", "#eefaff", "#ffffff"])
    for (yy, x0) in [(14, 5), (16, 3), (18, 5)]:
        for xx in range(x0, cx - 6):
            put(img, xx, yy, SKY[0] if xx < x0 + 2 else SKY[1])
    for (px, py, c) in [(15, 14, "#ffffff"), (24, 9, SKY[3]), (25, 23, SKY[2]), (21, 6, SKY[2])]:
        put(img, px, py, c)
    return img


def _poly(pts, step=0.4):
    """Dense points along a polyline (thick_path_mask stamps discs at each point)."""
    out = []
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        n = max(2, int(math.hypot(x1 - x0, y1 - y0) / step))
        out += [(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n + 1)]
    return out


def icon_grapple():
    img = badge("#7a4a1a", "#ffb347")
    # grappling anchor flying up-right (rotated), rope trailing down-left from its eye
    a = math.radians(-135)
    ca, sa = math.cos(a), math.sin(a)

    def tr(px, py):            # local anchor coords (eye up, prongs down) -> icon coords
        return (17 + px * ca - py * sa, 15 + px * sa + py * ca)

    shank = _poly([tr(0, -5), tr(0, 6)])
    prong_l = _poly([tr(0, 5), tr(-3, 6), tr(-5, 4), tr(-6, 1)])
    prong_r = _poly([tr(0, 5), tr(3, 6), tr(5, 4), tr(6, 1)])
    m = thick_path_mask(32, 32, shank, 1.2) | thick_path_mask(32, 32, prong_l + prong_r, 1.0)
    tips = [tr(-6, 0), tr(6, 0)]
    for (tx, ty) in tips:
        m |= thick_path_mask(32, 32, [(tx, ty)], 1.2)
    x, y = grid(32)
    ex, ey = tr(0, -7)
    ring = (np.hypot(x - ex, y - ey) <= 2.3) & (np.hypot(x - ex, y - ey) > 1.1)
    line = thick_path_mask(32, 32, _poly([(ex - 1.5, ey + 1.5), (8, 25), (6, 27)]), 0.6)
    sym = _inside(m | ring | line)
    glow_halo(img, sym, "#4a2a12")
    img[_inside(line)] = col(AM[0])
    paint_symbol(img, _inside(m | ring), AM)
    for (px, py) in _poly([(ex - 1.5, ey + 1.5), (8, 25), (6, 27)], 3.0)[1::2]:
        put(img, px, py, AM[2])
    for (tx, ty) in tips:
        put(img, tx, ty, AM[3])
    return img


def icon_triple_jump():
    img = badge("#3a6a1a", "#9dff3a")
    chev = np.zeros((32, 32), bool)
    parts = []
    for k, (cy, half) in enumerate([(9, 7.0), (15, 6.0), (21, 5.0)]):
        m = thick_path_mask(32, 32, _poly([(16 - half, cy + half * 0.7), (16, cy),
                                           (16 + half, cy + half * 0.7)]), 1.5 - 0.15 * k)
        parts.append(m)
        chev |= m
    dots = np.zeros((32, 32), bool)
    for (px, py) in [(12, 26), (16, 26), (20, 26)]:
        dots[py, px] = True
    sym = _inside(chev | dots)
    glow_halo(img, sym, "#1f3a10")
    for k, m in enumerate(parts):
        ramp = LIME if k == 0 else (["#2f7a1a", "#4fbf26", "#9dff3a", "#c8ff80"] if k == 1 else
                                    ["#2f7a1a", "#2f7a1a", "#4fbf26", "#9dff3a"])
        paint_symbol(img, _inside(m), ramp)
    for (px, py) in [(12, 26), (16, 26), (20, 26)]:
        put(img, px, py, LIME[2])
    put(img, 16, 8, LIME[3])
    return img


ICONS = [icon_glide, icon_ground_pound, icon_charge_shot, icon_grapple, icon_triple_jump]
