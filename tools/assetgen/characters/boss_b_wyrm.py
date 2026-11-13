"""Wyrm rig for the BOSSES-B sheets (128x96 frames, collision 56x40, pivot [64, 84]).

A flying segmented serpent: the spine is a smoothed polyline (neck joint + control
points), resampled by arc length and displaced by a travelling wave that starts
behind the head (the head leads, the coils follow).  The body is drawn tail -> head
as short overlapping segments (one part each, so the rasteriser draws a contour
between segments), with a glowing belly band on the ventral side and dorsal features
per segment.  Two species share the rig but not their parts:

* Ignivore (fire): rust-red scales, molten belly, flame mane on the neck and skull,
  basalt dorsal spikes, swept-back obsidian horns, flame fins, flaming tail plume.
* Cryowyrm (ice):  ice-blue scales, pale frost belly, tall crystal spines along the
  whole back, branching crystal antlers, frost mane + long whiskers, crystal tail blade.
"""
import math

import numpy as np

import boss_b_palette as C
from boss_b_knight import crystal, spike
from raster import TCanvas

W, H = 128, 96
PIVOT = [64, 84]
TAU = 2 * math.pi
HS = 1.4            # head scale

# Spine shapes: neck joint + 14 control points toward the tail (absolute frame coords).  All presets
# have the same number of points so poses can be blended.  S-shaped body with a spiralled tail, back up.
SHAPES = {
    "idle": ((88, 50), [(83, 60), (76, 70), (66, 76), (55, 74), (47, 66), (42, 56), (36, 48), (28, 44), (19, 46),
                        (13, 53), (13, 61), (19, 65), (25, 62), (25, 56)]),
    "rear": ((85, 40), [(84, 50), (82, 60), (77, 70), (68, 77), (57, 77), (48, 70), (42, 59), (35, 51), (26, 48),
                        (18, 51), (13, 58), (15, 65), (21, 68), (26, 64)]),
    "dive": ((90, 66), [(83, 62), (76, 58), (69, 54), (62, 50), (55, 46), (48, 42), (41, 39), (34, 37), (27, 36),
                        (21, 35), (16, 33), (12, 30), (10, 26), (11, 22)]),
    "coil": ((80, 48), [(76, 58), (71, 68), (62, 74), (52, 72), (46, 64), (44, 55), (40, 47), (33, 43), (25, 45),
                        (20, 51), (21, 58), (26, 61), (31, 58), (30, 53)]),
}


def shape(name, t=1.0, other="idle", dx=0.0, dy=0.0):
    """Blend between two spine presets (t = 1 -> `name`), optionally shifted."""
    h0, c0 = SHAPES[other]
    h1, c1 = SHAPES[name]
    hd = (h0[0] + (h1[0] - h0[0]) * t + dx, h0[1] + (h1[1] - h0[1]) * t + dy)
    cs = [(a[0] + (b[0] - a[0]) * t + dx, a[1] + (b[1] - a[1]) * t + dy) for a, b in zip(c0, c1)]
    return hd, cs


class WPose:
    def __init__(self, **kw):
        self.head, self.ctrl = shape("idle")   # neck joint (back of the skull) + spine control points
        self.ang = 0.1             # head facing angle (0 = right, + = down)
        self.amp = 2.2             # travelling ripple amplitude (px) away from the head
        self.wl = 46.0             # ripple wavelength (px)
        self.phase = 0.0           # ripple phase 0..1 (moves head -> tail)
        self.jaw = 0.0
        self.eye = 0.6
        self.belly = 0.6           # belly glow
        self.pulse = None          # (u_centre 0..1, width, strength): glow travelling along the belly
        self.mane = 0.0            # mane / spine flare 0..1.5
        self.sweep = 0.0           # mane / fins swept back (speed) 0..1
        self.t = 0.0               # flicker phase
        self.dead = 0.0
        self.cracks = 0
        self.rot = 0.0
        self.rot_c = (64.0, 60.0)
        self.offset = (0.0, 0.0)
        self.rscale = 1.0
        self.__dict__.update(kw)

    def copy(self, **kw):
        d = dict(self.__dict__)
        d.update(kw)
        return WPose(**d)


