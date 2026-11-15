"""Shared helpers for the 2.19 sanctuary guardians (BOSSES-C): Mycelia, Aurorax, Pyrogyre and
Nyxara.  Each boss module draws its own body; this module holds the generic pose object, the
common clip set and timings (contract: idle, walk, telegraph, attack, telegraph_special, special,
summon, hurt, death + free extras) and the sheet writer.  Deterministic (fixed seeds)."""
import math

import numpy as np

import effects as FX
from boss_a_common import TAU, empty_put, over, shake  # noqa: F401  (re-exported for the boss modules)
from common.palette import OUTLINE
from sheet import Sheet

# Timings of the common clips (seconds per frame), close to the other guardians' sheets.
TIMING = {
    "telegraph": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "attack": [0.05, 0.06, 0.18, 0.12, 0.14],
    "telegraph_special": [0.1, 0.1, 0.1, 0.12, 0.14, 0.2],
    "summon": [0.12, 0.1, 0.12, 0.12, 0.14, 0.16],
    "roar": [0.12, 0.12, 0.1, 0.1, 0.1, 0.12, 0.14, 0.16],
    "hurt": [0.08, 0.14],
    "death": [0.1, 0.12, 0.12, 0.14, 0.14, 0.14, 0.16, 0.16, 0.12, 0.12, 0.12, 0.14, 0.16, 0.3],
}


class Pose:
    """Bag of named pose parameters with defaults given by each boss."""

    DEFAULTS = {}

    def __init__(self, **kw):
        self.__dict__.update(self.DEFAULTS)
        self.__dict__.update(kw)

    def copy(self, **kw):
        d = dict(self.__dict__)
        d.update(kw)
        p = self.__class__()
        p.__dict__.update(d)
        return p


def outline_mask(f):
    return (f[:, :, 0] == OUTLINE[0]) & (f[:, :, 1] == OUTLINE[1]) & (f[:, :, 2] == OUTLINE[2])


def clip_below(f, y):
    """Erase every pixel at or below row y (a body sinking into the ground)."""
    out = f.copy()
    out[int(y):] = 0
    return out


def hline(f, x0, x1, y, col, only_empty=True, step=1, phase=0):
    h, w = f.shape[:2]
    if not 0 <= y < h:
        return f
    for x in range(max(0, int(x0)), min(w, int(x1) + 1)):
        if (x + phase) % step:
            continue
        if only_empty and f[y, x, 3] != 0:
            continue
        f[y, x] = col
    return f


def dash_arc(f, cx, cy, rx, ry, a0, a1, col, dash=3, gap=2, phase=0, only_empty=True):
    """Dashed elliptical arc (telegraph guides)."""
    n = max(8, int(abs(a1 - a0) * max(rx, ry) * 1.6))
    k = 0
    last = None
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        x, y = int(round(cx + math.cos(a) * rx - 0.5)), int(round(cy + math.sin(a) * ry - 0.5))
        if (x, y) == last:
            continue
        last = (x, y)
        if (k + phase) % (dash + gap) < dash:
            if 0 <= x < f.shape[1] and 0 <= y < f.shape[0] and (not only_empty or f[y, x, 3] == 0):
                f[y, x] = col
        k += 1
    return f


def sound_rings(f, cx, cy, radii, cols, a0=-1.0, a1=1.0, dotted=None):
    from boss_a_common import ring
    for k, r in enumerate(radii):
        ring(f, cx, cy, r, cols[min(k, len(cols) - 1)], a0, a1, dotted=(dotted or {}).get(k, 0))
    return f


def build_sheet(boss, clips):
    """clips: list of (name, frames, kwargs).  Writes sprites/enemies/<id>.png +
    animations/<id>.json."""
    s = Sheet("sprites/enemies/%s.png" % boss.id, boss.W, boss.H, boss.PIVOT)
    for name, frames, kw in clips:
        s.add(name, frames, **kw)
    s.save("animations/%s.json" % boss.id)
    return s


def standard_clips(b, extras=()):
    """The common clip list, in row order, from a boss object exposing one method per clip."""
    walk = b.walk()
    clips = [
        ("idle", b.idle(), dict(fps=b.IDLE_FPS, loop=True)),
        ("walk", walk, dict(fps=b.WALK_FPS, loop=True, events=getattr(b, "WALK_EVENTS", None))),
        # `move` = the same cycle: the engine asks flying enemies for "move", walkers for "walk".
        ("move", [f.copy() for f in walk], dict(fps=b.WALK_FPS, loop=True, events=getattr(b, "WALK_EVENTS", None))),
        ("telegraph", b.telegraph(), dict(durations=TIMING["telegraph"], loop=False)),
        ("attack", b.attack(), dict(durations=TIMING["attack"], loop=False, events=b.ATTACK_EVENTS)),
        ("telegraph_special", b.telegraph_special(), dict(durations=TIMING["telegraph_special"], loop=False)),
        ("special", b.special(), dict(fps=b.SPECIAL_FPS, loop=True, events=getattr(b, "SPECIAL_EVENTS", None))),
        ("summon", b.summon(), dict(durations=TIMING["summon"], loop=False)),
        ("roar", b.roar(), dict(durations=TIMING["roar"], loop=False)),
        ("hurt", b.hurt(), dict(durations=TIMING["hurt"], loop=False)),
        ("death", b.death(), dict(durations=TIMING["death"], loop=False)),
    ]
    clips.extend(extras)
    return clips


def dissolve_seq(base, ts, seed, cols, residue=None, rise=24, drift=6, bounds=None, life=0.4, rate=0.16):
    out = []
    h, w = base.shape[:2]
    for t in ts:
        f = FX.dissolve(base, min(1.0, t), seed=seed, colors=cols, rise=rise, drift=drift, top_first=True,
                        life=life, particle_rate=rate, bounds=bounds or (0, 0, w, h - 1))
        if residue is not None:
            FX.under(f, residue(min(1.0, 0.3 + t)))
        out.append(f)
    return out


def scatter_px(f, pts, col):
    for (x, y) in pts:
        empty_put(f, x, y, col)
    return f


def rng(seed):
    return np.random.default_rng(seed)
