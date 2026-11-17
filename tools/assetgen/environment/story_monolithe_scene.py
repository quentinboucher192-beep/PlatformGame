"""The Monolithe's story as six animated shots (« Le Golem Primordial », version 2 of the format, 2.63).

Like Pyrrhax's (story_pyrrhax_scene.py), every shot is a little scene: layers of scenery drawn as
animated sheets (LAYERS, story_scenery.py), sprites playing their clips frame by frame
(characters/story_monolithe_sprites.py, the boss's own sheet, Nova's), glows and live particles,
moved by keyframes; a camera pans and zooms, the screen shakes or flashes.  This module writes the
scenes into data/stories/monolithe.json (the lines of narration are kept as written there) and
renders each shot's still (stories/monolithe_<n>.png).

  1. dust_pebble  the dust of the beginning, a pebble tumbling in it
  2. rune         two children find it in the cave, one carves a rune
  3. rising       generations carve theirs, the stone grows… and stands up
  4. village      the golem watching over the cave village, the children running to it
  5. ships        the people leave for the stars, the golem stays
  6. alone        the village in ruins, a visitor comes in: the golem roars

Deterministic (fixed seeds)."""
import numpy as np

import story_stage as S
from story_scenery import clouds, cave_wall, dust_band, floor, front_rocks, mist, peaks, sky, space, starfield
from story_stage import K, glow, particles, shade

BOSS = "animations/monolithe.json"
NOVA = "animations/nova.json"
L = S.Kit("monolithe")

COSMOS = ["#050308", "#0c0810", "#16101a", "#221824", "#30222c", "#443030", "#5a4236"]
CAVE = ["#0a0806", "#140f0b", "#1e1711", "#2a2017", "#382b1f", "#483828", "#5a4632"]
NIGHT = ["#060812", "#0a1020", "#101a34", "#18284a", "#24385e", "#344a72", "#4a5e86"]
AMBER = ("#fff2c0", "#ffb347", "#a0581c")
MOSS = ("#77953f", "#506f2f")

LAYERS = {
    # name: (frame w, frame h, frames, fps, frame function (k, n) -> RGBA)
    "cosmos": (560, 300, 6, 3, lambda k, n: space(560, 300, COSMOS, 51, k, n)),
    "etoiles": (560, 210, 6, 4, lambda k, n: starfield(560, 210, 380, 52, k, n,
                                                       ("#ffffff", "#fff4e0", "#ffd8b0", "#b09080", "#6a5a58"))),
    "poussiere": (640, 80, 4, 3, lambda k, n: dust_band(640, 80, 53, k, n, "#3a2c26", "#9a7a5a", alpha=150)),
    "grotte_fond": (560, 300, 4, 4, lambda k, n: cave_wall(560, 300, 54, k, n, CAVE, crystals=26,
                                                           crystal_cols=("#fff6d8", "#ffd27a", "#e09a3a", "#7a4a20"), drips=4)),
    "stalactites": (760, 110, 4, 4, lambda k, n: peaks(760, 110, 55, k, n, 16, 24, 74, 96, "#2a2018", "#6a5038", "#140e0a",
                                                       lean=0.04, wfac=(0.26, 0.42), lights=24, light_cols=AMBER,
                                                       glints="#9ab8c8", flip=True)),
    "piliers": (800, 160, 4, 5, lambda k, n: peaks(800, 160, 56, k, n, 14, 50, 150, 146, "#241a12", "#7a5a3a", "#120c08",
                                                   lean=0.06, wfac=(0.16, 0.3), lights=34, light_cols=AMBER, glints="#d8a860")),
    "brume": (640, 48, 4, 3, lambda k, n: mist(640, 48, 57, k, n, col="#a89880", amount=0.42)),
    "sol": (760, 110, 4, 5, lambda k, n: floor(760, 110, 58, k, n, 12, "#2c2218", "#7a6044", "#140e0a", rough=3.0, rocks=12,
                                               specks=70, speck_cols=AMBER, moss=MOSS, puddles=4, puddle_col="#5a7a8a")),
    "avant": (900, 120, 4, 4, lambda k, n: front_rocks(900, 120, 59, k, n, "#120e0a", "#4a3a2a", "#060403", spark="#ffb347")),
    "ciel": (560, 300, 6, 3, lambda k, n: sky(560, 300, NIGHT, 60, k, n)),
    "montagnes": (720, 130, 4, 4, lambda k, n: peaks(720, 130, 61, k, n, 22, 40, 118, 116, "#1a2238", "#4a5a7a", "#0e1220",
                                                     lean=0.06, wfac=(0.3, 0.5), lights=8, light_cols=("#ffe0a0", "#c89a5a", "#5a4a3a"),
                                                     glints="#7a8aaa")),
    "nuages": (640, 80, 4, 3, lambda k, n: clouds(640, 80, 62, k, n, body="#141c34", lit="#3a4a72", alpha=160)),
}


