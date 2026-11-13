"""Glacior terrain materials (frozen sanctuary): glacier_ice, snow_rock, frost_metal.

Same autotile engine as the Sylvanox materials (tiles.Material). glacier_ice is slippery:
its JSON carries "friction": 0.25.
"""
import math

import numpy as np

import tile_decor_glacior as TD
from helpers import bayer, col
from sprites import stamp
from tiles import Material, TS

# Glacior palette (CONTENT_CONTRACT section 1)
NAVY = ["#0a1428", "#13264a", "#1f3a66"]
ICE = ["#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]
SNOW = ["#c9d8ea", "#dfe9f5", "#ffffff"]
FROST = "#7ff6ff"
AURORA = ["#5dffb0", "#b48cff"]
STEEL = ["#3d4a5e", "#5c6b80", "#8a9ab0"]
SNOW_SH = ["#7d93b5", "#9fb4d2"]          # snow in shadow
OUT_G = "#0a1428"
B4 = bayer(TS, TS, 4)


def torus_voronoi(seeds, sx=1.0, sy=1.0):
    """Periodic Voronoi on the 16x16 torus. Returns nearest index, gap to the 2nd nearest,
    direction towards the neighbouring seed and offset from the own seed."""
    near = np.zeros((TS, TS), int)
    gap = np.zeros((TS, TS))
    nrm = np.zeros((TS, TS, 2))
    off = np.zeros((TS, TS, 2))
    for y in range(TS):
        for x in range(TS):
            ds = []
            for k, s in enumerate(seeds):
                dx = (x + 0.5 - s[0] + 8) % 16 - 8
                dy = (y + 0.5 - s[1] + 8) % 16 - 8
                ds.append(((dx * sx) ** 2 + (dy * sy) ** 2, k, dx, dy))
            ds.sort()
            near[y, x] = ds[0][1]
            gap[y, x] = ds[1][0] ** 0.5 - ds[0][0] ** 0.5
            nrm[y, x] = (ds[0][2] - ds[1][2], ds[0][3] - ds[1][3])
            off[y, x] = ds[0][2], ds[0][3]
    return near, gap, nrm, off


def _px(img, x, y, c):
    img[y % TS, x % TS] = col(c)


# ===========================================================================
# glacier_ice: translucent blue ice blocks, frost crust on top
# ===========================================================================
class GlacierIce(Material):
    name = "glacier_ice"
    map_color = "#5fb0ff"
    friction = 0.25
    ramp = ["#0c1d3c", "#163463", "#1f4f8f", "#2f6fb3", "#4d96e0", "#8fd2ff"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(1, False), (1, True)]          # light shining through the frost crust
    top_th = [3, 3, 2, 2, 3, 4, 3, 2, 2, 3, 3, 2, 3, 4, 4, 3]
    drape_l = (5, 3)
    drape_r = (3, 2)
    outline_color = OUT_G
    # depth zones: smooth 16-periodic field; cracks (x, y) polylines; glint streaks
    CRACKS = [[(1, 3), (3, 4), (4, 6), (6, 7)], [(10, 10), (11, 12), (13, 13), (14, 15)]]
    GLINTS = [(9, 5, 5), (2, 13, 3)]       # (x, y, length): slope -1 streak going up-right

    def depth(self, x, y):
        a = math.sin(2 * math.pi * (x + 2 * y) / TS + 0.4)
        b = math.sin(2 * math.pi * (2 * x - y) / TS + 1.9)
        c = math.sin(2 * math.pi * (x - 3 * y) / TS + 4.0)
        return 0.58 * a + 0.3 * b + 0.12 * c

    def make_base_idx(self):
        idx = np.zeros((TS, TS), int)
        for y in range(TS):
            for x in range(TS):
                t = self.depth(x, y)
                if t < -0.58:
                    v = 2
                elif t > 0.6:
                    v = 4
                else:
                    v = 3
                for thr, lo in ((-0.58, 2), (0.6, 3)):
                    if abs(t - thr) < 0.07 and (x + y) % 2 == 0:
                        v = lo + 1 if t < thr else lo
                idx[y, x] = v
        for poly in self.CRACKS:
            for (x0, y0), (x1, y1) in zip(poly[:-1], poly[1:]):
                n = max(abs(x1 - x0), abs(y1 - y0))
                for i in range(n + 1):
                    xx = round(x0 + (x1 - x0) * i / max(1, n))
                    yy = round(y0 + (y1 - y0) * i / max(1, n))
                    idx[yy % TS, xx % TS] = 1
                    if idx[(yy + 1) % TS, xx % TS] in (2, 3):
                        idx[(yy + 1) % TS, xx % TS] = 4
        for (x0, y0, n) in self.GLINTS:
            for i in range(n):
                idx[(y0 - i) % TS, (x0 + i) % TS] = 5 if 0 < i < n - 1 else 4
                if i < n - 1:
                    idx[(y0 - i + 1) % TS, (x0 + i) % TS] = 4
        return idx

    def add_base_details(self, img):
        # air bubbles + frosty sparkle
        for (x, y, c) in [(6, 10, ICE[3]), (7, 11, ICE[1]), (13, 2, ICE[2]), (5, 1, ICE[2])]:
            _px(img, x, y, c)

    def top_color(self, x, d, th):
        if d == 0:
            return SNOW[2] if x % 5 in (1, 2) else ICE[3]
        if d == 1:
            return SNOW[0] if x % 3 else ICE[2]
        if d == th - 1:
            return ICE[1]
        return ICE[2]

    def drape_color(self, side, k, d, n):
        if side == "l":
            if d == n - 1:
                return ICE[1]
            return ICE[3] if k == 0 else ICE[2]
        if d == n - 1:
            return self.ramp[4]
        return ICE[2] if k == 0 else ICE[1]

    def post_cell(self, img, g):
        # meltwater drips / tiny icicle tips glinting on the underside
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                if x in (4, 11) and 1 <= g.db[y, x] <= 3:
                    img[y, x] = col(ICE[1] if g.db[y, x] > 1 else ICE[2])
                if x == 5 and g.db[y, x] == 1:
                    img[y, x] = col(ICE[0])

    def variants(self):
        b = self.base
        L = {"o": OUT_G, "d": self.ramp[0], "k": self.ramp[1], "s": self.ramp[2],
             "u": self.ramp[3], "U": self.ramp[4], "S": self.ramp[5], "i": ICE[0], "I": ICE[1],
             "j": ICE[2], "w": ICE[3], "W": SNOW[2], "c": FROST, "g": AURORA[0], "v": AURORA[1],
             "n": NAVY[1], "N": NAVY[2], "m": STEEL[0], "M": STEEL[1], "t": STEEL[2]}
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["w.", ".j"], L, 9, 9)
        vs.append(v)                                                   # 2 bubbles
        v = b.copy()
        stamp(v, ["S...", ".S..", "..U."], L, 3, 2)
        vs.append(v)                                                   # 3 streak
        v = b.copy()
        stamp(v, ["k...", ".kk.", "...k"], L, 9, 4)
        vs.append(v)                                                   # 4 fracture
        v = b.copy()
        stamp(v, ["j.j", "...", ".w."], L, 11, 10)
        vs.append(v)                                                   # 5 bubble trail
        v = b.copy()
        stamp(v, ["SS", "S."], L, 2, 10)
        vs.append(v)                                                   # 6 frost flake
        v = b.copy()
        stamp(v, ["d.", ".k"], L, 6, 2)
        vs.append(v)                                                   # 7 dark inclusion
        v = b.copy()
        stamp(v, ["..S", ".U.", "U.."], L, 12, 1)
        vs.append(v)                                                   # 8 glint
        v = b.copy()
        stamp(v, ["k..k", ".kk."], L, 4, 8)
        vs.append(v)                                                   # 9 crack
        # --- detailed
        v = b.copy()
        stamp(v, ["W.......", ".wj.....", "...jw...", ".....wj.", ".......j"], L, 3, 5)
        vs.append(v)                                                   # 10 long refraction
        v = b.copy()
        stamp(v, ["..oo..", ".onNo.", "onNNmo", ".oNmo.", "..oo.."], L, 5, 5)
        vs.append(v)                                                   # 11 frozen stone
        v = b.copy()
        stamp(v, [".ooooo.", "omMtMmo", "omtMmmo", ".ooooo."], L, 4, 7)
        vs.append(v)                                                   # 12 frozen gear/relic
        v = b.copy()
        stamp(v, ["..c..", ".cwc.", "cwWwc", ".cwc.", "..c.."], L, 6, 5)
        vs.append(v)                                                   # 13 frost glyph (glow)
        v = b.copy()
        stamp(v, ["g.......", ".gv.....", "...vg...", ".....gv."], L, 3, 9)
        vs.append(v)                                                   # 14 aurora reflection
        v = b.copy()
        stamp(v, ["...w..", "..wjo.", ".wjIio", "wjIiio", "oIiioo", ".ooo.."], L, 5, 6)
        vs.append(v)                                                   # 15 crystal inclusion
        return vs

    def top_decor(self):
        return TD.glacier_ice()


