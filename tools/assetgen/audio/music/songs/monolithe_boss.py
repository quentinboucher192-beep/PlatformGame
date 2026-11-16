"""monolithe_boss - Monolithe, le Golem Primordial (giga boss of Les Confins). G minor with a
phrygian A-flat and a harmonic-minor D major, 100 BPM, 4/4, 30 bars = 72.0 s (one 16th note =
exactly 6615 samples).

Character: a rune-carved granite golem older than the planets, slow and crushing.  Every step is
a landslide: huge low drums and stone hits, gongs, a deep male choir droning under a heavy riff
of saturated bass and growling low brass, and above it all the glow of its runes, a cold motif on
bells and celesta.

Form:  intro 1-4 (gong, sub drops, the choir's drone, the first runes light up)
       | A 5-12 (the riff, half-time drums, stone hits on the backbeat)
       | B 13-20 (the rune motif on bells and celesta, the choir chanting, gongs)
       | breakdown 21-24 (the riff stops: stone hits, the drone, the runes alone, a rising rumble)
       | climax 25-30 (the rune motif on brass and choir over the riff, a riser back into the loop).
"""
import numpy as np

from dsp import bp, filt, hp, lp, soft_clip
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.drums import _ro, _rng
from music.sequencer import Song
from music.textures import blocks
from synth import SR, brown, note_midi, ns, perc, sine, white

NAME = 'monolithe_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 100, 4, 30, True

# name: (bass root, male choir voicing, bell dyad)
CHORD = {
    'Gm': ('G1', ['D3', 'G3', 'Bb3'], ['G5', 'D6']),
    'Ab': ('Ab1', ['Eb3', 'Ab3', 'C4'], ['Ab5', 'Eb6']),
    'Eb': ('Eb2', ['Eb3', 'G3', 'Bb3'], ['Eb5', 'Bb5']),
    'Cm': ('C2', ['C3', 'G3', 'Eb4'], ['C5', 'G5']),
    'D': ('D2', ['D3', 'F#3', 'A3'], ['D5', 'A5']),
    'Bb': ('Bb1', ['D3', 'F3', 'Bb3'], ['Bb4', 'F5']),
}

PROG = (['Gm', 'Gm', 'Ab', 'Gm'] +                                  # 1-4 intro
        ['Gm', 'Gm', 'Ab', 'Gm', 'Eb', 'Cm', 'D', 'D'] +            # 5-12 A
        ['Eb', 'Bb', 'Cm', 'Gm', 'Ab', 'Eb', 'D', 'D'] +            # 13-20 B
        ['Gm', 'Ab', 'Gm', 'Ab'] +                                  # 21-24 breakdown
        ['Gm', 'Eb', 'Cm', 'Ab', 'D', 'D'])                         # 25-30 climax

# the riff: (16th step, interval above the root, length in beats); on G it grinds on the
# phrygian A-flat and the minor third, on the other chords it stays on root / fifth / octave
RIFF = [(0, 0, 0.7), (3, 0, 0.2), (4, 0, 0.45), (6, 1, 0.2), (7, 0, 0.2), (8, 0, 0.7), (11, 3, 0.2),
        (12, 1, 0.45), (14, 0, 0.45)]
SAFE = {0: 0, 1: 0, 3: 7}

RUNES_INTRO = 'C6:2 Eb6:2 | D6:4 |'
RUNES_B = ('G5:1 Bb5:1 Eb6:2 | D6:1 F6:1 Bb5:2 | C6:1 Eb6:1 G6:1.5 F6:.5 | D6:4 | '
           'C6:1 Eb6:1 Ab6:2 | G6:1.5 F6:.5 Eb6:1 Bb5:1 | A5:1 D6:1 F#6:2 | F#6:2 A6:2 |')
