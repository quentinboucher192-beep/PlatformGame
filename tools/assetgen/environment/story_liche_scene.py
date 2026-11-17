"""The Liche's story as six animated shots (« Le Gardien des noms », version 2, 2.63).

Like the other stories told in animated scenes (story_stage.py): layers of scenery drawn as animated
sheets (LAYERS), sprites playing their clips frame by frame (characters/story_liche_sprites.py, the
boss's own sheet, its phylactery's), glows and live particles moved by keyframes, a camera, shakes
and flashes.  It writes the scenes into data/stories/liche.json (the lines kept as written) and
each shot's still.

  1. candles     the catacombs; old Ossian lights a candle for each of the dead
  2. names       the stones with their names, glowing as he tells them to a child
  3. exodus      the star collapses, the living flee; Ossian stays and goes back to his dead
  4. phylactery  grown very old, he hides his soul in a crystal on the altar
  5. bones       the centuries: the candles burn out, cobwebs grow, his flesh goes, the green flame stays
  6. lich        the Liche watching over its crypt, the names glowing green around it

Deterministic (fixed seeds)."""
import math

import numpy as np
from PIL import Image, ImageDraw

import story_stage as S
from story_scenery import _shade_mask, dither_ramp, floor, front_rocks, mist, peaks, rgb, rgba_of, sky, starfield
from story_stage import K, glow, particles, shade

BOSS = "animations/liche.json"
PHYL = "animations/phylactere.json"
L = S.Kit("liche")
TAU = 2 * math.pi

STONE = ["#060508", "#0e0c10", "#18151a", "#221e22", "#2e2a2c", "#3c3836", "#4c4642"]
NIGHT = ["#04030a", "#0a0816", "#120e22", "#1c1430", "#281a3c", "#342046", "#422a50"]
WARM = ("#fff2c0", "#ffcf7a", "#7a4a2a")


