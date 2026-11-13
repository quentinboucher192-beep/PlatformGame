"""Bulwark: ancient construct knight with a tall tower shield bearing a cyan
glyph and a long spear (32x32, collision 16x24, pivot bottom-centre)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import AMBER, CYAN, DANGER, OUTLINE, STONE, SUIT, VIOLET, hex_rgba
from raster import Mat, TCanvas, ik2, put
from sheet import Sheet

W = H = 32
TAU = 2 * math.pi

ARMOR = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="bw_armor", thresholds=[0.84, 0.6, 0.36])
ARMOR_BACK = Mat([STONE[2], STONE[1], STONE[0]], name="bw_armor_back", thresholds=[0.6, 0.35])
SLAB = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="bw_slab", thresholds=[0.8, 0.56, 0.34])
RIM = Mat([SUIT[1], SUIT[2], SUIT[3], STONE[2]], name="bw_rim", thresholds=[0.8, 0.56, 0.34])
GLYPH = Mat([CYAN[3], CYAN[2], CYAN[1]], emissive=True, name="bw_glyph", thresholds=[0.75, 0.45])
GLYPH_OFF = Mat([STONE[1], STONE[0]], name="bw_glyph_off", thresholds=[0.5])
VISOR = Mat([hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1]], emissive=True, name="bw_visor",
            thresholds=[0.88, 0.7, 0.45])
VISOR_OFF = Mat([STONE[1], STONE[0]], name="bw_visor_off", thresholds=[0.5])
SHAFT = Mat([SUIT[3], STONE[3], STONE[2]], name="bw_shaft", thresholds=[0.62, 0.36])
BLADE = Mat([SUIT[0], SUIT[1], SUIT[3]], name="bw_blade", thresholds=[0.7, 0.4])
TABARD = Mat([VIOLET[0], hex_rgba('#4a1f86'), hex_rgba('#2e1356')], name="bw_tabard", thresholds=[0.62, 0.36])
CRACK = Mat([OUTLINE], name="bw_crack")
SPARK = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]


class B:
    def __init__(self, **kw):
        self.body = (0.0, 0.0)
        self.lean = 0.0
        self.foot_f = (19.5, 31.5)
        self.foot_b = (12.0, 31.5)
        self.hand = (10.6, 21.0)      # spear hand (far arm)
        self.spear = -math.pi / 2     # spear direction (0 = right, -pi/2 = up)
        self.shield = (22.4, 20.4)    # shield centre
        self.shield_rot = 0.0
        self.glyph = 0.6
        self.visor = 0.5
        self.cracks = 0
        self.dead = False
        self.spear_on = True
        self.__dict__.update(kw)


def draw(b):
    cv = TCanvas(W, H)
    bx, by = b.body
    lean = b.lean

    def P(x, y):
        cx, cy = 15.5, 23.0
        dx, dy = x - cx, y - cy
        c, s = math.cos(lean), math.sin(lean)
        return (cx + dx * c - dy * s + bx, cy + dx * s + dy * c + by)

    sh_b, sh_f = P(12.4, 15.2), P(18.4, 15.6)
    hip_b, hip_f = P(13.6, 23.0), P(17.4, 23.0)
    # spear (behind everything; held in the far hand)
    if b.spear_on:
        ux, uy = math.cos(b.spear), math.sin(b.spear)
        hx, hy = b.hand
        butt = (hx - ux * 10.0, hy - uy * 10.0)
        tip = (hx + ux * 14.0, hy + uy * 14.0)
        neck = (hx + ux * 10.5, hy + uy * 10.5)
        cv.part()
        cv.capsule(butt, neck, 0.6, 0.6, SHAFT, flat=0.5)
        cv.part()
        nx, ny = -uy, ux
        mid = (hx + ux * 12.0, hy + uy * 12.0)
        cv.poly([neck, (mid[0] + nx * 1.4, mid[1] + ny * 1.4), tip, (mid[0] - nx * 1.4, mid[1] - ny * 1.4)],
                BLADE, bevel=0.9, base=0.6)
        cv.line(neck, (hx + ux * 13.0, hy + uy * 13.0), GLYPH if not b.dead else GLYPH_OFF, inten=0.6, decal=True)
    # far arm (spear arm)
    cv.part()
    el_b = ik2(sh_b, b.hand, 4.6, 4.6, -1.0)
    cv.capsule(sh_b, el_b, 1.9, 1.6, ARMOR_BACK)
    cv.capsule(el_b, b.hand, 1.6, 1.5, ARMOR_BACK)
    cv.disc(b.hand, 1.7, ARMOR_BACK)
    # back leg
    cv.part()
    kb = ik2(hip_b, (b.foot_b[0], b.foot_b[1] - 2.0), 4.4, 4.4, 1.0)
    cv.capsule(hip_b, kb, 2.2, 2.0, ARMOR_BACK)
    cv.capsule(kb, (b.foot_b[0], b.foot_b[1] - 2.0), 2.0, 1.8, ARMOR_BACK)
    fb = b.foot_b
    cv.poly([(fb[0] - 2.4, fb[1] - 3.0), (fb[0] + 2.0, fb[1] - 3.0), (fb[0] + 3.2, fb[1]), (fb[0] - 2.4, fb[1])],
            ARMOR_BACK, flat=0.35)
    # tabard between the legs
    cv.part()
    cv.poly([P(14.0, 22.0), P(17.6, 22.0), P(17.4, 27.0), P(15.8, 28.6), P(14.2, 27.0)], TABARD, shade="grad",
            base=0.5)
    # torso
    cv.part()
    torso = [P(10.6, 15.0), P(12.4, 12.6), P(18.6, 12.4), P(20.6, 15.0), P(19.8, 21.0), P(17.8, 23.6),
             P(13.0, 23.6), P(11.2, 20.6)]
    cv.poly(torso, ARMOR, bevel=1.8, base=0.6)
    cv.capsule(P(11.6, 21.8), P(19.2, 21.8), 1.1, 1.1, M.METAL, decal=True, bias=0.05)   # belt
    cv.line(P(15.5, 14.6), P(15.5, 19.6), M.STONE_DARK, inten=0.25, decal=True)          # chest seam
    gm = GLYPH if not b.dead else GLYPH_OFF
    cv.px_set(*P(13.6, 17.0), gm, inten=0.5 + 0.3 * b.glyph, decal=True)                 # chest rune light
    # moss on the far shoulder
    cv.ellipse(P(12.6, 13.0), 2.6, 1.1, M.MOSS_M, decal=True, bias=0.1)
    # head: helmet with crest and visor slit
    cv.part()
    hc = P(16.2, 9.6)
    cv.poly([(hc[0] - 3.4, hc[1] + 2.8), (hc[0] - 3.6, hc[1] - 1.0), (hc[0] - 1.6, hc[1] - 3.6),
             (hc[0] + 2.0, hc[1] - 3.6), (hc[0] + 3.6, hc[1] - 1.2), (hc[0] + 3.8, hc[1] + 2.8)],
            ARMOR, bevel=1.4, base=0.6)
    cv.part()
    cv.capsule((hc[0] - 2.6, hc[1] - 3.6), (hc[0] - 5.2, hc[1] - 5.6), 1.0, 0.5, M.METAL, bias=0.1)   # crest
    cv.capsule((hc[0] - 1.0, hc[1] - 3.6), (hc[0] - 2.8, hc[1] - 6.2), 1.0, 0.5, M.METAL, bias=0.15)
    cv.part(line=False)
    cv.capsule((hc[0] + 0.2, hc[1] + 0.2), (hc[0] + 3.6, hc[1] + 0.2), 0.9, 0.9, M.STONE_DARK, flat=0.15)
    vm = VISOR_OFF if b.dead else VISOR
    cv.capsule((hc[0] + 1.2, hc[1] + 0.2), (hc[0] + 3.4, hc[1] + 0.2), 0.5 + 0.3 * min(1.0, b.visor), 0.5, vm,
               flat=0.5 + 0.4 * min(1.0, b.visor))
    # front leg
    cv.part()
    kf = ik2(hip_f, (b.foot_f[0], b.foot_f[1] - 2.0), 4.4, 4.4, 1.0)
    cv.capsule(hip_f, kf, 2.3, 2.1, ARMOR)
    cv.capsule(kf, (b.foot_f[0], b.foot_f[1] - 2.0), 2.1, 1.9, ARMOR)
    cv.disc(kf, 1.6, M.METAL, bias=0.1)
    ff = b.foot_f
    cv.poly([(ff[0] - 2.4, ff[1] - 3.2), (ff[0] + 2.2, ff[1] - 3.2), (ff[0] + 3.4, ff[1]), (ff[0] - 2.4, ff[1])],
            ARMOR, bevel=1.0, base=0.55)
    # near arm reaching the shield grip
    cv.part()
    grip = (b.shield[0] - 1.6, b.shield[1] - 1.0)
    el_f = ik2(sh_f, grip, 4.0, 4.0, 1.0)
    cv.disc(sh_f, 2.6, ARMOR, bias=0.06)
    cv.capsule(sh_f, el_f, 1.8, 1.6, ARMOR)
    cv.capsule(el_f, grip, 1.6, 1.5, ARMOR)
    # tower shield (in front)
    cv.part(line=3)
    sx, sy = b.shield
    c, s = math.cos(b.shield_rot), math.sin(b.shield_rot)

    def S(dx, dy):
        return (sx + dx * c - dy * s, sy + dx * s + dy * c)

    hw, hh = 3.9, 9.0
    outline_pts = [S(-hw, -hh + 1.2), S(-hw + 1.2, -hh), S(hw - 1.2, -hh), S(hw, -hh + 1.2), S(hw, hh - 2.4),
                   S(0.0, hh + 0.4), S(-hw, hh - 2.4)]
    cv.poly(outline_pts, RIM, bevel=1.2, base=0.55)
    inner = [S(-hw + 1.2, -hh + 1.6), S(hw - 1.2, -hh + 1.6), S(hw - 1.2, hh - 2.8), S(0.0, hh - 1.0),
             S(-hw + 1.2, hh - 2.8)]
    cv.poly(inner, SLAB, shade="grad", base=0.5, decal=True)
    # cyan glyph: vertical stroke, chevron and dot
    g = 0.3 + 0.5 * b.glyph
    cv.line(S(0.0, -5.6), S(0.0, 3.6), gm, inten=g, decal=True)
    cv.line(S(-2.0, -1.6), S(0.0, -3.6), gm, inten=g, decal=True)
    cv.line(S(0.0, -3.6), S(2.0, -1.6), gm, inten=g, decal=True)
    cv.line(S(-1.6, 1.6), S(1.6, 1.6), gm, inten=g * 0.8, decal=True)
    cv.px_set(*S(0.0, 5.4), gm, inten=g, decal=True)
    # cracks
    if b.cracks:
        cv.part(line=False)
        segs = [((12.5, 13.5), (14.5, 17.5)), ((14.5, 17.5), (13.5, 20.5)), ((sx - 2.5, sy - 6.5), (sx - 0.5, sy - 3.5))]
        if b.cracks > 1:
            segs += [((18.5, 14.5), (17.5, 18.5)), ((sx + 1.5, sy + 1.5), (sx + 3.0, sy + 4.5)),
                     ((hc[0] - 2.0, hc[1] - 2.5), (hc[0] - 0.5, hc[1] + 0.5))]
        for a_, b_ in segs:
            cv.line(a_, b_, CRACK, decal=True)
    return cv.render()


def rubble(glyph=0.4, settle=0.0):
    """Collapsed construct: armour pieces, helmet and the shield lying down."""
    cv = TCanvas(W, H)
    rng = np.random.default_rng(7)
    rocks = [(12.0, 29.5, 3.6), (17.0, 30.0, 3.0), (8.0, 30.4, 2.2), (14.5, 26.6 + settle, 2.6)]
    for i, (x, y, r) in enumerate(rocks):
        cv.part()
        cv.ellipse((x, y), r * 1.2, r, ARMOR if i % 2 else SLAB, ang=rng.random() * 0.6 - 0.3)
    cv.part()
    cv.ellipse((11.0, 27.4 + settle), 2.4, 1.0, M.MOSS_M, decal=True)
    # helmet
    cv.part()
    cv.ellipse((6.0, 29.4), 2.8, 2.4, ARMOR, bias=0.05)
    cv.part(line=False)
    cv.line((6.0, 29.4), (8.0, 29.4), VISOR_OFF, inten=0.2)
    # shield lying on the ground
    cv.part(line=3)
    cv.poly([(16.0, 28.6), (29.6, 28.6), (30.6, 30.0), (29.6, 31.6), (16.0, 31.6), (15.2, 30.0)], RIM, bevel=1.0,
            base=0.55)
    cv.poly([(17.2, 29.4), (28.6, 29.4), (28.6, 30.8), (17.2, 30.8)], SLAB, flat=0.45, decal=True)
    gm = GLYPH if glyph > 0.05 else GLYPH_OFF
    cv.line((19.0, 30.0), (27.0, 30.0), gm, inten=0.3 + 0.5 * glyph, decal=True)
    # broken spear
    cv.part()
    cv.capsule((3.0, 26.6), (13.0, 24.8 + settle), 0.6, 0.6, SHAFT, flat=0.5)
    return cv.render()


def clips():
    idle = []
    for i in range(4):
        b = [0, 0, 1, 1][i]
        idle.append(draw(B(body=(0, b), hand=(10.6, 21.0 + b), shield=(22.4, 20.4 + b * 0.5),
                           glyph=0.55 + 0.12 * math.sin(TAU * i / 4), visor=0.5)))
    walk = []
    steps = [  # foot_f, foot_b, body_dy
        ((21.5, 31.5), (10.5, 31.0), 0), ((20.5, 30.0), (11.5, 31.5), 1), ((18.5, 29.5), (13.5, 31.5), 0),
        ((17.5, 31.5), (14.5, 31.5), 0), ((17.0, 31.5), (13.0, 30.0), 1), ((19.0, 31.5), (11.5, 29.5), 0)]
    for i, (ff, fb, dy) in enumerate(steps):
        s = math.sin(TAU * i / 6)
        walk.append(draw(B(body=(0, dy), foot_f=ff, foot_b=fb, hand=(10.6 - 0.5 * s, 21.0 + dy),
                           spear=-math.pi / 2 + 0.06 * s, shield=(22.4 + 0.4 * s, 20.4 + dy), glyph=0.6)))
    defend = [
        draw(B(body=(0, 1), shield=(22.8, 18.4), hand=(10.6, 21.5), glyph=0.8, visor=0.7)),
        draw(B(body=(0, 2), shield=(23.0, 16.6), hand=(10.8, 22.0), glyph=1.0, visor=0.8,
               foot_f=(20.5, 31.5), foot_b=(11.0, 31.5))),
        draw(B(body=(0, 2), shield=(23.2, 15.8), hand=(10.8, 22.0), glyph=1.2, visor=0.8,
               foot_f=(20.5, 31.5), foot_b=(11.0, 31.5))),
    ]
    FX.sparkle(defend[2], 23, 12, 1, SPARK[0], SPARK[1])
    tele = []
    keys = [((9.0, 17.0), -1.2), ((7.4, 14.0), -0.5), ((6.2, 13.2), -0.02), ((5.6, 13.0), 0.03)]
    for i, (hand, ang) in enumerate(keys):
        t = (i + 1) / 4.0
        tele.append(draw(B(hand=hand, spear=ang, lean=-0.06 * t, body=(-0.5 * t, 0.5 * t),
                           shield=(22.4 + 0.4 * t, 20.4 + 2.6 * t), visor=0.5 + 0.9 * t, glyph=0.6 + 0.3 * t)))
    FX.sparkle(tele[3], 20, 9, 2, hex_rgba('#fff4d6'), AMBER[2])
    attack = [
        draw(B(hand=(5.4, 13.0), spear=0.03, lean=-0.06, body=(-0.5, 0.5), shield=(22.8, 23.0), visor=1.4)),
        draw(B(hand=(11.0, 13.2), spear=0.03, lean=0.03, body=(0.5, 0.5), shield=(23.0, 23.2), visor=1.2,
               foot_f=(21.0, 31.5))),
        draw(B(hand=(16.6, 13.4), spear=0.03, lean=0.1, body=(1.5, 1.0), shield=(23.4, 23.4), visor=1.0,
               foot_f=(22.0, 31.5), foot_b=(11.0, 31.5))),
        draw(B(hand=(15.0, 13.4), spear=0.03, lean=0.08, body=(1.0, 1.0), shield=(23.2, 23.0), visor=0.8,
               foot_f=(22.0, 31.5), foot_b=(11.0, 31.5))),
        draw(B(hand=(11.6, 16.0), spear=-0.7, lean=0.02, body=(0.5, 0.5), shield=(22.6, 21.4), visor=0.6,
               foot_f=(21.0, 31.5))),
    ]
    # thrust streaks above and below the shaft
    for (x, y) in ((20, 12), (22, 12), (21, 16), (24, 16)):
        if attack[2][y, x, 3] == 0:
            put(attack[2], x, y, SPARK[1])
    hurt = [FX.flash(draw(B(lean=-0.08, body=(-1, 0), cracks=1, shield=(21.8, 20.6))), amount=0.6),
            draw(B(lean=-0.05, body=(-1, 0), shield=(21.8, 20.6)))]
    d0 = FX.flash(draw(B(lean=-0.1, cracks=1, glyph=1.2, visor=1.2)), amount=0.5)
    d1 = draw(B(lean=-0.05, cracks=2, glyph=0.2, visor=0.2, hand=(10.0, 22.0), spear=-1.3))
    d2 = draw(B(lean=0.1, cracks=2, glyph=0.6, visor=0.1, body=(0, 3), foot_f=(21.0, 31.5), foot_b=(11.0, 31.5),
                hand=(9.0, 24.0), spear=-0.9, shield=(23.0, 22.4), shield_rot=0.1))
    d3 = draw(B(lean=0.2, cracks=2, glyph=0.1, dead=True, body=(1, 5), foot_f=(22.0, 31.5), foot_b=(10.5, 31.5),
                hand=(9.0, 26.0), spear=-0.4, shield=(24.0, 23.6), shield_rot=0.35))
    for (x, y, sz) in ((7, 12, 2), (25, 8, 2), (17, 6, 1)):
        FX.chunk(d3, x, y, sz, STONE[3], STONE[1])
    d4 = draw(B(lean=0.35, cracks=2, glyph=0.0, dead=True, body=(2, 7), foot_f=(22.5, 31.5), foot_b=(10.0, 31.5),
                hand=(8.0, 28.0), spear=-0.15, shield=(25.0, 25.4), shield_rot=0.8))
    d5 = rubble(glyph=0.5, settle=-1.0)
    FX.burst(d5, 5, 30, 2.5, [STONE[4], STONE[3], STONE[2]], seed=5, jag=0.4)
    FX.burst(d5, 27, 30, 2.5, [STONE[4], STONE[3], STONE[2]], seed=6, jag=0.4)
    d6 = rubble(glyph=0.2)
    d7 = FX.dissolve(rubble(glyph=0.0), 0.6, seed=19, colors=SPARK, rise=8, drift=2)
    death = [d0, d1, d2, d3, d4, d5, d6, d7]
    return dict(idle=idle, walk=walk, defend=defend, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/bulwark.png", W, H, [16, 32])
    s.add("idle", c["idle"], fps=5, loop=True)
    s.add("walk", c["walk"], fps=8, loop=True)
    s.add("defend", c["defend"], durations=[0.06, 0.08, 0.12], loop=False)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.12, 0.2], loop=False)
    s.add("attack", c["attack"], durations=[0.05, 0.05, 0.14, 0.12, 0.14], loop=False, events=[(2, "hit")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.14], loop=False)
    s.add("death", c["death"], durations=[0.1, 0.12, 0.12, 0.12, 0.12, 0.14, 0.18, 0.22], loop=False)
    s.save("animations/bulwark.json")
