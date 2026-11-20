#!/usr/bin/env python3
"""2.78: the icons of the raids' « Tableau des contributions » and of the lair's distance indicator
(docs/RAIDS.md §26).

One sheet, deterministic (no randomness; the shapes drawn four times larger with Pillow's ImageDraw,
then averaged down by numpy, a dark outline under each so they read on every panel):

* ui/raids/contrib.png + ui/raids/contrib.json: icons of 32 px, one per category of the table, each in
  the colour of its category (the table writes its numbers in the same colours):
    boss       a sword (damage to the boss)            red
    mini       a star (damage to the mini-bosses)      orange
    support    a heart (support: blows blocked, allies revived)   green
    mechanisms a cog (plates held, vents, weak points)  gold
    objectives a flag (objectives of the branch)        blue
    deaths     a skull (times down)                     grey
    revives    a cross (allies revived)                 light green
    revived    a cross in a ring (revivals received)    pale green
    crown      the crown of the best of a column        gold
    arrow      an arrow pointing right, white (the distance indicator turns and tints it)
  The manifest names each cell: [x, y, w, h], and the colour of each category (« colors »).

    python3 tools/assetgen/environment/raid_contrib_icons.py           # writes the two files
    python3 tools/assetgen/environment/raid_contrib_icons.py --check   # 0: the files are the generator's

`--check` renders the sheet twice (the same bytes) and compares the JSON bytes and the PNG pixels with
the files on disk (tests/test_raid_zones.cpp runs it)."""
import io
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")
PNG = "ui/raids/contrib.png"
MANIFEST = "ui/raids/contrib.json"
CELL = 32
S = 4  # supersampling
OUTLINE = (12, 14, 30, 255)

COLORS = {
    "boss": (255, 96, 96), "mini": (255, 160, 80), "support": (96, 220, 150), "mechanisms": (255, 205, 110),
    "objectives": (110, 170, 255), "deaths": (170, 176, 196), "revives": (120, 235, 170), "revived": (180, 240, 210),
    "crown": (255, 205, 110), "arrow": (255, 255, 255),
}
ORDER = ["boss", "mini", "support", "mechanisms", "objectives", "deaths", "revives", "revived", "crown", "arrow"]


def shape(kind, d, col, grow=0.0):
    """Draws `kind` on a CELL × S canvas; `grow` widens every stroke (the outline pass)."""
    c = CELL * S / 2.0
    u = CELL * S / 32.0  # one pixel of the final icon
    g = grow * u
    fill = col
    if kind == "boss":  # a sword, blade up-right
        w = 4.2 * u + 2 * g
        d.line([(c - 10 * u, c + 10 * u), (c + 11 * u, c - 11 * u)], fill=fill, width=int(w))
        d.polygon([(c + 13 * u + g, c - 13 * u - g), (c + 7 * u, c - 12 * u - g), (c + 12 * u + g, c - 7 * u)], fill=fill)
        d.line([(c - 11 * u, c + 1 * u), (c - 1 * u, c + 11 * u)], fill=fill, width=int(4 * u + 2 * g))  # the guard
        d.line([(c - 13 * u, c + 13 * u), (c - 9 * u, c + 9 * u)], fill=fill, width=int(5 * u + 2 * g))  # the hilt
    elif kind == "mini":  # a five-pointed star
        pts = []
        for i in range(10):
            a = math.pi / 5 * i - math.pi / 2
            r = (13 * u + g) if i % 2 == 0 else (5.6 * u + g)
            pts.append((c + r * math.cos(a), c + 1 * u + r * math.sin(a)))
        d.polygon(pts, fill=fill)
    elif kind == "support":  # a heart
        r = 6.5 * u + g
        d.ellipse([c - 13 * u - g, c - 9 * u - g, c - 13 * u + 2 * r - g, c - 9 * u + 2 * r - g], fill=fill)
        d.ellipse([c + 13 * u - 2 * r + g, c - 9 * u - g, c + 13 * u + g, c - 9 * u + 2 * r - g], fill=fill)
        d.polygon([(c - 12.6 * u - g, c - 1 * u), (c + 12.6 * u + g, c - 1 * u), (c, c + 13 * u + g)], fill=fill)
    elif kind == "mechanisms":  # a cog: teeth, a ring, a hole
        for i in range(8):
            a = math.pi / 4 * i
            dx, dy = math.cos(a), math.sin(a)
            px, py = -dy, dx
            r0, r1, hw = 8 * u, 13.5 * u + g, 2.6 * u + g
            d.polygon([(c + dx * r0 + px * hw, c + dy * r0 + py * hw), (c + dx * r1 + px * hw, c + dy * r1 + py * hw),
                       (c + dx * r1 - px * hw, c + dy * r1 - py * hw), (c + dx * r0 - px * hw, c + dy * r0 - py * hw)], fill=fill)
        d.ellipse([c - 10 * u - g, c - 10 * u - g, c + 10 * u + g, c + 10 * u + g], fill=fill)
    elif kind == "objectives":  # a flag on its pole
        d.rectangle([c - 10 * u - g, c - 13 * u - g, c - 7 * u + g, c + 13 * u + g], fill=fill)
        d.polygon([(c - 8 * u, c - 13 * u - g), (c + 12 * u + g, c - 7 * u), (c - 8 * u, c - 1 * u + g)], fill=fill)
    elif kind == "deaths":  # a skull
        d.ellipse([c - 11 * u - g, c - 13 * u - g, c + 11 * u + g, c + 6 * u + g], fill=fill)
        d.rectangle([c - 6.5 * u - g, c + 2 * u, c + 6.5 * u + g, c + 12 * u + g], fill=fill)
    elif kind in ("revives", "revived"):  # a cross (in a ring: received)
        k = 0.75 if kind == "revived" else 1.0
        w = 3.6 * u * k + g
        L = 11.5 * u * k + g
        d.rectangle([c - w, c - L, c + w, c + L], fill=fill)
        d.rectangle([c - L, c - w, c + L, c + w], fill=fill)
        if kind == "revived":
            d.ellipse([c - 14 * u - g, c - 14 * u - g, c + 14 * u + g, c + 14 * u + g], outline=fill, width=int(2.6 * u + 2 * g))
    elif kind == "crown":
        d.polygon([(c - 13 * u - g, c - 9 * u - g), (c - 6 * u, c + 1 * u), (c, c - 12 * u - g), (c + 6 * u, c + 1 * u), (c + 13 * u + g, c - 9 * u - g),
                   (c + 12 * u + g, c + 9 * u + g), (c - 12 * u - g, c + 9 * u + g)], fill=fill)
    elif kind == "arrow":
        d.polygon([(c + 13 * u + g, c), (c - 4 * u, c - 12 * u - g), (c - 4 * u, c - 4.5 * u), (c - 13 * u - g, c - 4.5 * u),
                   (c - 13 * u - g, c + 4.5 * u), (c - 4 * u, c + 4.5 * u), (c - 4 * u, c + 12 * u + g)], fill=fill)


