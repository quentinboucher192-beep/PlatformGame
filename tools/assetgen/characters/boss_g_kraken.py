"""Le Kraken des Abysses (giga boss, eight phases).

An abyssal kraken: a huge bulbous mantle of deep-sea purple skin (teal where the light catches it,
covered in small papillae) leaning back over a head with two large eyes (the far one peeks over
the brow; slit pupils under a heavy frowning fold) and a parrot-like hooked beak under the head;
eight thick arms (four far ones behind the body, four near ones in front) that curl, undulate and
lash, a row of suckers with glowing centres on their inner side and a dotted line of photophores
along their back.  Seen from the side, facing right (the game flips it); it floats and swims (the
engine's flying movement), the arms trailing below and behind.  The body is one soft blob (a smooth
union of ellipsoids shaded from its height field); the arms are tapered tubes along curves given by
stances (REST, SWIM, REAR, CLENCH, SPREAD, LIMP) plus a travelling undulation.

Two forms:
  * form I  (phases 1-4): whole - bands of light arching across the mantle, columns of spots;
  * form II (phases 5-8): « Blessé » - the mantle is torn open on its glowing inner core (scars of
    light run from the tear), one near arm is severed (a glowing stump dripping light), more and
    brighter spots.
The bioluminescence (bands, spots, eyes, suckers, photophores, the core, the jet) changes colour at
every phase; the skin, the beak, the ink and the bubbles never do.  Every emissive pixel uses one of
five tones P0..P4 of the phase ramp (plus pure white), so each phase sheet is the phase-1 sheet with
those five colours swapped (exact LUT, `recolour`), see PHASES.

Frames 256x224, pivot [128, 196] = bottom-centre of the 104x168 collision box (x 76..180,
y 28..196) around the mantle, the head and the arms down to their usual tips (2.53: the pivot was
at the crown of the arms, y 136, and the arms hung ~70 px under the box: landed, they went through
the floor and under the HUD; the art is unchanged, only the anchor and the box).  Drawn at scale
1.25 in game (320x280 px frame, 130x210 px box).
ORIGIN (frame pixels from the pivot, x forward, y down; x1.25 in game):
  * attack frame 1 (`fire`, the lash: the front arm uncoiled forward, a bolt of light leaves its
    tip): (+109, -69) = frame pixel (237, 127);
  * special (the jet loop: the head and the beak do not move on its four frames): (+48, -71)
    = frame pixel (176, 125), between the open mandibles; the beam itself is drawn by the game,
    the sheet only shows a short burst of light and ink at the beak.
`Kraken.tip(pose)` and `Kraken.beak(pose)` give these points (see `attack_poses` and
`special_pose`).
Clips (packed several per row, `start` = first column): idle 8 (bobbing, breathing, the arms
undulating, loop), walk 8 (= move, same frames: swimming, the mantle squeezes and the arms sweep
back, loop), telegraph 6 (the front arms rear up, hooked), attack 5 (lash, fire f1),
telegraph_special 6 (inhales: the mantle swells, the light gathers in the eyes and the beak),
special 4 (loop, the jet), hurt 2, summon 6 (the arms spread, a pulse of light, two whirlpools),
roar 8 (phase change; the phase-5 sheet's roar is the mantle tearing open and the arm severed),
death 14 (convulses, sinks while its light fades, dissolves into ink and bubbles).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, mat, motes, ring, streak
from boss_c_common import TAU, TIMING, Pose, shake
from raster import LIGHT3, TCanvas

PI = math.pi
W, H = 256, 224
PIVOT = [128, 196]
BOX = (76, 28, 180, 196)  # x0, y0, x1, y1 (collision box, frame pixels)
BX, BY = 128.0, 100.0  # rotation centre (body coordinates): the whole kraken tilts around it
WHITE = "#ffffff"
SEED = 61873

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Abysses", "cyan", ["#dcfaff", "#7ae6ff", "#22b8ee", "#1670a8", "#0c3866"]),
    ("Marée", "turquoise", ["#dcfff4", "#74f6d6", "#1ecfaa", "#127e72", "#0a3e40"]),
    ("Encre", "indigo", ["#e6e0ff", "#a69aff", "#6a58f0", "#3e30b0", "#1e1660"]),
    ("Foudre marine", "jaune électrique", ["#ffffd8", "#fff060", "#f0c814", "#a48010", "#54400a"]),
    ("Brisé", "rouge corail", ["#ffe0d8", "#ff8a70", "#f04a3a", "#a42430", "#541220"]),
    ("Nacre", "rose nacré", ["#fff0f6", "#ffbede", "#f080b8", "#a8487e", "#562444"]),
    ("Tourbillon", "émeraude", ["#dcffe4", "#6af59a", "#1ec864", "#127a44", "#0a3c26"]),
    ("Léviathan", "blanc incandescent", ["#fffef6", "#f6f8ff", "#c8d4f0", "#7e8cb8", "#343c60"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Skin (never recoloured): deep-sea purple, teal where the light catches it.
SKIN = mat("kr_skin", ["#8fc0c4", "#5f8aa8", "#4f5e96", "#43407e", "#33285f", "#1f1640"],
           th=[0.9, 0.77, 0.62, 0.46, 0.28])
SKIN_FAR = mat("kr_skin_far", ["#4f6d8c", "#41447a", "#332b62", "#241d48", "#171230"], th=[0.86, 0.66, 0.46, 0.27])
BELLY = mat("kr_belly", ["#c8bedb", "#9d93c2", "#7a6fa5", "#5b4f85", "#3f345e"], th=[0.86, 0.68, 0.48, 0.28])
BELLY_FAR = mat("kr_belly_far", ["#776e9e", "#5c5283", "#443a67", "#2d2547"], th=[0.78, 0.56, 0.32])
RIM = mat("kr_rim", ["#e6e0f2", "#bab1d6", "#8b81b2", "#5f5587"], th=[0.78, 0.56, 0.34])
RIM_FAR = mat("kr_rim_far", ["#9b93bb", "#756b99", "#524877"], th=[0.66, 0.38])
BEAK = mat("kr_beak", ["#e2d6de", "#8e7894", "#45324c", "#25182d", "#130b18"], th=[0.84, 0.64, 0.42, 0.22])
MAW = mat("kr_maw", ["#3c1c3e", "#251028", "#150816"], th=[0.6, 0.3])
DEAD = mat("kr_dead", ["#6a6584", "#4c4864", "#34304a"], th=[0.62, 0.36])  # extinguished photophores
# Light (recoloured per phase).
HOT = mat("kr_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("kr_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
# Ink, bubbles and torn skin (never recoloured).
INK = [C("#17132a"), C("#241f40"), C("#352f5a"), C("#4a4274")]  # dense core -> thin rim
BUBBLE = [C("#d6e6f6"), C("#8ea8c8"), C("#5d7598")]
FLESH_L, FLESH_D = C("#5f8aa8"), C("#33285f")

# ------------------------------------------------------------------ the rig (body coordinates)
MC = (114.0, 64.0)  # centre of the mantle
MANG = -2.2  # direction of the mantle's axis, neck -> apex (up and back)
MA, MB = 50.0, 40.0  # half length, half width of the mantle
EU = (math.cos(MANG), math.sin(MANG))  # along the mantle, towards the apex
EV = (-math.sin(MANG), math.cos(MANG))  # across, towards the front-top side
NECK = (134.0, 92.0, 28.0, 24.0, -0.6, 26.0)  # (centre x, y, half axes, angle, height) of the body blobs
HEAD = (148.0, 104.0, 26.0, 21.0, 0.1, 24.0)
CROWN = (146.0, 121.0, 31.0, 11.0, 0.05, 12.0)
EYE = (151.0, 96.0)
FAR_EYE = (158.0, 82.0)
BEAK_O, BEAK_A = (162.0, 117.0), 0.14  # beak origin and direction (forward, a little down)
# Parrot-like mandibles (beak-local: x forward, y down): a thick ridge arching to a hook that bends
# down and back; the lower mandible closes under it.
UPPER_BEAK = [(-6, -4.5), (0, -7.5), (8, -8.5), (15, -7), (20.5, -3.5), (24.5, 1.5), (26.5, 7), (26, 12.5),
              (23.5, 7.5), (20, 4.2), (14, 2.6), (5, 2), (-6, 2)]
LOWER_BEAK = [(-4, 2.6), (5, 3.2), (12, 4.4), (17.5, 6.6), (16, 9.6), (10, 11), (3, 10.6), (-4, 7.5)]
BEAK_HINGE = (-2.0, 2.0)
# The eight arms: far side (drawn behind the body) then near side, rear to front.
# (far, base, base radius, length)
ARMS = [(True, (124.0, 117.0), 6.6, 80.0), (True, (138.0, 121.0), 6.8, 78.0), (True, (154.0, 123.0), 6.8, 74.0),
        (True, (168.0, 120.0), 6.4, 68.0),
        (False, (122.0, 123.0), 8.6, 92.0), (False, (136.0, 129.0), 9.0, 90.0), (False, (152.0, 131.0), 9.0, 84.0),
        (False, (166.0, 128.0), 8.6, 80.0)]
LASH = 7  # the striking arm
CUT = 5  # the severed arm of form II
CUT_AT = 0.44  # where it is cut (fraction of its length)
ARM_PH = [0.0, 2.1, 4.0, 1.2, 0.7, 3.0, 5.0, 1.9]  # phase of each arm's undulation
LAM = 0.85  # wavelength of the undulation (fraction of the arm)

# Stances: per arm (a0 = direction at the base (0 = forward, pi/2 = down), curl = total turn along
# the arm (+ = clockwise on screen), lf = length factor, pw = where the curl sits (high = the tip)).
REST = [(2.3, 1.3, 1.0, 2.0), (1.75, -1.6, 0.95, 2.0), (1.35, 1.9, 0.95, 2.0), (0.85, -2.2, 1.0, 2.0),
        (2.5, 1.7, 1.0, 2.0), (2.0, 1.1, 1.0, 1.8), (1.45, -1.3, 1.0, 1.8), (1.0, -2.6, 1.0, 2.0)]
SWIM = [(2.8, 0.25, 1.0, 1.5), (2.6, -0.3, 1.0, 1.5), (2.45, 0.35, 1.0, 1.5), (2.3, -0.4, 1.0, 1.5),
        (2.9, 0.2, 1.0, 1.5), (2.7, 0.3, 1.0, 1.5), (2.5, -0.3, 1.0, 1.5), (2.3, -0.5, 1.0, 1.5)]
REAR = [(2.5, 0.8, 1.0, 1.8), (2.1, 0.5, 1.0, 1.8), (0.5, -1.4, 1.0, 1.6), (-0.25, -1.9, 1.0, 1.6),
        (2.7, 0.9, 1.0, 1.8), (2.3, 0.6, 1.0, 1.8), (0.15, -1.6, 1.0, 1.6), (-0.55, -1.5, 1.0, 1.6)]
CLENCH = [(1.9, 2.6, 1.0, 1.3), (1.6, -2.6, 1.0, 1.3), (1.4, 2.8, 1.0, 1.3), (1.1, -2.8, 1.0, 1.3),
          (2.1, 2.7, 1.0, 1.3), (1.75, 2.4, 1.0, 1.3), (1.45, -2.5, 1.0, 1.3), (1.15, -2.9, 1.0, 1.3)]
SPREAD = [(2.75, 0.9, 1.0, 1.8), (2.05, 0.6, 1.0, 1.8), (1.3, -0.6, 1.0, 1.8), (0.45, -0.9, 1.0, 1.8),
          (2.95, 1.0, 1.0, 1.8), (2.3, 0.7, 1.0, 1.8), (1.55, -0.7, 1.0, 1.8), (0.7, -1.1, 1.0, 1.8)]
LIMP = [(1.9, 3.0, 1.0, 1.1), (1.5, -3.2, 1.0, 1.1), (1.3, 3.4, 1.0, 1.1), (1.0, -3.6, 1.0, 1.1),
        (2.2, 3.2, 1.0, 1.1), (1.75, 3.0, 1.0, 1.1), (1.45, -3.3, 1.0, 1.1), (1.1, -3.5, 1.0, 1.1)]
SIDE = [1 if c > 0 else -1 for (_, c, _, _) in REST]  # the suckers line the inner side of the curl

# Bands of light across the mantle: (u on the axis, bow towards the neck, |v| reach, half width px).
STRIPES = [(0.62, 0.32, 0.66, 1.4), (0.22, 0.3, 0.88, 1.7), (-0.2, 0.26, 0.86, 1.5)]
# The tear of form II: centre (mantle u, v), half length and half width (px).
TEAR = (0.22, -0.2, 14.0, 11.5)


def blend(a, b, t):
    return [tuple(x + (y - x) * t for x, y in zip(u, v)) for u, v in zip(a, b)]


def _hash(a, b, salt=0.0):
    x = np.sin(np.asarray(a, np.float64) * 12.9898 + np.asarray(b, np.float64) * 78.233 + salt * 37.719) * 43758.5453
    return x - np.floor(x)


def _smax(a, b, k):
    hh = np.clip(k - np.abs(a - b), 0, None) / k
    return np.maximum(a, b) + hh * hh * k * 0.25


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


def light(nx, ny, nz):
    return 0.5 + 0.5 * (nx * LIGHT3[0] + ny * LIGHT3[1] + nz * LIGHT3[2])


def _spots(broken):
    """Photophores in columns between the bands of light (u, v, radius px)."""
    rng = np.random.default_rng(SEED + (7 if broken else 3))
    pts = []
    cols = [(0.84, 3, 1.5), (0.42, 6, 2.2), (0.01, 7, 2.3), (-0.42, 6, 1.9), (-0.72, 4, 1.4)]
    if broken:
        cols += [(0.62, 5, 1.2), (0.22, 7, 1.3), (-0.2, 7, 1.3)]
    for j, (uc, n, r) in enumerate(cols):
        vm = 0.9 * math.sqrt(max(0.0, 1 - uc * uc))
        for k in range(n):
            v = -vm + 2 * vm * (k + 0.5) / n + rng.uniform(-0.05, 0.05)
            u = uc - 0.3 * v * v + rng.uniform(-0.04, 0.04)
            if u * u + v * v > 0.84:
                continue
            rr = r * (0.75 + 0.5 * rng.random()) * (1.12 if broken else 1.0)
            pts.append((u, v, rr))
    return pts


SPOTS = (_spots(False), _spots(True))


def _tear_cracks():
    """Scars of light running from the tear (polylines, px from its centre, along / across the mantle)."""
    rng = np.random.default_rng(SEED + 11)
    out = []
    for ang, ln in ((0.3, 26), (1.4, 20), (2.5, 24), (3.6, 18), (4.6, 22), (5.5, 16)):
        x, y = math.cos(ang) * TEAR[2] * 0.95, math.sin(ang) * TEAR[3] * 0.95
        pts = [(x, y)]
        a = ang
        for k in range(int(ln / 3)):
            a += rng.uniform(-0.45, 0.45)
            x += math.cos(a) * 3
            y += math.sin(a) * 3
            pts.append((x, y))
        out.append(pts)
    return out


TEAR_CRACKS = _tear_cracks()


class KPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, tilt=0.0, t=0.0, arms=None, wave=0.3, breath=0.0, glow=0.6, eye=0.5,
                    lid=0.25, brow=0.0, jaw=0.0, mouth=0.0, broken=False, crack=0.0, dead=0.0, jet=0.0,
                    piece=None)
    # arms: 8 (a0, curl, lf, pw) (default REST); t: phase of the undulation; wave: its amplitude;
    # breath: mantle swelling (-1 squeezed .. 1 swollen); glow: light of the spots, bands and suckers;
    # eye: eye glow; lid: upper eyelid (0 open .. 1 shut); brow: frown; jaw: beak opening; mouth: light
    # in the beak; crack: the mantle splitting (phase-5 roar); dead: the light going out (death, 1 = out);
    # jet: the burst of light at the beak (special); piece: (dx, dy, turn) of the severed arm flying off.


class Kraken:
    id = "kraken"
    W, H, PIVOT = W, H, PIVOT
    seed = SEED
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 8, 10, 12
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
    def mscale(p):
        return 1.0 + 0.06 * p.breath

    def mantle_pt(self, p, u, v):
        """Body coordinates of the mantle point (u along the axis, v across, both -1..1)."""
        b = self.mscale(p)
        return (MC[0] + u * MA * b * EU[0] + v * MB * b * EV[0], MC[1] + u * MA * b * EU[1] + v * MB * b * EV[1])

    def arm_geom(self, p, i, full=False):
        """Body-coordinate polyline and radii of arm i (cut at the stump for the severed arm in form II)."""
        far, base, r0, ln = ARMS[i]
        a0, curl, lf, pw = (p.arms or REST)[i]
        L = ln * lf
        n = max(14, int(L / 1.5))
        ph = p.t + ARM_PH[i]
        x, y = base
        pts = [(x, y)]
        for k in range(n):
            s = (k + 0.5) / n
            a = a0 + curl * s ** pw + p.wave * s * math.sin(TAU * s / LAM - ph)
            x += math.cos(a) * L / n
            y += math.sin(a) * L / n
            pts.append((x, y))
        radii = [r0 * (1.0 - 0.9 * (k / n) ** 0.8) + 0.55 for k in range(n + 1)]
        if p.broken and i == CUT and not full:
            m = int(round(CUT_AT * n))
            return pts[:m + 1], radii[:m + 1]
        return pts, radii

    def tip(self, p, i=LASH):
        """The tip of arm i (frame pixels): the projectile origin of the attack (the lash, f1)."""
        pts, _ = self.arm_geom(p, i)
        return self.T(p, pts[-1])

    def beak_fns(self, p):
        """BL: beak-local -> body coordinates; JL: the same for the lower mandible (opened by p.jaw)."""
        ca, sa = math.cos(BEAK_A), math.sin(BEAK_A)

        def BL(x, y):
            return (BEAK_O[0] + x * ca - y * sa, BEAK_O[1] + x * sa + y * ca)

        ja = p.jaw * 0.62
        jc, js = math.cos(ja), math.sin(ja)

        def JL(x, y):
            dx, dy = x - BEAK_HINGE[0], y - BEAK_HINGE[1]
            return BL(BEAK_HINGE[0] + dx * jc - dy * js, BEAK_HINGE[1] + dx * js + dy * jc)

        return BL, JL

    def beak_body(self, p):
        BL, JL = self.beak_fns(p)
        a, b = BL(19.0, 5.0), JL(16.0, 7.0)
        return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)

    def beak_dir(self, p):
        return BEAK_A + p.tilt + 0.25 * p.jaw

    def beak(self, p):
        """The projectile origin of the special (frame pixels): between the open mandibles."""
        return self.T(p, self.beak_body(p))

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = self.canvas(p)
        for i in range(4):
            self._arm(cv, p, i)
        self._eye(cv, p, far=True)
        self._body(cv, p)
        self._eye(cv, p, far=False)
        for i in range(4, 8):
            self._arm(cv, p, i)
        if p.piece is not None:
            self._piece(cv, p)
        self._beak(cv, p)
        if p.jet > 0.02:
            self._jet(cv, p)
        return cv.render()

    def g(self, p):
        """Brightness of the bioluminescence (form II glows brighter)."""
        return p.glow + (0.15 if p.broken else 0.0)

    def _light(self, cv, p, m, inten, decal=True, core=False):
        """Light pixels: the phase light while alive, dimming with p.dead, grey once it is out."""
        if p.dead >= 1.0:
            cv.put(m, 0.5, DEAD, decal=decal)
        else:
            cv.put(m, inten * (1.0 - 0.75 * p.dead), CORE if core else HOT, decal=decal)

    # ------------------------------------------------------------ arms
    def _arm(self, cv, p, i):
        pts, radii = self.arm_geom(p, i)
        sp = [cv.T(q) for q in pts]
        self._tube(cv, p, sp, radii, SIDE[i], ARMS[i][0], salt=i, stump=(p.broken and i == CUT))

    def _tube(self, cv, p, pts, radii, side, far, salt=0, stump=False):
        """A tapered arm along the screen polyline `pts`: round shading, the pale sucker side, a row of
        suckers with glowing centres, photophores along its back; `stump` ("start" or True for the end):
        a glowing wound."""
        P = np.asarray(pts, np.float32)
        R = np.asarray(radii, np.float32)
        seg = np.hypot(*(P[1:] - P[:-1]).T)
        cum = np.concatenate([[0.0], np.cumsum(seg)])
        total = float(cum[-1])
        ns = max(8, int(total / 0.7))
        s = np.linspace(0, total, ns).astype(np.float32)
        cx, cy, cr = np.interp(s, cum, P[:, 0]), np.interp(s, cum, P[:, 1]), np.interp(s, cum, R)
        tx, ty = np.gradient(cx), np.gradient(cy)
        tl = np.hypot(tx, ty) + 1e-6
        tx, ty = tx / tl, ty / tl
        nx, ny = -ty, tx
        x0 = int(max(0, math.floor((cx - cr).min()) - 2))
        x1 = int(min(W, math.ceil((cx + cr).max()) + 3))
        y0 = int(max(0, math.floor((cy - cr).min()) - 2))
        y1 = int(min(H, math.ceil((cy + cr).max()) + 3))
        if x0 >= x1 or y0 >= y1:
            return
        shp = (y1 - y0, x1 - x0)
        qx = cv.px[y0:y1, x0:x1].ravel()
        qy = cv.py[y0:y1, x0:x1].ravel()
        d = np.hypot(qx[:, None] - cx[None, :], qy[:, None] - cy[None, :]) - cr[None, :]
        j = np.argmin(d, axis=1)
        inside = d[np.arange(len(qx)), j] <= 0
        idx = np.nonzero(inside)[0]
        jj = j[inside]
        ex, ey = qx[inside] - cx[jj], qy[inside] - cy[jj]
        vn = np.clip((ex * nx[jj] + ey * ny[jj]) / np.maximum(cr[jj], 0.7), -1, 1)
        un = s[jj]
        nz = np.sqrt(np.clip(1 - vn * vn, 0, 1))
        inten = light(nx[jj] * vn, ny[jj] * vn, nz)
        belly = vn * side > 0.3
        inten = np.where(belly, inten + 0.04, inten)
        # faint pigment bands across the back of the arm
        inten = np.where(~belly & (np.mod(un + salt * 3.7, 11.0) < 1.2) & (cr[jj] > 2.5), inten - 0.08, inten)

        def full(sel, val=None):
            m = np.zeros((H, W), bool)
            v = np.zeros((H, W), np.float32)
            sm = np.zeros(len(qx), bool)
            sm[idx[sel]] = True
            m[y0:y1, x0:x1] = sm.reshape(shp)
            if val is not None:
                sv = np.zeros(len(qx), np.float32)
                sv[idx[sel]] = val[sel] if np.ndim(val) else val
                v[y0:y1, x0:x1] = sv.reshape(shp)
            return m, v

        allm, it = full(np.ones(len(jj), bool), inten)
        bm, _ = full(belly)
        cv.part()
        cv.put(allm & ~bm, it, SKIN_FAR if far else SKIN)
        cv.put(bm, it, BELLY_FAR if far else BELLY)
        g = self.g(p) * (0.75 if far else 1.0)
        # photophores: a dotted line of light along the back of the arm
        if not far:
            dot = (np.abs(vn * side + 0.42) < 0.2) & (np.mod(un + salt * 5.3, 9.0) < 1.6) & (cr[jj] > 2.4) & (un > 6)
            dm, ds = full(dot, 0.3 + 0.5 * g * (0.7 + 0.3 * np.sin(un * 0.4 - p.t * 2 + salt)))
            cv.part(line=False)
            self._light(cv, p, dm, ds)
        # suckers on the inner side, glowing centres
        rim_m = np.zeros((H, W), bool)
        rim_i = np.zeros((H, W), np.float32)
        glo_m = np.zeros((H, W), bool)
        glo_i = np.zeros((H, W), np.float32)
        u = 5.0
        while u < total - 2.0:
            jx = min(ns - 1, int(u / total * (ns - 1)))
            r = float(cr[jx])
            rr = min(3.6, max(0.8, 0.42 * r))
            sx = cx[jx] + nx[jx] * side * r * 0.7
            sy = cy[jx] + ny[jx] * side * r * 0.7
            gi = 0.3 + 0.55 * g * (0.75 + 0.25 * math.sin(u * 0.35 - p.t * 2.0 + salt))
            if rr >= 1.3:
                wx0, wx1 = int(max(0, sx - rr - 1)), int(min(W, sx + rr + 2))
                wy0, wy1 = int(max(0, sy - rr - 1)), int(min(H, sy + rr + 2))
                ox = cv.px[wy0:wy1, wx0:wx1] - sx
                oy = cv.py[wy0:wy1, wx0:wx1] - sy
                a = (ox * tx[jx] + oy * ty[jx]) / rr
                b = (ox * nx[jx] + oy * ny[jx]) / (rr * 0.85)
                q = a * a + b * b
                m = q <= 1.0
                rim_m[wy0:wy1, wx0:wx1] |= m
                rim_i[wy0:wy1, wx0:wx1] = np.where(m, light(a * 0.8, b * 0.8, np.sqrt(np.clip(1 - q, 0, 1)) + 0.2),
                                                    rim_i[wy0:wy1, wx0:wx1])
                c = q <= 0.3
                glo_m[wy0:wy1, wx0:wx1] |= c
                glo_i[wy0:wy1, wx0:wx1] = np.where(c, gi + 0.12 * (rr > 2.6), glo_i[wy0:wy1, wx0:wx1])
            else:
                xi, yi = int(sx), int(sy)
                if 0 <= xi < W and 0 <= yi < H:
                    glo_m[yi, xi] = True
                    glo_i[yi, xi] = gi - 0.1
            u += max(2.5, 1.65 * r)
        cv.part(line=1)
        cv.put(rim_m, rim_i, RIM_FAR if far else RIM)
        cv.part(line=False)
        self._light(cv, p, glo_m, glo_i, decal=False)
        if stump == "start":
            self._stump(cv, p, cx[0], cy[0], -tx[0], -ty[0], float(cr[0]))
        elif stump:
            self._stump(cv, p, cx[-1], cy[-1], tx[-1], ty[-1], float(cr[-1]))

    def _stump(self, cv, p, x, y, tx, ty, r):
        """Form II: the severed arm ends in a glowing wound (seen at an angle), a glowing rim around it."""
        nx, ny = -ty, tx
        wx0, wx1 = int(max(0, x - r - 3)), int(min(W, x + r + 4))
        wy0, wy1 = int(max(0, y - r - 3)), int(min(H, y + r + 4))
        ox = cv.px[wy0:wy1, wx0:wx1] - x
        oy = cv.py[wy0:wy1, wx0:wx1] - y
        a = (ox * tx + oy * ty) / max(1.0, r * 0.5)
        b = (ox * nx + oy * ny) / max(1.0, r * 1.05)
        q = a * a + b * b
        m = np.zeros((H, W), bool)
        it = np.zeros((H, W), np.float32)
        m[wy0:wy1, wx0:wx1] = q <= 1.0
        pulse = 0.06 * math.sin(p.t * 2.0)
        it[wy0:wy1, wx0:wx1] = np.clip(1.0 - 0.55 * q + pulse + 0.2 * (self.g(p) - 0.6), 0, 1)
        cv.part(line=False)
        self._light(cv, p, m, it, decal=False, core=True)
        rim = _dilate(m) & ~m & (cv.mat >= 0)
        self._light(cv, p, rim, np.full((H, W), 0.75, np.float32))

    def _piece(self, cv, p):
        """The severed end of the arm, flying off (phase-5 roar)."""
        ox, oy, rot = p.piece
        pts, radii = self.arm_geom(p, CUT, full=True)
        m = int(round(CUT_AT * (len(pts) - 1)))
        seg, rr = pts[m:], radii[m:]
        b = seg[0]
        c, s = math.cos(rot), math.sin(rot)
        moved = [(b[0] + ox + (x - b[0]) * c - (y - b[1]) * s, b[1] + oy + (x - b[0]) * s + (y - b[1]) * c)
                 for x, y in seg]
        self._tube(cv, p, [cv.T(q) for q in moved], rr, SIDE[CUT], False, salt=CUT, stump="start")

    # ------------------------------------------------------------ body
    def _body(self, cv, p):
        """Mantle, neck, head and the crown of the arms: one soft blob (smooth union of ellipsoids)
        shaded from its height field, papillae on the mantle, then its light."""
        lx, ly = self.local(cv)
        b = self.mscale(p)
        blobs = [(MC[0], MC[1], MA * b, MB * b, MANG, 36.0 * b), NECK, HEAD, CROWN]
        h = None
        mask = np.zeros((H, W), bool)
        for (cx, cy, rx, ry, ang, z) in blobs:
            ca, sa = math.cos(ang), math.sin(ang)
            dx, dy = lx - cx, ly - cy
            u = (dx * ca + dy * sa) / rx
            v = (-dx * sa + dy * ca) / ry
            q = u * u + v * v
            mask |= q <= 1.0
            hi = np.sqrt(np.clip(1 - q, 0, None)) * z
            h = hi if h is None else _smax(h, hi, 8.0)
        h = np.where(mask, h, 0.0)
        # mantle coordinates (px along / across the axis)
        dx, dy = lx - MC[0], ly - MC[1]
        mu = (dx * EU[0] + dy * EU[1]) / b
        mv = (dx * EV[0] + dy * EV[1]) / b
        U, V = mu / MA, mv / MB
        rm = U * U + V * V
        inm = mask & (rm < 1.0)
        h = h + 0.8 * self._papillae(mu, mv) * np.clip(1.0 - rm, 0, 1) ** 0.5 * inm
        gy, gx = np.gradient(h)
        gx = np.clip(gx, -6, 6)
        gy = np.clip(gy, -6, 6)
        nrm = np.sqrt(gx * gx + gy * gy + 1.0)
        inten = 0.98 * light(-gx / nrm, -gy / nrm, 1.0 / nrm) - 0.07
        cv.part()
        cv.put(mask, inten, SKIN)
        self._marks(cv, p, mask, inm, U, V, lx, ly)
        if p.crack > 0.0 and not p.broken:
            self._crack(cv, p, mask, mu, mv)
        if p.broken:
            self._wound(cv, p, mask, mu, mv)

    @staticmethod
    def _papillae(mu, mv, cell=8.0, rad=2.4):
        """Small round bumps (cellular noise in mantle coordinates)."""
        gx0, gy0 = np.floor(mu / cell), np.floor(mv / cell)
        out = np.zeros(mu.shape, np.float32)
        for ox in (-1, 0, 1):
            for oy in (-1, 0, 1):
                cx, cy = gx0 + ox, gy0 + oy
                px = (cx + 0.2 + 0.6 * _hash(cx, cy, 1.0)) * cell
                py = (cy + 0.2 + 0.6 * _hash(cx, cy, 2.0)) * cell
                rr = rad * (0.55 + 0.7 * _hash(cx, cy, 3.0))
                d2 = (mu - px) ** 2 + (mv - py) ** 2
                out = np.maximum(out, np.sqrt(np.clip(1 - d2 / (rr * rr), 0, None)))
        return out

    def _marks(self, cv, p, mask, inm, U, V, lx, ly):
        """The bioluminescence of the mantle: bands of light arching across it and columns of spots."""
        g = self.g(p)
        sm = np.zeros((H, W), bool)
        si = np.zeros((H, W), np.float32)
        for k, (c, bow, vmax, wd) in enumerate(STRIPES):
            dd = np.abs(U - (c - bow * V * V)) * MA
            sel = inm & (np.abs(V) < vmax) & (dd < wd)
            sm |= sel
            # a wave of light running along each band, brighter in its middle, fading at its ends
            si = np.where(sel, 0.22 + 0.55 * g * (0.62 + 0.38 * np.sin(V * 5.0 - p.t * 1.0 + k * 1.7))
                          + 0.14 * (1.0 - dd / wd) - 0.25 * np.clip(np.abs(V) / vmax - 0.75, 0, 1), si)
        cv.part(line=False)
        self._light(cv, p, sm, si)
        cm = np.zeros((H, W), bool)
        ci = np.zeros((H, W), np.float32)
        hm = np.zeros((H, W), bool)
        hi = np.zeros((H, W), np.float32)
        for k, (u, v, r) in enumerate(SPOTS[1 if p.broken else 0]):
            sx, sy = self.mantle_pt(p, u, v)
            d = np.hypot(lx - sx, ly - sy)
            sel = mask & (d < r)
            fl = 0.75 + 0.25 * math.sin(p.t * 1.0 + k * 2.3)
            hm |= sel
            hi = np.where(sel, 0.28 + 0.6 * g * fl * (1.0 - 0.55 * d / r), hi)
            if r >= 2.0:
                c = sel & (d < r * 0.42)
                cm |= c
                ci = np.where(c, 0.45 + 0.5 * g * fl, ci)
        self._light(cv, p, hm, hi)
        self._light(cv, p, cm, ci, core=True)

    @staticmethod
    def _hole(mu, mv):
        """Normalised distance to the centre of the tear (<= 1 inside its jagged edge), its angle and
        the offsets (px) from that centre."""
        ox, oy = mu - TEAR[0] * MA, mv - TEAR[1] * MB
        a = np.arctan2(oy / TEAR[3], ox / TEAR[2])
        q = np.hypot(ox / TEAR[2], oy / TEAR[3])
        q = q / (1.0 + 0.16 * np.sin(7 * a + 1.3) + 0.08 * np.sin(13 * a + 0.4) + 0.05 * np.sign(np.sin(19 * a)))
        return q, a, ox, oy

    def _scars(self, cv, p, frac=1.0, inten=0.6):
        """Glowing tears running from the wound across the mantle (frac: how far they have run)."""
        cv.part(line=False)
        for line in TEAR_CRACKS:
            n = max(2, int(round(len(line) * frac)))
            pts = [cv.T(self.mantle_pt(p, TEAR[0] + x / MA, TEAR[1] + y / MB)) for x, y in line[:n]]
            for q0, q1 in zip(pts[:-1], pts[1:]):
                cv.line(q0, q1, HOT, inten=inten, decal=True)

    def _crack(self, cv, p, mask, mu, mv):
        """Phase-5 roar, before the tear: the edge of the future wound splits and glows."""
        q, a, ox, oy = self._hole(mu, mv)
        frac = np.mod((a + PI) / TAU + 0.3, 1.0)
        cv.part(line=False)
        cv.put(mask & (np.abs(q - 1.0) < 0.09) & (frac < p.crack), 0.7 + 0.25 * p.crack, HOT, decal=True)
        cv.put(mask & (np.abs(q - 0.55 + 0.1 * np.sin(5 * a)) < 0.06) & (frac < p.crack * 0.8),
               0.5 + 0.3 * p.crack, HOT, decal=True)
        self._scars(cv, p, frac=p.crack, inten=0.55 + 0.35 * p.crack)

    def _wound(self, cv, p, mask, mu, mv):
        """Form II: the mantle is torn open on its glowing inner core (pulsing), scars run from the tear."""
        g = self.g(p)
        self._scars(cv, p, inten=0.36 + 0.4 * min(1.2, g))
        q, a, ox, oy = self._hole(mu, mv)
        hole = mask & (q <= 1.0)
        it = np.clip(1.04 - 0.72 * q + 0.07 * math.sin(p.t * 2.0) + 0.15 * (g - 0.75), 0, 1)
        self._light(cv, p, hole, it, core=True)
        # membranes across the core: darker veins
        for k in range(3):
            ang = 0.5 + k * 2.1
            perp = np.abs(ox * math.sin(ang) - oy * math.cos(ang))
            along = ox * math.cos(ang) + oy * math.sin(ang)
            vein = hole & (perp < 0.7) & (along > 0) & (q > 0.3) & (q < 0.88)
            self._light(cv, p, vein, np.full((H, W), 0.52, np.float32))
        rim = _dilate(hole) & mask & ~hole
        self._light(cv, p, rim, np.full((H, W), 0.8, np.float32))
        # torn flaps of skin around it, catching the light
        flap = _dilate(hole, 3) & mask & ~_dilate(hole) & (np.sin(a * 9.0 + 0.7) > -0.1)
        cv.put(flap, 0.93, SKIN, decal=True)

    # ------------------------------------------------------------ eyes
    def _eye(self, cv, p, far):
        T = cv.T
        c = FAR_EYE if far else EYE
        s = 0.72 if far else 1.0
        sk = SKIN_FAR if far else SKIN
        # the eye mound
        cv.part()
        cv.ellipse(T(c), 11.0 * s, 10.0 * s, sk, ang=0.15 + p.tilt)
        # the eyeball
        lx, ly = self.local(cv)
        ex, ey = (c[0] + 0.8 * s, c[1] + 0.6 * s)
        rx, ry = 8.2 * s, 7.4 * s
        u = (lx - ex) / rx
        v = (ly - ey) / ry
        q = u * u + v * v
        m = q <= 1.0
        cv.part(line=1)
        e = min(1.3, p.eye + (0.2 if p.broken else 0.0)) * (0.75 if far else 1.0)
        d = np.sqrt(q)
        it = 0.3 + 0.62 * e * (1.0 - 0.5 * d) + 0.1 * (-0.6 * u - v)
        self._light(cv, p, m, it, decal=False, core=True)
        self._light(cv, p, m & (d > 0.84), np.full((H, W), 0.3 + 0.3 * min(1.0, e), np.float32), core=True)
        # the pupil: a horizontal slit, rounded at the ends
        pw = 0.62 - 0.12 * max(0.0, e - 0.6)
        pm = m & ((v / (0.2 + 0.06 * (1 - min(1.0, e)))) ** 2 + (u / pw) ** 4 <= 1.0)
        cv.put(pm, 0.1, MAW, decal=True)
        if p.dead < 1.0:
            cv.px_set(*T((ex - 3.6 * s, ey - 3.4 * s)), CORE, inten=1.0, decal=True)
            if not far:
                cv.px_set(*T((ex - 2.6 * s, ey - 3.4 * s)), CORE, inten=0.9, decal=True)
                cv.px_set(*T((ex - 3.6 * s, ey - 2.4 * s)), CORE, inten=0.9, decal=True)
        # lids: the upper one comes down with p.lid, the lower one rises when it is shut tight
        lid = min(1.0, max(0.0, p.lid))
        cv.part(line=1)
        cv.put((q <= 1.3) & (v < -1.15 + 2.1 * lid), 0.66 - 0.32 * np.clip(v + 1, 0, 1), sk)
        if lid > 0.5:
            cv.put((q <= 1.3) & (v > 1.0 - 1.2 * (lid - 0.5)), 0.4, sk)
        if far:
            return
        # the brow: a heavy fold over the eye, slanting down to the front (menacing)
        cv.part()
        b0 = T((c[0] - 11.5, c[1] - 9.5 + p.brow * 2))
        bm = T((c[0] - 1.0, c[1] - 9.8 + p.brow * 1.5))
        b1 = T((c[0] + 10.5, c[1] - 6.0 + p.brow * 2.5))
        cv.chain([b0, bm, b1], [2.4, 3.2, 2.0], sk)

    # ------------------------------------------------------------ beak
    def _beak(self, cv, p):
        T = cv.T
        BL0, JL0 = self.beak_fns(p)

        def BL(x, y):
            return T(BL0(x, y))

        def JL(x, y):
            return T(JL0(x, y))

        if p.jaw > 0.05:
            cv.part(line=False)
            cv.poly([BL(-3, 2), BL(5, 2), BL(14, 2.6), BL(21, 5.5), JL(17.5, 7), JL(12, 4.4), JL(5, 3.2), JL(-3, 2.6)],
                    MAW, flat=0.3)
            if p.mouth > 0.03:
                gm = min(1.2, p.mouth)
                cv.poly([BL(-2, 2.2), BL(4 + 10 * gm, 2.6 + 2 * gm), JL(3 + 9 * gm, 3.4 + 1.5 * gm), JL(-2, 2.6)], HOT,
                        flat=0.4 + 0.35 * gm)
                cv.poly([BL(-2, 2.4), BL(2 + 6 * gm, 2.6 + gm), JL(2 + 5 * gm, 3.2 + gm), JL(-2, 2.6)], CORE,
                        flat=0.6 + 0.3 * gm)
        cv.part()
        cv.poly([JL(*q) for q in LOWER_BEAK], BEAK, bevel=1.2, base=0.3)
        cv.part()
        cv.poly([BL(*q) for q in UPPER_BEAK], BEAK, bevel=1.8, base=0.44)
        # gloss along the ridge and down the hook
        cv.part(line=False)
        for a, b, it in (((0, -6.4), (8, -7.4), 0.97), ((8, -7.4), (15, -6.0), 0.92), ((16, -5.2), (22, 0.0), 0.8),
                         ((22.5, 1.0), (24.2, 6.5), 0.7)):
            cv.line(BL(*a), BL(*b), BEAK, inten=it, decal=True)
        if p.jaw <= 0.05 and p.mouth > 0.03:  # light leaking between the closed mandibles
            cv.line(BL(0, 2.2), BL(12, 2.8), HOT, inten=0.4 + 0.4 * min(1.0, p.mouth), decal=True)

    def _jet(self, cv, p):
        """A short burst of light at the open beak (special; the beam itself is drawn by the game)."""
        mx, my = self.beak_body(p)
        d = self.beak_dir(p) - p.tilt
        c, s = math.cos(d), math.sin(d)
        T = cv.T
        x0, y0 = mx - c * 5, my - s * 5

        def at(ln, off):
            return T((x0 + c * ln - s * off, y0 + s * ln + c * off))

        for layer, (m, wf, lf, it) in enumerate(((HOT, 1.0, 1.0, 0.44), (HOT, 0.66, 0.86, 0.72),
                                                  (CORE, 0.34, 0.66, 0.93))):
            top, bot = [], []
            n = 8
            L = 30.0 * p.jet * lf
            wd = 0.0
            for i in range(n + 1):
                t = i / n
                wd = (1.6 + 8.0 * math.sin(min(1.0, t * 1.5) * PI / 2)) * wf
                rip = (1.0 if i % 2 else -0.5) * wf * (0.6 + 0.4 * math.sin(p.t * 2.3 + i * 1.7 + layer))
                top.append(at(L * t, -(wd + rip)))
                bot.append(at(L * t, wd + (rip if (i + layer) % 3 else -rip)))
            # a ragged front: the beam goes on from there (drawn by the game)
            front = [at(L * 1.06, -wd * 0.6), at(L * 0.98, -wd * 0.2), at(L * (1.04 + 0.04 * math.sin(p.t + layer)), 0),
                     at(L * 0.98, wd * 0.25), at(L * 1.06, wd * 0.62)]
            cv.part(line=False)
            cv.poly(top + front + bot[::-1], m, flat=it)

    # ------------------------------------------------------------- effects (on rendered frames)
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

    def inhale(self, f, x, y, i, n, seed, count=16):
        """Water and glowing plankton drawn into the beak from the front (streaks)."""
        rng = np.random.default_rng(seed)
        t = (i + 1) / n
        for k in range(count):
            a = -0.9 + 2.0 * rng.random()
            d = 12 + (1 - ((rng.random() + t * 1.7) % 1.0)) * 34
            x0, y0 = x + math.cos(a) * d, y + math.sin(a) * d
            x1, y1 = x + math.cos(a) * (d - 5), y + math.sin(a) * (d - 5)
            if max(x0, x1) < W - 3 and 2 < min(y0, y1) and max(y0, y1) < H - 3:
                streak(f, (x0, y0), (x1, y1), [PC[1], PC[2], PC[3]][k % 3])

    def plankton(self, f, i, n, seed, count=10, p=None):
        """Glowing plankton drifting around the body (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        ox, oy = (p.dx, p.dy) if p is not None else (0.0, 0.0)
        for k in range(count):
            x0 = 40 + rng.random() * 176 + ox
            y0 = 20 + rng.random() * 170 + oy
            ph = TAU * i / n + rng.random() * TAU
            put_if_empty(f, x0 + 3.0 * math.sin(ph), y0 + 2.0 * math.cos(ph + k), [PC[1], PC[2], PC[3]][k % 3])

    @staticmethod
    def bubble(f, x, y, r):
        """A bubble: a pale ring with a white glint (r < 1.5: one pixel); kept 3 px from the edges."""
        if not (3 + r <= x <= W - 4 - r and 3 + r <= y <= H - 4 - r):
            return
        if r < 1.5:
            put_if_empty(f, x, y, BUBBLE[0])
            return
        for (bx, by) in FX.ring_pixels(x, y, r):
            put_if_empty(f, bx, by, BUBBLE[1] if (bx - x) + (by - y) > 0 else BUBBLE[0])
        put_if_empty(f, x - r * 0.4, y - r * 0.4, WHITE_C)

    def bubbles(self, f, i, n, seed, x0, x1, y0, rise=60.0, count=6, p=None):
        """Bubbles rising from the band x0..x1 at y0 (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        ox, oy = (p.dx, p.dy) if p is not None else (0.0, 0.0)
        for k in range(count):
            bx = x0 + rng.random() * (x1 - x0) + ox
            t = (i / n + rng.random()) % 1.0
            r = 0.8 + 2.2 * rng.random() * (0.6 + 0.4 * t)
            y = y0 + oy - t * rise
            if 4 < y < H - 5:
                self.bubble(f, bx + 2.0 * math.sin(t * 7 + k), y, r)

    @staticmethod
    def ink(f, x, y, r, seed, light=0):
        """A cloud of ink behind the body (only on empty pixels, never closer than 2 px to the frame
        edge): a few round puffs, dense in the middle, paler at the rim; light: 0 dense .. 3 thinning
        out (dithered)."""
        rng = np.random.default_rng(seed)
        x0, x1 = int(max(2, x - r * 1.8)), int(min(W - 2, x + r * 1.8 + 1))
        y0, y1 = int(max(2, y - r * 1.8)), int(min(H - 2, y + r * 1.8 + 1))
        if x0 >= x1 or y0 >= y1 or r < 0.5:
            return
        ys, xs = np.mgrid[y0:y1, x0:x1]
        d = np.full(xs.shape, 9.0)
        for k in range(4):
            a = rng.random() * TAU
            o = r * 0.6 * rng.random() if k else 0.0
            rr = r * (0.5 + 0.35 * rng.random()) if k else r * 0.75
            d = np.minimum(d, np.hypot(xs + 0.5 - x - math.cos(a) * o, ys + 0.5 - y - math.sin(a) * o * 0.8) / rr)
        band = np.clip((d * 4).astype(np.int32) + light, 0, 3)
        sel = (d <= 1.0) & (f[y0:y1, x0:x1, 3] == 0)
        if light >= 2:
            sel &= (xs + ys) % 2 == 0
        if light >= 3:
            sel &= ys % 2 == 0
        sub = f[y0:y1, x0:x1]
        sub[sel] = np.array(INK, np.uint8)[band[sel]]

    def vortex(self, f, x, y, s, seed):
        """A whirl of light and bubbles (summon: the escort comes out of it)."""
        if s <= 0.05:
            return
        n = int(10 + 14 * s)
        for arm in range(3):
            a0 = arm * TAU / 3 + s * 2.0
            for k in range(n):
                t = k / n
                a = a0 + t * 4.5
                rr = (2 + 13 * t) * s
                put_if_empty(f, x + math.cos(a) * rr, y + math.sin(a) * rr * 0.6,
                             [WHITE_C, PC[0], PC[1], PC[2], PC[3]][min(4, int(t * 5))])
        motes(f, x, y, int(4 + 6 * s), 4, 10 * s + 6, [PC[0], PC[1], PC[2]], seed=seed + 1)
        rng = np.random.default_rng(seed + 2)
        for k in range(int(3 * s) + 1):
            self.bubble(f, x + rng.uniform(-10, 10) * s, y - 6 - rng.random() * 14 * s, 1.0 + rng.random() * 1.6)

    def drips(self, f, p, i, n):
        """Form II: drops of light falling from the severed arm (periodic over n frames)."""
        if not p.broken:
            return
        x, y = self.tip(p, CUT)
        for k in range(3):
            t = (i / n + k / 3.0) % 1.0
            c = [PC[0], PC[1], PC[2], PC[3]][min(3, int(t * 4))]
            dx, dy = x + 1.5 * math.sin(t * 5 + k), y + 5 + t * 18
            put_if_empty(f, dx, dy, c)
            if t < 0.5:
                put_if_empty(f, dx, dy - 1, c)

    # ================================================================= clips
    def idle(self, broken=False):
        """Floating: a slow bob, the mantle breathing, the arms undulating (loop)."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            arms = [(a0 + 0.07 * math.sin(ph + k * 0.9), c + 0.12 * math.sin(ph + k * 1.3), lf, pw)
                    for k, (a0, c, lf, pw) in enumerate(REST)]
            p = KPose(t=ph, broken=broken, dy=-3.0 * math.sin(ph), breath=0.5 * math.sin(ph - 0.7), arms=arms,
                      wave=0.32, glow=0.6 + 0.1 * math.sin(ph), eye=0.5)
            f = self.draw(p)
            self.plankton(f, i, n, self.seed + 1, p=p)
            self.bubbles(f, i, n, self.seed + 2, 60, 110, 62, rise=36, count=3, p=p)
            self.drips(f, p, i, n)
            out.append(f)
        return out

    def walk(self, broken=False):
        """Swimming (= move): the mantle squeezes, the arms sweep back together, then open (loop)."""
        out = []
        n = 8
        open_ = blend(REST, SWIM, 0.5)
        for i in range(n):
            ph = TAU * i / n
            k = 0.5 + 0.5 * math.cos(ph)  # 1: arms closed (power stroke), 0: arms open (recovery)
            p = KPose(t=2 * ph, broken=broken, tilt=-0.1, dx=1.5 * math.cos(ph), dy=-2.0 * math.sin(ph),
                      breath=-0.8 * k + 0.6 * (1 - k), arms=blend(open_, SWIM, k), wave=0.42, glow=0.65, eye=0.6,
                      lid=0.3)
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for j in range(7):
                y = int(rng.integers(40, 190))
                x0 = int(rng.integers(3, 30))
                streak(f, (x0, y), (x0 + int(rng.integers(6, 16)), y), PC[2 + j % 3])
            self.bubbles(f, i, n, self.seed + 3, 60, 110, 110, rise=70, count=6, p=p)
            self.plankton(f, i, n, self.seed + 4, count=6, p=p)
            self.drips(f, p, i, n)
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """The front arms rear up, hooked to strike; the eyes narrow and flare, the light rises."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            p = KPose(t=0.5 * i, broken=broken, tilt=-0.07 * e, dy=-3 * e, arms=blend(REST, REAR, e), wave=0.25,
                      breath=0.3 * e, glow=0.6 + 0.4 * t, eye=0.5 + 0.7 * t, lid=0.25 + 0.15 * t, brow=e,
                      jaw=0.25 * e)
            f = self.draw(p)
            if i >= 2:
                for arm in (LASH, 6):
                    x, y = self.tip(p, arm)
                    self.charge(f, x, y, i, 6, self.seed + 20 + i + arm, r0=16, r1=3, count=3 + i)
            if i >= 4:
                x, y = self.tip(p, LASH)
                self.sparkle(f, x, y, 2 + (i - 4) * 2)
            out.append(f)
        return out

    def attack_poses(self, broken=False):
        keys = [  # lash arm (a0, curl, lf, pw), blend of the others to REAR, dx, glow
            ((-1.15, -1.25, 1.0, 1.4), 0.9, -1, 0.95),
            ((-0.16, 0.36, 0.86, 1.0), 0.55, 2, 1.05),
            ((0.55, 1.5, 0.95, 1.2), 0.4, 1, 0.9),
            ((0.85, -0.5, 0.95, 1.5), 0.25, 1, 0.75),
            ((0.95, -2.0, 1.0, 2.0), 0.1, 0, 0.65)]
        out = []
        for i, (lash, rear, dx, g) in enumerate(keys):
            arms = blend(REST, REAR, rear)
            arms[LASH] = lash
            out.append(KPose(t=0.5 * i + 3, broken=broken, dx=dx, tilt=-0.05 * rear, arms=arms, wave=0.15,
                             glow=g, eye=0.5 + 0.6 * g, lid=0.35, brow=rear, jaw=0.3 * rear, breath=0.2))
        return out

    def attack(self, broken=False):
        """A lash of the front arm: it uncoils forward, a bolt of light leaves its tip on f1 (ORIGIN)."""
        out = []
        poses = self.attack_poses(broken)
        for i, p in enumerate(poses):
            f = self.draw(p)
            x, y = self.tip(p)
            if i == 0:
                self.charge(f, x, y, 4, 6, self.seed + 39, r0=12, r1=3, count=8)
            if i == 1:
                # the smear of the whip: arcs from the raised tip down to the lashing one
                px, py = self.tip(poses[0])
                for k in range(3):
                    cx, cy = (px + x) / 2 - 4 * k, (py + y) / 2 + 10 + 3 * k
                    r = math.hypot(x - cx, y - cy)
                    ring(f, cx, cy, r, [PC[0], PC[1], PC[2]][k], math.atan2(py - cy, px - cx),
                         math.atan2(y - cy, x - cx), dotted=0 if k < 2 else 2)
                FX.burst(f, x + 2, y, 7, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 40, jag=0.35, rays=6)
            if i == 2:
                for k in range(5):
                    a = -0.9 + 0.45 * k
                    self.sparkle(f, x + 8 + math.cos(a) * 10, y + math.sin(a) * 10, 2)
                ring(f, x + 2, y, 8, PC[1], -1.4, 1.4, dotted=2)
            if i == 3:
                motes(f, x, y - 4, 10, 4, 16, [WHITE_C, PC[0], PC[1]], seed=self.seed + 45, arc=(-PI, 0.3))
            out.append(f)
        return out

    def telegraph_special(self, broken=False):
        """It inhales: the mantle swells, the light gathers in the eyes and the beak, water is drawn in."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            p = KPose(t=0.7 * i, broken=broken, tilt=-0.05 * e, dy=-2 * e, arms=blend(REST, CLENCH, e), wave=0.2,
                      breath=1.0 * e, glow=0.6 + 0.45 * t, eye=0.5 + 0.75 * t, lid=0.25 - 0.15 * t, brow=0.5 * e,
                      jaw=0.15 + 0.4 * e, mouth=t)
            f = self.draw(p)
            x, y = self.beak(p)
            self.inhale(f, x, y, i, 6, self.seed + 50 + i, count=10 + 2 * i)
            self.charge(f, x, y, i, 6, self.seed + 56 + i, r0=22, r1=4, count=4 + i)
            if i == 5:
                self.sparkle(f, x + 2, y, 5)
            out.append(f)
        return out

    def special_pose(self, i, broken=False):
        """The four frames of the jet: the head (and the beak) never move, the mantle pumps."""
        arms = [(a0 + 0.05 * math.sin(i * PI / 2 + k), c, lf, pw) for k, (a0, c, lf, pw) in
                enumerate(blend(REST, SWIM, 0.35))]
        return KPose(t=PI / 2 * i, broken=broken, tilt=-0.05, dy=-2.0, arms=arms, wave=0.3,
                     breath=-0.5 - 0.25 * (i % 2), glow=1.0, eye=1.2, lid=0.3, brow=0.6, jaw=1.0, mouth=1.1,
                     jet=0.9 + 0.1 * (i % 2))

    def special(self, broken=False):
        """The jet (loop): beak wide open, a burst of light at the beak, ink puffing around it."""
        out = []
        for i in range(4):
            p = self.special_pose(i, broken)
            f = self.draw(p)
            x, y = self.beak(p)
            d = self.beak_dir(p)
            c, s = math.cos(d), math.sin(d)
            for k, (along, side, r) in enumerate(((7, -11, 7), (9, 11, 6), (20, -15, 6), (22, 15, 5))):
                along += 3 * ((i + k) % 2)
                self.ink(f, x + c * along + s * side * -1, y + s * along + c * side, r + (i + k) % 2,
                         self.seed + 60 + 4 * i + k, light=k // 2)
            motes(f, x + 14, y + 6, 8, 4, 12, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 74 + i,
                  arc=(d - 1.2, d + 1.2))
            self.bubbles(f, i, 4, self.seed + 78, x - 14, x + 4, y - 4, rise=30, count=4)
            out.append(f)
        return out

    def summon(self, broken=False):
        """The arms spread wide, a pulse of light leaves the body; two whirls open (the escort)."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.25]
        out = []
        for i, h in enumerate(keys):
            p = KPose(t=0.6 * i, broken=broken, tilt=-0.06 * h, dy=-2 * h, arms=blend(REST, SPREAD, h), wave=0.25,
                      breath=0.6 * h, glow=0.6 + 0.5 * h, eye=0.5 + 0.6 * h, lid=0.25 - 0.1 * h, brow=0.5 * h,
                      jaw=0.5 * h, mouth=0.5 * h)
            f = self.draw(p)
            cx, cy = BX + p.dx, BY + 4 + p.dy
            if 1 <= i <= 4:
                for k, r in enumerate((30 + 15 * i, 22 + 15 * i)):
                    ring(f, cx, cy, r, [PC[1], PC[2]][k], dotted=0 if k == 0 else 2)
            s = [0, 0.4, 0.8, 1.0, 1.0, 0.6][i]
            self.vortex(f, 26, 168, s * 1.2, self.seed + 80 + i)
            self.vortex(f, 230, 168, s * 1.2, self.seed + 86 + i)
            motes(f, 128, 110, 6 + 3 * i, 40, 100, [PC[0], PC[1], PC[2]], seed=self.seed + 92 + i)
            out.append(f)
        return out

    def roar_pose(self, h, i, broken, **kw):
        d = dict(t=0.8 * i, broken=broken, tilt=-0.1 * h, dy=-2 * h,
                 arms=blend(blend(REST, SPREAD, 0.6 * h), REAR, 0.3 * h), wave=0.3 + 0.2 * h, breath=0.8 * h,
                 glow=0.6 + 0.5 * h, eye=0.5 + 0.8 * h, lid=0.25 + 0.15 * h, brow=h, jaw=h, mouth=0.6 * h)
        d.update(kw)
        return KPose(**d)

    def _roar_fx(self, f, p, i):
        x, y = self.beak(p)
        d = self.beak_dir(p)
        for k, r in enumerate((7 + 2 * (i % 2), 12 + 2 * (i % 2), 17 + 2 * (i % 2))):
            ring(f, x, y, r, [PC[1], PC[2], PC[3]][k], max(-PI, d - 0.9), min(PI, d + 0.9), dotted=0 if k < 2 else 2)
        motes(f, BX, BY, 18, 60, 96, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 100 + i, big=0.2)

    def roar(self, broken=False):
        """Phase change: it rears, spreads its arms, opens its beak wide; the light flares."""
        keys = [(0.0, 0), (0.3, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = self.roar_pose(h, i, broken)
            f = self.draw(p)
            if h > 0.8:
                self._roar_fx(f, p, i)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def shatter(self):
        """Phase 5 roar: light splits the mantle, it tears open on the core, an arm is severed (Blessé)."""
        out = []
        seq = [(False, 0.5, 0.35, 1.0), (False, 0.8, 0.7, 1.2), (False, 1.0, 1.0, 1.4), (True, 1.0, 0.0, 1.2),
               (True, 1.0, 0.0, 1.1), (True, 0.9, 0.0, 1.0), (True, 0.6, 0.0, 0.8), (True, 0.2, 0.0, 0.7)]
        pieces = {3: (-8, 4, 0.3), 4: (-18, 8, 0.75), 5: (-28, 10, 1.2)}  # flung back, tumbling
        for i, (broken, h, crack, g) in enumerate(seq):
            p = self.roar_pose(h, i, broken, crack=crack, glow=g, piece=pieces.get(i))
            f = self.draw(p)
            cx, cy = self.T(p, self.mantle_pt(p, TEAR[0], TEAR[1]))
            if i == 2:
                f = flashed(f, 0.35)
            if i == 3:
                FX.burst(f, cx, cy, 14, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 110, jag=0.4, rays=10)
            if 3 <= i <= 5:
                rng = np.random.default_rng(self.seed + 111 + i)
                for k in range(9):
                    a = -2.9 + 3.4 * rng.random()
                    dd = 24 + 14 * (i - 3) + rng.random() * 10
                    FX.chunk(f, cx + math.cos(a) * dd, cy + math.sin(a) * dd * 0.8, 3 if k % 2 else 2, FLESH_L, FLESH_D)
                ring(f, cx, cy, 22 + 12 * (i - 3), PC[1 + (i - 3)], dotted=0 if i == 3 else 2)
                self.ink(f, cx - 20 - 6 * (i - 3), cy + 6, 6 + 2 * (i - 3), self.seed + 115 + i, light=i - 3)
            if h > 0.8:
                fl = getattr(f, "flash", 0.0)
                self._roar_fx(f, p, i)
                f = shake(np.asarray(f), [1, -1, 1, -1, 1, -1][min(5, i)], 0)
                if fl:
                    f = flashed(f, fl)
            out.append(f)
        return out

    def hurt(self, broken=False):
        a = KPose(t=0.3, broken=broken, dx=-5, tilt=-0.12, arms=blend(REST, SPREAD, 0.45), wave=0.5, breath=-0.4,
                  glow=0.9, eye=1.0, lid=0.6, brow=1.0, jaw=0.45)
        b = KPose(t=0.6, broken=broken, dx=-2, tilt=-0.05, arms=blend(REST, SPREAD, 0.15), wave=0.35, breath=-0.1,
                  glow=0.7, eye=0.7, lid=0.4, brow=0.4, jaw=0.15)
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        motes(fb, BX, BY - 10, 12, 50, 80, [WHITE_C, PC[0], PC[1]], seed=self.seed + 120)
        return [fa, fb]

    def death(self, broken=False):
        out = []
        # 0-2: convulsions, the light flares and gutters
        conv = [KPose(t=0.2, broken=broken, tilt=-0.12, arms=blend(REST, SPREAD, 0.9), wave=0.6, breath=0.8, glow=1.2,
                      eye=1.2, lid=0.0, brow=1.0, jaw=0.9, mouth=0.6),
                KPose(t=1.4, broken=broken, tilt=0.06, dy=4, arms=blend(REST, CLENCH, 0.9), wave=0.3, breath=-0.6,
                      glow=0.45, eye=0.4, lid=0.6, jaw=0.2),
                KPose(t=2.6, broken=broken, tilt=-0.06, dy=6, arms=blend(REST, SPREAD, 0.7), wave=0.6, breath=0.5,
                      glow=1.0, eye=1.0, lid=0.2, brow=0.8, jaw=0.8, mouth=0.5)]
        for i, p in enumerate(conv):
            f = self.draw(p)
            if i == 2:
                cx, cy = self.T(p, self.mantle_pt(p, TEAR[0], TEAR[1]))
                FX.burst(f, cx, cy, 6, [WHITE_C, PC[0], PC[1]], seed=self.seed + 130, jag=0.45, rays=5)
            f = shake(f, [1, -1, 2][i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        # 3-6: it sinks, the arms go limp, the light fades
        sink = [(9, 0.08, 0.3), (13, 0.14, 0.55), (17, 0.2, 0.8), (20, 0.24, 1.0)]
        for i, (dy, tl, lk) in enumerate(sink):
            p = KPose(t=3 + 0.5 * i, broken=broken, tilt=tl, dy=dy, arms=blend(blend(REST, SPREAD, 0.5), LIMP, lk),
                      wave=0.2, breath=-0.1 * i, glow=0.5 - 0.1 * i, eye=0.5 - 0.12 * i, lid=0.6 + 0.13 * i,
                      dead=0.25 + 0.25 * i, jaw=0.3)
            f = self.draw(p)
            if i < 2:
                motes(f, 120, 70 + dy, 10, 20, 70, [PC[1], PC[2], PC[3]], seed=self.seed + 131 + i, arc=(-2.8, -0.4))
            out.append(f)
        # 7: the light is out, ink starts to leak
        last = KPose(t=5, broken=broken, tilt=0.26, dy=22, arms=LIMP, wave=0.15, breath=-0.3, glow=0.0, eye=0.0,
                     lid=1.0, dead=1.0, jaw=0.3)
        base = self.draw(last)
        f7 = base.copy()
        bx, by = self.beak(last)
        self.ink(f7, bx + 2, by + 4, 5, self.seed + 135)
        self.bubbles(f7, 0, 1, self.seed + 136, 90, 170, 120, rise=40, count=5)
        out.append(f7)
        # 8-12: it dissolves into ink and bubbles
        for t in (0.2, 0.4, 0.6, 0.8, 0.96):
            out.append(self.ink_dissolve(base, t, self.seed + 141))
        # 13: the ink thins out, a few bubbles, two last motes of light
        fin = np.zeros((H, W, 4), np.uint8)
        for k, (x, y, r) in enumerate(((112, 112, 15), (144, 128, 13), (98, 144, 11), (130, 152, 12), (150, 100, 9))):
            self.ink(fin, x, y, r, self.seed + 150 + k, light=3)
        self.bubbles(fin, 0, 1, self.seed + 155, 90, 160, 120, rise=80, count=7)
        put_if_empty(fin, 121, 98, PC[3])
        put_if_empty(fin, 137, 86, PC[4])
        out.append(fin)
        return out

    @staticmethod
    def _vnoise(x, y, cell, seed):
        """Smooth value noise (0..1) on cells of `cell` px."""
        gx, gy = x / cell, y / cell
        x0, y0 = np.floor(gx), np.floor(gy)
        fx, fy = gx - x0, gy - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        h00, h10 = _hash(x0, y0, seed), _hash(x0 + 1, y0, seed)
        h01, h11 = _hash(x0, y0 + 1, seed), _hash(x0 + 1, y0 + 1, seed)
        return (h00 * (1 - fx) + h10 * fx) * (1 - fy) + (h01 * (1 - fx) + h11 * fx) * fy

    def ink_dissolve(self, base, t, seed):
        """The dead body comes apart into ink, from its edges inwards (the arms first, the mantle last)
        and in blotches: the dissolving front turns to ink, clouds of ink billow out and rise, bubbles
        escape (t: 0 whole .. 1 gone)."""
        img = np.asarray(base)
        out = np.zeros_like(img)
        alpha = img[:, :, 3] > 0
        if not alpha.any():
            return out
        depth = np.zeros((H, W), np.float32)
        cur = alpha.copy()
        for _ in range(40):
            if not cur.any():
                break
            depth[cur] += 1
            cur &= ~_dilate(~cur)
        ys, xs = np.mgrid[0:H, 0:W]
        noise = 0.75 * self._vnoise(xs, ys, 7.0, seed % 89) + 0.25 * _hash(xs, ys, seed % 31)
        thr = 0.5 * np.sqrt(depth / depth.max()) + 0.5 * noise
        thr = 0.96 * (thr - thr[alpha].min()) / max(1e-6, thr[alpha].max() - thr[alpha].min())
        keep = alpha & (thr > t)
        edge = keep & (thr < t + 0.07)
        out[keep] = img[keep]
        out[edge & (noise > 0.5)] = INK[1]
        out[edge & (noise <= 0.5)] = INK[2]
        # clouds and bubbles leave from fixed points of the body, each when the front reaches it
        py, px = np.nonzero(alpha)
        rng = np.random.default_rng(seed)
        for k, j in enumerate(rng.choice(len(py), 26, replace=False)):
            x, y = px[j] + 0.5, py[j] + 0.5
            age = t - thr[py[j], px[j]]
            if 0 < age < 0.6:
                r = 3 + 9 * min(1.0, age * 2.5)
                self.ink(out, x + (rng.random() - 0.5) * 16 * age, y - 18 * age, r, seed + k,
                         light=min(3, int(age * 6)))
        for k, j in enumerate(rng.choice(len(py), 18, replace=False)):
            age = t - thr[py[j], px[j]]
            if 0 < age < 0.5:
                self.bubble(out, px[j] + 3 * math.sin(age * 9 + k), py[j] - 70 * age, 1.0 + 1.6 * ((k * 7) % 5) / 4)
        return out

    # ------------------------------------------------------------ the sheet
    def clips(self, form2=False):
        b = form2
        return [
            ("idle", self.idle(b), dict(fps=self.IDLE_FPS, loop=True)),
            ("telegraph", self.telegraph(b), dict(durations=TIMING["telegraph"], loop=False)),
            ("walk", self.walk(b), dict(fps=self.WALK_FPS, loop=True)),
            ("attack", self.attack(b), dict(durations=TIMING["attack"], loop=False, events=self.ATTACK_EVENTS)),
            ("telegraph_special", self.telegraph_special(b), dict(durations=TIMING["telegraph_special"], loop=False)),
            ("special", self.special(b), dict(fps=self.SPECIAL_FPS, loop=True)),
            ("hurt", self.hurt(b), dict(durations=TIMING["hurt"], loop=False)),
            ("summon", self.summon(b), dict(durations=TIMING["summon"], loop=False)),
            ("roar", self.roar(b), dict(durations=TIMING["roar"], loop=False)),
            ("death", self.death(b), dict(durations=TIMING["death"], loop=False)),
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


BOSS = Kraken()


def phase_clips(k, form1, form2, tear):
    clips = form1 if k < 4 else (tear if k == 4 else form2)
    return [(name, [finish(f, k) for f in frames], kw) for name, frames, kw in clips]


def phase_sheets():
    """(phase index 0..7, image, clip meta) of the eight phase sheets."""
    form1 = BOSS.clips(False)
    form2 = BOSS.clips(True)
    tear = [c if c[0] != "roar" else ("roar", BOSS.shatter(), c[2]) for c in form2]
    out = []
    for k in range(8):
        img, meta = pack(phase_clips(k, form1, form2, tear))
        out.append((k, img, meta))
    return out


def build():
    from sheet import save_json, save_png
    for k, img, meta in phase_sheets():
        name = "kraken" if k == 0 else "kraken_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
