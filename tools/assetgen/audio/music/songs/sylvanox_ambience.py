"""sylvanox_ambience - quiet night alien-forest bed, 40.0 s seamless loop.

Layers: gusting wind (two decorrelated filtered-noise beds with periodic gust envelopes),
a soft ancient-tech hum, pulsing alien insects, distant FM bird calls, water drips in a cave-ish
space, a far low creature call and rare glowing-pod chimes. Every modulation completes a whole
number of cycles per loop and every event wraps around the loop end, so the file is periodic.
"""
import numpy as np

from dsp import filt, lp, hp, svf
from music import instruments as I
from music.sequencer import Song
from synth import SR, ns, pink, brown, sine, fm, saw, perc, points

NAME = 'sylvanox_ambience'
LOOP_S = 40.0
BPM, BEATS_PER_BAR, BARS, LOOP = 60, 1, 40, True   # 40 one-second 'bars'


def _periodic_lfo(n, cycles, phase=0.0):
    return np.sin(2 * np.pi * (cycles * np.arange(n) / n + phase))


def _bird_call(rng):
    """3-6 quick FM chirps, rising or falling, like a distant alien bird."""
    k = rng.integers(3, 7)
    base = rng.uniform(1800, 3200)
    up = rng.random() < 0.6
    parts = []
    t0 = 0.0
    for i in range(k):
        d = rng.uniform(0.045, 0.09)
        n = ns(d)
        f0, f1 = (base, base * 1.45) if up else (base * 1.4, base)
        f = f0 + (f1 - f0) * np.linspace(0, 1, n) ** 0.7
        f *= 1.0 + 0.04 * np.sin(2 * np.pi * 38 * np.arange(n) / SR)
        c = fm(f, n, 0.5, 0.6) * perc(n, 0.006, d * 0.4) * (1.0 - 0.1 * i)
        parts.append((t0, c))
        t0 += d + rng.uniform(0.03, 0.08)
    out = np.zeros(ns(t0 + 0.2))
    for t, c in parts:
        s = ns(t)
        out[s:s + len(c)] += c
    return filt(out, lp(6000, 0.7))


def _drip(rng):
    d = 0.18
    n = ns(d)
    f0 = rng.uniform(700, 1200)
    f = f0 * (1.0 + 1.2 * (1 - np.exp(-np.arange(n) / SR / 0.012)))
    return sine(f, n) * perc(n, 0.001, 0.035)


