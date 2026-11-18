"""asterion_film - the score of Asterion's film (2.68, data/raids/asterion_film.json). C minor (the
Neapolitan D-flat, the Picardy C major of the call at the end), 80 BPM, 4/4, 41 bars = 123 s plus a
3 s tail: one take, not a loop, following the film's ten shots (a bar is 3 s).

Character: a cosmic hunger.  A deep choir drone and a heartbeat of timpani in the void; strings and
taiko as the body is forged; the titan's theme on low brass (C - Eb - G - Ab ... the falling
Neapolitan); a staccato ostinato under the seven guardians; a wide choir over our planets; a
scramble of strings when Nova attacks, then silence; a march in E-flat major for the clans; cold
arpeggios for the hologram; the theme fortissimo with the roar; a last chord, the call.

Form (bars): void 1-4 | forge 5-8 | feast 9-12 | guardians 13-19 | galaxy 20-23 | Nova 24-27 |
             clans 28-32 | hologram 33-36 | roar 37-39 | call 40-41.
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks
from synth import note_midi

NAME = 'asterion_film'
BPM, BEATS_PER_BAR, BARS, LOOP, TAIL = 80, 4, 41, False, 3.0

# name: (bass root, voicing)
CHORD = {
    'Cm': ('C2', ['C4', 'Eb4', 'G4']),
    'Ab': ('Ab1', ['C4', 'Eb4', 'Ab4']),
    'Fm': ('F1', ['C4', 'F4', 'Ab4']),
    'Db': ('Db2', ['Db4', 'F4', 'Ab4']),
    'G': ('G1', ['B3', 'D4', 'G4']),
    'Eb': ('Eb2', ['Eb4', 'G4', 'Bb4']),
    'Bb': ('Bb1', ['D4', 'F4', 'Bb4']),
    'Abmaj': ('Ab1', ['Eb4', 'Ab4', 'C5']),
    'C': ('C2', ['C4', 'E4', 'G4']),
}

PROG = (['Cm', 'Cm', 'Cm', 'Cm'] +                                  # 1-4 the void
        ['Cm', 'Ab', 'Fm', 'G'] +                                   # 5-8 the forge
        ['Cm', 'Db', 'Cm', 'G'] +                                   # 9-12 the feast
        ['Cm', 'Ab', 'Fm', 'Db', 'Cm', 'Ab', 'G'] +                 # 13-19 the guardians
        ['Ab', 'Eb', 'Fm', 'G'] +                                   # 20-23 the galaxy
        ['Cm', 'Db', 'Cm', 'Cm'] +                                  # 24-27 Nova
        ['Eb', 'Bb', 'Abmaj', 'Bb', 'G'] +                          # 28-32 the clans
        ['Cm', 'Ab', 'Fm', 'G'] +                                   # 33-36 the hologram
        ['Cm', 'Db', 'G'] +                                         # 37-39 the roar
        ['C', 'C'])                                                 # 40-41 the call

THEME = 'C4:2 Eb4 G4 | Ab4:3 G4 | F4:1.5 Eb4:.5 Db4:2 | C4:4 |'
MARCH = 'Eb4:1.5 Bb3:.5 Eb4 G4 | F4:2 D4:2 | C4:1.5 Eb4:.5 Ab4 C5 | Bb4:4 |'
OSTINATO = [0, 12, 7, 12, 3, 12, 7, 12]  # eighths over the root


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, loop=LOOP, tail=TAIL, target_lufs=-15.0, end_fade=2.5)
    s.reverb.update(seconds=4.0, t60=3.2, predelay=0.03, ret=0.4, hf=0.35, lf=1.1)
    s.delay.update(beats=0.75, feedback=0.3, damp=3200.0, ret=0.18, to_reverb=0.4)

    drone = s.track('drone', gain_db=-9.0, reverb=0.5, highpass=40)
    choir = s.track('choir', gain_db=-7.0, reverb=0.6, highpass=150)
    strings = s.track('strings', gain_db=-8.0, reverb=0.45, highpass=90)
    stacc = s.track('ostinato', gain_db=-10.0, reverb=0.3, highpass=120, pan=-0.15)
    brass = s.track('brass', gain_db=-6.5, reverb=0.35, highpass=60)
    sub = s.track('sub', gain_db=-10.0, lowpass=160, highpass=30)
    bells = s.track('bells', gain_db=-12.0, reverb=0.6, delay=0.3, highpass=500, pan=0.2)
    arps = s.track('arps', gain_db=-12.0, reverb=0.4, delay=0.35, highpass=300, pan=-0.2)
    drums = s.track('drums', gain_db=-6.0, reverb=0.3)
    snare = s.track('snare', gain_db=-9.0, reverb=0.3)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.4)
    fx = s.track('fx', gain_db=-8.0, reverb=0.5)

    for bar, nb, name in blocks(PROG):
        root, voicing = CHORD[name]
        r = note_midi(root)
        beats = nb * 4 - 0.1
        drone.note(I.pad_warm, bar, 0, beats, r + 12, vel=0.6, attack=1.5, release=2.0, bright=2.0, max_cut=900.0)
        if bar >= 5 and not (24 <= bar <= 27):
            sub.note(I.bass_sub, bar, 0, beats, r if r >= 34 else r + 12, vel=0.6, attack=0.3, release=0.8)
        if 5 <= bar <= 23 or bar >= 28:
            strings.chord(I.strings_legato, bar, 0, beats, [v for v in voicing], vel=0.55 if bar < 13 else 0.65, attack=0.6, release=1.2)
        if 13 <= bar <= 23 or 37 <= bar:
            choir.chord(I.pad_choir, bar, 0, beats, voicing, vel=0.6, vowel='o' if bar < 20 else 'a', attack=0.5, release=1.6)
    choir.chord(I.pad_choir, 1, 0, 15.9, ['C3', 'G3'], vel=0.45, vowel='o', attack=3.0, release=2.0)

    # the void: a heartbeat on the timpani, a few notes of celesta
    for bar in range(1, 5):
        for beat in (0.0, 2.0):
            drums.at(D.tom(55.0, decay=0.35), bar, beat, gain=0.35 + 0.08 * bar)
            drums.at(D.tom(50.0, decay=0.3), bar, beat + 0.33, gain=0.25 + 0.06 * bar)
    for bar, nt in ((2, 'G5'), (3, 'Eb5'), (4, 'C6')):
        bells.note(I.celesta, bar, 1.0, 2.0, nt, vel=0.5)

    # the forge: taiko gathering, a riser into the feast
    for bar in range(6, 9):
        drums.hits(D.taiko(62.0, variant=bar), bar, 'x.......x...x...' if bar < 8 else 'x...x...x.x.xxxx', vel=0.7)
    fx.at(D.riser(s.spb * 8, f0=150, f1=5000, q=2.5, variant=1), 7, 0, gain=0.7)
    fx.at(D.boom(48.0, variant=1), 9, 0, gain=1.0)
    cym.at(D.crash(variant=1), 9, 0)

    # the feast: the titan's theme on low brass
    brass.line(T.brass_low, 9, THEME, vel=0.85, legato=0.92, transpose=-12, bright=2.8, max_cut=2200.0, growl=0.35)
    for bar in range(9, 13):
        drums.hits(D.taiko(58.0, variant=bar), bar, 'x.......x.......', vel=0.8)

    # the guardians: an ostinato in eighths, a strike on every bar
    for bar, name in enumerate(PROG, start=1):
        if not (13 <= bar <= 19 or 33 <= bar <= 36):
            continue
        r = note_midi(CHORD[name][0]) + 24
        for k, iv in enumerate(OSTINATO):
            stacc.note(I.strings_stacc, bar, k * 0.5, 0.4, r + iv, vel=0.75 if k % 2 == 0 else 0.6)
        if bar <= 19:
            drums.at(D.taiko(55.0, variant=bar), bar, 0, gain=0.9)
            drums.at(P.sub_hit(44.0, variant=bar), bar, 0, gain=0.5)
    brass.line(T.brass_low, 15, THEME, vel=0.75, legato=0.9, transpose=-12, bright=3.0, max_cut=2400.0, growl=0.3)
    cym.at(D.crash(variant=2), 13, 0)
    fx.at(D.reverse_cymbal(s.spb * 4, variant=2), 19, 0)

    # the galaxy: wide choir, arpeggios of light
    for bar, name in enumerate(PROG, start=1):
        if 20 <= bar <= 23:
            notes = CHORD[name][1]
            for k in range(8):
                bells.note(I.celesta, bar, k * 0.5, 0.5, notes[k % 3][:-1] + str(int(notes[k % 3][-1]) + 1), vel=0.45)
    fx.at(D.boom(42.0, variant=3), 22, 0, gain=0.8)

    # Nova: a scramble of strings, stabs, the blow — then silence
    for bar in (24, 25):
        r = note_midi(CHORD[PROG[bar - 1]][0]) + 24
        for k in range(16):
            stacc.note(I.strings_stacc, bar, k * 0.25, 0.2, r + (0, 3, 7, 12)[k % 4], vel=0.7 + 0.1 * (k % 4 == 0))
        drums.hits(D.snare(variant=bar), bar, '....x.......x.xx' if bar == 24 else '....x...x.x.xxxx', vel=0.6)
        brass.chord(T.brass_stab, bar, 0, 0.3, CHORD[PROG[bar - 1]][1], vel=0.8, bright=4.0)
    brass.chord(T.brass_stab, 25, 3.5, 0.5, ['C4', 'Db4', 'G4'], vel=1.0, bright=5.0)
    fx.at(D.boom(40.0, variant=4), 25, 3.8, gain=1.0)
    cym.at(D.crash(variant=3), 25, 3.8)
    drone.note(I.pad_warm, 26, 0, 7.9, 'C3', vel=0.45, attack=2.0, release=2.0, bright=1.5, max_cut=700.0)

    # the clans: a march in E-flat major, building
    brass.line(T.brass_low, 28, MARCH, vel=0.8, legato=0.92, bright=3.2, max_cut=2800.0, growl=0.2)
    brass.line(I.lead_brass, 30, 'C5:1.5 Eb5:.5 Ab5 C6 | Bb5:4 |', vel=0.7, max_cut=4500.0)
    for bar in range(28, 33):
        snare.hits(D.snare(tone=200, variant=bar), bar, 'x..xx..x..x.x.x.' if bar < 32 else 'x.x.x.x.xxxxXXXX', vel=0.55 + 0.05 * (bar - 28))
        drums.hits(D.taiko(62.0, variant=bar + 20), bar, 'x.......x.......', vel=0.8)
    cym.at(D.crash(variant=1), 28, 0)
    fx.at(D.riser(s.spb * 4, f0=300, f1=6000, q=2.5, variant=2), 32, 0, gain=0.6)

    # the hologram: cold arpeggios, a pulse
    for bar, name in enumerate(PROG, start=1):
        if 33 <= bar <= 36:
            notes = CHORD[name][1]
            for k in range(16):
                nt = notes[k % 3]
                arps.note(I.pluck, bar, k * 0.25, 0.22, nt[:-1] + str(int(nt[-1]) + 1), vel=0.55 + 0.15 * (k % 4 == 0), t60=0.5, bright=0.8)
            drums.hits(D.kick(f_start=110, f_end=44, decay=0.3), bar, 'x...x...x...x...', vel=0.5)
    fx.at(D.reverse_cymbal(s.spb * 4, variant=3), 36, 0)

    # the roar: the theme at full strength
    fx.at(D.boom(36.0, length=3.0, variant=5), 37, 2.0, gain=1.2)
    fx.at(P.sub_hit(38.0, length=2.4, variant=5), 37, 2.0, gain=1.0)
    cym.at(D.crash(variant=2), 37, 2.0)
    brass.line(T.brass_low, 37, 'r:2 C4 Eb4 | G4:2 Ab4:2 | Db4:2 B3:2 |', vel=1.0, legato=0.95, transpose=-12, bright=3.6, max_cut=3000.0,
               growl=0.45)
    brass.line(I.lead_brass, 37, 'r:2 C5 Eb5 | G5:2 Ab5:2 | Db5:2 B4:2 |', vel=0.8, max_cut=5000.0)
    for bar in (37, 38, 39):
        drums.hits(D.taiko(55.0, variant=bar + 40), bar, '..x.x...x.x.x.xx' if bar < 39 else 'x.x.x.x.xxxxXXXX', vel=0.9)

    # the call: a last chord in C major, the heart once more
    brass.chord(I.lead_brass, 40, 0, 7.5, ['C4', 'E4', 'G4', 'C5'], vel=0.75, bright=3.6, vib=5.0)
    strings.chord(I.strings_legato, 40, 0, 8.0, ['C3', 'G3', 'E4', 'G4', 'C5'], vel=0.7, attack=0.4, release=2.5)
    for beat in (0.0, 4.0):
        drums.at(D.tom(55.0, decay=0.4), 40, beat, gain=0.6)
        drums.at(D.tom(50.0, decay=0.35), 40, beat + 0.33, gain=0.45)
    cym.at(D.crash(variant=1), 40, 0)
    return s
