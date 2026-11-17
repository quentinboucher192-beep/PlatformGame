"""The Titan's story as six animated shots (« Le Dernier Quart », version 2, 2.63).

Like the other stories told in animated scenes (story_stage.py): layers of scenery drawn as animated
sheets (LAYERS), sprites playing their clips frame by frame (characters/story_titan_sprites.py, the
boss's own sheet, Nova's), glows and live particles moved by keyframes, a camera, shakes and
flashes.  It writes the scenes into data/stories/titan.json (the lines kept as written) and each
shot's still.

  1. foundry   the foundry under the Confins: furnaces, a crucible pouring steel, workers hammering
  2. builders  the colossus on its scaffold, a plate of brass lowered, it wakes up: the Titan
  3. promise   the star collapses above the shaft; the foreman entrusts it with the fires and leaves
  4. alone     nobody comes back; the Titan feeds the fires while the rock swallows the foundry
  5. rust      rust, worn pistons, the boiler overheating — it never stops
  6. titan     the Titan still at work; a visitor comes in: a thief come to put out its fires

Deterministic (fixed seeds)."""
import math

import numpy as np
from PIL import Image, ImageDraw

import story_stage as S
from story_scenery import _shade_mask, clouds, dither_ramp, floor, front_rocks, peaks, rgb, rgba_of, sky, starfield
from story_stage import K, glow, particles, shade

BOSS = "animations/titan.json"
NOVA = "animations/nova.json"
L = S.Kit("titan")
TAU = 2 * math.pi

BRICK = ["#0a0606", "#160c0a", "#22120e", "#301a14", "#40241a", "#522e20", "#663a26"]
NIGHT = ["#06040e", "#0e0a1e", "#1a1030", "#2a1844", "#3c2258", "#4e2c6a", "#64387a"]
FIRE = ["#fff6c8", "#ffd060", "#ff8a2a", "#c83a1a", "#5a1a10"]
WARM = ("#fff2c0", "#ffc23c", "#7a4a1a")


