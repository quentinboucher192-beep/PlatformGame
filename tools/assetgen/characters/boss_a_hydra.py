"""Hydra rig (BOSSES-A): a rooted giant plant / tree hydra with three heads on long
independent necks.  Used by `spore_queen` (Sylvanox) and `frost_hydra` (Glacior);
each boss supplies a *theme* object (boss_a_spore.py, boss_a_frost.py) that draws
its own body, crown, necks, heads, vines and effects.

Frames 128x112.  Collision box 56x64 = x 36..92, y 48..112, so the pivot
(bottom-centre of the box) is [64, 112]; the bottom pixel row is the ground.

Hydra clip set (contract section 3): idle 6, roar 8, hurt 2, death 14,
telegraph_bite 5, bite 5 (hit f2), telegraph_spores 6, spores 4 (fire f1),
telegraph_vines 5, vines 4 (impact f1), summon 6.
"""
import math

import numpy as np

import effects as FX
from boss_a_common import TAU, bezier, motes, over, ring, shake, streak
from common.palette import OUTLINE
from raster import TCanvas, tint_toward
from sheet import Sheet

W, H = 128, 112
PIVOT = [64, 112]
GROUND = 110.6


class HeadPose:
    def __init__(self, x, y, a, jaw=0.0, glow=0.0, bend=0.0, cut=None, stub=None):
        self.x, self.y, self.a = x, y, a
        self.jaw, self.glow, self.bend, self.cut = jaw, glow, bend, cut
        self.stub = stub      # fraction of the neck left on the body once severed (default = cut)

    def mouth(self, ln):
        return (self.x + math.cos(self.a) * ln, self.y + math.sin(self.a) * ln)


class VinePose:
    """A whipping vine/root: cubic bezier (base, c1, c2, tip), tapered radius."""

    def __init__(self, ctrl, r0=3.0, r1=1.0, back=False, glow=0.5, n=20):
        self.ctrl, self.r0, self.r1, self.back, self.glow, self.n = ctrl, r0, r1, back, glow, n

    def points(self):
        pts = bezier(*self.ctrl, self.n)
        return [(x, min(y, GROUND)) for (x, y) in pts]


class HPose:
    def __init__(self, **kw):
        self.heads = [None, None, None]
        self.sacs = 0.5       # spore sacs / crystal clusters: inflation + glow (0..1.6)
        self.crown = 0.15     # crown opening + glow (0..1.3)
        self.glow = 0.5       # bioluminescence level
        self.sway = 0.0       # body sway (px at the top)
        self.vines = []
        self.dead = 0.0       # 0..1 wither / extinguish
        self.wilt = 0.0       # 0..1 droop of petals / leaves
        self.cracks = 0
        self.burst = ()       # burst sac indices
        self.offset = (0.0, 0.0)
        self.scale = (1.0, 1.0)
        self.ground_glow = 0.0
        self.__dict__.update(kw)


def neck_points(th, k, hp, sway=0.0, n=18):
    (ax, ay), (dx, dy) = th.anchors[k]
    ax += sway * (GROUND - ay) / 60.0
    hx, hy = hp.x, hp.y
    D = max(6.0, math.hypot(hx - ax, hy - ay))
    hd = (math.cos(hp.a), math.sin(hp.a))
    p1 = (ax + dx * D * 0.42, ay + dy * D * 0.42)
    p2 = (hx - hd[0] * D * 0.42, hy - hd[1] * D * 0.42)
    nx, ny = -(hy - ay) / D, (hx - ax) / D
    p1 = (p1[0] + nx * hp.bend, p1[1] + ny * hp.bend)
    p2 = (p2[0] - nx * hp.bend * 0.8, p2[1] - ny * hp.bend * 0.8)
    t = np.linspace(0, 1, n)
    pts = []
    for tt in t:
        u = 1 - tt
        pts.append((u ** 3 * ax + 3 * u * u * tt * p1[0] + 3 * u * tt * tt * p2[0] + tt ** 3 * hx,
                    u ** 3 * ay + 3 * u * u * tt * p1[1] + 3 * u * tt * tt * p2[1] + tt ** 3 * hy))
    return pts


