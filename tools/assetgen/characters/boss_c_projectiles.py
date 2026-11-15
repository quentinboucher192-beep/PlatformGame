"""2.19 projectiles of the sanctuary guardians: rows appended to sprites/projectiles/projectiles.png
(16x16, pivot [8, 8], 4-frame loops, drawn travelling right), after the v2 rows.

  pollen        Mycelia   glowing mint spore puff with a lilac fuzz (falls between the thorn walls)
  aurora_shard  Aurorax   ice crystal with a green-to-violet aurora trail (fan of shards, slows)
  solar_flame   Pyrogyre  teardrop sun flame (the six flames of the Couronne de feu)
  solar_bolt    Pyrogyre  small white-hot bolt with an orange tail (Spirale solaire)
  void_star     Nyxara    four-point star with a pink core and a dark violet halo (seeking stars)
"""
import math

import numpy as np

from common.palette import hex_rgba
from raster import Canvas, Mat, put

W = H = 16
TAU = 2 * math.pi
Hx = hex_rgba

MINT = Mat([Hx('#f2fffb'), Hx('#b4fff0'), Hx('#7dffd6'), Hx('#39c8b0')], emissive=True, outline=Hx('#1a3f4a'),
           name="pc_mint", thresholds=[0.82, 0.6, 0.36])
FUZZ = Mat([Hx('#f0d0ff'), Hx('#c890f0'), Hx('#9a60d0')], emissive=True, outline=None, name="pc_fuzz",
           thresholds=[0.7, 0.4])
ICE = Mat([Hx('#ffffff'), Hx('#c8f4ff'), Hx('#8cd2f2'), Hx('#4480c6')], outline=Hx('#13264a'), name="pc_ice",
          thresholds=[0.84, 0.6, 0.36])
AUR = Mat([Hx('#d8ffe0'), Hx('#78e68c'), Hx('#4cb8b4'), Hx('#8a7ae8')], emissive=True, outline=None, name="pc_aur",
          thresholds=[0.8, 0.58, 0.34])
FLAME = Mat([Hx('#fffef0'), Hx('#fff3b0'), Hx('#ffd86a'), Hx('#ffb13b'), Hx('#ff7a2a')], emissive=True,
            outline=Hx('#5a1a08'), name="pc_flame", thresholds=[0.88, 0.7, 0.5, 0.3])
FLAME_NO = Mat([Hx('#ffd86a'), Hx('#ffb13b'), Hx('#ff7a2a'), Hx('#e2451e')], emissive=True, outline=None,
               name="pc_flame_no", thresholds=[0.75, 0.5, 0.28])
BOLT = Mat([Hx('#ffffff'), Hx('#fff6c2'), Hx('#ffd86a'), Hx('#ff9a3d')], emissive=True, outline=Hx('#5a1a08'),
           name="pc_bolt", thresholds=[0.82, 0.6, 0.36])
STAR = Mat([Hx('#ffffff'), Hx('#ffe0ee'), Hx('#ff9ab8'), Hx('#ff7696')], emissive=True, outline=Hx('#2a0f4a'),
           name="pc_star", thresholds=[0.82, 0.6, 0.36])
HALO = Mat([Hx('#8a4ee0'), Hx('#5b2a8c'), Hx('#2a1240')], emissive=True, outline=None, name="pc_halo",
           thresholds=[0.6, 0.3])


def pollen():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        a0 = TAU * i / 16
        cv.part(line=False)
        for k in range(8):
            a = a0 + TAU * k / 8
            ln = 5.6 + (0.8 if (k + i) % 2 else 0.0)
            cv.capsule((8.0, 8.0), (8.0 + math.cos(a) * ln, 8.0 + math.sin(a) * ln), 0.7, 0.5, FUZZ, flat=0.45)
            cv.px_set(8.0 + math.cos(a) * (ln + 0.6), 8.0 + math.sin(a) * (ln + 0.6), FUZZ, inten=0.9)
        cv.part()
        cv.disc((8.0, 8.0), 3.2, MINT)
        cv.disc((7.4, 7.4), 1.3, MINT, flat=1.0, decal=True)
        out.append(cv.render())
    return out


