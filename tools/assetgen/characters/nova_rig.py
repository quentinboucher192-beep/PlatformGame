"""Nova puppet: a posable side-view (and back-view) rig rasterised at 1x into a
32x32 frame.  A `Pose` holds joint targets in frame coordinates for a
right-facing, unrotated body; `draw(pose)` applies the global transform
(squash / rotation / mirror), solves the limbs with 2-bone IK and draws the
parts back to front."""
import math

import numpy as np

import materials as M
from raster import Canvas, ik2

W = H = 32


def _snap_i(p):
    return (float(round(p[0])), float(round(p[1])))


def _snap_h(p):
    return (math.floor(p[0]) + 0.5, math.floor(p[1]) + 0.5)


class Pose:
    def __init__(self, **kw):
        self.hip = (16.0, 26.0)
        self.lean = 0.0          # radians, + = top leans forward (right)
        self.bob = 0.0           # upper body vertical offset (px, + = down)
        self.head = (0.0, 0.0)   # extra helmet offset
        self.tilt = 0.0          # visor tilt (+ = looking down)
        self.foot_f = (18.0, 30.0)
        self.foot_b = (15.0, 30.0)
        self.toe_f = 0.0         # boot angle (+ = toe down)
        self.toe_b = 0.0
        self.knee_f = 1.0
        self.knee_b = 1.0
        self.hand_f = (19.0, 25.0)
        self.hand_b = (13.0, 25.0)
        self.elbow_f = -1.0
        self.elbow_b = -1.0
        self.scarf_ang = 2.85    # world angle of the tail (pi = straight back)
        self.scarf_curl = 0.07   # droop towards hanging down per segment
        self.scarf_amp = 0.22
        self.scarf_phase = 0.0
        self.scarf_len = 6
        self.scarf_seg = 1.6
        self.rot = 0.0
        self.rot_c = (16.0, 22.0)
        self.facing = 1
        self.offset = (0.0, 0.0)
        self.squash = 1.0        # vertical scale about the feet
        self.blade = None        # world angle of the energy blade (from front hand)
        self.blade_len = 9.0
        self.hand_glow = 0       # cast glow radius at the front hand
        self.fist = False
        self.visor = None        # override visor material
        self.pack = True
        self.__dict__.update(kw)

    def copy(self, **kw):
        p = Pose()
        p.__dict__.update(self.__dict__)
        p.__dict__.update(kw)
        return p


class Xf:
    """squash about the feet -> rotate about rot_c -> mirror -> offset."""

    def __init__(self, pose):
        self.p = pose

    def __call__(self, q):
        p = self.p
        x, y = q
        if p.squash != 1.0:
            sx = 1.0 / math.sqrt(p.squash)
            x = 16.0 + (x - 16.0) * sx
            y = 32.0 + (y - 32.0) * p.squash
        if p.rot:
            cx, cy = p.rot_c
            c, s = math.cos(p.rot), math.sin(p.rot)
            dx, dy = x - cx, y - cy
            x, y = cx + dx * c - dy * s, cy + dx * s + dy * c
        if p.facing < 0:
            x = 32.0 - x
        return (x + p.offset[0], y + p.offset[1])

    def ang(self, a):
        a = a + self.p.rot
        if self.p.facing < 0:
            a = math.pi - a
        return a

    def ell_ang(self, a):
        a = a + self.p.rot
        if self.p.facing < 0:
            a = -a
        return a

    def world_ang(self, a):
        """A world-space direction given for a right-facing body (no rotation)."""
        return math.pi - a if self.p.facing < 0 else a


def scarf_points(start, ang, n, seg, droop, amp, phase, wave_sign=1.0):
    """Chain of points: starts along `ang`, relaxes towards hanging straight
    down by `droop` per segment, plus a travelling flutter wave."""
    pts = [start]
    x, y = start
    base = ang
    for k in range(n):
        g = 1.0 - (1.0 - droop) ** k
        a = base + (math.pi / 2 - base) * g
        a += wave_sign * amp * math.sin(phase - 0.9 * k) * (0.3 + k / n)
        x += math.cos(a) * seg
        y += math.sin(a) * seg
        pts.append((x, y))
    return pts


