"""Aethra terrain materials (2.19, the planet of the sanctuaries): sanctum_stone, nebula_rock.

Same autotile engine and atlas format as the other planets (tiles.Material, ASSET_SPEC section 7).

  sanctum_stone  main ground: pale violet marble slabs with gold inlay, a lip of violet grass with
                 golden flowers; variants carry the four elements (root, frost, magma, void)
  nebula_rock    secondary: dark star-flecked rock of the floating islands, crust of glowing
                 violet moss, crystal inclusions of the four elements
"""
import numpy as np

from helpers import P, col, new
from sprites import stamp
from tiles import Material, TS

O = P.OUTLINE
GO = ["#6a4220", "#a8702a", "#e0a848", "#ffd27a", "#fff0c0"]
VG = ["#24123e", "#3e2068", "#62369a", "#8e58c8", "#c08cf0"]       # violet grass / moss
PK = ["#a8407a", "#ff7696", "#ffc0d8"]
ELEM = {"r": "#7dffd6", "R": "#1fa8a0", "f": "#c8f4ff", "F": "#5fa8e0", "m": "#ffb13b", "M": "#c8501e",
        "v": "#ff7696", "V": "#5b2a8c"}


def _legend(ramp):
    L = {"o": O, "d": ramp[0], "s": ramp[1], "t": ramp[2], "u": ramp[3], "U": ramp[4], "S": ramp[5],
         "g": GO[1], "G": GO[2], "y": GO[3], "Y": GO[4], "0": VG[0], "1": VG[1], "2": VG[2], "3": VG[3],
         "4": VG[4], "p": PK[0], "P": PK[1], "w": "#ffffff"}
    L.update(ELEM)
    return L


# ===========================================================================
# sanctum_stone
# ===========================================================================
class SanctumStone(Material):
    name = "sanctum_stone"
    map_color = "#6e6698"
    ramp = ["#161028", "#262040", "#38305a", "#4e4678", "#6e6698", "#958ec0"]
    left_band = [(2, False), (1, False), (1, True)]
    right_band = [(-1, False), (-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False), (-1, True)]
    #          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    top_th = [4, 4, 5, 4, 3, 4, 6, 5, 4, 4, 5, 4, 3, 4, 5, 4]
    drape_l = (8, 6)
    drape_r = (6, 4)
    FLOWERS = {3: GO[3], 10: PK[2], 14: GO[3]}

    # slabs: mortar rows 6 and 14; course A rows 7-13 (joint x=5), course B rows 15-5 (joint x=12)
    def make_base_idx(self, joints_a=(5,), joints_b=(12,)):
        idx = np.full((TS, TS), 3, int)
        for y in range(TS):
            for x in range(TS):
                if y in (6, 14):
                    idx[y, x] = 1
                    continue
                if 7 <= y <= 13:
                    joints, top = joints_a, 7
                else:
                    joints, top = joints_b, 15
                if x in joints:
                    idx[y, x] = 1
                    continue
                by = (y - top) % TS
                left = ((x - 1) % TS) in joints
                if by == 0 and left:
                    idx[y, x] = 5
                elif by == 0 or left:
                    idx[y, x] = 4
                elif (x + 2 * y) % 11 == 0:             # marble veins (diagonal, periodic)
                    idx[y, x] = 4
                elif (2 * x + y) % 13 == 5:
                    idx[y, x] = 2
        return idx

    def add_base_details(self, img):
        # gold inlay studs in the mortar lines
        for (x, y) in ((9, 6), (2, 14)):
            img[y, x] = col(GO[2])

    def top_color(self, x, d, th):
        if d == 0:
            return self.FLOWERS.get(x, VG[4] if x % 4 == 1 else VG[3])
        if d == 1:
            return VG[3] if x % 3 else VG[2]
        if d == 2:
            return VG[2] if x % 2 == 0 else VG[1]
        if d == th - 1:
            return GO[1] if x in (6, 7) else VG[0]
        return VG[1]

    def drape_color(self, side, k, d, n):
        if d == n - 1:
            return VG[0]
        if side == "l":
            return VG[2] if k == 0 else VG[1]
        return VG[1] if k == 0 else VG[0]

    def variants(self):
        C = self.colorize
        b = self.base
        L = _legend(self.ramp)
        merged = C(self.make_base_idx(joints_a=()))
        merged[6, 9] = merged[14, 2] = col(GO[2])
        vs = [b.copy(), b.copy(), merged.copy()]                              # 0-2 plain, long slab
        v = b.copy()
        stamp(v, ["s...", ".s..", "..ss", "....s"], L, 8, 8)
        vs.append(v)                                                          # 3 hairline crack
        v = b.copy()
        stamp(v, ["gGg", "G.G", "gGg"], L, 6, 9)
        vs.append(v)                                                          # 4 gold ring inlay
        v = b.copy()
        stamp(v, ["..1.", ".121", "1..."], L, 10, 1)
        vs.append(v)                                                          # 5 moss in a joint
        v = b.copy()
        stamp(v, ["ss.", "s.."], L, 13, 15)
        vs.append(v)                                                          # 6 chipped corner
        v = merged.copy()
        stamp(v, ["U...U", ".UgU.", ".gYg.", ".UgU.", "U...U"], L, 5, 8)
        vs.append(v)                                                          # 7 sun glyph
        v = b.copy()
        stamp(v, [".o.", "oRo", "orR", ".o."], L, 3, 9)
        vs.append(v)                                                          # 8 root element gem
        v = b.copy()
        stamp(v, [".o.", "ofo", "oFf", ".o."], L, 11, 1)
        vs.append(v)                                                          # 9 frost element gem
        v = b.copy()
        stamp(v, [".o.", "omo", "oMm", ".o."], L, 9, 9)
        vs.append(v)                                                          # 10 magma element gem
        v = b.copy()
        stamp(v, [".o.", "ovo", "oVv", ".o."], L, 2, 1)
        vs.append(v)                                                          # 11 void element gem
        v = merged.copy()
        stamp(v, ["gGGGGGGg", "G......G", "G.yGGy.G", "gGGGGGGg"], L, 4, 8)
        vs.append(v)                                                          # 12 engraved cartouche
        v = b.copy()
        stamp(v, ["0..", "10.", "211", ".21"], L, 0, 9)
        vs.append(v)                                                          # 13 creeping moss
        v = b.copy()
        stamp(v, ["r.......f", ".R.....F.", "..ry.yf..", "...yGy...", "..vy.ym..", ".V.....M.",
                  "v.......m"], L, 3, 7)
        vs.append(v)                                                          # 14 four-elements seal
        v = b.copy()
        stamp(v, ["o.......", ".oo.....", "...o....", "...oo...", ".....o..", "......oo"], L, 5, 7)
        stamp(v, ["U", ".", ".U"], L, 6, 7)
        vs.append(v)                                                          # 15 crack
        return vs

    def top_decor(self):
        return decor_sanctum()


