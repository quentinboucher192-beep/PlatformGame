"""The Phénix's story as six animated shots (« Le Porteur d'aube », version 2, 2.63).

Like the other stories told in animated scenes (story_stage.py): layers of scenery drawn as animated
sheets (LAYERS), sprites playing their clips frame by frame (characters/story_phenix_sprites.py, the
boss's own sheets), glows and live particles moved by keyframes, a camera, shakes and flashes.  It
writes the scenes into data/stories/phenix.json (the lines kept as written) and each shot's still.

  1. dawn_bird   a little bird of light leaves the star of the centre, carrying the dawn
  2. waking      it skims the sleeping volcanoes and the valleys of black glass: day breaks behind it
  3. long_night  the star collapses; the bird waits for its light on a crag, its own light fading
  4. burning     it sets its feathers alight, burns… and falls into ashes
  5. reborn      the next morning it rises from its ashes, bigger, brighter, and flies off again
  6. nest        the Phénix in its nest on the peak, burning itself away at dawn (its rebirth)

Deterministic (fixed seeds)."""
import math

import numpy as np

import story_stage as S
from story_scenery import _shade_mask, clouds, floor, front_rocks, mist, peaks, rgb, sky, starfield
from story_stage import K, glow, particles, shade

BOSS = "animations/phenix.json"
REBORN = "animations/phenix_p5.json"  # its rebirth: it bursts into flames, burns white-hot, is reborn
L = S.Kit("phenix")
TAU = 2 * math.pi

NIGHT = ["#04030c", "#0a0818", "#140c26", "#1e1234", "#2a1840", "#3a2048", "#4a2a50"]
DAWN = ["#1a1030", "#3a1a4a", "#6a2a5a", "#a04a6a", "#d8706a", "#f0a070", "#ffd090"]
GOLD = ("#fff2c0", "#ffd04a", "#a0581c")


def volcanoes(w, h, seed, k, n, count, base, hmin, hmax, body, rim, deep, glow_cols=("#ffb070", "#d8502a", "#6a1a14"),
              smoke="#5a4a5a", plume=1.0):
    """Sleeping volcanoes: cones with a crater, a dull glow breathing in it, smoke curling up."""
    from PIL import Image, ImageDraw
    rng = np.random.default_rng(seed)
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    tops = []
    for _ in range(count):
        x = rng.uniform(0, w)
        hh = rng.uniform(hmin, hmax)
        wd = hh * rng.uniform(1.2, 1.8)
        cw = wd * rng.uniform(0.1, 0.16)
        d.polygon([(x - wd, base + 4), (x - cw, base - hh), (x + cw, base - hh), (x + wd, base + 4)], fill=255)
        tops.append((x, base - hh, cw))
    d.rectangle([0, base, w, h], fill=255)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    a = TAU * k / n
    gc = [rgb(c) for c in glow_cols]
    sc = rgb(smoke)
    for i, (x, y, cw) in enumerate(tops):
        ph = rng.uniform(0, TAU)
        lit = 0.5 + 0.5 * math.sin(a + ph)
        for dx in range(int(-cw), int(cw) + 1):
            xx = int(x + dx)
            if 0 <= xx < w:
                for dy in range(2):
                    yy = int(y + dy)
                    if 0 <= yy < h and mask[yy, xx]:
                        out[yy, xx, :3] = gc[min(2, int((1 - lit) * 1.6 + abs(dx) / max(1.0, cw) + dy))]
        if plume <= 0:
            continue
        for j in range(7):  # the plume: puffs rising and drifting, each one a step of the loop
            u = ((k / n) + j / 7.0) % 1.0
            cx, cy = x + u * 22 * plume + 3 * math.sin(u * 6 + i), y - 2 - u * 46 * plume
            r = (1.5 + u * 7) * plume
            for yy in range(int(cy - r), int(cy + r) + 1):
                for xx in range(int(cx - r), int(cx + r) + 1):
                    if 0 <= yy < h and 0 <= xx < w and not mask[yy, xx]:
                        dd = math.hypot(xx + 0.5 - cx, yy + 0.5 - cy) / max(r, 0.5)
                        if dd <= 1 and ((xx + yy) % 2 == 0 or dd < 0.6) and (1 - u) > 0.2:
                            out[yy, xx, :3] = sc
                            out[yy, xx, 3] = int(200 * (1 - u))
    return out


