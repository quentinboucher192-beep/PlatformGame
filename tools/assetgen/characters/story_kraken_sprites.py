"""The sprites of the Kraken's story (« La Lanterne des profondeurs », 2.63): every actor of its six
animated shots, drawn frame by frame like the game's sprites (sheets under assets/stories/kraken/,
clips in assets/animations/stories/kraken_<name>.json; data/stories/kraken.json places them).

  poulpe    the little octopus, one light at the tip of an arm: swims, hovers, lights up, catches   64x64
  jeune     the Lanterne grown, its eight arms lighting one after the other                         128x128
  oeuf      its egg on the rocks: breathing, hatching, torn                                         48x48
  poisson   a lost fish: swims, flees                                                               24x16
  meduse    a jellyfish drifting, pulsing                                                           32x40
  source    a hot spring: its chimney glowing, its bubbles                                          64x64
  bulles    a column of bubbles                                                                     24x80
  etincelle a spark of the dead star, falling into the sea                                          24x32
  etoile    the star of the centre: shining, collapsing into a black hole                           96x96
  astre     a twinkling star                                                                        16x16

The grown Kraken is the game's own sheet (animations/kraken.json).  The octopus has the boss's
skin (deep-sea purple, teal in the light) and its cyan light.  One sheet pixel is two units of the
story's stage.  Deterministic."""
import math

import numpy as np

from boss_a_common import mat
from raster import Canvas
from story_monolithe_sprites import C, putpx, twinkle_clips  # noqa: F401
from story_pyrrhax_sprites import star_clips  # noqa: F401  (the same star of the centre)

PI = math.pi
TAU = 2 * PI

SKIN = mat("kr_skin", ["#8fc0c4", "#5f8aa8", "#4f5e96", "#43407e", "#33285f"], th=[0.86, 0.66, 0.46, 0.27])
SKIN_FAR = mat("kr_skin_far", ["#4f6d8c", "#41447a", "#332b62", "#241d48"], th=[0.78, 0.54, 0.3])
BELLY = mat("kr_belly", ["#c8bedb", "#9d93c2", "#7a6fa5", "#5b4f85"], th=[0.8, 0.56, 0.32])
EYE_W = mat("kr_eye", ["#f2f0f8", "#c8c0dc"], th=[0.5])
PUPIL = mat("kr_pupil", ["#140a1a"], outline=None)
LIGHT = ["#dcfaff", "#7ae6ff", "#22b8ee", "#1670a8", "#0c3866"]
LM = [mat(f"kr_l{i}", [c], emissive=True, outline=None) for i, c in enumerate(LIGHT)]
DEADL = mat("kr_dead", ["#6a6584", "#4c4864"], outline=None, th=[0.5])


# ======================================================================= the octopus rig
def _arm_points(base, ang, length, curl, wave, ph, n=9):
    pts = [base]
    x, y = base
    a = ang
    seg = length / n
    for j in range(1, n + 1):
        u = j / n
        a = ang + curl * u ** 1.4 + wave * math.sin(ph + u * 4.0)
        x += math.cos(a) * seg
        y += math.sin(a) * seg
        pts.append((x, y))
    return pts