def _dir(pts, i):
    i0 = max(0, min(i, len(pts) - 2))
    return math.atan2(pts[i0 + 1][1] - pts[i0][1], pts[i0 + 1][0] - pts[i0][0])


def draw(th, p, layer=None, tf=None):
    """Render one frame.  layer: None (everything), 'body' (all but the severed
    head pieces) or 'head0'/'head1'/'head2' (only that severed piece);
    tf = (rot, center, offset) transform for the piece."""
    rot, center, off = tf or (0.0, (64.0, 112.0), (0.0, 0.0))
    cv = TCanvas(W, H, rot=rot, center=center, offset=(off[0] + p.offset[0], off[1] + p.offset[1]),
                 scale=p.scale)
    body = layer in (None, "body")

    def head(k, dark):
        hp = p.heads[k]
        if hp is None:
            return
        pts = neck_points(th, k, hp, p.sway)
        n = len(pts)
        r0, r1 = th.neck_r[k]
        radii = [r0 + (r1 - r0) * i / (n - 1) for i in range(n)]
        if hp.cut is None:
            if layer in (None, "body"):
                th.neck(cv, pts, radii, k, p, dark)
                th.head(cv, hp, k, p, dark)
            return
        ic = max(2, min(n - 3, int(round(hp.cut * (n - 1)))))
        if layer in (None, "body"):
            st = ic if hp.stub is None else max(2, min(n - 3, int(round(hp.stub * (n - 1)))))
            th.neck(cv, pts[:st + 1], radii[:st + 1], k, p, dark)
            th.wound(cv, pts[st], radii[st], p, _dir(pts, st))
        if layer in (None, "head%d" % k):
            th.neck(cv, pts[ic:], radii[ic:], k, p, dark)
            th.wound(cv, pts[ic], radii[ic], p, _dir(pts, ic) + math.pi)
            th.head(cv, hp, k, p, dark)

    if body:
        th.back(cv, p)
        for v in p.vines:
            if v.back:
                th.vine(cv, v, p)
    head(0, True)
    if body:
        th.body(cv, p)
        th.crown_back(cv, p)
    head(1, False)
    if body:
        th.crown_front(cv, p)
    head(2, False)
    if body:
        th.front(cv, p)
        for v in p.vines:
            if not v.back:
                th.vine(cv, v, p)
    return cv.render()


class HydraTheme:
    """Base class of a hydra boss theme (defaults shared by both bosses)."""
    id = "hydra"
    seed = 0
    head_len = 20.0
    head_lie = 7.0             # half height of a head lying on the ground
    fall_dx = (-6.0, 4.0, 8.0)
    fall_rot = (-0.9, 0.7, 1.1)

    def fall_tf(self, k, drop, hp):
        """Transform of a severed head piece falling to the ground."""
        g = drop * drop * 0.4 + drop * 0.6
        dy = (GROUND - self.head_lie - hp.y) * g
        return (self.fall_rot[k] * drop, (hp.x, hp.y), (self.fall_dx[k] * g, dy))

    def post_idle(self, f, i, p):
        return f

    # vine / root whips (same strike zone for both hydras): (radius0, radius1, back)
    vine_r = ((6.0, 1.6, False), (5.2, 1.4, False), (4.2, 1.2, True))
    WINDUP = (
        ((94, 111), (98, 90), (114, 72), (103, 70)),
        ((108, 111), (114, 96), (126, 82), (116, 79)),
        ((84, 111), (83, 96), (92, 80), (84, 80)),
    )
    STRIKE = (
        (((94, 111), (90, 76), (116, 60), (124, 80)), ((108, 111), (106, 86), (124, 72), (127, 92)),
         ((84, 111), (80, 86), (100, 70), (108, 86))),
        (((94, 111), (100, 74), (122, 86), (126, 110)), ((108, 111), (112, 88), (124, 96), (127, 110)),
         ((84, 111), (88, 86), (106, 92), (114, 110))),
        (((94, 111), (100, 80), (120, 88), (124, 102)), ((108, 111), (112, 92), (122, 98), (126, 106)),
         ((84, 111), (88, 90), (102, 94), (110, 104))),
        (((94, 111), (96, 100), (102, 96), (107, 100)), ((108, 111), (109, 103), (114, 101), (117, 105)),
         ((84, 111), (85, 103), (90, 101), (94, 105))),
    )

    def _vines(self, ctrls, glow):
        return [VinePose(c, r0, r1, back=bk, glow=glow) for c, (r0, r1, bk) in zip(ctrls, self.vine_r)]

    def vines_windup(self, t):
        out = []
        for c in self.WINDUP:
            b = c[0]
            stub = (b, (b[0], b[1] - 2), (b[0] + 1, b[1] - 4), (b[0] + 1, b[1] - 6))
            e = t ** 0.8
            out.append(tuple((s[0] + (q[0] - s[0]) * e, s[1] + (q[1] - s[1]) * e) for s, q in zip(stub, c)))
        return self._vines(out, t)

    def vines_strike(self, i):
        return self._vines(self.STRIKE[i], 1.0 - 0.2 * i)


