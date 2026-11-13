"""v2 enemies and bosses: slimes, bats, burrowers, weavers, fire/ice roars, alarms, wyrms."""
from sfx.core import sound
from sfx.enemies import _growl
from sfx.kit import *  # noqa: F401,F403


@sound('slime_split', -17, 'enemies', fade_out=0.06)
def slime_split(rng):
    """Gooey stretch, a wet 'splt' and two wobbling blobs bouncing apart (one up, one down)."""
    d = 0.4
    n = ns(d)
    stretch = svf(noise(d, rng, 'pink'), points(n, [(0, 400), (0.15, 1200), (0.2, 300)], 'exp'), 4.0, 'bp')
    stretch *= points(n, [(0, 0), (0.12, 1), (0.18, 0.3), (d, 0)])
    m = ns(0.06)
    splt = filt(noise(0.06, rng), bp(1800, 1.2)) * perc(m, 0.0005, 0.01)
    k = ns(0.25)
    t = np.arange(k) / SR
    wob = 1.0 + 0.5 * np.sin(2 * np.pi * 22 * t) * np.exp(-t / 0.08)
    a = sine(glide(k, 260, 520, 0.1) * wob ** 0.1, k) * perc(k, 0.003, 0.06) * wob
    b = sine(glide(k, 260, 170, 0.1) * wob ** 0.1, k) * perc(k, 0.003, 0.07) * wob
    return place([(0.0, stretch * 0.7), (0.15, splt * 0.8), (0.16, a * 0.6), (0.19, b * 0.6)])


def _screech(d, f0, f1, rng):
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, f0), (d * 0.3, f0 * 1.15), (d, f1)], 'exp') * (1.0 + 0.03 * np.sin(2 * np.pi * 38 * t))
    v = fm(f, n, 1.5, 2.2) + 0.4 * saw(f * 0.5, n)
    rough = 1.0 - 0.45 * (0.5 + 0.5 * np.sin(2 * np.pi * 95 * t + rng.random() * 6.28))
    env = points(n, [(0, 0), (0.012, 1.0), (d * 0.6, 0.8), (d, 0)])
    return filt(v * rough, lp(7000, 0.7), hp(900, 0.7)) * env


@sound('bat_screech', -17, 'enemies', fade_out=0.05)
def bat_screech(rng):
    """Raspy high double shriek with a leathery wing flap."""
    a = _screech(0.15, 3200, 2400, rng)
    b = _screech(0.24, 3700, 2000, rng)
    m = ns(0.08)
    flap = filt(noise(0.08, rng, 'pink'), bp(900, 1.0)) * perc(m, 0.004, 0.02)
    return place([(0.0, flap * 0.5), (0.02, a * 0.8), (0.19, b)])


@sound('burrow', -16, 'enemies', fade_out=0.15)
def burrow(rng):
    """Burrowing through earth: a dive thump, churning dirt, crunching grit and low rumble."""
    d = 0.9
    n = ns(d)
    t = np.arange(n) / SR
    env = points(n, [(0, 0), (0.08, 1), (0.6, 0.8), (d, 0)])
    rumble = filt(noise(d, rng, 'brown'), lp(220), hp(40)) * env
    churn = svf(noise(d, rng, 'pink'), 500 + 300 * np.sin(2 * np.pi * 6 * t), 1.2, 'bp')
    churn *= (0.6 + 0.4 * np.abs(lfo(n, 11))) * env
    crunch = debris(d, rng, count=60, fmin=300, fmax=2500, grain=(0.004, 0.025), gravity=False)
    burst = thump(95, 45, 0.3, tau=0.03, decay=0.07, click=0.4, rng=rng)
    return mix(rumble * 0.9, churn * 0.6, crunch * 0.6, burst * 0.6)


@sound('web_hit', -17, 'enemies', fade_out=0.06)
def web_hit(rng):
    """Sticky web glob landing: a 'thwip', a wet splat, an elastic wobble and strands snapping."""
    thwip = chirp(1400, 300, 0.08, 'tri', time=0.06, decay=0.03) * 0.5
    m = ns(0.12)
    splat = svf(noise(0.12, rng, 'pink'), ramp(m, 2500, 600, 'exp'), 2.0, 'bp') * perc(m, 0.001, 0.03)
    k = ns(0.3)
    t = np.arange(k) / SR
    f = 150.0 * (1.0 + 0.35 * np.sin(2 * np.pi * 9 * t) * np.exp(-t / 0.09))
    stretch = (sine(f, k) + 0.3 * triangle(f * 2, k)) * perc(k, 0.004, 0.08)
    sticky = crackle(0.25, rng, rate=60) * perc(ns(0.25), 0.003, 0.07) * 0.35
    return place([(0.0, thwip), (0.02, splat), (0.03, stretch * 0.5), (0.05, sticky)])


@sound('boss_roar_fire', -10, 'boss', fade_out=0.3)
def boss_roar_fire(rng):
    """Molten colossus roar: low growl with formant movement inside a roaring flame and embers."""
    d = 2.2
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 50), (0.3, 62), (1.4, 55), (d, 38)], 'exp') * (1.0 + 0.03 * np.sin(2 * np.pi * 4.5 * t))
    v = _growl(f, n, rng, rate=26.0, depth=0.6) + 0.5 * _growl(f * 1.5, n, rng, 23.0, 0.5)
    f1 = points(n, [(0, 450), (0.4, 700), (1.4, 520), (d, 350)], 'exp')
    f2 = points(n, [(0, 900), (0.4, 1150), (1.4, 850), (d, 600)], 'exp')
    voc = svf(v, f1, 3.0, 'bp') + 0.6 * svf(v, f2, 4.0, 'bp') + 0.25 * svf(v, 170, 0.8, 'lp')
    flame = svf(noise(d, rng, 'pink'), f2 * 1.3, 0.7, 'lp')
    flame *= 1.0 + 0.4 * filt(white(n, rng), lp(18, 0.7)) / 0.05
    env = points(n, [(0, 0), (0.12, 0.85), (0.35, 1.0), (1.4, 0.8), (d, 0)])
    embers = filt(crackle(d, rng, rate=70, decay=False), lp(6000, 0.7)) * env * 0.6
    sub = sine(f, n) * env * 0.4
    x = soft_clip((voc + flame * 0.5) * env * 0.9, 2.2) + sub + embers
    return room(x, rng, t60=1.5, wet=0.22)