def niches(w, h, seed, k, n, cols, rows=4):
    """The wall of the catacombs: blocks of stone, rows of arched niches, a skull in most of them,
    a candle flickering in a few (its light on the niche's walls)."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    rng = np.random.default_rng(seed)
    mid = np.exp(-((yy - h * 0.5) / (h * 0.5)) ** 2)
    v = 0.5 + 0.5 * np.sin(xx * 0.11 + np.sin(yy * 0.07) * 2.0)
    t = np.clip(0.15 + 0.45 * mid + 0.08 * v, 0, 1)
    out = rgba_of(dither_ramp(t, cols), 255)
    dark = rgb(cols[0])
    mortar = (yy % 10 < 1) | (((xx + (yy // 10) % 2 * 9) % 18) < 1)
    out[mortar, :3] = (out[mortar, :3].astype(np.float32) * 0.6).astype(np.uint8)
    a = TAU * k / n
    bone = [rgb("#c8c0b0"), rgb("#8a8478"), rgb("#4a4640")]
    warm = [rgb(c) for c in WARM]
    y0 = h * 0.18
    for r in range(rows):
        cy = int(y0 + r * h * 0.19)
        off = 13 if r % 2 else 0
        for cx in range(14 + off, w - 10, 27):
            nw, nh = 9, 14
            lit = rng.random() < 0.18
            ph = rng.uniform(0, TAU)
            fl = 0.5 + 0.5 * math.sin(a * 2 + ph)
            for y in range(cy - nh, cy + 1):
                for x in range(cx - nw, cx + nw + 1):
                    dxn = (x - cx) / nw
                    top = cy - nh + (nw - math.sqrt(max(0.0, nw * nw - (x - cx) ** 2))) * 0.8
                    if y < top or not (0 <= x < w and 0 <= y < h):
                        continue
                    if lit:
                        d = math.hypot(dxn, (y - cy) / nh)
                        lv = 0.25 + 0.35 * fl - 0.3 * d
                        c = dark + (warm[1] - dark) * max(0.0, lv)
                        out[y, x, :3] = np.clip(c, 0, 255).astype(np.uint8)
                    else:
                        out[y, x, :3] = dark
            if rng.random() < 0.7:  # a skull
                sx, sy = cx + int(rng.integers(-3, 3)), cy - 3
                for (dx, dy) in ((-2, -1), (-1, -2), (0, -2), (1, -2), (2, -1), (-2, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (-1, 0), (1, -1),
                                 (0, -1), (-1, -1)):
                    if 0 <= sy + dy < h and 0 <= sx + dx < w:
                        out[sy + dy, sx + dx, :3] = bone[0 if dx < 0 else 1] * (1.0 if lit else 0.55)
                for dx in (-1, 1):
                    out[sy, sx + dx if dx > 0 else sx - 1, :3] = bone[2]
            if lit:  # its candle
                fx, fy = cx + 4, cy - 1
                for j in range(3 + (k % 2)):
                    if 0 <= fy - j < h:
                        out[fy - j, fx, :3] = warm[0 if j < 2 else 1]
                out[fy + 1, fx, :3] = rgb("#e8d8b4")
    return out


def arcade(w, h, seed, k, n, body, rim, deep, count=6, lights=0):
    """An arcade of pillars and round arches, candles flickering at the foot of a few pillars."""
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    span = w / count
    pw = span * 0.22
    for i in range(count + 1):
        x = i * span
        d.rectangle([x - pw / 2, h * 0.22, x + pw / 2, h], fill=255)  # the pillar
        d.rectangle([x - pw * 0.75, h * 0.22, x + pw * 0.75, h * 0.27], fill=255)  # its capital
        d.rectangle([x - pw * 0.7, h - 8, x + pw * 0.7, h], fill=255)  # its base
    d.rectangle([0, 0, w, h * 0.12], fill=255)
    for i in range(count):  # the arches between them
        x0, x1 = i * span, (i + 1) * span
        d.rectangle([x0, h * 0.1, x1, h * 0.25], fill=255)
        d.ellipse([x0 + pw / 2, h * 0.14, x1 - pw / 2, h * 0.14 + (x1 - x0 - pw) * 0.75], fill=0)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    rng = np.random.default_rng(seed)
    a = TAU * k / n
    warm = [rgb(c) for c in WARM]
    for i in range(lights):
        x = int((int(rng.integers(0, count + 1)) * span + rng.choice([-1, 1]) * pw * 0.9)) % w
        y = h - 10
        ph = rng.uniform(0, TAU)
        fl = 0.5 + 0.5 * math.sin(a * 2 + ph)
        for j in range(2 + int(fl * 2)):
            if 0 <= y - j < h:
                out[y - j, x, :3] = warm[0 if j < 1 else 1]
                out[y - j, x, 3] = 255
        out[y + 1, x, :3] = rgb("#e8d8b4")
        out[y + 1, x, 3] = 255
    # moss and drips glinting along the arches
    edge = mask & ~np.roll(mask, -1, axis=0)
    ys, xs = np.nonzero(edge)
    sel = ((xs * 0.29 + k * w / (n * 4.0)) % (w / 4.0)) < 1.5
    out[ys[sel], xs[sel], :3] = rgb("#9ab0a0")
    return out


def graves(w, h, seed, k, n, base, body, rim, deep, count=16):
    """A cemetery: headstones and crosses on a slope, small candles flickering on a few graves."""
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    rng = np.random.default_rng(seed)
    xs = np.arange(w)
    ground = base + 4 * np.sin(xs * 0.02 + 1.0) + 2 * np.sin(xs * 0.07)
    lights = []
    for i in range(count):
        x = rng.uniform(4, w - 4)
        gy = ground[int(x)]
        if rng.random() < 0.35:  # a cross
            hh = rng.uniform(10, 16)
            d.rectangle([x - 1, gy - hh, x + 1, gy + 2], fill=255)
            d.rectangle([x - 4, gy - hh * 0.7, x + 4, gy - hh * 0.7 + 2], fill=255)
        else:
            hh, ww = rng.uniform(8, 14), rng.uniform(4, 6)
            d.rectangle([x - ww, gy - hh + ww, x + ww, gy + 2], fill=255)
            d.ellipse([x - ww, gy - hh, x + ww, gy - hh + 2 * ww], fill=255)
        if rng.random() < 0.3:
            lights.append((int(x + 5), int(gy)))
    for x in range(w):
        d.line([(x, int(ground[x])), (x, h)], fill=255)
    mask = np.array(im) > 0
    out = _shade_mask(mask, body, rim, deep)
    a = TAU * k / n
    warm = [rgb(c) for c in WARM]
    for i, (x, y) in enumerate(lights):
        fl = 0.5 + 0.5 * math.sin(a * 2 + i * 1.7)
        if 0 <= x < w:
            for j in range(1 + int(fl * 2.5)):
                if 0 <= y - 1 - j < h:
                    out[y - 1 - j, x, :3] = warm[0 if j < 1 else 1]
                    out[y - 1 - j, x, 3] = 255
    return out


LAYERS = {
    "galerie": (560, 300, 4, 4, lambda k, n: niches(560, 300, 111, k, n, STONE)),
    "arcades": (800, 170, 4, 5, lambda k, n: arcade(800, 170, 112, k, n, "#2a2626", "#7a7068", "#121010", count=6, lights=8)),
    "brume": (640, 48, 4, 3, lambda k, n: mist(640, 48, 113, k, n, col="#a8b8a0", amount=0.42)),
    "dalles": (760, 110, 4, 5, lambda k, n: floor(760, 110, 114, k, n, 12, "#262222", "#6a625a", "#100e0e", rough=1.5, rocks=6,
                                                  specks=60, speck_cols=("#e8e0d0", "#a8a090", "#5a5450"), puddles=3,
                                                  puddle_col="#4a5a5a")),
    "avant": (900, 120, 4, 4, lambda k, n: front_rocks(900, 120, 115, k, n, "#0a0808", "#3a3430", "#040303", spark="#b4f566", rate=0.95)),
    "ciel": (560, 300, 6, 3, lambda k, n: sky(560, 300, NIGHT, 116, k, n)),
    "etoiles": (560, 210, 6, 4, lambda k, n: starfield(560, 210, 360, 117, k, n,
                                                       ("#ffffff", "#f0f0ff", "#d0c8f0", "#8a80b0", "#4a4470"))),
    "collines": (720, 130, 4, 4, lambda k, n: peaks(720, 130, 118, k, n, 10, 30, 70, 116, "#141220", "#4a4060", "#0a0810",
                                                    lean=0.04, wfac=(0.8, 1.4), lights=10, light_cols=WARM, glints="#6a6088")),
    "tombes": (800, 100, 4, 5, lambda k, n: graves(800, 100, 119, k, n, 70, "#1e1a24", "#6a6478", "#0c0a10", count=22)),
}


def _sprites():
    S.chars_path()
    import story_liche_sprites as SP
    return SP.SHEETS


# =================================================================== pieces of scenery
def crypt(tint=None, mist_col=None, far_candles=True, candle_col="#ffb347"):
    """The catacombs: the wall of niches, a far and a near arcade, mist drifting, dust falling,
    candles further away, the flagstones."""
    els = [L.layer("galerie", "galerie", 0.05, (-80, -30), tint=tint),
           L.layer("arcades_loin", "arcades", 0.25, (-200, 150), tint="#605860", offset=0.4),
           L.layer("brume", "brume", 0.3, (-160, 300), drift=(8, 0), repeat=True, alpha=0.7, tint=mist_col),
           particles("poussiere_voute", "dustfall", "#c8c0b0", (0, 40, 960, 380), [K(0, alpha=0.6)], depth=0.5),
           L.layer("arcades", "arcades", 0.5, (-320, 100), tint=tint),
           L.layer("brume_basse", "brume", 0.7, (-300, 380), drift=(-6, 0), repeat=True, alpha=0.45, tint=mist_col, offset=0.5)]
    if far_candles:
        for i, x in enumerate((110, 850)):
            els += candle(f"bougie_loin{i + 1}", (x, 436), [K(0, clip="green" if candle_col != "#ffb347" else "lit", offset=0.3 * i)],
                          color=candle_col, depth=0.8, scale=0.75, radius=55, phase=0.4 * i)
    els.append(L.layer("dalles", "dalles", 1.0, (-280, 400), tint=tint))
    return els


def front(tint=None):
    return [L.layer("avant", "avant", 1.35, (-420, 316), tint=tint)]


def candle(id_, pos, keys, color="#ffb347", depth=1.0, scale=1.0, radius=70, on=0.0, alpha=0.6, period=0.5, phase=0.0):
    """A candle and its light (the light on from `on`)."""
    k0 = [dict(k) for k in keys]
    k0[0] = dict(k0[0], pos=[round(pos[0], 1), round(pos[1], 1)], scale=scale)
    els = [L.sprite(id_, "bougie", k0, depth=depth)]
    gk = [K(0, pos=(pos[0], pos[1] - 28 * scale), alpha=alpha if on <= 0 else 0.0)]
    if on > 0:
        gk += [K(on, alpha=0.0), K(on + 0.2, alpha=alpha, ease="out")]
    els.append(glow(id_ + "_lueur", color, radius, gk, depth=depth, pulse=(0.3, period, phase)))
    return els


# =================================================================== the six shots
def shot_candles(D):
    """Sous les Confins dorment des catacombes. Le vieil Ossian y allumait une bougie pour chaque mort."""
    els = crypt(far_candles=False)
    for i, x in enumerate((180, 860)):  # candles already lit, further away
        els += candle(f"bougie_loin{i + 1}", (x, 432), [K(0, clip="lit", offset=0.2 * i)], depth=0.8, scale=0.8, radius=60, phase=0.3 * i)
    xs = [300, 450, 600, 750]
    walk, pts = [], []
    t = 0.0
    x = 140.0
    keys = [K(0, pos=(x, 450), clip="walk")]
    lit_at = []
    for i, cx in enumerate(xs):
        arrive = 0.9 + 1.5 * i
        keys += [K(arrive, pos=(cx - 34, 450), ease="linear"), K(arrive + 0.01, clip="point", ease="step"),
                 K(arrive + 0.62, clip="walk")]
        lit_at.append(arrive + 0.3)
    keys = keys[:-1] + [K(6.3, clip="idle")]
    keys[-1] = K(lit_at[-1] + 0.4, clip="idle")
    keys.append(K(6.4, clip="look_up", flip=True))
    del walk, pts, t
    els.append(L.sprite("ossian", "ossian", keys, shadow=shade(62, (xs[0], 400))))
    gk = [K(0, pos=(146, 410), alpha=0.6)] + [K(k["t"], pos=(k["pos"][0] + 8, 410)) for k in keys if "pos" in k][1:]
    els.append(glow("bougie_ossian", "#ffb347", 70, gk, pulse=(0.3, 0.45)))
    for i, (cx, tl) in enumerate(zip(xs, lit_at)):
        els += candle(f"bougie{i + 1}", (cx, 452), [K(0, clip="unlit", offset=0.2 * i), K(tl, clip="light")], on=tl, radius=80,
                      phase=0.2 * i)
    els += [particles("poussiere", "dust", "#e8d8b0", (80, 260, 800, 200), [K(0, alpha=0.6)])]
    els += front()
    cam = [K(0, pos=(250, 350), zoom=1.45), K(2.6, pos=(420, 350), zoom=1.42), K(D, pos=(660, 346), zoom=1.38)]
    return dict(backdrop="#060508", camera=cam, elements=els, still=6.0)


def shot_names(D):
    """Il savait tous leurs noms. « Tant qu'on se souvient de toi, disait-il, tu n'es jamais tout à fait parti. »"""
    els = crypt()
    els += candle("bougie1", (250, 452), [K(0, clip="lit")], radius=80)
    els += candle("bougie2", (780, 454), [K(0, clip="lit", offset=0.3)], radius=80, phase=0.4)
    grid = [(400 + 70 * c, 300 + 54 * r) for r in range(2) for c in range(5)]
    for i, p in enumerate(grid):
        t_on = 1.1 + 0.32 * i
        els.append(L.sprite(f"nom{i + 1}", "plaque", [K(0, pos=p, clip="idle", scale=1.1), K(t_on, clip="gold", offset=0.1 * i)], depth=0.9))
        els.append(glow(f"nom{i + 1}_lueur", "#ffcf7a", 34, [K(0, pos=p, alpha=0.0), K(t_on, alpha=0.0), K(t_on + 0.3, alpha=0.5, ease="out")],
                        depth=0.9, pulse=(0.4, 1.4, 0.13 * i)))
    els += [L.sprite("ossian", "ossian", [K(0, pos=(330, 452), clip="idle"), K(0.9, clip="point"), K(4.2, clip="idle"),
                                          K(5.2, clip="kneel")], shadow=shade(62, (250, 400))),
            glow("bougie_ossian", "#ffb347", 70, [K(0, pos=(338, 412), alpha=0.6)], pulse=(0.3, 0.45)),
            L.sprite("enfant", "enfant", [K(0, pos=(560, 456), clip="kneel", flip=True), K(3.0, clip="look_up", flip=False),
                                          K(5.4, clip="idle", flip=True), K(6.4, clip="wave")], shadow=shade(50, (780, 400))),
            particles("poussiere", "dust", "#e8d8b0", (200, 240, 600, 220), [K(0, alpha=0.6)]),
            particles("etincelles", "sparks", "#ffcf7a", (360, 260, 380, 140), [K(0, alpha=0.0), K(1.2, alpha=0.0), K(2.4, alpha=0.8)])]
    els += front()
    cam = [K(0, pos=(430, 344), zoom=1.3), K(3.0, pos=(500, 340), zoom=1.4), K(D, pos=(470, 360), zoom=1.55)]
    return dict(backdrop="#060508", camera=cam, elements=els, still=4.8)


def shot_exodus(D):
    """La nuit où l'étoile du centre s'effondra, les vivants s'enfuirent. Ossian, lui, resta avec ses morts."""
    star = (700, 110)
    els = [L.layer("ciel", "ciel", 0.03, (-80, -30)), L.layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(4, 0), repeat=True)]
    els += L.twinkles(303, 6, (40, 10, 880, 220))
    els += [glow("lueur_etoile", "#fff2c8", 150, [K(0, pos=star, alpha=0.55), K(1.2, alpha=0.7), K(1.6, alpha=1.0), K(2.0, alpha=0.0, ease="in")],
                 depth=0.06),
            glow("lueur_trou", "#c06cf0", 110, [K(0, pos=star, alpha=0.0), K(1.85, alpha=0.0), K(2.0, alpha=0.9, ease="out"), K(D, alpha=0.4)],
                 depth=0.06, pulse=(0.25, 1.4)),
            L.sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=1.4), K(1.2, clip="collapse")], depth=0.06),
            L.layer("collines", "collines", 0.25, (-240, 170)),
            L.layer("brume", "brume", 0.4, (-160, 330), drift=(10, 0), repeat=True, alpha=0.7, tint="#a0a0c8"),
            L.layer("tombes", "tombes", 0.6, (-320, 260)),
            L.layer("dalles", "dalles", 1.0, (-280, 400), tint="#a0a0c0")]
    gate = (200, 448)
    els += [glow("porte_lueur", "#ffb347", 110, [K(0, pos=(gate[0] - 66, gate[1] - 180), alpha=0.5)], pulse=(0.35, 0.5)),
            glow("porte_lueur2", "#ffb347", 110, [K(0, pos=(gate[0] + 66, gate[1] - 180), alpha=0.5)], pulse=(0.35, 0.55, 0.3)),
            L.sprite("porte", "porte", [K(0, pos=gate, clip="idle", scale=1.1)], shadow=shade(260, star, alpha=0.3))]
    t_turn = 5.6
    els += [L.sprite("ossian", "ossian", [K(0, pos=(300, 452), clip="idle"), K(1.0, clip="look_up"), K(3.2, clip="sad"),
                                          K(t_turn, clip="walk", flip=True), K(t_turn + 1.0, pos=(214, 450), alpha=1.0, ease="linear"),
                                          K(min(D, t_turn + 1.5), alpha=0.0)], shadow=shade(62, star)),
            glow("bougie_ossian", "#ffb347", 70, [K(0, pos=(308, 414), alpha=0.6), K(t_turn, pos=(308, 414)),
                                                  K(t_turn + 1.0, pos=(206, 412), ease="linear"), K(min(D, t_turn + 1.5), alpha=0.0)],
                 pulse=(0.3, 0.45))]
    for i, (name, t0, y) in enumerate((("fuyard1", 2.2, 458), ("fuyard2", 2.7, 462), ("fuyard3", 3.1, 466))):
        clip = "flee" if i < 2 else "run"
        els.append(L.sprite(name, name, [K(0, pos=(-60, y), alpha=0.0, clip=clip), K(t0, alpha=0.0, ease="step"),
                                         K(t0 + 0.01, alpha=1.0, ease="step"), K(min(D, t0 + 2.6), pos=(1060, y), ease="linear")],
                            shadow=shade(56, star)))
    els += [particles("etoiles_vives", "stars", "#d8d8ff", (0, 0, 960, 260), [K(0, alpha=0.6), K(2.0, alpha=0.2)], depth=0.1),
            particles("poussiere", "dust", "#a8a8c8", (0, 300, 960, 180), [K(0, alpha=0.5)])]
    els += front(tint="#a0a0c0")
    cam = [K(0, pos=(620, 200), zoom=1.2), K(2.0, pos=(560, 260), zoom=1.16), K(4.0, pos=(440, 330), zoom=1.12), K(D, pos=(330, 350), zoom=1.3)]
    shake = [{"from": 1.9, "to": 2.5, "amount": 3.0}]
    flash = [{"t": 1.92, "in": 0.06, "out": 0.5, "color": "#e0b0ff", "alpha": 0.4}]
    return dict(backdrop="#04030a", camera=cam, elements=els, shake=shake, flash=flash, still=3.4)


