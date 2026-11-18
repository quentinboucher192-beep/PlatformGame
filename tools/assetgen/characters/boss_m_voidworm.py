"""VOIDWORM, le ver du vide (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The worm coiled in the titan's abdomen: it rears out of a hole ringed with roots, a column of purple
chitin rings (Asterion's abdomen: violet plates, lime light between them, as in asterion_sprites.png)
ending in a round lamprey head, its maw full of rings of teeth facing right (the game flips it), three
green eyes over it.  It never walks: it sways, rears back, lunges, spits, roars.

Three phase sheets (one per phase of data/enemies/voidworm.json), each with its own light and form:
  * 1 « Le ventre affamé »       sprites/enemies/voidworm.png     violet chitin, lime rings;
  * 2 « Les anneaux de chitine » sprites/enemies/voidworm_p2.png  thick pale plates over every ring
    (what the hooks pin), the light paler;
  * 3 « Le reflux »              sprites/enemies/voidworm_p3.png  chitin split, the green flesh bare,
    acid dripping from its maw, a halo of sparks.
Frames 256x256, pivot [128, 244] = bottom-centre of the 96x136 collision box (x 80..176, y 108..244);
drawn at scale 2.5 in game (240x340 box).  Clips: idle 8, walk 8 (= move), telegraph 6, attack 5,
telegraph_special 6, special 4, summon 6, hurt 2, roar 8, death 14.

Also:
  * its glowing ring (data/enemies/voidworm_anneau.json) and its stomach (voidworm_poche.json):
    32x32 frames, idle 6 / hurt 2 / death 6;
  * its effects (drawn by the raid's combat screen): sprites/enemies/voidworm_fx.png, 48x144 frames:
    closed 1 (a hook planted, its chain taut: the ring pinned) / open 4 (the hook free, glowing) /
    warn 4 (acid bubbling) / jet 4 (an acid geyser) / fist 2 (a maw bursting out of the ground) /
    gulp 4 (its maw closing on a mark).

Deterministic (fixed seeds): `--check` renders everything twice and compares with the files on disk
(tests/test_raid_gigaboss_b.cpp); `--review out.png` writes a labelled contact sheet.
    python3 tools/assetgen/characters/boss_m_voidworm.py [--check | --review out.png]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.path[:0] = [HERE, os.path.dirname(HERE)]

import numpy as np  # noqa: E402

import gigaboss_kit as kit  # noqa: E402
from gigaboss_kit import C, TCanvas, mat, wave  # noqa: E402

W, H = 256, 256
GY = 244
PIVOT = [128, GY]
SEED = 69101

# The light between its rings at each phase (light -> dark): paler and hotter each time.
GLOWS = [
    ("Le ventre affamé", ["#e8ffb0", "#c6ff6a", "#9dff3a", "#6ccf1e", "#3f8f12"]),
    ("Les anneaux de chitine", ["#f4ffd0", "#dcff96", "#b8ff5a", "#86e02a", "#4fa018"]),
    ("Le reflux", ["#ffffff", "#f2ffc8", "#d8ff8a", "#a8ff4a", "#6ccf1e"]),
]
# Asterion's abdomen: violet chitin, its plates paler, its flesh within.
CHITIN = mat("vw_chitin", ["#8a5ad0", "#6a3dab", "#4b2c8a", "#35206b", "#24164f"])
PLATE = mat("vw_plate", ["#bfb0ea", "#9584cc", "#6c5aa8", "#4a3c80", "#2c2454"])
FLESH = mat("vw_flesh", ["#d48ae6", "#ad5fc6", "#83409e", "#5a2a74"])
MAW = mat("vw_maw", ["#4a1238", "#300c26", "#1c0716", "#10040c"])
TEETH = mat("vw_teeth", ["#fff6e0", "#e6d8b8", "#b8a888"])
ROOT = mat("vw_root", ["#7a5a48", "#5a4034", "#3e2a22", "#271913"])
HOLE = mat("vw_hole", ["#1a0c22", "#12081a", "#0a0410"], outline=None)
STEEL = mat("vw_steel", ["#dfe6f0", "#a8b4c8", "#76829c", "#4c5670"])
CHAIN = mat("vw_chain", ["#b8c0d0", "#868ea4", "#5a6078"])


def glow(phase):
    return mat("vw_glow%d" % phase, GLOWS[phase][1], emissive=True, outline=None)


def col(phase, k):
    return C(GLOWS[phase][1][k])


# ----------------------------------------------------------------------------- pose
class Pose:
    """sway: the S of its body (-1..1); rise: how far it rears (0..1.2); lean: its head forward (+) or
    back (-); mouth: its maw open (0..1); tilt: its head up (-) or down (+); glow (-1..1); flash; sink:
    sinking into its hole (death, 0..1); spit: acid thrown from its maw (0..1); bulge: a swelling
    running up its body (0..1, 0 = none); shake (px); drip."""

    def __init__(self, **k):
        self.sway = k.get("sway", 0.0)
        self.rise = k.get("rise", 1.0)
        self.lean = k.get("lean", 0.0)
        self.mouth = k.get("mouth", 0.15)
        self.tilt = k.get("tilt", 0.0)
        self.glow = k.get("glow", 0.0)
        self.flash = k.get("flash", 0.0)
        self.sink = k.get("sink", 0.0)
        self.spit = k.get("spit", 0.0)
        self.bulge = k.get("bulge", 0.0)
        self.shake = k.get("shake", 0.0)
        self.seed = k.get("seed", 0)


def bez(p0, p1, p2, p3, t):
    u = 1 - t
    return (u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
            u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1])


def spine(p):
    """The centre line of the body, from inside its hole to its neck, and its head."""
    sx = p.shake * math.sin(p.seed * 2.1)
    base = (128.0 + sx * 0.3, GY + 8.0)
    hx = 140.0 + p.lean * 34.0 + p.sway * 12.0 + sx
    hy = GY - (56.0 + 130.0 * p.rise) + p.sink * 180.0
    c1 = (128.0 - 44.0 * p.sway, GY - 80.0 * p.rise + p.sink * 80.0)
    c2 = (hx - 46.0 + 26.0 * p.sway, hy + 56.0)
    pts = [bez(base, c1, c2, (hx, hy + 30.0), i / 10.0) for i in range(11)]
    return pts, (hx, hy)


def draw_worm(p, phase):
    g = glow(phase)
    cv = TCanvas(W, H)
    # Its hole, ringed with roots.
    cv.part(line=False)
    cv.tellipse((128, GY + 1), 58.0, 10.0, HOLE)
    for k, (a, b, r) in enumerate((((76, GY + 2), (52, GY - 4), 5.0), ((84, GY - 2), (66, GY - 16), 4.0), ((176, GY + 2), (204, GY - 3), 5.0),
                                   ((170, GY - 2), (188, GY - 18), 4.0), ((100, GY + 3), (92, GY + 7), 5.0), ((156, GY + 3), (166, GY + 7), 5.0))):
        cv.part()
        cv.tcapsule(a, b, r, r * 0.45, ROOT)
    pts, head = spine(p)
    # The rings, from the hole up to the neck: a band of light under each chitin ring.
    n = len(pts) - 1
    for i in range(n):
        t = i / (n - 1)
        a, b = pts[i], pts[i + 1]
        if a[1] > GY + 12 and b[1] > GY + 12:
            continue
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        r = 31.0 - 7.0 * t
        bul = 0.0
        if p.bulge > 0:
            bul = 9.0 * math.exp(-((t - p.bulge) ** 2) / 0.012)
        c = (a[0] * 0.5 + b[0] * 0.5, a[1] * 0.5 + b[1] * 0.5)
        cv.part(line=False)
        cv.tellipse(c, r + 3.0 + bul, (r + 3.0 + bul) * 0.86, g, ang=ang + math.pi / 2, bias=0.1 + 0.25 * p.glow)
        cv.part()
        off = (math.cos(ang) * 3.0, math.sin(ang) * 3.0)
        cv.tellipse((c[0] + off[0], c[1] + off[1]), r + bul, (r + bul) * 0.84, CHITIN, ang=ang + math.pi / 2)
        if phase == 1:  # thick pale plates over every ring (what the hooks pin)
            cv.tellipse((c[0] + off[0] - 3, c[1] + off[1] - 3), (r + bul) * 0.9, (r + bul) * 0.62, PLATE, ang=ang + math.pi / 2, decal=True)
            cv.tline((c[0] - r * 0.7, c[1] + 2), (c[0] + r * 0.7, c[1] + 2), CHITIN, inten=0.2, decal=True)
        elif phase == 2:  # chitin split: the green flesh bare in the cracks
            cv.tellipse((c[0] + off[0] + 3, c[1] + off[1] + 2), (r + bul) * 0.42, (r + bul) * 0.28, FLESH, ang=ang, decal=True)
            cv.tline((c[0] - r * 0.5, c[1] - 2), (c[0] + r * 0.4, c[1] + 4), g, inten=0.9, decal=True)
        else:
            cv.tellipse((c[0] + off[0] - 5, c[1] + off[1] - 6), (r + bul) * 0.4, (r + bul) * 0.22, PLATE, ang=ang, decal=True, bias=0.1)
    # The head: a round lamprey head, horns back, three eyes, the maw facing right.
    hx, hy = head
    rot = p.tilt
    cr, sr = math.cos(rot), math.sin(rot)

    def hp(dx, dy):
        return (hx + dx * cr - dy * sr, hy + dx * sr + dy * cr)

    for k, (a, b) in enumerate((((-18, -22), (-46, -40)), ((-26, -8), (-58, -14)), ((-20, 10), (-48, 18)))):
        cv.part()
        cv.tcapsule(hp(*a), hp(*b), 7.0 - k, 2.0, PLATE if phase == 1 else CHITIN)
    cv.part()
    cv.tellipse(hp(0, 0), 40.0, 36.0, CHITIN, ang=rot)
    if phase == 1:
        cv.tellipse(hp(-8, -12), 26.0, 16.0, PLATE, ang=rot - 0.3, decal=True)
    if phase == 2:
        for a, b in (((-20, -18), (4, -4)), ((-12, 14), (10, 2)), ((-30, 2), (-12, -6))):
            cv.tline(hp(*a), hp(*b), g, inten=0.95, decal=True)
    # The maw.
    m = max(0.0, min(1.0, p.mouth))
    mc = hp(22 + 4 * m, 4)
    mrx, mry = 12.0 + 9.0 * m, 9.0 + 17.0 * m
    cv.part()
    cv.tellipse(mc, mrx + 5, mry + 5, FLESH, ang=rot)
    cv.part(line=False)
    cv.tellipse(mc, mrx, mry, MAW, ang=rot)
    cv.tellipse((mc[0] - 2, mc[1]), max(2.0, mrx * 0.35), max(2.0, mry * 0.35), g, ang=rot, bias=0.2 + 0.3 * m)
    for ring, rr in ((0, 0.92), (1, 0.62)):
        cnt = 12 - 4 * ring
        for k in range(cnt):
            a = k / cnt * 2 * math.pi + ring * 0.4
            tip = (mc[0] + math.cos(a) * mrx * rr * 0.55, mc[1] + math.sin(a) * mry * rr * 0.55)
            root = (mc[0] + math.cos(a) * mrx * rr, mc[1] + math.sin(a) * mry * rr)
            cv.part(line=False)
            cv.tcapsule(root, tip, 1.8 - 0.4 * ring, 0.6, TEETH)
    for e in ((-2, -24), (10, -26), (20, -18)):
        cv.part(line=False)
        cv.tdisc(hp(*e), 3.2, g, bias=0.3 + 0.3 * p.glow)
    img = cv.render(outline=True)
    return post(img, p, phase, mc)


def post(img, p, phase, mc):
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 1000)
    # Sinking (death): nothing below the rim of its hole.
    if p.sink > 0:
        img[GY + 9:, :] = 0
    if p.mouth > 0.3 or phase == 2:  # acid dripping from its maw
        for k in range(3 + 3 * phase):
            x = int(mc[0] + rng.uniform(-10, 10))
            y = int(mc[1] + 8 + rng.uniform(0, 30 + 12 * phase))
            kit.blob(img, x, y, 1 + int(rng.uniform(0, 2)), col(phase, 1 + k % 3))
    if p.spit > 0:  # a gob of acid thrown forward
        for k in range(int(10 * p.spit)):
            x = int(mc[0] + 12 + rng.uniform(0, 60) * p.spit)
            y = int(mc[1] + rng.uniform(-14, 14))
            kit.blob(img, x, y, 1 + int(rng.uniform(0, 3)), col(phase, k % 4))
    if phase == 2:
        kit.halo(img, col(phase, 1), rng, 1.0, 70)
    if p.glow > 0.5:
        kit.halo(img, col(phase, 2), rng, p.glow - 0.3, 40)
    if p.sink > 0.05:  # dirt thrown up by the sinking body
        for k in range(int(30 * p.sink)):
            kit.blob(img, int(128 + rng.uniform(-60, 60)), int(GY - rng.uniform(0, 26)), 1, C("#5a4034"))
    if p.glow < 0:
        kit.tint(img, (40, 30, 60, 255), min(0.6, -p.glow * 0.5))
    if p.flash:
        kit.flash(img, p.flash)
    return img


# ----------------------------------------------------------------------------- clips
def idle():
    return [Pose(sway=wave(i / 8, 0.55), lean=wave(i / 8 + 0.25, 0.15), mouth=0.15 + 0.08 * wave(i / 8), glow=wave(i / 8, 0.4), seed=i) for i in range(8)]


def walk():
    return [Pose(sway=wave(i / 8, 0.9), lean=wave(i / 8 + 0.25, 0.25), rise=0.95 + 0.05 * wave(i / 8), mouth=0.2, seed=10 + i) for i in range(8)]


def telegraph():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(lean=-0.7 * k, rise=1.0 + 0.1 * k, mouth=0.15 + 0.4 * k, glow=0.6 * k, tilt=-0.25 * k, seed=20 + i) for i, k in enumerate(ks)]


def attack():
    leans, mouths, spits = [0.2, 0.8, 1.0, 0.7, 0.3], [0.6, 1.0, 1.0, 0.7, 0.3], [0.0, 0.8, 1.0, 0.3, 0.0]
    return [Pose(lean=leans[i], mouth=mouths[i], spit=spits[i], tilt=0.15, glow=0.4, seed=30 + i) for i in range(5)]


def telegraph_special():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(rise=1.0 + 0.14 * k, mouth=0.2 + 0.8 * k, tilt=-0.5 * k, glow=k, seed=40 + i) for i, k in enumerate(ks)]


def special():
    return [Pose(rise=1.14, mouth=0.9 + 0.1 * wave(i / 4), tilt=-0.5, glow=1.0, shake=2.0 * (1 if i % 2 else -1), seed=50 + i) for i in range(4)]


def summon():
    return [Pose(bulge=0.15 + 0.17 * i, mouth=0.2 + (0.7 if i >= 4 else 0.0), spit=0.6 if i == 5 else 0.0, glow=0.3, seed=60 + i) for i in range(6)]


def hurt():
    return [Pose(lean=-0.25, flash=0.65, mouth=0.5, seed=70), Pose(lean=-0.1, flash=0.25, mouth=0.3, seed=71)]


def roar():
    return [Pose(mouth=min(1.0, 0.3 + 0.25 * i), tilt=-0.45, rise=1.08, glow=0.9, shake=3.0 * (1 if i % 2 else -1), seed=80 + i) for i in range(8)]


def death():
    out = []
    for i in range(14):
        t = i / 13
        out.append(Pose(sink=max(0.0, (t - 0.25) / 0.75), mouth=0.8 - 0.5 * t, tilt=-0.4 + 0.6 * t, glow=0.8 - 1.8 * t,
                        flash=0.6 if i == 0 else 0.0, shake=2.0 if i < 4 else 0.0, seed=90 + i))
    return out


CLIPS = [
    ("idle", idle, dict(fps=7, loop=True)),
    ("telegraph", telegraph, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("walk", walk, dict(fps=7, loop=True)),
    ("attack", attack, dict(durations=[0.05, 0.06, 0.18, 0.12, 0.14], loop=False, events=[(2, "sfx:burrow")])),
    ("telegraph_special", telegraph_special, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("special", special, dict(fps=12, loop=True)),
    ("hurt", hurt, dict(durations=[0.08, 0.14], loop=False)),
    ("summon", summon, dict(durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)),
    ("roar", roar, dict(durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)),
    ("death", death, dict(durations=[0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3], loop=False)),
]


def phase_sheet(phase):
    clips = [(name, [draw_worm(p, phase) for p in fn()], kw) for name, fn, kw in CLIPS]
    return kit.pack(clips, W, H)


# ----------------------------------------------------------------------------- its targets
def target_frames(kind):
    """Its glowing ring (« anneau ») or its stomach (« poche »): idle 6 / hurt 2 / death 6, 32x32."""
    fw = fh = 32
    g = glow(2)

    def ring(pulse, crack=0.0, scale=1.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tellipse((16, 16), 12.0 * scale, 10.0 * scale, CHITIN)
        cv.part(line=False)
        cv.tellipse((16, 16), 8.0 * scale, 6.0 * scale, g, bias=0.1 + 0.3 * pulse)
        cv.tellipse((15, 15), 3.0 * scale, 2.5 * scale, mat("vw_core", ["#ffffff", "#f2ffc8"], emissive=True, outline=None), bias=0.3 * pulse)
        if crack:
            cv.tline((9, 10), (20, 22), MAW, inten=0.3, decal=True)
        return cv.render(outline=True)

    def sac(pulse, crack=0.0, scale=1.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tellipse((16, 17), 12.5 * scale * (1 + 0.04 * pulse), 13.0 * scale, FLESH)
        cv.part(line=False)
        cv.tellipse((16, 17), 9.5 * scale, 10.0 * scale, g, bias=-0.2 + 0.2 * pulse)
        cv.part(line=False)
        cv.tellipse((15, 18), 4.0 * scale, 5.5 * scale, MAW)  # a shape curled up inside
        cv.tdisc((15, 12), 2.6 * scale, MAW)
        for a in range(4):
            ang = a * math.pi / 2 + 0.6
            cv.tline((16 + math.cos(ang) * 6, 17 + math.sin(ang) * 6), (16 + math.cos(ang) * 11, 17 + math.sin(ang) * 11), CHITIN, inten=0.4, decal=True)
        if crack:
            cv.tline((8, 12), (22, 24), TEETH, inten=0.6, decal=True)
        return cv.render(outline=True)

    draw = ring if kind == "anneau" else sac
    idle_f = [draw(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [kit.flash(draw(1.0, 1.0), 0.6), draw(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = draw(1.0, 1.0, 1.0 + 0.12 * i)
        img = kit.fade(img, 1.0 - i / 6)
        rng = np.random.RandomState(SEED + 300 + i + (50 if kind == "poche" else 0))
        for k in range(4 + 3 * i):
            ang, d = rng.uniform(0, 2 * math.pi), 4 + 2.2 * i + rng.uniform(0, 4)
            kit.blob(img, int(16 + math.cos(ang) * d), int(16 + math.sin(ang) * d), 1, col(2, 1 + k % 3))
        death_f.append(img)
    clips = [("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=[0.08, 0.12], loop=False)),
             ("death", death_f, dict(fps=12, loop=False))]
    return kit.pack(clips, fw, fh, columns=6)


# ----------------------------------------------------------------------------- its effects
def fx_frames():
    VW, VH = kit.FX_W, kit.FX_H
    g = glow(0)
    ramp = [col(0, k) for k in range(5)]

    def hook(planted, pulse=0.0, seed=0):
        cv = TCanvas(VW, VH)
        cv.part(line=False)
        cv.tellipse((24, VH - 4), 16.0, 4.0, HOLE)
        base = VH - 6 if planted else VH - 14 - 4 * pulse
        # Its chain up to the worm (taut when pinned).
        for k in range(10):
            y = base - 40 - 9 * k
            if y < 2:
                break
            cv.part()
            cv.tellipse((24 + (0 if planted else 2 * math.sin(k + seed)), y), 3.0, 4.6, CHAIN)
        cv.part()
        cv.tcapsule((24, base - 44), (24, base - 6), 3.4, 3.0, STEEL)
        cv.part()
        cv.tpoly([(24, base + 4), (14, base - 12), (24, base - 7), (34, base - 12)], STEEL)
        if not planted:
            cv.part(line=False)
            cv.tellipse((24, VH - 6), 12.0 + 2 * pulse, 3.0, g, bias=0.1 + 0.3 * pulse)
        img = cv.render(outline=True)
        if planted:
            rng = np.random.RandomState(SEED + 500)
            for _ in range(8):
                kit.blob(img, int(24 + rng.uniform(-14, 14)), int(VH - 4 - rng.uniform(0, 6)), 1, C("#5a4034"))
        return img

    def bubbles(i):
        img = hook(False, 0.5, i)
        rng = np.random.RandomState(SEED + 520 + i)
        for _ in range(10):
            kit.blob(img, int(rng.uniform(10, 38)), int(VH - 6 - rng.uniform(0, 16)), 1 + int(rng.uniform(0, 2)), ramp[1 + int(rng.uniform(0, 3))])
        return img

    def geyser(i):
        img = np.zeros((VH, VW, 4), np.uint8)
        rng = np.random.RandomState(SEED + 540 + i)
        top = 8 + 6 * (i % 2)
        for y in range(top, VH):
            t = (y - top) / (VH - top)
            half = 4 + 7 * t + 2 * math.sin(y * 0.4 + i)
            for x in range(int(24 - half), int(24 + half) + 1):
                d = abs(x - 24) / max(1.0, half)
                if 0 <= x < VW:
                    img[y, x] = ramp[0] if d < 0.3 else ramp[1] if d < 0.65 else ramp[2]
        for _ in range(14):
            kit.blob(img, int(rng.uniform(6, 42)), int(rng.uniform(2, top + 14)), 1 + int(rng.uniform(0, 2)), ramp[1 + int(rng.uniform(0, 3))])
        return img

    def maw(open_, y0, seed):
        """A maw of the worm bursting out of the ground, open toward the sky."""
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tcapsule((24, VH), (24, y0 + 8), 15.0, 13.0, CHITIN)
        cv.part()
        cv.tellipse((24, y0), 17.0, 9.0 + 4 * open_, FLESH)
        cv.part(line=False)
        cv.tellipse((24, y0 + 1), 12.0, 4.0 + 4 * open_, MAW)
        for k in range(10):
            a = k / 10 * 2 * math.pi
            cv.part(line=False)
            cv.tcapsule((24 + math.cos(a) * 12, y0 + 1 + math.sin(a) * (4 + 4 * open_)), (24 + math.cos(a) * 7, y0 + 1 + math.sin(a) * (2 + 2 * open_)),
                        1.6, 0.5, TEETH)
        cv.part(line=False)
        for r in range(3):
            cv.tellipse((24, y0 + 22 + 16 * r), 15.5, 2.0, g, bias=0.2)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 560 + seed)
        for _ in range(16):
            kit.blob(img, int(24 + rng.uniform(-22, 22)), int(VH - rng.uniform(0, 30)), 1 + int(rng.uniform(0, 2)), C("#5a4034"))
        return img

    def gulp(k):
        """Its maw closing on a mark, seen from the side: two jaws of teeth closing."""
        img = np.zeros((VH, VW, 4), np.uint8)
        cv = TCanvas(VW, VH)
        gap = 30 - 9 * k
        for side in (-1, 1):
            cv.part()
            y = VH - 50 + side * gap * 0.5
            cv.tellipse((24, y), 22.0, 9.0, FLESH)
            for t in range(6):
                x = 6 + t * 7.2
                cv.part(line=False)
                cv.tpoly([(x - 3, y - side * 4), (x + 3, y - side * 4), (x, y - side * 12)], TEETH)
        img = cv.render(outline=True)
        return img

    clips = [("closed", [hook(True)], dict(fps=1, loop=True)),
             ("open", [hook(False, 0.5 + 0.5 * math.sin(i / 4 * 2 * math.pi), i) for i in range(4)], dict(fps=6, loop=True)),
             ("warn", [bubbles(i) for i in range(4)], dict(fps=10, loop=True)),
             ("jet", [geyser(i) for i in range(4)], dict(fps=12, loop=True)),
             ("fist", [maw(0.4, 60, 0), maw(1.0, 40, 1)], dict(fps=10, loop=True)),
             ("gulp", [gulp(k) for k in range(4)], dict(fps=8, loop=True))]
    return kit.pack(clips, VW, VH, columns=8)


# ----------------------------------------------------------------------------- output
def outputs():
    out = []
    for k in range(3):
        img, meta = phase_sheet(k)
        name = "voidworm" if k == 0 else "voidworm_p%d" % (k + 1)
        out += kit.sheet_entry(name, img, meta, W, H, PIVOT)
    for kind in ("anneau", "poche"):
        img, meta = target_frames(kind)
        out += kit.sheet_entry("voidworm_" + kind, img, meta, 32, 32, [16, 28])
    img, meta = fx_frames()
    out += kit.sheet_entry("voidworm_fx", img, meta, kit.FX_W, kit.FX_H, [kit.FX_W // 2, kit.FX_H])
    return out


def review(path):
    picks = [("Repos", idle, 0), ("Préparation", telegraph, 4), ("Morsure (attaque)", attack, 2), ("Cri du gouffre", telegraph_special, 5),
             ("Aspiration", special, 1), ("Appel des parasites", summon, 4), ("Rugissement de phase", roar, 4), ("Touché", hurt, 0),
             ("Mort (il s'enfonce)", death, 8)]
    cells = []
    for k in range(3):
        for label, fn, i in picks:
            cells.append(("Phase %d · %s" % (k + 1, label), draw_worm(fn()[i], k)))
    fx, meta = fx_frames()
    fw, fh = kit.FX_W, kit.FX_H
    extras = []
    for kind, label in (("anneau", "Anneau lumineux (point faible) : repos, touché, brisé"), ("poche", "Poche (libère l'avalé) : repos, touché, crevée")):
        img, m = target_frames(kind)
        extras.append((label, kit.strip(kit.frames_of(img, m, 32, 32, "idle")[:2] + kit.frames_of(img, m, 32, 32, "hurt")[:1] +
                                         kit.frames_of(img, m, 32, 32, "death")[2:4]), 3))
    for clip, label in (("closed", "Crochet planté (anneau épinglé)"), ("open", "Crochet libre"), ("warn", "Acide qui bouillonne"),
                        ("jet", "Geyser d'acide"), ("fist", "Fouissement (sous chaque joueur)"), ("gulp", "Gueule qui avale")):
        extras.append((label, kit.strip(kit.frames_of(fx, meta, fw, fh, clip)), 1))
    return kit.contact(path, "VOIDWORM, le ver du vide — trois phases, ses cibles et ses effets", cells, extras, cell=(W, H), per=9)


if __name__ == "__main__":
    kit.main(sys.argv, outputs, review, "voidworm", "boss_m_voidworm.py")
