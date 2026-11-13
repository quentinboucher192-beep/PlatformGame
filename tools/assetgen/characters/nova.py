"""Player sheet: Nova (docs/ASSET_SPEC.md section 3).

32x32 frames, pivot [16, 32], 19 clips (one per row)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import CYAN, VIOLET
from nova_rig import Pose, draw
from raster import Canvas, put
from sheet import Sheet

TAU = 2 * math.pi
GROUND = 30.0  # ankle row when standing (boots cover rows 30-31)


def frames(poses, back=False):
    return [draw(p, back_view=back) for p in poses]


# ----------------------------------------------------------------- idle / walk
def idle():
    out = []
    bob = [0, 0, 0, 1, 1, 0]
    for i in range(6):
        ph = TAU * i / 6
        b = bob[i]
        out.append(Pose(bob=b, scarf_phase=ph, scarf_amp=0.22, scarf_ang=2.9,
                        hand_f=(19.0, 25.0 + b), hand_b=(13.0, 25.0 + b)))
    return frames(out)


WALK_F = [(3, 0), (2, 0), (0, 0), (-2, 0), (-3, 0), (-2, -2), (0, -2), (2, -1)]
WALK_BOB = [0, 1, 0, 0, 0, 1, 0, 0]


def walk():
    out = []
    for i in range(8):
        fx, fy = WALK_F[i]
        bx, by = WALK_F[(i + 4) % 8]
        b = WALK_BOB[i]
        hip = (16.0, 26.0 + b)
        # arms swing opposite to the legs
        out.append(Pose(hip=hip, lean=0.06,
                        foot_f=(16 + fx, GROUND + fy), foot_b=(15 + bx, GROUND + by),
                        toe_f=0.35 if fy < 0 else 0.0, toe_b=0.35 if by < 0 else 0.0,
                        hand_f=(18.5 - 0.9 * fx, 25.0 + b), hand_b=(14.5 - 0.9 * bx, 25.0 + b),
                        scarf_ang=3.0, scarf_curl=0.05, scarf_amp=0.3, scarf_phase=TAU * 2 * i / 8))
    return frames(out)


RUN_F = [(4, 0), (2, 0), (-1, 0), (-4, -1), (-5, -3), (-2, -4), (2, -4), (4, -2)]
RUN_BOB = [0, 1, 0, -1, 0, 1, 0, -1]


def run():
    out = []
    for i in range(8):
        fx, fy = RUN_F[i]
        bx, by = RUN_F[(i + 4) % 8]
        b = RUN_BOB[i]
        hip = (16.0, 26.0 + b)
        s = fx / 5.0  # +1 = front leg forward
        out.append(Pose(hip=hip, lean=0.28, bob=0,
                        foot_f=(16 + fx, GROUND + fy), foot_b=(15 + bx, GROUND + by),
                        toe_f=0.5 if fy < 0 else 0.0, toe_b=0.5 if by < 0 else 0.0,
                        hand_f=(19.0 - 3.5 * s, 23.0 + b + 1.2 * abs(s)),
                        hand_b=(16.0 + 3.5 * s, 23.0 + b + 1.2 * abs(s)),
                        elbow_f=-1, elbow_b=-1,
                        scarf_ang=3.05, scarf_curl=0.0, scarf_amp=0.32, scarf_len=7,
                        scarf_phase=TAU * 2 * i / 8))
    return frames(out)


# ------------------------------------------------------------------- airborne
def jump():
    ps = [
        Pose(hip=(16, 28), lean=0.12, foot_f=(18, 30), foot_b=(14, 30), hand_f=(16, 27), hand_b=(12, 27),
             squash=0.92, scarf_ang=2.6, scarf_curl=0.1, scarf_phase=0.5),
        Pose(hip=(16, 25), lean=0.05, foot_f=(17, 30), foot_b=(15, 30), toe_f=0.7, toe_b=0.8,
             hand_f=(14, 27), hand_b=(11, 26), squash=1.06, scarf_ang=2.1, scarf_curl=0.2, scarf_phase=1.5),
        Pose(hip=(16, 25), lean=0.08, foot_f=(18, 28), foot_b=(15, 30), toe_f=0.5, toe_b=0.7,
             hand_f=(22, 21), hand_b=(11, 22), scarf_ang=2.2, scarf_curl=0.15, scarf_phase=2.6),
    ]
    return frames(ps)


def double_jump():
    """Forward flip: a compact tuck rotated about its own centroid, then an
    open 'spread' pose that is held."""
    tuck = dict(hip=(16, 25), lean=0.45, tilt=0.5, foot_f=(19, 26), foot_b=(17, 27), toe_f=1.0, toe_b=1.0,
                knee_f=1, knee_b=1, hand_f=(20, 24), hand_b=(18, 25), scarf_len=0)
    ref = draw(Pose(**tuck))
    ys, xs = np.nonzero(ref[:, :, 3])
    cx, cy = xs.mean() + 0.5, ys.mean() + 0.5
    tuck["scarf_len"] = 6
    rots = [0.5, 1.6, 2.7, 3.8, 5.0]
    out = []
    for i, r in enumerate(rots):
        # scarf root position relative to the spin centre -> trail behind the spin
        root = (-2.0 * math.cos(0.45) + 6.4 * math.sin(0.45), -6.4 * math.cos(0.45) - 2.0 * math.sin(0.45))
        rx, ry = root[0] + 16 - cx, root[1] + 25 - cy
        phi = math.atan2(ry, rx) + r
        out.append(Pose(rot=r, rot_c=(cx, cy), offset=(16.0 - cx, 20.5 - cy),
                        scarf_ang=phi - 1.2, scarf_curl=0.1, scarf_amp=0.15, scarf_phase=i * 1.2, **tuck))
    out.append(Pose(hip=(16, 25), lean=0.05, foot_f=(19, 29), foot_b=(14, 30), toe_f=0.6, toe_b=0.8,
                    hand_f=(22, 20), hand_b=(10, 21), scarf_ang=2.0, scarf_curl=0.15, scarf_phase=4.0))
    return frames(out)


def fall():
    out = []
    for i in range(3):
        w = [0, 1, 0][i]
        out.append(Pose(hip=(16, 25), lean=-0.05, foot_f=(19, 29), foot_b=(14, 30), toe_f=0.6, toe_b=0.9,
                        hand_f=(22, 20 - w), hand_b=(10, 21 + w),
                        scarf_ang=-2.1, scarf_curl=0.0, scarf_amp=0.35, scarf_phase=TAU * i / 3, scarf_len=6))
    return frames(out)


def land():
    ps = [
        Pose(hip=(16, 28.5), lean=0.22, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 27), hand_b=(11, 26),
             squash=0.9, scarf_ang=-2.0, scarf_curl=0.25, scarf_phase=0.0),
        Pose(hip=(16, 27.5), lean=0.14, foot_f=(19, 30), foot_b=(13, 30), hand_f=(19, 26), hand_b=(12, 26),
             squash=0.96, scarf_ang=2.4, scarf_curl=0.2, scarf_phase=1.0),
        Pose(hip=(16, 26.5), lean=0.05, foot_f=(18, 30), foot_b=(14, 30), hand_f=(19, 25.5), hand_b=(13, 25.5),
             scarf_ang=2.7, scarf_curl=0.12, scarf_phase=2.0),
    ]
    return frames(ps)


# ------------------------------------------------------------- ground moves
def slide():
    ps = [
        Pose(hip=(17, 28.5), lean=-0.55, foot_f=(23, 30), foot_b=(19, 30), knee_b=1, hand_f=(22, 24),
             hand_b=(12, 28), scarf_ang=3.0, scarf_curl=0.05, scarf_amp=0.2, scarf_phase=0.0, scarf_len=5),
        Pose(hip=(19, 29.5), lean=-1.15, foot_f=(26, 30), foot_b=(23, 29), toe_b=0.2, hand_f=(22, 25),
             hand_b=(9, 31), scarf_ang=3.35, scarf_curl=0.0, scarf_amp=0.25, scarf_phase=1.6, scarf_len=7),
        Pose(hip=(19, 29.5), lean=-1.15, foot_f=(26, 30), foot_b=(23, 29), toe_b=0.2, hand_f=(22, 24),
             hand_b=(9, 31), scarf_ang=3.3, scarf_curl=0.0, scarf_amp=0.25, scarf_phase=3.4, scarf_len=7),
    ]
    return frames(ps)


def wall_slide():
    out = []
    for i in range(2):
        j = i
        out.append(Pose(facing=-1, hip=(17, 26), lean=0.12, foot_f=(20, 29 - j), foot_b=(14, 30),
                        toe_f=0.4, hand_b=(11, 19 + j), hand_f=(21, 24), elbow_b=1,
                        scarf_ang=-1.35, scarf_curl=0.0, scarf_amp=0.35, scarf_phase=math.pi * i, scarf_len=6))
    return frames(out)


def climb():
    out = []
    keys = [
        dict(hand_b=(9, 13), hand_f=(22, 20), foot_b=(14, 30), foot_f=(18, 27), bob=0),
        dict(hand_b=(9, 16), hand_f=(22, 17), foot_b=(14, 29), foot_f=(18, 29), bob=-1),
        dict(hand_b=(9, 20), hand_f=(22, 13), foot_b=(14, 27), foot_f=(18, 30), bob=0),
        dict(hand_b=(9, 17), hand_f=(22, 16), foot_b=(14, 29), foot_f=(18, 29), bob=-1),
    ]
    for i, k in enumerate(keys):
        hip = (16.0, 26.0 + k["bob"])
        out.append(Pose(hip=hip, bob=0, hand_b=k["hand_b"], hand_f=k["hand_f"],
                        foot_b=k["foot_b"], foot_f=k["foot_f"],
                        scarf_ang=1.95, scarf_curl=0.05, scarf_amp=0.25, scarf_phase=TAU * i / 4, scarf_len=6))
    return frames(out, back=True)


# ------------------------------------------------------------------- combat
def _arc(img, c, r0, r1, a0, a1, fade_to=1.0, over=False):
    """Slash smear: a crescent between radii r0..r1 sweeping from a0 (tail)
    to a1 (head).  Thickest just behind the head, bright on the outer edge."""
    h, w = img.shape[:2]
    cols = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]
    thick = r1 - r0
    for y in range(h):
        for x in range(w):
            dx, dy = x + 0.5 - c[0], y + 0.5 - c[1]
            d = math.hypot(dx, dy)
            a = math.atan2(dy, dx)
            while a < a0:
                a += TAU
            if a > a1:
                continue
            t = (a - a0) / max(1e-3, (a1 - a0))  # 0 at tail, 1 at head
            if t < 1 - fade_to:
                continue
            prof = math.sin(math.pi * min(1.0, t * 0.9 + 0.1)) ** 0.6
            inner = r1 - thick * prof
            if d > r1 or d < inner:
                continue
            k = (r1 - d) / max(0.5, r1 - inner)  # 0 outer edge .. 1 inner edge
            ci = 0 if k < 0.3 else (1 if k < 0.6 else 2)
            if t < 0.35:
                ci = min(3, ci + 1)
            if over or img[y, x, 3] == 0:
                img[y, x] = cols[ci]
    return img


def attack():
    ps = [
        Pose(lean=-0.12, foot_f=(19, 30), foot_b=(13, 30), hand_f=(11, 21), hand_b=(13, 25), elbow_f=1,
             blade=-2.35, scarf_ang=2.9, scarf_phase=0.0),
        Pose(lean=0.05, foot_f=(20, 30), foot_b=(13, 30), hand_f=(21, 17), hand_b=(13, 24),
             blade=-1.35, scarf_ang=3.0, scarf_phase=1.0),
        Pose(hip=(17, 26.5), lean=0.15, foot_f=(21, 30), foot_b=(13, 30), hand_f=(23, 23), hand_b=(13, 25),
             blade=0.3, blade_len=8, scarf_ang=3.1, scarf_curl=0.02, scarf_phase=2.0, scarf_len=7),
        Pose(hip=(17, 26.5), lean=0.25, foot_f=(21, 30), foot_b=(13, 30), hand_f=(22, 25), hand_b=(13, 25),
             blade=1.0, scarf_ang=3.0, scarf_phase=3.0),
        Pose(hip=(16.5, 26), lean=0.12, foot_f=(20, 30), foot_b=(14, 30), hand_f=(19.5, 25), hand_b=(13, 25),
             blade=1.45, scarf_ang=2.9, scarf_phase=4.0),
    ]
    out = frames(ps)
    # smear crescents (drawn only on empty pixels, i.e. behind the body)
    _arc(out[1], (17.0, 21.0), 10.5, 13.0, -2.5, -1.45, 1.0)
    _arc(out[2], (17.0, 22.0), 11.0, 14.5, -1.45, 0.42, 1.0, over=True)
    _arc(out[3], (17.0, 22.0), 12.5, 14.3, 0.1, 0.75, 0.5, over=False)
    return out


def air_attack():
    base = dict(foot_f=(19, 28), foot_b=(14, 29), toe_f=0.6, toe_b=0.8, hip=(16, 25))
    ps = [
        Pose(lean=-0.1, hand_f=(11, 20), hand_b=(12, 23), elbow_f=1, blade=-2.3, scarf_ang=2.2,
             scarf_phase=0, **base),
        Pose(lean=0.15, hand_f=(23, 23), hand_b=(12, 22), blade=0.5, blade_len=8, scarf_ang=2.4,
             scarf_phase=1.2, **base),
        Pose(lean=0.25, hand_f=(21, 26), hand_b=(12, 22), blade=1.35, scarf_ang=2.4, scarf_phase=2.4, **base),
        Pose(lean=0.1, hand_f=(19, 25), hand_b=(12, 23), blade=1.5, scarf_ang=2.3, scarf_phase=3.6, **base),
    ]
    out = frames(ps)
    _arc(out[1], (17.0, 21.0), 11.0, 14.5, -1.5, 0.6, 1.0, over=True)
    _arc(out[2], (17.0, 21.0), 12.5, 14.3, 0.3, 1.0, 0.5, over=False)
    return out


def cast():
    ps = [
        Pose(lean=-0.1, foot_f=(19, 30), foot_b=(13, 30), hand_f=(15, 22), hand_b=(12, 25), hand_glow=1.6,
             scarf_ang=2.8, scarf_phase=0),
        Pose(hip=(17, 26), lean=0.25, foot_f=(21, 30), foot_b=(13, 30), hand_f=(24, 21), hand_b=(12, 24),
             scarf_ang=3.05, scarf_curl=0.02, scarf_phase=1.2, scarf_len=7),
        Pose(hip=(17, 26), lean=0.2, foot_f=(21, 30), foot_b=(13, 30), hand_f=(23, 21), hand_b=(12, 24),
             scarf_ang=3.0, scarf_phase=2.4),
        Pose(lean=0.08, foot_f=(19, 30), foot_b=(14, 30), hand_f=(20, 23), hand_b=(13, 25),
             scarf_ang=2.9, scarf_phase=3.6),
    ]
    out = frames(ps)
    # palm burst on the release frame
    burst = FX.new(32, 32)
    hx, hy = 26, 20
    FX.sparkle(burst, hx, hy, 3, CYAN[3], CYAN[1])
    for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        put(burst, hx + dx, hy + dy, CYAN[2])
    FX.overlay(out[1], burst)
    small = FX.new(32, 32)
    FX.sparkle(small, 25, 20, 1, CYAN[3], CYAN[2])
    FX.overlay(out[2], small)
    return out


def dash():
    out = []
    for i in range(4):
        p = Pose(hip=(14, 25.5), lean=1.0, foot_f=(13, 28), foot_b=(7, 27), toe_f=0.9, toe_b=1.3,
                 knee_f=1, hand_f=(13, 22), hand_b=(10, 21), squash=0.95,
                 scarf_ang=3.14, scarf_curl=0.0, scarf_amp=0.35, scarf_phase=TAU * i / 4, scarf_len=7,
                 scarf_seg=1.7)
        f = draw(p)
        # speed streaks behind
        rng = np.random.default_rng(40 + i)
        for k in range(3):
            y = int(rng.integers(17, 29))
            x0 = int(rng.integers(0, 4))
            ln = int(rng.integers(3, 7))
            for x in range(x0, x0 + ln):
                if f[y, x, 3] == 0:
                    f[y, x] = CYAN[1] if x > x0 + 1 else CYAN[0]
        out.append(f)
    return out


def hurt():
    ps = [
        Pose(hip=(15, 26), lean=-0.38, head=(-1, 0), tilt=-0.35, foot_f=(18, 29), foot_b=(13, 30), toe_f=0.4,
             hand_f=(21, 18), hand_b=(10, 19), scarf_ang=0.2, scarf_curl=0.1, scarf_amp=0.3, scarf_phase=0,
             visor=M.RED_GLOW),
        Pose(hip=(15, 26.5), lean=-0.25, tilt=-0.2, foot_f=(18, 30), foot_b=(13, 30),
             hand_f=(20, 21), hand_b=(11, 22), scarf_ang=1.0, scarf_curl=0.15, scarf_amp=0.3, scarf_phase=1.5),
        Pose(hip=(16, 26.5), lean=-0.08, foot_f=(18, 30), foot_b=(14, 30),
             hand_f=(19, 24), hand_b=(13, 24), scarf_ang=2.2, scarf_curl=0.15, scarf_phase=3.0),
    ]
    out = frames(ps)
    out[0] = FX.flash(out[0], amount=0.35)
    return out


# --------------------------------------------------------------- death etc.
def _collapsed():
    return [
        Pose(hip=(15, 26), lean=-0.38, head=(-1, 0), tilt=-0.35, foot_f=(18, 29), foot_b=(13, 30), toe_f=0.4,
             hand_f=(21, 18), hand_b=(10, 19), scarf_ang=0.2, scarf_curl=0.1, scarf_amp=0.3, visor=M.RED_GLOW),
        Pose(hip=(16, 28), lean=0.1, tilt=0.4, foot_f=(19, 30), foot_b=(13, 30),
             hand_f=(19, 26), hand_b=(13, 26), scarf_ang=2.0, scarf_curl=0.2, scarf_phase=1.0),
        Pose(hip=(15, 29), lean=0.45, tilt=0.6, head=(0.5, 0.5), foot_f=(20, 30), foot_b=(12, 30), knee_b=1,
             hand_f=(21, 29), hand_b=(16, 29), scarf_ang=1.9, scarf_curl=0.25, scarf_phase=2.0),
        Pose(hip=(15, 29.5), lean=0.75, tilt=0.7, head=(0.5, 1.5), foot_f=(20, 30), foot_b=(12, 30),
             hand_f=(23, 30), hand_b=(18, 30), scarf_ang=1.8, scarf_curl=0.3, scarf_phase=2.5),
    ]


def death():
    ps = _collapsed()
    out = frames(ps)
    out[0] = FX.flash(out[0], amount=0.35)
    base = out[3]
    for i, t in enumerate([0.15, 0.32, 0.5, 0.68, 0.86, 1.05]):
        out.append(FX.dissolve(base, t, seed=7, rise=16, drift=3, life=0.5))
    return out


def respawn():
    base = draw(Pose())
    out = []
    ts = [1.2, 1.05, 0.9, 0.75, 0.6, 0.45, 0.3, 0.15, 0.04, 0.0]
    for i, t in enumerate(ts):
        f = FX.dissolve(base, t, seed=11, rise=-14, drift=3, top_first=False, life=0.5)
        # light column, strongest at the start
        col = FX.new(32, 32)
        k = max(0.0, 1.0 - i / 7.0)
        if k > 0:
            wcol = 1 if i < 2 else (2 if i < 5 else 1)
            top = int(2 + 14 * (1 - k)) if i > 2 else 2 + (2 - i) * 6
            for y in range(top, 32):
                for dx in range(-wcol, wcol):
                    x = 16 + dx
                    c = CYAN[3] if dx in (-1, 0) and k > 0.5 else (CYAN[2] if k > 0.3 else CYAN[1])
                    put(col, x, y, c)
            if i <= 3:
                # ground ring
                for dx in range(-5 + i, 6 - i):
                    put(col, 16 + dx, 31, CYAN[1] if abs(dx) > 2 else CYAN[2])
        FX.under(f, col) if i >= 4 else FX.overlay(f, col)
        if i == 8:
            f = FX.flash(f, CYAN[3], 0.35)
        out.append(f)
    return out


def victory():
    ps = [
        Pose(hip=(16, 28), lean=0.15, foot_f=(18, 30), foot_b=(14, 30), hand_f=(18, 27), hand_b=(13, 27),
             squash=0.93, scarf_ang=2.7, scarf_phase=0),
        Pose(hip=(16, 25), offset=(0, -4), foot_f=(17, 30), foot_b=(15, 30), toe_f=0.7, toe_b=0.8,
             hand_f=(20, 16), hand_b=(12, 18), squash=1.05, scarf_ang=1.9, scarf_curl=0.1, scarf_phase=1),
        Pose(hip=(16, 25), offset=(0, -6), foot_f=(18, 28), foot_b=(14, 29), toe_f=0.5, toe_b=0.6,
             hand_f=(23, 12), hand_b=(10, 20), fist=True, scarf_ang=2.3, scarf_phase=2),
        Pose(hip=(16, 25), offset=(0, -6), foot_f=(18, 28), foot_b=(14, 29), toe_f=0.5, toe_b=0.6,
             hand_f=(23, 12), hand_b=(10, 20), fist=True, scarf_ang=-2.6, scarf_curl=0.1, scarf_phase=3),
        Pose(hip=(16, 25), offset=(0, -3), foot_f=(18, 30), foot_b=(14, 30), toe_f=0.5, toe_b=0.6,
             hand_f=(21, 18), hand_b=(11, 19), scarf_ang=-2.3, scarf_phase=4),
        Pose(hip=(16, 28), lean=0.15, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 26), hand_b=(12, 26),
             squash=0.93, scarf_ang=-2.0, scarf_curl=0.2, scarf_phase=5),
        Pose(hip=(16, 26.5), lean=0.05, foot_f=(19, 30), foot_b=(13, 30), hand_f=(19, 25), hand_b=(13, 25),
             scarf_ang=2.4, scarf_curl=0.15, scarf_phase=6),
        Pose(hip=(16, 26), foot_f=(19, 30), foot_b=(13, 30), hand_f=(19, 20), hand_b=(13, 24), fist=True,
             elbow_f=-1, scarf_ang=2.7, scarf_phase=0.5),
        Pose(hip=(16, 26), lean=-0.05, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 8), hand_b=(13, 24),
             fist=True, tilt=-0.2, scarf_ang=2.85, scarf_amp=0.3, scarf_phase=1.6),
        Pose(hip=(16, 26), lean=-0.05, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 8), hand_b=(13, 24),
             fist=True, tilt=-0.2, scarf_ang=2.85, scarf_amp=0.3, scarf_phase=3.2),
    ]
    out = frames(ps)
    for f, (x, y, s) in ((out[8], (23, 5, 2)), (out[9], (24, 4, 1))):
        FX.sparkle(f, x, y, s, CYAN[3], CYAN[1])
    return out


def interact():
    ps = [
        Pose(lean=0.05, foot_f=(19, 30), foot_b=(14, 30), hand_f=(20, 23), hand_b=(13, 25), scarf_phase=0),
        Pose(hip=(16.5, 26), lean=0.15, foot_f=(20, 30), foot_b=(14, 30), hand_f=(23, 22), hand_b=(13, 25),
             scarf_phase=1.0),
        Pose(hip=(17, 26), lean=0.2, foot_f=(20, 30), foot_b=(14, 30), hand_f=(24, 22), hand_b=(13, 25),
             scarf_phase=2.0),
        Pose(hip=(16.5, 26), lean=0.1, foot_f=(19, 30), foot_b=(14, 30), hand_f=(21, 23), hand_b=(13, 25),
             scarf_phase=3.0),
    ]
    out = frames(ps)
    FX.sparkle(out[2], 26, 21, 1, CYAN[3], CYAN[1])
    return out


def build():
    s = Sheet("sprites/player/nova.png", 32, 32, [16, 32])
    s.add("idle", idle(), fps=7, loop=True)
    s.add("walk", walk(), fps=12, loop=True, events=[(0, "footstep"), (4, "footstep")])
    s.add("run", run(), fps=15, loop=True, events=[(0, "footstep"), (4, "footstep")])
    s.add("jump", jump(), durations=[0.06, 0.08, 0.1], loop=False)
    s.add("double_jump", double_jump(), durations=[0.05, 0.05, 0.05, 0.05, 0.05, 0.1], loop=False)
    s.add("fall", fall(), fps=10, loop=True)
    s.add("land", land(), durations=[0.05, 0.06, 0.07], loop=False, next="idle")
    s.add("slide", slide(), durations=[0.05, 0.06, 0.1], loop=False)
    s.add("wall_slide", wall_slide(), fps=10, loop=True)
    s.add("climb", climb(), fps=8, loop=True)
    s.add("attack", attack(), durations=[0.04, 0.05, 0.07, 0.08, 0.10], loop=False, next="idle",
          events=[(2, "hit")])
    s.add("air_attack", air_attack(), durations=[0.04, 0.06, 0.08, 0.1], loop=False, events=[(1, "hit")])
    s.add("cast", cast(), durations=[0.06, 0.06, 0.1, 0.12], loop=False, events=[(1, "fire")])
    s.add("dash", dash(), fps=18, loop=True)
    s.add("hurt", hurt(), durations=[0.08, 0.1, 0.12], loop=False)
    s.add("death", death(), durations=[0.12, 0.1, 0.12, 0.25, 0.09, 0.09, 0.09, 0.09, 0.1, 0.12], loop=False)
    s.add("respawn", respawn(), durations=[0.07, 0.07, 0.07, 0.07, 0.07, 0.07, 0.07, 0.07, 0.08, 0.12], loop=False)
    s.add("victory", victory(), durations=[0.1, 0.08, 0.08, 0.1, 0.08, 0.08, 0.1, 0.1, 0.12, 0.2], loop=False)
    s.add("interact", interact(), durations=[0.06, 0.08, 0.14, 0.1], loop=False)
    s.save("animations/nova.json")
    return s