def chaikin(pts, it=3):
    for _ in range(it):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            out.append((a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25))
            out.append((a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75))
        out.append(pts[-1])
        pts = out
    return pts


STEP = 2.0
SEG = 7.0


def _sm(v):
    v = np.clip(v, 0, 1)
    return v * v * (3 - 2 * v)


def spine(p):
    """Resampled spine: arrays s, x, y, tx, ty (tangent head -> tail), total length."""
    pts = np.array(chaikin([tuple(p.head)] + [tuple(q) for q in p.ctrl], 3), np.float64)
    d = np.hypot(np.diff(pts[:, 0]), np.diff(pts[:, 1]))
    cs = np.concatenate([[0], np.cumsum(d)])
    L = cs[-1]
    s = np.arange(0, L, STEP)
    x = np.interp(s, cs, pts[:, 0])
    y = np.interp(s, cs, pts[:, 1])
    tx, ty = np.gradient(x), np.gradient(y)
    n = np.hypot(tx, ty) + 1e-6
    tx, ty = tx / n, ty / n
    # travelling ripple, zero at the neck joint so the head leads
    disp = p.amp * _sm((s - 4) / 26.0) * np.sin(TAU * (s / p.wl - p.phase))
    disp *= 1 - 0.5 * _sm((s - (L - 30)) / 30.0)
    x = x + (-ty) * disp
    y = y + tx * disp
    tx, ty = np.gradient(x), np.gradient(y)
    n = np.hypot(tx, ty) + 1e-6
    return s, x, y, tx / n, ty / n, L


def radius(u):
    return np.interp(u, [0, 0.08, 0.26, 0.5, 0.76, 1.0], [5.4, 6.2, 7.2, 6.6, 4.2, 1.4])


def _flame(cv, base, d, perp, ln, wd, bend, t):
    """Three-tone flame tongue from `base` along direction d, curling by `bend`."""
    for k, (scale, inten) in enumerate(((1.0, 0.2), (0.74, 0.5), (0.45, 0.88))):
        l2, w2 = ln * scale, wd * scale
        mid = (base[0] + d[0] * l2 * 0.5 + perp[0] * bend * 0.6, base[1] + d[1] * l2 * 0.5 + perp[1] * bend * 0.6)
        tip = (base[0] + d[0] * l2 + perp[0] * bend * 1.4, base[1] + d[1] * l2 + perp[1] * bend * 1.4)
        cv.part(line=False)
        cv.tpoly([(base[0] + perp[0] * w2, base[1] + perp[1] * w2), (mid[0] + perp[0] * w2 * 0.6,
                                                                    mid[1] + perp[1] * w2 * 0.6), tip,
                  (mid[0] - perp[0] * w2 * 0.5, mid[1] - perp[1] * w2 * 0.5),
                  (base[0] - perp[0] * w2, base[1] - perp[1] * w2)], C.FLAME, flat=inten)