@sound('boss_roar_ice', -10, 'boss', fade_out=0.35)
def boss_roar_ice(rng):
    """Frozen titan roar: growl with a freezing hissing breath, glassy resonances riding the
    voice, and ice shards falling off at the end."""
    d = 2.3
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 70), (0.3, 84), (1.4, 72), (d, 52)], 'exp') * (1.0 + 0.02 * np.sin(2 * np.pi * 5 * t))
    v = _growl(f, n, rng, rate=30.0, depth=0.55) + 0.6 * _growl(f * 1.5, n, rng, 27.0, 0.5)
    f1 = points(n, [(0, 500), (0.4, 720), (1.4, 560), (d, 380)], 'exp')
    f2 = points(n, [(0, 1100), (0.4, 1400), (1.4, 1000), (d, 750)], 'exp')
    voc = svf(v, f1, 3.0, 'bp') + 0.6 * svf(v, f2, 4.0, 'bp') + 0.2 * svf(v, 190, 0.8, 'lp')
    env = points(n, [(0, 0), (0.12, 0.85), (0.3, 1.0), (1.3, 0.8), (d, 0)])
    breath = svf(noise(d, rng), points(n, [(0, 3000), (0.5, 6500), (d, 4000)], 'exp'), 1.4, 'bp')
    crystal = np.zeros(n)
    for r, a in ((12.0, 1.0), (17.3, 0.7), (23.1, 0.45)):
        crystal += a * sine(f * r, n)
    shards = np.zeros(n)
    for _ in range(12):
        p = ns(1.2 + rng.random() ** 1.3 * 0.9)
        fr = np.exp(rng.uniform(np.log(3000), np.log(8500)))
        g = bell(fr, 0.25, ratio=2.76, index=1.2, decay=rng.uniform(0.04, 0.12), idx_decay=0.01)
        e = min(n, p + len(g))
        shards[p:e] += g[:e - p] * rng.uniform(0.3, 1.0)
    sub = sine(f, n) * env * 0.3
    x = soft_clip(voc * env * 0.9, 1.8) + breath * env * 0.45 + crystal * env * 0.05 + sub
    x = x + shards * 0.12
    return room(x, rng, t60=2.0, wet=0.3, hf=0.6)


@sound('mech_alarm', -15, 'boss', fade_out=0.08)
def mech_alarm(rng):
    """Ancient machine alarm: a heavy relay clunk, servo spin-up and a two-tone klaxon (x3)."""
    clunk = thump(180, 70, 0.15, tau=0.015, decay=0.04, click=0.6, rng=rng)
    parts = [(0.0, clunk * 0.8)]
    for i in range(3):
        for j, f in enumerate((880.0, 660.0)):
            d = 0.17
            m = ns(d)
            tt = np.arange(m) / SR
            fr = f * (1.0 - 0.04 * np.exp(-tt / 0.02))
            tone = filt(square(fr, m, 0.4) + 0.5 * saw(fr * 1.005, m), lp(3500, 0.8))
            tone *= adsr(m, 0.006, 0.03, 0.85, 0.02, d - 0.03)
            parts.append((0.06 + (2 * i + j) * 0.175, soft_clip(tone * 0.6, 1.5) * (0.9 if i < 2 else 0.8)))
    d = 1.15
    n = ns(d)
    servo = svf(saw(points(n, [(0, 160), (0.4, 330), (d, 300)], 'exp'), n), 900, 2.0, 'lp')
    servo *= points(n, [(0, 0), (0.1, 0.6), (0.9, 0.4), (d, 0)])
    parts.append((0.0, servo * 0.18))
    return place(parts)


@sound('wyrm_roar', -10.5, 'boss', fade_out=0.3)
def wyrm_roar(rng):
    """Flying serpent: a high, trilling shriek-roar with a rattling hiss and a low body rumble."""
    d = 2.0
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 300), (0.2, 430), (1.0, 360), (d, 220)], 'exp')
    f = f * (1.0 + 0.025 * np.sin(2 * np.pi * 12 * t))
    v = fm(f, n, 1.0, 2.0) + 0.7 * _growl(f * 0.5, n, rng, rate=34.0, depth=0.5)
    voc = svf(v, points(n, [(0, 1100), (0.3, 1500), (d, 900)], 'exp'), 3.0, 'bp')
    voc += 0.5 * svf(v, points(n, [(0, 2400), (0.3, 2900), (d, 2000)], 'exp'), 5.0, 'bp')
    env = points(n, [(0, 0), (0.08, 0.9), (0.25, 1.0), (1.2, 0.75), (d, 0)])
    hiss = filt(noise(d, rng), hp(3000, 0.7), lp(10000, 0.7))
    hiss *= 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * 30 * t))
    low = svf(_growl(f * 0.25, n, rng, 20.0, 0.4), 320, 0.8, 'lp')
    x = soft_clip(voc * env * 1.1, 2.0) + hiss * env * 0.18 + low * env * 0.35
    return room(x, rng, t60=1.6, wet=0.25)