# ======================================================================= clips
def _outline_mask(f):
    return (f[:, :, 0] == OUTLINE[0]) & (f[:, :, 1] == OUTLINE[1]) & (f[:, :, 2] == OUTLINE[2])


def HP(th, k, dx=0.0, dy=0.0, da=0.0, jaw=0.0, glow=0.0, bend=0.0, cut=None):
    x, y, a = th.rest[k]
    x, y, a = x + dx, y + dy, a + da
    # keep the whole head (and its open jaws) inside the frame
    L = th.head_len
    cx, cy = x + math.cos(a) * L * 0.5, y + math.sin(a) * L * 0.5
    top = cy - (L * 0.5 + 2.0 + 4.0 * jaw)
    if top < 1.0:
        y += 1.0 - top
    right = cx + L * 0.5 + 2.0
    if right > W - 1:
        x -= right - (W - 1)
    return HeadPose(x, y, a, jaw, glow, bend, cut)


def heads(th, keys):
    """keys: 3 tuples (dx, dy, da, jaw, glow[, bend])."""
    return [HP(th, k, *keys[k]) for k in range(3)]


def idle(th):
    out = []
    for i in range(6):
        ph = TAU * i / 6
        hs = []
        for k in range(3):
            q = ph + k * 2.1
            hs.append(HP(th, k, 1.6 * math.sin(q), 2.0 * math.sin(q + 1.1), 0.09 * math.sin(q + 1.8),
                         max(0.0, 0.1 + 0.2 * math.sin(q + 2.4)), 0.35 + 0.2 * math.sin(q)))
        p = HPose(heads=hs, sacs=0.5 + 0.22 * math.sin(ph), crown=0.15 + 0.1 * math.sin(ph + 1),
                  glow=0.5 + 0.2 * math.sin(ph), sway=0.8 * math.sin(ph))
        out.append(th.post_idle(draw(th, p), i, p))
    return out


