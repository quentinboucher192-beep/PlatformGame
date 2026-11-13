"""sylvanox_depths - the Sylvanox theme sunk into the bioluminescent caverns (levels 2-3).
D minor turned phrygian (Eb major, A7b9), 90 BPM, 4/4, 36 bars = 96.0 s (one 16th note =
exactly 7350 samples).

It reuses the melodies of sylvanox_level an octave lower with the E flattened, over darker
harmony, a low marimba ostinato and cave drips.
Form:  intro 1-4 (heartbeat kick, low ostinato, drips, dark pad)
       | A 5-20 (A1: the Sylvanox ocarina theme, an octave low and phrygian; A2: the second half
         of the theme on glass lead over soft kit and twinkling plucks)
       | B 21-32 (half time: fragments of the Sylvanox B theme on glass lead + plucks over a
         dark 'ooh' choir, toms)
       | outro 33-36 (Dm-Eb-Bb-A7, thins out and falls back into the intro).
"""
import numpy as np

from music import drums as D
from music import instruments as I
from music import percussion as P
from music.sequencer import Song
from music.textures import blocks, air_layer
from synth import SR, ns, sine, perc, note_midi

NAME = 'sylvanox_depths'
BPM, BEATS_PER_BAR, BARS, LOOP = 90, 4, 36, True

# name: (bass, pad voicing, ostinato tones (root, fifth, octave, colour))
CHORD = {
    'Dm':   ('D2', ['F3', 'A3', 'D4'], ['D3', 'A3', 'D4', 'F4']),
    'Eb':   ('Eb2', ['G3', 'Bb3', 'D4'], ['Eb3', 'Bb3', 'D4', 'G4']),
    'Bb':   ('Bb1', ['F3', 'A3', 'D4'], ['Bb2', 'F3', 'D4', 'A3']),
    'Gm':   ('G1', ['F3', 'Bb3', 'D4'], ['G2', 'D3', 'Bb3', 'F3']),
    'A7':   ('A1', ['G3', 'C#4', 'E4'], ['A2', 'E3', 'C#4', 'G3']),
    'A7b9': ('A1', ['G3', 'C#4', 'Bb3'], ['A2', 'E3', 'C#4', 'Bb3']),
}

PROG = (['Dm', 'Dm', 'Eb', 'Dm'] +                                   # 1-4 intro
        ['Dm', 'Eb', 'Dm', 'Bb', 'Gm', 'Gm', 'Eb', 'A7'] +           # 5-12 A1
        ['Dm', 'Eb', 'Dm', 'Bb', 'Gm', 'Eb', 'A7b9', 'A7'] +         # 13-20 A2
        ['Bb', 'A7', 'Dm', 'Dm', 'Gm', 'Eb', 'A7', 'A7'] +           # 21-28 B1
        ['Bb', 'Gm', 'Eb', 'A7b9'] +                                 # 29-32 B2
        ['Dm', 'Eb', 'Bb', 'A7'])                                    # 33-36 outro

THEME_A1 = ('D4:1.5 Eb4:.5 F4 A4 | G4:1.5 F4:.5 Eb4:2 | D4:1.5 C4:.5 D4 F4 | A3:3 r | '
            'Bb3 D4 F4 A4 | G4:1.5 F4:.5 D4:2 | Eb4:1.5 D4:.5 Eb4 A4 | C#4:3 r |')
THEME_A2 = ('A4:1.5 G4:.5 F4 Eb4 | G4 Bb4 Eb5:2 | D5:1.5 C5:.5 A4 F4 | F4:3 r | '
            'Bb4:1.5 A4:.5 G4 D4 | Eb4:1.5 F4:.5 G4 Bb4 | Bb4:2 G4 E4 | E4:2 C#4 A3 |')
THEME_B = ('D5:3 F5 | E5:2 C#5:2 | F5:1.5 E5:.5 D5 A4 | D5:3 r | '
           'Bb5:2 G5 D5 | Eb5:2 G5 Bb5 | A5:1.5 G5:.5 E5 C#5 | E5:3 r | '
           'D5:2 F5 A5 | G5:1.5 F5:.5 D5 Bb4 | Bb4:1.5 G4:.5 Eb4 G4 | Bb4:2 C#5:2 |')

