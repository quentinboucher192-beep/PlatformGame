"""Extra percussion for the v2 planets: forge hammers and anvil clangs, metallic hats, chains,
steam bursts, brushes and glassy ice ticks. Mono, memoised like music.drums."""
import numpy as np

from dsp import filt, lp, hp, bp, peak, soft_clip
from music.drums import _ro, _rng, hat as _hat
from synth import SR, ns, sine, white, pink, brown, perc

_BAR = ((1.0, 1.0, 1.0), (2.756, 0.6, 0.5), (5.404, 0.38, 0.28), (8.933, 0.22, 0.16))


def _bar_ring(freq, n, decay, rng, bright=1.0):
    t = np.arange(n) / SR
    out = np.zeros(n)
    for i, (r, a, dm) in enumerate(_BAR):
        if freq * r > 15000.0:
            break
        out += a * bright ** i * np.sin(2 * np.pi * (freq * r * t + rng.random())) * perc(n, 0.0003, decay * dm)
    return out


@_ro
def clang(freq=1180.0, decay=0.45, variant=0, bright=1.0, length=1.6):
    """Unpitched-ish anvil clang for the drum kit (hammer on cold steel)."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'clang{variant}{freq}')
    ring = _bar_ring(freq, n, decay, rng, bright)
    ring += 0.5 * _bar_ring(freq * 1.37, n, decay * 0.6, rng, bright)
    tick = filt(white(n, rng), bp(4200.0, 0.8)) * perc(n, 0.0002, 0.004) * 0.9
    body = sine(freq * 0.18, n) * perc(n, 0.001, 0.03) * 0.4
    return (ring * 0.5 + tick + body) * np.clip((length - t) / 0.1, 0, 1)


@_ro
def hammer(variant=0, freq=820.0, length=1.2, ring=0.55):
    """Forge hammer blow on hot metal: dull thud + short clang + crack."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'hammer{variant}')
    f = 70.0 + 90.0 * np.exp(-t / 0.02)
    thud = sine(f, n) * perc(n, 0.001, 0.07)
    crack = filt(white(n, rng), bp(2600.0, 0.8)) * perc(n, 0.0003, 0.006)
    ring_ = _bar_ring(freq, n, 0.35, rng, 0.8) * ring
    out = soft_clip(thud * 1.1 + crack * 0.8, 1.4) + ring_ * 0.45
    return out * np.clip((length - t) / 0.1, 0, 1)


@_ro
def metal_hat(decay=0.04, length=0.14, variant=0, ring=0.35):
    """Closed hat with an extra steel 'tsk' resonance."""
    h = np.array(_hat(decay=decay, length=length, bright=1.05, metal=0.8, variant=variant + 11))
    n = len(h)
    rng = _rng(f'mhat{variant}')
    r = _bar_ring(6200.0, n, decay * 1.8, rng, 0.7) * ring
    return h + r * 0.3


@_ro
def chain(seconds=0.6, count=9, variant=0):
    """Rattling chain: a burst of small irregular metallic clinks."""
    n = ns(seconds)
    rng = _rng(f'chain{variant}')
    out = np.zeros(n)
    m = ns(0.12)
    for i in range(count):
        u = (i + rng.random() * 0.8) / count
        p = int(u ** 1.3 * (n - m))
        f = rng.uniform(1800.0, 4200.0)
        c = _bar_ring(f, m, rng.uniform(0.02, 0.05), rng, 0.8)
        c += filt(white(m, rng), bp(f * 1.5, 1.5)) * perc(m, 0.0002, 0.003)
        out[p:p + m] += c * rng.uniform(0.35, 1.0) * (1.0 - 0.5 * u)
    return out * 0.6


@_ro
def steam(seconds=0.9, variant=0, center=5200.0):
    """Pressure-release hiss: fast attack, sustained, tapering."""
    n = ns(seconds)
    t = np.arange(n) / SR
    rng = _rng(f'steam{variant}')
    nz = filt(white(n, rng), hp(2400.0, 0.7), bp(center, 0.7), lp(11000.0, 0.7))
    env = (1.0 - np.exp(-t / 0.015)) * np.exp(-t / (seconds * 0.45))
    return nz * env * np.clip((seconds - t) / 0.08, 0, 1) * 0.9


@_ro
def brush(variant=0, length=0.32, decay=0.09, center=3400.0):
    """Brushed snare swish (soft attack, airy)."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'brush{variant}')
    nz = filt(white(n, rng), bp(center, 0.6), hp(1200.0, 0.7))
    env = (1.0 - np.exp(-t / 0.008)) * np.exp(-t / decay)
    body = sine(210.0 * (1.0 + 0.2 * np.exp(-t / 0.01)), n) * perc(n, 0.002, 0.04) * 0.25
    return (nz + body) * env * np.clip((length - t) / 0.04, 0, 1)


@_ro
def ice_tick(freq=3900.0, variant=0, decay=0.018):
    """Glassy click (frozen clockwork)."""
    n = ns(0.12)
    rng = _rng(f'itick{variant}{freq}')
    s = sine(freq, n) * perc(n, 0.0003, decay) + 0.5 * sine(freq * 2.76, n) * perc(n, 0.0003, decay * 0.5)
    s += filt(white(n, rng), hp(6000.0, 0.7)) * perc(n, 0.0002, 0.002) * 0.6
    return s * 0.6


@_ro
def sub_hit(freq=48.0, length=1.4, variant=0):
    """Deep cinematic sub thump with a dark noise bloom (section downbeats)."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'sub{variant}')
    f = freq * (0.7 + 0.6 * np.exp(-t / 0.08))
    body = sine(f, n) * perc(n, 0.003, 0.45)
    bloom = filt(brown(n, rng), lp(260.0, 0.7)) * perc(n, 0.004, 0.3) * 1.6
    return soft_clip(body + bloom, 1.4) * np.clip((length - t) / 0.2, 0, 1)


@_ro
def frame_drum(freq=95.0, variant=0, length=0.6):
    """Soft low frame drum (felt mallet) for quiet sections."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'frame{variant}{freq}')
    f = freq * (1.0 + 0.3 * np.exp(-t / 0.025))
    body = sine(f, n) * perc(n, 0.003, 0.18) + 0.3 * sine(f * 1.59, n) * perc(n, 0.003, 0.07)
    skin = filt(pink(n, rng), lp(700.0, 0.7), peak(300.0, 1.0, 4.0)) * perc(n, 0.002, 0.03) * 0.6
    return (body + skin) * np.clip((length - t) / 0.05, 0, 1)
