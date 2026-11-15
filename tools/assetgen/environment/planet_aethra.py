"""Planet-map art of Aethra (2.19), drawn like the other planets (planets.py, banners.py):

  ui/planets/aethra.png         256x256, transparent: a violet world wrapped in nebula, its
                                golden sanctuary islands lit along the terminator, girdled by the
                                golden Halo ring with the four elemental lights
  ui/planets/aethra_banner.png  480x270 vista for the planet panel: offset crops of the Aethra
                                parallax layers, a foreground ledge of the sanctum_stone tileset
                                (autotiled like the engine), its top decorations and the petals
"""
import json
import math
import os

import numpy as np

from helpers import (ASSETS, Noise3, bayer, dither_alpha, hex_arr, load_png, new, paste, put, save_png,
                     smoothstep)
from planets import C, R, S, finish, halo, lit_dir, rim_light, rotate, sphere

RING_TILT = -0.38


def _ring_pts(rx, ry, n=900):
    c, s = math.cos(RING_TILT), math.sin(RING_TILT)
    for k in range(n):
        a = 2 * math.pi * k / n
        x, y = math.cos(a) * rx, math.sin(a) * ry
        yield a, C + x * c - y * s, C + x * s + y * c


def planet():
    dx, dy, nz, d2, inside, lam = sphere(r=R * 0.86)
    x, y, z = rotate(dx, dy, nz, 0.2, 1.4)
    neb = Noise3(2601).fbm(x * 2.0 + 3, y * 3.0, z * 2.0, 5)
    swirl = Noise3(2602).fbm(x * 4 + neb * 2, y * 4, z * 4, 4)
    isl = Noise3(2603).fbm(x * 5, y * 5, z * 5, 4)
    deep, mid, high = hex_arr(["#24123e"])[0], hex_arr(["#5a3488"])[0], hex_arr(["#b07ad8"])[0]
    rose = hex_arr(["#e080b8"])[0]
    base = deep + (mid - deep) * smoothstep(0.3, 0.6, neb)[..., None]
    bands = smoothstep(0.55, 0.75, swirl)
    base = base + (high - base) * bands[..., None] * 0.7
    base = base + (rose - base) * (smoothstep(0.68, 0.82, neb) * 0.5)[..., None]
    rgb = base * (0.16 + 0.84 * lam ** 0.8)[..., None]
    # golden sanctuary islands (small, bright), glowing on the night side too
    islands = smoothstep(0.71, 0.73, isl)
    cores = smoothstep(0.76, 0.78, isl)
    gold = hex_arr(["#e0a848"])[0]
    lit_gold = hex_arr(["#fff0c0"])[0]
    night = 1 - smoothstep(0.0, 0.45, lam)
    g = islands * (0.75 + 0.25 * night)
    rgb = rgb * (1 - g[..., None]) + gold * g[..., None]
    rgb = rgb * (1 - cores[..., None]) + lit_gold * cores[..., None]
    rgb = rim_light(rgb, d2, inside, "#c08cf0", 0.4)
    pal = ["#06030c", "#100820", "#1a0e34", "#2a1650", "#3a2066", "#4a2a7a", "#6a3e9a", "#9a64c8",
           "#c08cf0", "#a8407a", "#d86aa8", "#6a4220", "#a8702a", "#e0a848", "#ffd27a", "#fff0c0"]
    img = finish(rgb, inside, pal)
    img = halo(img, d2, lit_dir(dx, dy), "#c08cf0", width=8, strength=0.85, r=R * 0.86)
    # the Halo ring: back half behind the planet, front half in front
    rx, ry = R * 1.22, R * 0.34
    front = new(S, S)
    for a, px, py in _ring_pts(rx, ry):
        is_front = math.sin(a) > 0
        tgt = front if is_front else None
        for off, colr in ((0.0, "#ffd27a"), (1.0, "#a8702a")):
            qx, qy = int(px), int(py + off)
            if not (0 <= qx < S and 0 <= qy < S):
                continue
            if is_front:
                put(tgt, qx, qy, colr if off else ("#fff0c0" if math.cos(a) < -0.3 else colr))
            elif img[qy, qx, 3] == 0 or (img[qy, qx, 3] < 255 and off == 0):
                put(img, qx, qy, colr)
    paste(img, front, 0, 0)
    # four elemental lights on the ring
    for a, c in ((2.7, "#7dffd6"), (0.55, "#9fe8ff"), (1.9, "#ffb13b"), (3.9, "#ff7696")):
        cxr, cyr = math.cos(a) * rx, math.sin(a) * ry
        ct, st = math.cos(RING_TILT), math.sin(RING_TILT)
        px, py = C + cxr * ct - cyr * st, C + cxr * st + cyr * ct
        for ddx in range(-2, 3):
            for ddy in range(-2, 3):
                d = math.hypot(ddx, ddy)
                if d <= 0.8:
                    put(img, int(px) + ddx, int(py) + ddy, "#ffffff")
                elif d <= 1.8:
                    put(img, int(px) + ddx, int(py) + ddy, c)
    return img


