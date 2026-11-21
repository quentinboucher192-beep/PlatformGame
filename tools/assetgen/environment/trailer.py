#!/usr/bin/env python3
"""The game's trailer (2.63): about forty seconds, quick cuts, large titles — played by the same
engine as the giga bosses' stories (version 2 of data/stories/*.json: BossStoryScene), with the
main menu's attract mode (a minute without any input).

Nothing in it is made up: the four planets' own parallax backgrounds, Nova's sheet (run, jump,
dash, shoot, glide, victory), real enemies, the seven giga bosses' own sheets on the scenery of their
stories, the hatchlings of Pyrrhax's story, three Novas for the cooperation, Cosmic Online; the
music is the game's (Singularis's theme).  Every title is French in the data (translated in
data/i18n/en.json).  This writes data/trailer/trailer.json; with --media DIR it also renders the
trailer as frames (the same scenes, through story_stage.render, the titles drawn with the game's
fonts) and makes DIR/trailer.gif (640 px wide) and, with ffmpeg, DIR/trailer.mp4.

    python3 tools/assetgen/environment/trailer.py [--media docs/media]

Deterministic (fixed seeds)."""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

import story_stage as S  # noqa: E402
from story_stage import K, glow, particles, shade  # noqa: E402

REPO = S.REPO
OUT = os.path.join(REPO, "data", "trailer", "trailer.json")
NOVA = "animations/nova.json"
MUSIC = "music/singularis_boss.ogg"
CARD = 4.5  # the last card: the game's name
SX = S.Kit("x")  # sprites given by their whole path


def spr(id_, anim, keys, **kw):
    return SX.sprite(id_, anim, keys, **kw)


def lay(id_, path, depth, pos, scale=1.0, drift=None, repeat=False, alpha=None, tint=None):
    e = {"id": id_, "layer": path, "depth": depth}
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    if repeat:
        e["repeat"] = True
    if tint:
        e["tint"] = tint
    e["keys"] = [K(0, pos=pos, scale=scale, alpha=alpha)]
    return e


def story(boss, name):
    return f"animations/stories/{boss}_{name}.json"


def boss_title(boss):
    """The boss's name as the game shows it (its title in data/enemies, before a comma)."""
    d = json.load(open(os.path.join(REPO, "data", "enemies", f"{boss}.json"), encoding="utf-8"))

    def find(o):
        if isinstance(o, dict):
            if isinstance(o.get("title"), str):
                return o["title"]
            for v in o.values():
                r = find(v)
                if r:
                    return r
        elif isinstance(o, list):
            for v in o:
                r = find(v)
                if r:
                    return r
        return None

    t = find(d) or d.get("name", boss).upper()
    return t.split(",")[0].strip()


def story_title(boss):
    return json.load(open(os.path.join(REPO, "data", "stories", f"{boss}.json"), encoding="utf-8"))["title"]


# =================================================================== pieces
def planet(pl, speed=1.0, ground=None, ground_tint=None, fg=None, tint=None):
    """A planet's own parallax background (its sky, far, mid and near layers), scrolling at its
    depths as Nova runs, a ground under her feet (a scenery sheet of the stories), its foreground."""
    s = 0.8
    pos = ((960 - 1280 * s) / 2, (540 - 720 * s) / 2)
    els = []
    for name, depth, v in (("sky", 0.05, 4), ("far", 0.2, 18), ("mid", 0.45, 50), ("near", 0.75, 110)):
        els.append(lay(name, f"backgrounds/{pl}/{name}.png", depth, pos, scale=s, drift=(-v * speed, 0), repeat=True, tint=tint))
    if ground:
        els.append(spr("sol", ground, [K(0, pos=(-280, 404), clip="idle")], drift=(-170 * speed, 0), tint=ground_tint) | {"repeat": True})
    return els, (lay("avant", f"backgrounds/{pl}/{fg}.png", 1.3, pos, scale=s, drift=(-260 * speed, 0), repeat=True, alpha=0.85)
                 if fg else None)


def nova_jump(t0, x, ground=436, up=150, flip=False, tint=None, id_="nova", scale=1.7, end=None):
    """Nova running, then jumping (a double jump), landing, running on."""
    keys = [K(0, pos=(x, ground), clip="run", scale=scale, flip=flip if flip else None),
            K(t0, clip="jump"), K(t0 + 0.32, pos=(x, ground - up * 0.7), ease="out"),
            K(t0 + 0.34, clip="double_jump"), K(t0 + 0.7, pos=(x, ground - up), ease="out"),
            K(t0 + 0.72, clip="fall"), K(t0 + 1.1, pos=(x, ground), ease="in"), K(t0 + 1.12, clip="land"), K(t0 + 1.3, clip="run")]
    if end is not None:
        keys = [k for k in keys if k["t"] <= end]
    return spr(id_, NOVA, keys, tint=tint, shadow=shade(46, alpha=0.4))


