"""PYROCLAST, la main de magma (gigaboss of the clan raid « Asterion », 2.67, docs/RAIDS.md).

The magma hand of the titan's right arm: a colossal hand of basalt plates that walks on its four
fingertips like a crab, palm down, knuckles up, its torn wrist trailing behind it (a stump of
glowing magma, the titan's arm cut off), magma running in the cracks between its plates.  Side view
facing right (the game flips it).  It is a walker with the golem's moves (game/enemies/
GolemAttacks.cpp): it curls into a fist and rolls (« Poing roulant »), leaps out of sight and falls
fist first (« Poing météore »), pounds the ground (« Secousse »), slams (« Coup de poing »).

Three phase sheets (one per phase of data/enemies/pyroclast.json), each drawn with its own magma
ramp and its own form:
  * 1 « Poing de magma »       sprites/enemies/pyroclast.png     orange magma, bare plates;
  * 2 « Carapace de basalte »  sprites/enemies/pyroclast_p2.png  a carapace of thick grey plates over
    the back of the hand, its three vents glowing yellow on the knuckles (what the players close);
  * 3 « Cœur de magma »        sprites/enemies/pyroclast_p3.png  plates torn off, the white-hot core
    bare in the back of the hand, magma dripping, a heat halo.
Frames 224x224, pivot [112, 212] = bottom-centre of the 88x120 collision box (x 68..156, y 92..212);
drawn at scale 1.25 in game (110x150 box).  Clips (packed several per row): idle 8, telegraph 6,
walk 8 (= move, footsteps f0 f4), attack 5 (slam, `impact` f2), telegraph_special 6, special 4,
hurt 2, summon 6, roar 8, death 14, curl 6, roll 8, uncurl 4, dizzy 6, crouch 4, leap 3, fall 2,
land 5 (`impact` f0), quake 4.

Also:
  * its weak point (data/enemies/pyroclast_noyau.json): sprites/enemies/pyroclast_noyau.png, 32x32
    frames, idle 6 / hurt 2 / death 6;
  * its vents and its fists of magma (drawn by the raid's combat screen over the arena):
    sprites/enemies/pyroclast_vents.png, 48x144 frames, closed 1 / open 4 / warn 4 / jet 4 / fist 2, pivot [24, 144].

Deterministic (fixed seeds): `--check` renders everything twice and compares with the files on disk
(tests/test_raid.cpp); `--review out.png` writes a labelled contact sheet.
    python3 tools/assetgen/characters/boss_k_pyroclast.py [--check | --review out.png]
"""
import io
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.path[:0] = [HERE, os.path.dirname(HERE)]

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

from boss_a_common import C, mat  # noqa: E402
from raster import TCanvas, ik2  # noqa: E402
from sheet import ASSETS, dumps  # noqa: E402

W, H = 224, 224
GY = 212
PIVOT = [112, GY]
SEED = 67013

# The magma of each phase (light -> dark), from Asterion's own MOLTEN ramp (environment/raid_asterion.py):
# hotter at each phase, almost incandescent at the 3rd.
PHASES = [
    ("Poing de magma", ["#ffe6a8", "#ffb050", "#ff7a2e", "#d2481a", "#8e2210"]),
    ("Carapace de basalte", ["#fff2c8", "#ffd27a", "#ffa040", "#ff7a2e", "#d2481a"]),
    ("Cœur de magma", ["#ffffff", "#fff6dc", "#ffe6a8", "#ffb050", "#ff7a2e"]),
]
# The rock of Asterion's right arm: warm basalt for the fist (its BASALT), the dark purple-blue
# rock of the titan for the wrist and the carapace (its SKIN and HORN).
BASALT = mat("py_basalt", ["#6a3c2c", "#523026", "#3c2320", "#2a181b", "#1a0f13"])
BASALT_BK = mat("py_basalt_bk", ["#4a2c24", "#36201e", "#26161a", "#180d11"])
WRIST = mat("py_wrist", ["#6a3dab", "#4b2c8a", "#35206b", "#24164f", "#170f36"])
CRUST = mat("py_crust", ["#64508e", "#44306a", "#2c1c4a", "#1a1030", "#0c0716"])
COOL = mat("py_cool", ["#5c3428", "#422622", "#2c1a1c", "#1c1014"])
# How hot the rock itself glows at each phase (its pixels pulled toward the magma, the halo).
HEAT = [0.06, 0.14, 0.3]
DUST = [C("#a89a8c"), C("#857a6e"), C("#655c54")]


def magma(phase):
    return mat("py_magma%d" % phase, PHASES[phase][1], emissive=True, outline=None)


def core(phase):
    return mat("py_core%d" % phase, ["#ffffff"] + PHASES[phase][1][:3], emissive=True, outline=None)


# ----------------------------------------------------------------------------- pose
class Pose:
    """The hand's pose: where it is (dx, dy from its rest place), its tilt, how far its fingers curl
    (0: legs on the ground, 1: a fist), each finger's lift, its spread, its magma's glow (-1..1),
    the roll of a fist, a squash, a flash."""

    def __init__(self, **k):
        self.dx = k.get("dx", 0.0)
        self.dy = k.get("dy", 0.0)
        self.tilt = k.get("tilt", 0.0)
        self.curl = k.get("curl", 0.0)
        self.lift = list(k.get("lift", (0.0, 0.0, 0.0, 0.0)))
        self.reach = list(k.get("reach", (0.0, 0.0, 0.0, 0.0)))
        self.spread = k.get("spread", 1.0)
        self.glow = k.get("glow", 0.0)
        self.roll = k.get("roll", 0.0)
        self.squash = k.get("squash", 1.0)
        self.flash = k.get("flash", 0.0)
        self.cool = k.get("cool", 0.0)      # death: the magma going out (0..1)
        self.dust = k.get("dust", 0.0)      # dust at the fingertips / impact (0..1)
        self.burst = k.get("burst", 0.0)    # a ring of magma thrown out (roar, impact)
        self.drops = k.get("drops", 0.0)    # magma drops falling around (summon)
        self.seed = k.get("seed", 0)


