"""Nova's suits (2.19 cosmetics): recoloured copies of sprites/player/nova.png.

Same layout, frames and pivot as nova.png (the game swaps the texture of the "nova"
animation set, display only). Each pixel of the source sheet is classified once:
outline (very dark), visor/energy accent (saturated cyan), scarf (saturated red/orange)
or suit (the rest), then recoloured toward the suit's three target colours while keeping
its lightness, so the shading of the original drawing survives.

Run:  python3 tools/assetgen/characters/generate.py nova_suits
      python3 tools/assetgen/characters/nova_suits.py nebuleuse abysse   # only these suits
"""
import colorsys
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
SRC = os.path.join(ROOT, "assets", "sprites", "player", "nova.png")

# id -> (suit colour, accent colour, scarf colour). Ids match data/cosmetics/cosmetics.json.
SUITS = {
    "givre": ((200, 225, 255), (120, 200, 255), (235, 245, 255)),
    "braise": ((230, 200, 185), (255, 150, 60), (220, 40, 40)),
    "seve": ((205, 235, 205), (120, 255, 150), (176, 90, 255)),
    "noyau": ((70, 80, 120), (255, 210, 90), (255, 200, 80)),
    "spectre": ((205, 190, 250), (190, 120, 255), (120, 230, 255)),
    "or": ((255, 225, 140), (255, 255, 235), (200, 30, 60)),
    "echo": ((60, 60, 75), (255, 70, 100), (255, 70, 100)),
    # 2.24
    "nebuleuse": ((235, 150, 215), (90, 230, 255), (150, 80, 255)),
    "abysse": ((55, 125, 150), (80, 255, 190), (255, 120, 100)),
    "eruption": ((105, 70, 65), (255, 120, 30), (255, 215, 60)),
    "sanctuaire": ((236, 233, 240), (190, 150, 255), (255, 200, 70)),
    # 2.34: a reward of the challenges (ten golds)
    "champion": ((140, 20, 48), (255, 205, 80), (245, 245, 255)),
    # 2.48: Pyrrhax, le Dragon d'Obsidienne (black glass veined with fire)
    "obsidienne": ((52, 44, 60), (255, 110, 40), (190, 40, 110)),
    # 2.49: le Monolithe, le Golem Primordial (speckled granite, amber runes, moss)
    "granit": ((128, 122, 116), (255, 178, 70), (110, 150, 80)),
}


def _classify(r, g, b):
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    if l < 0.13:
        return "outline", h, l, s
    if s > 0.45 and 0.40 < h < 0.55:
        return "accent", h, l, s
    if s > 0.45 and (h < 0.08 or h > 0.95):
        return "scarf", h, l, s
    return "suit", h, l, s


def _hls(c):
    return colorsys.rgb_to_hls(c[0] / 255, c[1] / 255, c[2] / 255)


def recolor(img, suit, accent, scarf, suit_mix=0.55):
    out = img.copy()
    src = img.load()
    dst = out.load()
    th_s, tl_s, ts_s = _hls(suit)
    targets = {"accent": _hls(accent), "scarf": _hls(scarf)}
    cache = {}
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            p = src[x, y]
            if p[3] == 0:
                continue
            key = p[:3]
            if key not in cache:
                kind, h, l, s = _classify(*key)
                if kind == "outline":
                    cache[key] = key
                elif kind == "suit":
                    nl = l * (1 - suit_mix) + l * tl_s / 0.75 * suit_mix
                    rgb = colorsys.hls_to_rgb(th_s, min(0.96, nl), ts_s * 0.9)
                    cache[key] = tuple(int(round(c * 255)) for c in rgb)
                else:
                    th, tl, ts = targets[kind]
                    rgb = colorsys.hls_to_rgb(th, max(0.15, min(0.95, l * (tl / 0.55))), max(ts, 0.6))
                    cache[key] = tuple(int(round(c * 255)) for c in rgb)
            r, g, b = cache[key]
            dst[x, y] = (r, g, b, p[3])
    return out


def build(only=None):
    """Every suit, or only the ids in `only` (the others' sheets are left as they are)."""
    base = Image.open(SRC).convert("RGBA")
    out_dir = os.path.join(ROOT, "assets", "sprites", "player", "suits")
    os.makedirs(out_dir, exist_ok=True)
    for sid, (suit, accent, scarf) in SUITS.items():
        if only and sid not in only:
            continue
        recolor(base, suit, accent, scarf).save(os.path.join(out_dir, f"nova_{sid}.png"), optimize=True)


if __name__ == "__main__":
    unknown = [s for s in sys.argv[1:] if s not in SUITS]
    if unknown:
        sys.exit("combinaisons inconnues : " + ", ".join(unknown))
    build(sys.argv[1:] or None)
