"""Top-decoration overlays for the Pyralis tilesets (atlas row 3).

Same convention as tile_decor.py: the LAST row of each map lands on decor row 12, i.e. on
the top surface row of the tile below (engine offset -12). Bottom rows use ground colours
(ash) and no outline so the props sit in the crust.
"""
from helpers import new
from sprites import stamp

SMOKE = (74, 60, 60, 150)
SMOKE_L = (110, 92, 88, 120)

LEG = {
    "o": "#0d0607",
    # ash crust
    "0": "#3a2c2c", "1": "#5a4846", "2": "#7e6a64", "3": "#a8938a", "4": "#cbb8ac",
    # basalt
    "k": "#1f1314", "s": "#2f1e1e", "u": "#432a27", "U": "#5c3a33", "S": "#7c5246",
    # lava
    "r": "#b3201a", "R": "#ff4d1e", "a": "#ff8a3d", "y": "#ffc56b", "W": "#fff1c2",
    "e": "#6e1a12",
    # obsidian glass
    "v": "#5b2a6e", "V": "#9b4dca", "p": "#c99af0", "P": "#f2e2ff", "d": "#140b1d",
    "D": "#24143a",
    # forge iron + rust
    "m": "#181417", "n": "#262024", "M": "#383034", "N": "#51464a", "h": "#726468",
    "H": "#9a8a8c", "t": "#6e3b2a", "T": "#a0583a", "Y": "#d08a5a",
    # charred wood / plant
    "c": "#24161a", "C": "#46282a", "x": "#5e3426",
    # smoke (translucent)
    "g": SMOKE, "G": SMOKE_L,
}


def decor(rows):
    img = new(16, 16)
    oy = 0 if len(rows) == 16 else 13 - len(rows)
    stamp(img, rows, LEG, 0, oy)
    return img


# ----------------------------------------------------------------- basalt
ASH_DRIFT = [
    ".........y......",
    "...a............",
    "...........a....",
    ".......oo.......",
    "...ooo.o3oo.....",
    "..o344o4432oo...",
    ".o3432343322o3..",
    "2332222212221232",
]

EMBER_VENT = [
    ".......y........",
    "....a......W....",
    ".........a......",
    "......a.........",
    ".....o.yo..R....",
    "....oSoaoUo.....",
    "...oSurRaruso...",
    "..2uurayaRuu32..",
]

PEBBLES = [
    "...........oo...",
    "....ooo...oSUo..",
    "...oSUuo.oUuuso.",
    "...oUuuso.osso..",
    "..2ouuso3o22123.",
    ".212ooo2122.....",
]

FIREWEED = [
    "......W.........",
    ".....ya.....y...",
    "......R....aR...",
    "...y..c.....c...",
    "..aR..c..c..c...",
    "...c..cc.c.cc.y.",
    "...cc.c..c.c..R.",
    "....c.cc.ccc.c..",
    "....cc.cxc..cc..",
    ".....cCxCcccc...",
    "...12cCxCc21....",
]

LAVA_BUBBLE = [
    "........y.......",
    "...a............",
    "......oooo......",
    ".....oaRRro.....",
    "....oRyWaRRo....",
    "...oURayaaRUo...",
    "..23uURRRRUu32..",
]

OBS_CHIPS = [
    "..........P.....",
    "...p.....pVo....",
    "..pVo...pVdo....",
    "..pVdo..pVdo.e..",
    ".2oDdo32oDdo32..",
]

EMBER_CRYSTALS = [
    ".......W........",
    "......yao.......",
    "......yao..y....",
    "..y...yRao.aR...",
    ".yao..yRao.aRo..",
    ".yRo.oyRao.aro..",
    "21Ro2oyRro2oo12.",
]

STUMP = [
    "......o.........",
    "....o.oCo.......",
    "...oCooxCo......",
    "...oCxcCco......",
    "...oCcrcco......",
    "...oxccCco.a....",
    "..oCxcccCCo.....",
    ".23CCcccc332....",
]

# ----------------------------------------------------------------- obsidian
OB_SHARDS = [
    "........P.......",
    ".......pVo......",
    ".......pVo......",
    ".......pVdo.....",
    "....p..pVdo.....",
    "...pPo.pVdo.....",
    "...pVo.pVdo.p...",
    "...pVdopVDdpVo..",
    "...pVdopVddpVdo.",
    "..DpVddpVddpVdo.",
]

