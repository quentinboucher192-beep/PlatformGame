"""Le Kraken des Abysses, « Le Kraken des Abysses » (2.53): a little octopus born in the black sea
under the crust of the Confins, with a single light at the tip of one arm; the lost fish follow it
to the warm springs (« la Lanterne »); the night the star of the centre collapses, its light falls
into the sea as a rain of sparks; it gathers them, grows and grows, its eight arms light up; so
big that the fish flee its shadow; the Kraken, alone in the trench, its lights still on."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blob, blur, fbm, finish, rgb, rim, smoothstep, tapered

# Its skin (boss_g_kraken.py: deep-sea purple, teal where the light catches it), paler and rounder
# when it is little.
BABY = {"body": "#7468b4", "top": "#a6a2dc", "belly": "#e0d8f4", "spots": "#7ae6ff"}
GROWN = {"body": "#43407e", "top": "#6a8ab8", "belly": "#b0a6d2", "spots": "#7ae6ff"}
SHADOW = {"body": "#1a1634", "top": "#2a3050", "belly": "#3a3460", "spots": "#7ae6ff"}
CYAN = "#7ae6ff"


# --------------------------------------------------------------------------- the black sea

def deep_sea(c, seed, top="#0c2240", mid="#06142c", low="#02060e", rays_x=480, rays_k=0.14, snow=260):
    """The sea under the crust: dark water, murky clouds, faint light falling from the cracks
    above, marine snow."""
    c.vgradient([(0.0, top), (0.45, mid), (1.0, low)])
    c.nebula(seed, "#0e3a5a", "#1c1a4a", 0.22, cell=260)
    if rays_k > 0:
        c.rays(rays_x, -80, 8, 640, 46, "#3a8ab0", rays_k, seed + 1, a0=62, spread=56, soft=14)
    c.stars(seed + 2, snow, "#a8d0e8", 0.4, big=0.0)


def crust(c, seed, y, crack="#ff6a3a", k=0.55):
    """The obsidian crust of the Confins seen from below: black, cracked with the fire above."""
    n = fbm(seed, 80, 4)
    edge = y + (n - 0.5) * 34
    m = smoothstep(edge + 1.5, edge - 1.5, c.yy)
    c.over(c.gradient_field(0, 0, 0, y + 20, "#140a18", "#06040a"), m)
    r = fbm(seed + 1, 46, 4)
    cracks = smoothstep(0.46, 0.5, r) * smoothstep(0.54, 0.5, r) * m
    c.add(crack, cracks, k)
    c.add(crack, blur(cracks, 4) * m, k * 0.6)
    return m


def seabed(c, y, seed, color="#03060c", top="#0e1c2c", rim_color="#3a8ab0", rim_k=0.3):
    n = fbm(seed, 120, 4)
    edge = y + (n - 0.5) * 40
    m = smoothstep(edge - 1.5, edge + 1.5, c.yy)
    c.over(c.gradient_field(0, y - 20, 0, 545, top, color), m)
    c.add(rim_color, rim(m, 0, 3, 1.2), rim_k)
    return m


def rocks(c, seed, pts, color="#0a121c", top="#1a2a3a", rim_color="#3a8ab0", rim_k=0.35):
    """Rough rocks (x, y, r, flatten) of the seabed."""
    m = Mask()
    for i, (x, y, r, sy) in enumerate(pts):
        m.poly(blob(x, y, r, seed + i, 14, 0.22, sy))
    mm = m.get(0.5)
    ys = [p[1] - p[2] for p in pts]
    c.over(c.gradient_field(0, min(ys), 0, max(p[1] + p[2] for p in pts), top, color), mm)
    c.add(rim_color, rim(mm, 0, 2.5, 0.9), rim_k)
    return mm


def bubbles(c, seed, n, region, color="#bfe8ff", k=0.8, size=(1.5, 4.0)):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    for _ in range(n):
        x, y, r = rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(*size)
        ring = np.clip(Mask().circle(x, y, r).get(0.4) - Mask().circle(x, y, max(0.5, r - 1.1)).get(0.4), 0, 1)
        c.add(color, ring, k)
        c.add("#ffffff", Mask().circle(x - r * 0.35, y - r * 0.35, max(0.6, r * 0.25)).get(0.3), k * 0.8)


def fish(c, x, y, s, color="#9fd8ff", flip=False, k=1.0, lit=0.0, light="#7ae6ff"):
    """A small fish, side view (facing right unless `flip`)."""
    f = -1.0 if flip else 1.0
    m = Mask().ellipse(x, y, 9 * s, 4.2 * s).poly([(x - f * 7 * s, y), (x - f * 15 * s, y - 5.5 * s), (x - f * 13 * s, y),
                                                   (x - f * 15 * s, y + 5.5 * s)])
    mm = m.get(0.4)
    c.over(c.gradient_field(0, y - 5 * s, 0, y + 5 * s, color, rgb(color) * 0.45), mm, k)
    if lit > 0:
        c.add(light, rim(mm, -f * 2.0, 0, 0.7), lit * k)
    c.over("#020408", Mask().circle(x + f * 5 * s, y - 1 * s, 1.2 * s).get(0.2), k)


def vents(c, seed, pts, k=1.0):
    """Warm springs of the seabed: chimneys glowing at their mouths, a shimmer rising."""
    m = Mask()
    for (x, y, h, w) in pts:
        m.poly([(x - w, y + 30), (x - w * 0.45, y - h), (x + w * 0.45, y - h), (x + w, y + 30)])
    mm = m.get(0.6)
    c.over(c.gradient_field(0, min(p[1] - p[2] for p in pts), 0, 545, "#2a1a1a", "#08060a"), mm)
    c.add("#ff8a4a", rim(mm, 0, 2, 0.8), 0.35 * k)
    for i, (x, y, h, w) in enumerate(pts):
        c.glow(x, y - h, w * 0.9, "#ff9a5a", 0.9 * k, tail=0.5)
        sh = Mask().poly(tapered(bezier((x, y - h), (x + 10, y - h - 60), (x - 14, y - h - 120), (x + 6, y - h - 190), 18),
                                 w * 0.8, w * 2.4)).get(8)
        c.add("#ff7a3a", sh, 0.12 * k)


# --------------------------------------------------------------------------- the octopus

def octopus(c, x, y, s, pal, lights=(), light_k=1.0, curl=0.6, spread=1.0, seed=1, arm_len=62.0, sad=0.0,
            rim_color=CYAN, rim_k=0.45, inner=0.0, reach=None, spots=6, look=(0.0, 0.0)):
    """A front-facing octopus: a round mantle over a head with two big eyes, eight arms fanned out
    below, their tips curling. (x, y) = the centre of its head; `lights`: the arms whose tip glows;
    `inner`: light from inside its body; `reach` = (arm, (tx, ty)): that arm reaches for a point;
    `sad` droops its eyelids. Returns the tips of its arms."""
    rng = np.random.default_rng(seed)
    body = Mask()
    arms = []
    for k in range(8):
        u = (k - 3.5) / 3.5
        bx, by = x + u * 17 * s, y + 9 * s
        a = math.pi / 2 - u * 1.1 * spread
        L = arm_len * s * (0.85 + 0.3 * rng.random())
        side = 1.0 if u >= 0 else -1.0
        cu = curl * side + rng.uniform(-0.3, 0.3)
        d = (math.cos(a), math.sin(a))
        nrm = (-d[1], d[0])
        p1 = (bx + d[0] * L * 0.35, by + d[1] * L * 0.35)
        p2 = (bx + d[0] * L * 0.72 + nrm[0] * cu * L * 0.3, by + d[1] * L * 0.72 + nrm[1] * cu * L * 0.3)
        p3 = (bx + d[0] * L + nrm[0] * cu * L * 0.55, by + d[1] * L + nrm[1] * cu * L * 0.55)
        if reach is not None and k == reach[0]:
            # it dips under the body, then sweeps up to the point (a supple arm, not a rod)
            tx, ty = reach[1]
            p3 = (tx, ty)
            p1 = (bx + (tx - bx) * 0.2, by + 30 * s)
            p2 = (bx + (tx - bx) * 0.8, max(ty, by) + 26 * s)
        pts = bezier((bx, by), p1, p2, p3, 22)
        # the tip curls back on itself
        ex, ey = pts[-1]
        dx, dy = ex - pts[-2][0], ey - pts[-2][1]
        dl = math.hypot(dx, dy) or 1.0
        dx, dy = dx / dl, dy / dl
        r = 4.5 * s
        turn = side * (1.0 if reach is None or k != reach[0] else 0.0)
        for j in range(1, 7):
            t = j * 0.5 * turn
            ca, sa = math.cos(t), math.sin(t)
            pts.append((ex + (dx * ca - dy * sa) * r * j * 0.35, ey + (dx * sa + dy * ca) * r * j * 0.35))
        arms.append(pts)
        body.poly(tapered(pts, 9.5 * s, 1.4 * s))
    body.ellipse(x, y - 24 * s, 25 * s, 30 * s)
    body.ellipse(x, y, 22 * s, 16 * s)
    bm = body.get(0.35)
    c.over("#04020a", np.clip(blur(bm, 1.2) * 1.8, 0, 1), 0.85)
    c.over(c.gradient_field(0, y - 54 * s, 0, y + arm_len * s, pal["top"], pal["body"]), bm)
    # the mantle's sheen and its spots of light
    sheen = Mask().ellipse(x - 9 * s, y - 36 * s, 9 * s, 13 * s).get(4 * s)
    c.add(pal["top"], sheen * bm, 0.35)
    for i in range(spots):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(0.15, 0.8)
        c.glow(x + math.cos(a) * d * 20 * s, y - 26 * s + math.sin(a) * d * 22 * s, max(0.8, 1.5 * s), pal["spots"], 0.7)
    # suckers on the arms
    sm = Mask()
    for pts in arms:
        n = len(pts)
        for i in range(5, n - 6, 3):
            w = 9.5 * s + (1.4 * s - 9.5 * s) * i / (n - 1)
            sm.circle(pts[i][0], pts[i][1] + w * 0.25, max(0.7, w * 0.22))
    c.over(pal["belly"], sm.get(0.3) * bm, 0.75)
    if inner > 0:
        core = np.exp(-(((c.xx - x) / (26 * s)) ** 2 + ((c.yy - (y - 18 * s)) / (30 * s)) ** 2))
        c.add(pal["spots"], core * bm, 0.55 * inner)
    c.add(rim_color, rim(bm, 0, 3, 1.0), rim_k)
    # eyes: big and round (looking at `look`), droopy lids when sad
    for side in (-1.0, 1.0):
        ex, ey = x + side * 9 * s, y - 2 * s
        eye = Mask().ellipse(ex, ey, 6 * s, 7 * s).get(0.3)
        c.over("#f2eeff", eye)
        px, py = ex + look[0] * 2.2 * s, ey + 1 * s + look[1] * 2.2 * s
        c.over("#120a24", Mask().ellipse(px, py, 3.2 * s, 4.2 * s).get(0.3))
        c.over("#ffffff", Mask().circle(px - 1.3 * s, py - 1.8 * s, 1.2 * s).get(0.2))
        if sad > 0:
            # the lid droops toward the outer corner of each eye (inside the eye only)
            outer, inner_y = ey - 9 * s + 9 * s * sad, ey - 9 * s + 4 * s * sad
            yr, yl = (outer, inner_y) if side > 0 else (inner_y, outer)
            lid = Mask().poly([(ex - 7.5 * s, ey - 9 * s), (ex + 7.5 * s, ey - 9 * s), (ex + 7.5 * s, yr),
                               (ex - 7.5 * s, yl)]).get(0.3)
            c.over(pal["top"], lid * eye, 0.9)
            c.over("#04020a", rim(lid * eye, 0, -1.5, 0.4), 0.8)
    for k in lights:
        tx, ty = arms[k][-7]
        c.glow(tx, ty, 3.0 * s, "#ffffff", 0.9 * light_k)
        c.glow(tx, ty, 9.0 * s, pal["spots"], 0.75 * light_k, tail=0.5)
    return [a[-7] for a in arms]


def sparks(c, seed, n, region, color="#c8b8ff", k=1.0, fall=(0.2, 1.0)):
    """Sparks of the fallen star sinking in the water: a soft trail above a bright head."""
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    trails = Mask()
    heads = []
    for _ in range(n):
        x, y = rng.uniform(x0, x1), rng.uniform(y0, y1)
        L = rng.uniform(16, 60) * rng.uniform(*fall)
        trails.poly(tapered([(x + rng.uniform(-6, 6), y - L), (x, y)], 0.3, rng.uniform(1.4, 2.8)), v=int(rng.uniform(110, 230)))
        heads.append((x, y, rng.uniform(1.4, 3.0)))
    tm = trails.get(0.8)
    c.add(color, tm, 0.8 * k)
    c.add(color, blur(tm, 3), 0.5 * k)
    for x, y, r in heads:
        c.glow(x, y, r, "#ffffff", 0.8 * k)
        c.glow(x, y, r * 3, color, 0.45 * k)


# --------------------------------------------------------------------------- the six shots

def shot1():
    """Under the crust of the Confins sleeps a black sea; a little octopus is born there, with a
    single light at the tip of one arm."""
    c = Canvas()
    deep_sea(c, 2101, rays_x=520)
    crust(c, 2103, 40, k=0.45)
    seabed(c, 452, 2105)
    rocks(c, 2107, [(210, 440, 70, 0.6), (300, 452, 46, 0.55), (760, 446, 60, 0.6), (840, 458, 40, 0.5)])
    # its clutch on the rock: pale eggs, most still asleep, one open
    rng = np.random.default_rng(2109)
    for i in range(16):
        ex, ey = 170 + rng.uniform(0, 110), 372 + rng.uniform(0, 30)
        e = Mask().ellipse(ex, ey, 6, 8).get(0.4)
        c.over("#d8d0ee", e, 0.55)
        c.glow(ex, ey + 1, 2.0, "#9fd8ff", 0.35)
    shell = Mask().poly([(440, 420), (446, 398), (456, 404), (464, 392), (474, 402), (484, 392), (494, 404), (504, 396),
                         (512, 420)]).get(0.4)
    c.over("#d8d0ee", shell, 0.7)
    tips = octopus(c, 480, 318, 0.95, BABY, lights=(6,), light_k=1.1, curl=0.7, spread=0.9, seed=2111, look=(0.3, -0.3))
    c.glow(tips[6][0], tips[6][1], 70, CYAN, 0.22, tail=0.6)
    bubbles(c, 2113, 9, (430, 200, 560, 300))
    return finish(c, bloom=0.55, vignette=0.5)


def shot2():
    """In the dark, the lost fish followed its light to the warm springs: « la Lanterne »."""
    c = Canvas()
    deep_sea(c, 2201, top="#0a1c38", rays_k=0.08)
    seabed(c, 470, 2203)
    vents(c, 2205, [(760, 470, 70, 22), (836, 478, 46, 16), (700, 484, 34, 12)])
    tips = octopus(c, 420, 230, 0.8, BABY, lights=(7,), light_k=1.2, curl=-0.3, spread=0.7, seed=2207, arm_len=70,
                   reach=(7, (545, 196)), look=(1.0, -0.2))
    lx, ly = tips[7]
    c.glow(lx, ly, 110, CYAN, 0.2, tail=0.6)
    # the school of fish behind it, lit by the lantern
    rng = np.random.default_rng(2209)
    for i in range(18):
        t = i / 17.0
        fx = 300 - t * 260 + rng.uniform(-24, 24)
        fy = 250 + math.sin(t * 5.0) * 40 + rng.uniform(-26, 26)
        fish(c, fx, fy, rng.uniform(0.7, 1.1), "#8ac8e8", k=1.0 - 0.5 * t, lit=0.8 * (1.0 - t))
    bubbles(c, 2211, 14, (680, 220, 860, 430), "#ffd8b0", 0.6)
    return finish(c, bloom=0.58, vignette=0.5)


def shot3():
    """The night the star of the centre collapsed, its light fell into the sea as a rain of sparks."""
    c = Canvas()
    deep_sea(c, 2301, top="#141a40", mid="#0a1030", rays_k=0.0)
    m = crust(c, 2303, 70, k=0.3)
    # the crust split open: a jagged crack, violet light pouring in
    rng = np.random.default_rng(2304)
    pts = [(300 + i * 24 + rng.uniform(-6, 6), 44 + math.sin(i * 1.3) * 10 + rng.uniform(-8, 8)) for i in range(15)]
    split = Mask()
    for i in range(len(pts) - 1):
        w = 2.0 + 7.0 * math.sin(math.pi * i / (len(pts) - 1))
        split.line([pts[i], pts[i + 1]], w)
    sm = split.get(1.0) * m
    c.add("#f4ecff", sm, 1.0)
    c.add("#b890ff", blur(sm, 6), 0.9)
    c.glow(480, 56, 80, "#c8a8ff", 0.5, tail=0.6)
    c.rays(480, 60, 12, 560, 34, "#b8a0ff", 0.22, 2305, a0=55, spread=70, soft=10)
    sparks(c, 2307, 60, (180, 110, 800, 470), "#c8b8ff")
    seabed(c, 474, 2309)
    # the little Lantern reaching up for one
    tips = octopus(c, 470, 362, 0.85, BABY, lights=(6,), curl=0.5, spread=1.2, seed=2311, look=(0.0, -1.0),
                   reach=(5, (520, 278)))
    c.glow(522, 274, 4, "#ffffff", 1.0)
    c.glow(522, 274, 16, "#c8b8ff", 0.7)
    return finish(c, bloom=0.62, vignette=0.5)


def shot4():
    """It gathered them one by one; it grew and grew, and its eight arms lit up."""
    c = Canvas()
    deep_sea(c, 2401, top="#0c1a3a", rays_k=0.06, snow=200)
    c.glow(480, 260, 300, "#3a6ab0", 0.25, tail=0.6)
    seabed(c, 486, 2403)
    tips = octopus(c, 480, 236, 2.1, GROWN, lights=range(8), light_k=1.0, curl=0.55, spread=1.05, seed=2405, arm_len=82,
                   inner=0.35, spots=16, look=(0.0, -0.4))
    # the last sparks spiralling into it
    for i in range(14):
        a = i * 0.9
        d = 210 + i * 9
        sx, sy = 480 + math.cos(a) * d, 200 + math.sin(a) * d * 0.55
        c.glow(sx, sy, 2.4, "#ffffff", 0.9)
        c.glow(sx, sy, 8, "#c8b8ff", 0.5)
    for (tx, ty) in tips:
        c.glow(tx, ty, 40, CYAN, 0.12, tail=0.6)
    return finish(c, bloom=0.62, vignette=0.52)


def shot5():
    """But it grew so big that the fish feared its shadow and fled into the cracks."""
    c = Canvas()
    deep_sea(c, 2501, top="#08142a", mid="#040c1c", low="#010308", rays_k=0.05, snow=180)
    seabed(c, 470, 2503, rim_k=0.2)
    # the cracks of the cliff on the left, where the fish hide
    cliff = Mask().poly([(-10, 60), (170, 90), (210, 200), (180, 300), (240, 420), (200, 545), (-10, 545)]).get(0.8)
    c.over(c.gradient_field(0, 60, 220, 0, "#0e1a28", "#040810"), cliff)
    c.add("#3a8ab0", rim(cliff, 2, 0, 0.8), 0.25)
    for (x0, y0, x1, y1) in ((130, 140, 196, 190), (150, 280, 214, 330), (120, 380, 200, 400)):
        c.over("#010204", Mask().poly(tapered([(x0, y0), (x1, y1)], 10, 2)).get(1.0))
    rng = np.random.default_rng(2505)
    for i in range(12):
        fish(c, 230 + rng.uniform(0, 160), 150 + rng.uniform(0, 260), rng.uniform(0.7, 1.0), "#6a9ab8", flip=True, k=0.85)
    # the Kraken, huge and dark, its eyes sad
    octopus(c, 650, 200, 3.6, SHADOW, lights=range(8), light_k=0.8, curl=0.5, spread=0.9, seed=2507, arm_len=78,
            sad=0.75, spots=20, rim_k=0.25, look=(-1.0, 0.2))
    return finish(c, bloom=0.6, vignette=0.55)


def shot6():
    """The Kraken still lights its lamps at the bottom of the trench, waiting to be followed."""
    c = Canvas()
    deep_sea(c, 2601, top="#081830", mid="#04102a", low="#01040c", rays_k=0.07, snow=240)
    # the walls of the trench
    left = Mask().poly([(-10, 0), (200, 0), (250, 160), (220, 300), (300, 545), (-10, 545)]).get(0.8)
    right = Mask().poly([(970, 0), (760, 0), (720, 180), (760, 320), (680, 545), (970, 545)]).get(0.8)
    for wall, lx in ((left, 2), (right, -2)):
        c.over(c.gradient_field(0, 0, 0, 545, "#0e1c2c", "#03060c"), wall)
        c.add("#3a8ab0", rim(wall, lx, 0, 0.9), 0.3)
    seabed(c, 500, 2603)
    tips = octopus(c, 480, 300, 2.4, GROWN, lights=range(8), light_k=1.1, curl=0.6, spread=1.0, seed=2605, arm_len=70,
                   inner=0.2, spots=18, sad=0.35, look=(0.0, -1.0))
    for (tx, ty) in tips:
        c.glow(tx, ty, 44, CYAN, 0.14, tail=0.6)
    # a trail of little lights rising toward the surface, a way to follow
    rng = np.random.default_rng(2607)
    for i in range(16):
        t = i / 15.0
        px = 480 + math.sin(t * 7.0) * 60 * (1.0 - t) + rng.uniform(-10, 10)
        py = 190 - t * 170
        c.glow(px, py, 2.0 + 1.5 * (1.0 - t), "#ffffff", 0.7 * (1.0 - 0.6 * t))
        c.glow(px, py, 8, CYAN, 0.35 * (1.0 - 0.6 * t))
    return finish(c, bloom=0.62, vignette=0.55)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