LAYERS = {
    "ciel_nuit": (560, 300, 6, 3, lambda k, n: sky(560, 300, NIGHT, 91, k, n)),
    "ciel_aube": (560, 300, 6, 3, lambda k, n: sky(560, 300, DAWN, 92, k, n)),
    "etoiles": (560, 210, 6, 4, lambda k, n: starfield(560, 210, 380, 93, k, n,
                                                       ("#ffffff", "#fff4e0", "#ffd8e8", "#b090b0", "#6a5070"))),
    "nuages": (640, 80, 4, 3, lambda k, n: clouds(640, 80, 94, k, n, body="#3a1a3a", lit="#e07a8a", alpha=170)),
    "volcans_loin": (720, 130, 4, 4, lambda k, n: volcanoes(720, 130, 95, k, n, 6, 116, 40, 90, "#2a1830", "#7a4a6a", "#1a0c20",
                                                            plume=0.7, smoke="#6a5068")),
    "volcans": (800, 160, 4, 5, lambda k, n: volcanoes(800, 160, 96, k, n, 4, 146, 70, 130, "#1c1024", "#a05a6a", "#0e0612",
                                                       smoke="#7a6070")),
    "brume": (640, 48, 4, 3, lambda k, n: mist(640, 48, 97, k, n, col="#e0a0b0", amount=0.45)),
    "verre": (760, 110, 4, 5, lambda k, n: floor(760, 110, 98, k, n, 12, "#1c1830", "#8a7aa8", "#0c0a16", rough=3.0, rocks=16,
                                                 specks=90, speck_cols=("#ffffff", "#c8b8ff", "#5a4a7a"))),
    "pics": (800, 160, 4, 5, lambda k, n: peaks(800, 160, 99, k, n, 12, 50, 140, 146, "#160e20", "#6a4a7a", "#0a0610",
                                                lean=0.1, wfac=(0.16, 0.3), lights=20, light_cols=GOLD, glints="#c8a8e0")),
    "avant": (900, 120, 4, 4, lambda k, n: front_rocks(900, 120, 100, k, n, "#0a0610", "#3a2440", "#040206", spark="#ffd04a")),
}


def _sprites():
    S.chars_path()
    import story_phenix_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of scenery
def land(dawn=None, night_alpha=None, smoke=True, near=True, tint=None):
    """The Confins at the edge of night: the skies (the night, the dawn crossfading over it), stars,
    clouds, far and near volcanoes smoking, mist, the valley of black glass."""
    els = [L.layer("ciel", "ciel_nuit", 0.03, (-80, -30), keys=night_alpha)]
    if dawn is not None:
        els.append(L.layer("aube", "ciel_aube", 0.03, (-80, -30), keys=dawn))
    els += [L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True,
                    keys=[K(0, pos=(-80, -20), alpha=0.9)] if dawn is None else [K(0, pos=(-80, -20), alpha=0.8), K(4.5, alpha=0.3)]),
            L.layer("nuages_loin", "nuages", 0.12, (-200, 120), drift=(-8, 0), repeat=True, alpha=0.7, offset=0.3),
            L.layer("volcans_loin", "volcans_loin", 0.25, (-240, 170), tint=tint),
            L.layer("brume", "brume", 0.4, (-160, 330), drift=(12, 0), repeat=True, alpha=0.8)]
    if near:
        els.append(L.layer("volcans", "volcans", 0.55, (-320, 140), offset=0.2, tint=tint))
    els.append(L.layer("verre", "verre", 1.0, (-280, 400), tint=tint))
    return els


