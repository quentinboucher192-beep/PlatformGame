"""2.33: Singularis, le Coeur Effondre (giga boss of the Noyau stellaire)."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('star_roar', -10, 'boss', fade_out=0.4)
def star_roar(rng):
    """Singularis's voice: a dead star groaning awake: a vast sub growl bending down under a
    choir of shimmering overtones, a gravitational warble and a long glittering tail."""
    d = 2.8
    n = ns(d)
    t = np.arange(n) / SR
    f = points(n, [(0, 46), (0.35, 58), (1.6, 50), (d, 34)], 'exp') * (1.0 + 0.025 * np.sin(2 * np.pi * 3.2 * t))
    growl = saw(f, n) * (0.7 + 0.3 * np.sin(2 * np.pi * 21.0 * t))
    growl = svf(growl, points(n, [(0, 380), (0.5, 900), (1.8, 600), (d, 300)], 'exp'), 2.2, 'bp')
    sub = sine(f, n) + 0.4 * sine(f * 2.0, n)
    # overtones of a star: a cluster of slow-beating partials, like a choir in a cathedral
    choir = np.zeros(n)
    for k, r in enumerate((4.0, 5.04, 6.0, 7.13, 8.0, 10.1)):
        choir += sine(f * r * (1.0 + 0.002 * k), n) * (0.6 + 0.4 * np.sin(2 * np.pi * (0.7 + 0.13 * k) * t + k))
    choir = svf(choir, 1600, 0.8, 'lp') * points(n, [(0, 0), (0.4, 0.5), (1.6, 0.7), (d, 0)])
    env = points(n, [(0, 0), (0.15, 0.9), (0.4, 1.0), (1.8, 0.8), (d, 0)])
    glitter = sparkle(1.8, rng, count=18, fmin=3000, fmax=9000, density=[0.1, 0.5, 1, 0.6]) * 0.18
    x = mix(soft_clip(growl * env * 1.1, 2.0), sub * env * 0.45, choir * 0.12)
    x = place([(0.0, x), (0.9, glitter)])
    x = filt(x, hp(32))  # no rumble
    return filt(room(x, rng, t60=2.2, wet=0.32), hp(24))  # no DC


@sound('shell_shatter', -10.5, 'boss', fade_out=0.35)
def shell_shatter(rng):
    """Effondrement (phase 5): the crust of the dead star cracks in a rising groan, bursts in a
    heavy boom of plasma and its plates rain down."""
    d = 2.4
    n = ns(d)
    t = np.arange(n) / SR
    groan_f = points(n, [(0, 70), (0.55, 140), (d, 60)], 'exp')
    groan = svf(saw(groan_f, n), 700, 3.0, 'bp') * points(n, [(0, 0), (0.5, 0.8), (0.62, 0), (d, 0)])
    cracks = debris(0.6, rng, count=26, fmin=1200, fmax=6000, grain=(0.002, 0.012)) * 0.6
    boom = thump(110, 32, 1.4, tau=0.06, decay=0.45, click=0.6, rng=rng, drive=2.2)
    blast = svf(noise(1.6, rng, 'pink'), points(ns(1.6), [(0, 6000), (1.6, 300)], 'exp'), 0.8, 'lp')
    blast *= points(ns(1.6), [(0, 1), (1.6, 0)])
    plates = debris(1.4, rng, count=34, fmin=250, fmax=2500, grain=(0.01, 0.05), start=0.1) * 0.5
    hiss = svf(noise(d, rng), points(n, [(0, 3000), (d, 7000)], 'exp'), 1.2, 'bp') * points(
        n, [(0, 0), (0.6, 0.0), (0.7, 0.5), (d, 0)]) * 0.15
    x = place([(0.0, groan * 0.6), (0.0, cracks), (0.58, boom * 1.0), (0.58, blast * 0.7), (0.72, plates)])
    x = filt(mix(x, hiss), hp(38))
    return room(soft_clip(x, 1.6), rng, t60=1.8, wet=0.28)


@sound('supernova', -10, 'boss', fade_out=0.6)
def supernova(rng):
    """Supernova (phase 8 and the last light): everything is drawn in (a rising, accelerating
    whoosh and a climbing whine), a breath of silence, then a blinding boom and its long wash."""
    d = 3.6
    n = ns(d)
    t = np.arange(n) / SR
    rise = whoosh(1.5, rng, f_path=(200, 1500, 7000), q=1.4, peak_at=0.97, shape=3.5)
    whine = sine(points(ns(1.5), [(0, 220), (1.5, 1760)], 'exp'), ns(1.5)) * points(ns(1.5), [(0, 0), (1.4, 0.6), (1.5, 0)])
    boom = thump(90, 26, 2.0, tau=0.08, decay=0.7, click=0.8, rng=rng, drive=2.6)
    wash = svf(noise(2.0, rng, 'pink'), points(ns(2.0), [(0, 9000), (2.0, 400)], 'exp'), 0.7, 'lp')
    wash *= points(ns(2.0), [(0, 1), (0.4, 0.6), (2.0, 0)])
    ring = np.zeros(ns(2.0))
    for r in (1.0, 1.5, 2.01, 2.99):
        ring += sine(points(ns(2.0), [(0, 330 * r), (2.0, 300 * r)], 'exp'), ns(2.0)) / r
    ring *= points(ns(2.0), [(0, 0), (0.05, 0.4), (2.0, 0)])
    glints = sparkle(1.6, rng, count=24, fmin=3500, fmax=10000, density=[1, 0.7, 0.4, 0.1]) * 0.25
    x = place([(0.0, rise * 0.7), (0.0, whine * 0.18), (1.62, boom * 1.0), (1.62, wash * 0.75), (1.64, ring * 0.2),
               (1.7, glints)])
    _ = t
    x = filt(x, hp(42))
    return room(soft_clip(x, 1.8), rng, t60=2.6, wet=0.32)
