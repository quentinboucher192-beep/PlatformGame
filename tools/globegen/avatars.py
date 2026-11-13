"""avatars.png (4x4 x 128 px, RGBA) et avatars.json : portraits cosmiques sur badge rond.

Chaque avatar est dessiné à 512 px (4x) puis réduit (Lanczos) pour l'anticrénelage.
"""
import json
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 512
CELL = 128
C = S / 2
R_OUT, R_IN = 250, 230

CYAN, VIOLET, WARM = (0x39, 0xF0, 0xD8), (0xB0, 0x4C, 0xFF), (0xFF, 0xB3, 0x47)
PINK, GREEN, BLUE = (0xFF, 0x4D, 0x6D), (0x78, 0xE6, 0x8C), (0x4C, 0x8D, 0xFF)


# ----------------------------------------------------------------------------- outils
def layer():
    return Image.new('RGBA', (S, S), (0, 0, 0, 0))


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def shade(c, k):
    return tuple(max(0, min(255, int(round(v * k)))) for v in c[:3])


def mask_of(draw_fn):
    m = Image.new('L', (S, S), 0)
    draw_fn(ImageDraw.Draw(m))
    return m


_YY, _XX = np.mgrid[0:S, 0:S].astype(np.float64) + 0.5


def radial(center, radius, c0, c1, power=1.0):
    d = np.clip(np.hypot(_XX - center[0], _YY - center[1]) / radius, 0, 1) ** power
    a = np.array(c0, dtype=np.float64)[None, None, :]
    b = np.array(c1, dtype=np.float64)[None, None, :]
    rgb = a + (b - a) * d[..., None]
    out = np.dstack([np.clip(rgb, 0, 255), np.full((S, S), 255.0)]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def linear(p0, p1, c0, c1):
    vx, vy = p1[0] - p0[0], p1[1] - p0[1]
    t = np.clip(((_XX - p0[0]) * vx + (_YY - p0[1]) * vy) / (vx * vx + vy * vy), 0, 1)
    a = np.array(c0, dtype=np.float64)[None, None, :]
    b = np.array(c1, dtype=np.float64)[None, None, :]
    rgb = a + (b - a) * t[..., None]
    out = np.dstack([np.clip(rgb, 0, 255), np.full((S, S), 255.0)]).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def paint(dst, mask, src, alpha=1.0):
    """Peint src (couleur ou image RGBA pleine) à travers le masque L."""
    if isinstance(src, tuple):
        src = Image.new('RGBA', (S, S), src[:3] + (255,))
    a = np.asarray(mask, dtype=np.float64) / 255.0 * alpha
    if src.mode == 'RGBA':
        a = a * np.asarray(src.getchannel('A'), dtype=np.float64) / 255.0
    lay = src.copy()
    lay.putalpha(Image.fromarray(np.clip(a * 255 + 0.5, 0, 255).astype(np.uint8), 'L'))
    dst.alpha_composite(lay)


def glow(dst, mask, color, radius, alpha=1.0):
    m = mask.filter(ImageFilter.GaussianBlur(radius))
    paint(dst, m, color, alpha)


def ell(cx, cy, rx, ry):
    return [cx - rx, cy - ry, cx + rx, cy + ry]


def ellipse_mask(cx, cy, rx, ry):
    return mask_of(lambda d: d.ellipse(ell(cx, cy, rx, ry), fill=255))


def poly_mask(pts):
    return mask_of(lambda d: d.polygon(pts, fill=255))


def sub(a, b):
    return Image.fromarray(np.clip(np.asarray(a, np.int16) - np.asarray(b, np.int16), 0, 255).astype(np.uint8), 'L')


def mul(a, b):
    return Image.fromarray((np.asarray(a, np.uint16) * np.asarray(b, np.uint16) // 255).astype(np.uint8), 'L')


def highlight(dst, cx, cy, rx, ry, alpha=0.55, blur=6):
    paint(dst, ellipse_mask(cx, cy, rx, ry).filter(ImageFilter.GaussianBlur(blur)), (255, 255, 255), alpha)


def star_points(cx, cy, r_out, r_in, n=5, rot=-90):
    pts = []
    for i in range(2 * n):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(rot + i * 180.0 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def sparkle(dst, x, y, r, color=(255, 255, 255), alpha=1.0):
    m = poly_mask(star_points(x, y, r, r * 0.22, n=4, rot=0))
    glow(dst, m, color, r * 0.6, alpha * 0.8)
    paint(dst, m, color, alpha)


# ----------------------------------------------------------------------------- badge
def badge_background(rim, seed):
    bg = radial((C - 40, C - 70), 330, mix((34, 40, 96), rim, 0.18), (6, 8, 26), power=0.9)
    rnd = random.Random(seed)
    lay = layer()
    d = ImageDraw.Draw(lay)
    for _ in range(46):
        x, y = rnd.uniform(30, S - 30), rnd.uniform(30, S - 30)
        r = rnd.choice([1.5, 2, 2.5, 3, 4])
        a = rnd.randint(90, 220)
        d.ellipse(ell(x, y, r, r), fill=(255, 255, 255, a))
    bg.alpha_composite(lay)
    # nébuleuse légère dans la couleur du liseré
    neb = ellipse_mask(C + rnd.uniform(-90, 90), C + rnd.uniform(-60, 90), 170, 110).filter(ImageFilter.GaussianBlur(60))
    paint(bg, neb, rim, 0.22)
    return bg


def badge_rim(rim):
    lay = layer()
    ring = sub(ellipse_mask(C, C, R_OUT, R_OUT), ellipse_mask(C, C, R_IN, R_IN))
    paint(lay, ring, linear((C - 200, C - 200), (C + 200, C + 200), mix(rim, (255, 255, 255), 0.45), shade(rim, 0.62)))
    # liseré lumineux intérieur + ombre portée sur le contenu
    inner = sub(ellipse_mask(C, C, R_IN + 1, R_IN + 1), ellipse_mask(C, C, R_IN - 5, R_IN - 5))
    paint(lay, inner, (8, 10, 26), 0.75)
    edge = sub(ellipse_mask(C, C, R_OUT, R_OUT), ellipse_mask(C, C, R_OUT - 4, R_OUT - 4))
    paint(lay, edge, shade(rim, 0.45), 0.9)
    hl = sub(ellipse_mask(C, C, R_OUT - 5, R_OUT - 5), ellipse_mask(C, C, R_IN + 7, R_IN + 7))
    hl = mul(hl, ellipse_mask(C - 120, C - 140, 190, 140).filter(ImageFilter.GaussianBlur(30)))
    paint(lay, hl, (255, 255, 255), 0.55)
    return lay


def compose(rim, seed, content_fn):
    img = badge_background(rim, seed)
    content = layer()
    content_fn(content)
    clip = ellipse_mask(C, C, R_IN, R_IN)
    paint(img, clip, content)
    out = layer()
    paint(out, ellipse_mask(C, C, R_OUT, R_OUT), img)
    # halo extérieur discret dans la couleur du liseré
    halo = layer()
    glow(halo, sub(ellipse_mask(C, C, R_OUT + 2, R_OUT + 2), ellipse_mask(C, C, R_OUT - 8, R_OUT - 8)), rim, 3, 0.6)
    out = Image.alpha_composite(halo, out)
    out.alpha_composite(badge_rim(rim))
    return out


# ----------------------------------------------------------------------------- sujets
def astronaut(visor):
    def draw(img):
        suit_hi, suit_lo = (244, 247, 252), (150, 162, 188)
        # épaules / combinaison
        body = ellipse_mask(C, 520, 210, 150)
        paint(img, body, linear((C - 200, 380), (C + 200, 520), suit_hi, suit_lo))
        chest = poly_mask([(C - 46, 420), (C + 46, 420), (C + 38, 470), (C - 38, 470)])
        paint(img, chest, (60, 70, 100))
        for i, c in enumerate([visor, (255, 255, 255), mix(visor, (255, 255, 255), 0.5)]):
            paint(img, ellipse_mask(C - 20 + i * 20, 445, 7, 7), c)
        collar = sub(ellipse_mask(C, 378, 128, 46), ellipse_mask(C, 372, 104, 30))
        paint(img, collar, linear((C, 340), (C, 420), (200, 208, 226), (110, 120, 150)))
        # casque
        helmet = ellipse_mask(C, 238, 150, 148)
        glow(img, helmet, visor, 18, 0.35)
        paint(img, helmet, radial((C - 60, 170), 260, suit_hi, (128, 140, 170), power=1.3))
        # oreillettes lumineuses
        for sx in (-1, 1):
            ear = mask_of(lambda d, sx=sx: d.rounded_rectangle([C + sx * 150 - 18, 205, C + sx * 150 + 18, 285], 14, fill=255))
            paint(img, ear, (90, 100, 132))
            glow(img, ellipse_mask(C + sx * 150, 245, 7, 18), visor, 6, 0.9)
            paint(img, ellipse_mask(C + sx * 150, 245, 6, 16), mix(visor, (255, 255, 255), 0.4))
        # visière
        vis = mask_of(lambda d: d.rounded_rectangle([C - 112, 175, C + 112, 318], 64, fill=255))
        paint(img, vis, (20, 26, 48))
        inner = mask_of(lambda d: d.rounded_rectangle([C - 104, 182, C + 104, 310], 58, fill=255))
        paint(img, inner, linear((C - 90, 180), (C + 80, 310), mix(visor, (255, 255, 255), 0.35), shade(visor, 0.32)))
        # reflets
        refl = sub(ellipse_mask(C - 30, 230, 70, 40), ellipse_mask(C - 18, 244, 70, 40))
        paint(img, mul(refl, inner), (255, 255, 255), 0.75)
        paint(img, mul(ellipse_mask(C + 62, 286, 16, 9), inner), (255, 255, 255), 0.5)
        sparkle(img, C - 66, 206, 14)
        highlight(img, C - 70, 130, 46, 22, 0.6, 8)
    return draw


def alien_green(img):
    skin_hi, skin_lo = (180, 255, 190), (40, 150, 80)
    # antennes
    for sx in (-1, 1):
        d = ImageDraw.Draw(img)
        d.line([(C + sx * 50, 150), (C + sx * 92, 70)], fill=(70, 180, 100, 255), width=12)
        glow(img, ellipse_mask(C + sx * 95, 64, 18, 18), GREEN, 12, 0.9)
        paint(img, ellipse_mask(C + sx * 95, 64, 17, 17), radial((C + sx * 90, 58), 22, (240, 255, 220), GREEN))
    body = ellipse_mask(C, 520, 170, 130)
    paint(img, body, linear((C, 400), (C, 520), (60, 70, 120), (30, 34, 70)))
    neck = mask_of(lambda d: d.rounded_rectangle([C - 34, 340, C + 34, 420], 20, fill=255))
    paint(img, neck, shade(skin_lo, 0.95))
    head = poly_mask([(C + 165 * math.cos(math.radians(a)) * (1 - 0.32 * max(0, math.sin(math.radians(a)))),
                       250 + (145 if math.sin(math.radians(a)) > 0 else 125) * math.sin(math.radians(a)))
                      for a in range(0, 360, 3)])
    paint(img, head, radial((C - 50, 190), 240, skin_hi, skin_lo, power=1.2))
    for sx in (-1, 1):
        eye = ellipse_mask(C + sx * 54, 262, 40, 30).rotate(sx * 22, resample=Image.BICUBIC, center=(C + sx * 54, 262))
        paint(img, eye, (14, 18, 34))
        paint(img, ellipse_mask(C + sx * 52 - 12, 252, 12, 9), (255, 255, 255), 0.95)
        paint(img, ellipse_mask(C + sx * 52 + 14, 272, 5, 4), (200, 255, 230), 0.8)
    smile = mask_of(lambda d: d.arc(ell(C, 312, 34, 20), 20, 160, fill=255, width=7))
    paint(img, smile, (24, 80, 44))
    for sx in (-1, 1):
        paint(img, ellipse_mask(C + sx * 98, 312, 20, 11), (255, 140, 160), 0.35)
    highlight(img, C - 60, 160, 50, 24, 0.45, 10)


def alien_three_eyes(img):
    skin_hi, skin_lo = (196, 150, 255), (92, 48, 170)
    body = ellipse_mask(C, 525, 180, 140)
    paint(img, body, linear((C, 400), (C, 520), (255, 179, 71), (190, 100, 40)))
    for sx in (-1, 1):
        horn = poly_mask([(C + sx * 70, 140), (C + sx * 118, 66), (C + sx * 108, 150)])
        paint(img, horn, linear((C, 66), (C, 150), (255, 220, 160), (210, 140, 80)))
    head = ellipse_mask(C, 265, 170, 150)
    paint(img, head, radial((C - 60, 200), 260, skin_hi, skin_lo, power=1.1))
    for (ex, ey, r) in [(C - 74, 236, 34), (C + 74, 236, 34), (C, 190, 40)]:
        paint(img, ellipse_mask(ex, ey, r, r), (250, 250, 255))
        paint(img, ellipse_mask(ex + 4, ey + 6, r * 0.55, r * 0.55), (30, 200, 190))
        paint(img, ellipse_mask(ex + 6, ey + 8, r * 0.3, r * 0.3), (10, 14, 30))
        paint(img, ellipse_mask(ex - r * 0.25, ey - r * 0.3, r * 0.16, r * 0.16), (255, 255, 255))
    mouth = mask_of(lambda d: d.chord(ell(C, 300, 72, 52), 10, 170, fill=255))
    paint(img, mouth, (50, 14, 60))
    paint(img, mul(ellipse_mask(C, 352, 30, 14), mouth), (255, 110, 150))
    for tx in (-30, 30):
        paint(img, mul(mask_of(lambda d, tx=tx: d.rectangle([C + tx - 10, 300, C + tx + 10, 318], fill=255)), mouth), (255, 255, 255))
    highlight(img, C - 70, 170, 50, 24, 0.4, 10)


def octopus(img):
    skin_hi, skin_lo = (255, 170, 190), (210, 50, 100)
    tent = layer()
    for i, bx in enumerate(np.linspace(C - 150, C + 150, 6)):
        pts_l, pts_r = [], []
        for k in range(24):
            t = k / 23.0
            y = 300 + t * 200
            x = bx + math.sin(t * 5 + i) * 26 * t + (bx - C) * 0.35 * t
            w = 30 * (1 - t) + 7
            pts_l.append((x - w, y))
            pts_r.append((x + w, y))
        m = poly_mask(pts_l + pts_r[::-1])
        paint(tent, m, linear((C, 300), (C, 500), skin_lo, shade(skin_lo, 0.6)))
        for k in range(3, 20, 5):
            t = k / 23.0
            paint(tent, ellipse_mask(bx + math.sin(t * 5 + i) * 26 * t + (bx - C) * 0.35 * t, 300 + t * 200, 7, 6), (255, 210, 220), 0.9)
    img.alpha_composite(tent)
    head = ellipse_mask(C, 230, 175, 160)
    paint(img, head, radial((C - 60, 160), 270, skin_hi, skin_lo, power=1.1))
    for (sx, sy, r) in [(-90, 150, 16), (70, 130, 11), (110, 190, 9), (-40, 110, 8)]:
        paint(img, ellipse_mask(C + sx, sy, r, r), (255, 215, 225), 0.6)
    for sx in (-1, 1):
        paint(img, ellipse_mask(C + sx * 62, 255, 42, 48), (255, 255, 255))
        paint(img, ellipse_mask(C + sx * 58, 264, 28, 34), (20, 22, 50))
        paint(img, ellipse_mask(C + sx * 58 - 10, 250, 10, 12), (255, 255, 255))
        paint(img, ellipse_mask(C + sx * 58 + 10, 280, 5, 5), (255, 255, 255), 0.8)
        paint(img, ellipse_mask(C + sx * 112, 318, 22, 12), (255, 110, 140), 0.5)
    mouth = mask_of(lambda d: d.arc(ell(C, 318, 26, 18), 20, 160, fill=255, width=7))
    paint(img, mouth, (120, 20, 60))
    highlight(img, C - 70, 140, 56, 26, 0.5, 10)


def robot(img):
    metal_hi, metal_lo = (214, 226, 246), (90, 104, 140)
    body = mask_of(lambda d: d.rounded_rectangle([C - 170, 400, C + 170, 600], 60, fill=255))
    paint(img, body, linear((C - 170, 400), (C + 170, 560), metal_hi, metal_lo))
    paint(img, mask_of(lambda d: d.rounded_rectangle([C - 60, 430, C + 60, 480], 14, fill=255)), (30, 40, 70))
    for i, c in enumerate([CYAN, WARM, PINK]):
        glow(img, ellipse_mask(C - 32 + i * 32, 455, 9, 9), c, 6, 0.9)
        paint(img, ellipse_mask(C - 32 + i * 32, 455, 9, 9), c)
    neck = mask_of(lambda d: d.rectangle([C - 40, 350, C + 40, 410], fill=255))
    paint(img, neck, (70, 82, 112))
    for y in (362, 380, 398):
        paint(img, mask_of(lambda d, y=y: d.rectangle([C - 40, y, C + 40, y + 5], fill=255)), (120, 134, 170))
    # antenne
    ImageDraw.Draw(img).line([(C, 120), (C, 60)], fill=(150, 162, 190, 255), width=10)
    glow(img, ellipse_mask(C, 54, 20, 20), PINK, 14, 0.9)
    paint(img, ellipse_mask(C, 54, 17, 17), radial((C - 5, 48), 20, (255, 220, 225), PINK))
    for sx in (-1, 1):
        ear = mask_of(lambda d, sx=sx: d.rounded_rectangle([C + sx * 168 - 22, 190, C + sx * 168 + 22, 290], 14, fill=255))
        paint(img, ear, linear((C, 190), (C, 290), (170, 180, 206), (80, 92, 124)))
    head = mask_of(lambda d: d.rounded_rectangle([C - 160, 110, C + 160, 360], 56, fill=255))
    paint(img, head, linear((C - 160, 110), (C + 160, 360), metal_hi, metal_lo))
    screen = mask_of(lambda d: d.rounded_rectangle([C - 124, 160, C + 124, 300], 34, fill=255))
    paint(img, screen, linear((C, 160), (C, 300), (16, 30, 64), (8, 12, 30)))
    for sx in (-1, 1):
        eye = mask_of(lambda d, sx=sx: d.rounded_rectangle([C + sx * 60 - 30, 196, C + sx * 60 + 30, 248], 18, fill=255))
        glow(img, eye, CYAN, 12, 0.9)
        paint(img, eye, radial((C + sx * 60, 216), 40, (220, 255, 250), CYAN))
    smile = mask_of(lambda d: d.arc(ell(C, 252, 40, 24), 25, 155, fill=255, width=8))
    glow(img, smile, CYAN, 6, 0.8)
    paint(img, smile, (200, 255, 248))
    for (bx, by) in [(C - 136, 132), (C + 136, 132), (C - 136, 338), (C + 136, 338)]:
        paint(img, ellipse_mask(bx, by, 8, 8), (110, 122, 156))
        paint(img, ellipse_mask(bx - 2, by - 2, 4, 4), (230, 236, 250))
    highlight(img, C - 90, 132, 50, 14, 0.55, 6)


def ringed_planet(img):
    cx, cy, r = C, C + 6, 118
    tilt = -18
    def ring_mask(front):
        outer = Image.new('L', (S, S), 0)
        d = ImageDraw.Draw(outer)
        d.ellipse(ell(cx, cy, 225, 58), fill=255)
        d.ellipse(ell(cx, cy, 165, 36), fill=0)
        d.ellipse(ell(cx, cy, 150, 30), fill=255)
        d.ellipse(ell(cx, cy, 136, 25), fill=0)
        half = Image.new('L', (S, S), 0)
        ImageDraw.Draw(half).rectangle([0, cy, S, S] if front else [0, 0, S, cy], fill=255)
        return mul(outer, half).rotate(tilt, resample=Image.BICUBIC, center=(cx, cy))
    ringcol = linear((cx - 220, cy), (cx + 220, cy), (255, 220, 160), (190, 120, 255))
    paint(img, ring_mask(False), ringcol, 0.85)
    planet = ellipse_mask(cx, cy, r, r)
    glow(img, planet, WARM, 22, 0.5)
    bands = layer()
    bd = ImageDraw.Draw(bands)
    cols = [(255, 196, 110), (236, 150, 80), (255, 214, 150), (214, 120, 70), (250, 180, 110), (200, 110, 80), (255, 206, 140)]
    for i in range(14):
        y0 = cy - r + i * (2 * r / 14)
        bd.rectangle([0, y0, S, y0 + 2 * r / 14 + 1], fill=cols[i % len(cols)] + (255,))
    bands = bands.rotate(tilt, resample=Image.BICUBIC, center=(cx, cy)).filter(ImageFilter.GaussianBlur(3))
    paint(img, planet, bands)
    shadow = sub(planet, ellipse_mask(cx - 40, cy - 40, r + 10, r + 10)).filter(ImageFilter.GaussianBlur(14))
    paint(img, mul(shadow, planet), (40, 10, 60), 0.6)
    highlight(img, cx - 50, cy - 60, 40, 24, 0.45, 12)
    paint(img, ring_mask(True), ringcol, 0.95)
    sparkle(img, C + 150, C - 150, 18)
    sparkle(img, C - 160, C + 140, 12)


def comet(img):
    cx, cy = C - 70, C + 70
    tail = layer()
    ang = math.radians(-45)
    for i, (w, col, a) in enumerate([(150, BLUE, 0.6), (96, CYAN, 0.8), (46, (230, 255, 252), 1.0)]):
        L = 360 - i * 30
        tx, ty = cx + L * math.cos(ang), cy + L * math.sin(ang)
        nx, ny = -math.sin(ang), math.cos(ang)
        pts = [(cx + nx * w * 0.5, cy + ny * w * 0.5), (tx + nx * 8, ty + ny * 8), (tx - nx * 8, ty - ny * 8),
               (cx - nx * w * 0.5, cy - ny * w * 0.5)]
        m = poly_mask(pts).filter(ImageFilter.GaussianBlur(14 - i * 3))
        fade = linear((cx, cy), (tx, ty), (255, 255, 255), (40, 40, 40)).convert('L')
        paint(tail, mul(m, fade), col, a)
    img.alpha_composite(tail)
    glow(img, ellipse_mask(cx, cy, 80, 80), CYAN, 34, 1.0)
    paint(img, ellipse_mask(cx, cy, 60, 60), radial((cx - 16, cy - 16), 70, (255, 255, 255), (110, 236, 226)))
    highlight(img, cx - 20, cy - 22, 18, 12, 0.7, 4)
    rnd = random.Random(7)
    for _ in range(9):
        t = rnd.uniform(0.25, 0.9)
        off = rnd.uniform(-60, 60)
        x = cx + 330 * t * math.cos(ang) - math.sin(ang) * off
        y = cy + 330 * t * math.sin(ang) + math.cos(ang) * off
        sparkle(img, x, y, rnd.uniform(5, 11), (220, 255, 250), 0.9)


def star_face(img):
    pts = star_points(C, C + 12, 190, 86, rot=-90)
    m = mask_of(lambda d: d.polygon(pts, fill=255)).filter(ImageFilter.GaussianBlur(1))
    rounded = m.filter(ImageFilter.GaussianBlur(10)).point(lambda v: 255 if v > 110 else 0)
    glow(img, rounded, WARM, 28, 0.9)
    paint(img, rounded, radial((C - 40, C - 40), 230, (255, 246, 190), (255, 170, 40), power=1.1))
    inner = rounded.filter(ImageFilter.GaussianBlur(20)).point(lambda v: 255 if v > 200 else 0)
    paint(img, sub(rounded, inner).filter(ImageFilter.GaussianBlur(6)), (230, 120, 20), 0.35)
    for sx in (-1, 1):
        paint(img, ellipse_mask(C + sx * 42, C, 16, 22), (60, 30, 20))
        paint(img, ellipse_mask(C + sx * 42 - 5, C - 8, 6, 7), (255, 255, 255))
        paint(img, ellipse_mask(C + sx * 80, C + 38, 20, 12), (255, 110, 120), 0.45)
    smile = mask_of(lambda d: d.arc(ell(C, C + 30, 34, 26), 20, 160, fill=255, width=8))
    paint(img, smile, (110, 50, 20))
    highlight(img, C - 50, C - 70, 22, 34, 0.5, 8)
    sparkle(img, C + 170, C - 160, 16)
    sparkle(img, C - 175, C + 120, 11)


def moon_cat(img):
    # grande lune derrière
    moon = ellipse_mask(C + 40, C - 30, 165, 165)
    glow(img, moon, (255, 240, 200), 26, 0.45)
    paint(img, moon, radial((C - 20, C - 90), 260, (255, 250, 228), (214, 200, 170)))
    for (x, y, r) in [(C + 110, C - 100, 26), (C + 150, C + 20, 18), (C + 60, C - 140, 14), (C + 170, C - 60, 10)]:
        paint(img, ellipse_mask(x, y, r, r), (196, 182, 156), 0.6)
    # chat
    fur_hi, fur_lo = (78, 70, 140), (26, 22, 60)
    body = ellipse_mask(C - 20, 520, 190, 150)
    paint(img, body, linear((C, 380), (C, 520), fur_hi, fur_lo))
    for sx in (-1, 1):
        ear = poly_mask([(C - 20 + sx * 40, 210), (C - 20 + sx * 135, 120), (C - 20 + sx * 140, 260)])
        paint(img, ear, linear((C, 120), (C, 260), fur_hi, fur_lo))
        inner = poly_mask([(C - 20 + sx * 62, 216), (C - 20 + sx * 125, 152), (C - 20 + sx * 126, 246)])
        paint(img, inner, (230, 120, 170), 0.8)
    head = ellipse_mask(C - 20, 300, 150, 125)
    paint(img, head, radial((C - 80, 230), 260, fur_hi, fur_lo, power=1.0))
    # croissant sur le front
    cres = sub(ellipse_mask(C - 20, 228, 24, 24), ellipse_mask(C - 8, 220, 22, 22))
    glow(img, cres, WARM, 6, 0.9)
    paint(img, cres, (255, 214, 120))
    for sx in (-1, 1):
        ex = C - 20 + sx * 58
        eye = ellipse_mask(ex, 292, 30, 26)
        glow(img, eye, GREEN, 10, 0.8)
        paint(img, eye, radial((ex, 292), 32, (230, 255, 200), (90, 220, 120)))
        paint(img, ellipse_mask(ex, 292, 8, 23), (10, 20, 18))
        paint(img, ellipse_mask(ex - 9, 282, 6, 6), (255, 255, 255))
    nose = poly_mask([(C - 34, 330), (C - 6, 330), (C - 20, 346)])
    paint(img, nose, (255, 130, 170))
    mouth = mask_of(lambda d: (d.arc(ell(C - 32, 350, 12, 10), 0, 180, fill=255, width=5),
                               d.arc(ell(C - 8, 350, 12, 10), 0, 180, fill=255, width=5)))
    paint(img, mouth, (200, 190, 240), 0.8)
    wh = mask_of(lambda d: [d.line([(C - 20 + sx * 70, 336 + k * 14), (C - 20 + sx * 160, 322 + k * 26)], fill=255, width=4)
                            for sx in (-1, 1) for k in (0, 1)])
    paint(img, wh, (220, 220, 255), 0.7)


def rocket(img):
    rk = layer()
    cx = C
    flame = poly_mask([(cx - 40, 380), (cx + 40, 380), (cx + 22, 450), (cx, 510), (cx - 22, 450)])
    glow(rk, flame, WARM, 24, 0.9)
    paint(rk, flame, linear((cx, 380), (cx, 500), (255, 240, 180), (255, 90, 60)))
    paint(rk, poly_mask([(cx - 20, 380), (cx + 20, 380), (cx, 450)]), (255, 255, 230))
    for sx in (-1, 1):
        fin = poly_mask([(cx + sx * 60, 280), (cx + sx * 118, 380), (cx + sx * 112, 410), (cx + sx * 50, 370)])
        paint(rk, fin, linear((cx, 280), (cx, 410), PINK, shade(PINK, 0.6)))
    body = mask_of(lambda d: d.ellipse(ell(cx, 250, 72, 170), fill=255))
    body = mul(body, mask_of(lambda d: d.rectangle([0, 0, S, 382], fill=255)))
    paint(rk, body, linear((cx - 72, 0), (cx + 72, 0), (250, 252, 255), (150, 160, 190)))
    nose = mul(body, mask_of(lambda d: d.rectangle([0, 0, S, 150], fill=255)))
    paint(rk, nose, linear((cx - 72, 0), (cx + 72, 0), (255, 120, 140), shade(PINK, 0.65)))
    band = mul(body, mask_of(lambda d: d.rectangle([0, 340, S, 382], fill=255)))
    paint(rk, band, (90, 100, 140))
    paint(rk, ellipse_mask(cx, 230, 42, 42), (70, 80, 110))
    paint(rk, ellipse_mask(cx, 230, 32, 32), radial((cx - 10, 220), 40, (200, 255, 250), (20, 120, 150)))
    highlight(rk, cx - 12, 216, 12, 8, 0.8, 2)
    paint(rk, poly_mask([(cx - 6, 382), (cx + 6, 382), (cx + 4, 420), (cx - 4, 420)]), (110, 120, 160))
    highlight(rk, cx - 34, 270, 10, 70, 0.4, 6)
    rk = rk.rotate(-35, resample=Image.BICUBIC, center=(C, C))
    for (x, y, r) in [(C - 150, C + 120, 30), (C - 120, C + 170, 22), (C - 180, C + 60, 18)]:
        paint(img, ellipse_mask(x, y, r, r).filter(ImageFilter.GaussianBlur(4)), (200, 210, 240), 0.35)
    img.alpha_composite(rk)
    sparkle(img, C + 150, C + 140, 14)
    sparkle(img, C - 140, C - 150, 10)


def ufo(img):
    cx, cy = C, C - 30
    beam = poly_mask([(cx - 60, cy + 40), (cx + 60, cy + 40), (cx + 150, S), (cx - 150, S)])
    paint(img, beam.filter(ImageFilter.GaussianBlur(10)), linear((cx, cy), (cx, S), (160, 255, 190), (40, 120, 70)), 0.45)
    dome = mul(ellipse_mask(cx, cy - 10, 84, 90), mask_of(lambda d: d.rectangle([0, 0, S, cy + 4], fill=255)))
    paint(img, dome, (60, 120, 160), 0.5)
    # petit alien dans le dôme
    paint(img, ellipse_mask(cx, cy - 26, 40, 44), radial((cx - 12, cy - 40), 56, (190, 255, 200), (60, 170, 90)))
    for sx in (-1, 1):
        paint(img, ellipse_mask(cx + sx * 16, cy - 26, 11, 15), (15, 20, 30))
        paint(img, ellipse_mask(cx + sx * 16 - 3, cy - 32, 4, 4), (255, 255, 255))
    paint(img, dome, linear((cx - 80, cy - 90), (cx + 80, cy), (210, 250, 255), (60, 170, 220)), 0.35)
    paint(img, mul(sub(ellipse_mask(cx - 18, cy - 50, 50, 40), ellipse_mask(cx - 8, cy - 40, 50, 40)), dome), (255, 255, 255), 0.75)
    saucer = ellipse_mask(cx, cy + 20, 200, 58)
    glow(img, saucer, CYAN, 16, 0.4)
    paint(img, saucer, linear((cx, cy - 30), (cx, cy + 80), (226, 234, 250), (90, 100, 136)))
    paint(img, mul(ellipse_mask(cx, cy + 34, 200, 46), sub(saucer, ellipse_mask(cx, cy + 10, 200, 50))), (60, 66, 100), 0.8)
    rim = sub(ellipse_mask(cx, cy + 12, 190, 40), ellipse_mask(cx, cy + 8, 186, 34))
    paint(img, rim, (255, 255, 255), 0.6)
    for i, a in enumerate(np.linspace(math.radians(200), math.radians(340), 7)):
        x = cx + 170 * math.cos(a)
        y = cy + 32 - 26 * math.sin(a)
        c = [CYAN, WARM, PINK, GREEN][i % 4]
        glow(img, ellipse_mask(x, y, 11, 9), c, 8, 0.9)
        paint(img, ellipse_mask(x, y, 10, 8), mix(c, (255, 255, 255), 0.4))
    sparkle(img, C + 160, C - 150, 13)
    sparkle(img, C - 170, C - 120, 9)


def black_hole(img):
    cx, cy = C, C
    tilt = -16
    def disk(front):
        m = Image.new('L', (S, S), 0)
        d = ImageDraw.Draw(m)
        d.ellipse(ell(cx, cy, 222, 78), fill=255)
        d.ellipse(ell(cx, cy, 104, 28), fill=0)
        half = Image.new('L', (S, S), 0)
        ImageDraw.Draw(half).rectangle([0, cy, S, S] if front else [0, 0, S, cy], fill=255)
        return mul(m, half).filter(ImageFilter.GaussianBlur(6)).rotate(tilt, resample=Image.BICUBIC, center=(cx, cy))
    hot = radial((cx, cy), 225, (255, 236, 190), (150, 40, 220), power=1.7)
    paint(img, disk(False), hot, 0.9)
    lens = sub(ellipse_mask(cx, cy, 112, 112), ellipse_mask(cx, cy, 96, 96)).filter(ImageFilter.GaussianBlur(5))
    glow(img, lens, WARM, 14, 0.8)
    paint(img, lens, (255, 220, 170), 0.95)
    paint(img, ellipse_mask(cx, cy, 98, 98), (0, 0, 0))
    glow(img, disk(True), VIOLET, 16, 0.6)
    paint(img, disk(True), hot, 1.0)
    sparkle(img, C + 165, C - 150, 12, (230, 200, 255))
    sparkle(img, C - 150, C + 160, 10, (255, 220, 180))


def galaxy(img):
    h = np.zeros((S, S))
    yy, xx = _YY - C, _XX - C
    r = np.hypot(xx, yy) + 1e-6
    th = np.arctan2(yy * 1.35, xx)
    arms = np.cos(2 * (th - np.log(r / 14.0) * 1.7))
    h = np.clip(arms, 0, 1) ** 1.6 * np.exp(-r / 170.0) * 1.7 + np.exp(-(r / 52.0) ** 2) * 1.3
    h = np.clip(h, 0, 1)
    rgb = np.dstack([h * 160 + h ** 3 * 95, h * 140 + h ** 3 * 115, h * 255])
    a = np.clip(h * 1.4, 0, 1) * 255
    g = Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a]).astype(np.uint8), 'RGBA')
    g = g.rotate(-25, resample=Image.BICUBIC, center=(C, C)).filter(ImageFilter.GaussianBlur(3))
    glow(img, g.getchannel('A'), BLUE, 20, 0.6)
    img.alpha_composite(g)
    rnd = random.Random(16)
    for _ in range(70):
        rr = abs(rnd.gauss(0, 120))
        t = rnd.uniform(0, 2 * math.pi)
        x, y = C + rr * math.cos(t), C + rr * math.sin(t) / 1.35
        s = rnd.uniform(1.5, 4)
        paint(img, ellipse_mask(x, y, s, s), rnd.choice([(255, 255, 255), (190, 220, 255), (255, 210, 240)]), rnd.uniform(0.5, 1))
    glow(img, ellipse_mask(C, C, 40, 34), (255, 236, 210), 22, 0.9)
    paint(img, ellipse_mask(C, C, 22, 19), (255, 250, 240))
    sparkle(img, C + 160, C - 140, 14, (220, 230, 255))


AVATARS = [
    ('avatar_01', 'Astronaute cyan', 'Cyan astronaut', CYAN, astronaut(CYAN)),
    ('avatar_02', 'Astronaute violet', 'Violet astronaut', VIOLET, astronaut(VIOLET)),
    ('avatar_03', 'Astronaute ambre', 'Amber astronaut', WARM, astronaut(WARM)),
    ('avatar_04', 'Astronaute rose', 'Pink astronaut', PINK, astronaut(PINK)),
    ('avatar_05', 'Alien vert', 'Green alien', GREEN, alien_green),
    ('avatar_06', 'Alien à trois yeux', 'Three-eyed alien', BLUE, alien_three_eyes),
    ('avatar_07', 'Poulpe cosmique', 'Cosmic octopus', PINK, octopus),
    ('avatar_08', 'Robot', 'Robot', CYAN, robot),
    ('avatar_09', 'Planète à anneaux', 'Ringed planet', WARM, ringed_planet),
    ('avatar_10', 'Comète', 'Comet', BLUE, comet),
    ('avatar_11', 'Étoile', 'Star', VIOLET, star_face),
    ('avatar_12', 'Chat lunaire', 'Moon cat', GREEN, moon_cat),
    ('avatar_13', 'Fusée', 'Rocket', WARM, rocket),
    ('avatar_14', 'Soucoupe volante', 'Flying saucer', CYAN, ufo),
    ('avatar_15', 'Trou noir', 'Black hole', VIOLET, black_hole),
    ('avatar_16', 'Galaxie spirale', 'Spiral galaxy', BLUE, galaxy),
]
COLUMNS = 4


def build(out_assets, out_data, log):
    rows = (len(AVATARS) + COLUMNS - 1) // COLUMNS
    atlas = Image.new('RGBA', (COLUMNS * CELL, rows * CELL), (0, 0, 0, 0))
    entries = []
    for i, (aid, fr, en, rim, fn) in enumerate(AVATARS):
        big = compose(rim, 1000 + i, fn)
        small = big.resize((CELL, CELL), Image.LANCZOS)
        atlas.paste(small, ((i % COLUMNS) * CELL, (i // COLUMNS) * CELL))
        entries.append({'id': aid, 'name_fr': fr, 'name_en': en, 'rim': '#%02X%02X%02X' % rim})
    atlas.save(out_assets / 'avatars.png', optimize=True)
    with open(out_data / 'avatars.json', 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'cell': CELL, 'columns': COLUMNS, 'avatars': entries}, f, ensure_ascii=False, indent=1)
        f.write('\n')
    log(f'  {len(entries)} avatars, atlas {atlas.size[0]}x{atlas.size[1]}')
