"""TITANUS, la jambe qui écrase tout (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The titan's right leg, a colossus of its forge: iron armour under brass plates (Asterion's right leg
in asterion_sprites.png: dark blue iron, gold plates), torn off at the hip where its forge glows, a
great brass knee, a greave, two restraint cuffs riveted at its ankle, a huge sabaton flat on the
floor (toes right; the game flips it).  It walks with the golem's moves (game/enemies/
GolemAttacks.cpp): it raises its heel and stomps, crouches and leaps (« Bond du colosse »), lands,
shakes the floor (« Séisme du talon »), charges.

Three phase sheets (one per phase of data/enemies/titanus.json):
  * 1 « Le pas du colosse »  sprites/enemies/titanus.png     orange forge in its seams;
  * 2 « Les entraves »       sprites/enemies/titanus_p2.png  its plates locked, its ankle restraints
    blazing gold (what the players break together);
  * 3 « La forge à nu »      sprites/enemies/titanus_p3.png  plates torn off, the white-hot forge bare,
    sparks.
Frames 256x256, pivot [128, 244] = bottom-centre of the 88x150 collision box (x 84..172, y 94..244);
drawn at scale 2.8 in game (246x420 box).  Clips: idle 8, walk 8 (= move, footsteps f0 f4),
telegraph 6, attack 5 (`impact` f2), telegraph_special 6, special 4, summon 6, hurt 2, roar 8,
death 14, crouch 4, leap 3, fall 2, land 5 (`impact` f0), quake 4.

Also:
  * its ankle restraint (data/enemies/titanus_entrave.json) and its kneecap and seams
    (titanus_genou.json): 32x32 frames, idle 6 / hurt 2 / death 6;
  * its effects (drawn by the raid's combat screen): sprites/enemies/titanus_fx.png, 48x144 frames:
    fist 2 (its heel falling on a mark) / wave 4 (a shockwave's crest along the floor) / closed,
    open, warn, jet (a forge vent: the combat screen's plates).

Deterministic (fixed seeds): `--check` renders everything twice and compares with the files on disk
(tests/test_raid_gigaboss_b.cpp); `--review out.png` writes a labelled contact sheet.
    python3 tools/assetgen/characters/boss_m_titanus.py [--check | --review out.png]
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
SEED = 69303

FORGES = [
    ("Le pas du colosse", ["#ffe6a8", "#ffb050", "#ff7a2e", "#d2481a", "#8e2210"]),
    ("Les entraves", ["#fff2c8", "#ffd27a", "#ffa040", "#ff7a2e", "#d2481a"]),
    ("La forge à nu", ["#ffffff", "#fff6dc", "#ffe6a8", "#ffb050", "#ff7a2e"]),
]
IRON = mat("tt_iron", ["#6a7090", "#4c5272", "#363a56", "#24263c", "#141626"])
BRASS = mat("tt_brass", ["#ffe6a0", "#e0b45a", "#b8863a", "#8a5a26", "#5a3618"])
SOOT = mat("tt_soot", ["#3a3440", "#2a2430", "#1a1620"])
GOLD = mat("tt_gold", ["#fffbe0", "#ffe68a", "#ffc94a", "#e09a20"], emissive=True, outline=None)
DUST = [C("#b8a898"), C("#8a7c70"), C("#655a52")]


def forge(phase):
    return mat("tt_forge%d" % phase, FORGES[phase][1], emissive=True, outline=None)


def col(phase, k):
    return C(FORGES[phase][1][k])


class Pose:
    """lift: its foot raised (px); knee: its knee bent (0..1); crouch: the whole leg lowered (0..1);
    air: off the floor (px, a leap); tilt; glow; flash; dust (0..1, an impact); kneel (death 0..1);
    shake (px)."""

    def __init__(self, **k):
        self.lift = k.get("lift", 0.0)
        self.knee = k.get("knee", 0.2)
        self.crouch = k.get("crouch", 0.0)
        self.air = k.get("air", 0.0)
        self.tilt = k.get("tilt", 0.0)
        self.glow = k.get("glow", 0.0)
        self.flash = k.get("flash", 0.0)
        self.dust = k.get("dust", 0.0)
        self.kneel = k.get("kneel", 0.0)
        self.shake = k.get("shake", 0.0)
        self.seed = k.get("seed", 0)


def draw_leg(p, phase):
    f = forge(phase)
    sx = p.shake * math.sin(p.seed * 2.3)
    up = p.air
    ankle = (120.0 + sx, GY - 34.0 - p.lift - up + p.kneel * 6)
    knee = (140.0 + 16.0 * p.knee + sx, 146.0 - p.lift * 0.55 - up + p.crouch * 24 + p.kneel * 76)
    hip = (124.0 + sx - p.kneel * 20, 52.0 - up + p.crouch * 40 + p.kneel * 120)
    cv = TCanvas(W, H, rot=p.tilt, center=(128.0, float(GY)))
    # The forge at its hip, torn off.
    cv.part(line=False)
    cv.tellipse((hip[0], hip[1] - 8), 30.0, 12.0, f, bias=0.1 + 0.3 * p.glow)
    # Thigh.
    cv.part()
    cv.tcapsule(hip, knee, 34.0, 27.0, IRON)
    if phase < 2:
        cv.tellipse(((hip[0] + knee[0]) / 2 + 12, (hip[1] + knee[1]) / 2 - 4), 11.0, 36.0, BRASS, ang=-0.15, decal=True)
        for j in range(3):
            t = 0.25 + 0.25 * j
            c = (hip[0] + (knee[0] - hip[0]) * t - 6, hip[1] + (knee[1] - hip[1]) * t)
            cv.tdisc((c[0] - 12, c[1]), 2.0, SOOT, decal=True)
    else:
        cv.tellipse(((hip[0] + knee[0]) / 2 - 2, (hip[1] + knee[1]) / 2), 14.0, 24.0, f, ang=-0.3, decal=True, bias=0.1)
    cv.tline((hip[0] + 24, hip[1] + 8), (knee[0] + 18, knee[1] - 12), f, inten=0.9, decal=True)
    # Shin and greave.
    cv.part()
    cv.tcapsule(knee, ankle, 26.0, 19.0, IRON)
    if phase < 2:
        cv.tcapsule((knee[0] + 6, knee[1] + 14), (ankle[0] + 8, ankle[1] - 16), 12.0, 9.0, BRASS, decal=True)
    else:
        cv.tcapsule((knee[0] + 4, knee[1] + 16), (ankle[0] + 6, ankle[1] - 18), 7.0, 5.0, f, decal=True, bias=0.2)
    # The knee.
    cv.part()
    cv.tellipse((knee[0] + 6, knee[1]), 18.0, 21.0, BRASS)
    cv.part(line=False)
    cv.tellipse((knee[0] + 6, knee[1]), 4.0, 11.0, f, bias=0.2 + 0.3 * p.glow)
    for a in range(6):
        ang = a / 6 * 2 * math.pi
        cv.tdisc((knee[0] + 6 + math.cos(ang) * 13, knee[1] + math.sin(ang) * 16), 1.8, SOOT, decal=True)
    # The sabaton.
    a = ankle
    cv.part()
    cv.tpoly([(a[0] - 28, a[1] + 2), (a[0] + 8, a[1] - 10), (a[0] + 52, a[1] + 8), (a[0] + 66, a[1] + 24), (a[0] + 66, a[1] + 30),
              (a[0] - 30, a[1] + 30)], IRON)
    for k in range(3):
        cv.tpoly([(a[0] + 18 + 15 * k, a[1] + 2 + 4 * k), (a[0] + 30 + 15 * k, a[1] + 8 + 4 * k), (a[0] + 28 + 15 * k, a[1] + 30),
                  (a[0] + 16 + 15 * k, a[1] + 30)], BRASS, decal=True)
    cv.tline((a[0] - 28, a[1] + 29), (a[0] + 66, a[1] + 29), SOOT, inten=0.2, decal=True)
    # Its two ankle restraints.
    for k, dy in enumerate((-30.0, -18.0)):
        cv.part()
        cv.tellipse((a[0] + 2, a[1] + dy), 21.0, 4.5, GOLD if phase == 1 else BRASS, bias=0.15 if phase == 1 else 0.0)
        cv.tdisc((a[0] - 12, a[1] + dy), 1.6, SOOT, decal=True)
        cv.tdisc((a[0] + 16, a[1] + dy), 1.6, SOOT, decal=True)
    img = cv.render(outline=True)
    return post(img, p, phase, ankle, knee)


def post(img, p, phase, ankle, knee):
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 1000)
    if p.dust > 0:
        for k in range(int(40 * p.dust)):
            x = int(128 + rng.uniform(-110, 110) * p.dust)
            y = int(GY - rng.uniform(0, 30) * p.dust)
            kit.blob(img, x, y, 1 + int(rng.uniform(0, 2)), DUST[k % 3])
    if phase == 2 or p.glow > 0.5:  # sparks off its forge
        for k in range(30 if phase == 2 else int(30 * (p.glow - 0.3))):
            x = int(knee[0] + rng.uniform(-50, 50))
            y = int(knee[1] + rng.uniform(-120, 40))
            if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
                img[y, x] = col(phase, 1 + k % 3)
    if phase == 1:
        kit.halo(img, col(1, 1), rng, 0.4, 40)
    if p.glow < 0:
        kit.tint(img, (40, 34, 40, 255), min(0.6, -p.glow * 0.5))
    if p.flash:
        kit.flash(img, p.flash)
    return img


def idle():
    return [Pose(knee=0.2 + 0.05 * wave(i / 8), crouch=0.04 * (1 + wave(i / 8)), glow=wave(i / 8, 0.4), seed=i) for i in range(8)]


def walk():
    out = []
    for i in range(8):
        t = i / 8
        out.append(Pose(lift=max(0.0, wave(t, 28.0)), knee=0.2 + max(0.0, wave(t, 0.5)), tilt=wave(t, 0.04), dust=0.4 if i in (0, 4) else 0.0,
                        seed=10 + i))
    return out


def telegraph():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(lift=64 * k, knee=0.2 + 0.7 * k, glow=0.6 * k, tilt=-0.06 * k, seed=20 + i) for i, k in enumerate(ks)]


def attack():
    lifts, dusts = [44, 10, 0, 0, 0], [0.0, 0.0, 1.0, 0.7, 0.3]
    return [Pose(lift=lifts[i], knee=0.3, dust=dusts[i], glow=0.4, shake=3.0 if i == 2 else 0.0, seed=30 + i) for i in range(5)]


def telegraph_special():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(crouch=0.5 * k, knee=0.2 + 0.4 * k, glow=k, seed=40 + i) for i, k in enumerate(ks)]


def special():
    return [Pose(crouch=0.5, knee=0.6, glow=1.0, shake=2.0 * (1 if i % 2 else -1), seed=50 + i) for i in range(4)]


def summon():
    return [Pose(lift=[30, 50, 0, 0, 30, 0][i], dust=[0, 0, 1.0, 0.5, 0, 1.0][i], knee=0.4, glow=0.5, seed=60 + i) for i in range(6)]


def hurt():
    return [Pose(tilt=-0.06, flash=0.65, seed=70), Pose(tilt=-0.03, flash=0.25, seed=71)]


def roar():
    return [Pose(glow=0.9, crouch=0.2, knee=0.4, shake=3.0 * (1 if i % 2 else -1), seed=80 + i) for i in range(8)]


def death():
    return [Pose(kneel=min(1.0, i / 9), tilt=-0.25 * max(0.0, (i - 8) / 5), glow=0.8 - 1.8 * i / 13, flash=0.6 if i == 0 else 0.0,
                 dust=0.6 if i in (9, 10) else 0.0, seed=90 + i) for i in range(14)]


def crouch():
    return [Pose(crouch=k, knee=0.2 + 0.5 * k, glow=0.3 * k, seed=100 + i) for i, k in enumerate((0.25, 0.5, 0.75, 1.0))]


def leap():
    return [Pose(air=a, knee=0.1, crouch=0.2 * (2 - i), seed=110 + i) for i, a in enumerate((14, 30, 46))]


def fall():
    return [Pose(air=44 - 14 * i, knee=0.3, lift=10, seed=120 + i) for i in range(2)]


def land():
    return [Pose(crouch=c, knee=0.4, dust=d, shake=2.0 if i == 0 else 0.0, seed=130 + i)
            for i, (c, d) in enumerate(((0.8, 1.0), (0.6, 0.8), (0.4, 0.5), (0.2, 0.3), (0.05, 0.1)))]


def quake():
    return [Pose(crouch=0.2, knee=0.4, dust=0.5, shake=3.0 * (1 if i % 2 else -1), glow=0.6, seed=140 + i) for i in range(4)]


CLIPS = [
    ("idle", idle, dict(fps=6, loop=True)),
    ("telegraph", telegraph, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("walk", walk, dict(fps=7, loop=True, events=[(0, "footstep"), (4, "footstep")])),
    ("attack", attack, dict(durations=[0.05, 0.06, 0.18, 0.12, 0.14], loop=False, events=[(2, "impact")])),
    ("telegraph_special", telegraph_special, dict(durations=[0.1, 0.1, 0.1, 0.12, 0.14, 0.2], loop=False)),
    ("special", special, dict(fps=12, loop=True)),
    ("hurt", hurt, dict(durations=[0.08, 0.14], loop=False)),
    ("summon", summon, dict(durations=[0.12, 0.1, 0.12, 0.12, 0.14, 0.16], loop=False)),
    ("roar", roar, dict(durations=[0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16], loop=False)),
    ("death", death, dict(durations=[0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3], loop=False)),
    ("crouch", crouch, dict(durations=[0.08, 0.1, 0.12, 0.16], loop=False)),
    ("leap", leap, dict(durations=[0.08, 0.1, 0.12], loop=False)),
    ("fall", fall, dict(fps=10, loop=True)),
    ("land", land, dict(durations=[0.08, 0.1, 0.1, 0.12, 0.14], loop=False, events=[(0, "impact")])),
    ("quake", quake, dict(fps=8, loop=True)),
]


def phase_sheet(phase):
    clips = [(name, [draw_leg(p, phase) for p in fn()], kw) for name, fn, kw in CLIPS]
    return kit.pack(clips, W, H)


def target_frames(kind):
    """Its ankle restraint (« entrave »: a riveted brass cuff, its chain) or its kneecap (« genou »: a
    plate of the knee, its molten seam): idle 6 / hurt 2 / death 6, 32x32."""
    fw = fh = 32
    f = forge(1)

    def cuff(pulse, crack=0.0, scale=1.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tellipse((16, 16), 13.0 * scale, 8.0 * scale, GOLD, bias=0.1 * pulse)
        cv.part()
        cv.tellipse((16, 16), 8.0 * scale, 4.0 * scale, IRON)
        for x in (6, 16, 26):
            cv.tdisc((x, 12), 1.6, SOOT, decal=True)
        cv.part()
        for k in range(3):
            cv.tellipse((16 + (k - 1) * 0.0, 25 + 3 * k), 2.6, 1.8, BRASS)
        if crack:
            cv.tline((8, 10), (22, 22), f, inten=0.9, decal=True)
        return cv.render(outline=True)

    def kneecap(pulse, crack=0.0, scale=1.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tdisc((16, 16), 12.0 * scale, BRASS)
        cv.part(line=False)
        cv.tpoly([(16, 7), (19, 16), (16, 25), (13, 16)], f, bias=0.15 + 0.3 * pulse)
        cv.tdisc((16, 16), 3.0 * scale, mat("tt_core", ["#ffffff", "#fff6dc"], emissive=True, outline=None))
        if crack:
            cv.tline((9, 11), (22, 21), SOOT, inten=0.3, decal=True)
        return cv.render(outline=True)

    draw = cuff if kind == "entrave" else kneecap
    idle_f = [draw(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [kit.flash(draw(1.0, 1.0), 0.6), draw(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = kit.fade(draw(1.0, 1.0, 1.0 + 0.12 * i), 1.0 - i / 6)
        rng = np.random.RandomState(SEED + 300 + i + (50 if kind == "genou" else 0))
        for k in range(4 + 3 * i):
            a, d = rng.uniform(0, 2 * math.pi), 4 + 2.2 * i + rng.uniform(0, 4)
            kit.blob(img, int(16 + math.cos(a) * d), int(16 + math.sin(a) * d), 1, col(1, 1 + k % 3) if k % 2 else C("#b8863a"))
        death_f.append(img)
    clips = [("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=[0.08, 0.12], loop=False)),
             ("death", death_f, dict(fps=12, loop=False))]
    return kit.pack(clips, fw, fh, columns=6)


def fx_frames():
    VW, VH = kit.FX_W, kit.FX_H
    ramp = [col(0, k) for k in range(5)]

    def heel(i):
        """Its heel falling on a mark: the sabaton's sole seen from the side, motion streaks above."""
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tpoly([(2, VH - 30), (46, VH - 30), (46, VH - 6), (2, VH - 6)], IRON)
        cv.tpoly([(4, VH - 12), (44, VH - 12), (44, VH - 6), (4, VH - 6)], BRASS, decal=True)
        cv.part()
        cv.tcapsule((24, VH - 30), (24, VH - 74), 13.0, 15.0, IRON)
        cv.tellipse((24, VH - 46), 15.0, 4.0, BRASS, decal=True)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 600 + i)
        for y in range(4, VH - 80, 3):
            kit.blob(img, int(24 + rng.uniform(-18, 18)), y, 1, ramp[2 + (y // 3) % 3])
        return img

    def crest(i):
        """A shockwave running along the floor: an arc of dust and forge light."""
        img = np.zeros((VH, VW, 4), np.uint8)
        rng = np.random.RandomState(SEED + 620 + i)
        hgt = 46 + 8 * (i % 2)
        for x in range(VW):
            t = x / (VW - 1)
            top = int(VH - hgt * math.sin(t * math.pi) ** 0.8)
            for y in range(top, VH):
                d = (y - top) / max(1, VH - top)
                img[y, x] = ramp[1] if d < 0.2 else ramp[2] if d < 0.5 else DUST[0]
        for _ in range(26):
            kit.blob(img, int(rng.uniform(0, VW)), int(VH - hgt - rng.uniform(0, 30)), 1 + int(rng.uniform(0, 2)), DUST[int(rng.uniform(0, 3))])
        return img

    def vent(glow_, seed=0):
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tpoly([(8, VH), (40, VH), (36, VH - 14), (12, VH - 14)], IRON)
        cv.part(line=False)
        cv.tellipse((24, VH - 14), 11.0, 3.0, forge(0), bias=-0.3 + 0.5 * glow_)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 640 + seed)
        for _ in range(int(10 * glow_)):
            kit.blob(img, int(24 + rng.uniform(-8, 8)), int(VH - 18 - rng.uniform(0, 30 * glow_)), 1, ramp[1 + int(rng.uniform(0, 3))])
        return img

    clips = [("fist", [heel(i) for i in range(2)], dict(fps=10, loop=True)), ("wave", [crest(i) for i in range(4)], dict(fps=12, loop=True)),
             ("closed", [vent(0.0)], dict(fps=1, loop=True)), ("open", [vent(0.4, i) for i in range(4)], dict(fps=6, loop=True)),
             ("warn", [vent(0.8, 10 + i) for i in range(4)], dict(fps=10, loop=True)), ("jet", [vent(1.0, 20 + i) for i in range(4)], dict(fps=12, loop=True))]
    return kit.pack(clips, VW, VH, columns=8)


def outputs():
    out = []
    for k in range(3):
        img, meta = phase_sheet(k)
        out += kit.sheet_entry("titanus" if k == 0 else "titanus_p%d" % (k + 1), img, meta, W, H, PIVOT)
    for kind in ("entrave", "genou"):
        img, meta = target_frames(kind)
        out += kit.sheet_entry("titanus_" + kind, img, meta, 32, 32, [16, 28])
    img, meta = fx_frames()
    out += kit.sheet_entry("titanus_fx", img, meta, kit.FX_W, kit.FX_H, [kit.FX_W // 2, kit.FX_H])
    return out


def review(path):
    picks = [("Repos", idle, 0), ("Marche", walk, 2), ("Talon levé", telegraph, 5), ("Piétinement (impact)", attack, 2),
             ("Charge (préparation)", telegraph_special, 5), ("Rugissement de phase", roar, 3), ("Accroupi (bond)", crouch, 3),
             ("Bond du colosse", leap, 2), ("Atterrissage", land, 0), ("Séisme du talon", quake, 1), ("Touché", hurt, 0), ("Mort (à genou)", death, 11)]
    cells = []
    for k in range(3):
        for label, fn, i in picks:
            cells.append(("Phase %d · %s" % (k + 1, label), draw_leg(fn()[i], k)))
    fx, meta = fx_frames()
    fw, fh = kit.FX_W, kit.FX_H
    extras = []
    for kind, label in (("entrave", "Entrave de cheville : repos, touchée, brisée"), ("genou", "Rotule (point faible) : repos, touchée, brisée")):
        img, m = target_frames(kind)
        extras.append((label, kit.strip(kit.frames_of(img, m, 32, 32, "idle")[:2] + kit.frames_of(img, m, 32, 32, "hurt")[:1] +
                                         kit.frames_of(img, m, 32, 32, "death")[2:4]), 3))
    for clip, label in (("fist", "Écrasement (son talon sur chaque joueur)"), ("wave", "Onde de choc (à sauter)")):
        extras.append((label, kit.strip(kit.frames_of(fx, meta, fw, fh, clip)), 1))
    return kit.contact(path, "TITANUS, la jambe qui écrase tout — trois phases, ses cibles et ses effets", cells, extras, cell=(W, H), per=12)


if __name__ == "__main__":
    kit.main(sys.argv, outputs, review, "titanus", "boss_m_titanus.py")
