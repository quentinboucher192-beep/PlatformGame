"""victory - non-looping triumphant jingle (~6 s). D major at 120 BPM.

Triplet pickup -> D major hit -> bVI (Bb) -> bVII (C) -> big D major with a bell flourish,
then the reverb tail fades out by 6.0 s.
"""
from music import drums as D
from music import instruments as I
from music.sequencer import Song

NAME = 'victory'
BPM, BEATS_PER_BAR, BARS, LOOP, TAIL = 120, 4, 2, False, 2.0   # 4 s of music + 2 s tail


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, loop=LOOP, tail=TAIL,
             target_lufs=-14.0, end_fade=1.3)
    s.reverb.update(seconds=3.0, t60=2.0, predelay=0.02, ret=0.32, hf=0.45)
    s.delay.update(beats=0.5, feedback=0.3, damp=4000.0, ret=0.2, to_reverb=0.3)

    lead = s.track('brass_lead', gain_db=-4.0, reverb=0.35, delay=0.12, highpass=150)
    brass = s.track('brass_chords', gain_db=-9.0, reverb=0.35, highpass=120)
    strings = s.track('strings', gain_db=-12.0, reverb=0.45, highpass=120)
    bass = s.track('bass', gain_db=-9.0, lowpass=900)
    bells = s.track('bells', gain_db=-7.0, reverb=0.5, delay=0.3, highpass=400)
    drums = s.track('drums', gain_db=-6.0, reverb=0.2)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.3)

    # (beat, beats, note) - lead line
    for beat, beats, nt in [(0, 1 / 3, 'A4'), (1 / 3, 1 / 3, 'D5'), (2 / 3, 1 / 3, 'F#5'),
                            (1, 0.7, 'A5'), (1.75, 0.25, 'F#5'), (2, 0.95, 'A5'),
                            (3, 0.95, 'Bb5'), (4, 0.95, 'C6'), (5, 3.0, 'D6')]:
        lead.note(I.lead_brass, 1, beat, beats * 0.92, nt, vel=0.9 if beat >= 1 else 0.75,
                  max_cut=5500.0)
        lead.note(I.lead_brass, 1, beat, beats * 0.92, _down(nt), vel=0.5, bright=3.5)

    chords = [(1, 1.9, ['D4', 'F#4', 'A4'], 'D2'), (3, 0.95, ['D4', 'F4', 'Bb4'], 'Bb1'),
              (4, 0.95, ['E4', 'G4', 'C5'], 'C2'), (5, 3.0, ['D4', 'F#4', 'A4', 'D5'], 'D2')]
    for beat, beats, notes, b in chords:
        brass.chord(I.lead_brass, 1, beat, beats, notes, vel=0.75, bright=3.8, vib=6.0)
        bass.note(I.bass_pluck, 1, beat, beats, b, vel=0.9, peak=4.0, sub=0.7, drive=1.3)
        drums.at(D.taiko(73.0), 1, beat, gain=0.9)
    strings.chord(I.strings_legato, 1, 5, 3.0, ['D3', 'A3', 'F#4', 'D5'], vel=0.8, attack=0.1)
    for k, nt in enumerate(['D6', 'F#6', 'A6', 'D7', 'A6', 'F#6', 'A6', 'D7']):
        bells.note(I.celesta, 1, 5 + k * 0.25, 0.25, nt, vel=0.65 - 0.03 * k,
                   pan=-0.4 + 0.1 * k)
    kk = D.kick(f_start=140, f_end=50, decay=0.3, click=0.3, drive=1.5)
    drums.at(kk, 1, 1)
    drums.at(kk, 1, 5)
    for k in range(8):  # timpani-like roll on the final chord, fading
        drums.at(D.tom(73.0, decay=0.25), 1, 5.5 + k * 0.125, gain=0.45 * (1 - k / 9))
    cym.at(D.crash(), 1, 1)
    cym.at(D.crash(variant=2), 1, 5)
    return s


def _down(nt):
    """Same note one octave lower (string form)."""
    i = len(nt) - 1
    return nt[:i] + str(int(nt[i]) - 1)
