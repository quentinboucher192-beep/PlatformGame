"""La Vanne de pression du Titan (2.58): the pressure valves of the Titan's foundry, which appear on
the valve spots of its arena when it stalls at the end of its 4th phase (tools/contentgen/titan.py);
all of them turned (broken) before it restarts, its boiler bursts.

A red handwheel with five spokes on a brass valve body (two bolted flanges, a stem), steam leaking
around the gland; the wheel's rim glows with the heat of the pressure. Seen from the front.

Frames 48x48, pivot [24, 47] = bottom-centre of the 30x30 collision box (x 9..39, y 17..47).
Clips: idle 6 (steam leaks, the rim glows and fades), hurt 2 (a white flash, the wheel jolts a
quarter turn), death 10 (the wheel spins open, a jet of steam blasts upward and fades, the valve
fades with it).
Deterministic (fixed seeds); drawn at 4x and reduced (smooth edges).

    python3 tools/assetgen/characters/vanne.py
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")

W, H = 48, 48
PIVOT = [24, 47]
SS = 4
OUTLINE = (14, 12, 16, 255)
RED = [(255, 120, 96), (222, 52, 40), (160, 26, 24), (96, 14, 16)]
BRASS = [(246, 222, 150), (204, 166, 84), (150, 116, 54), (92, 68, 32)]
STEEL = [(196, 204, 216), (132, 140, 154), (84, 90, 102), (48, 52, 60)]
CLIPS = [("idle", 6), ("hurt", 2), ("death", 10)]


def S(v):
    return v * SS


class Frame:
    def __init__(self):
        self.img = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img, "RGBA")

    def ellipse(self, cx, cy, rx, ry, fill, outline=None, width=0):
        self.d.ellipse([S(cx - rx), S(cy - ry), S(cx + rx), S(cy + ry)], fill=fill, outline=outline, width=int(S(width)) if width else 0)

    def rect(self, x0, y0, x1, y1, fill):
        self.d.rectangle([S(x0), S(y0), S(x1), S(y1)], fill=fill)

    def line(self, a, b, w, fill):
        self.d.line([S(a[0]), S(a[1]), S(b[0]), S(b[1])], fill=fill, width=max(1, int(S(w))))
        for p in (a, b):
            self.ellipse(p[0], p[1], w * 0.5, w * 0.5, fill)

    def _soft(self, discs, color):
        """Soft discs (x, y, r, a) of one colour, composited over the frame (straight alpha)."""
        ys, xs = np.mgrid[0:H * SS, 0:W * SS]
        x = (xs + 0.5) / SS
        y = (ys + 0.5) / SS
        acc = np.zeros(x.shape, np.float32)
        for (cx, cy, r, a) in discs:
            d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / max(0.1, r)
            g = np.clip(1.0 - d, 0.0, 1.0) ** 1.5 * a
            acc = 1.0 - (1.0 - acc) * (1.0 - g)
        layer = np.zeros((H * SS, W * SS, 4), np.uint8)
        layer[..., 0], layer[..., 1], layer[..., 2] = color
        layer[..., 3] = np.clip(acc * 255.0, 0, 255).astype(np.uint8)
        self.img = Image.alpha_composite(self.img, Image.fromarray(layer, "RGBA"))
        self.d = ImageDraw.Draw(self.img, "RGBA")

    def glow(self, cx, cy, r, color, k):
        self._soft([(cx, cy, r, 0.55 * k)], color)

    def steam(self, puffs, alpha=1.0):
        self._soft([(x, y, r, min(1.0, a * alpha * 0.8)) for (x, y, r, a) in puffs], (240, 240, 246))

    def out(self):
        # Reduced with its colours weighted by their coverage (no dark fringe on the steam).
        a = np.asarray(self.img, np.float32).reshape(H, SS, W, SS, 4)
        alpha = a[..., 3:4] / 255.0
        wsum = alpha.sum(axis=(1, 3))
        col = (a[..., :3] * alpha).sum(axis=(1, 3)) / np.maximum(wsum, 1e-6)
        cov = a[..., 3].mean(axis=(1, 3))[..., None]
        return Image.fromarray(np.clip(np.concatenate([col, cov], axis=-1), 0, 255).astype(np.uint8), "RGBA")


WC = (24.0, 24.0)  # the wheel's centre
WR = 13.0          # its rim


def body(f):
    # The valve body under the wheel: a stem, a gland, a brass body with two bolted flanges, the
    # pipe stub going down to the spot.
    f.rect(19, 40, 29, 47, OUTLINE)
    f.rect(20, 40, 28, 47, STEEL[2])
    f.rect(20, 40, 22, 47, STEEL[1])
    f.ellipse(24, 37, 9, 5, OUTLINE)
    f.ellipse(24, 37, 8, 4, BRASS[2])
    f.ellipse(23, 36, 6, 2.6, BRASS[1])
    for x in (11, 37):
        f.rect(x - 3, 33, x + 3, 42, OUTLINE)
        f.rect(x - 2, 34, x + 2, 41, BRASS[2])
        f.rect(x - 2, 34, x - 1, 41, BRASS[1])
        f.ellipse(x, 36, 0.9, 0.9, BRASS[0])
        f.ellipse(x, 39, 0.9, 0.9, BRASS[0])
    f.rect(13, 35, 35, 40, OUTLINE)
    f.rect(14, 36, 34, 39, BRASS[3])
    f.rect(14, 36, 34, 37, BRASS[2])
    f.rect(22, 26, 26, 35, OUTLINE)
    f.rect(23, 26, 25, 35, STEEL[1])


def wheel(base, ang, heat, flash=0.0):
    cx, cy = WC
    # The glow of the pressure behind it.
    if heat > 0.0:
        base.glow(cx, cy, WR + 6, (255, 140, 60), heat)
    f = Frame()  # the wheel on its own layer (its inside is cut out: the valve shows through)
    f.ellipse(cx, cy, WR + 1.2, WR + 1.2, OUTLINE)
    f.ellipse(cx, cy, WR, WR, RED[2])
    f.ellipse(cx, cy, WR - 2.6, WR - 2.6, OUTLINE)
    ImageDraw.Draw(f.img).ellipse([S(cx - WR + 3.4), S(cy - WR + 3.4), S(cx + WR - 3.4), S(cy + WR - 3.4)], fill=(0, 0, 0, 0))
    # The rim's light and shade (an arc on its top left, its bottom right darker).
    f.d.arc([S(cx - WR + 0.6), S(cy - WR + 0.6), S(cx + WR - 0.6), S(cy + WR - 0.6)], 180, 300, fill=RED[0] + (255,), width=int(S(1.1)))
    f.d.arc([S(cx - WR + 0.6), S(cy - WR + 0.6), S(cx + WR - 0.6), S(cy + WR - 0.6)], 10, 120, fill=RED[3] + (255,), width=int(S(1.1)))
    # Five spokes and the hub.
    for k in range(5):
        a = ang + k * 2 * math.pi / 5
        p0 = (cx + math.cos(a) * 3.0, cy + math.sin(a) * 3.0)
        p1 = (cx + math.cos(a) * (WR - 2.2), cy + math.sin(a) * (WR - 2.2))
        f.line(p0, p1, 3.0, OUTLINE)
        f.line(p0, p1, 1.8, RED[1])
    f.ellipse(cx, cy, 4.2, 4.2, OUTLINE)
    f.ellipse(cx, cy, 3.4, 3.4, BRASS[2])
    f.ellipse(cx - 0.8, cy - 0.8, 1.6, 1.6, BRASS[0])
    # A handle knob on the rim.
    a = ang + 0.6
    kx, ky = cx + math.cos(a) * (WR - 0.6), cy + math.sin(a) * (WR - 0.6)
    f.ellipse(kx, ky, 2.2, 2.2, OUTLINE)
    f.ellipse(kx, ky, 1.5, 1.5, STEEL[0])
    if heat > 0.0:
        layer = Image.new("RGBA", f.img.size, (0, 0, 0, 0))
        g = ImageDraw.Draw(layer, "RGBA")
        g.ellipse([S(cx - WR + 0.4), S(cy - WR + 0.4), S(cx + WR - 0.4), S(cy + WR - 0.4)], outline=(255, 190, 110, int(150 * heat)), width=int(S(1)))
        f.img = Image.alpha_composite(f.img, layer)
        f.d = ImageDraw.Draw(f.img, "RGBA")
    base.img = Image.alpha_composite(base.img, f.img)
    if flash > 0.0:
        layer = Image.new("RGBA", base.img.size, (0, 0, 0, 0))
        mask = base.img.split()[3]
        white = Image.new("RGBA", base.img.size, (255, 255, 255, int(255 * flash)))
        layer.paste(white, (0, 0), mask)
        base.img = Image.alpha_composite(base.img, layer)
    base.d = ImageDraw.Draw(base.img, "RGBA")


def leak(t, seed):
    # Puffs of steam leaking at the gland, drifting up and aside.
    out = []
    for k in range(3):
        ph = (t + k / 3.0 + seed * 0.13) % 1.0
        side = -1 if k % 2 else 1
        out.append((24 + side * (4 + 6 * ph), 34 - 16 * ph, 2.0 + 3.5 * ph, 0.9 * (1.0 - ph)))
    return out


def idle(i):
    f = Frame()
    t = i / 6.0
    heat = 0.55 + 0.35 * math.sin(t * 2 * math.pi)
    body(f)
    wheel(f, -math.pi / 2, heat)
    f.steam(leak(t, 1))
    return f.out()


def hurt(i):
    f = Frame()
    body(f)
    wheel(f, -math.pi / 2 + (0.35 if i == 0 else 0.6), 0.9, flash=0.75 if i == 0 else 0.25)
    f.steam(leak(0.2 + 0.3 * i, 2), 1.2)
    return f.out()


def death(i):
    f = Frame()
    k = i / 9.0
    body(f)
    ang = -math.pi / 2 + 6.0 * k * k * math.pi
    wheel(f, ang, max(0.0, 0.9 - k))
    # A jet of steam blasting upward out of the gland, then fading.
    puffs = []
    blast = min(1.0, k * 2.5) * (1.0 - max(0.0, k - 0.6) / 0.4)
    for j in range(7):
        u = j / 6.0
        puffs.append((24 + math.sin(j * 1.7 + i) * 2.5 * u, 30 - 28 * u * min(1.0, k * 2.0), 2.5 + 5 * u, blast * (1.0 - 0.5 * u)))
    f.steam(puffs, 1.4)
    img = f.out()
    if k > 0.55:  # the valve fades away with the steam
        a = img.split()[3].point(lambda v: int(v * max(0.15, 1.0 - (k - 0.55) / 0.45)))
        img.putalpha(a)
    return img


def build():
    rows = [[idle(i) for i in range(6)], [hurt(i) for i in range(2)], [death(i) for i in range(10)]]
    cols = max(len(r) for r in rows)
    sheet = Image.new("RGBA", (W * cols, H * len(rows)), (0, 0, 0, 0))
    for y, r in enumerate(rows):
        for x, fr in enumerate(r):
            sheet.paste(fr, (x * W, y * H))
    os.makedirs(os.path.join(ASSETS, "sprites", "enemies"), exist_ok=True)
    sheet.save(os.path.join(ASSETS, "sprites", "enemies", "vanne.png"), optimize=True)
    anim = {
        "texture": "sprites/enemies/vanne.png",
        "frameWidth": W,
        "frameHeight": H,
        "pivot": PIVOT,
        "clips": {
            "idle": {"row": 0, "count": 6, "fps": 7, "loop": True},
            "hurt": {"row": 1, "count": 2, "durations": [0.08, 0.12], "loop": False},
            "death": {"row": 2, "count": 10, "durations": [0.06, 0.06, 0.07, 0.08, 0.08, 0.1, 0.1, 0.12, 0.12, 0.16], "loop": False},
        },
    }
    with open(os.path.join(ASSETS, "animations", "vanne.json"), "w", encoding="utf-8") as f:
        f.write("{\n")
        f.write('  "texture": "%s",\n  "frameWidth": %d,\n  "frameHeight": %d,\n  "pivot": [%d, %d],\n  "clips": {\n'
                % (anim["texture"], W, H, PIVOT[0], PIVOT[1]))
        items = list(anim["clips"].items())
        for n, (k, v) in enumerate(items):
            f.write('    "%s": %s%s\n' % (k, json.dumps(v), "," if n + 1 < len(items) else ""))
        f.write("  }\n}\n")
    print("vanne :", sheet.size)


if __name__ == "__main__":
    build()
