#!/usr/bin/env python3
"""Asterion's film (2.68): « ASTERION, LE DÉVOREUR DE MONDES » in ten animated shots, played by the
stories' scene (BossStoryScene, kind « film ») the first time the player sees the titan, then by
« Revoir la cinématique » on the RAIDS hero card.

The script — each shot's caption, its titles in the picture and its length — is hand-written in
data/raids/asterion_film.json (the user may change it: a length changed there stretches the scene,
nothing to regenerate).  This module writes what the script names as « scenes »:
data/raids/asterion_film_scenes.json, one animated stage per scene (the format of
game/content/BossStories.h, version 2 of the stories: layers, sprites moved by keyframes, glows,
particles, a camera, shakes, flashes, sounds), each with its `length` (the seconds its keys were
written for: the script's length when it was generated).

Nothing made up: the titan is its real sprite in layers (assets/ui/raids/asterion.png, written by
raid_asterion.py), cut here into one sheet per branch, its heart and its aura, each with its looks
(intact, affaibli, détruit, inconnu, contour) and its pivot on its joint, so that the branches
can move, glow and breathe; Nova is her own sheet; our planets their own pictures.  Drawn here:
the void and its nebula, a world being devoured (its lights going out one by one), chunks of rock,
magma and ice, the beacons of the clans and their banners, the hologram's grid, a target reticle,
a ridge.

  1. coeur        the void; a golden light: the cosmic heart beats, alone
  2. forge        rock, magma and ice weld around the heart: the branches assemble
  3. festin       Asterion swallows a world; its lights go out one by one; the seven parts feed
  4. gardiens     the seven parts light up one by one (with the script's titles)
  5. galaxie      our planets aligned; its eyes light up; it turns towards them
  6. nova         Nova rushes at the titan alone; Dreadfang swats her away
  7. clans        beacons light up everywhere; groups of eight and clan banners gather
  8. hologramme   the titan as a hologram: the seven branches lock as targets, the heart in the middle
  9. rugissement  Asterion roars and fills the screen (the title card is the script's)
 10. appel        the clans on a ridge before the titan, far away: « Rassemblez votre clan. »

    python3 tools/assetgen/environment/story_asterion.py [--storyboard FILE.png] [--check]

Deterministic (fixed seeds): `--check` builds everything twice and compares the bytes with each
other and with the files."""
import io
import json
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

import story_stage as S  # noqa: E402
from story_stage import K, amb, glow, particles, sfx  # noqa: E402

REPO, ASSETS = S.REPO, S.ASSETS
SCRIPT = os.path.join(REPO, "data", "raids", "asterion_film.json")
SPRITE_JSON = os.path.join(ASSETS, "ui", "raids", "asterion.json")
SPRITE_PNG = os.path.join(ASSETS, "ui", "raids", "asterion.png")
TEX = "stories/asterion/"
ANIM = "animations/stories/asterion_"
NOVA = "animations/nova.json"
W, H = S.W, S.H
C0 = (80.0, 64.0)  # the middle of the titan's canvas (its pixels)
TAU = 2 * math.pi

# The joint of each layer (canvas pixels): the point it is placed and turned about.
JOINT = {"aura": (80, 64), "jambe_droite": (70, 79), "jambe_gauche": (90, 79), "abdomen": (80, 72), "torse": (80, 50),
         "bras_droit": (57, 32), "bras_gauche": (103, 31), "crane": (80, 38), "coeur": (79, 54)}
LIFT = {"torse", "bras_droit", "bras_gauche", "crane", "coeur"}  # the upper body rises as it breathes
EYES = ((74.5, 19.5), (85.5, 19.5))
MAW = (80.0, 30.0)
BRANCHES = ["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"]  # the script's order
GLOW = {"crane": "#ff6a3d", "bras_gauche": "#c8d2ff", "bras_droit": "#ff8a2a", "torse": "#ff5a8a", "abdomen": "#9dff3a",
        "jambe_gauche": "#39f0d8", "jambe_droite": "#ffd27a"}
GOLD, EMBER, CYAN, VIOLET = "#ffd27a", "#ff8a3a", "#39f0d8", "#b46cff"
CLANS = ["#ff5a3a", "#39f0d8", "#ffd27a", "#b46cff"]


def sheet_cls():
    S.chars_path()
    from sheet import Sheet, dumps
    return Sheet, dumps


def manifest():
    with open(SPRITE_JSON, encoding="utf-8") as f:
        return json.load(f)


ORDER = [layer["id"] for layer in manifest()["layers"]]


def col(h, a=255):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)] + [a], np.float32)


# =================================================================== keys
EASES = S.EASE


class Track:
    """An element's channels (pos, scale, alpha, rot), each keyed on its own with its own ease, and
    its clips; written as the format's keys (each key carries every channel that moves, so that
    the channels never disturb one another: a segment shared by two channels is sampled)."""

    def __init__(self, pos=(0.0, 0.0), scale=1.0, alpha=1.0, rot=0.0, flip=None):
        self.ch = {"pos": [(0.0, (float(pos[0]), float(pos[1])), "linear")], "scale": [(0.0, float(scale), "linear")],
                   "alpha": [(0.0, float(alpha), "linear")], "rot": [(0.0, float(rot), "linear")]}
        self.clips = []
        self.flip = flip

    def to(self, t, ease="inout", **kw):
        for k, v in kw.items():
            v = (float(v[0]), float(v[1])) if k == "pos" else float(v)
            self.ch[k].append((round(float(t), 3), v, ease))
        return self

    def clip(self, t, name, offset=0.0):
        self.clips.append((round(float(t), 3), name, offset))
        return self

    def value(self, name, t):
        ks = sorted(self.ch[name], key=lambda k: k[0])
        if t <= ks[0][0]:
            return ks[0][1]
        for i in range(1, len(ks)):
            if t < ks[i][0]:
                a, b = ks[i - 1], ks[i]
                e = EASES[b[2]]((t - a[0]) / max(1e-6, b[0] - a[0]))
                if name == "pos":
                    return (a[1][0] + (b[1][0] - a[1][0]) * e, a[1][1] + (b[1][1] - a[1][1]) * e)
                return a[1] + (b[1] - a[1]) * e
        return ks[-1][1]

    def _own(self, name, a, b):
        """The ease of the channel's own segment (a, b) when it has keys exactly there."""
        ks = sorted(self.ch[name], key=lambda k: k[0])
        for i in range(1, len(ks)):
            if ks[i - 1][0] == a and ks[i][0] == b:
                return ks[i][2]
        return None

    def keys(self):
        times = sorted({k[0] for ks in self.ch.values() for k in ks} | {c[0] for c in self.clips} | {0.0})
        moving = {n for n, ks in self.ch.items() if len({k[1] for k in ks}) > 1}
        out, eases = [times[0]], {}
        for a, b in zip(times, times[1:]):
            changing = [n for n in moving if self.value(n, a) != self.value(n, b) or self.value(n, (a + b) / 2) != self.value(n, a)]
            own = [self._own(n, a, b) for n in changing]
            if all(e is not None for e in own) and len(set(own)) <= 1:
                out.append(b)
                eases[b] = own[0] if own else "linear"
            else:
                n = max(2, int(math.ceil((b - a) / 0.3)))
                for j in range(1, n + 1):
                    tt = round(a + (b - a) * j / n, 3)
                    out.append(tt)
                    eases[tt] = "linear"
        clips = {c[0]: c for c in self.clips}
        keys = []
        for i, t in enumerate(out):
            kw = {}
            for n in ("pos", "scale", "alpha", "rot"):
                if i == 0 or n in moving:
                    v = self.value(n, t)
                    if i == 0 and n not in moving and ((n == "scale" and v == 1.0) or (n == "alpha" and v == 1.0) or (n == "rot" and v == 0.0)):
                        continue
                    kw[n] = v
            if i == 0 and self.flip is not None:
                kw["flip"] = self.flip
            if t in clips:
                kw["clip"] = clips[t][1]
                if clips[t][2]:
                    kw["offset"] = float(clips[t][2])
            if i > 0 and eases.get(t, "inout") != "inout":
                kw["ease"] = eases[t]
            keys.append(K(t, **kw))
        if self.clips and "clip" not in keys[0]:
            keys[0]["clip"] = sorted(self.clips)[0][1]
        return keys


def sprite(id_, anim, track, depth=1.0, tint=None, add=False, pulse=None, drift=None, shadow=None):
    e = {"id": id_, "sprite": anim if anim.endswith(".json") else ANIM + anim + ".json", "depth": depth}
    if add:
        e["blend"] = "add"
    if tint:
        e["tint"] = tint
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    if shadow:
        e["shadow"] = shadow
    e["keys"] = track.keys()
    return e


def layer(id_, name, depth, pos, scale=1.0, alpha=None, keys=None, drift=None, repeat=False, tint=None, add=False):
    e = {"id": id_, "layer": name if name.endswith(".png") else TEX + name + ".png", "depth": depth}
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    if repeat:
        e["repeat"] = True
    if tint:
        e["tint"] = tint
    if add:
        e["blend"] = "add"
    e["keys"] = keys or [K(0, pos=pos, scale=scale if scale != 1.0 else None, alpha=alpha)]
    return e


def glo(id_, color, radius, track, depth=1.0, pulse=None):
    return glow(id_, color, radius, track.keys(), depth=depth, pulse=pulse)


# =================================================================== the titan
def canvas_at(p, O, s):
    """A point of the titan's canvas (its pixels) on the stage, the titan's middle at O, at scale s."""
    return (O[0] + (p[0] - C0[0]) * S.PIX * s, O[1] + (p[1] - C0[1]) * S.PIX * s)


def joint_at(pid, O, s):
    return canvas_at(JOINT[pid], O, s)


