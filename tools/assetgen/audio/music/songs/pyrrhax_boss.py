"""pyrrhax_boss - Pyrrhax, le Dragon d'Obsidienne (giga boss of Les Confins). C minor with a phrygian
D-flat and a harmonic-minor G major, 138 BPM, 4/4, 44 bars = 76.52 s.

Character: a dragon of black glass rising from rivers of living fire.  An epic, heavy fight:
pounding taikos and toms, a galloping saturated bass that grinds on the phrygian D-flat, the
dragon's motif on growling low brass, a roaring choir, a fiery brass lead, spiccato strings,
brass stabs, cold obsidian chimes, and the dragon itself roaring (a growl pitched in C).

Form:  intro 1-4 (roar, taikos, sub drops, the brass pedal grinding C / D-flat, a rising choir)
       | A1 5-12 (gallop, taikos, spiccato; the dragon motif on low brass)
       | A2 13-20 (the fiery lead takes the motif up, choir stabs, double kicks)
       | B 21-28 (roar, the roaring choir and the lead's soaring theme over off-beat stabs)
       | breakdown 29-32 (half time: obsidian chimes over crackling embers, heartbeat, a roar)
       | climax 33-44 (everything: the theme on lead + choir + brass, a riser back into the loop).
"""
import numpy as np

from dsp import soft_clip, svf
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from music.textures import blocks, fire_crackle
from synth import SR, note_midi, ns, pink, points, saw, sine

NAME = 'pyrrhax_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 138, 4, 44, True

# name: (bass root, choir voicing, spiccato tones (root, fifth, octave, third), obsidian dyad)
CHORD = {
    'Cm': ('C2', ['C4', 'Eb4', 'G4'], ['C4', 'G4', 'C5', 'Eb5'], ['C5', 'G5']),
    'Db': ('Db2', ['Db4', 'F4', 'Ab4'], ['Db4', 'Ab4', 'Db5', 'F5'], ['Db5', 'Ab5']),
    'Ab': ('Ab1', ['C4', 'Eb4', 'Ab4'], ['Ab3', 'Eb4', 'Ab4', 'C5'], ['Ab4', 'Eb5']),
    'Bb': ('Bb1', ['D4', 'F4', 'Bb4'], ['Bb3', 'F4', 'Bb4', 'D5'], ['Bb4', 'F5']),
    'Fm': ('F1', ['C4', 'F4', 'Ab4'], ['F3', 'C4', 'F4', 'Ab4'], ['F4', 'C5']),
    'G': ('G1', ['B3', 'D4', 'G4'], ['G3', 'D4', 'G4', 'B4'], ['G4', 'D5']),
    'Eb': ('Eb2', ['Eb4', 'G4', 'Bb4'], ['Eb4', 'Bb4', 'Eb5', 'G5'], ['Eb5', 'Bb5']),
}
GRIND = {'Cm': 1, 'G': 1}      # the gallop's last 16th: phrygian b2 on C, b9 on G (else the fifth)

PROG = (['Cm', 'Db', 'Cm', 'Db'] +                                          # 1-4 intro
        ['Cm', 'Cm', 'Ab', 'Bb', 'Cm', 'Cm', 'Db', 'G'] +                   # 5-12 A1
        ['Cm', 'Cm', 'Ab', 'Bb', 'Fm', 'Db', 'G', 'G'] +                    # 13-20 A2
        ['Ab', 'Bb', 'Eb', 'Cm', 'Fm', 'Db', 'G', 'G'] +                    # 21-28 B
        ['Cm', 'Db', 'Cm', 'Db'] +                                          # 29-32 breakdown
        ['Cm', 'Ab', 'Bb', 'Cm', 'Fm', 'Db', 'Eb', 'Bb', 'Ab', 'Bb', 'Db', 'G'])   # 33-44 climax

# the dragon's motif (A1, low brass an octave down)
MOTIF = ('C4:1.5 Eb4:.5 G4 C5 | Db5:1.5 C5:.5 G4:2 | Ab4:1.5 G4:.5 F4 Eb4 | F4:3 D4 | '
         'C4:1.5 Eb4:.5 G4 C5 | Eb5:1.5 D5:.5 C5 G4 | Ab4:1.5 F4:.5 Db5:2 | B4:3 G4 |')
# A2: the fiery lead takes it up
LEAD_A2 = ('G5:1.5 C6:.5 Eb6 D6 | C6:1.5 G5:.5 Eb5 G5 | Ab5:1.5 C6:.5 Eb6 Db6 | D6:3 F6 | '
           'F6:1.5 Eb6:.5 C6 Ab5 | Db6:1.5 C6:.5 Ab5 F5 | G5:1.5 B5:.5 D6 F6 | Eb6:2 D6:2 |')
