"""Synthesised drum kit and transition effects. Every function returns a mono array and is
memoised by its arguments (pieces are triggered hundreds of times)."""
import zlib
from functools import lru_cache

import numpy as np

from dsp import filt, lp, hp, bp, peak, soft_clip, svf, reverb_ir, convolve, pad_to
from synth import SR, ns, sine, square, white, pink, brown, perc, ramp


def _rng(tag):
    return np.random.default_rng(zlib.crc32(tag.encode()))


def _ro(fn):
    cached = lru_cache(maxsize=None)(fn)

    def wrapped(*a, **k):
        out = cached(*a, **k)
        out.setflags(write=False)
        return out
    wrapped.__name__ = fn.__name__
    return wrapped


@_ro
def kick(f_start=125.0, f_end=47.0, pitch_tau=0.035, decay=0.26, click=0.25, drive=1.4, length=0.55,
         variant=0):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'kick{variant}')
    f = f_end + (f_start - f_end) * np.exp(-t / pitch_tau)
    body = sine(f, n) * perc(n, 0.0012, decay)
    c = filt(white(n, rng), lp(3200, 0.7), hp(300)) * perc(n, 0.0004, 0.005) * click
    out = soft_clip(body + c, drive)
    return out * np.clip((length - t) / 0.03, 0, 1)


@_ro
def snare(tone=190.0, tone_decay=0.05, noise_decay=0.13, noise_lp=7500.0, noise_hp=1100.0,
          tone_mix=0.55, length=0.38, variant=0, roomy=0.0):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'snare{variant}')
    f = tone * (1.0 + 0.25 * np.exp(-t / 0.01))
    body = (sine(f, n) + 0.45 * sine(f * 1.72, n)) * perc(n, 0.001, tone_decay) * tone_mix
    nz = filt(white(n, rng), hp(noise_hp, 0.7), lp(noise_lp, 0.7), peak(3500, 1.0, 3.0))
    nz *= perc(n, 0.0008, noise_decay)
    out = soft_clip(body + nz * 0.8, 1.2)
    if roomy:
        ir = reverb_ir(0.6, 0.35, rng, predelay=0.004, stereo=False, early=3)
        out = out + roomy * convolve(out, ir)[:n]
    return out * np.clip((length - t) / 0.03, 0, 1)


@_ro
def clap(variant=0, decay=0.12):
    n = ns(0.35)
    rng = _rng(f'clap{variant}')
    nz = filt(white(n, rng), bp(1300, 1.4), hp(600))
    env = np.zeros(n)
    t = np.arange(n) / SR
    for k, off in enumerate((0.0, 0.009, 0.018, 0.028)):
        tt = t - off
        env += np.where(tt >= 0, np.exp(-np.maximum(tt, 0) / (0.006 if k < 3 else decay)), 0.0)
    return nz * env * 0.6 * np.clip((0.35 - t) / 0.05, 0, 1)


_METAL = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)


@_ro
def hat(decay=0.035, length=0.12, bright=1.0, variant=0, metal=0.5):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'hat{variant}')
    m = sum(square(f * 1.9, n, 0.5) for f in _METAL) / 6.0
    m = filt(m, bp(8500, 1.2))
    nz = filt(white(n, rng), hp(6500 * bright, 0.7))
    out = (nz * (1.0 - metal) + m * metal * 1.5) * perc(n, 0.0006, decay)
    out = filt(out, lp(13500, 0.7))
    return out * np.clip((length - t) / 0.01, 0, 1)


@_ro
def shaker(variant=0, length=0.11, decay=0.035, center=5500.0):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'shk{variant}')
    nz = filt(white(n, rng), bp(center, 1.1), hp(2500))
    env = (1.0 - np.exp(-t / 0.012)) * np.exp(-t / decay)
    return nz * env * np.clip((length - t) / 0.01, 0, 1) * 1.8


@_ro
def rim(variant=0):
    n = ns(0.08)
    rng = _rng(f'rim{variant}')
    s = sine(1650.0, n) * perc(n, 0.0004, 0.012) + 0.6 * sine(520.0, n) * perc(n, 0.0004, 0.018)
    s += filt(white(n, rng), bp(3000, 1.5)) * perc(n, 0.0003, 0.004) * 0.6
    return s * 0.7