def front(tint=None):
    return [L.layer("avant", "avant", 1.35, (-420, 316), tint=tint)]


def _path(points, D):
    """Keys along a path [(t, (x, y), clip?)] (clips change at their key)."""
    out = []
    for i, p in enumerate(points):
        t, pos = p[0], p[1]
        kw = dict(pos=pos)
        if len(p) > 2 and p[2]:
            kw["clip"] = p[2]
        if len(p) > 3:
            kw.update(p[3])
        out.append(K(min(t, D), ease="inout" if i else None, **kw) if i else K(t, **kw))
    return out


# =================================================================== the six shots
def shot_dawn_bird(D):
    """Autrefois, chaque matin, un petit oiseau de lumière quittait l'étoile du centre pour porter l'aube aux Confins."""
    star = (180, 170)
    els = [L.layer("ciel", "ciel_nuit", 0.03, (-80, -30)),
           L.layer("aube", "ciel_aube", 0.03, (-80, -30), keys=[K(0, pos=(-80, -30), alpha=0.0), K(4.0, alpha=0.0), K(D, alpha=0.55)]),
           L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True)]
    els += L.twinkles(601, 8, (300, 10, 640, 260))
    els += [glow("etoile_lueur", "#fff2c8", 260, [K(0, pos=star, alpha=0.8)], depth=0.1, pulse=(0.2, 2.4)),
            glow("etoile_halo", "#ffd04a", 140, [K(0, pos=star, alpha=0.7)], depth=0.1, pulse=(0.3, 1.3, 0.3)),
            L.sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=2.4)], depth=0.1),
            L.layer("nuages_loin", "nuages", 0.15, (-200, 200), drift=(-8, 0), repeat=True, alpha=0.75, offset=0.3),
            L.layer("volcans_loin", "volcans_loin", 0.3, (-140, 250)),
            glow("aube_horizon", "#ffb070", 320, [K(0, pos=(860, 470), alpha=0.0), K(4.6, alpha=0.0), K(D, alpha=0.7, ease="out")], depth=0.3),
            L.layer("nuages", "nuages", 0.4, (-300, 330), drift=(14, 0), repeat=True, alpha=0.85)]
    t_out = 1.0
    pts = [(0, (star[0] + 10, star[1]), "fly", dict(alpha=0.0, scale=0.6)), (t_out, (star[0] + 20, star[1] - 4), None, dict(alpha=0.0)),
           (t_out + 0.02, (star[0] + 22, star[1] - 4), None, dict(alpha=1.0)), (2.6, (360, 150), None, dict(scale=1.0)),
           (4.2, (560, 210), "glide"), (5.6, (720, 270), "fly"), (D, (860, 330), "glide", dict(scale=0.9))]
    keys = _path(pts, D)
    keys[2]["ease"] = "step"
    keys[1]["ease"] = "step"
    els.append(L.sprite("oiseau", "oiseau", keys, depth=0.6))
    trail = [K(0, pos=(star[0] + 10, star[1]), alpha=0.0)] + [K(t, pos=(p[0] - 30, p[1] + 6), alpha=0.8 if t > t_out else 0.0)
                                                              for (t, p, *_r) in pts[1:]]
    els.append(glow("oiseau_lueur", "#ffd04a", 90, trail, depth=0.6, pulse=(0.3, 0.6)))
    els += [particles("etincelles", "sparks", "#ffd04a", (200, 100, 700, 280), [K(0, alpha=0.4), K(t_out, alpha=0.4), K(2.0, alpha=1.0)],
                      depth=0.6),
            particles("etoiles_vives", "stars", "#fff0d8", (0, 0, 960, 300), [K(0, alpha=0.5)], depth=0.1)]
    cam = [K(0, pos=(300, 220), zoom=1.3), K(t_out + 0.6, pos=(330, 210), zoom=1.26), K(4.2, pos=(560, 250), zoom=1.2),
           K(D, pos=(720, 300), zoom=1.14)]
    flash = [{"t": t_out, "in": 0.1, "out": 0.4, "color": "#fff2c0", "alpha": 0.35}]
    return dict(backdrop="#04030c", camera=cam, elements=els, flash=flash, still=3.0)


