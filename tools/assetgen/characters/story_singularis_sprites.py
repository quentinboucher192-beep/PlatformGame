"""The sprites of Singularis's story (« Le Cœur effondré », 2.63): every actor of its six animated
shots, drawn frame by frame like the game's sprites (sheets under assets/stories/singularis/, clips
in assets/animations/stories/singularis_<name>.json; data/stories/singularis.json places them).

  lyse      Lyse, the young star: shining, singing light, dimming, the last spark, collapsing   96x96
  foret     the planet of forests: turning, blooming                                           48x48
  volcan    the planet of volcanoes: turning, erupting                                         48x48
  glace     the planet of ice: turning, glittering                                             48x48
  don       a gift of light travelling: sap, fire, ice, light                                  16x16
  machine   a machine come to drink her light: flying, drinking                                64x48
  grain     a grain of rock drifting                                                           16x16
  astre     a twinkling star                                                                   16x16

The collapsed heart is the game's own sheet (animations/singularis.json).  One sheet pixel is two
units of the story's stage.  Deterministic."""
import math

import numpy as np

from boss_a_common import mat
from raster import Canvas
from story_monolithe_sprites import C, grain_clips, putpx, twinkle_clips  # noqa: F401

PI = math.pi
TAU = 2 * PI

GOLD = [C("#ffffff"), C("#fff6d8"), C("#ffe9a8"), C("#ffd27a"), C("#f0a050"), C("#b8603a")]
PALE = [C("#f0f0ff"), C("#d8d8f0"), C("#b8b8d8"), C("#9090b8"), C("#6a6a98"), C("#4a4a78")]
VIOLET = [C("#ffe8ff"), C("#f0a8ff"), C("#c88bff"), C("#8a3cc8"), C("#4c1c7a"), C("#240c40")]
FACE = C("#5a2a1a")
BLUSH = C("#ff9a7a")


# ======================================================================= Lyse
def _lyse(f, cx, cy, r, k, n, cols=GOLD, rays=10, ray_len=10.0, eyes="open", mouth=0.0, rings=(), tear=False, brows=0.0):
    a = TAU * k / n
    for y in range(f.shape[0]):  # halo, core
        for x in range(f.shape[1]):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d <= r:
                lit = (dx * -0.5 + dy * -0.6) / max(r, 1)
                ci = min(5, max(0, int(d / r * 2.6 - lit * 1.2 + (0.3 if (x + y) % 2 else 0))))
                f[y, x] = cols[ci]
            elif d <= r + 3 + math.sin(math.atan2(dy, dx) * 6 + a * 2) and (x + y + k) % 2 == 0:
                f[y, x] = cols[3]
    for j in range(rays):  # the corona, turning
        ang = TAU * j / rays + a / rays * 2
        ln = ray_len * (1.0 if j % 2 == 0 else 0.6) * (0.85 + 0.15 * math.sin(a + j))
        for s in np.arange(r + 2, r + 2 + ln, 0.5):
            t = (s - r) / max(ln, 1e-3)
            putpx(f, cx + math.cos(ang) * s, cy + math.sin(ang) * s, cols[min(5, 2 + int(t * 3))], True)
    for (rr, al) in rings:  # rings of light she sings
        for q in range(int(TAU * rr * 2)):
            ang = TAU * q / int(TAU * rr * 2)
            if (q + k) % 3:
                putpx(f, cx + math.cos(ang) * rr, cy + math.sin(ang) * rr, C("#fff2c0", int(255 * al)), True)
    if r < 7:  # too small for a face: two dots
        if eyes != "closed":
            putpx(f, cx - 1.5, cy - 0.5, FACE)
            putpx(f, cx + 1.5, cy - 0.5, FACE)
        else:
            putpx(f, cx - 2, cy, FACE)
            putpx(f, cx + 2, cy, FACE)
        if tear:
            putpx(f, cx + 2, cy + 1 + (k % 3), C("#9ad8ff"))
        return f
    ey = cy - r * 0.15
    for sx in (-1, 1):
        ex = cx + sx * r * 0.32
        if eyes == "open":
            for dy in range(-1, 2):
                putpx(f, ex, ey + dy, FACE)
                putpx(f, ex + 1, ey + dy, FACE)
            putpx(f, ex + 1, ey - 1, C("#ffffff"))
        else:
            for dx in range(-1, 3):
                putpx(f, ex + dx, ey + (1 if abs(dx - 0.5) > 1 else 0), FACE)
        if brows:
            for dx in range(-1, 3):
                putpx(f, ex + dx, ey - 3 - sx * (dx - 0.5) * 0.5 * brows, FACE)
        putpx(f, ex + sx * 2 - 0.5, ey + 3, BLUSH)
    my = cy + r * 0.3
    if mouth > 0.5:  # singing: a round mouth
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                if abs(dx) + abs(dy) < 2:
                    putpx(f, cx + dx, my + dy, FACE)
    else:
        for dx in range(-2, 3):
            putpx(f, cx + dx, my + (0 if abs(dx) < 2 else -1) * (1 if brows <= 0 else -1), FACE)
    return f


