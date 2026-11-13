"""Shared pixel-art helpers for the environment generators.

Images are numpy uint8 arrays of shape (H, W, 4) (RGBA). Everything is deterministic:
random generators are always created from explicit seeds.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETGEN = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(ASSETGEN))
ASSETS = os.path.join(REPO, "assets")
REVIEW_DIR = "/tmp/claude-0/assetreview/environment"

if ASSETGEN not in sys.path:
    sys.path.insert(0, ASSETGEN)

from common import palette as P  # noqa: E402  (shared palette module)

T = (0, 0, 0, 0)


# ---------------------------------------------------------------------------
# colours
# ---------------------------------------------------------------------------
def col(c, a=None):
    """Hex string / tuple -> RGBA tuple."""
    if isinstance(c, str):
        r = P.hex_rgba(c)
    else:
        r = tuple(int(v) for v in c)
        if len(r) == 3:
            r = r + (255,)
    if a is not None:
        r = r[:3] + (int(a),)
    return r


def hexstr(c):
    c = col(c)
    return "#%02x%02x%02x" % c[:3]


def mix(a, b, t):
    a, b = col(a), col(b)
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(4))


def rng(seed):
    return np.random.default_rng(seed)


# ---------------------------------------------------------------------------
# canvas primitives
# ---------------------------------------------------------------------------
def new(w, h, c=T):
    img = np.zeros((h, w, 4), np.uint8)
    if c != T:
        img[:, :] = col(c)
    return img


def inb(img, x, y):
    return 0 <= x < img.shape[1] and 0 <= y < img.shape[0]


def put(img, x, y, c):
    x, y = int(x), int(y)
    if inb(img, x, y):
        img[y, x] = col(c)


def get(img, x, y):
    if inb(img, x, y):
        return tuple(int(v) for v in img[y, x])
    return T


def opaque(img, x, y):
    return inb(img, x, y) and img[y, x, 3] > 0


def rect(img, x, y, w, h, c):
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(img.shape[1], x + w), min(img.shape[0], y + h)
    if x1 > x0 and y1 > y0:
        img[y0:y1, x0:x1] = col(c)


def hline(img, x0, x1, y, c):
    for x in range(min(x0, x1), max(x0, x1) + 1):
        put(img, x, y, c)


def vline(img, x, y0, y1, c):
    for y in range(min(y0, y1), max(y0, y1) + 1):
        put(img, x, y, c)


def line(img, x0, y0, x1, y1, c):
    """Bresenham line."""
    x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        put(img, x0, y0, c)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def line_pts(x0, y0, x1, y1):
    pts = []
    x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        pts.append((x0, y0))
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy
    return pts


def mask_draw(w, h, fn):
    """Run fn(ImageDraw) on a 1-bit canvas and return a bool mask (h, w)."""
    m = Image.new("L", (w, h), 0)
    fn(ImageDraw.Draw(m))
    return np.array(m) > 127


def poly_mask(w, h, pts):
    return mask_draw(w, h, lambda d: d.polygon([tuple(p) for p in pts], fill=255))


def ellipse_mask(w, h, cx, cy, rx, ry):
    yy, xx = np.mgrid[0:h, 0:w]
    return ((xx + 0.5 - cx) / max(rx, 0.01)) ** 2 + ((yy + 0.5 - cy) / max(ry, 0.01)) ** 2 <= 1.0


def disc_mask(w, h, cx, cy, r):
    return ellipse_mask(w, h, cx, cy, r, r)


def fill_mask(img, m, c):
    img[m] = col(c)


def paste(dst, src, ox, oy, wrap_x=False):
    """Alpha-composite src over dst at (ox, oy). Optional horizontal wrap."""
    if wrap_x:
        W = dst.shape[1]
        for k in (-1, 0, 1):
            paste(dst, src, ox + k * W, oy)
        return dst
    h, w = src.shape[:2]
    x0, y0 = max(0, ox), max(0, oy)
    x1, y1 = min(dst.shape[1], ox + w), min(dst.shape[0], oy + h)
    if x1 <= x0 or y1 <= y0:
        return dst
    s = src[y0 - oy:y1 - oy, x0 - ox:x1 - ox].astype(np.float32) / 255.0
    d = dst[y0:y1, x0:x1].astype(np.float32) / 255.0
    sa = s[..., 3:4]
    da = d[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = np.where(oa > 0, (s[..., :3] * sa + d[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-6), 0)
    out = np.concatenate([rgb, oa], axis=-1)
    dst[y0:y1, x0:x1] = np.clip(out * 255 + 0.5, 0, 255).astype(np.uint8)
    return dst


def paste_hard(dst, src, ox, oy):
    """Copy opaque pixels of src (alpha>0) onto dst without blending."""
    h, w = src.shape[:2]
    for y in range(h):
        for x in range(w):
            if src[y, x, 3] > 0 and inb(dst, ox + x, oy + y):
                dst[oy + y, ox + x] = src[y, x]
    return dst


def flip_x(img):
    return img[:, ::-1].copy()


def flip_y(img):
    return img[::-1, :].copy()


def outline(img, c=None, diagonal=False, wrap_x=False):
    """Add a 1px outline around opaque pixels (in transparent neighbours)."""
    c = col(c if c is not None else P.OUTLINE)
    a = img[..., 3] > 0
    n = np.zeros_like(a)
    shifts = [(0, 1), (0, -1), (1, 0), (-1, 0)]
    if diagonal:
        shifts += [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    for dy, dx in shifts:
        if wrap_x:
            s = np.roll(a, dx, axis=1)
        else:
            s = np.zeros_like(a)
            if dx > 0:
                s[:, dx:] = a[:, :-dx]
            elif dx < 0:
                s[:, :dx] = a[:, -dx:]
            else:
                s = a.copy()
        if dy > 0:
            s2 = np.zeros_like(a)
            s2[dy:] = s[:-dy]
            s = s2
        elif dy < 0:
            s2 = np.zeros_like(a)
            s2[:dy] = s[-dy:]
            s = s2
        n |= s
    edge = n & ~a
    img[edge] = c
    return img


def inner_edge(m):
    """Pixels of a bool mask that touch the outside (4-neighbourhood)."""
    p = np.pad(m, 1, constant_values=False)
    inner = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return m & ~inner


def shift_mask(m, dx, dy, wrap_x=False):
    out = np.zeros_like(m)
    h, w = m.shape
    if wrap_x:
        m = np.roll(m, dx, axis=1)
        dx = 0
    xs0, xs1 = max(0, -dx), min(w, w - dx)
    ys0, ys1 = max(0, -dy), min(h, h - dy)
    out[ys0 + dy:ys1 + dy, xs0 + dx:xs1 + dx] = m[ys0:ys1, xs0:xs1]
    return out


def light_edges(m, wrap_x=False):
    """Return (top_left_edge, bottom_right_edge) masks of a shape (pixels whose
    upper/left neighbour is outside, resp. lower/right)."""
    up = shift_mask(m, 0, 1, wrap_x)
    left = shift_mask(m, 1, 0, wrap_x)
    down = shift_mask(m, 0, -1, wrap_x)
    right = shift_mask(m, -1, 0, wrap_x)
    tl = m & (~up | ~left)
    br = m & (~down | ~right)
    return tl, br


# ---------------------------------------------------------------------------
# dithering / noise
# ---------------------------------------------------------------------------
BAYER4 = (np.array([[0, 8, 2, 10],
                    [12, 4, 14, 6],
                    [3, 11, 1, 9],
                    [15, 7, 13, 5]], np.float32) + 0.5) / 16.0

BAYER8 = None


def _bayer8():
    global BAYER8
    if BAYER8 is None:
        b = np.array([[0, 2], [3, 1]])
        for _ in range(2):
            b = np.block([[4 * b + 0, 4 * b + 2], [4 * b + 3, 4 * b + 1]])
        BAYER8 = (b.astype(np.float32) + 0.5) / 64.0
    return BAYER8


def bayer(h, w, size=4, ox=0, oy=0):
    b = BAYER4 if size == 4 else _bayer8()
    n = b.shape[0]
    yy = (np.arange(h) + oy) % n
    xx = (np.arange(w) + ox) % n
    return b[yy][:, xx]


def ramp_index(t, n, dither, strength=1.0):
    """Map t in [0,1] to an index in [0, n-1] with ordered dithering."""
    v = t * (n - 1) + (dither - 0.5) * strength
    return np.clip(np.floor(v + 0.5), 0, n - 1).astype(np.int32)


def apply_ramp(t, ramp, dither=None, strength=1.0, alpha=None):
    """t (H,W) floats in [0,1] -> RGBA image using a colour ramp."""
    h, w = t.shape
    if dither is None:
        dither = bayer(h, w)
    idx = ramp_index(np.clip(t, 0, 1), len(ramp), dither, strength)
    pal = np.array([col(c) for c in ramp], np.uint8)
    out = pal[idx]
    if alpha is not None:
        out[..., 3] = np.where(alpha, out[..., 3], 0)
    return out


def value_noise(w, h, cell_x, cell_y=None, seed=0, wrap_x=True, wrap_y=False):
    """Smooth value noise in [0,1]. With wrap_x, w must be a multiple of cell_x."""
    cell_y = cell_y or cell_x
    r = rng(seed)
    gw = int(math.ceil(w / cell_x)) + (0 if wrap_x else 1)
    gh = int(math.ceil(h / cell_y)) + (0 if wrap_y else 1)
    grid = r.random((gh + 1, gw + 1)).astype(np.float32)
    if wrap_x:
        grid[:, gw] = grid[:, 0]
    if wrap_y:
        grid[gh, :] = grid[0, :]
    xs = np.arange(w, dtype=np.float32) / cell_x
    ys = np.arange(h, dtype=np.float32) / cell_y
    x0 = np.floor(xs).astype(int)
    y0 = np.floor(ys).astype(int)
    fx = xs - x0
    fy = ys - y0
    fx = fx * fx * (3 - 2 * fx)
    fy = fy * fy * (3 - 2 * fy)
    if wrap_x:
        x0 = x0 % gw
    if wrap_y:
        y0 = y0 % gh
    g00 = grid[y0][:, x0]
    g10 = grid[y0][:, x0 + 1]
    g01 = grid[y0 + 1][:, x0]
    g11 = grid[y0 + 1][:, x0 + 1]
    fxm = fx[None, :]
    fym = fy[:, None]
    top = g00 + (g10 - g00) * fxm
    bot = g01 + (g11 - g01) * fxm
    return top + (bot - top) * fym


def fbm(w, h, cell, octaves=4, seed=0, wrap_x=True, wrap_y=False, gain=0.5, cell_y=None):
    total = np.zeros((h, w), np.float32)
    amp, norm = 1.0, 0.0
    cx = cell
    cy = cell_y or cell
    for o in range(octaves):
        total += amp * value_noise(w, h, max(1, int(cx)), max(1, int(cy)), seed + o * 101, wrap_x, wrap_y)
        norm += amp
        amp *= gain
        cx /= 2
        cy /= 2
    return total / norm


def noise1d(n, cell, seed, wrap=True):
    """Periodic smooth 1D noise of length n in [0,1]."""
    return value_noise(n, 1, cell, 1, seed, wrap_x=wrap)[0]


def fbm1d(n, cell, octaves, seed, wrap=True, gain=0.5):
    total = np.zeros(n, np.float32)
    amp, norm = 1.0, 0.0
    c = cell
    for o in range(octaves):
        total += amp * noise1d(n, max(1, int(c)), seed + 37 * o, wrap)
        norm += amp
        amp *= gain
        c /= 2
    return total / norm


class Noise3:
    """3D value noise (for spheres)."""

    def __init__(self, seed, size=64):
        self.size = size
        self.g = rng(seed).random((size, size, size)).astype(np.float32)

    def __call__(self, x, y, z):
        s = self.size
        xi, yi, zi = np.floor(x).astype(int), np.floor(y).astype(int), np.floor(z).astype(int)
        fx, fy, fz = x - xi, y - yi, z - zi
        fx = fx * fx * (3 - 2 * fx)
        fy = fy * fy * (3 - 2 * fy)
        fz = fz * fz * (3 - 2 * fz)
        g = self.g

        def G(a, b, c):
            return g[(xi + a) % s, (yi + b) % s, (zi + c) % s]

        x00 = G(0, 0, 0) + (G(1, 0, 0) - G(0, 0, 0)) * fx
        x10 = G(0, 1, 0) + (G(1, 1, 0) - G(0, 1, 0)) * fx
        x01 = G(0, 0, 1) + (G(1, 0, 1) - G(0, 0, 1)) * fx
        x11 = G(0, 1, 1) + (G(1, 1, 1) - G(0, 1, 1)) * fx
        y0 = x00 + (x10 - x00) * fy
        y1 = x01 + (x11 - x01) * fy
        return y0 + (y1 - y0) * fz

    def fbm(self, x, y, z, octaves=4, gain=0.5, lac=2.0):
        t, amp, norm, f = 0, 1.0, 0, 1.0
        for o in range(octaves):
            t = t + amp * self(x * f + o * 17.3, y * f + o * 9.1, z * f + o * 3.7)
            norm += amp
            amp *= gain
            f *= lac
        return t / norm


# ---------------------------------------------------------------------------
# glow (used for previews and for baked glows in backgrounds)
# ---------------------------------------------------------------------------
def add_glow(img, cx, cy, radius, c, strength=0.6, wrap_x=False):
    """Additive soft glow, baked into RGB (alpha raised where needed)."""
    h, w = img.shape[:2]
    c = col(c)
    xs = np.arange(w)
    if wrap_x:
        dxs = np.minimum(np.abs(xs - cx), w - np.abs(xs - cx))
    else:
        dxs = np.abs(xs - cx)
    ys = np.arange(h)
    d = np.sqrt(dxs[None, :] ** 2 + (ys[:, None] - cy) ** 2) / radius
    a = np.clip(1 - d, 0, 1) ** 2 * strength
    f = img.astype(np.float32)
    for i in range(3):
        f[..., i] = np.clip(f[..., i] + c[i] * a, 0, 255)
    img[:] = f.astype(np.uint8)
    return img


# ---------------------------------------------------------------------------
# io
# ---------------------------------------------------------------------------
def to_pil(img):
    return Image.fromarray(img, "RGBA")


def save_png(img, relpath):
    path = os.path.join(ASSETS, relpath)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    to_pil(img).save(path, optimize=True)
    return path


def save_json(obj, relpath):
    path = os.path.join(ASSETS, relpath)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")
    return path


def load_png(relpath):
    return np.array(Image.open(os.path.join(ASSETS, relpath)).convert("RGBA"))


def save_review(img, name, scale=1, bg=None):
    os.makedirs(REVIEW_DIR, exist_ok=True)
    if bg is not None:
        base = new(img.shape[1], img.shape[0], bg)
        img = paste(base, img, 0, 0)
    im = to_pil(img)
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    path = os.path.join(REVIEW_DIR, name)
    im.save(path)
    return path


# ---------------------------------------------------------------------------
# tiny pixel fonts (for glyph labels)
# ---------------------------------------------------------------------------
FONT3x5 = {
    "A": ["010", "101", "111", "101", "101"],
    "B": ["110", "101", "110", "101", "110"],
    "L": ["100", "100", "100", "100", "111"],
    "R": ["110", "101", "110", "101", "101"],
    "T": ["111", "010", "010", "010", "010"],
    "S": ["011", "100", "010", "001", "110"],
    "Z": ["111", "001", "010", "100", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["110", "001", "010", "100", "111"],
    "3": ["110", "001", "010", "001", "110"],
}

FONT4x6 = {
    "L": ["1000", "1000", "1000", "1000", "1000", "1111"],
    "R": ["1110", "1001", "1001", "1110", "1010", "1001"],
    "B": ["1110", "1001", "1110", "1001", "1001", "1110"],
    "T": ["1111", "0110", "0110", "0110", "0110", "0110"],
    "1": ["0110", "1110", "0110", "0110", "0110", "1111"],
    "2": ["1110", "0001", "0011", "0110", "1100", "1111"],
    "3": ["1110", "0001", "0110", "0001", "0001", "1110"],
    "S": ["0111", "1000", "0110", "0001", "0001", "1110"],
}

# bold 7x8 letters for face buttons
FONT_BOLD = {
    "A": ["0011100",
          "0110110",
          "1100011",
          "1100011",
          "1111111",
          "1100011",
          "1100011",
          "1100011"],
    "B": ["1111110",
          "1100011",
          "1100011",
          "1111110",
          "1100011",
          "1100011",
          "1100011",
          "1111110"],
    "X": ["1100011",
          "1100011",
          "0110110",
          "0011100",
          "0011100",
          "0110110",
          "1100011",
          "1100011"],
    "Y": ["1100011",
          "1100011",
          "0110110",
          "0011100",
          "0001100",
          "0001100",
          "0001100",
          "0001100"],
}


def text_mask(text, font, spacing=1):
    rows = len(next(iter(font.values())))
    glyphs = [font[ch] for ch in text]
    w = sum(len(g[0]) for g in glyphs) + spacing * (len(glyphs) - 1)
    m = np.zeros((rows, w), bool)
    x = 0
    for g in glyphs:
        for yy, row in enumerate(g):
            for xx, ch in enumerate(row):
                if ch == "1":
                    m[yy, x + xx] = True
        x += len(g[0]) + spacing
    return m


def stamp_mask(img, m, ox, oy, c):
    h, w = m.shape
    for y in range(h):
        for x in range(w):
            if m[y, x]:
                put(img, ox + x, oy + y, c)


# ---------------------------------------------------------------------------
# shape shading helpers (decor / props)
# ---------------------------------------------------------------------------
def blur(a, sigma):
    """Separable gaussian blur with zero padding."""
    if sigma <= 0:
        return a.astype(np.float32)
    r = max(1, int(sigma * 3))
    k = np.exp(-(np.arange(-r, r + 1) ** 2) / (2.0 * sigma * sigma))
    k /= k.sum()
    a = a.astype(np.float32)
    p = np.pad(a, ((r, r), (r, r)))
    tmp = np.zeros_like(p)
    for i, kv in enumerate(k):
        tmp[:, r:-r] += kv * p[:, i:i + p.shape[1] - 2 * r]
    out = np.zeros_like(a)
    for i, kv in enumerate(k):
        out += kv * tmp[i:i + a.shape[0], r:-r]
    return out


def lightness(mask, sigma=2.0, light=(-0.75, -0.66), wrap_x=False):
    """Per-pixel lighting term in [-1, 1] from a mask's smoothed normals (pillow shading)."""
    f = blur(mask.astype(np.float32), sigma)
    gy, gx = np.gradient(f)
    l = -(gx * light[0] + gy * light[1])
    m = np.abs(l[mask]).max() if mask.any() else 1.0
    return np.clip(l / max(m, 1e-6), -1, 1)


