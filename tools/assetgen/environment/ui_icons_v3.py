"""Power icons added in 2.5 (ui/icons_powers.png cells 10-13), same badge style as
ui_icons.py: time_slow (hourglass), magnet, air_dash (double chevron), plasma_bomb."""
import numpy as np

from helpers import poly_mask, put, thick_path_mask
from ui_icons import badge, glow_halo, grid, paint_symbol

VIOLET = ["#5a2a9a", "#8a4cff", "#c9a0ff", "#f2e6ff"]
ROSE = ["#9a2a5a", "#ff4d8a", "#ff9ec2", "#fff0f6"]
STEEL = ["#4a5a7a", "#8ca0c8", "#c8d6f0", "#f4f8ff"]
CYAN = ["#13706a", "#2fc8b8", "#7ff8ea", "#e6fffb"]
EMBER = ["#9a3a12", "#ff7a2a", "#ffb47a", "#fff0dc"]


def _poly(pts, step=0.4):
    """Densified polyline (thick_path_mask draws discs at the given points)."""
    out = []
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        n = max(1, int(np.hypot(x1 - x0, y1 - y0) / step))
        for i in range(n):
            t = i / n
            out.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
    out.append(pts[-1])
    return out


def _inside(m):
    x, y = grid(32)
    return m & (np.hypot(x - 16, y - 16) <= 12.4)


def icon_time_slow():
    img = badge("#3a1a6a", "#b07aff")
    top = poly_mask(32, 32, [(10, 7), (22, 7), (22, 9), (17, 15.5), (15, 15.5), (10, 9)])
    bottom = poly_mask(32, 32, [(15, 16.5), (17, 16.5), (22, 23), (22, 25), (10, 25), (10, 23)])
    caps = np.zeros((32, 32), bool)
    caps[6, 9:23] = True
    caps[25, 9:23] = True
    sand = poly_mask(32, 32, [(12, 24), (20, 24), (16, 19)])
    sym = _inside(top | bottom | caps)
    glow_halo(img, sym, "#24104a")
    paint_symbol(img, _inside(top | bottom), STEEL)
    paint_symbol(img, _inside(sand), VIOLET)
    for x in range(9, 23):
        put(img, x, 6, VIOLET[2])
        put(img, x, 25, VIOLET[1])
    for y in range(16, 20):
        put(img, 16, y, VIOLET[3])
    return img


def icon_magnet():
    img = badge("#5a1030", "#ff5a8a")
    arc = thick_path_mask(32, 32, _poly([(9.5, 9), (9.5, 17), (11, 21.5), (16, 24), (21, 21.5), (22.5, 17), (22.5, 9)]), 2.4)
    tips = np.zeros((32, 32), bool)
    tips[6:10, 7:12] = True
    tips[6:10, 21:26] = True
    sym = _inside(arc | tips)
    glow_halo(img, sym, "#3a0a20")
    paint_symbol(img, _inside(arc & ~tips), ROSE)
    paint_symbol(img, _inside(tips), STEEL)
    for (x, y) in [(13, 4), (19, 4), (16, 3)]:
        put(img, x, y + 1, ROSE[3])
    return img


def icon_air_dash():
    img = badge("#0e4a46", "#39f0d8")
    c1 = thick_path_mask(32, 32, _poly([(9, 9), (15, 16), (9, 23)]), 1.6)
    c2 = thick_path_mask(32, 32, _poly([(16, 9), (22, 16), (16, 23)]), 1.6)
    trail = np.zeros((32, 32), bool)
    for (x0, x1, yy) in [(5, 9, 13), (4, 8, 16), (5, 9, 19)]:
        trail[yy, x0:x1] = True
    sym = _inside(c1 | c2 | trail)
    glow_halo(img, sym, "#08302c")
    paint_symbol(img, _inside(c1), ["#13706a", "#13706a", "#2fc8b8", "#7ff8ea"])
    paint_symbol(img, _inside(c2), CYAN)
    for (x0, x1, yy) in [(5, 9, 13), (4, 8, 16), (5, 9, 19)]:
        for x in range(x0, x1):
            put(img, x, yy, CYAN[1] if x > x0 else CYAN[0])
    put(img, 22, 16, CYAN[3])
    return img


def icon_plasma_bomb():
    img = badge("#5a200a", "#ff8a3c")
    x, y = grid(32)
    ball = np.hypot(x - 15, y - 18) <= 7.2
    cap = np.zeros((32, 32), bool)
    cap[9:12, 16:21] = True
    fuse = thick_path_mask(32, 32, _poly([(19, 10), (21, 7), (23, 6)]), 0.7)
    sym = _inside(ball | cap | fuse)
    glow_halo(img, sym, "#3a1406")
    paint_symbol(img, _inside(ball), EMBER)
    paint_symbol(img, _inside(cap), STEEL)
    for (px, py) in [(19, 10), (20, 8), (21, 7)]:
        put(img, px, py, EMBER[1])
    for (px, py, c) in [(23, 5, EMBER[3]), (24, 4, EMBER[2]), (22, 4, EMBER[2]), (24, 6, EMBER[2])]:
        put(img, px, py, c)
    for (px, py) in [(12, 15), (13, 15), (12, 16)]:
        put(img, px, py, EMBER[3])
    return img


ICONS = [icon_time_slow, icon_magnet, icon_air_dash, icon_plasma_bomb]
