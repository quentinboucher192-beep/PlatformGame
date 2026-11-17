"""Le Titan Mécanique, « Le Dernier Quart » (2.58): under the Confins roared a foundry where a
thousand workers cast the steel of the system's ships; for the heaviest work they built a colossus of
steel and brass, the Titan; the evening the star of the centre collapsed, the foreman left it the
foundry: keep the fires lit until they came back; nobody came back: alone, century after century,
the Titan kept the fires lit while the rock swallowed the foundry; rust gnawed its plates, its
pistons wore out, its boiler overheated, it never stopped; the Titan Mécanique still works: to it,
every visitor is a thief come to put out its fires."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blur, fbm, finish, rgb, rim, smoothstep, tapered
from story_monolithe import ship
from story_pyrrhax import collapsed_star

FIRE = "#ff8a2e"      # the furnaces, molten steel
FIRE_HOT = "#ffe6b0"
AMBER = "#ffc23c"     # the Titan's light (its first phase)
STEEL = {"hi": "#b6c2ce", "mid": "#66717f", "dark": "#2c323b"}
BRASS = {"hi": "#f4dc98", "mid": "#98773a", "dark": "#3c2a14"}
RUST = "#8a4a22"


# --------------------------------------------------------------------------- the foundry

def foundry_hall(c, seed, floor_y=452, glow=FIRE, k=1.0, columns=(90, 330, 630, 870), beams=True, sky=None):
    """The hall of the foundry: brick walls blackened by soot, steel columns with rivets, trusses
    under the roof, chains hanging; the floor below; the glow of the furnaces from below."""
    n = fbm(seed, 90, 5)
    n2 = fbm(seed + 1, 26, 3)
    base = c.gradient_field(0, 0, 0, 540, "#2a1c16", "#0a0605")
    c.img = base * (0.55 + 0.5 * n[..., None]) * (0.85 + 0.3 * n2[..., None])
    # courses of bricks
    bricks = Mask()
    for y in range(20, floor_y, 18):
        bricks.line([(0, y), (960, y)], 1.0, 120)
        off = 0 if (y // 18) % 2 else 22
        for x in range(off, 960, 44):
            bricks.line([(x, y), (x, y + 18)], 1.0, 90)
    c.multiply("#2a1810", bricks.get(0.5), 0.6)
    if sky is not None:
        sky(c)
    c.add(glow, smoothstep(140, floor_y, c.yy), 0.16 * k)
    for x in columns:
        col = Mask().poly([(x - 16, 0), (x + 16, 0), (x + 16, floor_y + 4), (x - 16, floor_y + 4)]).get(0.6)
        c.over(c.gradient_field(x - 16, 0, x + 16, 0, STEEL["mid"], STEEL["dark"]), col)
        c.add(glow, rim(col, 3, 0, 1.0) * col, 0.25 * k)
        fl = Mask().poly([(x - 20, 0), (x + 20, 0), (x + 20, floor_y + 4), (x - 20, floor_y + 4)]).get(0.6) - col
        c.over(STEEL["dark"], np.clip(fl, 0, 1), 0.9)
        rv = Mask()
        for y in range(30, floor_y, 34):
            rv.circle(x - 10, y, 1.8).circle(x + 10, y, 1.8)
        c.over(STEEL["hi"], rv.get(0.4), 0.7)
    if beams:
        for y in (60, 120):
            bm = Mask().poly([(0, y - 9), (960, y - 9), (960, y + 9), (0, y + 9)]).get(0.6)
            c.over(c.gradient_field(0, y - 9, 0, y + 9, STEEL["mid"], STEEL["dark"]), bm)
        truss = Mask()
        for x in range(0, 960, 60):
            truss.line([(x, 69), (x + 60, 111)], 2.4).line([(x + 60, 69), (x, 111)], 2.4)
        c.over(STEEL["dark"], truss.get(0.6), 0.95)
        ch = Mask()
        for x in (200, 470, 760):
            for y in range(129, 230, 7):
                ch.ellipse(x, y, 2.2, 3.4)
            ch.line([(x - 10, 236), (x + 10, 236)], 3.0)
        c.over("#4a4038", ch.get(0.6), 0.9)
    fl = smoothstep(floor_y - 3, floor_y + 3, c.yy + (fbm(seed + 5, 50, 3) - 0.5) * 4)
    c.over(c.gradient_field(0, floor_y, 0, 545, "#3a2a20", "#0c0806"), fl)
    c.add(glow, smoothstep(floor_y + 3, floor_y - 2, c.yy) * smoothstep(floor_y - 7, floor_y, c.yy), 0.3 * k)


def furnace(c, x, y, w, h, k=1.0):
    """A furnace in the wall: an arch of firebrick round a mouth of white-hot fire."""
    arch = Mask().poly([(x - w / 2 - 14, y), (x - w / 2 - 14, y - h), (x + w / 2 + 14, y - h), (x + w / 2 + 14, y)])
    arch.ellipse(x, y - h, w / 2 + 14, w / 2 + 10)
    am = arch.get(0.8)
    c.over(c.gradient_field(x, y - h - w / 2, x, y, "#5a2e1c", "#2a140c"), am)
    mouth = Mask().poly([(x - w / 2, y), (x - w / 2, y - h * 0.8), (x + w / 2, y - h * 0.8), (x + w / 2, y)])
    mouth.ellipse(x, y - h * 0.8, w / 2, w / 2 * 0.8)
    mm = mouth.get(0.8)
    c.over(c.gradient_field(x, y - h, x, y, FIRE_HOT, FIRE), mm)
    c.glow(x, y - h * 0.4, w * 0.6, FIRE, 0.9 * k, tail=0.7)
    c.add(FIRE, rim(am, 0, -2, 1.0) * am, 0.4 * k)


def molten(c, path, w, k=1.0):
    """A stream (or a channel) of molten steel along `path`."""
    m = Mask().poly(tapered(path, w, w * 0.8)).get(1.0)
    c.over(FIRE, m)
    c.add(FIRE_HOT, blur(m, 1.0) * m, 0.8 * k)
    for (px, py) in path[::4]:
        c.glow(px, py, w * 1.6, FIRE, 0.25 * k)


def crucible(c, x, y, s, tilt=0.5, k=1.0):
    """A ladle of molten steel hanging from its hook, tilted to pour; returns its lip."""
    ca, sa = math.cos(tilt), math.sin(tilt)

    def R(px, py):
        return (x + (px * ca - py * sa) * s, y + (px * sa + py * ca) * s)
    body = Mask().poly([R(-22, -20), R(22, -20), R(16, 18), R(-16, 18)]).get(0.6)
    c.over(c.gradient_field(x - 20 * s, y, x + 20 * s, y, "#5a4a40", "#1c1612"), body)
    c.add(FIRE, rim(body, -2, -2, 0.8) * body, 0.3 * k)
    top = Mask().poly([R(-22, -20), R(22, -20), R(22, -15), R(-22, -15)]).get(0.6)
    c.over(FIRE_HOT, top, 0.9)
    hook = Mask().line([R(-24, -18), (x, y - 50 * s), R(24, -18)], 2.0).line([(x, y - 50 * s), (x, y - 200 * s)], 3.0).get(0.5)
    c.over("#3a322c", hook)
    lip = R(24, -18)
    c.glow(lip[0], lip[1], 10 * s, FIRE, 0.6 * k)
    return lip


def worker(c, x, y, s, pose="stand", light=FIRE, k=0.6, flip=False):
    """A foundry worker: a helmet, a leather apron, heavy boots; dark against the fire."""
    d = -1 if flip else 1

    def P(px, py):
        return (x + d * px * s, y + py * s)
    m = Mask()
    m.poly([P(-4, 0), P(-2, -16), P(2, -16), P(4, 0)])            # legs
    m.poly([P(-5, -15), P(-6, -30), P(6, -30), P(5, -15)])        # apron, torso
    m.ellipse(*P(0, -34), 4.2 * s, 4.0 * s)                       # head
    m.ellipse(*P(0, -36), 5.4 * s, 2.6 * s)                       # helmet brim
    if pose == "point":
        m.line([P(5, -28), P(16, -38)], 2.4 * s)
        m.line([P(-5, -28), P(-8, -18)], 2.4 * s)
    elif pose == "carry":
        m.line([P(5, -28), P(10, -20), P(14, -24)], 2.4 * s)
        m.line([P(-5, -28), P(-2, -19), P(12, -26)], 2.4 * s)
        m.line([P(10, -26), P(30, -30)], 1.6 * s)                 # a bar on his shoulder
    elif pose == "wave":
        m.line([P(5, -28), P(9, -44)], 2.4 * s)
        m.line([P(-5, -28), P(-7, -18)], 2.4 * s)
    else:
        m.line([P(5, -28), P(7, -18)], 2.4 * s)
        m.line([P(-5, -28), P(-7, -18)], 2.4 * s)
    wm = m.get(0.5)
    c.over("#120c0a", wm)
    c.add(light, rim(wm, d * 1.5, -1, 0.6) * wm, k)
    c.glow(*P(0, -36), 1.2 * s, "#ffe0a0", 0.3)  # the lamp of his helmet


def sparks(c, seed, n, x, y, spread, color=FIRE_HOT, k=1.0, up=60):
    rng = np.random.default_rng(seed)
    m = Mask()
    for _ in range(n):
        a = rng.uniform(-math.pi, 0)
        L = rng.uniform(0.3, 1.0) * spread
        px, py = x + math.cos(a) * L, y + math.sin(a) * L * up / max(1, spread) + rng.uniform(-4, 4)
        m.line([(px, py), (px - math.cos(a) * 3, py - math.sin(a) * 3)], 0.9, int(rng.uniform(120, 255)))
    c.add(color, m.get(0.5), 1.4 * k)


def smoke(c, seed, x, y, h, w=40, color="#7a706a", k=0.5):
    rng = np.random.default_rng(seed)
    m = Mask()
    for i in range(10):
        t = i / 9
        m.circle(x + math.sin(t * 5 + rng.random()) * w * 0.4 * t, y - h * t, w * (0.25 + 0.5 * t), v=int(200 * (1 - t * 0.7)))
    c.over(color, m.get(6.0), k)


# --------------------------------------------------------------------------- the Titan

def titan(c, x, y, s, light=(-0.6, -0.8), glow=AMBER, k=1.0, rust=0.0, visor=1.0, reactor=0.8, fire=FIRE, fist_up=False, scaffold=False,
          flip=False, seed=1):
    """The Titan, seen from three quarters: two heavy legs, a riveted hull, a dome head with one
    visor slit, a piston fist (near arm), a rotary cannon (far arm), two stacks, a reactor glowing
    behind its chest grille. (x, y): between its feet; `s`: its scale (1 = 100 px from its feet to
    its head)."""
    d = -1 if flip else 1

    def P(px, py):
        return (x + d * px * s, y - py * s)

    def part(pts, colors, ell=None, soft=0.6):
        m = Mask().poly([P(*p) for p in pts])
        if ell:
            m.ellipse(*ell)
        mm = m.get(soft)
        cx = sum(p[0] for p in pts) / len(pts)
        cy = sum(p[1] for p in pts) / len(pts)
        g = c.gradient_field(*P(cx + light[0] * 18, cy - light[1] * 18), *P(cx - light[0] * 18, cy + light[1] * 18), colors[0], colors[2])
        c.over(g, mm)
        c.add(fire, rim(mm, d * light[0] * -2, light[1] * -2, 0.8) * mm, 0.3 * k)
        return mm
    # the cannon arm behind (far), the stacks
    part([(-8, 70), (2, 74), (58, 64), (60, 56), (4, 60)], [STEEL["mid"], STEEL["mid"], STEEL["dark"]])
    for bx in range(0, 6):
        c.over(STEEL["dark"], Mask().line([P(30, 62 + bx * 1.2), P(62, 61 + bx * 1.0)], 0.8 * s).get(0.4), 0.6)
    c.glow(*P(62, 60), 4 * s, glow, 0.5 * k)
    for (sx, h) in ((-18, 108), (-28, 102)):
        part([(sx - 4, 82), (sx + 4, 82), (sx + 3, h), (sx - 3, h)], [STEEL["mid"], STEEL["mid"], STEEL["dark"]])
    # legs
    for (lx, front) in ((-12, False), (12, True)):
        cols = [STEEL["hi"], STEEL["mid"], STEEL["dark"]] if front else [STEEL["mid"], STEEL["dark"], STEEL["dark"]]
        part([(lx - 9, 0), (lx + 11, 0), (lx + 9, 18), (lx + 7, 36), (lx - 7, 36), (lx - 9, 18)], cols)
        part([(lx - 12, 0), (lx + 14, 0), (lx + 12, 6), (lx - 10, 6)], [BRASS["hi"], BRASS["mid"], BRASS["dark"]])
        c.over(BRASS["mid"], Mask().circle(*P(lx, 19), 4.2 * s).get(0.5))
    # hull
    hull = part([(-22, 36), (24, 36), (30, 58), (24, 80), (6, 88), (-14, 86), (-26, 70)], [STEEL["hi"], STEEL["mid"], STEEL["dark"]])
    # its rust (later shots): blotches of rust and streaks
    if rust > 0:
        rn = fbm(seed, 18, 3)
        c.over(RUST, hull * np.clip((rn - (0.75 - 0.35 * rust)) * 4.0, 0, 1), 0.85)
        c.over("#3a1c0c", hull * np.clip((fbm(seed + 3, 9, 2) - 0.8) * 5.0, 0, 1) * rust, 0.6)
    # reactor grille
    grille = Mask().ellipse(*P(10, 64), 9 * s, 10 * s).get(0.6)
    c.over("#140c08", grille)
    c.glow(*P(10, 64), 8 * s, glow, reactor * k, tail=0.5)
    gl = Mask()
    for i in range(-3, 4):
        gl.line([P(10 + i * 2.4, 56), P(10 + i * 2.4, 72)], 0.7 * s)
    c.over(STEEL["dark"], gl.get(0.4) * grille, 0.9)
    # rivets
    rv = Mask()
    for (rx, ry) in ((-18, 44), (-6, 42), (8, 42), (20, 44), (-20, 64), (24, 56), (-10, 80), (6, 84)):
        rv.circle(*P(rx, ry), 1.0 * s)
    c.over(STEEL["hi"], rv.get(0.3), 0.8)
    # head
    part([(-10, 86), (14, 86), (12, 96), (2, 100), (-8, 96)], [STEEL["hi"], STEEL["mid"], STEEL["dark"]], ell=(*P(2, 92), 12 * s, 9 * s))
    vis = Mask().poly([P(-2, 92), P(13, 92), P(13, 95), P(-2, 95)]).get(0.4)
    c.over("#1a0a04", vis)
    c.add(glow, vis, 1.2 * visor * k)
    c.glow(*P(8, 93.5), 6 * s, glow, 0.6 * visor * k, tail=0.5)
    # piston fist (near arm)
    if fist_up:
        arm = [(-20, 78), (-30, 92), (-24, 112), (-14, 110), (-18, 92), (-10, 80)]
        fist = (-20, 116)
    else:
        arm = [(-20, 78), (-34, 64), (-36, 40), (-26, 40), (-24, 60), (-12, 74)]
        fist = (-31, 34)
    part(arm, [STEEL["hi"], STEEL["mid"], STEEL["dark"]])
    part([(fist[0] - 10, fist[1] - 9), (fist[0] + 10, fist[1] - 9), (fist[0] + 11, fist[1] + 9), (fist[0] - 11, fist[1] + 9)],
         [STEEL["hi"], STEEL["mid"], STEEL["dark"]])
    kn = Mask()
    for i in range(4):
        kn.circle(*P(fist[0] - 7 + i * 4.6, fist[1] + (8 if not fist_up else -8)), 1.8 * s)
    c.over(BRASS["hi"], kn.get(0.3), 0.9)
    # pauldron
    part([(-30, 82), (-14, 92), (2, 86), (-4, 74), (-24, 72)], [BRASS["hi"], BRASS["mid"], BRASS["dark"]])
    if scaffold:
        sc = Mask()
        for sx in (-60, 60):
            sc.line([P(sx, 0), P(sx, 130)], 2.0)
        for sy in (30, 60, 90, 120):
            sc.line([P(-64, sy), P(64, sy)], 2.0)
            sc.line([P(-60, sy - 30), P(-20, sy)], 1.2).line([P(60, sy - 30), P(20, sy)], 1.2)
        c.over("#2a221c", sc.get(0.5), 0.95)
    return P(10, 64), P(8, 93.5), P(-28, 108)


# --------------------------------------------------------------------------- the shots

def shot1():
    """Under the Confins roared a foundry. A thousand workers cast the steel of the system's ships."""
    c = Canvas()
    foundry_hall(c, 5801, floor_y=456)
    furnace(c, 820, 456, 90, 120, 1.0)
    furnace(c, 140, 456, 70, 96, 0.8)
    lip = crucible(c, 460, 220, 2.0, tilt=0.55)
    stream = [(lip[0] + i * 1.2, lip[1] + i * 9) for i in range(0, 24)]
    molten(c, stream, 6, 1.0)
    molten(c, [(lip[0] + 26, 452 - abs(i - 20) * 0.1) for i in range(0, 1)] + [(x, 452) for x in range(int(lip[0] + 20), 760, 12)], 7, 0.9)
    sparks(c, 5803, 120, lip[0] + 28, 452, 90, k=1.0)
    # the hull of a ship being built on the right, on its cradle
    hullm = Mask().poly([(560, 330), (700, 300), (760, 330), (720, 360), (580, 362)]).get(0.6)
    c.over(c.gradient_field(560, 300, 560, 362, "#7a8090", "#2a2c34"), hullm)
    c.add(FIRE, rim(hullm, 0, 3, 1.0) * hullm, 0.4)
    for (wx, ws, pose, fl) in ((240, 2.2, "carry", False), (300, 2.0, "stand", True), (600, 2.1, "point", True), (690, 1.8, "stand", False),
                               (380, 1.7, "carry", False), (880, 1.6, "stand", True)):
        worker(c, wx, 456, ws, pose, flip=fl)
    smoke(c, 5805, 820, 300, 200, 60, "#5a4a40", 0.35)
    return finish(c, bloom=0.6, vignette=0.55)