def shade_mask(mask, ramp, sigma=2.0, ambient=0.45, contrast=0.6, light=(-0.75, -0.66),
               extra=None, dither_strength=0.45, ox=0, oy=0):
    """Return an RGBA image of `mask` shaded with `ramp` (dark -> light), lit from top-left.
    `extra` is an optional (H,W) array added to the tone."""
    h, w = mask.shape
    t = ambient + contrast * 0.5 * lightness(mask, sigma, light)
    if extra is not None:
        t = t + extra
    img = apply_ramp(np.clip(t, 0, 1), ramp, bayer(h, w, 4, ox, oy), dither_strength)
    img[~mask] = 0
    return img


def bezier(p0, p1, p2, n=32, p3=None):
    ts = np.linspace(0, 1, n)
    pts = []
    for t in ts:
        if p3 is None:
            x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
            y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        else:
            x = ((1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0]
                 + t ** 3 * p3[0])
            y = ((1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1]
                 + t ** 3 * p3[1])
        pts.append((x, y))
    return pts


def thick_path_mask(w, h, pts, widths):
    """Mask of a path with per-point radius (float list or scalar)."""
    m = np.zeros((h, w), bool)
    if np.isscalar(widths):
        widths = [widths] * len(pts)
    yy, xx = np.mgrid[0:h, 0:w]
    for (x, y), r in zip(pts, widths):
        m |= (xx + 0.5 - x) ** 2 + (yy + 0.5 - y) ** 2 <= r * r
    return m


