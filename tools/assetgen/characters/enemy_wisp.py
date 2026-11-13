"""Wisp: arcane will-o'-wisp, a violet flame wearing a mask-like face
(24x24, collision 12x12, pivot bottom-centre of the box)."""
import math

import numpy as np

import effects as FX
from common.palette import DANGER, SUIT, VIOLET, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W = H = 24
TAU = 2 * math.pi

DVIO = hex_rgba('#2a0f4a')
F_OUT = Mat([VIOLET[0]], name="flame_out", outline=DVIO)
F_MID = Mat([VIOLET[1]], name="flame_mid", outline=DVIO)
F_IN = Mat([VIOLET[2]], name="flame_in", outline=DVIO)
F_CORE = Mat([hex_rgba('#f6dcff')], name="flame_core", outline=DVIO)
MASK = Mat([SUIT[0], SUIT[1], SUIT[2], SUIT[3]], name="mask", thresholds=[0.82, 0.6, 0.36])
HOLE = Mat([hex_rgba('#160829')], name="hole")
EYE = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="wisp_eye", thresholds=[0.8, 0.45])
CRACK = Mat([hex_rgba('#160829')], name="mask_crack")
SPARK = [hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]]


def flame(cv, base, r, tips, scale=1.0):
    """Layered flame: union of tapered capsules from the base to each tip,
    drawn outer->inner with shrinking radii (hard colour bands)."""
    layers = [(F_OUT, 1.0), (F_MID, 0.72), (F_IN, 0.46), (F_CORE, 0.24)]
    for mat, k in layers:
        cv.part(line=False)
        cv.tdisc(base, r * k * scale + (0.4 if k == 1.0 else 0.0), mat, flat=0.5)
        for (tx, ty, tr) in tips:
            # tips of inner layers are pulled towards the base
            px = base[0] + (tx - base[0]) * (0.35 + 0.65 * k)
            py = base[1] + (ty - base[1]) * (0.35 + 0.65 * k)
            cv.tcapsule(base, (px, py), r * k * scale * 0.95, max(0.3, tr * k), mat, flat=0.5)


def draw(ph=0.0, lean=0.0, size=1.0, offset=(0.0, 0.0), eye=0.0, mask=True, mask_off=(0.0, 0.0),
         mask_rot=0.0, cracked=0, squeeze=1.0, glow=0.0):
    cv = TCanvas(W, H, center=(12.0, 17.0), offset=offset, scale=(squeeze, 1.0))
    base = (11.5, 16.5)
    r = 5.0 * size
    s = size
    tips = [
        (base[0] - 3.5 * s + lean * 4 + 0.8 * math.sin(ph), base[1] - 9.0 * s - 1.0 * math.sin(ph + 1.0), 0.6),
        (base[0] + 0.2 * s + lean * 6 + 0.7 * math.sin(ph + 2.1), base[1] - 13.0 * s - 1.2 * math.cos(ph), 0.5),
        (base[0] + 3.4 * s + lean * 4 + 0.6 * math.sin(ph + 4.0), base[1] - 8.0 * s - 1.0 * math.sin(ph + 3.0), 0.6),
        (base[0] - 4.5 * s + lean * 2, base[1] + 1.5 * s + 0.6 * math.sin(ph + 0.5), 1.4),
        (base[0] + 0.5, base[1] + 4.5 * s + 0.5 * math.sin(ph), 1.4),
    ]
    flame(cv, base, r, tips)
    if mask:
        mc = (13.6 + mask_off[0], 15.2 + mask_off[1])
        cv.part()
        cv.tellipse(mc, 3.4, 4.1, MASK, ang=mask_rot, bias=0.06)
        # brow ridge / horns of the mask
        cv.tcapsule((mc[0] - 2.6, mc[1] - 3.2), (mc[0] - 3.6, mc[1] - 5.6), 0.8, 0.4, MASK, bias=-0.05)
        cv.tcapsule((mc[0] + 2.2, mc[1] - 3.4), (mc[0] + 3.4, mc[1] - 5.4), 0.8, 0.4, MASK, bias=0.05)
        ca, sa = math.cos(mask_rot), math.sin(mask_rot)

        def M_(dx, dy):
            return (mc[0] + dx * ca - dy * sa, mc[1] + dx * sa + dy * ca)
        cv.part(line=False)
        # angry slanted eye holes
        for ex, slant in ((-1.4, 0.45), (1.5, -0.45)):
            c = M_(ex, -0.6)
            cv.tcapsule((c[0] - 0.9, c[1] + slant * -1.0 - 0.2), (c[0] + 0.9, c[1] + slant * 1.0 - 0.2),
                        0.75, 0.75, HOLE, flat=0.3, decal=True)
            cv.tdisc((c[0] + 0.5, c[1] + 0.6), 0.6 + 0.2 * min(1.0, eye), EYE, flat=0.5 + 0.25 * min(1.0, eye),
                     decal=True)
            if eye > 0.8:
                cv.tpx((c[0] + 0.5, c[1]), EYE, inten=1.0, decal=True)
        # mouth slit
        m0, m1 = M_(-1.2, 2.3), M_(1.4, 2.0)
        cv.tline(m0, m1, HOLE, decal=True)
        if cracked:
            cv.tline(M_(0.4, -4.0), M_(-0.4, -1.0), CRACK, decal=True)
            cv.tline(M_(-0.4, -1.0), M_(0.8, 1.5), CRACK, decal=True)
            if cracked > 1:
                cv.tline(M_(0.8, 1.5), M_(0.0, 4.0), CRACK, decal=True)
    img = cv.render()
    return img


