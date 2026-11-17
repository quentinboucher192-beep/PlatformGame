"""The scenery of Pyrrhax's story (2.60): the Confins at night in parallax layers, pixel art like
the game's backgrounds (hard pixels, ordered dithering, rim light), one sheet pixel = two units of
the story's 960x540 stage.

  ciel_nuit   the night sky, violet to crimson (the nest, the smoke, the rain of embers)
  ciel_rouge  the sky set alight by the dying star (the swallowed embers, the black glass, the frontier)
  etoiles     the stars (transparent: it drifts over the sky)
  brume       a band of mist (drifting)
  pics_loin   far obsidian spires
  pics        nearer spires, magma at their feet
  sol         the ground the hatchlings walk on
  avant       dark rocks and shards in front of everything

Written to assets/stories/pyrrhax/<name>.png.  Deterministic (fixed seeds)."""
import math
import os

import numpy as np
from PIL import Image, ImageDraw

BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 + 1 / 32.0


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)], np.float32)


def dither_ramp(t, cols):
    """t (H, W) in 0..1 -> colours of `cols` (dark -> light) with ordered dithering between steps."""
    h, w = t.shape
    n = len(cols) - 1
    x = np.clip(t, 0, 1) * n
    i0 = np.floor(x).astype(int)
    f = x - i0
    th = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    idx = np.clip(i0 + (f > th), 0, n)
    pal = np.array([rgb(c) for c in cols], np.uint8)
    return pal[idx]


def rgba_of(rgbimg, alpha):
    out = np.zeros(rgbimg.shape[:2] + (4,), np.uint8)
    out[..., :3] = rgbimg
    out[..., 3] = alpha
    return out


def sky(w, h, cols, seed, nebula=("#3a1240", "#5a1a3a")):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    t = 1.0 - yy / (h - 1)
    t = 1.0 - t ** 1.25
    rng = np.random.default_rng(seed)
    # slow nebula bands: a few sines, quantised by the dithering
    neb = np.zeros((h, w), np.float32)
    for k in range(5):
        fx, fy, ph = rng.uniform(0.006, 0.02), rng.uniform(0.01, 0.03), rng.uniform(0, 6.28)
        neb += np.sin(xx * fx + np.sin(yy * fy + ph) * 2.0 + ph) / 5.0
    t = np.clip(t + 0.07 * neb * (1 - abs(t - 0.5) * 1.4), 0, 1)
    img = dither_ramp(t, cols)
    return rgba_of(img, 255)


def stars(w, h, n, seed):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    cols = [rgb("#ffffff"), rgb("#ffe8f0"), rgb("#ffc8d8"), rgb("#c8a0e0"), rgb("#8a6aa8")]
    for _ in range(n):
        x, y = int(rng.integers(0, w)), int(rng.integers(0, h))
        y = int(y * (0.25 + 0.75 * rng.random()))  # more at the top
        b = rng.random()
        c = cols[0] if b > 0.96 else cols[1] if b > 0.85 else cols[2] if b > 0.6 else cols[3] if b > 0.3 else cols[4]
        out[y, x, :3] = c
        out[y, x, 3] = 255
        if b > 0.97:  # a few crosses
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= x + dx < w and 0 <= y + dy < h:
                    out[y + dy, x + dx, :3] = cols[3]
                    out[y + dy, x + dx, 3] = 255
    return out


def _shade_mask(mask, body, rim, deep, top_light=True, light_dx=-1):
    """Colours a silhouette: its body, a rim light on the lit side, darker at its foot."""
    h, w = mask.shape
    out = np.zeros((h, w, 4), np.uint8)
    yy = np.mgrid[0:h, 0:w][0].astype(np.float32)
    t = np.clip((yy - yy[mask].min()) / max(1.0, yy[mask].max() - yy[mask].min()), 0, 1) if mask.any() else yy
    col = dither_ramp(1.0 - t * 0.9, [deep, body, body])
    out[mask, :3] = col[mask]
    out[mask, 3] = 255
    edge = mask & ~np.roll(mask, -light_dx, axis=1)
    if top_light:
        edge |= mask & ~np.roll(mask, 1, axis=0)
    out[edge, :3] = rgb(rim)
    return out


