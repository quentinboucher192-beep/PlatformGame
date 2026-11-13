"""Spore Queen (Reine Sporale, Sylvanox) — hydra theme: a giant carnivorous plant
rooted in the ground.  Round violet bulb with a crown of glowing petals, glowing
spore sacs, big ground leaves, three venus-flytrap heads on vine necks."""
import math

import numpy as np

import effects as FX
from boss_a_common import (C, chaikin, dust_cloud, glow_orb, mat, motes, polar, ring, seg_dir, streak)
from boss_a_hydra import H, W, HydraTheme
from common.palette import STONE
from raster import TCanvas, put

PI = math.pi

LEAF = mat("sq_leaf", ["#4fd49a", "#2a9e74", "#1a7058", "#124a42", "#0b2e2c"])
LEAF_DK = mat("sq_leaf_dk", ["#2f9a74", "#1c6e58", "#134a42", "#0c302d", "#081e22"])
LEAF_DEAD = mat("sq_leaf_dead", ["#8a8070", "#665a50", "#463c3a", "#2c2428"])
BULB = mat("sq_bulb", ["#e6b0ff", "#b870ec", "#8a42c8", "#5c2592", "#361458"])
BULB_DEAD = mat("sq_bulb_dead", ["#8e7c96", "#685874", "#483c56", "#2c2238"])
PETAL = mat("sq_petal", ["#ffc4ec", "#f47ccc", "#c8429e", "#8a2272", "#501046"])
PETAL_DK = mat("sq_petal_dk", ["#d85cb4", "#a83688", "#761c62", "#460e3e"])
PETAL_DEAD = mat("sq_petal_dead", ["#9a8296", "#6e5a72", "#4a3a52", "#2e2236"])
SAC = mat("sq_sac", ["#fdf0ff", "#f4c0ff", "#e08aff", "#b04cff", "#6a2bb8"], emissive=True)
SAC_DIM = mat("sq_sac_dim", ["#c070f0", "#9040d0", "#6a2bb8", "#45187c"])
SAC_DEAD = mat("sq_sac_dead", ["#6e5c7c", "#4c3e5c", "#30263e"])
NECK = mat("sq_neck", ["#62dca2", "#36aa7e", "#227a60", "#165046", "#0d322f"])
NECK_DK = mat("sq_neck_dk", ["#38a87e", "#237a60", "#165046", "#0e3430", "#091f22"])
HEAD = mat("sq_head", ["#8aecb8", "#46c08c", "#2a9068", "#1a604e", "#0f3a34"])
HEAD_DK = mat("sq_head_dk", ["#4cbc8c", "#2c8c68", "#1c5e4e", "#103a34", "#0a2626"])
HEAD_DEAD = mat("sq_head_dead", ["#8a8070", "#665a50", "#463c3a", "#2c2428"])
LIP = mat("sq_lip", ["#ffa8c4", "#ff5a7e", "#c8244e", "#7e1032"])
MOUTH = mat("sq_mouth", ["#ffe6ee", "#ff9ab4", "#ff4d6d", "#b0163e", "#560a24"], emissive=True)
THROAT = mat("sq_throat", ["#7e1032", "#4a0820", "#2a0414"])
TOOTH = mat("sq_tooth", ["#fbf4e2", "#d6caae", "#9c907a"])
EYE = mat("sq_eye", ["#fff0f8", "#ff86c0", "#e8347e", "#8e1048"], emissive=True)
BIO = mat("sq_bio", ["#e6fffb", "#8ffff0", "#39f0d8", "#1fb8b0"], emissive=True)
VINE = mat("sq_vine", ["#b07ad8", "#8450b4", "#5c3288", "#3a1e5e", "#22123a"])
VINE_DK = mat("sq_vine_dk", ["#8450b4", "#5c3288", "#3a1e5e", "#22123a", "#160a28"])
ROOT = mat("sq_root", ["#3e7466", "#28544c", "#183a36", "#0e2626"])
ROOT_DEAD = mat("sq_root_dead", ["#5e5650", "#403a38", "#282428"])

VIOLETS = [C("#fdf0ff"), C("#e08aff"), C("#b04cff"), C("#6a2bb8")]
PINKS = [C("#ffe6ee"), C("#ff9ab4"), C("#ff4d6d"), C("#a3123a")]
DUST = [STONE[4], STONE[3], STONE[2]]
SPORE_FX = [C("#fdf0ff"), C("#f0b8ff"), C("#e08aff"), C("#b04cff"), C("#6a2bb8")]

