"""The scenery of Pyrrhax's story (2.60): the Confins at night, drawn as animated sprites in layers
(pixel art like the game's backgrounds: hard pixels, ordered dithering, rim light), so that nothing
behind the hatchlings is a still picture: the sky's nebula shimmers, the stars twinkle, clouds of
smoke and mist morph while they drift, magma breathes at the feet of the obsidian spires, embers
crackle in the cracks of the ground, sparks glint on the dark rocks in front.  One sheet pixel is
two units of the story's 960x540 stage.  Every sheet loops (its frames wrap without a jump).

  ciel_nuit   the night sky, violet to crimson, its nebula drifting (6 frames)
  ciel_rouge  the sky set alight by the dying star (6 frames)
  etoiles     the stars, twinkling (transparent: it drifts over the sky)
  nuages      a band of dark smoke clouds lit from below (drifts)
  brume       a band of mist (drifts)
  pics_loin   far obsidian spires, lava glinting at their feet
  pics        nearer spires, magma veins and pools breathing
  sol         the ground: embers crackling in its cracks, ash
  avant       dark rocks and shards in front of everything, sparks on their edges
  flammeches  a few small flames dancing on the ground
  lave        a crack of lava, bubbling

Written as sheets (assets/stories/pyrrhax/<name>.png, a clip « idle ») with their animation
files (assets/animations/stories/pyrrhax_<name>.json).  Deterministic (fixed seeds)."""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

TAU = 2 * math.pi
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32) / 16.0 + 1 / 32.0


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)], np.float32)


