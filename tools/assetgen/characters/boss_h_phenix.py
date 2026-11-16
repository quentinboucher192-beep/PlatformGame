"""Le Phénix Stellaire (giga boss, eight phases).

A phoenix made of starlight and plasma: a slender bird of prey with a crested head and a hooked
beak, two huge wings whose feathers are blades of light, three long tail plumes streaming like
comet tails, and a small star burning in its chest.  Seen from the side, facing right (the game
flips it), always flying.  The plumage is a dark cosmic base (deep navy / violet feathers - small
overlapping body feathers, long flight feathers, coverts - with star speckles); the light is the
glow of the feather edges (the outer half of every flight feather, its tip white-hot), the eyes and
the light stripe behind them, the chest star, the tail plumes (brightest at the body, fading
towards their ends, which fray into sparks) and the sparks.

Two forms:
  * form I  (phases 1-4): the whole plumage;
  * form II (phases 5-8): « Renaissance » - reborn from its ashes: the flight feathers, the crest,
    the hackles and the fan of the tail are tongues of fire (gaps between them, some coverts
    gone), the chest star is larger and bare, the body feathers glow more, the tail plumes are
    longer, torn and wavy, ending in licks of fire.
The light changes colour at every phase; the plumage never does.  Every emissive pixel uses one of
five tones P0..P4 of the phase ramp (plus pure white), so each phase sheet is the phase-1 sheet with
those five colours swapped (exact LUT, `recolour`), see PHASES.

Frames 256x192, pivot [128, 150] = bottom-centre of the 64x56 collision box (x 96..160,
y 94..150) around the body (torso and legs; not the wings, the neck, the head or the tail).
Drawn at scale 1.25 in game (320x240 px frame, 80x70 px box).
ORIGIN (projectile origin = just in front of the beak, frame pixels from the pivot, x forward,
y down; x1.25 in game):
  * attack frame 1 (`fire`, the wing slash: wings struck down, talons forward, a bolt of starfire
    leaves the open beak): (+87, -61) = frame pixel (215, 89);
  * special (starfire beam loop; the head does not move on its four frames): (+80, -66)
    = frame pixel (208, 84); the beam itself is drawn by the game, the sheet only shows a short
    burst at the beak.
`Phenix.mouth(pose)` gives these points (see `attack_poses` and `special_pose`).
Clips (packed several per row, `start` = first column): idle 8 (hover, wing beats), walk 8
(= move, same frames: glide), telegraph 6 (the wings rise, light gathers at the beak and the wing
tips), attack 5 (wing slash, fire f1), telegraph_special 6 (the chest star brightens), special 4
(loop, starfire at the beak), hurt 2, summon 6 (wings spread wide, a burst of feathers of light),
roar 8 (phase change; the phase-5 sheet's roar is the rebirth: it bursts into flames, burns
white-hot, blazes away and is reborn in form II), death 14 (the light flares and gutters, it falls
while its plumage turns to ash, then collapses into a falling shower of embers and ash onto a small
heap; the light dies).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, bezier, mat, motes, ring, streak
from boss_c_common import TAU, TIMING, Pose, shake
from common.palette import OUTLINE
from raster import TCanvas

PI = math.pi
W, H = 256, 192
PIVOT = [128, 150]
BOX = (96, 94, 160, 150)  # x0, y0, x1, y1 (collision box, frame pixels)
BX, BY = 128.0, 112.0  # centre of the body (body coordinates): the whole bird tilts around it
GROUND = 184.0  # where the ashes settle (death)
WHITE = "#ffffff"
SEED = 70913

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Aube", "or", ["#fff3c8", "#ffd04a", "#f29a1c", "#a85c14", "#5a2e0c"]),
    ("Zénith", "blanc-jaune", ["#fffff2", "#fff7a8", "#efe264", "#aea23a", "#5c561c"]),
    ("Aurore", "rose", ["#ffe2ee", "#ff9ac6", "#ee5a98", "#a42e62", "#541636"]),
    ("Nébuleuse", "violet", ["#f0dfff", "#c48eff", "#8c54ee", "#5528a8", "#2b125a"]),
    ("Cendres", "gris-orangé", ["#f2e4da", "#e89c6c", "#bc6a42", "#78564a", "#3c302c"]),
    ("Renaissance", "cramoisi", ["#ffe0de", "#ff6e60", "#e22a36", "#9a1428", "#4e0a18"]),
    ("Comète", "cyan", ["#defeff", "#7aeeff", "#24bae6", "#176ea6", "#0b3658"]),
    ("Supernova", "blanc incandescent", ["#fffff8", "#f3f4ff", "#d0d6ff", "#8c92dc", "#3c3874"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# The cosmic plumage (never recoloured): deep navy / violet, star speckles.
PLUME = mat("ph_plume", ["#7069b6", "#4b4589", "#342f67", "#231f48", "#161332"], th=[0.86, 0.66, 0.46, 0.27])
PLUME_FAR = mat("ph_plume_far", ["#433f72", "#2e2a52", "#1f1c3a", "#141228"], th=[0.78, 0.52, 0.3])
VANE = mat("ph_vane", ["#7466bc", "#4e4292", "#372d6c", "#251e4c", "#171234"], th=[0.86, 0.66, 0.46, 0.27])
VANE_FAR = mat("ph_vane_far", ["#41386f", "#2c254f", "#1e1938", "#141026"], th=[0.74, 0.5, 0.28])
BEAK = mat("ph_beak", ["#d6d4f2", "#8e8cc2", "#57558c", "#35335c", "#201e3a"], th=[0.86, 0.68, 0.46, 0.26])
TALON = mat("ph_talon", ["#e8e6f6", "#aca8ce", "#6e6a90"], th=[0.7, 0.42])
MAW = mat("ph_maw", ["#3c1c3e", "#261028", "#160916"], th=[0.6, 0.3])
STAR = mat("ph_star", ["#f4f6ff", "#b6baf0"], th=[0.5])
# Ash (death): neutral greys, never recoloured.
ASH = [C("#aaa49e"), C("#7f7a75"), C("#58544f"), C("#3b3835"), C("#272523")]
ASH_MAT = mat("ph_ash", ["#aaa49e", "#7f7a75", "#58544f", "#3b3835"], th=[0.78, 0.55, 0.32])
# Light (recoloured per phase).
HOT = mat("ph_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("ph_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
# The light going out: white -> P0 .. P4 -> ash.
LADDER = [WHITE_C, *PC, ASH[2], ASH[3]]

# ------------------------------------------------------------------ the skeleton (body coordinates)
NECK_BASE = (151.0, 101.0)
TAIL_BASE = (90.0, 114.0)
SHOULDER, SHOULDER_FAR = (134.0, 98.0), (144.0, 95.0)
STAR_C = (151.0, 111.0)  # the small star in the chest
HEAD0 = (178.0, 70.0)
HS = 1.15  # head scale
WING_Y = 0.8  # the wings' vertical reach (raised: the tips stay clear of the frame top)
# Wing, relative to the shoulder, raised (y up = negative): elbow, wrist, hand tip, feather tips.
ELBOW, WRIST, HAND = (-8.0, -27.0), (5.0, -52.0), (-5.0, -71.0)
PRIMARIES = [(-12, -108), (-28, -106), (-43, -101), (-57, -93), (-68, -84), (-77, -74), (-84, -63)]
SECONDARIES = [(-90, -51), (-92, -40), (-90, -29), (-86, -19), (-79, -10), (-70, -3)]
TERTIALS = [(-60, 3), (-49, 7), (-38, 9)]
# Head, local (x forward along the head, y down), origin = the skull centre.
SKULL = [(-9, -1), (-7, -6.5), (-1, -9), (6, -8.5), (11, -5.5), (13, -1), (11, 4), (5, 7), (-3, 7.5), (-8, 4.5)]
UPPER_BEAK = [(9, -5.5), (15, -6), (21, -4.5), (25.5, -1), (27, 3), (26, 6.8), (24.5, 4.5), (22, 2.2), (16, 1.8),
              (10, 2.2)]
LOWER_BEAK = [(10, 2.4), (17, 2.2), (21.5, 3.2), (19, 5.6), (13, 6.4), (9.5, 5.2)]
HINGE = (9.5, 3.0)
BROW = [(1, -7.6), (8, -6.8), (11.5, -4.6), (8.5, -3.6), (3, -4.6)]
EYE = (6.0, -2.2)
# Crest plumes: root (head-local), angle (pi = backwards), length, half width, bow.
CREST = [((0, -8.5), PI + 0.7, 20, 3.0, -0.12), ((-3, -7.5), PI + 0.48, 27, 3.4, -0.14),
         ((-6, -6), PI + 0.3, 29, 3.4, -0.12), ((-8, -3), PI + 0.14, 22, 3.0, -0.1),
         ((-8.5, 0), PI + 0.0, 14, 2.6, -0.08)]


def _hash(a, b, salt=0.0):
    x = np.sin(np.asarray(a, np.float64) * 12.9898 + np.asarray(b, np.float64) * 78.233 + salt * 37.719) * 43758.5453
    return x - np.floor(x)


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


def _erode(m):
    g = m.copy()
    g[1:] &= m[:-1]
    g[:-1] &= m[1:]
    g[:, 1:] &= m[:, :-1]
    g[:, :-1] &= m[:, 1:]
    g[0, :] = g[-1, :] = False
    g[:, 0] = g[:, -1] = False
    return g


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def poly_mask(cv, pts):
    """Canvas.poly_mask restricted to the bounding box of `pts` (fast)."""
    m = np.zeros((cv.h, cv.w), bool)
    xs = [q[0] for q in pts]
    ys = [q[1] for q in pts]
    x0, x1 = max(0, int(math.floor(min(xs)))), min(cv.w, int(math.ceil(max(xs))) + 1)
    y0, y1 = max(0, int(math.floor(min(ys)))), min(cv.h, int(math.ceil(max(ys))) + 1)
    if x0 >= x1 or y0 >= y1:
        return m
    x, y = cv.px[y0:y1, x0:x1], cv.py[y0:y1, x0:x1]
    inside = np.zeros(x.shape, bool)
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        if ay == by:
            continue
        cond = (ay > y) != (by > y)
        xint = ax + (y - ay) * (bx - ax) / (by - ay)
        inside ^= cond & (x < xint)
    m[y0:y1, x0:x1] = inside
    return m


def blade(root, tip, w, bow=0.0, n=8, root_w=0.55, flame=0.0, t=0.0, salt=0.0, notch=0.0):
    """Outline and spine of a feather blade (a long pointed leaf) from `root` to `tip`, half width
    `w` at its widest, bowed sideways by `bow` (fraction of its length).  `flame` > 0: a tongue of
    fire (wavy edges, the end split in two licks); `notch`: ragged gaps along the vane."""
    dx, dy = tip[0] - root[0], tip[1] - root[1]
    ln = math.hypot(dx, dy) or 1.0
    ux, uy = dx / ln, dy / ln
    nx, ny = -uy, ux
    spine, left, right = [], [], []
    for i in range(n + 1):
        s = i / n
        off = bow * ln * math.sin(PI * s)
        cx, cy = root[0] + dx * s + nx * off, root[1] + dy * s + ny * off
        hw = w * (root_w + (1 - root_w) * smooth(s / 0.32))
        if s > 0.7:
            hw *= max(0.0, (1 - s) / 0.3) ** 0.75
        hl = hr = hw
        if flame:
            hl *= 1 + 0.32 * flame * math.sin(s * 10.5 + t * 2.3 + salt * 5.1)
            hr *= 1 + 0.32 * flame * math.sin(s * 9.0 - t * 1.9 + salt * 3.7 + 1.3)
        if notch and 0.25 < s < 0.95 and math.sin(s * 23.0 + salt * 7.3) > 0.55:
            hl *= 1 - notch
        spine.append((cx, cy))
        left.append((cx + nx * hl, cy + ny * hl))
        right.append((cx - nx * hr, cy - ny * hr))
    if flame:  # the end splits into two licks
        e = spine[-1]
        ln2 = ln * (0.12 + 0.05 * flame)
        wv = 0.6 * w
        lick_a = (e[0] + ux * ln2 + nx * wv * 1.1, e[1] + uy * ln2 + ny * wv * 1.1)
        lick_b = (e[0] + ux * ln2 * 0.5 - nx * wv * 0.9, e[1] + uy * ln2 * 0.5 - ny * wv * 0.9)
        mid = (e[0] - ux * ln * 0.04, e[1] - uy * ln * 0.04)
        out = left[:-1] + [lick_a, mid, lick_b] + right[:-1][::-1]
    else:
        out = left + right[-2::-1]
    return out, spine


class FPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, tilt=0.0, t=0.0, wing=(0.6, 0.6), wing_far=None, fold=0.0, spread=1.0,
                    head=HEAD0, hang=0.0, beak=0.0, glow=0.6, star=0.3, eye=0.5, sway=0.0, tail=0.0, crest=0.0,
                    talon=(0.0, 0.2), reborn=False, burst=0.0, neck=0.0)
    # wing / wing_far: (k of the arm, k of the feather tips), k = 1 raised .. 0 edge-on .. -0.8 lowered
    # hang: head angle (0 = right, < 0 up); beak 0 closed .. 1 wide; star: the chest star 0 .. 1.4
    # talon: (reach forward 0..1, toes open 0..1); tail: the plumes lifted (+) / dropped (-)
    # burst: the starfire burst at the beak (special)


def flap(ph):
    """Wing height over a flap cycle: 1 raised at ph = 0, -0.6 lowered at ph = pi."""
    return 0.2 + 0.8 * math.cos(ph)


def wings(ph, lag=0.7, far_lag=0.3):
    """Near and far wing (arm, tips) for a flap phase; the tips trail the arm."""
    return dict(wing=(flap(ph), flap(ph - lag)), wing_far=(flap(ph - far_lag), flap(ph - far_lag - lag)))


def held(k, kt=None, far=0.03):
    kt = k if kt is None else kt
    return dict(wing=(k, kt), wing_far=(k - far, kt - far))


def _lattice(u0, u1, w0, w1, su, sw, salt):
    """Staggered seeds of the small body feathers (u towards the head, w across)."""
    rng = np.random.default_rng(SEED + salt)
    pts = []
    for j, w in enumerate(np.arange(w0, w1 + sw, sw)):
        for u in np.arange(u0 + (j % 2) * su * 0.5, u1 + su, su):
            pts.append((u + rng.uniform(-0.8, 0.8), w + rng.uniform(-0.6, 0.6)))
    return np.array(pts, np.float32)


TORSO_LATTICE = _lattice(76, 170, 86, 132, 8.0, 5.2, 5)
NECK_LATTICE = _lattice(-4, 60, -10, 10, 6.0, 4.0, 7)
THIGH_LATTICE = _lattice(-14, 14, -12, 12, 5.0, 3.6, 9)


class Phenix:
    id = "phenix"
    W, H, PIVOT = W, H, PIVOT
    seed = SEED
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 9, 10, 12
    ATTACK_EVENTS = [(1, "fire")]

    # ------------------------------------------------------------ geometry
    @staticmethod
    def canvas(p):
        return TCanvas(W, H, rot=p.tilt, center=(BX, BY), offset=(p.dx, p.dy))

    @staticmethod
    def T(p, q):
        """Body coordinates -> frame pixels (same transform as the canvas)."""
        c, s = math.cos(p.tilt), math.sin(p.tilt)
        dx, dy = q[0] - BX, q[1] - BY
        return (BX + dx * c - dy * s + p.dx, BY + dx * s + dy * c + p.dy)

    @staticmethod
    def local(cv):
        """Body coordinates of every pixel centre of the canvas."""
        x = cv.px - cv.t_off[0] - cv.t_c[0]
        y = cv.py - cv.t_off[1] - cv.t_c[1]
        c, s = math.cos(-cv.t_rot), math.sin(-cv.t_rot)
        return cv.t_c[0] + x * c - y * s, cv.t_c[1] + x * s + y * c

    @staticmethod
    def head_fns(p):
        """HL: head-local -> body coordinates; JL: the same for the lower mandible (opened by p.beak)."""
        hx, hy = p.head
        ca, sa = math.cos(p.hang), math.sin(p.hang)

        def HL(lx, ly):
            lx, ly = lx * HS, ly * HS
            return (hx + lx * ca - ly * sa, hy + lx * sa + ly * ca)

        ja = p.beak * 0.55
        jc, js = math.cos(ja), math.sin(ja)

        def JL(lx, ly):
            dx, dy = lx - HINGE[0], ly - HINGE[1]
            return HL(HINGE[0] + dx * jc - dy * js, HINGE[1] + dx * js + dy * jc)

        return HL, JL

    def mouth_dir(self, p):
        return p.hang + p.tilt + 0.25 * p.beak

    def mouth_body(self, p):
        HL, JL = self.head_fns(p)
        a, b = HL(25.5, 4.2), JL(20.0, 4.4)
        d = p.hang + 0.25 * p.beak
        return ((a[0] + b[0]) / 2 + math.cos(d) * 3.0, (a[1] + b[1]) / 2 + math.sin(d) * 3.0)

    def mouth(self, p):
        """The projectile origin: just in front of the open beak (frame pixels)."""
        return self.T(p, self.mouth_body(p))

    def star_pos(self, p):
        return self.T(p, STAR_C)

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = self.canvas(p)
        self._wing(cv, p, far=True)
        self._tail(cv, p)
        self._leg(cv, p, far=True)
        self._neck(cv, p)
        self._torso(cv, p)
        self._leg(cv, p, far=False)
        self._wing(cv, p, far=False)
        self._head(cv, p)
        if p.burst > 0.02:
            self._burst(cv, p)
        return cv.render()

    @staticmethod
    def speckle(cv, x, y, bright=True, on=None):
        """A star speckle on the plumage (only over the materials `on`)."""
        xi, yi = int(math.floor(x)), int(math.floor(y))
        if not (0 <= xi < cv.w and 0 <= yi < cv.h) or cv.mat[yi, xi] < 0:
            return
        if on is not None and cv.mats[cv.mat[yi, xi]] not in on:
            return
        cv.mat[yi, xi] = cv._mid(STAR)
        cv.inten[yi, xi] = 0.8 if bright else 0.3

    def feather(self, cv, L, root, tip, w, m, bow=0.0, glow=0.0, side=1, s0=0.3, tip_glow=0.0, flame=0.0, t=0.0,
                salt=0.0, base=0.52, char=0.3, notch=0.0, line=True, n=8):
        """A feather blade from `root` to `tip` (any space, mapped to the frame by `L`): dark vane,
        lit on its upper side, a lighter shaft, its edge on `side` (+1 / -1 of root->tip) glowing
        from `s0` to the tip.  `flame` > 0: a tongue of fire instead (form II), its quill charred up
        to `char`."""
        out, spine = blade(root, tip, w, bow, n=n, flame=flame, t=t, salt=salt, notch=notch)
        pts = [L(q) for q in out]
        sp = np.array([L(q) for q in spine], np.float32)
        mask = poly_mask(cv, pts)
        if not mask.any():
            return mask
        ys, xs = np.nonzero(mask)
        px, py = xs + 0.5, ys + 0.5
        r, e = sp[0], sp[-1]
        dx, dy = float(e[0] - r[0]), float(e[1] - r[1])
        ll = max(1e-3, dx * dx + dy * dy)
        s = np.clip(((px - r[0]) * dx + (py - r[1]) * dy) / ll, 0, 1)
        ts = np.linspace(0, 1, len(sp))
        sx, sy = np.interp(s, ts, sp[:, 0]), np.interp(s, ts, sp[:, 1])
        v = (dx * (py - sy) - dy * (px - sx)) / math.sqrt(ll)
        vn = v / max(0.8, float(np.abs(v).max()))
        nl = (-dy * -0.62 + dx * -0.78) / math.sqrt(ll)  # the side of +v faces the light by nl
        it = np.zeros((cv.h, cv.w), np.float32)
        if flame > 0:
            fl = s > char
            core = 0.97 - 0.62 * np.abs(vn) ** 1.2 - 0.5 * np.clip((s - char) / (1 - char), 0, 1) ** 1.2
            it[ys, xs] = np.where(fl, core, base + 0.12 * vn * np.sign(nl) - 0.1 * s)
            qm = np.zeros_like(mask)
            qm[ys[~fl], xs[~fl]] = True
            cv.part(line)
            cv.put(qm, it, m)
            fm = np.zeros_like(mask)
            fm[ys[fl], xs[fl]] = True
            cv.part(line=False)
            cv.put(fm, it, HOT)
            # the quill glows where the flame starts
            gl = np.zeros_like(mask)
            sel = (~fl) & (s > char - 0.12) & (np.abs(v) < 1.0)
            gl[ys[sel], xs[sel]] = True
            cv.put(gl, 0.62, HOT, decal=True)
            return mask
        shaft = (np.abs(v) < 0.55) & (s > 0.04) & (s < 0.86)
        it[ys, xs] = base + 0.13 * vn * (1 if nl > 0 else -1) * min(1.0, abs(nl) * 2) + 0.1 * (1 - s) + 0.1 * shaft
        cv.part(line)
        cv.put(mask, it, m)
        if glow > 0.01:
            rim = mask & ~_erode(mask)
            rf = rim[ys, xs]
            g = np.zeros((cv.h, cv.w), np.float32)
            gs = rf & (v * side > -0.2) & (s >= s0)
            g[ys, xs] = glow * (0.42 + 0.58 * np.clip((s - s0) / max(1e-3, 1 - s0), 0, 1))
            tipm = rf & (s > 0.84)
            g[ys[tipm], xs[tipm]] = min(1.0, glow * 0.95 + tip_glow)
            gm = np.zeros_like(mask)
            gm[ys[gs | tipm], xs[gs | tipm]] = True
            cv.part(line=False)
            cv.put(gm, g, HOT, decal=True)
        return mask

    def plumage(self, cv, p, mask, u, w, seeds, rx, ry, salt, thr=0.6, extra=None, far=False):
        """Small overlapping feathers over `mask` (u: towards the head, w: across).  The feather
        nearer the head lies on top, its free rounded tip pointing back: the free edges catch the
        light, the feather under them is shaded; some edges glow (the light of the phase); star
        speckles here and there."""
        K = len(seeds)
        lab = np.argmin((u[:, None] - seeds[None, :, 0]) ** 2 + (w[:, None] - seeds[None, :, 1]) ** 2, axis=1)
        order = np.argsort(seeds[:, 0], kind="stable")
        for k in order:
            inside = ((u - (seeds[k, 0] - 0.45 * rx)) / rx) ** 2 + ((w - seeds[k, 1]) / ry) ** 2 <= 1.0
            lab[inside] = k
        rank = np.empty(K, np.int32)
        rank[order] = np.arange(K, dtype=np.int32)
        L2 = np.full(mask.shape, -1, np.int32)
        L2[mask] = rank[lab]
        edge = np.zeros(mask.shape, bool)
        shade = np.zeros(mask.shape, bool)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            nb = np.roll(np.roll(L2, dy, 0), dx, 1)
            edge |= mask & (nb >= 0) & (nb < L2)
            shade |= mask & (nb > L2)
        shade &= ~edge
        it = cv.inten.copy()
        var = (_hash(lab, 3, salt) - 0.5) * 0.1
        it[mask] = it[mask] + var - 0.08
        it[edge] += 0.12
        it[shade] -= 0.2
        cv.inten[mask] = np.clip(it[mask], 0, 1)
        # glowing edges
        pg = np.zeros(mask.shape, np.float32)
        pg[mask] = _hash(lab, 11, salt)
        ex = np.zeros(mask.shape, np.float32)
        if extra is not None:
            ex[mask] = extra
        hot = edge & ((pg > thr) | (ex > 0.15))
        gi = np.clip(0.3 + 0.3 * min(1.3, p.glow) * (0.5 + 0.5 * pg) + ex, 0, 1) * (0.8 if far else 1.0)
        cv.part(line=False)
        cv.put(hot, gi, HOT, decal=True)
        # star speckles
        h1 = np.zeros(mask.shape, np.float32)
        h1[mask] = _hash(np.floor(u), np.floor(w), salt + 0.5)
        h2 = np.zeros(mask.shape, np.float32)
        h2[mask] = _hash(np.floor(w), np.floor(u), salt + 0.7)
        sp = mask & ~edge & ~shade & (h1 > 0.986) & ~hot
        if sp.any():
            cv.mat[sp] = cv._mid(STAR)
            cv.inten[sp] = np.where(h2[sp] > 0.45, 0.8, 0.3)
        return edge

    # ------------------------------------------------------------ wings
    def wing_feathers(self):
        """Flight feathers (inner first, the outermost primary last) and coverts of a wing, raised
        layout (shoulder space): (root, tip, half width, bow, glow, kind)."""
        fl, cov = [], []
        nt, ns, npr = len(TERTIALS), len(SECONDARIES), len(PRIMARIES)
        for j in range(nt - 1, -1, -1):
            fl.append((lerp(ELBOW, (-5.0, 3.0), (j + 0.5) / nt), TERTIALS[j], 6.2, 0.02, 0.62, "t"))
        for j in range(ns - 1, -1, -1):
            fl.append((lerp(WRIST, ELBOW, (j + 0.5) / ns * 0.96), SECONDARIES[j], 6.0, 0.03, 0.8, "s"))
        for j in range(npr - 1, -1, -1):
            fl.append((lerp(HAND, WRIST, 0.08 + 0.8 * j / (npr - 1)), PRIMARIES[j], 4.4 + 0.2 * j, -0.05, 1.0, "p"))
        for root, tp, w, bow, g, kind in fl:
            if kind == "t":
                continue
            r2 = lerp(root, (0.0, -40.0) if kind == "s" else WRIST, 0.06)
            cov.append((r2, lerp(r2, tp, 0.42 if kind == "s" else 0.38), 5.4 if kind == "s" else 4.6, 0.0, 0.42,
                        "g"))
        for root, tp, w, bow, g, kind in fl:
            if kind == "s":
                r2 = lerp(root, (0.0, -40.0), 0.1)
                cov.append((r2, lerp(r2, tp, 0.24), 4.8, 0.0, 0.3, "m"))
        return fl, cov

    def wing_map(self, p, far):
        """(L, kof) of a wing: L maps the raised layout (shoulder space) to frame pixels for the
        pose (flap, fold, spread), kof gives the flap factor k at a point (the tips trail the arm)."""
        S = SHOULDER_FAR if far else SHOULDER
        if far:
            ka, kt = p.wing_far if p.wing_far is not None else (p.wing[0] - 0.03, p.wing[1] - 0.03)
        else:
            ka, kt = p.wing
        sc = 0.9 if far else 1.0
        rf = 0.18 if far else 0.0
        fo = min(1.0, max(0.0, p.fold))
        cr, sr = math.cos(rf), math.sin(rf)

        def kof(q):
            return ka + (kt - ka) * min(1.0, math.hypot(q[0], q[1]) / 95.0)

        def L(q):
            k = kof(q)
            x, y = q[0] * (1 - 0.45 * fo) * p.spread, q[1] * (1 - 0.6 * fo) * p.spread
            if rf:
                x, y = x * cr - y * sr, x * sr + y * cr
            return self.T(p, (S[0] + x * sc * (0.84 + 0.16 * abs(k)), S[1] + y * sc * k * WING_Y))

        return L, kof

    def wing_tips(self, p, far=False):
        """Frame pixels of the primary tips of a wing."""
        L, _ = self.wing_map(p, far)
        return [L(q) for q in PRIMARIES]

    def _wing(self, cv, p, far):
        T = cv.T
        S = SHOULDER_FAR if far else SHOULDER
        sc = 0.9 if far else 1.0
        L, kof = self.wing_map(p, far)
        vane = VANE_FAR if far else VANE
        plume = PLUME_FAR if far else PLUME
        gk = (0.72 if far else 1.0) * min(1.25, p.glow + 0.15)
        fl, cov = self.wing_feathers()
        rng = np.random.default_rng(SEED + (31 if far else 37))
        specks = []
        for i, (root, tp, w, bow, g, kind) in enumerate(fl):
            side = -1 if kof(tp) >= 0 else 1
            if p.reborn and kind != "t":
                self.feather(cv, L, root, tp, w * 0.8, vane, bow=bow, flame=1.0, t=p.t + i * 0.7, salt=i * 0.37,
                             base=0.42, char=0.26 if kind == "p" else 0.3, n=10)
            else:
                self.feather(cv, L, root, tp, w, vane, bow=bow, glow=g * gk, side=side, s0=0.5,
                             tip_glow=0.15 if kind == "p" else 0.0, base=0.5 if far else 0.54)
            specks.append(lerp(root, tp, 0.45 + 0.4 * rng.random()))
        # the arm (leading edge), under the coverts
        cv.part()
        cv.chain([T(S), L(ELBOW), L(WRIST), L(HAND)], [6.0 * sc, 4.2 * sc, 3.2 * sc, 1.8 * sc], plume, bias=-0.12)
        for i, (root, tp, w, bow, g, kind) in enumerate(cov):
            if p.reborn and i % 4 == 3:
                continue  # gaps in the reborn plumage
            side = -1 if kof(tp) >= 0 else 1
            self.feather(cv, L, root, tp, w, plume, bow=bow, glow=g * gk * (1.35 if p.reborn else 0.9), side=side,
                         s0=0.7, base=0.46)
            if rng.random() < 0.6:
                specks.append(lerp(root, tp, 0.3 + 0.5 * rng.random()))
        # marginal coverts: small feathers along the arm, lying back over the wing
        arm = [(0.0, 0.0), ELBOW, WRIST, HAND]
        for a0, a1, cnt in ((0, 1, 4), (1, 2, 4), (2, 3, 3)):
            for i in range(cnt):
                q = lerp(arm[a0], arm[a1], (i + 0.3) / cnt)
                d = math.atan2(arm[a1][1] - arm[a0][1], arm[a1][0] - arm[a0][0])
                b = d - 1.25  # towards the trailing edge
                ln = 11.0 - 1.5 * a0
                tip = (q[0] + math.cos(b) * ln, q[1] + math.sin(b) * ln)
                side = -1 if kof(tip) >= 0 else 1
                self.feather(cv, L, q, tip, 3.6 - 0.4 * a0, plume, glow=0.0, side=side, base=0.44)
        # the alula, the light along the leading edge
        self.feather(cv, L, WRIST, (WRIST[0] + 9, WRIST[1] - 10), 2.4, plume, glow=0.6 * gk, side=1, s0=0.3)
        cv.part(line=False)
        li = (0.3 + 0.32 * min(1.2, p.glow)) * (0.8 if far else 1.0)
        cv.line(L(lerp(ELBOW, WRIST, 0.15)), L(lerp(ELBOW, WRIST, 0.9)), HOT, inten=li, decal=True)
        cv.line(L(lerp(WRIST, HAND, 0.15)), L(lerp(WRIST, HAND, 0.95)), HOT, inten=li * 0.9, decal=True)
        on = (vane, plume)
        for k, q in enumerate(specks):
            x, y = L(q)
            self.speckle(cv, x, y, bright=(k % 3 != 0) and not far, on=on)

    # ------------------------------------------------------------ legs
    def _leg(self, cv, p, far):
        T = cv.T
        reach, open_ = p.talon
        if far:
            reach, open_ = reach * 0.7, open_ * 0.6
        ox, oy = (7.0, -3.0) if far else (0.0, 0.0)
        mp = PLUME_FAR if far else PLUME
        mb = PLUME_FAR if far else BEAK
        thigh = (118 + ox, 121 + oy)
        knee = lerp((114 + ox, 131 + oy), (128 + ox, 133 + oy), reach)
        ankle = lerp((106 + ox, 139 + oy), (146 + ox, 141 + oy), reach)
        cv.part()
        cv.ellipse(T(thigh), 10.5, 8.0, mp, ang=0.5 + p.tilt)
        cv.ellipse(T(lerp(thigh, knee, 0.6)), 6.5, 5.5, mp, ang=0.9 + p.tilt)
        if not far:
            mask = cv.pid == cv.cur
            lx, ly = self.local(cv)
            self.plumage(cv, p, mask, lx[mask] - thigh[0], ly[mask] - thigh[1], THIGH_LATTICE, 4.6, 3.2, salt=13,
                         thr=0.7)
        # the feathered "trousers": pointed feathers hanging over the shank
        d = math.atan2(knee[1] - thigh[1], knee[0] - thigh[0])
        for k, (o, ln) in enumerate(((-0.35, 13.0), (0.0, 15.0), (0.32, 12.0))):
            a = d + o * 0.8 + 0.35
            root = (thigh[0] + math.cos(a) * 4, thigh[1] + math.sin(a) * 4)
            tip = (root[0] + math.cos(a) * ln, root[1] + math.sin(a) * ln)
            self.feather(cv, T, root, tip, 3.0, mp, glow=(0.45 if not far else 0.3) * min(1.2, p.glow), side=1,
                         s0=0.5, base=0.48)
        cv.part()
        cv.chain([T(knee), T(ankle)], [3.4, 2.5], mb, bias=-0.18)
        dd = math.atan2(ankle[1] - knee[1], ankle[0] - knee[0])
        curl = 1.0 - open_
        cv.part()
        cv.ellipse(T(ankle), 3.2, 2.6, mb, ang=dd + p.tilt, bias=-0.18)
        base_a = dd - 0.9 + 0.9 * reach
        for f, (da, ln) in enumerate(((-0.55, 6.0), (0.0, 7.0), (0.5, 5.5))):
            a0 = base_a + da * (0.6 + 0.6 * open_)
            j1 = (ankle[0] + math.cos(a0) * ln * 0.6, ankle[1] + math.sin(a0) * ln * 0.6)
            a1 = a0 + curl * 0.9
            j2 = (j1[0] + math.cos(a1) * ln * 0.5, j1[1] + math.sin(a1) * ln * 0.5)
            a2 = a1 + curl * 0.8 + 0.3
            j3 = (j2[0] + math.cos(a2) * 4.0, j2[1] + math.sin(a2) * 4.0)
            cv.part()
            cv.chain([T(ankle), T(j1), T(j2)], [1.9, 1.6, 1.3], mb, bias=-0.12)
            cv.part()
            cv.capsule(T(j2), T(j3), 1.3, 0.3, TALON)
        a0 = base_a + PI - 0.5  # the hind toe
        j1 = (ankle[0] + math.cos(a0) * 4.0, ankle[1] + math.sin(a0) * 4.0)
        j2 = (j1[0] + math.cos(a0 - 0.9 * curl - 0.4) * 3.6, j1[1] + math.sin(a0 - 0.9 * curl - 0.4) * 3.6)
        cv.part()
        cv.chain([T(ankle), T(j1)], [1.7, 1.4], mb, bias=-0.12)
        cv.part()
        cv.capsule(T(j1), T(j2), 1.2, 0.3, TALON)
        if not far:
            cv.part(line=False)
            cv.line(T(lerp(knee, ankle, 0.15)), T(lerp(knee, ankle, 0.85)), HOT,
                    inten=0.22 + 0.3 * min(1.2, p.glow), decal=True)

    # ------------------------------------------------------------ neck, torso, chest star
    def neck_pts(self, p):
        HL, _ = self.head_fns(p)
        n0 = NECK_BASE
        ne = HL(-7, 2)
        ln = math.hypot(ne[0] - n0[0], ne[1] - n0[1])
        p1 = (n0[0] + 0.42 * 0.42 * ln, n0[1] - 0.9 * 0.42 * ln)
        hd = (math.cos(p.hang), math.sin(p.hang))
        p2 = (ne[0] - hd[0] * 0.4 * ln, ne[1] - hd[1] * 0.4 * ln)
        return bezier(n0, p1, p2, ne, 10)

    def _neck(self, cv, p):
        T = cv.T
        pts = np.array([T(q) for q in self.neck_pts(p)], np.float32)
        n = len(pts)
        radii = np.array([9.5 - 3.0 * i / (n - 1) for i in range(n)], np.float32)
        cv.part()
        cv.chain([tuple(q) for q in pts], list(radii), PLUME)
        mask = cv.pid == cv.cur
        seg = np.hypot(*(pts[1:] - pts[:-1]).T)
        cum = np.concatenate([[0.0], np.cumsum(seg)])
        total = float(cum[-1])
        ns = max(16, int(total))
        s = np.linspace(0, total, ns)
        cx, cy, cr = np.interp(s, cum, pts[:, 0]), np.interp(s, cum, pts[:, 1]), np.interp(s, cum, radii)
        tx, ty = np.gradient(cx), np.gradient(cy)
        tl = np.hypot(tx, ty) + 1e-6
        tx, ty = tx / tl, ty / tl
        ys, xs = np.nonzero(mask)
        px, py = xs + 0.5, ys + 0.5
        j = np.argmin((px[:, None] - cx[None, :]) ** 2 + (py[:, None] - cy[None, :]) ** 2, axis=1)
        v = (px - cx[j]) * (-ty[j]) + (py - cy[j]) * tx[j]
        vn = v / np.maximum(cr[j], 1.0)
        self.plumage(cv, p, mask, s[j], 9.0 * vn, NECK_LATTICE, 5.4, 3.4, salt=11, thr=0.78 if not p.reborn else 0.45)
        # hackles: pointed feathers along the back of the neck, lying back towards the body
        for k, sk in enumerate((0.42, 0.56, 0.7, 0.84)):
            i = min(ns - 1, int(sk * (ns - 1)))
            nx, ny = ty[i], -tx[i]  # the back of the neck
            root = (cx[i] + nx * cr[i] * 0.55, cy[i] + ny * cr[i] * 0.55)
            a = math.atan2(-ty[i], -tx[i]) + 0.42
            ln = 13.0 + 2.0 * (k % 2) + (4.0 if p.reborn else 0.0)
            tip = (root[0] + math.cos(a) * ln, root[1] + math.sin(a) * ln)
            if p.reborn:
                self.feather(cv, lambda q: q, root, tip, 2.8, PLUME, flame=0.8, t=p.t + k, salt=k * 0.6, char=0.35)
            else:
                self.feather(cv, lambda q: q, root, tip, 2.8, PLUME, glow=0.7 * min(1.2, p.glow + 0.1), side=1,
                             s0=0.35, base=0.5)

    def _torso(self, cv, p):
        T = cv.T
        cv.part()
        cv.ellipse(T((101, 113)), 18, 11, PLUME, ang=0.12 + p.tilt)
        cv.ellipse(T((124, 111)), 26, 15, PLUME, ang=0.0 + p.tilt)
        cv.ellipse(T((146, 107)), 16, 16, PLUME, ang=-0.3 + p.tilt)
        mask = cv.pid == cv.cur
        lx, ly = self.local(cv)
        u, w = lx[mask], ly[mask]
        dc = np.hypot(u - STAR_C[0], w - STAR_C[1])
        extra = (0.2 + 0.45 * p.star) * np.exp(-dc / (4.0 + 4.0 * p.star)) * (1.5 if p.reborn else 1.0)
        self.plumage(cv, p, mask, u, w, TORSO_LATTICE, 7.8, 5.0, salt=3, thr=0.8 if not p.reborn else 0.5,
                     extra=extra)
        self._star(cv, p, mask, lx, ly)

    def _star(self, cv, p, mask, lx, ly):
        """The small star in the chest: a white core, four long rays and four short ones; it shines
        through a parting of the breast feathers (form I), bare and larger in form II."""
        cx, cy = STAR_C
        st = p.star
        dx, dy = lx - cx, ly - cy
        d = np.hypot(dx, dy)
        if p.reborn:
            a = np.arctan2(dy, dx)
            r = 5.0 + 1.8 * st
            q = d / r
            hole = mask & (q <= 1.0 + 0.2 * np.sin(6 * a + p.t * 0.7) + 0.08 * np.sin(11 * a))
            cv.part(line=False)
            cv.put(hole, np.clip(1.05 - 0.75 * q + 0.05 * math.sin(p.t * 2.0), 0, 1), CORE, decal=True)
            rim = _dilate(hole) & mask & ~hole
            cv.put(rim, 0.78, HOT, decal=True)
            ring2 = _dilate(hole, 2) & mask & ~_dilate(hole)
            cv.put(ring2, 0.5, HOT, decal=True)
        else:
            r = 2.0 + 1.6 * st
            halo = mask & (d < r + 4.0) & (np.abs(dx) + np.abs(dy) < r + 4.0)
            cv.part(line=False)
            cv.put(halo, np.clip(0.3 + 0.4 * st - 0.06 * d, 0, 1), HOT, decal=True)
            core = mask & (d < r)
            cv.put(core, np.clip(1.05 - 0.55 * d / r + 0.1 * st, 0, 1), CORE, decal=True)
        # rays (body space: they tilt with the bird)
        T = cv.T
        ln = (5.0 + 7.0 * st) * (1.35 if p.reborn else 1.0)
        for k in range(8):
            a = k * PI / 4 + 0.05
            ll = ln if k % 2 == 0 else ln * 0.5
            r0 = r * 0.6
            for i in range(int(ll)):
                rr = r0 + i
                fr = i / max(1.0, ll)
                cv.px_set(*T((cx + math.cos(a) * rr, cy + math.sin(a) * rr)), HOT if fr > 0.25 else CORE,
                          inten=np.clip(0.98 - 0.7 * fr, 0, 1) if fr > 0.25 else 0.7, decal=True)

    # ------------------------------------------------------------ tail
    def tail_paths(self, p):
        """The three streamers (body coordinates, before the sway): upper, middle, lower."""
        if p.reborn:  # longer, more sinuous
            ends = [((66, 100), (42, 90), (18, 80)), ((62, 118), (38, 128), (15, 124)),
                    ((66, 133), (46, 154), (22, 163))]
        else:
            ends = [((66, 106), (44, 102), (20, 95)), ((64, 118), (42, 124), (18, 120)),
                    ((66, 128), (48, 142), (26, 148))]
        roots = [(90.0, 110.0), (89.0, 114.0), (89.0, 118.0)]
        out = []
        lift = p.tail
        for k, (r, (c1, c2, e)) in enumerate(zip(roots, ends)):
            c1 = (c1[0], c1[1] - 3 * lift)
            c2 = (c2[0], c2[1] - 7 * lift)
            e = (e[0], e[1] - 12 * lift)
            base = bezier(r, c1, c2, e, 18)
            pts = []
            n = len(base)
            for i, (x, y) in enumerate(base):
                s = i / (n - 1)
                a, b = base[max(0, i - 1)], base[min(n - 1, i + 1)]
                tx, ty = b[0] - a[0], b[1] - a[1]
                tl = math.hypot(tx, ty) or 1.0
                amp = (5.0 + 1.5 * k + (2.0 if p.reborn else 0.0)) * s * math.sin(p.sway - 2.8 * s - 0.6 * k)
                pts.append((x - ty / tl * amp, y + tx / tl * amp))
            out.append(pts)
        return out

    def _streamer(self, cv, p, pts, wmax, k):
        """A long tail plume of light streaming like a comet's tail: narrow at the body, widening,
        brightest along its middle near the body and fading towards the end, which frays into
        sparks (form II: longer, torn, licked by flames)."""
        T = cv.T
        P_ = np.array([T(q) for q in pts], np.float32)
        n = len(P_)
        left, right, tl, hws = [], [], [], []
        for i in range(n):
            s = i / (n - 1)
            a, b = P_[max(0, i - 1)], P_[min(n - 1, i + 1)]
            tx, ty = b[0] - a[0], b[1] - a[1]
            ll = math.hypot(tx, ty) or 1.0
            tx, ty = tx / ll, ty / ll
            tl.append((tx, ty))
            hw = 1.4 + (wmax - 1.4) * smooth(s / 0.62)
            if s > 0.8:
                hw *= 1.0 - 0.55 * (s - 0.8) / 0.2
            hws.append(hw)
            hl = hr = hw
            if p.reborn:  # torn (notches in the vane), wavy like flames
                hl *= 1 + 0.3 * math.sin(s * 17 + p.t * 2.0 + k)
                hr *= 1 + 0.3 * math.sin(s * 15 - p.t * 1.7 + k * 2)
                if 0.25 < s < 0.92 and math.sin(s * 29 + k * 2.1) > 0.55:
                    hl *= 0.3
                if 0.25 < s < 0.92 and math.sin(s * 23 + k * 1.3 + 1.0) > 0.65:
                    hr *= 0.35
            left.append((P_[i][0] - ty * hl, P_[i][1] + tx * hl))
            right.append((P_[i][0] + ty * hr, P_[i][1] - tx * hr))
        e = P_[-1]
        tx, ty = tl[-1]
        if p.reborn:  # the end frays into licks of fire
            tips = [(e[0] + tx * 6 - ty * 2.5, e[1] + ty * 6 + tx * 2.5), (e[0] + tx * 1.5, e[1] + ty * 1.5),
                    (e[0] + tx * 4.5 + ty * 2.5, e[1] + ty * 4.5 - tx * 2.5)]
            outline = left + tips + right[::-1]
        else:
            outline = left + [(e[0] + tx * 3, e[1] + ty * 3)] + right[::-1]
        mask = poly_mask(cv, outline)
        if not mask.any():
            return
        ys, xs = np.nonzero(mask)
        px, py = xs + 0.5, ys + 0.5
        j = np.argmin((px[:, None] - P_[None, :, 0]) ** 2 + (py[:, None] - P_[None, :, 1]) ** 2, axis=1)
        s = j / (n - 1)
        tla = np.array(tl, np.float32)
        v = (px - P_[j, 0]) * (-tla[j, 1]) + (py - P_[j, 1]) * tla[j, 0]
        vn = np.clip(np.abs(v) / np.maximum(np.array(hws)[j], 1.0), 0, 1)
        g = min(1.2, 0.75 + 0.35 * p.glow)
        it = g * (1.04 - 0.74 * s ** 1.1) - 0.4 * vn ** 1.6 + 0.06 * np.sin(v * 2.3 + k * 1.7)
        # the end dissolves
        hh = _hash(xs, ys, k + 0.3 + 0.37 * (int(p.t * 3) % 5))
        keep = ~((s > 0.74) & (hh < (s - 0.74) / 0.26 * 0.75))
        fm = np.zeros_like(mask)
        fm[ys[keep], xs[keep]] = True
        full = np.zeros((cv.h, cv.w), np.float32)
        full[ys, xs] = it
        cv.part(line=False)
        cv.put(fm, full, HOT)
        # sparks beyond the end
        for i in range(5):
            dd = 5 + i * 3.2 + (k % 2) * 1.5
            wob = math.sin(p.t * 1.3 + i * 2.1 + k) * (0.8 + 0.6 * i)
            x, y = e[0] + tx * dd - ty * wob, e[1] + ty * dd + tx * wob
            if 3 <= x < W - 3 and 3 <= y < H - 3 and cv.mat[int(y), int(x)] < 0:
                cv.px_set(x, y, HOT, inten=0.56 - 0.09 * i)

    def _tail(self, cv, p):
        paths = self.tail_paths(p)
        for k in (0, 2, 1):
            self._streamer(cv, p, paths[k], [4.4, 4.0, 5.2][k], k)
        # the fan of short tail feathers over the streamer roots
        T = cv.T
        g = min(1.2, p.glow + 0.1)
        fan = [(PI + 0.32, 26.0), (PI + 0.12, 30.0), (PI - 0.12, 29.0), (PI - 0.34, 24.0)]
        for i, (a, ln) in enumerate(fan):
            a += 0.08 * p.tail * -1 + 0.04 * math.sin(p.sway - i)
            root = (94.0, 111.0 + 2.2 * i)
            tip = (root[0] + math.cos(a) * ln, root[1] + math.sin(a) * ln)
            if p.reborn:
                self.feather(cv, T, root, tip, 4.0, VANE, flame=0.9, t=p.t + i, salt=i * 0.41, char=0.35)
            else:
                self.feather(cv, T, root, tip, 5.4, VANE, glow=0.8 * g, side=1 if i < 2 else -1, s0=0.4,
                             base=0.5)

    # ------------------------------------------------------------ head
    def _head(self, cv, p):
        T = cv.T
        HL0, JL0 = self.head_fns(p)

        def HL(x, y):
            return T(HL0(x, y))

        def JL(x, y):
            return T(JL0(x, y))

        def HLq(q):
            return HL(q[0], q[1])

        gl = min(1.2, p.glow)
        # crest (behind the skull), the far plumes first
        for i in range(len(CREST) - 1, -1, -1):
            root, ang, ln, w, bow = CREST[i]
            a = ang + 0.09 * math.sin(p.crest - i * 0.9)
            ln2 = ln * (1.25 if p.reborn else 1.0)
            tip = (root[0] + math.cos(a) * ln2, root[1] + math.sin(a) * ln2)
            if p.reborn:
                self.feather(cv, HLq, root, tip, w * 1.1, PLUME, bow=bow, flame=1.0, t=p.t + i * 1.1, salt=i * 0.53,
                             char=0.22)
            else:
                self.feather(cv, HLq, root, tip, w, PLUME, bow=bow, glow=min(1.2, gl + 0.35), side=1, s0=0.2,
                             tip_glow=0.25, base=0.5)
        # nape feathers
        for i, (r, a, ln) in enumerate((((-6, 0.5), PI - 0.2, 11.0), ((-5.5, 3.5), PI - 0.42, 10.0),
                                        ((-4.5, 6), PI - 0.66, 8.0))):
            tip = (r[0] + math.cos(a) * ln, r[1] + math.sin(a) * ln)
            self.feather(cv, HLq, r, tip, 2.6, PLUME, glow=0.6 * gl, side=-1, s0=0.4, base=0.46)
        # mouth interior, the starfire in the throat
        if p.beak > 0.05:
            cv.part(line=False)
            cv.poly([HL(8, 1.6), HL(24, 2.6), JL(20, 3.6), JL(8, 3.4)], MAW, flat=0.3)
            g = min(1.2, 0.35 + 0.65 * max(p.glow, p.star * 0.8))
            cv.poly([HL(8, 2.0), HL(10 + 7 * g, 2.3), JL(10 + 6 * g, 3.5), JL(8, 3.2)], HOT, flat=0.45 + 0.3 * g)
            cv.poly([HL(8, 2.3), HL(9 + 3 * g, 2.4), JL(9 + 3 * g, 3.3), JL(8, 3.0)], CORE, flat=0.6 + 0.3 * g)
        # lower mandible
        cv.part()
        cv.poly([JL(*q) for q in LOWER_BEAK], BEAK, bevel=1.2, base=0.5)
        # skull
        cv.part()
        cv.poly([HL(*q) for q in SKULL], PLUME, bevel=1.6, base=0.58)
        # upper beak (hooked)
        cv.part()
        cv.poly([HL(*q) for q in UPPER_BEAK], BEAK, bevel=1.4, base=0.6)
        # brow over the eye
        cv.part()
        cv.poly([HL(*q) for q in BROW], PLUME, bevel=1.0, base=0.36)
        cv.part(line=False)
        e = min(1.3, p.eye + (0.25 if p.reborn else 0.0))
        if p.glow > 0.03:
            cv.capsule(HL(EYE[0] - 1.6, EYE[1] - 0.2), HL(EYE[0] + 1.8, EYE[1] + 0.1), 1.25 + 0.25 * e, 1.0 + 0.2 * e,
                       CORE, flat=0.55 + 0.32 * e)
            cv.px_set(*HL(EYE[0] + 1.2, EYE[1] - 0.2), CORE, inten=1.0)
            # the light stripe running back from the eye (a falcon's mark, of light)
            si = 0.28 + 0.4 * gl
            cv.line(HL(EYE[0] - 2.5, EYE[1] + 0.5), HL(-5, 0.5), HOT, inten=si, decal=True)
            cv.line(HL(-5, 0.5), HL(-7.5, 4.0), HOT, inten=si * 0.8, decal=True)
            cv.line(HL(EYE[0] + 1, EYE[1] + 2.2), HL(EYE[0] - 0.5, EYE[1] + 5.5), HOT, inten=si * 0.85, decal=True)
            # the cutting edge of the beak, the nostril
            cv.line(HL(11, 1.6), HL(21, 1.9), HOT, inten=0.24 + 0.3 * gl, decal=True)
            cv.px_set(*HL(13.5, -3.2), HOT, inten=0.5 + 0.3 * gl, decal=True)
        else:
            cv.line(HL(EYE[0] - 1.5, EYE[1]), HL(EYE[0] + 1.5, EYE[1]), MAW, inten=0.4)
        cv.px_set(*HL(-1.0, -4.0), STAR, inten=0.8, decal=True)

    def _burst(self, cv, p):
        """A short burst of starfire at the open beak (the beam itself is drawn by the game)."""
        mx, my = self.mouth_body(p)
        d = p.hang + 0.25 * p.beak
        c, s = math.cos(d), math.sin(d)
        T = cv.T
        f = p.burst
        x0, y0 = mx - c * 4, my - s * 4

        def at(ln, off):
            return T((x0 + c * ln - s * off, y0 + s * ln + c * off))

        for layer, (m, wf, lf, it) in enumerate(((HOT, 1.0, 1.0, 0.5), (HOT, 0.62, 0.86, 0.76),
                                                  (CORE, 0.32, 0.7, 0.95))):
            n = 6
            L_ = 20.0 * f * lf
            top, bot = [], []
            for i in range(n + 1):
                t = i / n
                wd = (1.2 + 5.5 * math.sin(min(1.0, t * 1.4) * PI / 2)) * wf * (1.0 - 0.35 * t)
                fl = (0.8 if i % 2 else -0.4) * wf * math.sin(p.t * 2.1 + i * 1.7 + layer)
                top.append(at(L_ * t, -(wd + fl)))
                bot.append(at(L_ * t, wd - fl))
            end = [at(L_ * 1.1, -2.0 * wf), at(L_ * (1.18 + 0.05 * math.sin(p.t + layer)), 0.0), at(L_ * 1.1, 2.0 * wf)]
            cv.part(line=False)
            cv.poly(top + end + bot[::-1], m, flat=it)

    # ------------------------------------------------------------- effects
    def sparkle(self, f, x, y, size, c0=None, c1=None):
        FX.sparkle(f, int(x), int(y), size, c0 or WHITE_C, c1 or PC[1])

    def charge(self, f, x, y, i, n, seed, r0=26, r1=4, count=10):
        """Motes of light converging onto (x, y)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = rng.random() * TAU
            d = r0 * (1 - t) + r1 + rng.random() * 6
            put_if_empty(f, x + math.cos(a) * d, y + math.sin(a) * d, [WHITE_C, PC[0], PC[1], PC[2]][k % 4])

    def sparks(self, f, i, n, seed, count=10, p=None):
        """Sparks shed by the plumage, drifting back and up while they fade (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        ox, oy = (p.dx, p.dy) if p is not None else (0.0, 0.0)
        for k in range(count):
            x0 = 60 + rng.random() * 100 + ox
            y0 = 60 + rng.random() * 70 + oy
            t = (i / n + rng.random()) % 1.0
            x = x0 - t * 26
            y = y0 - t * 14 + math.sin(t * 6 + k) * 2.0
            put_if_empty(f, x, y, [PC[0], PC[1], PC[2], PC[3]][min(3, int(t * 4))])

    def streaks(self, f, seed, n=7):
        """Speed lines behind the glide."""
        rng = np.random.default_rng(seed)
        for j in range(n):
            y = int(rng.integers(36, 160))
            x0 = int(rng.integers(3, 26))
            streak(f, (x0, y), (x0 + int(rng.integers(6, 15)), y), PC[2 + j % 3])

    def screech(self, f, p, i, rings=(9, 16, 23)):
        """Sound rings from the open beak, motes thrown around."""
        x, y = self.mouth(p)
        d = self.mouth_dir(p)
        for k, r in enumerate(rings):
            r = r + 2 * (i % 2)
            ring(f, x, y, r, [PC[1], PC[2], PC[3]][k], max(-PI, d - 0.9), min(PI, d + 0.9), dotted=0 if k < 2 else 2)
        motes(f, BX, BY, 18, 52, 86, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 85 + i, big=0.2,
              arc=(-3.1, 0.3))

    # ================================================================= clips
    def idle(self, reborn=False):
        """Hover: the great wings beat, the body rises on each downstroke, the plumes wave."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            p = FPose(t=ph, reborn=reborn, dy=-3.0 * math.sin(ph), sway=ph, crest=ph, **wings(ph),
                      head=(HEAD0[0] + 1.5 * math.sin(ph - 0.8), HEAD0[1] + 2.0 * math.sin(ph - 1.3)),
                      hang=0.04 * math.sin(ph - 1.0), glow=0.62 + 0.1 * math.sin(ph), star=0.4 + 0.1 * math.sin(2 * ph),
                      eye=0.5, tail=0.3 * math.sin(ph + 0.6), talon=(0.0, 0.15 + 0.1 * math.sin(ph + 0.5)))
            f = self.draw(p)
            self.sparks(f, i, n, self.seed + 1, p=p)
            out.append(f)
        return out

    def walk(self, reborn=False):
        """Glide (= move): wings held high, a slow beat, the body stretched, the plumes streaming."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            k = 0.66 + 0.26 * math.cos(ph)
            kt = 0.62 + 0.26 * math.cos(ph - 0.7)
            p = FPose(t=ph * 1.5, reborn=reborn, tilt=0.05, dy=-1.5 * math.sin(ph), sway=ph + 1.0, crest=ph * 1.5,
                      tail=-0.25, wing=(k, kt), wing_far=(k - 0.05, kt - 0.05), spread=1.03,
                      head=(HEAD0[0] + 4 + 1.0 * math.sin(ph), HEAD0[1] + 4 + 1.5 * math.sin(ph - 1.0)), hang=0.1,
                      glow=0.66, star=0.45, eye=0.6, talon=(0.0, 0.1))
            f = self.draw(p)
            self.streaks(f, self.seed + 10 + i)
            self.sparks(f, i, n, self.seed + 2, count=6, p=p)
            out.append(f)
        return out

    def telegraph(self, reborn=False):
        """The wings rise high, the head draws back, light gathers at the beak and the wing tips."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = smooth(t)
            ph = 0.6 * i
            p = FPose(t=ph, reborn=reborn, tilt=-0.06 * e, dy=-2 * e, dx=-2 * e, sway=ph, crest=ph,
                      **held(0.7 + 0.3 * e, 0.6 + 0.4 * e), spread=1.0 + 0.04 * e,
                      head=(HEAD0[0] - 9 * e, HEAD0[1] - 7 * e), hang=-0.3 * e, beak=0.45 * e, glow=0.62 + 0.5 * t,
                      eye=0.5 + 0.8 * t, star=0.4 + 0.3 * t, talon=(0.0, 0.2 + 0.5 * t))
            f = self.draw(p)
            x, y = self.mouth(p)
            self.charge(f, x, y, i, 6, self.seed + 20 + i, r0=28, r1=5, count=6 + 2 * i)
            if i >= 2:
                for k, (tx, ty) in enumerate(self.wing_tips(p)[:4]):
                    if (k + i) % 2 == 0:
                        self.sparkle(f, tx, ty - 1, 1 + (i >= 4))
            if i >= 4:
                self.sparkle(f, x + 1, y, 3 + (i - 4) * 2)
            out.append(f)
        return out

    def attack_poses(self, reborn=False):
        keys = [  # wing arm, wing tips, head dx, dy, hang, beak, talon reach, open, dx, glow
            (1.0, 0.95, -8, -6, -0.3, 0.7, 0.2, 0.6, -2, 0.95),
            (-0.55, -0.3, 8, 5, 0.18, 1.0, 1.0, 1.0, 3, 1.1),
            (-0.75, -0.7, 6, 4, 0.12, 0.6, 0.8, 0.8, 2, 0.9),
            (-0.1, -0.35, 2, 1, 0.05, 0.25, 0.3, 0.4, 1, 0.75),
            (0.45, 0.3, 0, 0, 0.0, 0.05, 0.05, 0.2, 0, 0.65)]
        out = []
        for i, (ka, kt, hx, hy, hang, beak, rc, op, dx, g) in enumerate(keys):
            out.append(FPose(t=0.5 * i + 3, reborn=reborn, dx=dx, sway=0.4 * i, crest=0.5 * i, **held(ka, kt),
                             head=(HEAD0[0] + hx, HEAD0[1] + hy), hang=hang, beak=beak, glow=g, eye=0.4 + 0.8 * g,
                             star=0.3 + 0.5 * g, talon=(rc, op), tail=0.3 if i == 1 else 0.0))
        return out

    def attack(self, reborn=False):
        """Wing slash: the wings strike down, the talons lunge, a bolt of starfire leaves the beak
        on f1 (ORIGIN)."""
        out = []
        for i, p in enumerate(self.attack_poses(reborn)):
            f = self.draw(p)
            x, y = self.mouth(p)
            if i == 0:
                self.charge(f, x, y, 4, 6, self.seed + 39, r0=12, r1=3, count=8)
            if i == 1:
                FX.burst(f, x + 5, y, 9, [WHITE_C, PC[0], PC[1], PC[2], PC[3]], seed=self.seed + 40, jag=0.35, rays=7)
                self._slash(f, p, 1.0)
            if i == 2:
                self._slash(f, p, 0.6)
                for k in range(5):
                    a = -0.55 + 0.275 * k
                    self.sparkle(f, x + 10 + math.cos(a) * 14, y + math.sin(a) * 14, 2)
                ring(f, x + 4, y, 9, PC[1], -1.2, 1.2)
            if i == 3:
                motes(f, x + 14, y, 10, 6, 22, [WHITE_C, PC[0], PC[1]], seed=self.seed + 45)
            out.append(f)
        return out

    def _slash(self, f, p, s):
        """Crescents of light swept by the wings, under and in front of the body: a bright edge
        fading inwards (the second one fainter)."""
        cx, cy = self.T(p, (SHOULDER[0] + 6, SHOULDER[1] - 8))
        for j, (r0, a0, a1) in enumerate(((60, 0.3, 1.75), (50, 0.5, 1.6))):
            for k in range(4):
                if j == 1 and k > 2:
                    continue
                col = [WHITE_C, PC[0], PC[1], PC[2]][k] if s > 0.8 else [PC[1], PC[2], PC[3], PC[4]][k]
                if j == 1:
                    col = [PC[1], PC[2], PC[3]][k]
                ring(f, cx, cy, r0 - k, col, a0 + 0.06 * k, a1 - 0.08 * k, dotted=0 if s > 0.8 else 2)

    def telegraph_special(self, reborn=False):
        """The chest star brightens: light is drawn into it, the wings open, the beak parts."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = smooth(t)
            ph = 0.7 * i
            p = FPose(t=ph, reborn=reborn, tilt=-0.05 * e, dy=-2 * e, sway=ph, crest=ph,
                      **held(0.75 + 0.2 * e, 0.7 + 0.25 * e), spread=1.0 + 0.03 * e,
                      head=(HEAD0[0] + 2 * e, HEAD0[1] - 3 * e), hang=-0.12 * e, beak=0.2 + 0.5 * e,
                      glow=0.62 + 0.45 * t, star=0.45 + 1.0 * t, eye=0.5 + 0.7 * t, talon=(0.0, 0.3))
            f = self.draw(p)
            sx, sy = self.star_pos(p)
            self.charge(f, sx, sy, i, 6, self.seed + 50 + i, r0=34, r1=8, count=10 + 2 * i)
            if i >= 2:
                ring(f, sx, sy, 24 - 3 * i, PC[2], dotted=2)
            if i == 5:
                self.sparkle(f, sx + 1, sy, 6)
                x, y = self.mouth(p)
                self.sparkle(f, x, y, 3)
            out.append(f)
        return out

    def special_pose(self, i, reborn=False):
        return FPose(t=1.6 * i, reborn=reborn, sway=0.8 * i, crest=0.0,
                     **held(0.9 + 0.05 * (i % 2), 0.86 + 0.08 * ((i + 1) % 2)), head=(HEAD0[0] + 4, HEAD0[1] + 2),
                     hang=0.12, beak=1.0, glow=1.1, star=1.3 + 0.1 * (i % 2), eye=1.1, talon=(0.1, 0.5),
                     burst=0.9 + 0.1 * (i % 2))

    def special(self, reborn=False):
        """Starfire beam (loop): beak wide, chest star blazing, a short burst at the beak (the game
        draws the beam); the head does not move."""
        out = []
        for i in range(4):
            p = self.special_pose(i, reborn)
            f = self.draw(p)
            x, y = self.mouth(p)
            motes(f, x + 14, y, 8, 4, 12, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 60 + i, arc=(-1.2, 1.2))
            out.append(f)
        return out

    def summon(self, reborn=False):
        """Wings spread wide, it cries out; a burst of feathers of light flies from its body."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.25]
        rng = np.random.default_rng(self.seed + 70)
        angs = [TAU * (k + 0.6 * rng.random()) / 18 for k in range(18)]
        speed = [0.75 + 0.5 * rng.random() for _ in range(18)]
        out = []
        for i, h in enumerate(keys):
            p = FPose(t=0.6 * i, reborn=reborn, tilt=-0.08 * h, dy=-2 * h, sway=0.5 * i, crest=0.6 * i,
                      **held(0.75 + 0.25 * h, 0.7 + 0.3 * h), spread=1.0 + 0.04 * h,
                      head=(HEAD0[0] - 3 * h, HEAD0[1] - 6 * h), hang=-0.7 * h, beak=0.9 * h, glow=0.62 + 0.45 * h,
                      star=0.45 + 0.8 * h, eye=0.5 + 0.8 * h, talon=(0.0, 0.3 + 0.6 * h))
            f = self.draw(p)
            if h > 0.6:
                x, y = self.mouth(p)
                d = self.mouth_dir(p)
                for k, r in enumerate((10 + 3 * (i % 2), 17 + 3 * (i % 2))):
                    ring(f, x, y, r, [PC[1], PC[2]][k], d - 0.8, d + 0.8, dotted=0 if k == 0 else 2)
            cx, cy = self.star_pos(p)
            if i >= 1:
                fe = [((cx + math.cos(a) * (12 + 19 * i) * sp, cy + math.sin(a) * (12 + 19 * i) * sp * 0.8),
                       math.atan2(math.sin(a) * 0.8, math.cos(a))) for a, sp in zip(angs, speed)]
                over_safe(f, feathers_of_light(fe, 11.0 - 0.8 * max(0, i - 3), fade=max(0, i - 3)))
            if i == 1:
                FX.burst(f, cx, cy, 6, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 71, jag=0.4, rays=6)
            out.append(f)
        return out

    def roar_pose(self, h, i, reborn, **kw):
        d = dict(t=0.8 * i, reborn=reborn, tilt=-0.06 * h, sway=0.6 * i, crest=0.9 * i,
                 **held(0.7 + 0.3 * h, 0.6 + 0.4 * h), spread=1.0 + 0.04 * h,
                 head=(HEAD0[0] - 2 * h, HEAD0[1] - 7 * h), hang=-0.45 * h, beak=h, glow=0.62 + 0.5 * h,
                 star=0.4 + 0.6 * h, eye=0.5 + 0.8 * h, talon=(0.15 * h, 0.3 + 0.6 * h))
        d.update(kw)
        return FPose(**d)

    def roar(self, reborn=False):
        """Phase change: it rears, spreads its wings and screeches; the light flares."""
        keys = [(0.0, 0), (0.3, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = self.roar_pose(h, i, reborn)
            f = self.draw(p)
            if h > 0.8:
                self.screech(f, p, i)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def rebirth(self):
        """Phase 5 roar: the phoenix bursts into flames, burns to a blinding flash and is reborn
        from its ashes in form II (Renaissance)."""
        out = []
        # 0-2: the fire takes the plumage
        for i, (h, burn, sh) in enumerate(((0.6, 0.3, 0), (1.0, 0.65, 1), (1.0, 1.0, -1))):
            p = self.roar_pose(h, i, False, glow=0.9 + 0.3 * h, star=0.8 + 0.5 * h)
            f = ignite(self.draw(p), burn, self.seed + 200 + i)
            if h > 0.8:
                self.screech(f, p, i)
            f = shake(f, sh, 0)
            out.append(flashed(f, 0.3) if i == 2 else f)
        # 3: it burns away in a blaze; ashes fly
        p = self.roar_pose(1.0, 3, True, glow=1.2, star=1.4)
        f = self.draw(p)
        cx, cy = self.T(p, (BX + 10, BY - 14))
        FX.burst(f, cx, cy, 44, [WHITE_C, PC[0], PC[1], PC[2], PC[3]], seed=self.seed + 210, jag=0.3, rays=12)
        self.ashes(f, cx, cy, 40, 78, self.seed + 211, 30)
        out.append(flashed(f, 0.25))
        # 4-7: reborn: a ring of fire spreads, the ashes fall, it screeches and settles
        for i, (h, rr) in enumerate(((1.0, 42), (1.0, 60), (0.55, 0), (0.15, 0))):
            p = self.roar_pose(h, 4 + i, True, glow=0.75 + 0.45 * h, star=0.6 + 0.8 * h)
            f = self.draw(p)
            cx, cy = self.T(p, (BX + 10, BY - 14))
            if rr:
                ring(f, cx, cy, rr, PC[1 + i], dotted=0 if i == 0 else 2)
                ring(f, cx, cy, rr - 5, PC[2 + i], dotted=2)
                self.ashes(f, cx, cy, rr - 10, rr + 16, self.seed + 220 + i, 18 - 6 * i, fall=8 + 10 * i)
            if h > 0.8:
                self.screech(f, p, 4 + i)
                f = shake(f, [1, -1][i % 2], 0)
            out.append(f)
        return out

    def ashes(self, f, cx, cy, r0, r1, seed, n, fall=0.0):
        """Flakes of grey ash thrown around (cx, cy) (`fall`: how far they have dropped)."""
        rng = np.random.default_rng(seed)
        for k in range(n):
            a = rng.random() * TAU
            d = r0 + (r1 - r0) * rng.random()
            x, y = cx + math.cos(a) * d, cy + math.sin(a) * d * 0.8 + fall * (0.6 + 0.4 * rng.random())
            if 4 < x < W - 5 and 4 < y < H - 5:
                FX.chunk(f, x, y, 1 + (k % 3 == 0), ASH[1], ASH[3])

    def hurt(self, reborn=False):
        a = FPose(t=0.3, reborn=reborn, dx=-4, tilt=-0.1, **held(0.5, 0.3), fold=0.25,
                  head=(HEAD0[0] - 8, HEAD0[1] - 6), hang=-0.45, beak=0.6, glow=0.95, eye=1.0, star=0.6,
                  talon=(0.0, 0.7), tail=0.4)
        b = FPose(t=0.6, reborn=reborn, dx=-2, tilt=-0.04, **held(0.62, 0.45), fold=0.1,
                  head=(HEAD0[0] - 3, HEAD0[1] - 2), hang=-0.15, beak=0.2, glow=0.7, eye=0.6, star=0.4,
                  talon=(0.0, 0.4), tail=0.2)
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        cx, cy = self.star_pos(b)
        rng = np.random.default_rng(self.seed + 110)
        fe = [((cx - 50 + 50 * rng.random(), cy - 50 + 30 * rng.random()), 1.2 + 0.8 * rng.random()) for _ in range(4)]
        over_safe(fb, feathers_of_light(fe, 8.0, fade=1))  # loose feathers drifting down
        motes(fb, BX, BY - 4, 10, 40, 70, [WHITE_C, PC[0], PC[1]], seed=self.seed + 111)
        return [fa, fb]

    def death(self, reborn=False):
        out = []
        # 0-2: convulsions, the light flares and gutters
        conv = [FPose(t=0.2, reborn=reborn, tilt=-0.1, **held(1.0, 0.9), head=(HEAD0[0] - 8, HEAD0[1] - 12),
                      hang=-0.9, beak=0.9, glow=1.2, star=1.2, eye=1.3, talon=(0.0, 0.9), tail=0.5),
                FPose(t=0.9, reborn=reborn, tilt=0.08, dy=4, **held(0.1, -0.2), head=(HEAD0[0] + 4, HEAD0[1] + 6),
                      hang=0.35, beak=0.3, glow=0.45, star=0.3, eye=0.4, talon=(0.0, 0.2)),
                FPose(t=1.6, reborn=reborn, tilt=-0.04, dy=6, **held(0.95, 0.8), head=(HEAD0[0] - 6, HEAD0[1] - 10),
                      hang=-0.7, beak=0.9, glow=1.0, star=1.4, eye=1.0, talon=(0.0, 0.8), tail=0.3)]
        for i, p in enumerate(conv):
            f = self.draw(p)
            if i == 2:
                cx, cy = self.star_pos(p)
                FX.burst(f, cx, cy, 7, [WHITE_C, PC[0], PC[1]], seed=self.seed + 118, jag=0.45, rays=6)
            f = shake(f, [1, -1, 2][i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        # 3-6: it falls, the wings flung up, the light dying, the plumage turning to ash
        fall = [(0.16, 9, 0.5, 1, 0.12), (0.24, 14, 0.36, 2, 0.3), (0.32, 18, 0.24, 3, 0.5), (0.38, 21, 0.14, 4, 0.7)]
        last = None
        for i, (tl, dy, g, dim, ash) in enumerate(fall):
            p = FPose(t=2 + i, reborn=reborn, tilt=tl, dy=dy, **held(0.95, 0.85 - 0.12 * i), fold=0.2 + 0.08 * i,
                      head=(HEAD0[0] - 2 * i, HEAD0[1] + 6 + 5 * i), hang=0.4 + 0.2 * i, beak=0.35 - 0.08 * i,
                      glow=g, eye=g, star=g, sway=1.0 + 0.4 * i, tail=-0.4, talon=(0.0, 0.1))
            f = ashen(dimmed(self.draw(p), dim), ash, self.seed + 130 + i)
            self.sparks(f, i, 4, self.seed + 135, count=8 - i, p=p)
            out.append(f)
            last = p
        # 7-12: it collapses into a falling shower of embers and ash; the heap builds up
        base = ashen(dimmed(self.draw(last), 3), 1.0, self.seed + 139)
        shower = Shower(base, self.seed + 141)
        for k, t in enumerate((0.1, 0.26, 0.42, 0.58, 0.74, 0.9)):
            f = shower.frame(t)
            FX.under(f, ash_heap(min(1.0, 0.15 + 0.85 * t), self.seed + 150))
            out.append(f)
        # 13: the heap of ashes, two last embers, a few flakes still drifting down
        fin = ash_heap(1.0, self.seed + 150)
        put_if_empty(fin, 121, 178, PC[3])
        put_if_empty(fin, 136, 180, PC[4])
        rng = np.random.default_rng(self.seed + 151)
        for k in range(6):
            put_if_empty(fin, 104 + rng.random() * 48, 140 + rng.random() * 30, ASH[1 + k % 3])
        out.append(fin)
        return out

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
        ]


# ------------------------------------------------------------------ frame helpers
def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


def over_safe(f, src, margin=2):
    """Paste the opaque pixels of `src` over `f`, `margin` px away from the frame edges."""
    sel = src[:, :, 3] > 0
    sel[:margin] = sel[-margin:] = False
    sel[:, :margin] = sel[:, -margin:] = False
    f[sel] = src[sel]
    return f


def feathers_of_light(items, ln, fade=0):
    """Small feathers made of light: items = [((tip x, tip y), angle)], a blade `ln` px long, the tip
    white-hot, the quill fading (`fade` tones dimmer)."""
    cv = TCanvas(W, H)
    for (tip, a) in items:
        root = (tip[0] - math.cos(a) * ln, tip[1] - math.sin(a) * ln)
        out, _ = blade(root, tip, 2.2, bow=0.08, n=6, root_w=0.35)
        mask = poly_mask(cv, out)
        if not mask.any():
            continue
        ys, xs = np.nonzero(mask)
        s = np.clip(((xs + 0.5 - root[0]) * math.cos(a) + (ys + 0.5 - root[1]) * math.sin(a)) / ln, 0, 1)
        it = np.zeros((H, W), np.float32)
        it[ys, xs] = 0.22 + 0.8 * s - 0.2 * fade
        cv.part(line=False)
        cv.put(mask, it, CORE)
    return cv.render(outline=False)


LIGHT_RGB = np.array([c[:3] for c in [WHITE_C, *PC]], np.int16)


def light_mask(img):
    """The pixels painted in one of the light tones (white, P0..P4)."""
    rgb = img[:, :, :3].astype(np.int16)
    m = np.zeros(img.shape[:2], bool)
    for c in LIGHT_RGB:
        m |= np.all(rgb == c, axis=2)
    return m & (img[:, :, 3] > 0)


def plumage_mask(img):
    """Opaque pixels that are neither light nor outline (the dark plumage, beak, talons, stars)."""
    a = img[:, :, 3] > 0
    out = np.all(img[:, :, :3] == np.array(OUTLINE[:3], np.uint8), axis=2)
    return a & ~out & ~light_mask(img)


def dimmed(f, steps):
    """Every light pixel `steps` tones further down the ladder white, P0..P4, ash (exact colours)."""
    out = np.asarray(f).copy()
    if steps <= 0:
        return out
    rgb = out[:, :, :3]
    src = [np.all(rgb == np.array(c[:3], np.uint8), axis=2) & (out[:, :, 3] > 0) for c in LADDER]
    for i, sel in enumerate(src):
        j = min(len(LADDER) - 1, i + steps)
        out[sel, :3] = LADDER[j][:3]
    return out


def ashen(f, t, seed):
    """The dark plumage turns to grey ash (a share `t` of its pixels, keeping their shading)."""
    out = np.asarray(f).copy()
    if t <= 0:
        return out
    m = plumage_mask(out)
    h, w = m.shape
    ys, xs = np.mgrid[0:h, 0:w]
    n = 0.65 * _hash(xs // 4, ys // 4, seed % 97) + 0.35 * _hash(xs, ys, seed % 89)
    sel = m & (n < t * 1.05)
    lum = out[:, :, :3].astype(np.float32) @ np.array([0.3, 0.5, 0.2], np.float32)
    idx = np.clip(((150.0 - lum) / 30.0).astype(np.int32), 0, len(ASH) - 1)
    for i, c in enumerate(ASH):
        out[sel & (idx == i)] = c
    return out


def ignite(f, t, seed):
    """Flames take the plumage: the dark pixels turn to fire from the bottom up (with noise), tongues
    of fire rise above the silhouette.  Only light tones: recoloured with the phase."""
    out = np.asarray(f).copy()
    a = out[:, :, 3] > 0
    if not a.any():
        return out
    m = plumage_mask(out)
    h, w = m.shape
    ys, xs = np.mgrid[0:h, 0:w]
    rows = np.nonzero(a.any(axis=1))[0]
    y0, y1 = rows[0], rows[-1]
    hgt = (y1 - ys) / max(1, y1 - y0)
    n = _hash(xs // 2, ys // 2, seed % 89)
    burn = m & (0.55 * n + 0.45 * hgt < t * 1.05)
    lum = out[:, :, :3].astype(np.float32) @ np.array([0.3, 0.5, 0.2], np.float32)
    tone = np.where(lum > 80, 2, np.where(lum > 50, 3, 4))
    hot = burn & (_hash(xs, ys, seed % 83 + 0.5) > 0.82)
    tone = np.where(hot, 1, tone)
    for k in (1, 2, 3, 4):
        out[burn & (tone == k)] = PC[k]
    # tongues of fire over the silhouette
    top = np.where(a.any(axis=0), np.argmax(a, axis=0), -1)
    for x in range(2, w - 2):
        if top[x] < 0:
            continue
        pk = abs(math.sin(x * 0.36 + seed)) ** 3
        hh = t * (2.0 + 10.0 * pk * (0.65 + 0.35 * math.sin(x * 0.11 + 1.3 * seed)))
        for k in range(int(hh)):
            y = top[x] - 1 - k
            if y < 3:
                break
            if out[y, x, 3] == 0:
                fr = k / max(1.0, hh)
                out[y, x] = PC[1] if fr < 0.3 else (PC[2] if fr < 0.65 else PC[3])
    return out


class Shower:
    """The collapse: the ashen body comes apart into a falling shower of embers and ash flakes, the
    high parts first.  The edge about to fall smoulders; the embers (the light pixels and some of the
    ash) glow as they drop and die down the ladder; what reaches the ground joins the heap."""

    def __init__(self, base, seed):
        img = np.asarray(base).copy()
        out_px = np.all(img[:, :, :3] == np.array(OUTLINE[:3], np.uint8), axis=2)
        alpha = img[:, :, 3] > 0
        self.img = img
        self.outline = out_px & alpha
        ys, xs = np.nonzero(alpha & ~out_px)
        self.ys, self.xs = ys, xs
        rng = np.random.default_rng(seed)
        y0, y1 = ys.min(), ys.max()
        hn = (ys - y0) / max(1, y1 - y0)
        self.delay = 0.04 + 0.42 * rng.random(len(ys)) + 0.4 * hn
        self.vx = rng.normal(0.0, 8.0, len(ys))
        self.g = 380.0 + 160.0 * rng.random(len(ys))
        self.ph = rng.random(len(ys)) * TAU
        self.part = rng.random(len(ys)) < 0.5  # the others crumble to nothing
        self.ember = light_mask(img)[ys, xs] | (rng.random(len(ys)) < 0.16)
        self.hh = rng.random(len(ys))

    def frame(self, t):
        out = np.zeros_like(self.img)
        age = t - self.delay
        stay = age <= 0
        ys, xs = self.ys, self.xs
        m = np.zeros(self.img.shape[:2], bool)
        m[ys[stay], xs[stay]] = True
        out[self.outline & _dilate(m)] = OUTLINE
        out[ys[stay], xs[stay]] = self.img[ys[stay], xs[stay]]
        edge = stay & (age > -0.07)  # smouldering where it is about to fall
        out[ys[edge], xs[edge]] = np.where((self.hh[edge] > 0.5)[:, None], np.array(PC[2], np.uint8),
                                           np.array(PC[3], np.uint8))
        fall = (~stay) & self.part
        a = age[fall]
        fy = ys[fall] + self.g[fall] * a * a + 12 * a
        fx = xs[fall] + self.vx[fall] * a + 1.6 * np.sin(a * 9 + self.ph[fall])
        ok = (fy < GROUND - 2) & (fx >= 3) & (fx < W - 3) & (fy >= 3)
        emb = self.ember[fall]
        for k in np.nonzero(ok)[0]:
            X, Y = int(fx[k]), int(fy[k])
            if out[Y, X, 3]:
                continue
            if emb[k]:
                out[Y, X] = LADDER[min(len(LADDER) - 1, 2 + int(a[k] * 12))]
            else:
                out[Y, X] = ASH[1 + min(3, int(a[k] * 7))]
        return out


def ash_heap(t, seed):
    """The low heap of ashes where the phoenix fell (t: 0 a thin layer .. 1 settled), a few embers
    still glowing in it."""
    cv = TCanvas(W, H)
    piles = [(128, 182, 26, 6.0), (110, 183, 12, 4.0), (147, 183, 13, 4.2), (121, 178, 9, 4.0), (138, 179, 10, 4.2),
             (96, 185, 6, 2.4), (160, 185, 7, 2.6)]
    for i, (x, y, rx, ry) in enumerate(piles):
        cv.part()
        s = max(0.25, t)
        cv.ellipse((x, GROUND + 0.5 - ry * s), rx * (0.5 + 0.5 * t), ry * s, ASH_MAT, ang=0.08 * (i % 3 - 1))
    img = cv.render()
    rng = np.random.default_rng(seed)
    for k in range(int(3 + 5 * t)):  # embers in the ash
        x, y = 112 + rng.random() * 32, GROUND - 1 - rng.random() * 6 * t
        if img[int(y), int(x), 3] and not np.all(img[int(y), int(x), :3] == np.array(OUTLINE[:3], np.uint8)):
            img[int(y), int(x)] = PC[2 + (k % 3)]
    return img


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


BOSS = Phenix()


def phase_clips(k, form1, form2, reborn):
    clips = form1 if k < 4 else (reborn if k == 4 else form2)
    return [(name, [finish(f, k) for f in frames], kw) for name, frames, kw in clips]


def phase_sheets():
    """(phase index 0..7, image, clip meta) of the eight phase sheets."""
    form1 = BOSS.clips(False)
    form2 = BOSS.clips(True)
    reborn = [c if c[0] != "roar" else ("roar", BOSS.rebirth(), c[2]) for c in form2]
    out = []
    for k in range(8):
        img, meta = pack(phase_clips(k, form1, form2, reborn))
        out.append((k, img, meta))
    return out


def build():
    from sheet import save_json, save_png
    for k, img, meta in phase_sheets():
        name = "phenix" if k == 0 else "phenix_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
