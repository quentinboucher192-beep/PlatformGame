"""La Liche des Catacombes, « Le Gardien des noms » (2.57): under the Confins lie catacombs of
black glass; old Ossian lit a candle there for each of the dead; he knew all their names by heart
(« as long as someone remembers you, you are never quite gone »); the night the star of the centre
collapsed, the living fled to the stars and Ossian stayed with his dead; he grew old: who would
remember them after him? He locked his soul in a crystal, never to die; the centuries gnawed his
flesh to the bones, but in the crystal his soul still burned, a green flame; the Liche still
watches over its crypt: it has forgotten its own name, but not a single one of theirs."""
import math

import numpy as np

from story_paint import Canvas, Mask, bezier, blob, blur, fbm, finish, rgb, rim, smoothstep, tapered
from story_monolithe import ship
from story_pyrrhax import collapsed_star

CANDLE = "#ffcf7a"   # the candles of the living: warm
SOUL = "#b4f566"     # the soul fire (the Liche's first phase): sickly green
SOUL_HOT = "#eeffd6"
VIOLET = "#cf8eff"
GLASS = "#0c0a12"    # the black glass of the catacombs
ROBE = {"top": "#5a5a78", "body": "#3a3a52", "dark": "#14141e"}       # Ossian's robe, grey-blue
LICH_ROBE = {"top": "#6c5a84", "body": "#352a48", "dark": "#120d1a"}  # the Liche's (boss_i_liche.py)


# --------------------------------------------------------------------------- the catacombs

def catacombs(c, seed, floor_y=448, wall=GLASS, rows=3, light="#ffcf7a", light_k=0.6, skulls=True, x0=-20, x1=980, top=40):
    """A gallery of the catacombs: a wall of black glass carved with arched niches in rows, a
    skull resting in most of them; the floor below. The niches catch `light` on their rims."""
    n = fbm(seed, 110, 6)
    n2 = fbm(seed + 1, 44, 3)
    base = c.gradient_field(0, 0, 0, 540, "#1a1626", "#06050a")
    c.img = base * (0.6 + 0.5 * n[..., None]) * (0.9 + 0.2 * n2[..., None])
    rng = np.random.default_rng(seed + 2)
    nw, nh, gap = 52, 64, 22
    for row in range(rows):
        y = top + 30 + row * (nh + 36)
        if y + nh > floor_y - 20:
            break
        off = (row % 2) * (nw + gap) * 0.5
        x = x0 + off
        while x < x1:
            m = Mask()
            m.poly([(x, y + nh), (x, y + nw * 0.5), (x + nw, y + nw * 0.5), (x + nw, y + nh)])
            m.ellipse(x + nw * 0.5, y + nw * 0.5, nw * 0.5, nw * 0.5)
            nm = m.get(0.6)
            c.over("#030206", nm, 0.92)
            c.add(light, rim(nm, -2, -2, 0.9) * (1 - nm), light_k * 0.35)
            c.add("#4a3e64", rim(nm, 2, 2, 0.9) * (1 - nm), 0.35)
            if skulls and rng.random() < 0.8:
                skull(c, x + nw * 0.5 + rng.uniform(-6, 6), y + nh - 12, rng.uniform(7.5, 9.5), light=light, k=light_k)
            x += nw + gap
    fl = smoothstep(floor_y - 4, floor_y + 4, c.yy + (fbm(seed + 5, 60, 3) - 0.5) * 8)
    c.over(c.gradient_field(0, floor_y, 0, 545, "#221c2c", "#08060c"), fl)
    c.add(light, smoothstep(floor_y + 2, floor_y - 2, c.yy) * smoothstep(floor_y - 8, floor_y, c.yy), light_k * 0.25)


