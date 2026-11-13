"""Glowmite: small bioluminescent beetle (24x16, collision 14x10)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import CYAN, DANGER, MOSS, STONE, hex_rgba
from raster import Mat, TCanvas
from sheet import Sheet

W, H = 24, 16
TAU = 2 * math.pi

SHELL = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], name="mite_shell", thresholds=[0.96, 0.76, 0.5])
SHELL_HOT = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], name="mite_shell_hot", thresholds=[0.86, 0.62, 0.4])
BODY = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="mite_body", thresholds=[0.8, 0.56, 0.32])
LEG = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="mite_leg", thresholds=[0.8, 0.56, 0.32])
EYE = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="mite_eye", thresholds=[0.85, 0.4])
PLATE = Mat([MOSS[1], MOSS[0]], name="mite_plate", thresholds=[0.5])

ATTACH = [(7.5, 12.0), (11.0, 12.5), (14.5, 12.0)]
FOOT = [(4.5, 15.5), (11.0, 15.5), (17.5, 15.5)]


def draw(walk=None, bob=0.0, tilt=0.0, lunge=0.0, mand=0.0, eye=0.0, glow=0.0, offset=(0, 0),
         ant=0.0, curl=0.0, flip=False, hot=False):
    cv = TCanvas(W, H, rot=tilt, center=(12.0, 11.0), offset=(lunge + offset[0], bob + offset[1]),
                 scale=(1.0, -1.0 if flip else 1.0))

    def legs(far):
        for i in range(3):
            ax, ay = ATTACH[i]
            fx, fy = FOOT[i]
            if far:
                ax += 1.0
                fx += 1.5
            if walk is not None:
                ph = walk + (0.5 if (i % 2 == 0) != far else 0.0)
                fx += 1.6 * math.cos(TAU * ph)
                fy -= max(0.0, math.sin(TAU * ph)) * 1.6
            if curl:
                # dead: legs curl in and twitch
                fx = ax + (i - 1) * 1.5 + 0.8 * math.sin(i * 2.1 + curl * 2.0 + (1.0 if far else 0.0))
                fy = ay + 2.0 + 0.6 * math.cos(i * 1.7 + curl * 3.0)
            kx = (ax + fx) / 2 + (0.6 if i == 2 else -0.6 if i == 0 else 0)
            ky = min(ay, fy) - (1.4 if not curl else 0.0)
            cv.part(line=False)
            fl = 0.25 if far else 0.62
            cv.tcapsule((ax, ay), (kx, ky), 0.62, 0.62, LEG, flat=fl)
            cv.tcapsule((kx, ky), (fx, fy), 0.62, 0.55, LEG, flat=fl - 0.12)

    legs(True)
    # far antenna
    cv.part(line=False)
    cv.tline((18.5, 8.5), (20.5 + ant * 0.5, 4.5), LEG, inten=0.25)
    # carapace (glowing back), the underbody hides its lower half
    cv.part()
    shell = SHELL_HOT if hot else SHELL
    cv.tellipse((10.5, 9.6), 6.9, 4.7, shell, bias=glow)
    cv.tcapsule((9.5, 5.4), (9.0, 11.0), 0.45, 0.45, PLATE, flat=0.6, decal=True)
    # underbody + head
    cv.part()
    cv.tellipse((11.5, 12.0), 7.0, 2.4, BODY, bias=-0.05)
    cv.part()
    cv.tdisc((18.3, 10.8), 2.7, BODY, bias=0.08)
    # mandibles
    cv.part(line=False)
    o = mand
    cv.tcapsule((20.4, 12.0), (22.4 - o * 0.3, 12.6 + o * 1.2), 0.6, 0.5, LEG, flat=0.9)
    cv.tcapsule((20.4, 11.0), (22.5 - o * 0.3, 10.8 - o * 1.3), 0.6, 0.5, LEG, flat=0.9)
    # near antenna with a glowing tip
    cv.tline((19.5, 8.5), (21.5 + ant, 5.0 - ant * 0.5), LEG, inten=0.85)
    cv.tpx((21.5 + ant, 4.0 - ant * 0.5), shell, inten=0.9)
    # eye (bright red, bigger when charging)
    cv.part(line=False)
    er = 1.05 + 0.45 * eye
    cv.tdisc((19.8, 9.9), er, EYE, flat=0.6 + 0.3 * min(1.0, eye))
    legs(False)
    return cv.render()


def clips():
    idle = [draw(glow=g, ant=a, bob=b) for g, a, b in
            zip([0.0, 0.06, 0.12, 0.06], [0, 0.5, 1.0, 0.5], [0, 0, 0, 0])]
    walk = [draw(walk=i / 6, bob=(1 if i in (1, 4) else 0), ant=0.5 * math.sin(TAU * i / 6)) for i in range(6)]
    telegraph = [
        draw(tilt=-0.12, mand=0.4, eye=0.5, glow=0.08, offset=(-1, 0)),
        draw(tilt=-0.22, mand=0.8, eye=1.0, glow=0.14, offset=(-1, 0), hot=True),
        draw(tilt=-0.3, mand=1.0, eye=1.5, glow=0.2, offset=(-2, 0), hot=True),
    ]
    FX.sparkle(telegraph[2], 20, 8, 2, hex_rgba('#ffe0e6'), DANGER[1])
    attack = [
        draw(tilt=-0.1, lunge=-1, mand=1.0, eye=1.0),
        draw(tilt=0.08, lunge=1, mand=0.9, eye=0.8),
        draw(tilt=0.15, lunge=1, mand=0.0, eye=0.6),
        draw(tilt=0.05, lunge=0, mand=0.3, eye=0.3),
    ]
    hurt = [FX.flash(draw(offset=(-1, 0), tilt=-0.1), amount=0.6), draw(offset=(-1, 0), tilt=-0.05, mand=0.5)]
    d0 = FX.flash(draw(tilt=-0.35, offset=(0, -1)), amount=0.5)
    d1 = draw(tilt=-1.2, offset=(-1, -1), curl=0.5)
    d2 = draw(flip=True, curl=1.0, glow=-0.1, offset=(0, -6))
    d3 = draw(flip=True, curl=1.6, glow=-0.3, offset=(0, -6))
    death = [d0, d1, d2, d3, FX.dissolve(d3, 0.45, seed=3, rise=8, drift=2), FX.dissolve(d3, 0.95, seed=3, rise=8, drift=2)]
    return dict(idle=idle, walk=walk, telegraph=telegraph, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/glowmite.png", W, H, [12, 16])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("walk", c["walk"], fps=12, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.16], loop=False)
    s.add("attack", c["attack"], durations=[0.06, 0.06, 0.1, 0.12], loop=False, events=[(2, "hit")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.12, 0.14, 0.1, 0.12], loop=False)
    s.save("animations/glowmite.json")
