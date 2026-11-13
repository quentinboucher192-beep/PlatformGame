"""Pyralis terrain materials (volcanic forge world): basalt, obsidian, forge_metal.

Same autotile engine as the Sylvanox materials (tiles.Material): a 16-periodic interior
pattern expressed as indices into a colour ramp, periodic top-lip profiles and edge bands,
so connected cells always continue each other's pattern exactly.
"""
import numpy as np

import tile_decor_pyralis as TD
from helpers import col
from sprites import stamp
from tiles import Material, TS

# Pyralis palette (CONTENT_CONTRACT section 1)
LAVA = ["#b3201a", "#ff4d1e", "#ff8a3d", "#ffc56b", "#fff1c2"]
OBS = ["#5b2a6e", "#9b4dca", "#c99af0", "#f2e2ff"]
RUST = ["#6e3b2a", "#a0583a", "#d08a5a"]
ASH = ["#3a2c2c", "#5a4846", "#7e6a64", "#a8938a", "#cbb8ac"]
EMBER_DIM = "#6e1a12"
OUT_P = "#0d0607"


def _px(img, x, y, c):
    img[y % TS, x % TS] = col(c)


# ===========================================================================
# basalt: dark columnar volcanic rock, glowing lava seams, ash crust on top
# ===========================================================================
class Basalt(Material):
    name = "basalt"
    map_color = "#5a2b22"
    ramp = ["#110a0b", "#1e1416", "#2d2022", "#41302f", "#58403b", "#78584e"]
    left_band = [(2, False), (1, False), (1, True)]
    right_band = [(-1, False), (-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False), (-1, True)]
    #          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    top_th = [4, 4, 5, 5, 5, 4, 4, 5, 6, 6, 5, 4, 4, 5, 5, 4]
    drape_l = (7, 4)
    drape_r = (4, 3)
    outline_color = OUT_P
    # rock chunks: periodic Voronoi seeds (x, y) on the 16x16 torus
    SEEDS = [(2.5, 2.5), (9.5, 1.5), (14.0, 6.5), (6.0, 8.0), (11.0, 12.5), (2.0, 13.0)]
    # crevices between these chunk pairs glow with lava light
    GLOW = {(3, 4), (0, 3), (2, 4)}

    def voronoi(self):
        """Per pixel: nearest seed, gap to the second nearest, offset from the nearest seed."""
        near = np.zeros((TS, TS), int)
        gap = np.zeros((TS, TS))
        second = np.zeros((TS, TS), int)
        off = np.zeros((TS, TS, 2))
        for y in range(TS):
            for x in range(TS):
                ds = []
                for k, (sx, sy) in enumerate(self.SEEDS):
                    dx = (x + 0.5 - sx + 8) % 16 - 8
                    dy = (y + 0.5 - sy + 8) % 16 - 8
                    ds.append(((dx * 0.85) ** 2 + (dy * 1.1) ** 2, k, dx, dy))
                ds.sort()
                near[y, x] = ds[0][1]
                second[y, x] = ds[1][1]
                gap[y, x] = ds[1][0] ** 0.5 - ds[0][0] ** 0.5
                off[y, x] = ds[0][2], ds[0][3]
        return near, second, gap, off

    def make_base_idx(self):
        near, second, gap, off = self.voronoi()
        crev = gap < 0.85
        idx = np.full((TS, TS), 3, int)
        for y in range(TS):
            for x in range(TS):
                if crev[y, x]:
                    idx[y, x] = 0 if gap[y, x] < 0.4 else 1
                    continue
                dx, dy = off[y, x]
                lit = -(dx * 0.7 + dy * 0.75) / 4.5          # >0 towards the upper-left
                f = 3.0 + 1.1 * lit
                up = crev[(y - 1) % TS, x] or crev[y, (x - 1) % TS]
                dn = crev[(y + 1) % TS, x] or crev[y, (x + 1) % TS]
                if up and not dn:
                    f = 4.6 if lit > -0.2 else 4.0           # lit rim on the upper-left
                elif dn and not up:
                    f = 1.7                                  # shaded lower-right rim
                lo = int(np.floor(f))
                fr = f - lo
                v = lo + (1 if fr > 0.62 or (fr > 0.38 and (x + y) % 2) else 0)
                idx[y, x] = int(np.clip(v, 1, 5))
        # pores
        for (x, y) in [(4, 4), (12, 9), (8, 14)]:
            if not crev[y, x]:
                idx[y, x] = 1
        return idx

    def paint_glow(self, img, pairs, hot=False):
        """Lava light inside the crevices between the given chunk pairs (hot = bright core)."""
        near, second, gap, off = self.voronoi()
        for y in range(TS):
            for x in range(TS):
                pair = tuple(sorted((int(near[y, x]), int(second[y, x]))))
                if gap[y, x] < 0.85 and pair in pairs:
                    if hot:
                        c = LAVA[2] if gap[y, x] < 0.4 else LAVA[0]
                        if gap[y, x] < 0.4 and (x + y) % 3 == 0:
                            c = LAVA[3]
                    else:
                        c = "#8a2414" if gap[y, x] < 0.4 else "#5a1410"
                    img[y, x] = col(c)
        return img

    def add_base_details(self, img):
        self.paint_glow(img, self.GLOW)


    def top_color(self, x, d, th):
        if d == 0:
            return ASH[4] if x in (2, 3, 9, 14) else ASH[3]
        if d == 1:
            return ASH[3] if x in (1, 2, 8, 9, 10, 13) else ASH[2]
        if d == 2:
            return ASH[2] if x % 3 == 0 else ASH[1]
        if d == th - 1:
            return ASH[0]
        return ASH[1] if (x + d) % 2 else ASH[0]

    def drape_color(self, side, k, d, n):
        if d == n - 1:
            return ASH[0]
        if side == "l":
            return ASH[2] if k == 0 else ASH[1]
        return ASH[1] if k == 0 else ASH[0]

    def post_cell(self, img, g):
        # molten drips glowing on overhang undersides (lava light from below)
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                if g.db[y, x] == 1 and x in (5, 6, 12):
                    img[y, x] = col(LAVA[0] if x != 6 else EMBER_DIM)
                if g.db[y, x] == 2 and x == 5:
                    img[y, x] = col(EMBER_DIM)

    def variants(self):
        L = {"o": OUT_P, "d": self.ramp[0], "k": self.ramp[1], "s": self.ramp[2], "u": self.ramp[3],
             "U": self.ramp[4], "S": self.ramp[5], "r": LAVA[0], "R": LAVA[1], "a": LAVA[2],
             "y": LAVA[3], "W": LAVA[4], "e": EMBER_DIM, "v": OBS[0], "V": OBS[1], "p": OBS[2],
             "1": ASH[1], "2": ASH[2], "3": ASH[3]}
        raw = self.colorize(self.base_idx)

        def with_glow(pairs, hot=False):
            return self.paint_glow(raw.copy(), pairs, hot)

        b = self.base
        vs = [b.copy(), with_glow({(0, 1), (4, 5)}), with_glow({(2, 3), (1, 4), (0, 5)})]
        v = raw.copy()
        stamp(v, ["2312"], L, 10, 9)
        vs.append(v)                                                   # 3 cold, ash in crevice
        v = b.copy()
        stamp(v, ["k.k", ".k."], L, 5, 6)
        vs.append(v)                                                   # 4 pores
        v = with_glow({(0, 1), (4, 5)})
        stamp(v, ["k..", ".kk", "...k"], L, 9, 11)
        vs.append(v)                                                   # 5 hairline crack
        v = with_glow({(2, 3), (1, 4)})
        stamp(v, ["1", "2"], L, 6, 11)
        vs.append(v)                                                   # 6 ash streak
        v = b.copy()
        stamp(v, ["kk", "kU"], L, 13, 7)
        vs.append(v)                                                   # 7 chipped chunk
        v = with_glow({(0, 1), (4, 5)})
        stamp(v, ["V", "p"], L, 10, 1)
        vs.append(v)                                                   # 8 obsidian speck
        v = with_glow({(2, 3), (1, 4), (0, 5)})
        stamp(v, ["S.", ".U"], L, 1, 11)
        vs.append(v)                                                   # 9 glint
        # --- detailed variants (lava)
        v = b.copy()
        stamp(v, ["e.......", ".rR.....", "...Ra...", "....aR..", "......Re"], L, 3, 5)
        vs.append(v)                                                   # 10 lava vein
        v = with_glow({(0, 1)})
        stamp(v, ["..e..", ".rRr.", "eRyRe", ".rRr.", "..e.."], L, 8, 10)
        vs.append(v)                                                   # 11 magma pocket
        v = with_glow({(0, 1), (2, 3)})
        v = self.paint_glow(v, {(4, 5)}, hot=True)
        vs.append(v)                                                   # 12 white-hot crevice
        v = with_glow({(0, 1), (4, 5)})
        stamp(v, ["ooo.", "oRao", "oayo", ".oo."], L, 11, 5)
        vs.append(v)                                                   # 13 ember cavity
        v = b.copy()
        stamp(v, ["..p", ".pV", "pVv", "ovo"], L, 8, 0)
        vs.append(v)                                                   # 14 obsidian chip
        v = with_glow({(2, 3)}, hot=True)
        stamp(v, ["e....", "rR...", "..aRr", "....R"], L, 0, 11)
        vs.append(v)                                                   # 15 molten fork
        return vs

    def top_decor(self):
        return TD.basalt()


