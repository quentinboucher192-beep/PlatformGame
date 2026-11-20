"""mecanisme - the score of the two transition films after Asterion (2.82, data/raids/asterion_balancier.json and
asterion_tour.json): « Le Mécanisme », one take per film, not a loop, following the picture second by second.

Character: a clockwork gone mad.  Ticks and escapements, a music box slightly out of tune, low drones, brass that groans
and bends down, swells played backwards.  The time is never still: the tempo speeds up and slows down, the meter
switches every few bars (5/8, 7/8, 3/8, 9/8), a second clock ticks out of phase with the first, and the whole machine
stops dead — then starts again — where the ear does not expect it.

It follows the films: calm, dread-filled ticking while time freezes and in the workshop; the wildest part when Asterion
fights the slowing of time and over the six worlds of the dial; a steady second when the hands of the watch tick again;
then, in the black, the ticking speeds up with the film's own ticks and everything stops at once. The six worlds, the
watch, the eyes and the black (the last 46 s) are written once and played the same way in both films.

Written in seconds (60 BPM, a « bar » of one beat): every event is placed at its time in the film.
"""
import math
import zlib

import numpy as np

from music import drums as D
from music import instruments as I
from music import percussion as P
from music import timbres as T
from music.sequencer import Song
from synth import SR, ns

# The films' timelines (seconds), from their scripts.
FILMS = {
    "balancier": {"length": 113.0, "montage": 67.0,
                  "sections": [("onde", 0, 8), ("tic", 8, 16), ("fige", 16, 24), ("dechirure", 24, 34), ("vieil_homme", 34, 43),
                               ("remonter", 43, 50), ("atelier", 50, 59), ("balancier_vide", 59, 67)]},
    "tour": {"length": 112.0, "montage": 66.0,
             "sections": [("fuite", 0, 10), ("resiste", 10, 19), ("main", 19, 29), ("fils", 29, 39), ("encore", 39, 47),
                          ("chaine", 47, 57), ("atelier", 57, 66)]},
}
GROUPS = {3: (3,), 4: (2, 2), 5: (2, 3), 7: (2, 2, 3), 9: (2, 2, 2, 3)}
LULLABY = [74, 77, 81, 80, 77, 76, 74, 73]       # D5 F5 A5 G#5 F5 E5 D5 C#5: a music box, chromatic dread
CHIME = [76, 72, 74, 67, 67, 74, 76, 72]         # a clock's chime (quarters), bent out of tune


def rng(tag):
    return np.random.default_rng(zlib.crc32(("mecanisme:" + tag).encode()))


def detune(a, cents):
    """`a` played `cents` higher (resampled)."""
    a = np.asarray(a, dtype=float)
    if abs(cents) < 0.5:
        return a
    r = 2.0 ** (cents / 1200.0)
    n = a.shape[-1]
    idx = np.arange(0, n - 1, r)
    if a.ndim == 1:
        return np.interp(idx, np.arange(n), a)
    return np.stack([np.interp(idx, np.arange(n), ch) for ch in a])


def bend(a, c0, c1):
    """`a` gliding from `c0` to `c1` cents (a groan)."""
    a = np.asarray(a, dtype=float)
    n = a.shape[-1]
    rate = 2.0 ** (np.linspace(c0, c1, n) / 1200.0)
    idx = np.cumsum(rate)
    idx = idx[idx < n - 1]
    if a.ndim == 1:
        return np.interp(idx, np.arange(n), a)
    return np.stack([np.interp(idx, np.arange(n), ch) for ch in a])


