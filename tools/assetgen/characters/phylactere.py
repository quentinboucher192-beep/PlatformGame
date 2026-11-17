"""Le Phylactère de la Liche (2.57): the crystal in which the Liche hid its soul, set on one of the
altars of its crypt at the end of its 4th phase (tools/contentgen/liche.py); the Liche cannot be
hurt until it is broken.

The crystal of the Liche's staff (boss_i_liche.py, `_crystal`) on its own: a long hexagonal
crystal of soul fire (the pale cyan ramp of the « Spectres » phase), its left facets lit, a flame of
soul burning in its heart, held in a cup of tarnished gold by three claws; it floats and pulses,
a few motes of soul circling it.

Frames 48x64, pivot [24, 64] = bottom-centre of the 26x52 collision box (x 11..37, y 12..64).
Clips: idle 8 (it floats, its heart pulses, the motes circle), hurt 2 (a white flash, a crack),
death 10 (cracks spread through it, it bursts into shards that fly apart and fade, the soul
escapes upward).
Deterministic (fixed seeds); drawn at 4x and reduced (smooth edges).

    python3 tools/assetgen/characters/phylactere.py
"""
import json
import math
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")

W, H = 48, 64
PIVOT = [24, 64]
SS = 4  # supersampling

# The pale cyan ramp of the Liche's 4th phase (boss_i_liche.py PHASES[3]), light -> dark.
P = ["#eafeff", "#aaf2ff", "#62cdef", "#2c86b4", "#134266"]
GOLD = ["#ead292", "#bc9a52", "#8a6a32", "#5a421e", "#32240f"]
OUTLINE = "#0b1420"


def rgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], np.float32)


class Frame:
    """An RGBA frame drawn at SS x its size, polygons filled with coverage."""

    def __init__(self):
        self.c = np.zeros((H * SS, W * SS, 4), np.float32)
        ys, xs = np.mgrid[0:H * SS, 0:W * SS]
        self.x = (xs + 0.5) / SS
        self.y = (ys + 0.5) / SS

    def poly_mask(self, pts):
        """Inside test of a convex or concave polygon (even-odd), frame pixel coordinates."""
        inside = np.zeros(self.x.shape, bool)
        n = len(pts)
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            cond = (y0 > self.y) != (y1 > self.y)
            with np.errstate(divide="ignore", invalid="ignore"):
                xi = x0 + (self.y - y0) * (x1 - x0) / (y1 - y0 + 1e-9)
            inside ^= cond & (self.x < xi)
        return inside

    def fill(self, mask, color, alpha=1.0):
        col = rgb(color) if isinstance(color, str) else np.asarray(color, np.float32)
        a = np.where(mask, alpha, 0.0)[..., None]
        self.c[..., :3] = self.c[..., :3] * (1 - a) + col * a
        self.c[..., 3:] = np.maximum(self.c[..., 3:], a * 255.0)

    def glow(self, cx, cy, r, color, k=1.0):
        """Additive soft light (it also makes the pixels opaque where it is strong)."""
        d = np.sqrt((self.x - cx) ** 2 + (self.y - cy) ** 2) / max(0.1, r)
        g = np.clip(1.0 - d, 0.0, 1.0) ** 2 * k
        col = rgb(color)
        self.c[..., :3] = np.minimum(255.0, self.c[..., :3] + col * g[..., None])
        self.c[..., 3] = np.maximum(self.c[..., 3], np.clip(g * 255.0 * 0.9, 0, 255))

    def line(self, p0, p1, w, color, alpha=1.0):
        x0, y0 = p0
        x1, y1 = p1
        dx, dy = x1 - x0, y1 - y0
        L2 = dx * dx + dy * dy + 1e-9
        t = np.clip(((self.x - x0) * dx + (self.y - y0) * dy) / L2, 0, 1)
        d = np.sqrt((self.x - (x0 + t * dx)) ** 2 + (self.y - (y0 + t * dy)) ** 2)
        self.fill(d <= w * 0.5, color, alpha)

    def disc(self, cx, cy, r, color, alpha=1.0):
        self.fill((self.x - cx) ** 2 + (self.y - cy) ** 2 <= r * r, color, alpha)

    def image(self):
        # Colours averaged where the frame is opaque (no dark fringe), coverage averaged.
        c = self.c.reshape(H, SS, W, SS, 4)
        a = c[..., 3:4] / 255.0
        wsum = a.sum(axis=(1, 3))
        col = (c[..., :3] * a).sum(axis=(1, 3)) / np.maximum(wsum, 1e-6)
        alpha = c[..., 3].mean(axis=(1, 3))[..., None]
        out = np.concatenate([col, alpha], axis=-1)
        return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")


# Crystal geometry (frame pixels): a long hexagonal bipyramid seen from the side.
CX = 24.0
TOP, SH_T, SH_B, BOT = 10.0, 21.0, 45.0, 55.0
HW = 9.0  # half width at the shoulders


