#!/usr/bin/env python3
"""The film of Asterion's fall (2.72, phase 6): « LA CHUTE D'ASTERION » in seven animated shots, played by
the stories' scene (BossStoryScene, kind « film ») for everyone of a raid instance at the step the server
validates the shared victory over the Cœur d'Asterion, then by « Revoir la victoire » on the sheet of the
event.

The script — each shot's caption, its titles in the picture and its length — is hand-written in
data/raids/asterion_victoire.json (a length changed there stretches the scene, nothing to regenerate).
This module writes what the script names as « scenes »: data/raids/asterion_victoire_scenes.json (the
format of game/content/BossStories.h, as story_asterion.py writes the first film's), and two pictures of
its own: the supernova's burst and the ring of its shockwave (assets/stories/asterion/nova.png, onde.png).

Nothing made up: the titan is its real sprite in layers (the sheets story_asterion.py cuts from
assets/ui/raids/asterion.png: each part with its looks, « detruit » here, its heart, its aura), Nova and
our planets their own pictures, the beacons, banners, chunks and ridge those of the first film.

  1. assaut        the titan, its seven parts destroyed: the clans' blows light up all over its heart
  2. carcasse      the parts fallen flare one by one; the heart beats alone, faster and faster
  3. effondrement  the branches tear off and fall into the void, the heart left hanging
  4. supernova     the heart collapses to a point, then bursts (the script's title « SUPERNOVA »)
  5. onde          the shockwave crosses the galaxy; the worlds promised to the feast light up again
  6. bannieres     on a ridge among the titan's debris, the clans raise their banners
  7. victoire      the void where it was, a golden dawn (the script's title « ASTERION EST TOMBÉ »)

    python3 tools/assetgen/environment/story_asterion_victoire.py [--storyboard FILE.png] [--check]

Deterministic (fixed seeds): `--check` builds everything twice and compares the bytes with each other and
with the files."""
import json
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

import story_asterion as A  # noqa: E402
from story_stage import K, amb, particles, sfx  # noqa: E402

S = A.S
REPO, ASSETS = S.REPO, S.ASSETS
SCRIPT = os.path.join(REPO, "data", "raids", "asterion_victoire.json")
W, H = S.W, S.H
Track, Titan, sprite, layer, glo = A.Track, A.Titan, A.sprite, A.layer, A.glo
GOLD, EMBER, CYAN, VIOLET, CLANS = A.GOLD, A.EMBER, A.CYAN, A.VIOLET, A.CLANS
PARTS = [p for p in A.ORDER if p not in ("aura", "coeur")]
HALF = 128 * S.PIX  # half the drawn size of a 256-pixel picture at scale 1 (a layer: its pixels times S.PIX)


# =================================================================== its own pictures
def img_nova(size=256):
    """The supernova's burst: a white core, a gold corona, violet streamers, drawn additively."""
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    dx, dy = (xx - c) / c, (yy - c) / c
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    rays = 0.55 + 0.45 * np.cos(ang * 12.0) * np.cos(ang * 5.0 + 0.7)
    core = np.exp(-(r / 0.16) ** 2)
    corona = np.exp(-(r / 0.42) ** 2) * (0.6 + 0.4 * rays)
    streak = np.clip(1.0 - r, 0, 1) ** 1.5 * np.clip(rays - 0.35, 0, 1) * 1.4
    out = np.zeros((size, size, 4), np.float32)
    out[..., :3] = (A.col("#ffffff")[:3] * core[..., None] + A.col("#ffd27a")[:3] * corona[..., None] * 0.9
                    + A.col("#b46cff")[:3] * streak[..., None] * 0.7)
    out[..., 3] = np.clip((core + corona * 0.85 + streak * 0.6) * 255, 0, 255)
    out[..., :3] = np.clip(out[..., :3], 0, 255)
    return A._rgba(out)


