"""Off-screen boss arrow (assets/ui/boss_arrow.png): drawn on the screen border, turned
towards a boss that is out of view. Points right; the game rotates it. Drawn large then
reduced (smooth edges): a faceted blade of red energy with a white-hot spine, a dark
outline and a soft glow, in the colours of the boss bars.

    python3 tools/assetgen/boss_arrow.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "ui", "boss_arrow.png")
S = 4  # supersampling
SIZE = 128

DEEP = (92, 8, 28)
RED = (232, 44, 78)
HOT = (255, 120, 92)
GOLD = (255, 214, 140)
OUTLINE = (24, 4, 12)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def sc(pts):
    return [(x * S, y * S) for x, y in pts]


def main():
    n = SIZE * S
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    # Shape: a broad arrowhead with a notched back (chevron), centred on the image.
    tip = (112, 64)
    upper = (40, 22)
    notch = (58, 64)
    lower = (40, 106)
    shape = [tip, upper, notch, lower]

    # Glow behind.
    glow = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.polygon(sc([(118, 64), (36, 14), (52, 64), (36, 114)]), fill=(255, 60, 90, 170))
    glow = glow.filter(ImageFilter.GaussianBlur(9 * S))
    img = Image.alpha_composite(img, glow)

    # Outline then the two faceted halves (light upper face, darker lower face).
    d = ImageDraw.Draw(img)
    d.polygon(sc([(118, 64), (37, 16), (54, 64), (37, 112)]), fill=OUTLINE + (255,))
    body = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bd = ImageDraw.Draw(body)
    steps = 48
    # Upper face: gradient from the back (deep) to the tip (hot).
    for i in range(steps):
        t0, t1 = i / steps, (i + 1) / steps
        def on(a, b, t):
            return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        quad = [on(upper, tip, t0), on(upper, tip, t1), on(notch, tip, t1), on(notch, tip, t0)]
        bd.polygon(sc(quad), fill=lerp(lerp(DEEP, RED, 0.55), HOT, t1) + (255,))
    for i in range(steps):
        t0, t1 = i / steps, (i + 1) / steps
        def on(a, b, t):
            return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        quad = [on(lower, tip, t0), on(lower, tip, t1), on(notch, tip, t1), on(notch, tip, t0)]
        bd.polygon(sc(quad), fill=lerp(DEEP, RED, 0.25 + 0.7 * t1) + (255,))
    # Energy grooves across the blade.
    for k in range(1, 4):
        t = 0.22 * k
        a = (upper[0] + (tip[0] - upper[0]) * t, upper[1] + (tip[1] - upper[1]) * t)
        b = (lower[0] + (tip[0] - lower[0]) * t, lower[1] + (tip[1] - lower[1]) * t)
        m = (notch[0] + (tip[0] - notch[0]) * t, notch[1] + (tip[1] - notch[1]) * t)
        bd.line(sc([a, m, b]), fill=lerp(DEEP, (40, 0, 10), 0.4) + (200,), width=2 * S)
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).polygon(sc(shape), fill=255)
    img.paste(body, (0, 0), mask)

    # White-hot spine along the middle, brighter towards the tip.
    spine = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    sd = ImageDraw.Draw(spine)
    for i in range(24):
        t0, t1 = i / 24, (i + 1) / 24
        x0 = notch[0] + (tip[0] - 6 - notch[0]) * t0
        x1 = notch[0] + (tip[0] - 6 - notch[0]) * t1
        w = 3.2 * (1 - 0.6 * t1)
        sd.polygon(sc([(x0, 64 - w), (x1, 64 - w * 0.9), (x1, 64 + w * 0.9), (x0, 64 + w)]), fill=lerp(HOT, GOLD, t1) + (230,))
    spine = spine.filter(ImageFilter.GaussianBlur(0.6 * S))
    img = Image.alpha_composite(img, spine)

    # Bright rim on the upper edge.
    d = ImageDraw.Draw(img)
    d.line(sc([(upper[0] + 3, upper[1] + 3), (tip[0] - 4, tip[1] - 1)]), fill=GOLD + (210,), width=2 * S)

    out = img.resize((SIZE, SIZE), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out.save(OUT, optimize=True)
    print("wrote", os.path.relpath(OUT, ROOT), out.size)


if __name__ == "__main__":
    main()