BULB_C = (64.0, 87.0)
BULB_R = (23.0, 22.0)
SACS = [((42.0, 87.0), 6.4), ((86.5, 94.0), 6.0), ((52.5, 101.5), 4.6), ((76.5, 103.0), 4.4)]


def _side_up(nx, ny):
    """+1 if the normal (nx, ny) points towards the upper-left (lit side)."""
    return 1.0 if (nx * -0.6 + ny * -0.8) >= 0 else -1.0


def leaf(cv, base, tip, w, m, sag=0.0, n=12, rib=True, dots=(), glow=0.5, rib_mat=None):
    bx, by = base
    tx, ty = tip
    L = math.hypot(tx - bx, ty - by) or 1.0
    ux, uy = (tx - bx) / L, (ty - by) / L
    nx, ny = -uy, ux
    left, right, mid = [], [], []
    for i in range(n + 1):
        t = i / n
        s = math.sin(PI * t) * sag
        cx, cy = bx + (tx - bx) * t + nx * s, by + (ty - by) * t + ny * s
        hw = w * (math.sin(PI * min(1.0, t ** 0.8)) ** 0.85)
        left.append((cx + nx * hw, cy + ny * hw))
        right.append((cx - nx * hw, cy - ny * hw))
        mid.append((cx, cy))
    cv.part()
    cv.tpoly(left + right[::-1], m, bevel=1.7, base=0.6)
    if rib:
        cv.part(line=False)
        rm = rib_mat or m
        for a, b in zip(mid[1:-3], mid[2:-2]):
            cv.tline(a, b, rm, inten=0.3, decal=True)
        # side veins
        for i in range(3, n - 2, 3):
            a = mid[i]
            for sgn in (1, -1):
                hw = w * 0.7 * math.sin(PI * (i / n))
                b = (a[0] + (nx * sgn) * hw + ux * hw * 0.8, a[1] + (ny * sgn) * hw + uy * hw * 0.8)
                cv.tline(a, b, rm, inten=0.42, decal=True)
    for i in dots:
        if 0 <= i < len(mid):
            cv.tpx(mid[i], BIO, inten=0.45 + 0.5 * glow, decal=True)
    return mid


def thorn(cv, base, ang, ln, wd, m):
    tip = (base[0] + math.cos(ang) * ln, base[1] + math.sin(ang) * ln)
    nx, ny = -math.sin(ang) * wd, math.cos(ang) * wd
    cv.tpoly([(base[0] + nx, base[1] + ny), tip, (base[0] - nx, base[1] - ny)], m, bevel=0.8, base=0.6)


