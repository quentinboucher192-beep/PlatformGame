"""titan_boss - le Titan Mécanique (giga boss of Les Confins). E-flat minor (the B-flat major dominant,
the Neapolitan F-flat), 120 BPM, 4/4, 36 bars = 72.0 s.

Character: an industrial war machine.  A sequenced resonant synth bass, a machine groove of
metal (struck steel, clangs grouped 3-3-3-3-2-2 against the four-on-the-floor kick, steel hats,
hammers, pistons of steam, chains), a motor that spins up, gears ratcheting, syncopated brass
stabs, the titan's theme on low brass and a servo synth lead, an alarm siren in the overdrive.

Form:  intro 1-4 (the motor spins up, gears, steam, the bass sequence fades in)
       | A 5-12 (the machine groove: sequence, metal, stabs)
       | B 13-20 (the titan's theme on low brass and servo lead, choir, the filter opens)
       | jam 21-24 (the machine stalls: pistons, hammer heartbeat, the motor, the alarm)
       | overdrive 25-36 (double time, the theme an octave up, siren, a riser into the loop).
"""
import numpy as np

from dsp import bp, filt, hp, lp, soft_clip
from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.drums import _ro, _rng
from music.instruments import _spread, instrument
from music.sequencer import Song
from music.textures import blocks
from synth import SR, adsr, cents, midi_hz, morph_osc, note_midi, ns, perc, points, vibrato, white

NAME = 'titan_boss'
BPM, BEATS_PER_BAR, BARS, LOOP = 120, 4, 36, True

# name: (bass root, stab voicing, choir voicing)
CHORD = {
    'Ebm': ('Eb2', ['Eb4', 'Gb4', 'Bb4'], ['Eb4', 'Gb4', 'Bb4']),
    'Cb': ('Cb2', ['Eb4', 'Gb4', 'Cb5'], ['Eb4', 'Gb4', 'Cb5']),
    'Db': ('Db2', ['Db4', 'F4', 'Ab4'], ['Db4', 'F4', 'Ab4']),
    'Bb': ('Bb1', ['D4', 'F4', 'Bb4'], ['D4', 'F4', 'Bb4']),
    'Abm': ('Ab1', ['Eb4', 'Ab4', 'Cb5'], ['Eb4', 'Ab4', 'Cb5']),
    'Fb': ('Fb2', ['Fb4', 'Ab4', 'Cb5'], ['Fb4', 'Ab4', 'Cb5']),
}

PROG = (['Ebm', 'Ebm', 'Ebm', 'Fb'] +                                       # 1-4 intro
        ['Ebm', 'Ebm', 'Cb', 'Db', 'Ebm', 'Ebm', 'Cb', 'Bb'] +              # 5-12 A
        ['Abm', 'Cb', 'Ebm', 'Ebm', 'Abm', 'Fb', 'Bb', 'Bb'] +              # 13-20 B
        ['Ebm', 'Fb', 'Ebm', 'Fb'] +                                        # 21-24 jam
        ['Ebm', 'Cb', 'Db', 'Bb', 'Abm', 'Cb', 'Ebm', 'Ebm',
         'Abm', 'Fb', 'Bb', 'Bb'])                                          # 25-36 overdrive

# the sequence: (16th step, interval, accent)
SEQ = [(0, 0, 1), (2, 0, 0), (3, 12, 0), (4, 0, 1), (6, 0, 0), (7, 7, 0), (8, 0, 1), (10, 0, 0), (11, 12, 0),
       (12, 0, 1), (13, 10, 0), (14, 7, 1), (15, 0, 0)]
GROUPS = 'X..x..x..x..X.x.'      # 3-3-3-3-2-2 against the four-on-the-floor
THEME = ('Ab4:1.5 Gb4:.5 Eb4 Cb4 | Eb4:1.5 Gb4:.5 Bb4:2 | Bb4 Gb4 Eb4 Gb4 | Eb5:3 Db5 | '
         'Cb5:1.5 Bb4:.5 Ab4 Eb4 | Fb4:1.5 Ab4:.5 Cb5:2 | D5 F5 Bb4 D5 | F5:2 D5 Bb4 |')
