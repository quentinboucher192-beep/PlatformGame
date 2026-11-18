#!/usr/bin/env python3
"""Environment asset generator for Cosmic Platformer (planets Sylvanox, Pyralis, Glacior).

Run from the repository root:  python3 tools/assetgen/environment/generate.py
Optional args: names of steps to run (tiles platforms backgrounds bg_pyralis bg_glacior decor
decor_pyralis decor_glacior ui glyphs planets banners bg_aethra aethra_planet confins_planet stories raids),
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
  backgrounds/aethra/{sky,far,mid,near,abyss,fg_petals}.png                   (bg_aethra)
  textures/tiles/{sanctum_stone,nebula_rock}.png/.json (Aethra, in tiles)
  ui/planets/aethra.png, ui/planets/aethra_banner.png                         (aethra_planet)
  ui/planets/confins.png, ui/planets/confins_banner.png                       (confins_planet)
  stories/{singularis,pyrrhax,monolithe,kraken,phenix,liche,titan}_{1..6}.png (giga boss stories, 2.51; kraken 2.53, phenix 2.56, liche 2.57, titan 2.58)  (stories)
  stories/<boss>/*.png, animations/stories/<boss>_*.json, data/stories/<boss>.json (animated scenes: pyrrhax 2.60, the six others 2.63)  (stories)
  ui/raids/asterion.png/.json (the titan of the clan raids in layers, its states and hit masks)  (raids)
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
    "bg_aethra": ("bg_aethra", "generate"),
    "aethra_planet": ("planet_aethra", "generate"),
    "confins_planet": ("planet_confins", "generate"),
    "stories": ("stories", "generate"),
    "raids": ("raid_asterion", "generate"),
    "raid_film": ("story_asterion", "generate"),  # 2.68: Asterion's film (after the titan's sprite)
}


def load_step(name):
    import importlib
    mod, fn = MODULES[name]
    return getattr(importlib.import_module(mod), fn)


ORDER = ["tiles", "platforms", "backgrounds", "bg_pyralis", "bg_glacior", "decor", "decor_pyralis",
         "decor_glacior", "ui", "glyphs", "planets", "banners", "bg_aethra", "aethra_planet",
         "confins_planet", "stories", "raids", "raid_film"]


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