def shot2():
    """For the heaviest work they built a colossus of steel and brass: the Titan."""
    c = Canvas()
    foundry_hall(c, 5811, floor_y=462, columns=(70, 890), k=0.8)
    c.glow(480, 300, 280, "#5a3a20", 0.3, tail=0.7)
    titan(c, 480, 462, 3.0, glow="#3a2a1a", visor=0.15, reactor=0.15, scaffold=True, k=0.8)
    # welders on the scaffolding, their arcs
    for (wx, wy, fl) in ((328, 372, False), (630, 282, True), (340, 192, False), (612, 102, True)):
        worker(c, wx, wy, 1.6, "point", light="#bfe0ff", k=0.8, flip=fl)
        ax, ay = wx + (-1 if fl else 1) * 26, wy - 60
        c.glow(ax, ay, 4, "#e8f4ff", 1.6)
        c.glow(ax, ay, 26, "#7ab8ff", 0.35)
        sparks(c, 5813 + wx, 30, ax, ay + 8, 40, "#d8eeff", 0.8)
    for (wx, pose, fl) in ((150, "carry", False), (230, "stand", True), (760, "point", True), (830, "stand", False)):
        worker(c, wx, 462, 2.0, pose, flip=fl)
    return finish(c, bloom=0.6, vignette=0.55)


