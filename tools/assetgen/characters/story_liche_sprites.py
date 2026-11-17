"""The sprites of the Liche's story (« Le Gardien des noms », 2.63): every actor of its six animated
shots, drawn frame by frame like the game's sprites (sheets under assets/stories/liche/, clips in
assets/animations/stories/liche_<name>.json; data/stories/liche.json places them).

  ossian     old Ossian with his candle (story_folk.py)                         40x56
  vieux      Ossian grown very old: walks, kneels, sits, slumps                 40x56
  enfant     a child listening to him                                           40x56
  fuyard1-3  the living fleeing the night of the collapse                       40x56
  bougie     a candle: unlit (a thread of smoke), lighting, lit, green, burning low, going out   16x32
  plaque     a stone with a name: dim, its letters glowing gold, glowing green  32x24
  autel      the altar of the crypt                                             96x48
  ame        Ossian's soul, a green flame                                       32x32
  toile      a cobweb growing, then swaying                                     32x32
  chaise     the chair where he waits                                           32x48
  squelette  his bones on the chair, the green light in the eyes                32x48
  porte      the gate of the catacombs, torches on each side                    128x112
  etoile     the star of the centre: shining, collapsing into a black hole      96x96
  astre      a twinkling star                                                   16x16

The Liche is the game's own sheet (animations/liche.json), the crystal its phylactery's
(animations/phylactere.json).  One sheet pixel is two units of the story's stage.  Deterministic."""
import math

import numpy as np

import story_folk as F
from boss_a_common import mat
from raster import Canvas
from story_monolithe_sprites import C, putpx, twinkle_clips  # noqa: F401
from story_pyrrhax_sprites import star_clips  # noqa: F401  (the same star of the centre)

PI = math.pi
TAU = 2 * PI

FOLK = {
    "ossian": (dict(robe=True, bald=True, beard="#e8e4dc", cloth="#5a4a3a", trim="#a08a5a", item="candle", skin="#d8b090"),
               ["idle", "walk", "point", "kneel", "sad", "look_up"]),
    "vieux": (dict(robe=True, bald=True, beard="#f2f0ea", cloth="#4a4448", trim="#7a6a5a", skin="#c8b0a0"),
              ["walk", "kneel", "sit", "sad"]),
    "enfant": (dict(child=True, hair="#4a2a18", cloth="#3a6a5a", trim="#d0b070", hairstyle="tuft", skin="#e8b890"),
               ["idle", "look_up", "kneel", "wave"]),
    "fuyard1": (dict(hair="#2a1a10", cloth="#6a3a3a", trim="#c09050", skin="#c89068"), ["flee", "run"]),
    "fuyard2": (dict(hair="#7a4a2a", cloth="#3a4a6a", trim="#e0c080", hairstyle="long", skin="#f0c098"), ["flee", "run"]),
    "fuyard3": (dict(child=True, hair="#3a2a1a", cloth="#8a6a2a", trim="#f0d080", skin="#e0a880"), ["flee", "run"]),
}


def folk_sheet(name):
    look, names = FOLK[name]
    return lambda: F.clips(F.Folk(**look), names)


# ======================================================================= candles
WAX = mat("bo_wax", ["#fff6e0", "#e8d8b4", "#c0ac88", "#8a7a60"], th=[0.82, 0.56, 0.3])
FL = {"warm": ["#fff6c8", "#ffd060", "#ff8a2a"], "green": ["#f0ffe0", "#b4f566", "#5ab02a"]}


