"""singularis_boss - Singularis, le Coeur Effondre (giga boss of the Noyau stellaire). D minor with a
phrygian E-flat and a lydian G-sharp in the climaxes, 132 BPM, 4/4, 40 bars = 72.73 s.

Character: a dead star waking up, then going supernova.  An epic, relentless fight: a driving
octave bass ostinato, pounding taikos and toms, a cathedral choir singing the star's dying
overtones, a bright glass lead over stabbing brass, an arpeggiated celesta like orbiting debris,
and a gravitational breakdown where everything is sucked in before the final climb.

Form:  intro 1-4 (sub drops, bells, a rising choir)  | A 5-12 (ostinato, taikos, celesta orbit)
       | B 13-24 (choir + glass lead + brass stabs, double-time)
       | collapse 25-28 (half time, the void: drone, heartbeat, reversed swells)
       | supernova 29-40 (everything, the theme a fifth higher, a riser into the loop).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'singularis_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 132, 4, 40, True

# name: (bass root, bell dyad, choir voicing)
CHORD = {
    'Dm': ('D2', ['D5', 'A5'], ['D4', 'F4', 'A4']),
    'Eb': ('D#2', ['D#5', 'A#5'], ['D#4', 'G4', 'A#4']),
    'Bb': ('A#1', ['A#4', 'F5'], ['D4', 'F4', 'A#4']),
    'C': ('C2', ['C5', 'G5'], ['C4', 'E4', 'G4']),
    'Gm': ('G1', ['G4', 'D5'], ['D4', 'G4', 'A#4']),
    'A': ('A1', ['A4', 'E5'], ['C#4', 'E4', 'A4']),
    'F': ('F1', ['F4', 'C5'], ['C4', 'F4', 'A4']),
    'E': ('E2', ['E5', 'G#5'], ['E4', 'G#4', 'B4']),
}

PROG = (['Dm', 'Eb', 'Dm', 'Eb'] +                                         # 1-4 intro
        ['Dm', 'Dm', 'Bb', 'C', 'Dm', 'Dm', 'Gm', 'A'] +                   # 5-12 A
        ['Bb', 'C', 'Dm', 'Dm', 'Gm', 'Eb', 'A', 'A', 'Bb', 'C', 'E', 'A'] +   # 13-24 B
        ['Dm', 'Eb', 'Dm', 'Eb'] +                                         # 25-28 collapse
        ['Dm', 'Bb', 'C', 'Dm', 'Gm', 'Eb', 'A', 'A', 'F', 'C', 'E', 'A'])     # 29-40 supernova

LEAD_A = ('D5:2 F5 A5 | G5:2 F5 E5 | F5:3 D5 | E5:4 | D6:2 C6 A5 | A#5:2 A5 G5 | A5:3 G5 | E5:4 |')
LEAD_B = ('F5:2 A5 D6 | E6:2 D6 C6 | D6:3 A5 | A5:4 | A#5:2 D6 G6 | G6:2 F6 D#6 | E6:3 C#6 | A5:4 | '
          'D6:2 F6 A#5 | C6:2 E6 G5 | G#5:2 B5 E6 | A5:4 |')
LEAD_S = ('A5:2 C6 E6 | D6:2 C6 A#5 | C6:3 G5 | A5:4 | D6:2 F6 G6 | G6:2 F6 D#6 | E6:3 C#6 | A5:4 | '
          'C6:2 F6 A6 | G6:2 E6 C6 | G#5:2 B5 E6 | A5:4 |')
OSTINATO = [(0, 0), (2, 12), (3, 0), (5, 12), (6, 0), (8, 0), (10, 12), (11, 0), (13, 7), (14, 12)]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=4.5, t60=3.2, predelay=0.03, ret=0.34, hf=0.45, lf=1.1)
    s.delay.update(beats=0.75, feedback=0.35, damp=3200.0, ret=0.2, to_reverb=0.4)
    s.duck_tau = 0.1

    bass = s.track('bass', gain_db=-6.0, duck=0.4, lowpass=2800, highpass=34)
    sub = s.track('sub', gain_db=-6.5, duck=0.2, lowpass=200, highpass=30)
    bells = s.track('bells', gain_db=-7.0, reverb=0.55, delay=0.25, highpass=400)
    orbit = s.track('orbit', gain_db=-10.0, reverb=0.45, delay=0.35, highpass=500, pan=0.25)
    lead = s.track('lead', gain_db=-5.0, reverb=0.45, delay=0.22, highpass=250)
    brass = s.track('brass', gain_db=-7.0, reverb=0.35, highpass=90)
    strings = s.track('strings', gain_db=-9.0, reverb=0.5, duck=0.25, highpass=100)
    choir = s.track('choir', gain_db=-3.5, reverb=0.65, duck=0.15, highpass=150)
    kick = s.track('kick', gain_db=-7.0, reverb=0.04)
    snare = s.track('snare', gain_db=-5.5, reverb=0.35)
    hats = s.track('hats', gain_db=0.0, reverb=0.12, pan=-0.2, highpass=4500)
    taiko = s.track('taiko', gain_db=-5.5, reverb=0.35)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.4)
    fx = s.track('fx', gain_db=-9.0, reverb=0.45)

    for bar, nb, name in blocks(PROG):
        root, dyad, ch_v = CHORD[name]
        r = note_midi(root)
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r - 12 if r - 12 >= 24 else r, vel=0.72, attack=0.2, release=0.8,
                 drive=1.3)
        if bar <= 4 or 13 <= bar <= 28 or bar >= 29:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.55 if bar <= 4 else 0.72,
                        vowel='a' if bar >= 29 else 'o', attack=0.35, release=1.0)
        if bar >= 5:
            strings.chord(I.strings_legato, bar, 0, nb * 4 - 0.2, [r + 24, r + 31], vel=0.55, attack=0.5, release=0.8)

    for bar, name in enumerate(PROG, start=1):
        root, dyad, ch_v = CHORD[name]
        r = note_midi(root)
        collapse = 25 <= bar <= 28
        # driving octave ostinato (not in the intro nor in the collapse)
        if 5 <= bar <= 24 or bar >= 29:
            for step, iv in OSTINATO:
                bass.note(T.bass_forge, bar, step * 0.25, 0.3, r + iv, vel=0.95 if step in (0, 8) else 0.74,
                          peak_mul=4.5, drive=3.0)
        # bells: the star's overtones on beats 1 (and 3 in the B and the supernova)
        for beat in ((0, 2) if (13 <= bar <= 24 or bar >= 29) else (0,)):
            bells.chord(T.ice_bell, bar, beat, 2, dyad, vel=0.5 if beat else 0.6, decay=2.0)
        # orbiting debris: a celesta arpeggio of the chord (A, B and supernova)
        if not collapse and bar >= 5:
            notes = [note_midi(n) + 12 for n in ch_v]
            for k in range(8):
                orbit.note(I.celesta, bar, k * 0.5, 0.45, notes[k % 3] + (12 if k >= 4 else 0),
                           vel=0.45 + 0.1 * (k % 2))
        # brass stabs on the off-beats of the B and the supernova
        if 13 <= bar <= 24 or bar >= 29:
            for beat in (1.5, 3.5):
                brass.chord(T.brass_stab, bar, beat, 0.4, [n for n in ch_v], vel=0.7)

    lead.line(I.lead_glass, 5, LEAD_A, vel=0.82, legato=0.95, vib=9.0)
    lead.line(I.lead_glass, 13, LEAD_B, vel=0.95, legato=0.95, vib=9.0)
    brass.line(T.brass_low, 13, LEAD_B, vel=0.62, legato=0.92, transpose=-24, bright=2.6, max_cut=1800.0, growl=0.25)
    lead.line(I.lead_glass, 29, LEAD_S, vel=1.0, legato=0.95, vib=10.0)
    brass.line(T.brass_low, 29, LEAD_S, vel=0.7, legato=0.92, transpose=-24, bright=2.8, max_cut=2000.0, growl=0.3)

    # ---- drums
    kk = D.kick(f_start=160, f_end=42, pitch_tau=0.03, decay=0.32, click=0.35, drive=2.6, variant=61)
    sn = D.snare(tone=180, tone_decay=0.08, noise_decay=0.2, noise_lp=7500, tone_mix=0.65, roomy=0.5, variant=61)
    cl = D.clap(variant=6)
    hh = D.hat(decay=0.028, length=0.1, bright=1.15, metal=0.6, variant=61)
    tk = D.taiko(52.0, variant=61)
    heart = P.sub_hit(44.0, length=0.6, variant=62)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.......x.......' if bar % 2 else 'X...............', duck=True)
            if bar >= 3:
                hats.hits(hh, bar, '..x...x...x...x.', vel=0.5)
            continue
        if 25 <= bar <= 28:
            fx.hits(heart, bar, 'x..x............', vel=0.9)
            hats.hits(hh, bar, '........x.......', vel=0.4)
            continue
        big = bar >= 29
        B = 13 <= bar <= 24 or big
        kick.hits(kk, bar, 'x...x...x...x...' if not B else 'x..xx...x..xx.x.', duck=True)
        snare.hits(sn, bar, '....x.......x...')
        snare.hits(cl, bar, '....x.......x...', vel=0.5)
        hats.hits(hh, bar, 'x.xgx.xgx.xgx.xg' if not B else 'xxxxxgxxxxxxxgxx', vel=0.7)
        taiko.hits(tk, bar, 'X.....x...X.....' if not big else 'X..x..x.X..x..x.', vel=0.85)
    snare.hits(sn, 12, '........o.o.xxXX', vel=0.8)
    snare.hits(sn, 24, '........oooxxxXX', vel=0.85)
    snare.hits(sn, 40, 'oooooooooooxxxXX', vel=0.9)
    for bar in (1, 25):
        fx.at(P.sub_hit(38.0, length=2.5, variant=bar + 60), bar, 0, gain=1.2)
    for bar in (4, 12, 24, 28):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=150, f1=7000, q=3.0, variant=61), 27, 0, gain=0.9)
    fx.at(D.riser(s.spb * 8, f0=200, f1=8000, q=3.0, variant=62), 39, 0, gain=0.8)
    for bar in (5, 13, 21, 29, 37):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.boom(40.0, variant=61), 13, 0, gain=0.9)
    fx.at(D.boom(36.0, variant=62), 29, 0, gain=1.0)
    return s
