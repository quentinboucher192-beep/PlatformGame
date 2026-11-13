"""Themed variants of every regular enemy for Pyralis (fire/volcanic) and
Glacior (ice): real palette swaps of the generated Sylvanox base sheets.

Every colour ramp of the base sprites (stone, moss, cyan glow, violet, amber,
danger accents, hides, membranes...) is mapped index-by-index to a ramp of the
planet palettes (CONTENT_CONTRACT section 1). Colours that are not an exact
ramp entry (hurt flashes, dissolve tints) are decomposed as a blend of two
known colours and the same blend is rebuilt from their themed counterparts,
so flashes and particles stay coherent. Frame size, pivot, clips and events
are copied from the base JSON (only the texture path changes).

Small themed touches: Pyralis sheets get a few drifting ember specks in their
looping clips; Glacior sheets get a frost rim on the top edges of solid parts.
"""
import json
import os

import numpy as np
from PIL import Image

from sheet import asset_path, save_json, save_png

# base id -> (pyralis id, glacior id)
VARIANTS = {
    "glowmite": ("ember_mite", "frost_mite"),
    "sentinel": ("forge_drone", "cryo_drone"),
    "sporeflower": ("cinder_bloom", "frost_bloom"),
    "stalker": ("lava_stalker", "snow_stalker"),
    "golem": ("magma_golem", "ice_golem"),
    "wisp": ("fire_wisp", "frost_wisp"),
    "nightwing": ("ash_wing", "frost_wing"),
    "gloop": ("magma_gloop", "ice_gloop"),
    "weaver": ("ember_weaver", "frost_weaver"),
    "bulwark": ("forge_bulwark", "frost_bulwark"),
    "bomber": ("forge_bomber", "cryo_bomber"),
    "burrower": ("lava_burrower", "ice_burrower"),
}

