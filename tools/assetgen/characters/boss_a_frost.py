"""Frost Hydra (Hydre de Givre, Glacior) — hydra theme: a frozen tree-hydra.
Tall gnarled trunk encased in ice, a crown of icicles around a glowing frost
heart, ice spires and frozen roots at the base, three crystal-fanged dragon
heads on scaled necks, frozen roots that whip the ground."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, dust_cloud, mat, motes, polar, ring, seg_dir, shard, streak
from boss_a_hydra import H, W, HydraTheme
from raster import TCanvas

PI = math.pi

BARK = mat("fh_bark", ["#8496b0", "#5a6a84", "#3c4a64", "#273149", "#161c2e"])
BARK_DK = mat("fh_bark_dk", ["#5a6a84", "#3c4a64", "#273149", "#1a2236", "#101624"])
BARK_ICE = mat("fh_bark_ice", ["#c4eeff", "#7cc4f4", "#4a8ed0", "#2c5c9c", "#1a3666"])
ICE = mat("fh_ice", ["#f4fcff", "#bfeaff", "#7cc4f4", "#4a8ed0", "#2c5c9c"])
SCALE = mat("fh_scale", ["#bfe6ff", "#7cb2e2", "#4c7ebc", "#305694", "#1f3868", "#122244"])
SCALE_DK = mat("fh_scale_dk", ["#6e9fd2", "#44709f", "#2c4c84", "#1c325e", "#10203e"])
SCALE_DEAD = mat("fh_scale_dead", ["#8a9ab0", "#5c6b80", "#3d4a5e", "#283246"])
BELLY = mat("fh_belly", ["#f4f8ff", "#d2deec", "#9aaac0", "#66768c"])
CRYSTAL = mat("fh_crystal", ["#ffffff", "#c8f4ff", "#80d6ff", "#4296e0", "#2260aa"])
CRYSTAL_DK = mat("fh_crystal_dk", ["#a8e4ff", "#5fb0ff", "#3478c4", "#1f4c8e"])
GLOWC = mat("fh_glow", ["#ffffff", "#ccfcff", "#7ff6ff", "#34c6e0", "#1f7aa8"], emissive=True)
DIMC = mat("fh_dim", ["#5c6b80", "#3d4a5e", "#283246"])
FANG = mat("fh_fang", ["#ffffff", "#d4f2ff", "#8cc8ee"])
EYE = mat("fh_eye", ["#ffffff", "#d8fdff", "#7ff6ff", "#2fb8d0"], emissive=True)
MOUTH = mat("fh_mouth", ["#f0feff", "#9ffaff", "#40c8f0", "#2468b8", "#163a7a"], emissive=True)
THROAT = mat("fh_throat", ["#1f3a66", "#13264a", "#0a1428"])
SNOW = mat("fh_snow", ["#ffffff", "#eaf2fb", "#c9d8ea", "#93a6be"])
AUR = mat("fh_aurora", ["#d8fff0", "#5dffb0", "#2fb87e"], emissive=True)

ICES = [C("#ffffff"), C("#c8fbff"), C("#7ff6ff"), C("#2fc8e0")]
SNOWS = [C("#ffffff"), C("#dfe9f5"), C("#c9d8ea")]
SHARDS = [C("#ffffff"), C("#a8e4ff"), C("#5fb0ff"), C("#2f6fb3")]
AURORA = [C("#5dffb0"), C("#b48cff")]

TRUNK = [(36, 111), (43, 106), (48, 98), (49, 88), (47, 78), (50, 68), (53, 60), (56, 53), (61, 50), (68, 50),
         (73, 53), (76, 60), (79, 69), (81, 79), (80, 89), (82, 99), (87, 106), (94, 111)]
CLUSTERS = [((48.0, 74.0), (-2.5, -2.05, -2.9)), ((80.0, 82.0), (-0.65, -1.05, -0.25)), ((58.0, 97.0),
                                                                                         (-2.2, -1.75))]
HEART = (64.0, 64.0)


def _side_up(nx, ny):
    return 1.0 if (nx * -0.6 + ny * -0.8) >= 0 else -1.0


def crystal(cv, base, ang, ln, wd, m, glow_m=None, glow=0.0, facet=True):
    """Faceted crystal spike: hexagonal-ish prism with a pointed tip."""
    c, s = math.cos(ang), math.sin(ang)
    nx, ny = -s, c

    def P(u, v):
        return (base[0] + c * u + nx * v, base[1] + s * u + ny * v)
    cv.part()
    cv.tpoly([P(0, -wd), P(ln * 0.68, -wd * 0.95), P(ln, 0), P(ln * 0.68, wd * 0.95), P(0, wd)], m, bevel=1.1,
             base=0.6)
    if facet:
        cv.part(line=False)
        cv.tline(P(1, -wd * 0.15), P(ln * 0.68, -wd * 0.15), m, inten=0.95, decal=True)
    if glow_m is not None and glow > 0.05:
        cv.part(line=False)
        cv.tcapsule(P(ln * 0.55, 0), P(ln - 1.2, 0), 0.6 + 0.4 * glow, 0.4, glow_m, flat=0.3 + 0.6 * min(1.0, glow),
                    decal=True)


def fly_shards(f, specs, glow=0.8, under=False):
    """Outlined crystal shards (x, y, ang, ln, wd) rendered and laid over the frame."""
    cv = TCanvas(W, H)
    for (x, y, a, ln, wd) in specs:
        bx, by = x - math.cos(a) * ln * 0.5, y - math.sin(a) * ln * 0.5
        crystal(cv, (bx, by), a, ln, wd, CRYSTAL, GLOWC, glow=glow, facet=False)
    img = cv.render()
    if under:
        FX.under(f, img)
    else:
        sel = img[:, :, 3] > 0
        f[sel] = img[sel]
    return f


class FrostHydra(HydraTheme):
    id = "frost_hydra"
    seed = 2200
    head_len = 24.0
    head_lie = 6.5
    rest = [(24.0, 38.0, -0.22), (57.0, 14.0, -0.05), (97.0, 38.0, 0.18)]
    anchors = [((55.0, 60.0), (-0.75, -0.66)), ((64.0, 54.0), (0.1, -1.0)), ((74.0, 60.0), (0.85, -0.55))]
    neck_r = [(5.4, 3.8), (6.0, 4.0), (6.0, 4.0)]
    fall_dx = (-6.0, 4.0, 8.0)
    fall_rot = (-0.8, 0.6, 1.0)
    cols = {
        "glow": ICES, "ring": [C("#c8fbff"), C("#7ff6ff")], "smear": C("#a8e4ff"),
        "snap": [C("#ffffff"), C("#7ff6ff")], "dissolve": [C("#a8e4ff"), C("#7ff6ff"), C("#5fb0ff"), C("#2f6fb3")],
        "wither": C("#5c6b80"),
    }

    def bite_c(self, t):
        return (8.0 * t, 34.0 * t, 0.55 * t)

    def bite_b(self, t):
        return (20.0 * t, 26.0 * t, 0.5 * t)

    # ------------------------------------------------------------------ parts
    def back(self, cv, p):
        dead = p.dead >= 0.6
        # ice spires growing out of the ground behind the trunk (jagged silhouette)
        for (x, y, a, ln, wd) in ((40, 111, -2.05, 26, 4.2), (30, 111, -2.5, 16, 3.2), (90, 111, -1.1, 24, 4.0),
                                  (101, 111, -0.65, 15, 3.0), (48, 108, -1.75, 14, 2.6)):
            crystal(cv, (x, y), a, ln * (1 - 0.25 * p.wilt), wd, CRYSTAL_DK if not dead else DIMC)
        # frozen roots behind
        cv.part()
        for pts in (((50, 108), (38, 109), (24, 108.5), (10, 110.5)), ((78, 108), (92, 109), (106, 108.5),
                                                                       (120, 110.5))):
            cv.tchain(list(pts), [3.0, 2.4, 1.8, 1.2], BARK_DK)

    def body(self, cv, p):
        dead = p.dead >= 0.6
        sw = p.sway * 0.5
        pts = [(x + sw * (111 - y) / 60.0, y) for (x, y) in TRUNK]
        cv.part()
        cv.tpoly(pts, BARK, bevel=3.2, base=0.5)
        # bark grooves
        cv.part(line=False)
        for x0, wob in ((54, 1.4), (59, -1.2), (64, 1.0), (69, -1.4), (74, 1.2)):
            prev = None
            for y in range(56, 104, 3):
                k = (111 - y) / 60.0
                spread = 1.0 + (y - 70) / 70.0
                x = 64 + (x0 - 64) * spread + wob * math.sin(y * 0.35 + x0) + sw * k
                if prev is not None:
                    cv.tline(prev, (x, y), BARK, inten=0.18, decal=True)
                prev = (x, y)
        # ice encasing the lower trunk (jagged frozen crust on top)
        cv.part()
        shell = [(33, 111), (40, 106), (45, 98), (46, 88), (44, 79), (47, 73), (51, 78), (55, 71), (59, 77),
                 (63, 70), (67, 76), (71, 69), (75, 75), (79, 71), (84, 79), (83, 89), (85, 99), (90, 106),
                 (97, 111)]
        cv.tpoly([(x + sw * (111 - y) / 60.0, y) for (x, y) in shell], BARK_ICE, bevel=2.2, base=0.56)
        cv.part(line=False)
        # trunk grooves seen through the ice + glints
        for x0 in (52, 58, 64, 70, 76):
            cv.tline((x0 + 1 + sw * 0.5, 80), (x0 - 1 + (x0 - 64) * 0.35, 108), BARK_ICE, inten=0.22, decal=True)
        for (x0, y0, ln) in ((40, 104, 5), (47, 92, 6), (56, 99, 4), (68, 88, 5), (78, 98, 4), (52, 82, 3)):
            cv.tline((x0, y0), (x0 + ln * 0.6, y0 - ln), ICE, inten=0.97, decal=True)
        for (x0, y0, x1, y1) in ((47, 74, 51, 79), (55, 72, 59, 78), (63, 71, 67, 77), (71, 70, 75, 76),
                                 (79, 72, 83, 79)):
            cv.tline((x0 + sw * 0.6, y0 + 1), (x1 + sw * 0.6, y1), ICE, inten=0.9, decal=True)
        # glowing frost heart in the trunk
        cv.part()
        hx, hy = HEART[0] + sw * 0.8, HEART[1]
        cv.tellipse((hx, hy), 5.2, 6.4, BARK_DK, bias=-0.25)
        cv.part(line=False)
        hg = min(1.5, 0.5 * p.sacs + 0.5 * p.crown)
        hm = GLOWC if not dead else DIMC
        cv.tpoly([(hx, hy - 5.2), (hx + 3.4, hy), (hx, hy + 5.2), (hx - 3.4, hy)], hm, bevel=1.0,
                 base=0.3 + 0.45 * hg)
        cv.tpx((hx - 1, hy - 2), hm, inten=1.0, decal=True)
        # glowing crystal clusters (the hydra's "sacs")
        for i, (c, angs) in enumerate(CLUSTERS):
            c = (c[0] + sw * (111 - c[1]) / 60.0, c[1])
            broken = i in p.burst
            for j, a in enumerate(angs):
                ln = (10.0, 7.5, 6.0)[j] * (0.85 + 0.2 * min(1.6, p.sacs)) * (0.45 if broken else 1.0)
                crystal(cv, c, a, ln, (2.6, 2.0, 1.7)[j], CRYSTAL if not dead else DIMC,
                        GLOWC if not dead else None, glow=0.3 + 0.5 * min(1.6, p.sacs))
        if p.cracks and not dead:
            cv.part(line=False)
            segs = [((58, 70), (62, 76)), ((62, 76), (60, 82)), ((72, 66), (75, 72))]
            if p.cracks > 1:
                segs += [((52, 88), (56, 94)), ((76, 90), (73, 96)), ((66, 56), (68, 60))]
            for a, b in segs:
                cv.tline(a, b, GLOWC, inten=0.75, decal=True)

    def _crown(self, cv, p, front):
        cb = (64.0 + p.sway * 0.5, 55.0)
        dead = p.dead >= 0.6
        cr = min(1.3, p.crown)
        if front:
            spec = ((-2.75, 13, 2.6), (-0.4, 13, 2.6), (-2.25, 10, 2.2), (-0.9, 10, 2.2))
            m = CRYSTAL
        else:
            spec = ((-2.45, 20, 3.0), (-2.0, 24, 3.2), (-1.55, 27, 3.4), (-1.1, 23, 3.2), (-0.65, 19, 3.0))
            m = CRYSTAL_DK
        for a0, ln, wd in spec:
            a = a0 + (a0 + PI / 2) * 0.25 * cr + (1 if a0 > -PI / 2 else -1) * p.wilt * 0.9
            ln = ln * (1.0 + 0.32 * cr) * (1.0 - 0.45 * p.wilt)
            crystal(cv, polar(cb, a, 2.0), a, ln, wd * (1.0 + 0.15 * cr), m if not dead else DIMC,
                    GLOWC if not dead else None, glow=0.25 + 0.75 * cr)
            if cr > 0.6 and not dead:
                cv.part(line=False)
                cv.tcapsule(polar(cb, a, ln * 0.3), polar(cb, a, ln * 0.85), 0.8, 0.5, GLOWC,
                            flat=0.2 + 0.6 * (cr - 0.6), decal=True)

    def crown_back(self, cv, p):
        self._crown(cv, p, False)

    def crown_front(self, cv, p):
        self._crown(cv, p, True)

    def front(self, cv, p):
        dead = p.dead >= 0.6
        # frozen roots breaking through the snow
        cv.part()
        for pts in (((46, 108), (36, 106), (26, 108), (18, 110.5)), ((82, 108), (92, 106), (102, 108),
                                                                       (110, 110.5))):
            cv.tchain(list(pts), [3.4, 2.6, 1.8, 1.0], BARK)
        # snow drift around the base
        cv.part()
        cv.tellipse((64, 111), 40, 5.0, SNOW, bias=0.05)
        cv.part()
        cv.tellipse((40, 110), 10, 3.6, SNOW, bias=0.08)
        cv.tellipse((90, 110), 11, 3.4, SNOW, bias=0.08)
        # small ice shards in the snow
        for (x, a, ln) in ((28, -2.2, 6), (102, -0.9, 7), (54, -1.8, 5), (80, -1.2, 5)):
            crystal(cv, (x, 110), a, ln, 1.6, CRYSTAL if not dead else DIMC, facet=False)
        if p.ground_glow > 0.05 and not dead:
            cv.part(line=False)
            g = min(1.2, p.ground_glow)
            x1 = 84 + 42 * min(1.0, g)
            for x in np.arange(80, x1, 1.0):
                if int(x) % 6 != 5:
                    cv.tpx((x, 110.6 - (0.6 if int(x) % 9 < 4 else 0)), GLOWC, inten=0.35 + 0.45 * g)

    # ------------------------------------------------------------ neck/head
    def neck(self, cv, pts, radii, k, p, dark):
        dead = p.dead >= 0.55
        m = SCALE_DEAD if dead else (SCALE_DK if dark else SCALE)
        cv.part()
        cv.tchain(pts, radii, m)
        cv.part(line=False)
        n = len(pts)
        # pale belly band on the inner (lower) side
        if not dead:
            band = []
            for i in range(n):
                a = seg_dir(pts, i)
                nx, ny = -math.sin(a), math.cos(a)
                s = _side_up(nx, ny)
                band.append((pts[i][0] - nx * s * radii[i] * 0.62, pts[i][1] - ny * s * radii[i] * 0.62))
            cv.tchain(band, [max(0.9, r * 0.32) for r in radii], BELLY, decal=True, bias=-0.05)
        for i in range(1, n - 1):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            r = radii[i]
            if i % 2 == 0:
                cv.tline((pts[i][0] + nx * r * 0.8, pts[i][1] + ny * r * 0.8),
                         (pts[i][0] - nx * r * 0.8, pts[i][1] - ny * r * 0.8), m, inten=0.3, decal=True)
        # dorsal crystal spikes along the outer (upper) side
        for i in range(3, n - 2, 4):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            s = _side_up(nx, ny)
            r = radii[i]
            b = (pts[i][0] + nx * s * r * 0.7, pts[i][1] + ny * s * r * 0.7)
            sa = math.atan2(ny * s, nx * s) - 0.45 * math.copysign(1, s) * (1 if math.cos(a) >= 0 else -1)
            crystal(cv, b, sa, 5.0 + 1.5 * (i % 2), 1.7, CRYSTAL_DK if dark else CRYSTAL, facet=False)

    def wound(self, cv, pt, r, p, ang=0.0):
        """Shattered ice break: jagged crystal shards sticking out of the neck end."""
        for k, (da, ln, wd) in enumerate(((0.0, r * 1.1, r * 0.45), (0.55, r * 0.8, r * 0.35),
                                          (-0.6, r * 0.75, r * 0.35))):
            nx, ny = -math.sin(ang), math.cos(ang)
            off = (k - 1) * r * 0.45
            b = (pt[0] + nx * off - math.cos(ang) * 1.0, pt[1] + ny * off - math.sin(ang) * 1.0)
            crystal(cv, b, ang + da, ln, wd, CRYSTAL_DK if p.dead < 0.55 else DIMC, facet=False)

    def head(self, cv, hp, k, p, dark):
        x, y, a, j = hp.x, hp.y, hp.a, hp.jaw
        glow = min(1.3, hp.glow)
        dead = p.dead >= 0.55
        sm = SCALE_DEAD if dead else (SCALE_DK if dark else SCALE)
        cm = DIMC if dead else (CRYSTAL_DK if dark else CRYSTAL)
        ua, la = -0.3 * j, 0.55 * j
        hu, hv = 4.0, 1.5

        def Q(u, v, r=0.0):
            du, dv = u - hu, v - hv
            cr, sr = math.cos(r), math.sin(r)
            u2 = hu + du * cr - dv * sr
            v2 = hv + du * sr + dv * cr
            ca, sa = math.cos(a), math.sin(a)
            return (x + u2 * ca - v2 * sa, y + u2 * sa + v2 * ca)

        def spike(b, tip, wd, m):
            bx, by = b
            tx, ty = tip
            L = math.hypot(tx - bx, ty - by) or 1.0
            nx, ny = -(ty - by) / L * wd, (tx - bx) / L * wd
            cv.part()
            cv.tpoly([(bx + nx, by + ny), tip, (bx - nx, by - ny)], m, bevel=0.9, base=0.62)

        # crystal horns (behind the skull)
        spike(Q(4, -5, ua), Q(-9, -13, ua), 2.4, cm)
        spike(Q(2, -2, ua), Q(-8, -5, ua), 1.8, cm)
        # cheek frill
        spike(Q(2, 4.5, la), Q(-6, 10, la), 1.8, cm)
        # lower jaw
        cv.part()
        cv.tpoly([Q(*q, la) for q in ((2, 1.5), (19, 2.2), (22.5, 3.4), (18.5, 6.4), (6, 6.8), (0.5, 4.6))], sm,
                 bevel=1.6, base=0.5)
        cv.part(line=1)
        for u, ln in ((11, 2.6), (15, 2.8), (19.5, 2.4)):
            spike(Q(u, 2.4, la), Q(u + 0.4, 2.4 - ln, la), 0.8, FANG)
        # mouth interior
        if j > 0.04:
            cv.part()
            cv.tpoly([Q(3, 1.6), Q(23.5, 0.8, ua), Q(21.5, 2.6, la)], THROAT, flat=0.5)
            cv.part(line=False)
            mr = (ua + la) / 2
            cv.tcapsule(Q(5, 1.8, mr), Q(5 + 12 * (0.5 + 0.4 * j), 1.8, mr), 0.8 + 1.4 * j, 0.5 + 0.8 * j,
                        MOUTH if not dead else THROAT, flat=0.15 + 0.6 * glow)
        # skull + snout
        cv.part()
        cv.tellipse(Q(6.5, -1.2, ua), 8.0, 6.8, sm, ang=a + ua)
        cv.tpoly([Q(*q, ua) for q in ((5, -6.6), (13, -6.1), (20, -3.9), (25, -1.3), (25.6, 1.2), (21, 2.4),
                                      (6, 2.8), (2.5, 0))], sm, bevel=1.6, base=0.56)
        cv.part(line=False)
        for (u0, v0, u1, v1) in ((14, -5.2, 15, -2.0), (18.5, -3.6, 19.2, -1.2)):
            cv.tline(Q(u0, v0, ua), Q(u1, v1, ua), sm, inten=0.3, decal=True)
        cv.tpx(Q(23.0, -1.0, ua), THROAT, inten=0.3, decal=True)
        # upper crystal fangs
        cv.part(line=1)
        for u, ln in ((9.5, 3.6), (13.5, 3.0), (17.5, 3.8), (21.5, 2.6)):
            spike(Q(u, 2.2, ua), Q(u + 0.5, 2.2 + ln, ua), 0.85, FANG)
        # brow crystal plate + eye
        spike(Q(4, -6.2, ua), Q(15, -5.0, ua), 1.5, cm)
        cv.part(line=False)
        cv.tcapsule(Q(8.2, -3.3, ua), Q(13.6, -3.0, ua), 2.0, 1.6, THROAT, flat=0.45)
        if not dead:
            er = 1.1 + 0.35 * glow
            cv.tcapsule(Q(8.9, -3.3, ua), Q(13.0, -3.0, ua), er, er * 0.75, EYE, flat=0.55 + 0.4 * glow)
        else:
            cv.tcapsule(Q(9.0, -3.1, ua), Q(12.6, -2.9, ua), 0.8, 0.7, THROAT, flat=0.3)

    def vine(self, cv, v, p):
        pts = v.points()
        n = len(pts)
        radii = [v.r0 + (v.r1 - v.r0) * (i / (n - 1)) ** 0.8 for i in range(n)]
        cv.part()
        cv.tchain(pts, radii, BARK_DK if v.back else BARK)
        cv.part(line=False)
        band = []
        for i in range(n):
            a = seg_dir(pts, i)
            nx, ny = -math.sin(a), math.cos(a)
            s = _side_up(nx, ny)
            band.append((pts[i][0] + nx * s * radii[i] * 0.5, pts[i][1] + ny * s * radii[i] * 0.5))
        cv.tchain(band[:-2], [max(0.8, r * 0.35) for r in radii[:-2]], ICE, decal=True, bias=0.1)
        for i in range(2, n - 2, 4):
            cv.tpx(pts[i], GLOWC, inten=0.3 + 0.5 * min(1.0, v.glow), decal=True)
        # ice spikes along the root and an ice blade tip
        for i in range(3, n - 3, 4):
            a = seg_dir(pts, i)
            sd = 1 if (i // 4) % 2 else -1
            nx, ny = -math.sin(a) * sd, math.cos(a) * sd
            b = (pts[i][0] + nx * radii[i] * 0.7, pts[i][1] + ny * radii[i] * 0.7)
            crystal(cv, b, math.atan2(ny, nx) + 0.6 * sd, 4.0, 1.3, CRYSTAL, facet=False)
        a = seg_dir(pts, n - 2)
        crystal(cv, pts[-2], a, 6.0, 1.8, CRYSTAL, GLOWC, glow=v.glow)

    # ---------------------------------------------------------------- effects
    def post_idle(self, f, i, p):
        rng = np.random.default_rng(self.seed + 5)
        for k in range(9):
            x0 = rng.random() * W
            y0 = rng.random() * 90
            y = (y0 + i * 3 + k) % 100
            x = x0 + math.sin(i * 0.9 + k) * 1.5
            put_empty(f, x, y, SNOWS[k % 3])
        return f

    def telegraph_spores_fx(self, f, i, t, p):
        cb = (64 + p.sway * 0.5, 46)
        motes(f, cb[0], cb[1], 10 + 5 * i, 14, 46 - 14 * t, ICES[:3] + AURORA, seed=self.seed + 200 + i,
              inward=0.1 * t, big=0.3)
        if i >= 1:
            ring(f, cb[0], cb[1], 30 - 3.5 * i, ICES[2], -PI, 0.3, dotted=2)
        if i >= 3:
            ring(f, cb[0], cb[1], 20 - 3 * (i - 3), AURORA[0], -PI, 0.3, dotted=2)
            ring(f, HEART[0], HEART[1], 8 - (i - 3), ICES[1], -PI, PI, dotted=2)
        if i >= 4:
            FX.sparkle(f, int(cb[0]), int(cb[1] - 14), 3 + 2 * (i - 4), ICES[0], ICES[2])
        if i == 5:
            FX.sparkle(f, int(HEART[0]), int(HEART[1]), 5, ICES[0], ICES[2])

    def spores_fx(self, f, i, p):
        cb = (64.0, 44.0)
        if i == 0:
            ring(f, cb[0], cb[1], 16, ICES[1], -PI, 0.3, dotted=2)
            FX.sparkle(f, int(cb[0]), int(cb[1] - 10), 5, ICES[0], ICES[2])
        if i == 1:
            FX.burst(f, cb[0], cb[1] - 10, 8, [ICES[0], ICES[1], ICES[2], C("#5fb0ff")], seed=self.seed + 270,
                     jag=0.55, rays=6)
            fly_shards(f, ((86, 24, -0.55, 10, 2.2), (100, 38, -0.25, 10, 2.2), (72, 12, -1.0, 9, 2.0),
                           (108, 54, 0.05, 9, 2.0), (48, 16, -2.3, 8, 1.8)), glow=0.9)
            for (x, y, ang) in ((86, 24, -0.55), (100, 38, -0.25), (72, 12, -1.0), (108, 54, 0.05)):
                streak(f, (x - math.cos(ang) * 16, y - math.sin(ang) * 16), (x - math.cos(ang) * 6,
                                                                           y - math.sin(ang) * 6), SHARDS[2])
            motes(f, cb[0], cb[1] - 6, 24, 10, 34, ICES[:3] + SNOWS[:1], seed=self.seed + 273, arc=(-PI, 0.4))
        if i == 2:
            FX.burst(f, cb[0] + 2, cb[1] - 14, 5, [ICES[1], ICES[2], C("#5fb0ff")], seed=self.seed + 280, jag=0.6)
            fly_shards(f, ((104, 14, -0.55, 8, 1.8), (118, 32, -0.25, 8, 1.8), (88, 4, -1.0, 7, 1.6),
                           (122, 52, 0.05, 7, 1.6)), glow=0.6)
            motes(f, cb[0] + 8, cb[1] - 10, 18, 8, 40, ICES[1:3] + SNOWS[:2], seed=self.seed + 282, arc=(-PI, 0.3))
        if i == 3:
            motes(f, cb[0] + 10, cb[1] - 16, 12, 6, 40, SNOWS + ICES[2:3], seed=self.seed + 290, arc=(-PI, 0.3))

    def telegraph_vines_fx(self, f, i, t, p):
        # frost cracks race along the ground, snow puffs where the roots break out
        for x in (94, 108):
            motes(f, x, 106, 2 + 2 * i, 3, 10, SNOWS + ICES[2:3], seed=self.seed + 300 + x + i, arc=(-PI, 0))
        if i >= 1:
            dust_cloud(f, 100, 110, 3 + i, SNOWS, seed=self.seed + 310 + i, n=2)
        if i == 4:
            for v in self.vines_windup(1.0)[:2]:
                tip = v.points()[-1]
                FX.sparkle(f, int(tip[0]), int(tip[1]), 2, ICES[0], ICES[2])

    def vines_fx(self, f, i, p):
        if i == 0:
            for k in range(6):
                a = -2.3 + k * 0.3
                for rr, col in ((38, C("#a8e4ff")), (41, C("#5fb0ff"))):
                    put_empty(f, 94 + math.cos(a) * rr * 0.8, 111 + math.sin(a) * rr, col)
        if i in (1, 2):
            # ice spikes erupt where the roots hit
            cv = TCanvas(W, H)
            for (x, a, ln, wd) in ((112, -1.9, 13, 2.6), (118, -1.35, 17, 3.0), (124, -1.0, 12, 2.4),
                                   (106, -2.2, 9, 2.0), (127, -1.6, 8, 2.0)):
                ln = ln * (1.0 if i == 1 else 0.75)
                crystal(cv, (x, 111.5), a, ln, wd, CRYSTAL, GLOWC, glow=0.8 if i == 1 else 0.4)
            spikes = cv.render()
            FX.under(f, spikes)
            dust_cloud(f, 116, 110, 10 if i == 1 else 7, SNOWS, seed=self.seed + 320 + i, n=5)
            for (x, y, s) in ((122, 86, 2), (106, 90, 2), (127, 96, 1), (100, 94, 1), (114, 82, 1)):
                if i == 1:
                    FX.chunk(f, x, y, s, C("#c8f4ff"), C("#4296e0"))
            if i == 1:
                FX.sparkle(f, 118, 96, 4, ICES[0], ICES[2])
        if i == 3:
            dust_cloud(f, 116, 110, 4, SNOWS[1:], seed=self.seed + 340, n=3)

    def summon_fx(self, f, i, p):
        spots = (36, 92)
        if i in (1, 2):
            # frost shards lobbed from the crown towards both spawn points
            specs = []
            for k, x1 in enumerate(spots):
                t = 0.3 if i == 1 else 0.75
                x = 64 + (x1 - 64) * t
                y = 40 + (100 - 40) * t * t - 18 * math.sin(PI * t)
                ang = math.atan2((100 - 40) * 2 * t - 18 * PI * math.cos(PI * t), (x1 - 64))
                specs.append((x, y, ang, 9, 2.2))
                streak(f, (x - math.cos(ang) * 14, y - math.sin(ang) * 14), (x - math.cos(ang) * 5,
                                                                           y - math.sin(ang) * 5), ICES[2])
            fly_shards(f, specs, glow=0.9)
            if i == 1:
                FX.burst(f, 64, 40, 7, ICES[:3], seed=self.seed + 400, jag=0.5, rays=4)
        if i >= 3:
            cv = TCanvas(W, H)
            g = (0.55, 0.9, 1.0)[i - 3]
            for x in spots:
                for (dx, a, ln) in ((0, -1.57, 16), (-4, -2.15, 10), (4, -1.0, 10), (-7, -2.5, 6), (7, -0.65, 6)):
                    crystal(cv, (x + dx, 111.5), a, ln * g, 2.2 + 0.8 * g, CRYSTAL, GLOWC, glow=1.0)
            img = cv.render()
            sel = img[:, :, 3] > 0
            f[sel] = img[sel]
            for k, x in enumerate(spots):
                ring(f, x, 110, 8 + 3 * (i - 3), ICES[2], -PI, 0, dotted=2)
                motes(f, x, 100, 12, 3, 16, ICES[:3] + AURORA[:1], seed=self.seed + 420 + 10 * i + k, arc=(-PI, 0),
                      big=0.2)
            if i == 3:
                for x in spots:
                    FX.burst(f, x, 104, 6, ICES[:3], seed=self.seed + 430 + x, jag=0.5, rays=4)

    def death_fx(self, f, i, p):
        rng = np.random.default_rng(self.seed + 500 + i)
        if i in (1, 2):
            c = CLUSTERS[i - 1][0]
            FX.burst(f, c[0], c[1], 6, ICES[:3], seed=self.seed + 505 + i, jag=0.6, rays=5)
        if 1 <= i <= 6:
            # ice shell / crystals shatter into flying shards
            for k in range(5 + i):
                x = 30 + rng.random() * 70
                y = 40 + rng.random() * 60
                ang = -PI / 2 + (rng.random() - 0.5) * 2.4
                shard(f, x, y, ang, 3 + int(rng.random() * 4), SHARDS[0], SHARDS[1 + k % 2])
            motes(f, 64, 80, 10, 10, 44, SNOWS + ICES[1:3], seed=self.seed + 540 + i, arc=(-PI, 0))

    def residue(self, t):
        cv = TCanvas(W, H)
        cv.part()
        cv.ellipse((64, 112), 26 * (0.7 + 0.3 * t), 6 * max(0.5, t), SNOW)
        for (x, a, ln, wd) in ((54, -2.0, 9, 2.4), (64, -1.6, 12, 2.8), (74, -1.2, 9, 2.4), (46, -2.4, 6, 2.0),
                               (84, -0.8, 6, 2.0)):
            crystal(cv, (x, 110), a, ln * (0.4 + 0.6 * t), wd, CRYSTAL_DK, facet=False)
        cv.part()
        cv.chain([(44, 110), (54, 108), (64, 109), (76, 108), (86, 110)], [1.6] * 5, BARK_DK)
        return cv.render()


def put_empty(f, x, y, c):
    x, y = int(round(x)), int(round(y))
    if 0 <= y < f.shape[0] and 0 <= x < f.shape[1] and f[y, x, 3] == 0:
        f[y, x] = c


THEME = FrostHydra()
