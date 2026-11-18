#!/usr/bin/env python3
"""2.71: the clan raids' rising magma and the textures of their minimap (docs/RAIDS.md §15).

Two sheets, both deterministic (fixed seeds, numpy only for the pixels; the shapes of the icons are
drawn four times larger with Pillow's ImageDraw, then averaged down by numpy):

* sprites/objects/raid_magma.png + animations/raid_magma.json: animated lava for the rising magma
  (DamageZoneEntity, style « magma »), 32 × 32 frames that tile in both directions:
    body     molten rock that flows (8 frames, the pattern drifts one tile in a loop)
    crust    dark basalt crust drifting on it, its cracks glowing (8 frames, over the body)
    surface  the bright waves of its surface, the crest on row 12 (8 frames)
    bubble   a bubble that swells and pops (8 frames)
    ember    a spark, its glow flickering (8 frames)
    warn     the telegraph before a rise: the floor cracking, glowing (8 frames, drawn additive)
    glow     the glow at the surface above what the magma hides (1 frame)
* ui/raids/minimap.png + ui/raids/minimap.json: the textures of the raid's minimap and of its
  large map (ui/online/RaidHud): tiles of 16 px that tile (rock, its lit edge, a one-way ledge,
  lava, the rising magma, the background) and icons of 24 px (this player, an ally — white, tinted
  by the game with the seat's colour —, an enemy, a boss, the objectives to do, done and optional,
  an open passage, a locked one, a door, a checkpoint and a lit one, a co-op plate and a done one,
  a gigaboss's vent and a closed one, the magma). The manifest names each cell: {x, y, w, h}.

    python3 tools/assetgen/environment/raid_visuals.py            # writes the four files
    python3 tools/assetgen/environment/raid_visuals.py --check    # 0: the files are the generator's
    python3 tools/assetgen/environment/raid_visuals.py --review out.png   # a contact sheet in French

`--check` renders everything twice (the same bytes) and compares the JSON bytes and the PNG pixels
with the files on disk (tests/test_raid_visuals.cpp runs it)."""
import io
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")

MAGMA_PNG = "sprites/objects/raid_magma.png"
MAGMA_JSON = "animations/raid_magma.json"
MAP_PNG = "ui/raids/minimap.png"
MAP_JSON = "ui/raids/minimap.json"
F = 32          # a magma frame
FRAMES = 8
TILE, ICON = 16, 24
MAGMA_ROWS = [("body", 8, 6), ("crust", 8, 2), ("surface", 8, 9), ("bubble", 8, 12), ("ember", 8, 12), ("warn", 8, 10),
              ("glow", 1, 1)]

# The lava ramp: deep, dark red → orange → yellow → white-hot.
RAMP = np.array([(52, 8, 4), (104, 18, 6), (160, 34, 8), (214, 70, 12), (246, 120, 24), (255, 170, 48), (255, 214, 104),
                 (255, 244, 196)], np.float32)


def ramp(t):
    t = np.clip(t, 0.0, 1.0) * (len(RAMP) - 1)
    i = np.clip(np.floor(t).astype(int), 0, len(RAMP) - 2)
    f = (t - i)[..., None]
    return RAMP[i] * (1 - f) + RAMP[i + 1] * f


def noise(size, cell, seed):
    """Smooth value noise in [0, 1] that tiles on a `size` square (cell divides size)."""
    r = np.random.default_rng(seed)
    g = size // cell
    grid = r.random((g, g)).astype(np.float32)
    xs = np.arange(size, dtype=np.float32) / cell
    x0 = np.floor(xs).astype(int)
    fx = xs - x0
    fx = fx * fx * (3 - 2 * fx)
    x0 %= g
    x1 = (x0 + 1) % g
    a = grid[x0][:, x0] * (1 - fx[None, :]) + grid[x0][:, x1] * fx[None, :]
    b = grid[x1][:, x0] * (1 - fx[None, :]) + grid[x1][:, x1] * fx[None, :]
    return a * (1 - fx[:, None]) + b * fx[:, None]


def fbm(size, cells, seed):
    out, amp, norm = np.zeros((size, size), np.float32), 1.0, 0.0
    for k, c in enumerate(cells):
        out += amp * noise(size, c, seed + 31 * k)
        norm += amp
        amp *= 0.55
    return out / norm


