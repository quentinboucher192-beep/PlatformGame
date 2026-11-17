"""Scenery for the stories told in animated scenes (2.63): the generators of their layers, shared by
the giga bosses' stories (Pyrrhax's own are in story_pyrrhax_layers.py, reused here).

Each generator draws one frame (k of a loop of n) as RGBA, pixel art like the game's backgrounds
(hard pixels, ordered dithering, rim light): skies whose nebula shimmers, stars that twinkle, bands
of cloud, dust or mist that morph while they drift, peaks and walls of rock whose crystals breathe,
grounds that glint, dark rocks in front with sparks on their edges, the deep sea with its light
rays and its silt.  Every loop wraps without a jump; deterministic (fixed seeds)."""
import math

import numpy as np
from PIL import Image, ImageDraw

from story_pyrrhax_layers import _shade_mask, _spire_mask, bayer, clouds, dither_ramp, mist, rgb, rgba_of, sky  # noqa: F401

TAU = 2 * math.pi


# ------------------------------------------------------------------- skies
def starfield(w, h, count, seed, k, n, cols=("#ffffff", "#f0f0ff", "#c8d8ff", "#8a9ac8", "#4a5a88"), fade=True):
    """Stars twinkling, each on its own beat (transparent: drifts over a sky)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    cs = [rgb(c) for c in cols]
    for i in range(count):
        x, y = int(rng.integers(0, w)), int(rng.integers(0, h))
        if fade:
            y = int(y * (0.25 + 0.75 * rng.random()))
        b = rng.random()
        ph = rng.uniform(0, TAU)
        tw = 0.5 + 0.5 * math.sin(TAU * k / n * (1 + i % 2) + ph)
        lvl = min(4, int((1 - b) * 4.5 + (1 - tw) * 1.8))
        if b < 0.3 and tw < 0.35:
            continue
        out[y, x, :3] = cs[lvl]
        out[y, x, 3] = 255
        if b > 0.965 and tw > 0.5:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= x + dx < w and 0 <= y + dy < h:
                    out[y + dy, x + dx, :3] = cs[3]
                    out[y + dy, x + dx, 3] = 255
    return out


def space(w, h, cols, seed, k, n, swirl=1.0):
    """Deep space: a nebula of dust in soft bands (no horizon), slowly turning."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    v = np.zeros((h, w), np.float32)
    for j in range(6):
        fx, fy, ph = rng.uniform(0.008, 0.024), rng.uniform(0.008, 0.03), rng.uniform(0, TAU)
        v += np.sin(xx * fx + np.sin(yy * fy + ph + 0.4 * swirl * math.sin(a + j)) * 2.2 + yy * fx * 0.6 + ph) / 6.0
    band = np.exp(-((yy - h * 0.5 + (xx - w * 0.5) * 0.25) / (h * 0.35)) ** 2)
    t = np.clip(0.18 + 0.55 * band * (0.5 + 0.5 * v) + 0.12 * v, 0, 1)
    return rgba_of(dither_ramp(t, cols), 255)


