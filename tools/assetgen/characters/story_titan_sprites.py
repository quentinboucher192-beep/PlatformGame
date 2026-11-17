"""The sprites of the Titan's story (« Le Dernier Quart », 2.63): every actor of its six animated
shots, drawn frame by frame like the game's sprites (sheets under assets/stories/titan/, clips in
assets/animations/stories/titan_<name>.json; data/stories/titan.json places them).

  ouvrier1-3   workers of the foundry: helmet, apron, hammer (story_folk.py)         40x56
  contremaitre the foreman                                                           40x56
  creuset      a crucible on its chains, pouring molten steel                        64x80
  moule        a mould, the steel in it glowing, cooling                             64x24
  engrenage    a gear turning                                                        48x48
  vapeur       a puff of steam (a burst, or a jet)                                   64x64
  echafaudage  the scaffold around the colossus, its lamps                           160x160
  crochet      the crane's hook, swaying, a plate of brass on it                     48x96
  fourneau     a furnace, its fire roaring                                           96x96
  etoile       the star of the centre: shining, collapsing into a black hole         96x96
  astre        a twinkling star                                                      16x16

The Titan is the game's own sheet (animations/titan.json); the visitor at the end is Nova
(animations/nova.json).  One sheet pixel is two units of the story's stage.  Deterministic."""
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
    "ouvrier1": (dict(hair="#2a1a10", cloth="#5a5048", trim="#8a7a5a", apron="#7a5a3a", helmet="#c89a40", item="hammer",
                      skin="#c89068"), ["idle", "walk", "hammer", "look_up", "wave"]),
    "ouvrier2": (dict(hair="#6a3a1a", cloth="#3a4a5a", trim="#a08a5a", apron="#6a4a30", helmet="#b08a3a", item="hammer",
                      skin="#e8b890"), ["idle", "walk", "hammer", "look_up", "wave"]),
    "ouvrier3": (dict(hair="#3a2a1a", cloth="#4a3a3a", trim="#c09a5a", apron="#5a4a3a", helmet="#d0a848", item="lantern",
                      skin="#a87050"), ["idle", "walk", "look_up", "point", "wave"]),
    "contremaitre": (dict(hair="#d8d4cc", beard="#d8d4cc", cloth="#2a3a5a", trim="#e0c070", helmet="#e8c060", skin="#d8a880"),
                     ["idle", "walk", "point", "wave", "look_up"]),
}


def folk_sheet(name):
    look, names = FOLK[name]
    return lambda: F.clips(F.Folk(**look), names)


GUN = mat("ti_gun", ["#b6c2ce", "#8a97a6", "#66717f", "#474f5b", "#2c323b"], th=[0.86, 0.68, 0.48, 0.28])
BRASS = mat("ti_brass", ["#f4dc98", "#cba553", "#98773a", "#664c22", "#3c2a14"], th=[0.86, 0.66, 0.46, 0.26])
IRON = mat("ti_iron", ["#7a7276", "#5a5458", "#3e3a3e", "#28252a"], th=[0.78, 0.55, 0.32])
MOLTEN = [C("#ffffff"), C("#fff2a0"), C("#ffc040"), C("#ff7a2a"), C("#c83a1a"), C("#6a1a10")]
FIRE = [mat(f"ti_f{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#fff6c8", "#ffd060", "#ff8a2a", "#d2481a"])]


# ======================================================================= the crucible, the mould, gears, steam
def _crucible(i, pour):
    cv = Canvas(64, 80)
    a = -0.5 if pour else 0.0
    cx, cy = 30, 24
    for dx in (-10, 10):  # its chains
        for y in range(0, 14, 3):
            cv.part()
            cv.ellipse((cx + dx * 0.9, y + 1.5), 1.2, 1.6, IRON)
    cv.part()

    def R(px, py):
        dx, dy = px - cx, py - cy
        return (cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a))

    cv.poly([R(16, 14), R(44, 14), R(40, 34), R(20, 34)], IRON, bevel=1.6)
    cv.part(line=False)
    cv.poly([R(18, 14), R(42, 14), R(41, 17), R(19, 17)], mat("ti_mol", ["#ffd060"], emissive=True, outline=None), flat=0.6)
    f = cv.render()
    if pour:  # the stream of molten steel down to the mould
        lip = R(44, 14)
        for y in range(int(lip[1]), 80):
            u = (y - lip[1]) / max(1.0, 80 - lip[1])
            x = lip[0] + 2 + 4 * u * u + 0.6 * math.sin(y * 0.7 + i * 1.3)
            w = 1.8 - 0.6 * u
            for xx in range(int(x - w), int(x + w) + 1):
                d = abs(xx + 0.5 - x) / max(0.5, w)
                putpx(f, xx, y, MOLTEN[min(5, int(d * 2.2 + (i + y) % 3 * 0.4))])
        for k in range(4):  # drops and sparks
            putpx(f, lip[0] + 6 + k * 2, 30 + ((i * 7 + k * 13) % 40), MOLTEN[1])
    else:  # the molten surface glinting
        putpx(f, 22 + 7 * i, 15, MOLTEN[0])
    return f


