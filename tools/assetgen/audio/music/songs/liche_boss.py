"""liche_boss - la Liche des Catacombes (giga boss of Les Confins). C-sharp minor (harmonic: the
G-sharp major dominant with its B-sharp, the Neapolitan D major, a B-sharp diminished seventh),
a waltz in 3/4 at 168 BPM, 64 bars = 68.57 s.

Character: a gothic danse macabre.  A crypt organ and its pedal, a harpsichord, a choir that only
whispers, a funeral bell; an elegant minor waltz that the lich slowly loses control of, until it
turns frantic: harpsichord runs in sixteenths, the organ hammering every beat, pounding drums,
then a sudden stop on a last chord, the bell, and the whispers leading back into the crypt.

Form:  intro 1-8 (organ chords and pedal, whispers, the bell, wind)
       | waltz 9-24 (harpsichord: oom-pah-pah and the melody; soft frame drum)
       | waltz 25-40 (the melody on the organ, the choir, the harpsichord in eighths, drums)
       | frantic 41-60 (sixteenth runs, organ on every beat, the melody in diminution, drums x2)
       | the fall 61-64 (a last chord, the bell, whispers over the pedal: back into the loop).
"""
import numpy as np

from dsp import bp, filt, hp
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.instruments import _rng, _spread, instrument
from music.sequencer import Song
from music.textures import air_layer, blocks
from synth import SR, adsr, harmonic_osc, midi_hz, note_midi, ns, perc, pink, pluck, white

NAME = 'liche_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 168, 3, 64, True

# name: (bass root, organ / harpsichord voicing, choir voicing, arpeggio tones over two octaves)
CHORD = {
    'C#m': ('C#2', ['G#3', 'C#4', 'E4'], ['C#4', 'E4', 'G#4'], ['C#4', 'E4', 'G#4', 'C#5', 'E5', 'G#5']),
    'G#': ('G#1', ['G#3', 'B#3', 'D#4', 'F#4'], ['B#3', 'D#4', 'G#4'], ['G#3', 'B#3', 'D#4', 'F#4', 'G#4', 'B#4']),
    'A': ('A1', ['A3', 'C#4', 'E4'], ['C#4', 'E4', 'A4'], ['A3', 'C#4', 'E4', 'A4', 'C#5', 'E5']),
    'F#m': ('F#1', ['F#3', 'A3', 'C#4'], ['C#4', 'F#4', 'A4'], ['F#3', 'A3', 'C#4', 'F#4', 'A4', 'C#5']),
    'E': ('E2', ['G#3', 'B3', 'E4'], ['B3', 'E4', 'G#4'], ['E4', 'G#4', 'B4', 'E5', 'G#5', 'B5']),
    'D': ('D2', ['F#3', 'A3', 'D4'], ['D4', 'F#4', 'A4'], ['D4', 'F#4', 'A4', 'D5', 'F#5', 'A5']),
    'dim': ('B#1', ['B#3', 'D#4', 'F#4', 'A4'], ['D#4', 'F#4', 'A4'], ['B#3', 'D#4', 'F#4', 'A4', 'B#4', 'D#5']),
}

PROG = (['C#m', 'C#m', 'G#', 'G#', 'A', 'A', 'G#', 'G#'] +                          # 1-8 intro
        ['C#m', 'C#m', 'G#', 'G#', 'C#m', 'C#m', 'F#m', 'G#',
         'A', 'A', 'E', 'E', 'D', 'D', 'G#', 'G#'] +                                 # 9-24 waltz
        ['F#m', 'F#m', 'C#m', 'C#m', 'D', 'D', 'G#', 'G#',
         'A', 'A', 'F#m', 'F#m', 'D', 'D', 'G#', 'G#'] +                             # 25-40 waltz II
        ['C#m', 'G#', 'C#m', 'G#', 'A', 'D', 'G#', 'G#', 'F#m', 'C#m',
         'D', 'G#', 'C#m', 'A', 'D', 'dim', 'C#m', 'D', 'G#', 'G#'] +                # 41-60 frantic
        ['C#m', 'C#m', 'G#', 'G#'])                                                  # 61-64 the fall

