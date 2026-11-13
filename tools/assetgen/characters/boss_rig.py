"""Thornwarden rig: giant ruin-knight overgrown with thorny vines, glowing
cyan core in the chest, one huge stone gauntlet arm (96x96 frames)."""
import math

import numpy as np

import materials as M
from common.palette import CYAN, DANGER, MOSS, OUTLINE, SKY, STONE, SUIT, hex_rgba
from raster import Mat, TCanvas, ik2

W = H = 96

ARMOR = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="tw_armor", thresholds=[0.82, 0.6, 0.36])
ARMOR_DK = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="tw_armor_dk", thresholds=[0.8, 0.56, 0.32])
ARMOR_BK = Mat([STONE[2], STONE[1], STONE[0], SKY[1]], name="tw_armor_bk", thresholds=[0.8, 0.56, 0.32])
TRIM = Mat([SUIT[1], SUIT[2], SUIT[3], STONE[2]], name="tw_trim", thresholds=[0.8, 0.56, 0.32])
VINE = Mat([MOSS[2], MOSS[1], MOSS[0], hex_rgba('#0a2a2a')], name="tw_vine", thresholds=[0.86, 0.6, 0.34])
VINE_DEAD = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="tw_vine_dead", thresholds=[0.8, 0.56, 0.32])
THORN = Mat([hex_rgba('#e8dcc8'), hex_rgba('#b8a88e'), hex_rgba('#7a6a58')], name="tw_thorn",
            thresholds=[0.7, 0.4])
CLOAK = Mat([MOSS[1], MOSS[0], hex_rgba('#0a2a2a'), SKY[1]], name="tw_cloak", thresholds=[0.8, 0.55, 0.3])
CORE = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="tw_core", thresholds=[0.86, 0.64, 0.4])
CORE_DEAD = Mat([STONE[2], STONE[1], STONE[0]], name="tw_core_dead", thresholds=[0.6, 0.3])
BUD = Mat([CYAN[3], CYAN[2], CYAN[1]], emissive=True, name="tw_bud", thresholds=[0.75, 0.45])
EYE = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0], hex_rgba('#5a0f1e')], emissive=True, name="tw_eye",
          thresholds=[0.86, 0.5, 0.25])
SLOT = Mat([SKY[1], SKY[0]], name="tw_slot", thresholds=[0.5])
CRACK = Mat([OUTLINE], name="tw_crack")
CRACK_HOT = Mat([CYAN[3], CYAN[2]], emissive=True, name="tw_crack_hot", thresholds=[0.5])


class Pose:
    def __init__(self, **kw):
        self.hip = (47.0, 66.0)
        self.lean = 0.0           # torso rotation about the hips (+ = forward)
        self.head_tilt = 0.0      # + = looking down
        self.head_off = (0.0, 0.0)
        self.foot_f = (58.0, 95.0)
        self.foot_b = (36.0, 95.0)
        self.fist = (72.0, 74.0)  # huge arm fist centre
        self.fist_rot = 0.0
        self.elbow_dir = 1.0
        self.hand_b = (31.0, 66.0)
        self.core = 0.6           # glow level 0..1.5
        self.eye = 0.6
        self.bristle = 0.0        # thorn length boost
        self.buds = 0.5
        self.cloak_wave = 0.0
        self.cloak_lift = 0.0
        self.chest_open = 0.0     # rib plates open around the core (laser)
        self.cracks = 0
        self.dead = 0.0           # 0..1 wilt / extinguish
        self.rot = 0.0
        self.rot_c = (48.0, 95.0)
        self.offset = (0.0, 0.0)
        self.jaw = 0.0
        self.__dict__.update(kw)


def thorn(cv, base, ang, ln, wd, mat=THORN):
    tip = (base[0] + math.cos(ang) * ln, base[1] + math.sin(ang) * ln)
    nx, ny = -math.sin(ang) * wd, math.cos(ang) * wd
    cv.tpoly([(base[0] + nx, base[1] + ny), tip, (base[0] - nx, base[1] - ny)], mat, bevel=0.8, base=0.6)