class Titan:
    """Where the titan stands through a shot: (t, middle, scale, ease) keys; its parts follow."""

    def __init__(self, O, s):
        self.place = [(0.0, (float(O[0]), float(O[1])), float(s), "linear")]

    def move(self, t, O, s, ease="inout"):
        self.place.append((float(t), (float(O[0]), float(O[1])), float(s), ease))
        return self

    def at(self, t):
        ks = self.place
        if t <= ks[0][0]:
            return ks[0][1], ks[0][2]
        for i in range(1, len(ks)):
            if t < ks[i][0]:
                a, b = ks[i - 1], ks[i]
                e = EASES[b[3]]((t - a[0]) / max(1e-6, b[0] - a[0]))
                return (a[1][0] + (b[1][0] - a[1][0]) * e, a[1][1] + (b[1][1] - a[1][1]) * e), a[2] + (b[2] - a[2]) * e
        return ks[-1][1], ks[-1][2]

    def point(self, p, t=0.0):
        O, s = self.at(t)
        return canvas_at(p, O, s)

    def track(self, pid, D, look="intact", breathe=True, alpha=1.0, offset=(0.0, 0.0)):
        """A part's track: its joint where the titan stands (the upper body breathing when it
        stands still), `offset` stage units away."""
        O, s = self.place[0][1], self.place[0][2]
        p = joint_at(pid, O, s)
        tr = Track(pos=(p[0] + offset[0], p[1] + offset[1]), scale=s, alpha=alpha)
        for t, O, s, ease in self.place[1:]:
            p = joint_at(pid, O, s)
            tr.to(t, ease, pos=(p[0] + offset[0], p[1] + offset[1]), scale=s)
        if breathe and len(self.place) == 1 and pid in LIFT:
            up = S.PIX * s
            k, t = 0, 1.3
            while t < D + 1.3:
                tr.to(t, "inout", pos=(p[0] + offset[0], p[1] + offset[1] - (up if k % 2 == 0 else 0.0)))
                k += 1
                t += 1.3
        tr.clip(0, look)
        return tr

    def parts(self, D, prefix="", look="intact", only=None, tint=None, add=False, alpha=1.0, depth=1.0, aura=True, breathe=True):
        out = []
        for pid in ORDER:
            if (only and pid not in only) or (pid == "aura" and not aura):
                continue
            lk = "intact" if pid == "aura" else look
            out.append(sprite(prefix + pid, pid, self.track(pid, D, lk, breathe, alpha), depth=depth, tint=tint, add=add))
        return out


# =================================================================== pictures drawn here
def _rgba(a):
    return np.clip(a + 0.5, 0, 255).astype(np.uint8)


def value_noise(w, h, cell, seed):
    from helpers import fbm
    return fbm(w, h, cell, octaves=4, seed=seed, wrap_x=True, wrap_y=True)


def img_space(w=600, h=340, seed=7):
    """The void: a deep blue-black, a faint nebula, stars of every size."""
    rng = np.random.default_rng(seed)
    yy = np.linspace(0, 1, h)[:, None]
    base = np.zeros((h, w, 4), np.float32)
    base[..., :3] = col("#05040c")[:3] * (1 - yy[..., None] * 0.4) + col("#0c0a1e")[:3] * (yy[..., None] * 0.4)
    n = value_noise(w, h, 90, seed)
    m = value_noise(w, h, 40, seed + 5)
    neb = np.clip((n - 0.45) * 2.4, 0, 1) ** 1.6 * (0.6 + 0.4 * m)
    base[..., :3] += neb[..., None] * col("#3a1c5e")[:3] * 0.55 + (np.clip((m - 0.55) * 3, 0, 1) ** 2)[..., None] * col("#103a52")[:3] * 0.4
    for _ in range(int(w * h / 90)):
        x, y = int(rng.integers(0, w)), int(rng.integers(0, h))
        b = float(rng.random()) ** 3
        c = col(["#ffffff", "#cfe0ff", "#ffe6c0", "#e0c8ff"][int(rng.integers(0, 4))])[:3]
        base[y, x, :3] = np.maximum(base[y, x, :3], c * (0.25 + 0.75 * b))
        if b > 0.75:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy2 = (x + dx) % w, min(h - 1, max(0, y + dy))
                base[yy2, xx, :3] = np.maximum(base[yy2, xx, :3], c * 0.45 * b)
    base[..., 3] = 255
    return _rgba(base)


def img_nebula(w=320, h=150, seed=21):
    """Clouds of violet, magenta and teal (drawn over the void at twice its pixels, they drift);
    its alpha in a few steps (pixel art, and a small file)."""
    n = value_noise(w, h, 55, seed)
    m = value_noise(w, h, 25, seed + 3)
    a = np.clip((n - 0.5) * 2.6, 0, 1) ** 1.4 * (0.5 + 0.5 * m)
    out = np.zeros((h, w, 4), np.float32)
    c1, c2, c3 = col("#6a2c9a")[:3], col("#c04aac")[:3], col("#1f6a8a")[:3]
    mix = np.clip((m - 0.3) * 2, 0, 1)[..., None]
    out[..., :3] = c1 * (1 - mix) + c2 * mix
    teal = np.clip((value_noise(w, h, 35, seed + 9) - 0.55) * 3, 0, 1)[..., None]
    out[..., :3] = out[..., :3] * (1 - teal) + c3 * teal
    edge = np.minimum(np.minimum(np.arange(w)[None, :], w - 1 - np.arange(w)[None, :]) / 30.0, 1.0)
    edge = edge * np.minimum(np.minimum(np.arange(h)[:, None], h - 1 - np.arange(h)[:, None]) / 25.0, 1.0)
    out[..., 3] = np.floor(a * edge * 6.0) / 6.0 * 150
    out[..., :3] = np.floor(out[..., :3] / 16.0) * 16.0
    return _rgba(out)


def img_grid(w=480, h=270):
    """The hologram's floor: a grid in perspective, rings around its middle."""
    out = np.zeros((h, w, 4), np.float32)
    c = col(CYAN)
    horizon, cx = int(h * 0.58), w / 2
    for k in range(1, 14):
        y = horizon + int((h - horizon) * (k / 13.0) ** 1.8)
        if y < h:
            out[y, :, :3] = c[:3]
            out[y, :, 3] = 70 + 8 * k
    for k in range(-12, 13):
        x0 = cx + k * 8
        x1 = cx + k * 60
        for y in range(horizon, h):
            u = (y - horizon) / max(1, h - horizon)
            x = int(round(x0 + (x1 - x0) * u))
            if 0 <= x < w:
                out[y, x, :3] = c[:3]
                out[y, x, 3] = max(out[y, x, 3], 40 + 90 * u)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    for r in (40, 70, 100):
        d = np.abs(np.hypot((xx - cx) / 1.0, (yy - horizon - 34) * 3.2) - r * 1.6)
        ring = (d < 0.8) & (yy > horizon)
        out[ring, :3] = c[:3]
        out[ring, 3] = np.maximum(out[ring, 3], 120)
    for y in range(0, horizon, 3):  # scanlines above the floor
        out[y, :, :3] = c[:3]
        out[y, :, 3] = np.maximum(out[y, :, 3], 14)
    return _rgba(out)


def ridge_top(w=640, seed=33):
    """The ridge's crest: the row of its top at each column."""
    from helpers import fbm1d
    xs = np.arange(w)
    top = 40 + 30 * fbm1d(w, 80, 4, seed) + 18 * np.sin(xs / w * TAU * 1.5 + 0.7)
    return top + (np.abs(xs - w / 2) / (w / 2)) ** 2 * -26


def img_ridge(w=640, h=150, seed=33):
    """A dark ridge in the foreground, its crest lit by a violet light."""
    rng = np.random.default_rng(seed)
    top = ridge_top(w, seed)
    out = np.zeros((h, w, 4), np.float32)
    rock = [col(c)[:3] for c in ("#07060e", "#0d0b1a", "#141128", "#1e1838")]
    for x in range(w):
        t0 = int(max(0, top[x]))
        for y in range(t0, h):
            d = y - t0
            shade = rock[3] if d < 2 else rock[2] if d < 5 else rock[1] if d < 14 else rock[0]
            if d >= 5 and d < 14 and ((x + y) % 4 == 0):
                shade = rock[2]
            out[y, x, :3] = shade
            out[y, x, 3] = 255
        if t0 < h:
            out[t0, x, :3] = col("#8a5ac8")[:3]
    for _ in range(18):  # rocks standing up
        x = int(rng.integers(20, w - 20))
        hh = int(rng.integers(6, 16))
        t0 = int(top[x])
        for y in range(max(0, t0 - hh), t0):
            ww = max(1, int((y - (t0 - hh)) * 0.5))
            out[y, max(0, x - ww):x + ww, :3] = rock[1]
            out[y, max(0, x - ww):x + ww, 3] = 255
            out[y, max(0, x - ww), :3] = col("#5a3a8a")[:3]
    return _rgba(out)