WALTZ = ('G#4 C#5 E5 | G#5:2 E5 | F#5 E5 D#5 | B#4:2 G#4 | C#5 E5 G#5 | C#6:2 B5 | A5 G#5 F#5 | '
         'B#5:2 D#5 | E5 A5 C#6 | B5:2 A5 | G#5 E5 B4 | E5:3 | F#5 A5 D6 | C#6:2 A5 | B#5 G#5 D#5 | G#5:3 |')
WALTZ_B = ('A4:2 C#5 | F#5:2 E5 | E5 G#5 C#6 | B5:2 G#5 | A5:2 F#5 | D6:2 C#6 | B#5:2 G#5 | D#6:3 | '
           'C#6:2 E6 | A5:3 | F#5 A5 C#6 | E6:2 C#6 | D6 C#6 A5 | F#5:3 | G#5 B#5 D#6 | F#6:2 D#6 |')
FRANTIC = ('C#5:.5 E5:.5 G#5:.5 C#6:.5 B5:.5 G#5:.5 | B#5:.5 D#6:.5 F#6:.5 D#6:.5 B#5:.5 G#5:.5 | '
           'E6:.5 C#6:.5 G#5:.5 E5:.5 G#5:.5 C#6:.5 | D#6:1.5 B#5:1.5 | '
           'C#6:.5 E6:.5 A6:.5 E6:.5 C#6:.5 A5:.5 | D6:.5 F#6:.5 A6:.5 F#6:.5 D6:.5 A5:.5 | '
           'G#5:.5 B#5:.5 D#6:.5 F#6:.5 D#6:.5 B#5:.5 | G#6:3 | '
           'A5:.5 C#6:.5 F#6:.5 C#6:.5 A5:.5 F#5:.5 | G#5:.5 C#6:.5 E6:.5 C#6:.5 G#5:.5 E5:.5 | '
           'F#5:.5 A5:.5 D6:.5 A5:.5 F#5:.5 D5:.5 | B#5:1.5 G#5:1.5 | E6:1 D#6:.5 E6:.5 G#6:1 | '
           'E6:1 C#6:.5 E6:.5 A6:1 | F#6:1 D6:.5 F#6:.5 A6:1 | F#6 D#6 A5 | '
           'G#5:.5 C#6:.5 E6:.5 G#6:.5 E6:.5 C#6:.5 | A5:.5 D6:.5 F#6:.5 A6:.5 F#6:.5 D6:.5 | '
           'B#5:.5 D#6:.5 G#6:.5 D#6:.5 B#5:.5 G#5:.5 | G#6:1.5 D#6:.5 B#5:.5 G#5:.5 |')
ARP16 = [0, 1, 2, 3, 4, 5, 4, 3, 2, 3, 4, 5]


