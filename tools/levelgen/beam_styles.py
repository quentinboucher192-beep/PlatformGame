#!/usr/bin/env python3
"""Beam textures of the guardians' lasers and breaths (version 2.8, appearance only: not part
of the level fingerprints). Writes "beamTexture" / "beamWidth" into data/enemies/<boss>.json.
Run from the repository root before stellar_bosses.py (the echoes copy their originals):
    python3 tools/levelgen/beam_styles.py
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STYLES = {
    "thornwarden": ("thorn", 14), "archon_catacombs": ("thorn", 16), "prime_sentinel": ("plasma", 14),
    "cryowyrm": ("frost", 16), "howling_wyrm": ("frost", 18), "glaciarch": ("frost", 14),
    "magmaroth": ("flame", 16), "crucible_heart": ("flame", 18), "ignivore": ("flame", 16), "forge_core": ("flame", 16),
}

for boss, (style, width) in STYLES.items():
    path = os.path.join(ROOT, "data", "enemies", boss + ".json")
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    n = 0
    for a in d["attacks"]:
        if a.get("type") == "laser":
            a["beamTexture"] = style
            if width != 14:
                a["beamWidth"] = width
            n += 1
    with open(path, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print(boss, style, n, "rayon(s)")