THEME_B = ('C5:2 Eb5 Ab5 | G5:1.5 F5:.5 D5:2 | Eb5:1.5 G5:.5 Bb5 G5 | G5:4 | '
           'Ab5:1.5 G5:.5 F5 C5 | Db5:1.5 F5:.5 Ab5 F5 | G5:1.5 F5:.5 D5 B4 | D5:2 B4:2 |')
THEME_S = ('C5:1.5 Eb5:.5 G5 C6 | Db6:1.5 C6:.5 Ab5:2 | Bb5:1.5 Ab5:.5 G5 F5 | G5:3 Eb5 | '
           'F5:1.5 Ab5:.5 C6 F6 | F6:1.5 Eb6:.5 Db6 Ab5 | G5:1.5 Bb5:.5 Eb6 G6 | F6:3 D6 | '
           'Eb6:1.5 C6:.5 Ab5 C6 | D6:1.5 F6:.5 Bb5 D6 | Db6:1.5 F6:.5 Ab6:2 | G6:2 B5:2 |')
# breakdown: obsidian chimes, the motif's head slowed down
CHIMES = 'C5:3 Db5 | C5:4 | G5:3 Ab5 | G5:2 Db5:2 |'

GALLOP = [(0, 0), (2, 0), (3, 0), (4, 12), (6, 0), (7, 0), (8, 0), (10, 0), (11, 0), (12, 12),
          (14, None), (15, 0)]
SPIC = [0, 1, 2, 1, 3, 1, 2, 1]


