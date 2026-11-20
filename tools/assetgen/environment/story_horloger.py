#!/usr/bin/env python3
"""« Le Balancier » and « Encore un tour » (2.82): the two transition films after Asterion, each about seventy seconds,
played by the stories' scene (BossStoryScene, kind « film ») and watched together, at a time stamped by the server.

  * « Le Balancier » (data/raids/asterion_balancier.json) follows « Après Asterion » when the Heart fell: the supernova
    fades, a ticking rises and goes wrong, the debris of Asterion freezes in mid-flight, a tear in time opens near Lyse
    and a watch the size of a moon comes out of it (its hands running wild, its balance rod snapped); behind its glass
    an old man looks at the frozen debris (« Mon balancier… »), his eyes start turning like gears (« Il faudra tout
    remonter. »).
  * « Encore un tour » (data/raids/asterion_tour.json) plays when a world event ends without the Heart's fall: wounded
    Asterion flees through the void, an immense mechanical hand catches it, golden threads of stolen time sew its
    wounds (« Encore un tour, mon vieux. »), and the watch comes out near Lyse with Asterion chained under it as a living
    balance wheel.
  * Both end on the same three shots, written once here: the watch hangs near Lyse, its dome lights up its six worlds
    and its hands tick again (`cadran`); the gear-eyes, close (`regard`); black, the ticking speeding up then stopping,
    « À SUIVRE » (`noir`).

The scripts — each shot's caption, its titles and its length — are hand-written in those two files (a length changed
there stretches the scene). This module writes what they name as « scenes » (data/raids/asterion_balancier_scenes.json,
asterion_tour_scenes.json: the format of game/content/BossStories.h), every picture of its own (assets/stories/horloger/,
assets/animations/stories/horloger_*.json, drawn by story_horloger_art.py) and the thumbnails of the two films for the
films' lists (assets/ui/films/asterion_balancier.png, asterion_tour.png). Nobody is named: the old man is a silhouette.
Reused: Asterion's layers (its parts, its chunks, its ring and its cloud), Lyse and her sky (Singularis's story), space.

    python3 tools/assetgen/environment/story_horloger.py [--storyboard balancier|tour FILE.png] [--frames DIR] [--check]

Deterministic (fixed seeds): `--check` builds everything twice and compares the bytes with each other and with the
files; `--frames DIR` writes a few key frames of each film (as the game draws them, without live particles)."""
import io
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
import story_horloger_art as P  # noqa: E402
from story_stage import K, amb, particles, sfx  # noqa: E402

S = A.S
REPO, ASSETS = S.REPO, S.ASSETS
W, H = S.W, S.H
PIX = S.PIX
Track, glo = A.Track, A.glo
TEX = "stories/horloger/"
ANIM = "animations/stories/horloger_"
STORIES = "animations/stories/"
FILMS = {"balancier": os.path.join(REPO, "data", "raids", "asterion_balancier.json"),
         "tour": os.path.join(REPO, "data", "raids", "asterion_tour.json")}
MONTAGE = ("monde_serre", "monde_glacier", "monde_forge", "monde_ruines", "monde_archives", "monde_cadran")
COMMON = MONTAGE + ("cadran", "regard", "noir")  # the same last shots in both films (and « atelier »)
GOLD, CYAN, BRASS = "#ffd27a", "#39f0d8", "#e2b24a"
BIOME_LIGHT = [b[4] for b in P.BIOMES]