def roar(th):
    out = []
    # per frame: rear (0..1), jaw, glow, shake
    keys = [(-0.25, 0.1, 0.4, 0), (0.35, 0.45, 0.7, 0), (1.0, 1.0, 1.1, 1), (1.0, 1.0, 1.3, -1),
            (1.0, 1.0, 1.3, 1), (0.95, 1.0, 1.2, -1), (0.45, 0.5, 0.8, 0), (0.05, 0.15, 0.5, 0)]
    for i, (r, jaw, gl, sh) in enumerate(keys):
        hs = []
        for k in range(3):
            w = (1.0, 1.15, 0.9)[k]
            ph = 0.6 * math.sin(i * 1.7 + k * 2)
            hs.append(HP(th, k, (-3.0 - k * 0.0) * r * w + ph, -9.0 * r * w + abs(ph), -0.75 * r * w,
                         jaw, gl, -3 * r))
        p = HPose(heads=hs, sacs=0.6 + 0.8 * max(0, r), crown=0.3 + 0.9 * max(0, r), glow=0.5 + 0.6 * max(0, r),
                  sway=-1.2 * r)
        f = draw(th, p)
        if jaw > 0.6:
            for k in range(3):
                hp = hs[k]
                mx, my = hp.mouth(th.head_len * 0.8)
                for j, rr in enumerate((5 + (i % 2) * 2, 9 + (i % 2) * 2)):
                    a0 = hp.a - 0.9
                    ring(f, mx, my, rr, th.cols["ring"][j], a0, hp.a + 0.9)
            f = shake(f, sh, 0)
        out.append(f)
    return out


def hurt(th):
    a = HPose(heads=heads(th, [(-4, -3, -0.25, 0.7, 0.9), (-3, -4, -0.3, 0.8, 0.9), (-5, -2, -0.2, 0.7, 0.9)]),
              sacs=0.9, crown=0.4, glow=0.8, sway=-1.5, cracks=1)
    b = HPose(heads=heads(th, [(-2, -1, -0.1, 0.3, 0.6), (-1, -2, -0.12, 0.3, 0.6), (-2, -1, -0.1, 0.3, 0.6)]),
              sacs=0.6, crown=0.2, glow=0.6, sway=-0.6)
    return [FX.flash(draw(th, a), amount=0.55), draw(th, b)]


def telegraph_bite(th):
    out = []
    for i in range(5):
        t = (i + 1) / 5
        hs = heads(th, [(-4 * t, -4 * t, -0.15 * t, 0.45 * t, 0.4 + 0.6 * t, 2 * t),
                        (-8 * t, -5 * t, -0.25 * t, 0.7 * t, 0.4 + 0.8 * t, 4 * t),
                        (-11 * t, -8 * t, -0.4 * t, 0.25 + 0.75 * t, 0.4 + 1.0 * t, 7 * t)])
        p = HPose(heads=hs, sacs=0.5 + 0.3 * t, crown=0.2 + 0.2 * t, glow=0.5 + 0.4 * t, sway=-1.0 * t)
        f = draw(th, p)
        hp = hs[2]
        mx, my = hp.mouth(th.head_len * 0.75)
        if i >= 1:
            motes(f, mx, my, 4 + 3 * i, 5, 16 - 2 * i, th.cols["glow"][:3], seed=300 + i, inward=0.15 * t)
        if i >= 2:
            hb = hs[1]
            bx, by = hb.mouth(th.head_len * 0.75)
            motes(f, bx, by, 2 + 2 * i, 4, 12, th.cols["glow"][:3], seed=320 + i)
        if i == 4:
            FX.sparkle(f, int(mx), int(my), 4, th.cols["glow"][0], th.cols["glow"][1])
        out.append(f)
    return out