def roll(a, dx, dy):
    return np.roll(np.roll(a, dy, axis=0), dx, axis=1)


def rgba(rgb, alpha):
    out = np.zeros(rgb.shape[:2] + (4,), np.uint8)
    out[..., :3] = np.clip(np.round(rgb), 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(np.round(alpha * 255), 0, 255).astype(np.uint8)
    return out


def bayer4():
    m = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32)
    return (m + 0.5) / 16.0 - 0.5


def dither(t, steps):
    """Quantise t in [0, 1] to `steps` levels with an ordered dither (pixel-art shading)."""
    h, w = t.shape
    b = np.tile(bayer4(), (h // 4 + 1, w // 4 + 1))[:h, :w]
    return np.clip(np.round(t * (steps - 1) + b * 0.9) / (steps - 1), 0, 1)


# --------------------------------------------------------------------------------------- magma
def magma_body(k):
    """Molten rock: two layers of noise drifting apart (one tile per loop), veins of bright flow."""
    a = fbm(F, [16, 8, 4], 7001)
    b = fbm(F, [16, 8], 7101)
    t = 0.55 * roll(a, 4 * k, 0) + 0.45 * roll(b, -2 * k, 4 * k)
    veins = 1.0 - np.abs(roll(fbm(F, [16, 8], 7201), 4 * k, 2 * k) - 0.5) * 2.0
    heat = 0.28 + 0.5 * t + 0.32 * np.clip(veins - 0.55, 0, 1) / 0.45
    pulse = 0.04 * math.sin(2 * math.pi * k / FRAMES)
    return rgba(ramp(dither(np.clip(heat + pulse, 0, 1), 9)), np.ones((F, F), np.float32))


def magma_crust(k):
    """Dark plates of crust, their edges glowing, drifting slowly across the body."""
    n = roll(0.6 * fbm(F, [16, 8, 4], 7301) + 0.4 * fbm(F, [8, 4, 2], 7351), 4 * k, 0)
    plate = n > 0.67
    edge = (n > 0.64) & ~plate
    shade = dither(np.clip((n - 0.67) / 0.2, 0, 1), 4)
    rgb = np.zeros((F, F, 3), np.float32)
    rgb[...] = np.array([44, 24, 20], np.float32) + shade[..., None] * np.array([40, 30, 26], np.float32)
    rgb[edge] = np.array([214, 92, 24], np.float32)
    alpha = np.where(plate, 0.85, np.where(edge, 0.5, 0.0)).astype(np.float32)
    return rgba(rgb, alpha)


def magma_surface(k):
    """The surface: a bright crest that waves (row 12 on average), the heat fading down into the body."""
    out = np.zeros((F, F, 4), np.uint8)
    xs = np.arange(F)
    crest = 12 + 2.2 * np.sin(2 * math.pi * (xs / F - k / FRAMES)) + 1.0 * np.sin(2 * math.pi * (2 * xs / F + 2 * k / FRAMES) + 1.3)
    for x in xs:
        c = int(round(crest[x]))
        for y in range(F):
            d = y - c
            if d < -1:
                continue
            if d <= 0:
                rgb, a = (255, 246, 210), 1.0
            elif d <= 2:
                rgb, a = (255, 214, 104), 1.0
            else:
                t = min(1.0, (d - 2) / 14.0)
                rgb = tuple(int(round(v)) for v in ramp(np.array(0.85 - 0.45 * t)))
                a = max(0.0, 1.0 - t)
            out[y, x] = (*rgb, int(round(a * 255)))
    return out


def disc(size, cx, cy, r, soft=1.0):
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    d = np.sqrt((xs + 0.5 - cx) ** 2 + (ys + 0.5 - cy) ** 2)
    return np.clip((r - d) / soft + 0.5, 0, 1)


def magma_bubble(k):
    """A bubble rises out of the surface, swells, pops into a ring of drops (the last frame empty)."""
    rgb = np.zeros((F, F, 3), np.float32)
    alpha = np.zeros((F, F), np.float32)
    if k <= 4:
        r = 3.0 + 1.6 * k
        cy = 20.0 - 0.8 * k
        body = disc(F, 16, cy, r)
        rim = np.clip(body - disc(F, 16, cy, r - 1.4), 0, 1)
        shine = disc(F, 16 - r * 0.35, cy - r * 0.4, max(1.0, r * 0.28))
        rgb[...] = ramp(np.full((F, F), 0.62))
        rgb = rgb * (1 - rim[..., None]) + np.array([255, 220, 140], np.float32) * rim[..., None]
        rgb = rgb * (1 - shine[..., None]) + np.array([255, 250, 230], np.float32) * shine[..., None]
        alpha = body
    elif k <= 6:
        s = k - 4
        for i in range(6):
            ang = 2 * math.pi * i / 6 + 0.4
            dist = 6 + 5 * s
            d = disc(F, 16 + math.cos(ang) * dist, 18 - abs(math.sin(ang)) * dist * 0.8, 2.0 - 0.6 * s)
            alpha = np.maximum(alpha, d)
        rgb[...] = np.array([255, 196, 90], np.float32)
    return rgba(rgb, alpha)


def magma_ember(k):
    flick = [1.0, 0.8, 0.95, 0.6, 0.9, 0.7, 1.0, 0.75][k]
    glow = disc(F, 16, 16, 9.0 * flick, soft=6.0) * 0.55
    core = disc(F, 16, 16, 2.4 * flick + 0.6)
    rgb = np.zeros((F, F, 3), np.float32) + np.array([255, 120, 30], np.float32)
    rgb = rgb * (1 - core[..., None]) + np.array([255, 240, 200], np.float32) * core[..., None]
    return rgba(rgb, np.maximum(glow, core))


def magma_warn(k):
    """Glowing cracks in the floor (the same pattern, its glow pulsing) and heat rising from them."""
    r = np.random.default_rng(7501)
    img = Image.new("L", (F * 4, F * 4), 0)
    d = ImageDraw.Draw(img)
    x = 0.0
    y = F * 4 * 0.78
    pts = [(x, y)]
    while x < F * 4:
        x += r.uniform(10, 22)
        y = min(F * 4 - 8, max(F * 4 * 0.55, y + r.uniform(-14, 14)))
        pts.append((min(x, F * 4), y))
    pts[-1] = (F * 4, pts[0][1])  # it tiles: the crack ends where it began
    d.line(pts, fill=255, width=7)
    for i in range(1, len(pts) - 1, 2):
        px, py = pts[i]
        d.line([(px, py), (px + r.uniform(-12, 12), py - r.uniform(10, 26))], fill=200, width=4)
    crack = np.asarray(img, np.float32).reshape(F, 4, F, 4).mean(axis=(1, 3)) / 255.0
    pulse = 0.55 + 0.45 * math.sin(2 * math.pi * k / FRAMES)
    halo = np.zeros_like(crack)
    for dy in range(1, 6):
        halo = np.maximum(halo, np.roll(crack, -dy, axis=0) * (1 - dy / 6.0) * 0.55)
    heat = np.clip(roll(fbm(F, [8, 4], 7601), 0, -4 * k) - 0.55, 0, 1) * 1.6 * (np.arange(F)[:, None] / F)
    t = np.clip(crack * pulse + halo * pulse + heat * 0.35, 0, 1)
    rgb = ramp(0.45 + 0.55 * t)
    return rgba(rgb, t)


def magma_glow():
    a = disc(F, 16, 16, 15.0, soft=12.0) * 0.9
    rgb = np.zeros((F, F, 3), np.float32) + np.array([255, 214, 150], np.float32)
    return rgba(rgb, a)


def build_magma():
    rows = {"body": [magma_body(k) for k in range(FRAMES)], "crust": [magma_crust(k) for k in range(FRAMES)],
            "surface": [magma_surface(k) for k in range(FRAMES)], "bubble": [magma_bubble(k) for k in range(FRAMES)],
            "ember": [magma_ember(k) for k in range(FRAMES)], "warn": [magma_warn(k) for k in range(FRAMES)], "glow": [magma_glow()]}
    sheet = np.zeros((F * len(MAGMA_ROWS), F * FRAMES, 4), np.uint8)
    clips = {}
    for row, (name, count, fps) in enumerate(MAGMA_ROWS):
        for k, img in enumerate(rows[name]):
            sheet[row * F:(row + 1) * F, k * F:(k + 1) * F] = img
        clips[name] = {"row": row, "count": count, "fps": fps, "loop": True}
    man = {"texture": MAGMA_PNG, "frameWidth": F, "frameHeight": F, "pivot": [0, 0], "clips": clips}
    return sheet, man


# ------------------------------------------------------------------------------------ minimap
def tile_rock(seed=8001):
    n = fbm(TILE, [8, 4], seed)
    t = dither(0.25 + 0.5 * n, 5)
    rgb = np.array([46, 36, 44], np.float32) + t[..., None] * np.array([58, 44, 48], np.float32)
    return rgba(rgb, np.ones((TILE, TILE), np.float32))


def tile_edge():
    """The lit top of the rock (drawn over its first row of tiles)."""
    a = np.zeros((TILE, TILE), np.float32)
    a[0:2] = 1.0
    a[2:4] = 0.55
    rgb = np.zeros((TILE, TILE, 3), np.float32) + np.array([214, 168, 120], np.float32)
    rgb[0] = (255, 214, 150)
    return rgba(rgb, a)


def tile_ledge():
    a = np.zeros((TILE, TILE), np.float32)
    a[0:4] = 1.0
    rgb = np.zeros((TILE, TILE, 3), np.float32) + np.array([150, 120, 96], np.float32)
    rgb[0] = (230, 200, 150)
    rgb[3] = (80, 60, 50)
    for x in range(0, TILE, 4):
        rgb[1:3, x] = (96, 74, 60)
    return rgba(rgb, a)


def tile_lava():
    n = fbm(TILE, [8, 4], 8101)
    return rgba(ramp(dither(0.45 + 0.45 * n, 6)), np.ones((TILE, TILE), np.float32))


def tile_magma():
    """The area the magma floods: hatched in orange (the danger to come), the game dims or lights it."""
    ys, xs = np.mgrid[0:TILE, 0:TILE]
    hatch = ((xs + ys) % 8) < 3
    rgb = np.zeros((TILE, TILE, 3), np.float32) + np.array([255, 96, 30], np.float32)
    rgb[hatch] = (255, 178, 70)
    a = np.where(hatch, 0.95, 0.6).astype(np.float32)
    return rgba(rgb, a)


def tile_back():
    n = fbm(TILE, [8, 4], 8201)
    rgb = np.array([20, 12, 18], np.float32) + dither(n, 3)[..., None] * np.array([12, 8, 10], np.float32)
    rgb[0, :] += 6
    rgb[:, 0] += 6
    return rgba(rgb, np.ones((TILE, TILE), np.float32))


def icon(draw_fn, outline=(10, 8, 18)):
    """An icon drawn ×4 by `draw_fn(d, s)` on a mask of its fill and colours, averaged down, outlined."""
    s = 4
    colour = Image.new("RGBA", (ICON * s, ICON * s), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(colour), s)
    big = np.asarray(colour, np.float32)
    small = big.reshape(ICON, s, ICON, s, 4)
    a = small[..., 3].mean(axis=(1, 3))
    rgb = (small[..., :3] * small[..., 3:4]).sum(axis=(1, 3)) / np.maximum(1e-6, small[..., 3].sum(axis=(1, 3)))[..., None]
    # A dark outline one pixel around what is drawn (readable on any terrain).
    solid = a > 0.35 * 255
    ring = np.zeros_like(solid)
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        ring |= np.roll(np.roll(solid, dy, axis=0), dx, axis=1)
    ring &= ~solid
    out = np.zeros((ICON, ICON, 4), np.float32)
    out[..., :3] = rgb
    out[..., 3] = a
    out[ring, :3] = outline
    out[ring, 3] = 230
    return np.clip(np.round(out), 0, 255).astype(np.uint8)


def P(s, pts):
    return [(x * s, y * s) for x, y in pts]


def i_me(d, s):
    d.polygon(P(s, [(12, 2), (15, 9), (22, 9.5), (16.5, 14), (18.5, 21.5), (12, 17.5), (5.5, 21.5), (7.5, 14), (2, 9.5), (9, 9)]),
              fill=(255, 210, 122, 255))
    d.polygon(P(s, [(12, 6), (13.6, 10.4), (18, 10.8), (14.6, 13.6), (15.8, 18), (12, 15.6)]), fill=(255, 244, 200, 255))


def i_ally(d, s):
    d.ellipse(P(s, [(5, 5), (19, 19)]), fill=(236, 240, 255, 255))
    d.ellipse(P(s, [(9, 7.5), (15, 13.5)]), fill=(255, 255, 255, 255))
    d.rectangle(P(s, [(8, 14), (16, 16)]), fill=(196, 204, 226, 255))


def i_enemy(d, s):
    d.polygon(P(s, [(12, 3), (20, 9), (18, 20), (6, 20), (4, 9)]), fill=(255, 92, 92, 255))
    d.polygon(P(s, [(7, 10), (11, 12), (8, 14)]), fill=(40, 6, 10, 255))
    d.polygon(P(s, [(17, 10), (13, 12), (16, 14)]), fill=(40, 6, 10, 255))
    d.rectangle(P(s, [(9, 16), (15, 17.5)]), fill=(40, 6, 10, 255))


def i_boss(d, s):
    d.polygon(P(s, [(3, 19), (3, 8), (8, 12), (12, 4), (16, 12), (21, 8), (21, 19)]), fill=(255, 64, 96, 255))
    d.rectangle(P(s, [(3, 17), (21, 20)]), fill=(255, 196, 120, 255))
    d.ellipse(P(s, [(10.5, 12), (13.5, 15)]), fill=(255, 240, 200, 255))


def i_objective(colour):
    def f(d, s):
        d.polygon(P(s, [(12, 2), (21, 12), (12, 22), (3, 12)]), fill=colour)
        d.polygon(P(s, [(12, 6), (17, 12), (12, 12), (7, 12)]), fill=(255, 255, 255, 110))
    return f


def i_done(d, s):
    d.ellipse(P(s, [(3, 3), (21, 21)]), fill=(124, 240, 168, 255))
    d.line(P(s, [(7, 12.5), (10.5, 16), (17, 8)]), fill=(10, 40, 24, 255), width=3 * s)


def i_exit(d, s):
    d.rectangle(P(s, [(5, 6), (19, 21)]), fill=(159, 216, 255, 255))
    d.ellipse(P(s, [(5, 2), (19, 12)]), fill=(159, 216, 255, 255))
    d.rectangle(P(s, [(8, 9), (16, 21)]), fill=(24, 40, 72, 255))
    d.polygon(P(s, [(9, 15), (13, 11.5), (13, 13.5), (16, 13.5), (16, 16.5), (13, 16.5), (13, 18.5)]), fill=(255, 255, 255, 255))


def i_exit_locked(d, s):
    d.rectangle(P(s, [(5, 6), (19, 21)]), fill=(120, 106, 120, 255))
    d.ellipse(P(s, [(5, 2), (19, 12)]), fill=(120, 106, 120, 255))
    d.rectangle(P(s, [(8, 9), (16, 21)]), fill=(30, 24, 34, 255))
    d.arc(P(s, [(9, 9), (15, 16)]), 180, 360, fill=(255, 210, 122, 255), width=2 * s)
    d.rectangle(P(s, [(8, 13), (16, 20)]), fill=(255, 210, 122, 255))
    d.rectangle(P(s, [(11.4, 15), (12.6, 18)]), fill=(60, 40, 10, 255))


def i_door(d, s):
    d.rectangle(P(s, [(8, 2), (16, 22)]), fill=(255, 179, 71, 255))
    for y in (6, 11, 16):
        d.rectangle(P(s, [(8, y), (16, y + 1)]), fill=(120, 70, 20, 255))
    d.ellipse(P(s, [(13, 11), (15, 13)]), fill=(255, 240, 200, 255))


def i_checkpoint(lit):
    def f(d, s):
        d.rectangle(P(s, [(11, 4), (13, 21)]), fill=(150, 160, 180, 255))
        d.rectangle(P(s, [(7, 20), (17, 22)]), fill=(150, 160, 180, 255))
        d.polygon(P(s, [(13, 4), (21, 7.5), (13, 11)]), fill=(57, 240, 216, 255) if lit else (90, 110, 130, 255))
        if lit:
            d.ellipse(P(s, [(15, 5.5), (18, 8.5)]), fill=(220, 255, 250, 255))
    return f


def i_plate(done):
    def f(d, s):
        c = (124, 240, 168, 255) if done else (255, 210, 122, 255)
        d.rectangle(P(s, [(2, 16), (22, 21)]), fill=c)
        for x in (6, 12, 18):
            d.ellipse(P(s, [(x - 2.5, 5), (x + 2.5, 10)]), fill=(236, 240, 255, 255))
            d.rectangle(P(s, [(x - 2, 10.5), (x + 2, 15)]), fill=(236, 240, 255, 255))
    return f


def i_vent(closed):
    def f(d, s):
        base = (124, 240, 168, 255) if closed else (255, 176, 74, 255)
        d.polygon(P(s, [(3, 21), (6, 11), (18, 11), (21, 21)]), fill=(90, 70, 70, 255))
        d.ellipse(P(s, [(6, 8), (18, 14)]), fill=base)
        if not closed:
            d.polygon(P(s, [(9, 9), (12, 1), (15, 9)]), fill=(255, 120, 40, 255))
    return f


def i_magma(d, s):
    d.polygon(P(s, [(12, 2), (18, 11), (19, 16), (16, 21), (8, 21), (5, 16), (6, 11)]), fill=(255, 106, 30, 255))
    d.polygon(P(s, [(12, 9), (15, 14), (14, 19), (10, 19), (9, 14)]), fill=(255, 220, 120, 255))


ICONS = [("me", i_me), ("ally", i_ally), ("enemy", i_enemy), ("boss", i_boss), ("objective", i_objective((255, 176, 74, 255))),
         ("objective_done", i_done), ("objective_optional", i_objective((201, 168, 255, 255))), ("exit", i_exit),
         ("exit_locked", i_exit_locked), ("door", i_door), ("checkpoint", i_checkpoint(False)), ("checkpoint_on", i_checkpoint(True)),
         ("plate", i_plate(False)), ("plate_done", i_plate(True)), ("vent", i_vent(False)), ("vent_closed", i_vent(True)),
         ("magma", i_magma)]
TILES = [("rock", tile_rock), ("rock_edge", tile_edge), ("ledge", tile_ledge), ("lava", tile_lava), ("magma_zone", tile_magma),
         ("back", tile_back)]


def build_map():
    per_row = 8
    rows = 1 + (len(ICONS) + per_row - 1) // per_row
    sheet = np.zeros((TILE + ICON * (rows - 1), ICON * per_row, 4), np.uint8)
    cells = {}
    for i, (name, fn) in enumerate(TILES):
        sheet[0:TILE, i * TILE:(i + 1) * TILE] = fn()
        cells[name] = [i * TILE, 0, TILE, TILE]
    for i, (name, fn) in enumerate(ICONS):
        x, y = (i % per_row) * ICON, TILE + (i // per_row) * ICON
        sheet[y:y + ICON, x:x + ICON] = icon(fn)
        cells[name] = [x, y, ICON, ICON]
    man = {"texture": MAP_PNG, "tile": TILE, "icon": ICON, "cells": cells}
    return sheet, man


# ------------------------------------------------------------------------------------- output
def encode(sheet, man):
    buf = io.BytesIO()
    Image.fromarray(sheet, "RGBA").save(buf, format="PNG", optimize=True)
    js = (json.dumps(man, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    return buf.getvalue(), js


def outputs():
    m, mm = build_magma()
    t, tm = build_map()
    return [(MAGMA_PNG, MAGMA_JSON, m, mm), (MAP_PNG, MAP_JSON, t, tm)]


def generate():
    written = []
    for png, js, sheet, man in outputs():
        p, j = encode(sheet, man)
        for rel, data in ((png, p), (js, j)):
            path = os.path.join(ASSETS, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            written.append(rel)
    return written


def check():
    errs = []
    first, second = outputs(), outputs()
    for (png, js, sheet, man), (_, _, sheet2, man2) in zip(first, second):
        a, b = encode(sheet, man), encode(sheet2, man2)
        if a != b:
            errs.append(f"{png}: two renders differ (not deterministic)")
        jp, pp = os.path.join(ASSETS, js), os.path.join(ASSETS, png)
        if not (os.path.exists(jp) and os.path.exists(pp)):
            errs.append(f"{png} / {js} missing (python3 tools/assetgen/environment/raid_visuals.py)")
            continue
        with open(jp, "rb") as f:
            if f.read() != a[1]:
                errs.append(f"{js}: not the generator's output (regenerate)")
        disk = np.array(Image.open(pp).convert("RGBA"))
        if disk.shape != sheet.shape or (disk != sheet).any():
            errs.append(f"{png}: not the generator's output (regenerate)")
    return errs


# ------------------------------------------------------------------------------------- review
def review(path):
    """A contact sheet, labelled in French: each clip of the magma (×4), a pool rising, the tiles and
    the icons of the minimap (×3) with a small map made of them."""
    (_, _, msheet, mman), (_, _, tsheet, tman) = outputs()
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-SemiBold.ttf"), 18)
    small = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-SemiBold.ttf"), 14)
    W = 1500
    out = Image.new("RGBA", (W, 2200), (14, 10, 22, 255))
    d = ImageDraw.Draw(out)
    labels = {"body": "Corps de lave (qui coule)", "crust": "Croûte sombre qui dérive", "surface": "Vagues de surface",
              "bubble": "Bulle qui éclate", "ember": "Braise", "warn": "Alerte avant la montée (fissures)", "glow": "Lueur au-dessus d'un corps caché"}
    d.text((20, 14), "Raids · magma montant et textures de la minimap (tools/assetgen/environment/raid_visuals.py)", font=font,
           fill=(255, 210, 122, 255))
    y = 50
    S = 4
    for name, clip in mman["clips"].items():
        d.text((20, y + 4), labels[name], font=small, fill=(238, 240, 255, 255))
        for k in range(clip["count"]):
            fr = msheet[clip["row"] * F:(clip["row"] + 1) * F, k * F:(k + 1) * F]
            bg = Image.new("RGBA", (F, F), (60, 52, 70, 255))
            bg.alpha_composite(Image.fromarray(fr, "RGBA"))
            out.paste(bg.resize((F * S // 2, F * S // 2), Image.NEAREST), (300 + k * (F * S // 2 + 6), y))
        y += F * S // 2 + 10
    # A pool rising: the body, its crust and its surface tiled over a 6 × 3 area, three levels.
    d.text((20, y + 6), "Une montée : 0 %, 50 %, 100 % (tuiles assemblées, ×2)", font=small, fill=(238, 240, 255, 255))
    y += 30
    rows = {n: c["row"] for n, c in mman["clips"].items()}

    def frame(name, k):
        r = rows[name]
        return Image.fromarray(msheet[r * F:(r + 1) * F, k * F:(k + 1) * F], "RGBA")
    for i, level in enumerate((0.0, 0.5, 1.0)):
        pool = Image.new("RGBA", (F * 6, F * 3), (48, 40, 50, 255))
        pd = ImageDraw.Draw(pool)
        pd.rectangle([0, F * 3 - 8, F * 6, F * 3], fill=(70, 56, 60, 255))
        for c in range(6):
            pool.alpha_composite(frame("warn", c % 8), (c * F, F * 2))
        top = int(F * 3 * (1 - level))
        for c in range(6):
            for r in range(3):
                if r * F + F <= top:
                    continue
                tile = frame("body", (c + r) % 8).copy()
                tile.alpha_composite(frame("crust", (c * 3 + r) % 8))
                pool.paste(tile.crop((0, max(0, top - r * F), F, F)), (c * F, max(top, r * F)))
            if level > 0:
                pool.alpha_composite(frame("surface", c % 8), (c * F, top - 12))
        if level > 0:
            pool.alpha_composite(frame("bubble", 3), (F * 2, top - 20))
            pool.alpha_composite(frame("glow", 0), (F * 4, top - 16))
        out.paste(pool.resize((F * 12, F * 6), Image.NEAREST), (20 + i * (F * 12 + 20), y))
    y += F * 6 + 24
    d.text((20, y), "Minimap : tuiles (×3) et icônes (×3)", font=font, fill=(255, 210, 122, 255))
    y += 32
    tile_names = {"rock": "Roche", "rock_edge": "Bord éclairé", "ledge": "Corniche", "lava": "Lave", "magma_zone": "Zone de magma",
                  "back": "Fond", "me": "Vous", "ally": "Allié (teinté)", "enemy": "Ennemi", "boss": "Boss", "objective": "Objectif",
                  "objective_done": "Objectif fait", "objective_optional": "Facultatif", "exit": "Passage", "exit_locked": "Passage fermé",
                  "door": "Porte", "checkpoint": "Point de contrôle", "checkpoint_on": "Point atteint", "plate": "Plaque de groupe",
                  "plate_done": "Plaque faite", "vent": "Évent", "vent_closed": "Évent fermé", "magma": "Magma"}
    x = 20
    for name, (cx, cy, cw, ch) in tman["cells"].items():
        im = Image.fromarray(tsheet[cy:cy + ch, cx:cx + cw], "RGBA")
        bg = Image.new("RGBA", (cw, ch), (60, 52, 70, 255))
        bg.alpha_composite(im)
        big = bg.resize((cw * 3, ch * 3), Image.NEAREST)
        if x + 120 > W:
            x = 20
            y += 118
        out.paste(big, (x + (110 - cw * 3) // 2, y))
        label = tile_names.get(name, name)
        while d.textlength(label, font=small) > 116 and len(label) > 3:
            label = label[:-2] + "…"
        d.text((x + (110 - d.textlength(label, font=small)) / 2, y + 78), label, font=small, fill=(238, 240, 255, 255))
        x += 124
    y += 118
    # A small map drawn with them (a zone, its floors, the magma, the group and the marks).
    d.text((20, y), "Exemple de carte composée (illustration du générateur)", font=small, fill=(238, 240, 255, 255))
    y += 24
    cell = {n: Image.fromarray(tsheet[c[1]:c[1] + c[3], c[0]:c[0] + c[2]], "RGBA") for n, c in tman["cells"].items()}
    mp = Image.new("RGBA", (TILE * 40, TILE * 10), (0, 0, 0, 255))
    for cx in range(40):
        for cy in range(10):
            mp.alpha_composite(cell["back"], (cx * TILE, cy * TILE))
    floor = [8] * 12 + [9] * 10 + [6] * 8 + [4] * 10
    for cx, top in enumerate(floor):
        for cy in range(top, 10):
            mp.alpha_composite(cell["rock"], (cx * TILE, cy * TILE))
        mp.alpha_composite(cell["rock_edge"], (cx * TILE, top * TILE))
    for cx in range(12, 22):
        mp.alpha_composite(cell["magma_zone"], (cx * TILE, 7 * TILE))
        mp.alpha_composite(cell["magma_zone"], (cx * TILE, 8 * TILE))
    for cx in range(14, 18):
        mp.alpha_composite(cell["ledge"], (cx * TILE, 5 * TILE))
    for name, px, py in (("exit", 1, 6), ("door", 29, 3), ("exit_locked", 38, 2), ("objective", 25, 4), ("objective_done", 33, 2),
                         ("checkpoint_on", 6, 6), ("plate", 9, 6), ("enemy", 19, 6), ("enemy", 27, 4), ("boss", 35, 2), ("ally", 4, 6),
                         ("me", 15, 3)):
        mp.alpha_composite(cell[name], (px * TILE - 4, py * TILE - 8))
    out.paste(mp.resize((TILE * 80, TILE * 20), Image.NEAREST), (20, y))
    y += TILE * 20 + 20
    out = out.crop((0, 0, W, y))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    out.convert("RGB").save(path)
    return path


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        problems = check()
        for p in problems:
            print(p)
        sys.exit(1 if problems else 0)
    elif len(sys.argv) > 2 and sys.argv[1] == "--review":
        print(review(sys.argv[2]))
    else:
        for rel in generate():
            print("wrote assets/" + rel)