def shot_waking(D):
    """Il frôlait les volcans endormis et les vallées de verre noir. Partout où il passait, le jour se levait."""
    els = land(dawn=[K(0, pos=(-80, -30), alpha=0.0), K(1.0, alpha=0.1), K(D, alpha=1.0, ease="inout")])
    els += [L.sprite("rocher1", "rocher", [K(0, pos=(260, 452), clip="idle", scale=1.0)], shadow=shade(160, (700, 200))),
            L.sprite("rocher2", "rocher", [K(0, pos=(720, 456), clip="idle", scale=1.2, flip=True, offset=0.4)],
                     shadow=shade(190, (700, 200)))]
    pts = [(0, (-40, 300), "fly"), (1.8, (200, 360), "glide"), (3.2, (420, 330), "fly"), (4.6, (620, 380), "glide"),
           (6.0, (820, 330), "fly"), (D, (1000, 280))]
    els.append(L.sprite("oiseau", "oiseau", _path(pts, D)))
    els.append(glow("oiseau_lueur", "#ffd04a", 80, [K(0, pos=(-70, 306), alpha=0.8)] + [K(t, pos=(p[0] - 30, p[1] + 6)) for (t, p, *_r) in pts[1:]],
                    pulse=(0.3, 0.6)))
    els.append(glow("jour", "#ffc080", 300, [K(0, pos=(-120, 380), alpha=0.5)] + [K(t, pos=(p[0] - 140, 400), alpha=0.6)
                                                                                  for (t, p, *_r) in pts[1:]], depth=0.8, pulse=(0.2, 2.0)))
    els += [particles("eclats", "sparks", "#ffe0a0", (0, 400, 960, 70), [K(0, alpha=0.4), K(D, alpha=1.0)]),
            particles("etincelles", "embers", "#ffd04a", (60, 260, 840, 200), [K(0, alpha=0.6)]),
            particles("cendres", "ash", "#c8a8b8", (0, 330, 960, 150), [K(0, alpha=0.6)])]
    els += front()
    cam = [K(0, pos=(220, 330), zoom=1.22), K(3.2, pos=(440, 334), zoom=1.2), K(D, pos=(760, 320), zoom=1.18)]
    return dict(backdrop="#04030c", camera=cam, elements=els, still=4.0)


