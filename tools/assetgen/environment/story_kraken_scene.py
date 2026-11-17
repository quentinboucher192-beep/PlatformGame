"""The Kraken's story as six animated shots (« La Lanterne des profondeurs », version 2, 2.63).

Like Pyrrhax's and the Monolithe's (story_stage.py): layers of scenery drawn as animated sheets
(LAYERS), sprites playing their clips frame by frame (characters/story_kraken_sprites.py, the boss's
own sheet), glows and live particles moved by keyframes, a camera, shakes and flashes.  It writes
the scenes into data/stories/kraken.json (the lines kept as written) and each shot's still.

  1. birth      the black sea under the crust, an egg hatches: a little octopus, one light lit
  2. lantern    lost fish follow its light to the hot springs
  3. star_rain  the star of the centre collapses, its light falls into the sea in sparks
  4. growing    it gathers them one by one, grows, its eight arms light up
  5. alone      so big that the fish flee from its shadow into the cracks
  6. trench     the Kraken at the bottom of the Trench, its lights on, waiting

Deterministic (fixed seeds)."""
import math

import numpy as np

import story_stage as S
from story_scenery import (_shade_mask, deep_sea, dither_ramp, floor, front_rocks, kelp, peaks, rgb, rgba_of, silt, sky,
                           starfield)
from story_stage import K, glow, particles, shade

BOSS = "animations/kraken.json"
L = S.Kit("kraken")
TAU = 2 * math.pi

ABYSS = ["#010204", "#02060e", "#040b18", "#061226", "#0a1c36", "#102a4a", "#18385c"]
SEA = ["#02060e", "#061226", "#0a1c36", "#102a4a", "#1a3c62", "#28527a", "#3a6a92"]
NIGHT = ["#06040e", "#0e0a1e", "#1a1030", "#2a1844", "#3c2258", "#4e2c6a", "#64387a"]
CYAN = ("#dcfaff", "#7ae6ff", "#1670a8")


def waves(w, h, seed, k, n, body="#1a3c62", crest="#5a8ab8", foam="#c8e8ff"):
    """The sea's surface seen from the side: its crest rolling, foam glinting, water below it."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    a = TAU * k / n
    top = 6 + 2.4 * np.sin(xx * TAU * 6 / w + a) + 1.2 * np.sin(xx * TAU * 15 / w - 2 * a) + 0.6 * np.sin(xx * TAU * 31 / w + a)
    on = yy >= top
    t = np.clip((yy - top) / (h * 0.8), 0, 1)
    col = dither_ramp(1 - t, [body, body, crest])
    out = rgba_of(col, 255)
    out[~on] = 0
    edge = on & (yy < top + 1.2)
    out[edge, :3] = rgb(crest)
    f = edge & (np.sin(xx * 0.9 + a * 3) > 0.7)
    out[f, :3] = rgb(foam)
    return out


def cliffs(w, h, seed, k, n, body, rim, deep, lights=30, light_cols=CYAN, gap=(0.3, 0.7)):
    """The walls of the Trench, left and right, rough; small lights living in their cracks."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    p = [rng.uniform(0, TAU) for _ in range(4)]
    le = w * (gap[0] - 0.1 * yy / h + 0.04 * np.sin(yy * 0.05 + p[0]) + 0.02 * np.sin(yy * 0.19 + p[1]))
    re = w * (gap[1] + 0.1 * yy / h + 0.04 * np.sin(yy * 0.06 + p[2]) + 0.02 * np.sin(yy * 0.23 + p[3]))
    mask = (xx < le) | (xx > re)
    out = _shade_mask(mask, body, rim, deep)
    # lit edges facing the middle
    edge = mask & (~np.roll(mask, -1, axis=1) | ~np.roll(mask, 1, axis=1))
    out[edge, :3] = rgb(rim)
    a = TAU * k / n
    cs = [rgb(c) for c in light_cols]
    ys, xs = np.nonzero(mask & ~edge)
    for i in range(lights):
        j = int(rng.integers(0, len(xs)))
        ph = rng.uniform(0, TAU)
        lit = 0.5 + 0.5 * math.sin(a * (1 + i % 2) + ph)
        out[ys[j], xs[j], :3] = cs[0 if lit > 0.75 else 1 if lit > 0.35 else 2]
    return out