def foundry_wall(w, h, seed, k, n, cols, furnaces=4, hole=None):
    """The foundry's back wall: bricks, pipes, dials, furnace mouths roaring along the bottom;
    `hole` (cx, cy, r): a round shaft open on the sky, its iron rim."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    mid = np.exp(-((yy - h * 0.62) / (h * 0.5)) ** 2)
    t = np.clip(0.12 + 0.5 * mid + 0.06 * np.sin(xx * 0.13 + np.sin(yy * 0.05) * 2), 0, 1)
    out = rgba_of(dither_ramp(t, cols), 255)
    mortar = (yy % 6 < 1) | (((xx + (yy // 6) % 2 * 6) % 12) < 1)
    out[mortar, :3] = (out[mortar, :3].astype(np.float32) * 0.6).astype(np.uint8)
    a = TAU * k / n
    metal = [rgb("#8a8278"), rgb("#5a544e"), rgb("#34302c")]
    for i in range(7):  # pipes
        x = int(rng.uniform(6, w - 6))
        y0 = int(rng.uniform(0, h * 0.3))
        for y in range(y0, h):
            for dx in range(-2, 3):
                if 0 <= x + dx < w:
                    out[y, x + dx, :3] = metal[0 if dx < 0 else 1 if dx == 0 else 2]
            if (y - y0) % 24 == 0:
                for dx in range(-3, 4):
                    if 0 <= x + dx < w and y + 1 < h:
                        out[y:y + 2, x + dx, :3] = metal[1]
    fire = [rgb(c) for c in FIRE]
    span = w / furnaces
    for i in range(furnaces):  # furnace mouths
        cx = int(span * (i + 0.5) + rng.uniform(-10, 10))
        cy = int(h * 0.86)
        rw, rh = 16, 20
        ph = rng.uniform(0, TAU)
        for y in range(cy - rh, cy + 8):
            for x in range(cx - rw, cx + rw + 1):
                if not (0 <= x < w and 0 <= y < h):
                    continue
                dxn = (x - cx) / rw
                top = cy - rh + (1 - math.sqrt(max(0.0, 1 - dxn * dxn))) * rh * 0.5
                if y < top:
                    continue
                if abs(dxn) > 0.85 or y < top + 2:
                    out[y, x, :3] = metal[2]
                    continue
                v = 0.5 + 0.5 * math.sin(x * 0.6 + a * 2 + ph + math.sin(y * 0.4 + a) * 2)
                tt = (cy + 8 - y) / (rh + 8)
                out[y, x, :3] = fire[min(4, max(0, int(tt * 3.0 + (1 - v) * 1.8)))]
    for i in range(6):  # dials, their needle trembling
        x, y = int(rng.uniform(10, w - 10)), int(rng.uniform(h * 0.2, h * 0.6))
        for yy2 in range(y - 3, y + 4):
            for xx2 in range(x - 3, x + 4):
                d = math.hypot(xx2 - x, yy2 - y)
                if d <= 3.4 and 0 <= xx2 < w and 0 <= yy2 < h:
                    out[yy2, xx2, :3] = rgb("#d8c8a0") if d < 2.4 else metal[1]
        ang = -2.2 + 0.3 * math.sin(a * 3 + i)
        out[int(y + math.sin(ang) * 2), int(x + math.cos(ang) * 2), :3] = rgb("#a02a1a")
    if hole:
        cx, cy, r = hole
        d = np.hypot(xx - cx, yy - cy)
        out[d < r] = 0
        ring = (d >= r) & (d < r + 3)
        out[ring, :3] = metal[1]
        out[ring & (yy < cy) & (xx < cx), :3] = metal[0]
        out[ring, 3] = 255
    return out


def girders(w, h, seed, k, n, body, rim, deep, bays=6):
    """Iron girders and catwalks across the foundry, lamps hanging from them blinking."""
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    span = w / bays
    d.rectangle([0, 0, w, 6], fill=255)
    d.rectangle([0, h * 0.42, w, h * 0.42 + 5], fill=255)
    for i in range(bays + 1):
        x = i * span
        d.rectangle([x - 3, 0, x + 3, h], fill=255)
    for i in range(bays):
        x0, x1 = i * span, (i + 1) * span
        d.line([(x0, 6), (x1, h * 0.42)], fill=255, width=2)
        d.line([(x1, 6), (x0, h * 0.42)], fill=255, width=2)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    rng = np.random.default_rng(seed)
    warm = [rgb(c) for c in WARM]
    for i in range(bays * 2):  # lamps on their wires
        x = int(rng.uniform(4, w - 4))
        ln = int(rng.uniform(8, 22))
        y0 = int(h * 0.42 + 5)
        for y in range(y0, min(h, y0 + ln)):
            out[y, x, :3] = rgb(deep)
            out[y, x, 3] = 255
        on = math.sin(TAU * k / n * (1 + i % 2) + i) > -0.6
        for (dx, dy) in ((0, 0), (-1, 0), (1, 0), (0, 1)):
            yy, xx = min(h - 1, y0 + ln + dy), x + dx
            if 0 <= xx < w:
                out[yy, xx, :3] = warm[0 if on and dy == 0 and dx == 0 else 1 if on else 2]
                out[yy, xx, 3] = 255
    return out


def chains(w, h, seed, k, n, count=14, col="#5a5458", lit="#a8a098"):
    """Chains hanging from the dark above, swaying, a hook at the end of some."""
    out = np.zeros((h, w, 4), np.uint8)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    for i in range(count):
        x0 = rng.uniform(0, w)
        ln = rng.uniform(h * 0.3, h * 0.95)
        ph = rng.uniform(0, TAU)
        for y in range(int(ln)):
            x = int(x0 + 2.0 * math.sin(a + ph) * (y / h))
            if 0 <= x < w:
                c = rgb(lit) if (y // 2) % 2 == 0 else rgb(col)
                out[y, x, :3] = c
                out[y, x, 3] = 255
                if (y // 2) % 2 and x + 1 < w:
                    out[y, x + 1, :3] = rgb(col)
                    out[y, x + 1, 3] = 255
        if rng.random() < 0.5:
            xe = int(x0 + 2.0 * math.sin(a + ph) * (ln / h))
            for (dx, dy) in ((0, 0), (0, 1), (1, 2), (2, 2), (3, 1)):
                if 0 <= xe + dx < w and int(ln) + dy < h:
                    out[int(ln) + dy, xe + dx, :3] = rgb(lit)
                    out[int(ln) + dy, xe + dx, 3] = 255
    return out


LAYERS = {
    "fonderie": (560, 300, 6, 6, lambda k, n: foundry_wall(560, 300, 121, k, n, BRICK)),
    "puits": (560, 300, 6, 6, lambda k, n: foundry_wall(560, 300, 122, k, n, BRICK, furnaces=3, hole=(300, 70, 56))),
    "poutres": (800, 160, 4, 4, lambda k, n: girders(800, 160, 123, k, n, "#2a2428", "#8a7a6a", "#141014")),
    "chaines": (760, 110, 4, 4, lambda k, n: chains(760, 110, 124, k, n)),
    "fumee": (640, 80, 4, 3, lambda k, n: clouds(640, 80, 125, k, n, body="#2a2020", lit="#8a4a2a", alpha=150)),
    "sol": (760, 110, 4, 5, lambda k, n: floor(760, 110, 126, k, n, 12, "#2a2422", "#7a6a5a", "#121010", rough=1.0, rocks=4,
                                               specks=80, speck_cols=WARM, puddles=4, puddle_col="#ff8a3a")),
    "avant": (900, 120, 4, 4, lambda k, n: front_rocks(900, 120, 127, k, n, "#0e0a0a", "#4a3a30", "#060404", spark="#ffc23c")),
    "roche": (760, 110, 4, 4, lambda k, n: peaks(760, 110, 128, k, n, 22, 30, 96, 96, "#2a2018", "#6a5040", "#140e0a", lean=0.05,
                                                 wfac=(0.24, 0.4), lights=14, light_cols=("#ffe0a0", "#c8803a", "#5a3a1a"), flip=True)),
    "rochers": (800, 160, 4, 5, lambda k, n: peaks(800, 160, 129, k, n, 10, 60, 150, 146, "#241a14", "#6a5040", "#120c08",
                                                   lean=0.08, wfac=(0.3, 0.5), lights=12, light_cols=("#ffe0a0", "#c8803a", "#5a3a1a"),
                                                   glints="#c89a60")),
    "ciel": (560, 150, 6, 3, lambda k, n: sky(560, 150, NIGHT, 130, k, n)),
    "etoiles": (560, 120, 6, 4, lambda k, n: starfield(560, 120, 220, 131, k, n,
                                                       ("#ffffff", "#f0e8ff", "#d0c0ff", "#9080c0", "#584a80"))),
}


def _sprites():
    S.chars_path()
    import story_titan_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of scenery
def foundry(wall="fonderie", tint=None, rock=0.0, rock_keys=None, smoke=True):
    """The foundry: its wall of furnaces, gears turning on it, girders and lamps, chains, smoke
    drifting, sparks; the rock creeping over it (`rock` its alpha, or `rock_keys`)."""
    els = [L.layer("fond", wall, 0.05, (-80, -30), tint=tint)]
    for i, (x, y, sc) in enumerate(((150, 120, 1.2), (220, 150, 0.8), (760, 110, 1.4), (820, 160, 0.9))):
        els.append(L.sprite(f"engrenage{i + 1}", "engrenage", [K(0, pos=(x, y), clip="spin", scale=sc, flip=bool(i % 2))], depth=0.1,
                            tint=tint or "#c0b0a0"))
    els += [L.layer("poutres", "poutres", 0.3, (-240, -20), tint=tint),
            L.layer("chaines", "chaines", 0.45, (-200, -20), tint=tint, offset=0.3)]
    if smoke:
        els.append(L.layer("fumee", "fumee", 0.35, (-300, 60), drift=(10, 0), repeat=True, alpha=0.7))
    if rock > 0 or rock_keys:
        rk = rock_keys or [K(0, alpha=rock)]
        els.append(L.layer("roche", "roche", 0.55, (-280, -40), keys=[dict(k, pos=[-280.0, -40.0]) if i == 0 else k
                                                                     for i, k in enumerate(rk)]))
        els.append(L.layer("rochers", "rochers", 0.6, (-320, 150), keys=[dict(k, pos=[-320.0, 150.0], offset=0.2) if i == 0 else k
                                                                        for i, k in enumerate(rk)]))
    els.append(L.layer("sol", "sol", 1.0, (-280, 400), tint=tint))
    return els


def front(tint=None):
    return [L.layer("avant", "avant", 1.35, (-420, 316), tint=tint)]


def furnace(id_, pos, scale=1.0, depth=1.0, alpha=0.7, tint=None):
    return [glow(id_ + "_lueur", "#ff8a3a", 150 * scale, [K(0, pos=(pos[0], pos[1] - 50 * scale), alpha=alpha)], depth=depth,
                 pulse=(0.25, 0.7)),
            L.sprite(id_, "fourneau", [K(0, pos=pos, clip="idle", scale=scale)], depth=depth, tint=tint)]


# =================================================================== the six shots
def shot_foundry(D):
    """Sous les Confins grondait une fonderie. Mille ouvriers y coulaient l'acier des vaisseaux du système."""
    els = foundry()
    els += furnace("fourneau", (820, 452), 1.1)
    cru = (430, 120)
    els += [L.sprite("creuset", "creuset", [K(0, pos=cru, clip="idle", scale=1.2), K(0.8, clip="pour")], depth=0.9),
            glow("coulee_lueur", "#ffb040", 120, [K(0, pos=(cru[0] + 60, 380), alpha=0.0), K(0.8, alpha=0.0), K(1.2, alpha=0.8)],
                 depth=0.9, pulse=(0.3, 0.4)),
            L.sprite("moule", "moule", [K(0, pos=(cru[0] + 50, 448), clip="idle", scale=1.3)], shadow=shade(150, (820, 380))),
            L.sprite("ouvrier1", "ouvrier1", [K(0, pos=(250, 452), clip="hammer")], shadow=shade(62, (820, 380))),
            L.sprite("ouvrier2", "ouvrier2", [K(0, pos=(320, 456), clip="hammer", offset=0.35, flip=True)], shadow=shade(62, (820, 380))),
            L.sprite("ouvrier3", "ouvrier3", [K(0, pos=(-30, 454), clip="walk"), K(4.2, pos=(560, 454), ease="linear"),
                                              K(4.21, clip="point", ease="step"), K(5.6, clip="idle")], shadow=shade(62, (820, 380))),
            glow("lanterne", "#ffd27a", 60, [K(0, pos=(-20, 410), alpha=0.6), K(4.2, pos=(570, 410), ease="linear")], pulse=(0.3, 0.5)),
            L.sprite("vapeur", "vapeur", [K(0, pos=(680, 446), clip="jet", scale=1.2)], depth=0.95),
            particles("etincelles", "embers", "#ffb040", (140, 280, 760, 200), [K(0, alpha=0.9)]),
            particles("eclats", "sparks", "#ffd27a", (220, 380, 160, 70), [K(0, alpha=1.0)]),
            particles("cendres", "ash", "#a89888", (0, 300, 960, 180), [K(0, alpha=0.5)])]
    els += front()
    cam = [K(0, pos=(480, 290), zoom=1.12), K(2.4, pos=(440, 320), zoom=1.24), K(D, pos=(500, 330), zoom=1.3)]
    return dict(backdrop="#0a0606", camera=cam, elements=els, still=3.2)


