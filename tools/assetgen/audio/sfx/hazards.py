"""v2 projectiles, hazards and world objects: fire, ice, bombs, lava, updraft vents."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403
from sfx.loopkit import periodic, circ_noise, barber_noise, wrapped_svf


def _flicker(n, rng, rate=25.0, depth=0.35):
    """Random flame-like amplitude flicker around 1."""
    f = filt(white(n, rng), lp(rate, 0.7))
    f /= np.max(np.abs(f)) + 1e-9
    return 1.0 + depth * f


@sound('fireball', -15, 'enemies', fade_out=0.08)
def fireball(rng):
    """Flaming projectile launch: a fwoosh of flame with flicker, a launch thump and crackles."""
    d = 0.6
    n = ns(d)
    roar = svf(noise(d, rng, 'pink'), points(n, [(0, 400), (0.08, 1700), (d, 500)], 'exp'), 0.9, 'lp')
    roar *= points(n, [(0, 0), (0.03, 1), (0.15, 0.7), (d, 0)]) * _flicker(n, rng)
    body = sine(glide(n, 230, 90, 0.2), n) * perc(n, 0.002, 0.08) * 0.5
    crack = filt(crackle(d, rng, rate=90), lp(6000, 0.7)) * 0.6
    return mix(roar, body, crack)


@sound('ice_shatter', -14, 'enemies', fade_out=0.1)
def ice_shatter(rng):
    """Ice block breaking: sharp crack, crunchy body and a cascade of glassy shards."""
    crack = filt(noise(0.03, rng), hp(1200)) * perc(ns(0.03), 0.0002, 0.005)
    thud = thump(210, 90, 0.15, tau=0.015, decay=0.035, drive=0) * 0.5
    m = ns(0.2)
    crunch = filt(noise(0.2, rng), bp(3500, 0.8)) * perc(m, 0.001, 0.04)
    d = 0.9
    n = ns(d)
    shards = np.zeros(n)
    for i in range(26):
        u = rng.random() ** 1.6
        p = int(u * 0.6 * SR)
        f = np.exp(rng.uniform(np.log(2500), np.log(9000)))
        g = bell(f, 0.2, ratio=rng.choice([1.41, 2.76, 3.3]), index=1.4, decay=rng.uniform(0.03, 0.1),
                 idx_decay=0.01, attack=0.0005)
        e = min(n, p + len(g))
        shards[p:e] += g[:e - p] * rng.uniform(0.3, 1.0) * (1.0 - 0.6 * u)
    grit = debris(d, rng, count=30, fmin=3000, fmax=9000, grain=(0.002, 0.01), start=0.05)
    x = mix(crack * 1.2, thud, crunch * 0.6, shards * 0.3, grit * 0.35)
    return room(x, rng, t60=0.6, wet=0.18)


@sound('freeze', -16, 'enemies', fade_out=0.08)
def freeze(rng):
    """Something freezes solid: a fast, accelerating crystallisation crackle with a rising icy
    shimmer, locked by a glassy 'clink' (0.3 s in)."""
    d = 0.6
    n = ns(d)
    t = np.arange(n) / SR
    lock_t = 0.3
    clicks = np.zeros(n)
    g = ns(0.003)
    for _ in range(110):
        p = int(rng.random() ** 0.55 * (lock_t * SR))
        gr = filt(white(g + 64, rng), hp(3000, 0.7))[:g] * np.hanning(g)
        clicks[p:p + g] += gr * rng.uniform(0.2, 1.0)
    frost = filt(noise(d, rng), bp(6500, 0.9)) * points(n, [(0, 0.15), (lock_t, 0.6), (lock_t + 0.08, 0.2), (d, 0)])
    sh = np.zeros(n)
    for f0, a in ((note_hz('E6'), 0.5), (note_hz('B6'), 0.4), (note_hz('F#7'), 0.25)):
        f = f0 * points(n, [(0, 0.7), (lock_t, 1.0)], 'exp') * (1.0 + 0.004 * np.sin(2 * np.pi * 6 * t))
        sh += a * fm(f, n, 2.0, points(n, [(0, 0.2), (lock_t, 1.4)]))
    sh *= points(n, [(0, 0.1), (lock_t - 0.02, 1.0), (lock_t + 0.05, 0.3), (d, 0)])
    clink = chime('B6', 0.4, decay=0.1, bright=1.0)
    lock = thump(300, 160, 0.1, tau=0.01, decay=0.025, drive=0)
    crunch = filt(noise(0.08, rng), bp(4200, 1.0)) * perc(ns(0.08), 0.0005, 0.015)
    return place([(0.0, clicks * 0.5), (0.0, frost * 0.25), (0.0, sh * 0.12),
                  (lock_t, clink * 0.5), (lock_t, lock * 0.4), (lock_t, crunch * 0.5)])


@sound('bomb_drop', -17, 'enemies', fade_out=0.05)
def bomb_drop(rng):
    """Bomb bay release clunk, then the falling whistle."""
    m = ns(0.2)
    latch = thump(260, 120, 0.12, tau=0.012, decay=0.03, click=0.6, rng=rng) * 0.7
    latch = mix(latch, sine(930.0, m) * perc(m, 0.0005, 0.05) * 0.25 + sine(2560.0, m) * perc(m, 0.0005, 0.02) * 0.15)
    d = 0.75
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 2100), (d, 650)], 'exp') * (1.0 + 0.006 * np.sin(2 * np.pi * 7 * t))
    env = points(n, [(0, 0), (0.06, 0.8), (d - 0.1, 1.0), (d, 0)])
    whistle = (sine(f, n) + 0.25 * sine(f * 2, n)) * env
    air = svf(noise(d, rng), f * 1.2, 4.0, 'bp') * env
    return place([(0.0, latch), (0.06, whistle * 0.45), (0.06, air * 0.3)])


@sound('bomb_explode', -10.5, 'enemies', fade_out=0.18)
def bomb_explode(rng):
    """Short, punchy bomb blast with metallic shrapnel (brighter/tighter than `explosion`)."""
    d = 1.1
    n = ns(d)
    boom = thump(115, 40, d, tau=0.05, decay=0.28, click=1.0, rng=rng, drive=2.5)
    blast = svf(noise(d, rng), ramp(n, 9000, 300, 'exp'), 0.7, 'lp') * perc(n, 0.0005, 0.16)
    crack = crackle(0.7, rng, rate=200) * perc(ns(0.7), 0.001, 0.2) * 0.8
    shrapnel = debris(1.0, rng, count=28, fmin=1500, fmax=6500, grain=(0.004, 0.02), start=0.02)
    body = soft_clip(blast * 1.2 + boom, 1.8)
    return room(mix(body, crack, shrapnel * 0.4), rng, t60=0.9, wet=0.16)


@sound('lava_bubble', -19, 'world', fade_out=0.08)
def lava_bubble(rng):
    """Thick lava bubble: a rising 'blub', a pop and a little sizzle."""
    d = 0.32
    n = ns(d)
    t = np.arange(n) / SR
    f = 85.0 * (1.0 + 1.8 * (1.0 - np.exp(-t / 0.06)))
    blub = sine(f, n) * perc(n, 0.008, 0.07)
    goo = svf(noise(d, rng, 'pink'), f * 4, 3.0, 'bp') * perc(n, 0.006, 0.05) * 0.6
    pop = filt(noise(0.04, rng), bp(1300, 1.5)) * perc(ns(0.04), 0.0005, 0.006)
    pop_tone = chirp(600, 1200, 0.05, decay=0.01) * 0.3
    m = ns(0.4)
    sizzle = filt(noise(0.4, rng), hp(4000)) * perc(m, 0.02, 0.12) * 0.12
    sizzle = sizzle + crackle(0.4, rng, rate=40) * perc(m, 0.005, 0.12) * 0.25
    return place([(0.0, blub), (0.0, goo), (0.11, pop * 0.6), (0.11, pop_tone), (0.12, sizzle)])


@sound('updraft_loop', -23, 'world', loop=True)
def updraft_loop(rng):
    """Rising column of air from a vent: endlessly rising 'barber-pole' noise sweeps over a low
    breathy rumble and a soft whistle. 3.0 s seamless loop."""
    n = ns(3.0)
    rise = barber_noise(n, rng, 220.0, 4200.0, layers=3, q=3.5)
    rumble = circ_noise(n, rng, 'brown', lp(260, 0.7), hp(50, 0.7))
    rumble *= 0.75 + 0.25 * periodic(n, 2, 0.1)
    w = circ_noise(n, rng, 'pink')
    whistle = wrapped_svf(w, 1150.0 * (1.0 + 0.05 * periodic(n, 2) + 0.02 * periodic(n, 5, 0.3)), 16.0)
    hiss = circ_noise(n, rng, 'white', hp(3500, 0.7), lp(10000, 0.7)) * (0.8 + 0.2 * periodic(n, 3))
    return rise * 1.4 + rumble * 0.5 + whistle * 0.2 + hiss * 0.04
