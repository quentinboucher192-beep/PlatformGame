"""Effects of Singularis, le Coeur Effondre (giga boss of the Noyau stellaire): dedicated sheets,
each with its JSON.

  sprites/objects/plasma_flood.png   128x32 liquid format (as lava_flood.png): row 0 = 8 surface
      frames 16x16 (crest at the top), row 1 = 8 body frames 16x16; clips `surface`, `body`. The
      tide of star plasma (Maree de plasma) and the plasma of the arena (basin, curtains).
  sprites/projectiles/star_wall.png  32x96  pivot [16, 96]  rise 5, idle 4 (loop), wither 5: a pillar
      of starlight, the walls of the Cage stellaire.
  sprites/objects/crumble_platform_star.png  48x48: the crust floor of the arena (style "star" of the
      crumble platforms: three rows intact / shaking / about to give way, 16 px cells left, middle,
      right), dead stellar crust with glowing plasma cracks.
Deterministic."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, mat, motes
from raster import Canvas, TCanvas
from sheet import Sheet, save_json, save_png

PI = math.pi
TAU = 2 * PI

# Star plasma: the magenta of the Effondrement (phase 5), white-hot crest.
PF_CREST = C("#fff0fa")
PF_FOAM = C("#ffb4e6")
PF_BODY = [C("#ff7ad2"), C("#e0329e"), C("#b01e82"), C("#6e1258")]
PF_CRUST = [C("#3b3450"), C("#241e33")]

STREAKS = [(1, 2, 5, 1, 0), (9, 5, 4, -1, 2), (4, 8, 6, 1, 0), (12, 11, 3, 1, 2), (0, 13, 5, -1, 0), (7, 15, 3, 1, 2)]


def _plasma_body(x, y, t):
    c = PF_BODY[1]
    for (x0, sy, ln, d, tone) in STREAKS:
        if y != sy:
            continue
        if (x - x0 - 2 * t * d) % 16 < ln:
            c = PF_BODY[tone]
    if (x * 7 + y * 3) % 23 == 0:
        c = PF_BODY[3]
    if (x * 5 + y * 11 + t * 3) % 41 == 0:
        c = PF_CREST  # sparks of starlight in the depth
    return c


def plasma_flood():
    img = np.zeros((32, 128, 4), np.uint8)
    for f in range(8):
        ox = f * 16
        for x in range(16):
            crest = 3.0 + 1.6 * math.sin(TAU * x / 16 - TAU * f / 8) + 0.6 * math.sin(TAU * 2 * x / 16 + TAU * f / 4)
            cy = int(round(crest))
            for y in range(16):
                if y < cy - 3:
                    continue
                if y < cy:
                    if (x + 2 * f + y) % 4 == 0:  # corona flickering above the surface
                        img[y, ox + x] = (*PF_FOAM[:3], 140)
                    continue
                if y == cy:
                    c = PF_CREST
                elif y == cy + 1:
                    c = PF_FOAM
                elif y == cy + 2:
                    c = PF_BODY[0]
                else:
                    c = _plasma_body(x, y, f)
                img[y, ox + x] = c
            for y in range(16):
                img[16 + y, ox + x] = _plasma_body(x, y, f)
        # floes of dead crust riding the crest
        x0 = (5 + 2 * f) % 16
        for dx in range(3):
            x = (x0 + dx) % 16
            crest = 3.0 + 1.6 * math.sin(TAU * x / 16 - TAU * f / 8) + 0.6 * math.sin(TAU * 2 * x / 16 + TAU * f / 4)
            img[int(round(crest)) + 1, ox + x] = PF_CRUST[dx % 2]
        for (bx, by) in ((3, 10), (10, 4), (13, 13)):
            y = (by - 2 * f) % 16
            img[16 + y, ox + bx] = PF_CREST
            if y >= 7:
                img[y, ox + bx] = PF_CREST
    save_png(img, "sprites/objects/plasma_flood.png")
    save_json({"texture": "sprites/objects/plasma_flood.png", "frameWidth": 16, "frameHeight": 16, "pivot": [0, 0],
               "clips": {"surface": {"row": 0, "count": 8, "fps": 8, "loop": True},
                         "body": {"row": 1, "count": 8, "fps": 8, "loop": True}}},
              "animations/plasma_flood.json")


# ======================================================================= star_wall (32x96)
TW, TH = 32, 96
TG = 95
GLOW = mat("sw_glow", ["#ffffff", "#e6fff2", "#b0ffd8", "#5ef0b0", "#1fc47e", "#127056"], emissive=True, outline=None,
           th=[0.9, 0.76, 0.6, 0.42, 0.24])
CRUST = mat("sw_crust", ["#8a80a0", "#5c536f", "#3b3450", "#241e33"])
STARS = [C("#ffffff"), C("#e6fff2"), C("#b0ffd8"), C("#5ef0b0")]


def _pillar(height, t, glow=1.0, fade=1.0):
    """A pillar of starlight rising from a broken crust socket: a white core, emerald halo and
    rings of light climbing it (periodic in t)."""
    cv = TCanvas(TW, TH)
    if height > 1:
        top = TG + 1 - height
        cv.part(line=False)
        X = cv.px
        Y = cv.py
        inside = (Y >= top) & (Y <= TG + 1)
        dx = np.abs(X - 16.0)
        wav = 1.2 * np.sin(Y * 0.31 + t * TAU) + 0.6 * np.sin(Y * 0.83 - t * TAU * 2)
        halo_w = (6.0 + wav) * fade
        core_w = (2.4 + 0.5 * np.sin(Y * 0.5 + t * TAU)) * fade
        m_halo = inside & (dx <= halo_w)
        inten = np.clip(0.3 + 0.55 * (1.0 - dx / np.maximum(halo_w, 0.5)) * glow, 0, 1)
        cv.put(m_halo, inten, GLOW)
        m_core = inside & (dx <= core_w)
        cv.put(m_core, 0.95 * glow + 0.05, GLOW)
        # rings climbing the pillar
        for k in range(3):
            yy = TG - ((t + k / 3.0) % 1.0) * height
            m_ring = inside & (np.abs(Y - yy) <= 0.8) & (dx <= halo_w + 2.0)
            cv.put(m_ring, 0.82 * glow, GLOW)
        # a pointed tip
        m_tip = (Y < top) & (Y >= top - 6) & (dx <= (Y - (top - 6)) * 0.5 * fade)
        cv.put(m_tip, 0.7 * glow, GLOW)
    # the crust socket at the foot
    cv.part()
    cv.poly([(5, TG + 1), (8, TG - 4), (13, TG - 6), (19, TG - 6), (24, TG - 3), (27, TG + 1)], CRUST, base=0.55)
    f = cv.render()
    f[TG + 1:] = 0
    return f


def star_wall():
    rise = []
    for i, h in enumerate((16, 40, 66, 86, 92)):
        f = _pillar(h, i / 5.0, glow=1.0)
        FX.burst(f, 16, TG - 2, 5 - i * 0.6, [STARS[0], STARS[1], STARS[2]], seed=700 + i, jag=0.4, rays=4)
        rise.append(f)
    idle = []
    for i in range(4):
        f = _pillar(92, i / 4.0, glow=0.85 + 0.15 * math.sin(TAU * i / 4))
        motes(f, 16, TG - 46, 6, 10, 40, STARS, seed=710 + i)
        idle.append(f)
    wither = []
    for i, (h, g) in enumerate(((90, 0.8), (80, 0.6), (60, 0.45), (32, 0.3), (8, 0.2))):
        f = _pillar(h, i / 5.0, glow=g, fade=1.0 - 0.15 * i)
        motes(f, 16, TG - h * 0.5, 8, 4, 16, STARS[1:], seed=720 + i)
        wither.append(f)
    s = Sheet("sprites/projectiles/star_wall.png", TW, TH, [16, 96])
    s.add("rise", rise, durations=[0.05, 0.05, 0.05, 0.06, 0.1], loop=False)
    s.add("idle", idle, fps=8, loop=True)
    s.add("wither", wither, durations=[0.08, 0.08, 0.08, 0.08, 0.12], loop=False)
    s.save("animations/star_wall.json")


# ============================================================= crumble_platform_star (48x48)
STAR_CRUST = mat("cp_crust", ["#7a7092", "#544b68", "#3b3450", "#262038", "#16121f"], th=[0.86, 0.68, 0.48, 0.28])
STAR_CRACK = mat("cp_crack", ["#ffe8f8", "#ff9ae0", "#e0329e", "#8e1a6a"], emissive=True, outline=None)


def _slab(stage):
    """One row (48x16 = left, middle, right cells): a slab of dead crust, its cracks glowing
    brighter and wider as it is about to give way."""
    cv = Canvas(48, 16)
    cv.part()
    cv.poly([(0, 1), (48, 1), (48, 11), (44, 13), (36, 12), (28, 14), (20, 12), (12, 14), (4, 12), (0, 13)],
            STAR_CRUST, bevel=1.4, base=0.56)
    rng = np.random.default_rng(900 + stage)
    cv.part(line=False)
    # cracks: a zig-zag along the slab, more and brighter at each stage
    cracks = [[(3, 4), (8, 7), (13, 5), (18, 9)], [(22, 3), (27, 6), (31, 4)], [(34, 8), (39, 5), (45, 9)]]
    if stage >= 1:
        cracks += [[(10, 2), (12, 6), (11, 11)], [(30, 2), (33, 7), (32, 12)]]
    if stage >= 2:
        cracks += [[(19, 2), (21, 7), (20, 12)], [(40, 2), (41, 7), (43, 12)], [(2, 9), (6, 11)]]
    for pts in cracks:
        for a, b in zip(pts[:-1], pts[1:]):
            cv.line(a, b, STAR_CRACK, inten=0.55 + 0.2 * stage, decal=True)
    # the bright top edge (the arena floor's walkable surface)
    for x in range(48):
        cv.px_set(x, 1, STAR_CRUST, inten=0.95 if (x + stage) % 7 else 0.75, decal=True)
    for _ in range(4 + 3 * stage):
        x, y = int(rng.integers(2, 46)), int(rng.integers(3, 11))
        cv.px_set(x, y, STAR_CRACK, inten=0.35 + 0.2 * stage, decal=True)
    return cv.render()


def crumble_star():
    img = np.zeros((48, 48, 4), np.uint8)
    for stage in range(3):
        img[stage * 16:(stage + 1) * 16] = _slab(stage)
    save_png(img, "sprites/objects/crumble_platform_star.png")


def build():
    plasma_flood()
    star_wall()
    crumble_star()
