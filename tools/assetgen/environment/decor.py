"""Decor atlas (ASSET_SPEC section 9): sprites/decor/sylvanox_decor.png + .json.

Sprites are drawn by decor_nature / decor_ruins, then shelf-packed into one atlas. Animated
sprites keep their frames contiguous horizontally (frame i at rect.x + i * rect.w).
Glow fields: colour, radius (px) and offset relative to the pivot.
"""
import numpy as np

import decor_nature as N
import decor_ruins as R
from helpers import new, save_json, save_png

ATLAS_W = 256
GAP = 2

# name: (painter, frames, fps, pivot, glow or None)  glow = (colour, radius, (ox, oy))
SPRITES = {
    "mushroom_small": (N.mushroom_small, 1, 0, "bottom", ("#39f0d8", 20, (0, -11))),
    "mushroom_tall": (N.mushroom_tall, 1, 0, "bottom", ("#39f0d8", 40, (2, -32))),
    "mushroom_cluster": (N.mushroom_cluster, 1, 0, "bottom", ("#b04cff", 28, (3, -14))),
    "glow_fern": (N.glow_fern, 1, 0, "bottom", ("#39f0d8", 22, (0, -14))),
    "glow_flowers": (N.glow_flowers, 4, 6, "bottom", ("#e08aff", 16, (0, -9))),
    "crystal_cluster": (N.crystal_cluster, 1, 0, "bottom", ("#b04cff", 26, (0, -12))),
    "crystal_big": (N.crystal_big, 1, 0, "bottom", ("#b04cff", 44, (0, -24))),
    "ruin_pillar": (R.ruin_pillar, 1, 0, "bottom", ("#39f0d8", 14, (-2, -33))),
    "ruin_pillar_broken": (R.ruin_pillar_broken, 1, 0, "bottom", ("#39f0d8", 12, (-2, -21))),
    "ruin_arch": (R.ruin_arch, 1, 0, "bottom", ("#39f0d8", 18, (0, -58))),
    "statue_head": (R.statue_head, 1, 0, "bottom", ("#39f0d8", 24, (0, -19))),
    "tech_panel": (R.tech_panel, 4, 4, "bottom", ("#ffb347", 24, (0, -13))),
    "lantern_pod": (N.lantern_pod, 6, 6, "top", ("#39f0d8", 30, (0, 21))),
    "hanging_vine_short": (N.hanging_vine_short, 1, 0, "top", None),
    "hanging_vine_long": (N.hanging_vine_long, 1, 0, "top", None),
    "roots": (N.roots, 1, 0, "bottom", None),
    "rock_small": (N.rock_small, 1, 0, "bottom", None),
    "rock_big": (N.rock_big, 1, 0, "bottom", None),
    "floating_rock": (N.floating_rock, 1, 0, "center", ("#b04cff", 30, (0, 10))),
    "holo_glyph": (R.holo_glyph, 6, 8, "bottom", ("#39f0d8", 30, (0, -23))),
    "pipe_broken": (R.pipe_broken, 1, 0, "bottom", ("#ffb347", 16, (-3, -12))),
    "banner_torn": (R.banner_torn, 1, 0, "bottom", None),
    "grass_tuft": (N.grass_tuft, 1, 0, "bottom", None),
    "bones_ancient": (R.bones_ancient, 1, 0, "bottom", None),
}


def pack(items):
    """Shelf packing. items: list of (name, w, h). Returns {name: (x, y)} and atlas height."""
    order = sorted(items, key=lambda it: (-it[2], -it[1], it[0]))
    pos = {}
    x = y = 0
    shelf_h = 0
    for name, w, h in order:
        if x + w > ATLAS_W:
            x = 0
            y += shelf_h + GAP
            shelf_h = 0
        pos[name] = (x, y)
        x += w + GAP
        shelf_h = max(shelf_h, h)
    total = y + shelf_h
    return pos, int(np.ceil(total / 16.0) * 16)


def build_atlas(sprites, texture):
    """Draw, shelf-pack and describe a decor atlas. sprites: {name: (painter, frames, fps,
    pivot, glow)} with pivot in ("bottom", "top", "center") and glow = (colour, radius,
    (ox, oy)) or None."""
    imgs = {}
    for name, (fn, frames, fps, piv, glow) in sprites.items():
        img = fn()
        assert img.shape[1] % frames == 0, name
        imgs[name] = img
    pos, H = pack([(n, im.shape[1], im.shape[0]) for n, im in imgs.items()])
    atlas = new(ATLAS_W, H)
    meta = {"texture": texture, "sprites": {}}
    for name, (fn, frames, fps, piv, glow) in sprites.items():
        im = imgs[name]
        x, y = pos[name]
        atlas[y:y + im.shape[0], x:x + im.shape[1]] = im
        fw, fh = im.shape[1] // frames, im.shape[0]
        pivot = {"bottom": [fw // 2, fh], "top": [fw // 2, 0], "center": [fw // 2, fh // 2]}[piv]
        e = {"rect": [x, y, fw, fh], "pivot": pivot, "frames": frames, "fps": fps}
        if glow:
            e["glow"] = glow[0]
            e["glowRadius"] = glow[1]
            e["glowOffset"] = list(glow[2])
        meta["sprites"][name] = e
    return atlas, meta


def build():
    return build_atlas(SPRITES, "sprites/decor/sylvanox_decor.png")


def generate():
    atlas, meta = build()
    return [save_png(atlas, "sprites/decor/sylvanox_decor.png"),
            save_json(meta, "sprites/decor/sylvanox_decor.json")]