def boss_cut(boss, D, layers, pos, scale, clip="roar", backdrop="#000000", color="#ffffff", parts=()):
    """A giga boss, on the scenery of its story: it roars; a flash, the screen shakes, the camera
    punches in and slowly backs off."""
    els = list(layers)
    els.append(glow("aura", color, 260, [K(0, pos=(pos[0], pos[1] - 150 * scale), alpha=0.5)], pulse=(0.3, 0.5)))
    els.append(spr("boss", f"animations/{boss}.json", [K(0, pos=pos, clip=clip, scale=scale)], shadow=shade(220 * scale, alpha=0.5)))
    els += list(parts)
    cam = [K(0, pos=(pos[0], pos[1] - 140 * scale), zoom=1.38), K(0.35, zoom=1.22, ease="out"), K(D, pos=(pos[0], pos[1] - 120 * scale), zoom=1.1)]
    return dict(backdrop=backdrop, camera=cam, elements=els, shake=[{"from": 0.08, "to": 0.7, "amount": 6.0}],
                flash=[{"t": 0.02, "in": 0.02, "out": 0.3, "color": "#ffffff", "alpha": 0.7}])


# =================================================================== the shots
def shot_system(D):
    els = [spr("cosmos", story("singularis", "cosmos"), [K(0, pos=(-80, -30), clip="idle")], depth=0.03),
           spr("etoiles", story("singularis", "etoiles"), [K(0, pos=(-80, -30), clip="idle")], depth=0.08, drift=(6, 0)) | {"repeat": True},
           spr("voile", story("singularis", "voile"), [K(0, pos=(-200, 300), clip="idle", alpha=0.7)], depth=0.2, drift=(-14, 0)) | {"repeat": True},
           glow("soleil_lueur", "#ffd27a", 220, [K(0, pos=(480, 270), alpha=0.8)], depth=0.5, pulse=(0.25, 1.6)),
           lay("soleil", "ui/planets/sun.png", 0.5, (416, 206), scale=0.25)]
    # The six worlds: the Noyau stellaire is the star at the centre, the five others orbit it.
    orbit = [("sylvanox", 300, 0.0), ("pyralis", 300, 0.2), ("glacior", 300, 0.4), ("aethra", 300, 0.6), ("confins", 300, 0.8)]
    for name, r, ph in orbit:
        keys = []
        for j in range(5):
            t = D * j / 4
            a = 2 * math.pi * (ph + 0.08 * j / 4)
            x, y = 480 + math.cos(a) * r, 270 + math.sin(a) * r * 0.36
            sc = 0.16 + 0.05 * math.sin(a)
            keys.append(K(t, pos=(x - 128 * 2 * sc, y - 128 * 2 * sc), scale=round(sc, 3), ease="linear"))
        els.append({"id": name, "layer": f"ui/planets/{name}.png", "depth": 0.7, "keys": keys})
    els += [particles("etoiles_vives", "stars", "#fff0d8", (0, 0, 960, 540), [K(0, alpha=0.7)], depth=0.1),
            particles("poussiere", "dust", "#ffd27a", (0, 100, 960, 340), [K(0, alpha=0.5)])]
    cam = [K(0, pos=(480, 270), zoom=1.0), K(D, pos=(480, 276), zoom=1.3)]
    return dict(backdrop="#04030a", camera=cam, elements=els)


def shot_run(D):
    els, fg = planet("sylvanox", 1.0, ground=story("monolithe", "sol"), ground_tint="#c8e0b0", fg="fg_vines")
    els += [glow("lumiere", "#c8ff9a", 260, [K(0, pos=(620, 200), alpha=0.35)], depth=0.3, pulse=(0.3, 2.0)),
            nova_jump(1.0, 380),
            particles("pollen", "dust", "#d8ffb0", (0, 120, 960, 340), [K(0, alpha=0.8)]),
            particles("eclats", "sparks", "#ffffff", (300, 260, 200, 160), [K(0, alpha=0.6)])]
    els.append(fg)
    cam = [K(0, pos=(470, 330), zoom=1.3), K(D, pos=(430, 320), zoom=1.22)]
    return dict(backdrop="#06100a", camera=cam, elements=els)