def skull(c, x, y, r, light=CANDLE, k=0.6, eyes=None, eye_k=1.0):
    """A skull seen from the front: ivory, two hollow eyes (glowing with `eyes`), a jaw."""
    m = Mask().ellipse(x, y - r * 0.15, r, r * 0.92)
    m.poly([(x - r * 0.55, y + r * 0.45), (x + r * 0.55, y + r * 0.45), (x + r * 0.42, y + r * 1.05), (x - r * 0.42, y + r * 1.05)])
    sm = m.get(0.4)
    c.over(c.gradient_field(x - r, y - r, x + r, y + r, "#e8dcc0", "#6a5e4a"), sm)
    c.add(light, rim(sm, -1.5, -1.5, 0.6) * sm, 0.3 * k)
    holes = Mask().ellipse(x - r * 0.38, y, r * 0.27, r * 0.3).ellipse(x + r * 0.38, y, r * 0.27, r * 0.3)
    holes.poly([(x, y + r * 0.3), (x - r * 0.12, y + r * 0.52), (x + r * 0.12, y + r * 0.52)])
    hm = holes.get(0.3)
    c.over("#0a0608", hm)
    for i in range(4):
        tx = x - r * 0.33 + i * r * 0.22
        c.over("#2a2218", Mask().line([(tx, y + r * 0.7), (tx, y + r * 1.0)], max(0.6, r * 0.06)).get(0.3), 0.8)
    if eyes:
        for sx in (-1, 1):
            c.glow(x + sx * r * 0.38, y, r * 0.22, eyes, 1.2 * eye_k)
            c.glow(x + sx * r * 0.38, y, r * 0.8, eyes, 0.25 * eye_k)


def candle(c, x, y, h=14.0, w=4.0, color=CANDLE, k=1.0, seed=0):
    """A candle standing at (x, y): a stick of wax with drips, its flame and its light."""
    m = Mask().poly([(x - w / 2, y), (x - w / 2, y - h), (x + w / 2, y - h), (x + w / 2, y)])
    m.ellipse(x - w / 2 + 0.6, y - h * 0.5, 0.9, h * 0.25)
    wm = m.get(0.3)
    c.over(c.gradient_field(x - w, 0, x + w, 0, "#f0e6cc", "#9a8a6a"), wm)
    fy = y - h - w * 1.1
    flame = Mask().poly([(x, fy - w * 1.6), (x + w * 0.55, fy), (x, fy + w * 0.7), (x - w * 0.55, fy)]).get(0.6)
    c.add(color, flame, 1.4 * k)
    c.add("#ffffff", Mask().ellipse(x, fy + w * 0.1, w * 0.22, w * 0.5).get(0.4), 0.9 * k)
    c.glow(x, fy, w * 1.2, color, 0.9 * k)
    c.glow(x, fy, w * 6.0, color, 0.12 * k, tail=0.6)


def candles(c, seed, n, region, color=CANDLE, k=1.0, size=(8, 18)):
    """Many candles standing on a floor or a ledge (x0, x1, y, depth spread)."""
    rng = np.random.default_rng(seed)
    x0, x1, y, dy = region
    pts = sorted(((rng.uniform(x0, x1), y + rng.uniform(-dy, dy)) for _ in range(n)), key=lambda p: p[1])
    for (x, yy) in pts:
        candle(c, x, yy, rng.uniform(*size), rng.uniform(2.6, 3.8), color, k * rng.uniform(0.7, 1.0))


def motes(c, seed, n, region, color=SOUL, k=0.8, size=(0.8, 2.0)):
    rng = np.random.default_rng(seed)
    x0, y0, x1, y1 = region
    for _ in range(n):
        c.glow(rng.uniform(x0, x1), rng.uniform(y0, y1), rng.uniform(*size), color, rng.uniform(0.3, 1.0) * k)