DRIVE = ('Eb5:.5 Eb5:.5 Gb5:.5 Eb5:.5 Bb5 Gb5 | Eb5:.5 Eb5:.5 Gb5:.5 Eb5:.5 Cb6 Bb5 | '
         'F5:.5 F5:.5 Ab5:.5 F5:.5 Db6 Ab5 | D6:2 F6 D6 |')


@instrument
def servo_lead(midi, dur, vel, seed, bright=6.0, drive=2.2, vib=6.0):
    """Servo synth lead: square and saw a few cents apart through a snapping resonant filter."""
    f = float(midi_hz(midi))
    n = ns(dur + 0.12)
    t = np.arange(n) / SR
    rng = np.random.default_rng(int(midi) * 104729 + seed)
    v = vibrato(n, 5.6, vib, delay=0.2, fade=0.3, phase0=rng.random())
    cut = np.minimum(f * (1.5 + bright * vel * np.exp(-t / 0.12) + 1.5 * np.clip(t / 0.4, 0, 1)), 7000.0)
    a = morph_osc(f * v, n, cut, 'square', q=1.6, poles=4, k_tables=10, phase0=rng.random())
    b = morph_osc(f * float(cents(7.0)) * v, n, cut, 'saw', q=1.6, poles=4, k_tables=10, phase0=rng.random())
    st = _spread([a, b], 0.5)
    env = adsr(n, 0.005, 0.15, 0.8, 0.1, dur)
    return soft_clip(st * env * 0.7, drive) * vel * 0.5


@_ro
def motor(seconds, freq=38.9, variant=0):
    """A motor spinning up: a low saw drone whose speed and filter rise over `seconds` (mono)."""
    n = ns(seconds)
    t = np.arange(n) / SR
    f = points(n, [(0, freq * 0.5), (seconds * 0.7, freq * 0.97), (seconds, freq)], 'exp')
    cut = points(n, [(0, 120.0), (seconds, 1400.0)], 'exp')
    s = morph_osc(f, n, cut, 'saw', q=1.4, poles=4, k_tables=12) + 0.5 * morph_osc(f * 2.01, n, cut, 'square',
                                                                                    q=1.2, poles=4, k_tables=12)
    env = (0.35 + 0.65 * np.clip(t / (seconds * 0.4), 0, 1)) * np.clip((seconds - t) / 0.08, 0, 1)
    return soft_clip(s * env * 0.6, 1.8)


@_ro
def ratchet(seconds, rate0=6.0, rate1=28.0, variant=0):
    """Gears ratcheting faster and faster: steel clicks through two resonances (mono)."""
    n = ns(seconds)
    rng = _rng(f'ratchet{variant}')
    rate = points(n, [(0, rate0), (seconds, rate1)], 'exp')
    ph = np.cumsum(rate) / SR
    idx = np.nonzero(np.diff(np.floor(ph)) > 0)[0]
    imp = np.zeros(n)
    imp[idx] = rng.uniform(0.6, 1.0, len(idx))
    tick = filt(imp, bp(2600.0, 6.0))[:n] + 0.6 * filt(imp, bp(5200.0, 8.0))[:n] + 0.3 * filt(imp, bp(900.0, 4.0))[:n]
    return tick * np.clip((seconds - np.arange(n) / SR) / 0.05, 0, 1) * 3.0


@_ro
def siren(seconds, f0=600.0, f1=1100.0, cycles=2, variant=0):
    """An alarm siren rising and falling `cycles` times, through a horn-like band (mono)."""
    n = ns(seconds)
    t = np.arange(n) / SR
    f = f0 + (f1 - f0) * (0.5 - 0.5 * np.cos(2 * np.pi * cycles * t / seconds))
    ph = np.cumsum(f) / SR
    s = np.sign(np.sin(2 * np.pi * ph)) * 0.4 + np.sin(2 * np.pi * ph)
    s = filt(s, bp(1200.0, 0.9), lp(3500.0, 0.7))[:n]
    env = np.sin(np.pi * np.clip(t / seconds, 0, 1)) ** 0.5
    return s * env * 0.5