@_ro
def tom(freq=110.0, decay=0.3, variant=0, length=0.7):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'tom{variant}{freq}')
    f = freq * (1.0 + 0.6 * np.exp(-t / 0.02))
    body = sine(f, n) * perc(n, 0.0012, decay)
    skin = filt(white(n, rng), bp(freq * 6, 1.0)) * perc(n, 0.0005, 0.02) * 0.35
    return soft_clip(body + skin, 1.3) * np.clip((length - t) / 0.04, 0, 1)


@_ro
def taiko(freq=62.0, variant=0, length=1.2):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'taiko{variant}')
    f = freq * (1.0 + 0.45 * np.exp(-t / 0.03))
    body = sine(f, n) * perc(n, 0.002, 0.42) + 0.35 * sine(f * 1.58, n) * perc(n, 0.002, 0.15)
    thud = filt(white(n, rng), lp(500, 0.7)) * perc(n, 0.001, 0.05) * 1.2
    slap = filt(white(n, rng), bp(1800, 0.9)) * perc(n, 0.0005, 0.008) * 0.4
    return soft_clip(body + thud + slap, 1.5) * np.clip((length - t) / 0.05, 0, 1)


@_ro
def crash(length=2.8, decay=0.9, variant=0):
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'crash{variant}')
    m = sum(square(f * 2.7, n, 0.5) for f in _METAL) / 6.0
    nz = white(n, rng) * 0.7 + filt(m, hp(3000)) * 0.4
    nz = filt(nz, hp(3200, 0.7), lp(11000, 0.7), peak(6000, 0.8, 3.0))
    env = perc(n, 0.002, decay) * 0.7 + perc(n, 0.001, 0.05) * 0.5
    return nz * env * np.clip((length - t) / 0.3, 0, 1)


@_ro
def ride(variant=0):
    n = ns(0.9)
    rng = _rng(f'ride{variant}')
    m = sum(square(f * 3.1, n, 0.5) for f in _METAL) / 6.0
    s = filt(m, bp(5200, 1.5)) * perc(n, 0.001, 0.35) * 0.6
    s += filt(white(n, rng), hp(7000)) * perc(n, 0.0005, 0.03) * 0.3
    s += sine(3170.0, n) * perc(n, 0.001, 0.4) * 0.05
    return s


@_ro
def riser(seconds, f0=300.0, f1=7000.0, q=2.5, variant=0):
    """Noise riser ending at full level exactly at `seconds`."""
    n = ns(seconds)
    rng = _rng(f'riser{variant}')
    y = svf(white(n, rng), ramp(n, f0, f1, 'exp'), q, 'bp')
    env = ramp(n, 0.0, 1.0) ** 2.2
    y = y * env
    y[-ns(0.006):] *= np.linspace(1, 0, ns(0.006))
    return y


@_ro
def boom(freq=55.0, length=2.0, variant=0):
    """Cinematic low impact for section starts."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'boom{variant}')
    f = freq * 0.55 + freq * 0.45 * np.exp(-t / 0.12)
    body = sine(f, n) * perc(n, 0.002, 0.6)
    nz = filt(brown(n, rng), lp(300, 0.7)) * perc(n, 0.003, 0.4) * 2.0
    hit = filt(white(n, rng), lp(2500, 0.7)) * perc(n, 0.0005, 0.02) * 0.5
    return soft_clip(body + nz + hit, 1.6) * np.clip((length - t) / 0.2, 0, 1)


@_ro
def reverse_cymbal(seconds, variant=0):
    c = np.array(crash(seconds + 0.5, decay=0.8, variant=variant + 7))
    r = c[::-1][-ns(seconds):]
    return pad_to(r, ns(seconds))


@_ro
def air_bed(seconds, variant=0, center=6000.0):
    """Soft airy noise texture (breath of the jungle) - used under pads."""
    n = ns(seconds)
    rng = _rng(f'air{variant}')
    return filt(pink(n, rng), bp(center, 0.6), hp(1500)) * 0.5
