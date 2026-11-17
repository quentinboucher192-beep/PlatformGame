"""Singularis, « Le Cœur effondré »: the young star Lyse, her three planets, her gifts, the
machines that drank her light, her last spark, and the collapse into the black heart."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blur, fbm, finish, rgb, rim, smoothstep, tapered

VIOLET = "#c88bff"


def space(c, seed, top="#07031a", mid="#140a36", bottom="#0a0620", neb=("#5a2a9a", "#1f3a8a"), neb_k=0.55, stars=900):
    c.vgradient([(0.0, top), (0.55, mid), (1.0, bottom)])
    c.nebula(seed, neb[0], neb[1], neb_k, cell=260)
    c.nebula(seed + 7, VIOLET, "#ff9ec7", neb_k * 0.3, cell=150, power=3.0)
    c.stars(seed + 3, stars, "#e8e4ff", 0.9)


def face(c, cx, cy, r, mood, ink="#9a4212", k=0.85):
    """Lyse's face: closed happy eyes and a smile, a song, worry, or sadness."""
    m = Mask()
    ew = max(1.6, r * 0.075)
    ex, ey = r * 0.34, cy - r * 0.06
    cheeks = mood in ("happy", "sing")
    if mood in ("happy", "sing"):
        for s in (-1, 1):
            m.arc(cx + s * ex, ey + r * 0.05, r * 0.15, r * 0.13, 200, 340, ew)
        if mood == "happy":
            m.arc(cx, cy + r * 0.2, r * 0.2, r * 0.15, 25, 155, ew)
        else:
            m.ellipse(cx, cy + r * 0.3, r * 0.1, r * 0.13)
    elif mood == "worried":
        for s in (-1, 1):
            m.ellipse(cx + s * ex, ey + r * 0.04, r * 0.075, r * 0.11)
            m.line([(cx + s * (ex + r * 0.14), ey - r * 0.2), (cx + s * (ex - r * 0.1), ey - r * 0.28)], ew * 0.8)
        m.arc(cx, cy + r * 0.36, r * 0.1, r * 0.06, 200, 340, ew * 0.9)
    else:  # sad
        for s in (-1, 1):
            m.arc(cx + s * ex, ey + r * 0.02, r * 0.14, r * 0.1, 20, 160, ew)
            m.line([(cx + s * (ex + r * 0.16), ey - r * 0.16), (cx + s * (ex - r * 0.08), ey - r * 0.26)], ew * 0.8)
        m.arc(cx, cy + r * 0.42, r * 0.16, r * 0.1, 205, 335, ew)
    c.over(ink, m.get(0.35), k)
    if mood in ("worried",):
        hl = Mask()
        for s in (-1, 1):
            hl.circle(cx + s * ex + r * 0.025, ey - r * 0.01, r * 0.028)
        c.over("#fff8e0", hl.get(0.2), 0.9)
    if mood == "sad":
        tear = Mask().ellipse(cx + ex + r * 0.04, ey + r * 0.24, r * 0.045, r * 0.07)
        c.over("#bfe9ff", tear.get(0.3), 0.85)
        c.glow(cx + ex + r * 0.04, ey + r * 0.24, r * 0.08, "#8fd8ff", 0.4)
    if cheeks:
        for s in (-1, 1):
            ch = Mask().ellipse(cx + s * r * 0.55, cy + r * 0.16, r * 0.13, r * 0.08)
            c.over("#ff8f9e", ch.get(r * 0.06), 0.45)


def lyse(c, cx, cy, r, glow=1.0, mood="happy", seed=1, hot="#fffbe8", edge="#ffb347", rays=True):
    """The young star: a halo, rays, a granulated glowing body and her face."""
    c.glow(cx, cy, r * 5.0, "#7a4cff", 0.28 * glow)
    c.glow(cx, cy, r * 2.4, "#ff9a3a", 0.55 * glow)
    c.glow(cx, cy, r * 1.4, "#ffe7a8", 0.8 * glow)
    if rays:
        c.rays(cx, cy, 30, r * 6.5, r * 0.09, "#ffe2a0", 0.55 * glow, seed)
    d = c.dist(cx, cy) / r
    inside = smoothstep(1.0 + 1.5 / r, 1.0 - 1.5 / r, d)
    gran = fbm(seed + 40, max(4.0, r * 0.25), 4)
    k = np.clip(d, 0, 1) ** 1.6
    col = rgb(hot)[None, None, :] * (1 - k[..., None]) + rgb(edge)[None, None, :] * k[..., None]
    col = col * (0.86 + 0.26 * gran[..., None])
    c.over(col * glow + (1 - glow) * rgb("#3a2010"), inside)
    c.glow(cx, cy, r * 0.8, "#ffffff", 0.35 * glow)
    face(c, cx, cy, r, mood, ink="#5a1806" if mood == "sad" else "#9a4212", k=0.9)


