"""The four Sylvanox terrain materials: ruin_stone, ancient_metal, crystal, root."""
import math

import numpy as np

import tile_decor
from helpers import P
from sprites import stamp
from tiles import Material, TS

MOSS = ["#0f3b3a", "#135c50", "#1f8a6a", "#35b583"]
CY = ["#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
VI = ["#6a2bb8", "#b04cff", "#e08aff", "#f4d4ff"]
AM = ["#c8741f", "#ffb347", "#ffd98a"]
O = P.OUTLINE


def set_idx(idx, pts, v):
    for x, y in pts:
        idx[y % TS, x % TS] = v


# ===========================================================================
# ruin_stone: dark teal carved stone blocks, glowing cyan moss on top
# ===========================================================================
class RuinStone(Material):
    name = "ruin_stone"
    map_color = "#3b5466"
    ramp = ["#111827", "#1d2b3a", "#2a3e4f", "#3b5466", "#577286", "#7d97a8"]
    left_band = [(2, False), (1, False), (1, True)]
    right_band = [(-1, False), (-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False), (-1, True)]
    #          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    top_th = [5, 5, 4, 4, 5, 7, 5, 4, 4, 5, 5, 4, 4, 6, 8, 6]
    drape_l = (10, 8)
    drape_r = (7, 6)
    GLOW0 = {2: CY[1], 9: CY[1], 12: CY[2]}
    GLOW1 = {3: CY[0], 9: CY[0], 10: CY[1]}

    # Courses: mortar rows 5 and 13. Course A rows 6-12 (joint x=3), course B rows 14-4 (x=11)
    def make_base_idx(self, joints_a=(3,), joints_b=(11,)):
        idx = np.full((TS, TS), 2, int)
        for y in range(TS):
            for x in range(TS):
                if y in (5, 13):
                    idx[y, x] = 1
                    continue
                if 6 <= y <= 12:
                    joints, top = joints_a, 6
                else:
                    joints, top = joints_b, 14
                if x in joints:
                    idx[y, x] = 1
                    continue
                by = (y - top) % TS
                left_bevel = ((x - 1) % TS) in joints
                if by == 0 and left_bevel:
                    idx[y, x] = 4
                elif by == 0 or left_bevel:
                    idx[y, x] = 3
        # pits (dark hole with lit lower-right rim) and specks
        for (x, y) in [(8, 9), (14, 2), (6, 1), (1, 10)]:
            idx[y, x] = 1
            idx[y + 1, x + 1] = 3
        for (x, y) in [(13, 8), (5, 15), (2, 3), (10, 11)]:
            idx[y, x] = 3
        return idx

    def top_color(self, x, d, th):
        if d == 0:
            return self.GLOW0.get(x, MOSS[3])
        if d == 1:
            return self.GLOW1.get(x, MOSS[3] if x % 3 == 0 else MOSS[2])
        if d == 2:
            return MOSS[2] if x % 2 == 0 else MOSS[1]
        if th >= 6:
            if d == th - 1:
                return CY[1]
            if d == th - 2:
                return CY[0]
        if d == th - 1:
            return MOSS[0]
        return MOSS[1]

    def drape_color(self, side, k, d, n):
        if side == "l":
            if d == n - 1:
                return CY[0] if k == 0 else MOSS[0]
            return MOSS[2] if k == 0 else MOSS[1]
        if d == n - 1:
            return MOSS[0]
        return MOSS[1] if k == 0 else MOSS[0]

    def variants(self):
        C = self.colorize
        b = self.base
        L = {"o": O, "s": self.ramp[1], "d": self.ramp[0], "t": self.ramp[2], "u": self.ramp[3],
             "U": self.ramp[4], "S": self.ramp[5], "0": MOSS[0], "1": MOSS[1], "2": MOSS[2],
             "c": CY[0], "C": CY[1], "w": CY[2], "v": VI[0], "V": VI[1], "p": VI[2],
             "q": "#241a26", "r": "#3a2834"}
        merged = C(self.make_base_idx(joints_a=()))
        split = C(self.make_base_idx(joints_a=(3, 10)))
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["d.", ".u"], L, 4, 8)
        vs.append(v)                                                   # 2 extra pit
        vs.append(merged.copy())                                       # 3 long block
        vs.append(split.copy())                                        # 4 split block
        v = b.copy()
        stamp(v, ["ss.", "s.."], L, 12, 14)
        vs.append(v)                                                   # 5 chipped corner
        v = b.copy()
        stamp(v, ["s...", ".s..", ".ss.", "...s"], L, 8, 7)
        vs.append(v)                                                   # 6 hairline crack
        v = b.copy()
        stamp(v, ["..0..1", "011010", ".0...."], L, 0, 12)
        vs.append(v)                                                   # 7 moss in mortar
        v = merged.copy()
        stamp(v, [".s.", "sos", ".U."], L, 9, 8)
        vs.append(v)                                                   # 8 hole
        v = b.copy()
        stamp(v, ["qqqr..", "....qq", ".....q"], L, 5, 13)
        vs.append(v)                                                   # 9 root tendril
        # --- detailed variants
        v = split.copy()
        stamp(v, ["sUs.", "U.U.", "sUsU", "..Us"], L, 5, 7)
        vs.append(v)                                                   # 10 carved glyph
        v = b.copy()
        stamp(v, ["o.......", ".o......", ".oo.....", "...o....", "...oo...", ".....o..",
                  "......oo"], L, 6, 6)
        stamp(v, ["U", ".", ".", ".U"], L, 7, 6)
        vs.append(v)                                                   # 11 crack
        v = b.copy()
        stamp(v, ["c.........", ".cC.......", "...Cc...c.", ".....CwC..", "........c."], L, 3, 8)
        vs.append(v)                                                   # 12 glowing vein
        v = merged.copy()
        stamp(v, [".sss.", "sUSUs", "sUsUs", "sU.Us", ".sUs."], L, 6, 7)
        vs.append(v)                                                   # 13 fossil
        v = split.copy()
        stamp(v, [".ccc.", "c.C.c", "cCwCc", "c.C.c", ".ccc."], L, 5, 7)
        vs.append(v)                                                   # 14 glowing rune
        v = b.copy()
        stamp(v, ["..p.", ".pVv", "oVvo", ".oo."], L, 12, 8)
        vs.append(v)                                                   # 15 violet crystal
        return vs

    def top_decor(self):
        return tile_decor.ruin_stone()


