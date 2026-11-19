"""Nyxara, la Devoreuse d'etoiles (Noyau sanctuary guardian, 2.19).

Born of the void between the stars: a sphere of darkness with a violet event-horizon rim, three
glowing pink eyes, a wide accretion disc (white-pink inner edge to violet) whose back half passes
behind the sphere, and void tentacles with pink tips hanging below.  It floats, swallows the light
and steps through the shadows (teleport_out / teleport_in).

Frames 128x128, pivot [64, 92] (bottom-centre of a 52x52 collision box: x 38..90, y 40..92, centred
on the sphere (64, 66)).
Clips: idle 6, walk 6 (= move, drifting), move 6, telegraph 6 (eyes flare, stars gather),
attack 5 (fire f1, seeking stars), telegraph_special 6 (the disc falls inwards: well / eclipse),
special 4 (pulse, loop), summon 6 (void rifts), roar 8, hurt 2, death 14, teleport_out 5,
teleport_in 5."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, glow_orb, mat, motes, ring, streak
from boss_c_common import TAU, Pose, build_sheet, dash_arc, over, shake, standard_clips
from raster import TCanvas, put

PI = math.pi
W = H = 128
PIVOT = [64, 92]
CX, CY = 64.0, 64.0
SPH_R = 21.0
DISC_RX, DISC_RY, DISC_TILT = 50.0, 8.5, -0.08
ROT_STEP = (TAU / 8) / 6               # 8 disc streaks: one streak step per 6-frame loop

VOID = mat("nx_void", ["#3a1a5a", "#24103a", "#160828", "#0c0418", "#06020c"], th=[0.9, 0.74, 0.5, 0.26])
RIM = mat("nx_rim", ["#f0c8ff", "#c08cff", "#8a4ee0", "#5b2a8c"], emissive=True, outline=None)
DISC = mat("nx_disc", ["#fff0f6", "#ffb4d0", "#ff7696", "#d060c8", "#9a5ae0", "#5b2a8c"], emissive=True,
           outline=None, th=[0.9, 0.76, 0.6, 0.44, 0.26])
DISC_BK = mat("nx_disc_bk", ["#ff9ab8", "#d860a8", "#a050d0", "#6a34a0", "#40206a"], emissive=True, outline=None)
DISC_DEAD = mat("nx_disc_dead", ["#7a6a88", "#56486a", "#3a3050"], outline=None)
EYE = mat("nx_eye", ["#ffffff", "#ffd0e0", "#ff7696", "#d0305a", "#8a1040"], emissive=True, th=[0.9, 0.7, 0.5, 0.3])
TENT = mat("nx_tent", ["#5a2c80", "#3e1c5e", "#2a1240", "#1a0a2a", "#0e0418"], th=[0.86, 0.66, 0.46, 0.26])
TENT_DK = mat("nx_tent_dk", ["#3e1c5e", "#2a1240", "#1a0a2a", "#0e0418"])
SUCKER = mat("nx_sucker", ["#ffd0e0", "#ff7696", "#c03a78"], emissive=True, outline=None)
DEAD = mat("nx_dead", ["#4a4256", "#34303e", "#24202c", "#16141c"])

PINKS = [C("#ffffff"), C("#ffd0e0"), C("#ff7696"), C("#d0305a")]
VIOLETS = [C("#f0c8ff"), C("#c08cff"), C("#8a4ee0"), C("#5b2a8c")]
STARS = [C("#ffffff"), C("#ffe8f4"), C("#ffd0e0"), C("#c08cff")]


class NPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, rot=0.0, tilt=DISC_TILT, dr=1.0, eye=0.7, lid=1.0, look=(4.0, 0.0),
                    sway=0.0, reach=0.0, lift=0.0, trail=0.0, pulse=1.0, scale=1.0, flick=0, dead=0.0,
                    swallow=0.0)


class Nyxara:
    id = "nyxara"
    W, H, PIVOT = W, H, PIVOT
    seed = 21930
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 8, 10, 12
    ATTACK_EVENTS = [(1, "fire")]

    def centre(self, p):
        return (CX + p.dx, CY + p.dy)

    def disc_pt(self, p, th, r_off=0.0):
        cx, cy = self.centre(p)
        rx = (DISC_RX * p.dr + r_off) * p.scale
        ry = (DISC_RY * p.dr + r_off * 0.16) * p.scale
        x, y = math.cos(th) * rx, math.sin(th) * ry
        c, s = math.cos(p.tilt), math.sin(p.tilt)
        return (cx + x * c - y * s, cy + 2.0 * p.scale + x * s + y * c)

    def eyes(self, p):
        cx, cy = self.centre(p)
        k = p.scale
        lx, ly = p.look
        return [((cx - 6 * k + lx * k, cy - 5 * k + ly * k), 4.2 * k, 2.4 * k, -0.25),
                ((cx + 8 * k + lx * k, cy - 5 * k + ly * k), 4.2 * k, 2.4 * k, 0.25),
                ((cx + 1 * k + lx * k, cy + 6 * k + ly * k), 3.2 * k, 1.9 * k, 0.0)]

    # ------------------------------------------------------------------ draw
    def draw(self, p):
        cv = TCanvas(W, H)
        dead = p.dead >= 0.5
        if p.scale <= 0.04:
            return cv.render()
        # 2.76.2: the hit part of each pixel (raster.LABELS, tools/assetgen/hitparts.py)
        cv.tag = "tentacules"
        self._tentacles(cv, p, back=True, dead=dead)
        cv.tag = "anneau"
        self._disc(cv, p, back=True, dead=dead)
        cv.tag = "sphère"
        self._sphere(cv, p, dead)
        cv.tag = "tentacules"
        self._tentacles(cv, p, back=False, dead=dead)
        cv.tag = "anneau"
        self._disc(cv, p, back=False, dead=dead)
        return cv.render()

    def _disc(self, cv, p, back, dead):
        if p.dr <= 0.05:
            return
        a0, a1 = (PI, 2 * PI) if back else (0.0, PI)
        m = (DISC_BK if back else DISC) if not dead else DISC_DEAD
        n = 48
        k = p.scale
        cv.part(line=False)
        # bands from the outer (dim) to the inner edge (hot)
        for j, (off, rad, inten) in enumerate(((6.0, 2.4, 0.28), (2.5, 2.2, 0.48), (-1.0, 1.8, 0.66), (-3.8, 1.3, 0.86))):
            pts = [self.disc_pt(p, a0 + (a1 - a0) * i / n, r_off=off) for i in range(n + 1)]
            cv.chain(pts, [rad * k] * len(pts), m, flat=inten if not back else inten * 0.85)
        # rotating hot streaks
        if not dead:
            for s in range(8):
                t = (p.rot + TAU * s / 8) % TAU
                if not (a0 <= t <= a1):
                    continue
                q0 = self.disc_pt(p, t, r_off=0.5)
                q1 = self.disc_pt(p, t + 0.22, r_off=0.5)
                cv.line(q0, q1, m, inten=0.95 if not back else 0.8)
                q2 = self.disc_pt(p, t + 0.5, r_off=4.0)
                cv.px_set(q2[0], q2[1], m, inten=0.7)

    def _sphere(self, cv, p, dead):
        cx, cy = self.centre(p)
        r = SPH_R * p.pulse * p.scale
        cv.part()
        cv.disc((cx, cy), r, VOID if not dead else DEAD)
        if not dead:
            # event-horizon rim (brightest top-left), lensed photon ring
            cv.part(line=False)
            for (x, y) in FX.ring_pixels(cx, cy, r - 0.8):
                a = math.atan2(y + 0.5 - cy, x + 0.5 - cx)
                lit = 0.5 + 0.5 * math.cos(a + 2.3)
                cv.px_set(x, y, RIM, inten=0.35 + 0.6 * lit, decal=True)
            for (x, y) in FX.ring_pixels(cx, cy, r - 2.2):
                a = math.atan2(y + 0.5 - cy, x + 0.5 - cx)
                if math.cos(a + 2.3) > 0.2:
                    cv.px_set(x, y, RIM, inten=0.4, decal=True)
            # swallowed starlight swirling inside
            rng = np.random.default_rng(self.seed + 7)
            for kk in range(7):
                a = rng.random() * TAU + p.rot * (1.5 + 0.3 * kk)
                d = r * (0.3 + 0.5 * rng.random())
                cv.px_set(cx + math.cos(a) * d, cy + math.sin(a) * d, RIM, inten=0.3 + 0.3 * (kk % 2), decal=True)
            # eyes
            cv.tag = "yeux"
            for (q, rx, ry, ang) in self.eyes(p):
                cv.part(line=False)
                ry2 = ry * max(0.15, min(1.25, p.lid))
                cv.ellipse(q, rx + 1.0, ry2 + 1.0, EYE, ang=ang, flat=0.3)
                cv.ellipse(q, rx, ry2, EYE, ang=ang, flat=min(1.0, 0.5 + 0.3 * p.eye))
                if p.lid > 0.4:
                    cv.ellipse((q[0] + 0.6, q[1]), rx * 0.4, ry2 * 0.45, EYE, ang=ang, flat=min(1.0, 0.8 + 0.2 * p.eye))
        else:
            cv.tag = "yeux"
            for (q, rx, ry, ang) in self.eyes(p):
                cv.part(line=False)
                cv.line((q[0] - rx * 0.7, q[1]), (q[0] + rx * 0.7, q[1]), DEAD, inten=0.9)

    def _tentacles(self, cv, p, back, dead):
        cx, cy = self.centre(p)
        k = p.scale
        r = SPH_R * p.pulse * k
        specs = [(-0.75, 28, True, 0.0), (0.55, 26, True, 1.7), (-0.38, 33, False, 0.8), (0.02, 35, False, 2.6),
                 (0.42, 30, False, 4.1)]
        for j, (base_a, ln, is_back, ph) in enumerate(specs):
            if is_back != back:
                continue
            a = PI / 2 + base_a * (1.0 + 0.7 * p.reach)
            q = (cx + math.cos(a) * (r - 3), cy + math.sin(a) * (r - 3))
            pts = [q]
            ang = a + 0.3 * base_a * p.reach
            L = ln * k * (1.0 + 0.25 * p.reach)
            n = 9
            for i in range(1, n + 1):
                t = i / n
                ang += (0.1 * math.sin(p.sway + ph + t * 3.2) - 0.12 * p.trail - 0.12 * p.lift * math.copysign(1, base_a)
                        * 2 * t)
                ang -= 0.08 * p.lift * (1 if base_a >= 0 else -1)
                x = pts[-1][0] + math.cos(ang) * L / n
                y = pts[-1][1] + math.sin(ang) * L / n
                pts.append((x, y))
            m = (TENT_DK if back else TENT) if not dead else DEAD
            rad = [(4.2 - 3.4 * (i / n) ** 0.8) * k for i in range(n + 1)]
            cv.part()
            cv.chain(pts, rad, m)
            if not dead:
                cv.part(line=False)
                for i in (3, 5, 7):
                    cv.px_set(pts[i][0] + 0.8, pts[i][1], SUCKER, inten=0.5 + 0.4 * min(1.0, p.eye), decal=True)
                cv.px_set(pts[-1][0], pts[-1][1], SUCKER, inten=0.9)

    # ---------------------------------------------------------------- effects
    def star(self, f, x, y, size, cols=STARS):
        FX.sparkle(f, int(x), int(y), size, cols[0], cols[2])

    def drifting_stars(self, f, i, n, seed, count=6, inward=True):
        """Starlight spiralling into the sphere (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        for k in range(count):
            a0 = rng.random() * TAU
            r0 = 34 + rng.random() * 22
            off = rng.random()
            t = (i / n + off) % 1.0
            a = a0 + 1.6 * t
            r = r0 * (1 - 0.6 * t) if inward else r0 * (0.4 + 0.6 * t)
            x, y = int(CX + math.cos(a) * r), int(CY + math.sin(a) * r * 0.7)
            if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0 and t < 0.9:
                f[y, x] = STARS[k % 4]

    # ================================================================== clips
    def idle(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            p = NPose(dy=1.6 * math.sin(ph), rot=ROT_STEP * i, sway=ph, eye=0.7 + 0.2 * math.sin(ph),
                      pulse=1.0 + 0.02 * math.sin(ph), flick=i, lid=0.25 if i == 3 else 1.0)
            f = self.draw(p)
            self.drifting_stars(f, i, 6, self.seed + 1)
            out.append(f)
        return out

    def walk(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            p = NPose(dx=1.0, dy=1.2 * math.sin(ph), rot=ROT_STEP * i, sway=ph * 1.5, trail=1.0, eye=0.85,
                      tilt=DISC_TILT + 0.08, flick=i, look=(5.0, 0.5))
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for k in range(5):
                y = int(rng.integers(40, 96))
                x0 = int(rng.integers(2, 18))
                streak(f, (x0, y), (x0 + int(rng.integers(5, 12)), y), VIOLETS[2 + k % 2])
            out.append(f)
        return out

    def telegraph(self):
        """Eyes flare, tentacles rise, stars gather around (seeking stars, shadow step)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = NPose(rot=ROT_STEP * (1 + 2 * t) * i, eye=0.8 + 0.9 * t, lid=1.0 + 0.2 * t, lift=t, reach=0.6 * t,
                      sway=0.5 * i, flick=i, dy=-1.5 * t, dr=1.0 + 0.06 * t)
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 20 + i)
            for k in range(2 + i):
                a = rng.random() * TAU
                d = 30 + rng.random() * 20 - 8 * t
                x, y = CX + math.cos(a) * d, CY - 4 + math.sin(a) * d * 0.75
                self.star(f, x, y, 1 + (k % 2) + (1 if i >= 4 else 0))
            for (q, rx, ry, ang) in self.eyes(p)[:2]:
                motes(f, q[0], q[1], 2 + i, 5, 12, PINKS[:3], seed=self.seed + 30 + i + int(q[0]))
            if i == 5:
                q = self.eyes(p)[1][0]
                FX.sparkle(f, int(q[0]), int(q[1]), 6, PINKS[0], PINKS[2])
            out.append(f)
        return out

    def attack(self):
        """Tentacles lash forward and a spray of stars bursts out (projectiles spawn on f1)."""
        keys = [(1.0, 0.8, -0.2, 1.6), (0.4, 1.2, -0.8, 1.7), (0.2, 1.0, -0.6, 1.3), (0.4, 0.6, -0.3, 1.0),
                (0.2, 0.2, 0.0, 0.8)]
        out = []
        for i, (lift, reach, trail, eye) in enumerate(keys):
            p = NPose(rot=ROT_STEP * (6 + i), lift=lift, reach=reach, trail=trail, eye=eye, lid=1.15 if i < 2 else 1.0,
                      sway=1.2 * i, flick=i, dx=[0, 2, 1.5, 0.5, 0][i])
            f = self.draw(p)
            if i == 1:
                ex, ey = self.eyes(p)[1][0]
                FX.burst(f, ex + 18, ey + 2, 6, [PINKS[0], PINKS[1], PINKS[2], VIOLETS[2]], seed=self.seed + 40, jag=0.4,
                         rays=6)
                for k in range(5):
                    a = -0.7 + 0.35 * k
                    self.star(f, ex + 18 + math.cos(a) * 16, ey + 2 + math.sin(a) * 16, 2)
            if i == 2:
                ex, ey = self.eyes(p)[1][0]
                for k in range(5):
                    a = -0.7 + 0.35 * k
                    self.star(f, ex + 18 + math.cos(a) * 30, ey + 2 + math.sin(a) * 30, 2 if k % 2 else 1)
            if i == 3:
                motes(f, CX + 30, CY, 8, 10, 30, STARS, seed=self.seed + 45)
            out.append(f)
        return out

    def telegraph_special(self):
        """The disc falls inwards and the darkness thickens (gravity well, eclipse wave)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = NPose(rot=ROT_STEP * (1 + 4 * t) * i, dr=1.0 - 0.35 * t, eye=0.8 + 0.8 * t, lid=1.0 - 0.3 * t,
                      pulse=1.0 + 0.08 * t, reach=0.8 * t, lift=0.3 * t, sway=0.6 * i, flick=i, tilt=DISC_TILT * (1 - t))
            f = self.draw(p)
            cx, cy = self.centre(p)
            for k, r in enumerate((52 - 6 * i, 40 - 5 * i)):
                if r > SPH_R + 4:
                    dash_arc(f, cx, cy, r, r * 0.8, 0, TAU, VIOLETS[1 + k], dash=3, gap=3, phase=i)
            motes(f, cx, cy, 8 + 4 * i, 26, 56 - 16 * t, STARS + VIOLETS[:2], seed=self.seed + 50 + i, inward=0.2 * t)
            if i == 5:
                FX.sparkle(f, int(cx), int(cy - SPH_R - 6), 6, VIOLETS[0], VIOLETS[2])
            out.append(f)
        return out

    def special(self):
        """Pulse (loop): the sphere throbs and dark rings leave it (well / eclipse wave)."""
        out = []
        for i in range(4):
            p = NPose(rot=ROT_STEP * 4 * i, dr=0.7 + 0.1 * (i % 2), eye=1.6, lid=0.8, pulse=1.1 - 0.04 * (i % 2),
                      reach=1.0, lift=0.4, sway=1.6 * i, flick=i, tilt=0.0)
            f = self.draw(p)
            cx, cy = self.centre(p)
            for k in range(2):
                r = SPH_R + 6 + ((i * 6 + k * 12) % 24)
                ring(f, cx, cy, r, [VIOLETS[2], PINKS[2]][k], dotted=0 if r < SPH_R + 20 else 2)
            self.drifting_stars(f, i, 4, self.seed + 60, count=10)
            out.append(f)
        return out

    def _rift(self, f, x, y, s, seed):
        """Small void rift (summon): a dark tear with a pink rim."""
        if s <= 0.05:
            return
        cv = TCanvas(W, H)
        cv.part()
        cv.ellipse((x, y), 3.0 * s + 0.5, 7.0 * s + 0.5, VOID, flat=0.3)
        cv.part(line=False)
        for (px, py) in FX.ring_pixels(x, y, 2.0):
            pass
        cv.ellipse((x, y), 1.2 * s, 5.0 * s, RIM, flat=0.5)
        img = cv.render()
        over(f, img)
        motes(f, x, y, int(4 + 4 * s), 4, 10 * s + 4, PINKS[1:] + VIOLETS[1:3], seed=seed)

    def summon(self):
        """Tentacles open wide and tear void rifts on both sides (Appel du vide)."""
        keys = [0.3, 0.8, 1.0, 1.0, 0.7, 0.2]
        out = []
        for i, h in enumerate(keys):
            p = NPose(rot=ROT_STEP * i, reach=1.2 * h, lift=0.6 * h, eye=0.8 + 0.7 * h, sway=0.8 * i, flick=i,
                      dr=1.0 + 0.06 * h, dy=-2 * h)
            f = self.draw(p)
            s = [0, 0.4, 0.8, 1.0, 1.0, 0.6][i]
            self._rift(f, 14, 92, s * 1.3, self.seed + 70 + i)
            self._rift(f, 114, 92, s * 1.3, self.seed + 75 + i)
            if i in (1, 2):
                for (x0, x1) in ((44, 20), (84, 108)):
                    streak(f, (x0, 84), (x1, 90), VIOLETS[2])
            out.append(f)
        return out

    def roar(self):
        keys = [  # eye, dr, reach, shake
            (0.8, 1.0, 0.0, 0), (1.0, 1.04, 0.3, 0), (1.7, 1.12, 1.3, 1), (1.7, 1.14, 1.4, -1), (1.7, 1.14, 1.4, 1),
            (1.6, 1.12, 1.3, -1), (1.0, 1.04, 0.5, 0), (0.8, 1.0, 0.0, 0)]
        out = []
        for i, (eye, dr, reach, sh) in enumerate(keys):
            p = NPose(rot=ROT_STEP * 3 * i, eye=eye, dr=dr, reach=reach, lift=0.5 * reach, lid=1.2 if eye > 1.2 else 1.0,
                      pulse=1.0 + 0.05 * (eye > 1.2), sway=0.9 * i, flick=i)
            f = self.draw(p)
            if eye > 1.2:
                cx, cy = self.centre(p)
                for k, r in enumerate((SPH_R + 12 + 2 * (i % 2), SPH_R + 22 + 2 * (i % 2))):
                    ring(f, cx, cy, r, [PINKS[2], VIOLETS[2]][k], dotted=0 if k == 0 else 2)
                motes(f, cx, cy, 16, 30, 62, STARS + VIOLETS[:2], seed=self.seed + 80 + i, big=0.2)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def hurt(self):
        a = NPose(dx=-2, rot=ROT_STEP, eye=1.3, lid=0.4, reach=0.4, trail=0.6, pulse=0.96)
        b = NPose(dx=-1, rot=ROT_STEP * 2, eye=0.9, lid=0.8, reach=0.2)
        fa = FX.flash(self.draw(a), color=(255, 220, 240, 255), amount=0.55)
        fb = self.draw(b)
        motes(fb, CX, CY, 8, 24, 40, STARS, seed=self.seed + 90)
        return [fa, fb]

    def residue(self, t):
        cv = TCanvas(W, H)
        cv.part()
        cv.ellipse((64, 90), 14 * t + 2, 2.5 * t + 0.8, DEAD)
        cv.part()
        for (x, y, r) in ((56, 88, 2.4), (70, 89, 2.0), (63, 86, 1.8)):
            cv.disc((x, y), r * t + 0.5, TENT_DK)
        return cv.render()

    def death(self):
        out = []
        seq = [NPose(eye=1.7, lid=1.3, dr=1.15, reach=1.2, lift=0.6, rot=ROT_STEP),
               NPose(eye=0.3, lid=0.4, dr=1.1, reach=0.6, rot=ROT_STEP * 2.5, dx=1, flick=1),
               NPose(eye=1.5, lid=1.2, dr=1.2, reach=1.0, rot=ROT_STEP * 3, flick=2)]
        for i, p in enumerate(seq):
            f = self.draw(p)
            if i == 0:
                f = FX.flash(f, color=(255, 220, 240, 255), amount=0.5)
            for (q, rx, ry, ang) in self.eyes(p)[:2]:
                FX.sparkle(f, int(q[0]), int(q[1]), 2 + 2 * i, PINKS[0], PINKS[2])
            out.append(shake(f, [1, -1, 1][i], 0))
        # the disc falls in and the sphere implodes
        fall = [(0.8, 0.85, 0.2, 0.3), (0.5, 0.65, 0.4, 0.5), (0.25, 0.42, 0.6, 0.6), (0.0, 0.22, 0.85, 0.8)]
        for i, (dr, sc, dead, eye) in enumerate(fall):
            p = NPose(dr=dr, scale=sc, dead=dead, eye=eye * 0.5, lid=0.5, rot=ROT_STEP * (4 + 2 * i), reach=0.4,
                      trail=-0.5, sway=i, flick=3 + i)
            f = self.draw(p)
            cx, cy = self.centre(p)
            rng = np.random.default_rng(self.seed + 100 + i)
            for k in range(10):
                a = rng.random() * TAU
                d0 = 50 - 8 * i
                streak(f, (cx + math.cos(a) * d0, cy + math.sin(a) * d0 * 0.7),
                       (cx + math.cos(a) * (d0 - 10), cy + math.sin(a) * (d0 - 10) * 0.7), STARS[k % 4])
            if i == 3:
                FX.burst(f, cx, cy, 6, [PINKS[0], PINKS[1], VIOLETS[1], VIOLETS[2]], seed=self.seed + 110, jag=0.3,
                         rays=8)
            out.append(f)
        # final flash then the last stardust drifts away
        for k, t in enumerate((0.0, 0.2, 0.4, 0.6, 0.8, 0.95)):
            f = np.zeros((H, W, 4), np.uint8)
            r = 4 + 40 * t
            ring(f, CX, CY, r, [PINKS[1], PINKS[2], VIOLETS[1], VIOLETS[2], VIOLETS[3], VIOLETS[3]][k], dotted=0 if k < 3 else 2)
            if k < 2:
                FX.burst(f, CX, CY, 8 - 3 * k, [C("#ffffff"), PINKS[1], VIOLETS[1]], seed=self.seed + 120 + k, jag=0.3,
                         rays=10 - 4 * k)
            motes(f, CX, CY, 24 - 3 * k, 6 + 30 * t, 14 + 40 * t, STARS + VIOLETS[:2], seed=self.seed + 130 + k)
            FX.under(f, self.residue(min(1.0, 0.3 + t)))
            out.append(f)
        last = self.residue(1.0)
        motes(last, 64, 80, 7, 4, 22, STARS[:3], seed=self.seed + 140, arc=(-2.8, -0.3))
        out.append(last)
        return out

    def teleport_out(self):
        """Shadow step out: collapses into a point."""
        out = []
        for i, sc in enumerate((0.85, 0.6, 0.35, 0.12, 0.0)):
            p = NPose(scale=sc, dr=1.0 + 0.3 * (1 - sc), eye=1.4, lid=1.0, rot=ROT_STEP * 2 * i, reach=0.3, flick=i)
            f = self.draw(p)
            cx, cy = self.centre(p)
            rng = np.random.default_rng(self.seed + 150 + i)
            for k in range(8):
                a = rng.random() * TAU
                d0 = 40 * (0.4 + sc)
                streak(f, (cx + math.cos(a) * d0, cy + math.sin(a) * d0 * 0.7),
                       (cx + math.cos(a) * d0 * 0.6, cy + math.sin(a) * d0 * 0.42), VIOLETS[1 + k % 2])
            if i >= 3:
                FX.sparkle(f, int(cx), int(cy), 5 - i + 3, PINKS[0], VIOLETS[1])
            out.append(f)
        return out

    def teleport_in(self):
        """Shadow step in: a point of darkness tears open into the sphere."""
        out = []
        for i, sc in enumerate((0.0, 0.25, 0.55, 0.85, 1.0)):
            p = NPose(scale=sc, dr=1.0 + 0.3 * (1 - sc), eye=1.5 - 0.4 * sc, lid=1.1, rot=ROT_STEP * 2 * i, reach=0.6 * sc,
                      flick=i)
            f = self.draw(p)
            cx, cy = self.centre(p)
            if i == 0:
                FX.sparkle(f, int(cx), int(cy), 6, PINKS[0], VIOLETS[1])
                ring(f, cx, cy, 6, VIOLETS[2])
            if 1 <= i <= 3:
                ring(f, cx, cy, SPH_R * sc + 8 + 4 * i, [VIOLETS[1], VIOLETS[2], VIOLETS[3]][i - 1], dotted=2)
            out.append(f)
        return out

    def sheet(self):
        extras = [
            ("teleport_out", self.teleport_out(), dict(durations=[0.06, 0.06, 0.06, 0.08, 0.1], loop=False)),
            ("teleport_in", self.teleport_in(), dict(durations=[0.08, 0.06, 0.06, 0.06, 0.1], loop=False)),
        ]
        return build_sheet(self, standard_clips(self, extras))


BOSS = Nyxara()


def build():
    return BOSS.sheet()