def bite(th):
    keys = [
        [(-3, -3, -0.1, 0.5, 1.0), (-7, -4, -0.2, 0.9, 1.2), (*th.bite_c(0.45), 1.0, 1.4)],
        [(1, 0, 0.0, 0.6, 1.0), (*th.bite_b(0.6), 1.0, 1.2), (*th.bite_c(0.92), 0.85, 1.4)],
        [(3, 3, 0.1, 0.4, 0.9), (*th.bite_b(1.0), 0.8, 1.1), (*th.bite_c(1.0), 0.0, 1.2)],
        [(2, 2, 0.05, 0.1, 0.7), (*th.bite_b(1.0), 0.0, 0.9), (*th.bite_c(0.65), 0.15, 0.9)],
        [(0, 0, 0.0, 0.1, 0.5), (*th.bite_b(0.3), 0.1, 0.6), (*th.bite_c(0.2), 0.1, 0.6)],
    ]
    out = []
    for i, kk in enumerate(keys):
        p = HPose(heads=heads(th, kk), sacs=0.7 - 0.05 * i, crown=0.3, glow=0.8 - 0.06 * i, sway=0.8 if i < 3 else 0.3)
        f = draw(th, p)
        hc = p.heads[2]
        if i == 1:
            # speed smear behind the lunging heads
            for hp, seed in ((hc, 1), (p.heads[1], 2)):
                mx, my = hp.mouth(th.head_len * 0.5)
                for k in range(4):
                    oy = (k - 1.5) * 3
                    streak(f, (mx - 16 - 2 * k, my - 10 + oy), (mx - 6, my - 3 + oy), th.cols["smear"])
        if i == 2:
            mx, my = hc.mouth(th.head_len * 1.02)
            FX.sparkle(f, int(mx), int(my), 4, th.cols["snap"][0], th.cols["snap"][1])
            ring(f, mx, my, 6, th.cols["snap"][1], -1.6, 1.6, dotted=2)
        if i == 3:
            hb = p.heads[1]
            mx, my = hb.mouth(th.head_len * 1.02)
            FX.sparkle(f, int(mx), int(my), 3, th.cols["snap"][0], th.cols["snap"][1])
        out.append(f)
    return out


def telegraph_spores(th):
    out = []
    for i in range(6):
        t = (i + 1) / 6
        hs = heads(th, [(-3 * t, -3 * t, -0.25 * t, 0.3 * t, 0.5 + 0.3 * t),
                        (0, -4 * t, -0.35 * t, 0.4 * t, 0.5 + 0.4 * t),
                        (2 * t, -3 * t, -0.25 * t, 0.3 * t, 0.5 + 0.3 * t)])
        p = HPose(heads=hs, sacs=0.6 + 1.0 * t, crown=0.2 + 1.0 * t, glow=0.5 + 0.5 * t, sway=0.5 * math.sin(i * 2.1))
        f = draw(th, p)
        th.telegraph_spores_fx(f, i, t, p)
        out.append(f)
    return out


def spores(th):
    keys = [(1.6, 0.8, 0.2), (0.5, 1.3, 0.8), (0.35, 0.9, 0.5), (0.45, 0.45, 0.2)]
    out = []
    for i, (sacs, crown, jaw) in enumerate(keys):
        lift = [0.6, 1.0, 0.6, 0.2][i]
        hs = heads(th, [(-3 * lift, -3 * lift, -0.25 * lift, jaw, 0.7), (0, -5 * lift, -0.4 * lift, jaw, 0.8),
                        (2 * lift, -3 * lift, -0.25 * lift, jaw, 0.7)])
        p = HPose(heads=hs, sacs=sacs, crown=crown, glow=0.9 - 0.1 * i, sway=[0.0, -1.0, -0.5, 0.0][i])
        f = draw(th, p)
        th.spores_fx(f, i, p)
        out.append(f)
    return out


def telegraph_vines(th):
    out = []
    for i in range(5):
        t = (i + 1) / 5
        hs = heads(th, [(-2 * t, 2 * t, 0.25 * t, 0.2, 0.5 + 0.3 * t), (2 * t, 2 * t, 0.35 * t, 0.3, 0.5 + 0.4 * t),
                        (-3 * t, -4 * t, 0.45 * t, 0.5 * t, 0.5 + 0.5 * t, 3 * t)])
        p = HPose(heads=hs, sacs=0.5 + 0.3 * t, crown=0.2 + 0.2 * t, glow=0.5 + 0.5 * t, sway=-1.2 * t,
                  vines=th.vines_windup(t), ground_glow=t)
        f = draw(th, p)
        th.telegraph_vines_fx(f, i, t, p)
        out.append(f)
    return out