# (family, solid, [(sylvanox, pyralis, glacior), ...]); `solid` families get
# the Glacior frost rim.
FAMILIES = [
    ("outline", False, [("#0b0f1e", "#14090b", "#0a1428")]),
    ("sky", True, [
        ("#121a33", "#1e1416", "#13264a"), ("#1a2747", "#2c1e1f", "#1f3a66"),
        ("#22365a", "#3e2a29", "#2c5080"), ("#2e4a73", "#563b36", "#3d6a9e")]),
    ("stone", True, [  # ruin stone -> basalt / snowy steel
        ("#1d2b3a", "#1a1415", "#1c2638"), ("#2a3e4f", "#2a2122", "#2e3b52"), ("#3b5466", "#3e3132", "#45566f"),
        ("#577286", "#584645", "#6a7e98"), ("#7d97a8", "#7a6460", "#9aaec8")]),
    ("moss", True, [  # moss -> ash / frost
        ("#0f3b3a", "#241c1c", "#2f5a8e"), ("#135c50", "#3e3230", "#6a8fc0"), ("#1f8a6a", "#5e504c", "#a8c4e6"),
        ("#35b583", "#857670", "#e4f0ff")]),
    ("cyan", False, [  # bioluminescent cyan -> lava / frost cyan-white
        ("#1fb8b0", "#e0401c", "#3fa6e6"), ("#39f0d8", "#ff8a3d", "#7ff6ff"), ("#8ffff0", "#ffc56b", "#c4fbff"),
        ("#e6fffb", "#fff1c2", "#f4feff"), ("#167a80", "#8a2a10", "#2f6fb3")]),
    ("violet", False, [  # violet -> ember red / periwinkle ice (aurora-tinted)
        ("#6a2bb8", "#9a1c22", "#3a5ab8"), ("#b04cff", "#e0402a", "#6a9cff"), ("#e08aff", "#ff7a4a", "#b4d0ff"),
        ("#f6dcff", "#ffd2b0", "#eef4ff"), ("#3b1a6e", "#4a1414", "#1f2c6e"), ("#2a0f4a", "#2e0c0c", "#141c4a"),
        ("#160829", "#1a0606", "#0a0f2a"), ("#4a1f86", "#5e1818", "#2c3c90"), ("#2e1356", "#3a0e0e", "#1c2466")]),
    ("amber", False, [  # ancient-tech amber -> molten gold / ice-blue light
        ("#c8741f", "#d0601c", "#3f86d8"), ("#ffb347", "#ffa040", "#8fd4ff"), ("#ffd98a", "#ffd27a", "#d4f2ff"),
        ("#fff4d6", "#fff1c2", "#f4fcff"), ("#7a4a1c", "#6e2e16", "#2a4a7a"), ("#4a2c14", "#3e160c", "#1a2c4e")]),
    ("danger", False, [  # hostile accents: hot red / aurora magenta
        ("#a3123a", "#b3201a", "#7a1a8c"), ("#ff4d6d", "#ff3b2a", "#e05cff"), ("#ff8a3d", "#ff8a3d", "#9a9cff"),
        ("#ffe0e6", "#fff1c2", "#f6e0ff"), ("#ffd0c0", "#ffe0b0", "#e8e0ff"), ("#5a0a24", "#4a0c0a", "#3a0a4a")]),
    ("magenta", False, [  # stalker / nightwing eyes and mouths
        ("#ffd6f4", "#fff1c2", "#f0e6ff"), ("#ff5ccf", "#ffd23a", "#c48cff"), ("#b0208a", "#d0601c", "#6a3ab0"),
        ("#5a0f46", "#5a1a10", "#2a1a5a"), ("#2e0824", "#2e0c08", "#140c2e"), ("#6a1458", "#6a2010", "#3a2470")]),
    ("acid", False, [
        ("#2f7a1a", "#b3201a", "#2f6fb3"), ("#4fbf26", "#ff4d1e", "#5fb0ff"), ("#9dff3a", "#ff8a3d", "#a8e4ff"),
        ("#e4ffb0", "#fff1c2", "#eefaff")]),
    ("suit", True, [  # pale metal / bone -> rusted metal / snow white
        ("#e8eef5", "#f0d8b8", "#ffffff"), ("#c3d0de", "#d08a5a", "#dfe9f5"), ("#9fb3c8", "#a0583a", "#a8b8d0"),
        ("#6c819a", "#6e3b2a", "#6c7f9a")]),
    # --- species specific ramps ------------------------------------------
    ("goo", False, [  # gloop jelly -> magma / ice
        ("#a8f590", "#ffc56b", "#a8e4ff"), ("#5fd88a", "#ff8a3d", "#5fb0ff"), ("#2fae7c", "#e0401c", "#2f6fb3"),
        ("#1d7e66", "#a8201a", "#1f3a66"), ("#f6ffe8", "#fffbe8", "#ffffff"),
        ("#8fc4a0", "#a07060", "#a8b8d0"), ("#5f9a82", "#704a3e", "#7a8aa8"), ("#3f7468", "#4a2e28", "#4f5f80"),
        ("#2a5450", "#2e1a18", "#33405a"), ("#3f8a76", "#5a2b22", "#3d4a5e"), ("#246a5e", "#3a1d1c", "#2a3548"),
        ("#d4f6d8", "#f0d8b8", "#ffffff"), ("#8fd0a8", "#a0583a", "#c9d8ea"), ("#0f4a40", "#5a1a10", "#13264a"),
        ("#0a2f2c", "#2e0c08", "#0a1428"), ("#7a5a6a", "#6a3a2a", "#6a5a8a"), ("#4a3448", "#3a1d1c", "#3a3460")]),
    ("fur", True, [  # nightwing fur -> ash / frosted steel
        ("#8a78c0", "#9a847a", "#c9d8ea"), ("#62509a", "#6e5850", "#8a9ab0"), ("#443674", "#4a3834", "#5c6b80"),
        ("#2c2350", "#2e2020", "#3d4a5e"), ("#55458a", "#5e4a44", "#7a8aa0"), ("#3a2e66", "#40302c", "#56657a"),
        ("#272048", "#2a1e1c", "#3a4658")]),
    ("membrane", False, [  # nightwing wings -> ember red / ice blue
        ("#7c58c0", "#a83a24", "#5fa0e0"), ("#5e40a0", "#7e2a1c", "#3f78b8"), ("#452c7c", "#561c16", "#2a5288"),
        ("#523486", "#6a2418", "#3a6aa0"), ("#3e2668", "#4e1a12", "#2a4f80"), ("#2e1b52", "#36120e", "#1f3a66")]),
    ("crystal_dead", False, [
        ("#8a7aa6", "#8a6a60", "#9aa6c0"), ("#5e4f80", "#5e443e", "#6a7894"), ("#3b2f5c", "#3e2a28", "#46526a"),
        ("#271e40", "#281a1a", "#2c3448")]),
    ("flesh", False, [  # burrower belly + maw -> hot flesh / pale lavender
        ("#d69ac4", "#ffa070", "#c8d0ff"), ("#a8649e", "#d0583a", "#8c94d8"), ("#743e72", "#8a2a1c", "#5a5ea0"),
        ("#3a0f2e", "#3a0c08", "#1a1a4a"), ("#1e0818", "#1e0604", "#0c0c28")]),
    ("white", False, [("#ffffff", "#ffffff", "#ffffff")]),
]

