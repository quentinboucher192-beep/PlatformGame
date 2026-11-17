"""The sprites of the Monolithe's story (« Le Golem Primordial », 2.63): every actor of its six
animated shots, drawn frame by frame like the game's sprites (sheets under assets/stories/
monolithe/, clips in assets/animations/stories/monolithe_<name>.json; data/stories/monolithe.json
places and moves them).

  caillou     the pebble in the dust of the beginning, tumbling           48x48
  grain       a grain of rock drifting with it                            16x16
  rocher      the stone of the cave: carved, its runes lighting one by one, waking   112x96
  enfant1     a child of the Confins with a chisel (story_folk.py)        40x56
  enfant2     a child with a torch                                        40x56
  enfant3     a child who plays                                           40x56
  ancien      an elder (robe, beard, chisel)                              40x56
  villageois  a villager with a torch                                     40x56
  villageoise a villager                                                  40x56
  hutte       a hut of the cave village: its window, its smoke; « ruin »: abandoned   96x80
  feu         the village fire                                            48x40
  torche      a torch on the cave wall                                    24x40
  vaisseau    a ship leaving for the stars                                64x56
  nuage       a cloud of dust (puff)                                      96x48
  astre       a twinkling star                                            16x16

The grown golem is the game's own sheet (animations/monolithe.json); the visitor at the end is
Nova (animations/nova.json).  One sheet pixel is two units of the story's stage.  Deterministic."""
import math

import numpy as np

import story_folk as F
from boss_a_common import mat
from raster import Canvas, TCanvas

PI = math.pi
TAU = 2 * PI