def shot_long_night(D):
    """La nuit où l'étoile du centre s'effondra, l'oiseau attendit sa lumière. Elle ne revint jamais."""
    star = (660, 120)
    els = [L.layer("ciel", "ciel_nuit", 0.03, (-80, -30)),
           L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True)]
    els += L.twinkles(603, 6, (40, 10, 880, 220))
    els += [glow("lueur_etoile", "#fff2c8", 150, [K(0, pos=star, alpha=0.6), K(1.2, alpha=0.75), K(1.6, alpha=1.0), K(2.0, alpha=0.0, ease="in")],
                 depth=0.06),
            glow("lueur_trou", "#c06cf0", 110, [K(0, pos=star, alpha=0.0), K(1.85, alpha=0.0), K(2.0, alpha=0.9, ease="out"), K(D, alpha=0.3)],
                 depth=0.06, pulse=(0.3, 1.6)),
            L.sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=1.5), K(1.2, clip="collapse")], depth=0.06)]
    els += [L.layer("nuages_loin", "nuages", 0.12, (-200, 160), drift=(-6, 0), repeat=True, alpha=0.6, tint="#8080a0"),
            L.layer("volcans_loin", "volcans_loin", 0.25, (-240, 170), tint="#8888a8"),
            L.layer("brume", "brume", 0.4, (-160, 330), drift=(8, 0), repeat=True, alpha=0.6, tint="#8090b0"),
            L.layer("verre", "verre", 1.0, (-280, 400), tint="#a0a0c0")]
    crag = (420, 456)
    perch = (crag[0] + 6, crag[1] - 92)
    els += [L.sprite("rocher", "rocher", [K(0, pos=crag, clip="idle", scale=1.0)], shadow=shade(170, star)),
            glow("oiseau_lueur", "#ffd04a", 80, [K(0, pos=(perch[0], perch[1] - 16), alpha=0.7), K(2.6, alpha=0.7), K(4.2, alpha=0.1)],
                 pulse=(0.3, 1.0)),
            L.sprite("oiseau", "oiseau", [K(0, pos=perch, clip="look_up"), K(2.6, clip="fade"), K(5.4, clip="dim")], shadow=shade(30, star)),
            particles("poussiere", "dust", "#a0a0c0", (100, 200, 760, 260), [K(0, alpha=0.6)]),
            particles("etoiles_vives", "stars", "#d8d8ff", (0, 0, 960, 260), [K(0, alpha=0.6), K(2.0, alpha=0.2)], depth=0.1)]
    els += front(tint="#8888a8")
    cam = [K(0, pos=(580, 220), zoom=1.16), K(2.2, pos=(560, 240), zoom=1.16), K(4.0, pos=(450, 340), zoom=1.5), K(D, pos=(436, 346), zoom=1.62)]
    shake = [{"from": 1.9, "to": 2.5, "amount": 3.0}]
    flash = [{"t": 1.92, "in": 0.06, "out": 0.5, "color": "#e0b0ff", "alpha": 0.4}]
    return dict(backdrop="#04030c", camera=cam, elements=els, shake=shake, flash=flash, still=5.0)


def shot_burning(D):
    """Alors, pour que l'aube revienne, il alluma ses propres plumes. Il brûla, brûla… et tomba en cendres."""
    dark = "#9090b0"
    els = land(dawn=[K(0, pos=(-80, -30), alpha=0.0), K(1.4, alpha=0.0), K(3.4, alpha=0.45), K(4.8, alpha=0.35), K(5.8, alpha=0.0)],
               near=False, tint=dark)
    crag = (440, 456)
    perch = (crag[0] + 6, crag[1] - 92)
    t_fire, t_down, t_ash = 1.0, 4.6, 5.3
    els += [L.sprite("rocher", "rocher", [K(0, pos=crag, clip="idle", scale=1.0)], shadow=shade(170, (perch[0], perch[1] - 30))),
            glow("brasier", "#ff8a3a", 280, [K(0, pos=(perch[0], perch[1] - 20), alpha=0.15), K(t_fire, alpha=0.15), K(3.0, alpha=0.95, ease="out"),
                                             K(t_down, alpha=0.8), K(t_ash, alpha=0.3), K(D, alpha=0.2)], pulse=(0.2, 0.5)),
            glow("coeur", "#fff2c0", 90, [K(0, pos=(perch[0], perch[1] - 16), alpha=0.0), K(t_fire, alpha=0.0), K(3.0, alpha=0.9),
                                          K(t_down + 0.4, alpha=0.0)], pulse=(0.3, 0.35)),
            L.sprite("oiseau", "oiseau", [K(0, pos=perch, clip="dim"), K(t_fire, clip="ignite"), K(t_down, clip="collapse"),
                                          K(t_ash + 0.3, alpha=1.0), K(t_ash + 0.5, alpha=0.0)], shadow=shade(30)),
            L.sprite("cendres", "cendres", [K(0, pos=(perch[0], perch[1] + 2), alpha=0.0, clip="idle"), K(t_ash, alpha=0.0, ease="step"),
                                            K(t_ash + 0.02, alpha=1.0, ease="step")]),
            L.sprite("fumee", "fumee", [K(0, pos=(perch[0], perch[1] - 4), alpha=0.0, scale=1.0), K(t_down + 0.5, alpha=0.0, ease="step"),
                                        K(t_down + 0.52, alpha=0.9, clip="puff", ease="step")]),
            particles("braises", "embers", "#ffb066", (perch[0] - 120, perch[1] - 200, 240, 220), [K(0, alpha=0.0), K(t_fire + 0.4, alpha=1.0),
                                                                                                 K(t_ash, alpha=0.6)]),
            particles("etincelles", "sparks", "#ffd04a", (perch[0] - 160, perch[1] - 160, 320, 200), [K(0, alpha=0.0), K(2.0, alpha=1.0),
                                                                                                    K(t_ash, alpha=0.2)]),
            particles("cendres_vol", "ash", "#a8a0a8", (perch[0] - 200, perch[1] - 100, 400, 160), [K(0, alpha=0.0), K(t_down, alpha=0.0),
                                                                                                  K(t_ash, alpha=1.0)])]
    els += front(tint=dark)
    cam = [K(0, pos=(440, 350), zoom=1.55), K(t_fire, pos=(440, 346), zoom=1.5), K(3.2, pos=(444, 320), zoom=1.24),
           K(t_down, pos=(446, 330), zoom=1.3), K(D, pos=(446, 366), zoom=1.7)]
    shake = [{"from": 2.4, "to": 3.6, "amount": 2.0}]
    flash = [{"t": 3.0, "in": 0.3, "out": 0.8, "color": "#ffd8a0", "alpha": 0.35}]
    return dict(backdrop="#04030c", camera=cam, elements=els, shake=shake, flash=flash, still=3.3)