def chaikin(pts, it=2):
    for _ in range(it):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            out.append((a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25))
            out.append((a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75))
        out.append(pts[-1])
        pts = out
    return pts


def vine(cv, pts, r, mat, thorns=(), bristle=0.0, buds=(), bud_level=0.5):
    cv.part()
    sm = chaikin(list(pts))
    cv.tchain(sm, [r] * len(sm), mat)
    for (i, side, ln) in thorns:
        i0 = min(i, len(pts) - 2)
        a, b = pts[i0], pts[i0 + 1]
        ang = math.atan2(b[1] - a[1], b[0] - a[0]) + side * (math.pi / 2 - 0.5)
        thorn(cv, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), ang, ln * (1 + bristle), 1.0)
    for i in buds:
        cv.part(line=False)
        p = pts[min(i, len(pts) - 1)]
        cv.tdisc((p[0], p[1] - r * 0.6), 1.1 + 0.4 * bud_level, BUD, flat=0.4 + 0.5 * bud_level)


def draw(p):
    cv = TCanvas(W, H, rot=p.rot, center=p.rot_c, offset=p.offset)
    hx, hy = p.hip
    c, s = math.cos(p.lean), math.sin(p.lean)
    dead = p.dead
    vm = VINE if dead < 0.5 else VINE_DEAD

    def B(x, y):
        """Body space (relative to hips) -> canvas, torso rotated by lean."""
        return (hx + x * c - y * s, hy + x * s + y * c)

    sh_f, sh_b = B(16.0, -23.0), B(-10.0, -24.0)
    hip_f, hip_b = B(6.0, 3.0), B(-6.0, 3.0)
    hc = B(4.0 + p.head_off[0], -39.5 + p.head_off[1])
    hl = p.lean + p.head_tilt
    hcs, hsn = math.cos(hl), math.sin(hl)

    def HD(x, y):
        return (hc[0] + x * hcs - y * hsn, hc[1] + x * hsn + y * hcs)

    # ------------------------------------------------------------ cloak (back)
    cv.part()
    wv = p.cloak_wave
    lift = p.cloak_lift
    cloak = [B(-6, -28), B(-14, -24), B(-19 - 2 * lift, -8), B(-23 - 4 * lift, 8 - 6 * lift),
             B(-22 - 5 * lift + wv, 18 - 10 * lift), B(-18 - 4 * lift + wv, 14 - 9 * lift),
             B(-15 - 3 * lift + wv * 0.5, 20 - 9 * lift), B(-11 - 2 * lift, 13 - 6 * lift),
             B(-7, 16 - 4 * lift), B(-4, 4), B(-2, -20)]
    cv.tpoly(cloak, CLOAK, bevel=2.0, base=0.55)

    # ------------------------------------------------------------- back arm
    cv.part()
    el_b = ik2(sh_b, p.hand_b, 11.5, 11.5, -1.0)
    cv.tcapsule(sh_b, el_b, 4.2, 3.6, ARMOR_BK)
    cv.tcapsule(el_b, p.hand_b, 3.6, 3.0, ARMOR_BK)
    cv.tdisc(el_b, 3.6, ARMOR_BK, bias=0.08)
    for k in (-1, 0, 1):
        a = math.atan2(p.hand_b[1] - el_b[1], p.hand_b[0] - el_b[0]) + k * 0.4
        cv.tcapsule(p.hand_b, (p.hand_b[0] + math.cos(a) * 5, p.hand_b[1] + math.sin(a) * 5), 1.3, 0.5, ARMOR_BK)
    # ------------------------------------------------------------- back leg
    cv.part()
    ank_b = (p.foot_b[0], p.foot_b[1] - 4.5)
    kn_b = ik2(hip_b, ank_b, 13.5, 13.5, 1.0)
    cv.tcapsule(hip_b, kn_b, 5.4, 4.6, ARMOR_BK)
    cv.tcapsule(kn_b, ank_b, 4.6, 3.6, ARMOR_BK)
    cv.tdisc(kn_b, 4.4, ARMOR_BK, bias=0.1)
    fb = p.foot_b
    cv.tpoly([(fb[0] - 6, fb[1] - 7), (fb[0] + 3, fb[1] - 7), (fb[0] + 9, fb[1] - 2), (fb[0] + 9, fb[1] + 0.9),
              (fb[0] - 7, fb[1] + 0.9), (fb[0] - 7, fb[1] - 4)], ARMOR_BK, bevel=1.5, base=0.5)

    # ---------------------------------------------------------------- torso
    cv.part()
    torso = [B(-10, 2), B(-14, -10), B(-14, -23), B(-7, -31), B(10, -32), B(17, -24), B(17, -10), B(11, 2)]
    cv.tpoly(torso, ARMOR_DK, bevel=2.6, base=0.62)
    # plate seams
    cv.part(line=False)
    cv.tline(B(-12, -12), B(15, -12), ARMOR_DK, inten=0.15, decal=True)
    cv.tline(B(-11, -21), B(-4, -27), ARMOR_DK, inten=0.15, decal=True)
    # core cage (rib plates) + core
    cc = B(0.5, -15.5)
    co = p.chest_open
    cv.part()
    cv.tdisc(cc, 7.6 + co * 1.5, ARMOR_DK, bias=-0.05)
    cv.part(line=False)
    cv.tdisc(cc, 5.6 + co * 1.5, SLOT, flat=0.3)
    cm = CORE_DEAD if dead >= 0.7 else CORE
    cr = 4.0 + 0.6 * min(1.5, p.core) + co * 1.2
    cv.tdisc(cc, cr, cm, bias=-0.35 + 0.5 * p.core)
    if dead < 0.7 and p.core > 0.2:
        cv.tdisc((cc[0] - 1.2, cc[1] - 1.2), 1.4 + 0.4 * p.core, cm, flat=0.95)
    # armoured shutters around the core, sliding open when the laser charges
    if co > 0.05:
        cv.part()
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2 + p.lean
            d = 6.5 + 3.0 * co
            q = (cc[0] + math.cos(a) * d, cc[1] + math.sin(a) * d)
            cv.tdisc(q, 2.4, ARMOR_DK, bias=0.1)
    # tassets (skirt plates)
    cv.part()
    cv.tpoly([B(-11, -1), B(12, -1), B(15, 9), B(3, 11), B(-3, 10), B(-14, 8)], ARMOR_DK, bevel=1.8, base=0.6)
    cv.tline(B(2, 0), B(1, 10), ARMOR_BK, inten=0.1, decal=True)
    # torso vine (diagonal) with thorns and glowing buds
    vt = [B(-13, -25), B(-8, -22), B(-5, -14), B(-4, -6), B(2, -2), B(10, -4), B(16, -9)]
    vine(cv, vt, 1.9, vm, thorns=((0, -1, 3.0), (3, -1, 3.0), (5, 1, 3.0)), bristle=p.bristle,
         buds=(1, 4) if dead < 0.5 else (), bud_level=p.buds)

    # ------------------------------------------------------------- front leg
    cv.part()
    ank_f = (p.foot_f[0], p.foot_f[1] - 4.5)
    kn_f = ik2(hip_f, ank_f, 13.5, 13.5, 1.0)
    cv.tcapsule(hip_f, kn_f, 5.8, 4.8, ARMOR_DK)
    cv.tcapsule(kn_f, ank_f, 4.8, 3.8, ARMOR_DK)
    cv.part()
    cv.tdisc(kn_f, 4.8, ARMOR_DK, bias=0.16)
    thorn(cv, (kn_f[0] + 3, kn_f[1] - 1), -0.2, 4.0, 1.6, ARMOR_DK)
    ff = p.foot_f
    cv.part()
    cv.tpoly([(ff[0] - 6, ff[1] - 7.5), (ff[0] + 3, ff[1] - 7.5), (ff[0] + 10, ff[1] - 2), (ff[0] + 10, ff[1] + 0.9),
              (ff[0] - 7, ff[1] + 0.9), (ff[0] - 7, ff[1] - 4)], ARMOR_DK, bevel=1.8, base=0.62)
    # ------------------------------------------------------------------ head
    cv.part()
    # horns (behind the helm), thorny wood curving back
    for side, (l0, sc) in ((1, (1.0, 1.0)), (-1, (0.8, 0.85))):
        hp = [HD(-2 * side, -7), HD(-6, -15 * sc), HD(-12, -20 * sc), HD(-19, -21 * sc), HD(-24, -17 * sc)]
        mat = ARMOR_DK if side > 0 else ARMOR_BK
        cv.tchain(hp, [2.8, 2.4, 1.8, 1.2, 0.6], mat)
    cv.part()
    helm = [HD(-7, -7), HD(-2, -10), HD(5, -9), HD(9, -4), HD(9, 3), HD(6, 7), HD(-4, 8), HD(-8, 3)]
    cv.tpoly(helm, ARMOR, bevel=2.0, base=0.62)
    # crest
    cv.part()
    cv.tpoly([HD(-3, -9), HD(1, -16 - p.bristle * 3), HD(4, -9)], TRIM, bevel=1.0, base=0.6)
    # face plate + eye slit
    cv.part(line=False)
    cv.tpoly([HD(1, -2.5), HD(9.5, -3.5), HD(9.5, 1.0), HD(1, 0.5)], SLOT, flat=0.3)
    if dead < 0.7:
        er = 0.9 + 0.5 * min(1.5, p.eye)
        cv.tcapsule(HD(3.5, -1.2), HD(8.8, -1.6), er, er * 0.8, EYE, flat=0.45 + 0.4 * min(1.0, p.eye))
    # jaw / grille (opens when roaring)
    cv.part()
    j = p.jaw
    cv.tpoly([HD(0, 2 + j * 2), HD(9, 2 + j * 3), HD(7, 7 + j * 4), HD(-3, 7 + j * 2)], ARMOR_DK, bevel=1.2, base=0.55)
    if j > 0.1:
        cv.part(line=False)
        cv.tpoly([HD(1, 1.0), HD(9, 1.0), HD(9, 1.5 + j * 3), HD(1, 1.5 + j * 2)], EYE,
                 flat=0.4 + 0.3 * min(1.0, p.eye))
    for k in range(3):
        cv.tline(HD(2 + 2.2 * k, 3.5 + j * 2.4), HD(1.5 + 2.2 * k, 6.5 + j * 2.6), SLOT, inten=0.3, decal=True)
    # horn vine
    vine(cv, [HD(-6, -6), HD(-9, -11), HD(-14, -14)], 1.0, vm, thorns=((0, -1, 2.0), (1, 1, 2.0)),
         bristle=p.bristle)

    # ------------------------------------------------------- huge front arm
    el_f = ik2(sh_f, p.fist, 15.5, 15.5, -p.elbow_dir)
    cv.part()
    cv.tcapsule(sh_f, el_f, 6.6, 6.0, ARMOR)
    cv.part()
    cv.tdisc(el_f, 6.4, ARMOR_DK, bias=0.15)
    cv.part()
    fx, fy = p.fist
    ax, ay = fx - el_f[0], fy - el_f[1]
    L = math.hypot(ax, ay) or 1.0
    ux, uy = ax / L, ay / L
    nx, ny = -uy, ux
    # gauntlet: flared stone forearm
    g0 = (el_f[0] + ux * 2, el_f[1] + uy * 2)
    g1 = (fx - ux * 4, fy - uy * 4)
    cv.tpoly([(g0[0] + nx * 6.0, g0[1] + ny * 6.0), (g1[0] + nx * 9.5, g1[1] + ny * 9.5),
              (g1[0] - nx * 9.5, g1[1] - ny * 9.5), (g0[0] - nx * 6.0, g0[1] - ny * 6.0)], ARMOR, bevel=2.6, base=0.6)
    # cuff ring
    cv.part()
    cv.tcapsule((g1[0] + nx * 10.0, g1[1] + ny * 10.0), (g1[0] - nx * 10.0, g1[1] - ny * 10.0), 2.2, 2.2, TRIM)
    # fist: a big blocky stone gauntlet with knuckle spikes
    cv.part()
    fr = p.fist_rot
    fa = math.atan2(uy, ux) + fr
    fu, fv = (math.cos(fa), math.sin(fa)), (-math.sin(fa), math.cos(fa))

    def FQ(u, v):
        return (fx + fu[0] * u + fv[0] * v, fy + fu[1] * u + fv[1] * v)
    cv.tpoly([FQ(-5, -9), FQ(6, -9.5), FQ(11, -6), FQ(12, 5), FQ(8, 9.5), FQ(-5, 9)], ARMOR, bevel=2.6, base=0.62)
    for k in (-4.5, -1.0, 2.5):
        cv.tline(FQ(5, k), FQ(11, k + 0.5), ARMOR_BK, inten=0.2, decal=True)
    cv.tline(FQ(3, -8.5), FQ(3, 8.5), ARMOR_BK, inten=0.2, decal=True)
    cv.part()
    for k in (-6.5, -1.5, 3.5):
        thorn(cv, FQ(10.5, k), fa - 0.35 + k * 0.04, 4.5 + p.bristle * 2, 1.7, TRIM)
    # forearm vine coil with thorns and buds
    coil = []
    for k in range(9):
        t = k / 8
        px_ = g0[0] + (g1[0] - g0[0]) * t
        py_ = g0[1] + (g1[1] - g0[1]) * t
        w = (6.0 + 3.0 * t) * math.sin(t * math.pi * 1.6 + 0.9)
        coil.append((px_ + nx * w, py_ + ny * w))
    vine(cv, coil, 1.8, vm, thorns=((2, 1, 3.0), (5, -1, 3.5)), bristle=p.bristle,
         buds=(2, 6) if dead < 0.5 else (), bud_level=p.buds)
    # pauldron (on top of the arm)
    cv.part()
    pc = (sh_f[0] + 1.0, sh_f[1] - 1.0)
    cv.tellipse(pc, 12.0, 8.2, ARMOR, ang=p.lean * 0.6 - 0.15, bias=0.05)
    cv.part()
    cv.tellipse((pc[0] + 1.5, pc[1] + 5.5), 10.5, 3.8, ARMOR_DK, ang=p.lean * 0.6 - 0.1)
    cv.part()
    for k, ang in enumerate((-2.0, -1.5, -1.0)):
        thorn(cv, (pc[0] + math.cos(ang) * 10, pc[1] + math.sin(ang) * 7), ang, 5.0 + p.bristle * 4, 1.9, ARMOR_DK)
    vine(cv, [(pc[0] - 11, pc[1] + 1), (pc[0] - 4, pc[1] - 6), (pc[0] + 5, pc[1] - 7), (pc[0] + 11, pc[1] - 1)], 1.6, vm,
         thorns=((1, -1, 2.5), (2, -1, 2.5)), bristle=p.bristle, buds=(2,) if dead < 0.5 else (), bud_level=p.buds)

    # cracks
    if p.cracks:
        cv.part(line=False)
        cm2 = CRACK_HOT if dead < 0.7 else CRACK
        segs = [((3, -26), (0, -21)), ((6, -11), (10, -6)), ((-1, -11), (-5, -8))]
        if p.cracks > 1:
            segs += [((-10, -18), (-6, -14)), ((12, -20), (14, -15)), ((-3, -27), (-7, -24))]
        for a, b in segs:
            cv.tline(B(*a), B(*b), cm2, inten=0.8, decal=True)
    return cv.render()
