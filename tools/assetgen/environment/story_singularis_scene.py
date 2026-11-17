"""Singularis's story as six animated shots (« Le Cœur effondré », version 2, 2.63).

Like the other stories told in animated scenes (story_stage.py): layers of space drawn as animated
sheets (LAYERS), sprites playing their clips frame by frame (characters/story_singularis_sprites.py,
the boss's own sheet), glows and live particles moved by keyframes, a camera, shakes and flashes.
It writes the scenes into data/stories/singularis.json (the lines kept as written) and each shot's
still.  Deep space: nothing casts a shadow there.

  1. young_star     Lyse, a very young star at the centre of Kepler-Voss
  2. three_planets  she sings light; three small planets come round her to warm themselves
  3. gifts          she gives them everything: sap to the forests, fire to the volcanoes, glitter to the ice
  4. machines       machines come to drink her light, a little more every day
  5. last_spark     one evening only a spark is left; cold, afraid of being alone in the dark
  6. collapse       she folds in on herself… and wakes up hungry: nothing will ever get away again

Deterministic (fixed seeds)."""
import math

import numpy as np

import story_stage as S
from story_scenery import dust_band, space, starfield
from story_stage import K, glow, particles

BOSS = "animations/singularis.json"
L = S.Kit("singularis")
TAU = 2 * math.pi

COSMOS = ["#04030a", "#0a0614", "#140a20", "#20102e", "#2e163c", "#3e1e48", "#52284e"]
C0 = (480, 270)

LAYERS = {
    "cosmos": (560, 300, 6, 3, lambda k, n: space(560, 300, COSMOS, 141, k, n)),
    "etoiles": (560, 300, 6, 4, lambda k, n: starfield(560, 300, 520, 142, k, n,
                                                       ("#ffffff", "#fff4e0", "#e8d0ff", "#a080c0", "#5a4a78"), fade=False)),
    "voile": (640, 80, 4, 3, lambda k, n: dust_band(640, 80, 143, k, n, "#2a1638", "#c88bff", alpha=140)),
    "poussiere": (640, 80, 4, 3, lambda k, n: dust_band(640, 80, 144, k, n, "#3a2a1a", "#ffd27a", alpha=120, thick=0.25)),
}


def _sprites():
    S.chars_path()
    import story_singularis_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of space
def sky(seed, tint=None, dust=1.0):
    """Space: the nebula turning, stars twinkling and drifting, veils of violet and golden dust
    drifting at their depths, grains of rock passing."""
    els = [L.layer("cosmos", "cosmos", 0.03, (-80, -30), tint=tint),
           L.layer("etoiles", "etoiles", 0.08, (-80, -30), drift=(3, 0), repeat=True, tint=tint)]
    els += L.twinkles(seed, 8, (20, 10, 920, 500))
    els += [L.layer("voile", "voile", 0.15, (-200, 60), drift=(-6, 0), repeat=True, alpha=0.7 * dust, tint=tint, offset=0.3),
            L.layer("poussiere", "poussiere", 0.3, (-300, 380), drift=(9, 0), repeat=True, alpha=0.6 * dust, tint=tint, offset=0.5)]
    rng = np.random.default_rng(seed)
    for i in range(5):
        depth = float(rng.choice([0.4, 0.55, 0.7]))
        p0 = (rng.uniform(40, 920), rng.uniform(60, 480))
        els.append(L.sprite(f"grain{i + 1}", "grain", [K(0, pos=p0, clip="spin", offset=float(rng.uniform(0, 1)), scale=round(0.5 + depth, 2))],
                            depth=depth, drift=(float(rng.uniform(-10, 10)), float(rng.uniform(-4, 4))), tint=tint or "#b0a0c0"))
    return els


def front_dust(tint=None, alpha=0.5):
    return [L.layer("voile_proche", "voile", 1.3, (-300, 420), drift=(18, 0), repeat=True, alpha=alpha, tint=tint)]


def orbit(D, center, rx, ry, phase, turns=0.25, n=9):
    """Keys along an ellipse (a planet going round)."""
    out = []
    for j in range(n):
        t = D * j / (n - 1)
        a = TAU * (phase + turns * j / (n - 1))
        out.append(K(t, pos=(center[0] + math.cos(a) * rx, center[1] + math.sin(a) * ry), ease="linear",
                     scale=round(0.95 + 0.2 * math.sin(a), 2)))
    return out


