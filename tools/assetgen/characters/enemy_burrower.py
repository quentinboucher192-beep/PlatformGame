"""Burrower: segmented worm with a ring of teeth and a glowing gullet. Only the
part above ground is drawn; the bottom row of the frame is the ground line
(32x32, collision 18x16, pivot bottom-centre)."""
import math

import numpy as np

import effects as FX
from common.palette import AMBER, CYAN, DANGER, MOSS, STONE, SUIT, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W = H = 32
TAU = 2 * math.pi
BASE = (16.0, 32.0)

PLATE = Mat([STONE[4], STONE[3], STONE[2], STONE[1]], name="bur_plate", thresholds=[0.84, 0.62, 0.38])
PLATE_DEAD = Mat([STONE[3], STONE[2], STONE[1], STONE[0]], name="bur_plate_dead", thresholds=[0.84, 0.62, 0.38])
BELLY = Mat([hex_rgba(c) for c in ('#d69ac4', '#a8649e', '#743e72')], name="bur_belly", thresholds=[0.7, 0.42])
SEAM = Mat([CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="bur_seam", thresholds=[0.75, 0.45])
SEAM_OFF = Mat([STONE[1], STONE[0]], name="bur_seam_off", thresholds=[0.5])
MAW = Mat([hex_rgba('#3a0f2e'), hex_rgba('#1e0818')], name="bur_maw", thresholds=[0.5])
GULLET = Mat([hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1], DANGER[0]], emissive=True, name="bur_gullet",
             thresholds=[0.9, 0.74, 0.56, 0.36])
TOOTH = Mat([SUIT[0], SUIT[2]], name="bur_tooth", thresholds=[0.5])
SOIL = Mat([STONE[2], STONE[1], STONE[0]], name="bur_soil", thresholds=[0.62, 0.36])
SOIL_MOSS = Mat([MOSS[3], MOSS[2], MOSS[1]], name="bur_soil_moss", thresholds=[0.62, 0.36])
DIRT = [STONE[3], STONE[2], STONE[1]]
SPIT = [hex_rgba('#fff4d6'), AMBER[2], DANGER[2], DANGER[1]]


def spine(t, h, sway, wig):
    return (BASE[0] + sway * t * t + wig * math.sin(t * math.pi), BASE[1] - h * t)


def tangent(t, h, sway, wig):
    a = spine(max(0.0, t - 0.02), h, sway, wig)
    b = spine(min(1.0, t + 0.02), h, sway, wig)
    dx, dy = b[0] - a[0], b[1] - a[1]
    n = math.hypot(dx, dy) or 1.0
    return dx / n, dy / n


def mound(cv, front, spread=1.0, rise=0.0):
    """Dirt rim around the hole; `front` = the half drawn over the worm."""
    cv.part()
    if not front:
        cv.ellipse((16.0, 31.4 - rise), 8.2 * spread, 2.4 + rise * 0.5, SOIL, bias=-0.1)
        cv.ellipse((10.2, 30.6 - rise), 2.4 * spread, 1.6, SOIL_MOSS, bias=0.0, decal=True)
    else:
        cv.ellipse((9.6 - 1.4 * (spread - 1), 31.0 - rise * 0.6), 3.6 * spread, 1.8 + rise * 0.3, SOIL, bias=0.05)
        cv.ellipse((22.6 + 1.4 * (spread - 1), 31.2 - rise * 0.6), 3.4 * spread, 1.6 + rise * 0.3, SOIL, bias=0.0)
        cv.ellipse((8.8, 30.0 - rise * 0.6), 1.8, 0.8, SOIL_MOSS, bias=0.15, decal=True)
        cv.px_set(24.0, 29.8 - rise * 0.6, SOIL_MOSS, inten=0.7, decal=True)


