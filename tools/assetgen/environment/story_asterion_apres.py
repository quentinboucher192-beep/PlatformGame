#!/usr/bin/env python3
"""« Après Asterion » (2.81): what the fall of Asterion changes in the world, in eight animated shots, played by
the stories' scene (BossStoryScene, kind « film ») right after the victory film for every player of a Heart
instance and, at the same moment (a start time stamped by the server), for the online members of the clans
present at the fall; then from the sheet of the event, the clan's raid recap and the Collection.

The script — each shot's caption, its titles, the place of the clans' names and its length — is hand-written
in data/raids/asterion_apres.json (a length changed there stretches the scene, nothing to regenerate). This
module writes what the script names as « scenes »: data/raids/asterion_apres_scenes.json (the format of
game/content/BossStories.h, as story_asterion.py and story_asterion_victoire.py write theirs), and the pictures
of its own: the golden cloud left where the titan stood (assets/stories/asterion/nuage_or.png), the new star
(etoile.png) and the forgotten worlds lighting up again (the sheet « renaissance »).

Nothing made up: the titan's chunks, Nova, the beacons, banners and ridge are those of the first two films;
our planets their own pictures; Sylvanox its backgrounds; its peoples the villagers and ship of the Monolithe's
story (« Puis les gens partirent vers les étoiles ») ; the Phénix its nest of the Confins and its own sheet (« il
ne sait plus s'arrêter, de peur que la nuit gagne »), at rest the frame of its idle loop where its wings are folded ; the dead star at the centre Lyse, of Singularis's story.

  1. silence   a golden cloud where the titan stood, its chunks drifting, the last ring of the wave fading
  2. eclats    the chunks of the worlds it was made of gather, and three forgotten worlds light up again
  3. systeme   the sun and our five planets: the gold light passes, their skies clear (the intro's shot, without it)
  4. retour    on Sylvanox, a ship lands, villagers come back and look up at a new light; the children wave
  5. phenix    the Confins: a golden dawn the Phénix did not light; its flames calm down, it sleeps
  6. lyse      the dead star at the centre: a spark of the wave settles on it, it quivers
  7. clans     the new star, the clans' banners around it (the game writes the names of the clans of the fall)
  8. veille    Nova on the ridge, the beacons of every clan lit to the horizon (« LA GALAXIE EST LIBRE »)

It also writes the thumbnails of the game's films for their lists (ADMINISTRATION › Films, Collection › Galerie ›
Films: ui/screens/FilmCatalog), each a still of one of its scenes as the game draws it (assets/ui/films/): Asterion's
film, the film of its fall, this one, the trailer. They are rendered from the scenes and the pictures on disk: after
a change of one of them, run its generator first, then this one.

    python3 tools/assetgen/environment/story_asterion_apres.py [--storyboard FILE.png] [--check]

Deterministic (fixed seeds): `--check` builds everything twice and compares the bytes with each other and with
the files."""
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
SCRIPT = os.path.join(REPO, "data", "raids", "asterion_apres.json")
W, H = S.W, S.H
Track, sprite, layer, glo = A.Track, A.sprite, A.layer, A.glo
GOLD, EMBER, CYAN, VIOLET, CLANS = A.GOLD, A.EMBER, A.CYAN, A.VIOLET, A.CLANS
HALF = 128 * S.PIX  # half the drawn size of a 256-pixel picture at scale 1
STORIES = "animations/stories/"


# =================================================================== its own pictures
def img_cloud(w=320, h=180, seed=81):
    """The golden cloud left where the titan stood: gold and rose veils, a brighter heart, soft edges (additive)."""
    n = A.value_noise(w, h, 50, seed)
    m = A.value_noise(w, h, 22, seed + 4)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    core = np.clip(1.0 - r, 0, 1) ** 1.6
    a = np.clip((n - 0.38) * 2.2, 0, 1) * (0.45 + 0.55 * m) * core
    out = np.zeros((h, w, 4), np.float32)
    c1, c2, c3 = A.col("#ffc050")[:3], A.col("#ff8a5a")[:3], A.col("#fff2c0")[:3]
    mix = np.clip((m - 0.35) * 2, 0, 1)[..., None]
    out[..., :3] = c1 * (1 - mix) + c2 * mix
    hot = np.clip((core - 0.55) * 2.5, 0, 1)[..., None]
    out[..., :3] = out[..., :3] * (1 - hot) + c3 * hot
    out[..., 3] = np.floor(np.clip(a * 2.0, 0, 1) * 8.0) / 8.0 * 230
    out[..., :3] = np.floor(out[..., :3] / 16.0) * 16.0
    return A._rgba(out)


def img_star(size=256):
    """The new star: a white heart, a gold halo and eight long rays (one per player of a group), drawn additively."""
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    dx, dy = (xx - c) / c, (yy - c) / c
    r = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    core = np.exp(-(r / 0.08) ** 2)
    halo = np.exp(-(r / 0.3) ** 2) * 0.7
    rays = np.abs(np.cos(ang * 4.0)) ** 60 * np.clip(1.0 - r, 0, 1) ** 2.2
    small = np.abs(np.cos(ang * 4.0 + math.pi / 4)) ** 90 * np.clip(1.0 - r * 1.8, 0, 1) ** 2 * 0.6
    out = np.zeros((size, size, 4), np.float32)
    out[..., :3] = (A.col("#ffffff")[:3] * (core + rays * 0.6)[..., None] + A.col("#ffd27a")[:3] * halo[..., None]
                    + A.col("#9ff6ff")[:3] * small[..., None])
    out[..., 3] = np.clip((core + halo + rays + small) * 255, 0, 255)
    out[..., :3] = np.clip(out[..., :3], 0, 255)
    return A._rgba(out)