def C(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def blank(w, h):
    return np.zeros((h, w, 4), np.uint8)


def putpx(img, x, y, c, only_empty=False):
    x, y = int(math.floor(x)), int(math.floor(y))
    if 0 <= y < img.shape[0] and 0 <= x < img.shape[1]:
        if only_empty and img[y, x, 3]:
            return
        img[y, x] = c


# the boss's stone, moss and amber light (boss_f_monolithe.py, phase 1)
GRANITE = mat("mo_granite", ["#b3a691", "#8e8270", "#6c6255", "#4b443b", "#2f2a24"], th=[0.86, 0.68, 0.48, 0.28])
GRANITE_D = mat("mo_granite_d", ["#83796b", "#625a4f", "#463f37", "#2c2823"], th=[0.8, 0.58, 0.34])
MOSS = mat("mo_moss", ["#a6bb63", "#77953f", "#506f2f", "#334b25"], th=[0.8, 0.6, 0.38])
AMBER = ["#ffeccc", "#ffb340", "#e2771a", "#9e4a16", "#52240e"]
AMB = [mat(f"mo_amb{i}", [c], emissive=True, outline=None) for i, c in enumerate(AMBER)]
SPECK = mat("mo_speck", ["#d8ccb4", "#9a8e78"], th=[0.5])
RUNES = [
    [((0, 0), (0, 6)), ((0, 1), (3, 0)), ((0, 3), (3, 2))],
    [((0, 0), (0, 6)), ((0, 0), (3, 2)), ((3, 2), (0, 4)), ((0, 4), (3, 6))],
    [((2, 0), (2, 6)), ((0, 1), (4, 5)), ((4, 1), (0, 5))],
    [((0, 6), (2, 0)), ((2, 0), (4, 6)), ((1, 3), (3, 3))],
    [((0, 0), (0, 6)), ((4, 0), (4, 6)), ((0, 0), (4, 6))],
    [((2, 0), (0, 3)), ((0, 3), (2, 6)), ((2, 6), (4, 3)), ((4, 3), (2, 0))],
]


def _stone(cv, cx, cy, r, rot, seed, m=GRANITE, sq=0.86, n=9, rough=0.18, bevel=1.6):
    rng = np.random.default_rng(seed)
    radii = [r * (1 - rough + 2 * rough * rng.random()) for _ in range(n)]
    pts = []
    for i in range(n):
        a = TAU * i / n + rot
        pts.append((cx + math.cos(a) * radii[i], cy + math.sin(a) * radii[i] * sq))
    cv.poly(pts, m, bevel=bevel, base=0.6)
    return pts


# ======================================================================= the pebble, the grains
def pebble_clips():
    fr = []
    n = 12
    for i in range(n):
        cv = Canvas(48, 48)
        cv.part()
        _stone(cv, 24, 24, 13, TAU * i / n, 11, sq=0.9, n=10, rough=0.16, bevel=2.0)
        cv.part(line=False)
        rng = np.random.default_rng(12)
        for _ in range(9):  # specks turning with it
            a, rr = rng.uniform(0, TAU), rng.uniform(2, 10)
            x, y = 24 + math.cos(a + TAU * i / n) * rr, 24 + math.sin(a + TAU * i / n) * rr * 0.9
            cv.px_set(x, y, SPECK, 0.8 if rng.random() < 0.5 else 0.3, decal=True)
        f = cv.render()
        # a glint of the far star on its lit edge, coming and going
        if i % 4 in (1, 2):
            for (dx, dy) in ((0, 0), (1, 0), (0, 1)):
                putpx(f, 15 + dx + (i % 4), 15 + dy, C("#fff6e0"), False)
        fr.append(f)
    return [("spin", fr, dict(fps=5))]


def grain_clips():
    fr = []
    for i in range(6):
        cv = Canvas(16, 16)
        cv.part()
        _stone(cv, 8, 8, 4.5, TAU * i / 6, 21 + i % 2, sq=0.8, n=6, rough=0.25, bevel=1.2)
        fr.append(cv.render())
    return [("spin", fr, dict(fps=6))]


# ======================================================================= the stone of the cave
RC = (56.0, 58.0)  # its centre in the 112x96 frame (its foot on y = 90)
RUNE_AT = [(48, 52), (64, 40), (34, 64), (72, 62), (44, 34), (82, 48), (58, 70)]


def _rune(cv, k, x, y, lvl, size=1.6, upto=None):
    """Rune k at (x, y) (its box 4x6 times size); lvl 0..4 the light; `upto` strokes only."""
    strokes = RUNES[k % len(RUNES)]
    if upto is not None:
        strokes = strokes[:upto]
    for (a, b) in strokes:
        cv.line((x + a[0] * size, y + a[1] * size), (x + b[0] * size, y + b[1] * size), AMB[max(0, min(4, lvl))], 0.6, decal=True)
        cv.line((x + a[0] * size + 1, y + a[1] * size), (x + b[0] * size + 1, y + b[1] * size), AMB[max(0, min(4, lvl + 1))], 0.6,
                decal=True)


def _rock(runes=(), partial=None, shake=0.0, moss=1.0, cracks=0.0, seed=0, phase=0.0):
    cv = TCanvas(112, 96, offset=(shake, 0.0))
    cv.part()
    pts = []
    rng = np.random.default_rng(31)
    for i in range(12):
        a = TAU * i / 12 - PI / 2
        rr = 36 * (0.86 + 0.24 * rng.random())
        pts.append((RC[0] + math.cos(a) * rr * 1.15 + shake, RC[1] + math.sin(a) * rr * 0.86))
    pts = [(x, min(y, 90.0)) for (x, y) in pts]
    cv.poly(pts, GRANITE, bevel=2.2, base=0.58)
    cv.part(line=1)  # a facet line, a second block on its back
    cv.poly([(RC[0] - 30 + shake, 90), (RC[0] - 34 + shake, 66), (RC[0] - 18 + shake, 58), (RC[0] - 8 + shake, 90)], GRANITE_D,
            bevel=1.2, base=0.5)
    cv.part(line=False)
    rng = np.random.default_rng(32)
    for _ in range(26):
        x, y = rng.uniform(26, 88), rng.uniform(32, 86)
        cv.px_set(x + shake, y, SPECK, 0.8 if rng.random() < 0.4 else 0.3, decal=True)
    if moss > 0:
        cv.part(line=False)
        for j in range(int(5 * moss) + 2):
            x = 34 + j * 7.5
            y = RC[1] - 30 + abs(x - RC[0]) * 0.28
            cv.ellipse((x + shake, y), 5.0, 2.4, MOSS, decal=True)
            if j % 2 == 0:
                cv.capsule((x + 1 + shake, y + 1), (x + 1.5 + shake, y + 5 + j % 3), 0.8, 0.5, MOSS, decal=True)
    for idx, lvl in runes:
        x, y = RUNE_AT[idx]
        _rune(cv, idx, x + shake, y, lvl)
    if partial:
        idx, upto, lvl = partial
        x, y = RUNE_AT[idx]
        _rune(cv, idx, x + shake, y, lvl, upto=upto)
    if cracks > 0:
        rng = np.random.default_rng(40 + seed)
        for _ in range(int(cracks * 8)):
            x0, y0 = rng.uniform(34, 80), rng.uniform(38, 80)
            ang = rng.uniform(0, TAU)
            ln = rng.uniform(4, 10) * cracks
            cv.line((x0 + shake, y0), (x0 + math.cos(ang) * ln + shake, y0 + math.sin(ang) * ln), AMB[1], 0.6, decal=True)
    return cv.render()


def rock_clips():
    clips = []
    fr = []
    for i in range(4):  # a glint of torchlight sliding over its specks
        f = _rock(moss=1.0)
        for (x, y) in ((36 + 9 * i, 44 + 3 * i), (70 - 6 * i, 70 - 2 * i)):
            putpx(f, x, y, C("#fff2d0"))
            putpx(f, x + 1, y, C("#d8ccb4"))
        fr.append(f)
    clips.append(("idle", fr, dict(fps=3)))
    # carve: the first rune, stroke by stroke (a spark where the chisel bites)
    fr = []
    for i in range(9):
        upto = min(3, i // 2 + 1)
        f = _rock(partial=(0, upto, 1 if i % 2 else 2))
        x, y = RUNE_AT[0]
        a, b = RUNES[0][upto - 1]
        putpx(f, x + b[0] * 1.6 + 1, y + b[1] * 1.6, C("#fff6e0"))
        putpx(f, x + b[0] * 1.6 + 2, y + b[1] * 1.6 - 1, C(AMBER[1]))
        fr.append(f)
    clips.append(("carve", fr, dict(durations=[0.3, 0.12, 0.34, 0.12, 0.34, 0.12, 0.3, 0.12, 0.3], loop=False, next="rune")))
    lv = [1, 0, 1, 2, 3, 2]
    clips.append(("rune", [_rock(runes=[(0, lv[i])]) for i in range(6)], dict(fps=6)))
    # grow: generation after generation, a rune lights up, the moss spreads
    fr = []
    for i in range(12):
        n = min(7, 1 + (i + 1) // 2 + (1 if i >= 10 else 0))
        rs = [(j, 1 if j < n - 1 else 0) for j in range(n)]
        fr.append(_rock(runes=rs, moss=1.0 + i * 0.12))
    clips.append(("grow", fr, dict(durations=[0.3] * 12, loop=False, next="runes")))
    clips.append(("runes", [_rock(runes=[(j, lv[(i + j) % 6]) for j in range(7)], moss=2.4) for i in range(6)], dict(fps=7)))
    # wake: it shivers, light runs in its cracks
    fr = []
    for i in range(6):
        fr.append(_rock(runes=[(j, 0) for j in range(7)], moss=2.4, shake=[-1, 1, 0, 1, -1, 0][i], cracks=0.4 + 0.12 * i, seed=i))
    clips.append(("wake", fr, dict(fps=14)))
    return clips


# ======================================================================= the people
FOLK = {
    "enfant1": (dict(child=True, hair="#5a3a20", cloth="#c0603a", trim="#e8c070", hairstyle="tuft", item="chisel"),
                ["idle", "walk", "carve", "cheer", "look_up"]),
    "enfant2": (dict(child=True, hair="#2a1a10", cloth="#4a8a6a", trim="#d8b060", hairstyle="long", item="torch", skin="#d8a070"),
                ["idle", "walk", "cheer", "look_up", "wave"]),
    "enfant3": (dict(child=True, hair="#8a5a2a", cloth="#a04a7a", trim="#f0d080", skin="#f0c8a0"),
                ["idle", "run", "cheer", "wave", "look_up"]),
    "ancien": (dict(robe=True, bald=True, beard="#e8e4dc", cloth="#6a5a7a", trim="#c8a050", item="chisel", skin="#d8a880"),
               ["idle", "walk", "carve", "look_up"]),
    "villageois": (dict(hair="#3a2618", cloth="#5a6a3a", trim="#c09050", item="torch", skin="#c89068"),
                   ["idle", "walk", "carve", "look_up", "point"]),
    "villageoise": (dict(hair="#6a3a1a", cloth="#3a5a8a", trim="#e0c080", hairstyle="long", skin="#f0c098"),
                    ["idle", "walk", "kneel", "look_up", "wave"]),
}


def folk_sheet(name):
    look, names = FOLK[name]
    return lambda: F.clips(F.Folk(**look), names)


# ======================================================================= the village
CLAY = mat("hu_clay", ["#c09a72", "#9a7656", "#76563e", "#523a2a", "#34241a"], th=[0.86, 0.66, 0.44, 0.24])
THATCH = mat("hu_thatch", ["#c8a85a", "#a08240", "#78602c", "#4e3e1c"], th=[0.82, 0.58, 0.34])
WOOD = mat("hu_wood", ["#8a6440", "#6a4a2c", "#4a321c", "#2c1e12"], th=[0.8, 0.55, 0.3])
WIN = [mat(f"hu_win{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#fff2c0", "#ffd27a", "#f0a040", "#a05a20"])]
DARKWIN = mat("hu_dark", ["#1a120e"], outline=None)
SMOKE = mat("hu_smoke", ["#cfc6c0", "#a8a09a", "#7e7872"], outline=None, th=[0.66, 0.33])
WEB = C("#c8c4d0", 150)


def _hut(i, ruin=False):
    cv = Canvas(96, 80)
    cv.part()
    if ruin:  # the dome fallen in: a broken rim
        rng = np.random.default_rng(55)
        pts = [(14, 76), (14, 52)]
        for k in range(9):
            x = 18 + k * 7
            pts.append((x, 40 + rng.uniform(0, 12) + (6 if 3 <= k <= 5 else 0)))
        pts += [(82, 52), (82, 76)]
        cv.poly(pts, CLAY, bevel=1.6, base=0.5)
    else:
        cv.ellipse((48, 60), 36, 30, CLAY, within=lambda x, y: y < 76.5)
        cv.part()
        cv.ellipse((48, 34), 26, 12, THATCH, within=lambda x, y: y < 40)
        cv.capsule((48, 22), (48, 24), 5, 5, THATCH)
    cv.part()
    cv.poly([(38, 77), (38, 58), (43, 54), (49, 54), (54, 58), (54, 77)], WOOD, bevel=1.0, base=0.45)
    cv.part(line=False)
    if ruin:
        cv.rect(66, 56, 74, 63, DARKWIN, flat=0.5)
    else:
        lv = [1, 0, 1, 2, 1, 1][i % 6]
        cv.rect(66, 56, 74, 63, WIN[lv], flat=0.5)
        cv.rect(68, 58, 72, 61, WIN[max(0, lv - 1)], flat=0.5)
        cv.line((70, 55), (70, 63), WOOD, 0.3)
    f = cv.render()
    if not ruin:  # smoke from the roof
        rng = np.random.default_rng(60)
        for k in range(4):
            u = ((i / 6) + k / 4) % 1.0
            cx, cy = 48 + 4 * math.sin(u * 5 + k), 18 - u * 16
            r = 1.5 + u * 3.5
            for y in range(int(cy - r), int(cy + r) + 1):
                for x in range(int(cx - r), int(cx + r) + 1):
                    if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r and (x + y + k) % 2 == 0 and rng.random() > u * 0.6:
                        putpx(f, x, y, C("#b8b0aa", int(200 * (1 - u))), True)
    else:  # a cobweb in the door, a rag swaying
        for k in range(6):
            putpx(f, 39 + k, 58 + k // 2, WEB, False)
            putpx(f, 53 - k, 58 + k // 2, WEB, False)
        sw = [0, 1, 1, 0, -1, -1][i % 6]
        for y in range(6):
            putpx(f, 76 + sw * (y // 3), 46 + y, C("#6a4a5a"), False)
            putpx(f, 77 + sw * (y // 3), 46 + y, C("#4a3040"), False)
    return f


def hut_clips():
    return [("idle", [_hut(i) for i in range(6)], dict(fps=5)), ("ruin", [_hut(i, True) for i in range(6)], dict(fps=4))]


FIRE = [mat(f"fi_{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#fff6c8", "#ffd060", "#ff8a2a", "#d2481a"])]


def _flames(cv, cx, by, k, n=3, hgt=14.0, wid=5.0):
    a = TAU * k / 6
    for j in range(n):
        x0 = cx + (j - (n - 1) / 2) * wid * 0.9
        ht = hgt * (0.7 + 0.3 * math.sin(a * 2 + j * 1.9)) * (1.0 if j == n // 2 else 0.75)
        sway = 1.5 * math.sin(a + j * 2.3)
        for layer, (m, wf) in enumerate(zip(FIRE[::-1], (1.0, 0.75, 0.5, 0.25))):
            w = wid * wf * 0.6
            pts = [(x0 - w, by), (x0 + w, by), (x0 + w * 0.4 + sway * 0.5, by - ht * wf * 0.6), (x0 + sway, by - ht * (0.5 + 0.5 * wf))]
            cv.part(line=False)
            cv.poly(pts, m, flat=0.6)


def fire_clips():
    fr = []
    for i in range(6):
        cv = Canvas(48, 40)
        cv.part()
        cv.capsule((10, 35), (36, 31), 2.6, 2.2, WOOD)
        cv.part()
        cv.capsule((12, 31), (38, 35), 2.4, 2.6, WOOD)
        for j in range(5):
            cv.part()
            cv.disc((8 + j * 8, 37), 2.6, GRANITE)
        _flames(cv, 24, 33, i)
        fr.append(cv.render())
    return [("idle", fr, dict(fps=9))]


def torch_clips():
    fr = []
    for i in range(6):
        cv = Canvas(24, 40)
        cv.part()
        cv.rect(9, 30, 15, 33, WOOD)
        cv.part()
        cv.capsule((12, 36), (12, 18), 1.6, 2.0, WOOD)
        _flames(cv, 12, 17, i, n=1, hgt=12, wid=5)
        fr.append(cv.render())
    return [("idle", fr, dict(fps=9))]


# ======================================================================= the ships
HULL = mat("sh_hull", ["#e8ecf4", "#b8c0d4", "#8a92aa", "#5c6480", "#383e56"], th=[0.86, 0.66, 0.44, 0.24])
HULL_D = mat("sh_hull_d", ["#8a92aa", "#5c6480", "#383e56"], th=[0.7, 0.4])
GLASS = mat("sh_glass", ["#e8fbff", "#8ad8f0", "#3a8ab0"], emissive=True, outline=None, th=[0.75, 0.42])
THRUST = [mat(f"sh_t{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#ffffff", "#bfeaff", "#6ac0ff", "#3a6ad0"])]
BLINK = [C("#ff6a5a"), C("#7aff9a")]


def _ship(i, thrust):
    cv = Canvas(64, 56)
    cv.part()
    cv.poly([(22, 40), (14, 46), (16, 34)], HULL_D, bevel=1.0)
    cv.part()
    cv.poly([(42, 40), (50, 46), (48, 34)], HULL_D, bevel=1.0)
    cv.part()
    cv.ellipse((32, 26), 11, 17, HULL)
    cv.part(line=False)
    cv.ellipse((32, 20), 4.5, 5.5, GLASS)
    cv.part()
    cv.rect(26, 40, 39, 44, HULL_D)
    if thrust:
        a = TAU * i / 6
        for layer, (m, wf) in enumerate(zip(THRUST[::-1], (1.0, 0.75, 0.5, 0.3))):
            ln = (8 + 3 * math.sin(a * 2 + layer)) * wf + 2
            w = 5.0 * wf
            cv.part(line=False)
            cv.poly([(32 - w, 44), (32 + w, 44), (32 + w * 0.3, 44 + ln), (32, 46 + ln + (i % 2)), (32 - w * 0.3, 44 + ln)], m, flat=0.6)
    f = cv.render()
    putpx(f, 16, 44, BLINK[0] if i % 3 == 0 else C("#6a2a2a"))
    putpx(f, 48, 44, BLINK[1] if i % 3 == 1 else C("#2a5a3a"))
    return f


def ship_clips():
    return [("idle", [_ship(i, False) for i in range(6)], dict(fps=4)),
            ("launch", [_ship(i, True) for i in range(6)], dict(fps=14))]


# ======================================================================= dust, stars
DUST = mat("du_dust", ["#e2d4b8", "#bca88a", "#94826a", "#6c5e4c"], outline=None, th=[0.8, 0.55, 0.3])


def puff_clips():
    fr = []
    for i in range(9):
        k = i / 8
        cv = Canvas(96, 48)
        rng = np.random.default_rng(70)
        for j in range(9):
            side = -1 if j % 2 else 1
            x = 48 + side * (6 + j * 3.8) * (0.3 + 0.9 * k)
            r = (4 + rng.uniform(0, 4)) * (0.5 + 0.9 * k)
            y = 42 - r * 0.6 - k * 6
            cv.part(line=False)
            cv.disc((x, y), r, DUST)
        f = cv.render(outline=False)
        if k > 0.4:
            rng = np.random.default_rng(80 + i)
            holes = rng.random(f.shape[:2]) < (k - 0.4) * 1.5
            f[holes] = 0
        fr.append(f)
    return [("puff", fr, dict(durations=[0.05, 0.06, 0.07, 0.09, 0.11, 0.13, 0.15, 0.17, 0.2], loop=False))]


STAR_C = [C("#ffffff"), C("#f6f0ff"), C("#e0d8ff"), C("#a8a0d8"), C("#6a6aa0")]


def twinkle_clips():
    fr = []
    for i in range(6):
        f = blank(16, 16)
        sz = [0, 1, 2, 3, 2, 1][i]
        putpx(f, 8, 8, STAR_C[0])
        for d in range(1, sz + 1):
            c = STAR_C[1 if d == 1 else 3]
            for (dx, dy) in ((d, 0), (-d, 0), (0, d), (0, -d)):
                putpx(f, 8 + dx, 8 + dy, c)
        if sz >= 2:
            for (dx, dy) in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                putpx(f, 8 + dx, 8 + dy, STAR_C[4])
        fr.append(f)
    return [("twinkle", fr, dict(durations=[0.3, 0.12, 0.1, 0.16, 0.1, 0.14]))]


# ======================================================================= the sheets
SHEETS = {
    # name: (frame w, frame h, pivot, clips function)
    "caillou": (48, 48, [24, 24], pebble_clips),
    "grain": (16, 16, [8, 8], grain_clips),
    "rocher": (112, 96, [56, 90], rock_clips),
    "hutte": (96, 80, [48, 77], hut_clips),
    "feu": (48, 40, [24, 37], fire_clips),
    "torche": (24, 40, [12, 33], torch_clips),
    "vaisseau": (64, 56, [32, 44], ship_clips),
    "nuage": (96, 48, [48, 44], puff_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
for _n in FOLK:
    SHEETS[_n] = (F.FW, F.FH, F.PIVOT, folk_sheet(_n))