def draw(p, kind):
    fire = kind == "fire"
    cv = TCanvas(W, H, rot=p.rot, center=p.rot_c, offset=p.offset)
    s, x, y, tx, ty, L = spine(p)
    n = len(s)
    u = s / L
    r = radius(u) * p.rscale * (1.05 if fire else 0.9)
    nvx, nvy = ty, -tx                      # ventral normal
    alive = p.dead < 0.7
    scale = C.SCALE_F if fire else C.SCALE_I
    scale_dk = C.SCALE_F_DK if fire else C.SCALE_I_DK
    belly = (C.BELLY_F if fire else C.BELLY_I) if alive else (C.DEAD_F if fire else C.DEAD_I)
    line_col = C.SEAM_DIM if fire else C.ICE_DK
    fl = p.t

    def glow_at(uu):
        g = p.belly
        if p.pulse is not None:
            uc, wd, st = p.pulse
            g += st * math.exp(-((uu - uc) / max(wd, 1e-3)) ** 2)
        return g if alive else 0.0

    # ----------------------------------------------------------- tail end
    ex, ey = x[-1], y[-1]
    etx, ety = tx[-1], ty[-1]
    if fire:
        if alive:
            for k, a in enumerate((-0.5, 0.0, 0.5)):
                ca, sa = math.cos(a), math.sin(a)
                d = (etx * ca - ety * sa, etx * sa + ety * ca)
                d = (d[0] - 0.4 * p.sweep, d[1] - 0.25)
                dn = math.hypot(*d)
                d = (d[0] / dn, d[1] / dn)
                ln = (8 + 3 * math.sin((fl + k * 0.3) * TAU)) * (1.0 if k == 1 else 0.75)
                _flame(cv, (ex, ey), d, (-d[1], d[0]), ln, 2.2, 1.5 * math.sin((fl + k * 0.37) * TAU), fl)
    else:
        cv.part()
        bx, by = ex - etx * 1.0, ey - ety * 1.0
        crystal(cv, (bx, by), math.atan2(ety, etx) - 0.15, 10.0, 2.4, C.CRYST if alive else C.ICE_DK)
        cv.part()
        ex_n, ey_n = nvx[-1], nvy[-1]
        crystal(cv, (bx - ex_n * 0.5, by - ey_n * 0.5), math.atan2(ety - ey_n * 1.2, etx - ex_n * 1.2), 8.0, 2.0,
                C.CRYST if alive else C.ICE_DK)
        crystal(cv, (bx + ex_n * 0.5, by + ey_n * 0.5), math.atan2(ety + ey_n * 0.9, etx + ex_n * 0.9), 6.0, 1.8,
                C.ICE_M)

    # ------------------------------------------------- segments tail -> head
    seg_idx = []
    k0 = n - 1
    per = int(SEG / STEP)
    while k0 > 0:
        k1 = max(0, k0 - per)
        seg_idx.append((k1, k0))
        k0 = k1
    for si, (a, b) in enumerate(seg_idx):
        km = (a + b) // 2
        uc = u[km]
        cx, cy = x[km], y[km]
        dvx, dvy = -nvx[km], -nvy[km]            # dorsal direction
        bkx, bky = tx[km], ty[km]                # toward the tail
        rr = r[km]
        # dorsal features (behind the segment)
        if fire:
            if uc < 0.34 and alive:
                flick = math.sin((fl + uc * 3.1) * TAU)
                d = (dvx * 0.8 + bkx * (0.65 + 0.6 * p.sweep), dvy * 0.8 + bky * (0.65 + 0.6 * p.sweep))
                dn = math.hypot(*d)
                d = (d[0] / dn, d[1] / dn)
                ln = (12.0 + 5.0 * p.mane + 2.5 * flick) * (1 - 0.6 * uc / 0.34)
                base = (cx + dvx * rr * 0.5, cy + dvy * rr * 0.5)
                _flame(cv, base, d, (-d[1], d[0]), ln, 3.4, 2.0 * flick, fl)
            elif 0.34 <= uc < 0.88:
                ang = math.atan2(dvy * 0.8 + bky * 0.6, dvx * 0.8 + bkx * 0.6)
                cv.part()
                spike(cv, (cx + dvx * rr * 0.6, cy + dvy * rr * 0.6), ang, (2.0 + 3.5 * (1 - uc)) *
                      (1 + 0.4 * p.mane), 1.8, C.BAS_DK if alive else C.DEAD_F, base_i=0.6)
        else:
            if 0.06 < uc < 0.93:
                ang = math.atan2(dvy * 0.85 + bky * (0.45 + 0.4 * p.sweep), dvx * 0.85 + bkx * (0.45 + 0.4 * p.sweep))
                ln = (4.0 + 11.0 * (1 - uc) ** 1.2) * (1.0 if si % 2 == 0 else 0.6) * (1 + 0.35 * p.mane)
                ln *= (1 - 0.5 * p.dead)
                cv.part()
                crystal(cv, (cx + dvx * rr * 0.55, cy + dvy * rr * 0.55), ang, ln, 1.6 + 0.6 * (1 - uc),
                        C.CRYST if alive else C.ICE_DK)
        # flame fins (ignivore) / frost frills (cryowyrm) near the front of the body
        vnx, vny = nvx[km], nvy[km]
        if si == len(seg_idx) - 5:
            fin_ang = math.atan2(vny * 0.5 + bky, vnx * 0.5 + bkx)
            if fire and alive:
                for j, da in enumerate((-0.25, 0.25)):
                    d = (math.cos(fin_ang + da), math.sin(fin_ang + da))
                    _flame(cv, (cx + vnx * rr * 0.3, cy + vny * rr * 0.3), d, (-d[1], d[0]),
                           11 + 2 * math.sin((fl + j * 0.4) * TAU) + 3 * p.sweep, 3.0, 1.5, fl)
            elif not fire:
                cv.part()
                fa = fin_ang
                q0 = (cx + vnx * rr * 0.4, cy + vny * rr * 0.4)
                pts = [q0]
                for j, (dd, ofs) in enumerate(((13, -0.35), (10, 0.05), (12, 0.4))):
                    pts.append((q0[0] + math.cos(fa + ofs) * dd, q0[1] + math.sin(fa + ofs) * dd))
                pts.append((q0[0] + math.cos(fa + 0.7) * 4, q0[1] + math.sin(fa + 0.7) * 4))
                cv.tpoly(pts, C.ICE_M if alive else C.ICE_DK, bevel=1.2, base=0.62)
                for (dd, ofs) in ((13, -0.35), (12, 0.4)):
                    cv.tline(q0, (q0[0] + math.cos(fa + ofs) * dd * 0.9, q0[1] + math.sin(fa + ofs) * dd * 0.9),
                             C.ICE_DK, inten=0.4, decal=True)
        # body segment
        cv.part()
        idx = list(range(a, min(n, b + 2)))
        pts = [(x[k], y[k]) for k in idx]
        rad = [r[k] for k in idx]
        if len(pts) == 1:
            pts.append(pts[0])
            rad.append(rad[0])
        cv.tchain(pts, rad, scale)
        # dorsal ridge highlight on top of the segment
        cv.tline((x[b] + dvx * rr * 0.55, y[b] + dvy * rr * 0.55), (x[a] + dvx * rr * 0.55, y[a] + dvy * rr * 0.55),
                 scale, inten=0.95, decal=True)
        # belly band
        g = glow_at(uc)
        bp = [(x[k] + nvx[k] * r[k] * 0.55, y[k] + nvy[k] * r[k] * 0.55) for k in idx]
        br = [r[k] * 0.44 for k in idx]
        if len(bp) == 1:
            bp.append(bp[0])
            br.append(br[0])
        cv.tchain(bp, br, belly, flat=0.28 + 0.32 * min(1.5, g), decal=True)
        cp = [(x[k] + nvx[k] * r[k] * 0.58, y[k] + nvy[k] * r[k] * 0.58) for k in idx]
        if alive and g > 0.15:
            cv.tchain(cp if len(cp) > 1 else cp * 2, [max(0.6, r[k] * 0.17) for k in idx] if len(idx) > 1 else
                      [max(0.6, r[idx[0]] * 0.17)] * 2, belly, flat=0.5 + 0.3 * min(1.5, g), decal=True)
        # plate line across the belly at the segment boundary
        cv.tline((x[a] + nvx[a] * r[a] * 0.05, y[a] + nvy[a] * r[a] * 0.05),
                 (x[a] + nvx[a] * r[a] * 0.95, y[a] + nvy[a] * r[a] * 0.95), line_col, inten=0.2, decal=True)
        if p.cracks and si % 3 == 1:
            cv.part(line=False)
            cm = (C.SEAM if fire else C.FROST_GLOW) if alive else (C.SEAM_DEAD if fire else C.SLOT_G)
            cv.tline((cx + dvx * rr * 0.6, cy + dvy * rr * 0.6), (cx - bkx * 2 + dvx * rr * 0.1,
                                                                  cy - bky * 2 + dvy * rr * 0.1), cm, inten=0.85,
                     decal=True)

    # ------------------------------------------------------------------ head
    hx, hy = p.head
    ca, sa = math.cos(p.ang), math.sin(p.ang)

    def HL(lx, ly):
        lx, ly = lx * HS, ly * HS
        return (hx + lx * ca - ly * sa, hy + lx * sa + ly * ca)

    ja = p.jaw * 0.62
    jc, js = math.cos(ja), math.sin(ja)
    hinge = (3.0, 1.5)

    def JL(lx, ly):
        dx, dy = lx - hinge[0], ly - hinge[1]
        return HL(hinge[0] + dx * jc - dy * js, hinge[1] + dx * js + dy * jc)

    if fire:
        _head_fire(cv, p, HL, JL, alive, fl)
    else:
        _head_ice(cv, p, HL, JL, alive, fl)
    return cv.render()


