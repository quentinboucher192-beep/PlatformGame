"""Developer helper: upscaled contact sheets for visual review.

    python3 tools/assetgen/characters/review.py [out_dir]

Writes one PNG per generated sheet (nearest-neighbour upscaled, frame grid,
dusk background) so the art can be inspected frame by frame.
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)

from sheet import ASSETS  # noqa: E402

BG = (26, 39, 71, 255)
BG2 = (34, 54, 90, 255)
GRID = (60, 80, 110, 255)


def contact(img, fw, fh, scale, labels=None, bg=BG):
    img = Image.fromarray(img) if isinstance(img, np.ndarray) else img
    w, h = img.size
    cols, rows = w // fw, h // fh
    lab_w = 110 if labels else 0
    out = Image.new("RGBA", (lab_w + cols * (fw * scale + 2), rows * (fh * scale + 2)), (12, 15, 30, 255))
    d = ImageDraw.Draw(out)
    for r in range(rows):
        for c in range(cols):
            fr = img.crop((c * fw, r * fh, (c + 1) * fw, (r + 1) * fh))
            tile = Image.new("RGBA", (fw, fh), bg if (r + c) % 2 == 0 else BG2)
            tile.alpha_composite(fr)
            tile = tile.resize((fw * scale, fh * scale), Image.NEAREST)
            x = lab_w + c * (fw * scale + 2)
            y = r * (fh * scale + 2)
            out.paste(tile, (x, y))
        if labels and r < len(labels):
            d.text((4, r * (fh * scale + 2) + 4), labels[r], fill=(220, 230, 240, 255))
    return out


def review_anim(json_path, out_dir, scale=None, max_rows=None):
    meta = json.load(open(json_path))
    img = Image.open(os.path.join(ASSETS, meta["texture"])).convert("RGBA")
    fw, fh = meta["frameWidth"], meta["frameHeight"]
    if scale is None:
        scale = max(3, min(8, 192 // max(fw, fh)))
    clips = sorted(meta["clips"].items(), key=lambda kv: kv[1]["row"])
    labels = [f'{n} ({c["count"]})' for n, c in clips]
    name = os.path.splitext(os.path.basename(json_path))[0]
    if max_rows and len(labels) > max_rows:
        for i in range(0, len(labels), max_rows):
            cols = max(c["count"] for _, c in clips[i:i + max_rows])
            sub = img.crop((0, i * fh, cols * fw, min(img.height, (i + max_rows) * fh)))
            contact(sub, fw, fh, scale, labels[i:i + max_rows]).save(
                os.path.join(out_dir, f"{name}_{i // max_rows}.png"))
    else:
        contact(img, fw, fh, scale, labels).save(os.path.join(out_dir, f"{name}.png"))


def review_static(rel, fw, fh, out_dir, scale=6):
    img = Image.open(os.path.join(ASSETS, rel)).convert("RGBA")
    name = rel.replace("/", "_").replace(".png", "")
    contact(img, fw, fh, scale).save(os.path.join(out_dir, f"{name}.png"))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/claude-0/assetreview/characters"
    only = sys.argv[2:] if len(sys.argv) > 2 else None
    os.makedirs(out_dir, exist_ok=True)
    anim_dir = os.path.join(ASSETS, "animations")
    for fn in sorted(os.listdir(anim_dir)):
        if not fn.endswith(".json"):
            continue
        if only and os.path.splitext(fn)[0] not in only:
            continue
        mr = 10 if fn.startswith("nova") else (4 if fn.startswith("thornwarden") else None)
        review_anim(os.path.join(anim_dir, fn), out_dir, max_rows=mr)
    print("review written to", out_dir)


if __name__ == "__main__":
    main()
