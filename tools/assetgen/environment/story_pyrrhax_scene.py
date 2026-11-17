"""Pyrrhax's story as six animated shots (« Histoire des giga-boss », version 2 of the format, 2.60).

Every shot is a little scene: parallax layers of scenery (story_pyrrhax_layers.py), sprites
playing their clips frame by frame (characters/story_pyrrhax_sprites.py, and the boss's own sheet),
glows and live particles, moved by simple keyframes; a camera pans and zooms, the screen shakes
or flashes.  This module writes the scenes into data/stories/pyrrhax.json (the lines of narration
are kept as written there), and renders each shot's still from its scene (stories/pyrrhax_<n>.png:
the shot's image when its scene cannot be played, and the story's thumbnails).

The format (BossStories.h): a shot's « stage » has a size (960x540 units), a backdrop colour, camera
keys {t, pos, zoom}, shakes {from, to, amount}, flashes {t, in, out, color, alpha} and elements
drawn in order.  An element is a layer (an image), a sprite (an animation file), a glow (a colour)
or particles (a kind of the story's particles in an area); it has a depth (parallax: 0 never moves,
1 moves with the camera, more is in front), a blend, a tint and keys {t, pos, scale, alpha, rot,
flip, clip, offset, ease}: what a key does not give it keeps from the key before; pos, scale, alpha
and rot go from one key to the next with that key's ease, flip and clip change at its time (a clip
starts at its key, `offset` seconds in).  One image pixel is two units (scale 1).  The times are
seconds of the shot as written in French (max(4.5 s, 1.2 s + characters / 16)): the game stretches
them to the shot's length in the language shown.

Deterministic (fixed seeds)."""
import json
import math
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
ASSETS = os.path.join(REPO, "assets")
STORY_JSON = os.path.join(REPO, "data", "stories", "pyrrhax.json")
W, H = 960, 540
PIX = 2.0  # units per image pixel

L = "stories/pyrrhax/"
A = "animations/stories/pyrrhax_"
BOSS = "animations/pyrrhax.json"


def shot_duration(text):
    return max(4.5, 1.2 + len(text) / 16.0)


# =================================================================== helpers to write scenes
def K(t, **kw):
    k = {"t": round(float(t), 3)}
    for name, v in kw.items():
        if v is None:
            continue
        if name == "pos":
            v = [round(float(v[0]), 1), round(float(v[1]), 1)]
        elif isinstance(v, float):
            v = round(v, 3)
        k[name] = v
    return k


def layer(id_, image, depth, pos, alpha=1.0, drift=None, repeat=False, tint=None, keys=None, blend=None, offset=0.0,
          pulse=None):
    """A layer of scenery: an animated sheet (story_pyrrhax_layers.py, its point at its top-left)."""
    e = {"id": id_, "sprite": A + image + ".json", "depth": depth}
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    if repeat:
        e["repeat"] = True
    if tint:
        e["tint"] = tint
    if blend:
        e["blend"] = blend
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    if keys:
        keys[0].setdefault("clip", "idle")
        if offset:
            keys[0]["offset"] = round(float(offset), 3)
    e["keys"] = keys or [K(0, pos=pos, alpha=alpha if alpha != 1.0 else None, clip="idle", offset=offset or None)]
    return e


def sprite(id_, anim, keys, depth=1.0, blend=None, tint=None, shadow=None, pulse=None, drift=None):
    e = {"id": id_, "sprite": anim if anim.endswith(".json") else A + anim + ".json", "depth": depth}
    if blend:
        e["blend"] = blend
    if tint:
        e["tint"] = tint
    if shadow:
        e["shadow"] = shadow
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    if drift:
        e["drift"] = [float(drift[0]), float(drift[1])]
    e["keys"] = keys
    return e


def shade(width, light=None, alpha=0.45):
    """A soft shadow on the ground under a sprite, pushed away from `light`."""
    sh = {"alpha": alpha, "size": [float(width), round(width * 0.16, 1)]}
    if light:
        sh["light"] = [float(light[0]), float(light[1])]
    return sh


def glow(id_, color, radius, keys, depth=1.0, pulse=None):
    e = {"id": id_, "glow": color, "radius": float(radius), "depth": depth, "keys": keys}
    if pulse:
        e["pulse"] = [float(v) for v in pulse]
    return e


def particles(id_, kind, color, area, keys, depth=1.0):
    return {"id": id_, "particles": kind, "color": color, "area": [float(v) for v in area], "depth": depth, "keys": keys}


def twinkles(seed, n, area, depth=0.08, dur=10.0):
    rng = np.random.default_rng(seed)
    out = []
    for i in range(n):
        x = rng.uniform(area[0], area[0] + area[2])
        y = rng.uniform(area[1], area[1] + area[3])
        out.append(sprite(f"astre{i + 1}", "astre", [K(0, pos=(x, y), clip="twinkle", offset=float(rng.uniform(0, 1.2)),
                                                       scale=float(rng.choice([1.0, 1.0, 1.5])))], depth=depth))
    return out


def sky(name="ciel_nuit", stars=True, seed=1, extra=None, clouds=True):
    """The sky: its nebula shimmering, the stars twinkling and drifting, two bands of smoke clouds
    drifting at their own speeds (the farther, the slower)."""
    els = [layer("ciel", name, 0.03, (-80, -30))]
    if extra:
        els += extra
    if stars:
        els.append(layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(5, 0), repeat=True))
        els += twinkles(seed, 7, (40, 20, 880, 250))
    if clouds:
        els.append(layer("nuages_loin", "nuages", 0.12, (-200, 150), drift=(-9, 0), repeat=True, alpha=0.75, offset=0.3))
        els.append(layer("nuages", "nuages", 0.2, (-300, 40), drift=(16, 0), repeat=True, alpha=0.9))
    return els