def planet(c, cx, cy, r, kind, light_to, seed, dead=False):
    """A small planet lit from `light_to` (the star): forest, volcano or ice."""
    lx, ly = light_to[0] - cx, light_to[1] - cy
    L = math.hypot(lx, ly) or 1.0
    light = (lx / L * 0.85, ly / L * 0.85)
    tex = fbm(seed, max(3.0, r * 0.5), 4)
    if dead:
        base, rimc = {"forest": "#3a4652", "volcano": "#4a4448", "ice": "#5a6878"}[kind], "#8fb4d8"
        c.sphere(cx, cy, r, base, light, ambient=0.06, texture=tex, tex_k=0.3, rim=rimc, rim_k=0.35)
        frost = Mask().circle(cx, cy, r)
        c.over("#c8e4ff", frost.get(0.6) * smoothstep(0.55, 0.9, tex), 0.25)
        return
    if kind == "forest":
        land = smoothstep(0.45, 0.55, tex)
        base = np.broadcast_to(rgb("#2f7fd0"), c.img.shape) * (1 - land[..., None]) + rgb("#3fae5a") * land[..., None]
        c.glow(cx, cy, r * 1.35, "#7dffb0", 0.22)
        mask = Mask().circle(cx, cy, r).get()
        c.over(base * (0.12 + 0.88 * _ndl(c, cx, cy, r, light))[..., None], mask)
        c.add("#bff7ff", rim(mask, -light[0] * 3, -light[1] * 3, 1.2), 0.5)
    elif kind == "volcano":
        c.glow(cx, cy, r * 1.35, "#ff8a3a", 0.25)
        c.sphere(cx, cy, r, "#c8552a", light, ambient=0.12, texture=tex, tex_k=0.45, rim="#ffd0a0", rim_k=0.5)
        cracks = smoothstep(0.47, 0.5, tex) * smoothstep(0.53, 0.5, tex)
        disc = Mask().circle(cx, cy, r * 0.97).get()
        c.add("#ff7a20", cracks * disc, 1.6)
        c.add("#ffd060", cracks * disc, 0.6)
    else:  # ice
        c.glow(cx, cy, r * 1.35, "#a8f0ff", 0.22)
        c.sphere(cx, cy, r, "#a9dcff", light, ambient=0.18, texture=tex, tex_k=0.25, rim="#ffffff", rim_k=0.6, spec=0.5)
        caps = Mask().ellipse(cx, cy - r * 0.78, r * 0.62, r * 0.3).get(0.8) * Mask().circle(cx, cy, r).get()
        c.over("#f4fbff", caps, 0.85)


def _ndl(c, cx, cy, r, light):
    dx = (c.xx - cx) / r
    dy = (c.yy - cy) / r
    nz = np.sqrt(np.clip(1 - dx * dx - dy * dy, 0, 1))
    lx, ly = light
    lz = math.sqrt(max(0.0, 1 - lx * lx - ly * ly))
    return np.clip(dx * lx + dy * ly + nz * lz, 0, 1)


def orbit_point(cx, cy, rx, ry, tilt, a):
    t = math.radians(a)
    x, y = rx * math.cos(t), ry * math.sin(t)
    ca, sa = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
    return cx + x * ca - y * sa, cy + x * sa + y * ca


# --------------------------------------------------------------------------- the six shots

