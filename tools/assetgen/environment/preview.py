"""Self-review compositions (not shipped). Renders assets the way the engine will use them,
reading only the files in assets/ + their JSON (so it also exercises the contract).

Output: /tmp/claude-0/assetreview/environment/*.png
"""
import json
import os

import numpy as np

from helpers import ASSETS, new, paste, save_review, load_png, add_glow

SAMPLE_MAP = [
    "..............................................",
    "..............................................",
    "..................######......................",
    "......####.......########.........#...........",
    ".....######.......##..##..........#....####...",
    "....########...........#..........#...######..",
    "....###..###...........#.....########.######..",
    "....###..###.....###...#.....########..####...",
    "....########.....###.........##....##.........",
    ".....##..##......###.........##....##.........",
    "..........................#####....######.....",
    ".#######......########...######################",
    ".###########..########...######################",
    ".#############.#######...###...#####..#########",
    ".###...########.#####....###...#####..##..#####",
    ".###...#########.#######.#########......#######",
    ".##############################################",
]


def h2(x, y, s=0):
    v = (x * 73856093) ^ (y * 19349663) ^ (s * 83492791)
    v = (v ^ (v >> 13)) * 1274126177
    return (v ^ (v >> 16)) & 0x7FFFFFFF


def render_terrain(material, grid=SAMPLE_MAP, decor_rate=0.6, bg="#121a33", margin=16):
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{material}.json")))
    atlas = load_png(meta["texture"])
    ts = meta["tileSize"]
    H, W = len(grid), len(grid[0])
    img = new(W * ts, H * ts + margin, bg)

    def solid(i, j):
        return 0 <= j < H and 0 <= i < W and grid[j][i] == "#"

    def cell(row, c):
        return atlas[row * ts:(row + 1) * ts, c * ts:(c + 1) * ts]

    for j in range(H):
        for i in range(W):
            if not solid(i, j):
                continue
            m = (solid(i, j - 1) << 0) | (solid(i + 1, j) << 1) | (solid(i, j + 1) << 2) | (solid(i - 1, j) << 3)
            x, y = i * ts, j * ts + margin
            if m == 15:
                paste(img, cell(meta["variantRow"], h2(i, j) % meta["variantCount"]), x, y)
                for k, (di, dj) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
                    if not solid(i + di, j + dj):
                        paste(img, cell(meta["innerCornerRow"], k), x, y)
            else:
                paste(img, cell(meta["maskRow"], m), x, y)
    # top decor pass
    for j in range(H):
        for i in range(W):
            if solid(i, j) and not solid(i, j - 1):
                if (h2(i, j, 7) % 1000) / 1000.0 < decor_rate:
                    d = h2(i, j, 3) % meta["topDecorCount"]
                    paste(img, cell(meta["topDecorRow"], d), i * ts, j * ts + margin + meta["topDecorOffsetY"])
    return img


def atlas_sheet(material):
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{material}.json")))
    return load_png(meta["texture"])


def review_tiles(materials):
    paths = []
    for m in materials:
        img = render_terrain(m)
        paths.append(save_review(img, f"terrain_{m}.png", 3))
        paths.append(save_review(atlas_sheet(m), f"atlas_{m}.png", 4, bg="#3a2a4a"))
    return paths



def review_platforms():
    meta = json.load(open(os.path.join(ASSETS, "textures/tiles/platforms.json")))
    atlas = load_png(meta["texture"])
    img = new(16 * 22, 16 * 7, "#121a33")
    for name, row in meta["styles"].items():
        for k, n in enumerate((1, 2, 5)):
            x0 = 8 + k * 112
            y0 = 8 + row * 32
            cells = [0] + [1] * n + [2]
            for i, c in enumerate(cells):
                paste(img, atlas[row * 16:(row + 1) * 16, c * 16:(c + 1) * 16], x0 + i * 16, y0)
    return [save_review(img, "platforms.png", 4), save_review(atlas, "atlas_platforms.png", 6, bg="#3a2a4a")]


BG_ORDER = [("sky", 0.0), ("far", 0.12), ("mid", 0.3), ("abyss", 0.5), ("near", 0.55)]


def compose_backgrounds(cam_x, w=640, h=360, with_fg=True):
    img = new(w, h, "#000000")
    for name, p in BG_ORDER + ([("fg_vines", 1.2)] if with_fg else []):
        layer = load_png(f"backgrounds/sylvanox/{name}.png")
        off = int(round(-cam_x * p)) % layer.shape[1]
        tiled = np.concatenate([layer, layer, layer], axis=1)
        paste(img, tiled[:, off:off + w], 0, 0)
    return img


def review_backgrounds():
    paths = []
    names = [n for n, _ in BG_ORDER] + ["fg_vines"]
    # every layer tiled twice side by side (seam check), on a checker-free dark bg
    for n in names:
        layer = load_png(f"backgrounds/sylvanox/{n}.png")
        two = np.concatenate([layer, layer], axis=1)
        paths.append(save_review(two, f"bg_seam_{n}.png", 1, bg="#401030"))
    a = compose_backgrounds(0)
    b = compose_backgrounds(700)
    paths.append(save_review(np.concatenate([a, b], axis=0), "bg_stack.png", 2))
    return paths