def hills(mist=True, far_tint=None, seed=7, props=True):
    """Spires far and near (their magma breathing), mist drifting between them, dust falling from
    them, the ground with its embers; small flames and cracks of lava on it, each lighting the
    ground around it, ash blown along by the wind."""
    els = [layer("pics_loin", "pics_loin", 0.25, (-240, 170), tint=far_tint)]
    if mist:
        els.append(layer("brume", "brume", 0.4, (-160, 330), drift=(12, 0), repeat=True, alpha=0.9))
    els.append(layer("pics", "pics", 0.5, (-320, 140), offset=0.2))
    els.append(particles("poussiere_pics", "dustfall", "#d0a0a0", (0, 160, 960, 260), [K(0, alpha=0.8)], depth=0.5))
    els.append(layer("sol", "sol", 1.0, (-280, 400)))
    if props:
        rng = np.random.default_rng(seed)
        for i, x in enumerate((rng.uniform(-60, 120), rng.uniform(820, 1000))):
            y = rng.uniform(452, 470)
            els.append(sprite(f"lave{i + 1}", "lave", [K(0, pos=(x, y), clip="idle", offset=float(rng.uniform(0, 0.8)))]))
            els.append(glow(f"lave{i + 1}_lueur", "#ff6a2a", 70, [K(0, pos=(x, y - 10), alpha=0.55)],
                            pulse=(0.5, float(rng.uniform(1.2, 2.0)), float(rng.uniform(0, 1)))))
        for i, x in enumerate((rng.uniform(140, 220), rng.uniform(700, 800), rng.uniform(1000, 1100))):
            y = rng.uniform(446, 460)
            els.append(sprite(f"flammeches{i + 1}", "flammeches", [K(0, pos=(x, y), clip="idle", offset=float(rng.uniform(0, 0.6)))]))
            els.append(glow(f"flammeches{i + 1}_lueur", "#ffa040", 46, [K(0, pos=(x, y - 14), alpha=0.6)],
                            pulse=(0.45, float(rng.uniform(0.35, 0.6)), float(rng.uniform(0, 1)))))
    els.append(particles("cendres", "ash", "#c8b0b0", (0, 330, 960, 160), [K(0, alpha=0.9)]))
    return els


def front():
    return [layer("avant", "avant", 1.35, (-420, 316))]


# The hatchlings' rig: where their mouth is (fire, smoke, embers) in a pose of a clip.
_RIG = {}


def mouth_offset(sheet, clip, frame):
    """Units from a hatchling's pivot to its mouth in that frame (facing right, scale 1)."""
    import sys
    chars = os.path.join(os.path.dirname(HERE), "characters")
    if chars not in sys.path:
        sys.path[:0] = [chars, os.path.dirname(HERE)]
    import story_pyrrhax_sprites as S
    from story_dragonet import DPose, Dragonet
    grow = {"fumerolle": 1.0, "frere": 1.15, "soeur": 1.12}[sheet]
    d = Dragonet({"fumerolle": "grey", "frere": "fire", "soeur": "crimson"}[sheet])
    poses = {
        "fire": DPose(jaw=1.0, hx=1.5, grow=grow),
        "blow": DPose(jaw=0.85, head=0.08, hx=1.0, grow=grow),
        "cough": DPose(head=0.32, hy=1.5, jaw=0.45, grow=grow),
        "swallow": DPose(jaw=1.0, head=-0.5, hy=-1.0, grow=grow),
    }
    p = poses[clip]
    (mx, my), _ = d.mouth(p)
    q = d.frame_point(p, (mx, my))
    pv = S.SHEETS[sheet][2]
    return ((q[0] - pv[0]) * PIX, (q[1] - pv[1]) * PIX)


# =================================================================== the six shots
NEST = (480, 440)


def shot_nest(D):
    """Aux Confins, dans un nid de cendres tièdes, éclot le plus petit d'une couvée de dragonnets."""
    els = sky("ciel_nuit", True, 101) + hills()
    nest = (480, 440)
    els += [
        sprite("volute1", "volute", [K(0, pos=(400, 436), clip="rise", alpha=0.7)]),
        sprite("volute2", "volute", [K(0, pos=(570, 432), clip="rise", offset=0.5, alpha=0.6, flip=True)]),
        sprite("nid_fond", "nid", [K(0, pos=nest, clip="back")]),
        sprite("frere_fond", "frere", [K(0, pos=(598, 410), clip="watch", flip=True, scale=0.8), K(3.75, clip="cheer"),
                                       K(5.4, clip="watch", offset=0.3)], depth=0.96, shadow=shade(100, NEST)),
        sprite("coquilles", "coquilles", [K(0, pos=(372, 452), clip="idle")]),
        sprite("coquilles2", "coquilles", [K(0, pos=(600, 450), clip="idle", flip=True, offset=0.4)]),
        sprite("frere", "frere", [K(0, pos=(262, 444), clip="watch"), K(3.7, clip="cheer"), K(5.6, clip="watch")],
               shadow=shade(120, NEST)),
        sprite("soeur", "soeur", [K(0, pos=(768, 448), clip="watch", flip=True, offset=0.2), K(3.85, clip="cheer"),
                                  K(5.5, clip="idle")], shadow=shade(120, NEST)),
        glow("lueur_nid", "#ff8a3a", 240, [K(0, pos=(480, 440), alpha=0.3), K(2.2, alpha=0.36), K(3.5, alpha=0.62, ease="out"),
                                           K(D, alpha=0.4)], pulse=(0.3, 1.7)),
        sprite("oeuf", "oeuf", [K(0, pos=(480, 452), clip="idle", scale=1.25), K(2.2, clip="crack"), K(3.5, clip="hatch")]),
        sprite("coquille", "coquille", [K(0, pos=(480, 392), alpha=0.0, scale=1.25), K(3.6, alpha=0.0, ease="step"),
                                        K(3.62, alpha=1.0, clip="spin", ease="step"),
                                        K(4.05, pos=(548, 330), ease="out"), K(4.5, pos=(596, 452), ease="in"),
                                        K(4.52, clip="rest")]),
        sprite("nid_devant", "nid", [K(0, pos=nest, clip="front")]),
        glow("eclat", "#fff0c0", 110, [K(0, pos=(480, 420), alpha=0.0), K(3.5, alpha=0.0), K(3.62, alpha=0.9, ease="out"),
                                       K(4.6, alpha=0.15), K(D, alpha=0.1)]),
        particles("braises", "embers", "#ffb066", (260, 200, 440, 260), [K(0, alpha=0.9)]),
    ]
    els += front()
    cam = [K(0, pos=(400, 250), zoom=1.06), K(1.0, pos=(420, 262), zoom=1.08), K(3.2, pos=(480, 362), zoom=1.3),
           K(D, pos=(512, 370), zoom=1.38)]
    return dict(backdrop="#0a0414", camera=cam, elements=els, still=4.6)