def _sprites():
    S.chars_path()
    import story_monolithe_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of scenery
def cave(seed, torches=((236, 300), (742, 296)), tint=None, mist_alpha=0.8):
    """The cave: its back wall (crystals breathing, water running), far and near stalactites, pillars
    with their crystals, mist drifting, dust falling, torches on the pillars, the ground."""
    els = [L.layer("fond", "grotte_fond", 0.03, (-80, -30), tint=tint),
           L.layer("stalactites_loin", "stalactites", 0.15, (-200, -110), tint=tint or "#6a6058", offset=0.4),
           L.layer("piliers", "piliers", 0.45, (-320, 150), tint=tint),
           L.layer("brume", "brume", 0.4, (-160, 330), drift=(10, 0), repeat=True, alpha=mist_alpha),
           particles("poussiere_voute", "dustfall", "#c8b090", (0, 40, 960, 380), [K(0, alpha=0.7)], depth=0.5)]
    for i, (x, y) in enumerate(torches):
        els.append(L.sprite(f"torche{i + 1}", "torche", [K(0, pos=(x, y), clip="idle", offset=0.17 * i)], depth=0.45))
        els.append(glow(f"torche{i + 1}_lueur", "#ffa040", 120, [K(0, pos=(x, y - 34), alpha=0.6)], depth=0.45,
                        pulse=(0.35, 0.5 + 0.13 * i, 0.3 * i)))
    els.append(L.layer("stalactites", "stalactites", 0.6, (-300, -40), offset=0.2, tint=tint))
    els.append(L.layer("sol", "sol", 1.0, (-280, 400), tint=tint))
    return els


def front(tint=None):
    return [L.layer("avant", "avant", 1.35, (-420, 316), tint=tint)]


def eye(id_, pos, flip, scale, keys_alpha):
    """The golem's eye, glowing (its point at rest: (+44, -101) px from its feet)."""
    x = pos[0] + (-44 if flip else 44) * S.PIX * scale
    y = pos[1] - 101 * S.PIX * scale
    return glow(id_, "#ffd27a", 34 * scale, [dict(k, pos=[round(x, 1), round(y, 1)]) if i == 0 else k
                                             for i, k in enumerate(keys_alpha)], pulse=(0.3, 1.3))