def _jaws(cv, p, HL, JL, upper, lower, mat, mat_dk, alive, fire, teeth_mat, fangs=1.0):
    ue = upper[5][0]                       # snout tip x
    le = lower[1][0]
    if p.jaw > 0.05:
        # maw interior: dark gullet, glowing throat at the back
        cv.part(line=False)
        cv.tpoly([HL(1.5, 1.0), HL(ue - 1.5, 0.8), JL(le - 1.5, 1.8), JL(1.5, 1.8)], C.SLOT_P if fire else C.SLOT_G,
                 flat=0.2)
        if alive:
            th = C.LAVA_M if fire else C.FROST_GLOW
            g = min(1.4, p.eye)
            cv.tpoly([HL(1.5, 1.2), HL(4 + 7 * g, 1.0), JL(3 + 6 * g, 1.8), JL(1.5, 1.8)], th, flat=0.5 + 0.3 * g)
            cv.tpoly([HL(1.5, 1.3), HL(2.5 + 4 * g, 1.2), JL(2 + 3 * g, 1.7), JL(1.5, 1.7)], th, flat=0.95)
        cv.part()
        for xx in (8.0, 11.0, 14.0, ue - 4.0):
            cv.tpoly([HL(xx - 0.8, 0.9), HL(xx + 0.8, 0.9), HL(xx + 0.1, 0.9 + 1.7 * fangs)], teeth_mat, flat=0.8)
        for xx in (7.0, 10.0, 13.0, le - 4.0):
            cv.tpoly([JL(xx - 0.8, 1.8), JL(xx + 0.8, 1.8), JL(xx - 0.1, 1.8 - 1.5 * fangs)], teeth_mat, flat=0.7)
    cv.part()
    cv.tpoly([JL(*q) for q in lower], mat_dk, bevel=1.4, base=0.62)
    cv.part()
    cv.tpoly([HL(*q) for q in upper], mat, bevel=1.8, base=0.66)
    # glowing mouth seam when closed (inner fire / frost)
    cv.part(line=False)
    if alive:
        sm = C.SEAM if fire else C.FROST_GLOW
        cv.tline(HL(3, 1.6), HL(ue - 3, 1.2), sm, inten=0.35 + 0.35 * min(1.0, p.belly), decal=True)
    # snout ridge highlight + nostril
    cv.tline(HL(4, upper[2][1] + 0.6), HL(ue - 2.5, upper[4][1] + 0.4), mat, inten=0.97, decal=True)


