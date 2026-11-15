"""aethra_level - Aethra, the planet of the sanctuaries (2.19). D lydian (the bright G#, the
E-over-D colour chord, a borrowed C for mystery), 126 BPM, 4/4, 52 bars = 99.05 s (one 16th note =
exactly 5250 samples).

Character: wonder and quest among islands floating on a nebula - violet and gold.  A celesta and
marimba arpeggio that drifts like islands, an ocarina theme, warm strings and choir, light hand
percussion; the breakdown visits the four elements in turn (roots: marimba, frost: glass bells,
magma: struck anvils, void: glassy lead) before the theme returns.

Form:  intro 1-4 (celesta arpeggio, pad)  | A 5-20 (groove, ocarina theme)
       | B 21-36 (strings + brass counter-theme, choir, fuller drums)
       | C 37-44 (the four elements, two bars each)  | A' 45-52 (theme, back to the intro).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'aethra_level'
BPM, BEATS_PER_BAR, BARS, LOOP = 126, 4, 52, True

# name: (bass root, pad voicing, arpeggio tones (4))
CHORD = {
    'D':   ('D2', ['F#3', 'A3', 'D4', 'E4'], ['D5', 'F#5', 'A5', 'E6']),
    'E_D': ('D2', ['E3', 'G#3', 'B3', 'D4'], ['E5', 'G#5', 'B5', 'D6']),
    'Bm':  ('B1', ['F#3', 'B3', 'D4', 'A4'], ['B4', 'D5', 'F#5', 'C#6']),
    'G':   ('G1', ['G3', 'B3', 'D4', 'F#4'], ['G4', 'B4', 'D5', 'F#5']),
    'A':   ('A1', ['E3', 'A3', 'C#4', 'E4'], ['A4', 'C#5', 'E5', 'B5']),
    'F#m': ('F#1', ['F#3', 'A3', 'C#4', 'E4'], ['F#4', 'A4', 'C#5', 'E5']),
    'Em':  ('E2', ['E3', 'G3', 'B3', 'D4'], ['E5', 'G5', 'B5', 'F#6']),
    'C':   ('C2', ['E3', 'G3', 'C4', 'D4'], ['C5', 'E5', 'G5', 'D6']),
}

A_PROG = ['D', 'E_D', 'Bm', 'G', 'D', 'E_D', 'A', 'A', 'Bm', 'F#m', 'G', 'D', 'Em', 'G', 'A', 'A']
B_PROG = ['G', 'A', 'F#m', 'Bm', 'G', 'A', 'D', 'D', 'Em', 'C', 'G', 'D', 'Em', 'F#m', 'G', 'A']
C_PROG = ['Bm', 'G', 'D', 'A', 'Bm', 'G', 'A', 'A']
PROG = ['D', 'E_D', 'D', 'E_D'] + A_PROG + B_PROG + C_PROG + A_PROG[:4] + ['Em', 'G', 'A', 'A']

THEME = ('F#5:1.5 A5:.5 D6 C#6 | B5:1.5 G#5:.5 E5:2 | F#5 D5 B4 D5 | E5:4 | '
         'F#5:1.5 A5:.5 D6 E6 | C#6:2 A5:2 | B5:1.5 C#6:.5 D6 E6 | A5:4 | '
         'D6:1.5 C#6:.5 B5 F#5 | A5:2 C#6:2 | B5:1.5 A5:.5 G5 B5 | A5:2 F#5:2 | '
         'G5:1.5 F#5:.5 E5 B5 | B5:2 A5 G5 | A5:1.5 B5:.5 C#6 E6 | E6:4 |')
COUNTER = ('B5:3 A5 | C#6:2 E6:2 | F#6:3 E6 | D6:4 | B5:2 D6 G6 | F#6:2 E6 C#6 | D6:4 | D6:2 F#6:2 | '
           'G6:3 E6 | E6:2 C6 G5 | B5:2 D6 G6 | F#6:4 | G6:2 E6 B5 | A5:2 C#6 F#6 | D6:2 B5 G5 | A5:4 |')
ELEMENTS = 'F#6:2 D6 B5 | G5:2 B5 D6 | A5:2 F#5 D5 | E5:4 | F#6:2 D6 B5 | G5:2 B5 D6 | E6:2 C#6 A5 | A5:4 |'
ARP = [0, 1, 2, 3, 2, 1, 2, 3]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-14.0)
    s.reverb.update(seconds=3.4, t60=2.6, predelay=0.025, ret=0.34, hf=0.45, lf=1.0)
    s.delay.update(beats=0.75, feedback=0.36, damp=4200.0, ret=0.26, to_reverb=0.4)
    s.duck_tau = 0.12

    bass = s.track('bass', gain_db=-6.0, duck=0.25, lowpass=1800, highpass=35)
    pad = s.track('pad', gain_db=-10.0, reverb=0.5, duck=0.15, highpass=120)
    arp = s.track('celesta', gain_db=-6.0, reverb=0.4, delay=0.25, highpass=400)
    mar = s.track('marimba', gain_db=-6.0, reverb=0.3, delay=0.15, highpass=200)
    lead = s.track('ocarina', gain_db=-5.0, reverb=0.4, delay=0.18, highpass=250)
    strings = s.track('strings', gain_db=-8.0, reverb=0.45, highpass=150)
    brass = s.track('brass', gain_db=-10.0, reverb=0.4, highpass=150)
    choir = s.track('choir', gain_db=-6.0, reverb=0.6, duck=0.1, highpass=180)
    elem = s.track('elements', gain_db=-6.0, reverb=0.5, delay=0.25, highpass=300)
    kick = s.track('kick', gain_db=-10.0, reverb=0.04)
    snare = s.track('snare', gain_db=-9.0, reverb=0.3)
    shk = s.track('shaker', gain_db=-6.0, reverb=0.12, pan=-0.25, highpass=3000)
    frame = s.track('frame', gain_db=-7.0, reverb=0.25)
    cym = s.track('cymbals', gain_db=-14.0, reverb=0.35)
    fx = s.track('fx', gain_db=-14.0, reverb=0.45)

    sec_b = range(21, 37)
    sec_c = range(37, 45)
    for bar, nb, name in blocks(PROG):
        root, voicing, tones = CHORD[name]
        pad.chord(I.pad_warm, bar, 0, nb * 4 - 0.2, voicing, vel=0.55, attack=0.8, release=1.6, bright=3.5,
                  max_cut=2400.0)
        if bar in sec_b:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, voicing[1:], vel=0.6, vowel='a', attack=0.3, release=0.9)
            strings.chord(I.strings_legato, bar, 0, nb * 4 - 0.1, [note_midi(x) + 12 for x in voicing[:3]], vel=0.5,
                          attack=0.25, release=0.6)

    for bar, name in enumerate(PROG, start=1):
        root, voicing, tones = CHORD[name]
        r = note_midi(root)
        intro = bar <= 4
        # bass: a gentle syncopated pulse (pedal notes in the intro and the elements)
        if intro or bar in sec_c:
            bass.note(I.bass_sub, bar, 0, 3.8, r, vel=0.7, attack=0.05, release=0.4, drive=1.1)
        else:
            for (b, d, iv) in ((0, 1.4, 0), (1.5, 0.4, 0), (2.5, 1.0, 7), (3.5, 0.4, 12)):
                bass.note(I.bass_pluck, bar, b, d, r + iv, vel=0.8 if b == 0 else 0.62, peak=3.5, floor=1.2, tau=0.1,
                          q=1.0, sub=0.6, drive=1.1)
        # celesta arpeggio in 8ths (everywhere but the elements section)
        if bar not in sec_c:
            for k in range(8):
                arp.note(I.celesta, bar, k * 0.5, 0.5, tones[ARP[k]], vel=0.55 if k % 2 else 0.68,
                         pan=(-0.35 if k % 2 else 0.35))
        # marimba off-beat answers in B
        if bar in sec_b:
            for st in (3, 7, 11, 14):
                mar.note(I.marimba, bar, st * 0.25, 0.25, tones[(st // 4) % 4], vel=0.5, pan=0.3)

    lead.line(I.lead_flute, 5, THEME, vel=0.9, legato=0.94, vib=12.0, breath=0.08)
    strings.line(I.strings_legato, 21, COUNTER, vel=0.75, legato=0.96, attack=0.12, release=0.4)
    brass.line(I.lead_brass, 21, COUNTER, vel=0.45, legato=0.92, transpose=-12, bright=3.0, max_cut=2600.0)
    lead.line(I.lead_flute, 45, '|'.join(THEME.split('|')[:8]) + ' |', vel=0.9, legato=0.94, vib=12.0,
              breath=0.08)
    # the four elements: roots (marimba), frost (glass bells), magma (anvils), void (glassy lead)
    el = ELEMENTS.split('|')
    elem.line(I.marimba, 37, '|'.join(el[0:2]) + ' |', vel=0.85)
    elem.line(T.ice_bell, 39, '|'.join(el[2:4]) + ' |', vel=0.7, decay=1.8)
    elem.line(T.anvil, 41, '|'.join(el[4:6]) + ' |', vel=0.6, decay=1.2)
    elem.line(I.lead_glass, 43, '|'.join(el[6:8]) + ' |', vel=0.8)

    # ---- light percussion
    kk = D.kick(f_start=120, f_end=46, pitch_tau=0.03, decay=0.24, click=0.2, drive=1.3, variant=61)
    sn = D.snare(tone=200, tone_decay=0.05, noise_decay=0.11, noise_lp=7000, tone_mix=0.55, roomy=0.45, variant=61)
    rm = D.rim(variant=6)
    sh = D.shaker(variant=6)
    fd = P.frame_drum(110.0, variant=6)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            if bar >= 3:
                shk.hits(sh, bar, 'x.x.x.x.x.x.x.x.', vel=0.5)
            continue
        if bar in sec_c:
            frame.hits(fd, bar, 'x.......x.......', vel=0.6)
            shk.hits(sh, bar, '..x...x...x...x.', vel=0.4)
            continue
        B = bar in sec_b
        kick.hits(kk, bar, 'x.......x.x.....' if not B else 'x.....x.x.x.....', duck=True)
        snare.hits(rm if not B else sn, bar, '....x.......x...')
        shk.hits(sh, bar, 'xgxxxgxxxgxxxgxx' if B else 'x.xgx.xgx.xgx.xg', vel=0.65)
        frame.hits(fd, bar, '......x.......x.' if not B else '...x..x....x..x.', vel=0.5, pan=0.3)
    snare.hits(sn, 20, '........o.o.oxox', vel=0.6)
    snare.hits(sn, 36, '........o.o.oxox', vel=0.6)
    for bar in (5, 21, 45):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.riser(s.spb * 4, f0=400, f1=6000, q=2.0, variant=61), 52, 0, gain=0.7)
    fx.at(D.reverse_cymbal(s.spb * 2), 36, 2.0)
    return s