def _sky_star(c):
    # the evening sky through the great open hatch of the roof: the star of the centre collapses
    hatch = Mask().poly([(260, 0), (700, 0), (660, 150), (300, 150)]).get(0.8)
    sky = Canvas()
    sky.vgradient([(0.0, "#05040e"), (0.3, "#1a0a26"), (0.6, "#3a1428"), (1.0, "#0a0408")])
    sky.stars(5821, 300, "#e8e0ff", 0.8, region=(260, 0, 700, 150))
    collapsed_star(sky, 480, 60, 16)
    sky.rays(480, 60, 10, 160, 14, "#c88bff", 0.08, 5823, soft=8)
    for i, (x0, y0, x1, y1) in enumerate(((420, 150, 380, 30), (520, 150, 600, 20), (470, 150, 520, 50))):
        path = bezier((x0, y0), (x0, y0 - 60), (x1, y1), None, 20)
        tm = Mask().poly(tapered(path, 0.5, 2.4)).get(1.4)
        sky.add("#7ab8ff", tm, 0.45)
        a = math.atan2(path[-1][1] - path[-3][1], path[-1][0] - path[-3][0])
        ship(sky, x1, y1, a, 0.9, 5825 + i)
    c.img = c.img * (1 - hatch[..., None]) + sky.img * hatch[..., None]


