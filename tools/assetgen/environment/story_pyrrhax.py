"""Pyrrhax, « Le Dragon d'Obsidienne »: the smallest of a brood of hatchlings, grey, who only
breathes smoke (« la Fumerolle »); the rain of embers when the star collapses; the embers he
swallows; his scales turned to black glass; the giant guarding the last frontier.

2.60: Pyrrhax's story is no longer painted here: it is told in animated scenes
(story_pyrrhax_scene.py, which also renders its stills). This module stays for the painting
helpers the other stories share (collapsed_star, embers, flame, ground, spires…); its SHOTS are the
paintings of version 1, kept for reference and no longer written."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blob, blur, fbm, finish, rgb, rim, smoothstep, tapered

RED = {"body": "#c8401c", "top": "#e86a34", "belly": "#ffb070", "dark": "#5a140a", "horn": "#3a0c08"}
GREY = {"body": "#76747e", "top": "#918f98", "belly": "#bdb8b0", "dark": "#44424c", "horn": "#34323a"}
GLASS = {"body": "#120e18", "top": "#211a2a", "belly": "#1c1422", "dark": "#0a070e", "horn": "#08060c"}


# --------------------------------------------------------------------------- the Confins

def confins_sky(c, seed, horizon=0.78, top="#0c0618", mid="#2a0c2e", low="#7a1e22", glow_k=1.0, stars=500):
    c.vgradient([(0.0, top), (horizon * 0.55, mid), (horizon, low), (1.0, "#1a0608")])
    c.nebula(seed, "#5a1a4a", "#8a2a1a", 0.35, cell=240)
    c.stars(seed + 1, stars, "#ffd8e8", 0.7, region=(0, 0, c.w, c.h * horizon))
    c.glow(c.w * 0.5, c.h * horizon, 420, "#ff5a2a", 0.22 * glow_k, tail=0.6)


def spires(c, seed, base, n, hmin, hmax, color, rim_color="#ff7a4a", rim_k=0.4, x0=-40, x1=1000, light=(0, -3)):
    """Obsidian spires (the Confins' crystals), sharp and leaning a little."""
    rng = np.random.default_rng(seed)
    m = Mask()
    for i in range(n):
        x = rng.uniform(x0, x1)
        h = rng.uniform(hmin, hmax)
        w = h * rng.uniform(0.12, 0.22)
        lean = rng.uniform(-0.12, 0.12) * h
        m.poly([(x - w / 2, base + 10), (x + lean, base - h), (x + w / 2, base + 10)])
        if rng.random() < 0.6:
            h2 = h * rng.uniform(0.4, 0.7)
            m.poly([(x + w * 0.2, base + 10), (x + w * 0.5 + lean * 0.6, base - h2), (x + w * 0.9, base + 10)])
    mm = m.get(0.4)
    c.over(c.gradient_field(0, base - hmax, 0, base, rgb(color) * 1.5, color), mm)
    c.add(rim_color, rim(mm, light[0], light[1], 0.8), rim_k)
    return mm


def ground(c, y, seed, color="#120608", top="#2a0e10"):
    n = fbm(seed, 90, 4)
    edge = y + (n - 0.5) * 18
    m = smoothstep(edge - 1.5, edge + 1.5, c.yy)
    c.over(c.gradient_field(0, y, 0, 545, top, color), m)
    return m


def embers(c, seed, n, region, color="#ffb050", size=(1.2, 3.0), k=1.0):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    for _ in range(n):
        c.glow(rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(*size), color, k * rng.uniform(0.5, 1.0))


def smoke(c, x, y, size, seed, color="#9a9298", light="#ffd0a0", k=0.9):
    """A puff of smoke: round billows, soft, lit from above."""
    rng = np.random.default_rng(seed)
    m = Mask()
    for i in range(9):
        a = rng.uniform(0, 2 * math.pi)
        d = size * rng.uniform(0.0, 0.55)
        m.circle(x + math.cos(a) * d * 1.3, y + math.sin(a) * d * 0.8, size * rng.uniform(0.32, 0.55))
    sm = m.get(size * 0.08) * (0.75 + 0.25 * fbm(seed, max(4.0, size * 0.3), 3))
    c.over(c.gradient_field(x, y - size, x, y + size, light, color), sm, k)
    c.add("#ffffff", rim(sm, 0, 3, 1.5), 0.15)


# --------------------------------------------------------------------------- dragons

class _Shape:
    """Points and ellipses of a creature in its own units, placed (scale, mirror) on the canvas."""

    def __init__(self, x, y, s, flip):
        self.x, self.y, self.s, self.f = x, y, s, -1.0 if flip else 1.0

    def p(self, px, py):
        return (self.x + px * self.s * self.f, self.y + py * self.s)

    def pts(self, ps):
        return [self.p(a, b) for a, b in ps]

    def ell(self, m, px, py, rx, ry):
        cx, cy = self.p(px, py)
        m.ellipse(cx, cy, rx * self.s, ry * self.s)

    def rot_ell(self, m, px, py, rx, ry, ang):
        cx, cy = self.p(px, py)
        m.rotated_ellipse(cx, cy, rx * self.s, ry * self.s, ang * self.f)


def hatchling(c, x, y, s, pal, flip=False, mouth=0.0, light=(0, 1), light_color="#ff9a4a", light_k=0.6, look_up=0.0, glow_belly=0.0):
    """A dragon hatchling (side view, facing right unless `flip`): a big round head, a chubby body,
    little folded wings, a curled tail. `mouth` opens the jaw (0..1); `glow_belly` lights the belly
    from inside like a forge. Returns the mouth's position."""
    S = _Shape(x, y, s, flip)
    body, belly, dark, horn = Mask(), Mask(), Mask(), Mask()
    hy = -26 - look_up * 8
    # tail
    tail = bezier((-22, 6), (-52, 22), (-74, 6), (-66, -12), 20)
    body.poly(S.pts(tapered(tail, 14, 3)))
    dark.poly(S.pts([(-66, -12), (-76, -22), (-60, -18)]))
    # back leg, body
    S.ell(body, -16, 14, 11, 11)
    S.ell(body, -16, 24, 12, 4)
    S.rot_ell(body, 0, 0, 30, 21, -6)
    # folded wing
    dark.poly(S.pts([(-4, -14), (-34, -40), (-26, -26), (-40, -30), (-24, -16), (-32, -14), (-12, -6)]))
    # belly, front leg
    S.rot_ell(belly, 8, 7, 19, 12, -10)
    S.ell(body, 16, 16, 7, 10)
    S.ell(body, 18, 24, 8, 3.5)
    # back spikes
    for i, (bx, by) in enumerate(((-24, -14), (-14, -19), (-3, -21))):
        dark.poly(S.pts([(bx - 4, by + 2), (bx, by - 7), (bx + 4, by + 2)]))
    # head: a big round skull, a snout, an opening jaw
    S.ell(body, 28, hy, 21, 18)
    jaw = mouth * 9
    S.rot_ell(body, 47, hy + 6, 14, 8.5, -8 - mouth * 6)
    if mouth > 0.05:
        S.rot_ell(body, 44, hy + 15 + jaw * 0.6, 12, 4.5, 18 * mouth)
        S.rot_ell(dark, 50, hy + 11 + jaw * 0.3, 8, 2 + jaw * 0.45, 8 * mouth)
    S.rot_ell(belly, 40, hy + 13 + jaw * 0.3, 12, 4.5, 10)
    # horns and spikes on the head
    horn.poly(S.pts([(16, hy - 13), (6, hy - 28), (22, hy - 16)]))
    horn.poly(S.pts([(26, hy - 17), (22, hy - 32), (32, hy - 17)]))
    bm, blm, dm, hm = body.get(0.3), belly.get(0.3), dark.get(0.3), horn.get(0.3)
    full = np.clip(bm + dm + hm, 0, 1)
    # an ink outline, the colours, a soft shading from above, the light's rim
    c.over("#0a0406", np.clip(blur(full, 1.1) * 1.8, 0, 1), 0.85)
    c.over(c.gradient_field(0, y + hy * s - 20 * s, 0, y + 30 * s, pal["top"], pal["body"]), bm)
    c.over(pal["dark"], dm)
    c.over(pal["horn"], hm)
    c.over(c.gradient_field(0, y - 10 * s, 0, y + 20 * s, pal["belly"], pal["body"]), blm * bm, 0.9)
    if glow_belly > 0:
        gx, gy = S.p(6, 4)
        inner = np.exp(-(((c.xx - gx) / (26 * s)) ** 2 + ((c.yy - gy) / (20 * s)) ** 2))
        c.multiply("#ff8a3a", inner * bm, 0.6 * glow_belly)
        c.add("#ff6a10", inner * bm, 0.9 * glow_belly)
        c.add("#ffd060", inner ** 4 * bm, 0.9 * glow_belly)
        c.glow(gx, gy, 34 * s, "#ff7a2a", 0.35 * glow_belly)
    c.add(light_color, rim(full, -light[0] * 2.2 * s, -light[1] * 2.2 * s, 0.9) * full, light_k)
    # eye: big and bright (a baby), its highlight
    ex, ey = S.p(33, hy - 3)
    eye = Mask().ellipse(ex, ey, 6.2 * s, 7.4 * s).get(0.3)
    c.over("#fff6e0", eye)
    pupil = Mask().ellipse(ex + 1.6 * s * S.f, ey + 0.6 * s - look_up * 1.6 * s, 3.6 * s, 5.2 * s).get(0.3)
    c.over("#1a0a06", pupil * eye)
    hl = Mask().circle(ex + 2.6 * s * S.f, ey - 2.6 * s - look_up * 1.2 * s, 1.5 * s).get(0.2)
    c.over("#ffffff", hl)
    nx, ny = S.p(56, hy + 1)
    c.over("#1a0a06", Mask().circle(nx, ny, 1.4 * s).get(0.3), 0.8)
    return S.p(60, hy + 8 + jaw * 0.4)


def wing_shape(S, m, bones, off=(0.0, 0.0), k=1.0, spread=1.0):
    """A bat wing raised behind the shoulder: arm and wrist, four fingers fanning backwards,
    a scalloped membrane down to the back."""
    ox, oy = off

    def P(x, y):
        return (ox + x * k, oy + y * k * spread)

    sh, el, wr = P(8, -20), P(22, -72), P(-8, -130)
    tips = [P(-92, -170), P(-142, -134), P(-166, -80), P(-136, -28)]
    back = P(-44, -12)
    poly = [sh, el, wr, tips[0]]
    for i in range(len(tips) - 1):
        a, b = tips[i], tips[i + 1]
        mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        poly.append((mid[0] + (wr[0] - mid[0]) * 0.28, mid[1] + (wr[1] - mid[1]) * 0.28))
        poly.append(b)
    last = tips[-1]
    poly.append(((last[0] + back[0]) / 2 + 6, (last[1] + back[1]) / 2 - 10))
    poly.append(back)
    m.poly(S.pts(poly))
    if bones is not None:
        bones.line(S.pts([sh, el, wr]), 3.4 * S.s)
        for t in tips:
            bones.line(S.pts([wr, t]), 2.0 * S.s)


def dragon(c, x, y, s, pal, flip=False, wings=1.0, light=(0, -1), light_color="#ff7a4a", light_k=0.5, cracks=0.0,
           seed=1, eye_color="#ffb030", mouth=0.0, neck_up=0.0):
    """A grown dragon (side view, facing right unless `flip`): a deep chest, an S neck, a horned
    head, wings raised (`wings`: their spread), a long tail; `cracks` lights the fire veins of its
    black-glass scales."""
    S = _Shape(x, y, s, flip)
    body, wing, far, horn, bones = Mask(), Mask(), Mask(), Mask(), Mask()
    fbones = Mask()
    wing_shape(S, far, fbones, (34, -18), 0.78, wings)
    tail = bezier((-44, 2), (-104, 34), (-158, 18), (-194, -18), 30)
    body.poly(S.pts(tapered(tail, 28, 4)))
    horn.poly(S.pts([(-194, -18), (-212, -26), (-200, -6), (-186, -10)]))
    # hind leg, front leg, claws
    S.ell(body, -30, 8, 22, 18)
    body.poly(S.pts([(-40, 14), (-48, 40), (-40, 48), (-22, 48), (-28, 38), (-16, 16)]))
    body.poly(S.pts([(26, 10), (22, 36), (30, 48), (44, 48), (36, 34), (42, 12)]))
    for fx in (-42, -34, -26, 30, 38, 46):
        horn.poly(S.pts([(fx - 3, 47), (fx + 1, 54), (fx + 4, 47)]))
    # body and chest
    S.rot_ell(body, -6, 0, 54, 24, -4)
    S.rot_ell(body, 28, -6, 28, 25, -20)
    # S-shaped neck, head
    hx, hy = 84, -78 - neck_up * 10
    neck = bezier((34, -14), (62, -24), (58, -58 - neck_up * 6), (hx - 6, hy + 4), 18)
    body.poly(S.pts(tapered(neck, 34, 18)))
    S.rot_ell(body, hx, hy, 17, 12, -8)
    body.poly(S.pts([(hx + 2, hy - 9), (hx + 38, hy + 1), (hx + 38, hy + 7), (hx + 4, hy + 8)]))
    jaw = mouth * 12
    body.poly(S.pts([(hx, hy + 6), (hx + 34, hy + 9 + jaw), (hx + 31, hy + 14 + jaw), (hx - 2, hy + 12)]))
    horn.poly(S.pts([(hx - 6, hy - 6), (hx - 40, hy - 26), (hx - 2, hy - 12)]))
    horn.poly(S.pts([(hx + 2, hy - 9), (hx - 26, hy - 36), (hx + 8, hy - 11)]))
    # spikes along the back, the neck and the tail
    for i in range(12):
        t = i / 11.0
        px, py = bezier((-150, 10), (-80, 16), (-10, -30), (60, -44), 11)[i]
        horn.poly(S.pts([(px - 5, py + 4), (px - 1, py - 9 - 4 * math.sin(t * 3)), (px + 5, py + 4)]))
    wing_shape(S, wing, bones, (0, 0), 1.0, wings)
    fm, bm, wm, hm, bnm = far.get(0.3), body.get(0.3), wing.get(0.3), horn.get(0.3), bones.get(0.3)
    full = np.clip(bm + wm + hm, 0, 1)
    c.over(c.gradient_field(0, y - 170 * s, 0, y, pal["top"], pal["dark"]), fm, 0.7)
    c.over(pal["top"], fbones.get(0.3) * fm, 0.5)
    c.add(light_color, rim(fm, -light[0] * 2, -light[1] * 2, 0.8) * fm, light_k * 0.6)
    c.over("#050204", np.clip(blur(full, 1.0) * 1.8, 0, 1), 0.9)
    c.over(c.gradient_field(0, y - 170 * s, 0, y, pal["top"], pal["dark"]), wm)
    c.over(pal["top"], bnm * wm, 0.9)
    c.over(c.gradient_field(0, y - 80 * s, 0, y + 40 * s, pal["top"], pal["body"]), bm)
    c.over(pal["horn"], hm)
    if cracks > 0:
        n = fbm(seed, 18.0 * s, 4)
        n2 = fbm(seed + 1, 40.0 * s, 3)
        veins = smoothstep(0.03, 0.0, np.abs(n - 0.5)) * smoothstep(0.35, 0.7, n2) * np.clip(bm + wm * 0.4, 0, 1)
        c.add("#ff5a1a", veins, 1.5 * cracks)
        c.add("#ffb040", blur(veins, 2.5), 1.0 * cracks)
        # glass: cold sheens along the curves
        sheen = smoothstep(0.62, 0.8, fbm(seed + 2, 30.0 * s, 3)) * bm
        c.add("#9a8ac8", sheen, 0.1 * cracks)
        c.add("#d8d0ff", rim(bm, 0, 2.0 * s, 0.5) * bm, 0.55 * cracks)
        c.add("#d8d0ff", rim(wm, 0, 2.0 * s, 0.5) * wm, 0.3 * cracks)
    c.add(light_color, rim(full, -light[0] * 2.5, -light[1] * 2.5, 0.9) * full, light_k)
    ex, ey = S.p(hx + 6, hy - 2)
    eye = Mask().rotated_ellipse(ex, ey, 5 * s, 2.2 * s, -12 * S.f).get(0.3)
    c.over(eye_color, eye)
    c.glow(ex, ey, 6 * s, eye_color, 0.8)
    return S.p(hx + 40, hy + 8 + jaw * 0.5)


def flame(c, x, y, angle, length, width, seed, k=1.0):
    """A jet of fire: heat accumulated along the breath, broken by turbulence, coloured by heat
    (dark red, orange, yellow, white)."""
    ca, sa = math.cos(angle), math.sin(angle)
    R = int(length + width) + 8
    x0, x1 = max(0, int(x - R)), min(c.w, int(x + R))
    y0, y1 = max(0, int(y - R)), min(c.h, int(y + R))
    xx, yy = c.xx[y0:y1, x0:x1], c.yy[y0:y1, x0:x1]
    heat = np.zeros_like(xx)
    rng = np.random.default_rng(seed)
    for i in range(46):
        u = i / 45.0
        wob = math.sin(u * 9.0 + seed) * width * 0.12 * u
        px = x + ca * length * u - sa * wob
        py = y + sa * length * u + ca * wob
        r = width * (0.12 + 0.5 * u ** 0.8) * (1.0 - 0.35 * u ** 3)
        heat += np.exp(-(((xx - px) ** 2 + (yy - py) ** 2) / (r * r))) * (1.1 - 0.75 * u) * 0.32
    turb = fbm(seed, max(4.0, width * 0.35), 4)[y0:y1, x0:x1]
    heat = heat * (0.45 + 1.0 * turb)
    stops = [(0.12, (0.0, 0.0, 0.0)), (0.3, rgb("#a8140a")), (0.55, rgb("#ff5a14")), (0.85, rgb("#ffb030")),
             (1.15, rgb("#ffe890")), (1.6, rgb("#fffbe8"))]
    col = np.zeros(xx.shape + (3,), np.float32)
    for ch in range(3):
        col[..., ch] = np.interp(heat, [s[0] for s in stops], [s[1][ch] for s in stops])
    c.img[y0:y1, x0:x1] += col * k
    c.glow(x + ca * length * 0.45, y + sa * length * 0.45, length * 0.4, "#ff5a1a", 0.35 * k)


def collapsed_star(c, x, y, r):
    """The star of the centre, collapsed: a black heart in a violet ring (far away)."""
    c.glow(x, y, r * 4, "#c88bff", 0.35)
    ring = np.clip(Mask().rotated_ellipse(x, y, r * 2.6, r * 0.7, -12).get(1) - Mask().rotated_ellipse(x, y, r * 1.4, r * 0.34, -12).get(1), 0, 1)
    c.add("#e0a8ff", ring, 0.9)
    body = Mask().circle(x, y, r).get(0.4)
    c.over("#06030a", body)
    c.add("#c88bff", rim(body, 2, 2, 1.0), 0.8)
    c.glow(x, y, r * 0.3, "#ff3a2a", 0.6)


# --------------------------------------------------------------------------- the six shots

def shot1():
    """In a nest of warm ashes, the smallest of a brood of hatchlings breaks its shell."""
    c = Canvas()
    confins_sky(c, 1101, horizon=0.72)
    spires(c, 1103, 400, 14, 120, 300, "#1c0c24", "#ff6a3a", 0.35)
    ground(c, 392, 1105, "#100406", "#2a0c0c")
    cx, cy = 480, 410
    # the nest: a ring of dark rocks round a bed of glowing ashes
    bed = Mask().ellipse(cx, cy, 250, 62).get(2)
    ash = fbm(1107, 14, 4)
    c.over(c.gradient_field(0, cy - 60, 0, cy + 60, "#5a2010", "#2a0a06"), bed)
    hot = smoothstep(0.62, 0.9, ash) * bed
    c.add("#ff6a1a", hot, 1.2)
    c.add("#ffc060", smoothstep(0.82, 0.95, ash) * bed, 1.0)
    c.glow(cx, cy - 10, 200, "#ff7a2a", 0.4, tail=0.5)
    rocks = Mask()
    for i in range(22):
        a = math.pi * (i / 21.0)
        rx, ry = cx - math.cos(a) * 262, cy + math.sin(a) * 66
        for p in blob(rx, ry, 22 + (i % 3) * 6, 1110 + i, 12, 0.25, 0.75):
            pass
        rocks.poly(blob(rx, ry, 22 + (i % 3) * 6, 1110 + i, 12, 0.25, 0.75))
    for i in range(7):
        a = math.pi * (1 + (i + 0.5) / 7.0)
        rocks.poly(blob(cx - math.cos(a) * 252, cy + math.sin(a) * 60, 16, 1140 + i, 12, 0.25, 0.75))
    rm = rocks.get(0.5)
    c.over(c.gradient_field(0, cy - 80, 0, cy + 90, "#3a2026", "#120608"), rm)
    c.add("#ff8a3a", rim(rm, 0, -2.5, 0.9), 0.55)
    # the brood: three red hatchlings around, the small grey one in its shell, in front
    hatchling(c, 320, 366, 1.45, RED, light=(0, 1), light_k=0.8)
    hatchling(c, 652, 360, 1.5, RED, flip=True, light=(0, 1), light_k=0.8)
    hatchling(c, 566, 326, 1.2, RED, flip=True, light=(0, 1), light_k=0.7, look_up=0.5)
    # its shell (the back half), the hatchling, the front half
    shell = Mask().poly([(436, 436), (430, 406), (444, 386), (452, 396), (462, 380), (474, 394), (488, 378),
                         (500, 392), (512, 382), (522, 398), (530, 410), (524, 436)]).get(0.4)
    c.over("#d8c8b0", shell)
    c.add("#ffb070", rim(shell, 0, 2, 0.8), 0.4)
    hatchling(c, 478, 404, 0.85, GREY, light=(0, 1), light_color="#ffa050", light_k=0.9, look_up=0.6)
    front = Mask().poly([(432, 420), (448, 414), (460, 424), (474, 412), (488, 424), (504, 412), (518, 422), (530, 418),
                         (536, 440), (528, 456), (440, 456), (426, 440)]).get(0.4)
    c.over(c.gradient_field(0, 412, 0, 456, "#efe2cc", "#a89078"), front)
    for sx, sy in ((448, 440), (470, 448), (506, 436), (520, 448)):
        c.over("#7a6450", Mask().circle(sx, sy, 2.2).get(0.4), 0.8)
    c.glow(480, 430, 60, "#ffb060", 0.25)
    embers(c, 1150, 40, (200, 200, 760, 430), "#ffb050", (1.0, 2.6))
    return finish(c, bloom=0.55, vignette=0.5)


def shot2():
    """His brothers spat flames; he only blew a little cloud of smoke: « la Fumerolle »."""
    c = Canvas()
    confins_sky(c, 1201, horizon=0.74, glow_k=1.3)
    spires(c, 1203, 420, 10, 140, 320, "#1c0c24", "#ff6a3a", 0.3)
    ground(c, 410, 1205, "#120406", "#301010")
    # the brothers, proud, breathing fire to the sky
    m1 = hatchling(c, 600, 372, 1.45, RED, mouth=1.0, light=(1, -1), light_color="#ffc070", light_k=0.7, look_up=1.0)
    flame(c, m1[0], m1[1] - 6, math.radians(-38), 230, 70, 1207)
    m2 = hatchling(c, 760, 392, 1.3, RED, mouth=1.0, light=(1, -1), light_color="#ffc070", light_k=0.7, look_up=1.0)
    flame(c, m2[0], m2[1] - 4, math.radians(-30), 170, 56, 1209)
    # la Fumerolle, small and grey, its puff of smoke
    mf = hatchling(c, 300, 392, 1.15, GREY, mouth=0.7, light=(1, -1), light_color="#ffb070", light_k=0.7)
    smoke(c, mf[0] + 44, mf[1] - 16, 34, 1211)
    smoke(c, mf[0] + 18, mf[1] - 4, 16, 1213)
    c.glow(700, 200, 260, "#ff7a2a", 0.25)
    embers(c, 1215, 30, (500, 60, 960, 360), "#ffd070", (1.0, 2.4))
    return finish(c, bloom=0.6, vignette=0.48)


def shot3():
    """The night the star of the centre collapsed, a rain of embers fell on the Confins."""
    c = Canvas()
    confins_sky(c, 1301, horizon=0.8, top="#0a0414", mid="#240a2a", low="#6a1a1a")
    collapsed_star(c, 520, 120, 26)
    rng = np.random.default_rng(1303)
    trails = Mask()
    heads = []
    for i in range(70):
        x = rng.uniform(-100, 1000)
        y = rng.uniform(-40, 420)
        L = rng.uniform(30, 110)
        ang = math.radians(rng.uniform(62, 78))
        x2, y2 = x + math.cos(ang) * L, y + math.sin(ang) * L
        trails.poly(tapered([(x, y), (x2, y2)], 0.4, rng.uniform(1.8, 3.6)), v=int(rng.uniform(120, 255)))
        heads.append((x2, y2, rng.uniform(1.6, 3.4)))
    tm = trails.get(0.7)
    c.add("#ff7a2a", tm, 0.9)
    c.add("#ff5a1a", blur(tm, 3), 0.8)
    for x2, y2, r in heads:
        c.glow(x2, y2, r, "#ffd080", 1.0)
    spires(c, 1305, 440, 16, 120, 330, "#170a1e", "#ff7a3a", 0.5)
    ground(c, 430, 1307, "#0e0406", "#2a0c0c")
    # small embers already burning on the ground
    for i in range(14):
        x, y = rng.uniform(40, 920), rng.uniform(440, 520)
        c.glow(x, y, rng.uniform(2, 4), "#ff8a3a", 0.9)
    # the little grey one, alone on a rock, looking up
    rock = Mask().poly(blob(486, 452, 46, 1309, 14, 0.2, 0.55)).get(0.5)
    c.over("#24121a", rock)
    c.add("#ff8a4a", rim(rock, 0, 2.5, 0.8), 0.6)
    hatchling(c, 480, 418, 0.7, GREY, light=(0, -1), light_color="#ffa060", light_k=0.9, look_up=1.0)
    return finish(c, bloom=0.62, vignette=0.5)


def shot4():
    """He swallowed them one by one, until his belly glowed like a forge."""
    c = Canvas()
    c.vgradient([(0.0, "#0a0410"), (0.6, "#1e0814"), (1.0, "#2a0a0a")])
    c.nebula(1401, "#4a1030", "#6a2010", 0.3, cell=200)
    c.glow(480, 300, 320, "#ff4a1a", 0.2, tail=0.6)
    ground(c, 450, 1403, "#0e0406", "#2a0e0c")
    spires(c, 1402, 452, 12, 120, 300, "#1a0a1e", "#ff6a3a", 0.3)
    ground(c, 446, 1404, "#0e0406", "#2a0e0c")
    mouth = hatchling(c, 440, 362, 2.6, GREY, mouth=0.9, light=(0.6, 0.3), light_color="#ffa050", light_k=0.7,
                      look_up=0.7, glow_belly=0.6)
    rng = np.random.default_rng(1405)
    # embers drifting to his mouth, with their trails
    for i in range(9):
        a = math.radians(rng.uniform(-80, 10))
        d = rng.uniform(60, 330)
        ex, ey = mouth[0] + math.cos(a) * d + 30, mouth[1] + math.sin(a) * d * 0.8 - 20
        tr = Mask().poly(tapered(bezier((ex + 30, ey - 26), (ex + 18, ey - 8), (ex, ey), None, 8), 0.5, 4)).get(1.2)
        c.add("#ff8a3a", tr, 0.8)
        c.glow(ex, ey, rng.uniform(3, 6), "#ffd080", 1.0)
    c.glow(mouth[0] + 6, mouth[1] - 4, 9, "#ffe0a0", 1.0)
    return finish(c, bloom=0.65, vignette=0.5)


def shot5():
    """A dying star's fire is not made for a little dragon: his scales turned to black glass."""
    c = Canvas()
    c.vgradient([(0.0, "#12040a"), (0.55, "#3a0a10"), (1.0, "#14040a")])
    c.nebula(1501, "#7a1a10", "#3a0a2a", 0.45, cell=180)
    c.glow(480, 280, 360, "#ff3a1a", 0.3, tail=0.6)
    collapsed_star(c, 150, 120, 18)
    spires(c, 1502, 476, 14, 100, 260, "#1e0a14", "#ff5a2a", 0.3)
    ground(c, 470, 1503, "#0a0204", "#2a0808")
    dragon(c, 520, 372, 1.35, GLASS, wings=0.95, light=(0, 1), light_color="#ff6a3a", light_k=0.8, cracks=1.0, seed=1505,
           neck_up=0.4)
    rng = np.random.default_rng(1507)
    # shards of glass and sparks flying off
    for i in range(18):
        x, y = rng.uniform(160, 820), rng.uniform(90, 440)
        sh = Mask().poly(blob(x, y, rng.uniform(3, 7), 1510 + i, 5, 0.6)).get(0.3)
        c.over("#1a1420", sh)
        c.add("#ff9a5a", rim(sh, 0, 1.5, 0.5), 0.8)
    embers(c, 1509, 50, (100, 60, 860, 470), "#ff9040", (1.0, 2.6))
    return finish(c, bloom=0.6, vignette=0.5)


def shot6():
    """Since then, he guards the last frontier: no one will ever laugh at his fire again."""
    c = Canvas()
    confins_sky(c, 1601, horizon=0.82, top="#0a0414", mid="#2c0a2a", low="#8a2418", glow_k=1.2)
    # the red moon behind him
    mx, my, mr = 620, 200, 120
    c.glow(mx, my, mr * 1.6, "#ff4a2a", 0.35, tail=0.5)
    moon = Mask().circle(mx, my, mr).get(0.5)
    mtex = fbm(1603, 30, 5)
    c.over(c.gradient_field(mx - mr, my - mr, mx + mr, my + mr, "#ffb080", "#c83a2a") * (0.85 + 0.25 * mtex[..., None]), moon)
    spires(c, 1605, 470, 18, 100, 260, "#1a0a20", "#ff6a3a", 0.35)
    # the frontier: a gate of obsidian pillars and its glowing seal, far below on the left
    gate = Mask()
    gate.poly([(120, 470), (128, 300), (150, 290), (160, 470)])
    gate.poly([(300, 470), (306, 292), (326, 300), (338, 470)])
    gate.poly([(112, 310), (230, 262), (346, 306), (346, 322), (230, 280), (112, 326)])
    gm = gate.get(0.5)
    c.over("#14081a", gm)
    c.add("#ff7a4a", rim(gm, 2, 0, 0.8), 0.5)
    seal = Mask().poly([(160, 470), (160, 330), (230, 296), (300, 330), (300, 470)]).get(4)
    c.add("#ff5a2a", seal * (0.4 + 0.6 * fbm(1607, 12, 3)), 0.35)
    # his rock, a tall spire on the right
    peak = Mask().poly([(540, 545), (630, 384), (700, 360), (770, 376), (870, 545)]).get(0.6)
    c.over(c.gradient_field(0, 300, 0, 545, "#2a1420", "#0a0408"), peak)
    c.add("#ff7a4a", rim(peak, -2, 0, 0.8), 0.5)
    dragon(c, 706, 306, 1.2, GLASS, flip=True, wings=1.0, light=(1, 0.2), light_color="#ff7a3a", light_k=0.9, cracks=0.8, seed=1609,
           neck_up=0.6)
    ground(c, 470, 1611, "#0a0204", "#1e0808")
    embers(c, 1613, 60, (0, 80, 960, 480), "#ff9a4a", (1.0, 2.6))
    return finish(c, bloom=0.6, vignette=0.5)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
