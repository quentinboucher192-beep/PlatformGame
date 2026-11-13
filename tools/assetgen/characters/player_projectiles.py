"""v2 projectiles (CONTENT_CONTRACT section 4), appended after the v1 rows of
sprites/projectiles/projectiles.png: 16x16 frames, pivot [8, 8], one looping
4-frame clip per row, drawn travelling to the RIGHT (the engine rotates them
along their velocity)."""
import math

import numpy as np

import effects as FX
from common.palette import ACID, AMBER, CYAN, OUTLINE, STONE, VIOLET, hex_rgba
from raster import Canvas, Mat, put

W = H = 16
TAU = 2 * math.pi
H_ = hex_rgba

# --- local ramps (Pyralis / Glacior colours from the content contract) ------
LAVA = [H_('#fff1c2'), H_('#ffc56b'), H_('#ff8a3d'), H_('#ff4d1e'), H_('#b3201a')]
BASALT = [H_('#5a2b22'), H_('#3a1d1c'), H_('#241215'), H_('#14090b')]
ICE = [H_('#eefaff'), H_('#a8e4ff'), H_('#5fb0ff'), H_('#2f6fb3')]
FROST = H_('#7ff6ff')
NAVY = H_('#13264a')
DTEAL = H_('#0f3b3a')
DRED = H_('#5a0f1e')
DEMBER = H_('#4a1008')
DVIO = H_('#2a0f4a')
DACID = H_('#14380c')
WHITE = H_('#ffffff')

PLASMA = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=DTEAL, name="pp_plasma",
             thresholds=[0.8, 0.58, 0.34])
PLASMA_NO = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=None, name="pp_plasma_no",
                thresholds=[0.8, 0.58, 0.34])
FIRE = Mat([LAVA[0], LAVA[1], LAVA[2], LAVA[3]], emissive=True, outline=DEMBER, name="pp_fire",
           thresholds=[0.8, 0.6, 0.38])
FIRE_TAIL = Mat([LAVA[1], LAVA[2], LAVA[3], LAVA[4]], emissive=True, outline=None, name="pp_fire_tail",
                thresholds=[0.75, 0.5, 0.28])
CRUST = Mat([BASALT[0], BASALT[1], BASALT[2]], outline=OUTLINE, name="pp_crust", thresholds=[0.7, 0.42])
MOLTEN = Mat([LAVA[1], LAVA[2], LAVA[3]], emissive=True, outline=DEMBER, name="pp_molten", thresholds=[0.7, 0.4])
ICE_M = Mat([ICE[0], ICE[1], ICE[2], ICE[3]], outline=NAVY, name="pp_ice", thresholds=[0.82, 0.6, 0.36])
FROST_M = Mat([ICE[0], FROST, ICE[2], ICE[3]], emissive=True, outline=NAVY, name="pp_frost",
              thresholds=[0.84, 0.62, 0.36])
FLAKE = Mat([WHITE], emissive=True, outline=None, name="pp_flake")
BOMB_M = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], outline=OUTLINE, name="pp_bomb",
             thresholds=[0.86, 0.62, 0.36])
BAND = Mat([AMBER[1], AMBER[0], H_('#7a4512')], name="pp_band", thresholds=[0.7, 0.4])
ACID_M = Mat([ACID[3], ACID[2], ACID[1], ACID[0]], emissive=True, outline=DACID, name="pp_acid",
             thresholds=[0.84, 0.6, 0.34])
