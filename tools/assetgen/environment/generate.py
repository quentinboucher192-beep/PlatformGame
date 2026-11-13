#!/usr/bin/env python3
"""Environment asset generator for Cosmic Platformer (planets Sylvanox, Pyralis, Glacior).

Run from the repository root:  python3 tools/assetgen/environment/generate.py
Optional args: names of steps to run (tiles platforms backgrounds bg_pyralis bg_glacior decor
decor_pyralis decor_glacior ui glyphs planets banners),
--review to also write upscaled review sheets to /tmp/claude-0/assetreview/environment/,
--no-validate to skip the contract checker.

Produces (paths relative to assets/):
  textures/tiles/{ruin_stone,ancient_metal,crystal,root}.png/.json            (tiles)
  textures/tiles/{basalt,obsidian,forge_metal,glacier_ice,snow_rock,frost_metal}.png/.json
  textures/tiles/platforms.png/.json (7 styles)                               (platforms)
  backgrounds/sylvanox/{sky,far,mid,near,abyss,fg_vines}.png                  (backgrounds)
  backgrounds/pyralis/{sky,far,mid,near,abyss,fg_embers}.png                  (bg_pyralis)
  backgrounds/glacior/{sky,far,mid,near,abyss,fg_icicles}.png                 (bg_glacior)
  sprites/decor/{sylvanox,pyralis,glacior}_decor.png/.json                    (decor*)
  ui/icons_powers.png (10 icons), ui/icons_medals.png, ui/icons_hud.png       (ui)
  ui/glyphs/{xbox,playstation,generic}.png                                    (glyphs)
  ui/planets/{sylvanox,pyralis,glacior,sun,sylvanox_banner}.png, ui/map_background.png
  ui/planets/{pyralis,glacior}_banner.png                                     (banners)
All generators are deterministic (fixed seeds).
"""
import os
import sys

sys.dont_write_bytecode = True  # keep the source tree free of __pycache__

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import helpers  # noqa: E402,F401  (sets up sys.path for common.palette)

MODULES = {
    "tiles": ("tiles", "generate"),
    "platforms": ("platforms", "generate"),
    "backgrounds": ("backgrounds", "generate"),
    "bg_pyralis": ("bg_pyralis", "generate"),
    "bg_glacior": ("bg_glacior", "generate"),
    "decor": ("decor", "generate"),
    "decor_pyralis": ("decor_pyralis", "generate"),
    "decor_glacior": ("decor_glacior", "generate"),
    "ui": ("ui_icons", "generate"),
    "glyphs": ("glyphs", "generate"),
    "planets": ("planets", "generate"),
    "banners": ("banners", "generate"),
}


def load_step(name):
    import importlib
    mod, fn = MODULES[name]
    return getattr(importlib.import_module(mod), fn)


ORDER = ["tiles", "platforms", "backgrounds", "bg_pyralis", "bg_glacior", "decor", "decor_pyralis",
         "decor_glacior", "ui", "glyphs", "planets", "banners"]


def main(argv):
    review = "--review" in argv
    validate = "--no-validate" not in argv
    names = [a for a in argv if not a.startswith("--")] or ORDER
    for n in names:
        if n not in MODULES:
            print(f"unknown step {n}")
            return 2
        files = load_step(n)()
        print(f"[{n}] {len(files)} files")
    if review:
        import preview
        for p in preview.review_all(names):
            print("review:", p)
    if validate:
        import validate as V
        errs = V.check()
        if errs:
            print("VALIDATION FAILED:")
            for e in errs:
                print("  -", e)
            return 1
        print("validation OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
