"""Wyrm-set clips (content contract section 3) for Ignivore (fire) and Cryowyrm (ice).

Clip order / counts / events:
idle 6 (loop), move 6 (loop), roar 8, hurt 2, death 14, telegraph_breath 6, breath 4 (loop, stream from the
mouth to the right edge), telegraph_dive 5, dive 4 (loop), telegraph_meteor 6, meteor 4 (fire f1), summon 6.
"""
import math

import numpy as np

import boss_b_fx as X
import boss_b_palette as C
import boss_b_wyrm as Wy
import effects as FX
from boss_b_knight import crystal
from raster import TCanvas

W, H = Wy.W, Wy.H
PIVOT = Wy.PIVOT
TAU = 2 * math.pi

DURATIONS = {
    "roar": [0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16],
    "hurt": [0.08, 0.14],
    "death": [0.1, 0.12, 0.12, 0.14, 0.1, 0.1, 0.1, 0.12, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3],
    "telegraph_breath": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "telegraph_dive": [0.1, 0.1, 0.12, 0.14, 0.2],
    "telegraph_meteor": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "meteor": [0.06, 0.1, 0.1, 0.12],
    "summon": [0.12, 0.1, 0.12, 0.12, 0.14, 0.16],
}
FPS = {"idle": 8, "move": 10, "breath": 14, "dive": 12}


def assemble(clips):
    out = []
    for name, frames in clips:
        kw = {"loop": name in FPS}
        if name in DURATIONS:
            kw["durations"] = DURATIONS[name]
        else:
            kw["fps"] = FPS[name]
        if name == "meteor":
            kw["events"] = [(1, "fire")]
        out.append((name, frames, kw))
    return out


class Theme:
    def __init__(self, kind):
        self.kind = kind
        fire = kind == "fire"
        self.out = C.P_OUT if fire else C.G_OUT
        self.hot = [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.LAVA[1]] if fire else [C.WHITE, C.ICE[3], C.FROST, C.ICE[1]]
        self.ambient = C.EMBERS[1:4] if fire else C.SNOWS
        self.ring = [C.LAVA[3], C.LAVA[2], C.LAVA[1]] if fire else [C.WHITE, C.FROST, C.AURORA[1]]
        self.smoke = C.ASHES if fire else [C.WHITE, C.SNOW[1], C.ICE[2]]
        self.summon = ([C.LAVA[4], C.LAVA[2], C.OBSID[1], C.LAVA[1]] if fire else
                       [C.WHITE, C.AURORA[0], C.AURORA[1], C.FROST])
        self.rise = 14 if fire else -10      # embers rise, snow falls

    def draw(self, p):
        return Wy.draw(p, self.kind)


def P(name="idle", bl=1.0, other="idle", dx=0.0, dy=0.0, **kw):
    hd, cs = Wy.shape(name, bl, other, dx, dy)
    return Wy.WPose(head=hd, ctrl=cs, **kw)


def aura(f, col, step, n=1):
    """Dotted glowing rim around the silhouette (telegraph warning)."""
    a = f[:, :, 3] > 0
    for _ in range(n):
        g = a.copy()
        g[1:, :] |= a[:-1, :]
        g[:-1, :] |= a[1:, :]
        g[:, 1:] |= a[:, :-1]
        g[:, :-1] |= a[:, 1:]
        ring = g & ~a
        ys, xs = np.nonzero(ring)
        for y, x in zip(ys, xs):
            if (x + y + step) % 2 == 0:
                f[y, x] = col
        a = g
    return f


def ambient(f, th, phase, seed, n=6, box=(10, 120, 20, 90)):
    X.rising(f, box[0], box[1], box[2], box[3], n, phase, th.ambient, seed, rise=th.rise, sway=2)
    return f


