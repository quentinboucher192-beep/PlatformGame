#!/usr/bin/env python3
"""The gift wraps of « Cadeaux » (docs/COLLECTION.md « Cadeaux »): eleven boxes in pixel art.

One sheet, deterministic (fixed seeds, Pillow's ImageDraw on exact pixels, no smoothing):

* ui/gifts/wraps.png: one row of 80 x 40 per wrap, in the order of WRAPS below (the server's GIFT_WRAPS):
    x  0..39  the body of the box (its ribbon down the middle, its pattern);
    x 40..79  the lid with its bow, drawn 14 px higher than the body (`lid_dy`) when the box is closed.
* ui/gifts/wraps.json: the cells of each wrap ([x, y, w, h] of the body and of the lid), the lid's offset, and the
  colour of its light (the rays, the halo and the sparks of the opening scene; the box's own colours are in the PNG).

Each wrap: a box colour with its shadow, dark edge and light edge, a ribbon (with its shadow and light), and a
pattern (stars, dots, stripes, galaxy, snow, cracks, gears, a heart) in its own colour.

    python3 tools/assetgen/environment/gift_wraps.py           # writes the two files
    python3 tools/assetgen/environment/gift_wraps.py --check   # 0: the files are the generator's

`--check` renders the sheet twice (the same pixels) and compares the JSON bytes and the PNG pixels with the files
on disk (tests/test_gifts.cpp runs it)."""
import io
import json
import os
import random
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")
PNG = "ui/gifts/wraps.png"
MANIFEST = "ui/gifts/wraps.json"
CELL = 40
LID_DY = -14

# id: (box, shadow, dark, light, ribbon, ribbon shadow, ribbon light, pattern, pattern colour, light of the scene)
WRAPS = [
    ("or",        ("#eca834", "#b8701e", "#703e16", "#ffde80", "#9646e6", "#6028aa", "#c896ff", "stars", "#ffde80", "#ffcd6e")),
    ("amethyste", ("#9650e6", "#622eb0", "#36166e", "#cea0ff", "#ffc450", "#c48024", "#ffeca0", "stars", "#cea0ff", "#be96ff")),
    ("azur",      ("#38aae6", "#1c70b0", "#103a6e", "#96dcff", "#f0f4ff", "#aab6d6", "#ffffff", "stars", "#96dcff", "#5ee0ff")),
    ("rubis",     ("#d8344c", "#9a1e34", "#5a1020", "#ff8a9a", "#ffcc5a", "#c48a24", "#fff0a8", "dots", "#ff8a9a", "#ff7088")),
    ("emeraude",  ("#2cb86c", "#1a7c48", "#0e4a2a", "#8cf0b4", "#f4f6ff", "#aeb8d4", "#ffffff", "stripes", "#8cf0b4", "#6ee6a0")),
    ("nebuleuse", ("#c050c8", "#8030a0", "#461a64", "#ffa0f0", "#5ee0ff", "#2a9ac0", "#c8f6ff", "galaxy", "#ffffff", "#f08cf0")),
    ("givre",     ("#cfe8ff", "#8cb4e0", "#4a6a9a", "#ffffff", "#4aa8f0", "#2a70b8", "#a8dcff", "snow", "#4aa8f0", "#b4e2ff")),
    ("braise",    ("#3a2230", "#24141e", "#120a10", "#5a3448", "#ff8a2a", "#c0501a", "#ffd27a", "cracks", "#ff9a30", "#ff9a30")),
    ("cosmos",    ("#1c2458", "#121842", "#080c24", "#34408a", "#5ee0ff", "#2a9ac0", "#c8f6ff", "galaxy", "#ffffff", "#7ab8ff")),
    ("rouage",    ("#b88a4a", "#8a6230", "#4e3618", "#e8c488", "#4a3a2a", "#2a2016", "#8a6a4a", "gears", "#5a4020", "#e8c488")),
    ("asterion",  ("#5a1626", "#3c0c18", "#1e060c", "#9a3248", "#ffcd6e", "#c4882a", "#fff0b0", "heart", "#ff5a78", "#ff5a78")),
]


