"""Monolithe, « Le Golem Primordial »: a pebble in the primordial dust; the children of the
Confins carving a rune on it in a cave (« Protège-nous »); rune after rune it grows and stands
up; it watches over the cave's village for ten thousand years; the people leave for the stars;
it stays, alone, its rune still glowing."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blob, blur, fbm, finish, rgb, rim, smoothstep, tapered

AMBER = "#ffb347"
STONE = {"top": "#a8927a", "body": "#64543f", "dark": "#1c140e"}

# Runes: strokes in a unit box (x, y from -1 to 1), drawn glowing.
RUNES = [
    [[(-0.2, 1), (-0.2, -1)], [(-0.2, -0.5), (0.5, -1)], [(-0.2, 0.0), (0.5, -0.5)]],      # ᚠ
    [[(0, 1), (0, -1)], [(0, -0.2), (-0.6, -0.9)], [(0, -0.2), (0.6, -0.9)]],                # ᛉ
    [[(-0.5, 1), (-0.5, -1)], [(-0.5, -1), (0.5, -0.4)], [(0.5, -0.4), (0.5, 1)]],           # ᚢ
    [[(0, -1), (0.6, -0.2)], [(0.6, -0.2), (0, 0.6)], [(0, 0.6), (-0.6, -0.2)], [(-0.6, -0.2), (0, -1)],
     [(-0.6, 1), (0, 0.6)], [(0.6, 1), (0, 0.6)]],                                          # ᛟ
    [[(-0.4, 1), (-0.4, -1)], [(0.4, 1), (0.4, -1)], [(-0.4, -0.3), (0.4, 0.3)]],            # ᚺ
    [[(0, 1), (0, -1)], [(0, -1), (0.6, -0.5)], [(0.6, -0.5), (0, 0)], [(0, 0), (0.6, 0.5)]],  # ᛒ
]


def rune(c, x, y, size, kind, k=1.0, color=AMBER):
    m = Mask()
    for a, b in RUNES[kind % len(RUNES)]:
        m.line([(x + a[0] * size, y + a[1] * size), (x + b[0] * size, y + b[1] * size)], max(1.3, size * 0.22))
    rm = m.get(0.4)
    c.add(color, rm, 1.3 * k)
    c.add("#ffe8b0", rm, 0.5 * k)
    c.add(color, blur(rm, size * 0.5), 1.4 * k)
    c.glow(x, y, size * 1.6, color, 0.35 * k)


def boulder(c, x, y, r, seed, pal=STONE, light=(-1, -1), light_color="#ffd8a0", light_k=0.35, sy=0.85, rough=0.22, outline=True):
    m = Mask().poly(blob(x, y, r, seed, 16, rough, sy)).get(0.4)
    if outline:
        c.over("#0a0604", np.clip(blur(m, 1.2) * 1.8, 0, 1), 0.7)
    tex = fbm(seed, max(3.0, r * 0.35), 4)
    shade = c.gradient_field(x + light[0] * r, y + light[1] * r, x - light[0] * r, y - light[1] * r, pal["top"], pal["dark"])
    c.over(shade * (0.8 + 0.4 * tex[..., None]), m)
    c.add(light_color, rim(m, -light[0] * 2, -light[1] * 2, 0.8) * m, light_k)
    return m


def cave(c, seed, opening=None, wall="#2a1e18", deep="#0c0806", floor_y=440, warm=0.0):
    """A cave: rough walls darkening at the edges, an optional opening on the night outside."""
    n = fbm(seed, 120, 6)
    n2 = fbm(seed + 1, 30, 4)
    base = c.gradient_field(0, 0, 0, 540, wall, deep)
    c.img = base * (0.6 + 0.6 * n[..., None]) * (0.85 + 0.3 * n2[..., None])
    if opening is not None:
        ox, oy, orx, ory = opening
        hole = Mask().poly(blob(ox, oy, orx, seed + 2, 22, 0.22, ory / orx)).get(1.0)
        ring = np.clip(blur(hole, 9) * 2.2 - hole, 0, 1)
        c.over("#0c0806", ring, 0.85)
        sky = c.gradient_field(0, oy - ory, 0, oy + ory, "#0a1430", "#24345a")
        c.over(sky, hole)
        c.stars(seed + 3, 260, "#dfe8ff", 0.9, region=(ox - orx, oy - ory, ox + orx, oy + ory))
        # the night's blue light on its rim
        c.add("#8aa8e0", rim(hole, 4, 4, 2.5) * (1 - hole), 0.45)
        c.glow(ox, oy + ory, orx * 1.4, "#3a5a9a", 0.18)
    fl = smoothstep(floor_y - 6, floor_y + 6, c.yy + (fbm(seed + 5, 60, 3) - 0.5) * 16)
    c.over(c.gradient_field(0, floor_y, 0, 545, "#3a2c22", "#120c08"), fl)
    if warm > 0:
        c.glow(480, floor_y - 40, 380, "#ff9a4a", 0.18 * warm, tail=0.6)


def child(c, x, y, s, flip=False, pose="stand", tunic="#3a5a8a", hair="#2a1a10", skin="#e8b890", light=(1, 0), light_color="#ffc070", light_k=0.7):
    """A child of the Confins (side view): a round head, hair, a tunic, arms and legs.
    `pose`: stand, kneel, point, run, carry."""
    f = -1.0 if flip else 1.0
    P = lambda px, py: (x + px * s * f, y + py * s)
    body, skinm, hairm, tun = Mask(), Mask(), Mask(), Mask()
    if pose == "kneel":
        hip = (0, -10)
        tun.poly([P(-7, -10), P(-5, -30), P(5, -30), P(8, -10)])
        body.line([P(-2, -10), P(8, -2), P(-6, 0)], 4.2 * s)
        head = (2, -38)
    else:
        legs = {"stand": ((-3, 0), (3, 0)), "run": ((-9, -1), (7, -3)), "point": ((-3, 0), (3, 0)), "carry": ((-3, 0), (3, 0))}[pose]
        body.line([P(-1.5, -14), P(*legs[0])], 3.8 * s)
        body.line([P(1.5, -14), P(*legs[1])], 3.8 * s)
        tun.poly([P(-7, -12), P(-5, -32), P(5, -32), P(7, -12)])
        head = (0, -40)
    # arms
    ay = -28 if pose != "kneel" else -26
    if pose == "point":
        skinm.line([P(2, ay), P(12, ay - 12)], 3.0 * s)
        skinm.line([P(-2, ay), P(-6, ay + 10)], 3.0 * s)
    elif pose in ("kneel", "carry"):
        skinm.line([P(2, ay), P(12, ay + 6)], 3.0 * s)
        skinm.line([P(-1, ay), P(9, ay + 8)], 3.0 * s)
    else:
        skinm.line([P(2, ay), P(9, ay + 9)], 3.0 * s)
        skinm.line([P(-2, ay), P(-9, ay + 9)], 3.0 * s)
    hx, hy = P(*head)
    skinm.circle(hx, hy, 6.4 * s)
    hairm.poly([P(head[0] - 6.6, head[1] + 1), P(head[0] - 5, head[1] - 6), P(head[0] + 2, head[1] - 7.4),
                P(head[0] + 6.4, head[1] - 3), P(head[0] + 1, head[1] - 3), P(head[0] - 3, head[1] + 3)])
    bm, sm, hm, tm = body.get(0.3), skinm.get(0.3), hairm.get(0.3), tun.get(0.3)
    full = np.clip(bm + sm + hm + tm, 0, 1)
    c.over("#0a0604", np.clip(blur(full, 0.8) * 1.6, 0, 1), 0.8)
    c.over("#4a3426", bm)
    c.over(tunic, tm)
    c.over(skin, sm)
    c.over(hair, hm)
    c.add(light_color, rim(full, -light[0] * 1.6 * f, -light[1] * 1.6, 0.6) * full, light_k)
    ex, ey = P(head[0] + 3, head[1])
    c.over("#1a0e08", Mask().circle(ex, ey, 0.9 * s).get(0.2), 0.9)
    return P(12, ay + 6)


def golem(c, x, y, s, pose="stand", runes=6, rune_k=1.0, light=(-1, -1), light_color="#ffd8a0", light_k=0.3, seed=1, moss=True, eyes=1.0,
          pal=STONE):
    """The golem: boulders piled up into a body (head, shoulders, long arms, hips, legs), moss
    on top, glowing runes. `pose`: stand, rise (one knee, a hand on the ground), hunch (sitting)."""
    S = lambda px, py: (x + px * s, y + py * s)
    parts = []  # (px, py, r, sy)
    if pose == "stand":
        parts = [(-22, -14, 20, 0.9), (22, -14, 20, 0.9), (0, -48, 34, 0.8), (0, -96, 46, 0.85), (-50, -118, 26, 0.85), (50, -118, 26, 0.85),
                 (-64, -80, 20, 1.0), (64, -80, 20, 1.0), (-70, -44, 22, 1.0), (70, -44, 22, 1.0), (0, -150, 22, 0.85)]
    elif pose == "rise":
        parts = [(-34, -10, 20, 0.7), (26, -18, 22, 0.9), (-6, -46, 34, 0.8), (6, -92, 46, 0.85), (-42, -114, 26, 0.85), (56, -110, 26, 0.85),
                 (-66, -72, 20, 1.0), (72, -70, 20, 1.0), (-80, -30, 22, 1.0), (86, -20, 22, 0.9), (14, -146, 22, 0.85)]
    else:  # hunch: sitting, head low, arms on the knees
        parts = [(-34, -10, 24, 0.7), (34, -10, 24, 0.7), (0, -36, 36, 0.75), (0, -78, 46, 0.8), (-46, -96, 26, 0.85), (46, -96, 26, 0.85),
                 (-48, -50, 20, 1.0), (48, -50, 20, 1.0), (-30, -22, 18, 1.0), (30, -22, 18, 1.0), (0, -116, 21, 0.85)]
    allm = np.zeros_like(c.yy)
    for i, (px, py, r, sy) in enumerate(parts):
        cx, cy = S(px, py)
        m = boulder(c, cx, cy, r * s, seed + i, pal=pal, light=light, light_color=light_color, light_k=light_k, sy=sy)
        allm = np.maximum(allm, m)
    hx, hy = S(*parts[-1][:2])
    if moss:
        mm = Mask()
        for (px, py, r, sy) in (parts[-1], parts[4], parts[5]):
            cx, cy = S(px, py)
            mm.poly(blob(cx, cy - r * s * 0.55, r * s * 0.7, seed + 50 + int(px), 12, 0.3, 0.4))
        mo = mm.get(0.4) * allm
        c.over(c.gradient_field(0, hy - 30 * s, 0, hy + 10 * s, "#8ac070", "#3a6a30"), mo, 0.9)
    if eyes > 0:
        for dx in (-7, 7):
            c.glow(hx + dx * s, hy + 2 * s, 2.6 * s, AMBER, 0.9 * eyes)
            c.over("#fff0c0", Mask().circle(hx + dx * s, hy + 2 * s, 1.6 * s).get(0.2), 0.9 * eyes)
    rng = np.random.default_rng(seed + 99)
    spots = [(0, -96), (-18, -50), (18, -52), (-50, -118), (52, -116), (-68, -44), (68, -44), (0, -70)]
    for i in range(min(runes, len(spots))):
        px, py = spots[i]
        if pose != "stand":
            py = py * 0.85
        cx, cy = S(px + rng.uniform(-4, 4), py + rng.uniform(-4, 4))
        rune(c, cx, cy, (9 if i == 0 else 6) * s, i + seed, rune_k if i == 0 else rune_k * 0.85)
    return allm, (hx, hy)


def house(c, x, y, w, h, seed, lit=True, light=(-1, -1)):
    """A small stone house of the cave: walls, a round roof, a door, a warm window."""
    m = Mask()
    m.poly([(x - w / 2, y), (x - w / 2, y - h), (x + w / 2, y - h), (x + w / 2, y)])
    m.ellipse(x, y - h, w * 0.58, h * 0.45)
    hm = m.get(0.4)
    tex = fbm(seed, 6, 3)
    c.over(c.gradient_field(x, y - h * 1.4, x, y, "#7a6450", "#3a2c22") * (0.85 + 0.3 * tex[..., None]), hm)
    c.add("#ffc070", rim(hm, -light[0] * 1.5, -light[1] * 1.5, 0.6) * hm, 0.3)
    door = Mask().poly([(x - w * 0.12, y), (x - w * 0.12, y - h * 0.55), (x + w * 0.12, y - h * 0.55), (x + w * 0.12, y)]).get(0.3)
    win = Mask().ellipse(x + w * 0.26 * (1 if seed % 2 else -1), y - h * 0.7, w * 0.1, h * 0.12).get(0.3)
    if lit:
        c.over("#3a1e0c", door)
        c.add("#ffb050", door, 0.5)
        c.over("#ffd070", win)
        c.glow(x, y - h * 0.6, w * 0.8, "#ffa040", 0.35)
    else:
        c.over("#0a0806", door)
        c.over("#0a0806", win)


def ship(c, x, y, angle, s, seed):
    """A small ship leaving for the stars, its trail behind it."""
    ca, sa = math.cos(angle), math.sin(angle)
    trail = [(x - ca * (14 + i * 9) * s + math.sin(i * 0.5 + seed) * i * 0.8, y - sa * (14 + i * 9) * s) for i in range(16)]
    tm = Mask().poly(tapered(trail, 5 * s, 0.4)).get(1.4)
    c.add("#8ad0ff", tm, 0.7)
    c.add("#ffffff", blur(tm, 3) * 0.6, 0.5)
    pts = [(10, 0), (2, -4.5), (-8, -4), (-10, 0), (-8, 4), (2, 4.5)]
    body = Mask().poly([(x + (px * ca - py * sa) * s, y + (px * sa + py * ca) * s) for px, py in pts]).get(0.3)
    c.over("#c8d0e0", body)
    c.add("#ffffff", rim(body, 0, 1.2, 0.4) * body, 0.5)
    c.glow(x - ca * 10 * s, y - sa * 10 * s, 4 * s, "#bfe8ff", 1.0)


# --------------------------------------------------------------------------- the six shots

def shot1():
    """Before the planets there was only dust; and in that dust, a pebble."""
    c = Canvas()
    c.vgradient([(0.0, "#0c0806"), (0.5, "#1a120c"), (1.0, "#0a0604")])
    cx, cy = 480, 272
    dx, dy = c.xx - cx, (c.yy - cy) * 1.8
    rho = np.sqrt(dx * dx + dy * dy) + 1.0
    theta = np.arctan2(dy, dx)
    warp = fbm(2101, 80, 5)
    arms = 0.5 + 0.5 * np.sin(3.0 * theta - 3.2 * np.log(rho) + warp * 3.0)
    dust = arms ** 2.2 * np.exp(-rho / 340.0) * (0.4 + 0.9 * fbm(2103, 24, 5))
    c.add(c.gradient_field(cx - 300, cy, cx + 300, cy, "#c88a4a", "#e8c080") * 1.0, dust, 1.1)
    c.add("#6a7aa8", (1 - arms) ** 3 * np.exp(-rho / 500.0) * fbm(2105, 60, 4), 0.25)
    c.stars(2107, 260, "#ffe8c8", 0.6)
    c.glow(cx, cy, 120, "#ffb060", 0.35, tail=0.5)
    rng = np.random.default_rng(2109)
    for _ in range(60):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(40, 420)
        c.glow(cx + math.cos(a) * d, cy + math.sin(a) * d / 1.8, rng.uniform(0.8, 2.0), "#ffe0b0", rng.uniform(0.3, 0.8))
    # the pebble, alone in the middle of everything
    boulder(c, cx, cy, 22, 2111, light=(-0.8, -0.6), light_color="#ffe0a0", light_k=0.7, sy=0.8)
    c.glow(cx - 8, cy - 8, 6, "#ffe8c0", 0.3)
    return finish(c, bloom=0.5, vignette=0.55)


def shot2():
    """The first children of the Confins found it in a cave and carved a rune on it, for fun:
    « Protège-nous »."""
    c = Canvas()
    cave(c, 2201, opening=(770, 150, 150, 95), floor_y=430, warm=1.0)
    lx, ly = 566, 372  # the lantern
    c.glow(lx, ly, 160, "#ff9a40", 0.45, tail=0.5)
    m = boulder(c, 470, 404, 52, 2203, light=(1, -0.6), light_color="#ffc070", light_k=0.6, sy=0.78)
    rune(c, 466, 398, 16, 1, 1.2)
    child(c, 376, 432, 2.3, pose="kneel", tunic="#4a6a9a", light=(1, -0.3))
    tip = (446, 404)
    stick = Mask().line([(404, 372), tip], 2.6).get(0.4)
    c.over("#6a4a2a", stick)
    for i in range(6):
        c.glow(tip[0] + np.cos(i) * 6, tip[1] - abs(np.sin(i * 1.7)) * 8, 1.5, "#ffe0a0", 1.0)
    child(c, 530, 432, 2.0, flip=True, pose="point", tunic="#5a7a4a", hair="#c89050", light=(-0.4, -1))
    hand = child(c, 600, 432, 2.25, flip=True, pose="carry", tunic="#8a4a3a", hair="#5a2a14", light=(-1, -0.3))
    lan = Mask()
    lan.poly([(lx - 7, ly - 10), (lx + 7, ly - 10), (lx + 8, ly + 8), (lx - 8, ly + 8)])
    lm = lan.get(0.3)
    c.over("#ffd070", lm)
    c.glow(lx, ly, 14, "#ffe0a0", 1.0)
    c.over("#2a1a0c", Mask().line([(lx, ly - 10), (lx, ly - 20), (hand[0], hand[1])], 1.4).get(0.3))
    return finish(c, bloom=0.5, vignette=0.55)


def shot3():
    """Each generation added its own. Rune after rune, the pebble grew… and stood up."""
    c = Canvas()
    cave(c, 2301, floor_y=450, warm=0.8)
    allm, (hx, hy) = golem(c, 480, 452, 1.55, pose="rise", runes=8, rune_k=1.1, light=(0.2, -1), light_color="#ffc080", light_k=0.35, seed=2303)
    rng = np.random.default_rng(2305)
    # dust and pebbles falling off it as it rises
    falls = Mask()
    for i in range(26):
        x = rng.uniform(330, 630)
        y = rng.uniform(150, 420)
        falls.line([(x, y), (x + rng.uniform(-3, 3), y + rng.uniform(10, 30))], rng.uniform(0.8, 1.8))
    c.add("#d8b890", falls.get(0.8), 0.35)
    for i in range(10):
        boulder(c, rng.uniform(360, 610), rng.uniform(250, 440), rng.uniform(3, 6), 2310 + i, light=(0, -1), light_k=0.4)
    c.over("#2a1e14", smoothstep(0.6, 0.95, fbm(2307, 50, 4)) * smoothstep(440, 470, c.yy), 0.5)
    # the generations, small at its feet, looking up
    for i, (x, tun) in enumerate(((250, "#4a6a9a"), (300, "#8a4a3a"), (660, "#5a7a4a"), (712, "#7a5a8a"), (760, "#9a7a3a"))):
        child(c, x, 466, 1.25 + 0.25 * (i % 2), flip=x > 480, pose="point" if i in (1, 3) else "stand", tunic=tun, light=(0 if x < 480 else 0, -1), light_color="#ffb060")
    return finish(c, bloom=0.55, vignette=0.55)


def shot4():
    """It watched over the cave's village for ten thousand years; it knew every laugh."""
    c = Canvas()
    cave(c, 2401, floor_y=438, warm=1.4)
    rng = np.random.default_rng(2403)
    # houses on the floor and on the ledges carved in the walls
    for (x, y, w, h) in ((120, 300, 56, 40), (196, 296, 48, 34), (90, 438, 70, 56), (190, 446, 78, 64), (300, 440, 64, 50),
                         (420, 444, 70, 54), (540, 210, 50, 36), (610, 214, 44, 32), (530, 440, 58, 46)):
        house(c, x, y, w, h, int(x + y))
    ledge = Mask()
    ledge.poly([(40, 300), (250, 296), (256, 312), (40, 318)])
    ledge.poly([(500, 210), (660, 214), (664, 228), (500, 226)])
    c.over("#2a2018", ledge.get(0.5))
    # strings of lanterns across the cave
    for (a, b, sag) in (((60, 200), (520, 160), 70), ((300, 250), (900, 200), 60)):
        pts = bezier(a, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + sag), b, None, 30)
        c.over("#1a120c", Mask().line(pts, 1.2).get(0.3), 0.8)
        for i in range(2, 30, 3):
            c.glow(pts[i][0], pts[i][1] + 4, 3.2, ["#ffd070", "#ff9a5a", "#ffe0a0"][i % 3], 1.0)
    # its fire and the smoke going up
    c.glow(330, 452, 40, "#ff8a3a", 0.8)
    # the golem standing guard on the right, children playing at its feet
    golem(c, 760, 442, 1.25, pose="stand", runes=5, rune_k=0.7, light=(-1, -0.6), light_color="#ffc070", light_k=0.45, seed=2405)
    child(c, 640, 452, 1.35, pose="run", tunic="#4a6a9a", light=(-1, 0))
    child(c, 686, 452, 1.3, flip=True, pose="point", tunic="#9a4a3a", hair="#5a2a14", light=(-1, 0))
    child(c, 574, 452, 1.2, pose="run", tunic="#5a7a4a", hair="#c89050", light=(1, 0))
    for _ in range(30):
        c.glow(rng.uniform(80, 900), rng.uniform(120, 420), rng.uniform(1.0, 2.0), "#ffd070", rng.uniform(0.3, 0.8))
    return finish(c, bloom=0.55, vignette=0.5)


