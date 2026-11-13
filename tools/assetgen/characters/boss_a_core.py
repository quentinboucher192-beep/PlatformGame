"""Core rig (BOSSES-A): a floating machine boss — a central eye/heart wrapped in
orbiting armour plates that rotate over time, with weapon arms and hatches.
Used by `prime_sentinel` (Sylvanox, boss_a_sentinel.py) and `forge_core`
(Pyralis, boss_a_forge.py).  Each boss module provides a `Boss` object with its
own draw() and effect hooks; this module sequences the clips.

Frames 112x112.  Collision box 48x48 = x 32..80, y 32..80 (centred on the body
centre (56, 56)), so the pivot (bottom-centre of the box) is [56, 80].

Core clip set (contract section 3): idle 6, move 6, roar 8, hurt 2, death 14,
telegraph_beam 6, beam 4 (loop), telegraph_volley 5, volley 4 (fire f1),
telegraph_slam 5, slam 5 (impact f2), summon 6.
"""
import math

import effects as FX
from boss_a_common import TAU, motes, over, shake, split_frame
from common.palette import OUTLINE
from raster import tint_toward
from sheet import Sheet

W = H = 112
PIVOT = [56, 80]
CX, CY = 56.0, 56.0


class CPose:
    def __init__(self, **kw):
        self.dx = 0.0          # body offset (bob, drops)
        self.dy = 0.0
        self.tilt = 0.0        # whole-body lean (+ = nose down / forward)
        self.rot = 0.0         # orbit angle of the armour plates
        self.spin = 0.0        # extra rotation of secondary parts (rings / runes)
        self.prad = 0.0        # plate orbit radius offset (+ = flared out)
        self.eye = 0.6         # eye / heart glow (0..1.7)
        self.lid = 1.0         # eye shutters (1 = open, 0 = closed, >1 = wide)
        self.arm = 0.0         # weapon arms: -1 wound back .. 0 rest .. 1 thrust / raised
        self.recoil = 0.0      # weapon recoil (px)
        self.spread = 0.0      # arms spread apart (beam) 0..1
        self.hatch = 0.0       # summon hatches 0..1
        self.thrust = 1.0      # lift / thruster glow
        self.cracks = 0
        self.dead = 0.0
        self.scatter = 0.0     # plates flying off (death) 0..1
        self.slam = 0.0        # plates folded into a ram below the body 0..1
        self.open = 0.0        # armour opened around the eye / heart (beam) 0..1
        self.flick = 0
        self.wobble = 0.0      # chains / dangling parts swing
        self.__dict__.update(kw)

    def copy(self, **kw):
        d = dict(self.__dict__)
        d.update(kw)
        return CPose(**d)

    @property
    def c(self):
        return (CX + self.dx, CY + self.dy)


def _outline_mask(f):
    return (f[:, :, 0] == OUTLINE[0]) & (f[:, :, 1] == OUTLINE[1]) & (f[:, :, 2] == OUTLINE[2])


# ======================================================================= clips
def idle(b):
    out = []
    for i in range(6):
        ph = TAU * i / 6
        p = CPose(dy=1.6 * math.sin(ph), rot=b.rot_step * i, spin=b.spin_step * i, eye=0.6 + 0.2 * math.sin(ph),
                  flick=i, wobble=math.sin(ph), thrust=1.0 + 0.15 * math.sin(ph * 2))
        f = b.draw(p)
        b.fx("idle", f, i, p)
        out.append(f)
    return out


def move(b):
    out = []
    for i in range(6):
        ph = TAU * i / 6
        p = CPose(dx=1.0, dy=1.2 * math.sin(ph), tilt=0.14, rot=b.rot_step * 1.5 * i, spin=b.spin_step * 1.5 * i,
                  eye=0.7, flick=i, wobble=1.6 + 0.6 * math.sin(ph), thrust=1.7 + 0.2 * math.sin(ph * 2), arm=-0.2)
        f = b.draw(p)
        b.fx("move", f, i, p)
        out.append(f)
    return out


def roar(b):
    out = []
    keys = [  # lid, prad, eye, shake, arm, spread
        (0.5, -2.0, 0.8, 0, -0.3, 0.0), (0.8, 0.0, 1.0, 0, -0.1, 0.2), (1.25, 5.0, 1.6, 1, 0.3, 0.8),
        (1.3, 6.0, 1.7, -1, 0.35, 0.9), (1.3, 6.0, 1.7, 1, 0.35, 0.9), (1.25, 5.0, 1.6, -1, 0.3, 0.8),
        (1.0, 2.0, 1.0, 0, 0.1, 0.3), (1.0, 0.0, 0.7, 0, 0.0, 0.0)]
    for i, (lid, prad, eye, sh, arm, spread) in enumerate(keys):
        p = CPose(lid=lid, prad=prad, eye=eye, arm=arm, spread=spread, rot=b.rot_step * 2 * i,
                  spin=b.spin_step * 2.5 * i, tilt=-0.06 if 2 <= i <= 5 else 0.0, flick=i, wobble=sh * 1.5,
                  thrust=1.0 + 0.6 * (eye > 1.2))
        f = b.draw(p)
        b.fx("roar", f, i, p)
        if sh:
            f = shake(f, sh, 0)
        out.append(f)
    return out