def shot_builders(D):
    """Pour les tâches les plus lourdes, ils bâtirent un colosse d'acier et de laiton : le Titan."""
    els = foundry()
    els += furnace("fourneau", (880, 452), 1.0)
    ti = (500, 448)
    TS = 0.85
    t_plate, t_wake = 2.4, 4.6
    els += [L.sprite("titan_eteint", BOSS, [K(0, pos=ti, clip="idle", scale=TS), K(t_wake, alpha=1.0), K(t_wake + 0.4, alpha=0.0)],
                     tint="#605a60", shadow=shade(240, (820, 380), alpha=0.5)),
            L.sprite("titan", BOSS, [K(0, pos=ti, clip="idle", scale=TS, alpha=0.0), K(t_wake, alpha=0.0), K(t_wake + 0.4, alpha=1.0),
                                     K(t_wake + 0.42, clip="roar")]),
            glow("reacteur", "#ffc23c", 70, [K(0, pos=(ti[0] + 20, ti[1] - 250), alpha=0.0), K(t_wake, alpha=0.0),
                                             K(t_wake + 0.5, alpha=0.9, ease="out")], pulse=(0.3, 0.8)),
            # the scaffold and the workers on it are gone in the flash of its waking
            L.sprite("echafaudage", "echafaudage", [K(0, pos=(ti[0] + 10, ti[1] + 4), clip="idle", scale=1.15), K(t_wake + 0.36, alpha=1.0),
                                                    K(t_wake + 0.44, alpha=0.0, ease="step")]),
            L.sprite("crochet", "crochet", [K(0, pos=(ti[0] + 60, -200), clip="idle"), K(t_plate, pos=(ti[0] + 60, 110), ease="out"),
                                            K(t_plate + 1.0, pos=(ti[0] + 60, 110)), K(t_wake, pos=(ti[0] + 60, -220), ease="in")], depth=0.95),
            L.sprite("ouvrier1", "ouvrier1", [K(0, pos=(ti[0] - 40, 244), clip="hammer"), K(t_wake - 0.6, clip="look_up"),
                                              K(t_wake + 0.36, alpha=1.0), K(t_wake + 0.44, alpha=0.0, ease="step")]),
            L.sprite("ouvrier2", "ouvrier2", [K(0, pos=(ti[0] + 60, 356), clip="hammer", offset=0.3), K(t_wake - 0.4, clip="look_up"),
                                              K(t_wake + 0.36, alpha=1.0), K(t_wake + 0.44, alpha=0.0, ease="step")]),
            L.sprite("ouvrier3", "ouvrier3", [K(0, pos=(300, 456), alpha=0.0, clip="look_up"), K(t_wake + 0.36, alpha=0.0, ease="step"),
                                              K(t_wake + 0.44, alpha=1.0, ease="step"), K(t_wake + 1.4, clip="wave")], shadow=shade(62, (880, 380))),
            L.sprite("contremaitre", "contremaitre", [K(0, pos=(200, 452), clip="point"), K(t_wake + 0.3, clip="wave")],
                     shadow=shade(62, (820, 380))),
            particles("soudure", "sparks", "#ffe8a0", (ti[0] - 80, 200, 200, 200), [K(0, alpha=1.0), K(t_wake - 0.5, alpha=1.0),
                                                                                   K(t_wake, alpha=0.0)]),
            particles("etincelles", "embers", "#ffb040", (200, 200, 600, 260), [K(0, alpha=0.7)])]
    els += front()
    cam = [K(0, pos=(500, 300), zoom=1.3), K(t_plate, pos=(510, 260), zoom=1.24), K(t_wake, pos=(500, 270), zoom=1.12),
           K(D, pos=(500, 280), zoom=1.06)]
    shake = [{"from": t_wake + 0.4, "to": t_wake + 1.2, "amount": 4.0}]
    flash = [{"t": t_wake + 0.4, "in": 0.1, "out": 0.6, "color": "#ffd890", "alpha": 0.5}]
    return dict(backdrop="#0a0606", camera=cam, elements=els, shake=shake, flash=flash, still=5.8)


