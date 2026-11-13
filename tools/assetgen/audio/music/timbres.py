"""Melodic instruments of the v2 planets (same signature and memoisation as music.instruments).

Pyralis (forge): struck anvils, low brass section, stabs, a distorted forge bass.
Glacior (ice): glass bells, crystal plucks, frosty additive pads.
"""
import numpy as np

from dsp import filt, bp, lp, hp, peak, soft_clip
from music.instruments import instrument, _rng, _spread
from synth import (SR, ns, midi_hz, adsr, perc, sine, fm, white, morph_osc, harmonic_osc, cents,
                   vibrato)

# free-free bar modes (anvil / struck metal): (frequency ratio, amplitude, decay multiplier)
_BAR = ((1.0, 1.0, 1.0), (2.756, 0.62, 0.55), (5.404, 0.4, 0.3), (8.933, 0.24, 0.17),
        (13.34, 0.12, 0.1))


# --------------------------------------------------------------------------- Pyralis

@instrument
def anvil(midi, dur, vel, seed, decay=1.0, bright=1.0, click=0.6, width=0.5, thud=0.3):
    """Pitched struck anvil: inharmonic bar partials, each a beating pair spread in stereo,
    a hammer 'tchk' and a little low mass. Rings past the note length (stereo)."""
    f = float(midi_hz(midi))
    n = ns(min(decay * 4.5, 6.0))
    t = np.arange(n) / SR
    rng = _rng('anvil', midi, seed)
    L = np.zeros(n)
    R = np.zeros(n)
    for i, (r, a, dm) in enumerate(_BAR):
        fr = f * r
        if fr > 15000.0:
            break
        a *= bright ** i
        env = perc(n, 0.0004, decay * dm)
        beat = 1.0 + 0.0009 * (i + 1)
        p1, p2 = rng.random(), rng.random()
        L += a * np.sin(2 * np.pi * (fr * t + p1)) * env
        R += a * np.sin(2 * np.pi * (fr * beat * t + p2)) * env
    strike = filt(white(n, rng), bp(min(f * 3.5, 9000.0), 0.9)) * perc(n, 0.0002, 0.003) * click
    low = sine(f * 0.25 if f > 400 else f * 0.5, n) * perc(n, 0.001, 0.04) * thud
    mid = 0.5 * (L + R)
    L = mid + width * (L - mid) + strike + low
    R = mid + width * (R - mid) + strike + low
    return np.vstack([L, R]) * vel * 0.32


@instrument
def brass_low(midi, dur, vel, seed, attack=0.07, release=0.3, voices=4, detune=9.0, bright=3.0,
              max_cut=2400.0, drive=2.0, width=0.6, growl=0.0, vib=6.0):
    """Low brass section (horns / trombones): detuned saws with a swelling filter, horn
    formants and saturation. `growl` adds flutter-tongue roughness (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('brlow', midi, seed)
    rise = 1.0 - np.exp(-t / max(attack * 1.2, 0.01))
    cut = np.minimum(f * (1.1 + (bright * (0.65 + 0.45 * vel) - 1.1) * rise), max_cut)
    vib_c = vibrato(n, 5.0, vib, delay=0.3, fade=0.4, phase0=rng.random())
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        sigs.append(morph_osc(f * float(cents(x * detune)) * vib_c, n, cut, 'saw', q=0.85, poles=4,
                              k_tables=8, phase0=rng.random()))
    st = _spread(sigs, width) * (0.85 / np.sqrt(voices))
    if growl:
        st = st * (1.0 - growl * 0.5 * (1.0 + np.sin(2 * np.pi * 31.0 * t)))
    st = filt(st, peak(480.0, 1.1, 5.0), peak(1150.0, 1.6, 3.0), hp(45.0, 0.7))
    env = adsr(n, attack, 0.4, 0.85, release, dur)
    return soft_clip(st * env, drive) * vel * 0.75


@instrument
def brass_stab(midi, dur, vel, seed, decay=0.16, bright=4.0, max_cut=3800.0, drive=2.4, voices=3,
               detune=10.0, width=0.7):
    """Short accented brass hit with a fall-off (stereo)."""
    f = float(midi_hz(midi))
    gate = min(dur, 0.5)
    n = ns(gate + 0.15)
    t = np.arange(n) / SR
    rng = _rng('brstab', midi, seed)
    fall = cents(-60.0 * np.clip((t - gate * 0.6) / 0.15, 0.0, 1.0) ** 2)
    cut = np.minimum(f * (1.4 + bright * vel * np.exp(-t / 0.09)), max_cut)
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        sigs.append(morph_osc(f * float(cents(x * detune)) * fall, n, cut, 'saw', q=0.9, poles=4,
                              k_tables=8, phase0=rng.random()))
    st = _spread(sigs, width) * (0.9 / np.sqrt(voices))
    st = filt(st, peak(560.0, 1.2, 4.0), hp(60.0, 0.7))
    env = perc(n, 0.006, decay) * np.clip((gate + 0.12 - t) / 0.12, 0.0, 1.0)
    return soft_clip(st * env, drive) * vel * 0.8


@instrument
def bass_forge(midi, dur, vel, seed, peak_mul=6.0, floor=1.4, tau=0.08, q=1.3, release=0.05, sub=0.7,
               drive=3.0, detune=11.0, top=3800.0):
    """Saturated saw+square bass with a filter thump (palm-muted forge chug)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    cut = np.minimum(f * (floor + (peak_mul - floor) * vel * np.exp(-t / tau)), 7000.0)
    s = 0.55 * morph_osc(f, n, cut, 'saw', q=q, poles=4, k_tables=10)
    s += 0.45 * morph_osc(f * float(cents(detune)), n, cut, 'square', q=q, poles=4, k_tables=10,
                          phase0=0.31)
    s = s * 0.65 + sub * sine(f, n)
    env = adsr(n, 0.002, 0.2, 0.85, release, dur)
    y = soft_clip(s * env * 0.8, drive)
    return filt(y, lp(top, 0.7))[:n] * vel