def spires(w, h, seed, n, hmin, hmax, base, body, rim, deep, lean=0.12, wfac=(0.12, 0.22), magma=None):
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    tips = []
    for _ in range(n):
        x = rng.uniform(-20, w + 20)
        hh = rng.uniform(hmin, hmax)
        ww = hh * rng.uniform(*wfac)
        ln = rng.uniform(-lean, lean) * hh
        d.polygon([(x - ww / 2, base + 4), (x + ln, base - hh), (x + ww / 2, base + 4)], fill=255)
        tips.append((x, base - hh))
        if rng.random() < 0.6:
            h2 = hh * rng.uniform(0.35, 0.7)
            d.polygon([(x + ww * 0.15, base + 4), (x + ww * 0.45 + ln * 0.6, base - h2), (x + ww * 0.85, base + 4)], fill=255)
    d.rectangle([0, base, w, h], fill=255)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    if magma:  # glowing veins at the feet of the spires
        rng2 = np.random.default_rng(seed + 9)
        for _ in range(int(w / 18)):
            x = int(rng2.uniform(0, w))
            y = int(base + rng2.uniform(-2, 6))
            ln = int(rng2.uniform(3, 9))
            for k in range(ln):
                yy, xx = y - k, x + int(k * rng2.uniform(-0.6, 0.6))
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx]:
                    out[yy, xx, :3] = rgb(magma[min(len(magma) - 1, k * len(magma) // ln)])
    return out


def ground(w, h, seed, top, body, rim, deep, rough=4.0, rocks=0):
    rng = np.random.default_rng(seed)
    xs = np.arange(w)
    edge = top + rough * np.sin(xs * 0.031 + 1.3) + rough * 0.6 * np.sin(xs * 0.11) + rough * 0.3 * np.sin(xs * 0.37)
    yy = np.mgrid[0:h, 0:w][0]
    mask = yy >= edge[None, :]
    im = Image.fromarray((mask * 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    for _ in range(rocks):
        x = rng.uniform(0, w)
        r = rng.uniform(3, 8)
        y = edge[int(min(w - 1, max(0, x)))]
        d.polygon([(x - r * 1.4, y + 2), (x - r * 0.6, y - r), (x + r * 0.5, y - r * 1.1), (x + r * 1.5, y + 2)], fill=255)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    # pebbles and cracks: darker specks
    sp = (np.random.default_rng(seed + 1).random((h, w)) < 0.025) & mask & (yy > edge[None, :] + 3)
    out[sp, :3] = rgb(deep)
    return out


def mist(w, h, seed, col="#c8a0c0", amount=0.5):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    v = np.zeros((h, w), np.float32)
    for k in range(4):
        f, ph = rng.uniform(0.01, 0.04), rng.uniform(0, 6.28)
        v += np.sin(xx * f + ph + np.sin(yy * 0.08 + ph) * 1.5) / 4.0
    band = np.exp(-((yy - h * 0.5) / (h * 0.32)) ** 2)
    a = np.clip((0.5 + 0.5 * v) * band * amount, 0, 1)
    th = np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]
    on = a > th
    out = np.zeros((h, w, 4), np.uint8)
    out[on, :3] = rgb(col)
    out[on, 3] = 110
    return out


def shards(w, h, seed, body, rim, deep):
    """Dark foreground: rocks and obsidian shards rising from the bottom edge (corners)."""
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for side in (0, 1):
        x0 = 0 if side == 0 else w
        for k in range(7):
            x = x0 + (1 if side == 0 else -1) * rng.uniform(0, w * 0.22)
            hh = rng.uniform(h * 0.25, h * 0.95) * (1.0 - abs(x - x0) / (w * 0.3))
            ww = rng.uniform(10, 26)
            ln = rng.uniform(-6, 6)
            d.polygon([(x - ww, h), (x + ln, h - max(6.0, hh)), (x + ww * 0.8, h)], fill=255)
    # a low ridge all along
    xs = np.arange(w)
    ridge = h - 8 - 5 * np.sin(xs * 0.05) - 3 * np.sin(xs * 0.17)
    for x in range(w):
        d.line([(x, int(ridge[x])), (x, h)], fill=255)
    mask = np.array(im) > 0
    return _shade_mask(mask, body, rim, deep)


LAYERS = {
    # name: (function, size in sheet pixels)
    "ciel_nuit": lambda: sky(560, 300, ["#0a0414", "#140822", "#22092e", "#3a0c34", "#5a1236", "#7c1c32", "#9a2a2a"], 11),
    "ciel_rouge": lambda: sky(560, 300, ["#12040e", "#2a0818", "#4a0c1e", "#701822", "#982a22", "#c04424", "#e06a2c"], 12),
    "etoiles": lambda: stars(560, 210, 420, 13),
    "brume": lambda: mist(640, 48, 14),
    "pics_loin": lambda: spires(720, 130, 21, 30, 40, 120, 116, "#2a1434", "#6a2a4a", "#1c0c24", lean=0.1),
    "pics": lambda: spires(800, 160, 22, 18, 50, 150, 146, "#1a0c22", "#8a2a32", "#10060e", lean=0.14,
                           wfac=(0.14, 0.26), magma=["#ffd060", "#ff8a2a", "#c83c18", "#6a1a10"]),
    "sol": lambda: ground(760, 110, 31, 12, "#24101a", "#7a3030", "#120610", rough=3.0, rocks=10),
    "avant": lambda: shards(900, 120, 41, "#0c0610", "#3a1420", "#060306"),
}


def build(out_dir, names=None):
    os.makedirs(out_dir, exist_ok=True)
    files = []
    for name, fn in LAYERS.items():
        if names and name not in names:
            continue
        img = fn()
        p = os.path.join(out_dir, name + ".png")
        Image.fromarray(img, "RGBA").save(p, optimize=True)
        files.append(p)
    return files