RUNES_BREAK = 'G5:1 D6:1 Bb5:2 | C6:1 Eb6:1 Ab5:2 | G5:1 D6:1 Bb5:1 A5:1 | Ab5:4 |'
THEME_C = ('G5:1 Bb5:1 D6:2 | Eb6:1.5 D6:.5 Bb5:2 | C6:1 Eb6:1 G6:2 | Ab5:1 C6:1 Eb6:2 | '
           'F#6:2 D6:1 A5:1 | D6:4 |')


@_ro
def stone_hit(variant=0, size=1.0):
    """A block of granite crashing down: a deep thump, the crack of the stone and gravel."""
    n = ns(1.4)
    t = np.arange(n) / SR
    rng = _rng(f'stone{variant}')
    f = 52.0 + 70.0 * np.exp(-t / (0.035 * size))
    thump = sine(f, n) * perc(n, 0.002, 0.22 * size)
    body = filt(brown(n, rng), lp(380.0, 0.7)) * perc(n, 0.002, 0.12 * size) * 2.2
    crack = filt(white(n, rng), bp(1900.0, 0.9), hp(700.0, 0.7)) * perc(n, 0.0003, 0.012)
    gravel = np.zeros(n)
    g = ns(0.006)
    for _ in range(40):
        p = int(ns(0.02) + rng.random() ** 1.6 * ns(0.5))
        gr = filt(white(g + 64, rng), bp(rng.uniform(1500.0, 4500.0), 1.2))[:g] * np.hanning(g)
        gravel[p:p + g] += gr * rng.uniform(0.1, 0.5) * np.exp(-p / ns(0.25))
    out = soft_clip(thump * 1.2 + body, 1.6) + crack * 0.7 + gravel * 0.6
    return out * np.clip((1.4 - t) / 0.2, 0, 1)


_GONG = ((1.0, 1.0, 1.0, 0.004), (1.47, 0.7, 0.8, 0.12), (1.98, 0.55, 0.7, 0.2), (2.43, 0.45, 0.6, 0.35),
         (2.94, 0.4, 0.5, 0.45), (3.51, 0.3, 0.42, 0.6), (4.19, 0.25, 0.35, 0.7), (5.08, 0.18, 0.3, 0.8),
         (6.31, 0.12, 0.25, 0.9))


