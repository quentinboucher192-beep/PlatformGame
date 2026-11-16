"""phenix_boss - le Phénix Stellaire (giga boss of Les Confins). B-flat major with the lydian E natural,
dying into G minor, reborn a whole tone higher in C major; 156 BPM, 4/4, 48 bars = 73.85 s.

Character: a bird of starfire, bright and soaring rather than menacing.  A heroic brass fanfare
over driving spiccato strings and a plucked octave bass, a bright choir, sparks of celesta and
glass bells; then the phoenix burns down to ash (a lonely flute over embers and a heartbeat) and
bursts back to life one tone higher, everything brighter, before falling back into the loop on
the dominant of B-flat.

Form:  intro 1-4 (shimmering strings and sparks, a rising choir, the fanfare's call)
       | A1 5-12 (the theme on brass, spiccato, octave bass, march drums)
       | A2 13-20 (the theme's answer on high strings, brass counter-line, stabs)
       | B 21-28 (soaring: choir, glass lead doubling the brass, double-time hats)
       | ashes 29-32 (G minor: flute, embers, heartbeat; a riser, then the G major dominant)
       | rebirth 33-48 (C major: the theme a tone higher, everything; ends on F, the way home).
"""
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks, fire_crackle
from synth import note_midi

NAME = 'phenix_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 156, 4, 48, True

# name: (bass root, brass voicing, choir voicing, spiccato tones (root, fifth, third, octave))
CHORD = {
    'Bb': ('Bb1', ['D4', 'F4', 'Bb4'], ['D4', 'F4', 'Bb4'], ['Bb4', 'F5', 'D5', 'Bb5']),
    'C': ('C2', ['E4', 'G4', 'C5'], ['E4', 'G4', 'C5'], ['C5', 'G5', 'E5', 'C6']),
    'F': ('F1', ['C4', 'F4', 'A4'], ['C4', 'F4', 'A4'], ['F4', 'C5', 'A4', 'F5']),
    'Gm': ('G1', ['D4', 'G4', 'Bb4'], ['D4', 'G4', 'Bb4'], ['G4', 'D5', 'Bb4', 'G5']),
    'Eb': ('Eb2', ['Eb4', 'G4', 'Bb4'], ['Eb4', 'G4', 'Bb4'], ['Eb4', 'Bb4', 'G4', 'Eb5']),
    'Dm': ('D2', ['D4', 'F4', 'A4'], ['D4', 'F4', 'A4'], ['D4', 'A4', 'F4', 'D5']),
    'Cm': ('C2', ['C4', 'Eb4', 'G4'], ['C4', 'Eb4', 'G4'], ['C5', 'G5', 'Eb5', 'C6']),
    'G': ('G1', ['D4', 'G4', 'B4'], ['D4', 'G4', 'B4'], ['G4', 'D5', 'B4', 'G5']),
    'Am': ('A1', ['C4', 'E4', 'A4'], ['C4', 'E4', 'A4'], ['A4', 'E5', 'C5', 'A5']),
}

PROG = (['Bb', 'C', 'Bb', 'C'] +                                    # 1-4 intro (lydian shimmer)
        ['Bb', 'F', 'Gm', 'Eb', 'Bb', 'F', 'Eb', 'F'] +             # 5-12 A1
        ['Gm', 'Eb', 'Bb', 'F', 'Gm', 'C', 'Eb', 'F'] +             # 13-20 A2
        ['Eb', 'F', 'Dm', 'Gm', 'Eb', 'C', 'F', 'F'] +              # 21-28 B
        ['Gm', 'Eb', 'Cm', 'G'] +                                   # 29-32 ashes
        ['C', 'G', 'Am', 'F', 'C', 'G', 'F', 'G'] +                 # 33-40 rebirth (A1 a tone up)
        ['Am', 'F', 'C', 'G', 'F', 'G', 'C', 'F'])                  # 41-48 rebirth, home on F