LAYERS = {
    "abysses": (560, 300, 6, 3, lambda k, n: deep_sea(560, 300, ABYSS, 71, k, n, rays=4, ray_col="#3a6a9a")),
    "vase": (560, 300, 6, 4, lambda k, n: silt(560, 300, 72, k, n, col="#7aa8c8", count=180)),
    "croute": (760, 110, 4, 4, lambda k, n: peaks(760, 110, 73, k, n, 18, 24, 70, 96, "#140c14", "#4a2a3a", "#08040a",
                                                  lean=0.05, wfac=(0.3, 0.5), lights=40,
                                                  light_cols=("#ffd27a", "#ff7a3a", "#5a1a14"), flip=True)),
    "recifs_loin": (720, 130, 4, 4, lambda k, n: peaks(720, 130, 74, k, n, 26, 30, 110, 116, "#0e1c30", "#3a6488", "#060c16",
                                                       lean=0.12, wfac=(0.2, 0.4), lights=40, light_cols=CYAN, glints="#3a7aa8")),
    "recifs": (800, 160, 4, 5, lambda k, n: peaks(800, 160, 75, k, n, 12, 50, 150, 146, "#0c182a", "#3a7090", "#04080e",
                                                  lean=0.16, wfac=(0.24, 0.42), lights=50, light_cols=CYAN, glints="#5aa8d8")),
    "algues": (760, 120, 4, 4, lambda k, n: kelp(760, 120, 76, k, n, 40, "#1a4a4a", "#3a8a7a", "#0a2428")),
    "fond": (760, 110, 4, 5, lambda k, n: floor(760, 110, 77, k, n, 12, "#0c1624", "#2a5a7a", "#050a12", rough=3.0, rocks=14,
                                                specks=80, speck_cols=CYAN)),
    "avant": (900, 120, 4, 4, lambda k, n: front_rocks(900, 120, 78, k, n, "#04080e", "#163248", "#020406", spark="#7ae6ff")),
    "ciel": (560, 150, 6, 3, lambda k, n: sky(560, 150, NIGHT, 79, k, n)),
    "etoiles": (560, 120, 6, 4, lambda k, n: starfield(560, 120, 240, 80, k, n,
                                                       ("#ffffff", "#f0e8ff", "#d0c0ff", "#9080c0", "#584a80"))),
    "mer": (560, 200, 6, 3, lambda k, n: deep_sea(560, 200, SEA, 81, k, n, rays=6, ray_col="#9ab8ff")),
    "vagues": (640, 24, 4, 5, lambda k, n: waves(640, 24, 82, k, n)),
    "fosse": (560, 300, 4, 4, lambda k, n: cliffs(560, 300, 83, k, n, "#12223a", "#4a8aaa", "#060c18")),
}


def _sprites():
    S.chars_path()
    import story_kraken_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of scenery
def sea(seed, crust=True, kelp_alpha=1.0, far_tint=None):
    """The black sea: its depths (faint rays), silt drifting, the crust above (magma glowing through
    its cracks), far and near reefs whose lights breathe, kelp swaying, jellyfish, the bottom."""
    els = [L.layer("abysses", "abysses", 0.03, (-80, -30)),
           L.layer("vase_loin", "vase", 0.15, (-80, -30), drift=(-4, -2), repeat=True, alpha=0.6, offset=0.3)]
    if crust:
        els.append(L.layer("croute", "croute", 0.25, (-240, -60)))
    els += [L.layer("recifs_loin", "recifs_loin", 0.3, (-240, 170), tint=far_tint),
            L.layer("algues_loin", "algues", 0.45, (-300, 300), drift=(0, 0), repeat=True, alpha=0.7 * kelp_alpha, tint="#7a9ab8",
                    offset=0.5)]
    rng = np.random.default_rng(seed)
    for i in range(3):
        x0, y0 = rng.uniform(80, 880), rng.uniform(140, 320)
        els.append(L.sprite(f"meduse{i + 1}", "meduse", [K(0, pos=(x0, y0), clip="idle", offset=float(rng.uniform(0, 1)))],
                            depth=float(0.5 + 0.1 * i), blend="add", drift=(float(rng.uniform(-3, 3)), -5.0)))
    els += [L.layer("recifs", "recifs", 0.6, (-320, 150), offset=0.2),
            L.layer("algues", "algues", 0.8, (-280, 330), alpha=kelp_alpha),
            L.layer("fond", "fond", 1.0, (-280, 400)),
            L.layer("vase", "vase", 0.9, (-80, -30), drift=(6, -3), repeat=True, alpha=0.8)]
    return els


