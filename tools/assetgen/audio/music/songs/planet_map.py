"""planet_map - floating, sense of wonder. A major, 90 BPM, 3/4 (lilting waltz), 24 bars = 48.0 s.

Bars 1-8 music box + pad + harp, bars 9-16 airy FM melody enters with soft waltz percussion,
bars 9-16 borrow the minor iv (Dm6) for the "wonder" colour, bars 17-24 lift through
Bm9 - C#m7 - Dmaj7 - Esus4/E which leads back into Amaj7 at the loop point.
"""
import numpy as np

from dsp import filt, bp, hp, highshelf
from music import drums as D
from music import instruments as I
from music.sequencer import Song
from synth import SR, pink

NAME = 'planet_map'
BPM, BEATS_PER_BAR, BARS, LOOP = 90, 3, 24, True

# (bar, bars, bass, pad voicing, music-box tones (6, ascending))
CHORDS = [
    (1, 2, 'A1', ['C#4', 'E4', 'G#4', 'B4'], ['A3', 'E4', 'A4', 'C#5', 'E5', 'G#5']),     # Amaj9
    (3, 2, 'C#2', ['B3', 'E4', 'G#4', 'C#5'], ['C#4', 'G#4', 'B4', 'C#5', 'E5', 'G#5']),  # C#m7
    (5, 2, 'D2', ['A3', 'C#4', 'F#4', 'A4'], ['D4', 'A4', 'C#5', 'D5', 'F#5', 'A5']),     # Dmaj7
    (7, 1, 'E2', ['B3', 'E4', 'A4', 'B4'], ['E4', 'A4', 'B4', 'E5', 'A5', 'B5']),         # Esus4
    (8, 1, 'E2', ['B3', 'E4', 'G#4', 'B4'], ['E4', 'G#4', 'B4', 'E5', 'G#5', 'B5']),      # E
    (9, 2, 'A1', ['C#4', 'E4', 'G#4', 'B4'], ['A3', 'E4', 'A4', 'C#5', 'E5', 'G#5']),     # Amaj9
    (11, 2, 'F#2', ['A3', 'C#4', 'E4', 'G#4'], ['F#4', 'A4', 'C#5', 'E5', 'G#5', 'A5']),  # F#m9
    (13, 2, 'D2', ['A3', 'C#4', 'E4', 'F#4'], ['D4', 'A4', 'C#5', 'E5', 'F#5', 'A5']),    # Dmaj9
    (15, 2, 'D2', ['A3', 'B3', 'D4', 'F4'], ['D4', 'A4', 'B4', 'D5', 'F5', 'A5']),        # Dm6
    (17, 2, 'B1', ['A3', 'C#4', 'D4', 'F#4'], ['B3', 'F#4', 'A4', 'C#5', 'D5', 'F#5']),   # Bm9
    (19, 2, 'C#2', ['B3', 'E4', 'G#4', 'C#5'], ['C#4', 'G#4', 'B4', 'C#5', 'E5', 'G#5']), # C#m7
    (21, 2, 'D2', ['A3', 'C#4', 'F#4', 'A4'], ['D4', 'A4', 'C#5', 'D5', 'F#5', 'A5']),    # Dmaj7
    (23, 1, 'E2', ['B3', 'E4', 'A4', 'B4'], ['E4', 'A4', 'B4', 'E5', 'A5', 'B5']),        # Esus4
    (24, 1, 'E2', ['B3', 'E4', 'G#4', 'B4'], ['E4', 'G#4', 'B4', 'E5', 'G#5', 'B5']),     # E
]

MELODY = (
    'C#5 E5 A5 | G#5:2 E5 | F#5 G#5 A5 | C#6:2 B5 | A5 F#5 E5 | F#5:2 A5 | F5:2 D5 | E5:3 | '
    'D5 F#5 A5 | C#6:2 B5 | G#5:1.5 E5:.5 C#5 | E5:2 G#5 | F#5 A5 C#6 | B5:2 A5 | A5:2 B5 | G#5:3 |'
)

