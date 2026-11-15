"""Sprites of the 2.19 powers (sprites/projectiles/power_fx.png + animations/power_fx.json),
a sheet of its own so that the shared projectile sheet is left untouched:
  boomerang  the energy blade of the Lame boomerang, 8 frames of a full turn (drawn
             centred, spinning clockwise; the engine mirrors it when thrown to the left)
  beacon     the recall beacon (Balise de rappel): a small violet pylon with a pulsing
             crystal, 8 frames, standing on the bottom of the frame
Frames are 24x24, pivot [12, 24] (the blade is drawn centred by the engine).

    python3 tools/assetgen/characters/generate.py power_fx
"""
import math

from common.palette import CYAN, OUTLINE, STONE, hex_rgba
from raster import Canvas, Mat, put
from sheet import Sheet

W = H = 24
TAU = 2 * math.pi
WHITE = hex_rgba('#ffffff')
LILAC = [hex_rgba(c) for c in ('#efe2ff', '#b48cff', '#8a5cff', '#4a2a8a')]

BLADE = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=hex_rgba('#0f3b3a'), name="fx_blade",
            thresholds=[0.8, 0.58, 0.34])
CRYSTAL = Mat(LILAC, emissive=True, outline=hex_rgba('#24104a'), name="fx_beacon_crystal",
              thresholds=[0.82, 0.58, 0.34])
METAL = Mat([hex_rgba('#c8d6f0'), hex_rgba('#8ca0c8'), STONE[3], STONE[2]], outline=OUTLINE, name="fx_beacon_metal",
            thresholds=[0.8, 0.56, 0.32])


def _rot(p, a, c=(12.0, 12.0)):
    x, y = p[0] - c[0], p[1] - c[1]
    return (c[0] + x * math.cos(a) - y * math.sin(a), c[1] + x * math.sin(a) + y * math.cos(a))


def boomerang():
    """A curved V blade (two arms meeting at a rounded elbow), a full turn over 8 frames."""
    out = []
    arm1 = [(4.5, 6.0), (6.0, 10.5), (8.5, 14.0), (12.0, 16.0)]
    arm2 = [(12.0, 16.0), (15.5, 15.0), (19.0, 11.5)]
    for i in range(8):
        a = TAU * i / 8
        cv = Canvas(W, H)
        cv.part()
        pts = [_rot(p, a) for p in arm1 + arm2[1:]]
        cv.chain(pts, [1.3, 1.9, 2.2, 2.3, 2.0, 1.3], BLADE)
        cv.part(line=False)
        core = [_rot(p, a) for p in [(7.0, 11.0), (9.0, 13.8), (12.0, 15.2), (15.0, 14.3)]]
        cv.chain(core, [0.7, 0.8, 0.8, 0.6], BLADE, flat=1.0, decal=True)
        img = cv.render()
        # the elbow glints
        ex, ey = _rot((12.0, 15.6), a)
        put(img, int(ex), int(ey), WHITE)
        out.append(img)
    return out


def beacon():
    """Violet recall beacon: a short metal pylon on a base, a floating crystal on top that
    pulses (8 frames)."""
    out = []
    for i in range(8):
        pulse = 0.5 + 0.5 * math.sin(TAU * i / 8)
        bob = [0, 0, -1, -1, -1, -1, 0, 0][i]
        cv = Canvas(W, H)
        cv.part()
        cv.poly([(6.5, 23.9), (17.5, 23.9), (16.0, 20.5), (8.0, 20.5)], METAL, bevel=1.0, base=0.58)
        cv.part()
        cv.poly([(10.5, 20.8), (13.5, 20.8), (13.0, 11.5), (11.0, 11.5)], METAL, bevel=0.9, base=0.62)
        cv.part()
        cy = 7.5 + bob
        cv.poly([(12.0, cy - 5.2), (15.2, cy), (12.0, cy + 4.2), (8.8, cy)], CRYSTAL, bevel=1.2, base=0.6)
        img = cv.render()
        put(img, 11, int(cy) - 2, WHITE if pulse > 0.5 else LILAC[0])
        # rune lights on the pylon
        for k, y in enumerate((18, 15, 13)):
            if pulse * 3 > k:
                put(img, 12, y, LILAC[1])
        # sparks orbiting the crystal
        for k in range(2):
            ang = TAU * i / 8 + math.pi * k
            x = 12 + math.cos(ang) * 6.0
            y = cy + math.sin(ang) * 2.0
            if img[int(y), int(x), 3] == 0:
                put(img, int(x), int(y), LILAC[0] if k == 0 else LILAC[1])
        out.append(img)
    return out


def build():
    s = Sheet("sprites/projectiles/power_fx.png", W, H, [12, 24])
    s.add("boomerang", boomerang(), fps=22, loop=True)
    s.add("beacon", beacon(), fps=10, loop=True)
    s.save("animations/power_fx.json")