class Score(Song):
    """A film's score: placed in seconds; its hard stops and its last cut silence everything at once."""

    def __init__(self, name, length):
        super().__init__(name, bpm=60, bar_beats=1, bars=int(math.ceil(length)), loop=False, tail=0.2, target_lufs=-20.0, end_fade=0.0)
        self.reverb.update(seconds=3.6, t60=2.8, predelay=0.02, ret=0.42, hf=0.4, lf=1.0)
        self.delay.update(beats=0.37, feedback=0.32, damp=3000.0, ret=0.16, to_reverb=0.4)
        self.stops = []    # (from, to): the machine stops dead
        self.cut = None    # everything ends here
        self.tick = self.track("tic", gain_db=-6.5, reverb=0.25, highpass=900)
        self.tock = self.track("tac", gain_db=-8.0, reverb=0.3, highpass=120)
        self.clock2 = self.track("horloge2", gain_db=-13.0, reverb=0.35, highpass=1200, pan=0.45)
        self.box = self.track("boite", gain_db=-11.0, reverb=0.55, delay=0.3, highpass=400, pan=-0.2)
        self.drone = self.track("bourdon", gain_db=-10.0, reverb=0.5, highpass=35)
        self.brass = self.track("cuivres", gain_db=-10.0, reverb=0.45, highpass=50)
        self.low = self.track("grave", gain_db=-12.0, reverb=0.25, lowpass=400, highpass=28)
        self.fx = self.track("effets", gain_db=-11.0, reverb=0.5)

    def stopped(self, t):
        return any(a <= t < b for a, b in self.stops) or (self.cut is not None and t >= self.cut)

    def put(self, track, audio, t, gain=1.0, pan=None):
        if t < 0 or self.stopped(t):
            return
        a = np.asarray(audio, dtype=float)
        # nothing rings into a hard stop or past the cut
        n = a.shape[-1]
        ends = [s for s, _ in self.stops if s > t] + ([self.cut] if self.cut is not None and self.cut > t else [])
        if ends:
            m = int((min(ends) - t) * SR)
            if m < n:
                a = a[..., :m].copy()
                f = min(m, ns(0.006))
                if f > 0:
                    a[..., m - f:] *= np.linspace(1, 0, f)
        track.add(a, int(round(t * SR)), pan, gain)

    def render(self, log=print):
        stereo, info = super().render(log=log)
        env = np.ones(stereo.shape[1])
        ramp = ns(0.006)
        for a, b in self.stops + ([(self.cut, stereo.shape[1] / SR + 1)] if self.cut is not None else []):
            i0, i1 = int(a * SR), min(stereo.shape[1], int(b * SR))
            env[i0:i1] = 0.0
            if i0 > ramp:
                env[i0 - ramp:i0] = np.minimum(env[i0 - ramp:i0], np.linspace(1, 0, ramp))
            if i1 + ramp < len(env) and i1 < stereo.shape[1]:
                env[i1:i1 + ramp] = np.minimum(env[i1:i1 + ramp], np.linspace(0, 1, ramp))
        return stereo * env, info


# =================================================================== the pieces of the machine
def drone(s, t0, t1, notes=(38, 44), vel=0.5, frost=False, tag="d"):
    """A low drone (D and G#: a tritone) from t0 to t1, cut by the stops."""
    for k, nt in enumerate(notes):
        inst = T.pad_frost if frost else I.pad_warm
        a = inst(nt, max(0.5, t1 - t0), vel * (1.0 - 0.2 * k), attack=1.2, release=1.4)
        s.put(s.drone, detune(a, (-8, 11)[k % 2]), t0)


def groan(s, t, nt=38, dur=2.4, vel=0.6, c1=-90):
    """Brass that groans, bending down."""
    s.put(s.brass, bend(T.brass_low(nt, dur, vel, bright=2.4, growl=0.4), 0, c1), t)


def swell(s, t_end, seconds=2.0, variant=0, gain=0.8):
    """A swell played backwards, ending at t_end."""
    s.put(s.fx, D.reverse_cymbal(seconds, variant=variant), t_end - seconds, gain)


def box(s, t, nt, cents=0.0, vel=0.5):
    s.put(s.box, detune(I.celesta(nt, 0.9, vel), cents), t)