# two-bar music-box figure over the six chord tones (8th notes, 12 per two bars)
BOX_2BAR = [0, 2, 4, 5, 4, 2, 1, 3, 5, 4, 3, 2]
BOX_1BAR = [0, 2, 4, 5, 4, 2]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-15.0)
    s.reverb.update(seconds=4.0, t60=3.0, predelay=0.025, ret=0.38, hf=0.45)
    s.delay.update(beats=1.5, feedback=0.36, damp=3800.0, ret=0.3, to_reverb=0.4)

    pad = s.track('pad', gain_db=-7.0, reverb=0.45, highpass=120, eq=[highshelf(3000, 4.0)])
    choir = s.track('choir', gain_db=-8.0, reverb=0.6, highpass=200)
    bass = s.track('bass', gain_db=-9.0, lowpass=400)
    box = s.track('musicbox', gain_db=-3.0, reverb=0.4, delay=0.35, highpass=200)
    harp = s.track('harp', gain_db=2.0, reverb=0.5, delay=0.15, highpass=120)
    lead = s.track('lead', gain_db=-2.5, reverb=0.45, delay=0.3, highpass=200)
    perc = s.track('perc', gain_db=-3.0, reverb=0.25)
    kick = s.track('kick', gain_db=-8.0, reverb=0.08, lowpass=4000)
    air = s.track('air', gain_db=-16.0, reverb=0.2)

    for bar, bars, b, voicing, tones in CHORDS:
        beats = bars * 3
        pad.chord(I.pad_warm, bar, 0, beats - 0.25, voicing, vel=0.75, attack=0.9, release=2.0,
                  bright=7.0, max_cut=5000.0)
        if bar >= 17:
            choir.chord(I.pad_choir, bar, 0, beats - 0.5, voicing[1:], vel=0.55, vowel='o')
        for k in range(bars):
            bass.note(I.bass_sub, bar + k, 0, 2.75, b, vel=0.8 if k == 0 else 0.65, attack=0.03,
                      release=0.5)
        pattern = BOX_2BAR if bars == 2 else BOX_1BAR
        for k, idx in enumerate(pattern):
            vel = 0.75 if k % 6 == 0 else 0.55 if k % 2 == 0 else 0.45
            if bar >= 9:
                vel *= 0.85
            box.note(I.celesta, bar + k // 6, (k % 6) * 0.5, 0.5, tones[idx], vel=vel,
                     pan=0.3 if k % 2 else -0.3)
        if bar in (1, 9, 17):  # harp glissando into each phrase
            for k, nt in enumerate(tones + [tones[2], tones[3]]):
                harp.note(I.pluck, bar, k * 0.25, 2.0, nt, vel=0.55 + 0.05 * k, t60=2.5, bright=0.55,
                          ring=True, pan=-0.4 + 0.1 * k)
        else:  # soft harp root/fifth on beat 1
            harp.note(I.pluck, bar, 0, 2.5, tones[0], vel=0.5, t60=2.0, bright=0.45, ring=True, pan=-0.3)

    lead.line(I.lead_glass, 9, MELODY, vel=0.75, legato=0.95)

    sh = D.shaker(variant=1, center=6000.0)
    for bar in range(9, 25):
        perc.hits(sh, bar, 'xgogog', step=0.5, vel=0.55, pan=0.25)
        kick.hits(D.kick(f_start=110, f_end=50, decay=0.22, click=0.1, drive=1.1), bar, 'x.....',
                  step=0.5, vel=0.7)
        if bar >= 17:
            perc.hits(D.rim(variant=1), bar, '..o.o.', step=0.5, vel=0.35, pan=-0.2)

    n = s.L
    t = np.arange(n) / SR
    noise = filt(pink(n, s.rng('air')), bp(4500, 0.6), hp(1500), circular=True)
    swell = 0.6 + 0.4 * np.sin(2 * np.pi * 3 * t / (n / SR) + 1.0)
    air.at(np.vstack([noise * swell, np.roll(noise, n // 2) * swell]), 1, 0)
    return s