def frame_world(k, n, lights, size=96, seed=5):
    """A world being devoured: oceans of violet, ochre continents, its night side sown with the
    lights of its cities; `k` of `n` steps of its lights going out (and its day side dimming)."""
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    r = size / 2.0 - 2
    dx, dy = (xx - c) / r, (yy - c) / r
    d2 = dx * dx + dy * dy
    inside = d2 <= 1.0
    dz = np.sqrt(np.clip(1 - d2, 0, 1))
    land = value_noise(size, size, 24, seed)
    u = (dx * 0.5 + 0.5)
    base = np.where((land > 0.52)[..., None], col("#b8864a")[:3], col("#3a2a7a")[:3])
    base = np.where((land > 0.62)[..., None], col("#7a5a30")[:3], base)
    light = np.clip(-dx * 0.75 - dy * 0.35 + dz * 0.55, 0, 1)
    dim = 1.0 - 0.6 * (k / max(1, n - 1))
    rgb = base * (0.12 + 0.95 * light[..., None] * dim)
    rim = np.clip((d2 - 0.82) * 6, 0, 1) * (1 - u)
    rgb += rim[..., None] * col("#8ab4ff")[:3] * 0.5 * dim
    out = np.zeros((size, size, 4), np.float32)
    out[..., :3] = rgb
    out[..., 3] = np.where(inside, 255, 0)
    gone = int(round(len(lights) * k / max(1, n - 1)))
    for i, (x, y, b) in enumerate(lights):
        if i < gone:
            continue
        out[y, x, :3] = col("#ffe6a0")[:3] * b
        if b > 0.8:
            for ddx, ddy in ((1, 0), (0, 1)):
                if 0 <= x + ddx < size and 0 <= y + ddy < size and inside[y + ddy, x + ddx]:
                    out[y + ddy, x + ddx, :3] = np.maximum(out[y + ddy, x + ddx, :3], col("#ffb050")[:3] * 0.7)
    edge = inside & ~np.roll(inside, 1, 0) | inside & ~np.roll(inside, -1, 0) | inside & ~np.roll(inside, 1, 1) | inside & ~np.roll(inside, -1, 1)
    out[edge, :3] *= 0.5
    return _rgba(out)


def world_lights(size=96, seed=11):
    rng = np.random.default_rng(seed)
    c = (size - 1) / 2.0
    r = size / 2.0 - 2
    land = value_noise(size, size, 24, 5)
    out = []
    while len(out) < 60:
        x, y = int(rng.integers(4, size - 4)), int(rng.integers(4, size - 4))
        dx, dy = (x - c) / r, (y - c) / r
        if dx * dx + dy * dy > 0.86 or dx < -0.15 or land[y, x] <= 0.5:
            continue
        out.append((x, y, 0.6 + 0.4 * float(rng.random())))
    order = rng.permutation(len(out))
    return [out[i] for i in order]


def frame_chunk(kind, size=12, seed=1):
    """A chunk of a broken world: rock, glowing magma, a shard of ice."""
    rng = np.random.default_rng(seed + {"roche": 0, "magma": 7, "glace": 13}[kind])
    out = np.zeros((size, size, 4), np.float32)
    c = (size - 1) / 2.0
    pts = []
    for i in range(7):
        a = TAU * i / 7 + float(rng.random()) * 0.5
        rr = (0.6 + 0.4 * float(rng.random())) * (size / 2 - 1)
        pts.append((c + math.cos(a) * rr, c + math.sin(a) * rr * (1.4 if kind == "glace" else 1.0)))
    im = Image.new("L", (size, size), 0)
    ImageDraw.Draw(im).polygon(pts, fill=255)
    m = np.array(im) > 0
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    lit = np.clip(1 - ((xx - c + 2) ** 2 + (yy - c + 2) ** 2) / (size * size * 0.35), 0, 1)
    ramp = {"roche": ["#1a1418", "#3c3036", "#6a5a5a", "#9a8a80"], "magma": ["#4a0f08", "#c8401a", "#ff8a2e", "#ffe6a8"],
            "glace": ["#123a5a", "#2b6a99", "#8fd8f0", "#e6fffb"]}[kind]
    idx = np.clip((lit * 3.99).astype(int), 0, 3)
    for i, h in enumerate(ramp):
        out[(idx == i) & m, :3] = col(h)[:3]
    out[m, 3] = 255
    edge = m & ~(np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1))
    out[edge, :3] = col({"roche": "#0e0a0e", "magma": "#5a1408", "glace": "#0e2a44"}[kind])[:3]
    return _rgba(out)


def frame_beacon(on, k=0):
    """A beacon of the clans: a stone pylon, its bronze cap, its lamp (off, or burning)."""
    w, h = 16, 40
    out = np.zeros((h, w, 4), np.float32)
    stone = [col(c)[:3] for c in ("#141018", "#2a2430", "#463c4a", "#6a5e6a")]
    for y in range(12, h):
        half = 3 + (y - 12) * 3 // 28
        for x in range(8 - half, 8 + half):
            s = stone[1] if x < 8 - half + 2 else stone[3] if x > 8 + half - 3 else stone[2]
            if (y % 7 == 0):
                s = stone[0]
            out[y, x, :3] = s
            out[y, x, 3] = 255
        out[y, 8 - half, :3] = stone[0]
        out[y, 8 + half - 1, :3] = stone[0]
    for y in range(8, 12):
        out[y, 3:13, :3] = col("#7a4a20" if y in (8, 11) else "#e0a850")[:3]
        out[y, 3:13, 3] = 255
    lamp = ["#2a1c10", "#4a3018"] if not on else [["#ffe6a8", "#ffb050"], ["#fffbea", "#ffc060"], ["#ffd27a", "#ff8a2e"]][k % 3]
    for y in range(2, 8):
        hw = 3 if 3 <= y <= 6 else 2
        for x in range(8 - hw, 8 + hw):
            out[y, x, :3] = col(lamp[0] if abs(x - 7.5) < 1.5 and 3 <= y <= 6 else lamp[1])[:3]
            out[y, x, 3] = 255
    return _rgba(out)


