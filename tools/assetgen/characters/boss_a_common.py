"""Shared helpers for the BOSSES-A sheets (hydra rig: spore_queen, frost_hydra;
core rig: prime_sentinel, forge_core).  Pure functions on RGBA frames plus a few
geometry helpers; everything is deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from common.palette import OUTLINE, hex_rgba
from raster import Mat, put

TAU = 2 * math.pi


# ------------------------------------------------------------------ colours
def C(h, a=255):
    return hex_rgba(h, a)


def ramp(*hs):
    return [hex_rgba(h) for h in hs]


def mat(name, hs, emissive=False, outline=OUTLINE, th=None):
    """Material from a light -> dark list of hex colours."""
    cols = ramp(*hs)
    if th is None:
        n = len(cols)
        th = {1: [], 2: [0.55], 3: [0.72, 0.42], 4: [0.84, 0.6, 0.36],
              5: [0.86, 0.68, 0.48, 0.28], 6: [0.88, 0.74, 0.58, 0.42, 0.24]}.get(n)
    return Mat(cols, emissive=emissive, outline=outline, thresholds=th, name=name)


# ------------------------------------------------------------------ geometry
def lerp2(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def bezier(p0, p1, p2, p3, n):
    out = []
    for i in range(n):
        t = i / (n - 1)
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                    u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return out


def chaikin(pts, it=2):
    for _ in range(it):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            out.append((a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25))
            out.append((a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75))
        out.append(pts[-1])
        pts = out
    return pts


def polar(c, ang, r):
    return (c[0] + math.cos(ang) * r, c[1] + math.sin(ang) * r)


def seg_dir(pts, i):
    i0 = max(0, min(i, len(pts) - 2))
    a, b = pts[i0], pts[i0 + 1]
    return math.atan2(b[1] - a[1], b[0] - a[0])


def curl_line(base, ang0, seg, n, curl, curl_pow=1.0):
    """Polyline starting at `base` heading `ang0`, each segment turning a bit more
    (total turn = curl).  curl_pow > 1 concentrates the curl near the tip."""
    pts = [base]
    x, y = base
    for i in range(n):
        t = (i + 1) / n
        a = ang0 + curl * (t ** curl_pow)
        x += math.cos(a) * seg
        y += math.sin(a) * seg
        pts.append((x, y))
    return pts


# ------------------------------------------------------------------ frame ops
def shake(f, dx, dy=0):
    out = np.zeros_like(f)
    h, w = f.shape[:2]
    ys0, ys1 = max(0, dy), h + min(0, dy)
    xs0, xs1 = max(0, dx), w + min(0, dx)
    out[ys0:ys1, xs0:xs1] = f[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
    return out


def over(dst, src):
    """Alpha-over (hard pixels: src opaque pixels replace dst)."""
    sel = src[:, :, 3] > 0
    dst[sel] = src[sel]
    return dst


def under(dst, src):
    return FX.under(dst, src)


def empty_put(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


def motes(f, cx, cy, n, r0, r1, cols, seed, inward=0.0, arc=None, big=0.0):
    """Scatter glowing motes on empty pixels around (cx, cy). `arc` = (a0, a1)."""
    rng = np.random.default_rng(seed)
    h, w = f.shape[:2]
    for _ in range(n):
        if arc:
            a = arc[0] + (arc[1] - arc[0]) * rng.random()
        else:
            a = rng.random() * TAU
        d = (r0 + (r1 - r0) * rng.random()) * (1 - inward)
        x, y = int(cx + math.cos(a) * d), int(cy + math.sin(a) * d)
        c = cols[int(rng.integers(0, len(cols)))]
        if 0 <= x < w and 0 <= y < h and f[y, x, 3] == 0:
            f[y, x] = c
            if rng.random() < big:
                empty_put(f, x + 1, y, c)
                empty_put(f, x, y + 1, c)
                empty_put(f, x + 1, y + 1, c)
    return f


def ring(f, cx, cy, r, col, a0=-math.pi, a1=math.pi, dotted=0, only_empty=True):
    for (x, y) in FX.ring_pixels(cx, cy, r):
        a = math.atan2(y + 0.5 - cy, x + 0.5 - cx)
        if not (a0 <= a <= a1):
            continue
        if dotted and (x + y) % dotted:
            continue
        if 0 <= x < f.shape[1] and 0 <= y < f.shape[0]:
            if only_empty and f[y, x, 3] != 0:
                continue
            f[y, x] = col
    return f


def streak(f, p0, p1, col, only_empty=True):
    """1px line of colour (motion streaks)."""
    x0, y0 = p0
    x1, y1 = p1
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n):
        t = i / max(1, n - 1)
        x, y = int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t))
        if 0 <= x < f.shape[1] and 0 <= y < f.shape[0]:
            if only_empty and f[y, x, 3] != 0:
                continue
            f[y, x] = col
    return f


def glow_orb(f, cx, cy, r, cols, only_empty=False):
    """Hard-banded glowing orb (cols bright -> dark from the centre)."""
    h, w = f.shape[:2]
    for y in range(max(0, int(cy - r - 1)), min(h, int(cy + r + 2))):
        for x in range(max(0, int(cx - r - 1)), min(w, int(cx + r + 2))):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d <= r:
                if only_empty and f[y, x, 3] != 0:
                    continue
                band = min(len(cols) - 1, int(d / max(r, 1e-3) * len(cols)))
                f[y, x] = cols[band]
    return f


def dust_cloud(f, cx, cy, r, cols, seed, flat=0.55, n=4):
    """Low dust/snow cloud made of several jagged bursts, flattened vertically."""
    rng = np.random.default_rng(seed)
    tmp = np.zeros_like(f)
    for k in range(n):
        ox = (rng.random() - 0.5) * r * 2.2
        oy = (rng.random() - 0.3) * r * 0.5
        rr = r * (0.55 + 0.45 * rng.random())
        FX.burst(tmp, cx + ox, cy + oy, rr, cols, seed=seed * 7 + k, jag=0.45)
    # flatten: keep only rows near the ground line
    h = f.shape[0]
    ys = np.arange(h)[:, None]
    keep = ys >= cy - r * flat * 1.6
    tmp[~np.broadcast_to(keep, tmp.shape[:2])] = 0
    sel = (tmp[:, :, 3] > 0) & (f[:, :, 3] == 0)
    f[sel] = tmp[sel]
    return f


def debris(f, pts, light, dark):
    for (x, y, s) in pts:
        FX.chunk(f, x, y, s, light, dark)
    return f


def shard(f, x, y, ang, ln, light, mid, dark=OUTLINE):
    """Small flying crystal/ice shard (bright tip, darker tail, outline)."""
    c, s = math.cos(ang), math.sin(ang)
    pts = []
    for k in range(int(ln)):
        pts.append((int(round(x + c * k)), int(round(y + s * k)), k))
    for (px, py, k) in pts:
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            empty_put(f, px + ox, py + oy, dark)
    for (px, py, k) in pts:
        put(f, px, py, light if k >= ln - 2 else mid)
    return f


__all__ = ["TAU", "C", "ramp", "mat", "lerp2", "bezier", "chaikin", "polar", "seg_dir", "curl_line", "shake",
           "over", "under", "empty_put", "motes", "ring", "streak", "glow_orb", "dust_cloud", "debris", "shard"]


def split_frame(img, cx, cy, ang, offs, hot, seed=0, jag=2.5, cuts=1):
    """Break a rendered frame into pieces along jagged cut lines through (cx, cy)
    (angles ang, ang + pi/cuts ...), move each piece by offs[k], paint the cut
    edges with the `hot` colour and re-outline every piece."""
    from raster import outline_rgba
    h, w = img.shape[:2]
    ys, xs = np.mgrid[0:h, 0:w]
    xs = xs + 0.5
    ys = ys + 0.5
    rng = np.random.default_rng(seed)
    label = np.zeros((h, w), np.int32)
    for c in range(cuts):
        a = ang + c * math.pi / cuts
        nx, ny = -math.sin(a), math.cos(a)
        tx, ty = math.cos(a), math.sin(a)
        t = (xs - cx) * tx + (ys - cy) * ty
        ph = rng.random(3) * TAU
        noise = jag * (0.6 * np.sin(t * 0.55 + ph[0]) + 0.3 * np.sin(t * 1.3 + ph[1]) + 0.25 * np.sign(
            np.sin(t * 2.1 + ph[2])))
        side = ((xs - cx) * nx + (ys - cy) * ny + noise) > 0
        label = label * 2 + side.astype(np.int32)
    out = np.zeros_like(img)
    opaque = img[:, :, 3] > 0
    for k in range(2 ** cuts):
        sel = opaque & (label == k)
        if not sel.any():
            continue
        piece = np.zeros_like(img)
        piece[sel] = img[sel]
        # hot cut edge: opaque pixels of this piece touching pixels of another piece
        other = opaque & (label != k)
        edge = np.zeros((h, w), bool)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            edge |= np.roll(np.roll(other, dy, 0), dx, 1)
        edge &= sel
        piece[edge] = hot
        dx, dy = offs[k % len(offs)]
        piece = shake(piece, int(round(dx)), int(round(dy)))
        outline_rgba(piece)
        over(out, piece)
    return out
