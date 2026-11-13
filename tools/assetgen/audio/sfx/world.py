"""World / level objects: collectibles, checkpoint, doors, levers, crumbling platforms, portal."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('collect_shard', -18, 'pickup', fade_out=0.05)
def collect_shard(rng):
    a = chime('E6', 0.35, decay=0.12, bright=0.8)
    b = chime('B6', 0.45, decay=0.16, bright=0.8)
    glint = sparkle(0.3, rng, count=5, fmin=5000, fmax=9000, grain=0.04) * 0.25
    return place([(0.0, a * 0.5), (0.06, b * 0.55), (0.03, glint)])


@sound('collect_orb', -18, 'pickup', fade_out=0.05)
def collect_orb(rng):
    d = 0.3
    n = ns(d)
    bloop = sine(glide(n, 480, 960, 0.08, 'exp'), n) * perc(n, 0.002, 0.07)
    ding = bell('E6', 0.3, ratio=2.0, index=1.0, decay=0.09) * 0.4
    bub = filt(noise(0.05, rng), bp(1800, 3.0)) * perc(ns(0.05), 0.002, 0.012) * 0.3
    return place([(0.0, bloop * 0.7), (0.0, bub), (0.05, ding)])


@sound('collect_relic', -15, 'pickup', fade_out=0.15)
def collect_relic(rng):
    notes = ['D5', 'F#5', 'A5', 'D6', 'F#6']
    line = arp(notes, 0.07, lambda f, i: chime(f, 0.8, decay=0.25 + 0.05 * i, bright=0.8),
               vel=[0.6, 0.65, 0.7, 0.8, 0.9])
    d = 1.2
    n = ns(d)
    pad = np.zeros(n)
    for nt in ('D4', 'A4', 'D5', 'F#5'):
        f = note_hz(nt)
        pad += saw(f, n) + saw(f * 1.005, n)
    pad = filt(pad, lp(2200, 0.7)) * points(n, [(0, 0), (0.3, 0.12), (d, 0)])
    glints = sparkle(d, rng, count=20, fmin=4000, fmax=9500, density=[0.2, 1, 0.8, 0.3]) * 0.25
    x = place([(0.0, line * 0.45), (0.0, pad * 0.25), (0.1, glints)])
    return room(x, rng, t60=1.1, wet=0.25)


@sound('checkpoint', -15, 'world', fade_out=0.2)
def checkpoint(rng):
    hum_n = ns(1.4)
    t = np.arange(hum_n) / SR
    hum = svf(saw(110 * (1 + 0.003 * np.sin(2 * np.pi * 4 * t)), hum_n), ramp(hum_n, 300, 1800, 'exp'),
              2.5, 'lp') * points(hum_n, [(0, 0), (0.25, 0.4), (0.6, 0.25), (1.4, 0)])
    notes = ['D5', 'A5', 'D6', 'E6', 'F#6']
    line = arp(notes, 0.09, lambda f, i: chime(f, 0.9, decay=0.3, bright=0.6),
               vel=[0.5, 0.55, 0.65, 0.6, 0.75])
    beam = whoosh(0.5, rng, (300, 5000), q=1.4, peak_at=0.75) * 0.3
    x = place([(0.0, hum * 0.4), (0.0, beam), (0.3, line * 0.45)])
    return room(x, rng, t60=1.3, wet=0.3)


@sound('power_unlock', -13, 'world', fade_out=0.3)
def power_unlock(rng):
    d = 2.4
    rise_n = ns(0.8)
    riser = svf(noise(0.8, rng), ramp(rise_n, 300, 7000, 'exp'), 2.5, 'bp') * ramp(rise_n, 0, 1) ** 2
    m = ns(1.8)
    t = np.arange(m) / SR
    chord = np.zeros(m)
    for nt, a in (('D3', 0.6), ('A3', 0.5), ('D4', 0.45), ('F#4', 0.4), ('A4', 0.35), ('E5', 0.3)):
        f = note_hz(nt) * (1 + 0.003 * np.sin(2 * np.pi * 5.2 * t + a * 7))
        chord += a * (saw(f, m) + saw(f * 1.004, m) * 0.8)
    chord = filt(chord, lp(2600, 0.7)) * perc(m, 0.02, 0.7)
    bells = arp(['D6', 'F#6', 'A6', 'D7'], 0.06, lambda f, i: chime(f, 1.0, decay=0.35, bright=0.7))
    hit = thump(90, 40, 0.8, tau=0.05, decay=0.25, click=0.5, rng=rng)
    glints = sparkle(1.6, rng, count=36, fmin=3500, fmax=9500, density=[1, 0.8, 0.5, 0.2]) * 0.3
    x = place([(0.0, riser * 0.4), (0.75, hit * 0.7), (0.75, chord * 0.2), (0.78, bells * 0.35),
               (0.8, glints)], ns(d))
    return room(x, rng, t60=1.6, wet=0.3)


@sound('door_open', -17, 'world', fade_out=0.15)
def door_open(rng):
    d = 0.9
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 220), (0.15, 160), (d, 90)], 'exp')
    hum = svf(saw(f, n) + 0.6 * saw(f * 1.5, n), 1400, 1.5, 'lp')
    hum *= points(n, [(0, 0), (0.03, 1), (0.5, 0.6), (d, 0)])
    servo = svf(saw(points(n, [(0, 90), (d, 140)], 'exp'), n), 900, 3.0, 'bp')
    servo *= (0.6 + 0.4 * np.sin(2 * np.pi * 9 * t)) * points(n, [(0, 0), (0.1, 0.5), (d, 0)])
    grind = svf(noise(d, rng, 'brown'), 500 + 200 * np.sin(2 * np.pi * 3 * t), 1.0, 'lp')
    grind *= points(n, [(0, 0), (0.1, 0.6), (0.6, 0.4), (d, 0)])
    zap_ = zap(1500, 400, 0.2, cut0=6000, cut1=1000, q=3.0, decay=0.05) * 0.4
    clunk = thump(130, 60, 0.2, tau=0.02, decay=0.05) * 0.6
    return place([(0.0, zap_), (0.0, hum * 0.3), (0.05, servo * 0.25), (0.05, grind * 0.7),
                  (0.72, clunk)])


@sound('lever', -19, 'world')
def lever(rng):
    def click(f, d=0.04):
        n = ns(d)
        return filt(white(n, rng), bp(f, 2.0)) * perc(n, 0.0004, 0.006)
    clunk = thump(170, 80, 0.15, tau=0.015, decay=0.035, drive=0)
    ratchet = place([(0.018 * i, click(2600, 0.02) * 0.35) for i in range(1, 4)])
    return place([(0.0, click(1900) * 0.8), (0.0, ratchet), (0.085, click(1200) * 0.9),
                  (0.085, clunk * 0.8)])


@sound('crumble', -17, 'world', fade_out=0.12)
def crumble(rng):
    d = 0.9
    n = ns(d)
    crack = filt(noise(0.06, rng), bp(1500, 1.0)) * perc(ns(0.06), 0.0005, 0.015)
    rocks = debris(d, rng, count=45, fmin=250, fmax=2600, grain=(0.006, 0.04), start=0.02)
    rumble = filt(noise(d, rng, 'brown'), lp(250)) * points(n, [(0, 0), (0.05, 0.8), (d, 0)])
    dust = filt(noise(d, rng), bp(4000, 0.8)) * points(n, [(0, 0), (0.1, 0.15), (d, 0)])
    return mix(crack, rocks * 0.8, rumble * 0.6, dust)


@sound('portal', -15, 'world', fade_out=0.25)
def portal(rng):
    d = 1.6
    n = ns(d)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for i, nt in enumerate(('D3', 'A3', 'E4', 'F#4', 'A4')):
        f = note_hz(nt) * points(n, [(0, 0.94), (d, 1.0)], 'exp')
        ph = 2 * np.pi * (0.7 + 0.3 * i) * t
        out += fm(f * (1 + 0.006 * np.sin(ph)), n, 1.0, 0.8 + 0.6 * np.sin(ph + i)) * (0.55 - 0.07 * i)
    sweep = svf(out, 600 + 2400 * (0.5 + 0.5 * np.sin(2 * np.pi * 1.6 * t)), 2.0, 'lp')
    env = points(n, [(0, 0), (0.35, 1.0), (1.0, 0.7), (d, 0)])
    swirl = whoosh(d, rng, (400, 3000, 900, 4000, 600), q=2.5, peak_at=0.45) * 0.4
    glints = sparkle(d, rng, count=24, fmin=3000, fmax=9000, density=[0.2, 1, 1, 0.3]) * 0.25
    return room(mix(sweep * env * 0.35, swirl, glints), rng, t60=1.4, wet=0.3)
