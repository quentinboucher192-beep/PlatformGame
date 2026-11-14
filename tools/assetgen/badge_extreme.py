"""EXTRÊME badge (assets/ui/badge_extreme.png): the mark of a one-life level with a
« course à la vérification ». Drawn large then reduced (smooth edges), in the game's colours
and fonts.

    python3 tools/assetgen/badge_extreme.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "ui", "badge_extreme.png")
S = 4  # supersampling
W, H = 512, 176


def plaque(w, h, cut):
    return [(cut, 0), (w - cut, 0), (w, cut), (w, h - cut), (w - cut, h), (cut, h), (0, h - cut), (0, cut)]


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
                    u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1]))
    return out


def tongue(cx, cy, s, lean):
    """One flame: round base at (cx, cy), curved flanks up to a tip leaning by `lean`."""
    r = s * 0.42
    tip = (cx + lean * s, cy - s * 1.15)
    pts = []
    # Round bottom (from the right flank, through the bottom, to the left flank).
    for i in range(0, 181, 6):
        a = math.radians(-10 + i * 200 / 180)
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    left = pts[-1]
    right = pts[0]
    # Left flank up to the tip, then down the right flank (S-shaped, like a lick of fire).
    pts += bezier(left, (left[0] - r * 0.35, cy - s * 0.45), (tip[0] - r * 0.9, tip[1] + s * 0.45), tip)
    pts += bezier(tip, (tip[0] + r * 0.2, tip[1] + s * 0.35), (right[0] + r * 0.55, cy - s * 0.5), right)
    return pts


def flame(draw, cx, cy, size, colors):
    """A flame pointing up, with two smaller side tongues; outer to inner."""
    for k, (scale, col) in enumerate(zip((1.0, 0.7, 0.42), colors)):
        s = size * scale
        base = cy + size * 0.42 - s * 0.42
        if k == 0:
            draw.polygon(tongue(cx - s * 0.22, base - s * 0.05, s * 0.62, -0.28), fill=col)
            draw.polygon(tongue(cx + s * 0.24, base - s * 0.02, s * 0.55, 0.3), fill=col)
        draw.polygon(tongue(cx, base, s, 0.06), fill=col)


def main():
    w, h = W * S, H * S
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pad = 14 * S
    cut = 30 * S
    shape = [(x + pad, y + pad) for x, y in plaque(w - 2 * pad, h - 2 * pad, cut)]

    # Outer glow.
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).polygon(shape, fill=(255, 60, 40, 200))
    glow = glow.filter(ImageFilter.GaussianBlur(9 * S))
    img = Image.alpha_composite(img, glow)

    # Border: a red-to-amber gradient, then the dark plate inside.
    grad = Image.new("RGBA", (w, h))
    gd = ImageDraw.Draw(grad)
    for x in range(w):
        gd.line([(x, 0), (x, h)], fill=lerp((255, 52, 70, 255), (255, 170, 50, 255), x / w))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).polygon(shape, fill=255)
    img.paste(grad, (0, 0), mask)
    inset = 6 * S
    inner = [(x + pad + inset, y + pad + inset) for x, y in plaque(w - 2 * (pad + inset), h - 2 * (pad + inset), cut - inset // 2)]
    plate = Image.new("RGBA", (w, h))
    pd = ImageDraw.Draw(plate)
    for y in range(h):
        pd.line([(0, y), (w, y)], fill=lerp((44, 10, 22, 255), (14, 6, 18, 255), y / h))
    mask2 = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask2).polygon(inner, fill=255)
    img.paste(plate, (0, 0), mask2)
    # Thin bright line along the top of the plate.
    d = ImageDraw.Draw(img)
    d.line([inner[0], inner[1]], fill=(255, 140, 90, 180), width=2 * S)

    # Flame on the left.
    fx = pad + inset + 58 * S
    fl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    flame(ImageDraw.Draw(fl), fx, h * 0.5 + 18 * S, 70 * S, [(255, 70, 40, 255), (255, 160, 50, 255), (255, 240, 170, 255)])
    fglow = fl.filter(ImageFilter.GaussianBlur(5 * S))
    img = Image.alpha_composite(img, fglow)
    img = Image.alpha_composite(img, fl)

    # Words.
    tx = fx + 56 * S
    room = (w - pad - inset - 22 * S) - tx  # up to the plate's right edge, with a margin
    size = 64
    while True:
        title = ImageFont.truetype(os.path.join(ROOT, "assets", "fonts", "Orbitron.ttf"), size * S)
        if title.getlength("EXTRÊME") + 2 * S <= room or size <= 30:
            break
        size -= 1
    small = ImageFont.truetype(os.path.join(ROOT, "assets", "fonts", "ChakraPetch-SemiBold.ttf"), 24 * S)
    text = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    td = ImageDraw.Draw(text)
    td.text((tx, h * 0.5 - 6 * S), "EXTRÊME", font=title, fill=(255, 255, 255, 255), anchor="lm", stroke_width=1 * S, stroke_fill=(255, 255, 255, 255))
    tglow = text.filter(ImageFilter.GaussianBlur(6 * S))
    r, g, b, a = tglow.split()
    tglow = Image.merge("RGBA", (r.point(lambda v: 255), g.point(lambda v: 70), b.point(lambda v: 50), a))
    img = Image.alpha_composite(img, tglow)
    img = Image.alpha_composite(img, text)
    ImageDraw.Draw(img).text((tx + 4 * S, h * 0.5 + 40 * S), "UNE SEULE VIE", font=small, fill=(255, 176, 90, 255), anchor="lm")

    out = img.resize((W, H), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out.save(OUT)
    print("wrote", OUT, out.size)


if __name__ == "__main__":
    main()
