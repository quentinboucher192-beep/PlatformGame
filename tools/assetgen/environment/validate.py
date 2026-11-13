"""Contract checker for the environment assets (ASSET_SPEC sections 7, 8, 9, 11 and the v2
content contract: Pyralis / Glacior tilesets, platforms rows 3-6, backgrounds, decor atlases,
10 power icons, planet banners).

Run: python3 tools/assetgen/environment/validate.py   (exit code 1 on failure)
"""
import json
import os
import sys

sys.dont_write_bytecode = True  # keep the source tree free of __pycache__

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from helpers import ASSETS  # noqa: E402

MATERIALS = ["ruin_stone", "ancient_metal", "crystal", "root",
             "basalt", "obsidian", "forge_metal",              # Pyralis
             "glacier_ice", "snow_rock", "frost_metal"]        # Glacior
FRICTION = {"glacier_ice": 0.25}          # required "friction" values (others optional, 0..1)
PLATFORM_STYLES = {"ruin_bridge": 0, "tech_ledge": 1, "root_branch": 2, "basalt_ledge": 3,
                   "chain_grate": 4, "ice_ledge": 5, "snow_bridge": 6}
BACKGROUNDS = {"sylvanox": ["sky", "far", "mid", "near", "abyss", "fg_vines"],
               "pyralis": ["sky", "far", "mid", "near", "abyss", "fg_embers"],
               "glacior": ["sky", "far", "mid", "near", "abyss", "fg_icicles"]}
DECOR_REQUIRED = ["mushroom_small", "mushroom_tall", "mushroom_cluster", "glow_fern", "glow_flowers",
                  "crystal_cluster", "crystal_big", "ruin_pillar", "ruin_pillar_broken", "ruin_arch",
                  "statue_head", "tech_panel", "lantern_pod", "hanging_vine_short",
                  "hanging_vine_long", "roots", "rock_small", "rock_big", "floating_rock",
                  "holo_glyph", "pipe_broken", "banner_torn", "grass_tuft", "bones_ancient"]
DECOR_FRAMES = {"glow_flowers": 4, "tech_panel": 4, "lantern_pod": 6, "holo_glyph": 6}
TOP_PIVOT = ["lantern_pod", "hanging_vine_long"]
# per decor atlas: (required names, animated frame counts, sprites that must hang from the top)
DECOR_ATLASES = {
    "sylvanox": (DECOR_REQUIRED, DECOR_FRAMES, TOP_PIVOT),
    "pyralis": (["lava_vent", "basalt_column", "basalt_column_broken", "obsidian_spire",
                 "forge_anvil", "forge_brazier", "hanging_chain", "hanging_chain_long", "lava_fall",
                 "fire_plant", "slag_pile", "gear_wreck", "skull_rock", "ember_crystal",
                 "steam_pipe", "burnt_banner", "ash_rock_small", "ash_rock_big"],
                {"lava_vent": 6, "forge_brazier": 6, "lava_fall": 6, "fire_plant": 4},
                ["hanging_chain", "hanging_chain_long"]),
    "glacior": (["ice_spire", "icicles", "icicles_long", "snow_drift", "frozen_tree", "snow_pine",
                 "ice_crystal", "ice_crystal_big", "frozen_statue", "aurora_lamp", "frost_flower",
                 "ice_arch", "frozen_machine", "frost_banner", "frozen_bones", "snow_rock_small",
                 "snow_rock_big", "ice_floating"],
                {"aurora_lamp": 6, "frost_flower": 4},
                ["icicles", "icicles_long"]),
}
DECOR_GLOW = {"pyralis": ["lava_vent", "forge_brazier", "lava_fall", "fire_plant", "ember_crystal"],
              "glacior": ["ice_crystal", "ice_crystal_big", "aurora_lamp", "frost_flower"]}
