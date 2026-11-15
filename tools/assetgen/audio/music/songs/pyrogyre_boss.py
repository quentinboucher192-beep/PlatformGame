"""pyrogyre_boss - Pyrogyre, l'Anneau solaire (sanctuary of Aethra). A minor with a bright dorian
F# in the fanfare, 175 BPM, 4/4, 48 bars = 65.83 s (one 16th note = exactly 3780 samples).

Character: a blazing solar drive.  The 'orbit': a 16th-note arpeggio that climbs two octaves and
falls back like flames circling the eye, four-on-the-floor kick, saturated bass in octaves,
brass stabs and a heroic fanfare on brass and choir; half-time 'supernova' breakdown.

Form:  intro 1-4 (orbit arpeggio alone, taiko, riser)
       | A 5-20 (drive: octave bass, orbit, stabs; fanfare from bar 13)
       | B 21-36 (choir + high brass theme, double-time hats)
       | breakdown 37-40 (half time: sub hits and the orbit slowed)  | build 41-48.
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'pyrogyre_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 175, 4, 48, True

# name: (bass root, stab voicing, arpeggio chord tones (root, third, fifth), choir voicing)
CHORD = {
    'Am': ('A1', ['A3', 'C4', 'E4'], ['A4', 'C5', 'E5'], ['C4', 'E4', 'A4']),
    'F':  ('F1', ['F3', 'A3', 'C4'], ['F4', 'A4', 'C5'], ['C4', 'F4', 'A4']),
    'C':  ('C2', ['G3', 'C4', 'E4'], ['C5', 'E5', 'G5'], ['C4', 'E4', 'G4']),
    'G':  ('G1', ['G3', 'B3', 'D4'], ['G4', 'B4', 'D5'], ['B3', 'D4', 'G4']),
    'E':  ('E2', ['G#3', 'B3', 'E4'], ['E4', 'G#4', 'B4'], ['B3', 'E4', 'G#4']),
    'Dm': ('D2', ['F3', 'A3', 'D4'], ['D4', 'F4', 'A4'], ['D4', 'F4', 'A4']),
    'D':  ('D2', ['F#3', 'A3', 'D4'], ['D4', 'F#4', 'A4'], ['D4', 'F#4', 'A4']),
    'Em': ('E2', ['G3', 'B3', 'E4'], ['E4', 'G4', 'B4'], ['B3', 'E4', 'G4']),
}

PROG = (['Am', 'Am', 'F', 'G'] +                                     # 1-4 intro
        ['Am', 'F', 'C', 'G', 'Am', 'F', 'C', 'G'] +                 # 5-12 A1
        ['Am', 'F', 'G', 'E', 'Dm', 'F', 'E', 'E'] +                 # 13-20 A2
        ['F', 'G', 'Am', 'Am', 'F', 'G', 'E', 'E'] +                 # 21-28 B1
        ['D', 'Em', 'F', 'G', 'Am', 'G', 'F', 'E'] +                 # 29-36 B2
        ['Am', 'Am', 'F', 'F'] +                                     # 37-40 breakdown
        ['Dm', 'Dm', 'F', 'F', 'E', 'E', 'E', 'E'])                  # 41-48 build

FANFARE = ('A4:1.5 C5:.5 E5 A5 | G5:1.5 E5:.5 D5:2 | C5:1.5 D5:.5 E5 G5 | B4:4 | '
           'A4:1.5 C5:.5 D5 F5 | A5:2 G5 F5 | E5:1.5 F5:.5 G#5:2 | B5:4 |')
THEME_B = ('C6:1.5 A5:.5 F5 C6 | D6:1.5 B5:.5 G5:2 | E6:2 C6 A5 | A5:4 | '
           'C6:1.5 A5:.5 F5 A5 | B5:2 D6:2 | B5:1.5 G#5:.5 E5 B5 | E6:4 | '
           'F#5:1.5 A5:.5 D6 F#6 | E6:2 B5:2 | C6:1.5 A5:.5 F5 C6 | D6:2 B5:2 | '
           'E6:1.5 C6:.5 A5 E6 | D6:2 B5 G5 | C6:1.5 A5:.5 F5 A5 | G#5:4 |')
# the orbit: up two octaves through the triad then back down (16 steps)
ORBIT = [0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1]


def _orbit_notes(tones):
    base = [note_midi(t) for t in tones]
    seq = []
    for octv in range(3):
        for b in base:
            seq.append(b + 12 * octv)
    return seq  # 9 notes: three octaves of the triad


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=2.6, t60=1.8, predelay=0.015, ret=0.26, hf=0.4, lf=1.1)
    s.delay.update(beats=0.75, feedback=0.3, damp=3500.0, ret=0.22, to_reverb=0.3)
    s.duck_tau = 0.075

    bass = s.track('bass', gain_db=-7.0, duck=0.4, lowpass=3200, highpass=38)
    orbit = s.track('orbit', gain_db=-6.0, reverb=0.25, delay=0.18, duck=0.25, highpass=250)
    stab = s.track('stabs', gain_db=-7.0, reverb=0.25, highpass=120)
    lead = s.track('brass', gain_db=-6.0, reverb=0.35, delay=0.12, highpass=150)
    horn = s.track('horn', gain_db=-6.0, reverb=0.3, delay=0.08, highpass=90)
    choir = s.track('choir', gain_db=-3.0, reverb=0.55, duck=0.15, highpass=170)
    anv = s.track('anvil', gain_db=1.0, reverb=0.3, delay=0.15, highpass=300)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    snare = s.track('snare', gain_db=-6.0, reverb=0.25)
    hats = s.track('hats', gain_db=2.0, reverb=0.1, pan=0.2, highpass=4000)
    taiko = s.track('taiko', gain_db=-7.0, reverb=0.28)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.3)
    fx = s.track('fx', gain_db=-11.0, reverb=0.35)

    for bar, nb, name in blocks(PROG):
        root, stabs, tones, ch_v = CHORD[name]
        if 21 <= bar <= 40:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.68, vowel='a' if bar <= 36 else 'o',
                        attack=0.2, release=0.7)
        if bar <= 4 or 37 <= bar <= 40:
            horn.chord(T.brass_low, bar, 0, nb * 4 - 0.2, stabs, vel=0.5, attack=0.3, release=0.6, bright=2.4,
                       max_cut=1600.0, drive=1.6, vib=0.0)

    for bar, name in enumerate(PROG, start=1):
        root, stabs, tones, ch_v = CHORD[name]
        r = note_midi(root)
        notes = _orbit_notes(tones)
        half = 37 <= bar <= 40
        # bass: octave drive (8ths), sub hits in the breakdown, 16ths in the build
        if bar <= 4:
            bass.note(I.bass_sub, bar, 0, 3.8, r, vel=0.85, attack=0.02, release=0.3, drive=1.6)
        elif half:
            bass.note(I.bass_sub, bar, 0, 1.8, r, vel=0.95, attack=0.01, release=0.2, drive=1.8)
            bass.note(I.bass_sub, bar, 2.5, 1.3, r, vel=0.8, attack=0.01, release=0.2, drive=1.8)
        else:
            step_n = 16 if bar >= 41 else 8
            for k in range(step_n):
                b = k * (4.0 / step_n)
                bass.note(T.bass_forge, bar, b, 0.2 if step_n == 16 else 0.4, r + (12 if k % 2 else 0),
                          vel=0.9 if k % 4 == 0 else 0.72, peak_mul=5.5, drive=2.6)
        # the orbit arpeggio
        if half:
            for k in range(8):
                orbit.note(T.glass_pluck, bar, k * 0.5, 0.5, notes[ORBIT[k * 2 % 16]], vel=0.65, decay=0.5,
                           pan=(-0.4 if k % 2 else 0.4))
        else:
            for k in range(16):
                orbit.note(I.bass_pluck, bar, k * 0.25, 0.22, notes[ORBIT[k]] + 12, vel=0.72 if k % 4 == 0 else 0.55,
                           peak=9.0, floor=2.2, tau=0.05, q=1.6, sub=0.0, drive=1.2, shape='saw', detune=8.0,
                           pan=(-0.35 if k % 2 else 0.35))
        # brass stabs
        if 5 <= bar <= 20 or bar >= 41:
            for beat in (0, 0.75, 2.5):
                stab.chord(T.brass_stab, bar, beat, 0.3, [note_midi(x) + 12 for x in stabs], vel=0.85 if beat == 0 else 0.7)
        elif 21 <= bar <= 36:
            stab.chord(T.brass_stab, bar, 0, 0.5, [note_midi(x) + 12 for x in stabs], vel=0.8)
        # anvil sparks on the off-beats
        if 5 <= bar <= 36 and bar % 2 == 0:
            for st in (6, 14):
                anv.note(T.anvil, bar, st * 0.25, 0.25, note_midi(tones[2]) + 12, vel=0.45, decay=0.5,
                         pan=(0.4 if st == 6 else -0.4))

    horn.line(T.brass_low, 13, FANFARE, vel=0.9, legato=0.92, bright=3.8, max_cut=3600.0)
    horn.line(T.brass_low, 13, FANFARE, vel=0.5, legato=0.92, transpose=-12, bright=2.6, max_cut=1500.0)
    lead.line(I.lead_brass, 21, THEME_B, vel=0.9, legato=0.92, max_cut=5000.0)
    horn.line(T.brass_low, 21, THEME_B, vel=0.5, legato=0.92, transpose=-12, bright=3.0, max_cut=2400.0)
    horn.line(T.brass_low, 41, 'D5:2 F5:2 | A5:3 G5 | F5:2 E5 D5 | C5:4 | B4:1.5 C5:.5 D5 E5 | '
              'G#5:4 | B5:4 | E5:4 |', vel=0.9, legato=0.92, bright=3.8, max_cut=3600.0)

    # ---- drums
    kk = D.kick(f_start=170, f_end=48, pitch_tau=0.026, decay=0.24, click=0.45, drive=2.3, variant=41)
    sn = D.snare(tone=185, tone_decay=0.06, noise_decay=0.14, noise_lp=9000, tone_mix=0.65, roomy=0.3, variant=41)
    cl = D.clap(variant=4)
    hh = P.metal_hat(decay=0.026, length=0.09, variant=4)
    tk = D.taiko(64.0, variant=41)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X..x..x.X..x..x.', vel=0.85, duck=True)
            if bar >= 3:
                hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.', vel=0.6)
            continue
        if 37 <= bar <= 40:
            kick.hits(kk, bar, 'x.........x.....', duck=True)
            snare.hits(sn, bar, '........x.......')
            hats.hits(hh, bar, 'x...x...x...x...', vel=0.5)
            continue
        if bar >= 41:
            kick.hits(kk, bar, 'x...x...x...x...', duck=True)
            roll = 'o.o.o.o.o.o.o.o.' if bar <= 44 else 'oooooooooooooooo' if bar <= 46 else 'xxxxxxxxXXXXXXXX'
            snare.hits(sn, bar, roll, vel=0.55 + 0.05 * (bar - 41))
            hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.', vel=0.7)
            continue
        B = bar >= 21
        kick.hits(kk, bar, 'x...x...x...x...' if not B else 'x...x...x...x.x.', duck=True)
        snare.hits(sn, bar, '....x.......x...')
        snare.hits(cl, bar, '....x.......x...', vel=0.55)
        hats.hits(hh, bar, '.x.x.x.x.x.x.x.x' if not B else 'xxxxxxxxxxxxxxxx', vel=0.8 if not B else 0.65)
        if B and bar % 4 == 1:
            taiko.hits(tk, bar, 'X.......x.......', vel=0.9)
    for bar in (12, 28):
        snare.hits(D.tom(175.0, variant=41), bar, '............x.x.', pan=0.3)
        snare.hits(D.tom(120.0, variant=41), bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 36, '........x.x.xxXX', vel=0.85)
    fx.at(P.sub_hit(50.0, variant=41), 1, 0, gain=1.1)
    fx.at(D.riser(s.spb * 4, f0=300, f1=8000, q=3.0, variant=41), 4, 0)
    fx.at(D.riser(s.spb * 8, f0=250, f1=9000, q=3.0, variant=42), 47, 0, gain=0.9)
    fx.at(D.reverse_cymbal(s.spb * 2), 20, 2.0)
    for bar in (5, 13, 21, 29, 41):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    for bar in (21, 37, 39):
        fx.at(P.sub_hit(48.0, variant=bar), bar, 0, gain=1.1)
    return s