# ===========================================================================
# ancient_metal: tech ruin plating, amber light strips, rivets
# ===========================================================================
class AncientMetal(Material):
    name = "ancient_metal"
    map_color = "#577286"
    ramp = ["#0e131d", "#1a2230", "#2c3846", "#45556a", "#66798f", "#9fb3c8"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-1, False), (-1, False)]
    under_lip = [(-1, False)]
    top_th = [5] * 16
    LIGHT = {2: AM[0], 3: AM[1], 4: AM[2], 5: AM[1], 6: AM[0],
             10: AM[0], 11: AM[1], 12: AM[2], 13: AM[1], 14: AM[0]}

    def make_base_idx(self, rivets=True, ribs=True):
        idx = np.full((TS, TS), 2, int)
        # plate A rows 0-7, seams at row 7 and col 15
        idx[0, :] = 3
        idx[0:7, 0] = 3
        idx[0, 0] = 4
        idx[7, :] = 1
        idx[0:7, 15] = 1
        if rivets:
            for rx in (2, 12):
                idx[2, rx], idx[2, rx + 1], idx[3, rx], idx[3, rx + 1] = 5, 3, 3, 1
        # plate B rows 8-15, vertical seam x=7, ribbed
        idx[8, :] = 3
        idx[15, :] = 1
        if ribs:
            idx[11, :] = 1
            idx[12, :] = 3
        idx[8:16, 7] = 1
        for y in range(8, 15):
            if idx[y, 8] == 2:
                idx[y, 8] = 3
        idx[8, 8] = 4
        return idx

    def top_color(self, x, d, th):
        if d == 0:
            return self.ramp[5]
        if d == 1:
            return self.ramp[4]
        if d == 2:
            return self.ramp[1]
        if d == 3:
            return self.LIGHT.get(x, self.ramp[2])
        return self.ramp[0]

    def post_cell(self, img, g):
        # tiny amber running lights under overhangs and a lit seam on the left wall
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                if g.db[y, x] == 2 and x in (4, 11):
                    img[y, x] = np.array([int(AM[1][i:i + 2], 16) for i in (1, 3, 5)] + [255])
                if g.db[y, x] == 2 and x in (3, 5, 10, 12):
                    img[y, x] = np.array([int(AM[0][i:i + 2], 16) for i in (1, 3, 5)] + [255])

    def variants(self):
        C = self.colorize
        b = self.base
        L = {"o": O, "m": self.ramp[1], "d": self.ramp[0], "n": self.ramp[2], "M": self.ramp[3],
             "N": self.ramp[4], "h": self.ramp[5], "a": AM[0], "A": AM[1], "y": AM[2],
             "0": MOSS[0], "1": MOSS[1], "2": MOSS[2], "3": MOSS[3], "c": CY[0], "C": CY[1],
             "w": CY[2]}
        plain = C(self.make_base_idx(rivets=False))
        smooth = C(self.make_base_idx(ribs=False))
        vs = [b.copy(), b.copy(), plain.copy()]
        v = b.copy()
        stamp(v, ["dddddddd", "NNNNNNNN", "dddddddd", "NNNNNNNN"], L, 4, 2)
        vs.append(v)                                                   # 3 vent grille
        v = b.copy()
        stamp(v, ["N...", ".N..", "..n."], L, 5, 4)
        vs.append(v)                                                   # 4 scratch
        v = smooth.copy()
        stamp(v, ["..1.", ".101", "1.1.", ".0.."], L, 9, 9)
        vs.append(v)                                                   # 5 patina
        v = b.copy()
        stamp(v, [".n.", "nmN", ".N."], L, 8, 3)
        vs.append(v)                                                   # 6 dent
        v = b.copy()
        stamp(v, ["mmo", "o.d"], L, 12, 2)
        vs.append(v)                                                   # 7 missing rivet
        v = b.copy()
        stamp(v, ["..1....1..2.", "0.10..1011.."], L, 1, 6)
        vs.append(v)                                                   # 8 moss in seam
        v = b.copy()
        stamp(v, ["N..", ".n.", "..N"], L, 10, 9)
        vs.append(v)                                                   # 9 scratch 2
        # --- detailed
        v = b.copy()
        stamp(v, ["oooo", "oyAo", "oAao", "oooo"], L, 6, 2)
        vs.append(v)                                                   # 10 amber light
        v = smooth.copy()
        stamp(v, ["o.....", ".oc...", "..oCo.", "...cwo", "....oC", ".....o"], L, 2, 1)
        vs.append(v)                                                   # 11 crack w/ cyan
        v = smooth.copy()
        stamp(v, ["a.a.a", ".aAa.", "aAyAa", ".aAa.", "a.a.a"], L, 9, 9)
        vs.append(v)                                                   # 12 amber glyph
        v = b.copy()
        stamp(v, ["oooooo", "ocCcco", "occCco", "oooooo"], L, 5, 2)
        vs.append(v)                                                   # 13 cyan display
        v = smooth.copy()
        stamp(v, ["..3.........", ".323..3.....", "..21.323.3..", "...1..2..2..",
                  "......1..1.."], L, 2, 5)
        vs.append(v)                                                   # 14 vine growth
        v = b.copy()
        stamp(v, ["d.d.d", "N.N.N"], L, 9, 4)
        vs.append(v)                                                   # 15 bolt holes
        return vs

    def top_decor(self):
        return tile_decor.ancient_metal()


