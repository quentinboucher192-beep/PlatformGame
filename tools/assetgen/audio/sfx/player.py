"""Player state sounds: hurt, death, respawn, energy low."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('player_hurt', -14, 'player')
def player_hurt(rng):
    d = 0.3
    n = ns(d)
    crack = filt(noise(0.06, rng), bp(2200, 0.8)) * perc(ns(0.06), 0.0005, 0.014)
    f = glide(n, 560, 210, 0.16, 'exp')
    oof = svf(square(f, n, 0.4) + 0.6 * saw(f * 1.01, n), 2400, 1.2, 'lp') * perc(n, 0.002, 0.07)
    thud = thump(140, 55, 0.2, tau=0.02, decay=0.05, drive=1.5)
    return soft_clip(mix(crack * 0.9, oof * 0.35, thud * 0.8), 1.4)


@sound('player_death', -13, 'player', fade_out=0.3)
def player_death(rng):
    # sad descending FM line (D minor) dissolving into cyan particles
    notes = ['A5', 'F5', 'D5', 'A4', 'D4']
    line = arp(notes, 0.12, lambda f, i: bell(f, 0.7, ratio=1.0, index=1.6, decay=0.18 + 0.05 * i,
                                               idx_decay=0.05), vel=[1, 0.9, 0.85, 0.8, 0.9])
    d = 1.5
    n = ns(d)
    drop = sine(points(n, [(0, 330), (0.7, 120), (d, 70)], 'exp'), n) * perc(n, 0.005, 0.3) * 0.35
    hit = thump(150, 50, 0.3, tau=0.03, decay=0.07, click=0.5, rng=rng) * 0.6
    dissolve = sparkle(d, rng, count=40, fmin=2500, fmax=9000, grain=0.05,
                       density=[0.0, 0.3, 1.0, 1.0, 0.6, 0.2]) * 0.35
    shimmer = filt(noise(d, rng), bp(6000, 1.0)) * points(n, [(0, 0), (0.4, 0.2), (d, 0)]) * 0.4
    x = place([(0.0, hit), (0.0, drop), (0.05, line * 0.45), (0.1, dissolve), (0.0, shimmer)])
    return room(x, rng, t60=1.2, wet=0.25)


@sound('respawn', -16, 'player', fade_out=0.15)
def respawn(rng):
    notes = ['D4', 'A4', 'D5', 'F#5', 'A5', 'D6']
    line = arp(notes, 0.085, lambda f, i: bell(f, 0.6, ratio=2.0, index=1.4, decay=0.2,
                                                idx_decay=0.04), vel=[0.5, 0.6, 0.7, 0.8, 0.9, 1.0])
    d = 1.0
    n = ns(d)
    gather = sparkle(d, rng, count=30, fmin=3000, fmax=9500, grain=0.05,
                     density=[0.3, 0.6, 1.0, 1.0, 0.4]) * 0.3
    rise = whoosh(0.7, rng, (500, 6000), q=1.5, peak_at=0.85, color='white') * 0.3
    pad = np.zeros(n)
    for nt in ('D4', 'A4', 'F#5'):
        pad += fm(note_hz(nt), n, 2.0, 0.5)
    pad *= points(n, [(0, 0), (0.45, 0.25), (d, 0)])
    x = place([(0.0, rise), (0.0, gather), (0.25, line * 0.5), (0.0, pad * 0.35)])
    return room(x, rng, t60=1.0, wet=0.25)


@sound('energy_low', -19, 'player', fade_out=0.03)
def energy_low(rng):
    def beep(f):
        n = ns(0.11)
        return (sine(f, n) + 0.3 * triangle(f, n)) * adsr(n, 0.005, 0.03, 0.6, 0.04, 0.07)
    return place([(0.0, beep(659.3)), (0.16, beep(440.0)), (0.32, beep(440.0) * 0.6)])