def names_wall(c, seed, x0, y0, x1, y1, color=CANDLE, k=1.0, lit=None, cols=4):
    """A wall of names: tablets of carved stone side by side, each engraved with rows of names
    (words suggested by little runic strokes), a faint light in their grooves; `lit` (x, y, r):
    the names near that point burn brighter."""
    rng = np.random.default_rng(seed)
    tw = (x1 - x0) / cols
    plates = np.zeros((c.h, c.w), np.float32)
    strokes = Mask()
    for i in range(cols):
        px0, px1 = x0 + i * tw + 5, x0 + (i + 1) * tw - 5
        pm = Mask().poly([(px0, y0 + 14), (px1, y0 + 14), (px1, y1), (px0, y1)])
        pm.ellipse((px0 + px1) / 2, y0 + 14, (px1 - px0) / 2, 14)
        plate = pm.get(0.8)
        plates = np.maximum(plates, plate)
        c.over(c.gradient_field(px0, y0, px1, y1, "#2c2638", "#120e1a"), plate)
        c.add("#7a6a9a", rim(plate, -2, -2, 1.0) * plate, 0.35)
        c.over("#06040a", rim(plate, 2, 2, 1.2) * (1 - plate), 0.6)
        y = y0 + 34
        while y < y1 - 12:
            x = px0 + 12
            while x < px1 - 24:
                wlen = rng.uniform(22, 50)
                if x + wlen > px1 - 10:
                    break
                for j in range(int(wlen / 6)):
                    sx = x + j * 6
                    kind = rng.integers(0, 3)
                    if kind == 0:
                        strokes.line([(sx, y - 3), (sx, y + 3)], 1.1)
                    elif kind == 1:
                        strokes.line([(sx - 1.6, y + 3), (sx + 1.6, y - 3)], 1.1)
                    else:
                        strokes.line([(sx - 1.6, y - 2), (sx + 1.6, y - 2)], 1.1)
                x += wlen + rng.uniform(12, 20)
            y += 21
    sm = strokes.get(0.7) * plates
    glow = np.ones_like(sm) * 0.4
    if lit is not None:
        lx, ly, lr = lit
        glow = glow + 1.5 * np.exp(-((c.xx - lx) ** 2 + (c.yy - ly) ** 2) / (lr * lr))
    c.add(color, sm * glow, 0.8 * k)
    c.add(color, blur(sm * glow, 2.5), 0.5 * k)


# --------------------------------------------------------------------------- figures

def keeper(c, x, y, s, pose="stand", flip=False, light=(1, -0.4), light_color=CANDLE, light_k=0.7, age=0.0, pal=ROBE):
    """Ossian, the keeper of the catacombs: a long hooded robe, a long white beard, thin hands;
    side view facing right (left if `flip`). (x, y) = his feet. `pose`: stand, reach (an arm raised
    forward: lighting a candle, touching a name), hold (a candle held at his chest), kneel (on his
    knees, both hands raised). `age` bends his back. Returns the point of his front hand."""
    f = -1.0 if flip else 1.0

    def P(px, py):
        return (x + px * s * f, y + py * s)

    bend = 4.0 * age
    robe, hood, skinm, beard = Mask(), Mask(), Mask(), Mask()
    if pose == "kneel":
        robe.poly([P(-14, 0), P(-12, -18), P(-9, -36), P(-3, -44 + bend), P(6, -44 + bend), P(10, -30), P(12, -14), P(18, 0)])
        head = (4 + bend * 0.6, -50 + bend)
    else:
        robe.poly([P(-11, 0), P(-9, -24), P(-7, -46 + bend), P(-2, -56 + bend), P(7, -56 + bend), P(9, -40), P(11, -18), P(13, 0)])
        head = (4 + bend, -62 + bend * 1.3)
    hx, hy = P(*head)
    hood.poly([P(head[0] - 9, head[1] + 9), P(head[0] - 9, head[1] - 4), P(head[0] - 3, head[1] - 10), P(head[0] + 5, head[1] - 9),
               P(head[0] + 8, head[1] - 2), P(head[0] + 4, head[1] + 2), P(head[0] - 2, head[1] + 10)])
    skinm.ellipse(hx + 2.4 * s * f, hy + 0.5 * s, 3.6 * s, 4.6 * s)
    beard.poly([P(head[0] + 2, head[1] + 2), P(head[0] + 7, head[1] + 2), P(head[0] + 6, head[1] + 14), P(head[0] + 2, head[1] + 19),
                P(head[0] - 1, head[1] + 8)])
    # arms
    sh = P(head[0] - 1, head[1] + 12)
    if pose == "reach":
        hand = P(head[0] + 16, head[1] + 4)
        el = P(head[0] + 8, head[1] + 13)
    elif pose == "hold":
        hand = P(head[0] + 9, head[1] + 22)
        el = P(head[0] + 4, head[1] + 24)
    elif pose == "kneel":
        hand = P(head[0] + 12, head[1] - 6)
        el = P(head[0] + 9, head[1] + 8)
    else:
        hand = P(head[0] + 3, head[1] + 30)
        el = P(head[0] + 1, head[1] + 22)
    sleeve = Mask().poly(tapered([sh, el, hand], 7.0 * s, 4.2 * s)).get(0.4)
    skinm.circle(hand[0], hand[1], 1.9 * s)
    rm, hm, km, bm = robe.get(0.4), hood.get(0.4), skinm.get(0.3), beard.get(0.4)
    full = np.clip(rm + hm + sleeve + km + bm, 0, 1)
    c.over("#05040a", np.clip(blur(full, 0.9) * 1.7, 0, 1), 0.8)
    c.over(c.gradient_field(x, y - 60 * s, x, y, pal["top"], pal["dark"]), rm)
    folds = Mask()
    for i in range(4):
        fx = -6 + i * 4.5
        folds.line([P(fx, -40 + bend), P(fx + 1.5, -4)], 0.8 * s)
    c.over(pal["dark"], folds.get(0.5) * rm, 0.5)
    c.over(c.gradient_field(hx, hy - 10 * s, hx, hy + 10 * s, pal["top"], pal["body"]), hm)
    c.over("#e0b898", km)
    c.over("#e8e4e0", bm)
    c.over(c.gradient_field(sh[0], sh[1], hand[0], hand[1], pal["top"], pal["body"]), sleeve)
    c.over("#e0b898", Mask().circle(hand[0], hand[1], 1.9 * s).get(0.3))
    ex, ey = hx + 3.2 * s * f, hy - 0.5 * s
    c.over("#2a1a14", Mask().circle(ex, ey, 0.55 * s).get(0.2), 0.9)
    c.add(light_color, rim(full, -light[0] * 1.8 * f, -light[1] * 1.8, 0.7) * full, light_k)
    return hand