def embers(img, ph, n=3, seed=0, spread=8):
    rng = np.random.default_rng(seed)
    for k in range(n):
        x0 = 12 + (rng.random() - 0.5) * spread
        t = (ph / TAU + rng.random()) % 1.0
        y = 12 - t * 10
        x = x0 + math.sin(t * 6 + k) * 1.0
        c = SPARK[min(3, int(t * 4))]
        xi, yi = int(x), int(y)
        if 0 <= xi < W and 0 <= yi < H and img[yi, xi, 3] == 0:
            img[yi, xi] = c
    return img


def clips():
    idle = []
    for i in range(6):
        ph = TAU * i / 6
        f = draw(ph=ph, offset=(0, round(0.6 * math.sin(ph))))
        idle.append(embers(f, ph, 3, seed=1))
    move = []
    for i in range(6):
        ph = TAU * i / 6
        f = draw(ph=ph, lean=-0.7, mask_off=(0.6, 0.3), offset=(0, round(0.5 * math.sin(ph))))
        move.append(embers(f, ph, 2, seed=2))
    tele = []
    for i in range(5):
        t = (i + 1) / 5
        f = draw(ph=TAU * i / 5, size=1.0 + 0.18 * t, eye=t * 1.3, offset=(0, -round(1 * t)))
        embers(f, TAU * i / 5, 3 + i, seed=3, spread=12)
        if i >= 3:
            FX.sparkle(f, 16, 13 - round(t), 1 + (i - 3), hex_rgba('#ffe0e6'), DANGER[1])
        tele.append(f)
    attack = [
        draw(ph=0.0, size=0.9, eye=1.0, offset=(-1, 0), lean=0.3),
        draw(ph=1.0, size=1.05, eye=1.2, offset=(1, 0), lean=-0.6, mask_off=(0.5, 0)),
        draw(ph=2.0, size=1.1, eye=1.4, offset=(2, 0), lean=-0.9, mask_off=(1.0, 0)),
        draw(ph=3.0, size=1.0, eye=0.6, offset=(0, 0), lean=-0.2),
    ]
    FX.burst(attack[2], 21, 15, 2.2, SPARK[:3], seed=3, jag=0.3)
    tout = []
    for i, sq in enumerate((0.8, 0.5, 0.25)):
        f = draw(ph=i, squeeze=sq, size=1.0 + 0.15 * i, eye=0.5, mask=i < 2)
        embers(f, i, 3 + i * 2, seed=4 + i, spread=10)
        tout.append(f)
    f = FX.new(W, H)
    for y in range(4, 22):
        put(f, 12, y, SPARK[0] if 8 < y < 18 else SPARK[1])
    for (x, y) in ((9, 10), (15, 8), (8, 16), (16, 18), (11, 3)):
        put(f, x, y, SPARK[1])
    tout.append(f)
    f = FX.new(W, H)
    FX.sparkle(f, 12, 13, 2, SPARK[0], SPARK[1])
    for (x, y) in ((7, 9), (17, 7), (6, 17), (18, 19), (12, 2), (12, 22)):
        put(f, x, y, SPARK[2])
    tout.append(f)
    tin = [tout[4], tout[3], tout[2], tout[1], tout[0]]
    tin = [t.copy() for t in tin]
    summon = []
    for i in range(6):
        t = i / 5
        f = draw(ph=TAU * i / 6, size=1.0 + 0.25 * math.sin(math.pi * min(1, t * 1.2)), eye=0.8 + 0.6 * t,
                 mask_off=(0, -0.5 * t), mask_rot=-0.15 * t, offset=(0, -1))
        # glyph ring below
        rr = 3 + 3 * min(1.0, t * 1.5)
        for k in range(24):
            a = TAU * k / 24 + t
            x, y = int(12 + math.cos(a) * rr), int(22 + math.sin(a) * rr * 0.3)
            if 0 <= x < W and 0 <= y < H and f[y, x, 3] == 0 and (k % 3 != 0 or i > 2):
                f[y, x] = SPARK[1] if k % 2 else SPARK[2]
        embers(f, TAU * i / 6, 4 + i, seed=6, spread=14)
        summon.append(f)
    hurt = [FX.flash(draw(ph=0.5, offset=(-1, 0), lean=0.4, eye=0.3), amount=0.6), draw(ph=1.5, offset=(-1, 0), lean=0.3)]
    death = [
        FX.flash(draw(ph=0.3, eye=1.0, lean=0.3), amount=0.5),
        draw(ph=1.3, size=0.85, eye=0.6, cracked=1),
        draw(ph=2.3, size=0.65, eye=0.3, cracked=2, mask_off=(0, 1), mask_rot=0.2),
        draw(ph=3.3, size=0.45, eye=0.0, cracked=2, mask_off=(0, 3), mask_rot=0.4),
    ]
    last = draw(ph=4.3, size=0.25, eye=0.0, cracked=2, mask_off=(0, 5), mask_rot=0.6)
    death.append(embers(last.copy(), 1.0, 6, seed=9, spread=10))
    death.append(FX.dissolve(last, 0.55, seed=12, colors=SPARK, rise=8, drift=2))
    death.append(FX.dissolve(last, 1.0, seed=12, colors=SPARK, rise=8, drift=2))
    return dict(idle=idle, move=move, telegraph=tele, attack=attack, teleport_out=tout, teleport_in=tin,
                summon=summon, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/wisp.png", W, H, [12, 24])
    s.add("idle", c["idle"], fps=10, loop=True)
    s.add("move", c["move"], fps=12, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.1, 0.1, 0.1, 0.12, 0.14], loop=False)
    s.add("attack", c["attack"], durations=[0.06, 0.06, 0.1, 0.12], loop=False, events=[(2, "fire")])
    s.add("teleport_out", c["teleport_out"], durations=[0.05, 0.05, 0.05, 0.06, 0.08], loop=False)
    s.add("teleport_in", c["teleport_in"], durations=[0.06, 0.05, 0.05, 0.05, 0.06], loop=False)
    s.add("summon", c["summon"], durations=[0.1, 0.1, 0.1, 0.1, 0.12, 0.16], loop=False)
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.12, 0.12, 0.12, 0.14], loop=False)
    s.save("animations/wisp.json")
