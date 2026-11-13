"""Animated interactive objects (docs/ASSET_SPEC.md section 6) + JSONs.
Static 3-slice sprites, spikes, laser beam and liquids live in objects_static.py;
the v2 ability objects (grapple_point, cracked_floor, updraft_vent,
heart_container) in player_objects.py."""
import math

import numpy as np

import effects as FX
import materials as M
import objects_static
import player_objects
import power_symbols
from common.palette import (ACID, AMBER, CYAN, DANGER, MOSS, OUTLINE, SCARF, SKY, STONE, SUIT, VIOLET,
                            hex_rgba)
from raster import Canvas, Mat, put
from sheet import Sheet

TAU = 2 * math.pi
WHITE = hex_rgba('#ffffff')
CRYSTAL_OFF = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="crys_off", thresholds=[0.82, 0.6, 0.36])
CRYSTAL_ON = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="crys_on", thresholds=[0.82, 0.6, 0.36])
RUNE_OFF = Mat([STONE[1], STONE[0]], name="rune_off", thresholds=[0.5])
RUNE_ON = Mat([CYAN[2], CYAN[1]], emissive=True, name="rune_on", thresholds=[0.5])
GOLD = Mat([AMBER[2], AMBER[1], AMBER[0], hex_rgba('#7a4512')], name="gold", thresholds=[0.82, 0.58, 0.34])
PINK = Mat([hex_rgba('#ffd0dc'), DANGER[1], DANGER[0], hex_rgba('#5a0f1e')], emissive=True, name="pink",
           outline=hex_rgba('#3a0814'), thresholds=[0.82, 0.58, 0.34])
CYANORB = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=hex_rgba('#0f3b3a'), name="cyanorb",
              thresholds=[0.84, 0.6, 0.34])
VIOLET_C = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], name="violet_c", thresholds=[0.84, 0.6, 0.34])
MUSH = Mat([VIOLET[2], VIOLET[1], VIOLET[0], hex_rgba('#3b1a6e')], name="mush", thresholds=[0.82, 0.58, 0.34])
STEM = Mat([SUIT[1], SUIT[2], SUIT[3]], name="stem", thresholds=[0.7, 0.4])
RED_LIGHT = Mat([hex_rgba('#ffd0c0'), DANGER[1], DANGER[0]], emissive=True, name="redlight", thresholds=[0.75, 0.4])


# ------------------------------------------------------------- checkpoint
def checkpoint_frame(level=0.0, rune_h=0.0, bob=0, flash=0.0, sparkle_ph=None):
    """level: crystal glow 0..1, rune_h: how far up the light has travelled (0..1)."""
    cv = Canvas(24, 48)
    # pedestal
    cv.part()
    cv.poly([(2, 47.9), (22, 47.9), (21, 43), (18, 41), (6, 41), (3, 43)], M.STONE_M, bevel=1.4, base=0.55)
    cv.ellipse((6, 43.5), 3.0, 1.2, M.MOSS_M, decal=True)
    # pillar
    cv.part()
    cv.poly([(7.5, 41.5), (16.5, 41.5), (15.5, 16), (8.5, 16)], M.STONE_M, bevel=1.4, base=0.6)
    # prongs holding the crystal
    cv.part()
    cv.poly([(8, 17), (6.5, 10), (8.5, 9), (10.5, 16.5)], M.STONE_DARK, bevel=1, base=0.6)
    cv.poly([(16, 17), (17.5, 10), (15.5, 9), (13.5, 16.5)], M.STONE_DARK, bevel=1, base=0.6)
    # runes up the pillar: lit up to rune_h
    cv.part(line=False)
    ys = [38, 34, 30, 26, 22, 19]
    for k, y in enumerate(ys):
        lit = rune_h * len(ys) > k
        mat = RUNE_ON if lit else RUNE_OFF
        cv.line((10.5, y + 0.5), (13.5, y + 0.5), mat, inten=0.8, decal=True)
        cv.px_set(12, y - 1, mat, inten=0.8, decal=True)
    # crystal
    cv.part()
    cy = 7.5 + bob
    mat = CRYSTAL_ON if level > 0.05 else CRYSTAL_OFF
    cv.poly([(12, cy - 6.5), (16, cy - 1), (12, cy + 7), (8, cy - 1)], mat, bevel=1.6,
            base=0.45 + 0.35 * level)
    cv.line((12, cy - 5), (12, cy + 5), mat, inten=0.95 if level > 0.05 else 0.7, decal=True)
    img = cv.render()
    if flash > 0:
        img = FX.flash(img, CYAN[3], flash)
    if sparkle_ph is not None:
        for k in range(3):
            a = sparkle_ph + TAU * k / 3
            x = int(12 + math.cos(a) * 8)
            y = int(8 + bob + math.sin(a) * 3)
            if 0 <= x < 24 and img[y, x, 3] == 0:
                img[y, x] = CYAN[2] if k else CYAN[3]
    return img