PALETTES = {  # a world lit again: its sea, its land, its high land, the colour of its lights
    "vert": ("#1d5a6a", "#4aa86a", "#2a7a4a", "#c8ffb0"),
    "glace": ("#2b4a8a", "#cfeeff", "#8fc8e8", "#e6fffb"),
    "ambre": ("#5a2a6a", "#d89a4a", "#9a5a2a", "#ffe6a0"),
}


def frame_reborn(kind, k, n, size=80, seed=0):
    """A forgotten world lighting up again, `k` of `n` steps: from a dark ball to its day side and its lights."""
    sea, land1, land2, lamp = (A.col(h)[:3] for h in PALETTES[kind])
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = (size - 1) / 2.0
    rr = size / 2.0 - 2
    dx, dy = (xx - c) / rr, (yy - c) / rr
    d2 = dx * dx + dy * dy
    inside = d2 <= 1.0
    dz = np.sqrt(np.clip(1 - d2, 0, 1))
    land = A.value_noise(size, size, 20, 40 + seed)
    base = np.where((land > 0.5)[..., None], land1, sea)
    base = np.where((land > 0.62)[..., None], land2, base)
    u = k / max(1, n - 1)
    light = np.clip(-dx * 0.7 - dy * 0.4 + dz * 0.6, 0, 1)
    rgb = base * (0.08 + (0.1 + 0.9 * u) * light[..., None])
    rim = np.clip((d2 - 0.8) * 6, 0, 1)
    rgb += rim[..., None] * A.col("#ffe6a8")[:3] * 0.6 * u
    out = np.zeros((size, size, 4), np.float32)
    out[..., :3] = rgb
    out[..., 3] = np.where(inside, 255, 0)
    rng = np.random.default_rng(90 + seed)
    lit = int(round(26 * u))
    placed = 0
    while placed < 26:
        x, y = int(rng.integers(3, size - 3)), int(rng.integers(3, size - 3))
        ddx, ddy = (x - c) / rr, (y - c) / rr
        if ddx * ddx + ddy * ddy > 0.8 or land[y, x] <= 0.5:
            continue
        if placed < lit:
            out[y, x, :3] = lamp
        placed += 1
    edge = inside & ~np.roll(inside, 1, 0) | inside & ~np.roll(inside, -1, 0) | inside & ~np.roll(inside, 1, 1) | inside & ~np.roll(inside, -1, 1)
    out[edge, :3] *= 0.5
    return A._rgba(out)


PHENIX_JSON = os.path.join(ASSETS, "animations", "phenix.json")


def frames_resting():
    """The Phénix at rest: the frame of its own idle loop where its wings are folded (cut from its sheet), and the same
    one a pixel lower — two breaths, slow."""
    with open(PHENIX_JSON, encoding="utf-8") as f:
        meta = json.load(f)
    img = np.array(Image.open(os.path.join(ASSETS, meta["texture"])).convert("RGBA"))
    fw, fh = meta["frameWidth"], meta["frameHeight"]
    clip = meta["clips"]["idle"]
    k = clip.get("start", 0) + clip["count"] // 2
    x, y = k * fw, clip["row"] * fh
    a = img[y:y + fh, x:x + fw].copy()
    b = np.zeros_like(a)
    b[1:] = a[:-1]
    return [a, b], fw, fh, meta["pivot"]


def build_assets():
    Sheet, dumps = A.sheet_cls()
    files = {A.TEX + "nuage_or.png": A.png_bytes(img_cloud()), A.TEX + "etoile.png": A.png_bytes(img_star())}
    rest, fw, fh, pivot = frames_resting()
    sh = Sheet("", fw, fh, list(pivot))
    sh.add("dort", rest, durations=[1.4, 1.4], loop=True)
    sh.texture = A.TEX + "phenix_repos.png"
    files[sh.texture] = A.png_bytes(sh.image())
    files[A.ANIM + "phenix_repos.json"] = (dumps(sh.meta()) + "\n").encode("utf-8")
    n = 8
    sh = Sheet("", 80, 80, [40, 40])
    for i, kind in enumerate(PALETTES):
        sh.add(kind, [frame_reborn(kind, k, n, seed=i) for k in range(n)], durations=[0.25] * n, loop=False, next=kind + "_vie")
        sh.add(kind + "_vie", [frame_reborn(kind, n - 1, n, seed=i)], fps=1, loop=True)
    sh.texture = A.TEX + "renaissance.png"
    files[sh.texture] = A.png_bytes(sh.image())
    files[A.ANIM + "renaissance.json"] = (dumps(sh.meta()) + "\n").encode("utf-8")
    return files


# =================================================================== pieces
def cloud(id_, pos, scale, track=None, depth=0.5, alpha=0.8):
    tr = track or Track(pos=(pos[0] - 160 * S.PIX * scale, pos[1] - 90 * S.PIX * scale), scale=scale, alpha=alpha)
    return {"id": id_, "layer": A.TEX + "nuage_or.png", "depth": depth, "blend": "add", "keys": tr.keys()}


def star(id_, pos, scale, track=None, depth=1.0, alpha=1.0, pulse=None):
    tr = track or Track(pos=(pos[0] - HALF * scale, pos[1] - HALF * scale), scale=scale, alpha=alpha)
    e = {"id": id_, "layer": A.TEX + "etoile.png", "depth": depth, "blend": "add", "keys": tr.keys()}
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    return e