CALL = 'F4:1 Bb4:1 D5:2 | E5:1.5 D5:.5 C5:2 |'
THEME = ('F4:1 Bb4:1 D5:1.5 C5:.5 | C5:3 A4 | Bb4:1 D5:1 G5:1.5 F5:.5 | Eb5:3 G4 | '
         'F4:1 Bb4:1 D5:1.5 E5:.5 | F5:3 C5 | Bb4:1.5 C5:.5 Eb5 G5 | F5:2 A4:2 |')
ANSWER = ('D5:1.5 G5:.5 Bb5:2 | Bb5:1.5 G5:.5 Eb5:2 | F5:1 Bb5:1 D6:2 | C6:3 A5 | '
          'Bb5:1.5 A5:.5 G5 D5 | E5:1.5 G5:.5 C6:2 | Bb5:1.5 G5:.5 Eb6:2 | C6:2 A5 F5 |')
SOAR = ('G4:2 Bb4 Eb5 | F5:3 C5 | D5:1.5 F5:.5 A5:2 | G5:3 D5 | '
        'Eb5:1.5 F5:.5 G5 Bb5 | C6:1.5 G5:.5 E5:2 | F5 A5 C6 Eb6 | C6:2 A5:2 |')
ASHES = 'D5:3 Bb4 | G4:4 | Eb5:3 D5 | B4:4 |'
REBIRTH_B = ('E5:1.5 A5:.5 C6:2 | C6:1.5 A5:.5 F5:2 | G5 C6 E6:2 | D6:3 B5 | '
             'A5:1.5 C6:.5 F6:2 | G6:2 D6 B5 | E6:2 C6:2 | A5:2 F5 C5 |')