def checkpoint():
    s = Sheet("sprites/objects/checkpoint.png", 24, 48, [12, 48])
    s.add("inactive", [checkpoint_frame()], fps=1, loop=False)
    act = []
    for i in range(8):
        t = (i + 1) / 8
        rune = min(1.0, t * 1.6)
        lvl = max(0.0, (t - 0.55) / 0.45)
        f = checkpoint_frame(level=lvl, rune_h=rune, flash=0.5 if i == 6 else 0.0)
        if i >= 5:
            FX.sparkle(f, 12, 8, 2 + (i - 5), CYAN[3], CYAN[1])
        act.append(f)
    s.add("activate", act, fps=12, loop=False, next="active")
    active = [checkpoint_frame(level=0.7 + 0.3 * math.sin(TAU * i / 6), rune_h=1.0, bob=[0, 0, -1, -1, -1, 0][i],
                               sparkle_ph=TAU * i / 6) for i in range(6)]
    s.add("active", active, fps=8, loop=True)
    s.save("animations/checkpoint.json")


# ------------------------------------------------------------ exit portal
def portal_frame(ph, active):
    W = H = 64
    cx, cy = 32.0, 33.0
    img = np.zeros((H, W, 4), np.uint8)
    # vortex interior
    ys, xs = np.mgrid[0:H, 0:W]
    dx, dy = xs + 0.5 - cx, ys + 0.5 - cy
    r = np.sqrt(dx * dx + (dy * 0.92) ** 2)
    th = np.arctan2(dy, dx)
    inside = r < 19.5
    swirl = np.sin(3 * th + r * (0.45 if active else 0.3) - ph * (2 if active else 1))
    if active:
        cols = [hex_rgba('#e6fffb', 255), hex_rgba('#8ffff0', 240), hex_rgba('#39f0d8', 220),
                hex_rgba('#b04cff', 200), hex_rgba('#6a2bb8', 190), hex_rgba('#22365a', 170)]
        v = swirl * 0.5 + 0.5
        v = v * 0.7 + (1 - r / 19.5) * 0.5
        idx = np.clip(((1 - v) * len(cols)).astype(int), 0, len(cols) - 1)
    else:
        cols = [hex_rgba('#39f0d8', 150), hex_rgba('#6a2bb8', 120), hex_rgba('#22365a', 110),
                hex_rgba('#1a2747', 100), hex_rgba('#121a33', 90)]
        v = swirl * 0.5 + 0.5
        v = v * 0.5 + (1 - r / 19.5) * 0.3
        idx = np.clip(((1 - v) * len(cols)).astype(int), 0, len(cols) - 1)
    pal = np.array(cols, np.uint8)
    img[inside] = pal[idx[inside]]
    # stone ring + base
    cv = Canvas(W, H)
    cv.part()
    cv.poly([(10, 63.9), (54, 63.9), (51, 58), (13, 58)], M.STONE_M, bevel=1.6, base=0.55)
    cv.part()
    outer = (cv.px - cx) ** 2 + ((cv.py - cy) * 0.92) ** 2 <= 26.5 ** 2
    inner = (cv.px - cx) ** 2 + ((cv.py - cy) * 0.92) ** 2 <= 20.0 ** 2
    ring = outer & ~inner
    rr = np.sqrt((cv.px - cx) ** 2 + ((cv.py - cy) * 0.92) ** 2)
    nx, ny = (cv.px - cx) / np.maximum(rr, 1), (cv.py - cy) / np.maximum(rr, 1)
    mid = (rr - 23.25) / 3.25  # -1 inner edge .. 1 outer edge
    nz = np.sqrt(np.clip(1 - mid ** 2, 0, 1))
    inten = cv.light_from_normal(nx * mid, ny * mid, nz)
    cv.put(ring, inten, M.STONE_M)
    # keystone blocks
    for k in range(12):
        a = TAU * k / 12 + 0.26
        p0 = (cx + math.cos(a) * 20.3, cy + math.sin(a) * 20.3 / 0.92)
        p1 = (cx + math.cos(a) * 26.3, cy + math.sin(a) * 26.3 / 0.92)
        cv.line(p0, p1, M.STONE_DARK, inten=0.2, decal=True)
    # runes
    cv.part(line=False)
    rm = RUNE_ON if active else Mat([AMBER[1], AMBER[0]], emissive=True, name="amb_rune", thresholds=[0.5])
    for k in range(12):
        a = TAU * k / 12 + 0.26 + TAU / 24
        x, y = cx + math.cos(a) * 23.3, cy + math.sin(a) * 23.3 / 0.92
        on = active or (k + int(ph / TAU * 12)) % 3 == 0
        if on:
            cv.px_set(x, y, rm, inten=0.9, decal=True)
            cv.px_set(x + math.cos(a + 1.57), y + math.sin(a + 1.57), rm, inten=0.5, decal=True)
    # moss on top of the ring
    cv.ellipse((26, 8.5), 7, 2.0, M.MOSS_M, decal=True)
    cv.ellipse((44, 12), 3.5, 1.4, M.MOSS_M, decal=True)
    frame = cv.render()
    FX.overlay(img, frame)
    # orbiting motes
    for k in range(4 if active else 2):
        a = ph * (1.0 if active else 0.5) + TAU * k / (4 if active else 2)
        rad = 14 - (ph / TAU * 6 + k * 3) % 12
        x, y = int(cx + math.cos(a) * rad), int(cy + math.sin(a) * rad)
        put(img, x, y, CYAN[3] if active else CYAN[1])
    return img