def chunks(rng, n, area, depth, prefix="eclat", tint=None, spin=(-60, 60)):
    els = []
    for i in range(n):
        kind = ("roche", "magma", "glace")[i % 3]
        x0, y0 = float(rng.uniform(area[0], area[0] + area[2])), float(rng.uniform(area[1], area[1] + area[3]))
        tr = Track(pos=(x0, y0), scale=float(rng.uniform(1.0, 2.2)), rot=float(rng.uniform(0, 360)))
        tr.to(20.0, "linear", pos=(x0 + float(rng.uniform(-30, 30)), y0 + float(rng.uniform(-12, 12))), rot=float(rng.uniform(*spin)) * 3)
        tr.clip(0, kind)
        els.append(sprite(f"{prefix}{i + 1}", "eclat", tr, depth=depth, tint=tint))
    return els


# =================================================================== the eight scenes
def scene_silence(D, script):
    """A golden cloud where the titan stood, its chunks drifting, the last ring of the wave fading."""
    O = (480, 270)
    els = A.space(D, drift=2.0, nebula=0.35)
    els.append(A.stars(0.8))
    els.append(glo("lueur", GOLD, 380, Track(pos=O, alpha=0.5).to(D, alpha=0.75), depth=0.4, pulse=(0.15, 3.2)))
    els.append(cloud("nuage", O, 1.5, Track(pos=(O[0] - 160 * S.PIX * 1.4, O[1] - 90 * S.PIX * 1.4), scale=1.4, alpha=0.0)
                     .to(1.5, alpha=0.75).to(D, pos=(O[0] - 160 * S.PIX * 1.6, O[1] - 90 * S.PIX * 1.6), scale=1.6, alpha=0.9), depth=0.5))
    ring = Track(pos=(O[0] - HALF * 3.2, O[1] - HALF * 3.2), scale=3.2, alpha=0.45)
    ring.to(D, "out", pos=(O[0] - HALF * 5.0, O[1] - HALF * 5.0), scale=5.0, alpha=0.0)
    els.append({"id": "onde", "layer": A.TEX + "onde.png", "depth": 0.6, "blend": "add", "tint": GOLD, "keys": ring.keys()})
    rng = np.random.default_rng(8101)
    els += chunks(rng, 22, (80, 60, 800, 420), 0.8, tint="#9a8aa8")
    els.append(glo("dernier_battement", "#fff2c0", 60, Track(pos=O, alpha=0.6).to(2.0, alpha=0.6).to(4.0, alpha=0.0)))
    els.append(particles("poussiere", "dust", "#ffe6b0", (120, 80, 720, 380), [K(0, alpha=0.6)]))
    cam = A.camera((0, O, 1.45, "linear"), (D, O, 1.05, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.6), None


def scene_eclats(D, script):
    """The chunks of the worlds it was made of gather, and three forgotten worlds light up again."""
    els = A.space(D, drift=3.0, nebula=0.45)
    els.append(A.stars(0.8))
    els.append(cloud("nuage", (480, 270), 1.8, depth=0.2, alpha=0.4))
    worlds = [("vert", (250, 230), 1.3, 3.4), ("glace", (520, 170), 1.0, 4.6), ("ambre", (720, 310), 1.5, 5.8)]
    rng = np.random.default_rng(8102)
    times = []
    for w, (kind, c, sc, t_lit) in enumerate(worlds):
        for i in range(10):  # its chunks spiral in
            ang = float(rng.uniform(0, A.TAU))
            d0 = float(rng.uniform(110, 220))
            p0 = (c[0] + math.cos(ang) * d0, c[1] + math.sin(ang) * d0 * 0.7)
            mid = (c[0] + math.cos(ang + 1.2) * d0 * 0.45, c[1] + math.sin(ang + 1.2) * d0 * 0.3)
            t0 = 0.3 + float(rng.uniform(0, 0.8)) + 0.5 * w
            tr = Track(pos=p0, scale=float(rng.uniform(1.4, 2.4)), rot=float(rng.uniform(0, 360)))
            tr.to(t0, pos=p0).to((t0 + t_lit) / 2, "linear", pos=mid, rot=float(rng.uniform(200, 400)))
            tr.to(t_lit - 0.1, "in", pos=c, rot=float(rng.uniform(500, 800)), alpha=1.0).to(t_lit, alpha=0.0)
            tr.clip(0, ("roche", "magma", "glace")[(i + w) % 3])
            els.append(sprite(f"monde{w + 1}_eclat{i + 1}", "eclat", tr))
        tr = Track(pos=c, scale=sc, alpha=0.0).to(t_lit - 0.2, alpha=0.0).to(t_lit, alpha=1.0, ease="out").clip(0, kind).clip(t_lit, kind)
        els.append(glo(f"monde{w + 1}_halo", PALETTES[kind][3], 90 * sc, Track(pos=c, alpha=0.0).to(t_lit, alpha=0.0).to(t_lit + 0.5, alpha=0.8, ease="out")
                       .to(D, alpha=0.5), pulse=(0.2, 2.4, w * 0.3)))
        els.append(sprite(f"monde{w + 1}", "renaissance", tr))
        els.append(glo(f"monde{w + 1}_eclair", "#ffffff", 70 * sc, Track(pos=c, alpha=0.0).to(t_lit - 0.05, alpha=0.0).to(t_lit, alpha=1.0, ease="out")
                       .to(t_lit + 0.7, alpha=0.0)))
        times.append(t_lit)
    els.append(particles("eclats", "sparks", "#fff2c0", (160, 80, 640, 360), [K(0, alpha=0.3), K(3.0, alpha=0.8)]))
    cam = A.camera((0, (480, 260), 1.1, "linear"), (D, (500, 250), 1.3, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=7.4), times


def scene_systeme(D, script):
    """The sun and our five planets: the gold light passes, their skies clear (the intro's shot, without the titan)."""
    els = A.space(D, drift=5.0, nebula=0.5)
    els.append(A.stars(0.85))
    sun = (150, 150)
    els.append(glo("soleil_lueur", GOLD, 240, Track(pos=sun, alpha=0.7).to(D, alpha=1.0), depth=0.3, pulse=(0.2, 1.8)))
    els.append(layer("soleil", "ui/planets/sun.png", 0.3, (sun[0] - 64, sun[1] - 64), scale=0.25))
    # where the titan was, on the right of the intro's shot: its golden cloud
    els.append(cloud("nuage", (850, 340), 1.1, depth=0.25, alpha=0.7))
    planets = [("sylvanox", 0.15, "#39f0d8"), ("pyralis", 0.13, "#ff8a3d"), ("glacior", 0.14, "#a8e0ff"), ("aethra", 0.16, "#c9a2ff"),
               ("confins", 0.13, "#ff7a6a")]
    band = Track(pos=(980, 300), alpha=0.0).to(0.4, alpha=0.9).to(D - 1.0, "linear", pos=(60, 220), alpha=0.6).to(D, alpha=0.0)
    els.append(glo("lumiere", "#fff2c0", 200, band, depth=0.5))
    times = []
    for i, (p, sc, tint) in enumerate(planets):
        x, y = 250 + i * 92, 205 + i * 34
        size = 256 * S.PIX * sc
        reach = 0.4 + (980 - x) / 920.0 * (D - 1.4)
        times.append(reach)
        els.append(glo(p + "_ciel", tint, size * 0.8, Track(pos=(x, y), alpha=0.15).to(reach, alpha=0.15).to(reach + 0.6, alpha=0.85, ease="out")
                       .to(D, alpha=0.6), depth=0.35 + 0.05 * i, pulse=(0.15, 2.6, i * 0.2)))
        els.append(layer(p, f"ui/planets/{p}.png", 0.35 + 0.05 * i, (x - size / 2, y - size / 2), scale=sc))
    els.append(particles("poussiere", "dust", "#ffe6b0", (100, 80, 760, 360), [K(0, alpha=0.4)]))
    cam = A.camera((0, (430, 260), 1.3, "linear"), (D, (520, 270), 1.15, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.75), times


def scene_retour(D, script):
    """On Sylvanox, a ship lands, villagers come back and look up at a new light; the children wave."""
    s = 0.8
    pos = ((960 - 1280 * s) / 2, (540 - 720 * s) / 2)
    G = 436
    els = []
    for name, depth, v in (("sky", 0.05, 2), ("far", 0.2, 5), ("mid", 0.45, 10)):
        els.append(layer(name, f"backgrounds/sylvanox/{name}.png", depth, pos, scale=s, drift=(-v, 0), repeat=True))
    els.append(layer("near", "backgrounds/sylvanox/near.png", 0.75, pos, scale=s, drift=(-16, 0), repeat=True))
    els.append(glo("aube", GOLD, 300, Track(pos=(420, 80), alpha=0.2).to(D, alpha=0.65), depth=0.06, pulse=(0.1, 3.0)))
    els.append(star("etoile", (420, 80), 0.45, depth=0.06, pulse=(0.15, 2.2)))
    els.append(sprite("sol", STORIES + "monolithe_sol.json", Track(pos=(-280, G - 32)).clip(0, "idle"), tint="#5a8a80") | {"repeat": True})
    # the ship comes down behind them, its lights on
    ship = Track(pos=(760, 120), scale=1.6, alpha=0.0).to(0.4, alpha=1.0).to(4.2, "out", pos=(740, G - 10)).clip(0, "launch").clip(4.2, "idle")
    els.append(sprite("vaisseau", STORIES + "monolithe_vaisseau.json", ship, shadow=S.shade(70, alpha=0.35)))
    els.append(glo("vaisseau_feux", "#9ff6ff", 70, Track(pos=(760, 150), alpha=0.8).to(4.2, "out", pos=(740, G - 30), alpha=0.3)))
    people = [("monolithe_villageois", 610, 2.0, "look_up"), ("monolithe_villageoise", 660, 2.6, "look_up"), ("monolithe_enfant1", 560, 3.2, "cheer"),
              ("monolithe_enfant2", 700, 3.8, "wave"), ("monolithe_villageois", 520, 4.4, "gaze"), ("monolithe_villageoise", 470, 5.0, "wave")]
    for k, (who, x, t, pose) in enumerate(people):
        x0 = 760 - 10 * k
        tr = Track(pos=(x0, G), scale=1.5, alpha=0.0).to(t - 0.3, alpha=0.0).to(t, alpha=1.0).to(t + 1.6, "linear", pos=(x, G))
        tr.clip(0, "walk", offset=k * 0.11).clip(t + 1.6, pose, offset=k * 0.07)
        tr.flip = True
        els.append(sprite(f"gens{k + 1}", STORIES + who + ".json", tr, shadow=S.shade(26, alpha=0.3)))
    els.append(particles("lucioles", "sparks", "#c8ffb0", (60, 220, 840, 220), [K(0, alpha=0.6)]))
    els.append(layer("lianes", "backgrounds/sylvanox/fg_vines.png", 1.3, pos, scale=s, drift=(-30, 0), repeat=True, alpha=0.6))
    cam = A.camera((0, (560, 300), 1.35, "linear"), (D, (560, 260), 1.15, "inout"))
    return dict(backdrop="#06070e", camera=cam, elements=els, still=8.4), None


def scene_phenix(D, script):
    """The Confins: a golden dawn the Phénix did not light; its flames calm down, it sleeps."""
    els = [
        {"id": "ciel", "sprite": STORIES + "phenix_ciel_nuit.json", "depth": 0.03, "keys": [K(0, pos=(-80, -30), clip="idle")]},
        {"id": "aube", "sprite": STORIES + "phenix_ciel_aube.json", "depth": 0.03, "keys": [K(0, pos=(-80, -30), alpha=0.1, clip="idle"),
                                                                                           K(D, alpha=1.0)]},
        {"id": "etoiles", "sprite": STORIES + "phenix_etoiles.json", "depth": 0.08, "drift": [4.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-80, -20), alpha=0.8, clip="idle"), K(D * 0.6, alpha=0.1)]},
    ]
    els.append(star("etoile", (240, 110), 0.55, Track(pos=(240 - HALF * 0.55, 150 - HALF * 0.55), scale=0.55, alpha=0.2)
                    .to(D, pos=(240 - HALF * 0.55, 100 - HALF * 0.55), alpha=1.0), depth=0.1, pulse=(0.12, 2.4)))
    els.append(glo("aube_or", GOLD, 360, Track(pos=(240, 200), alpha=0.1).to(D, pos=(240, 140), alpha=0.8), depth=0.1))
    els += [
        {"id": "nuages_loin", "sprite": STORIES + "phenix_nuages.json", "depth": 0.12, "drift": [-6.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-200, 120), alpha=0.7, clip="idle", offset=0.3)]},
        {"id": "volcans_loin", "sprite": STORIES + "phenix_volcans_loin.json", "depth": 0.25, "keys": [K(0, pos=(-240, 170), clip="idle")]},
        {"id": "brume", "sprite": STORIES + "phenix_brume.json", "depth": 0.4, "drift": [10.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-160, 330), alpha=0.8, clip="idle")]},
        {"id": "volcans", "sprite": STORIES + "phenix_volcans.json", "depth": 0.55, "keys": [K(0, pos=(-320, 140), clip="idle", offset=0.2)]},
        {"id": "verre", "sprite": STORIES + "phenix_verre.json", "depth": 1.0, "keys": [K(0, pos=(-280, 400), clip="idle")]},
        {"id": "pics", "sprite": STORIES + "phenix_pics.json", "depth": 0.7, "keys": [K(0, pos=(-320, 220), clip="idle", offset=0.3)]},
        {"id": "pic", "sprite": STORIES + "phenix_rocher.json", "depth": 1.0, "keys": [K(0, pos=(520, 560), clip="idle", scale=2.3)]},
        {"id": "nid_fond", "sprite": STORIES + "phenix_nid.json", "depth": 1.0, "keys": [K(0, pos=(520, 330), clip="back")]},
    ]
    els.append(glo("nid_lueur", EMBER, 200, Track(pos=(520, 260), alpha=0.7).to(D * 0.7, alpha=0.15), pulse=(0.25, 0.8)))
    # its flames die down (its burning sheet fades), it breathes slower, it sleeps
    burn = Track(pos=(520, 326), scale=0.82, alpha=0.8).to(D * 0.45, alpha=0.0).clip(0, "roar")
    els.append(sprite("phenix_brasier", "animations/phenix_p5.json", burn))
    t_rest = D * 0.45
    awake = Track(pos=(520, 326), scale=0.82).to(t_rest - 0.8, alpha=1.0).to(t_rest + 0.6, pos=(520, 334), alpha=0.0).clip(0, "idle")
    els.append(sprite("phenix", "animations/phenix.json", awake, shadow={"alpha": 0.35, "size": [150.0, 24.0], "light": [240.0, 140.0]}))
    asleep = Track(pos=(520, 334), scale=0.82, alpha=0.0).to(t_rest - 0.8, alpha=0.0).to(t_rest + 0.6, pos=(520, 340), alpha=1.0).clip(0, "dort")
    els.append(sprite("phenix_repos", "phenix_repos", asleep))
    els.append({"id": "nid_devant", "sprite": STORIES + "phenix_nid.json", "depth": 1.0, "keys": [K(0, pos=(520, 330), clip="front")]})
    els.append(particles("braises", "embers", "#ffb066", (320, 70, 400, 260), [K(0, alpha=0.9), K(D * 0.6, alpha=0.1)]))
    els.append(particles("poussiere", "dust", "#ffe6b0", (0, 60, 960, 300), [K(0, alpha=0.0), K(D * 0.5, alpha=0.6)]))
    els.append({"id": "avant", "sprite": STORIES + "phenix_avant.json", "depth": 1.35, "keys": [K(0, pos=(-420, 316), clip="idle")]})
    cam = A.camera((0, (520, 250), 1.3, "linear"), (D, (480, 236), 1.1, "inout"))
    return dict(backdrop="#07040a", camera=cam, elements=els, still=D * 0.8), None


