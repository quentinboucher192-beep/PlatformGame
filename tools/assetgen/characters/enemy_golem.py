"""Golem: heavy ruin golem, stone body with an amber core and big arms, the
front arm carries a stone shield slab (48x48, collision 26x34)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import AMBER, MOSS, OUTLINE, STONE, hex_rgba
from raster import Mat, TCanvas, ik2
from sheet import Sheet

W = H = 48
TAU = 2 * math.pi

ROCK = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="golem_rock", thresholds=[0.84, 0.6, 0.36])
ROCK_BACK = Mat([STONE[2], STONE[1], STONE[0]], name="golem_rock_back", thresholds=[0.6, 0.35])
SLAB = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="golem_slab", thresholds=[0.8, 0.56, 0.34])
CORE = Mat([hex_rgba('#fff4d6'), AMBER[2], AMBER[1], AMBER[0]], emissive=True, name="golem_core",
           thresholds=[0.86, 0.66, 0.4])
CORE_DEAD = Mat([STONE[2], STONE[1], STONE[0]], name="golem_core_dead", thresholds=[0.6, 0.3])
SOCKET = Mat([STONE[1], STONE[0]], name="golem_socket", thresholds=[0.5])
RUNE = Mat([AMBER[1], AMBER[0]], emissive=True, name="golem_rune", thresholds=[0.5])
CRACK = Mat([OUTLINE], name="golem_crack")
GLOWMOSS = Mat([hex_rgba('#8ffff0'), hex_rgba('#39f0d8')], emissive=True, name="golem_glowmoss", thresholds=[0.5])


class G:
    def __init__(self, **kw):
        self.body = (0.0, 0.0)        # torso offset
        self.lean = 0.0
        self.fist_f = (37.0, 39.0)
        self.fist_b = (14.0, 38.0)
        self.foot_f = (29.0, 47.0)
        self.foot_b = (19.0, 47.0)
        self.core = 0.5               # glow 0..1+
        self.eye = 0.5
        self.cracks = 0
        self.shield = 0.0             # 0 = slab along the forearm, 1 = raised as a wall
        self.dead_core = False
        self.rot = 0.0
        self.__dict__.update(kw)


def draw(g):
    cv = TCanvas(W, H, rot=g.rot, center=(24.0, 47.0))
    bx, by = g.body
    lean = g.lean

    def B(x, y):
        # torso-space point: rotate around the hips by lean, then offset
        cx, cy = 24.0, 38.0
        dx, dy = x - cx, y - cy
        c, s = math.cos(lean), math.sin(lean)
        return (cx + dx * c - dy * s + bx, cy + dx * s + dy * c + by)

    hip_f, hip_b = B(28.5, 37.5), B(19.5, 37.5)
    sh_f, sh_b = B(31.5, 22.5), B(16.0, 21.0)

    # back arm
    cv.part()
    el_b = ik2(sh_b, g.fist_b, 8.0, 8.0, -1.0)
    cv.tcapsule(sh_b, el_b, 3.4, 3.0, ROCK_BACK)
    cv.tcapsule(el_b, g.fist_b, 3.0, 3.4, ROCK_BACK)
    cv.tdisc(g.fist_b, 3.8, ROCK_BACK)
    # back leg
    cv.part()
    kn_b = ik2(hip_b, (g.foot_b[0], g.foot_b[1] - 2.5), 4.5, 4.5, 1.0)
    cv.tcapsule(hip_b, kn_b, 3.6, 3.4, ROCK_BACK)
    cv.tcapsule(kn_b, (g.foot_b[0], g.foot_b[1] - 2.5), 3.4, 3.2, ROCK_BACK)
    cv.tpoly([(g.foot_b[0] - 4, g.foot_b[1] - 4), (g.foot_b[0] + 4, g.foot_b[1] - 4),
              (g.foot_b[0] + 5, g.foot_b[1]), (g.foot_b[0] - 4, g.foot_b[1])], ROCK_BACK, flat=0.4)
    # torso
    cv.part()
    torso = [B(12.5, 23.0), B(17.0, 16.5), B(30.0, 15.5), B(35.5, 19.5), B(35.5, 31.0), B(31.0, 38.5),
             B(18.0, 39.5), B(13.5, 34.0)]
    cv.tpoly(torso, ROCK, bevel=2.2, base=0.62)
    # moss on the shoulders/back
    cv.tellipse(B(19.0, 17.8), 6.0, 2.2, M.MOSS_M, ang=-0.15, decal=True, bias=0.05)
    cv.tellipse(B(14.5, 24.0), 2.0, 4.0, M.MOSS_M, decal=True, bias=-0.1)
    for p in ((16.5, 17.5), (21.5, 16.5), (14.5, 22.5)):
        cv.tpx(B(*p), GLOWMOSS, inten=0.8, decal=True)
    # carved runes
    cv.tline(B(16.5, 28.5), B(16.5, 33.5), RUNE, inten=0.5 if not g.dead_core else 0.0, decal=True)
    cv.tline(B(16.5, 31.0), B(18.5, 31.0), RUNE, inten=0.5 if not g.dead_core else 0.0, decal=True)
    # core housing + crystal
    cv.part(line=False)
    cc = B(25.5, 27.0)
    cv.tdisc(cc, 4.4, SOCKET, flat=0.4)
    cm = CORE_DEAD if g.dead_core else CORE
    r = 2.9 + 0.5 * min(1.5, g.core)
    cv.tpoly([(cc[0], cc[1] - r), (cc[0] + r * 0.8, cc[1]), (cc[0], cc[1] + r), (cc[0] - r * 0.8, cc[1])], cm,
             shade="grad", base=0.45 + 0.35 * g.core)
    if not g.dead_core and g.core > 0.3:
        cv.tpx((cc[0] - 1.0, cc[1] - 1.0), cm, inten=1.0)
    # head
    cv.part()
    hc = B(27.0, 12.5)
    cv.tpoly([(hc[0] - 5, hc[1] - 2), (hc[0] - 2, hc[1] - 5), (hc[0] + 4, hc[1] - 4.5), (hc[0] + 5.5, hc[1] - 1),
              (hc[0] + 4.5, hc[1] + 3.5), (hc[0] - 4, hc[1] + 3.5)], ROCK, bevel=1.6, base=0.6)
    cv.tellipse((hc[0] - 2.5, hc[1] - 4.2), 3.0, 1.2, M.MOSS_M, decal=True)
    cv.tcapsule((hc[0] - 0.5, hc[1] - 1.6), (hc[0] + 5.2, hc[1] - 1.6), 0.8, 0.8, ROCK, decal=True, flat=0.92)
    # eye slit
    cv.part(line=False)
    cv.tcapsule((hc[0] + 0.5, hc[1] + 0.2), (hc[0] + 5.0, hc[1] + 0.2), 1.1 + 0.3 * g.eye, 1.0, SOCKET, flat=0.2)
    if not g.dead_core:
        cv.tcapsule((hc[0] + 2.0, hc[1] + 0.2), (hc[0] + 4.6, hc[1] + 0.2), 0.6 + 0.35 * g.eye, 0.55, CORE,
                    flat=0.55 + 0.35 * g.eye)
    # front leg
    cv.part()
    kn_f = ik2(hip_f, (g.foot_f[0], g.foot_f[1] - 2.5), 4.5, 4.5, 1.0)
    cv.tcapsule(hip_f, kn_f, 3.8, 3.5, ROCK)
    cv.tcapsule(kn_f, (g.foot_f[0], g.foot_f[1] - 2.5), 3.5, 3.3, ROCK)
    cv.tpoly([(g.foot_f[0] - 4, g.foot_f[1] - 4.5), (g.foot_f[0] + 4, g.foot_f[1] - 4.5),
              (g.foot_f[0] + 5.5, g.foot_f[1]), (g.foot_f[0] - 4, g.foot_f[1])], ROCK, bevel=1.2, base=0.55)
    # front arm (shield arm)
    cv.part()
    el_f = ik2(sh_f, g.fist_f, 8.0, 8.0, -1.0 if g.shield < 0.5 else 1.0)
    cv.tdisc(sh_f, 4.9, ROCK, bias=0.04)              # pauldron
    cv.tellipse((sh_f[0] - 1.0, sh_f[1] - 3.6), 3.4, 1.4, M.MOSS_M, decal=True, bias=0.1)
    cv.part()
    cv.tcapsule(sh_f, el_f, 3.6, 3.2, ROCK)
    cv.tcapsule(el_f, g.fist_f, 3.2, 3.6, ROCK)
    cv.part()
    cv.tdisc(g.fist_f, 4.2, ROCK, bias=0.02)
    # shield slab strapped to the forearm
    fx, fy = g.fist_f
    ex, ey = el_f
    ux, uy = fx - ex, fy - ey
    L = math.hypot(ux, uy) or 1.0
    ux, uy = ux / L, uy / L
    if g.shield > 0:
        # slab standing up in front of the body
        ang = math.atan2(uy, ux) * (1 - g.shield) + (-math.pi / 2) * g.shield
    else:
        ang = math.atan2(uy, ux)
    sx, sy = math.cos(ang), math.sin(ang)
    nx, ny = -sy, sx
    mx, my = (ex + fx) / 2 + 2.6 * (1 if g.shield < 0.5 else 0.6), (ey + fy) / 2
    if g.shield > 0:
        mx, my = fx + 3.0, fy - 4.0 * g.shield
    hl, hw = 8.5, 2.6
    cv.part(line=3)
    cv.tpoly([(mx - sx * hl - nx * hw, my - sy * hl - ny * hw), (mx + sx * hl - nx * hw, my + sy * hl - ny * hw),
              (mx + sx * hl + nx * hw, my + sy * hl + ny * hw), (mx - sx * hl + nx * hw, my - sy * hl + ny * hw)],
             SLAB, bevel=1.4, base=0.55)
    # rune on the slab
    cv.tline((mx - sx * 3, my - sy * 3), (mx + sx * 3, my + sy * 3), RUNE,
             inten=0.4 + 0.4 * min(1.0, g.shield + 0.2) if not g.dead_core else 0.0, decal=True)
    # cracks
    if g.cracks:
        cv.part(line=False)
        segs = [((20.5, 18.5), (23.5, 23.5)), ((23.5, 23.5), (21.5, 27.5)), ((32.5, 31.5), (29.5, 35.5))]
        if g.cracks > 1:
            segs += [((15.5, 25.5), (18.5, 29.5)), ((28.5, 16.5), (30.5, 20.5)), ((21.5, 33.5), (24.5, 37.5))]
        for a, b in segs:
            cv.tline(B(*a), B(*b), CRACK, decal=True)
    return cv.render()


def rubble(core=0.0, settle=0.0, seed=3):
    """Collapsed golem: a mound of boulders, core fading in the middle."""
    cv = TCanvas(W, H)
    rng = np.random.default_rng(seed)
    rocks = [(24, 43, 6.5), (16, 44.5, 4.2), (32, 44.5, 4.6), (20, 39.5, 4.0), (29, 40, 3.6), (11, 45.5, 2.6),
             (38, 45.5, 2.8), (25, 37 + settle, 3.2)]
    for i, (x, y, r) in enumerate(rocks):
        cv.part()
        cv.ellipse((x, y), r * 1.15, r, ROCK if i % 2 else SLAB, ang=rng.random() * 0.6 - 0.3)
    cv.part()
    cv.ellipse((19, 40.5), 3.4, 1.2, M.MOSS_M, decal=True)
    if core > 0:
        cv.part(line=False)
        cv.poly([(26, 39.5), (28.2, 41.5), (26, 43.5), (23.8, 41.5)], CORE, shade="grad", base=0.2 + 0.4 * core)
    else:
        cv.part(line=False)
        cv.poly([(26, 39.5), (28.2, 41.5), (26, 43.5), (23.8, 41.5)], CORE_DEAD, flat=0.4)
    return cv.render()


def clips():
    idle = []
    for i in range(4):
        b = [0, 0, 1, 1][i]
        idle.append(draw(G(body=(0, b), fist_f=(37.0, 39.0 + b), fist_b=(14.0, 38.0 + b),
                           core=0.45 + 0.25 * math.sin(TAU * i / 4), eye=0.5)))
    walk = []
    steps = [  # foot_f, foot_b, body_dy, sway
        ((31, 47), (17, 46), 0, 0.02), ((30, 45), (18, 47), 1, 0.0), ((28, 44), (20, 47), 0, -0.02),
        ((26, 47), (22, 47), 0, -0.02), ((26, 47), (21, 45), 1, 0.0), ((28, 47), (19, 44), 0, 0.02)]
    for i, (ff, fb, dy, sw) in enumerate(steps):
        s = math.sin(TAU * i / 6)
        walk.append(draw(G(body=(0, dy), lean=sw, foot_f=ff, foot_b=fb, fist_f=(37 - 2 * s, 39 + dy),
                           fist_b=(14 + 2 * s, 38 + dy), core=0.5)))
    defend = [
        draw(G(fist_f=(37, 33), shield=0.35, core=0.4, lean=-0.02)),
        draw(G(fist_f=(37, 28), shield=0.7, core=0.35, lean=-0.04, body=(0, 1))),
        draw(G(fist_f=(36, 26), shield=1.0, core=0.3, lean=-0.05, body=(0, 1), eye=0.8)),
    ]
    tele = []
    for i in range(5):
        t = (i + 1) / 5.0
        fx = 37 - 6 * t
        fy = 39 - 31 * t
        tele.append(draw(G(fist_f=(fx, fy), fist_b=(14 + 4 * t, 38 - 30 * t), lean=-0.08 * t, body=(0, -1 * t),
                           core=0.5 + 0.8 * t, eye=0.5 + 0.8 * t)))
    FX.sparkle(tele[-1], 25, 26, 3, hex_rgba('#fff4d6'), AMBER[2])
    a = [
        draw(G(fist_f=(30, 7), fist_b=(19, 8), lean=-0.1, body=(0, -1), core=1.3, eye=1.3)),
        draw(G(fist_f=(41, 18), fist_b=(33, 16), lean=0.08, core=1.1, eye=1.0)),
        draw(G(fist_f=(42, 37), fist_b=(38, 36), lean=0.18, body=(1, 2), core=0.9, eye=0.9)),
        draw(G(fist_f=(42, 43), fist_b=(37, 43), lean=0.22, body=(1, 3), core=0.8, eye=0.8,
               foot_f=(30, 47), foot_b=(18, 47))),
        draw(G(fist_f=(40, 40), fist_b=(30, 40), lean=0.1, body=(0, 1), core=0.6, eye=0.6)),
    ]
    # impact: dust + debris + shock lines on the ground in front
    imp = a[3]
    dust = [STONE[4], STONE[3], STONE[2]]
    FX.burst(imp, 46, 45, 5.0, dust, seed=1, jag=0.4)
    FX.burst(imp, 33, 46, 3.0, dust, seed=7, jag=0.4)
    for (x, y, sz) in ((46, 38, 2), (35, 40, 2), (44, 34, 1), (39, 36, 1)):
        FX.chunk(imp, x, y, sz, STONE[3], STONE[1])
    for x0 in (30, 33):
        imp[47, x0 - 3:x0] = AMBER[1]
    a[4] = a[4]
    FX.burst(a[4], 45, 45, 2.5, dust[1:], seed=2, jag=0.4)
    hurt = [FX.flash(draw(G(lean=-0.06, body=(-1, 0), cracks=1)), amount=0.6), draw(G(lean=-0.04, body=(-1, 0)))]
    d = [
        FX.flash(draw(G(lean=-0.08, cracks=1, core=1.2)), amount=0.5),
        draw(G(lean=-0.04, cracks=2, core=0.2, eye=0.2)),
        draw(G(lean=0.08, cracks=2, core=0.9, eye=0.1, body=(0, 3), foot_f=(30, 47), foot_b=(18, 47),
               fist_f=(38, 43), fist_b=(15, 43))),
        draw(G(lean=0.15, cracks=2, core=0.1, eye=0.0, body=(0, 5), foot_f=(31, 47), foot_b=(17, 47),
               fist_f=(39, 45), fist_b=(16, 45), dead_core=False)),
    ]
    d4 = draw(G(lean=0.25, cracks=2, core=0.0, body=(1, 7), foot_f=(32, 47), foot_b=(16, 47),
                fist_f=(40, 45), fist_b=(15, 45), dead_core=True))
    for (x, y, sz) in ((12, 30, 3), (36, 26, 2), (28, 22, 2), (18, 24, 2)):
        FX.chunk(d4, x, y, sz, STONE[3], STONE[1])
    d5 = rubble(core=0.8, settle=-1)
    for (x, y, sz) in ((10, 38, 2), (38, 36, 2), (30, 31, 1)):
        FX.chunk(d5, x, y, sz, STONE[3], STONE[1])
    FX.burst(d5, 8, 45, 3, [STONE[4], STONE[3], STONE[2]], seed=5, jag=0.4)
    FX.burst(d5, 41, 45, 3, [STONE[4], STONE[3], STONE[2]], seed=6, jag=0.4)
    d6 = rubble(core=0.5)
    d7 = rubble(core=0.2)
    d8 = rubble(core=0.0)
    death = d + [d4, d5, d6, d7, d8]
    return dict(idle=idle, walk=walk, defend=defend, telegraph=tele, attack=a, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/golem.png", W, H, [24, 48])
    s.add("idle", c["idle"], fps=5, loop=True)
    s.add("walk", c["walk"], fps=8, loop=True)
    s.add("defend", c["defend"], durations=[0.08, 0.08, 0.12], loop=False)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.12, 0.14, 0.2], loop=False)
    s.add("attack", c["attack"], durations=[0.06, 0.05, 0.05, 0.2, 0.18], loop=False, events=[(3, "impact")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.14], loop=False)
    s.add("death", c["death"], durations=[0.1, 0.12, 0.12, 0.12, 0.12, 0.14, 0.16, 0.18, 0.25], loop=False)
    s.save("animations/golem.json")