WEB = Mat([WHITE, H_('#f6dcff'), VIOLET[2], VIOLET[1]], outline=DVIO, name="pp_web", thresholds=[0.8, 0.55, 0.3])
SPORE_B = Mat([H_('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], outline=DVIO, name="pp_spore_big",
              thresholds=[0.86, 0.62, 0.36])
DISC = Mat([H_('#fff4d6'), AMBER[2], AMBER[1], AMBER[0]], emissive=True, outline=H_('#4a2a08'), name="pp_disc",
           thresholds=[0.82, 0.6, 0.36])
ROCK = Mat([H_('#9fb3c8'), STONE[4], STONE[3], STONE[2]], outline=OUTLINE, name="pp_rock",
           thresholds=[0.8, 0.6, 0.38])
HOOK = Mat([H_('#e8eef5'), H_('#c3d0de'), H_('#6c819a'), STONE[2]], outline=OUTLINE, name="pp_hook",
           thresholds=[0.8, 0.56, 0.33])


def _over(img, x, y, c):
    """put only on empty pixels (trails stay behind the projectile)."""
    x, y = int(x), int(y)
    if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
        img[y, x] = c


# ------------------------------------------------------------- charge_shot
def charge_shot():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        wob = math.sin(TAU * i / 4)
        # comet tail (left), flickering length
        cv.part(line=False)
        cv.capsule((9.0, 8.0), (1.0 + (i % 2), 8.0 + wob * 0.6), 4.4, 0.9, PLASMA, flat=0.3)
        cv.capsule((9.0, 8.0), (3.5, 8.0 - wob * 0.4), 3.0, 0.6, PLASMA, flat=0.5)
        cv.part()
        s = 1.0 + 0.05 * math.cos(TAU * i / 4)
        cv.disc((10.0, 8.0), 4.9 * s, PLASMA, bias=0.12)
        cv.part(line=False)
        cv.disc((10.6, 7.4), 2.4, PLASMA_NO, flat=1.0, decal=True)
        # rotating plasma filaments inside
        for k in range(2):
            a = TAU * i / 4 + math.pi * k
            cv.line((10.0, 8.0), (10.0 + math.cos(a) * 3.6, 8.0 + math.sin(a) * 3.6), PLASMA_NO, inten=0.7,
                    decal=True)
        img = cv.render()
        put(img, 11, 7, WHITE)
        # sparks shed by the ball
        for k in range(3):
            a = TAU * (i + k * 1.3) / 4
            _over(img, 3 + ((i + 2 * k) % 4), 8 + int(round(math.sin(a) * 5)), CYAN[2] if k else CYAN[3])
        out.append(img)
    return out


# ---------------------------------------------------------------- fireball
def fireball():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        # flame tongues trailing left, alternating
        cv.part(line=False)
        for k, (dy, ln, r) in enumerate(((-2.2, 8.5, 1.6), (0.0, 10.0, 2.4), (2.2, 8.0, 1.6))):
            fl = ln - 1.5 * ((i + k) % 2)
            cv.capsule((10.0, 8.0 + dy * 0.5), (10.0 - fl, 8.0 + dy + math.sin(TAU * i / 4 + k) * 0.8), r + 1.2,
                       0.5, FIRE_TAIL, flat=0.3 + 0.12 * (k == 1))
        cv.capsule((10.0, 8.0), (4.0, 8.0), 3.3, 1.0, FIRE_TAIL, flat=0.62)
        cv.part()
        cv.disc((10.5, 8.0), 3.9, FIRE, bias=0.1)
        cv.part(line=False)
        cv.disc((11.2, 7.5), 1.9, FIRE, flat=1.0, decal=True)
        img = cv.render()
        put(img, 12, 7, WHITE)
        for k in range(2):
            _over(img, 1 + ((i * 3 + k * 5) % 5), 4 + ((i * 5 + k * 7) % 8), LAVA[1] if k else LAVA[2])
        out.append(img)
    return out


# -------------------------------------------------------------- magma_glob
def magma_glob():
    """Lobbed molten rock: basalt crust with glowing seams, dripping lava."""
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        rot = TAU * i / 8
        cv.part()
        pts = []
        for k in range(9):
            a = rot + TAU * k / 9
            r = 4.6 + 0.7 * math.sin(3 * a + 1.0)
            pts.append((9.0 + math.cos(a) * r, 7.5 + math.sin(a) * r * 0.92))
        cv.poly(pts, CRUST, bevel=1.4, base=0.6)
        # molten seams (rotate with the rock)
        cv.part(line=False)
        for k in range(3):
            a = rot + TAU * k / 3 + 0.3
            p0 = (9.0 + math.cos(a) * 0.6, 7.5 + math.sin(a) * 0.6)
            p1 = (9.0 + math.cos(a) * 3.8, 7.5 + math.sin(a) * 3.5)
            cv.line(p0, p1, MOLTEN, inten=0.6, decal=True)
        cv.disc((9.0, 7.5), 1.8, MOLTEN, flat=0.95, decal=True)
        img = cv.render()
        # drips: falling behind/below (left, down)
        for k, (x, y0) in enumerate(((5, 11), (8, 12), (3, 9))):
            dy = (i + k * 2) % 4
            for j in range(2):
                yy = y0 + dy + j
                _over(img, x - k % 2, yy, LAVA[2] if j == 0 else LAVA[3])
            _over(img, x - k % 2, y0 + dy + 2, DEMBER)
        out.append(img)
    return out


# ------------------------------------------------------------------- ember
def ember():
    out = []
    for i in range(4):
        img = FX.new(W, H)
        y = 8 + [0, -1, 0, 1][i]
        # short flickering streak
        for k, x in enumerate(range(3 + (i % 2), 9)):
            c = LAVA[3] if k < 2 else (LAVA[2] if k < 4 else LAVA[1])
            put(img, x, y + (1 if k < 2 and i % 2 else 0), c)
        FX.sparkle(img, 10, y, 2 if i % 2 == 0 else 1, LAVA[0], LAVA[2])
        put(img, 11, y, LAVA[1])
        # outline-free glow corners
        put(img, 9, y - 1, LAVA[3])
        put(img, 9, y + 1, LAVA[3])
        out.append(img)
    return out


# --------------------------------------------------------------- ice_shard
def ice_shard():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        wob = [0.0, 0.04, 0.0, -0.04][i]
        tip = (15.4, 8.0 + wob * 8)
        # small side crystal behind the main spike
        cv.part()
        cv.poly([(3.0, 8.5), (7.0, 10.0), (9.5, 12.6), (5.0, 11.2)], ICE_M, bevel=0.8, base=0.42)
        cv.part()
        cv.poly([(0.8, 8.0), (5.5, 5.4), tip, (5.5, 10.4)], ICE_M, bevel=1.0, base=0.5)
        # lit upper facet + bright ridge
        cv.part(line=False)
        cv.poly([(0.8, 8.0), (5.5, 5.4), tip, (5.5, 7.9)], ICE_M, flat=0.74, decal=True)
        cv.line((3.0, 7.6), (13.5, 7.9 + wob * 6), ICE_M, inten=1.0, decal=True)
        img = cv.render()
        gx = 4 + 3 * i
        put(img, gx, 7 if gx < 12 else 8, WHITE)
        for k in range(2):
            _over(img, (i + k * 2) % 3, 3 + ((i * 3 + k * 5) % 10), FROST if k else ICE[1])
        out.append(img)
    return out


# --------------------------------------------------------------- frost_orb
def frost_orb():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        cv.part(line=False)
        wob = math.sin(TAU * i / 4)
        cv.capsule((9.0, 8.0), (2.0, 8.0 + wob * 0.8), 3.4, 0.7, FROST_M, flat=0.45)
        cv.capsule((9.0, 8.0), (4.0, 8.0 - wob * 0.5), 2.2, 0.5, FROST_M, flat=0.7)
        cv.part()
        cv.disc((9.5, 8.0), 4.4, FROST_M, flat=0.5)
        cv.part(line=False)
        ring = ((cv.px - 9.5) ** 2 + (cv.py - 8.0) ** 2 > 3.0 ** 2) & ((cv.px - 9.5) ** 2 + (cv.py - 8.0) ** 2 <= 4.4 ** 2)
        lit = (cv.px - 9.5) + (cv.py - 8.0) < 0
        cv.put(ring & lit, 0.7, FROST_M, decal=True)
        cv.put(ring & ~lit, 0.3, FROST_M, decal=True)
        # rotating snowflake
        a0 = TAU * i / 12
        for k in range(3):
            a = a0 + math.pi * k / 3
            p0 = (9.5 - math.cos(a) * 2.6, 8.0 - math.sin(a) * 2.6)
            p1 = (9.5 + math.cos(a) * 2.6, 8.0 + math.sin(a) * 2.6)
            cv.line(p0, p1, FLAKE, decal=True)
        img = cv.render()
        for k in range(3):
            _over(img, ((i + k * 2) % 4), 4 + ((i * 3 + k * 4) % 9), ICE[0] if k == 0 else FROST)
        out.append(img)
    return out


# -------------------------------------------------------------------- bomb
def bomb():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        bob = [0, 0, 1, 1][i] * 0.0
        cv.part()
        cv.disc((8.0, 9.0 + bob), 5.2, BOMB_M)
        # amber hazard band + bolts
        cv.part(line=False)
        band = (np.abs(cv.py - (9.0 + bob) - (cv.px - 8.0) * 0.25) < 0.9) & \
               ((cv.px - 8.0) ** 2 + (cv.py - 9.0 - bob) ** 2 <= 4.6 ** 2)
        cv.put(band, 0.6, BAND, decal=True)
        cv.px_set(5, 7, BOMB_M, inten=1.0, decal=True)
        # fuse cap
        cv.part()
        cv.rect(7, 2.5, 10, 4.5, BOMB_M, bevel=0.8, base=0.4)
        img = cv.render()
        # fuse cord + blinking amber spark
        put(img, 10, 2, H_('#7a4512'))
        put(img, 11, 1, H_('#c8741f'))
        if i % 2 == 0:
            FX.sparkle(img, 12, 1, 1, AMBER[2], AMBER[1])
            put(img, 12, 1, WHITE)
            put(img, 8, 9, AMBER[1])  # warning light on the shell
        else:
            put(img, 12, 1, AMBER[0])
            put(img, 8, 9, H_('#7a4512'))
        out.append(img)
    return out


# --------------------------------------------------------------- acid_glob
def acid_glob():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        sq = 1.0 + 0.1 * math.sin(TAU * i / 4)
        cv.part(line=False)
        cv.capsule((9.0, 8.0), (3.0, 8.0 + 0.6 * math.sin(TAU * i / 4)), 3.0, 0.8, ACID_M, flat=0.4)
        cv.part()
        cv.ellipse((9.5, 8.0), 4.6 * sq, 4.0 / sq, ACID_M, bias=0.05)
        cv.part(line=False)
        cv.disc((10.5, 6.8), 1.3, ACID_M, flat=1.0, decal=True)
        # bubbles inside
        for k in range(2):
            y = 10 - ((i + k * 2) % 4)
            cv.px_set(8 + 2 * k, y, ACID_M, inten=0.95, decal=True)
        img = cv.render()
        # drips falling off the back
        for k, x in enumerate((4, 6)):
            dy = (i + k * 2) % 4
            _over(img, x, 11 + dy, ACID[2])
            _over(img, x, 12 + dy, ACID[1])
        out.append(img)
    return out


# --------------------------------------------------------------------- web
def web():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        c = (8.5, 8.0)
        cv.part()
        cv.disc(c, 3.2, WEB, flat=0.45)
        rot = TAU * i / 16
        cv.part(line=False)
        tips = []
        for k in range(6):
            a = rot + TAU * k / 6
            t = (c[0] + math.cos(a) * 6.4, c[1] + math.sin(a) * 6.0)
            tips.append(t)
            cv.line(c, t, WEB, inten=0.95)
        # concentric strands
        for rr in (2.6, 4.6):
            for k in range(6):
                a0 = rot + TAU * k / 6
                a1 = rot + TAU * (k + 1) / 6
                p0 = (c[0] + math.cos(a0) * rr, c[1] + math.sin(a0) * rr * 0.94)
                p1 = (c[0] + math.cos(a1) * rr, c[1] + math.sin(a1) * rr * 0.94)
                cv.line(p0, p1, WEB, inten=0.6 if rr > 3 else 0.8)
        img = cv.render()
        put(img, 8, 7, WHITE)
        put(img, 9, 8, VIOLET[2])
        out.append(img)
    return out


# --------------------------------------------------------------- spore_big
def spore_big():
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        a0 = TAU * i / 16
        for k in range(8):
            a = a0 + TAU * k / 8
            cv.part(line=False)
            cv.capsule((8.0, 8.0), (8.0 + math.cos(a) * 7.0, 8.0 + math.sin(a) * 7.0), 1.2, 0.5, SPORE_B, flat=0.42)
        cv.part()
        cv.disc((8.0, 8.0), 5.0 + 0.25 * math.sin(TAU * i / 4), SPORE_B)
        cv.part(line=False)
        for k in range(4):
            a = -a0 * 2 + TAU * k / 4
            cv.disc((8.0 + math.cos(a) * 2.6, 8.0 + math.sin(a) * 2.6), 0.8, SPORE_B, flat=0.95, decal=True)
        cv.disc((8.0, 8.0), 1.0, SPORE_B, flat=0.2, decal=True)
        img = cv.render()
        put(img, 6, 5, WHITE)
        out.append(img)
    return out


# ------------------------------------------------------------- energy_disc
def energy_disc():
    """Face-on amber saw disc: toothed rim, dark groove, bright hub; the
    teeth advance 1/4 of their spacing per frame (spins clockwise)."""
    out = []
    n_teeth = 6
    for i in range(4):
        cv = Canvas(W, H)
        c = (8.0, 8.0)
        rot = TAU / n_teeth * i / 4
        cv.part()
        pts = []
        for k in range(n_teeth):
            a = rot + TAU * k / n_teeth
            pts.append((c[0] + math.cos(a) * 5.0, c[1] + math.sin(a) * 5.0))
            a2 = a + TAU / n_teeth * 0.35
            pts.append((c[0] + math.cos(a2) * 7.0, c[1] + math.sin(a2) * 7.0))
            a3 = a + TAU / n_teeth * 0.55
            pts.append((c[0] + math.cos(a3) * 5.6, c[1] + math.sin(a3) * 5.6))
        cv.poly(pts, DISC, bevel=1.2, base=0.55)
        cv.part(line=False)
        d2 = (cv.px - c[0]) ** 2 + (cv.py - c[1]) ** 2
        cv.put((d2 <= 3.7 ** 2) & (d2 > 2.6 ** 2), 0.2, DISC, decal=True)
        cv.disc(c, 2.0, DISC, flat=1.0, decal=True)
        # spokes of light rotating
        for k in range(3):
            a = -rot * 2 + TAU * k / 3
            cv.px_set(c[0] + math.cos(a) * 3.2, c[1] + math.sin(a) * 3.2, DISC, inten=0.95, decal=True)
        img = cv.render()
        put(img, 7, 7, WHITE)
        out.append(img)
    return out


# -------------------------------------------------------------------- rock
def rock():
    out = []
    base = [(-4.6, -1.2), (-2.2, -4.4), (2.4, -4.2), (4.8, -0.8), (3.6, 3.4), (-0.8, 4.6), (-4.0, 2.8)]
    for i in range(4):
        cv = Canvas(W, H)
        a = TAU * i / 4 * 0.5  # tumbles: half a turn over the 4-frame loop (shape is near-symmetric)
        ca, sa = math.cos(a), math.sin(a)
        R = lambda x, y: (9.0 + x * ca - y * sa, 8.0 + x * sa + y * ca)
        pts = [R(x, y) for x, y in base]
        cv.part()
        cv.poly(pts, ROCK, bevel=1.8, base=0.56)
        # a lit facet and a crack that rotate with the chunk
        cv.part(line=False)
        cv.poly([R(-2.2, -4.4), R(2.4, -4.2), R(0.6, -0.6), R(-2.6, -1.0)], ROCK, flat=0.72, decal=True)
        cv.line(R(-1.0, 0.6), R(2.0, 2.2), ROCK, inten=0.1, decal=True)
        img = cv.render()
        for k in range(3):
            _over(img, ((i + k * 2) % 3), 6 + ((i + k * 3) % 6), STONE[2] if k else STONE[3])
        out.append(img)
    return out


# ------------------------------------------------------------ grapple_hook
def grapple_hook():
    """Energy grappling hook: a pointed head with two barbs curving back,
    a glowing cyan core, and the stub of the line behind it."""
    out = []
    for i in range(4):
        cv = Canvas(W, H)
        flick = [0.0, 0.3, 0.5, 0.3][i]
        # barbs (behind the head)
        cv.part()
        for s in (-1, 1):
            p0 = (9.0, 8.0)
            p1 = (6.6, 8.0 + s * 3.9)
            p2 = (8.2, 8.0 + s * 5.6)
            p3 = (10.6, 8.0 + s * 5.0)
            cv.capsule(p0, p1, 1.1, 0.95, HOOK)
            cv.capsule(p1, p2, 0.95, 0.8, HOOK)
            cv.capsule(p2, p3, 0.8, 0.45, HOOK)
        # head: arrow tip
        cv.part()
        cv.poly([(6.0, 6.2), (11.0, 5.6), (15.6, 8.0), (11.0, 10.4), (6.0, 9.8)], HOOK, bevel=1.1, base=0.62)
        # energy core
        cv.part()
        cv.disc((7.5, 8.0), 2.1 + flick * 0.4, PLASMA, bias=0.1)
        img = cv.render()
        put(img, 7, 7, WHITE)
        put(img, 12, 7, CYAN[2] if i % 2 == 0 else CYAN[3])
        for x in range(0, 5):
            _over(img, x, 8, CYAN[3] if (x + i) % 4 else CYAN[2])
            if (x + i) % 2 == 0:
                _over(img, x, 9, CYAN[0])
        out.append(img)
    return out


CLIPS = [
    ("charge_shot", charge_shot, 14),
    ("fireball", fireball, 14),
    ("magma_glob", magma_glob, 10),
    ("ember", ember, 16),
    ("ice_shard", ice_shard, 12),
    ("frost_orb", frost_orb, 12),
    ("bomb", bomb, 8),
    ("acid_glob", acid_glob, 10),
    ("web", web, 10),
    ("spore_big", spore_big, 10),
    ("energy_disc", energy_disc, 16),
    ("rock", rock, 12),
    ("grapple_hook", grapple_hook, 12),
]


def add_rows(sheet):
    for name, fn, fps in CLIPS:
        sheet.add(name, fn(), fps=fps, loop=True)
    return sheet
