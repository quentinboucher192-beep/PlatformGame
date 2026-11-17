#!/usr/bin/env python3
"""Paintings of the giga bosses' stories (« Histoire des giga-boss », 2.51).

One illustration per shot of data/stories/<boss>.json: assets/stories/<boss>_<n>.png, 960x540
(16:9), painted procedurally (story_paint.py) in the boss's colours. The game shows each one
with a slow zoom between letterbox bands (only the middle ~74 % in height is seen, plus the zoom:
the subject stays in the middle). Deterministic: fixed seeds.

    python3 tools/assetgen/environment/stories.py [singularis|pyrrhax|monolithe|kraken|phenix|liche|titan …] [--review DIR]

--review DIR also writes the shots and one contact sheet per boss (the six shots in a 3x2 grid,
numbered) into DIR.

2.60: Pyrrhax's story is told in animated scenes (version 2 of data/stories/*.json): its sprites,
its layers of scenery, its scenes and its stills come from story_pyrrhax_scene.py (SCENES); the
budget counts the files of the scenes with the paintings.  2.63: the other giga bosses' stories
follow, one by one (story_<boss>_scene.py, on the tooling of story_stage.py and story_scenery.py).
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
MAX_TOTAL = 14 * 1024 * 1024  # every painting of every story (2.53: room for the next giga bosses; 2.57: the Liche, 12 MiB; 2.58: the Titan, 14 MiB)


MODULES = {"singularis": "story_singularis", "titan": "story_titan"}  # 2.53, 2.56, 2.57, 2.58
SCENES = {"pyrrhax": "story_pyrrhax_scene",  # 2.60: animated scenes (their stills are the paintings)
          "monolithe": "story_monolithe_scene", "kraken": "story_kraken_scene",
          "phenix": "story_phenix_scene",
          "liche": "story_liche_scene"}  # 2.63: the other giga bosses follow (story_stage.py)


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
    import importlib
    from story_paint import save
    os.makedirs(OUT, exist_ok=True)
    files = []
    for boss, mod in SCENES.items():
        if names and boss not in names:
            continue
        written = importlib.import_module(mod).generate(review)
        for p in written:
            print(f"  {os.path.relpath(p, ASSETS)}  {os.path.getsize(p) // 1024} KiB", flush=True)
        files += written
        if review:
            stills = [os.path.join(OUT, f"{boss}_{i}.png") for i in range(1, 7)]
            contact_sheet(stills, os.path.join(review, f"story_{boss}.png"))
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


_SHEETS_OK = {}


def check_sheet(rel, meta):
    """A sprite sheet of a scene: RGBA, every frame of every clip inside it and not empty."""
    if rel in _SHEETS_OK:
        return []
    import numpy as np
    errs = []
    tp = os.path.join(ASSETS, meta["texture"])
    if not os.path.isfile(tp):
        return [f"{rel}: missing texture {meta['texture']}"]
    im = Image.open(tp)
    if im.mode != "RGBA":
        errs.append(f"{meta['texture']}: mode {im.mode} (expected RGBA)")
    a = np.array(im.convert("RGBA"))[:, :, 3]
    fw, fh = meta["frameWidth"], meta["frameHeight"]
    for name, c in meta["clips"].items():
        n = c["count"]
        durs = c.get("durations")
        if (durs is not None and (len(durs) != n or min(durs) <= 0)) or (durs is None and not c.get("fps", 0) > 0):
            errs.append(f"{rel}.{name}: bad timing")
        if c.get("next") and c["next"] not in meta["clips"]:
            errs.append(f"{rel}.{name}: next -> unknown clip {c['next']}")
        for f in range(n):
            x0, y0 = (c.get("start", 0) + f) * fw, c["row"] * fh
            if x0 + fw > a.shape[1] or y0 + fh > a.shape[0]:
                errs.append(f"{rel}.{name}: frame {f} outside its sheet")
            elif a[y0:y0 + fh, x0:x0 + fw].max() == 0:
                errs.append(f"{rel}.{name}: frame {f} is empty")
    _SHEETS_OK[rel] = not errs
    return errs


def check_stage(where, shot):
    """A shot's scene (version 2): its files (returned, to be counted in the budget) present, the
    clips its sprites play in their sheets, its keys within the shot (seconds of its French line)."""
    import json
    errs, files = [], []
    dur = max(4.5, 1.2 + len(shot["text"]) / 16.0)
    stage = shot["stage"]
    times = [k["t"] for k in stage.get("camera", [])] + [fl["t"] for fl in stage.get("flash", [])]
    times += [s["from"] for s in stage.get("shake", [])] + [s["to"] for s in stage.get("shake", [])]
    for el in stage.get("elements", []):
        keys = el.get("keys", [])
        if not keys:
            errs.append(f"{where}: {el.get('id')} has no keys")
        times += [k["t"] for k in keys]
        if "layer" in el:
            p = os.path.join(ASSETS, el["layer"])
            files.append(p)
            if not os.path.isfile(p):
                errs.append(f"{where}: missing layer {el['layer']}")
        elif "sprite" in el:
            jp = os.path.join(ASSETS, el["sprite"])
            if not os.path.isfile(jp):
                errs.append(f"{where}: missing sprite {el['sprite']}")
                continue
            meta = json.load(open(jp, encoding="utf-8"))
            files += [jp, os.path.join(ASSETS, meta["texture"])]
            errs += check_sheet(el["sprite"], meta)
            for k in keys:
                if "clip" in k and k["clip"] not in meta["clips"]:
                    errs.append(f"{where}: {el.get('id')}: no clip {k['clip']} in {el['sprite']}")
    bad = [t for t in times if not 0.0 <= t <= dur + 1e-6]
    if bad:
        errs.append(f"{where}: times {bad[:4]} outside the shot (0..{dur:.2f} s)")
    return errs, files


def check():
    """The paintings every story names: present, 960x540, within the size budget (with the files
    of the animated scenes)."""
    import json
    errs = []
    total = 0
    counted = set()
    sdir = os.path.join(os.path.dirname(ASSETS), "data", "stories")
    for f in sorted(os.listdir(sdir)) if os.path.isdir(sdir) else []:
        if not f.endswith(".json"):
            continue
        story = json.load(open(os.path.join(sdir, f), encoding="utf-8"))
        for n, shot in enumerate(story.get("shots", []), 1):
            if "stage" in shot:
                e2, files = check_stage(f"{f} shot {n}", shot)
                errs += e2
                for q in files:
                    if q not in counted and os.path.isfile(q):
                        counted.add(q)
                        total += os.path.getsize(q)
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