def shot5():
    """Then the people left for the stars. No one thought of erasing the rune."""
    c = Canvas()
    c.vgradient([(0.0, "#050a1c"), (0.55, "#14204a"), (0.8, "#3a2a4a"), (1.0, "#1a1018")])
    c.nebula(2501, "#2a4a8a", "#6a3a8a", 0.4, cell=260)
    c.stars(2503, 1100, "#e8f0ff", 1.0, region=(0, 0, 960, 430))
    # the ships rising on curved trails
    rng = np.random.default_rng(2505)
    for i, (x0, y0, x1, y1) in enumerate(((420, 420, 560, 90), (470, 430, 700, 130), (380, 410, 470, 70), (520, 430, 820, 220),
                                          (440, 420, 620, 40), (360, 420, 300, 120))):
        path = bezier((x0, y0), (x0 + (x1 - x0) * 0.2, y0 - 160), (x1, y1), None, 30)
        tm = Mask().poly(tapered(path, 0.6, 3.2)).get(1.6)
        c.add("#7ab8ff", tm, 0.45)
        a = math.atan2(path[-1][1] - path[-3][1], path[-1][0] - path[-3][0])
        ship(c, x1, y1, a, 1.3 + rng.random() * 0.4, 2510 + i)
    # the horizon of the Confins, and the cliff with the cave's mouth on the left
    hz = Mask().poly([(0, 440)] + [(x, 430 - 14 * math.sin(x * 0.013) - 10 * math.sin(x * 0.031)) for x in range(0, 961, 20)] + [(960, 545), (0, 545)]).get(0.6)
    c.over(c.gradient_field(0, 400, 0, 545, "#1e1424", "#080408"), hz)
    c.add("#8a9ad8", rim(hz, 0, 3, 1.0), 0.35)
    cliff = Mask().poly([(0, 545), (0, 160), (90, 150), (170, 200), (230, 260), (260, 340), (300, 420), (320, 545)]).get(0.6)
    c.over(c.gradient_field(0, 150, 0, 545, "#2a2028", "#0a0608"), cliff)
    c.add("#8a9ad8", rim(cliff, -3, 2, 1.0), 0.4)
    mouth = Mask().poly(blob(150, 400, 70, 2507, 18, 0.12, 1.3)).get(1.0) * cliff
    c.over("#050304", mouth)
    c.glow(150, 420, 60, "#ff9a40", 0.25)
    # the golem in the mouth of its cave, watching them go, its rune glowing
    golem(c, 150, 468, 0.62, pose="stand", runes=1, rune_k=1.2, light=(1, -1), light_color="#9ab0ff", light_k=0.4, seed=2509, eyes=0.8)
    return finish(c, bloom=0.55, vignette=0.5)