def shot_promise(D):
    """Le soir où l'étoile du centre s'effondra, le contremaître lui confia les feux, jusqu'à leur retour."""
    star = (520, 70)
    els = [L.layer("ciel", "ciel", 0.05, (-80, -40)), L.layer("etoiles", "etoiles", 0.05, (-80, -40), drift=(3, 0), repeat=True),
           glow("lueur_etoile", "#fff2c8", 110, [K(0, pos=star, alpha=0.55), K(1.0, alpha=0.7), K(1.4, alpha=1.0), K(1.8, alpha=0.0, ease="in")],
                depth=0.05),
           glow("lueur_trou", "#c06cf0", 90, [K(0, pos=star, alpha=0.0), K(1.65, alpha=0.0), K(1.8, alpha=0.9, ease="out"), K(D, alpha=0.4)],
                depth=0.05, pulse=(0.25, 1.4)),
           L.sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=1.1), K(1.0, clip="collapse")], depth=0.05)]
    els += L.twinkles(303, 4, (380, 0, 280, 130), depth=0.05)
    els += foundry(wall="puits")
    els += furnace("fourneau", (160, 452), 1.0)
    ti = (640, 448)
    t_give, t_leave = 3.0, 4.8
    els += [L.sprite("titan", BOSS, [K(0, pos=ti, clip="idle", scale=0.85, flip=True), K(t_give + 0.6, clip="telegraph"),
                                     K(t_give + 1.4, clip="idle")], shadow=shade(240, (160, 380), alpha=0.5)),
            L.sprite("contremaitre", "contremaitre", [K(0, pos=(470, 452), clip="look_up"), K(t_give - 0.8, clip="idle"),
                                                      K(t_give, clip="point"), K(t_leave - 0.6, clip="wave"),
                                                      K(t_leave, clip="walk", flip=True), K(D, pos=(250, 452), ease="linear")],
                     shadow=shade(62, (160, 380))),
            L.sprite("ouvrier1", "ouvrier1", [K(0, pos=(360, 456), clip="look_up"), K(2.4, clip="walk", flip=True),
                                              K(min(D, 5.6), pos=(-40, 456), ease="linear")], shadow=shade(62, (160, 380))),
            L.sprite("ouvrier3", "ouvrier3", [K(0, pos=(300, 458), clip="look_up"), K(2.9, clip="walk", flip=True),
                                              K(min(D, 6.2), pos=(-40, 458), ease="linear")], shadow=shade(62, (160, 380))),
            particles("etoiles_vives", "stars", "#d8d8ff", (330, 0, 300, 160), [K(0, alpha=0.6), K(1.8, alpha=0.2)], depth=0.05),
            particles("etincelles", "embers", "#ffb040", (60, 280, 300, 180), [K(0, alpha=0.8)])]
    els += front()
    cam = [K(0, pos=(520, 160), zoom=1.3), K(2.0, pos=(520, 200), zoom=1.26), K(3.4, pos=(540, 320), zoom=1.2), K(D, pos=(500, 330), zoom=1.16)]
    shake = [{"from": 1.7, "to": 2.3, "amount": 3.0}]
    flash = [{"t": 1.72, "in": 0.06, "out": 0.5, "color": "#e0b0ff", "alpha": 0.4}]
    return dict(backdrop="#06040e", camera=cam, elements=els, shake=shake, flash=flash, still=3.4)