def shot1():
    """The young star Lyse, smiling, in her nebula (a disc of golden dust around her)."""
    c = Canvas()
    space(c, 101)
    cx, cy = 480, 252
    tilt = -10
    dust = fbm(105, 26, 5)
    band = np.zeros_like(c.yy)
    for i, (ro, ri, k) in enumerate(((310, 250, 0.7), (240, 200, 1.0), (190, 150, 0.6))):
        band += np.clip(Mask().rotated_ellipse(cx, cy, ro, ro * 0.23, tilt).get(2) - Mask().rotated_ellipse(cx, cy, ri, ri * 0.23, tilt).get(2), 0, 1) * k
    band *= 0.35 + 0.65 * dust
    front = band * (c.yy > cy + (c.xx - cx) * np.tan(np.radians(tilt)))
    c.add("#ffcf7a", band - front, 0.42)
    c.add("#c88bff", blur(band - front, 6), 0.25)
    lyse(c, cx, cy, 64, 1.0, "happy", 107)
    c.add("#ffd9a0", front, 0.5)
    c.add("#c88bff", blur(front, 6), 0.25)
    rng = np.random.default_rng(109)
    for _ in range(26):
        a, d = rng.uniform(0, 2 * math.pi), rng.uniform(110, 330)
        c.glow(cx + math.cos(a) * d, cy + math.sin(a) * d * 0.55, rng.uniform(1.5, 3.5), "#ffe9a8", rng.uniform(0.4, 0.9))
    return finish(c, bloom=0.6, vignette=0.45)


def shot2():
    """Lyse sings light; three small planets come to warm themselves around her."""
    c = Canvas()
    space(c, 201, mid="#170b38")
    cx, cy = 480, 262
    rx, ry, tilt = 330, 112, -7
    orbit = Mask()
    pts = [orbit_point(cx, cy, rx, ry, tilt, a) for a in range(0, 361, 4)]
    orbit.line(pts, 1.4, 160)
    c.add("#ffd9a0", orbit.get(0.8), 0.35)
    # Her song: soft rings of light.
    for i, rr in enumerate((88, 128, 172, 220)):
        ring = Mask().arc(cx, cy, rr, rr * 0.92, 0, 360, 2.2, 255, 64)
        c.add("#ffe2a0", ring.get(1.6), 0.28 - i * 0.05)
    lyse(c, cx, cy, 50, 1.0, "sing", 207)
    # Notes of light rising from her song.
    rng = np.random.default_rng(211)
    for i in range(14):
        a = rng.uniform(-2.6, -0.5)
        d = rng.uniform(70, 200)
        c.glow(cx + math.cos(a) * d, cy + math.sin(a) * d, rng.uniform(2, 4), "#fff1c4", 0.8)
    for kind, a, r in (("ice", 252, 27), ("forest", 166, 36), ("volcano", 16, 40)):
        x, y = orbit_point(cx, cy, rx, ry, tilt, a)
        planet(c, x, y, r, kind, (cx, cy), 220 + r)
    return finish(c, bloom=0.6)


def shot3():
    """Her gifts: ribbons of light carry the sap of the forests, the fire of the volcanoes and the
    sparkle of the ice to the three planets."""
    c = Canvas()
    space(c, 301, mid="#150a30", stars=700)
    sx, sy = 222, 272
    targets = [("forest", (700, 158), 40, "#7dff9a", "#d8ffb0"),
               ("volcano", (792, 294), 46, "#ff9a3a", "#ffe08a"),
               ("ice", (690, 420), 34, "#9eeaff", "#ffffff")]
    rng = np.random.default_rng(303)
    for kind, (tx, ty), r, col, core in targets:
        mid1 = (sx + (tx - sx) * 0.35, sy + (ty - sy) * 0.1 - 50)
        mid2 = (sx + (tx - sx) * 0.7, ty + (sy - ty) * 0.15 + 20)
        path = bezier((sx + 40, sy), mid1, mid2, (tx - r * 0.8, ty), 40)
        band = Mask().poly(tapered(path, 34, 10)).get(5)
        c.add(col, band, 0.55)
        line = Mask().line(path, 3.0).get(1.0)
        c.add(core, line, 0.7)
        for k in range(26):
            px, py = path[int(rng.integers(4, len(path) - 2))]
            px += rng.normal(0, 7)
            py += rng.normal(0, 7)
            c.glow(px, py, rng.uniform(1.2, 3.0), core, rng.uniform(0.5, 1.0))
    lyse(c, sx, sy, 72, 1.0, "happy", 307)
    for kind, (tx, ty), r, col, core in targets:
        planet(c, tx, ty, r, kind, (sx, sy), 330 + r)
        c.glow(tx, ty, r * 1.6, col, 0.25)
    # The volcano answers with a plume of fire, the forest with fireflies, the ice with glints.
    c.glow(792 + 30, 294 - 40, 10, "#ffb04a", 0.8)
    for k in range(10):
        a = rng.uniform(0, 2 * math.pi)
        c.glow(700 + math.cos(a) * 52, 158 + math.sin(a) * 52, 1.8, "#c8ff9a", 0.9)
        c.glow(690 + math.cos(a) * 44, 420 + math.sin(a) * 44, 1.6, "#ffffff", 0.9)
    return finish(c, bloom=0.62)


