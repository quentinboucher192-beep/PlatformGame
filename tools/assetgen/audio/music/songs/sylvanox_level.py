"""sylvanox_level - mysterious but adventurous exploration. D minor (dorian colours), 105 BPM, 4/4,
40 bars = 91.43 s (one 16th note = exactly 6300 samples).

Form:  intro 1-4 | A 5-20 (ocarina melody over marimba ostinato, soft driving drums)
       | B 21-36 (relative-major lift, soaring glass lead, fuller drums) | outro 37-40 (thins
       out; flows into the intro when the loop restarts).
Palette: FM marimba ostinato, Karplus-Strong plucks, airy detuned pads, soft synthesised kit.
"""
import numpy as np

from dsp import filt, bp, hp, highshelf
from music import drums as D
from music import instruments as I
from music.sequencer import Song
from synth import SR, pink, note_midi

NAME = 'sylvanox_level'
BPM, BEATS_PER_BAR, BARS, LOOP = 105, 4, 40, True

# name: (bass, pad voicing, ostinato tones (root, fifth, ninth, third))
CHORD = {
    'Dm9':    ('D2', ['F3', 'A3', 'C4', 'E4'], ['D4', 'A4', 'E5', 'F5']),
    'Bbmaj7': ('Bb1', ['F3', 'Bb3', 'D4', 'A4'], ['Bb3', 'F4', 'C5', 'D5']),
    'Cadd9':  ('C2', ['E3', 'G3', 'C4', 'D4'], ['C4', 'G4', 'D5', 'E5']),
    'Gm9':    ('G1', ['F3', 'Bb3', 'D4', 'A4'], ['G3', 'D4', 'A4', 'Bb4']),
    'Asus4':  ('A1', ['E3', 'A3', 'D4', 'E4'], ['A3', 'E4', 'B4', 'D5']),
    'A7':     ('A1', ['E3', 'G3', 'C#4', 'E4'], ['A3', 'E4', 'G4', 'C#5']),
    'Fmaj7':  ('F2', ['F3', 'A3', 'C4', 'E4'], ['F3', 'C4', 'G4', 'A4']),
    'C/E':    ('E2', ['E3', 'G3', 'C4', 'D4'], ['C4', 'G4', 'D5', 'E5']),
    'Am7':    ('A1', ['E3', 'G3', 'A3', 'C4'], ['A3', 'E4', 'B4', 'C5']),
    'Bbmaj9': ('Bb1', ['F3', 'A3', 'C4', 'D4'], ['Bb3', 'F4', 'C5', 'D5']),
    'C6':     ('C2', ['E3', 'G3', 'A3', 'C4'], ['C4', 'G4', 'A4', 'E5']),
    'C11':    ('C2', ['F3', 'Bb3', 'D4', 'E4'], ['C4', 'G4', 'Bb4', 'D5']),
}

PROG = (['Dm9', 'Dm9', 'Bbmaj7', 'Cadd9'] +                                       # 1-4 intro
        ['Dm9', 'Dm9', 'Bbmaj7', 'Bbmaj7', 'Gm9', 'Gm9', 'Asus4', 'A7'] +         # 5-12 A1
        ['Dm9', 'Dm9', 'Bbmaj7', 'Bbmaj7', 'Gm9', 'C6', 'Asus4', 'A7'] +          # 13-20 A2
        ['Fmaj7', 'C/E', 'Gm9', 'Bbmaj7', 'Fmaj7', 'Am7', 'Bbmaj9', 'C6'] +       # 21-28 B1
        ['Fmaj7', 'C/E', 'Dm9', 'Bbmaj7', 'Gm9', 'C11', 'Asus4', 'A7'] +          # 29-36 B2
        ['Dm9', 'Dm9', 'Bbmaj7', 'Cadd9'])                                         # 37-40 outro

