"""Mycelia, la Mere-Racine (Sylvanox sanctuary guardian, 2.19).

The mother mushroom that links every root of Sylvanox: a huge violet cap dotted with glowing
cyan spots, a pale stem with amber eyes under the gills, standing on four arching root legs
veined with cyan light.  She sinks into the ground and resurfaces elsewhere.

Frames 128x112, pivot [64, 112] (bottom-centre of a 60x64 collision box: x 34..94, y 48..112).
Clips: idle 6, walk 8, move 8, telegraph 6, attack 5 (impact f2), telegraph_special 6 (inhale
build-up), special 4 (inhale, loop), summon 6 (buds), roar 8, hurt 2, death 14,
teleport_out 6 (burrows), teleport_in 6 (emerges)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, bezier, dust_cloud, glow_orb, mat, motes, streak
from boss_c_common import TAU, Pose, build_sheet, dash_arc, hline, over, shake, sound_rings, standard_clips
from raster import TCanvas, put, shift

PI = math.pi
W, H = 128, 112
PIVOT = [64, 112]
GY = 111                     # ground row (feet)
B = (64.0, 88.0)             # body pivot (stem base) for lean / squash
S = (64.0, 60.0)             # stem top (cap hinge)
CAPC = (64.0, 50.0)          # cap centre (rest)
CAP_RX, CAP_RY, RIM_V = 44.0, 30.0, 1.5

CAP = mat("my_cap", ["#c49af8", "#9a68e6", "#7d4fd0", "#6036b0", "#46258a", "#2e185e"],
          th=[0.95, 0.82, 0.64, 0.44, 0.24])
CAP_DEAD = mat("my_cap_dead", ["#9a8aa8", "#7a6a8c", "#5c4e70", "#423852", "#2c2438"])
GILL = mat("my_gill", ["#8e62c8", "#6c44a4", "#4e2c80", "#361c5e", "#22103e"])
GILL_DEAD = mat("my_gill_dead", ["#5c4e70", "#423852", "#2c2438", "#1c1626"])
GILL_GLOW = mat("my_gill_glow", ["#ffe0ff", "#f0a0ff", "#d070f0", "#a048d0"], emissive=True)
SPOT = mat("my_spot", ["#f2fffb", "#b4fff0", "#7dffd6", "#39e0c0", "#1fa8a0"], emissive=True,
           th=[0.9, 0.72, 0.5, 0.3])
SPOT_DIM = mat("my_spot_dim", ["#6ab4a8", "#4a8a84", "#356a68", "#244c4e"])
SPOT_DEAD = mat("my_spot_dead", ["#5a6670", "#424e58", "#2e3842"])
STEM = mat("my_stem", ["#f6eee0", "#e0d0b8", "#c9b9a3", "#a8957e", "#7c6a58", "#54463c"],
           th=[0.88, 0.72, 0.56, 0.38, 0.2])
STEM_DEAD = mat("my_stem_dead", ["#b0a8a0", "#8c847c", "#6a625c", "#4a4440", "#302c2a"])
SKIRT = mat("my_skirt", ["#f0e2f6", "#cdb4dc", "#a488bc", "#765c92", "#4e3a66"])
ROOT = mat("my_root", ["#a87a5c", "#865c40", "#663f2c", "#48291e", "#2c1812"])
ROOT_DK = mat("my_root_dk", ["#7a5238", "#5b3a2a", "#42281e", "#2c1812", "#1c0e0c"])
ROOT_DEAD = mat("my_root_dead", ["#6e625a", "#544a44", "#3c3430", "#26201e"])
VEIN = mat("my_vein", ["#b4fff0", "#5df0d0", "#2fc0b0"], emissive=True, outline=None)
EYE = mat("my_eye", ["#fffbe0", "#ffe98a", "#ffd65a", "#e8a020", "#a86010"], emissive=True,
          th=[0.9, 0.7, 0.5, 0.3])
BROW = mat("my_brow", ["#54463c", "#3a2e28", "#241c18"])
MOUTH = mat("my_mouth", ["#3a1240", "#24082a", "#14041a"])
MAW = mat("my_maw", ["#fff0ff", "#f0a0ff", "#c060f0", "#7a2ab8"], emissive=True)
SOIL = mat("my_soil", ["#6a5040", "#4e3a30", "#36282a", "#22181c"])
BUD = mat("my_bud", ["#c8a0f8", "#9a6ae0", "#6e44b4", "#4a2a80"])

CYANS = [C("#f2fffb"), C("#b4fff0"), C("#7dffd6"), C("#39e0c0")]
VIOLETS = [C("#fff0ff"), C("#f0a0ff"), C("#c060f0"), C("#7a2ab8")]
SPORES = [C("#b4fff0"), C("#7dffd6"), C("#e0b0ff"), C("#c080f0")]
DIRT = [C("#8a6a54"), C("#6a5040"), C("#4e3a30")]
AMBERS = [C("#fffbe0"), C("#ffe98a"), C("#ffd65a")]

# legs: (attach rest point, rest foot, knee height, near?)  near legs are drawn in front
LEGS = [((55.0, 87.0), (24.0, GY), 15.0, True),     # near back
        ((60.0, 84.0), (42.0, GY), 9.0, False),     # far back
        ((69.0, 84.0), (88.0, GY), 9.0, False),     # far front
        ((74.0, 87.0), (106.0, GY), 15.0, True)]    # near front
REST_FEET = [l[1] for l in LEGS]
SPOTS = [(-23, -12, 4.6), (1, -21, 5.6), (24, -11, 4.2), (12, -2, 2.6), (-9, -2, 2.6), (-32, -1, 2.2),
         (34, 0, 2.0), (-12, -23, 2.2), (17, -21, 2.2), (-3, -10, 1.8)]


class MPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, lean=0.0, sx=1.0, sy=1.0, cap_lift=0.0, cap_tilt=0.0, glow=0.6,
                    eye=0.7, mouth=0.0, gill=0.3, feet=None, knee=1.0, sway=0.0, dead=0.0, flick=0)


def rot_about(p, c, a):
    if not a:
        return p
    ca, sa = math.cos(a), math.sin(a)
    x, y = p[0] - c[0], p[1] - c[1]
    return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca)


class Mycelia:
    id = "mycelia"
    W, H, PIVOT = W, H, PIVOT
    seed = 21900
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 6, 8, 12
    WALK_EVENTS = [(0, "footstep"), (4, "footstep")]
    ATTACK_EVENTS = [(2, "impact")]

    # ------------------------------------------------------------ transforms
    def body(self, p, q):
        x = B[0] + (q[0] - B[0]) * p.sx
        y = B[1] + (q[1] - B[1]) * p.sy
        x, y = rot_about((x, y), B, p.lean)
        return (x + p.dx, y + p.dy)

    def cap(self, p, q):
        q = rot_about(q, S, p.cap_tilt)
        return self.body(p, (q[0], q[1] - p.cap_lift))

    def mouth_pos(self, p):
        return self.body(p, (66.5, 75.0))

    def cap_top(self, p):
        return self.cap(p, (CAPC[0], CAPC[1] - CAP_RY + 2))

    # ------------------------------------------------------------------ draw
    def draw(self, p):
        cv = TCanvas(W, H)
        dead = p.dead >= 0.5
        feet = p.feet or REST_FEET
        for k in (1, 2):
            self._leg(cv, p, k, feet[k], dead)
        self._stem(cv, p, dead)
        for k in (0, 3):
            self._leg(cv, p, k, feet[k], dead)
        self._cap(cv, p, dead)
        return cv.render()

    def _leg(self, cv, p, k, foot, dead):
        attach, _, kh, near = LEGS[k]
        A = self.body(p, attach)
        F = foot
        side = 1 if F[0] > A[0] else -1
        knee = (A[0] + (F[0] - A[0]) * 0.55, min(A[1], F[1]) - kh * p.knee)
        pts = bezier(A, (A[0] + side * 6, A[1] - 6 * p.knee), (knee[0], knee[1]), (F[0] - side * 2, F[1] - 3), 9)
        pts.append((F[0], F[1] - 0.5))
        m = (ROOT if near else ROOT_DK) if not dead else ROOT_DEAD
        r0 = 5.0 if near else 4.2
        radii = [r0 - (r0 - 1.8) * (i / (len(pts) - 1)) ** 0.9 for i in range(len(pts))]
        cv.part()
        cv.chain(pts, radii, m)
        # root toes spreading on the ground
        cv.part()
        for dxt, ln in ((-1, 6.0), (1, 5.0)):
            cv.capsule((F[0], F[1] - 1.0), (F[0] + dxt * ln, F[1] - 0.2), 1.4, 0.6, m)
        if not dead:
            cv.part(line=False)
            vi = 0.3 + 0.45 * min(1.0, p.glow)
            for i in range(1, len(pts) - 2):
                if (i + k + p.flick) % 3 != 0:
                    cv.px_set(pts[i][0] + 0.4 * side, pts[i][1] - 1.0, VEIN, inten=vi, decal=True)

    def _stem(self, cv, p, dead):
        sm = STEM if not dead else STEM_DEAD
        cv.part()
        cv.ellipse(self.body(p, (64.5, 85.5)), 11.5 * p.sx, 6.5 * p.sy, sm, ang=p.lean, bias=-0.06)
        cv.capsule(self.body(p, (64.5, 84.0)), self.body(p, S), 10.0 * p.sx, 8.4 * p.sx, sm)
        # fibres
        cv.part(line=False)
        for x0, y0, y1 in ((58.0, 72, 84), (70.5, 74, 86)):
            cv.line(self.body(p, (x0, y0)), self.body(p, (x0 + 0.6, y1)), sm, inten=0.4, decal=True)
        # thin ring (annulus)
        cv.part()
        cv.ellipse(self.body(p, (64.5, 80.5)), 11.4 * p.sx, 2.2, SKIRT if not dead else STEM_DEAD, ang=p.lean,
                   bias=-0.05)
        # face: sockets, eyes, brows, mouth
        e = p.eye
        for (ex, ey, s) in ((61.0, 68.5, -1), (71.0, 68.0, 1)):
            E = self.body(p, (ex, ey))
            cv.part(line=False)
            cv.ellipse((E[0], E[1] + 0.3), 3.3, 2.7, MOUTH, ang=p.lean, flat=0.45)
            if dead:
                cv.line((E[0] - 1.5, E[1]), (E[0] + 1.5, E[1]), BROW, inten=0.5)
                continue
            rx = 2.2 + 0.5 * min(1.0, max(0.0, e - 0.8))
            ry = 1.4 + 0.5 * min(1.2, e)
            cv.ellipse(E, rx, ry, EYE, flat=min(1.0, 0.42 + 0.4 * e))
            cv.ellipse((E[0] + 0.6 * s * 0 + 0.5, E[1]), 0.9, 0.9, EYE, flat=min(1.0, 0.75 + 0.25 * e))
            # angry brow slanting down towards the centre of the face
            b0 = (E[0] - 3.2, E[1] - 3.0 + (1.2 if s > 0 else -0.6))
            b1 = (E[0] + 3.2, E[1] - 3.0 + (-0.6 if s > 0 else 1.2))
            cv.line(b0, b1, BROW, inten=0.2)
        M = self.mouth_pos(p)
        mo = p.mouth
        cv.part(line=False)
        if mo < 0.08:
            cv.line((M[0] - 3, M[1] + 0.5), (M[0] + 3, M[1] - 0.5), BROW, inten=0.2)
        else:
            cv.ellipse(M, 2.6 + 2.0 * mo, 0.9 + 3.0 * mo, MOUTH, ang=p.lean, flat=0.3)
            if not dead:
                cv.ellipse((M[0] + 0.3, M[1] + 0.6), 0.8 + 1.2 * mo, 0.5 + 1.4 * mo, MAW,
                           flat=min(1.0, 0.35 + 0.5 * mo))

    def _cap(self, cv, p, dead):
        tilt = p.lean + p.cap_tilt
        cc = self.cap(p, CAPC)
        rx, ry = CAP_RX * p.sx, CAP_RY * p.sy
        ca, sa = math.cos(tilt), math.sin(tilt)

        def below_rim(px, py, v0=RIM_V * p.sy):
            return (-(px - cc[0]) * sa + (py - cc[1]) * ca) <= v0

        # underside with gills
        gm = GILL if not dead else GILL_DEAD
        cv.part()
        gc = self.cap(p, (CAPC[0], CAPC[1] + 3.5))
        cv.ellipse(gc, rx * 0.94, 9.5 * p.sy, gm, ang=tilt, bias=-0.1 + 0.2 * p.gill)
        cv.part(line=False)
        hub = self.cap(p, (64.0, 60.0))
        gl = GILL_GLOW if (p.gill > 0.55 and not dead) else gm
        for k in range(-7, 8):
            u = k * 5.6
            q = self.cap(p, (CAPC[0] + u * p.sx, CAPC[1] + RIM_V + 0.5))
            q2 = (q[0] + (hub[0] - q[0]) * 0.18, q[1] + (hub[1] - q[1]) * 0.18 + 2.0)
            q3 = (hub[0] + (q[0] - hub[0]) * 0.3, hub[1] + (q[1] - hub[1]) * 0.3)
            cv.line(q2, q3, gl, inten=0.15 + 0.75 * min(1.0, p.gill) if gl is GILL_GLOW else 0.12, decal=True)
        # dome
        cm = CAP if not dead else CAP_DEAD
        cv.part()
        cv.ellipse(cc, rx, ry, cm, ang=tilt, within=below_rim)
        # curled rim (scallops)
        for k in range(-5, 6):
            u = k * 8.0 * p.sx
            q = self.cap(p, (CAPC[0] + k * 8.0, CAPC[1] + RIM_V))
            cv.ellipse(q, 4.6, 2.2, cm, ang=tilt, bias=-0.05 - 0.04 * (k % 2))
        # spots
        sm = SPOT if not dead else SPOT_DEAD
        if not dead and p.glow < 0.25:
            sm = SPOT_DIM
        cv.part(line=False)
        for j, (u, v, r) in enumerate(SPOTS):
            q = self.cap(p, (CAPC[0] + u, CAPC[1] + v))
            g = p.glow + 0.15 * math.sin(j * 1.7 + p.flick * 1.3)
            cv.ellipse(q, r * p.sx, r * 0.85 * p.sy, sm, ang=tilt, flat=min(0.86, 0.35 + 0.35 * g), decal=True)
            if r > 2.4:
                cv.ellipse((q[0] - 0.4, q[1] - 0.4), r * 0.5, r * 0.4, sm, ang=tilt, flat=min(1.0, 0.6 + 0.4 * g),
                           decal=True)
            if r > 3.0 and not dead:
                cv.px_set(q[0] - r * 0.35, q[1] - r * 0.45, sm, inten=1.0, decal=True)
        # glossy highlight on the dome (top-left)
        if not dead:
            for (u, v) in ((-17, -24), (-15, -25), (-13, -26), (-20, -22)):
                cv.px_set(*self.cap(p, (CAPC[0] + u, CAPC[1] + v)), cm, inten=1.0, decal=True)

    # ---------------------------------------------------------------- effects
    def floating_spores(self, f, i, n_frames, seed, count=7, rise=22.0, cols=SPORES):
        rng = np.random.default_rng(seed)
        for k in range(count):
            x0 = 18 + rng.random() * 92
            y0 = 30 + rng.random() * 60
            off = rng.random()
            t = (i / n_frames + off) % 1.0
            x = x0 + 3.0 * math.sin(TAU * (t + off))
            y = y0 - rise * t
            c = cols[k % len(cols)]
            if 0.08 < t < 0.92:
                x, y = int(x), int(y)
                if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0:
                    f[y, x] = c

    def ground_veins(self, f, x0, x1, bright, phase=0):
        """Glowing root veins running in the ground line (telegraphs)."""
        cols = [CYANS[1], CYANS[2], CYANS[3]]
        for x in range(int(min(x0, x1)), int(max(x0, x1)) + 1):
            if 0 <= x < W:
                if (x + phase) % 5 < 3:
                    f[GY, x] = cols[0] if bright > 0.7 and (x + phase) % 5 == 1 else cols[1]
                elif f[GY, x, 3] == 0:
                    f[GY, x] = cols[2]

    def soil_mound(self, f, cx, w, h, seed, glow=0.0):
        cv = TCanvas(W, H)
        cv.part()
        cv.ellipse((cx, GY + 1.5), w, h, SOIL, bias=0.05)
        img = cv.render()
        img[GY + 1:] = 0
        over(f, img)
        rng = np.random.default_rng(seed)
        for _ in range(int(w * 0.6)):
            x = int(cx + (rng.random() - 0.5) * 1.6 * w)
            y = int(GY - rng.random() * h * 0.8)
            if 0 <= x < W and 0 <= y < H and f[y, x, 3] and f[y, x, 0] != 11:
                f[y, x] = DIRT[int(rng.integers(0, 3))]
        if glow > 0:
            for k in range(int(3 + 4 * glow)):
                x = int(cx + (rng.random() - 0.5) * 1.2 * w)
                y = int(GY - 1 - rng.random() * h * 0.5)
                if 0 <= x < W and 0 <= y < H and f[y, x, 3]:
                    f[y, x] = CYANS[1] if k % 2 else CYANS[2]
        return f

    def root_spikes(self, f, x, n, height, seed, lean=0.0):
        """Small roots bursting out of the ground (impact)."""
        cv = TCanvas(W, H)
        rng = np.random.default_rng(seed)
        for k in range(n):
            bx = x + (k - (n - 1) / 2) * 6 + rng.random() * 2
            hh = height * (0.6 + 0.4 * rng.random())
            a = -PI / 2 + lean + (rng.random() - 0.5) * 0.6
            tip = (bx + math.cos(a) * hh, GY + 0.5 + math.sin(a) * hh)
            cv.part()
            cv.capsule((bx, GY + 0.5), tip, 2.4, 0.6, ROOT)
            cv.part(line=False)
            mid = ((bx + tip[0]) / 2, (GY + tip[1]) / 2)
            cv.px_set(mid[0], mid[1], VEIN, inten=0.8, decal=True)
        img = cv.render()
        img[GY + 1:] = 0
        over(f, img)
        return f

    def bud(self, f, x, h, seed):
        """Small mushroom bud sprouting (summon)."""
        if h <= 0.5:
            return f
        cv = TCanvas(W, H)
        cv.part()
        cv.capsule((x, GY), (x + 0.5, GY - h * 0.7), 1.6, 1.3, STEM)
        cv.part()
        cr = 1.6 + 2.6 * min(1.0, h / 9.0)
        cv.ellipse((x + 0.5, GY - h * 0.7 - 0.5), cr, cr * 0.75, BUD, within=lambda px, py: py <= GY - h * 0.7 + 0.5)
        cv.part(line=False)
        if h > 5:
            cv.px_set(x - 0.5, GY - h * 0.7 - cr * 0.4, SPOT, inten=0.9, decal=True)
            cv.px_set(x + 2.0, GY - h * 0.7 - cr * 0.2, SPOT, inten=0.7, decal=True)
        img = cv.render()
        img[GY + 1:] = 0
        over(f, img)
        return f

    # ================================================================== clips
    def idle(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            p = MPose(sy=1.0 + 0.025 * math.sin(ph), sx=1.0 - 0.015 * math.sin(ph), dy=-0.8 * math.sin(ph),
                      glow=0.55 + 0.3 * math.sin(ph + 1.0), gill=0.3 + 0.12 * math.sin(ph), cap_tilt=0.02 * math.sin(ph),
                      eye=0.7, flick=i)
            f = self.draw(p)
            self.floating_spores(f, i, 6, self.seed + 1)
            out.append(f)
        return out

    def _walk_feet(self, t):
        feet = []
        for k, (fx_, fy) in enumerate(REST_FEET):
            pair = 0 if k in (0, 2) else 1
            u = (t + 0.5 * pair) % 1.0
            stride = 9.0 if LEGS[k][3] else 7.0
            if u < 0.5:          # planted, sliding back
                x = fx_ + stride - 4 * stride * u
                y = fy
            else:                # lifted, swinging forward
                v = (u - 0.5) / 0.5
                x = fx_ - stride + 2 * stride * v
                y = fy - 6.0 * math.sin(PI * v)
            feet.append((x, y))
        return feet

    def walk(self):
        out = []
        for i in range(8):
            t = i / 8
            p = MPose(feet=self._walk_feet(t), dy=-1.2 * abs(math.sin(TAU * t)), lean=0.05,
                      cap_tilt=0.03 * math.sin(TAU * t * 2), glow=0.6 + 0.1 * math.sin(TAU * t), eye=0.8, flick=i,
                      sx=1.0 + 0.012 * math.cos(TAU * t * 2), sy=1.0 - 0.012 * math.cos(TAU * t * 2))
            f = self.draw(p)
            # dirt puffs at the planted feet
            for k, (x, y) in enumerate(p.feet):
                if y >= GY - 0.1 and i % 4 == (0 if k in (0, 2) else 2):
                    FX.burst(f, x - 3, GY, 2.2, DIRT, seed=self.seed + 20 + i * 4 + k, jag=0.5)
            self.floating_spores(f, i, 8, self.seed + 2, count=5)
            out.append(f)
        return out

    def telegraph(self):
        """Rears back, raises the near front root and lights the roots in the ground."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            feet = list(REST_FEET)
            feet[3] = (106 - 12 * t, GY - 20 * t)
            p = MPose(lean=-0.14 * t, cap_lift=2.0 * t, cap_tilt=-0.05 * t, glow=0.6 + 0.9 * t, eye=0.8 + 0.9 * t,
                      gill=0.3 + 0.7 * t, knee=1.0 + 0.25 * t, feet=feet, flick=i, dy=-1.0 * t)
            f = self.draw(p)
            self.ground_veins(f, 64, 64 + 62 * t, t, phase=i)
            self.ground_veins(f, 64, 64 - 40 * t, t, phase=i)
            for x in (24, 42, 88):
                if i % 2 == 0:
                    FX.burst(f, x + 2, GY, 2 + i * 0.3, DIRT, seed=self.seed + 40 + i + x, jag=0.5)
            tip = feet[3]
            motes(f, tip[0], tip[1] - 4, 3 + 2 * i, 3, 12 - 4 * t, CYANS[:3], seed=self.seed + 50 + i, inward=0.2 * t)
            if i >= 4:
                FX.sparkle(f, int(tip[0]) + 1, int(tip[1]) - 6, 3 + (i - 4) * 2, CYANS[0], CYANS[2])
            out.append(f)
        return out

    def attack(self):
        """Slams the raised root into the ground: dirt burst and roots erupting in front."""
        keys = [  # lean, near-front foot, knee, cap_tilt
            (-0.17, (93.0, GY - 22.0), 1.3, -0.06),
            (0.06, (110.0, GY - 9.0), 1.1, 0.03),
            (0.18, (112.0, GY), 0.85, 0.09),
            (0.12, (110.0, GY), 0.9, 0.05),
            (0.04, (108.0, GY), 1.0, 0.01),
        ]
        out = []
        for i, (lean, ff, kn, ct) in enumerate(keys):
            feet = list(REST_FEET)
            feet[3] = ff
            p = MPose(lean=lean, feet=feet, knee=kn, cap_tilt=ct, glow=[1.5, 1.4, 1.2, 0.9, 0.7][i],
                      eye=[1.6, 1.5, 1.4, 1.0, 0.8][i], gill=[1.0, 0.9, 0.7, 0.5, 0.35][i], flick=i,
                      dy=[-1, 0, 2, 1, 0][i], sy=[1.0, 1.0, 0.95, 0.98, 1.0][i], sx=[1.0, 1.0, 1.04, 1.02, 1.0][i])
            f = self.draw(p)
            if i == 1:
                for k in range(4):
                    streak(f, (100 + k * 4, 70 + k * 6), (106 + k * 4, 86 + k * 6), CYANS[2])
            if i == 2:
                dust_cloud(f, 112, GY, 10, DIRT, seed=self.seed + 60, n=5)
                self.root_spikes(f, 116, 3, 18, self.seed + 61, lean=0.15)
                FX.burst(f, 112, GY - 2, 6, [CYANS[0], CYANS[1], CYANS[2]], seed=self.seed + 62, jag=0.4, rays=5)
                for (x, y, s) in ((100, 92, 2), (122, 88, 2), (118, 80, 1), (96, 84, 1)):
                    FX.chunk(f, x, y, s, DIRT[0], DIRT[2])
                hline(f, 70, 127, GY, CYANS[1], step=3)
                f = shake(f, 0, 1)
            if i == 3:
                dust_cloud(f, 114, GY, 7, DIRT[1:], seed=self.seed + 63, n=4)
                self.root_spikes(f, 116, 3, 12, self.seed + 61, lean=0.15)
                motes(f, 112, 90, 8, 4, 20, SPORES, seed=self.seed + 64)
            if i == 4:
                self.root_spikes(f, 116, 3, 5, self.seed + 61, lean=0.15)
                motes(f, 110, 96, 5, 2, 14, SPORES[2:], seed=self.seed + 65)
            out.append(f)
        return out

    def _inhale_lines(self, f, p, i, strength, n=7):
        """Air and spores sucked towards the mouth (lines move inwards frame after frame)."""
        mx, my = self.mouth_pos(p)
        rng = np.random.default_rng(self.seed + 70)
        for k in range(n):
            a = -1.0 + 2.0 * rng.random()
            r0 = 26 + 36 * rng.random()
            ph = (rng.random() + i * 0.25) % 1.0
            r = r0 * (1 - ph * 0.8)
            ln = 5 + 7 * strength
            x0, y0 = mx + math.cos(a) * r, my + math.sin(a) * r * 0.8
            x1, y1 = mx + math.cos(a) * (r - ln), my + math.sin(a) * (r - ln) * 0.8
            col = [VIOLETS[1], CYANS[1], C("#d0a8ff")][k % 3]
            dark = [VIOLETS[3], CYANS[3], VIOLETS[2]][k % 3]
            if r > 8:
                streak(f, (x0, y0 + 1), (x1, y1 + 1), dark)
                streak(f, (x0, y0), (x1, y1), col)
            sx, sy = int(x0 + 3), int(y0 - 2)
            if k % 2 == 0 and 0 <= sx < W and 0 <= sy < H and f[sy, sx, 3] == 0:
                f[sy, sx] = SPORES[k % 4]

    def telegraph_special(self):
        """Inhale build-up: the cap tilts back, the mouth opens and the gills light up."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = MPose(mouth=t, cap_tilt=-0.12 * t, lean=-0.05 * t, gill=0.4 + 0.8 * t, glow=0.6 + 0.6 * t,
                      eye=0.8 + 0.6 * t, sx=1.0 + 0.03 * t, sy=1.0 + 0.02 * t, flick=i, cap_lift=1.5 * t)
            f = self.draw(p)
            if i >= 2:
                self._inhale_lines(f, p, i, t, n=3 + i)
            mx, my = self.mouth_pos(p)
            motes(f, mx, my, 2 + 2 * i, 6, 24 - 10 * t, VIOLETS[:3], seed=self.seed + 80 + i, inward=0.3 * t)
            if i == 5:
                FX.sparkle(f, int(mx), int(my), 4, VIOLETS[0], VIOLETS[2])
            out.append(f)
        return out

    def special(self):
        """Inhale (loop): mouth wide open, everything in front is pulled in."""
        out = []
        for i in range(4):
            p = MPose(mouth=1.0 + 0.06 * (i % 2), cap_tilt=-0.12, lean=-0.05, gill=1.2, glow=1.2 + 0.2 * (i % 2),
                      eye=1.4, sx=1.03 + 0.01 * (i % 2), sy=1.02, flick=i, cap_lift=1.5)
            f = self.draw(p)
            self._inhale_lines(f, p, i, 1.0, n=10)
            mx, my = self.mouth_pos(p)
            for k, r in enumerate((14, 24, 34)):
                rr = r - 3 * i
                dash_arc(f, mx, my, rr, rr * 0.8, -0.9, 0.9, [VIOLETS[1], VIOLETS[2], VIOLETS[3]][k], dash=3, gap=2,
                         phase=i)
            out.append(f)
        return out

    def summon(self):
        """Shakes her cap: a puff of spores, and buds sprout from the ground on both sides."""
        keys = [(0.92, 1.05, 0.9), (1.08, 0.96, 1.3), (1.0, 1.0, 1.4), (1.02, 0.99, 1.2), (1.0, 1.0, 0.9),
                (1.0, 1.0, 0.7)]
        out = []
        for i, (sy, sx, g) in enumerate(keys):
            p = MPose(sy=sy, sx=sx, glow=g, eye=1.0, gill=0.6, flick=i, cap_tilt=[0, -0.04, 0.03, 0, 0, 0][i])
            f = self.draw(p)
            top = self.cap_top(p)
            if 1 <= i <= 3:
                for k in range(5 + 3 * i):
                    pass
                motes(f, top[0], top[1] + 6, 10 + 5 * i, 6, 18 + 8 * i, SPORES, seed=self.seed + 90 + i, big=0.25,
                      arc=(-PI, 0))
            hs = [0, 0, 3, 6, 9, 10][i]
            for (x, s) in ((12, 0), (116, 1), (30, 2), (98, 3)):
                self.bud(f, x, hs * (1.0 if s < 2 else 0.75), self.seed + 95 + s)
                if i == 2:
                    FX.burst(f, x, GY, 3, DIRT, seed=self.seed + 97 + s, jag=0.5)
            out.append(f)
        return out

    def roar(self):
        keys = [  # mouth, sx, sy, lean, glow, shake
            (0.2, 1.0, 1.0, 0.04, 0.7, 0), (0.5, 1.02, 1.01, -0.04, 0.9, 0), (1.1, 1.06, 1.04, -0.12, 1.6, 1),
            (1.2, 1.07, 1.05, -0.13, 1.7, -1), (1.2, 1.07, 1.05, -0.13, 1.7, 1), (1.1, 1.06, 1.04, -0.12, 1.6, -1),
            (0.5, 1.02, 1.01, -0.04, 1.0, 0), (0.1, 1.0, 1.0, 0.0, 0.7, 0)]
        out = []
        for i, (mo, sx, sy, lean, g, sh) in enumerate(keys):
            p = MPose(mouth=mo, sx=sx, sy=sy, lean=lean, glow=g, eye=0.8 + 0.6 * mo, gill=0.3 + 0.7 * mo,
                      cap_lift=2.5 * min(1.0, mo), cap_tilt=-0.06 * min(1.0, mo), flick=i)
            f = self.draw(p)
            if mo > 1.0:
                mx, my = self.mouth_pos(p)
                sound_rings(f, mx + 2, my, (8 + (i % 2) * 2, 14 + (i % 2) * 2, 20 + (i % 2) * 2),
                            [VIOLETS[1], VIOLETS[2], VIOLETS[3]], -1.0, 1.0, dotted={2: 2})
                motes(f, 64, 46, 12, 30, 50, SPORES, seed=self.seed + 100 + i)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def hurt(self):
        a = MPose(lean=-0.1, dx=-2, eye=1.3, glow=1.2, gill=0.8, cap_tilt=-0.06, sy=0.97, sx=1.03)
        b = MPose(lean=-0.05, dx=-1, eye=0.9, glow=0.7, gill=0.4)
        fa = FX.flash(self.draw(a), amount=0.55)
        fb = self.draw(b)
        motes(fb, 60, 40, 6, 20, 40, SPORES, seed=self.seed + 110)
        return [fa, fb]

    def residue(self, t):
        cv = TCanvas(W, H)
        cv.part()
        cv.ellipse((64, GY + 2), 20 * (0.6 + 0.4 * t), 6 * t + 1, SOIL)
        cv.part()
        for (x, y, a) in ((50, GY - 1, -2.6), (78, GY - 1, -0.5)):
            cv.capsule((x, y), (x + math.cos(a) * 10 * t, y + math.sin(a) * 3), 1.6, 0.6, ROOT_DEAD)
        img = cv.render()
        img[GY + 1:] = 0
        if t >= 0.95:
            self.bud(img, 66, 5, self.seed + 120)
        return img

    def death(self):
        out = []
        seq = [
            MPose(eye=1.7, glow=1.7, gill=1.2, mouth=0.8, lean=-0.1, cap_tilt=-0.08),
            MPose(eye=0.6, glow=0.3, gill=0.4, mouth=0.4, lean=-0.04, dx=1, flick=1),
            MPose(eye=1.4, glow=1.4, gill=0.9, mouth=0.7, lean=-0.08, flick=2),
        ]
        for i, p in enumerate(seq):
            f = self.draw(p)
            if i == 0:
                f = FX.flash(f, amount=0.5)
            motes(f, 64, 48, 6 + 4 * i, 26, 48, SPORES, seed=self.seed + 130 + i)
            out.append(shake(f, [1, -1, 1][i], 0))
        fall = [  # lean, dy, knee, cap_tilt, glow, dead, spread
            (0.08, 3, 0.8, 0.08, 0.6, 0.0, 2),
            (0.16, 8, 0.6, 0.16, 0.3, 0.3, 5),
            (0.24, 14, 0.4, 0.26, 0.15, 0.55, 8),
            (0.3, 19, 0.25, 0.34, 0.05, 0.8, 10),
            (0.33, 21, 0.2, 0.38, 0.0, 1.0, 11),
        ]
        for i, (lean, dy, kn, ct, g, dead, sp) in enumerate(fall):
            feet = [(x + (sp if x > 64 else -sp), y) for (x, y) in REST_FEET]
            p = MPose(lean=lean, dy=dy, knee=kn, cap_tilt=ct, glow=g, eye=0.3 * (1 - dead), dead=dead, feet=feet,
                      sy=1.0 - 0.06 * dead, flick=i + 3)
            f = self.draw(p)
            f[GY + 1:] = 0
            if i <= 2:
                FX.burst(f, 64 + 30, GY, 3 + i, DIRT, seed=self.seed + 140 + i, jag=0.5)
                FX.burst(f, 64 - 30, GY, 3 + i, DIRT, seed=self.seed + 145 + i, jag=0.5)
            motes(f, 64, 60, 8, 20, 46, SPORES, seed=self.seed + 150 + i)
            out.append(f)
        base = out[-1].copy()
        for k, t in enumerate((0.18, 0.36, 0.54, 0.72, 0.9)):
            f = FX.dissolve(base, t, seed=self.seed + 160, colors=[C("#e0b0ff"), C("#b4fff0"), C("#7dffd6"),
                                                                    C("#8a5ac8")],
                            rise=26, drift=7, top_first=True, life=0.42, particle_rate=0.18, bounds=(0, 0, W, GY))
            FX.under(f, self.residue(min(1.0, 0.35 + t)))
            out.append(f)
        last = self.residue(1.0)
        motes(last, 64, 92, 7, 4, 22, SPORES[:2], seed=self.seed + 170, arc=(-2.8, -0.3))
        out.append(last)
        return out

    def teleport_out(self):
        """Burrows: crouches, then sinks into the ground leaving a mound of soil."""
        out = []
        sinks = [0, 8, 20, 36, 56, 80]
        for i, s in enumerate(sinks):
            p = MPose(sy=0.95 if i == 0 else 0.97, sx=1.03, dy=2 if i == 0 else 0, knee=0.7, lean=0.0,
                      glow=0.8 + 0.1 * i, eye=1.0, flick=i, cap_tilt=0.02 * (i % 2))
            f = self.draw(p)
            if s:
                f = shift(f, 0, s)
                f[GY - 1:] = 0
            mound_w = [0, 30, 34, 32, 26, 20][i]
            if mound_w:
                self.soil_mound(f, 64, mound_w, 5 + (i > 1) * 2, self.seed + 180 + i, glow=0.5)
                FX.burst(f, 64 - mound_w * 0.8, GY - 2, 3, DIRT, seed=self.seed + 186 + i, jag=0.5)
                FX.burst(f, 64 + mound_w * 0.8, GY - 2, 3, DIRT, seed=self.seed + 190 + i, jag=0.5)
                for (x, y, sz) in ((64 - mound_w, GY - 8 - i, 1), (64 + mound_w + 2, GY - 6 - i, 2)):
                    FX.chunk(f, x, y, sz, DIRT[0], DIRT[2])
            if i >= 4:
                motes(f, 64, GY - 6, 8, 2, 16, SPORES[:2], seed=self.seed + 195 + i, arc=(-PI, 0))
            out.append(f)
        return out

    def teleport_in(self):
        """Emerges: the soil swells and glows, then she bursts out of it."""
        out = []
        sinks = [90, 52, 28, 12, 3, 0]
        for i, s in enumerate(sinks):
            p = MPose(sy=1.04 if i in (3, 4) else 1.0, sx=0.98 if i in (3, 4) else 1.0, knee=0.8 + 0.04 * i,
                      glow=1.3 - 0.1 * i, eye=1.2, flick=i, gill=0.6)
            f = self.draw(p)
            if s:
                f = shift(f, 0, s)
                f[GY - 1:] = 0
            mound_w = [24, 30, 32, 28, 0, 0][i]
            if mound_w:
                self.soil_mound(f, 64, mound_w, [8, 7, 6, 5][i], self.seed + 200 + i, glow=1.0)
            if i == 0:
                self.ground_veins(f, 40, 88, 1.0)
                motes(f, 64, GY - 4, 8, 2, 12, CYANS[:3], seed=self.seed + 206, arc=(-PI, 0))
            if 1 <= i <= 3:
                dust_cloud(f, 64 - 34, GY, 8 - i, DIRT, seed=self.seed + 210 + i, n=4)
                dust_cloud(f, 64 + 34, GY, 8 - i, DIRT, seed=self.seed + 215 + i, n=4)
                for (x, y, sz) in ((26, 84 + 4 * i, 2), (104, 80 + 4 * i, 2), (16, 94, 1), (114, 92, 1)):
                    FX.chunk(f, x, y, sz, DIRT[0], DIRT[2])
            if i == 4:
                dust_cloud(f, 64, GY, 6, DIRT[1:], seed=self.seed + 220, n=4)
            out.append(f)
        return out

    def sheet(self):
        extras = [
            ("teleport_out", self.teleport_out(), dict(durations=[0.08, 0.08, 0.08, 0.1, 0.1, 0.16], loop=False)),
            ("teleport_in", self.teleport_in(), dict(durations=[0.12, 0.08, 0.08, 0.08, 0.1, 0.14], loop=False)),
        ]
        return build_sheet(self, standard_clips(self, extras))


BOSS = Mycelia()


def build():
    return BOSS.sheet()