def shot_smoke(D):
    """Ses frères crachaient des flammes. Lui ne soufflait qu'un nuage de fumée. On l'appelait « la Fumerolle »."""
    els = sky("ciel_nuit", True, 202) + hills()
    fx, sx, fumx = (500, 434), (676, 442), (290, 436)
    mf = mouth_offset("frere", "fire", 0)
    ms = mouth_offset("soeur", "fire", 0)
    mb = mouth_offset("fumerolle", "blow", 3)
    mc = mouth_offset("fumerolle", "cough", 1)
    f_on, f_off, f_rot = 0.78, 2.7, -16.0
    s_on, s_off, s_rot = 1.38, 3.1, -9.0

    def turned(m, deg):
        a = math.radians(deg)
        return (m[0] * math.cos(a) - m[1] * math.sin(a), m[0] * math.sin(a) + m[1] * math.cos(a))

    def flame(id_, who, mouth, on, off, rot):
        m = turned(mouth, rot)
        x, y = who[0] + m[0], who[1] + m[1]
        r = rot - 8.0
        gx, gy = x + 120 * math.cos(math.radians(r)), y + 120 * math.sin(math.radians(r))
        return [sprite(id_, "flamme", [K(0, pos=(x, y), alpha=0.0, rot=r), K(on - 0.01, alpha=0.0, ease="step"),
                                       K(on, alpha=1.0, clip="start", ease="step"), K(off, clip="end"),
                                       K(off + 0.2, alpha=0.0, ease="step")], blend="add"),
                glow(id_ + "_lueur", "#ff8a3a", 150, [K(0, pos=(gx, gy), alpha=0.0), K(on, alpha=0.0),
                                                      K(on + 0.2, alpha=0.75), K(off, alpha=0.7), K(off + 0.3, alpha=0.0)])]

    els += [
        sprite("coquilles", "coquilles", [K(0, pos=(420, 450), clip="idle", offset=0.2)]),
        sprite("soeur", "soeur", [K(0, pos=sx, clip="idle", offset=0.3), K(0.9, clip="inhale"), K(1.35, rot=s_rot),
                                  K(s_off, rot=s_rot), K(s_off + 0.15, clip="idle"), K(s_off + 0.45, rot=0.0),
                                  K(5.05, clip="laugh", flip=True, offset=0.15)], shadow=shade(120, (900, 300))),
        sprite("frere", "frere", [K(0, pos=fx, clip="idle"), K(0.3, clip="inhale"), K(0.75, rot=f_rot), K(f_off, rot=f_rot),
                                  K(f_off + 0.15, clip="idle"), K(f_off + 0.45, rot=0.0), K(4.9, clip="laugh", flip=True)],
               shadow=shade(124, (900, 300))),
        sprite("fumerolle", "fumerolle", [K(0, pos=fumx, clip="idle"), K(3.3, clip="inhale"), K(3.96, clip="blow"),
                                          K(4.96, clip="cough"), K(5.94, clip="sad")], shadow=shade(110, (700, 300))),
    ]
    els += flame("flamme2", sx, ms, s_on, s_off, s_rot) + flame("flamme1", fx, mf, f_on, f_off, f_rot)
    px, py = fumx[0] + mb[0], fumx[1] + mb[1]
    cx, cy = fumx[0] + mc[0], fumx[1] + mc[1]
    els += [
        sprite("fumee", "fumee", [K(0, pos=(px, py), alpha=0.0, scale=1.7), K(4.05, alpha=0.0, ease="step"),
                                  K(4.06, alpha=1.0, clip="puff", ease="step"), K(5.5, pos=(px + 30, py - 16), ease="linear")]),
        sprite("toux1", "fumee", [K(0, pos=(cx, cy), alpha=0.0), K(5.06, alpha=0.0, ease="step"),
                                  K(5.07, alpha=0.9, clip="small", ease="step"), K(5.7, pos=(cx + 10, cy - 12), ease="linear")]),
        sprite("toux2", "fumee", [K(0, pos=(cx, cy + 4), alpha=0.0), K(5.66, alpha=0.0, ease="step"),
                                  K(5.67, alpha=0.9, clip="small", ease="step"), K(6.3, pos=(cx + 12, cy - 10), ease="linear")]),
        particles("etincelles", "embers", "#ffa040", (560, 300, 400, 160),
                  [K(0, alpha=0.0), K(0.8, alpha=0.0), K(1.2, alpha=1.0), K(3.0, alpha=1.0), K(3.6, alpha=0.2)]),
        particles("poussiere", "dust", "#d8cfc4", (120, 260, 720, 200), [K(0, alpha=0.5)]),
    ]
    els += front()
    cam = [K(0, pos=(620, 330), zoom=1.3), K(2.6, pos=(640, 330), zoom=1.32), K(3.4, pos=(370, 370), zoom=1.5),
           K(4.8, pos=(366, 372), zoom=1.55), K(5.6, pos=(480, 340), zoom=1.12), K(D, pos=(474, 344), zoom=1.16)]
    return dict(backdrop="#0a0414", camera=cam, elements=els, still=1.9)


