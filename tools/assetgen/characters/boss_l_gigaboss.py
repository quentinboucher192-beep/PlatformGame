"""Drawing helpers of the gigaboss sprites SKULLMAW (boss_l_skullmaw.py), DREADFANG
(boss_l_dreadfang.py) and HEARTBREAKER (boss_l_heartbreaker.py), 2.69 (phase 4B, docs/RAIDS.md §13),
over the shared kit of the gigaboss of phase 4B (gigaboss_kit.py: packing, PNG, writing, `--check`).

Each generator gives: its boss in three phase sheets (one per phase of data/enemies/<id>.json, its
look changing with its health: intact, damaged, broken), its weak points, and its effects sheet
(<id>_fx, 48x144 frames like the kit's: the plates of its mechanism, the marks of its blows, its
blades and waves) drawn by the raid's combat screen over the arena.

`--check` renders everything twice and compares with the files on disk (tests/test_raid_gigaboss.cpp);
`--review out.png` writes the labelled contact sheet (French captions): one picture per clip of each
phase, the weak points and the effects sheet.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import gigaboss_kit as kit
from gigaboss_kit import pack, png as _png  # noqa: F401  (re-exported for the three generators)
from sheet import ASSETS

_blob = kit.blob
FX_W, FX_H = kit.FX_W, kit.FX_H
W, H = 224, 224
GY = 212
PIVOT = [W // 2, GY]
COLUMNS = 14


class Pose:
    """A frame's pose: where it is (dx, dy), its tilt, its glow (-1..1), a flash, a burst of its
    energy (rings, shards), dust, and the boss's own numbers in `k` (jaw, curl, ribs, beat…)."""

    def __init__(self, **k):
        self.dx = k.pop("dx", 0.0)
        self.dy = k.pop("dy", 0.0)
        self.tilt = k.pop("tilt", 0.0)
        self.glow = k.pop("glow", 0.0)
        self.flash = k.pop("flash", 0.0)
        self.burst = k.pop("burst", 0.0)
        self.dust = k.pop("dust", 0.0)
        self.fade = k.pop("fade", 0.0)   # death: its light going out (0..1)
        self.seed = k.pop("seed", 0)
        self.k = k

    def get(self, name, default=0.0):
        return self.k.get(name, default)


def T(*d):
    return list(d)


def wave(t, a=1.0):
    return math.sin(t * 2 * math.pi) * a


def flash(img, k):
    """Every opaque pixel pulled toward white by `k`."""
    if k <= 0:
        return img
    out = img.copy()
    a = out[:, :, 3] > 0
    rgb = out[:, :, :3].astype(np.float32)
    rgb[a] = rgb[a] + (255 - rgb[a]) * k
    out[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return out


def halo(img, color, density, seed, rings=1, floor=GY):
    """A broken glow hugging the silhouette (its energy), `rings` pixels wide."""
    out = img.copy()
    rng = np.random.RandomState(seed)
    a = out[:, :, 3] > 0
    g = a.copy()
    for _ in range(rings):
        n = g.copy()
        n[1:] |= g[:-1]
        n[:-1] |= g[1:]
        n[:, 1:] |= g[:, :-1]
        n[:, :-1] |= g[:, 1:]
        g = n
    rim = g & ~a
    rim[floor:] = False
    out[rim & (rng.uniform(0, 1, rim.shape) < density)] = color
    return out


def sparks(img, center, radius, n, colors, seed, squash=0.7):
    """A ring of motes thrown out from `center` (a burst, a roar)."""
    rng = np.random.RandomState(seed)
    cx, cy = center
    for k in range(n):
        ang = k / max(1, n) * 2 * math.pi + rng.uniform(-0.2, 0.2)
        d = radius + rng.uniform(-6, 6)
        _blob(img, int(cx + math.cos(ang) * d), int(cy + math.sin(ang) * d * squash), 1 + int(rng.uniform(0, 2)), colors[k % len(colors)])
    return img


def trim(img):
    """Nothing under the ground line nor on the frame's border."""
    img[GY + 3:, :, :] = 0
    img[:2, :, :] = 0
    img[:, :2, :] = 0
    img[:, -2:, :] = 0
    return img


def boss_outputs(name, draw, clips, extras):
    """The three phase sheets of `name` (draw(pose, phase) -> frame), then `extras` ((stem, (img,
    meta), fw, fh, pivot)…): (relative path, bytes) of every file, in a fixed order."""
    out = []
    for phase in range(3):
        img, meta = pack([(clip, [draw(p, phase) for p in fn()], kw) for clip, fn, kw in clips], W, H, COLUMNS)
        out += kit.sheet_entry(name if phase == 0 else "%s_p%d" % (name, phase + 1), img, meta, W, H, PIVOT)
    for stem, (img, meta), fw, fh, pivot in extras:
        out += kit.sheet_entry(stem, img, meta, fw, fh, pivot)
    return out


def fitted(d, text, font, width):
    """`text` cut (with « … ») to fit `width` pixels: no caption runs into its neighbour."""
    if d.textlength(text, font=font) <= width:
        return text
    while text and d.textlength(text + "…", font=font) > width:
        text = text[:-1]
    return text.rstrip() + "…"


def review(path, title, phases, picks, draw, extras, bg=(20, 16, 30, 255)):
    """A contact sheet: for each phase one frame of each clip (captioned in French), then the
    weak points and the sheet of its rules (their own captions)."""
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 12)
    big = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 18)
    cell, label, per, head = W, 22, 7, 26
    rows = (len(picks) + per - 1) // per
    top = 34
    extra_h = sum(label + img.shape[0] * scale + 10 for _, img, scale in extras)
    sheet = Image.new("RGBA", (per * cell, top + 3 * (head + rows * (cell + label)) + extra_h), bg)
    d = ImageDraw.Draw(sheet)
    d.text((6, 6), fitted(d, title, big, sheet.width - 12), fill=(255, 236, 200, 255), font=big)
    for k in range(3):
        y0 = top + k * (head + rows * (cell + label))
        d.text((6, y0 + 4), "Phase %d · %s" % (k + 1, phases[k]), fill=(201, 168, 255, 255), font=big)
        for i, (caption, pose) in enumerate(picks):
            x, y1 = (i % per) * cell, y0 + head + (i // per) * (cell + label)
            sheet.alpha_composite(Image.fromarray(draw(pose, k), "RGBA"), (x, y1 + label))
            d.text((x + 4, y1 + 5), fitted(d, caption, font, cell - 8), fill=(255, 220, 170, 255), font=font)
    y = top + 3 * (head + rows * (cell + label))
    for caption, img, scale in extras:
        d.text((4, y + 5), fitted(d, caption, font, sheet.width - 8), fill=(255, 220, 170, 255), font=font)
        pic = Image.fromarray(img, "RGBA")
        if scale != 1:
            pic = pic.resize((img.shape[1] * scale, img.shape[0] * scale), Image.NEAREST)
        if pic.width > sheet.width - 8:
            pic = pic.crop((0, 0, sheet.width - 8, pic.height))
        sheet.alpha_composite(pic, (4, y + label))
        y += label + img.shape[0] * scale + 10
    sheet.save(path)
    return path


def main(name, outputs, do_review):
    kit.main(sys.argv, outputs, do_review, name, "boss_l_%s.py" % name)


__all__ = ["W", "H", "GY", "PIVOT", "FX_W", "FX_H", "Pose", "T", "wave", "flash", "halo", "sparks", "trim", "pack", "_png", "_blob",
           "boss_outputs", "review", "main", "json", "math", "np"]
