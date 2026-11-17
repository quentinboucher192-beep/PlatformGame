"""The sprites of Pyrrhax's story (« Le Dragon d'Obsidienne », 2.60): every actor of its six
animated shots, drawn frame by frame like the game's sprites and written in the game's animation
format (a sheet under assets/stories/pyrrhax/, its clips in assets/animations/stories/
pyrrhax_<name>.json; data/stories/pyrrhax.json places and moves them).

  fumerolle   la Fumerolle, the small grey hatchling      128x88   (story_dragonet.py)
  frere       a fire brother (orange)                     128x88
  soeur       a fire sister (crimson)                     128x88
  jeune       the Fumerolle turning into black glass      192x160
  oeuf        its egg: rocking, cracking, hatching         96x80
  coquille    the top of the egg, spinning away            32x32
  coquilles   the brood's broken shells around the nest    64x32
  nid         the nest of warm ashes (back and front rims) 224x72
  flamme      a brother's jet of fire                      128x48
  fumee       the Fumerolle's puff of smoke, coughs        80x64
  volute      smoke rising from the nest                   48x80
  braise      an ember: falling, floating, impact, spark   32x32
  etoile      the star of the centre: shining, collapsing into a black hole   96x96
  astre       a twinkling star                             16x16
  lune        the red moon                                 112x112
  porte       the last frontier's gate                     192x160
  rocher      Pyrrhax's rock                               176x96

The grown dragon is the game's own sheet (animations/pyrrhax.json).  One sheet pixel is two
units of the story's 960x540 stage.  Deterministic (fixed seeds)."""
import math
import os

import numpy as np

from boss_a_common import mat
from raster import TCanvas
from sheet import Sheet
from story_dragonet import FIRE, HOT, OBS, OBS_FAR, DPose, Dragonet

PI = math.pi
TAU = 2 * PI
TEX_DIR = "stories/pyrrhax"
JSON_DIR = "animations/stories"


def tex(name):
    return f"{TEX_DIR}/{name}.png"


def jpath(name):
    return f"{JSON_DIR}/pyrrhax_{name}.json"


def over(dst, src):
    sel = src[:, :, 3] > 0
    dst[sel] = src[sel]
    return dst


def blank(w, h):
    return np.zeros((h, w, 4), np.uint8)