def build():
    s = Song(NAME, bpm=BPM, bar_beats=BEATS_PER_BAR, bars=BARS, target_lufs=-13.0)
    s.reverb.update(seconds=2.6, t60=1.6, predelay=0.015, ret=0.26, hf=0.45, lf=1.0)
    s.delay.update(beats=0.75, feedback=0.3, damp=4000.0, ret=0.16, to_reverb=0.3)
    s.duck_tau = 0.08

    seq = s.track('sequence', gain_db=-5.0, duck=0.45, lowpass=6000, highpass=36)
    sub = s.track('sub', gain_db=-9.0, duck=0.4, lowpass=150, highpass=34)
    stabs = s.track('stabs', gain_db=-7.5, reverb=0.25, highpass=150)
    brass = s.track('brass', gain_db=-7.0, reverb=0.3, highpass=70)
    lead = s.track('servo lead', gain_db=-6.0, reverb=0.3, delay=0.2, highpass=250, pan=0.1)
    choir = s.track('choir', gain_db=-5.0, reverb=0.55, duck=0.2, highpass=160)
    kick = s.track('kick', gain_db=-6.5, reverb=0.03)
    snare = s.track('snare', gain_db=-5.0, reverb=0.25)
    metal = s.track('metal', gain_db=-4.0, reverb=0.2, pan=-0.2, highpass=300)
    hats = s.track('hats', gain_db=2.0, reverb=0.1, pan=0.25, highpass=4000)
    hammers = s.track('hammers', gain_db=-2.0, reverb=0.25)
    machine = s.track('machine', gain_db=-5.0, reverb=0.25, highpass=40)
    steam = s.track('steam', gain_db=-12.0, reverb=0.25, highpass=1500)
    alarm = s.track('alarm', gain_db=-16.0, reverb=0.4, delay=0.2, highpass=400)
    cym = s.track('cymbals', gain_db=-12.0, reverb=0.3)
    fx = s.track('fx', gain_db=-9.0, reverb=0.4)

    for bar, nb, name in blocks(PROG):
        root, st_v, ch_v = CHORD[name]
        r = note_midi(root)
        sub.note(I.bass_sub, bar, 0, nb * 4 - 0.1, r if r >= 34 else r + 12, vel=0.75, attack=0.02, release=0.2,
                 drive=1.5)
        if 13 <= bar <= 20 or bar >= 29:
            choir.chord(I.pad_choir, bar, 0, nb * 4 - 0.1, ch_v, vel=0.65, vowel='a', attack=0.15, release=0.7)

    for bar, name in enumerate(PROG, start=1):
        root, st_v, ch_v = CHORD[name]
        r = note_midi(root)
        jam = 21 <= bar <= 24
        over = bar >= 25
        # the sequence: closed in the intro, opening through the song, stuttering in the jam
        if bar >= 2:
            peak = 3.0 if bar <= 4 else (5.0 if bar <= 12 else (7.0 if bar <= 20 else 8.5))
            for step, iv, acc in SEQ:
                if jam and step % 4:
                    continue
                v = 0.95 if acc else 0.72
                if bar <= 4:
                    v *= 0.6 + 0.15 * (bar - 2)
                seq.note(I.bass_pluck, bar, step * 0.25, 0.22, r + iv, vel=round(v, 2), peak=peak, floor=1.3,
                         tau=0.07, q=2.6, drive=2.4, sub=0.4, detune=8.0)
        # stabs: syncopated, on the 3-3-2 of each half bar
        if 5 <= bar <= 20 or over:
            for beat in (0, 0.75, 1.5, 2.0, 2.75, 3.5):
                if beat in (2.0,) and bar <= 12:
                    continue
                stabs.chord(T.brass_stab, bar, beat, 0.22, st_v, vel=0.8 if beat in (0, 2.0) else 0.62, bright=4.5)

    brass.line(T.brass_low, 13, THEME, vel=0.9, legato=0.9, transpose=-12, bright=3.4, max_cut=2600.0, growl=0.3)
    lead.line(servo_lead, 13, THEME, vel=0.75, legato=0.9)
    lead.line(servo_lead, 25, DRIVE, vel=0.85, legato=0.85, bright=7.0)
    brass.line(T.brass_low, 25, DRIVE, vel=0.75, legato=0.85, transpose=-24, bright=3.0, max_cut=2200.0)
    lead.line(servo_lead, 29, THEME, vel=0.9, legato=0.9, transpose=12, bright=7.0)
    brass.line(T.brass_low, 29, THEME, vel=0.95, legato=0.9, transpose=-12, bright=3.6, max_cut=2800.0, growl=0.35)

    # ---- the machine
    kk = D.kick(f_start=180, f_end=48, pitch_tau=0.022, decay=0.22, click=0.5, drive=2.6, variant=121)
    sn = D.snare(tone=210, tone_decay=0.05, noise_decay=0.12, noise_lp=9000, tone_mix=0.5, roomy=0.25, variant=121)
    cl = P.clang(1320.0, decay=0.25, variant=121)
    cl_lo = P.clang(760.0, decay=0.35, variant=122)
    mh = P.metal_hat(decay=0.03, length=0.1, variant=121)
    hm = P.hammer(variant=121, freq=700.0)
    sh = P.steam(0.5, variant=121)
    for bar in range(1, BARS + 1):
        if bar <= 4:
            hats.hits(mh, bar, 'x.x.x.x.x.x.x.x.' if bar >= 2 else '........x.x.x.x.', vel=0.3 + 0.1 * bar)
            continue
        if 21 <= bar <= 24:
            hammers.hits(hm, bar, 'x..x............' if bar % 2 else 'x..x......x.....', vel=0.85)
            steam.hits(sh, bar, '..x.......x.....', vel=0.8)
            metal.hits(cl_lo, bar, '........x.......', vel=0.6)
            continue
        over = bar >= 25
        kick.hits(kk, bar, 'x...x...x...x...', duck=True)
        snare.hits(sn, bar, '....x.......x...')
        metal.hits(cl, bar, '....x.......x...', vel=0.6)
        metal.hits(cl_lo, bar, GROUPS, vel=0.45)
        hats.hits(mh, bar, 'xgxgxgxgxgxgxgxg' if not over else 'xxxxxxxxxxxxxxxx', vel=0.6)
        hammers.hits(hm, bar, '..x.......x.....' if bar % 2 else '..x.......x...x.', vel=0.55)
        if bar % 4 == 0:
            steam.hits(sh, bar, '......x.........', vel=0.6)
    snare.hits(sn, 12, '........o.o.xxXX', vel=0.75)
    snare.hits(sn, 20, '........ooooxxXX', vel=0.8)
    snare.hits(sn, 36, 'oooooooooooxxxXX', vel=0.85)
    machine.at(motor(s.spb * 16, 38.9, variant=1), 1, 0, gain=1.0)
    machine.at(motor(s.spb * 16, 38.9, variant=2), 21, 0, gain=0.8)
    fx.at(ratchet(s.spb * 8, 5.0, 30.0, variant=1), 3, 0, gain=0.9, pan=0.4)
    fx.at(ratchet(s.spb * 4, 8.0, 24.0, variant=2), 24, 0, gain=0.8, pan=-0.4)
    for bar in (1, 21):
        steam.at(P.steam(1.6, variant=bar + 120), bar, 0, gain=1.0, pan=-0.3)
    for bar in (9, 17, 31):
        fx.at(P.chain(0.6, 10, variant=bar + 120), bar, 2.0, gain=1.1, pan=0.5 if bar % 2 else -0.5)
    for bar in (25, 29, 33):
        alarm.at(siren(s.spb * 8, variant=bar), bar, 0, pan=-0.2 if bar % 2 else 0.2)
    alarm.at(siren(s.spb * 8, 450.0, 800.0, 1, variant=21), 21, 0)
    for bar in (5, 13, 25, 29):
        cym.at(D.crash(variant=bar % 3), bar, 0, pan=-0.3 if bar % 2 else 0.3)
    for bar in (1, 5, 25):
        fx.at(D.boom(46.0, variant=bar + 120), bar, 0, gain=0.9)
    metal.at(cl_lo, 1, 0, gain=1.0)
    cym.at(D.crash(variant=2), 1, 0, pan=0.3)
    for bar in (12, 20):
        fx.at(D.reverse_cymbal(s.spb * 4, variant=bar + 120), bar, 0.0)
    fx.at(D.riser(s.spb * 8, f0=200, f1=8000, q=3.0, variant=121), 35, 0, gain=0.8)
    fx.at(P.sub_hit(44.0, length=1.6, variant=121), 21, 0, gain=1.1)
    return s
