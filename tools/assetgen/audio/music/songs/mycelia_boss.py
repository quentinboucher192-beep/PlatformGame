"""mycelia_boss - Mycelia, la Mere-Racine (sanctuary of Aethra). E phrygian (E F G A B C D, the B
major dominant for tension), 126 BPM, 4/4, 36 bars = 68.57 s (one 16th note = exactly 5250 samples).

Character: organic and tribal - frame drums and taiko, a woody marimba ostinato that creeps like
mycelium, a plucked kalimba, a deep woody bass, an ocarina theme, eerie 'ooh' choir and a pad that
breathes in and out like the giant mushroom.

Form:  intro 1-4 (frame drums, marimba, breathing pad)
       | A1 5-12 (tribal groove, woody 3-3-2 bass, kalimba)  | A2 13-20 (ocarina theme)
       | B 21-28 (choir, brass doubling the theme, taiko)
       | breakdown 29-32 (drums only + the breath, a huge 'inhale')  | build 33-36.
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'mycelia_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 126, 4, 36, True

# name: (bass root, marimba tones (4), choir / pad voicing)
CHORD = {
    'Em': ('E1', ['E4', 'B4', 'G4', 'E5'], ['E4', 'G4', 'B4']),
    'F':  ('F1', ['F4', 'C5', 'A4', 'F5'], ['F4', 'A4', 'C5']),
    'G':  ('G1', ['G4', 'D5', 'B4', 'G5'], ['D4', 'G4', 'B4']),
    'Am': ('A1', ['A4', 'E5', 'C5', 'A5'], ['E4', 'A4', 'C5']),
    'C':  ('C2', ['C5', 'G5', 'E5', 'C6'], ['E4', 'G4', 'C5']),
    'D':  ('D2', ['D5', 'A5', 'F#5', 'D6'], ['D4', 'F#4', 'A4']),
    'B':  ('B1', ['B4', 'F#5', 'D#5', 'B5'], ['D#4', 'F#4', 'B4']),
}

PROG = (['Em', 'Em', 'F', 'Em'] +                                   # 1-4 intro
        ['Em', 'Em', 'F', 'Em', 'Am', 'G', 'F', 'Em'] +             # 5-12 A1
        ['Em', 'Em', 'F', 'G', 'Am', 'C', 'B', 'B'] +               # 13-20 A2
        ['C', 'D', 'Em', 'Em', 'Am', 'F', 'B', 'B'] +               # 21-28 B
        ['Em', 'F', 'Em', 'F'] +                                    # 29-32 breakdown
        ['Am', 'F', 'B', 'B'])                                      # 33-36 build

THEME = ('E5:1.5 F5:.5 G5 B5 | A5:1.5 G5:.5 F5:2 | E5 G5 F5 E5 | D5:2 E5:2 | '
         'A5:1.5 B5:.5 C6 A5 | G5:1.5 A5:.5 B5:2 | D#5:1.5 E5:.5 F#5 A5 | B5:4 |')
THEME_B = ('E6:1.5 D6:.5 C6 G5 | F#5:1.5 G5:.5 A5:2 | B5:2 G5 E5 | E5:4 | '
           'C6:1.5 B5:.5 A5 E5 | F5:2 A5 C6 | B5:1.5 A5:.5 F#5 D#5 | B4:4 |')
MAR = [0, 1, 2, 1, 3, 2, 1, 2]          # creeping marimba ostinato (tone indexes)
WOOD = [(0, 0), (3, 0), (6, 0), (8, 12), (10, 0), (11, 7), (14, 0)]   # 3-3-2 woody bass


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.5)
    s.reverb.update(seconds=3.0, t60=2.2, predelay=0.02, ret=0.3, hf=0.4, lf=1.1)
    s.delay.update(beats=0.75, feedback=0.3, damp=3000.0, ret=0.22, to_reverb=0.35)
    s.duck_tau = 0.1

    bass = s.track('bass', gain_db=-5.0, duck=0.35, lowpass=2200, highpass=32)
    mar = s.track('marimba', gain_db=-3.0, reverb=0.3, delay=0.12, duck=0.15, highpass=200)
    kal = s.track('kalimba', gain_db=12.0, reverb=0.4, delay=0.2, highpass=300)
    breath = s.track('breath', gain_db=-9.0, reverb=0.5, duck=0.25, highpass=80)
    lead = s.track('ocarina', gain_db=-5.0, reverb=0.4, delay=0.15, highpass=250)
    brass = s.track('brass', gain_db=-8.0, reverb=0.35, delay=0.1, highpass=150)
    choir = s.track('choir', gain_db=-3.0, reverb=0.6, duck=0.15, highpass=180)
    kick = s.track('kick', gain_db=-8.0, reverb=0.03)
    frame = s.track('frame', gain_db=-3.0, reverb=0.25)
    snare = s.track('snare', gain_db=-6.0, reverb=0.3)
    shk = s.track('shaker', gain_db=-2.0, reverb=0.1, pan=-0.25, highpass=3000)
    taiko = s.track('taiko', gain_db=-6.0, reverb=0.3)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.35)
    fx = s.track('fx', gain_db=-10.0, reverb=0.4)

    for bar, nb, name in blocks(PROG):
        root, tones, ch_v = CHORD[name]
        # the breathing pad: swells in and out once per block (the mushroom breathes)
        if bar <= 4 or 29 <= bar <= 32 or bar >= 21:
            breath.chord(I.pad_warm, bar, 0, nb * 4 - 0.2, ch_v, vel=0.6, attack=nb * 1.2, release=1.4,
                         bright=3.0, max_cut=1800.0)
        if 21 <= bar <= 28:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.66, vowel='u', attack=0.3, release=0.8)
        if 29 <= bar <= 32:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, [note_midi(x) - 12 for x in ch_v], vel=0.6, vowel='o',
                        attack=0.6, release=1.0)

    for bar, name in enumerate(PROG, start=1):
        root, tones, ch_v = CHORD[name]
        r = note_midi(root)
        if bar <= 4:
            bass.note(I.bass_sub, bar, 0, 3.8, r, vel=0.8, attack=0.05, release=0.4, drive=1.4)
        elif 29 <= bar <= 32:
            bass.note(I.bass_sub, bar, 0, 3.9, r, vel=0.9, attack=0.02, release=0.3, drive=1.6)
        else:
            for step, iv in WOOD:
                bass.note(I.bass_pluck, bar, step * 0.25, 0.3 if step % 4 else 0.45, r + iv,
                          vel=0.92 if step in (0, 8) else 0.72, peak=4.5, floor=1.3, tau=0.06, q=1.4, sub=0.7,
                          drive=1.8, shape='square')
        # creeping marimba 16ths (silent in the breakdown, softer in the intro)
        if not (29 <= bar <= 32):
            for k in range(16):
                v = (0.78 if k % 4 == 0 else 0.55 if k % 2 == 0 else 0.42) * (0.85 if bar <= 4 else 1.0)
                mar.note(I.marimba, bar, k * 0.25, 0.25, tones[MAR[k % 8]], vel=v,
                         pan=(-0.3 if k % 2 else 0.3))
        # kalimba arpeggio on the off-beats (A sections)
        if 5 <= bar <= 20 or 33 <= bar <= 35:
            for k, st in enumerate((2, 6, 10, 13)):
                kal.note(I.pluck, bar, st * 0.25, 0.5, note_midi(tones[k % 4]) + 12, vel=0.5, t60=0.9, bright=0.7,
                         pan=(0.5 if k % 2 else -0.4))

    lead.line(I.lead_flute, 13, THEME, vel=0.9, legato=0.94, vib=14.0, breath=0.09)
    lead.line(I.lead_flute, 21, THEME_B, vel=0.95, legato=0.94, vib=14.0, breath=0.09)
    brass.line(I.lead_brass, 21, THEME_B, vel=0.55, legato=0.92, transpose=-12, bright=3.5, max_cut=2600.0)
    lead.line(I.lead_flute, 33, 'A5:2 C6:2 | F5:2 A5 C6 | B5:1.5 A5:.5 F#5 D#5 | B5:4 |', vel=0.9,
              legato=0.94, vib=14.0, breath=0.09)

    # ---- drums: tribal frame drums, taiko, shaker
    kk = D.kick(f_start=140, f_end=44, pitch_tau=0.03, decay=0.3, click=0.25, drive=1.8, variant=11)
    sn = D.snare(tone=160, tone_decay=0.08, noise_decay=0.14, noise_lp=6000, tone_mix=0.75, roomy=0.4, variant=11)
    fd_lo = P.frame_drum(80.0, variant=1)
    fd_hi = P.frame_drum(140.0, variant=2)
    sh = D.shaker(variant=3)
    tk = D.taiko(58.0, variant=11)
    tk_hi = D.taiko(96.0, variant=12)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            frame.hits(fd_lo, bar, 'X..x..x...x.x...', vel=0.85, duck=True)
            frame.hits(fd_hi, bar, '....o.....o..o.o', vel=0.7, pan=0.3)
            shk.hits(sh, bar, 'x.xxx.xxx.xxx.xx', vel=0.6 if bar >= 3 else 0.45)
            continue
        if 29 <= bar <= 32:
            taiko.hits(tk, bar, 'X.......x...x...', duck=True)
            frame.hits(fd_hi, bar, '..o...o...o...oo', vel=0.6, pan=-0.3)
            continue
        A = bar <= 20 or bar >= 33
        kick.hits(kk, bar, 'x.....x...x.....' if A else 'x.....x.x.x...x.', duck=True)
        frame.hits(fd_lo, bar, '...x..x.....x..x' if A else 'x..x..x.x..x..x.', vel=0.75)
        frame.hits(fd_hi, bar, '.x..x...xo.x.o..', vel=0.6, pan=0.35)
        snare.hits(sn, bar, '....x.......x...')
        shk.hits(sh, bar, 'xgxxxgxxxgxxxgxx', vel=0.7)
        if not A and bar % 2 == 1:
            taiko.hits(tk, bar, 'X.......X.......', vel=0.9)
            taiko.hits(tk_hi, bar, '......x.......x.', vel=0.7, pan=0.3)
    for bar in (12, 28):
        snare.hits(D.tom(150.0, variant=11), bar, '............x.x.', pan=0.3)
        snare.hits(D.tom(100.0, variant=11), bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........ooxxoxXX', vel=0.8)
    snare.hits(sn, 36, 'g.g.o.o.ooooxxXX', vel=0.85)
    fx.at(P.sub_hit(46.0, variant=21), 1, 0, gain=1.1)
    fx.at(D.riser(s.spb * 4, f0=200, f1=5000, q=2.5, variant=21), 32, 0, gain=0.9)
    fx.at(D.riser(s.spb * 4, f0=300, f1=7000, q=3.0, variant=22), 36, 0)
    fx.at(D.reverse_cymbal(s.spb * 2), 20, 2.0)
    for bar in (1, 5, 13, 21, 33):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.boom(48.0, variant=21), 21, 0, gain=0.8)
    fx.at(P.sub_hit(44.0, variant=29), 29, 0, gain=1.1)
    return s