def img_ring(size=256):
    """The shockwave: a thin bright ring with a soft violet halo inside."""
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    r = np.sqrt((xx - c) ** 2 + (yy - c) ** 2) / c
    ring = np.exp(-((r - 0.92) / 0.035) ** 2)
    inner = np.exp(-((r - 0.82) / 0.12) ** 2) * 0.35
    out = np.zeros((size, size, 4), np.float32)
    out[..., :3] = A.col("#fff2c0")[:3] * ring[..., None] + A.col("#b46cff")[:3] * inner[..., None]
    out[..., 3] = np.clip((ring + inner) * 255, 0, 255)
    out[..., :3] = np.clip(out[..., :3], 0, 255)
    return A._rgba(out)


def build_assets():
    return {A.TEX + "nova.png": A.png_bytes(img_nova()), A.TEX + "onde.png": A.png_bytes(img_ring())}


# =================================================================== pieces
def fallen_titan(T, D, prefix="", tint=None, breathe=False):
    """The titan with its seven parts destroyed (their « detruit » look) and its aura."""
    els = []
    for pid in A.ORDER:
        if pid == "coeur":
            continue
        look = "intact" if pid == "aura" else "detruit"
        els.append(sprite(prefix + pid, pid, T.track(pid, D, look, breathe=breathe), tint=tint, add=pid == "aura"))
    return els


