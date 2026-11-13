"""Top-decoration overlays for the terrain tilesets (atlas row 3).

Each map is drawn so that its LAST row lands on decor row 12, which is the top surface row
of the tile below (the engine draws decor at offset -12: rows 0-11 stick out above the tile,
rows 12-15 overlap the tile's top 4 px). Maps of exactly 16 rows are placed verbatim.
"""
from helpers import P, new
from sprites import stamp

LEG = {
    "o": P.OUTLINE,
    # moss
    "0": "#0f3b3a", "1": "#135c50", "2": "#1f8a6a", "3": "#35b583",
    # cyan glow
    "c": "#1fb8b0", "C": "#39f0d8", "w": "#8ffff0", "W": "#e6fffb",
    # violet glow + dark violet rock
    "v": "#6a2bb8", "V": "#b04cff", "p": "#e08aff", "P": "#f4d4ff",
    "d": "#2d1b52", "D": "#43286f",
    # amber
    "a": "#c8741f", "A": "#ffb347", "y": "#ffd98a",
    # stone
    "s": "#1d2b3a", "t": "#2a3e4f", "u": "#3b5466", "U": "#577286", "S": "#7d97a8",
    # metal
    "m": "#1a2230", "n": "#2c3846", "M": "#45556a", "N": "#66798f", "h": "#9fb3c8",
    # bark
    "q": "#221827", "r": "#362538", "R": "#4d3549", "e": "#6d4d62", "E": "#906f82",
}


def decor(rows):
    img = new(16, 16)
    oy = 0 if len(rows) == 16 else 13 - len(rows)
    stamp(img, rows, LEG, 0, oy)
    return img


# ----------------------------------------------------------------- plants (shared)
GRASS_SHORT = [
    "....3.......3...",
    ".3..3..3...32...",
    ".32.23.3..32..3.",
    "..2.2.32.22..32.",
    "..22212222122.2.",
    ".1122111121111..",
]

GRASS_TALL = [
    "........w.......",
    "........C.......",
    "...3....3.......",
    "...3...32...3...",
    "...23..2....3...",
    "....2..2...32...",
    "..3.2..2.3.2....",
    "..32.2.2.2.2..3.",
    "...2.2.2.22..32.",
    "...22.222.2.22..",
    "...1222212122...",
    "..11121111111...",
]

MOSS_TUFT = [
    ".....w..........",
    "....wCc...W.....",
    "...cCC20.wCc....",
    "..12cc21.cC1....",
    "..1222111c210...",
    ".11112111111....",
]

MUSHROOMS = [
    ".....oooo.......",
    "....owWwCo......",
    "...owCCCCco.....",
    "...occcccco.oo..",
    "....oo3uoo.oCwo.",
    ".....oUuo.occco.",
    ".....oUuo..oUo..",
    "....1oUuo2.oU12.",
    "...11211121111..",
]

MUSHROOM_VIOLET = [
    ".......ooo......",
    ".....ooVpVoo....",
    "....oVpVVPVVo...",
    "...oVVPVVVpVVo..",
    "...ovvvvvvvvvo..",
    "....ooooUoooo...",
    "........Uu......",
    ".......oUuo.....",
    "......2oUuo.....",
    "....1121Uu112...",
]

FERN = [
    ".......33.......",
    "......3..2......",
    "......2.c2..3...",
    ".3.....22..3....",
    "..3....2..32....",
    "..23...2.32.....",
    "...22..2.2......",
    "....22.222......",
    "....1122211.....",
    "...111111111....",
]

FLOWERS = [
    "..........p.....",
    "...p.....pVp....",
    "..pVp.....p.....",
    "...p...P..2.....",
    "...2..pVp.2.....",
    "...2...2..2.3...",
    "...2.3.2.22.2...",
    "..1222.2121.2...",
    "..11111111111...",
]

PEBBLE_SPROUT = [
    "..........3.....",
    "........3.2.....",
    "...oooo..22.....",
    "..oUSUuo..2.....",
    ".oUUuuuto.2.....",
    ".ouuuttto121....",
    "..1tttt1111.....",
]

# ----------------------------------------------------------------- ancient metal
M_ANTENNA = [
    ".....y..........",
    "....yAy.........",
    ".....a..........",
    ".....N..........",
    ".....M..........",
    ".....N..........",
    ".....M..........",
    ".....N.....oo...",
    "....oMo...oNmo..",
    "...oNMMo..oMmo..",
    "...ohNMmo.oNmo..",
]

M_CABLE = [
    ".....oooo.......",
    "....oNMMno......",
    "...oNo..onmo....",
    "...oMo...omo....",
    "...oMo....omo...",
    "..oNMno...omo...",
    "..ommmo...ommo..",
]

M_LAMP = [
    "......oo........",
    ".....oyAo.......",
    ".....oAAo.......",
    ".....oaao.......",
    ".....oNmo.......",
    ".....oNmo.......",
    ".....oNmo.......",
    "....ohNMmo......",
    "....oNMMmo......",
]