def tower(m, x, ground, h, wb, wt, bar=2.4):
    """A lattice tower: two legs and their braces."""
    m.line([(x - wb / 2, ground), (x - wt / 2, ground - h)], bar * 1.4)
    m.line([(x + wb / 2, ground), (x + wt / 2, ground - h)], bar * 1.4)
    n = max(3, int(h / 34))
    for k in range(n + 1):
        t0, t1 = k / n, min(1.0, (k + 1) / n)
        y0, y1 = ground - h * t0, ground - h * t1
        w0, w1 = wb + (wt - wb) * t0, wb + (wt - wb) * t1
        m.line([(x - w0 / 2, y0), (x + w0 / 2, y0)], bar)
        if k < n:
            m.line([(x - w0 / 2, y0), (x + w1 / 2, y1)], bar * 0.8)
            m.line([(x + w0 / 2, y0), (x - w1 / 2, y1)], bar * 0.8)


def dish(m, cx, cy, r, angle):
    """A collecting dish turned towards `angle` (radians): its bowl, its arm and its horn."""
    ca, sa = math.cos(angle), math.sin(angle)
    pts = []
    for i in range(25):
        t = math.pi / 2 + math.pi * i / 24  # the back half of an ellipse: a bowl open towards the angle
        u, v = r * 0.38 * math.cos(t), r * math.sin(t)
        pts.append((cx + u * ca - v * sa, cy + u * sa + v * ca))
    m.poly(pts)
    hx, hy = cx + ca * r * 0.9, cy + sa * r * 0.9
    m.line([(cx - ca * r * 0.1 + sa * r * 0.5, cy - sa * r * 0.1 - ca * r * 0.5), (hx, hy)], max(1.5, r * 0.06))
    m.line([(cx - ca * r * 0.1 - sa * r * 0.5, cy - sa * r * 0.1 + ca * r * 0.5), (hx, hy)], max(1.5, r * 0.06))
    m.circle(hx, hy, r * 0.1)
    return hx, hy