def C(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def pattern(d, kind, col, x0, y0, x1, y1, seed):
    rng = random.Random(seed)
    if kind == "stars":
        for (sx, sy) in [(x0 + 3, y0 + 5), (x1 - 5, y0 + 11), (x0 + 6, y1 - 4)]:
            d.point([(sx, sy)], fill=col)
            d.point([(sx - 1, sy), (sx + 1, sy), (sx, sy - 1), (sx, sy + 1)], fill=col + (150,))
    elif kind == "dots":
        for yy in range(y0 + 3, y1 - 1, 5):
            for xx in range(x0 + 3 + (yy // 5 % 2) * 2, x1 - 1, 5):
                if not 17 <= xx <= 22:
                    d.point([(xx, yy), (xx + 1, yy), (xx, yy + 1), (xx + 1, yy + 1)], fill=col)
    elif kind == "stripes":
        for k in range(-40, 40, 6):
            for t in range(0, 40):
                xx, yy = x0 + t + k, y0 + t
                if x0 < xx < x1 and y0 < yy < y1 and not 17 <= xx <= 22:
                    d.point([(xx, yy), (xx + 1, yy)], fill=col)
    elif kind == "galaxy":
        for _ in range(int((x1 - x0) * (y1 - y0) / 14)):
            xx, yy = rng.randint(x0 + 1, x1 - 1), rng.randint(y0 + 1, y1 - 1)
            if not 17 <= xx <= 22:
                d.point([(xx, yy)], fill=rng.choice([col, (255, 160, 240), (150, 220, 255)]))
    elif kind == "snow":
        for (sx, sy) in [(x0 + 4, y0 + 4), (x1 - 6, y0 + 8), (x0 + 7, y1 - 5), (x1 - 4, y1 - 4)]:
            if sy > y1 - 2 or sy < y0 + 2:
                continue
            d.point([(sx, sy), (sx - 2, sy), (sx + 2, sy), (sx, sy - 2), (sx, sy + 2), (sx - 1, sy - 1), (sx + 1, sy + 1),
                     (sx - 1, sy + 1), (sx + 1, sy - 1)], fill=col)
    elif kind == "cracks":
        for start in [(x0 + 2, y0 + 2), (x1 - 3, y0 + 3), (x0 + 4, y1 - 6)]:
            xx, yy = start
            for _ in range(9):
                if x0 < xx < x1 and y0 < yy < y1 and not 17 <= xx <= 22:
                    d.point([(xx, yy)], fill=col)
                xx += rng.choice([-1, 1, 1]) * (1 if start[0] < 20 else -1)
                yy += 1
    elif kind == "gears":
        for (gx, gy) in [(x0 + 5, y0 + 6), (x1 - 6, y1 - 6), (x1 - 6, y0 + 5)]:
            if gy > y1 - 3 or gy < y0 + 2:
                continue
            d.ellipse([gx - 2, gy - 2, gx + 2, gy + 2], outline=col)
            d.point([(gx, gy - 3), (gx, gy + 3), (gx - 3, gy), (gx + 3, gy)], fill=col)
    elif kind == "heart":
        hx, hy = x0 + 6, y0 + 8
        for (a, b) in [(0, 0), (1, 0), (3, 0), (4, 0), (-1, 1), (0, 1), (1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (-1, 2), (0, 2), (1, 2),
                       (2, 2), (3, 2), (4, 2), (5, 2), (0, 3), (1, 3), (2, 3), (3, 3), (4, 3), (1, 4), (2, 4), (3, 4), (2, 5)]:
            d.point([(hx + a, hy + b)], fill=col)


def box(p):
    """The body (40 x 40) and the lid with its bow (40 x 40) of one wrap."""
    b, bs, bd, bl, r, rs, rl, kind, pc, _light = p
    b, bs, bd, bl, r, rs, rl, pc = map(C, (b, bs, bd, bl, r, rs, rl, pc))
    body = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    x0, y0, x1, y1 = 6, 18, 33, 38
    d.rectangle([x0, y0, x1, y1], fill=bd)
    d.rectangle([x0 + 1, y0, x1 - 1, y1 - 1], fill=b)
    d.rectangle([x0 + 1, y0 + 12, x1 - 1, y1 - 1], fill=bs)
    pattern(d, kind, pc, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 7)
    d.rectangle([x0 + 1, y0, x0 + 2, y1 - 2], fill=bl)
    d.rectangle([x1 - 3, y0, x1 - 1, y1 - 1], fill=bs)
    d.rectangle([18, y0, 21, y1 - 1], fill=r)
    d.rectangle([21, y0, 21, y1 - 1], fill=rs)
    d.rectangle([18, y0, 18, y1 - 1], fill=rl)
    d.rectangle([x0, y0, x1, y0], fill=bd)
    lid = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(lid)
    lx0, ly0, lx1, ly1 = 4, 26, 35, 33
    d.rectangle([lx0, ly0, lx1, ly1], fill=bd)
    d.rectangle([lx0 + 1, ly0 + 1, lx1 - 1, ly1 - 1], fill=b)
    pattern(d, kind if kind not in ("heart", "snow", "gears", "stars") else "none", pc, lx0 + 1, ly0 + 1, lx1 - 1, ly1 - 1, 3)
    d.rectangle([lx0 + 1, ly0 + 1, lx1 - 1, ly0 + 2], fill=bl)
    d.rectangle([lx0 + 1, ly1 - 2, lx1 - 1, ly1 - 1], fill=bs)
    d.rectangle([18, ly0, 21, ly1], fill=r)
    d.rectangle([18, ly0, 18, ly1], fill=rl)
    for side in (-1, 1):
        cx = 19.5 + side * 6
        d.ellipse([cx - 5, 15, cx + 5, 25], fill=rs)
        d.ellipse([cx - 4, 16, cx + 4, 24], fill=r)
        d.ellipse([cx - 2 + side, 18, cx + 2 + side, 22], fill=rs)
        d.point([(int(cx - 2 * side), 17)], fill=rl)
    d.rectangle([17, 19, 22, 25], fill=rs)
    d.rectangle([18, 20, 21, 24], fill=r)
    d.point([(18, 20)], fill=rl)
    return body, lid


def render():
    sheet = Image.new("RGBA", (2 * CELL, CELL * len(WRAPS)), (0, 0, 0, 0))
    wraps = []
    for i, (wid, p) in enumerate(WRAPS):
        body, lid = box(p)
        y = i * CELL
        sheet.paste(body, (0, y))
        sheet.paste(lid, (CELL, y))
        wraps.append({"id": wid, "body": [0, y, CELL, CELL], "lid": [CELL, y, CELL, CELL], "light": list(C(p[9]))})
    manifest = {"texture": PNG, "cell": CELL, "lid_dy": LID_DY, "wraps": wraps,
                "comment": "Written by tools/assetgen/environment/gift_wraps.py: never edit by hand."}
    buf = io.BytesIO()
    sheet.save(buf, format="PNG", optimize=False)
    text = json.dumps(manifest, ensure_ascii=False, indent=1) + "\n"
    return buf.getvalue(), text, sheet


def main():
    png, text, sheet = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        _png2, text2, sheet2 = render()
        if text != text2 or sheet.tobytes() != sheet2.tobytes():
            print("gift_wraps: not deterministic")
            return 1
        try:
            with open(os.path.join(ASSETS, MANIFEST), encoding="utf-8") as f:
                on_disk = f.read()
            disk = Image.open(os.path.join(ASSETS, PNG)).convert("RGBA")
        except OSError as e:
            print("gift_wraps: %s" % e)
            return 1
        if on_disk != text or disk.size != sheet.size or disk.tobytes() != sheet.tobytes():
            print("gift_wraps: the files on disk are not the generator's (run it)")
            return 1
        print("gift_wraps: ok")
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