def front():
    return [L.layer("avant", "avant", 1.35, (-420, 316))]


def _fix(keys, end):
    """Drops what goes past the shot's end (keys after it), keeping it inside."""
    return [k for k in keys if k["t"] <= end + 1e-6]


# =================================================================== the six shots
def shot_birth(D):
    """Sous la croûte des Confins dort une mer noire. Un petit poulpe y naquit, avec une seule lueur au bout d'un bras."""
    els = sea(101)
    egg = (520, 446)
    t_hatch, t_light = 3.6, 5.6
    pp0, pp1 = (520, 420), (548, 360)
    els += [
        L.sprite("oeuf2", "oeuf", [K(0, pos=(452, 452), clip="idle", offset=0.5, scale=0.9)], tint="#8a8aaa", shadow=shade(40)),
        L.sprite("oeuf3", "oeuf", [K(0, pos=(590, 450), clip="idle", offset=0.2, scale=0.85, flip=True)], tint="#8a8aaa", shadow=shade(40)),
        glow("oeuf_lueur", "#7ae6ff", 50, [K(0, pos=(egg[0], egg[1] - 36), alpha=0.25), K(t_hatch, alpha=0.5), K(t_hatch + 0.4, alpha=0.0)],
             pulse=(0.5, 1.2)),
        L.sprite("oeuf", "oeuf", [K(0, pos=egg, clip="idle", scale=1.1), K(t_hatch, clip="hatch")], shadow=shade(50)),
        L.sprite("bulles", "bulles", [K(0, pos=(egg[0] + 4, egg[1] - 20), alpha=0.0), K(t_hatch, alpha=0.0, ease="step"),
                                      K(t_hatch + 0.02, alpha=1.0, clip="idle", ease="step")]),
        L.sprite("poulpe", "poulpe", [K(0, pos=pp0, alpha=0.0, clip="dark"), K(t_hatch + 0.3, alpha=0.0, ease="step"),
                                      K(t_hatch + 0.32, alpha=1.0, clip="swim_dark", ease="step"),
                                      K(t_light - 0.6, pos=pp1, ease="out"), K(t_light - 0.59, clip="dark", ease="step"),
                                      K(t_light, clip="light")], shadow=shade(40, alpha=0.25)),
        glow("lanterne", "#7ae6ff", 70, [K(0, pos=(pp1[0] + 18, pp1[1] + 50), alpha=0.0), K(t_light + 0.3, alpha=0.0),
                                         K(t_light + 0.8, alpha=0.9, ease="out"), K(D, alpha=0.85)], pulse=(0.3, 1.0)),
        particles("bulles_vives", "embers", "#9adcff", (360, 300, 320, 180), [K(0, alpha=0.3), K(t_hatch, alpha=1.0), K(t_hatch + 1.6, alpha=0.4)]),
        particles("vase_vive", "dust", "#7aa8c8", (60, 120, 840, 360), [K(0, alpha=0.5)]),
    ]
    els += front()
    cam = [K(0, pos=(480, 200), zoom=1.12), K(2.4, pos=(500, 330), zoom=1.24), K(t_hatch, pos=(520, 384), zoom=1.55),
           K(t_light + 0.4, pos=(540, 372), zoom=1.8), K(D, pos=(546, 366), zoom=1.86)]
    return dict(backdrop="#010204", camera=cam, elements=els, still=6.8)


