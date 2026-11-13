"""pyralis_level - 'La forge en fusion': driving, percussive, ominous. E phrygian (the F-over-E
semitone grind), 108 BPM, 4/4, 44 bars = 97.78 s (one 16th note = exactly 6125 samples).

Form:  intro 1-4 (taiko, forge hammers, anvil calls, pedal bass)
       | A 5-20 (palm-muted forge bass chug, low brass horn theme, anvils on the off-beats;
         A2 adds the F grind in the bass and the theme climbs)
       | B 21-36 (bVI-bVII-i lift C-D-Em, heroic high brass over choir, four-on-the-floor kit)
       | breakdown 37-40 (half time: the theme rung on anvils over a sub pedal and dark choir)
       | rebuild 41-44 (kit and chug return, theme fragment, tom fill back into the intro).
Palette: struck anvils, low brass section and stabs, saturated forge bass, metallic hats,
hammer/clang backbeat, taiko, chains and steam bursts.
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks, air_layer
from synth import note_midi

NAME = 'pyralis_level'
BPM, BEATS_PER_BAR, BARS, LOOP = 108, 4, 44, True

# name: (bass root, horn voicing, choir voicing, anvil tones)
CHORD = {
    'Em': ('E2', ['E3', 'B3', 'E4'], ['B3', 'E4', 'G4'], ['E5', 'B5', 'G5']),
    'F':  ('F2', ['F3', 'C4', 'F4'], ['C4', 'F4', 'A4'], ['F5', 'C6', 'A5']),
    'G':  ('G2', ['G3', 'D4', 'G4'], ['B3', 'D4', 'G4'], ['G5', 'D6', 'B5']),
    'Dm': ('D2', ['D3', 'A3', 'D4'], ['A3', 'D4', 'F4'], ['D5', 'A5', 'F5']),
    'C':  ('C2', ['C3', 'G3', 'C4'], ['C4', 'E4', 'G4'], ['C5', 'G5', 'E5']),
    'D':  ('D2', ['D3', 'A3', 'D4'], ['A3', 'D4', 'F#4'], ['D5', 'A5', 'F#5']),
    'Am': ('A1', ['A2', 'E3', 'A3'], ['C4', 'E4', 'A4'], ['A4', 'E5', 'C5']),
}

PROG = (['Em', 'Em', 'F', 'Em'] +                                   # 1-4 intro
        ['Em', 'Em', 'F', 'Em', 'Em', 'Em', 'Dm', 'F'] +            # 5-12 A1
        ['Em', 'Em', 'F', 'Em', 'Em', 'Em', 'G', 'F'] +             # 13-20 A2
        ['C', 'D', 'Em', 'Em', 'C', 'D', 'F', 'F'] +                # 21-28 B1
        ['Am', 'C', 'D', 'Em', 'C', 'D', 'F', 'F'] +                # 29-36 B2
        ['Em', 'F', 'Em', 'F'] +                                    # 37-40 breakdown
        ['Em', 'Em', 'F', 'Em'])                                    # 41-44 rebuild

THEME_A1 = (
    'E3:1.5 F3:.5 G3 B3 | A3:1.5 G3:.5 F3:2 | F3:1.5 G3:.5 A3 C4 | B3:4 | '
    'E4:1.5 D4:.5 B3 G3 | A3:1.5 B3:.5 G3:2 | F3:1.5 E3:.5 D3 F3 | E3:3 r |'
)
THEME_A2 = (
    'E3:1.5 F3:.5 G3 B3 | C4:1.5 B3:.5 A3:2 | A3:1.5 C4:.5 F4 E4 | E4:4 | '
    'G4:1.5 F4:.5 E4 D4 | E4:1.5 B3:.5 G3:2 | B3:1.5 D4:.5 G4 F4 | F4:2 E4 G4 |'
)
THEME_B = (
    'G4:1.5 C5:.5 E5:2 | F#5:1.5 E5:.5 D5:2 | E5:1.5 B4:.5 G4 B4 | E5:4 | '
    'G5:2 E5 C5 | D5:1.5 F#5:.5 A5:2 | A5:1.5 G5:.5 F5 C5 | F5:3 r | '
    'A4:1.5 C5:.5 E5 A5 | G5:2 E5 C5 | D5:1.5 E5:.5 F#5 A5 | G5:1.5 F#5:.5 E5:2 | '
    'E5:1.5 G5:.5 C6:2 | B5:1.5 A5:.5 F#5 D5 | F5:1.5 G5:.5 A5 C6 | B5:2 E5:2 |'
)
ANVIL_THEME = 'E5:1.5 F5:.5 G5 B5 | A5:1.5 G5:.5 F5:2 | E5:1.5 F5:.5 G5 C6 | B5:2 F5:2 |'

CHUG_A1 = [(0, 0), (2, 0), (3, 0), (4, 0), (6, 0), (7, 0), (8, 0), (10, 0), (12, 0), (13, 0),
           (14, 12)]
CHUG_A2 = [(0, 0), (2, 0), (3, 0), (4, 0), (6, 0), (7, 0), (8, 0), (10, 0), (12, 0), (13, 0),
           (14, 12), (15, 1)]
DRIVE_B = [(0, 0), (2, 0), (4, 0), (6, 0), (8, 0), (10, 0), (12, 12), (14, 7)]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-14.0)
    s.reverb.update(seconds=3.0, t60=2.1, predelay=0.025, ret=0.3, hf=0.32, lf=1.2)
    s.delay.update(beats=0.75, feedback=0.3, damp=2600.0, ret=0.22, to_reverb=0.35)
    s.duck_tau = 0.1

    drone = s.track('drone', gain_db=-13.0, reverb=0.4, duck=0.35, highpass=70)
    bass = s.track('bass', gain_db=-9.0, duck=0.4, highpass=40, lowpass=3200)
    horn = s.track('horn', gain_db=-4.0, reverb=0.32, delay=0.08, highpass=90)
    lead = s.track('lead', gain_db=-7.5, reverb=0.36, delay=0.14, highpass=160)
    stabs = s.track('stabs', gain_db=-9.0, reverb=0.28, highpass=110)
    choir = s.track('choir', gain_db=-4.0, reverb=0.55, duck=0.15, highpass=160)
    anv = s.track('anvil', gain_db=-4.0, reverb=0.3, delay=0.2, highpass=300)
    kick = s.track('kick', gain_db=-6.0, reverb=0.03)
    snare = s.track('snare', gain_db=-7.0, reverb=0.22)
    forge = s.track('forge', gain_db=-6.0, reverb=0.3, highpass=60)
    hats = s.track('hats', gain_db=-1.0, reverb=0.1, pan=0.22, highpass=4000)
    taiko = s.track('taiko', gain_db=-6.0, reverb=0.25)
    toms = s.track('toms', gain_db=-12.0, reverb=0.25)
    fx = s.track('fx', gain_db=-13.0, reverb=0.4)
    air = s.track('air', gain_db=-27.0, reverb=0.2)

    # ---- harmony: low brass drone + choir in B / breakdown
    for bar, nb, name in blocks(PROG):
        root, horns, ch_v, _ = CHORD[name]
        if 5 <= bar <= 20 or bar >= 41:
            continue  # the horn theme owns that register in A
        drone.chord(T.brass_low, bar, 0, nb * 4 - 0.2, horns, vel=0.5, attack=0.6, release=0.8,
                    bright=2.0, max_cut=1300.0, drive=1.4, vib=0.0)
        if 21 <= bar <= 40:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.62,
                        vowel='o' if bar >= 37 else 'a', attack=0.35, release=1.0)

    # ---- bass
    for bar, name in enumerate(PROG, start=1):
        r = note_midi(CHORD[name][0])
        if bar <= 4:
            if bar >= 2:
                for k in range(8):
                    bass.note(T.bass_forge, bar, k * 0.5, 0.36, r, vel=0.8 if k % 2 == 0 else 0.62,
                              peak_mul=4.0, drive=2.4)
        elif 37 <= bar <= 40:
            bass.note(I.bass_sub, bar, 0, 3.8, r, vel=0.95, attack=0.02,
                      release=0.4, drive=1.6)
        else:
            if 21 <= bar <= 36:
                pat = DRIVE_B
            elif 13 <= bar <= 20 or bar >= 41:
                pat = CHUG_A2 if name == 'Em' else CHUG_A1
            else:
                pat = CHUG_A1
            for step, iv in pat:
                bass.note(T.bass_forge, bar, step * 0.25, 0.4 if step % 4 == 0 else 0.2, r + iv,
                          vel=0.92 if step % 4 == 0 else 0.72, peak_mul=5.5 if bar < 21 else 6.5,
                          drive=3.0)

    # ---- themes
    horn.line(T.brass_low, 5, THEME_A1, vel=0.85, legato=0.94, bright=3.6, max_cut=3200.0)
    horn.line(T.brass_low, 13, THEME_A2, vel=0.88, legato=0.94, bright=3.8, max_cut=3400.0)
    horn.line(T.brass_low, 13, THEME_A2, vel=0.5, legato=0.94, transpose=-12, bright=2.6,
              max_cut=1400.0, drive=1.6)
    lead.line(I.lead_brass, 21, THEME_B, vel=0.85, legato=0.92, max_cut=4200.0, bright=4.0)
    horn.line(T.brass_low, 21, THEME_B, vel=0.5, legato=0.92, transpose=-12, bright=3.0,
              max_cut=2200.0)
    horn.line(T.brass_low, 41, 'E3:1.5 F3:.5 G3 B3 | A3:1.5 G3:.5 F3:2 | r:4 | r:4 |', vel=0.8,
              legato=0.94)
    anv.line(T.anvil, 37, ANVIL_THEME, vel=0.75, decay=1.4, pan=0.15)
    anv.line(T.anvil, 37, ANVIL_THEME, vel=0.35, transpose=-12, decay=1.1, pan=-0.25)

    # ---- brass stabs: 3-3-2 hits in B and the rebuild
    for bar, name in enumerate(PROG, start=1):
        horns = CHORD[name][1]
        if 21 <= bar <= 36 or bar >= 41:
            for beat in (0, 1.5, 3):
                stabs.chord(T.brass_stab, bar, beat, 0.35, [note_midi(x) + 12 for x in horns],
                            vel=0.85 if beat == 0 else 0.68)

    # ---- anvils on the off-beats (forge rhythm)
    for bar, name in enumerate(PROG, start=1):
        tones = CHORD[name][3]
        if 37 <= bar <= 40:
            continue
        if bar <= 4:
            steps = [(2, 0), (10, 1)] if bar % 2 else [(2, 0), (6, 2), (10, 1)]
            v = 0.55
        elif bar <= 12:
            steps = [(2, 0), (10, 1)]
            v = 0.55
        elif bar <= 20 or bar >= 41:
            steps = [(2, 0), (6, 2), (10, 1), (14, 2)]
            v = 0.5
        else:
            steps = [(2, 0), (6, 1), (10, 2), (14, 1)]
            v = 0.42
        for st, ti in steps:
            anv.note(T.anvil, bar, st * 0.25, 0.25, tones[ti], vel=v * (1.0 if st % 8 == 2 else 0.8),
                     decay=0.7, pan=(-0.35 if st % 8 == 2 else 0.35))

    # ---- drums
    kk = D.kick(f_start=140, f_end=46, pitch_tau=0.032, decay=0.3, click=0.3, drive=1.9, variant=5)
    sn = D.snare(tone=170, tone_decay=0.07, noise_decay=0.15, noise_lp=7000, tone_mix=0.65,
                 roomy=0.35, variant=5)
    hh = P.metal_hat(decay=0.035, length=0.12)
    oh = P.metal_hat(decay=0.16, length=0.4, variant=1)
    cl = P.clang(1180.0, decay=0.35)
    hm = P.hammer()
    tk = D.taiko(58.0, variant=4)
    tk_hi = D.taiko(88.0, variant=5)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.....x.X.......' if bar < 4 else 'X.....x.X...x.x.', duck=True)
            taiko.hits(tk_hi, bar, '....o.......o..o', vel=0.8, pan=0.3)
            forge.hits(hm, bar, '....x.......x...', vel=0.9)
            if bar >= 3:
                hats.hits(hh, bar, 'x.o.x.o.x.o.x.o.', vel=0.7)
            continue
        if 37 <= bar <= 40:
            taiko.hits(tk, bar, 'X.......o.......', duck=True)
            forge.hits(hm, bar, '........x.......', vel=0.8)
            if bar == 40:
                taiko.hits(tk_hi, bar, '........o.o.x.xx', vel=0.85, pan=0.25)
            continue
        if bar <= 20 or bar >= 41:
            A2 = bar >= 13
            kick.hits(kk, bar, ('x.....x.x.....x.' if not A2 else 'x..x..x.x.....x.'), duck=True)
            snare.hits(sn, bar, '....x.......x...' if bar % 4 else '....x.......x.gg')
            forge.hits(cl, bar, '....x.......x...', vel=0.7)
            hats.hits(hh, bar, 'xgoxxgoxxgoxxgox' if A2 else 'x.o.x.o.x.o.x.o.')
            if bar % 2 == 1:
                taiko.hits(tk, bar, 'X...............', vel=0.7)
        else:
            kick.hits(kk, bar, 'x...x...x...x...' if bar % 4 else 'x...x...x.x.x.x.', duck=True)
            snare.hits(sn, bar, '....x.......x...' if bar % 4 else '....x.......x.xx')
            forge.hits(hm, bar, '....x.......x...', vel=0.75)
            hats.hits(oh, bar, '..x...x...x...x.', vel=0.75)
            hats.hits(hh, bar, 'x.o.x.o.x.o.x.o.', vel=0.6)
            if bar % 2 == 1:
                taiko.hits(tk, bar, 'X.......x.......', vel=0.8)
    # fills / transitions
    for bar in (12, 28):
        toms.hits(D.tom(150.0, variant=4), bar, '............x.x.', pan=0.3)
        toms.hits(D.tom(100.0, variant=4), bar, '..............xx', pan=-0.3)
    for bar in (20, 36, 44):
        toms.hits(D.tom(180.0, variant=5), bar, '........x.x.....', pan=0.35)
        toms.hits(D.tom(130.0, variant=5), bar, '............x.x.', pan=0.0)
        toms.hits(D.tom(92.0, variant=5), bar, '..............xX', pan=-0.35)
    fx.at(D.reverse_cymbal(s.spb * 2), 4, 2.0)
    fx.at(D.riser(s.spb * 4, f0=250, f1=6500, q=3.0), 20, 0.0, gain=0.7)
    fx.at(D.riser(s.spb * 4, f0=250, f1=5000, q=3.0, variant=1), 40, 0.0, gain=0.5)
    for bar in (5, 21, 29, 41):
        fx.at(D.crash(variant=bar % 3), bar, 0, gain=0.7, pan=-0.3 if bar % 2 else 0.3)
    for bar in (21, 37):
        fx.at(P.sub_hit(50.0, variant=bar), bar, 0, gain=1.2)
    for bar, beat in ((9, 2.0), (17, 2.0), (25, 2.0), (39, 0.0)):
        fx.at(P.chain(0.7, 10, variant=bar), bar, beat, gain=1.2, pan=0.5 if bar % 2 else -0.5)
    for bar, beat in ((13, 3.0), (33, 0.0), (38, 2.0)):
        fx.at(P.steam(1.1, variant=bar), bar, beat, gain=0.8, pan=-0.45)

    # ---- hot air: dark, slowly swelling haze
    air.at(air_layer(s.L, s.rng('air'), center=2600.0, low=700.0, swells=7), 1, 0)
    return s