def scene_lyse(D, script):
    """The dead star at the centre: a spark of the wave settles on it, it quivers."""
    O = (520, 270)
    t_touch = 4.2
    els = [
        {"id": "cosmos", "sprite": STORIES + "singularis_cosmos.json", "depth": 0.03, "keys": [K(0, pos=(-80, -30), clip="idle", alpha=0.6)]},
        {"id": "etoiles", "sprite": STORIES + "singularis_etoiles.json", "depth": 0.08, "drift": [3.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-80, -30), clip="idle")]},
        {"id": "voile", "sprite": STORIES + "singularis_voile.json", "depth": 0.15, "drift": [-6.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-200, 60), alpha=0.5, clip="idle", offset=0.3)]},
    ]
    els.append(glo("lyse_ombre", "#3a2a5a", 260, Track(pos=O, alpha=0.6), depth=0.9))
    halo = Track(pos=O, alpha=0.05).to(t_touch, alpha=0.05).to(t_touch + 0.5, alpha=0.5, ease="out").to(D, alpha=0.3)
    els.append(glo("lyse_halo", "#ffe9a8", 220, halo, depth=0.9, pulse=(0.3, 1.6)))
    lyse = Track(pos=O, scale=1.3, alpha=0.75).to(t_touch, alpha=0.75).to(t_touch + 0.6, alpha=1.0).clip(0, "dim").clip(t_touch, "spark")
    els.append(sprite("lyse", STORIES + "singularis_lyse.json", lyse, depth=0.9))
    # the spark of the wave: from the left, a little gold light that settles on her
    sp = Track(pos=(-40, 180), alpha=0.0).to(0.5, alpha=1.0).to(t_touch, "inout", pos=(O[0] - 20, O[1] - 10)).to(t_touch + 0.4, pos=O, alpha=0.0)
    els.append(glo("etincelle", "#fff2c0", 26, sp, depth=0.9))
    els.append(glo("etincelle_halo", GOLD, 70, Track(pos=(-40, 180), alpha=0.0).to(0.5, alpha=0.6).to(t_touch, "inout", pos=(O[0] - 20, O[1] - 10))
                   .to(t_touch + 0.4, pos=O, alpha=0.0), depth=0.9))
    els.append(glo("frisson", "#ffffff", 120, Track(pos=O, alpha=0.0).to(t_touch, alpha=0.0).to(t_touch + 0.1, alpha=0.9, ease="out").to(t_touch + 1.2, alpha=0.0),
                   depth=0.9))
    els.append(particles("trainee", "sparks", "#ffe6a8", (40, 160, 440, 140), [K(0, alpha=0.0), K(0.6, alpha=0.7), K(t_touch, alpha=0.0)]))
    els.append(particles("poussiere", "dust", "#ffd27a", (0, 60, 960, 420), [K(0, alpha=0.2), K(t_touch + 0.5, alpha=0.6)]))
    cam = A.camera((0, (380, 240), 1.15, "linear"), (t_touch, (500, 262), 1.35, "inout"), (D, O, 1.5, "inout"))
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=[A.shake(t_touch, t_touch + 0.8, 3.0)], still=t_touch - 0.9), t_touch


