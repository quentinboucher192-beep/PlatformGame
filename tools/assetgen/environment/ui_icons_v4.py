"""Power icons added in 2.19 (ui/icons_powers.png cells 14-17), same badge style as
ui_icons.py: boomerang (crescent blade), frost_nova (snow crystal in a ring), decoy (Nova and
her hologram), recall (beacon flag over a return arc)."""
import numpy as np

from helpers import poly_mask, put, thick_path_mask
from ui_icons import badge, glow_halo, grid, paint_symbol

CYAN = ["#13706a", "#2fc8b8", "#7ff8ea", "#e6fffb"]
HOLO = ["#0e4a46", "#1f8a80", "#4fd8c8", "#a8fff4"]
ICE = ["#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]
VIOLET = ["#4a2a8a", "#8a5cff", "#b48cff", "#efe2ff"]


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


def _arc(cx, cy, r, a0, a1, n=40):
    return [(cx + r * np.cos(a), cy + r * np.sin(a)) for a in np.linspace(a0, a1, n)]


def _inside(m):
    x, y = grid(32)
    return m & (np.hypot(x - 16, y - 16) <= 12.4)


def icon_boomerang():
    img = badge("#0e4a46", "#8ffff0")
    # a curved V blade: two arms meeting at a rounded elbow, spinning (motion arcs)
    arm1 = _poly([(9.0, 9.5), (10.0, 14.0), (12.0, 18.5), (15.0, 21.5)])
    arm2 = _poly([(15.0, 21.5), (19.0, 21.0), (23.0, 18.5)])
    blade = thick_path_mask(32, 32, arm1 + arm2, 2.1)
    trail = thick_path_mask(32, 32, _arc(16.0, 16.0, 10.6, np.radians(-80), np.radians(-20)), 0.6)
    sym = _inside(blade | trail)
    glow_halo(img, sym, "#08302c")
    paint_symbol(img, _inside(blade), CYAN)
    for (px, py) in _arc(16.0, 16.0, 10.6, np.radians(-78), np.radians(-22), 8):
        put(img, int(px), int(py), CYAN[1])
    # bright cutting edge along the inside of the curve
    for (px, py) in [(11, 10), (11, 11), (12, 13), (12, 14), (13, 16), (14, 17), (15, 18), (16, 19), (17, 19),
                     (18, 19), (19, 18), (20, 18), (21, 17)]:
        put(img, px, py, CYAN[3])
    return img


def icon_frost_nova():
    img = badge("#1a3a6a", "#9fe8ff")
    x, y = grid(32)
    d = np.hypot(x - 16, y - 16)
    ring = (d >= 9.6) & (d <= 11.2)
    arms = np.zeros((32, 32), bool)
    for k in range(3):
        a = np.pi / 3 * k
        dx, dy = np.cos(a) * 7.2, np.sin(a) * 7.2
        arms |= thick_path_mask(32, 32, _poly([(16 - dx, 16 - dy), (16 + dx, 16 + dy)]), 0.8)
    barbs = np.zeros((32, 32), bool)
    for k in range(6):
        a = np.pi / 3 * k
        cx, cy = 16 + np.cos(a) * 4.8, 16 + np.sin(a) * 4.8
        for s in (-1, 1):
            b = a + s * np.radians(50)
            barbs |= thick_path_mask(32, 32, _poly([(cx, cy), (cx + np.cos(b) * 2.4, cy + np.sin(b) * 2.4)]), 0.5)
    sym = _inside(ring | arms | barbs)
    glow_halo(img, sym, "#10264a")
    paint_symbol(img, _inside(ring), ICE)
    paint_symbol(img, _inside(arms | barbs), ["#5fb0ff", "#a8e4ff", "#dff7ff", "#ffffff"])
    put(img, 16, 16, "#ffffff")
    put(img, 15, 15, "#ffffff")
    return img


def _figure(ox, oy):
    """Nova seen from the front: head and body (mask)."""
    x, y = grid(32)
    head = np.hypot(x - (ox + 3.0), y - (oy + 2.6)) <= 2.6
    body = poly_mask(32, 32, [(ox + 0.6, oy + 6.0), (ox + 5.4, oy + 6.0), (ox + 6.0, oy + 14.5), (ox + 0.0, oy + 14.5)])
    return head | body


def icon_decoy():
    img = badge("#0e4a46", "#39f0d8")
    holo = _inside(_figure(7.5, 7.0))
    nova = _inside(_figure(16.5, 9.5))
    glow_halo(img, holo | nova, "#08302c")
    paint_symbol(img, holo & ~nova, HOLO)
    # scan lines across the hologram
    for yy in range(8, 23, 3):
        for xx in range(6, 16):
            if holo[yy, xx] and not nova[yy, xx]:
                put(img, xx, yy, HOLO[3] if xx % 2 else HOLO[2])
    paint_symbol(img, nova, CYAN)
    put(img, 20, 12, "#ffffff")
    return img


def icon_recall():
    img = badge("#3a1a6a", "#b48cff")
    pole = np.zeros((32, 32), bool)
    pole[7:21, 15:17] = True
    flag = poly_mask(32, 32, [(16.5, 7.0), (24.0, 10.0), (16.5, 13.0)])
    arc = thick_path_mask(32, 32, _poly(_arc(16.0, 20.5, 6.5, np.radians(10), np.radians(170))), 0.9)
    sym = _inside(pole | flag | arc)
    glow_halo(img, sym, "#24104a")
    paint_symbol(img, _inside(arc), CYAN)
    paint_symbol(img, _inside(pole), ["#6c5a9a", "#8c7ac8", "#c8bcf0", "#f4f0ff"])
    paint_symbol(img, _inside(flag), VIOLET)
    # arrow head of the return arc (left end, pointing up toward the beacon)
    for (px, py) in [(9, 22), (10, 21), (8, 21), (11, 22)]:
        put(img, px, py, CYAN[3])
    put(img, 17, 8, VIOLET[3])
    return img


ICONS = [icon_boomerang, icon_frost_nova, icon_decoy, icon_recall]