REST = (112.0, 124.0)   # the centre of the back of the hand at rest
FINGERS = [             # knuckle (from the centre), ground target x (from the centre), lengths
    ((34.0, 10.0), 58.0, 38.0, 44.0),   # index (near side, front)
    ((18.0, 16.0), 26.0, 40.0, 46.0),   # middle (near side)
    ((26.0, 6.0), 44.0, 38.0, 42.0),    # ring (far side)
    ((10.0, 10.0), 8.0, 34.0, 38.0),    # little (far side)
]
THUMB = ((-30.0, 18.0), -40.0, 30.0, 34.0)


def hand_center(p):
    return (REST[0] + p.dx, REST[1] + p.dy)


def draw_hand(p, phase):
    """One frame of the hand in `phase` (0..2) for pose `p`."""
    cx, cy = hand_center(p)
    cv = TCanvas(W, H, rot=p.tilt + (p.roll if p.curl > 0.7 else 0.0), center=(cx, cy), scale=(1.0 / max(0.6, p.squash) ** 0.3, p.squash))
    mg = magma(phase)
    if p.cool > 0:
        mg = mat("py_magma_cool%d_%d" % (phase, int(p.cool * 10)), _cool_ramp(phase, p.cool), emissive=p.cool < 0.85, outline=None)
    curl = max(0.0, min(1.0, p.curl))
    # The fingers: far ones first (behind), then the hand, then the near ones.
    def finger(k, near):
        (kx, ky), gx, l1, l2 = FINGERS[k]
        root = (cx + kx * p.spread, cy + ky)
        ground = (cx + gx * p.spread + p.reach[k], GY - 3 - p.lift[k])
        fist = (cx + kx * 0.6, cy + 30 + ky * 0.3)
        tx = ground[0] + (fist[0] - ground[0]) * curl
        ty = ground[1] + (fist[1] - ground[1]) * curl
        # In the canvas's own space (it is tilted around the centre): undo the tilt for the target.
        tgt = _untilt((tx, ty), (cx, cy), p.tilt)
        mid = ik2(root, tgt, l1 * (1 - 0.35 * curl), l2 * (1 - 0.35 * curl), bend=-1.0)
        m = BASALT if near else BASALT_BK
        cv.part()
        cv.tcapsule(root, mid, 12.5 if near else 11.0, 10.5 if near else 9.5, m)
        cv.tcapsule(mid, tgt, 10.5 if near else 9.5, 7.0, m)
        cv.tdisc(mid, 9.5 if near else 8.5, m, bias=0.06)
        # A molten fingertip, magma running along the finger and glowing in its joints.
        cv.tdisc(tgt, 5.0, mg, bias=0.05 + 0.15 * p.glow)
        cv.tcapsule(root, mid, 1.8, 1.4, mg, flat=0.5 + 0.25 * p.glow, decal=True)
        cv.tcapsule(mid, tgt, 1.4, 1.0, mg, flat=0.4 + 0.25 * p.glow, decal=True)
        cv.tdisc(mid, 4.0 if near else 3.0, mg, bias=0.1 + 0.2 * p.glow, decal=True)
        cv.tdisc(root, 3.0 if near else 2.4, mg, bias=0.15 + 0.2 * p.glow, decal=True)
    finger(3, False)
    finger(2, False)
    # The thumb (behind, toward the back).
    (kx, ky), gx, l1, l2 = THUMB
    root = (cx + kx, cy + ky)
    tgt = _untilt((cx + gx - curl * 10, GY - 4 - curl * 50), (cx, cy), p.tilt)
    mid = ik2(root, tgt, l1, l2, bend=1.0)
    cv.part()
    cv.tcapsule(root, mid, 10.0, 8.5, BASALT_BK)
    cv.tcapsule(mid, tgt, 8.5, 6.0, BASALT_BK)
    cv.tdisc(tgt, 4.5, mg, bias=0.05 + 0.15 * p.glow)
    cv.tdisc(mid, 2.6, mg, bias=0.1 + 0.2 * p.glow, decal=True)
    # The torn wrist: a thick stump trailing up and back, its cut a disc of magma.
    cv.part()
    w0, w1 = (cx - 36, cy - 4), (cx - 62, cy - 30)
    cv.tcapsule(w0, w1, 22.0, 18.0, WRIST)
    cv.tcapsule((w0[0] + 4, w0[1] - 6), (w1[0] + 6, w1[1] - 4), 2.0, 1.6, mg, flat=0.45 + 0.25 * p.glow, decal=True)
    cv.tellipse((w1[0] - 4, w1[1] - 3), 9.0, 15.0, mg, ang=-0.75, bias=0.12 + 0.2 * p.glow)
    cv.tellipse((w1[0] - 5, w1[1] - 4), 4.0, 8.0, core(phase), ang=-0.75, bias=0.2 * p.glow)
    # The back of the hand: a broad dome of plates.
    cv.part()
    back = [(-44, -6), (-30, -30), (-2, -40), (30, -36), (48, -18), (50, 6), (34, 22), (-8, 26), (-40, 16)]
    cv.tpoly([(cx + x, cy + y) for x, y in back], BASALT, bevel=2.2, base=0.58)
    # Knuckles (four domes along the front top), the near ones brighter.
    for i, (kx, ky, r) in enumerate(((40, -20, 11), (26, -32, 12), (10, -36, 12), (-6, -36, 11))):
        cv.part(line=1)
        cv.tdisc((cx + kx, cy + ky), r, BASALT, bias=0.05 if i < 2 else -0.03)
        # Molten knuckles (Asterion's fist): a glowing cap on each.
        cv.tellipse((cx + kx - 1, cy + ky - r * 0.35), r * 0.55, r * 0.4, mg, bias=0.05 + 0.2 * p.glow, decal=True)
    # Wide cracks of magma between the plates and across the knuckles (fixed lines, wider and
    # brighter at each phase), a bright core line in each.
    rng = np.random.RandomState(SEED + phase)
    cracks = [((-34, -2), (-12, -14), (8, -10), (22, -20)), ((-20, 12), (2, 4), (24, 10), (44, 2)), ((14, -24), (20, -8), (38, 0)),
              ((-12, -28), (-6, -16), (-18, -4)), ((30, -28), (36, -14), (46, -10)), ((-2, -40), (6, -30), (18, -36)),
              ((32, -34), (40, -26))]
    width = 1.6 + 0.5 * phase
    for line in cracks:
        for a, b in zip(line, line[1:]):
            cv.tcapsule((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), width, width * 0.8, mg, flat=0.42 + 0.25 * p.glow, decal=True)
            cv.tline((cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), mg, inten=0.8 + 0.2 * p.glow, decal=True)
    # Magma seeping along the lower rim of the hand (the palm glows under it).
    cv.tchain([(cx - 38, cy + 14), (cx - 10, cy + 24), (cx + 18, cy + 21), (cx + 44, cy + 6)], [2.2, 3.0, 3.0, 2.0], mg,
              flat=0.45 + 0.3 * p.glow, decal=True)
    for _ in range(10):
        x, y = rng.uniform(-36, 40), rng.uniform(-30, 18)
        cv.tpx((cx + x, cy + y), mg, inten=0.35 + 0.3 * p.glow, decal=True)
    if phase == 1:
        # The carapace: thick plates over the back, three vents on the knuckles.
        cv.part()
        plates = [[(-38, -10), (-26, -30), (-4, -38), (-6, -18), (-30, -2)], [(-2, -38), (26, -36), (40, -22), (18, -14), (-2, -18)],
                  [(-30, 2), (-6, -14), (20, -10), (40, -16), (44, 2), (20, 14), (-10, 18)]]
        for pl in plates:
            cv.part(line=1)
            cv.tpoly([(cx + x, cy + y - 3) for x, y in pl], CRUST, bevel=2.0, base=0.6)
            # Magma seeping at the seams of the carapace.
            for a, b in zip(pl, pl[1:] + pl[:1]):
                cv.tline((cx + a[0], cy + a[1] - 3), (cx + b[0], cy + b[1] - 3), mg, inten=0.45 + 0.2 * p.glow, decal=True)
        for vx, vy in ((-18, -24), (12, -32), (30, -10)):
            cv.part(line=False)
            cv.tellipse((cx + vx, cy + vy), 6.5, 4.5, COOL)
            cv.tellipse((cx + vx, cy + vy), 4.5, 3.0, mg, bias=0.2 + 0.25 * p.glow)
            cv.tdisc((cx + vx, cy + vy - 1), 1.6, core(phase), bias=0.3)
    if phase == 2:
        # Plates torn off: the core bare in the back of the hand, its rim cracked white.
        cv.part(line=False)
        cv.tellipse((cx + 2, cy - 12), 21.0, 16.0, mg, bias=0.15 + 0.2 * p.glow)
        cv.tellipse((cx + 2, cy - 13), 13.0, 9.0, core(phase), bias=0.25 + 0.2 * p.glow)
        cv.tdisc((cx - 2, cy - 17), 4.0, core(phase), bias=0.5)
        for a in range(7):
            ang = a * 0.9 + 0.3
            r0, r1 = 18.0, 30.0 + 4 * (a % 3)
            cv.tline((cx + 2 + math.cos(ang) * r0, cy - 12 + math.sin(ang) * r0 * 0.8),
                     (cx + 2 + math.cos(ang) * r1, cy - 12 + math.sin(ang) * r1 * 0.8), mg, inten=0.75, decal=True)
    finger(1, True)
    finger(0, True)
    img = cv.render(outline=True)
    img = _post(img, p, phase)
    return img


