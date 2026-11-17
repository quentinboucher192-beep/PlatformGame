"""Le Phénix Stellaire, « Le Porteur d'aube » (2.56): long ago, each morning, a little bird of light
left the star of the centre to carry the dawn to the Confins; wherever it flew, the day rose over
the volcanoes and the valleys of black glass; the night the star collapsed, it waited for its
light, which never came back; so, for the dawn to come back, it set its own feathers alight, burned
and fell to ashes; the next day it was reborn from them, bigger and brighter, and began again;
since then the Phénix burns itself every morning in its nest at the top of the Confins: it no
longer knows how to stop, for fear that the night would win."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blur, fbm, finish, rgb, rim, smoothstep, tapered
from story_pyrrhax import collapsed_star, embers, flame, ground, spires

# Its plumage (boss_h_phenix.py: deep navy and violet, star speckles; the light on the edge of
# every feather, white-hot at the tips), paler and rounder when it is little.
LITTLE = {"body": "#5a54a0", "top": "#8a84d0", "dark": "#2e2a5a", "light": "#ffd04a", "hot": "#fff3c8"}
GROWN = {"body": "#342f67", "top": "#4b4589", "dark": "#161332", "light": "#ffd04a", "hot": "#fff3c8"}
EMBER = {"body": "#3c302c", "top": "#78564a", "dark": "#1c1412", "light": "#ff6e40", "hot": "#ffe0c0"}
GOLD = "#ffd04a"
DAWN = "#ff9ac6"


# --------------------------------------------------------------------------- the sky, the Confins

def dawn_sky(c, seed, horizon=0.72, top="#140c30", mid="#5a2a5a", low="#ff9a6a", sun=None, stars=360, sun_k=1.0):
    """The sky of the Confins at dawn: night above, rose and gold at the horizon, a few stars."""
    c.vgradient([(0.0, top), (horizon * 0.6, mid), (horizon, low), (1.0, "#2a0e14")])
    c.nebula(seed, "#6a2a6a", "#ff8a5a", 0.18, cell=260)
    c.stars(seed + 1, stars, "#ffe8f0", 0.6, region=(0, 0, c.w, c.h * horizon * 0.7))
    if sun is not None:
        sx, sy, sr = sun
        c.glow(sx, sy, sr * 6, "#ff9a5a", 0.35 * sun_k, tail=0.6)
        c.glow(sx, sy, sr * 2.2, "#ffe0a0", 0.7 * sun_k, tail=0.5)
        c.glow(sx, sy, sr * 0.8, "#fffbe8", 1.2 * sun_k, tail=0.3)


def night_sky(c, seed, horizon=0.76):
    c.vgradient([(0.0, "#06040e"), (horizon * 0.6, "#140a24"), (horizon, "#2a0e24"), (1.0, "#0a0408")])
    c.nebula(seed, "#3a1a5a", "#1a1a4a", 0.3, cell=240)
    c.stars(seed + 1, 700, "#e8e0ff", 0.8, region=(0, 0, c.w, c.h * horizon))


def peaks(c, seed, base, pts, color="#1a0e1c", top="#3a1e2a", rim_color=GOLD, rim_k=0.4, light=(-2, -2), jag=14.0):
    """A ridge of volcanic peaks of black glass above `base`: the summits (x, height) joined by
    jagged slopes (small cliffs and notches), lit on their crests."""
    rng = np.random.default_rng(seed)
    xs = np.arange(-30.0, 1000.0, 9.0)
    px = np.array([p[0] for p in pts], np.float32)
    ph = np.array([p[1] for p in pts], np.float32)
    order = np.argsort(px)
    px, ph = px[order], ph[order]
    hs = []
    for x in xs:
        # the summits as sharp cones, the highest wins, a floor between them
        h = np.max(ph - np.abs(x - px) * (0.9 + 0.5 * (ph / max(1.0, ph.max()))))
        hs.append(max(h, 20.0))
    hs = np.array(hs)
    hs = hs + np.cumsum(rng.normal(0, 0.9, len(hs))) * 0.0 + rng.uniform(-jag, jag, len(hs)) * (hs > 30)
    ridge = [(-30.0, base + 80)] + [(float(x), float(base - h)) for x, h in zip(xs, hs)] + [(1000.0, base + 80)]
    m = Mask().poly(ridge).get(0.6)
    hmax = float(ph.max())
    tex = fbm(seed + 1, 40, 4)
    c.over(c.gradient_field(0, base - hmax, 0, base + 40, top, color) * (0.85 + 0.3 * tex[..., None]), m)
    # facets of glass catching the light: a few strokes down the slopes
    fac = Mask()
    for i in range(int(len(pts) * 3)):
        x0 = rng.uniform(0, 960)
        h0 = float(np.interp(x0, xs, hs))
        if h0 < 40:
            continue
        fac.line([(x0, base - h0 + 6), (x0 + rng.uniform(-30, 30), base - h0 * rng.uniform(0.2, 0.6))], 1.2, 140)
    c.add(rim_color, fac.get(0.6) * m, rim_k * 0.35)
    c.add(rim_color, rim(m, light[0], light[1], 0.9), rim_k)
    return m


def ash(c, seed, n, region, color="#b8aca4", k=0.7, size=(0.8, 2.2)):
    """Flakes of ash drifting down."""
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    m = Mask()
    for _ in range(n):
        x, y, r = rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(*size)
        m.rotated_ellipse(x, y, r * 1.6, r, rng.uniform(0, 180), v=int(rng.uniform(120, 255)), steps=8)
    c.over(color, m.get(0.4), k)


def trail(c, pts, w0, w1, color, k=1.0, soft=3.0):
    """A ribbon of light along a path (the dawn it leaves behind)."""
    m = Mask().poly(tapered(pts, w0, w1)).get(soft)
    c.add(color, m, k)
    c.add(color, blur(m, soft * 4), k * 0.6)


# --------------------------------------------------------------------------- the bird

def bird(c, x, y, s, pal, flip=False, wings=0.0, fire=0.0, glow=1.0, tail=1.0, eye=1.0, sleepy=0.0, seed=1, look=0.0):
    """The phoenix seen from the side, facing right (left if `flip`): a slender body with a star in
    its chest, a crested head and a hooked beak, two great wings of long feathers whose edges glow,
    three tail plumes streaming like comets. (x, y) = the middle of its body; `wings`: -1 folded
    along its back, 0 raised, 1 spread high; `fire`: its feathers burning; `glow`: how bright its
    light is; `sleepy`: its eye half closed; `look`: its head raised (> 0) or lowered (< 0)."""
    f = -1.0 if flip else 1.0
    rng = np.random.default_rng(seed)

    def P(px, py):
        return (x + px * s * f, y + py * s)

    def ang(a):
        """An angle drawn for a bird facing right, mirrored when it faces left."""
        return a if f > 0 else math.pi - a

    light, hot = pal["light"], pal["hot"]
    # ---- the tail plumes (behind everything): long ribbons of night, a vein of light in each
    tm, tl = Mask(), Mask()
    tips = []
    for i, (dy, ln, bend) in enumerate(((0.0, 78, -14), (8.0, 70, 6), (-7.0, 60, -24))):
        L = ln * tail
        pts = bezier(P(-13, 4 + dy * 0.2), P(-13 - L * 0.35, 6 + dy + bend * 0.2), P(-13 - L * 0.72, 12 + dy + bend),
                     P(-13 - L, 6 + dy + bend * 1.4 + 6 * math.sin(i * 2.0)), 26)
        tm.poly(tapered(pts, 8.5 * s, 2.0 * s))
        tl.poly(tapered(pts[4:], 2.6 * s, 0.8 * s))
        tips.append(pts[-1])
    tmm = tm.get(0.5)
    c.over(c.gradient_field(0, y - 6 * s, 0, y + 34 * s, pal["top"], pal["dark"]), tmm)
    c.add(light, tl.get(1.0), 0.75 * glow)
    c.add(light, rim(tmm, 0, -1.5, 0.6), 0.35 * glow)
    for (ex, ey) in tips:  # each frays into sparks
        for j in range(6):
            c.glow(ex - f * rng.uniform(0, 30) * s, ey + rng.uniform(-9, 9) * s, max(0.6, rng.uniform(0.5, 1.2) * s), light, 0.55 * glow)

    # ---- a wing: feathers from all along the arm, fanned from the long primaries at the wrist to
    # the short secondaries by the body, overlapping into one great wing; their outer half glows
    def wing(near):
        sk = 1.0 if near else 0.84
        sh = P(5, -6) if near else P(9, -8)
        main = math.radians(-118 - 52 * (1.0 - (wings + 1.0) * 0.5) * 1.1)  # spread high .. folded back
        if wings < -0.5:
            main = math.radians(-172)
        arm = 30.0 * s * sk
        wrist = (sh[0] + math.cos(ang(main + 0.25)) * arm * 0.55, sh[1] + math.sin(main + 0.25) * arm * 0.55)
        m, edge, hotm = Mask(), Mask(), Mask()
        n = 15
        for i in range(n):
            u = i / (n - 1)  # 0 = the outermost primary, 1 = the last secondary by the body
            ox = wrist[0] + (sh[0] - wrist[0]) * u * 0.9
            oy = wrist[1] + (sh[1] - wrist[1]) * u * 0.9
            a = main + math.radians(8) - u * math.radians(78 if wings > -0.5 else 18)
            L = (52 - 30 * u) * s * sk * (0.92 + 0.16 * rng.random()) * (0.62 if wings < -0.5 else 1.0)
            d = (math.cos(ang(a)), math.sin(a))
            bend = f * 5 * s * (0.3 + u)
            tip = (ox + d[0] * L - bend * 0.3, oy + d[1] * L + abs(bend) * 0.4)
            mid = (ox + d[0] * L * 0.55, oy + d[1] * L * 0.55)
            pts = bezier((ox, oy), mid, tip, None, 12)
            m.poly(tapered(pts, 12.5 * s * sk, 4.0 * s * sk))
            edge.poly(tapered(pts[6:], 3.4 * s * sk, 1.2 * s * sk))
            hotm.circle(tip[0], tip[1], 1.4 * s * sk)
        m.poly(tapered([sh, wrist], 10.0 * s * sk, 8.0 * s * sk))
        mm = m.get(0.5)
        base_c = pal["body"] if near else pal["dark"]
        c.over("#07050f", np.clip(blur(mm, 1.0) * 1.6, 0, 1), 0.7)
        c.over(c.gradient_field(wrist[0], wrist[1] - 30 * s, sh[0], sh[1] + 20 * s, pal["top"] if near else base_c, base_c), mm)
        # coverts: a darker band near the arm
        cov = Mask().poly(tapered([sh, wrist], 16.0 * s * sk, 12.0 * s * sk)).get(1.0) * mm
        c.over(pal["dark"], cov, 0.35)
        c.add(light, edge.get(0.7) * mm, (0.85 if near else 0.5) * glow)
        c.add(hot, hotm.get(0.8), (0.8 if near else 0.4) * glow)
        c.add(light, rim(mm, 0, -1.5, 0.6), 0.3 * glow)

    wing(False)
    # ---- the body, the neck, the head
    body = Mask()
    body.rotated_ellipse(x, y, 18 * s, 10.5 * s, -10 * f)
    neck = bezier(P(10, -4), P(15, -10 - 2 * look), P(18, -16 - 4 * look), None, 10)
    body.poly(tapered(neck, 10 * s, 7.5 * s))
    hx, hy = P(20, -18 - 5 * look)
    body.circle(hx, hy, 7.2 * s)
    bm = body.get(0.5)
    c.over("#07050f", np.clip(blur(bm, 1.0) * 1.6, 0, 1), 0.6)
    c.over(c.gradient_field(0, y - 24 * s, 0, y + 10 * s, pal["top"], pal["body"]), bm)
    c.add(light, rim(bm, 0, -2, 0.8), 0.4 * glow)
    c.add(light, rim(bm, f * 2, 1, 0.8), 0.2 * glow)
    # small feathers and star speckles on the plumage
    for i in range(int(14 * s)):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, 1)
        c.glow(x + math.cos(a) * d * 15 * s, y + math.sin(a) * d * 8 * s, max(0.5, 0.55 * s), "#e8e0ff", 0.3)
    # the crest: three plumes swept back, their tips of light
    crest, cl = Mask(), Mask()
    for i, (a0, ln) in enumerate(((200, 17), (214, 22), (228, 18))):
        a = math.radians(a0 + 12 * look)
        tip = (hx + math.cos(ang(a)) * ln * s, hy - 5 * s + math.sin(a) * ln * s)
        pts = bezier((hx - f * 1 * s, hy - 5 * s), ((hx + tip[0]) / 2 - f * 2 * s, hy - 10 * s), tip, None, 8)
        crest.poly(tapered(pts, 3.6 * s, 0.9 * s))
        cl.poly(tapered(pts[3:], 1.8 * s, 0.7 * s))
    c.over(pal["top"], crest.get(0.4))
    c.add(light, cl.get(0.6), 0.85 * glow)
    # the hooked beak, pale as bone
    bx, by = hx + f * 5.5 * s, hy + 0.5 * s
    beak = Mask().poly([(bx, by - 3.0 * s), (bx + f * 7 * s, by - 2.4 * s), (bx + f * 11 * s, by + 0.8 * s), (bx + f * 9.5 * s, by + 3.4 * s),
                        (bx + f * 6.5 * s, by + 1.4 * s), (bx, by + 2.6 * s)]).get(0.4)
    c.over("#d6d4f2", beak)
    c.over("#57558c", rim(beak, 0, 1.2, 0.4), 0.8)
    # the eye and the stripe of light behind it
    ex, ey = hx + f * 2.2 * s, hy - 1.4 * s
    stripe = Mask().poly(tapered([(ex, ey), (ex - f * 8 * s, ey + 1.2 * s)], 2.2 * s, 0.7 * s)).get(0.4)
    c.add(light, stripe, 0.75 * glow)
    c.over("#0a0818", Mask().circle(ex, ey, 1.9 * s).get(0.3))
    if sleepy < 0.95:
        c.glow(ex, ey, 1.3 * s * (1 - 0.6 * sleepy), hot, 1.0 * eye * glow)
    if sleepy > 0:
        lid = Mask().poly([(ex - 2.6 * s, ey - 2.6 * s), (ex + 2.6 * s, ey - 2.6 * s), (ex + 2.6 * s, ey - 2.6 * s + 4.0 * s * sleepy),
                           (ex - 2.6 * s, ey - 2.6 * s + 4.0 * s * sleepy)]).get(0.3)
        c.over(pal["top"], lid * Mask().circle(ex, ey, 2.2 * s).get(0.3), 0.95)
    # the star in its chest
    sx, sy = P(10, 1)
    c.glow(sx, sy, 2.4 * s, "#ffffff", 0.9 * glow)
    c.glow(sx, sy, 7.0 * s, light, 0.45 * glow, tail=0.5)
    wing(True)
    # ---- burning: tongues of fire from its feathers and its back
    if fire > 0:
        for i in range(7):
            u = i / 6.0
            px, py = P(-14 + 32 * u, -8 - 6 * math.sin(u * math.pi))
            flame(c, px, py, -math.pi / 2 + rng.uniform(-0.35, 0.35), (26 + 22 * rng.random()) * s * fire, 12 * s, seed + i,
                  k=0.8 * fire)
        c.glow(x, y - 10 * s, 40 * s, "#ff7a2a", 0.35 * fire, tail=0.5)
    c.glow(x, y, 30 * s, light, 0.1 * glow, tail=0.6)
    return P(10, 1)


def nest(c, cx, cy, rx, seed, glow_k=1.0):
    """Its nest at the top of the Confins: a ring of black glass shards round a bed of embers."""
    bed = Mask().ellipse(cx, cy, rx, rx * 0.22).get(2)
    heat = fbm(seed, 14, 4)
    c.over("#2a1410", bed)
    c.add("#ff6a2a", bed * smoothstep(0.45, 0.85, heat), 0.9 * glow_k)
    c.add("#ffd080", bed * smoothstep(0.75, 0.95, heat), 0.7 * glow_k)
    rng = np.random.default_rng(seed + 1)
    shards = Mask()
    for i in range(26):
        a = math.pi * (0.03 + 0.94 * i / 25)
        px, py = cx + math.cos(a) * rx * 1.02, cy + math.sin(a) * rx * 0.24 + 4
        h = rng.uniform(14, 34) * (0.6 + 0.4 * math.sin(a))
        lean = rng.uniform(-8, 8)
        shards.poly([(px - 7, py + 8), (px + lean, py - h), (px + 7, py + 8)])
    sm = shards.get(0.5)
    c.over(c.gradient_field(0, cy - 30, 0, cy + 30, "#3a2030", "#120810"), sm)
    c.add("#ff8a4a", rim(sm, 0, -2, 0.7), 0.5 * glow_k)
    c.glow(cx, cy - 6, rx * 0.8, "#ff7a3a", 0.25 * glow_k, tail=0.6)


# --------------------------------------------------------------------------- the six shots

def shot1():
    """Long ago, each morning, a little bird of light left the star of the centre to carry the
    dawn to the Confins."""
    c = Canvas()
    dawn_sky(c, 4101, horizon=0.74, top="#100a2c", mid="#4a2050", low="#ff8a6a", sun=(780, 360, 26))
    c.rays(780, 360, 16, 520, 40, "#ffd8a0", 0.14, 4102, soft=10)
    peaks(c, 4103, 470, [(60, 120), (170, 210), (280, 150), (380, 240), (470, 170), (580, 220), (690, 140), (820, 190),
                         (930, 110)], rim_color="#ffb070", rim_k=0.45, light=(-2, -2))
    ground(c, 492, 4105, "#0a0406", "#1e0c10")
    # its way from the star, a ribbon of gold
    way = bezier((770, 356), (690, 250), (560, 200), (450, 214), 30)
    trail(c, way, 1.0, 9.0, GOLD, 0.45, 2.5)
    bird(c, 420, 214, 1.45, LITTLE, flip=True, wings=0.6, glow=1.1, tail=0.8, seed=4107, look=0.3)
    embers(c, 4109, 30, (380, 160, 780, 330), "#ffe0a0", (0.8, 1.8))
    return finish(c, bloom=0.62, vignette=0.48)


def shot2():
    """It brushed the sleeping volcanoes and the valleys of black glass: wherever it passed, the
    day rose."""
    c = Canvas()
    dawn_sky(c, 4201, horizon=0.7, top="#160c34", mid="#4a2258", low="#ff9a72", sun=None, stars=260)
    # the left of the valley is lit already, the right still in the night
    lit = smoothstep(620, 180, c.xx)
    c.add("#ffb070", lit * smoothstep(200, 420, c.yy), 0.25)
    m = peaks(c, 4203, 452, [(40, 140), (150, 230), (300, 120), (420, 200), (560, 110), (700, 260), (860, 180),
                             (950, 140)], rim_color="#ffb070", rim_k=0.5)
    c.multiply("#3a3060", m * smoothstep(380, 760, c.xx), 0.6)
    # the volcano mouths glowing faintly where the day has come
    for (vx, vy) in ((150, 226), (420, 256)):
        c.glow(vx, vy, 16, "#ff7a3a", 0.6, tail=0.5)
    ground(c, 470, 4205, "#0a0406", "#22100e")
    # the valley of black glass catching the light: long glints where the day has come
    rng = np.random.default_rng(4206)
    glints = Mask()
    for i in range(26):
        gx, gy = rng.uniform(0, 620), rng.uniform(482, 536)
        glints.line([(gx, gy), (gx + rng.uniform(20, 70), gy + rng.uniform(-2, 2))], 1.2, int(rng.uniform(120, 255)))
    c.add("#ffd8a0", glints.get(0.6) * smoothstep(660, 120, c.xx), 0.6)
    way = bezier((80, 300), (230, 330), (380, 300), (560, 330), 30)
    trail(c, way, 12.0, 1.5, GOLD, 0.5, 2.5)
    bird(c, 600, 326, 1.35, LITTLE, wings=0.9, glow=1.15, tail=0.9, seed=4207, look=-0.2)
    embers(c, 4209, 40, (60, 260, 600, 380), "#ffe0a0", (0.8, 1.6))
    return finish(c, bloom=0.62, vignette=0.5)


def shot3():
    """The night the star of the centre collapsed, the bird waited for its light. It never came
    back."""
    c = Canvas()
    night_sky(c, 4301)
    collapsed_star(c, 620, 150, 26)
    c.rays(620, 150, 10, 300, 22, "#c88bff", 0.08, 4302, soft=10)
    m = peaks(c, 4303, 476, [(60, 90), (210, 160), (330, 120), (450, 200), (600, 100), (760, 170), (900, 120)],
              color="#0a0612", top="#1e1428", rim_color="#a888ff", rim_k=0.35, light=(2, -2))
    ground(c, 492, 4305, "#050208", "#120a14")
    # the little bird on its peak, dim, looking up at where the light used to come from
    # its peak, a needle of black glass
    perch = Mask().poly([(360, 545), (384, 452), (378, 420), (402, 380), (412, 340), (426, 318), (436, 296), (446, 284),
                         (456, 292), (462, 318), (478, 344), (474, 372), (494, 410), (502, 452), (530, 545)]).get(0.6)
    c.over(c.gradient_field(0, 280, 0, 545, "#2a2038", "#08060c"), perch)
    c.add("#a888ff", rim(perch, 2, -2, 0.8), 0.4)
    bird(c, 444, 272, 1.25, LITTLE, wings=-1.0, glow=0.35, tail=0.7, seed=4307, look=0.9, eye=0.7)
    ash(c, 4309, 160, (0, 0, 960, 520), "#9a92b0", 0.35, (0.6, 1.6))
    return finish(c, bloom=0.55, vignette=0.55)


def shot4():
    """So, for the dawn to come back, it set its own feathers alight. It burned and burned… and
    fell to ashes."""
    c = Canvas()
    night_sky(c, 4401, horizon=0.8)
    c.glow(470, 250, 380, "#ff6a2a", 0.32, tail=0.6)
    peaks(c, 4403, 486, [(40, 120), (180, 200), (330, 150), (480, 230), (640, 160), (800, 210), (930, 130)],
          color="#160a0c", top="#4a1e1a", rim_color="#ff9a4a", rim_k=0.6, light=(0, -3))
    ground(c, 500, 4405, "#0a0404", "#2a0e0a")
    # it falls, burning, a trail of ash and sparks above it
    way = bezier((640, 70), (600, 120), (540, 180), (500, 230), 24)
    m = Mask().poly(tapered(way, 2.0, 22.0)).get(4)
    c.over("#6a5a58", m * (0.5 + 0.5 * fbm(4406, 16, 3)), 0.6)
    bird(c, 486, 250, 1.6, EMBER, wings=0.2, fire=0.75, glow=1.3, tail=0.9, seed=4407, look=-0.4, sleepy=0.6)
    embers(c, 4409, 90, (300, 60, 700, 420), "#ffb050", (0.8, 2.4))
    ash(c, 4411, 140, (260, 40, 760, 470), "#a89a92", 0.55)
    return finish(c, bloom=0.66, vignette=0.5)


def shot5():
    """The next day it was reborn from its ashes, bigger and brighter. And it began again."""
    c = Canvas()
    dawn_sky(c, 4501, horizon=0.78, top="#1a0c30", mid="#6a2a50", low="#ffb070", sun=(480, 420, 40), sun_k=0.8)
    c.rays(480, 300, 22, 560, 46, "#ffd890", 0.2, 4502, soft=8)
    peaks(c, 4503, 500, [(30, 100), (170, 170), (300, 120), (660, 120), (790, 180), (930, 110)], rim_color="#ffc080", rim_k=0.5)
    # the summit and its nest, the heap of ash glowing in it
    summit = Mask().poly(bezier((230, 545), (330, 430), (420, 412), (480, 412), 16) + bezier((480, 412), (540, 412), (630, 430), (730, 545), 16)).get(0.6)
    c.over(c.gradient_field(0, 400, 0, 545, "#3a1e24", "#0e0608"), summit)
    c.add("#ffb070", rim(summit, 0, -2, 0.8), 0.5)
    nest(c, 480, 424, 110, 4505, glow_k=1.3)
    heap = Mask().ellipse(480, 420, 46, 13).get(1.5)
    c.over("#8a7a72", heap)
    c.add("#ff8a3a", heap * smoothstep(0.5, 0.9, fbm(4506, 8, 3)), 0.7)
    # it rises from it, wings wide, bigger and brighter
    bird(c, 486, 250, 2.1, GROWN, wings=1.0, glow=1.6, tail=1.15, seed=4507, look=0.6)
    c.glow(480, 330, 120, "#ffe0a0", 0.22, tail=0.6)
    embers(c, 4509, 70, (260, 120, 720, 430), "#ffe0a0", (0.8, 2.2))
    return finish(c, bloom=0.68, vignette=0.48)


def shot6():
    """Since then, the Phénix burns itself every morning in its nest. It no longer knows how to
    stop, for fear that the night would win."""
    c = Canvas()
    dawn_sky(c, 4601, horizon=0.8, top="#120a2a", mid="#5a2a52", low="#ff9a6a", sun=None, stars=420)
    c.glow(480, 470, 260, "#ff9a5a", 0.3, tail=0.6)
    peaks(c, 4602, 520, [(40, 150), (180, 230), (300, 160), (660, 170), (790, 240), (930, 140)], rim_color="#ffb070", rim_k=0.45)
    summit = Mask().poly(bezier((150, 545), (290, 420), (400, 396), (480, 396), 16) + bezier((480, 396), (560, 396), (670, 420), (810, 545), 16)).get(0.6)
    c.over(c.gradient_field(0, 380, 0, 545, "#3a1e28", "#0c0608"), summit)
    c.add("#ffb070", rim(summit, 0, -2, 0.8), 0.45)
    nest(c, 480, 410, 140, 4603, glow_k=1.1)
    # the grown Phénix in its nest, wings folded, its eye heavy, burning softly
    bird(c, 486, 368, 2.3, GROWN, wings=-1.0, fire=0.3, glow=1.2, tail=1.0, seed=4605, look=0.25, sleepy=0.55)
    # the front of its nest over it
    front = Mask().ellipse(480, 414, 150, 22).get(1.5) * smoothstep(404, 418, c.yy)
    c.over("#1a0c0e", front, 0.9)
    c.add("#ff7a3a", rim(front, 0, -2, 0.8), 0.5)
    ash(c, 4607, 220, (0, 0, 960, 540), "#c8bab2", 0.5)
    embers(c, 4609, 50, (260, 140, 700, 420), "#ffc070", (0.8, 2.0))
    return finish(c, bloom=0.64, vignette=0.52)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
