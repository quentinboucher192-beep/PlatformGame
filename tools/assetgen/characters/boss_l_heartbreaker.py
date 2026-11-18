"""HEARTBREAKER, la cage qui garde le cœur (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The armoured rib cage of the titan's TORSE, as drawn on Asterion (environment/raid_asterion.py,
part_torse): the titan's violet skin with molten seams, a gorget and pectoral plates, a ribcage of
bone around the cavity where Asterion's heart burns.  Front view (symmetric: the game may flip it).
Three phase sheets, its look following its health:
  * 1 « La cuirasse »          sprites/enemies/heartbreaker.png     four plate mounts (its armour plates
    are its weak points, drawn over them), the heart dim behind closed ribs;
  * 2 « La cage thoracique »   sprites/enemies/heartbreaker_p2.png  mounts torn off and glowing, the ribs
    shut tight like a trap over a brighter heart (the players pry them open);
  * 3 « Le cœur affolé »       sprites/enemies/heartbreaker_p3.png  the ribs broken, the heart bare and
    racing, white-hot seams, a halo of heat.
Frames 224x224, pivot [112, 212] = bottom-centre of the 136x128 collision box (scale 2.5: 340x320).
Clips: idle 6 (a heartbeat), move 6, telegraph 5, attack 5, telegraph_special 5, special 4, summon 5,
hurt 2, roar 6, death 10.

Also its weak points (an armour plate: heartbreaker_plaque, the light of its heart:
heartbreaker_coeur, 48x48: idle 6, hurt 2, death 6) and the sheet of its own rules (heartbreaker_fx,
64x128: rib 1, rib_held 4, rib_open 2 = its ribs pried from the flank towers, trap 4 = a rib rising
around a player, wave 4 = the wave of a strong beat), drawn by the raid's combat screen.

    python3 tools/assetgen/characters/boss_l_heartbreaker.py [--check | --review out.png]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.path[:0] = [HERE, os.path.dirname(HERE)]

import numpy as np  # noqa: E402

from boss_a_common import C, mat  # noqa: E402
from boss_l_gigaboss import GY, Pose, T, _blob, boss_outputs, flash, halo, main, pack, review, sparks, trim, wave  # noqa: E402
from raster import TCanvas  # noqa: E402

W = H = 224
SEED = 69301
SKIN = mat("hb_skin", ["#8f58c9", "#6a3dab", "#4b2c8a", "#35206b", "#24164f"])
SKIN_DK = mat("hb_skin_dk", ["#4b2c8a", "#35206b", "#24164f", "#170f36"])
BONE = mat("hb_bone", ["#fbf7ff", "#e2dbea", "#b8aec6", "#8a7c9c", "#5e5272"])
ARMOR = mat("hb_armor", ["#8ea3c6", "#6177a0", "#46587f", "#323f63", "#232d4b"])
CAV = mat("hb_cav", ["#963218", "#5e1c18", "#341016", "#1a070c"])
PHASES = [
    ("La cuirasse", ["#ff9a6a", "#ff5d4f", "#c8301a", "#6e1408"]),
    ("La cage thoracique", ["#ffd0a0", "#ff8a6a", "#ff5d8f", "#c8401a"]),
    ("Le cœur affolé", ["#ffffff", "#ffd0e0", "#ff8aa8", "#ff5d8f"]),
]
SEAM = [mat("hb_seam%d" % k, ["#ffe6a8", "#ffb050", "#ff7a2e", "#d2481a"] if k < 2 else ["#ffffff", "#fff6dc", "#ffe6a8", "#ffb050"],
            emissive=True, outline=None) for k in range(3)]
HALO = [C("#ff7a2e"), C("#ff5d8f"), C("#ffe8a8")]

REST = (112.0, 140.0)  # the middle of its cavity (the heart: world [0, -170] from its feet)
# Its plates (weak points of phase 1, world from its feet: [-150, -250] [150, -250] [-160, -80] [160, -80]).
PLATES = [(-60.0, -40.0), (60.0, -40.0), (-64.0, 28.0), (64.0, 28.0)]


def heart_mat(phase):
    return mat("hb_heart%d" % phase, PHASES[phase][1], emissive=True, outline=None)


def center(p):
    return (REST[0] + p.dx, REST[1] + p.dy)


def draw(p, phase):
    """One frame of the cage in `phase` (0..2) for pose `p` (k: beat 0..1, ribs 0 shut .. 1 open)."""
    cx, cy = center(p)
    cv = TCanvas(W, H, rot=p.tilt, center=(cx, GY))
    beat = max(0.0, min(1.0, p.get("beat", 0.3)))
    ribs = max(0.0, min(1.0, p.get("ribs", 0.0 if phase == 1 else 0.25)))
    hm = heart_mat(phase)
    # Its base: the spine and the pelvis it stands on.
    cv.part()
    cv.tpoly([(cx - 34, GY), (cx - 26, cy + 56), (cx + 26, cy + 56), (cx + 34, GY)], SKIN_DK, bevel=1.6, base=0.55)
    for k in range(3):
        cv.part(line=1)
        cv.tellipse((cx, cy + 62 + 8 * k), 10.0 - k, 4.0, BONE)
    # The torso: broad shoulders, a narrow waist.
    cv.part()
    body = [(cx - 72, cy - 58), (cx - 40, cy - 70), (cx + 40, cy - 70), (cx + 72, cy - 58), (cx + 66, cy - 10), (cx + 50, cy + 40),
            (cx + 30, cy + 60), (cx - 30, cy + 60), (cx - 50, cy + 40), (cx - 66, cy - 10)]
    cv.tpoly(body, SKIN, bevel=2.4, base=0.56)
    # Molten seams over the skin (white-hot at the last phase).
    for line in (((-60, -40), (-48, -20), (-56, 0)), ((58, -44), (50, -24), (58, -6)), ((-40, 36), (-28, 48)), ((40, 34), (30, 50)),
                 ((-30, -60), (-20, -50)), ((30, -62), (20, -52))):
        for a, b in zip(line, line[1:]):
            cv.tcapsule((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), 1.4 + 0.4 * phase, 1.1, SEAM[phase], flat=0.45 + 0.15 * phase, decal=True)
    # The cavity and the heart beating in it.
    cv.part(line=False)
    cv.tellipse((cx, cy), 30.0, 34.0, CAV)
    r = 13.0 + 4.0 * beat + 3.0 * phase
    cv.part(line=False)
    cv.tellipse((cx, cy + 2), r, r * 1.1, hm, bias=-0.1 + 0.3 * beat + 0.1 * phase - 0.7 * p.fade, decal=True)
    cv.tdisc((cx - 3, cy - 2), r * 0.4, hm, bias=0.4 + 0.2 * beat - 0.8 * p.fade, decal=True)
    for k in range(3):
        a = -0.8 + 0.8 * k
        cv.tcapsule((cx + math.cos(a) * r * 0.6, cy - r * 0.9), (cx + math.cos(a) * r * 0.9, cy - r * 1.5), 2.6, 1.8, hm, flat=0.35, decal=True)
    # The ribs: four pairs from the sternum curving round the cavity (shut over it, pried open, or broken).
    for i, y in enumerate((cy - 26, cy - 12, cy + 2, cy + 16)):
        for side in (-1, 1):
            spread = 1.0 + 0.9 * ribs
            pts = [(cx + 6 * side, y), (cx + (24 * spread) * side, y + 2), (cx + (36 * spread) * side, y + 10)]
            if phase == 2:
                pts = pts[:2]  # broken: the stubs only
                pts[1] = (cx + 18 * side, y + 1)
            cv.part()
            cv.tchain(pts, [3.0, 2.6, 1.6][:len(pts)], BONE)
            if phase == 2:
                cv.tdisc(pts[-1], 1.6, SEAM[2], bias=0.3, decal=True)
    # The sternum and the gorget.
    cv.part()
    cv.tpoly([(cx - 6, cy - 40), (cx + 6, cy - 40), (cx + 5, cy + 22), (cx - 5, cy + 22)], ARMOR if phase < 2 else BONE, bevel=1.2, base=0.62)
    cv.part()
    cv.tpoly([(cx - 34, cy - 70), (cx + 34, cy - 70), (cx + 40, cy - 56), (cx, cy - 46), (cx - 40, cy - 56)], ARMOR, bevel=1.8, base=0.6)
    for side in (-1, 1):
        cv.part()
        cv.tellipse((cx + 70 * side, cy - 58), 18.0, 14.0, ARMOR, ang=0.3 * side)
    # The mounts of its four plates: dark sockets with rivets (phase 1: its plates sit on them), torn and glowing after.
    for px, py in PLATES:
        cv.part(line=False)
        cv.tellipse((cx + px, cy + py), 12.0, 11.0, SKIN_DK, decal=True)
        if phase == 0:
            for a in range(4):
                ang = a * math.pi / 2 + 0.6
                cv.tdisc((cx + px + math.cos(ang) * 9, cy + py + math.sin(ang) * 8), 1.6, ARMOR, bias=0.15, decal=True)
        else:
            cv.tellipse((cx + px, cy + py), 7.0, 6.0, SEAM[phase], bias=0.1, decal=True)
    img = cv.render(outline=True)
    return post(img, p, phase)


def post(img, p, phase):
    cx, cy = center(p)
    out = img
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 131)
    if p.fade < 0.6:
        out = halo(out, HALO[phase], 0.12 + 0.12 * phase, SEED + p.seed + 41 * phase, rings=1 + (phase == 2))
    if phase == 2 and p.fade < 0.5:
        for _ in range(14):
            ang, d = rng.uniform(0, 2 * math.pi), rng.uniform(40, 70)
            _blob(out, int(cx + math.cos(ang) * d), int(cy + math.sin(ang) * d), 1, C(PHASES[2][1][2]))
    if p.burst > 0:
        sparks(out, (cx, cy), 50 + 60 * p.burst, int(14 + 16 * p.burst), [C(h) for h in PHASES[phase][1][:3]], SEED + p.seed)
    if p.dust > 0:
        for _ in range(int(18 * p.dust)):
            _blob(out, int(rng.uniform(cx - 70, cx + 70)), int(GY - rng.uniform(0, 12)), 1 + int(rng.uniform(0, 2)), C("#8a7c9c"))
    out = flash(out, p.flash)
    return trim(out)


# ----------------------------------------------------------------------------- clips
def idle():
    beats = [0.1, 1.0, 0.4, 0.8, 0.2, 0.1]  # lub-dub, then rest
    return [Pose(dy=-wave(i / 6, 1.5), beat=b, seed=i) for i, b in enumerate(beats)]


def move():
    return [Pose(dy=-abs(wave(i / 6, 2.0)), tilt=wave(i / 6, 0.02), beat=0.3 + 0.3 * abs(wave(i / 6)), seed=10 + i) for i in range(6)]


def telegraph():
    return [Pose(dy=3 * k, beat=0.3 + 0.7 * k, ribs=0.2 * k, seed=20 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def attack():
    seq = [(1.0, 0.6, 0.3), (1.0, 1.0, 0.2), (0.7, 0.6, 0.0), (0.5, 0.3, 0.0), (0.3, 0.1, 0.0)]
    return [Pose(dy=-4 if i == 1 else 0, beat=b, burst=bu, flash=f, ribs=0.3, seed=30 + i) for i, (b, bu, f) in enumerate(seq)]


def telegraph_special():
    return [Pose(dy=4 * k, beat=0.2 + 0.8 * k, ribs=0.5 * k, burst=0.2 * k, seed=40 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def special():
    return [Pose(beat=1.0 - 0.3 * (i % 2), ribs=0.6, burst=0.6 + 0.4 * (i % 2), flash=0.15 if i == 0 else 0.0, seed=50 + i) for i in range(4)]


def summon():
    return [Pose(dy=-6 * k, beat=0.4 + 0.6 * k, ribs=0.4 * k, burst=0.4 * k, seed=60 + i) for i, k in enumerate((0.0, 0.4, 0.8, 1.0, 0.8))]


def hurt():
    return [Pose(dx=-3, tilt=-0.05, beat=0.8, flash=0.55, seed=70), Pose(dx=-1, tilt=-0.02, beat=0.5, flash=0.2, seed=71)]


def roar():
    seq = [0.2, 0.6, 1.0, 1.0, 0.7, 0.3]
    return [Pose(dy=-5 * k, beat=k, ribs=0.8 * k, burst=k if i in (2, 3) else 0.2 * k, flash=0.3 if i == 2 else 0.0, seed=80 + i) for i, k in enumerate(seq)]


def death():
    out = []
    for i in range(10):
        t = i / 9
        out.append(Pose(dy=min(18.0, 22 * t), tilt=0.12 * t, beat=max(0.0, 1.0 - 1.4 * t), ribs=0.4 + 0.6 * t, fade=min(1.0, t * 1.2),
                        flash=0.4 if i == 1 else 0.0, burst=0.7 if i == 2 else 0.0, dust=0.8 if i >= 6 else 0.0, seed=90 + i))
    return out


CLIPS = [
    ("idle", idle, dict(durations=T(0.3, 0.12, 0.12, 0.12, 0.2, 0.3), loop=True)),
    ("move", move, dict(fps=6, loop=True)),
    ("telegraph", telegraph, dict(durations=T(0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("attack", attack, dict(durations=T(0.08, 0.12, 0.14, 0.14, 0.16), loop=False)),
    ("telegraph_special", telegraph_special, dict(durations=T(0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("special", special, dict(fps=10, loop=True)),
    ("summon", summon, dict(durations=T(0.12, 0.12, 0.14, 0.16, 0.16), loop=False)),
    ("hurt", hurt, dict(durations=T(0.08, 0.14), loop=False)),
    ("roar", roar, dict(durations=T(0.12, 0.12, 0.12, 0.14, 0.16, 0.2), loop=False)),
    ("death", death, dict(durations=T(0.1, 0.12, 0.12, 0.14, 0.14, 0.16, 0.16, 0.18, 0.2, 0.4), loop=False)),
]


# ----------------------------------------------------------------------------- weak points
def weak_frames(kind):
    """An armour plate (riveted, its seams glowing) or the light of its heart: idle, hurt, breaking."""
    fw = fh = 48
    hm = heart_mat(1)
    ramp = [C(h) for h in (PHASES[1][1] if kind == "coeur" else ("#8ea3c6", "#6177a0", "#ffb050", "#ff7a2e"))]

    def one(pulse, crack=0.0, scale=1.0, fl=0.0):
        cv = TCanvas(fw, fh)
        if kind == "plaque":
            cv.part()
            pts = [(8, 12), (24, 6), (40, 12), (40, 32), (24, 42), (8, 32)]
            cv.tpoly([(24 + (x - 24) * scale, 24 + (y - 24) * scale) for x, y in pts], ARMOR, bevel=2.0, base=0.6)
            for x, y in ((14, 15), (34, 15), (14, 30), (34, 30)):
                cv.tdisc((x, y), 1.8, BONE, bias=0.2, decal=True)
            cv.tline((12, 23), (36, 23), SEAM[0], inten=0.4 + 0.4 * pulse, decal=True)
        else:
            cv.part(line=False)
            r = (11.0 + 2.0 * pulse) * scale
            cv.tellipse((24, 24), r, r * 1.1, hm, bias=-0.05 + 0.3 * pulse)
            cv.tdisc((21, 21), r * 0.4, hm, bias=0.5, decal=True)
            for k in range(3):
                a = -0.8 + 0.8 * k
                cv.tcapsule((24 + math.cos(a) * r * 0.5, 24 - r * 0.9), (24 + math.cos(a) * r * 0.8, 24 - r * 1.4), 2.2, 1.4, hm, flat=0.4, decal=True)
        if crack:
            cv.tline((16, 16), (30, 32), SEAM[2], inten=0.8, decal=True)
            cv.tline((32, 12), (24, 24), SEAM[2], inten=0.8, decal=True)
        img = cv.render(outline=True)
        if kind == "coeur":
            img = halo(img, C("#ffc060"), 0.3 + 0.3 * pulse, SEED + 600 + int(pulse * 10))
        return flash(img, fl)

    idle_f = [one(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [one(1.0, 1.0, 1.0, 0.6), one(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = one(1.0, 1.0, 1.0 - 0.15 * i, 0.4) if i < 2 else np.zeros((fh, fw, 4), np.uint8)
        t = (i + 1) / 6
        for k in range(10):
            ang = k / 10 * 2 * math.pi + 0.3
            d = 5 + 16 * t
            _blob(img, int(24 + math.cos(ang) * d), int(24 + math.sin(ang) * d), 1 if i > 3 else 2, ramp[(k + i) % 4] if i < 5 else C("#5e5272"))
        death_f.append(img)
    return pack([("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=T(0.06, 0.1), loop=False)),
                 ("death", death_f, dict(durations=T(0.06, 0.06, 0.08, 0.08, 0.1, 0.12), loop=False))], fw, fh, columns=6)


# ----------------------------------------------------------------------------- its rules
FW, FH = 64, 128


def fx_frames():
    """Its ribs to pry open (a rib of bone with a grip, shut / held (glowing) / open), a rib trap
    rising out of the ground (growing, then shut), the wave of a strong beat running on the floor."""
    lit = mat("hb_rib_lit", ["#ffffff", "#ffe8a8", "#ffc060", "#ff7a2e"], emissive=True, outline=None)

    def rib(glow, open_=0.0, seed=0):
        cv = TCanvas(FW, FH)
        cv.part()
        cv.tpoly([(12, FH - 1), (18, FH - 10), (46, FH - 10), (52, FH - 1)], ARMOR, bevel=1.5, base=0.6)
        top = (34 - 18 * open_, 8 + 10 * open_)
        cv.part()
        cv.tchain([(30, FH - 10), (40, FH - 60), top], [6.0, 5.0, 3.0], BONE)
        cv.part()
        cv.tcapsule((24, FH - 44), (44, FH - 48), 3.0, 3.0, ARMOR)
        if glow > 0:
            cv.tcapsule((26, FH - 45), (42, FH - 48), 1.6, 1.6, lit, flat=0.5 + 0.4 * glow, decal=True)
        img = cv.render(outline=True)
        if glow > 0:
            img = halo(img, C("#ffc060"), 0.3 * glow, SEED + 900 + seed)
        return img

    def trap(k, seed):
        cv = TCanvas(FW, FH)
        h = 20 + 80 * min(1.0, k)
        bend = 0.0 if k <= 1.0 else 1.0
        tip = (30 + 26 * bend, FH - 4 - h + 14 * bend)
        cv.part()
        cv.tchain([(26, FH - 2), (30 + 6 * bend, FH - 4 - h * 0.6), tip], [6.0, 4.5, 1.2], BONE)
        img = cv.render(outline=True)
        sparks(img, (26, FH - 4), 8 + 6 * k, 6, [C("#8a7c9c"), C("#b8aec6")], SEED + 920 + seed, squash=0.3)
        return img

    def wave_frame(i):
        img = np.zeros((FH, FW, 4), np.uint8)
        ramp = [C("#ffffff"), C("#ffe8a8"), C("#ff5d8f"), C("#c8401a")]
        rng = np.random.RandomState(SEED + 940 + i)
        for x in range(FW):
            t = x / (FW - 1)
            hgt = int(6 + 34 * math.sin(t * math.pi) ** 1.5 * (0.85 + 0.15 * math.sin(i * 1.7 + t * 6)))
            for y in range(FH - hgt, FH - 1):
                d = (FH - 1 - y) / max(1, hgt)
                img[y, x] = ramp[0] if d > 0.8 else ramp[1] if d > 0.55 else ramp[2] if d > 0.25 else ramp[3]
        for _ in range(10):
            _blob(img, int(rng.uniform(8, 56)), int(FH - 44 - rng.uniform(0, 14)), 1, ramp[int(rng.uniform(0, 3))])
        return img

    clips = [("rib", [rib(0.0)], dict(fps=1, loop=True)),
             ("rib_held", [rib(0.5 + 0.5 * abs(wave(i / 4)), 0.2, i) for i in range(4)], dict(fps=8, loop=True)),
             ("rib_open", [rib(1.0, 1.0, 10), rib(0.8, 0.9, 11)], dict(fps=6, loop=True)),
             ("trap", [trap(k, i) for i, k in enumerate((0.2, 0.55, 1.0, 1.5))], dict(fps=8, loop=False)),
             ("wave", [wave_frame(i) for i in range(4)], dict(fps=12, loop=True))]
    return pack(clips, FW, FH, columns=8)


# ----------------------------------------------------------------------------- output
def outputs():
    return boss_outputs("heartbreaker", draw, CLIPS, [("heartbreaker_plaque", weak_frames("plaque"), 48, 48, [24, 44]),
                                                      ("heartbreaker_coeur", weak_frames("coeur"), 48, 48, [24, 44]),
                                                      ("heartbreaker_fx", fx_frames(), FW, FH, [FW // 2, FH])])


def do_review(path):
    picks = [("Repos (battement)", idle()[1]), ("Respiration", move()[2]), ("Le cœur se gonfle", telegraph()[4]), ("Battement fort", attack()[1]),
             ("Onde du cœur (préparation)", telegraph_special()[4]), ("Onde du cœur", special()[0]), ("Caillots brûlants", summon()[3]),
             ("Touché", hurt()[0]), ("Rugissement de phase", roar()[2]), ("Mort", death()[5])]
    plaque, _ = weak_frames("plaque")
    coeur, _ = weak_frames("coeur")
    fx, _ = fx_frames()
    extras = [("Plaque d'armure (repos, touchée, brisée) x2", plaque, 2), ("Lumière du cœur (repos, touchée, éteinte) x2", coeur, 2),
              ("Côtes à écarter (fermée, tenue, ouverte), côte-piège qui se dresse, onde d'un battement fort", fx, 2)]
    return review(path, "HEARTBREAKER — la cage qui garde le cœur (phases : la cuirasse, la cage thoracique, le cœur affolé)",
                  [ph[0] for ph in PHASES], picks, draw, extras)


if __name__ == "__main__":
    main("heartbreaker", outputs, do_review)
