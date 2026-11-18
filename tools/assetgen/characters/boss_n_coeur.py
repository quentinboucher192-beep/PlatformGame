"""LE CŒUR D'ASTERION, l'étoile captive (the final fight of the clan raid « Asterion », 2.72, docs/RAIDS.md §15).

The cosmic heart of the titan, as drawn on Asterion (environment/raid_asterion.py, part « coeur »: the
golden light in its chest): a star held captive in a heart of violet flesh, crystal arteries and a cage of
bone, its two ventricles bulging low on its flanks and a third, the crown ventricle, on its top. Front
view (symmetric: the game may flip it). Four phase sheets, its look following its (shared) health:
  * 1 « Le cœur qui bat »        sprites/enemies/asterion_coeur.png     the cage closed, the star dim gold;
  * 2 « Les artères »            sprites/enemies/asterion_coeur_p2.png  the arteries swollen and glowing,
    the ribs pulled apart, the star brighter;
  * 3 « La couronne d'étoiles »  sprites/enemies/asterion_coeur_p3.png  a crown of stars turning over it,
    violet cracks of light in its flesh;
  * 4 « Supernova »              sprites/enemies/asterion_coeur_p4.png  collapsing: white-hot, the flesh
    torn, shards and a halo of heat.
Frames 224x224, pivot [112, 212] = bottom-centre of the 133x133 collision box (scale 3: 400x400 in the
world). Clips: idle 6 (a heartbeat), move 6, telegraph 5, attack 5, telegraph_special 5, special 4,
summon 5, hurt 2, roar 6, death 10.

Also its weak points (a ventricle: asterion_ventricule, 48x48: idle 6, hurt 2, death 6), the sheet of its
rules and of its arena (asterion_coeur_fx, 48x144 like the gigaboss kit's, drawn by the raid's combat
screen: closed 1 / open 4 / warn 4 / jet 4 = an artery plate, anchor 4 = a plate lit to contain the
supernova, lance 4 = a star lance falling on a player, blade 4 = an artery sweep, comet 4 = a star charge,
rain 4 = a column of magma, well 4 = a gravity trap, tide 4 = the plasma tide's crest) and the sheet of
the « Supernova d'Asterion » victory effect (sprites/fx/asterion_supernova.png, 96x96, 12 frames: the
heart beats, collapses, bursts into golden and violet rings), drawn by ui/screens/CosmeticFx.

    python3 tools/assetgen/characters/boss_n_coeur.py [--check | --review out.png]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.path[:0] = [HERE, os.path.dirname(HERE)]

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

import gigaboss_kit as kit  # noqa: E402
from boss_a_common import C, mat  # noqa: E402
from boss_l_gigaboss import FX_H, FX_W, GY, Pose, T, _blob, fitted, flash, halo, pack, sparks, trim, wave  # noqa: E402
from raster import TCanvas  # noqa: E402
from sheet import ASSETS  # noqa: E402

W = H = 224
PIVOT = [W // 2, GY]
COLUMNS = 14
SEED = 72601
FLESH = mat("ac_flesh", ["#a46ad8", "#7c48b8", "#5a3194", "#3d2070", "#26134b"])
FLESH_DK = mat("ac_flesh_dk", ["#5a3194", "#3d2070", "#26134b", "#160a30"])
BONE = mat("ac_bone", ["#fbf7ff", "#e2dbea", "#b8aec6", "#8a7c9c", "#5e5272"])
CRYSTAL = mat("ac_crystal", ["#e8fbff", "#9fe8ff", "#58b8e8", "#2e74b0", "#1c4078"])
PHASES = [
    ("Le cœur qui bat", ["#fff2c0", "#ffd27a", "#ff9a3a", "#c85a14"]),
    ("Les artères", ["#ffe8c0", "#ffb070", "#ff7a3a", "#c8401a"]),
    ("La couronne d'étoiles", ["#ffffff", "#e6ccff", "#b46cff", "#6a2cc8"]),
    ("Supernova", ["#ffffff", "#fffbe8", "#ffe6a8", "#ffb050"]),
]
STAR = [mat("ac_star%d" % k, PHASES[k][1], emissive=True, outline=None) for k in range(4)]
VEIN = [mat("ac_vein%d" % k, ["#ffe6a8", "#ffb050", "#ff7a2e", "#d2481a"] if k < 2 else
            (["#f0e0ff", "#c9a8ff", "#9a5aff", "#6a2cc8"] if k == 2 else ["#ffffff", "#fff6dc", "#ffe6a8", "#ffb050"]),
            emissive=True, outline=None) for k in range(4)]
HALO = [C("#ffd27a"), C("#ff9a5a"), C("#b46cff"), C("#ffffff")]
REST = (112.0, 142.0)  # the middle of its star (world [0, -210] from its feet at scale 3)
# Its ventricles (weak points, world from its feet: [-150, -96] [150, -96] [0, -372]) on the canvas.
VENTRICLES = [(-50.0, 180.0 - 142.0), (50.0, 180.0 - 142.0), (0.0, 88.0 - 142.0)]


def center(p):
    return (REST[0] + p.dx, REST[1] + p.dy)


def draw(p, phase):
    """One frame of the heart in `phase` (0..3) for pose `p` (k: beat 0..1, open 0..1 its ribs apart)."""
    cx, cy = center(p)
    cv = TCanvas(W, H, rot=p.tilt, center=(cx, GY))
    beat = max(0.0, min(1.0, p.get("beat", 0.3)))
    opened = max(0.0, min(1.0, p.get("open", 0.15 + 0.25 * phase)))
    swell = 1.0 + 0.05 * beat
    star = STAR[phase]
    # Its aorta and great veins rising from its top, curling out (they hold it in the void).
    for side in (-1, 1):
        cv.part()
        cv.tchain([(cx + 20 * side, cy - 44), (cx + 34 * side, cy - 76), (cx + 58 * side, cy - 90), (cx + 80 * side, cy - 84)],
                  [9.0, 7.5, 6.0, 4.0], FLESH_DK)
        cv.part(line=False)
        cv.tchain([(cx + 20 * side, cy - 46), (cx + 34 * side, cy - 77), (cx + 58 * side, cy - 90)], [3.0, 2.6, 2.0], VEIN[phase], flat=0.5 + 0.12 * phase, decal=True)
    # The body: two great lobes and the point, swelling with each beat.
    cv.part()
    for side in (-1, 1):
        cv.tellipse((cx + 30 * side * swell, cy - 12), 44.0 * swell, 46.0 * swell, FLESH, ang=0.35 * side)
    cv.tpoly([(cx - 66 * swell, cy + 2), (cx + 66 * swell, cy + 2), (cx + 20, cy + 60), (cx, cy + 70), (cx - 20, cy + 60)], FLESH, bevel=2.2, base=0.55)
    # The ventricles bulging on its flanks and its top (its weak points come out of them).
    for vx, vy in VENTRICLES:
        cv.part()
        cv.tellipse((cx + vx, cy + vy), 15.0, 13.0, FLESH_DK)
        cv.part(line=False)
        cv.tellipse((cx + vx, cy + vy), 8.0 + 2.0 * beat, 7.0 + 2.0 * beat, VEIN[phase], bias=0.1 * phase, decal=True)
    # The star in its middle, seen through the parted flesh.
    cv.part(line=False)
    r = 22.0 + 6.0 * beat + 4.0 * opened + 3.0 * phase
    cv.tellipse((cx, cy), r * 1.05, r, FLESH_DK, decal=True)
    cv.tdisc((cx, cy), r * 0.82, star, bias=-0.1 + 0.3 * beat + 0.12 * phase - 0.8 * p.fade, decal=True)
    cv.tdisc((cx - 4, cy - 4), r * 0.36, star, bias=0.45 + 0.2 * beat - 0.8 * p.fade, decal=True)
    for k in range(6):  # its rays
        a = k / 6 * 2 * math.pi + 0.3 * phase
        cv.tline((cx + math.cos(a) * r * 0.5, cy + math.sin(a) * r * 0.5), (cx + math.cos(a) * r * 0.95, cy + math.sin(a) * r * 0.95),
                 star, inten=0.9, decal=True)
    # Crystal arteries over the flesh (swollen and lit from its 2nd phase).
    for line in (((-58, -20), (-40, -4), (-46, 22)), ((58, -20), (40, -4), (46, 22)), ((-30, -48), (-12, -36)), ((30, -48), (12, -36)),
                 ((-24, 40), (-8, 54)), ((24, 40), (8, 54))):
        for a, b in zip(line, line[1:]):
            mt = CRYSTAL if phase == 0 else VEIN[phase]
            cv.tcapsule((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), 1.6 + 0.6 * min(phase, 2), 1.2, mt, flat=0.4 + 0.15 * phase, decal=True)
    # The cage of bone: four pairs of ribs from its top and bottom, parted more at each phase, broken at the last.
    if phase < 3:
        for i, (y0, bend) in enumerate(((cy - 40, -1), (cy - 20, -1), (cy + 18, 1), (cy + 38, 1))):
            for side in (-1, 1):
                spread = 1.0 + 0.7 * opened
                pts = [(cx + 8 * side, y0), (cx + 36 * spread * side, y0 + 4 * bend), (cx + 58 * spread * side, y0 + 14 * bend)]
                cv.part()
                cv.tchain(pts, [3.2, 2.8, 1.8], BONE)
        cv.part()
        cv.tpoly([(cx - 5, cy - 52), (cx + 5, cy - 52), (cx + 4, cy - 24), (cx - 4, cy - 24)], BONE, bevel=1.0, base=0.62)
    else:
        for i, (y0, bend) in enumerate(((cy - 40, -1), (cy + 38, 1))):
            for side in (-1, 1):
                cv.part()
                cv.tchain([(cx + 8 * side, y0), (cx + 24 * side, y0 + 3 * bend)], [3.0, 2.0], BONE)
                cv.tdisc((cx + 24 * side, y0 + 3 * bend), 1.8, VEIN[3], bias=0.3, decal=True)
    # Cracks of light (from its 3rd phase).
    if phase >= 2:
        for a, b in (((-50, -30), (-30, -10)), ((44, -34), (26, -8)), ((-36, 28), (-16, 40)), ((40, 26), (20, 44)), ((-8, -60), (6, -44))):
            cv.tline((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), VEIN[phase], inten=0.95, decal=True)
    img = cv.render(outline=True)
    return post(img, p, phase)


def post(img, p, phase):
    cx, cy = center(p)
    out = img
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 131)
    if p.fade < 0.6:
        out = halo(out, HALO[phase], 0.10 + 0.08 * phase, SEED + p.seed + 41 * phase, rings=1 + (phase >= 2))
    if phase == 2 and p.fade < 0.5:
        # Its crown of stars, turning over it.
        for k in range(7):
            a = k / 7 * 2 * math.pi + p.seed * 0.21
            sx, sy = cx + math.cos(a) * 92, cy - 70 + math.sin(a) * 14
            _blob(out, int(sx), int(sy), 2, C(PHASES[2][1][0]))
            _blob(out, int(sx), int(sy), 1, C("#ffffff"))
    if phase == 3 and p.fade < 0.5:
        for _ in range(26):
            ang, d = rng.uniform(0, 2 * math.pi), rng.uniform(70, 104)
            _blob(out, int(cx + math.cos(ang) * d), int(cy + math.sin(ang) * d * 0.9), 1 + int(rng.uniform(0, 2)), C(PHASES[3][1][int(rng.uniform(0, 3))]))
    if p.burst > 0:
        sparks(out, (cx, cy), 56 + 60 * p.burst, int(14 + 18 * p.burst), [C(h) for h in PHASES[phase][1][:3]], SEED + p.seed)
    if p.dust > 0:
        for _ in range(int(20 * p.dust)):
            _blob(out, int(rng.uniform(cx - 80, cx + 80)), int(rng.uniform(cy - 60, cy + 70)), 1 + int(rng.uniform(0, 2)), C("#8a7c9c"))
    out = flash(out, p.flash)
    return trim(out)


# ----------------------------------------------------------------------------- clips
def idle():
    beats = [0.1, 1.0, 0.4, 0.85, 0.2, 0.1]  # lub-dub, then rest
    return [Pose(dy=-wave(i / 6, 2.0), beat=b, seed=i) for i, b in enumerate(beats)]


def move():
    return [Pose(dy=-abs(wave(i / 6, 3.0)), tilt=wave(i / 6, 0.02), beat=0.3 + 0.3 * abs(wave(i / 6)), seed=10 + i) for i in range(6)]


def telegraph():
    return [Pose(dy=3 * k, beat=0.3 + 0.7 * k, open=0.2 + 0.5 * k, seed=20 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def attack():
    seq = [(1.0, 0.6, 0.3), (1.0, 1.0, 0.2), (0.7, 0.6, 0.0), (0.5, 0.3, 0.0), (0.3, 0.1, 0.0)]
    return [Pose(dy=-5 if i == 1 else 0, beat=b, burst=bu, flash=f, open=0.5, seed=30 + i) for i, (b, bu, f) in enumerate(seq)]


def telegraph_special():
    return [Pose(dy=4 * k, beat=0.2 + 0.8 * k, open=0.3 + 0.6 * k, burst=0.2 * k, seed=40 + i) for i, k in enumerate((0.0, 0.3, 0.6, 0.85, 1.0))]


def special():
    return [Pose(beat=1.0 - 0.3 * (i % 2), open=0.9, burst=0.6 + 0.4 * (i % 2), flash=0.15 if i == 0 else 0.0, seed=50 + i) for i in range(4)]


def summon():
    return [Pose(dy=-6 * k, beat=0.4 + 0.6 * k, open=0.5 * k, burst=0.4 * k, seed=60 + i) for i, k in enumerate((0.0, 0.4, 0.8, 1.0, 0.8))]


def hurt():
    return [Pose(dx=-3, tilt=-0.05, beat=0.8, flash=0.55, seed=70), Pose(dx=-1, tilt=-0.02, beat=0.5, flash=0.2, seed=71)]


def roar():
    seq = [0.2, 0.6, 1.0, 1.0, 0.7, 0.3]
    return [Pose(dy=-6 * k, beat=k, open=0.9 * k, burst=k if i in (2, 3) else 0.2 * k, flash=0.3 if i == 2 else 0.0, seed=80 + i) for i, k in enumerate(seq)]


def death():
    out = []
    for i in range(10):
        t = i / 9
        out.append(Pose(dy=min(16.0, 20 * t), tilt=0.1 * t, beat=max(0.0, 1.0 - 1.4 * t), open=0.6 + 0.4 * t, fade=min(1.0, t * 1.2),
                        flash=0.5 if i in (1, 3) else 0.0, burst=1.0 if i in (2, 4) else 0.0, dust=0.8 if i >= 6 else 0.0, seed=90 + i))
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


# ----------------------------------------------------------------------------- its weak point
def ventricle_frames():
    """A ventricle bared: a chamber of flesh around a gold light (idle), hurt, bursting."""
    fw = fh = 48

    def one(pulse, crack=0.0, scale=1.0, fl=0.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tellipse((24, 26), 15.0 * scale, 13.0 * scale, FLESH)
        cv.part(line=False)
        r = (8.0 + 2.0 * pulse) * scale
        cv.tellipse((24, 26), r, r * 0.9, STAR[0], bias=-0.05 + 0.3 * pulse, decal=True)
        cv.tdisc((22, 24), r * 0.4, STAR[0], bias=0.5, decal=True)
        for k in range(3):
            a = -2.4 + 0.8 * k
            cv.tcapsule((24 + math.cos(a) * 11 * scale, 26 + math.sin(a) * 9 * scale), (24 + math.cos(a) * 18 * scale, 26 + math.sin(a) * 15 * scale),
                        2.4, 1.4, CRYSTAL)
        if crack:
            cv.tline((16, 18), (30, 34), VEIN[3], inten=0.9, decal=True)
            cv.tline((32, 16), (24, 26), VEIN[3], inten=0.9, decal=True)
        img = cv.render(outline=True)
        img = halo(img, C("#ffd27a"), 0.3 + 0.3 * pulse, SEED + 600 + int(pulse * 10))
        return flash(img, fl)

    idle_f = [one(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [one(1.0, 1.0, 1.0, 0.6), one(0.8, 1.0)]
    ramp = [C(h) for h in PHASES[0][1]]
    death_f = []
    for i in range(6):
        img = one(1.0, 1.0, 1.0 - 0.15 * i, 0.4) if i < 2 else np.zeros((fh, fw, 4), np.uint8)
        t = (i + 1) / 6
        for k in range(12):
            ang = k / 12 * 2 * math.pi + 0.3
            d = 5 + 17 * t
            _blob(img, int(24 + math.cos(ang) * d), int(25 + math.sin(ang) * d), 1 if i > 3 else 2, ramp[(k + i) % 4] if i < 5 else C("#5e5272"))
        death_f.append(img)
    return pack([("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=T(0.06, 0.1), loop=False)),
                 ("death", death_f, dict(durations=T(0.06, 0.06, 0.08, 0.08, 0.1, 0.12), loop=False))], fw, fh, columns=6)


# ----------------------------------------------------------------------------- its rules and its arena
FW, FH = FX_W, FX_H  # the kit's effects frames (48x144, bottom-centre pivot)


def fx_frames():
    """The artery plates (closed, open to hold, plasma gathering, a jet), the anchor lit for the
    supernova, and what falls on the players: a star lance, an artery sweep, a star charge, a column of
    magma, a gravity trap, the plasma tide's crest."""
    gold = [C("#ffffff"), C("#fff2c0"), C("#ffd27a"), C("#ff9a3a")]
    plasma = [C("#ffffff"), C("#ffe8c0"), C("#ffb070"), C("#ff7a3a")]
    violet = [C("#ffffff"), C("#e6ccff"), C("#b46cff"), C("#6a2cc8")]
    lit = mat("ac_plate_lit", ["#ffffff", "#ffe8a8", "#ffc060", "#ff7a2e"], emissive=True, outline=None)
    cx = FW // 2

    def plate(glow, seed=0, anchor=False):
        cv = TCanvas(FW, FH)
        cv.part()
        cv.tpoly([(cx - 20, FH - 1), (cx - 15, FH - 10), (cx + 15, FH - 10), (cx + 20, FH - 1)], FLESH_DK, bevel=1.5, base=0.6)
        cv.part()
        cv.tchain([(cx, FH - 10), (cx - 3, FH - 40), (cx + 2, FH - 70)], [7.0, 5.5, 3.6], FLESH)  # the artery
        cv.part(line=False)
        cv.tchain([(cx, FH - 11), (cx - 3, FH - 40), (cx + 2, FH - 68)], [2.6, 2.2, 1.4], lit if glow > 0 else CRYSTAL, flat=0.4 + 0.5 * glow, decal=True)
        cv.part()
        cv.tellipse((cx, FH - 12), 12.0, 4.0, BONE)
        img = cv.render(outline=True)
        if glow > 0:
            img = halo(img, C("#ffd27a" if anchor else "#ffb070"), 0.3 * glow + (0.2 if anchor else 0.0), SEED + 900 + seed)
        if anchor:
            for k in range(8):
                a = k / 8 * 2 * math.pi + seed * 0.4
                _blob(img, int(cx + math.cos(a) * 18), int(FH - 40 + math.sin(a) * 30), 1, gold[k % 3])
        return img

    def gather(k, seed):
        img = plate(0.3, seed)
        rng = np.random.RandomState(SEED + 905 + seed)
        for _ in range(int(8 + 24 * k)):
            _blob(img, int(rng.uniform(cx - 10, cx + 10)), int(rng.uniform(FH - 40 - 60 * k, FH - 12)), 1, plasma[int(rng.uniform(0, 4))])
        return img

    def column(i, cols, top, width, wobble=1.5):
        img = np.zeros((FH, FW, 4), np.uint8)
        for y in range(top, FH - 2):
            t = (y - top) / max(1, FH - 2 - top)
            half = width * (0.5 + 0.5 * t) + wobble * math.sin(y * 0.4 + i)
            for x in range(int(cx - half), int(cx + half) + 1):
                if 0 <= x < FW:
                    d = abs(x - cx) / max(1.0, half)
                    img[y, x] = cols[0] if d < 0.3 else cols[1] if d < 0.6 else cols[2]
        return img

    def jet(i):
        img = plate(0.6, 30 + i)
        col = column(i, plasma, 30 + 8 * (i % 2), 8)
        a = col[:, :, 3] > 0
        img[a] = col[a]
        return img

    def lance(i):
        """A lance of starlight falling on its mark: its head, a long trail."""
        img = column(i, gold, 4, 4.5, wobble=0.8)
        rng = np.random.RandomState(SEED + 930 + i)
        for _ in range(12):
            _blob(img, int(cx + rng.uniform(-12, 12)), int(rng.uniform(FH - 50, FH - 4)), 1, gold[int(rng.uniform(0, 3))])
        _blob(img, cx, FH - 6, 6, gold[0])
        return img

    def blade(i):
        """An artery sweep: a crescent of plasma running along a storey (drawn going east)."""
        img = np.zeros((FH, FW, 4), np.uint8)
        for y in range(FH - 56, FH - 2):
            t = (FH - 2 - y) / 54.0
            x1 = int(cx + 18 - 14 * (t - 0.5) ** 2 * 4 + 2 * math.sin(i * 1.4 + y * 0.3))
            x0 = int(x1 - 6 - 10 * (1 - abs(t - 0.5) * 2))
            for x in range(max(0, x0), min(FW, x1)):
                d = (x1 - x) / max(1, x1 - x0)
                img[y, x] = plasma[0] if d < 0.25 else plasma[2] if d < 0.6 else plasma[3]
        return img

    def comet(i):
        """A star charge: a burning head and its tail (drawn going east)."""
        img = np.zeros((FH, FW, 4), np.uint8)
        hy = FH - 30
        for x in range(0, FW):
            t = x / (FW - 1)
            half = 2 + 16 * t ** 2
            for y in range(int(hy - half), int(hy + half) + 1):
                d = abs(y - hy) / max(1.0, half)
                if d < 1.0 and (x + y + i) % (1 if t > 0.5 else 2) == 0:
                    img[y, x] = violet[0] if d < 0.3 and t > 0.6 else violet[1] if d < 0.6 else violet[2] if t > 0.3 else violet[3]
        _blob(img, FW - 12, hy, 9, violet[1])
        _blob(img, FW - 12, hy, 5, violet[0])
        return img

    def rain(i):
        img = column(i, [C("#ffe6a8"), C("#ff9a3a"), C("#c8401a")], 0, 9, wobble=2.5)
        rng = np.random.RandomState(SEED + 950 + i)
        for _ in range(10):
            _blob(img, int(cx + rng.uniform(-18, 18)), int(FH - rng.uniform(4, 30)), 2, C("#ffb050"))
        return img

    def well(i):
        img = np.zeros((FH, FW, 4), np.uint8)
        for k in range(4):
            r = 6 + ((k * 5 + i * 2) % 20)
            for a in range(36):
                ang = a / 36 * 2 * math.pi + i * 0.3
                x, y = int(cx + math.cos(ang) * r), int(FH - 24 + math.sin(ang) * r * 0.45)
                if 0 <= x < FW and 0 <= y < FH:
                    img[y, x] = violet[k % 4]
        _blob(img, cx, FH - 24, 4, C("#160a30"))
        return img

    def tide(i):
        img = np.zeros((FH, FW, 4), np.uint8)
        rng = np.random.RandomState(SEED + 970 + i)
        for x in range(FW):
            hgt = int(28 + 8 * math.sin((x + i * 12) * 0.13) + 4 * math.sin(x * 0.41 + i))
            for y in range(FH - hgt, FH):
                d = (FH - y) / max(1, hgt)
                img[y, x] = plasma[0] if d > 0.85 else plasma[1] if d > 0.6 else plasma[2] if d > 0.3 else plasma[3]
        for _ in range(8):
            _blob(img, int(rng.uniform(4, FW - 4)), int(FH - 40 - rng.uniform(0, 14)), 1, plasma[int(rng.uniform(0, 2))])
        return img

    clips = [("closed", [plate(0.0)], dict(fps=1, loop=True)),
             ("open", [plate(0.5 + 0.5 * abs(wave(i / 4)), i) for i in range(4)], dict(fps=8, loop=True)),
             ("warn", [gather(0.3 + 0.2 * i, 10 + i) for i in range(4)], dict(fps=10, loop=True)),
             ("jet", [jet(i) for i in range(4)], dict(fps=12, loop=True)),
             ("anchor", [plate(1.0, 40 + i, anchor=True) for i in range(4)], dict(fps=8, loop=True)),
             ("lance", [lance(i) for i in range(4)], dict(fps=12, loop=True)),
             ("blade", [blade(i) for i in range(4)], dict(fps=12, loop=True)),
             ("comet", [comet(i) for i in range(4)], dict(fps=12, loop=True)),
             ("rain", [rain(i) for i in range(4)], dict(fps=12, loop=True)),
             ("well", [well(i) for i in range(4)], dict(fps=10, loop=True)),
             ("tide", [tide(i) for i in range(4)], dict(fps=8, loop=True))]
    return pack(clips, FW, FH, columns=12)


