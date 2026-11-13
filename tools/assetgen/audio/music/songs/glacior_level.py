"""glacior_level - 'Le sanctuaire gele': crystalline, airy, melancholic. F# minor (aeolian with a
Neapolitan G major colour), 84 BPM, 3/4 (a slow music-box waltz), 48 bars = 102.86 s (one 16th
note = exactly 7875 samples).

Form:  intro 1-4 (music-box arpeggios, frost pad, wind, ice ticks)
       | A 5-20 (A1: airy flute theme over celesta arps and a felt frame drum;
         A2: glass lead, the theme climbs to the Neapolitan Gmaj7)
       | B 21-36 (relative-major lift D-E-C#m, glass lead over legato strings, crystal plucks,
         soft choir and brushes)
       | A3 37-44 (the theme alone on the music box over strings - fragile)
       | outro 45-48 (arpeggios and bells, flows into the intro).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from dsp import highshelf
from music.textures import blocks, air_layer
from synth import note_midi

NAME = 'glacior_level'
BPM, BEATS_PER_BAR, BARS, LOOP = 84, 3, 48, True

# name: (bass, pad voicing, arp tones (root, fifth, ninth, tenth))
CHORD = {
    'F#m9':   ('F#1', ['A3', 'C#4', 'E4', 'G#4'], ['F#4', 'C#5', 'G#5', 'A5']),
    'Dmaj7':  ('D2', ['F#3', 'A3', 'C#4', 'E4'], ['D4', 'A4', 'E5', 'F#5']),
    'Dmaj9':  ('D2', ['F#3', 'A3', 'C#4', 'E4'], ['D4', 'A4', 'E5', 'F#5']),
    'A':      ('A1', ['E3', 'A3', 'C#4', 'E4'], ['A3', 'E4', 'B4', 'C#5']),
    'A/C#':   ('C#2', ['E3', 'A3', 'C#4', 'E4'], ['A3', 'E4', 'B4', 'C#5']),
    'E/G#':   ('G#1', ['E3', 'G#3', 'B3', 'E4'], ['E4', 'B4', 'F#5', 'G#5']),
    'E':      ('E2', ['E3', 'G#3', 'B3', 'E4'], ['E4', 'B4', 'F#5', 'G#5']),
    'Bm7':    ('B1', ['F#3', 'A3', 'D4', 'E4'], ['B3', 'F#4', 'C#5', 'D5']),
    'Bm9':    ('B1', ['D3', 'F#3', 'A3', 'C#4'], ['B3', 'F#4', 'C#5', 'D5']),
    'Gmaj7':  ('G1', ['F#3', 'B3', 'D4', 'F#4'], ['G3', 'D4', 'A4', 'B4']),
    'C#7sus': ('C#2', ['F#3', 'G#3', 'B3', 'F#4'], ['C#4', 'G#4', 'B4', 'F#5']),
    'C#7':    ('C#2', ['E#3', 'G#3', 'B3', 'E#4'], ['C#4', 'G#4', 'B4', 'E#5']),
    'C#m7':   ('C#2', ['E3', 'G#3', 'B3', 'E4'], ['C#4', 'G#4', 'D#5', 'E5']),
}

PROG = (['F#m9', 'F#m9', 'Dmaj7', 'C#7sus'] +                                   # 1-4 intro
        ['F#m9', 'F#m9', 'Dmaj7', 'Dmaj7', 'A', 'E/G#', 'Bm7', 'C#7'] +         # 5-12 A1
        ['F#m9', 'F#m9', 'Dmaj7', 'Dmaj7', 'Bm9', 'Gmaj7', 'C#7sus', 'C#7'] +   # 13-20 A2
        ['Dmaj9', 'E', 'C#m7', 'F#m9', 'Dmaj9', 'E', 'A', 'A/C#'] +             # 21-28 B1
        ['Bm9', 'C#m7', 'Dmaj9', 'E', 'Dmaj9', 'C#m7', 'Bm7', 'C#7'] +          # 29-36 B2
        ['F#m9', 'Dmaj7', 'A', 'E/G#', 'Bm9', 'Gmaj7', 'C#7sus', 'C#7'] +       # 37-44 A3
        ['F#m9', 'Dmaj7', 'Bm9', 'C#7sus'])                                     # 45-48 outro

THEME_A1 = ('A4 C#5 F#5 | E5:2 C#5 | D5:1.5 C#5:.5 A4 | C#5:2 r | '
            'C#5 E5 A5 | G#5:2 F#5:.5 E5:.5 | F#5:1.5 E5:.5 D5 | E#5:2 C#5 |')
THEME_A2 = ('A4 C#5 F#5 | A5:2 G#5 | F#5:1.5 E5:.5 C#5 | E5:3 | '
            'D5:1.5 C#5:.5 B4 | B4 D5 F#5 | F#5:2 B4 | E#5:1.5 C#5:.5 G#4 |')
THEME_B = ('F#5:2 A5 | G#5:2 B5 | E5:2 G#5 | F#5:3 | '
           'A5:2 C#6 | B5:1.5 A5:.5 G#5 | A5:2 E5 | E5 C#5 A4 | '
           'D5:1.5 E5:.5 F#5 | E5:2 G#5 | A5:2 F#5 | B5:1.5 G#5:.5 E5 | '
           'C#6:2 A5 | B5:1.5 G#5:.5 E5 | F#5:1.5 E5:.5 D5 | C#5 E#5 G#5 |')
THEME_A3 = ('A4 C#5 F#5 | E5:2 C#5 | C#5 E5 A5 | G#5:2 E5 | '
            'D5:1.5 C#5:.5 B4 | B4 D5 F#5 | F#5:2 B4 | E#5:3 |')

ARP = [0, 1, 2, 3, 2, 1]
BELLS = [(1, 'C#6'), (5, 'F#6'), (13, 'C#6'), (21, 'A6'), (25, 'F#6'), (29, 'D6'), (33, 'C#7'),
         (37, 'F#6'), (45, 'C#6'), (47, 'B5')]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-14.0)
    s.reverb.update(seconds=5.0, t60=3.8, predelay=0.035, ret=0.42, hf=0.45, lf=1.0)
    s.delay.update(beats=1.0, feedback=0.4, damp=4200.0, ret=0.3, to_reverb=0.45)
    s.duck_tau = 0.15

    pad = s.track('pad', gain_db=-12.0, reverb=0.55, duck=0.15, highpass=180,
                  eq=[highshelf(2500.0, 6.0)])
    bass = s.track('bass', gain_db=-11.0, lowpass=500, highpass=35)
    box = s.track('musicbox', gain_db=-1.5, reverb=0.4, delay=0.25, highpass=250)
    crys = s.track('crystal', gain_db=-3.0, reverb=0.45, delay=0.35, highpass=300)
    lead = s.track('lead', gain_db=-4.0, reverb=0.45, delay=0.25, highpass=220)
    bells = s.track('bells', gain_db=-3.0, reverb=0.8, delay=0.35, highpass=400)
    strings = s.track('strings', gain_db=-11.0, reverb=0.5, highpass=160)
    choir = s.track('choir', gain_db=-6.0, reverb=0.65, highpass=200)
    drum = s.track('drum', gain_db=-6.0, reverb=0.3)
    brushes = s.track('brushes', gain_db=-3.0, reverb=0.35, pan=-0.15)
    ticks = s.track('ticks', gain_db=-3.0, reverb=0.4, delay=0.2, pan=0.25)
    fx = s.track('fx', gain_db=-14.0, reverb=0.5)
    air = s.track('air', gain_db=-11.0, reverb=0.2)

    # ---- harmony
    for bar, nb, name in blocks(PROG):
        b, voicing, _ = CHORD[name]
        pad.chord(T.pad_frost, bar, 0, nb * 3 - 0.2, voicing, vel=0.75, attack=1.2, release=2.0)
        if bar >= 5:
            bass.note(I.bass_sub, bar, 0, nb * 3 - 0.3, b, vel=0.75, attack=0.15, release=0.8,
                      drive=1.0)
        if 21 <= bar <= 44:
            strings.chord(I.strings_legato, bar, 0, nb * 3 - 0.15, voicing[1:], vel=0.55,
                          attack=0.6, release=1.2, bright=3.0)
        if 21 <= bar <= 36:
            choir.chord(I.pad_choir, bar, 0, nb * 3 - 0.1, voicing[1:], vel=0.5, vowel='u',
                        attack=0.8, release=1.5)

    # ---- arpeggios: music box everywhere except B, crystal plucks in B
    for bar, name in enumerate(PROG, start=1):
        tones = CHORD[name][2]
        B = 21 <= bar <= 36
        quiet = bar <= 4 or bar >= 45 or 37 <= bar <= 44
        for k in range(6):
            nt = note_midi(tones[ARP[k]])
            v = (0.62 if k == 0 else 0.48) * (0.85 if quiet else 1.0)
            if B:
                crys.note(T.glass_pluck, bar, k * 0.5, 0.5, nt + 12 if k in (2, 3) else nt, vel=v,
                          decay=0.5, pan=(-0.4 if k % 2 else 0.4))
                if k in (0, 3):
                    box.note(I.celesta, bar, k * 0.5, 0.5, nt, vel=v * 0.5, pan=-0.2)
            else:
                box.note(I.celesta, bar, k * 0.5, 0.5, nt, vel=v, pan=(-0.3 if k % 2 else 0.3))

    # ---- melodies
    lead.line(I.lead_flute, 5, THEME_A1, vel=0.72, legato=0.96, vib=9.0, breath=0.09)
    bells.line(T.ice_bell, 5, THEME_A1, vel=0.3, decay=1.6, transpose=12)
    lead.line(I.lead_glass, 13, THEME_A2, vel=0.75, legato=0.97, vib=7.0)
    bells.line(T.ice_bell, 13, THEME_A2, vel=0.28, decay=1.6)
    lead.line(I.lead_glass, 21, THEME_B, vel=0.78, legato=0.97, vib=7.0)
    strings.line(I.strings_legato, 21, THEME_B, vel=0.45, legato=0.98, transpose=-12, bright=3.5)
    box.line(I.celesta, 37, THEME_A3, vel=0.75, transpose=12)
    box.line(T.ice_bell, 37, THEME_A3, vel=0.3, decay=1.8)
    for bar, nt in BELLS:
        bells.note(T.ice_bell, bar, 0, 3, nt, vel=0.55, decay=2.6, pan=0.35 if bar % 2 else -0.35)

    # ---- percussion: felt frame drum, brushes, glassy clockwork ticks
    fd = P.frame_drum(92.0)
    fd_hi = P.frame_drum(138.0, variant=1)
    br = P.brush()
    br2 = P.brush(variant=1, length=0.5, decay=0.16, center=2800.0)
    tk = P.ice_tick(3900.0)
    tk2 = P.ice_tick(5200.0, variant=1)
    sh = D.shaker(variant=4, center=6500.0)
    for bar in range(1, BARS + 1):
        intro = bar <= 4 or bar >= 45
        if intro:
            ticks.hits(tk, bar, 'o.....g.....' if bar % 2 else 'o.....g...g.', vel=0.8)
            continue
        B = 21 <= bar <= 36
        A3 = 37 <= bar <= 44
        drum.hits(fd, bar, 'x...........' if not B else 'x.......o...', duck=True)
        if bar % 4 == 0 and not A3:
            drum.hits(fd_hi, bar, '........o.o.', vel=0.7, pan=0.3)
        if A3:
            ticks.hits(tk, bar, 'o.g.o.g.o.g.', vel=0.7)
            continue
        brushes.hits(br, bar, '....x...x...' if B else '........x...', vel=0.8)
        if B:
            brushes.hits(br2, bar, '..g.......g.', vel=0.7)
            ticks.hits(tk2, bar, 'o.g.o.g.o.go', vel=0.8)
        else:
            ticks.hits(tk, bar, 'o...g...o...', vel=0.8)
        if bar >= 13:
            ticks.hits(sh, bar, 'g.g.g.g.g.g.', vel=0.6, pan=-0.4)
    for bar in (20, 36):
        fx.at(D.reverse_cymbal(s.spb * 3), bar, 0.0, gain=0.8)
    for bar in (21, 29):
        fx.at(D.crash(variant=2, decay=1.4), bar, 0, gain=0.35, pan=0.3)
    fx.at(D.riser(s.spb * 3, f0=1200, f1=9000, q=2.0, variant=3), 44, 0.0, gain=0.25)

    # ---- polar air: high hiss with slow swells, decorrelated in stereo
    air.at(air_layer(s.L, s.rng('air'), center=6500.0, low=2500.0, swells=6), 1, 0)
    return s
