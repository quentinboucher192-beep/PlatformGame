"""Melodic instruments. Signature: inst(midi, dur_seconds, vel, seed=0, **params) -> mono (n,) or
stereo (2, n) float array (the note including its release tail). Results are memoised, so
repeated notes (ostinatos, bass riffs) are rendered once."""
import zlib

import numpy as np

from dsp import filt, bp, lp, soft_clip
from synth import (SR, ns, midi_hz, adsr, perc, sine, fm, white, morph_osc, harmonic_osc, cents,
                   vibrato, pluck as ks_pluck)

_CACHE = {}


def instrument(fn):
    def wrapped(midi, dur, vel=0.8, seed=0, **kw):
        key = (fn.__name__, int(midi), round(float(dur), 4), round(float(vel), 3), seed,
               tuple(sorted(kw.items())))
        out = _CACHE.get(key)
        if out is None:
            out = fn(int(midi), float(dur), float(vel), seed, **kw)
            out.setflags(write=False)
            _CACHE[key] = out
        return out
    wrapped.__name__ = fn.__name__
    return wrapped


def clear_cache():
    _CACHE.clear()


def _rng(name, midi, seed):
    return np.random.default_rng(zlib.crc32(f'{name}:{midi}:{seed}'.encode()))


def _spread(sig_list, width):
    """Pan a list of mono voices evenly across [-width, width] -> stereo."""
    k = len(sig_list)
    L = np.zeros_like(sig_list[0])
    R = np.zeros_like(sig_list[0])
    for i, s in enumerate(sig_list):
        x = (2.0 * i / (k - 1) - 1.0) * width if k > 1 else 0.0
        th = (x + 1.0) * np.pi / 4.0
        L += s * np.cos(th) * np.sqrt(2.0)
        R += s * np.sin(th) * np.sqrt(2.0)
    return np.vstack([L, R])


# --------------------------------------------------------------------------- pads

