"""Asterion, le Dévoreur de mondes: the titan of the clan raids, drawn for CLANS › RAIDS and the
sheet of a raid event (ui/widgets/AsterionFigure).  Pixel art like the game's bosses: pillow
shading lit from the top left, ordered dithering, highlights and dark outlines, a nebula skin
(violet, teal and magenta clouds sown with stars), obsidian armour plates with rivets and bronze
trims, molten cracks, burning eyes.

The titan faces the viewer (its right side is on the left of the picture).  It is cut into
separate layers drawn in this order (one per branch of its body, its heart and its aura):

  aura          a nebula halo that shimmers and motes orbiting around the body (6 frames)
  jambe_droite  TITANUS: a leg clad in armour (thigh plate, knee cop, greave, sabaton, bronze trims)
  jambe_gauche  GRAVITON: nebula skin with three gravity generators (cyan cores, an orbiting ring)
  abdomen       VOIDWORM: a segmented belly pierced by void pits, parasite worms crawling out
  torse         HEARTBREAKER: chest plates, a ribcage around the heart's cavity, molten seams
  bras_droit    PYROCLAST: a basalt arm cracked with lava, a glowing fist dripping magma
  bras_gauche   DREADFANG: a clawed arm, bone fangs on the pauldron and the forearm
  crane         SKULLMAW: a horned skull, an armoured brow, burning eyes, a maw full of fangs
  coeur         the cosmic heart, beating (4 frames)

Every layer but the aura comes in four looks (« intact », « affaibli »: dimmed and cracked,
« detruit »: turned to ash and chipped, its gigaboss defeated, « inconnu »: a silhouette of fog)
plus its « contour » (a white rim around it, tinted by the game: orange and pulsing when the
branch is under attack, the accent colour when it is hovered or focused).  The game draws the
idle animation: the aura's and the heart's frames, and the upper body rising by one pixel as
it breathes (`lift`).

Written as one sheet (assets/ui/raids/asterion.png, every cell cropped to its layer's box) and
its manifest (assets/ui/raids/asterion.json): the canvas, each layer's box, cells, frames per
second, breathing lift, the point its keyboard / gamepad focus sits on (`anchor`, deep inside
what is visible of it) and its hit mask (row spans [y, x0, x1[ of the canvas).  Deterministic:
fixed seeds; `check()` renders it twice and compares the bytes with each other and the files.

`python3 raid_asterion.py --review <file.png>` writes a contact sheet (every layer and the
assembled titan in each state, labelled in French, enlarged x2)."""
import io
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from helpers import (ASSETS, REPO, apply_ramp, bayer, bezier, blur, col, dither_alpha, ellipse_mask, fbm,  # noqa: E402
                     inner_edge, light_edges, lightness, line_pts, new, outline, poly_mask, rng, thick_path_mask)

W, H = 160, 128
REL_PNG = "ui/raids/asterion.png"
REL_JSON = "ui/raids/asterion.json"
MAX_BYTES = 200 * 1024
SHEET_W = 512
STATES = ["intact", "affaibli", "detruit", "inconnu"]
ORDER = ["aura", "jambe_droite", "jambe_gauche", "abdomen", "torse", "bras_droit", "bras_gauche", "crane", "coeur"]
LIFT = {"torse": 1, "bras_droit": 1, "bras_gauche": 1, "crane": 1, "coeur": 1}
FPS = {"aura": 5, "coeur": 6}