def shot_phylactery(D):
    """Il vieillissait. Qui se souviendrait d'eux après lui ? Il cacha son âme dans un cristal, pour ne jamais mourir."""
    dim = "#b0a8b8"
    els = crypt(tint=dim)
    altar = (580, 452)
    crys = (altar[0], altar[1] - 58)
    t_kneel, t_soul, t_in = 2.5, 4.2, 5.6
    els += candle("bougie1", (420, 452), [K(0, clip="lit")], radius=70)
    els += candle("bougie2", (740, 454), [K(0, clip="lit", offset=0.3)], radius=70, phase=0.3)
    els += [L.sprite("autel", "autel", [K(0, pos=altar, clip="idle", scale=1.0)], shadow=shade(200, (420, 400))),
            glow("cristal_lueur", "#b4f566", 90, [K(0, pos=(crys[0], crys[1] - 40), alpha=0.25), K(t_in, alpha=0.25), K(t_in + 0.2, alpha=1.0, scale=1.6),
                                                  K(D, alpha=0.7, scale=1.2)], pulse=(0.3, 1.2)),
            L.sprite("cristal", PHYL, [K(0, pos=crys, clip="idle", scale=0.6)], tint="#d0ffb8"),
            L.sprite("vieux", "vieux", [K(0, pos=(220, 452), clip="walk"), K(t_kneel - 0.1, pos=(486, 452), ease="linear"),
                                        K(t_kneel, clip="kneel"), K(t_in + 0.6, clip="kneel")], shadow=shade(62, (420, 400))),
            L.sprite("ame", "ame", [K(0, pos=(500, 412), alpha=0.0, scale=0.6), K(t_soul, alpha=0.0, ease="step"),
                                    K(t_soul + 0.02, alpha=1.0, clip="idle", ease="step"), K(t_soul + 0.7, pos=(530, 330), scale=1.0, ease="out"),
                                    K(t_in, pos=(crys[0], crys[1] - 30), scale=0.5, ease="in"), K(t_in + 0.05, alpha=0.0, ease="step")]),
            glow("ame_lueur", "#b4f566", 60, [K(0, pos=(500, 400), alpha=0.0), K(t_soul, alpha=0.0), K(t_soul + 0.2, alpha=0.8),
                                              K(t_soul + 0.7, pos=(530, 320), ease="out"), K(t_in, pos=(crys[0], crys[1] - 40), ease="in"),
                                              K(t_in + 0.1, alpha=0.0)]),
            particles("ames", "sparks", "#b4f566", (460, 280, 200, 160), [K(0, alpha=0.0), K(t_soul, alpha=0.0), K(t_soul + 0.4, alpha=1.0),
                                                                           K(D, alpha=0.6)]),
            particles("poussiere", "dust", "#d8d0c0", (200, 240, 600, 220), [K(0, alpha=0.5)])]
    els += front(tint=dim)
    cam = [K(0, pos=(400, 350), zoom=1.36), K(t_kneel, pos=(500, 348), zoom=1.45), K(t_in, pos=(540, 336), zoom=1.62), K(D, pos=(548, 330), zoom=1.7)]
    shake = [{"from": t_in, "to": t_in + 0.5, "amount": 2.5}]
    flash = [{"t": t_in + 0.05, "in": 0.08, "out": 0.6, "color": "#d8ffb0", "alpha": 0.55}]
    return dict(backdrop="#060508", camera=cam, elements=els, shake=shake, flash=flash, still=4.9)


