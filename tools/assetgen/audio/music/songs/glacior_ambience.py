"""glacior_ambience - the frozen sanctuary, 60.0 s seamless loop.

Layers: cold gusting wind (decorrelated noise beds through a slowly wandering band-pass), two
eerie howling whistles, blown snow hiss, the stressed creaking of glacier ice, sharp ice cracks
in a vast cavern, falling crystal tinkles, a distant calving boom, the faint dead hum of frozen
ancient machinery and a ghostly aurora shimmer. Every modulation completes whole cycles per loop
and every event wraps around the loop end, so the file is periodic.
"""
import numpy as np

from dsp import filt, lp, hp, svf
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import periodic_lfo, loop_noise, ice_creak, ice_crack, tinkle
from synth import SR, ns, brown, pink

NAME = 'glacior_ambience'
LOOP_S = 60.0
BPM, BEATS_PER_BAR, BARS, LOOP = 60, 1, 60, True   # 60 one-second 'bars'


def _whole(f):
    """Frequency rounded so it completes an integer number of cycles per loop."""
    return round(f * LOOP_S) / LOOP_S


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-24.0, ceiling_db=-3.0)
    s.reverb.update(seconds=6.0, t60=4.5, predelay=0.06, ret=0.5, hf=0.5, lf=0.9)
    s.delay.update(beats=0.61, feedback=0.35, damp=3500.0, ret=0.2, to_reverb=0.5)
    n = s.L
    t = np.arange(n) / SR
    rng = s.rng('events')

    wind = s.track('wind', gain_db=6.0, reverb=0.12)
    howl = s.track('howl', gain_db=2.0, reverb=0.4)
    snow = s.track('snow', gain_db=-16.0, reverb=0.15)
    creak = s.track('creak', gain_db=0.0, reverb=0.6, highpass=120)
    crack = s.track('crack', gain_db=-14.0, reverb=0.9, delay=0.3)
    crystals = s.track('crystals', gain_db=-14.0, reverb=0.8, delay=0.3, highpass=1500)
    boom = s.track('boom', gain_db=-16.0, reverb=0.7, lowpass=600, highpass=38)
    machine = s.track('machine', gain_db=-26.0, reverb=0.4)
    aurora = s.track('aurora', gain_db=-32.0, reverb=0.6)

    # cold wind: two decorrelated beds, gusts with periodic envelopes, wandering band
    for ch, ph in ((0, 0.0), (1, 0.31)):
        nz = brown(n, s.rng(f'wind{ch}')) * 0.5 + pink(n, s.rng(f'windp{ch}')) * 0.5
        band = 750.0 * np.exp(0.55 * periodic_lfo(n, 4, ph) + 0.3 * periodic_lfo(n, 9, ph * 2))
        w = svf(nz, band, 0.8, 'bp', circular=True)
        w = filt(w, hp(110.0, 0.7), circular=True)
        gust = 0.5 + 0.28 * periodic_lfo(n, 3, ph) + 0.15 * periodic_lfo(n, 7, ph + 0.2) \
            + 0.07 * periodic_lfo(n, 13, ph)
        st = np.zeros((2, n))
        st[ch] = w * gust
        st[1 - ch] = 0.35 * np.roll(w * gust, ns(0.017))
        wind.at(st, 1, 0)

    # howling whistles: narrow resonant bands sliding slowly (vortices over ice ridges)
    for k, (c0, cyc, pn) in enumerate(((620.0, 2, -0.5), (940.0, 3, 0.55))):
        src = pink(n, s.rng(f'howl{k}'))
        fc = c0 * np.exp(0.35 * periodic_lfo(n, cyc, 0.15 * k) + 0.08 * periodic_lfo(n, 11, k * 0.3))
        h = svf(src, fc, 14.0, 'bp', circular=True)
        env = np.clip(periodic_lfo(n, cyc, 0.25 + 0.4 * k), 0.0, 1.0) ** 2
        howl.at(h * env, 1, 0, pan=pn)

    # blown snow: fine high hiss following the gusts
    for ch, ph in ((0, 0.1), (1, 0.6)):
        hs = loop_noise(n, s.rng(f'snow{ch}'), 'white', hp(4500.0, 0.7), lp(12000.0, 0.7))
        g = np.clip(0.45 + 0.4 * periodic_lfo(n, 3, ph) + 0.15 * periodic_lfo(n, 7, ph), 0, 1) ** 2
        st = np.zeros((2, n))
        st[ch] = hs * g
        snow.at(st, 1, 0)

    # creaking ice (glacier under stress)
    for pos, pn, secs in ((5.0, -0.4, 2.2), (17.5, 0.5, 1.6), (31.0, -0.2, 2.8), (44.5, 0.35, 1.9),
                          (55.0, -0.6, 1.4)):
        c = ice_creak(s.rng(f'creak{pos}'), seconds=secs, f_lo=22.0, f_hi=60.0, res=(380.0, 1050.0))
        creak.add(c * 3.0, ns(pos), pan=pn)

    # sharp cracks in the cavern
    for pos, pn, sz in ((11.0, 0.45, 1.0), (26.5, -0.5, 1.4), (49.0, 0.15, 0.8)):
        crack.add(ice_crack(s.rng(f'crack{pos}'), sz), ns(pos), pan=pn)

    # falling crystals
    for i in range(9):
        pos = rng.uniform(0, LOOP_S)
        crystals.add(tinkle(rng, count=int(rng.integers(3, 7)), spread=rng.uniform(0.3, 0.8)),
                     ns(pos), pan=rng.uniform(-0.8, 0.8))

    # distant calving boom
    for pos, pn in ((36.0, -0.3),):
        boom.add(P.sub_hit(50.0, length=3.0, variant=77) * 1.2, ns(pos), pan=pn)
        boom.add(filt(brown(ns(4.0), s.rng('calve')), lp(300.0, 0.7))
                 * np.exp(-np.arange(ns(4.0)) / SR / 0.9) * 0.6, ns(pos + 0.08), pan=pn)

    # the dead hum of frozen machinery (integer cycles per loop)
    f0 = _whole(55.0)
    hum = (np.sin(2 * np.pi * f0 * t) + 0.6 * np.sin(2 * np.pi * _whole(55.1) * t)
           + 0.3 * np.sin(2 * np.pi * _whole(165.2) * t))
    machine.at(hum * (0.7 + 0.3 * periodic_lfo(n, 4)), 1, 0)

    # aurora shimmer: high sine cluster (E major add9) breathing slowly
    for k, (f, cyc) in enumerate(((1318.5, 2), (1661.2, 3), (1975.5, 2), (2489.0, 5))):
        tone = np.sin(2 * np.pi * _whole(f) * t + k) + 0.5 * np.sin(2 * np.pi * _whole(f * 1.003) * t)
        env = np.clip(periodic_lfo(n, cyc, 0.13 * k), 0.0, 1.0) ** 2
        aurora.at(tone * env, 1, 0, pan=-0.6 + 0.4 * k)

    # a few soft glass bells far away
    for pos, nt in ((8.0, 'E6'), (22.0, 'B5'), (39.5, 'G#6'), (51.5, 'C#6')):
        crystals.note(T.ice_bell, 1, pos, 1, nt, vel=0.35, decay=3.0, pan=rng.uniform(-0.7, 0.7))
    return s