def _fall(i, x0, t0, dur, depth, rng, land_y=436, scale=1.0, impact=True, end=None):
    """A falling ember from (x0, -40) to the ground, then its impact (cut at `end`, the shot's end)."""
    dx, dy = -0.5 * (land_y + 40), land_y + 40
    x1, y1 = x0 + dx, land_y
    ang = math.degrees(math.atan2(dy, dx)) - 135.0
    keys = [K(0, pos=(x0, -40), alpha=0.0, rot=ang, scale=scale), K(t0, alpha=0.0, ease="step"),
            K(t0 + 0.01, alpha=1.0, clip="fall", ease="step", offset=float(rng.uniform(0, 0.3)))]
    if end is not None and t0 + dur > end:  # still falling when the shot ends
        u = (end - t0) / dur
        keys.append(K(end, pos=(x0 + dx * u, -40 + (y1 + 40) * u), ease="linear"))
        return sprite(f"braise{i}", "braise", keys, depth=depth)
    keys.append(K(t0 + dur, pos=(x1, y1), ease="linear"))
    if impact:
        keys += [K(t0 + dur + 0.01, clip="impact", rot=0.0, ease="step")]
        if end is None or t0 + dur + 0.5 <= end:
            keys.append(K(t0 + dur + 0.5, alpha=0.0, ease="step"))
    elif end is None or t0 + dur + 0.01 <= end:
        keys += [K(t0 + dur + 0.01, alpha=0.0, ease="step")]
    return sprite(f"braise{i}", "braise", keys, depth=depth)


def _impact_light(i, x0, t0, dur, depth, land_y, end):
    """The ground lit where an ember lands, the light dying out."""
    x1 = x0 - 0.5 * (land_y + 40)
    t1 = t0 + dur
    if t1 + 0.05 > end:
        return None
    keys = [K(0, pos=(x1, land_y - 6), alpha=0.0), K(t1 - 0.05, alpha=0.0), K(t1 + 0.05, alpha=0.85, ease="out")]
    keys.append(K(min(end, t1 + 0.9), alpha=0.0, ease="in"))
    return glow(f"braise{i}_lueur", "#ff7a2a", 60, keys, depth=depth)


def shot_ember_rain(D):
    """La nuit où l'étoile du centre s'effondra, une pluie de braises tomba sur les Confins."""
    rng = np.random.default_rng(303)
    star = (600, 150)
    extra = [layer("ciel_feu", "ciel_rouge", 0.03, (-80, -30), keys=[K(0, pos=(-80, -30), alpha=0.0), K(1.9, alpha=0.0),
                                                                     K(3.4, alpha=0.75)])]
    els = sky("ciel_nuit", True, 303, extra)
    els += [
        glow("lueur_etoile", "#fff2c8", 150, [K(0, pos=star, alpha=0.55), K(1.2, alpha=0.7), K(1.6, alpha=1.0),
                                              K(2.0, alpha=0.0, ease="in")], depth=0.06),
        glow("lueur_trou", "#c06cf0", 120, [K(0, pos=star, alpha=0.0), K(1.85, alpha=0.0), K(2.0, alpha=0.9, ease="out"),
                                            K(3.0, alpha=0.4), K(D, alpha=0.35)], depth=0.06),
        sprite("etoile", "etoile", [K(0, pos=star, clip="shine", scale=1.5), K(1.2, clip="collapse")], depth=0.06),
    ]
    els += hills()
    # the rain: far embers first (slower, smaller), then near ones
    t = 2.0
    k = 1
    while t < D - 0.4:
        far = k % 3 == 1
        x0 = rng.uniform(280, 1300)
        dur = rng.uniform(1.3, 1.9) if not far else rng.uniform(1.9, 2.4)
        land = 410 if far else rng.uniform(430, 452)
        els.append(_fall(k, x0, t, dur, 0.55 if far else 1.0, rng, land_y=land, scale=0.8 if far else 1.35, impact=not far, end=D))
        if not far:
            lit = _impact_light(k, x0, t, dur, 1.0, land, D)
            if lit:
                els.append(lit)
        k += 1
        t += rng.uniform(0.07, 0.18)
    els += [
        sprite("fumerolle", "fumerolle", [K(0, pos=(250, 436), clip="walk"), K(2.3, pos=(420, 436), ease="linear"),
                                          K(2.31, clip="idle", ease="step"), K(2.8, clip="look_up")], shadow=shade(110, star)),
        particles("braises", "embers", "#ff8a3a", (100, 180, 760, 280), [K(0, alpha=0.0), K(2.2, alpha=0.0), K(3.0, alpha=1.0)]),
        particles("etincelles", "sparks", "#ffb070", (100, 340, 760, 110), [K(0, alpha=0.0), K(3.2, alpha=0.0), K(3.8, alpha=0.9)]),
        particles("pluie", "ember_rain", "#ff9a40", (0, -40, 1000, 520), [K(0, alpha=0.0), K(2.0, alpha=0.0), K(2.8, alpha=1.0)],
                  depth=0.7),
    ]
    els += front()
    cam = [K(0, pos=(570, 190), zoom=1.32), K(2.2, pos=(560, 200), zoom=1.3), K(4.4, pos=(450, 352), zoom=1.2),
           K(D, pos=(446, 358), zoom=1.26)]
    shake = [{"from": 1.9, "to": 2.6, "amount": 4.0}]
    flash = [{"t": 1.92, "in": 0.06, "out": 0.5, "color": "#e0b0ff", "alpha": 0.45}]
    return dict(backdrop="#0a0414", camera=cam, elements=els, shake=shake, flash=flash, still=4.6)