def crystal(c, x, y, size, color=SOUL, k=1.0, angle=0.0, cracked=0.0):
    """The phylactery: a long hexagonal crystal of soul light (its point up), its heart burning."""
    ca, sa = math.cos(angle), math.sin(angle)

    def R(px, py):
        return (x + (px * ca - py * sa) * size, y + (px * sa + py * ca) * size)

    out = [R(0, -1.6), R(0.55, -0.8), R(0.55, 0.8), R(0, 1.3), R(-0.55, 0.8), R(-0.55, -0.8)]
    left = [R(0, -1.6), R(0.1, -0.85), R(0.1, 0.9), R(0, 1.3), R(-0.55, 0.8), R(-0.55, -0.8)]
    om, lm = Mask().poly(out).get(0.4), Mask().poly(left).get(0.4)
    c.glow(x, y, size * 3.2, color, 0.35 * k, tail=0.5)
    c.over(c.gradient_field(x, y - size, x, y + size, "#3a6a2a", "#0e2410"), om)
    c.over(color, lm, 0.55)
    c.add(color, om, 0.35 * k)
    c.add(SOUL_HOT, Mask().line([R(0.1, -1.5), R(0.1, 1.2)], max(0.8, size * 0.06)).get(0.4), 0.6 * k)
    c.glow(x, y, size * 0.45, SOUL_HOT, 1.2 * k)
    c.add(SOUL_HOT, rim(om, -1.2, -1.2, 0.5) * om, 0.6)
    if cracked > 0:
        cm = Mask().line([R(0.5, -0.6), R(0.15, -0.1), R(0.25, 0.3), R(-0.2, 0.7)], max(0.8, size * 0.05)).get(0.3)
        c.over("#06100a", cm, 0.9)