def shot4():
    """Machines came to drink her light, a little more each day: their dishes draw it down."""
    c = Canvas()
    space(c, 401, top="#05030e", mid="#0e0a20", bottom="#1c1222", neb=("#3a2a6a", "#1a2a4a"), neb_k=0.35, stars=500)
    sx, sy, sr = 480, 150, 50
    ground = 470
    rng = np.random.default_rng(409)
    # The machines' plain: low silhouettes of tanks and pipes far away, in the smog.
    far = Mask()
    x = -30
    while x < 990:
        w = rng.uniform(40, 110)
        h = rng.uniform(18, 60)
        far.ellipse(x + w / 2, ground - h * 0.6, w / 2, h * 0.6)
        far.poly([(x, ground - h * 0.6), (x + w, ground - h * 0.6), (x + w, 545), (x, 545)])
        if rng.random() < 0.4:
            far.poly([(x + w * 0.4, ground - h), (x + w * 0.44, ground - h - 70), (x + w * 0.52, ground - h - 70), (x + w * 0.56, ground - h)])
        x += w * rng.uniform(0.8, 1.3)
    fm = far.get(0.6)
    smog = fbm(413, 140, 5)
    c.add("#6a3a4a", smoothstep(330, 470, c.yy) * (0.35 + 0.65 * smog), 0.45)
    c.over("#1c1628", fm)
    c.add("#ffb070", rim(fm, 0, 3, 1.0), 0.2)
    # The towers and their dishes, all turned to her.
    towers = [(130, 230, 0.95), (300, 160, 0.8), (660, 170, 0.82), (830, 240, 1.0)]
    near, lights = Mask(), Mask()
    mouths = []
    for tx, h, k in towers:
        top = ground + 40 - h - 60
        tower(near, tx, ground + 40, h + 40, 70 * k, 22 * k)
        ang = math.atan2(sy - top, sx - tx)
        mouths.append((dish(near, tx, top, 52 * k, ang), 52 * k))
        for j in range(4):
            lights.circle(tx + rng.uniform(-14, 14) * k, top + 60 + j * (h / 4.5), 2.6)
    # Her light pulled into each dish: a beam narrowing from her to the dish's horn.
    for (hx, hy), r in mouths:
        ang = math.atan2(hy - sy, hx - sx)
        nx, ny = -math.sin(ang), math.cos(ang)
        ex, ey = sx + math.cos(ang) * sr, sy + math.sin(ang) * sr
        beam = Mask().poly([(ex + nx * 30, ey + ny * 30), (hx + nx * 5, hy + ny * 5), (hx - nx * 5, hy - ny * 5), (ex - nx * 30, ey - ny * 30)]).get(6)
        along = np.clip(c.dist(hx, hy) / max(1.0, math.hypot(hx - ex, hy - ey)), 0, 1)
        flow = fbm(431 + int(hx), 12, 3)
        c.add("#ffa040", beam * (0.45 + 0.55 * along) * (0.6 + 0.4 * flow), 0.7)
        core = Mask().poly(tapered([(ex, ey), (hx, hy)], 7, 2)).get(1.5)
        c.add("#fff0c0", core, 0.55)
        for j in range(5):
            t = (j + 0.3 + rng.random() * 0.4) / 5.0
            c.glow(ex + (hx - ex) * t, ey + (hy - ey) * t, 3.0, "#ffe8b0", 0.45)
        c.glow(hx, hy, 9, "#ffd070", 0.9)
    lyse(c, sx, sy, sr, 0.72, "worried", 407, hot="#fff0d0", edge="#e09048")
    nm = near.get(0.3)
    c.over(c.gradient_field(0, 200, 0, 520, "#1a2034", "#07080f"), nm)
    c.add("#ffd090", rim(nm, (sx - 480) * 0.0, 2, 0.6), 0.5)
    lm = lights.get(0.3)
    c.add("#4fe0ff", lm, 1.4)
    c.add("#4fe0ff", blur(lm, 4), 1.4)
    # The ground: a dark plain with a few cyan lights of the machines.
    gm = smoothstep(ground - 4, ground + 6, c.yy)
    c.over(c.gradient_field(0, ground, 0, 545, "#100c18", "#050409"), gm)
    for j in range(30):
        c.glow(rng.uniform(0, 960), rng.uniform(ground + 8, 530), rng.uniform(1.0, 2.2), "#4fe0ff", rng.uniform(0.3, 0.8))
    return finish(c, bloom=0.55, vignette=0.5)


def shot5():
    """One evening, only a spark was left: cold, afraid of being alone in the dark."""
    c = Canvas()
    c.vgradient([(0.0, "#020208"), (0.6, "#05060f"), (1.0, "#03030a")])
    c.stars(501, 220, "#9fb0d8", 0.55)
    cx, cy = 480, 282
    for kind, x, y, r in (("forest", 196, 338, 30), ("volcano", 760, 214, 36), ("ice", 664, 424, 22)):
        planet(c, x, y, r, kind, (cx, cy), 510 + r, dead=True)
    mist = fbm(507, 180, 5)
    c.add("#2a3a5a", smoothstep(0.5, 1.0, mist), 0.35)
    rng = np.random.default_rng(513)
    for _ in range(40):  # frost in the dark
        c.glow(rng.uniform(0, 960), rng.uniform(60, 480), rng.uniform(0.8, 1.6), "#bfe0ff", rng.uniform(0.2, 0.5))
    c.glow(cx, cy, 120, "#ff7a3a", 0.08)
    c.glow(cx, cy, 55, "#ff9a5a", 0.14)
    lyse(c, cx, cy, 32, 0.92, "sad", 511, hot="#ffd09a", edge="#ff6a2a", rays=False)
    return finish(c, bloom=0.5, vignette=0.55)