SPIC = [0, 1, 2, 1, 3, 1, 2, 1]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=3.6, t60=2.6, predelay=0.025, ret=0.32, hf=0.5, lf=1.0)
    s.delay.update(beats=0.75, feedback=0.32, damp=4500.0, ret=0.2, to_reverb=0.35)
    s.duck_tau = 0.09

    bass = s.track('bass', gain_db=-7.0, duck=0.35, lowpass=3200, highpass=38)
    sub = s.track('sub', gain_db=-9.0, duck=0.25, lowpass=180, highpass=34)
    spic = s.track('spiccato', gain_db=-4.5, reverb=0.28, duck=0.15, highpass=200)
    strings = s.track('strings', gain_db=-6.0, reverb=0.45, highpass=120)
    pads = s.track('string pads', gain_db=-11.0, reverb=0.5, duck=0.2, highpass=120)
    lead = s.track('brass lead', gain_db=-8.5, reverb=0.38, delay=0.15, highpass=180)
    glass = s.track('glass', gain_db=-12.0, reverb=0.5, delay=0.3, highpass=600, pan=0.2)
    horns = s.track('horns', gain_db=-9.0, reverb=0.35, highpass=80)
    stabs = s.track('stabs', gain_db=-9.0, reverb=0.3, highpass=150)
    choir = s.track('choir', gain_db=-4.0, reverb=0.6, duck=0.12, highpass=180)
    sparks = s.track('sparks', gain_db=-6.0, reverb=0.55, delay=0.35, highpass=900, pan=-0.2)
    flute = s.track('flute', gain_db=-6.0, reverb=0.6, delay=0.3, highpass=300)
    kick = s.track('kick', gain_db=-8.0, reverb=0.04)
    snare = s.track('snare', gain_db=-7.0, reverb=0.3)
    hats = s.track('hats', gain_db=-1.0, reverb=0.12, pan=0.2, highpass=5000)
    timp = s.track('timpani', gain_db=-6.0, reverb=0.4, highpass=40)
    cym = s.track('cymbals', gain_db=-11.0, reverb=0.4)
    fire = s.track('fire', gain_db=-8.0, reverb=0.3, highpass=1500)
    fx = s.track('fx', gain_db=-9.0, reverb=0.45)

    for bar, nb, name in blocks(PROG):
        root, br_v, ch_v, tones = CHORD[name]
        r = note_midi(root)
        ashes = 29 <= bar <= 32
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r + 12 if r < 34 else r, vel=0.7, attack=0.05, release=0.5)
        if bar <= 4 or bar >= 21:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1,
                        [note_midi(x) - 12 for x in ch_v] if ashes else ch_v,
                        vel=0.5 if bar <= 4 else (0.45 if ashes else 0.72), vowel='o' if ashes else 'a',
                        attack=0.6 if bar <= 4 or ashes else 0.12, release=1.0)
        if not ashes:
            pads.chord(I.strings_legato, bar, 0, nb * 4 - 0.2, [r + 24, r + 31, r + 36], vel=0.5, attack=0.3,
                       release=0.7, bright=4.5)
        else:
            horns.chord(T.brass_low, bar, 0, nb * 4 - 0.2, [r + 12, r + 19], vel=0.4, attack=0.6, release=0.8,
                        bright=1.8, max_cut=1000.0, vib=0.0)

    for bar, name in enumerate(PROG, start=1):
        root, br_v, ch_v, tones = CHORD[name]
        r = note_midi(root)
        ashes = 29 <= bar <= 32
        reborn = bar >= 33
        # octave bass in 8ths (intro: on the beat only)
        if not ashes:
            for k in range(8 if bar > 4 else 4):
                step = 0.5 if bar > 4 else 1.0
                bass.note(I.bass_pluck, bar, k * step, 0.4, r + (12 if k % 2 else 0) + 12,
                          vel=0.9 if k % 2 == 0 else 0.7, peak=6.0, tau=0.1, drive=1.6)
        # spiccato: 8ths in A, 16ths in B and the rebirth, soft shimmer in the intro
        if bar <= 4:
            for k in range(16):
                spic.note(I.strings_stacc, bar, k * 0.25, 0.25, tones[SPIC[k % 8]], vel=0.35 + 0.1 * (k % 4 == 0),
                          decay=0.05, pan=(-0.35 if k % 2 else 0.35))
        elif not ashes:
            sixteenth = bar >= 21
            for k in range(16 if sixteenth else 8):
                st = 0.25 if sixteenth else 0.5
                v = 0.78 if k % (4 if sixteenth else 2) == 0 else 0.56
                spic.note(I.strings_stacc, bar, k * st, st, tones[SPIC[k % 8]], vel=v, decay=0.07,
                          pan=(-0.35 if k % 2 else 0.35))
        # brass stabs: the fanfare's rhythm on beats 1, 2-and, 4
        if 13 <= bar <= 28 or reborn:
            for beat in (0, 1.5, 3):
                stabs.chord(T.brass_stab, bar, beat, 0.3, br_v,
                            vel=0.85 if beat == 0 else 0.65, bright=5.0, max_cut=5000.0)
        # sparks: celesta flickers on the chord, two per bar (intro, B, rebirth)
        if bar <= 4 or 21 <= bar <= 28 or reborn:
            for k, beat in enumerate((0.5, 2.5)):
                sparks.note(I.celesta, bar, beat, 0.5, note_midi(tones[(k * 2 + bar) % 4]) + 12, vel=0.5)

    lead.line(I.lead_brass, 3, CALL, vel=0.8, legato=0.92, bright=5.0, max_cut=5500.0)
    lead.line(I.lead_brass, 5, THEME, vel=0.9, legato=0.92, bright=5.0, max_cut=5500.0)
    strings.line(I.strings_legato, 13, ANSWER, vel=0.8, legato=0.97, attack=0.08, bright=5.0)
    horns.line(T.brass_low, 13, ANSWER, vel=0.6, legato=0.92, transpose=-24, bright=3.0, max_cut=2400.0)
    lead.line(I.lead_brass, 21, SOAR, vel=0.95, legato=0.92, bright=5.5, max_cut=6000.0)
    glass.line(I.lead_glass, 21, SOAR, vel=0.6, transpose=12, vib=8.0)
    strings.line(I.strings_legato, 21, SOAR, vel=0.55, legato=0.97, transpose=-12, attack=0.1, bright=4.0)
    flute.line(I.lead_flute, 29, ASHES, vel=0.8, legato=0.95, vib=14.0)
    lead.line(I.lead_brass, 33, THEME, vel=1.0, legato=0.92, transpose=2, bright=5.8, max_cut=6200.0)
    strings.line(I.strings_legato, 33, THEME, vel=0.6, legato=0.97, transpose=14, attack=0.08, bright=5.0)
    horns.line(T.brass_low, 33, THEME, vel=0.7, legato=0.92, transpose=-10, bright=3.2, max_cut=2600.0)
    lead.line(I.lead_brass, 41, REBIRTH_B, vel=1.0, legato=0.92, bright=5.8, max_cut=6200.0)
    glass.line(I.lead_glass, 41, REBIRTH_B, vel=0.65, transpose=12, vib=9.0)
    strings.line(I.strings_legato, 41, REBIRTH_B, vel=0.6, legato=0.97, transpose=-12, attack=0.1, bright=4.5)
    horns.line(T.brass_low, 41, REBIRTH_B, vel=0.65, legato=0.92, transpose=-24, bright=3.0, max_cut=2400.0)

    # ---- drums
    kk = D.kick(f_start=170, f_end=50, pitch_tau=0.026, decay=0.24, click=0.4, drive=1.8, variant=101)
    sn = D.snare(tone=200, tone_decay=0.06, noise_decay=0.15, noise_lp=9000, tone_mix=0.55, roomy=0.35,
                 variant=101)
    hh = D.hat(decay=0.026, length=0.09, bright=1.2, metal=0.5, variant=101)
    tp_lo = D.taiko(64.0, variant=101)
    tp_hi = D.taiko(96.0, variant=102)
    heart = P.sub_hit(48.0, length=0.6, variant=102)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            timp.hits(tp_lo, bar, 'X...........o.o.' if bar < 4 else 'X.......oooooooo', vel=0.8)
            continue
        if 29 <= bar <= 32:
            fx.hits(heart, bar, 'x..x............', vel=0.75)
            if bar == 32:
                timp.hits(tp_lo, bar, '........oooxxxXX', vel=0.8)
            continue
        soar = bar >= 21
        kick.hits(kk, bar, 'x..x..x.x...x...' if not soar else 'x..x..x.x..x..x.', duck=True)
        snare.hits(sn, bar, '....x.......x...' if bar % 4 else '....x.......x.xx')
        hats.hits(hh, bar, 'x.x.x.x.x.x.x.x.' if not soar else 'xxxgxxxgxxxgxxxg', vel=0.7)
        timp.hits(tp_lo if bar % 2 else tp_hi, bar, 'X.......x.......', vel=0.7)
    snare.hits(sn, 12, '........o.o.xxXX', vel=0.75)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 28, 'oooooooooooxxxXX', vel=0.85)
    snare.hits(sn, 48, '............xxXX', vel=0.8)
    # fire: the phoenix's flames crackling over the intro, the ashes and the rebirth
    for bar, nbars, rate in ((1, 4, 9.0), (29, 4, 7.0), (33, 4, 12.0)):
        n = s.pos(bar + nbars) - s.pos(bar)
        for ch in (-0.6, 0.6):
            fire.at(fire_crackle(n, s.rng(f'fire{bar}{ch}'), rate=rate), bar, 0, pan=ch)
    for bar in (5, 13, 21, 33, 41):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    for bar in (12, 20, 40):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar + 100), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=200, f1=9000, q=3.0, variant=101), 31, 0, gain=1.0)
    fx.at(D.riser(s.spb * 4, f0=300, f1=7000, q=3.0, variant=102), 4, 0, gain=0.7)
    fx.at(D.boom(50.0, variant=101), 33, 0, gain=1.0)
    fx.at(P.sub_hit(46.0, length=2.0, variant=103), 29, 0, gain=1.0)
    return s
