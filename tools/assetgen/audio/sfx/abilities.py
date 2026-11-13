"""v2 player abilities: glide, ground pound, charged shot, grapple, triple jump, heart container.
Cyan-energy palette like the v1 powers (FM shimmer, resonant zaps), plus physical layers."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403
from sfx.loopkit import periodic, whole_hz, circ_noise, wrap_add, shepard, wrapped_svf


# --------------------------------------------------------------------------- glide

@sound('glide_open', -17, 'powers', fade_out=0.06)
def glide_open(rng):
    """Two-stage membrane snap as the glider wings unfold, then they catch the air and light up."""
    snap1 = filt(noise(0.05, rng), bp(1400, 1.2)) * perc(ns(0.05), 0.0008, 0.01)
    snap2 = filt(noise(0.06, rng), bp(2300, 1.4)) * perc(ns(0.06), 0.0008, 0.012)
    catch = whoosh(0.45, rng, (300, 1500, 600), q=1.0, peak_at=0.3, shape=1.6)
    fwump = thump(120, 60, 0.25, tau=0.03, decay=0.06, drive=0) * 0.5
    d = 0.5
    n = ns(d)
    f = glide(n, 660, 990, 0.12, 'exp')
    hum = (fm(f, n, 2.0, 0.8) * 0.5 + sine(f * 1.5, n) * 0.3) * points(n, [(0, 0), (0.06, 1), (d, 0)])
    glints = sparkle(0.45, rng, count=8, fmin=3500, fmax=8000, grain=0.05) * 0.2
    return place([(0.0, snap1 * 0.8), (0.045, snap2 * 0.7), (0.03, catch * 0.9), (0.04, fwump),
                  (0.05, hum * 0.3), (0.08, glints)])


@sound('glide_loop', -23, 'powers', loop=True)
def glide_loop(rng):
    """Steady rushing air under the wings with a slight membrane flutter and a faint energy hum.
    2.4 s seamless loop."""
    n = ns(2.4)
    x = circ_noise(n, rng, 'pink')
    band = 950.0 * np.exp(0.25 * periodic(n, 1) + 0.1 * periodic(n, 3, 0.2))
    wind = wrapped_svf(x, band, 0.9, 'bp')
    rush = circ_noise(n, rng, 'white', hp(2600, 0.7), lp(9000, 0.7))
    rush *= 0.75 + 0.25 * periodic(n, 2, 0.4)
    flutter = 1.0 + 0.16 * periodic(n, 34) + 0.06 * periodic(n, 53, 0.3)
    t = np.arange(n) / SR
    hum = (np.sin(2 * np.pi * whole_hz(220.0, n) * t) + 0.6 * np.sin(2 * np.pi * whole_hz(330.0, n) * t)
           + 0.3 * np.sin(2 * np.pi * whole_hz(441.0, n) * t)) * (0.7 + 0.3 * periodic(n, 3))
    return wind * flutter * 1.0 + rush * 0.18 + hum * 0.012


# --------------------------------------------------------------------------- ground pound

@sound('pound_dive', -16, 'powers', fade_out=0.05)
def pound_dive(rng):
    """Tuck flip, then a plunging energy dive that intensifies toward the ground."""
    d = 0.42
    n = ns(d)
    flip = whoosh(0.12, rng, (1500, 4000), q=1.4, peak_at=0.4, color='white') * 0.6
    dive = whoosh(d, rng, (3500, 800), q=1.3, peak_at=0.8, shape=1.4)
    z = zap(1300, 160, d, cut0=6000, cut1=500, q=5.0, time=d, attack=0.01, release=0.06)
    z *= points(n, [(0, 0.3), (d * 0.8, 1.0), (d, 0.0)])
    sub = sine(glide(n, 170, 60, d), n) * points(n, [(0, 0), (d * 0.8, 0.5), (d, 0)])
    return place([(0.0, flip), (0.03, dive), (0.03, z * 0.4), (0.03, sub * 0.4)])


@sound('pound_impact', -11, 'powers', fade_out=0.15)
def pound_impact(rng):
    """Heavy ground slam: deep body, crunch, cyan shock ring and flying debris."""
    boom = thump(125, 38, 1.0, tau=0.05, decay=0.24, click=0.8, rng=rng, drive=2.2)
    d = 0.7
    n = ns(d)
    crunch = svf(noise(d, rng, 'pink'), ramp(n, 3500, 180, 'exp'), 0.8, 'lp') * perc(n, 0.0008, 0.09)
    shock = whoosh(0.6, rng, (1800, 400), q=2.2, peak_at=0.08, shape=1.2) * 0.45
    rocks = debris(0.9, rng, count=30, fmin=400, fmax=3200, grain=(0.005, 0.03), start=0.03)
    energy = zap(700, 90, 0.35, cut0=5000, cut1=300, q=4.0, decay=0.1) * 0.3
    return room(mix(boom, crunch * 0.9, shock, rocks * 0.45, energy), rng, t60=0.8, wet=0.15)


# --------------------------------------------------------------------------- charged shot

@sound('charge_loop', -20, 'powers', loop=True)
def charge_loop(rng):
    """Energy gathering: an endlessly rising Shepard whine (root + fifth), a pulsing low hum and
    electric crackles. 1.5 s seamless loop (it keeps 'rising' however long it is held)."""
    n = ns(1.5)
    t = np.arange(n) / SR
    rise = shepard(n, 140.0, 6, partials=((1.0, 1.0), (2.0, 0.25)), rng=rng)
    fifth = shepard(n, 210.0, 6, partials=((1.0, 1.0),), rng=rng)
    hum_f = whole_hz(73.4, n)
    hum = filt(saw(np.full(n, hum_f), n), lp(500, 0.9), circular=True)
    hum *= 0.6 + 0.4 * periodic(n, 9) ** 2
    zz = np.zeros(n)
    for _ in range(26):
        m = ns(0.012)
        g = filt(white(m + 64, rng), bp(rng.uniform(2500, 7000), 2.0))[:m] * np.hanning(m)
        wrap_add(zz, g * rng.uniform(0.3, 1.0), int(rng.integers(0, n)))
    sh = np.zeros(n)
    for _ in range(9):
        f = rng.uniform(3000, 8000)
        m = ns(0.06)
        tt = np.arange(m) / SR
        g = fm(f, m, 2.0, 1.5 * np.exp(-tt / 0.01) + 0.1) * perc(m, 0.002, 0.015)
        wrap_add(sh, g * rng.uniform(0.3, 0.8), int(rng.integers(0, n)))
    pulse = 0.8 + 0.2 * periodic(n, 6)
    return (rise * 0.5 + fifth * 0.25) * pulse + hum * 0.22 + zz * 0.5 + sh * 0.12


@sound('charge_ready', -16, 'powers', fade_out=0.08)
def charge_ready(rng):
    """Fully charged: a quick upward zap and a bright two-note glass ding."""
    up = zap(400, 1800, 0.12, cut0=1500, cut1=8000, q=4.0, decay=0.05) * 0.4
    a = chime('E6', 0.5, decay=0.14, bright=0.9)
    b = chime('B6', 0.6, decay=0.2, bright=0.9)
    glint = sparkle(0.4, rng, count=10, fmin=4000, fmax=9500, grain=0.04) * 0.3
    hum = sine(note_hz('E4'), ns(0.5)) * perc(ns(0.5), 0.005, 0.15) * 0.3
    return place([(0.0, up), (0.05, a * 0.5), (0.11, b * 0.55), (0.06, glint), (0.05, hum)])


@sound('charge_shot', -13, 'powers', fade_out=0.1)
def charge_shot(rng):
    """Big plasma blast: punchy body, double resonant down-zap, FM plasma and sizzle."""
    boom = thump(150, 50, 0.6, tau=0.035, decay=0.12, click=0.6, rng=rng, drive=2.0)
    d = 0.6
    n = ns(d)
    t = np.arange(n) / SR
    z1 = zap(1600, 140, d, cut0=9000, cut1=400, q=6.0, time=0.4, decay=0.14)
    z2 = zap(1210, 105, d, cut0=7000, cut1=300, q=4.0, time=0.4, decay=0.16, shape='square') * 0.5
    m = ns(0.4)
    blast = svf(noise(0.4, rng, 'pink'), ramp(m, 8000, 600, 'exp'), 0.8, 'lp') * perc(m, 0.0005, 0.07)
    plasma = fm(glide(n, 880, 220, 0.3), n, 1.5, 2.5 * np.exp(-t / 0.08) + 0.3) * perc(n, 0.002, 0.12)
    sizzle = filt(noise(d, rng), hp(5000)) * perc(n, 0.003, 0.15) * (0.6 + 0.4 * np.abs(lfo(n, 53)))
    return mix(boom * 0.8, z1 * 0.5, z2, blast * 0.7, plasma * 0.4, sizzle * 0.2)


# --------------------------------------------------------------------------- grapple

@sound('grapple_fire', -17, 'powers', fade_out=0.04)
def grapple_fire(rng):
    """Launcher latch click, then the energy line zipping out (rising rattling whir)."""
    m = ns(0.05)
    click = mix(filt(white(m, rng), bp(3200, 2.0)) * perc(m, 0.0003, 0.004),
                thump(320, 150, 0.06, tau=0.01, decay=0.015, drive=0) * 0.4)
    d = 0.3
    n = ns(d)
    f = glide(n, 500, 2600, 0.22, 'exp')
    rattle = 0.7 + 0.3 * np.sign(np.sin(2 * np.pi * np.cumsum(f / 7.0) / SR))
    whir = svf(saw(f, n), f * 3, 3.0, 'lp') * rattle * points(n, [(0, 0), (0.02, 1), (0.22, 0.8), (d, 0)])
    zipn = whoosh(0.28, rng, (1200, 6000), q=2.0, peak_at=0.6, color='white') * 0.4
    return place([(0.0, click * 0.8), (0.01, whir * 0.3), (0.01, zipn)])


@sound('grapple_hit', -16, 'powers', fade_out=0.06)
def grapple_hit(rng):
    """Hook bites the anchor: metallic clank, the line snapping taut, an energy lock-on blip."""
    d = 0.4
    n = ns(d)
    t = np.arange(n) / SR
    clank = np.zeros(n)
    for r, a, tau in ((1.0, 1.0, 0.12), (2.76, 0.6, 0.06), (5.4, 0.35, 0.03), (8.93, 0.2, 0.015)):
        clank += a * sine(1250.0 * r, n) * perc(n, 0.0004, tau)
    knock = thump(240, 110, 0.12, tau=0.012, decay=0.03, click=0.5, rng=rng)
    tf = 190.0 * (1.0 + 0.08 * np.exp(-t / 0.05) * np.sin(2 * np.pi * 31 * t))
    twang = (sine(tf, n) + 0.4 * triangle(tf * 2, n)) * perc(n, 0.002, 0.08)
    lock = chirp(500, 1000, 0.18, 'tri', time=0.05, decay=0.06) * 0.3
    lock = mix(lock, bell(1500, 0.3, ratio=2.0, index=1.2, decay=0.08) * 0.25)
    return place([(0.0, clank * 0.3), (0.0, knock * 0.6), (0.01, twang * 0.35), (0.04, lock)])


# --------------------------------------------------------------------------- triple jump

@sound('triple_jump', -16, 'movement', fade_out=0.05)
def triple_jump(rng):
    """Third jump: a fast three-step rising chirp, a spin swirl and a burst of glints (higher and
    brighter than double_jump)."""
    a = chirp(520, 1100, 0.1, 'sine', time=0.05, decay=0.04)
    b = chirp(700, 1500, 0.12, 'sine', time=0.05, decay=0.045)
    c = chirp(900, 2000, 0.25, 'sine', time=0.07, decay=0.08)
    c += chirp(900, 2000, 0.25, 'tri', time=0.07, decay=0.06) * 0.3
    swirl = whoosh(0.35, rng, (900, 5500, 2500), q=1.5, peak_at=0.35) * 0.4
    glint = sparkle(0.3, rng, count=12, fmin=4000, fmax=9000, grain=0.05, density=[1, 0.8, 0.4, 0.1]) * 0.25
    return place([(0.0, a * 0.8), (0.04, b * 0.85), (0.08, c), (0.02, swirl), (0.08, glint)])


# --------------------------------------------------------------------------- heart container

@sound('heart_pickup', -14, 'pickup', fade_out=0.2)
def heart_pickup(rng):
    """Crystal heart (+1 max health): a warm 'lub-dub' heartbeat, a rising A major crystal
    arpeggio over a soft pad and a shower of glints."""
    def beat(v):
        return thump(95, 50, 0.2, tau=0.02, decay=0.05, drive=0) * v
    lubdub = place([(0.0, beat(0.9)), (0.16, beat(0.65))])
    line = arp(['A5', 'C#6', 'E6', 'A6'], 0.075,
               lambda f, i: chime(f, 0.9, decay=0.3 + 0.05 * i, bright=0.8), vel=[0.6, 0.65, 0.75, 0.9])
    d = 1.1
    n = ns(d)
    pad = np.zeros(n)
    for nt in ('A4', 'C#5', 'E5'):
        f = note_hz(nt)
        pad += saw(f, n) + saw(f * 1.005, n)
    pad = filt(pad, lp(2500, 0.7)) * points(n, [(0, 0), (0.25, 0.12), (d, 0)])
    glints = sparkle(1.1, rng, count=24, fmin=4000, fmax=9500, density=[0.3, 1, 0.7, 0.2]) * 0.25
    x = place([(0.0, lubdub * 0.7), (0.12, line * 0.5), (0.1, pad * 0.2), (0.15, glints)])
    return room(x, rng, t60=1.1, wet=0.25)