def draw(p, back_view=False):
    if back_view:
        return draw_back(p)
    cv = Canvas(W, H)
    X = Xf(p)
    lean = p.lean
    up = (math.sin(lean), -math.cos(lean))
    rt = (math.cos(lean), math.sin(lean))
    hip = p.hip
    bob = p.bob

    def L(dx, du, b=True):
        bb = bob if b else 0.0
        return (hip[0] + rt[0] * dx + up[0] * du, hip[1] + rt[1] * dx + up[1] * du + bb)

    torso_c = L(0.4, 3.3)
    neck = L(0.4, 6.4)
    helm = (neck[0] + up[0] * 5.0 + p.head[0] + rt[0] * 0.3, neck[1] + up[1] * 5.0 + p.head[1])
    sh_f = L(1.2, 5.0)
    sh_b = L(-0.4, 5.0)
    hip_f = L(1.6, 0.6, False)
    hip_b = L(-0.6, 0.6, False)

    # transform everything to world
    T = X
    w_helm = _snap_h(T(helm))
    w_torso = T(torso_c)
    w_neck = T(neck)
    w_sh_f, w_sh_b = _snap_i(T(sh_f)), _snap_i(T(sh_b))
    w_hip_f, w_hip_b = _snap_i(T(hip_f)), _snap_i(T(hip_b))
    w_foot_f, w_foot_b = _snap_i(T(p.foot_f)), _snap_i(T(p.foot_b))
    w_hand_f, w_hand_b = _snap_i(T(p.hand_f)), _snap_i(T(p.hand_b))
    fsgn = 1.0 if p.facing > 0 else -1.0

    LEG1, LEG2 = 2.4, 2.4
    ARM1, ARM2 = 2.8, 2.8

    def leg(hipj, foot, toe, kneesgn, bias, flat=None):
        # knee bend direction follows facing and rotation
        knee = ik2(hipj, foot, LEG1, LEG2, kneesgn * fsgn)
        cv.capsule(hipj, knee, 1.25, 1.1, M.SUIT_M, bias=bias, flat=flat)
        cv.capsule(knee, foot, 1.1, 1.05, M.SUIT_M, bias=bias, flat=flat)
        # boot: sole one pixel below the ankle, pointing along the toe angle
        ta = X.ang(toe)
        bdir = (math.cos(ta), math.sin(ta))
        down = (-bdir[1] * fsgn, bdir[0] * fsgn)
        heel = (foot[0] - bdir[0] * 0.5 + down[0], foot[1] - bdir[1] * 0.5 + down[1])
        toe_p = (heel[0] + bdir[0] * 2.2, heel[1] + bdir[1] * 2.2)
        cv.capsule(heel, toe_p, 1.0, 1.0, M.BOOT, bias=bias)

    def arm(sh, hand, esgn, bias, glove=True, flat=None):
        elbow = ik2(sh, hand, ARM1, ARM2, esgn * fsgn)
        elbow = _snap_i(elbow)
        cv.capsule(sh, elbow, 1.1, 1.0, M.SUIT_M, bias=bias, flat=flat)
        cv.capsule(elbow, hand, 1.0, 0.95, M.SUIT_M, bias=bias, flat=flat)
        if glove:
            cv.disc(hand, 1.2 if p.fist else 1.1, M.GLOVE, bias=bias)
        return elbow

    # ---- back arm
    cv.part()
    arm(w_sh_b, w_hand_b, p.elbow_b, -0.16, flat=0.45)
    # ---- back leg
    cv.part()
    leg(w_hip_b, w_foot_b, p.toe_b, p.knee_b, -0.14, flat=0.47)

    # ---- scarf tail (world space flow): a tapered ribbon with a notched end
    if p.scarf_len >= 2:
        cv.part()
        st = T(L(-2.0, 6.4))
        sa = X.world_ang(p.scarf_ang)
        pts = scarf_points(st, sa, p.scarf_len, p.scarf_seg, p.scarf_curl, p.scarf_amp, p.scarf_phase,
                           1.0 if p.facing > 0 else -1.0)
        n = len(pts)
        radii = [1.3 - 0.55 * i / (n - 1) for i in range(n)]
        cv.chain(pts, radii, M.SCARF_M)
        # short second tail
        if p.scarf_len >= 4:
            pts2 = scarf_points(st, sa + 0.35 * (1 if p.facing > 0 else -1) * (-1 if math.sin(sa) < -0.3 else 1),
                                max(2, p.scarf_len - 3), p.scarf_seg, p.scarf_curl * 1.4, p.scarf_amp,
                                p.scarf_phase + 1.3, 1.0 if p.facing > 0 else -1.0)
            m2 = len(pts2)
            cv.chain(pts2, [1.0 - 0.35 * i / max(1, m2 - 1) for i in range(m2)], M.SCARF_M, bias=-0.12)

    # ---- backpack
    if p.pack:
        cv.part()
        corners = [L(-6.0, 1.0), L(-2.0, 1.0), L(-2.0, 6.2), L(-5.2, 6.2), L(-6.0, 5.4)]
        cv.poly([T(c) for c in corners], M.SUIT_DARK, bevel=1.1, base=0.5)
        core = [L(-6.0, 2.0), L(-5.0, 2.0), L(-5.0, 4.6), L(-6.0, 4.6)]
        cv.poly([T(c) for c in core], M.CORE, flat=0.6, decal=True)
        cv.px_set(*T(L(-5.5, 3.8)), M.CORE, inten=0.95, decal=True)

    # ---- torso
    cv.part()
    cv.capsule(T(L(0.4, 1.7)), T(L(0.4, 4.4)), 3.05, 3.0, M.SUIT_M, bias=0.08)
    # belt
    b0, b1 = T(L(-2.6, 1.4)), T(L(3.4, 1.4))
    cv.capsule(b0, b1, 0.55, 0.55, M.SUIT_DARK, flat=0.45, decal=True)
    cv.px_set(*T(L(2.6, 1.4)), M.CORE, inten=0.9, decal=True)

    # ---- front leg
    cv.part()
    leg(w_hip_f, w_foot_f, p.toe_f, p.knee_f, 0.08)

    # ---- helmet
    cv.part()
    cv.disc(w_helm, 5.45, M.SUIT_M, bias=0.03)
    # visor
    vang = X.ell_ang(p.tilt)
    voff = (2.0, 0.7)
    c, s = math.cos(p.tilt), math.sin(p.tilt)
    vo = (voff[0] * c - voff[1] * s, voff[0] * s + voff[1] * c)
    # rotate/mirror the visor offset like the body
    vw = X((helm[0] + vo[0], helm[1] + vo[1]))
    hw = X(helm)
    vc = (w_helm[0] + vw[0] - hw[0], w_helm[1] + vw[1] - hw[1])
    hc = w_helm

    def inside_helm(px, py, r=4.35):
        return (px - hc[0]) ** 2 + (py - hc[1]) ** 2 <= r * r
    vmat = p.visor or M.VISOR_M
    cv.ellipse(vc, 3.6, 3.0, vmat, ang=vang, decal=True, within=inside_helm)
    # antenna
    a0 = X((helm[0] - 2.6 * c + 4.4 * s, helm[1] - 4.6 * c - 2.6 * s))
    a1 = X((helm[0] - 3.6 * c + 6.9 * s, helm[1] - 6.6 * c - 3.6 * s))
    a0 = (w_helm[0] + a0[0] - hw[0], w_helm[1] + a0[1] - hw[1])
    a1 = (w_helm[0] + a1[0] - hw[0], w_helm[1] + a1[1] - hw[1])
    cv.part(line=False)
    cv.line(a0, a1, M.SUIT_DARK, inten=0.3)
    cv.px_set(a1[0], a1[1], M.CORE, inten=0.95)

    # ---- scarf collar
    cv.part()
    c0, c1 = T(L(-2.6, 6.3)), T(L(2.6, 6.0))
    cv.capsule(c0, c1, 1.15, 1.05, M.SCARF_M)

    # ---- front arm + blade
    cv.part(line=3)
    arm(w_sh_f, w_hand_f, p.elbow_f, 0.04, flat=0.88)
    if p.blade is not None:
        ba = X.world_ang(p.blade)
        d = (math.cos(ba), math.sin(ba))
        hx, hy = w_hand_f
        cv.part(line=False)
        cv.capsule((hx + d[0] * 1.2, hy + d[1] * 1.2), (hx + d[0] * p.blade_len, hy + d[1] * p.blade_len),
                   0.95, 0.6, M.BLADE)
        cv.capsule((hx + d[0] * 1.6, hy + d[1] * 1.6), (hx + d[0] * (p.blade_len - 1.5), hy + d[1] * (p.blade_len - 1.5)),
                   0.4, 0.3, M.BLADE, flat=1.0)
        cv.part()
        cv.disc((hx, hy), 1.2, M.GLOVE)
    if p.hand_glow:
        cv.part(line=False)
        cv.disc(w_hand_f, p.hand_glow, M.GLOW_CYAN_NO, flat=None)
    return cv.render()