# ===========================================================================
# nebula_rock
# ===========================================================================
class NebulaRock(Material):
    name = "nebula_rock"
    map_color = "#4a3474"
    ramp = ["#0c0818", "#170f2c", "#241840", "#342458", "#4a3474", "#62489a"]
    left_band = [(2, False), (1, False)]
    right_band = [(-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False)]
    top_th = [3, 3, 2, 3, 4, 3, 3, 2, 3, 3, 4, 4, 3, 2, 3, 3]
    drape_l = (6, 4)
    drape_r = (4, 3)
    # chunky faceted rock: three facet bands that tile, star flecks
    FACETS = [((0, 0), (9, 0), (6, 7), (0, 5)), ((9, 0), (16, 0), (16, 8), (6, 7)),
              ((0, 5), (6, 7), (11, 16), (0, 16)), ((6, 7), (16, 8), (16, 16), (11, 16))]

    def make_base_idx(self):
        idx = np.full((TS, TS), 3, int)
        yy, xx = np.mgrid[0:TS, 0:TS]
        tones = [4, 3, 2, 3]
        for k, poly in enumerate(self.FACETS):
            inside = np.ones((TS, TS), bool)
            n = len(poly)
            for i in range(n):
                (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
                cross = (x1 - x0) * (yy + 0.5 - y0) - (y1 - y0) * (xx + 0.5 - x0)
                inside &= cross >= -0.01
            idx[inside] = tones[k]
        # facet edges: dark seams with a lit pixel on their upper side
        for (x0, y0), (x1, y1) in (((9, 0), (6, 7)), ((0, 5), (6, 7)), ((6, 7), (16, 8)), ((6, 7), (11, 16))):
            n = max(abs(x1 - x0), abs(y1 - y0))
            for i in range(n + 1):
                x = int(round(x0 + (x1 - x0) * i / n)) % TS
                y = int(round(y0 + (y1 - y0) * i / n)) % TS
                idx[y, x] = 1
                idx[(y - 1) % TS, x] = max(idx[(y - 1) % TS, x], 4)
        return idx

    def add_base_details(self, img):
        for (x, y, c) in ((3, 2, "#e8dcff"), (12, 12, PK[1]), (13, 4, "#ffffff")):
            img[y, x] = col(c)

    def top_color(self, x, d, th):
        if d == 0:
            return PK[1] if x in (4, 11) else VG[3]
        if d == 1:
            return VG[2]
        if d == th - 1 and th >= 3:
            return VG[0]
        return VG[1]

    def drape_color(self, side, k, d, n):
        if d == n - 1:
            return PK[0] if (side == "l" and k == 0) else VG[0]
        return VG[2] if k == 0 else VG[1]

    def variants(self):
        b = self.base
        L = _legend(self.ramp)
        vs = [b.copy(), b.copy(), b.copy()]
        v = b.copy()
        stamp(v, ["w..", ".P.", "..w"], L, 10, 9)
        vs.append(v)                                                          # 3 star cluster
        v = b.copy()
        stamp(v, ["..o", ".oR", "orr", "oRr", ".oo"], L, 3, 9)
        vs.append(v)                                                          # 4 root crystal
        v = b.copy()
        stamp(v, ["..o.", ".ofo", "offF", ".oFo"], L, 10, 2)
        vs.append(v)                                                          # 5 frost crystal
        v = b.copy()
        stamp(v, [".oo.", "omMo", ".oo."], L, 7, 11)
        vs.append(v)                                                          # 6 magma pocket
        v = b.copy()
        stamp(v, [".o.", "ovo", "oVo", ".o."], L, 2, 3)
        vs.append(v)                                                          # 7 void crystal
        v = b.copy()
        stamp(v, ["d..", ".dd", "...d"], L, 9, 4)
        vs.append(v)                                                          # 8 crack
        v = b.copy()
        stamp(v, ["1.", "21", ".1"], L, 13, 9)
        vs.append(v)                                                          # 9 moss
        v = b.copy()
        stamp(v, ["P......", ".p....P", "..PwP.p", "....p.."], L, 3, 6)
        vs.append(v)                                                          # 10 glowing vein
        v = b.copy()
        stamp(v, ["..o..o", ".oRoofo", "orRrfFo", ".oo.oo."], L, 4, 8)
        vs.append(v)                                                          # 11 twin crystals
        v = b.copy()
        stamp(v, [".oo..oo", "omMooVvo", ".oo..oo"], L, 3, 4)
        vs.append(v)                                                          # 12 magma + void
        v = b.copy()
        stamp(v, ["gy", "yG"], L, 7, 6)
        vs.append(v)                                                          # 13 gold nugget
        v = b.copy()
        stamp(v, ["w.....w", "...w...", "w.....w"], L, 4, 5)
        vs.append(v)                                                          # 14 constellation
        v = b.copy()
        stamp(v, ["..o..", ".oyo.", "oyYyo", ".oyo.", "..o.."], L, 6, 6)
        vs.append(v)                                                          # 15 star seed
        return vs

    def top_decor(self):
        return decor_nebula()


# ===========================================================================
# top decorations (row 3): last map row lands on decor row 12 (see tile_decor.py)
# ===========================================================================
DLEG = _legend(SanctumStone.ramp)


def _decor(rows):
    img = new(16, 16)
    oy = 0 if len(rows) == 16 else 13 - len(rows)
    stamp(img, rows, DLEG, 0, oy)
    return img


GRASS = ["....3.......3...",
         ".3..3..3...32...",
         ".32.23.3..32..3.",
         "..2.2.32.22..32.",
         "..22212222122.2.",
         ".1122111121111.."]
GRASS_GOLD = ["...y.......Y....",
              "..yGy.....yGy...",
              "...g..3....g....",
              "...2..3..3.2..3.",
              ".3.2.32..2.2.32.",
              ".2222122.2122.2.",
              ".1122111121111.."]
ROOT_SHROOMS = ["....oooo........",
                "...orrRRo.......",
                "..orRrrRRo..oo..",
                "..oooooooo.orRo.",
                "....oSUo...oooo.",
                "....oSUo....oUo.",
                "...1oSUo2...oU1.",
                "..112111221111.."]
FROST_SPROUT = ["......o.........",
                ".....ofo........",
                ".....ofFo...o...",
                "..o..ofFo..ofo..",
                ".ofo.offFo.oFo..",
                ".oFFoofFFo.oFo..",
                "..2122112212112.",
                ".11211111121111."]
EMBER_FLOWER = [".......Y........",
                "......YmY.......",
                ".....omMmo......",
                "......oMo.......",
                ".......g....3...",
                "...3...g...32...",
                "...23..g..2.....",
                "..1121122121....",
                ".11111111111...."]
VOID_BLOOM = ["......oo........",
              ".....oPvo.......",
              "....oPvVvo......",
              "....ovVVvo......",
              ".....oVVo.......",
              "......oo........",
              "......2...3.....",
              "...3..2..32.....",
              "..12112112211...",
              ".111111111111..."]
LANTERN_LILY = ["......Y.........",
                ".....YyY........",
                "......g.........",
                "......g...y.....",
                "...2..g..yGy....",
                "..32..g...g.....",
                "..2.2.2...2.3...",
                "..2212222122....",
                ".11211111211...."]
STONE_SHARD = ["......o.........",
               ".....oUo........",
               ".....oUto.......",
               "....oUUto..o....",
               "....oUttoooUo...",
               "...2oUtto3oto2..",
               "..1211111211111."]


def decor_sanctum():
    return [_decor(m) for m in (GRASS, GRASS_GOLD, ROOT_SHROOMS, FROST_SPROUT, EMBER_FLOWER, VOID_BLOOM,
                                LANTERN_LILY, STONE_SHARD)]


def decor_nebula():
    return [_decor(m) for m in (GRASS, VOID_BLOOM, FROST_SPROUT, STONE_SHARD, GRASS_GOLD, EMBER_FLOWER,
                                ROOT_SHROOMS, LANTERN_LILY)]


MATERIALS = [SanctumStone, NebulaRock]
