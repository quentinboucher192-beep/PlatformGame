"""Shared helpers of the generators of Asterion's gigaboss of phase 4B (2.69, docs/RAIDS.md §13):
boss_m_voidworm.py, boss_m_graviton.py, boss_m_titanus.py.

Each generator describes its boss (a draw function of a pose and a phase, its clips), the targets of
its mechanisms (small sheets: idle / hurt / death) and its effects sheet (48x144 frames drawn by the
raid's combat screen over the arena: its plates, the marks of its blows, what falls on each player).
This module packs the frames into sheets, writes them, checks that two renders give the same bytes as
the files on disk (`--check`, tests/test_raid_gigaboss_b.cpp) and writes a labelled contact sheet in
French (`--review out.png`).
"""
import io
import math
import os

import numpy as np
from PIL import Image

from boss_a_common import C, mat  # noqa: F401  (re-exported for the generators)
from raster import TCanvas  # noqa: F401
from sheet import ASSETS, dumps

COLUMNS = 16
FX_W, FX_H = 48, 144


def blob(img, x, y, r, c):
    """A filled disc of colour `c` (RGBA tuple) straight into an image."""
    h, w = img.shape[:2]
    for yy in range(y - r, y + r + 1):
        for xx in range(x - r, x + r + 1):
            if 0 <= xx < w and 0 <= yy < h and (xx - x) ** 2 + (yy - y) ** 2 <= r * r:
                img[yy, xx] = c


def wave(t, a=1.0):
    return math.sin(t * 2 * math.pi) * a


def flash(img, k):
    """Every opaque pixel pulled toward white (hurt)."""
    if k <= 0:
        return img
    a = img[:, :, 3] > 0
    img[a, :3] = np.clip(img[a, :3].astype(np.float32) + (255 - img[a, :3].astype(np.float32)) * k, 0, 255).astype(np.uint8)
    return img


def fade(img, k):
    """The alpha of every pixel times k (death, a ghost)."""
    img[:, :, 3] = (img[:, :, 3].astype(np.float32) * max(0.0, min(1.0, k))).astype(np.uint8)
    return img


def tint(img, color, k, mask=None):
    """Opaque pixels pulled toward `color` (RGBA tuple) by k."""
    a = img[:, :, 3] > 0 if mask is None else mask & (img[:, :, 3] > 0)
    col = np.array(color[:3], np.float32)
    img[a, :3] = np.clip(img[a, :3].astype(np.float32) * (1 - k) + col * k, 0, 255).astype(np.uint8)
    return img


def halo(img, color, rng, strength=1.0, count=60):
    """Sparks of `color` hugging the silhouette (deterministic with the generator's rng)."""
    h, w = img.shape[:2]
    ys, xs = np.nonzero(img[:, :, 3] > 0)
    if len(xs) == 0:
        return img
    for _ in range(int(count * strength)):
        i = int(rng.randint(0, len(xs)))
        x = int(xs[i] + rng.randint(-4, 5))
        y = int(ys[i] + rng.randint(-4, 5))
        if 0 <= x < w and 0 <= y < h and img[y, x, 3] == 0:
            img[y, x] = color
    return img


def pack(clips, fw, fh, columns=COLUMNS):
    """Clips placed left to right, several per row (never split). `move` shares `walk`'s frames."""
    rows, cur, used = [], [], 0
    for name, frames, kw in clips:
        if used + len(frames) > columns:
            rows.append(cur)
            cur, used = [], 0
        cur.append((name, frames, kw, used))
        used += len(frames)
    if cur:
        rows.append(cur)
    img = np.zeros((fh * len(rows), fw * columns, 4), np.uint8)
    meta = {}
    for r, row in enumerate(rows):
        for name, frames, kw, start in row:
            for c, f in enumerate(frames):
                img[r * fh:(r + 1) * fh, (start + c) * fw:(start + c + 1) * fw] = f
            m = {"row": r, "count": len(frames)}
            if start:
                m["start"] = start
            if kw.get("durations") is not None:
                m["durations"] = [round(float(d), 3) for d in kw["durations"]]
            else:
                m["fps"] = kw.get("fps", 10)
            m["loop"] = bool(kw.get("loop", True))
            if kw.get("events"):
                m["events"] = [{"frame": int(fr), "name": n} for fr, n in kw["events"]]
            meta[name] = m
            if name == "walk":
                meta["move"] = dict(m)
    return img, meta


