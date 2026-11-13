"""Reusable sound-design building blocks shared by every effect (the "synth sci-fi + organic"
palette: chirps, thumps, whooshes, FM bells, sparkles, resonant zaps, debris...)."""
import numpy as np

from synth import (SR, ns, curve, ramp, points, glide, perc, adsr, sine, saw, square, triangle,
                   fm, white, pink, brown, note_hz, morph_osc, lfo)
from dsp import (filt, lp, hp, bp, lp1, hp1, peak, highshelf, lowshelf, svf, convolve, reverb_ir,
                 echo, mix, place, pad_to, soft_clip, fade)

__all__ = ['SR', 'ns', 'curve', 'ramp', 'points', 'glide', 'perc', 'adsr', 'sine', 'saw', 'square',
           'triangle', 'fm', 'white', 'pink', 'brown', 'note_hz', 'morph_osc', 'lfo', 'filt', 'lp',
           'hp', 'bp', 'lp1', 'hp1', 'peak', 'highshelf', 'lowshelf', 'svf', 'convolve', 'echo',
           'mix', 'place', 'pad_to', 'soft_clip', 'fade', 'osc', 'tone', 'chirp', 'noise',
           'whoosh', 'thump', 'bell', 'chime', 'sparkle', 'zap', 'debris', 'crackle', 'room',
           'arp', 'swell', 'blip', 'np', 'hz']


def hz(x):
    return note_hz(x) if isinstance(x, str) else float(x)


def osc(shape, freq, n, rng=None, pw=0.5):
    """Basic oscillator by name. 'tri', 'sine', 'square', 'saw', 'soft' (sine+tri blend)."""
    if shape == 'sine':
        return sine(freq, n, rng.random() if rng is not None else 0.0)
    if shape == 'tri':
        return triangle(freq, n)
    if shape == 'square':
        return square(freq, n, pw)
    if shape == 'saw':
        return saw(freq, n)
    if shape == 'soft':
        return 0.75 * sine(freq, n) + 0.25 * triangle(freq, n)
    raise ValueError(shape)


def tone(freq, dur, shape='sine', attack=0.002, decay=None, release=0.02, lowpass=None):
    """A note with either a percussive decay (decay=tau) or a flat body with a release."""
    n = ns(dur)
    s = osc(shape, curve(hz(freq) if not np.ndim(freq) else freq, n), n)
    if lowpass:
        s = filt(s, lp(lowpass, 0.7))
    if decay:
        env = perc(n, attack, decay)
    else:
        env = adsr(n, attack, 0.05, 1.0, release, max(dur - release, 0.0))
    return s * env


def chirp(f0, f1, dur, shape='sine', time=None, curve_shape='exp', attack=0.002, decay=None,
          release=0.03, lowpass=None):
    """Pitch sweep f0 -> f1 over `time` (default whole note)."""
    n = ns(dur)
    f = glide(n, hz(f0), hz(f1), time if time is not None else dur, curve_shape)
    return tone(f, dur, shape, attack, decay, release, lowpass)


def noise(dur, rng, color='white'):
    n = ns(dur)
    if color == 'pink':
        return pink(n, rng) * 3.0
    if color == 'brown':
        return brown(n, rng) * 3.0
    return white(n, rng)


def whoosh(dur, rng, f_path=(400, 2500, 800), q=1.2, peak_at=0.4, color='pink', shape=2.0):
    """Band-passed noise sweep with a bell-shaped amplitude (air movement)."""
    n = ns(dur)
    x = noise(dur, rng, color)
    fc = np.exp(curve(np.log(np.asarray(f_path, dtype=float)), n))
    y = svf(x, fc, q, 'bp')
    t = np.linspace(0.0, 1.0, n)
    pa = max(min(peak_at, 0.95), 0.05)
    env = np.where(t < pa, (t / pa) ** shape, ((1.0 - t) / (1.0 - pa)) ** (shape * 0.8))
    return y * env


def thump(f0=120.0, f1=45.0, dur=0.25, tau=0.04, decay=0.08, click=0.0, rng=None, drive=1.2):
    """Low pitch-dropping sine body (kick-like impact)."""
    n = ns(dur)
    t = np.arange(n) / SR
    f = f1 + (f0 - f1) * np.exp(-t / tau)
    body = sine(f, n) * perc(n, 0.0015, decay)
    if click and rng is not None:
        c = filt(white(n, rng), lp(3500, 0.7)) * perc(n, 0.0005, 0.004)
        body = body + click * c
    return soft_clip(body, drive) if drive else body


def bell(freq, dur, ratio=3.5, index=2.5, decay=0.35, idx_decay=0.08, attack=0.001, idx_floor=0.15):
    """FM bell / chime voice. freq may be a curve."""
    n = ns(dur)
    t = np.arange(n) / SR
    idx = index * np.exp(-t / idx_decay) + idx_floor
    f = curve(hz(freq) if not np.ndim(freq) else freq, n)
    return fm(f, n, ratio, idx) * perc(n, attack, decay)