def lich(c, x, y, s, pal=LICH_ROBE, soul=SOUL, k=1.0, light=(-1, -0.5), light_k=0.4, crown=True, flip=False, seed=1, sit=False):
    """The Liche: a tall skeleton in tattered robes of deep purple-black floating over the ground,
    a high collar, a cracked crown on its skull whose sockets burn, a staff of dark wood topped with
    the phylactery held in its near hand, the far hand bare bone; the hem frays into wisps of soul
    fire. (x, y) = the bottom of the hem's wisps. `sit`: slumped on a throne-like heap (the bones
    of shot 5), no crown. Returns the crystal's centre."""
    f = -1.0 if flip else 1.0
    rng = np.random.default_rng(seed)

    def P(px, py):
        return (x + px * s * f, y + py * s)

    # the robe: a long bell flaring down, its hem torn into tails
    hem = []
    for i in range(9):
        t = i / 8
        hx = -26 + 52 * t
        hy = -4 - (10 if i % 2 else 0) - rng.uniform(0, 6)
        hem.append(P(hx, hy))
    robe_pts = [P(-9, -118), P(9, -118), P(16, -90), P(22, -50), P(27, -18)] + hem[::-1][:-1] + [P(-27, -18), P(-21, -50), P(-15, -90)]
    if sit:
        robe_pts = [P(-10, -88), P(10, -88), P(18, -60), P(28, -20), P(34, -2), P(-34, -2), P(-26, -22), P(-16, -60)]
    rm = Mask().poly(robe_pts).get(0.5)
    # its wisps of soul fire under the hem
    wisp = Mask()
    if not sit:
        for i in range(0, 9, 2):
            hx, hy = hem[i]
            pts = bezier((hx, hy), (hx - f * 6 * s, hy + 6 * s), (hx + f * 4 * s * math.sin(i), hy + 14 * s), None, 10)
            wisp.poly(tapered(pts, 5 * s, 0.4))
    wm = wisp.get(1.2)
    c.add(soul, wm, 0.9 * k)
    c.add(soul, blur(wm, 6), 0.6 * k)
    c.over("#05040a", np.clip(blur(rm, 1.0) * 1.7, 0, 1), 0.8)
    c.over(c.gradient_field(x, y - 120 * s, x, y, pal["top"], pal["dark"]), rm)
    tears = Mask()
    for i in range(7):
        tx = rng.uniform(-18, 18)
        ty = rng.uniform(-80, -20)
        tears.poly([P(tx, ty), P(tx + 2, ty + 5), P(tx - 1, ty + 9)])
    c.over("#06040a", tears.get(0.5) * rm, 0.7)
    folds = Mask()
    for i in range(5):
        fx = -14 + i * 7
        folds.line([P(fx * 0.5, -96), P(fx, -10)], 0.9 * s)
    c.over(pal["dark"], folds.get(0.6) * rm, 0.45)
    # the stole: a band of runes down its front, glowing faintly
    stole = Mask().poly([P(3, -110), P(8, -110), P(11, -40), P(5, -40)]).get(0.4)
    c.over("#2a2236", stole)
    for i in range(6):
        c.glow(*P(7 + i * 0.5, -100 + i * 11), 1.4 * s, soul, 0.5 * k)
    # the collar, the skull and its crown
    top = -88 if sit else -118
    collar = Mask().poly([P(-14, top + 4), P(-12, top - 14), P(-4, top - 4), P(4, top - 4), P(12, top - 14), P(14, top + 4)]).get(0.4)
    c.over(c.gradient_field(x, y + (top - 14) * s, x, y + top * s, pal["top"], pal["body"]), collar)
    sx, sy = P(2, top - 12)
    skull(c, sx, sy, 8.5 * s, light=soul, k=0.8, eyes=soul, eye_k=1.3 * k)
    if crown:
        cr = Mask()
        cr.poly([(sx - 8 * s, sy - 5 * s), (sx + 8 * s, sy - 5 * s), (sx + 8.5 * s, sy - 10 * s), (sx + 5 * s, sy - 8 * s),
                 (sx + 3 * s, sy - 16 * s), (sx, sy - 9 * s), (sx - 3 * s, sy - 15 * s), (sx - 5 * s, sy - 8 * s), (sx - 8.5 * s, sy - 11 * s)])
        crm = cr.get(0.4)
        c.over(c.gradient_field(sx, sy - 16 * s, sx, sy - 5 * s, "#ead292", "#5a421e"), crm)
        c.glow(sx, sy - 9 * s, 1.5 * s, soul, 0.9 * k)
    # the bony far hand, casting
    fh = P(-20, top + 28)
    c.over("#d8ccb0", Mask().line([P(-12, top + 12), fh], 2.2 * s).get(0.4))
    for j in range(3):
        c.over("#e8dcc0", Mask().line([fh, (fh[0] - f * (3 + j) * s, fh[1] - (4 - j * 2) * s)], 0.9 * s).get(0.3))
    c.glow(fh[0] - f * 4 * s, fh[1] - 2 * s, 5 * s, soul, 0.5 * k)
    # the staff in its near hand, the phylactery on top
    st0, st1 = P(18, -4 if not sit else -2), P(16, top - 46)
    c.over("#3a2a1c", Mask().line([st0, st1], 2.6 * s).get(0.4))
    c.add("#8a6a4a", Mask().line([(st0[0] - f * 0.6 * s, st0[1]), (st1[0] - f * 0.6 * s, st1[1])], 0.7 * s).get(0.3), 0.5)
    nh = P(15, top + 10)
    c.over("#e8dcc0", Mask().circle(nh[0], nh[1], 2.4 * s).get(0.3))
    cx, cy = st1[0], st1[1] - 9 * s
    gold = Mask().poly([(cx - 4 * s, cy + 9 * s), (cx + 4 * s, cy + 9 * s), (cx + 6 * s, cy + 2 * s), (cx - 6 * s, cy + 2 * s)]).get(0.3)
    c.over("#bc9a52", gold)
    crystal(c, cx, cy, 6.5 * s, soul, k)
    full = np.clip(rm + collar, 0, 1)
    c.add(soul, rim(full, -light[0] * 2 * f, -light[1] * 2, 0.8) * full, light_k)
    return (cx, cy)


