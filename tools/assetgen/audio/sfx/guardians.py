"""2.19: attacks of the four sanctuary guardians (Mycelia, Aurorax, Pyrogyre, Nyxara)."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


def _creak(d, f0, f1, rng, rough=0.6):
    """Woody creak: a slow, gritty pitch glide through a resonant body."""
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, f0), (d, f1)], 'exp')
    pulses = (np.sin(2 * np.pi * np.cumsum(f) / SR) > 0.6).astype(float)
    grit = filt(pulses + rough * filt(white(n, rng), bp(900, 1.0)) * 0.2, bp(700, 1.4), lp(3500, 0.7))
    am = 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * 7.0 * t + rng.random() * 6.28))
    return grit * am


# ---------------------------------------------------------------------------------- Mycelia
@sound('root_burst', -12.5, 'boss', fade_out=0.15)
def root_burst(rng):
    """Racine traqueuse: the ground cracks, a root spear bursts out (woody whoosh + snap) and
    dirt rains back down."""
    rumble = filt(noise(0.35, rng, 'brown'), lp(160), hp(35)) * points(ns(0.35), [(0, 0), (0.25, 1), (0.35, 0.3)])
    crack = filt(noise(0.05, rng), bp(1600, 0.8)) * perc(ns(0.05), 0.0005, 0.012)
    burst = thump(110, 42, 0.45, tau=0.03, decay=0.12, click=0.6, rng=rng, drive=1.8)
    up = whoosh(0.35, rng, f_path=(300, 1800, 2600), q=1.4, peak_at=0.35)
    snap = _creak(0.18, 260, 520, rng) * perc(ns(0.18), 0.002, 0.06)
    dirt = debris(0.8, rng, count=45, fmin=400, fmax=3000, grain=(0.004, 0.02), start=0.1)
    x = place([(0.0, rumble * 0.6), (0.3, crack), (0.3, burst * 0.9), (0.31, up * 0.6), (0.33, snap * 0.8),
               (0.36, dirt * 0.45)])
    return room(x, rng, t60=0.7, wet=0.18)


@sound('thorn_wall', -13.5, 'boss', fade_out=0.15)
def thorn_wall(rng):
    """Cage de ronces: two walls of brambles shoot up (left then right): fast creaking growth,
    rustling leaves, thorny crackle and two earthy thuds."""
    parts = []
    for k, t0 in enumerate((0.0, 0.14)):
        grow = _creak(0.5, 140 + 30 * k, 420 + 60 * k, rng) * points(ns(0.5), [(0, 0), (0.08, 1), (0.4, 0.7), (0.5, 0)])
        rustle = svf(noise(0.5, rng, 'pink'), points(ns(0.5), [(0, 1200), (0.5, 4500)], 'exp'), 1.2, 'bp')
        rustle *= points(ns(0.5), [(0, 0), (0.1, 1), (0.5, 0)])
        thud = thump(95, 45, 0.3, tau=0.025, decay=0.08, click=0.4, rng=rng)
        prick = filt(crackle(0.4, rng, rate=110), hp(2500, 0.7)) * 0.5
        parts += [(t0, thud * 0.8), (t0 + 0.01, grow * 0.7), (t0 + 0.02, rustle * 0.35), (t0 + 0.05, prick * 0.4)]
    return room(place(parts), rng, t60=0.6, wet=0.2)


@sound('inhale', -15.5, 'boss', fade_out=0.2)
def inhale(rng):
    """Inspiration: a huge 2 s intake of air: rising breathy roar, a deep throat drone that climbs,
    and a hollow gulp at the end."""
    d = 2.1
    n = ns(d)
    t = np.arange(n) / SR
    env = points(n, [(0, 0), (0.25, 0.5), (1.7, 1.0), (1.9, 0.6), (d, 0)])
    air = svf(noise(d, rng, 'pink'), points(n, [(0, 300), (1.8, 2400), (d, 900)], 'exp'), 1.6, 'bp')
    air += 0.5 * svf(noise(d, rng), points(n, [(0, 1500), (1.8, 6000), (d, 2000)], 'exp'), 2.5, 'bp')
    f = points(n, [(0, 55), (1.8, 95), (d, 70)], 'exp') * (1.0 + 0.02 * np.sin(2 * np.pi * 5.5 * t))
    throat = svf(saw(f, n) + 0.6 * saw(f * 1.5, n), points(n, [(0, 300), (1.8, 900)], 'exp'), 2.0, 'lp')
    flutter = 0.75 + 0.25 * np.sin(2 * np.pi * 9.0 * t)
    gulp = chirp(240, 90, 0.18, 'sine', decay=0.05) * 0.8
    x = mix(air * env * flutter * 0.8, throat * env * 0.3)
    x = place([(0.0, x), (1.85, gulp)])
    return room(x, rng, t60=1.0, wet=0.25)


# ---------------------------------------------------------------------------------- Aurorax
@sound('mirror_split', -14, 'boss', fade_out=0.25)
def mirror_split(rng):
    """Reflets: a glassy chord rings, then splits into two slightly detuned copies that fly apart
    (two doubles appear), over a soft aurora shimmer."""
    d = 1.2
    n = ns(d)
    chord = np.zeros(n)
    for f in (880.0, 1318.5, 1760.0):
        chord += chime(f, d, decay=0.5, bright=0.8)
    left = np.zeros(n)
    for f in (880.0 * 0.985, 1318.5 * 1.012):
        left += bell(points(n, [(0, f), (d, f * 0.94)], 'exp'), d, ratio=2.0, index=1.2, decay=0.35, idx_decay=0.05)
    right = np.zeros(n)
    for f in (880.0 * 1.015, 1760.0 * 0.99):
        right += bell(points(n, [(0, f), (d, f * 1.06)], 'exp'), d, ratio=2.0, index=1.2, decay=0.35, idx_decay=0.05)
    swish = whoosh(0.5, rng, f_path=(3000, 7000, 4000), q=2.0, peak_at=0.3)
    glints = sparkle(1.0, rng, count=16, fmin=3000, fmax=9000, density=[0.2, 1, 0.8, 0.3]) * 0.4
    x = place([(0.0, chord * 0.35), (0.16, left * 0.35), (0.2, right * 0.35), (0.14, swish * 0.3), (0.1, glints)])
    return room(x, rng, t60=1.4, wet=0.3, hf=0.6)


@sound('ice_floor', -14.5, 'boss', fade_out=0.25)
def ice_floor(rng):
    """Lac gele: frost races across the floor: an accelerating crystallisation crackle that spreads,
    an icy hiss, a low creak of the ice sheet and a final glassy settle."""
    d = 1.3
    n = ns(d)
    t = np.arange(n) / SR
    clicks = np.zeros(n)
    g = ns(0.003)
    for _ in range(170):
        p = int(rng.random() ** 0.7 * 0.9 * SR)
        f = np.exp(rng.uniform(np.log(2500), np.log(9000)))
        tt = np.arange(g) / SR
        e = min(n, p + g)
        clicks[p:e] += (np.sin(2 * np.pi * f * tt) * np.exp(-tt / 0.0008))[:e - p] * rng.uniform(0.3, 1.0)
    hiss = svf(noise(d, rng), points(n, [(0, 4000), (0.9, 8000), (d, 6000)], 'exp'), 1.4, 'bp')
    hiss *= points(n, [(0, 0), (0.1, 0.6), (0.9, 1.0), (d, 0)])
    creak = _creak(0.9, 60, 45, rng, rough=0.2) * points(ns(0.9), [(0, 0), (0.3, 1), (0.9, 0)])
    settle = chime(2093.0, 0.6, decay=0.25, bright=0.6) * 0.5
    x = place([(0.0, clicks * 0.5), (0.0, hiss * 0.25), (0.15, creak * 0.5), (0.85, settle)])
    return room(x, rng, t60=1.2, wet=0.25, hf=0.6)


@sound('prism', -13, 'boss', fade_out=0.15)
def prism(rng):
    """Prisme: a crystal ray charges for a beat and fires: a bright resonant zap split into a
    shimmering cluster of detuned partials (the spectrum), with ricochet pings."""
    d = 0.9
    n = ns(d)
    charge = chirp(900, 2600, 0.18, 'tri', decay=0.12) * 0.4
    fire = zap(3200, 900, 0.5, cut0=9000, cut1=2000, q=4.0)
    cluster = np.zeros(n)
    for k, r in enumerate((1.0, 1.26, 1.5, 1.68, 2.0)):
        f = points(n, [(0, 1400 * r), (d, 1250 * r)], 'exp')
        cluster += sine(f, n) * perc(n, 0.004, 0.18 + 0.03 * k) * (0.5 if k % 2 else 0.7)
    pings = np.zeros(n)
    for k, (p, f) in enumerate(((0.35, 3520.0), (0.55, 4186.0))):
        g = bell(f, 0.3, ratio=2.76, index=1.4, decay=0.08, idx_decay=0.02)
        s = ns(p)
        e = min(n, s + len(g))
        pings[s:e] += g[:e - s] * (0.6 - 0.2 * k)
    x = place([(0.0, charge), (0.16, fire * 0.6), (0.16, cluster * 0.35), (0.16, pings * 0.5)])
    return room(x, rng, t60=0.9, wet=0.25, hf=0.6)


# ---------------------------------------------------------------------------------- Pyrogyre
def _flame(d, rng, f_path=(500, 1600, 600), flick=25.0):
    n = ns(d)
    x = svf(noise(d, rng, 'pink'), points(n, [(0, f_path[0]), (d * 0.3, f_path[1]), (d, f_path[2])], 'exp'), 0.9, 'lp')
    fl = filt(white(n, rng), lp(flick, 0.7))
    fl /= np.max(np.abs(fl)) + 1e-9
    return x * (1.0 + 0.4 * fl)


@sound('orbit_flames', -14, 'boss', fade_out=0.2)
def orbit_flames(rng):
    """Couronne de feu: six flames ignite one after another and swirl around: puffs of ignition,
    then a roaring ring whose loudness pulses with the rotation."""
    d = 1.4
    n = ns(d)
    t = np.arange(n) / SR
    puffs = []
    for k in range(6):
        p = _flame(0.25, rng, (800, 2600, 900)) * perc(ns(0.25), 0.004, 0.07)
        puffs.append((0.05 * k, p * 0.6))
    roar = _flame(d, rng, (400, 1300, 500), flick=18.0)
    rot = 0.65 + 0.35 * np.sin(2 * np.pi * 3.0 * t) ** 2
    roar *= rot * points(n, [(0, 0), (0.3, 1.0), (1.0, 0.8), (d, 0)])
    body = sine(points(n, [(0, 90), (d, 70)], 'exp'), n) * points(n, [(0, 0), (0.2, 1), (d, 0)]) * 0.3
    crack = filt(crackle(d, rng, rate=60, decay=False), lp(6000, 0.7)) * 0.4
    return room(place(puffs + [(0.1, roar * 0.8), (0.1, body), (0.1, crack * 0.6)]), rng, t60=0.8, wet=0.2)


@sound('spiral', -14, 'boss', fade_out=0.15)
def spiral(rng):
    """Spirale solaire: a rapid stream of fire bolts spat out in two arms, the pitch of the shots
    climbing as the spiral turns."""
    parts = []
    k = 0
    tpos = 0.0
    while tpos < 1.25:
        f0 = 900 + 500 * (tpos / 1.25) + (120 if k % 2 else 0)
        shot = chirp(f0, f0 * 0.45, 0.09, 'saw', decay=0.03, lowpass=4000) * 0.5
        fw = _flame(0.12, rng, (1200, 3000, 1200)) * perc(ns(0.12), 0.002, 0.04) * 0.5
        parts += [(tpos, shot), (tpos, fw)]
        tpos += 0.065
        k += 1
    bed = _flame(1.4, rng, (300, 900, 400)) * points(ns(1.4), [(0, 0), (0.2, 1), (1.2, 0.8), (1.4, 0)]) * 0.35
    return room(place(parts + [(0.0, bed)]), rng, t60=0.6, wet=0.18)


@sound('lava_rise', -13, 'boss', fade_out=0.3)
def lava_rise(rng):
    """Maree de lave: a deep rumble swells as the lava climbs, with heavy bubbling blubs, a hiss
    of heat and a final surge."""
    d = 2.4
    n = ns(d)
    t = np.arange(n) / SR
    rumble = filt(noise(d, rng, 'brown'), lp(140), hp(30))
    swell = points(n, [(0, 0.1), (1.6, 1.0), (2.0, 0.8), (d, 0)])
    drone = svf(saw(points(n, [(0, 38), (1.8, 55)], 'exp'), n), 260, 1.0, 'lp') * swell
    blubs = np.zeros(n)
    for k in range(18):
        p = int((0.1 + rng.random() * 2.0) * SR)
        f0 = rng.uniform(90, 220)
        b = chirp(f0, f0 * 2.2, 0.08, 'sine', decay=0.03)
        e = min(n, p + len(b))
        blubs[p:e] += b[:e - p] * rng.uniform(0.4, 1.0)
    hiss = svf(noise(d, rng), points(n, [(0, 2500), (d, 6000)], 'exp'), 1.0, 'bp') * swell
    x = mix(rumble * swell * 1.2, drone * 0.4, blubs * 0.5, hiss * 0.12)
    return room(soft_clip(x, 1.4), rng, t60=1.0, wet=0.2)


# ---------------------------------------------------------------------------------- Nyxara
@sound('gravity_well', -13.5, 'boss', fade_out=0.35)
def gravity_well(rng):
    """Puits gravitationnel: a black hole opens: a sub drop that bends down, warbling as it
    spins, a swirling whoosh dragged inwards and a dark shimmering tail."""
    d = 2.2
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 160), (0.4, 70), (d, 40)], 'exp') * (1.0 + 0.04 * np.sin(2 * np.pi * 6.0 * t))
    sub = sine(f, n) + 0.3 * sine(f * 2.01, n)
    sub *= points(n, [(0, 0), (0.05, 1), (1.6, 0.7), (d, 0)])
    swirl = svf(noise(d, rng, 'pink'), points(n, [(0, 3000), (d, 400)], 'exp')
                * (1.0 + 0.3 * np.sin(2 * np.pi * 2.5 * t)), 3.0, 'bp')
    swirl *= points(n, [(0, 0), (0.3, 1), (1.8, 0.6), (d, 0)])
    dark = np.zeros(n)
    for r in (1.0, 1.19, 1.5):
        dark += sine(110 * r * (1 - 0.1 * t / d), n)
    dark = svf(dark, 900, 1.0, 'lp') * points(n, [(0, 0), (0.6, 0.5), (d, 0)])
    x = mix(soft_clip(sub * 0.9, 1.6), swirl * 0.45, dark * 0.12)
    return room(x, rng, t60=1.8, wet=0.3)


@sound('shadow_step', -14, 'boss', fade_out=0.12)
def shadow_step(rng):
    """Pas de l'ombre: a reversed dark whoosh sucks in, a metallic 'shing' and a low thump as
    Nyxara reappears and strikes."""
    d = 0.35
    rev = whoosh(d, rng, f_path=(400, 1800, 5000), q=1.5, peak_at=0.9, shape=3.0)
    shing = bell(points(ns(0.4), [(0, 2600), (0.4, 2300)], 'exp'), 0.4, ratio=1.41, index=2.0, decay=0.12)
    thud = thump(130, 50, 0.3, tau=0.02, decay=0.07, click=0.5, rng=rng)
    tail = svf(noise(0.3, rng, 'pink'), 700, 1.0, 'lp') * perc(ns(0.3), 0.002, 0.08)
    x = place([(0.0, rev * 0.8), (0.33, shing * 0.45), (0.33, thud * 0.8), (0.34, tail * 0.4)])
    return room(x, rng, t60=0.8, wet=0.22)


@sound('eclipse_ring', -12.5, 'boss', fade_out=0.3)
def eclipse_ring(rng):
    """Onde d'eclipse: a dark shockwave leaves the boss: a low boom, a phased ring sweeping up as
    it expands and a fading shimmer."""
    d = 1.5
    n = ns(d)
    t = np.arange(n) / SR
    boom = thump(100, 48, 0.9, tau=0.05, decay=0.3, click=0.3, rng=rng, drive=1.6)
    ring_f = points(n, [(0, 220), (d, 660)], 'exp')
    ring = sine(ring_f, n) + 0.5 * sine(ring_f * 1.5, n) + 0.3 * sine(ring_f * 2.02, n)
    ring *= points(n, [(0, 0), (0.08, 1), (1.0, 0.6), (d, 0)]) * (0.7 + 0.3 * np.sin(2 * np.pi * 11 * t))
    wave = svf(noise(d, rng, 'pink'), points(n, [(0, 300), (d, 4000)], 'exp'), 2.5, 'bp')
    wave *= points(n, [(0, 0), (0.05, 1), (d, 0)])
    glints = sparkle(1.2, rng, count=12, fmin=2500, fmax=7000, density=[0.2, 0.8, 1, 0.5]) * 0.25
    x = place([(0.0, boom * 0.9), (0.02, ring * 0.25), (0.02, wave * 0.4), (0.2, glints)])
    return room(x, rng, t60=1.5, wet=0.28)