def exit_portal():
    s = Sheet("sprites/objects/exit_portal.png", 64, 64, [32, 64])
    s.add("idle", [portal_frame(TAU * i / 8, False) for i in range(8)], fps=8, loop=True)
    s.add("active", [portal_frame(TAU * i / 8, True) for i in range(8)], fps=12, loop=True)
    s.save("animations/exit_portal.json")


# ---------------------------------------------------------- collectibles
def shard():
    out = []
    for i in range(8):
        a = TAU * i / 8
        w = abs(math.cos(a)) * 3.0 + 1.1
        cv = Canvas(12, 12)
        cv.part()
        front = math.cos(a) >= 0
        pts = [(6, 0.6), (6 + w, 5), (6, 11.4), (6 - w, 5)]
        cv.poly(pts, CYANORB, shade="grad", base=0.55)
        # facet: light half switches sides as it spins
        half = [(6, 0.6), (6 + (w if front else -w), 5), (6, 11.4)]
        cv.poly(half, CYANORB, flat=0.92 if front else 0.4, decal=True)
        img = cv.render()
        if i in (1, 5):
            FX.sparkle(img, 8 if i == 1 else 3, 2, 1, CYAN[3], CYAN[2])
        out.append(img)
    s = Sheet("sprites/objects/shard.png", 12, 12, [6, 6])
    s.add("spin", out, fps=10, loop=True)
    s.save("animations/shard.json")