def shot_swallow(D):
    """La Fumerolle les avala une à une, jusqu'à ce que son ventre brille comme une forge."""
    rng = np.random.default_rng(404)
    fum = (470, 436)
    els = sky("ciel_rouge", True, 404) + hills(mist=True)
    m = mouth_offset("fumerolle", "swallow", 1)
    mouth = (fum[0] + m[0], fum[1] + m[1])
    swallows = [(0.8, "swallow"), (1.9, "swallow_more"), (3.0, "swallow_last")]
    # background rain, far
    for j in range(6):
        els.append(_fall(30 + j, rng.uniform(500, 1200), 0.2 + j * 0.9, 2.2, 0.55, rng, land_y=410, scale=0.7, impact=False, end=D))
    els.append(particles("pluie", "ember_rain", "#ff8a3a", (0, -40, 1000, 470), [K(0, alpha=0.55)], depth=0.5))
    belly =(fum[0] + (63 - 56) * PIX, fum[1] - (74 - 61) * PIX)
    els.append(glow("forge", "#ff7a2a", 300, [K(0, pos=belly, alpha=0.0), K(1.5, alpha=0.1), K(2.9, alpha=0.2),
                                              K(4.1, alpha=0.45, ease="out"), K(D, alpha=0.4)]))
    keys = [K(0, pos=fum, clip="idle")]
    for (t, c) in swallows:
        keys.append(K(t, clip=c))
    keys.append(K(4.06, clip="glow"))
    els.append(sprite("fumerolle", "fumerolle", keys, shadow=shade(110)))
    # the three embers, floating down to its open mouth
    for i, (t, c) in enumerate(swallows):
        arrive = t + 0.24
        start = arrive - 1.25
        p0 = (mouth[0] + 260 - 40 * i, mouth[1] - 230 + 30 * i)
        p1 = (mouth[0] + 90, mouth[1] - 120)
        els.append(sprite(f"braise{i + 1}", "braise",
                          [K(0, pos=p0, alpha=0.0, scale=1.2), K(max(0.0, start - 0.2), alpha=0.0),
                           K(max(0.01, start), alpha=1.0, clip="float", offset=0.1 * i),
                           K(start + 0.7, pos=p1, ease="out"), K(arrive, pos=mouth, scale=0.6, ease="in"),
                           K(arrive + 0.01, alpha=0.0, ease="step")]))
        els.append(glow(f"braise{i + 1}_lueur", "#ffb347", 46, [K(0, pos=p0, alpha=0.0), K(max(0.0, start - 0.2), alpha=0.0),
                                                                K(max(0.01, start), alpha=0.8), K(start + 0.7, pos=p1, ease="out"),
                                                                K(arrive, pos=mouth, ease="in"), K(arrive + 0.01, alpha=0.0, ease="step")]))
    els += [
        glow("ventre", "#ffb347", 40, pulse=(0.25, 0.7), keys=[K(0, pos=belly, alpha=0.0, scale=0.6), K(1.4, alpha=0.0), K(1.6, alpha=0.25, scale=0.8),
                                       K(2.7, alpha=0.25), K(2.9, alpha=0.4, scale=1.0), K(3.8, alpha=0.4),
                                       K(4.0, alpha=0.55, scale=1.3), K(D, alpha=0.5, scale=1.4)]),
        particles("etincelles", "sparks", "#ffc060", (370, 300, 220, 150), [K(0, alpha=0.0), K(3.9, alpha=0.0), K(4.4, alpha=1.0)]),
        particles("braises", "embers", "#ff8a3a", (300, 160, 400, 300), [K(0, alpha=0.4), K(4.0, alpha=0.4), K(4.6, alpha=0.9)]),
    ]
    els += front()
    cam = [K(0, pos=(530, 352), zoom=2.0), K(1.2, pos=(520, 360), zoom=2.06), K(4.0, pos=(500, 376), zoom=2.24),
           K(D, pos=(496, 380), zoom=2.34)]
    return dict(backdrop="#12040e", camera=cam, elements=els, still=5.2)


def shot_glass(D):
    """Le feu d'une étoile mourante n'est pas fait pour un petit dragon : ses écailles devinrent du verre noir."""
    hole = (250, 120)
    els = [layer("ciel", "ciel_rouge", 0.03, (-80, -30), tint="#d8b0c0")]
    els.append(layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(5, 0), repeat=True))
    els += twinkles(505, 6, (40, 20, 880, 230))
    els += [glow("lueur_trou", "#c06cf0", 110, [K(0, pos=hole, alpha=0.45)], depth=0.06),
            sprite("trou_noir", "etoile", [K(0, pos=hole, clip="hole", scale=1.2)], depth=0.06)]
    els += hills(mist=True, far_tint="#e0c0d0")
    j = (480, 436)
    t_tr, t_roar, t_flash = 1.8, 3.9, 5.0
    els += [
        glow("chaleur", "#ff6a3a", 320, pulse=(0.25, 1.1), keys=[K(0, pos=(480, 380), alpha=0.25), K(t_tr + 1.5, alpha=0.45), K(t_flash, alpha=0.55),
                                         K(D, alpha=0.3)]),
        glow("ventre", "#ffb347", 50, [K(0, pos=(j[0] + 14, j[1] - 26), alpha=0.5, scale=1.0), K(t_tr, alpha=0.6, scale=1.2),
                                       K(t_tr + 1.6, pos=(j[0] + 26, j[1] - 48), alpha=0.5, scale=2.0), K(t_flash, alpha=0.5),
                                       K(t_flash + 0.05, alpha=0.0, ease="step")]),
        sprite("jeune", "jeune", [K(0, pos=j, clip="glow"), K(t_tr, clip="transform"), K(t_roar, clip="roar"),
                                  K(t_flash, alpha=1.0), K(t_flash + 0.06, alpha=0.0, ease="step")], shadow=shade(150, alpha=0.5)),
        sprite("pyrrhax", BOSS, [K(0, pos=(500, 392), alpha=0.0, scale=0.9), K(t_flash, alpha=0.0, ease="step"),
                                 K(t_flash + 0.06, alpha=1.0, clip="summon", ease="step"), K(t_flash + 1.0, clip="idle"),
                                 K(D, pos=(500, 380), ease="inout")]),
        particles("eclats", "sparks", "#c8b8ff", (300, 220, 360, 230), [K(0, alpha=0.0), K(t_tr, alpha=0.0),
                                                                        K(t_tr + 0.4, alpha=1.0), K(D, alpha=0.8)]),
        particles("braises", "embers", "#ff6a3a", (120, 160, 720, 300), [K(0, alpha=0.5), K(t_flash, alpha=1.0)]),
    ]
    els += front()
    cam = [K(0, pos=(450, 372), zoom=1.62), K(t_tr, pos=(468, 368), zoom=1.6), K(t_tr + 1.8, pos=(486, 330), zoom=1.18),
           K(t_flash, pos=(496, 320), zoom=1.1), K(D, pos=(520, 300), zoom=1.04)]
    shake = [{"from": t_tr, "to": t_tr + 1.7, "amount": 3.0}, {"from": t_roar + 0.2, "to": t_roar + 0.8, "amount": 5.0},
             {"from": t_flash, "to": t_flash + 0.7, "amount": 7.0}]
    flash = [{"t": t_flash, "in": 0.1, "out": 0.8, "color": "#fff0d8", "alpha": 0.95}]
    return dict(backdrop="#12040e", camera=cam, elements=els, shake=shake, flash=flash, still=6.6)