def lyse_clips():
    S, c = 96, 48
    clips = []
    n = 8
    clips.append(("idle", [_lyse(np.zeros((S, S, 4), np.uint8), c, c, 15, i, n, eyes="closed" if i == 6 else "open") for i in range(n)],
                  dict(fps=7)))
    fr = []
    for i in range(n):
        u = i / n
        rings = [(18 + 26 * u, 1 - u), (18 + 26 * ((u + 0.5) % 1), 1 - (u + 0.5) % 1)]
        fr.append(_lyse(np.zeros((S, S, 4), np.uint8), c, c, 15, i, n, mouth=1.0 if i % 4 < 3 else 0.0, rings=rings,
                        eyes="closed" if i % 4 == 1 else "open"))
    clips.append(("sing", fr, dict(fps=8)))
    dim = [GOLD[2], GOLD[3], GOLD[4], C("#c88a5a"), C("#8a5a4a"), C("#5a3a3a")]
    clips.append(("dim", [_lyse(np.zeros((S, S, 4), np.uint8), c, c, 11 - 0.4 * (i % 2), i, n, cols=dim, rays=8, ray_len=6.0,
                                brows=1.0, eyes="closed" if i == 5 else "open") for i in range(n)], dict(fps=6)))
    clips.append(("spark", [_lyse(np.zeros((S, S, 4), np.uint8), c + [0, 1, 0, -1, 0, 1][i], c + [0, 0, 1, 0, 0, -1][i], 4.5, i, 6,
                                  cols=PALE, rays=4, ray_len=3.0, eyes="closed" if i < 3 else "open", tear=True) for i in range(6)],
                  dict(fps=10)))
    fr = []
    for i in range(10):  # implode: the spark swells, then falls into itself: a dark heart, a violet ring
        f = np.zeros((S, S, 4), np.uint8)
        if i < 3:
            _lyse(f, c, c, 5 + 2 * i, i, 10, cols=PALE if i < 2 else GOLD, rays=6, ray_len=4 + 3 * i, eyes="closed")
        else:
            k = (i - 3) / 6
            for q in range(int(TAU * 40)):
                ang = TAU * q / int(TAU * 40)
                rr = 8 + 30 * (1 - k) * (0.5 + 0.5 * math.sin(ang * 3 + i))
                putpx(f, c + math.cos(ang) * rr, c + math.sin(ang) * rr * 0.9, VIOLET[min(5, 1 + int(k * 3))], True)
            for y in range(S):
                for x in range(S):
                    d = math.hypot(x + 0.5 - c, y + 0.5 - c)
                    if d <= 3 + 6 * k:
                        f[y, x] = C("#05020a") if d < 2 + 6 * k else VIOLET[2]
        fr.append(f)
    clips.append(("implode", fr, dict(durations=[0.12, 0.1, 0.1, 0.08, 0.08, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)))
    return clips


# ======================================================================= the planets
PLANETS = {
    "foret": (["#dcffb0", "#8ad85a", "#4a9a3a", "#2a6a3a", "#1a4030"], ["#9ad8ff", "#3a8ab8", "#1a4a7a"], "#c8ff9a"),
    "volcan": (["#ffe0a0", "#ff8a3a", "#c84a2a", "#7a2a22", "#3a1418"], ["#5a3a3a", "#3a2424", "#241416"], "#ffb070"),
    "glace": (["#ffffff", "#d8f4ff", "#9ad8f0", "#5aa8d0", "#2a6a9a"], ["#e8f8ff", "#a8d8f0", "#6aa8d0"], "#e8ffff"),
}


def _planet(kind, k, n, bloom=0.0):
    land, sea, spark = PLANETS[kind]
    R = 15
    f = np.zeros((48, 48, 4), np.uint8)
    rot = TAU * k / n
    L_ = [C(c) for c in land]
    W_ = [C(c) for c in sea]
    for y in range(48):
        for x in range(48):
            dx, dy = (x + 0.5 - 24) / R, (y + 0.5 - 24) / R
            r2 = dx * dx + dy * dy
            if r2 > 1:
                if r2 < 1.18 and (x + y) % 2 == 0:  # atmosphere
                    f[y, x] = C(spark, 150)
                continue
            z = math.sqrt(1 - r2)
            lon = math.atan2(dx, z) + rot
            lat = dy
            v = math.sin(lon * 3 + math.sin(lat * 4) * 1.5) + 0.6 * math.sin(lon * 5 - lat * 3 + 1.0)
            light = max(0.0, -dx * 0.75 - dy * 0.35 + z * 0.55)
            shade_i = min(4, max(0, int((1 - light) * 3.6 + (0.4 if (x + y) % 2 else 0))))
            if kind == "glace" and abs(lat) > 0.6:
                f[y, x] = L_[min(4, shade_i // 2)]
            elif v > 0.2:
                f[y, x] = L_[min(4, shade_i + (0 if bloom <= 0 else -1 if shade_i > 0 else 0))]
            else:
                f[y, x] = W_[min(2, shade_i // 2)]
            if kind == "volcan" and v > 0.9 and (bloom > 0 or (x * 7 + y * 3 + k) % 9 == 0):
                f[y, x] = C("#ffd060") if light > 0.2 else C("#ff7a3a")
    if bloom > 0:  # it blooms: sparkles over it
        rng = np.random.default_rng(700 + k)
        for _ in range(int(6 * bloom)):
            a, rr = rng.uniform(0, TAU), rng.uniform(0, R * 1.2)
            putpx(f, 24 + math.cos(a) * rr, 24 + math.sin(a) * rr, C("#ffffff") if rng.random() < 0.5 else C(spark))
    return f


def planet_clips(kind):
    return lambda: [("spin", [_planet(kind, i, 8) for i in range(8)], dict(fps=6)),
                    ("bloom", [_planet(kind, i, 8, bloom=1.0) for i in range(8)], dict(fps=8))]


# ======================================================================= gifts, machines
GIFT = {"seve": ["#f0ffe0", "#a8f070", "#4aa83a"], "feu": ["#fff2c0", "#ffa040", "#d04a1a"],
        "glace": ["#ffffff", "#a8e8ff", "#4a9ad0"], "lumiere": ["#ffffff", "#ffe9a8", "#ffb347"]}


def gift_clips():
    clips = []
    for name, cols in GIFT.items():
        fr = []
        for i in range(6):
            f = np.zeros((16, 16, 4), np.uint8)
            r = 3.0 + 0.6 * math.sin(TAU * i / 6)
            for y in range(16):
                for x in range(16):
                    d = math.hypot(x + 0.5 - 8, y + 0.5 - 8)
                    if d <= r:
                        f[y, x] = C(cols[0] if d < r * 0.5 else cols[1])
                    elif d <= r + 2 and (x + y + i) % 2 == 0:
                        f[y, x] = C(cols[2], 200)
            fr.append(f)
        clips.append((name, fr, dict(fps=10)))
    return clips


HULL = mat("ma_hull", ["#c8ccd8", "#9098ac", "#646c84", "#40465a", "#262a38"], th=[0.86, 0.66, 0.44, 0.24])
PIPE = mat("ma_pipe", ["#7a7488", "#544e62", "#36324a"], th=[0.66, 0.36])
EYE = [mat(f"ma_e{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#e8fbff", "#7fe8ff", "#2a8ab0"])]


def _machine(i, drink):
    cv = Canvas(64, 48)
    cv.part()  # its body: a hexagonal drum, fins
    cv.poly([(34, 12), (50, 12), (58, 24), (50, 36), (34, 36), (28, 24)], HULL, bevel=1.6)
    cv.part()
    cv.poly([(44, 36), (52, 36), (56, 46), (46, 44)], PIPE, bevel=1.0)
    cv.part()
    cv.poly([(44, 12), (52, 12), (56, 2), (46, 4)], PIPE, bevel=1.0)
    cv.part(line=False)
    cv.disc((40, 24), 4.0, EYE[1])
    cv.disc((39, 23), 1.6, EYE[0], decal=True)
    if drink:  # its siphon, out to the left
        cv.part()
        cv.capsule((2, 24), (29, 24), 2.4, 3.0, PIPE)
        cv.part()
        cv.ellipse((3, 24), 2.4, 4.6, HULL)
    f = cv.render()
    if drink:  # gulps of light going up the siphon
        for k in range(3):
            x = 4 + ((i * 4 + k * 9) % 26)
            for dy in (-1, 0, 1):
                putpx(f, x, 24 + dy, C("#ffe9a8" if dy == 0 else "#ffb347"))
    else:  # thrusters
        for k in range(1 + i % 2):
            putpx(f, 60 + k, 22 + (i + k) % 4, C("#7fe8ff"))
    putpx(f, 50, 16, C("#ff5a5a") if i % 3 == 0 else C("#5a2a2a"))
    return f


def machine_clips():
    return [("fly", [_machine(i, False) for i in range(6)], dict(fps=10)), ("drink", [_machine(i, True) for i in range(6)], dict(fps=10))]


SHEETS = {
    "lyse": (96, 96, [48, 48], lyse_clips),
    "foret": (48, 48, [24, 24], planet_clips("foret")),
    "volcan": (48, 48, [24, 24], planet_clips("volcan")),
    "glace": (48, 48, [24, 24], planet_clips("glace")),
    "don": (16, 16, [8, 8], gift_clips),
    "machine": (64, 48, [40, 24], machine_clips),
    "grain": (16, 16, [8, 8], grain_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
