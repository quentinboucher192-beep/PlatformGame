"""Forge Core (Forgeron Primordial, Pyralis) — core rig: a molten forge heart
held in a gyroscope cage of iron rings.  Riveted top cap with a smoking
chimney, a keel with dangling chains, two jointed crucible arms carrying
molten metal, and three heavy riveted plates on a tilted orbit (they pass
behind and in front of the heart)."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, glow_orb, mat, motes, ring, streak
from boss_a_core import CX, CY, H, W
from raster import TCanvas, ik2, put

PI = math.pi
OL = C("#14090b")

IRON = mat("fc_iron", ["#a8785e", "#7a4e3a", "#55342a", "#371f1c", "#221113"], outline=OL)
IRON_DK = mat("fc_iron_dk", ["#7a4e3a", "#55342a", "#371f1c", "#241315", "#170a0c"], outline=OL)
RING = mat("fc_ring", ["#b07e60", "#80523c", "#58362a", "#3a201c", "#221113"], outline=OL)
RING_DK = mat("fc_ring_dk", ["#80523c", "#5a3628", "#3a2020", "#241315"], outline=OL)
RUST = mat("fc_rust", ["#e0a070", "#d08a5a", "#a0583a", "#6e3b2a"], outline=OL)
LAVA = mat("fc_lava", ["#fff1c2", "#ffc56b", "#ff8a3d", "#ff4d1e", "#b3201a"], emissive=True, outline=OL)
LAVA_NO = mat("fc_lava_no", ["#fff1c2", "#ffc56b", "#ff8a3d", "#ff4d1e", "#b3201a"], emissive=True, outline=None)
CRUST = mat("fc_crust", ["#5a2b22", "#3a1d1c", "#241215", "#14090b"], outline=OL)
COLD = mat("fc_cold", ["#5a2b22", "#3a1d1c", "#241215", "#14090b"], outline=OL)
OBSID = mat("fc_obsidian", ["#c88ae8", "#9b4dca", "#5b2a6e", "#2e1238"], outline=OL)
SOOT = mat("fc_soot", ["#3a1d1c", "#241215", "#14090b"], outline=OL)
CHAIN = mat("fc_chain", ["#b08a70", "#7a5644", "#4e3226", "#2e1a16"], outline=OL)

FIRE = [C("#fff1c2"), C("#ffc56b"), C("#ff8a3d"), C("#ff4d1e"), C("#b3201a")]
EMBER = [C("#ffc56b"), C("#ff8a3d"), C("#ff4d1e"), C("#b3201a")]
SMOKE = [C("#5a2b22"), C("#3a1d1c"), C("#241215")]
ASH = [C("#6a4a40"), C("#4a3430"), C("#2e2020")]


def rot2(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


def lerp2(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


class Forge:
    id = "forge_core"
    seed = 4400
    rot_step = (2 * PI / 3) / 6     # 3 plates: a third of a turn per 6-frame loop is seamless
    spin_step = PI / 6              # gyroscope rings (180 deg symmetric) half turn per loop
    wither = C("#3a1d1c")
    hot = C("#ff8a3d")
    split_angle = 0.35
    dissolve_cols = [C("#ffc56b"), C("#ff8a3d"), C("#ff4d1e"), C("#b3201a")]
    HEART_R = 12.5
    RING_R = 19.0

    # ------------------------------------------------------------ positions
    def W(self, p, x, y):
        v = rot2((x, y), p.tilt)
        c = p.c
        return (c[0] + v[0], c[1] + v[1])

    def heart_pos(self, p):
        return self.W(p, 0.0, 0.0)

    def eye_pos(self, p):
        return self.W(p, self.HEART_R - 1.0, 0.0)

    def hand(self, p, side):
        rest = (side * 29.0, -1.0)
        back = (side * 17.0, -21.0)
        fling = (side * 33.0, -13.0)
        raised = (side * 23.0, -32.0)
        down = (side * 27.0, 8.5)
        if p.slam > 0:
            if p.arm >= 0:
                tgt = raised
            else:
                tgt = lerp2(raised, down, min(1.0, -p.arm))
            return lerp2(rest, tgt, min(1.0, p.slam))
        if p.arm < 0:
            return lerp2(rest, back, min(1.0, -p.arm))
        return lerp2(rest, fling, min(1.0, p.arm))

    def crucible_tilt(self, p, side):
        if p.slam > 0:
            return 0.0
        return 0.95 * max(0.0, p.arm) - 0.25 * max(0.0, -p.arm)

    def crucible_mouths(self, p):
        """Molten surface centre of each crucible (near first) in frame pixels."""
        out = []
        for side in (1, -1):
            hx, hy = self.hand(p, side)
            tl = self.crucible_tilt(p, side)
            m = rot2((0.0, 6.0), tl)
            out.append(self.W(p, hx + m[0], hy + m[1] - 0.5))
        return out

    # ------------------------------------------------------------------ draw
    def draw(self, p, layer=None):
        c = p.c
        cv = TCanvas(W, H, rot=p.tilt, center=c)
        L = lambda x, y: (c[0] + x, c[1] + y)  # noqa: E731
        dead = p.dead >= 0.5
        plates = self._plate_specs(p)
        if layer == "plates":
            for sp in plates:
                self._plate(cv, p, L, sp, dead)
            return cv.render()
        self._chains(cv, p, L, back=True)
        self._arm(cv, p, L, -1, dead)
        if layer is None:
            for sp in plates:
                if sp[3] < 0:
                    self._plate(cv, p, L, sp, dead)
        self._rings(cv, p, L, back=True, dead=dead)
        self._heart(cv, p, L, dead)
        self._rings(cv, p, L, back=False, dead=dead)
        self._caps(cv, p, L, dead)
        if layer is None:
            for sp in plates:
                if sp[3] >= 0:
                    self._plate(cv, p, L, sp, dead)
        self._chains(cv, p, L, back=False)
        self._arm(cv, p, L, 1, dead)
        return cv.render()

    # chains ---------------------------------------------------------------
    def _links(self, cv, L, pts, m):
        for k, q in enumerate(pts):
            cv.part()
            if k % 2 == 0:
                cv.tellipse(L(*q), 1.5, 1.5, m)
                cv.part(line=False)
                cv.tpx(L(q[0], q[1]), SOOT, inten=0.3, decal=True)
            else:
                cv.tellipse(L(*q), 1.1, 1.1, m, bias=-0.12)

    def _chains(self, cv, p, L, back):
        side = -1 if back else 1
        m = CHAIN if not back else RING_DK
        # swag from the keel to the crucible
        a = (side * 8.0, 21.0)
        hd = self.hand(p, side)
        tl = self.crucible_tilt(p, side) * (1 if side > 0 else 0.7)
        bq = rot2((side * -4.5, 12.0), tl)
        b = (hd[0] + bq[0], hd[1] + bq[1])
        dist = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(3, int(dist / 2.6))
        sag = 6.0 + 0.4 * p.wobble * side
        pts = []
        for k in range(n + 1):
            t = k / n
            pts.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t + sag * 4 * t * (1 - t)))
        self._links(cv, L, pts, m)
        if back:
            return
        # short central chain with a weight
        sway = -0.8 * p.wobble - 6.0 * p.tilt
        ln = 12.0 * (1.0 - 0.3 * min(1.0, p.slam))
        pts = [(0.5 + sway * (k * 2.6 / ln) ** 2, 24.5 + k * 2.6) for k in range(int(ln / 2.6) + 1)]
        self._links(cv, L, pts, CHAIN)
        e = pts[-1]
        cv.part()
        cv.tpoly([L(e[0] - 3.5, e[1] + 1.5), L(e[0] + 3.5, e[1] + 1.5), L(e[0] + 2.0, e[1] + 4.0),
                  L(e[0] + 4.0, e[1] + 6.5), L(e[0] - 4.0, e[1] + 6.5), L(e[0] - 2.0, e[1] + 4.0)], IRON,
                 bevel=1.0, base=0.58)

    # orbit plates -----------------------------------------------------------
    def _plate_specs(self, p):
        out = []
        for k in range(3):
            th = p.rot + k * 2 * PI / 3 + 0.3
            if p.open > 0:
                # plates slide away from the front (where the beam leaves)
                th = th + 0.0
            rx = 34.0 + p.prad
            ry = 8.5
            x = math.cos(th) * rx
            y = math.sin(th) * ry - math.cos(th) * 3.5
            depth = math.sin(th)
            if p.slam > 0:
                ts = PI / 2 + (k - 1) * 0.9
                x = x + (math.cos(ts) * 16 - x) * min(1.0, p.slam)
                y = y + (14.0 - y) * min(1.0, p.slam)
                depth = 1.0
            sc = p.scatter
            if sc > 0:
                x += math.cos(th) * 30 * sc + (k - 1) * 6 * sc
                y += -12 * sc + 50 * sc * sc
            out.append((k, x, y, depth, th))
        return sorted(out, key=lambda s: s[3])

    def _plate(self, cv, p, L, sp, dead):
        k, x, y, depth, th = sp
        face = abs(math.sin(th)) if p.slam <= 0 else 0.9
        hw = 2.4 + 5.4 * face
        hh = 8.5
        spin = p.scatter * (1.3 if k % 2 else -1.1)
        m = IRON if depth >= 0 else IRON_DK

        def P(u, v):
            q = rot2((u, v), spin)
            return L(x + q[0], y + q[1])
        cv.part()
        cv.tpoly([P(-hw, -hh + 2), P(-hw + 2, -hh), P(hw - 2, -hh), P(hw, -hh + 2), P(hw, hh - 3), P(0, hh + 1.5),
                  P(-hw, hh - 3)], m, bevel=1.5, base=0.56)
        cv.part(line=False)
        if face > 0.35:
            # rivets + glowing rune slit
            for (u, v) in ((-hw + 1.6, -hh + 2.2), (hw - 1.6, -hh + 2.2), (-hw + 1.6, hh - 3.6), (hw - 1.6, hh - 3.6)):
                cv.tpx(P(u, v), RUST, inten=0.9, decal=True)
            gm = LAVA if not dead else COLD
            gi = 0.45 + 0.4 * min(1.0, p.eye)
            cv.tline(P(0, -hh + 4.0), P(0, hh - 5.0), gm, inten=gi, decal=True)
            if hw > 5:
                cv.tpx(P(-2.5, hh - 4.0), gm, inten=gi, decal=True)
                cv.tpx(P(2.5, hh - 4.0), gm, inten=gi, decal=True)
                cv.tline(P(-hw + 1, -hh + 4.5), P(hw - 1, -hh + 4.5), IRON_DK, inten=0.15, decal=True)
        else:
            cv.tline(P(0, -hh + 1), P(0, hh - 2), RUST, inten=0.6, decal=True)

    # rings -------------------------------------------------------------------
    def _ring_pts(self, p, which):
        R = self.RING_R
        pts = []
        n = 40
        for i in range(n + 1):
            u = 2 * PI * i / n
            if which == 0:      # meridian ring spinning about the vertical axis
                ph = p.spin
                x, y, z = R * math.cos(u) * math.cos(ph), R * math.sin(u), R * math.cos(u) * math.sin(ph)
            else:               # tilted equatorial ring wobbling about the horizontal axis
                ps = 1.15 + 0.35 * math.sin(p.spin)
                x, y, z = R * 1.05 * math.cos(u), R * math.sin(u) * math.cos(ps), R * math.sin(u) * math.sin(ps)
                x, y = rot2((x, y), -0.22)
            pts.append((x, y, z))
        return pts

    def _rings(self, cv, p, L, back, dead):
        if p.scatter > 0.3:
            return
        for which in (0, 1):
            pts = self._ring_pts(p, which)
            m = RING if not back else RING_DK
            cv.part()
            seg = []
            for a, b in zip(pts[:-1], pts[1:]):
                zf = (a[2] + b[2]) / 2
                if (zf < 0) == back:
                    seg.append((a, b))
            for a, b in seg:
                cv.tcapsule(L(a[0], a[1]), L(b[0], b[1]), 2.2, 2.2, m)
            if not back:
                cv.part(line=False)
                for i, q in enumerate(pts[:-1]):
                    if q[2] >= 0 and i % 5 == 0:
                        cv.tpx(L(q[0], q[1]), RUST, inten=0.95, decal=True)

    # heart -------------------------------------------------------------------
    def _heart(self, cv, p, L, dead):
        r = self.HEART_R + 0.8 * min(1.0, p.open) + 0.6 * max(0.0, p.eye - 1.2)
        cv.part()
        if dead:
            cv.tdisc(L(0, 0), r, COLD)
            cv.part(line=False)
            for (x, y, rr) in ((-4, -3, 3.0), (3, 4, 2.5)):
                cv.tdisc(L(x, y), rr, CRUST, decal=True)
            return
        cv.tdisc(L(0, 0), r, LAVA, bias=-0.42 + 0.3 * p.eye)
        # cooling crust plates on the surface (they melt away when charging)
        cv.part(line=False)
        crust = max(0.0, 1.0 - 0.6 * max(0.0, p.eye - 0.6) - 0.8 * p.open)
        if crust > 0.1:
            rs = np.random.default_rng(self.seed + 3)
            for k in range(7):
                a = k * 0.9 + 0.2 + p.spin * 0.15
                d = r * (0.62 + 0.25 * rs.random())
                cv.tellipse(L(math.cos(a) * d, math.sin(a) * d), 4.4 * crust, 2.8 * crust, CRUST,
                            ang=a + PI / 2, decal=True, bias=0.05)
        cv.tdisc(L(-1.5, -1.5), 2.6 + 1.8 * min(1.6, p.eye), LAVA, flat=min(1.0, 0.72 + 0.2 * p.eye), decal=True)
        cv.tpx(L(-3.0, -3.0), LAVA, inten=1.0, decal=True)
        if p.cracks:
            for (a, b) in (((-6, -8), (-2, -2)), ((5, 2), (9, 7)), ((-9, 4), (-4, 6))):
                cv.tline(L(*a), L(*b), CRUST, inten=0.1, decal=True)

    # caps / chimney / keel ----------------------------------------------------
    def _caps(self, cv, p, L, dead):
        # top cap (riveted dome) + obsidian spikes + chimney
        cv.part()
        cv.tchain([L(-3, -26), L(-2.5, -33)], [3.2, 3.6], IRON_DK)
        cv.part()
        cv.tcapsule(L(-2.5, -33.5), L(-2.5, -34.5), 4.4, 4.4, IRON)
        cv.part(line=False)
        cv.tellipse(L(-2.5, -35.2), 2.6, 1.0, LAVA if not dead else SOOT, flat=0.4 + 0.3 * min(1.0, p.eye))
        for (x, a, ln, wd) in ((-10, -2.35, 11, 2.2), (9, -0.8, 12, 2.2), (-6, -2.0, 8, 1.8), (5, -1.15, 8, 1.8)):
            b = L(x, -21)
            tip = (b[0] + math.cos(a) * ln, b[1] + math.sin(a) * ln)
            nx, ny = -math.sin(a) * wd, math.cos(a) * wd
            cv.part()
            cv.tpoly([(b[0] + nx, b[1] + ny), tip, (b[0] - nx, b[1] - ny)], OBSID, bevel=0.9, base=0.62)
        cv.part()
        cv.tellipse(L(0, -19.0), 12.5, 5.5, IRON)
        cv.part(line=False)
        cv.tcapsule(L(-11, -17.5), L(11, -17.5), 1.2, 1.2, RUST, decal=True, bias=-0.1)
        for x in (-8, -3, 2, 7):
            cv.tpx(L(x, -21), RUST, inten=0.9, decal=True)
        # keel (bottom cap) with a glowing vent
        cv.part()
        cv.tpoly([L(-11, 16), L(11, 16), L(8, 21), L(3, 24), L(-3, 24), L(-8, 21)], IRON, bevel=1.6, base=0.55)
        cv.part(line=False)
        hg = p.hatch
        if hg > 0.05:
            cv.tellipse(L(0, 20.5), 4.0 + 2.5 * hg, 1.6 + 0.8 * hg, LAVA if not dead else SOOT, flat=0.5 + 0.4 * hg)
        else:
            cv.tline(L(-4, 20), L(4, 20), LAVA if not dead else SOOT, inten=0.4 + 0.3 * min(1.0, p.thrust), decal=True)
        for x in (-7, 7):
            cv.tpx(L(x, 17.5), RUST, inten=0.9, decal=True)
        # side hatches on the cap (summon): doors swing up revealing fire
        if hg > 0.05:
            for side in (-1, 1):
                cv.part()
                cv.tellipse(L(side * 8.5, -20.5), 2.8, 2.0, SOOT, flat=0.3)
                cv.part(line=False)
                cv.tellipse(L(side * 8.5, -20.5), 1.8 * hg + 0.4, 1.3 * hg + 0.3, LAVA, flat=0.6 + 0.3 * hg)
                cv.part()
                cv.tellipse(L(side * (8.5 + 3 * hg), -23.0 - 3 * hg), 3.0, 1.2, IRON, ang=side * (0.4 + hg))

    # crucible arms ------------------------------------------------------------
    def _arm(self, cv, p, L, side, dead):
        m = IRON if side > 0 else IRON_DK
        sh = (side * 9.5, -16.0)
        hd = self.hand(p, side)
        el = ik2(sh, hd, 13.0, 19.0, side * 1.0)
        el = (el[0], min(el[1], sh[1] + 2))
        cv.part()
        cv.tdisc(L(*sh), 4.2, m, bias=0.05)
        cv.part()
        cv.tcapsule(L(*sh), L(*el), 3.4, 3.0, m)
        cv.part()
        cv.tdisc(L(*el), 3.6, m, bias=0.12)
        cv.part(line=False)
        cv.tpx(L(el[0], el[1]), RUST, inten=0.9, decal=True)
        cv.part()
        cv.tcapsule(L(*el), L(*hd), 3.0, 2.4, m)
        # claw gripping the crucible bail
        cv.part()
        cv.tdisc(L(*hd), 2.8, m, bias=0.1)
        # crucible
        tl = self.crucible_tilt(p, side) * (1 if side > 0 else 0.7)

        def Q(u, v):
            q = rot2((u, v), tl)
            return L(hd[0] + q[0], hd[1] + q[1])
        cm = IRON if side > 0 else IRON_DK
        cv.part()
        cv.tchain([Q(-6.5, 5.5), Q(-4, 1), Q(0, -0.5), Q(4, 1), Q(6.5, 5.5)], [0.8] * 5, m)
        cv.part()
        bowl = [Q(-8.5, 5.0), Q(8.5, 5.0), Q(7.5, 9.5), Q(5.0, 12.5), Q(-5.0, 12.5), Q(-7.5, 9.5)]
        cv.tpoly(bowl, cm, bevel=1.8, base=0.5)
        cv.part(line=False)
        cv.tline(Q(-7.5, 8.0), Q(7.5, 8.0), cm, inten=0.25, decal=True)
        # molten surface + rim
        cv.part()
        boil = min(1.0, max(0.0, -p.arm) + 0.4 * max(0.0, p.eye - 0.8))
        lm = LAVA if not dead else SOOT
        cv.tellipse(Q(0, 5.0), 7.6, 2.0, lm, ang=p.tilt + tl, flat=0.5 + 0.3 * boil + 0.1 * (p.flick % 2))
        cv.part(line=False)
        cv.tline(Q(-6.5, 6.6), Q(6.5, 6.6), RUST, inten=0.85, decal=True)
        if not dead:
            cv.tpx(Q(-2 + (p.flick % 3) * 2, 4.6), LAVA, inten=1.0)
            # molten drip under the bowl
            dl = 1.5 + (p.flick % 3) * 0.8
            cv.tcapsule(Q(1.5, 12.5), Q(1.5, 12.5 + dl), 0.7, 0.5, LAVA_NO, flat=0.6)

    # ---------------------------------------------------------------- effects
    def fx(self, clip, f, i, p):
        getattr(self, "fx_" + clip, lambda f, i, p: None)(f, i, p)

    def _smoke(self, f, p, i, strength=1.0, seed=0):
        cx, cy = self.W(p, -2.5, -37.0)
        for k in range(int(4 * strength) + 2):
            age = ((i + k * 2) % 6) / 6.0
            x = cx + math.sin(k * 1.7 + i * 0.8) * (1.5 + 3 * age) - 2 * age
            y = cy - 3 - age * 22 * strength
            r = 1.2 + 2.2 * age
            col = SMOKE if age > 0.35 else [FIRE[2], SMOKE[0], SMOKE[1]]
            tmp = np.zeros_like(f)
            FX.burst(tmp, x, y, r, col, seed=self.seed + seed + k, jag=0.5)
            FX.under(f, tmp)
        motes(f, cx, cy - 8, int(5 * strength), 2, 14, EMBER, seed=self.seed + seed + 50 + i, arc=(-2.6, -0.5))

    def fx_idle(self, f, i, p):
        self._smoke(f, p, i, 1.0, seed=10)

    def fx_move(self, f, i, p):
        self._smoke(f, p, i, 0.8, seed=20)
        rng = np.random.default_rng(self.seed + 30 + i)
        for k in range(6):
            x = int(rng.integers(2, 26))
            y = int(rng.integers(30, 86))
            f[y, x] = EMBER[k % 4] if f[y, x, 3] == 0 else f[y, x]

    def fx_roar(self, f, i, p):
        self._smoke(f, p, i, 1.8 if p.eye > 1.2 else 1.0, seed=40)
        if p.eye > 1.2:
            hx, hy = self.heart_pos(p)
            for k, r in enumerate((24 + (i % 2) * 2, 31 + (i % 2) * 2)):
                ring(f, hx, hy, r, FIRE[2 + k], -1.2, 1.2, dotted=0 if k == 0 else 2)
            cx, cy = self.W(p, -2.5, -37.0)
            FX.burst(f, cx, cy - 4, 5 + (i % 2), FIRE[:4], seed=self.seed + 45 + i, jag=0.5, rays=3)
            motes(f, hx, hy, 14, 26, 44, EMBER, seed=self.seed + 47 + i)

    def fx_hurt(self, f, i, p):
        motes(f, CX, CY, 8, 14, 30, EMBER, seed=self.seed + 60)

    def fx_telegraph_beam(self, f, i, p):
        hx, hy = self.heart_pos(p)
        ex, ey = self.eye_pos(p)
        t = (i + 1) / 6
        motes(f, hx, hy, 10 + 5 * i, 18, 44 - 16 * t, FIRE[:4], seed=self.seed + 100 + i, inward=0.1 * t, big=0.25)
        if i >= 1:
            ring(f, hx, hy, 30 - 3 * i, FIRE[2], -PI, PI, dotted=2)
        if i >= 3:
            ring(f, hx, hy, 18 - 1.5 * (i - 3), FIRE[1], -PI, PI, only_empty=False)
            pass  # the aiming line is drawn by the game (one line only)
        self._smoke(f, p, i, 1.0 + t, seed=110)
        if i == 5:
            FX.sparkle(f, int(ex), int(ey), 7, FIRE[0], FIRE[2])

    def fx_beam(self, f, i, p):
        ex, ey = self.eye_pos(p)
        ex, ey = int(round(ex)), int(round(ey))
        # The beam itself is drawn by the game (textured beam, 2.8): only the muzzle here.
        FX.burst(f, ex + 2, ey, 8 + (i % 2), FIRE[:4], seed=self.seed + 140 + i, jag=0.4)
        self._smoke(f, p, i, 1.4, seed=150)

    def fx_telegraph_volley(self, f, i, p):
        for k, (mx, my) in enumerate(self.crucible_mouths(p)):
            motes(f, mx, my - 2, 3 + 3 * i, 2, 10 + 2 * i, EMBER, seed=self.seed + 200 + 10 * k + i, arc=(-PI, 0))
            if i >= 2 and k == 0:
                ring(f, mx, my, 9 - i, FIRE[2], -PI, 0.0, dotted=2)
                for b in range(i - 1):
                    glow_orb(f, mx - 3 + b * 3, my - 2 - b, 1.2, FIRE[1:3])
            if i == 4 and k == 0:
                FX.sparkle(f, int(mx), int(my - 4), 4, FIRE[0], FIRE[2])
        self._smoke(f, p, i, 1.0, seed=210)

    def fx_volley(self, f, i, p):
        mouths = self.crucible_mouths(p)
        nx, ny = mouths[0]
        if i == 1:
            FX.burst(f, nx + 4, ny - 2, 6, FIRE[:4], seed=self.seed + 300, jag=0.45, rays=3)
            for (dx, dy, r) in ((12, -8, 3.6), (20, -3, 3.0), (9, 2, 2.4)):
                x, y = nx + dx, ny + dy
                streak(f, (x - 9, y + 2), (x - 3, y + 1), FIRE[3], only_empty=False)
                glow_orb(f, x, y, r, FIRE[:4])
                put(f, x - 1, y + r + 1, FIRE[2])
            motes(f, nx + 8, ny - 4, 12, 3, 14, EMBER, seed=self.seed + 301)
        if i == 2:
            for (dx, dy, r) in ((24, -10, 2.8), (32, -2, 2.4), (20, 6, 2.0)):
                x, y = nx + dx, ny + dy
                if x < W - 2:
                    glow_orb(f, x, y, r, FIRE[:4])
                    streak(f, (x - 6, y + 1), (x - 3, y + 0.5), FIRE[3])
            motes(f, nx + 4, ny - 2, 8, 2, 10, SMOKE + EMBER[:1], seed=self.seed + 310)
        if i in (0, 3):
            motes(f, nx, ny - 2, 5, 2, 8, EMBER, seed=self.seed + 320 + i, arc=(-PI, 0))
        self._smoke(f, p, i, 1.0, seed=330)

    def fx_telegraph_slam(self, f, i, p):
        t = (i + 1) / 5
        for k, (mx, my) in enumerate(self.crucible_mouths(p)):
            motes(f, mx, my, 3 + 2 * i, 4, 12, EMBER, seed=self.seed + 400 + k * 10 + i)
            if i >= 2:
                ring(f, mx, my + 4, 12 - 1.5 * i, FIRE[2], -PI, PI, dotted=2)
        cx = CX + p.dx
        for k in range(1 + i):
            yy = 80 - k * 1.5
            for x in range(int(cx - 36), int(cx + 37), 2):
                if abs(x - cx) > 30 - 3 * k:
                    put_empty(f, x, yy, FIRE[min(4, 2 + k // 2)])
        self._smoke(f, p, i, 1.0 + t, seed=410)
        if i == 4:
            mx, my = self.crucible_mouths(p)[0]
            FX.sparkle(f, int(mx), int(my), 5, FIRE[0], FIRE[2])

    def fx_slam(self, f, i, p):
        cx = CX + p.dx
        if i in (0, 1):
            for side in (-1, 1):
                for k in range(3):
                    x = cx + side * (22 + k * 4)
                    streak(f, (x, 8 + k * 3), (x, 24 + k * 4), C("#5a2b22"))
        if i == 2:
            for side in (-1, 1):
                x = cx + side * 28
                FX.burst(f, x, 80, 8, FIRE[:4], seed=self.seed + 500 + side, jag=0.5, rays=5)
                dust_cloud(f, x + side * 10, 81, 9, ASH, seed=self.seed + 502 + side, n=4)
                for (dx, dy) in ((4, -10), (10, -6), (-2, -14), (14, -12)):
                    glow_orb(f, x + side * dx, 80 + dy, 1.4, FIRE[1:4])
            for x in range(int(cx - 48), int(cx + 49)):
                if 0 <= x < W and (x % 4) < 3:
                    put(f, x, 81, FIRE[2] if abs(abs(x - cx) - 28) < 12 else FIRE[4])
        if i == 3:
            for side in (-1, 1):
                x = cx + side * 30
                dust_cloud(f, x + side * 6, 81, 7, ASH, seed=self.seed + 510 + side, n=4)
                for (dx, dy) in ((10, -14), (18, -10), (4, -18)):
                    glow_orb(f, x + side * dx, 80 + dy, 1.2, FIRE[2:4])
                for xx in range(int(x - 8), int(x + 9)):
                    if (xx % 3) == 0:
                        put_empty(f, xx, 81, FIRE[3])
        if i == 4:
            for side in (-1, 1):
                dust_cloud(f, cx + side * 34, 81, 4, ASH[1:], seed=self.seed + 520 + side, n=3)
        self._smoke(f, p, i, 1.0, seed=530)

    def fx_summon(self, f, i, p):
        self._smoke(f, p, i, 1.0 + p.hatch, seed=600)
        hs = [self.W(p, s * 8.5, -22.0) for s in (-1, 1)]
        targets = [(CX - 28, 78), (CX + 28, 78)]
        if i == 1:
            for (hx, hy) in hs:
                FX.burst(f, hx, hy - 2, 4, FIRE[:3], seed=self.seed + 610 + int(hx), jag=0.4)
        if i in (2, 3, 4):
            t = (i - 1) / 3.0
            for (hx, hy), (tx, ty) in zip(hs, targets):
                side = 1 if tx > CX else -1
                x = hx + (tx - hx) * t
                y = hy - 22 * math.sin(PI * t) + (ty - hy) * t * t
                streak(f, (hx + (x - hx) * 0.4, hy + (y - hy) * 0.4 - 8 * t), (x, y), FIRE[3], only_empty=False)
                glow_orb(f, x, y, 3.0, FIRE[:4])
                put(f, x + side, y - 3, FIRE[1])
                if i == 4:
                    FX.burst(f, tx, ty, 6, FIRE[:4], seed=self.seed + 620 + int(tx), jag=0.5, rays=4)
        if i >= 4:
            for (tx, ty) in targets:
                motes(f, tx, ty - 4, 10, 2, 12, EMBER, seed=self.seed + 630 + int(tx) + i, arc=(-PI, 0))

    def fx_death(self, f, i, p):
        rng = np.random.default_rng(self.seed + 700 + i)
        if p is None:
            return
        hx, hy = self.heart_pos(p)
        if i <= 2:
            FX.sparkle(f, int(hx), int(hy), 3 + 2 * i, FIRE[0], FIRE[2])
            for k in range(2 + i):
                a = rng.random() * 2 * PI
                d = 16 + rng.random() * 14
                FX.sparkle(f, int(hx + math.cos(a) * d), int(hy + math.sin(a) * d), 2, FIRE[1], FIRE[3])
            self._smoke(f, p, i, 1.5, seed=710)
        if 3 <= i <= 6:
            r = [14, 18, 12, 7][i - 3]
            FX.burst(f, hx, hy, r, FIRE + [C("#5a2b22")], seed=self.seed + 720 + i, jag=0.45, rays=7 if i < 5 else 0)
            for k in range(5 + 2 * (i - 3)):
                a = rng.random() * 2 * PI
                d = 16 + rng.random() * 28 + 6 * (i - 3)
                x, y = hx + math.cos(a) * d, hy + math.sin(a) * d + 3 * (i - 3)
                if k % 2:
                    glow_orb(f, x, y, 1.4, FIRE[1:4])
                else:
                    FX.chunk(f, x, y, 1 + int(rng.random() * 2.2), C("#7a4e3a"), C("#371f1c"), outline=OL)
            motes(f, hx, hy, 16, 12, 44, EMBER, seed=self.seed + 730 + i)

    def residue(self, t):
        cv = TCanvas(W, H)
        sink = (1 - t) * 4
        parts = [(56, 78, 12, 4.6, COLD), (42, 79, 7, 3.2, IRON), (70, 79, 8, 3.4, IRON_DK), (50, 74, 5, 3.0, RUST),
                 (62, 75, 6, 3.2, IRON), (32, 80, 4, 2.0, IRON_DK), (80, 80, 5, 2.2, RUST)]
        for i, (x, y, rx, ry, m) in enumerate(parts):
            cv.part()
            cv.ellipse((x, y + sink), rx * (0.7 + 0.3 * t), ry * max(0.5, t), m, ang=0.12 * ((i % 3) - 1))
        cv.part(line=False)
        cv.px_set(55, 76, LAVA, 0.5)
        cv.px_set(58, 77, LAVA, 0.3)
        return cv.render()


def put_empty(f, x, y, c):
    x, y = int(round(x)), int(round(y))
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


BOSS = Forge()