def vines(th):
    out = []
    for i in range(4):
        hs = heads(th, [(0, 3, 0.3, 0.3, 0.8), (3, 4, 0.4, 0.4, 0.9), ([-2, 2, 2, 0][i], [-4, 2, 1, -1][i],
                                                                        [0.5, 0.7, 0.6, 0.3][i], 0.6, 1.0)])
        p = HPose(heads=hs, sacs=0.7, crown=0.3, glow=0.9 - 0.1 * i, sway=[1.0, 1.6, 1.0, 0.3][i],
                  vines=th.vines_strike(i), ground_glow=[0.8, 1.2, 0.7, 0.3][i])
        f = draw(th, p)
        th.vines_fx(f, i, p)
        if i == 1:
            f = shake(f, 0, 1)
        out.append(f)
    return out


def summon(th):
    keys = [0.4, 1.0, 1.0, 1.0, 0.8, 0.3]
    out = []
    for i, r in enumerate(keys):
        hs = heads(th, [(-2 * r, -6 * r, -0.6 * r, 0.8 * r, 0.6 + 0.6 * r), (0, -8 * r, -0.8 * r, 1.0 * r, 0.6 + 0.7 * r),
                        (2 * r, -6 * r, -0.6 * r, 0.8 * r, 0.6 + 0.6 * r)])
        p = HPose(heads=hs, sacs=0.6 + 0.7 * r, crown=0.4 + 0.8 * r, glow=0.6 + 0.5 * r, sway=0.4 * math.sin(i * 1.9),
                  ground_glow=[0.0, 0.3, 0.7, 1.0, 0.8, 0.4][i])
        f = draw(th, p)
        th.summon_fx(f, i, p)
        out.append(f)
    return out


