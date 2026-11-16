"""Monolithe, le Golem Primordial (giga boss, eight phases).

A colossus of ancient stone blocks: grey-brown granite speckled like a quarry, moss on every top
face, carved runes and veins of crystal running through the masonry.  Hunched like a great ape:
two boulder shoulders rise above a small head sunk between them, whose single eye glows under a
heavy brow; the arms are huge, the forearms heavier still, ending in two enormous fists bound by
rune bracers (the fists are its weapons).  Short pillar legs, flat block feet: it walks, heavily.
Side view facing right (the game flips it).

Two forms:
  * form I  (phases 1-4): whole - two chest plates carved with a rune seal, only the runes, the
    bracers and a few crystal veins glow;
  * form II (phases 5-8): "Fendu" - the chest plates are gone and the crystal core shows in the
    torn chest, glowing cracks run everywhere, the near shoulder is broken (crystal shards in the
    fracture).
The light (runes, veins, cracks, eye, crystal core) changes colour at every phase; the stone and
the moss never do.  Every emissive pixel uses one of five tones P0..P4 of the phase ramp (plus
pure white), so each phase sheet is the phase-1 sheet with those five colours swapped (exact LUT,
`recolour`), see PHASES.

Frames 224x224, pivot [112, 212] = bottom-centre of the 80x128 collision box (x 72..152,
y 84..212) around the body (legs, pelvis, torso and shoulders; the arms swing outside it).  The
ground line is y 212: the feet stand on it and nothing of the body goes below it (only dust and
glowing cracks, at most 3 rows).  Drawn at scale 1.25 in game (280 px frame, 100x160 px box).
ORIGIN (attack, impact frame f2, event `impact`): the near fist hits the ground at (+65, -20)
from the pivot (fist centre; its knuckles touch the ground at (+71, 0)) - the far fist lands
just behind it, at (+53, -21).
EYE (special, the 4 frames): the eye is at (+63, -78) from the pivot (origin of the eye beam);
at rest (idle) it is at (+44, -101).
Clips (packed several per row, `start` = first column): idle 8, telegraph 6 (raises both fists),
walk 8 (= move, footsteps f0 and f4), attack 5 (double fist slam, `impact` f2), telegraph_special
6 (plants its fists, the runes flare), special 4 (loop: the eye blazes, the ground shakes), hurt 2,
summon 6 (tears rocks from the ground), roar 8 (phase change; the phase-5 sheet's roar is the chest
plates bursting off), death 14 (crumbles into a pile of stones, the light dying).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, mat, motes, ring
from boss_c_common import TAU, TIMING, Pose, dash_arc, shake
from common.palette import OUTLINE
from raster import TCanvas, ik2

PI = math.pi
W = H = 224
GY = 212                      # ground line (feet bottom); the outline of the feet sits on row 211
PIVOT = [112, GY]
BOX = (72, 84, 152, GY)       # collision box x0, y0, x1, y1 (frame pixels)
WHITE = "#ffffff"

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Ambre", "ambre", ["#ffeccc", "#ffb340", "#e2771a", "#9e4a16", "#52240e"]),
    ("Émeraude", "émeraude", ["#dcffe8", "#6dfab0", "#1fc47e", "#127056", "#0a3830"]),
    ("Cyan", "cyan", ["#dcffff", "#7cf2ff", "#2bb6e8", "#1d6aa8", "#10335c"]),
    ("Améthyste", "améthyste", ["#f2dcff", "#c98aff", "#9550e6", "#5a2aa6", "#2e145c"]),
    ("Magma", "orange-rouge", ["#ffdcc0", "#ff7034", "#dc3418", "#981a1e", "#4e0e18"]),
    ("Or", "or", ["#fffbdc", "#ffe866", "#e2bc2a", "#a07e16", "#54400c"]),
    ("Azur", "azur", ["#dfe8ff", "#8fb0ff", "#4a6cf2", "#28379e", "#141a52"]),
    ("Blanc incandescent", "blanc incandescent", ["#fffef4", "#f2f6ff", "#c4d6ff", "#8296e6", "#464ea0"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Stone and moss (never recoloured).
GRANITE = mat("mo_granite", ["#b3a691", "#8e8270", "#6c6255", "#4b443b", "#2f2a24"], th=[0.86, 0.68, 0.48, 0.28])
GRANITE_BK = mat("mo_granite_bk", ["#83796b", "#625a4f", "#463f37", "#2c2823"], th=[0.8, 0.58, 0.34])
CAVITY = mat("mo_cavity", ["#40372e", "#2a241e", "#181512"], th=[0.62, 0.34])
MOSS = mat("mo_moss", ["#a6bb63", "#77953f", "#506f2f", "#334b25"], th=[0.8, 0.6, 0.38])
MOSS_BK = mat("mo_moss_bk", ["#6c8240", "#4c632d", "#34471f"], th=[0.66, 0.38])
DEADC = mat("mo_dead", ["#706b76", "#4f4b57", "#35323c", "#211f27"])  # extinct crystal
# Light (recoloured per phase).
HOT = mat("mo_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("mo_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
DUST = [C("#b3a691"), C("#8e8270"), C("#6c6255")]
ROCK_L, ROCK_D = C("#8e8270"), C("#4b443b")

# ---------------------------------------------------------------- rig (body space)
# Body space: origin at the hip centre, x forward (right), y down; rotated by `lean` about the hips.
HIP_F, HIP_B = (10.0, 0.0), (-12.0, -2.0)
SH_F, SH_B = (-1.0, -52.0), (-24.0, -60.0)        # shoulder joints (arm roots, under the boulders)
HUMP_F, HUMP_B = (1.0, -62.0), (-24.0, -70.0)    # shoulder boulder centres
HEAD = (31.0, -55.0)
EYE_L = (8.0, 0.0)                                # the eye in head space
L_UP, L_FORE = 40.0, 50.0                         # shoulder -> elbow, elbow -> fist centre
L_THIGH, L_SHIN = 22.0, 21.0                      # hip -> knee -> ankle (ankle 10 px above the sole)

BACK_LOW = [(-40, -8), (-47, -34), (-22, -40), (4, -34), (8, -10), (-18, -2)]
BACK_UP = [(-47, -34), (-43, -58), (-30, -74), (-8, -80), (10, -70), (16, -48), (8, -30), (-22, -40)]
PELVIS = [(-34, -14), (-12, -22), (16, -24), (30, -14), (26, 2), (8, 10), (-18, 8), (-32, 0)]
CHEST = [(6, -62), (24, -56), (38, -44), (42, -30), (34, -22), (14, -24), (4, -40)]
ABDO = [(10, -24), (32, -22), (36, -12), (30, -2), (14, -2), (6, -12)]
CAV = [(8, -58), (24, -53), (36, -42), (39, -27), (33, -8), (14, -5), (5, -20), (3, -42)]
CORE_C = (22.0, -32.0)
# Remains of the chest plates around the hole (form II): jagged rims.
RIM = [[(6, -62), (24, -56), (30, -51), (22, -50), (16, -54), (10, -52), (5, -46)],
       [(36, -42), (42, -30), (38, -22), (35, -26), (36, -32), (33, -38)],
       [(30, -6), (34, -2), (14, -2), (10, -6), (18, -8), (24, -5)]]
HUMP_PTS = [(-23, -2), (-20, -15), (-8, -23), (8, -22), (19, -11), (22, 3), (14, 16), (-6, 19), (-21, 11)]
HUMP_BROKEN = [(-23, -2), (-20, -15), (-11, -21), (-6, -12), (0, -16), (5, -7), (12, -9), (22, 3), (14, 16),
               (-6, 19), (-21, 11)]
HUMP_BK_PTS = [(-18, -3), (-15, -14), (-5, -19), (7, -17), (15, -8), (17, 4), (9, 14), (-6, 15), (-16, 9)]
HEAD_PTS = [(-12, -6), (-6, -11), (6, -11), (12, -7), (14, 3), (9, 10), (-7, 10), (-13, 3)]
FIST_PTS = [(-15, -14), (2, -17), (14, -14), (18, -6), (18, 6), (14, 14), (0, 16), (-15, 14), (-18, 0)]
FOOT_PTS = [(-15, -11), (5, -12), (13, -9), (18, -3), (18, 0), (-16, 0), (-17, -6)]

# Crystal veins (body space): a few in form I, cracks everywhere in form II.
VEINS = [[(-44, -40), (-38, -43), (-35, -37), (-29, -41)],
         [(-20, -10), (-15, -6), (-8, -11), (-3, -8)],
         [(-40, -24), (-36, -18), (-30, -19), (-27, -12)],
         [(-36, -66), (-30, -62), (-24, -66)]]
CRACKS = [[(-40, -18), (-33, -24), (-34, -31), (-26, -38), (-24, -50), (-16, -56)],
          [(-46, -50), (-39, -54), (-36, -62), (-30, -66)],
          [(-28, -8), (-22, -14), (-14, -13), (-10, -20), (-2, -18)],
          [(6, -30), (1, -36), (4, -44), (-2, -52)],
          [(18, -16), (24, -12), (22, -4), (27, 3)],
          [(-16, 2), (-10, -2), (-4, 4)]]
# Runes: strokes in a 4x6 box.
RUNES = [
    [((0, 0), (0, 6)), ((0, 1), (3, 0)), ((0, 3), (3, 2))],
    [((0, 0), (0, 6)), ((0, 0), (3, 2)), ((3, 2), (0, 4)), ((0, 4), (3, 6))],
    [((2, 0), (2, 6)), ((0, 1), (4, 5)), ((4, 1), (0, 5))],
    [((0, 6), (2, 0)), ((2, 0), (4, 6)), ((1, 3), (3, 3))],
    [((0, 0), (0, 6)), ((4, 0), (4, 6)), ((0, 0), (4, 6))],
    [((2, 0), (0, 3)), ((0, 3), (2, 6)), ((2, 6), (4, 3)), ((4, 3), (2, 0))],
]


class MPose(Pose):
    DEFAULTS = dict(t=0.0, hip=(114.0, 164.0), lean=0.05,
                    foot_f=(126.0, float(GY)), foot_b=(101.0, float(GY)), toe_f=0.0, toe_b=0.0,
                    fist_f=(152.0, 190.0), fist_b=(68.0, 184.0), turn_f=0.0, turn_b=0.0,
                    head=(0.0, 0.0), tilt=0.0, hump=(0.0, 0.0, 0.0, 0.0), plate=(0.0, 0.0),
                    glow=0.6, eye=0.7, lid=1.0, broken=False, crack=1.0, dead=0.0, spread=0.0, fly=0.0,
                    elbow_f=(-30.0, 14.0), elbow_b=(-30.0, 14.0), crackle=0.0)
    # hump: offsets (near dx, dy, far dx, dy) of the shoulder boulders (breathing, stones shifting)
    # plate: offset of the chest plates; fly: the chest plates and the near shoulder bursting off
    # elbow_f / elbow_b: where the elbow tends to go, from the shoulder (picks the IK solution)
    # glow: runes / veins 0..1.5; eye 0..1.5; dead: the light dying 0..1 (1 = extinct)
    # crackle: glowing cracks on the whole form (form I: roar of phase 5, death)


def _hash(ix, iy, seed):
    h = np.sin(ix * 12.9898 + iy * 78.233 + seed * 37.719) * 43758.5453
    return h - np.floor(h)


cv_y, cv_x = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5


def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


class Monolithe:
    id = "monolithe"
    W, H, PIVOT = W, H, PIVOT
    seed = 51207
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 7, 8, 12
    ATTACK_EVENTS = [(2, "impact")]
    WALK_EVENTS = [(0, "footstep"), (4, "footstep")]

    # ------------------------------------------------------------ geometry
    def B(self, p, q, off=(0.0, 0.0)):
        """Body space -> frame."""
        x, y = q[0] + off[0], q[1] + off[1]
        c, s = math.cos(p.lean), math.sin(p.lean)
        return (p.hip[0] + x * c - y * s, p.hip[1] + x * s + y * c)

    def push(self, p, centre, k=1.0):
        """Offset of a block pushed away from the torso centre by `spread` (roar, death)."""
        if not p.spread:
            return (0.0, 0.0)
        dx, dy = centre[0] - 0.0, centre[1] + 36.0
        d = math.hypot(dx, dy) or 1.0
        return (dx / d * p.spread * k, dy / d * p.spread * k)

    def hump_off(self, p, near):
        n = self.push(p, HUMP_F if near else HUMP_B)
        o = p.hump[:2] if near else p.hump[2:]
        return (n[0] + o[0], n[1] + o[1])

    def head_centre(self, p):
        o = self.push(p, HEAD, 0.6)
        return self.B(p, (HEAD[0] + p.head[0] + o[0], HEAD[1] + p.head[1] + o[1]))

    def head_L(self, p):
        hc = self.head_centre(p)
        a = p.lean + p.tilt
        c, s = math.cos(a), math.sin(a)
        return lambda x, y: (hc[0] + x * c - y * s, hc[1] + x * s + y * c)

    def eye_centre(self, p):
        return self.head_L(p)(*EYE_L)

    def fist(self, p, near=True):
        return p.fist_f if near else p.fist_b

    def shoulder(self, p, near):
        return self.B(p, SH_F if near else SH_B, self.hump_off(p, near))

    def elbow(self, p, near):
        sh, fi = self.shoulder(p, near), self.fist(p, near)
        hx, hy = p.elbow_f if near else p.elbow_b
        best = None
        for bend in (1.0, -1.0):
            e = ik2(sh, fi, L_UP, L_FORE, bend)
            d = (e[0] - sh[0] - hx) ** 2 + (e[1] - sh[1] - hy) ** 2
            if best is None or d < best[0]:
                best = (d, e)
        return best[1]

    # ------------------------------------------------------------ light
    def light(self, p):
        return DEADC if p.dead >= 1.0 else HOT

    def li(self, p, v):
        """Intensity of a light pixel (dimmed while dying)."""
        if p.dead >= 1.0:
            return 0.25 + 0.3 * v
        return v * (1.0 - 0.55 * p.dead)

    # ------------------------------------------------------------ primitives
    def grain(self, cv, mask, origin, ang, seed, amt=1.0):
        """Granite speckles in the local frame of a part (they move with it)."""
        if mask is None or not mask.any():
            return
        c, s = math.cos(-ang), math.sin(-ang)
        dx, dy = cv.px - origin[0], cv.py - origin[1]
        lx = np.floor(dx * c - dy * s)
        ly = np.floor(dx * s + dy * c)
        n = _hash(lx, ly, seed)
        d = np.where(n > 0.955, -0.17, np.where(n < 0.025, 0.13, 0.0)) + 0.015 * (n - 0.5)
        cv.inten[mask] = np.clip(cv.inten[mask] + amt * d[mask], 0, 1)

    def block(self, cv, pts, m, origin, ang, seed, base=0.56, bevel=2.6, grad=0.14, line=True):
        """A stone block (frame-space polygon): bevelled edges, light from the top, speckles."""
        cv.part(line=line)
        mask = cv.poly(pts, m, bevel=bevel, base=base)
        if mask is None or not mask.any():
            return mask
        ys = [q[1] for q in pts]
        y0, y1 = min(ys), max(ys)
        g = grad * (0.5 - (cv.py - y0) / max(1.0, y1 - y0))
        cv.inten[mask] = np.clip(cv.inten[mask] + g[mask], 0, 1)
        self.grain(cv, mask, origin, ang, seed)
        return mask

    def body_block(self, cv, p, pts, m, seed, off=(0.0, 0.0), **kw):
        cx = sum(q[0] for q in pts) / len(pts)
        cy = sum(q[1] for q in pts) / len(pts)
        o = self.push(p, (cx, cy))
        o = (o[0] + off[0], o[1] + off[1])
        fp = [self.B(p, q, o) for q in pts]
        return self.block(cv, fp, m, self.B(p, (cx, cy), o), p.lean, seed, **kw)

    def moss(self, cv, c, rx, ry, ang, seed, m=MOSS, drips=3):
        """A patch of moss on the top of a block (decal) with a few drips."""
        cv.part(line=False)
        mask = cv.ellipse(c, rx, ry, m, ang=ang, decal=True, bias=0.06)
        if mask is not None and mask.any():
            self.grain(cv, mask, c, ang, seed + 5, amt=0.8)
        rng = np.random.default_rng(seed)
        ca, sa = math.cos(ang), math.sin(ang)
        for k in range(drips):
            u = (rng.random() * 1.4 - 0.7) * rx
            ln = 2 + int(rng.integers(0, 3))
            x0, y0 = c[0] + u * ca, c[1] + u * sa + ry * 0.55
            cv.line((x0, y0), (x0, y0 + ln), m, inten=0.42, decal=True)

    def vein(self, cv, p, pts, inten=0.6, wide=False):
        """A glowing crystal vein / crack (frame-space polyline), decal on the stone."""
        L = self.light(p)
        if wide:
            cv.part(line=False)
            for a, b in zip(pts[:-1], pts[1:]):
                cv.capsule(a, b, 1.25, 1.25, L, flat=self.li(p, 0.3), decal=True)
        cv.part(line=False)
        for a, b in zip(pts[:-1], pts[1:]):
            cv.line(a, b, L, inten=self.li(p, inten), decal=True)

    def rune(self, cv, p, k, L, size=1.0, inten=0.75):
        """Rune glyph k drawn through the local mapping L (glyph box 4x6 centred on 0)."""
        m = self.light(p)
        cv.part(line=False)
        if inten > 0.5 and p.dead < 0.5:  # a glowing halo around the carved strokes (wider when flaring)
            r = 1.3 if inten > 0.95 else 1.0
            for (a, b) in RUNES[k % len(RUNES)]:
                pa = L((a[0] - 2) * size, (a[1] - 3) * size)
                pb = L((b[0] - 2) * size, (b[1] - 3) * size)
                cv.capsule(pa, pb, r, r, m, flat=self.li(p, 0.38 if inten > 0.95 else 0.3), decal=True)
        for (a, b) in RUNES[k % len(RUNES)]:
            pa = L((a[0] - 2) * size, (a[1] - 3) * size)
            pb = L((b[0] - 2) * size, (b[1] - 3) * size)
            cv.line(pa, pb, m, inten=self.li(p, inten), decal=True)

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = TCanvas(W, H)
        self._upper_arm(cv, p, near=False)
        self._hump(cv, p, near=False)
        self._arm(cv, p, near=False)
        self._leg(cv, p, near=False)
        self._torso(cv, p)
        self._leg(cv, p, near=True)
        self._head(cv, p)
        self._upper_arm(cv, p, near=True)
        self._hump(cv, p, near=True)
        self._arm(cv, p, near=True)
        img = cv.render()
        # nothing of the body below the ground line: fists and knees rest on it (closed outline)
        img[GY:] = 0
        row = img[GY - 1]
        row[row[:, 3] > 0] = OUTLINE
        return img

    def _hump(self, cv, p, near):
        off = self.hump_off(p, near)
        c0 = HUMP_F if near else HUMP_B
        broken = near and p.broken
        pts = HUMP_BROKEN if broken else (HUMP_PTS if near else HUMP_BK_PTS)
        c = self.B(p, c0, off)
        fp = [self.B(p, (c0[0] + x, c0[1] + y), off) for x, y in pts]
        m = GRANITE if near else GRANITE_BK
        self.block(cv, fp, m, c, p.lean, 31 if near else 32, base=0.6 if near else 0.55, bevel=3.2, grad=0.2)
        L = lambda x, y: self.B(p, (c0[0] + x, c0[1] + y), off)
        # a seam across the boulder (two stones)
        cv.part(line=False)
        cv.line(L(-18, 8), L(-6, 5), m, inten=0.12, decal=True)
        cv.line(L(-6, 5), L(2, 13), m, inten=0.12, decal=True)
        if broken:
            # fracture: a lighter raw face, crystal shards growing out of it, glowing cracks
            cv.part(line=False)
            cv.poly([L(-10, -18), L(-5, -10), L(1, -14), L(5, -6), L(11, -8), L(6, -2), L(-6, -4)], m, flat=0.8,
                    decal=True)
            for (bx, by, ang, ln, wd) in ((-4, -9, -1.9, 9, 2.6), (3, -7, -1.2, 7, 2.2), (9, -6, -0.5, 5, 1.8)):
                self.crystal(cv, p, L(bx, by), p.lean + ang, ln, wd)
            self.vein(cv, p, [L(-5, -9), L(-8, -2), L(-14, 3)], inten=0.62, wide=True)
            self.vein(cv, p, [L(5, -6), L(9, 2), L(14, 6)], inten=0.55)
        if near:
            self.moss(cv, L(-6, -15) if not broken else L(-13, -14), 11 if not broken else 7, 4.5, p.lean - 0.15,
                      seed=41, drips=4)
        else:
            self.moss(cv, L(-3, -15), 10, 4, p.lean - 0.1, seed=42, m=MOSS_BK, drips=2)
        if p.broken or p.crackle > 0.5:
            self.vein(cv, p, [L(-18, 2), L(-12, -2), L(-12, 8)], inten=0.5)
        if near and p.broken and p.fly > 0:
            # the top of the shoulder flying off (phase-5 roar)
            fo = (off[0] - 16 * p.fly, off[1] - 30 * p.fly + 26 * p.fly * p.fly)
            r = -1.2 * p.fly
            cr, sr = math.cos(r), math.sin(r)
            piece = [(-11, -21), (-8, -23), (8, -22), (19, -11), (12, -9), (5, -7), (0, -16), (-6, -12)]
            fp = [self.B(p, (c0[0] + x * cr - y * sr, c0[1] + x * sr + y * cr), fo) for x, y in piece]
            self.block(cv, fp, GRANITE, self.B(p, c0, fo), p.lean + r, 34, base=0.62, bevel=2.4)
            self.moss(cv, self.B(p, (c0[0] - 2 * cr + 17 * sr, c0[1] - 2 * sr - 17 * cr), fo), 7, 3, p.lean + r,
                      seed=45, drips=1)

    def crystal(self, cv, p, base, ang, ln, wd):
        """A crystal prism from `base` towards `ang`: lit face, shaded face, bright ridge and tip."""
        m = CORE if p.dead < 1.0 else DEADC
        ca, sa = math.cos(ang), math.sin(ang)

        def L(x, y):
            return (base[0] + x * ca - y * sa, base[1] + x * sa + y * ca)
        cv.part(line=False)
        cv.poly([L(-1, -wd), L(ln * 0.72, -wd), L(ln, 0), L(-1, 0)], m, flat=self.li(p, 0.74))
        cv.poly([L(-1, 0), L(ln, 0), L(ln * 0.72, wd), L(-1, wd)], m, flat=self.li(p, 0.56))
        cv.line(L(ln * 0.25, -0.2), L(ln - 1.5, -0.2), m, inten=self.li(p, 0.8))
        cv.px_set(*L(ln - 1.4, -0.3), m, inten=self.li(p, 1.0))

    def _torso(self, cv, p):
        L = lambda x, y: self.B(p, (x, y))
        self.body_block(cv, p, BACK_UP, GRANITE, 11, base=0.52, bevel=3.0, grad=0.22)
        self.body_block(cv, p, BACK_LOW, GRANITE, 13, base=0.48, bevel=2.6, grad=0.16)
        # courses of stone: seams
        cv.part(line=False)
        for a, b in (((-44, -58), (-26, -56)), ((-26, -56), (-18, -62)), ((-38, -20), (-24, -22))):
            cv.line(L(*a), L(*b), GRANITE, inten=0.14, decal=True)
        self.moss(cv, L(-34, -66), 12, 5, p.lean - 0.6, seed=43, drips=3)
        self.body_block(cv, p, PELVIS, GRANITE, 12, base=0.5, bevel=2.6)
        for pts in VEINS:
            self.vein(cv, p, [L(*q) for q in pts], inten=0.55 + 0.25 * min(1.0, p.glow))
        open_chest = p.broken or p.fly > 0
        if open_chest:
            self._core(cv, p)
        if not p.broken or p.fly > 0:
            self._plates(cv, p)
        if p.broken:
            for k, pts in enumerate(RIM):
                self.body_block(cv, p, pts, GRANITE, 60 + k, base=0.58, bevel=2.0)
        n = len(CRACKS) if p.broken else int(round(p.crackle * len(CRACKS)))
        for k, pts in enumerate(CRACKS[:n]):
            self.vein(cv, p, [L(*q) for q in pts], inten=0.62 + 0.2 * min(1.0, p.glow), wide=p.crack > 1.2 or k < 2)

    def _plates(self, cv, p):
        fly = p.fly
        for k, (pts, seed) in enumerate(((CHEST, 21), (ABDO, 22))):
            cx = sum(q[0] for q in pts) / len(pts)
            cy = sum(q[1] for q in pts) / len(pts)
            off = (p.plate[0], p.plate[1])
            rot = 0.0
            if fly > 0:
                # bursting off: forward, up for the upper plate, down for the lower one
                d = (1.0, -0.7) if k == 0 else (0.8, 0.05)
                off = (off[0] + d[0] * 34 * fly, off[1] + d[1] * 34 * fly + (20 if k == 0 else 14) * fly * fly)
                rot = (0.9 if k == 0 else -0.7) * fly
            cr, sr = math.cos(rot), math.sin(rot)
            q2 = [(cx + (x - cx) * cr - (y - cy) * sr, cy + (x - cx) * sr + (y - cy) * cr) for x, y in pts]
            self.body_block(cv, p, q2, GRANITE, seed, off=off, base=0.62, bevel=2.4, grad=0.16)
            o = self.push(p, (cx, cy))
            o = (o[0] + off[0], o[1] + off[1])

            def L(x, y, cx=cx, cy=cy, o=o, cr=cr, sr=sr):
                return self.B(p, (cx + x * cr - y * sr, cy + x * sr + y * cr), o)
            if k == 0:
                # the rune seal: a ring with a glyph
                cv.part(line=False)
                ring_pts = [L(math.cos(a) * 7.5 + 2, math.sin(a) * 7.5 + 1) for a in np.linspace(0, TAU, 25)]
                for a, b in zip(ring_pts[:-1], ring_pts[1:]):
                    cv.line(a, b, self.light(p), inten=self.li(p, 0.5 + 0.3 * min(1.2, p.glow)), decal=True)
                self.rune(cv, p, 2, lambda x, y: L(x + 2, y + 1), size=1.0, inten=0.6 + 0.3 * min(1.2, p.glow))
            else:
                self.rune(cv, p, 0, lambda x, y: L(x - 6, y), size=0.9, inten=0.55 + 0.3 * min(1.2, p.glow))
                self.rune(cv, p, 3, lambda x, y: L(x + 4, y), size=0.9, inten=0.55 + 0.3 * min(1.2, p.glow))
            if p.fly > 0:
                # the glow leaking through the splitting plates
                self.vein(cv, p, [L(-8, -4), L(-2, 1), L(5, -2), L(10, 3)], inten=0.8, wide=True)

    def _core(self, cv, p):
        """Form II: the torn chest - a dark cavity and the crystal heart growing in it."""
        L = lambda x, y: self.B(p, (x, y))
        cv.part()
        cav = [L(*q) for q in CAV]
        mask = cv.poly(cav, CAVITY, bevel=2.0, base=0.3)
        g = min(1.4, p.glow)
        cx, cy = CORE_C
        if mask is not None and mask.any() and p.dead < 1.0:
            # light spilling on the cavity walls around the crystals
            hx, hy = L(cx, cy - 4)
            d = np.hypot(cv.px - hx, (cv.py - hy) * 0.8)
            spill = mask & (d < 15 + 3 * g)
            cv.part(line=False)
            cv.put(spill, self.li(p, np.clip(0.36 - 0.022 * d, 0.05, 0.4)), HOT)
        # a geode cluster: prisms fanning up from the bottom of the hole
        for (dx, dy, ang, ln, wd) in ((-9, 8, -2.35, 10 + g, 3.0), (8, 9, -0.55, 11 + g, 3.0),
                                      (-5, 8, -1.95, 15 + 2 * g, 3.8), (4, 8, -1.05, 16 + 2 * g, 3.8),
                                      (-1, 9, -1.5, 22 + 3 * g, 4.6)):
            self.crystal(cv, p, L(cx + dx, cy + dy), p.lean + ang, ln, wd)

    def _head(self, cv, p):
        L = self.head_L(p)
        a = p.lean + p.tilt
        hc = L(0, 0)
        self.block(cv, [L(*q) for q in HEAD_PTS], GRANITE, hc, a, 71, base=0.58, bevel=2.6, grad=0.2)
        # jaw
        self.block(cv, [L(-6, 5), L(13, 4), L(12, 11), L(-4, 12)], GRANITE, L(4, 8), a, 72, base=0.42, bevel=1.6)
        # eye socket under the brow
        ex, ey = L(*EYE_L)
        cv.part(line=False)
        cv.ellipse((ex, ey), 5.6, 3.6 * max(0.35, p.lid), CAVITY, ang=a, flat=0.15)
        if p.lid > 0.15:
            m = CORE if p.dead < 1.0 else DEADC
            e = min(1.5, p.eye)
            cv.ellipse((ex + 0.5, ey), 4.2, 2.6 * min(1.0, p.lid), m, ang=a, flat=self.li(p, 0.42 + 0.12 * e))
            cv.ellipse((ex + 0.9, ey), 2.8, 1.8 * min(1.0, p.lid), m, ang=a, flat=self.li(p, 0.62 + 0.1 * e))
            cv.ellipse((ex + 1.2, ey), 1.4, 1.0 * min(1.0, p.lid), m, ang=a, flat=self.li(p, 0.75 + 0.12 * e))
        # heavy brow (in front of the socket top)
        cv.part()
        mk = cv.capsule(L(-4, -6), L(14, -5.5), 3.4, 3.2, GRANITE)
        self.grain(cv, mk, hc, a, 73)
        cv.inten[mk] = np.clip(cv.inten[mk] + 0.08, 0, 1)
        self.moss(cv, L(-3, -10), 9, 3, a - 0.1, seed=44, drips=2)
        if p.broken or p.crackle > 0.5:
            self.vein(cv, p, [L(-8, -4), L(-4, 0), L(-7, 5)], inten=0.6)

    def _leg(self, cv, p, near):
        m = GRANITE if near else GRANITE_BK
        hip = self.B(p, HIP_F if near else HIP_B, self.push(p, HIP_F if near else HIP_B, 0.3))
        foot = p.foot_f if near else p.foot_b
        toe = p.toe_f if near else p.toe_b
        fx, fy = foot[0], foot[1] - 1.0
        ct, st = math.cos(-toe), math.sin(-toe)

        def F(x, y):  # foot space: origin at the heel-bottom centre, toe tilt about the heel
            return (fx + x * ct - y * st, fy + x * st + y * ct)
        ankle = F(-2.0, -12.0)
        knee = ik2(hip, ankle, L_THIGH, L_SHIN, 1.0)
        sd = 41 if near else 51
        cv.part()
        mk = cv.capsule(hip, knee, 13.0, 11.5, m)
        self.grain(cv, mk, hip, math.atan2(knee[1] - hip[1], knee[0] - hip[0]), sd)
        cv.part()
        mk = cv.capsule(knee, ankle, 10.5, 10.5, m)
        self.grain(cv, mk, knee, math.atan2(ankle[1] - knee[1], ankle[0] - knee[0]), sd + 1)
        cv.part()
        mk = cv.disc(knee, 10.0, m, bias=0.04)
        self.grain(cv, mk, knee, 0.0, sd + 2)
        self.block(cv, [F(*q) for q in FOOT_PTS], m, F(0, -5), -toe, sd + 3, base=0.5, bevel=2.2, grad=0.2)
        cv.part(line=False)
        for x in (7.0, 12.5):
            cv.line(F(x, -1), F(x - 1, -8), m, inten=0.12, decal=True)
        if near:
            # knee rune
            kd = math.atan2(ankle[1] - knee[1], ankle[0] - knee[0])
            ck, sk = math.cos(kd - PI / 2), math.sin(kd - PI / 2)
            self.rune(cv, p, 5, lambda x, y: (knee[0] + x * ck - y * sk, knee[1] + x * sk + y * ck), size=0.8,
                      inten=0.5 + 0.25 * min(1.2, p.glow))
        if p.broken or p.crackle > 0.5:
            self.vein(cv, p, [((hip[0] * 2 + knee[0]) / 3 - 4, (hip[1] * 2 + knee[1]) / 3),
                              ((hip[0] + knee[0] * 2) / 3 + 2, (hip[1] + knee[1] * 2) / 3 - 1),
                              (knee[0] - 3, knee[1] + 6)], inten=0.55 if near else 0.4)

    def _upper_arm(self, cv, p, near):
        m = GRANITE if near else GRANITE_BK
        sh = self.shoulder(p, near)
        el = self.elbow(p, near)
        cv.part()
        mk = cv.capsule(sh, el, 12.0, 11.0, m)
        self.grain(cv, mk, sh, math.atan2(el[1] - sh[1], el[0] - sh[0]), 81 if near else 91)

    def _arm(self, cv, p, near):
        """Elbow, forearm, rune bracer and fist (the upper arm is drawn under the shoulder boulder)."""
        m = GRANITE if near else GRANITE_BK
        fist = self.fist(p, near)
        el = self.elbow(p, near)
        sd = 81 if near else 91
        a = math.atan2(fist[1] - el[1], fist[0] - el[0])
        ca, sa = math.cos(a), math.sin(a)
        wrist = (fist[0] - ca * 15, fist[1] - sa * 15)
        cv.part()
        mk = cv.capsule(el, wrist, 12.5, 15.5, m)
        self.grain(cv, mk, el, a, sd + 1)
        cv.part()
        mk = cv.disc(el, 11.5, m, bias=0.03)
        self.grain(cv, mk, el, 0.0, sd + 2)

        def A(x, y):  # forearm space: origin at the fist centre, x towards the knuckles
            return (fist[0] + x * ca - y * sa, fist[1] + x * sa + y * ca)
        # rune bracer around the wrist
        self.block(cv, [A(-31, -17), A(-14, -18), A(-13, 18), A(-31, 17)], m, A(-22, 0), a, sd + 3, base=0.6,
                   bevel=2.0, grad=0.1)
        k = 1.0 if near else 0.78
        cv.part(line=False)
        cv.line(A(-30, -11), A(-14, -11), self.light(p), inten=self.li(p, (0.45 + 0.3 * min(1.2, p.glow)) * k),
                decal=True)
        cv.line(A(-30, 11), A(-14, 11), self.light(p), inten=self.li(p, (0.45 + 0.3 * min(1.2, p.glow)) * k),
                decal=True)
        self.rune(cv, p, 1 if near else 4, lambda x, y: A(-22 + y, -x), size=1.2,
                  inten=(0.55 + 0.35 * min(1.2, p.glow)) * k)
        self._fist(cv, p, fist, a + (p.turn_f if near else p.turn_b), m, near, sd + 4)
        if p.broken or p.crackle > 0.5:
            self.vein(cv, p, [A(-62, 3), A(-52, -3), A(-44, 4), A(-35, -1)], inten=0.55 if near else 0.4,
                      wide=near)

    def _fist(self, cv, p, c, a, m, near, sd):
        ca, sa = math.cos(a), math.sin(a)

        def F(x, y):
            return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca)
        self.block(cv, [F(*q) for q in FIST_PTS], m, c, a, sd, base=0.58, bevel=2.8, grad=0.12)
        # knuckles: four bumps on the far end, seams between the fingers
        for k, y in enumerate((-10.5, -3.5, 3.5, 10.5)):
            cv.part(line=1)
            mk = cv.disc(F(14.5, y), 4.0, m, bias=0.05)
            self.grain(cv, mk, c, a, sd + 10 + k)
        # thumb wrapped over the fingers
        cv.part()
        mk = cv.capsule(F(-7, -14.5), F(8, -15.5), 5.0, 4.4, m, bias=0.04)
        self.grain(cv, mk, c, a, sd + 20)
        # rune on the back of the hand
        self.rune(cv, p, 3 if near else 0, lambda x, y: F(-3 + y, 3 - x), size=1.3,
                  inten=(0.6 + 0.35 * min(1.2, p.glow)) * (1 if near else 0.8))
        if p.broken or p.crackle > 0.5:
            self.vein(cv, p, [F(-12, 8), F(-6, 5), F(-1, 9)], inten=0.55 if near else 0.4)

    # ------------------------------------------------------------- effects
    def sparkle(self, f, x, y, size, c0=None, c1=None):
        FX.sparkle(f, int(x), int(y), size, c0 or WHITE_C, c1 or PC[1])

    def charge(self, f, x, y, i, n, seed, r0=24, r1=4, count=10):
        """Motes of light converging onto (x, y)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = rng.random() * TAU
            d = r0 * (1 - t) + r1 + rng.random() * 6
            put_if_empty(f, x + math.cos(a) * d, y + math.sin(a) * d, [WHITE_C, PC[0], PC[1], PC[2]][k % 4])

    def dust(self, f, x, r, seed, y=GY - 2):
        """A low dust cloud on the ground (on empty pixels; at most 3 rows into the floor)."""
        layer = np.zeros_like(f)
        dust_cloud(layer, x, y, r, DUST, seed)
        layer[GY + 3:] = 0
        layer[:, :3] = 0
        layer[:, W - 3:] = 0
        FX.under(f, layer)

    def debris(self, f, seed, x0, x1, y0, y1, n, sizes=(1, 2, 3)):
        rng = np.random.default_rng(seed)
        for k in range(n):
            FX.chunk(f, x0 + rng.random() * (x1 - x0), y0 + rng.random() * (y1 - y0), sizes[k % len(sizes)], ROCK_L,
                     ROCK_D)

    def ground_cracks(self, f, x, reach, seed, level=0):
        """Glowing cracks running along the ground from x both ways (rows GY-1..GY+3, in the floor);
        `level` darkens them (0 = brightest)."""
        rng = np.random.default_rng(seed)
        cols = [WHITE_C, PC[0], PC[1], PC[2], PC[3], PC[4]]
        for side in (-1, 1):
            for branch in range(2):
                cx, cy = int(x) + side * (1 + 5 * branch), GY + branch
                n = int(reach * (1.0 if branch == 0 else 0.5))
                for k in range(n):
                    cx += side
                    if rng.random() < 0.4:
                        cy = min(GY + 3, max(GY, cy + int(rng.integers(-1, 2))))
                    if not 3 <= cx < W - 3:
                        break
                    c = cols[min(5, 1 + level + int(3 * k / max(1, n)) + branch)]
                    if f[cy, cx, 3] == 0 or cy >= GY:
                        f[cy, cx] = c

    def smear(self, f, cx, cy, r0, r1, a0, a1, bands=7):
        """Swing smear: an annular sector behind a fist made of streaks of different lengths, bright
        at the leading edge (a1)."""
        rr = np.hypot(cv_x - cx, cv_y - cy)
        aa = np.arctan2(cv_y - cy, cv_x - cx)
        s = (aa - a0) / (a1 - a0)
        rad = (rr - r0) / (r1 - r0)
        band = np.floor(np.clip(rad, 0, 0.999) * bands)
        start = 0.04 + 0.4 * _hash(band, band * 3.0, 11)
        m = (s >= start) & (s <= 1) & (rad >= 0) & (rad <= 1) & (f[:, :, 3] == 0)
        k = np.clip((1.0 - s) * 2.6 + np.abs(rad - 0.5) * 1.6, 0, 3).astype(int)
        cols = np.array([PC[0], PC[1], PC[2], PC[3]], np.uint8)
        f[m] = cols[k[m]]

    def shock(self, f, x, r, col, dotted=False):
        """A shockwave arc on the ground (flattened half ring above the floor)."""
        dash_arc(f, x, GY - 1, r, r * 0.34, PI, TAU, col, dash=2 if dotted else 99, gap=3 if dotted else 0)

    def rock(self, cx, cy, r, seed, glow=0.8, rune=True):
        """A boulder torn from the ground (summon), with a glowing rune; returns a frame layer."""
        p = MPose(glow=glow)
        cv = TCanvas(W, H)
        rng = np.random.default_rng(seed)
        pts = []
        for k in range(8):
            a = TAU * k / 8 + rng.random() * 0.35
            rr = r * (0.8 + 0.3 * rng.random())
            pts.append((cx + math.cos(a) * rr * 1.12, cy + math.sin(a) * rr * 0.86))
        self.block(cv, pts, GRANITE, (cx, cy), 0.0, seed, base=0.56, bevel=2.4, grad=0.24)
        self.moss(cv, (cx - r * 0.2, cy - r * 0.66), r * 0.6, 2.4, -0.1, seed=seed + 1, drips=1)
        if rune:
            self.rune(cv, p, seed % len(RUNES), lambda x, y: (cx + x, cy + y + 1), size=1.0,
                      inten=0.6 + 0.3 * min(1.2, glow))
        return cv.render()

    # ================================================================= clips
    def base(self, **kw):
        return MPose(**kw)

    def idle(self, broken=False):
        """Breathing: the chest heaves, the boulders of the shoulders shift, a pebble rolls off."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            s = math.sin(ph)
            p = MPose(t=ph, broken=broken, hip=(114.0, 164.0 + 1.2 * s),
                      hump=(0.0, -1.3 * s, 0.0, -1.3 * math.sin(ph - 1.0)),
                      plate=(0.0, -0.9 * math.sin(ph + 0.4)), head=(0.0, 0.9 * math.sin(ph - 0.6)),
                      fist_f=(152 + 1.5 * math.sin(ph + 0.5), 190 + 1.0 * math.sin(ph + 0.5)),
                      fist_b=(68 - 1.2 * math.sin(ph + 1.2), 184 + 1.0 * math.sin(ph + 1.2)),
                      glow=0.62 + 0.14 * s, eye=0.75 + 0.12 * s, lid=0.3 if i == 5 else 1.0)
            f = self.draw(p)
            if 1 <= i <= 3:
                x, y = [(66, 100), (60, 120), (55, 146)][i - 1]
                FX.chunk(f, x, y, 2 if i < 3 else 3, ROCK_L, ROCK_D)
            out.append(f)
        return out

    @staticmethod
    def _step(u, base):
        """Foot x, lift, toe tilt at phase u (0..0.5 on the ground, 0.5..1 swinging forward)."""
        S, L = 15.0, 9.0
        if u < 0.5:
            return base + S - 2 * S * (u / 0.5), 0.0, 0.0
        s = (u - 0.5) / 0.5
        sm = s * s * (3 - 2 * s)
        return base - S + 2 * S * sm, L * math.sin(PI * s), 0.18 * (2 * s - 1) * math.sin(PI * s)

    def walk_pose(self, i, broken, n=8):
        ph = TAU * i / n
        u = i / n
        bob = [2, 3, 1, -1, 2, 3, 1, -1]
        xf, lf, tf = self._step(u, 124.0)
        xb, lb, tb = self._step((u + 0.5) % 1.0, 103.0)
        lag = 0.6 * (bob[(i - 1) % n] - bob[i])
        return MPose(t=ph, broken=broken, hip=(115.0 + 0.8 * math.sin(2 * ph), 163.0 + bob[i]),
                     lean=0.08 + 0.02 * math.cos(2 * ph), foot_f=(xf, GY - lf), foot_b=(xb, GY - lb), toe_f=tf,
                     toe_b=tb,
                     fist_f=(151 - 9 * math.cos(ph), 189 - 3 * abs(math.cos(ph)) + 0.5 * bob[i]),
                     fist_b=(70 + 8 * math.cos(ph), 183 - 3 * abs(math.cos(ph)) + 0.5 * bob[i]),
                     hump=(0.0, lag, 0.0, lag), head=(0.0, 0.4 * bob[i]), plate=(0.0, 0.5 * lag), glow=0.66, eye=0.82)

    def walk(self, broken=False):
        """Heavy steps: each foot stamps down (dust, f0 and f4), the arms swing like pendulums."""
        out = []
        for i in range(8):
            p = self.walk_pose(i, broken)
            f = self.draw(p)
            if i in (0, 4):
                fx = (p.foot_f if i == 0 else p.foot_b)[0]
                self.dust(f, fx + 16, 5, self.seed + 10 + i)
                self.dust(f, fx - 15, 4, self.seed + 12 + i)
            elif i in (1, 5):
                fx = (p.foot_f if i == 1 else p.foot_b)[0]
                self.dust(f, fx + 20, 3, self.seed + 14 + i)
                self.dust(f, fx - 19, 3, self.seed + 16 + i)
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """Both fists rise above the head, their runes flare (slams, quakes)."""
        near = [(154, 178), (170, 150), (172, 112), (160, 76), (136, 46), (121, 32)]
        far = [(70, 172), (96, 170), (130, 140), (140, 100), (122, 52), (106, 34)]
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = MPose(t=0.6 * i, broken=broken, hip=(114.0, 164.0 - 2 * t), lean=0.05 - 0.17 * t, tilt=-0.18 * t,
                      head=(0.0, -1.0 * t), fist_f=near[i], fist_b=far[i], hump=(0.0, -3 * t, 0.0, -3 * t),
                      glow=0.6 + 0.7 * t, eye=0.8 + 0.6 * t, crack=1.0 + 0.6 * t,
                      elbow_f=RAISED if i >= 3 else (-30.0, 14.0), elbow_b=RAISED if i >= 3 else (-30.0, 14.0))
            f = self.draw(p)
            if i >= 2:
                for k, (fx, fy) in enumerate((near[i], far[i])):
                    self.charge(f, fx, fy, i, 6, self.seed + 20 + 2 * i + k, r0=30, r1=18, count=4 + 2 * i)
            if i >= 4:
                self.sparkle(f, near[i][0] + 6, near[i][1] - 20, 2 + 2 * (i - 4))
            out.append(f)
        return out

    def attack(self, broken=False):
        """Double hammer blow: both fists come down on the ground in front of it (impact f2)."""
        keys = [  # near fist, far fist, hip, lean, glow, eye
            ((130, 34), (115, 34), (114, 162), -0.1, 1.3, 1.4),
            ((194, 122), (182, 116), (116, 166), 0.22, 1.2, 1.3),
            (IMPACT_F, IMPACT_B, (118, 173), 0.38, 1.1, 1.2),
            ((IMPACT_F[0], IMPACT_F[1] - 1), (IMPACT_B[0], IMPACT_B[1] - 1), (118, 172), 0.36, 0.95, 1.0),
            ((170, 176), (156, 174), (116, 168), 0.22, 0.75, 0.9),
        ]
        out = []
        for i, (ff, fb, hip, lean, g, e) in enumerate(keys):
            p = MPose(t=0.5 * i, broken=broken, hip=hip, lean=lean, fist_f=ff, fist_b=fb, glow=g, eye=e,
                      tilt=0.1 * (i >= 2), hump=(0.0, 2.0 if i == 2 else 0.0, 0.0, 2.0 if i == 2 else 0.0),
                      elbow_f=RAISED if i == 0 else (-30.0, 14.0), elbow_b=RAISED if i == 0 else (-30.0, 14.0))
            f = self.draw(p)
            ix = int(ff[0])
            if i == 1:
                # swing smear behind the fists
                sx, sy = self.shoulder(p, True)
                rf = math.hypot(ff[0] - sx, ff[1] - sy)
                af = math.atan2(ff[1] - sy, ff[0] - sx)
                self.smear(f, sx, sy, rf - 16, rf + 18, af - 1.1, af - 0.1)
            if i == 2:
                back = np.zeros_like(f)
                for k, (bx, br) in enumerate(((ix - 18, 12), (ix + 16, 13), (ix + 2, 10))):
                    FX.burst(back, bx, GY - 3, br, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 40 + k, jag=0.45,
                             rays=7)
                back[GY + 3:] = 0
                back[:, W - 3:] = 0
                FX.under(f, back)
                self.ground_cracks(f, ix, 42, self.seed + 41)
                self.shock(f, ix, 30, PC[0])
                self.shock(f, ix, 42, PC[1], dotted=True)
                self.dust(f, ix - 34, 13, self.seed + 42)
                self.dust(f, ix + 22, 8, self.seed + 43)
                self.debris(f, self.seed + 44, ix - 44, ix + 44, 132, 200, 12, sizes=(2, 3, 2, 1))
                f = shake(f, 1, 0)
            if i == 3:
                self.ground_cracks(f, ix, 44, self.seed + 41, level=1)
                self.shock(f, ix, 44, PC[2], dotted=True)
                self.dust(f, ix - 38, 14, self.seed + 45)
                self.dust(f, ix + 22, 9, self.seed + 46)
                self.debris(f, self.seed + 47, ix - 50, ix + 40, 110, 170, 8, sizes=(2, 3, 1))
            if i == 4:
                self.ground_cracks(f, ix, 40, self.seed + 41, level=3)
                self.dust(f, ix - 40, 6, self.seed + 48)
                self.dust(f, ix + 26, 6, self.seed + 49)
                self.debris(f, self.seed + 50, ix - 46, ix + 38, 170, 204, 4, sizes=(1, 2))
            out.append(f)
        return out

    def plant_pose(self, t, broken, i=0, glow=None):
        e = t * t * (3 - 2 * t)
        nf = [(156, 184), (166, 178), (174, 182), (176, 190), PLANT_F, PLANT_F]
        nb = [(76, 180), (96, 172), (122, 174), (140, 184), (146, 190), PLANT_B]
        return MPose(t=0.7 * i, broken=broken, hip=(114 + 4 * e, 164 + 8 * e), lean=0.05 + 0.33 * e, tilt=-0.2 * e,
                     fist_f=nf[i], fist_b=nb[i], glow=0.6 + 0.9 * t if glow is None else glow, eye=0.8 + 0.6 * t,
                     crack=1.0 + t, hump=(0.0, -2.0 * t, 0.0, -2.0 * t))

    def telegraph_special(self, broken=False):
        """It plants both fists on the ground in front of it; every rune flares (quake, eye beam)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = self.plant_pose(t, broken, i)
            f = self.draw(p)
            if i >= 2:
                f = aura(f, i - 2)
            motes(f, 116, 150, 8 + 4 * i, 40, 96, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 50 + i,
                  arc=(-PI * 0.95, -PI * 0.05), big=0.25)
            if i >= 3:
                self.ground_cracks(f, p.fist_f[0], 8 + 10 * (i - 3), self.seed + 55, level=2 - (i - 3))
            if i == 5:
                ex, ey = self.eye_centre(p)
                self.sparkle(f, ex + 2, ey, 5)
            out.append(f)
        return out

    def special(self, broken=False):
        """Loop: fists planted, the eye blazes (the beam leaves it) and the ground shakes."""
        out = []
        for i in range(4):
            p = self.plant_pose(1.0, broken, 5, glow=1.4 + 0.1 * (i % 2))
            p = p.copy(t=1.5 * i, eye=1.5, crack=2.0, hump=(0.0, -2.0 + (i % 2), 0.0, -2.0 - (i % 2)),
                       plate=(0.0, -(i % 2)))
            f = aura(self.draw(p), 3 + i)
            ex, ey = self.eye_centre(p)
            FX.burst(f, ex + 3, ey, 4 + 2 * (i % 2), [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 60 + i, jag=0.3,
                     rays=3)
            for k in range(2):
                r = 16 + ((i * 8 + k * 16) % 32)
                self.shock(f, PLANT_F[0], r, PC[1 + k], dotted=k == 1)
                self.shock(f, 100, r + 4, PC[2 + k], dotted=True)
            self.ground_cracks(f, PLANT_F[0], 30 + 6 * (i % 2), self.seed + 61, level=i % 2)
            # pebbles hopping on the shaking ground
            rng = np.random.default_rng(self.seed + 62)
            for k in range(8):
                x = (10 + rng.random() * 46) if k % 2 else (196 + rng.random() * 20)
                hop = (3 + 6 * rng.random()) * abs(math.sin(PI * (i / 4 + rng.random())))
                FX.chunk(f, x, GY - 4 - hop, 2 + (k % 3 == 0), ROCK_L, ROCK_D)
            out.append(f)
        return out

    def summon(self, broken=False):
        """Fists driven into the ground, then heaved up: rocks torn out of the floor rise around it."""
        keys = [  # near fist, far fist, hip, lean, glow
            ((160, 176), (134, 172), (115, 168), 0.24, 0.9),
            ((164, 204), (142, 202), (117, 172), 0.36, 1.2),
            ((166, 150), (146, 146), (115, 167), 0.16, 1.4),
            ((132, 40), (116, 38), (114, 162), -0.1, 1.5),
            ((134, 44), (118, 42), (114, 162), -0.08, 1.3),
            ((154, 170), (84, 172), (114, 164), 0.04, 0.9),
        ]
        rocks = [(203, 13, 3, True), (24, 13, 7, False), (176, 10, 11, True), (54, 10, 13, False)]
        rise = [None, None, 0.2, 0.55, 0.85, 1.0]
        out = []
        for i, (ff, fb, hip, lean, g) in enumerate(keys):
            up = RAISED if i in (3, 4) else (-30.0, 14.0)
            p = MPose(t=0.6 * i, broken=broken, hip=hip, lean=lean, fist_f=ff, fist_b=fb, glow=g,
                      eye=0.8 + 0.5 * (g - 0.9),
                      tilt=-0.15 if i in (3, 4) else 0.0, crack=1.0 + 0.5 * g, elbow_f=up, elbow_b=up)
            f = self.draw(p)
            if i == 1:
                f[GY:, 120:200] = 0                           # the fists are in the ground
                self.dust(f, 152, 12, self.seed + 70)
                self.debris(f, self.seed + 71, 128, 186, 176, 206, 8)
                self.ground_cracks(f, 152, 40, self.seed + 72)
            if rise[i] is not None:
                s = rise[i]
                for k, (x, r, seed, front) in enumerate(rocks):
                    y = GY - 1 - r * 0.2 - (44 + 14 * (k % 2)) * s
                    layer = self.rock(x, y, r, self.seed + 80 + seed, glow=g)
                    if s < 0.3:
                        layer[GY:] = 0
                        self.dust(f, x, min(r, (W - 4 - x) / 1.8), self.seed + 85 + k)
                    if front:
                        FX.overlay(f, layer)
                    else:
                        FX.under(f, layer)
                    if 0.3 <= s < 0.9:
                        self.debris(f, self.seed + 90 + 7 * i + k, x - r, x + r, y + r + 2, min(GY - 4, y + r + 22), 3,
                                    sizes=(1, 2))
                    motes(f, x, y, 4, r + 2, r + 8, [PC[0], PC[1], PC[2]], seed=self.seed + 95 + 5 * i + k)
                if i == 2:
                    self.ground_cracks(f, 152, 56, self.seed + 72, level=1)
                    self.dust(f, 152, 10, self.seed + 73)
            out.append(f)
        return out

    def roar_pose(self, h, broken, t):
        return MPose(t=t, broken=broken, hip=(114.0, 164.0 - 2 * h), lean=0.05 - 0.2 * h, tilt=-0.35 * h,
                     head=(3 * h, -2 * h), fist_f=(152 - 56 * h, 190 - 16 * h), fist_b=(68 - 30 * h, 184 - 36 * h),
                     hump=(0.0, -3 * h, 0.0, -3 * h), spread=1.6 * h, glow=0.6 + 0.8 * h, eye=0.8 + 0.7 * h,
                     crack=1.0 + h)

    def _roar_fx(self, f, p, i, h, sh):
        if h > 0.8:
            ex, ey = self.eye_centre(p)
            for k, r in enumerate((12 + 3 * (i % 2), 22 + 3 * (i % 2), 32 + 3 * (i % 2))):
                ring(f, ex + 4, ey, r, [PC[0], PC[1], PC[2]][k], -1.1, 1.1, dotted=0 if k < 2 else 2)
            motes(f, 112, 150, 16, 60, 100, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 100 + i, big=0.2,
                  arc=(-PI, 0.0))
            self.debris(f, self.seed + 105 + i, 70, 150, 60 + 8 * (i % 3), 100 + 8 * (i % 3), 4, sizes=(1, 2))
            f = shake(f, sh, 0)
        return f

    def roar(self, broken=False):
        """Phase change: it rears up, flings its arms wide and roars; the light flares."""
        keys = [(0.0, 0), (0.35, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = self.roar_pose(h, broken, 0.8 * i)
            out.append(self._roar_fx(self.draw(p), p, i, h, sh))
        return out

    def shatter(self):
        """Phase 5 roar: the chest plates burst off, the shoulder breaks - form II ("Fendu")."""
        seq = [  # broken, fly, crackle, h, flash
            (False, 0.0, 0.3, 0.35, 0.0), (False, 0.0, 0.7, 0.8, 0.0), (False, 0.0, 1.0, 1.0, 0.35),
            (True, 0.35, 1.0, 1.0, 0.0), (True, 0.75, 1.0, 1.0, 0.0), (True, 1.15, 1.0, 0.9, 0.0),
            (True, 0.0, 1.0, 0.4, 0.0), (True, 0.0, 1.0, 0.0, 0.0)]
        shk = [0, 0, 1, -2, 1, -1, 0, 0]
        out = []
        for i, (broken, fly, crk, h, fl) in enumerate(seq):
            p = self.roar_pose(h, broken, 0.8 * i).copy(fly=fly, crackle=crk, crack=1.0 + crk)
            f = self.draw(p)
            cx, cy = self.B(p, CORE_C)
            if i == 3:
                FX.burst(f, cx + 6, cy, 16, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 110, jag=0.4, rays=10)
            if 3 <= i <= 5:
                rng = np.random.default_rng(self.seed + 111 + i)
                for k in range(8):
                    a = -2.6 + rng.random() * 3.2
                    d = 26 + 16 * (i - 3) + rng.random() * 12
                    FX.chunk(f, cx + math.cos(a) * d, cy + math.sin(a) * d * 0.8 + 8 * (i - 3), 3 if k % 2 else 2,
                             ROCK_L, ROCK_D)
                ring(f, cx, cy, 20 + 14 * (i - 3), PC[1 + (i - 3)], -1.6, 1.6, dotted=0 if i == 3 else 2)
            if i >= 6:
                # the plates lie shattered on the ground in front of it
                self.debris(f, self.seed + 118, 178, 214, GY - 6, GY - 3, 7 - 2 * (i - 6), sizes=(3, 2, 3, 1))
            f = self._roar_fx(f, p, i, h, 0) if h > 0.8 else f
            if shk[i]:
                f = shake(f, shk[i], 0)
            if fl:
                f = flashed(f, fl)
            out.append(f)
        return out

    def hurt(self, broken=False):
        a = MPose(broken=broken, hip=(111.0, 164.0), lean=-0.08, tilt=-0.25, fist_f=(146.0, 186.0),
                  fist_b=(63.0, 181.0), glow=0.95, eye=0.5, lid=0.5, spread=1.0)
        b = MPose(broken=broken, hip=(113.0, 164.0), lean=0.01, tilt=-0.08, fist_f=(150.0, 189.0),
                  fist_b=(66.0, 183.0), glow=0.75, eye=0.8, lid=0.9)
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        self.debris(fb, self.seed + 120, 56, 84, 92, 130, 4, sizes=(1, 2, 2))
        return [fa, fb]

    def death(self, broken=False):
        out = []
        seq = [
            MPose(broken=broken, hip=(113.0, 162.0), lean=-0.12, tilt=-0.35, head=(1.0, -2.0), fist_f=(166.0, 150.0),
                  fist_b=(56.0, 146.0), glow=1.5, eye=1.5, spread=1.5, crackle=1.0, crack=2.2),
            MPose(broken=broken, hip=(114.0, 166.0), lean=0.06, tilt=0.05, fist_f=(150.0, 186.0), fist_b=(66.0, 182.0),
                  glow=0.25, eye=0.2, lid=0.6, spread=0.6, crackle=1.0),
            MPose(broken=broken, hip=(115.0, 168.0), lean=0.1, tilt=-0.1, fist_f=(156.0, 182.0), fist_b=(64.0, 176.0),
                  glow=1.3, eye=1.2, spread=2.6, crackle=1.0, crack=2.4),
            MPose(broken=broken, hip=(116.0, 178.0), lean=0.26, tilt=0.15, fist_f=(160.0, 194.0),
                  fist_b=(130.0, 192.0), glow=0.8, eye=0.6, spread=1.2, crackle=1.0, foot_f=(130.0, float(GY))),
            MPose(broken=broken, hip=(118.0, 184.0), lean=0.42, tilt=0.3, fist_f=(164.0, 196.0),
                  fist_b=(136.0, 196.0), glow=0.6, eye=0.35, lid=0.7, spread=1.6, crackle=1.0, dead=0.3,
                  foot_f=(132.0, float(GY))),
            MPose(broken=broken, hip=(120.0, 188.0), lean=0.56, tilt=0.4, fist_f=(168.0, 197.0),
                  fist_b=(140.0, 197.0), glow=0.45, eye=0.2, lid=0.5, spread=2.4, crackle=1.0, dead=0.5,
                  foot_f=(134.0, float(GY))),
        ]
        shk = [1, -1, 2, 0, 0, 0]
        for i, p in enumerate(seq):
            f = shake(self.draw(p), shk[i], 0)
            if i == 3:
                self.dust(f, 160, 8, self.seed + 130)
                self.dust(f, 130, 7, self.seed + 131)
            out.append(flashed(f, 0.4) if i == 0 else f)
        base = np.asarray(out[-1])
        stones = Stones(base, self.seed + 140, centre_x=118)
        for k, (t, dim) in enumerate(((0.14, 1), (0.32, 1), (0.52, 2), (0.72, 3), (0.9, 4), (1.0, 5))):
            f = stones.frame(t)
            if k >= 2:
                self.dust(f, 80 + 8 * k, 6 + k, self.seed + 150 + k)
                self.dust(f, 168 - 6 * k, 5 + k, self.seed + 156 + k)
            out.append(dimmed(f, dim))
        f = stones.frame(1.0)
        self.dust(f, 118, 6, self.seed + 163)
        f = dimmed(f, 6)
        motes(f, 118, 176, 6, 6, 22, [PC[3], PC[4]], seed=self.seed + 164, arc=(-2.8, -0.3))
        out.append(f)
        out.append(dimmed(stones.frame(1.0), 9))
        return out

    # ======================================================= 2.51: its new attacks
    # « Carapace roulante »: curl (6, it folds into a rune-carved shell), roll (8, loop: the shell
    # rolling right, a full turn), uncurl (4), dizzy (6, loop: reeling, rune pebbles circling its
    # head); « Bond titanesque »: crouch (4), leap (3: up and away), fall (2, loop: coming down,
    # fists joined above its head), land (5, `impact` f0: the ground split, shockwaves both ways);
    # « Tremblement de terre »: quake (4, loop: the fists pound the ground in turn).
    SHELL_R = 30.0                       # the curled golem: a boulder 60 px wide (75 px in game)
    SHELL_C = (112.0, GY - 30.0)

    def shell(self, rot, glow=0.8, sx=1.0, sy=1.0, broken=False, seed=0):
        """The golem curled into a boulder of carved plates (rotated by `rot`, radians,
        clockwise on screen): a granite sphere, seams between five plates, a rune on each plate,
        moss on the plate of its back, crystal veins (form II: glowing cracks)."""
        p = MPose(glow=glow, broken=broken)
        R = self.SHELL_R
        cx, cy = self.SHELL_C[0], GY - R * sy
        cv = TCanvas(W, H)
        cv.part()
        mask = cv.ellipse((cx, cy), R * sx, R * sy, GRANITE)
        self.grain(cv, mask, (cx, cy), rot, 301 + seed)

        def L(r, a):  # polar, in the shell's rotating frame, squashed with it
            return (cx + math.cos(a + rot) * r * sx, cy + math.sin(a + rot) * r * sy)
        # the plates: seams from the hub to the rim (a curled back of stone blocks)
        cv.part(line=False)
        hub = 0.32 * R
        for k in range(5):
            a = TAU * k / 5
            pts = [L(hub, a), L(0.55 * R, a + 0.08), L(0.8 * R, a - 0.04), L(R - 0.5, a + 0.03)]
            for q0, q1 in zip(pts[:-1], pts[1:]):
                cv.line(q0, q1, GRANITE, inten=0.12, decal=True)
        ring_pts = [L(hub, TAU * k / 10) for k in range(11)]
        for q0, q1 in zip(ring_pts[:-1], ring_pts[1:]):
            cv.line(q0, q1, GRANITE, inten=0.16, decal=True)
        # the hub: the golem's head tucked in, its eye shut to a glowing slit
        cv.part()
        hm = cv.ellipse((cx + math.cos(rot) * 2 * sx, cy + math.sin(rot) * 2 * sy), hub * 0.9 * sx, hub * 0.9 * sy, GRANITE,
                        bias=0.08)
        self.grain(cv, hm, (cx, cy), rot, 311 + seed)
        cv.part(line=False)
        e0, e1 = L(hub * 0.15, PI * 0.5), L(hub * 0.55, PI * 0.5)
        cv.line(e0, e1, self.light(p), inten=self.li(p, 0.55 + 0.25 * min(1.2, glow)), decal=True)
        # a rune on every plate, turning with the shell
        for k in range(5):
            a = TAU * k / 5 + TAU / 10
            c = L(0.66 * R, a)
            ang = a + rot + PI / 2
            ca, sa = math.cos(ang), math.sin(ang)
            self.rune(cv, p, k, lambda x, y, c=c, ca=ca, sa=sa: (c[0] + (x * ca - y * sa) * sx, c[1] + (x * sa + y * ca) * sy),
                      size=1.15, inten=0.55 + 0.35 * min(1.3, glow))
        # moss on one plate (the top of its back), veins of crystal across two others
        mc = L(0.7 * R, PI * 1.32)
        self.moss(cv, mc, 7.5 * sx, 3.2 * sy, rot + PI * 1.32 + PI / 2, seed=321, drips=0)
        self.vein(cv, p, [L(0.4 * R, 0.55), L(0.6 * R, 0.75), L(0.78 * R, 0.62), L(0.95 * R, 0.8)],
                  inten=0.55 + 0.25 * min(1.2, glow), wide=broken)
        self.vein(cv, p, [L(0.42 * R, 2.6), L(0.62 * R, 2.45), L(0.86 * R, 2.62)], inten=0.5 + 0.2 * min(1.2, glow))
        if broken:
            # form II: the crystal heart shows between two plates, cracks everywhere
            for a0 in (1.6, 3.9, 5.2):
                self.vein(cv, p, [L(0.35 * R, a0), L(0.6 * R, a0 + 0.12), L(0.9 * R, a0 - 0.05)], inten=0.7, wide=True)
            self.crystal(cv, p, L(0.45 * R, 4.55), rot + 4.55, 9, 2.6)
        img = cv.render()
        img[GY:] = 0
        row = img[GY - 1]
        row[row[:, 3] > 0] = OUTLINE
        return img

    def crouch_pose(self, t, broken, i=0, deep=1.0, arms_back=0.0):
        """Knees bent, hunched, the fists pulled in (t 0..1) - and swung back with `arms_back`."""
        e = t * t * (3 - 2 * t)
        d = deep * e
        nf = (152 - 14 * e - 24 * arms_back * e, 190 + 4 * e - 8 * arms_back * e)
        nb = (68 + 26 * e - 22 * arms_back * e, 184 + 10 * e - 14 * arms_back * e)
        return MPose(t=0.7 * i, broken=broken, hip=(114.0 + 2 * d, 164.0 + 16 * d), lean=0.05 + 0.32 * d,
                     tilt=0.25 * d, head=(1.0 * d, 3.0 * d), fist_f=nf, fist_b=nb,
                     hump=(1.5 * d, 2.0 * d, 1.5 * d, 2.0 * d), glow=0.7 + 0.5 * e, eye=0.8 - 0.3 * e, lid=1.0 - 0.4 * e,
                     crack=1.0 + 0.5 * e)

    def curl(self, broken=False):
        """It folds up (three crouching frames), packs into a boulder, its runes blaze."""
        out = []
        for i, t in enumerate((0.35, 0.7, 1.0)):
            p = self.crouch_pose(t, broken, i)
            f = self.draw(p)
            if i == 2:
                f = aura(f, 1)
            out.append(f)
        f = self.shell(0.0, glow=1.0, sx=1.22, sy=0.86, broken=broken, seed=1)
        self.dust(f, 112 - 34, 9, self.seed + 200)
        self.dust(f, 112 + 34, 9, self.seed + 201)
        out.append(f)
        f = aura(self.shell(0.0, glow=1.25, broken=broken, seed=2), 2)
        self.charge(f, self.SHELL_C[0], self.SHELL_C[1], 3, 6, self.seed + 202, r0=44, r1=34, count=10)
        out.append(f)
        f = aura(self.shell(0.0, glow=1.5, broken=broken, seed=3), 5)
        self.charge(f, self.SHELL_C[0], self.SHELL_C[1], 5, 6, self.seed + 203, r0=44, r1=33, count=12)
        self.sparkle(f, self.SHELL_C[0] + 18, self.SHELL_C[1] - 22, 4)
        out.append(f)
        return out

    def roll(self, broken=False):
        """The shell rolling to the right (a full turn in 8 frames): dust thrown up behind it,
        streaks of light from its runes."""
        out = []
        R = self.SHELL_R
        cx, cy = self.SHELL_C
        for i in range(8):
            f = self.shell(TAU * i / 8, glow=1.2 + 0.1 * (i % 2), broken=broken, seed=10 + i)
            # streaks behind it (left), at the height of its runes
            for k, (dy, ln) in enumerate(((-16, 22), (-2, 30), (12, 18))):
                y = int(cy + dy)
                x0 = int(cx - R - 3 - ((i * 5 + k * 7) % 9))
                for x in range(x0 - ln, x0):
                    if 3 <= x < W and f[y, x, 3] == 0 and (x + i) % 4 != 0:
                        f[y, x] = [PC[1], PC[2], PC[3]][min(2, (x0 - x) * 3 // ln)]
            self.dust(f, cx - R - 6 - (i % 2) * 3, 6 + (i % 3), self.seed + 210 + i)
            self.debris(f, self.seed + 220 + i, cx - R - 24, cx - R - 4, GY - 14, GY - 4, 3, sizes=(1, 2))
            out.append(f)
        return out

    def uncurl(self, broken=False):
        """The boulder bursts open: it unfolds and stands, reeling."""
        out = []
        f = self.shell(0.0, glow=0.9, sx=1.2, sy=0.88, broken=broken, seed=30)
        self.dust(f, 112 - 30, 8, self.seed + 230)
        self.dust(f, 112 + 30, 8, self.seed + 231)
        out.append(f)
        for i, t in enumerate((1.0, 0.55)):
            out.append(self.draw(self.crouch_pose(t, broken, i)))
        out.append(self.draw(self.dizzy_pose(0, broken)))
        return out

    def dizzy_pose(self, i, broken):
        ph = TAU * i / 6
        s = math.sin(ph)
        return MPose(t=ph, broken=broken, hip=(114.0 + 4.0 * s, 165.0), lean=0.1 + 0.11 * s, tilt=0.22 + 0.2 * math.sin(ph + 0.8),
                     head=(2.5 * s, 2.0), fist_f=(150.0 + 4 * s, 196.0), fist_b=(70.0 + 3 * s, 192.0),
                     hump=(0.0, 1.0 + s, 0.0, 1.0 - s), glow=0.45, eye=0.35 + 0.2 * (i % 2), lid=0.45 + 0.15 * (i % 3 == 0))

    def dizzy(self, broken=False):
        """Dizzy after its roll: it sways, its eye flickers half shut, rune pebbles circle over
        its head (the moment to strike it)."""
        out = []
        for i in range(6):
            p = self.dizzy_pose(i, broken)
            f = self.draw(p)
            hx, hy = self.head_centre(p)
            # three pebbles turning (120 degrees apart: 6 frames make the loop seamless)
            for k in range(3):
                a = TAU * k / 3 + TAU * i / 18
                x, y = hx - 2 + math.cos(a) * 24, hy - 24 + math.sin(a) * 7
                front = math.sin(a) > 0
                if front or f[int(y), int(x), 3] == 0:
                    FX.chunk(f, x, y, 3, ROCK_L, ROCK_D)
                    self.sparkle(f, x + 4, y - 4, 2)
            out.append(f)
        return out

    def crouch(self, broken=False):
        """Titanic leap, wind-up: it squats, swings its fists back, the runes of its legs blaze."""
        out = []
        for i, t in enumerate((0.3, 0.6, 0.85, 1.0)):
            p = self.crouch_pose(t, broken, i, deep=1.1, arms_back=1.0).copy(glow=0.7 + 0.7 * t, eye=0.9 + 0.5 * t, lid=1.0)
            f = self.draw(p)
            if i >= 2:
                f = aura(f, i)
                self.ground_cracks(f, 112, 10 + 8 * (i - 2), self.seed + 240, level=3 - i)
            if i == 3:
                self.dust(f, 88, 8, self.seed + 241)
                self.dust(f, 138, 8, self.seed + 242)
            out.append(f)
        return out

    def leap(self, broken=False):
        """It springs up: legs stretched, fists thrown up; the ground bursts under it (f0), then
        streaks of its flight under its feet."""
        out = []
        keys = [  # hip, lean, near fist, far fist, toes
            ((114.0, 156.0), -0.06, (138.0, 52.0), (118.0, 50.0), 0.35),
            ((114.0, 154.0), -0.08, (134.0, 40.0), (116.0, 40.0), 0.5),
            ((114.0, 154.0), -0.06, (132.0, 44.0), (115.0, 44.0), 0.55),
        ]
        for i, (hip, lean, ff, fb, toe) in enumerate(keys):
            p = MPose(t=0.6 * i, broken=broken, hip=hip, lean=lean, tilt=-0.2, head=(0.0, -2.0), fist_f=ff, fist_b=fb,
                      foot_f=(122.0, float(GY)), foot_b=(105.0, float(GY - 3)), toe_f=-toe, toe_b=-toe,
                      elbow_f=RAISED, elbow_b=RAISED, hump=(0.0, -3.0, 0.0, -3.0), glow=1.2, eye=1.3)
            f = self.draw(p)
            if i == 0:
                back = np.zeros_like(f)
                FX.burst(back, 112, GY - 3, 16, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 250, jag=0.45, rays=8)
                back[GY + 3:] = 0
                FX.under(f, back)
                self.dust(f, 80, 12, self.seed + 251)
                self.dust(f, 146, 12, self.seed + 252)
                self.debris(f, self.seed + 253, 70, 156, 170, 206, 10, sizes=(2, 3, 2, 1))
            else:
                # the air torn behind it: streaks below its feet
                for k, x in enumerate((96, 108, 120, 130)):
                    y0 = GY - 30 + (k % 2) * 6
                    for y in range(y0, min(H - 1, y0 + 26 + 6 * i)):
                        if f[y, x, 3] == 0 and (y + k + i) % 5 != 0:
                            f[y, x] = [C("#d8cfc0"), C("#b3a691"), C("#8e8270")][min(2, (y - y0) // 10)]
            out.append(f)
        return out

    def fall(self, broken=False):
        """Coming down on its shadow: knees up, both fists joined above its head, the air
        rushing past it (streaks above)."""
        out = []
        for i in range(2):
            p = MPose(t=0.8 * i, broken=broken, hip=(114.0, 160.0 + i), lean=0.12, tilt=0.1, head=(1.0, 1.0),
                      fist_f=(128.0, 50.0 + 2 * i), fist_b=(116.0, 52.0 + 2 * i), foot_f=(126.0, float(GY - 6)),
                      foot_b=(100.0, float(GY - 3)), toe_f=0.2, toe_b=0.2, elbow_f=RAISED, elbow_b=RAISED,
                      hump=(0.0, -2.0, 0.0, -2.0), glow=1.3 + 0.1 * i, eye=1.4)
            f = aura(self.draw(p), 2 + i)
            for k, x in enumerate((84, 98, 112, 126, 140, 152)):
                y1 = 30 + ((k * 7 + i * 11) % 18)
                for y in range(max(1, y1 - 30 - 6 * (k % 2)), y1):
                    if f[y, x, 3] == 0 and (y + k) % 6 != 0:
                        f[y, x] = [C("#8e8270"), C("#b3a691"), C("#d8cfc0")][min(2, (y1 - y) // 11)]
            out.append(f)
        return out

    def land(self, broken=False):
        """It lands (impact f0): a deep crouch, fists slammed down on both sides, the floor split,
        shockwaves leaving both ways; then it straightens."""
        out = []
        keys = [  # crouch t, near fist, far fist, wave radius
            (1.0, (178.0, 194.0), (56.0, 192.0), 26),
            (1.0, (177.0, 193.0), (57.0, 191.0), 44),
            (0.75, (170.0, 190.0), (62.0, 188.0), 62),
            (0.45, (162.0, 190.0), (66.0, 186.0), 0),
            (0.15, (155.0, 190.0), (68.0, 185.0), 0),
        ]
        for i, (t, ff, fb, wave) in enumerate(keys):
            p = self.crouch_pose(t, broken, i, deep=1.15).copy(fist_f=ff, fist_b=fb, glow=1.3 - 0.15 * i, eye=1.3 - 0.1 * i, lid=1.0,
                                                               elbow_f=(-10.0, 30.0), elbow_b=(-36.0, 20.0))
            f = self.draw(p)
            if i == 0:
                back = np.zeros_like(f)
                for k, (bx, br) in enumerate(((60, 12), (112, 16), (172, 12))):
                    FX.burst(back, bx, GY - 3, br, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 260 + k, jag=0.45, rays=7)
                back[GY + 3:] = 0
                back[:, W - 3:] = 0
                back[:, :3] = 0
                FX.under(f, back)
                f = shake(f, 0, 1)
            if wave:
                for cxw in (112 - 40 - wave, 112 + 40 + wave):
                    if 8 < cxw < W - 8:
                        self.shock(f, cxw, 14, PC[0 if i == 0 else 1])
                        self.shock(f, cxw, 20, PC[1 if i == 0 else 2], dotted=True)
            if i <= 2:
                self.ground_cracks(f, 112, 70 - 8 * i, self.seed + 263, level=i)
                self.dust(f, 112 - 52 - 6 * i, 12 - 2 * i, self.seed + 264 + i)
                self.dust(f, 112 + 52 + 6 * i, 12 - 2 * i, self.seed + 268 + i)
                self.debris(f, self.seed + 272 + i, 30, 194, 120 + 20 * i, 200, 12 - 3 * i, sizes=(2, 3, 2, 1))
            out.append(f)
        return out

    def quake(self, broken=False):
        """Loop: its fists pound the ground in turn; the ground cracks and shakes, pebbles hop."""
        out = []
        keys = [((166.0, 82.0), PLANT_B, 2), (PLANT_F, (140.0, 86.0), 1), ((162.0, 90.0), PLANT_B, 2),
                (PLANT_F, (136.0, 92.0), 1)]
        for i, (ff, fb, hit) in enumerate(keys):
            p = self.plant_pose(1.0, broken, 5, glow=1.2 + 0.15 * (hit == 1)).copy(
                t=1.1 * i, fist_f=ff, fist_b=fb, eye=1.1, crack=1.8,
                elbow_f=RAISED if ff[1] < 160 else (-30.0, 14.0), elbow_b=RAISED if fb[1] < 160 else (-30.0, 14.0),
                hump=(0.0, -1.0 + (i % 2), 0.0, -1.0 - (i % 2)))
            f = self.draw(p)
            hx = ff[0] if hit == 1 else fb[0]
            if hit:
                self.ground_cracks(f, hx, 34, self.seed + 280 + i, level=0)
                self.shock(f, hx, 18, PC[0])
                self.shock(f, hx, 30, PC[1], dotted=True)
                self.dust(f, hx - 22, 8, self.seed + 284 + i)
                self.dust(f, min(W - 20, hx + 18), 7, self.seed + 288 + i)
                f = shake(f, 1 if i % 2 else -1, 0)
            rng = np.random.default_rng(self.seed + 292)
            for k in range(8):
                x = (10 + rng.random() * 40) if k % 2 else (198 + rng.random() * 18)
                hop = (3 + 7 * rng.random()) * abs(math.sin(PI * (i / 4 + rng.random())))
                FX.chunk(f, x, GY - 4 - hop, 2 + (k % 3 == 0), ROCK_L, ROCK_D)
            out.append(f)
        return out

    def new_clips(self, form2=False):
        """The clips of 2.51, packed after the others (the frames of before do not move)."""
        b = form2
        return [
            ("curl", self.curl(b), dict(durations=[0.1, 0.1, 0.12, 0.08, 0.14, 0.2], loop=False)),
            ("roll", self.roll(b), dict(fps=16, loop=True)),
            ("uncurl", self.uncurl(b), dict(durations=[0.08, 0.08, 0.08, 0.12], loop=False)),
            ("dizzy", self.dizzy(b), dict(fps=7, loop=True)),
            ("crouch", self.crouch(b), dict(durations=[0.1, 0.1, 0.12, 0.2], loop=False)),
            ("leap", self.leap(b), dict(durations=[0.06, 0.1, 0.12], loop=False)),
            ("fall", self.fall(b), dict(fps=10, loop=True)),
            ("land", self.land(b), dict(durations=[0.08, 0.1, 0.12, 0.14, 0.16], loop=False, events=[(0, "impact")])),
            ("quake", self.quake(b), dict(fps=8, loop=True)),
        ]

    # ------------------------------------------------------------ the sheet
    def clips(self, form2=False):
        b = form2
        return [
            ("idle", self.idle(b), dict(fps=self.IDLE_FPS, loop=True)),
            ("telegraph", self.telegraph(b), dict(durations=TIMING["telegraph"], loop=False)),
            ("walk", self.walk(b), dict(fps=self.WALK_FPS, loop=True, events=self.WALK_EVENTS)),
            ("attack", self.attack(b), dict(durations=TIMING["attack"], loop=False, events=self.ATTACK_EVENTS)),
            ("telegraph_special", self.telegraph_special(b), dict(durations=TIMING["telegraph_special"], loop=False)),
            ("special", self.special(b), dict(fps=self.SPECIAL_FPS, loop=True)),
            ("hurt", self.hurt(b), dict(durations=TIMING["hurt"], loop=False)),
            ("summon", self.summon(b), dict(durations=TIMING["summon"], loop=False)),
            ("roar", self.roar(b), dict(durations=TIMING["roar"], loop=False)),
            ("death", self.death(b), dict(durations=TIMING["death"], loop=False)),
        ] + self.new_clips(form2)


IMPACT_F, IMPACT_B = (176.0, 192.0), (164.0, 191.0)   # fist centres on the impact frame (attack f2)
RAISED = (-24.0, -26.0)                               # elbow hint of the fists raised over the head
PLANT_F, PLANT_B = (176.0, 194.0), (148.0, 190.0)     # fists planted (telegraph_special, special)


class Stones:
    """The crumbling: a frame cut into stones (jagged Voronoi cells of its pixels) that fall and
    come to rest in a low mound on the ground (each stone keeps its shape and gets its own
    outline; the stones of the pile overlap like rubble)."""

    def __init__(self, base, seed, centre_x, n=58, height=46, half=74):
        img = np.asarray(base).copy()
        alpha = img[:, :, 3] > 0
        # drop the outer outline: every stone gets its own
        out_px = alpha & np.all(img[:, :, :3] == np.array(OUTLINE[:3], np.uint8), axis=2)
        edge = np.zeros_like(alpha)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            edge |= ~np.roll(np.roll(alpha, dy, 0), dx, 1)
        img[out_px & edge] = 0
        alpha = img[:, :, 3] > 0
        ys, xs = np.nonzero(alpha)
        rng = np.random.default_rng(seed)
        pick = rng.choice(len(ys), n, replace=False)
        sx, sy = xs[pick].astype(np.float32), ys[pick].astype(np.float32)
        wx = xs + 1.6 * np.sin(ys * 0.61 + 1.3)
        wy = ys + 1.6 * np.sin(xs * 0.53 + 0.4)
        d = (wx[:, None] - sx[None, :]) ** 2 + (wy[:, None] - sy[None, :]) ** 2 * 1.3
        lab = np.argmin(d, axis=1)
        self.img = img
        self.cells = []
        for k in range(n):
            sel = lab == k
            if sel.sum() < 3:
                continue
            self.cells.append((xs[sel], ys[sel]))
        self.moves = {}
        keys = {}
        for k, (cx_, cy_) in enumerate(self.cells):
            mx = float(cx_.mean())
            hgt = float(cy_.max() - cy_.min())
            tx = centre_x + (mx - centre_x) * 1.3 + rng.uniform(-6, 6)
            tx = min(W - 4 - (cx_.max() - mx), max(3 + (mx - cx_.min()), tx))
            u = (tx - centre_x) / half
            surf = GY - 2 - height * max(0.0, 1.0 - u * u)          # the mound's surface at tx
            lo = min(GY - 2.0, surf + hgt)
            bottom = lo + rng.random() ** 1.5 * (GY - 2 - lo)
            dx = int(round(tx - mx))
            dy = max(0, int(round(bottom - cy_.max())))
            self.moves[k] = (dx, dy, rng.random() * 0.28)
            keys[k] = bottom + rng.random() * 12
        self.order = sorted(keys, key=lambda k: keys[k])         # the front (low) stones last

    def frame(self, t):
        out = np.zeros((H, W, 4), np.uint8)
        for k in self.order:
            xs, ys = self.cells[k]
            dx, dy, delay = self.moves[k]
            s = min(1.0, max(0.0, (t - delay) / max(1e-3, 1.0 - delay)))
            ox, oy = int(round(dx * (s * s * (3 - 2 * s)))), int(round(dy * s * s))
            X, Y = xs + ox, ys + oy
            ok = (X >= 0) & (X < W) & (Y >= 0) & (Y < H)
            m = np.zeros((H, W), bool)
            m[Y[ok], X[ok]] = True
            ring_m = np.zeros_like(m)
            for ddy, ddx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                ring_m |= np.roll(np.roll(m, ddy, 0), ddx, 1)
            ring_m &= ~m
            out[ring_m] = OUTLINE
            out[Y[ok], X[ok]] = self.img[ys[ok], xs[ok]]
        return out


def aura(f, phase, rows=GY):
    """Glow around the silhouette (runes flaring): a P3 rim hugging the outline and a broken P2
    halo beyond it, shimmering with `phase`."""
    out = np.asarray(f).copy()
    a = out[:, :, 3] > 0

    def grow(m):
        g = m.copy()
        g[1:] |= m[:-1]
        g[:-1] |= m[1:]
        g[:, 1:] |= m[:, :-1]
        g[:, :-1] |= m[:, 1:]
        return g
    r1 = grow(a) & ~a
    r2 = grow(a | r1) & ~(a | r1)
    r1[rows:] = False
    r2[rows:] = False
    n = _hash(cv_x + 0.0, np.floor(cv_y / 2) + phase * 5.0, 3)
    out[r1 & (n < 0.75)] = PC[3]
    out[r2 & (n < 0.4)] = PC[2]
    return out


class Flashed(np.ndarray):
    """A frame to be flashed white once recoloured (a flash blends colours out of the phase
    ramps, so it is applied to each phase sheet after the LUT)."""
    flash = 0.0


def flashed(f, amount):
    g = np.asarray(f).copy().view(Flashed)
    g.flash = amount
    return g


def finish(f, phase):
    """Recolour a frame for `phase`, then apply its pending flash."""
    out = recolour(np.asarray(f), phase)
    amount = getattr(f, "flash", 0.0)
    return FX.flash(out, color=(255, 255, 255, 255), amount=amount) if amount else out


# The light going out: white -> P0 .. P4 -> extinct crystal greys.
LADDER = [WHITE_C, *PC, C("#4f4b57"), C("#35323c")]


def dimmed(f, steps):
    """Every light pixel `steps` tones further down the ladder (exact colours)."""
    out = np.asarray(f).copy()
    rgb = out[:, :, :3]
    src = [np.all(rgb == np.array(c[:3], np.uint8), axis=2) & (out[:, :, 3] > 0) for c in LADDER]
    for i, sel in enumerate(src):
        j = min(len(LADDER) - 1, i + steps)
        out[sel, :3] = LADDER[j][:3]
    return out


# ------------------------------------------------------------------ recolour + packing
def recolour(img, phase):
    """The phase-1 sheet with the five light tones of `phase` (0-based): an exact LUT."""
    if phase == 0:
        return img.copy()
    out = img.copy()
    src = [C(h) for h in PHASES[0][2]]
    dst = [C(h) for h in PHASES[phase][2]]
    rgb = img[:, :, :3]
    for a, b in zip(src, dst):
        sel = (rgb[:, :, 0] == a[0]) & (rgb[:, :, 1] == a[1]) & (rgb[:, :, 2] == a[2]) & (img[:, :, 3] > 0)
        out[sel, :3] = b[:3]
    return out


COLUMNS = 14  # the longest clip (death) fills a row; the others are packed side by side


def pack(clips, columns=COLUMNS):
    """Clips placed left to right, several per row (never split).  `move` shares `walk`'s frames.
    Returns (image, meta clips)."""
    rows, meta = [], {}
    cur, used = [], 0
    for name, frames, kw in clips:
        if used + len(frames) > columns:
            rows.append(cur)
            cur, used = [], 0
        cur.append((name, frames, kw, used))
        used += len(frames)
    if cur:
        rows.append(cur)
    img = np.zeros((H * len(rows), W * columns, 4), np.uint8)
    for r, row in enumerate(rows):
        for name, frames, kw, start in row:
            for c, f in enumerate(frames):
                img[r * H:(r + 1) * H, (start + c) * W:(start + c + 1) * W] = np.asarray(f)
            m = {"row": r, "count": len(frames)}
            if start:
                m["start"] = start
            if kw.get("durations") is not None:
                assert len(kw["durations"]) == len(frames), name
                m["durations"] = [round(float(d), 3) for d in kw["durations"]]
            else:
                m["fps"] = kw.get("fps", 10)
            m["loop"] = bool(kw.get("loop", True))
            if kw.get("events"):
                m["events"] = [{"frame": int(fr), "name": n} for fr, n in kw["events"]]
            meta[name] = m
            if name == "walk":
                meta["move"] = dict(m)
    return img, meta


BOSS = Monolithe()


def phase_clips(k, form1, form2, shat):
    clips = form1 if k < 4 else (shat if k == 4 else form2)
    return [(name, [finish(f, k) for f in frames], kw) for name, frames, kw in clips]


def phase_sheets():
    """(phase index 0..7, image, clip meta) of the eight phase sheets."""
    form1 = BOSS.clips(False)
    form2 = BOSS.clips(True)
    shat = [c if c[0] != "roar" else ("roar", BOSS.shatter(), c[2]) for c in form2]
    out = []
    for k in range(8):
        img, meta = pack(phase_clips(k, form1, form2, shat))
        out.append((k, img, meta))
    return out


def build():
    from sheet import save_json, save_png
    for k, img, meta in phase_sheets():
        name = "monolithe" if k == 0 else "monolithe_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