def crucible_clips():
    return [("idle", [_crucible(i, False) for i in range(2)], dict(fps=2)), ("pour", [_crucible(i, True) for i in range(6)], dict(fps=12))]


def mould_clips():
    fr = []
    for i in range(6):
        cv = Canvas(64, 24)
        cv.part()
        cv.poly([(2, 23), (2, 8), (62, 8), (62, 23)], IRON, bevel=1.4)
        f = cv.render()
        for x in range(6, 58):
            for y in range(10, 14):
                v = 0.5 + 0.5 * math.sin(x * 0.4 - i * 1.0 + y)
                putpx(f, x, y, MOLTEN[min(5, 1 + int((1 - v) * 2.6) + (y - 10) // 2)])
        fr.append(f)
    return [("idle", fr, dict(fps=8))]


def _gear(f, cx, cy, r, teeth, rot, m_light, m_dark):
    for y in range(int(cy - r - 3), int(cy + r + 4)):
        for x in range(int(cx - r - 3), int(cx + r + 4)):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            ang = math.atan2(dy, dx) - rot
            tooth = (math.cos(ang * teeth) > 0.2)
            rr = r + (2.5 if tooth else 0.0)
            if d <= rr and d > r * 0.28:
                hole = r * 0.45 < d < r * 0.7 and math.cos(ang * 4) > 0.55
                if hole:
                    continue
                lit = dx * -0.6 + dy * -0.8
                putpx(f, x, y, m_light if lit > 0 or d > r else m_dark)
            elif d <= r * 0.28 and d > r * 0.14:
                putpx(f, x, y, m_dark)


def gear_clips():
    fr = []
    for i in range(8):
        f = np.zeros((48, 48, 4), np.uint8)
        _gear(f, 24, 24, 16, 10, TAU * i / 8 / 10, C("#e8c878"), C("#98773a"))
        fr.append(f)
    return [("spin", fr, dict(fps=10))]


STEAM = mat("ti_steam", ["#f6f8fa", "#d8dee6", "#b0b8c4", "#8a92a0"], outline=None, th=[0.8, 0.55, 0.3])


def _steam(k, seed, jet=False, i=0):
    cv = Canvas(64, 64)
    rng = np.random.default_rng(seed)
    for j in range(8):
        u = (j / 8 + (i / 6 if jet else 0)) % 1.0 if jet else j / 8
        if jet:
            x, y = 32 + rng.uniform(-3, 3) + u * 6, 58 - u * 50
            r = 2 + u * 9
        else:
            x = 32 + rng.uniform(-10, 10) * (0.4 + k)
            y = 50 - k * 26 - rng.uniform(0, 10) * k
            r = (3 + rng.uniform(0, 5)) * (0.5 + 0.9 * k)
        cv.part(line=False)
        cv.disc((x, y), r, STEAM)
    f = cv.render(outline=False)
    fade = k if not jet else 0.0
    holes = np.random.default_rng(seed + 7 + i).random(f.shape[:2]) < (max(0.0, fade - 0.4) * 1.6 if not jet else 0.0)
    if jet:
        yy = np.indices(f.shape[:2])[0]
        holes |= (np.random.default_rng(seed + i).random(f.shape[:2]) < (1 - yy / 64) * 0.7)
    f[holes] = 0
    return f


def steam_clips():
    return [("puff", [_steam(i / 8, 600 + i) for i in range(9)], dict(durations=[0.05, 0.06, 0.07, 0.09, 0.11, 0.13, 0.15, 0.17, 0.2],
                                                                     loop=False)),
            ("jet", [_steam(0.0, 620, jet=True, i=i) for i in range(6)], dict(fps=12))]


# ======================================================================= the scaffold, the hook, the furnace
def scaffold_clips():
    fr = []
    for i in range(4):
        cv = Canvas(160, 160)
        for x in (8, 76, 150):  # posts
            cv.part()
            cv.rect(x, 4, x + 4, 160, IRON)
        for y in (40, 96, 150):  # planks
            cv.part()
            cv.rect(4, y, 156, y + 4, mat("ti_plank", ["#a07a4a", "#7a5634", "#523820"], th=[0.7, 0.4]))
        for (x0, y0, x1, y1) in ((10, 44, 76, 96), (80, 44, 150, 96), (10, 100, 76, 150), (80, 100, 150, 150)):
            cv.part(line=False)
            cv.line((x0, y0), (x1, y1), IRON, 0.4)
        f = cv.render()
        for k, (x, y) in enumerate(((40, 34), (118, 90), (60, 144))):  # lamps
            on = (i + k) % 4 != 3
            putpx(f, x, y, C("#fff2c0" if on else "#a08040"))
            putpx(f, x + 1, y, C("#ffd27a" if on else "#7a6030"))
        fr.append(f)
    return [("idle", fr, dict(fps=4))]


def hook_clips():
    fr = []
    for i in range(6):
        sw = 2.0 * math.sin(TAU * i / 6)
        cv = Canvas(48, 96)
        for y in range(0, 58, 3):
            cv.part()
            cv.ellipse((24 + sw * y / 58, y + 1.5), 1.2, 1.6, IRON)
        cv.part()
        hx = 24 + sw
        cv.capsule((hx, 58), (hx, 64), 2.0, 2.0, GUN)
        cv.part()
        cv.poly([(hx - 18, 64), (hx + 18, 64), (hx + 16, 90), (hx - 16, 90)], BRASS, bevel=1.8)
        cv.part(line=False)
        for (rx, ry) in ((-12, 69), (12, 69), (-12, 85), (12, 85)):
            cv.disc((hx + rx, ry), 1.2, GUN, decal=True)
        fr.append(cv.render())
    return [("idle", fr, dict(fps=5))]


def _furnace(i):
    cv = Canvas(96, 96)
    cv.part()
    cv.poly([(8, 95), (8, 30), (20, 14), (76, 14), (88, 30), (88, 95)], mat("ti_brick", ["#8a5a4a", "#6a4234", "#4a2c22", "#2e1a14"],
                                                                         th=[0.8, 0.55, 0.3]), bevel=2.0)
    cv.part()
    cv.rect(36, 0, 60, 16, IRON)
    cv.part(line=1)
    cv.poly([(24, 95), (24, 60), (32, 48), (64, 48), (72, 60), (72, 95)], IRON, bevel=1.2, base=0.3)
    f = cv.render()
    a = TAU * i / 6
    for y in range(52, 92):  # its fire
        for x in range(28, 69):
            if f[y, x, 3] == 0:
                continue
            inside = (x - 48) ** 2 / 400 + (y - 72) ** 2 / 500 < 1
            if inside:
                v = 0.5 + 0.5 * math.sin(x * 0.5 + a * 2 + math.sin(y * 0.3 + a) * 2)
                t = (92 - y) / 40
                ci = min(5, max(0, int(t * 3.2 + (1 - v) * 1.6)))
                putpx(f, x, y, MOLTEN[ci])
    for x in range(14, 84, 6):  # rivets glinting
        putpx(f, x, 32, C("#c8a080") if (x // 6 + i) % 4 == 0 else C("#6a4234"))
    return f


def furnace_clips():
    return [("idle", [_furnace(i) for i in range(6)], dict(fps=9))]


SHEETS = {
    "creuset": (64, 80, [30, 0], crucible_clips),
    "moule": (64, 24, [32, 23], mould_clips),
    "engrenage": (48, 48, [24, 24], gear_clips),
    "vapeur": (64, 64, [32, 60], steam_clips),
    "echafaudage": (160, 160, [80, 159], scaffold_clips),
    "crochet": (48, 96, [24, 0], hook_clips),
    "fourneau": (96, 96, [48, 95], furnace_clips),
    "etoile": (96, 96, [48, 48], star_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
for _n in FOLK:
    SHEETS[_n] = (F.FW, F.FH, F.PIVOT, folk_sheet(_n))
