"""Player movement sounds: jump, double jump, land, footsteps, wall jump, slide, dash, bounce."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('jump', -19, 'movement')
def jump(rng):
    d = 0.2
    body = chirp(270, 720, d, 'sine', time=0.09, decay=0.07)
    edge = chirp(270, 720, d, 'square', time=0.09, decay=0.045, lowpass=2600) * 0.18
    air = whoosh(0.13, rng, (900, 3200), q=0.9, peak_at=0.25) * 0.35
    return mix(body + edge, air)


@sound('double_jump', -18, 'movement')
def double_jump(rng):
    a = chirp(380, 820, 0.12, 'sine', time=0.06, decay=0.045)
    b = chirp(560, 1250, 0.22, 'sine', time=0.08, decay=0.07)
    b += chirp(560, 1250, 0.22, 'tri', time=0.08, decay=0.05) * 0.25
    swirl = whoosh(0.28, rng, (700, 4200, 2000), q=1.4, peak_at=0.35) * 0.4
    glint = sparkle(0.3, rng, count=6, fmin=3500, fmax=7000, grain=0.05) * 0.2
    return place([(0.0, a), (0.055, b), (0.02, swirl), (0.06, glint)])


@sound('land', -20, 'movement')
def land(rng):
    body = thump(115, 48, 0.22, tau=0.03, decay=0.055)
    dirt = filt(noise(0.12, rng, 'pink'), lp(900, 0.7)) * perc(ns(0.12), 0.001, 0.028) * 0.7
    grit = debris(0.12, rng, count=7, fmin=1200, fmax=3500, grain=(0.002, 0.008)) * 0.25
    return mix(body, dirt, grit)


def _footstep(rng, f_body, f_tick, squish):
    d = 0.085
    n = ns(d)
    tick = filt(white(n, rng), bp(f_tick, 1.1), lp(4500)) * perc(n, 0.0008, 0.012)
    pad = filt(noise(d, rng, 'pink'), lp(1100)) * perc(n, 0.002, 0.02) * 0.8
    low = thump(f_body * 1.4, f_body, d, tau=0.015, decay=0.025, drive=0) * 0.55
    moss = filt(white(n, rng), bp(squish, 3.0)) * perc(n, 0.006, 0.018) * 0.25
    return mix(tick * 0.9, pad, low, moss)


@sound('footstep_a', -28, 'movement')
def footstep_a(rng):
    return _footstep(rng, 92, 1900, 650)


@sound('footstep_b', -28, 'movement')
def footstep_b(rng):
    return _footstep(rng, 84, 1600, 780)


@sound('wall_jump', -19, 'movement')
def wall_jump(rng):
    scrape = filt(noise(0.07, rng), bp(2300, 1.5)) * perc(ns(0.07), 0.001, 0.018) * 0.6
    kick = thump(150, 70, 0.1, tau=0.02, decay=0.03) * 0.6
    up = chirp(340, 860, 0.18, 'sine', time=0.075, decay=0.06)
    up += chirp(340, 860, 0.18, 'square', time=0.075, decay=0.04, lowpass=3000) * 0.15
    air = whoosh(0.12, rng, (1200, 3800), q=1.0, peak_at=0.3) * 0.3
    return place([(0.0, scrape), (0.0, kick), (0.02, up), (0.03, air)])


@sound('slide', -22, 'movement', fade_out=0.06)
def slide(rng):
    d = 0.42
    n = ns(d)
    x = noise(d, rng, 'pink')
    fc = ramp(n, 2400, 1300, 'exp') * (1.0 + 0.12 * lfo(n, 23.0))
    fr = svf(x, fc, 1.6, 'bp')
    grit = filt(white(n, rng), hp(3500)) * (0.5 + 0.5 * np.abs(lfo(n, 31.0))) * 0.12
    env = adsr(n, 0.025, 0.1, 0.75, 0.16, d - 0.16)
    return (fr + grit) * env


@sound('dash', -15, 'movement', fade_out=0.04)
def dash(rng):
    air = whoosh(0.36, rng, (350, 3400, 700), q=1.1, peak_at=0.28, shape=1.6)
    zap_ = zap(1100, 280, 0.26, cut0=6000, cut1=900, q=3.0, time=0.2, decay=0.08) * 0.55
    sub = thump(90, 45, 0.25, tau=0.04, decay=0.07) * 0.5
    glint = bell(1760, 0.18, ratio=2.0, index=2.0, decay=0.05) * 0.15
    return place([(0.0, air), (0.0, zap_), (0.0, sub), (0.0, glint)])


@sound('bounce', -17, 'world')
def bounce(rng):
    d = 0.45
    n = ns(d)
    t = np.arange(n) / SR
    f = glide(n, 140, 520, 0.07, 'exp') * (1.0 + 0.09 * np.exp(-t / 0.12) * np.sin(2 * np.pi * 17 * t))
    body = sine(f, n) * perc(n, 0.002, 0.14)
    body += 0.25 * triangle(f * 2, n) * perc(n, 0.002, 0.06)
    squish = filt(noise(0.09, rng, 'pink'), bp(500, 1.6)) * perc(ns(0.09), 0.002, 0.03)
    thud = thump(110, 55, 0.15, tau=0.02, decay=0.04) * 0.6
    return mix(body * 0.8, squish * 0.6, thud)