def _head_fire(cv, p, HL, JL, alive, fl):
    # head mane: flames streaming back from the skull
    if alive:
        for k, (bx, by, a, ln) in enumerate(((-2, -5.5, -2.45, 12), (-4, -2.5, -2.8, 14), (-4.5, 1.5, 3.05, 11),
                                             (1, -7, -2.15, 8), (-3, 3.5, 2.6, 8))):
            fk = math.sin((fl + k * 0.29) * TAU)
            aa = a + p.ang - 0.25 * p.sweep * (1 if a < 0 else -1)
            d = (math.cos(aa), math.sin(aa))
            _flame(cv, HL(bx, by), d, (-d[1], d[0]), (ln + 3 * p.mane + 2 * fk) * HS, 3.2, 2.0 * fk, fl)
    # far horn
    cv.part()
    hn = [(3.5, -6.5), (-2, -11), (-8, -14), (-14, -15)]
    cv.tchain([HL(*q) for q in hn], [2.2 * HS, 1.8 * HS, 1.2 * HS, 0.5 * HS], C.OBS_DK)
    upper = [(-5, -3), (-1, -7.5), (6, -8.5), (11, -7), (17, -4.2), (21.5, -1.5), (21, 0.6), (17, 1.6), (5, 2.2),
             (-4, 4)]
    lower = [(2, 1.4), (17.5, 1.6), (17, 3.8), (9, 5.4), (1.5, 5.2)]
    _jaws(cv, p, HL, JL, upper, lower, C.SCALE_F, C.SCALE_F_DK, alive, True, C.TEETH)
    # brow plate + burning eye
    cv.part()
    cv.tpoly([HL(2.5, -8.4), HL(11.5, -7.2), HL(14, -4.6), HL(6, -4.8)], C.SCALE_F_DK, bevel=1.0, base=0.6)
    cv.part(line=False)
    cv.tpoly([HL(6.5, -4.8), HL(13, -4.4), HL(12.4, -2.6), HL(7, -2.8)], C.SLOT_P, flat=0.3)
    if alive:
        er = 0.8 + 0.4 * min(1.5, p.eye)
        cv.tcapsule(HL(8, -3.8), HL(12.2, -3.5), er, er * 0.8, C.EYE_FIRE, flat=0.45 + 0.35 * min(1.0, p.eye))
        cv.tpx(HL(19.5, -1.6), C.LAVA_M, inten=0.6 + 0.3 * min(1.0, p.eye))
        sm = C.SEAM
        gi = 0.3 + 0.35 * min(1.2, p.belly)
        cv.tline(HL(-3, -1), HL(2, 0.5), sm, inten=gi, decal=True)
        cv.tline(HL(2, 0.5), HL(5, -1.5), sm, inten=gi, decal=True)
        cv.tline(HL(-1, 3.5), HL(5, 4.2), sm, inten=gi, decal=True)
    else:
        cv.tpx(HL(19.5, -1.6), C.SLOT_P, inten=0.2)
    # cheek spikes
    cv.part()
    for (bx, by, a, ln) in ((-3, 3, 2.7, 4.5), (-1, 4.8, 2.3, 3.5)):
        spike(cv, HL(bx, by), a + p.ang, ln * HS, 1.2 * HS, C.SCALE_F_DK)
    # near horn: big swept-back obsidian horn
    cv.part()
    hn = [(1.5, -6.5), (-4.5, -11), (-11.5, -14), (-19, -14.5)]
    cv.tchain([HL(*q) for q in hn], [2.7 * HS, 2.2 * HS, 1.5 * HS, 0.5 * HS], C.OBS_M)
    cv.part()
    spike(cv, HL(-1, -9), -1.9 + p.ang, 3.5 * HS, 1.1 * HS, C.OBS_DK)


