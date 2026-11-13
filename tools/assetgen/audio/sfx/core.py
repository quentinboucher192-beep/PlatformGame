"""Sound-effect registry. Each effect is a function rng -> mono float array, registered with a
target loudness (max momentary K-weighted loudness, LUFS) so effects stay balanced.

Effects registered with loop=True return one exactly periodic buffer (seamless loop); they are
finished without trimming or fades (see sfx.render.finalize_loop)."""
import zlib
from dataclasses import dataclass
from typing import Callable

import numpy as np

REGISTRY = {}


@dataclass
class SoundDef:
    name: str
    fn: Callable
    loud: float       # target max-momentary loudness (100 ms window), LUFS
    category: str
    fade_out: float = 0.012
    loop: bool = False


def sound(name, loud, category, fade_out=0.012, loop=False):
    def deco(fn):
        if name in REGISTRY:
            raise ValueError(f'duplicate sound {name}')
        REGISTRY[name] = SoundDef(name, fn, loud, category, fade_out, loop)
        return fn
    return deco


def rng_for(name):
    """Deterministic per-sound random generator (independent of registration order)."""
    return np.random.default_rng(zlib.crc32(name.encode('utf-8')))