def crystal_parts(dy, scale=1.0):
    """Facets of the crystal: left (lit), middle, right (dark) and their outline polygon."""
    def P_(x, y):
        return (CX + (x - CX) * scale, (y - 33.0) * scale + 33.0 + dy)
    top, bot = P_(CX, TOP), P_(CX, BOT)
    lt, rt = P_(CX - HW, SH_T), P_(CX + HW, SH_T)
    lb, rb = P_(CX - HW + 1, SH_B), P_(CX + HW - 1, SH_B)
    mt, mb = P_(CX + 1.5, SH_T - 1), P_(CX + 1.5, SH_B + 1)
    outline = [top, rt, rb, bot, lb, lt]
    left = [top, mt, mb, bot, lb, lt]
    right = [top, rt, rb, bot, mb, mt]
    return outline, left, right, (mt, mb)


def draw_cup(f, dy):
    # A cup of tarnished gold under the crystal, three claws gripping it.
    cup = [(CX - 8, 52 + dy), (CX + 8, 52 + dy), (CX + 5, 59 + dy), (CX - 5, 59 + dy)]
    f.fill(f.poly_mask([(x - 0.8, y - 0.8) for x, y in cup[:2]] + [(x + 0.0, y + 1.2) for x, y in cup[2:]]), OUTLINE)
    f.fill(f.poly_mask(cup), GOLD[2])
    f.fill(f.poly_mask([(CX - 8, 52 + dy), (CX + 1, 52 + dy), (CX - 1, 59 + dy), (CX - 5, 59 + dy)]), GOLD[1])
    f.line((CX - 7, 52.6 + dy), (CX + 7, 52.6 + dy), 1.0, GOLD[0])
    f.line((CX - 2, 59 + dy), (CX + 2, 59 + dy), 1.2, GOLD[3])
    for sx, tip in ((-1.0, (CX - 10.5, 41 + dy)), (1.0, (CX + 10.5, 41 + dy)), (0.0, (CX + 0.5, 44 + dy))):
        base = (CX + sx * 6.5, 52 + dy)
        mid = (CX + sx * 10.0, 47 + dy)
        f.line(base, mid, 2.2, OUTLINE)
        f.line(mid, tip, 1.8, OUTLINE)
        f.line(base, mid, 1.4, GOLD[1] if sx <= 0 else GOLD[2])
        f.line(mid, tip, 1.1, GOLD[0] if sx <= 0 else GOLD[2])


def draw_crystal(f, dy, heart, flash=0.0, cracks=0.0, seed=0):
    outline, left, right, (mt, mb) = crystal_parts(dy)
    # dark outline (the polygon grown a little)
    grown = [(CX + (x - CX) * 1.12, 33 + dy + (y - 33 - dy) * 1.05) for x, y in outline]
    f.fill(f.poly_mask(grown), OUTLINE)
    f.fill(f.poly_mask(left), P[2])
    f.fill(f.poly_mask(right), P[3])
    # lit edge of the left facets, the ridge
    f.line(outline[0], outline[5], 1.1, P[1])
    f.line(outline[5], outline[4], 1.1, P[1])
    f.line(mt, mb, 1.0, P[0], 0.9)
    f.line(outline[1], outline[2], 0.9, P[4])
    # its heart: a flame of soul burning inside
    hy = 34 + dy
    f.glow(CX + 0.5, hy, 9 + 3 * heart, P[1], 0.55 + 0.35 * heart)
    flame = [(CX + 0.5, hy - 9 - 2 * heart), (CX + 4, hy - 1), (CX + 2.5, hy + 5), (CX - 1.5, hy + 5), (CX - 3, hy - 1)]
    f.fill(f.poly_mask(flame), P[1], 0.85)
    f.fill(f.poly_mask([(x * 0.5 + (CX + 0.5) * 0.5, y * 0.5 + hy * 0.5 + 1) for x, y in flame]), P[0], 0.95)
    f.disc(CX + 0.5, hy + 1.5, 1.2, "#ffffff", 0.95)
    # a glint near the top
    f.line((CX - 4, 19 + dy), (CX - 2, 15 + dy), 0.8, "#ffffff", 0.9)
    if cracks > 0.0:
        rng = np.random.default_rng(seed)
        n = int(2 + 7 * cracks)
        for _ in range(n):
            x0, y0 = CX + rng.uniform(-6, 6), 33 + dy + rng.uniform(-16, 14)
            ang = rng.uniform(0, 2 * math.pi)
            ln = rng.uniform(3, 9) * (0.5 + cracks)
            x1, y1 = x0 + math.cos(ang) * ln, y0 + math.sin(ang) * ln
            f.line((x0, y0), (x1, y1), 0.9, OUTLINE)
            f.line((x0 + 0.5, y0), (x1 + 0.5, y1), 0.6, P[0], 0.9)
    if flash > 0.0:
        m = f.poly_mask(grown)
        f.fill(m, "#ffffff", 0.75 * flash)