def shot_powers(D):
    els, fg = planet("pyralis", 0.0, ground=story("pyrrhax", "sol"), fg="fg_embers")
    els += [spr("lave", story("pyrrhax", "lave"), [K(0, pos=(220, 462), clip="idle")]),
            glow("lave_lueur", "#ff6a2a", 80, [K(0, pos=(220, 452), alpha=0.55)], pulse=(0.5, 1.4)),
            spr("golem", "animations/magma_golem.json", [K(0, pos=(760, 438), clip="idle", scale=2.2, flip=True), K(1.25, clip="hurt"),
                                                         K(1.55, clip="death")], shadow=shade(90, alpha=0.4)),
            spr("nova", NOVA, [K(0, pos=(120, 436), clip="dash", scale=1.7), K(0.45, pos=(420, 436), ease="out"),
                               K(0.46, clip="shoot", ease="step"), K(0.9, clip="cast"), K(1.6, clip="idle")], shadow=shade(46, alpha=0.4)),
            spr("tir", "animations/projectiles.json", [K(0, pos=(470, 390), alpha=0.0, clip="charge_shot", scale=3.0),
                                                       K(0.55, alpha=0.0, ease="step"), K(0.57, alpha=1.0, ease="step"),
                                                       K(1.2, pos=(740, 380), ease="linear"), K(1.22, alpha=0.0, ease="step")], blend="add"),
            glow("tir_lueur", "#9adcff", 70, [K(0, pos=(470, 390), alpha=0.0), K(0.55, alpha=0.0), K(0.6, alpha=0.9),
                                              K(1.2, pos=(740, 380), ease="linear"), K(1.3, alpha=0.0)]),
            glow("impact", "#ffffff", 160, [K(0, pos=(750, 380), alpha=0.0), K(1.2, alpha=0.0), K(1.25, alpha=0.9, ease="out"),
                                            K(1.9, alpha=0.0)]),
            particles("braises", "embers", "#ff8a3a", (0, 200, 960, 260), [K(0, alpha=0.8)]),
            particles("etincelles", "sparks", "#ffd27a", (640, 300, 220, 160), [K(0, alpha=0.0), K(1.2, alpha=1.0)])]
    els.append(fg)
    cam = [K(0, pos=(380, 340), zoom=1.3), K(0.5, pos=(480, 340), zoom=1.26), K(1.25, pos=(560, 330), zoom=1.32), K(D, pos=(540, 330), zoom=1.2)]
    return dict(backdrop="#12040e", camera=cam, elements=els, shake=[{"from": 1.2, "to": 1.6, "amount": 4.0}])


def shot_enemies(D):
    els, fg = planet("glacior", 0.6, ground=story("phenix", "verre"), ground_tint="#d8f0ff", fg="fg_icicles")
    els += [spr("wisp1", "animations/frost_wisp.json", [K(0, pos=(660, 250), clip="move", scale=2.4), K(D, pos=(620, 230))], blend="add"),
            spr("wisp2", "animations/frost_wisp.json", [K(0, pos=(800, 330), clip="move", scale=2.0, offset=0.3), K(D, pos=(760, 300))],
                blend="add"),
            spr("golem", "animations/ice_golem.json", [K(0, pos=(820, 438), clip="walk", scale=2.4, flip=True), K(D, pos=(700, 438), ease="linear")],
                shadow=shade(90, alpha=0.4)),
            spr("nova", NOVA, [K(0, pos=(160, 120), clip="glide", scale=1.7), K(1.6, pos=(380, 400), ease="inout"),
                               K(1.7, pos=(390, 436), ease="in"), K(1.72, clip="land"), K(1.9, clip="attack")], shadow=shade(46, alpha=0.4)),
            particles("neige", "snow", "#e8f8ff", (0, 0, 960, 540), [K(0, alpha=0.9)]),
            particles("eclats", "sparks", "#c8f0ff", (500, 200, 400, 240), [K(0, alpha=0.7)])]
    els.append(fg)
    cam = [K(0, pos=(400, 260), zoom=1.3), K(D, pos=(520, 330), zoom=1.18)]
    return dict(backdrop="#06101a", camera=cam, elements=els)


def shot_giga(D):
    els = [spr("cosmos", story("singularis", "cosmos"), [K(0, pos=(-80, -30), clip="idle")], depth=0.03, tint="#ff9a9a"),
           spr("etoiles", story("singularis", "etoiles"), [K(0, pos=(-80, -30), clip="idle")], depth=0.08, drift=(30, 0)) | {"repeat": True},
           glow("coeur", "#c88bff", 320, [K(0, pos=(480, 270), alpha=0.2), K(0.3, alpha=0.9, ease="out"), K(D, alpha=0.6)], pulse=(0.3, 0.4)),
           spr("trou", story("singularis", "lyse"), [K(0, pos=(480, 270), clip="implode", scale=2.0)]),
           particles("aspiration", "sparks", "#c88bff", (180, 80, 600, 380), [K(0, alpha=1.0)]),
           particles("braises", "embers", "#ff6a3a", (0, 200, 960, 340), [K(0, alpha=0.8)])]
    cam = [K(0, pos=(480, 270), zoom=1.5), K(D, pos=(480, 270), zoom=1.15)]
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=[{"from": 0.0, "to": 0.8, "amount": 7.0}],
                flash=[{"t": 0.05, "in": 0.05, "out": 0.4, "color": "#ffffff", "alpha": 0.9}])


