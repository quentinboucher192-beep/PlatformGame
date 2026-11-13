"""sylvanox_boss - Thornwarden fight. C minor (harmonic minor tension), 140 BPM, 4/4,
36 bars = 61.71 s (one 16th note = exactly 4725 samples).

Form:  intro 1-4 (taiko + pedal bass + riser) | A 5-20 (galloping bass, spiccato strings,
       3-3-2 stabs, brass motif from bar 13) | B 21-36 (choir, soaring brass, heavier drums,
       Neapolitan Db -> G7 build that drops back into the intro).
"""
from music import drums as D
from music import instruments as I
from music.sequencer import Song
from synth import note_midi

NAME = 'sylvanox_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 140, 4, 36, True

# name: (bass root, stab voicing, spiccato tones (root, fifth, third+, octave), choir voicing)
CHORD = {
    'Cm': ('C2', ['G3', 'C4', 'Eb4', 'G4'], ['C4', 'G4', 'Eb5', 'C5'], ['Eb4', 'G4', 'C5']),
    'Ab': ('Ab1', ['Ab3', 'C4', 'Eb4', 'Ab4'], ['Ab3', 'Eb4', 'C5', 'Ab4'], ['Eb4', 'Ab4', 'C5']),
    'Bb': ('Bb1', ['F3', 'Bb3', 'D4', 'F4'], ['Bb3', 'F4', 'D5', 'Bb4'], ['D4', 'F4', 'Bb4']),
    'G': ('G1', ['G3', 'B3', 'D4', 'G4'], ['G3', 'D4', 'B4', 'G4'], ['D4', 'G4', 'B4']),
    'G7': ('G1', ['F3', 'B3', 'D4', 'G4'], ['G3', 'D4', 'B4', 'F4'], ['D4', 'F4', 'B4']),
    'Fm': ('F1', ['F3', 'Ab3', 'C4', 'F4'], ['F3', 'C4', 'Ab4', 'F4'], ['C4', 'F4', 'Ab4']),
    'Db': ('Db2', ['Ab3', 'Db4', 'F4', 'Ab4'], ['Db4', 'Ab4', 'F5', 'Db5'], ['Db4', 'F4', 'Ab4']),
}

PROG = (['Cm', 'Cm', 'Ab', 'G'] +                                   # 1-4 intro
        ['Cm', 'Cm', 'Ab', 'Bb', 'Cm', 'Cm', 'Fm', 'G'] +           # 5-12 A1
        ['Cm', 'Cm', 'Ab', 'Bb', 'Fm', 'Ab', 'G', 'G'] +            # 13-20 A2
        ['Ab', 'Bb', 'Cm', 'Cm', 'Ab', 'Bb', 'G', 'G'] +            # 21-28 B1
        ['Fm', 'G', 'Cm', 'Ab', 'Fm', 'Db', 'G', 'G7'])             # 29-36 B2

MELODY_A = (
    'G4 C5 D5 Eb5 | D5:1.5 C5:.5 G4:2 | Ab4 C5 Eb5 F5 | G5:1.5 F5:.5 D5:2 | '
    'F5 Eb5 C5 Ab4 | C5:1.5 Eb5:.5 Ab5:2 | G5:2 F5 D5 | B4:4 |'
)
MELODY_B = (
    'C6:2 Bb5 Ab5 | Bb5:2 F5:2 | Eb5 G5 C6:2 | D6:1.5 C6:.5 G5:2 | '
    'Ab5:1.5 G5:.5 F5 Eb5 | D5 F5 Bb5:2 | B5:2 D6:2 | G5:4 | '
    'Ab5:1.5 G5:.5 F5 C6 | B5:2 G5:2 | C6:1.5 D6:.5 Eb6:2 | D6 C6 Ab5:2 | '
    'F5 Ab5 C6 F6 | F6:2 Eb6 Db6 | D6:2 B5:2 | G5 F5 D5 B4 |'
)