def png(img):
    buf = io.BytesIO()
    Image.fromarray(img, "RGBA").save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def sheet_entry(name, img, meta, fw, fh, pivot, folder="sprites/enemies"):
    """The two files of a sheet: (path, bytes) of its png and of its animation."""
    tex = "%s/%s.png" % (folder, name)
    anim = dumps({"texture": tex, "frameWidth": fw, "frameHeight": fh, "pivot": pivot, "clips": meta}) + "\n"
    return [(tex, png(img)), ("animations/%s.json" % name, anim.encode())]


def build(outputs):
    for rel, data in outputs():
        path = os.path.join(ASSETS, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)


def check(outputs, name, script):
    """Two renders give the same bytes, and the files on disk are the generator's output."""
    errs = []
    a, b = outputs(), outputs()
    if a != b:
        errs.append("%s: two renders differ (not deterministic)" % name)
    for rel, data in a:
        path = os.path.join(ASSETS, rel)
        if not os.path.exists(path):
            errs.append("%s: missing (python3 tools/assetgen/characters/%s)" % (rel, script))
            continue
        if rel.endswith(".json"):
            with open(path, "rb") as f:
                if f.read().replace(b"\r", b"") != data:
                    errs.append("%s: not the generator's output (regenerate)" % rel)
        else:
            disk = np.array(Image.open(path).convert("RGBA"))
            fresh = np.array(Image.open(io.BytesIO(data)).convert("RGBA"))
            if disk.shape != fresh.shape or (disk != fresh).any():
                errs.append("%s: not the generator's output (regenerate)" % rel)
    return errs


def contact(path, title, cells, extras, cell=(224, 224), per=7, bg=(20, 16, 30, 255), ink=(235, 240, 220, 255)):
    """A labelled contact sheet: `cells` = [(label, rgba image)] at x1, laid out `per` a row;
    `extras` = [(label, rgba image, zoom)] in a last band (targets, effects)."""
    from PIL import ImageDraw, ImageFont
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 12)
    big = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 18)
    cw, ch = cell
    label = 22
    rows = (len(cells) + per - 1) // per
    band = 0
    for _, img, zoom in extras:
        band = max(band, img.shape[0] * zoom)
    width = per * cw
    head = 34
    # The extras flow left to right, wrapping.
    ex_rows, x, row = [[]], 0, 0
    for lab, img, zoom in extras:
        wdt = max(img.shape[1] * zoom, int(font.getlength(lab)) + 8) + 12
        if x + wdt > width and ex_rows[-1]:
            ex_rows.append([])
            x = 0
        ex_rows[-1].append((lab, img, zoom, x))
        x += wdt
    ex_h = sum(label + max(img.shape[0] * zoom for _, img, zoom, _ in r) + 10 for r in ex_rows if r)
    sheet = Image.new("RGBA", (width, head + rows * (ch + label) + ex_h + 8), bg)
    d = ImageDraw.Draw(sheet)
    d.text((6, 6), title, fill=ink, font=big)
    for i, (lab, img) in enumerate(cells):
        x, y = (i % per) * cw, head + (i // per) * (ch + label)
        sheet.alpha_composite(Image.fromarray(img, "RGBA"), (x + (cw - img.shape[1]) // 2, y + label))
        d.text((x + 4, y + 5), lab, fill=ink, font=font)
    y = head + rows * (ch + label)
    for r in ex_rows:
        if not r:
            continue
        hh = max(img.shape[0] * zoom for _, img, zoom, _ in r)
        for lab, img, zoom, x in r:
            d.text((x + 4, y + 5), lab, fill=ink, font=font)
            im = Image.fromarray(img, "RGBA")
            if zoom != 1:
                im = im.resize((img.shape[1] * zoom, img.shape[0] * zoom), Image.NEAREST)
            sheet.alpha_composite(im, (x + 4, y + label))
        y += label + hh + 10
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sheet.save(path)
    return path


def frames_of(img, meta, fw, fh, clip):
    """The frames of a clip of a packed sheet (the contact sheet of the effects)."""
    m = meta[clip]
    r, s = m["row"], m.get("start", 0)
    return [img[r * fh:(r + 1) * fh, (s + k) * fw:(s + k + 1) * fw] for k in range(m["count"])]


def strip(frames):
    """Frames side by side (one image)."""
    return np.concatenate(frames, axis=1)


def main(argv, outputs, review, name, script):
    import sys
    if len(argv) > 1 and argv[1] == "--check":
        e = check(outputs, name, script)
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    elif len(argv) > 2 and argv[1] == "--review":
        print(review(argv[2]))
    else:
        build(outputs)
        for rel, data in outputs():
            print(rel, len(data))