def bosses():
    """The seven cuts: (id, scenery, point, scale, clip, backdrop, colour)."""
    def L(boss, items):
        out = []
        for i, (name, depth, pos) in enumerate(items):
            e = spr(f"{boss}_{name}", story(boss, name), [K(0, pos=pos, clip="idle")], depth=depth)
            out.append(e)
        return out
    return [
        ("monolithe", L("monolithe", [("grotte_fond", 0.05, (-80, -30)), ("piliers", 0.45, (-320, 150)), ("sol", 1.0, (-280, 400))]),
         (480, 446), 0.85, "roar", "#0a0806", "#ffb347",
         [particles("poussiere", "dustfall", "#c8b090", (0, 40, 960, 380), [K(0, alpha=1.0)])]),
        ("kraken", L("kraken", [("abysses", 0.03, (-80, -30)), ("recifs", 0.6, (-320, 150)), ("algues", 0.8, (-280, 330)),
                                ("fond", 1.0, (-280, 400))]),
         (480, 440), 0.8, "roar", "#010204", "#7ae6ff", [particles("bulles", "embers", "#9adcff", (200, 100, 560, 360), [K(0, alpha=1.0)])]),
        ("phenix", L("phenix", [("ciel_aube", 0.03, (-80, -30)), ("volcans", 0.55, (-320, 140)), ("verre", 1.0, (-280, 400))]),
         (480, 380), 0.8, "summon", "#1a1030", "#ffd04a", [particles("braises", "embers", "#ffb066", (200, 60, 560, 380), [K(0, alpha=1.0)])]),
        ("liche", L("liche", [("galerie", 0.05, (-80, -30)), ("arcades", 0.5, (-320, 100)), ("dalles", 1.0, (-280, 400))]),
         (480, 446), 0.85, "summon", "#060508", "#b4f566", [particles("ames", "sparks", "#b4f566", (200, 100, 560, 360), [K(0, alpha=1.0)])]),
        ("titan", L("titan", [("fonderie", 0.05, (-80, -30)), ("poutres", 0.3, (-240, -20)), ("sol", 1.0, (-280, 400))]),
         (480, 448), 0.85, "roar", "#0a0606", "#ffc23c", [particles("etincelles", "embers", "#ffb040", (200, 200, 560, 260), [K(0, alpha=1.0)])]),
        ("pyrrhax", L("pyrrhax", [("ciel_rouge", 0.03, (-80, -30)), ("pics", 0.5, (-320, 140)), ("sol", 1.0, (-280, 400))]),
         (500, 400), 0.8, "roar", "#12040e", "#ff6a3a", [particles("pluie", "ember_rain", "#ff9a40", (0, -40, 1000, 520), [K(0, alpha=1.0)])]),
        ("singularis", L("singularis", [("cosmos", 0.03, (-80, -30)), ("etoiles", 0.08, (-80, -30)), ("voile", 0.3, (-200, 300))]),
         (480, 360), 0.85, "roar", "#04030a", "#c88bff", [particles("aspiration", "sparks", "#c88bff", (200, 60, 560, 400), [K(0, alpha=1.0)])]),
    ]


def shot_stories(D):
    els = [spr("ciel", story("pyrrhax", "ciel_nuit"), [K(0, pos=(-80, -30), clip="idle")], depth=0.03),
           spr("etoiles", story("pyrrhax", "etoiles"), [K(0, pos=(-80, -20), clip="idle")], depth=0.08, drift=(5, 0)) | {"repeat": True},
           spr("pics", story("pyrrhax", "pics"), [K(0, pos=(-320, 140), clip="idle")], depth=0.5),
           spr("sol", story("pyrrhax", "sol"), [K(0, pos=(-280, 400), clip="idle")]),
           spr("nid_fond", story("pyrrhax", "nid"), [K(0, pos=(480, 440), clip="back")]),
           spr("frere", story("pyrrhax", "frere"), [K(0, pos=(300, 444), clip="watch"), K(1.2, clip="cheer")], shadow=shade(120)),
           spr("soeur", story("pyrrhax", "soeur"), [K(0, pos=(660, 448), clip="watch", flip=True), K(1.3, clip="cheer")], shadow=shade(120)),
           glow("lueur_nid", "#ff8a3a", 240, [K(0, pos=(480, 440), alpha=0.4), K(1.1, alpha=0.7)], pulse=(0.3, 1.2)),
           spr("oeuf", story("pyrrhax", "oeuf"), [K(0, pos=(480, 452), clip="crack", scale=1.25), K(1.0, clip="hatch")]),
           spr("nid_devant", story("pyrrhax", "nid"), [K(0, pos=(480, 440), clip="front")]),
           particles("braises", "embers", "#ffb066", (260, 200, 440, 260), [K(0, alpha=1.0)])]
    cam = [K(0, pos=(480, 380), zoom=1.5), K(D, pos=(480, 360), zoom=1.3)]
    return dict(backdrop="#0a0414", camera=cam, elements=els)