# =================================================================== the six shots
def shot_young_star(D):
    """Il y a très longtemps, au centre de Kepler-Voss, brillait une toute jeune étoile. Elle s'appelait Lyse."""
    els = sky(701)
    els += [glow("lyse_halo", "#ffe9a8", 300, [K(0, pos=C0, alpha=0.5)], depth=0.9, pulse=(0.25, 2.6)),
            glow("lyse_lueur", "#fff6d8", 120, [K(0, pos=C0, alpha=0.8)], depth=0.9, pulse=(0.3, 1.2, 0.4)),
            L.sprite("lyse", "lyse", [K(0, pos=C0, clip="idle", scale=1.3)], depth=0.9),
            particles("poussiere_vive", "dust", "#ffd27a", (0, 60, 960, 420), [K(0, alpha=0.7)]),
            particles("etoiles_vives", "stars", "#fff0d8", (0, 0, 960, 540), [K(0, alpha=0.5)], depth=0.1)]
    els += front_dust()
    cam = [K(0, pos=(480, 270), zoom=1.04), K(2.0, pos=(480, 270), zoom=1.1), K(D, pos=(482, 268), zoom=1.62)]
    return dict(backdrop="#04030a", camera=cam, elements=els, still=5.6)


def shot_three_planets(D):
    """Lyse chantait de la lumière, et trois petites planètes venaient se réchauffer autour d'elle."""
    els = sky(702)
    els += [glow("lyse_halo", "#ffe9a8", 320, [K(0, pos=C0, alpha=0.55)], depth=0.9, pulse=(0.25, 2.2)),
            L.sprite("lyse", "lyse", [K(0, pos=C0, clip="sing", scale=1.15)], depth=0.9)]
    for i, (kind, col) in enumerate((("foret", "#c8ff9a"), ("volcan", "#ffb070"), ("glace", "#e8ffff"))):
        keys = orbit(D, C0, 300, 100, 0.12 + i / 3)
        keys[0]["clip"] = "spin"
        keys[0]["offset"] = round(0.3 * i, 3)
        els.append(L.sprite(kind, kind, keys))
        gk = [dict(k) for k in keys]
        gk[0].pop("clip", None)
        gk[0].pop("offset", None)
        gk[0]["alpha"] = 0.35
        els.append(glow(f"{kind}_lueur", col, 50, gk, pulse=(0.3, 1.6 + 0.2 * i, 0.3 * i)))
    els += [particles("chant", "sparks", "#ffe9a8", (300, 120, 360, 300), [K(0, alpha=0.8)]),
            particles("poussiere_vive", "dust", "#ffd27a", (0, 60, 960, 420), [K(0, alpha=0.6)])]
    els += front_dust()
    cam = [K(0, pos=(480, 280), zoom=1.2), K(D, pos=(480, 270), zoom=1.08)]
    return dict(backdrop="#04030a", camera=cam, elements=els, still=2.4)


def shot_gifts(D):
    """Elle leur donnait tout : la sève des forêts, le feu des volcans, l'éclat des glaces."""
    els = sky(703)
    ly = (330, 270)
    els += [glow("lyse_halo", "#ffe9a8", 300, [K(0, pos=ly, alpha=0.55)], depth=0.9, pulse=(0.25, 2.2)),
            L.sprite("lyse", "lyse", [K(0, pos=ly, clip="sing", scale=1.15)], depth=0.9)]
    planets = [("foret", "seve", (640, 160), 0.6, "#a8f070"), ("volcan", "feu", (720, 300), 2.2, "#ffa040"),
               ("glace", "glace", (610, 430), 3.8, "#a8e8ff")]
    for i, (kind, gift, p, t0, col) in enumerate(planets):
        arrive = t0 + 1.0
        els.append(glow(f"{kind}_lueur", col, 70, [K(0, pos=p, alpha=0.3), K(arrive, alpha=0.3), K(arrive + 0.2, alpha=0.7, scale=1.5),
                                                   K(min(D, arrive + 1.2), alpha=0.45, scale=1.2)], pulse=(0.3, 1.4, 0.2 * i)))
        els.append(L.sprite(kind, kind, [K(0, pos=p, clip="spin", scale=1.3, offset=0.2 * i), K(arrive, clip="bloom"),
                                         K(D, pos=(p[0] + 10, p[1] - 6), ease="linear")]))
        for j in range(4):  # the gift: motes of light flowing to it
            ts = t0 + 0.12 * j
            mid = ((ly[0] + p[0]) / 2, (ly[1] + p[1]) / 2 - 50 + 20 * j)
            els.append(L.sprite(f"don_{kind}{j + 1}", "don", [K(0, pos=ly, alpha=0.0, clip=gift, scale=1.4), K(ts, alpha=0.0, ease="step"),
                                                              K(ts + 0.01, alpha=1.0, ease="step"), K(ts + 0.5, pos=mid, ease="out"),
                                                              K(ts + 1.0, pos=p, ease="in", scale=0.8), K(ts + 1.02, alpha=0.0, ease="step")],
                                blend="add"))
    els += [particles("chant", "sparks", "#ffe9a8", (220, 140, 560, 280), [K(0, alpha=0.8)]),
            particles("poussiere_vive", "dust", "#ffd27a", (0, 60, 960, 420), [K(0, alpha=0.6)])]
    els += front_dust()
    cam = [K(0, pos=(420, 270), zoom=1.24), K(2.2, pos=(480, 280), zoom=1.2), K(D, pos=(500, 290), zoom=1.16)]
    return dict(backdrop="#04030a", camera=cam, elements=els, still=5.2)