def shot_bones(D):
    """Les siècles rongèrent sa chair jusqu'aux os. Dans le cristal, son âme brûlait encore, d'une flamme verte."""
    dim = "#a8a0b0"
    els = crypt(tint=dim)
    chair = (470, 452)
    CS = 1.2
    t0, t1 = 2.4, 4.6
    els += [L.sprite("chaise", "chaise", [K(0, pos=chair, clip="idle", scale=CS)], shadow=shade(80, (640, 380))),
            L.sprite("vieux", "vieux", [K(0, pos=(chair[0] - 6, chair[1]), clip="sit", scale=CS), K(t0, alpha=1.0), K(t1, alpha=0.0)]),
            L.sprite("squelette", "squelette", [K(0, pos=chair, clip="idle", scale=CS, alpha=0.0), K(t0, alpha=0.0), K(t1, alpha=1.0)]),
            L.sprite("toile1", "toile", [K(0, pos=(chair[0] - 10, chair[1] - 92), alpha=0.0, scale=0.9), K(1.0, alpha=0.0, ease="step"),
                                         K(1.02, alpha=1.0, clip="grow", ease="step")]),
            L.sprite("toile2", "toile", [K(0, pos=(300, 250), alpha=0.0, scale=1.6), K(2.2, alpha=0.0, ease="step"),
                                         K(2.22, alpha=1.0, clip="grow", ease="step")], depth=0.5),
            L.sprite("socle", "autel", [K(0, pos=(640, 452), clip="idle", scale=0.7)], shadow=shade(130, (640, 380))),
            glow("cristal_lueur", "#b4f566", 80, [K(0, pos=(640, 360), alpha=0.5), K(t1, alpha=0.9, scale=1.4), K(D, alpha=1.0, scale=1.5)],
                 pulse=(0.3, 1.1)),
            L.sprite("cristal", PHYL, [K(0, pos=(640, 412), clip="idle", scale=0.55)], tint="#d0ffb8")]
    for i, (x, t_out) in enumerate(((330, 3.6), (380, 4.4), (760, 5.2))):  # the candles burn down and go out
        els += [L.sprite(f"bougie{i + 1}", "bougie", [K(0, pos=(x, 452), clip="lit", offset=0.2 * i), K(t_out - 1.6, clip="low"),
                                                      K(t_out, clip="out")]),
                glow(f"bougie{i + 1}_lueur", "#ffb347", 70, [K(0, pos=(x, 424), alpha=0.6), K(t_out - 1.6, alpha=0.4), K(t_out, alpha=0.0)],
                     pulse=(0.3, 0.5, 0.2 * i))]
    els += [particles("chute", "dustfall", "#c8c0b0", (200, 100, 560, 360), [K(0, alpha=0.5), K(t0, alpha=1.0)]),
            particles("ame", "sparks", "#b4f566", (580, 300, 120, 140), [K(0, alpha=0.6), K(t1, alpha=1.0)])]
    els += front(tint=dim)
    cam = [K(0, pos=(520, 360), zoom=1.36), K(t1, pos=(520, 356), zoom=1.5), K(D, pos=(540, 350), zoom=1.62)]
    return dict(backdrop="#060508", camera=cam, elements=els, still=6.6)


