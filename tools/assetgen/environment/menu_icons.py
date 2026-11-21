#!/usr/bin/env python3
"""The pixel icons of the main menu (ui/widgets/CosmicStyle: plates and diamond buttons), 24 x 24 each.

One sheet, deterministic (Pillow's ImageDraw on exact pixels, no smoothing, no random):

* ui/menu_icons.png: one row of 24 x 24 cells, in the order of ICONS below;
* ui/menu_icons.json: the cell size and the index of each icon by id (CosmicStyle::menuIcon reads it).

Each icon: a dark outline (D), its colours, a white highlight (H).

    python3 tools/assetgen/environment/menu_icons.py           # writes the two files
    python3 tools/assetgen/environment/menu_icons.py --check   # 0: the files are the generator's

`--check` renders the sheet twice (the same pixels) and compares the JSON bytes and the PNG pixels with the files
on disk (tests/test_cosmic_menu.cpp runs it)."""
import io
import json
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")
PNG = "ui/menu_icons.png"
MANIFEST = "ui/menu_icons.json"
CELL = 24

D, L, H = (10, 14, 30), (226, 236, 255), (255, 255, 255)
CYAN, GOLD, RED, VIOLET, GREEN = (94, 224, 255), (255, 205, 110), (255, 90, 110), (190, 150, 255), (110, 225, 150)


def i_play(d):
    d.polygon([(7, 4), (20, 12), (7, 20)], fill=D)
    d.polygon([(8, 6), (18, 12), (8, 18)], fill=CYAN)
    d.line([(9, 8), (9, 15)], fill=H)
    d.line([(1, 9), (4, 9)], fill=CYAN)
    d.line([(2, 14), (5, 14)], fill=CYAN)


def i_planet(d):
    d.ellipse([5, 5, 18, 18], fill=D)
    d.ellipse([6, 6, 17, 17], fill=(240, 140, 80))
    d.ellipse([8, 7, 12, 10], fill=(255, 200, 140))
    d.arc([0, 9, 23, 16], 160, 380, fill=(255, 220, 160), width=2)


def i_target(d):
    for r, c in [(10, RED), (7, L), (4, RED), (1, L)]:
        d.ellipse([12 - r, 12 - r, 12 + r, 12 + r], fill=c, outline=D)
    d.line([(12, 12), (21, 3)], fill=GOLD, width=2)
    d.polygon([(19, 2), (23, 1), (22, 5)], fill=GOLD)


def i_coop(d):
    for x, c in [(2, VIOLET), (12, CYAN)]:
        d.ellipse([x, 4, x + 10, 14], fill=D)
        d.ellipse([x + 1, 5, x + 9, 13], fill=c)
        d.rectangle([x + 3, 7, x + 8, 10], fill=(20, 40, 80))
        d.rectangle([x, 15, x + 10, 22], fill=D)
        d.rectangle([x + 1, 16, x + 9, 22], fill=c)


def i_globe(d):
    d.ellipse([2, 2, 21, 21], fill=D)
    d.ellipse([3, 3, 20, 20], fill=(40, 120, 200))
    d.polygon([(6, 7), (11, 5), (12, 10), (8, 12)], fill=GREEN)
    d.polygon([(13, 13), (18, 12), (17, 17), (14, 18)], fill=GREEN)
    d.arc([3, 3, 20, 20], 0, 360, fill=(150, 220, 255))
    d.line([(11, 3), (11, 20)], fill=(150, 220, 255, 120))


def i_bell(d):
    d.polygon([(6, 17), (8, 7), (12, 4), (16, 7), (18, 17)], fill=D)
    d.polygon([(7, 16), (9, 8), (12, 5), (15, 8), (17, 16)], fill=GOLD)
    d.rectangle([4, 16, 20, 18], fill=GOLD, outline=D)
    d.ellipse([10, 18, 14, 22], fill=GOLD, outline=D)
    d.line([(10, 8), (9, 14)], fill=H)


