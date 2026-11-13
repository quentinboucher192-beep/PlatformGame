"""Gloop: translucent lime-teal slime with a glowing nucleus and debris
floating inside (24x20, collision 14x10, pivot bottom-centre)."""
import math

import numpy as np

import effects as FX
from common.palette import ACID, DANGER, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W, H = 24, 20
TAU = 2 * math.pi

GOO_RAMP = [hex_rgba(c) for c in ('#e4ffb0', '#a8f590', '#5fd88a', '#2fae7c', '#1d7e66')]
GOO = Mat(GOO_RAMP, name="gloop_goo", thresholds=[0.97, 0.82, 0.62, 0.42])
GOO_DEAD = Mat([hex_rgba(c) for c in ('#8fc4a0', '#5f9a82', '#3f7468', '#2a5450')], name="gloop_goo_dead",
               thresholds=[0.85, 0.6, 0.34])
# lighter jelly around the nucleus (light scattered inside the slime)
HALO = Mat([GOO_RAMP[1], GOO_RAMP[2]], name="gloop_halo", thresholds=[0.5])
BUBBLE = Mat([GOO_RAMP[0]], name="gloop_bubble")
NUCLEUS = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="gloop_nucleus",
              thresholds=[0.8, 0.45])
NUCLEUS_DEAD = Mat([hex_rgba('#7a5a6a'), hex_rgba('#4a3448')], name="gloop_nucleus_dead", thresholds=[0.5])
CHIP = Mat([hex_rgba('#3f8a76'), hex_rgba('#246a5e')], name="gloop_chip", thresholds=[0.5])
BONE = Mat([hex_rgba('#d4f6d8'), hex_rgba('#8fd0a8')], name="gloop_bone", thresholds=[0.5])
SPEC = Mat([hex_rgba('#f6ffe8')], name="gloop_spec")
MOUTH = Mat([hex_rgba('#0f4a40'), hex_rgba('#0a2f2c')], name="gloop_mouth", thresholds=[0.5])
DROP = [hex_rgba('#e4ffb0'), GOO_RAMP[1], GOO_RAMP[2], GOO_RAMP[3]]
SPIT = [hex_rgba('#e4ffb0'), ACID[2], GOO_RAMP[2], GOO_RAMP[3]]


def _rim(cv, mat_id, ground, inten=0.62):
    """Translucency: light that went through the jelly brightens the inner
    bottom edge of the body (one pixel above the outline)."""
    m = cv.mat == mat_id
    below = np.zeros_like(m)
    below[:-1] = m[1:]
    edge = m & ~below & (cv.py > ground - 3.0)
    cv.inten[edge] = np.maximum(cv.inten[edge], inten)