def octo(W, H, cx, cy, s, t=0.0, pulse=0.0, arm_dir=PI * 0.5, spread=1.0, curl=0.9, wave=0.25, lit=(), lit_lvl=1,
         reach=None, eyes=1.0, arm_len=20.0, want_tips=False):
    """An octopus facing right, centre (cx, cy), size s (1: the little one): mantle leaning back,
    two eyes, eight arms (four far, four near) along curves, lights at the tips of the arms in `lit`
    (lit_lvl 0 bright .. 3 dim); `reach`: the first arm reaching to that point."""
    cv = Canvas(W, H)
    crown = (cx + 1.5 * s, cy + 5.0 * s)
    arms = []
    for i in range(8):
        far = i % 2 == 1
        ang = arm_dir + spread * (i - 3.5) * 0.14
        ln = arm_len * s * (0.85 if far else 1.0) * (1.0 + 0.06 * math.sin(i * 2.1))
        b = (crown[0] + (i - 3.5) * 0.9 * s, crown[1] + (0.6 if far else 0.0) * s)
        if i == 0 and reach is not None:
            dx, dy = reach[0] - b[0], reach[1] - b[1]
            pts = _arm_points(b, math.atan2(dy, dx), min(math.hypot(dx, dy), ln * 1.3), 0.0, 0.08, t, n=9)
        else:
            pts = _arm_points(b, ang, ln, curl * (1 if i % 3 else -0.6), wave, t + i * 0.8)
        arms.append((i, far, pts))
    tips = {}
    for i, far, pts in arms:  # the far arms, behind
        if far:
            cv.part()
            cv.chain(pts, [s * 2.0 * (1 - 0.8 * k / (len(pts) - 1)) + 0.4 for k in range(len(pts))], SKIN_FAR)
            tips[i] = pts[-1]
    # the mantle, leaning back, breathing
    cv.part()
    mx, my = cx - 4.0 * s, cy - 6.5 * s
    cv.ellipse((mx, my), 8.5 * s * (1 + 0.12 * pulse), 11.0 * s * (1 - 0.08 * pulse), SKIN, ang=-0.55)
    cv.part(line=False)
    for k in range(4):  # bands of light across it
        q = (mx - 2 * s + k * 2.2 * s, my - 5 * s + k * 3.0 * s)
        cv.px_set(q[0], q[1], LM[1 if k % 2 else 2], 0.6, decal=True)
    cv.part()
    cv.ellipse((cx + 2.0 * s, cy + 1.5 * s), 7.0 * s, 6.0 * s, SKIN)
    cv.part(line=False)
    cv.ellipse((cx + 3.0 * s, cy + 3.5 * s), 5.0 * s, 2.6 * s, BELLY, decal=True)
    for i, far, pts in arms:  # the near arms, in front, suckers and photophores
        if not far:
            cv.part()
            cv.chain(pts, [s * 2.3 * (1 - 0.8 * k / (len(pts) - 1)) + 0.5 for k in range(len(pts))], SKIN)
            cv.part(line=False)
            for k in range(2, len(pts) - 1, 2):
                cv.px_set(pts[k][0], pts[k][1] + s * 0.8, BELLY, 0.7, decal=True)
            tips[i] = pts[-1]
    # eyes (the far one peeking over the brow)
    cv.part(line=False)
    cv.disc((cx + 1.2 * s, cy - 0.6 * s), 1.6 * s, EYE_W, decal=True)
    cv.part()
    cv.disc((cx + 5.0 * s, cy + 0.2 * s), 2.3 * s, EYE_W)
    cv.part(line=False)
    if eyes > 0.5:
        cv.rect(int(cx + 5.4 * s - 0.5), int(cy - 0.8 * s), int(cx + 5.4 * s + 1.5), int(cy + 1.6 * s), PUPIL, flat=0.5)
    else:
        cv.line((cx + 3.2 * s, cy + 0.4 * s), (cx + 7.0 * s, cy + 0.4 * s), PUPIL)
    f = cv.render()
    for i in lit:
        if i in tips:
            x, y = tips[i]
            r = 1.2 * s + (0.6 if lit_lvl == 0 else 0)
            for yy in range(int(y - r - 2), int(y + r + 3)):
                for xx in range(int(x - r - 2), int(x + r + 3)):
                    d = math.hypot(xx + 0.5 - x, yy + 0.5 - y)
                    if d <= r:
                        putpx(f, xx, yy, C(LIGHT[min(4, lit_lvl + (1 if d > r * 0.6 else 0))]))
                    elif d <= r + 1.6 and (xx + yy) % 2 == 0 and lit_lvl < 3:
                        putpx(f, xx, yy, C(LIGHT[2], 170), True)
    return (f, tips) if want_tips else f


def _light_lvl(i, n):
    return [0, 1, 0, 1, 1, 0, 1, 1][i % 8] if n else 3