MELODY_A = (
    'D5:1.5 E5:.5 F5 A5 | G5:1.5 F5:.5 E5:2 | D5:1.5 C5:.5 D5 F5 | A4:3 r | '
    'Bb4 D5 F5 A5 | G5:1.5 F5:.5 D5:2 | E5:1.5 D5:.5 E5 A5 | C#5:3 r | '
    'A5:1.5 G5:.5 F5 E5 | F5 A5 D6:2 | C6:1.5 Bb5:.5 A5 F5 | G5:3 r | '
    'Bb5:1.5 A5:.5 G5 D5 | E5:1.5 F5:.5 G5 A5 | A5:2 G5 E5 | E5:2 C#5 A4 |'
)
MELODY_B = (
    'A5:3 C6 | G5:2 E5:2 | F5:1.5 G5:.5 A5 Bb5 | A5:3 r | '
    'C6:2 A5 F5 | E5:2 G5 A5 | F5:1.5 D5:.5 C5 D5 | E5:2 G5:2 | '
    'A5:3 C6 | D6:2 C6 G5 | A5:2 F5 E5 | D5:1.5 F5:.5 A5:2 | '
    'Bb5:1.5 A5:.5 G5 F5 | D5 F5 G5:2 | A5:4 | G5 E5 C#5 A4 |'
)

OSTINATO = [0, 1, 2, 1, 3, 1, 2, 1]
OST_ACCENT = [1.0, 0.7, 0.75, 0.95, 0.7, 0.75, 0.9, 0.7]   # 3+3+2 feel
TWINKLE = 'x..x..x...x..x..'
TWINKLE_IDX = [3, 2, 1, 3, 2]


