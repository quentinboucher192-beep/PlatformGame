"""Player powers and melee: slash, energy wave, shield, impulse, teleport."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('slash', -17, 'combat')
def slash(rng):
    d = 0.2
    sw = whoosh(d, rng, (1400, 6500, 3000), q=1.3, peak_at=0.3, color='white', shape=1.5)
    sw = filt(sw, lp(9000, 0.7))
    blade = bell(1850, 0.16, ratio=1.5, index=1.8, decay=0.045, idx_decay=0.02) * 0.22
    blade += sine(glide(ns(0.16), 2600, 1700, 0.12), ns(0.16)) * perc(ns(0.16), 0.003, 0.04) * 0.12
    return place([(0.0, sw), (0.025, blade)])


@sound('slash_hit', -15, 'combat')
def slash_hit(rng):
    crack = filt(noise(0.05, rng), bp(2400, 0.9)) * perc(ns(0.05), 0.0005, 0.012)
    body = thump(170, 60, 0.18, tau=0.025, decay=0.05, click=0.4, rng=rng, drive=1.8) * 0.8
    ring = chime(1320, 0.25, decay=0.08, bright=0.7) * 0.18
    fizz = crackle(0.12, rng, rate=120) * 0.3
    return mix(soft_clip(mix(crack * 1.2, body), 1.3), ring, fizz)


@sound('wave_cast', -15, 'powers', fade_out=0.05)
def wave_cast(rng):
    d = 0.5
    n = ns(d)
    z = zap(190, 520, d, cut0=350, cut1=4800, q=7.0, time=0.32, attack=0.004, decay=0.16)
    t = np.arange(n) / SR
    f = glide(n, 420, 1260, 0.28, 'exp') * (1.0 + 0.012 * np.sin(2 * np.pi * 21 * t))
    shimmer = fm(f, n, 2.0, 1.2) * perc(n, 0.01, 0.16) * 0.35
    air = whoosh(0.3, rng, (800, 5000), q=2.0, peak_at=0.6) * 0.3
    dry = mix(z * 0.8, shimmer, air)
    return echo(dry, 0.085, 0.3, 4000)[:ns(0.75)]


@sound('wave_hit', -16, 'powers')
def wave_hit(rng):
    z = zap(1500, 300, 0.3, cut0=5000, cut1=600, q=6.0, time=0.18, decay=0.08) * 0.7
    burst = filt(noise(0.08, rng), bp(3000, 0.8)) * perc(ns(0.08), 0.0005, 0.02) * 0.6
    glints = sparkle(0.3, rng, count=8, fmin=3000, fmax=8000, grain=0.05) * 0.25
    low = thump(140, 70, 0.14, tau=0.02, decay=0.04) * 0.45
    return mix(z, burst, glints, low)


def _shield_chord(rng, d, f_mul, amp_env):
    n = ns(d)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for f0, a in ((note_hz('A4'), 0.5), (note_hz('E5'), 0.4), (note_hz('A5'), 0.3),
                  (note_hz('C#6'), 0.18)):
        f = f0 * f_mul * (1.0 + 0.004 * np.sin(2 * np.pi * (5.5 + rng.random()) * t))
        out += a * fm(f, n, 2.0, 0.8 + 0.6 * amp_env)
    return out * amp_env


@sound('shield_up', -17, 'powers', fade_out=0.08)
def shield_up(rng):
    d = 0.6
    n = ns(d)
    env = points(n, [(0, 0), (0.12, 1.0), (0.3, 0.6), (d, 0.0)])
    f_mul = ramp(n, 0.82, 1.0, 'sq')
    chord = _shield_chord(rng, d, f_mul, env)
    rise = whoosh(0.35, rng, (600, 6000), q=1.6, peak_at=0.5) * 0.35
    hum = sine(110 * f_mul, n) * env * 0.25
    return mix(chord, rise, hum)


@sound('shield_block', -16, 'powers')
def shield_block(rng):
    ting = chime(980, 0.5, decay=0.16, bright=1.0) * 0.55
    hit = filt(noise(0.04, rng), bp(3500, 1.0)) * perc(ns(0.04), 0.0005, 0.008) * 0.6
    low = thump(160, 80, 0.12, tau=0.015, decay=0.035) * 0.55
    buzz = zap(440, 330, 0.2, cut0=3000, cut1=1200, q=2.0, decay=0.05, shape='square') * 0.12
    return mix(ting, hit, low, buzz)


@sound('shield_down', -18, 'powers', fade_out=0.06)
def shield_down(rng):
    d = 0.55
    n = ns(d)
    env = points(n, [(0, 0), (0.01, 1.0), (d, 0.0)])
    f_mul = ramp(n, 1.0, 0.55, 'exp')
    chord = _shield_chord(rng, d, f_mul, env ** 1.5)
    fizz = filt(noise(d, rng), hp(4000)) * env ** 2 * (0.5 + 0.5 * np.abs(lfo(n, 37))) * 0.12
    return mix(chord, fizz)


@sound('impulse', -14, 'powers', fade_out=0.08)
def impulse(rng):
    boom = thump(100, 44, 0.7, tau=0.05, decay=0.2, click=0.5, rng=rng, drive=1.5)
    d = 0.5
    n = ns(d)
    blast = svf(noise(d, rng, 'pink'), ramp(n, 5000, 250, 'exp'), 0.9, 'lp') * perc(n, 0.001, 0.12)
    ring = whoosh(0.45, rng, (2500, 900), q=2.5, peak_at=0.15) * 0.35
    zap_ = zap(900, 160, 0.25, cut0=6000, cut1=500, q=4.0, decay=0.06) * 0.35
    return mix(boom * 0.9, blast * 0.8, ring, zap_)


@sound('teleport_out', -16, 'powers', fade_out=0.05)
def teleport_out(rng):
    d = 0.42
    n = ns(d)
    f = glide(n, 320, 2600, 0.36, 'exp')
    env = points(n, [(0, 0), (0.02, 0.8), (0.3, 1.0), (d, 0)])
    body = fm(f, n, 1.5, 1.4) * env * 0.6
    body += sine(f * 0.5, n) * env * 0.3
    spk = sparkle(d, rng, count=18, fmin=3000, fmax=9500, grain=0.04, density=[0.2, 0.6, 1.0, 1.0])
    air = whoosh(d, rng, (500, 7000), q=1.2, peak_at=0.7, color='white') * 0.25
    return mix(body, spk * 0.3, air)


@sound('teleport_in', -16, 'powers', fade_out=0.05)
def teleport_in(rng):
    d = 0.42
    n = ns(d)
    f = glide(n, 2600, 380, 0.3, 'exp')
    env = points(n, [(0, 0), (0.03, 1.0), (0.25, 0.7), (d, 0)])
    body = fm(f, n, 1.5, 1.2) * env * 0.6
    spk = sparkle(d, rng, count=14, fmin=3000, fmax=9500, grain=0.04, density=[1.0, 0.7, 0.3, 0.1])
    settle = thump(140, 70, 0.15, tau=0.02, decay=0.04) * 0.5
    return place([(0.0, body), (0.0, spk * 0.3), (0.26, settle)])
