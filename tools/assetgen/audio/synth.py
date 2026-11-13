"""Synthesis primitives: pitch helpers, envelopes, oscillators, wavetables, FM, plucked strings.

Everything is plain numpy, mono float64 arrays at SR, and deterministic (randomness always comes
from an explicit ``numpy.random.Generator``).
"""
from functools import lru_cache

import numpy as np

SR = 44100
TAU = 2.0 * np.pi


# --------------------------------------------------------------------------- pitch / time

def ns(seconds):
    """Seconds -> sample count."""
    return max(0, int(round(float(seconds) * SR)))


def midi_hz(m):
    return 440.0 * 2.0 ** ((np.asarray(m, dtype=float) - 69.0) / 12.0)


_NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def note_midi(name):
    """'C4' -> 60, 'F#5' -> 78, 'Bb3' -> 58. Integers pass through."""
    if isinstance(name, (int, np.integer)):
        return int(name)
    s = name.strip()
    value = _NOTE[s[0].upper()]
    i = 1
    while i < len(s) and s[i] in '#b':
        value += 1 if s[i] == '#' else -1
        i += 1
    return 12 * (int(s[i:]) + 1) + value


def note_hz(name):
    return float(midi_hz(note_midi(name)))


def cents(c):
    return 2.0 ** (np.asarray(c, dtype=float) / 1200.0)


def time_axis(n):
    return np.arange(n) / SR


# --------------------------------------------------------------------------- control curves

def curve(value, n):
    """Scalar -> constant array; short list -> linearly stretched to n; array of n -> itself."""
    if np.isscalar(value):
        return np.full(n, float(value))
    a = np.asarray(value, dtype=float)
    if len(a) == n:
        return a
    if len(a) == 1:
        return np.full(n, a[0])
    return np.interp(np.linspace(0.0, 1.0, n), np.linspace(0.0, 1.0, len(a)), a)


def ramp(n, v0, v1, shape='lin'):
    t = np.linspace(0.0, 1.0, n) if n > 1 else np.zeros(n)
    if shape == 'exp':
        return v0 * (v1 / v0) ** t
    if shape == 'cos':
        return v0 + (v1 - v0) * (0.5 - 0.5 * np.cos(np.pi * t))
    if shape == 'sq':  # fast start, slow end
        return v0 + (v1 - v0) * (1.0 - (1.0 - t) ** 2)
    return v0 + (v1 - v0) * t


def points(n, pts, shape='lin'):
    """Piecewise curve through [(t_seconds, value), ...]; holds the end values."""
    ts = np.array([p[0] for p in pts], dtype=float) * SR
    vs = np.array([p[1] for p in pts], dtype=float)
    idx = np.arange(n, dtype=float)
    if shape == 'exp':
        return np.exp(np.interp(idx, ts, np.log(np.maximum(vs, 1e-9))))
    return np.interp(idx, ts, vs)


def glide(n, f0, f1, time, shape='exp'):
    """Frequency glide from f0 to f1 over `time` seconds, then hold f1."""
    out = np.full(n, float(f1))
    m = min(n, ns(time))
    if m > 0:
        out[:m] = ramp(m, f0, f1, shape)
    return out


def exp_glide(n, f0, f1, tau):
    """Exponential approach from f0 to f1 with time constant tau (seconds)."""
    t = np.arange(n) / SR
    return f1 + (f0 - f1) * np.exp(-t / tau)