def shot_reborn(D):
    """Le lendemain, il renaquit de ses cendres, plus grand et plus brillant. Et il recommença."""
    els = land(dawn=[K(0, pos=(-80, -30), alpha=0.3), K(D, alpha=1.0)], near=False)
    crag = (440, 456)
    perch = (crag[0] + 6, crag[1] - 92)
    t_stir, t_burst, t_rise, t_go = 0.8, 1.9, 2.4, 5.0
    els += L.twinkles(605, 3, (560, 20, 380, 140))
    els += [glow("soleil", "#ffd090", 300, [K(0, pos=(820, 440), alpha=0.2), K(D, pos=(820, 400), alpha=0.6)], depth=0.25),
            L.sprite("rocher", "rocher", [K(0, pos=crag, clip="idle", scale=1.0)], shadow=shade(170, (perch[0], perch[1] - 60))),
            glow("braise", "#ff9a3a", 120, [K(0, pos=(perch[0], perch[1] - 6), alpha=0.3), K(t_burst, alpha=0.8), K(t_rise, alpha=1.0, scale=2.0),
                                            K(t_go, alpha=0.6, scale=1.4), K(D, alpha=0.2)], pulse=(0.3, 0.6)),
            L.sprite("cendres", "cendres", [K(0, pos=(perch[0], perch[1] + 2), clip="idle"), K(t_stir, clip="stir"), K(t_burst, clip="burst"),
                                            K(t_rise + 0.6, alpha=1.0), K(t_rise + 1.2, alpha=0.0)]),
            L.sprite("grand", "grand", [K(0, pos=(perch[0], perch[1] + 4), alpha=0.0), K(t_rise, alpha=0.0, ease="step"),
                                        K(t_rise + 0.02, alpha=1.0, clip="rise", ease="step"), K(t_rise + 1.4, pos=(perch[0], perch[1] - 50), ease="out"),
                                        K(t_go, pos=(perch[0] + 20, perch[1] - 60)), K(D, pos=(perch[0] + 420, perch[1] - 220), ease="in")],
                     shadow=shade(60)),
            glow("grand_lueur", "#ffd04a", 120, [K(0, pos=(perch[0], perch[1] - 40), alpha=0.0), K(t_rise, alpha=0.0),
                                                 K(t_rise + 0.4, alpha=0.9), K(t_rise + 1.4, pos=(perch[0], perch[1] - 86)),
                                                 K(t_go, pos=(perch[0] + 20, perch[1] - 96)), K(D, pos=(perch[0] + 420, perch[1] - 256), ease="in")],
                 pulse=(0.25, 0.7)),
            particles("braises", "embers", "#ffb066", (perch[0] - 120, perch[1] - 220, 240, 240), [K(0, alpha=0.3), K(t_burst, alpha=1.0)]),
            particles("etincelles", "sparks", "#ffe080", (perch[0] - 200, perch[1] - 240, 400, 260), [K(0, alpha=0.0), K(t_rise, alpha=1.0)])]
    els += front()
    cam = [K(0, pos=(446, 366), zoom=1.7), K(t_burst, pos=(446, 356), zoom=1.6), K(t_rise + 1.4, pos=(450, 300), zoom=1.3),
           K(D, pos=(600, 250), zoom=1.16)]
    shake = [{"from": t_burst, "to": t_rise + 0.6, "amount": 3.0}]
    flash = [{"t": t_rise, "in": 0.12, "out": 0.7, "color": "#fff0c0", "alpha": 0.8}]
    return dict(backdrop="#1a1030", camera=cam, elements=els, shake=shake, flash=flash, still=4.0)