# =================================================================== the six shots
def shot_dust(D):
    """Avant les planètes, il n'y avait que de la poussière. Et dans cette poussière, un caillou."""
    rng = np.random.default_rng(101)
    els = [L.layer("cosmos", "cosmos", 0.03, (-80, -30)),
           L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True)]
    els += L.twinkles(101, 8, (40, 20, 880, 420))
    els += [glow("soleil_lointain", "#ffe0b0", 280, [K(0, pos=(840, 96), alpha=0.5)], depth=0.06, pulse=(0.3, 3.2)),
            L.sprite("soleil", "astre", [K(0, pos=(840, 96), clip="twinkle", scale=3.0)], depth=0.06),
            L.layer("poussiere_loin", "poussiere", 0.15, (-200, 40), drift=(-6, 0), repeat=True, alpha=0.75, offset=0.3),
            L.layer("poussiere_bas", "poussiere", 0.22, (-300, 360), drift=(9, 0), repeat=True, alpha=0.8, offset=0.6)]
    for i in range(12):  # grains drifting at their depths, the far ones darker
        depth = float(rng.choice([0.3, 0.45, 0.6, 0.75]))
        p0 = (rng.uniform(60, 900), rng.uniform(80, 470))
        v = (rng.uniform(-14, 14), rng.uniform(-6, 6))
        els.append(L.sprite(f"grain{i + 1}", "grain", [K(0, pos=p0, clip="spin", offset=float(rng.uniform(0, 1)),
                                                          scale=round(0.5 + depth, 2)), K(D, pos=(p0[0] + v[0] * D, p0[1] + v[1] * D), ease="linear")],
                            depth=depth, tint="#a09088" if depth < 0.5 else None))
    els.append(L.layer("poussiere", "poussiere", 0.5, (-300, 220), drift=(14, 0), repeat=True, alpha=0.55))
    peb = (500, 292)
    els += [glow("caillou_lueur", "#ffcf9a", 90, [K(0, pos=(peb[0] + 10, peb[1] - 8), alpha=0.3),
                                                  K(D, pos=(peb[0] + 32, peb[1] - 16), alpha=0.45)], pulse=(0.35, 2.4)),
            L.sprite("caillou", "caillou", [K(0, pos=peb, clip="spin", scale=1.4), K(D, pos=(peb[0] + 22, peb[1] - 8), ease="linear")]),
            particles("poussiere_vive", "dust", "#e8d2a6", (0, 80, 960, 400), [K(0, alpha=0.9)]),
            particles("etoiles_vives", "stars", "#fff0d8", (0, 0, 960, 300), [K(0, alpha=0.5)], depth=0.1)]
    for i in range(3):  # grains passing close by, in front
        y = 140 + 150 * i
        t0 = 0.6 + 1.7 * i
        els.append(L.sprite(f"grain_proche{i + 1}", "grain", [K(0, pos=(-60, y), clip="spin", scale=2.2, offset=0.3 * i),
                                                                K(t0, pos=(-60, y)), K(min(D, t0 + 3.2), pos=(1040, y - 40), ease="linear")],
                            depth=1.3))
    cam = [K(0, pos=(480, 270), zoom=1.04), K(2.4, pos=(488, 278), zoom=1.12), K(D, pos=(518, 286), zoom=1.62)]
    return dict(backdrop="#050308", camera=cam, elements=els, still=5.4)


def shot_rune(D):
    """Les premiers enfants des Confins le trouvèrent dans une caverne et y gravèrent une rune, pour jouer : « Protège-nous »."""
    els = cave(201)
    rock = (566, 446)
    RS = 0.6
    rune = (rock[0] - 4.8 * S.PIX * RS, rock[1] - 33.2 * S.PIX * RS)
    t_carve = 3.0
    els += [
        L.sprite("rocher", "rocher", [K(0, pos=rock, clip="idle", scale=RS), K(t_carve, clip="carve")], shadow=shade(130, (236, 300))),
        glow("rune_lueur", "#ffb347", 44, [K(0, pos=rune, alpha=0.0), K(t_carve + 0.3, alpha=0.0), K(t_carve + 2.1, alpha=0.5, ease="out"),
                                           K(D, alpha=0.55)], pulse=(0.3, 1.1)),
        particles("eclats", "sparks", "#ffc870", (rune[0] - 40, rune[1] - 30, 80, 60),
                  [K(0, alpha=0.0), K(t_carve, alpha=0.0), K(t_carve + 0.2, alpha=1.0), K(t_carve + 2.0, alpha=1.0), K(t_carve + 2.4, alpha=0.3)]),
        L.sprite("enfant2", "enfant2", [K(0, pos=(120, 452), clip="walk"), K(1.0, pos=(120, 452)), K(3.4, pos=(404, 452), ease="linear"),
                                        K(3.41, clip="idle", ease="step"), K(5.4, clip="look_up"), K(6.5, clip="cheer"), K(7.6, clip="wave")],
                 shadow=shade(56, (742, 296))),
        glow("torche_enfant", "#ffa040", 110, [K(0, pos=(132, 380), alpha=0.6), K(1.0, pos=(132, 380)), K(3.4, pos=(416, 380), ease="linear")],
             pulse=(0.3, 0.45)),
        L.sprite("enfant1", "enfant1", [K(0, pos=(200, 448), clip="walk"), K(0.4, pos=(200, 448)), K(2.6, pos=(488, 448), ease="linear"),
                                        K(2.61, clip="idle", ease="step"), K(2.8, clip="carve"), K(5.3, clip="idle"), K(6.3, clip="cheer"),
                                        K(7.5, clip="idle")], shadow=shade(56, (236, 300))),
        particles("poussiere", "dust", "#e8c890", (120, 260, 720, 200), [K(0, alpha=0.6)]),
    ]
    els += front()
    cam = [K(0, pos=(420, 330), zoom=1.2), K(2.0, pos=(462, 348), zoom=1.3), K(3.4, pos=(520, 384), zoom=1.62),
           K(6.2, pos=(528, 384), zoom=1.7), K(D, pos=(500, 372), zoom=1.48)]
    return dict(backdrop="#0a0806", camera=cam, elements=els, still=6.6)