def projectile(f, th, x, y, ang, r):
    """Fireball (fire) / frost orb (ice) travelling along `ang` with a trail."""
    c, s = math.cos(ang), math.sin(ang)
    for k in range(1, 9):
        px, py = int(round(x - c * (k + r))), int(round(y - s * (k + r) + math.sin(k * 1.3) * 0.8))
        col = th.hot[min(3, k // 2)]
        if X.empty_at(f, px, py):
            f[py, px] = col
        if k < 5:
            qx, qy = int(round(px - s)), int(round(py + c))
            if X.empty_at(f, qx, qy):
                f[qy, qx] = th.hot[min(3, k // 2 + 1)]
    cv = TCanvas(W, H)
    cv.part()
    if th.kind == "fire":
        cv.disc((x, y), r, C.LAVA_M, bias=0.15)
    else:
        cv.disc((x, y), r, C.FROST_GLOW, bias=0.1)
        cv.part()
        crystal(cv, (x - c * 1, y - s * 1), ang, r + 3, 1.2, C.CRYST)
    X.over(f, cv.render())
    return f


def stream(f, th, x0, y0, i, x1=W, seed=0, spread=0.16):
    """Breath stream from the mouth to the right edge (flame or frost)."""
    if th.kind == "fire":
        cols = [C.LAVA[4], C.LAVA[3], C.LAVA[2], C.LAVA[1], C.LAVA[0]]
    else:
        cols = [C.WHITE, C.ICE[3], C.FROST, C.ICE[1], C.ICE[0]]
    rng = np.random.default_rng(seed + i)
    for x in range(int(x0), x1):
        d = x - x0
        hw = 2.2 + d * spread + 0.8 * math.sin(d * 0.55 + i * 1.7)
        yc = y0 + 1.0 * math.sin(d * 0.3 - i * 1.3) + d * 0.05
        for y in range(int(yc - hw - 2), int(yc + hw + 3)):
            if not (0 <= y < H):
                continue
            e = abs(y + 0.5 - yc) / max(hw, 0.5) + 0.12 * math.sin(x * 1.3 + y * 0.9 + i * 2.1)
            if e > 1.05:
                continue
            ci = 0 if e < 0.3 else (1 if e < 0.55 else (2 if e < 0.8 else 3))
            if d > 22 and e > 0.6:
                ci = min(4, ci + 1)
            f[y, x] = cols[ci]
    for _ in range(14):
        x = int(x0 + 4 + rng.random() * max(1, (x1 - x0 - 4)))
        d = x - x0
        hw = 2.2 + d * spread
        side = -1 if rng.random() < 0.5 else 1
        y = int(y0 + d * 0.05 + side * (hw + 1 + rng.random() * 4))
        if X.empty_at(f, x, y):
            f[y, x] = cols[2 + int(rng.integers(0, 2))]
    if th.kind != "fire":
        for k in range(3):
            x = int(x0 + 6 + k * 7 + (i * 3) % 5)
            if x < x1 - 2:
                cv = TCanvas(W, H)
                cv.part()
                side = -1 if (k + i) % 2 else 1
                crystal(cv, (x, y0 + side * (3 + (x - x0) * spread)), -math.pi / 2 * side + 0.3, 4.0, 1.2, C.CRYST)
                X.over(f, cv.render())
    return f


# ---------------------------------------------------------------- clips
def idle(th):
    out = []
    for i in range(6):
        ph = TAU * i / 6
        bob = [0, -1, -1, 0, 1, 1][i]
        p = P(dy=0, phase=i / 6, t=i / 6, belly=0.55 + 0.15 * math.sin(ph), ang=0.1 + 0.04 * math.sin(ph),
              eye=0.6, amp=3.0)
        p.head = (p.head[0], p.head[1] + bob)
        f = th.draw(p)
        ambient(f, th, i / 6, seed=10)
        out.append(f)
    return out


def move(th):
    out = []
    for i in range(6):
        ph = TAU * i / 6
        bob = [0, -1, -2, -1, 0, 1][i]
        p = P(dx=3, phase=i / 6, t=i / 6, amp=4.5, wl=40, belly=0.7, ang=0.16 + 0.03 * math.sin(ph), eye=0.8,
              sweep=0.7)
        p.head = (p.head[0], p.head[1] + bob)
        f = th.draw(p)
        X.rising(f, 2, 40, 30, 80, 10, i / 6, th.ambient, seed=20, rise=-6 if th.kind == "fire" else 4, sway=3)
        X.speed_lines(f, 21 + i, 4, 30, 80, 2, 20, th.ring[2] if th.kind != "fire" else C.ASH[1])
        out.append(f)
    return out


def roar(th):
    out = []
    keys = [  # rear blend, head angle, jaw, mane, shake
        (0.3, -0.05, 0.0, 0.2, 0), (0.6, 0.1, 0.0, 0.4, 0), (0.9, -0.55, 1.0, 1.3, 1), (1.0, -0.6, 1.0, 1.5, -1),
        (1.0, -0.6, 1.0, 1.5, 1), (0.9, -0.55, 1.0, 1.3, -1), (0.5, -0.25, 0.4, 0.6, 0), (0.15, 0.0, 0.0, 0.2, 0)]
    for i, (bl, ang, jaw, mane, sh) in enumerate(keys):
        p = P("rear", bl, phase=i / 8, t=i / 8, ang=ang, jaw=jaw, mane=mane, eye=0.7 + 0.7 * jaw,
              belly=0.6 + 0.6 * jaw, amp=2.2 + 1.5 * jaw)
        f = th.draw(p)
        ambient(f, th, i / 8, seed=30, n=6 + int(8 * jaw))
        if jaw > 0.5:
            mx, my = Wy.mouth(p, th.kind)
            X.rings(f, mx + 1, my, (8 + (i % 2) * 2, 14 + (i % 2) * 2, 20 + (i % 2) * 2), th.ring, a0=-1.6, a1=0.6)
            X.burst_under(f, mx + 2, my, 3 + (i % 2), th.hot, seed=31 + i, jag=0.4)
            f = X.shake(f, sh, 0)
        out.append(f)
    return out


def hurt(th):
    a = X.flash(th.draw(P(dx=-3, dy=-1, ang=-0.25, jaw=0.5, eye=1.2, cracks=1, belly=1.0, t=0.2)), [th.out],
                amount=0.6)
    b = th.draw(P(dx=-1, ang=-0.08, jaw=0.15, eye=0.8, belly=0.7, t=0.4, phase=0.2))
    return [a, b]


def death(th):
    seq = [
        P("rear", 0.8, ang=-0.7, jaw=1.0, eye=1.5, belly=1.4, cracks=1, mane=1.4, t=0.1),
        P("coil", 0.8, ang=0.2, jaw=0.6, eye=1.0, belly=1.0, cracks=1, t=0.3, phase=0.3, amp=4.0),
        P("coil", 0.6, ang=0.55, jaw=0.4, eye=0.6, belly=1.4, cracks=1, t=0.5, phase=0.5, amp=3.0, dy=3),
        P("coil", 0.4, ang=0.7, jaw=0.3, eye=0.3, belly=1.6, cracks=1, t=0.7, phase=0.7, amp=2.0, dy=6),
    ]
    out = [th.draw(p) for p in seq]
    out[0] = X.flash(out[0], [th.out], amount=0.5)
    for k, f in enumerate(out[1:], 1):
        for (u, s) in ((0.2, 2), (0.45, 3), (0.7, 1)):
            bx, by, _, _, _ = Wy.body_points(seq[k], [u])[0]
            X.burst_under(f, bx, by - 4, 2 + (k + s) % 3, th.hot, seed=40 + k * 7 + s, jag=0.4)
    hot = th.draw(seq[3].copy(belly=1.6, eye=0.6))
    cold = th.draw(seq[3].copy(belly=0.0, eye=0.0, dead=0.8))
    bx, by, _, _, _ = Wy.body_points(seq[3], [0.35])[0]
    edge_hot = th.hot[:3]
    edge_cold = [C.LAVA[0], C.BASALT[2]] if th.kind == "fire" else [C.ICE[1], C.ICE[0]]
    for k in range(9):
        ts = 0.12 + 0.11 * k
        src = hot if k < 3 else cold
        f = X.shatter(src, ts, seed=50, out_col=th.out, edge_cols=edge_hot if k < 3 else edge_cold, n=18,
                      center=(bx, by), spread=0.9, pop=0.9, grav=34, ground=H + 40)
        if k >= 2:
            f = X.dissolve(f, min(1.0, (k - 1) / 7.5), th.out, seed=60, colors=th.hot + [th.smoke[1]], rise=18,
                           drift=5, top_first=False, life=0.45, particle_rate=0.2)
        if k < 2:
            X.burst_under(f, bx, by, 6 - 2 * k, th.hot, seed=70 + k, jag=0.5)
        ambient(f, th, k / 9, seed=80 + k, n=12 - k)
        out.append(f)
    last = np.zeros((H, W, 4), np.uint8)
    for (x, y, c) in ((50, 60, 1), (58, 52, 2), (66, 64, 1), (44, 48, 3), (72, 56, 2), (61, 40, 3)):
        last[y, x] = th.hot[c]
    out.append(last)
    return out


def telegraph_breath(th):
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = P("coil", 0.6 * t, dx=-8 * t, ang=0.1 - 0.4 * t, jaw=0.35 * t, eye=0.7 + 0.8 * t, belly=0.6,
              pulse=(1.0 - t * 1.05, 0.14, 1.1), mane=0.6 * t, phase=i / 6, t=i / 6, amp=2.0)
        f = th.draw(p)
        mx, my = Wy.mouth(p, th.kind)
        X.converge(f, mx + 1, my, 5 + 3 * i, t, th.hot, seed=90 + i, r_max=24)
        if i >= 2:
            X.burst_under(f, mx + 1, my, 1.5 + 0.7 * i, th.hot, seed=95 + i, jag=0.35)
            X.rings(f, mx + 1, my, (11 - i,), [th.hot[2]], a0=-math.pi, a1=math.pi, dotted=True)
        if i == 5:
            FX.sparkle(f, int(mx) + 2, int(my), 5, th.hot[0], th.hot[1])
        ambient(f, th, i / 6, seed=99)
        out.append(f)
    return out


def breath(th):
    out = []
    for i in range(4):
        p = P("coil", 0.6, dx=-8, ang=0.06, jaw=1.0, eye=1.4, belly=1.0, pulse=(0.05, 0.25, 0.8), mane=0.8,
              phase=i / 4, t=i / 4, amp=1.6, sweep=0.3)
        f = th.draw(p)
        mx, my = Wy.mouth(p, th.kind)
        stream(f, th, int(mx), my, i, seed=100)
        X.burst_under(f, mx + 1, my, 4 + (i % 2), th.hot[:3], seed=105 + i, jag=0.3)
        out.append(f)
    return out


def telegraph_dive(th):
    out = []
    for i in range(5):
        t = (i + 1) / 5
        p = P("coil", t, ang=0.1 + 0.25 * t, eye=0.7 + 0.9 * t, belly=0.6 + 0.6 * t, mane=1.0 * t, phase=i / 5,
              t=i / 5, amp=2.2 - 1.2 * t, dx=-2 * t, jaw=0.15 * t)
        f = th.draw(p)
        if i >= 1:
            aura(f, th.hot[2] if i < 3 else th.hot[1], i, n=1 if i < 4 else 2)
        hx, hy = p.head
        X.converge(f, hx + 6, hy, 4 + 3 * i, t, th.hot, seed=110 + i, r_max=30)
        if i >= 2:
            # eye flare
            ex, ey = hx + 13 * math.cos(p.ang), hy + 13 * math.sin(p.ang) - 5
            FX.sparkle(f, int(ex), int(ey), 1 + i // 2, th.hot[0], th.hot[1])
        ambient(f, th, i / 5, seed=118)
        out.append(f)
    return out


def dive(th):
    out = []
    for i in range(4):
        p = P("dive", ang=0.42, eye=1.3, belly=1.0, mane=0.6, sweep=1.0, phase=i / 4, t=i / 4, amp=1.2,
              wl=34)
        f = th.draw(p)
        # trail streaming behind the body (up-left), speed lines along the dive direction
        rng = np.random.default_rng(120 + i)
        for k in range(26):
            u = rng.random()
            bx, by, nx, ny, r = Wy.body_points(p, [u])[0]
            off = r + 1 + rng.random() * 5
            sgn = -1 if rng.random() < 0.6 else 1
            x, y = int(bx + nx * off * sgn - 3 * rng.random()), int(by + ny * off * sgn - 1 * rng.random())
            if X.empty_at(f, x, y):
                f[y, x] = th.hot[int(rng.integers(1, 4))]
        for k in range(7):
            y0 = int(rng.integers(20, 86))
            x0 = int(rng.integers(0, 40))
            for j in range(int(rng.integers(5, 11))):
                x, y = x0 + j, y0 + j // 3
                if X.empty_at(f, x, y):
                    f[y, x] = th.ring[2] if th.kind != "fire" else C.ASH[1]
        out.append(f)
    return out


def telegraph_meteor(th):
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = P("rear", t, ang=0.05 - 0.75 * t, jaw=0.3 * t, eye=0.7 + 0.8 * t, belly=0.6 + 0.3 * t,
              pulse=(0.9 - 0.85 * t, 0.16, 1.2), mane=0.9 * t, phase=i / 6, t=i / 6)
        f = th.draw(p)
        mx, my = Wy.mouth(p, th.kind)
        if i >= 1:
            X.burst_under(f, mx + 1, my - 1, 1.5 + 0.6 * i, th.hot, seed=130 + i, jag=0.35)
        X.converge(f, mx, my, 4 + 3 * i, t, th.hot, seed=135 + i, r_max=22)
        X.rising(f, 40, 120, 10, 60, 4 + 2 * i, i / 6, th.ambient, seed=140, rise=12, sway=2)
        out.append(f)
    return out


def meteor(th):
    keys = [(-0.85, 1.0, 1.4), (-0.7, 1.0, 1.2), (-0.55, 0.6, 0.9), (-0.35, 0.2, 0.7)]
    out = []
    for i, (ang, jaw, gl) in enumerate(keys):
        p = P("rear", 1.0 - 0.15 * i, ang=ang, jaw=jaw, eye=0.6 + 0.6 * gl, belly=0.6 + 0.4 * gl, mane=gl,
              phase=i / 4, t=i / 4)
        f = th.draw(p)
        mx, my = Wy.mouth(p, th.kind)
        if i == 0:
            X.burst_under(f, mx + 2, my - 2, 5, th.hot, seed=150, jag=0.4)
        if i == 1:
            X.burst_under(f, mx + 2, my - 2, 6, th.hot, seed=151, jag=0.45)
            for (a, d, r) in ((-1.35, 12, 3.0), (-1.0, 15, 2.6), (-1.7, 9, 2.4)):
                projectile(f, th, mx + math.cos(a) * d, my + math.sin(a) * d, a, r)
        if i == 2:
            for (a, d, r) in ((-1.35, 28, 3.0), (-1.0, 31, 2.6), (-1.7, 24, 2.4)):
                x, y = mx + math.cos(a) * d, my + math.sin(a) * d
                if y > 3:
                    projectile(f, th, x, y, a, r)
            X.burst_under(f, mx + 1, my - 1, 3, th.hot[1:], seed=152, jag=0.5)
        ambient(f, th, i / 4, seed=155, n=8)
        out.append(f)
    return out


def summon(th):
    out = []
    keys = [(0.3, -0.2, 0.3), (0.6, -0.45, 0.8), (0.7, -0.5, 1.0), (0.7, -0.5, 1.2), (0.6, -0.4, 0.9),
            (0.3, -0.1, 0.4)]
    for i, (bl, ang, g) in enumerate(keys):
        p = P("rear", bl, ang=ang, jaw=0.5 * g, eye=0.6 + 0.6 * g, belly=0.6 + 0.8 * g, mane=g, phase=i / 6,
              t=i / 6, pulse=(0.5, 0.5, 0.6 * g))
        f = th.draw(p)
        # summoning circle of motes under the body, spawn sparks dropping into it
        cx, cy = 64, 88
        if 1 <= i <= 4:
            for (x, y) in FX.ring_pixels(cx, cy, 18 + 2 * i):
                yy = int(cy + (y - cy) * 0.3)
                if X.empty_at(f, x, yy) and (x + i) % 3 != 0:
                    f[yy, x] = th.summon[(x // 3 + i) % len(th.summon)]
            X.motes(f, cx, cy - 6, 6 + 3 * i, 4, 14 + 2 * i, th.summon, seed=160 + i)
            for k in range(3):
                sx = 44 + k * 18 + (i * 5) % 7
                sy = 60 + ((i * 7 + k * 11) % 20)
                for j in range(3):
                    if X.empty_at(f, sx, sy - j * 2):
                        f[sy - j * 2, sx] = th.summon[j]
        ambient(f, th, i / 6, seed=165, n=6 + 2 * i)
        out.append(f)
    return out


def clips(kind):
    th = Theme(kind)
    return assemble([
        ("idle", idle(th)), ("move", move(th)), ("roar", roar(th)), ("hurt", hurt(th)), ("death", death(th)),
        ("telegraph_breath", telegraph_breath(th)), ("breath", breath(th)),
        ("telegraph_dive", telegraph_dive(th)), ("dive", dive(th)),
        ("telegraph_meteor", telegraph_meteor(th)), ("meteor", meteor(th)), ("summon", summon(th))])


def ignivore_clips():
    return clips("fire")


def cryowyrm_clips():
    return clips("ice")