def shot_nest(D):
    """Depuis, le Phénix se consume chaque matin dans son nid. Il ne sait plus s'arrêter, de peur que la nuit gagne."""
    els = land(dawn=[K(0, pos=(-80, -30), alpha=0.2), K(D, alpha=0.9)])
    peak = (520, 560)
    nest = (520, 330)
    t_burn = 3.2
    t_white = t_burn + 0.56
    els += [glow("soleil", "#ffd090", 320, [K(0, pos=(160, 470), alpha=0.2), K(D, pos=(160, 420), alpha=0.7)], depth=0.25,
                 pulse=(0.2, 3.0)),
            L.layer("pics", "pics", 0.7, (-320, 220), offset=0.3),
            L.sprite("pic", "rocher", [K(0, pos=peak, clip="idle", scale=2.3)]),
            L.sprite("nid_fond", "nid", [K(0, pos=nest, clip="back")]),
            glow("nid_lueur", "#ff8a3a", 200, [K(0, pos=(nest[0], nest[1] - 70), alpha=0.4), K(t_burn, alpha=0.5), K(t_burn + 0.6, alpha=1.0),
                                               K(D, alpha=0.8)], pulse=(0.25, 0.8)),
            L.sprite("phenix", BOSS, [K(0, pos=(nest[0], nest[1] - 4), clip="idle", scale=0.82), K(t_burn, alpha=1.0),
                                      K(t_burn + 0.02, alpha=0.0, ease="step"), K(t_white, alpha=0.0),
                                      K(t_white + 0.02, alpha=1.0, clip="summon", ease="step"), K(t_white + 0.9, clip="idle")],
                     shadow=shade(150, (160, 420), alpha=0.35)),
            # it bursts into flames and burns white-hot (the rebirth of its sheet of phase 5), and is
            # itself again, blazing, when the white light fades
            L.sprite("phenix_brasier", REBORN, [K(0, pos=(nest[0], nest[1] - 4), alpha=0.0, scale=0.82, clip="roar"),
                                                K(t_burn, alpha=0.0, ease="step"), K(t_burn + 0.02, alpha=1.0, clip="roar", ease="step"),
                                                K(t_white, alpha=1.0), K(t_white + 0.02, alpha=0.0, ease="step")]),
            L.sprite("nid_devant", "nid", [K(0, pos=nest, clip="front")]),
            particles("braises", "embers", "#ffb066", (nest[0] - 200, nest[1] - 260, 400, 260), [K(0, alpha=0.6), K(t_burn, alpha=1.0)]),
            particles("etincelles", "sparks", "#ffd04a", (nest[0] - 260, nest[1] - 280, 520, 300), [K(0, alpha=0.3), K(t_burn + 0.3, alpha=1.0)]),
            particles("cendres", "ash", "#c8a8b8", (0, 300, 960, 200), [K(0, alpha=0.5)])]
    els += front()
    cam = [K(0, pos=(520, 250), zoom=1.3), K(t_burn, pos=(520, 236), zoom=1.22), K(D, pos=(520, 250), zoom=1.08)]
    shake = [{"from": t_burn, "to": t_burn + 1.2, "amount": 4.0}]
    flash = [{"t": t_burn + 0.05, "in": 0.1, "out": 0.4, "color": "#fff0c0", "alpha": 0.5},
             {"t": t_white, "in": 0.08, "out": 0.7, "color": "#ffffff", "alpha": 0.8}]
    return dict(backdrop="#1a1030", camera=cam, elements=els, shake=shake, flash=flash, still=6.6)