def shot_frontier(D):
    """Depuis, il garde la dernière frontière. Plus personne ne se moquera jamais de son feu."""
    moon = (600, 168)
    els = [layer("ciel", "ciel_rouge", 0.03, (-80, -30)),
           layer("etoiles", "etoiles", 0.08, (-80, -20), drift=(5, 0), repeat=True)]
    els += twinkles(606, 6, (40, 20, 600, 200))
    els += [glow("lueur_lune", "#ff7a5a", 230, pulse=(0.2, 3.0), depth=0.1,
                 keys=[K(0, pos=moon, alpha=0.5), K(D, pos=(moon[0], moon[1] - 16), alpha=0.6)]),
            sprite("lune", "lune", [K(0, pos=moon, clip="idle", scale=1.25), K(D, pos=(moon[0], moon[1] - 16), ease="linear")],
                   depth=0.1)]
    els += [layer("nuages_loin", "nuages", 0.12, (-200, 210), drift=(-8, 0), repeat=True, alpha=0.7, offset=0.4),
            layer("pics_loin", "pics_loin", 0.25, (-240, 170)),
            layer("brume", "brume", 0.4, (-160, 330), drift=(14, 0), repeat=True, alpha=0.9)]
    gate = (290, 434)
    els += [
        glow("lueur_porte", "#ff6a3a", 160, pulse=(0.35, 1.4),
             keys=[K(0, pos=(gate[0], gate[1] - 170), alpha=0.6), K(D, alpha=0.7)], depth=0.7),
        sprite("porte", "porte", [K(0, pos=gate, clip="idle")], depth=0.7),
        layer("pics", "pics", 0.5, (-320, 150), offset=0.3),
        particles("poussiere_pics", "dustfall", "#d0a0a0", (0, 160, 960, 260), [K(0, alpha=0.8)], depth=0.5),
        layer("sol", "sol", 1.0, (-280, 400)),
        sprite("lave1", "lave", [K(0, pos=(470, 462), clip="idle")]),
        glow("lave1_lueur", "#ff6a2a", 70, [K(0, pos=(470, 452), alpha=0.55)], pulse=(0.5, 1.5)),
        sprite("flammeches1", "flammeches", [K(0, pos=(150, 452), clip="idle")]),
        glow("flammeches1_lueur", "#ffa040", 46, [K(0, pos=(150, 438), alpha=0.6)], pulse=(0.45, 0.45)),
        sprite("flammeches2", "flammeches", [K(0, pos=(900, 456), clip="idle", offset=0.3)]),
        glow("flammeches2_lueur", "#ffa040", 46, [K(0, pos=(900, 442), alpha=0.6)], pulse=(0.45, 0.5, 0.4)),
        particles("cendres", "ash", "#c8b0b0", (0, 330, 960, 160), [K(0, alpha=0.9)]),
    ]
    px, py = 700, 380
    t_in, t_fire = 3.5, 4.26
    PS = 0.85  # the boss a little smaller than in its arena: the whole dragon in the frame
    mouth = (px - (228 - 128) * PIX * PS, py + (95 - 152) * PIX * PS)
    els += [
        sprite("rocher", "rocher", [K(0, pos=(706, 524), clip="idle")]),
        sprite("pyrrhax", BOSS, [K(0, pos=(px, py), clip="idle", flip=True, scale=PS), K(t_in, clip="telegraph_special"),
                                 K(t_fire, clip="special")]),
        sprite("souffle", "flamme", [K(0, pos=mouth, alpha=0.0, flip=True, scale=1.3), K(t_fire, alpha=0.0, ease="step"),
                                     K(t_fire + 0.01, alpha=1.0, clip="start", ease="step")], blend="add"),
        glow("souffle_lueur", "#ff8a3a", 220, [K(0, pos=(mouth[0] - 200, mouth[1]), alpha=0.0), K(t_fire, alpha=0.0),
                                               K(t_fire + 0.3, alpha=0.8), K(D, alpha=0.8)]),
        particles("braises", "embers", "#ff6a3a", (100, 200, 760, 260), [K(0, alpha=0.8)]),
        particles("etincelles", "sparks", "#ffb070", (0, 120, 520, 160), [K(0, alpha=0.0), K(t_fire, alpha=0.0),
                                                                           K(t_fire + 0.4, alpha=1.0)]),
    ]
    els += front()
    cam = [K(0, pos=(390, 312), zoom=1.16), K(1.0, pos=(410, 310), zoom=1.15), K(3.2, pos=(590, 300), zoom=1.08),
           K(D, pos=(560, 296), zoom=1.04)]
    shake = [{"from": t_fire, "to": D, "amount": 2.5}]
    return dict(backdrop="#12040e", camera=cam, elements=els, shake=shake, still=5.6)


SHOTS = [shot_nest, shot_smoke, shot_ember_rain, shot_swallow, shot_glass, shot_frontier]


# =================================================================== the story file
def stages(story):
    out = []
    for shot, fn in zip(story["shots"], SHOTS):
        D = shot_duration(shot["text"])
        st = fn(math.floor(D * 1000) / 1000)
        still = st.pop("still")
        stage = {"size": [W, H]}
        stage.update(st)
        out.append((stage, still))
    return out


def write_story(built):
    story = json.load(open(STORY_JSON, encoding="utf-8"))
    story["version"] = 2
    story["comment"] = ("Story of a giga boss (game/content/BossStories.h), told before the first fight against it. Version 2: "
                        "each shot is an animated scene (« stage »: parallax layers, sprites moved by keyframes, glows, "
                        "particles, camera, shakes, flashes), written by tools/assetgen/environment/story_pyrrhax_scene.py "
                        "(edit it there); its image is the still rendered from the scene. A shot lasts max(4.5 s, 1.2 s + "
                        "characters / 16); then the title card (3 s).")
    for shot, (stage, _) in zip(story["shots"], built):
        shot["particles"] = "none"
        shot["zoom"] = 1.04
        shot["stage"] = stage
    with open(STORY_JSON, "w", encoding="utf-8") as f:
        json.dump(story, f, ensure_ascii=False, indent=1)
        f.write("\n")
    return story


