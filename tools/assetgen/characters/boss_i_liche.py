"""La Liche des Catacombes (giga boss, eight phases).

An undead sorcerer-king floating above the ground: a tall emaciated skeleton in tattered robes of
deep purple-black, a high spiked collar and a short mantle over bony shoulders, a cracked crown of
tarnished gold on a skull whose sockets burn, a long staff of dark wood held in the near hand, its
golden claw holding the phylactery crystal; the far hand is bare bone and casting.  Seen from the
side, facing right (the game flips it), always floating (it flies): the frayed hem of the robe
trails behind in long tails that dissolve into wisps of soul fire.

Two forms:
  * form I  (phases 1-4): whole - only the soul fire glows: the eyes, the phylactery, the runes of
    the stole, the gems of the crown and of the clasp, the spell in the far hand, the wisps;
  * form II (phases 5-8): « Brisée » - the crown is split in two, the phylactery is cracked and
    leaks light, the robe is in shreds (torn hem, holes, the chest torn open on the ribcage and its
    burning core), the bones of the far arm float apart, held by threads of soul fire.
The light (soul fire) changes colour at every phase; the bone, the robe, the wood and the crown
never do.  Every emissive pixel uses one of five tones P0..P4 of the phase ramp (plus pure white),
so each phase sheet is the phase-1 sheet with those five colours swapped (exact LUT, `recolour`),
see PHASES.

Frames 224x224, pivot [112, 184] = bottom-centre of the 56x128 collision box (x 84..140,
y 56..184) around the skull, the torso and the robe (not the staff, the far hand nor the tails
and wisps of the hem).  Drawn at scale 1.25 in game (280 px frame, 70x160 px box).
ORIGIN (projectile origin = the point of the phylactery crystal, frame pixels from the pivot,
x forward, y down; x1.25 in game):
  * attack frame 1 (`fire`, the staff thrust forward, a bolt leaves the crystal): (+83, -128)
    = frame pixel (195, 56);
  * special (beam loop, the staff levelled forward does not move on its four frames): (+86, -104)
    = frame pixel (198, 80); the beam itself is drawn by the game, the sheet only shows a burst
    at the crystal.
`Liche.origin(pose)` gives these points (the point of the crystal; `Liche.crystal(pose)` its
centre), see `attack_poses` and `special_pose`.
Clips (packed several per row, `start` = first column): idle 8 (floating, the tails waving),
walk 8 (= move, same frames: it glides), telegraph 6 (raises the staff, the runes flare), attack 5
(bolt from the crystal, fire f1), telegraph_special 6 (both hands raised, the eyes blaze),
special 4 (loop: the staff levelled, a burst at the crystal), hurt 2, summon 6 (arms spread, souls
rise from the ground), roar 8 (phase change; the phase-5 sheet's roar is the phylactery cracking
and the crown splitting), death 14 (the bones collapse, the robe falls empty, the soul fire
escapes and fades).
Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, chaikin, mat, motes, ring, shard, streak
from boss_c_common import TAU, TIMING, Pose, dash_arc, shake
from common.palette import OUTLINE
from raster import TCanvas, ik2

PI = math.pi
W = H = 224
PIVOT = [112, 184]
BOX = (84, 56, 140, 184)  # x0, y0, x1, y1 (collision box, frame pixels)
GD = 212                  # death: the ground the empty robe falls on (rows >= GD stay empty)
WHITE = "#ffffff"
SEED = 51613

# The eight phases: name, French colour name, ramp P0..P4 (light -> dark).
PHASES = [
    ("Nécromancie", "vert maladif", ["#eeffd6", "#b4f566", "#6cd23c", "#2f8f3a", "#134a24"]),
    ("Ossuaire", "jaune d'os", ["#fffce0", "#ffe882", "#f2c23a", "#b07e1c", "#5a3c0c"]),
    ("Malédiction", "violet", ["#f6deff", "#cf8eff", "#9a52f0", "#5e2aac", "#2e1260"]),
    ("Spectres", "cyan pâle", ["#eafeff", "#aaf2ff", "#62cdef", "#2c86b4", "#134266"]),
    ("Phylactère brisé", "rouge sang", ["#ffdcd8", "#ff6658", "#e01e30", "#981020", "#4c0814"]),
    ("Âmes", "turquoise", ["#dcfff6", "#68f4d4", "#1cc4a2", "#10786a", "#073c38"]),
    ("Crypte", "indigo", ["#e4e4ff", "#9ea0ff", "#5c58f2", "#3329a8", "#18115c"]),
    ("Néant", "blanc incandescent", ["#fffff4", "#f2f0ff", "#cecbe8", "#8a85b8", "#403b62"]),
]
P = PHASES[0][2]
PC = [C(h) for h in P]
WHITE_C = C(WHITE)

# Bone, cloth, wood and gold (never recoloured).
BONE = mat("li_bone", ["#f0e8d2", "#d2c6a6", "#a99c7c", "#786c54", "#4a4132"], th=[0.84, 0.64, 0.44, 0.26])
BONE_FAR = mat("li_bone_far", ["#a89c82", "#7c715c", "#554d3e", "#363026"], th=[0.76, 0.52, 0.3])
ROBE = mat("li_robe", ["#6c5a84", "#4b3c62", "#352a48", "#241c33", "#16101f"], th=[0.84, 0.64, 0.44, 0.24])
ROBE_FAR = mat("li_robe_far", ["#3f3254", "#2b223b", "#1c1628", "#120d1a"], th=[0.7, 0.46, 0.24])
TRIM = mat("li_trim", ["#93809a", "#6a5972", "#4a3d52", "#2f2736"], th=[0.78, 0.56, 0.32])
GOLD = mat("li_gold", ["#ead292", "#bc9a52", "#8a6a32", "#5a421e", "#32240f"], th=[0.84, 0.64, 0.44, 0.24])
WOOD = mat("li_wood", ["#8c7864", "#655440", "#45392c", "#2b231b"], th=[0.78, 0.54, 0.3])
VOID = mat("li_void", ["#2c1734", "#140a18"], th=[0.5])
DEAD = mat("li_dead", ["#7e7888", "#5a5468", "#3d384a", "#26222f"])  # the extinct phylactery
# Soul fire (recoloured per phase).
HOT = mat("li_hot", P, emissive=True, outline=None, th=[0.86, 0.68, 0.48, 0.28])
CORE = mat("li_core", [WHITE] + P[:4], emissive=True, outline=None, th=[0.85, 0.68, 0.5, 0.3])
SCRAP_L, SCRAP_D = C("#4b3c62"), C("#241c33")  # torn cloth (debris)
GOLD_L, GOLD_D = C("#bc9a52"), C("#5a421e")    # crown shards

# ---------------------------------------------------------------- rig (body coordinates)
# Body coordinates = frame pixels of the resting pose; `lean` rotates them about CENTER, `crumple`
# squashes them onto the hem (death), then (dx, dy) moves them.
CENTER = (110.0, 136.0)
NECK_TOP = (118.0, 78.0)     # top of the neck: the skull sits on it
ATLAS = (-3.0, 9.5)          # skull-local point resting on the neck
HINGE = (-3.5, 5.5)          # jaw hinge (skull-local)
SHOULDER_N, SHOULDER_F = (116.0, 92.0), (105.0, 89.0)
HEART = (110.0, 113.0)       # the burning core in the ribcage (form II)
HEM_Y = 186.0
L_ARM, L_FORE = 24.0, 23.0   # shoulder -> elbow -> wrist
STAFF_UP, STAFF_DN = 60.0, 76.0  # shaft length above / below the grip
CRY = 12.0                   # crystal centre above the top of the shaft
CRY_TOP, CRY_BOT, CRY_W = 10.0, 8.0, 4.6

FRONT = [(122, 88), (126, 96), (128, 107), (126, 119), (124, 131), (127, 146), (131, 158), (134, 170)]
BACK = [(82, 183), (83, 168), (86, 152), (91, 137), (94, 122), (95, 108), (98, 97), (104, 89)]
HEM_TEETH = 10
COLLAR = [(98, 93), (93, 81), (87, 63), (95, 72), (95, 56), (102, 69), (107, 59), (109, 74), (114, 83), (110, 91)]
# Mantle: the top line, then its ragged lower edge (front to back), smoothed into scallops.
MANTLE_TOP = [(95, 93), (99, 88), (110, 85), (121, 87), (127, 93), (130, 100)]
MANTLE_EDGE = [(130.5, 107), (127.5, 111.5), (125, 106.5), (122, 114.5), (118.5, 108.5), (114.5, 116.5), (110.5, 109.5),
               (106, 116), (102, 109), (98.5, 113.5), (95.5, 106.5), (94, 99)]
MANTLE_EDGE_TORN = [(128, 99), (125.5, 103), (123, 98.5), (120, 103), (116.5, 98.5), (113, 104), (109.5, 99.5),
                    (105.5, 105), (101.5, 101), (98.5, 114), (95.5, 106.5), (94, 99)]
TEAR = [(101, 99), (107, 102), (113, 98), (119, 103), (124, 101), (127.5, 109), (125.5, 118), (126.5, 127),
        (120, 131), (114, 127.5), (108, 132.5), (102.5, 128), (100, 120), (98.5, 110)]
# Rents in the robe (form II): centre, half-length, half-width, angle, seed.
HOLES = [((97.0, 154.0), 6.5, 2.6, 1.75, 1), ((118.0, 163.0), 5.0, 2.0, 1.45, 2), ((93.0, 128.0), 4.0, 1.8, 1.6, 3),
         ((109.0, 172.0), 4.0, 1.8, 1.9, 4)]
# Tails of the hem: root (fraction along the hem, front 0 .. back 1), length, half-width, rest
# angle, gliding angle (radians, 0 = forward).
TAILS = [(1.0, 26.0, 5.2, 2.62, 3.0), (0.84, 22.0, 4.6, 2.46, 2.9), (0.68, 18.0, 4.0, 2.3, 2.78),
         (0.52, 14.0, 3.4, 2.14, 2.66)]
# Runes: strokes in a 3x5 box.
RUNES = [
    [((1, 0), (1, 4)), ((0, 1), (2, 1)), ((0, 4), (2, 3))],
    [((0, 0), (2, 2)), ((2, 2), (0, 4)), ((2, 0), (2, 4))],
    [((1, 0), (0, 2)), ((0, 2), (1, 4)), ((1, 0), (2, 2)), ((2, 2), (1, 4))],
    [((0, 0), (0, 4)), ((0, 2), (2, 0)), ((0, 2), (2, 4))],
    [((0, 0), (2, 0)), ((1, 0), (1, 4)), ((0, 4), (2, 2))],
    [((0, 4), (1, 0)), ((1, 0), (2, 4)), ((0, 2), (2, 2))],
]


class LPose(Pose):
    DEFAULTS = dict(t=0.0, dx=0.0, dy=0.0, lean=0.0, head=(0.0, 0.0), tilt=0.0, jaw=0.0,
                    grip=(151.0, 124.0), staff=0.04, free=None, elbow_n=(-2.0, 22.0),
                    hand=(140.0, 106.0), palm=None, spread=0.5, curl=0.3, cast=0.3, elbow_f=(-4.0, 22.0),
                    glow=0.6, eye=0.6, crystal=0.65, wave=0.0, flow=0.0,
                    broken=False, crack=None, split=None, shred=None, detach=None,
                    crumple=0.0, dead=0.0, crown=None, ground=False)
    # grip: the near hand on the staff (body coordinates); staff: angle (0 = upright, > 0 = the top
    #   leans forward); free: (x, y, angle) of a dropped staff in frame pixels (death)
    # hand: the far wrist (body coordinates); palm: absolute angle of the far hand (None: the forearm's)
    # glow: runes 0..1.5; eye: eye fire 0..1.6; crystal: phylactery 0..1.6; flow: gliding 0..1
    # crack / split / shred / detach: form II features 0..1 (None: 1 when broken, else 0)
    # crumple: the empty robe falling onto the hem (death); dead: the light dying 0..1 (1 = extinct)
    # crown: (dx, dy, angle) of the crown knocked off the skull (death)


def _rent(c, ln, wd, ang, seed, n=10):
    """A ragged rent in the cloth: a long jagged lens along `ang`."""
    rng = np.random.default_rng(seed)
    ca, sa = math.cos(ang), math.sin(ang)
    pts = []
    for k in range(n):
        t = TAU * k / n
        u = math.cos(t) * ln
        v = math.sin(t) * wd * (0.55 + 0.9 * rng.random())
        pts.append((c[0] + u * ca - v * sa, c[1] + u * sa + v * ca))
    return pts


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


def put_if_empty(f, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


def unit(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = math.hypot(dx, dy) or 1.0
    return dx / d, dy / d


class Liche:
    id = "liche"
    W, H, PIVOT = W, H, PIVOT
    seed = SEED
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 8, 9, 12
    ATTACK_EVENTS = [(1, "fire")]

    # ------------------------------------------------------------ geometry
    @staticmethod
    def v(p, name):
        """A form II feature (0..1): explicit, or 1 when broken."""
        x = getattr(p, name)
        return (1.0 if p.broken else 0.0) if x is None else x

    def B(self, p, q):
        """Body coordinates -> frame pixels."""
        x, y = q
        if p.crumple:
            c = p.crumple
            x = CENTER[0] + (x - CENTER[0]) * (1 + 0.4 * c)
            y = HEM_Y - (HEM_Y - y) * (1 - 0.68 * c)
        ca, sa = math.cos(p.lean), math.sin(p.lean)
        dx, dy = x - CENTER[0], y - CENTER[1]
        return (CENTER[0] + dx * ca - dy * sa + p.dx, CENTER[1] + dx * sa + dy * ca + p.dy)

    def head_fns(self, p):
        """HL: skull-local -> frame; JL: the same for the jaw (opened by p.jaw)."""
        nx, ny = self.B(p, NECK_TOP)
        hx, hy = nx + p.head[0], ny + p.head[1]
        a = p.lean + p.tilt
        ca, sa = math.cos(a), math.sin(a)

        def HL(lx, ly):
            dx, dy = lx - ATLAS[0], ly - ATLAS[1]
            return (hx + dx * ca - dy * sa, hy + dx * sa + dy * ca)

        ja = p.jaw * 0.5
        jc, js = math.cos(ja), math.sin(ja)

        def JL(lx, ly):
            dx, dy = lx - HINGE[0], ly - HINGE[1]
            return HL(HINGE[0] + dx * jc - dy * js, HINGE[1] + dx * js + dy * jc)

        return HL, JL

    def mouth(self, p):
        HL, JL = self.head_fns(p)
        a, b = HL(9.0, 10.5), JL(9.0, 11.5)
        return ((a[0] + b[0]) / 2 + 1, (a[1] + b[1]) / 2)

    def eye_pos(self, p):
        HL, _ = self.head_fns(p)
        return HL(5.0, 0.5)

    def staff_frame(self, p):
        """(grip, angle) of the staff in frame pixels."""
        if p.free is not None:
            return (p.free[0], p.free[1]), p.free[2]
        return self.B(p, p.grip), p.staff + p.lean

    def S(self, p):
        """Staff space -> frame: s along the shaft from the grip (up > 0), o across (> 0 forward)."""
        g, a = self.staff_frame(p)
        u = (math.sin(a), -math.cos(a))
        n = (math.cos(a), math.sin(a))
        return lambda s, o=0.0: (g[0] + u[0] * s + n[0] * o, g[1] + u[1] * s + n[1] * o)

    def crystal(self, p):
        """Centre of the phylactery crystal (frame pixels)."""
        return self.S(p)(STAFF_UP + CRY)

    def origin(self, p):
        """The projectile / beam origin: the point of the crystal (frame pixels)."""
        return self.S(p)(STAFF_UP + CRY + CRY_TOP)

    def heart(self, p):
        return self.B(p, HEART)

    def arm(self, p, near):
        """Shoulder, elbow, wrist (frame pixels)."""
        sh = self.B(p, SHOULDER_N if near else SHOULDER_F)
        if near and p.free is None:
            g, a = self.staff_frame(p)
            d = unit(sh, g)
            wr = (g[0] - d[0] * 4.0, g[1] - d[1] * 4.0)
        else:
            wr = self.B(p, p.grip if near else p.hand)
        hx, hy = p.elbow_n if near else p.elbow_f
        best = None
        for bend in (1.0, -1.0):
            e = ik2(sh, wr, L_ARM, L_FORE, bend)
            d = (e[0] - sh[0] - hx) ** 2 + (e[1] - sh[1] - hy) ** 2
            if best is None or d < best[0]:
                best = (d, e)
        el = best[1]
        # an out-of-reach wrist stays at the end of the arm
        d = math.hypot(wr[0] - el[0], wr[1] - el[1])
        if d > L_FORE + 0.5:
            u = unit(el, wr)
            wr = (el[0] + u[0] * L_FORE, el[1] + u[1] * L_FORE)
        return sh, el, wr

    # ------------------------------------------------------------ light
    def lit(self, p):
        return p.dead < 1.0

    def li(self, p, v):
        """Intensity of a soul-fire pixel (dimmed while dying)."""
        return v * (1.0 - 0.55 * p.dead)

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = TCanvas(W, H)
        self._tails(cv, p)
        self._far_arm(cv, p)
        self._collar(cv, p)
        self._robe(cv, p)
        self._neck(cv, p)
        self._near_sleeve(cv, p)
        self._mantle(cv, p)
        self._skull(cv, p)
        self._staff(cv, p)
        self._near_hand(cv, p)
        img = cv.render()
        if p.ground:
            # the heap rests on the ground line: nothing below it, a closed outline on its last row
            img[GD:] = 0
            row = img[GD - 1]
            row[row[:, 3] > 0] = OUTLINE
        return img

    # ------------------------------------------------------------ robe
    def flow_pt(self, p, x, y, tip=0.0):
        """The lower robe trails back when gliding and sways (body coordinates)."""
        k = max(0.0, (y - 110.0) / 76.0)
        x -= p.flow * 15.0 * k * k
        y -= p.flow * 4.0 * k
        x += (1.2 + 1.6 * tip) * k * math.sin(p.wave - y * 0.045)
        return (x, y)

    def hem(self, p):
        """The jagged hem, front to back: (x, y, tatter tip?, fraction) in body coordinates."""
        shred = self.v(p, "shred")
        rng = np.random.default_rng(SEED + 3)
        jit = rng.uniform(-1.0, 1.0, 2 * HEM_TEETH + 1)
        deep = rng.uniform(0.0, 1.0, 2 * HEM_TEETH + 1)
        (x0, y0), (x1, y1) = FRONT[-1], BACK[0]
        n = 2 * HEM_TEETH
        out = []
        for i in range(1, n):
            s = i / n
            x = x0 + (x1 - x0) * s + 0.8 * jit[i]
            y = y0 + (y1 - y0) * s
            if i % 2:  # a tatter hanging down (and back)
                out.append((x - 1.5 - 2.0 * deep[i], y + 5.0 + 6.0 * deep[i] + shred * (2.0 + 5.0 * deep[i]), 1, s))
            else:      # the notch between two tatters
                out.append((x, y - 1.0 - 2.5 * deep[i] - shred * (2.0 + 4.0 * deep[i]), 0, s))
        return out

    def robe_pts(self, p):
        pts = [self.flow_pt(p, x, y) for x, y in FRONT]
        pts += [self.flow_pt(p, x, y, tip) for (x, y, tip, s) in self.hem(p)]
        pts += [self.flow_pt(p, x, y) for x, y in BACK]
        return pts

    def _robe_shade(self, cv, p, mask, salt=0):
        """Cloth folds fanning down from the chest, a shadow inside the hem."""
        fo = self.B(p, (111.0, 96.0))
        lo = self.B(p, (111.0, 186.0))
        dy = np.maximum(cv.py - fo[1], 10.0)
        u = (cv.px - fo[0]) / dy
        k = np.clip((cv.py - fo[1] - 16.0) / 50.0, 0, 1)
        fold = np.sin(u * 12.0 + 0.7 * math.sin(p.wave + salt) + 0.4)
        sharp = np.sign(fold) * np.abs(fold) ** 0.5
        inten = cv.inten + 0.19 * sharp * k
        inten -= 0.16 * np.clip((cv.py - lo[1] + 16.0) / 14.0, 0, 1)
        cv.inten[mask] = np.clip(inten[mask], 0, 1)

    def _robe(self, cv, p):
        fp = [self.B(p, q) for q in self.robe_pts(p)]
        shred = self.v(p, "shred")
        holes = np.zeros((H, W), bool)
        if shred > 0.3:
            for (c, ln, wd, ang, sd) in HOLES:
                q = _rent(self.flow_pt(p, *c), ln * min(1.0, shred), wd, ang, SEED + 40 + sd)
                holes |= cv.poly_mask([self.B(p, x) for x in q])
        cv.part()
        mask = cv.poly(fp, ROBE, bevel=2.4, base=0.5, clip=~holes)
        if mask is None or not mask.any():
            return
        self._robe_shade(cv, p, mask)
        L = lambda x, y: self.B(p, (x, y))
        # the stole down the front edge: an embroidered band with runes of soul fire
        sp = [L(*self.flow_pt(p, x - 5.0, y)) for (x, y) in FRONT[2:]]
        sp.append(L(*self.flow_pt(p, FRONT[-1][0] - 6.5, FRONT[-1][1] + 6.0)))
        cv.part(line=1)
        sm = cv.chain(sp, [2.8] * len(sp), TRIM, clip=mask)
        if sm is not None and sm.any():
            cv.inten[sm] = np.clip(cv.inten[sm] * 0.6 + 0.22, 0, 1)
        for j, s in enumerate((0.1, 0.3, 0.5, 0.7, 0.88)):
            i = min(len(sp) - 2, int(s * (len(sp) - 1)))
            f = s * (len(sp) - 1) - i
            c = (sp[i][0] + (sp[i + 1][0] - sp[i][0]) * f, sp[i][1] + (sp[i + 1][1] - sp[i][1]) * f)
            d = unit(sp[i], sp[i + 1])
            self.rune(cv, p, j + 1, c, math.atan2(d[1], d[0]) - PI / 2, 0.55 + 0.32 * min(1.3, p.glow))
        # belt: a cord at the waist, a gold medallion with a gem, two hanging ends
        cv.part(line=1)
        cv.chain([L(92, 133), L(104, 135), L(116, 134), L(127, 131)], [1.5, 1.6, 1.6, 1.5], GOLD, clip=mask)
        sw = 1.5 * math.sin(p.wave + 0.6)
        for x0, ln in ((124.0, 17.0), (127.5, 12.0)):
            cv.part()
            cv.chain([L(x0, 132), L(x0 + 0.5 * sw, 132 + ln * 0.5), L(x0 + sw - p.flow * 4, 132 + ln)],
                     [1.2, 1.1, 0.9], GOLD)
        cv.part()
        mc = L(126, 132)
        cv.disc(mc, 2.6, GOLD)
        if self.lit(p):
            cv.part(line=False)
            cv.disc(mc, 1.1, CORE, flat=self.li(p, 0.62 + 0.15 * min(1.3, p.glow)))
        if self.v(p, "shred") > 0.3:
            self._tear(cv, p, mask)

    def _tear(self, cv, p, robe):
        """Form II: the chest torn open on the ribcage and its burning core."""
        tm = cv.poly_mask([self.B(p, q) for q in TEAR]) & robe
        if not tm.any():
            return
        cv.part(line=False)
        cv.put(tm, 0.2, VOID)
        L = lambda x, y: self.B(p, (x, y))
        hx, hy = self.heart(p)
        g = min(1.5, p.glow)
        if self.lit(p):
            d = np.hypot(cv.px - hx, (cv.py - hy) * 0.8)
            spill = tm & (d < 12 + 3 * g)
            cv.put(spill, self.li(p, np.clip(0.42 - 0.03 * d, 0.05, 0.42)), HOT)
        # the spine at the back, the burning core, the ribs curving down to the front
        for k in range(6):
            cv.part()
            cv.capsule(L(100.5, 99.5 + 6 * k), L(100.9, 103.5 + 6 * k), 1.7, 1.7, BONE_FAR, clip=tm)
        if self.lit(p):
            self._core(cv, p, (hx, hy), tm)
        for k in range(4):
            y0 = 103.5 + 6.5 * k
            cv.part()
            cv.chain([L(101.0, y0), L(108.5, y0 - 2.2), L(116.5, y0 - 0.2), L(123.0, y0 + 5.0)],
                     [1.15, 1.0, 0.95, 0.75], BONE, clip=tm)
        cv.part()
        cv.capsule(L(124.0, 106), L(124.6, 121), 1.1, 0.9, BONE, clip=tm)
        # the torn edge of the cloth catches the light
        rim = _dilate(tm) & robe & ~tm
        cv.part(line=False)
        cv.put(rim, 0.86, ROBE, decal=True)

    def _core(self, cv, p, c, clip):
        """The burning core in the ribcage: a soul flame."""
        g = min(1.5, p.glow)
        cv.part(line=False)
        d = np.hypot(cv.px - c[0], (cv.py - c[1]) * 0.85)
        r = 5.0 + 0.8 * g + 0.6 * math.sin(p.t * 2.0)
        cv.put(clip & (d < r), self.li(p, np.clip(1.02 - 0.09 * d, 0, 1)), CORE)
        self.flame(cv, (c[0], c[1] - 3), (-0.15, -1.0), 9 + 3 * g, 4.2, p.t, clip=clip, k=self.li(p, 1.0))

    def ribbon(self, cv, pts, widths, m, flat=None, rag=0.25, **kw):
        """A strip of cloth (or of soul fire) along `pts`, half-widths `widths`, ragged edges."""
        left, right = [], []
        n = len(pts)
        for i in range(n):
            a, b = pts[max(0, i - 1)], pts[min(n - 1, i + 1)]
            d = unit(a, b)
            nx, ny = -d[1], d[0]
            w = widths[i]
            wl = w * (1.0 + rag * ((i % 3) - 1))
            wr = w * (1.0 + rag * (((i + 1) % 3) - 1))
            left.append((pts[i][0] + nx * wl, pts[i][1] + ny * wl))
            right.append((pts[i][0] - nx * wr, pts[i][1] - ny * wr))
        if flat is not None:
            return cv.poly(left + right[::-1], m, flat=flat, **kw)
        return cv.poly(left + right[::-1], m, bevel=1.4, base=0.5, **kw)

    def tail_pts(self, p, k):
        """Centre line of tail k of the hem (frame pixels) and its half-widths."""
        frac, ln, w0, a0, a1 = TAILS[k]
        shred = self.v(p, "shred")
        if frac >= 1.0:
            rx, ry = BACK[0][0] + 1.5, BACK[0][1] - 3.0
        else:
            tips = [h for h in self.hem(p) if h[2]]
            h = min(tips, key=lambda q: abs(q[3] - frac))
            rx, ry = h[0] + 1.0, h[1] - 7.0
        ln = ln + 4.0 * shred
        ang = a0 + (a1 - a0) * p.flow
        if p.crumple:
            ang = ang + (3.08 - ang) * min(1.0, p.crumple)
        q = self.flow_pt(p, rx, ry, 1.0)
        pts = [q]
        n = 13
        curl = 0.0 if p.crumple else 0.7
        for i in range(1, n):
            s = i / (n - 1)
            a = ang + 0.3 * s * math.sin(p.wave - s * 3.4 + k * 1.9) + curl * s * s
            q = pts[-1]
            step = (ln + 13.0) / (n - 1)
            pts.append((q[0] + math.cos(a) * step, q[1] + math.sin(a) * step))
        fp = [self.B(p, q) for q in pts]
        widths = [w0 * (1.0 - 0.8 * (i / (n - 1)) ** 1.1) for i in range(n)]
        return fp, widths

    def _tails(self, cv, p):
        """Long tails of the hem trailing behind; their ends dissolve into wisps of soul fire."""
        for k in range(len(TAILS) - 1, -1, -1):
            fp, wd = self.tail_pts(p, k)
            cut = 8
            cv.part()
            m = self.ribbon(cv, fp[:cut + 1], wd[:cut + 1], ROBE, rag=0.3)
            if m is not None and m.any():
                g = np.clip(np.hypot(cv.px - fp[0][0], cv.py - fp[0][1]) / 26.0, 0, 1)
                cv.inten[m] = np.clip(cv.inten[m] - 0.08 - 0.22 * g[m] - 0.04 * k, 0, 1)
            if p.dead < 1.0:
                cv.part(line=False)
                wisp = [fp[cut - 1]] + fp[cut:]
                ww = [wd[cut - 1] * 0.7] + [w * 0.85 for w in wd[cut:]]
                self.ribbon(cv, wisp[:3], ww[:3], HOT, flat=self.li(p, 0.52), rag=0.0)
                self.ribbon(cv, wisp[2:5], ww[2:5], HOT, flat=self.li(p, 0.36), rag=0.0)
                self.ribbon(cv, wisp[4:], ww[4:], HOT, flat=self.li(p, 0.2), rag=0.0)

    def robe_root(self, p, q):
        """A point of the lower robe (body coordinates, moved like the hem) -> frame."""
        return self.B(p, self.flow_pt(p, q[0], q[1], 1.0))

    def _collar(self, cv, p):
        fp = [self.B(p, q) for q in COLLAR]
        cv.part()
        m = cv.poly(fp, ROBE_FAR, bevel=1.6, base=0.5)
        L = lambda x, y: self.B(p, (x, y))
        cv.part(line=False)
        for a, b in (((93, 81), (88, 64)), ((95, 72), (95, 57)), ((102, 69), (107, 60)), ((109, 74), (113, 82))):
            cv.line(L(*a), L(*b), GOLD, inten=0.5, decal=True)
        if m is not None and m.any():  # the inside of the collar darkens towards the neck
            g = np.clip((cv.px - fp[0][0]) / 18.0, 0, 1)
            cv.inten[m] = np.clip(cv.inten[m] - 0.15 * g[m], 0, 1)

    def _mantle(self, cv, p):
        torn = self.v(p, "shred") > 0.3
        edge = MANTLE_EDGE_TORN if torn else MANTLE_EDGE
        sw = 0.8 * math.sin(p.wave + 0.4)
        edge = [(x - p.flow * 2.0 * (y - 100) / 16.0 + sw * (y - 100) / 16.0, y) for x, y in edge]
        pts = MANTLE_TOP + chaikin(edge, 1)
        fp = [self.B(p, q) for q in pts]
        cv.part()
        m = cv.poly(fp, ROBE, bevel=2.0, base=0.58)
        L = lambda x, y: self.B(p, (x, y))
        if m is not None and m.any():
            g = np.clip((cv.py - fp[2][1]) / 26.0, 0, 1)
            cv.inten[m] = np.clip(cv.inten[m] + 0.12 - 0.22 * g[m], 0, 1)
            # folds: ridges falling into the tips, valleys into the notches
            cv.part(line=False)
            for j, (x, y) in enumerate(edge[1:-2]):
                top = L(x + (112.0 - x) * 0.3, 92.5)
                cv.line(top, L(x, y - 2.0), ROBE, inten=0.3 if j % 2 == 0 else 0.76, decal=True)
        # gold edge along the top, a clasp with a gem
        cv.part(line=False)
        cv.line(L(100, 89), L(110, 86), GOLD, inten=0.55, decal=True)
        cv.line(L(110, 86), L(121, 88), GOLD, inten=0.55, decal=True)
        if not torn:
            cv.part()
            cv.disc(L(124.5, 95), 2.4, GOLD)
            if self.lit(p):
                cv.part(line=False)
                cv.disc(L(124.5, 95), 1.0, CORE, flat=self.li(p, 0.6 + 0.15 * min(1.3, p.glow)))

    def _neck(self, cv, p):
        if math.hypot(*p.head) > 5.0:  # the skull has fallen off (death)
            return
        HL, _ = self.head_fns(p)
        a = HL(*ATLAS)
        b = self.B(p, (114.5, 92.0))
        cv.part()
        for k in range(3):
            t = (k + 0.5) / 3
            cv.disc((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t), 2.5, BONE_FAR)

    # ------------------------------------------------------------ arms
    def bones(self, cv, a, b, m, r=1.15, knob=1.9):
        """Two parallel forearm bones (radius, ulna) from a to b with knobbly ends."""
        d = unit(a, b)
        n = (-d[1], d[0])
        for side in (-1.0, 1.0):
            o = (n[0] * 1.15 * side, n[1] * 1.15 * side)
            cv.part()
            cv.capsule((a[0] + o[0], a[1] + o[1]), (b[0] + o[0] * 0.6, b[1] + o[1] * 0.6), r, r * 0.9, m)
        cv.part()
        cv.disc(b, knob * 0.85, m)

    def humerus(self, cv, a, b, m):
        cv.part()
        cv.capsule(a, b, 1.7, 1.5, m)
        cv.part()
        cv.disc(a, 2.5, m)
        cv.disc(b, 2.2, m)

    def sleeve(self, cv, p, S, E, Wr, m, droop=7.0, salt=0):
        """A wide bell sleeve: the upper arm, then the cuff opening around the forearm, its lower
        edge hanging (tattered) under gravity."""
        cv.part()
        cv.capsule(S, E, 6.0, 5.4, m)
        d = unit(E, Wr)
        n = (d[1], -d[0])  # the "upper" side of the forearm
        if n[1] > 0:
            n = (-n[0], -n[1])
        C2 = (E[0] + (Wr[0] - E[0]) * 0.62, E[1] + (Wr[1] - E[1]) * 0.62)
        up0 = (E[0] + n[0] * 5.4, E[1] + n[1] * 5.4)
        lo0 = (E[0] - n[0] * 5.4, E[1] - n[1] * 5.4)
        up1 = (C2[0] + n[0] * 6.8, C2[1] + n[1] * 6.8)
        lo1 = (C2[0] - n[0] * 7.5, C2[1] - n[1] * 7.5 + droop)
        sw = 1.4 * math.sin(p.wave + salt)
        cuff = []
        for k in range(1, 5):
            t = k / 5
            x = up1[0] + (lo1[0] - up1[0]) * t + d[0] * (1.6 if k % 2 else -0.6) + sw * t
            y = up1[1] + (lo1[1] - up1[1]) * t + d[1] * (1.6 if k % 2 else -0.6) + 1.5 * t * (k % 2)
            cuff.append((x, y))
        lo2 = (lo1[0] + sw - d[0] * 3, lo1[1] + 3.5)
        pts = [up0, up1] + cuff + [lo2, (lo1[0] - d[0] * 7 + sw * 0.5, lo1[1] - 1.0), lo0]
        cv.part()
        mk = cv.poly(pts, m, bevel=1.8, base=0.5)
        if mk is not None and mk.any():
            ys = [q[1] for q in pts]
            g = np.clip((cv.py - min(ys)) / max(1.0, max(ys) - min(ys)), 0, 1)
            cv.inten[mk] = np.clip(cv.inten[mk] + 0.08 - 0.2 * g[mk], 0, 1)
        # the dark inside of the cuff
        cv.part(line=False)
        ci = ((up1[0] + lo1[0]) / 2 - d[0] * 1.5, (up1[1] + lo1[1]) / 2 - d[1] * 1.5)
        cv.ellipse(ci, 1.6, 4.6, VOID, ang=math.atan2(d[1], d[0]), flat=0.2, decal=True)
        return C2

    def open_hand(self, cv, wr, ang, m, spread, curl, sc=1.0):
        """A bony hand at the wrist pointing at `ang`: carpals, three long fingers, the thumb."""
        ca, sa = math.cos(ang), math.sin(ang)

        def L(x, y):
            return (wr[0] + x * ca - y * sa, wr[1] + x * sa + y * ca)

        cv.part()
        cv.ellipse(L(2.2 * sc, 0), 2.6 * sc, 2.0 * sc, m, ang=ang)
        for k, off in enumerate((-1.6, 0.0, 1.6)):
            fa = ang + off * 0.3 * (0.4 + spread)
            b0 = L(3.6 * sc, off * 0.9 * sc)
            pts = [b0]
            a = fa
            for seg, ln in enumerate((3.8, 3.0, 2.4)):
                a += curl * (0.5 + 0.3 * seg)
                q = pts[-1]
                pts.append((q[0] + math.cos(a) * ln * sc, q[1] + math.sin(a) * ln * sc))
            cv.part(line=1)
            cv.chain(pts, [1.0 * sc, 0.95 * sc, 0.8 * sc, 0.55 * sc], m)
        # thumb (on the upper side)
        ta = ang - 0.9 - 0.4 * spread
        t0 = L(2.2 * sc, -2.0 * sc)
        t1 = (t0[0] + math.cos(ta) * 3.2 * sc, t0[1] + math.sin(ta) * 3.2 * sc)
        t2 = (t1[0] + math.cos(ta + curl * 0.8) * 2.4 * sc, t1[1] + math.sin(ta + curl * 0.8) * 2.4 * sc)
        cv.part(line=1)
        cv.chain([t0, t1, t2], [1.0 * sc, 0.9 * sc, 0.55 * sc], m)
        return L(6.5 * sc, 0)

    def _far_arm(self, cv, p):
        S, E, Wr = self.arm(p, False)
        det = self.v(p, "detach")
        ang = p.palm if p.palm is not None else math.atan2(Wr[1] - E[1], Wr[0] - E[0])
        if det <= 0.05:
            self.bones(cv, E, Wr, BONE_FAR)
            self.sleeve(cv, p, S, E, Wr, ROBE_FAR, salt=1.3)
            palm = self.open_hand(cv, Wr, ang, BONE_FAR, p.spread, p.curl)
        else:
            # Form II: the sleeve is gone, the bones float apart, tied by threads of soul fire.
            d1, d2 = unit(S, E), unit(E, Wr)
            w = math.sin(p.t * 1.3)
            g1 = (d1[0] * 5.0 * det + 0.6 * w, d1[1] * 5.0 * det)
            g2 = (g1[0] + d2[0] * 6.0 * det, g1[1] + d2[1] * 6.0 * det - 0.8 * w)
            g3 = (g2[0] + d2[0] * 5.0 * det + 0.5 * w, g2[1] + d2[1] * 5.0 * det)
            # the pieces spread around their places (the hand stays near its target)
            c = (g3[0] * 0.5, g3[1] * 0.5)
            g1, g2, g3 = [(g[0] - c[0], g[1] - c[1]) for g in (g1, g2, g3)]
            hu0 = (S[0] + d1[0] * 2 + g1[0], S[1] + d1[1] * 2 + g1[1])
            hu1 = (E[0] - d1[0] * 1.5 + g1[0], E[1] - d1[1] * 1.5 + g1[1])
            fo0 = (E[0] + d2[0] * 1.5 + g2[0], E[1] + d2[1] * 1.5 + g2[1])
            fo1 = (Wr[0] + g2[0], Wr[1] + g2[1])
            wr3 = (Wr[0] + g3[0], Wr[1] + g3[1])
            # torn sleeve stub at the shoulder
            cv.part()
            cv.capsule(S, (S[0] + d1[0] * 6, S[1] + d1[1] * 6), 6.0, 5.0, ROBE_FAR)
            for k in range(3):
                a0 = (S[0] + d1[0] * (5 + 0.5 * k) + (k - 1) * 3.0, S[1] + d1[1] * 5 + 2)
                cv.part()
                cv.chain([a0, (a0[0] + 0.8 * math.sin(p.wave + k), a0[1] + 5 + k), (a0[0] + 1.5 * math.sin(p.wave + k),
                                                                                    a0[1] + 9 + 1.5 * k)],
                         [1.4, 1.0, 0.5], ROBE_FAR)
            if self.lit(p):  # threads of soul fire across the gaps (under the bones)
                cv.part(line=False)
                for a, b in ((S, hu0), (hu1, fo0), (fo1, wr3)):
                    cv.capsule(a, b, 1.7, 1.7, HOT, flat=self.li(p, 0.38))
                    cv.line(a, b, HOT, inten=self.li(p, 0.74))
                    cv.disc(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), 1.0, CORE, flat=self.li(p, 0.88))
            self.humerus(cv, hu0, hu1, BONE_FAR)
            self.bones(cv, fo0, fo1, BONE_FAR)
            palm = self.open_hand(cv, wr3, ang, BONE_FAR, p.spread, p.curl)
        if p.cast > 0.05 and self.lit(p):
            c = (palm[0] + math.cos(ang) * 1.5, palm[1] + math.sin(ang) * 1.5 - 2.5)
            cv.part(line=False)
            cv.disc(c, 1.6 + 1.8 * p.cast, HOT, flat=self.li(p, 0.5))
            self.flame(cv, (c[0], c[1] + 1), (-0.2 - 0.4 * p.flow, -1.0), 4 + 7 * p.cast, 1.8 + 1.8 * p.cast, p.t + 1.0,
                       k=self.li(p, 1.0))

    def _near_sleeve(self, cv, p):
        S, E, Wr = self.arm(p, True)
        self.bones(cv, E, Wr, BONE)
        self.sleeve(cv, p, S, E, Wr, ROBE, salt=0.0)
        # a rune on the cuff
        d = unit(E, Wr)
        c = (E[0] + (Wr[0] - E[0]) * 0.28, E[1] + (Wr[1] - E[1]) * 0.28 + 1.0)
        self.rune(cv, p, 0, c, math.atan2(d[1], d[0]) - PI / 2, 0.45 + 0.3 * min(1.3, p.glow))

    def _near_hand(self, cv, p):
        S, E, Wr = self.arm(p, True)
        if p.free is not None:
            self.open_hand(cv, Wr, math.atan2(Wr[1] - E[1], Wr[0] - E[0]) + 0.6, BONE, 0.3, 0.7)
            return
        g, a = self.staff_frame(p)
        Sf = self.S(p)
        d = unit(E, Wr)
        # carpals, three metacarpals to the knuckles, finger bones wrapped round the shaft, the thumb
        cv.part()
        cv.ellipse((Wr[0] + d[0] * 1.2, Wr[1] + d[1] * 1.2), 2.3, 1.9, BONE, ang=math.atan2(d[1], d[0]))
        for k, s in enumerate((2.6, 0.2, -2.2)):
            kn = Sf(s, -3.0)
            cv.part(line=1)
            cv.capsule((Wr[0] + d[0] * 2.0, Wr[1] + d[1] * 2.0), kn, 0.85, 0.95, BONE)
            cv.part(line=1)
            cv.disc(kn, 1.3, BONE)
            cv.part(line=1)
            cv.capsule(kn, Sf(s - 0.4, 2.7), 1.0, 0.8, BONE)
        cv.part(line=1)
        cv.chain([(Wr[0] + d[0] * 1.5, Wr[1] + d[1] * 1.5), Sf(4.6, -2.6), Sf(6.8, -0.4)], [1.0, 0.95, 0.6], BONE)

    # ------------------------------------------------------------ skull and crown
    def _skull(self, cv, p):
        HL, JL = self.head_fns(p)
        a = p.lean + p.tilt
        lit = self.lit(p)
        # jaw (behind the skull)
        cv.part()
        cv.poly([JL(*q) for q in ((-5.5, 4), (-2, 7.5), (3, 10.6), (12.5, 10.6), (13, 13), (10.5, 15.2), (3.5, 15),
                                  (-1.5, 13), (-4.5, 9.5))], BONE, bevel=1.2, base=0.5)
        cv.part(line=False)
        for xx in (5.0, 7.2, 9.4, 11.4):
            cv.line(JL(xx, 10.8), JL(xx, 11.8), BONE, inten=0.95, decal=True)
        cv.line(JL(4.0, 12.4), JL(12.0, 12.4), BONE, inten=0.3, decal=True)
        if p.jaw > 0.15:  # the dark mouth between the jaws
            cv.part(line=False)
            cv.poly([HL(3, 9.6), HL(12.5, 9.6), JL(12.5, 10.8), JL(3, 10.8)], VOID, flat=0.2)
        # cranium + face
        cv.part()
        cv.ellipse(HL(-1.0, -1.5), 11.5, 10.8, BONE, ang=a, bias=0.04)
        cv.part(line=False)
        face = [(3, -7), (9, -6), (12.5, -2.5), (13.6, 1.5), (13.2, 5.5), (12.6, 9.6), (3.5, 10.2), (-1, 8.5),
                (-6, 6.5), (-6, 1)]
        cv.poly([HL(*q) for q in face], BONE, bevel=1.4, base=0.6)
        # temple hollow, cheekbone
        cv.part(line=False)
        cv.ellipse(HL(-2.5, 3.5), 3.6, 2.4, BONE, ang=a, flat=0.4, decal=True)
        cv.line(HL(0.5, 6.2), HL(8.5, 6.6), BONE, inten=0.92, decal=True)
        cv.line(HL(0.5, 7.2), HL(8.5, 7.8), BONE, inten=0.36, decal=True)
        # upper teeth
        cv.line(HL(4.0, 9.4), HL(12.4, 9.4), BONE, inten=0.95, decal=True)
        for xx in (5.0, 7.2, 9.4, 11.4):
            cv.line(HL(xx, 8.6), HL(xx, 9.8), BONE, inten=0.32, decal=True)
        # crack across the cranium
        cv.line(HL(-6, -11.5), HL(-4, -8), BONE, inten=0.2, decal=True)
        cv.line(HL(-4, -8), HL(-5.5, -5), BONE, inten=0.2, decal=True)
        cv.line(HL(-5.5, -5), HL(-3, -2.5), BONE, inten=0.2, decal=True)
        # sockets: the near one round and deep, the far one a sliver at the front; the nose hole
        cv.part(line=False)
        cv.ellipse(HL(5.0, 0.5), 3.5, 3.7, VOID, ang=a, flat=0.2)
        cv.ellipse(HL(12.0, 0.6), 1.3, 3.0, VOID, ang=a, flat=0.2)
        cv.poly([HL(10.2, 3.6), HL(11.8, 3.4), HL(11.6, 6.6), HL(10.4, 6.4)], VOID, flat=0.25)
        cv.line(HL(1.8, -3.6), HL(8.6, -3.4), BONE, inten=0.95, decal=True)  # the brow ridge catches the light
        e = min(1.6, p.eye)
        if lit and p.eye > 0.02:
            k = self.li(p, 1.0)
            cv.ellipse(HL(5.4, 0.7), 1.6 + 0.5 * e, 1.8 + 0.5 * e, HOT, ang=a, flat=0.55 * k + 0.1)
            cv.ellipse(HL(5.6, 0.7), 0.9 + 0.3 * e, 1.1 + 0.3 * e, CORE, ang=a, flat=(0.62 + 0.2 * e) * k)
            cv.ellipse(HL(12.1, 0.8), 0.6, 1.5 + 0.3 * e, HOT, ang=a, flat=(0.6 + 0.15 * e) * k)
        if lit and p.eye > 0.2:
            # the fire of the eye streams back over the temple, its tip curling up
            base = HL(5.0, 0.2)
            dirv = (-1.0, -0.42 - 0.25 * math.sin(p.t * 1.7) + 0.15 * p.flow)
            self.flame(cv, base, dirv, 4.5 + 6.0 * e, 1.4 + 0.9 * e, p.t * 1.3, k=self.li(p, 1.0), curl=0.7)
            cv.part(line=False)
            cv.ellipse(HL(5.5, 0.7), 0.9 + 0.3 * e, 1.1 + 0.3 * e, CORE, ang=a, flat=self.li(p, 0.9 + 0.05 * e))
        self._crown(cv, p, HL, a)

    def _crown(self, cv, p, HL, a):
        split = self.v(p, "split")
        off = p.crown if p.crown is not None else (0.0, 0.0, 0.0)
        cut = 0.42
        c0, c1 = (-11.0, -6.0), (9.8, -9.4)   # the band's lower edge, back to front
        hb = 3.6
        spikes = [(0.04, 5.5), (0.27, 8.5), (0.5, 10.5), (0.73, 8.5), (0.95, 6.0)]

        def CL(x, y, half):
            """Crown-local -> frame (the front half tilts and lifts when split; the whole crown
            can be knocked off)."""
            if half and split > 0:
                px, py = c0[0] + (c1[0] - c0[0]) * cut, c0[1] + (c1[1] - c0[1]) * cut - hb
                r = 0.42 * split
                dx, dy = x - px, y - py
                x = px + dx * math.cos(r) - dy * math.sin(r) + 1.6 * split
                y = py + dx * math.sin(r) + dy * math.cos(r) - 3.4 * split
            if off[2]:
                cx, cy = 0.0, -9.0
                dx, dy = x - cx, y - cy
                x = cx + dx * math.cos(off[2]) - dy * math.sin(off[2])
                y = cy + dx * math.sin(off[2]) + dy * math.cos(off[2])
            q = HL(x, y)
            return (q[0] + off[0], q[1] + off[1])

        ba = math.atan2(c1[1] - c0[1], c1[0] - c0[0])
        nrm = (math.sin(ba), -math.cos(ba))
        halves = [(0.0, cut, 0), (cut, 1.0, 1)] if split > 0 else [(0.0, 1.0, 0)]
        lit = self.lit(p)
        for (s0, s1, half) in halves:
            def at(s, h, sp=0.0):
                x = c0[0] + (c1[0] - c0[0]) * s + nrm[0] * h
                y = c0[1] + (c1[1] - c0[1]) * s + nrm[1] * h
                return (x, y)
            # spikes (behind the band)
            cv.part()
            for (s, hgt) in spikes:
                if not s0 <= s <= s1:
                    continue
                fan = (s - 0.5) * 0.5
                bx, by = at(s, hb - 0.5)
                ta = -PI / 2 + ba + fan
                tip = (bx + math.cos(ta) * hgt, by + math.sin(ta) * hgt)
                w = 2.3
                cv.poly([CL(bx - w * math.cos(ba), by - w * math.sin(ba), half), CL(*tip, half),
                         CL(bx + w * math.cos(ba), by + w * math.sin(ba), half)], GOLD, bevel=1.0, base=0.6)
                cv.disc(CL(*tip, half), 1.05, GOLD, flat=0.9)
            # band
            cv.part()
            band = [at(s0, 0), at(s1, 0), at(s1, hb), at(s0, hb)]
            cv.poly([CL(x, y, half) for x, y in band], GOLD, bevel=1.1, base=0.56)
            cv.part(line=False)
            cv.line(CL(*at(s0, hb - 0.8), half), CL(*at(s1, hb - 0.8), half), GOLD, inten=0.9, decal=True)
            cv.line(CL(*at(s0, 0.3), half), CL(*at(s1, 0.3), half), GOLD, inten=0.3, decal=True)
            # gems
            for (s, r, big) in ((0.5, 1.3, True), (0.2, 0.9, False), (0.8, 0.9, False)):
                if not s0 <= s <= s1:
                    continue
                gc = CL(*at(s, hb * 0.5), half)
                if lit:
                    v = (0.7 if big else 0.62) + 0.12 * min(1.3, p.glow)
                    cv.disc(gc, r, CORE if big else HOT, flat=self.li(p, v))
                else:
                    cv.disc(gc, r, VOID, flat=0.4)
        # the crack (form I: dark; split: a glowing gap)
        a0 = at(cut, 0)
        a1 = at(cut - 0.03, hb)
        a2 = at(0.27, hb + 4.0)
        cv.part(line=False)
        if split > 0 and lit:
            cv.line(CL(*a0, 0), CL(*a1, 0), HOT, inten=self.li(p, 0.8))
            cv.line(CL(*a0, 1), CL(*a1, 1), HOT, inten=self.li(p, 0.6))
        else:
            cv.line(CL(*a0, 0), CL(*a1, 0), GOLD, inten=0.1, decal=True)
            cv.line(CL(*a1, 0), CL(*a2, 0), GOLD, inten=0.1, decal=True)

    # ------------------------------------------------------------ staff
    def _staff(self, cv, p):
        Sf = self.S(p)
        g, a = self.staff_frame(p)
        top = STAFF_UP
        # shaft: gnarled dark wood
        pts, radii = [], []
        n = 14
        for k in range(n + 1):
            s = -STAFF_DN + (STAFF_DN + top) * k / n
            pts.append(Sf(s, 0.7 * math.sin(s * 0.17 + 1.0)))
            radii.append(1.9 + 0.35 * (k / n))
        cv.part()
        mk = cv.chain(pts, radii, WOOD)
        if mk is not None and mk.any():
            # spiral grooves
            ln = (cv.px - g[0]) * math.sin(a) - (cv.py - g[1]) * math.cos(a)
            cr = (cv.px - g[0]) * math.cos(a) + (cv.py - g[1]) * math.sin(a)
            groove = np.mod(ln * 0.32 + cr * 0.9, 3.2) < 0.55
            cv.inten[mk & groove] = np.clip(cv.inten[mk & groove] - 0.3, 0, 1)
        # gold bands, the spike at the foot
        for s in (-STAFF_DN + 7.0, -30.0, top - 5.0):
            cv.part()
            cv.capsule(Sf(s, -2.7), Sf(s, 2.7), 1.25, 1.25, GOLD)
        cv.part()
        cv.poly([Sf(-STAFF_DN + 1.5, -2.2), Sf(-STAFF_DN + 1.5, 2.2), Sf(-STAFF_DN - 7.5, 0.3)], GOLD, bevel=1.0,
                base=0.6)
        # headpiece: a gold cup, the crystal, the claws gripping it
        cv.part()
        cv.poly([Sf(top - 3, -2.6), Sf(top - 3, 2.6), Sf(top + 2.5, 4.6), Sf(top + 3.5, 0), Sf(top + 2.5, -4.6)], GOLD,
                bevel=1.2, base=0.6)
        self._crystal(cv, p, Sf)
        for side in (-1.0, 1.0):
            cv.part()
            cv.chain([Sf(top + 1.5, side * 4.0), Sf(top + 6, side * 6.6), Sf(top + 12, side * 6.2),
                      Sf(top + 17.5, side * 3.4)], [1.5, 1.35, 1.05, 0.5], GOLD)

    def _crystal(self, cv, p, Sf):
        c = STAFF_UP + CRY
        k = min(1.6, p.crystal)
        lit = self.lit(p)
        m = CORE if lit else DEAD
        top, bot = Sf(c + CRY_TOP), Sf(c - CRY_BOT)
        l0, l1 = Sf(c - 3.5, -CRY_W), Sf(c + 4.5, -CRY_W)
        r0, r1 = Sf(c - 3.5, CRY_W), Sf(c + 4.5, CRY_W)
        mid0, mid1 = Sf(c - 4.5, 0.4), Sf(c + 6.0, 0.4)
        if lit:
            lv, rv, rid = self.li(p, 0.55 + 0.2 * k), self.li(p, 0.36 + 0.2 * k), self.li(p, 0.78 + 0.16 * k)
        else:
            lv, rv, rid = 0.7, 0.4, 0.9
        cv.part()
        cv.poly([bot, l0, l1, top, mid1, mid0], m, flat=lv)
        cv.part(line=False)
        cv.poly([bot, mid0, mid1, top, r1, r0], m, flat=rv)
        cv.line(mid0, mid1, m, inten=rid)
        cv.line(Sf(c + 6.5, -2.0), Sf(c + 8.5, -1.0), m, inten=min(1.0, rid + 0.1))
        if lit:
            cv.px_set(*Sf(c + 2.0, -1.5), CORE, inten=self.li(p, 0.8 + 0.12 * k))
        crack = self.v(p, "crack")
        if crack > 0.05:
            # a crack across the crystal; at full crack a chip is missing and the light leaks out
            pts = [Sf(c + 7.0, -CRY_W), Sf(c + 3.5, -1.0), Sf(c + 1.5, 1.2), Sf(c - 1.0, -0.6), Sf(c - 4.0, CRY_W)]
            nseg = max(1, int(round(crack * (len(pts) - 1))))
            cv.part(line=False)
            for q0, q1 in zip(pts[:nseg], pts[1:nseg + 1]):
                cv.line(q0, q1, VOID, inten=0.15)
            if lit:
                for q0, q1 in zip(pts[:nseg], pts[1:nseg + 1]):
                    cv.line((q0[0] + 1, q0[1]), (q1[0] + 1, q1[1]), CORE, inten=0.95, decal=True)
            if crack > 0.9:
                chip = cv.poly_mask([Sf(c + 4.5, CRY_W + 0.5), Sf(c + 9.5, 1.5), Sf(c + 3.0, 1.6)])
                cv.erase(chip & (cv.pid == cv.cur - 1))
                cv.erase(chip & (cv.pid == cv.cur))

    # ------------------------------------------------------------ fire and runes
    def flame(self, cv, base, dirv, length, width, t, clip=None, k=1.0, curl=0.0):
        """A soul flame from `base` along `dirv` (three layers: outer, middle, core); `curl` bends
        it towards its tip (radians over its length, > 0 clockwise)."""
        a0 = math.atan2(dirv[1], dirv[0])
        for layer, (m, wf, lf, it) in enumerate(((HOT, 1.0, 1.0, 0.56), (HOT, 0.62, 0.74, 0.76),
                                                  (CORE, 0.32, 0.46, 0.9))):
            Lx = length * lf
            left, right = [], []
            nseg = 8
            cx, cy = base
            for i in range(nseg + 1):
                s = i / nseg
                a = a0 + curl * s * s
                ux, uy = math.cos(a), math.sin(a)
                nx, ny = -uy, ux
                if i:
                    cx, cy = cx + ux * Lx / nseg, cy + uy * Lx / nseg
                wd = width * wf * math.sin(min(1.0, s * 1.8 + 0.25) * PI / 2) * (1.0 - s) ** 0.8
                wig = 0.9 * s * math.sin(t * 2.1 + s * 5.0 + layer) * width * 0.5
                left.append((cx + nx * (wd + wig), cy + ny * (wd + wig)))
                right.append((cx - nx * (wd - wig), cy - ny * (wd - wig)))
            cv.part(line=False)
            kw = {} if clip is None else {"clip": clip}
            cv.poly(left + right[::-1], m, flat=it * k, **kw)

    def rune(self, cv, p, k, c, ang, inten, size=1.25):
        """Rune glyph k (3x5 box) centred on c, embroidered in soul fire (dark when extinct); a halo
        around the strokes when it flares."""
        ca, sa = math.cos(ang), math.sin(ang)

        def L(x, y):
            x, y = (x - 1.0) * size, (y - 2.0) * size
            return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca)

        lit = self.lit(p)
        m = HOT if lit else TRIM
        cv.part(line=False)
        if lit and inten > 0.8:
            for (a, b) in RUNES[k % len(RUNES)]:
                cv.capsule(L(*a), L(*b), 0.9, 0.9, HOT, flat=self.li(p, 0.32 + 0.1 * (inten > 1.0)), decal=True)
        v = self.li(p, inten) if lit else 0.2
        for (a, b) in RUNES[k % len(RUNES)]:
            cv.line(L(*a), L(*b), m, inten=v, decal=True)

    # ------------------------------------------------------------- effects (on rendered frames)
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

    def wisps(self, f, p, i, n, seed, count=9):
        """Soul motes drifting up from the hem (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        for k in range(count):
            x0, y0 = self.robe_root(p, (86 + rng.random() * 50, 178 + rng.random() * 8))
            t = (i / n + rng.random()) % 1.0
            x = x0 - t * (8 + 14 * p.flow) + math.sin(t * 6 + k) * 2.0
            y = y0 - t * 34
            put_if_empty(f, x, y, [PC[1], PC[2], PC[3]][min(2, int(t * 3))])

    def spectre(self, f, x, y, s, level, t, seed, under=True):
        """A soul of the dead: a skull of soul fire (dark sockets, a grim mouth) whose flames stream
        down into a wavering tail and lick up from its crown (level 0 bright .. 3 dim; s = size)."""
        cv = TCanvas(W, H)
        k = max(0.25, 1.0 - 0.2 * level)
        # tail: a downward flame (outer, middle layers)
        for layer, (wf, it) in enumerate(((1.0, 0.5), (0.55, 0.74))):
            left, right = [], []
            n = 9
            for i in range(n + 1):
                u = i / n
                cx = x + 1.6 * s * math.sin(t * 2.0 + u * 4.2 + seed) * u
                cy = y + 1.5 * s + u * 10.0 * s
                wd = 3.1 * s * wf * (1.0 - u) ** 0.9
                left.append((cx - wd, cy))
                right.append((cx + wd * (0.8 + 0.3 * math.sin(u * 7 + t)), cy))
            cv.part(line=False)
            cv.poly(left + right[::-1], HOT, flat=it * k)
        # crest: three small flames from the top of the head
        for j, (dx, hl, wd) in enumerate(((-1.5, 0.45, 0.8), (1.6, 0.4, 0.8), (0.0, 1.0, 1.5))):
            ln = s * hl * (5.0 + 1.6 * math.sin(t * 2.3 + j * 2.1 + seed))
            self.flame(cv, (x + dx * s, y - 2.0 * s), (0.1 * dx - 0.25, -1.0), ln, wd * s, t + j, k=k)
        # the skull of fire
        cv.part(line=False)
        cv.ellipse((x, y), 3.4 * s, 3.2 * s, HOT, flat=0.56 * k)
        cv.ellipse((x - 0.3 * s, y - 0.5 * s), 2.6 * s, 2.4 * s, HOT, flat=0.76 * k)
        cv.ellipse((x - 0.2 * s, y + 2.4 * s), 2.0 * s, 1.3 * s, HOT, flat=0.66 * k)
        if level < 2:
            cv.ellipse((x - 1.0 * s, y - 1.3 * s), 1.1 * s, 0.8 * s, CORE, flat=0.92 * k)
        cv.part(line=False)
        for ex in (-1.25, 1.25):
            cv.ellipse((x + ex * s, y + 0.4 * s), 0.85 * s, 1.0 * s, VOID, flat=0.2)
        cv.ellipse((x, y + 1.7 * s), 0.35 * s, 0.5 * s, VOID, flat=0.2)
        cv.line((x - 1.2 * s, y + 2.6 * s), (x + 1.2 * s, y + 2.6 * s), VOID, inten=0.2)
        layer = cv.render(outline=False)
        if under:
            FX.under(f, layer)
        else:
            FX.overlay(f, layer)
        return f

    # ================================================================= clips
    def idle(self, broken=False):
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            s = math.sin(ph)
            p = LPose(t=ph, broken=broken, dy=-3.0 * s, wave=ph, lean=0.015 * math.sin(ph - 0.6),
                      head=(0.0, 0.6 * math.sin(ph - 0.9)), tilt=0.03 * math.sin(ph - 1.2),
                      grip=(151.0, 124.0 + 0.8 * math.sin(ph - 0.6)), staff=0.04 + 0.015 * s,
                      hand=(135.0 + 1.0 * math.sin(ph + 1.0), 97.0 + 1.4 * math.sin(ph + 0.8)), palm=-1.3,
                      spread=0.6, curl=0.35, cast=0.35 + 0.1 * s, glow=0.6 + 0.12 * s,
                      eye=0.6 + 0.1 * math.sin(2 * ph), crystal=0.65 + 0.12 * s)
            f = self.draw(p)
            self.wisps(f, p, i, n, self.seed + 1)
            out.append(f)
        return out

    def walk(self, broken=False):
        """Glide (= move): it leans into the motion, the robe streams behind."""
        out = []
        n = 8
        for i in range(n):
            ph = TAU * i / n
            p = LPose(t=ph * 1.5, broken=broken, dy=-1.5 * math.sin(ph), wave=ph * 1.0 + 0.5, lean=0.08, flow=1.0,
                      head=(0.0, 0.4 * math.sin(ph)), tilt=0.04, grip=(152.0, 124.0), staff=0.2,
                      hand=(136.0, 112.0 + 1.0 * math.sin(ph)), palm=-0.6, spread=0.4, curl=0.5, cast=0.25,
                      glow=0.65, eye=0.7, crystal=0.7)
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for j in range(7):
                y = int(rng.integers(50, 200))
                x0 = int(rng.integers(4, 34))
                streak(f, (x0, y), (x0 + int(rng.integers(6, 16)), y), PC[2 + j % 3])
            self.wisps(f, p, i, n, self.seed + 2, count=7)
            out.append(f)
        return out

    def telegraph(self, broken=False):
        """It raises the staff; the runes and the phylactery flare, soul motes gather."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            p = LPose(t=0.7 * i, broken=broken, wave=0.6 * i, lean=-0.05 * e, dy=-2 * e, tilt=-0.18 * e,
                      grip=(151.0 - 3 * e, 124.0 - 26 * e), staff=0.04 - 0.14 * e, elbow_n=(-6.0, 14.0),
                      hand=(140.0 - 4 * e, 106.0 - 8 * e), palm=-1.3, spread=0.6 + 0.4 * e, cast=0.35 + 0.4 * e,
                      glow=0.6 + 0.8 * t, eye=0.6 + 0.5 * t, crystal=0.65 + 0.9 * t)
            f = self.draw(p)
            x, y = self.crystal(p)
            self.charge(f, x, y, i, 6, self.seed + 20 + i, r0=30, r1=9, count=6 + 2 * i)
            if i >= 4:
                self.sparkle(f, x - 1, y - 2, 3 + (i - 4) * 2)
            out.append(f)
        return out

    def attack_poses(self, broken=False):
        keys = [  # grip, staff angle, lean, crystal, far hand
            ((147.0, 108.0), 0.3, 0.0, 1.4, (142.0, 100.0)),
            ((131.0, 108.0), 0.8, 0.06, 1.6, (146.0, 98.0)),
            ((133.0, 109.0), 0.74, 0.05, 1.3, (145.0, 100.0)),
            ((145.0, 114.0), 0.48, 0.03, 1.0, (144.0, 103.0)),
            ((149.0, 121.0), 0.15, 0.01, 0.8, (141.0, 106.0)),
        ]
        out = []
        for i, (g, a, lean, cr, hd) in enumerate(keys):
            out.append(LPose(t=0.5 * i + 3, broken=broken, wave=0.5 * i, lean=lean, grip=g, staff=a,
                             elbow_n=(-8.0, 16.0), hand=hd, palm=-0.4 - 0.2 * i, spread=0.9, curl=0.15,
                             cast=0.3, glow=0.7 + 0.3 * cr / 1.6, eye=0.6 + 0.6 * cr / 1.6, crystal=cr,
                             tilt=-0.04 * (i < 3)))
        return out

    def attack(self, broken=False):
        """The staff is thrust forward: a bolt of soul fire leaves the crystal on f1 (ORIGIN)."""
        out = []
        for i, p in enumerate(self.attack_poses(broken)):
            f = self.draw(p)
            x, y = self.origin(p)
            g, a = self.staff_frame(p)
            u = (math.sin(a), -math.cos(a))
            if i == 0:
                self.charge(f, x, y, 4, 6, self.seed + 39, r0=12, r1=3, count=8)
            if i == 1:
                FX.burst(f, x + u[0] * 2, y + u[1] * 2, 6, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 40,
                         jag=0.35, rays=5)
                for k in range(3):  # the bolt's trail of sparks, ahead of the crystal
                    o = (-u[1] * (k - 1) * 3, u[0] * (k - 1) * 3)
                    a0, a1 = 8 + 3 * k, 15 + 3 * k
                    streak(f, (x + u[0] * a0 + o[0], y + u[1] * a0 + o[1]), (x + u[0] * a1 + o[0], y + u[1] * a1 + o[1]),
                           PC[1 + k % 2])
            if i == 2:
                ring(f, x, y, 7, PC[1])
                ring(f, x, y, 11, PC[2], dotted=2)
            if i == 3:
                motes(f, x, y, 10, 5, 16, [WHITE_C, PC[0], PC[1]], seed=self.seed + 45)
            out.append(f)
        return out

    def telegraph_special(self, broken=False):
        """Both hands raised, the eyes blaze; soul motes are drawn to the eyes and the crystal."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            e = t * t * (3 - 2 * t)
            p = LPose(t=0.8 * i, broken=broken, wave=0.7 * i, lean=-0.04 * e, dy=-2 * e, tilt=-0.14 * e,
                      jaw=0.25 * e, grip=(151.0 - 6 * e, 124.0 - 24 * e), staff=0.04 - 0.08 * e, elbow_n=(-6.0, 14.0),
                      hand=(140.0 - 40 * e, 106.0 - 62 * e), palm=-1.2 - 0.5 * e, elbow_f=(-14.0, -4.0),
                      spread=0.6 + 0.4 * e, curl=0.3 - 0.2 * e, cast=0.35 + 0.65 * e,
                      glow=0.6 + 0.7 * t, eye=0.6 + 1.0 * t, crystal=0.65 + 0.7 * t)
            f = self.draw(p)
            ex, ey = self.eye_pos(p)
            self.charge(f, ex, ey, i, 6, self.seed + 50 + i, r0=34, r1=10, count=5 + 2 * i)
            x, y = self.crystal(p)
            self.charge(f, x, y, i, 6, self.seed + 56 + i, r0=26, r1=9, count=4 + i)
            if i == 5:
                self.sparkle(f, ex + 2, ey, 5)
            out.append(f)
        return out

    def special_pose(self, i, broken=False):
        return LPose(t=1.6 * i, broken=broken, wave=PI / 2 * i, lean=0.0, grip=(124.0, 116.0), staff=1.12,
                     elbow_n=(-10.0, 12.0), hand=(144.0, 92.0), palm=-0.3, elbow_f=(-2.0, 20.0), spread=1.0, curl=0.1,
                     cast=0.8 + 0.1 * (i % 2), glow=1.3, eye=1.4 + 0.1 * (i % 2), crystal=1.5)

    def special(self, broken=False):
        """Beam (loop): the staff levelled, a burst at the crystal (the beam is drawn by the game)."""
        out = []
        for i in range(4):
            p = self.special_pose(i, broken)
            f = self.draw(p)
            x, y = self.origin(p)
            FX.burst(f, x + 1, y, 5 + (i % 2) * 2, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 60 + i, jag=0.3,
                     rays=3)
            motes(f, x - 4, y, 8, 6, 14, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 64 + i)
            out.append(f)
        return out

    def summon(self, broken=False):
        """Arms spread wide, it calls the dead: souls rise from the ground around it."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.25]
        rise = [0.0, 0.15, 0.4, 0.65, 0.85, 1.0]
        souls = [(28.0, 1.5, 0.0, 0.95), (60.0, 1.25, 0.15, 0.75), (174.0, 1.35, 0.05, 0.85), (204.0, 1.5, 0.2, 1.0)]
        out = []
        for i, h in enumerate(keys):
            p = LPose(t=0.7 * i, broken=broken, wave=0.6 * i, lean=-0.04 * h, dy=-2 * h, tilt=-0.2 * h, jaw=0.5 * h,
                      grip=(151.0 + 8 * h, 124.0 - 14 * h), staff=0.04 + 0.2 * h, elbow_n=(-4.0, 18.0),
                      hand=(140.0 - 72 * h, 106.0 - 24 * h), palm=-1.6 - 1.2 * h, elbow_f=(-6.0, -10.0),
                      spread=0.6 + 0.4 * h, curl=0.3 - 0.2 * h, cast=0.35 + 0.5 * h,
                      glow=0.6 + 0.7 * h, eye=0.6 + 0.8 * h, crystal=0.65 + 0.6 * h)
            f = self.draw(p)
            # a circle of runes glowing on the ground below it
            if i >= 1:
                s = min(1.0, 0.4 + 0.3 * i)
                dash_arc(f, 112, 210, 70 * s, 7 * s, 0.0, TAU, PC[2 if i < 5 else 3], dash=3, gap=2, phase=i)
                dash_arc(f, 112, 210, 52 * s, 5 * s, 0.0, TAU, PC[1 if i < 5 else 3], dash=2, gap=3, phase=i + 1)
            for k, (x, sz, delay, top) in enumerate(souls):
                r = max(0.0, min(1.0, (rise[i] - delay) / (1.0 - delay)))
                if r <= 0.0:
                    continue
                y = 196 - 76 * top * r
                lvl = 0 if i < 4 else (1 if i == 4 else 2)
                self.spectre(f, x, y, sz, lvl, 0.9 * i + k, k)
                if r < 0.5:
                    FX.burst(f, x, 208, 3 + 2 * r, [PC[1], PC[2], PC[3]], seed=self.seed + 70 + 5 * i + k, jag=0.5)
            motes(f, 112, 190, 6 + 3 * i, 30, 96, [PC[0], PC[1], PC[2]], seed=self.seed + 80 + i, arc=(-2.9, -0.25))
            out.append(f)
        return out

    def roar_pose(self, h, i, broken, **kw):
        d = dict(t=0.8 * i, broken=broken, wave=0.6 * i, lean=-0.07 * h, dy=-2 * h, tilt=-0.42 * h, jaw=h,
                 grip=(151.0 + 4 * h, 124.0 - 22 * h), staff=0.04 + 0.12 * h, elbow_n=(-4.0, 16.0),
                 hand=(140.0 - 46 * h, 106.0 - 40 * h), palm=-1.6 - 0.9 * h, elbow_f=(-8.0, -6.0),
                 spread=0.6 + 0.4 * h, curl=0.3 - 0.25 * h, cast=0.35 + 0.5 * h,
                 glow=0.6 + 0.8 * h, eye=0.6 + 1.0 * h, crystal=0.65 + 0.8 * h)
        d.update(kw)
        return LPose(**d)

    def _roar_fx(self, f, p, i):
        x, y = self.mouth(p)
        for k, r in enumerate((7 + 2 * (i % 2), 13 + 2 * (i % 2), 19 + 2 * (i % 2))):
            ring(f, x + 2, y, r, [PC[1], PC[2], PC[3]][k], -1.5, 0.7, dotted=0 if k < 2 else 2)
        motes(f, 112, 120, 18, 50, 96, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 85 + i, big=0.2,
              arc=(-3.1, 0.2))

    def roar(self, broken=False):
        """Phase change: it throws its head back and screams, arms spread; the soul fire flares."""
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
        """Phase 5 roar: the phylactery cracks, the crown splits, the robe tears - form II."""
        seq = [  # broken, h, crack, flash
            (False, 0.4, 0.0, 0.0), (False, 0.85, 0.4, 0.0), (False, 1.0, 0.8, 0.35), (True, 1.0, 1.0, 0.0),
            (True, 1.0, 1.0, 0.0), (True, 0.9, 1.0, 0.0), (True, 0.4, 1.0, 0.0), (True, 0.0, 1.0, 0.0)]
        shk = [0, 1, -1, 2, -1, 1, 0, 0]
        out = []
        for i, (broken, h, crack, fl) in enumerate(seq):
            p = self.roar_pose(h, i, broken, crack=crack, glow=0.6 + 0.9 * h)
            f = self.draw(p)
            cx, cy = self.crystal(p)
            HL, _ = self.head_fns(p)
            kx, ky = HL(0.0, -12.0)
            hx, hy = self.heart(p)
            if i == 3:
                FX.burst(f, cx, cy, 11, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 90, jag=0.4, rays=8)
                FX.burst(f, kx, ky - 2, 7, [WHITE_C, PC[0], PC[1]], seed=self.seed + 91, jag=0.4, rays=5)
                FX.burst(f, hx + 4, hy, 9, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 92, jag=0.45, rays=6)
            if 3 <= i <= 5:
                rng = np.random.default_rng(self.seed + 93 + i)
                d0 = 14 + 12 * (i - 3)
                def inside(x, y, m=6):
                    return m <= x < W - m and m <= y < H - m
                for k in range(7):  # crystal shards
                    a = rng.random() * TAU
                    dd = d0 + rng.random() * 8
                    x, y = cx + math.cos(a) * dd, cy + math.sin(a) * dd
                    if inside(x, y):
                        shard(f, x, y, a, 3 + k % 2, PC[0], PC[2])
                for k in range(4):  # gold splinters of the crown
                    a = -PI / 2 + (rng.random() - 0.5) * 2.2
                    dd = 8 + 9 * (i - 3) + rng.random() * 6
                    x, y = kx + math.cos(a) * dd, ky + math.sin(a) * dd
                    if inside(x, y):
                        FX.chunk(f, x, y, 2, GOLD_L, GOLD_D)
                for k in range(8):  # rags torn off the robe
                    a = -0.9 + rng.random() * 2.2
                    dd = 18 + 14 * (i - 3) + rng.random() * 10
                    FX.chunk(f, hx + math.cos(a) * dd, hy + math.sin(a) * dd * 0.8 + 5 * (i - 3), 3 if k % 2 else 2,
                             SCRAP_L, SCRAP_D)
                ring(f, cx, cy, 11 + 7 * (i - 3), PC[1 + (i - 3)], dotted=0 if i == 3 else 2)
            if h > 0.8:
                self._roar_fx(f, p, i)
            if shk[i]:
                f = shake(f, shk[i], 0)
            if fl:
                f = flashed(f, fl)
            out.append(f)
        return out

    def hurt(self, broken=False):
        a = LPose(t=0.3, broken=broken, dx=-5, lean=-0.12, tilt=-0.3, jaw=0.55, grip=(146.0, 120.0), staff=-0.12,
                  hand=(130.0, 98.0), palm=-2.0, spread=1.0, curl=0.0, cast=0.0, glow=0.95, eye=1.1, crystal=1.0,
                  wave=0.4)
        b = LPose(t=0.6, broken=broken, dx=-2, lean=-0.05, tilt=-0.1, jaw=0.2, grip=(149.0, 122.0), staff=-0.03,
                  hand=(136.0, 103.0), palm=-1.4, spread=0.7, curl=0.3, cast=0.15, glow=0.75, eye=0.8, crystal=0.8,
                  wave=0.9)
        fa = flashed(self.draw(a), 0.55)
        fb = self.draw(b)
        motes(fb, 112, 118, 12, 40, 70, [WHITE_C, PC[0], PC[1]], seed=self.seed + 110)
        return [fa, fb]

    def death(self, broken=False):
        out = []
        # 0-2: convulsions: the soul fire flares, gutters, bursts out of the chest
        conv = [self.roar_pose(1.0, 0, broken, tilt=-0.5, jaw=0.95, eye=1.6, crystal=1.6, glow=1.5),
                LPose(t=0.9, broken=broken, dy=3, lean=0.08, tilt=0.3, jaw=0.2, grip=(148.0, 130.0), staff=0.14,
                      hand=(128.0, 128.0), palm=1.2, spread=0.2, curl=0.8, cast=0.0, glow=0.3, eye=0.3,
                      crystal=0.4, wave=1.0),
                LPose(t=1.6, broken=broken, dy=4, lean=-0.04, tilt=-0.32, jaw=0.85, grip=(150.0, 120.0), staff=-0.05,
                      hand=(122.0, 96.0), palm=-2.2, spread=1.0, curl=0.0, cast=0.0, glow=1.3, eye=1.4,
                      crystal=1.4, wave=1.6)]
        for i, p in enumerate(conv):
            f = self.draw(p)
            if i == 2:
                hx, hy = self.heart(p)
                FX.burst(f, hx + 3, hy, 8, [WHITE_C, PC[0], PC[1], PC[2]], seed=self.seed + 118, jag=0.45, rays=6)
            f = shake(f, [1, -1, 2][i], 0)
            out.append(flashed(f, 0.4) if i == 0 else f)
        # 3-7: the staff slips from its hand, the bones collapse, the empty robe falls to the ground and
        # the skull rolls off; the soul tears itself out of the chest
        fall = [  # dy, crumple, staff (x, y, angle), tilt, head offset, crown (dx, dy, angle), dead
            (9.0, 0.0, (154.0, 134.0, 0.3), 0.35, (0.0, 1.0), None, 0.15),
            (17.0, 0.22, (160.0, 152.0, 0.8), 0.45, (1.0, 3.0), None, 0.3),
            (22.0, 0.5, (138.0, 197.0, 1.3), 0.25, (6.0, 7.0), (3.0, -4.0, 0.3), 0.42),
            (25.0, 0.8, (110.0, 206.0, PI / 2), -0.2, (19.0, 18.0), ("at", 131.0, 163.0, 0.7), 0.52),
            (26.0, 1.0, (110.0, 206.0, PI / 2), -0.5, (33.0, 29.0), ("at", 124.0, 176.5, -0.28), 0.6),
        ]
        souls = [None, None, (0.0, -6.0, 1.5), (0.0, -24.0, 2.0), (-2.0, -44.0, 2.2)]
        p = None
        for i, (dy, cr, st, tl, ho, crown, dead) in enumerate(fall):
            p = LPose(t=2.0 + i, broken=broken, dy=dy, crumple=cr, free=st, tilt=tl, head=ho, crown=crown,
                      jaw=0.3 + 0.1 * i, hand=(118.0 - 12 * i, 132.0 + 13 * i), palm=1.6 + 0.3 * i,
                      grip=(140.0 + 1 * i, 134.0 + 13 * i), spread=0.2, curl=0.9, cast=0.0, glow=0.5 - 0.06 * i,
                      eye=0.5 - 0.08 * i, crystal=0.5 - 0.06 * i, wave=2.0 + 0.5 * i, dead=dead, ground=cr > 0.1)
            if crown is not None and crown[0] == "at":
                # the crown leaves the skull: put its centre at a given point, at a given angle
                HLc, _ = self.head_fns(p)
                ax, ay = HLc(0.0, -9.0)
                p = p.copy(crown=(crown[1] - ax, crown[2] - ay, crown[3] - tl))
            f = self.draw(p)
            hx, hy = self.heart(p)
            if i <= 1:
                motes(f, hx, hy - 10, 10, 8, 40, [PC[0], PC[1], PC[2]], seed=self.seed + 120 + i, arc=(-2.8, -0.3))
            if souls[i] is not None:
                ox, oy, sz = souls[i]
                self.spectre(f, hx + ox, hy + oy, sz, 0, 3.0 + i, 9, under=False)
                motes(f, hx + ox, hy + oy + 10, 6, 6, 16, [PC[1], PC[2], PC[3]], seed=self.seed + 124 + i)
            out.append(f)
        heap = p
        hx, hy = self.heart(heap)
        # 8-11: the soul rises and fades into motes; the last light of the bones goes out
        for k in range(4):
            p = heap.copy(t=7.0 + k, dead=0.68 + 0.08 * k)
            f = self.draw(p)
            x, y = hx - 3 - 2 * k, hy - 62 - 18 * k
            if k < 3:
                self.spectre(f, x, y, 2.2 - 0.25 * k, k + 1, 7.0 + k, 9, under=False)
            motes(f, x, y + 4, 8 + 5 * k, 4 + 3 * k, 12 + 6 * k, [PC[1], PC[2], PC[3], PC[4]][min(k, 2):],
                  seed=self.seed + 130 + k)
            out.append(f)
        # 12-13: dark: the empty robe, the skull, the fallen crown and staff; two last motes
        p = heap.copy(t=11.0, dead=1.0)
        f = self.draw(p)
        put_if_empty(f, hx - 9, hy - 120, PC[3])
        put_if_empty(f, hx - 4, hy - 100, PC[4])
        out.append(f)
        out.append(self.draw(p))
        return out

    # ------------------------------------------------------------ the sheet
    def clips(self, form2=False):
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


BOSS = Liche()


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
        name = "liche" if k == 0 else "liche_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        save_png(img, tex)
        save_json({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta},
                  "animations/%s.json" % name)
