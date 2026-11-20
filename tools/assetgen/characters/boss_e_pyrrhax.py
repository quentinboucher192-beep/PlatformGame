"""Pyrrhax, le Dragon d'Obsidienne (giga boss, eight phases).

A great winged dragon of black obsidian scales split by glowing magma seams.  Seen from the side,
facing right (the game flips it), always flying: a long neck carrying a horned head forward-right,
two great membranous wings (the far one behind the body), clawed fore and hind legs hanging under
the body, a long tail ending in a spiked mace behind.  The scales are faceted plates (Voronoi
cells along the body, the neck and the tail); the seams between them glow, the eye, the mouth, the
throat and the breath are fire.

Two forms:
  * form I  (phases 1-4): the armour is whole (only the seams glow);
  * form II (phases 5-8): « Brisé » - the scales of the chest are torn off and show the magma heart,
    the near wing is torn (a notch, two holes with burning rims, a broken finger), the seams are
    wider and brighter.
The light (seams, eyes, mouth, throat, heart, breath, sparks) changes colour at every phase; the
obsidian never does.  Every emissive pixel uses one of five tones P0..P4 of the phase ramp (plus
pure white), so each phase sheet is the phase-1 sheet with those five colours swapped (exact LUT,
`recolour`), see PHASES.

Frames 256x192, pivot [128, 152] = bottom-centre of the 88x60 collision box (x 84..172,
y 92..152) around the body (torso and legs, the claws hang just below; not the wings, the neck,
the head or the tail).  Drawn at scale 1.25 in game (320x240 px frame, 110x75 px box).
ORIGIN (projectile origin = the mouth, frame pixels from the pivot, x forward, y down; x1.25 in
game):
  * attack frame 1 (`fire`, the bite: jaws wide, a fireball leaves the mouth): (+110, -52)
    = frame pixel (238, 100);
  * special (fire breath loop, the head does not move on its four frames): (+100, -57)
    = frame pixel (228, 95); the beam itself is drawn by the game, the sheet only shows a short
    flame at the mouth.
`Pyrrhax.mouth(pose)` gives these points (see `attack_poses` and `special_pose`).
Clips (packed several per row, `start` = first column): idle 8 (hover, wings flapping), walk 8
(= move, same frames: glide), telegraph 6 (rears its head), attack 5 (bite + claw swipe, fire f1),
telegraph_special 6 (inhales: chest and throat glow), special 4 (loop, fire breath), summon 6
(spreads its wings, roars to the sky), roar 8 (phase change; the phase-5 sheet's roar is the
scales bursting off), hurt 2, death 14 (falls, the magma cools into grey stone, crumbles); 2.76.2, packed after
them (the cells of before unchanged): telegraph_dive 6 (it folds its wings, tips its nose down), dive 4 (loop,
wings folded: the dives with the rules of 17, « clips » of its parts in data/enemies/pyrrhax.json).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, bezier, mat, motes, ring, streak
from boss_c_common import TAU, TIMING, Pose, shake
from raster import TCanvas

PI = math.pi
W, H = 256, 192
PIVOT = [128, 152]
BOX = (84, 92, 172, 152)  # x0, y0, x1, y1 (collision box, frame pixels)
BX, BY = 128.0, 112.0  # centre of the body (body coordinates): the whole dragon tilts around it
YOFF = 6.0  # body coordinates -> frame: the body sits YOFF px lower in the frame
HS = 1.0  # head scale
WHITE = "#ffffff"
SEED = 51417

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Braise", "orange profond", ["#ffe2b6", "#ff9a3a", "#f25a12", "#a8300e", "#561608"]),
    ("Lave", "or", ["#fff6c6", "#ffd440", "#f39a16", "#a85a12", "#582c0a"]),
    ("Flamme bleue", "bleu", ["#e2eaff", "#8aa6ff", "#4a62f4", "#2a32b2", "#141660"]),
    ("Feu d'âme", "violet", ["#f2dcff", "#c28cff", "#8a4cf0", "#5428a8", "#2a125e"]),
    ("Cendre vive", "magenta", ["#ffdcf0", "#ff6ccc", "#e22896", "#921864", "#4a0a34"]),
    ("Feu vert", "émeraude", ["#dcffe8", "#6cf8a8", "#1ec872", "#127a4c", "#0a3c28"]),
    ("Givre ardent", "azur", ["#e4fcff", "#98eaff", "#3cc2ee", "#1a7ab2", "#0c3a62"]),
    ("Soleil noir", "blanc incandescent", ["#fffdf2", "#f2eeff", "#c4c0e6", "#7670ac", "#2c2646"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Obsidian (never recoloured): glassy black-violet, a sharp gloss on top.
OBS = mat("px_obs", ["#928cb8", "#433e68", "#2e2a47", "#201d33", "#151322"], th=[0.86, 0.66, 0.46, 0.27])
OBS_FAR = mat("px_obs_far", ["#3e3a5a", "#2a2740", "#1c1a2c", "#12111d"], th=[0.78, 0.52, 0.3])
MEMB = mat("px_memb", ["#56465f", "#3e3149", "#2c2337", "#1e1727"], th=[0.7, 0.5, 0.3])
MEMB_FAR = mat("px_memb_far", ["#33293d", "#251d2e", "#191321"], th=[0.56, 0.32])
CLAW = mat("px_claw", ["#dcd6ec", "#a198ba", "#635c7c"], th=[0.7, 0.42])
TOOTH = mat("px_tooth", ["#f2eee6", "#c0b9ab", "#7e776a"], th=[0.7, 0.42])
MAW = mat("px_maw", ["#3c1828", "#240d19", "#13070d"], th=[0.6, 0.3])
# Cooled stone (death): neutral grey, never recoloured.
STONE = mat("px_stone", ["#a7a29a", "#7d7870", "#5b5751", "#3e3b37", "#272522"], th=[0.9, 0.7, 0.48, 0.27])
STONE_FAR = mat("px_stone_far", ["#615d57", "#46433e", "#302e2b"], th=[0.6, 0.34])
STONE_MEMB = mat("px_stone_memb", ["#6e6a63", "#55524c", "#3d3a36"], th=[0.62, 0.36])
ASH = mat("px_ash", ["#6a645e", "#4a4541", "#332f2c"], th=[0.6, 0.34])
# Light (recoloured per phase).
HOT = mat("px_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("px_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
STONE_MAP = {"px_obs": STONE, "px_obs_far": STONE_FAR, "px_memb": STONE_MEMB, "px_memb_far": STONE_FAR,
             "px_hot": ASH, "px_core": ASH, "px_claw": STONE, "px_tooth": STONE, "px_maw": STONE_FAR}

# ------------------------------------------------------------------ the skeleton (body coordinates)
NECK_BASE = (158.0, 99.0)
TAIL_BASE = (102.0, 108.0)
SHOULDER, SHOULDER_FAR = (136.0, 94.0), (146.0, 91.0)
WING_ROOT = (96.0, 106.0)  # the membrane's trailing edge joins the flank here
HEART = (160.0, 104.0)  # the magma heart (form II), the glowing chest (inhale)
# Wing, relative to the shoulder, raised (y up = negative): elbow, wrist, four finger tips.
WING_OPEN = ((-6.0, -30.0), (4.0, -60.0), ((-20.0, -92.0), (-56.0, -85.0), (-89.0, -62.0), (-104.0, -30.0)))
WING_FOLD = ((-10.0, -16.0), (-6.0, -30.0), ((-14.0, -38.0), (-20.0, -33.0), (-25.0, -26.0), (-28.0, -17.0)))
# Dorsal spikes of the back: (x, y, angle, length).
BACK_SPIKES = [(160, 89, -1.8, 8), (147, 89, -1.95, 10), (133, 90, -2.1, 11), (119, 91, -2.25, 11),
               (105, 95, -2.4, 10), (93, 99, -2.55, 8)]
# Head, local (x forward along the head, y down), before HS.
UPPER = [(-10, -3), (-8, -10), (0, -14), (10, -14.5), (19, -12), (28, -8.5), (35, -6), (40, -2.5), (40, 1),
         (34, 2.8), (20, 3.4), (6, 3.8), (-4, 6.5), (-10, 3)]
LOWER = [(-4, 3.2), (8, 3.8), (22, 3.8), (35, 3.2), (34, 6), (27, 8.5), (14, 10.5), (3, 11), (-5, 8.5)]
HINGE = (-3.0, 4.5)
BROW = [(3, -14.5), (21, -12), (23, -7.8), (8, -9)]
NEAR_HORN = [(2, -11), (-6, -19), (-17, -24), (-31, -23)]
FAR_HORN = [(7, -12), (1, -21), (-7, -28), (-18, -31)]


def _hash(a, b, salt=0.0):
    x = np.sin(np.asarray(a, np.float64) * 12.9898 + np.asarray(b, np.float64) * 78.233 + salt * 37.719) * 43758.5453
    return x - np.floor(x)


def _plates(u, w, seeds):
    """Voronoi of points (u, w) over seeds (K, 2): nearest index, second index, gap d2 - d1."""
    d = np.hypot(u[:, None] - seeds[None, :, 0], w[:, None] - seeds[None, :, 1])
    order = np.argsort(d, axis=1)[:, :2]
    i1, i2 = order[:, 0], order[:, 1]
    r = np.arange(len(u))
    return i1, i2, d[r, i2] - d[r, i1]


def _dilate(m, n=1):
    out = m.copy()
    for _ in range(n):
        g = out.copy()
        g[1:] |= out[:-1]
        g[:-1] |= out[1:]
        g[:, 1:] |= out[:, :-1]
        g[:, :-1] |= out[:, 1:]
        out = g
    return out


def _torso_seeds(broken):
    rng = np.random.default_rng(SEED + 5)
    pts = []
    for j, y in enumerate(range(86, 140, 13)):
        for x in range(74 + (j % 2) * 8, 182, 16):
            pts.append((x + rng.uniform(-3.5, 3.5), y + rng.uniform(-2.5, 2.5)))
    if broken:  # smaller plates around the torn chest: more seams
        for k in range(9):
            a = TAU * k / 9 + 0.3
            pts.append((HEART[0] + math.cos(a) * 17 + rng.uniform(-2, 2),
                        HEART[1] + math.sin(a) * 16 + rng.uniform(-2, 2)))
    return np.array(pts, np.float32)


TORSO_SEEDS = (_torso_seeds(False), _torso_seeds(True))


def _chain_seeds(length, step, salt):
    """Plates along a chain, in (u, 9 * v / r) space: two staggered rows (the belly is a band)."""
    rng = np.random.default_rng(SEED + salt)
    pts = []
    u = 1.0
    while u < length + step:
        for row, vn in enumerate((-0.5, 0.3)):
            off = (step * 0.5) if row == 1 else 0.0
            pts.append((u + off + rng.uniform(-1.5, 1.5), 9.0 * vn + rng.uniform(-0.8, 0.8)))
        u += step
    return np.array(pts, np.float32)


class DPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, tilt=0.0, t=0.0, wing=(0.6, 0.6), wing_far=None, fold=0.0, spread=1.0,
                    head=(198.0, 62.0), hang=0.06, jaw=0.0, glow=0.6, chest=0.0, eye=0.3, sway=0.0, tail=1.0,
                    fore=(0.0, 0.2, 0.0), hind=0.2, broken=False, stone=0.0, seam=1.0, flame=0.0)
    # wing / wing_far: (k of the arm, k of the finger tips), k = 1 raised .. 0 edge-on .. -0.8 lowered
    # fore: (reach 0..1, claws open 0..1, lift 0..1); hind: tuck 0..1; hang: head angle (0 = right)


def flap(ph):
    """Wing height over a flap cycle: 1 raised at ph = 0, -0.75 lowered at ph = pi."""
    return 0.125 + 0.875 * math.cos(ph)


def wings(ph, lag=0.75, far_lag=0.3):
    """Near and far wing (arm, tips) for a flap phase; the tips trail the arm."""
    return dict(wing=(flap(ph), flap(ph - lag)), wing_far=(flap(ph - far_lag), flap(ph - far_lag - lag)))


def held(k, kt=None, far=0.03):
    kt = k if kt is None else kt
    return dict(wing=(k, kt), wing_far=(k - far, kt - far))


class Pyrrhax:
    id = "pyrrhax"
    W, H, PIVOT = W, H, PIVOT
    seed = SEED
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 9, 10, 12
    ATTACK_EVENTS = [(1, "fire")]

    # ------------------------------------------------------------ geometry
    @staticmethod
    def canvas(p):
        return TCanvas(W, H, rot=p.tilt, center=(BX, BY), offset=(p.dx, p.dy + YOFF))

    @staticmethod
    def T(p, q):
        """Body coordinates -> frame pixels (same transform as the canvas)."""
        c, s = math.cos(p.tilt), math.sin(p.tilt)
        dx, dy = q[0] - BX, q[1] - BY
        return (BX + dx * c - dy * s + p.dx, BY + dx * s + dy * c + p.dy + YOFF)

    @staticmethod
    def local(cv):
        """Body coordinates of every pixel centre of the canvas."""
        x = cv.px - cv.t_off[0] - cv.t_c[0]
        y = cv.py - cv.t_off[1] - cv.t_c[1]
        c, s = math.cos(-cv.t_rot), math.sin(-cv.t_rot)
        return cv.t_c[0] + x * c - y * s, cv.t_c[1] + x * s + y * c

    @staticmethod
    def head_fns(p):
        """HL: head-local -> body coordinates; JL: the same for the lower jaw (opened by p.jaw)."""
        hx, hy = p.head
        ca, sa = math.cos(p.hang), math.sin(p.hang)

        def HL(lx, ly):
            lx, ly = lx * HS, ly * HS
            return (hx + lx * ca - ly * sa, hy + lx * sa + ly * ca)

        ja = p.jaw * 0.62
        jc, js = math.cos(ja), math.sin(ja)

        def JL(lx, ly):
            dx, dy = lx - HINGE[0], ly - HINGE[1]
            return HL(HINGE[0] + dx * jc - dy * js, HINGE[1] + dx * js + dy * jc)

        return HL, JL

    def mouth_dir(self, p):
        return p.hang + p.tilt + 0.3 * p.jaw

    def mouth_body(self, p):
        HL, JL = self.head_fns(p)
        a, b = HL(39, 1.8), JL(34, 5.0)
        d = p.hang + 0.3 * p.jaw
        return ((a[0] + b[0]) / 2 + math.cos(d) * 2.5, (a[1] + b[1]) / 2 + math.sin(d) * 2.5)

    def mouth(self, p):
        """The projectile origin (frame pixels)."""
        return self.T(p, self.mouth_body(p))

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = self.canvas(p)
        self._wing(cv, p, far=True)
        self._hind(cv, p, far=True)
        self._fore(cv, p, far=True)
        self._tail(cv, p)
        self._neck(cv, p)
        self._torso(cv, p)
        self._hind(cv, p, far=False)
        self._fore(cv, p, far=False)
        self._wing(cv, p, far=False)
        self._head(cv, p)
        if p.flame > 0.02:
            self._flame(cv, p)
        if p.stone > 0.0:
            self._petrify(cv, p)
        return cv.render()

    def seam_i(self, p, pg, gap, width, extra=0.0):
        """Intensity of a glowing seam pixel (pg: brightness of that seam, gap: distance to its middle)."""
        g = p.glow + (0.15 if p.broken else 0.0)
        return np.clip(0.22 + 0.5 * g * (0.55 + 0.45 * pg) + 0.16 * (1 - gap / width) + extra, 0, 1)

    @staticmethod
    def spike(cv, T, base, ang, ln, hw, m, curve=0.35):
        c, s = math.cos(ang), math.sin(ang)
        nx, ny = -s, c
        b0 = (base[0] + nx * hw, base[1] + ny * hw)
        b1 = (base[0] - nx * hw, base[1] - ny * hw)
        tip = (base[0] + c * ln, base[1] + s * ln)
        mid = (base[0] + c * ln * 0.55 + nx * hw * curve, base[1] + s * ln * 0.55 + ny * hw * curve)
        cv.poly([T(b0), T(mid), T(tip), T(b1)], m, bevel=1.2, base=0.56)
        return tip

    def _plated(self, cv, p, mask, u, w, seeds, width, extra=None, salt=0, light=(-0.62, -0.78)):
        """Faceted obsidian plates on the pixels `mask` (plate coordinates u, w): bevelled edges
        (lit towards `light`), dark joints, and some joints split open on glowing magma."""
        i1, i2, gap = _plates(u, w, seeds)
        # flat glassy facets: each plate keeps a little of the body's rounding around its mean light
        raw = cv.inten[mask]
        k = len(seeds)
        mean = np.bincount(i1, raw, k) / np.maximum(1, np.bincount(i1, None, k))
        inten = mean[i1] + 0.3 * (raw - mean[i1]) + (_hash(i1, 7, salt) - 0.5) * 0.14
        vx, vy = u - seeds[i1, 0], w - seeds[i1, 1]
        facing = (vx * light[0] + vy * light[1]) / (np.hypot(vx, vy) + 1e-3)
        edge = gap < width + 1.1
        inten = np.where(edge & (facing > 0.35), inten + 0.26, inten)  # a sharp glint on the lit edge
        inten = np.where((gap < width + 1.6) & (facing < -0.3), inten - 0.1, inten)
        crack = gap < width
        ex = np.zeros(len(u), np.float32) if extra is None else extra
        pg = _hash(np.minimum(i1, i2), np.maximum(i1, i2), 1 + salt)
        thr = 0.42 if p.broken else 0.62
        hot = crack & ((pg > thr) | (ex > 0.12))
        inten = np.where(crack & ~hot, 0.0, inten)  # dark joints between the scales
        cv.inten[mask] = np.clip(inten, 0, 1)
        si = self.seam_i(p, np.clip((pg - thr) / (1 - thr), 0, 1), gap, width, ex)
        cm = np.zeros(mask.shape, bool)
        cm[mask] = hot
        it = np.zeros(mask.shape, np.float32)
        it[mask] = si
        cv.part(line=False)
        cv.put(cm, it, HOT, decal=True)
        # glow bleeding on the plate edges along the glowing seams
        if p.glow > 0.45:
            rim = (gap >= width) & (gap < width + 0.9) & (pg > thr + 0.12)
            rm = np.zeros(mask.shape, bool)
            rm[mask] = rim
            cv.put(rm, 0.1 + 0.12 * min(1.2, p.glow), HOT, decal=True)
        return cm

    # ------------------------------------------------------------ wings
    def _wing(self, cv, p, far):
        T = cv.T
        S = SHOULDER_FAR if far else SHOULDER
        if far:
            ka, kt = p.wing_far if p.wing_far is not None else (p.wing[0] - 0.03, p.wing[1] - 0.03)
        else:
            ka, kt = p.wing
        sc = 0.9 if far else 1.0
        rf = 0.2 if far else 0.0
        fo = min(1.0, max(0.0, p.fold))

        def lerp(a, b, t):
            return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

        E = lerp(WING_OPEN[0], WING_FOLD[0], fo)
        Wr = lerp(WING_OPEN[1], WING_FOLD[1], fo)
        tips = [lerp(a, b, fo) for a, b in zip(WING_OPEN[2], WING_FOLD[2])]
        torn = p.broken and not far
        if torn:  # a broken finger
            tips[3] = lerp(Wr, tips[3], 0.74)

        def L(q, k):
            x, y = q[0] * p.spread, q[1] * p.spread
            if rf:
                c, s = math.cos(rf), math.sin(rf)
                x, y = x * c - y * s, x * s + y * c
            return T((S[0] + x * sc * (0.84 + 0.16 * abs(k)), S[1] + y * sc * k))

        root = T((WING_ROOT[0] + (8 if far else 0), WING_ROOT[1] - (2 if far else 0)))
        sh = T(S)
        mem = MEMB_FAR if far else MEMB
        bone = OBS_FAR if far else OBS
        # membrane: scalloped between the fingers, back to the flank
        pts = [sh, L(E, ka), L(Wr, ka)]
        for j, t in enumerate(tips):
            pts.append(L(t, kt))
            if j < 3:
                m = lerp(t, tips[j + 1], 0.5)
                pts.append(L(lerp(m, Wr, 0.16), kt))
        t4 = pts[-1]
        mid = ((t4[0] + root[0]) / 2, (t4[1] + root[1]) / 2)
        pts.append((mid[0] + (sh[0] - mid[0]) * 0.22, mid[1] + (sh[1] - mid[1]) * 0.22))
        pts.append(root)
        mask = cv.poly_mask(pts)
        holes = np.zeros(mask.shape, bool)
        if torn:
            m23 = lerp(tips[1], tips[2], 0.5)
            out = (m23[0] - Wr[0], m23[1] - Wr[1])
            notch = [lerp(tips[1], tips[2], 0.18), lerp(Wr, m23, 0.36), lerp(tips[1], tips[2], 0.4),
                     lerp(Wr, m23, 0.5), lerp(tips[1], tips[2], 0.74),
                     (m23[0] + out[0] * 0.3, m23[1] + out[1] * 0.3)]
            holes |= cv.poly_mask([L(q, kt) for q in notch])
            for (cx, cy), rr, n in (((Wr[0] * 0.4 + tips[0][0] * 0.3 + tips[1][0] * 0.3,
                                      Wr[1] * 0.4 + tips[0][1] * 0.3 + tips[1][1] * 0.3), 7.0, 7),
                                     ((tips[2][0] * 0.4 + tips[3][0] * 0.25 + (WING_ROOT[0] - S[0]) * 0.35,
                                       tips[2][1] * 0.4 + tips[3][1] * 0.25 + (WING_ROOT[1] - S[1]) * 0.35), 8.0, 8)):
                hp = [(cx + math.cos(TAU * i / n + 0.4) * rr * (0.75 + 0.45 * ((i * 7) % 3) / 2),
                       cy + math.sin(TAU * i / n + 0.4) * rr * (0.75 + 0.45 * ((i * 5) % 3) / 2)) for i in range(n)]
                holes |= cv.poly_mask([L(q, kt) for q in hp])
        memb = mask & ~holes
        xs = [q[0] for q in pts]
        ys = [q[1] for q in pts]
        gx = (cv.px - min(xs)) / max(1.0, max(xs) - min(xs))
        gy = (cv.py - min(ys)) / max(1.0, max(ys) - min(ys))
        base = 0.46 + 0.1 * kt
        inten = base + 0.25 - 0.3 * (0.35 * gx + 0.65 * gy)
        cv.part()
        cv.put(memb, inten, mem)
        if torn:  # burning rims around the tears
            rim = _dilate(holes) & memb
            cv.part(line=False)
            cv.put(rim, 0.3 + 0.35 * min(1.0, p.glow), HOT, decal=True)
        # faint veins in the membrane (embers in the skin)
        cv.part(line=False)
        for j in range(3):
            a = L(lerp(Wr, tips[j], 0.45), (ka + kt) / 2)
            m = lerp(tips[j], tips[j + 1], 0.5)
            b = L(lerp(lerp(m, Wr, 0.16), Wr, 0.25), kt)
            cv.line(a, b, HOT, inten=0.16 + 0.12 * min(1.0, p.glow) * (0.8 if far else 1.0), decal=True)
        # bones: arm and fingers
        cv.part()
        cv.chain([sh, L(E, ka), L(Wr, ka)], [5.5 * sc, 4.2 * sc, 3.2 * sc], bone)
        for j, t in enumerate(tips):
            k_mid = (ka + kt) / 2
            cv.chain([L(Wr, ka), L(lerp(Wr, t, 0.5), k_mid), L(t, kt)], [2.3 * sc, 1.6 * sc, 0.8 * sc], bone)
        # thumb claw
        cv.part()
        w0 = L(Wr, ka)
        w1 = L((Wr[0] + 7, Wr[1] - 3), ka)
        cv.capsule(w0, w1, 1.9 * sc, 0.5 * sc, CLAW)
        # magma seams along the bones
        cv.part(line=False)
        si = (0.36 + 0.42 * min(1.2, p.glow)) * (0.85 if far else 1.0)
        cv.line(L(lerp((0, 0), E, 0.2), ka), L(E, ka), HOT, inten=si, decal=True)
        cv.line(L(E, ka), L(lerp(E, Wr, 0.85), ka), HOT, inten=si, decal=True)
        for j, t in enumerate(tips):
            if j % 2 == 0:
                cv.line(L(lerp(Wr, t, 0.14), ka), L(lerp(Wr, t, 0.42), (ka + kt) / 2), HOT, inten=si * 0.8, decal=True)
        if torn:  # the broken finger end glows
            cv.disc(L(tips[3], kt), 1.2, HOT, flat=0.7)

    # ------------------------------------------------------------ legs
    def _fore(self, cv, p, far):
        T = cv.T
        reach, open_, lift = p.fore
        if far:
            reach, open_, lift = reach * 0.4, open_ * 0.5, lift * 0.5
        ox, oy = (7.0, -3.0) if far else (0.0, 0.0)
        m = OBS_FAR if far else OBS

        def lerp(a, b, t):
            return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

        S = (152 + ox, 118 + oy)
        E = lerp((148 + ox, 134 + oy), (170 + ox, 123 + oy), reach)
        E = (E[0], E[1] - 5 * lift)
        Wr = lerp((159 + ox, 142 + oy), (190 + ox, 120 + oy), reach)
        Wr = (Wr[0] + 2 * lift, Wr[1] - 12 * lift)
        cv.part()
        cv.chain([T(S), T(E)], [8.0, 5.2], m)
        cv.part()
        cv.chain([T(E), T(Wr)], [5.0, 3.5], m)
        d = math.atan2(Wr[1] - E[1], Wr[0] - E[0])
        curl = 1.0 - open_
        cv.part()
        cv.ellipse(T(Wr), 3.6, 3.0, m, ang=d + p.tilt)
        for f, da in enumerate((-0.5, 0.0, 0.5)):
            a0 = d + da * (0.5 + 0.7 * open_)
            j1 = (Wr[0] + math.cos(a0) * 4.5, Wr[1] + math.sin(a0) * 4.5)
            a1 = a0 + curl * 0.9
            j2 = (j1[0] + math.cos(a1) * 3.6, j1[1] + math.sin(a1) * 3.6)
            a2 = a1 + curl * 0.7 + 0.25
            j3 = (j2[0] + math.cos(a2) * 4.0, j2[1] + math.sin(a2) * 4.0)
            cv.part()
            cv.chain([T(Wr), T(j1), T(j2)], [2.0, 1.7, 1.4], m)
            cv.part()
            cv.capsule(T(j2), T(j3), 1.4, 0.35, CLAW)
        if not far:
            cv.part(line=False)
            cv.line(T(lerp(S, E, 0.2)), T(lerp(S, E, 0.85)), HOT, inten=0.3 + 0.35 * min(1.2, p.glow), decal=True)
            cv.line(T(E), T(lerp(E, Wr, 0.7)), HOT, inten=0.26 + 0.3 * min(1.2, p.glow), decal=True)

    def _hind(self, cv, p, far):
        T = cv.T
        tuck = p.hind
        ox, oy = (8.0, -3.0) if far else (0.0, 0.0)
        m = OBS_FAR if far else OBS
        K = (113 + ox - 3 * tuck, 137 + oy - 4 * tuck)
        A = (101 + ox - 9 * tuck, 149 + oy - 5 * tuck)
        cv.part()
        cv.ellipse(T((106 + ox, 122 + oy)), 10.5, 14.5, m, ang=-0.6 + p.tilt, bias=-0.08)
        cv.part()
        cv.chain([T(K), T(A)], [5.8, 3.6], m)
        d = math.atan2(A[1] - K[1], A[0] - K[0])
        cv.part()
        cv.ellipse(T(A), 3.4, 2.8, m, ang=d + p.tilt)
        for f, da in enumerate((-0.55, 0.0, 0.5)):
            a0 = d + 0.35 + da
            j1 = (A[0] + math.cos(a0) * 5.0, A[1] + math.sin(a0) * 5.0)
            a1 = a0 - 0.5
            j2 = (j1[0] + math.cos(a1) * 3.8, j1[1] + math.sin(a1) * 3.8)
            cv.part()
            cv.chain([T(A), T(j1)], [2.1, 1.6], m)
            cv.part()
            cv.capsule(T(j1), T(j2), 1.4, 0.35, CLAW)
        if not far:
            cv.part(line=False)
            cv.line(T((100, 116)), T((109, 132)), HOT, inten=0.28 + 0.34 * min(1.2, p.glow), decal=True)
            cv.line(T(K), T(((K[0] + A[0]) / 2, (K[1] + A[1]) / 2)), HOT, inten=0.25 + 0.3 * min(1.2, p.glow),
                    decal=True)

    # ------------------------------------------------------------ neck, tail, torso
    def neck_pts(self, p):
        HL, _ = self.head_fns(p)
        n0 = NECK_BASE
        ne = HL(-6, 1)
        ln = math.hypot(ne[0] - n0[0], ne[1] - n0[1])
        p1 = (n0[0] + 0.5 * 0.45 * ln, n0[1] - 0.86 * 0.45 * ln)
        hd = (math.cos(p.hang), math.sin(p.hang))
        p2 = (ne[0] - hd[0] * 0.38 * ln, ne[1] - hd[1] * 0.38 * ln)
        return bezier(n0, p1, p2, ne, 10)

    def _chain_body(self, cv, p, pts, radii, dorsal, spike_len, spike_back, salt, step, throat=0.0):
        """A plated chain (neck or tail): dorsal spikes, the chain, faceted plates, glowing seams."""
        T = cv.T
        P = np.array([T(q) for q in pts], np.float32)
        R = np.array(radii, np.float32)
        seg = np.hypot(*(P[1:] - P[:-1]).T)
        cum = np.concatenate([[0.0], np.cumsum(seg)])
        total = float(cum[-1])
        ns = max(16, int(total))
        s = np.linspace(0, total, ns)
        cx, cy, cr = np.interp(s, cum, P[:, 0]), np.interp(s, cum, P[:, 1]), np.interp(s, cum, R)
        tx, ty = np.gradient(cx), np.gradient(cy)
        tl = np.hypot(tx, ty) + 1e-6
        tx, ty = tx / tl, ty / tl
        # dorsal spikes (behind the chain)
        cv.part()
        for k, sk in enumerate(np.arange(5.0, total - 4.0, 9.0)):
            j = min(ns - 1, int(sk / total * (ns - 1)))
            nx, ny = (-ty[j], tx[j]) if dorsal > 0 else (ty[j], -tx[j])
            bx, by = cx[j] + nx * cr[j] * 0.9, cy[j] + ny * cr[j] * 0.9
            ax, ay = nx + tx[j] * spike_back, ny + ty[j] * spike_back
            a = math.atan2(ay, ax)
            ln = spike_len * (1.0 - 0.5 * sk / total) * (1.0 if k % 2 == 0 else 0.75)
            self.spike(cv, lambda q: q, (bx, by), a, ln, max(1.6, cr[j] * 0.3), OBS)
        cv.part()
        cv.chain([tuple(q) for q in P], list(R), OBS)
        mask = cv.pid == cv.cur
        ys, xs = np.nonzero(mask)
        px, py = xs + 0.5, ys + 0.5
        d2 = (px[:, None] - cx[None, :]) ** 2 + (py[:, None] - cy[None, :]) ** 2
        j = np.argmin(d2, axis=1)
        v = (px - cx[j]) * (-ty[j]) + (py - cy[j]) * tx[j]
        vn = v / np.maximum(cr[j], 1.0) * (-dorsal)  # > 0: ventral side
        u = s[j]
        seeds = _chain_seeds(total, step, salt)
        extra = None
        if throat > 0.02:
            extra = throat * 0.6 * np.clip(vn, 0, 1) * (1.0 - 0.5 * u / total)
        self._plated(cv, p, mask, u, 9.0 * vn, seeds, 1.5 * p.seam + (0.5 if p.broken else 0.0), extra, salt,
                     light=(-0.25, -0.97))
        # the belly: a band of transverse scutes
        band = vn > 0.62
        bi = cv.inten[mask]
        bi = np.where(band, bi + 0.08, bi)
        bi = np.where(band & (np.mod(u + 0.5, 4.5) < 1.0), bi - 0.3, bi)
        cv.inten[mask] = np.clip(bi, 0, 1)
        if throat > 0.02:  # the throat glows through the ventral scutes
            tm = np.zeros(mask.shape, bool)
            tm[mask] = band
            ti = np.zeros(mask.shape, np.float32)
            ti[mask] = 0.14 + 0.55 * throat * (1.0 - 0.6 * u / total) - np.where(np.mod(u + 0.5, 4.5) < 1.0, 0.15, 0)
            cv.part(line=False)
            cv.put(tm & (cv.mat == cv._mid(OBS)), ti, HOT, decal=True)
        return P, total

    def _neck(self, cv, p):
        pts = self.neck_pts(p)
        n = len(pts)
        radii = [15.0 - 6.0 * i / (n - 1) for i in range(n)]
        self._chain_body(cv, p, pts, radii, dorsal=-1, spike_len=13.0, spike_back=-0.45, salt=11, step=10.0,
                         throat=p.chest)

    def tail_pts(self, p):
        sw = p.sway
        base = bezier(TAIL_BASE, (74, 124), (44, 138), (29, 111 - 6 * (p.tail - 1)), 15)
        out = []
        n = len(base)
        for i, (x, y) in enumerate(base):
            s = i / (n - 1)
            if 0 < i < n - 1:
                a, b = base[i - 1], base[i + 1]
            elif i == 0:
                a, b = base[0], base[1]
            else:
                a, b = base[-2], base[-1]
            tx, ty = b[0] - a[0], b[1] - a[1]
            tl = math.hypot(tx, ty) or 1.0
            nx, ny = -ty / tl, tx / tl
            amp = 7.0 * s * math.sin(sw - 2.6 * s)
            out.append((x + nx * amp, y + ny * amp))
        return out

    def _tail(self, cv, p):
        pts = self.tail_pts(p)
        n = len(pts)
        radii = [13.0 - 9.5 * (i / (n - 1)) ** 0.85 for i in range(n)]
        P, total = self._chain_body(cv, p, pts, radii, dorsal=1, spike_len=10.0, spike_back=0.7, salt=23, step=10.0)
        # the spiked mace at the tip
        T = cv.T
        tip, prev = pts[-1], pts[-3]
        d = math.atan2(tip[1] - prev[1], tip[0] - prev[0])
        cv.part()
        blades = [(d, 18.0, 5.2), (d - 0.9, 12.0, 4.0), (d + 0.9, 12.0, 4.0), (d - 1.9, 8.0, 3.2),
                  (d + 1.9, 8.0, 3.2)]
        tips = []
        for (a, ln, hw) in blades[3:]:
            tips.append(self.spike(cv, T, tip, a, ln, hw, OBS))
        cv.part()
        cv.disc(T(tip), 5.6, OBS)
        cv.part()
        for (a, ln, hw) in blades[:3]:
            tips.append(self.spike(cv, T, (tip[0] + math.cos(a) * 2, tip[1] + math.sin(a) * 2), a, ln, hw, OBS))
        cv.part(line=False)
        si = 0.34 + 0.45 * min(1.2, p.glow) + (0.1 if p.broken else 0.0)
        for (a, ln, hw) in blades[:3]:
            cv.line(T((tip[0] + math.cos(a) * 1, tip[1] + math.sin(a) * 1)),
                    T((tip[0] + math.cos(a) * ln * 0.7, tip[1] + math.sin(a) * ln * 0.7)), HOT, inten=si, decal=True)
        cv.px_set(*T(tip), HOT, inten=min(1.0, si + 0.2), decal=True)

    def _torso(self, cv, p):
        T = cv.T
        cv.part()
        for (x, y, a, ln) in BACK_SPIKES:
            base = (x, y + 3)
            tip = self.spike(cv, T, base, a, ln, 3.2, OBS)
            if ln >= 9:
                cv.part(line=False)
                cv.line(T(base), T(((base[0] * 0.4 + tip[0] * 0.6), (base[1] * 0.4 + tip[1] * 0.6))), HOT,
                        inten=0.3 + 0.3 * min(1.2, p.glow), decal=True)
                cv.part()
        puff = 1.0 + 0.07 * p.chest
        cv.part()
        cv.ellipse(T((101, 112)), 20, 17, OBS, ang=0.25 + p.tilt)
        cv.ellipse(T((127, 111)), 36, 21, OBS, ang=-0.05 + p.tilt)
        cv.ellipse(T((154, 106)), 21 * puff, 20 * puff, OBS, ang=-0.4 + p.tilt)
        mask = cv.pid == cv.cur
        lx, ly = self.local(cv)
        u, w = lx[mask], ly[mask]
        dc = np.hypot(u - HEART[0], w - HEART[1])
        extra = p.chest * 0.6 * np.exp(-dc / 15.0)
        seeds = TORSO_SEEDS[1 if p.broken else 0]
        width = (2.2 if p.broken else 1.6) * p.seam
        self._plated(cv, p, mask, u, w, seeds, width, extra, salt=3)
        if p.chest > 0.05:  # the chest glows through its scales
            dh = np.hypot(lx - HEART[0], ly - HEART[1])
            gm = mask & (dh < 4 + 15 * p.chest) & (cv.mat == cv._mid(OBS))
            gi = np.clip(0.1 + 0.36 * p.chest * (1.0 - dh / (4 + 15 * p.chest)), 0, 1)
            cv.part(line=False)
            cv.put(gm, gi, HOT, decal=True)
        if p.broken:
            self._heart(cv, p, mask, lx, ly)

    def _heart(self, cv, p, mask, lx, ly):
        """Form II: the chest plates are gone, the magma heart shows (pulsing)."""
        hx, hy = HEART
        a = np.arctan2(ly - hy, lx - hx)
        q = ((lx - hx) / 12.0) ** 2 + ((ly - hy) / 13.0) ** 2
        hole = mask & (q <= 1.0 + 0.22 * np.sin(7 * a + 1.3) + 0.1 * np.sin(13 * a))
        d = np.sqrt(q)
        pulse = 0.07 * math.sin(p.t * 2.0)
        inten = np.clip(1.02 - 0.7 * d + pulse + 0.15 * (p.glow - 0.6), 0, 1)
        cv.part(line=False)
        cv.put(hole, inten, CORE, decal=True)
        # dark veins of crust across the heart
        for k in range(3):
            ang = 0.7 + k * 2.1
            ln = (hole & (np.abs((lx - hx) * math.sin(ang) - (ly - hy) * math.cos(ang)) < 0.6)
                  & (q > 0.25) & (q < 0.8) & ((lx - hx) * math.cos(ang) + (ly - hy) * math.sin(ang) > 0))
            cv.put(ln, 0.5, HOT, decal=True)
        rim = _dilate(hole) & mask & ~hole
        cv.put(rim, 0.8, HOT, decal=True)
        lifted = _dilate(hole, 2) & mask & ~_dilate(hole)
        cv.put(lifted, 0.96, OBS, decal=True)  # the torn scale edges catch the light

    # ------------------------------------------------------------ head
    def _head(self, cv, p):
        T = cv.T
        HL0, JL0 = self.head_fns(p)

        def HL(x, y):
            return T(HL0(x, y))

        def JL(x, y):
            return T(JL0(x, y))

        lit = p.glow > 0.03
        ha = p.hang + p.tilt
        gl = min(1.2, p.glow)
        # far horn + far frill
        cv.part()
        cv.chain([HL(*q) for q in FAR_HORN], [r * HS for r in (3.2, 2.6, 1.6, 0.5)], OBS_FAR)
        cv.part()
        self.spike(cv, lambda q: q, HL(-7, -5), ha + PI - 0.35, 9 * HS, 2.2 * HS, OBS_FAR)
        # mouth interior, teeth
        if p.jaw > 0.05:
            cv.part(line=False)
            cv.poly([HL(-1, 2), HL(37, 1.5), JL(32, 4.5), JL(-1, 5)], MAW, flat=0.3)
            if lit:
                g = min(1.2, 0.35 + 0.65 * max(p.glow, p.chest))
                cv.poly([HL(-1, 2.4), HL(9 + 12 * g, 2.2), JL(8 + 11 * g, 4.4), JL(-1, 4.8)], HOT, flat=0.45 + 0.3 * g)
                cv.poly([HL(-1, 2.8), HL(4 + 7 * g, 2.6), JL(3 + 6 * g, 4.2), JL(-1, 4.4)], CORE, flat=0.62 + 0.3 * g)
            cv.part()
            for xx in (12, 18, 24, 30):
                cv.poly([HL(xx - 1.2, 2.4), HL(xx + 1.2, 2.4), HL(xx + 0.2, 2.4 + 2.8)], TOOTH, flat=0.85)
            for xx in (10, 16, 22, 28):
                cv.poly([JL(xx - 1.2, 3.8), JL(xx + 1.2, 3.8), JL(xx - 0.2, 3.8 - 2.6)], TOOTH, flat=0.7)
        # lower jaw (+ chin spike)
        cv.part()
        self.spike(cv, lambda q: q, JL(2, 8.5), ha + 0.6 * p.jaw + 2.2, 6 * HS, 1.8 * HS, OBS)
        cv.part()
        cv.poly([JL(*q) for q in LOWER], OBS, bevel=1.4, base=0.48)
        if p.jaw <= 0.05:  # a fang over the lower lip
            cv.part()
            cv.poly([HL(28.8, 2.6), HL(31.2, 2.6), HL(30.2, 6.4)], TOOTH, flat=0.85)
        # skull
        cv.part()
        cv.poly([HL(*q) for q in UPPER], OBS, bevel=1.8, base=0.6)
        # nose horn
        cv.part()
        self.spike(cv, lambda q: q, HL(29, -6), ha - 1.2, 5.5 * HS, 1.7 * HS, OBS)
        # brow ridge over a deep socket, the burning eye
        cv.part(line=False)
        cv.poly([HL(9, -9), HL(21, -8.5), HL(20, -4.6), HL(10, -5.2)], MAW, flat=0.15)
        cv.part()
        cv.poly([HL(*q) for q in BROW], OBS, bevel=1.0, base=0.72)
        cv.part(line=False)
        if lit:
            e = min(1.3, p.eye + (0.25 if p.broken else 0.0))
            cv.capsule(HL(12.5, -6.6), HL(19, -5.6), (1.25 + 0.3 * e) * HS, (0.9 + 0.2 * e) * HS, CORE,
                       flat=0.58 + 0.3 * e)
            cv.px_set(*HL(18.4, -5.9), CORE, inten=1.0)
        else:
            cv.line(HL(12.5, -6.4), HL(19, -5.6), MAW, inten=0.4)
        # magma seams of the head
        si = 0.32 + 0.42 * gl + (0.1 if p.broken else 0.0)
        for a, b, k in (((-6, -2), (3, -6.5), 1.0), ((3, -6.5), (10, -10.5), 0.8), ((-5, 2), (5, 0.5), 0.9),
                        ((22, -9), (30, -5.5), 0.7), ((2, -11.5), (12, -11.8), 0.6)):
            cv.line(HL(*a), HL(*b), HOT, inten=si * k, decal=True)
        if p.jaw <= 0.05:  # the closed mouth glows along the lips
            cv.line(HL(2, 3.2), HL(33, 2.4), HOT, inten=0.3 + 0.3 * gl, decal=True)
        else:
            cv.line(JL(4, 6.5), JL(24, 6.0), HOT, inten=si * 0.8, decal=True)
        cv.px_set(*HL(35.5, -1.8), HOT, inten=0.55 + 0.35 * gl, decal=True)  # nostril
        # near horn (swept back) and frill
        cv.part()
        cv.chain([HL(*q) for q in NEAR_HORN], [r * HS for r in (4.2, 3.2, 2.0, 0.5)], OBS)
        cv.part(line=False)
        cv.line(HL(1, -11), HL(-9, -17.5), HOT, inten=si * 0.85, decal=True)
        cv.part()
        self.spike(cv, lambda q: q, HL(-8, 0.5), ha + PI - 0.15, 9 * HS, 2.4 * HS, OBS)
        self.spike(cv, lambda q: q, HL(-6, 5), ha + PI - 0.55, 7 * HS, 2.0 * HS, OBS)

    def _flame(self, cv, p):
        """A short flame at the open mouth (breath; the beam itself is drawn by the game)."""
        mx, my = self.mouth_body(p)
        d = p.hang + 0.3 * p.jaw
        c, s = math.cos(d), math.sin(d)
        T = cv.T
        f = p.flame
        x0, y0 = mx - c * 7, my - s * 7  # the fire comes out from between the jaws

        def at(ln, off):
            return T((x0 + c * ln - s * off, y0 + s * ln + c * off))

        for layer, (m, wf, lf, it) in enumerate(((HOT, 1.0, 1.0, 0.46), (HOT, 0.68, 0.84, 0.74),
                                                  (CORE, 0.38, 0.62, 0.92))):
            top, bot = [], []
            n = 8
            L = 27.0 * f * lf
            for i in range(n + 1):
                t = i / n
                wd = (1.5 + 8.0 * math.sin(min(1.0, t * 1.25) * PI / 2)) * wf * (1.0 - 0.55 * t ** 2)
                lick = (1.4 if i % 2 else -0.6) * wf * (0.6 + 0.4 * math.sin(p.t * 2.3 + i * 1.9 + layer))
                top.append(at(L * t, -(wd + lick)))
                bot.append(at(L * t, wd + (lick if (i + layer) % 3 else -lick)))
            # the end splits into three tongues
            tongues = [at(L * 1.12, -wd * 0.7), at(L * (1.0 + 0.1 * math.sin(p.t * 1.7 + layer)), 0.0),
                       at(L * 1.08, wd * 0.7)]
            end = [tongues[0], at(L * 0.96, -wd * 0.25), tongues[1], at(L * 0.96, wd * 0.25), tongues[2]]
            cv.part(line=False)
            cv.poly(top + end + bot[::-1], m, flat=it)

    # ------------------------------------------------------------ death helpers
    def _petrify(self, cv, p):
        """The magma cools: everything behind the cooling front becomes grey stone."""
        lx, ly = self.local(cv)
        front = -10.0 + 280.0 * p.stone
        sel = lx < front + 3.0 * np.sin(ly * 0.31)
        for i, m in enumerate(list(cv.mats)):
            tgt = STONE_MAP.get(m.name)
            if tgt is None:
                continue
            s = sel & (cv.mat == i)
            if s.any():
                cv.mat[s] = cv._mid(tgt)
        # the front itself still smoulders
        edge = sel & ~(lx < front - 2.0 + 3.0 * np.sin(ly * 0.31)) & (cv.mat == cv._mid(ASH))
        if p.stone < 1.0:
            cv.put(edge, 0.3, HOT, decal=True)

    def rubble(self, t):
        """The heap of cold stone the dragon leaves (t: 0 scattered .. 1 settled)."""
        cv = TCanvas(W, H)
        rocks = [(128, 179, 16, 6.5), (108, 181, 9, 5), (150, 181, 11, 5.4), (118, 174, 8, 4.6), (140, 174, 9, 5),
                 (90, 183, 6, 3.4), (168, 183, 7, 3.6), (130, 169, 6, 3.6), (78, 185, 4, 2.4), (180, 185, 4, 2.4)]
        for i, (x, y, rx, ry) in enumerate(rocks):
            cv.part()
            m = (STONE, STONE_FAR, STONE)[i % 3]
            s = max(0.3, t)  # the heap grows in place, its base on the ground line
            cv.ellipse((x, y + ry * (1 - s) - 0.5), rx * (0.4 + 0.6 * t), ry * s, m, ang=0.15 * (i % 3 - 1))
        # a broken horn on top of the heap
        if t > 0.6:
            cv.part()
            dy = (1.0 - t) * 12
            cv.chain([(124, 167 + dy), (132, 163 + dy), (142, 162 + dy)], [2.6, 1.8, 0.5], STONE)
        return cv.render()

    # ------------------------------------------------------------- effects
    def sparkle(self, f, x, y, size, c0=None, c1=None):
        FX.sparkle(f, int(x), int(y), size, c0 or WHITE_C, c1 or PC[1])

    def charge(self, f, x, y, i, n, seed, r0=26, r1=4, count=10):
        """Motes converging onto (x, y)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = rng.random() * TAU
            d = r0 * (1 - t) + r1 + rng.random() * 6
            put_if_empty(f, x + math.cos(a) * d, y + math.sin(a) * d, [WHITE_C, PC[0], PC[1], PC[2]][k % 4])

    def inhale(self, f, x, y, i, n, seed, count=16):
        """Air and sparks drawn into the mouth from the front (streaks)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = -1.1 + 2.2 * rng.random()
            d = 14 + (1 - ((rng.random() + t * 1.7) % 1.0)) * 34
            x0, y0 = x + math.cos(a) * d, y + math.sin(a) * d
            x1, y1 = x + math.cos(a) * (d - 5), y + math.sin(a) * (d - 5)
            if max(x0, x1) < W - 3 and min(y0, y1) > 2:
                streak(f, (x0, y0), (x1, y1), [PC[1], PC[2], PC[3]][k % 3])

    def embers(self, f, i, n, seed, count=10, p=None):
        """Sparks rising from the seams (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        ox, oy = (p.dx, p.dy) if p is not None else (0.0, 0.0)
        for k in range(count):
            x0 = 84 + rng.random() * 90 + ox
            off = rng.random()
            t = (i / n + off) % 1.0
            y = 112 + oy - t * 60
            put_if_empty(f, x0 + math.sin(t * 6 + k) * 2.5, y, [PC[1], PC[2], PC[3]][min(2, int(t * 3))])

    def smoke(self, f, x, y, i, n, seed, count=4):
        rng = np.random.default_rng(seed)
        for k in range(count):
            t = (i / n + rng.random()) % 1.0
            put_if_empty(f, x - 2 - t * 10 + rng.random() * 2, y - t * 12, C("#5a5560") if t < 0.5 else C("#3e3a44"))

    # ================================================================= clips
    def idle(self, broken=False):
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            p = DPose(t=ph, broken=broken, dy=-2.5 * math.sin(ph), sway=ph, **wings(ph),
                      head=(198 + 1.5 * math.sin(ph - 0.8), 62 + 2.0 * math.sin(ph - 1.3)),
                      hang=0.06 + 0.05 * math.sin(ph - 1.0), glow=0.6 + 0.1 * math.sin(ph), eye=0.3,
                      hind=0.2 + 0.12 * math.sin(ph + 1.0), fore=(0.0, 0.2, 0.15 + 0.1 * math.sin(ph + 0.5)))
            f = self.draw(p)
            self.embers(f, i, n, self.seed + 1, p=p)
            out.append(f)
        return out

    def walk(self, broken=False):
        """Glide (= move): wings held high, a slow beat, body stretched, the tail streaming."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            k = 0.62 + 0.3 * math.cos(ph)
            kt = 0.62 + 0.3 * math.cos(ph - 0.7)
            p = DPose(t=ph * 1.5, broken=broken, tilt=0.04, dy=-1.5 * math.sin(ph), sway=ph * 1.0 + 1.0, tail=0.6,
                      wing=(k, kt), wing_far=(k - 0.05, kt - 0.05), spread=1.03,
                      head=(205 + 1.0 * math.sin(ph), 68 + 1.5 * math.sin(ph - 1.0)), hang=0.12, glow=0.65,
                      eye=0.4, hind=0.75, fore=(0.0, 0.1, 0.5))
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for j in range(7):
                y = int(rng.integers(40, 160))
                x0 = int(rng.integers(2, 30))
                streak(f, (x0, y), (x0 + int(rng.integers(6, 16)), y), PC[2 + j % 3])
            self.embers(f, i, n, self.seed + 2, count=6, p=p)
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """The head rears back and up, fire gathers in the jaws, the eye flares."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            ph = 0.6 * i
            p = DPose(t=ph, broken=broken, tilt=-0.08 * e, dy=-2 * e, sway=ph, **held(0.7 + 0.3 * e, 0.6 + 0.4 * e),
                      head=(198 - 16 * e, 62 - 16 * e), hang=0.06 - 0.6 * e, jaw=0.4 * e, glow=0.6 + 0.4 * t,
                      eye=0.3 + 0.9 * t, hind=0.3, fore=(0.0, 0.2 + 0.5 * t, 0.2 + 0.3 * t))
            f = self.draw(p)
            x, y = self.mouth(p)
            self.charge(f, x, y, i, 6, self.seed + 20 + i, r0=28, r1=5, count=6 + 2 * i)
            if i >= 4:
                self.sparkle(f, x + 1, y, 3 + (i - 4) * 2)
            out.append(f)
        return out

    def attack_poses(self, broken=False):
        keys = [  # head x, y, hang, jaw, fore reach, open, lift, dx, glow
            (201, 69, 0.16, 0.65, 0.2, 0.6, 0.8, 1, 0.85),
            (204, 73, 0.18, 1.0, 1.0, 1.0, 0.1, 2, 1.0),
            (203, 73, 0.18, 0.7, 0.85, 0.8, 0.0, 2, 0.85),
            (200, 67, 0.12, 0.3, 0.4, 0.5, 0.1, 1, 0.75),
            (198, 63, 0.07, 0.1, 0.1, 0.3, 0.15, 0, 0.65)]
        out = []
        for i, (hx, hy, hang, jaw, rc, op, lf, dx, g) in enumerate(keys):
            out.append(DPose(t=0.5 * i + 3, broken=broken, dx=dx, sway=0.4 * i, **held(0.95 - 0.12 * i, 0.85 - 0.1 * i),
                             head=(hx, hy), hang=hang, jaw=jaw, glow=g, eye=0.4 + 0.8 * g, hind=0.4,
                             fore=(rc, op, lf)))
        return out

    def attack(self, broken=False):
        """Bite and claw swipe: the head lunges, a fireball leaves the mouth on f1 (ORIGIN)."""
        out = []
        for i, p in enumerate(self.attack_poses(broken)):
            f = self.draw(p)
            x, y = self.mouth(p)
            if i == 0:
                self.charge(f, x, y, 4, 6, self.seed + 39, r0=12, r1=3, count=8)
            if i == 1:
                FX.burst(f, x + 4, y, 7, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 40, jag=0.35, rays=6)
                self._swipe(f, p, 1.0)
            if i == 2:
                self._swipe(f, p, 0.6)
                for k in range(5):
                    a = -0.55 + 0.275 * k
                    self.sparkle(f, x + 10 + math.cos(a) * 14, y + math.sin(a) * 14, 2)
                ring(f, x + 4, y, 9, PC[1], -1.2, 1.2)
            if i == 3:
                motes(f, x + 14, y, 10, 6, 22, [WHITE_C, PC[0], PC[1]], seed=self.seed + 45)
            out.append(f)
        return out

    def _swipe(self, f, p, s):
        """Claw smear: three arcs in front of the swiping fore leg."""
        wx, wy = self.T(p, (190 + 2 * p.fore[2], 120 - 12 * p.fore[2]))
        for k in range(3):
            r = 15 + 4 * k
            ring(f, wx - 8, wy + 6, r, [PC[0], PC[1], PC[2]][k] if s > 0.8 else [PC[1], PC[2], PC[3]][k],
                 -1.4 + 0.2 * k, 0.9 - 0.1 * k, dotted=0 if s > 0.8 else 2)

    def telegraph_special(self, broken=False):
        """It inhales: the chest swells and lights up, then the throat; air is drawn into the mouth."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            ph = 0.7 * i
            p = DPose(t=ph, broken=broken, tilt=-0.06 * e, dy=-2 * e, sway=ph, **held(0.75 + 0.25 * e, 0.7 + 0.3 * e),
                      spread=1.0, head=(198 - 10 * e, 62 - 10 * e), hang=0.06 - 0.35 * e, jaw=0.15 + 0.3 * e,
                      glow=0.6 + 0.4 * t, chest=t, eye=0.3 + 0.8 * t, hind=0.35, fore=(0.0, 0.3, 0.3))
            f = self.draw(p)
            x, y = self.mouth(p)
            self.inhale(f, x, y, i, 6, self.seed + 50 + i, count=10 + 2 * i)
            if i == 5:
                self.sparkle(f, x + 2, y, 5)
            out.append(f)
        return out

    # 2.76.2: the dive with its wings folded (the player's rule: its whole body hurts on contact, so the dive is as
    # wide as its drawing; wings folded against its flanks, nose down along its line, it fits the lane the game shows).
    DIVE = dict(tilt=1.25, head=(182.0, 98.0), hang=0.2, dy=-16.0, tail=-5.0)

    def dive_pose(self, i, broken=False):
        d = self.DIVE
        return DPose(t=2.0 + 0.9 * i, broken=broken, tilt=d["tilt"], dy=d["dy"], sway=2.0 + 0.9 * i, **held(0.05, 0.0), fold=1.0,
                     head=d["head"], hang=d["hang"], jaw=0.45 + 0.1 * (i % 2), glow=0.95, eye=1.1, hind=0.85, fore=(0.0, 0.2, 0.7),
                     tail=d["tail"])

    def telegraph_dive(self, broken=False):
        """2.76.2: the wind-up of the dive: it folds its wings against its flanks and tips its nose down."""
        d = self.DIVE
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            p = DPose(t=0.7 * i, broken=broken, tilt=-0.06 + (d["tilt"] + 0.06) * e, dy=d["dy"] * e, sway=0.7 * i + 2.0 * e,
                      **held(0.8 - 0.75 * e, 0.75 - 0.75 * e), fold=e, head=(198.0 + (d["head"][0] - 198.0) * e, 62.0 + (d["head"][1] - 62.0) * e),
                      hang=0.06 + (d["hang"] - 0.06) * e, jaw=0.15 + 0.3 * e, glow=0.6 + 0.35 * t, eye=0.3 + 0.8 * t,
                      hind=0.35 + 0.5 * e, fore=(0.0, 0.3 - 0.1 * e, 0.3 + 0.4 * e), tail=1.0 + (d["tail"] - 1.0) * e)
            f = self.draw(p)
            if i == 5:
                x, y = self.mouth(p)
                self.sparkle(f, x, y, 4)
            out.append(f)
        return out

    def dive(self, broken=False):
        """2.76.2: the dive itself, wings folded, embers torn off its seams."""
        out = []
        for i in range(4):
            p = self.dive_pose(i, broken)
            f = self.draw(p)
            self.embers(f, i, 4, self.seed + 70, count=6, p=p)
            out.append(f)
        return out

    def special_pose(self, i, broken=False):
        return DPose(t=1.6 * i, broken=broken, sway=0.8 * i, **held(0.88 + 0.06 * (i % 2), 0.84 + 0.08 * ((i + 1) % 2)),
                     head=(197.0, 67.0), hang=0.22, jaw=1.0, glow=1.0, chest=0.85 + 0.15 * (i % 2), eye=1.1, hind=0.4,
                     fore=(0.0, 0.4, 0.3), flame=0.9 + 0.1 * (i % 2))

    def special(self, broken=False):
        """Fire breath (loop): jaws wide, chest and throat ablaze, a short flame at the mouth."""
        out = []
        for i in range(4):
            p = self.special_pose(i, broken)
            f = self.draw(p)
            x, y = self.mouth(p)
            motes(f, x + 14, y, 8, 4, 12, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 60 + i, arc=(-1.2, 1.2))
            out.append(f)
        return out

    def _rift(self, f, x, y, s, seed):
        """A swirl of embers (summon: the escort comes out of it)."""
        if s <= 0.05:
            return
        FX.burst(f, x, y, 2 + 4 * s, [WHITE_C, PC[0], PC[1], PC[2]], seed=seed, jag=0.5, rays=int(4 * s))
        ring(f, x, y, 5 + 6 * s, PC[2], dotted=2)
        motes(f, x, y, int(5 + 6 * s), 4, 10 * s + 6, [PC[0], PC[1], PC[2]], seed=seed + 1)

    def summon(self, broken=False):
        """Wings spread wide, it roars to the sky; embers rise and two swirls of fire open."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.25]
        out = []
        for i, h in enumerate(keys):
            p = DPose(t=0.6 * i, broken=broken, tilt=-0.1 * h, dy=-2 * h, sway=0.5 * i,
                      **held(0.75 + 0.25 * h, 0.7 + 0.3 * h), spread=1.0 + 0.04 * h,
                      head=(190 - 4 * h, 60 - 6 * h), hang=0.06 - 1.2 * h, jaw=0.95 * h, glow=0.6 + 0.4 * h,
                      chest=0.3 * h, eye=0.3 + 0.9 * h, hind=0.3, fore=(0.0, 0.3 + 0.6 * h, 0.4 * h))
            f = self.draw(p)
            x, y = self.mouth(p)
            if h > 0.6:
                d = self.mouth_dir(p)
                for k, r in enumerate((10 + 3 * (i % 2), 18 + 3 * (i % 2))):
                    ring(f, x, y, r, [PC[1], PC[2]][k], d - 0.8, d + 0.8, dotted=0 if k == 0 else 2)
            s = [0, 0.4, 0.8, 1.0, 1.0, 0.6][i]
            self._rift(f, 22, 150, s * 1.3, self.seed + 70 + i)
            self._rift(f, 234, 150, s * 1.3, self.seed + 75 + i)
            motes(f, 128, 120, 6 + 3 * i, 30, 90, [PC[0], PC[1], PC[2]], seed=self.seed + 80 + i, arc=(-2.9, -0.2))
            out.append(f)
        return out

    def roar_pose(self, h, i, broken, **kw):
        d = dict(t=0.8 * i, broken=broken, tilt=-0.06 * h, sway=0.6 * i, **held(0.7 + 0.3 * h, 0.6 + 0.4 * h),
                 spread=1.0 + 0.03 * h, head=(198 - 2 * h, 62 - 6 * h), hang=0.06 - 0.32 * h, jaw=h,
                 glow=0.6 + 0.4 * h, chest=0.2 * h, eye=0.3 + 0.9 * h, hind=0.3, fore=(0.0, 0.3 + 0.6 * h, 0.3 * h))
        d.update(kw)
        return DPose(**d)

    def _roar_fx(self, f, p, i):
        x, y = self.mouth(p)
        d = self.mouth_dir(p)
        for k, r in enumerate((7 + 2 * (i % 2), 12 + 2 * (i % 2), 17 + 2 * (i % 2))):
            ring(f, x, y, r, [PC[1], PC[2], PC[3]][k], max(-PI, d - 0.9), min(PI, d + 0.9), dotted=0 if k < 2 else 2)
        motes(f, BX, BY + YOFF, 18, 50, 84, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 85 + i, big=0.2,
              arc=(-3.1, 0.3))

    def roar(self, broken=False):
        """Phase change: it rears, spreads its wings and roars; the seams flare."""
        keys = [(0.0, 0), (0.3, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = self.roar_pose(h, i, broken, seam=1.0 + 0.3 * h)
            f = self.draw(p)
            if h > 0.8:
                self._roar_fx(f, p, i)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def shatter(self):
        """Phase 5 roar: the seams blaze, the chest scales burst off, form II settles (Brise)."""
        out = []
        seq = [(False, 0.5, 1.4, 0.4), (False, 0.8, 1.9, 0.8), (False, 1.0, 2.5, 1.0), (True, 1.0, 1.4, 1.0),
               (True, 1.0, 1.2, 1.0), (True, 0.9, 1.0, 1.0), (True, 0.6, 1.0, 0.7), (True, 0.2, 1.0, 0.2)]
        for i, (broken, h, seam, ch) in enumerate(seq):
            p = self.roar_pose(h, i, broken, seam=seam, chest=ch, glow=0.6 + 0.5 * h)
            f = self.draw(p)
            cx, cy = self.T(p, HEART)
            if i == 2:
                f = flashed(f, 0.35)
            if i == 3:
                FX.burst(f, cx + 2, cy, 14, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 90, jag=0.4, rays=10)
            if 3 <= i <= 5:
                rng = np.random.default_rng(self.seed + 91 + i)
                for k in range(9):
                    a = -2.6 + 3.6 * rng.random()
                    dd = 22 + 14 * (i - 3) + rng.random() * 10
                    FX.chunk(f, cx + math.cos(a) * dd, cy + math.sin(a) * dd * 0.8, 3 if k % 2 else 2, C("#5f5a82"),
                             C("#29263b"))
                ring(f, cx, cy, 20 + 12 * (i - 3), PC[1 + (i - 3)], dotted=0 if i == 3 else 2)
            if h > 0.8:
                fl = getattr(f, "flash", 0.0)
                self._roar_fx(f, p, i)
                f = shake(np.asarray(f), [1, -1, 1, -1, 1, -1][min(5, i)], 0)
                if fl:
                    f = flashed(f, fl)
            out.append(f)
        return out

    def hurt(self, broken=False):
        a = DPose(t=0.3, broken=broken, dx=-4, tilt=-0.1, **held(0.55, 0.35), fold=0.25, head=(188, 56), hang=-0.45,
                  jaw=0.55, glow=0.9, eye=1.0, hind=0.5, fore=(0.0, 0.6, 0.5), seam=1.3)
        b = DPose(t=0.6, broken=broken, dx=-2, tilt=-0.04, **held(0.65, 0.5), fold=0.1, head=(194, 60), hang=-0.15,
                  jaw=0.2, glow=0.7, eye=0.6, hind=0.35, fore=(0.0, 0.3, 0.3))
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        motes(fb, BX, BY - 4, 12, 40, 70, [WHITE_C, PC[0], PC[1]], seed=self.seed + 110)
        return [fa, fb]

    def death(self, broken=False):
        out = []
        # 0-2: convulsions, the fire flares and gutters
        conv = [DPose(t=0.2, broken=broken, tilt=-0.1, **held(1.0, 0.9), head=(190, 50), hang=-0.9, jaw=0.85, glow=1.1,
                      chest=0.6, eye=1.2, seam=1.6, fore=(0.0, 0.8, 0.6), hind=0.5),
                DPose(t=0.9, broken=broken, tilt=0.08, dy=4, **held(0.2, -0.1), head=(200, 70), hang=0.35, jaw=0.3,
                      glow=0.45, eye=0.4, fore=(0.0, 0.2, 0.0), hind=0.1),
                DPose(t=1.6, broken=broken, tilt=-0.04, dy=6, **held(0.95, 0.8), head=(192, 54), hang=-0.7, jaw=0.9,
                      glow=1.0, chest=0.8, eye=1.0, seam=1.8, fore=(0.0, 0.9, 0.5), hind=0.5)]
        for i, p in enumerate(conv):
            f = self.draw(p)
            if i == 2:
                cx, cy = self.T(p, HEART)
                FX.burst(f, cx, cy, 6, [WHITE_C, PC[0], PC[1]], seed=self.seed + 118, jag=0.45, rays=5)
            f = shake(f, [1, -1, 2][i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        # 3-6: it falls, the wings flung up, the glow dying; the magma starts to cool
        fall = [(0.18, 8, 0.45, 0.0, 0.5), (0.26, 13, 0.3, 0.0, 0.7), (0.3, 16, 0.18, 0.18, 0.85),
                (0.32, 17, 0.1, 0.45, 0.95)]
        for i, (tl, dy, g, st, dr) in enumerate(fall):
            p = DPose(t=2 + i, broken=broken, tilt=tl, dy=dy, **held(1.0, 1.0 - 0.1 * i), fold=0.15 + 0.1 * i,
                      head=(198 - 4 * i, 68 + 6 * i), hang=0.3 + 0.35 * dr, jaw=0.4 - 0.1 * i, glow=g, eye=g,
                      sway=1.0 + 0.4 * i, tail=0.6, fore=(0.0, 0.1, 0.0), hind=0.0, stone=st)
            f = self.draw(p)
            if i < 2:
                motes(f, BX + 10, BY + YOFF + dy - 30, 8, 10, 50, [PC[1], PC[2], PC[3]], seed=self.seed + 120 + i,
                      arc=(-2.8, -0.4))
            out.append(f)
        # 7-8: cold stone
        last = None
        for i, st in enumerate((0.75, 1.0)):
            p = DPose(t=6 + i, broken=broken, tilt=0.32, dy=17, **held(1.0, 0.7), fold=0.5, head=(190, 86),
                      hang=0.62, jaw=0.1, glow=0.05, eye=0.0, sway=2.6, tail=0.6, fore=(0.0, 0.1, 0.0), hind=0.0,
                      stone=st)
            f = self.draw(p)
            self.smoke(f, 180, 110, i, 2, self.seed + 125 + i, count=6)
            out.append(f)
            last = f
        base = self._cracked(last)
        out[-1] = base.copy()
        # 9-12: it crumbles: the statue breaks into blocks that fall, the heap builds up
        for k, t in enumerate((0.22, 0.45, 0.68, 0.9)):
            f = self.crumble(base, t, self.seed + 141, ground=176.0)
            FX.under(f, self.rubble(min(1.0, 0.25 + t * 0.85)))
            out.append(f)
        # 13: the heap, smoking, two last embers
        fin = self.rubble(1.0)
        self.smoke(fin, 132, 165, 0, 2, self.seed + 150, count=6)
        put_if_empty(fin, 120, 165, PC[3])
        put_if_empty(fin, 147, 168, PC[4])
        out.append(fin)
        return out

    @staticmethod
    def crumble(base, t, seed, cell=9.0, ground=180.0):
        """The stone statue `base` breaks into blocks (Voronoi cells) that fall one after the other,
        the top first, and vanish into the heap at `ground` (t: 0 whole .. 1 gone)."""
        from raster import outline_rgba
        img = np.asarray(base)
        ys, xs = np.nonzero(img[:, :, 3] > 0)
        out = np.zeros_like(img)
        if len(ys) == 0:
            return out
        rng = np.random.default_rng(seed)
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        seeds = np.array([(x + rng.uniform(-3, 3), y + rng.uniform(-3, 3))
                          for y in np.arange(y0, y1 + cell, cell) for x in np.arange(x0, x1 + cell, cell)], np.float32)
        lab = np.argmin((xs[:, None] + 0.5 - seeds[None, :, 0]) ** 2 + (ys[:, None] + 0.5 - seeds[None, :, 1]) ** 2,
                        axis=1)
        delay = rng.random(len(seeds))
        drift = rng.uniform(-14, 14, len(seeds))
        falling = []
        for k in np.unique(lab):
            sel = lab == k
            py, px = ys[sel], xs[sel]
            t0 = 0.5 * (py.mean() - y0) / max(1.0, y1 - y0) + 0.4 * delay[k]
            age = t - t0
            if age <= 0:
                out[py, px] = img[py, px]
                continue
            dy = int(round(170 * age * age + 8 * age))
            dx = int(round(drift[k] * age))
            if py.mean() + dy > ground:
                continue  # landed: it is part of the heap now
            falling.append((py, px, dy, dx))
        for py, px, dy, dx in falling:
            ny, nx = py + dy, px + dx
            ok = (ny >= 1) & (ny < H - 1) & (nx >= 1) & (nx < W - 1)
            if not ok.any():
                continue
            by0, by1, bx0, bx1 = ny[ok].min() - 1, ny[ok].max() + 2, nx[ok].min() - 1, nx[ok].max() + 2
            piece = np.zeros((by1 - by0, bx1 - bx0, 4), np.uint8)
            piece[ny[ok] - by0, nx[ok] - bx0] = img[py[ok], px[ok]]
            outline_rgba(piece)
            sub = out[by0:by1, bx0:bx1]
            sel = piece[:, :, 3] > 0
            sub[sel] = piece[sel]
        return out

    def _cracked(self, f):
        """Dark cracks across the cold stone before it crumbles."""
        g = np.asarray(f).copy()
        rng = np.random.default_rng(self.seed + 130)
        for k in range(9):
            x, y = 70 + rng.random() * 140, 90 + rng.random() * 70
            a = rng.random() * TAU
            for s in range(14):
                a += (rng.random() - 0.5) * 0.9
                x += math.cos(a)
                y += math.sin(a)
                xi, yi = int(x), int(y)
                if 0 <= xi < W and 0 <= yi < H and g[yi, xi, 3] and tuple(g[yi, xi, :3]) != (11, 15, 30):
                    g[yi, xi] = C("#272522")
        return g

    # ------------------------------------------------------------ the sheet
    def clips(self, form2=False, roar=None):
        b = form2
        walk = self.walk(b)
        return [
            ("idle", self.idle(b), dict(fps=self.IDLE_FPS, loop=True)),
            ("telegraph", self.telegraph(b), dict(durations=TIMING["telegraph"], loop=False)),
            ("walk", walk, dict(fps=self.WALK_FPS, loop=True)),
            ("attack", self.attack(b), dict(durations=TIMING["attack"], loop=False, events=self.ATTACK_EVENTS)),
            ("telegraph_special", self.telegraph_special(b), dict(durations=TIMING["telegraph_special"], loop=False)),
            ("special", self.special(b), dict(fps=self.SPECIAL_FPS, loop=True)),
            ("hurt", self.hurt(b), dict(durations=TIMING["hurt"], loop=False)),
            ("summon", self.summon(b), dict(durations=TIMING["summon"], loop=False)),
            ("roar", roar if roar is not None else self.roar(b), dict(durations=TIMING["roar"], loop=False)),
            ("death", self.death(b), dict(durations=TIMING["death"], loop=False)),
            # 2.76.2, after every other clip (their cells unchanged): the dive with folded wings.
            ("telegraph_dive", self.telegraph_dive(b), dict(fps=9, loop=False)),
            ("dive", self.dive(b), dict(fps=12, loop=True)),
        ]


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


def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


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


BOSS = Pyrrhax()


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
        name = "pyrrhax" if k == 0 else "pyrrhax_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