def shot_coop(D):
    els, fg = planet("aethra", 1.2, ground=story("monolithe", "sol"), ground_tint="#e8d8ff", fg="fg_petals")
    els += [glow("ciel", "#ffc8f0", 300, [K(0, pos=(480, 160), alpha=0.35)], depth=0.2, pulse=(0.3, 2.4)),
            nova_jump(0.5, 260, tint="#ffb0d0", id_="nova2", end=D),
            nova_jump(0.9, 420, end=D),
            nova_jump(1.3, 580, tint="#a0e8ff", id_="nova3", end=D),
            particles("petales", "ash", "#ffc8f0", (0, 200, 960, 300), [K(0, alpha=0.9)]),
            particles("eclats", "sparks", "#ffffff", (200, 260, 560, 160), [K(0, alpha=0.6)])]
    els.append(fg)
    cam = [K(0, pos=(420, 330), zoom=1.3), K(D, pos=(460, 320), zoom=1.2)]
    return dict(backdrop="#140a1e", camera=cam, elements=els)


def shot_online(D):
    els, fg = planet("noyau", 0.0, ground=story("titan", "sol"), ground_tint="#d0c8ff", fg="fg_motes")
    els += [glow("gloire", "#ffd27a", 220, [K(0, pos=(480, 340), alpha=0.3), K(0.6, alpha=0.8, ease="out")], pulse=(0.25, 0.8)),
            spr("nova", NOVA, [K(0, pos=(480, 436), clip="victory", scale=2.0)], shadow=shade(52, alpha=0.4)),
            particles("confettis", "sparks", "#ffd27a", (200, 120, 560, 300), [K(0, alpha=1.0)]),
            particles("etoiles", "stars", "#ffffff", (0, 0, 960, 300), [K(0, alpha=0.8)]),
            particles("braises", "embers", "#c8b8ff", (200, 200, 560, 260), [K(0, alpha=0.8)])]
    els.append(fg)
    cam = [K(0, pos=(480, 360), zoom=1.6), K(D, pos=(480, 340), zoom=1.3)]
    return dict(backdrop="#08061a", camera=cam, elements=els, flash=[{"t": 0.1, "in": 0.05, "out": 0.5, "color": "#fff2c0", "alpha": 0.5}])


# =================================================================== the trailer
def shots():
    out = [
        ("system", "UN SYSTÈME À EXPLORER", "Sylvanox · Pyralis · Glacior · Noyau stellaire · Aethra · Les Confins", 3.4, (0.5, 0.78), "#ffd27a", shot_system),
        ("run", "COURIR, SAUTER, GLISSER", "", 3.2, (0.5, 0.2), "#c8ff9a", shot_run),
        ("powers", "19 POUVOIRS À DÉBLOQUER", "", 2.8, (0.5, 0.2), "#9adcff", shot_powers),
        ("enemies", "DES ENNEMIS SUR CHAQUE PLANÈTE", "", 2.6, (0.5, 0.2), "#c8f0ff", shot_enemies),
        ("giga", "7 GIGA-BOSS", "", 1.8, (0.5, 0.5), "#c88bff", shot_giga),
    ]
    for boss, layers, pos, scale, clip, backdrop, color, parts in bosses():
        fn = (lambda D, b=boss, l=layers, p=pos, s=scale, c=clip, bd=backdrop, col=color, pa=parts:
              boss_cut(b, D, l, p, s, c, bd, col, pa))
        out.append((f"boss_{boss}", boss_title(boss), story_title(boss), 1.7, (0.5, 0.82), color, fn))
    out += [
        ("stories", "7 HISTOIRES ANIMÉES", "avant le premier combat de chaque giga-boss", 3.0, (0.5, 0.2), "#ff8a3a", shot_stories),
        ("coop", "JUSQU'À 3 EN COOPÉRATION", "même PC, réseau local ou en ligne", 3.0, (0.5, 0.2), "#ffc8f0", shot_coop),
        ("online", "DÉFIS, CLASSEMENTS, CLANS", "Cosmic Online", 2.6, (0.5, 0.2), "#ffd27a", shot_online),
    ]
    return out