OSTINATO = [0, 1, 2, 1, 3, 1, 2, 1]
OST_ACCENT = [1.0, 0.7, 0.75, 0.95, 0.7, 0.75, 0.9, 0.7]   # the 3+3+2 feel of sylvanox_level
TWINKLE = 'x..x..x...x..x..'


def _drip(rng):
    d = 0.2
    n = ns(d)
    f0 = rng.uniform(650, 1100)
    f = f0 * (1.0 + 1.3 * (1 - np.exp(-np.arange(n) / SR / 0.012)))
    return sine(f, n) * perc(n, 0.001, 0.035)


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-14.0)
    s.reverb.update(seconds=4.5, t60=3.4, predelay=0.03, ret=0.4, hf=0.3, lf=1.2)
    s.delay.update(beats=0.75, feedback=0.42, damp=2400.0, ret=0.3, to_reverb=0.45)
    s.duck_tau = 0.13

    pad = s.track('pad', gain_db=-11.0, reverb=0.5, duck=0.25, highpass=130)
    choir = s.track('choir', gain_db=-6.0, reverb=0.7, highpass=150)
    bass = s.track('bass', gain_db=-8.0, duck=0.3, lowpass=1200, highpass=35)
    mar = s.track('marimba', gain_db=-1.0, reverb=0.3, delay=0.15, highpass=90)
    twk = s.track('pluck', gain_db=7.0, reverb=0.45, delay=0.5, highpass=250)
    lead = s.track('lead', gain_db=-3.0, reverb=0.45, delay=0.25, highpass=150)
    kick = s.track('kick', gain_db=-6.0, reverb=0.08)
    snare = s.track('snare', gain_db=-8.0, reverb=0.35)
    hats = s.track('hats', gain_db=2.0, reverb=0.15, pan=0.2)
    shk = s.track('shaker', gain_db=0.0, reverb=0.2, pan=-0.25)
    toms = s.track('toms', gain_db=-10.0, reverb=0.4)
    drips = s.track('drips', gain_db=-8.0, reverb=0.9, delay=0.25)
    fx = s.track('fx', gain_db=-13.0, reverb=0.5)
    air = s.track('air', gain_db=-18.0, reverb=0.3)

    # ---- harmony
    for bar, nb, name in blocks(PROG):
        b, voicing, _ = CHORD[name]
        pad.chord(I.pad_warm, bar, 0, nb * 4 - 0.25, voicing, vel=0.7, attack=0.9, release=1.8,
                  bright=4.0, max_cut=2600.0, detune=12.0)
        if 21 <= bar <= 32:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, voicing, vel=0.6, vowel='u', attack=0.6,
                        release=1.4)

    for bar, name in enumerate(PROG, start=1):
        b, voicing, tones = CHORD[name]
        quiet = bar <= 4 or bar >= 33
        half = 21 <= bar <= 32
        # low marimba ostinato (8ths), sparser in B
        for k in range(8):
            if half and k % 2:
                continue
            v = OST_ACCENT[k] * (0.55 if quiet else 0.66)
            mar.note(I.marimba, bar, k * 0.5, 0.5, tones[OSTINATO[k]], vel=v,
                     pan=-0.25 if k % 2 == 0 else 0.15, bright=0.85)
        root = note_midi(b)
        if quiet:
            bass.note(I.bass_sub, bar, 0, 3.75, root, vel=0.8, attack=0.05, release=0.4)
        elif half:
            bass.note(I.bass_sub, bar, 0, 1.8, root, vel=0.85, attack=0.02, release=0.3)
            bass.note(I.bass_sub, bar, 2.5, 1.3, root, vel=0.7, attack=0.02, release=0.3)
        else:
            for beat, dur, iv in ((0, 1.25, 0), (1.5, 0.45, 0), (2, 0.9, 0), (3, 0.45, 12),
                                  (3.5, 0.45, 7)):
                bass.note(I.bass_pluck, bar, beat, dur, root + iv, vel=0.85 if beat == 0 else 0.68,
                          peak=3.5, floor=1.3, tau=0.1, q=1.0, sub=0.7, drive=1.2)

    # ---- twinkling plucks (A2): low and sparse
    for bar, name in enumerate(PROG, start=1):
        if not 13 <= bar <= 20:
            continue
        tones = CHORD[name][2]
        hits = [i for i, c in enumerate(TWINKLE) if c == 'x']
        for j, step in enumerate(hits):
            nt = note_midi(tones[(3, 2, 1, 3, 2)[j % 5]]) + 12
            twk.note(I.pluck, bar, step * 0.25, 0.4, nt, vel=0.5 if j == 0 else 0.4, t60=1.1,
                     bright=0.55, pan=(-0.5 if j % 2 else 0.5))

    # ---- melodies
    lead.line(I.lead_flute, 5, THEME_A1, vel=0.85, legato=0.95, vib=10.0, breath=0.08)
    lead.line(I.lead_glass, 13, THEME_A2, vel=0.78, legato=0.97, transpose=12)
    lead.line(I.lead_glass, 21, THEME_B, vel=0.72, legato=0.97, transpose=-12)
    twk.line(I.pluck, 21, THEME_B, vel=0.4, t60=1.4, bright=0.5, pan=0.3)
    lead.line(I.marimba, 33, 'D4:1.5 Eb4:.5 F4 A4 | G4:4 | r:4 | r:4 |', vel=0.5, pan=0.3)

    # ---- drums
    k_soft = D.kick(f_start=110, f_end=46, decay=0.26, click=0.12, drive=1.3, variant=9)
    sn = D.snare(tone=180, noise_decay=0.14, noise_lp=5500, tone_mix=0.55, roomy=0.45, variant=9)
    rim = D.rim(variant=3)
    ch = D.hat(decay=0.03, length=0.1, bright=0.9, metal=0.4, variant=9)
    sh = D.shaker(variant=7, center=4500.0)
    fd = P.frame_drum(80.0, variant=3)
    for bar in range(1, BARS + 1):
        if bar <= 4 or bar >= 33:
            kick.hits(k_soft, bar, 'x..o............' if bar % 2 else 'x..o........x...', vel=0.85,
                      duck=True)
            shk.hits(sh, bar, 'g.g.g.g.g.g.g.g.', vel=0.7)
            if bar in (4, 36):
                snare.hits(rim, bar, '............o...', vel=0.7)
            continue
        if 21 <= bar <= 32:
            kick.hits(k_soft, bar, 'x.........o.....', duck=True)
            snare.hits(sn, bar, '........x.......', vel=0.8)
            toms.hits(fd, bar, '......o.......o.', vel=0.7, pan=0.3)
            hats.hits(ch, bar, 'x...x...x...x...', vel=0.5)
            shk.hits(sh, bar, 'gogogogogogogogo', vel=0.6)
            continue
        A2 = bar >= 13
        kick.hits(k_soft, bar, 'x.....x.x.......' if not A2 else 'x.....x.x.....o.', duck=True)
        snare.hits(sn, bar, '....x.......x...' if not A2 else '....x......gx...', vel=0.75)
        hats.hits(ch, bar, 'x.o.x.o.x.o.x.o.' if not A2 else 'xgo.xgo.xgo.xgo.', vel=0.75)
        if A2:
            shk.hits(sh, bar, 'gggggggggggggggg', vel=0.7)
    for bar in (12, 20, 28):
        toms.hits(D.tom(160.0, variant=9), bar, '........x.x.....', pan=0.3)
        toms.hits(D.tom(120.0, variant=9), bar, '............x.o.', pan=0.0)
        toms.hits(D.tom(90.0, variant=9), bar, '..............x.', pan=-0.3)
    fx.at(D.reverse_cymbal(s.spb * 2), 4, 2.0)
    fx.at(D.reverse_cymbal(s.spb * 2, variant=2), 20, 2.0)
    fx.at(D.riser(s.spb * 4, f0=300, f1=4500, q=2.5, variant=6), 32, 0.0, gain=0.5)
    for bar in (5, 21):
        fx.at(D.crash(variant=4, decay=1.2), bar, 0, gain=0.5, pan=-0.3)
    fx.at(D.boom(46.0, variant=9), 21, 0, gain=0.7)

    # ---- cave drips and a damp, dark air
    rng = s.rng('drips')
    for i in range(26):
        pos = rng.uniform(0, s.seconds)
        drips.add(_drip(rng) * rng.uniform(0.4, 1.0), ns(pos), pan=rng.uniform(-0.7, 0.7))
        if rng.random() < 0.35:
            drips.add(_drip(rng) * 0.4, ns(pos + rng.uniform(0.3, 0.8)), pan=rng.uniform(-0.7, 0.7))
    air.at(air_layer(s.L, s.rng('air'), center=2200.0, low=600.0, swells=4), 1, 0)
    return s