def relic():
    out = []
    for i in range(8):
        bob = [0, 0, -1, -1, -1, 0, 0, 0][i]
        cv = Canvas(16, 16)
        cv.part()
        # idol: golden disc with a ring and a cyan gem, small wings
        cv.poly([(1.5, 7.5 + bob), (4, 5.5 + bob), (4.5, 9.5 + bob)], GOLD, bevel=0.8, base=0.6)
        cv.poly([(14.5, 7.5 + bob), (12, 5.5 + bob), (11.5, 9.5 + bob)], GOLD, bevel=0.8, base=0.6)
        cv.part()
        cv.disc((8, 7.5 + bob), 5.2, GOLD)
        cv.part(line=False)
        ring = ((cv.px - 8) ** 2 + (cv.py - 7.5 - bob) ** 2 <= 3.6 ** 2) & \
               ((cv.px - 8) ** 2 + (cv.py - 7.5 - bob) ** 2 > 2.4 ** 2)
        cv.put(ring, 0.15, GOLD, decal=True)
        cv.disc((8, 7.5 + bob), 1.7, CYANORB, decal=True)
        cv.part()
        cv.poly([(6.5, 13 + bob), (9.5, 13 + bob), (8, 15.2 + bob)], GOLD, flat=0.55)
        img = cv.render()
        # glint sweep
        gx = [3, 5, 7, 9, 11, -1, -1, -1][i]
        if gx > 0:
            for k in range(3):
                x, y = gx + k - 1, 4 + bob + 3 - k
                if 0 <= y < 16 and img[y, x, 3] and tuple(img[y, x]) != OUTLINE:
                    img[y, x] = WHITE if k == 1 else AMBER[2]
        if i == 4:
            FX.sparkle(img, 12, 3 + bob, 1, WHITE, AMBER[2])
        out.append(img)
    s = Sheet("sprites/objects/relic.png", 16, 16, [8, 8])
    s.add("idle", out, fps=8, loop=True)
    s.save("animations/relic.json")


def heart_mask(cv, cx, cy, k=1.0):
    x = (cv.px - cx) / (2.6 * k)
    y = -(cv.py - cy) / (2.6 * k)
    return (x * x + y * y - 1) ** 3 - x * x * y ** 3 <= 0


HEART = ["#.#", "###", ".#."]
HEART5 = [".#.#.", "#####", "#####", ".###.", "..#.."]
BOLT = ["..##", ".##.", "####", ".##.", "##..", "#..."]


def orb(kind):
    out = []
    for i in range(6):
        pulse = math.sin(TAU * i / 6)
        cv = Canvas(12, 12)
        cv.part()
        mat = PINK if kind == "health" else CYANORB
        cv.disc((6, 6), 4.8 + 0.3 * pulse, mat, bias=-0.3 + 0.05 * pulse)
        img = cv.render()
        if kind == "health":
            glyph, ox, oy = HEART5, 4, 4
            c_main, c_hi = hex_rgba('#fff0f4'), hex_rgba('#ffd0dc')
        else:
            glyph, ox, oy = BOLT, 4, 3
            c_main, c_hi = CYAN[3], CYAN[2]
        for y, row in enumerate(glyph):
            for x, ch in enumerate(row):
                if ch == "#":
                    put(img, ox + x, oy + y, c_main if (x + y + i) % 5 else c_hi)
        if i == 2:
            FX.sparkle(img, 9, 2, 1, WHITE, DANGER[1] if kind == "health" else CYAN[1])
        out.append(img)
    name = "health_orb" if kind == "health" else "energy_orb"
    s = Sheet(f"sprites/objects/{name}.png", 12, 12, [6, 6])
    s.add("idle", out, fps=8, loop=True)
    s.save(f"animations/{name}.json")


# ---------------------------------------------------------- power unlock
def power_unlock():
    s = Sheet("sprites/objects/power_unlock.png", 32, 32, [16, 16])
    for name in power_symbols.POWERS:
        frames = []
        for i in range(8):
            frames.append(power_symbols.tablet(name, i))
        s.add(name, frames, fps=8, loop=True)
    s.save("animations/power_unlock.json")


