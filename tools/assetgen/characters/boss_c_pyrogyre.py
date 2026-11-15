"""Pyrogyre, l'Anneau solaire (Pyralis sanctuary guardian, 2.19).

A ring of magma in orbit around a solar eye: a small sun (white-hot core, orange limb, flickering
corona of flame tongues) with a slit pupil, circled by a tilted ring of molten lava under
drifting basalt plates and four ember beads.  It floats; the ring's back half passes behind the
eye, the front half in front of it.

Frames 128x128, pivot [64, 92] (bottom-centre of a 52x52 collision box: x 38..90, y 40..92, centred
on the eye (64, 66)).
Clips: idle 6, walk 6 (= move, drifting), move 6, telegraph 6 (ring spins up and widens),
attack 5 (fire f1, flare), telegraph_special 6 (pupil narrows, heat gathers: solar ray),
special 4 (ray muzzle, loop), summon 6 (the ring pours lava), roar 8, hurt 2, death 14."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, glow_orb, mat, motes, ring, split_frame, streak
from boss_c_common import TAU, Pose, build_sheet, dash_arc, outline_mask, over, shake, standard_clips
from raster import TCanvas, put, tint_toward

PI = math.pi
W = H = 128
PIVOT = [64, 92]
CX, CY = 64.0, 66.0
EYE_R = 19.0
RING_RX, RING_RY, RING_TILT = 46.0, 15.0, -0.22
N_PLATES, N_BEADS = 12, 4
ROT_STEP = (2 * PI / N_BEADS) / 6        # one bead step per 6-frame loop: seamless

SUN = mat("pg_sun", ["#fffef0", "#fff3b0", "#ffd86a", "#ffb13b", "#ff7a2a", "#e2451e"], emissive=True,
          th=[0.9, 0.76, 0.6, 0.44, 0.26])
SUN_DIM = mat("pg_sun_dim", ["#ffb13b", "#ff7a2a", "#e2451e", "#a8261a", "#6a1410"], emissive=True)
SUN_DEAD = mat("pg_sun_dead", ["#6a4a44", "#4e3430", "#36221e", "#22140f"])
IRIS = mat("pg_iris", ["#ff8a3d", "#d8401a", "#a0200e", "#6a1006"], emissive=True)
PUPIL = mat("pg_pupil", ["#3a1206", "#24090a", "#140404"])
FLAME = mat("pg_flame", ["#fff6c2", "#ffd86a", "#ffb13b", "#ff7a2a", "#e2451e"], emissive=True, outline=None,
            th=[0.86, 0.68, 0.48, 0.28])
LAVA = mat("pg_lava", ["#fff1c2", "#ffc56b", "#ff8a3d", "#ff5a1e", "#c8301a"], emissive=True,
           th=[0.88, 0.7, 0.5, 0.3])
LAVA_DIM = mat("pg_lava_dim", ["#ff8a3d", "#e2451e", "#a8261a", "#6a1410"], emissive=True)
CRUST = mat("pg_crust", ["#7a4234", "#5a2c24", "#401c1a", "#2a1012", "#1a080a"], th=[0.84, 0.64, 0.44, 0.24])
CRUST_DK = mat("pg_crust_dk", ["#4e2620", "#381818", "#261012", "#18080a"])
CRUST_DEAD = mat("pg_crust_dead", ["#5a5050", "#423a3a", "#2c2626", "#1a1616"])
BEAD = mat("pg_bead", ["#ffffff", "#fff3b0", "#ffd86a", "#ffb13b"], emissive=True)

HOTS = [C("#fffef0"), C("#fff3b0"), C("#ffd86a"), C("#ffb13b"), C("#ff7a2a"), C("#e2451e")]
EMBERS = [C("#ffd86a"), C("#ffb13b"), C("#ff7a2a"), C("#e2451e")]
SMOKE = [C("#6a5a5a"), C("#4e4242"), C("#3a3030")]


class PPose(Pose):
    DEFAULTS = dict(dx=0.0, dy=0.0, rot=0.0, tilt=RING_TILT, rr=1.0, eye=0.7, pupil=0.3, look=(3.0, 0.0),
                    corona=1.0, flick=0, dead=0.0, cracks=0, scatter=0.0, lid=1.0, ring_dy=0.0, pour=0.0,
                    plates=1.0)


class Pyrogyre:
    id = "pyrogyre"
    W, H, PIVOT = W, H, PIVOT
    seed = 21920
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 8, 10, 14
    ATTACK_EVENTS = [(1, "fire")]

    def centre(self, p):
        return (CX + p.dx, CY + p.dy)

    def ring_pt(self, p, th, r_off=0.0):
        cx, cy = self.centre(p)
        rx = RING_RX * p.rr + r_off
        ry = RING_RY * p.rr + r_off * 0.33
        x, y = math.cos(th) * rx, math.sin(th) * ry
        c, s = math.cos(p.tilt), math.sin(p.tilt)
        return (cx + x * c - y * s, cy + p.ring_dy + x * s + y * c)

    def eye_pos(self, p):
        cx, cy = self.centre(p)
        return (cx + p.look[0], cy + p.look[1])

    # ------------------------------------------------------------------ draw
    def draw(self, p):
        cv = TCanvas(W, H)
        dead = p.dead >= 0.5
        self._ring(cv, p, back=True, dead=dead)
        self._corona(cv, p, dead)
        self._eye(cv, p, dead)
        self._ring(cv, p, back=False, dead=dead)
        return cv.render()

    def _ring(self, cv, p, back, dead):
        n = 40
        a0, a1 = (PI, 2 * PI) if back else (0.0, PI)
        lm = (LAVA if not back else LAVA_DIM) if not dead else CRUST_DEAD
        if p.scatter > 0:
            self._ring_broken(cv, p, back, dead)
            return
        pts = [self.ring_pt(p, a0 + (a1 - a0) * i / n) for i in range(n + 1)]
        th = 4.6 if not back else 3.8
        cv.part()
        cv.chain(pts, [th] * len(pts), lm, flat=0.55 if not back else 0.5)
        # molten inner edge
        if not dead:
            cv.part(line=False)
            inner = [self.ring_pt(p, a0 + (a1 - a0) * i / n, r_off=-2.2) for i in range(n + 1)]
            cv.chain(inner, [1.1] * len(inner), lm, flat=0.92 if not back else 0.7, decal=True)
        # drifting basalt plates
        cm = (CRUST if not back else CRUST_DK) if not dead else CRUST_DEAD
        if p.plates > 0:
            for k in range(N_PLATES):
                t = (p.rot + TAU * k / N_PLATES) % TAU
                if not (a0 <= t <= a1):
                    continue
                if (k * 5) % 7 == 3 and not dead:
                    continue                      # a few gaps where the lava shows
                q0 = self.ring_pt(p, t - 0.1, r_off=1.2)
                q1 = self.ring_pt(p, t + 0.08, r_off=1.0)
                cv.part()
                cv.capsule(q0, q1, (3.2 if not back else 2.6) * p.plates, (2.4 if not back else 2.0) * p.plates, cm,
                           bias=0.06 if not back else 0.0)
                if not dead and not back:
                    cv.part(line=False)
                    qm = self.ring_pt(p, t - 0.01, r_off=1.4)
                    cv.px_set(qm[0], qm[1], LAVA, inten=0.6, decal=True)
        # ember beads
        if not dead:
            for k in range(N_BEADS):
                t = (p.rot + TAU * k / N_BEADS + TAU / (2 * N_PLATES)) % TAU
                if not (a0 <= t <= a1):
                    continue
                q = self.ring_pt(p, t, r_off=1.0)
                cv.part(line=False)
                cv.disc(q, 3.0 if not back else 2.4, BEAD, flat=0.55 if back else 0.75)
                cv.disc((q[0] - 0.6, q[1] - 0.6), 1.2, BEAD, flat=1.0)

    def _ring_broken(self, cv, p, back, dead):
        """Death: the ring breaks into chunks that fall away."""
        sc = p.scatter
        rng = np.random.default_rng(self.seed + 5)
        for k in range(N_PLATES):
            t = (p.rot + TAU * k / N_PLATES) % TAU
            front = 0.0 <= t <= PI
            if front == back:
                continue
            q = self.ring_pt(p, t)
            vx = math.cos(t) * 10 * sc + (rng.random() - 0.5) * 6 * sc
            vy = 30 * sc * sc + rng.random() * 8 * sc
            c = (q[0] + vx, q[1] + vy)
            ang = math.atan2(math.sin(t), math.cos(t)) + PI / 2 + sc * (2.0 if k % 2 else -1.6)
            cv.part()
            cv.capsule((c[0] - math.cos(ang) * 4, c[1] - math.sin(ang) * 4),
                       (c[0] + math.cos(ang) * 4, c[1] + math.sin(ang) * 4), 3.6, 3.0,
                       CRUST_DEAD if dead else (LAVA_DIM if sc > 0.6 else LAVA), flat=0.5)
            if not dead or sc < 0.8:
                cv.part()
                cv.capsule((c[0] - math.cos(ang) * 3, c[1] - math.sin(ang) * 3 - 1),
                           (c[0] + math.cos(ang) * 3, c[1] + math.sin(ang) * 3 - 1), 2.6, 2.2,
                           CRUST if not dead else CRUST_DEAD)

    def _corona(self, cv, p, dead):
        if dead or p.corona <= 0.05:
            return
        cx, cy = self.centre(p)
        cv.part(line=False)
        n = 14
        for k in range(n):
            a = TAU * k / n + 0.11 * math.sin(p.flick * 1.3 + k)
            ln = (4.0 + 3.0 * ((k * 7 + p.flick * 3) % 5) / 4.0) * p.corona
            r0 = EYE_R - 2.0
            q0 = (cx + math.cos(a) * r0, cy + math.sin(a) * r0)
            q1 = (cx + math.cos(a + 0.12) * (r0 + ln + 2), cy + math.sin(a + 0.12) * (r0 + ln + 2))
            cv.capsule(q0, q1, 3.2, 0.7, FLAME, flat=0.42)
            cv.capsule(q0, ((q0[0] + q1[0]) / 2, (q0[1] + q1[1]) / 2), 1.8, 0.6, FLAME, flat=0.62)

    def _eye(self, cv, p, dead):
        cx, cy = self.centre(p)
        sm = SUN if not dead else SUN_DEAD
        if not dead and p.eye < 0.3:
            sm = SUN_DIM
        cv.part()
        g = p.eye
        bands = [(EYE_R, 0.32), (EYE_R - 3.0, 0.48), (EYE_R - 6.5, 0.64), (EYE_R - 10.5, 0.8), (EYE_R - 14.0, 0.95)]
        for k, (r, inten) in enumerate(bands):
            off = (-1.2 * k * 0.5, -1.2 * k * 0.5)
            cv.disc((cx + off[0], cy + off[1]), r, sm, flat=min(1.0, inten + 0.12 * (g - 0.7)), decal=k > 0)
        # granulation
        if not dead:
            cv.part(line=False)
            rng = np.random.default_rng(self.seed + 30 + p.flick)
            for _ in range(22):
                a = rng.random() * TAU
                d = EYE_R * math.sqrt(rng.random()) * 0.92
                cv.px_set(cx + math.cos(a) * d, cy + math.sin(a) * d, sm, inten=0.3 + 0.5 * rng.random(), decal=True)
        # iris + slit pupil
        ex, ey = self.eye_pos(p)
        pw = 1.6 + 3.6 * p.pupil
        ph = 11.5 * min(1.0, p.lid)
        cv.part(line=False)
        if dead:
            cv.ellipse((ex, ey), 2.0, ph * 0.6, PUPIL, flat=0.3)
            return
        cv.ellipse((ex, ey), pw + 2.6, ph + 2.2, IRIS, flat=0.55 + 0.2 * min(1.0, g))
        cv.ellipse((ex, ey), pw, ph, PUPIL, flat=0.4)
        cv.px_set(ex - pw * 0.4, ey - ph * 0.55, IRIS, inten=1.0)

    # ---------------------------------------------------------------- effects
    def embers(self, f, i, n, seed, count=8, rise=20.0, x0=30, x1=98, y0=56, y1=110):
        rng = np.random.default_rng(seed)
        for k in range(count):
            x = x0 + rng.random() * (x1 - x0)
            y = y0 + rng.random() * (y1 - y0)
            off = rng.random()
            t = (i / n + off) % 1.0
            xx = int(x + 2 * math.sin(TAU * (t + off)))
            yy = int(y - rise * t)
            if 0.08 < t < 0.92 and 0 <= xx < W and 0 <= yy < H and f[yy, xx, 3] == 0:
                f[yy, xx] = EMBERS[k % 4]

    def drips(self, f, p, i, n, seed, count=3, length=26.0):
        """Lava drops falling from the underside of the ring (periodic over n frames)."""
        rng = np.random.default_rng(seed)
        for k in range(count):
            th = 0.4 + rng.random() * (PI - 0.8)
            off = rng.random()
            t = (i / n + off) % 1.0
            q = self.ring_pt(p, th, r_off=3.0)
            y = q[1] + 2 + length * t * t
            x = q[0]
            if t < 0.85:
                glow_orb(f, x, y, 1.6, [HOTS[2], HOTS[4]], only_empty=True)
                streak(f, (x, y - 3), (x, y - 1), HOTS[4])

    # ================================================================== clips
    def idle(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            p = PPose(dy=1.6 * math.sin(ph), rot=ROT_STEP * i, eye=0.75 + 0.15 * math.sin(ph), flick=i,
                      corona=1.0 + 0.12 * math.sin(ph * 2), tilt=RING_TILT + 0.02 * math.sin(ph))
            f = self.draw(p)
            self.embers(f, i, 6, self.seed + 1)
            self.drips(f, p, i, 6, self.seed + 2, count=2)
            out.append(f)
        return out

    def walk(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            p = PPose(dx=1.0, dy=1.2 * math.sin(ph), rot=ROT_STEP * 1.5 * i * 0 + ROT_STEP * i, tilt=RING_TILT + 0.1,
                      eye=0.85, flick=i, corona=1.15, look=(4.5, 0.5))
            f = self.draw(p)
            rng = np.random.default_rng(self.seed + 10 + i)
            for k in range(6):
                y = int(rng.integers(40, 96))
                x0 = int(rng.integers(2, 20))
                streak(f, (x0, y), (x0 + int(rng.integers(5, 12)), y), EMBERS[2 + k % 2])
            self.embers(f, i, 6, self.seed + 3, count=6, x0=8, x1=50)
            out.append(f)
        return out

    def telegraph(self):
        """The ring spins up and widens, the eye heats up (couronne de feu, spirale, eruptions)."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = PPose(rot=ROT_STEP * (1 + 2.5 * t) * i, rr=1.0 + 0.18 * t, eye=0.8 + 0.8 * t, corona=1.0 + 0.6 * t,
                      pupil=0.3 - 0.15 * t, flick=i, tilt=RING_TILT * (1 - 0.3 * t), dy=-1.5 * t)
            f = self.draw(p)
            cx, cy = self.centre(p)
            motes(f, cx, cy, 6 + 4 * i, 30, 58 - 10 * t, EMBERS, seed=self.seed + 20 + i, inward=0.15 * t, big=0.2)
            if i >= 2:
                dash_arc(f, cx, cy + p.ring_dy, RING_RX * p.rr + 8, RING_RY * p.rr + 6, 0, TAU, EMBERS[1],
                         dash=3, gap=3, phase=i * 2)
            if i == 5:
                FX.sparkle(f, int(cx), int(cy - EYE_R - 6), 6, HOTS[0], HOTS[3])
            out.append(f)
        return out

    def attack(self):
        """Flare: the ring pulses out and jets of fire burst from it (projectiles spawn on f1)."""
        keys = [(1.22, 1.6, 0.1), (1.3, 1.7, 0.4), (1.16, 1.3, 0.3), (1.06, 1.0, 0.3), (1.0, 0.8, 0.3)]
        out = []
        for i, (rr, eye, pu) in enumerate(keys):
            p = PPose(rot=ROT_STEP * (6 + 2 * i), rr=rr, eye=eye, pupil=pu, corona=[1.6, 2.0, 1.5, 1.2, 1.0][i],
                      flick=i, dy=[-1.5, 0, 0.5, 0.5, 0][i])
            f = self.draw(p)
            if i == 1:
                for k in range(8):
                    th = TAU * k / 8 + 0.2
                    q = self.ring_pt(p, th, r_off=5.0)
                    q2 = self.ring_pt(p, th, r_off=14.0)
                    streak(f, q, q2, HOTS[3])
                    glow_orb(f, q2[0], q2[1], 2.6, HOTS[1:5])
                cx, cy = self.centre(p)
                ring(f, cx, cy, EYE_R + 10, HOTS[2], dotted=2)
            if i == 2:
                for k in range(8):
                    th = TAU * k / 8 + 0.2
                    q2 = self.ring_pt(p, th, r_off=22.0)
                    glow_orb(f, q2[0], q2[1], 1.8, HOTS[2:5], only_empty=True)
            if i == 3:
                motes(f, CX, CY, 14, 40, 62, EMBERS, seed=self.seed + 40)
            out.append(f)
        return out

    def telegraph_special(self):
        """Solar ray wind-up: the ring tilts up out of the way, the pupil narrows to a slit and the eye
        fills with white heat."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            p = PPose(rot=ROT_STEP * (1 + 3 * t) * i, tilt=RING_TILT - 0.5 * t, rr=1.0 - 0.06 * t, eye=0.8 + 0.9 * t,
                      pupil=0.3 - 0.3 * t, lid=1.0 - 0.15 * t, look=(3.0 + 3.0 * t, 0.0), corona=1.0 + 0.4 * t,
                      flick=i, dx=-1.5 * t)
            f = self.draw(p)
            ex, ey = self.eye_pos(p)
            motes(f, ex, ey, 8 + 4 * i, 10, 40 - 16 * t, HOTS[:4], seed=self.seed + 50 + i, inward=0.15 * t, big=0.2)
            if i >= 2:
                ring(f, ex, ey, 28 - 3 * i, HOTS[3], dotted=2)
            if i == 5:
                FX.sparkle(f, int(ex + 2), int(ey), 7, HOTS[0], HOTS[2])
            out.append(f)
        return out

    def special(self):
        """Solar ray (loop): eye white-hot, pupil open, the muzzle flare in front (the ray itself is drawn
        by the game)."""
        out = []
        for i in range(4):
            p = PPose(rot=ROT_STEP * (4 * i), tilt=RING_TILT - 0.5, rr=0.94, eye=1.7, pupil=0.9, lid=1.05,
                      look=(6.0, 0.0), corona=1.5 + 0.2 * (i % 2), flick=i, dx=-2.0 - 0.5 * (i % 2))
            f = self.draw(p)
            ex, ey = self.eye_pos(p)
            FX.burst(f, ex + 10, ey, 8 + (i % 2), HOTS[:5], seed=self.seed + 60 + i, jag=0.35)
            motes(f, ex + 10, ey, 10, 8, 20, EMBERS, seed=self.seed + 64 + i)
            out.append(f)
        return out

    def summon(self):
        """The ring tips down and pours lava (eruptions, maree de lave)."""
        out = []
        tilts = [0.0, 0.35, 0.55, 0.55, 0.45, 0.15]
        for i, tl in enumerate(tilts):
            p = PPose(rot=ROT_STEP * i, tilt=RING_TILT + tl, rr=1.0 + 0.06 * tl, eye=1.0 + 0.6 * tl,
                      look=(2.0, 4.0 * tl), corona=1.2, flick=i, dy=-2.0 * tl, ring_dy=3.0 * tl)
            f = self.draw(p)
            if 1 <= i <= 4:
                for k in range(3 + i):
                    th = PI * (0.15 + 0.7 * k / (2 + i))
                    q = self.ring_pt(p, th, r_off=3.5)
                    ln = 8 + 7 * i + 5 * (k % 2)
                    # a tapering stream that breaks into drops
                    y = q[1] + 1
                    r = 2.4
                    step = 0
                    while y < min(H - 2, q[1] + ln):
                        glow_orb(f, q[0] + 0.3 * math.sin(y * 0.4 + k), y, r, HOTS[1:5] if r > 1.6 else HOTS[2:5],
                                 only_empty=True)
                        y += 1.6 + step * 0.5
                        r = max(1.0, r - 0.18)
                        step += 1
                    glow_orb(f, q[0], q[1] + ln + 3, 2.0, HOTS[1:5], only_empty=True)
            if i >= 2:
                for x in (24, 48, 80, 104):
                    FX.burst(f, x, 124, 2 + i * 0.6, HOTS[2:6], seed=self.seed + 70 + i + x, jag=0.5)
            self.embers(f, i, 6, self.seed + 75, count=8)
            out.append(f)
        return out

    def roar(self):
        keys = [  # rr, eye, corona, shake
            (0.95, 0.8, 1.0, 0), (1.0, 1.0, 1.2, 0), (1.25, 1.7, 2.2, 1), (1.28, 1.7, 2.4, -1), (1.28, 1.7, 2.4, 1),
            (1.25, 1.6, 2.2, -1), (1.1, 1.1, 1.4, 0), (1.0, 0.8, 1.0, 0)]
        out = []
        for i, (rr, eye, co, sh) in enumerate(keys):
            p = PPose(rot=ROT_STEP * 3 * i, rr=rr, eye=eye, corona=co, pupil=0.1 if eye > 1.2 else 0.3,
                      lid=1.1 if eye > 1.2 else 1.0, flick=i)
            f = self.draw(p)
            if eye > 1.2:
                cx, cy = self.centre(p)
                for k, r in enumerate((EYE_R + 14 + 2 * (i % 2), EYE_R + 22 + 2 * (i % 2))):
                    ring(f, cx, cy, r, HOTS[3 + k], dotted=0 if k == 0 else 2)
                motes(f, cx, cy, 16, 30, 62, EMBERS, seed=self.seed + 80 + i, big=0.2)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def hurt(self):
        a = PPose(dx=-2, rot=ROT_STEP, eye=1.3, pupil=0.0, lid=0.6, corona=0.8, rr=0.95, tilt=RING_TILT + 0.1)
        b = PPose(dx=-1, rot=ROT_STEP * 2, eye=0.9, pupil=0.2, lid=0.85, corona=0.9)
        fa = FX.flash(self.draw(a), amount=0.55)
        fb = self.draw(b)
        motes(fb, CX, CY, 8, 20, 40, EMBERS, seed=self.seed + 90)
        return [fa, fb]

    def residue(self, t):
        cv = TCanvas(W, H)
        rocks = [(64, 90, 11, 4.0, CRUST_DEAD), (50, 91, 6, 3.0, CRUST_DK), (78, 91, 7, 3.2, CRUST),
                 (58, 87, 5, 2.8, CRUST), (70, 87, 4, 2.4, CRUST_DEAD), (38, 92, 4, 2.0, CRUST_DK),
                 (90, 92, 4, 2.0, CRUST)]
        for i, (x, y, rx, ry, m) in enumerate(rocks):
            cv.part()
            cv.ellipse((x, y + (1 - t) * 3), rx * (0.7 + 0.3 * t), ry * max(0.5, t), m, ang=0.12 * ((i % 3) - 1))
        cv.part(line=False)
        cv.px_set(64, 87, LAVA_DIM, 0.5)
        cv.px_set(77, 89, LAVA_DIM, 0.4)
        return cv.render()

    def death(self):
        out = []
        seq = [PPose(eye=1.7, corona=2.2, pupil=0.0, lid=1.2, rr=1.1, rot=ROT_STEP, flick=0),
               PPose(eye=0.25, corona=0.4, pupil=0.6, lid=0.7, rr=1.05, rot=ROT_STEP * 2.5, flick=1, dx=1),
               PPose(eye=1.5, corona=1.8, pupil=0.0, lid=1.1, rr=1.12, rot=ROT_STEP * 3, flick=2, cracks=1)]
        for i, p in enumerate(seq):
            f = self.draw(p)
            if i == 0:
                f = FX.flash(f, amount=0.5)
            cx, cy = self.centre(p)
            FX.sparkle(f, int(cx), int(cy), 4 + 2 * i, HOTS[0], HOTS[3])
            out.append(shake(f, [1, -1, 1][i], 0))
        fall = [  # scatter, eye, dy, corona, dead
            (0.25, 1.0, 2, 1.2, 0.1), (0.5, 0.6, 5, 0.6, 0.3), (0.75, 0.3, 9, 0.2, 0.5), (1.0, 0.1, 13, 0.0, 0.8)]
        for i, (sc, eye, dy, co, dead) in enumerate(fall):
            p = PPose(scatter=sc, eye=eye, dy=dy, corona=co, dead=dead, rot=ROT_STEP * (3.5 + 0.5 * i), pupil=0.5,
                      lid=0.6, flick=3 + i)
            f = self.draw(p)
            cx, cy = self.centre(p)
            if i >= 1:
                sep = [0, 2.0, 4.5, 7.0][i]
                f = split_frame(f, cx, cy, -0.6, [(-sep, sep * 0.6), (sep, sep * 0.2), (-sep * 0.5, sep), (sep * 0.8, sep)],
                                HOTS[3], seed=self.seed + 100, cuts=2 if i >= 2 else 1)
                f = tint_toward(f, C("#4e3430"), 0.12 * i, mask=~outline_mask(f))
            r = [14, 18, 12, 7][i]
            FX.burst(f, cx, cy, r, HOTS[:6], seed=self.seed + 110 + i, jag=0.45, rays=6 if i < 2 else 0)
            motes(f, cx, cy, 16, 14, 50, EMBERS, seed=self.seed + 120 + i)
            out.append(f)
        base = out[-1].copy()
        for k, t in enumerate((0.15, 0.32, 0.5, 0.66, 0.82, 0.95)):
            f = FX.dissolve(base, t, seed=self.seed + 130, colors=[C("#fff3b0"), C("#ffb13b"), C("#ff7a2a"),
                                                                    C("#a8261a")],
                            rise=26, drift=6, top_first=True, life=0.4, particle_rate=0.16, bounds=(0, 0, W, H - 1))
            FX.under(f, self.residue(min(1.0, 0.3 + t)))
            out.append(f)
        last = self.residue(1.0)
        motes(last, 64, 80, 7, 4, 22, EMBERS[:3], seed=self.seed + 140, arc=(-2.8, -0.3))
        out.append(last)
        return out

    def sheet(self):
        return build_sheet(self, standard_clips(self))


BOSS = Pyrogyre()


def build():
    return BOSS.sheet()