# per-variant ramp overrides {variant: {sylvanox hex: themed hex}}
OVERRIDES = {
    # white-furred snow stalker with dark ice-blue stripes
    "snow_stalker": {
        "#3b5466": "#f2f8ff", "#2e4a73": "#c9d8ea", "#22365a": "#8a9ab0", "#1a2747": "#5c6b80",
        "#121a33": "#3d4a5e",
        "#1fb8b0": "#13264a", "#39f0d8": "#1f4a8a", "#8ffff0": "#2f6fb3", "#e6fffb": "#5fb0ff"},
    # construct knight sheathed in glacier ice (the base is already slate-blue)
    "frost_bulwark": {
        "#1d2b3a": "#13264a", "#2a3e4f": "#1f4a80", "#3b5466": "#2f6fb3", "#577286": "#5fa0dc",
        "#7d97a8": "#a8dcff"},
    # obsidian nucleus (Pyralis obsidian glints) so it reads inside the lava jelly
    "magma_gloop": {"#ff4d6d": "#9b4dca", "#a3123a": "#5b2a6e", "#ffe0e6": "#f0dcff"},
}

EMBER = [(255, 197, 107, 255), (255, 138, 61, 255), (255, 77, 30, 255)]
FROST = (238, 250, 255, 255)


def _h(c):
    c = c.lstrip("#")
    return (int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16))


def theme_table(theme, variant=None):
    """{sylvanox rgb: (themed rgb, solid)}"""
    col = 1 if theme == "pyralis" else 2
    table = {}
    for _, solid, rows in FAMILIES:
        for row in rows:
            table[_h(row[0])] = (_h(row[col]), solid)
    for src, dst in OVERRIDES.get(variant, {}).items():
        table[_h(src)] = (_h(dst), table.get(_h(src), (None, False))[1])
    return table


def _decompose(u, A, TA):
    """Best blend a + t (b - a) of two anchors reproducing `u`; returns the
    same blend of the themed anchors and the error."""
    u = np.asarray(u, np.float32)
    d = A[None, :, :] - A[:, None, :]                      # b - a  [i, j, 3]
    ll = (d * d).sum(-1)
    t = np.where(ll > 0, ((u - A)[:, None, :] * d).sum(-1) / np.maximum(ll, 1e-6), 0.0)
    t = np.clip(t, 0.0, 1.0)
    rec = A[:, None, :] + t[..., None] * d
    err = np.sqrt(((rec - u) ** 2).sum(-1))
    i, j = np.unravel_index(np.argmin(err), err.shape)
    out = TA[i] + t[i, j] * (TA[j] - TA[i])
    return tuple(int(round(float(x))) for x in np.clip(out, 0, 255)), float(err[i, j])