# ===========================================================================
# obsidian: glossy black-violet volcanic glass with sharp specular glints
# ===========================================================================
class Obsidian(Material):
    name = "obsidian"
    map_color = "#5b2a6e"
    ramp = ["#07040b", "#110a18", "#1b1026", "#281738", "#3d2152", "#5b2a6e"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-1, False), (-1, False)]
    under_lip = [(-1, False)]
    top_th = [2, 2, 3, 3, 2, 2, 2, 3, 4, 3, 2, 2, 3, 3, 2, 2]
    drape_l = (5, 3)
    drape_r = (3, 2)
    outline_color = "#05030a"

    # glass shards: periodic Voronoi facets, flat-shaded, with lit / dark ridges
    SEEDS = [(3.0, 3.0, 1), (11.0, 2.0, 2), (7.5, 9.0, 3), (14.5, 10.5, 1), (2.0, 12.5, 2),
             (11.0, 15.0, 2)]

    def facets(self):
        near = np.zeros((TS, TS), int)
        gap = np.zeros((TS, TS))
        nrm = np.zeros((TS, TS, 2))
        off = np.zeros((TS, TS, 2))
        for y in range(TS):
            for x in range(TS):
                ds = []
                for k, (sx, sy, _t) in enumerate(self.SEEDS):
                    dx = (x + 0.5 - sx + 8) % 16 - 8
                    dy = (y + 0.5 - sy + 8) % 16 - 8
                    ds.append((dx * dx * 0.7 + dy * dy, k, dx, dy))
                ds.sort()
                near[y, x] = ds[0][1]
                gap[y, x] = ds[1][0] ** 0.5 - ds[0][0] ** 0.5
                # direction from this facet's seed towards the neighbouring facet's seed
                nrm[y, x] = (ds[0][2] - ds[1][2], ds[0][3] - ds[1][3])
                off[y, x] = ds[0][2], ds[0][3]
        return near, gap, nrm, off

    def make_base_idx(self):
        near, gap, nrm, off = self.facets()
        idx = np.zeros((TS, TS), int)
        for y in range(TS):
            for x in range(TS):
                tone = self.SEEDS[near[y, x]][2]
                dx, dy = off[y, x]
                f = tone + 0.5 * (-(dx * 0.6 + dy * 0.8) / 5.0)
                v = int(np.floor(f + (0.5 if (x + y) % 2 else 0.35)))
                if gap[y, x] < 0.9:
                    nx, ny = nrm[y, x]
                    v = 4 if (nx + ny) > 0 else 0     # ridge: lit on the far side of the light
                idx[y, x] = int(np.clip(v, 0, 4))
        return idx

    def add_base_details(self, img):
        # specular streaks (glass catches the light), a soft sheen and one lava reflection
        for (x, y, c) in [(4, 5, OBS[1]), (5, 4, OBS[2]), (6, 3, OBS[1]),
                          (9, 12, OBS[0]), (10, 11, OBS[1]), (11, 10, OBS[0]),
                          (13, 5, OBS[0]), (1, 9, "#7a2a1e")]:
            _px(img, x, y, c)

    def top_color(self, x, d, th):
        if d == 0:
            return OBS[3] if x in (3, 4, 11) else (OBS[2] if x % 4 != 1 else OBS[1])
        if d == 1:
            return OBS[1] if x % 3 else OBS[0]
        if d == th - 1:
            return self.ramp[4]
        return OBS[0]

    def drape_color(self, side, k, d, n):
        if side == "l":
            if d == 0 and k == 0:
                return OBS[2]
            return OBS[1] if k == 0 else OBS[0]
        return OBS[0] if k == 0 else self.ramp[4]

    def post_cell(self, img, g):
        # warm lava glints reflected on the underside
        for y in range(TS):
            for x in range(TS):
                if g.solid[y, x] and g.db[y, x] == 1 and x in (5, 6, 13):
                    img[y, x] = col("#7a2a1e" if x != 6 else LAVA[0])

    def variants(self):
        b = self.base
        L = {"o": "#05030a", "d": self.ramp[0], "k": self.ramp[1], "s": self.ramp[2],
             "u": self.ramp[3], "U": self.ramp[4], "S": self.ramp[5], "v": OBS[0], "V": OBS[1],
             "p": OBS[2], "P": OBS[3], "r": LAVA[0], "R": LAVA[1], "a": LAVA[2], "y": LAVA[3],
             "e": "#7a2a1e"}
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["V.", ".p"], L, 9, 8)
        vs.append(v)                                                   # 2 glint
        v = b.copy()
        stamp(v, ["d...", ".dd.", "...d"], L, 2, 5)
        vs.append(v)                                                   # 3 crack
        v = b.copy()
        stamp(v, ["..V", ".Vp", "vV."], L, 11, 11)
        vs.append(v)                                                   # 4 sheen
        v = b.copy()
        stamp(v, ["e.", ".e"], L, 6, 6)
        vs.append(v)                                                   # 5 warm reflection
        v = b.copy()
        stamp(v, ["kk", "kd"], L, 8, 12)
        vs.append(v)                                                   # 6 pit
        v = b.copy()
        stamp(v, ["U...", ".U..", "..UU"], L, 1, 9)
        vs.append(v)                                                   # 7 ridge glint
        v = b.copy()
        stamp(v, ["d..", ".d.", ".d.", "..d"], L, 13, 2)
        vs.append(v)                                                   # 8 crack 2
        v = b.copy()
        stamp(v, ["v", ".", "V"], L, 4, 10)
        vs.append(v)                                                   # 9 specks
        # --- detailed
        v = b.copy()
        stamp(v, ["P.......", ".pV.....", "..Vp....", "....pV..", ".....Vv."], L, 3, 4)
        vs.append(v)                                                   # 10 long specular streak
        v = b.copy()
        stamp(v, ["...P..", "..pPo.", ".pVVvo", "pPVvvo", "oVvvoo", ".ooo.."], L, 5, 6)
        vs.append(v)                                                   # 11 glass shard
        v = b.copy()
        stamp(v, ["d.....", ".dr...", "..dRe.", "....ad", ".....d"], L, 4, 6)
        vs.append(v)                                                   # 12 lava-lit crack
        v = b.copy()
        stamp(v, [".ooo.", "oVpVo", "opPpo", "oVpVo", ".ooo."], L, 6, 6)
        vs.append(v)                                                   # 13 geode
        v = b.copy()
        stamp(v, ["..a.", ".aya", "..a.", "..e."], L, 10, 9)
        vs.append(v)                                                   # 14 trapped ember
        v = b.copy()
        stamp(v, [".p...p", "pVo.pV", "oo..oo"], L, 4, 9)
        vs.append(v)                                                   # 15 twin shards
        return vs

    def top_decor(self):
        return TD.obsidian()