def draw_back(p):
    """Back view used by the climb clip (symmetric about x = 16)."""
    cv = Canvas(W, H)
    hip = p.hip
    bob = p.bob
    # legs (behind torso)
    for side, foot in ((-1, p.foot_b), (1, p.foot_f)):
        cv.part()
        hj = (16.0 + side * 1.5, hip[1] - 0.5)
        knee = ik2(hj, foot, 2.8, 2.8, side * 1.0)
        cv.capsule(hj, knee, 1.25, 1.1, M.SUIT_M, bias=-0.04)
        cv.capsule(knee, foot, 1.1, 1.05, M.SUIT_M, bias=-0.04)
        cv.capsule((foot[0], foot[1] + 0.5), (foot[0], foot[1] + 1.2), 1.15, 1.1, M.BOOT)
    # torso
    cv.part()
    cv.ellipse((16.0, hip[1] - 3.2 + bob), 3.6, 3.7, M.SUIT_M, bias=0.04)
    # scarf hanging down the back, drifting to one side
    cv.part()
    st = (16.0, hip[1] - 6.6 + bob)
    pts = scarf_points(st, p.scarf_ang, p.scarf_len, 1.35, p.scarf_curl, p.scarf_amp, p.scarf_phase)
    n = len(pts)
    cv.chain(pts, [1.15 - 0.45 * i / (n - 1) for i in range(n)], M.SCARF_M)
    # backpack centred on the back
    cv.part()
    by = hip[1] - 3.4 + bob
    cv.poly([(12.6, by - 3.2), (19.4, by - 3.2), (19.4, by + 2.4), (18.6, by + 3.2), (13.4, by + 3.2), (12.6, by + 2.4)],
            M.SUIT_DARK, bevel=1.1, base=0.6)
    cv.poly([(14.0, by - 1.6), (18.0, by - 1.6), (18.0, by + 1.6), (14.0, by + 1.6)], M.STONE_DARK, flat=0.25, decal=True)
    cv.poly([(15.0, by - 1.0), (17.0, by - 1.0), (17.0, by + 1.0), (15.0, by + 1.0)], M.CORE, flat=0.6, decal=True)
    cv.px_set(15, int(by - 1), M.CORE, inten=0.95, decal=True)
    # helmet (back)
    cv.part()
    hc = (16.0, hip[1] - 11.4 + bob)
    cv.disc(hc, 5.45, M.SUIT_M, bias=0.10)
    cv.capsule((12.0, hc[1] + 3.0), (20.0, hc[1] + 3.0), 0.6, 0.6, M.SUIT_DARK, flat=0.3, decal=True)
    cv.part(line=False)
    cv.line((13.5, hc[1] - 4.5), (12.5, hc[1] - 7.0), M.SUIT_DARK, inten=0.3)
    cv.px_set(12.5, hc[1] - 7.0, M.CORE, inten=0.95)
    # collar
    cv.part()
    cv.capsule((12.6, hc[1] + 5.3), (19.4, hc[1] + 5.3), 1.1, 1.1, M.SCARF_M)
    # arms
    for side, hand in ((-1, p.hand_b), (1, p.hand_f)):
        cv.part(line=3)
        sh = (16.0 + side * 3.0, hip[1] - 5.0 + bob)
        elbow = ik2(sh, hand, 3.2, 3.2, -side * 1.0)
        cv.capsule(sh, elbow, 1.15, 1.05, M.SUIT_M)
        cv.capsule(elbow, hand, 1.05, 1.0, M.SUIT_M)
        cv.disc(hand, 1.15, M.GLOVE)
    return cv.render()