def _candle(i, h=12, flame=None, size=1.0, smoke=False, drip=0):
    cv = Canvas(16, 32)
    cv.part()
    cv.rect(5, 31 - h, 11, 31, WAX)
    cv.part(line=False)
    for j in range(drip):
        cv.capsule((5.5 + j * 4, 31 - h + 1), (5.5 + j * 4, 31 - h + 3 + j), 0.8, 0.8, WAX, decal=True)
    f = cv.render()
    putpx(f, 8, 31 - h - 1, C("#3a2a20"))  # the wick
    top = 31 - h - 2
    if flame:
        cols = [C(c) for c in FL[flame]]
        sway = [0, 1, 0, -1, 0, 1][i % 6] * 0.6
        ht = (5 + (i % 3 == 1)) * size
        for y in range(int(ht)):
            t = y / max(1.0, ht)
            half = 1.8 * size * (1 - t) + 0.3
            xc = 8 + sway * t
            for x in range(int(xc - half), int(xc + half) + 1):
                d = abs(x + 0.5 - xc) / max(0.5, half)
                putpx(f, x, top - y, cols[min(2, int(d * 1.5 + t * 1.4))])
    if smoke:
        for y in range(10):
            putpx(f, 8 + 1.2 * math.sin(i * 1.1 + y * 0.6), top - y, C("#a8a0a8", 170 - y * 14), True)
    return f


def candle_clips():
    return [("unlit", [_candle(i, smoke=True) for i in range(6)], dict(fps=6)),
            ("light", [_candle(i, flame="warm", size=s) for i, s in enumerate((0.3, 0.6, 1.2, 0.9, 1.0))],
             dict(durations=[0.06, 0.08, 0.1, 0.08, 0.1], loop=False, next="lit")),
            ("lit", [_candle(i, flame="warm") for i in range(6)], dict(fps=9)),
            ("green", [_candle(i, flame="green", drip=2) for i in range(6)], dict(fps=8)),
            ("low", [_candle(i, h=5, flame="warm", size=0.8, drip=1) for i in range(6)], dict(fps=9)),
            ("out", [_candle(i, h=4, smoke=i < 5, drip=1) for i in range(6)], dict(durations=[0.1] * 5 + [0.4], loop=False))]


# ======================================================================= names, altar, soul, cobweb
STONE = mat("li_stone", ["#a8a49a", "#848076", "#625e56", "#44403a", "#2c2a26"], th=[0.86, 0.64, 0.42, 0.22])
STONE_D = mat("li_stone_d", ["#6a665e", "#4e4a44", "#36332e", "#24221e"], th=[0.78, 0.54, 0.3])
GOLDL = [C("#fff2c0"), C("#ffcf7a"), C("#c08a3a"), C("#5a4a3a")]
GREENL = [C("#f0ffe0"), C("#b4f566"), C("#5ab02a"), C("#3a4a30")]


def _plaque(i, glow=None, seed=0):
    cv = Canvas(32, 24)
    cv.part()
    cv.poly([(2, 22), (2, 6), (6, 2), (26, 2), (30, 6), (30, 22)], STONE, bevel=1.4)
    f = cv.render()
    rng = np.random.default_rng(500 + seed)
    cols = GOLDL if glow == "gold" else GREENL if glow == "green" else [C("#3a3630")] * 4
    for row in range(3):  # a name, written in three short lines
        x = 7
        while x < 25:
            ln = int(rng.integers(1, 4))
            lvl = 1 if glow else 0
            if glow:
                lvl = [0, 1, 1, 2, 1, 0][(i + row + x) % 6]
            for q in range(ln):
                putpx(f, x + q, 7 + row * 5, cols[lvl])
                if rng.random() < 0.5:
                    putpx(f, x + q, 8 + row * 5, cols[min(3, lvl + 1)])
            x += ln + 1
    return f


def plaque_clips():
    return [("idle", [_plaque(i) for i in range(2)], dict(fps=2)),
            ("gold", [_plaque(i, "gold") for i in range(6)], dict(fps=8)),
            ("green", [_plaque(i, "green") for i in range(6)], dict(fps=8))]