# --------------------------------------------------------------------------- Glacior

@instrument
def ice_bell(midi, dur, vel, seed, decay=2.0, bright=1.0, width=0.7, index=1.4):
    """Glass bell: FM body + inharmonic glass partials, slow stereo beating; long ring."""
    f = float(midi_hz(midi))
    n = ns(min(decay * 3.5, 7.0))
    t = np.arange(n) / SR
    rng = _rng('icebell', midi, seed)
    body = fm(f, n, 1.0, index * vel * np.exp(-t / 0.06) + 0.18) * perc(n, 0.0008, decay)
    parts = ((2.0, 0.3, 0.7), (2.76, 0.32, 0.45), (5.40, 0.2, 0.22), (8.93, 0.1, 0.12))
    L = body.copy()
    R = body.copy()
    for i, (r, a, dm) in enumerate(parts):
        fr = f * r
        if fr > 15000.0:
            break
        a *= bright ** (i // 2)
        env = perc(n, 0.0006, decay * dm)
        L += a * np.sin(2 * np.pi * (fr * t + rng.random())) * env
        R += a * np.sin(2 * np.pi * (fr * 1.0012 * t + rng.random())) * env
    mid = 0.5 * (L + R)
    L = mid + width * (L - mid)
    R = mid + width * (R - mid)
    return np.vstack([L, R]) * vel * 0.38


@instrument
def glass_pluck(midi, dur, vel, seed, decay=0.45, ratio=3.0, index=2.0, bright=1.0):
    """Crystal pluck: bright FM attack settling on a sine, glassy top partial (mono)."""
    f = float(midi_hz(midi))
    n = ns(min(decay * 5.0, 4.0))
    t = np.arange(n) / SR
    s = fm(f, n, ratio, index * (0.5 + 0.5 * vel) * np.exp(-t / 0.025) + 0.08) * perc(n, 0.0008, decay)
    s += 0.55 * sine(f, n) * perc(n, 0.001, decay * 1.5)
    s += 0.12 * bright * sine(f * 6.79, n) * perc(n, 0.0004, decay * 0.07)
    return s * vel * 0.45


def _soft_amps(nh, tilt=1.6, odd=1.0):
    k = np.arange(1, nh + 1, dtype=float)
    a = 1.0 / k ** tilt
    a[1::2] *= odd
    return a


@instrument
def pad_frost(midi, dur, vel, seed, attack=1.4, release=2.4, voices=4, detune=7.0, width=0.9,
              air=0.06, shimmer=0.22, tilt=1.6):
    """Airy additive pad: soft harmonic voices, a slowly breathing octave shimmer and a
    breath of filtered noise (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('frost', midi, seed)
    amps = _soft_amps(14, tilt, 0.8)
    sigs = []
    for v in range(voices):
        x = (2.0 * v / (voices - 1) - 1.0) if voices > 1 else 0.0
        drift = 1.0 + 0.001 * np.sin(2 * np.pi * (0.11 + 0.05 * v) * t + rng.random() * 6.283)
        sigs.append(harmonic_osc(amps, f * float(cents(x * detune)) * drift, n, rng.random()))
    st = _spread(sigs, width) * (0.6 / np.sqrt(voices))
    sh_am = 0.5 + 0.5 * np.sin(2 * np.pi * (0.23 * t + rng.random()))
    sh = sine(f * 2.0, n, rng.random()) * sh_am + 0.5 * sine(f * 3.0, n, rng.random()) * (1.0 - sh_am)
    st = st + np.vstack([sh, np.roll(sh, ns(0.011))]) * shimmer * 0.3
    if air:
        nz = filt(white(n, rng), bp(min(f * 8.0, 9000.0), 0.8))[:n]
        st = st + np.vstack([nz, np.roll(nz, ns(0.017))]) * air
    env = adsr(n, attack, 1.0, 0.85, release, dur)
    return st * env * vel