def hurt(b):
    a = CPose(dx=-2, tilt=-0.12, lid=0.5, eye=1.1, cracks=1, rot=b.rot_step * 0.5, prad=2.0, wobble=-2)
    c = CPose(dx=-1, tilt=-0.05, lid=0.8, eye=0.8, rot=b.rot_step, wobble=-1)
    fa = FX.flash(b.draw(a), amount=0.55)
    fc = b.draw(c)
    b.fx("hurt", fc, 1, c)
    return [fa, fc]


def telegraph_beam(b):
    out = []
    for i in range(6):
        t = (i + 1) / 6
        p = CPose(open=t, eye=0.7 + 1.0 * t, lid=1.0 + 0.25 * t, spread=t, rot=b.rot_step * (1 + 3 * t) * i,
                  spin=b.spin_step * (1 + 4 * t) * i, prad=-1.5 * t, tilt=-0.05 * t, dx=-1.5 * t, flick=i,
                  thrust=1.0 + 0.3 * t)
        f = b.draw(p)
        b.fx("telegraph_beam", f, i, p)
        out.append(f)
    return out


def beam(b):
    out = []
    for i in range(4):
        p = CPose(open=1.0, eye=1.7, lid=1.25, spread=1.0, rot=b.rot_step * 4 * i, spin=b.spin_step * 5 * i,
                  prad=-1.5, tilt=-0.05, dx=-2.0 - (i % 2) * 0.5, flick=i, thrust=1.4)
        f = b.draw(p)
        b.fx("beam", f, i, p)
        out.append(f)
    return out


def telegraph_volley(b):
    out = []
    for i in range(5):
        t = (i + 1) / 5
        p = CPose(arm=-t, recoil=0.0, eye=0.7 + 0.5 * t, lid=1.0, rot=b.rot_step * (1 + 2 * t) * i,
                  spin=b.spin_step * (1 + 2 * t) * i, tilt=-0.06 * t, dx=-1.0 * t, flick=i)
        f = b.draw(p)
        b.fx("telegraph_volley", f, i, p)
        out.append(f)
    return out


def volley(b):
    keys = [(0.6, 0.0, 1.2), (1.0, 4.0, 1.4), (0.5, 2.5, 1.0), (0.1, 0.5, 0.8)]
    out = []
    for i, (arm, rec, eye) in enumerate(keys):
        p = CPose(arm=arm, recoil=rec, eye=eye, rot=b.rot_step * (3 + i), spin=b.spin_step * (3 + i),
                  tilt=[0.05, -0.08, -0.04, 0.0][i], dx=[1.0, -2.0, -1.0, 0.0][i], flick=i)
        f = b.draw(p)
        b.fx("volley", f, i, p)
        out.append(f)
    return out


def telegraph_slam(b):
    out = []
    for i in range(5):
        t = (i + 1) / 5
        p = CPose(slam=t, dy=-6.0 * t, tilt=-0.1 * t, eye=0.7 + 0.7 * t, lid=1.0 + 0.2 * t, arm=0.8 * t,
                  rot=b.rot_step * i * (1 - 0.5 * t), spin=b.spin_step * i, flick=i, wobble=-1.5 * t,
                  thrust=1.0 + 0.8 * t)
        f = b.draw(p)
        b.fx("telegraph_slam", f, i, p)
        out.append(f)
    return out


def slam(b):
    keys = [  # dy, tilt, slam, eye, arm
        (-3.0, 0.08, 1.0, 1.4, 0.4), (0.5, 0.12, 1.0, 1.4, -0.4), (1.0, 0.1, 1.0, 1.2, -1.0),
        (0.5, 0.04, 0.7, 1.0, -0.6), (0.0, 0.0, 0.25, 0.8, -0.2)]
    out = []
    for i, (dy, tilt, sl, eye, arm) in enumerate(keys):
        p = CPose(dy=dy, tilt=tilt, slam=sl, eye=eye, arm=arm, rot=b.rot_step * 4.5, spin=b.spin_step * (4 + i),
                  flick=i, wobble=[2, 3, -2, -1, 0][i], thrust=[1.0, 0.4, 0.2, 0.8, 1.0][i])
        f = b.draw(p)
        b.fx("slam", f, i, p)
        if i == 2:
            f = shake(f, 0, 1)
        out.append(f)
    return out


def summon(b):
    keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.15]
    out = []
    for i, h in enumerate(keys):
        p = CPose(hatch=h, eye=0.8 + 0.5 * h, lid=1.0, rot=b.rot_step * i, spin=b.spin_step * i,
                  prad=2.0 * h, tilt=-0.04 * h, dy=-1.0 * h, flick=i)
        f = b.draw(p)
        b.fx("summon", f, i, p)
        out.append(f)
    return out