def sounds(scene, D):
    """The trailer's sounds (2.63): the game's own noises on its cuts — Nova's jumps and shots, the
    giga bosses' roars, impacts — over its music (which lowers a little under the big ones)."""
    from story_stage import amb, sfx
    roars = {"monolithe": ("boss_roar", 1.0), "kraken": ("boss_roar", 0.7), "phenix": ("boss_roar_fire", 1.3),
             "liche": ("summon", 1.0), "titan": ("boss_roar", 0.8), "pyrrhax": ("boss_roar_fire", 1.0), "singularis": ("star_roar", 1.0)}
    if scene.startswith("boss_"):
        name, pitch = roars[scene[5:]]
        return [sfx(name, 0.03, 0.75, pitch=pitch, duck=True)]
    table = {
        "system": [amb("story_amb_space", 0.3), sfx("story_glow", 0.3, 0.35)],
        "run": [sfx("jump", 1.0, 0.45), sfx("double_jump", 1.34, 0.45), sfx("land", 2.12, 0.4)],
        "powers": [sfx("dash", 0.0, 0.45), sfx("charge_shot", 0.57, 0.55), sfx("enemy_hurt", 1.25, 0.45),
                   sfx("explosion", 1.3, 0.5, duck=True)],
        "enemies": [sfx("glide_open", 0.05, 0.4), sfx("land", 1.72, 0.4), sfx("slash", 1.9, 0.45)],
        "giga": [sfx("supernova", 0.05, 0.7, duck=True)],
        "stories": [sfx("story_egg_crack", 1.0, 0.6)],
        "coop": [sfx("jump", t, 0.35, pan=p) for t, p in ((0.5, -0.4), (0.9, 0.0), (1.3, 0.4))],
        "online": [sfx("medal", 0.1, 0.6)],
    }
    return table.get(scene, [])


def build():
    data = {
        "format": "cosmic.boss_story",
        "version": 2,
        "kind": "trailer",
        "comment": ("The game's trailer (2.63), played by the stories' scene (BossStoryScene) from the main menu's attract mode: "
                    "each shot has its own length, its line is a title drawn large (where its focus says), its subtitle under it; "
                    "the last card is the game's name with this file's title. Written by tools/assetgen/environment/trailer.py "
                    "(edit it there)."),
        "boss": "trailer",
        "title": "Au clavier, à la manette ou à la souris",
        "color": "#39f0d8",
        "accent": "#ffd27a",
        "music": MUSIC,
        "titleCard": CARD,
        "shots": [],
    }
    for scene, text, sub, D, focus, col, fn in shots():
        st = fn(D)
        st["sfx"] = sounds(scene, D)
        stage = {"size": [960, 540]}
        stage.update(st)
        shot = {"scene": scene, "image": "", "text": text}
        if sub:
            shot["subtitle"] = sub
        shot.update({"duration": D, "focus": [focus[0], focus[1]], "zoom": 1.0, "particles": "none", "glow": col, "stage": stage})
        data["shots"].append(shot)
    return data


def write(data):
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
        f.write("\n")
    return OUT


def check(data):
    """Every file the trailer names is there, its clips in their sheets, its times within its shots."""
    errs = []
    for n, shot in enumerate(data["shots"], 1):
        D = shot["duration"]
        st = shot["stage"]
        times = [k["t"] for k in st["camera"]] + [f["t"] for f in st.get("flash", [])]
        times += [s["to"] for s in st.get("shake", [])]
        for el in st["elements"]:
            times += [k["t"] for k in el["keys"]]
            if "layer" in el and not os.path.isfile(os.path.join(S.ASSETS, el["layer"])):
                errs.append(f"shot {n}: no {el['layer']}")
            if "sprite" in el:
                p = os.path.join(S.ASSETS, el["sprite"])
                if not os.path.isfile(p):
                    errs.append(f"shot {n}: no {el['sprite']}")
                    continue
                clips = json.load(open(p, encoding="utf-8"))["clips"]
                for k in el["keys"]:
                    if "clip" in k and k["clip"] not in clips:
                        errs.append(f"shot {n}: {el['id']}: no clip {k['clip']}")
        for fx in st.get("sfx", []):
            times.append(fx["t"])
            if not os.path.isfile(os.path.join(S.ASSETS, fx["sound"])):
                errs.append(f"shot {n}: no {fx['sound']}")
        bad = [t for t in times if not 0 <= t <= D + 1e-6]
        if bad:
            errs.append(f"shot {n}: times outside 0..{D}: {bad[:3]}")
    total = sum(s["duration"] for s in data["shots"]) + data["titleCard"]
    if not 35.0 <= total <= 45.0:
        errs.append(f"length {total:.1f} s")
    return errs