def draw(sx=1.0, sy=1.0, lift=0.0, lean=0.0, wob=0.0, nuc=(0.0, 0.0), glow=0.0, mouth=0.0,
         dead=False, debris=0.0, offset=(0.0, 0.0), bright=0.0):
    """sx/sy = squash & stretch about the bottom centre, lift = hop height,
    lean = top shifted forward (px), wob = jelly ripple amount, glow = nucleus
    charge, bright = whole body brightening."""
    cv = TCanvas(W, H, center=(12.0, 19.6), offset=(offset[0], offset[1] - lift), scale=(sx, sy))
    goo = GOO_DEAD if dead else GOO
    ground = cv.T((12.0, 19.6))[1]
    clip = cv.py < ground
    # body: upper part of a big ellipse, cut flat by the ground
    cv.part()
    cv.tellipse((12.0 + lean * 0.35, 16.8), 8.0 + 0.4 * wob, 8.4 - 0.4 * wob, goo, ang=lean * 0.05,
                bias=bright, clip=clip)
    # a small bulge at the front-bottom (where it spits from)
    cv.tellipse((17.4 + lean * 0.3, 17.6), 3.2, 2.6, goo, bias=bright - 0.1, clip=clip)
    body = cv.mat == cv._mid(goo)
    shade = np.clip((cv.py - (ground - 7.0)) / 7.0, 0, 1)
    cv.inten[body] -= 0.2 * shade[body]
    _rim(cv, cv._mid(goo), ground, 0.5 if not dead else 0.45)
    nc = (13.8 + lean * 0.5 + nuc[0], 13.6 + nuc[1])
    if not dead:
        cv.tellipse(nc, 2.5 + 0.9 * glow, 2.3 + 0.9 * glow, HALO, flat=0.3 + 0.5 * glow, decal=True)
    # debris floating inside: stone chip, little bone, bubbles
    d = debris
    cv.tpoly([(14.6, 16.6 - d), (16.4, 15.9 - d), (16.9, 17.5 - d), (15.0, 18.0 - d)], CHIP, flat=0.6,
             decal=True)
    bx, by = 8.0 + lean * 0.3, 13.6 - d * 0.6
    cv.tline((bx, by), (bx + 2.0, by + 2.0), BONE, inten=0.8, decal=True)
    cv.tpx((bx - 1.0, by), BONE, inten=0.3, decal=True)
    cv.tpx((bx + 2.0, by + 3.0), BONE, inten=0.3, decal=True)
    cv.tpx((10.0 + lean * 0.4, 11.4 - d * 1.5), BUBBLE, inten=1.0, decal=True)
    cv.tpx((11.5 + lean * 0.3, 17.0 - d * 0.8), BUBBLE, inten=1.0, decal=True)
    if glow > 0.2:
        cv.tpx((15.5 + lean * 0.4, 10.0 - d), BUBBLE, inten=1.0, decal=True)
        cv.tpx((6.5 + lean * 0.4, 15.0 - d), BUBBLE, inten=1.0, decal=True)
    # specular highlight (top-left)
    cv.tline((6.2 + lean * 0.45, 12.0), (7.4 + lean * 0.5, 10.4), SPEC, decal=True)
    cv.tline((8.6 + lean * 0.55, 9.4), (9.6 + lean * 0.55, 9.4), SPEC, decal=True)
    # mouth (spitting)
    if mouth > 0.1:
        cv.part(line=False)
        cv.tellipse((19.6 + lean * 0.4, 16.0), 0.9 + 0.9 * mouth, 0.6 + 1.3 * mouth, MOUTH, flat=0.4, decal=True)
    # nucleus
    cv.part(line=False)
    nm = NUCLEUS_DEAD if dead else NUCLEUS
    r = 1.5 + 0.6 * glow
    cv.tellipse(nc, r, r * 0.95, nm, flat=0.6)
    if not dead:
        cv.tpx((nc[0] - 0.6, nc[1] - 0.6), nm, inten=1.0)
        if glow > 0.3:
            cv.tpx((nc[0] + 0.4, nc[1] - 0.6), nm, inten=1.0)
            cv.tpx((nc[0] - 0.6, nc[1] + 0.4), nm, inten=1.0)
    return cv.render()


def puddle(spread=1.0, drops=(), nuc=0.6, dead=True):
    """Splattered slime: flat puddle with the dimming nucleus."""
    cv = TCanvas(W, H)
    goo = GOO_DEAD if dead else GOO
    cv.part()
    cv.ellipse((12.0, 18.6), 9.0 * spread, 1.6, goo, bias=0.1)
    cv.ellipse((8.0, 18.0), 3.0 * spread, 1.4, goo, bias=0.15)
    cv.ellipse((15.5, 18.2), 3.4 * spread, 1.5, goo, bias=0.1)
    cv.tpx((7.0, 17.4), SPEC, decal=True)
    cv.part(line=False)
    if nuc > 0:
        cv.ellipse((13.0, 18.0), 1.4, 0.9, NUCLEUS, flat=0.2 + 0.5 * nuc)
    else:
        cv.ellipse((13.0, 18.0), 1.4, 0.9, NUCLEUS_DEAD, flat=0.4)
    img = cv.render()
    for (x, y, big) in drops:
        if big:
            FX.chunk(img, x, y, 2, DROP[1], DROP[3])
        else:
            put(img, x, y, DROP[1])
    return img


def droplets(img, pts, cols=DROP):
    for i, (x, y) in enumerate(pts):
        if 0 <= x < W and 0 <= y < H and img[y, x, 3] == 0:
            img[y, x] = cols[i % 2 + 1]
    return img


