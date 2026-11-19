"""Aurorax, le Miroir boreal (Glacior sanctuary guardian, 2.19).

A spirit of the aurora frozen in a body of crystal: a stag-headed knight of glacier-blue ice
with great branching crystal antlers and a small crown, a green aurora heart in a faceted
cuirass, a cape of aurora light (green to violet) and a long crystal lance whose blade splits
light like a prism.  The engine draws its mirror doubles with this same sheet (translucent).

Frames 128x128, pivot [64, 128] (bottom-centre of a 48x76 collision box: x 40..88, y 52..128).
Clips: idle 6, walk 8 (footsteps f1, f5), move 8, telegraph 6 (lance raised, prism light),
attack 5 (fire f1, lance thrust), telegraph_special 6 (stag charge wind-up), special 4 (charge,
loop), summon 6 (lance planted: frost spreads, aurora surge), roar 8, hurt 2, death 14."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, glow_orb, mat, motes, shard, split_frame, streak
from boss_c_common import TAU, Pose, build_sheet, hline, outline_mask, over, shake, sound_rings, standard_clips
from raster import TCanvas, ik2, put, tint_toward

PI = math.pi
W = H = 128
PIVOT = [64, 128]
GY = 127
K = 1.3          # the rig is designed in a 112x112 space (ground row 111) and scaled up by K
GYD = 111


def M(q):
    """Design space -> frame pixels."""
    return (64.0 + (q[0] - 56.0) * K, GY - (GYD - q[1]) * K)


def MX(x):
    return 64.0 + (x - 56.0) * K

ICE = mat("ax_ice", ["#eefcff", "#b8ecfc", "#8cd2f2", "#62aae2", "#4480c6", "#2c5a9e"],
          th=[0.9, 0.76, 0.6, 0.42, 0.24])
ICE_DK = mat("ax_ice_dk", ["#a8e4fa", "#74bce8", "#4f8fcc", "#3466a8", "#22477e", "#163058"],
             th=[0.9, 0.76, 0.6, 0.42, 0.24])
ICE_DEAD = mat("ax_ice_dead", ["#b8c4d0", "#8e9cae", "#6a788c", "#4c586c", "#323c4e"])
FACET = mat("ax_facet", ["#ffffff", "#e0faff"], emissive=True, outline=None)
TIP = mat("ax_tip", ["#ffffff", "#e8fcff", "#b8fff0", "#9fe8ff"], emissive=True)
GEM = mat("ax_gem", ["#f4fff4", "#b4ffc0", "#78e68c", "#3cb860", "#1f7a48"], emissive=True,
          th=[0.9, 0.72, 0.5, 0.28])
GEM_DEAD = mat("ax_gem_dead", ["#7a8a80", "#56645c", "#3a4640"])
EYE = mat("ax_eye", ["#ffffff", "#c8ffd0", "#78e68c"], emissive=True)
HOOF = mat("ax_hoof", ["#5a70a0", "#3c4e7c", "#283658", "#1a2240"])
SLOT = mat("ax_slot", ["#1a2f58", "#0f1c3a"])
AURORA = mat("ax_aurora", ["#d8ffe0", "#9ef0a8", "#64d898", "#4cb8b4", "#7a84e8", "#a688f8"], emissive=True,
             outline=None, th=[0.92, 0.8, 0.66, 0.52, 0.36])
AURORA_DEAD = mat("ax_aurora_dead", ["#8aa0b4", "#6c8098", "#56667c"], outline=None)
PRISM = mat("ax_prism", ["#ffffff", "#fff6c8", "#b4ffc0", "#9fe8ff", "#c8a8ff"], emissive=True,
            th=[0.86, 0.68, 0.5, 0.3])
CRACK = mat("ax_crack", ["#ffffff", "#c8ffd0"], emissive=True, outline=None)

ICES = [C("#ffffff"), C("#c8f4ff"), C("#9fe8ff"), C("#6cc4f0")]
GREENS = [C("#f4fff4"), C("#b4ffc0"), C("#78e68c"), C("#3cb860")]
AUR = [C("#b4ffc0"), C("#78e68c"), C("#9fe8ff"), C("#b48cff")]
RAINBOW = [C("#ffffff"), C("#fff6a8"), C("#b4ffc0"), C("#9fe8ff"), C("#c8a8ff"), C("#ffb4e0")]
SNOW = [C("#ffffff"), C("#dff4ff"), C("#b8d8f0")]


class APose(Pose):
    DEFAULTS = dict(hip=(54.0, 77.0), lean=0.0, head_tilt=0.0, foot_f=(64.0, GYD), foot_b=(44.0, GYD),
                    hand_f=(72.0, 70.0), lance_a=-1.3, hand_b=(42.0, 70.0), gem=0.6, eye=0.8, antler=0.4,
                    cape_wave=0.0, cape_lift=0.0, prism=0.0, cracks=0, dead=0.0, flick=0, lance=True,
                    lance_grip=0.0)


class Aurorax:
    id = "aurorax"
    W, H, PIVOT = W, H, PIVOT
    seed = 21910
    IDLE_FPS, WALK_FPS, SPECIAL_FPS = 7, 9, 12
    WALK_EVENTS = [(1, "footstep"), (5, "footstep")]
    ATTACK_EVENTS = [(1, "fire")]

    # -------------------------------------------------------------- skeleton
    def frame(self, p):
        hx, hy = M(p.hip)
        c, s = math.cos(p.lean), math.sin(p.lean)

        def B(x, y):
            return (hx + K * (x * c - y * s), hy + K * (x * s + y * c))

        hc = B(6.0, -37.0)
        hl = p.lean + p.head_tilt
        hcs, hsn = math.cos(hl), math.sin(hl)

        def HD(x, y, k=K):
            return (hc[0] + k * (x * hcs - y * hsn), hc[1] + k * (x * hsn + y * hcs))
        return B, HD

    def lance_pts(self, p):
        d = (math.cos(p.lance_a), math.sin(p.lance_a))
        hx, hy = M(p.hand_f)
        g = p.lance_grip
        tip = (hx + d[0] * (26 - g) * K, hy + d[1] * (26 - g) * K)
        butt = (hx - d[0] * (16 + g) * K, hy - d[1] * (16 + g) * K)
        return butt, tip, d

    def tip_pos(self, p):
        return self.lance_pts(p)[1]

    def eye_pos(self, p):
        return self.frame(p)[1](4.5, -1.2)

    # ------------------------------------------------------------------ draw
    def draw(self, p):
        cv = TCanvas(W, H)
        B, HD = self.frame(p)
        dead = p.dead >= 0.5
        ice = ICE if not dead else ICE_DEAD
        ice_dk = ICE_DK if not dead else ICE_DEAD
        hand_f, hand_b = M(p.hand_f), M(p.hand_b)
        cv.tag = "cape"  # 2.76.2: the hit part of each pixel (raster.LABELS, tools/assetgen/hitparts.py)
        self._cape(cv, p, B, dead)
        # back leg, back arm, far antler
        cv.tag = "jambes"
        self._leg(cv, B(-4.0, 2.0), M(p.foot_b), ice_dk)
        sh_b = B(-6.0, -27.0)
        el = ik2(sh_b, hand_b, 11.0 * K, 11.0 * K, bend=1.0)
        cv.tag = "bras"
        cv.part()
        cv.capsule(sh_b, el, 3.8 * K, 3.2 * K, ice_dk)
        cv.capsule(el, hand_b, 3.2 * K, 2.7 * K, ice_dk)
        cv.part()
        cv.disc(hand_b, 3.0 * K, ice_dk, bias=0.05)
        cv.tag = "bois"
        self._antler(cv, p, HD, far=True, dead=dead)
        # torso
        cv.tag = "corps"
        cv.part()
        tv = [(-8, 0), (8, 0), (12, -10), (15, -19), (12, -28), (4, -32), (-6, -31), (-12, -25), (-14, -13)]
        torso = [B(*q) for q in tv]
        cv.poly(torso, ice, bevel=2.6, base=0.56)
        # faceted shading: a fan of flat facets around the heart, lit from the top-left
        cv.part(line=False)
        cx, cy = 3.0, -18.0
        for k in range(len(tv)):
            a, b = tv[k], tv[(k + 1) % len(tv)]
            mx, my = (a[0] + b[0]) / 2 - cx, (a[1] + b[1]) / 2 - cy
            ln = math.hypot(mx, my) or 1.0
            lit = (-0.6 * mx - 0.8 * my) / ln
            cv.poly([B(cx, cy), B(*a), B(*b)], ice, flat=0.5 + 0.3 * lit + 0.04 * (k % 2), decal=True)
        for q in tv:
            cv.line(B(cx, cy), B(*q), ice, inten=0.3, decal=True)
        if not dead:
            for a, b in ((B(-10, -24), B(-4, -29)), (B(10, -12), B(13, -17)), (B(-12, -14), B(-9, -20))):
                cv.line(a, b, FACET, inten=0.8, decal=True)
        # belt
        cv.part()
        cv.capsule(B(-8.5, -1.0), B(8.5, -1.0), 1.6 * K, 1.6 * K, ice_dk)
        # aurora heart
        g = B(4.0, -19.0)
        cv.tag = "cœur"
        cv.part()
        gm = GEM if not dead else GEM_DEAD
        r = 6.6 * K
        cv.poly([(g[0], g[1] - r), (g[0] + r * 0.68, g[1]), (g[0], g[1] + r), (g[0] - r * 0.68, g[1])], gm,
                flat=min(0.8, 0.4 + 0.3 * p.gem))
        cv.part(line=False)
        r2 = r * 0.5
        cv.poly([(g[0], g[1] - r2), (g[0] + r2 * 0.66, g[1]), (g[0], g[1] + r2), (g[0] - r2 * 0.66, g[1])], gm,
                flat=min(0.95, 0.62 + 0.25 * p.gem))
        if p.gem > 1.0 and not dead:
            cv.px_set(g[0] - 1, g[1] - r2 * 0.5, gm, inten=1.0)
        # tassets (crystal shards hanging from the waist)
        cv.tag = "corps"
        cv.part()
        for (x0, x1, y1) in ((-9, -3, 9), (-4, 3, 11), (2, 9, 9)):
            a, b = B(x0, 0), B(x1, 0)
            tip = B((x0 + x1) / 2 + 1, y1)
            cv.poly([a, b, tip], ice_dk, bevel=1.2, base=0.55)
        # front leg
        cv.tag = "jambes"
        self._leg(cv, B(5.0, 2.0), M(p.foot_f), ice)
        # neck + head
        cv.tag = "tête"
        cv.part()
        cv.capsule(B(3.0, -29.0), HD(-2.0, 3.0), 4.8 * K, 4.0 * K, ice_dk)
        self._head(cv, p, HD, ice, dead)
        cv.tag = "bois"
        self._antler(cv, p, HD, far=False, dead=dead)
        # front arm + lance
        sh_f = B(8.0, -25.0)
        cv.tag = "lance"
        if p.lance:
            butt, tip, d = self.lance_pts(p)
            cv.part()
            cv.capsule(butt, tip, 1.5, 1.3, ice_dk)
            self._blade(cv, p, tip, d, dead)
        el = ik2(sh_f, hand_f, 11.0 * K, 11.0 * K, bend=-1.0)
        cv.tag = "bras"
        cv.part()
        cv.capsule(sh_f, el, 4.2 * K, 3.5 * K, ice)
        cv.part()
        cv.capsule(el, hand_f, 3.5 * K, 3.0 * K, ice)
        cv.part()
        cv.disc(hand_f, 3.3 * K, ice, bias=0.05)
        # pauldron
        cv.tag = "épaulière"
        cv.part()
        pa = [B(1, -31), B(12, -30), B(16, -23), B(10, -19), B(4, -23)]
        cv.poly(pa, ice, bevel=1.8, base=0.66)
        if not dead:
            cv.part(line=False)
            cv.line(B(4, -30), B(13, -28), FACET, inten=0.8, decal=True)
        if p.cracks:
            cv.part(line=False)
            cm = CRACK if not dead else SLOT
            segs = [((4, -20), (0, -12)), ((0, -12), (3, -6)), ((-6, -26), (-9, -18))]
            if p.cracks > 1:
                segs += [((4, -20), (11, -24)), ((-2, -8), (-8, -4)), ((9, -14), (12, -8))]
            for a, b in segs:
                cv.line(B(*a), B(*b), cm, inten=0.8, decal=True)
        return cv.render()

    def _cape(self, cv, p, B, dead):
        wv, lf = p.cape_wave, p.cape_lift
        pts = [B(-3, -31), B(-11, -28), B(-18 - 2 * lf, -16 - 3 * lf), B(-24 - 5 * lf + wv * 0.5, -2 - 8 * lf),
               B(-29 - 7 * lf + wv, 11 - 12 * lf), B(-25 - 6 * lf + wv * 1.4, 8 - 10 * lf),
               B(-22 - 5 * lf + wv, 15 - 11 * lf), B(-17 - 4 * lf + wv * 0.6, 10 - 8 * lf),
               B(-13 - 2 * lf, 14 - 6 * lf), B(-9, 4 - 2 * lf), B(-5, -10)]
        cv.part(line=False)
        cv.poly(pts, AURORA if not dead else AURORA_DEAD, shade="grad", base=0.62 + 0.1 * p.gem)
        if not dead:
            for k in range(3):
                a = B(-7 - 4 * k, -26 + 2 * k)
                b = B(-15 - 5 * k - 4 * lf + wv * 0.7, 6 - 6 * lf + 2 * k)
                cv.line(a, b, AURORA, inten=0.82, decal=True)

    def _leg(self, cv, hip, foot, m):
        ankle = (foot[0], foot[1] - 4.0 * K)
        knee = ik2(hip, ankle, 14.5 * K, 14.0 * K, bend=1.0)
        cv.part()
        cv.capsule(hip, knee, 5.4 * K, 4.2 * K, m)
        cv.part()
        cv.capsule(knee, ankle, 4.0 * K, 3.0 * K, m)
        # knee guard
        cv.part()
        cv.poly([(knee[0] - 3.0, knee[1] - 3.0), (knee[0] + 5.5, knee[1] - 1.0), (knee[0] + 1.0, knee[1] + 4.0)], m,
                bevel=1.0, base=0.66)
        # hoof
        cv.tag = "sabots"  # 2.76.2: hit parts (raster.LABELS)
        cv.part()
        x, y = foot
        cv.poly([(x - 4.0 * K, y - 4.6 * K), (x + 3.4 * K, y - 4.6 * K), (x + 5.4 * K, y + 0.6),
                 (x - 4.2 * K, y + 0.6)], HOOF, bevel=1.2, base=0.6)
        cv.part(line=False)
        cv.line((x + 0.5, y - 2.0), (x + 1.5, y + 0.4), SLOT, inten=0.3, decal=True)

    def _head(self, cv, p, HD, ice, dead):
        cv.part()
        cv.poly([HD(-3, -3), HD(-12, -9), HD(-5, 0)], ice, bevel=1.0, base=0.55)
        cv.part()
        cv.ellipse(HD(0, 0), 6.8 * K, 5.6 * K, ice, ang=p.lean + p.head_tilt)
        cv.poly([HD(2, -4.2), HD(13.5, -1.8), HD(15, 1.2), HD(12.5, 3.9), HD(2, 5.0)], ice, bevel=1.6, base=0.62)
        cv.part(line=False)
        cv.disc(HD(14.0, 0.6), 1.5 * K, HOOF, flat=0.4)
        cv.line(HD(6, 2.8), HD(11.5, 3.0), SLOT, inten=0.2, decal=True)
        # eye slit (green, the mark of the true Aurorax)
        if not dead:
            e = p.eye
            cv.line(HD(2.6, -1.6), HD(6.4, -0.6), EYE, inten=min(1.0, 0.55 + 0.4 * e))
            cv.line(HD(3.0, -0.9), HD(6.0, -0.2), EYE, inten=min(1.0, 0.45 + 0.4 * e))
            cv.px_set(*HD(5.0, -1.0), EYE, inten=1.0)
        else:
            cv.line(HD(2.6, -1.0), HD(6.4, -0.6), SLOT, inten=0.2)
        # crown
        cv.part()
        for (x, h) in ((-3.0, 5.0), (0.5, 7.5), (4.0, 5.0)):
            cv.poly([HD(x - 1.8, -4.2), HD(x, -4.2 - h), HD(x + 1.8, -4.2)], TIP if not dead else ice, bevel=0.7,
                    base=0.7)

    def _antler(self, cv, p, HD, far, dead):
        """Crystal antlers, drawn in head space at 1:1 pixel scale (they sweep out more than up)."""
        m = (ICE_DK if far else ICE) if not dead else ICE_DEAD
        if far:
            beam = [(-3, -5), (-8, -11), (-13, -17), (-20, -21), (-28, -22)]
            tines = [((-8, -11), (-5, -21)), ((-13, -17), (-14, -28)), ((-20, -21), (-25, -30)), ((-24, -22), (-32, -18))]
        else:
            beam = [(2, -6), (5, -12), (6, -19), (10, -25), (17, -28)]
            tines = [((5, -12), (13, -14)), ((6, -19), (1, -28)), ((10, -25), (11, -34)), ((14, -27), (22, -32))]
        pts = [HD(*q, k=1.0) for q in beam]
        cv.part()
        cv.chain(pts, [2.9, 2.5, 2.1, 1.7, 1.2], m)
        for a, b in tines:
            cv.capsule(HD(*a, k=1.0), HD(*b, k=1.0), 1.9, 1.0, m)
        cv.part(line=False)
        if not dead:
            gl = min(1.0, 0.55 + 0.4 * p.antler)
            for k, (a, b) in enumerate(tines):
                q = HD(*b, k=1.0)
                cv.px_set(*q, TIP, inten=gl)
                if p.antler > 0.9 or (k + p.flick) % 2 == 0:
                    a2 = HD(*a, k=1.0)
                    cv.px_set(q[0] + (a2[0] - q[0]) * 0.25, q[1] + (a2[1] - q[1]) * 0.25, TIP, inten=gl * 0.8)
            cv.px_set(*pts[-1], TIP, inten=gl)
            for q0, q1 in zip(pts[:-1], pts[1:]):
                cv.line((q0[0] - 0.7, q0[1] - 0.5), (q1[0] - 0.7, q1[1] - 0.5), FACET, inten=0.8, decal=True)

    def _blade(self, cv, p, tip, d, dead):
        n = (-d[1], d[0])
        L, Wd = 13.0, 4.4

        def Q(u, v):
            return (tip[0] + d[0] * u + n[0] * v, tip[1] + d[1] * u + n[1] * v)
        cv.part()
        bm = PRISM if (p.prism > 0.3 and not dead) else (ICE if not dead else ICE_DEAD)
        cv.poly([Q(-L * 0.45, 0), Q(-L * 0.1, -Wd), Q(L * 0.55, 0), Q(-L * 0.1, Wd)], bm, bevel=1.2,
                base=0.62 + 0.3 * min(1.0, p.prism))
        cv.part()
        cv.poly([Q(-L * 0.55, -Wd - 1.8), Q(-L * 0.35, -0.8), Q(-L * 0.35, 0.8), Q(-L * 0.55, Wd + 1.8),
                 Q(-L * 0.7, 0)], ICE_DK if not dead else ICE_DEAD, bevel=0.8, base=0.6)
        if not dead:
            cv.part(line=False)
            cv.line(Q(-L * 0.35, 0), Q(L * 0.4, 0), FACET, inten=0.9, decal=True)

    # --------------------------------------------------------------- effects
    def aura_motes(self, f, i, n, seed, count=8):
        rng = np.random.default_rng(seed)
        for k in range(count):
            x0 = 34 + rng.random() * 64
            y0 = 40 + rng.random() * 80
            off = rng.random()
            t = (i / n + off) % 1.0
            x = int(x0 + 2 * math.sin(TAU * (t + off)))
            y = int(y0 - 16 * t)
            if 0.1 < t < 0.9 and 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0:
                f[y, x] = AUR[k % 4]

    def frost_spikes(self, f, x, n, h, seed, spread=6.0):
        cv = TCanvas(W, H)
        rng = np.random.default_rng(seed)
        for k in range(n):
            bx = x + (k - (n - 1) / 2) * spread + rng.random() * 2
            hh = h * (0.6 + 0.4 * rng.random())
            a = -PI / 2 + (rng.random() - 0.5) * 0.7
            tip = (bx + math.cos(a) * hh, GY + 0.5 + math.sin(a) * hh)
            cv.part()
            nx, ny = -math.sin(a) * 2.0, math.cos(a) * 2.0
            cv.poly([(bx + nx, GY + 1), tip, (bx - nx, GY + 1)], ICE, bevel=1.0, base=0.66)
        img = cv.render()
        img[GY + 1:] = 0
        over(f, img)
        return f

    # ================================================================== clips
    def idle(self):
        out = []
        for i in range(6):
            ph = TAU * i / 6
            b = math.sin(ph)
            p = APose(hip=(54.0, 77.0 + 0.6 * b), hand_f=(72.0, 70.0 + 0.6 * b), hand_b=(42.0, 70.0 + 0.6 * b),
                      head_tilt=0.03 * b, cape_wave=1.6 * math.sin(ph + 0.6), gem=0.6 + 0.25 * b, antler=0.4,
                      flick=i)
            f = self.draw(p)
            self.aura_motes(f, i, 6, self.seed + 1)
            out.append(f)
        return out

    def walk(self):
        out = []
        keys = [(70, 0, 40, 0, 0), (67, 0, 44, 4, 1), (62, 0, 50, 6, 0), (57, 0, 56, 3, -1),
                (52, 0, 60, 0, 0), (56, 4, 56, 0, 1), (62, 6, 50, 0, 0), (67, 3, 44, 0, -1)]
        for i, (fx_, fl, bx, bl, bob) in enumerate(keys):
            ph = TAU * i / 8
            p = APose(hip=(54.0 + 0.5 * math.sin(2 * ph), 77.0 + bob * 0.8), foot_f=(fx_, GYD - fl),
                      foot_b=(bx, GYD - bl), lean=0.05, hand_f=(72.0 - 2 * math.sin(ph), 70.0 + bob * 0.8),
                      hand_b=(42.0 + 2 * math.sin(ph), 70.0 + bob * 0.8), cape_wave=2.0 * math.sin(ph),
                      cape_lift=0.15, gem=0.65, antler=0.45, flick=i, head_tilt=0.03 * math.sin(2 * ph))
            f = self.draw(p)
            if i in (1, 5):
                x = (fx_ if i == 1 else bx)
                FX.burst(f, MX(x + 2), GY, 2.4, SNOW, seed=self.seed + 10 + i, jag=0.5)
            self.aura_motes(f, i, 8, self.seed + 2, count=5)
            out.append(f)
        return out

    def telegraph(self):
        """Raises the lance diagonally: light gathers into a prism at the blade."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            hand = (72.0, 70.0 - 18.0 * t)
            p = APose(hand_f=hand, lance_a=-1.3 + 0.45 * t, lean=-0.08 * t, head_tilt=-0.1 * t, gem=0.6 + 0.9 * t,
                      eye=0.8 + 0.6 * t, antler=0.4 + 0.8 * t, prism=t, cape_wave=-1.5 * t, cape_lift=0.3 * t,
                      hand_b=(40.0 - 2 * t, 70.0 - 6 * t), foot_f=(66.0, GYD), foot_b=(42.0, GYD), flick=i)
            f = self.draw(p)
            tx, ty = self.tip_pos(p)
            motes(f, tx, ty, 4 + 3 * i, 4, 20 - 9 * t, RAINBOW, seed=self.seed + 30 + i, inward=0.25 * t, big=0.15)
            if i >= 2:
                from boss_a_common import ring
                ring(f, tx, ty, 9 - 1.2 * i, RAINBOW[2 + (i % 3)], dotted=2)
            if i >= 4:
                FX.sparkle(f, int(tx), int(ty), 3 + (i - 4) * 3, RAINBOW[0], RAINBOW[3])
            out.append(f)
        return out

    def attack(self):
        """Thrusts the lance: a prismatic flash bursts from the blade (projectiles/beam spawn on f1)."""
        keys = [  # hand, lance angle, lean, prism
            ((72.0, 52.0), -0.9, -0.06, 1.0), ((77.0, 50.0), -0.82, 0.1, 1.0), ((76.0, 51.0), -0.84, 0.08, 0.7),
            ((74.0, 58.0), -1.0, 0.03, 0.4), ((72.0, 66.0), -1.2, 0.0, 0.1)]
        out = []
        for i, (hand, la, lean, pr) in enumerate(keys):
            p = APose(hand_f=hand, lance_a=la, lean=lean, prism=pr, gem=[1.5, 1.6, 1.2, 0.9, 0.7][i],
                      eye=[1.4, 1.5, 1.2, 1.0, 0.8][i], antler=[1.2, 1.2, 0.9, 0.6, 0.45][i],
                      foot_f=(68.0 if i in (1, 2) else 66.0, GYD), foot_b=(42.0, GYD), cape_wave=[-1, -3, -2, 0, 1][i],
                      cape_lift=[0.3, 0.6, 0.4, 0.2, 0.1][i], hip=(54.0 + [0, 2, 2, 1, 0][i], 77.0),
                      hand_b=(40.0, 66.0), flick=i)
            f = self.draw(p)
            tx, ty = self.tip_pos(p)
            butt, tip, d = self.lance_pts(p)
            if i == 1:
                FX.burst(f, tx + d[0] * 3, ty + d[1] * 3, 7, [RAINBOW[0], RAINBOW[1], RAINBOW[2], RAINBOW[3]],
                         seed=self.seed + 40, jag=0.35, rays=6)
                for k, a in enumerate((-0.3, 0.0, 0.3)):
                    aa = la + a
                    shard(f, tx + math.cos(aa) * 10, ty + math.sin(aa) * 10, aa, 6, ICES[0], ICES[2])
                for k in range(5):
                    a = la + (k - 2) * 0.35
                    streak(f, (tx + math.cos(a) * 9, ty + math.sin(a) * 9), (tx + math.cos(a) * 18, ty + math.sin(a) * 18),
                           RAINBOW[1 + k % 5])
            if i == 2:
                FX.burst(f, tx + d[0] * 2, ty + d[1] * 2, 4, [RAINBOW[0], RAINBOW[3], RAINBOW[4]],
                         seed=self.seed + 41, jag=0.4)
                motes(f, tx, ty, 10, 6, 20, RAINBOW, seed=self.seed + 42)
            if i == 3:
                motes(f, tx, ty, 6, 3, 14, RAINBOW[2:], seed=self.seed + 43)
            out.append(f)
        return out

    def telegraph_special(self):
        """Charge wind-up: lowers the antlers like a stag, scrapes the ice with a hoof."""
        out = []
        for i in range(6):
            t = (i + 1) / 6
            scrape = (i % 2) * 3
            p = APose(lean=0.3 * t, head_tilt=0.45 * t, hip=(52.0 - 2 * t, 77.0 + 3 * t), foot_f=(66.0, GYD),
                      foot_b=(36.0 - scrape, GYD - (i % 2) * 2), hand_f=(66.0 + 2 * t, 74.0 - 2 * t),
                      lance_a=-1.42 + 1.2 * t, hand_b=(44.0, 74.0), gem=0.7 + 0.8 * t, eye=0.9 + 0.7 * t,
                      antler=0.5 + 0.8 * t, cape_wave=-1 - t, cape_lift=0.4 * t, flick=i)
            f = self.draw(p)
            dust_cloud(f, MX(34), GY, 3 + i * 0.6, SNOW, seed=self.seed + 50 + i, n=3)
            _, HD = self.frame(p)
            for (u, v) in ((8, -16), (5, -27), (6, -38)):
                q = HD(u, v)
                motes(f, q[0], q[1], 2 + i, 2, 7, GREENS[:3], seed=self.seed + 56 + i + u)
            if i == 5:
                q = HD(8, -16)
                FX.sparkle(f, int(q[0]), int(q[1]), 4, GREENS[0], GREENS[2])
            out.append(f)
        return out

    def special(self):
        """Charge (loop): galloping head-down, antlers forward, frost spraying behind."""
        out = []
        feet = [((72, GYD), (40, GYD - 6)), ((64, GYD - 5), (48, GYD)), ((56, GYD - 2), (58, GYD)), ((66, GYD), (36, GYD - 2))]
        for i, (ff, fb) in enumerate(feet):
            bob = [0, -2, 0, 1][i]
            p = APose(lean=0.32, head_tilt=0.45, hip=(52.0, 78.0 + bob), foot_f=ff, foot_b=fb,
                      hand_f=(68.0, 72.0 + bob), lance_a=-0.2, hand_b=(46.0, 74.0 + bob), gem=1.4, eye=1.5,
                      antler=1.3, cape_wave=2.5 * math.sin(i * PI / 2), cape_lift=1.0, flick=i)
            f = self.draw(p)
            dust_cloud(f, MX(26 - (i % 2) * 4), GY, 6, SNOW, seed=self.seed + 60 + i, n=4)
            rng = np.random.default_rng(self.seed + 65 + i)
            for k in range(5):
                y = int(rng.integers(40, 116))
                x0 = int(rng.integers(2, 16))
                streak(f, (x0, y), (x0 + int(rng.integers(5, 11)), y), ICES[2 + k % 2])
            out.append(f)
        return out

    def summon(self):
        """Plants the lance: frost runs along the ground and the aurora surges up (reflets, lac gele)."""
        keys = [  # hand, lance angle, lean, lift
            ((72.0, 60.0), -1.5, -0.05, 0.4), ((74.0, 84.0), -1.62, 0.12, 0.8), ((74.0, 86.0), -1.62, 0.14, 1.0),
            ((74.0, 86.0), -1.62, 0.13, 1.0), ((73.0, 84.0), -1.6, 0.1, 0.8), ((70.0, 76.0), -1.5, 0.03, 0.4)]
        out = []
        for i, (hand, la, lean, lift) in enumerate(keys):
            p = APose(hand_f=hand, lance_a=la + PI if i in (1, 2, 3, 4) else la, lean=lean, cape_lift=lift,
                      cape_wave=-2 * lift, gem=0.8 + 0.7 * lift, eye=1.0 + 0.4 * lift, antler=0.6 + 0.6 * lift,
                      hand_b=(36.0, 56.0 - 6 * lift), hip=(54.0, 77.0 + 2 * lift * (i in (1, 2, 3))), flick=i,
                      lance_grip=6.0 if i in (1, 2, 3, 4) else 0.0)
            f = self.draw(p)
            if 1 <= i <= 4:
                reach = [12, 34, 52, 62][i - 1]
                x0 = int(MX(76))
                hline(f, x0 - reach, x0 + reach, GY, ICES[1], only_empty=False)
                hline(f, x0 - reach + 3, x0 + reach - 3, GY - 1, ICES[2], step=2, phase=i)
                if i >= 2:
                    self.frost_spikes(f, x0 - reach + 4, 2, 5 + 2 * i, self.seed + 70 + i, spread=5)
                    self.frost_spikes(f, min(W - 8, x0 + reach - 4), 2, 5 + 2 * i, self.seed + 75 + i, spread=5)
                motes(f, 62, 44, 6 + 4 * i, 24, 56, AUR, seed=self.seed + 80 + i, arc=(-PI, 0), big=0.2)
            if i == 1:
                FX.burst(f, MX(76), GY, 5, [ICES[0], ICES[1], ICES[2]], seed=self.seed + 85, jag=0.4, rays=6)
            out.append(f)
        return out

    def roar(self):
        keys = [  # head_tilt, lean, gem, cape_lift, shake
            (0.15, 0.05, 0.7, 0.1, 0), (0.25, 0.08, 0.9, 0.2, 0), (-0.55, -0.12, 1.6, 1.0, 1), (-0.6, -0.14, 1.7, 1.1, -1),
            (-0.6, -0.14, 1.7, 1.1, 1), (-0.55, -0.12, 1.6, 1.0, -1), (-0.2, -0.04, 1.0, 0.5, 0), (0.0, 0.0, 0.7, 0.1, 0)]
        out = []
        for i, (ht, lean, g, lf, sh) in enumerate(keys):
            p = APose(head_tilt=ht, lean=lean, gem=g, eye=0.6 + 0.6 * g, antler=g, cape_lift=lf, cape_wave=sh * 2,
                      hand_b=(36.0, 58.0 - 8 * (g > 1.2)), hand_f=(70.0, 70.0), flick=i)
            f = self.draw(p)
            if g > 1.2:
                _, HD = self.frame(p)
                q = HD(10, -2)
                sound_rings(f, q[0], q[1], (8 + (i % 2) * 2, 14 + (i % 2) * 2, 20 + (i % 2) * 2),
                            [GREENS[1], GREENS[2], AUR[3]], -1.4, 0.6, dotted={2: 2})
                motes(f, 58, 44, 14, 30, 58, AUR, seed=self.seed + 90 + i, big=0.2)
                f = shake(f, sh, 0)
            out.append(f)
        return out

    def hurt(self):
        a = APose(lean=-0.12, hip=(52.0, 77.0), head_tilt=-0.2, gem=1.2, eye=1.3, cracks=1, hand_f=(66.0, 70.0),
                  cape_wave=2)
        b = APose(lean=-0.06, hip=(53.0, 77.0), head_tilt=-0.1, gem=0.8, eye=0.9, hand_f=(68.0, 71.0))
        fa = FX.flash(self.draw(a), amount=0.55)
        fb = self.draw(b)
        for k in range(3):
            shard(fb, 80 + 7 * k, 52 + 6 * k, -0.6 + 0.4 * k, 4, ICES[0], ICES[2])
        return [fa, fb]

    def residue(self, t):
        cv = TCanvas(W, H)
        pieces = [(64, GY - 2, 13, 4.6, ICE_DEAD), (48, GY - 1, 8, 3.4, ICE_DK), (80, GY - 1, 9, 3.6, ICE),
                  (59, GY - 6, 6, 3.4, ICE), (71, GY - 5, 5, 3.0, ICE_DK)]
        for i, (x, y, rx, ry, m) in enumerate(pieces):
            cv.part()
            cv.poly([(x - rx, y + ry), (x - rx * 0.3, y - ry * t - 0.5), (x + rx * 0.5, y - ry * 0.6 * t),
                     (x + rx, y + ry)], m, bevel=1.0, base=0.6)
        cv.part()
        cv.chain([(42, GY - 3), (35, GY - 9), (32, GY - 17)], [2.0, 1.6, 1.1], ICE_DEAD)
        cv.capsule((35, GY - 9), (28, GY - 12), 1.2, 0.8, ICE_DEAD)
        img = cv.render()
        img[GY + 1:] = 0
        return img

    def death(self):
        out = []
        seq = [
            APose(lean=-0.12, head_tilt=-0.35, gem=1.7, eye=1.6, cracks=1, cape_lift=0.8, hand_f=(68.0, 68.0)),
            APose(lean=-0.05, head_tilt=-0.2, gem=0.4, eye=0.6, cracks=2, hip=(54.0, 78.0), cape_lift=0.4),
            APose(lean=0.05, head_tilt=0.1, gem=1.4, eye=0.4, cracks=2, hip=(54.0, 80.0), hand_f=(70.0, 80.0),
                  cape_lift=0.2),
        ]
        for i, p in enumerate(seq):
            f = self.draw(p)
            if i == 0:
                f = FX.flash(f, amount=0.5)
            out.append(shake(f, [1, -1, 1][i], 0))
        fall = [
            APose(lean=0.15, head_tilt=0.3, gem=0.4, cracks=2, hip=(54.0, 84.0), foot_f=(66.0, GYD), foot_b=(40.0, GYD),
                  hand_f=(72.0, 92.0), lance_a=-0.3, dead=0.2, cape_lift=0.0, hand_b=(44.0, 86.0)),
            APose(lean=0.25, head_tilt=0.4, gem=0.2, cracks=2, hip=(54.0, 88.0), foot_f=(68.0, GYD), foot_b=(38.0, GYD),
                  hand_f=(74.0, 98.0), lance_a=-0.15, dead=0.4, hand_b=(46.0, 92.0)),
            APose(lean=0.32, head_tilt=0.5, gem=0.1, cracks=2, hip=(54.0, 91.0), foot_f=(70.0, GYD), foot_b=(36.0, GYD),
                  hand_f=(76.0, 102.0), lance_a=-0.08, dead=0.6, hand_b=(46.0, 96.0)),
            APose(lean=0.36, head_tilt=0.55, gem=0.0, cracks=2, hip=(54.0, 92.0), foot_f=(70.0, GYD), foot_b=(36.0, GYD),
                  hand_f=(76.0, 103.0), lance_a=-0.05, dead=0.85, hand_b=(46.0, 97.0)),
        ]
        for i, p in enumerate(fall):
            f = self.draw(p)
            if i >= 1:
                sep = [0, 2.5, 5.0, 7.5][i]
                f = split_frame(f, 64, 86, -0.4, [(-sep, sep * 0.5), (sep, -sep * 0.2 + sep * 0.5),
                                                  (-sep * 0.6, sep * 1.0), (sep * 0.8, sep * 0.8)],
                                C("#c8ffd0"), seed=self.seed + 100, cuts=2 if i >= 2 else 1)
                f = tint_toward(f, C("#8e9cae"), 0.12 * i, mask=~outline_mask(f))
            f[GY + 1:] = 0
            rng = np.random.default_rng(self.seed + 110 + i)
            for k in range(4 + 2 * i):
                a = rng.random() * PI + PI
                d = 14 + rng.random() * 28
                shard(f, 64 + math.cos(a) * d * K, 84 + math.sin(a) * d * 0.7 * K + 6 * i, a, 3 + int(rng.random() * 3),
                      ICES[0], ICES[2])
            motes(f, 64, 70, 10, 20, 56, AUR, seed=self.seed + 120 + i)
            out.append(f)
        base = out[-1].copy()
        for k, t in enumerate((0.16, 0.32, 0.5, 0.66, 0.82, 0.95)):
            f = FX.dissolve(base, t, seed=self.seed + 130, colors=[C("#ffffff"), C("#b4ffc0"), C("#9fe8ff"),
                                                                    C("#b48cff")],
                            rise=26, drift=6, top_first=True, life=0.4, particle_rate=0.16, bounds=(0, 0, W, GY))
            FX.under(f, self.residue(min(1.0, 0.3 + t)))
            out.append(f)
        last = self.residue(1.0)
        motes(last, 64, 112, 7, 4, 26, AUR[:3], seed=self.seed + 140, arc=(-2.8, -0.3))
        out.append(last)
        return out

    def sheet(self):
        return build_sheet(self, standard_clips(self))


BOSS = Aurorax()


def build():
    return BOSS.sheet()
