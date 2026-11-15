"""nyxara_boss - Nyxara, la Devoreuse d'etoiles (sanctuary of Aethra, the last guardian). B minor
darkened towards locrian (C and F naturals, the tritone), 105 BPM half-time feel, 4/4, 32 bars =
73.14 s (one 16th note = exactly 6300 samples).

Character: the void swallowing the light.  A heavy half-time groove with sub-bass drops, a
distorted pulsing bass, dissonant glass bells (tritones), a slow chromatic glass lead, dark 'oh'
choir, reversed swells and silences where everything is sucked in.

Form:  intro 1-4 (sub drops, bells, reversed swells)  | A 5-12 (half-time groove, pulsing bass)
       | B 13-24 (choir, glass lead + low brass, double-time hats)
       | breakdown 25-28 (the void: drone, bells, heartbeat)  | build 29-32.
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'nyxara_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 105, 4, 32, True

# name: (bass root, bell dyad (tritone colour), choir voicing)
CHORD = {
    'Bm': ('B1', ['B4', 'F5'], ['D4', 'F#4', 'B4']),
    'C':  ('C2', ['C5', 'F#5'], ['C4', 'E4', 'G4']),
    'G':  ('G1', ['G4', 'C#5'], ['D4', 'G4', 'B4']),
    'F#': ('F#1', ['F#4', 'C5'], ['C#4', 'F#4', 'A#4']),
    'Em': ('E2', ['E5', 'A#5'], ['E4', 'G4', 'B4']),
    'D':  ('D2', ['D5', 'G#5'], ['D4', 'F4', 'A4']),
    'A':  ('A1', ['A4', 'D#5'], ['C#4', 'E4', 'A4']),
}

PROG = (['Bm', 'C', 'Bm', 'C'] +                                     # 1-4 intro
        ['Bm', 'Bm', 'C', 'C', 'G', 'G', 'F#', 'F#'] +               # 5-12 A
        ['Em', 'C', 'Bm', 'Bm', 'G', 'A', 'F#', 'F#', 'Em', 'C', 'D', 'C'] +   # 13-24 B
        ['Bm', 'C', 'Bm', 'C'] +                                     # 25-28 breakdown
        ['G', 'G', 'F#', 'F#'])                                      # 29-32 build

LEAD_A = ('B5:3 C6 | B5:2 F5:2 | E5:3 F5 | G5:2 F#5:2 | D6:3 C6 | B5:2 G5:2 | A#5:3 C#6 | F#6:4 |')
LEAD_B = ('G5:2 B5 E6 | E6:2 D6 C6 | B5:3 F5 | F#5:4 | B5:2 D6 G6 | A5:2 C#6 E6 | F#6:2 E6 C#6 | A#5:4 | '
          'B5:2 G5 E5 | C6:3 B5 | A5:2 F5 D5 | C6:2 B5 F5 |')
PULSE = [(0, 0), (3, 0), (6, 12), (8, 0), (10, 0), (11, 1), (14, 0)]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=4.0, t60=3.0, predelay=0.03, ret=0.36, hf=0.4, lf=1.2)
    s.delay.update(beats=0.75, feedback=0.38, damp=2800.0, ret=0.24, to_reverb=0.4)
    s.duck_tau = 0.12

    bass = s.track('bass', gain_db=-6.0, duck=0.4, lowpass=2600, highpass=32)
    sub = s.track('sub', gain_db=-6.0, duck=0.2, lowpass=200, highpass=30)
    bells = s.track('bells', gain_db=-6.0, reverb=0.6, delay=0.3, highpass=400)
    lead = s.track('lead', gain_db=-5.0, reverb=0.5, delay=0.25, highpass=250)
    brass = s.track('brass', gain_db=-8.0, reverb=0.4, highpass=90)
    strings = s.track('strings', gain_db=-9.0, reverb=0.5, duck=0.25, highpass=100)
    choir = s.track('choir', gain_db=-3.0, reverb=0.65, duck=0.15, highpass=150)
    kick = s.track('kick', gain_db=-7.0, reverb=0.04)
    snare = s.track('snare', gain_db=-5.0, reverb=0.4)
    hats = s.track('hats', gain_db=1.0, reverb=0.12, pan=0.2, highpass=4500)
    taiko = s.track('taiko', gain_db=-6.0, reverb=0.35)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.4)
    fx = s.track('fx', gain_db=-9.0, reverb=0.45)

    for bar, nb, name in blocks(PROG):
        root, dyad, ch_v = CHORD[name]
        r = note_midi(root)
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r - 12 if r - 12 >= 24 else r, vel=0.7, attack=0.3, release=0.8,
                 drive=1.2)
        if 13 <= bar <= 28:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.7, vowel='o', attack=0.3, release=1.0)
        if bar >= 5:
            strings.chord(I.strings_legato, bar, 0, nb * 4 - 0.2, [r + 24, r + 31], vel=0.55, attack=0.6, release=0.8)

    for bar, name in enumerate(PROG, start=1):
        root, dyad, ch_v = CHORD[name]
        r = note_midi(root)
        # distorted pulsing bass (A, B, build)
        if 5 <= bar <= 24 or bar >= 29:
            for step, iv in PULSE:
                bass.note(T.bass_forge, bar, step * 0.25, 0.45 if step in (0, 8) else 0.25, r + iv,
                          vel=0.95 if step in (0, 8) else 0.72, peak_mul=4.0, drive=3.4)
        # dissonant bells: the tritone dyad on beats 1 and 3 (sparser in A)
        for beat in ((0, 2) if (bar <= 4 or 25 <= bar <= 28 or bar >= 13) else (0,)):
            bells.chord(T.ice_bell, bar, beat, 2, dyad, vel=0.5 if beat else 0.6, decay=2.2)

    lead.line(I.lead_glass, 5, LEAD_A, vel=0.85, legato=0.96, vib=10.0)
    lead.line(I.lead_glass, 13, LEAD_B, vel=0.95, legato=0.96, vib=10.0)
    brass.line(T.brass_low, 13, LEAD_B, vel=0.6, legato=0.92, transpose=-24, bright=2.6, max_cut=1800.0, growl=0.25)
    lead.line(I.lead_glass, 29, 'G5:2 B5 D6 | E6:4 | F#6:2 E6 C6 | A#5:4 |', vel=0.9, legato=0.96, vib=10.0)

    # ---- drums: half time
    kk = D.kick(f_start=150, f_end=40, pitch_tau=0.035, decay=0.38, click=0.3, drive=2.4, variant=51)
    sn = D.snare(tone=170, tone_decay=0.09, noise_decay=0.22, noise_lp=7000, tone_mix=0.7, roomy=0.5, variant=51)
    cl = D.clap(variant=5)
    hh = D.hat(decay=0.03, length=0.1, bright=1.1, metal=0.6, variant=51)
    tk = D.taiko(55.0, variant=51)
    beat_h = P.sub_hit(42.0, length=0.6, variant=52)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X...............', duck=True)
            if bar >= 3:
                hats.hits(hh, bar, '..x...x...x...x.', vel=0.5)
            continue
        if 25 <= bar <= 28:
            fx.hits(beat_h, bar, 'x..x............', vel=0.9)
            hats.hits(hh, bar, '........x.......', vel=0.4)
            continue
        if bar >= 29:
            kick.hits(kk, bar, 'x.....x...x.....', duck=True)
            roll = {29: 'o...o...o...o...', 30: 'o.o.o.o.o.o.o.o.', 31: 'oooooooooooooooo', 32: 'xxxxxxxxXXXXXXXX'}[bar]
            snare.hits(sn, bar, roll, vel=0.6 + 0.08 * (bar - 29))
            hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.', vel=0.6)
            continue
        B = bar >= 13
        kick.hits(kk, bar, 'x.....x...x.....' if not B else 'x.....x.x.x...x.', duck=True)
        snare.hits(sn, bar, '........x.......')
        snare.hits(cl, bar, '........x.......', vel=0.5)
        hats.hits(hh, bar, 'x.xgx.xgx.xgx.xg' if not B else 'xgxxxgxxxgxxxgxx', vel=0.7)
        if B and bar % 2 == 1:
            taiko.hits(tk, bar, 'X.......x.......', vel=0.85)
    snare.hits(sn, 12, '........o.o.xxXX', vel=0.8)
    snare.hits(sn, 24, '........oooxxxXX', vel=0.85)
    for bar in (1, 25):
        fx.at(P.sub_hit(40.0, length=2.5, variant=bar + 50), bar, 0, gain=1.2)
    for bar in (4, 12, 28):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=150, f1=6000, q=3.0, variant=51), 31, 0, gain=0.9)
    for bar in (5, 13, 21, 29):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.boom(42.0, variant=51), 13, 0, gain=0.9)
    return s