def path_pixels(pts):
    """Connected 1px pixel path through float points."""
    out = []
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        for p in line_pts(x0, y0, x1, y1):
            if not out or out[-1] != p:
                out.append(p)
    return out


def colorize_where(img, m, c):
    img[m] = col(c)
    return img


def outline_selout(img, strength=0.28, diagonal=False):
    """1px outline whose colour is a very dark shade of the neighbouring pixel
    ("selective outline"), blended towards the shared outline colour."""
    a = img[..., 3] > 0
    h, w = a.shape
    out = img.copy()
    base = np.array(col(P.OUTLINE)[:3], np.float32)
    shifts = [(0, 1), (0, -1), (1, 0), (-1, 0)]
    if diagonal:
        shifts += [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    for y in range(h):
        for x in range(w):
            if a[y, x]:
                continue
            acc, n = np.zeros(3, np.float32), 0
            for dy, dx in shifts:
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and a[yy, xx]:
                    acc += img[yy, xx, :3]
                    n += 1
            if n:
                c = base + (acc / n - base) * strength
                out[y, x] = (int(c[0]), int(c[1]), int(c[2]), 255)
    img[:] = out
    return img


def hex_arr(cs):
    return np.array([col(c)[:3] for c in cs], np.float32)


def quantize_dither(rgb, palette, dither=None, strength=1.0):
    """Ordered-dither a float RGB image (H,W,3) onto a palette: each pixel picks between its
    two nearest palette colours according to the Bayer threshold."""
    h, w = rgb.shape[:2]
    pal = hex_arr(palette)
    if dither is None:
        dither = bayer(h, w, 8)
    d = ((rgb[:, :, None, :] - pal[None, None]) ** 2).sum(-1)
    order = np.argsort(d, axis=-1)
    i1, i2 = order[..., 0], order[..., 1]
    c1, c2 = pal[i1], pal[i2]
    seg = c2 - c1
    t = ((rgb - c1) * seg).sum(-1) / np.maximum((seg * seg).sum(-1), 1e-6)
    t = np.clip(t, 0, 1)
    pick2 = t * strength > dither
    idx = np.where(pick2, i2, i1)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = pal[idx].astype(np.uint8)
    out[..., 3] = 255
    return out


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def gradient_rgb(h, stops):
    """Vertical gradient (h,3) from [(t, colour), ...]."""
    ts = np.array([s[0] for s in stops], np.float32)
    cs = hex_arr([s[1] for s in stops])
    y = np.linspace(0, 1, h)
    return np.stack([np.interp(y, ts, cs[:, i]) for i in range(3)], -1)


def dither_alpha(alpha, levels=(0, 96, 160, 224, 255), dither=None):
    """Quantize a float alpha field [0,1] to a few levels with ordered dithering."""
    h, w = alpha.shape
    if dither is None:
        dither = bayer(h, w, 4)
    lv = np.array(levels, np.float32) / 255.0
    a = np.clip(alpha, 0, 1)
    k = np.searchsorted(lv, a, side="right") - 1
    k = np.clip(k, 0, len(lv) - 2)
    lo, hi = lv[k], lv[k + 1]
    t = (a - lo) / np.maximum(hi - lo, 1e-6)
    res = np.where(t > dither, hi, lo)
    return (res * 255).astype(np.uint8)