def chime(freq, dur, decay=0.5, bright=1.0):
    """Glassy chime: FM bell + inharmonic partials."""
    f = hz(freq)
    n = ns(dur)
    s = bell(f, dur, 1.0, 1.2 * bright, decay, 0.05)
    s += 0.35 * sine(f * 2.76, n) * perc(n, 0.001, decay * 0.45)
    s += 0.18 * bright * sine(f * 5.40, n) * perc(n, 0.001, decay * 0.2)
    s += 0.08 * bright * sine(f * 8.93, n) * perc(n, 0.001, decay * 0.1)
    return s


def sparkle(dur, rng, count=14, fmin=2500.0, fmax=8000.0, grain=0.06, density=None, level=1.0):
    """Random tiny high FM grains (glints / particles). density: curve over time (0..1)."""
    n = ns(dur)
    out = np.zeros(n)
    if density is None:
        density = np.ones(64)
    dens = curve(density, 256)
    cdf = np.cumsum(dens + 1e-6)
    cdf /= cdf[-1]
    for _ in range(count):
        pos = np.interp(rng.random(), cdf, np.linspace(0, 1, 256))
        start = int(pos * max(n - ns(grain), 1))
        f = np.exp(rng.uniform(np.log(fmin), np.log(fmax)))
        g = bell(f, grain, ratio=rng.choice([1.41, 2.0, 3.0]), index=1.5, decay=grain * 0.3,
                 idx_decay=grain * 0.2, attack=0.003)
        g *= rng.uniform(0.4, 1.0)
        e = min(n, start + len(g))
        out[start:e] += g[:e - start]
    return out * level


def zap(f0, f1, dur, cut0=None, cut1=None, q=5.0, shape='saw', time=None, attack=0.002,
        decay=None, release=0.05):
    """Resonant filtered oscillator sweep (sci-fi zap)."""
    n = ns(dur)
    f = glide(n, hz(f0), hz(f1), time if time is not None else dur, 'exp')
    s = saw(f, n) if shape == 'saw' else square(f, n)
    c0 = cut0 if cut0 else hz(f0) * 4
    c1 = cut1 if cut1 else hz(f1) * 4
    fc = glide(n, c0, c1, time if time is not None else dur, 'exp')
    y = svf(s, fc, q, 'lp')
    if decay:
        env = perc(n, attack, decay)
    else:
        env = adsr(n, attack, 0.05, 1.0, release, max(dur - release, 0.0))
    return y * env


def debris(dur, rng, count=20, fmin=300.0, fmax=3000.0, grain=(0.004, 0.03), start=0.0,
           spread=None, gravity=True):
    """Many short band-passed noise grains (stones, crunch). Grains thin out over time."""
    n = ns(dur)
    out = np.zeros(n)
    s0 = ns(start)
    span = (n - s0) if spread is None else ns(spread)
    for _ in range(count):
        u = rng.random()
        if gravity:
            u = u ** 1.8
        pos = s0 + int(u * max(span - 1, 1))
        g_len = rng.uniform(*grain)
        m = ns(g_len) + 32
        f = np.exp(rng.uniform(np.log(fmin), np.log(fmax)))
        gr = filt(white(m, rng), bp(f, rng.uniform(1.5, 4.0))) * perc(m, 0.0005, g_len * 0.35)
        gr *= rng.uniform(0.3, 1.0) * (1.0 - 0.6 * u)
        e = min(n, pos + m)
        out[pos:e] += gr[:e - pos]
    return out


_GRAIN = np.sin(np.pi * np.minimum(np.arange(24) / 3.0, 0.5)) * np.exp(-np.arange(24) / 4.0)


def crackle(dur, rng, rate=80.0, decay=True):
    """Sparse random clicks (fire / electricity)."""
    n = ns(dur)
    out = np.zeros(n)
    k = int(rate * dur)
    for _ in range(k):
        p = int(rng.random() ** (1.5 if decay else 1.0) * (n - 64))
        out[p:p + 24] += rng.uniform(-1, 1) * _GRAIN
    return filt(out, hp(1500, 0.7))


def room(x, rng, t60=0.5, wet=0.2, size=None, predelay=0.008, hf=0.35):
    """Small synthetic room/space reverb (keeps the tail)."""
    secs = size if size else min(3.0, t60 * 1.3)
    ir = reverb_ir(secs, t60, rng, hf_ratio=hf, predelay=predelay, stereo=False, early=4)
    w = convolve(x, ir)
    return pad_to(x, len(w)) + wet * w


def arp(notes, step, voice, start=0.0, vel=None):
    """Sequence notes (hz or names) with `step` seconds spacing; voice(freq, i) -> array."""
    parts = []
    for i, nt in enumerate(notes):
        a = voice(hz(nt), i)
        if vel is not None:
            a = a * vel[i]
        parts.append((start + i * step, a))
    return place(parts)


def swell(x, attack_frac=0.7, power=2.0):
    """Reverse-like swell envelope applied over x."""
    n = len(x)
    t = np.linspace(0, 1, n)
    env = np.where(t < attack_frac, (t / attack_frac) ** power,
                   np.clip(1.0 - (t - attack_frac) / (1.0 - attack_frac + 1e-9), 0, 1))
    return x * env


def blip(freq, dur, shape='tri', attack=0.001, decay=None):
    """Very short clean UI tone."""
    return tone(freq, dur, shape, attack, decay if decay else dur * 0.35)
