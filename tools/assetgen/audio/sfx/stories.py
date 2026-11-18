"""2.63: the sounds of the giga bosses' stories and of the trailer (data/stories/*.json, their
scenes' « sfx »): an ambience under each shot (seamless loops, kept low) and the noises of what
happens (an egg cracking, a jet of fire, wings, stone splitting, the star of the centre
collapsing, deep water, bones, candles, steam, a singing star…).  The game's own sounds are used
too where they fit (the bosses' roars, impacts, crumbling rock, a supernova…)."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403
from sfx.loopkit import circ_noise, periodic, whole_hz, wrap_add, wrapped_svf


def _sines(n, freqs, amps, mod=None):
    t = np.arange(n) / SR
    x = np.zeros(n)
    for f, a in zip(freqs, amps):
        x += a * np.sin(2 * np.pi * whole_hz(f, n) * t)
    return x * (mod if mod is not None else 1.0)


# =========================================================================== ambiences (loops)
@sound('story_amb_night', -31, 'story', loop=True)
def story_amb_night(rng):
    """A night in the open: wind breathing over the rocks, a few crickets far away."""
    n = ns(3.0)
    wind = wrapped_svf(circ_noise(n, rng, 'pink'), 520.0 * np.exp(0.5 * periodic(n, 1) + 0.2 * periodic(n, 3, 0.3)), 0.8, 'bp')
    wind *= 0.7 + 0.3 * periodic(n, 2, 0.1)
    x = wind
    for k in range(5):
        chirp_ = tone(4200 + 300 * k, 0.05, 'sine', decay=0.02) * 0.08
        for j in range(3):
            wrap_add(x, chirp_, int(rng.uniform(0, n)) + j * ns(0.07))
    return x


@sound('story_amb_cave', -31, 'story', loop=True)
def story_amb_cave(rng):
    """Inside a cave: a deep hum of the rock, air moving, drops falling into puddles, echoing."""
    n = ns(3.0)
    hum = wrapped_svf(circ_noise(n, rng, 'brown'), 140.0 * np.exp(0.2 * periodic(n, 1)), 1.2, 'bp') * 0.8
    air = wrapped_svf(circ_noise(n, rng, 'pink'), 900.0 * np.exp(0.3 * periodic(n, 2, 0.2)), 0.7, 'bp') * 0.25
    x = hum + air
    for k in range(4):
        f = rng.uniform(1100, 2200)
        drop = chirp(f, f * 1.6, 0.06, 'sine', decay=0.03) * 0.5
        echo_ = np.concatenate([drop, np.zeros(ns(0.18)), drop * 0.35, np.zeros(ns(0.2)), drop * 0.12])
        wrap_add(x, echo_, int(rng.uniform(0, n)))
    return x


@sound('story_amb_space', -32, 'story', loop=True)
def story_amb_space(rng):
    """Deep space: a vast slow chord humming, beating softly, a hiss of stardust."""
    n = ns(3.0)
    pad = _sines(n, [55, 82.5, 110, 165, 220.5, 330], [0.5, 0.35, 0.3, 0.2, 0.12, 0.06],
                 0.75 + 0.25 * periodic(n, 1, 0.1))
    shimmer = _sines(n, [880, 1321, 1762], [0.03, 0.02, 0.015], 0.5 + 0.5 * periodic(n, 2, 0.4))
    dust = circ_noise(n, rng, 'white', hp(5000, 0.7)) * 0.03 * (0.6 + 0.4 * periodic(n, 3))
    return pad + shimmer + dust


@sound('story_amb_deep', -31, 'story', loop=True)
def story_amb_deep(rng):
    """The deep sea: a muffled pressure rumble, slow currents, bubbles rising now and then."""
    n = ns(3.0)
    rumble = wrapped_svf(circ_noise(n, rng, 'brown'), 90.0 * np.exp(0.3 * periodic(n, 1)), 1.0, 'lp') * 1.2
    current = wrapped_svf(circ_noise(n, rng, 'pink'), 380.0 * np.exp(0.4 * periodic(n, 1, 0.3)), 1.4, 'bp') * 0.35
    x = rumble + current
    for k in range(7):
        f = rng.uniform(500, 1100)
        b = chirp(f, f * 2.2, 0.05, 'sine', decay=0.025) * 0.35
        wrap_add(x, b, int(rng.uniform(0, n)))
    return x


@sound('story_amb_magma', -30, 'story', loop=True)
def story_amb_magma(rng):
    """Lava nearby: a heavy rumble breathing, glops of magma, embers crackling."""
    n = ns(3.0)
    rumble = wrapped_svf(circ_noise(n, rng, 'brown'), 110.0 * np.exp(0.25 * periodic(n, 2)), 1.0, 'lp') * (0.8 + 0.2 * periodic(n, 1))
    x = rumble
    for k in range(5):
        glop = chirp(rng.uniform(90, 160), rng.uniform(220, 320), 0.12, 'sine', decay=0.05) * 0.5
        wrap_add(x, glop, int(rng.uniform(0, n)))
    cr = crackle(3.0, rng, rate=40.0, decay=False) * 0.25
    return x + cr[:n]


@sound('story_amb_fire', -29, 'story', loop=True)
def story_amb_fire(rng):
    """A fire burning: its roar and its crackles."""
    n = ns(3.0)
    roar = wrapped_svf(circ_noise(n, rng, 'pink'), 700.0 * np.exp(0.3 * periodic(n, 3, 0.2)), 0.6, 'bp') * (0.8 + 0.2 * periodic(n, 5))
    cr = crackle(3.0, rng, rate=55.0, decay=False) * 0.5
    return roar + cr[:n]


@sound('story_amb_foundry', -30, 'story', loop=True)
def story_amb_foundry(rng):
    """The foundry: furnaces roaring, a machine humming, hammers far away on a slow beat."""
    n = ns(3.0)
    furn = wrapped_svf(circ_noise(n, rng, 'pink'), 300.0 * np.exp(0.2 * periodic(n, 1)), 0.7, 'bp') * 0.6
    hum = _sines(n, [60, 120, 180], [0.3, 0.15, 0.06], 0.85 + 0.15 * periodic(n, 4))
    x = furn + hum
    for k in range(4):
        clang = bell(rng.uniform(600, 900), 0.4, ratio=2.76, index=3.0, decay=0.12) * 0.12
        wrap_add(x, filt(clang, lp(2500)), int(k * n / 4 + rng.uniform(0, n / 16)))
    return x


@sound('story_amb_crypt', -32, 'story', loop=True)
def story_amb_crypt(rng):
    """The catacombs: a hollow wind moaning through the galleries, a drop far away."""
    n = ns(3.0)
    moan = wrapped_svf(circ_noise(n, rng, 'pink'), 330.0 * np.exp(0.45 * periodic(n, 1) + 0.15 * periodic(n, 3, 0.4)), 4.0, 'bp')
    moan *= 0.6 + 0.4 * periodic(n, 1, 0.25)
    low = wrapped_svf(circ_noise(n, rng, 'brown'), 120.0, 0.8, 'lp') * 0.6
    x = moan * 0.5 + low
    drop = chirp(1500, 2300, 0.05, 'sine', decay=0.03) * 0.3
    wrap_add(x, np.concatenate([drop, np.zeros(ns(0.25)), drop * 0.3]), int(n * 0.6))
    return x


# =========================================================================== one-shots
@sound('story_egg_crack', -16, 'story', fade_out=0.05)
def story_egg_crack(rng):
    """An egg cracking, then its shell bursting open."""
    ticks = [debris(0.05, rng, count=4, fmin=2500, fmax=6000, grain=(0.002, 0.006)) for _ in range(3)]
    burst = debris(0.3, rng, count=18, fmin=1500, fmax=7000, grain=(0.003, 0.02)) * 0.8
    pop = thump(300, 140, 0.12, tau=0.02, decay=0.05, click=0.6, rng=rng) * 0.4
    return room(place([(0.0, ticks[0]), (0.18, ticks[1]), (0.31, ticks[2]), (0.45, burst), (0.45, pop)]), rng, t60=0.3, wet=0.12)


@sound('story_flame', -14, 'story', fade_out=0.3)
def story_flame(rng):
    """A jet of fire: a blast of air catching, roaring, dying down."""
    d = 1.6
    n = ns(d)
    roar = svf(noise(d, rng, 'pink'), points(n, [(0, 400), (0.15, 1800), (d, 600)], 'exp'), 0.7, 'bp')
    roar *= points(n, [(0, 0), (0.06, 1.0), (1.1, 0.8), (d, 0)])
    rumble = svf(noise(d, rng, 'brown'), 160, 0.8, 'lp') * points(n, [(0, 0), (0.1, 1.0), (d, 0)]) * 0.8
    cr = crackle(d, rng, rate=70.0) * 0.3
    return mix(soft_clip(roar * 1.2, 1.4), rumble, cr)


@sound('story_puff', -18, 'story', fade_out=0.1)
def story_puff(rng):
    """A small breath of smoke, then a little cough."""
    puff = whoosh(0.45, rng, (300, 900, 400), q=0.9, peak_at=0.25, color='pink') * 0.8
    cough = mix(whoosh(0.12, rng, (900, 1600, 700), q=1.6, peak_at=0.2, color='white') * 0.6,
                tone(420, 0.1, 'tri', decay=0.04) * 0.15)
    return place([(0.0, puff), (0.55, cough), (0.78, cough * 0.7)])


@sound('story_wings', -17, 'story', fade_out=0.08)
def story_wings(rng):
    """Wings beating: three soft gusts of feathers."""
    flap = lambda k: whoosh(0.22, rng, (600, 1500, 500), q=0.8, peak_at=0.35, color='pink') * (1.0 - 0.15 * k)
    return place([(0.0, flap(0)), (0.26, flap(1)), (0.52, flap(2))])


@sound('story_stone', -14, 'story', fade_out=0.2)
def story_stone(rng):
    """Stone splitting: a deep grinding groan, a crack, grit falling."""
    d = 1.4
    n = ns(d)
    grind = svf(noise(d, rng, 'brown'), points(n, [(0, 120), (0.6, 260), (d, 90)], 'exp'), 2.0, 'bp')
    grind *= points(n, [(0, 0), (0.3, 0.8), (0.7, 1.0), (d, 0)]) * (0.7 + 0.3 * np.abs(np.sin(np.arange(n) / SR * 2 * np.pi * 13)))
    crack = debris(0.2, rng, count=14, fmin=800, fmax=4000, grain=(0.002, 0.01)) * 0.9
    grit = debris(0.8, rng, count=24, fmin=1500, fmax=6000, grain=(0.002, 0.008)) * 0.35
    boom = thump(90, 40, 0.5, tau=0.04, decay=0.18, click=0.5, rng=rng) * 0.6
    return room(place([(0.0, grind), (0.62, crack), (0.62, boom), (0.7, grit)]), rng, t60=0.9, wet=0.2)


@sound('story_chisel', -18, 'story', fade_out=0.05)
def story_chisel(rng):
    """A chisel tapping stone: two light taps, a third harder, grit."""
    tap = lambda f, a: mix(bell(f, 0.12, ratio=3.1, index=1.6, decay=0.03), debris(0.04, rng, count=3, fmin=3000, fmax=7000) * 0.5) * a
    return room(place([(0.0, tap(2400, 0.6)), (0.32, tap(2300, 0.6)), (0.66, tap(2200, 1.0))]), rng, t60=0.5, wet=0.2)


@sound('story_launch', -14, 'story', fade_out=0.6)
def story_launch(rng):
    """A ship lifting off: ignition, a swelling roar, rising, fading into the sky."""
    d = 3.2
    n = ns(d)
    roar = svf(noise(d, rng, 'pink'), points(n, [(0, 300), (0.8, 900), (d, 2400)], 'exp'), 0.6, 'bp')
    roar *= points(n, [(0, 0), (0.3, 0.7), (1.0, 1.0), (d, 0)])
    rumble = svf(noise(d, rng, 'brown'), 120, 0.8, 'lp') * points(n, [(0, 0), (0.4, 1.0), (1.6, 0.6), (d, 0)])
    whine = sine(points(n, [(0, 200), (d, 900)], 'exp'), n) * points(n, [(0, 0), (1.0, 0.08), (d, 0)])
    ign = thump(140, 60, 0.4, tau=0.03, decay=0.12, click=0.4, rng=rng) * 0.5
    return mix(roar, rumble, whine, ign)


@sound('story_collapse', -11, 'story', fade_out=0.6)
def story_collapse(rng):
    """The star of the centre collapsing: a rising shimmer that chokes, a deep implosion, a long
    hollow wind of light rushing inward."""
    d = 3.0
    n = ns(d)
    rise = mix(*[sine(points(n, [(0, f), (0.9, f * 1.8), (1.0, f * 0.3)], 'exp'), n) * a
                 for f, a in ((440, 0.3), (660, 0.2), (990, 0.12))])
    rise *= points(n, [(0, 0), (0.85, 1.0), (1.0, 0), (d, 0)])
    boom = thump(70, 28, 2.0, tau=0.08, decay=0.6, click=0.3, rng=rng, drive=2.0)
    suck = whoosh(1.8, rng, (3000, 300), q=1.0, peak_at=0.2, shape=1.2) * 0.6
    glitter = sparkle(1.2, rng, count=14, fmin=2500, fmax=7000, density=[1, 0.6, 0.2]) * 0.2
    return room(place([(0.0, rise), (0.95, boom), (0.98, suck), (1.0, glitter)]), rng, t60=2.0, wet=0.3)


@sound('story_bubbles', -17, 'story', fade_out=0.1)
def story_bubbles(rng):
    """A burst of bubbles rising (under water)."""
    parts = []
    for k in range(9):
        f = rng.uniform(400, 1200)
        parts.append((k * 0.05 + rng.uniform(0, 0.03), chirp(f, f * 2.4, 0.06, 'sine', decay=0.03) * rng.uniform(0.4, 0.9)))
    return filt(room(place(parts), rng, t60=0.6, wet=0.3), lp(3500))


@sound('story_glow', -19, 'story', fade_out=0.4)
def story_glow(rng):
    """A light coming on: a soft glassy ping that blooms (the Lantern, the arms, the names)."""
    return room(mix(chime(1320, 1.2, decay=0.5, bright=0.7), chime(1980, 1.0, decay=0.4, bright=0.5) * 0.4,
                    sparkle(0.5, rng, count=6, fmin=3000, fmax=8000) * 0.15), rng, t60=1.2, wet=0.3)


@sound('story_candle', -19, 'story', fade_out=0.15)
def story_candle(rng):
    """A candle lit: a scratch, a small flame catching with a soft whoomph."""
    scratch = svf(noise(0.12, rng, 'white'), 3500, 1.2, 'bp') * perc(ns(0.12), 0.002, 0.05) * 0.6
    catch = whoosh(0.35, rng, (500, 1400, 600), q=0.9, peak_at=0.2) * 0.8
    return place([(0.0, scratch), (0.1, catch), (0.15, crackle(0.4, rng, rate=30.0) * 0.15)])


@sound('story_bones', -16, 'story', fade_out=0.1)
def story_bones(rng):
    """Dry bones rattling."""
    parts = []
    for k in range(10):
        f = rng.uniform(900, 1800)
        clack = mix(bell(f, 0.06, ratio=1.7, index=1.2, decay=0.012), debris(0.02, rng, count=2, fmin=2000, fmax=5000) * 0.4)
        parts.append((k * 0.06 + rng.uniform(0, 0.03), clack * rng.uniform(0.5, 1.0)))
    return room(place(parts), rng, t60=0.6, wet=0.25)


@sound('story_soul', -16, 'story', fade_out=0.4)
def story_soul(rng):
    """A soul leaving: an airy rising whisper and a ghostly chord that fades into the crystal."""
    d = 1.6
    n = ns(d)
    whisper = whoosh(d, rng, (500, 2600, 1200), q=2.0, peak_at=0.6, shape=1.5) * 0.6
    chord = mix(*[sine(points(n, [(0, f), (d, f * 1.5)], 'exp'), n) * 0.12 for f in (330, 415, 495)])
    chord *= points(n, [(0, 0), (0.7, 1.0), (d, 0)])
    return room(mix(whisper, chord), rng, t60=1.5, wet=0.35)


@sound('story_steam', -15, 'story', fade_out=0.25)
def story_steam(rng):
    """A burst of steam from a valve."""
    d = 1.0
    n = ns(d)
    hiss = svf(noise(d, rng, 'white'), 4200, 0.9, 'bp') * points(n, [(0, 0), (0.03, 1.0), (0.5, 0.6), (d, 0)])
    return mix(hiss, svf(noise(d, rng, 'pink'), 900, 0.8, 'bp') * points(n, [(0, 0), (0.05, 0.4), (d, 0)]))


@sound('story_clang', -14, 'story', fade_out=0.3)
def story_clang(rng):
    """A hammer on hot metal: a heavy ringing clang."""
    hit = mix(bell(520, 1.2, ratio=2.76, index=3.5, decay=0.25), bell(1310, 0.8, ratio=1.41, index=2.0, decay=0.12) * 0.5,
              thump(180, 90, 0.15, tau=0.01, decay=0.05, click=0.8, rng=rng) * 0.5)
    return room(hit, rng, t60=1.0, wet=0.2)


@sound('story_ignite', -12, 'story', fade_out=0.5)
def story_ignite(rng):
    """Catching fire all at once: a deep whoomph swelling into a roaring blaze."""
    d = 2.2
    n = ns(d)
    whoomph = svf(noise(0.6, rng, 'brown'), 180, 0.8, 'lp') * perc(ns(0.6), 0.01, 0.2) * 1.3
    blaze = svf(noise(d, rng, 'pink'), points(n, [(0, 300), (0.4, 1600), (d, 700)], 'exp'), 0.6, 'bp')
    blaze *= points(n, [(0, 0), (0.25, 1.0), (1.4, 0.8), (d, 0)])
    return mix(whoomph, soft_clip(blaze * 1.2, 1.4), crackle(d, rng, rate=90.0) * 0.4)


@sound('story_sing', -18, 'story', fade_out=0.6)
def story_sing(rng):
    """A young star singing light: a bright rising arpeggio of bells over a soft choir."""
    notes = ['C6', 'E6', 'G6', 'C7', 'G6', 'E6']
    bells = arp(notes, 0.16, lambda f, i: chime(f, 0.9, decay=0.45, bright=0.6))
    d = 1.8
    n = ns(d)
    choir = mix(*[sine(hz(f) * np.ones(n), n) * 0.08 for f in ('C5', 'E5', 'G5')]) * points(n, [(0, 0), (0.6, 1.0), (d, 0)])
    return room(mix(pad_to(bells, n), choir), rng, t60=1.8, wet=0.35)


@sound('story_drain', -17, 'story', fade_out=0.3)
def story_drain(rng):
    """Machines drinking light: a pulsing electric suction, falling in pitch."""
    d = 1.6
    n = ns(d)
    t = np.arange(n) / SR
    buzz = saw(points(n, [(0, 220), (d, 110)], 'exp'), n) * (0.6 + 0.4 * np.sin(2 * np.pi * 7.0 * t))
    buzz = svf(buzz, 1400, 1.5, 'lp') * points(n, [(0, 0), (0.1, 1.0), (d, 0)])
    suck = whoosh(d, rng, (2600, 600), q=1.4, peak_at=0.3) * 0.35
    return mix(buzz * 0.5, suck)


# =========================================================================== 2.68: Asterion's film
@sound('story_heartbeat', -14, 'story', fade_out=0.25)
def story_heartbeat(rng):
    """The cosmic heart beating: a deep double thump (lub-dub) and a warm shimmer of light."""
    lub = thump(72, 36, 0.4, tau=0.05, decay=0.15, click=0.18, rng=rng, drive=1.4)
    dub = thump(64, 32, 0.34, tau=0.05, decay=0.12, click=0.12, rng=rng, drive=1.3) * 0.75
    shimmer = chime(392, 1.0, decay=0.55, bright=0.5) * 0.05
    return room(place([(0.0, lub), (0.24, dub), (0.0, shimmer)]), rng, t60=1.3, wet=0.25)


@sound('story_devour', -11, 'story', fade_out=0.7)
def story_devour(rng):
    """A world swallowed: a long suction, a crust crunching, a muffled boom, the rumble after."""
    suck = whoosh(1.3, rng, (180, 1500, 260), q=1.0, peak_at=0.8, color='pink') * 0.8
    crunch = debris(1.0, rng, count=46, fmin=250, fmax=3600, grain=(0.004, 0.03)) * 0.9
    g = 1.5
    m = ns(g)
    grind = svf(noise(g, rng, 'brown'), points(m, [(0, 90), (0.5, 210), (g, 60)], 'exp'), 2.0, 'bp')
    grind = grind * points(m, [(0, 0), (0.2, 1.0), (g, 0)])
    boom = thump(82, 28, 1.2, tau=0.06, decay=0.45, click=0.4, rng=rng, drive=1.6)
    return room(place([(0.0, suck), (1.05, crunch), (1.05, grind), (1.1, boom)]), rng, t60=1.8, wet=0.3)


@sound('story_reveal', -12, 'story', fade_out=0.6)
def story_reveal(rng):
    """A branch of the titan lights up: a deep strike, a ringing of dark metal, sparks of light."""
    hit = thump(110, 38, 0.9, tau=0.03, decay=0.32, click=0.5, rng=rng, drive=1.5)
    ring = mix(bell(220, 1.8, ratio=2.76, index=2.0, decay=0.8), bell(331, 1.8, ratio=3.1, index=1.5, decay=0.7) * 0.6) * 0.22
    shimmer = sparkle(1.4, rng, count=18, fmin=2000, fmax=7000) * 0.22
    return room(place([(0.0, hit), (0.0, ring), (0.05, shimmer)]), rng, t60=1.5, wet=0.3)


@sound('story_holo', -16, 'story', fade_out=0.2)
def story_holo(rng):
    """A hologram powering on: a rising sweep, a hum, digital blips."""
    sweep = zap(180, 1900, 0.6, q=4.0, shape='saw') * 0.35
    hum = tone(60, 1.3, 'saw', release=0.3, lowpass=420) * 0.12
    blips = place([(0.55 + 0.09 * k, blip(1200 + 300 * (k % 3), 0.05)) for k in range(6)]) * 0.25
    return room(mix(pad_to(sweep, ns(1.3)), hum, pad_to(blips, ns(1.3))), rng, t60=0.6, wet=0.2)


@sound('story_beacon', -16, 'story', fade_out=0.4)
def story_beacon(rng):
    """A beacon of the clans igniting: a strike, a flame catching and roaring, a bright call."""
    strike = whoosh(0.35, rng, (800, 3000, 1200), q=1.2, peak_at=0.2, color='white') * 0.5
    d = 1.3
    n = ns(d)
    fire = svf(noise(d, rng, 'pink'), points(n, [(0, 500), (0.2, 1500), (d, 700)], 'exp'), 0.7, 'bp')
    fire = fire * points(n, [(0, 0), (0.12, 0.8), (d, 0)])
    call = chime(660, 1.0, decay=0.5) * 0.14
    return room(place([(0.0, strike), (0.08, fire), (0.15, call)]), rng, t60=0.9, wet=0.25)


@sound('story_titan_roar', -10, 'story', fade_out=0.8)
def story_titan_roar(rng):
    """Asterion roaring: a growl of many throats rising, a rumble that shakes the void, stars ringing."""
    d = 3.6
    n = ns(d)
    f = points(n, [(0, 46), (0.35, 68), (1.6, 60), (d, 38)], 'exp')
    throats = mix(*[saw(f * r, n) * a for r, a in ((1.0, 0.5), (1.012, 0.45), (1.5, 0.25), (2.02, 0.18))])
    vowel = svf(throats, points(n, [(0, 300), (0.4, 900), (2.0, 600), (d, 250)], 'exp'), 1.6, 'bp')
    breath = svf(noise(d, rng, 'pink'), points(n, [(0, 400), (0.5, 1400), (d, 500)], 'exp'), 0.8, 'bp') * 0.5
    env = points(n, [(0, 0), (0.3, 1.0), (2.2, 0.8), (d, 0)])
    body = soft_clip((vowel + breath) * env * 1.3, 2.0)
    sub = thump(48, 26, 2.4, tau=0.1, decay=0.9, click=0.2, rng=rng, drive=1.4) * 0.8
    stars_ = sparkle(2.6, rng, count=24, fmin=3000, fmax=8000) * 0.12
    return room(mix(body, pad_to(sub, n), pad_to(place([(0.4, stars_)]), n)), rng, t60=2.2, wet=0.3)