@instrument
def pad_warm(midi, dur, vel, seed, attack=1.2, release=2.2, bright=5.0, max_cut=2600.0,
             voices=3, detune=9.0, width=0.8, shape='saw'):
    """Lush detuned analog pad with a slowly opening filter (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('pad', midi, seed)
    env = adsr(n, attack, 1.0, 0.8, release, dur)
    c0 = min(f * bright, max_cut)
    cut = c0 * (0.55 + 0.45 * np.clip(t / (attack * 1.8), 0.0, 1.0))
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        drift = 1.0 + 0.0012 * np.sin(2 * np.pi * (0.13 + 0.07 * v) * t + rng.random() * 6.283)
        fv = f * float(cents(x * detune)) * drift
        sigs.append(morph_osc(fv, n, cut, shape, q=0.6, poles=2, k_tables=6, phase0=rng.random()))
    return _spread(sigs, width) * env * vel * (0.55 / np.sqrt(voices))


_VOWELS = {
    'a': ((730, 90, 1.0), (1090, 110, 0.5), (2440, 160, 0.22), (3400, 220, 0.08)),
    'o': ((550, 80, 1.0), (850, 100, 0.5), (2400, 150, 0.08), (3300, 200, 0.03)),
    'u': ((330, 60, 1.0), (750, 80, 0.25), (2350, 150, 0.04), (3200, 200, 0.02)),
}


def _formant_amps(f0, vowel):
    nh = max(1, int(0.45 * SR / f0))
    k = np.arange(1, nh + 1)
    fr = k * f0
    env = np.full(nh, 0.015)
    for F, bw, g in _VOWELS[vowel]:
        env += g / (1.0 + ((fr - F) / bw) ** 2)
    return env / k ** 0.3


@instrument
def pad_choir(midi, dur, vel, seed, vowel='a', attack=0.4, release=1.4, voices=4, detune=11.0,
              width=0.7):
    """Formant-synthesised choir 'aah' / 'ooh' (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    rng = _rng('choir', midi, seed)
    amps = _formant_amps(f, vowel)
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        vib = vibrato(n, 4.8 + 0.4 * rng.random(), 10.0, delay=0.15, fade=0.5, phase0=rng.random())
        sigs.append(harmonic_osc(amps, f * float(cents(x * detune)) * vib, n, rng.random()))
    st = _spread(sigs, width)
    breath = filt(white(n, rng), bp(_VOWELS[vowel][1][0] * 1.4, 1.5)) * 0.04
    env = adsr(n, attack, 0.8, 0.85, release, dur)
    st = (st + breath) * env
    return st * vel * (0.5 / np.sqrt(voices)) / (np.max(amps) * 2.5)


@instrument
def strings_legato(midi, dur, vel, seed, attack=0.22, release=0.6, voices=4, detune=10.0,
                   width=0.75, bright=4.0):
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('strl', midi, seed)
    cut = np.minimum(f * bright, 4500.0) * (0.6 + 0.4 * np.clip(t / (attack * 2), 0, 1))
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        vib = vibrato(n, 5.3 + 0.5 * rng.random(), 9.0, delay=0.1, fade=0.4, phase0=rng.random())
        sigs.append(morph_osc(f * float(cents(x * detune)) * vib, n, cut, 'saw', q=0.7, poles=4,
                              k_tables=6, phase0=rng.random()))
    env = adsr(n, attack, 0.5, 0.85, release, dur)
    return _spread(sigs, width) * env * vel * (0.6 / np.sqrt(voices))


@instrument
def strings_stacc(midi, dur, vel, seed, voices=4, detune=12.0, width=0.8, decay=0.11):
    """Short spiccato ensemble hit (stereo)."""
    f = float(midi_hz(midi))
    gate = min(dur, 0.35)
    n = ns(gate + 0.12)
    t = np.arange(n) / SR
    rng = _rng('strs', midi, seed)
    cut = np.minimum(f * (2.2 + 6.0 * vel * np.exp(-t / 0.05)), 7000.0)
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        sigs.append(morph_osc(f * float(cents(x * detune)), n, cut, 'saw', q=0.8, poles=4,
                              k_tables=8, phase0=rng.random()))
    scrape = filt(white(n, rng), bp(min(f * 6, 6000), 1.2)) * perc(n, 0.002, 0.015) * 0.25
    env = perc(n, 0.004, decay) * np.clip((gate + 0.1 - t) / 0.1, 0.0, 1.0)
    st = _spread(sigs, width) * (0.7 / np.sqrt(voices)) + scrape
    return st * env * vel


# --------------------------------------------------------------------------- keys / mallets

@instrument
def epiano(midi, dur, vel, seed, release=0.35, bright=1.0, decay=None):
    """Soft FM electric piano (dreamy arps)."""
    f = float(midi_hz(midi))
    tau = decay if decay else float(np.clip(1.6 * (262.0 / f) ** 0.5, 0.4, 2.2))
    n = ns(dur + release)
    t = np.arange(n) / SR
    idx = (1.5 * bright * vel + 0.2) * np.exp(-t / 0.4) + 0.2
    body = fm(f, n, 1.0, idx)
    tine = np.sin(2 * np.pi * f * t + 0.5 * bright * np.exp(-t / 0.015) * np.sin(2 * np.pi * 14 * f * t))
    tine = (tine - np.sin(2 * np.pi * f * t)) * 0.6
    env = perc(n, 0.002, tau) * np.where(t < dur, 1.0, np.exp(-(t - dur) / (release / 4)))
    return (body + tine) * env * vel * 0.6


@instrument
def celesta(midi, dur, vel, seed, decay=None, bright=1.0):
    """Music box / celesta: FM with a bright metallic attack; rings past the note length."""
    f = float(midi_hz(midi))
    tau = decay if decay else float(np.clip(1.1 * (523.0 / f) ** 0.6, 0.25, 1.8))
    n = ns(min(tau * 4.5, 5.0))
    t = np.arange(n) / SR
    s = fm(f, n, 1.0, (1.0 * bright * vel + 0.15) * np.exp(-t / 0.05) + 0.12) * perc(n, 0.001, tau)
    s += 0.22 * bright * sine(4.0 * f, n) * perc(n, 0.001, tau * 0.12)
    s += 0.07 * bright * sine(6.79 * f, n) * perc(n, 0.0005, tau * 0.05)
    return s * vel * 0.55


@instrument
def bell(midi, dur, vel, seed, ratio=3.5, index=2.0, decay=1.5):
    f = float(midi_hz(midi))
    n = ns(decay * 4)
    t = np.arange(n) / SR
    s = fm(f, n, ratio, index * vel * np.exp(-t / (decay * 0.15)) + 0.2) * perc(n, 0.001, decay)
    return s * vel * 0.45


@instrument
def marimba(midi, dur, vel, seed, decay=None, bright=1.0):
    """Marimba-like mallet: fundamental + tuned 4th partial + FM woody attack."""
    f = float(midi_hz(midi))
    tau = decay if decay else float(np.clip(0.5 * (220.0 / f) ** 0.6, 0.1, 0.8))
    n = ns(tau * 5.5)
    t = np.arange(n) / SR
    rng = _rng('mar', midi, seed)
    body = fm(f, n, 1.0, 1.4 * bright * vel * np.exp(-t / 0.01)) * perc(n, 0.0012, tau)
    body += 0.32 * sine(3.93 * f, n) * perc(n, 0.0008, tau * 0.22)
    body += 0.10 * bright * sine(9.2 * f, n) * perc(n, 0.0005, tau * 0.06)
    click = filt(white(ns(0.02), rng), lp(2500.0, 0.7)) * perc(ns(0.02), 0.0003, 0.003) * 0.15
    body[:len(click)] += click
    return body * vel * 0.6


@instrument
def pluck(midi, dur, vel, seed, t60=1.2, bright=0.6, release=0.1, ring=False):
    """Karplus-Strong plucked string (harp, kalimba-ish organic plucks)."""
    f = float(midi_hz(midi))
    length = t60 if ring else min(t60, dur + release)
    rng = _rng('pluck', midi, seed)
    s = ks_pluck(f, length + 0.02, rng, t60=t60, bright=float(np.clip(bright * (0.6 + 0.5 * vel), 0, 1)))
    n = len(s)
    t = np.arange(n) / SR
    if not ring:
        s *= np.where(t < dur, 1.0, np.exp(-(t - dur) / (release / 4)))
    s *= np.clip((length + 0.02 - t) / 0.02, 0, 1)
    return s * vel * 0.5


# --------------------------------------------------------------------------- bass

@instrument
def bass_sub(midi, dur, vel, seed, attack=0.01, release=0.12, drive=1.2):
    f = float(midi_hz(midi))
    n = ns(dur + release)
    s = sine(f, n) + 0.22 * sine(2 * f, n) + 0.07 * sine(3 * f, n)
    env = adsr(n, attack, 0.3, 0.85, release, dur)
    return soft_clip(s * env * 0.6, drive) * vel


@instrument
def bass_pluck(midi, dur, vel, seed, peak=7.0, floor=1.6, tau=0.12, q=1.1, release=0.06,
               sub=0.55, drive=1.3, shape='saw', detune=0.0):
    """Analog-style bass with a filter envelope; `detune` adds a second oscillator (cents)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    cut = np.minimum(f * (floor + (peak - floor) * vel * np.exp(-t / tau)), 9000.0)
    s = morph_osc(f, n, cut, shape, q=q, poles=4, k_tables=10)
    if detune:
        s = 0.6 * s + 0.6 * morph_osc(f * float(cents(detune)), n, cut, 'saw', q=q, poles=4,
                                      k_tables=10, phase0=0.37)
    s = s * 0.6 + sub * sine(f, n)
    env = adsr(n, 0.003, 0.25, 0.8, release, dur)
    return soft_clip(s * env * 0.7, drive) * vel


# --------------------------------------------------------------------------- leads

_FLUTE = [1.0, 0.1, 0.24, 0.035, 0.06, 0.012, 0.015, 0.005]


@instrument
def lead_flute(midi, dur, vel, seed, attack=0.05, release=0.22, vib=13.0, breath=0.07):
    """Soft ocarina/flute lead with delayed vibrato and breath noise."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    rng = _rng('flute', midi, seed)
    vib_c = vibrato(n, 5.0, vib, delay=0.18, fade=0.4, phase0=rng.random())
    s = harmonic_osc(_FLUTE, f * vib_c, n)
    br = filt(white(n, rng), bp(min(f * 2.0, 8000), 1.3)) * breath
    env = adsr(n, attack, 0.25, 0.85, release, dur)
    return (s + br) * env * vel * 0.55


@instrument
def lead_brass(midi, dur, vel, seed, attack=0.03, release=0.16, voices=3, detune=7.0,
               bright=4.5, max_cut=5000.0, vib=10.0, drive=1.5, width=0.4):
    """Brassy detuned-saw lead with swell filter, scoop and vibrato (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('brass', midi, seed)
    scoop = cents(-30.0 * np.exp(-t / 0.035))
    vib_c = vibrato(n, 5.4, vib, delay=0.22, fade=0.35, phase0=rng.random())
    rise = 1.0 - np.exp(-t / 0.045)
    cut = np.minimum(f * (1.3 + (bright * (0.7 + 0.4 * vel) - 1.3) * rise * (0.8 + 0.2 * np.exp(-t / 0.3))),
                     max_cut)
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        sigs.append(morph_osc(f * float(cents(x * detune)) * scoop * vib_c, n, cut, 'saw', q=0.9,
                              poles=4, k_tables=8, phase0=rng.random()))
    env = adsr(n, attack, 0.3, 0.85, release, dur)
    st = _spread(sigs, width) * (0.8 / np.sqrt(voices)) * env
    return soft_clip(st, drive) * vel * 0.8


@instrument
def lead_glass(midi, dur, vel, seed, release=0.3, vib=8.0):
    """Airy FM lead: sine-ish body with a glassy FM shimmer (B-section soaring lead)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('glass', midi, seed)
    vib_c = vibrato(n, 5.2, vib, delay=0.25, fade=0.4, phase0=rng.random())
    idx = 0.9 * np.exp(-t / 0.15) + 0.35
    s = fm(f * vib_c, n, 2.0, idx) * 0.7 + 0.5 * sine(f * vib_c, n)
    s += 0.12 * sine(2.0 * f * vib_c, n)
    env = adsr(n, 0.04, 0.3, 0.8, release, dur)
    return s * env * vel * 0.5
