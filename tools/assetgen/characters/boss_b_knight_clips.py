"""Knight-set clips (docs: content contract section 3) for Magmaroth and Glaciarch.

Clip order / counts / events (exactly the Thornwarden "Knight set"):
idle 6, walk 8, telegraph_slam 6, slam 6 (impact f3), telegraph_charge 4, charge 6 (loop),
telegraph_barrage 6, barrage 4 (fire f1), summon 6, roar 8, telegraph_laser 6, laser 4 (loop), hurt 2, death 14.
"""
import math

import numpy as np

import boss_b_fx as X
import boss_b_knight as K
import boss_b_palette as C
import effects as FX
from raster import TCanvas

W = H = 96
TAU = 2 * math.pi

DURATIONS = {
    "telegraph_slam": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "slam": [0.05, 0.05, 0.05, 0.2, 0.15, 0.15],
    "telegraph_charge": [0.12, 0.12, 0.14, 0.18],
    "telegraph_barrage": [0.1, 0.1, 0.1, 0.1, 0.12, 0.16],
    "barrage": [0.06, 0.1, 0.1, 0.12],
    "summon": [0.12, 0.1, 0.12, 0.12, 0.14, 0.16],
    "roar": [0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16],
    "telegraph_laser": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "hurt": [0.08, 0.14],
    "death": [0.1, 0.12, 0.12, 0.14, 0.1, 0.1, 0.12, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3],
}


def assemble(clips):
    """[(name, frames)] -> [(name, frames, sheet kwargs)] with the knight-set timing."""
    out = []
    for name, frames in clips:
        kw = {"loop": name in ("idle", "walk", "charge", "laser")}
        if name in DURATIONS:
            kw["durations"] = DURATIONS[name]
        else:
            kw["fps"] = {"idle": 6, "walk": 9, "charge": 12, "laser": 14}[name]
        if name == "slam":
            kw["events"] = [(3, "impact")]
        if name == "barrage":
            kw["events"] = [(1, "fire")]
        out.append((name, frames, kw))
    return out