# ---------------------------------------------------------------------------
BW, BH = 480, 270
BANNER = {
    "bg": "#0a0618",
    "layers": [("sky", 260, 0, None), ("far", 200, 30, None), ("mid", 40, -20, None), ("abyss", 0, 70, None),
               ("near", 300, 80, (300, BW))],
    "material": "sanctum_stone",
    "grid": ["########......",
             "##########....",
             "############..",
             "#############."],
    "top_decor": [(1, 0, 1), (3, 0, 2), (5, 0, 6), (7, 0, 3), (8, 1, 4), (10, 2, 5), (12, 3, 7)],
    "fg": ("fg_petals", 120, 60, 0),
}


def banner():
    cfg = BANNER
    img = new(BW, BH, cfg["bg"])
    for name, ox, oy, xr in cfg["layers"]:
        lay = load_png(f"backgrounds/aethra/{name}.png")
        tiled = np.concatenate([lay, lay], axis=1)[:, ox:ox + BW]
        crop = new(BW, BH)
        src0, dst0 = max(0, oy), max(0, -oy)
        n = min(BH - dst0, tiled.shape[0] - src0)
        crop[dst0:dst0 + n] = tiled[src0:src0 + n]
        if dst0 + n < BH:
            crop[dst0 + n:] = crop[dst0 + n - 1]
        if xr is not None:
            crop[:, :xr[0]] = 0
            crop[:, xr[1]:] = 0
        paste(img, crop, 0, 0)
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{cfg['material']}.json")))
    atlas = load_png(meta["texture"])
    grid = cfg["grid"]
    gy0 = BH - 16 * len(grid) + 6
    rows, cols = len(grid), len(grid[0])

    def solid(i, j):
        if j >= rows:
            return 0 <= i < cols and grid[rows - 1][i] == "#"
        return 0 <= j < rows and 0 <= i < cols and grid[j][i] == "#"

    for j in range(rows):
        for i in range(cols):
            if not solid(i, j):
                continue
            m = solid(i, j - 1) | (solid(i + 1, j) << 1) | (solid(i, j + 1) << 2) | (
                (solid(i - 1, j) or i == 0) << 3)
            if m == 15:
                v = (i * 7 + j * 3) % 16
                cell = atlas[16:32, v * 16:(v + 1) * 16]
            else:
                cell = atlas[0:16, m * 16:(m + 1) * 16]
            paste(img, cell, i * 16, gy0 + j * 16)
            if m == 15:
                for k, (di, dj) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
                    if not solid(i + di, j + dj) and not (i + di < 0):
                        paste(img, atlas[32:48, k * 16:(k + 1) * 16], i * 16, gy0 + j * 16)
    for (i, j, d) in cfg["top_decor"]:
        if solid(i, j) and not solid(i, j - 1):
            paste(img, atlas[48:64, d * 16:(d + 1) * 16], i * 16, gy0 + j * 16 - 12)
    fg_name, fx, fh, fy = cfg["fg"]
    fg = load_png(f"backgrounds/aethra/{fg_name}.png")
    canopy = np.concatenate([fg, fg], 1)[0:fh, fx:fx + BW].copy()
    paste(img, canopy, 0, fy)
    img[..., 3] = 255
    return img


def generate():
    return [save_png(planet(), "ui/planets/aethra.png"), save_png(banner(), "ui/planets/aethra_banner.png")]


if __name__ == "__main__":
    for p in generate():
        print(p)