def poulpe_clips():
    S = 1.0
    clips = []
    n = 8
    # swim: the mantle squeezes (a jet), the arms trail behind and wave
    clips.append(("swim", [octo(64, 64, 34, 30, S, t=TAU * i / n, pulse=max(0.0, math.sin(TAU * i / n)), arm_dir=PI * 0.86,
                                spread=0.6, curl=0.2, wave=0.35, lit=(0,), lit_lvl=[0, 1][i % 2]) for i in range(n)], dict(fps=10)))
    clips.append(("swim_dark", [octo(64, 64, 34, 30, S, t=TAU * i / n, pulse=max(0.0, math.sin(TAU * i / n)), arm_dir=PI * 0.86,
                                     spread=0.6, curl=0.2, wave=0.35) for i in range(n)], dict(fps=10)))
    # idle: hovering, arms curling under it
    clips.append(("idle", [octo(64, 64, 32, 26, S, t=TAU * i / n, pulse=0.3 + 0.3 * math.sin(TAU * i / n), arm_dir=PI * 0.5,
                                spread=1.4, curl=1.1, wave=0.3, lit=(0,), lit_lvl=[0, 0, 1, 1, 0, 0, 1, 2][i]) for i in range(n)],
                  dict(fps=7)))
    clips.append(("dark", [octo(64, 64, 32, 26, S, t=TAU * i / n, pulse=0.3 + 0.3 * math.sin(TAU * i / n), arm_dir=PI * 0.5,
                                spread=1.4, curl=1.1, wave=0.3, eyes=0.0 if i == 6 else 1.0) for i in range(n)], dict(fps=7)))
    # light: the tip of its first arm flickers and lights up for good
    fr = []
    for i, lv in enumerate([3, 2, 3, 1, 2, 0, 1, 0]):
        fr.append(octo(64, 64, 32, 26, S, t=0.4 * i, pulse=0.3, arm_dir=PI * 0.5, spread=1.4, curl=1.1, wave=0.3,
                       lit=(0,) if i % 3 != 2 else (), lit_lvl=lv))
    clips.append(("light", fr, dict(durations=[0.12, 0.08, 0.14, 0.08, 0.1, 0.12, 0.1, 0.2], loop=False, next="idle")))
    # catch: its lit arm reaches up and brings a spark back
    fr = []
    reach = [(40, 14), (44, 6), (46, 2), (44, 6), (38, 16), (34, 24)]
    for i, r in enumerate(reach):
        fr.append(octo(64, 64, 32, 28, S, t=0.5 * i, pulse=0.5 if i == 5 else 0.2, arm_dir=PI * 0.5, spread=1.4, curl=1.1,
                       wave=0.25, lit=(0,), lit_lvl=0, reach=r))
    clips.append(("catch", fr, dict(durations=[0.08, 0.08, 0.12, 0.1, 0.1, 0.16], loop=False, next="idle")))
    return clips


def jeune_tips():
    """Where the young one's eight arms end (frame pixels) once they are all lit."""
    return octo(128, 128, 66, 54, 2.2, t=0.0, pulse=0.4, arm_dir=PI * 0.5, spread=1.5, curl=1.0, wave=0.25, want_tips=True)[1]


def jeune_clips():
    S = 2.2
    n = 8
    order = [0, 2, 4, 6, 7, 5, 3, 1]
    fr = []
    for i in range(10):  # its eight arms light up one after the other
        on = tuple(order[:max(0, min(8, i))])
        fr.append(octo(128, 128, 66, 54, S, t=0.3 * i, pulse=0.4, arm_dir=PI * 0.5, spread=1.5, curl=1.0, wave=0.22,
                       lit=(0,) + on, lit_lvl=0 if i % 2 else 1))
    clips = [("light_arms", fr, dict(durations=[0.16] * 10, loop=False, next="lit"))]
    clips.append(("lit", [octo(128, 128, 66, 54, S, t=TAU * i / n, pulse=0.3 + 0.3 * math.sin(TAU * i / n), arm_dir=PI * 0.5,
                               spread=1.5, curl=1.0, wave=0.25, lit=tuple(range(8)), lit_lvl=[0, 1][i % 2]) for i in range(n)],
                  dict(fps=7)))
    return clips


# ======================================================================= the egg
SAC = mat("eg_sac", ["#dfe8ff", "#a8b8e8", "#7a86c0", "#56609a"], th=[0.8, 0.55, 0.3])