def shot_rising(D):
    """Chaque génération ajouta la sienne. Rune après rune, le caillou grandit… et se leva."""
    els = cave(301)
    rock = (500, 446)
    t_grow, t_wake, t_up = 0.2, 3.9, 4.5
    els += [
        glow("runes_lueur", "#ffb347", 90, [K(0, pos=(rock[0], rock[1] - 40), alpha=0.25), K(t_wake, pos=(rock[0], rock[1] - 84),
                                                                                            alpha=0.6, scale=2.0),
                                            K(t_up, alpha=0.9), K(t_up + 0.3, alpha=0.0)], pulse=(0.25, 0.9)),
        L.sprite("ancien", "ancien", [K(0, pos=(436, 448), clip="carve"), K(1.5, clip="walk", flip=True), K(1.6, pos=(436, 448)),
                                      K(2.9, pos=(318, 448), ease="linear"), K(2.91, clip="idle", flip=False, ease="step"),
                                      K(t_up + 0.1, clip="look_up")], shadow=shade(66, (236, 300))),
        L.sprite("villageois", "villageois", [K(0, pos=(690, 446), clip="walk", flip=True), K(0.9, pos=(586, 446), ease="linear"),
                                              K(0.91, clip="carve", ease="step"), K(2.5, clip="walk", flip=False), K(2.6, pos=(586, 446)),
                                              K(3.7, pos=(720, 446), ease="linear"), K(3.71, clip="idle", flip=True, ease="step"),
                                              K(t_up + 0.2, clip="look_up")], shadow=shade(66, (742, 296))),
        L.sprite("villageoise", "villageoise", [K(0, pos=(250, 450), clip="kneel"), K(t_up, clip="look_up")], shadow=shade(66, (236, 300))),
        L.sprite("enfant3", "enfant3", [K(0, pos=(150, 454), clip="run"), K(0.5, pos=(150, 454)), K(2.2, pos=(360, 454), ease="linear"),
                                        K(2.21, clip="cheer", ease="step"), K(3.4, clip="idle"), K(t_up + 0.15, clip="look_up")],
                 shadow=shade(50, (236, 300))),
        L.sprite("rocher", "rocher", [K(0, pos=rock, clip="grow", scale=0.62), K(t_grow, scale=0.62),
                                      K(t_wake, scale=1.24, clip="wake"), K(t_up, alpha=1.0), K(t_up + 0.02, alpha=0.0, ease="step")],
                 shadow=shade(140, (236, 300))),
        L.sprite("golem", BOSS, [K(0, pos=rock, alpha=0.0, scale=0.85, clip="uncurl"), K(t_up, alpha=0.0, ease="step"),
                                 K(t_up + 0.02, alpha=1.0, clip="uncurl", ease="step"), K(t_up + 0.4, clip="idle")],
                 shadow=shade(220, (236, 300), alpha=0.5)),
        eye("oeil", rock, False, 0.85, [K(0, alpha=0.0), K(t_up + 0.3, alpha=0.0), K(t_up + 0.8, alpha=0.9, ease="out"), K(D, alpha=0.8)]),
        L.sprite("nuage1", "nuage", [K(0, pos=(rock[0] - 70, rock[1] + 4), alpha=0.0, scale=1.3), K(t_up, alpha=0.0, ease="step"),
                                     K(t_up + 0.02, alpha=1.0, clip="puff", ease="step")]),
        L.sprite("nuage2", "nuage", [K(0, pos=(rock[0] + 70, rock[1] + 6), alpha=0.0, scale=1.3, flip=True), K(t_up, alpha=0.0, ease="step"),
                                     K(t_up + 0.02, alpha=1.0, clip="puff", ease="step")]),
        particles("eclats", "sparks", "#ffc870", (380, 300, 240, 140), [K(0, alpha=0.6), K(t_wake, alpha=1.0), K(t_up + 0.6, alpha=0.2)]),
        particles("poussiere", "dust", "#e8c890", (100, 240, 760, 220), [K(0, alpha=0.5), K(t_up, alpha=1.0)]),
    ]
    els += front()
    cam = [K(0, pos=(470, 372), zoom=1.42), K(t_wake, pos=(490, 334), zoom=1.2), K(t_up, pos=(500, 304), zoom=1.08),
           K(D, pos=(500, 292), zoom=1.04)]
    shake = [{"from": t_wake, "to": t_up, "amount": 2.0}, {"from": t_up, "to": t_up + 0.7, "amount": 6.0}]
    flash = [{"t": t_up, "in": 0.08, "out": 0.6, "color": "#ffd8a0", "alpha": 0.7}]
    return dict(backdrop="#0a0806", camera=cam, elements=els, shake=shake, flash=flash, still=5.9)