def _head_ice(cv, p, HL, JL, alive, fl):
    cm = C.CRYST if alive else C.ICE_DK
    # frost mane: white strands flowing back over the neck
    for k, (bx, by, ln, a) in enumerate(((-3, -4, 17, -2.75), (-4, -1, 19, -3.0), (-3.5, 2.5, 15, 2.95),
                                         (0, -6, 12, -2.45), (-2, 4, 10, 2.6))):
        cv.part()
        pts = []
        aa = a + p.ang - 0.25 * p.sweep * (1 if a < 0 else -1)
        aa -= 0.25 * p.mane * (1 if a < 0 else -1)
        for j in range(5):
            tt = j / 4
            wob = math.sin((fl + tt * 0.8 + k * 0.23) * TAU) * 1.6 * tt
            q = HL(bx, by)
            px_ = q[0] + math.cos(aa) * ln * tt * HS - math.sin(aa) * wob
            py_ = q[1] + math.sin(aa) * ln * tt * HS + math.cos(aa) * wob
            pts.append((px_, py_))
        cv.tchain(pts, [1.9, 1.6, 1.2, 0.8, 0.4], C.SNOW_M if k % 2 == 0 else C.MIST_M)
    # far antler
    cv.part()
    crystal(cv, HL(3, -6), -2.35 + p.ang, 11 * HS * (1 - 0.4 * p.dead), 1.5 * HS, C.ICE_DK)
    upper = [(-5, -3), (-1, -6.8), (8, -7.2), (14, -5.4), (20, -3.2), (25, -1.2), (24.5, 0.6), (20, 1.4), (6, 2.0),
             (-4, 3.4)]
    lower = [(2, 1.2), (21, 1.4), (20.5, 3.2), (10, 4.8), (1.5, 4.8)]
    _jaws(cv, p, HL, JL, upper, lower, C.SCALE_I, C.SCALE_I_DK, alive, False, C.SNOW_M, fangs=1.3)
    # crystal brow crest + pale eye
    cv.part()
    for (bx, by, a, ln) in ((6, -6.6, -2.8, 6.0), (11, -5.6, -2.6, 4.5), (15.5, -4.4, -2.5, 3.0)):
        crystal(cv, HL(bx, by), a + p.ang, ln * HS * (1 - 0.4 * p.dead), 1.0 * HS, cm)
    cv.part(line=False)
    cv.tpoly([HL(7, -5.0), HL(13.2, -4.4), HL(12.8, -2.4), HL(7.4, -2.8)], C.SLOT_G, flat=0.3)
    if alive:
        er = 0.8 + 0.4 * min(1.5, p.eye)
        cv.tcapsule(HL(8.6, -3.8), HL(12.4, -3.4), er, er * 0.8, C.EYE_ICE, flat=0.5 + 0.35 * min(1.0, p.eye))
        cv.tpx(HL(22.5, -1.4), C.FROST_GLOW, inten=0.6)
        cv.tline(HL(-3, 0), HL(3, -2), C.FROST_GLOW, inten=0.4 + 0.3 * min(1.0, p.belly), decal=True)
    # whiskers
    for k, (bx, by) in enumerate(((22, 1.6), (18, 2.6))):
        cv.part()
        pts = []
        for j in range(6):
            tt = j / 5
            wob = math.sin((fl + tt * 0.9 + k * 0.4) * TAU) * 2.0 * tt
            q = (bx - 22 * tt * (1 + 0.3 * p.sweep), by + 6 * tt * (1 - 0.6 * p.sweep) + wob + 2 * k * tt)
            pts.append(HL(*q))
        cv.tchain(pts, [0.9, 0.8, 0.7, 0.6, 0.5, 0.4], C.SNOW_M if k == 0 else C.ICE_M)
    # near antler: branching crystal
    cv.part()
    a0 = -2.5 + p.ang - 0.15 * p.mane
    crystal(cv, HL(1, -6), a0, 14 * HS * (1 - 0.4 * p.dead), 1.9 * HS, cm)
    cv.part()
    mid = HL(1 + math.cos(a0 - p.ang) * 6, -6 + math.sin(a0 - p.ang) * 6)
    crystal(cv, mid, -1.75 + p.ang - 0.2 * p.mane, 7 * HS * (1 - 0.4 * p.dead), 1.3 * HS, cm)
    cv.part()
    crystal(cv, HL(-1, -7), -3.0 + p.ang, 6 * HS * (1 - 0.4 * p.dead), 1.1 * HS, cm)


def mouth(p, kind):
    """Frame position of the mouth (breath / projectile origin)."""
    hx, hy = p.head
    ca, sa = math.cos(p.ang), math.sin(p.ang)
    lx, ly = ((20.5 if kind == "fire" else 23.5) * HS, (1.4 + 3.0 * p.jaw) * HS)
    return (hx + lx * ca - ly * sa + p.offset[0], hy + lx * sa + ly * ca + p.offset[1])


def body_points(p, us):
    s, x, y, tx, ty, L = spine(p)
    out = []
    for uu in us:
        k = min(len(s) - 1, int(uu * (len(s) - 1)))
        out.append((x[k] + p.offset[0], y[k] + p.offset[1], ty[k], -tx[k], radius(uu) * p.rscale))  # ventral n
    return out
