"""Bomber: hovering ruin drone with a bomb bay underneath, amber lights and two
downward thrusters (32x24, collision 18x12, pivot bottom-centre of the box =
[16, 20]; the thruster flames and the dropped bomb hang below the box)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import AMBER, DANGER, OUTLINE, SKY, STONE, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W, H = 32, 24
TAU = 2 * math.pi

FLAME = Mat([AMBER[2], AMBER[1], DANGER[2], AMBER[0]], emissive=True, outline=None, name="bomber_flame",
            thresholds=[0.8, 0.6, 0.35])
LIGHT_ON = Mat([AMBER[2], AMBER[1], AMBER[0]], emissive=True, name="bomber_light", thresholds=[0.8, 0.5])
LIGHT_OFF = Mat([hex_rgba('#7a4a1c'), hex_rgba('#4a2c14')], name="bomber_light_off", thresholds=[0.5])
WARN_ON = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="bomber_warn",
              thresholds=[0.8, 0.45])
WARN_OFF = Mat([DANGER[0], hex_rgba('#5a0a24')], name="bomber_warn_off", thresholds=[0.5])
EYE = Mat([hex_rgba('#fff4d6'), DANGER[2], DANGER[1], DANGER[0]], emissive=True, name="bomber_eye",
          thresholds=[0.85, 0.6, 0.35])
SOCKET = Mat([SKY[2], SKY[1], SKY[0]], name="bomber_socket", thresholds=[0.6, 0.3])
BAY = Mat([SKY[1], SKY[0]], name="bomber_bay", thresholds=[0.5])
BOMB = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="bomber_bomb", thresholds=[0.8, 0.56, 0.32])
BOMB_BAND = Mat([AMBER[1], AMBER[0]], name="bomber_bomb_band", thresholds=[0.5])
CRACK = Mat([OUTLINE], name="bomber_crack")
BOOM = [hex_rgba('#fff4d6'), AMBER[2], AMBER[1], DANGER[2], DANGER[1], DANGER[0]]
SMOKE = [STONE[3], STONE[2], STONE[1]]


def draw(bob=0.0, tilt=0.0, flame=1.0, flick=0, chase=0, warn=False, bay=0.0, bomb=True, bomb_y=0.0,
         fuse=True, eye=0.5, dead=False, cracks=0, offset=(0.0, 0.0)):
    """bay = door opening 0..1, bomb_y = bomb drop distance below the bay."""
    cv = TCanvas(W, H, rot=tilt, center=(16.0, 14.0), offset=(offset[0], offset[1] + bob))
    # thruster flames (behind everything)
    if flame > 0:
        cv.part(line=False)
        fl = flame * (1.0 + 0.3 * (flick % 2))
        for x in (6.8, 25.2):
            cv.tcapsule((x, 17.8), (x - 0.8 * fl, 17.8 + 2.4 * fl), 1.5, 0.3, FLAME, flat=0.55)
            cv.tcapsule((x, 17.8), (x - 0.4 * fl, 17.8 + 1.3 * fl), 0.8, 0.3, FLAME, flat=0.95)
    # bomb (inside the bay / falling)
    def the_bomb(by, decal=False):
        cv.tdisc((16.0, by), 2.5, BOMB, bias=0.05, decal=decal)
        cv.tline((14.0, by + 0.4), (18.0, by + 0.4), BOMB_BAND, inten=0.8, decal=True)
        cv.tpx((15.0, by - 1.2), BOMB, inten=1.0, decal=True)
        cv.tpx((16.5, by - 1.2), WARN_ON if fuse else WARN_OFF, inten=0.95 if fuse else 0.6, decal=True)

    if bomb and bomb_y >= 1.0:
        cv.part()
        the_bomb(17.4 + bomb_y)
    # thruster pods
    for x in (6.8, 25.2):
        cv.part()
        cv.tellipse((x, 15.0), 2.6, 2.4, M.METAL, bias=0.0)
        cv.tcapsule((x - 1.6, 17.4), (x + 1.6, 17.4), 0.8, 0.8, M.STONE_DARK, bias=-0.05)
        cv.tpx((x - 0.8, 14.2), M.METAL, inten=1.0, decal=True)
    # main hull (ancient stone dome over a metal band)
    cv.part()
    cv.tellipse((16.0, 12.2), 9.0, 5.0, M.STONE_M, bias=0.02)
    cv.tcapsule((7.6, 14.2), (24.4, 14.2), 1.4, 1.4, M.METAL, decal=True, bias=0.05)
    cv.tellipse((13.5, 7.9), 4.4, 1.5, M.MOSS_M, decal=True, bias=0.1)
    # carved seams
    cv.tline((18.5, 7.4), (17.5, 11.4), CRACK, decal=True)
    cv.tline((10.5, 10.4), (12.5, 12.4), CRACK, decal=True)
    # amber running lights on the band (chase)
    for k, x in enumerate((9.5, 12.5, 15.5, 18.5)):
        on = (not dead) and (k == chase % 4 or warn)
        cv.tpx((x, 14.2), LIGHT_ON if on else LIGHT_OFF, inten=0.9 if on else 0.6, decal=True)
    # bay housing under the hull, dark inside with the bomb when open
    cv.part()
    cv.tpoly([(10.4, 15.6), (21.6, 15.6), (20.6, 19.2), (11.4, 19.2)], M.METAL, bevel=1.0, base=0.5)
    if bay > 0.05:
        cv.tpoly([(11.8, 16.4), (20.2, 16.4), (19.6, 19.2), (12.4, 19.2)], BAY, flat=0.3, decal=True)
        if bomb and bomb_y < 1.0:
            the_bomb(18.0 + bomb_y, decal=True)
    else:
        cv.tline((12.0, 18.4), (20.0, 18.4), M.STONE_DARK, inten=0.3, decal=True)
        cv.tline((16.0, 16.6), (16.0, 18.4), M.STONE_DARK, inten=0.3, decal=True)
    # bay doors, hinged at the outer edges, swing down
    if bay > 0.05:
        cv.part()
        da = bay * 1.0
        for hx, sgn in ((11.6, 1), (20.4, -1)):
            ex = hx + sgn * math.cos(da) * 4.2
            ey = 19.2 + math.sin(da) * 3.4
            cv.tcapsule((hx, 19.2), (ex, ey), 0.9, 0.7, M.METAL, bias=0.05)
    # front sensor eye
    cv.part()
    cv.tellipse((23.0, 11.6), 2.4, 2.2, SOCKET, bias=-0.1)
    cv.part(line=False)
    if not dead:
        cv.tellipse((23.6, 11.6), 1.4 + 0.3 * min(1.0, eye), 1.3, EYE, flat=0.5 + 0.35 * min(1.0, eye))
        cv.tpx((23.6, 10.8), EYE, inten=1.0)
    # antenna + warning light
    cv.part()
    cv.tline((14.5, 7.4), (14.5, 4.6), M.METAL, inten=0.7)
    cv.part(line=False)
    cv.tdisc((14.5, 3.8), 1.1, WARN_ON if (warn and not dead) else WARN_OFF, flat=0.7)
    if cracks:
        cv.part(line=False)
        cv.tline((15.5, 8.5), (17.5, 11.5), CRACK, decal=True)
        cv.tline((17.5, 11.5), (16.5, 13.5), CRACK, decal=True)
        if cracks > 1:
            cv.tline((9.5, 12.5), (11.5, 15.5), CRACK, decal=True)
            cv.tline((20.5, 9.5), (22.5, 8.5), CRACK, decal=True)
    return cv.render()


def warn_ring(img, r, c):
    for (x, y) in FX.ring_pixels(14.5, 3.8, r):
        if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
            img[y, x] = c
    return img


def clips():
    bob = [0, 0, 1, 1, 1, 0]
    idle = [draw(bob=bob[i], flick=i, chase=i % 4, warn=i < 2) for i in range(6)]
    move = [draw(bob=bob[i], tilt=0.16, flame=1.4, flick=i, chase=(i * 2) % 4, warn=i < 2) for i in range(6)]
    tele = []
    for i in range(4):
        t = (i + 1) / 4.0
        f = draw(bob=0, bay=t, warn=i % 2 == 0, fuse=i % 2 == 0, chase=i, eye=0.5 + 0.5 * t, flick=i,
                 offset=(0, -1 if i >= 2 else 0))
        if i % 2 == 0:
            warn_ring(f, 2.6 + 0.4 * i, DANGER[1])
        tele.append(f)
    FX.sparkle(tele[3], 14, 2, 2, hex_rgba('#ffe0e6'), DANGER[1])
    a0 = draw(bay=1.0, bomb_y=1.5, warn=True, eye=1.0, flick=0)
    a1 = draw(bay=1.0, bomb_y=4.5, warn=True, eye=1.0, flick=1, offset=(0, -1), flame=1.4)
    warn_ring(a1, 3.0, DANGER[1])
    a2 = draw(bay=1.0, bomb=False, eye=0.8, flick=0, offset=(0, -1))
    a3 = draw(bay=0.45, bomb=False, eye=0.6, flick=1)
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(tilt=-0.18, offset=(-1, 0), eye=0.3), amount=0.6),
            draw(tilt=-0.1, offset=(-1, 0), cracks=1, flick=1)]
    d0 = FX.flash(draw(tilt=-0.2, cracks=1, eye=1.0, warn=True), amount=0.5)
    d1 = draw(tilt=0.25, cracks=2, eye=0.3, flame=0.6, flick=1, warn=True, bay=0.6)
    for p in ((8, 7), (24, 16), (20, 5)):
        FX.sparkle(d1, p[0], p[1], 1, AMBER[2], AMBER[1])
    d2 = draw(tilt=0.5, cracks=2, dead=True, flame=0.3, bay=0.9, bomb=False, offset=(0, 1))
    FX.burst(d2, 9, 6, 2.0, SMOKE, seed=3, jag=0.4)
    d3 = draw(tilt=0.8, cracks=2, dead=True, flame=0, bay=1.0, bomb=False, offset=(1, 2))
    FX.burst(d3, 8, 4, 2.6, SMOKE, seed=4, jag=0.4)
    FX.sparkle(d3, 17, 10, 2, AMBER[2], DANGER[2])
    d4 = FX.new(W, H)
    FX.burst(d4, 16, 13, 8, BOOM, rays=5, seed=2)
    d5 = FX.new(W, H)
    FX.burst(d5, 16, 12, 9.5, BOOM[2:] + [SKY[3]], seed=4, jag=0.35)
    d6 = FX.new(W, H)
    d7 = FX.new(W, H)
    rng = np.random.default_rng(11)
    pieces = [(rng.random() * 9 - 4.5, -rng.random() * 3 - 1, int(rng.integers(1, 4))) for _ in range(9)]
    for f, t in ((d5, 0.5), (d6, 1.0), (d7, 1.5)):
        for vx, vy, sz in pieces:
            x = 16 + vx * t * 2.2
            y = 13 + vy * t * 3 + 4.5 * t * t
            if y < 23:
                FX.chunk(f, x, y, sz, STONE[3], STONE[1])
    for (x, y) in ((14, 9), (18, 7), (16, 5)):
        put(d6, x, y, SMOKE[0])
    put(d7, 17, 4, SMOKE[1])
    put(d7, 13, 6, SMOKE[2])
    death = [d0, d1, d2, d3, d4, d5, d6, d7]
    return dict(idle=idle, move=move, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/bomber.png", W, H, [16, 20])
    s.add("idle", c["idle"], fps=8, loop=True)
    s.add("move", c["move"], fps=10, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.12, 0.16], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.1, 0.12, 0.12], loop=False, events=[(1, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.1, 0.08, 0.1, 0.1, 0.12], loop=False)
    s.save("animations/bomber.json")