def clips():
    idle = []
    for i in range(4):
        ph = TAU * i / 4
        idle.append(draw(sx=1.0 + 0.05 * math.sin(ph), sy=1.0 - 0.06 * math.sin(ph), lean=0.4 * math.cos(ph),
                         wob=math.sin(ph), nuc=(0.3 * math.cos(ph), 0.4 * math.sin(ph)), glow=0.05 * (i % 2),
                         debris=0.4 * math.sin(ph)))
    # hop: anticipation squash -> stretch takeoff -> air -> peak -> fall -> landing squash
    hop = [  # sx, sy, lift, lean, nuc_dy, debris
        (1.16, 0.80, 0.0, -0.6, 0.6, -0.4),
        (0.86, 1.18, 1.0, 1.2, 0.6, 0.4),
        (0.92, 1.10, 4.0, 1.4, 0.3, 0.8),
        (1.00, 1.00, 5.0, 0.8, -0.2, 0.6),
        (0.92, 1.10, 2.5, 0.0, -0.6, 0.0),
        (1.22, 0.76, 0.0, 0.6, -0.4, -0.6),
    ]
    walk = []
    for i, (sx, sy, lift, lean, ndy, deb) in enumerate(hop):
        f = draw(sx=sx, sy=sy, lift=lift, lean=lean, nuc=(0.2, ndy), debris=deb, wob=0.5 * math.sin(TAU * i / 6))
        if i == 1:
            droplets(f, [(5, 19), (19, 19)])
        if i == 5:
            droplets(f, [(1, 18), (22, 18), (2, 16), (21, 17)])
        walk.append(f)
    tele = []
    for i in range(3):
        t = (i + 1) / 3.0
        f = draw(sx=1.0 + 0.12 * t, sy=1.0 + 0.16 * t, lean=-0.6 * t, glow=0.35 * t + 0.1,
                 nuc=(0.4, -0.6 * t), wob=0.6 * (i % 2), debris=0.8 * t)
        tele.append(f)
    FX.sparkle(tele[2], 14, 6, 1, hex_rgba('#ffe0e6'), DANGER[1])
    a0 = draw(sx=1.18, sy=0.84, lean=-1.2, glow=0.5, nuc=(-0.6, 0.4))
    a1 = draw(sx=0.94, sy=1.06, lean=1.6, glow=0.3, mouth=1.0, nuc=(0.8, 0.0), offset=(1, 0))
    FX.burst(a1, 22, 14, 2.2, SPIT[:3], seed=4, jag=0.3)
    droplets(a1, [(22, 11), (23, 16), (21, 17)], SPIT)
    a2 = draw(sx=0.98, sy=1.02, lean=1.0, glow=0.15, mouth=0.5, nuc=(0.5, 0.0), offset=(1, 0))
    droplets(a2, [(22, 13), (21, 18)], SPIT)
    a3 = draw(sx=1.04, sy=0.97, lean=0.2, glow=0.05, mouth=0.0)
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(sx=1.2, sy=0.82, lean=-1.5, offset=(-1, 0)), amount=0.6),
            draw(sx=0.9, sy=1.1, lean=-0.8, offset=(-1, 0), wob=1.0)]
    d0 = FX.flash(draw(sx=0.84, sy=1.2, lean=-1.0, glow=0.4), amount=0.5)
    d1 = draw(sx=1.36, sy=0.62, lean=0.4, glow=0.2, wob=1.0, nuc=(0.0, 1.0))
    droplets(d1, [(1, 15), (22, 14), (3, 12), (20, 11)])
    d2 = puddle(1.0, drops=[(1, 13, True), (20, 12, True), (5, 10, False), (18, 9, False)], nuc=0.8, dead=False)
    d3 = puddle(1.05, drops=[(0, 16, True), (21, 16, True), (4, 14, False), (19, 13, False)], nuc=0.3)
    death = [d0, d1, d2, d3,
             FX.dissolve(d3, 0.5, seed=6, colors=DROP, rise=7, drift=2),
             FX.dissolve(d3, 0.97, seed=6, colors=DROP, rise=7, drift=2)]
    return dict(idle=idle, walk=walk, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/gloop.png", W, H, [12, 20])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("walk", c["walk"], durations=[0.1, 0.06, 0.07, 0.08, 0.07, 0.12], loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.18], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.08, 0.1, 0.12], loop=False, events=[(1, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.12, 0.16, 0.12, 0.14], loop=False)
    s.save("animations/gloop.json")
