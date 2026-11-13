"""Arrangement helpers and loopable sound textures for the v2 songs and ambiences.

Everything that spans the whole loop is built periodic: noise is filtered circularly and every
modulation completes a whole number of cycles per loop, so the rendered file loops seamlessly.
"""
import numpy as np

from dsp import filt, bp, hp
from synth import SR, ns, sine, white, pink, brown, perc, fm, points


def blocks(prog):
    """Merge consecutive identical chords -> [[bar, n_bars, name], ...] (bars 1-based)."""
    out = []
    for i, name in enumerate(prog):
        if out and out[-1][2] == name:
            out[-1][1] += 1
        else:
            out.append([i + 1, 1, name])
    return out


def periodic_lfo(n, cycles, phase=0.0):
    """Sine with exactly `cycles` periods over n samples."""
    return np.sin(2 * np.pi * (cycles * np.arange(n) / n + phase))


def loop_noise(n, rng, color='pink', *responses):
    """Noise bed filtered circularly (periodic over n)."""
    if color == 'brown':
        x = brown(n, rng)
    elif color == 'white':
        x = white(n, rng) * 0.5
    else:
        x = pink(n, rng)
    return filt(x, *responses, circular=True) if responses else x


def air_layer(n, rng, center=5000.0, low=1800.0, swells=5, offset=0.5):
    """Stereo airy noise with a whole-number of slow swells over the loop."""
    nz = filt(pink(n, rng), bp(center, 0.6), hp(low, 0.7), circular=True)
    sw = 0.6 + 0.4 * periodic_lfo(n, swells)
    return np.vstack([nz * sw, np.roll(nz, int(n * offset)) * sw])


# --------------------------------------------------------------------------- Pyralis events

def lava_blub(rng, size=1.0):
    """Thick lava bubble: a rising resonant 'blub' and a soft pop."""
    d = 0.35 * size + 0.1
    n = ns(d)
    t = np.arange(n) / SR
    f0 = rng.uniform(70.0, 120.0) / size
    f = f0 * (1.0 + 1.6 * (1.0 - np.exp(-t / (0.05 * size))))
    blub = sine(f, n) * perc(n, 0.006, 0.07 * size)
    gl = filt(white(n, rng), bp(f0 * 4.0, 2.5))[:n] * perc(n, 0.004, 0.04 * size) * 0.5
    m = ns(0.05)
    pop = filt(white(m, rng), bp(rng.uniform(900.0, 1600.0), 1.5)) * perc(m, 0.0005, 0.006)
    out = blub + gl
    p = min(n - m, ns(0.06 * size + 0.04))
    out[p:p + m] += pop * 0.35
    return out


def fire_crackle(n, rng, rate=6.0):
    """Sparse wood/ember crackle grains over n samples (non-periodic placement; caller wraps)."""
    out = np.zeros(n)
    g = ns(0.004)
    k = int(rate * n / SR)
    for _ in range(k):
        p = rng.integers(0, n - g)
        gr = filt(white(g + 64, rng), hp(2000.0, 0.7))[:g] * np.hanning(g)
        out[p:p + g] += gr * rng.uniform(0.2, 1.0) ** 2
    return out


# --------------------------------------------------------------------------- Glacior events

def ice_creak(rng, seconds=1.6, f_lo=28.0, f_hi=70.0, res=(420.0, 1150.0)):
    """Stick-slip creak of stressed ice / frozen wood: an irregular pulse train whose rate
    wanders, through two resonances."""
    n = ns(seconds)
    t = np.arange(n) / SR
    rate = points(n, [(0, rng.uniform(f_lo, f_hi)), (seconds * 0.4, rng.uniform(f_lo, f_hi)),
                      (seconds, rng.uniform(f_lo, f_hi))], 'exp')
    rate = rate * (1.0 + 0.15 * np.sin(2 * np.pi * rng.uniform(2.0, 5.0) * t))
    ph = np.cumsum(rate / SR)
    idx = np.nonzero(np.diff(np.floor(ph)) > 0)[0]
    pulses = np.zeros(n)
    pulses[idx] = rng.uniform(0.4, 1.0, len(idx))
    pulses = np.convolve(pulses, np.hanning(9), mode='same')
    body = filt(pulses, bp(res[0], 6.0))[:n] + 0.6 * filt(pulses, bp(res[1], 8.0))[:n]
    env = points(n, [(0, 0), (seconds * 0.2, 1.0), (seconds * 0.7, 0.8), (seconds, 0)])
    return body * env


def ice_crack(rng, size=1.0):
    """Sharp ice crack: broadband snap, a dull thump and a short glassy ring."""
    d = 0.9
    n = ns(d)
    t = np.arange(n) / SR
    snap = filt(white(n, rng), hp(1500.0, 0.7))[:n] * perc(n, 0.0002, 0.006 * size)
    thump = sine(60.0 + 90.0 * np.exp(-t / 0.02), n) * perc(n, 0.001, 0.06) * 0.6
    ring = np.zeros(n)
    for f in (rng.uniform(1800, 2600), rng.uniform(3200, 4400)):
        ring += sine(f, n) * perc(n, 0.0005, 0.12) * 0.12
    zing = fm(points(n, [(0, 5200.0), (0.25, 2400.0)], 'exp'), n, 1.5, 1.0) * perc(n, 0.001, 0.05) * 0.08
    return snap + thump + ring + zing


def tinkle(rng, count=5, spread=0.5):
    """A few tiny ice crystals falling and clinking."""
    d = spread + 0.4
    n = ns(d)
    out = np.zeros(n)
    m = ns(0.35)
    for i in range(count):
        p = int(((i + rng.random()) / count) ** 1.4 * spread * SR)
        f = rng.uniform(3000.0, 7500.0)
        tt = np.arange(m) / SR
        g = fm(f, m, 2.76, 0.9 * np.exp(-tt / 0.01) + 0.1) * perc(m, 0.0005, rng.uniform(0.03, 0.08))
        out[p:p + m] += g * rng.uniform(0.3, 1.0)
    return out