def shot_alone(D):
    """Personne ne revint. Siècle après siècle, le Titan garda les feux allumés, et la roche engloutit la fonderie."""
    els = foundry(rock_keys=[K(0, alpha=0.0), K(1.6, alpha=0.0), K(D - 0.6, alpha=1.0)], smoke=True)
    els += furnace("fourneau1", (260, 452), 1.1)
    els += furnace("fourneau2", (760, 446), 0.8, depth=0.85)
    ti = (470, 448)
    els += [L.sprite("titan", BOSS, [K(0, pos=ti, clip="walk", scale=0.82, flip=True), K(1.6, pos=(390, 448), ease="linear"),
                                     K(1.61, clip="attack", ease="step"), K(2.4, clip="idle"), K(3.6, clip="attack"), K(4.4, clip="idle"),
                                     K(5.6, clip="attack"), K(6.4, clip="idle")], shadow=shade(240, (260, 380), alpha=0.5)),
            particles("chute", "dustfall", "#b8a898", (0, 40, 960, 380), [K(0, alpha=0.3), K(D, alpha=1.0)], depth=0.5),
            particles("etincelles", "embers", "#ffb040", (160, 280, 200, 180), [K(0, alpha=0.9)]),
            particles("eclats", "sparks", "#ffd27a", (200, 360, 140, 90), [K(0, alpha=0.0), K(1.7, alpha=1.0), K(2.4, alpha=0.2),
                                                                           K(3.7, alpha=1.0), K(4.4, alpha=0.2), K(5.7, alpha=1.0)])]
    els += front()
    cam = [K(0, pos=(420, 320), zoom=1.3), K(D, pos=(480, 290), zoom=1.06)]
    shake = [{"from": 1.7, "to": 1.9, "amount": 3.0}, {"from": 3.7, "to": 3.9, "amount": 3.0}, {"from": 5.7, "to": 5.9, "amount": 3.0}]
    return dict(backdrop="#0a0606", camera=cam, elements=els, shake=shake, still=6.6)


