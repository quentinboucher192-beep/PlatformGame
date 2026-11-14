"""Mini-boss sheet: Thornwarden, ancient guardian (96x96, collision 44x60,
pivot bottom-centre [48, 96]).  14 clips, see docs/ASSET_SPEC.md section 4."""
import math

import numpy as np

import effects as FX
from boss_rig import Pose, draw
from common.palette import CYAN, DANGER, MOSS, SKY, STONE, hex_rgba
from raster import put
from sheet import Sheet

W = H = 96
TAU = 2 * math.pi
DUST = [STONE[4], STONE[3], STONE[2]]
CYANS = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]
REDS = [hex_rgba('#ffe0e6'), DANGER[2], DANGER[1], DANGER[0]]


def P(**kw):
    return Pose(**kw)


def shake(f, dx, dy=0):
    out = np.zeros_like(f)
    h, w = f.shape[:2]
    ys0, ys1 = max(0, dy), h + min(0, dy)
    xs0, xs1 = max(0, dx), w + min(0, dx)
    out[ys0:ys1, xs0:xs1] = f[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
    return out


def motes(f, cx, cy, n, r0, r1, cols, seed, inward=0.0):
    rng = np.random.default_rng(seed)
    for _ in range(n):
        a = rng.random() * TAU
        d = r0 + (r1 - r0) * rng.random()
        d *= (1 - inward)
        x, y = int(cx + math.cos(a) * d), int(cy + math.sin(a) * d)
        if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0:
            f[y, x] = cols[int(rng.integers(0, len(cols)))]
    return f


def core_pos(p):
    c, s = math.cos(p.lean), math.sin(p.lean)
    x, y = 0.5, -15.5
    return (p.hip[0] + x * c - y * s + p.offset[0], p.hip[1] + x * s + y * c + p.offset[1])


# --------------------------------------------------------------------- clips
def idle():
    out = []
    for i in range(6):
        b = [0, 0, 1, 1, 1, 0][i]
        ph = TAU * i / 6
        out.append(draw(P(hip=(47, 66 + b), fist=(72, 74 + b), hand_b=(31, 66 + b), core=0.55 + 0.25 * math.sin(ph),
                          buds=0.4 + 0.3 * math.sin(ph), cloak_wave=math.sin(ph), eye=0.6)))
    return out


def walk():
    out = []
    # stride keys: front foot x/lift, back foot x/lift, body bob
    keys = [(62, 0, 33, 0, 0), (60, 0, 36, 3, 1), (56, 0, 41, 5, 0), (52, 0, 46, 2, -1),
            (49, 0, 50, 0, 0), (52, 3, 46, 0, 1), (57, 5, 41, 0, 0), (61, 2, 36, 0, -1)]
    for i, (fx, fl, bx, bl, bob) in enumerate(keys):
        ph = TAU * i / 8
        out.append(draw(P(hip=(47 + 0.5 * math.sin(ph * 2), 66 + bob), foot_f=(fx, 95 - fl), foot_b=(bx, 95 - bl),
                          fist=(72 - 3 * math.sin(ph), 74 + bob), hand_b=(31 + 3 * math.sin(ph), 66 + bob),
                          lean=0.04, cloak_wave=1.5 * math.sin(ph), core=0.6, buds=0.5)))
    return out


def telegraph_slam():
    out = []
    path = [(72, 70), (73, 56), (71, 40), (66, 27), (61, 18), (58, 15)]
    for i, fist in enumerate(path):
        t = (i + 1) / 6
        f = draw(P(fist=fist, lean=-0.12 * t, hip=(46, 66 - t), core=0.6 + 0.6 * t, eye=0.6 + 0.9 * t,
                   bristle=0.3 * t, buds=0.5 + 0.5 * t, head_tilt=-0.1 * t, cloak_wave=-t, fist_rot=-0.3 * t,
                   foot_f=(60, 95), foot_b=(34, 95)))
        if i >= 3:
            motes(f, fist[0] + 4, fist[1], 4 + 3 * i, 10, 15, CYANS[:3], seed=20 + i, inward=0.3 * t)
        out.append(f)
    FX.sparkle(out[-1], 52, 8, 4, CYAN[3], CYAN[1])
    return out


def slam():
    a = [
        P(fist=(61, 15), lean=-0.1, hip=(46, 65), core=1.2, eye=1.4, bristle=0.3, fist_rot=-0.2,
          foot_f=(60, 95), foot_b=(34, 95)),
        P(fist=(82, 34), lean=0.12, hip=(48, 66), core=1.0, eye=1.3, bristle=0.2, foot_f=(62, 95), foot_b=(34, 95)),
        P(fist=(84, 70), lean=0.28, hip=(50, 69), core=0.9, eye=1.2, foot_f=(63, 95), foot_b=(34, 95)),
        P(fist=(80, 84), lean=0.34, hip=(51, 71), core=0.8, eye=1.1, foot_f=(63, 95), foot_b=(34, 95)),
        P(fist=(80, 84), lean=0.32, hip=(51, 71), core=0.7, eye=1.0, foot_f=(63, 95), foot_b=(34, 95)),
        P(fist=(76, 76), lean=0.16, hip=(49, 68), core=0.6, eye=0.8, foot_f=(61, 95), foot_b=(35, 95)),
    ]
    out = [draw(p) for p in a]
    # smear on the swing
    for (x, y) in ((70, 14), (76, 20), (80, 27), (74, 16), (79, 24)):
        out[1][y:y + 2, x:x + 2] = CYAN[1] if out[1][y, x, 3] == 0 else out[1][y, x]
    # impact: dust walls, debris, cracks
    imp = out[3]
    FX.burst(imp, 88, 92, 7, DUST, seed=3, jag=0.45)
    FX.burst(imp, 70, 93, 5, DUST, seed=4, jag=0.45)
    for (x, y, sz) in ((92, 76, 3), (66, 80, 2), (89, 70, 2), (76, 72, 2), (94, 84, 2)):
        FX.chunk(imp, x, y, sz, STONE[3], STONE[1])
    imp[95, 60:95] = np.where(imp[95, 60:95, 3:4] == 0, np.array(CYAN[1], np.uint8), imp[95, 60:95])
    out[3] = shake(imp, 0, 1)
    f4 = out[4]
    FX.burst(f4, 90, 90, 6, DUST[1:], seed=5, jag=0.5)
    FX.burst(f4, 68, 92, 4, DUST[1:], seed=6, jag=0.5)
    for (x, y, sz) in ((94, 66, 2), (60, 74, 2), (86, 60, 1)):
        FX.chunk(f4, x, y, sz, STONE[3], STONE[1])
    FX.burst(out[5], 90, 92, 3, DUST[2:], seed=7, jag=0.5)
    return out


def telegraph_charge():
    out = []
    for i in range(4):
        t = (i + 1) / 4
        f = draw(P(lean=0.18 + 0.12 * t, hip=(45 - 2 * t, 67 + 2 * t), fist=(68 + 3 * t, 62 - 4 * t),
                   foot_f=(60, 95), foot_b=(30 - (i % 2) * 2, 95 - (i % 2) * 2), core=0.7 + 0.4 * t,
                   eye=0.8 + 0.6 * t, head_tilt=0.15 * t, bristle=0.4 * t, cloak_wave=-1 - t, elbow_dir=1.0))
        # back foot scraping dust
        FX.burst(f, 26, 93, 2 + i, DUST[1:], seed=30 + i, jag=0.5)
        out.append(f)
    return out


def charge():
    out = []
    keys = [(66, 0, 30, 2, 0), (62, 0, 36, 6, 1), (56, 3, 44, 3, 0), (50, 6, 50, 0, -1),
            (56, 3, 44, 0, 0), (62, 1, 36, 3, 1)]
    keys = [(66, 0, 30, 3, 0), (61, 0, 38, 7, 1), (54, 4, 46, 2, -1), (46, 7, 54, 0, 0), (40, 3, 60, 0, 1),
            (58, 1, 34, 4, -1)]
    feet = [((66, 95), (30, 92)), ((61, 95), (39, 88)), ((54, 91), (47, 93)), ((47, 88), (54, 95)),
            ((42, 92), (60, 95)), ((58, 93), (36, 91))]
    for i, ((ff, fb)) in enumerate(feet):
        ph = TAU * i / 6
        bob = [0, 1, 0, -1, 0, 1][i]
        f = draw(P(lean=0.32, hip=(46, 67 + bob), fist=(74, 60 + bob), elbow_dir=1.0, foot_f=ff, foot_b=fb,
                   core=1.0, eye=1.3, head_tilt=0.15, cloak_lift=1.0, cloak_wave=2 * math.sin(ph),
                   bristle=0.3))
        # dust kicked behind + speed lines
        FX.burst(f, 22 - (i % 3) * 3, 93, 2 + (i % 2), DUST[1:], seed=40 + i, jag=0.5)
        rng = np.random.default_rng(60 + i)
        for k in range(4):
            y = int(rng.integers(40, 88))
            x0 = int(rng.integers(2, 14))
            for x in range(x0, x0 + int(rng.integers(4, 9))):
                if f[y, x, 3] == 0:
                    f[y, x] = SKY[4]
        out.append(f)
    return out


def telegraph_barrage():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        f = draw(P(fist=(74 + 4 * t, 70 - 22 * t), fist_rot=-0.6 * t, lean=-0.05 * t, bristle=1.6 * t,
                   buds=0.5 + 0.9 * t, core=0.7 + 0.4 * t, eye=0.7 + 0.7 * t, cloak_wave=math.sin(i)))
        if i >= 2:
            motes(f, 60, 50, 3 + 2 * i, 14, 26, [hex_rgba('#e8dcc8'), CYAN[2], CYAN[1]], seed=70 + i, inward=0.2 * t)
        out.append(f)
    return out


def _thorn_proj(f, x, y, ang, ln=7):
    c, s = math.cos(ang), math.sin(ang)
    for k in range(ln):
        px, py = int(x + c * k), int(y + s * k)
        col = hex_rgba('#e8dcc8') if k > ln - 3 else hex_rgba('#b8a88e')
        if 0 <= px < W and 0 <= py < H:
            f[py, px] = col
            if k < ln - 2:
                qx, qy = int(x + c * k - s), int(y + s * k + c)
                if 0 <= qx < W and 0 <= qy < H and f[qy, qx, 3] == 0:
                    f[qy, qx] = hex_rgba('#7a6a58')


def barrage():
    a = [
        P(fist=(82, 42), fist_rot=-0.7, lean=0.05, bristle=1.8, buds=1.4, core=1.1, eye=1.4),
        P(fist=(84, 44), fist_rot=-0.5, lean=0.1, bristle=0.4, buds=1.0, core=1.0, eye=1.3),
        P(fist=(80, 50), fist_rot=-0.3, lean=0.04, bristle=0.2, buds=0.7, core=0.8, eye=1.0),
        P(fist=(76, 60), fist_rot=-0.1, lean=0.0, bristle=0.1, buds=0.5, core=0.7, eye=0.8),
    ]
    out = [draw(p) for p in a]
    FX.burst(out[0], 90, 40, 3, CYANS[:3], seed=80, jag=0.3)
    for k, ang in enumerate((-0.5, -0.25, 0.0, -0.75)):
        _thorn_proj(out[1], 84 + 4 * math.cos(ang), 38 + 8 * k * 0.4, ang, 7)
    FX.burst(out[1], 88, 42, 4, CYANS[:3], seed=81, jag=0.4)
    for k, ang in enumerate((-0.5, -0.2)):
        _thorn_proj(out[2], 88, 34 + 6 * k, ang, 5)
    return out


def summon():
    a = [
        P(fist=(68, 32), lean=-0.05, core=0.9, eye=1.0, buds=0.8, bristle=0.4),
        P(fist=(76, 86), lean=0.35, hip=(50, 71), foot_f=(62, 95), foot_b=(32, 95), core=1.1, eye=1.2, buds=1.0),
        P(fist=(76, 86), lean=0.35, hip=(50, 71), foot_f=(62, 95), foot_b=(32, 95), core=1.3, eye=1.3, buds=1.3),
        P(fist=(76, 86), lean=0.33, hip=(50, 71), foot_f=(62, 95), foot_b=(32, 95), core=1.4, eye=1.4, buds=1.5),
        P(fist=(76, 86), lean=0.33, hip=(50, 71), foot_f=(62, 95), foot_b=(32, 95), core=1.2, eye=1.2, buds=1.2),
        P(fist=(72, 74), lean=0.1, hip=(48, 67), core=0.8, eye=0.9, buds=0.8),
    ]
    out = [draw(p) for p in a]
    # glowing roots spreading along the ground from the fist
    for i in range(1, 5):
        f = out[i]
        reach = 8 + i * 9
        for side in (-1, 1):
            x = 78
            y = 94
            for k in range(reach):
                x += side
                y = 94 + (1 if (k // 3) % 2 else 0)
                if 0 <= x < W:
                    f[y, x] = MOSS[2] if f[y, x, 3] == 0 else f[y, x]
                    if k % 7 == 3 and 0 <= y - 1:
                        f[y - 1, x] = CYAN[2]
                        if i >= 3 and y - 2 >= 0:
                            f[y - 2, x] = CYAN[1]
        motes(f, 78, 84, 4 + 2 * i, 4, 12 + i * 2, CYANS[:3], seed=90 + i)
    FX.burst(out[1], 80, 92, 5, DUST, seed=95, jag=0.45)
    return out


def roar():
    out = []
    keys = [  # head tilt, jaw, core, bristle, lean, shake
        (0.25, 0.0, 0.7, 0.2, 0.08, 0), (0.35, 0.0, 0.9, 0.4, 0.12, 0),
        (-0.45, 1.0, 1.4, 1.6, -0.12, 1), (-0.5, 1.0, 1.5, 1.8, -0.14, -1), (-0.5, 1.0, 1.5, 1.8, -0.14, 1),
        (-0.45, 1.0, 1.4, 1.6, -0.12, -1), (-0.2, 0.5, 1.0, 0.8, -0.04, 0), (0.0, 0.0, 0.7, 0.3, 0.0, 0)]
    for i, (ht, jaw, core, br, lean, sh) in enumerate(keys):
        p = P(head_tilt=ht, jaw=jaw, core=core, eye=0.6 + core * 0.6, bristle=br, buds=core, lean=lean,
              fist=(74, 70 - 6 * jaw), hand_b=(26, 58 - 4 * jaw), cloak_lift=0.4 * jaw, cloak_wave=sh * 2)
        f = draw(p)
        if jaw > 0.5:
            # sound rings in front of the head
            hx, hy = 62, 26
            for k, r in enumerate((6 + (i % 2) * 2, 11 + (i % 2) * 2)):
                for (x, y) in FX.ring_pixels(hx, hy, r):
                    a = math.atan2(y - hy, x - hx)
                    if -1.0 < a < 0.9 and 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0:
                        f[y, x] = REDS[1] if k == 0 else REDS[2]
            f = shake(f, sh, 0)
        out.append(f)
    return out


def telegraph_laser():
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = P(lean=-0.06 * t, chest_open=t, core=0.8 + 0.8 * t, eye=0.8 + 0.6 * t, foot_f=(62, 95), foot_b=(31, 95),
              hip=(47, 67), fist=(70 - 4 * t, 76), hand_b=(28, 70), buds=0.6 + 0.6 * t, bristle=0.2 * t)
        f = draw(p)
        cx, cy = core_pos(p)
        motes(f, cx, cy, 6 + 3 * i, 9, 24 - 10 * t, CYANS[:3], seed=100 + i, inward=0.0)
        if i >= 2:
            for (x, y) in FX.ring_pixels(cx, cy, 12 - 1.5 * i):
                if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0 and (x + y) % 2 == 0:
                    f[y, x] = CYAN[1]
        if i == 5:
            FX.sparkle(f, int(cx), int(cy), 6, CYAN[3], CYAN[1])
        out.append(f)
    return out


def laser():
    out = []
    p = P(lean=-0.06, chest_open=1.0, core=1.6, eye=1.4, foot_f=(62, 95), foot_b=(31, 95), hip=(47, 67),
          fist=(66, 76), hand_b=(28, 70), buds=1.2)
    for i in range(4):
        f = draw(p.__class__(**{**p.__dict__, "cloak_wave": math.sin(i * 1.6)}))
        cx, cy = core_pos(p)
        cx, cy = int(round(cx)), int(round(cy))
        # The beam itself is drawn by the game (textured beam, 2.8): only the muzzle here.
        FX.burst(f, cx + 2, cy, 5 + (i % 2), CYANS[:3], seed=110 + i, jag=0.3)
        out.append(f)
    return out


def hurt():
    a = FX.flash(draw(P(lean=-0.1, hip=(45, 66), head_tilt=-0.2, core=1.0, cracks=1)), amount=0.55)
    b = draw(P(lean=-0.06, hip=(46, 66), head_tilt=-0.1, core=0.8))
    return [a, b]


def death():
    seq = [
        P(lean=-0.12, head_tilt=-0.3, core=1.5, eye=1.4, cracks=1, jaw=0.6),
        P(lean=-0.05, head_tilt=-0.2, core=0.4, eye=0.8, cracks=2, hip=(46, 67)),
        P(lean=0.05, head_tilt=0.1, core=1.2, eye=0.5, cracks=2, hip=(46, 69), fist=(74, 80)),
        P(lean=0.14, head_tilt=0.2, core=0.3, eye=0.3, cracks=2, hip=(46, 73), foot_f=(62, 95), foot_b=(34, 95),
          fist=(76, 86)),
        P(lean=0.22, head_tilt=0.3, core=0.6, eye=0.2, cracks=2, hip=(46, 78), foot_f=(64, 95), foot_b=(30, 95),
          fist=(76, 88), hand_b=(30, 80)),
        P(lean=0.3, head_tilt=0.4, core=0.2, eye=0.1, cracks=2, hip=(46, 80), foot_f=(66, 95), foot_b=(28, 95),
          fist=(78, 88), hand_b=(32, 84), dead=0.3),
        P(lean=0.36, head_tilt=0.5, core=0.4, eye=0.0, cracks=2, hip=(46, 81), foot_f=(66, 95), foot_b=(28, 95),
          fist=(78, 88), hand_b=(34, 86), dead=0.6, buds=0.0),
        P(lean=0.42, head_tilt=0.55, core=0.1, eye=0.0, cracks=2, hip=(46, 82), foot_f=(66, 95), foot_b=(28, 95),
          fist=(78, 89), hand_b=(34, 87), dead=0.8, buds=0.0),
    ]
    out = [draw(p) for p in seq]
    out[0] = FX.flash(out[0], amount=0.5)
    # core bursts: crack light
    cx, cy = core_pos(seq[2])
    FX.sparkle(out[2], int(cx), int(cy), 5, CYAN[3], CYAN[1])
    cx, cy = core_pos(seq[4])
    FX.sparkle(out[4], int(cx), int(cy), 4, CYAN[3], CYAN[1])
    base = out[-1]
    # crumble: the body sinks into rubble while the core's cyan light escapes upwards
    for k, t in enumerate((0.22, 0.42, 0.62, 0.8, 0.95, 1.2)):
        f = FX.dissolve(base, min(1.0, t), seed=141, colors=[CYAN[3], CYAN[2], CYAN[1], CYAN[0]], rise=22,
                        drift=5, top_first=True, life=0.35, particle_rate=0.12, bounds=(0, 0, W, 90))
        mound = _rubble(min(1.0, 0.35 + t * 0.8))
        FX.under(f, mound)
        if k < 3:
            FX.burst(f, 30 + 12 * k, 93, 3 + k, DUST[1:], seed=160 + k, jag=0.5)
        out.append(f)
    # final, held frame: settled rubble with the last motes of light
    last = _rubble(1.0)
    for (x, y) in ((44, 70), (53, 62), (49, 54), (58, 74)):
        put(last, x, y, CYAN[1])
    out[-1] = last
    return out


def _rubble(t):
    from raster import TCanvas
    from boss_rig import ARMOR, ARMOR_DK, ARMOR_BK, VINE_DEAD
    cv = TCanvas(W, H)
    rocks = [(48, 92, 14, 5.5), (34, 93, 7, 4), (62, 93, 8, 4.2), (42, 88, 6, 3.6), (56, 88, 7, 4), (74, 93, 6, 3.4),
             (24, 94, 4, 2.4), (50, 85, 5, 3)]
    for i, (x, y, rx, ry) in enumerate(rocks):
        cv.part()
        mat = (ARMOR, ARMOR_DK, ARMOR_BK)[i % 3]
        yy = 96 - (96 - y) * t - 0.5
        cv.ellipse((x, yy + ry * (1 - t)), rx, ry * max(0.3, t), mat, ang=0.1 * (i % 3 - 1))
    cv.part()
    cv.chain([(30, 90), (40, 86), (52, 88), (64, 86), (72, 90)], [1.4] * 5, VINE_DEAD)
    return cv.render()


def build():
    s = Sheet("sprites/enemies/thornwarden.png", W, H, [48, 96])
    s.add("idle", idle(), fps=6, loop=True)
    s.add("walk", walk(), fps=9, loop=True)
    s.add("telegraph_slam", telegraph_slam(), durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("slam", slam(), durations=[0.05, 0.05, 0.05, 0.2, 0.15, 0.15], loop=False, events=[(3, "impact")])
    s.add("telegraph_charge", telegraph_charge(), durations=[0.12, 0.12, 0.14, 0.18], loop=False)
    s.add("charge", charge(), fps=12, loop=True)
    s.add("telegraph_barrage", telegraph_barrage(), durations=[0.1, 0.1, 0.1, 0.1, 0.12, 0.16], loop=False)
    s.add("barrage", barrage(), durations=[0.06, 0.1, 0.1, 0.12], loop=False, events=[(1, "fire")])
    s.add("summon", summon(), durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)
    s.add("roar", roar(), durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)
    s.add("telegraph_laser", telegraph_laser(), durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("laser", laser(), fps=14, loop=True)
    s.add("hurt", hurt(), durations=[0.08, 0.14], loop=False)
    s.add("death", death(), durations=[0.1, 0.12, 0.12, 0.14, 0.14, 0.16, 0.16, 0.18, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3],
          loop=False)
    s.save("animations/thornwarden.json")
    # v2 bosses (docs/ASSET_SPEC.md section 4b), one module per family of rigs.
    import importlib.util
    for name in ("bosses_a", "bosses_b"):
        if importlib.util.find_spec(name) is not None:
            importlib.import_module(name).build()