def shot_lantern(D):
    """Dans le noir, les poissons perdus suivaient sa lueur jusqu'aux sources chaudes. On l'appelait « la Lanterne »."""
    rng = np.random.default_rng(202)
    els = sea(202, crust=False)
    springs = [(760, 446), (850, 452)]
    for i, p in enumerate(springs):
        els += [glow(f"source{i + 1}_lueur", "#ff9a4a", 110, [K(0, pos=(p[0], p[1] - 70), alpha=0.5)], pulse=(0.35, 1.1 + 0.3 * i, 0.4 * i)),
                L.sprite(f"source{i + 1}", "source", [K(0, pos=p, clip="idle", offset=0.3 * i, scale=1.2 - 0.25 * i)],
                         shadow=shade(80, alpha=0.3)),
                L.sprite(f"bulles{i + 1}", "bulles", [K(0, pos=(p[0], p[1] - 70), clip="idle", offset=0.5 * i)])]
    path = [(0.0, (60, 300)), (2.0, (300, 340)), (4.0, (520, 312)), (6.0, (690, 350)), (D, (720, 344))]
    keys = [K(0, pos=path[0][1], clip="swim")] + [K(t, pos=p, ease="inout" if i < 3 else "out") for i, (t, p) in enumerate(path[1:])]
    keys.insert(4, K(6.01, clip="idle"))
    keys.sort(key=lambda k: k["t"])
    els.append(L.sprite("poulpe", "poulpe", keys))
    lk = [K(0, pos=(path[0][1][0] - 14, path[0][1][1] + 24), alpha=0.9)] + [K(t, pos=(p[0] - 14 if t < 6 else p[0] + 2, p[1] + 24 if t < 6 else p[1] + 34))
                                                                         for t, p in path[1:]]
    els.append(glow("lanterne", "#7ae6ff", 90, lk, pulse=(0.3, 0.9)))
    for i in range(7):  # the fish, following in a loose line, a little later each
        lag = 0.45 + 0.42 * i
        dy = rng.uniform(-30, 30)
        fk = [K(0, pos=(path[0][1][0] - 60 - 30 * i, path[0][1][1] + dy), clip="swim", offset=float(rng.uniform(0, 0.6)),
                scale=float(rng.choice([0.9, 1.0, 1.2])))]
        for t, p in path[1:4]:
            fk.append(K(min(D, t + lag), pos=(p[0] - 50 - 18 * i, p[1] + dy + 6 * math.sin(i)), ease="inout"))
        fk.append(K(D, pos=(640 - 40 * (i % 4) + rng.uniform(-10, 10), 300 + 22 * (i % 3) + dy * 0.4), ease="out"))
        fk = _fix(sorted(fk, key=lambda k: k["t"]), D)
        els.append(L.sprite(f"poisson{i + 1}", "poisson", fk, tint=["#ffffff", "#c8e8ff", "#ffe0c0", "#d8c8ff"][i % 4]))
    els += [particles("bulles_vives", "embers", "#9adcff", (700, 260, 200, 200), [K(0, alpha=0.8)]),
            particles("vase_vive", "dust", "#7aa8c8", (60, 120, 840, 360), [K(0, alpha=0.5)])]
    els += front()
    cam = [K(0, pos=(250, 330), zoom=1.36), K(2.0, pos=(330, 336), zoom=1.3), K(4.0, pos=(500, 330), zoom=1.26),
           K(6.0, pos=(640, 340), zoom=1.22), K(D, pos=(660, 336), zoom=1.18)]
    return dict(backdrop="#010204", camera=cam, elements=els, still=4.6)