@_ro
def gong(freq=82.0, length=6.0, variant=0):
    """Tam-tam: a mallet thud, then inharmonic partials that bloom and shimmer (mono)."""
    n = ns(length)
    t = np.arange(n) / SR
    rng = _rng(f'gong{variant}')
    out = np.zeros(n)
    for r, a, dm, bloom in _GONG:
        fr = freq * r * (1.0 - 0.004 * np.exp(-t / 0.8))
        env = (1.0 - np.exp(-t / bloom)) * np.exp(-t / (2.2 * dm))
        out += a * np.sin(2 * np.pi * np.cumsum(fr) / SR + rng.random() * 6.28) * env
    wash = filt(white(n, rng), bp(3200.0, 0.6), hp(1200.0, 0.7)) * (1.0 - np.exp(-t / 0.5)) * np.exp(-t / 1.4)
    thud = sine(freq * 0.75, n) * perc(n, 0.003, 0.25)
    out = out * 0.35 + wash * 0.25 + thud * 0.5
    return out * np.clip((length - t) / 0.8, 0, 1)


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=5.0, t60=3.6, predelay=0.04, ret=0.34, hf=0.35, lf=1.2)
    s.delay.update(beats=1.5, feedback=0.3, damp=2600.0, ret=0.16, to_reverb=0.4)
    s.duck_tau = 0.14

    bass = s.track('bass', gain_db=-6.0, duck=0.35, lowpass=1800, highpass=42)
    sub = s.track('sub', gain_db=-9.0, duck=0.25, lowpass=160, highpass=42)
    riff = s.track('riff', gain_db=-6.0, reverb=0.25, highpass=60)
    brass = s.track('brass', gain_db=-9.5, reverb=0.35, delay=0.05, highpass=60)
    strings = s.track('strings', gain_db=-10.0, reverb=0.5, duck=0.2, highpass=70)
    choir = s.track('choir', gain_db=-2.0, reverb=0.6, duck=0.12, highpass=80)
    runes = s.track('runes', gain_db=-6.0, reverb=0.55, delay=0.35, highpass=400, pan=0.15)
    bells = s.track('bells', gain_db=-8.0, reverb=0.6, delay=0.2, highpass=250, pan=-0.15)
    kick = s.track('kick', gain_db=-6.5, reverb=0.06, highpass=42)
    snare = s.track('snare', gain_db=-6.0, reverb=0.4)
    stone = s.track('stone', gain_db=-2.0, reverb=0.35, highpass=45)
    taiko = s.track('taiko', gain_db=-3.5, reverb=0.4, highpass=42)
    shaker = s.track('gravel', gain_db=-9.0, reverb=0.15, pan=0.25, highpass=2500)
    gongs = s.track('gongs', gain_db=-6.0, reverb=0.5, highpass=45)
    fx = s.track('fx', gain_db=-9.0, reverb=0.45, highpass=40)

    for bar, nb, name in blocks(PROG):
        root, ch_v, dyad = CHORD[name]
        r = note_midi(root)
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r if r >= 31 else r + 12, vel=0.75, attack=0.2,
                 release=0.9, drive=1.4)
        drone = bar <= 4 or 21 <= bar <= 24
        if drone:
            # the deep drone: root and fifth, an octave below the chant
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, [r + 12, r + 19, r + 24], vel=0.62, vowel='u',
                        attack=0.6, release=1.2)
        else:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.52 if bar <= 12 else 0.7,
                        vowel='o' if bar <= 12 else 'a', attack=0.3, release=1.0)
        if bar >= 13 and not drone:
            strings.chord(I.strings_legato, bar, 0, nb * 4 - 0.2, [r + 12, r + 19, r + 24], vel=0.55,
                          attack=0.5, release=0.9, bright=3.0)

    for bar, name in enumerate(PROG, start=1):
        root, ch_v, dyad = CHORD[name]
        r = note_midi(root)
        if 5 <= bar <= 20 or bar >= 25:
            for step, iv, ln in RIFF:
                iv = iv if name == 'Gm' else SAFE[iv]
                v = 0.95 if step in (0, 8) else 0.75
                bass.note(T.bass_forge, bar, step * 0.25, ln, r + iv, vel=v, peak_mul=3.5, floor=1.2, tau=0.1,
                          drive=3.4, top=2600.0)
                riff.note(T.brass_low, bar, step * 0.25, ln, r + 12 + iv, vel=v * 0.85, attack=0.02,
                          release=0.12, bright=3.2, max_cut=1800.0, drive=2.6, growl=0.35, vib=0.0)
        elif bar <= 4:
            # intro: the golem's steps, one per half bar
            for beat in (0, 2):
                bass.note(T.bass_forge, bar, beat, 1.2, r, vel=0.9, peak_mul=2.6, floor=1.1, tau=0.2, drive=2.6,
                          top=1600.0)
        # bells on the downbeats of B and the climax
        if 13 <= bar <= 20 or bar >= 25:
            bells.chord(T.ice_bell, bar, 0, 2, dyad, vel=0.5, decay=2.4, bright=0.8)

    runes.line(I.celesta, 3, RUNES_INTRO, vel=0.55, decay=1.6)
    runes.line(I.celesta, 13, RUNES_B, vel=0.7, decay=1.4)
    bells.line(T.ice_bell, 13, RUNES_B, vel=0.6, transpose=-12, decay=2.2, bright=0.9)
    runes.line(I.celesta, 21, RUNES_BREAK, vel=0.75, decay=1.8)
    bells.line(T.ice_bell, 21, RUNES_BREAK, vel=0.55, transpose=-12, decay=2.8, bright=0.8)
    brass.line(T.brass_low, 25, THEME_C, vel=0.95, legato=0.94, transpose=-24, bright=3.4, max_cut=2400.0,
               growl=0.25)
    brass.line(T.brass_low, 25, THEME_C, vel=0.6, legato=0.94, transpose=-12, bright=3.8, max_cut=3200.0)
    choir.line(I.pad_choir, 25, THEME_C, vel=0.5, legato=0.98, transpose=-24, vowel='a', attack=0.1,
               release=0.6)
    runes.line(I.celesta, 25, THEME_C, vel=0.65, decay=1.2)

    # ---- drums and stone
    kk = D.kick(f_start=120, f_end=48, pitch_tau=0.04, decay=0.42, click=0.3, drive=2.4, length=0.8,
                variant=81)
    sn = D.snare(tone=150, tone_decay=0.1, noise_decay=0.24, noise_lp=6500, tone_mix=0.75, roomy=0.6,
                 variant=81)
    cl = D.clap(variant=8, decay=0.16)
    tk = D.taiko(50.0, variant=81, length=1.6)
    tk_hi = D.taiko(66.0, variant=82)
    sk = D.shaker(variant=81, center=3800.0)
    st = stone_hit(variant=1)
    st_big = stone_hit(variant=2, size=1.6)
    tm_lo = D.tom(78.0, decay=0.4, variant=81)
    tm_hi = D.tom(112.0, decay=0.35, variant=81)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.......X.......', duck=True)
            stone.hits(st, bar, '........x.......' if bar % 2 else '........x.....o.', vel=0.8)
            continue
        if 21 <= bar <= 24:
            taiko.hits(tk, bar, 'X...............' if bar % 2 else 'X.......o.......', vel=0.85, duck=True)
            stone.hits(st_big if bar % 2 else st, bar, '........x.......', vel=0.9)
            continue
        big = bar >= 25
        B = 13 <= bar <= 20 or big
        kick.hits(kk, bar, 'x.....x.x.......' if not B else ('x.....x.x...x...' if not big else 'x..x..x.x..x..x.'),
                  duck=True)
        snare.hits(sn, bar, '........X.......')
        snare.hits(cl, bar, '........x.......', vel=0.5)
        stone.hits(st, bar, '........x.......', vel=0.75)
        taiko.hits(tk, bar, 'X.......x.....x.' if not big else 'X..x..x.X..x..x.', vel=0.9)
        taiko.hits(tk_hi, bar, '....o.......o...' if B else '............o...', vel=0.75, pan=0.3)
        if B:
            shaker.hits(sk, bar, 'x.o.x.o.x.o.x.oo', vel=0.6)
    for bar in (12, 20):
        snare.hits(tm_hi, bar, '........x.x.x...', pan=0.3)
        snare.hits(tm_lo, bar, '..............xx', pan=-0.3)
    stone.hits(st_big, 24, '........x...x.x.', vel=0.9)
    snare.hits(sn, 30, '........o.o.xxXX', vel=0.85)
    snare.hits(tm_lo, 30, 'x...x...x.x.x.x.', vel=0.8, pan=-0.3)
    for bar, f in ((1, 82.0), (5, 82.0), (13, 98.0), (21, 73.0), (25, 82.0)):
        gongs.at(gong(f, variant=bar), bar, 0, pan=-0.2 if bar % 2 else 0.2)
    for bar in (1, 21):
        fx.at(P.sub_hit(46.0, length=2.5, variant=bar + 80), bar, 0, gain=1.2)
    for bar in (5, 25):
        fx.at(D.boom(46.0, variant=bar + 80), bar, 0, gain=1.0)
    for bar in (12, 20):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar + 80), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=120, f1=5000, q=2.5, variant=81), 23, 0, gain=0.9)
    fx.at(D.riser(s.spb * 8, f0=150, f1=6500, q=2.8, variant=82), 29, 0, gain=0.8)
    return s