def glob(f, x, y, ang, r, mat, trail_cols, tl=5):
    """Projectile blob (magma glob / ice shard ball) with a trail behind it."""
    c, s = math.cos(ang), math.sin(ang)
    for k in range(1, tl + 1):
        px, py = int(round(x - c * (k + r))), int(round(y - s * (k + r)))
        if X.empty_at(f, px, py):
            f[py, px] = trail_cols[min(len(trail_cols) - 1, k * len(trail_cols) // (tl + 1))]
    cv = TCanvas(W, H)
    cv.part()
    cv.disc((x, y), r, mat, bias=0.1)
    X.over(f, cv.render())
    return f


def shard(f, x, y, ang, ln, mat=C.CRYST, trail=None):
    cv = TCanvas(W, H)
    cv.part()
    K.crystal(cv, (x - math.cos(ang) * ln * 0.5, y - math.sin(ang) * ln * 0.5), ang, ln, 1.4, mat)
    img = cv.render()
    if trail:
        c, s = math.cos(ang), math.sin(ang)
        for k in range(2, 2 + len(trail) * 2):
            px, py = int(round(x - c * (ln * 0.5 + k))), int(round(y - s * (ln * 0.5 + k)))
            if X.empty_at(f, px, py):
                f[py, px] = trail[min(len(trail) - 1, (k - 2) // 2)]
    X.over(f, img)
    return f


def _fit(pts, fist, lo=2.0, hi=93.0):
    xs = [q[0] for q in pts]
    ys = [q[1] for q in pts]
    dx = hi - max(xs) if max(xs) > hi else (lo - min(xs) if min(xs) < lo else 0.0)
    dy = lo - min(ys) if min(ys) < lo else 0.0
    return (fist[0] + dx, fist[1] + dy)


def fit_hammer(fist, wpn):
    """Move the fist so the whole maul stays inside the frame (x 2..93, y >= 2)."""
    u = (math.cos(wpn), math.sin(wpn))
    pts = K.hammer_extent(fist, wpn) + [(fist[0] - u[0] * 9.5, fist[1] - u[1] * 9.5)]
    return _fit(pts, fist)


def fit_sword(fist, wpn, lost=0.0):
    guard, tip, u = K.sword_geo(fist, wpn, lost)
    v = (-u[1], u[0])
    pts = [tip, (fist[0] - u[0] * 9, fist[1] - u[1] * 9)]
    for k in (-9.0, 9.0):
        pts.append((guard[0] + v[0] * k, guard[1] + v[1] * k))
    for k in (-3.8, 3.8):
        pts.append((guard[0] + v[0] * k, guard[1] + v[1] * k))
    q = _fit(pts, fist)
    return (q[0], fist[1]) if tip[1] > 95 else q


# ============================================================== MAGMAROTH
def M(**kw):
    base = dict(hip=(46.0, 66.0), fist=(74.0, 70.5), wpn=math.pi / 2, foot_f=(59.0, 95.0), foot_b=(34.0, 95.0),
                hand_b=(29.0, 67.0))
    base.update(kw)
    base["fist"] = fit_hammer(base["fist"], base["wpn"])
    return K.Pose(**base)


def vent_fx(f, p, phase, power=1.0, seed=0):
    for j, (vx, vy) in enumerate(K.magma_vents(p)):
        for k in range(2):
            age = (phase + k / 2.0 + j * 0.3) % 1.0
            if age < 0.12:
                continue
            r = (0.7 + age * 1.5) * (0.8 + 0.3 * power)
            x = vx - 1 - age * 6 * power
            y = vy - 3 - age * 16 * power
            X.puff(f, x, y, r, C.ASHES if age < 0.55 else C.ASHES[1:], seed=seed + 11 * j + k)
        X.rising(f, vx - 3, vx + 3, vy - 1, vy + 1, int(3 + 3 * power), phase, C.EMBERS[1:4], seed + 40 + j,
                 rise=12 + 8 * power, sway=1.5)
    return f


def lava_drips(f, x, y, n, phase, seed):
    rng = np.random.default_rng(seed)
    for _ in range(n):
        dx = int(rng.integers(-6, 7))
        k = (phase + rng.random()) % 1.0
        yy = int(y + 2 + k * 12)
        for j in range(2):
            if X.empty_at(f, int(x + dx), yy + j):
                f[yy + j, int(x + dx)] = C.LAVA[3 - j]
    return f


def ground_cracks(f, x0, reach, seed, hot=1.0, spurts=True, phase=0.0, cols=None):
    """Glowing lava fissures spreading left and right along the ground, with flame spurts."""
    rng = np.random.default_rng(seed)
    cols = cols or [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.LAVA[1]]
    for side in (-1, 1):
        x, y = x0, 94
        for k in range(int(reach)):
            x += side
            if rng.random() < 0.3:
                y = 94 if y == 95 else 95
            if not (0 <= x < W):
                break
            fade = k / max(1.0, reach)
            ci = min(len(cols) - 1, int((fade * 0.7 + (1 - hot) * 0.5) * len(cols)))
            f[y, x] = cols[ci]
            if y == 94 and f[95, x, 3] == 0:
                f[95, x] = cols[min(len(cols) - 1, ci + 1)] if hot > 0.6 else C.P_OUT
            if hot > 0.55 and rng.random() < 0.35 * hot and X.empty_at(f, x, y - 1):
                f[y - 1, x] = cols[min(len(cols) - 1, ci + 1)]
            if spurts and k % 7 == 3 and fade < 0.9:
                hgt = int((3 + 5 * rng.random()) * hot * (0.6 + 0.4 * math.sin((phase + k * 0.1) * TAU)))
                for j in range(1, hgt + 1):
                    yy = y - j
                    c = cols[min(len(cols) - 1, j * len(cols) // (hgt + 1))]
                    if X.empty_at(f, x, yy):
                        f[yy, x] = c
                    if j < hgt * 0.5 and X.empty_at(f, x + side, yy):
                        f[yy, x + side] = cols[min(len(cols) - 1, 1 + j * len(cols) // (hgt + 1))]
    return f


def mag_idle():
    out = []
    for i in range(6):
        b = [0, 0, 1, 1, 1, 0][i]
        ph = TAU * i / 6
        p = M(hip=(46, 66 + b), hand_b=(29, 67 + b), core=0.6 + 0.25 * math.sin(ph), glow=0.55 + 0.25 * math.sin(ph),
              wglow=0.45 + 0.15 * math.sin(ph), eye=0.6, t=i / 6)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 6, 0.8, seed=10)
        out.append(f)
    return out


def mag_walk():
    out = []
    keys = [(62, 0, 33, 0), (60, 0, 36, 3), (56, 0, 41, 5), (52, 0, 46, 2),
            (49, 0, 50, 0), (52, 3, 46, 0), (57, 5, 41, 0), (61, 2, 36, 0)]
    bobs = [2, 1, 0, -1, 2, 1, 0, -1]
    for i, (fx, fl, bx, bl) in enumerate(keys):
        ph = TAU * i / 8
        bob = bobs[i]
        p = M(hip=(46 + 0.5 * math.sin(ph * 2), 66 + bob), foot_f=(fx, 95 - fl), foot_b=(bx, 95 - bl),
              fist=(69 - 2 * math.sin(ph), 66 + bob), wpn=1.05 + 0.08 * math.sin(ph),
              hand_b=(29 + 3 * math.sin(ph), 67 + bob), lean=0.06, core=0.65, glow=0.6 + 0.1 * math.sin(ph),
              wglow=0.5, t=i / 8)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 8, 0.8, seed=20)
        if i in (0, 4):
            fx_ = fx + 4 if i == 0 else bx - 6
            FX.burst(f, fx_, 94, 2.5, C.DUST_P, seed=30 + i, jag=0.5)
        out.append(f)
    return out


def mag_telegraph_slam():
    out = []
    path = [(74, 64, 1.25), (73, 52, 0.4), (70, 38, -0.7), (66, 28, -1.6), (62, 21, -2.2), (60, 19, -2.5)]
    for i, (fx, fy, wa) in enumerate(path):
        t = (i + 1) / 6
        p = M(fist=(fx, fy), wpn=wa, lean=-0.12 * t, hip=(46, 66 - t), glow=0.6 + 0.9 * t, wglow=0.5 + 1.1 * t,
              core=0.6 + 0.7 * t, eye=0.6 + 0.9 * t, bristle=0.4 * t, head_tilt=-0.1 * t, foot_f=(61, 95),
              foot_b=(33, 95), t=i / 6)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 6, 0.8 + 0.6 * t, seed=40)
        hc, _, _ = K.hammer_geo(p.fist, p.wpn)
        if i >= 2:
            X.converge(f, hc[0], hc[1], 4 + 3 * i, t, C.EMBERS[:4], seed=50 + i, r_max=22)
        if i >= 3:
            lava_drips(f, hc[0], hc[1] + 6, 2 + i, i / 6, seed=60 + i)
        if i == 5:
            FX.sparkle(f, int(hc[0]), int(hc[1]) - 12, 4, C.LAVA[4], C.LAVA[3])
        out.append(f)
    return out


def mag_slam():
    a = [
        M(fist=(60, 19), wpn=-2.5, lean=-0.1, hip=(46, 65), glow=1.4, wglow=1.6, core=1.2, eye=1.4, bristle=0.4,
          foot_f=(61, 95), foot_b=(33, 95)),
        M(fist=(76, 36), wpn=-1.25, lean=0.12, hip=(48, 66), glow=1.3, wglow=1.6, core=1.1, eye=1.3,
          foot_f=(62, 95), foot_b=(33, 95)),
        M(fist=(70, 58), wpn=0.55, lean=0.28, hip=(50, 69), glow=1.2, wglow=1.6, core=1.0, eye=1.2,
          foot_f=(63, 95), foot_b=(33, 95)),
        M(fist=(64, 81), wpn=0.1, lean=0.38, hip=(51, 72), glow=1.5, wglow=1.6, core=1.0, eye=1.2,
          foot_f=(64, 95), foot_b=(32, 95), head_tilt=0.1, hand_b=(38, 78)),
        M(fist=(64, 81), wpn=0.1, lean=0.36, hip=(51, 72), glow=1.1, wglow=1.2, core=0.9, eye=1.0,
          foot_f=(64, 95), foot_b=(32, 95), head_tilt=0.1, hand_b=(38, 78)),
        M(fist=(70, 74), wpn=0.85, lean=0.18, hip=(49, 69), glow=0.8, wglow=0.8, core=0.7, eye=0.8,
          foot_f=(62, 95), foot_b=(33, 95)),
    ]
    out = [K.draw_magmaroth(p) for p in a]
    for i, f in enumerate(out):
        vent_fx(f, a[i], i / 6, 1.0, seed=70)
    # swing smears (arc of glowing pixels behind the maul head)
    for k, (cx, cy, r, a0, a1) in enumerate(((64, 40, 24, -2.4, -1.2), (64, 54, 26, -1.4, 0.2))):
        f = out[k + 1]
        for j in range(3):
            for (x, y) in FX.ring_pixels(cx, cy, r - j * 2):
                ang = math.atan2(y - cy, x - cx)
                if a0 < ang < a1 and X.empty_at(f, x, y) and (x + y + j) % (j + 1) == 0:
                    f[y, x] = C.LAVA[3 - j]
    # impact
    hc, _, _ = K.hammer_geo(a[3].fist, a[3].wpn)
    imp = out[3]
    ground_cracks(imp, int(hc[0]), 30, seed=71, hot=1.0)
    X.burst_under(imp, hc[0] + 6, 91, 8, C.DUST_P, seed=72, jag=0.45)
    X.burst_under(imp, hc[0] - 8, 93, 5, C.DUST_P, seed=73, jag=0.45)
    X.burst_under(imp, hc[0], 94, 6, [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.LAVA[1]], seed=74, jag=0.4)
    for (x, y, sz) in ((92, 74, 3), (70, 80, 2), (89, 66, 2), (78, 70, 2), (94, 84, 2), (60, 84, 2)):
        X.chunk(imp, x, y, sz, C.MAG[0], C.MAG[2], C.P_OUT)
    X.rising(imp, hc[0] - 12, hc[0] + 12, 80, 94, 10, 0.2, C.EMBERS[:4], seed=75, rise=10)
    out[3] = X.shake(imp, 0, 1)
    f4 = out[4]
    ground_cracks(f4, int(hc[0]), 36, seed=71, hot=0.7, phase=0.3)
    X.burst_under(f4, hc[0] + 7, 89, 7, C.DUST_P[1:], seed=76, jag=0.5)
    X.burst_under(f4, hc[0] - 9, 92, 4, C.DUST_P[1:], seed=77, jag=0.5)
    for (x, y, sz) in ((94, 62, 2), (62, 72, 2), (86, 56, 1)):
        X.chunk(f4, x, y, sz, C.MAG[0], C.MAG[2], C.P_OUT)
    X.rising(f4, hc[0] - 14, hc[0] + 14, 74, 92, 10, 0.5, C.EMBERS[1:4], seed=78, rise=12)
    ground_cracks(out[5], int(hc[0]), 36, seed=71, hot=0.3, spurts=False)
    X.burst_under(out[5], hc[0] + 8, 92, 3, C.DUST_P[2:], seed=79, jag=0.5)
    return out


def mag_telegraph_charge():
    out = []
    for i in range(4):
        t = (i + 1) / 4
        p = M(lean=0.18 + 0.14 * t, hip=(45 - 2 * t, 67 + 2 * t), head_tilt=0.12 + 0.18 * t,
              fist=(70 - 2 * t, 66 + 2 * t), wpn=1.15 + 0.15 * t, foot_f=(61, 95),
              foot_b=(30 - (i % 2) * 3, 95 - (i % 2) * 2), core=0.7 + 0.5 * t, glow=0.7 + 0.6 * t,
              eye=0.8 + 0.7 * t, wglow=0.6, bristle=0.3 * t, t=i / 4)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 4, 1.0 + 0.6 * t, seed=80)
        FX.burst(f, 25, 93, 2 + i, C.DUST_P[1:], seed=81 + i, jag=0.5)
        X.rising(f, 20, 34, 86, 94, 3 + i, i / 4, C.EMBERS[1:4], seed=85, rise=8)
        out.append(f)
    return out


def mag_charge():
    out = []
    feet = [((66, 95), (30, 92)), ((61, 95), (39, 88)), ((54, 91), (47, 93)), ((47, 88), (54, 95)),
            ((42, 92), (60, 95)), ((58, 93), (36, 91))]
    for i, (ff, fb) in enumerate(feet):
        bob = [0, 1, 0, -1, 0, 1][i]
        p = M(lean=0.32, hip=(46, 67 + bob), fist=(72, 64 + bob), wpn=0.95 + 0.06 * math.sin(i * TAU / 6),
              elbow_dir=1.0, foot_f=ff, foot_b=fb, core=1.0, glow=1.1, eye=1.3, wglow=0.8, head_tilt=0.3,
              bristle=0.3, t=i / 6)
        f = K.draw_magmaroth(p)
        # embers & smoke streaming back from the vents
        for j, (vx, vy) in enumerate(K.magma_vents(p)):
            for k in range(3):
                age = (i / 6 + k / 3 + j * 0.2) % 1.0
                X.puff(f, vx - 3 - age * 14, vy - 1 - age * 4, 1.0 + age * 1.8, C.ASHES, seed=90 + k)
        X.rising(f, 4, 30, 50, 80, 8, i / 6, C.EMBERS[1:4], seed=91, rise=-6, sway=3)
        FX.burst(f, 22 - (i % 3) * 3, 93, 2 + (i % 2), C.DUST_P[1:], seed=92 + i, jag=0.5)
        X.speed_lines(f, 93 + i, 4, 36, 88, 2, 14, C.ASH[1])
        out.append(f)
    return out


def mag_telegraph_barrage():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        wa = math.pi / 2 - (math.pi / 2 + 1.45) * t
        p = M(fist=(74 - 4 * t, 68 - 30 * t), wpn=wa, lean=-0.06 * t, glow=0.6 + 0.8 * t, wglow=0.5 + 1.1 * t,
              core=0.6 + 0.8 * t, eye=0.7 + 0.7 * t, bristle=0.5 * t, t=i / 6)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 6, 0.8 + 0.5 * t, seed=100)
        hc, _, _ = K.hammer_geo(p.fist, p.wpn)
        if i >= 2:
            X.converge(f, hc[0], hc[1], 3 + 2 * i, t, C.EMBERS[:4], seed=101 + i, r_max=20)
            lava_drips(f, hc[0], hc[1] + 5, 2 + i, i / 6, seed=110 + i)
        if i >= 4:
            # molten globs swelling on the maul head
            for k, (du, dv) in enumerate(((7, -6), (8, 2), (6, 8))[:i - 2]):
                _, u, v = K.hammer_geo(p.fist, p.wpn)
                gx, gy = hc[0] + u[0] * du + v[0] * dv, hc[1] + u[1] * du + v[1] * dv
                glob(f, gx, gy, -1.2, 1.6 + 0.3 * (i - 4), C.LAVA_M, C.EMBERS[2:], tl=0)
        out.append(f)
    return out


def mag_barrage():
    a = [M(fist=(70, 34), wpn=-1.5, lean=0.0, glow=1.5, wglow=1.6, core=1.4, eye=1.4, bristle=0.5),
         M(fist=(72, 40), wpn=-1.0, lean=0.08, glow=1.3, wglow=1.3, core=1.2, eye=1.3, bristle=0.3),
         M(fist=(70, 56), wpn=0.8, lean=0.12, glow=1.0, wglow=1.0, core=1.0, eye=1.0),
         M(fist=(72, 66), wpn=1.2, lean=0.05, glow=0.8, wglow=0.7, core=0.8, eye=0.8)]
    out = [K.draw_magmaroth(p) for p in a]
    for i, f in enumerate(out):
        vent_fx(f, a[i], i / 4, 1.0, seed=120)
    hc, _, _ = K.hammer_geo(a[0].fist, a[0].wpn)
    FX.burst(out[0], hc[0] + 2, hc[1] - 4, 4, C.EMBERS[:3], seed=121, jag=0.35)
    hc, _, _ = K.hammer_geo(a[1].fist, a[1].wpn)
    for k, (ang, d, r) in enumerate(((-1.25, 13, 2.4), (-0.85, 15, 2.2), (-0.45, 13, 2.0), (-1.6, 10, 1.8))):
        glob(out[1], hc[0] + math.cos(ang) * d, hc[1] + math.sin(ang) * d, ang, r, C.LAVA_M, C.EMBERS[2:], tl=5)
    FX.burst(out[1], hc[0] + 3, hc[1] - 3, 5, C.EMBERS[:4], seed=122, jag=0.4)
    for k, (x, y, ang, r) in enumerate(((90, 10, -1.0, 1.8), (94, 24, -0.6, 1.6))):
        glob(out[2], x, y, ang, r, C.LAVA_M, C.EMBERS[2:], tl=4)
    X.rising(out[2], 76, 94, 14, 40, 6, 0.4, C.EMBERS[1:4], seed=123, rise=-6)
    return out


def mag_summon():
    a = [M(fist=(72, 42), wpn=-1.45, lean=-0.04, glow=1.0, wglow=1.2, core=1.0, eye=1.1, bristle=0.3),
         M(fist=(74, 70.5), wpn=math.pi / 2, lean=0.16, hip=(47, 68), glow=1.3, wglow=1.6, core=1.2, eye=1.2),
         M(fist=(74, 70.5), wpn=math.pi / 2, lean=0.16, hip=(47, 68), glow=1.4, wglow=1.4, core=1.4, eye=1.3),
         M(fist=(74, 70.5), wpn=math.pi / 2, lean=0.14, hip=(47, 68), glow=1.5, wglow=1.3, core=1.5, eye=1.4),
         M(fist=(74, 70.5), wpn=math.pi / 2, lean=0.14, hip=(47, 68), glow=1.2, wglow=1.0, core=1.2, eye=1.2),
         M(fist=(74, 70.5), wpn=math.pi / 2, lean=0.04, glow=0.8, wglow=0.6, core=0.8, eye=0.9)]
    out = [K.draw_magmaroth(p) for p in a]
    for i in range(6):
        f = out[i]
        vent_fx(f, a[i], i / 6, 1.0 + (0.5 if 1 <= i <= 4 else 0), seed=130)
        if 1 <= i <= 4:
            ground_cracks(f, 76, 8 + i * 10, seed=131, hot=1.0 - 0.1 * i, phase=i / 6)
            X.rising(f, 76 - 10 * i, 76 + 10 * i, 84, 94, 4 + 3 * i, i / 6, C.EMBERS[:4], seed=132 + i, rise=14)
        if i == 5:
            ground_cracks(f, 76, 50, seed=131, hot=0.35, spurts=False)
    X.burst_under(out[1], 77, 92, 7, [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.DUST_P[0]], seed=135, jag=0.45)
    X.burst_under(out[1], 64, 93, 4, C.DUST_P, seed=136, jag=0.5)
    out[1] = X.shake(out[1], 0, 1)
    return out


def mag_roar():
    out = []
    keys = [(0.2, 0.0, 0.7, 0.08, 0), (0.3, 0.0, 0.9, 0.12, 0), (-0.45, 1.0, 1.4, -0.12, 1),
            (-0.5, 1.0, 1.5, -0.14, -1), (-0.5, 1.0, 1.5, -0.14, 1), (-0.45, 1.0, 1.4, -0.12, -1),
            (-0.2, 0.5, 1.0, -0.04, 0), (0.0, 0.0, 0.7, 0.0, 0)]
    for i, (ht, jaw, core, lean, sh) in enumerate(keys):
        p = M(head_tilt=ht, jaw=jaw, core=core, glow=0.5 + core * 0.6, eye=0.6 + core * 0.6, lean=lean,
              fist=(73, 70.5 - 12 * jaw), wpn=math.pi / 2 - 0.5 * jaw, hand_b=(26, 60 - 8 * jaw), bristle=jaw,
              wglow=0.4 + 0.6 * jaw, t=i / 8)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 8, 0.8 + 1.0 * jaw, seed=140)
        if jaw > 0.5:
            mx, my = K.magma_mouth(p)
            FX.burst(f, mx + 3, my, 3 + (i % 2), C.EMBERS[:4], seed=141 + i, jag=0.4)
            X.rings(f, mx + 1, my, (7 + (i % 2) * 2, 12 + (i % 2) * 2, 17 + (i % 2) * 2),
                    [C.LAVA[3], C.LAVA[2], C.LAVA[1]], a0=-1.2, a1=0.9)
            X.rising(f, 20, 80, 10, 50, 8, i / 8, C.EMBERS[1:4], seed=145, rise=10)
            f = X.shake(f, sh, 0)
        out.append(f)
    return out


def flame_stream(f, x0, y0, i, cols=None, x1=W, seed=0):
    """Turbulent flame breath from (x0, y0) to the right edge."""
    cols = cols or [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.LAVA[1], C.LAVA[0]]
    rng = np.random.default_rng(seed + i)
    for x in range(int(x0), x1):
        d = x - x0
        hw = 2.4 + d * 0.2 + 0.9 * math.sin(d * 0.55 + i * 1.7)
        yc = y0 + 1.0 * math.sin(d * 0.3 - i * 1.3) + d * 0.06
        for y in range(int(yc - hw - 2), int(yc + hw + 3)):
            if not (0 <= y < H):
                continue
            e = abs(y + 0.5 - yc) / max(hw, 0.5)
            jit = 0.12 * math.sin(x * 1.3 + y * 0.9 + i * 2.1)
            e += jit
            if e > 1.05:
                continue
            ci = 0 if e < 0.3 else (1 if e < 0.55 else (2 if e < 0.8 else 3))
            if d > 40 and e > 0.6:
                ci = min(4, ci + 1)
            f[y, x] = cols[ci]
    # licks of flame / sparks around the stream
    for _ in range(14):
        x = int(x0 + 6 + rng.random() * (x1 - x0 - 6))
        d = x - x0
        hw = 2.4 + d * 0.2
        side = -1 if rng.random() < 0.5 else 1
        y = int(y0 + side * (hw + 1 + rng.random() * 4))
        if X.empty_at(f, x, y):
            f[y, x] = cols[2 + int(rng.integers(0, 2))]
    return f


def mag_telegraph_laser():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = M(lean=-0.08 * t, head_tilt=-0.28 * t, jaw=0.5 * t, core=0.8 + 0.8 * t, glow=0.7 + 0.9 * t,
              eye=0.8 + 0.6 * t, wglow=0.5 + 0.3 * t, bristle=0.3 * t, t=i / 6)
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 6, 0.8 + 0.8 * t, seed=150)
        mx, my = K.magma_mouth(p)
        X.converge(f, mx + 2, my, 5 + 3 * i, t, C.EMBERS[:4], seed=151 + i, r_max=24)
        if i >= 1:
            X.burst_under(f, mx + 1.5, my + 0.5, 1.5 + 0.6 * i, C.EMBERS[:4], seed=165 + i, jag=0.35)
        cx, cy = K.magma_core(p)
        X.motes(f, cx, cy, 3 + i, 9, 14, C.EMBERS[1:4], seed=160 + i)
        if i >= 2:
            X.rings(f, mx + 2, my, (9 - i,), [C.LAVA[2]], a0=-math.pi, a1=math.pi, dotted=True)
        if i == 5:
            FX.sparkle(f, int(mx) + 3, int(my), 5, C.LAVA[4], C.LAVA[3])
        out.append(f)
    return out


def mag_laser():
    out = []
    for i in range(4):
        p = M(lean=0.14, head_tilt=0.2, jaw=1.0, core=1.5, glow=1.4, eye=1.4, wglow=0.8, bristle=0.4, t=i / 4,
              hand_b=(30, 66), hip=(46, 67))
        f = K.draw_magmaroth(p)
        vent_fx(f, p, i / 4, 1.4, seed=170)
        mx, my = K.magma_mouth(p)
        # flame stream drawn by the game (textured beam, 2.8): only the muzzle here
        FX.burst(f, mx + 2, my + 0.5, 4 + (i % 2), C.EMBERS[:3], seed=172 + i, jag=0.3)
        X.rising(f, 70, 95, 28, 50, 8, i / 4, C.ASHES, seed=176, rise=8)
        out.append(f)
    return out


def mag_hurt():
    a = X.flash(K.draw_magmaroth(M(lean=-0.1, hip=(45, 66), head_tilt=-0.2, core=1.0, glow=1.2, cracks=1,
                                   fist=(72, 70.5), wpn=1.4)), [C.P_OUT], amount=0.6)
    b = K.draw_magmaroth(M(lean=-0.06, hip=(46, 66), head_tilt=-0.1, core=0.8, glow=0.9, fist=(73, 70.5), wpn=1.5))
    return [a, b]


def _mag_rubble(t, glow=0.5):
    cv = TCanvas(W, H)
    rocks = [(48, 92, 13, 5.0), (35, 93, 7, 3.6), (61, 93, 8, 4.0), (43, 88, 6, 3.4), (55, 88, 7, 3.6),
             (72, 93, 6, 3.0), (26, 94, 4, 2.2)]
    for i, (x, y, rx, ry) in enumerate(rocks):
        cv.part()
        mat = (C.BAS, C.BAS_DK, C.BAS_BK)[i % 3]
        yy = 96 - (96 - y) * t - 0.5
        cv.ellipse((x, yy + ry * (1 - t)), rx, ry * max(0.3, t), mat, ang=0.12 * (i % 3 - 1))
    img = cv.render()
    if glow > 0:
        for (x0, y0, x1, y1) in ((42, 90, 50, 92), (52, 89, 57, 93), (33, 93, 37, 92)):
            n = max(abs(x1 - x0), 1)
            for k in range(n + 1):
                x = int(x0 + (x1 - x0) * k / n)
                y = int(y0 + (y1 - y0) * k / n)
                if img[y, x, 3]:
                    img[y, x] = C.LAVA[2] if glow > 0.6 else C.LAVA[0]
    return img


def mag_death():
    seq = [
        M(lean=-0.12, head_tilt=-0.35, jaw=0.8, core=1.6, glow=1.6, eye=1.5, cracks=1, fist=(72, 66), wpn=1.4),
        M(lean=-0.04, head_tilt=-0.15, core=0.5, glow=1.2, eye=0.8, cracks=2, hip=(46, 68), fist=(73, 69), wpn=1.45),
        M(lean=0.08, head_tilt=0.15, core=1.4, glow=1.5, eye=0.5, cracks=2, hip=(46, 72), fist=(72, 73), wpn=1.4,
          foot_f=(62, 95), foot_b=(32, 95), hand_b=(30, 74)),
        M(lean=0.18, head_tilt=0.3, core=1.6, glow=1.6, eye=0.3, cracks=2, hip=(46, 78), fist=(72, 78), wpn=1.15,
          foot_f=(64, 95), foot_b=(30, 95), hand_b=(31, 82), jaw=0.4),
    ]
    out = [K.draw_magmaroth(p) for p in seq]
    out[0] = X.flash(out[0], [C.P_OUT], amount=0.5)
    for i, f in enumerate(out):
        vent_fx(f, seq[i], i / 4, 1.2 - 0.3 * i, seed=180)
    for (x, y) in ((50, 38), (58, 52), (40, 60)):
        FX.burst(out[1], x, y, 2, C.EMBERS[:3], seed=181 + x, jag=0.4)
    cx, cy = K.magma_core(seq[2])
    FX.sparkle(out[2], int(cx), int(cy), 5, C.LAVA[4], C.LAVA[3])
    cx, cy = K.magma_core(seq[3])
    FX.burst(out[3], cx, cy, 5, C.EMBERS[:4], seed=185, jag=0.4)
    lava_drips(out[3], 52, 70, 6, 0.3, seed=186)
    # break apart: the kneeling body splits along glowing seams and crashes down
    hot = K.draw_magmaroth(seq[3].copy(core=1.6, glow=1.6))
    cold = K.draw_magmaroth(seq[3].copy(core=0.0, glow=0.0, eye=0.0, dead=0.8))
    for k, t in enumerate((0.18, 0.38, 0.62, 0.9)):
        src = hot if k < 2 else cold
        edge = [C.LAVA[4], C.LAVA[3], C.LAVA[2]] if k < 2 else ([C.LAVA[1], C.LAVA[0]] if k < 3 else
                                                                 [C.LAVA[0], C.MAG[2]])
        f = X.shatter(src, t, seed=190, out_col=C.P_OUT, edge_cols=edge, n=14, center=(cx, cy), spread=1.0,
                      pop=1.2, grav=48)
        X.rising(f, 24, 76, 40, 92, 10 + 4 * k, k / 4, C.EMBERS[1:4], seed=191 + k, rise=14)
        if k < 2:
            FX.burst(f, cx, cy, 6 - 2 * k, C.EMBERS[:4], seed=195 + k, jag=0.5)
        else:
            FX.burst(f, 30 + 14 * k, 93, 3 + k, C.DUST_P[1:], seed=197 + k, jag=0.5)
        out.append(f)
    pile = X.shatter(cold, 1.0, seed=190, out_col=C.P_OUT, edge_cols=[C.LAVA[0], C.MAG[2]], n=14,
                     center=(cx, cy), spread=1.0, pop=1.2, grav=48)
    for k, t in enumerate((0.2, 0.36, 0.52, 0.68, 0.84, 1.0)):
        f = X.dissolve(pile, min(1.0, t), C.P_OUT, seed=200, colors=[C.LAVA[3], C.LAVA[2], C.LAVA[1], C.ASH[1]],
                       rise=22, drift=5, top_first=True, life=0.4, particle_rate=0.14, bounds=(0, 0, W, 92))
        FX.under(f, _mag_rubble(min(1.0, 0.4 + t * 0.7), glow=0.8 - 0.15 * k))
        X.rising(f, 28, 70, 50, 92, 8 - k, k / 5, C.EMBERS[1:4], seed=205 + k, rise=16)
        out.append(f)
    last = _mag_rubble(1.0, glow=0.4)
    for (x, y) in ((44, 72), (52, 64), (48, 56), (57, 78)):
        last[y, x] = C.LAVA[2]
    out[-1] = last
    return out


def magmaroth_clips():
    return assemble([
        ("idle", mag_idle()), ("walk", mag_walk()), ("telegraph_slam", mag_telegraph_slam()), ("slam", mag_slam()),
        ("telegraph_charge", mag_telegraph_charge()), ("charge", mag_charge()),
        ("telegraph_barrage", mag_telegraph_barrage()), ("barrage", mag_barrage()), ("summon", mag_summon()),
        ("roar", mag_roar()), ("telegraph_laser", mag_telegraph_laser()), ("laser", mag_laser()),
        ("hurt", mag_hurt()), ("death", mag_death())])


# ============================================================== GLACIARCH
def G(**kw):
    base = dict(hip=(47.0, 63.0), fist=(70.0, 65.0), wpn=1.42, foot_f=(58.0, 95.0), foot_b=(36.0, 95.0),
                hand_b=(32.0, 64.0))
    base.update(kw)
    base["fist"] = fit_sword(base["fist"], base["wpn"], base.get("broken_weapon", 0.0))
    return K.Pose(**base)


def frost_patch(f, x0, reach, seed, cols=None, crystals=True, grow=1.0):
    """Frost spreading along the ground with small crystals sprouting."""
    rng = np.random.default_rng(seed)
    cols = cols or [C.WHITE, C.ICE[3], C.FROST, C.ICE[2]]
    cv = TCanvas(W, H)
    for side in (-1, 1):
        x = x0
        for k in range(int(reach)):
            x += side
            if not (0 <= x < W):
                break
            fade = k / max(1.0, reach)
            ci = min(len(cols) - 1, int(fade * len(cols)))
            if f[95, x, 3] == 0 or True:
                f[95, x] = cols[ci]
            if rng.random() < 0.5 and X.empty_at(f, x, 94):
                f[94, x] = cols[min(len(cols) - 1, ci + 1)]
            if crystals and k % 7 == 3 and fade < 0.92:
                ln = (3 + 5 * rng.random()) * grow * (1 - 0.6 * fade)
                if ln >= 2:
                    cv.part()
                    K.crystal(cv, (x + 0.5, 95.5), -math.pi / 2 + side * (0.25 + 0.3 * rng.random()), ln, 1.4,
                              C.CRYST)
    X.under(f, cv.render())
    return f


def ice_spikes(f, x0, spikes, mat=C.CRYST):
    cv = TCanvas(W, H)
    for (dx, ln, a, wd) in spikes:
        cv.part()
        K.crystal(cv, (x0 + dx, 96.5), a, ln, wd, mat)
    X.over(f, cv.render())
    return f


def gla_idle():
    out = []
    for i in range(6):
        b = [0, 0, 1, 1, 1, 0][i]
        ph = TAU * i / 6
        p = G(hip=(47, 63 + b), hand_b=(32, 64 + b), core=0.6 + 0.25 * math.sin(ph), glow=0.6 + 0.2 * math.sin(ph),
              wglow=0.45 + 0.15 * math.sin(ph), eye=0.6, cape_wave=1.2 * math.sin(ph), t=i / 6)
        f = K.draw_glaciarch(p)
        frost_patch(f, 74, 9, seed=300, crystals=False)
        X.rising(f, 20, 80, 30, 92, 6, i / 6, C.SNOWS, seed=301, rise=-10, sway=2)
        cx, cy = K.glaciarch_crown(p)
        tw = [(cx - 4, cy - 10), (cx + 2, cy - 14), (cx + 6, cy - 9)][i % 3]
        if i % 2 == 0:
            FX.sparkle(f, int(tw[0]), int(tw[1]), 2, C.WHITE, C.FROST)
        out.append(f)
    return out


def gla_walk():
    out = []
    keys = [(62, 0, 34, 0, 0), (60, 0, 37, 3, 1), (56, 0, 42, 5, 0), (52, 0, 47, 2, -1),
            (49, 0, 51, 0, 0), (52, 3, 47, 0, 1), (57, 5, 42, 0, 0), (61, 2, 37, 0, -1)]
    for i, (fx, fl, bx, bl, bob) in enumerate(keys):
        ph = TAU * i / 8
        p = G(hip=(47 + 0.5 * math.sin(ph * 2), 63 + bob), foot_f=(fx, 95 - fl), foot_b=(bx, 95 - bl),
              fist=(66 - 2 * math.sin(ph), 63 + bob), wpn=0.82 + 0.05 * math.sin(ph),
              hand_b=(32 + 3 * math.sin(ph), 64 + bob), lean=0.04, cape_wave=1.8 * math.sin(ph), cape_lift=0.15,
              core=0.65, glow=0.6, wglow=0.5, t=i / 8)
        f = K.draw_glaciarch(p)
        X.rising(f, 10, 40, 60, 92, 4, i / 8, C.SNOWS, seed=310, rise=-8, sway=2)
        out.append(f)
    return out


def gla_telegraph_slam():
    out = []
    path = [(70, 58, 0.6), (70, 46, -0.6), (67, 36, -1.4), (63, 28, -2.0), (60, 23, -2.45), (59, 21, -2.65)]
    for i, (fx, fy, wa) in enumerate(path):
        t = (i + 1) / 6
        p = G(fist=(fx, fy), wpn=wa, lean=-0.1 * t, hip=(46, 63 - t), glow=0.6 + 0.8 * t, wglow=0.5 + 1.1 * t,
              core=0.6 + 0.7 * t, eye=0.6 + 0.9 * t, bristle=0.6 * t, head_tilt=-0.1 * t, cape_wave=-t,
              cape_lift=0.2 * t, foot_f=(60, 95), foot_b=(34, 95), t=i / 6)
        f = K.draw_glaciarch(p)
        _, tip, _ = K.sword_geo(p.fist, p.wpn)
        guard, _, _ = K.sword_geo(p.fist, p.wpn)
        mid = ((tip[0] + guard[0]) / 2, (tip[1] + guard[1]) / 2)
        if i >= 2:
            X.converge(f, mid[0], mid[1], 5 + 3 * i, t, C.FROSTS[:4], seed=320 + i, r_max=22)
        if i == 5:
            FX.sparkle(f, int(tip[0]), int(tip[1]), 4, C.WHITE, C.FROST)
        out.append(f)
    return out


def gla_slam():
    a = [
        G(fist=(59, 21), wpn=-2.6, lean=-0.1, hip=(46, 62), glow=1.4, wglow=1.6, core=1.2, eye=1.4, bristle=0.6,
          foot_f=(60, 95), foot_b=(34, 95), cape_lift=0.2),
        G(fist=(76, 40), wpn=-1.15, lean=0.1, hip=(48, 63), glow=1.3, wglow=1.6, core=1.1, eye=1.3,
          foot_f=(61, 95), foot_b=(34, 95), cape_lift=0.4, cape_wave=-2),
        G(fist=(64, 56), wpn=0.5, lean=0.24, hip=(50, 66), glow=1.2, wglow=1.6, core=1.0, eye=1.2,
          foot_f=(63, 95), foot_b=(33, 95), cape_lift=0.5, cape_wave=-1),
        G(fist=(66, 70), wpn=1.05, lean=0.34, hip=(51, 69), glow=1.4, wglow=1.6, core=1.0, eye=1.2,
          foot_f=(64, 95), foot_b=(32, 95), cape_lift=0.3, head_tilt=0.1),
        G(fist=(66, 70), wpn=1.05, lean=0.32, hip=(51, 69), glow=1.1, wglow=1.2, core=0.9, eye=1.0,
          foot_f=(64, 95), foot_b=(32, 95), cape_wave=1.5, head_tilt=0.1),
        G(fist=(68, 64), wpn=1.2, lean=0.16, hip=(49, 66), glow=0.8, wglow=0.8, core=0.7, eye=0.8,
          foot_f=(62, 95), foot_b=(34, 95), cape_wave=0.5),
    ]
    out = [K.draw_glaciarch(p) for p in a]
    for k, (cx, cy, r, a0, a1) in enumerate(((58, 44, 30, -2.0, -0.9), (60, 58, 32, -1.0, 0.7))):
        f = out[k + 1]
        for j in range(3):
            for (x, y) in FX.ring_pixels(cx, cy, r - j * 2):
                ang = math.atan2(y - cy, x - cx)
                if a0 < ang < a1 and X.empty_at(f, x, y) and (x + y + j) % (j + 1) == 0:
                    f[y, x] = [C.WHITE, C.FROST, C.ICE[1]][j]
    _, tip, _ = K.sword_geo(a[3].fist, a[3].wpn)
    x0 = int(min(tip[0], 90))
    imp = out[3]
    ice_spikes(imp, x0, [(-9, 7, -1.9, 1.8), (-3, 12, -1.7, 2.2), (3, 15, -1.45, 2.4), (8, 10, -1.2, 2.0),
                         (12, 6, -1.0, 1.6)])
    FX.burst(imp, x0 + 2, 92, 6, C.DUST_G, seed=330, jag=0.45)
    frost_patch(imp, x0, 26, seed=331, crystals=False)
    for (x, y, sz) in ((93, 70, 2), (72, 74, 2), (88, 62, 2), (80, 66, 1), (60, 80, 2)):
        X.chunk(imp, x, y, sz, C.ICE[3], C.ICE[1], C.G_OUT)
    out[3] = X.shake(imp, 0, 1)
    f4 = out[4]
    ice_spikes(f4, x0, [(-11, 6, -2.0, 1.6), (-4, 14, -1.75, 2.2), (2, 18, -1.5, 2.6), (8, 12, -1.25, 2.2),
                        (13, 8, -1.0, 1.8), (-16, 4, -2.2, 1.4)])
    frost_patch(f4, x0, 34, seed=331, crystals=False)
    FX.burst(f4, x0 - 10, 93, 4, C.DUST_G[1:], seed=332, jag=0.5)
    X.rising(f4, x0 - 16, x0 + 10, 60, 92, 8, 0.3, C.FROSTS[1:], seed=333, rise=10)
    f5 = out[5]
    ice_spikes(f5, x0, [(-4, 9, -1.75, 2.0), (2, 12, -1.5, 2.2), (8, 7, -1.25, 1.8)], mat=C.ICE_M)
    for (x, y, sz) in ((x0 - 8, 88, 2), (x0 + 10, 86, 2), (x0 + 3, 80, 1)):
        X.chunk(f5, x, y, sz, C.ICE[3], C.ICE[1], C.G_OUT)
    frost_patch(f5, x0, 34, seed=331, crystals=False, cols=[C.ICE[3], C.ICE[2], C.ICE[1]])
    return out


def gla_telegraph_charge():
    out = []
    for i in range(4):
        t = (i + 1) / 4
        p = G(lean=0.16 + 0.14 * t, hip=(45 - 2 * t, 64 + 2 * t), fist=(60 - 2 * t, 62 + 2 * t), wpn=0.32,
              foot_f=(60, 95), foot_b=(30 - (i % 2) * 2, 95 - (i % 2) * 2), core=0.7 + 0.5 * t, glow=0.7 + 0.6 * t,
              eye=0.8 + 0.7 * t, wglow=0.6 + 0.7 * t, head_tilt=0.12 * t, bristle=0.4 * t, cape_lift=0.3 + 0.3 * t,
              cape_wave=-1 - t, elbow_dir=1.0, t=i / 4)
        f = K.draw_glaciarch(p)
        FX.burst(f, 26, 93, 2 + i, C.DUST_G, seed=340 + i, jag=0.5)
        _, tip, _ = K.sword_geo(p.fist, p.wpn)
        if i >= 1:
            FX.sparkle(f, int(tip[0]) - 1, int(tip[1]), 1 + i, C.WHITE, C.FROST)
        out.append(f)
    return out


def gla_charge():
    out = []
    feet = [((66, 95), (30, 92)), ((61, 95), (39, 88)), ((54, 91), (47, 93)), ((47, 88), (54, 95)),
            ((42, 92), (60, 95)), ((58, 93), (36, 91))]
    for i, (ff, fb) in enumerate(feet):
        ph = TAU * i / 6
        bob = [0, 1, 0, -1, 0, 1][i]
        p = G(lean=0.3, hip=(46, 64 + bob), fist=(58, 62 + bob), wpn=0.3, elbow_dir=1.0, foot_f=ff, foot_b=fb,
              core=1.0, glow=1.0, eye=1.3, wglow=1.2, head_tilt=0.12, cape_lift=1.0, cape_wave=2 * math.sin(ph),
              bristle=0.3, t=i / 6)
        f = K.draw_glaciarch(p)
        FX.burst(f, 22 - (i % 3) * 3, 93, 2 + (i % 2), C.DUST_G, seed=350 + i, jag=0.5)
        X.speed_lines(f, 360 + i, 4, 30, 88, 2, 14, C.ICE[1])
        X.rising(f, 2, 26, 40, 90, 8, i / 6, C.FROSTS[1:], seed=366, rise=-4, sway=3)
        out.append(f)
    return out


def gla_telegraph_barrage():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        fx_, fy_, wa = [(68, 62, 1.0), (60, 58, 0.25), (62, 52, -0.55), (64, 49, -1.1), (65, 48, -1.4),
                        (66, 48, -1.5)][i]
        p = G(fist=(fx_, fy_), wpn=wa, lean=-0.05 * t, glow=0.6 + 0.8 * t,
              wglow=0.5 + 1.1 * t, core=0.6 + 0.6 * t, eye=0.7 + 0.7 * t, bristle=1.0 * t, cape_wave=math.sin(i),
              t=i / 6)
        f = K.draw_glaciarch(p)
        guard, tip, u = K.sword_geo(p.fist, p.wpn)
        if i >= 1:
            X.converge(f, (guard[0] + tip[0]) / 2, (guard[1] + tip[1]) / 2, 3 + 2 * i, t, C.FROSTS[:4],
                       seed=370 + i, r_max=20)
        # ice shards crystallising in an arc beside the blade
        for k in range(max(0, i - 1)):
            sx = guard[0] + 10 + math.cos(-1.9 + k * 0.45) * 14
            sy = guard[1] - 14 + math.sin(-1.9 + k * 0.45) * 14
            shard(f, sx, sy, -1.9 + k * 0.45, 3.5 + 0.5 * (i - k), C.CRYST)
        out.append(f)
    return out


def gla_barrage():
    a = [G(fist=(66, 48), wpn=-1.2, lean=0.02, glow=1.5, wglow=1.6, core=1.3, eye=1.4, bristle=1.0, cape_wave=-1),
         G(fist=(62, 56), wpn=0.15, lean=0.12, glow=1.3, wglow=1.4, core=1.2, eye=1.3, bristle=0.5, cape_wave=-2,
           cape_lift=0.3),
         G(fist=(64, 64), wpn=0.95, lean=0.14, glow=1.0, wglow=1.0, core=1.0, eye=1.0, cape_wave=-1),
         G(fist=(68, 64), wpn=1.25, lean=0.06, glow=0.8, wglow=0.7, core=0.8, eye=0.8)]
    out = [K.draw_glaciarch(p) for p in a]
    guard, tip, _ = K.sword_geo(a[0].fist, a[0].wpn)
    FX.burst(out[0], tip[0], tip[1] + 2, 4, C.FROSTS[:3], seed=380, jag=0.35)
    # slash arc
    f = out[1]
    for j in range(3):
        for (x, y) in FX.ring_pixels(56, 54, 34 - j * 2):
            ang = math.atan2(y - 54, x - 56)
            if -1.1 < ang < 0.2 and X.empty_at(f, x, y) and (x + y + j) % (j + 1) == 0:
                f[y, x] = [C.WHITE, C.FROST, C.ICE[1]][j]
    for k, (x, y, ang) in enumerate(((82, 28, -0.7), (88, 38, -0.45), (91, 48, -0.2), (78, 20, -0.95))):
        shard(f, x, y, ang, 6, C.CRYST, trail=[C.FROST, C.ICE[2], C.ICE[1]])
    FX.burst(f, 86, 46, 4, C.FROSTS[:3], seed=381, jag=0.4)
    for (x, y, ang) in ((90, 14, -0.8), (94, 30, -0.5)):
        shard(out[2], x, y, ang, 5, C.CRYST, trail=[C.ICE[2], C.ICE[1]])
    X.rising(out[2], 70, 94, 20, 60, 6, 0.3, C.FROSTS[1:], seed=382, rise=-6)
    return out


def gla_summon():
    a = [G(fist=(68, 46), wpn=1.45, lean=-0.04, glow=1.0, wglow=1.2, core=1.0, eye=1.1, bristle=0.4, cape_wave=-1),
         G(fist=(70, 68), wpn=1.45, lean=0.14, hip=(48, 65), glow=1.3, wglow=1.6, core=1.2, eye=1.2, bristle=0.6),
         G(fist=(70, 68), wpn=1.45, lean=0.14, hip=(48, 65), glow=1.4, wglow=1.5, core=1.4, eye=1.3, bristle=0.8),
         G(fist=(70, 68), wpn=1.45, lean=0.12, hip=(48, 65), glow=1.5, wglow=1.4, core=1.5, eye=1.4, bristle=1.0),
         G(fist=(70, 68), wpn=1.45, lean=0.12, hip=(48, 65), glow=1.2, wglow=1.1, core=1.2, eye=1.2, bristle=0.7),
         G(fist=(70, 66), wpn=1.42, lean=0.04, glow=0.8, wglow=0.7, core=0.8, eye=0.9)]
    out = [K.draw_glaciarch(p) for p in a]
    for i in range(1, 5):
        f = out[i]
        frost_patch(f, 74, 8 + i * 10, seed=390, grow=0.6 + 0.25 * i)
        X.rising(f, 74 - 9 * i, 74 + 9 * i, 74, 94, 4 + 3 * i, i / 6,
                 [C.WHITE, C.AURORA[0], C.AURORA[1], C.FROST], seed=391 + i, rise=16)
    frost_patch(out[5], 74, 46, seed=390, grow=0.4, cols=[C.ICE[3], C.ICE[2], C.ICE[1]])
    FX.burst(out[1], 75, 92, 6, C.FROSTS[:4], seed=395, jag=0.45)
    FX.burst(out[1], 66, 93, 4, C.DUST_G, seed=396, jag=0.5)
    out[1] = X.shake(out[1], 0, 1)
    return out


def gla_roar():
    out = []
    keys = [(0.2, 0.0, 0.7, 0.08, 0), (0.3, 0.0, 0.9, 0.12, 0), (-0.45, 1.0, 1.4, -0.12, 1),
            (-0.5, 1.0, 1.5, -0.14, -1), (-0.5, 1.0, 1.5, -0.14, 1), (-0.45, 1.0, 1.4, -0.12, -1),
            (-0.2, 0.5, 1.0, -0.04, 0), (0.0, 0.0, 0.7, 0.0, 0)]
    for i, (ht, jaw, core, lean, sh) in enumerate(keys):
        wa = [1.42, 1.3, -1.5, -1.5, -1.5, -1.5, 0.7, 1.4][i]
        p = G(head_tilt=ht, jaw=jaw, core=core, glow=0.5 + core * 0.6, eye=0.6 + core * 0.6, lean=lean,
              fist=(68, 64 - 14 * jaw), wpn=wa, hand_b=(28, 58 - 8 * jaw), bristle=1.2 * jaw,
              wglow=0.4 + 0.8 * jaw, cape_lift=0.5 * jaw, cape_wave=sh * 2, t=i / 8)
        f = K.draw_glaciarch(p)
        if jaw > 0.5:
            hx, hy = K.glaciarch_head(p)
            X.rings(f, hx + 1, hy, (6 + (i % 2) * 2, 11 + (i % 2) * 2, 16 + (i % 2) * 2),
                    [C.WHITE, C.FROST, C.AURORA[1]], a0=-1.3, a1=0.9)
            for k in range(3):
                X.puff(f, hx + 4 + k * 4, hy + 4 + k, 1.2 + k * 0.6, [C.WHITE, C.SNOW[1], C.ICE[2]], seed=400 + i + k)
            X.rising(f, 10, 90, 4, 60, 10, i / 8, C.SNOWS, seed=405, rise=-12, sway=3)
            f = X.shake(f, sh, 0)
        out.append(f)
    return out


def frost_beam(f, x0, y0, i, x1=W, seed=0):
    cols = [C.WHITE, C.ICE[3], C.FROST, C.ICE[1], C.ICE[0]]
    hw = [3, 4, 3, 3][i % 4]
    for x in range(int(x0), x1):
        d = x - x0
        w_ = hw + (1 if (d + i * 3) % 11 < 3 else 0)
        for dy in range(-w_ - 1, w_ + 2):
            y = int(y0) + dy
            if not (0 <= y < H):
                continue
            e = abs(dy)
            if e <= w_ - 2:
                c = cols[0]
            elif e <= w_ - 1:
                c = cols[1]
            elif e <= w_:
                c = cols[2]
            else:
                c = cols[3] if (x + i) % 3 else None
            if c is not None:
                f[y, x] = c
    # crystal shards growing along the beam edges + mist
    rng = np.random.default_rng(seed + i)
    for k in range(5):
        x = int(x0 + 8 + rng.random() * (x1 - x0 - 10))
        side = -1 if k % 2 else 1
        shard(f, x, y0 + side * (hw + 3), -math.pi / 2 * side + 0.6 * (rng.random() - 0.5), 4 + 2 * rng.random(),
              C.CRYST)
    for _ in range(10):
        x = int(x0 + 4 + rng.random() * (x1 - x0 - 4))
        y = int(y0 + (rng.random() - 0.5) * (hw * 2 + 12))
        if X.empty_at(f, x, y):
            f[y, x] = C.SNOW[1]
    return f


def gla_telegraph_laser():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = G(lean=-0.06 * t, chest_open=t, core=0.8 + 0.8 * t, eye=0.8 + 0.6 * t, glow=0.7 + 0.8 * t,
              fist=(66 - 8 * t, 64 - 36 * t), wpn=1.42 - (1.42 + 2.1) * t, hand_b=(30, 66), cape_wave=-t,
              cape_lift=0.3 * t, bristle=0.6 * t, wglow=0.5 + 0.8 * t, t=i / 6)
        f = K.draw_glaciarch(p)
        cx, cy = K.glaciarch_core(p)
        X.converge(f, cx, cy, 6 + 3 * i, t, C.FROSTS[:4], seed=410 + i, r_max=26)
        if i >= 2:
            X.rings(f, cx, cy, (13 - 1.5 * i,), [C.FROST], a0=-math.pi, a1=math.pi, dotted=True)
        if i == 5:
            FX.sparkle(f, int(cx), int(cy), 6, C.WHITE, C.FROST)
        out.append(f)
    return out


def gla_laser():
    out = []
    for i in range(4):
        p = G(lean=-0.06, chest_open=1.0, core=1.6, eye=1.4, glow=1.4, fist=(58, 28), wpn=-2.68, hand_b=(30, 66),
              cape_wave=math.sin(i * 1.6) - 1, cape_lift=0.4, bristle=0.6, wglow=1.3, t=i / 4)
        f = K.draw_glaciarch(p)
        cx, cy = K.glaciarch_core(p)
        # frost beam drawn by the game (textured beam, 2.8): only the muzzle here
        FX.burst(f, cx + 3, cy, 5 + (i % 2), C.FROSTS[:3], seed=425 + i, jag=0.3)
        out.append(f)
    return out


def gla_hurt():
    a = X.flash(K.draw_glaciarch(G(lean=-0.1, hip=(45, 63), head_tilt=-0.2, core=1.0, glow=1.2, cracks=1,
                                   fist=(68, 63), wpn=1.3, cape_wave=2)), [C.G_OUT], amount=0.6)
    b = K.draw_glaciarch(G(lean=-0.06, hip=(46, 63), head_tilt=-0.1, core=0.8, glow=0.9, fist=(69, 64), wpn=1.38,
                           cape_wave=1))
    return [a, b]


def _gla_rubble(t):
    cv = TCanvas(W, H)
    rocks = [(46, 93, 10, 3.6, C.ICE_M), (35, 94, 6, 2.6, C.ICE_DK), (58, 93, 7, 3.0, C.STL_DK),
             (42, 90, 4, 2.6, C.STL), (53, 90, 5, 2.8, C.ICE_DK), (68, 94, 5, 2.2, C.ICE_M)]
    for i, (x, y, rx, ry, mat) in enumerate(rocks):
        cv.part()
        yy = 96 - (96 - y) * t - 0.5
        cv.poly([(x - rx, yy + ry), (x - rx * 0.4, yy - ry * max(0.3, t)), (x + rx * 0.5, yy - ry * max(0.3, t) * 0.8),
                 (x + rx, yy + ry)], mat, bevel=1.2, base=0.6)
    for (x, ln, a) in ((40, 7, -1.75), (51, 9, -1.4), (60, 5, -1.2)):
        cv.part()
        K.crystal(cv, (x, 95.5), a, ln * t, 1.4, C.ICE_M)
    return cv.render()


def gla_death():
    seq = [
        G(lean=-0.12, head_tilt=-0.35, jaw=0.8, core=1.6, glow=1.6, eye=1.5, cracks=1, fist=(68, 60), wpn=1.2,
          cape_wave=2, cape_lift=0.3),
        G(lean=-0.04, head_tilt=-0.15, core=0.5, glow=1.2, eye=0.8, cracks=2, hip=(46, 65), fist=(69, 64), wpn=1.35),
        G(lean=0.08, head_tilt=0.15, core=1.4, glow=1.4, eye=0.5, cracks=2, hip=(46, 69), fist=(70, 70), wpn=1.42,
          foot_f=(61, 95), foot_b=(33, 95), hand_b=(33, 72), broken_weapon=0.45),
        G(lean=0.18, head_tilt=0.3, core=1.6, glow=1.6, eye=0.3, cracks=2, hip=(46, 75), fist=(70, 76), wpn=1.42,
          foot_f=(63, 95), foot_b=(31, 95), hand_b=(33, 80), broken_weapon=0.6),
    ]
    out = [K.draw_glaciarch(p) for p in seq]
    out[0] = X.flash(out[0], [C.G_OUT], amount=0.5)
    for (x, y, sz) in ((80, 76, 2), (84, 84, 2), (77, 88, 1)):  # blade shards
        X.chunk(out[2], x, y, sz, C.ICE[3], C.ICE[1], C.G_OUT)
    cx, cy = K.glaciarch_core(seq[2])
    FX.sparkle(out[2], int(cx), int(cy), 5, C.WHITE, C.FROST)
    cx, cy = K.glaciarch_core(seq[3])
    FX.burst(out[3], cx, cy, 5, C.FROSTS[:4], seed=430, jag=0.4)
    hot = K.draw_glaciarch(seq[3].copy(core=1.6, glow=1.6))
    cold = K.draw_glaciarch(seq[3].copy(core=0.0, glow=0.0, eye=0.0, dead=0.8))
    for k, t in enumerate((0.18, 0.38, 0.62, 0.9)):
        src = hot if k < 2 else cold
        edge = [C.WHITE, C.ICE[3], C.FROST] if k < 2 else [C.ICE[2], C.ICE[1]]
        f = X.shatter(src, t, seed=440, out_col=C.G_OUT, edge_cols=edge, n=16, center=(cx, cy), spread=1.1,
                      pop=1.3, grav=50)
        X.rising(f, 20, 80, 30, 92, 10 + 4 * k, k / 4, C.FROSTS[1:], seed=441 + k, rise=12)
        if k < 2:
            FX.burst(f, cx, cy, 6 - 2 * k, C.FROSTS[:4], seed=445 + k, jag=0.5)
        else:
            FX.burst(f, 30 + 14 * k, 93, 3 + k, C.DUST_G, seed=447 + k, jag=0.5)
        out.append(f)
    pile = X.shatter(cold, 1.0, seed=440, out_col=C.G_OUT, edge_cols=[C.ICE[2], C.ICE[1]], n=16, center=(cx, cy),
                     spread=1.1, pop=1.3, grav=50)
    for k, t in enumerate((0.2, 0.36, 0.52, 0.68, 0.84, 1.0)):
        f = X.dissolve(pile, min(1.0, t), C.G_OUT, seed=450, colors=[C.WHITE, C.ICE[3], C.FROST, C.ICE[1]],
                       rise=20, drift=6, top_first=True, life=0.4, particle_rate=0.14, bounds=(0, 0, W, 92))
        FX.under(f, _gla_rubble(min(1.0, 0.4 + t * 0.7)))
        X.rising(f, 24, 74, 50, 92, 8 - k, k / 5, C.SNOWS, seed=455 + k, rise=14)
        out.append(f)
    last = _gla_rubble(1.0)
    for (x, y) in ((44, 72), (52, 64), (48, 56), (57, 78)):
        last[y, x] = C.FROST
    out[-1] = last
    return out


def glaciarch_clips():
    return assemble([
        ("idle", gla_idle()), ("walk", gla_walk()), ("telegraph_slam", gla_telegraph_slam()), ("slam", gla_slam()),
        ("telegraph_charge", gla_telegraph_charge()), ("charge", gla_charge()),
        ("telegraph_barrage", gla_telegraph_barrage()), ("barrage", gla_barrage()), ("summon", gla_summon()),
        ("roar", gla_roar()), ("telegraph_laser", gla_telegraph_laser()), ("laser", gla_laser()),
        ("hurt", gla_hurt()), ("death", gla_death())])