def shot_star_rain(D):
    """La nuit où l'étoile du centre s'effondra, sa lumière tomba dans la mer en une pluie d'étincelles."""
    rng = np.random.default_rng(303)
    star = (620, 86)
    surf = 214
    els = [L.layer("ciel", "ciel", 0.03, (-80, -30)),
           L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True)]
    els += L.twinkles(303, 6, (40, 0, 880, 170))
    els += [
        glow("lueur_etoile", "#fff2c8", 140, [K(0, pos=star, alpha=0.55), K(1.2, alpha=0.7), K(1.6, alpha=1.0), K(2.0, alpha=0.0, ease="in")],
             depth=0.06),
        glow("lueur_trou", "#c06cf0", 110, [K(0, pos=star, alpha=0.0), K(1.85, alpha=0.0), K(2.0, alpha=0.9, ease="out"), K(D, alpha=0.4)],
             depth=0.06, pulse=(0.25, 1.4)),
        L.sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=1.4), K(1.2, clip="collapse")], depth=0.06),
        L.layer("mer", "mer", 0.9, (-80, surf + 10)),
        L.layer("vagues", "vagues", 0.9, (-200, surf - 10), drift=(10, 0), repeat=True),
        particles("pluie", "ember_rain", "#c8b8ff", (0, -40, 1000, surf + 30), [K(0, alpha=0.0), K(2.0, alpha=0.0), K(2.6, alpha=1.0)],
                  depth=0.7),
    ]
    k = 1
    t = 2.0
    while t < D - 0.8:  # sparks: fast through the air, slow once in the sea
        x0 = rng.uniform(250, 1000)
        y_in = surf + 4
        t_in = t + 0.55
        x_in = x0 - 0.5 * (y_in + 40)
        sink = rng.uniform(3.0, 4.2)
        y_end = rng.uniform(380, 470)
        keys = [K(0, pos=(x0, -40), alpha=0.0, rot=-26.6), K(t, alpha=0.0, ease="step"),
                K(t + 0.01, alpha=1.0, clip="fall", ease="step", offset=float(rng.uniform(0, 0.3))),
                K(t_in, pos=(x_in, y_in), ease="linear"), K(t_in + 0.25, rot=0.0)]
        t_end = t_in + sink
        if t_end <= D:
            keys.append(K(t_end, pos=(x_in - 30, y_end), alpha=0.5, ease="out"))
        else:
            u = (D - t_in) / sink
            keys.append(K(D, pos=(x_in - 30 * u, y_in + (y_end - y_in) * u), ease="linear"))
        els.append(L.sprite(f"etincelle{k}", "etincelle", keys, blend="add"))
        k += 1
        t += rng.uniform(0.18, 0.4)
    pp = (440, 430)
    els += [
        L.layer("algues", "algues", 0.95, (-280, 330)),
        L.layer("fond", "fond", 1.0, (-280, 410)),
        L.sprite("poulpe", "poulpe", [K(0, pos=pp, clip="idle"), K(D, pos=(pp[0] + 10, pp[1] - 14), ease="linear")], shadow=shade(40, alpha=0.3)),
        glow("lanterne", "#7ae6ff", 60, [K(0, pos=(pp[0] - 16, pp[1] + 20), alpha=0.8), K(D, pos=(pp[0] - 6, pp[1] + 6), ease="linear")],
             pulse=(0.3, 1.0)),
        particles("lumiere", "snow", "#c8b8ff", (0, surf + 20, 960, 300), [K(0, alpha=0.0), K(2.6, alpha=0.0), K(3.4, alpha=0.9)]),
        particles("vase_vive", "dust", "#7aa8c8", (60, surf + 40, 840, 260), [K(0, alpha=0.4)]),
    ]
    cam = [K(0, pos=(600, 150), zoom=1.3), K(2.2, pos=(586, 170), zoom=1.26), K(4.4, pos=(480, 340), zoom=1.14),
           K(D, pos=(470, 356), zoom=1.2)]
    shake = [{"from": 1.9, "to": 2.6, "amount": 4.0}]
    flash = [{"t": 1.92, "in": 0.06, "out": 0.5, "color": "#e0b0ff", "alpha": 0.45}]
    return dict(backdrop="#06040e", camera=cam, elements=els, shake=shake, flash=flash, still=3.6)