def shot_rust(D):
    """La rouille rongea ses plaques, ses pistons s'usèrent, sa chaudière surchauffa. Jamais il ne s'arrêta."""
    rust = "#d8a080"
    els = foundry(rock=1.0, tint="#c0a8a0")
    els += furnace("fourneau", (220, 452), 1.1, tint="#c0a8a0")
    ti = (520, 448)
    els += [glow("chaudiere", "#ff5a2a", 140, [K(0, pos=(ti[0] + 10, ti[1] - 250), alpha=0.4), K(3.0, alpha=0.7), K(D, alpha=1.0, scale=1.4)],
                 pulse=(0.4, 0.6)),
            L.sprite("titan", BOSS, [K(0, pos=ti, clip="idle", scale=0.95, flip=True), K(1.0, clip="attack"), K(2.0, clip="hurt"),
                                     K(2.4, clip="attack"), K(3.4, clip="hurt"), K(3.8, clip="idle"), K(4.6, clip="attack"),
                                     K(5.6, clip="hurt"), K(6.0, clip="attack")], tint=rust, shadow=shade(260, (220, 380), alpha=0.5))]
    for i, (dx, dy, t0) in enumerate(((-70, -230, 0.6), (60, -150, 1.9), (-20, -300, 3.1), (90, -260, 4.3), (-90, -120, 5.5))):
        els.append(L.sprite(f"vapeur{i + 1}", "vapeur", [K(0, pos=(ti[0] + dx, ti[1] + dy), alpha=0.0, scale=1.1), K(t0, alpha=0.0, ease="step"),
                                                         K(t0 + 0.02, alpha=0.9, clip="puff", ease="step")]))
    els += [particles("etincelles", "sparks", "#ffd27a", (ti[0] - 120, ti[1] - 320, 240, 300), [K(0, alpha=0.8)]),
            particles("rouille", "ash", "#a86a4a", (ti[0] - 200, ti[1] - 300, 400, 300), [K(0, alpha=0.8)]),
            particles("braises", "embers", "#ff7a3a", (ti[0] - 100, ti[1] - 300, 200, 200), [K(0, alpha=0.5), K(D, alpha=1.0)])]
    els += front(tint="#c0a8a0")
    cam = [K(0, pos=(520, 250), zoom=1.7), K(3.0, pos=(510, 280), zoom=1.5), K(D, pos=(500, 300), zoom=1.3)]
    shake = [{"from": 1.0, "to": 1.3, "amount": 2.0}, {"from": 2.4, "to": 2.7, "amount": 2.0}, {"from": 4.6, "to": D, "amount": 1.5}]
    return dict(backdrop="#0a0606", camera=cam, elements=els, shake=shake, still=2.1)