def _altar(i):
    cv = Canvas(96, 48)
    cv.part()
    cv.poly([(10, 47), (10, 24), (86, 24), (86, 47)], STONE, bevel=1.6)
    cv.part()
    cv.poly([(4, 26), (4, 18), (92, 18), (92, 26)], STONE, bevel=1.4)
    cv.part(line=False)
    for x in range(18, 80, 12):  # carved skulls along its side
        cv.disc((x, 34), 3.2, STONE_D, decal=True)
        cv.px_set(x - 1, 34, STONE_D, 0.1, decal=True)
    f = cv.render()
    for k, x in enumerate((10, 86)):  # a candle at each end
        c = _candle(i + k * 3)
        sub = c[12:32, :]
        for y in range(sub.shape[0]):
            for xx in range(16):
                if sub[y, xx, 3]:
                    putpx(f, x - 8 + xx, y - 2, tuple(int(v) for v in sub[y, xx]))
    return f


def altar_clips():
    return [("idle", [_altar(i) for i in range(6)], dict(fps=8))]


def _soul(i):
    f = np.zeros((32, 32, 4), np.uint8)
    a = TAU * i / 6
    for y in range(32):
        for x in range(32):
            dx, dy = x + 0.5 - 16, y + 0.5 - 20
            r = 6.0 - max(0.0, -dy) * 0.32
            sway = 1.5 * math.sin(a + dy * 0.35) * max(0.0, -dy) / 10
            d = math.hypot(dx - sway, dy * (0.6 if dy < 0 else 1.0))
            if d <= r and dy > -16:
                k = d / max(r, 0.5)
                f[y, x] = GREENL[0 if k < 0.35 else 1 if k < 0.7 else 2]
    for j in range(5):  # motes falling off it
        u = ((i / 6) + j / 5) % 1.0
        putpx(f, 16 + 7 * math.sin(j * 2.3 + a), 24 + u * 7, GREENL[1 if u < 0.5 else 2], True)
    return f


def soul_clips():
    return [("idle", [_soul(i) for i in range(6)], dict(fps=10))]


WEB = C("#d8d4e0", 170)


def _web(k, sway=0.0):
    f = np.zeros((32, 32, 4), np.uint8)
    for j in range(6):  # threads from the corner (top-left)
        a = PI * 0.05 + j * PI * 0.08
        ln = 30 * k
        for s in np.arange(0, ln, 0.5):
            putpx(f, 1 + math.cos(a) * s + sway * s / 30, 1 + math.sin(a) * s, WEB)
    for r in (8, 14, 20, 26):  # rings
        if r < 30 * k:
            for q in range(24):
                a = PI * 0.05 + q * PI * 0.4 / 23
                rr = r * (0.92 + 0.08 * math.sin(q * PI / 4))
                putpx(f, 1 + math.cos(a) * rr + sway * rr / 30, 1 + math.sin(a) * rr, WEB)
    return f


def web_clips():
    return [("grow", [_web(k) for k in (0.15, 0.3, 0.5, 0.7, 0.85, 1.0)], dict(durations=[0.3] * 6, loop=False, next="sway")),
            ("sway", [_web(1.0, s) for s in (0, 1, 1.5, 1, 0, -1)], dict(fps=4))]


WOOD = mat("li_wood", ["#7a5a40", "#5a402c", "#3e2a1c", "#261a10"], th=[0.8, 0.55, 0.3])
BONE = mat("li_bone", ["#f2ecdc", "#d4ccb4", "#a89e86", "#7a705c"], th=[0.82, 0.56, 0.3])
ROBE = mat("li_robe", ["#6a6468", "#4a4448", "#322e32", "#1e1a1e"], th=[0.8, 0.55, 0.3])


def _chair(cv):
    """A chair (frame 32x48, its seat 12 px high: a seated person of story_folk.py fits on it)."""
    cv.part()
    cv.rect(4, 10, 8, 48, WOOD)
    cv.part()
    cv.rect(4, 35, 27, 38, WOOD)
    cv.part()
    cv.rect(23, 38, 27, 48, WOOD)
    cv.part()
    cv.ellipse((6, 10), 3, 2.2, WOOD)