def shot_growing(D):
    """La Lanterne les recueillit une à une. Il grandit, grandit encore, et ses huit bras s'allumèrent."""
    els = sea(404, crust=False)
    pp = (480, 380)
    catches = [0.9, 2.0, 3.1]
    scales = [1.0, 1.25, 1.5, 1.75]
    t_grow = 4.2
    keys = [K(0, pos=pp, clip="idle", scale=scales[0])]
    for i, t in enumerate(catches):
        keys += [K(t, clip="catch"), K(t + 0.6, scale=scales[i + 1], ease="out")]
    keys += [K(t_grow, alpha=1.0), K(t_grow + 0.02, alpha=0.0, ease="step")]
    els += [glow("lanterne", "#7ae6ff", 70, [K(0, pos=(pp[0] - 14, pp[1] + 22), alpha=0.8), K(1.6, pos=(pp[0] - 18, pp[1] + 28), alpha=0.9),
                                             K(2.7, pos=(pp[0] - 22, pp[1] + 34)), K(3.8, pos=(pp[0] - 26, pp[1] + 40)),
                                             K(t_grow, alpha=0.0)], pulse=(0.3, 0.9)),
            L.sprite("poulpe", "poulpe", keys, shadow=shade(40, alpha=0.3))]
    for i, t in enumerate(catches):  # the sparks sinking to it, caught at the top of its reach
        sc = scales[i]
        tgt = (pp[0] + 14 * sc * 2 * 0.5, pp[1] - 30 * sc * 2 * 0.5)
        p0 = (pp[0] + 120 - 60 * i, -40)
        els.append(L.sprite(f"etincelle{i + 1}", "etincelle", [K(0, pos=p0, alpha=0.0), K(max(0.0, t - 2.0), alpha=0.0, ease="step"),
                                                                 K(max(0.01, t - 1.99), alpha=1.0, clip="fall", ease="step"),
                                                                 K(t + 0.24, pos=tgt, ease="out"), K(t + 0.26, alpha=0.0, ease="step")],
                            blend="add"))
        els.append(glow(f"eclat{i + 1}", "#e0d0ff", 80, [K(0, pos=(pp[0], pp[1] - 4), alpha=0.0), K(t + 0.24, alpha=0.0),
                                                        K(t + 0.34, alpha=0.8, ease="out"), K(t + 1.0, alpha=0.0)]))
    J = (pp[0] - 4, pp[1] + 62)  # the young one's point: its body where the little one was
    order = [0, 2, 4, 6, 7, 5, 3, 1]
    els += [L.sprite("jeune", "jeune", [K(0, pos=J, alpha=0.0, scale=0.9), K(t_grow, alpha=0.0, ease="step"),
                                        K(t_grow + 0.02, alpha=1.0, clip="light_arms", ease="step"), K(D, scale=1.0, ease="out")],
                     shadow=shade(120, alpha=0.3))]
    S.chars_path()
    import story_kraken_sprites as SP
    tips = SP.jeune_tips()
    for k, arm in enumerate(order):  # each arm's light, popping on with its frame
        tip = (J[0] + (tips[arm][0] - 64) * S.PIX * 0.95, J[1] + (tips[arm][1] - 96) * S.PIX * 0.95)
        t_on = t_grow + 0.16 * (k + 1)
        els.append(glow(f"bras{arm + 1}", "#7ae6ff", 46, [K(0, pos=tip, alpha=0.0), K(t_on, alpha=0.0), K(t_on + 0.12, alpha=0.8, ease="out"),
                                                          K(D, alpha=0.6)], pulse=(0.35, 0.8 + 0.1 * k, 0.13 * k)))
    els += [particles("etincelles", "sparks", "#c8b8ff", (pp[0] - 140, pp[1] - 160, 280, 220), [K(0, alpha=0.5), K(t_grow, alpha=1.0)]),
            particles("lumiere", "snow", "#c8b8ff", (0, 0, 960, 400), [K(0, alpha=0.6), K(t_grow, alpha=0.2)]),
            particles("vase_vive", "dust", "#7aa8c8", (60, 120, 840, 360), [K(0, alpha=0.5)])]
    els += front()
    cam = [K(0, pos=(490, 350), zoom=1.8), K(3.6, pos=(486, 352), zoom=1.62), K(t_grow, pos=(480, 340), zoom=1.36),
           K(D, pos=(480, 330), zoom=1.2)]
    shake = [{"from": t_grow, "to": t_grow + 0.6, "amount": 3.0}]
    flash = [{"t": t_grow, "in": 0.1, "out": 0.7, "color": "#c8f4ff", "alpha": 0.8}]
    return dict(backdrop="#010204", camera=cam, elements=els, shake=shake, flash=flash, still=6.4)