def shot_titan(D):
    """Le Titan Mécanique travaille toujours. Pour lui, chaque visiteur est un voleur venu éteindre ses feux."""
    els = foundry(rock=1.0, tint="#b8a8a8")
    els += furnace("fourneau1", (800, 452), 1.1, tint="#d0c0b0")
    els += furnace("fourneau2", (660, 440), 0.75, depth=0.85, tint="#d0c0b0")
    ti = (600, 448)
    t_in, t_stop, t_turn = 1.2, 3.6, 4.4
    els += [glow("reacteur", "#ffc23c", 70, [K(0, pos=(ti[0] + 20, ti[1] - 250), alpha=0.6), K(t_turn + 0.4, alpha=1.0, scale=1.6),
                                             K(D, alpha=0.8, scale=1.2)], pulse=(0.3, 0.7)),
            L.sprite("titan", BOSS, [K(0, pos=ti, clip="attack", scale=0.9), K(0.8, clip="idle"), K(t_turn, flip=True, clip="telegraph_special"),
                                     K(t_turn + 0.8, clip="roar")], shadow=shade(250, (800, 380), alpha=0.5)),
            L.sprite("nova", NOVA, [K(0, pos=(-40, 448), clip="walk", scale=1.1), K(t_in, pos=(-40, 448)), K(t_stop, pos=(300, 448), ease="linear"),
                                    K(t_stop + 0.01, clip="idle", ease="step"), K(t_turn + 1.0, clip="hurt"), K(t_turn + 1.4, clip="idle")],
                     shadow=shade(40, (800, 380))),
            L.sprite("vapeur", "vapeur", [K(0, pos=(ti[0] - 60, ti[1] - 200), alpha=0.0, scale=1.3), K(t_turn + 0.8, alpha=0.0, ease="step"),
                                          K(t_turn + 0.82, alpha=0.9, clip="puff", ease="step")]),
            particles("etincelles", "embers", "#ffb040", (500, 260, 400, 200), [K(0, alpha=0.8)]),
            particles("chute", "dustfall", "#b8a898", (100, 60, 760, 360), [K(0, alpha=0.4), K(t_turn + 0.8, alpha=1.0)], depth=0.5)]
    els += front(tint="#b8a8a8")
    cam = [K(0, pos=(600, 300), zoom=1.32), K(2.4, pos=(500, 320), zoom=1.12), K(t_stop, pos=(440, 330), zoom=1.06),
           K(t_turn + 0.8, pos=(460, 320), zoom=1.16), K(D, pos=(470, 316), zoom=1.22)]
    shake = [{"from": t_turn + 0.8, "to": min(D, t_turn + 2.0), "amount": 6.0}]
    flash = [{"t": t_turn + 0.9, "in": 0.08, "out": 0.6, "color": "#ffc070", "alpha": 0.35}]
    return dict(backdrop="#0a0606", camera=cam, elements=els, shake=shake, flash=flash, still=5.8)