# ------------------------------------------------------------------ door
def door_frame(field):
    """field: 0 = open (no barrier) .. 1 = fully closed barrier."""
    W, H = 16, 64
    img = np.zeros((H, W, 4), np.uint8)
    # energy barrier between the emitters
    if field > 0:
        top, bot = 9, 55
        half = (bot - top) / 2 * field
        midy = (top + bot) / 2
        y0, y1 = int(round(top)), int(round(bot))
        for y in range(y0, y1):
            # retracts towards both emitters: gap opens from the middle
            if field < 1 and abs(y + 0.5 - midy) < (bot - top) / 2 * (1 - field):
                continue
            for x in range(3, 13):
                d = abs(x + 0.5 - 8)
                wav = math.sin(y * 0.7 + x * 0.3) * 0.5 + 0.5
                if d < 1.0:
                    c = CYAN[3]
                elif d < 2.0:
                    c = CYAN[2] if wav > 0.3 else CYAN[3]
                elif d < 3.5:
                    c = (CYAN[1][0], CYAN[1][1], CYAN[1][2], 200)
                else:
                    c = (CYAN[0][0], CYAN[0][1], CYAN[0][2], 110 if (y + x) % 2 else 70)
                img[y, x] = c
    cv = Canvas(W, H)
    for (ya, yb, flip) in ((0, 10, False), (54, 64, True)):
        cv.part()
        if not flip:
            pts = [(0.5, 0), (15.5, 0), (15.5, 6), (12, 9.5), (4, 9.5), (0.5, 6)]
        else:
            pts = [(0.5, 63.9), (15.5, 63.9), (15.5, 58), (12, 54.5), (4, 54.5), (0.5, 58)]
        cv.poly(pts, M.STONE_M, bevel=1.4, base=0.58)
        cv.part(line=False)
        ey = 8.5 if not flip else 55.5
        mat = RUNE_ON if field > 0 else RUNE_OFF
        cv.line((5, ey), (11, ey), mat, inten=0.9, decal=True)
        cv.px_set(8, 4 if not flip else 59, mat if field > 0 else Mat([AMBER[1]], emissive=True, name="amb1"),
                  inten=0.8, decal=True)
    cv.ellipse((5, 1.5), 4, 1.3, M.MOSS_M, decal=True)
    frame = cv.render()
    FX.overlay(img, frame)
    return img


def door():
    s = Sheet("sprites/objects/door.png", 16, 64, [8, 64])
    s.add("closed", [door_frame(1.0)], fps=1, loop=False)
    s.add("opening", [door_frame(1 - (i + 1) / 7) for i in range(6)], fps=14, loop=False, next="open")
    s.add("open", [door_frame(0.0)], fps=1, loop=False)
    s.add("closing", [door_frame((i + 1) / 7) for i in range(6)], fps=14, loop=False, next="closed")
    s.save("animations/door.json")


# ----------------------------------------------------------------- lever
def lever_frame(t):
    """t: 0 = off (handle tilted left) .. 1 = on (tilted right)."""
    cv = Canvas(16, 16)
    a = -math.pi / 2 + (-0.75 + 1.5 * t)
    piv = (8.0, 11.0)
    tip = (piv[0] + math.cos(a) * 8.0, piv[1] + math.sin(a) * 8.0)
    cv.part()
    cv.capsule(piv, tip, 0.8, 0.8, M.METAL)
    cv.part()
    cv.disc(tip, 1.6, RED_LIGHT if t < 0.5 else CRYSTAL_ON, flat=0.6)
    cv.part()
    cv.poly([(1.5, 15.9), (14.5, 15.9), (13, 11), (10, 9.5), (6, 9.5), (3, 11)], M.STONE_M, bevel=1.2, base=0.58)
    cv.part(line=False)
    cv.line((5, 13.5), (10, 13.5), RUNE_ON if t >= 1 else RUNE_OFF, inten=0.8, decal=True)
    return cv.render()


def lever():
    s = Sheet("sprites/objects/lever.png", 16, 16, [8, 16])
    s.add("off", [lever_frame(0.0)], fps=1, loop=False)
    s.add("switching", [lever_frame((i + 1) / 5) for i in range(4)], fps=16, loop=False, next="on")
    s.add("on", [lever_frame(1.0)], fps=1, loop=False)
    s.save("animations/lever.json")