def shot_alone(D):
    """Mais il devint si grand que les poissons eurent peur de son ombre et s'enfuirent dans les failles."""
    rng = np.random.default_rng(505)
    els = sea(505, crust=False, kelp_alpha=0.8)
    kr = (380, 420)
    t_move, t_flee = 1.2, 1.9
    els += [
        glow("lumieres", "#7ae6ff", 200, [K(0, pos=(kr[0] + 20, kr[1] - 150), alpha=0.35)], pulse=(0.3, 2.0)),
        L.sprite("kraken", BOSS, [K(0, pos=kr, clip="idle", scale=0.8), K(t_move, clip="move"), K(3.4, pos=(470, 412)),
                                  K(3.41, clip="idle", ease="step")], shadow=shade(330, alpha=0.6)),
    ]
    cracks = [(840, 350), (900, 410), (760, 300), (930, 300)]
    els.append(glow("failles_lueur", "#3a8ab8", 160, [K(0, pos=(860, 350), alpha=0.35)], depth=0.6, pulse=(0.4, 2.2)))
    for i in range(10):  # the school: calm around it, then fleeing into the cracks
        p0 = (kr[0] + rng.uniform(140, 300), kr[1] - rng.uniform(80, 260))
        c = cracks[i % len(cracks)]
        tf = t_flee + rng.uniform(0, 0.6)
        arrive = tf + rng.uniform(1.2, 2.0)
        fk = [K(0, pos=p0, clip="swim", offset=float(rng.uniform(0, 0.6)), scale=float(rng.choice([0.9, 1.0, 1.1]))),
              K(tf, pos=(p0[0] + rng.uniform(-20, 20), p0[1] + rng.uniform(-10, 10))), K(tf + 0.01, clip="flee"),
              K(min(D, arrive), pos=c, ease="in"), K(min(D, arrive + 0.1), alpha=0.0)]
        els.append(L.sprite(f"poisson{i + 1}", "poisson", _fix(fk, D), depth=0.62 if i % 3 == 0 else 1.0,
                            tint=["#ffffff", "#c8e8ff", "#ffe0c0", "#d8c8ff"][i % 4]))
    els += [particles("bulles_vives", "embers", "#9adcff", (200, 200, 400, 260), [K(0, alpha=0.4), K(t_move, alpha=0.9)]),
            particles("vase_vive", "dust", "#7aa8c8", (60, 120, 840, 360), [K(0, alpha=0.6)])]
    els += front()
    cam = [K(0, pos=(470, 300), zoom=1.12), K(t_flee, pos=(520, 310), zoom=1.1), K(D, pos=(560, 320), zoom=1.18)]
    return dict(backdrop="#010204", camera=cam, elements=els, still=2.9)


def shot_trench(D):
    """Le Kraken allume toujours ses lueurs au fond de la Fosse. Il attend qu'on le suive, comme autrefois."""
    els = [L.layer("abysses", "abysses", 0.03, (-80, -30)),
           L.layer("vase_loin", "vase", 0.15, (-80, -30), drift=(-3, -2), repeat=True, alpha=0.6),
           L.layer("fosse_loin", "fosse", 0.3, (-80, -40), tint="#6a7a9a", offset=0.4),
           L.layer("recifs_loin", "recifs_loin", 0.45, (-240, 230), tint="#8090b0"),
           L.sprite("meduse1", "meduse", [K(0, pos=(330, 200), clip="idle")], depth=0.5, blend="add", drift=(2.0, -6.0)),
           L.sprite("meduse2", "meduse", [K(0, pos=(640, 150), clip="idle", offset=0.4)], depth=0.55, blend="add", drift=(-2.0, -4.0)),
           L.layer("fosse", "fosse", 0.7, (-110, -50)),
           L.layer("algues", "algues", 0.85, (-280, 340)),
           L.sprite("bulles", "bulles", [K(0, pos=(560, 430), clip="idle")], depth=0.9),
           L.layer("fond", "fond", 1.0, (-280, 410)),
           L.layer("vase", "vase", 0.9, (-80, -30), drift=(5, -3), repeat=True, alpha=0.8)]
    kr = (480, 470)
    lights = [(-70, -90), (60, -110), (-30, -170), (40, -40), (-110, -40), (100, -60)]
    for i, (dx, dy) in enumerate(lights):  # its lights, waking one after another, breathing
        t_on = 0.5 + 0.35 * i
        els.append(glow(f"lueur{i + 1}", "#7ae6ff", 70, [K(0, pos=(kr[0] + dx, kr[1] + dy), alpha=0.0), K(t_on, alpha=0.0),
                                                         K(t_on + 0.3, alpha=0.75, ease="out"), K(D, alpha=0.7)],
                        pulse=(0.45, 1.2 + 0.2 * i, 0.17 * i)))
    t_call = 3.0
    els += [
        L.sprite("kraken", BOSS, [K(0, pos=kr, clip="idle", scale=0.8), K(t_call, clip="summon"), K(t_call + 1.0, clip="idle")],
                 shadow=shade(300, alpha=0.5)),
        L.sprite("poisson", "poisson", [K(0, pos=(200, 140), alpha=0.0, clip="swim"), K(3.6, alpha=0.0, ease="step"),
                                        K(3.62, alpha=1.0, ease="step"), K(5.0, pos=(330, 250), ease="out"), K(5.6, pos=(338, 246)),
                                        K(5.62, clip="flee", flip=True, ease="step"), K(D, pos=(150, 120), ease="in")]),
        particles("bulles_vives", "embers", "#9adcff", (320, 250, 320, 240), [K(0, alpha=0.6)]),
        particles("etincelles", "sparks", "#7ae6ff", (300, 220, 360, 260), [K(0, alpha=0.0), K(t_call, alpha=0.0), K(t_call + 0.4, alpha=1.0)]),
        particles("vase_vive", "dust", "#7aa8c8", (60, 60, 840, 420), [K(0, alpha=0.5)]),
    ]
    els += front()
    cam = [K(0, pos=(480, 380), zoom=1.3), K(t_call, pos=(480, 372), zoom=1.24), K(5.0, pos=(440, 320), zoom=1.12),
           K(D, pos=(470, 260), zoom=1.08)]
    shake = [{"from": t_call, "to": t_call + 0.6, "amount": 2.5}]
    return dict(backdrop="#010204", camera=cam, elements=els, shake=shake, still=4.4)


