"""Power icon added in 2.50 (ui/icons_powers.png cell 18), same badge style as ui_icons.py:
boomerang_charge (a tri-blade inside its charge ring, notches at 1/3 and 2/3)."""
import numpy as np

from helpers import poly_mask, put
from ui_icons import badge, glow_halo, grid, paint_symbol

CYAN = ["#13706a", "#2fc8b8", "#7ff8ea", "#e6fffb"]
DIM = ["#0b2228", "#10303a", "#173f48", "#1f4f58"]


def _inside(m, r=12.4):
    x, y = grid(32)
    return m & (np.hypot(x - 16, y - 16) <= r)


def _blade(a):
    """One swept blade of the tri-blade, pointing at angle `a` (radians), as a mask."""
    local = [(2.0, -1.5), (5.0, -2.5), (8.6, 0.4), (4.8, 0.5), (2.2, 1.1)]
    ca, sa = np.cos(a), np.sin(a)
    pts = [(16.0 + x * ca - y * sa, 16.0 + x * sa + y * ca) for (x, y) in local]
    return poly_mask(32, 32, pts)


def icon_boomerang_charge():
    img = badge("#0e4a46", "#8ffff0")
    x, y = grid(32)
    d = np.hypot(x - 16, y - 16)
    ang = np.arctan2(y - 16, x - 16)  # 0 = right, clockwise on screen (y down)
    # The charge ring: full track dim, the gauge lit from the top, clockwise, up to 2/3 and a bit.
    track = (d >= 9.9) & (d <= 11.1)
    from_top = np.mod(ang + np.pi / 2, 2 * np.pi)  # 0 at the top, growing clockwise
    lit = track & (from_top <= 2 * np.pi * 0.74)
    blades = np.zeros((32, 32), bool)
    for k in range(3):
        blades |= _blade(np.radians(-80) + k * 2 * np.pi / 3)
    hub = d <= 2.3
    sym = _inside(track | blades | hub)
    glow_halo(img, sym, "#08302c")
    paint_symbol(img, _inside(track & ~lit), DIM)
    paint_symbol(img, _inside(lit), CYAN)
    paint_symbol(img, _inside(blades), CYAN)
    paint_symbol(img, _inside(hub), ["#7ff8ea", "#bffcf4", "#e6fffb", "#ffffff"])
    # The three notches of the gauge (1/3, 2/3, full): bright ticks across the ring.
    for frac in (1 / 3, 2 / 3, 1.0):
        a = -np.pi / 2 + 2 * np.pi * frac
        for rr in (9.4, 10.5, 11.6):
            px, py = 16 + np.cos(a) * rr, 16 + np.sin(a) * rr
            if np.hypot(px - 16, py - 16) <= 12.4:
                put(img, int(px), int(py), CYAN[3] if frac < 0.74 else "#bfeee8")
    # A bright cutting edge on each blade, and the core.
    for k in range(3):
        a = np.radians(-80) + k * 2 * np.pi / 3
        for t in (3.0, 5.0, 6.8):
            px, py = 16 + np.cos(a) * t - np.sin(a) * (-1.4 + t * 0.12), 16 + np.sin(a) * t + np.cos(a) * (-1.4 + t * 0.12)
            put(img, int(round(px)), int(round(py)), CYAN[3])
    put(img, 16, 16, "#ffffff")
    put(img, 15, 15, "#ffffff")
    return img


ICONS = [icon_boomerang_charge]