def perc(n, attack=0.002, decay=0.2, tail=0.006):
    """Smooth attack then exponential decay (time constant `decay`). The last `tail` seconds
    are faded to exactly zero so a buffer that ends mid-decay never clicks."""
    t = np.arange(n) / SR
    a = max(attack, 1.0 / SR)
    rise = np.sin(0.5 * np.pi * np.clip(t / a, 0.0, 1.0))
    env = rise * np.exp(-np.maximum(t - a, 0.0) / decay)
    m = min(n // 4, ns(tail))
    if m > 1:
        env[n - m:] *= 0.5 + 0.5 * np.cos(np.pi * (np.arange(m) + 0.5) / m)
    return env


def adsr(n, attack, decay, sustain, release, hold):
    """ADSR envelope. `hold` = gate time in seconds; release after it, reaches 0 at hold+release."""
    t = np.arange(n) / SR
    a = max(attack, 1.0 / SR)
    d = max(decay, 1.0 / SR)
    env = np.where(t < a, 1.0 - (1.0 - t / a) ** 2,
                   sustain + (1.0 - sustain) * np.exp(-np.maximum(t - a, 0.0) / (d / 3.0)))
    # level at gate-off
    if hold < a:
        lvl = 1.0 - (1.0 - hold / a) ** 2
    else:
        lvl = sustain + (1.0 - sustain) * np.exp(-(hold - a) / (d / 3.0))
    rel = max(release, 1.0 / SR)
    tr = t - hold
    after = lvl * np.exp(-np.maximum(tr, 0.0) / (rel / 4.0)) * np.clip(1.0 - tr / rel, 0.0, 1.0)
    return np.where(t < hold, env, after)


def lfo(n, rate, depth=1.0, phase0=0.0, shape='sine'):
    p = TAU * (rate * np.arange(n) / SR + phase0)
    if shape == 'tri':
        return depth * (2.0 / np.pi) * np.arcsin(np.sin(p))
    return depth * np.sin(p)


def vibrato(n, rate=5.2, depth_cents=15.0, delay=0.25, fade=0.35, phase0=0.0):
    """Frequency multiplier with delayed, faded-in vibrato."""
    t = np.arange(n) / SR
    amt = np.clip((t - delay) / max(fade, 1e-3), 0.0, 1.0)
    return cents(depth_cents * amt * np.sin(TAU * (rate * t + phase0)))


# --------------------------------------------------------------------------- noise

def white(n, rng):
    return rng.uniform(-1.0, 1.0, n)


def colored(n, rng, slope_db_per_oct=-3.0):
    """Noise with a spectral slope (-3 pink, -6 brown), RMS ~0.3."""
    x = rng.standard_normal(n)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    f[0] = f[1] if n > 1 else 1.0
    X *= (f / 1000.0) ** (slope_db_per_oct / 6.0206)
    X[0] = 0.0
    y = np.fft.irfft(X, n)
    return 0.3 * y / (np.sqrt(np.mean(y ** 2)) + 1e-12)


def pink(n, rng):
    return colored(n, rng, -3.0)


def brown(n, rng):
    return colored(n, rng, -6.0)


# --------------------------------------------------------------------------- oscillators

def phase(freq, n, phase0=0.0):
    """Unwrapped phase in cycles for a (possibly time-varying) frequency."""
    f = curve(freq, n)
    ph = np.empty(n)
    if n:
        ph[0] = 0.0
        np.cumsum(f[:-1] / SR, out=ph[1:])
    return ph + phase0


def sine(freq, n, phase0=0.0):
    return np.sin(TAU * phase(freq, n, phase0))


def _polyblep(t, dt):
    out = np.zeros_like(t)
    m = t < dt
    x = t[m] / dt[m]
    out[m] = x + x - x * x - 1.0
    m2 = t > 1.0 - dt
    x = (t[m2] - 1.0) / dt[m2]
    out[m2] = x * x + x + x + 1.0
    return out


def saw(freq, n, phase0=0.0):
    f = curve(freq, n)
    ph = phase(f, n, phase0) % 1.0
    dt = np.clip(np.abs(f) / SR, 1e-7, 0.5)
    return 2.0 * ph - 1.0 - _polyblep(ph, dt)


def square(freq, n, pw=0.5, phase0=0.0):
    f = curve(freq, n)
    ph = phase(f, n, phase0) % 1.0
    dt = np.clip(np.abs(f) / SR, 1e-7, 0.5)
    s = np.where(ph < pw, 1.0, -1.0)
    s += _polyblep(ph, dt)
    s -= _polyblep((ph - pw) % 1.0, dt)
    return s


def triangle(freq, n, phase0=0.0):
    ph = phase(freq, n, phase0) % 1.0
    return 4.0 * np.abs(ph - 0.5) - 1.0


def fm(freq, n, ratio=1.0, index=1.0, phase0=0.0, mod_phase0=0.0, mod_hz=None):
    """Two-operator FM: sin(carrier + index * sin(modulator)). index in radians (scalar/curve)."""
    f = curve(freq, n)
    pc = phase(f, n, phase0)
    fmod = f * ratio if mod_hz is None else curve(mod_hz, n)
    pm = phase(fmod, n, mod_phase0)
    return np.sin(TAU * pc + curve(index, n) * np.sin(TAU * pm))


# --------------------------------------------------------------------------- wavetables

TABLE = 2048


@lru_cache(maxsize=8)
def _basis(nh):
    k = np.arange(1, nh + 1, dtype=float)[:, None]
    i = np.arange(TABLE + 1, dtype=float)[None, :]
    return np.sin(TAU * k * i / TABLE)


def spectrum(shape, nh):
    k = np.arange(1, nh + 1, dtype=float)
    if shape == 'saw':
        return 1.0 / k
    if shape == 'square':
        return np.where(k % 2 == 1, 1.0 / k, 0.0)
    if shape == 'tri':
        return np.where(k % 2 == 1, ((-1.0) ** ((k - 1) // 2)) / k ** 2, 0.0)
    if shape == 'pulse':  # 30 % pulse, nasal
        return np.sin(np.pi * k * 0.3) / k
    if shape == 'sine':
        return (k == 1).astype(float)
    raise ValueError(shape)


def lp_mag(freqs, fc, q=0.7071):
    x = freqs / fc
    return 1.0 / np.sqrt((1.0 - x * x) ** 2 + (x / q) ** 2)


def tables_from_amps(amps):
    amps = np.atleast_2d(amps)
    return amps @ _basis(amps.shape[1])


def table_osc(table, freq, n, phase0=0.0):
    ph = (phase(freq, n, phase0) % 1.0) * TABLE
    i = ph.astype(np.int64)
    fr = ph - i
    return table[i] * (1.0 - fr) + table[i + 1] * fr


def harmonic_osc(amps, freq, n, phase0=0.0):
    """Oscillator from explicit harmonic amplitudes (band-limited for the given pitch)."""
    f = curve(freq, n)
    nh = max(1, min(len(amps), int(0.47 * SR / max(f.max(), 1.0))))
    table = tables_from_amps(np.asarray(amps[:nh], dtype=float))[0]
    return table_osc(table, f, n, phase0)


@lru_cache(maxsize=512)
def _morph_tables(shape, nh, f_ref, lv, q, poles):
    base = spectrum(shape, nh)
    fr = np.arange(1, nh + 1) * f_ref
    lv = np.asarray(lv)
    amps = base[None, :] * lp_mag(fr[None, :], lv[:, None], q) ** (poles / 2.0)
    return tables_from_amps(amps)


def morph_osc(freq, n, cutoff, shape='saw', q=0.7071, poles=4, k_tables=12, phase0=0.0):
    """Band-limited oscillator through a time-varying low-pass (wavetable morph emulation).

    cutoff: Hz (scalar or curve). Resonance via q > 0.707.
    """
    f = curve(freq, n)
    c = np.maximum(curve(cutoff, n), 20.0)
    f_ref = float(np.round(np.median(f), 2))
    nh = max(1, min(256, int(0.47 * SR / max(f.max(), 1.0))))
    lo, hi = float(c.min()), float(c.max())
    if hi / lo < 1.01:
        hi = lo * 1.01
    lv = tuple(np.round(np.geomspace(lo, hi, k_tables), 3))
    tbl = _morph_tables(shape, nh, f_ref, lv, round(q, 4), poles)
    pos = np.interp(np.log(c), np.log(np.asarray(lv)), np.arange(k_tables, dtype=float))
    ph = (phase(f, n, phase0) % 1.0) * TABLE
    i = ph.astype(np.int64)
    fr = ph - i
    j = np.minimum(pos.astype(np.int64), k_tables - 2)
    w = pos - j
    s0 = tbl[j, i] * (1.0 - fr) + tbl[j, i + 1] * fr
    s1 = tbl[j + 1, i] * (1.0 - fr) + tbl[j + 1, i + 1] * fr
    return s0 * (1.0 - w) + s1 * w


# --------------------------------------------------------------------------- plucked string

def pluck(freq, seconds, rng, t60=1.5, bright=0.6, pick=0.13):
    """Karplus-Strong string, exactly tuned (rendered at a matched internal rate, then resampled)."""
    n_out = ns(seconds)
    period = SR / freq
    N = max(2, int(round(period - 0.5)))
    sr_int = freq * (N + 0.5)
    n_int = int(np.ceil(seconds * sr_int)) + 4
    exc = rng.uniform(-1.0, 1.0, N)
    # brightness: blend noise with a smoothed copy
    k = max(1, int(round((1.0 - bright) * 6)) + 1)
    smooth = np.convolve(np.concatenate([exc, exc[:k]]), np.ones(k) / k, mode='valid')[:N]
    exc = bright * exc + (1.0 - bright) * smooth
    if pick > 0:
        exc = exc - np.roll(exc, max(1, int(N * pick)))
    exc -= exc.mean()
    g = 0.001 ** (1.0 / (freq * t60))  # per-period loss for the requested T60
    y = np.zeros(n_int + 1)
    y[1:N + 1] = exc
    start = N + 1
    while start < n_int + 1:
        end = min(start + N, n_int + 1)
        y[start:end] = g * 0.5 * (y[start - N:end - N] + y[start - N - 1:end - N - 1])
        start = end
    y = y[1:]
    t_out = np.arange(n_out) * (sr_int / SR)
    out = np.interp(t_out, np.arange(n_int), y)
    return out / (np.max(np.abs(out)) + 1e-12)
