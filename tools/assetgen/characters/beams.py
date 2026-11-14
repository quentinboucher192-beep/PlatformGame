#!/usr/bin/env python3
"""Textures of the bosses' beams (version 2.8): sprites/projectiles/beams.png and
animations/beams.json. The game used to draw lasers as plain lines; the beams are now these
animated strips, drawn along the beam: a start cap at the boss's mouth, a body repeated as
many times as the beam is long, an end cap where it hits.

Each frame is 64x16: start cap (0-15), body (16-47, seamless horizontally), end cap (48-63).
One row of 4 frames per style, in the palettes of the bosses' own breaths and beams
(boss_b_palette: the Magmaroth's flame stream, the Glaciarque's frost beam...):

  flame   Pyralis: turbulent fire, white-hot core            (Magmaroth, Ignivore, Forgeron...)
  frost   Glacior: ice-white core, crystal edges              (Cryowyrm, Glaciarque, Wyrm...)
  plasma  Sylvanox's machines: cyan energy with sparks        (Sentinelle Prime)
  thorn   Sylvanox's guardians: green-cyan sap light, spores  (Thornwarden, Archonte)
  star    the Stellar Core: gold and white starlight           (Astrarque)

Run from the repository root:  python3 tools/assetgen/characters/beams.py
"""
import json
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
FW, FH, FRAMES = 64, 16, 4
BODY0, BODY1 = 16, 48  # the repeated part
PERIOD = BODY1 - BODY0

STYLES = [
    # id, colours from core to rim (5), spark colour, edge shape
    ("flame", ["#fff1c2", "#ffc56b", "#ff8a3d", "#ff4d1e", "#b3201a"], "#ffc56b", "fire"),
    ("frost", ["#ffffff", "#eefaff", "#7ff6ff", "#5fb0ff", "#2f6fb3"], "#dfe9f5", "crystal"),
    ("plasma", ["#ffffff", "#c9fff8", "#39f0d8", "#1fa8c8", "#5b2a9e"], "#ffffff", "smooth"),
    ("thorn", ["#f4ffe0", "#b8ff9a", "#5dffb0", "#2fb37a", "#1e5a3a"], "#d6ff8a", "wave"),
    ("star", ["#ffffff", "#fff6d0", "#ffd98a", "#ffb347", "#d36ad8"], "#ffffff", "smooth"),
]


def hexc(h):
    h = h.lstrip("#")
    return [int(h[i:i + 2], 16) for i in (0, 2, 4)]


def half_width(style, x, i):
    """Beam half thickness at column x (body columns are periodic over PERIOD)."""
    t = 2 * math.pi * (x - BODY0) / PERIOD
    ph = i * math.pi / 2
    if style == "fire":
        return 4.6 + 0.9 * math.sin(t * 2 + ph) + 0.5 * math.sin(t * 3 - ph * 1.5)
    if style == "crystal":
        return 3.6 + (0.8 if (x + i * 3) % 8 < 2 else 0.0)
    if style == "wave":
        return 3.8 + 0.8 * math.sin(t * 2 + ph)
    return 3.9 + 0.5 * math.sin(t * 4 + ph)


def centre(style, x, i):
    t = 2 * math.pi * (x - BODY0) / PERIOD
    if style == "fire":
        return 7.5 + 0.8 * math.sin(t + i * math.pi / 2)
    if style == "wave":
        return 7.5 + 1.2 * math.sin(t * 2 - i * math.pi / 2)
    return 7.5


def frame(style_id, cols, spark, shape, i):
    img = np.zeros((FH, FW, 4), np.uint8)
    c = [hexc(h) for h in cols]
    rng = np.random.default_rng(1000 + i * 7 + len(style_id))
    for x in range(FW):
        # caps: the beam widens from a bright point (start) and frays (end)
        if x < BODY0:
            k = (x + 1) / BODY0
            hw = half_width(shape, BODY0, i) * (0.45 + 0.55 * k) + (1.6 if x < 5 else 0.0)
            yc = 7.5
        elif x >= BODY1:
            k = 1 - (x - BODY1) / (FW - BODY1)
            hw = half_width(shape, BODY1 - 1, i) * (0.35 + 0.75 * k)
            yc = centre(shape, BODY1 - 1, i)
        else:
            hw = half_width(shape, x, i)
            yc = centre(shape, x, i)
        for y in range(FH):
            e = abs(y + 0.5 - yc) / max(hw, 0.5)
            if shape == "fire":
                e += 0.12 * math.sin(x * 1.3 + y * 0.9 + i * 2.1)
            if e > 1.05:
                continue
            ci = 0 if e < 0.28 else (1 if e < 0.5 else (2 if e < 0.72 else (3 if e < 0.9 else 4)))
            a = 255 if ci < 4 else 190
            if x >= BODY1 and (x + y + i) % 3 == 0 and ci >= 3:
                continue  # fraying end
            img[y, x, :3] = c[ci]
            img[y, x, 3] = a
    # sparks and motes around the body (periodic positions, so the body still tiles)
    for k in range(5):
        x = BODY0 + (k * 7 + i * 5) % PERIOD
        side = -1 if (k + i) % 2 else 1
        y = int(round(centre(shape, x, i) + side * (half_width(shape, x, i) + 1.5)))
        if 0 <= y < FH and img[y, x, 3] == 0:
            img[y, x, :3] = hexc(spark)
            img[y, x, 3] = 230
    # start flash
    for y in range(FH):
        for x in range(0, 6):
            d = math.hypot(x - 1.5, y - 7.5)
            if d < 3.2:
                img[y, x, :3] = c[0]
                img[y, x, 3] = 255
    _ = rng
    return img


def build():
    atlas = np.zeros((FH * len(STYLES), FW * FRAMES, 4), np.uint8)
    clips = {}
    for row, (sid, cols, spark, shape) in enumerate(STYLES):
        for i in range(FRAMES):
            atlas[row * FH:(row + 1) * FH, i * FW:(i + 1) * FW] = frame(sid, cols, spark, shape, i)
        clips[sid] = {"row": row, "count": FRAMES, "fps": 14, "loop": True}
    png = os.path.join(ROOT, "assets", "sprites", "projectiles", "beams.png")
    Image.fromarray(atlas, "RGBA").save(png, optimize=True)
    meta = {"texture": "sprites/projectiles/beams.png", "frameWidth": FW, "frameHeight": FH, "pivot": [0, FH // 2],
            "beam": {"startCap": BODY0, "body": [BODY0, BODY1], "endCap": FW - BODY1}, "clips": clips}
    with open(os.path.join(ROOT, "assets", "animations", "beams.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")
    print(png, atlas.shape)


if __name__ == "__main__":
    build()