# ----------------------------------------------------------------------------- the victory effect
def supernova_frames():
    """« Supernova d'Asterion » (a victory effect, 96x96): the heart beats twice, collapses to a point,
    then bursts into golden and violet rings and star shards."""
    fw = fh = 96
    frames = []
    for i in range(12):
        img = np.zeros((fh, fw, 4), np.uint8)
        if i < 5:
            s = [0.55, 0.7, 0.55, 0.72, 0.3][i]
            cv = TCanvas(fw, fh)
            cv.part()
            for side in (-1, 1):
                cv.tellipse((48 + 9 * side * s, 46), 14.0 * s, 15.0 * s, FLESH, ang=0.35 * side)
            cv.tpoly([(48 - 21 * s, 50), (48 + 21 * s, 50), (48, 50 + 22 * s)], FLESH, bevel=1.4, base=0.55)
            cv.part(line=False)
            cv.tdisc((48, 48), 8.0 * s, STAR[0], bias=0.3 + 0.1 * i, decal=True)
            img = cv.render(outline=True)
            img = halo(img, C("#ffd27a"), 0.3 + 0.1 * i, SEED + 700 + i)
        else:
            t = (i - 5) / 6.0
            yy, xx = np.mgrid[0:fh, 0:fw].astype(np.float32)
            rr = np.hypot(xx - 47.5, yy - 47.5)
            for r0, col, wdt in ((6 + 40 * t, (255, 210, 122), 3.0), (4 + 30 * t, (180, 108, 255), 2.0), (2 + 18 * t, (255, 255, 255), 1.5)):
                ring = np.abs(rr - r0) < wdt
                a = int(255 * (1.0 - t) ** 0.7)
                img[ring] = (col[0], col[1], col[2], a)
            if i < 8:
                core = rr < (10 - 3 * (i - 5))
                img[core] = (255, 255, 255, 255)
            rng = np.random.RandomState(SEED + 800 + i)
            for k in range(14):
                ang = k / 14 * 2 * math.pi + rng.uniform(-0.15, 0.15)
                d = 8 + 38 * t + rng.uniform(-3, 3)
                _blob(img, int(48 + math.cos(ang) * d), int(48 + math.sin(ang) * d), 1 if t > 0.6 else 2, C("#fff2c0" if k % 2 else "#e6ccff"))
        frames.append(img)
    return pack([("burst", frames, dict(durations=T(0.16, 0.12, 0.16, 0.12, 0.2, 0.08, 0.1, 0.12, 0.14, 0.16, 0.2, 0.3), loop=False))],
                fw, fh, columns=12)