M_DEBRIS = [
    "........oo......",
    ".......oNMo.....",
    "..oo..oNMMmo.3..",
    ".oNmo.oMMmmo12..",
]

M_VENT = [
    "..........3.....",
    "...oooo..32.....",
    "..oNNMmo.2......",
    "..oMooomo2.1....",
    "..oNo.oMo21.....",
    "..oMo.oNmo......",
    ".ohNMooNMmo.....",
]

M_BEACON = [
    "......ooo.......",
    ".....oyAAo......",
    ".....oAAao......",
    "....oNMMMmo.....",
    "....ohNMMmmo....",
]

# ----------------------------------------------------------------- crystal
C_SHARDS = [
    "........P.......",
    ".......pVo......",
    ".......pVo......",
    ".......pVvo.....",
    "....p..pVvo.....",
    "...pPo.pVvo.....",
    "...pVo.pVvo.p...",
    "...pVvopVvopVo..",
    "...pVvopVvopVvo.",
    "..DpVvvpVvvpVvo.",
]

C_SPIKE = [
    "..........P.....",
    ".........pV.....",
    ".........pVo....",
    "........pVvo....",
    "........pVvo....",
    ".......pVVvo....",
    ".......pVvo.....",
    "......pVVvo.....",
    "......pVvvo.....",
    ".....pVVvo......",
    ".....pVvvo......",
    "....DpVvvoD.....",
]

C_PAIR = [
    "......p.........",
    ".....pPo........",
    ".....pVo..p.....",
    ".....pVvopPo....",
    "....DpVvopVvo...",
    "...DDpVvvpVvoD..",
]

C_GRASS = [
    "........p.......",
    "...p....V.......",
    "...V....V...p...",
    "...V...vV...V...",
    "..vV...V...vV...",
    "..V.v..V.v.V....",
    "..V..v.V.V.V....",
    ".vV..vvV.VvV.v..",
    ".DvDDDvVDvVvDv..",
]

C_BLOOM = [
    ".......P........",
    "......pPp.......",
    ".....pPWPp......",
    "......pPp.......",
    ".......v........",
    ".......v..p.....",
    "......Dv.pVo....",
    ".....DDvDpVvoD..",
]

C_RUBBLE = [
    "..........p.....",
    "...oo....pVo....",
    "..oDdo..oDVvo...",
    ".oDddo.oDddddo..",
]

C_CYAN = [
    ".........W......",
    "........wCo.....",
    "........wCo.....",
    ".......wCco.....",
    "...w...wCco.....",
    "..wCo..wCco.....",
    "..wCco.wCco.....",
    "..wCcowCcco.....",
    ".DwCccwCccoD....",
]

C_SMALL = [
    "...p........p...",
    "..pVo......pVo..",
    "..pVo..p...pVvo.",
    ".DpVvoDpVoDpVvoD",
]

# ----------------------------------------------------------------- root
R_LOOP = [
    ".....ooooo......",
    "....oEeeeRo.....",
    "...oEeRRRrRo....",
    "...oeRo..orRo...",
    "..oeRo....orRo..",
    "..oeRo.....oRqo.",
    ".oeRro.....oRqo.",
]

R_SPROUT = [
    "........33......",
    "......3322......",
    ".....32221.33...",
    "......11.2322...",
    ".........2111...",
    "........2.......",
    "........2.......",
    ".......121......",
]

R_BULB = [
    ".........ooo....",
    "........oWwCo...",
    "........owCCco..",
    "........oCCcco..",
    ".........occo...",
    "..........2.....",
    ".........2......",
    "........2.......",
    ".......12.......",
    "......1121......",
]

R_CURL = [
    "..........c.....",
    ".........cCc....",
    "..........R.....",
    "..........R.....",
    ".........oRo....",
    "...ooo...oRo....",
    "..oeRqo..oRo....",
    ".oeo.oRo.oeRo...",
    ".oRo..oqooeRqo..",
]


def ruin_stone():
    return [decor(m) for m in (GRASS_SHORT, MOSS_TUFT, MUSHROOMS, GRASS_TALL, FERN, FLOWERS,
                               MUSHROOM_VIOLET, PEBBLE_SPROUT)]


def ancient_metal():
    return [decor(m) for m in (M_ANTENNA, M_CABLE, M_LAMP, M_DEBRIS, M_VENT, M_BEACON,
                               MOSS_TUFT, GRASS_SHORT)]


def crystal():
    return [decor(m) for m in (C_SHARDS, C_SPIKE, C_PAIR, C_GRASS, C_BLOOM, C_RUBBLE, C_CYAN,
                               C_SMALL)]


def root():
    return [decor(m) for m in (GRASS_SHORT, MOSS_TUFT, MUSHROOMS, R_LOOP, R_SPROUT, R_BULB,
                               R_CURL, GRASS_TALL)]