def shot_machines(D):
    """Puis des machines vinrent boire sa lumière. Un peu plus chaque jour."""
    els = sky(704)
    t_drink = 1.3
    els += [glow("lyse_halo", "#ffe9a8", 300, [K(0, pos=C0, alpha=0.55), K(t_drink, alpha=0.55), K(D, alpha=0.2, scale=0.7)], depth=0.9,
                 pulse=(0.25, 2.0)),
            glow("lyse_lueur", "#fff6d8", 110, [K(0, pos=C0, alpha=0.8), K(D, alpha=0.35, scale=0.7)], depth=0.9, pulse=(0.35, 0.9)),
            L.sprite("lyse", "lyse", [K(0, pos=C0, clip="idle", scale=1.2), K(t_drink + 1.2, clip="dim"), K(D, scale=0.95)], depth=0.9)]
    spots = [((760, 140), (620, 200)), ((800, 380), (640, 330)), ((180, 120), (330, 200))]
    for i, (p0, p1) in enumerate(spots):
        flip = p1[0] < C0[0]
        tin = 0.2 + 0.25 * i
        els.append(L.sprite(f"machine{i + 1}", "machine", [K(0, pos=(p0[0] + (200 if not flip else -200), p0[1]), clip="fly", flip=flip),
                                                           K(tin, pos=(p0[0] + (200 if not flip else -200), p0[1])),
                                                           K(t_drink - 0.05, pos=p1, ease="out"), K(t_drink, clip="drink")]))
        for j in range(5):  # gulps of light, from her to it
            ts = t_drink + 0.2 + 0.75 * j + 0.2 * i
            if ts + 0.6 > D:
                break
            els.append(L.sprite(f"gorgee{i + 1}_{j + 1}", "don", [K(0, pos=C0, alpha=0.0, clip="lumiere"), K(ts, alpha=0.0, ease="step"),
                                                                  K(ts + 0.01, alpha=1.0, ease="step"),
                                                                  K(ts + 0.6, pos=(p1[0] + (-30 if not flip else 30), p1[1]), ease="in"),
                                                                  K(ts + 0.62, alpha=0.0, ease="step")], blend="add"))
    els += [particles("lumiere", "sparks", "#ffe9a8", (360, 160, 240, 220), [K(0, alpha=0.8), K(D, alpha=0.3)]),
            particles("poussiere_vive", "dust", "#c8b8d8", (0, 60, 960, 420), [K(0, alpha=0.6)])]
    els += front_dust()
    cam = [K(0, pos=(480, 270), zoom=1.12), K(D, pos=(480, 270), zoom=1.3)]
    return dict(backdrop="#04030a", camera=cam, elements=els, still=3.6)


def shot_last_spark(D):
    """Un soir, il ne lui resta qu'une étincelle. Elle eut froid. Elle eut peur d'être seule dans le noir."""
    cold = "#7a7aa8"
    els = sky(705, tint=cold, dust=0.6)
    for i, (kind, p) in enumerate((("foret", (120, 140)), ("volcan", (860, 420)), ("glace", (820, 120)))):
        els.append(L.sprite(kind, kind, [K(0, pos=p, clip="spin", scale=0.6, offset=0.3 * i), K(D, pos=(p[0] + (p[0] - 480) * 0.1,
                                                                                                    p[1] + (p[1] - 270) * 0.1), ease="linear")],
                            depth=0.5, tint="#606080"))
    els += [glow("etincelle_lueur", "#d8d8ff", 50, [K(0, pos=C0, alpha=0.6)], depth=0.9, pulse=(0.6, 0.9)),
            glow("froid", "#5a6aa8", 260, [K(0, pos=C0, alpha=0.25)], depth=0.9, pulse=(0.3, 3.0)),
            L.sprite("lyse", "lyse", [K(0, pos=C0, clip="spark", scale=1.2)], depth=0.9),
            particles("froid_vif", "snow", "#c8d8ff", (0, 0, 960, 540), [K(0, alpha=0.6)]),
            particles("poussiere_vive", "dust", "#8a8ab8", (0, 60, 960, 420), [K(0, alpha=0.4)])]
    els += front_dust(tint=cold, alpha=0.4)
    cam = [K(0, pos=(480, 270), zoom=1.04), K(D, pos=(480, 270), zoom=1.86)]
    return dict(backdrop="#020206", camera=cam, elements=els, still=5.4)


