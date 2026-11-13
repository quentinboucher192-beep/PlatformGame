"""Turns registered effect functions into finished, levelled, click-free WAV files."""
import os

import numpy as np

from audio_io import write_wav16
from dsp import filt, hp, fade, limit, max_momentary, peak_db
from sfx.core import REGISTRY, rng_for
from synth import ns

CEILING_DB = -1.3        # limiter ceiling; files must stay <= -1 dBFS after 16-bit rounding
LEAD_GATE_DB = -45.0     # leading silence threshold, relative to the peak
TAIL_GATE_DB = -66.0     # trailing silence threshold, absolute (after levelling)


def trim(x):
    a = np.abs(x)
    pk = a.max()
    if pk <= 0:
        return x
    lead = np.nonzero(a > pk * 10 ** (LEAD_GATE_DB / 20))[0][0]
    start = max(0, lead - ns(0.001))
    return x[start:]


def trim_tail(x):
    idx = np.nonzero(np.abs(x) > 10 ** (TAIL_GATE_DB / 20))[0]
    end = idx[-1] + 1 if len(idx) else len(x)
    return x[:min(len(x), end + ns(0.004))]


def finalize(x, sd):
    x = np.asarray(x, dtype=float)
    x = filt(x, hp(34.0, 0.7))           # remove DC / inaudible sub rumble
    x = trim(x)
    x = x * 10 ** ((sd.loud - max_momentary(x)) / 20)
    x = limit(x, CEILING_DB, window=0.002)
    x = trim_tail(x)
    fo = min(sd.fade_out, 0.3 * len(x) / 44100)
    x = fade(x, fin=0.0015, fout=fo)
    x = fade(trim_tail(x), fout=0.004)   # drop the part the fade pushed below the gate
    assert peak_db(x) <= -1.0, (sd.name, peak_db(x))
    return x


def render(name):
    sd = REGISTRY[name]
    return finalize(sd.fn(rng_for(name)), sd)


def render_all(out_dir, names=None, log=print):
    os.makedirs(out_dir, exist_ok=True)
    for name in sorted(REGISTRY):
        if names and name not in names:
            continue
        x = render(name)
        write_wav16(os.path.join(out_dir, name + '.wav'), x)
        log(f'  sounds/{name}.wav  {len(x) / 44100:5.2f}s  peak {peak_db(x):6.1f} dBFS  '
            f'loud {max_momentary(x):6.1f}')