SHOTS = [shot_birth, shot_lantern, shot_star_rain, shot_growing, shot_alone, shot_trench]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("kraken", "story_kraken_scene", SHOTS, _sprites(), LAYERS, review, SOUNDS)


# =================================================================== the sounds (2.63)
from story_stage import amb, sfx  # noqa: E402


def _snd_birth(D):
    return [amb("story_amb_deep", 0.55), sfx("story_egg_crack", 3.6, 0.5, pitch=0.8, at=(520, 420)), sfx("story_bubbles", 3.65, 0.6, at=(520, 420)),
            sfx("story_glow", 5.6, 0.6, at=(548, 380))]


def _snd_lantern(D):
    return [amb("story_amb_deep", 0.5), sfx("story_bubbles", 2.0, 0.3, pan=-0.2), sfx("lava_bubble", 6.0, 0.4, at=(780, 400)),
            sfx("story_bubbles", 6.4, 0.35, at=(800, 380))]


def _snd_star_rain(D):
    return [amb("story_amb_night", 0.45, to=2.6), amb("story_amb_deep", 0.5, t=2.2), sfx("story_collapse", 1.2, 0.9, at=(620, 86), duck=True)] + [
        sfx("story_bubbles", t, 0.3, pan=p) for t, p in ((2.8, 0.3), (3.5, -0.2), (4.3, 0.5))]


def _snd_growing(D):
    return [amb("story_amb_deep", 0.5)] + [sfx("story_glow", t + 0.24, 0.45, pitch=1.0 + 0.2 * i) for i, t in enumerate((0.9, 2.0, 3.1))] + [
        sfx("power_unlock", 4.2, 0.7, duck=True), sfx("story_glow", 4.4, 0.4, pitch=0.8)]


def _snd_alone(D):
    return [amb("story_amb_deep", 0.5), sfx("wave_cast", 1.2, 0.5, pitch=0.6, at=(380, 300)), sfx("story_bubbles", 1.9, 0.5, pan=0.4)]


def _snd_trench(D):
    return [amb("story_amb_deep", 0.55), sfx("story_glow", 0.5, 0.3, pitch=0.9), sfx("story_glow", 1.55, 0.3, pitch=1.1),
            sfx("summon", 3.0, 0.6, duck=True), sfx("story_bubbles", 5.62, 0.3, pan=-0.4)]


SOUNDS = {"shot_birth": _snd_birth, "shot_lantern": _snd_lantern, "shot_star_rain": _snd_star_rain, "shot_growing": _snd_growing,
          "shot_alone": _snd_alone, "shot_trench": _snd_trench}