# =================================================================== the seven scenes
def scene_assaut(D, script):
    """The titan, its parts destroyed: the clans' blows light up all over its heart."""
    O, s = (480, 300), 1.6
    T = Titan(O, s)
    els = A.space(D, drift=6.0, nebula=0.6)
    els.append(A.stars(0.8))
    els.append(glo("aura_lueur", VIOLET, 360, Track(pos=O, alpha=0.4), pulse=(0.2, 2.2)))
    els += fallen_titan(T, D)
    hp = T.point(A.JOINT["coeur"])
    els.append(sprite("coeur", "coeur", T.track("coeur", D, "intact", breathe=False)))
    els += A.heart_glow(Track(pos=hp, alpha=0.8).to(D, alpha=1.0, scale=1.2), radius=90)
    rng = np.random.default_rng(7201)
    hits = []
    for i in range(26):
        t = 0.8 + i * (D - 2.0) / 26.0 + float(rng.uniform(-0.15, 0.15))
        ang, d = float(rng.uniform(0, A.TAU)), float(rng.uniform(10, 46))
        p = (hp[0] + math.cos(ang) * d, hp[1] + math.sin(ang) * d * 0.8)
        c = CLANS[i % 4]
        els.append(glo(f"coup{i + 1}", c, 34, Track(pos=p, alpha=0.0).to(t, alpha=0.0).to(t + 0.06, alpha=1.0, ease="out").to(t + 0.5, alpha=0.0)))
        hits.append(t)
    # the clans' beacons far below, beams rising toward the heart
    for i, x in enumerate((150, 300, 660, 810)):
        t = 0.4 + 0.5 * i
        els.append(sprite(f"faisceau{i + 1}", "faisceau", Track(pos=(x, 540), scale=2.4, alpha=0.0).to(t, alpha=0.0).to(t + 0.6, alpha=0.6, ease="out")
                          .clip(0, "idle"), add=True, depth=0.6))
    els.append(particles("braises", "embers", GOLD, (300, 160, 360, 260), [K(0, alpha=0.6)]))
    els.append(particles("eclats", "sparks", "#ffe8b0", (360, 180, 240, 200), [K(0, alpha=0.0), K(1.0, alpha=0.9)]))
    cam = A.camera((0, (480, 300), 1.05, "linear"), (D, (hp[0], hp[1] + 20), 1.6, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[A.shake(D - 3.0, D, 4.0)], still=D * 0.6), hits


def scene_carcasse(D, script):
    """The parts fallen flare one by one; the heart beats alone, faster and faster."""
    O, s = (480, 290), 1.9
    T = Titan(O, s)
    els = A.space(D, drift=4.0, nebula=0.5)
    els.append(A.stars(0.6))
    els += fallen_titan(T, D, tint="#8a7aa8")
    for k, pid in enumerate(PARTS):
        t = 0.8 + 1.0 * k
        rim = T.track(pid, D, "contour", breathe=False, alpha=0.0)
        rim.to(t, alpha=0.0).to(t + 0.15, alpha=1.0, ease="out").to(t + 1.2, alpha=0.25).to(D, alpha=0.2)
        els.append(sprite(pid + "_contour", pid, rim, tint=A.GLOW[pid], add=True))
    hp = T.point(A.JOINT["coeur"])
    els.append(sprite("coeur", "coeur", T.track("coeur", D, "intact", breathe=False)))
    beat = Track(pos=hp, alpha=0.7)
    t, period = 0.3, 1.0
    while t < D - 0.2:
        beat.to(t, alpha=0.6, scale=1.0).to(t + 0.1, alpha=1.0, scale=1.25, ease="out")
        t += period
        period = max(0.32, period * 0.86)
    els += A.heart_glow(beat, radius=80)
    els.append(particles("cendres", "ash", "#b8a0c8", (120, 80, 720, 420), [K(0, alpha=0.5)]))
    cam = A.camera((0, (480, 280), 1.15, "linear"), (D, (hp[0], hp[1] + 10), 1.55, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.75), None


def scene_effondrement(D, script):
    """The branches tear off and fall into the void, the heart left hanging."""
    O, s = (480, 270), 1.7
    T = Titan(O, s)
    els = A.space(D, drift=8.0, nebula=0.6)
    els.append(A.stars(0.7))
    rng = np.random.default_rng(7203)
    hp = T.point(A.JOINT["coeur"])
    for k, pid in enumerate(A.ORDER):
        if pid == "coeur":
            continue
        tr = T.track(pid, D, "intact" if pid == "aura" else "detruit", breathe=False)
        j = A.joint_at(pid, O, s)
        t0 = 1.6 + 0.9 * (k % 4) + float(rng.uniform(0.0, 0.5))
        out = ((j[0] - hp[0]) * 2.2, (j[1] - hp[1]) * 1.2 + 420.0)
        tr.to(t0, pos=j, rot=0.0).to(D, "in", pos=(j[0] + out[0], j[1] + out[1]), rot=float(rng.uniform(-40, 40)), alpha=0.0 if pid == "aura" else 0.4)
        els.append(sprite(pid, pid, tr, add=pid == "aura"))
        els.append(glo(pid + "_rupture", A.GLOW.get(pid, GOLD), 46, Track(pos=j, alpha=0.0).to(t0 - 0.05, alpha=0.0).to(t0, alpha=1.0, ease="out")
                       .to(t0 + 0.6, alpha=0.0)))
    els.append(sprite("coeur", "coeur", Track(pos=hp, scale=s).clip(0, "intact")))
    els += A.heart_glow(Track(pos=hp, alpha=0.9).to(D, alpha=1.0, scale=1.5), radius=100)
    for i in range(18):
        kind = ("roche", "magma", "glace")[i % 3]
        x0, y0 = float(rng.uniform(260, 700)), float(rng.uniform(160, 380))
        t0 = float(rng.uniform(1.0, D - 3.0))
        tr = Track(pos=(x0, y0), scale=float(rng.uniform(1.5, 2.6)), alpha=0.0, rot=float(rng.uniform(0, 360)))
        tr.to(t0, alpha=0.0).to(t0 + 0.2, alpha=1.0).to(D, "in", pos=(x0 + float(rng.uniform(-120, 120)), y0 + 420.0), rot=float(rng.uniform(300, 700)))
        tr.clip(0, kind)
        els.append(sprite(f"debris{i + 1}", "eclat", tr))
    els.append(particles("cendres", "ash", "#b8a0c8", (80, 60, 800, 460), [K(0, alpha=0.4), K(2.0, alpha=1.0)]))
    cam = A.camera((0, (480, 290), 1.0, "linear"), (D, (hp[0], hp[1] + 30), 1.35, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[A.shake(1.5, D - 0.5, 9.0)], flash=[A.flash(1.6, "#ffb070", 0.35, 0.1, 0.6)],
                still=D * 0.55), None


def scene_supernova(D, script):
    """The heart collapses to a point, then bursts."""
    O = (480, 270)
    t_burst = 6.0
    els = A.space(D, drift=3.0, nebula=0.4)
    els.append(A.stars(0.7))
    heart = Track(pos=O, scale=3.0).to(t_burst - 1.8, scale=3.4, ease="out").to(t_burst, "in", scale=0.3, alpha=1.0).to(t_burst + 0.05, alpha=0.0)
    heart.clip(0, "intact")
    els.append(sprite("coeur", "coeur", heart))
    els += A.heart_glow(Track(pos=O, alpha=1.0, scale=1.0).to(t_burst - 1.8, scale=1.6).to(t_burst, "in", scale=0.4).to(t_burst + 0.1, alpha=0.0),
                        radius=140)
    nova = Track(pos=(O[0] - HALF * 0.2, O[1] - HALF * 0.2), scale=0.2, alpha=0.0)
    nova.to(t_burst, alpha=0.0).to(t_burst + 0.05, alpha=1.0, ease="out")
    nova.to(D, "out", pos=(O[0] - HALF * 5.5, O[1] - HALF * 5.5), scale=5.5, alpha=0.75)
    els.append({"id": "nova", "layer": A.TEX + "nova.png", "depth": 1.0, "blend": "add", "keys": nova.keys()})
    for i, (t, sc, col) in enumerate(((t_burst + 0.1, 4.2, GOLD), (t_burst + 0.5, 3.4, VIOLET), (t_burst + 1.0, 2.8, "#ffffff"))):
        ring = Track(pos=(O[0] - HALF * 0.1, O[1] - HALF * 0.1), scale=0.1, alpha=0.0)
        ring.to(t, alpha=0.0).to(t + 0.05, alpha=1.0).to(D, "out", pos=(O[0] - HALF * sc, O[1] - HALF * sc), scale=sc, alpha=0.0)
        els.append({"id": f"anneau{i + 1}", "layer": A.TEX + "onde.png", "depth": 1.0, "blend": "add", "tint": col, "keys": ring.keys()})
    els.append(glo("eclat_blanc", "#ffffff", 420, Track(pos=O, alpha=0.0).to(t_burst, alpha=0.0).to(t_burst + 0.05, alpha=1.0).to(t_burst + 1.6, alpha=0.3)))
    els.append(particles("eclats", "sparks", "#fff2c0", (240, 100, 480, 340), [K(0, alpha=0.0), K(t_burst, alpha=0.0), K(t_burst + 0.1, alpha=1.0)]))
    els.append(particles("braises", "embers", GOLD, (300, 160, 360, 220), [K(0, alpha=0.8), K(t_burst, alpha=0.2)]))
    cam = A.camera((0, O, 1.2, "linear"), (t_burst, O, 1.6, "in"), (t_burst + 0.6, O, 1.0, "out"), (D, O, 0.9, "inout"))
    return dict(backdrop="#020206", camera=cam, elements=els, shake=[A.shake(t_burst - 2.0, t_burst, 3.0), A.shake(t_burst, t_burst + 2.6, 16.0)],
                flash=[A.flash(t_burst, "#ffffff", 0.95, 0.04, 1.4)], still=t_burst + 1.4), t_burst


def scene_onde(D, script):
    """The shockwave crosses the galaxy; the worlds promised to the feast light up again."""
    els = A.space(D, drift=5.0, nebula=0.55)
    els.append(A.stars(0.8))
    sun = (150, 150)
    els.append(glo("soleil_lueur", GOLD, 220, Track(pos=sun, alpha=0.6).to(D, alpha=1.0), depth=0.3, pulse=(0.2, 1.8)))
    els.append(layer("soleil", "ui/planets/sun.png", 0.3, (sun[0] - 64, sun[1] - 64), scale=0.25))
    origin = (1000, 330)
    planets = [("sylvanox", 0.15), ("pyralis", 0.13), ("glacior", 0.14), ("aethra", 0.16), ("confins", 0.13)]
    times = []
    for i, (p, sc) in enumerate(planets):
        x, y = 250 + i * 92, 205 + i * 34
        size = 256 * S.PIX * sc
        reach = math.hypot(origin[0] - x, origin[1] - y) / 1100.0 * (D - 2.0) + 1.0
        times.append(reach)
        els.append(glo(p + "_lueur", "#ffe6a8", size * 0.9, Track(pos=(x, y), alpha=0.1).to(reach, alpha=0.1).to(reach + 0.4, alpha=0.9, ease="out")
                       .to(D, alpha=0.6), depth=0.35 + 0.05 * i))
        els.append(layer(p, f"ui/planets/{p}.png", 0.35 + 0.05 * i, (x - size / 2, y - size / 2), scale=sc))
    ring = Track(pos=(origin[0] - HALF * 0.2, origin[1] - HALF * 0.2), scale=0.2, alpha=0.0)
    ring.to(0.6, alpha=1.0).to(D, "linear", pos=(origin[0] - HALF * 9.0, origin[1] - HALF * 9.0), scale=9.0, alpha=0.2)
    els.append({"id": "onde", "layer": A.TEX + "onde.png", "depth": 1.0, "blend": "add", "tint": GOLD, "keys": ring.keys()})
    els.append(glo("origine", "#fff2c0", 160, Track(pos=origin, alpha=1.0).to(D, alpha=0.2)))
    els.append(particles("eclats", "sparks", "#fff2c0", (200, 100, 760, 360), [K(0, alpha=0.0), K(2.0, alpha=0.6)]))
    cam = A.camera((0, (600, 290), 1.15, "linear"), (D, (430, 260), 1.25, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.6), times


def scene_bannieres(D, script):
    """On a ridge among the titan's debris, the clans raise their banners."""
    els = A.space(D, drift=4.0, nebula=0.6)
    els.append(A.stars(0.8))
    els.append(glo("aube", GOLD, 420, Track(pos=(480, 300), alpha=0.3).to(D, alpha=0.7), depth=0.15, pulse=(0.1, 3.0)))
    rng = np.random.default_rng(7206)
    for i in range(14):  # the titan's debris drifting in the sky
        kind = ("roche", "magma", "glace")[i % 3]
        x0, y0 = float(rng.uniform(60, 900)), float(rng.uniform(40, 220))
        tr = Track(pos=(x0, y0), scale=float(rng.uniform(1.2, 2.4)), rot=float(rng.uniform(0, 360)))
        tr.to(D, "linear", pos=(x0 + float(rng.uniform(-40, 40)), y0 + float(rng.uniform(-10, 20))), rot=float(rng.uniform(-90, 90)))
        tr.clip(0, kind)
        els.append(sprite(f"debris{i + 1}", "eclat", tr, depth=0.25, tint="#8a7aa8"))
    rx, ry = -160, 300
    els.append(layer("crete", "crete", 1.0, (rx, ry)))
    top = A.ridge_top()
    ground = lambda x: ry + S.PIX * float(top[int(min(len(top) - 1, max(0, (x - rx) / S.PIX)))]) + 3  # noqa: E731
    for g in range(4):
        cx = 150 + g * 220
        t = 1.0 + 1.6 * g
        for k in range(8):
            x = cx + (k - 3.5) * 16 + float(rng.uniform(-4, 4))
            tr = Track(pos=(x, ground(x)), scale=1.3).clip(0, "idle", offset=float(rng.uniform(0, 1)))
            els.append(sprite(f"heros{g + 1}_{k + 1}", A.NOVA, tr, tint="#120e20"))
        bt = Track(pos=(cx + 4, ground(cx + 4) + 40), scale=1.6, alpha=0.0).to(t, alpha=0.0).to(t + 0.2, alpha=1.0).to(t + 1.2, "out", pos=(cx + 4, ground(cx + 4)))
        bt.clip(0, "wave", offset=g * 0.17)
        els.append(sprite(f"banniere{g + 1}", "banniere", bt, tint=CLANS[g]))
        bx = cx + 60
        b = Track(pos=(bx, ground(bx)), scale=1.1).clip(0, "off").clip(t + 1.2, "on")
        els.append(sprite(f"balise{g + 1}", "balise", b))
        beam = Track(pos=(bx, ground(bx) - 60), scale=1.3, alpha=0.0).to(t + 1.2, alpha=0.0).to(t + 1.7, alpha=0.7, ease="out")
        beam.clip(0, "idle")
        els.append(sprite(f"faisceau{g + 1}", "faisceau", beam, add=True))
    els.append(particles("braises", "embers", GOLD, (0, 260, 960, 280), [K(0, alpha=0.5)]))
    cam = A.camera((0, (480, 340), 1.35, "linear"), (D, (480, 300), 1.08, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.85), None


def scene_victoire(D, script):
    """The void where it was, a golden dawn, the clans' banners all around."""
    O = (480, 300)
    els = A.space(D, drift=3.0, nebula=0.8)
    els.append(A.stars(0.9))
    els.append(glo("aube", GOLD, 520, Track(pos=O, alpha=0.4).to(D, alpha=0.9), pulse=(0.15, 2.6)))
    els.append({"id": "nova", "layer": A.TEX + "nova.png", "depth": 0.5, "blend": "add",
                "keys": Track(pos=(O[0] - HALF * 2.4, O[1] - HALF * 2.4), scale=2.4, alpha=0.35).to(D, alpha=0.55).keys()})
    for i in range(8):
        a = i / 8 * A.TAU + 0.2
        p = (O[0] + math.cos(a) * 360, O[1] + 40 + math.sin(a) * 150)
        bt = Track(pos=p, scale=1.4, alpha=0.0).to(0.4 + 0.25 * i, alpha=0.0).to(0.7 + 0.25 * i, alpha=1.0)
        bt.clip(0, "wave", offset=i * 0.13)
        els.append(sprite(f"banniere{i + 1}", "banniere", bt, tint=CLANS[i % 4], depth=0.8))
    els.append(particles("eclats", "sparks", GOLD, (160, 80, 640, 380), [K(0, alpha=0.6)]))
    els.append(particles("braises", "embers", "#fff2c0", (200, 200, 560, 260), [K(0, alpha=0.5)]))
    cam = A.camera((0, O, 1.2, "linear"), (D, O, 1.05, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.5), None


SCENES = {"assaut": scene_assaut, "carcasse": scene_carcasse, "effondrement": scene_effondrement, "supernova": scene_supernova, "onde": scene_onde,
          "bannieres": scene_bannieres, "victoire": scene_victoire}


def sounds(name, D, extra):
    if name == "assaut":
        return [amb("story_amb_space", 0.4)] + [sfx("pound_impact", t, 0.35, pan=0.0) for t in (extra or [])[::3]] + [
            sfx("story_heartbeat", 0.6 + 1.0 * k, 0.5) for k in range(int(D - 1))]
    if name == "carcasse":
        return [amb("story_amb_space", 0.35)] + [sfx("story_reveal", 0.8 + 1.0 * k, 0.5, pitch=0.8 + 0.05 * k) for k in range(7)] + [
            sfx("story_heartbeat", 0.3 + 0.86 ** k * k, 0.6) for k in range(10)]
    if name == "effondrement":
        return [amb("story_amb_space", 0.4), sfx("story_titan_roar", 1.4, 0.9, pitch=0.8, duck=True), sfx("golem_slam", 2.0, 0.6),
                sfx("story_stone", 2.6, 0.6, pan=-0.4), sfx("ice_shatter", 3.4, 0.5, pan=0.4), sfx("story_stone", 4.6, 0.6, pitch=0.8)]
    if name == "supernova":
        t = extra or 6.0
        return [amb("story_amb_space", 0.3), sfx("inhale", t - 3.0, 0.7, pitch=0.6), sfx("laser_charge", t - 2.0, 0.6, pitch=0.6),
                sfx("supernova", t, 1.0, duck=True), sfx("story_reveal", t + 0.6, 0.8)]
    if name == "onde":
        return [amb("story_amb_space", 0.4), sfx("story_glow", 0.6, 0.6)] + [sfx("story_glow", t, 0.4, pitch=1.0 + 0.06 * i) for i, t in enumerate(extra or [])]
    if name == "bannieres":
        return [amb("story_amb_night", 0.4)] + [sfx("story_beacon", 2.2 + 1.6 * g, 0.5, pan=round((150 + g * 220) / 480.0 - 1.0, 2)) for g in range(4)]
    return [amb("story_amb_space", 0.3), sfx("story_reveal", 1.0, 0.9, duck=True), sfx("checkpoint", 1.4, 0.5)]


# =================================================================== the scenes file
def read_script():
    with open(SCRIPT, encoding="utf-8") as f:
        return json.load(f)


def build_scenes():
    script = read_script()
    scenes, stills = {}, {}
    for shot in script["shots"]:
        name = shot["scene"]
        D = float(shot.get("duration") or S.shot_duration(shot.get("text", "")))
        st, extra = SCENES[name](D, shot)
        stills[name] = st.pop("still")
        st["sfx"] = [x for x in sounds(name, D, extra) if x["t"] < D]
        stage = {"size": [W, H], "length": round(D, 3)}
        stage.update(st)
        scenes[name] = stage
    rel = script.get("scenes", "raids/asterion_victoire_scenes.json")
    out = {"format": "cosmic.film_scenes", "version": 1,
           "comment": ("The animated scenes of the film of Asterion's fall (2.72), named by data/raids/asterion_victoire.json (its captions, "
                       "titles and lengths: edit them there). Written by tools/assetgen/environment/story_asterion_victoire.py: do not edit. "
                       "Each scene is a stage of the stories' format (game/content/BossStories.h); its `length` is the seconds its keys were "
                       "written for (stretched to the script's length)."),
           "scenes": scenes}
    return {os.path.join("data", rel): A.dump_scenes(out).encode("utf-8")}, stills


def build_all():
    files = {os.path.join("assets", k): v for k, v in build_assets().items()}
    scenes, stills = build_scenes()
    files.update(scenes)
    return files, stills


def check():
    a, _ = build_all()
    b, _ = build_all()
    errs = []
    if a != b:
        errs.append("story_asterion_victoire: two builds differ (not deterministic)")
    for rel, data in sorted(a.items()):
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            errs.append(f"{rel}: missing (python3 tools/assetgen/environment/story_asterion_victoire.py)")
            continue
        with open(p, "rb") as f:
            if f.read() != data:
                errs.append(f"{rel}: not the generator's output (regenerate)")
    return errs


def storyboard(path):
    """One frame per shot (its still, as the game draws it without its live particles), labelled in French
    with its number, its scene and its caption or title."""
    files, stills = build_all()
    A.write(files)
    S.Anim._cache.clear()
    script = read_script()
    with open(os.path.join(REPO, "data", script.get("scenes", "raids/asterion_victoire_scenes.json")), encoding="utf-8") as f:
        scenes = json.load(f)["scenes"]
    fw, fh, lab = 480, 270, 44
    cols = 2
    rows = (len(script["shots"]) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (fw + 12) + 12, rows * (fh + lab + 12) + 60), (16, 14, 26))
    d = ImageDraw.Draw(sheet)
    fonts = sorted(f for f in os.listdir(os.path.join(ASSETS, "fonts")) if f.endswith(".ttf"))
    pick = "ChakraPetch-Regular.ttf" if "ChakraPetch-Regular.ttf" in fonts else fonts[0]
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", pick), 15)
    big = ImageFont.truetype(os.path.join(ASSETS, "fonts", pick), 22)
    total = sum(float(s.get("duration", 0)) for s in script["shots"])
    d.text((12, 16), f"LA CHUTE D'ASTERION — le film de la victoire, plan par plan ({total:.0f} s)", fill=(255, 210, 160), font=big)
    for i, shot in enumerate(script["shots"]):
        r, c = divmod(i, cols)
        x, y = 12 + c * (fw + 12), 60 + r * (fh + lab + 12)
        img = S.render(scenes[shot["scene"]], stills[shot["scene"]], size=(fw, fh))
        sheet.paste(img, (x, y))
        words = shot.get("text") or " / ".join(f"{t['text']} — {t.get('subtitle', '')}" for t in shot.get("titles", [])[:2])
        full = f"{i + 1}. {shot['scene']} ({float(shot.get('duration', 0)):.0f} s) : {words}"
        label = full
        while d.textlength(label, font=font) > fw and len(label) > 8:
            label = label[:-2]
        if label != full:
            label = label[:-1] + "…"
        d.text((x, y + fh + 8), label, fill=(232, 228, 246), font=font)
    sheet.save(path)
    return path


if __name__ == "__main__":
    if "--check" in sys.argv:
        e = check()
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    if "--storyboard" in sys.argv:
        print(storyboard(sys.argv[sys.argv.index("--storyboard") + 1]))
    else:
        for p in A.write(build_all()[0]):
            print(os.path.relpath(p, REPO), os.path.getsize(p))