def shot6():
    """She folded in on herself… and woke hungry: the black heart with its burning eye."""
    c = Canvas()
    space(c, 601, top="#040008", mid="#12021c", bottom="#08000c", neb=("#5a1a7a", "#7a1a2a"), neb_k=0.5, stars=600)
    cx, cy, r = 480, 266, 92
    rng = np.random.default_rng(603)
    # Matter spiralling in.
    streaks = Mask()
    for i in range(70):
        a0 = rng.uniform(0, 2 * math.pi)
        d0 = rng.uniform(r * 1.6, 470)
        pts = []
        for k in range(14):
            a = a0 + k * 0.06 * (300.0 / d0)
            d = d0 * (1 - k * 0.018)
            pts.append((cx + math.cos(a) * d, cy + math.sin(a) * d * 0.5))
        streaks.line(pts, rng.uniform(1.0, 2.6), int(rng.uniform(90, 255)))
    sm = streaks.get(1.2)
    c.add("#ff6ad5", sm, 0.45)
    c.add(VIOLET, blur(sm, 4), 0.5)
    tilt = -12
    disc_o = Mask().rotated_ellipse(cx, cy, 270, 64, tilt).get(2)
    disc_i = Mask().rotated_ellipse(cx, cy, 128, 30, tilt).get(2)
    ring = np.clip(disc_o - disc_i, 0, 1)
    swirl = fbm(605, 30, 4)
    hot = np.clip(1.0 - (c.dist(cx, cy, 1.0, 0.25) - 120) / 160.0, 0, 1)
    back = ring * (c.yy < cy + (c.xx - cx) * math.tan(math.radians(tilt)))
    front = ring - back
    def disc(mask):
        c.add("#7a2aff", mask * (0.4 + 0.6 * swirl), 0.8)
        c.add("#ffb0f0", mask * hot * (0.5 + 0.5 * swirl), 0.9)
    disc(back)
    c.glow(cx, cy, r * 1.25, "#c88bff", 0.65, tail=0.2)
    # The black heart.
    body = Mask().circle(cx, cy, r).get()
    c.over("#06030a", body)
    c.add("#c88bff", rim(body, 3, 3, 1.5), 0.7)
    cracks = Mask()
    for i in range(11):
        a = rng.uniform(0, 2 * math.pi)
        pts = [(cx + math.cos(a) * r * 0.42, cy + math.sin(a) * r * 0.42)]
        for k in range(12):
            a += rng.normal(0, 0.16)
            d = r * (0.42 + 0.05 * (k + 1))
            pts.append((cx + math.cos(a) * d, cy + math.sin(a) * d))
            if k == 5 and rng.random() < 0.6:
                b = a + rng.choice([-1, 1]) * 0.5
                cracks.line([pts[-1], (cx + math.cos(b) * d * 1.25, cy + math.sin(b) * d * 1.25)], 1.1)
        cracks.line(pts, rng.uniform(1.1, 2.0))
    cm = cracks.get(0.5) * body
    c.add("#ff2a3a", cm, 1.2)
    c.add("#ff2a3a", blur(cm, 3), 0.9)
    # Its eye, open and hungry.
    eye = Mask().poly(bezier((cx - 54, cy), (cx, cy - 44), (cx + 54, cy), None, 24) + bezier((cx + 54, cy), (cx, cy + 44), (cx - 54, cy), None, 24)).get(0.5)
    c.over("#1a0505", eye)
    iris = Mask().circle(cx, cy, 27).get(0.4) * eye
    c.over(c.gradient_field(cx, cy - 26, cx, cy + 26, "#fff2a0", "#ff5a14"), iris)
    pupil = Mask().ellipse(cx, cy, 6, 22).get(0.4) * eye
    c.over("#050002", pupil)
    c.add("#ff3a2a", rim(eye, 0, 2, 1.0), 0.8)
    c.glow(cx, cy, 34, "#ff5a2a", 0.5)
    c.glow(cx - 8, cy - 10, 3.5, "#ffffff", 0.9)
    disc(front * (1.0 - body * 0.7))
    return finish(c, bloom=0.7, vignette=0.5)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