def _egg(i, state):
    cv = Canvas(48, 48)
    rng = np.random.default_rng(300)
    cv.part()
    for _ in range(3):  # the rock it clings to
        x = rng.uniform(10, 38)
        cv.ellipse((x, 45), rng.uniform(6, 10), 4.0, SKIN_FAR)
    if state != "empty":
        wob = 0.6 * math.sin(TAU * i / 6)
        cv.part()
        msk = cv.ellipse((24, 28), 11 + wob, 13 - wob, SAC)
        if state == "hatch":
            k = i / 5
            cv.erase(msk & (cv.py < 16 + 14 * k + 2 * np.sin(cv.px * 1.3)))
        f = cv.render()
        # inside: the curled baby, translucent
        if state != "hatch" or i < 3:
            for y in range(20, 38):
                for x in range(16, 33):
                    d = math.hypot((x - 24) / 6.5, (y - 29) / 7.0)
                    if d < 1 and (x + y + i) % 2 == 0 and f[y, x, 3]:
                        putpx(f, x, y, C("#5f5aa0"))
            if (state == "idle" and i % 3 == 0) or state == "hatch":
                putpx(f, 28, 33, C(LIGHT[1]))
        return f
    cv.part()  # torn: the lower half swaying
    sw = [0, 1, 1, 0, -1, -1][i % 6]
    pts = [(14, 40), (34, 40), (35 + sw, 30), (30 + sw, 24), (27, 31), (22 + sw, 25), (18, 32), (13 + sw, 28)]
    cv.poly(pts, SAC, bevel=1.0)
    return cv.render()


def egg_clips():
    return [("idle", [_egg(i, "idle") for i in range(6)], dict(fps=5)),
            ("hatch", [_egg(i, "hatch") for i in range(6)], dict(durations=[0.08, 0.08, 0.1, 0.1, 0.12, 0.14], loop=False,
                                                                     next="empty")),
            ("empty", [_egg(i, "empty") for i in range(6)], dict(fps=5))]


# ======================================================================= fish, jellyfish
FISH = mat("fi_body", ["#e8f4ff", "#a8c8e8", "#7090c0", "#4a5a90"], th=[0.82, 0.56, 0.3])
FIN = mat("fi_fin", ["#c8e0ff", "#8aa8d8", "#5a70a8"], th=[0.7, 0.4])


def _fish(i, n, fast=False):
    cv = Canvas(24, 16)
    a = TAU * i / n
    wag = math.sin(a) * (3.0 if fast else 1.8)
    cv.part()
    cv.poly([(7, 8), (1, 3 + wag * 0.5), (2, 8), (1, 13 + wag * 0.5)], FIN, bevel=0.8)
    cv.part()
    cv.ellipse((13, 8 + 0.3 * math.sin(a + 1)), 7.5, 4.0, FISH, ang=0.05 * wag)
    cv.part(line=False)
    cv.poly([(11, 5), (15, 4), (13, 2)], FIN, flat=0.5)
    cv.px_set(18, 7, PUPIL, 0.5)
    f = cv.render()
    putpx(f, 16, 9, C("#ffffff"), False)
    return f


def fish_clips():
    return [("swim", [_fish(i, 6) for i in range(6)], dict(fps=9)), ("flee", [_fish(i, 4, True) for i in range(4)], dict(fps=16))]


JELLY = [mat(f"je_{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#ffe6ff", "#e0a8f0", "#a870d0", "#6a4a9a"])]


def jelly_clips():
    fr = []
    for i in range(8):
        a = TAU * i / 8
        sq = math.sin(a)
        f = np.zeros((40, 32, 4), np.uint8)
        rx, ry = 9 + 1.6 * sq, 7 - 1.2 * sq
        for y in range(40):
            for x in range(32):
                dx, dy = (x + 0.5 - 16) / rx, (y + 0.5 - 12) / ry
                d = dx * dx + dy * dy
                if d <= 1 and y + 0.5 <= 12 + 1:
                    lvl = 0 if d < 0.25 else 1 if d < 0.6 else 2
                    if (x + y) % 2 and lvl == 2:
                        lvl = 3
                    f[y, x] = C(["#ffe6ff", "#e0a8f0", "#a870d0", "#6a4a9a"][lvl], 230)
        for k in range(5):  # tentacles waving
            x0 = 16 + (k - 2) * 3.2
            for j in range(18):
                x = x0 + math.sin(a + j * 0.4 + k) * (1 + j * 0.12)
                putpx(f, x, 13 + j, C("#c890e0", 200 - j * 8), True)
        fr.append(f)
    return [("idle", fr, dict(fps=7))]