def altar(c, x, y, w, h, soul=SOUL, k=1.0):
    """A block of carved stone: a slab, a skull carved in front, runes, two candles."""
    m = Mask().poly([(x - w / 2, y), (x - w / 2, y - h), (x + w / 2, y - h), (x + w / 2, y)]).get(0.5)
    c.over(c.gradient_field(x - w / 2, y - h, x + w / 2, y, "#4a4458", "#16121e"), m)
    slab = Mask().poly([(x - w / 2 - 6, y - h), (x - w / 2 - 6, y - h - 10), (x + w / 2 + 6, y - h - 10), (x + w / 2 + 6, y - h)]).get(0.5)
    c.over(c.gradient_field(x, y - h - 10, x, y - h, "#6a6280", "#2a2436"), slab)
    skull(c, x, y - h * 0.55, h * 0.18, light=soul, k=0.6, eyes=soul, eye_k=0.7 * k)
    for side in (-1, 1):
        for i in range(3):
            c.glow(x + side * w * 0.36, y - h * 0.8 + i * h * 0.22, 1.6, soul, 0.6 * k)
    candle(c, x - w / 2 + 6, y - h - 10, 12, 3.2, soul, k)
    candle(c, x + w / 2 - 6, y - h - 10, 16, 3.2, soul, k)


# --------------------------------------------------------------------------- the six shots

def shot1():
    """Under the Confins lie catacombs of black glass. Old Ossian lit a candle there for each of
    the dead."""
    c = Canvas()
    catacombs(c, 5101, floor_y=452, rows=4, light=CANDLE, light_k=0.8)
    c.glow(560, 380, 260, "#ff9a4a", 0.22, tail=0.6)
    candles(c, 5103, 46, (40, 920, 456, 6), CANDLE, 0.9, size=(8, 20))
    hand = keeper(c, 440, 462, 2.7, pose="reach", light=(1, -0.2), light_k=0.8)
    # the taper in his hand, lighting a new candle
    c.over("#d8c8a0", Mask().line([hand, (hand[0] + 18, hand[1] + 6)], 1.6).get(0.3))
    c.glow(hand[0] + 19, hand[1] + 5, 3.0, CANDLE, 1.2)
    candle(c, hand[0] + 26, 456, 16, 3.6, CANDLE, 1.0)
    motes(c, 5105, 40, (0, 60, 960, 440), "#ffd8a0", 0.5)
    return finish(c, bloom=0.55, vignette=0.55)