SHOTS = [shot_dawn_bird, shot_waking, shot_long_night, shot_burning, shot_reborn, shot_nest]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("phenix", "story_phenix_scene", SHOTS, _sprites(), LAYERS, review, SOUNDS)


# =================================================================== the sounds (2.63)
from story_stage import amb, sfx  # noqa: E402


def _snd_dawn_bird(D):
    return [amb("story_amb_space", 0.45), sfx("story_glow", 1.0, 0.5, at=(200, 170)), sfx("story_wings", 1.1, 0.5, at=(220, 170)),
            sfx("story_wings", 2.8, 0.4, at=(380, 150)), sfx("story_wings", 5.6, 0.4, at=(720, 270))]


def _snd_waking(D):
    return [amb("story_amb_night", 0.4), amb("story_amb_magma", 0.25)] + [sfx("story_wings", t, v, at=p)
                                                                         for t, v, p in ((0.4, 0.45, (60, 320)), (3.2, 0.4, (420, 330)),
                                                                                         (6.0, 0.35, (820, 330)))]


def _snd_long_night(D):
    return [amb("story_amb_night", 0.45), sfx("story_collapse", 1.2, 0.9, at=(660, 120), duck=True), sfx("story_glow", 2.7, 0.3, pitch=0.5)]


def _snd_burning(D):
    return [amb("story_amb_night", 0.35), sfx("story_ignite", 1.0, 0.8, duck=True), amb("story_amb_fire", 0.6, t=1.3, to=5.0),
            sfx("crumble", 4.6, 0.4, pitch=1.3), sfx("story_puff", 5.1, 0.5, pitch=0.8)]


def _snd_reborn(D):
    return [amb("story_amb_night", 0.3), amb("story_amb_fire", 0.5, t=1.9), sfx("story_glow", 0.8, 0.3, pitch=0.7),
            sfx("story_ignite", 1.9, 0.7), sfx("respawn", 2.4, 0.7, duck=True), sfx("story_wings", 3.4, 0.45), sfx("story_wings", 5.0, 0.4)]


def _snd_nest(D):
    return [amb("story_amb_fire", 0.45), amb("story_amb_magma", 0.25), sfx("story_ignite", 3.2, 0.9, duck=True),
            sfx("supernova", 3.76, 0.6), sfx("story_wings", 3.8, 0.5)]


SOUNDS = {"shot_dawn_bird": _snd_dawn_bird, "shot_waking": _snd_waking, "shot_long_night": _snd_long_night, "shot_burning": _snd_burning,
          "shot_reborn": _snd_reborn, "shot_nest": _snd_nest}
