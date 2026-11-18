"""GRAVITON, la jambe qui plie l'espace (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The titan's left leg, torn off at the hip and held up by its own gravity generators: a leg of dark
violet armour (Asterion's leg in asterion_sprites.png) with silver trims, its knee bent, its foot
pointing down at nothing; cyan generators glow at its hip, knee and ankle, rings of light orbit its
knee, chunks of ruin orbit the whole leg (in front and behind), a field of light under its foot.  Side
view facing right (the game flips it).  It floats: it bobs, kicks down, opens its rings, roars.

Three phase sheets (one per phase of data/enemies/graviton.json):
  * 1 « Le champ de gravité »  sprites/enemies/graviton.png     cyan generators, one ring;
  * 2 « Les générateurs »      sprites/enemies/graviton_p2.png  the generators blazing, three rings,
    shield plates of light on the thigh;
  * 3 « Le monde à l'envers »  sprites/enemies/graviton_p3.png  armour cracked, the light white and
    violet, its debris orbiting above it (gravity turned over).
Frames 224x256, pivot [112, 240] = bottom-centre of the 84x140 collision box (x 70..154, y 100..240);
drawn at scale 2.5 in game (210x350 box).  Clips: idle 8, walk 8 (= move), telegraph 6, attack 5,
telegraph_special 6, special 4, summon 6, hurt 2, roar 8, death 14.

Also:
  * its gravity core (data/enemies/graviton_coeur.json): 32x32 frames, idle 6 / hurt 2 / death 6;
  * its effects (drawn by the raid's combat screen): sprites/enemies/graviton_fx.png, 48x144 frames:
    gen_off 1 / gen_on 4 / gen_held 4 (a gravity generator: dark, running, held by a player) /
    fist 2 (a chunk of debris falling on a mark) / well 4 (a well opening) / closed, open, warn, jet
    (the generator again: the combat screen's plates).

Deterministic (fixed seeds): `--check` renders everything twice and compares with the files on disk
(tests/test_raid_gigaboss_b.cpp); `--review out.png` writes a labelled contact sheet.
    python3 tools/assetgen/characters/boss_m_graviton.py [--check | --review out.png]
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

W, H = 224, 256
GY = 240
PIVOT = [112, GY]
SEED = 69202

GLOWS = [
    ("Le champ de gravité", ["#e6ffff", "#aaf8ff", "#7ff6ff", "#3fc8e0", "#1f8aa8"]),
    ("Les générateurs", ["#ffffff", "#ccfcff", "#9ff8ff", "#5ad8f0", "#2a9cc0"]),
    ("Le monde à l'envers", ["#ffffff", "#f0e8ff", "#d0c0ff", "#a088ff", "#7058d8"]),
]
ARMOR = mat("gv_armor", ["#6a5ab8", "#4b3c94", "#35286e", "#24194f", "#150e33"])
TRIM = mat("gv_trim", ["#d6e0f6", "#a2b0d6", "#7080ac", "#465280"])
ROCK = mat("gv_rock", ["#9a90a8", "#706884", "#4e4860", "#2e2a3c"])
DARK = mat("gv_dark", ["#2c2448", "#1c1630", "#100c1e"])


def glow(phase):
    return mat("gv_glow%d" % phase, GLOWS[phase][1], emissive=True, outline=None)


def col(phase, k):
    return C(GLOWS[phase][1][k])


class Pose:
    """dy: its bob; tilt (radians); knee: how far its knee bends (0..1); kick: its foot thrust down
    (0..1); ring: the turn of its rings and debris (0..1); spread: its rings opened wide (0..1); glow;
    flash; fall (death 0..1); gather: its debris drawn close (0..1); shake."""

    def __init__(self, **k):
        self.dy = k.get("dy", 0.0)
        self.tilt = k.get("tilt", 0.0)
        self.knee = k.get("knee", 0.3)
        self.kick = k.get("kick", 0.0)
        self.ring = k.get("ring", 0.0)
        self.spread = k.get("spread", 0.0)
        self.glow = k.get("glow", 0.0)
        self.flash = k.get("flash", 0.0)
        self.fall = k.get("fall", 0.0)
        self.gather = k.get("gather", 0.0)
        self.shake = k.get("shake", 0.0)
        self.seed = k.get("seed", 0)


def joints(p):
    dy = p.dy + p.fall * 40.0 + p.shake * math.sin(p.seed * 1.7)
    hip = (102.0, 64.0 + dy)
    knee = (128.0 + 6 * p.kick - 6 * p.knee, 140.0 + dy + 8 * p.kick)
    ankle = (112.0 - 14 * p.knee + 8 * p.kick, 202.0 + dy + 14 * p.kick)
    toe = (ankle[0] + 26 + 6 * p.kick, ankle[1] + 26 - 6 * p.knee)
    return hip, knee, ankle, toe


def orbit_pts(p, phase, front):
    """The debris around it: (centre, size, seed) of those in front (or behind)."""
    out = []
    n = 6
    for k in range(n):
        a = (p.ring + k / n) * 2 * math.pi
        s = math.sin(a)
        if (s >= 0) != front:
            continue
        r = (92 - 40 * p.gather) * (1.0 + 0.1 * math.sin(k * 2.3))
        cy = 150.0 + p.dy + p.fall * 60.0
        if phase == 2:
            cy -= 100  # its debris orbiting above it: gravity turned over
        out.append(((112 + math.cos(a) * r, cy + s * r * 0.28 + (8 * k % 3)), 9 + (k * 5) % 6, k))
    return out


def chunk(cv, c, size, k, phase, g):
    pts = []
    for j in range(5):
        a = j / 5 * 2 * math.pi + k
        rr = size * (0.75 + 0.35 * math.sin(j * 1.9 + k))
        pts.append((c[0] + math.cos(a) * rr, c[1] + math.sin(a) * rr))
    cv.part()
    cv.tpoly(pts, ROCK)
    cv.tellipse((c[0], c[1] + size * 0.5), size * 0.7, 1.6, g, decal=True, bias=0.2)


def ring(cv, center, rx, ry, ang, g, front, step):
    for j in range(28):
        a = j / 28 * 2 * math.pi + step * 2 * math.pi
        if (math.sin(a) >= 0) != front:
            continue
        x, y = math.cos(a) * rx, math.sin(a) * ry
        ca, sa = math.cos(ang), math.sin(ang)
        cv.part(line=False)
        cv.tdisc((center[0] + x * ca - y * sa, center[1] + x * sa + y * ca), 2.2, g, bias=0.2)


def draw_leg(p, phase):
    g = glow(phase)
    hip, knee, ankle, toe = joints(p)
    tilt = p.tilt + p.fall * 0.5
    cv = TCanvas(W, H, rot=tilt, center=(112.0, 150.0))
    rings = [(knee, 46 + 26 * p.spread, 11 + 6 * p.spread, 0.35)]
    if phase >= 1:
        rings += [(hip, 40 + 20 * p.spread, 9 + 5 * p.spread, -0.25), (ankle, 34 + 18 * p.spread, 8 + 4 * p.spread, 0.5)]
    for c, s, k in orbit_pts(p, phase, False):
        chunk(cv, c, s, k, phase, g)
    for c, rx, ry, a in rings:
        ring(cv, c, rx, ry, a, g, False, p.ring)
    # The leg: thigh, knee, shin, foot.
    cv.part()
    cv.tcapsule(hip, knee, 30.0, 23.0, ARMOR)
    cv.tellipse(((hip[0] + knee[0]) / 2 - 6, (hip[1] + knee[1]) / 2 - 4), 16.0, 30.0, TRIM, ang=-0.3, decal=True, bias=-0.1)
    if phase == 1:  # shield plates of light on the thigh
        for j in range(3):
            t = 0.25 + 0.25 * j
            c = (hip[0] + (knee[0] - hip[0]) * t + 10, hip[1] + (knee[1] - hip[1]) * t)
            cv.tellipse(c, 7.0, 4.0, g, decal=True, bias=0.1)
    cv.part()
    cv.tcapsule(knee, ankle, 22.0, 15.0, ARMOR)
    cv.tcapsule((knee[0] + 8, knee[1] + 8), (ankle[0] + 8, ankle[1] - 4), 6.0, 4.0, TRIM, decal=True)
    cv.part()
    cv.tcapsule(ankle, toe, 15.0, 8.0, ARMOR)
    cv.tcapsule((ankle[0] + 4, ankle[1] + 8), (toe[0], toe[1] + 3), 5.0, 3.0, TRIM, decal=True)
    cv.part()
    cv.tdisc(knee, 19.0, ARMOR)
    cv.tdisc(knee, 13.0, TRIM, decal=True)
    # The generators (hip stump, knee, ankle).
    for c, r in ((hip, 15.0), (knee, 8.5), (ankle, 7.0)):
        cv.part(line=False)
        cv.tdisc(c, r + 2.0, DARK)
        cv.tdisc(c, r, g, bias=0.15 + 0.3 * p.glow)
    if phase == 2:
        for a, b in (((hip[0] - 14, hip[1] + 20), (hip[0] + 10, hip[1] + 52)), ((knee[0] - 10, knee[1] + 18), (knee[0] + 2, knee[1] + 46)),
                     ((hip[0] + 16, hip[1] + 12), (hip[0] + 4, hip[1] + 34))):
            cv.tline(a, b, g, inten=0.95, decal=True)
    for c, rx, ry, a in rings:
        ring(cv, c, rx, ry, a, g, True, p.ring)
    for c, s, k in orbit_pts(p, phase, True):
        chunk(cv, c, s, k, phase, g)
    img = cv.render(outline=True)
    return post(img, p, phase, toe)


def post(img, p, phase, toe):
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 1000)
    # The field of light under its foot.
    if p.fall < 0.6:
        cx = int(112 + (toe[0] - 112) * 0.7)
        for k in range(30):
            a, d = rng.uniform(0, 2 * math.pi), rng.uniform(4, 20)
            x, y = int(cx + math.cos(a) * d * 1.6), int(min(GY - 2, toe[1] + 12 + math.sin(a) * d * 0.4))
            if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
                img[y, x] = col(phase, 1 + k % 3)
    if phase == 2 or p.glow > 0.5:
        kit.halo(img, col(phase, 1), rng, 1.0 if phase == 2 else p.glow - 0.3, 60)
    if p.glow < 0:
        kit.tint(img, (30, 26, 50, 255), min(0.6, -p.glow * 0.5))
    if p.flash:
        kit.flash(img, p.flash)
    return img


def idle():
    return [Pose(dy=wave(i / 8, 3.0), ring=i / 8, glow=wave(i / 8, 0.4), seed=i) for i in range(8)]


def walk():
    return [Pose(dy=wave(i / 8, 2.0), tilt=0.12, knee=0.45, ring=i / 8, seed=10 + i) for i in range(8)]


def telegraph():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(dy=-14 * k, knee=0.3 + 0.6 * k, glow=0.7 * k, ring=0.1 * i, tilt=-0.1 * k, seed=20 + i) for i, k in enumerate(ks)]


def attack():
    kicks = [0.3, 0.9, 1.0, 0.6, 0.2]
    return [Pose(kick=kicks[i], knee=0.1, glow=0.5, ring=0.2 + 0.05 * i, tilt=0.08, seed=30 + i) for i in range(5)]


def telegraph_special():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(spread=k, glow=k, ring=0.15 * i, dy=-6 * k, seed=40 + i) for i, k in enumerate(ks)]


def special():
    return [Pose(spread=1.0, glow=1.0, ring=0.25 * i, shake=2.0 * (1 if i % 2 else -1), seed=50 + i) for i in range(4)]


def summon():
    return [Pose(gather=min(1.0, i / 4), ring=0.12 * i, glow=0.3 + 0.1 * i, seed=60 + i) for i in range(6)]


def hurt():
    return [Pose(dy=4, flash=0.65, tilt=-0.08, seed=70), Pose(dy=2, flash=0.25, seed=71)]


def roar():
    return [Pose(spread=0.6, glow=0.9, ring=0.1 * i, shake=3.0 * (1 if i % 2 else -1), knee=0.6, seed=80 + i) for i in range(8)]


def death():
    return [Pose(fall=min(1.0, i / 10), glow=0.8 - 1.8 * i / 13, ring=0.05 * i, spread=0.3 * (1 - i / 13), flash=0.6 if i == 0 else 0.0,
                 shake=2.0 if i < 4 else 0.0, seed=90 + i) for i in range(14)]


CLIPS = [
    ("idle", idle, dict(fps=7, loop=True)),
    ("telegraph", telegraph, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("walk", walk, dict(fps=7, loop=True)),
    ("attack", attack, dict(durations=[0.05, 0.06, 0.18, 0.12, 0.14], loop=False, events=[(2, "sfx:gravity_well")])),
    ("telegraph_special", telegraph_special, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("special", special, dict(fps=12, loop=True)),
    ("hurt", hurt, dict(durations=[0.08, 0.14], loop=False)),
    ("summon", summon, dict(durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)),
    ("roar", roar, dict(durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)),
    ("death", death, dict(durations=[0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3], loop=False)),
]


def phase_sheet(phase):
    clips = [(name, [draw_leg(p, phase) for p in fn()], kw) for name, fn, kw in CLIPS]
    return kit.pack(clips, W, H)


def core_frames():
    """Its gravity core, bare when it collapses: a sphere of light in a cage of trims, a ring around."""
    fw = fh = 32
    g = glow(1)

    def sphere(pulse, crack=0.0, scale=1.0):
        cv = TCanvas(fw, fh)
        cv.part(line=False)
        cv.tellipse((16, 16), 14.0 * scale, 4.0 * scale, g, ang=0.3 + pulse * 0.4, bias=-0.1)
        cv.part()
        cv.tdisc((16, 16), 9.0 * scale, TRIM)
        cv.part(line=False)
        cv.tdisc((16, 16), 6.5 * scale, g, bias=0.15 + 0.3 * pulse)
        cv.tdisc((14, 14), 2.5 * scale, mat("gv_core", ["#ffffff", "#e6ffff"], emissive=True, outline=None))
        if crack:
            cv.tline((10, 10), (21, 22), DARK, inten=0.3, decal=True)
        return cv.render(outline=True)

    idle_f = [sphere(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [kit.flash(sphere(1.0, 1.0), 0.6), sphere(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = kit.fade(sphere(1.0, 1.0, 1.0 + 0.15 * i), 1.0 - i / 6)
        rng = np.random.RandomState(SEED + 300 + i)
        for k in range(4 + 3 * i):
            a, d = rng.uniform(0, 2 * math.pi), 4 + 2.2 * i + rng.uniform(0, 4)
            kit.blob(img, int(16 + math.cos(a) * d), int(16 + math.sin(a) * d), 1, col(1, 1 + k % 3))
        death_f.append(img)
    clips = [("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=[0.08, 0.12], loop=False)),
             ("death", death_f, dict(fps=12, loop=False))]
    return kit.pack(clips, fw, fh, columns=6)


def fx_frames():
    VW, VH = kit.FX_W, kit.FX_H

    def generator(state, pulse=0.0, seed=0):
        """A gravity generator: a pylon of ruin stone, its crystal dark, running or held."""
        g = glow(0) if state != "held" else mat("gv_held", ["#fff6dc", "#ffd27a", "#ffb050", "#d2801a"], emissive=True, outline=None)
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tpoly([(10, VH), (38, VH), (33, VH - 30), (15, VH - 30)], ARMOR)
        cv.tpoly([(12, VH - 4), (36, VH - 4), (34, VH - 10), (14, VH - 10)], TRIM, decal=True)
        cv.part()
        c = (24, VH - 46 - 3 * pulse)
        cv.tpoly([(c[0], c[1] - 16), (c[0] + 9, c[1]), (c[0], c[1] + 14), (c[0] - 9, c[1])], DARK if state == "off" else g,
                 bias=0.1 + 0.3 * pulse if state != "off" else 0.0)
        if state != "off":
            for j in range(3):
                ring(cv, c, 16 + 3 * pulse, 4, 0.2, g, True, pulse + j / 3)
        img = cv.render(outline=True)
        if state != "off":
            rng = np.random.RandomState(SEED + 400 + seed + (20 if state == "held" else 0))
            ramp = GLOWS[0][1] if state == "on" else ["#fff6dc", "#ffd27a", "#ffb050", "#d2801a"]
            for k in range(10):
                kit.blob(img, int(24 + rng.uniform(-6, 6)), int(VH - 70 - rng.uniform(0, 60)), 1, C(ramp[k % 4]))
        return img

    def debris(i):
        cv = TCanvas(VW, VH)
        chunk(cv, (24, VH - 22), 15, 3 + i, 0, glow(0))
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 450 + i)
        for y in range(10, VH - 40, 4):
            t = (y - 10) / (VH - 50)
            kit.blob(img, int(24 + rng.uniform(-10 * t - 2, 10 * t + 2)), y, 1, col(0, 3 - min(3, int(t * 4))))
        return img

    def well(i):
        img = np.zeros((VH, VW, 4), np.uint8)
        for k in range(40):
            a = k / 40 * 6 * math.pi + i * 0.8
            d = 22 - k * 0.5
            x, y = int(24 + math.cos(a) * d), int(VH - 14 + math.sin(a) * d * 0.35)
            kit.blob(img, x, y, 1, col(2, k % 4))
        kit.blob(img, 24, VH - 14, 3, C("#2c2448"))
        return img

    on = [generator("on", 0.5 + 0.5 * math.sin(i / 4 * 2 * math.pi), i) for i in range(4)]
    held = [generator("held", 0.5 + 0.5 * math.sin(i / 4 * 2 * math.pi), i) for i in range(4)]
    off = [generator("off")]
    clips = [("gen_off", off, dict(fps=1, loop=True)), ("gen_on", on, dict(fps=8, loop=True)), ("gen_held", held, dict(fps=10, loop=True)),
             ("fist", [debris(i) for i in range(2)], dict(fps=10, loop=True)), ("well", [well(i) for i in range(4)], dict(fps=10, loop=True)),
             ("closed", off, dict(fps=1, loop=True)), ("open", on, dict(fps=8, loop=True)), ("warn", held, dict(fps=10, loop=True)),
             ("jet", on, dict(fps=12, loop=True))]
    return kit.pack(clips, VW, VH, columns=9)


def outputs():
    out = []
    for k in range(3):
        img, meta = phase_sheet(k)
        out += kit.sheet_entry("graviton" if k == 0 else "graviton_p%d" % (k + 1), img, meta, W, H, PIVOT)
    img, meta = core_frames()
    out += kit.sheet_entry("graviton_coeur", img, meta, 32, 32, [16, 28])
    img, meta = fx_frames()
    out += kit.sheet_entry("graviton_fx", img, meta, kit.FX_W, kit.FX_H, [kit.FX_W // 2, kit.FX_H])
    return out


def review(path):
    picks = [("Repos", idle, 0), ("Préparation", telegraph, 4), ("Coup de pied (attaque)", attack, 2), ("Anneaux ouverts", telegraph_special, 5),
             ("Onde gravitationnelle", special, 1), ("Débris rassemblés", summon, 4), ("Rugissement de phase", roar, 4), ("Touché", hurt, 0),
             ("Mort (elle tombe)", death, 9)]
    cells = []
    for k in range(3):
        for label, fn, i in picks:
            cells.append(("Phase %d · %s" % (k + 1, label), draw_leg(fn()[i], k)))
    fx, meta = fx_frames()
    fw, fh = kit.FX_W, kit.FX_H
    img, m = core_frames()
    extras = [("Cœur de gravité (point faible) : repos, touché, brisé",
               kit.strip(kit.frames_of(img, m, 32, 32, "idle")[:2] + kit.frames_of(img, m, 32, 32, "hurt")[:1] + kit.frames_of(img, m, 32, 32, "death")[2:4]), 3)]
    for clip, label in (("gen_off", "Générateur éteint"), ("gen_on", "Générateur en marche"), ("gen_held", "Générateur tenu par un joueur"),
                        ("fist", "Chute de débris (sur chaque joueur)"), ("well", "Puits de gravité")):
        extras.append((label, kit.strip(kit.frames_of(fx, meta, fw, fh, clip)), 1))
    return kit.contact(path, "GRAVITON, la jambe qui plie l'espace — trois phases, ses cibles et ses effets", cells, extras, cell=(W, H), per=9)


if __name__ == "__main__":
    kit.main(sys.argv, outputs, review, "graviton", "boss_m_graviton.py")
