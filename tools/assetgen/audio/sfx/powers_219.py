"""2.19 powers, one per sanctuary: Lame boomerang (throw, catch), Nova de givre, Leurre
holographique, Balise de rappel (set, warp). Same cyan-energy family as the other powers
(FM shimmer, resonant zaps), with the frost and the violet warp of their own palettes."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


@sound('boomerang_throw', -16, 'powers', fade_out=0.06)
def boomerang_throw(rng):
    """A blade flung away: a short snap, then a spinning whirr that recedes (Doppler down)."""
    d = 0.55
    n = ns(d)
    snap = filt(noise(0.04, rng), bp(2600, 1.1)) * perc(ns(0.04), 0.0006, 0.01)
    spin_rate = glide(n, 22.0, 14.0, 0.45, 'exp')
    flutter = 0.55 + 0.45 * np.abs(np.sin(np.cumsum(2 * np.pi * spin_rate / SR)))
    f = glide(n, 980, 620, 0.5, 'exp')
    body = (fm(f, n, 1.5, 1.1) * 0.5 + sine(f * 2.0, n) * 0.2) * flutter
    env = points(n, [(0, 0), (0.015, 1.0), (0.2, 0.75), (d, 0.0)])
    air = whoosh(d, rng, (2800, 1400, 700), q=1.6, peak_at=0.18) * 0.55
    air = air * flutter
    glints = sparkle(0.3, rng, count=6, fmin=4000, fmax=8000, grain=0.04) * 0.15
    return place([(0.0, snap * 0.9), (0.005, body * env * 0.6), (0.0, air), (0.02, glints)])


@sound('boomerang_return', -16, 'powers', fade_out=0.05)
def boomerang_return(rng):
    """The blade comes back: an approaching whirr (Doppler up) closed by a bright catch."""
    d = 0.32
    n = ns(d)
    spin_rate = glide(n, 14.0, 24.0, 0.3, 'exp')
    flutter = 0.55 + 0.45 * np.abs(np.sin(np.cumsum(2 * np.pi * spin_rate / SR)))
    f = glide(n, 620, 1040, 0.3, 'exp')
    body = fm(f, n, 1.5, 1.0) * flutter * points(n, [(0, 0), (0.25, 1.0), (d, 0.6)]) * 0.45
    air = whoosh(d, rng, (700, 2600), q=1.4, peak_at=0.85) * 0.45
    catch_ = chime(1760, 0.35, decay=0.1, bright=0.9) * 0.4
    click = filt(noise(0.03, rng), bp(4200, 1.2)) * perc(ns(0.03), 0.0004, 0.008) * 0.6
    low = thump(190, 110, 0.12, tau=0.015, decay=0.035) * 0.35
    return place([(0.0, body), (0.0, air), (0.3, click), (0.3, catch_), (0.3, low)])


@sound('frost_nova', -14, 'powers', fade_out=0.1)
def frost_nova(rng):
    """A burst of cold: a soft boom, an icy crackling ring spreading out and tinkling crystals."""
    boom = thump(120, 50, 0.5, tau=0.04, decay=0.14, click=0.3, rng=rng, drive=1.3) * 0.7
    d = 0.9
    n = ns(d)
    hiss = svf(noise(d, rng, 'white'), ramp(n, 9000, 1800, 'exp'), 0.9, 'lp') * perc(n, 0.004, 0.32) * 0.35
    ring = whoosh(0.6, rng, (5000, 2400), q=3.0, peak_at=0.12) * 0.4
    crack = crackle(0.6, rng, rate=150) * 0.28
    f = glide(n, 1560, 1180, 0.6, 'exp')
    shimmer = fm(f, n, 2.01, 1.6) * perc(n, 0.006, 0.35) * 0.22
    tinkle = sparkle(0.9, rng, count=22, fmin=3500, fmax=9500, grain=0.06, density=[1.0, 0.8, 0.5, 0.25]) * 0.3
    return place([(0.0, boom), (0.0, hiss), (0.01, ring), (0.03, crack), (0.0, shimmer), (0.05, tinkle)])


@sound('decoy_spawn', -17, 'powers', fade_out=0.08)
def decoy_spawn(rng):
    """A hologram flickers on: a rising projector hum, a scan sweep and a glassy shimmer."""
    d = 0.6
    n = ns(d)
    t = np.arange(n) / SR
    f = glide(n, 220, 440, 0.25, 'exp')
    hum = (square(f, n) * 0.25 + sine(f * 2.0, n) * 0.35) * points(n, [(0, 0), (0.08, 1.0), (0.4, 0.7), (d, 0)])
    hum = filt(hum, lp(2600))
    flicker = 0.6 + 0.4 * (np.sin(2 * np.pi * 31 * t) > -0.3)
    sweep = zap(400, 3200, 0.35, cut0=800, cut1=6000, q=6.0, time=0.3, attack=0.01, decay=0.14) * 0.35
    glass = fm(glide(n, 1320, 1980, 0.3, 'exp'), n, 3.0, 0.9) * perc(n, 0.02, 0.22) * 0.2
    return place([(0.0, hum * flicker * 0.7), (0.02, sweep), (0.08, glass)])


@sound('recall_set', -17, 'powers', fade_out=0.06)
def recall_set(rng):
    """A beacon planted: a metallic tick, then a soft violet two-note ping."""
    tick = filt(noise(0.03, rng), bp(3200, 1.4)) * perc(ns(0.03), 0.0005, 0.007) * 0.7
    low = thump(220, 140, 0.1, tau=0.01, decay=0.03) * 0.4
    p1 = bell(note_hz('E5'), 0.5, ratio=2.0, index=1.4, decay=0.18) * 0.35
    p2 = bell(note_hz('B5'), 0.6, ratio=2.0, index=1.2, decay=0.22) * 0.3
    return place([(0.0, tick), (0.0, low), (0.02, p1), (0.11, p2)])


@sound('recall_warp', -15, 'powers', fade_out=0.06)
def recall_warp(rng):
    """Pulled back to the beacon: a reversed suck-in sweep snapping into a bright arrival."""
    d = 0.32
    n = ns(d)
    f = glide(n, 300, 2200, 0.3, 'exp')
    env = points(n, [(0, 0), (0.24, 1.0), (d, 0.0)])
    suck = fm(f, n, 1.5, 1.3) * env * 0.5
    air = whoosh(d, rng, (600, 6000), q=1.3, peak_at=0.9, color='white') * 0.35
    arrive = bell(note_hz('B5'), 0.45, ratio=1.5, index=1.6, decay=0.14) * 0.4
    spk = sparkle(0.35, rng, count=10, fmin=3000, fmax=9000, grain=0.04, density=[1.0, 0.6, 0.3, 0.1]) * 0.25
    settle = thump(160, 80, 0.14, tau=0.02, decay=0.04) * 0.45
    return place([(0.0, suck), (0.0, air), (0.29, arrive), (0.29, spk), (0.3, settle)])
