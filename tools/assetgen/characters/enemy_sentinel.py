"""Sentinel: hovering ancient drone, stone + metal shell, single red-amber eye
(32x32, collision 16x14, pivot bottom-centre)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import AMBER, DANGER, MOSS, OUTLINE, SKY, STONE, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W = H = 32
TAU = 2 * math.pi

EYE = Mat([hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1], DANGER[0]], emissive=True, name="sent_eye",
          thresholds=[0.9, 0.75, 0.55, 0.3])
SOCKET = Mat([SKY[2], SKY[1], SKY[0]], name="sent_socket", thresholds=[0.6, 0.3])
FLAME = Mat([AMBER[2], AMBER[1], DANGER[2], AMBER[0]], emissive=True, outline=None, name="sent_flame",
            thresholds=[0.8, 0.6, 0.35])
RUNE = Mat([AMBER[2], AMBER[1], AMBER[0]], emissive=True, name="sent_rune", thresholds=[0.7, 0.4])
RUNE_OFF = Mat([STONE[2], STONE[1]], name="sent_rune_off", thresholds=[0.5])
CRACK = Mat([OUTLINE], name="crack")
HALO = [hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1], DANGER[0]]


def draw(bob=0.0, tilt=0.0, eye=0.0, flame=1.0, flick=0, dead=False, cracks=0, offset=(0.0, 0.0), runes=True):
    cv = TCanvas(W, H, rot=tilt, center=(16.0, 23.0), offset=(offset[0], offset[1] + bob))
    # back thruster pod + flame
    if flame > 0:
        cv.part(line=False)
        fl = flame * (1.0 + 0.25 * (flick % 2))
        cv.tcapsule((8.6, 27.6), (8.6 - 2.0 * fl, 27.6 + 3.2 * fl), 1.3, 0.4, FLAME, flat=0.55)
        cv.tcapsule((8.6, 27.6), (8.6 - 1.2 * fl, 27.6 + 2.0 * fl), 0.7, 0.3, FLAME, flat=0.95)
        cv.tcapsule((16.0, 29.4), (16.0, 29.4 + 2.2 * fl), 1.2, 0.4, FLAME, flat=0.55)
        cv.tcapsule((16.0, 29.4), (16.0, 29.4 + 1.2 * fl), 0.6, 0.3, FLAME, flat=0.95)
    cv.part()
    cv.tcapsule((10.5, 25.2), (8.6, 27.4), 1.7, 1.4, M.METAL, bias=-0.05)
    # crest fin (back-top)
    cv.part()
    cv.tpoly([(10.5, 19.0), (11.0, 13.0), (13.5, 14.0), (16.5, 17.5)], M.STONE_M, bevel=1.0, base=0.55)
    # shell
    cv.part()
    cv.tdisc((16.0, 22.5), 7.3, M.STONE_M, bias=0.02)
    # lower shell darker metal
    cv.tellipse((16.0, 25.6), 6.6, 3.6, M.METAL, decal=True, bias=-0.05)
    # moss on the crown
    cv.tellipse((14.0, 16.3), 3.6, 1.4, M.MOSS_M, decal=True, bias=0.1)
    # equator band with rune lights
    cv.tcapsule((8.4, 23.0), (23.6, 23.0), 1.15, 1.15, M.METAL, decal=True, bias=0.12)
    rm = RUNE if (runes and not dead) else RUNE_OFF
    cv.tline((9.5, 23.5), (12.5, 23.5), rm, inten=0.55 + 0.35 * (flick % 2), decal=True)
    # carved seams on the upper shell
    cv.tline((16.5, 15.5), (15.5, 21.5), CRACK, inten=0.5, decal=True)
    cv.tline((10.5, 20.5), (12.5, 18.5), CRACK, inten=0.5, decal=True)
    # bottom nozzle
    cv.part()
    cv.tcapsule((16.0, 28.6), (16.0, 29.4), 1.6, 1.3, M.METAL, bias=-0.1)
    # eye socket + lens
    cv.part()
    cv.tdisc((20.6, 21.6), 3.6, SOCKET, bias=-0.1)
    cv.part(line=False)
    if not dead:
        cv.tdisc((21.0, 21.4), 2.5 + 0.4 * min(1.0, eye), EYE, bias=-0.15 + 0.35 * eye)
        cv.tpx((21.0, 20.0), EYE, inten=1.0)
    else:
        cv.tdisc((21.0, 21.4), 2.2, SOCKET, flat=0.2)
    # cracks
    if cracks:
        cv.part(line=False)
        cv.tline((13.5, 16.5), (15.5, 19.5), CRACK, decal=True)
        cv.tline((15.5, 19.5), (14.5, 21.5), CRACK, decal=True)
        if cracks > 1:
            cv.tline((18.5, 25.5), (21.5, 27.5), CRACK, decal=True)
            cv.tline((11.5, 24.5), (12.5, 27.5), CRACK, decal=True)
    img = cv.render()
    return img


def halo(img, cx, cy, r, level):
    """Charging glow ring around the eye (drawn over transparent pixels)."""
    pts = FX.ring_pixels(cx, cy, r)
    c = HALO[max(0, 3 - level)]
    for (x, y) in pts:
        if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
            img[y, x] = c
    return img


def clips():
    bob = [0, 0, 1, 1, 1, 0]
    idle = [draw(bob=bob[i], flick=i) for i in range(6)]
    move = [draw(bob=bob[i], tilt=0.18, flame=1.5, flick=i) for i in range(6)]
    tele = []
    rng = np.random.default_rng(5)
    for i in range(6):
        e = (i + 1) / 6.0
        f = draw(eye=e * 1.15, flick=i, offset=((1 if i >= 4 and i % 2 else 0), 0), tilt=-0.06 * e)
        cx, cy = 21.5 + (1 if i >= 4 and i % 2 else 0), 21.9
        if i >= 1:
            halo(f, cx, cy, 4.2 + 0.5 * i, min(3, i // 2 + 1))
        # converging motes
        for k in range(2 + i):
            a = rng.random() * TAU
            d = 9 - i * 1.1 + rng.random() * 2
            x, y = int(cx + math.cos(a) * d), int(cy + math.sin(a) * d)
            if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0:
                f[y, x] = HALO[1 if i > 3 else 2]
        if i == 5:
            FX.sparkle(f, int(cx), int(cy) - 1, 3, HALO[0], HALO[1])
        tele.append(f)
    a0 = draw(eye=1.6, tilt=-0.08)
    FX.sparkle(a0, 25, 21, 4, HALO[0], HALO[1])
    halo(a0, 21.5, 21.9, 5.0, 3)
    attack = [a0, draw(eye=0.5, tilt=-0.2, offset=(-1, 0)), draw(eye=0.2, tilt=-0.08)]
    hurt = [FX.flash(draw(tilt=-0.15, offset=(-1, 0), eye=0.3), amount=0.6), draw(tilt=-0.1, offset=(-1, 0), cracks=1)]
    death = []
    d0 = FX.flash(draw(tilt=-0.2, cracks=1, eye=1.0), amount=0.5)
    d1 = draw(tilt=0.25, cracks=2, eye=0.4, flame=0.6, flick=1)
    for p in ((10, 15), (22, 27), (8, 22)):
        FX.sparkle(d1, p[0], p[1], 1, AMBER[2], AMBER[1])
    d2 = draw(tilt=0.5, cracks=2, dead=True, flame=0.3, offset=(0, 2))
    d3 = draw(tilt=0.8, cracks=2, dead=True, flame=0, offset=(1, 4))
    FX.sparkle(d3, 13, 18, 2, AMBER[2], DANGER[2])
    boom = [hex_rgba('#fff4d6'), AMBER[2], AMBER[1], DANGER[2], DANGER[1], DANGER[0]]
    d4 = FX.new(W, H)
    FX.burst(d4, 16, 23, 9, boom, rays=5, seed=2)
    d5 = FX.new(W, H)
    FX.burst(d5, 16, 22, 10, boom[2:] + [SKY[3]], seed=4, jag=0.35)
    d6 = FX.new(W, H)
    d7 = FX.new(W, H)
    rng = np.random.default_rng(9)
    pieces = [(rng.random() * 8 - 4, -rng.random() * 3 - 1, int(rng.integers(1, 4))) for _ in range(8)]
    for f, t in ((d5, 0.5), (d6, 1.0), (d7, 1.6)):
        for vx, vy, sz in pieces:
            x = 16 + vx * t * 2.2
            y = 22 + vy * t * 3 + 4.5 * t * t
            if y < 31:
                FX.chunk(f, x, y, sz, STONE[3], STONE[1])
    for (x, y) in ((14, 18), (18, 16), (16, 13)):
        put(d6, x, y, SKY[4])
    put(d7, 17, 12, SKY[3])
    death = [d0, d1, d2, d3, d4, d5, d6, d7]
    return dict(idle=idle, move=move, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/sentinel.png", W, H, [16, 32])
    s.add("idle", c["idle"], fps=8, loop=True)
    s.add("move", c["move"], fps=10, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.1, 0.1, 0.1, 0.1, 0.1, 0.12], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.1, 0.12], loop=False, events=[(0, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.1, 0.08, 0.1, 0.1, 0.12], loop=False)
    s.save("animations/sentinel.json")