def shot2():
    """He knew all their names. « As long as someone remembers you, you are never quite gone. »"""
    c = Canvas()
    catacombs(c, 5201, floor_y=460, rows=1, light=CANDLE, light_k=0.5, top=10)
    names_wall(c, 5203, 200, 60, 920, 420, CANDLE, 1.0, lit=(610, 240, 80))
    c.glow(600, 250, 120, "#ffb060", 0.25, tail=0.6)
    candles(c, 5205, 30, (230, 890, 452, 4), CANDLE, 1.0, size=(10, 22))
    hand = keeper(c, 520, 468, 3.0, pose="reach", light=(1, -0.6), light_k=0.9)
    c.glow(hand[0] + 6, hand[1] - 2, 10, CANDLE, 0.5)
    motes(c, 5207, 30, (200, 60, 920, 440), "#ffe0a0", 0.45)
    return finish(c, bloom=0.55, vignette=0.55)


def shot3():
    """The night the star of the centre collapsed, the living fled to the stars. Ossian stayed with
    his dead."""
    c = Canvas()
    c.vgradient([(0.0, "#05040e"), (0.5, "#140a26"), (0.78, "#2a1030"), (1.0, "#0a0408")])
    c.nebula(5301, "#3a1a5a", "#1a2a5a", 0.35, cell=240)
    c.stars(5303, 900, "#e8e0ff", 0.9, region=(0, 0, 960, 420))
    collapsed_star(c, 690, 120, 22)
    c.rays(690, 120, 10, 260, 20, "#c88bff", 0.07, 5305, soft=10)
    rng = np.random.default_rng(5307)
    for i, (x0, y0, x1, y1) in enumerate(((520, 430, 620, 140), (560, 440, 780, 200), (480, 420, 520, 90), (600, 440, 880, 260),
                                          (540, 430, 700, 60))):
        path = bezier((x0, y0), (x0 + (x1 - x0) * 0.2, y0 - 150), (x1, y1), None, 30)
        tm = Mask().poly(tapered(path, 0.6, 3.0)).get(1.6)
        c.add("#7ab8ff", tm, 0.4)
        a = math.atan2(path[-1][1] - path[-3][1], path[-1][0] - path[-3][0])
        ship(c, x1, y1, a, 1.2 + rng.random() * 0.4, 5310 + i)
    hz = Mask().poly([(0, 450)] + [(x, 440 - 12 * math.sin(x * 0.012) - 8 * math.sin(x * 0.033)) for x in range(0, 961, 20)]
                     + [(960, 545), (0, 545)]).get(0.6)
    c.over(c.gradient_field(0, 420, 0, 545, "#1c1222", "#060308"), hz)
    c.add("#a888ff", rim(hz, 0, 3, 1.0), 0.3)
    # the cliff of black glass and the arch of the catacombs on the left
    cliff = Mask().poly([(0, 545), (0, 120), (110, 100), (220, 170), (300, 260), (330, 360), (360, 545)]).get(0.6)
    c.over(c.gradient_field(0, 100, 0, 545, "#221a2c", "#08060c"), cliff)
    c.add("#a888ff", rim(cliff, -3, 2, 1.0), 0.4)
    arch = Mask().poly([(120, 470), (120, 360), (240, 360), (240, 470)]).ellipse(180, 360, 60, 50).get(0.8) * cliff
    c.over("#030206", arch)
    c.glow(180, 440, 70, CANDLE, 0.3, tail=0.6)
    candles(c, 5309, 9, (128, 232, 466, 3), CANDLE, 0.8, size=(6, 12))
    hand = keeper(c, 200, 470, 1.25, pose="hold", light=(1, -1), light_color="#b8a8ff", light_k=0.5)
    candle(c, hand[0] + 1, hand[1] + 2, 6, 2.2, CANDLE, 1.1)
    return finish(c, bloom=0.55, vignette=0.5)