def clockwork(s, t0, t1, tag, meters=(7,), bpm=(120, 120), jitter=0.0, motif=None, motif_every=2, second=None, low=True,
              accent_gain=1.0, tick_gain=0.8, drift=0.0):
    """Ticks in odd meters: each bar its meter (eighths, grouped 2+2+3…), its tempo from bpm[0] to bpm[1] (and a jitter
    per bar), an accented tock on each group, a low pulse on each bar; a music box playing `motif` every `motif_every`
    eighths, drifting out of tune by `drift` cents; a second clock out of phase (`second`: its period in eighths)."""
    r = rng(tag)
    t, bar, step = float(t0), 0, 0
    while t < t1 - 1e-6:
        m = meters[bar % len(meters)]
        u = (t - t0) / max(1e-6, t1 - t0)
        q = bpm[0] + (bpm[1] - bpm[0]) * u
        q *= 1.0 + float(r.uniform(-jitter, jitter))
        e = 30.0 / q
        k = 0
        for gi, g in enumerate(GROUPS[m]):
            for j in range(g):
                tt = t + k * e
                if tt >= t1:
                    break
                if j == 0:
                    s.put(s.tock, D.rim(variant=(bar + gi) % 4), tt, accent_gain * (0.9 if gi == 0 else 0.6))
                else:
                    s.put(s.tick, P.ice_tick(freq=3600 + 300 * ((step + j) % 3), variant=step % 5), tt, tick_gain)
                if motif and step % motif_every == 0:
                    nt = motif[(step // motif_every) % len(motif)]
                    box(s, tt, nt, cents=drift * math.sin(tt * 0.7) + float(r.uniform(-6, 6)), vel=0.45)
                k += 1
                step += 1
        if low:
            s.put(s.low, D.tom(58.0 + 4 * (bar % 2), decay=0.3, variant=bar % 3), t, 0.7)
        t += m * e
        bar += 1
    if second:
        r2 = rng(tag + ":2")
        e2 = 30.0 / bpm[0] * second
        t = t0 + float(r2.uniform(0.05, 0.2))
        while t < t1:
            s.put(s.clock2, P.metal_hat(variant=int(t * 7) % 4), t, 0.6)
            t += e2 * (1.0 + 0.03 * math.sin(t))


def pendulum(s, t0, t1, half, vel=0.7, first_tick=True):
    """Tick, tock: one at each end of a swing of `half` seconds."""
    t, k = t0, 0
    while t < t1:
        s.put(s.tock if (k % 2) != first_tick else s.tick, D.rim(variant=k % 4) if (k % 2) != first_tick else P.ice_tick(freq=2800, variant=k % 5), t, vel)
        t += half
        k += 1


# =================================================================== the sections
def sec_onde(s, a, b):
    drone(s, a, b + 2, vel=0.45)
    swell(s, a + 3.0, 3.0, 1, 0.6)
    groan(s, a + 4.0, 38, 3.5, 0.45, -60)
    swell(s, b, 2.5, 2, 0.7)


def sec_tic(s, a, b):  # the film's own ticks lead; the music only shivers when they go wrong
    drone(s, a, b, notes=(38,), vel=0.35, frost=True)
    for k, t in enumerate((4.35, 4.55, 5.7, 5.82, 6.95, 7.45)):
        box(s, a + t + 0.05, LULLABY[k], cents=-25 - 15 * k, vel=0.4)


def sec_fige(s, a, b):
    clockwork(s, a, a + 2.35, "fige", meters=(5,), bpm=(96, 120), motif=LULLABY, motif_every=3, low=False, tick_gain=0.5)
    s.stops.append((a + 2.4, a + 3.1))  # everything stops with the frozen time
    drone(s, a + 3.1, b + 1, notes=(62, 68), vel=0.35, frost=True)
    t = a + 3.6
    while t < b:
        s.put(s.tick, P.ice_tick(freq=4200, decay=0.03), t, 0.5)
        t += 1.6


def sec_dechirure(s, a, b):
    drone(s, a, b, vel=0.45)
    swell(s, a + 2.6, 2.4, 3, 0.9)
    s.put(s.low, P.sub_hit(42.0, variant=1), a + 2.6, 1.0)
    groan(s, a + 2.7, 38, 3.0, 0.7, -120)
    clockwork(s, a + 3.0, b, "dechirure", meters=(7, 5, 7, 9), bpm=(130, 200), jitter=0.12, motif=LULLABY, motif_every=3, second=1.5, drift=40)


def sec_dread(s, a, b, tag, bpm=(52, 46), meters=(5,), motif=LULLABY, groan_at=None):
    drone(s, a, b, vel=0.4)
    clockwork(s, a, b, tag, meters=meters, bpm=bpm, jitter=0.06, motif=motif, motif_every=4, low=True, accent_gain=0.6, tick_gain=0.45, drift=20)
    if groan_at is not None:
        groan(s, a + groan_at, 36, 3.0, 0.45, -70)


def sec_balancier_vide(s, a, b):
    drone(s, a, b, notes=(37, 43), vel=0.4)
    pendulum(s, a + 1.6, b, 1.6, 0.6)
    groan(s, a + 3.0, 34, 3.0, 0.4, -50)
    swell(s, b, 2.0, 4, 0.8)


def sec_fuite(s, a, b):
    drone(s, a, b, vel=0.45)
    clockwork(s, a, b, "fuite", meters=(7, 7, 5, 7), bpm=(150, 176), jitter=0.05, motif=None, low=True, second=1.5)
    for t in (1.0, 5.2):
        groan(s, a + t, 38, 2.6, 0.55, -100)


def sec_resiste(s, a, b):
    drone(s, a, a + 5.6, vel=0.5)
    s.put(s.low, P.sub_hit(40.0, variant=2), a + 1.3, 1.0)
    groan(s, a + 1.3, 36, 2.4, 0.8, -150)
    clockwork(s, a + 1.6, a + 5.4, "resiste", meters=(7, 5, 3, 9, 5, 7, 3), bpm=(190, 60), jitter=0.2, motif=LULLABY, motif_every=2, second=1.33, drift=60)
    s.stops.append((a + 3.05, a + 3.35))
    s.stops.append((a + 4.4, a + 4.6))
    drone(s, a + 5.6, b, notes=(62, 68), vel=0.35, frost=True)
    t = a + 6.0
    while t < b:
        s.put(s.tick, P.ice_tick(freq=4200, decay=0.03), t, 0.45)
        t += 1.4


def sec_main(s, a, b):
    drone(s, a, b, vel=0.45)
    swell(s, a + 0.4, 1.4, 5, 0.7)
    clockwork(s, a + 0.6, a + 3.05, "main", meters=(5, 7), bpm=(110, 190), jitter=0.04, motif=None, second=1.5)
    s.stops.append((a + 3.05, a + 3.5))
    s.put(s.low, P.sub_hit(44.0, variant=3), a + 3.5, 1.0)
    s.put(s.brass, T.brass_stab(38, 0.5, 0.9, bright=3.0), a + 3.5, 0.9)
    clockwork(s, a + 4.2, b, "main2", meters=(5,), bpm=(70, 64), motif=None, accent_gain=0.9, tick_gain=0.4)
    groan(s, a + 5.0, 34, 3.0, 0.55, -80)


def sec_fils(s, a, b):
    drone(s, a, b, notes=(50, 56), vel=0.35, frost=True)
    clockwork(s, a, b, "fils", meters=(5, 5, 7), bpm=(88, 96), jitter=0.04, motif=LULLABY, motif_every=1, low=False, second=1.25, drift=30,
              tick_gain=0.4)


def sec_chaine(s, a, b):
    drone(s, a, b, vel=0.45)
    swell(s, a + 2.2, 2.0, 6, 0.8)
    s.put(s.low, P.sub_hit(42.0, variant=4), a + 2.2, 0.9)
    clockwork(s, a + 2.4, a + 4.2, "chaine", meters=(7,), bpm=(200, 160), jitter=0.1, motif=None)
    pendulum(s, a + 4.2, b, 1.25, 0.55)
    groan(s, a + 5.0, 36, 3.0, 0.5, -60)


def sec_atelier(s, a, b):  # the same in both films
    drone(s, a, b, notes=(38, 44), vel=0.35)
    t, k = a + 0.4, 0
    r = rng("atelier")
    while t < b - 0.8:  # a few clocks that still try, unevenly
        s.put(s.tick, P.ice_tick(freq=3000 + 400 * (k % 3), variant=k % 5), t, 0.35)
        t += float(r.uniform(0.6, 1.4))
        k += 1
    for k, nt in enumerate(CHIME[:4]):
        box(s, a + 4.0 + 0.9 * k, nt, cents=-30 + 12 * k, vel=0.4)
    swell(s, b, 2.0, 7, 0.7)


# =================================================================== the shared ending (the six worlds, the watch, the eyes, black)
WORLDS = [  # (meters, bpm from, bpm to, jitter, motif)
    ((7, 5, 7, 3), 150, 220, 0.1, LULLABY),   # vines growing too fast: accelerating
    ((9,), 40, 36, 0.0, CHIME),               # snow in slow motion
    ((5,), 132, 132, 0.0, None),              # the forge: one bar, again and again
    ((7, 5, 3), 60, 160, 0.15, LULLABY[::-1]),  # ruins rewinding
    ((5,), 90, 90, 0.0, None),                # the archives: stuck
    ((7, 5), 96, 96, 0.05, CHIME),            # the dial: two clocks out of phase
]


def ending(s, m0):
    """From the start of the montage (m0): the six worlds (4 s each), the watch (9 s), the eyes (6 s), black (7 s)."""
    for i, (meters, b0, b1, jit, motif) in enumerate(WORLDS):
        a, b = m0 + 4.0 * i, m0 + 4.0 * (i + 1)
        tag = f"monde{i}"
        drone(s, a, b, notes=(38 + i, 44 + i), vel=0.4, frost=i in (1, 4))
        if i == 2:  # the forge: the same bar, stopped and started again
            for k in range(4):
                clockwork(s, a + 0.95 * k, a + 0.95 * k + 0.8, tag, meters=meters, bpm=(b0, b0), motif=None)
                s.stops.append((a + 0.95 * k + 0.8, a + 0.95 * k + 0.95))
        elif i == 4:  # the archives: a tick caught between two seconds, then nothing
            for k in range(4):
                s.put(s.tick, P.ice_tick(freq=3300, variant=0), a + 0.3 + 0.7 * k, 0.5)
            s.put(s.box, detune(I.celesta(81, 2.5, 0.4), -35), a + 0.2)
            s.stops.append((a + 3.2, b))
        else:
            clockwork(s, a, b, tag, meters=meters, bpm=(b0, b1), jitter=jit, motif=motif, motif_every=2, second=1.5 if i in (0, 5) else None,
                      drift=45 if i != 1 else 10, tick_gain=0.7 if i != 1 else 0.4)
        if i in (0, 3):
            s.stops.append((b - 0.35, b))
        swell(s, b, 1.2, 8 + i, 0.5)
    # the watch near Lyse: its hands find their second again (one tick a second, with the film's), its gong, its chime
    c = m0 + 24.0
    drone(s, c, c + 9.0, notes=(38, 45), vel=0.45)
    clockwork(s, c, c + 4.4, "cadran_ralenti", meters=(7, 5), bpm=(220, 70), jitter=0.05, motif=None, low=False, tick_gain=0.5)
    pendulum(s, c + 4.4, c + 9.0, 1.0, 0.45)
    s.put(s.low, P.sub_hit(38.0, variant=5), c + 3.44, 0.9)
    s.put(s.brass, bend(T.brass_low(38, 4.0, 0.55, bright=2.6), 0, -30), c + 3.44, 0.8)
    for k, nt in enumerate(CHIME):
        box(s, c + 3.6 + 0.55 * k, nt, cents=-12 - 4 * k, vel=0.5)
    # the eyes: the same second, the drone sinking out of tune
    e = c + 9.0
    for k, nt in enumerate((38, 44)):
        s.put(s.drone, bend(I.pad_warm(nt, 6.0, 0.4, attack=0.6, release=1.0), 0, -60 - 30 * k), e)
    pendulum(s, e, e + 6.0, 1.0, 0.5)
    for k in range(4):
        box(s, e + 0.5 + 1.2 * k, CHIME[(k * 3) % 8] + 12, cents=-40 - 10 * k, vel=0.35)
    # black: the ticking speeds up with the film's, then the whole machine stops
    n0 = e + 6.0
    t, gap, ticks = 0.25, 0.56, []
    while t < 3.95:
        ticks.append(t)
        t += gap
        gap = max(0.055, gap * 0.84)
    s.put(s.drone, bend(I.pad_warm(38, 4.5, 0.45, attack=1.5, release=0.5), 0, 200), n0)
    for k, t in enumerate(ticks):
        s.put(s.low, D.tom(70.0 + 3 * k, decay=0.12), n0 + t, 0.5 + 0.02 * k)
    s.cut = n0 + ticks[-1] + 0.06


def build_film(film):
    f = FILMS[film]
    s = Score("mecanisme_" + film, f["length"])
    for name, a, b in f["sections"]:
        if name in ("vieil_homme", "encore"):
            sec_dread(s, a, b, name, bpm=(50, 44), meters=(5,), groan_at=3.0)
        elif name == "remonter":
            sec_dread(s, a, b, name, bpm=(48, 80), meters=(5, 7), groan_at=1.0)
        else:
            globals()["sec_" + name](s, a, b)
    ending(s, f["montage"])
    return s