def chair_clips():
    fr = []
    for i in range(4):  # a glint of candlelight sliding over its wood
        cv = Canvas(32, 48)
        _chair(cv)
        f = cv.render()
        putpx(f, 6, 14 + i * 6, C("#c8a880"))
        putpx(f, 9 + i * 4, 35, C("#c8a880"))
        fr.append(f)
    return [("idle", fr, dict(fps=3))]


def _skeleton(i):
    cv = Canvas(32, 48)
    _chair(cv)
    cv.part()  # what is left of his robe
    cv.poly([(7, 36), (8, 18), (15, 16), (17, 24), (22, 32), (24, 37), (18, 38), (9, 38)], ROBE, bevel=1.0)
    cv.part()
    cv.capsule((12, 34), (21, 34), 1.3, 1.2, BONE)  # thigh
    cv.part()
    cv.capsule((21, 34), (22, 46), 1.2, 1.0, BONE)  # shin
    cv.part()
    cv.capsule((12, 20), (17, 28), 1.1, 1.0, BONE)  # arm
    cv.part()
    cv.capsule((17, 28), (21, 33), 1.0, 0.9, BONE)
    cv.part()
    hy = 12 + (i % 3 == 1)
    cv.disc((13, hy), 4.2, BONE)
    cv.part(line=False)
    cv.poly([(13, hy + 3), (18, hy + 3), (17, hy + 5), (13, hy + 5)], BONE, flat=0.5)
    f = cv.render()
    lv = [1, 0, 1, 2, 1, 1][i % 6]
    for x in (14, 17):  # the sockets, a green light in them
        putpx(f, x, hy - 1, GREENL[lv])
    return f


def skeleton_clips():
    return [("idle", [_skeleton(i) for i in range(6)], dict(fps=5))]


def _gate(i):
    cv = Canvas(128, 112)
    cv.part()
    cv.poly([(18, 111), (18, 40), (30, 22), (64, 10), (98, 22), (110, 40), (110, 111)], STONE, bevel=2.0)
    cv.part(line=1)
    cv.poly([(38, 111), (38, 52), (48, 38), (64, 32), (80, 38), (90, 52), (90, 111)], mat("li_dark", ["#0c0a0e"], outline=None),
            flat=0.5)
    cv.part(line=False)
    for y in range(22, 111, 9):  # its blocks
        cv.line((18, y), (38, y), STONE_D, 0.3, decal=True)
        cv.line((90, y), (110, y), STONE_D, 0.3, decal=True)
    for x in (55, 64, 73):  # a skull above the door
        cv.disc((x, 24), 3.0, BONE, decal=True)
    f = cv.render()
    for k, x in enumerate((10, 118)):  # torches
        for y in range(54, 70):
            putpx(f, x, y, C("#5a402c"))
        sway = [0, 1, 0, -1, 0, 1][(i + 3 * k) % 6]
        for y in range(9):
            half = 2.5 * (1 - y / 9) + 0.3
            for xx in range(int(x - half), int(x + half) + 1):
                d = abs(xx + 0.5 - x - sway * y / 9) / max(0.5, half)
                putpx(f, xx + sway * y // 9, 53 - y, C(FL["warm"][min(2, int(d * 1.5 + y / 6))]))
    return f


def gate_clips():
    return [("idle", [_gate(i) for i in range(6)], dict(fps=9))]


SHEETS = {
    "bougie": (16, 32, [8, 31], candle_clips),
    "plaque": (32, 24, [16, 12], plaque_clips),
    "autel": (96, 48, [48, 47], altar_clips),
    "ame": (32, 32, [16, 20], soul_clips),
    "toile": (32, 32, [0, 0], web_clips),
    "chaise": (32, 48, [16, 47], chair_clips),
    "squelette": (32, 48, [16, 47], skeleton_clips),
    "porte": (128, 112, [64, 111], gate_clips),
    "etoile": (96, 96, [48, 48], star_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
for _n in FOLK:
    SHEETS[_n] = (F.FW, F.FH, F.PIVOT, folk_sheet(_n))
