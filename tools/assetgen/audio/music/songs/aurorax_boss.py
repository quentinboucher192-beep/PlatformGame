"""aurorax_boss - Aurorax, le Miroir boreal (sanctuary of Aethra). F# minor (harmonic: the C#
major dominant), a fast waltz: 180 BPM in 3/4, 66 bars = 66.0 s (one 16th note = exactly 3675
samples).

Character: a dark, glittering ice waltz.  Every phrase of the theme is answered one bar later by
its 'reflection' (the same line an octave higher on glass bells, panned to the other side), like
the doubles of Aurorax; oom-pah-pah strings, glass plucks, ice ticks, choir and taiko.

Form:  intro 1-4 (glass ostinato, bells)  | A 5-20 (waltz, theme + reflection)
       | B 21-36 (choir, soaring strings, double-time ticks)  | C 37-44 (breakdown: bells and choir)
       | A' 45-60 (full)  | build 61-66 (snare roll back into the intro).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'aurorax_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 180, 3, 66, True

# name: (bass root, waltz chord (3), glass ostinato tones (3), choir voicing)
CHORD = {
    'F#m': ('F#1', ['A3', 'C#4', 'F#4'], ['F#5', 'C#6', 'A5'], ['C#4', 'F#4', 'A4']),
    'D':   ('D2', ['A3', 'D4', 'F#4'], ['D5', 'A5', 'F#5'], ['D4', 'F#4', 'A4']),
    'C#':  ('C#2', ['G#3', 'C#4', 'E#4'], ['C#5', 'G#5', 'E#5'], ['C#4', 'E#4', 'G#4']),
    'E':   ('E2', ['G#3', 'B3', 'E4'], ['E5', 'B5', 'G#5'], ['B3', 'E4', 'G#4']),
    'Bm':  ('B1', ['F#3', 'B3', 'D4'], ['B4', 'F#5', 'D5'], ['B3', 'D4', 'F#4']),
    'A':   ('A1', ['A3', 'C#4', 'E4'], ['A4', 'E5', 'C#5'], ['C#4', 'E4', 'A4']),
}

A_PROG = ['F#m', 'F#m', 'D', 'E', 'F#m', 'F#m', 'Bm', 'C#', 'F#m', 'A', 'D', 'E', 'Bm', 'D', 'C#', 'C#']
B_PROG = ['D', 'E', 'A', 'F#m', 'D', 'E', 'C#', 'C#', 'Bm', 'E', 'A', 'F#m', 'D', 'Bm', 'C#', 'C#']
PROG = (['F#m', 'F#m', 'D', 'C#'] + A_PROG + B_PROG + ['F#m', 'D', 'F#m', 'D', 'Bm', 'Bm', 'C#', 'C#']
        + A_PROG + ['Bm', 'Bm', 'D', 'D', 'C#', 'C#'])

# theme in 2-bar phrases; the reflection plays each phrase one bar later, a third higher
THEME_A = ('F#5:2 A5 | C#6:3 | B5 A5 F#5 | E5:3 | F#5:2 A5 | D6:3 | C#6 B5 A5 | G#5:3 | '
           'A5:2 C#6 | E6:3 | D6 C#6 B5 | A5:3 | B5:2 D6 | F#6:2 E6 | E#6:3 | C#6:3 |')
THEME_B = ('A5:3 | B5:2 C#6 | E6:3 | C#6:3 | D6:2 C#6 | B5:2 G#5 | E#5:3 | G#5:3 | '
           'F#5:2 B5 | D6:2 E6 | C#6:2 A5 | F#5:3 | A5:2 D6 | F#6:2 D6 | C#6:3 | E#6:3 |')
BREAK = 'F#6:3 | D6:3 | C#6:3 | A5:3 | B5:3 | D6:3 | C#6:3 | E#6:3 |'
def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=3.6, t60=2.6, predelay=0.02, ret=0.32, hf=0.5, lf=1.0)
    s.delay.update(beats=1.5, feedback=0.3, damp=5000.0, ret=0.24, to_reverb=0.35)
    s.duck_tau = 0.08

    bass = s.track('bass', gain_db=-5.0, duck=0.35, lowpass=2400, highpass=35)
    waltz = s.track('waltz', gain_db=-6.0, reverb=0.3, duck=0.2, highpass=150)
    glass = s.track('glass', gain_db=-5.0, reverb=0.35, delay=0.15, duck=0.1, highpass=500)
    lead = s.track('lead', gain_db=-6.0, reverb=0.4, delay=0.12, highpass=250, pan=-0.25)
    mirror = s.track('mirror', gain_db=-6.0, reverb=0.55, delay=0.2, highpass=400, pan=0.35)
    strings = s.track('strings', gain_db=-9.0, reverb=0.45, duck=0.2, highpass=120)
    choir = s.track('choir', gain_db=-3.0, reverb=0.6, duck=0.15, highpass=180)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    snare = s.track('snare', gain_db=-6.0, reverb=0.3)
    ticks = s.track('ticks', gain_db=-2.0, reverb=0.2, pan=0.2, highpass=2500)
    taiko = s.track('taiko', gain_db=-7.0, reverb=0.3)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.35)
    fx = s.track('fx', gain_db=-10.0, reverb=0.4)

    sec_b = range(21, 37)
    sec_c = range(37, 45)
    for bar, nb, name in blocks(PROG):
        root, chord, _, ch_v = CHORD[name]
        if bar in sec_b or bar in sec_c or bar >= 61:
            choir.chord(I.pad_choir, bar, 0, nb * 3 - 0.1, ch_v, vel=0.66, vowel='a' if bar not in sec_c else 'o',
                        attack=0.25, release=0.8)
        if bar in sec_b or bar >= 45:
            strings.chord(I.strings_legato, bar, 0, nb * 3 - 0.1, [note_midi(x) + 12 for x in chord], vel=0.6,
                          attack=0.15, release=0.5)

    for bar, name in enumerate(PROG, start=1):
        root, chord, tones, ch_v = CHORD[name]
        r = note_midi(root)
        intro = bar <= 4
        # oom-pah-pah: bass on 1 (fifth on alternate bars), chord stabs on 2 and 3
        if intro or bar in sec_c:
            bass.note(I.bass_sub, bar, 0, 2.8, r, vel=0.85, attack=0.02, release=0.3, drive=1.4)
        else:
            bass.note(I.bass_pluck, bar, 0, 0.9, r if bar % 2 else r + 7, vel=0.92, peak=5.0, floor=1.5, tau=0.08,
                      q=1.2, sub=0.65, drive=1.6)
            bass.note(I.bass_pluck, bar, 2.5, 0.4, r + 12, vel=0.6, peak=4.0, floor=1.4, tau=0.06, q=1.2, sub=0.5)
            for beat in (1, 2):
                waltz.chord(I.strings_stacc, bar, beat, 0.5, chord, vel=0.8 if beat == 1 else 0.68, decay=0.1)
        # glass ostinato in 8ths
        if not (bar in sec_c):
            for k in range(6):
                v = 0.7 if k % 2 == 0 else 0.5
                glass.note(T.glass_pluck, bar, k * 0.5, 0.5, tones[[0, 1, 2, 1, 2, 1][k]], vel=v * (0.85 if intro else 1),
                           decay=0.4, pan=(-0.35 if k % 2 else 0.35))

    # theme and its reflection (one bar later, an octave above, on glass bells, panned opposite)
    lead.line(I.lead_glass, 5, THEME_A, vel=0.9, legato=0.95)
    mirror.line(T.ice_bell, 6, THEME_A, vel=0.55, transpose=12, decay=1.4)
    lead.line(I.lead_glass, 21, THEME_B, vel=0.95, legato=0.95)
    lead.line(I.lead_brass, 21, THEME_B, vel=0.5, legato=0.92, transpose=-12, bright=3.0, max_cut=2400.0)
    mirror.line(T.ice_bell, 22, THEME_B, vel=0.55, transpose=12, decay=1.4)
    mirror.line(T.ice_bell, 37, BREAK, vel=0.75, decay=2.6)
    mirror.line(T.ice_bell, 38, BREAK, vel=0.4, transpose=-12, decay=2.0)
    lead.line(I.lead_glass, 45, THEME_A, vel=0.95, legato=0.95)
    lead.line(I.lead_brass, 45, THEME_A, vel=0.45, legato=0.92, transpose=-12, bright=3.0, max_cut=2400.0)
    mirror.line(T.ice_bell, 46, THEME_A, vel=0.55, transpose=12, decay=1.4)

    # ---- drums in 3/4 (12 sixteenths per bar)
    kk = D.kick(f_start=150, f_end=46, pitch_tau=0.03, decay=0.26, click=0.35, drive=1.8, variant=31)
    sn = D.snare(tone=200, tone_decay=0.06, noise_decay=0.14, noise_lp=9000, tone_mix=0.6, roomy=0.4, variant=31)
    tick = P.ice_tick(4200.0, variant=31)
    tick2 = P.ice_tick(3300.0, variant=32)
    tk = D.taiko(60.0, variant=31)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.....x.....', duck=True)
            if bar >= 3:
                ticks.hits(tick, bar, 'x.x.x.x.x.x.', vel=0.6)
            continue
        if bar in sec_c:
            taiko.hits(tk, bar, 'X...........' if bar % 2 else 'x.......x...', vel=0.8, duck=True)
            ticks.hits(tick2, bar, '....x.......', vel=0.5)
            continue
        if bar >= 61:
            kick.hits(kk, bar, 'x...x...x...', duck=True)
            roll = {61: 'g.g.g.o.o.o.', 62: 'o.o.o.oooooo', 63: 'oooooooooooo', 64: 'oooooooooooo',
                    65: 'xxxxxxxxxxxx', 66: 'xxxxxxXXXXXX'}[bar]
            snare.hits(sn, bar, roll, vel=0.55 + 0.07 * (bar - 61))
            ticks.hits(tick, bar, 'x.x.x.x.x.x.', vel=0.7)
            continue
        B = bar in sec_b
        kick.hits(kk, bar, 'x...........' if not B else 'x.......x.x.', duck=True)
        snare.hits(sn, bar, '....x...x...' if not B else '....x...x..g', vel=0.75)
        ticks.hits(tick, bar, 'x.xox.xox.xo' if not B else 'xoxoxoxoxoxo', vel=0.8)
        if B and bar % 4 == 1:
            taiko.hits(tk, bar, 'X.....x.....', vel=0.9)
    for bar in (20, 36, 60):
        snare.hits(sn, bar, '......ooxxXX', vel=0.8)
    fx.at(D.boom(52.0, variant=31), 1, 0, gain=0.9)
    fx.at(D.riser(s.spb * 6, f0=400, f1=9000, q=3.0, variant=31), 64, 0, gain=0.9)
    fx.at(D.reverse_cymbal(s.spb * 3), 20, 0.0)
    for bar in (5, 21, 29, 45, 53):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(P.sub_hit(46.0, variant=37), 37, 0, gain=1.0)
    return s
