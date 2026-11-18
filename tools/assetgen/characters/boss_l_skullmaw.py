"""SKULLMAW, le crâne qui voit tout (gigaboss of the clan raid « Asterion », 2.69, docs/RAIDS.md §13).

The colossal horned skull of the titan's CRÂNE, as drawn on Asterion (environment/raid_asterion.py,
part_crane): a cranium of pale bone with violet cracks, two great horns of dark horn, an armoured
brow, the pink crystal of its forehead, burning eyes in their sockets and a maw of fangs over a
furnace — floating, a few vertebrae trailing under it.  Front view (the game may flip it: it is
symmetric).  Three phase sheets, its look following its health:
  * 1 « Le regard qui traque »  sprites/enemies/skullmaw.png     orange fire in its eyes, few cracks;
  * 2 « Le voile psychique »    sprites/enemies/skullmaw_p2.png  pink psychic eyes and crystal, a veil
    of pink motes around it, the brow plate cracked, more cracks;
  * 3 « Les ténèbres »          sprites/enemies/skullmaw_p3.png  a horn broken, a hole in the cranium,
    white-hot eyes, cracks glowing white, a violet aura.
Frames 224x224, pivot [112, 212] = bottom-centre of the 120x120 collision box (scale 2.5: 300x300).
Clips: idle 6, move 6, telegraph 5, attack 5, telegraph_special 5, special 4, summon 5, hurt 2,
roar 6, death 10.

Also its weak points (an eye: skullmaw_oeil, the crystal: skullmaw_cristal, 32x32: idle 6, hurt 2,
death 6) and the sheet of its mirrors (skullmaw_fx, 64x96: mirror 4, mirror_lit 4, mirror_flash 2),
drawn by the raid's combat screen.

    python3 tools/assetgen/characters/boss_l_skullmaw.py [--check | --review out.png]
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
SEED = 69101
BONE = mat("sk_bone", ["#fbf7ff", "#e2dbea", "#b8aec6", "#8a7c9c", "#5e5272"])
BONE_DK = mat("sk_bone_dk", ["#b8aec6", "#8a7c9c", "#5e5272", "#3a3048"])
HORN = mat("sk_horn", ["#9078b8", "#64508e", "#44306a", "#2c1c4a", "#1a1030"])
ARMOR = mat("sk_armor", ["#8ea3c6", "#6177a0", "#46587f", "#323f63", "#232d4b"])
SOCKET = mat("sk_socket", ["#3a1840", "#2a1030", "#1a0820", "#100614"])
GEM = mat("sk_gem", ["#fff4fc", "#ffd0f2", "#ff6ad5", "#9a2a9a", "#4a0f4e"], emissive=True, outline=None)
CRACK = mat("sk_crack", ["#fbeaff", "#f0b8ff", "#c070f0", "#8a3ad0"], emissive=True, outline=None)
PHASES = [
    ("Le regard qui traque", ["#ffffff", "#ffe6a8", "#ffb050", "#ff7a2e", "#d2481a"], ["#ffb050", "#ec6a2a", "#a8321a", "#621612", "#2e0a10"]),
    ("Le voile psychique", ["#ffffff", "#ffe0fa", "#ff9ae6", "#ff6ad5", "#c03aa8"], ["#ff9ae6", "#e04ab8", "#9a2a8a", "#5a1050", "#2a0828"]),
    ("Les ténèbres", ["#ffffff", "#fffaff", "#f0dcff", "#d8b8ff", "#a87ae0"], ["#fff0ff", "#e8c8ff", "#b088e8", "#6a40a8", "#2a1048"]),
]
AURA = [C("#ffb050"), C("#ff6ad5"), C("#e8d8ff")]


def eye_mat(phase):
    return mat("sk_eye%d" % phase, PHASES[phase][1], emissive=True, outline=None)


def maw_mat(phase):
    return mat("sk_maw%d" % phase, PHASES[phase][2], emissive=True, outline=None)


REST = (112.0, 128.0)  # the middle of its face (between its eyes)
EYES = (-22.0, 2.0)    # its eyes from REST (x mirrored): world (scale 2.5) [±55, -205] from its feet


def center(p):
    return (REST[0] + p.dx, REST[1] + p.dy)


def draw(p, phase):
    """One frame of the skull in `phase` (0..2) for pose `p` (k: jaw 0..1, look: its eyes' fire)."""
    cx, cy = center(p)
    cv = TCanvas(W, H, rot=p.tilt, center=(cx, cy))
    jaw = max(0.0, min(1.0, p.get("jaw", 0.1)))
    look = p.get("look", 0.5)
    mg, mw = eye_mat(phase), maw_mat(phase)
    # The vertebrae trailing under it (behind everything).
    for i, (vx, vy, r) in enumerate(((0, 62, 9.0), (-4, 74, 7.5), (2, 84, 6.0))):
        bob = wave(p.seed / 7.0 + i * 0.3, 1.5)
        cv.part()
        cv.tellipse((cx + vx + bob, cy + vy), r * 1.5, r * 0.8, BONE_DK)
        cv.tdisc((cx + vx + bob, cy + vy - 2), r * 0.5, BONE, bias=0.05)
    # Its horns, from the temples, sweeping out and up (the right one broken at its 3rd phase).
    for side in (-1, 1):
        pts = [(cx + 40 * side, cy - 22), (cx + 64 * side, cy - 36), (cx + 80 * side, cy - 62), (cx + 76 * side, cy - 90)]
        radii = [11.0, 9.0, 6.0, 2.0]
        if phase == 2 and side == 1:
            pts, radii = pts[:2] + [(cx + 72 * side, cy - 46)], [11.0, 9.0, 7.5]
        cv.part()
        cv.tchain(pts, radii, HORN)
        for t in (0.25, 0.45, 0.65):
            k = int(t * (len(pts) - 1))
            a, b = pts[k], pts[min(k + 1, len(pts) - 1)]
            u = t * (len(pts) - 1) - k
            q = (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)
            cv.tline((q[0] - 6, q[1] + 3 * side), (q[0] + 6, q[1] - 3 * side), HORN, inten=0.2, decal=True)
        if phase == 2 and side == 1:
            cv.tpoly([(cx + 66, cy - 50), (cx + 76, cy - 52), (cx + 72, cy - 42), (cx + 80, cy - 40), (cx + 70, cy - 36)], CRACK, flat=0.7, decal=True)
    # The cranium and the cheekbones.
    cv.part()
    cv.tellipse((cx, cy - 22), 50.0, 44.0, BONE)
    cv.part(line=1)
    cv.tpoly([(cx - 46, cy - 6), (cx - 30, cy + 20), (cx - 18, cy + 34), (cx + 18, cy + 34), (cx + 30, cy + 20), (cx + 46, cy - 6), (cx + 36, cy - 14),
              (cx - 36, cy - 14)], BONE, bevel=2.0, base=0.6)
    # A hole in the cranium at its 3rd phase, its rim glowing.
    if phase == 2:
        cv.part(line=False)
        cv.tellipse((cx - 22, cy - 48), 11.0, 8.0, SOCKET)
        cv.tellipse((cx - 22, cy - 47), 7.0, 4.5, CRACK, bias=0.1, decal=True)
    # Violet cracks: more at each phase, glowing white at the last.
    cracks = [((-30, -40), (-20, -30), (-24, -16)), ((26, -50), (16, -38), (22, -26)), ((-6, -64), (2, -54), (-4, -44)),
              ((36, -28), (44, -16)), ((-40, -24), (-46, -10)), ((10, -62), (20, -56), (30, -60)), ((-14, -36), (-4, -30)),
              ((30, -6), (24, 8))]
    for line in cracks[:2 + 3 * phase]:
        for a, b in zip(line, line[1:]):
            cv.tcapsule((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), 1.2 + 0.4 * phase, 1.0, CRACK, flat=0.45 + 0.2 * phase, decal=True)
    # The armoured brow and the crystal of its forehead (its weak point: world [0, -280]).
    cv.part()
    cv.tpoly([(cx - 44, cy - 20), (cx - 30, cy - 34), (cx - 8, cy - 30), (cx, cy - 36), (cx + 8, cy - 30), (cx + 30, cy - 34), (cx + 44, cy - 20),
              (cx + 30, cy - 22), (cx, cy - 24), (cx - 30, cy - 22)], ARMOR, bevel=1.6, base=0.62)
    for rx in (-34, -16, 16, 34):
        cv.tdisc((cx + rx, cy - 27), 1.6, BONE, bias=0.2, decal=True)
    if phase >= 1:
        cv.tline((cx - 30, cy - 30), (cx - 18, cy - 24), CRACK, inten=0.8, decal=True)
        cv.tline((cx + 22, cy - 32), (cx + 28, cy - 24), CRACK, inten=0.8, decal=True)
    cv.part(line=1)
    gy = cy - 28
    cv.tpoly([(cx, gy - 10), (cx + 6, gy), (cx, gy + 9), (cx - 6, gy)], GEM, flat=0.55 + 0.1 * phase + 0.2 * max(0.0, look))
    cv.tpoly([(cx, gy - 7), (cx + 3, gy - 1), (cx, gy + 2)], GEM, flat=0.95, decal=True)
    # Its eyes: dark sockets, burning cores (brighter as it looks).
    for side in (-1, 1):
        ex, ey = cx + EYES[0] * -side, cy + EYES[1]
        cv.part(line=False)
        cv.tellipse((ex, ey), 13.0, 10.0, SOCKET, ang=0.15 * side)
        r = 5.0 + 1.5 * phase + 2.0 * max(0.0, look)
        cv.tellipse((ex, ey + 1), r, r * 0.8, mg, bias=-0.25 + 0.3 * max(0.0, look) - 0.6 * p.fade)
        cv.tdisc((ex - 1, ey), r * 0.4, mg, bias=0.3 + 0.2 * max(0.0, look) - 0.8 * p.fade, decal=True)
    # The nose: a dark heart.
    cv.part(line=False)
    cv.tpoly([(cx - 7, cy + 14), (cx, cy + 10), (cx + 7, cy + 14), (cx + 3, cy + 24), (cx, cy + 27), (cx - 3, cy + 24)], SOCKET)
    # The maw: its upper fangs, then its jaw dropping open over the furnace.
    mouth_top, drop = cy + 34, 4 + 18 * jaw
    cv.part(line=False)
    cv.tpoly([(cx - 26, mouth_top), (cx + 26, mouth_top), (cx + 22, mouth_top + drop + 6), (cx - 22, mouth_top + drop + 6)], mw,
             flat=0.35 + 0.4 * jaw + 0.2 * max(0.0, look) - 0.5 * p.fade)
    for i, fx in enumerate(range(-22, 23, 9)):
        cv.part(line=1)
        n = 9 if i % 2 == 0 else 6
        cv.tpoly([(cx + fx - 3, mouth_top - 1), (cx + fx + 3, mouth_top - 1), (cx + fx, mouth_top + n)], BONE, bevel=1.0)
    cv.part()
    jy = mouth_top + drop
    cv.tpoly([(cx - 30, jy), (cx + 30, jy), (cx + 24, jy + 16), (cx + 8, jy + 22), (cx - 8, jy + 22), (cx - 24, jy + 16)], BONE, bevel=2.0, base=0.55)
    for i, fx in enumerate(range(-18, 19, 9)):
        cv.part(line=1)
        n = 7 if i % 2 == 0 else 10
        cv.tpoly([(cx + fx - 3, jy + 1), (cx + fx + 3, jy + 1), (cx + fx, jy - n)], BONE, bevel=1.0)
    img = cv.render(outline=True)
    return post(img, p, phase)


def post(img, p, phase):
    """Its energy over a frame: the veil of motes (2nd phase), the aura (3rd), a burst, a flash."""
    cx, cy = center(p)
    out = img
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 131)
    if p.fade < 0.6:
        out = halo(out, AURA[phase], 0.18 + 0.12 * phase, SEED + p.seed + 31 * phase, rings=1 + (phase == 2))
    if phase >= 1 and p.fade < 0.5:
        for _ in range(10 + 8 * phase):
            ang, d = rng.uniform(0, 2 * math.pi), rng.uniform(62, 96)
            _blob(out, int(cx + math.cos(ang) * d), int(cy - 16 + math.sin(ang) * d * 0.8), 1, AURA[phase])
    if p.burst > 0:
        sparks(out, (cx, cy), 60 + 50 * p.burst, int(14 + 14 * p.burst), [C(PHASES[phase][1][2]), C(PHASES[phase][1][3]), AURA[phase]], SEED + p.seed)
    if p.dust > 0:
        for _ in range(int(16 * p.dust)):
            _blob(out, int(rng.uniform(cx - 50, cx + 50)), int(rng.uniform(cy + 40, GY)), 1 + int(rng.uniform(0, 2)), C("#8a7c9c"))
    out = flash(out, p.flash)
    return trim(out)