def shot_village(D):
    """Il veilla sur le village de la caverne pendant dix mille ans. Il connaissait chaque rire."""
    els = cave(401, torches=((120, 296), (700, 300)))
    fire = (380, 458)
    gol = (790, 446)
    GS = 0.8
    els += [
        L.sprite("hutte_loin", "hutte", [K(0, pos=(560, 424), clip="idle", scale=0.7, offset=0.5)], depth=0.8, tint="#c0a890"),
        L.sprite("hutte1", "hutte", [K(0, pos=(196, 446), clip="idle", scale=0.95)], shadow=shade(150, fire)),
        L.sprite("hutte2", "hutte", [K(0, pos=(-40, 452), clip="idle", scale=1.0, offset=0.3, flip=True)], shadow=shade(150, fire)),
        glow("feu_lueur", "#ff9a3a", 230, [K(0, pos=(fire[0], fire[1] - 30), alpha=0.6)], pulse=(0.3, 0.6)),
        L.sprite("feu", "feu", [K(0, pos=fire, clip="idle", scale=1.1)]),
        L.sprite("villageoise", "villageoise", [K(0, pos=(320, 454), clip="kneel")], shadow=shade(62, fire)),
        L.sprite("villageois", "villageois", [K(0, pos=(40, 452), clip="walk"), K(3.6, pos=(270, 452), ease="linear"),
                                              K(3.61, clip="idle", ease="step"), K(4.8, clip="point")], shadow=shade(62, fire)),
        L.sprite("golem", BOSS, [K(0, pos=gol, clip="idle", flip=True, scale=GS), K(3.4, clip="crouch"), K(5.6, clip="idle", offset=0.2)],
                 shadow=shade(210, fire, alpha=0.45)),
        eye("oeil", gol, True, GS, [K(0, alpha=0.7), K(3.4, alpha=0.7), K(3.9, alpha=1.0), K(D, alpha=0.9)]),
        glow("rune_golem", "#ffb347", 40, [K(0, pos=(gol[0] - 14, gol[1] - 120), alpha=0.3)], pulse=(0.4, 1.6)),
        L.sprite("enfant3", "enfant3", [K(0, pos=(230, 460), clip="run"), K(0.4, pos=(230, 460)), K(3.0, pos=(640, 458), ease="linear"),
                                        K(3.01, clip="cheer", ease="step"), K(4.3, clip="wave")], shadow=shade(50, fire)),
        L.sprite("enfant1", "enfant1", [K(0, pos=(110, 462), clip="walk"), K(1.0, pos=(110, 462)), K(1.01, clip="cheer", ease="step"),
                                        K(1.9, clip="walk"), K(4.6, pos=(560, 460), ease="linear"), K(4.61, clip="cheer", ease="step")],
                 shadow=shade(50, fire)),
        particles("braises", "embers", "#ffb066", (fire[0] - 60, 300, 120, 150), [K(0, alpha=0.9)]),
        particles("poussiere", "dust", "#e8c890", (60, 260, 840, 200), [K(0, alpha=0.5)]),
    ]
    els += front()
    cam = [K(0, pos=(320, 350), zoom=1.28), K(1.8, pos=(380, 352), zoom=1.24), K(D, pos=(600, 340), zoom=1.16)]
    return dict(backdrop="#0a0806", camera=cam, elements=els, still=4.6)