def details(kind, d):
    """What is cut out of a shape (the skull's eyes, the cog's hole), drawn after it, transparent."""
    c = CELL * S / 2.0
    u = CELL * S / 32.0
    clear = (0, 0, 0, 0)
    if kind == "deaths":
        d.ellipse([c - 7.5 * u, c - 6 * u, c - 2 * u, c - 0.5 * u], fill=clear)
        d.ellipse([c + 2 * u, c - 6 * u, c + 7.5 * u, c - 0.5 * u], fill=clear)
        d.rectangle([c - 1 * u, c + 6 * u, c + 1 * u, c + 12 * u], fill=clear)
    elif kind == "mechanisms":
        d.ellipse([c - 4.5 * u, c - 4.5 * u, c + 4.5 * u, c + 4.5 * u], fill=clear)


def icon(kind):
    big = CELL * S
    col = COLORS[kind] + (255,)
    outline = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    shape(kind, ImageDraw.Draw(outline), OUTLINE, grow=1.6)
    details(kind, ImageDraw.Draw(outline))
    body = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    shape(kind, ImageDraw.Draw(body), col)
    details(kind, ImageDraw.Draw(body))
    # A light from the top: the upper half a little brighter (read as a lit relief, not a flat glyph).
    a = np.asarray(body).astype(np.float32)
    ramp = np.linspace(1.18, 0.86, big, dtype=np.float32)[:, None, None]
    a[..., :3] = np.clip(a[..., :3] * ramp, 0, 255)
    body = Image.fromarray(a.astype(np.uint8), "RGBA")
    out = Image.alpha_composite(outline, body)
    # Averaged down (premultiplied, so the edges do not darken).
    arr = np.asarray(out).astype(np.float32) / 255.0
    rgb, al = arr[..., :3] * arr[..., 3:4], arr[..., 3:4]
    rgb = rgb.reshape(CELL, S, CELL, S, 3).mean(axis=(1, 3))
    al = al.reshape(CELL, S, CELL, S, 1).mean(axis=(1, 3))
    rgb = np.where(al > 1e-6, rgb / np.maximum(al, 1e-6), 0.0)
    res = np.concatenate([rgb, al], axis=-1)
    return np.clip(np.round(res * 255.0), 0, 255).astype(np.uint8)


def render():
    cols = len(ORDER)  # one row: UIContext::icon picks an icon by its index
    rows = (len(ORDER) + cols - 1) // cols
    sheet = np.zeros((rows * CELL, cols * CELL, 4), np.uint8)
    cells = {}
    for i, kind in enumerate(ORDER):
        x, y = (i % cols) * CELL, (i // cols) * CELL
        sheet[y:y + CELL, x:x + CELL] = icon(kind)
        cells[kind] = [x, y, CELL, CELL]
    manifest = {"texture": PNG, "icon": CELL, "cells": cells,
                "colors": {k: list(COLORS[k]) for k in ORDER},
                "comment": "Written by tools/assetgen/environment/raid_contrib_icons.py (2.78): never edit by hand."}
    buf = io.BytesIO()
    Image.fromarray(sheet, "RGBA").save(buf, format="PNG", optimize=False)
    text = json.dumps(manifest, ensure_ascii=False, indent=1) + "\n"
    return buf.getvalue(), text, sheet


def main():
    png, text, sheet = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        png2, text2, sheet2 = render()
        if text != text2 or not np.array_equal(sheet, sheet2):
            print("raid_contrib_icons: not deterministic")
            return 1
        try:
            with open(os.path.join(ASSETS, MANIFEST), encoding="utf-8") as f:
                on_disk = f.read()
            disk = np.asarray(Image.open(os.path.join(ASSETS, PNG)).convert("RGBA"))
        except OSError as e:
            print("raid_contrib_icons: %s" % e)
            return 1
        if on_disk != text or disk.shape != sheet.shape or not np.array_equal(disk, sheet):
            print("raid_contrib_icons: the files on disk are not the generator's (run it)")
            return 1
        print("raid_contrib_icons: ok")
        return 0
    os.makedirs(os.path.dirname(os.path.join(ASSETS, PNG)), exist_ok=True)
    with open(os.path.join(ASSETS, PNG), "wb") as f:
        f.write(png)
    with open(os.path.join(ASSETS, MANIFEST), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote assets/%s and assets/%s" % (PNG, MANIFEST))
    return 0


if __name__ == "__main__":
    sys.exit(main())