SHOTS = [shot_foundry, shot_builders, shot_promise, shot_alone, shot_rust, shot_titan]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("titan", "story_titan_scene", SHOTS, _sprites(), LAYERS, review, SOUNDS)


# =================================================================== the sounds (2.63)
from story_stage import amb, sfx  # noqa: E402


def _snd_foundry(D):
    return [amb("story_amb_foundry", 0.55), sfx("lava_bubble", 0.8, 0.4, at=(480, 300)), sfx("story_steam", 1.0, 0.35, at=(480, 420))] + [
        sfx("story_clang", t, 0.45, at=(280, 440), pitch=1.0 + 0.05 * (i % 2)) for i, t in enumerate((0.45, 1.1, 1.75, 2.4, 3.05, 3.7))] + [
        sfx("story_steam", 3.0, 0.35, at=(680, 400))]


def _snd_builders(D):
    return [amb("story_amb_foundry", 0.5), sfx("story_clang", 2.4, 0.7, at=(560, 200)), sfx("story_steam", 4.7, 0.5),
            sfx("boss_roar", 5.02, 0.85, pitch=0.85, duck=True)]


def _snd_promise(D):
    return [amb("story_amb_foundry", 0.4), sfx("story_collapse", 1.0, 0.8, at=(520, 70), duck=True), sfx("story_steam", 3.6, 0.4, at=(640, 300))]


def _snd_alone(D):
    return [amb("story_amb_foundry", 0.45)] + [s for t in (1.61, 3.6, 5.6) for s in (sfx("story_clang", t + 0.1, 0.55),
                                                                                       sfx("golem_slam", t + 0.12, 0.35))] + [
        sfx("story_stone", 2.4, 0.35), sfx("story_stone", 6.6, 0.35, pitch=0.9)]


def _snd_rust(D):
    return [amb("story_amb_foundry", 0.45)] + [sfx("story_clang", t + 0.1, 0.5, pitch=0.8) for t in (1.0, 2.4, 4.6, 6.0)] + [
        sfx("story_steam", t, 0.5, pan=p) for t, p in ((0.6, -0.3), (1.9, 0.3), (3.1, -0.1), (4.3, 0.4), (5.5, -0.4))]


def _snd_titan(D):
    return [amb("story_amb_foundry", 0.45), sfx("laser_charge", 4.4, 0.5), sfx("boss_roar", 5.2, 0.9, duck=True), sfx("story_steam", 5.25, 0.5)]


SOUNDS = {"shot_foundry": _snd_foundry, "shot_builders": _snd_builders, "shot_promise": _snd_promise, "shot_alone": _snd_alone,
          "shot_rust": _snd_rust, "shot_titan": _snd_titan}