# ===========================================================================
# crystal: violet crystal rock, glowing
# ===========================================================================
class Crystal(Material):
    name = "crystal"
    map_color = "#b04cff"
    ramp = ["#0d0918", "#181029", "#22173b", "#2f1f52", "#43296f", "#5c3a8f"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False)]
    top_th = [3, 4, 5, 4, 3, 3, 4, 6, 5, 4, 3, 4, 5, 4, 3, 3]
    drape_l = (8, 6)
    drape_r = (6, 4)

    def make_base_idx(self):
        """Large slanted facets: one crack family of slope 1 (period 16) and two of
        slope -1/2 (8 apart) -> periodic on the 16x16 torus."""
        idx = np.zeros((TS, TS), int)
        for y in range(TS):
            for x in range(TS):
                u = (y - x) % TS
                w2 = (2 * y + x) % 16          # 2*(y + x/2) -> family B every 8 rows
                fb = (2 * y + x) % 32 < 16      # which band of family B
                if u == 0 or w2 in (0, 1):
                    v = 1
                elif u == 1 and x % 3 != 0:
                    v = 4                       # lit lower lip of the slope-1 crack
                elif w2 in (2, 3) and x % 2 == 0:
                    v = 4 if fb else 3
                else:
                    v = 3 if fb else 2
                    if u >= 12:
                        v -= 1                  # facet darkens towards its lower-right
                idx[y, x] = max(1, v)
        return idx

    def add_base_details(self, img):
        for (x, y, c) in [(5, 9, VI[0]), (12, 4, VI[0]), (9, 14, VI[1])]:
            img[y, x] = np.array([int(c[i:i + 2], 16) for i in (1, 3, 5)] + [255])

    def top_color(self, x, d, th):
        if d == 0:
            return VI[3] if x in (3, 8, 13) else VI[2]
        if d == 1:
            return VI[1]
        if d == th - 1:
            return self.ramp[3]
        return VI[0]

    def drape_color(self, side, k, d, n):
        if side == "l":
            if d == n - 1:
                return self.ramp[3]
            return VI[1] if k == 0 else VI[0]
        if d == n - 1:
            return self.ramp[2]
        return VI[0] if k == 0 else self.ramp[3]

    def post_cell(self, img, g):
        # faint violet glints along the shaded underside
        for y in range(TS):
            for x in range(TS):
                if g.solid[y, x] and g.db[y, x] == 1 and x in (6, 13):
                    img[y, x] = np.array([0x6a, 0x2b, 0xb8, 255])

    def variants(self):
        b = self.base
        L = {"o": O, "d": self.ramp[0], "k": self.ramp[1], "D": self.ramp[3], "e": self.ramp[4],
             "v": VI[0], "V": VI[1], "p": VI[2], "P": VI[3], "c": CY[0], "C": CY[1], "w": CY[2]}
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["v..", "..V", ".v."], L, 9, 5)
        vs.append(v)                                                   # 2 glints
        v = b.copy()
        stamp(v, ["k...", ".k..", ".kk.", "...k"], L, 5, 9)
        vs.append(v)                                                   # 3 crack
        v = b.copy()
        stamp(v, ["..p", ".pV", "pVv", "ovo"], L, 10, 9)
        vs.append(v)                                                   # 4 small crystal
        v = b.copy()
        stamp(v, ["e..", ".e.", "..D"], L, 3, 10)
        vs.append(v)                                                   # 5 facet highlight
        v = b.copy()
        stamp(v, ["kk", "kd"], L, 12, 4)
        vs.append(v)                                                   # 6 pocket
        v = b.copy()
        stamp(v, ["v", ".", "..v"], L, 2, 6)
        vs.append(v)                                                   # 7 glints 2
        v = b.copy()
        stamp(v, ["D..e", ".De.", ".eD."], L, 9, 11)
        vs.append(v)                                                   # 8 facet lines
        v = b.copy()
        stamp(v, ["..k", "kk.", "k.."], L, 6, 3)
        vs.append(v)                                                   # 9 crack 2
        # --- detailed
        v = b.copy()
        stamp(v, ["v.........", ".vV.......", "...Vp..v..", ".....pPV..", "........v."], L, 3, 6)
        vs.append(v)                                                   # 10 glowing vein
        v = b.copy()
        stamp(v, ["...p..", "..pPo.", ".ppVvo", "pPVvvo", "oVvvoo", ".ooo.."], L, 5, 6)
        vs.append(v)                                                   # 11 crystal cluster
        v = b.copy()
        stamp(v, [".ooo.", "oVpVo", "opPpo", "oVpVo", ".ooo."], L, 5, 5)
        vs.append(v)                                                   # 12 geode
        v = b.copy()
        stamp(v, ["..w", ".wC", "wCc", "occ", ".oo"], L, 9, 5)
        vs.append(v)                                                   # 13 cyan shard
        v = b.copy()
        stamp(v, ["k.....", ".kV...", "..kpV.", "....kv"], L, 4, 8)
        vs.append(v)                                                   # 14 glowing crack
        v = b.copy()
        stamp(v, [".p...p", "pVo.pV", "oo..oo"], L, 4, 9)
        vs.append(v)                                                   # 15 twin crystals
        return vs

    def top_decor(self):
        return tile_decor.crystal()


