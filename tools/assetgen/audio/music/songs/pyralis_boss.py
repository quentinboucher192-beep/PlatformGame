"""pyralis_boss - Magmaroth / Ignivore / Forgeron Primordial. D phrygian dominant (D Eb F# G A
Bb C), 150 BPM, 4/4, 40 bars = 64.0 s (one 16th note = exactly 4410 samples).

Form:  intro 1-4 (taiko + hammers, pedal bass, riser)
       | A 5-20 (galloping saturated bass, spiccato ostinato with the Eb grind, 3-3-2 brass stabs,
         anvil off-beats; from bar 13 the Pyralis horn motif returns, faster, in D)
       | B 21-32 (choir + soaring high brass over double-kick drums)
       | breakdown 33-36 (half time: huge anvil/sub hits, dark choir 'oh')
       | build 37-40 (straight 16th bass, snare roll and riser that slam back into the intro).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'pyralis_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 150, 4, 40, True

# name: (bass root, horn voicing, choir voicing, ostinato tones (root, fifth, colour, octave))
CHORD = {
    'D':  ('D2', ['D3', 'A3', 'D4'], ['D4', 'F#4', 'A4'], ['D4', 'A4', 'Eb5', 'D5']),
    'Eb': ('Eb2', ['Eb3', 'Bb3', 'Eb4'], ['Eb4', 'G4', 'Bb4'], ['Eb4', 'Bb4', 'G4', 'Eb5']),
    'Cm': ('C2', ['C3', 'G3', 'C4'], ['Eb4', 'G4', 'C5'], ['C4', 'G4', 'Eb4', 'C5']),
    'Gm': ('G1', ['G2', 'D3', 'G3'], ['D4', 'G4', 'Bb4'], ['G3', 'D4', 'Bb4', 'G4']),
    'Bb': ('Bb1', ['Bb2', 'F3', 'Bb3'], ['D4', 'F4', 'Bb4'], ['Bb3', 'F4', 'D5', 'Bb4']),
}

PROG = (['D', 'D', 'Eb', 'D'] +                                     # 1-4 intro
        ['D', 'D', 'Eb', 'D', 'Gm', 'Eb', 'Cm', 'D'] +              # 5-12 A1
        ['D', 'D', 'Eb', 'D', 'Gm', 'Bb', 'Eb', 'Eb'] +             # 13-20 A2
        ['Gm', 'Eb', 'Bb', 'D', 'Gm', 'Eb', 'Cm', 'D'] +            # 21-28 B1
        ['Eb', 'Cm', 'Eb', 'D'] +                                   # 29-32 B2
        ['D', 'Eb', 'D', 'Eb'] +                                    # 33-36 breakdown
        ['Gm', 'Cm', 'Eb', 'Eb'])                                   # 37-40 build

MOTIF = (
    'D4:1.5 Eb4:.5 F#4 A4 | G4:1.5 F#4:.5 Eb4:2 | Eb4:1.5 G4:.5 Bb4 C5 | A4:4 | '
    'D5:1.5 C5:.5 Bb4 G4 | Bb4:1.5 C5:.5 D5 F5 | Eb5:1.5 D5:.5 C5 Bb4 | C5:1.5 Bb4:.5 A4:2 |'
)
THEME_B = (
    'D5:1.5 G5:.5 Bb5:2 | Bb5:1.5 G5:.5 Eb5:2 | F5:1.5 G5:.5 F5 D5 | F#5:4 | '
    'G5:1.5 A5:.5 Bb5 D6 | C6:1.5 Bb5:.5 G5:2 | G5:1.5 Eb5:.5 C5 Eb5 | F#5:2 A5:2 | '
    'Bb5:1.5 C6:.5 Bb5 G5 | Eb5:1.5 G5:.5 C6:2 | Bb5:2 G5 Eb5 | A5:2 F#5 D5 |'
)
BREAK_ANVIL = 'D5:3 Eb5 | D5:4 | A5:3 Bb5 | A5:2 Eb5:2 |'

GALLOP = [(0, 0), (2, 0), (3, 0), (4, 0), (6, 0), (7, 0), (8, 0), (10, 0), (11, 0), (12, 12),
          (14, 1), (15, 0)]
DRIVE = [(0, 0), (2, 0), (4, 12), (6, 0), (8, 0), (10, 0), (12, 12), (14, 7)]
SPIC = [0, 1, 2, 1, 3, 1, 2, 1]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=2.8, t60=1.9, predelay=0.018, ret=0.28, hf=0.35, lf=1.2)
    s.delay.update(beats=0.75, feedback=0.28, damp=2800.0, ret=0.2, to_reverb=0.3)
    s.duck_tau = 0.085

    bass = s.track('bass', gain_db=-7.0, duck=0.38, lowpass=3000, highpass=40)
    spic = s.track('spiccato', gain_db=-3.0, reverb=0.25, duck=0.2, highpass=160)
    stab = s.track('stabs', gain_db=-7.0, reverb=0.28, highpass=120)
    horn = s.track('horn', gain_db=-5.0, reverb=0.3, delay=0.08, highpass=90)
    lead = s.track('lead', gain_db=-7.0, reverb=0.35, delay=0.14, highpass=150)
    drone = s.track('drone', gain_db=-12.0, reverb=0.4, duck=0.3, highpass=60)
    choir = s.track('choir', gain_db=-3.0, reverb=0.55, duck=0.15, highpass=170)
    anv = s.track('anvil', gain_db=-5.0, reverb=0.3, delay=0.15, highpass=300)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    snare = s.track('snare', gain_db=-6.0, reverb=0.25)
    forge = s.track('forge', gain_db=-6.0, reverb=0.28, highpass=60)
    hats = s.track('hats', gain_db=3.0, reverb=0.1, pan=0.2, highpass=4000)
    taiko = s.track('taiko', gain_db=-7.0, reverb=0.28)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.3)
    fx = s.track('fx', gain_db=-11.0, reverb=0.35)

    for bar, nb, name in blocks(PROG):
        root, horns, ch_v, _ = CHORD[name]
        if bar <= 4 or bar >= 33:
            drone.chord(T.brass_low, bar, 0, nb * 4 - 0.2, horns, vel=0.55, attack=0.25, release=0.6,
                        bright=2.2, max_cut=1500.0, drive=1.6, vib=0.0, growl=0.15)
        if 21 <= bar <= 36:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.68,
                        vowel='o' if bar >= 33 else 'a', attack=0.2, release=0.7)

    for bar, name in enumerate(PROG, start=1):
        root, horns, ch_v, tones = CHORD[name]
        r = note_midi(root)
        # bass
        if bar <= 4:
            for k in range(8):
                bass.note(T.bass_forge, bar, k * 0.5, 0.3, r, vel=0.85 if k % 2 == 0 else 0.65,
                          peak_mul=4.5, drive=2.6)
        elif 33 <= bar <= 36:
            bass.note(I.bass_sub, bar, 0, 3.9, r, vel=0.95, attack=0.01, release=0.3, drive=1.8)
        elif bar >= 37:
            for k in range(16):
                bass.note(T.bass_forge, bar, k * 0.25, 0.2, r + (12 if k % 4 == 3 else 0),
                          vel=0.6 + 0.3 * (k % 4 == 0) + 0.1 * (bar - 37) / 3, peak_mul=6.0, drive=3.2)
        else:
            pat = GALLOP if bar <= 20 else DRIVE
            for step, iv in pat:
                bass.note(T.bass_forge, bar, step * 0.25, 0.4 if step % 4 == 0 else 0.2, r + iv,
                          vel=0.92 if step % 4 == 0 else 0.72, peak_mul=6.5, drive=3.2)
        # spiccato 16ths (A, B, build)
        if 5 <= bar <= 32 or bar >= 37:
            for k in range(16):
                v = (0.78 if k % 4 == 0 else 0.56) * (0.9 if bar <= 12 else 1.0)
                spic.note(I.strings_stacc, bar, k * 0.25, 0.25, tones[SPIC[k % 8]], vel=v,
                          decay=0.065, pan=(-0.35 if k % 2 else 0.35))
        # brass stabs 3-3-2
        if 5 <= bar <= 20 or bar >= 37:
            for beat in (0, 1.5, 3):
                stab.chord(T.brass_stab, bar, beat, 0.32, [note_midi(x) + 12 for x in horns],
                           vel=0.88 if beat == 0 else 0.7)
        elif 21 <= bar <= 32:
            stab.chord(T.brass_stab, bar, 0, 0.5, [note_midi(x) + 12 for x in horns], vel=0.8)
        # anvil off-beats
        if 5 <= bar <= 32:
            for st, ti in ((2, 0), (6, 1), (10, 0), (14, 3)):
                anv.note(T.anvil, bar, st * 0.25, 0.25, note_midi(tones[ti]) + 12,
                         vel=0.5 if st in (2, 10) else 0.38, decay=0.55,
                         pan=(-0.35 if st in (2, 10) else 0.35))

    # themes
    horn.line(T.brass_low, 13, MOTIF, vel=0.9, legato=0.92, bright=3.8, max_cut=3400.0)
    horn.line(T.brass_low, 13, MOTIF, vel=0.5, legato=0.92, transpose=-12, bright=2.6,
              max_cut=1500.0)
    lead.line(I.lead_brass, 21, THEME_B, vel=0.88, legato=0.92, max_cut=4500.0)
    horn.line(T.brass_low, 21, THEME_B, vel=0.55, legato=0.92, transpose=-12, bright=3.0,
              max_cut=2400.0)
    anv.line(T.anvil, 33, BREAK_ANVIL, vel=0.85, decay=1.6, pan=0.1)
    anv.line(T.anvil, 33, BREAK_ANVIL, vel=0.45, transpose=-12, decay=1.2, pan=-0.2)
    horn.line(T.brass_low, 37, 'D4:1.5 Eb4:.5 F#4 A4 | G4:1.5 F#4:.5 Eb4:2 | '
              'Eb4:1 G4:1 Bb4:1 C5:1 | Eb5:2 D5:2 |', vel=0.9, legato=0.92, bright=3.8,
              max_cut=3400.0)

    # ---- drums
    kk = D.kick(f_start=165, f_end=47, pitch_tau=0.028, decay=0.26, click=0.4, drive=2.2, variant=6)
    sn = D.snare(tone=175, tone_decay=0.07, noise_decay=0.15, noise_lp=8000, tone_mix=0.7,
                 roomy=0.3, variant=6)
    hh = P.metal_hat(decay=0.028, length=0.1, variant=2)
    cl = P.clang(1320.0, decay=0.3, variant=1)
    hm = P.hammer(variant=1, freq=900.0)
    tk = D.taiko(60.0, variant=6)
    tk_hi = D.taiko(90.0, variant=7)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X..x..x.X...x.x.', vel=0.8, duck=True)
            taiko.hits(tk_hi, bar, '....o.....o....o', vel=0.8, pan=0.3)
            forge.hits(hm, bar, '....x.......x...', vel=0.85)
            if bar >= 3:
                hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.', vel=0.6)
            continue
        if 33 <= bar <= 36:
            taiko.hits(tk, bar, 'X.......x.......', duck=True)
            forge.hits(hm, bar, '........X.......', vel=0.9)
            hats.hits(hh, bar, 'x...x...x...x...', vel=0.45)
            continue
        if bar >= 37:
            kick.hits(kk, bar, 'x...x...x...x...', duck=True)
            roll = {37: 'g.g.g.g.o.o.o.o.', 38: 'o.o.o.o.oooooooo', 39: 'oooooooooooooooo',
                    40: 'xxxxxxxxXXXXXXXX'}[bar]
            snare.hits(sn, bar, roll, vel=0.55 + 0.12 * (bar - 37))
            hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.', vel=0.7)
            continue
        A = bar <= 20
        if A:
            kick.hits(kk, bar, 'x.xxx.xxx.xxx.xx' if bar >= 13 else 'x.x.x...x.x.x..x', duck=True)
            if bar <= 12:
                taiko.hits(tk, bar, 'X.......X.......', vel=0.75)
        else:
            kick.hits(kk, bar, 'x..xx..xx..xx..x', duck=True)
        snare.hits(sn, bar, '....x.......x...' if bar % 4 else '....x.......x.xx')
        forge.hits(cl, bar, '....x.......x...', vel=0.7)
        hats.hits(hh, bar, 'xgoxxgoxxgoxxgox' if A else 'xoxoxoxoxoxoxoxo', vel=0.85)
        if not A and bar % 2 == 1:
            taiko.hits(tk, bar, 'X.......x.......', vel=0.9)
    for bar in (12, 28):
        snare.hits(D.tom(170.0, variant=6), bar, '............x.x.', pan=0.3)
        snare.hits(D.tom(115.0, variant=6), bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 32, '........x.x.xxXX', vel=0.85)
    fx.at(P.sub_hit(50.0, variant=1), 1, 0, gain=1.2)
    fx.at(D.riser(s.spb * 4, f0=300, f1=7000, q=3.0), 4, 0)
    fx.at(D.riser(s.spb * 8, f0=250, f1=8000, q=3.0, variant=2), 39, 0, gain=0.9)
    fx.at(D.reverse_cymbal(s.spb * 2), 20, 2.0)
    for bar in (5, 13, 21, 25, 29, 37):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    for bar in (21, 33):
        fx.at(P.sub_hit(48.0, variant=bar), bar, 0, gain=1.1)
    for bar, beat in ((9, 2.0), (25, 0.0), (35, 2.0)):
        fx.at(P.chain(0.6, 10, variant=bar + 50), bar, beat, gain=1.2, pan=0.5 if bar % 2 else -0.5)
    for bar in (17, 34):
        fx.at(P.steam(1.0, variant=bar + 50), bar, 0.0, gain=0.8, pan=0.45)
    return s