# -------------------------------------------------------------- receptor
def receptor_frame(level, ph=0.0):
    cv = Canvas(16, 32)
    cv.part()
    cv.poly([(2, 31.9), (14, 31.9), (13, 28), (3, 28)], M.STONE_M, bevel=1.2, base=0.55)
    cv.part()
    cv.poly([(5, 28.5), (11, 28.5), (10, 14), (6, 14)], M.STONE_M, bevel=1.2, base=0.6)
    cv.part(line=False)
    for y in (24, 20, 17):
        cv.line((7, y), (9, y), RUNE_ON if level > 0 else RUNE_OFF, inten=0.8, decal=True)
    # dish / claws
    cv.part()
    cv.poly([(2, 7), (5, 14.5), (8, 15), (6, 9)], M.STONE_DARK, bevel=1, base=0.6)
    cv.poly([(14, 7), (11, 14.5), (8, 15), (10, 9)], M.STONE_DARK, bevel=1, base=0.6)
    # receiver crystal
    cv.part()
    mat = CRYSTAL_ON if level > 0 else CRYSTAL_OFF
    cv.disc((8, 9.5), 3.3 + 0.3 * level * math.sin(ph), mat, bias=-0.1 + 0.25 * level)
    img = cv.render()
    if level > 0:
        for k in range(4):
            a = ph + TAU * k / 4
            x, y = int(8 + math.cos(a) * 6), int(9 + math.sin(a) * 5)
            if 0 <= x < 16 and 0 <= y < 32 and img[y, x, 3] == 0:
                img[y, x] = CYAN[2]
        if int(ph / TAU * 6) % 3 == 0:
            FX.sparkle(img, 8, 2, 1, CYAN[3], CYAN[1])
    return img


def receptor():
    s = Sheet("sprites/objects/receptor.png", 16, 32, [8, 32])
    s.add("off", [receptor_frame(0.0)], fps=1, loop=False)
    s.add("on", [receptor_frame(1.0, TAU * i / 6) for i in range(6)], fps=8, loop=True)
    s.save("animations/receptor.json")


# ------------------------------------------------------------ bounce pad
def bounce_frame(h, squash):
    """h: cap height offset (+ = up), squash: cap width factor."""
    cv = Canvas(32, 16)
    # stem
    cv.part()
    top = 10.5 - h
    cv.poly([(12.5, 15.9), (19.5, 15.9), (18.5, top + 1), (13.5, top + 1)], STEM, shade="grad", base=0.6)
    cv.part()
    cv.ellipse((10, 15), 3.5, 1.2, M.MOSS_M)
    cv.ellipse((23, 15.2), 3.5, 1.1, M.MOSS_M)
    # cap
    cv.part()
    rx, ry = 13.5 * squash, 5.0 / squash
    cap = (cv.px - 16) ** 2 / rx ** 2 + (cv.py - top) ** 2 / ry ** 2 <= 1
    cap &= cv.py <= top + 1.6
    dx, dy = (cv.px - 16) / rx, (cv.py - top) / ry
    nz = np.sqrt(np.clip(1 - dx * dx - dy * dy, 0, 1))
    cv.put(cap, cv.light_from_normal(dx, dy, nz), MUSH)
    # glowing spots
    cv.part(line=False)
    for (sx, sy) in ((-7, -1.5), (-1.5, -3.2), (5, -2), (9.5, 0.2), (-10.5, 0.4)):
        cv.disc((16 + sx * squash, top + sy / squash), 0.9, CRYSTAL_ON, flat=0.85, decal=True)
    # gill rim
    cv.line((16 - rx + 1.5, top + 1.5), (16 + rx - 1.5, top + 1.5), Mat([VIOLET[0]], name="gill"), decal=True)
    return cv.render()


def bounce_pad():
    s = Sheet("sprites/objects/bounce_pad.png", 32, 16, [16, 16])
    s.add("idle", [bounce_frame(0, 1.0)], fps=1, loop=False)
    seq = [bounce_frame(-3, 1.15), bounce_frame(3, 0.86), bounce_frame(-1, 1.06), bounce_frame(1, 0.96),
           bounce_frame(0, 1.0)]
    s.add("bounce", seq, durations=[0.05, 0.07, 0.07, 0.07, 0.08], loop=False, next="idle")
    s.save("animations/bounce_pad.json")


