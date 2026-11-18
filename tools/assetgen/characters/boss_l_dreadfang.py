"""DREADFANG, les griffes qui déchirent (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The giant left arm of the titan's BRAS GAUCHE, as drawn on Asterion (environment/raid_asterion.py,
part_bras_gauche): an arm of the titan's violet skin, bone fangs along its pauldron and forearm,
an armoured bracer and knuckles, and its hand of four long blade-claws reaching down to the ground,
in the frost of Glacior.  Side view facing right (the game flips it).  Three phase sheets, its look
following its health:
  * 1 « Les griffes »        sprites/enemies/dreadfang.png     bare skin, frost glinting on the blades;
  * 2 « Le bras enchaîné »   sprites/enemies/dreadfang_p2.png  covered in frost armour, a shackle and
    its chain at the wrist (the chains the players hold);
  * 3 « Griffes déchaînées » sprites/enemies/dreadfang_p3.png  the armour shattered, the shackle broken,
    longer claws, violet cracks glowing white, frost mist.
Frames 224x224, pivot [112, 212] = bottom-centre of the 128x96 collision box (scale 2.5: 320x240).
Clips: idle 6, move 6, telegraph 5, attack 5, telegraph_special 5, special 4, summon 5, hurt 2,
roar 6, death 10.

Also its weak points (a knuckle joint: dreadfang_jointure, 32x32; the nailing claw:
dreadfang_griffe, 48x48: idle 6, hurt 2, death 6) and the sheet of its own rules (dreadfang_fx,
64x160: anchor 1, anchor_held 4, anchor_locked 2, claws 4 = the claws out of the ground, blade 4 = its
sweeping talons), drawn by the raid's combat screen.

    python3 tools/assetgen/characters/boss_l_dreadfang.py [--check | --review out.png]
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
SEED = 69201
SKIN = mat("df_skin", ["#8f58c9", "#6a3dab", "#4b2c8a", "#35206b", "#24164f"])
SKIN_DK = mat("df_skin_dk", ["#6a3dab", "#4b2c8a", "#35206b", "#24164f", "#170f36"])
BONE = mat("df_bone", ["#fbf7ff", "#e2dbea", "#b8aec6", "#8a7c9c", "#5e5272"])
ARMOR = mat("df_armor", ["#8ea3c6", "#6177a0", "#46587f", "#323f63", "#232d4b"])
ICE = mat("df_ice", ["#e8feff", "#bff8ff", "#7ff6ff", "#3fb8d8", "#1e6e98"])
FROST = mat("df_frost", ["#ffffff", "#bff8ff", "#7ff6ff"], emissive=True, outline=None)
CRACK = mat("df_crack", ["#ffffff", "#f0b8ff", "#c070f0", "#8a3ad0"], emissive=True, outline=None)
CHAIN = mat("df_chain", ["#c8d4e8", "#8ea3c6", "#5c6e92", "#323f63"])
PHASES = ["Les griffes", "Le bras enchaîné", "Griffes déchaînées"]
MIST = [C("#bff8ff"), C("#7ff6ff"), C("#e8d8ff")]

REST = (122.0, 150.0)  # the back of its hand
# Its claws: root on the hand (from REST), tip on the ground (x from REST), its bend.
CLAWS = [((34.0, 8.0), 66.0, 0.9), ((22.0, 12.0), 40.0, 0.8), ((10.0, 12.0), 16.0, 0.7), ((-2.0, 10.0), -8.0, 0.6)]
# Its knuckle joints (weak points, world from its feet at scale 2.5): [70, -175], [100, -180], [130, -165].
KNUCKLES = [(18.0, -8.0), (30.0, -10.0), (42.0, -4.0)]


def center(p):
    return (REST[0] + p.dx, REST[1] + p.dy)


def claw(cv, root, tip, bend, width, mat_, edge):
    """A blade-claw: a bone blade curving from its root to its tip, an icy edge along it."""
    mx, my = (root[0] + tip[0]) / 2 + 10 * bend, (root[1] + tip[1]) / 2 - 6 * bend
    cv.part()
    cv.tchain([root, (mx, my), tip], [width, width * 0.6, 0.8], mat_)
    cv.tline((root[0] + 2, root[1] + 1), (mx + 2, my), edge, inten=0.8, decal=True)
    cv.tline((mx + 2, my), (tip[0] + 1, tip[1] - 2), edge, inten=0.9, decal=True)


def draw(p, phase):
    """One frame of the arm in `phase` (0..2) for pose `p` (k: curl 0..1 of its claws, reach)."""
    cx, cy = center(p)
    cv = TCanvas(W, H, rot=p.tilt, center=(cx, cy))
    curl = max(0.0, min(1.0, p.get("curl", 0.0)))
    reach = p.get("reach", 0.0)
    longer = 8.0 if phase == 2 else 0.0
    # Far claws (behind the hand), the thumb claw toward the back.
    for k in (3, 2):
        (rx, ry), tx, bend = CLAWS[k]
        tip = (cx + tx + reach - curl * 30, GY - 2 - curl * 44)
        claw(cv, (cx + rx, cy + ry), tip, bend, 5.5, BONE, FROST)
    claw(cv, (cx - 14, cy + 6), (cx - 34 - curl * 6, GY - 8 - curl * 30), -0.5, 5.0, BONE, FROST)
    # The upper arm trailing up and back, its pauldron and bone fangs.
    cv.part()
    elbow, shoulder = (cx - 50, cy - 44), (cx - 84, cy - 90)
    cv.tcapsule(shoulder, elbow, 21.0, 18.0, SKIN_DK)
    for a, b in (((cx - 66, cy - 78), (cx - 58, cy - 100)), ((cx - 78, cy - 66), (cx - 92, cy - 82)), ((cx - 54, cy - 62), (cx - 40, cy - 80))):
        cv.part()
        cv.tcapsule(a, b, 4.0, 0.8, BONE)
    cv.part()
    cv.tellipse((cx - 80, cy - 88), 20.0, 15.0, ARMOR, ang=-0.7)
    # The forearm and its bracer.
    cv.part()
    cv.tcapsule(elbow, (cx + 6, cy - 6), 19.0, 17.0, SKIN)
    cv.part(line=1)
    cv.tpoly([(cx - 34, cy - 40), (cx - 6, cy - 22), (cx - 12, cy - 6), (cx - 42, cy - 22)], ARMOR, bevel=1.6, base=0.6)
    for a, b in (((cx - 30, cy - 34), (cx - 20, cy - 52)), ((cx - 16, cy - 24), (cx - 4, cy - 40))):
        cv.part()
        cv.tcapsule(a, b, 3.5, 0.8, BONE)
    # The hand, its armoured knuckles.
    cv.part()
    cv.tellipse((cx + 22, cy + 2), 31.0, 21.0, SKIN, ang=0.15)
    for kx, ky in KNUCKLES:
        cv.part(line=1)
        cv.tdisc((cx + kx, cy + ky), 7.5, ARMOR, bias=0.05)
    # Violet cracks in the skin (more at each phase; white-hot at the last).
    cracks = [((-70, -74), (-60, -60), (-64, -50)), ((-30, -24), (-18, -16)), ((4, 2), (16, 8), (24, 4)), ((-48, -40), (-40, -30)),
              ((30, 12), (40, 10)), ((-80, -88), (-74, -78))]
    for line in cracks[:2 + 2 * phase]:
        for a, b in zip(line, line[1:]):
            cv.tcapsule((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), 1.0 + 0.4 * phase, 0.9, CRACK, flat=0.45 + 0.2 * phase, decal=True)
    if phase == 1:
        # Its frost armour: plates of ice over the forearm and the back of the hand; its shackle and chain.
        for pl in ([(-40, -44), (-20, -36), (-26, -16), (-46, -26)], [(-16, -30), (2, -20), (-2, -2), (-20, -12)],
                   [(4, -12), (26, -16), (44, -8), (34, 8), (10, 6)]):
            cv.part(line=1)
            cv.tpoly([(cx + x, cy + y) for x, y in pl], ICE, bevel=1.8, base=0.62)
        cv.part()
        cv.tellipse((cx - 2, cy - 8), 8.0, 17.0, CHAIN, ang=-0.5)
        for i in range(5):
            cv.part(line=1)
            cv.tellipse((cx - 8 - 4 * i, cy + 10 + 9 * i), 3.2, 4.6, CHAIN, ang=0.4 * (i % 2))
    if phase == 2:
        # The armour shattered (shards still stuck on), the shackle broken, a short chain end.
        for sx, sy in ((-34, -38), (-8, -26), (18, -14), (36, -10)):
            cv.part(line=1)
            cv.tpoly([(cx + sx, cy + sy - 6), (cx + sx + 6, cy + sy), (cx + sx - 2, cy + sy + 4)], ICE, bevel=1.0)
        cv.part()
        cv.tellipse((cx - 2, cy - 8), 8.0, 17.0, CHAIN, ang=-0.5)
        cv.tline((cx - 6, cy - 22), (cx + 2, cy - 14), CRACK, inten=0.9, decal=True)
        cv.part(line=1)
        cv.tellipse((cx - 8, cy + 12), 3.2, 4.6, CHAIN, ang=0.4)
    # Near claws (in front of the hand).
    for k in (1, 0):
        (rx, ry), tx, bend = CLAWS[k]
        tip = (cx + tx + reach + longer - curl * 34, GY - 2 - curl * 48)
        claw(cv, (cx + rx, cy + ry), tip, bend, 6.5, BONE, FROST)
    img = cv.render(outline=True)
    return post(img, p, phase)


def post(img, p, phase):
    cx, cy = center(p)
    out = img
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 131)
    if p.fade < 0.6:
        out = halo(out, MIST[phase], 0.12 + 0.1 * phase, SEED + p.seed + 37 * phase, rings=1 + (phase == 2))
    if p.fade < 0.5:
        # Frost glinting on the blades and drifting off it (thicker as it breaks free).
        for _ in range(6 + 8 * phase):
            _blob(out, int(rng.uniform(cx - 60, cx + 80)), int(rng.uniform(cy - 90, GY)), 1, MIST[min(2, phase)] if rng.uniform() < 0.5 else C("#ffffff"))
    if p.burst > 0:
        sparks(out, (cx + 30, GY - 20), 40 + 50 * p.burst, int(12 + 14 * p.burst), [C("#ffffff"), C("#7ff6ff"), C("#bff8ff")], SEED + p.seed, squash=0.5)
    if p.dust > 0:
        for _ in range(int(18 * p.dust)):
            _blob(out, int(rng.uniform(cx - 40, cx + 90)), int(GY - rng.uniform(0, 10)), 1 + int(rng.uniform(0, 2)), C("#d8f4ff"))
    out = flash(out, p.flash)
    return trim(out)


# ----------------------------------------------------------------------------- clips
def idle():
    return [Pose(dy=wave(i / 6, 3.0), curl=0.05 + 0.05 * wave(i / 6), reach=wave(i / 6, 2.0), seed=i) for i in range(6)]


def move():
    return [Pose(dx=wave(i / 6, 3.0), dy=-abs(wave(i / 6, 5.0)), tilt=wave(i / 6, 0.04), curl=0.15 + 0.1 * abs(wave(i / 6)), reach=wave(i / 6 + 0.25, 6.0),
                 seed=10 + i) for i in range(6)]


def telegraph():
    return [Pose(dy=-16 * k, dx=-8 * k, tilt=-0.18 * k, curl=0.7 * k, seed=20 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def attack():
    seq = [(-12, 4, 0.1, 0.9, 0.0), (-2, 14, 0.18, 0.4, 0.3), (8, 24, 0.22, 0.0, 1.0), (6, 18, 0.12, 0.0, 0.5), (2, 8, 0.04, 0.1, 0.0)]
    return [Pose(dy=dy, dx=dx, tilt=t, curl=c, reach=10 if b else 0, burst=b, dust=b, seed=30 + i) for i, (dy, dx, t, c, b) in enumerate(seq)]


def telegraph_special():
    return [Pose(dy=-10 * k, curl=0.9 * k, tilt=-0.1 * k, burst=0.2 * k, seed=40 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def special():
    return [Pose(dy=10, curl=0.0, reach=12, burst=0.5 + 0.4 * (i % 2), dust=0.6, seed=50 + i) for i in range(4)]


def summon():
    return [Pose(dy=-14 * k, curl=0.5 * k, tilt=-0.12 * k, burst=0.4 * k, seed=60 + i) for i, k in enumerate((0.0, 0.4, 0.8, 1.0, 0.8))]


def hurt():
    return [Pose(dx=-6, tilt=-0.12, curl=0.3, flash=0.55, seed=70), Pose(dx=-3, tilt=-0.06, curl=0.2, flash=0.2, seed=71)]


def roar():
    seq = [0.2, 0.6, 1.0, 1.0, 0.7, 0.3]
    return [Pose(dy=-12 * k, tilt=-0.12 * k, curl=0.2, reach=-6 * k, burst=k if i in (2, 3) else 0.2 * k, flash=0.3 if i == 2 else 0.0, seed=80 + i)
            for i, k in enumerate(seq)]


def death():
    out = []
    for i in range(10):
        t = i / 9
        out.append(Pose(dy=min(34.0, 40 * t), dx=-10 * t, tilt=0.3 * t, curl=0.6 * t, fade=min(1.0, t * 1.2), flash=0.4 if i == 1 else 0.0,
                        burst=0.6 if i == 2 else 0.0, dust=0.8 if i >= 6 else 0.0, seed=90 + i))
    return out


CLIPS = [
    ("idle", idle, dict(fps=6, loop=True)),
    ("move", move, dict(fps=8, loop=True)),
    ("telegraph", telegraph, dict(durations=T(0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("attack", attack, dict(durations=T(0.06, 0.08, 0.16, 0.14, 0.16), loop=False, events=[(2, "impact")])),
    ("telegraph_special", telegraph_special, dict(durations=T(0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("special", special, dict(fps=10, loop=True)),
    ("summon", summon, dict(durations=T(0.12, 0.12, 0.14, 0.16, 0.16), loop=False)),
    ("hurt", hurt, dict(durations=T(0.08, 0.14), loop=False)),
    ("roar", roar, dict(durations=T(0.12, 0.12, 0.12, 0.14, 0.16, 0.2), loop=False)),
    ("death", death, dict(durations=T(0.1, 0.12, 0.12, 0.14, 0.14, 0.16, 0.16, 0.18, 0.2, 0.4), loop=False)),
]


# ----------------------------------------------------------------------------- weak points
def weak_frames(kind):
    """A knuckle joint (a bare joint of ice-blue light in its armour ring), or the nailing claw (a
    blade of bone and ice driven into the ground): idle, hurt, breaking."""
    fw, fh = (32, 32) if kind == "jointure" else (48, 48)
    ramp = [C(h) for h in ("#ffffff", "#bff8ff", "#7ff6ff", "#3fb8d8")]
    glow = mat("df_joint", ["#ffffff", "#bff8ff", "#7ff6ff", "#3fb8d8"], emissive=True, outline=None)

    def one(pulse, crack=0.0, scale=1.0, fl=0.0):
        cv = TCanvas(fw, fh)
        if kind == "jointure":
            cv.part()
            cv.tdisc((16, 16), 11.0 * scale, ARMOR)
            cv.part(line=False)
            cv.tdisc((16, 16), 7.0 * scale, SKIN_DK)
            cv.tdisc((16, 16), (3.5 + 1.5 * pulse) * scale, glow, bias=0.1 + 0.3 * pulse)
        else:
            cv.part()
            cv.tpoly([(14, 46), (20, 30), (24, 12), (30, 4), (28, 18), (34, 30), (32, 46)], BONE, bevel=1.6, base=0.62)
            cv.tline((26, 10), (30, 30), FROST, inten=0.8 + 0.2 * pulse, decal=True)
            cv.part()
            cv.tellipse((24, 44), 14.0 * scale, 4.0, ICE)
            for k in range(3):
                cv.tline((14 + 8 * k, 44), (12 + 8 * k, 36), FROST, inten=0.6 + 0.3 * pulse, decal=True)
        if crack:
            cv.tline((fw // 2 - 6, fh // 2 - 5), (fw // 2 + 5, fh // 2 + 6), CRACK, inten=0.8, decal=True)
        return flash(cv.render(outline=True), fl)

    idle_f = [one(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [one(1.0, 1.0, 1.0, 0.6), one(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = one(1.0, 1.0, 1.0 - 0.15 * i, 0.4) if i < 2 else np.zeros((fh, fw, 4), np.uint8)
        t = (i + 1) / 6
        for k in range(8):
            ang = k / 8 * 2 * math.pi + 0.3
            d = 4 + (fw // 2 - 5) * t
            _blob(img, int(fw / 2 + math.cos(ang) * d), int(fh / 2 + math.sin(ang) * d), 1 if i > 3 else 2, ramp[(k + i) % 4] if i < 5 else C("#5e5272"))
        death_f.append(img)
    return pack([("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=T(0.06, 0.1), loop=False)),
                 ("death", death_f, dict(durations=T(0.06, 0.06, 0.08, 0.08, 0.1, 0.12), loop=False))], fw, fh, columns=6)


# ----------------------------------------------------------------------------- its rules
FW, FH = 64, 160


def fx_frames():
    """Its anchors (a winch of bone and iron, its chain coiled: free, held (glowing), locked (the
    chain drawn up taut)), the claws of ice out of the ground (growing), its sweeping talons."""
    lit = mat("df_anchor_lit", ["#ffffff", "#bff8ff", "#7ff6ff", "#3fb8d8"], emissive=True, outline=None)

    def anchor(glow, taut=False, seed=0):
        cv = TCanvas(FW, FH)
        cv.part()
        cv.tpoly([(10, FH - 1), (16, FH - 12), (48, FH - 12), (54, FH - 1)], ARMOR, bevel=1.5, base=0.6)
        cv.part()
        cv.tcapsule((32, FH - 12), (32, FH - 46), 7.0, 6.0, BONE)
        cv.part()
        cv.tellipse((32, FH - 34), 14.0, 8.0, CHAIN)
        for k in range(3):
            cv.tline((20, FH - 38 + 4 * k), (44, FH - 38 + 4 * k), CHAIN, inten=0.25, decal=True)
        if glow > 0:
            cv.part(line=False)
            cv.tellipse((32, FH - 34), 9.0, 4.0, lit, bias=0.2 * glow, decal=True)
        links = 9 if taut else 3
        for i in range(links):
            cv.part(line=1)
            y = FH - 50 - 11 * i
            cv.tellipse((32 + (0 if taut else 6 * math.sin(i)), y), 3.4, 5.0, lit if (taut and glow > 0) else CHAIN, ang=0.3 * (i % 2))
        img = cv.render(outline=True)
        if glow > 0:
            img = halo(img, C("#7ff6ff"), 0.3 * glow, SEED + 800 + seed)
        return img

    def ground_claws(k, seed):
        cv = TCanvas(FW, FH)
        h = 30 + 120 * k
        for j, (bx, lean) in enumerate(((20, -0.25), (32, 0.0), (44, 0.25))):
            hh = h * (0.75 if j != 1 else 1.0)
            cv.part()
            cv.tpoly([(bx - 6, FH - 2), (bx + 6, FH - 2), (bx + lean * hh * 0.3 + 1, FH - 2 - hh)], ICE, bevel=1.4, base=0.6)
            cv.tline((bx, FH - 4), (bx + lean * hh * 0.3, FH - 2 - hh * 0.9), FROST, inten=0.9, decal=True)
        img = cv.render(outline=True)
        sparks(img, (32, FH - 6), 14 + 10 * k, 10, [C("#ffffff"), C("#bff8ff")], SEED + 820 + seed, squash=0.3)
        return img

    def blade(i):
        cv = TCanvas(FW, FH)
        for j, x0 in enumerate((14, 30, 46)):
            sway = 3 * math.sin(i * 1.4 + j)
            cv.part()
            cv.tchain([(x0 - 8, 4), (x0 + 2 + sway, FH * 0.55), (x0 + 10 + sway, FH - 6)], [6.0, 4.0, 0.8], BONE)
            cv.tline((x0 + 3 + sway, FH * 0.55), (x0 + 10 + sway, FH - 8), FROST, inten=0.9, decal=True)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 840 + i)
        for _ in range(16):
            y = int(rng.uniform(FH * 0.5, FH - 4))
            x = int(rng.uniform(2, 20))
            for xx in range(x, x + int(rng.uniform(4, 12))):
                if 0 <= xx < FW:
                    img[y, xx] = C("#bff8ff")
        return img

    clips = [("anchor", [anchor(0.0)], dict(fps=1, loop=True)),
             ("anchor_held", [anchor(0.5 + 0.5 * abs(wave(i / 4)), seed=i) for i in range(4)], dict(fps=8, loop=True)),
             ("anchor_locked", [anchor(1.0, True, 10), anchor(0.7, True, 11)], dict(fps=6, loop=True)),
             ("claws", [ground_claws(k, i) for i, k in enumerate((0.15, 0.45, 0.8, 1.0))], dict(fps=10, loop=False)),
             ("blade", [blade(i) for i in range(4)], dict(fps=16, loop=True))]
    return pack(clips, FW, FH, columns=8)


# ----------------------------------------------------------------------------- output
def outputs():
    return boss_outputs("dreadfang", draw, CLIPS, [("dreadfang_jointure", weak_frames("jointure"), 32, 32, [16, 28]),
                                                   ("dreadfang_griffe", weak_frames("griffe"), 48, 48, [24, 44]),
                                                   ("dreadfang_fx", fx_frames(), FW, FH, [FW // 2, FH])])


def do_review(path):
    picks = [("Repos", idle()[0]), ("Vol", move()[2]), ("Griffes levées", telegraph()[4]), ("Ruée des griffes (impact)", attack()[2]),
             ("Griffes sous la glace", telegraph_special()[4]), ("Griffes plantées", special()[0]), ("Grêle de givre", summon()[3]),
             ("Touché", hurt()[0]), ("Rugissement de phase", roar()[2]), ("Mort", death()[5])]
    joint, _ = weak_frames("jointure")
    griffe, _ = weak_frames("griffe")
    fx, _ = fx_frames()
    extras = [("Point faible : jointure (repos, touchée, brisée) x3", joint, 3), ("Griffe clouante (repos, touchée, brisée) x2", griffe, 2),
              ("Ancres des chaînes (libre, tenue, verrouillée), griffes du sol, lames du balayage", fx, 1)]
    return review(path, "DREADFANG — les griffes qui déchirent (phases : les griffes, le bras enchaîné, griffes déchaînées)", PHASES, picks, draw, extras)


if __name__ == "__main__":
    main("dreadfang", outputs, do_review)
