"""menu_theme - dreamy spacey ambient. D major (Lydian colours), 80 BPM, 4/4, 20 bars = 60.0 s.

Form: bars 1-8 pads + slow e-piano arps + distant bells, bars 9-20 a soft ocarina melody on top.
The last chord (Asus2) resolves into the first (Dmaj9) when the loop restarts.
"""
import numpy as np

from dsp import filt, bp, hp, highshelf
from music import instruments as I
from music.sequencer import Song
from synth import SR, pink

NAME = 'menu_theme'
BPM, BEATS_PER_BAR, BARS, LOOP = 80, 4, 20, True

# (bar, beats, bass, pad voicing, arp tones)
CHORDS = [
    (1, 8, 'D2', ['F#3', 'A3', 'C#4', 'E4'], ['D4', 'A4', 'C#5', 'E5', 'F#5']),        # Dmaj9
    (3, 8, 'B1', ['F#3', 'A3', 'D4', 'E4'], ['B3', 'F#4', 'A4', 'D5', 'E5']),          # Bm11
    (5, 8, 'G1', ['F#3', 'A3', 'B3', 'D4'], ['G3', 'D4', 'F#4', 'A4', 'C#5']),         # Gmaj9#11
    (7, 4, 'A1', ['E3', 'A3', 'B3', 'D4'], ['A3', 'E4', 'A4', 'B4', 'D5']),            # Asus4
    (8, 4, 'A1', ['E3', 'A3', 'B3', 'C#4'], ['A3', 'E4', 'A4', 'B4', 'C#5']),          # A(add9)
    (9, 8, 'D2', ['F#3', 'A3', 'C#4', 'E4'], ['D4', 'A4', 'C#5', 'E5', 'F#5']),        # Dmaj9
    (11, 8, 'F#1', ['F#3', 'A3', 'C#4', 'E4'], ['F#3', 'C#4', 'E4', 'A4', 'C#5']),     # F#m7
    (13, 8, 'G1', ['F#3', 'A3', 'B3', 'D4'], ['G3', 'D4', 'F#4', 'A4', 'C#5']),        # Gmaj9#11
    (15, 4, 'E2', ['G3', 'B3', 'D4', 'F#4'], ['E3', 'B3', 'D4', 'F#4', 'G4']),         # Em9
    (16, 4, 'A1', ['G3', 'A3', 'C#4', 'E4'], ['A3', 'E4', 'G4', 'A4', 'C#5']),         # A7
    (17, 8, 'B1', ['F#3', 'A3', 'D4', 'C#5'], ['B3', 'F#4', 'A4', 'C#5', 'D5']),       # Bm9
    (19, 4, 'G1', ['F#3', 'A3', 'B3', 'D4'], ['G3', 'D4', 'F#4', 'A4', 'B4']),         # Gmaj9
    (20, 4, 'A1', ['E3', 'A3', 'B3', 'E4'], ['A3', 'E4', 'A4', 'B4', 'E5']),           # Asus2
]

MELODY = (
    'A4:1.5 D5:.5 F#5:2 | E5:3 r | A4:1.5 C#5:.5 E5:2 | F#5:2 E5 C#5 | '
    'D5:1.5 E5:.5 F#5 A5 | F#5:3 r | G5:1.5 F#5:.5 E5 D5 | E5:2 C#5 A4 | '
    'D5:1.5 C#5:.5 B4 F#5 | E5:2 D5 C#5 | B4:2 D5 F#5 | E5:3 r |'
)

ARP_PATTERN = [0, 1, 2, 3, 4, 3, 2, 1]
BELLS = [(1, 'A5'), (3, 'F#5'), (5, 'C#6'), (7, 'B5'), (11, 'C#6'), (13, 'A5'), (17, 'F#6'),
         (19, 'D6')]


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-15.0)
    s.reverb.update(seconds=5.0, t60=3.8, predelay=0.03, ret=0.42, hf=0.4)
    s.delay.update(beats=0.75, feedback=0.45, damp=2800.0, ret=0.34, to_reverb=0.5)

    pad = s.track('pad', gain_db=-6.0, reverb=0.5, highpass=110, eq=[highshelf(2500, 5.0)])
    bass = s.track('bass', gain_db=-10.0, lowpass=380)
    arp = s.track('arp', gain_db=-1.0, reverb=0.35, delay=0.4, highpass=150)
    lead = s.track('lead', gain_db=-3.0, reverb=0.45, delay=0.3, highpass=200)
    bells = s.track('bells', gain_db=-6.0, reverb=0.8, delay=0.35)
    air = s.track('air', gain_db=-14.0, reverb=0.15)

    for bar, beats, b, voicing, tones in CHORDS:
        dur = beats - 0.25
        pad.chord(I.pad_warm, bar, 0, dur, voicing, vel=0.75, attack=1.4, release=2.6, bright=7.0,
                 max_cut=4800.0)
        bass.note(I.bass_sub, bar, 0, dur, b, vel=0.8, attack=0.25, release=1.0)
        for k in range(int(beats * 2)):
            sub_bar = bar + k // 8
            beat = (k % 8) * 0.5
            if bar < 5 and k % 2:
                continue  # sparse quarter-note arps in the intro
            idx = ARP_PATTERN[k % 8]
            vel = (0.62 if k % 8 == 0 else 0.5 if k % 2 == 0 else 0.4)
            if bar >= 9:
                vel *= 0.85  # leave room for the melody
            arp.note(I.epiano, sub_bar, beat, 0.9, tones[idx], vel=vel, pan=(-0.35 if k % 2 else 0.35),
                     bright=1.0)

    lead.line(I.lead_flute, 9, MELODY, vel=0.7, legato=0.97, vib=11.0, breath=0.05)
    for bar, nt in BELLS:
        bells.note(I.bell, bar, 0, 1, nt, vel=0.5, ratio=3.5, index=1.6, decay=1.8)

    # airy space texture, periodic over the loop (circular filtering + whole-cycle swell)
    n = s.L
    t = np.arange(n) / SR
    noise = filt(pink(n, s.rng('air')), bp(3200, 0.5), hp(900), circular=True)
    swell = 0.6 + 0.4 * np.sin(2 * np.pi * 2 * t / (n / SR))
    air.at(np.vstack([noise * swell, np.roll(noise, n // 3) * swell]), 1, 0)
    return s