def shot_collapse(D):
    """Alors elle se replia sur elle-même… et s'éveilla affamée. Plus rien ne s'éloignerait jamais d'elle."""
    els = sky(706, tint="#a090c0")
    t_imp, t_wake, t_roar = 0.8, 2.4, 4.6
    rng = np.random.default_rng(706)
    for i, (kind, p) in enumerate((("foret", (140, 150)), ("volcan", (840, 420)), ("glace", (800, 110)))):
        q = (C0[0] + (p[0] - C0[0]) * 0.55, C0[1] + (p[1] - C0[1]) * 0.55)
        els.append(L.sprite(kind, kind, [K(0, pos=p, clip="spin", scale=0.6), K(t_wake, pos=p), K(D, pos=q, scale=0.75, ease="in")],
                            depth=0.6, tint="#8080a0"))
    for i in range(14):  # debris spiralling in
        a0, r0 = rng.uniform(0, TAU), rng.uniform(260, 420)
        t0 = t_wake + rng.uniform(0, 3.0)
        keys = [K(0, pos=(C0[0] + math.cos(a0) * r0, C0[1] + math.sin(a0) * r0 * 0.6), clip="spin", scale=round(rng.uniform(0.8, 1.4), 2),
                  offset=float(rng.uniform(0, 1))), K(t0, alpha=1.0)]
        for j in range(1, 5):
            u = j / 4
            a = a0 + 2.2 * u
            r = r0 * (1 - u) + 10 * u
            t = min(D, t0 + 2.2 * u)
            keys.append(K(t, pos=(C0[0] + math.cos(a) * r, C0[1] + math.sin(a) * r * 0.6), ease="linear", alpha=1.0 if u < 1 else 0.0))
            if t >= D:
                break
        els.append(L.sprite(f"debris{i + 1}", "grain", keys, depth=0.95))
    els += [glow("trou_lueur", "#c88bff", 220, [K(0, pos=C0, alpha=0.0), K(t_imp + 0.4, alpha=0.0), K(t_wake, alpha=0.8, ease="out"),
                                                K(D, alpha=0.6)], depth=0.9, pulse=(0.25, 1.2)),
            L.sprite("lyse", "lyse", [K(0, pos=C0, clip="spark", scale=1.2), K(t_imp, clip="implode"), K(t_wake, alpha=1.0),
                                      K(t_wake + 0.05, alpha=0.0, ease="step")], depth=0.9),
            L.sprite("singularis", BOSS, [K(0, pos=(C0[0], C0[1] + 100), alpha=0.0, scale=0.8, clip="summon"), K(t_wake, alpha=0.0, ease="step"),
                                          K(t_wake + 0.02, alpha=1.0, clip="summon", ease="step"), K(t_wake + 1.0, clip="idle"),
                                          K(t_roar, clip="roar"), K(min(D, t_roar + 1.4), clip="idle")], depth=0.9),
            glow("coeur", "#ffd27a", 70, [K(0, pos=C0, alpha=0.0), K(t_wake, alpha=0.0), K(t_wake + 0.4, alpha=0.7, ease="out"),
                                          K(D, alpha=0.6)], depth=0.9, pulse=(0.4, 0.8)),
            particles("aspiration", "sparks", "#c88bff", (220, 80, 520, 380), [K(0, alpha=0.0), K(t_wake, alpha=1.0)]),
            particles("poussiere_vive", "dust", "#c8b8d8", (0, 60, 960, 420), [K(0, alpha=0.6)])]
    els += front_dust(tint="#a090c0")
    cam = [K(0, pos=(480, 270), zoom=1.7), K(t_wake, pos=(480, 270), zoom=1.5), K(t_roar, pos=(480, 280), zoom=1.12), K(D, pos=(480, 276), zoom=1.06)]
    shake = [{"from": t_wake, "to": t_wake + 0.8, "amount": 5.0}, {"from": t_roar, "to": min(D, t_roar + 1.0), "amount": 6.0}]
    flash = [{"t": t_wake, "in": 0.1, "out": 0.7, "color": "#e0b0ff", "alpha": 0.7}]
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=shake, flash=flash, still=5.6)


SHOTS = [shot_young_star, shot_three_planets, shot_gifts, shot_machines, shot_last_spark, shot_collapse]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("singularis", "story_singularis_scene", SHOTS, _sprites(), LAYERS, review)