def shot3():
    """The evening the star of the centre collapsed, the foreman left it the foundry: keep the fires
    lit until they came back."""
    c = Canvas()
    foundry_hall(c, 5821, floor_y=462, columns=(90, 870), beams=False, sky=_sky_star, k=0.9)
    furnace(c, 820, 462, 80, 110, 0.9)
    furnace(c, 140, 462, 80, 110, 0.9)
    c.glow(480, 80, 200, "#5a2a6a", 0.25, tail=0.7)
    titan(c, 560, 462, 2.6, light=(0.5, -0.8), glow=AMBER, visor=0.9, reactor=0.6, k=0.9, flip=True)
    # the foreman, his hand raised toward it; the last workers leaving up a ladder to the hatch
    worker(c, 380, 462, 2.6, "wave", light=AMBER, k=0.8)
    lad = Mask()
    for lx in (250, 268):
        lad.line([(lx, 462), (lx + 40, 150)], 2.0)
    for i in range(1, 16):
        t = i / 16
        lad.line([(250 + 40 * t, 462 - 312 * t), (268 + 40 * t, 462 - 312 * t)], 1.4)
    c.over("#2a221c", lad.get(0.5), 0.95)
    for (t, s) in ((0.35, 1.5), (0.6, 1.3), (0.85, 1.1)):
        worker(c, 262 + 40 * t, 462 - 312 * t, s, "stand", light="#c8a8ff", k=0.5)
    return finish(c, bloom=0.6, vignette=0.55)