def _untilt(pt, c, tilt):
    if not tilt:
        return pt
    co, si = math.cos(-tilt), math.sin(-tilt)
    dx, dy = pt[0] - c[0], pt[1] - c[1]
    return (c[0] + dx * co - dy * si, c[1] + dx * si + dy * co)


def _cool_ramp(phase, k):
    hot = [C(h) for h in PHASES[phase][1]]
    cold = [C(h) for h in ("#6a6070", "#544b5a", "#3c3542", "#2a252f", "#1c1920")]
    out = []
    for a, b in zip(hot, cold):
        out.append("#%02x%02x%02x" % tuple(int(round(a[i] + (b[i] - a[i]) * k)) for i in range(3)))
    return out


def _post(img, p, phase):
    """Effects over a frame: the rock warmed by its magma, lava dripping from it, the heat halo
    (wider at each phase), dust, a burst of magma, drops, a flash."""
    out = img.copy()
    rng = np.random.RandomState(SEED + p.seed * 7 + phase * 131)
    ramp = [C(h) for h in PHASES[phase][1]]
    heat = HEAT[phase] * (1.0 - p.cool)
    a0 = out[:, :, 3] > 0
    if heat > 0:
        # The rock glows from within: every pixel pulled toward the magma's darkest tones, by its
        # height (the lower part, over the palm's magma, more), quantised to keep the palette small.
        hot = np.array(ramp[4][:3], np.float32)
        ys = np.arange(H, dtype=np.float32)[:, None] / H
        k = np.round((heat * (0.6 + 0.8 * ys)) * 6.0) / 6.0
        rgb = out[:, :, :3].astype(np.float32)
        dark = rgb.sum(axis=2) < 3 * 150  # the rock, not the magma already bright
        sel = a0 & dark
        mix = rgb + (hot - rgb) * np.broadcast_to(k, (H, W))[:, :, None] * 0.45
        rgb[sel] = mix[sel]
        out[:, :, :3] = np.clip(np.round(rgb), 0, 255).astype(np.uint8)
    if p.cool < 0.8:
        # Lava dripping from the palm and the knuckles: streaks and drops under the hand.
        cx, cy = hand_center(p)
        drng = np.random.RandomState(SEED + 500 + p.seed * 3)
        for d in range(2 + phase * 2):
            x = int(cx + drng.uniform(-40, 46))
            top = int(cy + 18 + drng.uniform(-6, 8))
            if not (0 <= x < W) or not (0 <= top < H) or not a0[min(H - 1, max(0, top - 2)), x]:
                continue
            length = int(4 + drng.uniform(0, 7) + 2 * phase)
            for yy in range(top, min(GY - 1, top + length)):
                out[yy, x] = ramp[2 if yy < top + length - 2 else 1]
            _blob(out, x, min(GY - 2, top + length + 2), 1, ramp[1])
    if p.cool < 0.5:
        # A heat halo hugging the silhouette (broken, shimmering with the seed).
        a = out[:, :, 3] > 0
        g = a.copy()
        g[1:] |= a[:-1]
        g[:-1] |= a[1:]
        g[:, 1:] |= a[:, :-1]
        g[:, :-1] |= a[:, 1:]
        rim = g & ~a
        rim[GY - 1:] = False
        noise = rng.uniform(0, 1, rim.shape)
        out[rim & (noise < 0.25 + 0.3 * phase)] = ramp[3 - phase]
        if phase == 2:
            # Almost incandescent: a second, broken ring.
            g2 = g.copy()
            g2[1:] |= g[:-1]
            g2[:-1] |= g[1:]
            g2[:, 1:] |= g[:, :-1]
            g2[:, :-1] |= g[:, 1:]
            rim2 = g2 & ~g
            rim2[GY - 1:] = False
            out[rim2 & (noise < 0.3)] = ramp[3]
    if p.dust > 0:
        for _ in range(int(14 * p.dust)):
            x = int(rng.uniform(40, 184))
            y = int(GY - 1 - rng.uniform(0, 10 * p.dust))
            r = 1 + int(rng.uniform(0, 2.5 * p.dust))
            c = DUST[int(rng.uniform(0, len(DUST)))]
            _blob(out, x, y, r, c)
    if p.burst > 0:
        cx, cy = hand_center(p)
        n = int(16 * p.burst)
        for k in range(n):
            ang = k / max(1, n) * 2 * math.pi + rng.uniform(-0.2, 0.2)
            d = 50 + 40 * p.burst + rng.uniform(-6, 6)
            x, y = int(cx + math.cos(ang) * d), int(min(GY - 2, cy + math.sin(ang) * d * 0.7))
            _blob(out, x, y, 2, ramp[1 + k % 3])
    if p.drops > 0:
        for _ in range(int(12 * p.drops)):
            x, y = int(rng.uniform(20, 204)), int(rng.uniform(8, 120))
            _blob(out, x, y, 1, ramp[1])
            _blob(out, x, y + 3, 2, ramp[2])
    if p.flash > 0:
        a = out[:, :, 3] > 0
        rgb = out[:, :, :3].astype(np.float32)
        rgb[a] = rgb[a] + (255 - rgb[a]) * p.flash
        out[:, :, :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    out[GY + 3:, :, :] = 0  # nothing more than 3 rows under the ground line
    out[:3, :, :] = 0
    out[:, :3, :] = 0
    out[:, -3:, :] = 0
    return out


def _blob(img, x, y, r, c):
    h, w = img.shape[:2]
    for yy in range(y - r, y + r + 1):
        for xx in range(x - r, x + r + 1):
            if 0 <= xx < w and 0 <= yy < h and (xx - x) ** 2 + (yy - y) ** 2 <= r * r:
                img[yy, xx] = c


# ----------------------------------------------------------------------------- clips
def wave(t, a=1.0):
    return math.sin(t * 2 * math.pi) * a


def idle():
    return [Pose(dy=wave(i / 8, 2.0), lift=(max(0, wave(i / 8 + 0.1, 3)), 0, max(0, wave(i / 8 + 0.6, 2)), 0), glow=wave(i / 8, 0.5), seed=i)
            for i in range(8)]


def walk():
    out = []
    for i in range(8):
        t = i / 8
        lifts = [max(0.0, wave(t + o, 9.0)) for o in (0.0, 0.5, 0.25, 0.75)]
        reach = [wave(t + o + 0.25, 8.0) for o in (0.0, 0.5, 0.25, 0.75)]
        out.append(Pose(dx=wave(t, 1.5), dy=-abs(wave(t, 3.0)), tilt=wave(t, 0.03), lift=lifts, reach=reach, dust=0.4 if i in (0, 4) else 0.0, seed=10 + i))
    return out


def telegraph():
    ks = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]
    return [Pose(dy=-30 * k, dx=-8 * k, tilt=-0.35 * k, curl=0.9 * k, glow=0.6 * k, lift=(20 * k, 20 * k, 20 * k, 20 * k), seed=20 + i)
            for i, k in enumerate(ks)]