# ===========================================================================
# snow_rock: blue-grey stratified rock under a thick snow cap
# ===========================================================================
class SnowRock(Material):
    name = "snow_rock"
    map_color = "#c9d8ea"
    ramp = ["#0f1626", "#1b253b", "#29364f", "#3b4a66", "#536380", "#7486a2"]
    left_band = [(2, False), (1, False), (1, True)]
    right_band = [(-1, False), (-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False), (-1, False), (-1, True)]
    #          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    top_th = [6, 6, 5, 5, 6, 7, 7, 6, 5, 5, 6, 8, 8, 7, 6, 6]
    drape_l = (9, 6)
    drape_r = (6, 4)
    outline_color = OUT_G
    # strata: (row of the dark bedding line, phase of its wave)
    BEDS = [(3, 0.0), (8, 2.0), (13, 4.5)]
    CRACKS = [(5, 4, 7), (12, 9, 12), (2, 14, 15), (9, 0, 2)]      # (x, y0, y1) vertical joints

    def bed_y(self, k, x):
        r, ph = self.BEDS[k]
        return int(round(r + 0.7 * math.sin(2 * math.pi * (x + ph) / TS)))

    def make_base_idx(self):
        idx = np.full((TS, TS), 3, int)
        for x in range(TS):
            for k in range(len(self.BEDS)):
                by = self.bed_y(k, x) % TS
                idx[by, x] = 1
                idx[(by + 1) % TS, x] = 4 if x % 5 else 5
                idx[(by - 1) % TS, x] = 2
            # gentle shading inside each stratum: darker towards its bottom
        for (x, y0, y1) in self.CRACKS:
            for y in range(y0, y1 + 1):
                if idx[y % TS, x] in (2, 3, 4):
                    idx[y % TS, x] = 1
                    if idx[y % TS, (x + 1) % TS] == 3:
                        idx[y % TS, (x + 1) % TS] = 4
        for (x, y) in [(3, 6), (11, 1), (14, 11), (7, 10), (1, 15)]:
            if idx[y, x] == 3:
                idx[y, x] = 2
        for (x, y) in [(10, 6), (6, 1), (3, 11)]:
            if idx[y, x] == 3:
                idx[y, x] = 4
        return idx

    def add_base_details(self, img):
        # frost caught in a few cracks / bedding lines
        for (x, y, c) in [(5, 5, SNOW_SH[1]), (12, 10, SNOW_SH[0]), (0, 9, SNOW_SH[0])]:
            _px(img, x, y, c)

    def top_color(self, x, d, th):
        if d == 0:
            return SNOW[2]
        if d == 1:
            return SNOW[2] if x % 4 in (1, 2) else SNOW[1]
        if d == th - 1:
            return SNOW_SH[0]
        if d == th - 2:
            return SNOW_SH[1] if (x + th) % 3 else SNOW[0]
        return SNOW[1] if d <= 2 else SNOW[0]

    def drape_color(self, side, k, d, n):
        if d == n - 1:
            return SNOW_SH[0]
        if side == "l":
            return SNOW[1] if k == 0 else SNOW[0]
        return SNOW[0] if k == 0 else SNOW_SH[1]

    def post_cell(self, img, g):
        # frost rime on the overhang underside
        for y in range(TS):
            for x in range(TS):
                if g.solid[y, x] and g.db[y, x] == 1 and x in (2, 9, 10):
                    img[y, x] = col(SNOW_SH[0])

    def variants(self):
        b = self.base
        L = {"o": OUT_G, "d": self.ramp[0], "k": self.ramp[1], "s": self.ramp[2],
             "u": self.ramp[3], "U": self.ramp[4], "S": self.ramp[5], "f": SNOW_SH[0],
             "F": SNOW_SH[1], "n": SNOW[0], "N": SNOW[2], "i": ICE[0], "I": ICE[1], "j": ICE[2],
             "w": ICE[3], "c": FROST, "g": AURORA[0], "v": AURORA[1], "b": "#2a2230",
             "B": "#4a3a48", "y": "#c8741f", "Y": "#ffb347"}
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["k.", ".U"], L, 10, 11)
        vs.append(v)                                                   # 2 pit
        v = b.copy()
        stamp(v, ["F", "f"], L, 7, 5)
        vs.append(v)                                                   # 3 frost speck
        v = b.copy()
        stamp(v, ["k...", ".kk.", "...k"], L, 1, 5)
        vs.append(v)                                                   # 4 crack
        v = b.copy()
        stamp(v, [".U.", "UsU"], L, 12, 5)
        vs.append(v)                                                   # 5 pebble
        v = b.copy()
        stamp(v, ["ff", "f."], L, 13, 0)
        vs.append(v)                                                   # 6 rime
        v = b.copy()
        stamp(v, ["kk", "kd"], L, 4, 10)
        vs.append(v)                                                   # 7 hole
        v = b.copy()
        stamp(v, ["S..", ".U."], L, 8, 1)
        vs.append(v)                                                   # 8 glint
        v = b.copy()
        stamp(v, ["d.d"], L, 6, 11)
        vs.append(v)                                                   # 9 pores
        # --- detailed
        v = b.copy()
        stamp(v, ["j......", ".Iw....", "...wI..", ".....jI"], L, 4, 5)
        vs.append(v)                                                   # 10 ice vein
        v = b.copy()
        stamp(v, ["..w", ".wj", "wjI", "oIo"], L, 10, 9)
        stamp(v, ["w", "j"], L, 8, 11)
        vs.append(v)                                                   # 11 ice crystals
        v = b.copy()
        stamp(v, [".sss.", "sUSUs", "sUsUs", "sU.Us", ".sUs."], L, 6, 4)
        vs.append(v)                                                   # 12 fossil
        v = b.copy()
        stamp(v, ["..c..", ".c.c.", "c.w.c", ".c.c.", "..c.."], L, 5, 9)
        vs.append(v)                                                   # 13 frost rune (glow)
        v = b.copy()
        stamp(v, ["bB.", "BbB", ".bb"], L, 11, 4)
        vs.append(v)                                                   # 14 frozen root
        v = b.copy()
        stamp(v, ["ooo", "oYo", "oyo"], L, 2, 9)
        vs.append(v)                                                   # 15 ancient amber lamp
        return vs

    def top_decor(self):
        return TD.snow_rock()