# =================================================================== the sheets
def build_assets():
    Sheet, dumps = A.sheet_cls()
    files = {}

    def save(name, sh, extra=None):
        sh.texture = TEX + name + ".png"
        meta = sh.meta()
        for clip, m in (extra or {}).items():  # clips sharing a row (another speed, another start)
            meta["clips"][clip] = m
        files[sh.texture] = A.png_bytes(sh.image())
        files[ANIM + name + ".json"] = (dumps(meta) + "\n").encode("utf-8")

    def tex(name, arr):
        files[TEX + name + ".png"] = A.png_bytes(arr)

    for name, (R, teeth, spokes, seed) in {"engrenage_grand": (42, 24, 6, 2), "engrenage_moyen": (34, 20, 5, 4),
                                           "engrenage_petit": (18, 12, 4, 6)}.items():
        fr = P.gear_sheet(R, teeth, spokes, 16, seed=seed)
        sh = Sheet("", fr[0].shape[1], fr[0].shape[0], [fr[0].shape[1] // 2, fr[0].shape[0] // 2])
        sh.add("tourne", fr, fps=10, loop=True)
        save(name, sh, {"folle": {"row": 0, "count": 16, "fps": 60, "loop": True}, "lent": {"row": 0, "count": 16, "fps": 4, "loop": True},
                        "arret": {"row": 0, "count": 1, "fps": 1, "loop": True}})
    piv = [P.OX, P.OY]
    sh = Sheet("", P.CW, P.CH, piv)
    sh.add("tourne", [P.crown(k / 4.0) for k in range(4)], fps=8, loop=True)
    save("couronne", sh, {"folle": {"row": 0, "count": 4, "fps": 40, "loop": True}, "arret": {"row": 0, "count": 1, "fps": 1, "loop": True}})
    rng = np.random.default_rng(4242)
    flick = [[float(rng.choice([0.2, 0.28, 0.45, 0.6])) for _ in range(6)] for _ in range(4)]
    sh = Sheet("", P.CW, P.CH, piv)
    sh.add("eteint", [P.dome([0.2] * 6, 0, 0.1)], fps=1, loop=True)
    sh.add("vacille", [P.dome(f, k, 0.3 + 0.15 * k) for k, f in enumerate(flick)], durations=[0.12, 0.3, 0.08, 0.4], loop=True)
    sh.add("eveil", [P.dome([1.0 if i < k else 0.2 for i in range(6)], k, 0.2 + 0.13 * k) for k in range(1, 8)], durations=[0.32] * 7,
           loop=False, next="allume")
    sh.add("allume", [P.dome([1.0] * 6, k, 1.0) for k in range(4)], fps=3, loop=True)
    save("dome", sh)
    sh = Sheet("", P.CW, P.CH, piv)
    sh.add("eteint", [P.lights([0.0] * 6, 0)], fps=1, loop=True)
    sh.add("eveil", [P.lights([1.0 if i < k else 0.0 for i in range(6)], k) for k in range(1, 8)], durations=[0.32] * 7, loop=False, next="allume")
    sh.add("allume", [P.lights([1.0] * 6, k) for k in range(4)], fps=3, loop=True)
    save("lueurs", sh)
    slow = [round(0.035 * (1.12 ** i), 3) for i in range(24)]  # wild, then slowing down to a tick (about 4 s)
    for kind, name in (("m", "aiguille_m"), ("h", "aiguille_h")):
        fr = [P.hand(15 * k, kind) for k in range(24)]
        sh = Sheet("", P.HW, P.HH, [P.HPX, P.HPY])
        sh.add("avance", fr, fps=24, loop=True)
        sh.add("recule", list(reversed(fr)), fps=18, loop=True)
        if kind == "m":
            extra = {"folle": {"row": 0, "count": 24, "fps": 40, "loop": True},
                     "ralentit": {"row": 0, "count": 24, "durations": slow, "loop": False, "next": "tic"}}
        else:
            extra = {"folle": {"row": 1, "count": 24, "fps": 22, "loop": True},
                     "ralentit": {"row": 0, "start": 12, "count": 12, "durations": [round(d * 2.0, 3) for d in slow[:12]], "loop": False, "next": "tic"}}
        extra["tic"] = {"row": 0, "count": 24, "durations": [1.0 if kind == "m" else 4.0] * 24, "loop": True}
        save(name, sh, extra)
    sh = Sheet("", P.CW, P.CH, piv)
    sh.add("reflet", [P.glass(-150 + k * 24) for k in range(12)] + [P.glass(None)] * 8, fps=10, loop=True)
    save("verre", sh)
    sh = Sheet("", P.CW, P.CH, piv)
    sh.add("idle", [P.ribs()], fps=1, loop=True)
    save("nervures", sh)
    sh = Sheet("", P.RW, P.RH, [P.RPX, P.RPY])
    sh.add("entiere", [P.rod(False)], fps=1, loop=True)
    sh.add("brisee", [P.rod(True, k) for k in range(3)], fps=10, loop=True)
    save("tige", sh)
    sh = Sheet("", P.RW, P.RH, [P.RPX, 90])
    sh.add("idle", [P.rod_piece()], fps=1, loop=True)
    save("tige_morceau", sh)
    sh = Sheet("", P.AW, P.AH, [P.APX, P.APY])
    sh.add("balance", [P.chained(k) for k in range(4)], durations=[0.17, 0.17, 0.17, 0.5], loop=True)
    save("asterion_enchaine", sh)
    opening = [0.06, 0.16, 0.3, 0.46, 0.62, 0.78, 0.9, 1.0]
    closing = [0.85, 0.66, 0.46, 0.28, 0.12, 0.03, 0.0]
    for name, fn in (("dechirure", P.tear), ("dechirure_lueur", P.tear_glow)):
        sh = Sheet("", P.TW, P.TH, [P.TW // 2, P.TH // 2])
        sh.add("ouverture", [fn(o, k) for k, o in enumerate(opening)], durations=[0.11] * 8, loop=False, next="vibre")
        sh.add("vibre", [fn(1.0, 10 + k) for k in range(6)], fps=12, loop=True)
        sh.add("fermeture", [fn(o, 20 + k) for k, o in enumerate(closing)], durations=[0.08] * 7, loop=False)
        # a narrow, jagged slit (the defeat's tears): opening, shivering
        sh.add("faille_ouverture", [fn(o * 0.34, 30 + k) for k, o in enumerate(opening)], durations=[0.11] * 8, loop=False, next="faille")
        sh.add("faille", [fn(0.34, 40 + k) for k in range(6)], fps=12, loop=True)
        sh.add("faille_fermeture", [fn(o * 0.34, 50 + k) for k, o in enumerate(closing)], durations=[0.08] * 7, loop=False)
        save(name, sh)
    sh = Sheet("", 200, 16, [0, 8])
    sh.add("tisse", [P.thread((k + 1) / 10.0, k) for k in range(10)], durations=[0.06] * 10, loop=False, next="vibre")
    sh.add("vibre", [P.thread(1.0, k) for k in range(4)], fps=10, loop=True)
    save("fil", sh)
    c2 = [P.SW_ // 2, P.SH_ // 2]
    sh = Sheet("", P.SW_, P.SH_, c2)
    sh.add("ouverte", [P.wound(0.0, k) for k in range(3)], fps=8, loop=True)
    sh.add("referme", [P.wound(s, k) for k, s in enumerate((0.15, 0.3, 0.5, 0.7, 0.85, 0.95))], durations=[0.12] * 6, loop=False, next="cicatrice")
    sh.add("cicatrice", [P.wound(1.0, 0)], fps=1, loop=True)
    save("plaie", sh)
    sh = Sheet("", P.SW_, P.SH_, c2)
    sh.add("coud", [P.stitches(k) for k in range(7)], durations=[0.15] * 7, loop=False, next="cousu")
    sh.add("cousu", [P.stitches(6), P.stitches(7)], fps=3, loop=True)
    save("couture", sh)
    sh = Sheet("", P.OMW, P.OMH, [P.OMW // 2, P.OMH - 1])
    sh.add("immobile", [P.old_man(0), P.old_man(1)], durations=[1.2, 1.2], loop=True)
    save("vieil_homme", sh)
    sh = Sheet("", 32, 14, [16, 7])
    sh.add("allume", [P.eyes_small(0, lv) for lv in (0.15, 0.35, 0.6, 0.85)], durations=[0.1] * 4, loop=False, next="tournent")
    sh.add("tournent", [P.eyes_small(k) for k in range(6)], fps=10, loop=True)
    sh.add("lueur", [P.eyes_small(0, 0.35), P.eyes_small(0, 0.2)], durations=[0.5, 0.9], loop=True)
    save("yeux", sh)
    sh = Sheet("", P.GW_, P.GH_, [P.GW_ // 2, P.GH_ // 2])
    sh.add("clos", [P.gaze(0.06, 0)], fps=1, loop=True)
    sh.add("ouvre", [P.gaze(o, 0) for o in (0.12, 0.3, 0.5, 0.72, 0.9, 1.0)], durations=[0.12] * 6, loop=False, next="tournent")
    sh.add("tournent", [P.gaze(1.0, k) for k in range(8)], fps=10, loop=True)
    save("regard", sh)
    sh = Sheet("", 22, 22, [11, 11])
    sh.add("prend", [P.frost(k) for k in range(5)], durations=[0.09] * 5, loop=False, next="fige")
    sh.add("fige", [P.frost(5), P.frost(4)], durations=[0.15, 1.4], loop=True)
    save("givre", sh)
    sh = Sheet("", 16, 150, [8, 130])
    sh.add("idle", [P.ghost_needle()], fps=1, loop=True)
    save("aiguille_fantome", sh)
    sh = Sheet("", 24, 24, [12, 12])
    for i, kind in enumerate(("roche", "magma", "glace")):
        sh.add(kind, [A.frame_chunk(kind, size=24, seed=70 + i)], fps=1, loop=True)
    save("debris", sh)
    sh = Sheet("", 200, 150, [100, 75])
    sh.add("fuit", [P.titan_flee(k) for k in range(4)], durations=[0.12] * 4, loop=True)
    save("asterion_fuite", sh)
    curl = [0.0, 0.12, 0.24, 0.36, 0.48, 0.6, 0.72, 0.84, 0.94, 1.0]
    sides = [P.side_hand(c) for c in curl]
    for i, name in enumerate(("main_cote_paume", "main_cote_doigts")):
        sh = Sheet("", P.SHW, P.SHH, [P.SHX, P.SHY])
        sh.add("ouverte", [sides[0][i]], fps=1, loop=True)
        sh.add("ferme", [x[i] for x in sides], durations=[0.13] * len(curl), loop=False, next="serre")
        sh.add("serre", [sides[-1][i]], fps=1, loop=True)
        save(name, sh)
    a2 = P.side_arm()
    sh = Sheet("", a2.shape[1], a2.shape[0], [a2.shape[1] - 4, 62])
    sh.add("idle", [a2], fps=1, loop=True)
    save("bras_cote", sh)
    sh = Sheet("", 28, 9, [14, 4])
    sh.add("brille", [P.light_needle(k) for k in range(2)], fps=12, loop=True)
    save("aiguille_lumiere", sh)
    sh = Sheet("", P.PMW, P.PMH, [P.PMW // 2, P.PMH - 1])
    sh.add("immobile", [P.old_man_profile(0), P.old_man_profile(1)], durations=[1.2, 1.2], loop=True)
    sh.add("tend", [P.old_man_profile(0, u) for u in (0.0, 0.25, 0.5, 0.75, 1.0)], durations=[0.16] * 5, loop=False, next="tendu")
    sh.add("tendu", [P.old_man_profile(0, 1.0), P.old_man_profile(1, 1.0)], durations=[1.2, 1.2], loop=True)
    save("vieil_homme_profil", sh)
    sh = Sheet("", 72, 72, [36, 36])
    sh.add("frisson", [P.lyse_dead(k) for k in range(4)], durations=[0.4, 0.3, 0.5, 0.3], loop=True)
    save("lyse", sh)
    tex("filantes", P.streaks())
    tex("horloges", P.clock_wall())
    tex("etabli", P.bench())
    for b in P.BIOMES:
        tex("monde_" + b[0], P.world_backdrop(b[0]))
    big = P.clock_face(26, seed=3)
    gh = np.zeros((112, 60, 4), np.uint8)
    gh[2:2 + big.shape[0], 30 - big.shape[1] // 2:30 - big.shape[1] // 2 + big.shape[1]] = big
    stem = P.line_mask(60, 112, [(30, 56), (30, 96)], 2)
    gh[stem] = P.C(P.BRASS[4]).astype(np.uint8)
    bob = P.ell_mask(60, 112, 30, 100, 7, 7)
    gh[bob] = P.C(P.BRASS[5]).astype(np.uint8)
    gh[P.ell_mask(60, 112, 30, 100, 3, 3)] = P.C(P.CYAN[2]).astype(np.uint8)
    sh = Sheet("", 60, 112, [30, 30])
    sh.add("idle", [gh], fps=1, loop=True)
    save("grande_horloge", sh)
    sh = Sheet("", 180, 120, [90, 60])
    sh.add("trace", [P.blueprint(0), P.blueprint(1)], durations=[0.9, 0.3], loop=True)
    save("plan", sh)
    sh = Sheet("", 24, 24, [12, 12])
    for kind in ("rouage", "vis", "plaque", "ressort"):
        sh.add(kind, [P.brass_bits(kind)], fps=1, loop=True)
    save("laiton", sh)
    sh = Sheet("", P.GUARD_W, P.GUARD_H, [P.GUARD_W // 2, P.GUARD_H - 1])
    for b in P.BIOMES:
        sh.add(b[0], [P.guardian(b[0], 0), P.guardian(b[0], 1)], durations=[1.6, 0.2], loop=True)
    save("gardien", sh)
    sh = Sheet("", 140, 180, [70, 179])
    sh.add("pousse", [P.vines(k) for k in range(8)], durations=[0.09] * 8, loop=True)
    save("lianes", sh)
    sh = Sheet("", 20, 20, [10, 10])
    sh.add("idle", [P.snowflake(0), P.snowflake(1)], durations=[0.8, 0.2], loop=True)
    save("flocon", sh)
    sh = Sheet("", 120, 100, [60, 99])
    sh.add("frappe", [P.anvil(k) for k in range(6)], durations=[0.18, 0.1, 0.06, 0.12, 0.2, 0.06], loop=True)
    save("enclume", sh)
    sh = Sheet("", 22, 18, [11, 9])
    sh.add("idle", [P.page(0), P.page(1)], durations=[1.2, 0.2], loop=True)
    save("page", sh)
    tex("cadran_fantome", P.ghost_dial())
    tex("etoiles", P.near_stars())
    tex("voile", P.veil())
    tex("vague", P.wave())
    tex("noir", P.black())
    return files


def anim(name):
    return ANIM + name + ".json"


def spr(id_, name, track, depth=1.0, add=False, tint=None, pulse=None, hold=None):
    e = A.sprite(id_, name if name.endswith(".json") else anim(name), track, depth=depth, tint=tint, add=add, pulse=pulse)
    if hold is not None:
        e["hold"] = [round(float(v), 3) for v in hold]
    return e


def lay(id_, name, depth, track, add=False, drift=None, repeat=False, tint=None, hold=None):
    e = {"id": id_, "layer": name if name.endswith(".png") else TEX + name + ".png", "depth": depth}
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    if repeat:
        e["repeat"] = True
    if tint:
        e["tint"] = tint
    if add:
        e["blend"] = "add"
    if hold is not None:
        e["hold"] = [round(float(v), 3) for v in hold]
    e["keys"] = track.keys() if isinstance(track, Track) else track
    return e


# =================================================================== pieces
class Ship:
    """Where the Échappement hangs through a shot: (t, centre of its dial, scale, alpha, ease) keys; its parts follow,
    each at its offset from that centre (pixels of its sheets)."""

    def __init__(self, O, s, alpha=1.0):
        self.place = [(0.0, (float(O[0]), float(O[1])), float(s), float(alpha), "linear")]

    def move(self, t, O=None, s=None, alpha=None, ease="inout"):
        _, O0, s0, a0, _ = self.place[-1]
        self.place.append((float(t), O0 if O is None else (float(O[0]), float(O[1])), s0 if s is None else float(s), a0 if alpha is None else float(alpha), ease))
        return self

    def track(self, off=(0.0, 0.0), alpha=1.0):
        t0, O, s, a, _ = self.place[0]
        tr = Track(pos=(O[0] + off[0] * PIX * s, O[1] + off[1] * PIX * s), scale=s, alpha=a * alpha)
        for t, O, s, a, ease in self.place[1:]:
            tr.to(t, ease, pos=(O[0] + off[0] * PIX * s, O[1] + off[1] * PIX * s), scale=s, alpha=a * alpha)
        return tr

    def at(self, t, off=(0.0, 0.0)):
        tr = self.track(off)
        return tr.value("pos", t), tr.value("scale", t)


GEARS = [("engrenage_moyen", (-72, -58)), ("engrenage_grand", (64, -64)), ("engrenage_petit", (-6, -86))]
ROD_OFF = (0, P.RY + P.BAND - 4)


def clips(tr, seq):
    for item in seq:
        t, name = item[0], item[1]
        tr.clip(t, name, item[2] if len(item) > 2 else 0.0)
    return tr


def ship_parts(ship, prefix="ech_", gears=((0, "tourne"),), crown=((0, "tourne"),), dome=((0, "allume"),), lights=((0, "allume"),),
               hm=((0, "tic"),), hh=((0, "tic"),), rod=None, rod_rot=None, depth=1.0, glass=True):
    """The watch's parts in their drawing order: the gears of its back, its rod (`rod`: « entiere », « brisee »,
    « enchaine »; None: out of the picture), the crown, the dome, the lights over it (a little nearer: they float), the
    hands, the ribs, the glass. Each takes its clips: (t, clip[, offset]) pairs."""
    els = []
    for i, (name, off) in enumerate(GEARS):
        els.append(spr(f"{prefix}{name}", name, clips(ship.track(off), [(t, c, 0.37 * i) for t, c in gears]), depth=depth))
    if rod:
        tr = ship.track(ROD_OFF)
        if rod_rot:
            for t, r, e in rod_rot:
                tr.to(t, e, rot=r)
        if rod == "enchaine":
            els.append(spr(prefix + "asterion", "asterion_enchaine", clips(tr, [(0, "balance")]), depth=depth))
        else:
            els.append(spr(prefix + "tige", "tige", clips(tr, [(0, rod)]), depth=depth))
    els.append(spr(prefix + "couronne", "couronne", clips(ship.track(), crown), depth=depth))
    els.append(spr(prefix + "dome", "dome", clips(ship.track(), dome), depth=depth))
    els.append(spr(prefix + "lueurs", "lueurs", clips(ship.track(), lights), depth=depth * 1.03, add=True))
    els.append(spr(prefix + "aiguille_h", "aiguille_h", clips(ship.track(), hh), depth=depth))
    els.append(spr(prefix + "aiguille_m", "aiguille_m", clips(ship.track(), hm), depth=depth))
    els.append(spr(prefix + "nervures", "nervures", ship.track(), depth=depth))
    if glass:
        els.append(spr(prefix + "verre", "verre", clips(ship.track(), [(0, "reflet")]), depth=depth, add=True))
    return els


def sky(D, drift=4.0, nebula=0.45, hold=None):
    """Space: the far layers of Asterion's films, near stars that slide faster (parallax), a veil of cold light."""
    els = A.space(D, drift=drift, nebula=nebula)
    if hold is not None:
        for e in els:
            e["hold"] = [round(float(hold), 3)]
    els.append(A.stars(0.8))
    if hold is not None:
        els[-1]["hold"] = [round(float(hold), 3)]
    els.append(lay("etoiles_proches", "etoiles", 0.35, [K(0, pos=(-60, -40), scale=1.15)], drift=(-drift * 2.2, 0), repeat=True, hold=None if hold is None else (hold,)))
    els.append(lay("voile", "voile", 0.2, [K(0, pos=(-120, -40), scale=2.2, alpha=0.55)], add=True, drift=(-drift * 0.8, 0), hold=None if hold is None else (hold,)))
    return els


def lyse_sky(O=(210, 150), scale=1.0, alpha=1.0):
    """Lyse — a dim dead star, a spark quivering on her, as « Après Asterion » left her — and her sky (Singularis's story)."""
    els = [
        {"id": "cosmos", "sprite": STORIES + "singularis_cosmos.json", "depth": 0.03, "keys": [K(0, pos=(-80, -30), clip="idle", alpha=0.6 * alpha)]},
        {"id": "etoiles_lyse", "sprite": STORIES + "singularis_etoiles.json", "depth": 0.08, "drift": [3.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-80, -30), clip="idle")]},
        {"id": "voile_lyse", "sprite": STORIES + "singularis_voile.json", "depth": 0.15, "drift": [-6.0, 0.0], "repeat": True,
         "keys": [K(0, pos=(-200, 60), alpha=0.45, clip="idle", offset=0.3)]},
    ]
    els.append(glo("lyse_halo", "#ff9a6a", 150 * scale, Track(pos=O, alpha=0.22 * alpha), depth=0.5, pulse=(0.3, 1.6)))
    els.append(spr("lyse", "lyse", Track(pos=O, scale=scale * 1.3, alpha=0.85 * alpha).clip(0, "frisson"), depth=0.5))
    return els


def chunk(id_, p0, p1, t1, rot0, rot1, scale, kind, depth=1.0, hold=None, tint=None, big=False):
    tr = Track(pos=p0, scale=scale, rot=rot0).to(t1, "linear", pos=p1, rot=rot1).clip(0, kind)
    return spr(id_, "debris" if big else STORIES + "asterion_eclat.json", tr, depth=depth, hold=hold, tint=tint)


def flight(rng, n, O, t1, r0, r1, depth=(0.7, 1.25), prefix="eclat", hold_at=None, tint=None, frozen=None):
    """`n` chunks of Asterion flying out from O: at r0 at the shot's start, r1 at t1; held (frozen) at `hold_at(x)`
    when given (its time), `frozen` collects (id, position at the hold, scale, depth) for the frost."""
    els = []
    for i in range(n):
        a = float(rng.uniform(0, A.TAU))
        d0, d1 = r0 * float(rng.uniform(0.5, 1.2)), r1 * float(rng.uniform(0.7, 1.3))
        p0 = (O[0] + math.cos(a) * d0, O[1] + math.sin(a) * d0 * 0.75)
        p1 = (O[0] + math.cos(a) * d1, O[1] + math.sin(a) * d1 * 0.75)
        big = i % 3 == 0
        sc = float(rng.uniform(0.9, 1.6)) if big else float(rng.uniform(1.3, 2.4))
        dp = float(rng.uniform(*depth))
        rot0 = float(rng.uniform(0, 360))
        rot1 = rot0 + float(rng.uniform(-240, 240))
        kind = ("roche", "magma", "glace")[i % 3]
        hold = None
        if hold_at:
            th = hold_at(p0, p1, i)
            hold = (th,)
            if frozen is not None:
                u = th / t1
                frozen.append((f"{prefix}{i + 1}", (p0[0] + (p1[0] - p0[0]) * u, p0[1] + (p1[1] - p0[1]) * u), sc * (2.0 if big else 1.0), dp, th))
        els.append(chunk(f"{prefix}{i + 1}", p0, p1, t1, rot0, rot1, sc, kind, dp, hold, tint, big))
    return els


def frost_on(frozen, start=0.0):
    els = []
    for id_, p, sc, dp, th in frozen:
        tr = Track(pos=p, scale=min(1.3, sc * 0.55), alpha=0.0).to(max(0.0, th - start), alpha=0.0).to(max(0.0, th - start) + 0.05, alpha=0.8).clip(0, "prend").clip(
            max(0.0, th - start), "prend")
        els.append(spr(id_ + "_givre", "givre", tr, depth=dp, add=True))
    return els


def black(t0, t1, a0=0.0, a1=1.0, id_="noir"):
    tr = Track(pos=(-40, -40), scale=66, alpha=a0).to(t0, alpha=a0).to(t1, "linear", alpha=a1)
    return lay(id_, "noir", 0.0, tr)


def flash(t, color="#ffffff", alpha=0.6, i=0.08, o=0.6):
    return A.flash(t, color, alpha, i, o)


# =================================================================== Le Balancier (after a victory)
def scene_onde(D, shot):
    """The supernova fades: its glow shrinks, its ring goes out, Asterion's chunks still fly out of it."""
    O = (480, 270)
    els = sky(D, drift=3.0, nebula=0.5)
    els.append(glo("nova", "#fff2c0", 520, Track(pos=O, scale=1.0, alpha=1.0).to(D, "out", scale=0.45, alpha=0.25), depth=0.4))
    els.append(glo("nova_or", GOLD, 300, Track(pos=O, alpha=0.9).to(D, "out", alpha=0.2), depth=0.45, pulse=(0.2, 1.4)))
    half = 128 * PIX
    ring = Track(pos=(O[0] - half * 2.0, O[1] - half * 2.0), scale=2.0, alpha=0.7).to(D, "out", pos=(O[0] - half * 4.6, O[1] - half * 4.6), scale=4.6, alpha=0.0)
    els.append({"id": "onde", "layer": A.TEX + "onde.png", "depth": 0.55, "blend": "add", "tint": GOLD, "keys": ring.keys()})
    cl = Track(pos=(O[0] - 160 * PIX * 1.3, O[1] - 90 * PIX * 1.3), scale=1.3, alpha=0.8).to(D, pos=(O[0] - 160 * PIX * 1.7, O[1] - 90 * PIX * 1.7), scale=1.7, alpha=0.35)
    els.append({"id": "nuage", "layer": A.TEX + "nuage_or.png", "depth": 0.5, "blend": "add", "keys": cl.keys()})
    els += flight(np.random.default_rng(8201), 30, O, D, 30, 520)
    els.append(particles("braises", "embers", GOLD, (180, 100, 600, 340), [K(0, alpha=0.9), K(D, alpha=0.3)], depth=0.8))
    els.append(particles("etincelles", "sparks", "#fff2c0", (100, 60, 760, 420), [K(0, alpha=0.8), K(D, alpha=0.2)], depth=0.9))
    cam = A.camera((0, O, 1.35, "linear"), (D, O, 1.0, "out"))
    return dict(backdrop="#030308", camera=cam, elements=els, flash=[flash(0.05, "#fff2c0", 0.5, 0.02, 1.6)], still=D * 0.35)


TICKS = [0.6, 1.6, 2.6, 3.6, 4.35, 4.55, 5.7, 5.82, 6.95, 7.45]  # regular, then wrong


def scene_tic(D, shot):
    """A ticking out of the silence: a dial of light pulses with each tick, its needle jumps — then the beat goes wrong."""
    O = (480, 270)
    els = sky(D, drift=2.0, nebula=0.4)
    els += flight(np.random.default_rng(8202), 22, O, D, 260, 470, depth=(0.6, 1.2))
    dial = Track(pos=(O[0] - 160 * PIX * 1.45, O[1] - 160 * PIX * 1.45), scale=1.45, alpha=0.0)
    needle = Track(pos=O, scale=2.0, alpha=0.0).to(0.5, alpha=0.0).to(0.6, alpha=0.8)
    ang = 0.0
    for i, t in enumerate(TICKS):
        wrong = i >= 4
        dial.to(t - 0.02, alpha=0.1 if i else 0.0).to(t, "out", alpha=0.42 if not wrong else 0.6).to(t + 0.35, "in", alpha=0.12)
        ang += 6.0 if not wrong else (-12.0 if i % 2 else 21.0)
        needle.to(t - 0.01, "step", rot=needle.value("rot", t - 0.02)).to(t, "out", rot=ang)
    els.append(lay("cadran", "cadran_fantome", 0.6, dial, add=True))
    els.append(spr("aiguille", "aiguille_fantome", needle, depth=0.6, add=True))
    els.append(glo("coeur_cadran", CYAN, 60, Track(pos=O, alpha=0.0).to(0.6, alpha=0.5), depth=0.6, pulse=(0.5, 1.0)))
    els.append(particles("poussiere", "dust", "#bff6ff", (80, 60, 800, 420), [K(0, alpha=0.4)], depth=0.7))
    shakes = [A.shake(t, t + 0.25, 4.0 if i % 2 else 2.5) for i, t in enumerate(TICKS) if i >= 4]
    fl = [flash(t, CYAN, 0.18, 0.02, 0.25) for i, t in enumerate(TICKS) if i >= 4]
    cam = A.camera((0, O, 1.0, "linear"), (D, O, 1.12, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=shakes, flash=fl, still=4.4)


T_FREEZE = 2.4


def scene_fige(D, shot):
    """Asterion's debris freezes in mid-flight: a wave of frozen time crosses the picture and everything it passes
    stops — the chunks, the sparks, the dust, the far sky — while the camera goes on moving among them."""
    O = (480, 270)
    els = sky(D, drift=3.0, nebula=0.45, hold=T_FREEZE + 0.3)
    frozen = []

    def hold_at(p0, p1, i):  # the wave reaches it, left to right
        x = p0[0] + (p1[0] - p0[0]) * (T_FREEZE / D)
        return round(T_FREEZE - 0.35 + 0.7 * min(1.0, max(0.0, x / 960.0)), 3)

    els += flight(np.random.default_rng(8203), 34, O, D, 140, 520, depth=(0.6, 1.35), hold_at=hold_at, frozen=frozen)
    els += frost_on(frozen)
    for i, (id_, area, kind, col) in enumerate((("braises", (100, 80, 760, 400), "embers", GOLD), ("etincelles", (60, 40, 840, 460), "sparks", "#fff2c0"),
                                               ("poussiere", (60, 60, 840, 420), "dust", "#ffe6b0"))):
        p = particles(id_, kind, col, area, [K(0, alpha=0.8), K(T_FREEZE, alpha=0.8), K(T_FREEZE + 1.0, alpha=0.55)], depth=0.8 + 0.1 * i)
        p["hold"] = [round(T_FREEZE - 0.1 + 0.3 * i, 3)]
        els.append(p)
    wv = Track(pos=(-160, -20), scale=2.2, alpha=0.0).to(T_FREEZE - 0.45, alpha=0.0).to(T_FREEZE - 0.35, alpha=1.0).to(T_FREEZE + 0.4, "linear", pos=(1060, -20)).to(
        T_FREEZE + 0.55, alpha=0.0)
    els.append(lay("vague", "vague", 0.9, wv, add=True))
    els.append(glo("froid", "#bff6ff", 600, Track(pos=O, alpha=0.0).to(T_FREEZE, alpha=0.0).to(T_FREEZE + 0.4, alpha=0.18).to(D, alpha=0.1), depth=0.3))
    cam = A.camera((0, (430, 280), 1.05, "linear"), (D, (560, 250), 1.35, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, flash=[flash(T_FREEZE, "#bff6ff", 0.35, 0.05, 0.9)], still=D * 0.75)


T_TEAR, T_GONG = 1.0, 2.6


def tear_els(O, scale, t_open, t_close, D, slit=False):
    """The tear in time at O: its depths (first list) and its light (second); `slit`: a narrow jagged one."""
    pre = "faille_" if slit else ""
    if t_open <= 0:  # already open
        tr = Track(pos=O, scale=scale).clip(0, "faille" if slit else "vibre")
        hid = Track(pos=O, scale=scale)
    else:
        tr = Track(pos=O, scale=scale).clip(0, pre + "ouverture").clip(t_open, pre + "ouverture")
        hid = Track(pos=O, scale=scale, alpha=0.0).to(t_open - 0.01, alpha=0.0).to(t_open, alpha=1.0)
    if t_close < D:
        tr.clip(t_close, pre + "fermeture")
    k = tr.keys()
    hk = hid.keys()
    a = spr("dechirure", "dechirure", tr)
    a["keys"] = merge_keys(hk, k)
    b = spr("dechirure_lueur", "dechirure_lueur", tr, add=True)
    b["keys"] = merge_keys(hk, k)
    gt = Track(pos=O, alpha=0.6) if t_open <= 0 else Track(pos=O, alpha=0.0).to(t_open, alpha=0.0).to(t_open + 0.6, alpha=0.6)
    if t_close < D:
        gt.to(t_close, alpha=0.5).to(min(D, t_close + 0.6), alpha=0.0)
    g = glo("dechirure_halo", CYAN, (150 if slit else 260) * scale, gt, depth=1.0, pulse=(0.25, 0.4))
    return [a], [b, g]


def merge_keys(a, b):
    """Keys of two tracks of one element (an alpha and the clips): every time of both, each with what it sets."""
    out = {}
    for k in a + b:
        d = out.setdefault(k["t"], {"t": k["t"]})
        d.update({x: y for x, y in k.items() if x != "t"})
    return [out[t] for t in sorted(out)]


def scene_dechirure(D, shot):
    """Near Lyse, a tear in time opens; the watch comes out of it, growing out of its depths, hands running wild,
    its balance rod snapped — the broken piece tumbling away; the tear closes behind it."""
    T = (560, 290)
    els = lyse_sky((190, 140), 0.9)
    els.append(lay("etoiles_proches", "etoiles", 0.35, [K(0, pos=(-60, -40), scale=1.15)], drift=(-2.0, 0), repeat=True))
    rng = np.random.default_rng(8204)
    for i in range(8):  # frozen debris in front, held since the last shot
        p = (float(rng.uniform(40, 920)), float(rng.choice([float(rng.uniform(30, 110)), float(rng.uniform(430, 510))])))
        els.append(chunk(f"fige{i + 1}", p, p, D, float(rng.uniform(0, 360)), float(rng.uniform(0, 360)), float(rng.uniform(1.2, 2.0)),
                         ("roche", "magma", "glace")[i % 3], depth=1.3, hold=(0.0,), big=True, tint="#bcd8e8"))
        els.append(spr(f"fige{i + 1}_givre", "givre", Track(pos=p, scale=2.0).clip(0, "fige", 0.3 * i), depth=1.3, add=True))
    t_close = D - 1.6
    back, front = tear_els(T, 1.35, T_TEAR, t_close, D)
    els += back
    ship = Ship(T, 0.1, 0.0).move(T_GONG, alpha=0.0).move(T_GONG + 0.5, s=0.22, alpha=1.0, ease="out").move(D - 2.2, O=(520, 262), s=0.92, ease="out")
    piece = Track(pos=(T[0], T[1] + 60), scale=0.2, alpha=0.0, rot=0).to(T_GONG + 1.6, alpha=0.0)
    p0, s0 = ship.at(T_GONG + 1.8, ROD_OFF)
    piece = Track(pos=(p0[0], p0[1] + 46 * PIX * s0), scale=s0, alpha=0.0).to(T_GONG + 1.79, alpha=0.0).to(T_GONG + 1.8, alpha=1.0).to(
        D, "out", pos=(p0[0] + 160, p0[1] + 330), rot=150, scale=0.9)
    els.append(spr("tige_morceau", "tige_morceau", piece))
    els += ship_parts(ship, gears=((0, "folle"),), crown=((0, "folle"),), dome=((0, "vacille"),), lights=((0, "eteint"),),
                      hm=((0, "folle"),), hh=((0, "folle"),), rod="brisee")
    els += front
    els.append(particles("eclairs", "sparks", "#9ff6ff", (380, 60, 380, 460), [K(0, alpha=0.0), K(T_TEAR + 0.3, alpha=0.9), K(t_close, alpha=0.6), K(D, alpha=0.0)]))
    els.append(particles("rouille", "embers", BRASS, (380, 200, 300, 220), [K(0, alpha=0.0), K(T_GONG + 1.8, alpha=0.0), K(T_GONG + 2.2, alpha=0.7)]))
    cam = A.camera((0, (470, 270), 1.0, "linear"), (T_GONG, (520, 280), 1.04, "inout"), (D, (520, 270), 1.12, "inout"))
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=[A.shake(T_GONG, T_GONG + 0.9, 7.0)],
                flash=[flash(T_GONG, "#bff6ff", 0.55, 0.05, 0.9)], still=D - 2.6)


def interior(D, alpha=1.0, wedge_glow=True, head=(480, 240)):
    """Behind the old man: the inside of the watch — the white-gold light of the Cadran Central behind his head (he
    stands out against it), its six worlds lit under the dome below and their lights."""
    els = [glo("salle", "#fff2c0", 640, Track(pos=head, alpha=0.95 * alpha), depth=0.2, pulse=(0.08, 2.4)),
           glo("salle_coeur", "#ffffff", 260, Track(pos=head, alpha=0.55 * alpha), depth=0.2)]
    big = Track(pos=(480, 640), scale=2.1, alpha=0.9 * alpha).clip(0, "allume")
    els.append(spr("cadran_interieur", "dome", big, depth=0.35))
    els.append(spr("lueurs_interieur", "lueurs", Track(pos=(480, 640), scale=2.1, alpha=alpha).clip(0, "allume"), depth=0.42, add=True))
    if wedge_glow:
        for k, c in enumerate(BIOME_LIGHT):
            a = math.radians(k * 60 - 60)
            p = (480 + math.cos(a) * 330, 600 + math.sin(a) * 140)
            els.append(glo(f"monde{k + 1}", c, 200, Track(pos=p, alpha=0.4 * alpha), depth=0.4, pulse=(0.25, 2.0 + 0.3 * k)))
    return els


def glass_front(D, scale=3.6, O=(480, 330), alpha=0.5):
    """The dome's glass between us and him: its highlight and its sheen sliding across."""
    return [spr("vitre", "verre", Track(pos=O, scale=scale, alpha=alpha).clip(0, "reflet", 0.4), depth=1.15, add=True)]


def scene_vieil_homme(D, shot):
    """Through the glass of the dome: an old man, a silhouette against the light of his six worlds, looks at the debris
    frozen outside; his hand rises to the glass."""
    els = [lay("fond", "voile", 0.05, [K(0, pos=(-80, -40), scale=3.4, alpha=0.4)], add=True)]
    els += interior(D, head=(560, 230))
    MS = 1.36
    MP = (560, 560)
    man = Track(pos=MP, scale=MS, flip=True).clip(0, "immobile").clip(2.6, "tend")
    els.append(spr("vieil_homme", "vieil_homme_profil", man, depth=0.85))
    eye = (MP[0] - (P.PM_EYE[0] - P.PMW // 2) * PIX * MS, MP[1] + (P.PM_EYE[1] - (P.PMH - 1)) * PIX * MS)
    els.append(glo("oeil", GOLD, 16, Track(pos=eye, alpha=0.0).to(5.6, alpha=0.0).to(6.4, alpha=0.9), depth=0.85, pulse=(0.4, 1.0)))
    els += glass_front(D)
    rng = np.random.default_rng(8205)
    for i in range(7):  # the frozen debris he is looking at, between us and him
        x = float(rng.uniform(30, 930))
        y = float(rng.choice([float(rng.uniform(20, 140)), float(rng.uniform(380, 520))])) if i % 3 else float(rng.uniform(150, 380))
        x = x if not (300 < x < 660) or i % 3 else (x - 300 if x < 480 else x + 300)
        els.append(chunk(f"devant{i + 1}", (x, y), (x, y), D, float(rng.uniform(0, 360)), float(rng.uniform(0, 360)), float(rng.uniform(1.8, 3.0)),
                         ("roche", "magma", "glace")[i % 3], depth=1.5, hold=(0.0,), big=True, tint="#7a8ea0"))
        els.append(spr(f"devant{i + 1}_givre", "givre", Track(pos=(x, y), scale=2.6).clip(0, "fige", 0.4 * i), depth=1.5, add=True))
    els.append(glo("contact", CYAN, 60, Track(pos=(MP[0] - (144 - P.PMW // 2) * PIX * MS, MP[1] + (56 - (P.PMH - 1)) * PIX * MS), alpha=0.0).to(3.4, alpha=0.0).to(3.6, alpha=0.9).to(
        D, alpha=0.5), depth=0.85, pulse=(0.3, 0.9)))
    els.append(particles("poussiere", "dust", "#fff2c0", (100, 60, 760, 420), [K(0, alpha=0.4)], depth=0.9))
    cam = A.camera((0, (480, 290), 1.0, "linear"), (D, (470, 260), 1.12, "inout"))
    return dict(backdrop="#06060c", camera=cam, elements=els, still=D * 0.62)


def scene_remonter(D, shot):
    """Closer: his eyes light up and start turning like gears."""
    sc = 2.2
    O = (480, 700)
    els = interior(D, 0.9, wedge_glow=False, head=(480, 170))
    els.append(spr("vieil_homme", "vieil_homme", Track(pos=O, scale=sc).clip(0, "immobile"), depth=0.9))
    ex = O[0] + ((P.EYE_L[0] + P.EYE_R[0]) / 2 - P.OMW // 2) * PIX * sc
    ey = O[1] + (P.EYE_L[1] - (P.OMH - 1)) * PIX * sc
    t_on = 1.1
    els.append(spr("yeux", "yeux", Track(pos=(ex, ey), scale=sc).clip(0, "lueur").clip(t_on, "allume"), depth=0.9, add=True))
    for side, e in ((-1, P.EYE_L), (1, P.EYE_R)):
        p = (O[0] + (e[0] - P.OMW // 2) * PIX * sc, O[1] + (e[1] - (P.OMH - 1)) * PIX * sc)
        els.append(glo(f"oeil{side}", GOLD, 60, Track(pos=p, alpha=0.0).to(t_on, alpha=0.0).to(t_on + 0.4, alpha=0.9), depth=0.9, pulse=(0.3, 1.0)))
        els.append(glo(f"oeil{side}_froid", CYAN, 26, Track(pos=p, alpha=0.0).to(t_on + 0.2, alpha=0.0).to(t_on + 0.6, alpha=0.8), depth=0.9))
    els += glass_front(D, 4.4, (480, 300), 0.35)
    els.append(particles("poussiere", "dust", GOLD, (180, 60, 600, 380), [K(0, alpha=0.3), K(t_on + 0.5, alpha=0.6)], depth=0.95))
    cam = A.camera((0, (480, 250), 1.0, "linear"), (D, (480, 220), 1.18, "inout"))
    return dict(backdrop="#06060c", camera=cam, elements=els, flash=[flash(t_on + 0.1, GOLD, 0.18, 0.05, 0.5)], still=t_on + 2.0)


# =================================================================== the three last shots (both films)
def scene_cadran(D, shot):
    """The watch hangs near Lyse; under its dome its six worlds light up one after another, like the hours of a dial;
    its hands slow down from their wild run and start ticking again. (Its rod is under the picture: the same shot after a
    victory and after a defeat.)"""
    els = lyse_sky((190, 150), 0.85)
    els.append(lay("voile", "voile", 0.15, [K(0, pos=(-120, -40), scale=2.2, alpha=0.4)], add=True, drift=(-2.0, 0)))
    els.append(lay("etoiles_proches", "etoiles", 0.3, [K(0, pos=(-60, -40), scale=1.15)], drift=(-1.5, 0), repeat=True))
    O = (540, 392)
    s = 1.26
    ship = Ship(O, s)
    t_wake = 1.2
    els += ship_parts(ship, gears=((0, "lent"), (4.0, "tourne")), crown=((0, "arret"), (4.0, "tourne")), dome=((0, "eteint"), (t_wake, "eveil")),
                      lights=((0, "eteint"), (t_wake, "eveil")), hm=((0, "ralentit"),), hh=((0, "ralentit"),))
    for k, c in enumerate(BIOME_LIGHT):  # each world's light over its wedge, when it wakes
        a = math.radians(k * 60 - 60)
        p = (O[0] + math.cos(a) * 0.6 * P.RXI * PIX * s, O[1] + math.sin(a) * 0.6 * P.RYI * PIX * s - 14 * s)
        t = t_wake + 0.32 * (k + 1)
        els.append(glo(f"monde{k + 1}", c, 120, Track(pos=p, alpha=0.0).to(t - 0.05, alpha=0.0).to(t + 0.3, alpha=0.55, ease="out").to(D, alpha=0.4),
                       depth=1.08, pulse=(0.2, 2.2 + 0.2 * k)))
    t_all = t_wake + 0.32 * 7
    els.append(glo("salle", CYAN, 70, Track(pos=(O[0], O[1] - 4), alpha=0.2).to(t_all, alpha=0.2).to(t_all + 0.4, alpha=0.9), depth=1.0, pulse=(0.45, 1.0)))
    els.append(glo("dome_lueur", "#fff2c0", 420, Track(pos=(O[0], O[1] - 40), alpha=0.0).to(t_wake, alpha=0.0).to(t_all, alpha=0.3), depth=0.95))
    els.append(particles("poussiere", "dust", "#fff2c0", (200, 120, 680, 360), [K(0, alpha=0.2), K(t_all, alpha=0.5)], depth=1.1))
    cam = A.camera((0, (480, 270), 1.0, "linear"), (D, (505, 300), 1.1, "inout"))
    return dict(backdrop="#04030a", camera=cam, elements=els, flash=[flash(t_all, "#fff2c0", 0.22, 0.1, 1.0)], still=D - 1.0)


def scene_regard(D, shot):
    """The gear-eyes, close: they turn, the six worlds shining in the monocle; then the dark comes."""
    O = (480, 270)
    els = [glo("ombre", "#1a1630", 600, Track(pos=O, alpha=0.6), depth=0.2)]
    els.append(spr("regard", "regard", Track(pos=O, scale=1.9).clip(0, "tournent"), depth=1.0))
    for side, e in ((-1, P.GEL), (1, P.GER)):
        p = (O[0] + (e[0] - P.GW_ // 2) * PIX * 1.9, O[1] + (e[1] - P.GH_ // 2) * PIX * 1.9)
        els.append(glo(f"oeil{side}", GOLD, 110, Track(pos=p, alpha=0.55), depth=1.0, pulse=(0.35, 1.0, 0.5 * side)))
        els.append(glo(f"oeil{side}_froid", CYAN, 45, Track(pos=p, alpha=0.6), depth=1.0, pulse=(0.3, 0.5)))
    els.append(particles("poussiere", "dust", GOLD, (120, 60, 720, 420), [K(0, alpha=0.4)], depth=1.1))
    els.append(black(D - 1.4, D - 0.05))
    pr = (O[0] + (P.GER[0] - P.GW_ // 2) * PIX * 1.9, O[1] + (P.GER[1] - P.GH_ // 2) * PIX * 1.9)
    cam = A.camera((0, O, 1.0, "linear"), (D, pr, 1.5, "in"))
    return dict(backdrop="#020204", camera=cam, elements=els, still=2.0)


NOIR_TICKS = []
_t, _gap = 0.25, 0.56
while _t < 3.95:
    NOIR_TICKS.append(round(_t, 3))
    _t += _gap
    _gap = max(0.055, _gap * 0.84)
T_STOP = NOIR_TICKS[-1]


def scene_noir(D, shot):
    """Black. The ticking speeds up, faster, faster — and stops."""
    O = (480, 270)
    els = [spr("yeux", "yeux", Track(pos=(O[0], O[1] - 30), scale=3.0, alpha=0.5).to(1.3, "in", alpha=0.0).clip(0, "tournent"), add=True)]
    for i, t in enumerate(NOIR_TICKS):
        els.append(glo(f"tic{i + 1}", CYAN, 18 + 3 * i, Track(pos=O, alpha=0.0).to(t - 0.01, alpha=0.0).to(t, alpha=0.08 + 0.02 * i).to(t + 0.12, alpha=0.0)))
    els.append(glo("arret", "#ffffff", 40, Track(pos=O, alpha=0.0).to(T_STOP + 0.05, alpha=0.0).to(T_STOP + 0.1, alpha=0.7).to(T_STOP + 0.9, "out", alpha=0.0)))
    return dict(backdrop="#000000", camera=A.camera((0, O, 1.0, "linear")), elements=els, still=0.4)


# =================================================================== Encore un tour (after a defeat)
WOUNDS = [(78, 46), (60, 62), (96, 70), (84, 92)]  # on the titan's canvas (its layers, upright)


def titan_els(titan, D, prefix="", look="affaibli", alpha=1.0, depth=1.0):
    els = titan.parts(D, prefix=prefix, look=look, alpha=alpha, depth=depth, breathe=len(titan.place) == 1)
    O, s = titan.at(0)
    tr = Track(pos=A.canvas_at(A.JOINT["coeur"], O, s), alpha=0.55)
    els += A.heart_glow(tr, depth=depth, radius=70 * s)
    return els


def flee_track(path, D, bob=8.0, lean=(-16.0, -20.0)):
    """Asterion in flight: through `path` [(t, pos, scale)], bobbing and leaning into its flight."""
    t0, p0, s0 = path[0]
    tr = Track(pos=p0, scale=s0, rot=lean[0])
    k, t = 0, 0.6
    for (ta, pa, sa), (tb, pb, sb) in zip(path, path[1:]):
        while t < tb:
            u = (t - ta) / max(1e-6, tb - ta)
            p = (pa[0] + (pb[0] - pa[0]) * u, pa[1] + (pb[1] - pa[1]) * u + (bob if k % 2 else -bob))
            tr.to(t, "inout", pos=p, scale=sa + (sb - sa) * u, rot=lean[k % 2])
            k += 1
            t += 0.6
    tr.to(path[-1][0], "inout", pos=path[-1][1], scale=path[-1][2], rot=lean[0])
    return tr.clip(0, "fuit")


def scene_fuite(D, shot):
    """Wounded Asterion flees: turned three quarters, leaning into its flight, its cracks glowing; the camera follows it,
    the stars streak past, a long trail of shards of worlds falls behind it."""
    els = A.space(D, drift=-30.0, nebula=0.5)
    els.append(A.stars(0.8))
    els.append(lay("etoiles_proches", "etoiles", 0.35, [K(0, pos=(-60, -40), scale=1.15)], drift=(160.0, 0), repeat=True))
    els.append(lay("filantes", "filantes", 0.6, [K(0, pos=(-40, -20), scale=2.2, alpha=0.7)], add=True, drift=(900.0, 0), repeat=True))
    path = [(0.0, (620, 290), 1.35), (D, (430, 270), 1.42)]
    for k, (dx, a) in enumerate(((210, 0.12), (140, 0.2), (70, 0.32))):  # its afterimages, behind it
        tr = flee_track([(t, (p[0] + dx, p[1] + 6), s) for t, p, s in path], D)
        e = spr(f"sillage{k + 1}", "asterion_fuite", tr, add=True, tint="#ff6a3d")
        e["keys"][0]["alpha"] = a
        els.append(e)
    rng = np.random.default_rng(8311)
    for i in range(26):  # the shards of worlds falling behind it
        t0 = float(rng.uniform(0, D - 1.2))
        u = t0 / D
        O = (path[0][1][0] + (path[1][1][0] - path[0][1][0]) * u + 90, path[0][1][1] + float(rng.uniform(-90, 110)))
        p1 = (O[0] + float(rng.uniform(700, 1100)), O[1] + float(rng.uniform(-80, 120)))
        tr = Track(pos=O, scale=float(rng.uniform(1.2, 2.6)), alpha=0.0, rot=float(rng.uniform(0, 360))).to(t0, alpha=0.0).to(t0 + 0.05, alpha=1.0).to(
            t0 + float(rng.uniform(1.0, 1.8)), "in", pos=p1, rot=float(rng.uniform(-400, 400)))
        tr.clip(0, ("roche", "magma", "glace")[i % 3])
        els.append(spr(f"chute{i + 1}", "debris" if i % 2 else STORIES + "asterion_eclat.json", tr, depth=1.05))
    els.append(spr("asterion", "asterion_fuite", flee_track(path, D)))
    hp = Track(pos=(path[0][1][0] + 4, path[0][1][1] - 10), alpha=0.6).to(D, "inout", pos=(path[1][1][0] + 4, path[1][1][1] - 10))
    els += A.heart_glow(hp, radius=60)
    trail = particles("trainee", "embers", "#ff8a3a", (560, 150, 400, 260), [K(0, alpha=0.9)], depth=1.0)
    els.append(trail)
    els.append(particles("sang_de_mondes", "ember_rain", "#ff6a3d", (500, 100, 460, 320), [K(0, alpha=0.4)], depth=1.0))
    cam = A.camera((0, (540, 280), 1.05, "linear"), (D, (450, 270), 1.08, "linear"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[A.shake(0, D, 1.6)], still=D * 0.45)


T_CATCH = 3.1
SH_OFF = (226, -96)  # where the titan sits in the side hand, from its wrist (stage units at the hand's scale 1)
SH_TITAN = 0.55      # the titan's scale when the hand's is 1


def side_hand_els(wrist, clip_seq, depth=1.0, arm=True):
    """The side hand and its arm, one track (the wrist): the arm and the palm behind what it holds, the near fingers and
    the thumb in front (two lists)."""
    back = [spr("bras", "bras_cote", wrist(), depth=depth)] if arm else []
    back.append(spr("main_paume", "main_cote_paume", clips(wrist(), clip_seq), depth=depth))
    front = [spr("main_doigts", "main_cote_doigts", clips(wrist(), clip_seq), depth=depth)]
    return back, front


def scene_main(D, shot):
    """An immense hand of brass comes out of a tear in time at the edge of the picture, palm open; the titan flies into
    it; its fingers close round it, joint after joint."""
    els = A.space(D, drift=-6.0, nebula=0.5)
    els.append(A.stars(0.8))
    els.append(lay("filantes", "filantes", 0.6, [K(0, pos=(-40, -20), scale=2.2, alpha=0.6), K(2.4, alpha=0.0)], add=True, drift=(500.0, 0), repeat=True))
    T = (60, 300)
    back_t, front_t = tear_els(T, 2.4, 0.4, D + 1.0, D, slit=True)
    els += back_t
    HK = 1.7
    W1 = (70, 392)

    def wrist():
        return Track(pos=(-1000, 400), scale=HK).to(0.9, pos=(-1000, 400)).to(2.5, "out", pos=W1).to(T_CATCH + 0.2, "out", pos=(W1[0] + 8, W1[1] + 6)).to(
            D, "inout", pos=(W1[0] - 30, W1[1] - 8))

    hb, hf = side_hand_els(wrist, [(0, "ouverte"), (T_CATCH - 0.15, "ferme")])
    els += hb
    held = (W1[0] + SH_OFF[0] * HK, W1[1] + SH_OFF[1] * HK)
    tt = Track(pos=(1120, 200), scale=SH_TITAN * HK, rot=-16).to(T_CATCH, "linear", pos=(held[0] + 6, held[1])).to(T_CATCH + 0.3, "out", pos=held, rot=-6).to(
        D, "inout", pos=(held[0] - 30, held[1] - 8), rot=-8)
    els.append(spr("asterion", "asterion_fuite", tt.clip(0, "fuit")))
    els += hf
    els += front_t
    for i, gy in enumerate((-22, 0, 22)):  # the gears of the wrist, their glass lit
        els.append(glo(f"poignet{i + 1}", CYAN, 40, Track(pos=(W1[0] - 14 * HK, W1[1] + gy * HK), alpha=0.0).to(2.4, alpha=0.0).to(2.8, alpha=0.7), pulse=(0.4, 0.9, 0.3 * i)))
    els.append(particles("etincelles", "sparks", "#fff2c0", (300, 60, 360, 300), [K(0, alpha=0.0), K(T_CATCH, alpha=0.0), K(T_CATCH + 0.2, alpha=0.9),
                                                                                     K(D, alpha=0.3)]))
    cam = A.camera((0, (540, 270), 1.0, "linear"), (T_CATCH, (480, 270), 1.0, "inout"), (D, (440, 250), 1.06, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[A.shake(T_CATCH, T_CATCH + 0.8, 9.0)],
                flash=[flash(T_CATCH, BRASS, 0.3, 0.03, 0.6), flash(0.4, "#bff6ff", 0.25, 0.05, 0.6)], still=T_CATCH + 2.2)


THREADS = [0.8, 2.6, 4.4, 6.2]
SEW = 1.3  # a wound's stitching


def scene_fils(D, shot):
    """Close, inside the closed fingers: golden threads of stolen time come down on its wounds and needles of light sew
    them, stitch after stitch, in a shower of sparks; each wound closes into a golden scar."""
    els = [glo("chaleur", "#ffb84a", 620, Track(pos=(480, 300), alpha=0.35), depth=0.2, pulse=(0.1, 3.0)),
           glo("fond", "#3a2a5a", 500, Track(pos=(200, 120), alpha=0.5), depth=0.2)]
    els.append(particles("etoiles", "stars", "#e8e0ff", (0, 0, 960, 540), [K(0, alpha=0.4)], depth=0.1))
    sc = 2.6
    W = (-40, 590)

    def wrist():
        return Track(pos=W, scale=sc)

    hb, _ = side_hand_els(wrist, [(0, "serre")], arm=False)
    els += hb
    titan = A.Titan((470, 300), 1.55)
    els += titan_els(titan, D)

    def seq(i):
        return [(0, "ouverte", 0.2 * i), (THREADS[i] + SEW, "referme")]

    O, s = titan.at(0)
    rng = np.random.default_rng(8321)
    for i, w in enumerate(WOUNDS):
        t = THREADS[i]
        c = A.canvas_at(w, O, s)
        tr = Track(pos=c, scale=s * 1.05)
        els.append(spr(f"plaie{i + 1}", "plaie", clips(tr, seq(i)), add=True))
        # the thread, from out of the picture (the fingertips), to the wound
        root = (980 + 30 * i, -40 - 20 * i)
        ang = math.degrees(math.atan2(c[1] - root[1], c[0] - root[0]))
        ln = math.hypot(c[0] - root[0], c[1] - root[1]) / (196 * PIX)
        th = Track(pos=root, scale=ln, rot=ang, alpha=0.0).to(t - 0.01, alpha=0.0).to(t, alpha=1.0).to(t + SEW + 0.6, alpha=1.0).to(t + SEW + 1.4, alpha=0.3)
        els.append(spr(f"fil{i + 1}", "fil", clips(th, [(0, "tisse"), (t, "tisse")]), add=True))
        # the stitches and the needle sewing them: across the wound and back, six times
        st = Track(pos=c, scale=s * 1.05, alpha=0.0).to(t + 0.35, alpha=0.0).to(t + 0.4, alpha=1.0)
        els.append(spr(f"couture{i + 1}", "couture", clips(st, [(0, "coud"), (t + 0.4, "coud")]), add=True))
        half = 20 * s * 1.05
        nd = Track(pos=(c[0] - half, c[1] - 30), scale=s * 0.9, alpha=0.0, rot=60).to(t + 0.3, alpha=0.0).to(t + 0.38, alpha=1.0)
        for k in range(6):
            x = c[0] - half + (k + 0.5) * (2 * half / 6)
            up = k % 2 == 0
            nd.to(t + 0.4 + k * 0.15 + 0.07, "inout", pos=(x, c[1] + (14 if up else -14) * s), rot=70 if up else -70)
            nd.to(t + 0.4 + k * 0.15 + 0.15, "inout", pos=(x + 4 * s, c[1] + (-14 if up else 14) * s), rot=-70 if up else 70)
        nd.to(t + SEW, alpha=1.0).to(t + SEW + 0.3, "in", pos=(c[0] + 80, c[1] - 120), alpha=0.0, rot=-40)
        els.append(spr(f"aiguille{i + 1}", "aiguille_lumiere", nd.clip(0, "brille"), add=True))
        sp = Track(pos=(c[0] - half, c[1]), alpha=0.0).to(t + 0.38, alpha=0.0).to(t + 0.42, alpha=0.9).to(t + SEW, "linear", pos=(c[0] + half, c[1])).to(
            t + SEW + 0.3, alpha=0.0)
        els.append(glo(f"etincelle{i + 1}", "#fff2c0", 34, sp, pulse=(0.5, 0.12)))
        els.append(glo(f"or{i + 1}", GOLD, 70, Track(pos=c, alpha=0.0).to(t + 0.3, alpha=0.0).to(t + 0.7, alpha=0.8).to(t + SEW + 1.5, alpha=0.25),
                       pulse=(0.3, 0.5)))
        els.append(particles(f"gerbe{i + 1}", "sparks", "#fff2c0", (c[0] - half, c[1] - 30, 2 * half, 60),
                             [K(0, alpha=0.0), K(t + 0.35, alpha=0.0), K(t + 0.45, alpha=1.0), K(t + SEW + 0.4, alpha=0.0)]))
    els.append(particles("temps_vole", "dust", GOLD, (120, 60, 720, 420), [K(0, alpha=0.4), K(THREADS[-1] + SEW, alpha=0.8)], depth=1.1))
    pts = [A.canvas_at(w, O, s) for w in WOUNDS]
    cam = A.camera((0, pts[0], 1.25, "linear"), (THREADS[1] + 0.4, pts[1], 1.3, "inout"), (THREADS[2] + 0.4, pts[2], 1.3, "inout"),
                   (THREADS[3] + 0.4, pts[3], 1.3, "inout"), (D, (470, 300), 1.05, "inout"))
    return dict(backdrop="#0a0608", camera=cam, elements=els, still=THREADS[1] + 0.9)


def scene_encore(D, shot):
    """The old man, a silhouette against the warm light of the dial, before the titan held — small, far — in the hand
    that comes out of a tear; he reaches out to it."""
    els = A.space(D, drift=2.0, nebula=0.5)
    els.append(A.stars(0.7))
    els.append(glo("cadran_lumiere", "#ffd8a0", 560, Track(pos=(330, 300), alpha=0.85), depth=0.25, pulse=(0.08, 2.6)))
    els.append(glo("cadran_coeur", "#fff2c0", 260, Track(pos=(310, 250), alpha=0.6), depth=0.25))
    k = 0.42
    W = (600, 300)
    back_t, front_t = tear_els((W[0] - 10, W[1]), 0.9, -1.0, D + 1.0, D, slit=True)
    for e in back_t + front_t:
        e["depth"] = 0.6
    els += back_t

    def wrist():
        return Track(pos=W, scale=k)

    hb, hf = side_hand_els(wrist, [(0, "serre")], depth=0.6, arm=False)
    els += hb
    held = (W[0] + SH_OFF[0] * k, W[1] + SH_OFF[1] * k)
    els.append(spr("asterion", "asterion_fuite", Track(pos=held, scale=SH_TITAN * k, rot=-8).clip(0, "fuit"), depth=0.6))
    els += hf
    els += front_t
    els.append(glo("coeur_titan", "#ff8a3a", 26, Track(pos=held, alpha=0.6), depth=0.6, pulse=(0.5, 0.67)))
    man = Track(pos=(290, 556), scale=1.32).clip(0, "immobile").clip(2.2, "tend")
    els.append(spr("vieil_homme", "vieil_homme_profil", man, depth=1.0))
    eye = (290 + (P.PM_EYE[0] - P.PMW // 2) * PIX * 1.32, 556 + (P.PM_EYE[1] - (P.PMH - 1)) * PIX * 1.32)
    els.append(glo("monocle", GOLD, 18, Track(pos=eye, alpha=0.0).to(1.0, alpha=0.0).to(1.6, alpha=0.9), pulse=(0.4, 1.0)))
    els.append(particles("poussiere_or", "dust", GOLD, (100, 120, 760, 380), [K(0, alpha=0.4)], depth=0.9))
    cam = A.camera((0, (450, 290), 1.0, "linear"), (D, (460, 280), 1.07, "inout"))
    return dict(backdrop="#05040a", camera=cam, elements=els, still=D * 0.7)


def scene_chaine(D, shot):
    """Near Lyse the void tears again; the watch comes out and draws away from the tear, Asterion hanging under its rod
    by golden chains, swinging slowly like the pendulum of a clock — tick, tock."""
    T = (730, 240)
    els = lyse_sky((150, 120), 0.85)
    els.append(lay("etoiles_proches", "etoiles", 0.35, [K(0, pos=(-60, -40), scale=1.15)], drift=(-2.0, 0), repeat=True))
    t_open, t_gong = 0.7, 2.2
    t_close = 6.4
    back, front = tear_els(T, 1.8, t_open, t_close, D, slit=True)
    els += back
    ship = Ship(T, 0.08, 0.0).move(t_gong, alpha=0.0).move(t_gong + 0.5, s=0.2, alpha=1.0, ease="out").move(6.8, O=(430, 118), s=0.6, ease="inout")
    swing = [(0, 0.0, "linear"), (t_gong + 0.8, 0.0, "linear")]
    t, side = t_gong + 2.0, 1
    while t < D + 1.25:
        swing.append((t, 15.0 * side, "inout"))
        side = -side
        t += 1.25
    els += ship_parts(ship, gears=((0, "folle"), (6.8, "tourne")), crown=((0, "folle"), (6.8, "tourne")), dome=((0, "vacille"),),
                      lights=((0, "eteint"),), hm=((0, "folle"), (6.8, "tic")), hh=((0, "folle"), (6.8, "tic")), rod="enchaine", rod_rot=swing)
    els += front
    els.append(particles("eclairs", "sparks", "#9ff6ff", (600, 40, 260, 420), [K(0, alpha=0.0), K(t_open + 0.3, alpha=0.9), K(t_close, alpha=0.5), K(t_close + 0.6, alpha=0.0)]))
    cam = A.camera((0, (560, 250), 1.0, "linear"), (D, (470, 250), 1.04, "inout"))
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=[A.shake(t_gong, t_gong + 0.9, 7.0)],
                flash=[flash(t_gong, "#bff6ff", 0.5, 0.05, 0.9)], still=8.6)


CHAINE_SWING = [2.2 + 2.0 + 1.25 * k for k in range(5)]


# =================================================================== 2.82, longer films: the workshop, the empty rod, the struggle, the six worlds
def scene_atelier(D, shot):
    """Inside the watch: a workshop of a thousand clocks, all stopped at the same second; on the workbench, under a lamp,
    plans of Asterion drawn as a balance wheel. (The same shot in both films.)"""
    els = [lay("mur", "horloges", 0.35, [K(0, pos=(-80, -60), scale=1.3)])]
    rng = np.random.default_rng(8401)
    for i in range(8):  # great clocks on the wall, stopped too
        p = (60 + i * 120 + float(rng.uniform(-30, 30)), float(rng.uniform(70, 260)))
        els.append(spr(f"horloge{i + 1}", "grande_horloge", Track(pos=p, scale=float(rng.uniform(1.1, 1.8))), depth=0.6 + 0.04 * i))
    els.append(glo("lampe", "#ffd8a0", 460, Track(pos=(540, 380), alpha=0.75), depth=0.9, pulse=(0.06, 3.0)))
    els.append(glo("verre_froid", CYAN, 380, Track(pos=(120, 90), alpha=0.25), depth=0.4))
    els.append(lay("etabli", "etabli", 1.0, [K(0, pos=(-60, 392), scale=1.15)]))
    els.append(spr("plan", "plan", Track(pos=(540, 452), scale=1.35, rot=-3).clip(0, "trace"), depth=1.0))
    els.append(spr("rouage_pose1", "engrenage_petit", Track(pos=(290, 446), scale=0.7).clip(0, "arret"), depth=1.0))
    els.append(spr("rouage_pose2", "engrenage_moyen", Track(pos=(800, 458), scale=0.5, rot=20).clip(0, "arret"), depth=1.0))
    els.append(spr("laiton_pose", "laiton", Track(pos=(370, 470), scale=1.4, rot=-30).clip(0, "vis"), depth=1.0))
    els.append(glo("plan_lueur", "#9fd8ff", 120, Track(pos=(540, 440), alpha=0.0).to(4.0, alpha=0.0).to(5.0, alpha=0.5), depth=1.0, pulse=(0.3, 1.2)))
    p = particles("poussiere", "dust", "#ffe6b0", (300, 160, 480, 300), [K(0, alpha=0.6)], depth=0.95)
    p["hold"] = [0.0]  # even the dust stopped
    els.append(p)
    cam = A.camera((0, (470, 200), 1.12, "linear"), (D * 0.45, (500, 300), 1.2, "inout"), (D, (540, 420), 1.65, "inout"))
    return dict(backdrop="#0c0806", camera=cam, elements=els, still=D * 0.8)


def scene_balancier_vide(D, shot):
    """Under the watch, its rod snapped and empty, still swaying in the void; shards of brass drift around it."""
    els = lyse_sky((780, 380), 0.7)
    els.append(lay("etoiles_proches", "etoiles", 0.35, [K(0, pos=(-60, -40), scale=1.15)], drift=(-3.0, 0), repeat=True))
    ship = Ship((480, -70), 1.55)
    swing = [(0, -9.0, "linear")]
    t, side = 1.6, 1
    while t < D + 1.6:
        swing.append((t, 9.0 * side, "inout"))
        side = -side
        t += 1.6
    els += ship_parts(ship, gears=((0, "lent"),), crown=((0, "arret"),), dome=((0, "vacille"),), lights=((0, "eteint"),), hm=((0, "tic"),),
                      hh=((0, "tic"),), rod="brisee", rod_rot=swing)
    rng = np.random.default_rng(8402)
    for i in range(16):
        p0 = (float(rng.uniform(80, 880)), float(rng.uniform(200, 520)))
        p1 = (p0[0] + float(rng.uniform(-40, 40)), p0[1] + float(rng.uniform(-30, 30)))
        tr = Track(pos=p0, scale=float(rng.uniform(1.0, 2.0)), rot=float(rng.uniform(0, 360))).to(D, "linear", pos=p1, rot=float(rng.uniform(-90, 90)))
        els.append(spr(f"laiton{i + 1}", "laiton", tr.clip(0, ("rouage", "vis", "plaque", "ressort")[i % 4]), depth=float(rng.uniform(0.8, 1.3))))
    piece = Track(pos=(620, 470), scale=0.5, rot=40).to(D, "linear", pos=(650, 520), rot=80)
    els.append(spr("tige_morceau", "tige_morceau", piece, depth=0.7))
    els.append(particles("etincelles", "sparks", BRASS, (300, 200, 360, 200), [K(0, alpha=0.5)]))
    cam = A.camera((0, (480, 280), 1.0, "linear"), (D, (480, 250), 1.08, "inout"))
    return dict(backdrop="#04030a", camera=cam, elements=els, still=D * 0.5)


STRUGGLE = [2.6, 3.8, 4.9, 5.9, 6.8]


def scene_resiste(D, shot):
    """Asterion turns, roars and tries to tear itself free — but time slows around it: each lunge leaves an image of it
    behind, frozen, one after another."""
    els = A.space(D, drift=-8.0, nebula=0.5)
    for e in els:
        e["hold"] = [5.6]
    st = A.stars(0.8)
    st["hold"] = [5.0]
    els.append(st)
    els.append(lay("cadran", "cadran_fantome", 0.5, Track(pos=(480 - 160 * PIX * 1.5, 290 - 160 * PIX * 1.5), scale=1.5, alpha=0.0).to(2.0, alpha=0.0).to(
        6.5, alpha=0.4), add=True))
    O = (480, 300)

    def track(alpha_from=None):
        tr = Track(pos=(560, 300), scale=1.3, rot=-16, flip=None)
        tr.to(0.8, pos=(520, 300), rot=-10).to(1.1, "out", pos=O, rot=4)
        tr.to(1.5, "out", scale=1.42).to(2.2, "inout", scale=1.34)
        amp = 70.0
        for k, t in enumerate(STRUGGLE):
            side = 1 if k % 2 == 0 else -1
            tr.to(t, "out", pos=(O[0] + side * amp, O[1] - 20 * side), rot=4 + 8 * side)
            tr.to(t + 0.3 + 0.15 * k, "inout", pos=(O[0] + side * amp * 0.3, O[1]), rot=4)
            amp *= 0.7
        if alpha_from is not None:
            tr.to(alpha_from - 0.01, "step", alpha=0.0).to(alpha_from, "step", alpha=0.5)
        return tr

    for k, t in enumerate(STRUGGLE):  # the images it leaves, frozen one after another
        tr = track(alpha_from=t)
        tr.ch["alpha"] = [(0.0, 0.0, "linear")] + tr.ch["alpha"][1:]
        e = spr(f"fige{k + 1}", "asterion_fuite", tr.clip(0, "fuit"), add=True, tint="#7fe8ff", hold=(t,))
        e["keys"][0]["flip"] = True
        els.append(e)
    e = spr("asterion", "asterion_fuite", track().clip(0, "fuit"))
    e["keys"][0]["flip"] = False
    turn = next(k for k in e["keys"] if k["t"] >= 0.8)
    turn["flip"] = True  # it turns round
    els.append(e)
    els.append(glo("rugissement", "#ff8a3a", 260, Track(pos=(O[0] - 10, O[1] - 60), alpha=0.0).to(1.3, alpha=0.0).to(1.5, alpha=0.9).to(2.6, alpha=0.2)))
    els.append(glo("froid", "#9ff6ff", 520, Track(pos=O, alpha=0.0).to(2.4, alpha=0.0).to(7.0, alpha=0.35), depth=0.6))
    p = particles("braises", "embers", "#ff8a3a", (200, 120, 560, 340), [K(0, alpha=0.8)])
    p["hold"] = [5.4]
    els.append(p)
    cam = A.camera((0, (500, 290), 1.0, "linear"), (1.5, (480, 280), 1.06, "out"), (D, (480, 290), 1.2, "inout"))
    return dict(backdrop="#030308", camera=cam, elements=els, shake=[A.shake(1.4, 2.4, 9.0)] + [A.shake(t, t + 0.25, 4.0 - 0.6 * k) for k, t in enumerate(STRUGGLE)],
                flash=[flash(5.5, "#bff6ff", 0.25, 0.1, 1.0)], still=6.4)


WORLD_GUARD = {"serre": (760, 1.7), "glacier": (230, 1.8), "forge": (770, 1.75), "ruines": (220, 1.7), "archives": (750, 1.85), "cadran": (480, 1.6)}


def world(kind, D, els_mid, cam=None, flashes=None):
    """A world of the dial seen through the glass of the dome: its backdrop, what its law of time does, the shadow that
    keeps it (rising from the dark), the glass and its ribs in front."""
    els = [lay("monde", "monde_" + kind, 0.4, [K(0, pos=(-60, -40), scale=2.2)], drift=(-4.0, 0))]
    gx, gs = WORLD_GUARD[kind]
    gy = 540 + 40
    guard = Track(pos=(gx, gy), scale=gs, alpha=0.0).to(0.6, alpha=0.0).to(2.0, alpha=0.92).clip(0, kind)
    els.append(spr("gardien", "gardien", guard, depth=0.6))
    els += els_mid
    els.append(spr("vitre_nervures", "nervures", Track(pos=(480, 330), scale=4.3, alpha=0.45), depth=1.2))
    els.append(spr("vitre", "verre", Track(pos=(480, 330), scale=4.3, alpha=0.4).clip(0, "reflet", 0.8), depth=1.2, add=True))
    cam = cam or A.camera((0, (470, 280), 1.04, "linear"), (D, (490, 270), 1.12, "inout"))
    return dict(backdrop="#04040a", camera=cam, elements=els, flash=flashes or [], still=D * 0.7)


def scene_monde_serre(D, shot):
    """The Serre d'Avant: vines growing too fast, over and over."""
    mid = []
    for i, x in enumerate((90, 300, 520, 700, 880)):
        mid.append(spr(f"lianes{i + 1}", "lianes", Track(pos=(x, 560), scale=1.9 + 0.2 * (i % 2)).clip(0, "pousse", 0.17 * i), depth=0.85))
    mid.append(particles("feuilles", "dust", "#8fe8a0", (60, 100, 840, 400), [K(0, alpha=0.7)], depth=0.9))
    return world("serre", D, mid)


def scene_monde_glacier(D, shot):
    """The Glacier de Demain: snow falling in slow motion."""
    mid = []
    rng = np.random.default_rng(8411)
    for i in range(18):
        p0 = (float(rng.uniform(40, 920)), float(rng.uniform(40, 460)))
        tr = Track(pos=p0, scale=float(rng.uniform(1.2, 2.6)), rot=float(rng.uniform(0, 60))).to(D, "linear", pos=(p0[0] + 6, p0[1] + 22), rot=float(rng.uniform(70, 90)))
        mid.append(spr(f"flocon{i + 1}", "flocon", tr.clip(0, "idle", 0.3 * i), depth=float(rng.uniform(0.8, 1.15)), add=True))
    p = particles("neige", "snow", "#e6fffb", (0, 0, 960, 540), [K(0, alpha=0.8)], depth=0.9)
    p["hold"] = [0.3, 40.0]
    mid.append(p)
    return world("glacier", D, mid)


def scene_monde_forge(D, shot):
    """The Forge en Boucle: the same blow struck again and again, pulled back each time."""
    loop = 0.18 + 0.1 + 0.06 + 0.12 + 0.2 + 0.06
    mid = [spr("enclume", "enclume", Track(pos=(470, 470), scale=2.4).clip(0, "frappe"), depth=0.85)]
    mid.append(particles("braises", "embers", "#ffd070", (300, 240, 360, 260), [K(0, alpha=0.8)], depth=0.9))
    mid.append(glo("braise", "#ff8a2a", 200, Track(pos=(470, 420), alpha=0.6), depth=0.85, pulse=(0.5, loop)))
    fl, t = [], 0.66
    while t < D:
        fl.append(flash(t, "#9ff6ff", 0.14, 0.02, 0.18))
        t += loop
    return world("forge", D, mid, flashes=fl)


def scene_monde_ruines(D, shot):
    """The Ruines d'Aethra: fallen stones rising back into their column, as if the fall were rewound."""
    mid = []
    rng = np.random.default_rng(8412)
    for i in range(6):
        p0 = (float(rng.uniform(380, 760)), float(rng.uniform(470, 510)))
        p1 = (560 + float(rng.uniform(-3, 3)), 470 - i * 44)
        t0 = 0.3 + 0.35 * i
        tr = Track(pos=p0, scale=3.0, rot=float(rng.uniform(-120, 120))).to(t0, pos=p0).to(t0 + 1.2, "out", pos=p1, rot=0.0)
        mid.append(spr(f"pierre{i + 1}", "debris", tr.clip(0, "roche"), depth=0.85, tint="#c8a8ff"))
    mid.append(particles("rebours", "dust", "#f0c8ff", (420, 180, 300, 320), [K(0, alpha=0.8)], depth=0.9))
    mid.append(glo("runes", "#d6a8ff", 160, Track(pos=(560, 330), alpha=0.3).to(D, alpha=0.6), depth=0.85, pulse=(0.3, 1.0)))
    return world("ruines", D, mid)


def scene_monde_archives(D, shot):
    """The Archives Figées: pages caught in the air between two seconds, dust that does not fall."""
    mid = []
    rng = np.random.default_rng(8413)
    for i in range(16):
        p = (float(rng.uniform(40, 920)), float(rng.uniform(60, 460)))
        mid.append(spr(f"page{i + 1}", "page", Track(pos=p, scale=float(rng.uniform(1.6, 2.6)), rot=float(rng.uniform(-50, 50))).clip(0, "idle", 0.25 * i),
                       depth=float(rng.uniform(0.8, 1.15))))
    p = particles("poussiere", "dust", "#e6eaf2", (40, 40, 880, 460), [K(0, alpha=0.8)], depth=0.9)
    p["hold"] = [0.0]
    mid.append(p)
    return world("archives", D, mid)


def scene_monde_cadran(D, shot):
    """The Cadran Central: a dial for a floor, a hand as long as a street turning slowly, columns of light."""
    mid = [lay("cadran_sol", "cadran_fantome", 0.8, Track(pos=(480 - 160 * PIX * 1.6, 520 - 160 * PIX * 1.6), scale=1.6, alpha=0.7), add=True, tint=GOLD)]
    mid.append(spr("aiguille_sol", "aiguille_fantome", Track(pos=(480, 520), scale=2.2, rot=-40).to(D, "linear", rot=-22), depth=0.8, add=True, tint=GOLD))
    mid.append(particles("or", "dust", GOLD, (120, 60, 720, 420), [K(0, alpha=0.7)], depth=0.9))
    return world("cadran", D, mid)


SCENES = {"onde": scene_onde, "tic": scene_tic, "fige": scene_fige, "dechirure": scene_dechirure, "vieil_homme": scene_vieil_homme,
          "remonter": scene_remonter, "fuite": scene_fuite, "main": scene_main, "fils": scene_fils, "encore": scene_encore,
          "chaine": scene_chaine, "cadran": scene_cadran, "regard": scene_regard, "noir": scene_noir, "atelier": scene_atelier,
          "balancier_vide": scene_balancier_vide, "resiste": scene_resiste, "monde_serre": scene_monde_serre, "monde_glacier": scene_monde_glacier,
          "monde_forge": scene_monde_forge, "monde_ruines": scene_monde_ruines, "monde_archives": scene_monde_archives, "monde_cadran": scene_monde_cadran}


# =================================================================== sounds
def ticking(t0, t1, period=1.0, vol=0.45, pitch=1.0):
    out, t, k = [], t0, 0
    while t < t1:
        out.append(sfx("story_tick" if k % 2 == 0 else "story_tock", t, vol, pitch=pitch))
        t += period
        k += 1
    return out


def sounds(name, D):
    if name == "onde":
        return [amb("story_amb_space", 0.35), sfx("story_glow", 0.2, 0.55, pitch=0.7), sfx("story_stone", 1.2, 0.35, pan=-0.4), sfx("story_stone", 3.0, 0.3, pan=0.5)]
    if name == "tic":
        out = [amb("story_amb_space", 0.3)]
        for i, t in enumerate(TICKS):
            out.append(sfx(("story_tick", "story_tock")[i % 2], t, 0.55) if i < 4 else sfx("story_tick_faux", t, 0.6, pitch=(1.0, 0.86, 1.12, 0.8, 0.94, 0.7)[i - 4]))
        return out
    if name == "fige":
        return [amb("story_amb_space", 0.3, to=T_FREEZE), sfx("story_tick_faux", T_FREEZE - 0.5, 0.5, pitch=0.7), sfx("story_freeze", T_FREEZE - 0.3, 0.8, duck=True),
                amb("story_amb_deep", 0.18, t=T_FREEZE + 0.6)]
    if name == "dechirure":
        return [amb("story_amb_space", 0.3), sfx("story_tear", T_TEAR, 0.75), sfx("story_gong", T_GONG, 0.85, duck=True)] + ticking(T_GONG + 0.6, D - 0.4, 0.09, 0.22, 1.3) + [
            sfx("story_clang", T_GONG + 1.8, 0.45, pitch=1.3)]
    if name == "vieil_homme":
        return [amb("story_amb_deep", 0.25)] + ticking(0.8, D, 2.0, 0.3, 0.75) + [sfx("story_glow", 3.5, 0.45, pitch=1.3)]
    if name == "remonter":
        return [amb("story_amb_deep", 0.25), sfx("story_reveal", 1.1, 0.55, pitch=1.2)] + ticking(1.5, D, 0.5, 0.35, 0.9)
    if name == "cadran":
        out = [amb("story_amb_space", 0.3)] + [sfx("story_glow", 1.2 + 0.32 * (k + 1), 0.35, pitch=0.9 + 0.08 * k) for k in range(6)]
        return out + [sfx("story_gong", 1.2 + 0.32 * 7, 0.7, duck=True)] + ticking(4.4, D, 1.0, 0.45)
    if name == "regard":
        return [amb("story_amb_deep", 0.25, to=D - 1.0)] + ticking(0.0, D - 0.6, 1.0, 0.5)
    if name == "noir":
        return [sfx("story_tick" if i % 2 == 0 else "story_tock", t, 0.5 + 0.02 * i, pitch=1.0 + 0.02 * i) for i, t in enumerate(NOIR_TICKS)]
    if name == "atelier":
        return [amb("story_amb_deep", 0.22), sfx("story_glow", 4.2, 0.4, pitch=0.9), sfx("story_tock", D - 1.2, 0.3, pitch=0.6)]
    if name == "balancier_vide":
        return [amb("story_amb_space", 0.3)] + [sfx("story_chain", 1.6 * k, 0.22, pitch=0.6) for k in range(1, 5) if 1.6 * k < D] + [
            sfx("story_tick_faux", 3.0, 0.3, pitch=0.7)]
    if name == "resiste":
        return [amb("story_amb_space", 0.3, to=5.6), sfx("story_titan_roar", 1.3, 0.75, duck=True), sfx("story_heartbeat", 2.4, 0.4)] + [
            sfx("story_tick_faux", t, 0.45, pitch=1.0 - 0.1 * k) for k, t in enumerate(STRUGGLE)] + [sfx("story_freeze", 5.4, 0.6), amb("story_amb_deep", 0.2, t=5.8)]
    if name.startswith("monde_"):
        base = ticking(0.0, D, 1.0, 0.3)
        kind = name[6:]
        if kind == "serre":
            return base + [sfx("story_glow", 0.4, 0.35, pitch=1.4), sfx("story_wings", 1.4, 0.25, pitch=1.5)]
        if kind == "glacier":
            return base + [sfx("story_freeze", 0.2, 0.3, pitch=1.3)]
        if kind == "forge":
            loop = 0.72
            return base + [sfx("story_clang", 0.34 + loop * k, 0.3, pitch=1.2) for k in range(6) if 0.34 + loop * k < D]
        if kind == "ruines":
            return base + [sfx("story_stone", 0.3 + 0.35 * k, 0.25, pitch=1.3) for k in range(6)]
        if kind == "archives":
            return [sfx("story_tick_faux", 0.3, 0.3, pitch=0.6), sfx("story_glow", 1.0, 0.3, pitch=0.7)]
        return base + [sfx("story_sing", 0.5, 0.35, pitch=0.8)]
    if name == "fuite":
        return [amb("story_amb_space", 0.35), sfx("story_titan_roar", 1.0, 0.55, pitch=0.8), sfx("story_heartbeat", 4.0, 0.4, pitch=0.8),
                sfx("story_stone", 2.5, 0.35, pan=0.5), sfx("story_heartbeat", 7.0, 0.35, pitch=0.75)]
    if name == "main":
        return [amb("story_amb_space", 0.3), sfx("story_tear", 0.4, 0.6), sfx("story_titan_roar", 1.4, 0.4, pitch=0.85), sfx("story_servo", T_CATCH - 0.9, 0.7),
                sfx("story_clang", T_CATCH, 0.8, pitch=0.6, duck=True), sfx("story_chain", T_CATCH + 0.4, 0.4)]
    if name == "fils":
        return [amb("story_amb_space", 0.3)] + [sfx("story_thread", t, 0.55, pitch=1.0 + 0.06 * i) for i, t in enumerate(THREADS)] + [
            sfx("story_tick", t + 0.4 + 0.15 * k, 0.18, pitch=2.0) for t in THREADS for k in range(6)] + [
            sfx("story_heartbeat", THREADS[-1] + 1.6, 0.5)]
    if name == "encore":
        return [amb("story_amb_deep", 0.25), sfx("story_heartbeat", 1.0, 0.35, pitch=0.9)] + ticking(0.5, D, 2.0, 0.3, 0.75)
    if name == "chaine":
        return [amb("story_amb_space", 0.3), sfx("story_tear", 0.7, 0.75), sfx("story_gong", 2.2, 0.85, duck=True), sfx("story_chain", 3.4, 0.5),
                sfx("story_tear", 6.4, 0.35, pitch=0.7)] + ticking(2.8, 6.8, 0.09, 0.2, 1.3) + [
            sfx("story_tock" if k % 2 else "story_tick", t, 0.55, pitch=0.7) for k, t in enumerate(CHAINE_SWING) if t < D]
    return []


# =================================================================== the scenes files
def read_script(film):
    with open(FILMS[film], encoding="utf-8") as f:
        return json.load(f)


def build_scenes(film):
    script = read_script(film)
    scenes, stills = {}, {}
    for shot in script["shots"]:
        name = shot["scene"]
        D = float(shot.get("duration") or S.shot_duration(shot.get("text", "")))
        st = SCENES[name](D, shot)
        stills[name] = st.pop("still")
        st["sfx"] = [x for x in sounds(name, D) if x["t"] < D]
        stage = {"size": [W, H], "length": round(D, 3)}
        stage.update(st)
        scenes[name] = stage
    rel = script["scenes"]
    out = {"format": "cosmic.film_scenes", "version": 1,
           "comment": (f"The animated scenes of {script['title']} (2.82), named by data/raids/{os.path.basename(FILMS[film])} (its captions, titles "
                       "and lengths: edit them there). Written by tools/assetgen/environment/story_horloger.py: do not edit. Each scene is a stage of "
                       "the stories' format (game/content/BossStories.h); its `length` is the seconds its keys were written for (stretched to the "
                       "script's length). The last three (cadran, regard, noir) are the same in both transition films."),
           "scenes": scenes}
    return {os.path.join("data", rel): A.dump_scenes(out).encode("utf-8")}, stills, scenes


THUMB = (320, 180)


def thumb_bytes(stage, t):
    img = S.render(stage, t, size=THUMB)
    buf = io.BytesIO()
    img.convert("RGB").quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def build_all():
    files = {os.path.join("assets", k): v for k, v in build_assets().items()}
    stills, scenes = {}, {}
    for film in FILMS:
        f, st, sc = build_scenes(film)
        files.update(f)
        stills[film], scenes[film] = st, sc
    return files, stills, scenes


def build_thumbs(scenes, stills):
    return {os.path.join("assets", "ui", "films", "asterion_balancier.png"): thumb_bytes(scenes["balancier"]["dechirure"], stills["balancier"]["dechirure"]),
            os.path.join("assets", "ui", "films", "asterion_tour.png"): thumb_bytes(scenes["tour"]["chaine"], stills["tour"]["chaine"])}


def build_everything():
    files, stills, scenes = build_all()
    A.write(files)  # the thumbnails are rendered from the pictures on the disk
    S.Anim._cache.clear()
    files.update(build_thumbs(scenes, stills))
    return files, stills, scenes


def check():
    errs = []
    a = build_all()[0]
    b, stills, scenes = build_all()
    if a != b:
        errs.append("story_horloger: two builds differ (not deterministic)")
    for rel, data in sorted(a.items()):
        p = os.path.join(REPO, rel)
        if not os.path.exists(p):
            errs.append(f"{rel}: missing (python3 tools/assetgen/environment/story_horloger.py)")
            continue
        with open(p, "rb") as f:
            if f.read() != data:
                errs.append(f"{rel}: not the generator's output (regenerate)")
    if not errs:
        S.Anim._cache.clear()
        for rel, data in build_thumbs(scenes, stills).items():
            p = os.path.join(REPO, rel)
            if not os.path.exists(p) or open(p, "rb").read() != data:
                errs.append(f"{rel}: not the generator's output (regenerate)")
    n = len(COMMON)
    for film in FILMS:  # the last shots: the same in both films
        sh = read_script(film)["shots"][-n:]
        if [s["scene"] for s in sh] != list(COMMON):
            errs.append(f"{film}: its last {n} shots must be {', '.join(COMMON)}")
    if read_script("balancier")["shots"][-n:] != read_script("tour")["shots"][-n:]:
        errs.append(f"the last {n} shots differ between the two films (captions, titles, lengths)")
    ab = [s for s in read_script("balancier")["shots"] if s["scene"] == "atelier"]
    at = [s for s in read_script("tour")["shots"] if s["scene"] == "atelier"]
    if len(ab) != 1 or ab != at:
        errs.append("the workshop (atelier) must be the same shot in both films")
    if any(scenes["balancier"][x] != scenes["tour"][x] for x in COMMON + ("atelier",)):
        errs.append("the shared scenes differ between the two films")
    return errs


def _font(size):
    fonts = sorted(f for f in os.listdir(os.path.join(ASSETS, "fonts")) if f.endswith(".ttf"))
    pick = "ChakraPetch-Regular.ttf" if "ChakraPetch-Regular.ttf" in fonts else fonts[0]
    return ImageFont.truetype(os.path.join(ASSETS, "fonts", pick), size)


def storyboard(film, path):
    """One frame per shot (its still, as the game draws it without its live particles), labelled in French with its
    number, its scene, its length and its caption or title."""
    files, stills, scenes = build_everything()
    S.Anim._cache.clear()
    script = read_script(film)
    fw, fh, lab = 480, 270, 64
    cols = 3
    rows = (len(script["shots"]) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (fw + 12) + 12, rows * (fh + lab + 12) + 60), (16, 14, 26))
    d = ImageDraw.Draw(sheet)
    font, big = _font(14), _font(22)
    total = sum(float(s.get("duration", 0)) for s in script["shots"])
    d.text((12, 16), f"{script['title']} — film de transition, plan par plan ({total:.0f} s)", fill=(255, 210, 160), font=big)

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
        img = S.render(scenes[film][shot["scene"]], stills[film][shot["scene"]], size=(fw, fh))
        sheet.paste(img, (x, y))
        words = shot.get("text") or ""
        if shot.get("titles"):
            words = " / ".join(t["text"] for t in shot["titles"]) + (" / " + words if words else "")
        common = " [commun]" if shot["scene"] in COMMON else ""
        lines = wrap(f"{i + 1}. {shot['scene']}{common} ({float(shot.get('duration', 0)):.0f} s) : {words}", fw)[:3]
        for k, ln in enumerate(lines):
            d.text((x, y + fh + 6 + k * 18), ln, fill=(232, 228, 246), font=font)
    sheet.save(path)
    return path


KEYFRAMES = {"balancier": [("onde", 2.5), ("fige", 5.5), ("dechirure", 6.4), ("vieil_homme", 5.0), ("atelier", 7.0), ("monde_forge", 2.0)],
             "tour": [("fuite", 4.0), ("resiste", 6.0), ("main", 6.0), ("fils", 3.4), ("chaine", 8.6), ("monde_ruines", 2.5)]}


def frames(out_dir):
    """A few key frames of each film, as the game draws them (960 x 540, without live particles)."""
    files, stills, scenes = build_everything()
    S.Anim._cache.clear()
    paths = []
    for film, shots in KEYFRAMES.items():
        for k, (name, t) in enumerate(shots):
            p = os.path.join(out_dir, f"{film}_{k + 1}_{name}.png")
            S.render(scenes[film][name], t, size=(960, 540)).save(p)
            paths.append(p)
    return paths


if __name__ == "__main__":
    if "--check" in sys.argv:
        e = check()
        print("\n".join(e) or "OK")
        sys.exit(1 if e else 0)
    if "--storyboard" in sys.argv:
        i = sys.argv.index("--storyboard")
        print(storyboard(sys.argv[i + 1], sys.argv[i + 2]))
    elif "--frames" in sys.argv:
        print("\n".join(frames(sys.argv[sys.argv.index("--frames") + 1])))
    else:
        for p in A.write(build_everything()[0]):
            print(os.path.relpath(p, REPO), os.path.getsize(p))