def frame_beam(k):
    """A beam of light rising from a beacon (drawn additively)."""
    w, h = 10, 160
    out = np.zeros((h, w, 4), np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    core = np.exp(-((xx - 4.5) / (1.4 + 0.3 * k)) ** 2)
    fall = (yy / h) ** 1.3
    shimmer = 0.85 + 0.15 * np.sin(yy * 0.35 + k * 2.1)
    a = core * fall * shimmer
    out[..., :3] = col("#ffe6b0")[:3]
    out[..., 3] = np.clip(a * 230, 0, 255)
    return _rgba(out)


def frame_banner(k):
    """A clan's banner on its pole, waving (light grey: the game tints it with the clan's colour)."""
    w, h = 24, 52
    out = np.zeros((h, w, 4), np.float32)
    for y in range(0, h):
        out[y, 3:5, :3] = col("#5a4a3a" if y > 2 else "#e0a850")[:3]
        out[y, 3:5, 3] = 255
    out[0:3, 2:6, :3] = col("#ffd98a")[:3]
    out[0:3, 2:6, 3] = 255
    for x in range(5, 23):
        u = (x - 5) / 17.0
        dy = int(round(math.sin(u * 3.2 - k * TAU / 4) * 2.0 * u))
        y0, y1 = 4 + dy, 24 + dy - int(u * 3)
        for y in range(y0, y1):
            shade = 0.78 + 0.22 * math.cos(u * 3.2 - k * TAU / 4)
            c = col("#f2f0fa")[:3] * shade
            if y in (y0, y1 - 1):
                c = col("#9a96aa")[:3] * shade
            # its emblem: a star
            ex, ey = 14, (y0 + y1) / 2
            if abs(x - ex) + abs(y - ey) * 1.3 < 4.2 and not (abs(x - ex) > 1 and abs(y - ey) > 1):
                c = col("#4a4658")[:3] * shade
            out[y, x, :3] = c
            out[y, x, 3] = 255
        if x == 22:
            for y in range(y0, y1):
                if (y - y0) % 4 == 0:
                    out[y, x, 3] = 0
    return _rgba(out)


def frame_reticle(k, n, hold=False):
    """A target locking: four brackets closing on the middle, a ring; white (tinted by the game)."""
    w = 40
    out = np.zeros((w, w, 4), np.float32)
    c = (w - 1) / 2.0
    u = 1.0 if hold else k / max(1, n - 1)
    d = int(round(18 - 9 * u))
    arm = 5
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, y0 = int(round(c + sx * d)), int(round(c + sy * d))
            for i in range(arm):
                for (x, y) in ((x0 - sx * i, y0), (x0, y0 - sy * i)):
                    if 0 <= x < w and 0 <= y < w:
                        out[y, x] = (255, 255, 255, 255)
    yy, xx = np.mgrid[0:w, 0:w].astype(np.float32)
    rr = np.hypot(xx - c, yy - c)
    ring = np.abs(rr - (6 + (2 if hold and k % 2 else 0))) < 0.7
    out[ring] = (255, 255, 255, 200 if u >= 1 else 120)
    out[int(c), int(c)] = (255, 255, 255, 255)
    return _rgba(out)


# =================================================================== sheets and layers
def build_assets():
    """{relative path: bytes} of every sheet and layer of the film."""
    Sheet, dumps = sheet_cls()
    files = {}

    def save_sheet(name, sh):
        sh.texture = TEX + name + ".png"
        files[sh.texture] = png_bytes(sh.image())
        files[ANIM + name + ".json"] = (dumps(sh.meta()) + "\n").encode("utf-8")

    man = manifest()
    sheet = np.array(Image.open(SPRITE_PNG).convert("RGBA"))
    for lay in man["layers"]:
        x, y, w, h = lay["box"]
        jx, jy = JOINT[lay["id"]]
        sh = Sheet(TEX + lay["id"] + ".png", w, h, [jx - x, jy - y])
        for look in ("intact", "affaibli", "detruit", "inconnu", "contour"):
            cells = lay["cells"].get(look)
            if not cells:
                continue
            frames = [sheet[cy:cy + h, cx:cx + w].copy() for cx, cy in cells]
            sh.add(look, frames, fps=lay.get("fps", 6) if len(frames) > 1 else 1, loop=True)
        save_sheet(lay["id"], sh)

    lights = world_lights()
    n = 10
    sh = Sheet("", 96, 96, [48, 48])
    sh.add("lit", [frame_world(0, n, lights), frame_world(0, n, lights[3:] + lights[:3])], fps=2, loop=True)
    sh.add("extinction", [frame_world(k, n, lights) for k in range(n)], durations=[0.5] * n, loop=False, next="eteint")
    sh.add("eteint", [frame_world(n - 1, n, lights)], fps=1, loop=True)
    save_sheet("monde", sh)

    sh = Sheet("", 12, 12, [6, 6])
    for kind in ("roche", "magma", "glace"):
        sh.add(kind, [frame_chunk(kind)], fps=1, loop=True)
    save_sheet("eclat", sh)

    sh = Sheet("", 16, 40, [8, 40])
    sh.add("off", [frame_beacon(False)], fps=1, loop=True)
    sh.add("on", [frame_beacon(True, k) for k in range(3)], fps=8, loop=True)
    save_sheet("balise", sh)

    sh = Sheet("", 10, 160, [5, 160])
    sh.add("idle", [frame_beam(k) for k in range(3)], fps=6, loop=True)
    save_sheet("faisceau", sh)

    sh = Sheet("", 24, 52, [4, 52])
    sh.add("wave", [frame_banner(k) for k in range(4)], fps=7, loop=True)
    save_sheet("banniere", sh)

    sh = Sheet("", 40, 40, [20, 20])
    sh.add("lock", [frame_reticle(k, 6) for k in range(6)], fps=14, loop=False, next="hold")
    sh.add("hold", [frame_reticle(k, 2, hold=True) for k in range(2)], fps=4, loop=True)
    save_sheet("cible", sh)

    files[TEX + "espace.png"] = png_bytes(img_space())
    files[TEX + "nebuleuse.png"] = png_bytes(img_nebula())
    files[TEX + "grille.png"] = png_bytes(img_grid())
    files[TEX + "crete.png"] = png_bytes(img_ridge())
    return files


def png_bytes(a):
    buf = io.BytesIO()
    Image.fromarray(a, "RGBA").save(buf, format="PNG", optimize=True)
    return buf.getvalue()


# =================================================================== pieces of scenery
def space(D, drift=6.0, nebula=0.7, alpha=None):
    els = [layer("espace", "espace", 0.04, (-120, -70), keys=[K(0, pos=(-120, -70), alpha=alpha)] if alpha is not None else None,
                 drift=(-drift * 0.3, 0)),
           layer("nebuleuse", "nebuleuse", 0.12, (-160, -40), scale=2.0, alpha=nebula, drift=(-drift, 0), add=True)]
    return els


def stars(alpha=0.8, area=(0, 0, 960, 540), depth=0.1):
    return particles("etoiles", "stars", "#e8e0ff", area, [K(0, alpha=alpha)], depth=depth)


def heart_glow(tr, base=1.0, depth=1.0, radius=120):
    """The heart's light: a broad gold halo and a white core, beating with it (0.67 s)."""
    return [glo("coeur_halo", GOLD, radius * 2.2, tr, depth=depth, pulse=(0.35, 0.667)),
            glo("coeur_feu", EMBER, radius, tr, depth=depth, pulse=(0.5, 0.667, 0.15))]


def shake(a, b, amount):
    return {"from": round(a, 3), "to": round(b, 3), "amount": float(amount)}


def flash(t, color="#ffffff", alpha=0.6, i=0.08, o=0.6):
    return {"t": round(t, 3), "in": i, "out": o, "color": color, "alpha": alpha}


def camera(*keys):
    return [K(t, pos=p, zoom=z, ease=e if e != "inout" else None) for t, p, z, e in keys]


# =================================================================== the ten scenes
def scene_coeur(D, script):
    """The void; a golden light: the cosmic heart beats, alone."""
    O = (480, 270)
    hs = 3.2
    tr = Track(pos=O, alpha=0.0).to(1.6, alpha=0.0).to(3.4, alpha=1.0).to(D, alpha=1.0)
    els = space(D, drift=3.0, nebula=0.0, alpha=0.0)
    els[0]["keys"] = [K(0, pos=(-120, -70), alpha=0.25), K(4.0, alpha=0.6), K(D, alpha=0.75)]
    els[1]["keys"] = [K(0, pos=(-160, -40), scale=2.0, alpha=0.0), K(5.0, alpha=0.0), K(D, alpha=0.45)]
    els.append(stars(0.5))
    els += heart_glow(Track(pos=O, alpha=0.0).to(1.4, alpha=0.0).to(3.6, alpha=0.9).to(D, alpha=1.0), radius=130)
    heart = Track(pos=O, scale=hs * 0.6, alpha=0.0).to(1.8, alpha=0.0).to(3.6, alpha=1.0, scale=hs).to(D, scale=hs * 1.05)
    heart.clip(0, "intact")
    els.append(sprite("coeur", "coeur", heart))
    els.append(glo("etincelle", "#fff6d8", 40, Track(pos=O, alpha=0.0).to(1.5, alpha=0.0).to(1.7, alpha=1.0, ease="out").to(2.6, alpha=0.0)))
    els.append(particles("braises", "embers", GOLD, (300, 160, 360, 240), [K(0, alpha=0.0), K(3.0, alpha=0.0), K(5.0, alpha=0.7)]))
    els.append(particles("poussiere", "dust", "#c8b0ff", (0, 60, 960, 420), [K(0, alpha=0.3)], depth=0.3))
    cam = camera((0, (480, 270), 1.0, "linear"), (D, (480, 266), 1.45, "inout"))
    return dict(backdrop="#020206", camera=cam, elements=els, still=7.0)


def scene_forge(D, script):
    """Rock, magma and ice weld around the heart: its body assembles, branch after branch."""
    O, s = (480, 286), 1.55
    T = Titan(O, s)
    rng = np.random.default_rng(42)
    els = space(D, drift=5.0, nebula=0.5)
    els.append(stars(0.6))
    els.append(glo("aura_lueur", VIOLET, 360, Track(pos=O, alpha=0.0).to(7.6, alpha=0.0).to(9.0, alpha=0.5).to(D, alpha=0.45), pulse=(0.25, 2.6)))
    # the chunks spiralling in from every side (rock, magma, ice)
    for i in range(30):
        kind = ("roche", "magma", "glace")[i % 3]
        a0 = float(rng.uniform(0, TAU))
        r0 = float(rng.uniform(480, 640))
        t0 = float(rng.uniform(0.0, 3.0))
        t1 = t0 + float(rng.uniform(2.2, 3.4))
        target = T.point((float(rng.uniform(56, 104)), float(rng.uniform(20, 112))))
        mid = (O[0] + math.cos(a0 + 1.1) * r0 * 0.45, O[1] + math.sin(a0 + 1.1) * r0 * 0.3)
        tr = Track(pos=(O[0] + math.cos(a0) * r0, O[1] + math.sin(a0) * r0 * 0.7), scale=float(rng.uniform(1.4, 2.6)), alpha=0.0,
                   rot=float(rng.uniform(0, 360)))
        tr.to(t0, alpha=0.0).to(t0 + 0.4, alpha=1.0)
        tr.to((t0 + t1) / 2, "linear", pos=mid).to(t1, "in", pos=target, scale=0.8)
        tr.to(t1 + 0.15, alpha=0.0).to(t1, "linear", rot=float(rng.uniform(360, 900)) * (1 if i % 2 else -1))
        tr.clip(0, kind)
        els.append(sprite(f"eclat{i + 1}", "eclat", tr, depth=1.0 if i % 4 else 1.2))
    # the heart, beating from the start
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=0.9).to(D, alpha=1.0), radius=90)
    # the branches: a fog of matter first, flying in, then the branch itself, its rim flaring
    times = {"jambe_droite": 3.6, "jambe_gauche": 4.1, "abdomen": 4.7, "torse": 5.3, "bras_droit": 5.9, "bras_gauche": 6.4, "crane": 7.2}
    for pid in ORDER:
        if pid in ("aura", "coeur"):
            continue
        t = times[pid]
        j = joint_at(pid, O, s)
        out = ((j[0] - O[0]) * 1.4, (j[1] - O[1]) * 1.4 + 20)
        fog = T.track(pid, D, "inconnu", breathe=False, alpha=0.0, offset=out)
        fog.to(t - 0.6, alpha=0.0).to(t - 0.2, alpha=0.9).to(t + 0.5, "out", pos=j).to(t + 0.9, alpha=0.0)
        els.append(sprite(pid + "_brume", pid, fog, tint="#d8c8ff"))
        body = T.track(pid, D, "intact", breathe=False, alpha=0.0)
        body.to(t + 0.4, alpha=0.0).to(t + 0.8, alpha=1.0)
        els.append(sprite(pid, pid, body))
        rim = T.track(pid, D, "contour", breathe=False, alpha=0.0)
        rim.to(t + 0.4, alpha=0.0).to(t + 0.55, alpha=1.0, ease="out").to(t + 1.4, alpha=0.0)
        els.append(sprite(pid + "_contour", pid, rim, tint="#ffb050", add=True))
    els.append(sprite("coeur", "coeur", T.track("coeur", D, "intact", breathe=False)))
    els.append(sprite("aura", "aura", T.track("aura", D, "intact", breathe=False, alpha=0.0).to(8.2, alpha=0.0).to(9.2, alpha=1.0), add=True))
    eyes = [glo(f"oeil{i + 1}", "#ff5a2a", 26, Track(pos=T.point(e), alpha=0.0).to(9.0, alpha=0.0).to(9.3, alpha=1.0, ease="out").to(D, alpha=0.8),
                pulse=(0.3, 0.5)) for i, e in enumerate(EYES)]
    els += eyes
    els.append(particles("braises", "embers", EMBER, (240, 120, 480, 360), [K(0, alpha=0.4), K(8.5, alpha=0.9)]))
    els.append(particles("eclats", "sparks", GOLD, (280, 100, 400, 360), [K(0, alpha=0.0), K(3.5, alpha=0.8), K(8.6, alpha=0.4)]))
    hpz = (hp[0], hp[1] - 10)
    cam = camera((0, hpz, 2.0, "linear"), (3.4, hpz, 1.7, "inout"), (8.6, (480, 280), 1.05, "inout"), (D, (480, 276), 1.12, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[shake(8.55, 9.6, 7.0)], flash=[flash(8.6, "#ffd0a0", 0.55, 0.12, 0.9)],
                still=9.6)


def scene_festin(D, script):
    """Asterion swallows a world; its lights go out one by one; the seven parts feed."""
    O, s = (650, 300), 1.5
    T = Titan(O, s)
    els = space(D, drift=7.0, nebula=0.6)
    els.append(stars(0.7))
    els.append(glo("aura_lueur", VIOLET, 340, Track(pos=O, alpha=0.4), pulse=(0.2, 2.4)))
    els += T.parts(D)
    maw = T.point(MAW)
    w0 = (250, 250)
    t_off, t_pull, t_eat = 3.2, 7.8, 10.6
    world = Track(pos=w0, scale=1.25).to(t_pull, "linear", pos=(330, 240)).to(t_eat, "in", pos=maw, scale=0.18)
    world.to(t_eat - 0.3, alpha=1.0).to(t_eat + 0.05, alpha=0.0)
    world.to(t_pull, rot=0.0).to(t_eat, "in", rot=-80)
    world.clip(0, "lit").clip(t_off, "extinction")
    els.append(glo("monde_lueur", "#ffd27a", 150, Track(pos=w0, alpha=0.5).to(t_off, alpha=0.5).to(t_pull, "linear", pos=(330, 240), alpha=0.12)
                   .to(t_eat, "in", pos=maw, alpha=0.0)))
    els.append(sprite("monde", "monde", world))
    els.append(particles("trainee", "ember_rain", "#ffb070", (300, 120, 300, 240), [K(0, alpha=0.0), K(t_pull + 0.8, alpha=0.0), K(t_eat - 0.4, alpha=0.9),
                                                                                       K(t_eat + 0.6, alpha=0.0)]))
    els.append(glo("gueule", EMBER, 70, Track(pos=maw, alpha=0.0).to(t_eat - 0.6, alpha=0.0).to(t_eat + 0.1, alpha=1.0, ease="out")
                   .to(t_eat + 1.5, alpha=0.0)))
    # fed: the heart flares, then each branch's rim lights up from the heart outwards
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=0.6).to(t_eat + 0.2, alpha=0.6).to(t_eat + 0.6, alpha=1.0, ease="out").to(D, alpha=0.9), radius=80)
    for i, pid in enumerate(["torse", "abdomen", "crane", "bras_droit", "bras_gauche", "jambe_droite", "jambe_gauche"]):
        t = t_eat + 0.7 + 0.22 * i
        rim = T.track(pid, D, "contour", alpha=0.0)
        rim.to(t, alpha=0.0).to(t + 0.2, alpha=1.0, ease="out").to(t + 1.4, alpha=0.25).to(D, alpha=0.0)
        els.append(sprite(pid + "_festin", pid, rim, tint=GLOW[pid], add=True))
    els.append(particles("eclats", "sparks", GOLD, (500, 120, 300, 360), [K(0, alpha=0.0), K(t_eat, alpha=0.0), K(t_eat + 0.6, alpha=0.9)]))
    cam = camera((0, (330, 260), 1.35, "linear"), (t_pull, (420, 260), 1.25, "inout"), (t_eat, (maw[0] - 40, maw[1] + 40), 1.45, "inout"),
                 (D, (600, 290), 1.12, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[shake(t_eat - 0.05, t_eat + 1.2, 6.0)],
                flash=[flash(t_eat, "#ffb070", 0.5, 0.1, 0.8)], still=5.6)


def scene_gardiens(D, script):
    """The seven parts light up one by one, as the script's titles name them."""
    O, s = (668, 284), 1.55
    T = Titan(O, s)
    titles = [ti for ti in script.get("titles", [])]
    starts = [float(ti.get("t", 1.2 + 2.6 * k)) for k, ti in enumerate(titles)][:7]
    while len(starts) < 7:
        starts.append(1.2 + 2.6 * len(starts))
    els = space(D, drift=4.0, nebula=0.45)
    els.append(stars(0.5))
    els.append(glo("aura_lueur", VIOLET, 330, Track(pos=O, alpha=0.25).to(D - 1.0, alpha=0.25).to(D, alpha=0.6), pulse=(0.2, 2.6)))
    els += T.parts(D, prefix="ombre_", look="intact", tint="#2a2444", aura=False)
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=0.35).to(starts[-1] + 2.0, alpha=0.35).to(D - 0.6, alpha=1.0), radius=80)
    for pid in ORDER:  # the branches in their drawing order, each lit at its time
        if pid not in BRANCHES:
            continue
        t = starts[BRANCHES.index(pid)]
        body = T.track(pid, D, "intact", alpha=0.0)
        body.to(t - 0.05, alpha=0.0).to(t + 0.45, alpha=1.0, ease="out")
        els.append(sprite(pid, pid, body))
    for k, pid in enumerate(BRANCHES):
        t = starts[k]
        rim = T.track(pid, D, "contour", alpha=0.0)
        rim.to(t - 0.05, alpha=0.0).to(t + 0.2, alpha=1.0, ease="out").to(t + 2.3, alpha=0.45).to(t + 2.8, alpha=0.0)
        els.append(sprite(pid + "_contour", pid, rim, tint=GLOW[pid], add=True))
        anchor = T.point(anchor_of(pid))
        els.append(glo(pid + "_lueur", GLOW[pid], 120, Track(pos=anchor, alpha=0.0).to(t - 0.05, alpha=0.0).to(t + 0.3, alpha=0.8, ease="out")
                       .to(t + 2.5, alpha=0.0)))
    els.append(sprite("coeur", "coeur", T.track("coeur", D, "intact")))
    els.append(sprite("aura", "aura", T.track("aura", D, "intact", alpha=0.0).to(D - 1.2, alpha=0.0).to(D, alpha=0.9), add=True))
    els.append(particles("poussiere", "dust", "#d8c8ff", (380, 60, 560, 440), [K(0, alpha=0.4)]))
    # the camera goes to each branch in turn (the titan kept right of the titles)
    cam = [(0, (560, 280), 1.2, "linear")]
    for k, pid in enumerate(BRANCHES):
        a = T.point(anchor_of(pid))
        cam.append((starts[k] + 0.3, (min(600, a[0] - 70), a[1]), 1.42, "inout"))
    cam.append((D, (540, 280), 1.15, "inout"))
    return dict(backdrop="#030308", camera=camera(*cam), elements=els, still=starts[3] + 1.2)