def attack():
    poses = [Pose(dy=-34, dx=-6, tilt=-0.3, curl=0.95, glow=0.8, seed=30),
             Pose(dy=-8, dx=10, tilt=0.25, curl=1.0, glow=0.9, seed=31),
             Pose(dy=26, dx=22, tilt=0.35, curl=1.0, glow=1.0, squash=0.88, dust=1.0, burst=0.8, seed=32),
             Pose(dy=20, dx=20, tilt=0.25, curl=0.8, glow=0.7, dust=0.6, burst=0.4, seed=33),
             Pose(dy=6, dx=8, tilt=0.08, curl=0.4, glow=0.3, dust=0.2, seed=34)]
    return poses


def telegraph_special():
    ks = [0.0, 0.25, 0.5, 0.7, 0.85, 1.0]
    return [Pose(dy=6 * k, squash=1 - 0.08 * k, glow=1.0 * k, spread=1 + 0.15 * k, seed=40 + i) for i, k in enumerate(ks)]


def special():
    return [Pose(dy=6, squash=0.92, glow=0.8 + 0.2 * wave(i / 4), spread=1.15, burst=0.3 + 0.2 * (i % 2), seed=50 + i) for i in range(4)]


def hurt():
    return [Pose(dx=-6, tilt=-0.12, glow=0.6, flash=0.55, seed=60), Pose(dx=-3, tilt=-0.06, glow=0.3, flash=0.2, seed=61)]