class SporeQueen(HydraTheme):
    id = "spore_queen"
    seed = 1100
    head_len = 24.0
    head_lie = 7.0
    rest = [(23.0, 47.0, -0.15), (54.0, 21.0, 0.1), (95.0, 53.0, 0.28)]
    anchors = [((50.0, 76.0), (-0.75, -0.66)), ((64.0, 67.0), (0.0, -1.0)), ((82.0, 79.0), (0.9, -0.44))]
    neck_r = [(5.0, 3.4), (5.4, 3.6), (5.6, 3.8)]
    fall_dx = (-8.0, 2.0, 6.0)
    fall_rot = (-1.2, 0.9, 1.3)
    cols = {
        "glow": PINKS, "ring": [C("#ff9ab4"), C("#e08aff")], "smear": C("#a0e8c8"),
        "snap": [C("#ffffff"), C("#ff9ab4")],
        "dissolve": [C("#f0b8ff"), C("#e08aff"), C("#b04cff"), C("#6a2bb8")], "wither": C("#6e6270"),
    }

    # ------------------------------------------------------------- bite path
    def bite_c(self, t):
        return (7.0 * t, 21.0 * t, 0.44 * t)

    def bite_b(self, t):
        return (20.0 * t, 23.0 * t, 0.45 * t)

    # ------------------------------------------------------------------ parts
    def _leaf_m(self, p, dark=False):
        if p.dead >= 0.55:
            return LEAF_DEAD
        return LEAF_DK if dark else LEAF

    def back(self, cv, p):
        m = self._leaf_m(p, True)
        wl = p.wilt * 14
        leaf(cv, (52, 104), (12, 88 + wl), 7.5, m, sag=-3, dots=(4, 7) if p.dead < 0.5 else (), glow=p.glow)
        leaf(cv, (76, 104), (118, 90 + wl), 7.5, m, sag=3, dots=(5, 8) if p.dead < 0.5 else (), glow=p.glow)
        leaf(cv, (57, 98), (36, 66 + wl * 1.4), 5.5, m, sag=-2)
        leaf(cv, (72, 98), (96, 70 + wl * 1.4), 5.5, m, sag=2)
        # roots creeping on the ground
        cv.part()
        rm = ROOT if p.dead < 0.55 else ROOT_DEAD
        for pts in (((50, 109), (36, 110), (22, 109.5), (8, 110.5)), ((78, 109), (94, 110), (110, 109.5),
                                                                       (124, 110.5))):
            sm = chaikin(list(pts), 1)
            cv.tchain(sm, [2.3 - 1.6 * i / (len(sm) - 1) for i in range(len(sm))], rm)

    def body(self, cv, p):
        m = BULB if p.dead < 0.6 else BULB_DEAD
        cx, cy = BULB_C[0] + p.sway * 0.3, BULB_C[1]
        rx, ry = BULB_R
        cv.part()
        cv.tellipse((cx, cy), rx, ry, m)
        # vertical ribs
        cv.part(line=False)
        for xo in (-0.72, -0.38, 0.0, 0.38, 0.72):
            pts = []
            for yy in np.arange(cy - ry + 3, cy + ry - 1, 2.0):
                e = max(0.0, 1 - ((yy - cy) / ry) ** 2)
                pts.append((cx + xo * rx * math.sqrt(e), yy))
            for a, b in zip(pts[:-1], pts[1:]):
                cv.tline(a, b, m, inten=0.3 if xo < 0.2 else 0.22, decal=True)
        # glowing pores between the ribs
        if p.dead < 0.6:
            for (ox, oy) in ((-10, -12), (-4, -15), (5, -14), (12, -10), (-15, -3), (16, -2), (-6, -6), (8, -5),
                             (-12, 6), (13, 7), (2, 3)):
                cv.tpx((cx + ox, cy + oy), SAC, inten=0.35 + 0.45 * min(1.0, p.glow), decal=True)
        # sacs
        for i, (c, r) in enumerate(SACS):
            c = (c[0] + p.sway * 0.25, c[1])
            cv.part()
            if i in p.burst or p.dead >= 0.6:
                cv.tdisc(c, r * 0.85, SAC_DEAD, bias=-0.1)
                cv.part(line=False)
                cv.tdisc((c[0] + 0.5, c[1] + 0.5), r * 0.45, THROAT, flat=0.2)
                continue
            s = min(1.6, p.sacs)
            rr = r * (0.78 + 0.3 * s)
            sm = SAC if s > 0.3 else SAC_DIM
            cv.tdisc(c, rr, sm, bias=-0.24 + 0.24 * s)
            cv.part(line=False)
            # veins over the sac
            cv.tline((c[0] - rr * 0.7, c[1] - rr * 0.1), (c[0] + rr * 0.2, c[1] + rr * 0.6), SAC_DIM, inten=0.3,
                     decal=True)
            cv.tline((c[0] - rr * 0.1, c[1] - rr * 0.7), (c[0] + rr * 0.6, c[1] - rr * 0.1), SAC_DIM, inten=0.3,
                     decal=True)
            cv.tpx((c[0] - rr * 0.45, c[1] - rr * 0.5), sm, inten=1.0, decal=True)

    def _petals(self, cv, p, front):
        cb = (64.0 + p.sway, 67.0)
        if front:
            spec = ((-2.8, 21.0, 5.0), (-0.34, 21.0, 5.0), (-2.4, 13.0, 3.6), (-0.74, 13.0, 3.6))
            m = PETAL
        else:
            spec = ((-2.4, 24.0, 4.6), (-1.95, 28.0, 4.4), (-1.2, 28.0, 4.4), (-0.75, 24.0, 4.6))
            m = PETAL_DK
        if p.dead >= 0.6:
            m = PETAL_DEAD
        cr = min(1.25, p.crown)
        for a0, ln0, wd in spec:
            side = 1.0 if a0 > -PI / 2 else -1.0
            a = a0 + (a0 + PI / 2) * 0.45 * cr + side * p.wilt * 1.3
            ln = ln0 + 3.0 * cr - 7.0 * p.wilt
            base = polar(cb, a, 2.5)
            tip = polar(cb, a, ln)
            mid = leaf(cv, base, tip, wd, m, sag=side * (2.5 + 2.0 * cr), n=12, rib=True)
            if p.dead < 0.6:
                cv.part(line=False)
                gl = min(1.0, 0.25 * p.glow + cr)
                for q0, q1 in zip(mid[4:-3], mid[5:-2]):
                    cv.tline(q0, q1, SAC, inten=0.3 + 0.5 * gl, decal=True)
                cv.tdisc(mid[-3], 1.0 + 0.7 * cr, SAC, flat=0.5 + 0.45 * gl)

    def crown_back(self, cv, p):
        self._petals(cv, p, False)
        # glowing spore cup the middle neck grows from
        cv.part()
        cb = (64.0 + p.sway, 67.0)
        cv.tellipse(cb, 8.0, 4.0, PETAL_DK if p.dead < 0.6 else PETAL_DEAD, bias=-0.1)
        if p.dead < 0.6:
            cv.part(line=False)
            cv.tellipse((cb[0], cb[1] - 0.5), 6.0, 2.4, SAC, flat=0.35 + 0.45 * min(1.3, p.crown))

    def crown_front(self, cv, p):
        self._petals(cv, p, True)

    def front(self, cv, p):
        m = self._leaf_m(p)
        wl = p.wilt * 3
        dots = (3, 6, 9) if p.dead < 0.5 else ()
        leaf(cv, (60, 108), (22, 106 + wl), 6.0, m, sag=-2.5, dots=dots, glow=p.glow)
        leaf(cv, (68, 108), (108, 104 + wl), 6.0, m, sag=2.5, dots=dots, glow=p.glow)
        leaf(cv, (61, 109), (45, 96 + wl * 2), 4.0, m, sag=-1)
        leaf(cv, (67, 109), (84, 97 + wl * 2), 4.0, m, sag=1)
        if p.ground_glow > 0.05 and p.dead < 0.5:
            # glowing roots crawling towards the vines
            cv.part(line=False)
            g = min(1.2, p.ground_glow)
            x1 = 80 + 44 * min(1.0, g)
            for x in np.arange(76, x1, 1.0):
                y = 110.6 - (0.6 if int(x) % 7 < 3 else 0)
                cv.tpx((x, y), SAC, inten=0.4 + 0.4 * g)

    # ------------------------------------------------------------ neck/head
    def neck(self, cv, pts, radii, k, p, dark):
        m = NECK_DK if dark else NECK
        lm = LEAF_DK if dark else LEAF
        if p.dead >= 0.55:
            m = lm = LEAF_DEAD
        cv.part()
        cv.tchain(pts, radii, m)
        cv.part(line=False)
        for i in range(3, len(pts) - 2, 4):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            r = radii[i]
            cv.tline((pts[i][0] + nx * r * 0.85, pts[i][1] + ny * r * 0.85),
                     (pts[i][0] - nx * r * 0.85, pts[i][1] - ny * r * 0.85), m, inten=0.25, decal=True)
        for i in range(2, len(pts) - 2, 3):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            s = _side_up(nx, ny)
            if p.dead < 0.5:
                cv.tpx((pts[i][0] + nx * s * radii[i] * 0.45, pts[i][1] + ny * s * radii[i] * 0.45), BIO,
                       inten=0.4 + 0.5 * min(1.0, p.glow), decal=True)
        # leaflets on the outer side
        for i in range(5, len(pts) - 4, 6):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            s = -_side_up(nx, ny)
            r = radii[i]
            b = (pts[i][0] + nx * s * r * 0.8, pts[i][1] + ny * s * r * 0.8)
            dx, dy = nx * s * 0.8 + math.cos(a) * 0.6, ny * s * 0.8 + math.sin(a) * 0.6
            dl = math.hypot(dx, dy)
            leaf(cv, b, (b[0] + dx / dl * 7, b[1] + dy / dl * 7 + p.wilt * 3), 2.2, lm, sag=0.0, n=6, rib=False)

    def wound(self, cv, pt, r, p, ang=0.0):
        cv.part()
        cv.tdisc(pt, r * 1.05, NECK_DK if p.dead < 0.55 else LEAF_DEAD)
        cv.part(line=False)
        cv.tdisc(pt, r * 0.6, SAC if p.dead < 0.7 else SAC_DEAD, flat=0.55)

    def head(self, cv, hp, k, p, dark):
        x, y, a, j = hp.x, hp.y, hp.a, hp.jaw
        glow = min(1.3, hp.glow)
        dead = p.dead >= 0.55
        hm = HEAD_DEAD if dead else (HEAD_DK if dark else HEAD)
        L = self.head_len
        ua, la = -0.8 * j, 0.5 * j
        hu = 1.5

        def Q(u, v, r=0.0):
            du, dv = u - hu, v
            cr, sr = math.cos(r), math.sin(r)
            u2 = hu + du * cr - dv * sr
            v2 = du * sr + dv * cr
            ca, sa = math.cos(a), math.sin(a)
            return (x + u2 * ca - v2 * sa, y + u2 * sa + v2 * ca)

        def half(rr, upper):
            """within-mask: one side of the lip line of a lobe rotated by rr."""
            h0 = cv.T(Q(hu, 0, rr))
            h1 = cv.T(Q(L, 0, rr))
            ex, ey = h1[0] - h0[0], h1[1] - h0[1]

            def fn(px, py):
                c = (px - h0[0]) * ey - (py - h0[1]) * ex
                return c > 0 if upper else c <= 0
            return fn

        # hinge knob (where the neck enters)
        cv.part()
        cv.tdisc(Q(0.5, 0), 5.0, hm, bias=-0.05)
        # mouth: dark crimson pit with a glowing throat
        if j > 0.04:
            cv.part()
            cv.tpoly([Q(hu, 0), Q(L - 0.5, -0.6, ua), Q(L - 0.5, 0.6, la)], THROAT, flat=0.55)
            cv.part(line=False)
            mr = (ua + la) / 2
            cv.tcapsule(Q(hu + 2.0, 0, mr), Q(hu + (L - hu) * (0.45 + 0.2 * j), 0, mr), 0.8 + 1.6 * j,
                        0.5 + 1.0 * j, MOUTH if not dead else THROAT, flat=0.15 + 0.6 * glow)
        cx = (hu + L) / 2
        rx = (L - hu) / 2 + 0.4
        # lower lobe
        cv.part()
        cv.tellipse(Q(cx, 0, la), rx, 7.0, hm, ang=a + la, within=half(la, False))
        cv.part(line=1)
        cv.tcapsule(Q(hu + 1.5, 0.8, la), Q(L - 1.2, 0.8, la), 1.4, 1.0, LIP if not dead else hm)
        cv.part(line=1)
        for u in (7.5, 11.5, 15.5, 19.5):
            thorn(cv, Q(u, 0.4, la), a + la - PI / 2 + (u - 13) * 0.05, 3.4, 0.85, TOOTH)
        # upper lobe
        cv.part()
        cv.tellipse(Q(cx, 0, ua), rx, 8.6, hm, ang=a + ua, within=half(ua, True))
        cv.part(line=1)
        cv.tcapsule(Q(hu + 1.5, -0.8, ua), Q(L - 1.2, -0.8, ua), 1.4, 1.0, LIP if not dead else hm)
        cv.part(line=1)
        for u in (5.5, 9.5, 13.5, 17.5, 21.5):
            thorn(cv, Q(u, -0.4, ua), a + ua + PI / 2 - (u - 13) * 0.05, 3.6 + 0.8 * (8 < u < 20), 0.85, TOOTH)
        # speckles + glowing eyes on the upper lobe
        cv.part(line=False)
        for (u, v) in ((14.5, -6.0), (18.0, -4.2), (11.0, -3.0), (19.5, -6.4), (16.0, -2.4)):
            cv.tpx(Q(u, v, ua), PETAL_DK if not dead else hm, inten=0.65, decal=True)
        if not dead:
            g = 0.4 + 0.5 * glow
            cv.tdisc(Q(6.6, -5.0, ua), 1.6, EYE, flat=g)
            cv.tdisc(Q(10.8, -6.6, ua), 1.15, EYE, flat=g * 0.92)
            cv.tdisc(Q(4.0, -2.6, ua), 0.9, EYE, flat=g * 0.85)

    def vine(self, cv, v, p):
        pts = v.points()
        n = len(pts)
        radii = [v.r0 + (v.r1 - v.r0) * (i / (n - 1)) ** 0.8 for i in range(n)]
        m = VINE_DK if v.back else VINE
        cv.part()
        cv.tchain(pts, radii, m)
        cv.part(line=False)
        # glowing vein along the lit side
        for i in range(1, n - 3):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            s = _side_up(nx, ny)
            q = (pts[i][0] + nx * s * radii[i] * 0.35, pts[i][1] + ny * s * radii[i] * 0.35)
            if i % 2 == 0:
                cv.tpx(q, SAC, inten=0.3 + 0.5 * min(1.0, v.glow), decal=True)
        cv.part(line=1)
        for i in range(2, n - 2, 3):
            a = seg_dir(pts, i)
            sd = 1 if (i // 3) % 2 else -1
            nx, ny = -math.sin(a) * sd, math.cos(a) * sd
            b = (pts[i][0] + nx * radii[i] * 0.8, pts[i][1] + ny * radii[i] * 0.8)
            thorn(cv, b, math.atan2(ny, nx) + 0.5 * sd, 3.2, 1.0, LIP)
        cv.part(line=False)
        cv.tdisc(pts[-1], 1.6, SAC, flat=0.45 + 0.5 * min(1.0, v.glow))
        for i in range(4, n - 2, 5):
            cv.tdisc(pts[i], 1.1, SAC, flat=0.35 + 0.5 * min(1.0, v.glow))

    # ---------------------------------------------------------------- effects
    def post_idle(self, f, i, p):
        rng = np.random.default_rng(self.seed + 5)
        for k in range(7):
            x0 = 40 + rng.random() * 50
            y0 = 70 - rng.random() * 30
            y = y0 - ((i + k) % 6) * 2.5
            if y > 4:
                put_empty(f, x0 + math.sin(i + k) * 1.5, y, VIOLETS[1 + (k % 3)])
        return f

    def telegraph_spores_fx(self, f, i, t, p):
        cb = (64 + p.sway, 64)
        motes(f, cb[0], cb[1], 6 + 4 * i, 10, 34 - 10 * t, SPORE_FX[:4], seed=self.seed + 200 + i, inward=0.1 * t,
              big=0.15)
        for k, (c, r) in enumerate(SACS):
            motes(f, c[0], c[1], 1 + i // 2, r + 2, r + 6, VIOLETS[1:3], seed=self.seed + 230 + i * 4 + k)
        if i >= 3:
            ring(f, cb[0], cb[1] - 4, 15 - 2 * (i - 3), VIOLETS[1], -PI, 0.0, dotted=2)
        if i == 5:
            FX.sparkle(f, int(cb[0]), int(cb[1] - 8), 5, VIOLETS[0], VIOLETS[1])

    def spores_fx(self, f, i, p):
        cb = (64.0, 60.0)
        tmp = np.zeros_like(f)
        if i == 0:
            ring(f, cb[0], cb[1], 12, VIOLETS[1], -PI, 0, dotted=2)
            for c, r in SACS:
                motes(f, c[0], c[1], 3, r + 1, r + 4, VIOLETS[:3], seed=self.seed + 260 + int(c[0]))
        if i == 1:
            FX.burst(tmp, cb[0] - 12, cb[1] - 10, 6, SPORE_FX[1:4], seed=self.seed + 272, jag=0.5)
            FX.burst(tmp, cb[0] + 13, cb[1] - 10, 6, SPORE_FX[1:4], seed=self.seed + 271, jag=0.5)
            FX.under(f, tmp)
            FX.burst(f, cb[0] + 4, cb[1] - 13, 8, SPORE_FX[:4], seed=self.seed + 270, jag=0.45)
            FX.burst(f, cb[0] - 5, cb[1] - 18, 5, SPORE_FX[1:4], seed=self.seed + 274, jag=0.5)
            for (x, y, r) in ((92, 34, 4.0), (80, 20, 3.4), (102, 48, 3.4)):
                streak(f, (x - 10, y + 7), (x - 3, y + 2), VIOLETS[3])
                streak(f, (x - 7, y + 5), (x - 3, y + 2), VIOLETS[2])
                glow_orb(f, x, y, r, SPORE_FX[:4])
            motes(f, cb[0], cb[1] - 8, 26, 12, 34, SPORE_FX[:4], seed=self.seed + 273, arc=(-PI, 0.3), big=0.2)
        if i == 2:
            FX.burst(tmp, cb[0] + 4, cb[1] - 16, 8, SPORE_FX[1:4], seed=self.seed + 280, jag=0.55)
            FX.burst(tmp, cb[0] + 16, cb[1] - 22, 5, SPORE_FX[2:4], seed=self.seed + 281, jag=0.55)
            FX.under(f, tmp)
            for (x, y, r) in ((104, 28, 2.6), (96, 12, 2.2), (114, 44, 2.2)):
                glow_orb(f, x, y, r, SPORE_FX[1:4])
            motes(f, cb[0] + 6, cb[1] - 14, 18, 8, 36, SPORE_FX[1:5], seed=self.seed + 282, arc=(-PI, 0.2))
        if i == 3:
            motes(f, cb[0] + 10, cb[1] - 22, 12, 6, 36, SPORE_FX[2:5], seed=self.seed + 290, arc=(-PI, 0.3))

    def telegraph_vines_fx(self, f, i, t, p):
        for x in (88, 100):
            motes(f, x, 106, 2 + 2 * i, 3, 10, VIOLETS[1:3] + [PINKS[1]], seed=self.seed + 300 + x + i,
                  arc=(-PI, 0))
        if i >= 1:
            dust_cloud(f, 94, 110, 3 + i, DUST, seed=self.seed + 310 + i, n=2)
        if i == 4:
            for v in self.vines_windup(1.0)[1:]:
                tip = v.points()[-1]
                FX.sparkle(f, int(tip[0]), int(tip[1]), 2, PINKS[0], VIOLETS[1])

    def vines_fx(self, f, i, p):
        if i == 0:
            for k in range(5):
                a = -2.2 + k * 0.32
                for rr in (40, 43):
                    x, y = 86 + math.cos(a) * rr * 0.75, 111 + math.sin(a) * rr
                    put_empty(f, x, y, C("#a0e8c8") if rr == 40 else C("#35b583"))
        if i == 1:
            tmp = np.zeros_like(f)
            FX.burst(tmp, 124, 104, 7, SPORE_FX[:4], seed=self.seed + 322, jag=0.5)
            FX.burst(tmp, 113, 107, 4, SPORE_FX[1:4], seed=self.seed + 323, jag=0.5)
            FX.under(f, tmp)
            dust_cloud(f, 118, 110, 10, DUST, seed=self.seed + 320, n=6)
            for (x, y, s) in ((120, 94, 2), (106, 96, 2), (126, 100, 1), (98, 92, 1), (112, 90, 1)):
                FX.chunk(f, x, y, s, STONE[3], STONE[1])
            for x in range(90, 128):
                if (x * 7) % 5 < 3:
                    f[111, x] = VIOLETS[1] if f[111, x, 3] == 0 else f[111, x]
            FX.sparkle(f, 118, 104, 3, VIOLETS[0], VIOLETS[1])
        if i == 2:
            dust_cloud(f, 116, 110, 6, DUST[1:], seed=self.seed + 330, n=4)
            for (x, y, s) in ((124, 88, 1), (110, 86, 2), (102, 98, 1)):
                FX.chunk(f, x, y, s, STONE[3], STONE[1])
        if i == 3:
            dust_cloud(f, 116, 110, 3, DUST[2:], seed=self.seed + 340, n=3)

    def summon_fx(self, f, i, p):
        spots = (36, 92)
        src = ((58.0, 56.0), (70.0, 56.0))
        # glowing seed pods lobbed from the crown to both sides, sprouts glow where they land
        path = {1: 0.0, 2: 0.45, 3: 1.0}
        if i in (1, 2):
            t = path[i]
            for (x0, y0), x1 in zip(src, spots):
                x = x0 + (x1 - x0) * t
                y = y0 + (108 - y0) * t * t - 22 * math.sin(PI * t)
                if i == 2:
                    xm = x0 + (x1 - x0) * 0.2
                    ym = y0 + (108 - y0) * 0.04 - 22 * math.sin(PI * 0.2)
                    streak(f, (xm, ym), (x, y), VIOLETS[3], only_empty=False)
                    streak(f, ((xm + x) / 2, (ym + y) / 2), (x, y), VIOLETS[2], only_empty=False)
                glow_orb(f, x, y, 3.4, SPORE_FX[:4])
            if i == 1:
                tmp = np.zeros_like(f)
                FX.burst(tmp, 64, 58, 7, SPORE_FX[:4], seed=self.seed + 400, jag=0.4)
                FX.under(f, tmp)
                motes(f, 64, 56, 12, 6, 18, VIOLETS[:3], seed=self.seed + 401)
        if i >= 3:
            g = (1.0, 0.85, 0.6)[i - 3]
            for k, x in enumerate(spots):
                if i == 3:
                    FX.burst(f, x, 107, 6, SPORE_FX[:4], seed=self.seed + 410 + k, jag=0.45)
                    ring(f, x, 110, 10, VIOLETS[1], -PI, 0, dotted=2)
                    ring(f, x, 110, 13, VIOLETS[2], -PI, 0, dotted=3)
                h = (2, 5, 7)[i - 3]
                # sprout: stem + two glowing leaves + bud
                for d in range(h):
                    put(f, x, 110 - d, C("#1f8a6a") if d % 2 else C("#35b583"))
                for s_ in (-1, 1):
                    for d in range(1, 3 + (i > 3)):
                        put(f, x + s_ * d, 110 - h // 2 - d + 1, C("#4fd49a"))
                glow_orb(f, x, 110 - h, 1.6 + 0.4 * (i - 3), SPORE_FX[:3])
                motes(f, x, 104, int(10 * g), 3, 14, VIOLETS[:3], seed=self.seed + 420 + 10 * i + k, arc=(-PI, 0))
        if i >= 2:
            for x in spots:
                for xx in range(x - 8, x + 9):
                    if (xx + i) % 3 == 0:
                        put_empty(f, xx, 111, VIOLETS[2])

    def death_fx(self, f, i, p):
        if i in (1, 2):
            c, r = SACS[i - 1]
            FX.burst(f, c[0], c[1], r + 3, SPORE_FX[:4], seed=self.seed + 500 + i, jag=0.5)
            motes(f, c[0], c[1], 14, r + 2, r + 12, SPORE_FX[:4], seed=self.seed + 510 + i, big=0.3)
        if 3 <= i <= 6:
            # petals and leaves torn off, falling
            rng = np.random.default_rng(self.seed + 520 + i)
            for k in range(4 + i):
                x = 30 + rng.random() * 70
                y = 30 + (i - 3) * 14 + rng.random() * 30
                col = (C("#e884f4"), C("#b84ede"), C("#2a9e74"), C("#802cae"))[k % 4]
                for d in range(2):
                    put_empty(f, x + d, y + (d if k % 2 else 0), col)
            motes(f, 64, 70, 10, 10, 40, SPORE_FX[1:5], seed=self.seed + 540 + i, arc=(-PI, 0))

    def residue(self, t):
        cv = TCanvas(W, H)
        sink = (1 - t) * 6
        for i, (x, y, rx, ry, m) in enumerate((
                (64, 106, 15, 6, BULB_DEAD), (48, 108, 8, 3.5, LEAF_DEAD), (80, 108, 9, 3.2, LEAF_DEAD),
                (58, 103, 6, 3.6, SAC_DEAD), (72, 104, 6, 3.0, BULB_DEAD), (36, 109, 5, 2.2, HEAD_DEAD),
                (94, 109, 5, 2.4, HEAD_DEAD))):
            cv.part()
            cv.ellipse((x, y + sink * 0.5), rx * (0.7 + 0.3 * t), ry * max(0.5, t), m, ang=0.15 * ((i % 3) - 1))
        cv.part()
        cv.chain([(40, 109), (52, 106), (64, 108), (78, 105), (90, 109)], [1.2] * 5, ROOT_DEAD)
        return cv.render()


def put_empty(f, x, y, c):
    x, y = int(round(x)), int(round(y))
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


THEME = SporeQueen()