def dust_band(w, h, seed, k, n, body, lit, alpha=150, thick=0.3):
    """A band of dust clouds (a variant of the smoke clouds), lit on one side."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    v = np.zeros((h, w), np.float32)
    for j in range(6):
        cyc = int(rng.integers(2, 8))
        ph = rng.uniform(0, TAU)
        v += np.sin(xx * TAU * cyc / w + ph + 0.6 * math.sin(a + j * 1.3) + np.sin(yy * 0.1 + ph + a) * 0.9) / 6.0
    band = np.exp(-((yy - h * 0.5) / (h * thick)) ** 2)
    d = (0.5 + 0.5 * v) * band
    on = d > 0.3 + 0.12 * bayer(h, w)
    out = np.zeros((h, w, 4), np.uint8)
    top = np.clip((h * 0.65 - yy) / (h * 0.5), 0, 1)
    col = dither_ramp(np.clip(top * 0.8 + (d - 0.3), 0, 1), [body, body, lit])
    out[on, :3] = col[on]
    out[on, 3] = alpha
    return out


# ------------------------------------------------------------------- rock: peaks, walls, ground, front
def peaks(w, h, seed, k, n, count, hmin, hmax, base, body, rim, deep, lean=0.12, wfac=(0.12, 0.22), lights=0,
          light_cols=("#fff2c0", "#ffd27a", "#b07a3a"), glints=None, flip=False):
    """Peaks (or, flipped, stalactites): their small lights (crystals, windows) breathing, glints
    running along their rims."""
    mask = _spire_mask(w, h, seed, count, hmin, hmax, base, lean, wfac)
    if flip:
        mask = mask[::-1]
    out = _shade_mask(mask, body, rim, deep)
    if flip:  # lit from below: darker at the root (the top)
        out2 = _shade_mask(mask[::-1], body, rim, deep)[::-1]
        out = out2.copy()
    a = TAU * k / n
    rng = np.random.default_rng(seed + 9)
    cs = [rgb(c) for c in light_cols]
    ys, xs = np.nonzero(mask)
    for i in range(lights):
        j = int(rng.integers(0, len(xs)))
        x, y = int(xs[j]), int(ys[j])
        ph = rng.uniform(0, TAU)
        lit = 0.5 + 0.5 * math.sin(a * (1 + i % 2) + ph)
        sz = int(rng.integers(1, 3))
        for dy in range(sz):
            for dx in range(sz):
                xx, yy = x + dx, y + dy
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx]:
                    out[yy, xx, :3] = cs[0 if lit > 0.75 else 1 if lit > 0.35 else 2]
    if glints:
        edge = mask & (~np.roll(mask, 1, axis=1) | ~np.roll(mask, 1, axis=0))
        ys2, xs2 = np.nonzero(edge)
        sel = ((xs2 * 0.37 + ys2 * 0.11 + k * w / (n * 3.0)) % (w / 3.0)) < 2.2
        out[ys2[sel], xs2[sel], :3] = rgb(glints)
    return out


def cave_wall(w, h, seed, k, n, cols, crystals=0, crystal_cols=("#fff6d8", "#ffd27a", "#e09a3a", "#7a4a20"), drips=0,
              drip_col="#9ab8c8"):
    """The back wall of a cave filling the frame: strata of rock, darker at the top and the bottom,
    clusters of crystals breathing light, water glinting as it runs down."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    v = np.zeros((h, w), np.float32)
    for j in range(5):
        cyc = int(rng.integers(2, 7))
        ph = rng.uniform(0, TAU)
        v += np.sin(xx * TAU * cyc / w + ph + np.sin(yy * rng.uniform(0.04, 0.09) + ph) * 1.4 + yy * rng.uniform(0.05, 0.12)) / 5.0
    strata = 0.5 + 0.5 * np.sin(yy * 0.35 + 2.0 * np.sin(xx * TAU * 3 / w + 1.3))
    mid = np.exp(-((yy - h * 0.55) / (h * 0.42)) ** 2)
    t = np.clip(0.12 + 0.5 * mid + 0.16 * v + 0.08 * strata, 0, 1)
    out = rgba_of(dither_ramp(t, cols), 255)
    a = TAU * k / n
    cs = [rgb(c) for c in crystal_cols]
    for i in range(crystals):  # a cluster: a few slanted shards, its light coming and going
        cx, cy = rng.uniform(8, w - 8), rng.uniform(h * 0.15, h * 0.85)
        ph = rng.uniform(0, TAU)
        lit = 0.5 + 0.5 * math.sin(a + ph)
        for s in range(int(rng.integers(2, 5))):
            ang = rng.uniform(-1.2, 1.2) - math.pi / 2
            ln = rng.uniform(3, 7)
            for q in np.arange(0, ln, 0.5):
                x, y = int(cx + math.cos(ang) * q), int(cy + math.sin(ang) * q)
                if 0 <= x < w and 0 <= y < h:
                    tip = q / ln
                    lvl = 3 - int(lit * 2.2 + tip * 1.2)
                    out[y, x, :3] = cs[max(0, min(3, lvl))]
                    if x + 1 < w:
                        out[y, x + 1, :3] = cs[min(3, max(0, lvl) + 1)]
    dc = rgb(drip_col)
    for i in range(drips):  # a trickle of water: a glint sliding down it
        x = int(rng.uniform(4, w - 4))
        y0 = int(rng.uniform(0, h * 0.4))
        ln = int(rng.uniform(h * 0.2, h * 0.5))
        ph = rng.uniform(0, 1)
        g = int(((k / n + ph) % 1.0) * ln)
        for j in range(ln):
            yy2 = y0 + j
            if yy2 < h:
                if abs(j - g) <= 1:
                    out[yy2, x, :3] = dc
                elif j % 3 == 0:
                    out[yy2, x, :3] = (out[yy2, x, :3].astype(np.float32) * 0.6 + dc * 0.4).astype(np.uint8)
    return out