def swap(img, table):
    """Palette swap of an RGBA sheet. Returns (image, solid mask, max error)."""
    out = img.copy()
    solid = np.zeros(img.shape[:2], bool)
    opaque = img[:, :, 3] > 0
    keys = list(table.keys())
    A = np.array(keys, np.float32)
    TA = np.array([table[k][0] for k in keys], np.float32)
    rgb = img[:, :, :3]
    flat = rgb[opaque]
    uniq, inv = np.unique(flat.reshape(-1, 3), axis=0, return_inverse=True)
    new = np.zeros_like(uniq)
    is_solid = np.zeros(len(uniq), bool)
    worst = 0.0
    for k, c in enumerate(uniq):
        key = tuple(int(x) for x in c)
        if key in table:
            new[k] = table[key][0]
            is_solid[k] = table[key][1]
        else:
            col, err = _decompose(c, A, TA)
            worst = max(worst, err)
            new[k] = col
    res = out[opaque]
    res[:, :3] = new[inv.reshape(-1)]
    out[opaque] = res
    solid[opaque] = is_solid[inv.reshape(-1)]
    return out, solid, worst


def frost_rim(img, solid, outline):
    """Glacior touch: snow/frost catches on the top edge of solid parts."""
    h, w = img.shape[:2]
    above = np.zeros((h, w, 4), np.uint8)
    above[1:] = img[:-1]
    above_empty = (above[:, :, 3] == 0) | np.all(above[:, :, :3] == np.array(outline, np.uint8), axis=2)
    above_empty[0, :] = True
    lum = img[:, :, :3].astype(np.int32).sum(-1)
    sel = solid & above_empty & (img[:, :, 3] > 0) & (lum > 150)
    rim = img.copy()
    c = np.array(FROST[:3], np.float32)
    rim[sel, :3] = np.clip(img[sel, :3].astype(np.float32) * 0.35 + c * 0.65 + 0.5, 0, 255).astype(np.uint8)
    return rim


def embers(frame, k, n, seed):
    """Pyralis touch: a few ember specks drifting up around the sprite."""
    h, w = frame.shape[:2]
    a = frame[:, :, 3] > 0
    ys, xs = np.nonzero(a)
    if len(ys) == 0:
        return frame
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    rng = np.random.default_rng(seed)
    for e in range(2):
        bx = x0 + rng.random() * max(1, x1 - x0)
        by = y0 + rng.random() * max(1, (y1 - y0)) * 0.6
        ph = (k / n + rng.random()) % 1.0
        x = int(round(bx + 1.2 * np.sin(6.28 * ph + e)))
        y = int(round(by - 6.0 * ph))
        if 0 <= x < w and 0 <= y < h and frame[y, x, 3] == 0:
            frame[y, x] = EMBER[min(2, int(ph * 3))]
    return frame


LOOPING_TOUCH = ("idle", "move", "walk", "run")


def make_variant(base, vid, theme):
    meta = json.load(open(asset_path(f"animations/{base}.json")))
    img = np.array(Image.open(asset_path(meta["texture"])).convert("RGBA"))
    table = theme_table(theme, vid)
    out, solid, worst = swap(img, table)
    fw, fh = meta["frameWidth"], meta["frameHeight"]
    if theme == "glacior":
        out = frost_rim(out, solid, table[_h("#0b0f1e")][0])
    else:
        for name, c in meta["clips"].items():
            if name not in LOOPING_TOUCH:
                continue
            for k in range(c["count"]):
                x0, y0 = (c.get("start", 0) + k) * fw, c["row"] * fh
                fr = out[y0:y0 + fh, x0:x0 + fw]
                embers(fr, k, c["count"], seed=hash_seed(vid, name))
    tex = f"sprites/enemies/{vid}.png"
    save_png(out, tex)
    meta2 = dict(meta)
    meta2["texture"] = tex
    save_json(meta2, f"animations/{vid}.json")
    return worst


def hash_seed(*parts):
    s = 0
    for p in parts:
        for ch in p:
            s = (s * 131 + ord(ch)) % 1000003
    return s


def build(verbose=False):
    for base, (pyr, gla) in VARIANTS.items():
        for vid, theme in ((pyr, "pyralis"), (gla, "glacior")):
            worst = make_variant(base, vid, theme)
            if verbose:
                print(f"    {vid:15s} <- {base:12s} ({theme}) max blend error {worst:.1f}")


if __name__ == "__main__":
    import sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    build(verbose=True)