OB_SPIKE = [
    "..........P.....",
    ".........pV.....",
    ".........pVo....",
    "........pVdo....",
    "........pVdo....",
    ".......pVVdo....",
    ".......pVdo.....",
    "......pVVdo.....",
    "......pVddo.....",
    ".....pVVdo......",
    ".....pVddoe.....",
    "....DpVddoD.....",
]

OB_PAIR = [
    "......p.........",
    ".....pPo........",
    ".....pVo..p.....",
    ".....pVdopPo....",
    "....DpVdopVdo...",
    "...DDpVddpVdoD..",
]

OB_RUBBLE = [
    "..........p.....",
    "...oo....pVo....",
    "..oDdo..oDVdo...",
    ".oDddo.oDddddo..",
]

OB_EMBERLIT = [
    ".........P......",
    "........pVa.....",
    "........pVa.....",
    ".......pVdRa....",
    "...p...pVdRa....",
    "..pPo..pVdra....",
    "..pVa.pVVdrR....",
    "..pVr.pVddrR....",
    ".DpVdopVdddroD..",
]

OB_SMALL = [
    "...p........p...",
    "..pVo......pVo..",
    "..pVo..p...pVdo.",
    ".DpVdoDpVoDpVdoD",
]

# ----------------------------------------------------------------- forge metal
F_BOLTS = [
    "..........oo....",
    "...oo....oHNo...",
    "..oHNo..oNooMo..",
    "..oNMo..oMooMo..",
    "...oo....oMMo...",
    ".nMmmn...nmmn...",
]

F_CHIMNEY = [
    ".....gg.........",
    "....gGGg........",
    ".....gGg........",
    "......g.........",
    ".....oooo.......",
    ".....oaRo.......",
    "....ohNMmo......",
    ".....oNmo.......",
    ".....oNTo.......",
    ".....oNmo.......",
    "....ohNMmo......",
    "....hNMMmm......",
]

F_PIPE = [
    "....oooooo......",
    "...oaRyRRao.....",
    "...ohNNMMmo.....",
    "....oNMMmo......",
    "....oTYTto......",
    "....ohNMmo......",
    "....oNMMmo......",
    "...ohNMMMmo.....",
    "...hNMMMMmm.....",
]

F_CRUCIBLE = [
    "......y.........",
    "....oooooooo....",
    "...oNyWyaRaNo...",
    "...ohNNNNNMmo...",
    "....oNTYTTmo....",
    "....ohNMMMmo....",
    ".....oNMMmo.....",
    "...nMmooooMmn...",
]

F_LAMP = [
    "......ooo.......",
    ".....oyaRo......",
    ".....oaRro......",
    ".....ooooo......",
    "......oNo.......",
    "......oNo.......",
    "......oTo.......",
    "......oNo.......",
    ".....ohNMo......",
    "....hNMMMm......",
]

F_GEAR = [
    ".....o.oo.o.....",
    "....oNoNHoNo....",
    "...oNHNNNNMNo...",
    "..ooNNMooMNMoo..",
    "..oHNMo..oMNNo..",
    "..ooNMoaRoMMoo..",
    "...nMMNMNMMMn...",
]

F_INGOT = [
    "..........y.....",
    ".....oooooo.....",
    "....oyyaaRro....",
    "...oyWyaaRro.a..",
    "...oaaRRRrro....",
    "..nMoooooooMn...",
]

F_SLAG = [
    "...........a....",
    "....oo..........",
    "...otTo...ooo...",
    "..otRTto.otTto..",
    "..oTtrto.oTrto..",
    ".nMooooonnoooMn.",
]


def basalt():
    return [decor(m) for m in (ASH_DRIFT, EMBER_VENT, PEBBLES, FIREWEED, LAVA_BUBBLE, OBS_CHIPS,
                               EMBER_CRYSTALS, STUMP)]


def obsidian():
    return [decor(m) for m in (OB_SHARDS, OB_SPIKE, OB_PAIR, OB_RUBBLE, OB_EMBERLIT, OB_SMALL,
                               EMBER_CRYSTALS, ASH_DRIFT)]


def forge_metal():
    return [decor(m) for m in (F_BOLTS, F_CHIMNEY, F_PIPE, F_CRUCIBLE, F_LAMP, F_GEAR, F_INGOT,
                               F_SLAG)]
