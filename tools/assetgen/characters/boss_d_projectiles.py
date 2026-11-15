"""Projectiles of Singularis, le Coeur Effondre: rows appended to sprites/projectiles/projectiles.png
(16x16, pivot [8, 8], 4-frame loops, drawn travelling right), after the sanctuary guardians' rows.
White-hot, so they read with every phase colour of the boss.

  star_shard  a splinter of starlight: white diamond, pale gold core, short streak (fans, homing)
  meteor      a lump of dead crust burning in a white-hot head and a tail of fire (star rain)
"""
import math

from common.palette import hex_rgba
from raster import Canvas, Mat

W = H = 16
TAU = 2 * math.pi
Hx = hex_rgba

SHARD = Mat([Hx('#ffffff'), Hx('#fff8de'), Hx('#ffe6a0'), Hx('#d8b0ff')], emissive=True, outline=Hx('#2a1840'),
            name="pd_shard", thresholds=[0.84, 0.6, 0.36])
STREAK = Mat([Hx('#fff8de'), Hx('#ffe6a0'), Hx('#c8a0ff')], emissive=True, outline=None, name="pd_streak",
             thresholds=[0.7, 0.4])
ROCK = Mat([Hx('#8a80a0'), Hx('#5c536f'), Hx('#3b3450'), Hx('#241e33')], outline=Hx('#0b0f1e'), name="pd_rock",
           thresholds=[0.84, 0.6, 0.36])
FIRE = Mat([Hx('#ffffff'), Hx('#fff0c0'), Hx('#ffc060'), Hx('#ff7a3a'), Hx('#c83a5a')], emissive=True, outline=None,
           name="pd_fire", thresholds=[0.86, 0.68, 0.48, 0.28])


def star_shard():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        cv.part(line=False)
        cv.capsule((9.0, 8.0), (1.0 + (i % 2), 8.0), 2.0, 0.4, STREAK, flat=0.5)
        cv.part()
        tw = 1.0 + 0.25 * math.sin(TAU * i / 4)
        cv.poly([(14.5, 8.0), (9.5, 8.0 - 3.2 * tw), (5.0, 8.0), (9.5, 8.0 + 3.2 * tw)], SHARD, bevel=1.0, base=0.7)
        cv.part(line=False)
        cv.disc((10.0, 8.0), 1.3, SHARD, flat=1.0)
        img = cv.render()
        # twinkle
        if i % 2 == 0:
            img[3, 10] = Hx('#ffffff')
            img[12, 10] = Hx('#ffffff')
        out.append(img)
    return out


def meteor():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        cv.part(line=False)
        # a tail of fire behind (to the left), flickering
        for k, (r0, ln, inten) in enumerate(((3.8, 9.0, 0.35), (2.8, 7.0, 0.55), (1.6, 5.0, 0.8))):
            wob = 0.7 * math.sin(TAU * i / 4 + k)
            cv.capsule((10.0, 8.0), (10.0 - ln - (i % 2), 8.0 + wob), r0, 0.6, FIRE, flat=inten)
        cv.part()
        rot = TAU * i / 4
        pts = [(10.5 + math.cos(rot + a) * r, 8.0 + math.sin(rot + a) * r)
               for a, r in ((0, 3.4), (1.2, 2.8), (2.3, 3.6), (3.4, 2.9), (4.6, 3.3))]
        cv.poly(pts, ROCK, bevel=1.0, base=0.58)
        cv.part(line=False)
        cv.disc((12.6, 8.0), 1.6, FIRE, flat=0.95)  # the white-hot head
        out.append(cv.render())
    return out


CLIPS = [
    ("star_shard", star_shard, 14),
    ("meteor", meteor, 12),
]
NAMES = [c for c, _, _ in CLIPS]


def add_rows(sheet):
    for name, fn, fps in CLIPS:
        sheet.add(name, fn(), fps=fps, loop=True)
