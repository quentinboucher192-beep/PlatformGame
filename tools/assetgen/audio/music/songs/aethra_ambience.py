"""aethra_ambience - the sanctuary islands floating on the nebula ocean, 60.0 s seamless loop.

Layers: the slow surf of the nebula ocean far below (decorrelated noise beds through a wandering
low band-pass, six swells per loop), a high starry air, a warm drone of the sanctuaries (D - A - E,
integer cycles per loop, breathing), soft golden bells far away, falling sparkles of starlight,
and one distant sign of each element: a root creaking, crystals tinkling, a lava blub, a void
whoosh.  Every modulation completes whole cycles per loop and every event wraps around the loop
end, so the file is periodic.
"""
import numpy as np

from dsp import filt, hp, lp, svf
from music import instruments as I
from music import timbres as T
from music.sequencer import Song
from music.textures import periodic_lfo, loop_noise, ice_creak, lava_blub, tinkle
from synth import SR, ns, brown, pink

NAME = 'aethra_ambience'
LOOP_S = 60.0
BPM, BEATS_PER_BAR, BARS, LOOP = 60, 1, 60, True   # 60 one-second 'bars'


def _whole(f):
    """Frequency rounded so it completes an integer number of cycles per loop."""
    return round(f * LOOP_S) / LOOP_S


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-24.0, ceiling_db=-3.0)
    s.reverb.update(seconds=6.0, t60=4.8, predelay=0.06, ret=0.5, hf=0.5, lf=0.9)
    s.delay.update(beats=0.73, feedback=0.4, damp=3500.0, ret=0.22, to_reverb=0.5)
    n = s.L
    t = np.arange(n) / SR
    rng = s.rng('events')

    ocean = s.track('ocean', gain_db=4.0, reverb=0.15)
    air = s.track('air', gain_db=-14.0, reverb=0.2)
    drone = s.track('drone', gain_db=-25.0, reverb=0.5)
    bells = s.track('bells', gain_db=-10.0, reverb=0.8, delay=0.35, highpass=500)
    sparkle = s.track('sparkle', gain_db=-14.0, reverb=0.8, delay=0.3, highpass=1800)
    elems = s.track('elements', gain_db=-6.0, reverb=0.7, highpass=80)

    # the nebula ocean: slow surf, six swells per loop, wandering low band
    for ch, ph in ((0, 0.0), (1, 0.37)):
        nz = brown(n, s.rng(f'ocean{ch}')) * 0.6 + pink(n, s.rng(f'oceanp{ch}')) * 0.4
        band = 380.0 * np.exp(0.5 * periodic_lfo(n, 6, ph) + 0.25 * periodic_lfo(n, 11, ph * 2))
        w = svf(nz, band, 0.7, 'bp', circular=True)
        w = filt(w, hp(60.0, 0.7), circular=True)
        swell = 0.45 + 0.35 * np.clip(periodic_lfo(n, 6, ph), 0, 1) ** 1.5 + 0.12 * periodic_lfo(n, 13, ph)
        st = np.zeros((2, n))
        st[ch] = w * swell
        st[1 - ch] = 0.4 * np.roll(w * swell, ns(0.021))
        ocean.at(st, 1, 0)

    # high starry air
    for ch, ph in ((0, 0.2), (1, 0.7)):
        hs = loop_noise(n, s.rng(f'air{ch}'), 'white', hp(5000.0, 0.7), lp(12000.0, 0.7))
        g = np.clip(0.4 + 0.35 * periodic_lfo(n, 3, ph) + 0.15 * periodic_lfo(n, 8, ph), 0, 1) ** 2
        st = np.zeros((2, n))
        st[ch] = hs * g
        air.at(st, 1, 0)

    # warm drone of the sanctuaries (D2 - A2 - E3 - F#3), breathing slowly
    for k, (f, cyc) in enumerate(((73.42, 2), (110.0, 3), (164.81, 2), (185.0, 5), (220.0, 4))):
        tone = np.sin(2 * np.pi * _whole(f) * t + k) + 0.5 * np.sin(2 * np.pi * _whole(f * 1.002) * t)
        env = 0.4 + 0.6 * np.clip(periodic_lfo(n, cyc, 0.17 * k), 0.0, 1.0) ** 1.5
        drone.at(tone * env, 1, 0, pan=-0.5 + 0.25 * k)

    # soft golden bells far away (a slow D lydian phrase)
    for pos, nt in ((4.0, 'A5'), (11.5, 'F#5'), (19.0, 'G#5'), (27.5, 'E6'), (36.0, 'D6'), (44.5, 'C#6'),
                    (52.0, 'B5')):
        bells.note(I.bell, 1, pos, 1, nt, vel=0.35, ratio=3.5, index=1.4, decay=2.4, pan=rng.uniform(-0.7, 0.7))
    # falling starlight
    for i in range(10):
        pos = rng.uniform(0, LOOP_S)
        sparkle.add(tinkle(rng, count=int(rng.integers(3, 6)), spread=rng.uniform(0.4, 0.9)), ns(pos),
                    pan=rng.uniform(-0.8, 0.8))
    # one distant sign of each element
    elems.add(ice_creak(s.rng('root'), seconds=2.2, f_lo=18.0, f_hi=40.0, res=(260.0, 640.0)) * 2.0, ns(8.0),
              pan=-0.5)                                                          # roots creaking
    elems.add(tinkle(s.rng('frost'), count=7, spread=1.0) * 0.8, ns(23.0), pan=0.55)   # frost crystals
    for k in range(3):                                                           # a lava blub
        elems.add(lava_blub(s.rng(f'magma{k}'), size=0.8 + 0.2 * k) * 0.7, ns(37.0 + 0.35 * k), pan=0.3)
    sw = svf(pink(ns(3.0), s.rng('void')), np.exp(np.linspace(np.log(3000.0), np.log(200.0), ns(3.0))), 2.0,
             'bp')
    env = np.sin(np.linspace(0, np.pi, ns(3.0))) ** 2
    elems.add(sw * env * 0.8, ns(49.0), pan=-0.3)                                # the void breathing in
    return s