def C(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def putpx(img, x, y, c, only_empty=False):
    x, y = int(math.floor(x)), int(math.floor(y))
    if 0 <= y < img.shape[0] and 0 <= x < img.shape[1]:
        if only_empty and img[y, x, 3]:
            return
        img[y, x] = c


# ======================================================================= the hatchlings
def _breath(i, n):
    return 0.5 + 0.5 * math.sin(TAU * i / n)


def hatchling_clips(d, grow=1.0, kind="grey"):
    """The clips shared by every hatchling of the brood (idle, walk) plus each one's own."""
    clips = []

    def P(**kw):
        kw.setdefault("grow", grow)
        return DPose(**kw)

    n = 8
    clips.append(("idle", [d.draw(P(t=i * 0.12, chest=_breath(i, n), bob=0.6 * math.sin(TAU * i / n), tail=TAU * i / n,
                                  wing=0.22 + 0.06 * math.sin(TAU * i / n), eye=0.1 if i == 5 else 1.0))
                           for i in range(n)], dict(fps=7)))
    clips.append(("walk", [d.draw(P(t=i * 0.1, step=TAU * i / n, bob=-1.2 * abs(math.sin(TAU * i / n)),
                                  head=0.05 * math.sin(TAU * i / n), tail=TAU * i / n, wing=0.25,
                                  hy=0.5 * math.sin(TAU * i / n)))
                           for i in range(n)], dict(fps=10)))
    if kind == "grey":
        clips += fumerolle_clips(d, P)
    else:
        clips += sibling_clips(d, P)
    return clips


def _smoke_motes(f, x, y, n, seed, spread=3.0, rise=0.0):
    rng = np.random.default_rng(seed)
    cols = [C("#e6e0ea"), C("#bdb5c4"), C("#948ca0")]
    for _ in range(n):
        a = rng.uniform(0, TAU)
        r = rng.uniform(0, spread)
        px, py = x + math.cos(a) * r * 1.3, y + math.sin(a) * r * 0.8 - rise
        c = cols[int(rng.integers(0, 3))]
        putpx(f, px, py, c, True)
        if rng.random() < 0.5:
            putpx(f, px + 1, py, c, True)
    return f


def fumerolle_clips(d, P):
    clips = []
    # look_up: raises its head to the sky (then « gaze »)
    ups = [0.0, -0.18, -0.36, -0.5, -0.56]
    clips.append(("look_up", [d.draw(P(t=i * 0.1, head=h, hy=-0.6 * i / 4, look=(0.5, -0.9 * i / 4), wing=0.25,
                                     chest=0.3)) for i, h in enumerate(ups)],
                  dict(durations=[0.12, 0.1, 0.1, 0.12, 0.2], loop=False, next="gaze")))
    n = 6
    clips.append(("gaze", [d.draw(P(t=i * 0.15, head=-0.56 + 0.04 * math.sin(TAU * i / n), hy=-0.6,
                                  look=(0.5, -0.9), chest=_breath(i, n), tail=TAU * i / n, wing=0.25,
                                  eye=0.15 if i == 4 else 1.0)) for i in range(n)], dict(fps=5)))
    # inhale: the chest swells, the head goes back
    clips.append(("inhale", [d.draw(P(t=i * 0.1, chest=i / 4, head=-0.22 * i / 4, hx=-1.0 * i / 4,
                                    wing=0.25 + 0.3 * i / 4, eye=1.0 - 0.35 * (i >= 3), frill=i / 4))
                             for i in range(5)], dict(durations=[0.1, 0.1, 0.12, 0.14, 0.2], loop=False)))
    # blow: mouth open, the breath comes out: only smoke (the cloud is its own sprite, « fumee »)
    fr = []
    for i in range(8):
        jaw = [0.3, 0.75, 0.85, 0.85, 0.8, 0.6, 0.35, 0.15][i]
        p = P(t=i * 0.1, jaw=jaw, chest=max(0.0, 1.0 - i / 5), head=0.1 - 0.02 * i, hx=1.0, wing=0.5 - 0.04 * i,
              eye=0.75, frill=max(0.0, 1.0 - i / 4))
        f = d.draw(p)
        (mx, my), _ = d.mouth(p)
        q = d.frame_point(p, (mx + 3.0, my))
        if 1 <= i <= 5:
            _smoke_motes(f, q[0] + 2 + i, q[1], 6 + 2 * i, 300 + i, spread=2.0 + i * 0.6)
        fr.append(f)
    clips.append(("blow", fr, dict(durations=[0.08, 0.1, 0.12, 0.12, 0.12, 0.12, 0.14, 0.2], loop=False)))
    # cough: head jerking down, eyes shut, smoke puffs out of the mouth and the nostrils
    fr = []
    for i in range(8):
        hit = i % 3 == 1
        p = P(t=i * 0.1, head=0.32 if hit else 0.14, hy=1.5 if hit else 0.5, jaw=0.45 if hit else 0.05,
              eye=0.1 if hit or i % 3 == 2 else 0.6, chest=0.0 if hit else 0.4, tail_lift=-0.5, wing=0.1, tilt=0.03 if hit else 0.0)
        f = d.draw(p)
        (mx, my), _ = d.mouth(p)
        q = d.frame_point(p, (mx + 2.0, my))
        if hit or i % 3 == 2:
            _smoke_motes(f, q[0] + 3, q[1] - (1 if hit else 4), 10 if hit else 7, 400 + i, spread=2.5 if hit else 3.5)
        fr.append(f)
    clips.append(("cough", fr, dict(durations=[0.1, 0.12, 0.1, 0.1, 0.12, 0.1, 0.12, 0.2], loop=False)))
    # sad: head low, tail dragging, half-closed eyes, a slow breath
    n = 6
    clips.append(("sad", [d.draw(P(t=i * 0.2, head=0.36, hy=2.2, look=(0.3, 0.9), eye=0.55, tail_lift=-1.2, wing=-0.1,
                                 chest=0.4 * _breath(i, n), bob=0.4 * math.sin(TAU * i / n), tail=0.4 * math.sin(TAU * i / n)))
                          for i in range(n)], dict(fps=4)))
    # swallow: an ember in the open mouth, a gulp, it goes down the neck, the belly lights up
    for name, b0, b1 in (("swallow", 0.0, 0.45), ("swallow_more", 0.45, 0.8), ("swallow_last", 0.8, 1.15)):
        fr = []
        for i in range(8):
            jaw = [0.7, 1.0, 0.15, 0.0, 0.0, 0.0, 0.0, 0.0][i]
            head = [-0.45, -0.5, -0.3, -0.2, -0.12, -0.06, 0.0, -0.05][i]
            throat = [-1.0, -1.0, 0.1, 0.35, 0.6, 0.85, -1.0, -1.0][i]
            bel = b0 if i < 5 else (b1 + 0.2 if i == 6 else b1)
            fr.append(d.draw(P(t=i * 0.1, jaw=jaw, head=head, hy=-1.0 if i < 2 else 0.0, throat=throat, belly=bel,
                               look=(0.6, -0.6) if i < 2 else (0.4, 0.5), eye=0.2 if i == 2 else 1.0,
                               chest=0.6 if i == 2 else 0.3, wing=0.3 + (0.25 if i == 6 else 0.0),
                               bob=-1.0 if i == 6 else 0.0)))
        clips.append((name, fr, dict(durations=[0.12, 0.14, 0.1, 0.1, 0.1, 0.12, 0.16, 0.2], loop=False)))
    # glow: proud and amazed, its belly shines like a forge
    n = 6
    clips.append(("glow", [d.draw(P(t=i * 0.14, belly=1.15 + 0.12 * math.sin(TAU * i / n), chest=0.7 + 0.3 * _breath(i, n),
                                  head=0.22, look=(0.1, 0.9), wing=0.45 + 0.1 * math.sin(TAU * i / n),
                                  tail=TAU * i / n, bob=-0.8 * _breath(i, n), frill=0.5))
                           for i in range(n)], dict(fps=7)))
    return clips


def sibling_clips(d, P):
    clips = []
    n = 6
    # watch: looking down at the egg, tail wagging
    clips.append(("watch", [d.draw(P(t=i * 0.12, head=0.3, hy=1.0, look=(0.5, 0.9), chest=_breath(i, n), tail=TAU * i / n * 2,
                                   wing=0.2, eye=0.15 if i == 3 else 1.0)) for i in range(n)], dict(fps=6)))
    # cheer: hopping, wings up, mouth open
    clips.append(("cheer", [d.draw(P(t=i * 0.1, bob=-3.5 * abs(math.sin(PI * i / n)), wing=0.6 + 0.5 * math.cos(TAU * i / n),
                                   jaw=0.55, happy=1.0, head=-0.15, tail=TAU * i / n, crouch=0.6 if i == 0 else 0.0))
                            for i in range(n)], dict(fps=10)))
    # inhale, then fire (the jet is its own sprite, « flamme »)
    clips.append(("inhale", [d.draw(P(t=i * 0.1, chest=i / 3, head=-0.2 * i / 3, hx=-1.2 * i / 3, wing=0.3 + 0.3 * i / 3,
                                    frill=i / 3)) for i in range(4)],
                  dict(durations=[0.1, 0.1, 0.12, 0.16], loop=False, next="fire")))
    clips.append(("fire", [d.draw(P(t=i * 0.1, jaw=1.0, head=0.02 * (-1) ** i, hx=1.5, chest=0.5 - 0.1 * i, wing=0.7,
                                  frill=1.0, eye=0.7, tremble=0.4)) for i in range(4)], dict(fps=12)))
    # laugh: eyes shut, mouth wide, rocking back
    clips.append(("laugh", [d.draw(P(t=i * 0.1, happy=1.0, jaw=0.75 if i % 2 == 0 else 0.35, head=-0.25 + 0.08 * (i % 2),
                                   tilt=-0.05 + 0.04 * (i % 2), bob=-1.5 * (i % 2), wing=0.4, tail=TAU * i / n))
                            for i in range(n)], dict(fps=9)))
    return clips


def sheet_from(texture, fw, fh, pivot, clips):
    sh = Sheet(texture, fw, fh, pivot)
    for name, frames, meta in clips:
        meta = dict(meta)
        loop = meta.pop("loop", True)
        sh.add(name, frames, fps=meta.get("fps"), durations=meta.get("durations"), loop=loop, next=meta.get("next"))
    return sh


# ======================================================================= the transformation
def jeune_clips():
    d = Dragonet("grey", w=192, h=160, origin=(96.0, 152.0))
    clips = []
    n = 6
    # glow: the swallowed fire is too much: it trembles, glowing, the first cracks open
    clips.append(("glow", [d.draw(DPose(t=i * 0.13, belly=1.25 + 0.15 * math.sin(TAU * i / n), tremble=0.7, chest=0.8,
                                        head=-0.15, look=(0.4, -0.5), wing=0.35, obs=0.02 + 0.015 * i, seam=1.0,
                                        eye=0.6 if i % 3 == 2 else 1.0, frill=0.6))
                           for i in range(n)], dict(fps=9)))
    # transform: the black glass spreads from the belly, it grows, spreads its wings, raises its head
    fr = []
    m = 12
    for i in range(m):
        k = i / (m - 1)
        e = k * k * (3 - 2 * k)
        fr.append(d.draw(DPose(t=i * 0.1, belly=1.3 - 0.4 * e, obs=0.08 + 0.92 * min(1.0, k * 1.25), seam=1.0 - 0.3 * e,
                               grow=1.0 + 0.85 * e, wing=0.35 + 0.75 * e, head=-0.1 - 0.3 * e, frill=0.6 + 0.4 * e,
                               tremble=0.8 * (1 - e), chest=0.8, tail=i * 0.5, tail_lift=e, eye=1.0)))
    clips.append(("transform", fr, dict(durations=[0.14] * 4 + [0.12] * 6 + [0.16, 0.24], loop=False, next="stand")))
    # stand: the young obsidian dragon, breathing, wings half spread, its seams pulsing
    clips.append(("stand", [d.draw(DPose(t=i * 0.15, obs=1.0, seam=0.55 + 0.25 * _breath(i, n), grow=1.85,
                                         wing=1.0 + 0.1 * math.sin(TAU * i / n), head=-0.4, chest=_breath(i, n),
                                         belly=0.8, frill=1.0, tail=TAU * i / n, tail_lift=1.0))
                            for i in range(n)], dict(fps=6)))
    # roar: head thrown back, jaws wide, the fire inside
    rr = []
    for i in range(6):
        j = [0.3, 0.8, 1.0, 1.0, 1.0, 0.6][i]
        rr.append(d.draw(DPose(t=i * 0.1, obs=1.0, seam=1.0, grow=1.85, wing=1.15, head=-0.55 - 0.1 * (i in (2, 3, 4)),
                               jaw=j, belly=1.2, frill=1.0, chest=1.0 - 0.15 * i, tremble=0.5 if 2 <= i <= 4 else 0.0,
                               tail_lift=1.2, eye=0.4 if 2 <= i <= 4 else 1.0)))
    clips.append(("roar", rr, dict(durations=[0.1, 0.1, 0.14, 0.14, 0.14, 0.2], loop=False, next="stand")))
    return clips


# ======================================================================= the egg
EGG = mat("eg_shell", ["#f6f1e6", "#ddd4c2", "#b4a994", "#857a66"], th=[0.82, 0.58, 0.32])
EGG_IN = mat("eg_inside", ["#6c5e52", "#4c4038", "#30281f"], th=[0.6, 0.3])
SPECK = mat("eg_speck", ["#6d6878", "#4c4858"], th=[0.5])
CRACK = mat("eg_crack", ["#2a2230"], outline=None)
EC = (48.0, 59.0)  # egg centre in the 96x80 frame (its bottom on y = 74)
ERX, ERY = 11.5, 15.0
CRACK_Y = 55.0


def _zig(x0, x1, y, n=7, amp=2.2):
    pts = []
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        pts.append((x, y + (amp if i % 2 else -amp * 0.6)))
    return pts


def egg_canvas(rot=0.0, dx=0.0):
    return TCanvas(96, 80, rot=rot, center=(48.0, 74.0), offset=(dx, 0.0))


def egg_whole(rot=0.0, cracks=0.0, dx=0.0):
    cv = egg_canvas(rot, dx)
    cv.part()
    cv.tellipse(EC, ERX, ERY, EGG)
    cv.part(line=False)
    rng = np.random.default_rng(77)
    for _ in range(14):
        a, r = rng.uniform(0, TAU), rng.uniform(0.15, 0.85)
        q = (EC[0] + math.cos(a) * ERX * r, EC[1] + math.sin(a) * ERY * r)
        cv.tdisc(q, rng.uniform(0.6, 1.4), SPECK, flat=0.6, decal=True)
    if cracks > 0:
        zz = _zig(EC[0] - ERX, EC[0] + ERX, CRACK_Y, 8, 2.0)
        k = int(round(cracks * (len(zz) - 1)))
        mid = len(zz) // 2
        lo, hi = max(0, mid - k // 2 - 1), min(len(zz) - 1, mid + (k + 1) // 2 + 1)
        for a, b in zip(zz[lo:hi], zz[lo + 1:hi + 1]):
            cv.tline(a, b, CRACK, inten=0.5, decal=True)
        if cracks > 0.6:  # small branches
            for (x, y) in zz[lo + 1:hi:3]:
                cv.tline((x, y), (x + 1.5, y - 3.0), CRACK, inten=0.5, decal=True)
    return cv.render()


def egg_lower(rot=0.0):
    """The bottom of the egg once hatched: a cup with a jagged rim, its inside dark."""
    cv = egg_canvas(rot)
    zz = _zig(EC[0] - ERX - 1, EC[0] + ERX + 1, CRACK_Y + 2.0, 8, 2.0)
    cup = cv.poly_mask([cv.T(q) for q in zz] + [cv.T((EC[0] + ERX + 2, 80)), cv.T((EC[0] - ERX - 2, 80))])
    cv.part()
    m = cv.tellipse(EC, ERX, ERY, EGG)
    cv.erase(m & ~cup)
    return cv.render()


def egg_inner(rot=0.0):
    """The dark inside of the cup (behind the hatchling)."""
    cv = egg_canvas(rot)
    cv.part()
    cv.tellipse((EC[0], CRACK_Y + 2.5), ERX - 1.0, 3.5, EGG_IN, flat=0.4)
    return cv.render()


def egg_cap(rot=0.0, w=32, h=32):
    """The top of the egg (above the crack), centred in a small frame."""
    cv = TCanvas(w, h, rot=rot, center=(EC[0], CRACK_Y - 5.0), offset=(16.0 - EC[0], 16.0 - (CRACK_Y - 5.0)))
    zz = _zig(EC[0] - ERX - 1, EC[0] + ERX + 1, CRACK_Y, 8, 2.0)
    cap = cv.poly_mask([cv.T(q) for q in zz] + [cv.T((EC[0] + ERX + 2, 30)), cv.T((EC[0] - ERX - 2, 30))])
    cv.part()
    m = cv.tellipse(EC, ERX, ERY, EGG)
    cv.erase(m & ~cap)
    cv.part(line=False)
    cv.tellipse((EC[0], CRACK_Y - 0.5), ERX - 1.5, 2.0, EGG_IN, flat=0.4, decal=True)
    return cv.render()


def _baby(p, sink):
    """The hatchling in its egg: its head above the rim, `sink` px lower (hidden in the cup)."""
    d = Dragonet("grey", w=96, h=80, origin=(36.0, 74.0 + sink))
    f = d.draw(p)
    f[int(CRACK_Y) + 1:] = 0  # below the rim: inside the cup
    f[:, :33] = 0  # the tail and the far wing stay in the egg
    return f


def egg_clips():
    clips = []
    n = 6
    clips.append(("idle", [egg_whole(rot=0.05 * math.sin(TAU * i / n)) for i in range(n)], dict(fps=5)))
    fr = []
    for i in range(8):
        k = (i + 1) / 8
        fr.append(egg_whole(rot=(0.06 + 0.12 * k) * (1 if i % 2 else -1), cracks=k, dx=0.0))
    clips.append(("crack", fr, dict(durations=[0.12, 0.1, 0.12, 0.1, 0.12, 0.1, 0.1, 0.14], loop=False)))
    # hatch: the cap pops (it flies away as its own sprite), the head comes out, blinks, looks
    # around, the paws on the rim, a big yawn
    fr = []
    seq = [
        dict(cap=0.0),
        dict(sink=19, head=-0.2, eye=0.0),
        dict(sink=14, head=-0.3, eye=0.0),
        dict(sink=11, head=-0.25, eye=0.3),
        dict(sink=9, head=-0.15, eye=1.0),
        dict(sink=9, head=-0.1, eye=1.0, look=(1.0, 0.0)),
        dict(sink=9, head=-0.1, eye=1.0, look=(-1.0, 0.2)),
        dict(sink=9, head=0.05, eye=0.1),
        dict(sink=6, head=-0.1, eye=1.0, look=(0.6, -0.2)),
        dict(sink=4, head=-0.4, eye=0.1, jaw=0.9, wing=0.9),
        dict(sink=4, head=-0.3, eye=0.1, jaw=0.6, wing=0.7),
        dict(sink=4, head=-0.05, eye=1.0, jaw=0.0, wing=0.35),
    ]
    for i, st in enumerate(seq):
        rot = 0.05 * math.sin(i * 1.7) if i < 3 else 0.0
        if "sink" not in st:
            f = egg_whole(rot=0.2, cracks=1.0)
            for x in range(38, 59):  # light through the open crack
                if (x * 7) % 3 == 0:
                    putpx(f, x, CRACK_Y, C("#ffe08a"))
            fr.append(f)
            continue
        f = egg_inner(rot)
        p = DPose(t=i * 0.1, grow=0.8, head=st["head"], eye=st["eye"], look=st.get("look", (0.6, 0.0)),
                  jaw=st.get("jaw", 0.0), wing=st.get("wing", 0.15), crouch=1.0, tail_lift=-1.0)
        over(f, _baby(p, st["sink"]))
        over(f, egg_lower(rot))
        if i == 1:  # bits of shell
            for (x, y) in ((33, 50), (63, 48), (36, 44), (60, 42)):
                putpx(f, x, y, C("#ddd4c2"))
                putpx(f, x + 1, y, C("#857a66"))
        fr.append(f)
    clips.append(("hatch", fr, dict(durations=[0.12, 0.1, 0.12, 0.16, 0.2, 0.3, 0.3, 0.12, 0.2, 0.25, 0.2, 0.3],
                                    loop=False, next="hatched")))
    fr = []
    for i in range(n):
        f = egg_inner()
        look = [(0.6, 0.0), (0.6, 0.0), (0.9, -0.3), (0.9, -0.3), (0.2, 0.2), (0.6, 0.0)][i]
        p = DPose(t=i * 0.15, grow=0.8, head=-0.05 + 0.05 * math.sin(TAU * i / n), eye=0.1 if i == 4 else 1.0, look=look,
                  wing=0.35 + 0.15 * math.sin(TAU * i / n), chest=_breath(i, n), crouch=1.0, tail_lift=-1.0)
        over(f, _baby(p, 4 + (1 if i in (2, 3) else 0)))
        over(f, egg_lower())
        fr.append(f)
    clips.append(("hatched", fr, dict(fps=5)))
    return clips


def cap_clips():
    return [("spin", [egg_cap(rot=TAU * i / 8) for i in range(8)], dict(fps=14)),
            ("rest", [egg_cap(rot=PI + a) for a in (0.0, 0.12, 0.0, -0.1)], dict(durations=[0.12, 0.14, 0.18, 0.4]))]


SHELL_O = mat("sh_fire", ["#ffd8a0", "#f2a05a", "#c8682e", "#8a3c1a"], th=[0.8, 0.55, 0.3])
SHELL_R = mat("sh_crimson", ["#ffc0a8", "#ea7a6a", "#b84444", "#7a2230"], th=[0.8, 0.55, 0.3])


def shells_clips():
    fr = []
    for i in range(4):
        cv = TCanvas(64, 32)
        for (cx, cy, rx, ry, a, m) in ((14, 24, 8, 5, -0.4, SHELL_O), (34, 26, 7, 4.5, 0.3, SHELL_R),
                                        (50, 25, 6.5, 4, -0.2, SHELL_O)):
            cv.part()
            msk = cv.ellipse((cx, cy), rx, ry * 1.6, m, ang=a)
            cv.erase(msk & (cv.py < cy - 1.5 + 2 * np.sin(cv.px * 1.7)))
            cv.part(line=False)
            cv.ellipse((cx, cy - 1.0), rx - 1.5, 1.6, EGG_IN, ang=a, flat=0.4, decal=True)
        f = cv.render()
        # the embers' light flickering on the shells
        rng = np.random.default_rng(900 + i)
        for _ in range(5):
            x, y = int(rng.integers(6, 58)), int(rng.integers(20, 30))
            if f[y, x, 3]:
                putpx(f, x, y, C("#ffe8a0"))
        fr.append(f)
    return [("idle", fr, dict(fps=5))]


# ======================================================================= the nest
ROCK = mat("ns_rock", ["#6e5f80", "#4c4060", "#342b46", "#221b30", "#150f1e"], th=[0.84, 0.64, 0.42, 0.22])
ASH = mat("ns_ash", ["#9a8784", "#776563", "#56474a", "#3a2f34", "#251d22"], th=[0.82, 0.62, 0.4, 0.2])
NW, NH = 224, 72
NC = (112.0, 50.0)


def _nest(front, phase):
    cv = TCanvas(NW, NH)
    rng = np.random.default_rng(4242)
    if not front:
        # the bed of warm ashes
        cv.part()
        cv.ellipse(NC, 92, 15, ASH, flat=None)
        cv.part(line=False)
        lx, ly = cv.px, cv.py
        q = ((lx - NC[0]) / 92) ** 2 + ((ly - NC[1]) / 15) ** 2
        heap = (q < 1.0)
        n = np.sin(lx * 0.9 + np.sin(ly * 1.3) * 2.0) * np.cos(ly * 1.7 + lx * 0.21)
        cv.put(heap & (n > 0.55), 0.3, ASH, decal=True)
    # rocks along the rim: the back half (y < NC) or the front half
    stones = []
    for k in range(26):
        a = TAU * k / 26 + rng.uniform(-0.08, 0.08)
        r = rng.uniform(0.92, 1.06)
        x, y = NC[0] + math.cos(a) * 98 * r, NC[1] + math.sin(a) * 17 * r
        stones.append((a, x, y, rng.uniform(6.0, 10.0), rng.uniform(4.0, 6.5), rng.uniform(-0.4, 0.4)))
    stones.sort(key=lambda s: s[2])
    for (a, x, y, rx, ry, ang) in stones:
        is_front = math.sin(a) > -0.05
        if is_front != front:
            continue
        cv.part()
        cv.ellipse((x, y - ry * 0.3), rx, ry, ROCK, ang=ang)
    f = cv.render()
    # embers glowing in the ashes and between the stones, flickering with the phase
    rng = np.random.default_rng(5151 if front else 6161)
    hot = [C("#fff2c0"), C("#ffd060"), C("#ff9a3a"), C("#e2561c"), C("#8a2a12")]
    for k in range(60 if not front else 26):
        a = rng.uniform(0, TAU)
        r = math.sqrt(rng.uniform(0, 1.0)) * (0.92 if not front else 1.02)
        x, y = NC[0] + math.cos(a) * 90 * r, NC[1] + math.sin(a) * 14 * r
        if front and math.sin(a) < 0.2:
            continue
        lvl = 0.5 + 0.5 * math.sin(phase * TAU / 4 + rng.uniform(0, TAU))
        ci = 4 - int(round(lvl * 3 + rng.uniform(0, 1)))
        ci = max(0, min(4, ci))
        xi, yi = int(x), int(y)
        if 0 <= yi < NH and 0 <= xi < NW and f[yi, xi, 3]:
            f[yi, xi] = hot[ci]
            if ci <= 1 and xi + 1 < NW and f[yi, xi + 1, 3]:
                f[yi, xi + 1] = hot[ci + 1]
    return f


def nest_clips():
    return [("back", [_nest(False, i) for i in range(4)], dict(fps=6)),
            ("front", [_nest(True, i) for i in range(4)], dict(fps=6))]


# ======================================================================= fire and smoke
FIRE_OUT = mat("fl_out", ["#ffb347", "#ff7a2a", "#e2401a", "#a8220e"], emissive=True, outline=None, th=[0.75, 0.5, 0.25])
FIRE_MID = mat("fl_mid", ["#fff2a0", "#ffd060", "#ffa030"], emissive=True, outline=None, th=[0.66, 0.33])
FIRE_CORE = mat("fl_core", ["#ffffff", "#fff8d8", "#ffe890"], emissive=True, outline=None, th=[0.66, 0.33])


def _jet(i, length=1.0, seed=0):
    """A jet of fire leaving the mouth at (4, 24) towards +x: three layers, licking tongues."""
    cv = TCanvas(128, 48)
    L = 106.0 * length
    for layer, (m, wf, lf) in enumerate(((FIRE_OUT, 1.0, 1.0), (FIRE_MID, 0.62, 0.86), (FIRE_CORE, 0.3, 0.62))):
        top, bot = [], []
        n = 12
        for k in range(n + 1):
            t = k / n
            wd = (2.0 + 13.0 * math.sin(min(1.0, t * 1.15) * PI / 2)) * wf * (1.0 - 0.45 * t ** 3)
            lick = 2.6 * wf * math.sin(i * 1.9 + k * 1.3 + layer * 0.7)
            x = 4.0 + L * lf * t
            top.append((x, 24.0 - wd - lick * (k % 2)))
            bot.append((x, 24.0 + wd + lick * ((k + 1) % 2)))
        tip = (4.0 + L * lf * (1.04 + 0.04 * math.sin(i * 2.1 + layer)), 24.0 + 2.0 * math.sin(i * 1.3 + layer))
        cv.part(line=False)
        cv.poly(top + [tip] + bot[::-1], m, shade="grad", base=0.58)
    f = cv.render(outline=False)
    # sparks flying off the jet
    rng = np.random.default_rng(700 + i + seed)
    for _ in range(14):
        x = rng.uniform(20, 124) * length
        y = 24 + rng.uniform(-1, 1) * (6 + x * 0.14)
        putpx(f, x, y, C("#ffe8a0") if rng.random() < 0.5 else C("#ff9a3a"), True)
    return f


def flame_clips():
    return [("start", [_jet(k, 0.25 + 0.25 * k) for k in range(3)], dict(durations=[0.05, 0.05, 0.06], loop=False, next="jet")),
            ("jet", [_jet(k + 3) for k in range(6)], dict(fps=14)),
            ("end", [_jet(k + 9, 0.8 - 0.25 * k) for k in range(3)], dict(durations=[0.06, 0.06, 0.08], loop=False))]


SMOKE = mat("sm_smoke", ["#f0eaf2", "#cfc7d6", "#a69eb2", "#7c748a", "#5a5268"], outline=C("#3c3448"),
            th=[0.84, 0.64, 0.44, 0.24])
SMOKE_T = mat("sm_thin", ["#d6cede", "#aea6b8", "#867e92"], outline=None, th=[0.66, 0.33])


def _cloud(w, h, blobs, holes=0.0, seed=0, outline=True):
    cv = TCanvas(w, h)
    for (x, y, r) in blobs:
        cv.part(line=False)
        cv.disc((x, y), r, SMOKE if outline else SMOKE_T)
    f = cv.render(outline=outline)
    if holes > 0:  # dissolving: dithered holes eat the cloud
        rng = np.random.default_rng(seed)
        mask = rng.random(f.shape[:2]) < holes
        yy, xx = np.mgrid[0:h, 0:w]
        mask |= ((xx + yy) % 2 == 0) & (rng.random(f.shape[:2]) < holes * 1.2)
        f[mask] = 0
    return f


def smoke_clips():
    # puff: from the mouth (8, 40), a little round cloud grows, rises and dissolves
    fr = []
    for i in range(10):
        k = i / 9
        g = min(1.0, k * 2.2)
        cx, cy = 14 + 34 * k, 40 - 14 * k
        r = 3 + 12 * g
        blobs = [(cx, cy, r), (cx - r * 0.6, cy + r * 0.25, r * 0.6), (cx + r * 0.55, cy + r * 0.2, r * 0.66),
                 (cx + r * 0.1, cy - r * 0.45, r * 0.62)]
        if i < 3:
            blobs.append((8 + 3 * i, 40, 2.5))
        fr.append(_cloud(80, 64, blobs, holes=max(0.0, (k - 0.55) * 1.6), seed=810 + i))
    clips = [("puff", fr, dict(durations=[0.06, 0.07, 0.08, 0.1, 0.12, 0.14, 0.16, 0.18, 0.2, 0.22], loop=False))]
    # small: a smaller grey puff (the coughs)
    fr = []
    for i in range(6):
        k = i / 5
        cx, cy = 12 + 18 * k, 40 - 10 * k
        r = 2 + 5 * min(1.0, k * 2)
        fr.append(_cloud(80, 64, [(cx, cy, r), (cx + r * 0.7, cy + 1, r * 0.6)], holes=max(0.0, (k - 0.5) * 1.6),
                         seed=830 + i))
    clips.append(("small", fr, dict(durations=[0.06, 0.08, 0.1, 0.12, 0.14, 0.16], loop=False)))
    return clips


def wisp_clips():
    """Thin smoke rising from the nest (48x80, from its bottom centre), curling."""
    fr = []
    n = 8
    for i in range(n):
        cv = TCanvas(48, 80)
        for k in range(14):
            t = ((k + i / n * 2) / 14)
            y = 76 - t * 74
            x = 24 + 6 * math.sin(t * 5.5 + i * TAU / n) * t + 3 * t
            r = 2.0 + 5.0 * t
            cv.part(line=False)
            cv.disc((x, y), r, SMOKE_T, bias=-0.15 * t)
        f = cv.render(outline=False)
        # thinner as it rises: the alpha fades, the top frays in a checker
        yy, xx = np.mgrid[0:80, 0:48]
        up = np.clip((76 - yy) / 76.0, 0, 1)
        on = f[:, :, 3] > 0
        f[on, 3] = np.clip(190 - 130 * up[on], 0, 255).astype(np.uint8)
        fray = on & (up > 0.6) & ((xx + yy + i) % 2 == 0)
        f[fray] = 0
        fr.append(f)
    return [("rise", fr, dict(fps=8))]


EMBER_C = [C("#ffffff"), C("#fff2b0"), C("#ffc860"), C("#ff8a2a"), C("#d2481a"), C("#8a2610")]


def _ember_ball(f, cx, cy, r, phase=0.0):
    for y in range(int(cy - r - 2), int(cy + r + 3)):
        for x in range(int(cx - r - 2), int(cx + r + 3)):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if d <= r:
                k = d / max(r, 1e-3)
                ci = min(5, int(k * 3.2 + (0.4 if (x + y) % 2 else 0.0) + 0.3 * math.sin(phase)))
                putpx(f, x, y, EMBER_C[ci])


def ember_clips():
    clips = []
    # fall: a burning ember falling down-left, its trail up-right
    fr = []
    for i in range(4):
        f = blank(32, 32)
        for k in range(16):
            t = k / 15
            x, y = 12 + t * 15, 20 - t * 15
            w = 2.4 * (1 - t) + 0.4
            ci = min(5, 2 + int(t * 4) + (1 if (k + i) % 3 == 0 else 0))
            for dy in range(-int(w), int(w) + 1):
                if (k + dy + i) % 4 == 0 and t > 0.5:
                    continue
                putpx(f, x + dy * 0.5, y + dy * 0.5, EMBER_C[ci])
        _ember_ball(f, 11.5, 20.5, 3.2 + 0.4 * (i % 2), i)
        clips_r = 1
        fr.append(f)
    clips.append(("fall", fr, dict(fps=12)))
    # float: a round ember hovering, pulsing, a little corona
    fr = []
    for i in range(6):
        f = blank(32, 32)
        r = 4.2 + 0.6 * math.sin(TAU * i / 6)
        _ember_ball(f, 16, 16, r, i)
        for k in range(8):
            a = TAU * k / 8 + i * 0.3
            d = r + 2.5 + (1.5 if (k + i) % 2 else 0)
            putpx(f, 16 + math.cos(a) * d, 16 + math.sin(a) * d, EMBER_C[3 if k % 2 else 2])
        fr.append(f)
    clips.append(("float", fr, dict(fps=9)))
    # impact: hitting the ground, a burst of sparks
    fr = []
    for i in range(6):
        f = blank(32, 32)
        k = i / 5
        if i < 3:
            _ember_ball(f, 16, 24, 5 - i * 1.2, i)
        rng = np.random.default_rng(910)
        for s in range(12):
            a = -PI * rng.uniform(0.05, 0.95)
            d = 3 + 12 * k * rng.uniform(0.6, 1.0)
            putpx(f, 16 + math.cos(a) * d, 24 + math.sin(a) * d + 6 * k * k, EMBER_C[min(5, 1 + int(k * 4))])
        for x in range(10, 23):
            if (x + i) % 3 and i < 4:
                putpx(f, x, 27, EMBER_C[min(5, 3 + i // 2)])
        fr.append(f)
    clips.append(("impact", fr, dict(durations=[0.05, 0.06, 0.07, 0.08, 0.1, 0.12], loop=False)))
    # spark: a tiny twinkle
    fr = []
    for i in range(4):
        f = blank(32, 32)
        sz = [1, 2, 3, 2][i]
        for d in range(-sz, sz + 1):
            putpx(f, 16 + d, 16, EMBER_C[1 if abs(d) < 2 else 3])
            putpx(f, 16, 16 + d, EMBER_C[1 if abs(d) < 2 else 3])
        putpx(f, 16, 16, EMBER_C[0])
        fr.append(f)
    clips.append(("spark", fr, dict(fps=10)))
    return clips


# ======================================================================= the sky
STAR_C = [C("#ffffff"), C("#fff6d8"), C("#ffe0a0"), C("#ffb070"), C("#e07050"), C("#7a3050")]
VIOLET = [C("#ffe8ff"), C("#f0a8ff"), C("#c06cf0"), C("#8a3cc8"), C("#4c1c7a"), C("#240c40")]


def _star(f, cx, cy, r, rays, ray_len, cols=STAR_C):
    for y in range(f.shape[0]):
        for x in range(f.shape[1]):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d <= r:
                ci = min(len(cols) - 1, int(d / max(r, 1e-3) * 3))
                f[y, x] = cols[ci]
            elif d <= r + 2.5 and (x + y) % 2 == 0:
                f[y, x] = cols[3]
    for k in range(rays):
        a = TAU * k / rays + (PI / 4 if k % 2 else 0)
        ln = ray_len * (1.0 if k % 2 == 0 else 0.55)
        for s in np.arange(r, r + ln, 0.5):
            t = (s - r) / max(ln, 1e-3)
            ci = min(len(cols) - 1, 1 + int(t * 3.5))
            putpx(f, cx + math.cos(a) * s, cy + math.sin(a) * s, cols[ci], True)


def _ring(f, cx, cy, rx, ry, rot, cols, width=2.0, phase=0.0, front=None):
    for k in range(int(TAU * rx * 3)):
        a = TAU * k / int(TAU * rx * 3)
        for wv in np.arange(-width / 2, width / 2 + 0.01, 0.5):
            x = cx + math.cos(a) * (rx + wv)
            y = cy + math.sin(a) * (ry + wv * ry / rx)
            if front is not None and (math.sin(a) > 0) != front:
                continue
            band = 0.5 + 0.5 * math.sin(a * 3 + phase)
            ci = min(len(cols) - 1, int((1 - band) * 2.5 + abs(wv) / width * 2))
            putpx(f, x, y, cols[ci])


def star_clips():
    S = 96
    clips = []
    fr = []
    for i in range(6):
        f = blank(S, S)
        k = 0.5 + 0.5 * math.sin(TAU * i / 6)
        _star(f, 48, 48, 7 + 1.2 * k, 8, 22 + 10 * k)
        fr.append(f)
    clips.append(("shine", fr, dict(fps=6)))
    # collapse: it swells and flickers, then falls into itself: a dark core, a violet flash ring
    fr = []
    for i in range(12):
        f = blank(S, S)
        if i < 4:
            _star(f, 48, 48, 8 + 3 * i, 8, 30 + 6 * i)
            if i % 2:
                f[f[:, :, 3] > 0] = STAR_C[0]
        elif i < 7:
            r = [6, 3, 1.5][i - 4]
            _star(f, 48, 48, r, 4, 14 - 4 * (i - 4))
        else:
            k = (i - 7) / 4
            _ring(f, 48, 48, 10 + 30 * k, 10 + 30 * k, 0, VIOLET, width=3.0 - 2 * k)
            for y in range(S):
                for x in range(S):
                    if math.hypot(x + 0.5 - 48, y + 0.5 - 48) <= 5.5:
                        f[y, x] = C("#05020a")
            _ring(f, 48, 48, 9, 3.2, 0, VIOLET, width=1.5, phase=i)
        fr.append(f)
    clips.append(("collapse", fr, dict(durations=[0.12, 0.1, 0.12, 0.1, 0.08, 0.08, 0.1, 0.07, 0.07, 0.08, 0.1, 0.14],
                                       loop=False, next="hole")))
    # hole: the black hole of the centre, its violet ring turning
    fr = []
    for i in range(8):
        f = blank(S, S)
        _ring(f, 48, 48, 26, 8, 0, VIOLET, width=4.0, phase=TAU * i / 8, front=False)
        for y in range(S):
            for x in range(S):
                d = math.hypot(x + 0.5 - 48, y + 0.5 - 48)
                if d <= 8.5:
                    f[y, x] = C("#05020a") if d < 7.5 else VIOLET[2]
        _ring(f, 48, 48, 26, 8, 0, VIOLET, width=4.0, phase=TAU * i / 8, front=True)
        fr.append(f)
    clips.append(("hole", fr, dict(fps=8)))
    return clips


def twinkle_clips():
    fr = []
    for i in range(6):
        f = blank(16, 16)
        sz = [0, 1, 2, 3, 2, 1][i]
        putpx(f, 8, 8, STAR_C[0])
        for d in range(1, sz + 1):
            c = STAR_C[1 if d == 1 else 3]
            for (dx, dy) in ((d, 0), (-d, 0), (0, d), (0, -d)):
                putpx(f, 8 + dx, 8 + dy, c)
        if sz >= 2:
            for (dx, dy) in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
                putpx(f, 8 + dx, 8 + dy, STAR_C[4])
        fr.append(f)
    return [("twinkle", fr, dict(durations=[0.3, 0.12, 0.1, 0.16, 0.1, 0.14]))]


MOON = mat("mn_moon", ["#ffb48a", "#f2805e", "#d85a4a", "#ac3c42", "#742638"], outline=None, th=[0.9, 0.7, 0.46, 0.2])
MOON_D = mat("mn_dark", ["#e2705a", "#c0504a", "#943844"], outline=None, th=[0.6, 0.3])


def moon_clips():
    fr = []
    for i in range(4):
        cv = TCanvas(112, 112)
        cv.part()
        cv.disc((56, 56), 44, MOON, bias=-0.06)
        cv.part(line=False)
        rng = np.random.default_rng(1200)
        for _ in range(11):
            a, r = rng.uniform(0, TAU), 36 * math.sqrt(rng.uniform(0, 1))
            q = (56 + math.cos(a) * r, 56 + math.sin(a) * r)
            cv.disc(q, rng.uniform(2.5, 7.0), MOON_D, decal=True, bias=0.1)
        f = cv.render(outline=False)
        # a soft halo (dithered) breathing with the frame
        for y in range(112):
            for x in range(112):
                d = math.hypot(x + 0.5 - 56, y + 0.5 - 56)
                if 44 < d < 50 + i % 2 and f[y, x, 3] == 0 and (x + y + i) % 3 == 0:
                    f[y, x] = C("#ff9a6a", 160)
        fr.append(f)
    return [("idle", fr, dict(fps=3))]


# ======================================================================= the frontier
GATE = mat("gt_obs", ["#7a74a0", "#3e3a60", "#2a2644", "#1c1930", "#110f1d"], th=[0.86, 0.66, 0.46, 0.26])
VEIL = mat("gt_veil", ["#ffd0a0", "#ff8a4a", "#e2502a", "#a02a1a", "#5a1010"], emissive=True, outline=None,
           th=[0.86, 0.66, 0.46, 0.26])


def gate_clips():
    fr = []
    for i in range(6):
        cv = TCanvas(192, 160)
        # the veil of fire between the pillars: slow curtains of light rising, hotter at the bottom
        cv.part(line=False)
        msk = cv.poly_mask([(44, 40), (148, 40), (148, 150), (44, 150)])
        x, y = cv.px, cv.py
        curtain = 0.5 + 0.5 * np.sin(x * 0.11 + np.sin(y * 0.045 + i * TAU / 6) * 1.6)
        rise = 0.5 + 0.5 * np.sin(y * 0.09 + i * TAU / 6 * 2 + np.sin(x * 0.05) * 0.8)
        v = 0.34 + 0.24 * curtain + 0.14 * rise + 0.3 * ((y - 40) / 110.0)
        cv.put(msk, np.clip(v, 0, 1), VEIL)
        # steps, pillars and lintel of black glass
        cv.part()
        cv.poly([(10, 160), (182, 160), (176, 150), (16, 150)], GATE, bevel=1.5, base=0.5)
        for x0 in (28, 148):
            cv.part()
            cv.poly([(x0, 151), (x0 + 16, 151), (x0 + 14, 38), (x0 + 2, 38)], GATE, bevel=2.0, base=0.55)
            cv.part()
            cv.poly([(x0 - 3, 151), (x0 + 19, 151), (x0 + 17, 140), (x0 - 1, 140)], GATE, bevel=1.5, base=0.62)
        cv.part()
        cv.poly([(14, 40), (178, 40), (172, 28), (20, 28)], GATE, bevel=2.0, base=0.62)
        cv.part()
        cv.poly([(80, 28), (112, 28), (96, 8)], GATE, bevel=1.5, base=0.6)
        cv.part()
        for xx in (22, 170):
            cv.poly([(xx - 6, 28), (xx + 6, 28), (xx, 16)], GATE, bevel=1.2, base=0.6)
        cv.part(line=False)
        cv.disc((96, 22), 2.6, HOT, flat=0.55 + 0.25 * math.sin(i * TAU / 6))
        f = cv.render()
        # runes on the pillars, glowing in turn
        for k in range(6):
            lit = (k + i) % 6 < 3
            for x0 in (35, 155):
                yy = 56 + k * 15
                c = C("#ffc080") if lit else C("#a8442a")
                for dd in range(4):
                    putpx(f, x0 + (dd % 2), yy + dd, c)
        fr.append(f)
    return [("idle", fr, dict(fps=6))]


ROCKM = mat("rk_obs", ["#6e6890", "#3e3a5c", "#2a2642", "#1c192e", "#110f1c"], th=[0.86, 0.66, 0.46, 0.26])


def rock_clips():
    """Pyrrhax's rock: a crag of black glass in facets, its cracks breathing magma."""
    facets = [
        ([(4, 96), (20, 64), (44, 52), (54, 96)], 0.42),
        ([(20, 64), (36, 44), (60, 34), (66, 58), (44, 52)], 0.66),
        ([(44, 52), (66, 58), (84, 96), (54, 96)], 0.5),
        ([(60, 34), (82, 26), (104, 28), (96, 56), (66, 58)], 0.72),
        ([(66, 58), (96, 56), (112, 96), (84, 96)], 0.46),
        ([(104, 28), (128, 36), (140, 60), (96, 56)], 0.6),
        ([(96, 56), (140, 60), (152, 96), (112, 96)], 0.4),
        ([(128, 36), (150, 52), (172, 96), (152, 96), (140, 60)], 0.34),
    ]
    cracks = (((52, 46), (60, 70)), ((60, 70), (56, 92)), ((108, 34), (116, 60)), ((116, 60), (130, 88)),
              ((84, 30), (88, 52)), ((88, 52), (96, 74)))
    fr = []
    for i in range(4):
        cv = TCanvas(176, 96)
        for pts, base in facets:
            cv.part(line=1)
            cv.poly(pts, ROCKM, bevel=1.6, base=base)
        cv.part(line=False)
        for k, (a, b) in enumerate(cracks):
            cv.line(a, b, HOT, inten=0.4 + 0.4 * (0.5 + 0.5 * math.sin(i * PI / 2 + k * 1.7)), decal=True)
        fr.append(cv.render())
    return [("idle", fr, dict(fps=4))]


# ======================================================================= build
SHEETS = {
    # name: (frame w, frame h, pivot, clips function)
    "fumerolle": (128, 88, [64, 82], lambda: hatchling_clips(Dragonet("grey"), 1.0, "grey")),
    "frere": (128, 88, [64, 82], lambda: hatchling_clips(Dragonet("fire"), 1.15, "fire")),
    "soeur": (128, 88, [64, 82], lambda: hatchling_clips(Dragonet("crimson"), 1.12, "crimson")),
    "jeune": (192, 160, [96, 152], jeune_clips),
    "oeuf": (96, 80, [48, 74], egg_clips),
    "coquille": (32, 32, [16, 16], cap_clips),
    "coquilles": (64, 32, [32, 30], shells_clips),
    "nid": (224, 72, [112, 50], nest_clips),
    "flamme": (128, 48, [4, 24], flame_clips),
    "fumee": (80, 64, [8, 40], smoke_clips),
    "volute": (48, 80, [24, 78], wisp_clips),
    "braise": (32, 32, [16, 16], ember_clips),
    "etoile": (96, 96, [48, 48], star_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
    "lune": (112, 112, [56, 56], moon_clips),
    "porte": (192, 160, [96, 158], gate_clips),
    "rocher": (176, 96, [88, 90], rock_clips),
}


def sheets(names=None):
    out = {}
    for name, (w, h, pivot, fn) in SHEETS.items():
        if names and name not in names:
            continue
        out[name] = sheet_from(tex(name), w, h, pivot, fn())
    return out


def build(names=None):
    """Writes every sheet and its animation file; returns {name: Sheet}."""
    built = sheets(names)
    for name, sh in built.items():
        sh.save(jpath(name))
    return built


def review(built, path, scale=2):
    """A contact sheet of every clip: its frames side by side, one row per clip."""
    from PIL import Image, ImageDraw
    rows = []
    for name, sh in built.items():
        for cname, frames, meta in sh.rows:
            rows.append((f"{name}.{cname}", frames))
    label_w = 150
    width = label_w + max(sum(f.shape[1] * scale + 4 for f in frames) for _, frames in rows)
    height = sum(max(f.shape[0] for f in frames) * scale + 8 for _, frames in rows)
    img = Image.new("RGBA", (width, height), (34, 24, 40, 255))
    d = ImageDraw.Draw(img)
    y = 0
    for label, frames in rows:
        fh = max(f.shape[0] for f in frames) * scale
        d.text((6, y + fh // 2 - 6), label, fill=(240, 230, 220, 255))
        x = label_w
        for f in frames:
            im = Image.fromarray(f, "RGBA").resize((f.shape[1] * scale, f.shape[0] * scale), Image.NEAREST)
            d.rectangle([x - 1, y, x + im.size[0], y + fh + 1], outline=(70, 56, 80, 255))
            img.alpha_composite(im, (x, y))
            x += im.size[0] + 4
        y += fh + 8
    img.save(path)
    return path


if __name__ == "__main__":
    import sys
    sys.dont_write_bytecode = True
    built = build(sys.argv[1:] or None)
    print(f"{len(built)} sheets", [os.path.basename(s.texture) for s in built.values()])
