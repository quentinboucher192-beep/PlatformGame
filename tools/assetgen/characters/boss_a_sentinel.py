"""Prime Sentinel (Sentinelle Prime, Sylvanox) — core rig: a huge floating ancient
stone + metal eye-core.  Mossy stone sphere with an amber/red eye behind iris
shutters, four rune-carved armour plates orbiting it, two cannon arms (above and
below the eye), summoning hatches and an amber lift thruster."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, glow_orb, mat, motes, ring, streak
from boss_a_core import CX, CY, H, W
from common.palette import AMBER, DANGER, STONE
from raster import TCanvas, put

PI = math.pi

STONE6 = mat("ps_stone", ["#a6bfcd", "#7d97a8", "#5a7488", "#3f586a", "#2b3f50", "#1d2b3a"])
PLATE = mat("ps_plate", ["#b4c8d4", "#8aa4b6", "#647e92", "#465e72", "#2f4356", "#1d2b3a"])
PLATE_DK = mat("ps_plate_dk", ["#7d97a8", "#5a7488", "#3f586a", "#2b3f50", "#1d2b3a"])
METAL = mat("ps_metal", ["#d4dee9", "#a7b8cb", "#7a8ea6", "#56677e", "#38465a"])
METAL_DK = mat("ps_metal_dk", ["#98aac0", "#6f839c", "#4e5f76", "#36445a", "#242f40"])
MOSS = mat("ps_moss", ["#4fd49a", "#35b583", "#1f8a6a", "#135c50", "#0f3b3a"])
SOCKET = mat("ps_socket", ["#2a4068", "#1a2747", "#121a33", "#0b0f1e"])
EYE = mat("ps_eye", ["#fff4d6", "#ffd98a", "#ffb347", "#ff8a3d", "#ff4d6d", "#a3123a"], emissive=True,
          th=[0.9, 0.78, 0.64, 0.48, 0.3])
AMB = mat("ps_amber", ["#fff4d6", "#ffd98a", "#ffb347", "#c8741f"], emissive=True)
AMB_OFF = mat("ps_amber_off", ["#577286", "#3b5466", "#2a3e4f"])
BIO = mat("ps_bio", ["#e6fffb", "#8ffff0", "#39f0d8", "#1fb8b0"], emissive=True)
FLAME = mat("ps_flame", ["#fff4d6", "#ffd98a", "#ffb347", "#ff8a3d", "#c8741f"], emissive=True, outline=None)
CRACK = mat("ps_crack", ["#0b0f1e"])
CRACK_HOT = mat("ps_crack_hot", ["#ffd98a", "#ff8a3d", "#ff4d6d"], emissive=True)

AMBERS = [C("#fff4d6"), AMBER[2], AMBER[1], AMBER[0]]
REDS = [C("#fff0e0"), C("#ffd0a0"), DANGER[2], DANGER[1], DANGER[0]]
DUST = [STONE[4], STONE[3], STONE[2]]
EMBERS = [C("#ffd98a"), C("#ffb347"), C("#ff8a3d"), C("#c8741f")]


def rot2(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


HATCHES = ((-2.35, 17.0), (1.95, 16.5))


class Sentinel:
    id = "prime_sentinel"
    seed = 3300
    rot_step = (PI / 2) / 6       # 4 plates: a quarter turn per 6-frame loop is seamless
    spin_step = PI / 6
    wither = C("#3b5466")
    hot = C("#ff8a3d")
    split_angle = -0.5
    dissolve_cols = [C("#ffd98a"), C("#ffb347"), C("#ff8a3d"), C("#c8741f")]
    EYE_OFF = (9.0, -2.0)

    # ------------------------------------------------------------ positions
    def L(self, p, x, y):
        """Body-local -> canvas (before the TCanvas tilt, which rotates about the centre)."""
        c = p.c
        return (c[0] + x, c[1] + y)

    def W(self, p, x, y):
        """Body-local -> final frame pixel (tilt applied)."""
        v = rot2((x, y), p.tilt)
        c = p.c
        return (c[0] + v[0], c[1] + v[1])

    def eye_pos(self, p):
        return self.W(p, self.EYE_OFF[0] + 2.0, self.EYE_OFF[1])

    def cannons(self, p):
        """[(pivot_local, angle, dark)] upper (far) then lower (near)."""
        sp = p.spread
        back = -3.5 * max(0.0, -p.arm) + 2.0 * max(0.0, p.arm) - p.recoil
        up_a = -0.05 - 0.42 * sp
        lo_a = 0.05 + 0.42 * sp
        up = ((16.0 + back * math.cos(up_a), -23.5 - 5.0 * sp + back * math.sin(up_a)), up_a, True)
        lo = ((18.0 + back * math.cos(lo_a), 21.5 + 5.0 * sp + back * math.sin(lo_a)), lo_a, False)
        return [up, lo]

    def muzzles(self, p):
        out = []
        for (pv, a, _) in self.cannons(p):
            out.append(self.W(p, pv[0] + math.cos(a) * 24.5, pv[1] + math.sin(a) * 24.5))
        return out

    # ------------------------------------------------------------------ draw
    def draw(self, p, layer=None):
        c = p.c
        cv = TCanvas(W, H, rot=p.tilt, center=c)
        L = lambda x, y: (c[0] + x, c[1] + y)  # noqa: E731
        dead = p.dead >= 0.5
        if layer in (None, "body"):
            self._thruster(cv, p, L)
        if layer in (None, "plates"):
            self._plates(cv, p, L)
        if layer == "plates":
            return cv.render()
        if layer == "body":
            # plates are rendered on their own layer: start a fresh canvas for the rest
            body_only = cv.render()
            cv = TCanvas(W, H, rot=p.tilt, center=c)
            self._sphere(cv, p, L, dead)
            self._eye(cv, p, L, dead)
            for (pv, a, dark) in self.cannons(p):
                self._cannon(cv, p, L, pv, a, dark, dead)
            img = cv.render()
            sel = (body_only[:, :, 3] > 0) & (img[:, :, 3] == 0)
            img[sel] = body_only[sel]
            return img
        self._sphere(cv, p, L, dead)
        self._eye(cv, p, L, dead)
        for (pv, a, dark) in self.cannons(p):
            self._cannon(cv, p, L, pv, a, dark, dead)
        return cv.render()

    def _thruster(self, cv, p, L):
        if p.thrust <= 0.05 or p.dead >= 0.8:
            return
        th = p.thrust * (1.0 + 0.2 * (p.flick % 2))
        cv.part(line=False)
        for (x, y, s) in ((0.0, 22.0, 1.0), (-12.0, 18.5, 0.6), (12.0, 18.5, 0.6)):
            ln = 4.5 * th * s
            cv.tcapsule(L(x, y), L(x - 2.0 * p.tilt * 10 * 0.1, y + ln), 2.4 * s + 0.3, 0.5, FLAME, flat=0.55)
            cv.tcapsule(L(x, y), L(x, y + ln * 0.55), 1.2 * s + 0.2, 0.4, FLAME, flat=0.95)
        cv.part()
        cv.tcapsule(L(-3, 21.5), L(3, 21.5), 2.6, 2.6, METAL_DK)

    def _plate_poly(self, cx, cy, th, R, hw, t_in, t_out, spin=0.0, off=(0.0, 0.0)):
        pts = []
        n = 7
        for i in range(n):
            a = th - hw + 2 * hw * i / (n - 1)
            pts.append((cx + math.cos(a) * (R + t_out), cy + math.sin(a) * (R + t_out)))
        for i in range(n):
            a = th + hw - 2 * hw * i / (n - 1)
            pts.append((cx + math.cos(a) * (R - t_in), cy + math.sin(a) * (R - t_in)))
        if spin:
            mx, my = cx + math.cos(th) * R, cy + math.sin(th) * R
            pts = [(mx + rot2((x - mx, y - my), spin)[0], my + rot2((x - mx, y - my), spin)[1]) for x, y in pts]
        return [(x + off[0], y + off[1]) for x, y in pts]

    def _plates(self, cv, p, L):
        c = L(0, 0)
        dead = p.dead >= 0.5
        for k in range(4):
            th = p.rot + k * PI / 2 + PI / 4
            if p.slam > 0:
                ts = PI / 2 + (k - 1.5) * 0.6
                d = (ts - th + PI) % (2 * PI) - PI
                th = th + d * min(1.0, p.slam)
            R = 30.0 + p.prad - 8.0 * min(1.0, p.slam)
            hw = 0.42 + 0.08 * min(1.0, p.slam)
            sc = p.scatter
            off = (math.cos(th) * 34 * sc + (k - 1.5) * 4 * sc, math.sin(th) * 20 * sc + 46 * sc * sc)
            spin = sc * (1.4 if k % 2 else -1.2)
            poly = self._plate_poly(c[0], c[1], th, R, hw, 4.0, 4.0, spin, off)
            cv.part()
            cv.tpoly(poly, PLATE if not dead else PLATE_DK, bevel=1.8, base=0.56)
            # metal trim along the outer edge, rune in the middle, rivets at the ends
            cv.part(line=False)
            mid = self._plate_poly(c[0], c[1], th, R + 2.2, hw * 0.9, 0.0, 0.0, spin, off)[:7]
            for a, b in zip(mid[:-1], mid[1:]):
                cv.tline(a, b, METAL, inten=0.75, decal=True)
            # amber anti-grav glow along the inner edge
            if not dead:
                inner = self._plate_poly(c[0], c[1], th, R - 3.4, hw * 0.8, 0.0, 0.0, spin, off)[:7]
                gi = 0.35 + 0.35 * min(1.0, p.eye) + 0.3 * min(1.0, p.slam)
                for a, b in zip(inner[:-1], inner[1:]):
                    cv.tline(a, b, AMB, inten=min(1.0, gi), decal=True)
            rc = self._plate_poly(c[0], c[1], th, R - 0.8, 0.0, 0.0, 0.0, spin, off)[0]
            rm = AMB if (not dead and p.eye > 0.05) else AMB_OFF
            g = 0.45 + 0.4 * min(1.0, p.eye) + 0.3 * min(1.0, p.slam)
            cv.tpoly([(rc[0] - 2.2, rc[1]), (rc[0], rc[1] - 2.2), (rc[0] + 2.2, rc[1]), (rc[0], rc[1] + 2.2)], rm,
                     flat=min(1.0, g))
            ends = self._plate_poly(c[0], c[1], th, R - 0.5, hw * 0.72, 0.0, 0.0, spin, off)
            cv.tpx(ends[0], CRACK, inten=0.2, decal=True)
            cv.tpx(ends[6], CRACK, inten=0.2, decal=True)

    def _sphere(self, cv, p, L, dead):
        cv.part()
        cv.tdisc(L(0, 0), 22.5, STONE6)
        # lower metal shell + equator band
        cv.tellipse(L(0, 15.0), 18.0, 8.8, METAL, decal=True, bias=-0.1)
        cv.part(line=False)
        band = [L(22 * math.cos(u), 5.0 + 3.4 * math.sin(u)) for u in np.linspace(0.05, PI - 0.05, 18)]
        cv.tchain(band, [1.3] * len(band), METAL, decal=True, bias=0.15)
        rm = AMB if not dead else AMB_OFF
        for u0 in (0.55, 1.2, 2.25, 2.75):
            a = L(22 * math.cos(u0), 5.0 + 3.4 * math.sin(u0))
            b = L(22 * math.cos(u0 + 0.16), 5.0 + 3.4 * math.sin(u0 + 0.16))
            cv.tline(a, b, rm, inten=0.6 + 0.3 * ((p.flick + int(u0 * 10)) % 2), decal=True)
        # moss cap with bioluminescent dots
        cv.tellipse(L(-7.0, -16.0), 12.5, 6.4, MOSS, ang=-0.35, decal=True, bias=0.08)
        if not dead:
            for (x, y) in ((-12, -13), (-6, -17), (-1, -16), (-9, -11)):
                cv.tpx(L(x, y), BIO, inten=0.55 + 0.35 * ((p.flick + x) % 2), decal=True)
        # carved seams + glyph runes
        for a, b in (((-3, -19), (-7, -7)), ((-7, -7), (-18, -5)), ((-7, -7), (-5, 3)), ((5, -20), (3, -12)),
                     ((-14, 8), (-6, 12))):
            cv.tline(L(*a), L(*b), CRACK, inten=0.2, decal=True)
        for (a, b) in (((-14, -1), (-11, -1)), ((-11, -1), (-11, 2)), ((-4, 9), (-1, 9)), ((-2, 8), (-2, 11))):
            cv.tline(L(*a), L(*b), rm, inten=0.5 + 0.4 * min(1.0, p.eye), decal=True)
        # summoning hatches
        for (ang, r) in HATCHES:
            hx, hy = math.cos(ang) * r, math.sin(ang) * r
            h = p.hatch
            cv.part()
            if h > 0.05:
                cv.tdisc(L(hx, hy), 4.6, SOCKET, flat=0.3)
                cv.part(line=False)
                cv.tdisc(L(hx, hy), 1.2 + 2.4 * h, AMB, flat=0.4 + 0.5 * h)
                cv.part()
                ox, oy = math.cos(ang) * 6.0 * h, math.sin(ang) * 6.0 * h
                cv.tellipse(L(hx + ox, hy + oy), 4.6, 2.0, METAL, ang=ang + PI / 2, bias=0.05)
            else:
                cv.tdisc(L(hx, hy), 4.4, METAL_DK, bias=0.05)
                cv.part(line=False)
                cv.tdisc(L(hx, hy), 2.2, METAL_DK, bias=-0.15)
        if p.cracks:
            cv.part(line=False)
            cm = CRACK_HOT if not dead else CRACK
            segs = [((-2, -18), (2, -12)), ((2, -12), (-1, -6)), ((-16, 4), (-10, 7)), ((10, 10), (14, 15))]
            if p.cracks > 1:
                segs += [((-12, -10), (-17, -4)), ((4, 14), (0, 19)), ((-1, -6), (-6, -2)), ((14, -12), (18, -8))]
            for a, b in segs:
                cv.tline(L(*a), L(*b), cm, inten=0.75, decal=True)

    def _eye(self, cv, p, L, dead):
        ex, ey = self.EYE_OFF
        E = L(ex, ey)
        # bezel
        cv.part()
        cv.tdisc(E, 12.0 + 0.8 * p.open, METAL, bias=0.05)
        cv.part()
        cv.tdisc(E, 9.8 + 0.6 * p.open, SOCKET, flat=0.3)
        cv.part(line=False)
        if dead:
            cv.tdisc((E[0] + 1, E[1]), 5.5, SOCKET, flat=0.15)
            return
        lid = p.lid
        ir = 7.3 * min(1.0, 0.55 + 0.45 * lid) + 0.6 * max(0.0, lid - 1.0) + 0.8 * p.open
        cv.tdisc((E[0] + 1.0, E[1]), ir, EYE, bias=-0.22 + 0.32 * p.eye)
        cv.tdisc((E[0] + 1.6, E[1] - 0.4), 2.2 + 1.0 * min(1.6, p.eye), EYE, flat=min(1.0, 0.7 + 0.2 * p.eye))
        if p.eye < 1.25:
            cv.tcapsule((E[0] + 2.2, E[1] - 3.6 * (1.25 - p.eye)), (E[0] + 2.2, E[1] + 3.6 * (1.25 - p.eye)),
                        0.9, 0.9, SOCKET, flat=0.1)
        cv.tpx((E[0] - 2.0, E[1] - 3.0), EYE, inten=1.0)
        # iris shutters (eyelids)
        if lid < 0.98:
            Ec = cv.T(E)
            gap = 9.8 * max(0.0, lid)
            cv.part()
            cv.tdisc(E, 10.4, METAL_DK, bias=0.05, within=lambda px, py: py < Ec[1] - gap)
            cv.part()
            cv.tdisc(E, 10.4, METAL_DK, bias=-0.05, within=lambda px, py: py > Ec[1] + gap)

    def _cannon(self, cv, p, L, pv, a, dark, dead):
        m = METAL_DK if dark else METAL
        hm = PLATE_DK if dark else PLATE
        d = (math.cos(a), math.sin(a))
        n = (-d[1], d[0])
        sh = (10.0, -15.5) if dark else (12.0, 14.0)
        P = L(*pv)
        S = L(*sh)
        cv.part()
        cv.tcapsule(S, P, 3.2, 2.8, m)
        cv.part()
        cv.tdisc(P, 4.0, m, bias=0.1)

        def Q(u, v):
            return (P[0] + d[0] * u + n[0] * v, P[1] + d[1] * u + n[1] * v)
        # housing
        cv.part()
        cv.tpoly([Q(-3, -5.8), Q(10, -5.4), Q(12, -3.0), Q(12, 3.0), Q(10, 5.4), Q(-3, 5.8)], hm, bevel=1.8,
                 base=0.58)
        cv.part(line=False)
        rm = AMB if not dead else AMB_OFF
        ch = max(0.0, -p.arm) + (1.0 if p.recoil > 2 else 0.0)
        cv.tline(Q(0, -2.6), Q(8.5, -2.6), rm, inten=0.5 + 0.45 * min(1.0, ch + 0.3 * p.eye), decal=True)
        cv.tline(Q(0, 2.4), Q(8.5, 2.4), hm, inten=0.2, decal=True)
        # barrel + muzzle
        cv.part()
        cv.tcapsule(Q(10, 0), Q(22.5, 0), 3.4, 2.8, m)
        cv.part()
        cv.tcapsule(Q(21.5, 0), Q(23.5, 0), 4.1, 4.1, m, bias=0.08)
        cv.part(line=False)
        cv.tdisc(Q(24.0, 0), 2.1, SOCKET, flat=0.2)
        if not dead and ch > 0.05:
            cv.tdisc(Q(24.0, 0), 0.9 + 1.1 * min(1.0, ch), AMB, flat=0.4 + 0.5 * min(1.0, ch))
        for u in (14.0, 17.5):
            cv.tline(Q(u, -3.0), Q(u, 3.0), m, inten=0.25, decal=True)

    # ---------------------------------------------------------------- effects
    def fx(self, clip, f, i, p):
        getattr(self, "fx_" + clip, lambda f, i, p: None)(f, i, p)

    def fx_move(self, f, i, p):
        rng = np.random.default_rng(self.seed + 10 + i)
        for k in range(5):
            y = int(rng.integers(30, 84))
            x0 = int(rng.integers(2, 18))
            streak(f, (x0, y), (x0 + int(rng.integers(4, 10)), y), C("#2e4a73"))

    def fx_roar(self, f, i, p):
        if p.lid > 1.1:
            ex, ey = self.eye_pos(p)
            for k, r in enumerate((12 + (i % 2) * 2, 18 + (i % 2) * 2, 24 + (i % 2) * 2)):
                ring(f, ex, ey, r, REDS[2 + min(k, 2)], -1.1, 1.1, dotted=0 if k < 2 else 2)
            motes(f, CX, CY, 10, 30, 44, EMBERS, seed=self.seed + 20 + i)

    def fx_telegraph_beam(self, f, i, p):
        ex, ey = self.eye_pos(p)
        t = (i + 1) / 6
        motes(f, ex, ey, 8 + 4 * i, 8, 36 - 14 * t, REDS[:4], seed=self.seed + 100 + i, inward=0.1 * t, big=0.2)
        if i >= 1:
            ring(f, ex, ey, 24 - 2.5 * i, REDS[2], -PI, PI, dotted=2)
        if i >= 2:
            ring(f, ex, ey, 14.5 - 1.2 * (i - 2), REDS[3] if i < 4 else REDS[2], -PI, PI, only_empty=False)
        if i >= 3:
            # red targeting line towards the right edge
            for x in range(int(ex) + 14, W, 2 if i < 5 else 1):
                put(f, x, int(round(ey)), REDS[3] if i < 5 else REDS[2])
        if i == 5:
            FX.sparkle(f, int(ex), int(ey), 7, REDS[0], REDS[2])

    def fx_beam(self, f, i, p):
        ex, ey = self.eye_pos(p)
        ex, ey = int(round(ex)), int(round(ey))
        hw = [4, 5, 4, 3][i]
        for x in range(ex + 4, W):
            wob = 1 if ((x + i * 3) // 6) % 2 else 0
            for dy in range(-hw - 2, hw + 3):
                y = ey + dy
                if not (0 <= y < H):
                    continue
                d = abs(dy)
                if d <= hw - 3:
                    col = REDS[0]
                elif d <= hw - 2:
                    col = REDS[1]
                elif d <= hw - 1:
                    col = REDS[2]
                elif d <= hw:
                    col = REDS[3]
                elif d <= hw + 1 + wob:
                    col = REDS[4] if (x + i) % 2 else None
                else:
                    col = None
                if col is not None:
                    f[y, x] = col
        FX.burst(f, ex + 4, ey, 7 + (i % 2), REDS[:4], seed=self.seed + 110 + i, jag=0.35)
        rng = np.random.default_rng(self.seed + 120 + i)
        for _ in range(12):
            x = int(rng.integers(ex + 10, W))
            y = ey + int(rng.integers(-hw - 7, hw + 8))
            if 0 <= y < H and f[y, x, 3] == 0:
                f[y, x] = REDS[int(rng.integers(1, 4))]

    def fx_telegraph_volley(self, f, i, p):
        t = (i + 1) / 5
        for k, (mx, my) in enumerate(self.muzzles(p)):
            if i >= 1:
                ring(f, mx + 1, my, 7 - 1.2 * i, AMBERS[1 + (i % 2)], -PI, PI, dotted=2 if i < 3 else 0)
            motes(f, mx, my, 3 + 2 * i, 4, 14 - 4 * t, AMBERS[:3], seed=self.seed + 200 + 10 * k + i, inward=0.1)
            if i == 4:
                FX.sparkle(f, int(mx + 1), int(my), 4, AMBERS[0], AMBERS[2])

    def fx_volley(self, f, i, p):
        ms = self.muzzles(p)
        if i == 0:
            for (mx, my) in ms:
                glow_orb(f, mx + 1, my, 2.5, AMBERS[:3])
        if i == 1:
            for k, (mx, my) in enumerate(ms):
                FX.burst(f, mx + 2, my, 5, [C("#fff4d6"), AMBER[2], AMBER[1], C("#ff8a3d")], seed=self.seed + 300 + k,
                         jag=0.4, rays=4)
                for j in range(3):
                    bx = mx + 9 + 7 * j
                    by = my + (k * 2 - 1) * j * 0.8
                    if bx < W - 2:
                        streak(f, (bx - 5, by), (bx, by), C("#ff8a3d"), only_empty=False)
                        glow_orb(f, bx + 1, by, 1.8, [C("#fff4d6"), AMBER[2], C("#ff8a3d")])
        if i == 2:
            for k, (mx, my) in enumerate(ms):
                FX.burst(f, mx + 3, my - 1, 3, [STONE[4], STONE[3], STONE[2]], seed=self.seed + 310 + k, jag=0.5)
                for j in range(2):
                    bx = mx + 22 + 9 * j
                    if bx < W - 2:
                        glow_orb(f, bx, my + (k * 2 - 1) * (2 + j), 1.6, [C("#fff4d6"), AMBER[2], C("#ff8a3d")])
        if i == 3:
            for k, (mx, my) in enumerate(ms):
                motes(f, mx + 4, my - 3, 4, 1, 6, [STONE[3], STONE[2]], seed=self.seed + 320 + k)

    def fx_telegraph_slam(self, f, i, p):
        cx, cy = CX + p.dx, CY + p.dy
        # warning glow under the plate ram
        for k in range(1 + i):
            yy = cy + 30 + k * 2.5
            w = 10 + 3 * k
            streak(f, (cx - w, yy), (cx + w, yy), AMBERS[min(3, 1 + k // 2)])
        motes(f, cx, cy + 26, 4 + 3 * i, 6, 22, AMBERS[:3], seed=self.seed + 400 + i, arc=(0.2, PI - 0.2))
        if i == 4:
            FX.sparkle(f, int(cx), int(cy + 30), 5, AMBERS[0], AMBERS[2])

    def fx_slam(self, f, i, p):
        cx = CX + p.dx
        if i in (0, 1):
            for k in range(6):
                x = cx - 20 + k * 8
                streak(f, (x, 4 + (k % 2) * 3), (x, 22 + (k % 3) * 4), C("#2e4a73"))
        if i == 2:
            dust_cloud(f, cx - 24, 81, 12, DUST, seed=self.seed + 500, n=5)
            dust_cloud(f, cx + 24, 81, 12, DUST, seed=self.seed + 501, n=5)
            FX.burst(f, cx, 82, 9, AMBERS, seed=self.seed + 502, jag=0.4, rays=8)
            ring(f, cx, 82, 30, AMBERS[2], -PI, 0.0, dotted=2)
            for x in range(int(cx - 44), int(cx + 45)):
                if 0 <= x < W and (x % 5) < 3:
                    put(f, x, 81, AMBER[1] if abs(x - cx) < 30 else STONE[3])
            for (x, y, s) in ((cx - 30, 70, 2), (cx + 32, 72, 2), (cx - 40, 76, 1), (cx + 42, 74, 1), (cx + 16, 66, 1),
                              (cx - 14, 64, 1)):
                FX.chunk(f, x, y, s, STONE[3], STONE[1])
        if i == 3:
            dust_cloud(f, cx - 26, 81, 7, DUST[1:], seed=self.seed + 510, n=4)
            dust_cloud(f, cx + 26, 81, 7, DUST[1:], seed=self.seed + 511, n=4)
            for (x, y, s) in ((cx - 36, 62, 1), (cx + 38, 64, 2), (cx + 46, 72, 1)):
                FX.chunk(f, x, y, s, STONE[3], STONE[1])
        if i == 4:
            dust_cloud(f, cx - 30, 81, 4, DUST[2:], seed=self.seed + 520, n=3)
            dust_cloud(f, cx + 30, 81, 4, DUST[2:], seed=self.seed + 521, n=3)

    def _drone(self, f, x, y, a=0.0):
        """Small sentinel drone being launched (stone shell, amber eye, outline)."""
        cv = TCanvas(W, H)
        cv.part()
        cv.disc((x, y), 4.2, STONE6)
        cv.ellipse((x, y + 2.0), 3.6, 1.8, METAL, decal=True)
        cv.ellipse((x - 1.2, y - 2.4), 2.6, 1.2, MOSS, decal=True)
        cv.part(line=False)
        cv.disc((x + 1.6, y - 0.2), 1.6, EYE, flat=0.85)
        img = cv.render()
        sel = img[:, :, 3] > 0
        f[sel] = img[sel]

    def fx_summon(self, f, i, p):
        hatches = [self.W(p, math.cos(a) * 20, math.sin(a) * 20) for a, _ in HATCHES]
        targets = [(CX - 30, 72), (CX + 26, 76)]
        if i == 1:
            for (hx, hy) in hatches:
                FX.burst(f, hx, hy, 4, AMBERS[:3], seed=self.seed + 600 + int(hx), jag=0.4)
        if i in (2, 3, 4):
            t = (i - 1) / 3.0
            for k, ((hx, hy), (tx, ty)) in enumerate(zip(hatches, targets)):
                ang = math.atan2(hy - CY, hx - CX)
                ox, oy = hx + math.cos(ang) * 12 * t, hy + math.sin(ang) * 12 * t
                x = ox + (tx - ox) * t * t
                y = oy + (ty - oy) * t * t
                streak(f, (hx, hy), (x, y), C("#c8741f"), only_empty=False)
                streak(f, ((hx + x) / 2, (hy + y) / 2), (x, y), C("#ffb347"), only_empty=False)
                self._drone(f, x, y)
        if i >= 2:
            for (hx, hy) in hatches:
                motes(f, hx, hy, 5, 3, 10, AMBERS[:3], seed=self.seed + 610 + i + int(hy))

    def fx_death(self, f, i, p):
        rng = np.random.default_rng(self.seed + 700 + i)
        if i <= 2:
            ex, ey = self.eye_pos(p)
            FX.sparkle(f, int(ex), int(ey), 3 + 2 * i, REDS[0], REDS[2])
            for k in range(2 + i):
                x = CX + (rng.random() - 0.5) * 40
                y = CY + (rng.random() - 0.5) * 40
                FX.sparkle(f, int(x), int(y), 2, AMBERS[0], AMBERS[2])
        if 3 <= i <= 6:
            cx, cy = CX + p.dx, CY + p.dy
            r = [12, 16, 11, 6][i - 3]
            FX.burst(f, cx + 4, cy - 2, r, [C("#fff4d6"), C("#ffd98a"), C("#ff8a3d"), C("#ff4d6d"), C("#a3123a")],
                     seed=self.seed + 710 + i, jag=0.45, rays=6 if i < 5 else 0)
            for k in range(4 + 2 * (i - 3)):
                a = rng.random() * 2 * PI
                d = 14 + rng.random() * 26 + 6 * (i - 3)
                FX.chunk(f, cx + math.cos(a) * d, cy + math.sin(a) * d + 4 * (i - 3), 1 + int(rng.random() * 2.2),
                         STONE[3], STONE[1])
            motes(f, cx, cy, 14, 10, 40, EMBERS, seed=self.seed + 720 + i)

    def residue(self, t):
        cv = TCanvas(W, H)
        sink = (1 - t) * 4
        rocks = [(56, 78, 13, 5.0, STONE6), (44, 79, 7, 3.4, PLATE), (68, 79, 8, 3.6, PLATE_DK),
                 (52, 74, 6, 3.4, METAL_DK), (62, 75, 5, 3.0, STONE6), (34, 80, 4, 2.2, PLATE_DK),
                 (78, 80, 5, 2.4, PLATE)]
        for i, (x, y, rx, ry, m) in enumerate(rocks):
            cv.part()
            cv.ellipse((x, y + sink), rx * (0.7 + 0.3 * t), ry * max(0.5, t), m, ang=0.12 * ((i % 3) - 1))
        cv.part(line=False)
        cv.px_set(58, 74, AMB_OFF, 0.5)
        return cv.render()


BOSS = Sentinel()