SIZES = {
    "textures/tiles/platforms.png": (48, 16 * len(PLATFORM_STYLES)),
    "ui/icons_powers.png": (320, 32),
    "ui/icons_medals.png": (160, 32),
    "ui/icons_hud.png": (176, 16),
    "ui/glyphs/xbox.png": (608, 32),
    "ui/glyphs/playstation.png": (608, 32),
    "ui/glyphs/generic.png": (608, 32),
    "ui/planets/sylvanox.png": (256, 256),
    "ui/planets/pyralis.png": (256, 256),
    "ui/planets/glacior.png": (256, 256),
    "ui/planets/sun.png": (256, 256),
    "ui/planets/sylvanox_banner.png": (480, 270),
    "ui/planets/pyralis_banner.png": (480, 270),
    "ui/planets/glacior_banner.png": (480, 270),
    "ui/map_background.png": (960, 540),
}
for _m in MATERIALS:
    SIZES[f"textures/tiles/{_m}.png"] = (256, 64)
for _p, _names in BACKGROUNDS.items():
    for _n in _names:
        SIZES[f"backgrounds/{_p}/{_n}.png"] = (640, 360)


def load(rel):
    return np.array(Image.open(os.path.join(ASSETS, rel)).convert("RGBA"))


def cells_nonempty(img, cw, ch, n, errs, what):
    for i in range(n):
        x, y = (i * cw) % img.shape[1], ((i * cw) // img.shape[1]) * ch
        if not (img[y:y + ch, x:x + cw, 3] > 0).any():
            errs.append(f"{what}: cell {i} is empty")


def seam_ok(img):
    """Horizontal seamlessness: the wrap-around column step (640 -> 0), measured on 8-row
    block averages to ignore dithering, must be no larger than a typical neighbouring-column
    step of the same image."""
    a = img.astype(np.float32)
    f = np.concatenate([a[..., :3] * (a[..., 3:4] / 255.0), a[..., 3:4]], -1)
    k = 8
    h = f.shape[0] // k * k
    f = f[:h].reshape(h // k, k, f.shape[1], 4).mean(1)
    g = np.concatenate([f, f[:, :1]], 1)
    d = np.abs(g[:, 1:] - g[:, :-1]).mean(axis=(0, 2))
    wrap, med = float(d[-1]), float(np.median(d[:-1]))
    limit = max(med * 1.5 + 0.5, float(np.percentile(d[:-1], 95)))
    return wrap <= limit, wrap, med


def check():
    errs = []
    # sizes / existence
    for rel, (w, h) in SIZES.items():
        p = os.path.join(ASSETS, rel)
        if not os.path.exists(p):
            errs.append(f"missing {rel}")
            continue
        im = Image.open(p)
        if im.size != (w, h):
            errs.append(f"{rel}: size {im.size} != {(w, h)}")
        if im.mode != "RGBA":
            errs.append(f"{rel}: mode {im.mode} (expected RGBA)")
    # tilesets
    for m in MATERIALS:
        jp = os.path.join(ASSETS, f"textures/tiles/{m}.json")
        if not os.path.exists(jp):
            errs.append(f"missing textures/tiles/{m}.json")
            continue
        j = json.load(open(jp))
        exp = {"texture": f"textures/tiles/{m}.png", "tileSize": 16, "maskRow": 0, "variantRow": 1,
               "variantCount": 16, "innerCornerRow": 2, "topDecorRow": 3, "topDecorCount": 8,
               "topDecorOffsetY": -12}
        for k, v in exp.items():
            if j.get(k) != v:
                errs.append(f"{m}.json: {k}={j.get(k)!r} expected {v!r}")
        mc = j.get("mapColor", "")
        if not (isinstance(mc, str) and mc.startswith("#") and len(mc) == 7):
            errs.append(f"{m}.json: bad mapColor {mc!r}")
        if m in FRICTION and j.get("friction") != FRICTION[m]:
            errs.append(f"{m}.json: friction={j.get('friction')!r} expected {FRICTION[m]!r}")
        if "friction" in j and not (isinstance(j["friction"], (int, float)) and 0 <= j["friction"] <= 1):
            errs.append(f"{m}.json: friction must be a number in [0, 1]")
        if not os.path.exists(os.path.join(ASSETS, j.get("texture", ""))):
            errs.append(f"{m}.json: texture not found")
            continue
        a = load(j["texture"])
        for mask in range(16):
            cell = a[0:16, mask * 16:(mask + 1) * 16]
            if (cell[..., 3] > 0).sum() < 200:
                errs.append(f"{m}: mask cell {mask} mostly empty")
        if (a[16:32, :, 3] < 255).any():
            errs.append(f"{m}: variant row must be fully opaque (mask 15 interiors)")
        if (a[0:16, 15 * 16:256, 3] < 255).any():
            errs.append(f"{m}: mask 15 cell must be fully opaque")
        cells_nonempty(a[32:48, 0:64], 16, 16, 4, errs, f"{m} inner corners")
        if (a[32:48, 64:, 3] > 0).any():
            errs.append(f"{m}: row 2 cols 4-15 must be transparent")
        cells_nonempty(a[48:64, 0:128], 16, 16, 8, errs, f"{m} top decor")
        if (a[48:64, 128:, 3] > 0).any():
            errs.append(f"{m}: row 3 cols 8-15 must be transparent")
        # seamless autotile: an edge shared with a same-material neighbour must look exactly
        # like that edge in the corresponding "middle" cell (so any two connected cells match)
        def cell(k):
            return a[0:16, k * 16:(k + 1) * 16]
        for mask in range(16):
            ref_h = cell((mask & 5) | 2 | 8)       # same up/down exposure, both sides connected
            ref_v = cell((mask & 10) | 1 | 4)      # same left/right exposure, up+down connected
            if mask & 2 and not np.array_equal(cell(mask)[:, 15], ref_h[:, 15]):
                errs.append(f"{m}: mask {mask} right edge does not match its neighbours")
            if mask & 8 and not np.array_equal(cell(mask)[:, 0], ref_h[:, 0]):
                errs.append(f"{m}: mask {mask} left edge does not match its neighbours")
            if mask & 4 and not np.array_equal(cell(mask)[15], ref_v[15]):
                errs.append(f"{m}: mask {mask} bottom edge does not match its neighbours")
            if mask & 1 and not np.array_equal(cell(mask)[0], ref_v[0]):
                errs.append(f"{m}: mask {mask} top edge does not match its neighbours")
    # platforms
    pj = os.path.join(ASSETS, "textures/tiles/platforms.json")
    if os.path.exists(pj):
        j = json.load(open(pj))
        if j != {"texture": "textures/tiles/platforms.png", "tileSize": 16,
                 "styles": PLATFORM_STYLES}:
            errs.append(f"platforms.json content mismatch: {j}")
        a = load("textures/tiles/platforms.png")
        for row in range(min(len(PLATFORM_STYLES), a.shape[0] // 16)):
            for c in range(3):
                cell = a[row * 16:(row + 1) * 16, c * 16:(c + 1) * 16]
                if not (cell[0:2, 2:14, 3] > 0).all():
                    errs.append(f"platforms row {row} col {c}: walkable top rows not solid")
            lft = a[row * 16:(row + 1) * 16, 0:16]
            mid = a[row * 16:(row + 1) * 16, 16:32]
            rgt = a[row * 16:(row + 1) * 16, 32:48]
            if not np.array_equal(lft[:, 15], mid[:, 15]) or not np.array_equal(rgt[:, 0], mid[:, 0]):
                errs.append(f"platforms row {row}: caps do not join the middle seamlessly")
    else:
        errs.append("missing textures/tiles/platforms.json")
    # backgrounds
    for planet, names in BACKGROUNDS.items():
        for n in names:
            rel = f"backgrounds/{planet}/{n}.png"
            if not os.path.exists(os.path.join(ASSETS, rel)):
                continue
            a = load(rel)
            if n == "sky":
                if (a[..., 3] < 255).any():
                    errs.append(f"{rel} must be opaque")
            elif not (a[..., 3] < 255).any():
                errs.append(f"{rel} must have transparency")
            ok, wrap, med = seam_ok(a)
            if not ok:
                errs.append(f"{rel}: horizontal seam (wrap diff {wrap:.2f}, median adj {med:.2f})")
        rel = f"backgrounds/{planet}/abyss.png"
        if os.path.exists(os.path.join(ASSETS, rel)) and load(rel)[:40, :, 3].any():
            errs.append(f"{rel} should be transparent at the top")
    # decor atlases (names must be unique across all atlases)
    seen = {}
    for planet, (required, frames_req, top_pivot) in DECOR_ATLASES.items():
        rel_j = f"sprites/decor/{planet}_decor.json"
        dj = os.path.join(ASSETS, rel_j)
        if not os.path.exists(dj):
            errs.append(f"missing {rel_j}")
            continue
        j = json.load(open(dj))
        if j.get("texture") != f"sprites/decor/{planet}_decor.png":
            errs.append(f"{rel_j}: texture path wrong")
        a = load(f"sprites/decor/{planet}_decor.png")
        H, W = a.shape[:2]
        sprites = j.get("sprites", {})
        for n in required:
            if n not in sprites:
                errs.append(f"{planet} decor: missing sprite {n}")
        for n in DECOR_GLOW.get(planet, []):
            if n in sprites and "glow" not in sprites[n]:
                errs.append(f"{planet} decor {n}: glow required")
        rects = []
        for n, e in sprites.items():
            if n in seen:
                errs.append(f"decor name {n} used by both {seen[n]} and {planet}")
            seen[n] = planet
            x, y, w, h = e["rect"]
            fr = e.get("frames", 1)
            if fr != frames_req.get(n, 1):
                errs.append(f"{planet} decor {n}: frames {fr} expected {frames_req.get(n, 1)}")
            if fr > 1 and not e.get("fps"):
                errs.append(f"{planet} decor {n}: animated but fps is 0")
            if x < 0 or y < 0 or x + w * fr > W or y + h > H:
                errs.append(f"{planet} decor {n}: rect out of bounds")
                continue
            px, py = e["pivot"]
            if not (0 <= px <= w and 0 <= py <= h):
                errs.append(f"{planet} decor {n}: pivot outside frame")
            if n in top_pivot and (py != 0 or px != w // 2):
                errs.append(f"{planet} decor {n}: pivot must be top-centre")
            for f in range(fr):
                if not (a[y:y + h, x + f * w:x + (f + 1) * w, 3] > 0).any():
                    errs.append(f"{planet} decor {n}: frame {f} empty")
            if "glow" in e:
                if not (isinstance(e.get("glowRadius"), (int, float)) and len(e.get("glowOffset", [])) == 2):
                    errs.append(f"{planet} decor {n}: incomplete glow fields")
            rects.append((n, x, y, w * fr, h))
        for i in range(len(rects)):
            for k in range(i + 1, len(rects)):
                n1, x1, y1, w1, h1 = rects[i]
                n2, x2, y2, w2, h2 = rects[k]
                if x1 < x2 + w2 and x2 < x1 + w1 and y1 < y2 + h2 and y2 < y1 + h1:
                    errs.append(f"{planet} decor: {n1} overlaps {n2}")
    # ui cells
    for rel, cw, ch, n in [("ui/icons_powers.png", 32, 32, 10), ("ui/icons_medals.png", 32, 32, 5),
                           ("ui/icons_hud.png", 16, 16, 11), ("ui/glyphs/xbox.png", 32, 32, 19),
                           ("ui/glyphs/playstation.png", 32, 32, 19), ("ui/glyphs/generic.png", 32, 32, 19)]:
        if os.path.exists(os.path.join(ASSETS, rel)):
            cells_nonempty(load(rel), cw, ch, n, errs, rel)
    for p in ["sylvanox", "pyralis", "glacior", "sun"]:
        rel = f"ui/planets/{p}.png"
        if os.path.exists(os.path.join(ASSETS, rel)):
            a = load(rel)
            if a[0, 0, 3] or a[-1, -1, 3] or a[0, -1, 3] or a[-1, 0, 3]:
                errs.append(f"{rel}: corners should be transparent")
    for rel in ["ui/planets/sylvanox_banner.png", "ui/planets/pyralis_banner.png",
                "ui/planets/glacior_banner.png", "ui/map_background.png"]:
        if os.path.exists(os.path.join(ASSETS, rel)) and (load(rel)[..., 3] < 255).any():
            errs.append(f"{rel}: should be opaque")
    return errs


if __name__ == "__main__":
    e = check()
    for x in e:
        print("-", x)
    print("OK" if not e else f"{len(e)} problem(s)")
    sys.exit(1 if e else 0)
