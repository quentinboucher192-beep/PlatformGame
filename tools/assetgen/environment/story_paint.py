"""Painting toolkit of the giga bosses' story illustrations (stories.py).

A float RGB canvas (0..1, 960x540) painted in layers: gradients, fractal noise (nebulae, rock,
dust), soft shapes drawn supersampled with PIL (anti-aliased masks), glows, light rays, shaded
spheres, rim lights; then the finish (bloom, tone curve, vignette, a fine ordered dither against
banding). Pure numpy + PIL, deterministic (every random draw comes from an explicit seed).
"""
import math

import numpy as np
from PIL import Image, ImageDraw

W, H = 960, 540
SS = 2  # supersampling of the masks


# --------------------------------------------------------------------------- colours

def rgb(c):
    """'#rrggbb' or (r, g, b) 0..255 -> float array 0..1."""
    if isinstance(c, str):
        c = c.lstrip("#")
        return np.array([int(c[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)
    c = np.asarray(c, np.float32)
    return c / 255.0 if c.max() > 1.0 else c


def mix(a, b, t):
    return rgb(a) * (1.0 - t) + rgb(b) * t


# --------------------------------------------------------------------------- blur, noise

def _box(a, r, axis):
    if r < 1:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    p = np.pad(a, pad, mode="edge")
    c = np.cumsum(p, axis=axis, dtype=np.float64)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return ((hi - lo) / (2 * r + 1)).astype(np.float32)


def blur(a, sigma):
    """Gaussian blur (three box passes) of a (h, w) or (h, w, c) array."""
    if sigma <= 0.3:
        return a
    w = math.sqrt(12.0 * sigma * sigma / 3.0 + 1.0)
    r = max(1, int(round((w - 1.0) / 2.0)))
    out = a.astype(np.float32)
    for _ in range(3):
        out = _box(out, r, 0)
        out = _box(out, r, 1)
    return out


def fbm(seed, cell=160.0, octaves=5, gain=0.5, w=W, h=H):
    """Fractal value noise in 0..1 (bicubic octaves of random grids)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        c = max(2.0, cell / (2 ** o))
        gw, gh = int(w / c) + 3, int(h / c) + 3
        g = rng.random((gh, gw)).astype(np.float32)
        im = Image.fromarray(g, mode="F").resize((int(gw * c), int(gh * c)), Image.BICUBIC)
        arr = np.asarray(im, np.float32)
        ox, oy = int(rng.integers(0, max(1, arr.shape[1] - w))), int(rng.integers(0, max(1, arr.shape[0] - h)))
        out += arr[oy:oy + h, ox:ox + w] * amp
        tot += amp
        amp *= gain
    out /= tot
    lo, hi = np.percentile(out, 1), np.percentile(out, 99)
    return np.clip((out - lo) / max(1e-6, hi - lo), 0.0, 1.0)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# --------------------------------------------------------------------------- masks

class Mask:
    """A soft mask drawn supersampled (anti-aliased), in canvas coordinates."""

    def __init__(self, w=W, h=H, ss=SS):
        self.w, self.h, self.ss = w, h, ss
        self.im = Image.new("L", (w * ss, h * ss), 0)
        self.d = ImageDraw.Draw(self.im)

    def _p(self, pts):
        return [(float(x) * self.ss, float(y) * self.ss) for x, y in pts]

    def poly(self, pts, v=255):
        self.d.polygon(self._p(pts), fill=int(v))
        return self

    def ellipse(self, cx, cy, rx, ry, v=255):
        s = self.ss
        self.d.ellipse([(cx - rx) * s, (cy - ry) * s, (cx + rx) * s, (cy + ry) * s], fill=int(v))
        return self

    def circle(self, cx, cy, r, v=255):
        return self.ellipse(cx, cy, r, r, v)

    def line(self, pts, width, v=255):
        s = self.ss
        p = self._p(pts)
        self.d.line(p, fill=int(v), width=max(1, int(round(width * s))), joint="curve")
        for x, y in (p[0], p[-1]):  # round caps
            r = width * s / 2.0
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=int(v))
        return self

    def arc(self, cx, cy, rx, ry, a0, a1, width, v=255, steps=24):
        pts = [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
                cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]
        return self.line(pts, width, v)

    def rotated_ellipse(self, cx, cy, rx, ry, angle, v=255, steps=40):
        a = math.radians(angle)
        ca, sa = math.cos(a), math.sin(a)
        pts = []
        for i in range(steps):
            t = 2 * math.pi * i / steps
            x, y = rx * math.cos(t), ry * math.sin(t)
            pts.append((cx + x * ca - y * sa, cy + x * sa + y * ca))
        return self.poly(pts, v)

    def get(self, soft=0.0):
        m = np.asarray(self.im.reduce(self.ss), np.float32) / 255.0
        return blur(m, soft) if soft > 0 else m


def tapered(points, w0, w1):
    """A polygon along a polyline whose width goes from w0 to w1 (tails, necks, tubes, rays)."""
    left, right = [], []
    n = len(points)
    for i, (x, y) in enumerate(points):
        if i == 0:
            dx, dy = points[1][0] - x, points[1][1] - y
        elif i == n - 1:
            dx, dy = x - points[i - 1][0], y - points[i - 1][1]
        else:
            dx, dy = points[i + 1][0] - points[i - 1][0], points[i + 1][1] - points[i - 1][1]
        L = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / L, dx / L
        w = (w0 + (w1 - w0) * i / (n - 1)) / 2.0
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
    return left + right[::-1]


def bezier(p0, p1, p2, p3=None, n=24):
    pts = []
    for i in range(n + 1):
        t = i / n
        if p3 is None:
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        else:
            x = (1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0] + t ** 3 * p3[0]
            y = (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1] + t ** 3 * p3[1]
        pts.append((x, y))
    return pts


def blob(cx, cy, r, seed, n=18, rough=0.18, sy=1.0):
    """A rough round outline (rocks, clouds, boulders)."""
    rng = np.random.default_rng(seed)
    k = rng.random(n) * 2 - 1
    k = (k + np.roll(k, 1) + np.roll(k, -1)) / 3.0
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        rr = r * (1.0 + rough * k[i])
        pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr * sy))
    return pts


# --------------------------------------------------------------------------- canvas

class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.img = np.zeros((h, w, 3), np.float32)
        yy, xx = np.mgrid[0:h, 0:w]
        self.yy, self.xx = yy.astype(np.float32), xx.astype(np.float32)

    # ---- fills
    def vgradient(self, stops):
        """Vertical gradient: [(t, colour), …] with t from 0 (top) to 1 (bottom)."""
        t = self.yy / (self.h - 1)
        out = np.zeros_like(self.img)
        ts = [s[0] for s in stops]
        cs = [rgb(s[1]) for s in stops]
        for ch in range(3):
            out[..., ch] = np.interp(t, ts, [c[ch] for c in cs])
        self.img = out
        return self

    def gradient_field(self, x0, y0, x1, y1, c0, c1):
        """A linear gradient (h, w, 3) from (x0, y0) to (x1, y1), to paint shapes with."""
        dx, dy = x1 - x0, y1 - y0
        L2 = dx * dx + dy * dy or 1.0
        t = np.clip(((self.xx - x0) * dx + (self.yy - y0) * dy) / L2, 0.0, 1.0)[..., None]
        return rgb(c0) * (1 - t) + rgb(c1) * t

    def dist(self, cx, cy, sx=1.0, sy=1.0):
        return np.sqrt(((self.xx - cx) / sx) ** 2 + ((self.yy - cy) / sy) ** 2)

    # ---- compositing
    def over(self, color, mask, alpha=1.0):
        m = (mask * alpha)[..., None]
        col = rgb(color) if not isinstance(color, np.ndarray) or color.ndim == 1 else color
        self.img = self.img * (1.0 - m) + col * m
        return self

    def add(self, color, mask, k=1.0):
        col = rgb(color) if not isinstance(color, np.ndarray) or color.ndim == 1 else color
        self.img = self.img + col * (mask * k)[..., None]
        return self

    def multiply(self, color, mask, k=1.0):
        col = rgb(color) if not isinstance(color, np.ndarray) or color.ndim == 1 else color
        m = (mask * k)[..., None]
        self.img = self.img * (1.0 - m + col * m)
        return self

    def glow(self, cx, cy, r, color, k=1.0, tail=0.35):
        """A soft light: a gaussian core and a long halo, added."""
        R = int(r * 7) + 2
        x0, x1 = max(0, int(cx) - R), min(self.w, int(cx) + R)
        y0, y1 = max(0, int(cy) - R), min(self.h, int(cy) + R)
        if x0 >= x1 or y0 >= y1:
            return self
        xx, yy = self.xx[y0:y1, x0:x1], self.yy[y0:y1, x0:x1]
        d2 = ((xx - cx) ** 2 + (yy - cy) ** 2) / (r * r)
        g = np.exp(-d2) * (1.0 - tail) + tail / (1.0 + d2 * 3.0)
        g *= smoothstep(R * R / (r * r), R * R / (r * r) * 0.6, d2)  # fades out before the window's edge
        self.img[y0:y1, x0:x1] += rgb(color) * (g * k)[..., None]
        return self

    def rays(self, cx, cy, n, length, width, color, k, seed, a0=0.0, spread=360.0, soft=3.0):
        """Soft beams from a point (a star's rays, a light through a crack)."""
        rng = np.random.default_rng(seed)
        m = Mask()
        for i in range(n):
            a = math.radians(a0 + spread * (i + rng.random() * 0.6) / n)
            L = length * (0.55 + 0.45 * rng.random())
            wv = width * (0.5 + rng.random())
            tip = (cx + math.cos(a) * L, cy + math.sin(a) * L)
            nx, ny = -math.sin(a) * wv, math.cos(a) * wv
            m.poly([(cx + nx, cy + ny), tip, (cx - nx, cy - ny)], v=110 + int(100 * rng.random()))
        fall = np.clip(1.0 - self.dist(cx, cy) / length, 0.0, 1.0) ** 1.6
        self.add(color, m.get(soft) * fall, k)
        return self

    def stars(self, seed, n, color="#ffffff", k=1.0, region=None, big=0.04):
        """A starfield: many faint points, a few bright ones with a halo and a cross."""
        rng = np.random.default_rng(seed)
        x0, y0, x1, y1 = region or (0, 0, self.w, self.h)
        layer = np.zeros((self.h, self.w), np.float32)
        xs = rng.uniform(x0, x1, n).astype(int).clip(0, self.w - 1)
        ys = rng.uniform(y0, y1, n).astype(int).clip(0, self.h - 1)
        b = rng.random(n) ** 3
        np.add.at(layer, (ys, xs), 0.25 + 1.6 * b)
        tint = rgb(color)
        self.add(tint, blur(layer, 0.6) * 1.6, k)
        for i in np.nonzero(b > 1.0 - big)[0]:
            r = 1.2 + 2.4 * rng.random()
            self.glow(xs[i], ys[i], r, tint, 0.7 * k)
            m = Mask()
            L = r * (4 + 4 * rng.random())
            m.line([(xs[i] - L, ys[i]), (xs[i] + L, ys[i])], 0.8, 200)
            m.line([(xs[i], ys[i] - L), (xs[i], ys[i] + L)], 0.8, 200)
            self.add(tint, m.get(0.6) * np.clip(1 - self.dist(xs[i], ys[i]) / L, 0, 1), 0.8 * k)
        return self

    def nebula(self, seed, c1, c2, k=1.0, cell=220.0, power=2.2, region_mask=None):
        """A cloud of gas: noise shaped and tinted between two colours, added."""
        n = fbm(seed, cell, 6)
        n2 = fbm(seed + 1, cell * 0.6, 5)
        d = np.clip(n * 0.75 + n2 * 0.35 - 0.25, 0, 1) ** power
        col = rgb(c1)[None, None, :] * (1 - n2[..., None]) + rgb(c2)[None, None, :] * n2[..., None]
        if region_mask is not None:
            d = d * region_mask
        self.img = self.img + col * (d * k)[..., None]
        return self

    def sphere(self, cx, cy, r, base, light=(-0.55, -0.6), ambient=0.12, texture=None, tex_k=0.35,
               rim=None, rim_k=0.6, spec=0.0, shade_color=None):
        """A lit sphere (planets, stars' bodies, pebbles), anti-aliased, with an optional texture
        (h, w) in 0..1, a rim light colour and a specular spot."""
        dx = (self.xx - cx) / r
        dy = (self.yy - cy) / r
        d2 = dx * dx + dy * dy
        d = np.sqrt(d2)
        inside = smoothstep(1.0 + 1.2 / r, 1.0 - 1.2 / r, d)
        nz = np.sqrt(np.clip(1.0 - d2, 0.0, 1.0))
        lx, ly = light
        lz = math.sqrt(max(0.0, 1.0 - lx * lx - ly * ly))
        ndl = np.clip(dx * lx + dy * ly + nz * lz, 0.0, 1.0)
        shade = ambient + (1.0 - ambient) * ndl
        col = np.broadcast_to(rgb(base), self.img.shape).copy()
        if texture is not None:
            col = col * (1.0 - tex_k + tex_k * 2.0 * texture[..., None])
        lit = col * shade[..., None]
        if shade_color is not None:
            lit = lit + rgb(shade_color) * ((1.0 - ndl) * 0.35)[..., None] * (1.0 - ambient)
        if spec > 0:
            hx, hy = lx * 0.5, ly * 0.5
            s = np.exp(-(((dx - hx) ** 2 + (dy - hy) ** 2) / 0.04))
            lit = lit + spec * s[..., None]
        self.img = self.img * (1.0 - inside[..., None]) + lit * inside[..., None]
        if rim is not None:
            edge = smoothstep(0.7, 1.0, d) * inside * np.clip(-(dx * lx + dy * ly) * 1.2 + 0.2, 0, 1)
            self.add(rim, edge, rim_k)
        return self

    def haze(self, y0, y1, color, k):
        """Atmospheric haze between two heights (depth)."""
        t = smoothstep(y0, y1, self.yy)
        self.over(color, t, k)
        return self


def rim(mask, dx, dy, soft=0.8):
    """The edge of `mask` facing a light from (dx, dy) (pixels): rim lights."""
    shifted = np.roll(np.roll(mask, int(round(dy)), axis=0), int(round(dx)), axis=1)
    return blur(np.clip(mask - shifted, 0.0, 1.0), soft)


# --------------------------------------------------------------------------- finish

BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 - 0.5


def finish(c, bloom=0.55, threshold=0.62, vignette=0.42, exposure=1.0, lift=0.0, tint=None, tint_k=0.0):
    """Bloom, a soft tone curve, a vignette and an ordered dither -> uint8 (h, w, 3)."""
    img = c.img * exposure
    bright = np.clip(img - threshold, 0.0, None)
    img = img + blur(bright, 22.0) * bloom + blur(bright, 6.0) * bloom * 0.6
    if tint is not None and tint_k > 0:
        lum = img.mean(axis=2, keepdims=True)
        img = img * (1 - tint_k) + lum * rgb(tint) * tint_k * 1.6
    # Soft shoulder above 0.75: highlights roll off instead of clipping (saturated glows).
    knee = 0.75
    over = np.clip(img - knee, 0.0, None)
    img = np.minimum(img, knee) + (1.0 - knee) * (1.0 - np.exp(-over / (1.0 - knee)))
    nx = (c.xx / c.w - 0.5) * 2.0
    ny = (c.yy / c.h - 0.5) * 2.0
    v = 1.0 - vignette * np.clip((nx * nx * 0.55 + ny * ny * 0.75) - 0.12, 0.0, 1.0)
    img = img * v[..., None] + lift
    d = np.tile(BAYER4, (c.h // 4 + 1, c.w // 4 + 1))[:c.h, :c.w]
    out = np.clip(img * 255.0 + d[..., None] * 1.2 + 0.5, 0, 255).astype(np.uint8)
    return out


def save(arr, path):
    Image.fromarray(arr, "RGB").save(path, optimize=True)