def shot6():
    """The Monolithe still guards the heart of the Confins; it knows no one any more: every
    visitor is a stranger."""
    c = Canvas()
    cave(c, 2601, wall="#151a22", deep="#040508", floor_y=450)
    # the abandoned village, dark
    for (x, y, w, h) in ((110, 452, 70, 56), (220, 458, 78, 60), (750, 456, 74, 58), (860, 450, 64, 50)):
        house(c, x, y, w, h, int(x + y) + 1, lit=False)
    # a thin beam from a crack in the ceiling, dust floating in it
    beam = Mask().poly([(440, 60), (462, 60), (560, 470), (420, 470)]).get(10)
    c.add("#9ab0d8", beam, 0.18)
    rng = np.random.default_rng(2603)
    for _ in range(40):
        y = rng.uniform(80, 460)
        t = (y - 60) / 410
        x = 451 + (490 - 451) * t + rng.uniform(-60, 60) * t
        c.glow(x, y, rng.uniform(0.8, 1.6), "#dfe8ff", rng.uniform(0.3, 0.8))
    # the golem, sitting alone, one rune glowing on its chest
    golem(c, 480, 456, 1.45, pose="hunch", runes=0, light=(-0.3, -1), light_color="#9ab0d8", light_k=0.45, seed=2605, eyes=0.7,
          pal={"top": "#5a5450", "body": "#38302a", "dark": "#0e0a08"})
    rune(c, 480, 456 - 78 * 1.45 * 0.85 + 6, 13, 1, 1.4)
    c.glow(480, 360, 120, AMBER, 0.15)
    return finish(c, bloom=0.5, vignette=0.6)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
