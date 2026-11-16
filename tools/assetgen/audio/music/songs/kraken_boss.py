"""kraken_boss - le Kraken des Abysses (giga boss of Les Confins). F minor with a Neapolitan G-flat
and the C major dominant, a slow 6/8 swell: 162 eighth notes per minute (dotted quarter = 54),
6 eighths per bar, 32 bars = 71.11 s.

Character: something enormous rising from black water.  Everything sways like a swell: rolling
low strings, a deep organ with a slow tremolo, sonar pings answered by their own echoes, bubbles,
surges of water, a dark choir, and the kraken's theme passed from the cellos to the organ and the
brass.  Long, dark, watery reverb.

Form:  intro 1-4 (sonar, the organ's pedal, bubbles, a rising surge)
       | A 5-12 (the rolling cellos, taikos on the swell, the theme on the low strings)
       | B 13-20 (the choir, the full organ, the theme up on horns, toms rolling)
       | breakdown 21-24 (the abyss: pings, heartbeat, the organ alone, a long moan)
       | climax 25-32 (the theme on organ, brass and choir over everything, a surge into the loop).
"""
import numpy as np

from dsp import bp, filt, hp, lp
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.drums import _ro, _rng
from music.instruments import instrument
from music.sequencer import Song
from music.textures import blocks, lava_blub
from synth import SR, adsr, harmonic_osc, midi_hz, note_midi, ns, perc, pink, sine

NAME = 'kraken_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 162, 6, 32, True     # 'beats' are eighth notes

# name: (bass root, organ voicing, choir voicing, rolling cello tones)
CHORD = {
    'Fm': ('F1', ['F3', 'Ab3', 'C4', 'F4'], ['C4', 'F4', 'Ab4'], ['F2', 'C3', 'F3', 'Ab3']),
    'Db': ('Db2', ['F3', 'Ab3', 'Db4', 'F4'], ['Db4', 'F4', 'Ab4'], ['Db2', 'Ab2', 'Db3', 'F3']),
    'Eb': ('Eb2', ['G3', 'Bb3', 'Eb4', 'G4'], ['Eb4', 'G4', 'Bb4'], ['Eb2', 'Bb2', 'Eb3', 'G3']),
    'C': ('C2', ['E3', 'G3', 'C4', 'E4'], ['C4', 'E4', 'G4'], ['C2', 'G2', 'C3', 'E3']),
    'Bbm': ('Bb1', ['F3', 'Bb3', 'Db4', 'F4'], ['Db4', 'F4', 'Bb4'], ['Bb1', 'F2', 'Bb2', 'Db3']),
    'Gb': ('Gb1', ['Gb3', 'Bb3', 'Db4', 'Gb4'], ['Db4', 'Gb4', 'Bb4'], ['Gb2', 'Db3', 'Gb3', 'Bb3']),
    'Ab': ('Ab1', ['Ab3', 'C4', 'Eb4', 'Ab4'], ['C4', 'Eb4', 'Ab4'], ['Ab1', 'Eb2', 'Ab2', 'C3']),
}

PROG = (['Fm', 'Fm', 'Gb', 'Fm'] +                                  # 1-4 intro
        ['Fm', 'Db', 'Eb', 'C', 'Fm', 'Db', 'Bbm', 'C'] +           # 5-12 A
        ['Db', 'Eb', 'Fm', 'Fm', 'Bbm', 'Gb', 'C', 'C'] +           # 13-20 B
        ['Fm', 'Gb', 'Fm', 'Gb'] +                                  # 21-24 breakdown
        ['Fm', 'Db', 'Eb', 'Ab', 'Bbm', 'Gb', 'C', 'C'])            # 25-32 climax

THEME_A = ('C4:1 F4:2 Ab4:2 G4:1 | F4:3 Db4:3 | Eb4:1 G4:2 Bb4:2 Ab4:1 | G4:3 E4:3 | '
           'C4:1 F4:2 Ab4:2 C5:1 | Db5:3 C5:2 Ab4:1 | Bb4:2 Db5:1 F4:3 | E4:3 G4:2 C5:1 |')
THEME_B = ('Ab4:1 Db5:2 F5:3 | G5:2 F5:1 Eb5:3 | F5:3 Ab5:2 G5:1 | F5:6 | '
           'Db6:3 C6:2 Bb5:1 | Bb5:2 Ab5:1 Gb5:3 | G5:3 E5:2 F5:1 | E5:3 G5:3 |')