# ======================================================================= hot springs, bubbles, sparks
VENT = mat("ve_rock", ["#6a5a6a", "#4a3a50", "#33263a", "#201628"], th=[0.8, 0.56, 0.3])
HEAT = [C("#fff2c0"), C("#ffc860"), C("#ff8a3a"), C("#c04a2a")]
BUB = [C("#e8fbff"), C("#a8e0f8"), C("#5aa8d8")]


def _bubble(f, x, y, r):
    for yy in range(int(y - r - 1), int(y + r + 2)):
        for xx in range(int(x - r - 1), int(x + r + 2)):
            d = math.hypot(xx + 0.5 - x, yy + 0.5 - y)
            if r - 0.9 <= d <= r + 0.1:
                putpx(f, xx, yy, BUB[1 if d > r - 0.4 else 2], True)
    putpx(f, x - r * 0.4, y - r * 0.4, BUB[0], False)


def vent_clips():
    fr = []
    for i in range(8):
        cv = Canvas(64, 64)
        cv.part()
        cv.poly([(16, 63), (24, 34), (28, 30), (36, 30), (40, 36), (48, 63)], VENT, bevel=1.6)
        cv.part()
        cv.poly([(4, 63), (10, 50), (18, 52), (22, 63)], VENT, bevel=1.2)
        f = cv.render()
        for x in range(27, 38):  # the glowing mouth
            lv = int(abs(x - 32) / 2.0 + (i + x) % 2 * 0.6)
            putpx(f, x, 30, HEAT[min(3, lv)])
            putpx(f, x, 31, HEAT[min(3, lv + 1)])
        for k in range(5):  # bubbles rising from it
            u = ((i / 8) + k / 5) % 1.0
            _bubble(f, 32 + 3 * math.sin(u * 6 + k), 28 - u * 26, 1.0 + u * 1.8)
        for k in range(6):  # shimmering heat
            y = 26 - k * 4 - (i % 2)
            putpx(f, 30 + 2 * math.sin(i + k), y, C("#ffb070", 120), True)
        fr.append(f)
    return [("idle", fr, dict(fps=8))]


def bubbles_clips():
    fr = []
    rng = np.random.default_rng(320)
    seeds = [(rng.uniform(6, 18), rng.uniform(0, 1), rng.uniform(1.0, 2.6)) for _ in range(7)]
    for i in range(8):
        f = np.zeros((80, 24, 4), np.uint8)
        for (x0, ph, r) in seeds:
            u = ((i / 8) + ph) % 1.0
            _bubble(f, x0 + 2.5 * math.sin(u * 9 + ph * 6), 76 - u * 72, r)
        fr.append(f)
    return [("idle", fr, dict(fps=8))]


SPARK = [C("#ffffff"), C("#f0e0ff"), C("#c8b8ff"), C("#9a7ae0"), C("#6a4ab0")]


def spark_clips():
    fr = []
    for i in range(6):
        f = np.zeros((32, 24, 4), np.uint8)
        cx, cy = 12, 24
        r = 2.4 + 0.5 * math.sin(TAU * i / 6)
        for y in range(32):
            for x in range(24):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                if d <= r:
                    f[y, x] = SPARK[0 if d < r * 0.5 else 1]
                elif d <= r + 2 and (x + y + i) % 2 == 0:
                    f[y, x] = SPARK[3]
        for j in range(14):  # its trail, above it (it falls)
            y = cy - 3 - j
            x = cx + math.sin(j * 0.5 + i) * 0.6
            putpx(f, x, y, SPARK[min(4, 1 + j // 4)], True)
        fr.append(f)
    return [("fall", fr, dict(fps=10))]


SHEETS = {
    "poulpe": (64, 64, [32, 32], poulpe_clips),
    "jeune": (128, 128, [64, 96], jeune_clips),
    "oeuf": (48, 48, [24, 46], egg_clips),
    "poisson": (24, 16, [12, 8], fish_clips),
    "meduse": (32, 40, [16, 12], jelly_clips),
    "source": (64, 64, [32, 63], vent_clips),
    "bulles": (24, 80, [12, 79], bubbles_clips),
    "etincelle": (24, 32, [12, 24], spark_clips),
    "etoile": (96, 96, [48, 48], star_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
