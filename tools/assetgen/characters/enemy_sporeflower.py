"""Sporeflower: carnivorous plant turret rooted in the ground, violet bulb that
inflates and spits spores (32x32, collision 16x20, pivot bottom-centre)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import DANGER, MOSS, STONE, SUIT, VIOLET, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W = H = 32
TAU = 2 * math.pi

BULB = Mat([VIOLET[2], VIOLET[1], VIOLET[0], hex_rgba('#3b1a6e')], name="bulb", thresholds=[0.84, 0.6, 0.34])
BULB_HOT = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], name="bulb_hot", thresholds=[0.84, 0.6, 0.34])
BULB_DEAD = Mat([STONE[3], STONE[2], hex_rgba('#3b1a6e'), STONE[0]], name="bulb_dead", thresholds=[0.84, 0.6, 0.34])
SPOT = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1]], emissive=True, name="spot", thresholds=[0.75, 0.4])
MOUTH = Mat([hex_rgba('#2a0f4a'), hex_rgba('#160829')], name="mouth", thresholds=[0.5])
TOOTH = Mat([SUIT[0], SUIT[2]], name="tooth", thresholds=[0.5])
THROAT = Mat([DANGER[2], DANGER[1], DANGER[0]], emissive=True, name="throat", thresholds=[0.75, 0.4])
LEAF = M.MOSS_M
LEAF_DEAD = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="leaf_dead", thresholds=[0.8, 0.56, 0.34])
SPORE = [hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]]


def stem_points(lean, bend, h=11.0, n=6, base=(16.0, 30.0)):
    pts = [base]
    a = -math.pi / 2 + lean
    x, y = base
    for i in range(n):
        a += bend / n
        x += math.cos(a) * h / n
        y += math.sin(a) * h / n
        pts.append((x, y))
    return pts, a


def draw(lean=0.0, bend=0.0, inflate=1.0, mouth=0.0, glow=0.0, hot=False, dead=0.0, push=0.0, leaf_wave=0.0,
         h=11.0):
    cv = TCanvas(W, H)
    leaf = LEAF if dead < 0.5 else LEAF_DEAD
    bulb = BULB_DEAD if dead >= 0.5 else (BULB_HOT if hot else BULB)
    # back leaf + roots
    cv.part()
    cv.ellipse((19.5, 28.6 - leaf_wave * 0.5), 4.6, 1.7, leaf, ang=-0.55, bias=-0.25)
    cv.part(line=False)
    for x0, x1 in ((15.5, 12.5), (16.5, 19.5), (16.0, 16.0)):
        cv.line((x0, 30.5), (x1, 31.5), M.STONE_DARK, inten=0.3)
    # stem
    pts, a_end = stem_points(lean, bend, h=h)
    cv.part()
    n = len(pts)
    cv.chain(pts, [1.9 - 0.5 * i / (n - 1) for i in range(n)], leaf, bias=0.0)
    # small stem leaf
    mid = pts[3]
    cv.part()
    cv.ellipse((mid[0] - 2.4, mid[1] + 0.2 + leaf_wave * 0.4), 2.4, 1.0, leaf, ang=0.5 + leaf_wave * 0.2, bias=0.05)
    # thorns
    cv.part(line=False)
    for i in (2, 4):
        put_px = pts[i]
        cv.px_set(put_px[0] + 1.8, put_px[1] - 0.5, M.STONE_M, inten=0.9)
    # bulb
    tip = pts[-1]
    da = a_end + math.pi / 2  # tilt of the bulb follows the stem
    cx = tip[0] + math.cos(a_end) * 4.0 * inflate + push
    cy = tip[1] + math.sin(a_end) * 4.0 * inflate
    rx, ry = 6.0 * inflate, 5.4 * inflate
    cv.part()
    # crown spikes
    for k, (dx, dy, ln) in enumerate(((-2.5, -0.6, 3.2), (0.2, -1.0, 3.6), (2.6, -0.5, 2.6))):
        ang = da - math.pi / 2 + dx * 0.18
        bx = cx + math.cos(da - math.pi / 2) * ry * 0.85 + math.cos(da) * dx
        by = cy + math.sin(da - math.pi / 2) * ry * 0.85 + math.sin(da) * dx
        cv.capsule((bx, by), (bx + math.cos(ang) * ln, by + math.sin(ang) * ln), 0.9, 0.35, bulb, bias=-0.2)
    cv.part()
    cv.ellipse((cx, cy), rx, ry, bulb, ang=da, bias=glow)
    # glowing spots
    for (sx, sy) in ((-2.6, -2.2), (-3.8, 0.8), (-1.0, 2.6), (0.6, -3.4)):
        px = cx + math.cos(da) * sx * inflate - math.sin(da) * sy * inflate
        py = cy + math.sin(da) * sx * inflate + math.cos(da) * sy * inflate
        cv.px_set(px, py, SPOT, inten=0.5 + 0.4 * min(1.0, glow * 3 + (0.5 if hot else 0)), decal=True)
    # toothy maw on the front (right): a wedge cut into the bulb, red throat inside
    cv.part(line=False)
    ax0, ay0 = cx - 1.8, cy + 0.6          # hinge (deep inside the bulb)
    fx = cx + rx + 0.8                     # lip line beyond the front edge
    half = 0.7 + 3.6 * mouth
    if mouth > 0.15:
        wedge = [(ax0, ay0), (fx, ay0 - half), (fx + 0.5, ay0), (fx, ay0 + half)]
        cv.poly(wedge, MOUTH, flat=0.3)
        cv.ellipse((ax0 + (fx - ax0) * 0.45, ay0), (fx - ax0) * 0.35, half * 0.45, THROAT, flat=0.6, decal=True)
        n = 3
        for k in range(n):
            t = 0.35 + 0.6 * k / (n - 1)
            x = ax0 + (fx - ax0) * t
            cv.px_set(x, ay0 - half * t + 0.55, TOOTH, inten=0.9, decal=True)
            cv.px_set(x + 0.8, ay0 + half * t - 0.5, TOOTH, inten=0.5, decal=True)
    else:
        x = ax0
        k = 0
        while x < fx - 0.5:
            cv.px_set(x, ay0 + (0 if k % 2 == 0 else -0.0), MOUTH, inten=0.3, decal=True)
            if k % 2 == 1 and x > cx - 0.5:
                cv.px_set(x, ay0 - 1, TOOTH, inten=0.9, decal=True)
            if k % 2 == 0 and x > cx:
                cv.px_set(x, ay0 + 1, TOOTH, inten=0.4, decal=True)
            x += 1.0
            k += 1
    # front leaves
    cv.part()
    cv.ellipse((11.2, 29.6 - leaf_wave * 0.5), 5.2, 1.9, leaf, ang=0.42 + leaf_wave * 0.08, bias=0.05)
    cv.part()
    cv.ellipse((21.6, 30.0), 4.8, 1.7, leaf, ang=-0.3, bias=0.0)
    return cv.render()


def puff(img, x, y, r, seed=0):
    FX.burst(img, x, y, r, SPORE[:3], seed=seed, jag=0.3)
    return img


def clips():
    idle = []
    for i in range(6):
        ph = TAU * i / 6
        idle.append(draw(lean=0.08 * math.sin(ph), bend=0.25 * math.sin(ph + 0.6), inflate=1.0 + 0.03 * math.sin(ph),
                         glow=0.05 + 0.05 * math.sin(ph), leaf_wave=math.sin(ph)))
    tele = []
    for i in range(5):
        t = (i + 1) / 5.0
        tele.append(draw(lean=-0.05 * t, bend=-0.15 * t, inflate=1.0 + 0.32 * t, glow=0.02 + 0.08 * t,
                         hot=i >= 2, push=-0.6 * t))
    a0 = draw(lean=-0.12, bend=-0.2, inflate=0.9, glow=0.1, push=-1.0, hot=True)
    a1 = draw(lean=0.18, bend=0.3, inflate=1.05, mouth=1.0, glow=0.1, push=1.0)
    puff(a1, 27, 13, 2.6, seed=1)
    a2 = draw(lean=0.1, bend=0.2, inflate=1.0, mouth=0.6, glow=0.05, push=0.6)
    for (x, y) in ((28, 11), (29, 15), (27, 9)):
        put(a2, x, y, SPORE[1])
    a3 = draw(lean=0.02, bend=0.05, inflate=1.0, mouth=0.1)
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(lean=-0.15, bend=-0.3, mouth=0.4), amount=0.6), draw(lean=-0.1, bend=-0.2, mouth=0.3)]
    death = [
        FX.flash(draw(lean=-0.2, bend=-0.3, mouth=0.8), amount=0.5),
        draw(lean=-0.15, bend=0.9, inflate=0.95, mouth=0.6, glow=-0.05, h=10.5),
        draw(lean=-0.3, bend=1.6, inflate=0.85, mouth=0.4, glow=-0.15, dead=0.3, h=9.0),
        draw(lean=-0.45, bend=2.3, inflate=0.75, mouth=0.2, dead=0.6, h=7.5),
        draw(lean=-0.5, bend=2.6, inflate=0.65, mouth=0.1, dead=0.8, h=6.5),
    ]
    death.append(FX.dissolve(death[-1], 0.5, seed=4, colors=SPORE, rise=10, drift=3))
    death.append(FX.dissolve(death[-2], 0.98, seed=4, colors=SPORE, rise=10, drift=3))
    return dict(idle=idle, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/sporeflower.png", W, H, [16, 32])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.12, 0.12, 0.16], loop=False)
    s.add("attack", c["attack"], durations=[0.06, 0.1, 0.12, 0.14], loop=False, events=[(1, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.12, 0.12, 0.14, 0.16, 0.12, 0.14], loop=False)
    s.save("animations/sporeflower.json")