def shot_lich(D):
    """La Liche veille toujours sur sa crypte. Elle a oublié son propre nom… mais pas un seul des leurs."""
    tint = "#a8c8a0"
    els = crypt(tint=tint, mist_col="#b4f566", candle_col="#b4f566")
    for i, p in enumerate([(150 + 75 * c, 290 + 52 * r) for r in range(2) for c in range(3)] + [(660 + 75 * c, 290 + 52 * r) for r in range(2)
                                                                                                for c in range(3)]):
        els.append(L.sprite(f"nom{i + 1}", "plaque", [K(0, pos=p, clip="green", offset=0.13 * i, scale=1.0)], depth=0.85))
    for i, x in enumerate((240, 340, 620, 720)):
        els += candle(f"bougie{i + 1}", (x, 452), [K(0, clip="green", offset=0.15 * i)], color="#b4f566", radius=70, phase=0.25 * i)
    lich = (480, 446)
    t_call = 2.6
    els += [glow("liche_lueur", "#b4f566", 220, [K(0, pos=(lich[0], lich[1] - 160), alpha=0.35), K(t_call, alpha=0.35),
                                                 K(t_call + 0.4, alpha=0.7), K(D, alpha=0.5)], pulse=(0.25, 1.6)),
            L.sprite("liche", BOSS, [K(0, pos=lich, clip="idle", scale=0.82), K(t_call, clip="summon"), K(t_call + 1.0, clip="idle")],
                     shadow=shade(180, alpha=0.5))]
    for i in range(4):  # the names, souls of light circling it
        keys = []
        for j in range(9):
            t = D * j / 8
            a = TAU * (j / 8 * 0.9 + i / 4)
            keys.append(K(t, pos=(lich[0] + math.cos(a) * 170, lich[1] - 150 + math.sin(a) * 50), ease="linear",
                          clip="idle" if j == 0 else None, offset=0.17 * i if j == 0 else None,
                          scale=round(0.75 + 0.2 * math.sin(a), 2)))
        els.append(L.sprite(f"ame{i + 1}", "ame", keys, blend="add"))
    els += [L.sprite("cristal", PHYL, [K(0, pos=(800, 452), clip="idle", scale=0.6)], tint="#d0ffb8"),
            particles("etincelles", "sparks", "#b4f566", (200, 180, 560, 260), [K(0, alpha=0.6), K(t_call, alpha=1.0)]),
            particles("poussiere", "dust", "#c8e0b0", (80, 240, 800, 220), [K(0, alpha=0.5)])]
    els += front(tint=tint)
    cam = [K(0, pos=(480, 300), zoom=1.28), K(t_call, pos=(480, 296), zoom=1.2), K(D, pos=(480, 290), zoom=1.08)]
    shake = [{"from": t_call, "to": t_call + 0.6, "amount": 3.0}]
    flash = [{"t": t_call + 0.1, "in": 0.08, "out": 0.5, "color": "#d8ffb0", "alpha": 0.4}]
    return dict(backdrop="#060508", camera=cam, elements=els, shake=shake, flash=flash, still=5.2)