def roar(rng, seconds=2.6, f0=65.4):
    """The dragon's roar, pitched on the key: a detuned-saw growl with flutter, its formants
    opening then closing, a breath of fire in the throat and a sub under it (mono)."""
    n = ns(seconds)
    t = np.arange(n) / SR
    f = points(n, [(0, f0 * 0.75), (0.25, f0), (seconds * 0.6, f0 * 0.95), (seconds, f0 * 0.67)], 'exp')
    f = f * (1.0 + 0.02 * np.sin(2 * np.pi * 4.7 * t))
    v = saw(f, n) + 0.8 * saw(f * 1.011, n) + 0.5 * saw(f * 0.5, n)
    v = v * (1.0 - 0.3 * (1.0 + np.sin(2 * np.pi * 27.0 * t + rng.random() * 6.28)))
    f1 = points(n, [(0, 380), (0.35, 720), (seconds * 0.6, 560), (seconds, 330)], 'exp')
    f2 = points(n, [(0, 800), (0.35, 1250), (seconds * 0.6, 950), (seconds, 620)], 'exp')
    voc = svf(v, f1, 3.0, 'bp') + 0.6 * svf(v, f2, 4.0, 'bp') + 0.3 * svf(v, 180.0, 0.8, 'lp')
    breath = svf(pink(n, rng), f2 * 1.4, 0.8, 'lp') * 0.45
    env = points(n, [(0, 0), (0.15, 0.8), (0.4, 1.0), (seconds * 0.65, 0.8), (seconds, 0)])
    body = soft_clip((voc + breath) * env * 0.9, 2.2)
    return body + 0.3 * sine(f * 0.5, n) * env


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=4.0, t60=2.8, predelay=0.03, ret=0.32, hf=0.4, lf=1.15)
    s.delay.update(beats=0.75, feedback=0.3, damp=3000.0, ret=0.18, to_reverb=0.35)
    s.duck_tau = 0.1

    bass = s.track('bass', gain_db=-6.0, duck=0.4, lowpass=2600, highpass=36)
    sub = s.track('sub', gain_db=-7.0, duck=0.25, lowpass=180, highpass=32)
    spic = s.track('spiccato', gain_db=-5.0, reverb=0.25, duck=0.2, highpass=160)
    stabs = s.track('stabs', gain_db=-8.0, reverb=0.3, highpass=110)
    horn = s.track('horn', gain_db=-5.0, reverb=0.32, delay=0.08, highpass=70)
    lead = s.track('lead', gain_db=-7.0, reverb=0.4, delay=0.18, highpass=200)
    strings = s.track('strings', gain_db=-10.0, reverb=0.5, duck=0.25, highpass=100)
    choir = s.track('choir', gain_db=-3.0, reverb=0.6, duck=0.15, highpass=150)
    glass = s.track('obsidian', gain_db=-7.0, reverb=0.55, delay=0.3, highpass=350, pan=0.15)
    kick = s.track('kick', gain_db=-7.5, reverb=0.04)
    snare = s.track('snare', gain_db=-6.0, reverb=0.3)
    hats = s.track('hats', gain_db=0.0, reverb=0.12, pan=-0.2, highpass=4500)
    taiko = s.track('taiko', gain_db=-4.5, reverb=0.35)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.4)
    fire = s.track('fire', gain_db=-14.0, reverb=0.3, highpass=1200)
    dragon = s.track('dragon', gain_db=-7.0, reverb=0.45, delay=0.1, highpass=45)
    fx = s.track('fx', gain_db=-9.0, reverb=0.45)

    # sustained layers per chord block
    for bar, nb, name in blocks(PROG):
        root, ch_v, tones, dyad = CHORD[name]
        r = note_midi(root)
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r - 12 if r - 12 >= 28 else r, vel=0.72, attack=0.15,
                 release=0.8, drive=1.3)
        if bar <= 4 or 21 <= bar <= 28 or bar >= 33:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.55 if bar <= 4 else 0.74,
                        vowel='o' if bar <= 4 else 'a', attack=0.3 if bar <= 4 else 0.15, release=0.9)
        elif 29 <= bar <= 32:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, [note_midi(x) - 12 for x in ch_v], vel=0.6,
                        vowel='u', attack=0.5, release=1.0)
        if bar >= 13 and not 29 <= bar <= 32:
            strings.chord(I.strings_legato, bar, 0, nb * 4 - 0.2, [r + 24, r + 31], vel=0.55, attack=0.4,
                          release=0.8)
        if bar <= 4 or 29 <= bar <= 32:
            # the brass pedal: low horns grinding between C and D-flat (hushed in the breakdown)
            horn.chord(T.brass_low, bar, 0, nb * 4 - 0.2, [r + 12, r + 19], vel=0.6 if bar <= 4 else 0.42,
                       attack=0.3, release=0.6, bright=2.2, max_cut=1400.0, drive=1.8, vib=0.0, growl=0.2)

    for bar, name in enumerate(PROG, start=1):
        root, ch_v, tones, dyad = CHORD[name]
        r = note_midi(root)
        breakdown = 29 <= bar <= 32
        # bass: pedal 8ths in the intro, gallop everywhere else, silent in the breakdown (the sub
        # alone holds the floor)
        if bar <= 4:
            for k in range(8):
                bass.note(T.bass_forge, bar, k * 0.5, 0.3, r, vel=0.85 if k % 2 == 0 else 0.62,
                          peak_mul=4.0, drive=2.6)
        elif not breakdown:
            for step, iv in GALLOP:
                if iv is None:
                    iv = GRIND.get(name, 7)
                bass.note(T.bass_forge, bar, step * 0.25, 0.4 if step % 4 == 0 else 0.22, r + iv,
                          vel=0.95 if step in (0, 8) else 0.74, peak_mul=5.5, drive=3.2)
        # spiccato 16ths
        if 5 <= bar <= 28 or bar >= 33:
            for k in range(16):
                v = (0.76 if k % 4 == 0 else 0.54) * (0.88 if bar <= 12 else 1.0)
                spic.note(I.strings_stacc, bar, k * 0.25, 0.25, tones[SPIC[k % 8]], vel=v, decay=0.06,
                          pan=(-0.35 if k % 2 else 0.35))
        # brass stabs: choir-like hits in A2, off-beats in B and the climax
        if 13 <= bar <= 20:
            for beat in (0, 1.5, 3):
                stabs.chord(T.brass_stab, bar, beat, 0.32, ch_v, vel=0.85 if beat == 0 else 0.68)
        elif 21 <= bar <= 28 or bar >= 33:
            for beat in (0.75, 1.5, 2.75, 3.5):
                stabs.chord(T.brass_stab, bar, beat, 0.3, ch_v, vel=0.72 if beat in (1.5, 3.5) else 0.58)
        # obsidian glass: a struck dyad every second bar of the climax and the B
        if (21 <= bar <= 28 or bar >= 33) and bar % 2 == 1:
            glass.chord(T.ice_bell, bar, 0, 2, dyad, vel=0.5, decay=1.8, bright=0.8)

    # themes
    horn.line(T.brass_low, 5, MOTIF, vel=0.95, legato=0.92, transpose=-12, bright=3.4, max_cut=2600.0,
              growl=0.25)
    horn.line(T.brass_low, 13, LEAD_A2, vel=0.6, legato=0.92, transpose=-24, bright=2.8, max_cut=2000.0,
              growl=0.2)
    lead.line(I.lead_brass, 13, LEAD_A2, vel=0.84, legato=0.92, bright=5.0, max_cut=5000.0, drive=2.0)
    lead.line(I.lead_brass, 21, THEME_B, vel=0.9, legato=0.92, bright=5.0, max_cut=5200.0, drive=2.0)
    horn.line(T.brass_low, 21, THEME_B, vel=0.62, legato=0.92, transpose=-24, bright=3.0, max_cut=2200.0,
              growl=0.3)
    glass.line(T.ice_bell, 29, CHIMES, vel=0.75, decay=2.6, bright=0.9)
    glass.line(T.glass_pluck, 29, CHIMES, vel=0.5, transpose=-12, decay=0.9)
    lead.line(I.lead_brass, 33, THEME_S, vel=0.95, legato=0.92, bright=5.4, max_cut=5600.0, drive=2.2)
    choir.line(I.pad_choir, 33, THEME_S, vel=0.5, legato=0.98, transpose=-12, vowel='a', attack=0.08,
               release=0.5)
    horn.line(T.brass_low, 33, THEME_S, vel=0.7, legato=0.92, transpose=-24, bright=3.2, max_cut=2200.0,
              growl=0.3)

    # ---- the dragon
    dragon.at(roar(s.rng('roar1'), 2.8, 65.4), 1, 0.0, gain=1.0)
    dragon.at(roar(s.rng('roar21'), 2.4, 51.9), 21, 0.0, gain=0.8, pan=-0.2)
    dragon.at(roar(s.rng('roar32'), 2.0, 69.3), 32, 1.0, gain=0.95, pan=0.2)
    # embers crackling through the intro and the breakdown
    for bar in (1, 29):
        n = s.pos(bar + 4) - s.pos(bar)
        for ch in (-0.6, 0.6):
            fire.at(fire_crackle(n, s.rng(f'embers{bar}{ch}'), rate=7.0), bar, 0, pan=ch)

    # ---- drums
    kk = D.kick(f_start=150, f_end=44, pitch_tau=0.03, decay=0.3, click=0.35, drive=2.4, variant=71)
    sn = D.snare(tone=170, tone_decay=0.08, noise_decay=0.18, noise_lp=7500, tone_mix=0.7, roomy=0.45,
                 variant=71)
    cl = D.clap(variant=7)
    hh = D.hat(decay=0.03, length=0.1, bright=1.1, metal=0.55, variant=71)
    tk = D.taiko(50.0, variant=71)
    tk_hi = D.taiko(78.0, variant=72)
    tm_lo = D.tom(92.0, variant=71)
    tm_hi = D.tom(138.0, variant=71)
    heart = P.sub_hit(44.0, length=0.6, variant=72)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            taiko.hits(tk, bar, 'X.......x.x.....' if bar % 2 else 'X.....x.X...x.x.', duck=True)
            taiko.hits(tk_hi, bar, '....o.......o..o', vel=0.8, pan=0.3)
            if bar >= 3:
                hats.hits(hh, bar, '..x...x...x...x.', vel=0.5)
            continue
        if 29 <= bar <= 32:
            taiko.hits(tk, bar, 'X.......x.......', vel=0.8, duck=True)
            fx.hits(heart, bar, '....x..x........', vel=0.8)
            hats.hits(hh, bar, '........x.......', vel=0.4)
            continue
        big = bar >= 33
        B = 21 <= bar <= 28 or big
        if bar <= 12:
            kick.hits(kk, bar, 'x...x...x...x..x', duck=True)
        else:
            kick.hits(kk, bar, 'x.xxx.xxx.xxx.xx' if not B else 'x..xx..xx..xx.xx', duck=True)
        snare.hits(sn, bar, '....x.......x...')
        snare.hits(cl, bar, '....x.......x...', vel=0.45)
        hats.hits(hh, bar, 'x.xgx.xgx.xgx.xg' if not B else 'xxxgxxxgxxxgxxxg', vel=0.7)
        taiko.hits(tk, bar, 'X..x..x.X.....x.' if not big else 'X..xX..xX.x.X..x', vel=0.85)
        taiko.hits(tk_hi, bar, '....x.......x...' if B else '............x...', vel=0.7, pan=0.3)
    for bar in (12, 28):
        snare.hits(tm_hi, bar, '........x.x.x...', pan=0.3)
        snare.hits(tm_lo, bar, '..............xx', pan=-0.3)
    snare.hits(sn, 20, '........oooxxxXX', vel=0.8)
    snare.hits(sn, 32, '........o.o.xxXX', vel=0.85)
    snare.hits(sn, 44, 'oooooooooooxxxXX', vel=0.9)
    snare.hits(tm_lo, 44, 'x...x...x...x...', vel=0.8, pan=-0.3)
    for bar in (1, 29):
        fx.at(P.sub_hit(38.0, length=2.5, variant=bar + 70), bar, 0, gain=1.2)
    for bar in (4, 12, 20, 28):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar + 70), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=150, f1=7000, q=3.0, variant=71), 31, 0, gain=0.9)
    fx.at(D.riser(s.spb * 8, f0=200, f1=8000, q=3.0, variant=72), 43, 0, gain=0.8)
    for bar in (1, 5, 13, 21, 25, 33, 37, 41):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    fx.at(D.boom(40.0, variant=71), 21, 0, gain=0.9)
    fx.at(D.boom(36.0, variant=72), 33, 0, gain=1.0)
    return s