OUT = "#0b0f1e"
SKIN = ["#0c0820", "#170f36", "#24164f", "#35206b", "#4b2c8a", "#6a3dab", "#8f58c9"]
SKIN_B = ["#0a1024", "#10213f", "#16355d", "#1e4c7c", "#2b6a99", "#4290b8", "#6cb8d4"]
SKIN_C = ["#1a0820", "#311040", "#4c1a5e", "#6e2478", "#963293", "#c04aac", "#e477c6"]
ARMOR = ["#0e1222", "#181f36", "#232d4b", "#323f63", "#46587f", "#6177a0", "#8ea3c6"]
BRONZE = ["#24140a", "#4a2c14", "#7a4a20", "#b07430", "#e0a850", "#ffd98a"]
BONE = ["#1e1828", "#3a3048", "#5e5272", "#8a7c9c", "#b8aec6", "#e2dbea", "#fbf7ff"]
HORN = ["#0c0716", "#1a1030", "#2c1c4a", "#44306a", "#64508e", "#9078b8"]
BASALT = ["#0e080c", "#1c1014", "#2c1a1c", "#422622", "#5c3428", "#7a4630"]
MOLTEN = ["#4a0f08", "#8e2210", "#d2481a", "#ff7a2e", "#ffb050", "#ffe6a8"]
MAW = ["#0e0308", "#2e0a10", "#621612", "#a8321a", "#ec6a2a", "#ffb050"]
CAV = ["#0a0306", "#1a070c", "#341016", "#5e1c18", "#963218"]
WORM = ["#14300c", "#2f7a1a", "#4fbf26", "#9dff3a", "#e4ffb0"]
GRAV = ["#0f3a52", "#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
HEART = ["#6e1408", "#c8401a", "#ff7a2e", "#ffc060", "#ffe8a8", "#fffbea"]
ASH = ["#09070e", "#141020", "#1f1930", "#2c2442", "#3c3256", "#4d4268"]
FOG = ["#10142a", "#171e3a", "#20294c", "#2b365f", "#3a4875", "#4f5f90"]
VIOLET_CRACK = ("#f0b8ff", "#8a3ad0")


# ----------------------------------------------------------------------------- tools
def C(h, a=255):
    return np.array(col(h, a), np.uint8)


def dil(m, r=1):
    for _ in range(r):
        p = np.pad(m, 1)
        m = (p[1:-1, 1:-1] | p[:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, :-2] | p[1:-1, 2:]
             | p[:-2, :-2] | p[:-2, 2:] | p[2:, :-2] | p[2:, 2:])
    return m


def ero(m, r=1):
    for _ in range(r):
        p = np.pad(m, 1)
        m = p[1:-1, 1:-1] & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return m


def ell(cx, cy, rx, ry):
    return ellipse_mask(W, H, cx, cy, rx, ry)


def poly(pts):
    return poly_mask(W, H, pts)


def mirror(pts):
    return [(160 - x, y) for x, y in pts]


def bez(*p, n=24):
    return bezier(p[0], p[1], p[2], n=n, p3=p[3] if len(p) > 3 else None)


def tube(pts, r0, r1):
    return thick_path_mask(W, H, pts, list(np.linspace(r0, r1, len(pts))))


YY, XX = np.mgrid[0:H, 0:W]


def dist(cx, cy):
    return np.sqrt((XX + 0.5 - cx) ** 2 + (YY + 0.5 - cy) ** 2)


def paint(img, m, ramp, t, strength=0.5):
    rgba = apply_ramp(np.clip(t, 0, 1), ramp, bayer(H, W, 4), strength)
    img[m] = rgba[m]


def form(m, sigma=2.2, ambient=0.5, contrast=0.9):
    """Pillow shading lit from the top left, a highlight on the lit edge, a shade on the other."""
    if not m.any():
        return np.zeros((H, W), np.float32)
    t = ambient + contrast * 0.5 * lightness(m, sigma)
    tl, br = light_edges(m)
    return t + 0.2 * tl - 0.16 * br


def noise(seed, cell=14, octaves=3):
    return fbm(W, H, cell, octaves, seed, wrap_x=False)


def skin(img, m, seed, t):
    """The nebula skin: violet with clouds of teal and magenta, stars sown in it."""
    n1, n2 = noise(seed, 16, 4), noise(seed + 7, 11, 3)
    tt = np.clip(t + 0.26 * (n1 - 0.5), 0, 1)
    d = bayer(H, W, 4)
    a, b, c = (apply_ramp(tt, r, d, 0.5) for r in (SKIN, SKIN_B, SKIN_C))
    k = n2 + (d - 0.5) * 0.1
    out = np.where((k > 0.63)[..., None], b, np.where((k < 0.36)[..., None], c, a))
    img[m] = out[m]
    r = rng(seed + 99)
    roll = r.random((H, W))
    stars = (roll < 0.04) & ero(m, 1) & (tt > 0.3)
    pick = r.integers(0, 3, (H, W))
    for i, s in enumerate(["#f2e8ff", "#9ff8ff", "#ffd0f0"]):
        img[stars & (pick == i)] = C(s)


def plate(img, m, ramp=ARMOR, sigma=1.4, ambient=0.56, contrast=1.0, seed=0):
    if not m.any():
        return
    t = form(m, sigma, ambient, contrast) + 0.1 * (noise(seed + 300, 6, 2) - 0.5)
    paint(img, m, ramp, t, 0.4)
    e = inner_edge(m)
    tl, br = light_edges(m)
    img[br & e] = C(ramp[1])
    img[tl & e & ~br] = C(ramp[-2])


def rivet(img, x, y, ramp=ARMOR):
    img[y, x] = C(ramp[-1])
    img[y + 1, x + 1] = C(ramp[0])


def glow_in(img, m, cx, cy, r, c, s):
    a = np.clip(1 - dist(cx, cy) / r, 0, 1) ** 2 * s
    f = img.astype(np.float32)
    cc = np.array(col(c)[:3], np.float32)
    f[..., :3] = np.where(m[..., None], np.clip(f[..., :3] + cc * a[..., None], 0, 255), f[..., :3])
    img[:] = f.astype(np.uint8)


def walk(m, seed, start, ang, length, turn=0.55):
    """A crack: a random walk from `start` heading `ang`, inside `m`."""
    r = rng(seed)
    x, y = start
    pts = []
    for _ in range(length):
        xi, yi = int(round(x)), int(round(y))
        if not (0 <= xi < W and 0 <= yi < H and m[yi, xi]):
            break
        if not pts or pts[-1] != (xi, yi):
            pts.append((xi, yi))
        ang += (r.random() - 0.5) * turn * 2
        x += math.cos(ang)
        y += math.sin(ang)
    return pts


def crack(img, m, pts, core, halo, halo_k=0.55):
    on = np.zeros((H, W), bool)
    for x, y in pts:
        on[y, x] = True
    ring = dil(on, 1) & ~on & m & ((XX + YY) % 2 == 0)
    f = img.astype(np.float32)
    hc = np.array(col(halo)[:3], np.float32)
    f[ring, :3] = f[ring, :3] * (1 - halo_k) + hc * halo_k
    img[:] = f.astype(np.uint8)
    img[on & m] = C(core)


def lava(img, m, seed, starts):
    for i, (sx, sy, ang, n) in enumerate(starts):
        pts = walk(m, seed + i * 13, (sx, sy), ang, n)
        crack(img, m, pts, MOLTEN[4] if i % 2 else MOLTEN[5], MOLTEN[2])


def finish(img, color=OUT):
    outline(img, color)
    return img


# ----------------------------------------------------------------------------- parts
def part_crane():
    img = new(W, H)
    horns = (tube(bez((70, 13), (60, 14), (54, 9), (51, 2)), 3.8, 1.0) | tube(bez((90, 13), (100, 14), (106, 9), (109, 2)), 3.8, 1.0)
             | tube(bez((67, 20), (62, 22), (59, 21), (57, 17)), 2.0, 0.7) | tube(bez((93, 20), (98, 22), (101, 21), (103, 17)), 2.0, 0.7))
    t = form(horns, 1.2, 0.48, 1.0) + 0.18 * np.clip((15 - YY) / 14.0, 0, 1)
    paint(img, horns, HORN, t)
    bone = ell(80, 17, 14.5, 12.5) | ell(80, 25, 12, 8) | poly([(68, 27), (92, 27), (90, 37), (70, 37)])
    paint(img, bone, BONE, form(bone, 2.6, 0.52, 1.0) + 0.1 * (noise(11, 8, 3) - 0.5))
    # Cracks of violet light on the cranium.
    for i, (s, a, n) in enumerate([((70, 15), 2.2, 7), ((90, 12), 1.0, 8)]):
        crack(img, bone, walk(bone, 40 + i, s, a, n), VIOLET_CRACK[0], VIOLET_CRACK[1], 0.45)
    brow = poly([(65, 8), (95, 8), (97, 14), (88, 12), (80, 15), (72, 12), (63, 14)]) & dil(bone, 1)
    plate(img, brow, seed=1)
    for x in (68, 91):
        rivet(img, x, 10)
    gem = poly([(80, 7), (82.5, 10.5), (80, 14), (77.5, 10.5)])
    paint(img, gem, ["#4a0f4e", "#9a2a9a", "#ff6ad5", "#ffd0f2"], 1 - dist(79.5, 10) / 4.0)
    img[9, 79] = C("#fff4fc")
    # Burning eyes in their sockets.
    sockets = ell(74, 19.5, 3.7, 2.7) | ell(86, 19.5, 3.7, 2.7)
    img[sockets] = C("#100614")
    for cx in (74.2, 85.8):
        core = ell(cx, 19.8, 1.7, 1.1)
        img[dil(core, 1) & sockets & ~core] = C(MOLTEN[3])
        img[core] = C(MOLTEN[5])
        glow_in(img, bone & ~sockets, cx, 19.5, 6.5, "#ff7a2e", 0.5)
    img[poly([(77.5, 23), (82.5, 23), (80, 27)])] = C("#100614")
    # The maw: a furnace behind two rows of fangs.
    mouth = poly([(71, 29), (89, 29), (88, 35.5), (72, 35.5)])
    paint(img, mouth, MAW, 1.05 - np.abs(YY + 0.5 - 32.2) / 3.4 - np.abs(XX + 0.5 - 80) / 16.0, 0.6)
    for i, x in enumerate([72, 75, 78, 81, 84, 87]):
        n = 3 if i % 2 == 0 else 2
        for k in range(n):
            img[29 + k, x] = C(BONE[5])
            if k < n - 1:
                img[29 + k, x + 1] = C(BONE[3])
    for i, x in enumerate([73, 76, 79, 82, 85]):
        n = 2 if i % 2 == 0 else 3
        for k in range(n):
            img[35 - k, x + 1] = C(BONE[4])
            if k < n - 1:
                img[35 - k, x] = C(BONE[5])
    return finish(img)


def part_torse():
    img = new(W, H)
    body = poly([(72, 28), (88, 28), (89, 40), (71, 40)]) | poly([(52, 37), (108, 37), (113, 44), (105, 62), (97, 77), (63, 77), (55, 62), (47, 44)])
    skin(img, body, 21, form(body, 3.0, 0.5, 0.9))
    for i, (s, a, n) in enumerate([((58, 60), 1.1, 7), ((102, 60), 2.0, 7), ((62, 72), -0.3, 5), ((98, 72), 3.4, 5)]):
        crack(img, body, walk(body, 60 + i, s, a, n), MOLTEN[4], MOLTEN[2])
    cav = ell(80, 54, 7.5, 8.5)
    paint(img, cav, CAV, 1.1 - dist(80, 54) / 8.5, 0.6)
    ribs = np.zeros((H, W), bool)
    for y in (46, 51, 56, 61):
        ribs |= tube(bez((73.5, y), (69, y + 0.5), (65, y + 4)), 1.4, 0.8) | tube(bez((86.5, y), (91, y + 0.5), (95, y + 4)), 1.4, 0.8)
    ribs |= poly([(78.6, 39), (81.4, 39), (81.6, 46), (78.4, 46)])
    paint(img, ribs, BONE, form(ribs, 0.9, 0.5, 1.1))
    pl = [(53, 40), (76, 40), (74, 46), (70, 52), (61, 55), (53, 50)]
    gorget = poly([(69, 34), (91, 34), (93, 39), (67, 39)])
    for k, m in enumerate([gorget, poly(pl), poly(mirror(pl))]):
        plate(img, m, seed=10 + k)
    for x, y in [(56, 42), (72, 42), (56, 48), (103, 42), (87, 42), (103, 48), (70, 36), (89, 36)]:
        rivet(img, x, y)
    for r in range(2):
        y0 = 63 + r * 6
        plate(img, poly([(64 + 2 * r, y0), (77, y0), (77, y0 + 4), (65 + 2 * r, y0 + 4)]), seed=20 + r)
        plate(img, poly([(83, y0), (96 - 2 * r, y0), (95 - 2 * r, y0 + 4), (83, y0 + 4)]), seed=22 + r)
    glow_in(img, body & ~ero(cav, 2), 80, 54, 15, "#ff7a2e", 0.55)
    return finish(img)


def part_abdomen():
    img = new(W, H)
    belly = ell(80, 84, 19.5, 12.5) | poly([(63, 74), (97, 74), (100, 84), (60, 84)])
    skin(img, belly, 31, form(belly, 3.0, 0.48, 0.9))
    for y0 in (79, 85, 90):  # the belly's segments
        seg = belly & (np.abs(YY + 0.5 - (y0 + 0.012 * (XX + 0.5 - 80) ** 2)) < 0.5)
        img[seg] = C(SKIN[1])
        img[np.roll(seg, 1, axis=0) & belly & ~seg] = C(SKIN[5])
    worms = []
    for (cx, cy, rx, ry), path in [((71, 81, 3.2, 2.4), bez((71, 81), (66, 76), (61, 78), (59, 83))),
                                   ((89, 86, 3.6, 2.6), bez((89, 86), (95, 82), (100, 84), (102, 89))),
                                   ((79, 91, 2.6, 1.9), bez((79, 91), (77, 95), (80, 98.5), (85, 99)))]:
        pit = ell(cx, cy, rx, ry)
        rim = dil(pit, 1) & ~pit & belly
        img[rim] = np.where(((XX + YY) % 2 == 0)[rim][:, None], C("#ff6ad5"), C("#a02a98"))
        paint(img, pit, ["#05030a", "#12061e", "#260c36"], 0.9 - dist(cx, cy) / max(rx, ry), 0.4)
        glow_in(img, belly & ~pit & ~rim, cx, cy, rx + 4, "#c04aac", 0.35)
        worms.append(path)
    for w, path in enumerate(worms):  # the parasites crawling out of the pits
        pts = path[4::2]
        for i, (x, y) in enumerate(pts):
            r = 1.2 + 0.5 * i / max(1, len(pts) - 1)
            seg = ell(x, y, r, r)
            t = 0.45 + 0.4 * (1 - dist(x - 0.6, y - 0.6) / (r + 0.5)) - (0.18 if i % 2 else 0)
            paint(img, seg, WORM, t, 0.3)
        hx, hy = pts[-1]
        img[int(hy), int(hx)] = C(OUT)
        img[int(hy) - 1, int(hx) - 1] = C(WORM[4])
    return finish(img)


def _leg(side):
    """The thigh, shin and foot of a leg (side -1: the titan's right, on the left of the picture)."""
    def X(x):
        return 80 + side * (80 - x)
    thigh = tube(bez((X(70), 88), (X(68), 96), (X(64), 104)), 8.0, 6.5)
    shin = tube(bez((X(64), 106), (X(62), 112), (X(60), 119)), 6.0, 5.0)
    foot = poly([(X(49), 117), (X(68), 117), (X(71), 125), (X(47), 125)])
    return thigh, shin, foot, X


def part_jambe_droite():
    """Titanus: armour from hip to toe."""
    img = new(W, H)
    thigh, shin, foot, X = _leg(-1)
    leg = thigh | shin | foot
    skin(img, leg, 41, form(leg, 2.5, 0.42, 0.9))
    tp = thigh & poly([(57, 86), (81, 86), (76, 101), (59, 101)])
    gr = shin & poly([(53, 109), (71, 109), (67, 118), (54, 118)])
    plate(img, tp, seed=40)
    plate(img, gr, seed=41)
    for y0 in (117, 120, 123):
        plate(img, foot & (YY >= y0) & (YY < y0 + 3), seed=42 + y0)
    for m in (tp, gr):  # bronze trims on the top edges
        top = m & ~np.roll(m, 1, axis=0)
        img[top] = C(BRONZE[4])
        img[np.roll(top, 1, axis=0) & m] = C(BRONZE[2])
    knee = ell(63.5, 105, 5.4, 4.4)
    plate(img, knee, BRONZE, sigma=1.0, seed=44)
    for k in range(3):
        img[108 + k, 63] = C(BRONZE[4 - k])
    for x, y in [(60, 90), (73, 90), (61, 97), (56, 111), (66, 111), (57, 116)]:
        rivet(img, x, y)
    crack(img, leg, walk(leg, 47, (69, 102), 2.3, 5), MOLTEN[4], MOLTEN[2])
    for x in (47, 50, 53):  # claws over the toes
        img[124, x] = C(BONE[5])
        img[125, x - 1] = C(BONE[3])
    return finish(img)


def part_jambe_gauche():
    """Graviton: nebula skin and three gravity generators."""
    img = new(W, H)
    thigh, shin, foot, X = _leg(1)
    leg = thigh | shin | foot
    skin(img, leg, 51, form(leg, 2.5, 0.46, 0.9))
    for y0 in (117, 121):
        plate(img, foot & (YY >= y0) & (YY < y0 + 4), seed=50 + y0)
    housing = poly([(87, 91), (98, 91), (99, 99), (86, 99)])
    plate(img, housing, seed=52)
    knee = ell(96.5, 106, 5.2, 5.2)
    plate(img, knee, seed=53)
    ankle = ell(100, 117.5, 3.2, 2.4)
    plate(img, ankle, seed=54)
    for cx, cy, r in [(92.5, 95, 2.4), (96.5, 106, 2.8), (100, 117.5, 1.5)]:
        core = ell(cx, cy, r, r)
        paint(img, core, GRAV, 1.15 - dist(cx - 0.5, cy - 0.5) / (r + 0.6), 0.3)
        glow_in(img, leg & ~core, cx, cy, r + 5, "#39f0d8", 0.35)
    for x0, y0, x1, y1 in [(93, 99, 95, 102), (98, 110, 99, 115)]:  # conduits
        for i, (x, y) in enumerate(line_pts(x0, y0, x1, y1)):
            img[y, x] = C(GRAV[3] if i % 2 == 0 else GRAV[1])
    # The ring orbiting the knee: in front of the leg below its centre, behind it above.
    e = (((XX + 0.5 - 96.5) / 9.0) ** 2 + ((YY + 0.5 - 106) / 2.4) ** 2)
    ring = (np.abs(e - 1) < 0.28) & ((YY + 0.5 >= 106) | ~leg)
    img[ring] = np.where(((XX // 2) % 2 == 0)[ring][:, None], C(GRAV[3]), C(GRAV[2]))
    return finish(img)


def part_bras_droit():
    """Pyroclast: a basalt arm cracked with lava, a glowing fist dripping magma."""
    img = new(W, H)
    upper = tube(bez((50, 46), (44, 52), (38, 60)), 6.0, 5.2)
    fore = tube(bez((37, 62), (32, 70), (27, 78)), 5.6, 7.0)
    fist = ell(23, 84, 9.5, 8.5)
    drops = ell(18, 96, 1.4, 1.9) | ell(27, 98.5, 1.0, 1.4) | tube([(18, 91), (18, 94.5)], 0.8, 0.9)
    skin(img, upper, 61, form(upper, 2.0, 0.46, 0.9))
    rock = fore | fist
    paint(img, rock, BASALT, form(rock, 2.4, 0.5, 1.0) + 0.12 * (noise(62, 5, 2) - 0.5))
    glow_in(img, fist, 21, 86, 11, "#ff7a2e", 0.55)
    lava(img, rock, 63, [(36, 63, 2.0, 9), (31, 69, 1.6, 9), (29, 74, 2.4, 8), (27, 79, 2.2, 9), (19, 81, 2.0, 8), (26, 86, 1.0, 7),
                         (16, 86, 1.3, 6), (30, 84, 2.6, 6), (22, 90, -0.2, 6)])
    for x, y in [(18, 79), (22, 77), (26, 78), (29, 81)]:  # molten knuckles
        k = ell(x, y, 1.6, 1.3)
        img[k & fist] = C(MOLTEN[4])
        img[y - 1, x - 1] = C(MOLTEN[5])
    paint(img, drops, MOLTEN, 0.95 - (YY - 91) / 14.0, 0.4)
    lava(img, upper, 64, [(47, 49, 2.3, 6), (41, 56, 2.0, 5)])
    paul = ell(51, 42, 10, 8.5)
    spikes = tube(bez((46, 37), (44, 33), (42, 29)), 2.2, 0.6) | tube(bez((53, 35), (53, 31), (52, 27)), 2.0, 0.6)
    paint(img, spikes, HORN, form(spikes, 0.9, 0.5, 1.0))
    plate(img, paul, seed=65)
    crack(img, paul, walk(paul, 66, (44, 45), 0.1, 12), MOLTEN[4], MOLTEN[2])
    for x, y in [(47, 39), (55, 39), (51, 47)]:
        rivet(img, x, y)
    return finish(img)


def part_bras_gauche():
    """Dreadfang: a clawed arm, bone fangs on the pauldron and along the forearm."""
    img = new(W, H)
    upper = tube(bez((110, 46), (116, 52), (122, 60)), 6.0, 5.2)
    fore = tube(bez((123, 62), (128, 70), (133, 78)), 5.6, 5.0)
    hand = ell(137, 83, 6.8, 6.2)
    fangs = np.zeros((H, W), bool)
    for b, c, tip in [((132, 87), (129, 93), (128, 99)), ((135, 88.5), (133, 95), (132, 101.5)), ((138.5, 89), (138, 95), (137, 101.5)),
                      ((141.5, 87.5), (143, 93), (143.5, 98))]:
        fangs |= tube(bez(b, c, tip), 1.9, 0.5)
    for b, tip in [((127, 66), (134, 60)), ((130, 71), (138, 67)), ((132.5, 76), (140, 74))]:
        fangs |= tube(bez(b, ((b[0] + tip[0]) / 2 + 1, (b[1] + tip[1]) / 2 + 1), tip), 1.7, 0.45)
    for b, c, tip in [((105, 37), (103, 32), (104, 26)), ((113, 36), (115, 31), (117, 27))]:
        fangs |= tube(bez(b, c, tip), 2.3, 0.5)
    paint(img, fangs, BONE, form(fangs, 0.9, 0.55, 1.1))
    arm = upper | fore | hand
    skin(img, arm, 71, form(arm, 2.2, 0.46, 0.9))
    bracer = fore & poly([(124, 66), (134, 64), (136, 74), (127, 76)])
    plate(img, bracer, seed=72)
    knuckles = hand & (YY < 81)
    plate(img, knuckles, seed=73)
    for i, (s, a, n) in enumerate([((114, 51), 0.9, 6), ((135, 85), 1.8, 4)]):
        crack(img, arm, walk(arm, 74 + i, s, a, n), VIOLET_CRACK[0], VIOLET_CRACK[1], 0.45)
    paul = ell(109, 42, 10, 8.5)
    plate(img, paul, seed=75)
    for x, y in [(105, 39), (113, 39), (109, 47), (128, 68), (132, 72)]:
        rivet(img, x, y)
    return finish(img)


def heart_frame(s):
    """The cosmic heart: `s` 0 (at rest) .. 1 (the beat)."""
    img = new(W, H)
    d = dist(80, 54)
    r = 4.4 + 1.3 * s
    halo = np.clip(1 - (d - r) / (3.5 + 2.5 * s), 0, 1) * (d > r)
    a = dither_alpha(halo * (150 + 80 * s) / 255.0, levels=(0, 64, 128, 192))
    img[a > 0] = C(MOLTEN[3])
    img[..., 3] = np.where(a > 0, a, img[..., 3])
    orb = d <= r
    t = 1.05 - d / r + 0.25 * lightness(orb, 1.2)
    paint(img, orb, HEART, t, 0.4)
    img[d <= 1.0 + 0.7 * s] = C(HEART[5])
    if s > 0.7:  # the sparkle of the beat
        for k in range(int(r) + 1, int(r) + 4):
            for x, y in [(80 + k, 54), (79 - k, 54), (80, 54 + k), (80, 53 - k)]:
                img[y, x] = C("#fff1c4", 255 if k < int(r) + 3 else 160)
    return img


def heart_mask():
    return dist(80, 54) <= 6.2


# ----------------------------------------------------------------------------- the states
def luminance(img):
    f = img[..., :3].astype(np.float32)
    return (0.3 * f[..., 0] + 0.59 * f[..., 1] + 0.11 * f[..., 2]) / 255.0


def cracks_in(img, m, seed, n, length, core, halo, halo_k=0.55):
    pts = np.argwhere(ero(m, 2))
    if not len(pts):
        return
    r = rng(seed)
    for i in range(n):
        y, x = pts[r.integers(0, len(pts))]
        crack(img, m, walk(m, seed + 7 * i + 1, (x, y), r.random() * 2 * math.pi, length), core, halo, halo_k)


def weakened(img, seed):
    m = img[..., 3] > 0
    f = img.astype(np.float32)
    lum = luminance(img)[..., None] * 255
    f[..., :3] = (f[..., :3] * 0.62 + lum * 0.38) * 0.74
    out = f.astype(np.uint8)
    out[..., 3] = img[..., 3]
    cracks_in(out, m & (img[..., 3] == 255), seed, 4, 10, "#12080c", "#b0401a", 0.6)
    return out


def destroyed(img, seed):
    m = img[..., 3] > 0
    out = new(W, H)
    t = np.clip(luminance(img) * 1.35, 0, 1)
    out[m] = apply_ramp(t, ASH, bayer(H, W, 4), 0.5)[m]
    out[..., 3] = np.where(m, np.maximum(img[..., 3], 0), 0)
    solid = m & (img[..., 3] == 255)
    edge = np.argwhere(inner_edge(solid) & ~inner_edge(ero(solid, 3) | ~solid))
    r = rng(seed)
    for _ in range(3 if len(edge) else 0):  # chipped
        y, x = edge[r.integers(0, len(edge))]
        out[dist(x + 0.5, y + 0.5) <= 2.3] = 0
    outline(out, OUT)
    keep = out[..., 3] > 0
    cracks_in(out, keep & solid, seed + 5, 7, 14, "#040309", "#2a1c3a", 0.7)
    for _ in range(6):  # embers
        pts = np.argwhere(ero(keep & solid, 1))
        if len(pts):
            y, x = pts[r.integers(0, len(pts))]
            out[y, x] = C("#7a2410")
    return out


def fogged(mask, seed):
    img = new(W, H)
    t = 0.38 + 0.7 * (noise(seed, 9, 3) - 0.5) + 0.18 * lightness(mask, 2.0)
    paint(img, mask, FOG, t, 0.6)
    e = inner_edge(mask)
    img[e & ((XX + YY) % 2 == 0)] = C(FOG[4])
    img[e & ((XX + YY) % 2 == 1)] = C(FOG[3])
    return finish(img)


def rim(mask):
    img = new(W, H)
    r1 = dil(mask, 1) & ~mask
    r2 = dil(mask, 2) & ~dil(mask, 1)
    img[r2] = (255, 255, 255, 110)
    img[r1] = (255, 255, 255, 255)
    return img


def aura_frames(union):
    """The halo of the titan: a nebula shimmering around it and motes orbiting it (6 frames, looping)."""
    near = blur(dil(union, 1).astype(np.float32), 3.2)
    fade = np.clip((H - 3 - YY) / 12.0, 0, 1) * np.clip((YY - 1) / 6.0, 0, 1) * np.clip(XX / 6.0, 0, 1) * np.clip((W - 1 - XX) / 6.0, 0, 1)
    halo = np.clip(near * 1.5, 0, 1) * fade
    A, B, Cn = noise(81, 12, 3), noise(82, 12, 3), noise(83, 20, 2)
    r = rng(84)
    ring = np.argwhere((halo > 0.04) & (halo < 0.5) & ~dil(union, 2))
    motes = [(ring[i][1], ring[i][0], 1 + 2 * r.random(), r.random() * 2 * math.pi, r.integers(0, 4)) for i in r.choice(len(ring), 34, replace=False)]
    cols = ["#e6d8ff", "#ff9ad8", "#8ffff0", "#ffd27a"]
    frames = []
    for f in range(6):
        ph = 2 * math.pi * f / 6
        n = 0.5 + (A - 0.5) * math.cos(ph) + (B - 0.5) * math.sin(ph)
        a = np.clip(halo * (0.55 + 1.1 * (n - 0.5)), 0, 1)
        alpha = dither_alpha(a * 0.5, levels=(0, 36, 72, 108))
        img = new(W, H)
        hot = np.clip(halo * 1.2, 0, 1)
        c1, c2, c3 = (np.array(col(c)[:3], np.float32) for c in ("#5a22a8", "#c04aac", "#b88cff"))
        rgb = c1 * (1 - Cn[..., None]) + c2 * Cn[..., None]
        rgb = rgb * (1 - hot[..., None] * 0.5) + c3 * hot[..., None] * 0.5
        img[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
        img[..., 3] = alpha
        for x, y, rad, p, ci in motes:
            q = ph + p
            mx, my = int(round(x + rad * math.cos(q))), int(round(y + rad * 0.6 * math.sin(q)))
            if not (1 <= mx < W - 1 and 1 <= my < H - 1):
                continue
            bright = 0.5 + 0.5 * math.sin(q * 2)
            img[my, mx] = C(cols[ci], 255)
            if bright > 0.6:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    img[my + dy, mx + dx] = C(cols[ci], 120)
        img[img[..., 3] == 0] = 0
        frames.append(img)
    return frames


# ----------------------------------------------------------------------------- the sheet
def render():
    """{layer: {"frames"/state: [RGBA canvas...]}} and the hit masks."""
    makers = {"jambe_droite": part_jambe_droite, "jambe_gauche": part_jambe_gauche, "abdomen": part_abdomen, "torse": part_torse,
              "bras_droit": part_bras_droit, "bras_gauche": part_bras_gauche, "crane": part_crane}
    layers, masks = {}, {}
    for i, name in enumerate(ORDER):
        if name in makers:
            base = makers[name]()
            mask = base[..., 3] > 0
            frames = [base]
        elif name == "coeur":
            frames = [heart_frame(s) for s in (0.0, 0.45, 1.0, 0.45)]
            base = heart_frame(0.45)
            mask = heart_mask()
        else:
            continue
        seed = 500 + 37 * i
        layers[name] = {"intact": frames, "affaibli": [weakened(base, seed)], "detruit": [destroyed(base, seed + 1)],
                        "inconnu": [fogged(mask, seed + 2)], "contour": [rim(mask)]}
        masks[name] = mask
    union = np.zeros((H, W), bool)
    for m in masks.values():
        union |= m
    layers["aura"] = {"intact": aura_frames(union)}
    return layers, masks


def anchor(visible):
    """A point deep inside what is visible of a layer: the pixel nearest the centre of its
    deepest erosion."""
    m = visible
    while True:
        e = ero(m, 1)
        if not e.any():
            break
        m = e
    pts = np.argwhere(m)
    cy, cx = pts.mean(axis=0)
    y, x = pts[np.argmin((pts[:, 0] - cy) ** 2 + (pts[:, 1] - cx) ** 2)]
    return [int(x), int(y)]


def spans(mask):
    out = []
    for y in range(H):
        row = mask[y]
        x = 0
        while x < W:
            if row[x]:
                x0 = x
                while x < W and row[x]:
                    x += 1
                out.append([y, x0, x])
            else:
                x += 1
    return out


def build():
    """The sheet (RGBA array) and the manifest (dict)."""
    layers, masks = render()
    cells = []  # (layer, state, frame, crop, box)
    boxes = {}
    for name in ORDER:
        any_px = np.zeros((H, W), bool)
        for imgs in layers[name].values():
            for im in imgs:
                any_px |= im[..., 3] > 0
        ys, xs = np.nonzero(any_px)
        boxes[name] = [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)]
    # Shelf packing, tallest first (1 px apart).
    items = []
    for name in ORDER:
        x, y, w, h = boxes[name]
        for state, imgs in layers[name].items():
            for f, im in enumerate(imgs):
                items.append((h, w, name, state, f, im[y:y + h, x:x + w]))
    items.sort(key=lambda it: (-it[0], ORDER.index(it[2]), it[3], it[4]))
    pos = {}
    cx = cy = shelf = 0
    for h, w, name, state, f, crop in items:
        if cx + w > SHEET_W:
            cx, cy, shelf = 0, cy + shelf + 1, 0
        pos[(name, state, f)] = (cx, cy)
        shelf = max(shelf, h)
        cx += w + 1
    sheet_h = cy + shelf
    sheet_h = (sheet_h + 3) // 4 * 4
    sheet = np.zeros((sheet_h, SHEET_W, 4), np.uint8)
    for h, w, name, state, f, crop in items:
        x, y = pos[(name, state, f)]
        sheet[y:y + h, x:x + w] = crop
    manifest = {"format": "cosmic.asterion_sprite", "version": 1, "canvas": [W, H], "sheet": REL_PNG,
                "comment": "Generated by tools/assetgen/environment/raid_asterion.py: do not edit. Layers in drawing order; box: "
                           "the layer's place on the canvas; cells: top left of each frame in the sheet; mask: [y, x0, x1[ spans of the "
                           "canvas (hit test); anchor: the point its focus sits on; lift: rows it rises while breathing.",
                "layers": []}
    above = np.zeros((H, W), bool)
    anchors = {}
    for name in reversed(ORDER):
        if name in masks:
            anchors[name] = anchor(masks[name] & ~above)
            above |= masks[name]
    for name in ORDER:
        entry = {"id": name, "box": boxes[name]}
        if name in FPS:
            entry["fps"] = FPS[name]
        if name in LIFT:
            entry["lift"] = LIFT[name]
        entry["cells"] = {state: [list(pos[(name, state, f)]) for f in range(len(imgs))] for state, imgs in layers[name].items()}
        if name in masks:
            entry["anchor"] = anchors[name]
            entry["mask"] = spans(masks[name])
        manifest["layers"].append(entry)
    return sheet, manifest


def encode(sheet, manifest):
    buf = io.BytesIO()
    Image.fromarray(sheet, "RGBA").save(buf, format="PNG", optimize=True)
    text = json.dumps(manifest, ensure_ascii=False, separators=(",", ":"))
    # One layer per line, keeps diffs readable.
    text = text.replace(',{"id"', ',\n{"id"').replace('"layers":[{', '"layers":[\n{') + "\n"
    return buf.getvalue(), text.encode("utf-8")


def generate():
    png, js = encode(*build())
    out = []
    for rel, data in ((REL_PNG, png), (REL_JSON, js)):
        path = os.path.join(ASSETS, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        out.append(path)
    return out


def check():
    errs = []
    png_path, js_path = os.path.join(ASSETS, REL_PNG), os.path.join(ASSETS, REL_JSON)
    if not (os.path.exists(png_path) and os.path.exists(js_path)):
        return [f"{REL_PNG} / {REL_JSON} missing (python3 tools/assetgen/environment/generate.py raids)"]
    if os.path.getsize(png_path) > MAX_BYTES:
        errs.append(f"{REL_PNG}: {os.path.getsize(png_path)} bytes, over the budget of {MAX_BYTES}")
    a, b = encode(*build()), encode(*build())
    if a != b:
        errs.append("raid_asterion: two renders differ (not deterministic)")
    with open(js_path, "rb") as f:
        if f.read() != a[1]:
            errs.append(f"{REL_JSON}: not the generator's output (regenerate)")
    on_disk = np.array(Image.open(png_path).convert("RGBA"))
    fresh = np.array(Image.open(io.BytesIO(a[0])).convert("RGBA"))
    if on_disk.shape != fresh.shape or (on_disk != fresh).any():
        errs.append(f"{REL_PNG}: not the generator's output (regenerate)")
    return errs


# ----------------------------------------------------------------------------- review
def review(path):
    """A contact sheet: every layer and the assembled titan in each state, labelled, x2."""
    sheet, man = build()
    cells = {L["id"]: L for L in man["layers"]}

    def canvas_of(name, state, f=0, tint=None):
        L = cells[name]
        x, y, w, h = L["box"]
        img = np.zeros((H, W, 4), np.uint8)
        sx, sy = L["cells"][state][f]
        img[y:y + h, x:x + w] = sheet[sy:sy + h, sx:sx + w]
        if tint is not None:
            img[..., :3] = (img[..., :3].astype(np.float32) * np.array(tint, np.float32) / 255).astype(np.uint8)
        return img

    def over(dst, src):
        a = src[..., 3:4].astype(np.float32) / 255
        dst[..., :3] = (dst[..., :3] * (1 - a) + src[..., :3] * a).astype(np.uint8)
        dst[..., 3] = np.maximum(dst[..., 3], src[..., 3])

    names = {"aura": "AURA", "jambe_droite": "JAMBE DROITE · Titanus", "jambe_gauche": "JAMBE GAUCHE · Graviton", "abdomen": "ABDOMEN · Voidworm",
             "torse": "TORSE · Heartbreaker", "bras_droit": "BRAS DROIT · Pyroclast", "bras_gauche": "BRAS GAUCHE · Dreadfang",
             "crane": "CRÂNE · Skullmaw", "coeur": "CŒUR COSMIQUE"}
    cols = [("intact", None, "intacte"), ("intact", "attaque", "attaquée"), ("affaibli", None, "affaiblie"), ("detruit", None, "détruite"),
            ("inconnu", None, "brume")]
    order = ["crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite", "coeur", "aura", "titan"]
    S, pad, lab = 3, 14, 34
    tw, th = W * S, H * S
    out = Image.new("RGB", (pad + len(cols) * (tw + pad), pad + len(order) * (th + lab + pad)), (10, 12, 26))
    font = ImageFont.truetype(os.path.join(REPO, "assets", "fonts", "ChakraPetch-SemiBold.ttf"), 17)
    draw = ImageDraw.Draw(out)
    bg = np.zeros((H, W, 4), np.uint8)
    bg[..., :3] = (20, 22, 44)
    bg[..., 3] = 255
    for r, name in enumerate(order):
        for c, (state, extra, label) in enumerate(cols):
            img = bg.copy()
            if name == "aura":
                over(img, canvas_of("aura", "intact", c))
                text = f"AURA · image {c + 1} sur 6"
            elif name == "titan":
                over(img, canvas_of("aura", "intact", 0))
                for n in ORDER[1:]:
                    over(img, canvas_of(n, state))
                    if extra:
                        over(img, canvas_of(n, "contour", 0, (255, 138, 76)))
                text = f"TITAN ASSEMBLÉ · {label}"
            else:
                over(img, canvas_of(name, state))
                if extra:
                    over(img, canvas_of(name, "contour", 0, (255, 138, 76)))
                text = f"{names[name]} · {label}"
            bx, by, bw, bh = cells[name]["box"] if name in cells and name != "aura" else (0, 0, W, H)
            bx, by = max(0, bx - 3), max(0, by - 3)
            bw, bh = min(W - bx, bw + 6), min(H - by, bh + 6)
            k = max(1, min(tw // bw, th // bh))
            tile = Image.fromarray(img[by:by + bh, bx:bx + bw, :3], "RGB").resize((bw * k, bh * k), Image.NEAREST)
            x0, y0 = pad + c * (tw + pad), pad + r * (th + lab + pad)
            draw.rectangle([x0, y0, x0 + tw - 1, y0 + th - 1], fill=(20, 22, 44))
            out.paste(tile, (x0 + (tw - bw * k) // 2, y0 + (th - bh * k) // 2))
            while draw.textlength(text, font=font) > tw and len(text) > 4:
                text = text[:-2] + "…"
            draw.text((x0 + (tw - draw.textlength(text, font=font)) / 2, y0 + th + 7), text, fill=(238, 240, 255), font=font)
    out.save(path, optimize=True)
    return path


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--review":
        print(review(sys.argv[2]))
    elif len(sys.argv) > 1 and sys.argv[1] == "--check":
        e = check()
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    else:
        for p in generate():
            print(p, os.path.getsize(p))
