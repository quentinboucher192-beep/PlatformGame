"""pyralis_ambience - the molten forge world, 60.0 s seamless loop.

Layers: deep magma rumble (decorrelated brown-noise beds with slow periodic surges), a gurgling
lava-flow band, thick lava bubbles, ember crackle, the breathing of giant bellows over a low
forge hum, distant smiths hammering on anvils in a big stone hall, steam vents, chains and a
rare metallic groan of the hot superstructure. Every modulation completes whole cycles per
loop and every event wraps around the loop end, so the file is periodic.
"""
import numpy as np

from dsp import filt, lp, hp, bp
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import periodic_lfo, loop_noise, lava_blub, fire_crackle, ice_creak
from synth import SR, ns, saw

NAME = 'pyralis_ambience'
LOOP_S = 60.0
BPM, BEATS_PER_BAR, BARS, LOOP = 60, 1, 60, True   # 60 one-second 'bars'

# distant smiths: (start s, hit times relative, anvil note, pan)
SMITHS = [
    (4.0, (0.0, 0.62, 1.24, 1.86, 3.1, 3.72), 'B5', -0.55),
    (19.5, (0.0, 0.55, 1.1, 2.2, 2.75), 'F5', 0.6),
    (33.0, (0.0, 0.7, 1.4, 2.1, 2.8, 3.5, 4.9), 'C6', -0.35),
    (47.5, (0.0, 0.6, 1.2, 2.4, 3.0), 'G5', 0.45),
]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-24.0, ceiling_db=-3.0)
    s.reverb.update(seconds=5.0, t60=3.6, predelay=0.05, ret=0.5, hf=0.28, lf=1.3)
    s.delay.update(beats=0.43, feedback=0.25, damp=1800.0, ret=0.15, to_reverb=0.5)
    n = s.L
    t = np.arange(n) / SR
    rng = s.rng('events')

    rumble = s.track('rumble', gain_db=7.0, reverb=0.05)
    flow = s.track('flow', gain_db=-6.0, reverb=0.2)
    bellows = s.track('bellows', gain_db=-9.0, reverb=0.3)
    hum = s.track('hum', gain_db=-16.0, reverb=0.25)
    bubbles = s.track('bubbles', gain_db=-12.0, reverb=0.35)
    embers = s.track('embers', gain_db=-15.0, reverb=0.3, highpass=1500)
    sizzle = s.track('sizzle', gain_db=-26.0, reverb=0.2)
    smiths = s.track('smiths', gain_db=-15.0, reverb=0.9, delay=0.25, lowpass=3500, highpass=80)
    steam = s.track('steam', gain_db=-23.0, reverb=0.5)
    metal = s.track('metal', gain_db=-21.0, reverb=0.8, highpass=120)

    # magma rumble: two decorrelated beds with surges (3 and 7 cycles per loop)
    for ch, ph in ((0, 0.0), (1, 0.41)):
        bed = loop_noise(n, s.rng(f'rumble{ch}'), 'brown', lp(170.0, 0.7), hp(30.0, 0.7))
        surge = 0.6 + 0.25 * periodic_lfo(n, 3, ph) + 0.15 * periodic_lfo(n, 7, ph + 0.3)
        st = np.zeros((2, n))
        st[ch] = bed * surge
        st[1 - ch] = 0.45 * np.roll(bed * surge, ns(0.021))
        rumble.at(st, 1, 0)

    # lava flow: gurgling mid band, amplitude driven by a smoothed (periodic) random envelope
    for ch, c in ((0, 330.0), (1, 410.0)):
        g = loop_noise(n, s.rng(f'flow{ch}'), 'pink', bp(c, 1.2), lp(900.0, 0.7))
        am = loop_noise(n, s.rng(f'flowam{ch}'), 'white', lp(6.0, 0.7))
        am = np.clip(0.5 + am / (np.std(am) * 3.0), 0.05, 1.2)
        st = np.zeros((2, n))
        st[ch] = g * am
        st[1 - ch] = 0.3 * g * am
        flow.at(st, 1, 0)

    # giant bellows: slow breathing noise (6 breaths per loop) over a low forge hum
    breath = np.clip(periodic_lfo(n, 6, 0.75), 0.0, 1.0) ** 1.6
    bel = loop_noise(n, s.rng('bellows'), 'pink', bp(700.0, 0.8), lp(1600.0, 0.7))
    bellows.at(np.vstack([bel * breath, np.roll(bel, ns(0.03)) * breath]), 1, 0)
    drone = (np.sin(2 * np.pi * 49.0 * t) + 0.7 * np.sin(2 * np.pi * 49.15 * t)
             + 0.4 * np.sin(2 * np.pi * 98.1 * t) + 0.25 * np.sin(2 * np.pi * 147.05 * t))
    buzz = filt(saw(98.0, n), lp(420.0, 0.8), circular=True) * 0.1
    throb = 0.75 + 0.25 * periodic_lfo(n, 12)
    hum.at((drone * 0.3 + buzz) * throb, 1, 0)

    # lava bubbles: loose clusters, varied sizes
    for i in range(30):
        pos = rng.uniform(0, LOOP_S)
        size = rng.uniform(0.6, 1.6)
        bubbles.add(lava_blub(rng, size) * rng.uniform(0.4, 1.0), ns(pos), pan=rng.uniform(-0.7, 0.7))
        if rng.random() < 0.4:
            bubbles.add(lava_blub(rng, size * 0.7) * 0.5, ns(pos + rng.uniform(0.15, 0.5)),
                        pan=rng.uniform(-0.7, 0.7))

    # lava sizzle: thin high hiss breathing slowly (5 swells per loop)
    hiss = loop_noise(n, s.rng('sizzle'), 'pink', bp(5500.0, 0.7), hp(2500.0, 0.7))
    sw = 0.55 + 0.45 * periodic_lfo(n, 5, 0.2)
    sizzle.at(np.vstack([hiss * sw, np.roll(hiss, n // 2) * sw]), 1, 0)

    # ember crackle: sparse, in both channels
    for ch in (-0.6, 0.6):
        embers.at(fire_crackle(n, s.rng(f'embers{ch}'), rate=5.0), 1, 0, pan=ch)

    # distant smiths in a stone hall
    hm = P.hammer(variant=7, freq=760.0)
    for start, hits, note, pn in SMITHS:
        for k, h in enumerate(hits):
            v = 0.9 if k % 3 == 0 else 0.7
            smiths.add(hm * v, ns(start + h), pan=pn)
            smiths.note(T.anvil, 1, start + h, 0.3, note, vel=0.45 * v, decay=0.9, click=0.3, pan=pn)

    # steam vents
    for pos, pn in ((12.0, -0.4), (29.5, 0.5), (53.0, -0.1)):
        steam.add(P.steam(2.2, variant=int(pos), center=4200.0), ns(pos), pan=pn)

    # chains swaying and a metallic groan of the hot structure
    for pos, pn in ((8.5, 0.6), (41.0, -0.6)):
        metal.add(P.chain(1.2, 12, variant=int(pos)) * 0.8, ns(pos), pan=pn)
    for pos, pn in ((25.0, 0.3), (56.0, -0.4)):
        g = ice_creak(s.rng(f'groan{pos}'), seconds=3.0, f_lo=14.0, f_hi=26.0, res=(190.0, 520.0))
        metal.add(g * 2.5, ns(pos), pan=pn)
    return s