def shot_ships(D):
    """Puis les gens partirent vers les étoiles. Personne ne pensa à effacer la rune."""
    els = [L.layer("ciel", "ciel", 0.03, (-80, -30)),
           L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(5, 0), repeat=True)]
    els += L.twinkles(501, 8, (40, 10, 880, 240))
    els += [L.layer("nuages_loin", "nuages", 0.12, (-200, 120), drift=(-8, 0), repeat=True, alpha=0.75, offset=0.3),
            L.layer("montagnes", "montagnes", 0.25, (-240, 170)),
            L.layer("brume", "brume", 0.4, (-160, 330), drift=(12, 0), repeat=True, alpha=0.7)]
    launches = [(0.3, 560, 0.55), (1.3, 700, 0.62), (2.4, 470, 0.7)]
    for i, (t0, x, depth) in enumerate(launches):
        y0 = 420
        x1, y1 = x + 260, -160
        els += [
            L.sprite(f"nuage_depart{i + 1}", "nuage", [K(0, pos=(x, y0 + 14), alpha=0.0, scale=1.2), K(t0, alpha=0.0, ease="step"),
                                                       K(t0 + 0.02, alpha=0.9, clip="puff", ease="step")], depth=depth, tint="#a0a8c0"),
            glow(f"trainee{i + 1}", "#8ad0ff", 46, [K(0, pos=(x, y0 + 30), alpha=0.0), K(t0, alpha=0.0), K(t0 + 0.2, alpha=0.45),
                                                    K(min(D, t0 + 3.2), pos=(x1 - 40, y1 + 90), ease="in")], depth=depth),
            L.sprite(f"vaisseau{i + 1}", "vaisseau", [K(0, pos=(x, y0), clip="idle", scale=0.9), K(t0, clip="launch", rot=0.0),
                                                      K(min(D, t0 + 3.2), pos=(x1, y1), rot=22.0, ease="in")], depth=depth),
        ]
    els += [L.layer("sol", "sol", 1.0, (-280, 400)),
            L.sprite("golem", BOSS, [K(0, pos=(250, 448), clip="idle", scale=0.82)], shadow=shade(210, (600, 200), alpha=0.4)),
            eye("oeil", (250, 448), False, 0.82, [K(0, alpha=0.8)]),
            glow("rune_golem", "#ffb347", 40, [K(0, pos=(262, 330), alpha=0.3)], pulse=(0.4, 1.5)),
            particles("etincelles", "embers", "#8ad0ff", (420, 300, 380, 140), [K(0, alpha=0.0), K(0.3, alpha=0.8), K(D, alpha=0.6)], depth=0.6),
            particles("etoiles_vives", "stars", "#d8e8ff", (0, 0, 960, 260), [K(0, alpha=0.7)], depth=0.1),
            particles("poussiere", "dust", "#c8d0e0", (60, 320, 840, 140), [K(0, alpha=0.4)])]
    els += front()
    cam = [K(0, pos=(380, 360), zoom=1.24), K(1.4, pos=(430, 322), zoom=1.16), K(D, pos=(560, 230), zoom=1.12)]
    return dict(backdrop="#060812", camera=cam, elements=els, still=2.9)