SPIC = [0, 1, 2, 1, 3, 1, 2, 1]
GALLOP = [(0, 0), (2, 0), (3, 0), (4, 0), (6, 0), (7, 0), (8, 0), (10, 0), (11, 0),
          (12, 12), (14, 7), (15, 12)]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=2.8, t60=1.9, predelay=0.015, ret=0.28, hf=0.4)
    s.delay.update(beats=0.75, feedback=0.3, damp=3000.0, ret=0.25, to_reverb=0.3)
    s.duck_tau = 0.09

    bass = s.track('bass', gain_db=-6.0, duck=0.35, lowpass=2500, highpass=30)
    spic = s.track('spiccato', gain_db=-1.0, reverb=0.25, duck=0.2, highpass=150)
    stab = s.track('stabs', gain_db=-5.5, reverb=0.3, highpass=120)
    lead = s.track('brass', gain_db=-7.0, reverb=0.35, delay=0.15, highpass=150)
    low_strings = s.track('low_strings', gain_db=-10.0, reverb=0.35, duck=0.3, highpass=60)
    choir = s.track('choir', gain_db=-3.0, reverb=0.55, duck=0.15, highpass=180)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    snare = s.track('snare', gain_db=-5.0, reverb=0.25)
    hats = s.track('hats', gain_db=2.0, reverb=0.1, pan=0.2)
    taiko = s.track('taiko', gain_db=-8.0, reverb=0.3)
    cym = s.track('cymbals', gain_db=-11.0, reverb=0.3)
    fx = s.track('fx', gain_db=-10.0, reverb=0.35)

    for bar, name in enumerate(PROG, start=1):
        root, voicing, tones, ch_v = CHORD[name]
        r = note_midi(root)
        intro = bar <= 4
        # bass
        if intro:
            for k in range(8):
                bass.note(I.bass_pluck, bar, k * 0.5, 0.4, r, vel=0.8 if k % 2 == 0 else 0.6,
                          peak=4.0, floor=1.5, tau=0.08, q=1.2, sub=0.7, drive=1.8, detune=9.0)
        else:
            for step, iv in GALLOP:
                bass.note(I.bass_pluck, bar, step * 0.25, 0.22 if step % 4 else 0.4, r + iv,
                          vel=0.9 if step % 4 == 0 else 0.7, peak=6.0, floor=1.6, tau=0.07,
                          q=1.3, sub=0.6, drive=2.2, detune=9.0)
        # spiccato 16ths (from bar 3)
        if bar >= 3:
            for k in range(16):
                v = (0.75 if k % 4 == 0 else 0.55) * (0.85 if intro else 1.0)
                spic.note(I.strings_stacc, bar, k * 0.25, 0.25, tones[SPIC[k % 8]], vel=v,
                          decay=0.07, pan=(-0.35 if k % 2 else 0.35))
        # 3-3-2 stabs in A, sustained low strings + choir in B
        if 5 <= bar <= 20:
            for beat in (0, 1.5, 3):
                stab.chord(I.strings_stacc, bar, beat, 0.4, voicing, vel=0.85 if beat == 0 else 0.7,
                           decay=0.12)
        if bar >= 21:
            low_strings.chord(I.strings_legato, bar, 0, 3.8, [root, note_midi(root) + 7 + 12],
                              vel=0.7, attack=0.08, release=0.3)
            choir.chord(I.pad_choir, bar, 0, 3.9, ch_v, vel=0.7, vowel='a', attack=0.15,
                        release=0.6)

    # brass melody, doubled an octave lower for weight
    lead.line(I.lead_brass, 13, MELODY_A, vel=0.85, legato=0.92)
    lead.line(I.lead_brass, 13, MELODY_A, vel=0.55, legato=0.92, transpose=-12, bright=3.5)
    lead.line(I.lead_brass, 21, MELODY_B, vel=0.9, legato=0.92, max_cut=4500.0)
    lead.line(I.lead_brass, 21, MELODY_B, vel=0.6, legato=0.92, transpose=-12, bright=3.5)

    # ---- drums
    kk = D.kick(f_start=160, f_end=46, pitch_tau=0.03, decay=0.3, click=0.35, drive=2.0, variant=3)
    sn = D.snare(tone=180, tone_decay=0.07, noise_decay=0.16, noise_lp=8000, tone_mix=0.7,
                 roomy=0.35, variant=3)
    cl = D.clap(variant=1)
    ch = D.hat(decay=0.025, length=0.08, bright=1.0, metal=0.55, variant=3)
    tk = D.taiko(62.0)
    tk_hi = D.taiko(92.0, variant=1)
    for bar in range(1, 37):
        if bar <= 4:
            taiko.hits(tk, bar, 'X..x..x.X...x.x.', duck=True)
            taiko.hits(tk_hi, bar, '....o.....o....o', vel=0.8, pan=0.3)
            if bar >= 3:
                hats.hits(ch, bar, 'x.x.x.x.x.x.x.x.', vel=0.6)
            continue
        A = bar <= 20
        kick.hits(kk, bar, 'x...x...x...x...' if A and bar % 4 else
                  'x...x...x.x.x...' if A else 'x..xx...x..xx..x', duck=True)
        snare.hits(sn, bar, '....x.......x...' if A else '....x..g....x.gg')
        snare.hits(cl, bar, '....x.......x...', vel=0.6)
        hats.hits(ch, bar, 'xgoxxgoxxgoxxgox' if A else 'xoxoxoxoxoxoxoxo', vel=0.85)
        if not A and bar % 2 == 1:
            taiko.hits(tk, bar, 'X.......x.......', vel=0.9)
    # fills / transitions
    for bar in (12, 28):
        snare.hits(D.tom(180.0, variant=1), bar, '............x.x.', pan=0.3)
        snare.hits(D.tom(120.0, variant=1), bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 36, 'ggoooooxxxxxXXXX', vel=0.85)
    fx.at(D.boom(55.0), 1, 0)
    fx.at(D.riser(s.spb * 4, f0=300, f1=7000, q=3.0), 4, 0)
    fx.at(D.riser(s.spb * 4, f0=300, f1=7000, q=3.0, variant=1), 36, 0)
    fx.at(D.reverse_cymbal(s.spb * 2), 20, 2.0)
    for bar in (5, 13, 21, 25, 29, 33):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.boom(49.0, variant=1), 21, 0, gain=0.8)
    return s