# ------------------------------------------------------- breakable wall
def crystal_wall(cracked):
    """A wall of tall violet crystal prisms (two-facet shading) on a rock base."""
    W, H = 32, 64
    cv = Canvas(W, H)
    light = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1]], name="cw_light", thresholds=[0.8, 0.5])
    dark = Mat([VIOLET[1], VIOLET[0], hex_rgba('#3b1a6e')], name="cw_dark", thresholds=[0.7, 0.4])
    # (centre x, half width, top y, tilt) back row first, then the front row
    prisms = [(6, 5.0, 6, -0.6), (26, 5.0, 4, 0.5), (16, 6.0, 1, 0.0), (10, 4.5, 14, -0.3), (22, 4.5, 11, 0.4)]
    for k, (cx, hw, top, tilt) in enumerate(prisms):
        cv.part()
        tipx = cx + tilt * 3
        pts_l = [(cx - hw, 64), (cx - hw, top + 6), (tipx, top), (cx + 0.5, top + 6), (cx + 0.5, 64)]
        pts_r = [(cx + 0.5, 64), (cx + 0.5, top + 6), (tipx, top), (cx + hw, top + 6), (cx + hw, 64)]
        back = 0.0 if k >= 3 else -0.12
        cv.poly([(cx - hw, 64), (cx - hw, top + 6), (tipx, top), (cx + hw, top + 6), (cx + hw, 64)], light,
                flat=0.6 + back)
        cv.poly(pts_r, dark, flat=0.55 + back, decal=True)
        cv.line((cx - hw + 1.5, top + 8), (cx - hw + 1.5, 60), light, inten=0.95 + back, decal=True)
        cv.line((tipx - 0.5, top + 1.5), (cx - hw + 1.5, top + 7), light, inten=0.95 + back, decal=True)
    # rock base
    cv.part()
    cv.poly([(0.5, 64), (31.5, 64), (31, 57), (24, 55), (16, 57), (8, 55), (1, 58)], M.STONE_M, bevel=1.4, base=0.55)
    cv.ellipse((8, 56), 5, 1.4, M.MOSS_M, decal=True)
    img = cv.render()
    if cracked:
        cr = Canvas(W, H)
        cr.part(line=False)
        crack_mat = Mat([hex_rgba('#ffffff')], emissive=True, name="crack_glow")
        dk = Mat([hex_rgba('#2a0f4a')], name="crack_dark")
        segs = [((16, 32), (11, 22)), ((11, 22), (13, 12)), ((16, 32), (22, 25)), ((22, 25), (25, 15)),
                ((16, 32), (12, 41)), ((12, 41), (8, 49)), ((16, 32), (21, 40)), ((21, 40), (20, 52)),
                ((11, 22), (5, 18)), ((21, 40), (27, 44))]
        for a, b in segs:
            cr.line(a, b, dk)
            cr.line((a[0] + 1, a[1]), (b[0] + 1, b[1]), crack_mat)
        c = cr.render(outline=False)
        sel = (c[:, :, 3] > 0) & (img[:, :, 3] > 0)
        img[sel] = c[sel]
        FX.sparkle(img, 16, 32, 2, WHITE, VIOLET[2])
    return img


def breakable_wall():
    s = Sheet("sprites/objects/breakable_wall.png", 32, 64, [16, 64])
    s.add("intact", [crystal_wall(False)], fps=1, loop=False)
    s.add("cracked", [crystal_wall(True)], fps=1, loop=False)
    s.save("animations/breakable_wall.json")