def anchor_of(pid):
    for lay in manifest()["layers"]:
        if lay["id"] == pid and lay.get("anchor"):
            return tuple(lay["anchor"])
    return JOINT[pid]


def scene_galaxie(D, script):
    """Our planets aligned; its eyes light up; it turns towards them."""
    els = space(D, drift=5.0, nebula=0.55)
    els.append(stars(0.8))
    sun = (150, 150)
    els.append(glo("soleil_lueur", GOLD, 220, Track(pos=sun, alpha=0.8), depth=0.3, pulse=(0.2, 1.8)))
    els.append(layer("soleil", "ui/planets/sun.png", 0.3, (sun[0] - 64, sun[1] - 64), scale=0.25))
    planets = [("sylvanox", 0.15), ("pyralis", 0.13), ("glacior", 0.14), ("aethra", 0.16), ("confins", 0.13)]
    for i, (p, sc) in enumerate(planets):
        x, y = 250 + i * 92, 205 + i * 34
        size = 256 * S.PIX * sc
        els.append(glo(p + "_lueur", "#8ab4ff", size * 0.7, Track(pos=(x, y), alpha=0.25), depth=0.35 + 0.05 * i))
        els.append(layer(p, f"ui/planets/{p}.png", 0.35 + 0.05 * i, (x - size / 2, y - size / 2), scale=sc))
    O, s = (850, 360), 1.75
    t_eyes, t_turn = 3.6, 6.4
    T = Titan(O, s).move(t_turn, O, s).move(D, (800, 330), 1.95, "inout")
    els += T.parts(D, tint="#5a5470")
    for i, e in enumerate(EYES):
        tr = Track(pos=T.point(e, 0.0), alpha=0.0)
        tr.to(t_eyes, alpha=0.0).to(t_eyes + 0.3, alpha=1.0, ease="out").to(t_turn, pos=T.point(e, t_turn)).to(D, pos=T.point(e, D))
        els.append(glo(f"oeil{i + 1}", "#ff4a1a", 34, tr, pulse=(0.25, 0.4)))
    rim = Track(pos=T.point(JOINT["crane"]), scale=s, alpha=0.0).to(t_eyes, alpha=0.0).to(t_eyes + 0.25, alpha=0.9, ease="out")
    rim.to(t_eyes + 1.6, alpha=0.2).to(t_turn, pos=T.point(JOINT["crane"], t_turn)).to(D, pos=T.point(JOINT["crane"], D), scale=1.95)
    rim.clip(0, "contour")
    els.append(sprite("crane_contour", "crane", rim, tint="#ff4a1a", add=True))
    hp = Track(pos=T.point(JOINT["coeur"]), alpha=0.3).to(t_eyes, alpha=0.3).to(t_eyes + 0.6, alpha=0.9).to(t_turn, pos=T.point(JOINT["coeur"], t_turn))
    hp.to(D, pos=T.point(JOINT["coeur"], D))
    els += heart_glow(hp, radius=80)
    cam = camera((0, (430, 250), 1.3, "linear"), (t_eyes - 0.4, (520, 270), 1.2, "inout"), (t_eyes + 0.6, (700, 250), 1.5, "inout"),
                 (t_turn + 0.4, (600, 280), 1.2, "inout"), (D, (430, 260), 1.25, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=t_eyes + 1.2)


def scene_nova(D, script):
    """Nova rushes at the titan alone; Dreadfang swats her away with the back of its claws."""
    O, s = (330, 300), 1.7
    T = Titan(O, s)
    els = space(D, drift=8.0, nebula=0.6)
    els.append(stars(0.7))
    pl = (820, 120)
    els.append(glo("sylvanox_lueur", "#8ab4ff", 120, Track(pos=pl, alpha=0.3), depth=0.3))
    els.append(layer("sylvanox", "ui/planets/sylvanox.png", 0.3, (pl[0] - 64, pl[1] - 64), scale=0.25))
    els.append(glo("aura_lueur", VIOLET, 360, Track(pos=O, alpha=0.4), pulse=(0.2, 2.4)))
    t_in, t_rear, t_hit = 1.2, 4.0, 4.85
    for pid in ORDER:
        tr = T.track(pid, D, "intact", breathe=pid != "bras_gauche")
        if pid == "bras_gauche":
            tr.to(t_rear, rot=0.0).to(t_rear + 0.5, rot=22.0, ease="out").to(t_hit, "in", rot=-64.0).to(t_hit + 0.5, rot=-70.0, ease="out")
            tr.to(t_hit + 2.2, rot=0.0)
        els.append(sprite(pid, pid, tr, add=pid == "aura"))
    rim = T.track("bras_gauche", D, "contour", breathe=False, alpha=0.0)
    rim.to(t_rear, alpha=0.0, rot=0.0).to(t_rear + 0.5, alpha=0.9, rot=22.0, ease="out").to(t_hit, "in", rot=-64.0)
    rim.to(t_hit + 0.5, rot=-70.0, ease="out").to(t_hit + 1.2, alpha=0.0).to(t_hit + 2.2, rot=0.0)
    els.append(sprite("bras_gauche_contour", "bras_gauche", rim, tint=GLOW["bras_gauche"], add=True))
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=0.7), radius=80)
    eyes = [glo(f"oeil{i + 1}", "#ff4a1a", 24, Track(pos=T.point(e), alpha=0.7), pulse=(0.3, 0.5)) for i, e in enumerate(EYES)]
    els += eyes
    # Nova: she comes in from the right, dashes at the titan, is struck and spins away
    hit = (664, 214)
    nova = Track(pos=(1010, 330), scale=2.0)
    nova.to(t_in, "linear", pos=(860, 300)).to(2.6, "out", pos=(780, 250)).to(t_hit, "in", pos=(hit[0] + 4, hit[1] + 26))
    nova.to(t_hit + 1.4, "out", pos=(1060, 560), scale=1.2).to(t_hit, "linear", rot=0.0).to(t_hit + 1.4, "linear", rot=780.0)
    nova.clip(0, "run").clip(t_in, "jump").clip(1.7, "double_jump").clip(2.6, "dash").clip(t_hit, "hurt")
    nova.flip = True
    els.append(sprite("nova", NOVA, nova))
    els.append(glo("nova_lueur", "#9fe8ff", 46, Track(pos=(1010, 300), alpha=0.6).to(t_in, "linear", pos=(860, 270)).to(2.6, "out", pos=(780, 220))
                   .to(t_hit, "in", pos=(hit[0] + 4, hit[1] - 4), alpha=0.9).to(t_hit + 1.4, "out", pos=(1060, 530), alpha=0.0)))
    els.append(glo("impact", "#ffffff", 90, Track(pos=hit, alpha=0.0).to(t_hit - 0.05, alpha=0.0).to(t_hit + 0.02, alpha=1.0, ease="out")
                   .to(t_hit + 0.6, alpha=0.0)))
    els.append(particles("eclats", "sparks", "#e0f0ff", (600, 160, 160, 120), [K(0, alpha=0.0), K(t_hit, alpha=0.0), K(t_hit + 0.1, alpha=1.0),
                                                                                  K(t_hit + 1.6, alpha=0.0)]))
    els.append(particles("braises", "embers", EMBER, (180, 140, 300, 340), [K(0, alpha=0.5)]))
    cam = camera((0, (760, 290), 1.4, "linear"), (2.6, (680, 270), 1.3, "inout"), (t_hit, (600, 250), 1.5, "in"),
                 (t_hit + 1.6, (560, 300), 1.15, "out"), (D, (420, 290), 1.2, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[shake(t_hit, t_hit + 0.9, 12.0)],
                flash=[flash(t_hit, "#ffffff", 0.75, 0.04, 0.45)], still=t_hit - 0.4)


def scene_clans(D, script):
    """Beacons light up everywhere; groups of eight and the clans' banners gather."""
    s = 0.8
    pos = ((960 - 1280 * s) / 2, (540 - 720 * s) / 2)
    tint = "#5a6890"
    G = 436  # the ground under their feet (the sheet of Monolithe's story, as in the trailer)
    els = []
    for name, depth, v in (("sky", 0.05, 3), ("far", 0.2, 8), ("mid", 0.45, 16), ("near", 0.75, 26)):
        els.append(layer(name, f"backgrounds/aethra/{name}.png", depth, pos, scale=s, drift=(-v, 0), repeat=True, tint=tint))
    els.append(stars(0.6, (0, 0, 960, 220), 0.05))
    rng = np.random.default_rng(77)
    # far beacons on the hills (smaller, slower), then those on the ground
    beacons = [(240, 352, 0.45, 4.3), (610, 344, 0.45, 4.8), (420, 338, 0.4, 5.4), (790, 348, 0.42, 5.9), (90, 360, 0.5, 6.3),
               (120, G, 1.0, 0.9), (330, G, 1.0, 1.6), (520, G, 1.0, 2.3), (700, G, 1.0, 3.0), (880, G, 1.0, 3.6)]
    for i, (x, y, depth, t) in enumerate(beacons):
        sc = 0.7 + depth * 0.9
        els.append(glo(f"balise{i + 1}_lueur", GOLD, 70 * sc, Track(pos=(x, y - 76 * sc), alpha=0.0).to(t, alpha=0.0).to(t + 0.35, alpha=0.9, ease="out"),
                       depth=depth, pulse=(0.25, 0.9, i * 0.13)))
        beam = Track(pos=(x, y - 72 * sc), scale=sc * 1.1, alpha=0.0).to(t, alpha=0.0).to(t + 0.5, alpha=0.75, ease="out")
        beam.clip(0, "idle")
        els.append(sprite(f"faisceau{i + 1}", "faisceau", beam, depth=depth, add=True))
        b = Track(pos=(x, y), scale=sc).clip(0, "off").clip(t, "on")
        els.append(sprite(f"balise{i + 1}", "balise", b, depth=depth))
    # far away, more clans gather on the hills (small banners)
    for i in range(6):
        x, y = 80 + i * 160 + float(rng.uniform(-30, 30)), 350 + float(rng.uniform(-8, 8))
        t = 5.0 + i * 0.5
        bt = Track(pos=(x, y), scale=0.7, alpha=0.0).to(t, alpha=0.0).to(t + 0.6, alpha=1.0)
        bt.clip(0, "wave", offset=i * 0.11)
        els.append(sprite(f"loin{i + 1}", "banniere", bt, depth=0.5, tint=CLANS[i % 4]))
    els.append(sprite("sol", "animations/stories/monolithe_sol.json", Track(pos=(-280, G - 32)).clip(0, "idle"), tint="#7a80a8") | {"repeat": True})
    # three groups of eight running to the middle, each behind its banner
    groups = [(-160, G, 1, 0.8, CLANS[0]), (1120, G, -1, 1.6, CLANS[1]), (-300, G + 4, 1, 3.0, CLANS[2])]
    meet = [(300, G), (660, G), (480, G + 4)]
    for g, ((x0, y0, d, t0, c), (mx, my)) in enumerate(zip(groups, meet)):
        t1 = t0 + 5.2
        for k in range(8):
            ox = -d * (16 + k * 20) + float(rng.uniform(-5, 5))
            sc = 1.45 + float(rng.uniform(-0.08, 0.08))
            tr = Track(pos=(x0 + ox, y0), scale=sc)
            tr.to(t0, pos=(x0 + ox, y0)).to(t1, "linear", pos=(mx + ox * 0.5, my))
            tr.clip(0, "idle").clip(t0, "run", offset=k * 0.07).clip(t1, "idle")
            tr.flip = d < 0
            els.append(sprite(f"groupe{g + 1}_{k + 1}", NOVA, tr, tint="#24203a", shadow=S.shade(30, alpha=0.3)))
        bt = Track(pos=(x0 + d * 10, y0), scale=1.6).to(t0, pos=(x0 + d * 10, y0)).to(t1, "linear", pos=(mx + d * 10, my))
        bt.to(t1, rot=0.0).to(t1 + 0.4, rot=-6.0 * d, ease="out").to(t1 + 1.0, rot=0.0)
        bt.clip(0, "wave", offset=g * 0.2)
        bt.flip = d < 0
        els.append(sprite(f"banniere{g + 1}", "banniere", bt, tint=c))
    els.append(particles("braises", "embers", GOLD, (60, 300, 840, 200), [K(0, alpha=0.0), K(2.0, alpha=0.6)]))
    els.append(layer("avant", "backgrounds/aethra/fg_petals.png", 1.3, pos, scale=s, drift=(-40, 0), repeat=True, alpha=0.5, tint=tint))
    cam = camera((0, (300, 360), 1.45, "linear"), (5.0, (520, 340), 1.3, "inout"), (D, (480, 300), 1.05, "inout"))
    return dict(backdrop="#06070e", camera=cam, elements=els, still=9.6)


def scene_hologramme(D, script):
    """The titan as a hologram: the seven branches lock as targets, the heart in the middle."""
    O, s = (480, 250), 1.5
    T = Titan(O, s)
    els = [layer("grille", "grille", 0.6, (0, 0), add=True, alpha=0.9),
           glo("projecteur", CYAN, 300, Track(pos=(480, 470), alpha=0.5), depth=0.6, pulse=(0.2, 1.1))]
    els += T.parts(D, prefix="holo_", look="inconnu", tint=CYAN, add=True, alpha=0.5, aura=False)
    for pid in ORDER:
        if pid in ("aura",):
            continue
        rim = T.track(pid, D, "contour", alpha=0.8)
        els.append(sprite(pid + "_trait", pid, rim, tint=CYAN, add=True, pulse=(0.25, 0.23, (ORDER.index(pid) * 0.17) % 1.0)))
    for k, pid in enumerate(BRANCHES):
        t = 1.6 + 1.05 * k
        rim = T.track(pid, D, "contour", alpha=0.0)
        rim.to(t, alpha=0.0).to(t + 0.15, alpha=1.0, ease="out").to(D, alpha=0.85)
        els.append(sprite(pid + "_cible", pid, rim, tint="#ff4a3a", add=True))
        a = T.point(anchor_of(pid))
        ret = Track(pos=a, scale=1.6, alpha=0.0).to(t - 0.02, alpha=0.0).to(t, alpha=1.0).clip(0, "lock").clip(t, "lock")
        els.append(sprite(pid + "_viseur", "cible", ret, tint="#ff6a4a"))
    hp = T.point(JOINT["coeur"])
    els.append(sprite("coeur", "coeur", T.track("coeur", D, "intact")))
    els += heart_glow(Track(pos=hp, alpha=0.5).to(9.0, alpha=0.5).to(10.0, alpha=1.0, scale=1.4, ease="out").to(D, alpha=0.9, scale=1.2), radius=70)
    ret = Track(pos=hp, scale=2.4, alpha=0.0).to(9.2, alpha=0.0).to(9.25, alpha=1.0).clip(0, "lock").clip(9.25, "lock")
    els.append(sprite("coeur_viseur", "cible", ret, tint=GOLD))
    els.append(particles("points", "sparks", CYAN, (240, 40, 480, 420), [K(0, alpha=0.6)]))
    cam = camera((0, (480, 280), 1.05, "linear"), (8.6, (480, 270), 1.2, "inout"), (D, (hp[0], hp[1] + 10), 1.7, "inout"))
    return dict(backdrop="#020812", camera=cam, elements=els, flash=[flash(9.25, GOLD, 0.25, 0.05, 0.5)], still=8.8)


def scene_rugissement(D, script):
    """Asterion roars and fills the screen."""
    O, s = (480, 318), 2.55
    T = Titan(O, s)
    t_roar = 1.6
    els = space(D, drift=10.0, nebula=0.7)
    els.append(glo("aura_lueur", VIOLET, 520, Track(pos=O, alpha=0.35).to(t_roar, alpha=0.35).to(t_roar + 0.4, alpha=0.6, ease="out").to(D, alpha=0.4),
                   pulse=(0.2, 2.0)))
    for pid in ORDER:
        tr = T.track(pid, D, "intact", breathe=False)
        p = joint_at(pid, O, s)
        if pid == "crane":
            tr.to(t_roar - 0.5, pos=p).to(t_roar, pos=(p[0], p[1] - 14), ease="out").to(t_roar + 0.3, pos=(p[0], p[1] + 6), ease="in")
            tr.to(t_roar + 2.6, pos=(p[0], p[1] + 4)).to(D, pos=p)
        elif pid in ("bras_droit", "bras_gauche"):
            d = 1 if pid == "bras_droit" else -1
            tr.to(t_roar - 0.3, rot=0.0).to(t_roar + 0.3, rot=10.0 * d, ease="out").to(t_roar + 2.8, rot=6.0 * d).to(D, rot=0.0)
        els.append(sprite(pid, pid, tr, add=pid == "aura"))
    rim = T.track("crane", D, "contour", breathe=False, alpha=0.0)
    p = joint_at("crane", O, s)
    rim.to(t_roar - 0.5, pos=p, alpha=0.0).to(t_roar, pos=(p[0], p[1] - 14), alpha=1.0, ease="out").to(t_roar + 0.3, pos=(p[0], p[1] + 6), ease="in")
    rim.to(t_roar + 2.6, pos=(p[0], p[1] + 4), alpha=0.0).to(D, pos=p)
    els.append(sprite("crane_contour", "crane", rim, tint="#ff5a2a", add=True))
    for i, e in enumerate(EYES):
        q = T.point(e)
        tr = Track(pos=q, alpha=0.7).to(t_roar - 0.5, pos=q).to(t_roar, pos=(q[0], q[1] - 14), alpha=1.0, scale=1.6, ease="out")
        tr.to(t_roar + 0.3, pos=(q[0], q[1] + 6), ease="in").to(t_roar + 2.6, pos=(q[0], q[1] + 4), scale=1.0).to(D, pos=q, alpha=0.8)
        els.append(glo(f"oeil{i + 1}", "#ff4a1a", 40, tr, pulse=(0.25, 0.4)))
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=0.6).to(t_roar, alpha=0.6).to(t_roar + 0.4, alpha=0.9, scale=1.4).to(D, alpha=0.7, scale=1.1), radius=85)
    els.append(particles("cendres", "ash", "#b8a0c8", (0, 100, 960, 440), [K(0, alpha=0.3), K(t_roar, alpha=0.3), K(t_roar + 0.3, alpha=1.0)]))
    els.append(particles("braises", "embers", EMBER, (160, 120, 640, 420), [K(0, alpha=0.5)]))
    mz = T.point(MAW)
    cam = camera((0, (480, 300), 1.0, "linear"), (t_roar, (mz[0], mz[1] + 60), 1.25, "inout"), (t_roar + 2.6, (480, 290), 1.08, "out"),
                 (D, (480, 300), 1.0, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[shake(t_roar, t_roar + 2.4, 14.0)],
                flash=[flash(t_roar + 0.25, "#ff9a5a", 0.55, 0.15, 0.9)], still=4.6)


def scene_appel(D, script):
    """The clans on a ridge, the titan far away on the horizon: « Rassemblez votre clan. »"""
    els = space(D, drift=4.0, nebula=0.6)
    els.append(stars(0.8))
    O, s = (480, 300), 0.95
    T = Titan(O, s)
    els.append(glo("aura_lueur", VIOLET, 240, Track(pos=O, alpha=0.5), depth=0.2, pulse=(0.2, 2.4)))
    els += T.parts(D, tint="#3a3058", depth=0.2)
    hp = T.point(JOINT["coeur"])
    els += heart_glow(Track(pos=hp, alpha=1.0), depth=0.2, radius=60)
    for i, e in enumerate(EYES):
        els.append(glo(f"oeil{i + 1}", "#ff4a1a", 16, Track(pos=T.point(e), alpha=0.9), depth=0.2, pulse=(0.3, 0.5)))
    rx, ry = -160, 300
    els.append(layer("crete", "crete", 1.0, (rx, ry)))
    top = ridge_top()
    ground = lambda x: ry + S.PIX * float(top[int(min(len(top) - 1, max(0, (x - rx) / S.PIX)))]) + 3  # noqa: E731
    rng = np.random.default_rng(91)
    for g in range(4):
        cx = 150 + g * 220
        for k in range(8):
            x = cx + (k - 3.5) * 16 + float(rng.uniform(-4, 4))
            tr = Track(pos=(x, ground(x)), scale=1.3).clip(0, "idle", offset=float(rng.uniform(0, 1)))
            els.append(sprite(f"heros{g + 1}_{k + 1}", NOVA, tr, tint="#120e20"))
        bt = Track(pos=(cx + 4, ground(cx + 4)), scale=1.5).clip(0, "wave", offset=g * 0.17)
        els.append(sprite(f"banniere{g + 1}", "banniere", bt, tint=CLANS[g]))
    els.append(particles("braises", "embers", GOLD, (0, 260, 960, 280), [K(0, alpha=0.5)]))
    cam = camera((0, (480, 330), 1.3, "linear"), (D, (480, 300), 1.08, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=3.0)


SCENES = {"coeur": scene_coeur, "forge": scene_forge, "festin": scene_festin, "gardiens": scene_gardiens, "galaxie": scene_galaxie,
          "nova": scene_nova, "clans": scene_clans, "hologramme": scene_hologramme, "rugissement": scene_rugissement, "appel": scene_appel}


# =================================================================== the sounds
def snd_coeur(D):
    return [amb("story_amb_space", 0.5)] + [sfx("story_heartbeat", 1.6 + 1.334 * k, 0.55 + 0.04 * min(k, 5)) for k in range(int((D - 1.8) / 1.334))] + [
        sfx("story_glow", 1.6, 0.5)]


def snd_forge(D):
    return [amb("story_amb_space", 0.4), sfx("story_stone", 1.0, 0.45, pan=-0.5), sfx("lava_bubble", 2.0, 0.5, pan=0.4),
            sfx("ice_shatter", 2.6, 0.35, pan=-0.3), sfx("story_stone", 3.6, 0.6, pitch=0.85), sfx("golem_slam", 4.1, 0.45),
            sfx("story_stone", 4.7, 0.6, pitch=0.8), sfx("golem_slam", 5.3, 0.5, pitch=0.9), sfx("story_flame", 5.9, 0.4, pan=-0.5),
            sfx("ice_shatter", 6.4, 0.45, pan=0.5), sfx("story_ignite", 7.2, 0.6), sfx("story_reveal", 8.6, 0.85, duck=True)] + [
        sfx("story_heartbeat", 0.4 + 1.334 * k, 0.4) for k in range(3)]


def snd_festin(D):
    return [amb("story_amb_space", 0.45), sfx("inhale", 7.8, 0.6), sfx("story_devour", 9.6, 0.9, duck=True),
            sfx("story_heartbeat", 11.0, 0.7)] + [sfx("story_glow", 11.3 + 0.22 * i, 0.3, pitch=1.0 + 0.06 * i) for i in range(7)]


def snd_gardiens(D, starts):
    return [amb("story_amb_space", 0.4)] + [sfx("story_reveal", t, 0.75, pitch=1.0 + 0.05 * (k % 3), duck=True) for k, t in enumerate(starts)] + [
        sfx("story_heartbeat", D - 1.4, 0.6)]


def snd_galaxie(D):
    return [amb("story_amb_space", 0.5), sfx("story_ignite", 3.6, 0.6), sfx("wyrm_roar", 6.2, 0.5, pitch=0.6, duck=True),
            sfx("gravity_well", 6.6, 0.4)]


def snd_nova(D):
    return [amb("story_amb_space", 0.4), sfx("jump", 1.2, 0.5), sfx("double_jump", 1.7, 0.5), sfx("dash", 2.6, 0.6), sfx("slash", 4.4, 0.6, pitch=0.7),
            sfx("pound_impact", 4.85, 0.9, duck=True), sfx("player_hurt", 4.9, 0.7), sfx("boss_roar", 6.0, 0.45, pitch=0.7)]


def snd_clans(D):
    return [amb("story_amb_night", 0.45)] + [sfx("story_beacon", t, 0.45, pan=round(x / 480.0 - 1.0, 2)) for x, t in (
        (120, 0.9), (330, 1.6), (520, 2.3), (700, 3.0), (860, 3.6), (240, 4.3), (610, 4.8), (420, 5.4), (790, 5.9))] + [
        sfx("footstep_a", 1.2, 0.3), sfx("footstep_b", 2.2, 0.3), sfx("story_wings", 6.4, 0.35), sfx("checkpoint", 9.0, 0.4)]


def snd_hologramme(D):
    return [amb("story_amb_space", 0.35), sfx("story_holo", 0.2, 0.6)] + [sfx("laser_charge", 1.6 + 1.05 * k, 0.3, pitch=1.0 + 0.06 * k)
                                                                         for k in range(7)] + [sfx("story_heartbeat", 9.2, 0.8, duck=True)]


def snd_rugissement(D):
    return [amb("story_amb_space", 0.3), sfx("inhale", 0.6, 0.6, pitch=0.7), sfx("story_titan_roar", 1.6, 1.0, duck=True),
            sfx("story_reveal", 3.2, 0.7)]


def snd_appel(D):
    return [amb("story_amb_night", 0.35), sfx("story_heartbeat", 0.8, 0.45), sfx("story_heartbeat", 2.13, 0.45)]


SOUNDS = {"coeur": snd_coeur, "forge": snd_forge, "festin": snd_festin, "galaxie": snd_galaxie, "nova": snd_nova, "clans": snd_clans,
          "hologramme": snd_hologramme, "rugissement": snd_rugissement, "appel": snd_appel}


# =================================================================== the scenes file
def read_script():
    with open(SCRIPT, encoding="utf-8") as f:
        return json.load(f)


def build_scenes():
    script = read_script()
    scenes = {}
    stills = {}
    for shot in script["shots"]:
        name = shot["scene"]
        D = float(shot.get("duration") or S.shot_duration(shot.get("text", "")))
        st = SCENES[name](D, shot)
        stills[name] = st.pop("still")
        if name == "gardiens":
            starts = [float(t.get("t", 0.0)) for t in shot.get("titles", [])][:7]
            st["sfx"] = snd_gardiens(D, starts)
        else:
            st["sfx"] = SOUNDS[name](D)
        st["sfx"] = [x for x in st["sfx"] if x["t"] < D]
        stage = {"size": [W, H], "length": round(D, 3)}
        stage.update(st)
        scenes[name] = stage
    rel = script.get("scenes", "raids/asterion_film_scenes.json")
    out = {"format": "cosmic.film_scenes", "version": 1,
           "comment": ("The animated scenes of Asterion's film (2.68), named by data/raids/asterion_film.json (its captions, titles and "
                       "lengths: edit them there). Written by tools/assetgen/environment/story_asterion.py: do not edit. Each scene "
                       "is a stage of the stories' format (game/content/BossStories.h); its `length` is the seconds its keys were "
                       "written for (stretched to the script's length)."),
           "scenes": scenes}
    return {os.path.join("data", rel): dump_scenes(out).encode("utf-8")}, stills


def dump_scenes(out):
    """The scenes file, readable and small: each element of a stage on its own line."""
    c = lambda o: json.dumps(o, ensure_ascii=False, separators=(", ", ": "))  # noqa: E731
    lines = ["{", f' "format": {c(out["format"])},', f' "version": {c(out["version"])},', f' "comment": {c(out["comment"])},', ' "scenes": {']
    names = list(out["scenes"])
    for i, name in enumerate(names):
        st = out["scenes"][name]
        lines.append(f"  {c(name)}: {{")
        for k, v in st.items():
            if k != "elements":
                lines.append(f"   {c(k)}: {c(v)},")
        lines.append('   "elements": [')
        for j, e in enumerate(st["elements"]):
            lines.append("    " + c(e) + ("," if j < len(st["elements"]) - 1 else ""))
        lines.append("   ]")
        lines.append("  }" + ("," if i < len(names) - 1 else ""))
    lines += [" }", "}"]
    return "\n".join(lines) + "\n"


def build_all():
    files = {os.path.join("assets", k): v for k, v in build_assets().items()}
    scenes, stills = build_scenes()
    files.update(scenes)
    return files, stills


def write(files):
    out = []
    for rel, data in sorted(files.items()):
        p = os.path.join(REPO, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        out.append(p)
    return out


def generate(review=None):
    files, _ = build_all()
    return write(files)


def check():
    a, _ = build_all()
    b, _ = build_all()
    errs = []
    if a != b:
        errs.append("story_asterion: two builds differ (not deterministic)")
    for rel, data in sorted(a.items()):
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            errs.append(f"{rel}: missing (python3 tools/assetgen/environment/story_asterion.py)")
            continue
        with open(p, "rb") as f:
            if f.read() != data:
                errs.append(f"{rel}: not the generator's output (regenerate)")
    return errs


def storyboard(path):
    """One frame per shot (its still, as the game draws it without its live particles), labelled in
    French with its number, its scene and its caption or title."""
    files, stills = build_all()
    write(files)
    S.Anim._cache.clear()
    with open(os.path.join(REPO, "data", read_script().get("scenes", "raids/asterion_film_scenes.json")), encoding="utf-8") as f:
        scenes = json.load(f)["scenes"]
    script = read_script()
    fw, fh, lab = 480, 270, 44
    cols = 2
    rows = (len(script["shots"]) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (fw + 12) + 12, rows * (fh + lab + 12) + 60), (16, 14, 26))
    d = ImageDraw.Draw(sheet)
    font = None
    for cand in ("assets/fonts/Inter-Regular.ttf", "assets/fonts/NotoSans-Regular.ttf"):
        p = os.path.join(REPO, cand)
        if os.path.exists(p):
            font = ImageFont.truetype(p, 15)
            big = ImageFont.truetype(p, 22)
            break
    if font is None:
        fonts = [f for f in os.listdir(os.path.join(ASSETS, "fonts")) if f.endswith(".ttf")]
        font = ImageFont.truetype(os.path.join(ASSETS, "fonts", sorted(fonts)[0]), 15)
        big = ImageFont.truetype(os.path.join(ASSETS, "fonts", sorted(fonts)[0]), 22)
    total = sum(float(s.get("duration", 0)) for s in script["shots"])
    d.text((12, 16), f"ASTERION — LE DÉVOREUR DE MONDES : le mini-film, plan par plan ({total:.0f} s)", fill=(255, 210, 160), font=big)
    for i, shot in enumerate(script["shots"]):
        r, c = divmod(i, cols)
        x, y = 12 + c * (fw + 12), 60 + r * (fh + lab + 12)
        img = S.render(scenes[shot["scene"]], stills[shot["scene"]], size=(fw, fh))
        sheet.paste(img, (x, y))
        words = shot.get("text") or " / ".join(f"{t['text']} — {t.get('subtitle', '')}" for t in shot.get("titles", [])[:2])
        label = f"{i + 1}. {shot['scene']} ({float(shot.get('duration', 0)):.0f} s) : {words}"
        while d.textlength(label, font=font) > fw and len(label) > 8:
            label = label[:-2]
        if label != f"{i + 1}. {shot['scene']} ({float(shot.get('duration', 0)):.0f} s) : {words}":
            label = label[:-1] + "…"
        d.text((x, y + fh + 8), label, fill=(232, 228, 246), font=font)
    sheet.save(path)
    return path


if __name__ == "__main__":
    if "--check" in sys.argv:
        e = check()
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    if "--storyboard" in sys.argv:
        print(storyboard(sys.argv[sys.argv.index("--storyboard") + 1]))
    else:
        for p in generate():
            print(os.path.relpath(p, REPO), os.path.getsize(p))