def scene_clans(D, script):
    """The new star, the clans' banners all around it (the game writes the names of the clans of the fall over it)."""
    O = (480, 250)
    els = A.space(D, drift=2.0, nebula=0.5)
    els.append(A.stars(0.9))
    els.append(cloud("nuage", O, 1.4, depth=0.3, alpha=0.5))
    els.append(glo("halo", GOLD, 460, Track(pos=O, alpha=0.3).to(D, alpha=0.5), depth=0.5, pulse=(0.12, 3.0)))
    els.append(star("etoile", O, 1.6, Track(pos=(O[0] - HALF * 1.0, O[1] - HALF * 1.0), scale=1.0, alpha=0.4)
                    .to(2.0, pos=(O[0] - HALF * 1.6, O[1] - HALF * 1.6), scale=1.6, alpha=0.75, ease="out"), depth=0.5, pulse=(0.12, 2.2)))
    for i in range(12):
        a = i / 12 * A.TAU + 0.25
        p = (O[0] + math.cos(a) * 400, O[1] + 70 + math.sin(a) * 190)
        bt = Track(pos=p, scale=1.3, alpha=0.0).to(0.3 + 0.18 * i, alpha=0.0).to(0.6 + 0.18 * i, alpha=0.9)
        bt.clip(0, "wave", offset=i * 0.11)
        els.append(sprite(f"banniere{i + 1}", "banniere", bt, tint=CLANS[i % 4], depth=0.85))
    els.append(particles("eclats", "sparks", GOLD, (180, 80, 600, 360), [K(0, alpha=0.4)]))
    cam = A.camera((0, O, 1.25, "linear"), (D, (O[0], O[1] + 20), 1.05, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.5), None


def scene_veille(D, script):
    """Nova on the ridge, the beacons of every clan lit to the horizon, the new star high above."""
    els = A.space(D, drift=3.0, nebula=0.5)
    els.append(A.stars(0.9))
    sp = (480, 120)
    els.append(glo("aube", GOLD, 420, Track(pos=sp, alpha=0.35).to(D, alpha=0.7), depth=0.1, pulse=(0.1, 3.0)))
    els.append(star("etoile", sp, 0.9, Track(pos=(sp[0] - HALF * 0.9, sp[1] + 60 - HALF * 0.9), scale=0.9, alpha=0.6)
                    .to(D, pos=(sp[0] - HALF * 0.9, sp[1] - HALF * 0.9), alpha=1.0), depth=0.1, pulse=(0.12, 2.6)))
    rng = np.random.default_rng(8108)
    rx, ry = -160, 300
    els.append(layer("crete", "crete", 1.0, (rx, ry)))
    top = A.ridge_top()
    ground = lambda x: ry + S.PIX * float(top[int(min(len(top) - 1, max(0, (x - rx) / S.PIX)))]) + 3  # noqa: E731
    # beacons lighting up one after another to the horizon, left and right of Nova
    xs = [90, 200, 300, 660, 760, 870]
    for i, x in enumerate(xs):
        t = 0.6 + 0.55 * i
        b = Track(pos=(x, ground(x)), scale=1.0).clip(0, "off").clip(t, "on")
        els.append(sprite(f"balise{i + 1}", "balise", b))
        beam = Track(pos=(x, ground(x) - 56), scale=1.2, alpha=0.0).to(t, alpha=0.0).to(t + 0.5, alpha=0.65, ease="out")
        beam.clip(0, "idle")
        els.append(sprite(f"faisceau{i + 1}", "faisceau", beam, add=True))
        els.append(glo(f"balise{i + 1}_lueur", GOLD, 60, Track(pos=(x, ground(x) - 60), alpha=0.0).to(t, alpha=0.0).to(t + 0.3, alpha=0.8, ease="out"),
                       pulse=(0.25, 0.9, i * 0.13)))
    for g in range(2):  # two groups of the clans, waiting by the beacons
        cx = 250 if g == 0 else 720
        for k in range(4):
            x = cx + (k - 1.5) * 18 + float(rng.uniform(-3, 3))
            tr = Track(pos=(x, ground(x)), scale=1.25).clip(0, "idle", offset=float(rng.uniform(0, 1)))
            els.append(sprite(f"heros{g + 1}_{k + 1}", A.NOVA, tr, tint="#1a1630"))
        bt = Track(pos=(cx + 40, ground(cx + 40)), scale=1.4).clip(0, "wave", offset=g * 0.2)
        els.append(sprite(f"banniere{g + 1}", "banniere", bt, tint=CLANS[g + 1]))
    nx = 480
    nova = Track(pos=(nx, ground(nx)), scale=1.8).clip(0, "idle")
    els.append(sprite("nova", A.NOVA, nova))
    els.append(glo("nova_lueur", "#9fe8ff", 40, Track(pos=(nx, ground(nx) - 30), alpha=0.5), pulse=(0.2, 1.6)))
    els.append(particles("braises", "embers", GOLD, (0, 240, 960, 300), [K(0, alpha=0.5)]))
    cam = A.camera((0, (480, 330), 1.35, "linear"), (D, (480, 290), 1.08, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, still=D * 0.7), None


SCENES = {"silence": scene_silence, "eclats": scene_eclats, "systeme": scene_systeme, "retour": scene_retour, "phenix": scene_phenix,
          "lyse": scene_lyse, "clans": scene_clans, "veille": scene_veille}


def sounds(name, D, extra):
    if name == "silence":
        return [amb("story_amb_space", 0.35), sfx("story_glow", 1.0, 0.5, pitch=0.8), sfx("story_heartbeat", 2.2, 0.35, pitch=0.7)]
    if name == "eclats":
        return [amb("story_amb_space", 0.35), sfx("story_stone", 0.8, 0.4, pan=-0.3)] + [
            sfx("story_reveal", t, 0.6, pitch=0.9 + 0.08 * i) for i, t in enumerate(extra or [])]
    if name == "systeme":
        return [amb("story_amb_space", 0.35), sfx("story_glow", 0.4, 0.6)] + [
            sfx("story_glow", t, 0.35, pitch=1.0 + 0.05 * i) for i, t in enumerate(extra or [])]
    if name == "retour":
        return [amb("story_amb_night", 0.35), sfx("story_launch", 0.4, 0.5, pitch=0.8, pan=0.5), sfx("story_puff", 4.2, 0.5, pan=0.5),
                sfx("story_sing", 6.0, 0.45)]
    if name == "phenix":
        return [amb("story_amb_fire", 0.35, to=D * 0.6), amb("story_amb_night", 0.3, t=D * 0.4), sfx("story_wings", 1.0, 0.4),
                sfx("story_glow", D * 0.5, 0.5, pitch=0.8)]
    if name == "lyse":
        t = extra or 4.2
        return [amb("story_amb_space", 0.35), sfx("story_glow", 0.6, 0.4, pitch=1.2), sfx("story_sing", t, 0.6, duck=True), sfx("story_reveal", t + 0.4, 0.5)]
    if name == "clans":
        return [amb("story_amb_space", 0.3), sfx("story_reveal", 0.8, 0.8, duck=True)] + [
            sfx("story_beacon", 0.6 + 0.18 * i * 3, 0.3, pitch=0.9 + 0.04 * i) for i in range(4)]
    return [amb("story_amb_night", 0.35)] + [sfx("story_beacon", 0.6 + 0.55 * i, 0.4, pan=round(-0.8 + 0.32 * i, 2)) for i in range(6)] + [
        sfx("story_reveal", 1.2, 0.8, duck=True)]


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
    rel = script.get("scenes", "raids/asterion_apres_scenes.json")
    out = {"format": "cosmic.film_scenes", "version": 1,
           "comment": ("The animated scenes of the aftermath film of Asterion (2.81), named by data/raids/asterion_apres.json (its captions, titles, the place of "
                       "the clans' names and its lengths: edit them there). Written by tools/assetgen/environment/story_asterion_apres.py: do not edit. "
                       "Each scene is a stage of the stories' format (game/content/BossStories.h); its `length` is the seconds its keys were written "
                       "for (stretched to the script's length)."),
           "scenes": scenes}
    return {os.path.join("data", rel): A.dump_scenes(out).encode("utf-8")}, stills


THUMB = (320, 180)


def thumb_bytes(stage, t):
    import io
    img = S.render(stage, t, size=THUMB)
    buf = io.BytesIO()
    img.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def build_thumbs(own_scenes, own_stills):
    """The thumbnails of the films' lists: a still of one scene of each film (as the game draws it)."""
    import story_asterion_victoire as V
    out = {}
    intro, intro_stills = A.build_scenes()
    intro = json.loads(next(iter(intro.values())).decode("utf-8"))["scenes"]
    out["ui/films/asterion_film.png"] = thumb_bytes(intro["rugissement"], intro_stills["rugissement"])
    fall, fall_stills = V.build_scenes()
    fall = json.loads(next(iter(fall.values())).decode("utf-8"))["scenes"]
    out["ui/films/asterion_victoire.png"] = thumb_bytes(fall["effondrement"], fall_stills["effondrement"])
    out["ui/films/asterion_apres.png"] = thumb_bytes(own_scenes["clans"], own_stills["clans"])
    with open(os.path.join(REPO, "data", "trailer", "trailer.json"), encoding="utf-8") as f:
        trailer = json.load(f)
    shot = next(x for x in trailer["shots"] if x.get("scene") == "system")
    out["ui/films/trailer.png"] = thumb_bytes(shot["stage"], float(shot.get("duration", 3.0)) * 0.6)
    return out


def build_all():
    files = {os.path.join("assets", k): v for k, v in build_assets().items()}
    scenes, stills = build_scenes()
    files.update(scenes)
    own = json.loads(next(iter(scenes.values())).decode("utf-8"))["scenes"]
    S.Anim._cache.clear()
    files.update({os.path.join("assets", k): v for k, v in build_thumbs(own, stills).items()})
    return files, stills


def check():
    a, _ = build_all()
    b, _ = build_all()
    errs = []
    if a != b:
        errs.append("story_asterion_apres: two builds differ (not deterministic)")
    for rel, data in sorted(a.items()):
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            errs.append(f"{rel}: missing (python3 tools/assetgen/environment/story_asterion_apres.py)")
            continue
        with open(p, "rb") as f:
            if f.read() != data:
                errs.append(f"{rel}: not the generator's output (regenerate)")
    return errs


def storyboard(path):
    """One frame per shot (its still, as the game draws it without its live particles), labelled in French with its
    number, its scene and its caption or title; the shot of the clans shows where the game writes their names."""
    files, stills = build_all()
    A.write(files)
    S.Anim._cache.clear()
    script = read_script()
    with open(os.path.join(REPO, "data", script.get("scenes", "raids/asterion_apres_scenes.json")), encoding="utf-8") as f:
        scenes = json.load(f)["scenes"]
    fw, fh, lab = 480, 270, 64
    cols = 2
    rows = (len(script["shots"]) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (fw + 12) + 12, rows * (fh + lab + 12) + 60), (16, 14, 26))
    d = ImageDraw.Draw(sheet)
    fonts = sorted(f for f in os.listdir(os.path.join(ASSETS, "fonts")) if f.endswith(".ttf"))
    pick = "ChakraPetch-Regular.ttf" if "ChakraPetch-Regular.ttf" in fonts else fonts[0]
    font = ImageFont.truetype(os.path.join(ASSETS, "fonts", pick), 14)
    big = ImageFont.truetype(os.path.join(ASSETS, "fonts", pick), 22)
    total = sum(float(s.get("duration", 0)) for s in script["shots"])
    d.text((12, 16), f"APRÈS ASTERION — le monde d'après la chute, plan par plan ({total:.0f} s)", fill=(255, 210, 160), font=big)

    def wrap(text, width):
        lines, cur = [], ""
        for w in text.split(" "):
            cand = (cur + " " + w).strip()
            if cur and d.textlength(cand, font=font) > width:
                lines.append(cur)
                cur = w
            else:
                cur = cand
        if cur:
            lines.append(cur)
        return lines

    for i, shot in enumerate(script["shots"]):
        r, c = divmod(i, cols)
        x, y = 12 + c * (fw + 12), 60 + r * (fh + lab + 12)
        img = S.render(scenes[shot["scene"]], stills[shot["scene"]], size=(fw, fh))
        if shot.get("roster"):  # where the names go (the game writes them): a dashed frame
            ro = shot["roster"]["area"]
            dd = ImageDraw.Draw(img)
            bx = (ro[0] * fw, ro[1] * fh, (ro[0] + ro[2]) * fw, (ro[1] + ro[3]) * fh)
            for k in range(int(bx[0]), int(bx[2]), 10):
                dd.line((k, bx[1], min(k + 5, bx[2]), bx[1]), fill=(255, 210, 122))
                dd.line((k, bx[3], min(k + 5, bx[2]), bx[3]), fill=(255, 210, 122))
            dd.text((bx[0] + 8, bx[1] + 6), "[noms des clans de la chute, le vôtre en évidence]", fill=(255, 230, 180), font=font)
        sheet.paste(img, (x, y))
        words = shot.get("text") or " / ".join(f"{t['text']} — {t.get('subtitle', '')}" for t in shot.get("titles", [])[:2])
        if shot.get("text") and shot.get("titles"):
            words = shot["titles"][0]["text"] + " / " + words
        lines = wrap(f"{i + 1}. {shot['scene']} ({float(shot.get('duration', 0)):.0f} s) : {words}", fw)[:3]
        for k, ln in enumerate(lines):
            d.text((x, y + fh + 6 + k * 18), ln, fill=(232, 228, 246), font=font)
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