# ----------------------------------------------------------------------------- output
def outputs():
    out = []
    for phase in range(4):
        img, meta = pack([(clip, [draw(p, phase) for p in fn()], kw) for clip, fn, kw in CLIPS], W, H, COLUMNS)
        out += kit.sheet_entry("asterion_coeur" if phase == 0 else "asterion_coeur_p%d" % (phase + 1), img, meta, W, H, PIVOT)
    out += kit.sheet_entry("asterion_ventricule", *ventricle_frames(), 48, 48, [24, 44])
    out += kit.sheet_entry("asterion_coeur_fx", *fx_frames(), FW, FH, [FW // 2, FH])
    out += kit.sheet_entry("asterion_supernova", *supernova_frames(), 96, 96, [48, 48], folder="sprites/fx")
    return out


def do_review(path):
    """The contact sheet (French captions): one frame of each clip of each phase, the ventricle, the sheet
    of its rules and its arena, the victory effect."""
    picks = [("Repos (battement)", idle()[1]), ("Respiration", move()[2]), ("Il se gonfle", telegraph()[4]), ("Battement fort", attack()[1]),
             ("Onde stellaire (préparation)", telegraph_special()[4]), ("Onde stellaire", special()[0]), ("Engeances", summon()[3]),
             ("Touché", hurt()[0]), ("Rugissement de phase", roar()[2]), ("Mort", death()[5])]
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 12)
    big = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 18)
    cell, label, per, head, top = W, 22, 5, 26, 34
    rows = (len(picks) + per - 1) // per
    vent, _ = ventricle_frames()
    fx, _ = fx_frames()
    nova, _ = supernova_frames()
    extras = [("Ventricule (repos, touché, brisé) x2", vent, 2),
              ("Artère (repos, à tenir, plasma qui monte, jet), ancre de la supernova, lance stellaire, balayage, charge stellaire, pluie de magma, "
               "piège de gravité, marée de plasma", fx, 1),
              ("Effet de victoire « Supernova d'Asterion » (12 images)", nova, 1)]
    extra_h = sum(label + img.shape[0] * scale + 10 for _, img, scale in extras)
    sheet = Image.new("RGBA", (per * cell, top + 4 * (head + rows * (cell + label)) + extra_h), (20, 16, 30, 255))
    d = ImageDraw.Draw(sheet)
    d.text((6, 6), fitted(d, "LE CŒUR D'ASTERION — l'étoile captive (quatre phases, ses ventricules, son arène, sa supernova)", big, sheet.width - 12),
           fill=(255, 236, 200, 255), font=big)
    for k in range(4):
        y0 = top + k * (head + rows * (cell + label))
        d.text((6, y0 + 4), "Phase %d · %s" % (k + 1, PHASES[k][0]), fill=(201, 168, 255, 255), font=big)
        for i, (caption, pose) in enumerate(picks):
            x, y1 = (i % per) * cell, y0 + head + (i // per) * (cell + label)
            sheet.alpha_composite(Image.fromarray(draw(pose, k), "RGBA"), (x, y1 + label))
            d.text((x + 4, y1 + 5), fitted(d, caption, font, cell - 8), fill=(255, 220, 170, 255), font=font)
    y = top + 4 * (head + rows * (cell + label))
    for caption, img, scale in extras:
        d.text((4, y + 5), fitted(d, caption, font, sheet.width - 8), fill=(255, 220, 170, 255), font=font)
        pic = Image.fromarray(img, "RGBA")
        if scale != 1:
            pic = pic.resize((img.shape[1] * scale, img.shape[0] * scale), Image.NEAREST)
        if pic.width > sheet.width - 8:
            pic = pic.resize((sheet.width - 8, max(1, pic.height * (sheet.width - 8) // pic.width)), Image.NEAREST)
        sheet.alpha_composite(pic, (4, y + label))
        y += label + img.shape[0] * scale + 10
    sheet.save(path)
    return path


if __name__ == "__main__":
    kit.main(sys.argv, outputs, do_review, "asterion_coeur", "boss_n_coeur.py")
