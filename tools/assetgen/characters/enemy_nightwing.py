"""Nightwing: bat-like flyer with membranous wings veined with glowing cyan,
magenta eyes and small fangs (32x24, collision 14x10, pivot bottom-centre of
the box = [16, 20]; the wing tips flap above and below the box)."""
import math

import effects as FX
from common.palette import CYAN, SUIT, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W, H = 32, 24
TAU = 2 * math.pi

FUR = Mat([hex_rgba(c) for c in ('#8a78c0', '#62509a', '#443674', '#2c2350')], name="nw_fur",
          thresholds=[0.86, 0.62, 0.38])
FUR_FAR = Mat([hex_rgba(c) for c in ('#55458a', '#3a2e66', '#272048')], name="nw_fur_far",
              thresholds=[0.62, 0.36])
MEMB = Mat([hex_rgba(c) for c in ('#7c58c0', '#5e40a0', '#452c7c')], name="nw_membrane", thresholds=[0.6, 0.42])
MEMB_FAR = Mat([hex_rgba(c) for c in ('#523486', '#3e2668', '#2e1b52')], name="nw_membrane_far",
               thresholds=[0.6, 0.42])
VEIN = Mat([CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="nw_vein", thresholds=[0.75, 0.45])
VEIN_DIM = Mat([CYAN[0], hex_rgba('#167a80')], emissive=True, name="nw_vein_dim", thresholds=[0.5])
MAG = [hex_rgba('#ffd6f4'), hex_rgba('#ff5ccf'), hex_rgba('#b0208a')]
EYE = Mat(MAG, emissive=True, name="nw_eye", thresholds=[0.8, 0.45])
EAR_IN = Mat([hex_rgba('#b0208a'), hex_rgba('#6a1458')], name="nw_ear_in", thresholds=[0.5])
MOUTH = Mat([hex_rgba('#5a0f46'), hex_rgba('#2e0824')], name="nw_mouth", thresholds=[0.5])
FANG = Mat([SUIT[0], SUIT[2]], name="nw_fang", thresholds=[0.5])
CLAW = Mat([SUIT[2], SUIT[3]], name="nw_claw", thresholds=[0.5])
SPARK = [CYAN[3], CYAN[2], MAG[1], MAG[2]]


def wing(cv, S, side, ang, fold, far, glow=1.0, scale=1.0):
    """One wing from shoulder S. side +1 = towards +x. ang = flap angle (+ up).
    fold 0 = spread, 1 = folded against the body."""
    memb = MEMB_FAR if far else MEMB
    fur = FUR_FAR if far else FUR
    vein = VEIN if glow > 0.5 else VEIN_DIM
    sp = scale * (1.0 - 0.38 * fold)

    def P(a, l, base):
        return (base[0] + side * math.cos(a) * l * sp, base[1] - math.sin(a) * l * sp)

    Wr = P(ang, 5.4, S)
    spread = 1.0 - 0.55 * fold
    tips = [P(ang + 0.7 * spread, 7.4, Wr), P(ang - 0.25 * spread, 8.2, Wr), P(ang - 1.2 * spread, 6.6, Wr)]
    hip = (S[0] - side * 0.4, S[1] + 4.0)
    pts = [S, Wr, tips[0]]
    for k in range(2):
        a, b = tips[k], tips[k + 1]
        m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        pts.append((m[0] + (Wr[0] - m[0]) * 0.14, m[1] + (Wr[1] - m[1]) * 0.14))
        pts.append(b)
    m = ((tips[2][0] + hip[0]) / 2, (tips[2][1] + hip[1]) / 2)
    pts.append((m[0] + (S[0] - m[0]) * 0.25, m[1] + (S[1] - m[1]) * 0.25 - 0.4))
    pts.append(hip)
    cv.part()
    cv.tpoly(pts, memb, shade="grad", base=0.5)
    # glowing finger veins (stop short of the membrane tips)
    for k, t in enumerate(tips):
        e = (Wr[0] + (t[0] - Wr[0]) * 0.86, Wr[1] + (t[1] - Wr[1]) * 0.86)
        cv.tline(Wr, e, vein, inten=0.62 if k == 1 else 0.5, decal=True)
    # forearm + thumb claw
    cv.part()
    cv.tcapsule(S, Wr, 1.0, 0.7, fur, bias=0.05)
    th = P(ang + 1.3, 1.6, Wr)
    cv.part(line=False)
    cv.tline(Wr, th, CLAW, inten=0.8)


def draw(ang=0.4, fold=0.0, bob=0.0, rot=0.0, offset=(0.0, 0.0), eye=0.0, mouth=0.0, ears=0.0, claws=0.0,
         glow=1.0, far_ang=None, far_fold=None, dead=False):
    cv = TCanvas(W, H, rot=rot, center=(16.0, 14.0), offset=(offset[0], offset[1] + bob))
    fa = ang if far_ang is None else far_ang
    ff = fold if far_fold is None else far_fold
    # far wing (left of the body, slightly smaller)
    wing(cv, (14.4, 12.6), -1, fa + 0.05, ff, True, glow, scale=0.9)
    # far ear
    cv.part()
    cv.tpoly([(14.8, 8.6), (13.8 - ears * 0.6, 3.6 + ears * 0.8), (16.8, 7.4)], FUR_FAR, shade="grad", base=0.5)
    # dangling feet / claws
    cv.part()
    fx = 1.0 + 3.0 * claws
    cv.tcapsule((14.6, 17.2), (14.4 + fx * 0.8, 19.4 - claws * 0.6), 0.7, 0.55, FUR_FAR)
    cv.tcapsule((16.8, 17.2), (17.0 + fx, 19.6 - claws * 0.4), 0.8, 0.55, FUR)
    cv.part(line=False)
    cv.tpx((15.0 + fx * 0.8, 20.0 - claws * 0.6), CLAW, inten=0.8)
    cv.tpx((17.6 + fx, 20.2 - claws * 0.4), CLAW, inten=0.8)
    if claws > 0.5:
        cv.tpx((18.6 + fx, 19.8 - claws * 0.4), CLAW, inten=0.8)
    # torso
    cv.part()
    cv.tellipse((16.0, 14.6), 3.2, 3.9, FUR, bias=0.0)
    # chest tuft (lighter fur)
    cv.tellipse((17.2, 14.4), 1.3, 2.4, FUR, flat=0.8, decal=True)
    # near wing (in front of the body, behind the head)
    wing(cv, (17.8, 12.8), 1, ang, fold, False, glow)
    # head
    cv.part()
    hc = (17.2, 9.8)
    cv.tellipse(hc, 3.1, 2.8, FUR, bias=0.06)
    # snout
    cv.tellipse((19.8, 10.6), 1.5, 1.2, FUR, bias=0.1)
    # near ear (big, pointed) with magenta inner
    cv.part()
    cv.tpoly([(17.0, 7.8), (19.2 + ears * 0.6, 2.4 + ears * 0.8), (20.2, 8.0)], FUR, shade="grad", base=0.55)
    cv.tline((18.9, 7.2), (19.1 + ears * 0.6, 5.4 + ears * 0.8), EAR_IN, inten=0.3, decal=True)
    # mouth + fangs
    cv.part(line=False)
    if mouth > 0.2:
        cv.tellipse((20.2, 11.8), 1.3, 0.5 + 0.9 * mouth, MOUTH, flat=0.5)
        cv.tpx((19.6, 11.2), FANG, inten=0.9)
        cv.tpx((20.8, 12.6 + 0.6 * mouth), FANG, inten=0.9)
    else:
        cv.tline((19.2, 11.4), (21.0, 11.4), MOUTH, inten=0.2)
        cv.tpx((20.0, 11.9), FANG, inten=0.9)
    # eyes (angry slant), the far one smaller
    if not dead:
        er = 0.7 + 0.35 * min(1.0, eye)
        cv.tcapsule((18.0, 9.0), (19.4, 9.6), er, er * 0.9, EYE, flat=0.6)
        cv.tpx((19.2, 9.2), EYE, inten=1.0)
        if eye > 0.9:
            cv.tpx((18.2, 9.0), EYE, inten=1.0)
        cv.tpx((15.6, 9.4), EYE, inten=0.6 + 0.3 * min(1.0, eye))
    else:
        cv.tline((18.0, 9.4), (19.4, 9.4), MOUTH, inten=0.2)
    return cv.render()


# flap cycle: (angle, fold, body bob) — downstroke 0..3, upstroke 4..5 (folded)
FLAP = [(0.95, 0.0, 1.0), (0.45, 0.0, 0.0), (-0.15, 0.0, -1.0), (-0.65, 0.1, -1.0), (-0.2, 0.55, 0.0),
        (0.55, 0.45, 1.0)]


def clips():
    idle = [draw(ang=a, fold=f, bob=b, ears=0.3 * (i % 2)) for i, (a, f, b) in enumerate(FLAP)]
    move = []
    for i, (a, f, b) in enumerate(FLAP):
        move.append(draw(ang=a - 0.1, fold=f + 0.15, bob=b, rot=0.22, offset=(1, 0), ears=-0.4,
                         far_ang=a - 0.05))
    tele = [
        draw(ang=1.0, fold=0.1, bob=0, eye=0.5, mouth=0.3, ears=0.4, rot=-0.08),
        draw(ang=1.15, fold=0.0, bob=-1, eye=1.0, mouth=0.7, ears=0.8, rot=-0.14),
        draw(ang=1.25, fold=0.0, bob=-1, eye=1.6, mouth=1.0, ears=1.0, rot=-0.18),
    ]
    FX.sparkle(tele[1], 20, 8, 1, MAG[0], MAG[1])
    FX.sparkle(tele[2], 21, 8, 2, MAG[0], MAG[1])
    a0 = draw(ang=1.1, fold=0.35, rot=0.55, offset=(0, 1), eye=1.2, mouth=1.0, claws=0.6, ears=-0.6)
    a1 = draw(ang=0.9, fold=0.5, rot=0.7, offset=(3, 2), eye=1.0, mouth=1.0, claws=1.0, ears=-0.8)
    # claw swipe arc in front of the talons
    for k, (x, y) in enumerate(((27, 14), (28, 16), (28, 18), (27, 20), (25, 22))):
        put(a1, x, y, SPARK[0] if 1 <= k <= 3 else SPARK[1])
    a2 = draw(ang=-0.5, fold=0.0, rot=0.3, offset=(2, 1), eye=0.8, mouth=0.4, claws=0.6)
    a3 = draw(ang=0.3, fold=0.2, rot=0.05, offset=(1, 0), eye=0.4, claws=0.2)
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(ang=0.2, fold=0.4, rot=-0.25, offset=(-1, 0), mouth=0.8), amount=0.6),
            draw(ang=-0.3, fold=0.3, rot=-0.15, offset=(-1, 0), mouth=0.5)]
    d0 = FX.flash(draw(ang=1.0, fold=0.2, rot=-0.3, mouth=1.0, eye=1.0), amount=0.5)
    d1 = draw(ang=-0.7, fold=0.6, rot=0.4, offset=(0, 1), mouth=0.6, glow=0.4, far_ang=-0.2, far_fold=0.8)
    d2 = draw(ang=0.6, fold=0.9, rot=1.3, offset=(0, 2), mouth=0.3, glow=0.0, dead=True, far_ang=-0.9,
              far_fold=0.9)
    d3 = draw(ang=0.2, fold=1.0, rot=2.4, offset=(0, 3), mouth=0.2, glow=0.0, dead=True, far_fold=1.0)
    cols = [MAG[0], MAG[1], CYAN[1], CYAN[0]]
    death = [d0, d1, d2, d3,
             FX.dissolve(d3, 0.5, seed=21, colors=cols, rise=8, drift=3),
             FX.dissolve(d3, 0.97, seed=21, colors=cols, rise=8, drift=3)]
    return dict(idle=idle, move=move, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/nightwing.png", W, H, [16, 20])
    s.add("idle", c["idle"], fps=12, loop=True)
    s.add("move", c["move"], fps=15, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.1, 0.12, 0.18], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.1, 0.1, 0.12], loop=False, events=[(1, "hit")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.12, 0.12, 0.14], loop=False)
    s.save("animations/nightwing.json")