def _insects(n, rng, carrier, pulse_cycles, chirp_cycles):
    """Pulsing cricket-like texture: carrier AM'd by fast trills grouped into pulses."""
    t = np.arange(n) / SR
    trill = 0.5 + 0.5 * np.sin(2 * np.pi * (round(32 * LOOP_S) / LOOP_S) * t)
    pulse = np.clip(_periodic_lfo(n, pulse_cycles), 0, 1) ** 3
    groups = 0.5 + 0.5 * _periodic_lfo(n, chirp_cycles, rng.random())
    return sine(carrier, n, rng.random()) * trill * pulse * groups


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-24.0, ceiling_db=-3.0)
    s.reverb.update(seconds=4.5, t60=3.4, predelay=0.04, ret=0.5, hf=0.3)
    s.delay.update(beats=0.55, feedback=0.3, damp=2500.0, ret=0.2, to_reverb=0.5)
    n = s.L
    rng = s.rng('events')

    wind = s.track('wind', gain_db=7.0, reverb=0.1)
    hum = s.track('hum', gain_db=-15.0, reverb=0.3)
    bugs = s.track('insects', gain_db=-20.0, reverb=0.4)
    birds = s.track('birds', gain_db=-17.0, reverb=0.8, delay=0.15, highpass=900)
    drips = s.track('drips', gain_db=-14.0, reverb=0.9, delay=0.2)
    calls = s.track('calls', gain_db=-26.0, reverb=0.9)
    pods = s.track('pods', gain_db=-12.0, reverb=0.9, delay=0.3)

    # wind: two decorrelated beds, gusts with periodic envelopes, slowly moving band
    for ch, ph in ((0, 0.0), (1, 0.37)):
        nz = brown(n, s.rng(f'wind{ch}')) * 0.6 + pink(n, s.rng(f'windp{ch}')) * 0.4
        band = 520 * np.exp(0.6 * _periodic_lfo(n, 3, ph) + 0.3 * _periodic_lfo(n, 7, ph * 2))
        w = svf(nz, band, 0.9, 'bp', circular=True)
        w = filt(w, hp(90, 0.7), circular=True)
        gust = 0.55 + 0.25 * _periodic_lfo(n, 2, ph) + 0.15 * _periodic_lfo(n, 5, ph + 0.2) \
            + 0.05 * _periodic_lfo(n, 11, ph)
        st = np.zeros((2, n))
        st[ch] = w * gust
        st[1 - ch] = 0.35 * np.roll(w * gust, ns(0.013))
        wind.at(st, 1, 0)

    # ancient-tech hum: beating low partials (integer cycles over 40 s -> periodic)
    t = np.arange(n) / SR
    drone = (np.sin(2 * np.pi * 55.0 * t) + 0.8 * np.sin(2 * np.pi * 55.1 * t)
             + 0.35 * np.sin(2 * np.pi * 110.05 * t) + 0.12 * np.sin(2 * np.pi * 165.2 * t))
    buzz = filt(saw(220.0, n), lp(500, 0.9), circular=True) * 0.08
    throb = 0.7 + 0.3 * _periodic_lfo(n, 10)
    hum.at((drone * 0.25 + buzz) * throb, 1, 0)

    # insects: two pulsing voices, left/right
    bugs.at(_insects(n, rng, 4400.0, 100, 4), 1, 0, pan=-0.6)
    bugs.at(_insects(n, rng, 5150.0, 70, 3), 1, 0, pan=0.55)

    # distant bird calls
    for i in range(11):
        pos = rng.uniform(0, LOOP_S)
        birds.add(_bird_call(rng) * rng.uniform(0.5, 1.0), ns(pos), pan=rng.uniform(-0.8, 0.8))

    # drips: some clustered, some isolated
    for i in range(16):
        pos = rng.uniform(0, LOOP_S)
        drips.add(_drip(rng) * rng.uniform(0.4, 1.0), ns(pos), pan=rng.uniform(-0.6, 0.6))
        if rng.random() < 0.35:
            drips.add(_drip(rng) * 0.4, ns(pos + rng.uniform(0.3, 0.9)), pan=rng.uniform(-0.6, 0.6))

    # far low creature calls (two per loop)
    for pos, f0 in ((7.5, 175.0), (27.0, 150.0)):
        d = 2.6
        m = ns(d)
        tt = np.arange(m) / SR
        f = points(m, [(0, f0 * 0.92), (0.6, f0), (2.0, f0 * 0.97), (d, f0 * 0.8)], 'exp')
        f = f * (1.0 + 0.012 * np.sin(2 * np.pi * 4.5 * tt))
        v = fm(f, m, 1.0, 0.7) + 0.4 * sine(f * 2.0, m)
        v = filt(v, lp(900, 0.7)) * points(m, [(0, 0), (0.5, 1.0), (1.8, 0.7), (d, 0)])
        calls.add(v, ns(pos), pan=-0.4 if f0 > 160 else 0.45)

    # glowing pods: rare soft chimes from the D major pentatonic
    for pos, nt in ((3.0, 'A5'), (11.5, 'F#6'), (19.0, 'D6'), (24.5, 'E6'), (33.0, 'B5'),
                    (36.5, 'A6')):
        pods.note(I.bell, 1, pos, 1, nt, vel=0.5, ratio=3.5, index=1.2, decay=1.6,
                  pan=rng.uniform(-0.7, 0.7))
    return s