def death(th):
    out = []
    # 0-2: agony, thrashing heads, flashes; 3-5: necks snap, heads fall; 6-7: body withers;
    # 8-13: everything dissolves (last frames almost empty).
    agony = [
        dict(keys=[(-4, -8, -0.7, 1.0, 1.4), (-2, -10, -0.8, 1.0, 1.4), (-5, -7, -0.6, 1.0, 1.4)], sacs=1.4, crown=1.2,
             cracks=1),
        dict(keys=[(3, -2, 0.4, 0.8, 1.0), (-5, -6, -0.6, 0.6, 1.0), (4, 2, 0.6, 1.0, 1.0)], sacs=1.0, crown=0.8,
             cracks=1, burst=(0,)),
        dict(keys=[(-2, 6, 0.6, 0.6, 0.5), (4, -2, 0.5, 0.9, 0.8), (-2, 8, 0.9, 0.5, 0.6)], sacs=0.7, crown=0.5,
             cracks=2, burst=(0, 1)),
    ]
    for i, d in enumerate(agony):
        p = HPose(heads=heads(th, d["keys"]), sacs=d["sacs"], crown=d["crown"], glow=1.0 - 0.2 * i,
                  cracks=d["cracks"], burst=d.get("burst", ()), sway=[-1.5, 1.5, -1.0][i], wilt=0.15 * i)
        f = draw(th, p)
        if i == 0:
            f = FX.flash(f, amount=0.5)
        th.death_fx(f, i, p)
        out.append(shake(f, [1, -1, 1][i], 0))
    # severed heads fall (cut fraction per head, fall progress per head); the stumps wither
    fall = [
        dict(cut=(0.42, None, None), stub=(0.42, None, None), drop=(0.3, 0, 0),
             keys=[(0, 8, 0.9, 0.8, 0.3), (2, 4, 0.7, 0.5, 0.4), (-2, 10, 1.0, 0.6, 0.4)], wilt=0.4, dead=0.15),
        dict(cut=(0.42, None, 0.4), stub=(0.3, None, 0.4), drop=(0.7, 0, 0.35),
             keys=[(0, 8, 0.9, 0.6, 0.0), (3, 10, 1.0, 0.4, 0.3), (-2, 12, 1.1, 0.8, 0.2)], wilt=0.6, dead=0.3),
        dict(cut=(0.42, 0.4, 0.4), stub=(0.22, 0.4, 0.3), drop=(1.0, 0.35, 0.8),
             keys=[(0, 8, 0.9, 0.3, 0.0), (3, 12, 1.1, 0.7, 0.0), (-2, 12, 1.1, 0.4, 0.0)], wilt=0.8, dead=0.5),
    ]
    for i, d in enumerate(fall):
        hs = heads(th, d["keys"])
        for k in range(3):
            hs[k].cut, hs[k].stub = d["cut"][k], d["stub"][k]
        p = HPose(heads=hs, sacs=0.3, crown=0.2, glow=0.4 - 0.1 * i, cracks=2, burst=(0, 1, 2, 3), wilt=d["wilt"],
                  dead=d["dead"], sway=-0.5)
        f = draw(th, p, layer="body")
        for k in range(3):
            if hs[k].cut is None:
                continue
            piece = draw(th, p, layer="head%d" % k, tf=th.fall_tf(k, d["drop"][k], hs[k]))
            over(f, piece)
        if i >= 1:
            f = tint_toward(f, th.cols["wither"], 0.18 * i, mask=~_outline_mask(f))
        th.death_fx(f, 3 + i, p)
        out.append(f)
    # body slumps and withers, heads lie on the ground
    final_heads = []
    for k in range(3):
        hp = HP(th, k, *[(0, 8, 0.9, 0.2, 0.0), (3, 12, 1.1, 0.2, 0.0), (-2, 12, 1.1, 0.2, 0.0)][k])
        hp.cut, hp.stub = (0.42, 0.4, 0.4)[k], 0.16
        final_heads.append(hp)
    slump = []
    for i, (sy, dead) in enumerate(((0.94, 0.75), (0.86, 1.0))):
        p = HPose(heads=final_heads, sacs=0.1, crown=0.0, glow=0.1, cracks=2, burst=(0, 1, 2, 3), wilt=1.0,
                  dead=dead, scale=(1.0 + (1 - sy) * 0.4, sy))
        f = draw(th, p, layer="body")
        p2 = HPose(**{**p.__dict__, "scale": (1.0, 1.0)})
        for k in range(3):
            over(f, draw(th, p2, layer="head%d" % k, tf=th.fall_tf(k, 1.0, final_heads[k])))
        th.death_fx(f, 6 + i, p)
        slump.append(f)
    out += slump
    base = slump[-1]
    for k, t in enumerate((0.2, 0.38, 0.56, 0.74, 0.92)):
        f = FX.dissolve(base, min(1.0, t), seed=th.seed + 41, colors=th.cols["dissolve"], rise=26, drift=6,
                        top_first=True, life=0.4, particle_rate=0.16, bounds=(0, 0, W, H - 1))
        FX.under(f, th.residue(min(1.0, 0.3 + t)))
        out.append(f)
    last = th.residue(1.0)
    motes(last, 64, 92, 7, 6, 26, th.cols["dissolve"][:3], seed=th.seed + 77, arc=(-2.8, -0.3))
    out.append(last)
    return out


def build_sheet(th):
    s = Sheet("sprites/enemies/%s.png" % th.id, W, H, PIVOT)
    s.add("idle", idle(th), fps=6, loop=True)
    s.add("roar", roar(th), durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)
    s.add("hurt", hurt(th), durations=[0.08, 0.14], loop=False)
    s.add("death", death(th), durations=[0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14,
                                         0.16, 0.3], loop=False)
    s.add("telegraph_bite", telegraph_bite(th), durations=[0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("bite", bite(th), durations=[0.05, 0.05, 0.16, 0.12, 0.14], loop=False, events=[(2, "hit")])
    s.add("telegraph_spores", telegraph_spores(th), durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("spores", spores(th), durations=[0.06, 0.14, 0.12, 0.14], loop=False, events=[(1, "fire")])
    s.add("telegraph_vines", telegraph_vines(th), durations=[0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("vines", vines(th), durations=[0.05, 0.2, 0.14, 0.14], loop=False, events=[(1, "impact")])
    s.add("summon", summon(th), durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)
    s.save("animations/%s.json" % th.id)
    return s
