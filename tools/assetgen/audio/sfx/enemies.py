"""Enemies, hazards and the Thornwarden boss."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


def _growl(f, n, rng, rate=28.0, depth=0.5):
    """Creature voice: detuned saws with AM roughness."""
    t = np.arange(n) / SR
    v = saw(f, n) + saw(f * 1.012, n) * 0.8 + saw(f * 0.497, n) * 0.5
    am = 1.0 - depth * 0.5 * (1.0 + np.sin(2 * np.pi * rate * t + rng.random() * 6.28))
    return v * am


@sound('enemy_hurt', -17, 'enemies')
def enemy_hurt(rng):
    d = 0.16
    n = ns(d)
    f = glide(n, 640, 240, 0.12, 'exp')
    v = svf(_growl(f, n, rng, 45, 0.6), 2200, 1.5, 'lp') * perc(n, 0.002, 0.05)
    hit = filt(noise(0.04, rng), bp(1800, 1.0)) * perc(ns(0.04), 0.0005, 0.01)
    return mix(v * 0.7, hit * 0.6)


@sound('enemy_death', -15, 'enemies', fade_out=0.08)
def enemy_death(rng):
    d = 0.65
    n = ns(d)
    t = np.arange(n) / SR
    f = glide(n, 720, 80, 0.5, 'exp') * (1.0 + 0.06 * np.sin(2 * np.pi * 14 * t))
    v = svf(_growl(f, n, rng, 18, 0.5), ramp(n, 3500, 500, 'exp'), 2.0, 'lp') * perc(n, 0.003, 0.2)
    pop = thump(180, 60, 0.2, tau=0.02, decay=0.05, click=0.5, rng=rng) * 0.6
    burst = svf(noise(0.4, rng, 'pink'), ramp(ns(0.4), 4000, 300, 'exp'), 0.8, 'lp') * perc(ns(0.4), 0.001, 0.09)
    glints = sparkle(0.6, rng, count=10, fmin=2000, fmax=6000, density=[0.1, 1, 0.6, 0.2]) * 0.2
    return mix(v * 0.5, pop, burst * 0.6, glints)


@sound('enemy_shoot', -18, 'enemies')
def enemy_shoot(rng):
    d = 0.18
    n = ns(d)
    f = glide(n, 1500, 300, 0.12, 'exp')
    pew = square(f, n, 0.3) * perc(n, 0.001, 0.045)
    pew = filt(pew, lp(5000, 0.7))
    body = sine(f * 0.5, n) * perc(n, 0.001, 0.05)
    click = filt(noise(0.02, rng), hp(2000)) * perc(ns(0.02), 0.0003, 0.004)
    return mix(pew * 0.45, body * 0.6, click * 0.4)


@sound('enemy_alert', -16, 'enemies')
def enemy_alert(rng):
    def beep(f, d):
        n = ns(d)
        return filt(square(f, n, 0.5), lp(3200, 0.8)) * adsr(n, 0.004, 0.03, 0.7, 0.03, d - 0.03)
    return place([(0.0, beep(740, 0.085) * 0.6), (0.095, beep(1110, 0.13) * 0.6),
                  (0.0, sine(1480, ns(0.23)) * perc(ns(0.23), 0.003, 0.06) * 0.15)])


@sound('enemy_telegraph', -18, 'enemies', fade_out=0.04)
def enemy_telegraph(rng):
    d = 0.55
    n = ns(d)
    f = ramp(n, 240, 700, 'exp')
    rate = ramp(n, 9, 30, 'exp')
    trem = 0.55 + 0.45 * np.sin(2 * np.pi * np.cumsum(rate) / SR)
    v = svf(saw(f, n) + 0.5 * saw(f * 1.5, n), ramp(n, 600, 4000, 'exp'), 3.0, 'lp')
    env = points(n, [(0, 0), (0.05, 0.5), (d - 0.05, 1.0), (d, 0.0)])
    return v * trem * env * 0.5 + sine(f * 2, n) * env * 0.1


@sound('laser_charge', -18, 'enemies', fade_out=0.05)
def laser_charge(rng):
    d = 1.0
    n = ns(d)
    f = ramp(n, 180, 1700, 'exp')
    rate = ramp(n, 6, 40, 'exp')
    trem = 0.6 + 0.4 * np.sin(2 * np.pi * np.cumsum(rate) / SR)
    whine = fm(f, n, 2.0, ramp(n, 0.3, 2.0)) * trem
    hum = svf(saw(f * 0.25, n), f * 2, 2.0, 'lp') * 0.35
    fizz = filt(noise(d, rng), hp(5000)) * 0.08 * ramp(n, 0, 1) ** 2
    env = ramp(n, 0.15, 1.0, 'sq')
    return (whine * 0.5 + hum + fizz) * env


@sound('laser_fire', -14, 'enemies', fade_out=0.1)
def laser_fire(rng):
    d = 0.75
    n = ns(d)
    t = np.arange(n) / SR
    base = 110.0 * (1.0 + 0.01 * np.sin(2 * np.pi * 7 * t))
    beam = saw(base, n) + 0.7 * saw(base * 2.004, n) + 0.4 * square(base * 3.01, n, 0.4)
    beam = svf(beam, 2800 + 800 * np.sin(2 * np.pi * 11 * t), 1.4, 'lp')
    zing = chirp(3200, 900, 0.2, 'sine', time=0.12, decay=0.05) * 0.5
    blast = filt(noise(0.15, rng), bp(2500, 0.7)) * perc(ns(0.15), 0.0005, 0.03)
    sizzle = filt(noise(d, rng), hp(3500)) * 0.12 * (0.6 + 0.4 * np.abs(lfo(n, 47)))
    env = adsr(n, 0.004, 0.15, 0.6, 0.3, d - 0.3)
    return mix((beam * 0.3 + sizzle) * env, zing, blast * 0.7)


@sound('golem_slam', -11, 'enemies', fade_out=0.15)
def golem_slam(rng):
    boom = thump(90, 44, 1.0, tau=0.06, decay=0.26, click=0.6, rng=rng, drive=2.0)
    d = 0.6
    n = ns(d)
    crunch = svf(noise(d, rng, 'pink'), ramp(n, 2500, 200, 'exp'), 0.8, 'lp') * perc(n, 0.001, 0.12)
    rocks = debris(1.0, rng, count=26, fmin=350, fmax=2800, grain=(0.006, 0.035), start=0.04)
    rumble = filt(noise(1.0, rng, 'brown'), lp(220), hp(45)) * perc(ns(1.0), 0.01, 0.3) * 0.8
    return room(mix(boom, crunch * 0.9, rocks * 0.5, rumble), rng, t60=0.7, wet=0.15)


@sound('explosion', -10, 'enemies', fade_out=0.2)
def explosion(rng):
    d = 1.5
    n = ns(d)
    blast = svf(noise(d, rng, 'pink'), ramp(n, 7000, 220, 'exp') * 1.0, 0.7, 'lp')
    blast *= perc(n, 0.001, 0.28)
    boom = thump(85, 42, d, tau=0.07, decay=0.38, click=0.8, rng=rng, drive=2.2)
    crack = crackle(0.9, rng, rate=150) * perc(ns(0.9), 0.001, 0.3) * 0.7
    body = soft_clip(blast * 1.1 + boom, 1.6)
    return room(mix(body, crack), rng, t60=1.1, wet=0.18)


@sound('boss_roar', -10, 'boss', fade_out=0.25)
def boss_roar(rng):
    d = 1.9
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 62), (0.25, 74), (1.2, 64), (d, 44)], 'exp')
    f = f * (1.0 + 0.025 * np.sin(2 * np.pi * 5.5 * t))
    v = _growl(f, n, rng, rate=32.0, depth=0.6) + 0.6 * _growl(f * 1.5, n, rng, 29.0, 0.5)
    # vowel "aah" -> "ohh" formant movement
    f1 = points(n, [(0, 520), (0.4, 750), (1.3, 600), (d, 380)], 'exp')
    f2 = points(n, [(0, 1000), (0.4, 1250), (1.3, 950), (d, 700)], 'exp')
    voc = svf(v, f1, 3.0, 'bp') + 0.6 * svf(v, f2, 4.0, 'bp') + 0.25 * svf(v, 180, 0.8, 'lp')
    breath = svf(noise(d, rng, 'pink'), f2 * 1.6, 1.0, 'bp') * 0.5
    env = points(n, [(0, 0), (0.12, 0.85), (0.3, 1.0), (1.3, 0.8), (d, 0)])
    sub = sine(f, n) * env * 0.35
    x = soft_clip((voc + breath) * env * 0.9, 1.8) + sub
    return room(x, rng, t60=1.4, wet=0.22)


@sound('boss_phase', -11, 'boss', fade_out=0.25)
def boss_phase(rng):
    d = 1.8
    rise_n = ns(0.55)
    riser = svf(noise(0.55, rng), ramp(rise_n, 400, 6000, 'exp'), 3.0, 'bp') * ramp(rise_n, 0, 1) ** 2
    hit = thump(85, 42, 1.2, tau=0.06, decay=0.3, click=0.8, rng=rng, drive=2.0)
    m = ns(1.25)
    t = np.arange(m) / SR
    chord = np.zeros(m)
    for nt, a in (('C3', 0.5), ('G3', 0.4), ('C4', 0.35), ('Eb4', 0.3), ('Gb4', 0.25)):
        f = note_hz(nt) * (1.0 + 0.004 * np.sin(2 * np.pi * 5 * t + a * 10))
        chord += a * svf(saw(f, m) + saw(f * 1.006, m), 1800, 0.8, 'lp')
    chord *= perc(m, 0.01, 0.45)
    metal = chime(310, 1.2, decay=0.5, bright=0.6) * 0.35
    x = place([(0.0, riser * 0.5), (0.5, hit), (0.5, chord * 0.35), (0.5, metal)], ns(d + 0.3))
    return room(x, rng, t60=1.6, wet=0.25)


@sound('summon', -15, 'boss', fade_out=0.15)
def summon(rng):
    notes = ['D4', 'F4', 'Ab4', 'B4', 'D5', 'F5']
    seq = arp(notes, 0.075, lambda f, i: bell(f, 0.5, ratio=1.41, index=2.2, decay=0.25), vel=[
        0.5, 0.55, 0.6, 0.7, 0.8, 0.9])
    d = len(seq) / SR
    n = len(seq)
    swirl = whoosh(d, rng, (300, 2500, 600), q=3.0, peak_at=0.5) * 0.4
    t = np.arange(n) / SR
    drone = svf(saw(73.4 * (1 + 0.01 * np.sin(2 * np.pi * 3 * t)), n), 600, 2.0, 'lp')
    drone *= points(n, [(0, 0), (0.3, 0.6), (d, 0)]) * 0.35
    return room(mix(seq * 0.5, swirl, drone), rng, t60=1.0, wet=0.3)


@sound('wisp_teleport', -17, 'enemies', fade_out=0.06)
def wisp_teleport(rng):
    d = 0.45
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 600), (0.12, 1500), (0.3, 450), (d, 900)], 'exp')
    wob = 1.0 + 0.03 * np.sin(2 * np.pi * 13 * t)
    v = fm(f * wob, n, 1.5, 1.5) + 0.7 * fm(f * 1.01 * wob, n, 2.0, 1.0)
    env = points(n, [(0, 0), (0.04, 1.0), (0.2, 0.6), (d, 0)])
    air = whoosh(d, rng, (1500, 5000, 2000), q=2.0, peak_at=0.3) * 0.3
    return room(mix(v * env * 0.4, air), rng, t60=0.6, wet=0.25)


@sound('spore_spit', -18, 'enemies')
def spore_spit(rng):
    d = 0.24
    n = ns(d)
    plop = sine(glide(n, 520, 140, 0.07, 'exp'), n) * perc(n, 0.002, 0.05)
    squelch = svf(noise(d, rng, 'pink'), ramp(n, 1800, 500, 'exp'), 2.5, 'bp') * perc(n, 0.003, 0.04)
    puff = filt(noise(0.12, rng), bp(3000, 1.0)) * perc(ns(0.12), 0.01, 0.03) * 0.3
    return mix(plop * 0.8, squelch * 0.8, puff)
