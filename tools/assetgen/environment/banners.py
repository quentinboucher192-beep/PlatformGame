"""Planet info-panel banners (480x270) for Pyralis and Glacior, composed exactly like
planets.banner() does for Sylvanox: offset crops of the planet's parallax layers, a foreground
ledge built from one of its tilesets (autotiled), a few decor props and the top of the
foreground layer.

  ui/planets/pyralis_banner.png, ui/planets/glacior_banner.png
"""
import json
import os

import numpy as np

from helpers import ASSETS, add_glow, load_png, new, paste, save_png

W, H = 480, 270

PLANETS = {
    "pyralis": {
        "bg": "#0a0406",
        # (layer, x offset, y offset, visible x-range)
        "layers": [("sky", 96, 26, None), ("far", 560, 34, None), ("mid", 196, -40, None),
                   ("abyss", 0, 64, None), ("near", 142, 92, (301, W))],
        "material": "basalt",
        "grid": ["##########....",
                 "###########...",
                 "############..",
                 "#############."],
        "top_decor": [(1, 0, 6), (3, 0, 1), (6, 0, 2), (8, 0, 0), (9, 1, 4), (11, 2, 6),
                      (12, 3, 1)],
        "decor": [("basalt_column", 70, 0), ("forge_brazier", 28, 0), ("forge_anvil", 120, 16),
                  ("ember_crystal", 104, 0), ("fire_plant", 150, 32)],
        "fg": ("fg_embers", 210, 52, -6),
    },
    "glacior": {
        "bg": "#03060f",
        "layers": [("sky", 190, 18, None), ("far", 120, 40, None), ("mid", 200, -44, None),
                   ("abyss", 0, 64, None), ("near", 400, 92, (318, W))],
        "material": "snow_rock",
        "grid": ["#########.....",
                 "##########....",
                 "############..",
                 "#############."],
        "top_decor": [(1, 0, 0), (3, 0, 2), (6, 0, 6), (8, 0, 0), (9, 1, 1), (11, 2, 5),
                      (12, 3, 0)],
        "decor": [("snow_pine", 26, 0), ("ice_crystal_big", 82, 0), ("aurora_lamp", 120, 16),
                  ("frozen_statue", 150, 32), ("frost_flower", 104, 0)],
        "fg": ("fg_icicles", 40, 36, -4),
    },
}


def banner(planet):
    cfg = PLANETS[planet]
    img = new(W, H, cfg["bg"])
    for name, ox, oy, xr in cfg["layers"]:
        lay = load_png(f"backgrounds/{planet}/{name}.png")
        tiled = np.concatenate([lay, lay], axis=1)[:, ox:ox + W]
        crop = new(W, H)
        src0, dst0 = max(0, oy), max(0, -oy)
        n = min(H - dst0, tiled.shape[0] - src0)
        crop[dst0:dst0 + n] = tiled[src0:src0 + n]
        if dst0 + n < H:       # extend the bottom row (opaque ground / mist) downwards
            crop[dst0 + n:] = crop[dst0 + n - 1]
        if xr is not None:
            crop[:, :xr[0]] = 0
            crop[:, xr[1]:] = 0
        paste(img, crop, 0, 0)
    # foreground ledge (bottom-left) from the planet's tileset, autotiled like the engine
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{cfg['material']}.json")))
    atlas = load_png(meta["texture"])
    grid = cfg["grid"]
    gy0 = H - 16 * len(grid) + 6
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
    # decor props (+ their glow, baked softly like the engine's additive lights)
    dmeta = json.load(open(os.path.join(ASSETS, f"sprites/decor/{planet}_decor.json")))
    datlas = load_png(dmeta["texture"])
    glow = new(W, H)
    for name, px, dy in cfg["decor"]:
        e = dmeta["sprites"][name]
        x, y, w, h = e["rect"]
        py = gy0 + dy
        paste(img, datlas[y:y + h, x:x + w], px - e["pivot"][0], py - e["pivot"][1])
        if "glow" in e:
            add_glow(glow, px + e["glowOffset"][0], py + e["glowOffset"][1], e["glowRadius"],
                     e["glow"], strength=0.3)
    g = glow[..., :3].astype(np.int32)
    img[..., :3] = np.clip(img[..., :3].astype(np.int32) + g, 0, 255).astype(np.uint8)
    # foreground canopy along the top
    fg_name, fx, fh, fy = cfg["fg"]
    fg = load_png(f"backgrounds/{planet}/{fg_name}.png")
    canopy = np.concatenate([fg, fg], 1)[0:fh, fx:fx + W].copy()
    paste(img, canopy, 0, fy)
    img[..., 3] = 255
    return img


def generate():
    return [save_png(banner(p), f"ui/planets/{p}_banner.png") for p in ("pyralis", "glacior")]