SHOTS = [shot_candles, shot_names, shot_exodus, shot_phylactery, shot_bones, shot_lich]


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    return S.generate("liche", "story_liche_scene", SHOTS, _sprites(), LAYERS, review, SOUNDS)


# =================================================================== the sounds (2.63)
from story_stage import amb, sfx  # noqa: E402


def _snd_candles(D):
    return [amb("story_amb_crypt", 0.5)] + [sfx("story_candle", 0.9 + 1.5 * i + 0.3, 0.55, at=(x, 440))
                                            for i, x in enumerate((300, 450, 600, 750))]


def _snd_names(D):
    return [amb("story_amb_crypt", 0.45), sfx("story_glow", 1.1, 0.35), sfx("story_glow", 2.4, 0.3, pitch=1.2)]


def _snd_exodus(D):
    return [amb("story_amb_night", 0.45), sfx("story_collapse", 1.2, 0.9, at=(700, 110), duck=True)] + [
        sfx("footstep_a" if i % 2 else "footstep_b", 2.4 + 0.3 * i, 0.3, pan=-0.8 + 0.3 * i) for i in range(6)] + [
        sfx("door_open", 6.3, 0.4, at=(200, 400))]


def _snd_phylactery(D):
    return [amb("story_amb_crypt", 0.45), sfx("story_soul", 4.2, 0.7, at=(500, 400)), sfx("power_unlock", 5.6, 0.55, duck=True)]


def _snd_bones(D):
    return [amb("story_amb_crypt", 0.5)] + [sfx("story_puff", t, 0.2, pitch=1.6, at=(x, 440)) for t, x in ((3.6, 330), (4.4, 380), (5.2, 760))] + [
        sfx("story_bones", 4.6, 0.55, at=(470, 420))]


def _snd_lich(D):
    return [amb("story_amb_crypt", 0.5), sfx("story_soul", 0.5, 0.35), sfx("summon", 2.6, 0.7, duck=True), sfx("story_bones", 2.7, 0.45)]


SOUNDS = {"shot_candles": _snd_candles, "shot_names": _snd_names, "shot_exodus": _snd_exodus, "shot_phylactery": _snd_phylactery,
          "shot_bones": _snd_bones, "shot_lich": _snd_lich}