def shot4():
    """Nobody came back. Alone, century after century, the Titan kept the fires lit while the rock
    swallowed the foundry."""
    c = Canvas()
    foundry_hall(c, 5831, floor_y=462, columns=(120, 840), k=1.1)
    # the rock pouring through the broken roof
    rock = Mask().poly([(0, 0), (960, 0), (960, 90), (800, 140), (640, 110), (520, 170), (380, 120), (230, 150), (100, 100), (0, 130)]).get(1.2)
    rn = fbm(5833, 40, 4)
    c.over(c.gradient_field(0, 0, 0, 170, "#3a3028", "#120e0c") * (0.6 + 0.6 * rn[..., None]), rock)
    c.add(FIRE, rim(rock, 0, 3, 1.4) * rock, 0.25)
    rng = np.random.default_rng(5835)
    fall = Mask()
    for _ in range(140):
        fx, fy = rng.uniform(0, 960), rng.uniform(100, 440)
        fall.circle(fx, fy, rng.uniform(0.5, 1.6), v=int(rng.uniform(80, 200)))
    c.add("#b8a898", fall.get(0.4), 0.35)
    furnace(c, 700, 462, 100, 140, 1.2)
    # the Titan shovelling coal into the furnace, alone
    core, visor, _ = titan(c, 450, 462, 2.4, light=(0.9, -0.4), glow=AMBER, visor=0.9, reactor=0.8, k=1.1, rust=0.25, seed=5837)
    sparks(c, 5839, 70, 640, 400, 80, k=0.9)
    smoke(c, 5841, 700, 300, 220, 70, "#4a3e36", 0.4)
    return finish(c, bloom=0.6, vignette=0.6)


