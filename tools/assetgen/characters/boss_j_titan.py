"""Le Titan Mécanique (giga boss, eight phases).

A colossal war machine walking on two heavy legs: gunmetal armour plates with rivets and panel
lines, brass trims and gears turning at the joints, hydraulic pistons behind the calves.  A dome
head sunk between broad shoulders, with a single visor slit; exhaust stacks rise behind the near
shoulder plate (its vents are louvres on top), a drone bay with a hatch on its back.  The near arm
is a huge piston fist (a hydraulic housing, a chrome rod that fires out on the punch, an armoured
fist with brass knuckles); the far arm is a rotary cannon (six spinning barrels, a magazine drum,
cable lights).  A reactor glows in the chest behind a grille.  Side view facing right (the game
flips it), seen a little from the front (the far shoulder, with the cannon, is ahead of the near
one, so both weapons show); it walks on the ground.

Two forms:
  * form I  (phases 1-4): intact;
  * form II (phases 5-8): « Endommagé » - the chest plate is torn off and shows the reactor core
    (a blazing sphere in brass rings, torn edges glowing, cables hanging), the near shoulder plate
    is gone (a bare gear and brackets, sparking cables), dents everywhere, smoke from the stacks.
The light (visor, reactor, muzzle, vents, cable lights, sparks, thrusters) changes colour at every
phase; the metal never does.  Every emissive pixel uses one of five tones P0..P4 of the phase ramp
(plus pure white), so each phase sheet is the phase-1 sheet with those five colours swapped (exact
LUT, `recolour`), see PHASES: 1 Démarrage (ambre), 2 Vapeur (blanc orangé), 3 Surtension (bleu
électrique), 4 Laser (rouge), 5 Surchauffe (orange-rouge, form II), 6 Plasma (magenta), 7 Arc
(vert), 8 Cœur nucléaire (blanc incandescent).

Frames 256x224, pivot [128, 212] = bottom-centre of the 76x148 collision box (x 90..166,
y 64..212) around the body (legs, pelvis, hull; not the cannon, the fist, the head or the stacks).
The ground line is y 212: the feet stand on it and nothing of the body goes below it (only dust and
the glowing cracks of the slam, at most 3 rows).  No frame reaches the 3 px margin of the frame.
Drawn at scale 1.25 in game (320x280 px frame, 95x185 px box).
ORIGIN points (frame pixels from the pivot, x forward, y down; x1.25 in game), `Titan.origins()`:
  * attack frame 2 (`impact`, the piston punch hits the ground): the fist centre at (+57, -22)
    = frame pixel (185, 190); the fist meets the ground between (+46, 0) and (+58, 0);
  * cannon muzzle at rest (idle; idle and walk sway it by +-4 px): (+114, -109) = (242, 103);
  * special (beam loop; the body, the head and the cannon do not move on its four frames): the
    cannon's muzzle (+103, -106) = (231, 106) is the beam origin, the visor's bright spot is at
    (+23, -156) = (151, 56).  The game draws the beam; the sheet shows the muzzle flare and the
    blazing visor.
`Titan.muzzle(pose)` and `Titan.visor(pose)` give these points for any pose (see `special_pose`).
Clips (packed several per row, `start` = first column): idle 8 (pistons hiss, the visor scans),
telegraph 6 (the piston fist winds back over its shoulder, the cannon spins up), walk 8 (= move,
heavy steps, footsteps f0 and f4), attack 5 (piston punch slam, `impact` f2), telegraph_special 6
(the reactor brightens, the vents open, the cannon levels), special 4 (loop: the beam), hurt 2,
summon 6 (its back hatch opens, three drones launch upward), roar 8 (phase change: steam blasts and
a horn; the phase-5 sheet's roar is the chest plate and the shoulder plate blowing off), death 14
(it sparks, buckles to its knees, collapses in a heap, its lights die).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, mat, ring
from boss_c_common import TAU, TIMING, Pose, dash_arc, shake
from common.palette import OUTLINE
from raster import TCanvas, ik2

PI = math.pi
W, H = 256, 224
GY = 212                      # ground line (feet bottom); the outline of the feet sits on row 211
PIVOT = [128, GY]
BOX = (90, 64, 166, GY)       # collision box x0, y0, x1, y1 (frame pixels)
WHITE = "#ffffff"
SEED = 51613

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Démarrage", "ambre", ["#fff0c8", "#ffc23c", "#f28c16", "#ac540e", "#582a08"]),
    ("Vapeur", "blanc orangé", ["#fff8ee", "#ffdcb0", "#ffa462", "#c8642c", "#642e12"]),
    ("Surtension", "bleu électrique", ["#e2f6ff", "#78d6ff", "#2a9cf6", "#1a58b6", "#0c2a62"]),
    ("Laser", "rouge", ["#ffe2e2", "#ff6c6c", "#ea2430", "#9e1020", "#500812"]),
    ("Surchauffe", "orange-rouge", ["#ffe4d0", "#ff7c3c", "#e6461c", "#9c2612", "#4e1208"]),
    ("Plasma", "magenta", ["#ffdef6", "#ff72de", "#da2eb6", "#8e167a", "#480a3e"]),
    ("Arc", "vert", ["#e4ffe2", "#7eff8c", "#24d254", "#148234", "#08401a"]),
    ("Cœur nucléaire", "blanc incandescent", ["#fffffa", "#f0fcff", "#c4f0ff", "#74c4f0", "#2e5c96"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Metal (never recoloured): blue-grey gunmetal plates, brass trims and gears, chrome piston rods.
GUN = mat("ti_gun", ["#b6c2ce", "#8a97a6", "#66717f", "#474f5b", "#2c323b"], th=[0.86, 0.68, 0.48, 0.28])
GUN_BK = mat("ti_gun_bk", ["#6f7886", "#545c68", "#3c424c", "#282d35"], th=[0.78, 0.55, 0.32])
BRASS = mat("ti_brass", ["#f4dc98", "#cba553", "#98773a", "#664c22", "#3c2a14"], th=[0.86, 0.66, 0.46, 0.26])
BRASS_BK = mat("ti_brass_bk", ["#a28a52", "#78623a", "#504026", "#2f2516"], th=[0.78, 0.55, 0.32])
STEEL = mat("ti_steel", ["#f2f6fa", "#c4ccd6", "#8c96a4", "#5c6572"], th=[0.8, 0.58, 0.36])
STEEL_BK = mat("ti_steel_bk", ["#9aa2ae", "#727a86", "#4e5560"], th=[0.66, 0.38])
HOSE = mat("ti_hose", ["#615d6b", "#46434f", "#2f2d36", "#1e1d23"], th=[0.74, 0.5, 0.28])
CAVITY = mat("ti_cavity", ["#363944", "#24262d", "#15161a"], th=[0.62, 0.34])
DEADG = mat("ti_dead", ["#6c707a", "#4d515a", "#363940", "#24262b"])  # extinct glass / lamps
# Light (recoloured per phase).
HOT = mat("ti_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("ti_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
# Effects (never recoloured).
STEAM = [C("#f6f4f0"), C("#dcdad8"), C("#b8b6bc"), C("#918f98")]
SMOKE = [C("#9a96a0"), C("#7c7884"), C("#625e6a"), C("#4a4752")]
DUST = [C("#aaa598"), C("#878276"), C("#666258")]
BOLT_L, BOLT_D = C("#8a97a6"), C("#474f5b")

# ---------------------------------------------------------------- rig (body space)
# Body space: origin at the hip centre, x forward (right), y down; rotated by `lean` about the hips.
# Seen a little from the front (three-quarter): the far shoulder (the cannon) is ahead of the near
# one (the piston fist), so both weapons show.
HIP_F, HIP_B = (-5.0, 0.0), (6.0, -3.0)
SH_F, SH_B = (-10.0, -66.0), (27.0, -63.0)        # shoulder joints
PAUL_F, PAUL_B = (-10.0, -73.0), (32.0, -70.0)    # shoulder plate centres
HEAD = (15.0, -88.0)                              # dome centre (at the base of the dome)
GRILLE = (25.0, -52.0)                            # the reactor behind its grille (chest)
CORE_C = (24.0, -50.0)                            # the reactor core (form II, torn chest)
L_UP = 38.0                                       # shoulder -> elbow (near arm)
L_HOUSING, L_ROD0, L_ROD1, L_FIST = 28.0, 4.0, 16.0, 16.0   # forearm: housing, rod (+piston), fist
L_UP_B = 24.0                                     # far upper arm (cannon arm)
CAN_BACK, CAN_HOUSE, CAN_BARREL, CAN_MUZZLE = -7.0, 28.0, 60.0, 67.0   # along the cannon from the elbow
L_THIGH, L_SHIN = 30.0, 28.0                      # hip -> knee -> ankle (ankle 12 px above the sole)

PELVIS = [(-26, -16), (-16, -22), (16, -22), (27, -14), (25, 2), (11, 10), (-12, 10), (-25, 2)]
WAIST = [(-23, -29), (21, -29), (23, -21), (-25, -21)]
HULL = [(-30, -26), (-36, -50), (-32, -72), (-17, -84), (12, -85), (30, -75), (40, -57), (38, -36), (26, -26)]
CHEST = [(8, -75), (30, -73), (40, -57), (38, -36), (26, -29), (10, -32), (4, -54)]
ABDO = [[(-4, -33), (26, -30), (30, -25), (-6, -26)]]
PACK = [(-27, -34), (-42, -38), (-50, -58), (-46, -78), (-30, -86), (-22, -62)]
HATCH = [(-30, -86), (-46, -78), (-50, -58), (-38, -53), (-34, -70), (-22, -80)]
HATCH_HINGE = (-30.0, -86.0)
BAY = [(-29, -82), (-44, -75), (-47, -58), (-30, -40), (-24, -60)]
STACKS = [((-20.0, -80.0), (-25.0, -103.0), 5.0), ((-32.0, -76.0), (-41.0, -97.0), 4.4)]
# Torn chest (form II): the hole and the jagged remains of the plate around it.
HOLE = [(9, -71), (17, -66), (24, -72), (31, -66), (38, -58), (35, -50), (39, -42), (32, -34), (24, -38),
        (16, -32), (9, -39), (6, -52), (11, -58)]
RIM = [[(8, -75), (30, -73), (35, -66), (31, -66), (24, -72), (17, -66), (9, -71)],
       [(38, -58), (40, -57), (38, -36), (26, -29), (16, -32), (24, -38), (32, -34), (39, -42), (35, -50)]]
CHEST_CRACKS = [[(17, -60), (13, -64), (10, -70)], [(31, -58), (34, -63), (31, -70)], [(33, -46), (37, -42), (36, -38)],
                [(18, -44), (14, -38), (16, -33)], [(16, -52), (10, -53), (7, -58)]]
DENTS = [(-20, -60, 5.0, 3.2, 0.4), (-10, -38, 4.0, 2.6, -0.3), (2, -14, 3.6, 2.4, 0.2)]

# Pauldron (near shoulder), relative to PAUL_F: two lames under its dome; the far one's shell.
PAUL_LAMES = [[(-20, 3), (21, 3), (19, 9), (-18, 9)], [(-17, 9), (18, 9), (16, 14), (-15, 14)]]
PAUL_B_SHELL = [(-15, 2), (-12, -9), (-2, -15), (10, -13), (17, -5), (17, 4), (-13, 5)]
# Head (head space: origin at the dome base centre, x forward).
FACE = [(-13, -1), (14, -1), (16, 5), (11, 11), (-6, 12), (-12, 6)]
# Fist (forearm space: origin at the fist centre, x towards the knuckles).
FIST_PTS = [(-14, -15), (4, -17), (14, -14), (17, -6), (17, 7), (13, 15), (-2, 17), (-14, 15), (-17, 0)]
FOOT_PTS = [(-17, -10), (-9, -14), (8, -14), (17, -9), (23, -4), (23, 0), (-18, 0), (-19, -5)]


class TPose(Pose):
    DEFAULTS = dict(t=0.0, hip=(126.0, 146.0), lean=0.03,
                    foot_f=(110.0, float(GY)), foot_b=(146.0, float(GY)), toe_f=0.0, toe_b=0.0,
                    fist=(174.0, 140.0), piston=0.3, fist_rot=0.0, elbow_f=(-6.0, 36.0),
                    arm_b=0.9, aim=0.06, spin=0.0,
                    head=(0.0, 0.0), tilt=0.0, paul=(0.0, 0.0),
                    glow=0.6, reactor=0.6, visor=0.7, scan=0.5, muzzle=0.0, vents=0.0,
                    broken=False, fly=0.0, crackle=0.0, hatch=0.0, dead=0.0, slide=0.0, bare=False)
    # fist: near fist centre (frame); piston: extension of the forearm rod 0..1 (+16 px)
    # arm_b: far upper arm angle from straight down (+ = forward); aim: cannon angle (0 = forward)
    # spin: barrel rotation (0..1 = a sixth of a turn per 1/6); paul: near shoulder plate offset
    # glow: cable lights / vents 0..1.5; reactor 0..1.5; visor 0..1.5, scan: visor spot 0..1
    # muzzle: glow at the barrels' mouths 0..1.5; vents: shoulder vents opening 0..1
    # broken: form II; fly: the chest plate and the shoulder plate blowing off (phase-5 roar);
    # crackle: glowing cracks on the whole chest plate (form I, before it blows off); slide: the
    # shoulder plate sliding off (death, form I); bare: the shoulder plate gone (the death heap)
    # hatch: the back hatch opening (summon) 0..1; dead: the light dying 0..1 (1 = extinct)


def _hash(ix, iy, seed):
    h = np.sin(ix * 12.9898 + iy * 78.233 + seed * 37.719) * 43758.5453
    return h - np.floor(h)


cv_y, cv_x = np.mgrid[0:H, 0:W].astype(np.float32) + 0.5


def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 3 <= y < f.shape[0] - 3 and 3 <= x < f.shape[1] - 3 and f[y, x, 3] == 0:
        f[y, x] = c


def unit(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = math.hypot(dx, dy) or 1.0
    return dx / d, dy / d


def frame_of(o, d):
    """Local mapping along direction d from o: x along d, y across (y = +90 deg of d)."""
    dx, dy = d

    def L(x, y):
        return (o[0] + x * dx - y * dy, o[1] + x * dy + y * dx)
    return L


class Titan:
    id = "titan"
    W, H, PIVOT = W, H, PIVOT
    seed = SEED
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 7, 8, 12
    ATTACK_EVENTS = [(2, "impact")]
    WALK_EVENTS = [(0, "footstep"), (4, "footstep")]

    # ------------------------------------------------------------ geometry
    def B(self, p, q, off=(0.0, 0.0)):
        """Body space -> frame."""
        x, y = q[0] + off[0], q[1] + off[1]
        c, s = math.cos(p.lean), math.sin(p.lean)
        return (p.hip[0] + x * c - y * s, p.hip[1] + x * s + y * c)

    def head_L(self, p):
        hc = self.B(p, (HEAD[0] + p.head[0], HEAD[1] + p.head[1]))
        a = p.lean + p.tilt
        c, s = math.cos(a), math.sin(a)
        return lambda x, y: (hc[0] + x * c - y * s, hc[1] + x * s + y * c)

    def visor(self, p):
        """The visor's bright spot (frame pixels): origin of the visor beam."""
        return self.head_L(p)(13.0, -5.0)

    def shoulder(self, p, near):
        if near:
            return self.B(p, SH_F, p.paul)
        return self.B(p, SH_B)

    def l_fore(self, p):
        return L_HOUSING + L_ROD0 + L_ROD1 * p.piston + L_FIST

    def elbow(self, p):
        sh, fi = self.shoulder(p, True), p.fist
        hx, hy = p.elbow_f
        best = None
        for bend in (1.0, -1.0):
            e = ik2(sh, fi, L_UP, self.l_fore(p), bend)
            d = (e[0] - sh[0] - hx) ** 2 + (e[1] - sh[1] - hy) ** 2
            if best is None or d < best[0]:
                best = (d, e)
        return best[1]

    def cannon_elbow(self, p):
        sh = self.shoulder(p, False)
        a = p.arm_b
        return (sh[0] + math.sin(a) * L_UP_B, sh[1] + math.cos(a) * L_UP_B)

    def cannon_L(self, p):
        e = self.cannon_elbow(p)
        return frame_of(e, (math.cos(p.aim), math.sin(p.aim)))

    def muzzle(self, p):
        """The cannon's muzzle (frame pixels): origin of the cannon's shots and beam."""
        return self.cannon_L(p)(CAN_MUZZLE + 1.0, 0.0)

    def reactor(self, p):
        return self.B(p, CORE_C if p.broken else GRILLE)

    def origins(self):
        """The documented origin points, from the pivot (x forward, y down; frame pixels)."""
        def rel(q):
            return (round(q[0] - PIVOT[0]), round(q[1] - PIVOT[1]))
        sp = self.special_pose(0)
        return {"impact": rel((IMPACT[0] + 1, IMPACT[1])),            # attack f2 is shaken 1 px forward
                "muzzle": rel(self.muzzle(TPose())), "special_muzzle": rel(self.muzzle(sp)),
                "special_visor": rel(self.visor(sp))}

    # ------------------------------------------------------------ light
    def light(self, p):
        return DEADG if p.dead >= 1.0 else HOT

    def core_m(self, p):
        return DEADG if p.dead >= 1.0 else CORE

    def li(self, p, v):
        """Intensity of a light pixel (dimmed while dying)."""
        if p.dead >= 1.0:
            return 0.25 + 0.3 * min(1.0, v)
        return v * (1.0 - 0.55 * p.dead)

    # ------------------------------------------------------------ primitives
    def plate(self, cv, pts, m, base=0.56, bevel=2.2, grad=0.16, line=True):
        """A metal plate (frame-space polygon): bevelled edges lit from the top left, a soft
        gradient from the top and a diagonal sheen."""
        cv.part(line=line)
        mask = cv.poly(pts, m, bevel=bevel, base=base)
        if mask is None or not mask.any():
            return mask
        ys = [q[1] for q in pts]
        y0, y1 = min(ys), max(ys)
        g = grad * (0.5 - (cv.py - y0) / max(1.0, y1 - y0))
        xs = [q[0] for q in pts]
        u = (cv.px - min(xs)) + (cv.py - y0) * 0.8
        sheen = np.where(np.abs(u - 0.32 * (max(xs) - min(xs) + 0.8 * (y1 - y0))) < 2.2, 0.07, 0.0)
        cv.inten[mask] = np.clip(cv.inten[mask] + g[mask] + sheen[mask], 0, 1)
        return mask

    def body_plate(self, cv, p, pts, m, off=(0.0, 0.0), **kw):
        return self.plate(cv, [self.B(p, q, off) for q in pts], m, **kw)

    def dome(self, cv, c, rx, ry, ang, m, bias=0.04):
        """A dome: the upper half of a shaded ellipse (local y < 0 above the base line through c)."""
        cv.part()
        sa, ca = math.sin(ang), math.cos(ang)
        return cv.ellipse(c, rx, ry, m, ang=ang, bias=bias,
                          within=lambda x, y: ((x - c[0]) * -sa + (y - c[1]) * ca) <= 0)

    def rivets(self, cv, pts, m):
        """Rivet heads: a lit pixel and its shadow (decals)."""
        cv.part(line=False)
        for (x, y) in pts:
            cv.px_set(x, y, m, inten=0.97, decal=True)
            cv.px_set(x + 1, y + 1, m, inten=0.12, decal=True)

    def seam(self, cv, a, b, m, lit=True):
        """A panel line: a dark groove with a lit lip under it (decals)."""
        cv.part(line=False)
        cv.line(a, b, m, inten=0.1, decal=True)
        if lit:
            cv.line((a[0], a[1] + 1), (b[0], b[1] + 1), m, inten=0.8, decal=True)

    def gear(self, cv, c, r, m, ang, teeth=8, hub=True):
        """A brass gear: teeth around a shaded disc, a dark hub ring and the axle."""
        cv.part()
        for k in range(teeth):
            a = ang + TAU * k / teeth
            ca, sa = math.cos(a), math.sin(a)
            cv.capsule((c[0] + ca * (r - 1), c[1] + sa * (r - 1)), (c[0] + ca * (r + 1.0), c[1] + sa * (r + 1.0)), 1.3,
                       1.3, m, flat=0.5 + 0.25 * (-ca * 0.6 - sa * 0.8))
        cv.disc(c, r, m, bias=0.02)
        if hub:
            cv.part(line=False)
            cv.disc(c, r * 0.5, m, flat=0.2, decal=True)
            cv.disc(c, r * 0.3, m, flat=0.82, decal=True)
            for k in range(4):
                a = ang + TAU * (k + 0.5) / 4
                cv.px_set(c[0] + math.cos(a) * r * 0.72, c[1] + math.sin(a) * r * 0.72, m, inten=0.15, decal=True)

    def lamp(self, cv, p, x, y, v=0.7, m=None):
        """A one-pixel cable light."""
        cv.part(line=False)
        cv.px_set(x, y, m or self.light(p), inten=self.li(p, v))

    def hose(self, cv, pts, r, m=HOSE, ribs=True):
        """A ribbed hose along a polyline."""
        cv.part()
        cv.chain(pts, [r] * len(pts), m)
        if ribs:
            cv.part(line=False)
            acc = 0.0
            for a, b in zip(pts[:-1], pts[1:]):
                seg = math.hypot(b[0] - a[0], b[1] - a[1])
                ux, uy = unit(a, b)
                d = 3.0 - acc % 3.0
                while d < seg:
                    x, y = a[0] + ux * d, a[1] + uy * d
                    cv.line((x - uy * r, y + ux * r), (x + uy * r * 0.2, y - ux * r * 0.2), m, inten=0.12, decal=True)
                    d += 3.0
                acc += seg

    def piston(self, cv, a, b, r, m, br, rod=STEEL):
        """A hydraulic piston from a to b: a housing (60 %) with a brass collar, a chrome rod."""
        ux, uy = unit(a, b)
        ln = math.hypot(b[0] - a[0], b[1] - a[1])
        mid = (a[0] + ux * ln * 0.6, a[1] + uy * ln * 0.6)
        cv.part()
        cv.capsule(mid, b, r * 0.5, r * 0.5, rod)
        cv.part()
        cv.capsule(a, mid, r, r, m)
        cv.part(line=False)
        L = frame_of(mid, (ux, uy))
        cv.poly([L(-3, -r - 0.4), L(-1, -r - 0.4), L(-1, r + 0.4), L(-3, r + 0.4)], br, flat=0.6, decal=True)
        return mid

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = TCanvas(W, H)
        self._cannon_arm(cv, p)
        self._leg(cv, p, near=False)
        self._back(cv, p)
        self._torso(cv, p)
        self._head(cv, p)
        self._leg(cv, p, near=True)
        self._upper_arm(cv, p)
        self._pauldron(cv, p)
        self._fore_arm(cv, p)
        img = cv.render()
        # nothing of the body below the ground line: feet, knees and fists rest on it
        img[GY:] = 0
        row = img[GY - 1]
        row[row[:, 3] > 0] = OUTLINE
        return img

    # ---- legs
    def leg(self, p, near):
        """Leg geometry: hip, knee, ankle (frame) and the foot / thigh / shin local mappings."""
        hip = self.B(p, HIP_F if near else HIP_B)
        foot = p.foot_f if near else p.foot_b
        toe = p.toe_f if near else p.toe_b
        fx, fy = foot[0], foot[1] - 1.0
        ct, st = math.cos(-toe), math.sin(-toe)

        def F(x, y):  # foot space: origin at the heel-bottom centre, toe tilt about the heel
            return (fx + x * ct - y * st, fy + x * st + y * ct)
        ankle = F(-2.0, -13.0)
        knee = ik2(hip, ankle, L_THIGH, L_SHIN, 1.0)
        T = frame_of(hip, unit(hip, knee))        # thigh space: x down the thigh, y towards the back
        S = frame_of(knee, unit(knee, ankle))     # shin space
        return hip, knee, ankle, F, T, S

    def _leg(self, cv, p, near):
        m = GUN if near else GUN_BK
        br = BRASS if near else BRASS_BK
        hip, knee, ankle, F, T, S = self.leg(p, near)
        # calf piston (behind the shin), from the back of the knee to the heel
        self.piston(cv, S(2, 9), F(-13, -9), 3.4, m, br, rod=STEEL if near else STEEL_BK)
        cv.part()
        cv.capsule(hip, knee, 12.5, 10.5, m)
        cv.part()
        cv.capsule(knee, ankle, 10.0, 8.0, m)
        # foot: a broad armoured sole, toe cap, heel spur
        self.plate(cv, [F(*q) for q in FOOT_PTS], m, base=0.52, bevel=2.0, grad=0.22)
        self.plate(cv, [F(10, -12), F(17, -9), F(23, -4), F(23, 0), F(10, 0)], br, base=0.55, bevel=1.6, grad=0.1)
        self.seam(cv, F(-14, -4), F(9, -4), m)
        cv.part()
        cv.disc(ankle, 6.5, m)
        if near:
            self.rivets(cv, [F(-12, -8), F(2, -10)], m)
        # thigh armour (front) and shin plate (front)
        self.plate(cv, [T(1, 4), T(3, -13), T(26, -12), T(31, -5), T(28, 6)], m, base=0.6, bevel=2.2, grad=0.12)
        self.seam(cv, T(14, -11), T(14, 3), m, lit=False)
        self.plate(cv, [S(5, -9), S(4, 4), S(26, 5), S(30, -2), S(27, -9)], m, base=0.58, bevel=2.0, grad=0.1)
        if near:
            self.rivets(cv, [T(6, -9), T(22, -9), S(9, -6), S(23, -6)], m)
            # a cable along the thigh, with lights
            self.hose(cv, [T(4, 7), T(16, 10), T(28, 8)], 1.6, ribs=False)
            for u in (9, 19):
                self.lamp(cv, p, *T(u, 9.5), v=0.55 + 0.25 * min(1.2, p.glow))
        # knee: a gear under a knee cap
        self.gear(cv, knee, 6.5 if near else 5.5, br, 0.6 * p.t + (0.3 if near else 0.0), teeth=8)
        self.plate(cv, [S(-7, -4), S(-5, -12), S(4, -13), S(9, -7), S(6, -2)], m, base=0.62, bevel=1.8, grad=0.1)

    # ---- back: drone bay, exhaust stacks
    def _back(self, cv, p):
        L = lambda x, y: self.B(p, (x, y))
        self.body_plate(cv, p, PACK, GUN, base=0.48, bevel=2.4, grad=0.2)
        if p.hatch > 0.02:
            # the bay: a dark hold with glowing launch rails
            cv.part()
            cv.poly([L(*q) for q in BAY], CAVITY, bevel=1.6, base=0.4)
            cv.part(line=False)
            for k in range(3):
                y = -74 + 12 * k
                cv.line(L(-42 + 2 * k, y), L(-29, y + 3), self.light(p), inten=self.li(p, 0.5 + 0.35 * p.hatch),
                        decal=True)
        else:
            self.seam(cv, L(-44, -62), L(-30, -60), GUN)
            self.rivets(cv, [L(-42, -44), L(-47, -58), L(-43, -73)], GUN)
        self._hatch(cv, p)
        for (a, b, r), sd in zip(STACKS, (0, 1)):
            self._stack(cv, p, L(*a), L(*b), r, sd)

    def _hatch(self, cv, p):
        h = p.hatch
        ang = 1.75 * h * h * (3 - 2 * h)
        hx, hy = HATCH_HINGE
        ca, sa = math.cos(ang), math.sin(ang)
        pts = [(hx + (x - hx) * ca - (y - hy) * sa, hy + (x - hx) * sa + (y - hy) * ca) for x, y in HATCH]
        fp = [self.B(p, q) for q in pts]
        self.plate(cv, fp, GUN, base=0.56 if h < 0.5 else 0.44, bevel=2.0, grad=0.16)
        m = lambda x, y: self.B(p, (hx + (x - hx) * ca - (y - hy) * sa, hy + (x - hx) * sa + (y - hy) * ca))
        self.seam(cv, m(-44, -68), m(-35, -77), GUN)
        self.rivets(cv, [m(-46, -64), m(-40, -76)], GUN)
        # a warning stripe of brass on its edge
        self.plate(cv, [m(-50, -58), m(-44, -56), m(-42, -62), m(-48, -63)], BRASS, base=0.5, bevel=1.2, grad=0.0)

    def _stack(self, cv, p, a, b, r, k):
        """An exhaust stack: a ribbed pipe, a brass collar, a slanted mouth glowing inside."""
        ux, uy = unit(a, b)
        cv.part()
        cv.capsule(a, b, r, r * 0.92, GUN_BK if k else GUN)
        cv.part(line=False)
        for d in (5.0, 9.0):
            q = (a[0] + ux * d, a[1] + uy * d)
            cv.line((q[0] - uy * r, q[1] + ux * r), (q[0] + uy * r, q[1] - ux * r), GUN, inten=0.15, decal=True)
        c = (b[0] - ux * 3, b[1] - uy * 3)
        cv.part()
        cv.capsule((c[0] - ux * 1.5, c[1] - uy * 1.5), (c[0] + ux * 1.5, c[1] + uy * 1.5), r + 1.4, r + 1.4,
                   BRASS_BK if k else BRASS)
        # the mouth (seen slanted): dark rim, light inside
        cv.part(line=False)
        ang = math.atan2(uy, ux) + PI / 2
        cv.ellipse(b, r + 0.6, 2.4, CAVITY, ang=ang, flat=0.2)
        v = 0.45 + 0.3 * min(1.2, p.glow) + 0.35 * p.vents
        cv.ellipse(b, r - 0.6, 1.3, self.light(p), ang=ang, flat=self.li(p, v))

    # ---- torso
    def _torso(self, cv, p):
        L = lambda x, y: self.B(p, (x, y))
        self.body_plate(cv, p, HULL, GUN, base=0.46, bevel=2.8, grad=0.24)
        self.seam(cv, L(-34, -48), L(-4, -46), GUN)
        self.seam(cv, L(-30, -70), L(-6, -72), GUN)
        self.seam(cv, L(-6, -82), L(-4, -30), GUN, lit=False)
        self.rivets(cv, [L(-30, -32), L(-33, -44), L(-31, -56), L(-28, -66), L(-12, -79), L(-8, -40), L(-8, -54)], GUN)
        # waist: a brass turret ring; pelvis armour
        self.body_plate(cv, p, PELVIS, GUN, base=0.5, bevel=2.4, grad=0.2)
        self.seam(cv, L(-22, -4), L(22, -6), GUN)
        self.rivets(cv, [L(-20, -12), L(-6, -14), L(8, -14), L(20, -10)], GUN)
        self.body_plate(cv, p, WAIST, BRASS, base=0.5, bevel=1.4, grad=0.2)
        cv.part(line=False)
        for k in range(9):
            x = -20 + 5 * k
            cv.line(L(x, -28), L(x, -22), BRASS, inten=0.15, decal=True)
        for pts in ABDO:
            self.body_plate(cv, p, pts, GUN, base=0.58, bevel=1.6, grad=0.1)
        if p.broken:
            self._torn(cv, p)
            if p.fly > 0:
                self._flying_plate(cv, p)
        else:
            self._chest(cv, p)
        if p.broken:
            for (x, y, rx, ry, a) in DENTS:
                self.dent(cv, L(x, y), rx, ry, p.lean + a, GUN)

    def dent(self, cv, c, rx, ry, ang, m):
        """A dent: a shadowed hollow lit on its lower edge, with a scratch."""
        cv.part(line=False)
        cv.ellipse(c, rx, ry, m, ang=ang, flat=0.32, decal=True)
        cv.ellipse((c[0] + 0.6, c[1] + 0.8), rx * 0.7, ry * 0.6, m, ang=ang, flat=0.62, decal=True)
        cv.line((c[0] - rx - 2, c[1] - 1), (c[0] - rx + 1, c[1] + 1), m, inten=0.92, decal=True)

    def _flying_plate(self, cv, p):
        """The torn-off piece of the chest plate (with its grille) blown forward (phase-5 roar)."""
        fly = p.fly
        off = (46 * fly, -40 * fly + 30 * fly * fly)
        rot = 1.4 * fly
        cx, cy = GRILLE
        cr, sr = math.cos(rot), math.sin(rot)

        def L(x, y):
            return self.B(p, (cx + (x - cx) * cr - (y - cy) * sr + off[0], cy + (x - cx) * sr + (y - cy) * cr + off[1]))
        self.plate(cv, [L(*q) for q in HOLE], GUN, base=0.6, bevel=2.2, grad=0.2)
        cv.part()
        cv.disc(L(cx, cy), 10.5, BRASS, bias=0.04)
        cv.part()
        cv.disc(L(cx, cy), 8.5, CAVITY, flat=0.22)
        cv.part(line=False)
        for k in (-4.5, -1.5, 1.5, 4.5):
            h = math.sqrt(max(0.0, 81 - k * k)) - 0.6
            cv.line(L(cx + k, cy - h), L(cx + k, cy + h), GUN, inten=0.45)
        self.vein(cv, p, [L(9, -71), L(17, -66), L(24, -72), L(31, -66)], 0.7)

    def _chest(self, cv, p):
        """Form I: the chest plate, the reactor glowing behind its grille, cable lights."""
        L = lambda x, y: self.B(p, (x, y))
        self.plate(cv, [L(*q) for q in CHEST], GUN, base=0.6, bevel=2.4, grad=0.2)
        self.seam(cv, L(8, -36), L(28, -33), GUN)
        self.rivets(cv, [L(11, -71), L(28, -70), L(37, -55), L(35, -40), L(9, -50)], GUN)
        if p.crackle > 0:
            # the plate splitting: glowing cracks running from the grille (phase-5 roar)
            n = int(round(p.crackle * len(CHEST_CRACKS)))
            for pts in CHEST_CRACKS[:n]:
                self.vein(cv, p, [L(*q) for q in pts], 0.75)
        gx, gy = self.B(p, GRILLE)
        r = 9.0
        cv.part()
        cv.disc((gx, gy), r + 2.2, BRASS, bias=0.04)
        cv.part()
        cv.disc((gx, gy), r, CAVITY, flat=0.2)
        cv.part(line=False)
        rv = min(1.5, p.reactor)
        d = np.hypot(cv.px - gx, cv.py - gy)
        m = d < r - 0.5
        cv.put(m, self.li(p, np.clip(0.88 + 0.12 * rv - d * 0.07, 0.12, 1.0)), self.core_m(p))
        # grille bars over the glow
        c, s = math.cos(p.lean), math.sin(p.lean)
        for k in (-4.5, -1.5, 1.5, 4.5):
            h = math.sqrt(max(0.0, r * r - k * k)) - 0.4
            a = (gx + k * c + h * s, gy + k * s - h * c)
            b = (gx + k * c - h * s, gy + k * s + h * c)
            cv.part(line=False)
            cv.line(a, b, GUN, inten=0.42)
        cv.line((gx - r * c, gy - r * s), (gx + r * c, gy + r * s), GUN, inten=0.36)
        # rivets on the brass rim, cable lights under the grille
        self.rivets(cv, [(gx + math.cos(a) * (r + 1.2), gy + math.sin(a) * (r + 1.2)) for a in (-2.4, -0.7, 0.9, 2.5)],
                    BRASS)
        for k in range(3):
            self.lamp(cv, p, *self.B(p, (16 + 5 * k, -38)), v=0.5 + 0.3 * min(1.2, p.glow) * (1 if k != 1 else 1.2))

    def vein(self, cv, p, pts, inten=0.6):
        cv.part(line=False)
        for a, b in zip(pts[:-1], pts[1:]):
            cv.line(a, b, self.light(p), inten=self.li(p, inten), decal=True)

    def _core(self, cv, p):
        """The reactor core, bared: a dark well, a blazing sphere held by brass rings."""
        L = lambda x, y: self.B(p, (x, y))
        cv.part()
        mask = cv.poly([L(*q) for q in HOLE], CAVITY, bevel=2.0, base=0.3)
        cx, cy = L(*CORE_C)
        rv = min(1.5, p.reactor)
        if mask is not None and mask.any() and p.dead < 1.0:
            d = np.hypot(cv.px - cx, cv.py - cy)
            spill = mask & (d < 16 + 3 * rv)
            cv.part(line=False)
            cv.put(spill, self.li(p, np.clip(0.42 - 0.022 * d, 0.05, 0.42)), HOT)
        # coils behind the sphere
        for k, (dx, dy) in enumerate(((-9, -11), (9, -10), (-10, 9), (10, 10))):
            cv.part()
            cv.disc((cx + dx * 0.9, cy + dy * 0.9), 3.0, BRASS_BK, bias=0.02)
        r = 9.5 + 0.8 * rv
        d = np.hypot(cv.px - cx, cv.py - cy)
        cv.part(line=False)
        cv.put(d < r, self.li(p, np.clip(1.08 + 0.12 * rv - d * 0.06, 0.15, 1.0)), self.core_m(p))
        # containment rings (seen at an angle)
        for ang, rx in ((0.5, r + 2.5), (-0.7, r + 1.5)):
            cv.part(line=False)
            ca, sa = math.cos(ang), math.sin(ang)
            u = ((cv.px - cx) * ca + (cv.py - cy) * sa) / rx
            v = (-(cv.px - cx) * sa + (cv.py - cy) * ca) / (rx * 0.32)
            q = u * u + v * v
            ring_m = (q < 1.0) & (q > 0.45)
            front = ring_m & (v > 0)
            cv.put(front, 0.6 + 0.3 * u, BRASS)

    def _torn(self, cv, p):
        """Form II: the chest plate torn off - the reactor in its well, jagged remains, cables."""
        L = lambda x, y: self.B(p, (x, y))
        self._core(cv, p)
        for k, pts in enumerate(RIM):
            self.plate(cv, [L(*q) for q in pts], GUN, base=0.58, bevel=1.8, grad=0.12)
        self.rivets(cv, [L(12, -73), L(27, -72), L(38, -45)], GUN)
        # torn edges glowing hot
        self.vein(cv, p, [L(9, -71), L(17, -66), L(24, -72), L(31, -66)], 0.62)
        self.vein(cv, p, [L(32, -34), L(24, -38), L(16, -32)], 0.55)
        # hanging cables, sparking at their ends
        for (a, b, c) in (((14, -40), (12, -32), (15, -24)), ((33, -42), (36, -33), (33, -25))):
            self.hose(cv, [L(*a), L(*b), L(*c)], 1.4, ribs=False)
            self.lamp(cv, p, *L(c[0], c[1] + 1), v=0.95)

    # ---- head
    def _head(self, cv, p):
        L = self.head_L(p)
        a = p.lean + p.tilt
        hc = L(0, 0)
        # neck collar
        cv.part()
        cv.capsule(L(-9, 6), L(9, 6), 6.0, 6.0, BRASS_BK)
        # the dome
        self.dome(cv, hc, 16.0, 15.5, a, GUN, bias=0.08)
        # crest ridge over the dome (brass), along its top
        cv.part()
        pts = [L(math.cos(t) * 13.8, -math.sin(t) * 13.4) for t in np.linspace(0.75, 2.5, 8)]
        cv.chain(pts, [1.5] * len(pts), BRASS, bias=0.06)
        # face plate (chin) under the dome
        self.plate(cv, [L(*q) for q in FACE], GUN, base=0.5, bevel=1.8, grad=0.2)
        self.rivets(cv, [L(-9, 3), L(7, 4)], GUN)
        # visor slit across the front of the dome: a dark recess with the light in it
        cv.part(line=False)
        cv.poly([L(-1, -7.5), L(15.2, -7.5), L(16.4, -1.6), L(-1, -1.6)], CAVITY, flat=0.1)
        lm = self.light(p)
        e = min(1.5, p.visor)
        cv.line(L(0, -5), L(15, -5), lm, inten=self.li(p, 0.5 + 0.15 * e))
        cv.line(L(1, -6), L(14, -6), lm, inten=self.li(p, 0.36 + 0.12 * e))
        cv.line(L(1, -4), L(15, -4), lm, inten=self.li(p, 0.36 + 0.12 * e))
        sx = 2 + 12 * p.scan
        mc = self.core_m(p)
        cv.line(L(sx - 2, -5), L(sx + 2, -5), mc, inten=self.li(p, 0.6 + 0.12 * e))
        cv.px_set(*L(sx, -5), mc, inten=self.li(p, 0.8 + 0.12 * e))
        if e > 1.1:
            cv.line(L(sx - 1, -6), L(sx + 1, -6), mc, inten=self.li(p, 0.6))
            cv.line(L(sx - 1, -4), L(sx + 1, -4), mc, inten=self.li(p, 0.6))
        # small antenna at the back
        cv.part()
        cv.capsule(L(-9, -12), L(-14, -22), 1.2, 0.9, STEEL)
        self.lamp(cv, p, *L(-14.5, -23), v=0.6 + 0.3 * min(1.2, p.glow))
        if p.broken:
            self.dent(cv, L(-4, -9), 3.2, 2.0, a + 0.3, GUN)

    # ---- near arm: piston fist
    def _upper_arm(self, cv, p):
        sh, el = self.shoulder(p, True), self.elbow(p)
        cv.part()
        cv.capsule(sh, el, 11.0, 10.0, GUN)
        U = frame_of(sh, unit(sh, el))
        self.seam(cv, U(18, -9), U(18, 9), GUN)
        # a hose from the back down to the forearm housing
        hx, hy = self.B(p, (-26, -46))
        cv.part()
        el = self.elbow(p)
        lf = math.hypot(p.fist[0] - el[0], p.fist[1] - el[1])
        fh = self.fore_L(p)(-lf + 6, -9)
        mid = ((hx + fh[0]) / 2 - 6, max(hy, fh[1]) + 10)
        self.hose(cv, [(hx, hy), ((hx + mid[0]) / 2, (hy + mid[1]) / 2 + 2), mid, fh], 2.2)

    def fore_L(self, p):
        """Forearm space: origin at the fist centre, x towards the knuckles."""
        el = self.elbow(p)
        d = unit(el, p.fist)
        return frame_of(p.fist, d)

    def _pauldron(self, cv, p):
        c0 = PAUL_F
        if p.broken or p.bare:
            self._bare_shoulder(cv, p)
            if p.fly <= 0:
                return
        off = (p.paul[0], p.paul[1])
        rot = 0.0
        if p.fly > 0:
            off = (off[0] - 26 * p.fly, off[1] - 46 * p.fly + 34 * p.fly * p.fly)
            rot = -1.3 * p.fly
        if p.slide > 0:
            off = (off[0] - 16 * p.slide, off[1] + 10 * p.slide * p.slide)
            rot = -0.9 * p.slide
        cr, sr = math.cos(rot), math.sin(rot)

        def L(x, y):
            return self.B(p, (c0[0] + x * cr - y * sr, c0[1] + x * sr + y * cr), off)
        for pts in reversed(PAUL_LAMES):
            self.plate(cv, [L(*q) for q in pts], GUN, base=0.5, bevel=1.6, grad=0.14)
        self.dome(cv, L(0, 3), 23.0, 21.0, p.lean + rot, GUN, bias=0.1)
        # brass trim on the lower edge of the shell, rivets, the vent louvres on top
        self.plate(cv, [L(-21, 1), L(22, 1), L(21, 4), L(-20, 4)], BRASS, base=0.56, bevel=1.0, grad=0.0)
        self.rivets(cv, [L(-17, 2), L(-6, 2), L(6, 2), L(17, 2)], BRASS)
        self.rivets(cv, [L(-14, -9), L(12, -12)], GUN)
        cv.part(line=False)
        v = 0.42 + 0.28 * min(1.2, p.glow) + 0.4 * p.vents
        for k in range(3):
            x0 = -9 + 6 * k
            cv.line(L(x0, -14 + abs(k - 1)), L(x0 + 3, -15 + abs(k - 1)), CAVITY, inten=0.1, decal=True)
            cv.line(L(x0, -13 + abs(k - 1)), L(x0 + 3, -14 + abs(k - 1)), self.light(p), inten=self.li(p, v),
                    decal=True)

    def _bare_shoulder(self, cv, p):
        """Form II: the shoulder plate is gone - a bare joint (gear, brackets), sparking cables."""
        sh = self.shoulder(p, True)
        L = lambda x, y: self.B(p, (PAUL_F[0] + x, PAUL_F[1] + y))
        self.plate(cv, [L(-16, 6), L(-14, -6), L(-4, -10), L(6, -6), L(4, 4)], GUN_BK, base=0.5, bevel=1.4, grad=0.1)
        self.gear(cv, sh, 8.0, BRASS, 0.3 * p.t, teeth=10)
        # broken brackets
        self.plate(cv, [L(-20, 2), L(-17, -8), L(-13, -7), L(-15, 3)], GUN, base=0.55, bevel=1.2, grad=0.1)
        self.plate(cv, [L(10, -4), L(17, -9), L(19, -5), L(13, 1)], GUN, base=0.55, bevel=1.2, grad=0.1)
        # cables torn out, sparking
        for (a, b, c) in (((-6, -6), (-11, -15), (-8, -20)), ((2, -8), (6, -16), (11, -17))):
            self.hose(cv, [L(*a), L(*b), L(*c)], 1.3, ribs=False)
            self.lamp(cv, p, *L(c[0], c[1] - 1), v=0.95)

    def _fore_arm(self, cv, p):
        el = self.elbow(p)
        A = self.fore_L(p)
        a = math.atan2(p.fist[1] - el[1], p.fist[0] - el[0])
        lf = math.hypot(p.fist[0] - el[0], p.fist[1] - el[1])   # elbow -> fist centre
        h0 = A(-lf, 0)                         # the elbow
        h1 = A(-lf + L_HOUSING, 0)             # the housing's mouth
        rod_end = A(-L_FIST + 1, 0)
        # chrome rod, then the housing (a big cylinder with brass bands), the elbow gear
        cv.part()
        cv.capsule(h1, rod_end, 5.0, 5.0, STEEL)
        cv.part()
        cv.capsule(h0, h1, 12.0, 11.0, GUN)
        cv.part(line=False)
        for u in (7.0, L_HOUSING - 4.0):
            cv.poly([A(-lf + u - 1.4, -12.5), A(-lf + u + 1.4, -12.5), A(-lf + u + 1.4, 12.5), A(-lf + u - 1.4, 12.5)],
                    BRASS, flat=0.6, decal=True)
        cv.part()
        cv.poly([A(-lf + L_HOUSING - 1.5, -8), A(-lf + L_HOUSING + 1.5, -7), A(-lf + L_HOUSING + 1.5, 7),
                 A(-lf + L_HOUSING - 1.5, 8)], BRASS, bevel=1.0, base=0.6)
        self.rivets(cv, [A(-lf + 14, -8), A(-lf + 20, -8)], GUN)
        self.lamp(cv, p, *A(-lf + 14, 5), v=0.55 + 0.3 * min(1.2, p.glow))
        self.lamp(cv, p, *A(-lf + 18, 5), v=0.45 + 0.3 * min(1.2, p.glow))
        self.gear(cv, el, 7.5, BRASS, -0.5 * p.t, teeth=9)
        self._fist(cv, p, p.fist, a + p.fist_rot)
        if p.broken:
            self.dent(cv, A(-lf + 10, -3), 3.4, 2.2, a, GUN)

    def _fist(self, cv, p, c, a):
        ca, sa = math.cos(a), math.sin(a)

        def F(x, y):
            return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca)
        # the cuff (wrist block), the back of the hand, four knuckle blocks, the thumb
        self.plate(cv, [F(-20, -12), F(-12, -13), F(-12, 13), F(-20, 12)], GUN, base=0.48, bevel=1.6, grad=0.1)
        self.plate(cv, [F(*q) for q in FIST_PTS], GUN, base=0.6, bevel=2.6, grad=0.16)
        for k, y in enumerate((-11.5, -4.0, 3.5, 11.0)):
            self.plate(cv, [F(10, y - 3.5), F(18, y - 3.0), F(19, y + 3.0), F(10, y + 3.5)], GUN, base=0.62,
                       bevel=1.4, grad=0.1)
        # brass knuckle plate across the fingers
        self.plate(cv, [F(5, -16), F(10, -16), F(10, 16), F(5, 16)], BRASS, base=0.56, bevel=1.2, grad=0.1)
        self.rivets(cv, [F(7, -12), F(7, -4), F(7, 4), F(7, 12)], BRASS)
        cv.part()
        cv.capsule(F(-6, -15), F(6, -17), 4.4, 3.8, GUN, bias=0.04)
        self.rivets(cv, [F(-8, -6), F(-8, 6)], GUN)
        self.seam(cv, F(-4, -10), F(-4, 12), GUN, lit=False)

    # ---- far arm: the rotary cannon
    def _cannon_arm(self, cv, p):
        sh = self.shoulder(p, False)
        el = self.cannon_elbow(p)
        Lb = lambda x, y: self.B(p, (PAUL_B[0] + x, PAUL_B[1] + y))
        # far shoulder plate (behind the head) and upper arm
        self.plate(cv, [Lb(*q) for q in PAUL_B_SHELL], GUN_BK, base=0.6, bevel=2.4, grad=0.22)
        self.plate(cv, [Lb(-14, 2), Lb(17, 2), Lb(16, 5), Lb(-13, 5)], BRASS_BK, base=0.6, bevel=1.0, grad=0.0)
        cv.part()
        cv.capsule(sh, el, 9.0, 8.0, GUN_BK)
        self._cannon(cv, p)
        cv.part()
        cv.disc(el, 6.5, GUN_BK)

    def _cannon(self, cv, p):
        A = self.cannon_L(p)
        a = p.aim
        # barrels: six around the axis, spinning; the far ones darker, drawn first
        bars = []
        for k in range(6):
            th = TAU * (k / 6.0 + p.spin / 6.0)
            bars.append((math.cos(th), math.sin(th) * 5.2, k))
        for z, v, k in sorted(bars):
            cv.part()
            cv.capsule(A(CAN_HOUSE - 2, v), A(CAN_BARREL, v), 2.5, 2.5, GUN if z > -0.2 else GUN_BK,
                       bias=0.12 * z - 0.04)
        # clamps around the barrels
        for u in (CAN_HOUSE + 10, CAN_BARREL - 6):
            cv.part()
            cv.poly([A(u - 1.8, -8.2), A(u + 1.8, -8.2), A(u + 1.8, 8.2), A(u - 1.8, 8.2)], BRASS, bevel=1.0, base=0.55)
        # muzzle brake: a short ring
        cv.part()
        cv.poly([A(CAN_BARREL - 1, -8.6), A(CAN_MUZZLE, -8.0), A(CAN_MUZZLE, 8.0), A(CAN_BARREL - 1, 8.6)], GUN,
                bevel=1.4, base=0.58)
        cv.part(line=False)
        cv.line(A(CAN_BARREL + 3, -7.6), A(CAN_BARREL + 3, 7.6), GUN, inten=0.12, decal=True)
        # the mouths of the barrels: glowing when firing
        mz = min(1.5, p.muzzle)
        cv.part(line=False)
        for z, v, k in sorted(bars):
            if z > -0.2:
                cv.px_set(*A(CAN_MUZZLE - 0.5, v), CAVITY if mz < 0.1 else self.light(p),
                          inten=0.1 if mz < 0.1 else self.li(p, 0.5 + 0.3 * mz))
        # housing: a heavy drum with brass bands, a magazine drum on top
        cv.part()
        cv.ellipse(A(17, -13), 9.5, 7.5, GUN, ang=a, bias=0.0)
        cv.part(line=False)
        cv.ellipse(A(17, -13.5), 4.2, 3.2, BRASS, ang=a, flat=0.55, decal=True)
        cv.px_set(*A(17, -14), BRASS, inten=0.2, decal=True)
        self.plate(cv, [A(CAN_BACK, -12), A(CAN_HOUSE, -13), A(CAN_HOUSE + 3, -9), A(CAN_HOUSE + 3, 9),
                        A(CAN_HOUSE, 13), A(CAN_BACK, 12), A(CAN_BACK - 4, 0)], GUN, base=0.56, bevel=2.6, grad=0.2)
        cv.part(line=False)
        for u in (2.0, CAN_HOUSE - 3.0):
            cv.poly([A(u - 1.3, -12.6), A(u + 1.3, -12.7), A(u + 1.3, 12.7), A(u - 1.3, 12.6)], BRASS, flat=0.62,
                    decal=True)
        self.rivets(cv, [A(8, -8), A(14, -8), A(8, 7), A(14, 7)], GUN)
        # cable lights along the housing (they run faster when it spins)
        for k in range(4):
            u = 6 + 4 * k
            on = 0.45 + 0.25 * min(1.2, p.glow) + (0.3 if (k + int(p.spin * 4)) % 4 == 0 else 0.0)
            self.lamp(cv, p, *A(u, 2), v=on)
        if p.broken:
            self.dent(cv, A(16, -5), 3.0, 2.0, a, GUN)

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

    def _clean(self, layer, floor=3):
        """Effects never reach the frame edges (3 px) nor more than `floor` rows into the ground."""
        layer[GY + floor:] = 0
        layer[:3] = 0
        layer[:, :3] = 0
        layer[:, W - 3:] = 0
        return layer

    def dust(self, f, x, r, seed, y=GY - 2):
        """A low dust cloud on the ground (on empty pixels; at most 3 rows into the floor)."""
        layer = np.zeros_like(f)
        dust_cloud(layer, x, y, r, DUST, seed)
        FX.under(f, self._clean(layer))

    def puff(self, f, x, y, r, cols, seed, front=False):
        """A jagged puff of steam or smoke (behind the body, or in front of it)."""
        layer = np.zeros_like(f)
        FX.burst(layer, x, y, r, cols, seed=seed, jag=0.42)
        self._clean(layer, floor=0)
        (FX.overlay if front else FX.under)(f, layer)

    def jet(self, f, x, y, ang, ln, r0, r1, cols, seed, n=4, front=False):
        """A jet of steam: puffs along a direction, growing and thinning (the far end lighter)."""
        rng = np.random.default_rng(seed)
        ca, sa = math.cos(ang), math.sin(ang)
        for k in range(n):
            t = k / max(1, n - 1)
            r = r0 + (r1 - r0) * t
            j = (rng.random() - 0.5) * 2.0 * t * r * 0.4
            cs = cols[min(len(cols) - 1, int(t * 1.6)):] if t > 0.6 else cols
            self.puff(f, x + ca * ln * t - sa * j, y + sa * ln * t + ca * j, r, cs, seed * 5 + k, front=front)

    def sparks(self, f, x, y, n, r, seed, a0=-PI, a1=PI, front=True):
        """Electric sparks: short bright streaks flying out of (x, y)."""
        rng = np.random.default_rng(seed)
        cols = [WHITE_C, PC[0], PC[1], PC[1], PC[2]]
        for k in range(n):
            a = a0 + (a1 - a0) * rng.random()
            d0 = 2 + rng.random() * r * 0.5
            ln = 2 + int(rng.integers(0, 4))
            c = cols[k % len(cols)]
            for s in range(ln):
                px, py = x + math.cos(a) * (d0 + s), y + math.sin(a) * (d0 + s)
                ix, iy = int(px), int(py)
                if 3 <= ix < W - 3 and 3 <= iy < GY and (front or f[iy, ix, 3] == 0):
                    f[iy, ix] = c if s < ln - 1 else PC[2]

    def debris(self, f, seed, x0, x1, y0, y1, n, sizes=(1, 2, 2)):
        """Bolts and plate fragments (metal chunks)."""
        rng = np.random.default_rng(seed)
        for k in range(n):
            x = min(W - 6, max(4, x0 + rng.random() * (x1 - x0)))
            y = min(GY - 3, max(4, y0 + rng.random() * (y1 - y0)))
            FX.chunk(f, x, y, sizes[k % len(sizes)], BOLT_L, BOLT_D)

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
        """Swing smear: an annular sector behind the fist made of streaks of different lengths,
        bright at the leading edge (a1)."""
        rr = np.hypot(cv_x - cx, cv_y - cy)
        aa = np.arctan2(cv_y - cy, cv_x - cx)
        s = (aa - a0) / (a1 - a0)
        rad = (rr - r0) / (r1 - r0)
        band = np.floor(np.clip(rad, 0, 0.999) * bands)
        start = 0.04 + 0.4 * _hash(band, band * 3.0, 11)
        m = (s >= start) & (s <= 1) & (rad >= 0) & (rad <= 1) & (f[:, :, 3] == 0)
        m[GY:] = False
        k = np.clip((1.0 - s) * 2.6 + np.abs(rad - 0.5) * 1.6, 0, 3).astype(int)
        cols = np.array([PC[0], PC[1], PC[2], PC[3]], np.uint8)
        f[m] = cols[k[m]]

    def shock(self, f, x, r, col, dotted=False):
        """A shockwave arc on the ground (flattened half ring above the floor)."""
        dash_arc(f, x, GY - 1, r, r * 0.34, PI, TAU, col, dash=2 if dotted else 99, gap=3 if dotted else 0)

    def mouths(self, p):
        """The exhaust stacks' mouths (frame), their direction and radius."""
        out = []
        for a, b, r in STACKS:
            pa, pb = self.B(p, a), self.B(p, b)
            out.append((pb, math.atan2(pb[1] - pa[1], pb[0] - pa[0]), r))
        return out

    def housing_mouth(self, p):
        """Where the forearm's piston rod leaves its housing (steam hisses out there)."""
        el = self.elbow(p)
        d = unit(el, p.fist)
        return (el[0] + d[0] * (L_HOUSING + 1), el[1] + d[1] * (L_HOUSING + 1)), math.atan2(d[1], d[0])

    def vent_steam(self, f, p, i, seed, power=1.0, cols=None):
        """Steam (or smoke) blasting out of both stacks."""
        for k, (m, ang, r) in enumerate(self.mouths(p)):
            self.jet(f, m[0], m[1] - 1, ang, 10 + 16 * power, 2 + r * 0.3, 3 + 4 * power, cols or STEAM,
                     seed + 7 * k + i, n=3 + int(2 * power))

    def smoke(self, f, p, i, n, seed):
        """Form II: smoke curling out of the stacks, drifting back (periodic over n frames)."""
        for k, (m, ang, r) in enumerate(self.mouths(p)):
            for j in range(3):
                t = ((i / n) + j / 3.0 + 0.17 * k) % 1.0
                x = m[0] - 2 - 14 * t + 2 * math.sin(TAU * t + k)
                y = m[1] - 3 - 26 * t
                rr = 2.8 + 3.6 * t
                self.puff(f, x, y, rr, SMOKE[min(2, int(t * 3)):], seed + 31 * k + 7 * j)

    def damage_sparks(self, f, p, i, seed):
        """Form II: the torn cables of the bare shoulder and of the chest spit sparks."""
        if i % 2 == 0:
            x, y = self.B(p, (PAUL_F[0] - 8, PAUL_F[1] - 21))
            self.sparks(f, x, y, 4, 6, seed + i, a0=-2.6, a1=-0.6)
        else:
            x, y = self.B(p, (33, -24))
            self.sparks(f, x, y, 3, 5, seed + i, a0=0.2, a1=2.6)

    def drone(self, x, y, seed, tilt=0.0, flame=1.0):
        """An escort drone (summon): a riveted pod, one light eye, two thrusters firing down."""
        p = TPose()
        cv = TCanvas(W, H)
        L = frame_of((x, y), (math.cos(tilt), math.sin(tilt)))
        for dx in (-4.5, 4.5):
            if flame > 0:
                cv.part(line=False)
                cv.capsule(L(dx, 5), L(dx, 6 + 5 * flame), 1.8, 0.7, CORE, flat=0.72)
                cv.capsule(L(dx, 5), L(dx, 6 + 2 * flame), 0.9, 0.5, CORE, flat=0.92)
            cv.part()
            cv.capsule(L(dx, 2), L(dx, 5), 2.0, 1.7, GUN_BK)
        cv.part()
        cv.ellipse((x, y), 8.0, 5.5, GUN, ang=tilt, bias=0.06)
        cv.part(line=False)
        cv.line(L(-7, 2), L(7, 2), BRASS, inten=0.6, decal=True)
        cv.ellipse(L(4, -1), 2.4, 1.8, CAVITY, ang=tilt, flat=0.1, decal=True)
        cv.px_set(*L(4.6, -1.2), CORE, inten=0.92)
        cv.px_set(*L(3.6, -1.2), HOT, inten=0.7)
        cv.px_set(*L(-4, -3), GUN, inten=0.97, decal=True)
        cv.part()
        cv.capsule(L(-3, -5), L(-5, -8), 0.8, 0.7, STEEL)
        self.lamp(cv, p, *L(-5.5, -9), v=0.7)
        return cv.render()

    # ================================================================= clips
    def idle(self, broken=False):
        """Standing guard: the pistons hiss, the visor scans left and right, the reactor pulses."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            s = math.sin(ph)
            p = TPose(t=ph, broken=broken, hip=(126.0, 146.0 + 1.0 * s), paul=(0.0, -1.0 * math.sin(ph - 0.7)),
                      head=(0.0, 0.7 * math.sin(ph - 0.5)),
                      fist=(174 + 1.0 * math.sin(ph + 0.5), 140 + 1.6 * math.sin(ph + 0.3)),
                      piston=0.3 + 0.05 * math.sin(ph), arm_b=0.9 + 0.02 * math.sin(ph + 1.0),
                      aim=0.06 + 0.025 * math.sin(ph + 1.2), glow=0.62 + 0.12 * s,
                      reactor=0.6 + 0.16 * math.sin(2 * ph), visor=0.78, scan=0.5 + 0.5 * math.sin(ph))
            f = self.draw(p)
            # the pistons hiss: the forearm (f1-f3), then the calf of the near leg (f5-f7)
            if 1 <= i <= 3:
                (hx, hy), a = self.housing_mouth(p)
                self.jet(f, hx, hy - 8, -PI / 2 - 0.3, 5 + 4 * (i - 1), 1.4, 2.4 + 0.8 * (i - 1), STEAM[i - 1:],
                         self.seed + i, n=3, front=True)
            if 5 <= i <= 7:
                hip, knee, ankle, F, T, S = self.leg(p, True)
                x, y = S(14, 12)
                self.jet(f, x, y, PI + 0.6, 4 + 4 * (i - 5), 1.4, 2.2 + (i - 5), STEAM[i - 5:], self.seed + 10 + i, n=3)
            if broken:
                self.smoke(f, p, i, n, self.seed + 20)
                self.damage_sparks(f, p, i, self.seed + 30)
            out.append(f)
        return out

    @staticmethod
    def _step(u, base):
        """Foot x, lift, toe tilt at phase u (0..0.5 on the ground, 0.5..1 swinging forward)."""
        S, L = 14.0, 10.0
        if u < 0.5:
            return base + S - 2 * S * (u / 0.5), 0.0, 0.0
        s = (u - 0.5) / 0.5
        sm = s * s * (3 - 2 * s)
        return base - S + 2 * S * sm, L * math.sin(PI * s), 0.2 * (2 * s - 1) * math.sin(PI * s)

    def walk_pose(self, i, broken, n=8):
        ph = TAU * i / n
        u = i / n
        bob = [2, 3, 1, -1, 2, 3, 1, -1]
        xf, lf, tf = self._step(u, 112.0)
        xb, lb, tb = self._step((u + 0.5) % 1.0, 144.0)
        lag = 0.6 * (bob[(i - 1) % n] - bob[i])
        return TPose(t=ph, broken=broken, hip=(127.0 + 0.8 * math.sin(2 * ph), 145.0 + bob[i]),
                     lean=0.06 + 0.02 * math.cos(2 * ph), foot_f=(xf, GY - lf), foot_b=(xb, GY - lb), toe_f=tf,
                     toe_b=tb, fist=(173 - 5 * math.cos(ph), 142 + 0.6 * bob[i] - 2 * abs(math.cos(ph))),
                     arm_b=0.9 + 0.04 * math.cos(ph), aim=0.08 + 0.02 * bob[i], paul=(0.0, lag),
                     head=(0.0, 0.4 * bob[i]), glow=0.66, visor=0.82, scan=0.5 + 0.25 * math.sin(ph), spin=0.0)

    def walk(self, broken=False):
        """Heavy steps: each foot stamps down (dust, f0 and f4), the body jolts, the arms sway."""
        out = []
        n = 8
        for i in range(n):
            p = self.walk_pose(i, broken)
            f = self.draw(p)
            if i in (0, 4):
                fx = (p.foot_f if i == 0 else p.foot_b)[0]
                self.dust(f, fx + 24, 7, self.seed + 40 + i)
                self.dust(f, fx - 22, 7, self.seed + 42 + i)
                self.shock(f, fx + 2, 30, DUST[1], dotted=True)
                self.debris(f, self.seed + 44 + i, fx - 26, fx + 30, GY - 9, GY - 4, 3, sizes=(1, 2, 1))
            elif i in (1, 5):
                fx = (p.foot_f if i == 1 else p.foot_b)[0]
                self.dust(f, fx + 29, 4, self.seed + 46 + i)
                self.dust(f, fx - 27, 4, self.seed + 48 + i)
            if broken:
                self.smoke(f, p, i, n, self.seed + 50)
                self.damage_sparks(f, p, i, self.seed + 60)
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """The piston fist winds back over its shoulder (the rod retracts, steam hisses) while the
        cannon spins up."""
        fists = [(168, 132), (150, 120), (122, 112), (92, 92), (94, 58), (98, 44)]
        out = []
        spin = 0.0
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            spin += 0.2 + 0.25 * i
            p = TPose(t=0.6 * i, broken=broken, hip=(126.0 - 2 * e, 146.0 + e), lean=0.03 - 0.15 * e, tilt=-0.12 * e,
                      head=(0.0, -e), fist=fists[i], piston=0.3 * (1 - e),
                      elbow_f=RAISED if i >= 4 else ((-24.0, 26.0) if i >= 2 else (-6.0, 36.0)),
                      paul=(0.0, -2 * e), arm_b=0.9 + 0.08 * e, aim=0.06 - 0.1 * e, spin=spin, glow=0.6 + 0.7 * t,
                      visor=0.8 + 0.6 * t, reactor=0.6 + 0.4 * t, muzzle=0.4 * t if i >= 3 else 0.0, scan=0.85)
            f = self.draw(p)
            if 1 <= i <= 3:
                (hx, hy), a = self.housing_mouth(p)
                self.jet(f, hx, hy, a - PI / 2 - 0.4, 6 + 3 * i, 1.8, 3.0 + i, STEAM, self.seed + 70 + i, n=3,
                         front=True)
            if i >= 2:
                self.charge(f, fists[i][0], fists[i][1], i, 6, self.seed + 75 + i, r0=30, r1=20, count=4 + 2 * i)
            if i >= 3:
                mx, my = self.muzzle(p)
                self.charge(f, mx, my, i, 6, self.seed + 80 + i, r0=16, r1=5, count=2 + i)
            if i >= 4:
                self.sparkle(f, fists[i][0] + 2, fists[i][1] - 19, 2 + 2 * (i - 4))
            if broken:
                self.smoke(f, p, i, 6, self.seed + 85)
            out.append(f)
        return out

    def attack(self, broken=False):
        """Piston punch: the fist comes down from over its shoulder and the rod fires out, slamming
        the ground in front of it (impact f2)."""
        keys = [  # fist, piston, hip, lean, elbow hint, glow
            ((102, 40), 0.0, (124.0, 146.0), -0.14, RAISED, 1.3),
            ((198, 100), 0.6, (127.0, 152.0), 0.16, (6.0, 30.0), 1.25),
            (IMPACT, 1.0, (131.0, 166.0), 0.28, (12.0, 30.0), 1.1),
            ((IMPACT[0], IMPACT[1] - 1), 0.95, (131.0, 165.0), 0.27, (12.0, 30.0), 0.95),
            ((180, 160), 0.5, (128.0, 153.0), 0.16, (0.0, 34.0), 0.75),
        ]
        out = []
        for i, (fi, pis, hip, lean, eh, g) in enumerate(keys):
            p = TPose(t=0.5 * i, broken=broken, hip=hip, lean=lean, fist=fi, piston=pis, elbow_f=eh, glow=g,
                      visor=1.0 + 0.3 * (i < 3), reactor=0.6 + 0.4 * g, tilt=0.1 * (i >= 2),
                      arm_b=[0.95, 0.7, 0.5, 0.5, 0.75][i], aim=[-0.05, -0.5, -0.9, -0.85, -0.3][i],
                      spin=2.0 + 0.6 * i, scan=0.85,
                      foot_f=(110.0, float(GY)), foot_b=(148.0, float(GY)))
            f = self.draw(p)
            ix = int(fi[0])
            if i == 1:
                sx, sy = self.shoulder(p, True)
                rf = math.hypot(fi[0] - sx, fi[1] - sy)
                af = math.atan2(fi[1] - sy, fi[0] - sx)
                self.smear(f, sx, sy, rf - 16, rf + 18, af - 1.15, af - 0.12)
            if i == 2:
                back = np.zeros_like(f)
                for k, (bx, br) in enumerate(((ix - 18, 12), (ix + 16, 13), (ix + 2, 10))):
                    FX.burst(back, bx, GY - 3, br, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 90 + k, jag=0.45,
                             rays=7)
                FX.under(f, self._clean(back, floor=2))
                self.ground_cracks(f, ix, 44, self.seed + 93)
                self.shock(f, ix, 30, PC[0])
                self.shock(f, ix, 44, PC[1], dotted=True)
                self.dust(f, ix - 36, 13, self.seed + 94)
                self.dust(f, ix + 24, 8, self.seed + 95)
                self.debris(f, self.seed + 96, ix - 46, ix + 40, 130, 200, 12, sizes=(2, 1, 2, 1))
                self.sparks(f, ix + 4, GY - 22, 10, 18, self.seed + 97, a0=-PI, a1=0.0)
                (hx, hy), a = self.housing_mouth(p)
                self.jet(f, hx, hy, a - PI / 2 - 0.3, 14, 2.0, 5.0, STEAM, self.seed + 98, n=4)
                f = shake(f, 1, 0)
            if i == 3:
                self.ground_cracks(f, ix, 46, self.seed + 93, level=1)
                self.shock(f, ix, 46, PC[2], dotted=True)
                self.dust(f, ix - 40, 14, self.seed + 99)
                self.dust(f, ix + 24, 9, self.seed + 100)
                self.debris(f, self.seed + 101, ix - 52, ix + 40, 110, 170, 8, sizes=(2, 1))
                (hx, hy), a = self.housing_mouth(p)
                self.jet(f, hx, hy, a - PI / 2 - 0.3, 20, 3.0, 7.0, STEAM[1:], self.seed + 102, n=4)
            if i == 4:
                self.ground_cracks(f, ix + 6, 40, self.seed + 93, level=3)
                self.dust(f, ix - 42, 6, self.seed + 103)
                self.dust(f, ix + 28, 6, self.seed + 104)
                self.debris(f, self.seed + 105, ix - 46, ix + 38, 172, 204, 4, sizes=(1, 2))
            if broken:
                self.smoke(f, p, i, 5, self.seed + 106)
            out.append(f)
        return out

    def aim_pose(self, e, broken, i=0):
        """Bracing to fire: wide stance, the cannon levelled, the fist braced under it."""
        return TPose(t=0.7 * i, broken=broken, hip=(125.0 - e, 146.0 + 4 * e), lean=0.03 - 0.05 * e,
                     foot_f=(110.0 - 6 * e, float(GY)), foot_b=(146.0 + 4 * e, float(GY)),
                     fist=(174 - 10 * e, 140 + 4 * e), piston=0.3 - 0.2 * e, arm_b=0.9 - 0.3 * e, aim=0.06 - 0.06 * e,
                     paul=(0.0, -1.5 * e), tilt=0.0, head=(1.0 * e, 0.0), glow=0.6 + 0.8 * e, reactor=0.6 + 0.9 * e,
                     visor=0.8 + 0.7 * e, vents=e, scan=0.5 + 0.5 * e)

    def telegraph_special(self, broken=False):
        """The reactor brightens, the vents open, the cannon levels and spins up (beam)."""
        out = []
        spin = 0.0
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            spin += 0.3 + 0.3 * i
            p = self.aim_pose(e, broken, i).copy(spin=spin, muzzle=0.9 * t if i >= 2 else 0.0)
            f = self.draw(p)
            if i >= 2:
                f = aura(f, i - 2)
            rx, ry = self.reactor(p)
            self.charge(f, rx, ry, i, 6, self.seed + 110 + i, r0=34, r1=12, count=6 + 2 * i)
            mx, my = self.muzzle(p)
            if i >= 2:
                self.charge(f, mx, my, i, 6, self.seed + 116 + i, r0=22, r1=4, count=4 + 2 * i)
            if i >= 3:
                for k, (m, ang, r) in enumerate(self.mouths(p)):
                    self.jet(f, m[0], m[1] - 1, ang, 6 + 4 * (i - 3), 1.6, 2.4 + (i - 3), [PC[1], PC[2], PC[3]],
                             self.seed + 120 + 3 * i + k, n=3)
            if i == 5:
                vx, vy = self.visor(p)
                self.sparkle(f, vx + 2, vy, 4)
                self.sparkle(f, mx + 1, my, 5)
            if broken:
                self.smoke(f, p, i, 6, self.seed + 125)
            out.append(f)
        return out

    def special_pose(self, i, broken=False):
        """The firing pose (the four frames share the head, the cannon and the body)."""
        return self.aim_pose(1.0, broken, 5).copy(t=0.7 * 5 + 0.5 * i, spin=1.5 * i, muzzle=1.4 + 0.1 * (i % 2),
                                                  reactor=1.4 + 0.1 * (i % 2), visor=1.5, glow=1.4)

    def special(self, broken=False):
        """Loop: the cannon fires its beam (the game draws the beam): muzzle flare, visor ablaze,
        vents roaring; spent casings fall from the housing."""
        out = []
        for i in range(4):
            p = self.special_pose(i, broken)
            f = aura(self.draw(p), 4 + i)
            mx, my = self.muzzle(p)
            FX.burst(f, mx + 3, my, 5 + 2 * (i % 2), [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 130 + i, jag=0.3,
                     rays=3)
            ring(f, mx + 2, my, 9 + 2 * (i % 2), PC[2], -1.3, 1.3, dotted=2)
            vx, vy = self.visor(p)
            FX.burst(f, vx + 2, vy, 2 + (i % 2), [WHITE_C, PC[0], PC[1]], seed=self.seed + 134 + i, jag=0.3)
            for k, (m, ang, r) in enumerate(self.mouths(p)):
                self.jet(f, m[0], m[1] - 1, ang, 10 + 3 * ((i + k) % 2), 1.8, 3.4, [PC[1], PC[2], PC[3]],
                         self.seed + 136 + 2 * i + k, n=3)
            # spent casings tumbling out of the housing
            A = self.cannon_L(p)
            for k in range(3):
                t = ((i / 4.0) + k / 3.0) % 1.0
                x, y = A(10 - 4 * t, 14 + 40 * t * t)
                if y < GY - 4:
                    FX.chunk(f, x, y, 2, C("#f4dc98"), C("#98773a"))
            if broken:
                self.smoke(f, p, i, 4, self.seed + 140)
            out.append(f)
        return out

    def summon(self, broken=False):
        """It hunches, its back hatch swings open and drones launch upward out of the bay."""
        keys = [  # hatch, lean, hip y
            (0.3, 0.1, 147.0), (0.75, 0.18, 150.0), (1.0, 0.22, 152.0), (1.0, 0.22, 152.0), (1.0, 0.2, 151.0),
            (0.45, 0.1, 147.0)]
        flights = [  # per drone: offsets from the bay on frames 2..5 (None = not out yet)
            [(-6, -4), (-10, -24), (4, -38), (22, -44)],
            [None, (-4, -6), (-20, -24), (-32, -34)],
            [None, None, (-6, -4), (-6, -24)],
        ]
        out = []
        for i, (h, lean, hy) in enumerate(keys):
            p = TPose(t=0.6 * i, broken=broken, hip=(125.0, hy), lean=lean, hatch=h, tilt=0.12 * min(1.0, h),
                      fist=(170.0, 146.0 + 2 * min(1.0, h)), glow=0.7 + 0.5 * h, reactor=0.7 + 0.4 * h, visor=0.9,
                      scan=0.3, arm_b=0.86, aim=0.12)
            f = self.draw(p)
            bx, by = self.B(p, (-40, -86))
            if i in (0, 1):
                self.jet(f, bx, by, -PI / 2 - 0.4, 8 + 6 * i, 2.0, 4.0 + 2 * i, STEAM, self.seed + 150 + i, n=3)
            if i >= 1:
                self.charge(f, bx, by + 6, i, 6, self.seed + 155 + i, r0=10, r1=6, count=3 + i)
            for k, path in enumerate(flights):
                if i < 2 or path[i - 2] is None:
                    continue
                dx, dy = path[i - 2]
                x, y = bx + dx, by + dy
                launching = path[i - 2] == next(q for q in path if q is not None)
                layer = self.drone(x, y, self.seed + 160 + k, tilt=-0.15 + 0.12 * k + 0.06 * i,
                                   flame=1.6 if launching else 1.0)
                FX.overlay(f, layer)
                if launching:
                    self.jet(f, x, y + 10, PI / 2 + 0.2, 8, 2.0, 4.0, STEAM[1:], self.seed + 170 + 3 * i + k, n=3)
                else:
                    for j in range(3):  # thrust trail
                        put_if_empty(f, x - 4.5 + 9 * (j % 2), y + 12 + 3 * j, [PC[1], PC[2], PC[3]][j])
            if broken:
                self.smoke(f, p, i, 6, self.seed + 175)
            out.append(f)
        return out

    def roar_pose(self, h, broken, t):
        return TPose(t=t, broken=broken, hip=(126.0, 146.0 - 2 * h), lean=0.03 - 0.16 * h, tilt=-0.3 * h,
                     head=(1.5 * h, -2 * h), fist=(174 - 20 * h, 140 + 22 * h), piston=0.3 + 0.5 * h,
                     elbow_f=(-6.0 - 14 * h, 36.0), arm_b=0.9 + 0.45 * h, aim=0.06 - 0.62 * h,
                     paul=(0.0, -3 * h), glow=0.6 + 0.8 * h, visor=0.8 + 0.7 * h, reactor=0.6 + 0.8 * h, vents=h,
                     spin=3.0 * t, scan=1.0)

    def _roar_fx(self, f, p, i, h, sh):
        if h > 0.8:
            vx, vy = self.visor(p)
            for k, r in enumerate((10 + 3 * (i % 2), 19 + 3 * (i % 2), 28 + 3 * (i % 2))):
                ring(f, vx + 4, vy, r, [PC[0], PC[1], PC[2]][k], -1.2, 1.0, dotted=0 if k < 2 else 2)
            self.vent_steam(f, p, i, self.seed + 180, power=1.0)
            (hx, hy), a = self.housing_mouth(p)
            self.jet(f, hx, hy, a + PI / 2, 12, 2.0, 5.0, STEAM, self.seed + 185 + i, n=3)
            hip, knee, ankle, F, T, S = self.leg(p, True)
            x, y = S(14, 12)
            self.jet(f, x, y, PI + 0.3, 14, 2.0, 5.0, STEAM, self.seed + 188 + i, n=3)
            f = shake(f, sh, 0)
        elif h > 0.2:
            self.vent_steam(f, p, i, self.seed + 190, power=0.4)
        return f

    def roar(self, broken=False):
        """Phase change: it rears up, raises fist and cannon, its horn blares and every vent blasts
        steam."""
        keys = [(0.0, 0), (0.35, 0), (1.0, 1), (1.0, -1), (1.0, 1), (0.9, -1), (0.4, 0), (0.0, 0)]
        out = []
        for i, (h, sh) in enumerate(keys):
            p = self.roar_pose(h, broken, 0.8 * i)
            f = self.draw(p)
            if broken:
                self.smoke(f, p, i, 8, self.seed + 195)
            out.append(self._roar_fx(f, p, i, h, sh))
        return out

    def shatter(self):
        """Phase 5 roar: the chest plate cracks and blows off over the reactor, the shoulder plate
        is torn away - form II ("Endommagé")."""
        seq = [  # broken, fly, crackle, h, flash
            (False, 0.0, 0.3, 0.35, 0.0), (False, 0.0, 0.7, 0.8, 0.0), (False, 0.0, 1.0, 1.0, 0.35),
            (True, 0.35, 0.0, 1.0, 0.0), (True, 0.75, 0.0, 1.0, 0.0), (True, 1.15, 0.0, 0.9, 0.0),
            (True, 0.0, 0.0, 0.4, 0.0), (True, 0.0, 0.0, 0.0, 0.0)]
        shk = [0, 0, 1, -2, 1, -1, 0, 0]
        out = []
        for i, (broken, fly, crk, h, fl) in enumerate(seq):
            p = self.roar_pose(h, broken, 0.8 * i).copy(fly=fly, crackle=crk, reactor=0.6 + 0.8 * h + 0.3 * crk)
            f = self.draw(p)
            cx, cy = self.reactor(p)
            if i == 2:
                self.sparks(f, cx, cy, 12, 16, self.seed + 200)
            if i == 3:
                FX.burst(f, cx + 6, cy, 16, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 201, jag=0.4, rays=10)
                sx, sy = self.shoulder(p, True)
                FX.burst(f, sx, sy - 6, 9, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 202, jag=0.45, rays=6)
            if 3 <= i <= 5:
                self.debris(f, self.seed + 203 + i, cx - 10 + 14 * (i - 3), cx + 40 + 16 * (i - 3), cy - 40, cy + 20, 8,
                            sizes=(2, 3, 1))
                self.sparks(f, cx, cy, 14 - 3 * (i - 3), 20 + 10 * (i - 3), self.seed + 206 + i)
                ring(f, cx, cy, 20 + 14 * (i - 3), PC[1 + (i - 3)], -1.6, 1.6, dotted=0 if i == 3 else 2)
            if i >= 6:
                # the torn plate and the shoulder plate lie on the ground in front of it
                self.fallen(f, 212, "chest", self.seed + 210, n=4 - 2 * (i - 6))
                self.fallen(f, 72, "shoulder", self.seed + 211, n=2)
                self.smoke(f, p, i, 8, self.seed + 212)
                self.damage_sparks(f, p, i, self.seed + 214)
            f = self._roar_fx(f, p, i, h, 0) if h > 0.8 else f
            if shk[i]:
                f = shake(f, shk[i], 0)
            if fl:
                f = flashed(f, fl)
            out.append(f)
        return out

    def fallen(self, f, x, kind, seed, n=3):
        """A piece of armour lying on the ground (on empty pixels), with a few bolts: the chest
        plate with its grille ('chest') or the shoulder plate upturned like a bowl ('shoulder')."""
        cv = TCanvas(W, H)
        if kind == "shoulder":
            c = (x, GY - 13.0)
            self.dome(cv, c, 19.0, 12.5, PI - 0.1, GUN, bias=0.02)
            L = frame_of(c, (math.cos(-0.1), math.sin(-0.1)))
            self.plate(cv, [L(-19, -1), L(19, -1), L(19, 2), L(-19, 2)], BRASS, base=0.56, bevel=1.0, grad=0.0)
            self.rivets(cv, [L(-12, 0), L(0, 0), L(12, 0)], BRASS)
            self.rivets(cv, [L(-6, 7), L(8, 6)], GUN)
        else:
            self.plate(cv, [(x - 16, GY - 1), (x - 13, GY - 8), (x + 12, GY - 9), (x + 17, GY - 4), (x + 16, GY - 1)],
                       GUN, base=0.56, bevel=1.6, grad=0.22)
            cv.part()
            cv.ellipse((x - 1, GY - 8.5), 10.0, 3.0, BRASS, flat=0.62)
            cv.part(line=False)
            cv.ellipse((x - 1, GY - 8.5), 7.5, 1.8, CAVITY, flat=0.2, decal=True)
            for k in (-4, 0, 4):
                cv.line((x - 1 + k, GY - 10), (x - 1 + k, GY - 7), GUN, inten=0.45, decal=True)
            self.rivets(cv, [(x - 11, GY - 4), (x + 11, GY - 5)], GUN)
        layer = cv.render()
        layer[GY:] = 0
        FX.under(f, self._clean(layer, floor=0))
        self.debris(f, seed + 1, x - 24, x + 22, GY - 6, GY - 3, n, sizes=(2, 1, 2))

    def hurt(self, broken=False):
        a = TPose(broken=broken, hip=(123.0, 146.0), lean=-0.1, tilt=-0.22, fist=(168.0, 136.0), arm_b=0.8, aim=-0.05,
                  glow=0.95, visor=0.4, reactor=1.0, scan=0.2, piston=0.2)
        b = TPose(broken=broken, hip=(125.0, 146.0), lean=-0.02, tilt=-0.08, fist=(172.0, 139.0), arm_b=0.88, aim=0.04,
                  glow=0.75, visor=0.8, reactor=0.7, scan=0.4)
        fa = self.draw(a)
        cx, cy = self.reactor(a)
        self.sparks(fa, cx - 6, cy - 4, 10, 14, self.seed + 220, a0=-2.8, a1=-0.2)
        fa = flashed(fa, 0.55)
        fb = self.draw(b)
        self.debris(fb, self.seed + 221, 70, 96, 70, 110, 4, sizes=(1, 2, 1))
        self.sparks(fb, cx - 10, cy - 8, 4, 8, self.seed + 222, a0=-2.8, a1=-1.0)
        if broken:
            self.smoke(fb, b, 1, 2, self.seed + 223)
        return [fa, fb]

    def death(self, broken=False):
        """It shorts out (sparks, the reactor flickers), buckles to its knees, collapses in a heap
        of metal, and its lights go out."""
        out = []
        seq = [
            TPose(broken=broken, hip=(125.0, 145.0), lean=-0.12, tilt=-0.3, head=(1.0, -2.0), fist=(170.0, 126.0),
                  arm_b=0.7, aim=-0.3, glow=1.5, visor=1.5, reactor=1.5, scan=0.9, piston=0.6),
            TPose(broken=broken, hip=(126.0, 147.0), lean=0.04, tilt=0.06, fist=(172.0, 142.0), arm_b=0.86, aim=0.12,
                  glow=0.3, visor=0.15, reactor=0.25, scan=0.5),
            TPose(broken=broken, hip=(126.0, 150.0), lean=0.08, tilt=-0.1, fist=(172.0, 148.0), arm_b=0.82, aim=0.2,
                  glow=1.2, visor=1.0, reactor=1.3, scan=0.1),
            TPose(broken=broken, hip=(127.0, 164.0), lean=0.18, tilt=0.2, fist=(176.0, 172.0), arm_b=0.7, aim=0.55,
                  glow=0.8, visor=0.6, reactor=0.8, foot_f=(112.0, float(GY)), foot_b=(144.0, float(GY)), piston=0.5),
            TPose(broken=broken, hip=(128.0, 176.0), lean=0.3, tilt=0.35, head=(0.0, 2.0), fist=(180.0, 188.0),
                  arm_b=0.5, aim=0.85, glow=0.6, visor=0.35, reactor=0.6, dead=0.3, foot_f=(112.0, float(GY)),
                  foot_b=(144.0, float(GY)), piston=0.8, slide=0.3 if not broken else 0.0),
            TPose(broken=broken, hip=(128.0, 184.0), lean=0.42, tilt=0.5, head=(0.0, 4.0), fist=(182.0, 194.0),
                  arm_b=0.3, aim=1.05, glow=0.45, visor=0.2, reactor=0.45, dead=0.5, foot_f=(112.0, float(GY)),
                  foot_b=(144.0, float(GY)), piston=1.0, slide=1.0 if not broken else 0.0),
        ]
        shk = [1, -1, 2, 0, 0, 0]
        for i, p in enumerate(seq):
            f = self.draw(p)
            cx, cy = self.reactor(p)
            if i == 0:
                for k, (x, y) in enumerate((self.shoulder(p, True), self.elbow(p), self.leg(p, True)[1])):
                    self.sparks(f, x, y, 8, 12, self.seed + 230 + k)
            if i == 1:
                self.sparks(f, cx, cy, 6, 10, self.seed + 234)
            if i == 2:
                self.vent_steam(f, p, i, self.seed + 236, power=0.8)
                self.sparks(f, cx, cy, 10, 16, self.seed + 237)
            if i == 3:
                self.dust(f, 160, 8, self.seed + 238)
                self.dust(f, 104, 7, self.seed + 239)
            if i >= 4:
                self.vent_steam(f, p, i, self.seed + 240, power=0.5, cols=SMOKE)
            f = shake(f, shk[i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        base = self.heap(seq[-1], broken)
        for k, dim in enumerate((0, 1, 1, 2, 3, 4, 5)):
            f = base.copy()
            if k <= 1:
                self.dust(f, 186, 9 - 2 * k, self.seed + 250 + k)
                self.dust(f, 110, 8 - 2 * k, self.seed + 252 + k)
                self.debris(f, self.seed + 254 + k, 150, 214, GY - 10, GY - 4, 5 - 2 * k, sizes=(2, 1))
            if k in (0, 2, 4):
                cx, cy = self.reactor(seq[-1])
                self.sparks(f, cx, cy, 6 - k, 8, self.seed + 256 + k)
            self.heap_smoke(f, seq[-1], k, self.seed + 260)
            out.append(dimmed(f, dim))
        f = base.copy()
        self.heap_smoke(f, seq[-1], 7, self.seed + 260, thin=True)
        out.append(dimmed(f, 9))
        return out

    def heap(self, p, broken):
        """The collapsed machine: the last pose sunk a little more, its shoulder plate fallen behind."""
        q = p.copy(hip=(p.hip[0], p.hip[1] + 4), lean=p.lean + 0.08, tilt=p.tilt + 0.12, head=(0.0, 6.0), slide=0.0,
                   dead=0.5, bare=True)
        f = self.draw(q)
        if not broken:
            self.fallen(f, 70, "shoulder", self.seed + 270)
        return f

    def heap_smoke(self, f, p, i, seed, thin=False):
        for k, (m, ang, r) in enumerate(self.mouths(p)):
            for j in range(2 if thin else 3):
                t = ((i / 7.0) * 0.6 + j / 3.0 + 0.2 * k) % 1.0
                x = m[0] - 3 - 12 * t
                y = m[1] - 4 - 30 * t
                cs = SMOKE[min(3, 1 + int(t * 3)):] if thin else SMOKE[min(2, int(t * 3)):]
                self.puff(f, x, y, 2.8 + 3.6 * t, cs, seed + 13 * k + 5 * j)

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
        ]


IMPACT = (184.0, 190.0)        # near fist centre on the impact frame (attack f2)
RAISED = (-30.0, -6.0)         # elbow hint of the fist wound back over the shoulder


def aura(f, phase, rows=GY):
    """Glow around the silhouette (the reactor overloading): a P3 rim hugging the outline and a
    broken P2 halo beyond it, shimmering with `phase`."""
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


# The light going out: white -> P0 .. P4 -> extinct glass greys.
LADDER = [WHITE_C, *PC, C("#4d515a"), C("#363940")]


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


BOSS = Titan()


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
        name = "titan" if k == 0 else "titan_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