def aurora_shard():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        wob = [0.0, 0.6, 0.0, -0.6][i]
        cv.part(line=False)
        cv.capsule((7.0, 8.0), (0.5, 8.0 + wob), 2.4, 0.7, AUR, flat=0.4)
        cv.capsule((7.0, 8.0), (2.5, 8.0 - wob * 0.5), 1.2, 0.5, AUR, flat=0.75)
        cv.part()
        cv.poly([(5.5, 8.0), (9.0, 5.2), (15.2, 8.0), (9.0, 10.8)], ICE, bevel=1.0, base=0.62)
        cv.part(line=False)
        cv.line((7.0, 7.5), (13.5, 7.8), ICE, inten=1.0, decal=True)
        img = cv.render()
        put(img, 3 + (i % 2), 5 + i, hex_rgba('#b48cff'))
        put(img, 2 + (i % 3), 11 - i, hex_rgba('#78e68c'))
        out.append(img)
    return out


def solar_flame():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        ph = TAU * i / 4
        cv.part(line=False)
        for k, (dy, ln) in enumerate(((-2.6, 7.0), (0.0, 9.0), (2.6, 6.5))):
            tip = (9.0 - ln, 8.0 + dy * 1.3 + 0.9 * math.sin(ph + k * 2.1))
            cv.capsule((8.5, 8.0 + dy * 0.5), tip, 2.6, 0.5, FLAME_NO, flat=0.45)
        cv.part()
        cv.disc((9.5, 8.0), 4.4 + 0.25 * math.sin(ph), FLAME)
        cv.part(line=False)
        cv.disc((10.2, 7.4), 2.4, FLAME, flat=1.0, decal=True)
        img = cv.render()
        put(img, 1 + (i % 2), 4 + 2 * (i % 3), hex_rgba('#ffd86a'))
        out.append(img)
    return out


def solar_bolt():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        L = 5.0 + 0.6 * (i % 2)
        cv.part(line=False)
        cv.capsule((12.0 - 2 * L, 8.0), (11.0, 8.0), 0.6, 2.0, FLAME_NO, flat=0.4)
        cv.part()
        cv.capsule((12.5 - L, 8.0), (12.6, 8.0), 1.4, 2.6, BOLT)
        cv.part(line=False)
        cv.capsule((13.0 - L, 8.0), (12.4, 8.0), 0.5, 1.3, BOLT, flat=1.0)
        img = cv.render()
        for k, x in enumerate(range(1, 4)):
            if (x + i) % 2 == 0:
                put(img, x, 8 + ((i + k) % 3 - 1), hex_rgba('#ff9a3d'))
        out.append(img)
    return out


def void_star():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        rot = TAU * i / 16
        cv.part(line=False)
        cv.capsule((8.0, 8.0), (1.5, 8.0), 3.4, 0.8, HALO, flat=0.35)
        cv.disc((8.5, 8.0), 4.6, HALO, flat=0.5)
        cv.part()
        pts = []
        for k in range(8):
            a = rot + TAU * k / 8
            r = 6.4 if k % 2 == 0 else 2.0
            pts.append((8.5 + math.cos(a) * r, 8.0 + math.sin(a) * r))
        cv.poly(pts, STAR, flat=0.55)
        cv.part(line=False)
        cv.disc((8.5, 8.0), 1.6, STAR, flat=1.0)
        img = cv.render()
        out.append(img)
    return out


CLIPS = [
    ("pollen", pollen, 10),
    ("aurora_shard", aurora_shard, 12),
    ("solar_flame", solar_flame, 14),
    ("solar_bolt", solar_bolt, 16),
    ("void_star", void_star, 12),
]
NAMES = [c for c, _, _ in CLIPS]


def add_rows(sheet):
    for name, fn, fps in CLIPS:
        sheet.add(name, fn(), fps=fps, loop=True)