# ===========================================================================
# forge_metal: dark iron plates, hot orange rivets, glowing radiator vents
# ===========================================================================
class ForgeMetal(Material):
    name = "forge_metal"
    map_color = "#a0583a"
    ramp = ["#0c0a0c", "#181417", "#262024", "#383034", "#51464a", "#726468"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-1, False), (-1, False)]
    under_lip = [(-1, False)]
    top_th = [5] * 16
    outline_color = OUT_P
    STRIP = {1: LAVA[0], 2: LAVA[1], 3: LAVA[2], 4: LAVA[3], 5: LAVA[2], 6: LAVA[1], 7: LAVA[0],
             9: LAVA[0], 10: LAVA[1], 11: LAVA[2], 12: LAVA[3], 13: LAVA[2], 14: LAVA[1],
             15: LAVA[0]}

    def make_base_idx(self, rivets=True, ribs=True):
        idx = np.full((TS, TS), 3, int)
        # plate rows 0-9: bevelled top/left, seams at row 9 / col 15
        idx[0, :] = 4
        idx[0:9, 0] = 4
        idx[0, 0] = 5
        idx[9, :] = 1
        idx[0:9, 15] = 1
        idx[1, 1:15] = 3
        # subtle plate curvature: darker lower half
        for y in range(5, 9):
            for x in range(1, 15):
                if (x + y) % 2 == 0 or y >= 7:
                    idx[y, x] = 2 if y >= 7 else idx[y, x]
        if rivets:
            for rx in (2, 12):
                idx[3, rx], idx[3, rx + 1] = 5, 4
                idx[4, rx], idx[4, rx + 1] = 4, 1
        # radiator band rows 10-15: vertical ribs
        for y in range(10, 16):
            for x in range(TS):
                k = x % 4
                v = {0: 4, 1: 3, 2: 2, 3: 0}[k]
                if not ribs:
                    v = 3 if y > 10 else 4
                if y == 10:
                    v = 4 if k != 3 else 1
                if y == 15:
                    v = min(v, 1) if k == 3 else 2
                idx[y, x] = v
        return idx

    def add_base_details(self, img):
        # dull heat glow at the bottom of each radiator gap
        for x in range(3, TS, 4):
            _px(img, x, 13, "#3a1210")
            _px(img, x, 14, EMBER_DIM)
        # hot rivet centres
        for rx in (2, 12):
            _px(img, rx, 3, RUST[2])

    def top_color(self, x, d, th):
        if d == 0:
            return RUST[2] if x % 8 in (2, 3) else self.ramp[5]
        if d == 1:
            return self.ramp[4]
        if d == 2:
            return self.ramp[1]
        if d == 3:
            return self.STRIP.get(x, self.ramp[0])
        return self.ramp[0]

    def post_cell(self, img, g):
        # glowing slag drips under overhangs + heat-blued rim on the exposed right side
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                if g.db[y, x] == 2 and x == 9:
                    img[y, x] = col(LAVA[1])
                if g.db[y, x] == 1 and x == 9:
                    img[y, x] = col(LAVA[2])
                if g.dr[y, x] == 1 and y % 5 == 2 and g.dt[y, x] > 6:
                    img[y, x] = col("#3a2a48")

    def variants(self):
        C = self.colorize
        b = self.base
        L = {"o": OUT_P, "m": self.ramp[1], "d": self.ramp[0], "n": self.ramp[2], "M": self.ramp[3],
             "N": self.ramp[4], "h": self.ramp[5], "r": LAVA[0], "R": LAVA[1], "a": LAVA[2],
             "y": LAVA[3], "W": LAVA[4], "1": RUST[0], "2": RUST[1], "3": RUST[2],
             "t": "#3a2a48", "T": "#5a3a6a", "e": EMBER_DIM}
        plain = C(self.make_base_idx(rivets=False))
        smooth = C(self.make_base_idx(ribs=False))
        vs = [b.copy(), b.copy(), plain.copy()]
        v = b.copy()
        stamp(v, ["N..", ".n.", "..n"], L, 6, 3)
        vs.append(v)                                                   # 3 scratch
        v = b.copy()
        stamp(v, ["..1.", ".121", "1.1.", ".1.."], L, 8, 4)
        vs.append(v)                                                   # 4 rust patch
        v = b.copy()
        stamp(v, ["t.t", ".T."], L, 6, 6)
        vs.append(v)                                                   # 5 heat tint
        v = b.copy()
        stamp(v, [".n.", "nmN", ".N."], L, 7, 3)
        vs.append(v)                                                   # 6 dent
        v = b.copy()
        stamp(v, ["mmo", "o.d"], L, 12, 3)
        vs.append(v)                                                   # 7 missing rivet
        v = b.copy()
        stamp(v, ["1", "2", "1", "1"], L, 5, 5)
        vs.append(v)                                                   # 8 rust streak
        v = smooth.copy()
        vs.append(v)                                                   # 9 sealed radiator
        # --- detailed
        v = b.copy()
        stamp(v, ["oooooooo", "orRaRaRo", "oRayyaRo", "oooooooo"], L, 4, 3)
        vs.append(v)                                                   # 10 glowing vent slot
        v = b.copy()
        stamp(v, ["..a", ".aR", "aR.", "R.."], L, 3, 11)
        stamp(v, ["..a", ".aR", "aR."], L, 11, 11)
        vs.append(v)                                                   # 11 white-hot radiator
        v = smooth.copy()
        stamp(v, ["..oo..", ".oRRo.", "oRyyRo", "oRyyRo", ".oRRo.", "..oo.."], L, 5, 2)
        vs.append(v)                                                   # 12 furnace porthole
        v = b.copy()
        stamp(v, ["ttt.", "tTTt", ".ttt"], L, 5, 4)
        stamp(v, ["3"], L, 6, 5)
        vs.append(v)                                                   # 13 heat-blued plate
        v = b.copy()
        stamp(v, ["r.r.r", ".a.a.", "r.r.r"], L, 5, 4)
        vs.append(v)                                                   # 14 glowing glyph
        v = b.copy()
        stamp(v, ["d.d.d", "R.a.R"], L, 5, 5)
        vs.append(v)                                                   # 15 hot bolt row
        return vs

    def top_decor(self):
        return TD.forge_metal()


MATERIALS = [Basalt, Obsidian, ForgeMetal]