def motes(f, dy, t, k=1.0):
    for i in range(4):
        a = t * 2 * math.pi + i * math.pi / 2
        x = CX + math.cos(a) * 15
        y = 33 + dy + math.sin(a) * 5 - 6
        f.glow(x, y, 2.6, P[1], 0.9 * k)
        f.disc(x, y, 0.7, P[0], 0.9 * k)


def idle_frame(i):
    t = i / 8.0
    dy = round(1.5 * math.sin(2 * math.pi * t) * 2) / 2.0
    heart = 0.5 + 0.5 * math.sin(2 * math.pi * t)
    f = Frame()
    f.glow(CX, 33 + dy, 22, P[3], 0.35)
    draw_cup(f, dy)
    draw_crystal(f, dy, heart)
    motes(f, dy, t)
    return f.image()


def hurt_frame(i):
    f = Frame()
    f.glow(CX, 33, 24, P[2], 0.5)
    draw_cup(f, 0)
    draw_crystal(f, 0, 1.0, flash=1.0 if i == 0 else 0.45, cracks=0.25, seed=7)
    return f.image()


def death_frame(i):
    f = Frame()
    if i < 4:
        # cracks spread, the heart flares
        k = (i + 1) / 4.0
        f.glow(CX, 33, 22 + 6 * k, P[1], 0.4 + 0.4 * k)
        draw_cup(f, 0)
        draw_crystal(f, 0, 1.0 + k, flash=0.2 * k, cracks=0.3 + 0.7 * k, seed=11 + i)
        return f.image()
    # it bursts: shards fly apart and fade, the soul escapes upward
    k = (i - 3) / 6.0
    draw_cup(f, 0)
    rng = np.random.default_rng(23)
    fade = max(0.0, 1.0 - k * 0.9)
    for s in range(11):
        ang = rng.uniform(0, 2 * math.pi)
        dist = (6 + rng.uniform(6, 18)) * k * 1.6
        x = CX + math.cos(ang) * dist + rng.uniform(-3, 3)
        y = 33 + math.sin(ang) * dist * 0.8 + rng.uniform(-8, 8) + 10 * k * k
        r = rng.uniform(1.6, 3.2) * (1.0 - 0.4 * k)
        rot = rng.uniform(0, math.pi)
        pts = [(x + math.cos(rot + a) * r * (1.4 if j % 2 == 0 else 0.8), y + math.sin(rot + a) * r * (1.4 if j % 2 == 0 else 0.8))
               for j, a in enumerate((0, 1.6, 3.1, 4.6))]
        if 1 < x < W - 1 and 1 < y < H - 1:
            f.fill(f.poly_mask(pts), P[2 if s % 2 else 1], fade)
            f.fill(f.poly_mask([(px + 0.3, py + 0.3) for px, py in pts[:3]]), P[0], 0.7 * fade)
    sy = 33 - 22 * k
    f.glow(CX, sy, 10 + 6 * k, P[1], 0.9 * fade + 0.05)
    f.disc(CX, sy, 2.2 * (1 - k) + 0.6, P[0], max(0.1, fade))
    return f.image()


CLIPS = [("idle", 8, idle_frame, {"fps": 8, "loop": True}),
         ("hurt", 2, hurt_frame, {"durations": [0.08, 0.12], "loop": False}),
         ("death", 10, death_frame, {"durations": [0.1, 0.1, 0.12, 0.12, 0.08, 0.08, 0.1, 0.1, 0.12, 0.16], "loop": False})]


def build():
    cols = max(n for _, n, _, _ in CLIPS)
    sheet = Image.new("RGBA", (cols * W, len(CLIPS) * H), (0, 0, 0, 0))
    meta = {}
    for row, (name, n, fn, extra) in enumerate(CLIPS):
        for i in range(n):
            sheet.paste(fn(i), (i * W, row * H))
        clip = {"row": row, "count": n}
        clip.update(extra)
        meta[name] = clip
    tex = "sprites/enemies/phylactere.png"
    sheet.save(os.path.join(ASSETS, tex), optimize=True)
    lines = ['{', f'  "texture": "{tex}",', f'  "frameWidth": {W},', f'  "frameHeight": {H},', f'  "pivot": [{PIVOT[0]}, {PIVOT[1]}],',
             '  "clips": {']
    items = list(meta.items())
    for k, (name, clip) in enumerate(items):
        lines.append(f'    "{name}": {json.dumps(clip, ensure_ascii=False)}' + (',' if k < len(items) - 1 else ''))
    lines += ['  }', '}']
    with open(os.path.join(ASSETS, "animations", "phylactere.json"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("phylactere :", sheet.size)


if __name__ == "__main__":
    build()
