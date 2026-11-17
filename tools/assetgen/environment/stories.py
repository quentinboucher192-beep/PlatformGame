#!/usr/bin/env python3
"""Paintings of the giga bosses' stories (« Histoire des giga-boss », 2.51).

One illustration per shot of data/stories/<boss>.json: assets/stories/<boss>_<n>.png, 960x540
(16:9), painted procedurally (story_paint.py) in the boss's colours. The game shows each one
with a slow zoom between letterbox bands (only the middle ~74 % in height is seen, plus the zoom:
the subject stays in the middle). Deterministic: fixed seeds.

    python3 tools/assetgen/environment/stories.py [singularis|pyrrhax|monolithe|kraken|phenix …] [--review DIR]

--review DIR also writes the shots and one contact sheet per boss (the six shots in a 3x2 grid,
numbered) into DIR.
"""
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from PIL import Image, ImageDraw  # noqa: E402

from helpers import ASSETS  # noqa: E402

OUT = os.path.join(ASSETS, "stories")
SIZE = (960, 540)
MAX_BYTES = 420 * 1024        # per painting
MAX_TOTAL = 10 * 1024 * 1024  # every painting of every story (2.53: room for the next giga bosses)


MODULES = {"singularis": "story_singularis", "pyrrhax": "story_pyrrhax", "monolithe": "story_monolithe",
           "kraken": "story_kraken", "phenix": "story_phenix"}  # 2.53, 2.56


def bosses(names=None):
    import importlib
    return {b: importlib.import_module(m).SHOTS for b, m in MODULES.items() if not names or b in names}


def contact_sheet(paths, out):
    tw, th = 480, 270
    sheet = Image.new("RGB", (tw * 3 + 40, th * 2 + 30), (12, 12, 18))
    d = ImageDraw.Draw(sheet)
    for i, p in enumerate(paths):
        im = Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS)
        x, y = 10 + (i % 3) * (tw + 10), 10 + (i // 3) * (th + 10)
        sheet.paste(im, (x, y))
        d.rectangle([x + 6, y + 6, x + 34, y + 30], fill=(0, 0, 0))
        d.text((x + 15, y + 11), str(i + 1), fill=(255, 255, 255))
    sheet.save(out)


def generate(names=None, review=None):
    from story_paint import save
    os.makedirs(OUT, exist_ok=True)
    files = []
    for boss, shots in bosses(names).items():
        paths = []
        for i, shot in enumerate(shots, 1):
            p = os.path.join(OUT, f"{boss}_{i}.png")
            save(shot(), p)
            paths.append(p)
            files.append(p)
            print(f"  stories/{boss}_{i}.png  {os.path.getsize(p) // 1024} KiB", flush=True)
        if review:
            os.makedirs(review, exist_ok=True)
            for p in paths:
                Image.open(p).save(os.path.join(review, os.path.basename(p)))
            contact_sheet(paths, os.path.join(review, f"story_{boss}.png"))
    return files


def check():
    """The paintings every story names: present, 960x540, within the size budget."""
    import json
    errs = []
    total = 0
    sdir = os.path.join(os.path.dirname(ASSETS), "data", "stories")
    for f in sorted(os.listdir(sdir)) if os.path.isdir(sdir) else []:
        if not f.endswith(".json"):
            continue
        story = json.load(open(os.path.join(sdir, f), encoding="utf-8"))
        for shot in story.get("shots", []):
            p = os.path.join(ASSETS, shot.get("image", ""))
            if not os.path.isfile(p):
                errs.append(f"{f}: missing {shot.get('image')}")
                continue
            im = Image.open(p)
            if im.size != SIZE:
                errs.append(f"{shot['image']}: size {im.size} != {SIZE}")
            size = os.path.getsize(p)
            total += size
            if size > MAX_BYTES:
                errs.append(f"{shot['image']}: {size // 1024} KiB > {MAX_BYTES // 1024} KiB")
    if total > MAX_TOTAL:
        errs.append(f"stories: {total // 1024} KiB in all > {MAX_TOTAL // 1024} KiB")
    return errs


def main(argv):
    review = None
    if "--review" in argv:
        i = argv.index("--review")
        review = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    names = [a for a in argv if not a.startswith("--")]
    generate(names, review)
    errs = check()
    for e in errs:
        print("FAIL", e)
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