# =================================================================== the media (README)
def _font(name, px):
    from PIL import ImageFont
    return ImageFont.truetype(os.path.join(S.ASSETS, "fonts", name), max(6, int(px)))


def _fit(draw, text, name, px, width):
    while px > 6:
        f = _font(name, px)
        if draw.textlength(text, font=f) <= width:
            return f
        px -= 1
    return _font(name, 6)


def _text(img, text, font, cx, y, color, alpha, glow_col=None):
    from PIL import Image, ImageDraw, ImageFilter
    if alpha <= 0.01:
        return
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    w = d.textlength(text, font=font)
    x = cx - w / 2
    if glow_col:
        g = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).text((x, y), text, font=font, fill=glow_col + (int(160 * alpha),))
        layer = Image.alpha_composite(layer, g.filter(ImageFilter.GaussianBlur(max(2, font.size // 5))))
        d = ImageDraw.Draw(layer)
    d.text((x + max(1, font.size // 20), y + max(1, font.size // 20)), text, font=font, fill=(0, 0, 0, int(200 * alpha)))
    d.text((x, y), text, font=font, fill=color + (int(255 * alpha),))
    img.alpha_composite(layer)


def _hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def frame(data, t, W, Hh):
    """The trailer at time t as the game shows it in a W x Hh window (its letterbox bands)."""
    from PIL import Image, ImageDraw
    band = round(Hh * 0.13)
    ih = Hh - 2 * band
    out = Image.new("RGBA", (W, Hh), (0, 0, 0, 255))
    starts, acc = [], 0.0
    for s in data["shots"]:
        starts.append(acc)
        acc += s["duration"]
    card = acc
    cf = 0.18

    def shot_img(i, local):
        return S.render(data["shots"][i]["stage"], local, size=(W, ih)).convert("RGBA")

    if t < card:
        i = max(k for k in range(len(starts)) if starts[k] <= t)
        local = t - starts[i]
        im = shot_img(i, local)
        if i > 0 and local < cf:
            prev = shot_img(i - 1, t - starts[i - 1])
            im = Image.blend(prev, im, (local / cf) ** 2 * (3 - 2 * local / cf))
        out.paste(im, (0, band))
        s = data["shots"][i]
        d = ImageDraw.Draw(out)
        a_in = min(1.0, max(0.0, (local - 0.12) / 0.28))
        a_in = a_in * a_in * (3 - 2 * a_in)
        a = a_in * min(1.0, max(0.0, (s["duration"] - local) / 0.22))
        pop = 1.0 + 0.14 * (1 - a_in)
        tf = _fit(d, s["text"], "Orbitron.ttf", ih * 0.11 * pop, W * 0.86)
        sf = _fit(d, s["subtitle"], "ChakraPetch-Regular.ttf", ih * 0.05, W * 0.86) if s.get("subtitle") else None
        hgt = tf.size + ((ih * 0.02 + sf.size) if sf else 0)
        y = band + ih * s["focus"][1] - hgt / 2
        y = min(max(y, band + 4), band + ih - hgt - 4)
        _text(out, s["text"], tf, W / 2, y, (255, 255, 255), a, _hexrgb(s["glow"]))
        if sf:
            _text(out, s["subtitle"], sf, W / 2, y + tf.size + ih * 0.02, tuple(int(c * 0.45 + 255 * 0.55) for c in _hexrgb(s["glow"])), a)
    else:
        u = t - card
        im = Image.new("RGBA", (W, ih), (4, 6, 14, 255))
        g = np.zeros((ih, W, 4), np.uint8)
        yy, xx = np.mgrid[0:ih, 0:W]
        dd = np.clip(1 - np.hypot((xx - W / 2) / (W * 0.5), (yy - ih / 2) / (ih * 0.6)), 0, 1)
        g[..., 0], g[..., 1], g[..., 2], g[..., 3] = 57, 240, 216, (dd * dd * 70).astype(np.uint8)
        im.alpha_composite(Image.fromarray(g, "RGBA"))
        rng = np.random.default_rng(5)
        dr = ImageDraw.Draw(im)
        for _ in range(60):
            x, y0 = rng.uniform(0, W), rng.uniform(0, ih)
            tw = 0.5 + 0.5 * math.sin(u * rng.uniform(1, 3) + rng.uniform(0, 6))
            c = int(120 + 135 * tw)
            dr.point((x, y0), fill=(c, c, c, 255))
        out.paste(im, (0, band))
        ap = min(1.0, u / 0.5)
        ap = ap * ap * (3 - 2 * ap)
        size = min(ih * 0.3, W * 0.13) * (1 + 0.08 * (1 - ap))
        d = ImageDraw.Draw(out)
        f1 = _fit(d, "COSMIC", "Orbitron.ttf", size, W * 0.8)
        f2 = _fit(d, "PLATFORMER", "Orbitron.ttf", size * 0.62, W * 0.8)
        cy = band + ih * 0.44
        _text(out, "COSMIC", f1, W / 2, cy - size * 0.55 - f1.size / 2, (255, 255, 255), ap, (57, 240, 216))
        _text(out, "PLATFORMER", f2, W / 2, cy + size * 0.45 - f2.size / 2, (57, 240, 216), ap, (57, 240, 216))
        f3 = _fit(d, data["title"], "ChakraPetch-Regular.ttf", ih * 0.06, W * 0.8)
        _text(out, data["title"], f3, W / 2, cy + size * 0.95, (232, 236, 246), min(1.0, max(0.0, (u - 0.5) / 0.5)))
    return out.convert("RGB")


def media(data, out_dir, gif_w=640, gif_fps=8, mp4=True):
    """docs/media/trailer.gif (gif_w wide, under 8 MiB) and, with ffmpeg, trailer.mp4 (960x540, 30 i/s)."""
    total = sum(s["duration"] for s in data["shots"]) + data["titleCard"]
    os.makedirs(out_dir, exist_ok=True)
    ff = shutil.which("ffmpeg")
    written = []
    with tempfile.TemporaryDirectory() as tmp:
        gh = gif_w * 9 // 16
        n = int(total * gif_fps)
        for i in range(n):
            frame(data, i / gif_fps, gif_w, gh).save(os.path.join(tmp, f"g{i:04d}.png"))
        gif = os.path.join(out_dir, "trailer.gif")
        if ff:  # fewer colours until it is under 8 MiB (the README's limit)
            pal = os.path.join(tmp, "pal.png")
            for colors, dither in ((96, "bayer:bayer_scale=5"), (64, "bayer:bayer_scale=5"), (48, "none"), (32, "none")):
                subprocess.run([ff, "-v", "error", "-y", "-framerate", str(gif_fps), "-i", os.path.join(tmp, "g%04d.png"),
                                "-vf", f"palettegen=max_colors={colors}:stats_mode=diff", pal], check=True)
                subprocess.run([ff, "-v", "error", "-y", "-framerate", str(gif_fps), "-i", os.path.join(tmp, "g%04d.png"), "-i", pal,
                                "-lavfi", f"paletteuse=dither={dither}:diff_mode=rectangle", "-loop", "0", gif], check=True)
                if os.path.getsize(gif) <= 8 * 1024 * 1024:
                    break
        else:
            from PIL import Image
            frames = [Image.open(os.path.join(tmp, f"g{i:04d}.png")).convert("P", palette=Image.ADAPTIVE, colors=128) for i in range(n)]
            frames[0].save(gif, save_all=True, append_images=frames[1:], duration=int(1000 / gif_fps), loop=0, optimize=True)
        written.append(gif)
        if ff and mp4:
            fps = 30
            for i in range(int(total * fps)):
                frame(data, i / fps, 960, 540).save(os.path.join(tmp, f"m{i:04d}.png"))
            mp = os.path.join(out_dir, "trailer.mp4")
            music = os.path.join(S.ASSETS, MUSIC)
            cmd = [ff, "-v", "error", "-y", "-framerate", str(fps), "-i", os.path.join(tmp, "m%04d.png")]
            if os.path.isfile(music):
                cmd += ["-i", music, "-shortest", "-c:a", "aac", "-b:a", "128k", "-af", f"afade=t=out:st={total - 1.5:.2f}:d=1.5"]
            cmd += ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "24", "-preset", "slow", "-movflags", "+faststart",
                    "-t", f"{total:.2f}", mp]
            subprocess.run(cmd, check=True)
            written.append(mp)
    return written


def main(argv):
    data = build()
    errs = check(data)
    for e in errs:
        print("FAIL", e)
    if errs:
        return 1
    print(write(data), f"{sum(s['duration'] for s in data['shots']) + data['titleCard']:.1f} s")
    if "--media" in argv:
        out_dir = argv[argv.index("--media") + 1]
        for p in media(data, out_dir):
            print(p, os.path.getsize(p) // 1024, "KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