# =================================================================== rendering (stills, previews)
EASE = {
    "linear": lambda u: u,
    "in": lambda u: u * u,
    "out": lambda u: 1 - (1 - u) * (1 - u),
    "inout": lambda u: u * u * (3 - 2 * u),
    "step": lambda u: 1.0 if u >= 1.0 else 0.0,
}


def state(el, t):
    """The resolved state of an element at time t (the rule of BossStories::elementState)."""
    st = dict(pos=(0.0, 0.0), scale=1.0, alpha=1.0, rot=0.0, flip=False, clip="", clip_t=0.0)
    prev = None
    cur = dict(st)
    clip, start, offset = "", 0.0, 0.0
    for k in el["keys"]:
        nxt = dict(cur)
        for f in ("scale", "alpha", "rot"):
            if f in k:
                nxt[f] = float(k[f])
        if "pos" in k:
            nxt["pos"] = (float(k["pos"][0]), float(k["pos"][1]))
        if "flip" in k:
            nxt["flip"] = bool(k["flip"])
        if k["t"] <= t:
            if "clip" in k:
                clip, start, offset = k["clip"], k["t"], float(k.get("offset", 0.0))
            cur = nxt
            prev = k
            continue
        # between prev and k: interpolate the continuous values
        if prev is not None:
            u = (t - prev["t"]) / max(1e-6, k["t"] - prev["t"])
            e = EASE.get(k.get("ease", "inout"), EASE["inout"])(min(1.0, max(0.0, u)))
            out = dict(cur)
            for f in ("scale", "alpha", "rot"):
                out[f] = cur[f] + (nxt[f] - cur[f]) * e
            out["pos"] = (cur["pos"][0] + (nxt["pos"][0] - cur["pos"][0]) * e,
                          cur["pos"][1] + (nxt["pos"][1] - cur["pos"][1]) * e)
            cur = out
        else:
            cur = nxt  # before the first key: its values
        break
    if not clip:  # before its first clip starts: that clip's first frame
        first = next((k for k in el["keys"] if "clip" in k), None)
        if first is not None:
            clip, start, offset = first["clip"], t, float(first.get("offset", 0.0))
    cur["clip"] = clip
    cur["clip_t"] = max(0.0, t - start + offset)
    return cur


def camera(stage, t):
    keys = stage.get("camera") or [K(0, pos=(W / 2, H / 2), zoom=1.0)]
    el = {"keys": [dict(k, scale=k.get("zoom", 1.0)) for k in keys]}
    s = state(el, t)
    return s["pos"], s["scale"]


class Anim:
    _cache = {}

    def __init__(self, rel):
        meta = json.load(open(os.path.join(ASSETS, rel), encoding="utf-8"))
        self.meta = meta
        self.img = Image.open(os.path.join(ASSETS, meta["texture"])).convert("RGBA")
        self.fw, self.fh = meta["frameWidth"], meta["frameHeight"]
        self.pivot = meta["pivot"]

    @classmethod
    def get(cls, rel):
        if rel not in cls._cache:
            cls._cache[rel] = Anim(rel)
        return cls._cache[rel]

    def frame(self, clip, t):
        clips = self.meta["clips"]
        c = clips.get(clip) or clips.get("idle") or next(iter(clips.values()))
        for _ in range(8):
            durs = c.get("durations") or [1.0 / c.get("fps", 10)] * c["count"]
            total = sum(durs)
            if c.get("loop", True):
                t = t % total
            elif t >= total:
                if c.get("next") and c["next"] in clips:
                    t -= total
                    c = clips[c["next"]]
                    continue
                t = total - 1e-6
            acc = 0.0
            fi = len(durs) - 1
            for i, d in enumerate(durs):
                acc += d
                if t < acc:
                    fi = i
                    break
            x = (c.get("start", 0) + fi) * self.fw
            y = c["row"] * self.fh
            return self.img.crop((x, y, x + self.fw, y + self.fh))
        return None


def _blend(canvas, img, x, y, alpha, add=False, tint=None):
    """Composites an RGBA PIL image onto the float canvas at (x, y)."""
    a = np.asarray(img, np.float32) / 255.0
    h, w = a.shape[:2]
    H_, W_ = canvas.shape[:2]
    x0, y0 = int(round(x)), int(round(y))
    sx0, sy0 = max(0, -x0), max(0, -y0)
    dx0, dy0 = max(0, x0), max(0, y0)
    dx1, dy1 = min(W_, x0 + w), min(H_, y0 + h)
    if dx1 <= dx0 or dy1 <= dy0:
        return
    src = a[sy0:sy0 + dy1 - dy0, sx0:sx0 + dx1 - dx0]
    rgb = src[..., :3]
    if tint is not None:
        rgb = rgb * tint
    al = src[..., 3:4] * alpha
    dst = canvas[dy0:dy1, dx0:dx1]
    if add:
        dst += rgb * al
    else:
        dst *= 1 - al
        dst += rgb * al


def _hex(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)], np.float32)