# ------------------------------------------------------------ phase wall
def phase_wall():
    W, H = 16, 64
    out = []
    for i in range(6):
        img = np.zeros((H, W, 4), np.uint8)
        for y in range(H):
            for x in range(W):
                edge = min(x, W - 1 - x)
                band = (y + i * (64 // 6 // 2 * 2) // 1) % 8
                wav = math.sin((y / 64.0) * TAU * 2 + TAU * i / 6 + x * 0.4)
                if edge == 0:
                    c = (VIOLET[2][0], VIOLET[2][1], VIOLET[2][2], 230)
                elif edge == 1:
                    c = (VIOLET[1][0], VIOLET[1][1], VIOLET[1][2], 170)
                else:
                    a = 70 + int(40 * wav)
                    c = (VIOLET[0][0], VIOLET[0][1], VIOLET[0][2], a)
                    if (y - i * 2) % 11 == 0:
                        c = (VIOLET[2][0], VIOLET[2][1], VIOLET[2][2], 150)
                    if wav > 0.85 and (x + y) % 3 == 0:
                        c = (246, 220, 255, 170)
                img[y, x] = c
        # rising glyph motes
        for k in range(3):
            y = int((50 - i * 6 - k * 21) % 64)
            x = 4 + (k * 5) % 8
            img[y, x] = (246, 220, 255, 230)
        out.append(img)
    s = Sheet("sprites/objects/phase_wall.png", W, H, [8, 64])
    s.add("idle", out, fps=8, loop=True)
    s.save("animations/phase_wall.json")


# ------------------------------------------------------------------ sign
def sign():
    cv = Canvas(24, 24)
    cv.part()
    cv.poly([(9.5, 23.9), (14.5, 23.9), (14, 15), (10, 15)], M.STONE_DARK, bevel=1, base=0.55)
    cv.part()
    cv.poly([(2.5, 16.5), (2, 5), (5, 1.5), (19, 1.5), (22, 5), (21.5, 16.5)], M.STONE_M, bevel=1.6, base=0.6)
    cv.ellipse((6, 2.5), 4, 1.3, M.MOSS_M, decal=True)
    cv.ellipse((20, 15.5), 2.5, 1.2, M.MOSS_M, decal=True)
    cv.part(line=False)
    g = RUNE_ON
    # glowing glyph: eye-of-the-ruins symbol
    cv.line((8, 6), (16, 6), g, decal=True)
    cv.line((12, 6), (12, 13), g, decal=True)
    cv.line((8, 10), (10, 12), g, decal=True)
    cv.line((16, 10), (14, 12), g, decal=True)
    cv.px_set(12, 9, CRYSTAL_ON, inten=1.0, decal=True)
    img = cv.render()
    s = Sheet("sprites/objects/sign.png", 24, 24, [12, 24])
    s.add("idle", [img], fps=1, loop=False)
    s.save("animations/sign.json")


# --------------------------------------------------------- laser emitter
def emitter_frame(level, flick=0):
    cv = Canvas(16, 16)
    cv.part()
    cv.poly([(1.5, 15.9), (12.5, 15.9), (12.5, 3), (10, 1.5), (1.5, 1.5)], M.METAL, bevel=1.4, base=0.58)
    cv.part(line=False)
    cv.line((3, 4), (9, 4), M.STONE_DARK, inten=0.3, decal=True)
    cv.line((3, 13), (9, 13), M.STONE_DARK, inten=0.3, decal=True)
    cv.px_set(4, 9, Mat([AMBER[1]], emissive=True, name="amb_led") if level < 0.5 else RED_LIGHT, inten=0.8,
              decal=True)
    # lens housing on the right
    cv.part()
    cv.poly([(11.5, 4.5), (15.5, 5.5), (15.5, 10.5), (11.5, 11.5)], M.STONE_DARK, bevel=1, base=0.5)
    cv.part(line=False)
    mat = RED_LIGHT if level > 0 else Mat([DANGER[0], hex_rgba('#5a0f1e')], name="lens_off", thresholds=[0.5])
    cv.disc((14.0, 8.0), 1.6 + 0.5 * level, mat, flat=0.35 + 0.6 * level)
    img = cv.render()
    if level >= 1:
        put(img, 15, 8, WHITE)
        if flick % 2 == 0:
            put(img, 15, 7, DANGER[2])
            put(img, 15, 9, DANGER[2])
    return img


def laser_emitter():
    s = Sheet("sprites/objects/laser_emitter.png", 16, 16, [8, 16])
    s.add("off", [emitter_frame(0.0)], fps=1, loop=False)
    s.add("charging", [emitter_frame((i + 1) / 5) for i in range(4)], fps=8, loop=False)
    s.add("on", [emitter_frame(1.0, i) for i in range(4)], fps=16, loop=True)
    s.save("animations/laser_emitter.json")


def build():
    checkpoint()
    exit_portal()
    shard()
    relic()
    orb("health")
    orb("energy")
    power_unlock()
    door()
    lever()
    receptor()
    bounce_pad()
    breakable_wall()
    phase_wall()
    sign()
    laser_emitter()
    player_objects.build()
    objects_static.build()