# ===========================================================================
# frost_metal: pale steel plates rimmed with frost, cyan light strips
# ===========================================================================
class FrostMetal(Material):
    name = "frost_metal"
    map_color = "#5c6b80"
    ramp = ["#141a26", "#232d3e", "#3d4a5e", "#5c6b80", "#7d8da3", "#aab8c9"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-1, False), (-1, False)]
    under_lip = [(-1, False)]
    top_th = [5] * 16
    outline_color = OUT_G
    STRIP = {2: "#2fb8c8", 3: FROST, 4: "#d6fdff", 5: FROST, 6: "#2fb8c8",
             10: "#2fb8c8", 11: FROST, 12: "#d6fdff", 13: FROST, 14: "#2fb8c8"}

    def make_base_idx(self, bolts=True, conduit=True):
        idx = np.full((TS, TS), 3, int)
        # plate rows 0-10 with a bevel; seam at row 10 and x=15 / x=7 (staggered)
        idx[0, :] = 4
        idx[0:10, 0] = 4
        idx[0, 0] = 5
        idx[1, 1:15] = 4
        idx[0:10, 15] = 1
        idx[10, :] = 1
        idx[9, 1:15] = 2
        # brushed-steel streaks
        for (x, y, n) in [(3, 4, 4), (9, 6, 3), (5, 8, 2)]:
            for i in range(n):
                idx[y, x + i] = 4
        if bolts:
            for (bx, by) in [(2, 3), (12, 3), (2, 7), (12, 7)]:
                idx[by, bx], idx[by, bx + 1] = 5, 3
                idx[by + 1, bx], idx[by + 1, bx + 1] = 3, 1
        # conduit band rows 11-15: recessed channel with a light strip
        if conduit:
            idx[11, :] = 0
            idx[12, :] = 1
            idx[13, :] = 2
            idx[14, :] = 2
            idx[15, :] = 1
            for x in (7, 8):
                idx[11:16, x] = 3 if x == 7 else 2   # bracket
            idx[11, 7] = 4
        else:
            idx[11, :] = 4
            idx[15, :] = 1
        return idx

    def add_base_details(self, img):
        # cyan light dots in the conduit
        for x in (2, 3, 4, 11, 12, 13):
            _px(img, x, 13, "#2fb8c8" if x in (2, 4, 11, 13) else FROST)
        _px(img, 3, 12, "#1a6a7a")
        _px(img, 12, 12, "#1a6a7a")

    def top_color(self, x, d, th):
        if d == 0:
            return SNOW[2] if x % 6 in (1, 2, 3) else ICE[3]
        if d == 1:
            return ICE[2] if x % 2 else self.ramp[5]
        if d == 2:
            return self.ramp[1]
        if d == 3:
            return self.STRIP.get(x, self.ramp[0])
        return self.ramp[0]

    def drape_color(self, side, k, d, n):
        return None

    drape_l = (3, 1)
    drape_r = (2, 0)

    def post_cell(self, img, g):
        # frost rime clinging to the exposed sides and underside
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                if g.dl[y, x] == 1 and (y % 3 == 0) and g.dt[y, x] > 4:
                    img[y, x] = col(ICE[2])
                if g.dr[y, x] == 1 and (y % 4 == 1) and g.dt[y, x] > 4:
                    img[y, x] = col(SNOW_SH[1])
                if g.db[y, x] == 1 and x % 5 == 2:
                    img[y, x] = col(SNOW_SH[1])

    def variants(self):
        C = self.colorize
        b = self.base
        L = {"o": OUT_G, "d": self.ramp[0], "m": self.ramp[1], "n": self.ramp[2],
             "M": self.ramp[3], "N": self.ramp[4], "h": self.ramp[5], "c": "#2fb8c8", "C": FROST,
             "W": "#d6fdff", "j": ICE[2], "w": ICE[3], "f": SNOW_SH[1], "F": SNOW[1],
             "g": AURORA[0], "v": AURORA[1], "a": "#1a6a7a"}
        plain = C(self.make_base_idx(bolts=False))
        sealed = C(self.make_base_idx(conduit=False))
        vs = [b.copy(), b.copy(), plain.copy()]
        v = b.copy()
        stamp(v, ["N..", ".n.", "..n"], L, 6, 4)
        vs.append(v)                                                   # 3 scratch
        v = b.copy()
        stamp(v, ["..j.", ".jwj", "j.j."], L, 7, 2)
        vs.append(v)                                                   # 4 frost patch
        v = b.copy()
        stamp(v, [".n.", "nmN", ".N."], L, 8, 5)
        vs.append(v)                                                   # 5 dent
        v = b.copy()
        stamp(v, ["mmo", "o.d"], L, 12, 7)
        vs.append(v)                                                   # 6 missing bolt
        v = sealed.copy()
        vs.append(v)                                                   # 7 sealed conduit
        v = b.copy()
        stamp(v, ["f", "F", "f"], L, 5, 4)
        vs.append(v)                                                   # 8 frost streak
        v = b.copy()
        stamp(v, ["dddddd", "NNNNNN", "dddddd"], L, 5, 3)
        vs.append(v)                                                   # 9 vent grille
        # --- detailed
        v = b.copy()
        stamp(v, ["oooooo", "ocCCco", "oCWWCo", "ocCCco", "oooooo"], L, 5, 2)
        vs.append(v)                                                   # 10 cyan display
        v = b.copy()
        stamp(v, ["o....", ".oc..", "..oCo", "...ow"], L, 6, 3)
        vs.append(v)                                                   # 11 crack with light
        v = plain.copy()
        stamp(v, ["c.c.c", ".cCc.", "cCWCc", ".cCc.", "c.c.c"], L, 5, 2)
        vs.append(v)                                                   # 12 cyan glyph
        v = b.copy()
        stamp(v, ["..w..", ".wjw.", "wjwjw", ".wjw.", "..w.."], L, 5, 3)
        vs.append(v)                                                   # 13 frost star
        v = b.copy()
        stamp(v, ["oooo", "ogvo", "ovgo", "oooo"], L, 6, 3)
        vs.append(v)                                                   # 14 aurora gem
        v = sealed.copy()
        stamp(v, ["d.d.d", "C.c.C"], L, 5, 5)
        vs.append(v)                                                   # 15 status lights
        return vs

    def top_decor(self):
        return TD.frost_metal()


MATERIALS = [GlacierIce, SnowRock, FrostMetal]