def i_star(d):
    pts = []
    for i in range(10):
        r = 10 if i % 2 == 0 else 4.2
        a = -math.pi / 2 + i * math.pi / 5
        pts.append((round(12 + r * math.cos(a), 3), round(12.5 + r * math.sin(a), 3)))
    d.polygon(pts, fill=GOLD, outline=D)
    d.line([(10, 8), (9, 12)], fill=H)


def i_chest(d):
    d.rectangle([3, 10, 20, 20], fill=(150, 90, 40), outline=D)
    d.rectangle([3, 5, 20, 10], fill=(190, 120, 50), outline=D)
    d.rectangle([3, 10, 20, 11], fill=GOLD)
    d.rectangle([10, 9, 13, 14], fill=GOLD, outline=D)


def i_bars(d):
    for x, h, c in [(3, 8, CYAN), (9, 14, VIOLET), (15, 18, GREEN)]:
        d.rectangle([x, 21 - h, x + 4, 21], fill=c, outline=D)


def i_gear(d):
    for k in range(8):
        a = k * math.pi / 4
        cx, cy = round(12 + 8 * math.cos(a), 3), round(12 + 8 * math.sin(a), 3)
        d.rectangle([cx - 2, cy - 2, cx + 2, cy + 2], fill=(170, 185, 215))
    d.ellipse([4, 4, 19, 19], fill=(170, 185, 215), outline=D)
    d.ellipse([8, 8, 15, 15], fill=D)


def i_door(d):
    d.rectangle([4, 2, 14, 21], fill=D)
    d.rectangle([5, 3, 13, 21], fill=(255, 106, 106))
    d.point([(11, 12)], fill=H)
    d.line([(14, 12), (22, 12)], fill=L, width=2)
    d.polygon([(19, 8), (23, 12), (19, 16)], fill=L)


def i_shield(d):
    d.polygon([(12, 2), (21, 5), (19, 15), (12, 22), (5, 15), (3, 5)], fill=D)
    d.polygon([(12, 4), (19, 6), (17, 14), (12, 20), (7, 14), (5, 6)], fill=GOLD)
    d.line([(12, 6), (12, 17)], fill=(190, 120, 30), width=2)


# The order of the cells (never reorder: the JSON gives the index of each id).
ICONS = [("continue", i_play), ("campaign", i_planet), ("challenges", i_target), ("coop", i_coop), ("online", i_globe),
         ("news", i_bell), ("achievements", i_star), ("collection", i_chest), ("stats", i_bars), ("settings", i_gear),
         ("quit", i_door), ("admin", i_shield)]


def render():
    sheet = Image.new("RGBA", (CELL * len(ICONS), CELL), (0, 0, 0, 0))
    for i, (_, fn) in enumerate(ICONS):
        im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        fn(ImageDraw.Draw(im))
        sheet.paste(im, (i * CELL, 0))
    manifest = {"texture": PNG, "cell": CELL, "icons": {k: i for i, (k, _) in enumerate(ICONS)},
                "comment": "Written by tools/assetgen/environment/menu_icons.py: never edit by hand."}
    buf = io.BytesIO()
    sheet.save(buf, format="PNG", optimize=False)
    text = json.dumps(manifest, ensure_ascii=False, indent=1) + "\n"
    return buf.getvalue(), text, sheet


def main():
    png, text, sheet = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        _png2, text2, sheet2 = render()
        if text != text2 or sheet.tobytes() != sheet2.tobytes():
            print("menu_icons: not deterministic")
            return 1
        try:
            with open(os.path.join(ASSETS, MANIFEST), encoding="utf-8") as f:
                on_disk = f.read()
            disk = Image.open(os.path.join(ASSETS, PNG)).convert("RGBA")
        except OSError as e:
            print("menu_icons: %s" % e)
            return 1
        if on_disk != text or disk.size != sheet.size or disk.tobytes() != sheet.tobytes():
            print("menu_icons: the files on disk are not the generator's (run it)")
            return 1
        print("menu_icons: ok")
        return 0
    with open(os.path.join(ASSETS, PNG), "wb") as f:
        f.write(png)
    with open(os.path.join(ASSETS, MANIFEST), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote assets/%s and assets/%s" % (PNG, MANIFEST))
    return 0


if __name__ == "__main__":
    sys.exit(main())
