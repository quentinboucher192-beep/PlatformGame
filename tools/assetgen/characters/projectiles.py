"""Projectile sheet (docs/ASSET_SPEC.md section 5): 16x16 frames, pivot
[8, 8], one looping 4-frame clip per row, all travelling to the right.
Rows 0-5 = v1 clips below, rows 6+ = v2 clips from player_projectiles.py."""
import math

import numpy as np

import effects as FX
import player_projectiles
from common.palette import AMBER, CYAN, DANGER, MOSS, VIOLET, hex_rgba
from raster import Canvas, Mat, put
from sheet import Sheet

W = H = 16
TAU = 2 * math.pi
DVIO = hex_rgba('#2a0f4a')
DTEAL = hex_rgba('#0f3b3a')
DRED = hex_rgba('#5a0f1e')

WAVE = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=DTEAL, name="pw", thresholds=[0.82, 0.6, 0.35])
BOLT = Mat([hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1]], emissive=True, outline=DRED, name="bolt",
           thresholds=[0.8, 0.6, 0.35])
SPORE = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], name="spore", outline=DVIO,
            thresholds=[0.86, 0.62, 0.36])
ORB = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], emissive=True, outline=DVIO, name="orb",
          thresholds=[0.8, 0.58, 0.34])
THORN = Mat([hex_rgba('#f2ead8'), hex_rgba('#d8c8a8'), hex_rgba('#a89478'), hex_rgba('#6e5c48')], name="thorn",
            thresholds=[0.82, 0.6, 0.36])
VINE = Mat([MOSS[2], MOSS[1], MOSS[0]], name="pvine", thresholds=[0.7, 0.4])
BORB_OUT = Mat([DANGER[2], DANGER[1], DANGER[0]], emissive=True, outline=DRED, name="borb_out", thresholds=[0.7, 0.4])
BORB_IN = Mat([CYAN[3], CYAN[2], CYAN[1]], emissive=True, name="borb_in", thresholds=[0.75, 0.45])


def player_wave():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        s = 1.0 + 0.06 * math.sin(TAU * i / 4)
        outer = lambda px, py: (px - 6.0) ** 2 / (6.4 * s) ** 2 + (py - 8.0) ** 2 / (7.0 * s) ** 2 <= 1
        inner = lambda px, py: (px - 3.6) ** 2 / (5.6 * s) ** 2 + (py - 8.0) ** 2 / (6.6 * s) ** 2 <= 1
        mask = outer(cv.px, cv.py) & ~inner(cv.px, cv.py) & (cv.px > 5.0)
        # brightness: outer edge brightest
        d = np.sqrt((cv.px - 6.0) ** 2 / (6.4 * s) ** 2 + (cv.py - 8.0) ** 2 / (7.0 * s) ** 2)
        inten = 0.25 + 0.75 * d
        cv.put(mask, inten, WAVE)
        img = cv.render()
        # shimmer pixels along the edge + trailing sparks
        rng = np.random.default_rng(10 + i)
        for _ in range(3):
            y = int(rng.integers(3, 13))
            x = int(rng.integers(0, 5))
            put(img, x, y, CYAN[1] if rng.random() > 0.5 else CYAN[0])
        put(img, 12 + (i % 2), 8, CYAN[3])
        out.append(img)
    return out


def drone_bolt():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        L = 4.5 + 0.5 * (i % 2)
        cv.capsule((13.0 - 2 * L, 8.0), (12.5, 8.0), 1.2, 2.3, BOLT, flat=None)
        cv.part(line=False)
        cv.capsule((13.0 - 2 * L + 2.0, 8.0), (12.0, 8.0), 0.4, 1.2, BOLT, flat=1.0)
        img = cv.render()
        # flickering tail
        for k, x in enumerate(range(1, 4)):
            if (x + i) % 2 == 0:
                put(img, x, 8 + ((i + k) % 3 - 1), DANGER[1])
        out.append(img)
    return out


