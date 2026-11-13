"""Frame-level post effects shared by characters: dissolve into particles,
flash, glow rings, sparkles."""
import math

import numpy as np

from common.palette import CYAN, OUTLINE
from raster import blank, put

CYAN_FADE = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]


def _rng_field(h, w, seed):
    rng = np.random.default_rng(seed)
    return rng.random((h, w))


def dissolve(img, t, seed=1, colors=CYAN_FADE, rise=14.0, drift=3.0, top_first=True,
             edge=0.12, particle_rate=0.45, life=0.55, bounds=None):
    """Dissolve an RGBA frame into rising glowing particles.

    t = 0 -> untouched, t = 1 -> fully gone (only late particles left).
    Deterministic for a given seed, so successive t values form an animation.
    """
    h, w = img.shape[:2]
    alpha = img[:, :, 3] > 0
    ys, xs = np.nonzero(alpha)
    out = np.zeros_like(img)
    if len(ys) == 0:
        return out
    y0, y1 = ys.min(), ys.max() + 1
    rnd = _rng_field(h, w, seed)
    yy = np.mgrid[0:h, 0:w][0].astype(np.float32)
    ny = (yy - y0) / max(1, (y1 - y0))
    order = ny if top_first else 1 - ny
    thr = 0.55 * rnd + 0.45 * order
    thr = thr * (1 - edge)  # in [0, 1-edge]
    keep = alpha & (t < thr)
    glow = alpha & (t >= thr) & (t < thr + edge)
    out[keep] = img[keep]
    # tint remaining pixels slightly towards cyan as the dissolve progresses
    if t > 0:
        k = min(0.5, t * 0.6)
        c = np.array(colors[1][:3], np.float32)
        sel = keep & (img[:, :, 0] != OUTLINE[0])
        out[sel, :3] = np.clip(img[sel, :3] * (1 - k) + c * k, 0, 255).astype(np.uint8)
    for (y, x) in zip(*np.nonzero(glow)):
        out[y, x] = colors[0] if rnd[y, x] > 0.5 else colors[1]
    # particles
    prng = np.random.default_rng(seed + 991)
    pr = prng.random((h, w, 3))
    for (y, x) in zip(ys, xs):
        if pr[y, x, 0] > particle_rate:
            continue
        born = thr[y, x] + edge
        age = t - born
        if age < 0 or age > life:
            continue
        a = age / life
        px = x + (pr[y, x, 1] - 0.5) * 2 * drift * a
        py = y - rise * a * (0.6 + 0.8 * pr[y, x, 2])
        ci = min(len(colors) - 1, int(a * len(colors)))
        ix, iy = int(round(px)), int(round(py))
        if bounds is not None:
            bx0, by0, bx1, by1 = bounds
            if not (bx0 <= ix < bx1 and by0 <= iy < by1):
                continue
        if 0 <= ix < w and 0 <= iy < h and out[iy, ix, 3] == 0:
            out[iy, ix] = colors[ci]
    return out


def flash(img, color=(255, 255, 255, 255), amount=0.7):
    out = img.copy()
    sel = (img[:, :, 3] > 0) & ~np.all(img[:, :, :3] == np.array(OUTLINE[:3], np.uint8), axis=2)
    c = np.array(color[:3], np.float32)
    out[sel, :3] = np.clip(img[sel, :3] * (1 - amount) + c * amount, 0, 255).astype(np.uint8)
    return out


def sparkle(img, x, y, size, c_core, c_ray):
    """4-point twinkle (+ shape) centred at (x, y)."""
    put(img, x, y, c_core)
    for i in range(1, size + 1):
        c = c_core if i == 1 and size > 1 else c_ray
        put(img, x + i, y, c)
        put(img, x - i, y, c)
        put(img, x, y + i, c)
        put(img, x, y - i, c)
    return img


def ring_pixels(cx, cy, r, w=1.0):
    pts = []
    n = max(8, int(2 * math.pi * r * 2))
    for i in range(n):
        a = 2 * math.pi * i / n
        pts.append((int(round(cx + math.cos(a) * r - 0.5)), int(round(cy + math.sin(a) * r - 0.5))))
    return sorted(set(pts))


def overlay(dst, src):
    """src pixels with alpha replace dst pixels (hard pixel overlay)."""
    sel = src[:, :, 3] > 0
    dst[sel] = src[sel]
    return dst


def under(dst, src):
    """Place src pixels only where dst is transparent."""
    sel = (src[:, :, 3] > 0) & (dst[:, :, 3] == 0)
    dst[sel] = src[sel]
    return dst


def new(w, h):
    return blank(w, h)


def burst(img, cx, cy, r, cols, rays=0, seed=0, jag=0.25):
    """Filled explosion blob: concentric colour bands (cols bright -> dark),
    jagged edge, optional ray spikes. Hard pixels."""
    h, w = img.shape[:2]
    rng = np.random.default_rng(seed)
    nb = 16
    jit = 1.0 + (rng.random(nb) - 0.5) * 2 * jag
    for y in range(max(0, int(cy - r * 1.6) - 1), min(h, int(cy + r * 1.6) + 2)):
        for x in range(max(0, int(cx - r * 1.6) - 1), min(w, int(cx + r * 1.6) + 2)):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            a = (math.atan2(dy, dx) / (2 * math.pi)) % 1.0
            k = a * nb
            i0 = int(k) % nb
            f = k - int(k)
            rr = r * (jit[i0] * (1 - f) + jit[(i0 + 1) % nb] * f)
            if d <= rr:
                band = min(len(cols) - 1, int((d / max(rr, 1e-3)) * len(cols)))
                img[y, x] = cols[band]
    for i in range(rays):
        a = rng.random() * 2 * math.pi
        ln = r * (1.3 + rng.random() * 0.5)
        for t in np.linspace(r * 0.8, ln, int(ln * 2)):
            put(img, int(cx + math.cos(a) * t), int(cy + math.sin(a) * t), cols[min(1, len(cols) - 1)])
    return img


def chunk(img, x, y, size, light, dark, outline=OUTLINE):
    """Small debris rock (size 1-3) with outline."""
    x, y = int(round(x)), int(round(y))
    pts = [(x + i, y + j) for i in range(size) for j in range(size)]
    if size >= 3:
        pts.remove((x + size - 1, y))
    for (px, py) in pts:
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (px + ox, py + oy)
            if q not in pts and 0 <= q[0] < img.shape[1] and 0 <= q[1] < img.shape[0] and img[q[1], q[0], 3] == 0:
                img[q[1], q[0]] = outline
    for (px, py) in pts:
        c = light if (px - x + py - y) < size - 1 else dark
        put(img, px, py, c)
    return img