SCENE_MAP = [
    "..............................",
    "..............................",
    "..............................",
    "..............................",
    "......................######..",
    "......................#######.",
    "........####...........#####..",
    ".......######...........###...",
    "........####..................",
    ".........##...................",
    "..................#...........",
    "###...............#.......####",
    "####..............#......#####",
    "#####...........#####...######",
    "######.........######..#######",
    "######.........######..#######",
    "######.........######..#######",
]
# (sprite, x px, y px of pivot)
SCENE_DECOR = [
    ("mushroom_tall", 18, 176), ("grass_tuft", 44, 176), ("rock_small", 60, 192),
    ("glow_flowers", 74, 208), ("ruin_pillar_broken", 136, 96), ("lantern_pod", 160, 160),
    ("hanging_vine_long", 176, 112), ("crystal_cluster", 272, 208), ("statue_head", 312, 208),
    ("holo_glyph", 294, 160), ("tech_panel", 416, 176), ("mushroom_cluster", 450, 176),
    ("hanging_vine_short", 392, 128), ("lantern_pod", 424, 96), ("glow_fern", 384, 64),
    ("roots", 400, 240), ("floating_rock", 70, 50), ("banner_torn", 444, 64),
    ("bones_ancient", 60, 224), ("mushroom_small", 344, 208),
]
SCENE_PLATFORMS = [("ruin_bridge", 7, 12, 4), ("tech_ledge", 22, 9, 3), ("root_branch", 1, 7, 3)]


def scene(material, cam_x=260):
    w, h = 480, 270
    img = compose_backgrounds(cam_x, w, h, with_fg=False)
    paste(img, _terrain_layer(material), 0, 0)
    pm = json.load(open(os.path.join(ASSETS, "textures/tiles/platforms.json")))
    pa = load_png(pm["texture"])
    for style, tx, ty, n in SCENE_PLATFORMS:
        row = pm["styles"][style]
        cells = [0] + [1] * (n - 2) + [2]
        for i, c in enumerate(cells):
            paste(img, pa[row * 16:(row + 1) * 16, c * 16:(c + 1) * 16], (tx + i) * 16, ty * 16)
    dm = json.load(open(os.path.join(ASSETS, "sprites/decor/sylvanox_decor.json")))
    da = load_png(dm["texture"])
    glow = new(w, h)
    for name, px, py in SCENE_DECOR:
        e = dm["sprites"][name]
        x, y, fw, fh = e["rect"]
        spr = da[y:y + fh, x:x + fw]
        paste(img, spr, px - e["pivot"][0], py - e["pivot"][1])
        if "glow" in e:
            add_glow(glow, px + e["glowOffset"][0], py + e["glowOffset"][1], e["glowRadius"],
                     e["glow"], strength=0.35)
    # simulate the engine's additive glow lights
    g = glow[..., :3].astype(np.int32)
    img[..., :3] = np.clip(img[..., :3].astype(np.int32) + g, 0, 255).astype(np.uint8)
    fg = load_png("backgrounds/sylvanox/fg_vines.png")
    off = int(round(cam_x * 1.2)) % fg.shape[1]
    paste(img, np.concatenate([fg, fg], 1)[0:h, off:off + w], 0, 0)
    return img


def _terrain_layer(material):
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{material}.json")))
    atlas = load_png(meta["texture"])
    grid = SCENE_MAP
    H, W = len(grid), len(grid[0])
    img = new(W * 16, H * 16)

    def solid(i, j):
        if j >= H:
            return 0 <= i < W and grid[H - 1][i] == "#"
        if i < 0 or i >= W:
            return j >= 0 and grid[j][max(0, min(W - 1, i))] == "#" and j > 10
        return j >= 0 and grid[j][i] == "#"

    def cell(row, c):
        return atlas[row * 16:(row + 1) * 16, c * 16:(c + 1) * 16]

    for j in range(H):
        for i in range(W):
            if not solid(i, j):
                continue
            m = solid(i, j - 1) | (solid(i + 1, j) << 1) | (solid(i, j + 1) << 2) | (solid(i - 1, j) << 3)
            if m == 15:
                paste(img, cell(1, h2(i, j) % 16), i * 16, j * 16)
                for k, (di, dj) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
                    if not solid(i + di, j + dj):
                        paste(img, cell(2, k), i * 16, j * 16)
            else:
                paste(img, cell(0, m), i * 16, j * 16)
    for j in range(H):
        for i in range(W):
            if solid(i, j) and not solid(i, j - 1) and h2(i, j, 7) % 10 < 6:
                paste(img, cell(3, h2(i, j, 3) % 8), i * 16, j * 16 - 12)
    return img[:270]


def review_all(names=None):
    paths = []
    mats = ["ruin_stone", "ancient_metal", "crystal", "root"]
    paths += review_tiles(mats)
    paths += review_platforms()
    paths += review_backgrounds()
    scenes = [scene(m, cam_x=200 + 150 * k) for k, m in enumerate(mats)]
    for m, s in zip(mats, scenes):
        paths.append(save_review(s, f"scene_{m}.png", 3))
    da = load_png("sprites/decor/sylvanox_decor.png")
    paths.append(save_review(da, "atlas_decor.png", 4, bg="#2a2140"))
    sheet = new(616, 220, "#1a1f30")
    paste(sheet, load_png("ui/icons_powers.png"), 4, 4)
    paste(sheet, load_png("ui/icons_medals.png"), 172, 4)
    paste(sheet, load_png("ui/icons_hud.png"), 340, 12)
    for i, n in enumerate(["xbox", "playstation", "generic"]):
        paste(sheet, load_png(f"ui/glyphs/{n}.png"), 4, 44 + i * 36)
    paths.append(save_review(sheet, "ui_sheet.png", 3))
    pl = new(1040, 264, "#070a16")
    for i, n in enumerate(["sylvanox", "pyralis", "glacior", "sun"]):
        paste(pl, load_png(f"ui/planets/{n}.png"), 4 + i * 260, 4)
    paths.append(save_review(pl, "planets.png", 1))
    paths.append(save_review(load_png("ui/planets/sylvanox_banner.png"), "banner.png", 2))
    paths.append(save_review(load_png("ui/map_background.png"), "map_bg.png", 1))
    return paths


if __name__ == "__main__":
    for p in review_all():
        print(p)