# ===========================================================================
# root: giant organic roots / bark with cyan veins
# ===========================================================================
class Root(Material):
    name = "root"
    map_color = "#6d4d62"
    ramp = ["#120c18", "#221827", "#362538", "#4d3549", "#6d4d62", "#906f82"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False)]
    under_lip = [(-1, False)]
    top_th = [3, 3, 4, 3, 2, 2, 3, 4, 5, 3, 3, 2, 2, 3, 4, 3]
    drape_l = (7, 5)
    drape_r = (5, 3)
    MOSSX = {0, 1, 2, 3, 6, 7, 8, 9, 10, 13, 14, 15}
    GROOVES = [(2, 1.0, 0.0), (7, 1.3, 5.0), (12, 1.0, 11.0)]

    def groove_y(self, k, x):
        c, a, ph = self.GROOVES[k]
        return int(round(c + a * math.sin(2 * math.pi * (x + ph) / TS)))

    def make_base_idx(self):
        idx = np.full((TS, TS), 2, int)
        for x in range(TS):
            for k in range(len(self.GROOVES)):
                gy = self.groove_y(k, x) % TS
                idx[gy, x] = 1
                idx[(gy + 1) % TS, x] = 3
        # fibre dashes between grooves (fixed, periodic)
        for (x, y, n, v) in [(1, 5, 4, 3), (4, 10, 3, 1), (12, 9, 4, 3), (8, 15, 3, 1)]:
            for i in range(n):
                xx = (x + i) % TS
                yy = y + int(round(0.5 * math.sin(2 * math.pi * (xx + 5) / TS)))
                yy %= TS
                if idx[yy, xx] == 2:
                    idx[yy, xx] = v
        return idx

    def top_color(self, x, d, th):
        moss = x in self.MOSSX
        if d == 0:
            if x == 7:
                return CY[1]
            return MOSS[3] if moss else self.ramp[5]
        if d == 1:
            if x == 8:
                return CY[0]
            return MOSS[2] if moss else self.ramp[4]
        if d == th - 1 and th >= 4:
            return CY[0] if x == 8 else MOSS[0]
        return MOSS[1] if moss else self.ramp[3]

    def drape_color(self, side, k, d, n):
        if side == "l":
            if d == n - 1:
                return MOSS[0]
            return MOSS[2] if k == 0 else MOSS[1]
        if d == n - 1:
            return MOSS[0]
        return MOSS[1] if k == 0 else MOSS[0]

    def post_cell(self, img, g):
        # cyan vein tips glowing on the underside
        for y in range(TS):
            for x in range(TS):
                if g.solid[y, x] and g.db[y, x] == 1 and x in (5, 12):
                    img[y, x] = np.array([0x1f, 0xb8, 0xb0, 255])

    def variants(self):
        b = self.base
        L = {"o": O, "d": self.ramp[0], "q": self.ramp[1], "r": self.ramp[2], "R": self.ramp[3],
             "e": self.ramp[4], "E": self.ramp[5], "0": MOSS[0], "1": MOSS[1], "2": MOSS[2],
             "3": MOSS[3], "c": CY[0], "C": CY[1], "w": CY[2], "W": CY[3]}
        vs = [b.copy(), b.copy()]
        v = b.copy()
        stamp(v, [".qq.", "qRrq", "qrRe", ".ee."], L, 6, 4)
        vs.append(v)                                                   # 2 knot
        v = b.copy()
        stamp(v, ["q....", ".qq..", "...qd"], L, 3, 9)
        vs.append(v)                                                   # 3 crack
        v = b.copy()
        stamp(v, ["c....", ".cc..", "...c."], L, 8, 9)
        vs.append(v)                                                   # 4 short vein
        v = b.copy()
        stamp(v, ["..1.", ".101", "1..."], L, 11, 5)
        vs.append(v)                                                   # 5 moss patch
        v = b.copy()
        stamp(v, ["E..", ".e."], L, 4, 5)
        vs.append(v)                                                   # 6 scar
        v = b.copy()
        stamp(v, [".qqq.", "qRrrq", ".qqq."], L, 9, 9)
        vs.append(v)                                                   # 7 burl
        v = b.copy()
        stamp(v, ["e..", "..e"], L, 2, 9)
        vs.append(v)                                                   # 8 fibres
        v = b.copy()
        stamp(v, ["qq..", "..qq"], L, 10, 4)
        vs.append(v)                                                   # 9 split
        # --- detailed
        v = b.copy()
        stamp(v, ["c...........", ".cC.....c...", "...CwC.cC...", "......Cc..cC", "...........c"],
              L, 2, 5)
        vs.append(v)                                                   # 10 long vein
        v = b.copy()
        stamp(v, [".ooo.", "ocCco", "oCwCo", ".ooo."], L, 6, 6)
        vs.append(v)                                                   # 11 glowing hollow
        v = b.copy()
        stamp(v, [".oooo.", "oCwwCo", ".occo."], L, 5, 8)
        vs.append(v)                                                   # 12 shelf fungus
        v = b.copy()
        stamp(v, ["c..C", ".w..", "C..c"], L, 6, 6)
        vs.append(v)                                                   # 13 spores
        v = b.copy()
        stamp(v, [".qq..", "qRRq.", "qReq.", ".qq.c", "...cC"], L, 4, 4)
        vs.append(v)                                                   # 14 knot + vein
        v = b.copy()
        stamp(v, ["c.....c", ".C...C.", "..cwc..", "...c..."], L, 4, 7)
        vs.append(v)                                                   # 15 vein fork
        return vs

    def top_decor(self):
        return tile_decor.root()


MATERIALS = [RuinStone, AncientMetal, Crystal, Root]