def spore():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        a0 = TAU * i / 16
        for k in range(6):
            a = a0 + TAU * k / 6
            cv.part(line=False)
            cv.capsule((8.5, 8.0), (8.5 + math.cos(a) * 5.2, 8.0 + math.sin(a) * 5.2), 1.0, 0.45, SPORE, flat=0.45)
        cv.part()
        cv.disc((8.5, 8.0), 3.7, SPORE)
        for k in range(3):
            a = a0 * 2 + TAU * k / 3
            cv.px_set(8.5 + math.cos(a) * 2.0, 8.0 + math.sin(a) * 2.0, SPORE, inten=0.95, decal=True)
        img = cv.render()
        put(img, 1 + (i % 2), 7 + (i % 3), VIOLET[1])
        out.append(img)
    return out


def wisp_orb():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        # flame tail behind (left)
        cv.part(line=False)
        wob = math.sin(TAU * i / 4)
        cv.capsule((8.5, 8.0), (2.0, 8.0 + wob), 3.6, 0.8, ORB, flat=0.3)
        cv.capsule((8.5, 8.0), (4.0, 8.0 - wob * 0.5), 2.2, 0.6, ORB, flat=0.55)
        cv.part()
        cv.disc((9.0, 8.0), 3.8, ORB)
        cv.disc((8.5, 7.5), 1.6, ORB, flat=1.0, decal=True)
        img = cv.render()
        a = TAU * i / 4
        put(img, int(9 + math.cos(a) * 5.5), int(8 + math.sin(a) * 5.5), VIOLET[2])
        out.append(img)
    return out


def boss_thorn():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        wob = [0.0, 0.06, 0.0, -0.06][i]
        tip = (15.2, 8.0 + wob * 10)
        base = (2.5, 8.0 - wob * 6)
        cv.part()
        cv.poly([(base[0], base[1] - 2.4), tip, (base[0], base[1] + 2.4), (base[0] - 1.0, base[1])], THORN,
                bevel=1.0, base=0.62)
        cv.part()
        cv.capsule((2.0, base[1] - 1.5), (5.5, base[1] + 1.8), 0.8, 0.8, VINE)
        cv.part()
        cv.poly([(6.0, base[1] - 1.0), (7.5, base[1] - 4.0), (8.0, base[1] - 0.8)], THORN, flat=0.7)
        img = cv.render()
        # glint travelling along the spike
        gx = 6 + 2 * i
        put(img, gx, int(round(8 + wob * 10 * (gx - 2.5) / 12.7)) - 1, hex_rgba('#ffffff'))
        out.append(img)
    return out


def boss_orb():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        cv.part()
        cv.disc((8.0, 8.0), 6.4, BORB_OUT)
        cv.part(line=False)
        cv.disc((8.0, 8.0), 3.8, BORB_IN, decal=True)
        # swirl arms (rotating) mixing red into the cyan core
        for k in range(2):
            a = TAU * i / 8 + math.pi * k
            pts = [(8.0 + math.cos(a + t * 1.6) * (1.0 + t * 4.6), 8.0 + math.sin(a + t * 1.6) * (1.0 + t * 4.6))
                   for t in np.linspace(0, 1, 6)]
            cv.chain(pts, [0.5] * 6, BORB_OUT, flat=0.9, decal=True)
        cv.px_set(6.5, 6.5, BORB_IN, inten=1.0, decal=True)
        img = cv.render()
        out.append(img)
    return out


def build():
    s = Sheet("sprites/projectiles/projectiles.png", W, H, [8, 8])
    s.add("player_wave", player_wave(), fps=16, loop=True)
    s.add("drone_bolt", drone_bolt(), fps=16, loop=True)
    s.add("spore", spore(), fps=10, loop=True)
    s.add("wisp_orb", wisp_orb(), fps=12, loop=True)
    s.add("boss_thorn", boss_thorn(), fps=12, loop=True)
    s.add("boss_orb", boss_orb(), fps=12, loop=True)
    player_projectiles.add_rows(s)  # v2 rows 6-18
    s.save("animations/projectiles.json")