def shot4():
    """He grew old. Who would remember them after him? He locked his soul in a crystal, never to
    die."""
    c = Canvas()
    catacombs(c, 5401, floor_y=456, rows=3, light=SOUL, light_k=0.55)
    c.glow(480, 300, 300, "#2a5a2a", 0.25, tail=0.6)
    # a circle of runes on the floor around him
    ring = Mask().arc(480, 462, 150, 26, 0, 360, 2.4, steps=60).get(0.8)
    c.add(SOUL, ring, 0.9)
    for i in range(12):
        a = 2 * math.pi * i / 12
        c.glow(480 + math.cos(a) * 150, 462 + math.sin(a) * 26, 3.0, SOUL, 0.8)
    candles(c, 5403, 18, (120, 860, 462, 6), CANDLE, 0.45, size=(6, 14))
    hand = keeper(c, 450, 466, 2.5, pose="kneel", age=1.0, light=(1, -1), light_color=SOUL, light_k=0.9)
    cx, cy = hand[0] + 8, hand[1] - 22
    # his soul flowing from his chest into the crystal
    chest = (452 + 14, 466 - 2.5 * 34)
    stream = bezier(chest, (chest[0] + 40, chest[1] - 40), (cx, cy + 8), None, 24)
    sm = Mask().poly(tapered(stream, 9, 3)).get(2.0)
    c.add(SOUL, sm, 0.9)
    c.add(SOUL_HOT, blur(sm, 1.0) * 0.6, 0.6)
    crystal(c, cx, cy, 15, SOUL, 1.3)
    c.rays(cx, cy, 14, 170, 14, SOUL, 0.12, 5405, soft=8)
    motes(c, 5407, 50, (300, 120, 700, 460), SOUL, 0.8)
    return finish(c, bloom=0.6, vignette=0.55)


def shot5():
    """The centuries gnawed his flesh to the bones. In the crystal, his soul still burned, a green
    flame."""
    c = Canvas()
    catacombs(c, 5501, floor_y=456, rows=3, light=SOUL, light_k=0.4)
    # dust, cobwebs in the niches' corners, candles of soul fire burned low
    web = Mask()
    for (ax, ay, sgn) in ((0, 0, 1), (960, 0, -1)):
        for i in range(7):
            a = math.radians(5 + i * 12)
            web.line([(ax, ay), (ax + sgn * math.cos(a) * 170, ay + math.sin(a) * 170)], 0.7)
        for r in (40, 80, 120, 160):
            web.arc(ax, ay, r, r, 0 if sgn > 0 else 90, 90 if sgn > 0 else 180, 0.6, steps=14)
    c.add("#c8c0d8", web.get(0.4), 0.25)
    candles(c, 5503, 22, (80, 880, 462, 6), SOUL, 0.7, size=(3, 8))
    cx, cy = lich(c, 480, 462, 1.9, crown=False, sit=True, seed=5505, k=0.9, light=(0.3, -1), light_k=0.35)
    c.glow(cx, cy, 70, SOUL, 0.25)
    rng = np.random.default_rng(5507)
    dust = Mask()
    for _ in range(160):
        x, y = rng.uniform(0, 960), rng.uniform(0, 470)
        dust.circle(x, y, rng.uniform(0.4, 1.1), v=int(rng.uniform(60, 200)))
    c.add("#c8c0b0", dust.get(0.4), 0.35)
    return finish(c, bloom=0.55, vignette=0.6)


def shot6():
    """The Liche still watches over its crypt. It has forgotten its own name… but not a single one
    of theirs."""
    c = Canvas()
    catacombs(c, 5601, floor_y=462, rows=1, light=SOUL, light_k=0.5, top=6)
    names_wall(c, 5603, 40, 40, 920, 400, SOUL, 0.6, lit=(480, 230, 300), cols=5)
    c.glow(480, 300, 320, "#1e4a22", 0.35, tail=0.6)
    c.glow(480, 250, 150, SOUL, 0.18, tail=0.7)
    candles(c, 5605, 34, (40, 920, 470, 8), SOUL, 0.85, size=(6, 18))
    altar(c, 480, 466, 96, 52, SOUL, 1.0)
    lich(c, 480, 390, 1.78, seed=5607, k=1.2, light=(-0.6, -1), light_k=0.55)
    motes(c, 5609, 36, (0, 40, 960, 460), SOUL, 0.7)
    return finish(c, bloom=0.6, vignette=0.6)


SHOTS = [shot1, shot2, shot3, shot4, shot5, shot6]