def bayer(h, w):
    return np.tile(BAYER, (h // 4 + 1, w // 4 + 1))[:h, :w]


def dither_ramp(t, cols):
    """t (H, W) in 0..1 -> colours of `cols` (dark -> light) with ordered dithering between steps."""
    h, w = t.shape
    n = len(cols) - 1
    x = np.clip(t, 0, 1) * n
    i0 = np.floor(x).astype(int)
    f = x - i0
    idx = np.clip(i0 + (f > bayer(h, w)), 0, n)
    pal = np.array([rgb(c) for c in cols], np.uint8)
    return pal[idx]


def rgba_of(rgbimg, alpha):
    out = np.zeros(rgbimg.shape[:2] + (4,), np.uint8)
    out[..., :3] = rgbimg
    out[..., 3] = alpha
    return out


# ------------------------------------------------------------------- sky, stars, clouds, mist
def sky(w, h, cols, seed, k, n):
    """The sky; its nebula bands drift a little from frame to frame (a loop of n frames)."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    t = 1.0 - (1.0 - yy / (h - 1)) ** 1.25
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    neb = np.zeros((h, w), np.float32)
    for j in range(5):
        fx, fy, ph = rng.uniform(0.006, 0.02), rng.uniform(0.01, 0.03), rng.uniform(0, TAU)
        sp = rng.uniform(0.6, 1.4) * (1 if j % 2 else -1)
        neb += np.sin(xx * fx + np.sin(yy * fy + ph + 0.35 * math.sin(a + j)) * 2.0 + ph + 0.5 * sp * math.sin(a)) / 5.0
    t = np.clip(t + 0.075 * neb * (1 - abs(t - 0.5) * 1.4), 0, 1)
    return rgba_of(dither_ramp(t, cols), 255)


def stars(w, h, count, seed, k, n):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    cols = [rgb("#ffffff"), rgb("#ffe8f0"), rgb("#ffc8d8"), rgb("#c8a0e0"), rgb("#8a6aa8")]
    for i in range(count):
        x, y = int(rng.integers(0, w)), int(rng.integers(0, h))
        y = int(y * (0.25 + 0.75 * rng.random()))
        b = rng.random()
        ph = rng.uniform(0, TAU)
        tw = 0.5 + 0.5 * math.sin(TAU * k / n * (1 + i % 2) + ph)  # each star its own twinkle
        lvl = min(4, int((1 - b) * 4.5 + (1 - tw) * 1.8))
        if lvl >= 5 or (b < 0.3 and tw < 0.35):
            continue
        out[y, x, :3] = cols[lvl]
        out[y, x, 3] = 255
        if b > 0.965 and tw > 0.5:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if 0 <= x + dx < w and 0 <= y + dy < h:
                    out[y + dy, x + dx, :3] = cols[3]
                    out[y + dy, x + dx, 3] = 255
    return out


def clouds(w, h, seed, k, n, body="#3a1a3a", lit="#a0383a", alpha=170):
    """A band of dark smoke clouds lit from below, its billows breathing (a loop of n frames)."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    v = np.zeros((h, w), np.float32)
    for j in range(6):
        cyc = int(rng.integers(2, 9))  # whole waves across the width: it repeats without a seam
        ph = rng.uniform(0, TAU)
        v += np.sin(xx * TAU * cyc / w + ph + 0.6 * math.sin(a + j * 1.3) + np.sin(yy * 0.12 + ph + a) * 0.9) / 6.0
    band = np.exp(-((yy - h * 0.55) / (h * 0.3)) ** 2)
    d = (0.5 + 0.5 * v) * band
    on = d > 0.28 + 0.12 * bayer(h, w)
    out = np.zeros((h, w, 4), np.uint8)
    under = np.clip((yy - h * 0.35) / (h * 0.5), 0, 1)
    col = dither_ramp(np.clip(under * 0.9 + (d - 0.3), 0, 1), [body, body, lit])
    out[on, :3] = col[on]
    out[on, 3] = alpha
    return out


def mist(w, h, seed, k, n, col="#c8a0c0", amount=0.5):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    v = np.zeros((h, w), np.float32)
    for j in range(4):
        cyc = int(rng.integers(3, 12))  # whole waves across the width: it repeats without a seam
        ph = rng.uniform(0, TAU)
        v += np.sin(xx * TAU * cyc / w + ph + 0.7 * math.sin(a + j) + np.sin(yy * 0.08 + ph + a) * 1.5) / 4.0
    band = np.exp(-((yy - h * 0.5) / (h * 0.32)) ** 2)
    al = np.clip((0.5 + 0.5 * v) * band * amount, 0, 1)
    on = al > bayer(h, w)
    out = np.zeros((h, w, 4), np.uint8)
    out[on, :3] = rgb(col)
    out[on, 3] = 110
    return out


# ------------------------------------------------------------------- rocks and ground
def _shade_mask(mask, body, rim, deep, light_dx=-1):
    """Colours a silhouette: its body, a rim light on the lit side and its top, darker at its foot."""
    h, w = mask.shape
    out = np.zeros((h, w, 4), np.uint8)
    yy = np.mgrid[0:h, 0:w][0].astype(np.float32)
    if mask.any():
        t = np.clip((yy - yy[mask].min()) / max(1.0, yy[mask].max() - yy[mask].min()), 0, 1)
    else:
        t = yy
    col = dither_ramp(1.0 - t * 0.9, [deep, body, body])
    out[mask, :3] = col[mask]
    out[mask, 3] = 255
    edge = mask & (~np.roll(mask, -light_dx, axis=1) | ~np.roll(mask, 1, axis=0))
    out[edge, :3] = rgb(rim)
    return out


def _spire_mask(w, h, seed, n, hmin, hmax, base, lean, wfac):
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for _ in range(n):
        x = rng.uniform(-20, w + 20)
        hh = rng.uniform(hmin, hmax)
        ww = hh * rng.uniform(*wfac)
        ln = rng.uniform(-lean, lean) * hh
        d.polygon([(x - ww / 2, base + 4), (x + ln, base - hh), (x + ww / 2, base + 4)], fill=255)
        if rng.random() < 0.6:
            h2 = hh * rng.uniform(0.35, 0.7)
            d.polygon([(x + ww * 0.15, base + 4), (x + ww * 0.45 + ln * 0.6, base - h2), (x + ww * 0.85, base + 4)], fill=255)
    d.rectangle([0, base, w, h], fill=255)
    return np.array(im) > 0


MAGMA = ["#fff2b0", "#ffd060", "#ff8a2a", "#c83c18", "#6a1a10"]


def spires(w, h, seed, k, n, count, hmin, hmax, base, body, rim, deep, lean=0.12, wfac=(0.12, 0.22), veins=0, pools=0,
           glints=0):
    """Obsidian spires: their magma veins and pools breathe, glints run along their rims."""
    mask = _spire_mask(w, h, seed, count, hmin, hmax, base, lean, wfac)
    out = _shade_mask(mask, body, rim, deep)
    a = TAU * k / n
    rng = np.random.default_rng(seed + 9)
    for i in range(veins):  # veins climbing from the feet, their light going up and down
        x = int(rng.uniform(0, w))
        y = int(base + rng.uniform(-2, 6))
        ln = int(rng.uniform(4, 12))
        ph = rng.uniform(0, TAU)
        lit = 0.5 + 0.5 * math.sin(a + ph)
        top = int(ln * (0.45 + 0.55 * lit))
        drift = rng.uniform(-0.6, 0.6)
        for j in range(ln):
            yy, xx = y - j, x + int(j * drift)
            if 0 <= yy < h and 0 <= xx < w and mask[yy, xx]:
                if j > top:
                    out[yy, xx, :3] = rgb(MAGMA[4])
                else:
                    out[yy, xx, :3] = rgb(MAGMA[min(4, 1 + j * 3 // max(1, top) + (0 if lit > 0.6 else 1))])
    for i in range(pools):  # small pools of lava at the feet, their surface rolling
        x = int(rng.uniform(10, w - 10))
        y = int(base + rng.uniform(1, 6))
        r = int(rng.uniform(3, 7))
        ph = rng.uniform(0, TAU)
        for dx in range(-r, r + 1):
            for dy in range(-1, 2):
                xx, yy = x + dx, y + dy
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx]:
                    wave = 0.5 + 0.5 * math.sin(dx * 0.9 + a * 2 + ph)
                    out[yy, xx, :3] = rgb(MAGMA[1 + int((1 - wave) * 2.5) + (1 if abs(dx) == r else 0)])
    if glints:  # a glint of light running along the lit rims
        edge = mask & (~np.roll(mask, 1, axis=1) | ~np.roll(mask, 1, axis=0))
        ys, xs = np.nonzero(edge)
        sel = ((xs * 0.37 + ys * 0.11 + k * w / (n * 3.0)) % (w / 3.0)) < 2.2
        out[ys[sel], xs[sel], :3] = rgb(glints)
    return out


def ground(w, h, seed, k, n, top, body, rim, deep, rough=4.0, rocks=0, embers=0):
    """The ground: its edge, a few rocks, ash specks, embers crackling in its cracks."""
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
    rng = np.random.default_rng(seed + 5)
    for i in range(embers):  # cracks with embers: each one flares and dims on its own beat
        x = int(rng.uniform(0, w))
        y = int(edge[x] + rng.uniform(4, h - edge[x] - 4)) if h - edge[x] > 8 else int(edge[x] + 3)
        ln = int(rng.uniform(3, 9))
        ph = rng.uniform(0, TAU)
        beat = 0.5 + 0.5 * math.sin(a * (1 + i % 3) + ph)
        dx = 1 if rng.random() < 0.5 else -1
        for j in range(ln):
            xx, yy2 = x + j * dx, y + (j // 3)
            if 0 <= yy2 < h and 0 <= xx < w and mask[yy2, xx]:
                lvl = 4 - int(beat * 3.2 * (1 - abs(j - ln / 2) / ln))
                out[yy2, xx, :3] = rgb(MAGMA[max(1, min(4, lvl))])
    return out


def shards(w, h, seed, k, n, body, rim, deep):
    """Dark foreground: rocks and obsidian shards rising from the bottom corners, sparks on their edges."""
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for side in (0, 1):
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
    lit = np.sin(TAU * k / n + ph) > 0.93
    out[ys[lit], xs2[lit], :3] = rgb("#ff9a4a")
    return out


# ------------------------------------------------------------------- small animated props
def flamelets(k, n, w=48, h=32):
    """Three small flames dancing on the ground (bottom centre)."""
    out = np.zeros((h, w, 4), np.uint8)
    cols = [rgb(c) for c in ("#fff6c8", "#ffd060", "#ff8a2a", "#d2481a")]
    a = TAU * k / n
    for j, (cx, hh) in enumerate(((14, 14), (24, 20), (34, 12))):
        sway = 2.0 * math.sin(a + j * 2.1)
        ht = hh * (0.8 + 0.25 * math.sin(a * 2 + j))
        for y in range(int(ht)):
            t = y / max(1.0, ht)
            half = (1 - t) * (4.5 - j % 2) * (0.8 + 0.2 * math.sin(a * 3 + y * 0.7 + j))
            x0 = cx + sway * t * t
            for x in range(int(x0 - half), int(x0 + half) + 1):
                if 0 <= x < w:
                    d = abs(x + 0.5 - x0) / max(0.5, half)
                    ci = min(3, int(d * 2.0 + t * 2.2))
                    out[h - 1 - y, x, :3] = cols[ci]
                    out[h - 1 - y, x, 3] = 255
    return out


def lava(k, n, w=64, h=24):
    """A crack of lava in the ground, bubbling, its crust rolling."""
    out = np.zeros((h, w, 4), np.uint8)
    a = TAU * k / n
    crust = rgb("#2a0e14")
    for x in range(w):
        half = 5.5 * math.sin(math.pi * x / (w - 1)) ** 0.7
        cy = h * 0.6 + 1.5 * math.sin(x * 0.2)
        for y in range(int(cy - half - 1), int(cy + half + 2)):
            if 0 <= y < h:
                dd = abs(y + 0.5 - cy) / max(0.6, half)
                if dd > 1.15:
                    continue
                if dd > 0.85:
                    out[y, x, :3] = crust
                else:
                    wave = 0.5 + 0.5 * math.sin(x * 0.45 - a * 2 + y * 0.6)
                    out[y, x, :3] = rgb(MAGMA[min(4, int(dd * 2 + (1 - wave) * 2.2))])
                out[y, x, 3] = 255
    for j in range(3):  # bubbles rising and bursting
        bx = int(10 + j * 20 + 4 * math.sin(j * 1.7))
        ph = (k / n + j / 3.0) % 1.0
        by = int(h * 0.6 - ph * 8)
        if ph < 0.7 and 0 <= by < h:
            out[by, bx, :3] = rgb(MAGMA[0 if ph < 0.4 else 1])
            out[by, bx, 3] = 255
        elif 0 <= by < h:
            for dx in (-1, 1):
                out[by, bx + dx, :3] = rgb(MAGMA[2])
                out[by, bx + dx, 3] = 255
    return out


# ------------------------------------------------------------------- sheets
NIGHT = ["#0a0414", "#140822", "#22092e", "#3a0c34", "#5a1236", "#7c1c32", "#9a2a2a"]
FIRE = ["#12040e", "#2a0818", "#4a0c1e", "#701822", "#982a22", "#c04424", "#e06a2c"]

LAYERS = {
    # name: (frame w, frame h, frames, fps, frame function (k, n) -> RGBA)
    "ciel_nuit": (560, 300, 6, 3, lambda k, n: sky(560, 300, NIGHT, 11, k, n)),
    "ciel_rouge": (560, 300, 6, 3, lambda k, n: sky(560, 300, FIRE, 12, k, n)),
    "etoiles": (560, 210, 6, 4, lambda k, n: stars(560, 210, 420, 13, k, n)),
    "nuages": (640, 80, 4, 3, lambda k, n: clouds(640, 80, 15, k, n)),
    "brume": (640, 48, 4, 3, lambda k, n: mist(640, 48, 14, k, n)),
    "pics_loin": (720, 130, 4, 4, lambda k, n: spires(720, 130, 21, k, n, 30, 40, 120, 116, "#2a1434", "#6a2a4a", "#1c0c24",
                                                       lean=0.1, veins=14, glints="#9a4a6a")),
    "pics": (800, 160, 4, 5, lambda k, n: spires(800, 160, 22, k, n, 18, 50, 150, 146, "#1a0c22", "#8a2a32", "#10060e",
                                                  lean=0.14, wfac=(0.14, 0.26), veins=44, pools=9, glints="#c04a3a")),
    "sol": (760, 110, 4, 5, lambda k, n: ground(760, 110, 31, k, n, 12, "#24101a", "#7a3030", "#120610", rough=3.0, rocks=10,
                                                embers=46)),
    "avant": (900, 120, 4, 4, lambda k, n: shards(900, 120, 41, k, n, "#0c0610", "#3a1420", "#060306")),
    "flammeches": (48, 32, 6, 10, flamelets),
    "lave": (64, 24, 6, 6, lava),
}


def sheets(names=None):
    here = os.path.dirname(os.path.abspath(__file__))
    chars = os.path.join(os.path.dirname(here), "characters")
    if chars not in sys.path:
        sys.path[:0] = [chars, os.path.dirname(here)]
    from sheet import Sheet
    out = {}
    for name, (w, h, n, fps, fn) in LAYERS.items():
        if names and name not in names:
            continue
        sh = Sheet(f"stories/pyrrhax/{name}.png", w, h, [0, 0] if w > 100 else [w // 2, h])
        sh.add("idle", [fn(k, n) for k in range(n)], fps=fps, loop=True)
        out[name] = sh
    return out


def build(names=None):
    """Writes every sheet of scenery and its animation file; returns {name: Sheet}."""
    built = sheets(names)
    for name, sh in built.items():
        sh.save(f"animations/stories/pyrrhax_{name}.json")
    return built