def shot_alone(D):
    """Le Monolithe garde toujours le cœur des Confins. Il ne reconnaît plus personne : tout visiteur est un étranger."""
    dark = "#8a8aa8"
    els = cave(601, torches=(), tint=dark, mist_alpha=0.9)
    gol = (600, 446)
    GS = 0.9
    t_in, t_stop, t_roar = 1.4, 3.9, 5.0
    els += [
        L.sprite("ruine1", "hutte", [K(0, pos=(330, 440), clip="ruin", scale=0.85)], depth=0.85, tint=dark),
        L.sprite("ruine2", "hutte", [K(0, pos=(850, 452), clip="ruin", scale=1.0, flip=True, offset=0.4)], tint=dark,
                 shadow=shade(150, (600, 300))),
        glow("rune_golem", "#ffb347", 50, [K(0, pos=(gol[0] - 16, gol[1] - 130), alpha=0.3), K(t_roar, alpha=0.3), K(t_roar + 0.3, alpha=0.7),
                                           K(D, alpha=0.5)], pulse=(0.35, 1.8)),
        L.sprite("golem", BOSS, [K(0, pos=gol, clip="idle", flip=True, scale=GS), K(t_roar, clip="roar")],
                 shadow=shade(220, (600, 200), alpha=0.5)),
        eye("oeil", gol, True, GS, [K(0, alpha=0.6), K(t_roar, alpha=0.6), K(t_roar + 0.2, alpha=1.0, scale=1.8, ease="out"),
                                    K(D, alpha=0.9, scale=1.3)]),
        L.sprite("nova", NOVA, [K(0, pos=(-40, 446), clip="walk", scale=1.1), K(t_in, pos=(-40, 446)), K(t_stop, pos=(286, 446), ease="linear"),
                                K(t_stop + 0.01, clip="idle", ease="step"), K(t_roar + 0.2, clip="hurt"), K(t_roar + 0.6, clip="idle")],
                 shadow=shade(40, (600, 300))),
        L.sprite("nuage", "nuage", [K(0, pos=(gol[0], gol[1] + 6), alpha=0.0, scale=1.6), K(t_roar + 0.3, alpha=0.0, ease="step"),
                                    K(t_roar + 0.32, alpha=0.8, clip="puff", ease="step")], tint=dark),
        particles("chute", "dustfall", "#c8b090", (100, 60, 760, 360), [K(0, alpha=0.4), K(t_roar, alpha=0.4), K(t_roar + 0.4, alpha=1.0)]),
        particles("poussiere", "dust", "#b8b0c8", (60, 260, 840, 200), [K(0, alpha=0.6)]),
    ]
    els += front(tint=dark)
    cam = [K(0, pos=(610, 318), zoom=1.36), K(2.6, pos=(520, 330), zoom=1.16), K(t_stop, pos=(450, 340), zoom=1.08),
           K(t_roar + 0.2, pos=(480, 330), zoom=1.2), K(D, pos=(490, 326), zoom=1.26)]
    shake = [{"from": t_roar + 0.1, "to": min(D, t_roar + 1.2), "amount": 6.0}]
    flash = [{"t": t_roar + 0.2, "in": 0.08, "out": 0.6, "color": "#ffc070", "alpha": 0.35}]
    return dict(backdrop="#060508", camera=cam, elements=els, shake=shake, flash=flash, still=5.6)


SHOTS = [shot_dust, shot_rune, shot_rising, shot_village, shot_ships, shot_alone]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("monolithe", "story_monolithe_scene", SHOTS, _sprites(), LAYERS, review)