@instrument
def crypt_organ(midi, dur, vel, seed, attack=0.04, release=0.35, mixture=0.5, chiff=0.5):
    """Pipe organ in a crypt: principals 8' 4' 2' with a mixture (1 1/3', 1'), two slightly
    detuned ranks spread in stereo, a breathy 'chiff' as each pipe speaks."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    rng = _rng('cryptorgan', midi, seed)
    amps = np.zeros(8)
    for k, a in ((1, 1.0), (2, 0.7), (3, 0.18), (4, 0.45), (6, 0.3 * mixture), (8, 0.25 * mixture)):
        amps[k - 1] = a
    a = harmonic_osc(amps, f, n, rng.random())
    b = harmonic_osc(amps, f * 1.0018, n, rng.random())
    st = _spread([a, b], 0.6)
    wind = filt(pink(n, rng), bp(min(f * 3.0, 7000.0), 1.0))[:n]
    ch = wind * perc(n, 0.004, 0.05) * chiff * 0.6 + wind * 0.02
    env = adsr(n, attack, 0.2, 0.9, release, dur)
    return (st + np.vstack([ch, ch])) * env * vel * 0.22


@instrument
def harpsichord(midi, dur, vel, seed, damp=0.07, bright=0.95):
    """Two choirs of plucked strings (8' slightly detuned + 4'), the jack's quill click, damped
    when the key is released (mono)."""
    f = float(midi_hz(midi))
    rng = _rng('harpsi', midi, seed)
    t60 = float(np.clip(2.6 * (220.0 / f) ** 0.5, 0.6, 3.2))
    length = min(t60, dur + damp * 4) + 0.02
    s = pluck(f, length, rng, t60=t60, bright=bright, pick=0.08)
    s = s + 0.7 * pluck(f * 1.0015, length, rng, t60=t60, bright=bright, pick=0.11)
    s = s + 0.35 * pluck(f * 2.0, length, rng, t60=t60 * 0.7, bright=bright, pick=0.17)
    n = len(s)
    t = np.arange(n) / SR
    quill = filt(white(ns(0.01), rng), hp(3000.0, 0.7)) * perc(ns(0.01), 0.0002, 0.0015)
    s[:len(quill)] += quill * 0.5
    s *= np.where(t < dur, 1.0, np.exp(-(t - dur) / damp))
    s *= np.clip((length - t) / 0.02, 0, 1)
    return s * vel * 0.32


@instrument
def whisper(midi, dur, vel, seed, attack=0.5, release=1.2, voices=4):
    """A choir that only whispers: breath tuned to the note (noise through narrow partials and a
    vowel formant), each voice muttering its own syllables (stereo)."""
    f = float(midi_hz(midi))
    n = ns(dur + release)
    t = np.arange(n) / SR
    rng = _rng('whisper', midi, seed)
    sigs = []
    for v in range(voices):
        nz = pink(n, rng)
        tone = sum(filt(nz, bp(f * k * (1.0 + 0.004 * v), 28.0))[:n] / k for k in (1, 2, 3))
        hiss = filt(white(n, rng), bp(rng.uniform(1800.0, 3200.0), 1.6))[:n] * 0.25
        rate = rng.uniform(3.0, 5.5)
        syl = np.clip(np.sin(2 * np.pi * (rate * t + rng.random())) + rng.uniform(0.0, 0.6), 0.0, 1.0)
        sigs.append((tone * 3.0 + hiss) * (0.35 + 0.65 * syl))
    env = adsr(n, attack, 0.5, 0.85, release, dur)
    return _spread(sigs, 0.8) * env * vel * (0.5 / np.sqrt(voices))


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0, ceiling_db=-2.4)
    s.reverb.update(seconds=4.8, t60=3.4, predelay=0.035, ret=0.36, hf=0.4, lf=1.2)
    s.delay.update(beats=1.0, feedback=0.3, damp=3500.0, ret=0.14, to_reverb=0.4)
    s.duck_tau = 0.1

    pedal = s.track('pedal', gain_db=-6.0, duck=0.25, lowpass=900, highpass=30)
    organ = s.track('organ', gain_db=-6.0, reverb=0.45, duck=0.15, highpass=90)
    melody = s.track('organ melody', gain_db=-7.0, reverb=0.4, delay=0.12, highpass=200)
    harp = s.track('harpsichord', gain_db=7.0, reverb=0.3, delay=0.1, highpass=120, pan=-0.15)
    runs = s.track('runs', gain_db=-3.0, reverb=0.3, highpass=200, pan=0.2)
    strings = s.track('strings', gain_db=-8.0, reverb=0.35, duck=0.2, highpass=120)
    choir = s.track('choir', gain_db=-5.0, reverb=0.6, duck=0.12, highpass=160)
    whispers = s.track('whispers', gain_db=-1.0, reverb=0.55, delay=0.15, highpass=250)
    bells = s.track('bell', gain_db=-11.0, reverb=0.6, highpass=80)
    stabs = s.track('stabs', gain_db=-12.0, reverb=0.3, highpass=120)
    kick = s.track('kick', gain_db=-12.0, reverb=0.06)
    snare = s.track('snare', gain_db=-7.0, reverb=0.35)
    hats = s.track('hats', gain_db=-2.0, reverb=0.15, pan=0.25, highpass=5000)
    drum = s.track('frame drum', gain_db=-6.0, reverb=0.35)
    taiko = s.track('taiko', gain_db=-9.0, reverb=0.4)
    wind = s.track('wind', gain_db=-16.0, reverb=0.3)
    fx = s.track('fx', gain_db=-9.0, reverb=0.5)

    for bar, nb, name in blocks(PROG):
        root, voicing, ch_v, arp = CHORD[name]
        r = note_midi(root)
        beats = nb * 3 - 0.15
        frantic = 41 <= bar <= 60
        quiet = bar <= 8 or bar >= 61
        if not frantic:
            pedal.note(crypt_organ, bar, 0, beats, r, vel=0.9, attack=0.15, mixture=0.0, chiff=0.2)
        if quiet:
            organ.chord(crypt_organ, bar, 0, beats, voicing, vel=0.75, attack=0.12, release=0.9)
            whispers.chord(whisper, bar, 0, beats, ch_v, vel=0.8)
        elif bar <= 24:
            whispers.chord(whisper, bar, 0, beats, ch_v, vel=0.5)
            organ.chord(crypt_organ, bar, 0, beats, voicing, vel=0.3, attack=0.2, mixture=0.0, chiff=0.2)
        elif bar >= 25 and not frantic:
            organ.chord(crypt_organ, bar, 0, beats, voicing, vel=0.45, attack=0.1, mixture=0.3)
            choir.chord(I.pad_choir, bar, 0, beats, ch_v, vel=0.6, vowel='o', attack=0.3, release=0.8)
            whispers.chord(whisper, bar, 0, beats, [note_midi(x) + 12 for x in ch_v], vel=0.45)
        elif frantic:
            choir.chord(I.pad_choir, bar, 0, beats, ch_v, vel=0.72, vowel='a', attack=0.1, release=0.6)
            strings.chord(I.strings_legato, bar, 0, beats, [r + 24, r + 31], vel=0.55, attack=0.2, bright=4.0)

    for bar, name in enumerate(PROG, start=1):
        root, voicing, ch_v, arp = CHORD[name]
        r = note_midi(root)
        if 9 <= bar <= 24:
            # the waltz: oom (bass) - pah - pah
            harp.note(harpsichord, bar, 0, 0.9, r + 12, vel=0.85)
            for beat in (1, 2):
                harp.chord(harpsichord, bar, beat, 0.7, voicing, vel=0.55 if beat == 1 else 0.48)
        elif 25 <= bar <= 40:
            harp.note(harpsichord, bar, 0, 0.9, r + 12, vel=0.85)
            for k in range(1, 6):
                harp.note(harpsichord, bar, k * 0.5, 0.45, arp[[1, 2, 3, 2, 1][k - 1]], vel=0.55)
        elif 41 <= bar <= 60:
            for k in range(12):
                runs.note(harpsichord, bar, k * 0.25, 0.24, arp[ARP16[k]], vel=0.75 if k % 4 == 0 else 0.55)
            for beat in range(3):
                pedal.note(crypt_organ, bar, beat, 0.8, r + (12 if beat else 0), vel=0.9, attack=0.01, mixture=0.0)
                organ.chord(crypt_organ, bar, beat, 0.6, voicing, vel=0.5 if beat == 0 else 0.38, attack=0.01,
                            release=0.15, mixture=0.8, chiff=0.8)
            stabs.chord(T.brass_stab, bar, 0, 0.3, voicing, vel=0.75)
    # the last chord, cut short
    organ.chord(crypt_organ, 61, 0, 1.0, CHORD['C#m'][1] + ['C#5'], vel=0.95, attack=0.01, release=1.4, mixture=1.0)
    pedal.note(crypt_organ, 61, 0, 1.0, note_midi('C#1'), vel=1.0, attack=0.01, release=1.4, mixture=0.0)

    harp.line(harpsichord, 9, WALTZ, vel=0.8)
    melody.line(crypt_organ, 25, WALTZ_B, vel=0.75, legato=0.95, mixture=0.7)
    harp.line(harpsichord, 25, WALTZ_B, vel=0.5, transpose=12)
    melody.line(crypt_organ, 41, FRANTIC, vel=0.8, legato=0.9, attack=0.01, mixture=1.0, chiff=0.8)
    harp.line(harpsichord, 41, FRANTIC, vel=0.6, transpose=-12)

    # ---- the bell, the wind, the drums
    for bar in (1, 5, 61, 63):
        bells.note(I.bell, bar, 0, 3, note_midi('C#3'), vel=0.85, ratio=2.0, index=1.6, decay=3.0)
        bells.note(T.ice_bell, bar, 0, 3, note_midi('G#3'), vel=0.4, decay=3.0, bright=0.6)
    wind.at(air_layer(s.L, s.rng('wind'), center=900.0, low=300.0, swells=6), 1, 0)
    fd = P.frame_drum(92.0, variant=111)
    rim = D.rim(variant=111)
    kk = D.kick(f_start=150, f_end=50, pitch_tau=0.03, decay=0.25, click=0.35, drive=1.8, variant=111)
    sn = D.snare(tone=190, tone_decay=0.06, noise_decay=0.14, noise_lp=8000, tone_mix=0.55, roomy=0.4, variant=111)
    hh = D.hat(decay=0.025, length=0.09, bright=1.15, metal=0.6, variant=111)
    tk = D.taiko(58.0, variant=111)
    chains = P.chain(0.8, 11, variant=111)
    for bar in range(1, BARS + 1):
        if bar <= 8 or bar >= 61:
            continue
        if bar <= 24:
            drum.hits(fd, bar, 'x...........', vel=0.7)
            snare.hits(rim, bar, '....x...x...', vel=0.35)
        elif bar <= 40:
            kick.hits(kk, bar, 'x...........', duck=True)
            snare.hits(sn, bar, '........x...', vel=0.7)
            hats.hits(hh, bar, '..x.x.x.x.x.', vel=0.55)
            drum.hits(fd, bar, 'x...........', vel=0.6)
        else:
            kick.hits(kk, bar, 'x...x...x...' if bar % 2 else 'x...x...x.x.', duck=True)
            snare.hits(sn, bar, '..x...x...x.' if bar < 57 else '..x.x.x.x.xx', vel=0.8)
            hats.hits(hh, bar, 'xxxxxxxxxxxx', vel=0.6)
            if bar % 4 == 1:
                taiko.hits(tk, bar, 'X...........', vel=0.85)
    snare.hits(sn, 40, '......oooxxX', vel=0.8)
    snare.hits(sn, 60, 'xxxxxxxxXXXX', vel=0.9)
    for bar in (11, 31, 47, 62):
        fx.at(chains, bar, 1.0, gain=1.1, pan=0.5 if bar % 2 else -0.5)
    fx.at(D.crash(length=3.5, variant=1), 61, 0, gain=1.2, pan=0.2)
    fx.at(D.boom(48.0, variant=111), 61, 0, gain=1.0)
    for bar in (9, 25, 41):
        fx.at(D.crash(variant=bar % 3), bar, 0, gain=0.8, pan=-0.3)
    fx.at(D.reverse_cymbal(s.spb * 3, variant=111), 24, 0.0)
    fx.at(D.reverse_cymbal(s.spb * 3, variant=112), 40, 0.0)
    fx.at(D.riser(s.spb * 6, f0=250, f1=7000, q=3.0, variant=111), 59, 0, gain=0.8)
    return s