def summon():
    ks = [0.0, 0.3, 0.6, 1.0, 0.9, 0.7]
    return [Pose(dy=-22 * k, tilt=-0.2 * k, spread=1 + 0.25 * k, glow=k, drops=k, lift=(12 * k, 8 * k, 12 * k, 8 * k), seed=70 + i)
            for i, k in enumerate(ks)]


def roar():
    seq = [(0.2, 0.0, 0.0), (0.6, 0.3, 0.0), (1.0, 0.7, 0.2), (1.0, 1.0, 0.6), (0.6, 1.0, 1.0), (0.3, 0.8, 0.7), (0.1, 0.5, 0.3), (0.0, 0.2, 0.0)]
    return [Pose(curl=c, glow=g, burst=b, dy=-10 * c, tilt=-0.1 * c, flash=0.3 if i == 3 else 0.0, seed=80 + i) for i, (c, g, b) in enumerate(seq)]


def death():
    out = []
    for i in range(14):
        t = i / 13
        out.append(Pose(dy=min(34, 40 * t), dx=-10 * t, tilt=0.25 * t, spread=1 + 0.35 * t, squash=1 - 0.18 * t, cool=min(1.0, t * 1.15),
                        lift=(0, 0, 0, 0), reach=(16 * t, 12 * t, 20 * t, 10 * t), dust=0.8 if i in (8, 9, 10) else 0.0,
                        burst=0.5 if i == 2 else 0.0, flash=0.4 if i == 1 else 0.0, seed=90 + i))
    return out


def curl_clip():
    ks = [0.15, 0.35, 0.55, 0.75, 0.9, 1.0]
    return [Pose(curl=k, dy=12 * k, glow=0.4 * k, seed=110 + i) for i, k in enumerate(ks)]


def roll():
    return [Pose(curl=1.0, dy=12, roll=i * (2 * math.pi / 8), glow=0.6, dust=0.5, seed=120 + i) for i in range(8)]


def uncurl():
    ks = [0.9, 0.6, 0.35, 0.1]
    return [Pose(curl=k, dy=12 * k, glow=0.3 * k, seed=130 + i) for i, k in enumerate(ks)]


def dizzy():
    return [Pose(tilt=wave(i / 6, 0.12), dx=wave(i / 6, 5), dy=6, spread=1.2, glow=-0.4, seed=140 + i) for i in range(6)]


def crouch():
    ks = [0.25, 0.5, 0.8, 1.0]
    return [Pose(dy=16 * k, squash=1 - 0.12 * k, spread=1 + 0.2 * k, curl=0.3 * k, glow=0.5 * k, seed=150 + i) for i, k in enumerate(ks)]


def leap():
    return [Pose(dy=-20, curl=0.6, glow=0.6, dust=0.8, seed=160), Pose(dy=-60, curl=0.9, glow=0.7, seed=161), Pose(dy=-92, curl=1.0, glow=0.8, seed=162)]