def _blocks(prog):
    """Merge consecutive identical chords -> [(bar, n_bars, name)]."""
    out = []
    for i, name in enumerate(prog):
        if out and out[-1][2] == name:
            out[-1][1] += 1
        else:
            out.append([i + 1, 1, name])
    return out


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-14.0)
    s.reverb.update(seconds=3.2, t60=2.3, predelay=0.02, ret=0.3, hf=0.4)
    s.delay.update(beats=0.75, feedback=0.35, damp=3500.0, ret=0.3, to_reverb=0.35)
    s.duck_tau = 0.12

    pad = s.track('pad', gain_db=-9.0, reverb=0.45, duck=0.3, highpass=140, eq=[highshelf(3000, 4.0)])
    bass = s.track('bass', gain_db=-9.0, duck=0.35, lowpass=1800)
    mar = s.track('marimba', gain_db=-2.0, reverb=0.25, delay=0.12, highpass=120)
    twk = s.track('pluck', gain_db=5.0, reverb=0.35, delay=0.45, highpass=250)
    lead_a = s.track('lead_a', gain_db=-3.0, reverb=0.35, delay=0.22, highpass=200)
    lead_b = s.track('lead_b', gain_db=-3.0, reverb=0.4, delay=0.3, highpass=200)
    kick = s.track('kick', gain_db=-6.0, reverb=0.05)
    snare = s.track('snare', gain_db=-5.0, reverb=0.25)
    hats = s.track('hats', gain_db=-3.0, reverb=0.12, pan=0.2)
    shk = s.track('shaker', gain_db=-7.0, reverb=0.15, pan=-0.25)
    toms = s.track('toms', gain_db=-15.0, reverb=0.3)
    fx = s.track('fx', gain_db=-12.0, reverb=0.4)
    air = s.track('air', gain_db=-20.0, reverb=0.2)

    # ---- harmony: pads, ostinato, bass
    for bar, nb, name in _blocks(PROG):
        b, voicing, _ = CHORD[name]
        pad.chord(I.pad_warm, bar, 0, nb * 4 - 0.25, voicing, vel=0.7, attack=0.6, release=1.6,
                  bright=6.0, max_cut=4500.0, detune=11.0)

    for bar, name in enumerate(PROG, start=1):
        b, voicing, tones = CHORD[name]
        intro = bar <= 4 or bar >= 37
        for k in range(8):
            v = OST_ACCENT[k] * (0.55 if intro else 0.68)
            mar.note(I.marimba, bar, k * 0.5, 0.5, tones[OSTINATO[k]], vel=v,
                     pan=-0.25 if k % 2 == 0 else 0.15)
        root = note_midi(b)
        if bar in (1, 2):
            continue
        if intro:
            bass.note(I.bass_sub, bar, 0, 3.75, root, vel=0.8, attack=0.05, release=0.3)
            continue
        if bar <= 20:
            rhythm = [(0, 0.75, 0), (1.5, 0.45, 0), (2, 0.75, 0), (3, 0.45, 12), (3.5, 0.45, 7)]
        else:
            rhythm = [(0, 1.25, 0), (1.5, 0.45, 0), (2, 0.9, 0), (3, 0.45, 7), (3.5, 0.45, 12)]
        for beat, dur, iv in rhythm:
            bass.note(I.bass_pluck, bar, beat, dur, root + iv, vel=0.85 if beat == 0 else 0.7,
                      peak=5.0, floor=1.5, tau=0.1, q=1.0, sub=0.6, drive=1.2)

    # ---- twinkling plucks (A2 and B)
    for bar, name in enumerate(PROG, start=1):
        if not (13 <= bar <= 36):
            continue
        tones = CHORD[name][2]
        hits = [i for i, c in enumerate(TWINKLE) if c == 'x']
        for j, step in enumerate(hits):
            nt = note_midi(tones[TWINKLE_IDX[j % len(TWINKLE_IDX)]]) + 12
            twk.note(I.pluck, bar, step * 0.25, 0.4, nt, vel=0.6 if j == 0 else 0.48, t60=0.9,
                     bright=0.75, pan=(-0.5 if j % 2 else 0.5))

    # ---- melodies
    lead_a.line(I.lead_flute, 5, MELODY_A, vel=0.8, legato=0.95, vib=14.0, breath=0.06)
    lead_b.line(I.lead_glass, 21, MELODY_B, vel=0.78, legato=0.97)
    lead_b.line(I.pluck, 21, MELODY_B, vel=0.35, transpose=-12, t60=0.8, bright=0.5, pan=-0.3)
    lead_a.line(I.marimba, 37, 'D5:1.5 E5:.5 F5 A5 | E5:4 | r:4 | r:4 |', vel=0.45, pan=0.3)

    # ---- drums
    k_soft = D.kick(f_start=120, f_end=48, decay=0.24, click=0.18, drive=1.3)
    sn = D.snare(tone=200, noise_decay=0.12, noise_lp=6500, tone_mix=0.5, roomy=0.25)
    ch = D.hat(decay=0.03, length=0.1, bright=1.0, metal=0.45)
    oh = D.hat(decay=0.18, length=0.45, bright=0.95, metal=0.5, variant=2)
    sh = D.shaker(variant=2)
    rim = D.rim()
    for bar in range(1, 41):
        intro = bar <= 4 or bar >= 37
        if intro:
            shk.hits(sh, bar, 'gogogogogogogogo', vel=0.8)
            if bar in (2, 4, 38, 40):
                snare.hits(rim, bar, '............o...', vel=0.7)
            if bar in (37, 38, 39):
                kick.hits(k_soft, bar, 'o...............', duck=True)
            continue
        A = bar <= 20
        kick.hits(k_soft, bar, 'x.....x.x.......' if A else 'x.....x.x.....o.', duck=True)
        snare.hits(sn, bar, '....x.......x...' if A else '....x......gx..g', vel=0.85)
        if A:
            hats.hits(ch, bar, 'x.o.x.o.x.o.x.o.')
            if bar >= 13:
                shk.hits(sh, bar, 'gggggggggggggggg', vel=0.8)
        else:
            hats.hits(ch, bar, 'xgogxgogxgogxgo.')
            hats.hits(oh, bar, '..............o.', vel=0.8)
            shk.hits(sh, bar, 'gogogogogogogogo', vel=0.7)
    # fills and transitions
    for bar in (12, 28):
        toms.hits(D.tom(196.0), bar, '............x.o.', pan=0.3)
        toms.hits(D.tom(147.0), bar, '..............x.', pan=-0.1)
    for bar in (20, 36):
        toms.hits(D.tom(196.0), bar, '........x.x.....', pan=0.3)
        toms.hits(D.tom(147.0), bar, '............x.o.', pan=0.0)
        toms.hits(D.tom(110.0), bar, '..............x.', pan=-0.3)
    fx.at(D.reverse_cymbal(s.spb * 2), 4, 2.0)
    fx.at(D.riser(s.spb * 4, f0=400, f1=6000), 20, 0.0, gain=0.6)
    for bar in (5, 21, 29):
        fx.at(D.crash(variant=1), bar, 0, gain=0.6, pan=-0.3)

    # ---- jungle air
    n = s.L
    t = np.arange(n) / SR
    noise = filt(pink(n, s.rng('air')), bp(5000, 0.6), hp(1800), circular=True)
    swell = 0.6 + 0.4 * np.sin(2 * np.pi * 5 * t / (n / SR))
    air.at(np.vstack([noise * swell, np.roll(noise, n // 2) * swell]), 1, 0)
    return s