def floor(w, h, seed, k, n, top, body, rim, deep, rough=3.0, rocks=8, specks=0,
          speck_cols=("#fff2c0", "#ffd27a", "#9a6a3a"), moss=None, puddles=0, puddle_col="#6a8aa8"):
    """A ground: its edge, rocks, specks that glint on their own beat, moss, puddles that ripple."""
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
    sp = (np.random.default_rng(seed + 1).random((h, w)) < 0.025) & mask & (yy > edge[None, :] + 3)
    out[sp, :3] = rgb(deep)
    a = TAU * k / n
    if moss:
        mc = [rgb(c) for c in moss]
        top_edge = mask & ~np.roll(mask, 1, axis=0)
        ys, xs2 = np.nonzero(top_edge)
        for x, y in zip(xs2, ys):
            if (math.sin(x * 0.21 + 1.7) + math.sin(x * 0.057)) > 0.4:
                out[y, x, :3] = mc[0]
                if y + 1 < h and mask[y + 1, x]:
                    out[y + 1, x, :3] = mc[1]
    rng = np.random.default_rng(seed + 5)
    for i in range(puddles):  # a puddle: its ripples moving
        x = int(rng.uniform(10, w - 30))
        y = int(edge[x] + rng.uniform(5, max(6.0, h - edge[x] - 6)))
        ln = int(rng.uniform(10, 22))
        for j in range(ln):
            if 0 <= y < h and x + j < w and mask[y, x + j]:
                wave = 0.5 + 0.5 * math.sin(j * 0.8 - a * 2)
                c = rgb(puddle_col) * (0.7 + 0.5 * wave)
                out[y, x + j, :3] = np.clip(c, 0, 255).astype(np.uint8)
    cs = [rgb(c) for c in speck_cols]
    for i in range(specks):  # specks glinting each on its own beat
        x = int(rng.uniform(0, w))
        y = int(edge[x] + rng.uniform(2, max(3.0, h - edge[x] - 2)))
        ph = rng.uniform(0, TAU)
        beat = 0.5 + 0.5 * math.sin(a * (1 + i % 3) + ph)
        if 0 <= y < h and mask[y, x]:
            out[y, x, :3] = cs[0 if beat > 0.8 else 1 if beat > 0.45 else 2]
    return out


def front_rocks(w, h, seed, k, n, body, rim, deep, spark="#ffd27a", rate=0.93, sides=(0, 1)):
    """Dark foreground: rocks rising from the bottom corners, sparks on their edges."""
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for side in sides:
        x0 = 0 if side == 0 else w
        for _ in range(7):
            x = x0 + (1 if side == 0 else -1) * rng.uniform(0, w * 0.22)
            hh = rng.uniform(h * 0.25, h * 0.95) * (1.0 - abs(x - x0) / (w * 0.3))
            ww = rng.uniform(10, 26)
            ln = rng.uniform(-6, 6)
            d.polygon([(x - ww, h), (x + ln, h - max(6.0, hh)), (x + ww * 0.8, h)], fill=255)
    xs = np.arange(w)
    ridge = h - 8 - 5 * np.sin(xs * 0.05) - 3 * np.sin(xs * 0.17)
    for x in range(w):
        d.line([(x, int(ridge[x])), (x, h)], fill=255)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    edge = mask & (~np.roll(mask, 1, axis=1) | ~np.roll(mask, 1, axis=0))
    ys, xs2 = np.nonzero(edge)
    rng = np.random.default_rng(seed + 3)
    ph = rng.uniform(0, TAU, len(xs2))
    lit = np.sin(TAU * k / n + ph) > rate
    out[ys[lit], xs2[lit], :3] = rgb(spark)
    return out