def render(stage, t, size=(W, H), particles_on=False):
    """The stage at time t, as the game draws it in a frame of `size` (cover: the stage fills it)."""
    iw, ih = size
    base = max(iw / W, ih / H)
    (cx, cy), z = camera(stage, t)
    # clamp: the view stays inside the stage
    hw, hh = iw / (2 * base * z), ih / (2 * base * z)
    cx = W / 2 if hw >= W / 2 else min(max(cx, hw), W - hw)
    cy = H / 2 if hh >= H / 2 else min(max(cy, hh), H - hh)
    canvas = np.zeros((ih, iw, 3), np.float32)
    canvas[:] = _hex(stage.get("backdrop", "#000000"))
    C0 = (W / 2, H / 2)
    for el in stage["elements"]:
        d = el.get("depth", 1.0)
        zd = 1 + (z - 1) * d
        k = base * zd
        cdx, cdy = C0[0] + (cx - C0[0]) * d, C0[1] + (cy - C0[1]) * d

        def scr(p):
            return (iw / 2 + (p[0] - cdx) * k, ih / 2 + (p[1] - cdy) * k)

        s = state(el, t)
        if el.get("pulse"):  # a breathing light
            amt, per = el["pulse"][0], el["pulse"][1]
            ph = el["pulse"][2] if len(el["pulse"]) > 2 else 0.0
            s["alpha"] *= 1 - amt * (0.5 - 0.5 * math.cos(2 * math.pi * (t / per + ph)))
        if s["alpha"] <= 0.003:
            continue
        tint = _hex(el["tint"]) if el.get("tint") else None
        add = el.get("blend") == "add"
        if "layer" in el:
            img = Image.open(os.path.join(ASSETS, el["layer"])).convert("RGBA")
            sc = PIX * s["scale"] * k
            w2, h2 = max(1, int(round(img.width * sc))), max(1, int(round(img.height * sc)))
            im = img.resize((w2, h2), Image.NEAREST)
            dr = el.get("drift", [0, 0])
            p = (s["pos"][0] + dr[0] * t, s["pos"][1] + dr[1] * t)
            x, y = scr(p)
            if el.get("repeat"):
                x = (x % w2) - w2
                while x < iw:
                    _blend(canvas, im, x, y, s["alpha"], add, tint)
                    x += w2
            else:
                _blend(canvas, im, x, y, s["alpha"], add, tint)
        elif "sprite" in el:
            an = Anim.get(el["sprite"])
            fr = an.frame(s["clip"], s["clip_t"])
            if fr is None:
                continue
            sc = PIX * s["scale"] * k
            px, py = an.pivot
            if s["flip"]:
                fr = fr.transpose(Image.FLIP_LEFT_RIGHT)
                px = an.fw - px
            if abs(s["rot"]) > 0.01:
                pad = max(an.fw, an.fh)
                big = Image.new("RGBA", (an.fw + 2 * pad, an.fh + 2 * pad))
                big.paste(fr, (pad, pad))
                fr = big.rotate(-s["rot"], resample=Image.NEAREST, center=(px + pad, py + pad))
                px, py = px + pad, py + pad
            w2, h2 = max(1, int(round(fr.width * sc))), max(1, int(round(fr.height * sc)))
            im = fr.resize((w2, h2), Image.NEAREST)
            dr = el.get("drift", [0, 0])
            x, y = scr((s["pos"][0] + dr[0] * t, s["pos"][1] + dr[1] * t))
            sh = el.get("shadow")
            if sh:  # its soft shadow on the ground, pushed away from its light
                off = 0.0
                if sh.get("light"):
                    off = min(40.0, max(-40.0, (s["pos"][0] - sh["light"][0]) * 0.12))
                cx_, cy_ = scr((s["pos"][0] + off, s["pos"][1]))
                rw, rh = sh["size"][0] * s["scale"] * k / 2, sh["size"][1] * s["scale"] * k / 2
                x0, x1 = max(0, int(cx_ - rw)), min(iw, int(cx_ + rw) + 1)
                y0, y1 = max(0, int(cy_ - rh)), min(ih, int(cy_ + rh) + 1)
                if x1 > x0 and y1 > y0:
                    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
                    dd = np.clip(1 - np.hypot((xx + 0.5 - cx_) / rw, (yy + 0.5 - cy_) / rh), 0, 1)
                    a = dd * dd * (3 - 2 * dd) * s["alpha"] * sh.get("alpha", 0.4)
                    canvas[y0:y1, x0:x1] *= (1 - a)[..., None]
            if el.get("repeat"):
                x = ((x - px * sc) % w2) - w2
                while x < iw:
                    _blend(canvas, im, x, y - py * sc, s["alpha"], add, tint)
                    x += w2
            else:
                _blend(canvas, im, x - px * sc, y - py * sc, s["alpha"], add, tint)
        elif "glow" in el:
            r = el["radius"] * s["scale"] * k
            x, y = scr(s["pos"])
            col = _hex(el["glow"])
            x0, x1 = max(0, int(x - r)), min(iw, int(x + r) + 1)
            y0, y1 = max(0, int(y - r)), min(ih, int(y + r) + 1)
            if x1 > x0 and y1 > y0:
                yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
                dd = np.clip(1 - np.hypot(xx + 0.5 - x, yy + 0.5 - y) / max(r, 1e-3), 0, 1)
                a = dd * dd * (3 - 2 * dd) * s["alpha"]
                canvas[y0:y1, x0:x1] += col * a[..., None]
        elif "particles" in el and particles_on:
            pass
    # flashes
    for f in stage.get("flash", []):
        u = t - f["t"]
        a = 0.0
        if -f.get("in", 0.1) <= u < 0:
            a = 1 + u / max(1e-3, f.get("in", 0.1))
        elif 0 <= u <= f.get("out", 0.5):
            a = 1 - u / max(1e-3, f.get("out", 0.5))
        if a > 0:
            canvas += _hex(f.get("color", "#ffffff")) * a * f.get("alpha", 1.0)
    out = np.clip(canvas * 255 + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGB")


def generate(review=None):
    """Sprites, layers, the story file and the stills; returns the files written."""
    import sys
    chars = os.path.join(os.path.dirname(HERE), "characters")
    if chars not in sys.path:
        sys.path[:0] = [chars, os.path.dirname(HERE)]
    import story_pyrrhax_layers as LY
    import story_pyrrhax_sprites as S
    built = S.build()
    files = [os.path.join(ASSETS, sh.texture) for sh in built.values()]
    files += [os.path.join(ASSETS, S.jpath(n)) for n in built]
    scenery = LY.build()
    files += [os.path.join(ASSETS, sh.texture) for sh in scenery.values()]
    files += [os.path.join(ASSETS, f"animations/stories/pyrrhax_{n}.json") for n in scenery]
    built.update(scenery)
    story = json.load(open(STORY_JSON, encoding="utf-8"))
    st = stages(story)
    write_story(st)
    Anim._cache.clear()
    for i, (stage, still) in enumerate(st, 1):
        p = os.path.join(ASSETS, "stories", f"pyrrhax_{i}.png")
        render(stage, still).save(p, optimize=True)
        files.append(p)
    if review:
        os.makedirs(review, exist_ok=True)
        S.review(built, os.path.join(review, "sprites_pyrrhax.png"))
    return files
