"""Building blocks for seamless sound-effect loops (glide_loop, charge_loop, updraft_loop).

A loop buffer of n samples must be exactly periodic. Three techniques are used:
  * circular processing: noise filtered with circular FFT filters / a pre-rolled SVF, every
    modulation completing a whole number of cycles per loop, tones at whole-cycle frequencies;
  * wrap-around placement of events (grains that cross the loop end continue at the start);
  * folding: a long, enveloped (zero at both ends) process of k*n samples is cut into k pieces
    of n samples which are summed - the overlap-add is periodic by construction. This gives
    endlessly rising Shepard glissandi and 'barber-pole' noise sweeps.
"""
import numpy as np

from dsp import filt, svf
from synth import SR, ns, pink, brown, white


def loop_n(seconds):
    return ns(seconds)


def periodic(n, cycles, phase=0.0):
    """Sine with exactly `cycles` periods over n samples."""
    return np.sin(2 * np.pi * (cycles * np.arange(n) / n + phase))


def whole_hz(f, n):
    """Frequency rounded to a whole number of cycles over n samples."""
    return max(1, round(f * n / SR)) * SR / n


def circ_noise(n, rng, color='pink', *responses):
    if color == 'brown':
        x = brown(n, rng)
    elif color == 'white':
        x = white(n, rng) * 0.5
    else:
        x = pink(n, rng)
    return filt(x, *responses, circular=True) if responses else x


def wrap_add(dst, src, pos):
    """Add src into the loop buffer dst at pos, wrapping past the end."""
    n = len(dst)
    pos %= n
    off = 0
    while off < len(src):
        chunk = min(len(src) - off, n - pos)
        dst[pos:pos + chunk] += src[off:off + chunk]
        off += chunk
        pos = 0
    return dst


def fold(z, n):
    """Overlap-add a long signal (whose envelope is zero at both ends) into one period."""
    k = int(np.ceil(len(z) / n))
    zz = np.zeros(k * n)
    zz[:len(z)] = z
    return zz.reshape(k, n).sum(axis=0)


def _hann_env(m):
    u = np.arange(m) / m
    return 0.5 - 0.5 * np.cos(2 * np.pi * u)


def shepard(n, f0, octaves, partials=((1.0, 1.0),), rng=None):
    """Endlessly rising glissando: one sine sweep of `octaves` octaves (one octave per loop),
    raised-cosine enveloped over its whole length, folded into n samples."""
    m = n * octaves
    u = np.arange(m) / n                          # octaves risen
    f = f0 * 2.0 ** u
    ph = np.cumsum(f) / SR
    p0 = rng.random() if rng is not None else 0.0
    z = np.zeros(m)
    for ratio, amp in partials:
        z += amp * np.sin(2 * np.pi * (ratio * ph + p0))
    return fold(z * _hann_env(m), n)


def barber_noise(n, rng, f_lo, f_hi, layers=3, q=2.0, color='pink'):
    """Band of noise whose centre rises forever: one sweep f_lo -> f_hi over `layers` loops,
    enveloped and folded (so `layers` overlapping bands are always rising)."""
    m = n * layers
    x = pink(m, rng) if color == 'pink' else white(m, rng) * 0.5
    fc = f_lo * (f_hi / f_lo) ** (np.arange(m) / m)
    y = svf(x, fc, q, 'bp')
    return fold(y * _hann_env(m), n)


def wrapped_svf(x, cutoff, q, mode='bp'):
    """Time-varying SVF on a periodic buffer (cutoff must itself be periodic)."""
    return svf(x, cutoff, q, mode, circular=True)