def shot5():
    """Rust gnawed its plates, its pistons wore out, its boiler overheated. It never stopped."""
    c = Canvas()
    foundry_hall(c, 5851, floor_y=500, columns=(60, 900), beams=False, k=1.0)
    c.glow(480, 320, 300, "#6a2a10", 0.4, tail=0.7)
    core, visor, _ = titan(c, 480, 560, 4.4, light=(-0.6, -0.8), glow="#ff7c3c", visor=1.0, reactor=1.4, k=1.2, rust=0.9, fist_up=True,
                           seed=5853)
    c.glow(core[0], core[1], 50, "#ff7c3c", 0.6, tail=0.6)
    # steam hissing out of its joints, sparks of its worn pistons
    smoke(c, 5855, 300, 330, 220, 70, "#c8c4c0", 0.35)
    smoke(c, 5857, 640, 300, 200, 60, "#c8c4c0", 0.3)
    sparks(c, 5859, 90, 360, 300, 70, k=1.0)
    sparks(c, 5861, 60, 600, 380, 60, k=0.8)
    return finish(c, bloom=0.65, vignette=0.6)


def shot6():
    """The Titan Mécanique still works. To it, every visitor is a thief come to put out its fires."""
    c = Canvas()
    foundry_hall(c, 5871, floor_y=470, columns=(80, 880), k=1.2)
    furnace(c, 160, 470, 80, 110, 1.0)
    furnace(c, 800, 470, 80, 110, 1.0)
    c.glow(480, 260, 300, "#5a3010", 0.35, tail=0.7)
    molten(c, [(x, 468) for x in range(0, 961, 12)], 5, 0.8)
    core, visor, _ = titan(c, 480, 470, 3.3, light=(-0.4, -0.9), glow=AMBER, visor=1.4, reactor=1.1, k=1.2, rust=0.5, fist_up=True, seed=5873)
    c.glow(visor[0], visor[1], 30, AMBER, 0.5, tail=0.5)
    c.rays(visor[0], visor[1], 8, 160, 10, AMBER, 0.1, 5875, soft=8)
    smoke(c, 5877, 400, 160, 160, 50, "#5a4a40", 0.35)
    sparks(c, 5879, 60, 480, 466, 300, k=0.6, up=20)
    return finish(c, bloom=0.62, vignette=0.6)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
