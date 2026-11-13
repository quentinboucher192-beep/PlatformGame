"""Frame effects for the BOSSES-B sheets: shake, glow motes, embers, smoke,
frost mist, sound rings, speed lines, flash, a Voronoi shatter (break-apart)
and a dissolve wrapper that keeps the planet outline colours intact."""
import math

import numpy as np

import effects as FX
from raster import TCanvas, outline_rgba, put

TAU = 2 * math.pi


def shake(f, dx, dy=0):
    out = np.zeros_like(f)
    h, w = f.shape[:2]
    ys0, ys1 = max(0, dy), h + min(0, dy)
    xs0, xs1 = max(0, dx), w + min(0, dx)
    out[ys0:ys1, xs0:xs1] = f[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
    return out


def empty_at(f, x, y):
    h, w = f.shape[:2]
    return 0 <= x < w and 0 <= y < h and f[y, x, 3] == 0


def motes(f, cx, cy, n, r0, r1, cols, seed, inward=0.0, over=False):
    """Scattered glowing pixels in a ring around (cx, cy)."""
    rng = np.random.default_rng(seed)
    h, w = f.shape[:2]
    for _ in range(n):
        a = rng.random() * TAU
        d = (r0 + (r1 - r0) * rng.random()) * (1 - inward)
        x, y = int(cx + math.cos(a) * d), int(cy + math.sin(a) * d)
        if 0 <= x < w and 0 <= y < h and (over or f[y, x, 3] == 0):
            f[y, x] = cols[int(rng.integers(0, len(cols)))]
            if rng.random() < 0.25 and empty_at(f, x + 1, y):
                f[y, x + 1] = cols[-1]
    return f


def converge(f, cx, cy, n, t, cols, seed, r_max=26, streak=True):
    """Particles streaking toward (cx, cy); t = 0 far .. 1 arrived."""
    rng = np.random.default_rng(seed)
    for _ in range(n):
        a = rng.random() * TAU
        d0 = r_max * (0.55 + 0.45 * rng.random())
        d = d0 * (1 - t) * (0.6 + 0.4 * rng.random()) + 2
        x, y = cx + math.cos(a) * d, cy + math.sin(a) * d
        ci = int(rng.integers(0, len(cols)))
        if empty_at(f, int(x), int(y)):
            f[int(y), int(x)] = cols[ci]
        if streak:
            x2, y2 = cx + math.cos(a) * (d + 2), cy + math.sin(a) * (d + 2)
            if empty_at(f, int(x2), int(y2)):
                f[int(y2), int(x2)] = cols[min(len(cols) - 1, ci + 1)]
    return f


def rising(f, x0, x1, y0, y1, n, phase, cols, seed, rise=18, sway=2.0, over=False):
    """Embers / snow drifting up (rise > 0) or down (rise < 0) - loops with phase 0..1."""
    rng = np.random.default_rng(seed)
    h, w = f.shape[:2]
    for _ in range(n):
        bx = x0 + (x1 - x0) * rng.random()
        by = y0 + (y1 - y0) * rng.random()
        off = rng.random()
        k = (phase + off) % 1.0
        x = int(bx + math.sin((k + off) * TAU) * sway)
        y = int(by - rise * k)
        ci = min(len(cols) - 1, int(k * len(cols)))
        if 0 <= x < w and 0 <= y < h and (over or f[y, x, 3] == 0):
            f[y, x] = cols[ci]
    return f


def puff(f, x, y, r, cols, seed=0, over=False):
    """Small hard-pixel smoke / mist puff (cols bright -> dark, light from top-left)."""
    rng = np.random.default_rng(seed)
    h, w = f.shape[:2]
    for yy in range(int(y - r - 1), int(y + r + 2)):
        for xx in range(int(x - r - 1), int(x + r + 2)):
            if not (0 <= xx < w and 0 <= yy < h):
                continue
            dx, dy = xx + 0.5 - x, yy + 0.5 - y
            d = math.hypot(dx, dy) / max(r, 0.5)
            if d <= 1.0 - 0.18 * rng.random():
                if not over and f[yy, xx, 3] != 0:
                    continue
                lit = -(dx + dy) / max(r, 0.5) * 0.5 + 0.5 - 0.3 * d
                ci = 0 if lit > 0.62 else (1 if lit > 0.3 else 2)
                f[yy, xx] = cols[min(ci, len(cols) - 1)]
    return f


def rings(f, cx, cy, radii, cols, a0=-1.1, a1=1.0, dotted=False):
    for k, r in enumerate(radii):
        for (x, y) in FX.ring_pixels(cx, cy, r):
            a = math.atan2(y - cy, x - cx)
            if a0 < a < a1 and empty_at(f, x, y) and (not dotted or (x + y) % 2 == 0):
                f[y, x] = cols[min(k, len(cols) - 1)]
    return f


def speed_lines(f, seed, n, y0, y1, x0, x1, col, ln=(4, 9)):
    rng = np.random.default_rng(seed)
    for _ in range(n):
        y = int(rng.integers(y0, y1))
        xs = int(rng.integers(x0, x1))
        for x in range(xs, xs + int(rng.integers(ln[0], ln[1]))):
            if empty_at(f, x, y):
                f[y, x] = col
    return f


def flash(img, outlines, color=(255, 255, 255, 255), amount=0.7):
    out = img.copy()
    sel = img[:, :, 3] > 0
    for o in outlines:
        sel &= ~np.all(img[:, :, :3] == np.array(o[:3], np.uint8), axis=2)
    c = np.array(color[:3], np.float32)
    out[sel, :3] = np.clip(img[sel, :3] * (1 - amount) + c * amount, 0, 255).astype(np.uint8)
    return out


def dissolve(img, t, out_col, **kw):
    """effects.dissolve, then restore the outline colour on surviving pixels."""
    colors = kw.get("colors")
    out = FX.dissolve(img, t, **kw)
    was = np.all(img == np.array(out_col, np.uint8), axis=2)
    alive = out[:, :, 3] > 0
    if colors:
        for c in colors:
            alive &= ~np.all(out == np.array(c, np.uint8), axis=2)
    sel = was & alive
    out[sel] = out_col
    return out


def shatter(img, t, seed, out_col, edge_cols, n=12, center=None, ground=95, spread=1.0, pop=1.0, grav=40.0,
            order_seed=None):
    """Break the frame into Voronoi pieces that fly apart and fall to the ground.
    Cut edges get `edge_cols` (glowing lava / frost); every piece is re-outlined."""
    h, w = img.shape[:2]
    alpha = img[:, :, 3] > 0
    ys, xs = np.nonzero(alpha)
    if len(ys) == 0:
        return img.copy()
    rng = np.random.default_rng(seed)
    idx = rng.choice(len(ys), size=min(n, len(ys)), replace=False)
    sx, sy = xs[idx].astype(np.float32), ys[idx].astype(np.float32)
    # jagged cells: distance with a little per-pixel noise
    noise = np.random.default_rng(seed + 7).random(len(ys)) * 6.0
    d = (xs[:, None] - sx[None]) ** 2 + (ys[:, None] - sy[None]) ** 2 + noise[:, None] * 4.0
    lab = np.argmin(d, axis=1)
    labels = np.full((h, w), -1, np.int32)
    labels[ys, xs] = lab
    if center is None:
        center = (xs.mean(), ys.mean())
    out = np.zeros_like(img)
    vrng = np.random.default_rng(seed + 31)
    order = list(range(len(sx)))
    order.sort(key=lambda k: -sy[k])  # higher pieces drawn last (they fall onto lower ones)
    for k in order:
        sel = labels == k
        if not sel.any():
            continue
        py, px = np.nonzero(sel)
        vx = (sx[k] - center[0]) * 0.35 * spread + vrng.normal() * 3.0 * spread
        vy = -(4.0 + 8.0 * vrng.random()) * pop
        dx = vx * t
        dy = vy * t + grav * t * t
        bottom = py.max()
        if bottom + dy > ground:
            dy = ground - bottom
        dx = min(max(dx, 2 - px.min()), w - 3 - px.max())
        dx, dy = int(round(dx)), int(round(dy))
        piece = np.zeros_like(img)
        piece[sel] = img[sel]
        # cut edges: piece pixels touching another piece -> glowing edge colour
        if t > 0:
            cut = np.zeros((h, w), bool)
            for oy, ox in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                nb = np.roll(np.roll(labels, oy, 0), ox, 1)
                cut |= sel & (nb >= 0) & (nb != k)
            cy_, cx_ = np.nonzero(cut)
            for j, (yy, xx) in enumerate(zip(cy_, cx_)):
                if img[yy, xx, 3] and not np.all(img[yy, xx] == np.array(out_col, np.uint8)):
                    piece[yy, xx] = edge_cols[(yy * 3 + xx) % len(edge_cols)]
        piece = outline_rgba(piece, out_col)
        tgt = shake(piece, dx, dy)
        m = tgt[:, :, 3] > 0
        out[m] = tgt[m]
    return out


def burst_under(f, cx, cy, r, cols, seed=0, jag=0.25):
    """effects.burst drawn only on transparent pixels (keeps the character on top)."""
    lay = np.zeros_like(f)
    FX.burst(lay, cx, cy, r, cols, seed=seed, jag=jag)
    return under(f, lay)


def chunk(img, x, y, size, light, dark, out_col):
    return FX.chunk(img, x, y, size, light, dark, outline=out_col)


def layer(w, h):
    return TCanvas(w, h)


def over(dst, src):
    sel = src[:, :, 3] > 0
    dst[sel] = src[sel]
    return dst


def under(dst, src):
    sel = (src[:, :, 3] > 0) & (dst[:, :, 3] == 0)
    dst[sel] = src[sel]
    return dst


__all__ = ["shake", "motes", "converge", "rising", "puff", "rings", "speed_lines", "flash", "dissolve", "shatter",
           "chunk", "layer", "over", "under", "put", "empty_at"]