def fall():
    return [Pose(dy=-40, curl=1.0, tilt=0.4, glow=1.0, seed=170), Pose(dy=-20, curl=1.0, tilt=0.5, glow=1.0, seed=171)]


def land():
    return [Pose(dy=22, curl=1.0, tilt=0.3, squash=0.84, glow=1.0, dust=1.0, burst=1.0, seed=180),
            Pose(dy=18, curl=0.7, tilt=0.15, squash=0.9, glow=0.8, dust=0.8, burst=0.6, seed=181),
            Pose(dy=12, curl=0.4, squash=0.95, glow=0.5, dust=0.5, seed=182),
            Pose(dy=6, curl=0.2, glow=0.3, dust=0.2, seed=183),
            Pose(dy=2, curl=0.0, glow=0.1, seed=184)]


def quake():
    return [Pose(dy=-14, curl=0.9, glow=0.6, seed=190), Pose(dy=18, curl=1.0, squash=0.88, glow=1.0, dust=1.0, burst=0.5, seed=191),
            Pose(dy=8, curl=0.9, glow=0.7, dust=0.5, seed=192), Pose(dy=-4, curl=0.9, glow=0.5, seed=193)]


def T(*d):
    return list(d)


CLIPS = [
    ("idle", idle, dict(fps=7, loop=True)),
    ("telegraph", telegraph, dict(durations=T(0.1, 0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("walk", walk, dict(fps=8, loop=True, events=[(0, "footstep"), (4, "footstep")])),
    ("attack", attack, dict(durations=T(0.05, 0.06, 0.18, 0.12, 0.14), loop=False, events=[(2, "impact")])),
    ("telegraph_special", telegraph_special, dict(durations=T(0.1, 0.1, 0.1, 0.12, 0.14, 0.2), loop=False)),
    ("special", special, dict(fps=12, loop=True)),
    ("hurt", hurt, dict(durations=T(0.08, 0.14), loop=False)),
    ("summon", summon, dict(durations=T(0.12, 0.1, 0.12, 0.12, 0.14, 0.16), loop=False)),
    ("roar", roar, dict(durations=T(0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16), loop=False)),
    ("death", death, dict(durations=T(0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3), loop=False)),
    ("curl", curl_clip, dict(durations=T(0.08, 0.08, 0.08, 0.1, 0.1, 0.12), loop=False)),
    ("roll", roll, dict(fps=16, loop=True)),
    ("uncurl", uncurl, dict(durations=T(0.08, 0.08, 0.1, 0.12), loop=False)),
    ("dizzy", dizzy, dict(fps=7, loop=True)),
    ("crouch", crouch, dict(durations=T(0.08, 0.1, 0.12, 0.16), loop=False)),
    ("leap", leap, dict(durations=T(0.08, 0.1, 0.12), loop=False)),
    ("fall", fall, dict(fps=10, loop=True)),
    ("land", land, dict(durations=T(0.08, 0.1, 0.1, 0.12, 0.14), loop=False, events=[(0, "impact")])),
    ("quake", quake, dict(fps=8, loop=True)),
]
COLUMNS = 14


def pack(clips, fw, fh, columns=COLUMNS):
    """Clips placed left to right, several per row (never split).  `move` shares `walk`'s frames."""
    rows, cur, used = [], [], 0
    for name, frames, kw in clips:
        if used + len(frames) > columns:
            rows.append(cur)
            cur, used = [], 0
        cur.append((name, frames, kw, used))
        used += len(frames)
    if cur:
        rows.append(cur)
    img = np.zeros((fh * len(rows), fw * columns, 4), np.uint8)
    meta = {}
    for r, row in enumerate(rows):
        for name, frames, kw, start in row:
            for c, f in enumerate(frames):
                img[r * fh:(r + 1) * fh, (start + c) * fw:(start + c + 1) * fw] = f
            m = {"row": r, "count": len(frames)}
            if start:
                m["start"] = start
            if kw.get("durations") is not None:
                m["durations"] = [round(float(d), 3) for d in kw["durations"]]
            else:
                m["fps"] = kw.get("fps", 10)
            m["loop"] = bool(kw.get("loop", True))
            if kw.get("events"):
                m["events"] = [{"frame": int(fr), "name": n} for fr, n in kw["events"]]
            meta[name] = m
            if name == "walk":
                meta["move"] = dict(m)
    return img, meta


def phase_sheet(phase):
    clips = [(name, [draw_hand(p, phase) for p in fn()], kw) for name, fn, kw in CLIPS]
    return pack(clips, W, H)


# ----------------------------------------------------------------------------- weak point
def noyau_frames():
    """Its weak point: a shard of magma core set in a ring of basalt, pulsing; hurt; shattering."""
    fw = fh = 32
    mg, co = magma(2), core(2)

    def crystal(pulse, crack=0.0, scale=1.0, flash=0.0):
        cv = TCanvas(fw, fh)
        cv.part()
        cv.tdisc((16, 16), 11.0 * scale, BASALT)
        cv.part(line=False)
        pts = [(16, 6), (24, 16), (16, 26), (8, 16)]
        cv.tpoly([(16 + (x - 16) * scale, 16 + (y - 16) * scale) for x, y in pts], mg, bias=0.15 + 0.2 * pulse)
        cv.tdisc((15, 14), 3.5 * scale, co, bias=0.2 + 0.3 * pulse)
        if crack:
            cv.tline((10, 12), (20, 22), COOL, inten=0.3, decal=True)
            cv.tline((20, 9), (14, 18), COOL, inten=0.3, decal=True)
        img = cv.render(outline=True)
        if flash:
            a = img[:, :, 3] > 0
            img[a, :3] = np.clip(img[a, :3].astype(np.float32) + (255 - img[a, :3].astype(np.float32)) * flash, 0, 255).astype(np.uint8)
        return img

    idle_f = [crystal(0.5 + 0.5 * math.sin(i / 6 * 2 * math.pi)) for i in range(6)]
    hurt_f = [crystal(1.0, 1.0, 1.0, 0.6), crystal(0.8, 1.0)]
    death_f = []
    ramp = [C(h) for h in PHASES[2][1]]
    for i in range(6):
        img = np.zeros((fh, fw, 4), np.uint8)
        t = (i + 1) / 6
        if i < 2:
            img = crystal(1.0, 1.0, 1.0 - 0.15 * i, 0.4)
        for k in range(8):
            ang = k / 8 * 2 * math.pi + 0.3
            d = 4 + 11 * t
            _blob(img, int(16 + math.cos(ang) * d), int(16 + math.sin(ang) * d), 1 if i > 3 else 2, ramp[1 + (k + i) % 3] if i < 5 else C("#5a5560"))
        death_f.append(img)
    clips = [("idle", idle_f, dict(fps=8, loop=True)), ("hurt", hurt_f, dict(durations=T(0.06, 0.1), loop=False)),
             ("death", death_f, dict(durations=T(0.06, 0.06, 0.08, 0.08, 0.1, 0.12), loop=False))]
    return pack(clips, fw, fh, columns=6)


# ----------------------------------------------------------------------------- vents
VW, VH = 48, 144


def vent_frames():
    """Its vents, standing on the floor: closed (a cap of basalt), open (a grate of magma), warn
    (bubbling, brighter), jet (a column of magma thrown up the 112 px over it)."""
    ramp = [C(h) for h in PHASES[1][1]]

    def grate(glow, cap=False, seed=0):
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tpoly([(4, VH - 1), (8, VH - 12), (40, VH - 12), (44, VH - 1)], BASALT, bevel=1.5, base=0.6)
        if cap:
            cv.part()
            cv.tellipse((24, VH - 12), 16.0, 5.5, CRUST)
            cv.tline((14, VH - 13), (34, VH - 11), COOL, inten=0.3, decal=True)
        else:
            cv.part(line=False)
            cv.tellipse((24, VH - 12), 14.0, 4.5, magma(1), bias=0.1 + 0.3 * glow)
            for k in range(4):
                cv.tline((13 + 7 * k, VH - 15), (13 + 7 * k, VH - 9), COOL, inten=0.25, decal=True)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 900 + seed)
        if not cap:
            for _ in range(int(3 + 6 * glow)):
                _blob(img, int(rng.uniform(14, 34)), int(VH - 16 - rng.uniform(0, 10 + 18 * glow)), 1, ramp[1 + int(rng.uniform(0, 2))])
        return img

    def jet(i):
        img = grate(1.0, seed=40 + i)
        rng = np.random.RandomState(SEED + 950 + i)
        top = 20 + 6 * (i % 2)
        for y in range(top, VH - 12):
            t = (y - top) / (VH - 12 - top)
            half = 5 + 6 * t + 2 * math.sin(y * 0.35 + i)
            for x in range(int(24 - half), int(24 + half) + 1):
                d = abs(x - 24) / max(1.0, half)
                c = ramp[0] if d < 0.3 else ramp[1] if d < 0.65 else ramp[2]
                if 0 <= x < VW:
                    img[y, x] = c
        for _ in range(14):
            _blob(img, int(rng.uniform(8, 40)), int(rng.uniform(4, top + 10)), 1 + int(rng.uniform(0, 2)), ramp[1 + int(rng.uniform(0, 3))])
        return img

    def fist(i):
        """A fist of magma falling on its mark (« Poings de magma »), knuckles down, its trail of fire above."""
        cv = TCanvas(VW, VH)
        cv.part()
        cv.tellipse((24, 116), 15.0, 14.0, BASALT)
        for k, kx in enumerate((13, 20, 28, 35)):
            cv.part(line=1)
            cv.tdisc((kx, 130), 5.0, BASALT, bias=0.05)
            cv.tellipse((kx, 132), 3.2, 2.2, magma(1), bias=0.2, decal=True)
        for a, b in (((14, 106), (24, 118)), ((24, 118), (34, 110)), ((20, 124), (30, 125))):
            cv.tcapsule(a, b, 1.6, 1.2, magma(1), flat=0.6, decal=True)
        img = cv.render(outline=True)
        rng = np.random.RandomState(SEED + 970 + i)
        for y in range(18, 102, 3):
            t = (y - 18) / 84.0
            half = 2 + 9 * t
            for _ in range(2):
                x = int(24 + rng.uniform(-half, half))
                _blob(img, x, y, 1 if t < 0.5 else 2, ramp[3 - min(3, int(t * 4))])
        return img

    clips = [("closed", [grate(0.0, cap=True)], dict(fps=1, loop=True)),
             ("open", [grate(0.3 + 0.2 * math.sin(i / 4 * 2 * math.pi), seed=i) for i in range(4)], dict(fps=6, loop=True)),
             ("warn", [grate(0.8 + 0.2 * (i % 2), seed=10 + i) for i in range(4)], dict(fps=10, loop=True)),
             ("jet", [jet(i) for i in range(4)], dict(fps=12, loop=True)),
             ("fist", [fist(i) for i in range(2)], dict(fps=10, loop=True))]
    return pack(clips, VW, VH, columns=4)


# ----------------------------------------------------------------------------- output
def outputs():
    """(relative path, bytes) of every file, in a fixed order."""
    out = []
    for k in range(3):
        img, meta = phase_sheet(k)
        name = "pyroclast" if k == 0 else "pyroclast_p%d" % (k + 1)
        tex = "sprites/enemies/%s.png" % name
        out.append((tex, _png(img)))
        out.append(("animations/%s.json" % name, (dumps({"texture": tex, "frameWidth": W, "frameHeight": H, "pivot": PIVOT, "clips": meta}) + "\n").encode()))
    img, meta = noyau_frames()
    out.append(("sprites/enemies/pyroclast_noyau.png", _png(img)))
    out.append(("animations/pyroclast_noyau.json",
                (dumps({"texture": "sprites/enemies/pyroclast_noyau.png", "frameWidth": 32, "frameHeight": 32, "pivot": [16, 28], "clips": meta}) + "\n").encode()))
    img, meta = vent_frames()
    out.append(("sprites/enemies/pyroclast_vents.png", _png(img)))
    out.append(("animations/pyroclast_vents.json",
                (dumps({"texture": "sprites/enemies/pyroclast_vents.png", "frameWidth": VW, "frameHeight": VH, "pivot": [VW // 2, VH], "clips": meta}) + "\n").encode()))
    return out


def _png(img):
    buf = io.BytesIO()
    Image.fromarray(img, "RGBA").save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def build():
    for rel, data in outputs():
        path = os.path.join(ASSETS, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)


def check():
    """Two renders give the same bytes, and the files on disk are the generator's output."""
    errs = []
    a, b = outputs(), outputs()
    if a != b:
        errs.append("pyroclast: two renders differ (not deterministic)")
    for rel, data in a:
        path = os.path.join(ASSETS, rel)
        if not os.path.exists(path):
            errs.append("%s: missing (python3 tools/assetgen/characters/boss_k_pyroclast.py)" % rel)
            continue
        if rel.endswith(".json"):
            with open(path, "rb") as f:
                if f.read().replace(b"\r", b"") != data:
                    errs.append("%s: not the generator's output (regenerate)" % rel)
        else:
            disk = np.array(Image.open(path).convert("RGBA"))
            fresh = np.array(Image.open(io.BytesIO(data)).convert("RGBA"))
            if disk.shape != fresh.shape or (disk != fresh).any():
                errs.append("%s: not the generator's output (regenerate)" % rel)
    return errs


def review(path):
    """A contact sheet, x2: one frame of each clip and phase, the weak point and the vents, labelled in French."""
    from PIL import ImageDraw, ImageFont
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", "ChakraPetch-Regular.ttf"), 12)
    picks = [("Repos", idle()[0]), ("Marche", walk()[2]), ("Préparation du poing", telegraph()[4]), ("Coup de poing (impact)", attack()[2]),
             ("Pluie de magma", summon()[3]), ("Rugissement de phase", roar()[3]), ("Poing fermé", curl_clip()[5]),
             ("Poing roulant", roll()[2]), ("Accroupi (bond)", crouch()[3]), ("Poing météore (chute)", fall()[0]),
             ("Atterrissage", land()[0]), ("Secousse", quake()[1]), ("Étourdi", dizzy()[1]), ("Mort", death()[11])]
    cell, label, per = 224, 22, 7
    rows = (len(picks) + per - 1) // per
    names = ["Poing de magma", "Carapace de basalte", "Cœur de magma"]
    vimg, _ = vent_frames()
    nimg, _ = noyau_frames()
    extra = label + max(vimg.shape[0], nimg.shape[0] * 3) + 8
    sheet = Image.new("RGBA", (per * cell, 3 * rows * (cell + label) + extra), (24, 18, 30, 255))
    d = ImageDraw.Draw(sheet)
    for k in range(3):
        for i, (name, pose) in enumerate(picks):
            x, y0 = (i % per) * cell, (k * rows + i // per) * (cell + label)
            sheet.alpha_composite(Image.fromarray(draw_hand(pose, k), "RGBA"), (x, y0 + label))
            d.text((x + 4, y0 + 5), "Phase %d · %s" % (k + 1, name), fill=(255, 220, 170, 255), font=font)
    y = 3 * rows * (cell + label)
    d.text((4, y + 5), "Point faible : repos, touché, brisé (x3)", fill=(255, 220, 170, 255), font=font)
    sheet.alpha_composite(Image.fromarray(nimg, "RGBA").resize((nimg.shape[1] * 3, nimg.shape[0] * 3), Image.NEAREST), (4, y + label))
    d.text((620, y + 5), "Évents (fermé, ouvert, alerte, jet) et poing de magma", fill=(255, 220, 170, 255), font=font)
    sheet.alpha_composite(Image.fromarray(vimg, "RGBA"), (620, y + label))
    sheet.save(path)
    return path


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        e = check()
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    elif len(sys.argv) > 2 and sys.argv[1] == "--review":
        print(review(sys.argv[2]))
    else:
        build()
        for rel, data in outputs():
            print(rel, len(data))