def draw(h=18.0, sway=0.0, wig=0.0, head_a=0.5, mouth=0.0, glow=0.0, dead=False, seams=1.0, ground=True,
         spread=1.0, rise=0.0, offset=(0.0, 0.0)):
    """h = height of the head above the ground, sway = head x offset, head_a =
    head facing angle (0 = up, + = towards the right)."""
    cv = TCanvas(W, H, offset=offset)
    plate = PLATE_DEAD if dead else PLATE
    seam = SEAM_OFF if (dead or seams < 0.3) else SEAM
    if ground:
        mound(cv, False, spread, rise)
    # body rings from the ground up
    n = max(2, int(h / 2.4))
    for k in range(n):
        t = k / n
        p = spine(t, h, sway, wig)
        tx, ty = tangent(t, h, sway, wig)
        ang = math.atan2(ty, tx) + math.pi / 2
        r = 6.0 - 1.0 * t
        cv.part()
        cv.ellipse(p, r, r * 0.6, plate, ang=ang, bias=0.02)
        # fleshy belly on the front (right) side
        bx, by = p[0] - ty * r * 0.62, p[1] + tx * r * 0.62
        cv.ellipse((bx, by), r * 0.36, r * 0.5, BELLY, ang=ang, decal=True, bias=0.05)
        # glowing spiracle on the back side of every other ring
        if k % 2 == 1:
            sx, sy = p[0] + ty * r * 0.55, p[1] - tx * r * 0.55
            cv.px_set(sx, sy, seam, inten=0.55 + 0.3 * seams, decal=True)
    # head: blunt end of the worm, maw on its front face
    hp = spine(1.0, h, sway, wig)
    ux, uy = math.sin(head_a), -math.cos(head_a)   # facing direction
    px_, py_ = -uy, ux                                # across (perpendicular)
    hc = (hp[0] + ux * 0.6, hp[1] + uy * 0.6)
    cv.part()
    cv.ellipse(hc, 5.6, 4.6, plate, ang=head_a, bias=0.06)
    # bony ridge on the back of the head
    cv.part(line=False)
    back = (hc[0] - ux * 2.6, hc[1] - uy * 2.6)
    cv.line((back[0] + px_ * 2.2, back[1] + py_ * 2.2), (back[0] - px_ * 2.2, back[1] - py_ * 2.2), plate, inten=0.9,
            decal=True)
    # maw on the front face
    cv.part(line=False)
    mc = (hc[0] + ux * 2.5, hc[1] + uy * 2.5)
    am = head_a + math.pi / 2   # ellipse x-axis across the facing direction
    if mouth > 0.15:
        mrx = 1.4 + 2.4 * mouth
        mry = 1.0 + 1.3 * mouth
        cv.ellipse(mc, mrx + 0.9, mry + 0.8, BELLY, ang=am, bias=0.1)
        cv.ellipse(mc, mrx, mry, MAW, ang=am, flat=0.4)
        if not dead:
            cv.ellipse((mc[0] + ux * 0.3, mc[1] + uy * 0.3), mrx * 0.55, mry * 0.55, GULLET, ang=am,
                       flat=0.42 + 0.4 * glow + 0.1 * mouth)
            cv.px_set(mc[0] + ux * 0.3, mc[1] + uy * 0.3, GULLET, inten=0.62 + 0.38 * glow)
        # ring of teeth just inside the lip
        nt = 10
        for k in range(nt):
            a = TAU * k / nt + 0.3
            ca, sa = math.cos(a), math.sin(a)
            qx = mc[0] + px_ * ca * mrx * 0.9 + ux * sa * mry * 0.9
            qy = mc[1] + py_ * ca * mrx * 0.9 + uy * sa * mry * 0.9
            cv.px_set(qx, qy, TOOTH, inten=0.9 if qy < mc[1] + 0.5 else 0.4)
    else:
        # closed: puckered lips with tooth tips
        cv.ellipse(mc, 2.0, 1.2, BELLY, ang=am, bias=0.1)
        cv.line((mc[0] - px_ * 1.4, mc[1] - py_ * 1.4), (mc[0] + px_ * 1.4, mc[1] + py_ * 1.4), MAW, inten=0.3)
        cv.px_set(mc[0] + px_ * 0.6 - ux * 0.5, mc[1] + py_ * 0.6 - uy * 0.5, TOOTH, inten=0.9)
        cv.px_set(mc[0] - px_ * 0.8 + ux * 0.5, mc[1] - py_ * 0.8 + uy * 0.5, TOOTH, inten=0.4)
    # sensory pits (eyes) above the maw
    if not dead:
        ex, ey = hc[0] + ux * 0.4 - px_ * 2.8, hc[1] + uy * 0.4 - py_ * 2.8
        cv.px_set(ex, ey, GULLET, inten=0.5 + 0.3 * glow)
        cv.px_set(ex - ux * 1.2 + px_ * 0.3, ey - uy * 1.2 + py_ * 0.3, GULLET, inten=0.45 + 0.3 * glow)
    if ground:
        mound(cv, True, spread, rise)
    return cv.render()