# ----------------------------------------------------------------------------- clips
def idle():
    return [Pose(dy=wave(i / 6, 3.0), jaw=0.12 + 0.06 * wave(i / 6), look=0.3 + wave(i / 6, 0.2), seed=i) for i in range(6)]


def move():
    return [Pose(dx=wave(i / 6, 3.0), dy=wave(i / 6 + 0.25, 4.0), tilt=wave(i / 6, 0.04), jaw=0.1, look=0.4, seed=10 + i) for i in range(6)]


def telegraph():
    return [Pose(dy=-3 * k, jaw=0.1 - 0.08 * k, look=0.3 + 0.7 * k, glow=k, seed=20 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def attack():
    seq = [(1.0, 0.3, 0.4, 0.0), (1.0, 0.4, 0.2, 0.3), (0.9, 0.35, 0.0, 0.2), (0.7, 0.25, 0.0, 0.0), (0.5, 0.15, 0.0, 0.0)]
    return [Pose(dy=2 * i, jaw=j, look=lk, flash=f, burst=b, seed=30 + i) for i, (lk, j, f, b) in enumerate(seq)]


def telegraph_special():
    return [Pose(dy=-4 * k, jaw=0.2 + 0.6 * k, look=0.4 + 0.5 * k, burst=0.2 * k, seed=40 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def special():
    return [Pose(dy=-4, jaw=0.9, look=1.0, burst=0.6 + 0.4 * (i % 2), flash=0.15 if i == 0 else 0.0, seed=50 + i) for i in range(4)]


def summon():
    return [Pose(dy=-10 * k, jaw=0.5 * k, look=0.6 + 0.4 * k, burst=0.4 * k, dust=0.3 * k, seed=60 + i) for i, k in enumerate((0.0, 0.4, 0.8, 1.0, 0.8))]


def hurt():
    return [Pose(dx=-4, tilt=-0.1, jaw=0.4, look=0.2, flash=0.55, seed=70), Pose(dx=-2, tilt=-0.05, jaw=0.25, look=0.3, flash=0.2, seed=71)]


def roar():
    seq = [0.2, 0.6, 1.0, 1.0, 0.7, 0.3]
    return [Pose(dy=-6 * k, tilt=-0.06 * k, jaw=k, look=0.5 + 0.5 * k, burst=k if i in (2, 3) else 0.3 * k, flash=0.3 if i == 2 else 0.0, seed=80 + i)
            for i, k in enumerate(seq)]


def death():
    out = []
    for i in range(10):
        t = i / 9
        out.append(Pose(dy=min(40.0, 46 * t * t), tilt=0.3 * t, jaw=0.6 + 0.4 * t, look=0.8 - 1.6 * t, fade=min(1.0, t * 1.2),
                        flash=0.4 if i == 1 else 0.0, burst=0.6 if i == 2 else 0.0, dust=0.8 if i >= 6 else 0.0, seed=90 + i))
    return out


CLIPS = [
    ("idle", idle, dict(fps=6, loop=True)),
    ("move", move, dict(fps=7, loop=True)),
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
    """An eye (a ball of fire in a ring of bone) or the crystal (a pink gem in its setting): idle, hurt, breaking."""
    fw = fh = 32
    if kind == "oeil":
        core = mat("sk_weak_eye", PHASES[1][1], emissive=True, outline=None)
        ramp = [C(h) for h in PHASES[0][1]]
    else:
        core = GEM
        ramp = [C(h) for h in ("#fff4fc", "#ffd0f2", "#ff6ad5", "#9a2a9a")]

    def one(pulse, crack=0.0, scale=1.0, fl=0.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tdisc((16, 16), 11.0 * scale, BONE if kind == "oeil" else ARMOR)
        cv.part(line=False)
        if kind == "oeil":
            cv.tdisc((16, 16), 7.5 * scale, SOCKET)
            cv.tdisc((16, 16), (4.5 + pulse) * scale, core, bias=0.1 + 0.3 * pulse)
            cv.tdisc((15, 15), 2.0 * scale, core, bias=0.6, decal=True)
        else:
            cv.tpoly([(16, 16 - 10 * scale), (16 + 7 * scale, 16), (16, 16 + 10 * scale), (16 - 7 * scale, 16)], core, flat=0.45 + 0.3 * pulse)
            cv.tpoly([(16, 16 - 7 * scale), (16 + 3 * scale, 15), (16, 17)], core, flat=0.95, decal=True)
        if crack:
            cv.tline((9, 11), (20, 22), CRACK, inten=0.7, decal=True)
            cv.tline((21, 8), (14, 18), CRACK, inten=0.7, decal=True)
        return flash(cv.render(outline=True), fl)

    idle_f = [one(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [one(1.0, 1.0, 1.0, 0.6), one(0.8, 1.0)]
    death_f = []
    for i in range(6):
        img = one(1.0, 1.0, 1.0 - 0.15 * i, 0.4) if i < 2 else np.zeros((fh, fw, 4), np.uint8)
        t = (i + 1) / 6
        for k in range(8):
            ang = k / 8 * 2 * math.pi + 0.3
            d = 4 + 11 * t
            _blob(img, int(16 + math.cos(ang) * d), int(16 + math.sin(ang) * d), 1 if i > 3 else 2, ramp[(k + i) % 4] if i < 5 else C("#5e5272"))
        death_f.append(img)
    return pack([("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=T(0.06, 0.1), loop=False)),
                 ("death", death_f, dict(durations=T(0.06, 0.06, 0.08, 0.08, 0.1, 0.12), loop=False))], fw, fh, columns=6)


# ----------------------------------------------------------------------------- its mirrors
FW, FH = 64, 96
OBSIDIAN = mat("sk_obsidian", ["#5a4a78", "#3a2c58", "#241a3c", "#140e24"])


def fx_frames():
    """Its mirrors standing on the ground: dark obsidian on a bone pedestal (mirror), lit pink while a
    player holds it (mirror_lit), and the burst of its gaze thrown back (mirror_flash)."""
    lit = mat("sk_mirror_lit", ["#ffffff", "#ffe0fa", "#ff9ae6", "#ff6ad5"], emissive=True, outline=None)

    def mirror(glow, burst=0.0, seed=0):
        cv = TCanvas(FW, FH)
        cv.part()
        cv.tpoly([(12, FH - 1), (18, FH - 10), (46, FH - 10), (52, FH - 1)], BONE_DK, bevel=1.5, base=0.6)
        cv.part()
        cv.tellipse((32, FH - 46), 18.0, 34.0, ARMOR)
        cv.part(line=False)
        cv.tellipse((32, FH - 46), 14.0, 30.0, lit if glow > 0 else OBSIDIAN, bias=-0.1 + 0.4 * glow)
        for k in range(3):
            cv.tline((24 + 4 * k, FH - 70 + 6 * k), (30 + 4 * k, FH - 58 + 6 * k), lit, inten=0.6 + 0.3 * glow, decal=True)
        for a in (-1, 1):
            cv.part()
            cv.tchain([(32 + 16 * a, FH - 74), (32 + 22 * a, FH - 84), (32 + 18 * a, FH - 92)], [3.0, 2.0, 1.0], HORN)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 700 + seed)
        if glow > 0:
            img = halo(img, C("#ff6ad5"), 0.35 * glow, SEED + 710 + seed)
            for _ in range(int(6 * glow)):
                _blob(img, int(rng.uniform(12, 52)), int(rng.uniform(4, FH - 20)), 1, C("#ffd0f2"))
        if burst > 0:
            sparks(img, (32, FH - 46), 20 + 8 * burst, 14, [C("#ffffff"), C("#ff9ae6"), C("#ff6ad5")], SEED + 720 + seed, squash=1.4)
            img = flash(img, 0.4 * burst)
        return img

    clips = [("mirror", [mirror(0.0, seed=i) for i in range(4)], dict(fps=4, loop=True)),
             ("mirror_lit", [mirror(0.6 + 0.4 * abs(wave(i / 4)), seed=10 + i) for i in range(4)], dict(fps=8, loop=True)),
             ("mirror_flash", [mirror(1.0, 1.0, 20), mirror(1.0, 0.5, 21)], dict(fps=10, loop=True))]
    return pack(clips, FW, FH, columns=6)


# ----------------------------------------------------------------------------- output
def outputs():
    return boss_outputs("skullmaw", draw, CLIPS, [("skullmaw_oeil", weak_frames("oeil"), 32, 32, [16, 28]),
                                                  ("skullmaw_cristal", weak_frames("cristal"), 32, 32, [16, 28]),
                                                  ("skullmaw_fx", fx_frames(), FW, FH, [FW // 2, FH])])


def do_review(path):
    picks = [("Repos", idle()[0]), ("Vol", move()[2]), ("Regard : il se fige", telegraph()[4]), ("Regard laser", attack()[1]),
             ("Onde : la mâchoire s'ouvre", telegraph_special()[4]), ("Onde psychique", special()[0]), ("Pluie d'os", summon()[3]),
             ("Touché", hurt()[0]), ("Rugissement de phase", roar()[2]), ("Mort", death()[5])]
    oeil, _ = weak_frames("oeil")
    cristal, _ = weak_frames("cristal")
    fx, _ = fx_frames()
    extras = [("Points faibles : œil (repos, touché, crevé) x3", oeil, 3), ("Point faible : cristal du front (repos, touché, brisé) x3", cristal, 3),
              ("Miroirs : au repos, tenus par un joueur (allumés), son regard renvoyé (éclat)", fx, 2)]
    return review(path, "SKULLMAW — le crâne qui voit tout (phases : le regard qui traque, le voile psychique, les ténèbres)",
                  [ph[0] for ph in PHASES], picks, draw, extras)


if __name__ == "__main__":
    main("skullmaw", outputs, do_review)