def death(b):
    out = []
    # 0-2 overload (flash, cracks, plates jitter loose); 3-6 plates scatter + core blows apart and falls;
    # 7-13 the wreck dissolves (last frames almost empty).
    seq = [
        CPose(eye=1.7, lid=1.3, cracks=1, prad=3.0, rot=b.rot_step * 1, tilt=-0.1, flick=0),
        CPose(eye=0.4, lid=0.6, cracks=2, prad=5.0, rot=b.rot_step * 2.5, tilt=0.08, dx=1, flick=1),
        CPose(eye=1.5, lid=1.0, cracks=2, prad=8.0, rot=b.rot_step * 3, tilt=-0.06, flick=2, scatter=0.12),
    ]
    for i, p in enumerate(seq):
        f = b.draw(p)
        if i == 0:
            f = FX.flash(f, amount=0.5)
        b.fx("death", f, i, p)
        out.append(shake(f, [1, -1, 1][i], 0))
    fall = [
        CPose(eye=1.0, lid=0.8, cracks=2, rot=b.rot_step * 3.5, scatter=0.35, dy=1, tilt=0.15, dead=0.2, flick=3),
        CPose(eye=0.5, lid=0.5, cracks=2, rot=b.rot_step * 4, scatter=0.6, dy=3, tilt=0.3, dead=0.4, flick=4,
              thrust=0.3),
        CPose(eye=0.25, lid=0.3, cracks=2, rot=b.rot_step * 4.5, scatter=0.85, dy=5, tilt=0.45, dead=0.6, flick=5,
              thrust=0.0),
        CPose(eye=0.0, lid=0.2, cracks=2, rot=b.rot_step * 5, scatter=1.0, dy=6, tilt=0.55, dead=0.85, flick=6,
              thrust=0.0),
    ]
    for i, p in enumerate(fall):
        f = b.draw(p, layer="body")
        if i >= 1:
            # the core cracks open into pieces that drift apart and sink
            sep = [0, 2.5, 5.5, 8.0][i]
            c = p.c
            f = split_frame(f, c[0], c[1], b.split_angle, [(-sep, sep * 0.6), (sep, -sep * 0.3 + sep * 0.5),
                                                           (-sep * 0.6, sep * 1.1), (sep * 0.8, sep * 0.9)],
                            b.hot, seed=b.seed + 60, cuts=2 if i >= 2 else 1)
            f = tint_toward(f, b.wither, 0.15 * i, mask=~_outline_mask(f))
        over(f, b.draw(p, layer="plates"))
        b.fx("death", f, 3 + i, p)
        out.append(f)
    base = out[-1].copy()
    for k, t in enumerate((0.15, 0.32, 0.48, 0.64, 0.8, 0.94)):
        f = FX.dissolve(base, min(1.0, t), seed=b.seed + 41, colors=b.dissolve_cols, rise=24, drift=6,
                        top_first=True, life=0.4, particle_rate=0.16, bounds=(0, 0, W, H - 1))
        FX.under(f, b.residue(min(1.0, 0.3 + t)))
        b.fx("death", f, 7 + k, None)
        out.append(f)
    last = b.residue(1.0)
    motes(last, CX, 90, 7, 4, 22, b.dissolve_cols[:3], seed=b.seed + 77, arc=(-2.8, -0.3))
    out.append(last)
    return out


def build_sheet(b):
    s = Sheet("sprites/enemies/%s.png" % b.id, W, H, PIVOT)
    s.add("idle", idle(b), fps=8, loop=True)
    s.add("move", move(b), fps=10, loop=True)
    s.add("roar", roar(b), durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)
    s.add("hurt", hurt(b), durations=[0.08, 0.14], loop=False)
    s.add("death", death(b), durations=[0.1, 0.12, 0.12, 0.12, 0.12, 0.14, 0.16, 0.12, 0.12, 0.12, 0.14, 0.14,
                                         0.16, 0.3], loop=False)
    s.add("telegraph_beam", telegraph_beam(b), durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("beam", beam(b), fps=14, loop=True)
    s.add("telegraph_volley", telegraph_volley(b), durations=[0.1, 0.1, 0.12, 0.14, 0.18], loop=False)
    s.add("volley", volley(b), durations=[0.05, 0.12, 0.12, 0.14], loop=False, events=[(1, "fire")])
    s.add("telegraph_slam", telegraph_slam(b), durations=[0.1, 0.1, 0.12, 0.14, 0.2], loop=False)
    s.add("slam", slam(b), durations=[0.05, 0.05, 0.2, 0.14, 0.14], loop=False, events=[(2, "impact")])
    s.add("summon", summon(b), durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)
    s.save("animations/%s.json" % b.id)
    return s


def build_one(name):
    if name == "prime_sentinel":
        import boss_a_sentinel as mod
    else:
        import boss_a_forge as mod
    return build_sheet(mod.BOSS)


def build():
    for name in ("prime_sentinel", "forge_core"):
        build_one(name)
