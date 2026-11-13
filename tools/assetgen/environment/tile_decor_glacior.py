"""Top-decoration overlays for the Glacior tilesets (atlas row 3).

Same convention as tile_decor.py: the LAST row of each map lands on decor row 12, i.e. on
the top surface row of the tile below (engine offset -12). Bottom rows use snow/frost colours
and no outline so the props sit in the snow.
"""
from helpers import new
from sprites import stamp

LEG = {
    "o": "#0a1428",
    # ice
    "i": "#1f4f8f", "I": "#2f6fb3", "j": "#5fb0ff", "J": "#a8e4ff", "w": "#eefaff",
    "W": "#ffffff",
    # snow (lit -> shadow)
    "N": "#ffffff", "n": "#dfe9f5", "S": "#c9d8ea", "s": "#9fb4d2", "f": "#7d93b5",
    # frost + aurora glow
    "c": "#2fb8c8", "C": "#7ff6ff", "X": "#d6fdff", "g": "#5dffb0", "G": "#b8ffe0",
    "v": "#b48cff", "V": "#e0ccff",
    # rock
    "k": "#1b253b", "r": "#29364f", "R": "#3b4a66", "U": "#536380", "u": "#7486a2",
    # steel
    "m": "#232d3e", "M": "#3d4a5e", "t": "#5c6b80", "T": "#8a9ab0", "h": "#b8c6d6",
    # evergreen / wood
    "p": "#0f2a30", "P": "#17403f", "q": "#24584f", "b": "#2a2230", "B": "#4a3a48",
    "e": "#6a5a68",
}


def decor(rows):
    img = new(16, 16)
    oy = 0 if len(rows) == 16 else 13 - len(rows)
    stamp(img, rows, LEG, 0, oy)
    return img


# ----------------------------------------------------------------- ice / frost (shared)
FROST_CRYSTALS = [
    "..........W.....",
    "...W.....wJo....",
    "..wJo....wjo....",
    "..wjo..W.wjIo...",
    ".nwjIonwJwjIon..",
    "nSSnnSSnnSSnnSn.",
]

ICE_SPIKE = [
    "..........W.....",
    ".........wJ.....",
    ".........wJo....",
    "........wJjo....",
    "........wJjo....",
    ".......wJJjo....",
    ".......wJjo.....",
    "......wJJjIo....",
    "......wJjIio....",
    ".....wJJjIo.....",
    "....nwJjIioS....",
    "...nnSSnnSSnS...",
]

ICE_PAIR = [
    "......W.........",
    ".....wJo........",
    ".....wJo..W.....",
    ".....wJjowJo....",
    "....nwJjowjIo...",
    "...nSnnSnnSnS...",
]

ICE_RUBBLE = [
    "..........w.....",
    "...oo....wJo....",
    "..oJjo..oJjIo...",
    ".nSnnSSnSnnSSn..",
]

SNOW_TUFT = [
    "......NN........",
    "....NNnnN...N...",
    "..NNnnnnSnNNnN..",
    ".NnnnSnSSsnnSS..",
    "SnSSsSssfsSsSS..",
]

SNOW_MOUND = [
    "................",
    ".......NNNN.....",
    ".....NNnnnnnS...",
    "...NNnnnnnSSSs..",
    "..NnnnnSnSSSsss.",
    "SnnSSSSsSssssfss",
]

FROZEN_GRASS = [
    "........w.......",
    "...w....J.......",
    "...J...jJ...w...",
    "...j...J...jJ...",
    "..jJ.w.J.J.J....",
    "..J..j.J.j.J..w.",
    ".jJ..J.jJJ.J.jJ.",
    ".SjSnJSnjJSnSj..",
]

FROST_FLOWER = [
    ".......X........",
    "......XCX.......",
    ".....XCWCX......",
    "......XCX.......",
    ".......j..X.....",
    ".......j.XCX....",
    "......Jj..j.....",
    ".....nJjSnjS....",
]

SNOW_PEBBLE = [
    "..........NN....",
    "....NNNN.NnnN...",
    "...NnnnnSoUuRo..",
    "..oNnSSsoURRro..",
    "..ouUUuRoorro...",
    ".SnoRRrroSnS....",
]

# ----------------------------------------------------------------- snow rock
PINE_SAPLING = [
    ".......N........",
    "......NqN.......",
    ".....NqPPo......",
    "......PPo.......",
    ".....NqPPN......",
    "....NqPPPPo.....",
    "......PPpo......",
    "....NqPPPPPN....",
    "...NqPPPPPPPo...",
    "......obo.......",
    ".....SnBnS......",
]

DEAD_TWIGS = [
    ".......w........",
    "..w....e...w....",
    "..e.w..e..we....",
    "...e.e.e.e.e.w..",
    ".w..eBeBe.e.e...",
    "..e..eBBe.ee....",
    "...eB.BB.Be.....",
    "....eBBBBe......",
    "......BBb.......",
    "......bBb.......",
    ".....SnbnS......",
]

# ----------------------------------------------------------------- frost metal
FM_ANTENNA = [
    ".....X..........",
    "....XCX.........",
    ".....c..........",
    ".....T..........",
    ".....wM.........",
    ".....T..........",
    ".....M.....oo...",
    ".....T....oTMo..",
    "....oMo...wwwo..",
    "...oTMMo..oTmo..",
    "...ohwwmo.oMmo..",
]

FM_BEACON = [
    "......ooo.......",
    ".....oXCCo......",
    ".....oCCco......",
    "....owwwwwo.....",
    "....ohTMMmo.....",
]

FM_VENT = [
    "..........w.....",
    "...oooo.........",
    "..owwwwo..W.....",
    "..oMoooMo.......",
    "..oTo.oMo.......",
    "..oMo.oTm.......",
    ".ohTMooTMm......",
]

FM_DEBRIS = [
    "........NN......",
    ".......NnnN.....",
    "..oo..oTMMmo.N..",
    ".oTmo.oMMmmoNn..",
]

FM_PANEL = [
    "................",
    "...oooooooo.....",
    "..owwwwwwwwo....",
    "..oToooooomo....",
    "..oMocCcComo....",
    "..oMoCcccomo....",
    "..oMoooooomo....",
    "..oTMMMMMMmo....",
    "...om....mo.....",
]

FM_PIPE = [
    "...wwwww........",
    "..owJJJjo.......",
    "..oTMMMmo.......",
    "...oMmmo........",
    "...oTMmo........",
    "...occco........",
    "...oTMmo........",
    "..ohTMMmo.......",
]


def glacier_ice():
    return [decor(m) for m in (FROST_CRYSTALS, ICE_SPIKE, SNOW_TUFT, ICE_PAIR, FROST_FLOWER,
                               FROZEN_GRASS, SNOW_PEBBLE, ICE_RUBBLE)]


def snow_rock():
    return [decor(m) for m in (SNOW_MOUND, FROZEN_GRASS, PINE_SAPLING, SNOW_PEBBLE, DEAD_TWIGS,
                               FROST_FLOWER, SNOW_TUFT, ICE_PAIR)]


def frost_metal():
    return [decor(m) for m in (FM_ANTENNA, FM_VENT, FM_BEACON, FM_DEBRIS, FROST_CRYSTALS,
                               FM_PANEL, FM_PIPE, SNOW_TUFT)]
