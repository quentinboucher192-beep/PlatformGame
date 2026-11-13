"""glacior_boss - Cryowyrm / Hydre de Givre / Glaciarque. B minor (harmonic: the F# major
dominant, Neapolitan C), 135 BPM, 4/4, 40 bars = 71.11 s (one 16th note = exactly 4900 samples).

Form:  intro 1-4 (frozen-clockwork glass ostinato, taiko, pedal bass, riser)
       | A 5-20 (galloping bass, spiccato strings, ice-bell stabs; from bar 13 the Glacior waltz
         theme returns in 4/4 on high strings + glass lead)
       | B 21-32 (G-D-A-Bm lift, choir, soaring theme, double-time hats and big taiko)
       | breakdown 33-36 (half time: the theme rung on glass bells over choir 'ooh')
       | build 37-40 (driving 8ths, snare roll and riser back into the intro).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks, air_layer
from synth import note_midi

NAME = 'glacior_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 135, 4, 40, True

# name: (bass, stab voicing, ostinato tones (root, fifth, third+, octave), choir voicing)
CHORD = {
    'Bm': ('B1', ['F#3', 'B3', 'D4', 'F#4'], ['B4', 'F#5', 'D6', 'B5'], ['D4', 'F#4', 'B4']),
    'G':  ('G1', ['G3', 'B3', 'D4', 'G4'], ['G4', 'D5', 'B5', 'G5'], ['D4', 'G4', 'B4']),
    'A':  ('A1', ['E3', 'A3', 'C#4', 'E4'], ['A4', 'E5', 'C#6', 'A5'], ['C#4', 'E4', 'A4']),
    'F#': ('F#1', ['F#3', 'A#3', 'C#4', 'F#4'], ['F#4', 'C#5', 'A#5', 'F#5'], ['C#4', 'F#4', 'A#4']),
    'Em': ('E2', ['E3', 'G3', 'B3', 'E4'], ['E5', 'B5', 'G5', 'E6'], ['E4', 'G4', 'B4']),
    'D':  ('D2', ['F#3', 'A3', 'D4', 'F#4'], ['D5', 'A5', 'F#5', 'D6'], ['D4', 'F#4', 'A4']),
    'C':  ('C2', ['G3', 'C4', 'E4', 'G4'], ['C5', 'G5', 'E6', 'C6'], ['C4', 'E4', 'G4']),
}

PROG = (['Bm', 'Bm', 'G', 'F#'] +                                    # 1-4 intro
        ['Bm', 'Bm', 'G', 'A', 'Bm', 'Bm', 'Em', 'F#'] +             # 5-12 A1
        ['Bm', 'Bm', 'G', 'A', 'Em', 'G', 'C', 'F#'] +               # 13-20 A2
        ['G', 'D', 'A', 'Bm', 'G', 'D', 'Em', 'F#'] +                # 21-28 B1
        ['G', 'A', 'C', 'F#'] +                                      # 29-32 B2
        ['Bm', 'Bm', 'G', 'G'] +                                     # 33-36 breakdown
        ['Em', 'C', 'F#', 'F#'])                                     # 37-40 build

THEME_A = ('D5 F#5 B5:2 | A5:3 F#5 | G5:1.5 F#5:.5 D5:2 | E5:2 C#5:2 | '
           'E5 G5 B5:2 | D6:2 B5 G5 | C6:1.5 B5:.5 G5 E5 | A#5:2 F#5 C#5 |')
THEME_B = ('B5:3 G5 | A5:2 F#5:2 | C#6:2 A5 E5 | B5:4 | '
           'D6:3 B5 | A5:1.5 B5:.5 A5 F#5 | G5:1.5 A5:.5 B5:2 | A#5:2 C#6:2 | '
           'D6:2 B5 G5 | C#6:2 A5 E5 | E6:1.5 D6:.5 C6 G5 | F#5:4 |')
BREAK_BELLS = 'B5:2 D6 F#6 | E6:4 | D6:2 B5 G5 | F#5:4 |'
BUILD = 'E5 G5 B5:2 | C6:3 B5 | A#5:4 | C#6:4 |'

GALLOP = [(0, 0), (2, 0), (3, 0), (4, 0), (6, 0), (7, 0), (8, 0), (10, 0), (11, 0),
          (12, 12), (14, 7), (15, 12)]
OST = [0, 1, 2, 1, 3, 1, 2, 1]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=3.6, t60=2.6, predelay=0.02, ret=0.32, hf=0.45, lf=1.0)
    s.delay.update(beats=0.75, feedback=0.32, damp=4500.0, ret=0.22, to_reverb=0.35)
    s.duck_tau = 0.09

    bass = s.track('bass', gain_db=-6.0, duck=0.38, lowpass=2400, highpass=35)
    clock = s.track('clockwork', gain_db=-5.0, reverb=0.3, delay=0.15, duck=0.15, highpass=400)
    spic = s.track('spiccato', gain_db=-3.0, reverb=0.25, duck=0.2, highpass=150)
    stab = s.track('stabs', gain_db=-6.0, reverb=0.35, highpass=150)
    hi_str = s.track('strings', gain_db=-6.0, reverb=0.4, delay=0.1, highpass=180)
    lead = s.track('lead', gain_db=-7.0, reverb=0.4, delay=0.18, highpass=250)
    low_str = s.track('low_strings', gain_db=-10.0, reverb=0.35, duck=0.3, highpass=60)
    choir = s.track('choir', gain_db=-3.0, reverb=0.6, duck=0.15, highpass=180)
    bells = s.track('bells', gain_db=-4.0, reverb=0.6, delay=0.25, highpass=400)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    snare = s.track('snare', gain_db=-5.0, reverb=0.3)
    hats = s.track('hats', gain_db=1.0, reverb=0.12, pan=0.2)
    taiko = s.track('taiko', gain_db=-7.0, reverb=0.3)
    cym = s.track('cymbals', gain_db=-11.0, reverb=0.35)
    fx = s.track('fx', gain_db=-10.0, reverb=0.4)
    air = s.track('air', gain_db=-24.0, reverb=0.2)

    for bar, nb, name in blocks(PROG):
        root, voicing, _, ch_v = CHORD[name]
        if bar >= 21 and not (bar >= 37):
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.68,
                        vowel='u' if 33 <= bar <= 36 else 'a', attack=0.2, release=0.8)
        if 21 <= bar <= 36 or bar <= 4:
            low_str.chord(I.strings_legato, bar, 0, nb * 4 - 0.2,
                          [note_midi(root) + 12, note_midi(root) + 19], vel=0.7, attack=0.1,
                          release=0.4)

    for bar, name in enumerate(PROG, start=1):
        root, voicing, tones, _ = CHORD[name]
        r = note_midi(root)
        # bass
        if bar <= 4:
            for k in range(8):
                bass.note(I.bass_pluck, bar, k * 0.5, 0.4, r, vel=0.8 if k % 2 == 0 else 0.6,
                          peak=4.0, floor=1.5, tau=0.08, q=1.2, sub=0.75, drive=1.6, detune=8.0)
        elif 33 <= bar <= 36:
            bass.note(I.bass_sub, bar, 0, 3.9, r, vel=0.9, attack=0.02, release=0.5)
        elif bar >= 37:
            for k in range(8):
                bass.note(I.bass_pluck, bar, k * 0.5, 0.4, r + (12 if k % 4 == 3 else 0),
                          vel=0.85 if k % 2 == 0 else 0.7, peak=6.0, floor=1.6, tau=0.07, q=1.3,
                          sub=0.6, drive=2.0, detune=9.0)
        else:
            for step, iv in GALLOP:
                bass.note(I.bass_pluck, bar, step * 0.25, 0.22 if step % 4 else 0.4, r + iv,
                          vel=0.9 if step % 4 == 0 else 0.7, peak=6.0, floor=1.6, tau=0.07,
                          q=1.3, sub=0.6, drive=2.0, detune=9.0)
        # frozen clockwork: glass plucks in 16ths (not in the breakdown)
        if not 33 <= bar <= 36:
            for k in range(16):
                v = 0.62 if k % 4 == 0 else 0.42
                clock.note(T.glass_pluck, bar, k * 0.25, 0.25, tones[OST[k % 8]], vel=v,
                           decay=0.22, pan=(-0.5 if k % 2 else 0.5))
        # spiccato strings an octave+ below
        if 5 <= bar <= 32 or bar >= 37:
            for k in range(16):
                v = 0.75 if k % 4 == 0 else 0.52
                spic.note(I.strings_stacc, bar, k * 0.25, 0.25, note_midi(tones[OST[k % 8]]) - 12,
                          vel=v, decay=0.07, pan=(-0.3 if k % 2 else 0.3))
        # stabs: strings + glass bell, 3-3-2 in A, downbeats in B
        if 5 <= bar <= 20 or bar >= 37:
            for beat in (0, 1.5, 3):
                stab.chord(I.strings_stacc, bar, beat, 0.4, voicing, vel=0.85 if beat == 0 else 0.7,
                           decay=0.12)
            bells.note(T.ice_bell, bar, 0, 1, note_midi(tones[0]), vel=0.45, decay=1.0, pan=-0.3)
        elif 21 <= bar <= 32:
            bells.note(T.ice_bell, bar, 0, 1, note_midi(tones[3]), vel=0.5, decay=1.4, pan=0.3)
            bells.note(T.ice_bell, bar, 2, 1, note_midi(tones[1]), vel=0.4, decay=1.0, pan=-0.3)

    # themes
    hi_str.line(I.strings_legato, 13, THEME_A, vel=0.8, legato=0.95, bright=4.5)
    lead.line(I.lead_glass, 13, THEME_A, vel=0.7, legato=0.95)
    hi_str.line(I.strings_legato, 13, THEME_A, vel=0.5, legato=0.95, transpose=-12, bright=3.5)
    hi_str.line(I.strings_legato, 21, THEME_B, vel=0.85, legato=0.95, bright=4.5)
    lead.line(I.lead_glass, 21, THEME_B, vel=0.75, legato=0.95)
    hi_str.line(I.strings_legato, 21, THEME_B, vel=0.55, legato=0.95, transpose=-12, bright=3.5)
    bells.line(T.ice_bell, 33, BREAK_BELLS, vel=0.8, decay=2.4)
    bells.line(T.ice_bell, 33, BREAK_BELLS, vel=0.4, transpose=-12, decay=2.0, pan=-0.3)
    hi_str.line(I.strings_legato, 37, BUILD, vel=0.85, legato=0.95, bright=4.5)
    lead.line(I.lead_glass, 37, BUILD, vel=0.6, legato=0.95)

    # ---- drums
    kk = D.kick(f_start=150, f_end=48, pitch_tau=0.03, decay=0.28, click=0.35, drive=1.9, variant=7)
    sn = D.snare(tone=190, tone_decay=0.06, noise_decay=0.17, noise_lp=9000, tone_mix=0.55,
                 roomy=0.45, variant=7)
    cl = D.clap(variant=2)
    ch = D.hat(decay=0.025, length=0.08, bright=1.1, metal=0.4, variant=7)
    tick = P.ice_tick(4400.0, variant=2)
    tk = D.taiko(64.0, variant=8)
    tk_hi = D.taiko(96.0, variant=9)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.....x.X.......' if bar < 4 else 'X.....x.X...x.xx', duck=True)
            taiko.hits(tk_hi, bar, '....o.......o...', vel=0.8, pan=0.3)
            hats.hits(tick, bar, 'x.o.x.o.x.o.x.o.', vel=0.7)
            continue
        if 33 <= bar <= 36:
            taiko.hits(tk, bar, 'X.......o.......', duck=True)
            hats.hits(tick, bar, 'o...o...o...o...', vel=0.6)
            continue
        if bar >= 37:
            kick.hits(kk, bar, 'x...x...x...x...', duck=True)
            roll = {37: 'g.g.g.g.o.o.o.o.', 38: 'o.o.o.o.oooooooo', 39: 'oooooooooooooooo',
                    40: 'xxxxxxxxXXXXXXXX'}[bar]
            snare.hits(sn, bar, roll, vel=0.5 + 0.12 * (bar - 37))
            hats.hits(ch, bar, 'x.x.x.x.x.x.x.x.', vel=0.7)
            continue
        A = bar <= 20
        kick.hits(kk, bar, ('x.....x.x.....x.' if bar < 13 else 'x..x..x.x..x..x.') if A
                  else 'x..xx..xx..xx..x', duck=True)
        snare.hits(sn, bar, '....x.......x...' if bar % 4 else '....x.......x.gg')
        snare.hits(cl, bar, '....x.......x...', vel=0.5)
        hats.hits(ch, bar, 'xgoxxgoxxgoxxgox' if A else 'xoxoxoxoxoxoxoxo', vel=0.85)
        if bar % 2 == 1:
            taiko.hits(tk, bar, 'X.......x.......' if not A else 'X...............', vel=0.85)
    for bar in (12, 28):
        snare.hits(D.tom(185.0, variant=7), bar, '............x.x.', pan=0.3)
        snare.hits(D.tom(125.0, variant=7), bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 32, '........x.x.xxXX', vel=0.85)
    fx.at(D.boom(52.0, variant=3), 1, 0)
    fx.at(D.riser(s.spb * 4, f0=500, f1=9000, q=3.0, variant=4), 4, 0)
    fx.at(D.riser(s.spb * 8, f0=400, f1=9000, q=3.0, variant=5), 39, 0, gain=0.9)
    fx.at(D.reverse_cymbal(s.spb * 2), 20, 2.0)
    for bar in (5, 13, 21, 25, 29, 37):
        cym.at(D.crash(variant=bar % 3 + 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    for bar in (21, 33):
        fx.at(D.boom(48.0, variant=bar), bar, 0, gain=0.8)

    air.at(air_layer(s.L, s.rng('air'), center=7000.0, low=3000.0, swells=4), 1, 0)
    return s