# ------------------------------------------------------------------- water
def deep_sea(w, h, cols, seed, k, n, rays=5, ray_col="#9adcff"):
    """The deep sea filling the frame: lighter towards the top, slow currents, rays of light
    swaying down from the surface."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    v = np.zeros((h, w), np.float32)
    for j in range(4):
        cyc = int(rng.integers(2, 6))
        ph = rng.uniform(0, TAU)
        v += np.sin(xx * TAU * cyc / w + ph + 0.5 * math.sin(a + j) + np.sin(yy * 0.05 + ph + a) * 1.2) / 4.0
    t = np.clip(1.0 - (yy / (h - 1)) ** 0.8 + 0.06 * v, 0, 1)
    out = rgba_of(dither_ramp(t, cols), 255)
    rc = rgb(ray_col)
    for i in range(rays):  # rays: slanted bands whose light sways
        x0 = rng.uniform(0, w)
        wd = rng.uniform(6, 16)
        sl = rng.uniform(0.25, 0.45)
        ph = rng.uniform(0, TAU)
        sway = 4.0 * math.sin(a + ph)
        u = (xx - (x0 + sway + yy * sl))
        band = np.clip(1 - np.abs(u) / wd, 0, 1) * np.clip(1 - yy / (h * 0.85), 0, 1) * (0.55 + 0.45 * math.sin(a * 2 + ph))
        on = band * 0.5 > bayer(h, w)
        out[on, :3] = (out[on, :3].astype(np.float32) * 0.55 + rc * 0.45).astype(np.uint8)
    return out


def kelp(w, h, seed, k, n, count, body, rim, deep, hmin=0.4, hmax=0.95):
    """Kelp and sea grass swaying from the bottom."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    a = TAU * k / n
    cs = [rgb(rim), rgb(body), rgb(deep)]
    for i in range(count):
        x0 = rng.uniform(0, w)
        hh = rng.uniform(hmin, hmax) * h
        ph = rng.uniform(0, TAU)
        wd = rng.uniform(1.0, 2.6)
        for y in range(int(hh)):
            t = y / hh
            x = x0 + math.sin(a + ph + t * 3.0) * 6.0 * t + math.sin(t * 9 + ph) * 1.2
            for dx in range(int(-wd), int(wd) + 1):
                xx, yy = int(x + dx), h - 1 - y
                if 0 <= xx < w and 0 <= yy < h:
                    out[yy, xx, :3] = cs[0 if dx == int(-wd) else 2 if dx == int(wd) else 1]
                    out[yy, xx, 3] = 255
            if y % 7 == 3 and t > 0.2:  # leaves
                side = 1 if (y // 7) % 2 else -1
                for q in range(3):
                    xx, yy = int(x + side * (wd + q)), h - 1 - y + q // 2
                    if 0 <= xx < w and 0 <= yy < h:
                        out[yy, xx, :3] = cs[1]
                        out[yy, xx, 3] = 255
    return out


def silt(w, h, seed, k, n, col="#9ab8d0", count=160):
    """Specks of silt drifting in the water (transparent, a loop: they move one cycle)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    c = rgb(col)
    for i in range(count):
        x0, y0 = rng.uniform(0, w), rng.uniform(0, h)
        ph = rng.uniform(0, TAU)
        x = int((x0 + 3.0 * math.sin(TAU * k / n + ph)) % w)
        y = int((y0 + 2.0 * math.cos(TAU * k / n + ph)) % h)
        out[y, x, :3] = c
        out[y, x, 3] = 120 + int(80 * (0.5 + 0.5 * math.sin(ph + TAU * k / n)))
    return out