THEME_S = ('C5:1 F5:2 Ab5:2 C6:1 | Db6:3 C6:2 Ab5:1 | Bb5:2 G5:1 Eb6:3 | C6:3 Ab5:3 | '
           'Bb5:1 Db6:2 F6:2 Eb6:1 | Db6:3 Bb5:2 Gb5:1 | G5:2 C6:1 E6:3 | E6:3 C6:3 |')
MOAN = 'F3:6 | Gb3:6 | F3:3 Ab3:3 | Gb3:6 |'
ROLL = [0, 1, 2, 3, 2, 1]        # the cellos' wave across each bar


@instrument
def abyss_organ(midi, dur, vel, seed, attack=0.12, release=0.5, bright=1.0, trem=5.2, depth=0.14):
    """Deep church organ: 16' 8' 4' 2 2/3' 2' drawbars, a slow rotary tremolo whose two sides
    move against each other (stereo), a breath of wind at the attack."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = np.random.default_rng(int(midi) * 7919 + seed)
    amps = np.zeros(16)
    for k, a in ((1, 0.55), (2, 1.0), (4, 0.6 * bright), (6, 0.32 * bright), (8, 0.28 * bright),
                 (12, 0.1 * bright), (16, 0.06 * bright)):
        amps[k - 1] = a
    base = harmonic_osc(amps, f * 0.5, n, rng.random())
    wobble = 1.0 + 0.0015 * np.sin(2 * np.pi * trem * 0.5 * t)
    upper = harmonic_osc(amps, f * 0.5 * 1.0012 * wobble, n, rng.random())
    lfo = np.sin(2 * np.pi * trem * t + rng.random() * 6.28)
    L = (base + 0.5 * upper) * (1.0 - depth * (0.5 + 0.5 * lfo))
    R = (base + 0.5 * upper) * (1.0 - depth * (0.5 - 0.5 * lfo))
    wind = filt(pink(n, rng), bp(min(f * 4.0, 6000.0), 0.8))[:n] * perc(n, 0.02, 0.12) * 0.15
    env = adsr(n, attack, 0.3, 0.92, release, dur)
    st = np.vstack([L + wind, R + wind]) * env
    return st * vel * 0.16


@_ro
def sonar(freq=1320.0, variant=0):
    """A sonar ping: a pure tone struck hard, falling a hair in pitch, with a faint overtone."""
    n = ns(1.6)
    t = np.arange(n) / SR
    f = freq * (1.0 - 0.01 * (1.0 - np.exp(-t / 0.3)))
    ph = np.cumsum(f) / SR
    s = np.sin(2 * np.pi * ph) * perc(n, 0.002, 0.42) + 0.12 * np.sin(4 * np.pi * ph) * perc(n, 0.001, 0.12)
    return s * np.clip((1.6 - t) / 0.2, 0, 1)


@_ro
def surge(seconds, variant=0, center=420.0):
    """A wall of water swelling and breaking (stereo): low dark noise rising to a crest."""
    n = ns(seconds)
    t = np.arange(n) / SR
    rng = _rng(f'surge{variant}')
    env = np.sin(np.pi * np.clip(t / seconds, 0, 1)) ** 1.5
    a = filt(pink(n, rng), bp(center, 0.7), lp(1800.0, 0.7))
    b = filt(pink(n, rng), bp(center * 1.3, 0.7), lp(1800.0, 0.7))
    hiss = filt(pink(n, rng), hp(2500.0, 0.7)) * env ** 3 * 0.25
    return np.vstack([(a + hiss) * env, (b + hiss) * env])


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=6.0, t60=4.6, predelay=0.05, ret=0.42, hf=0.22, lf=1.25)
    s.delay.update(beats=3.0, feedback=0.42, damp=1800.0, ret=0.22, to_reverb=0.5)
    s.duck_tau = 0.18

    sub = s.track('sub', gain_db=-7.0, duck=0.25, lowpass=170, highpass=30)
    cellos = s.track('cellos', gain_db=-5.0, reverb=0.35, duck=0.2, highpass=55)
    lowstr = s.track('low strings', gain_db=-6.0, reverb=0.45, highpass=60)
    organ = s.track('organ', gain_db=-4.0, reverb=0.5, duck=0.15, highpass=40)
    horns = s.track('horns', gain_db=-9.5, reverb=0.45, delay=0.08, highpass=70)
    choir = s.track('choir', gain_db=-3.5, reverb=0.65, duck=0.12, highpass=140)
    pings = s.track('sonar', gain_db=-10.0, reverb=0.5, delay=0.7, highpass=600, pan=0.3)
    water = s.track('water', gain_db=-6.0, reverb=0.5, highpass=80)
    bubbles = s.track('bubbles', gain_db=-9.0, reverb=0.45, delay=0.2, highpass=150)
    kick = s.track('kick', gain_db=-7.0, reverb=0.1)
    snare = s.track('snare', gain_db=-7.0, reverb=0.5)
    taiko = s.track('taiko', gain_db=-4.0, reverb=0.45)
    toms = s.track('toms', gain_db=-7.0, reverb=0.4)
    shaker = s.track('shaker', gain_db=-10.0, reverb=0.25, pan=-0.25, highpass=3000)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.5)
    fx = s.track('fx', gain_db=-9.0, reverb=0.5)
    rng = s.rng('events')

    for bar, nb, name in blocks(PROG):
        root, org_v, ch_v, rolls = CHORD[name]
        r = note_midi(root)
        beats = nb * 6 - 0.2
        sub.note(I.bass_sub, bar, 0, beats, r if r >= 30 else r + 12, vel=0.75, attack=0.3, release=1.0, drive=1.3)
        quiet = bar <= 4 or 21 <= bar <= 24
        # the organ: pedal alone in the intro / breakdown, full chords elsewhere
        organ.note(abyss_organ, bar, 0, beats, r + 12, vel=0.8, attack=0.3, release=0.8, bright=0.6)
        if not quiet:
            organ.chord(abyss_organ, bar, 0, beats, org_v, vel=0.55 if bar <= 12 else 0.7, attack=0.15,
                        bright=0.8 if bar <= 12 else 1.1)
        if 13 <= bar <= 20 or bar >= 25:
            choir.chord(I.pad_choir, bar, 0, beats, ch_v, vel=0.68, vowel='o', attack=0.4, release=1.2)
        elif quiet:
            choir.chord(I.pad_choir, bar, 0, beats, [note_midi(x) - 12 for x in ch_v], vel=0.5, vowel='u',
                        attack=1.0, release=1.5)
        if 13 <= bar <= 20 or bar >= 25:
            lowstr.chord(I.strings_legato, bar, 0, beats, [r + 12, r + 19], vel=0.6, attack=0.9, release=1.0,
                         bright=3.0)

    for bar, name in enumerate(PROG, start=1):
        root, org_v, ch_v, rolls = CHORD[name]
        quiet = bar <= 4 or 21 <= bar <= 24
        # rolling cellos: an eighth-note wave, swelling to the middle of the bar
        if not quiet:
            for k in range(6):
                v = (0.6 + 0.25 * np.sin(np.pi * (k + 0.5) / 6)) * (0.9 if bar <= 12 else 1.0)
                cellos.note(I.strings_stacc, bar, k, 0.9, rolls[ROLL[k]], vel=float(v), decay=0.16,
                            pan=(-0.3 if k % 2 else 0.3))
        # sonar: a ping every other bar where it is quiet, and on the climax's odd bars
        if (quiet and bar % 2 == 1) or (bar >= 25 and bar % 4 == 1):
            pings.at(sonar(1320.0, variant=bar), bar, 0, gain=1.0)
        if quiet and bar % 2 == 0:
            pings.at(sonar(1760.0, variant=bar), bar, 3, gain=0.7, pan=-0.4)
        # bubbles rising from the deep
        for _ in range(3 if quiet else 1):
            pos = s.pos(bar) + int(rng.uniform(0, 6) * s.spb * SR)
            bubbles.add(lava_blub(rng, rng.uniform(0.25, 0.5)) * rng.uniform(0.4, 1.0), pos,
                        pan=rng.uniform(-0.7, 0.7))

    cellos.line(I.strings_legato, 5, THEME_A, vel=0.85, legato=0.95, transpose=-12, attack=0.12, bright=3.5)
    lowstr.line(I.strings_legato, 5, THEME_A, vel=0.6, legato=0.95, attack=0.15, bright=3.0)
    horns.line(T.brass_low, 13, THEME_B, vel=0.85, legato=0.94, transpose=-12, bright=3.2, max_cut=2600.0)
    organ.line(abyss_organ, 13, THEME_B, vel=0.45, legato=0.96, attack=0.06, bright=1.2)
    lowstr.line(I.strings_legato, 21, MOAN, vel=0.75, legato=0.98, attack=0.8, release=1.5, bright=2.5)
    organ.line(abyss_organ, 25, THEME_S, vel=0.6, legato=0.96, attack=0.05, bright=1.3)
    horns.line(T.brass_low, 25, THEME_S, vel=0.9, legato=0.94, transpose=-24, bright=3.0, max_cut=2200.0,
               growl=0.2)
    choir.line(I.pad_choir, 25, THEME_S, vel=0.45, legato=0.98, transpose=-12, vowel='a', attack=0.15,
               release=0.8)

    # ---- drums: the swell (beats 0 and 3 of each 6/8 bar)
    kk = D.kick(f_start=110, f_end=42, pitch_tau=0.04, decay=0.38, click=0.2, drive=2.0, length=0.8, variant=91)
    sn = D.snare(tone=160, tone_decay=0.09, noise_decay=0.22, noise_lp=6000, tone_mix=0.6, roomy=0.6,
                 variant=91)
    tk = D.taiko(46.0, variant=91, length=1.6)
    tk_hi = D.taiko(70.0, variant=92)
    tm_lo, tm_mid, tm_hi = D.tom(82.0, decay=0.4, variant=91), D.tom(110.0, variant=91), D.tom(150.0, variant=91)
    sk = D.shaker(variant=91, center=4200.0, decay=0.05)
    heart = P.sub_hit(42.0, length=0.6, variant=92)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.....', step=1.0, duck=True)
            continue
        if 21 <= bar <= 24:
            fx.hits(heart, bar, 'x..x..', step=1.0, vel=0.8)
            taiko.hits(tk, bar, 'X.....' if bar % 2 else '......', step=1.0, vel=0.8, duck=True)
            continue
        big = bar >= 25
        B = 13 <= bar <= 20 or big
        taiko.hits(tk, bar, 'X..x..' if not big else 'X.xX.x', step=1.0, vel=0.9, duck=True)
        kick.hits(kk, bar, 'x.....' if not B else 'x..x..', step=1.0)
        snare.hits(sn, bar, '...x..', step=1.0, vel=0.85 if B else 0.6)
        if B:
            toms.hits(tm_hi, bar, '.x....', step=1.0, vel=0.6, pan=0.35)
            toms.hits(tm_mid, bar, '..x...', step=1.0, vel=0.6)
            toms.hits(tm_lo, bar, '....xx', step=1.0, vel=0.7, pan=-0.35)
            shaker.hits(sk, bar, 'xoxoxoxoxoxo', step=0.5, vel=0.6)
        else:
            taiko.hits(tk_hi, bar, '.....o', step=1.0, vel=0.7, pan=0.3)
            shaker.hits(sk, bar, 'x.o.x.o.x.o.', step=0.5, vel=0.5)
    for bar in (12, 20):
        toms.hits(tm_hi, bar, '..xxxx', step=1.0, vel=0.8, pan=0.35)
        toms.hits(tm_lo, bar, '....xx', step=1.0, vel=0.9, pan=-0.35)
    snare.hits(sn, 32, 'oooooooooxxX', step=0.5, vel=0.85)
    # water: surges into the sections, a swell on every second bar of the B
    for bar, nbars in ((3, 2), (11, 2), (23, 2), (31, 2)):
        water.at(surge(s.spb * 6 * nbars, variant=bar), bar, 0, gain=1.2)
    for bar in range(13, 21, 2):
        water.at(surge(s.spb * 6 * 2, variant=bar, center=520.0), bar, 0, gain=0.7)
    for bar in (1, 21):
        fx.at(P.sub_hit(34.0, length=3.0, variant=bar + 90), bar, 0, gain=1.2)
    for bar in (5, 13, 25):
        fx.at(D.boom(36.0, length=2.5, variant=bar + 90), bar, 0, gain=1.0)
        cym.at(D.crash(length=3.5, variant=bar % 3), bar, 0, pan=0.3)
    for bar in (12, 20, 24):
        fx.at(D.reverse_cymbal(s.spb * 6, variant=bar + 90), bar, 0.0)
    fx.at(D.riser(s.spb * 12, f0=100, f1=4000, q=2.5, variant=91), 31, 0, gain=0.8)
    return s