def dirt(img, t, seed=0, n=10, power=1.0, cx=16.0):
    """Flying dirt clods + dust for a burst that started t frames ago."""
    rng = np.random.default_rng(seed)
    for k in range(n):
        vx = (rng.random() - 0.5) * 9.0 * power
        vy = -(2.5 + rng.random() * 3.5) * power
        x = cx + vx * t
        y = 30.0 + vy * t * 1.6 + 2.2 * t * t
        if y < 31 and 0 <= x < W:
            sz = 1 + int(rng.random() * 2.2)
            FX.chunk(img, x, y, sz, DIRT[0], DIRT[2])
    return img


def dust(img, r, seed=0, cy=30.0):
    FX.burst(img, 16, cy, r, [STONE[4], STONE[3], STONE[2]], seed=seed, jag=0.45)
    return img


def clips():
    idle = []
    for i in range(4):
        ph = TAU * i / 4
        idle.append(draw(h=17.6 + 0.6 * math.sin(ph), sway=1.6 * math.sin(ph), wig=-0.8 * math.sin(ph),
                         head_a=0.55 + 0.12 * math.sin(ph), mouth=0.0, glow=0.1))
    move = []
    for i in range(4):
        ph = TAU * i / 4
        f = draw(h=15.8 + 0.8 * math.cos(ph), sway=3.0 + 1.0 * math.sin(ph), wig=-1.6 * math.sin(ph + 0.8),
                 head_a=0.85 + 0.1 * math.sin(ph), spread=1.05)
        for (x, y, sz) in ((5 - (i % 2), 28 - (i % 3), 1), (3 + (i % 2), 26 + (i % 2), 2)):
            FX.chunk(f, x, y, sz, DIRT[0], DIRT[2])
        move.append(f)
    # dive: head arcs over and plunges in front, dirt bursts
    tout = [
        draw(h=19.4, sway=-1.5, wig=-1.0, head_a=0.3, spread=1.0),
        draw(h=16.7, sway=2.5, wig=-2.0, head_a=1.6, spread=1.05),
        draw(h=11.4, sway=4.5, wig=-1.0, head_a=2.4, spread=1.15, rise=0.5),
        draw(h=5.3, sway=2.5, wig=0.0, head_a=2.8, spread=1.25, rise=1.0),
    ]
    dirt(tout[2], 0.4, seed=1, n=6)
    dust(tout[3], 4.0, seed=2)
    dirt(tout[3], 0.8, seed=1, n=8)
    cv = TCanvas(W, H)
    mound(cv, False, 1.2, 1.0)
    mound(cv, True, 1.2, 1.0)
    f4 = cv.render()
    dust(f4, 5.0, seed=3, cy=29.5)
    dirt(f4, 1.3, seed=1, n=9)
    cv = TCanvas(W, H)
    mound(cv, False, 1.0, 0.4)
    mound(cv, True, 1.0, 0.4)
    f5 = cv.render()
    dirt(f5, 2.0, seed=1, n=9)
    for (x, y) in ((12, 26), (19, 25), (15, 23)):
        put(f5, x, y, DIRT[1])
    tout += [f4, f5]
    # emerge
    cv = TCanvas(W, H)
    mound(cv, False, 0.9, 1.4)
    mound(cv, True, 0.9, 1.4)
    e0 = cv.render()
    for (x, y) in ((10, 27), (21, 27), (16, 25)):
        put(e0, x, y, DIRT[1])
    e1 = draw(h=4.4, sway=0.0, head_a=0.0, spread=1.25, rise=1.0)
    dust(e1, 4.5, seed=4)
    dirt(e1, 0.5, seed=7, n=8, power=1.2)
    e2 = draw(h=10.6, sway=0.5, head_a=0.1, mouth=0.3, spread=1.15, rise=0.5)
    dirt(e2, 1.0, seed=7, n=8, power=1.2)
    e3 = draw(h=16.7, sway=1.0, wig=0.5, head_a=0.3, mouth=0.5, spread=1.05)
    dirt(e3, 1.6, seed=7, n=8, power=1.2)
    e4 = draw(h=19.4, sway=0.0, wig=-0.5, head_a=0.45, mouth=0.2)
    e5 = draw(h=17.6, sway=0.6, wig=-0.4, head_a=0.55, mouth=0.0)
    tin = [e0, e1, e2, e3, e4, e5]
    tele = []
    for i in range(3):
        t = (i + 1) / 3.0
        tele.append(draw(h=17.6 + 1.5 * t, sway=-1.5 * t, wig=-0.6 * t, head_a=0.6 + 0.25 * t, mouth=0.35 + 0.65 * t,
                         glow=0.4 + 0.6 * t))
    FX.sparkle(tele[2], 21, 9, 2, SPIT[0], SPIT[1])
    a0 = draw(h=18.9, sway=-2.5, wig=-1.0, head_a=0.75, mouth=1.0, glow=1.0)
    a1 = draw(h=17.2, sway=3.0, wig=-1.4, head_a=1.15, mouth=1.0, glow=0.8)
    FX.burst(a1, 27, 13, 2.4, SPIT[:3], seed=3, jag=0.3)
    for (x, y) in ((29, 11), (30, 15), (25, 10)):
        put(a1, x, y, SPIT[1])
    a2 = draw(h=17.6, sway=1.5, wig=-0.8, head_a=0.9, mouth=0.6, glow=0.4)
    a3 = draw(h=17.6, sway=0.5, wig=-0.4, head_a=0.65, mouth=0.2, glow=0.2)
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(h=16.7, sway=-3.0, wig=-1.0, head_a=0.2, mouth=0.7), amount=0.6),
            draw(h=17.2, sway=-2.0, wig=-0.6, head_a=0.35, mouth=0.4)]
    d0 = FX.flash(draw(h=18.5, sway=-2.5, wig=-1.0, head_a=0.1, mouth=1.0, glow=0.6), amount=0.5)
    d1 = draw(h=16.7, sway=3.0, wig=2.0, head_a=1.3, mouth=0.9, glow=0.3, seams=0.6)
    d2 = draw(h=11.4, sway=7.0, wig=1.5, head_a=2.0, mouth=0.6, glow=0.0, seams=0.2)
    d3 = draw(h=6.2, sway=9.0, wig=1.0, head_a=2.2, mouth=0.4, dead=True)
    dust(d3, 2.5, seed=9, cy=30.0)
    d4 = draw(h=5.3, sway=9.5, wig=0.8, head_a=2.3, mouth=0.3, dead=True)
    cols = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]
    death = [d0, d1, d2, d3, d4,
             FX.dissolve(d4, 0.5, seed=17, colors=cols, rise=9, drift=3),
             FX.dissolve(d4, 0.97, seed=17, colors=cols, rise=9, drift=3)]
    return dict(idle=idle, move=move, teleport_out=tout, teleport_in=tin, telegraph=tele, attack=attack,
                hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/burrower.png", W, H, [16, 32])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("move", c["move"], fps=8, loop=True)
    s.add("teleport_out", c["teleport_out"], durations=[0.08, 0.07, 0.06, 0.06, 0.08, 0.1], loop=False)
    s.add("teleport_in", c["teleport_in"], durations=[0.1, 0.06, 0.06, 0.07, 0.08, 0.1], loop=False)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.18], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.08, 0.1, 0.12], loop=False, events=[(1, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.12, 0.14, 0.12, 0.14], loop=False)
    s.save("animations/burrower.json")
