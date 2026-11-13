"""Stalker: fast lean four-legged predator, dark with cyan stripes and magenta
eyes (32x24, collision 18x12, pivot bottom-centre)."""
import math

import effects as FX
from common.palette import CYAN, SKY, STONE, VIOLET, hex_rgba
from raster import Mat, TCanvas, ik2
from sheet import Sheet

W, H = 32, 24
TAU = 2 * math.pi

HIDE = Mat([STONE[2], SKY[4], SKY[3], SKY[2]], name="stalker_hide", thresholds=[0.86, 0.62, 0.36])
HIDE_FAR = Mat([SKY[3], SKY[2], SKY[1]], name="stalker_far", thresholds=[0.6, 0.35])
STRIPE = Mat([CYAN[2], CYAN[1], CYAN[0]], emissive=True, name="stalker_stripe", thresholds=[0.7, 0.4])
MAG = [hex_rgba('#ffd6f4'), hex_rgba('#ff5ccf'), hex_rgba('#b0208a')]
EYE = Mat(MAG, emissive=True, name="stalker_eye", thresholds=[0.8, 0.45])
MOUTH = Mat([hex_rgba('#b0208a'), hex_rgba('#5a0f46')], name="stalker_mouth", thresholds=[0.5])
FANG = Mat([hex_rgba('#e8eef5')], name="fang")


class S:
    """Stalker pose."""

    def __init__(self, **kw):
        self.hip = (10.5, 15.0)
        self.chest = (19.5, 14.0)
        self.head = (25.0, 12.0)
        self.head_ang = 0.12
        self.jaw = 0.0
        self.tail = 0.0       # tail sway angle
        self.tail_up = 0.0
        self.paws = {  # near/far front/hind paw positions (ground row 23)
            "hn": (10.0, 23.0), "hf": (12.0, 23.0), "fn": (21.0, 23.0), "ff": (19.0, 23.0)}
        self.eye = 0.0
        self.stripes = 1.0
        self.rot = 0.0
        self.offset = (0.0, 0.0)
        self.__dict__.update(kw)


def draw(p):
    cv = TCanvas(W, H, rot=p.rot, center=(16.0, 18.0), offset=p.offset)
    hip, chest = p.hip, p.chest

    def hind(paw, far):
        mat = HIDE_FAR if far else HIDE
        hj = (hip[0] + (1.0 if far else 0.0), hip[1] + 1.0)
        ankle = (paw[0] - 1.2, paw[1] - 2.2)
        knee = ik2(hj, ankle, 3.6, 3.4, 1.0)
        cv.part()
        cv.tcapsule(hj, knee, 1.9 if not far else 1.6, 1.0, mat)
        cv.tcapsule(knee, ankle, 0.9, 0.75, mat)
        cv.tcapsule(ankle, (paw[0], paw[1] - 0.2), 0.75, 0.7, mat)
        cv.tcapsule((paw[0] - 0.3, paw[1]), (paw[0] + 0.9, paw[1]), 0.7, 0.6, mat)

    def front(paw, far):
        mat = HIDE_FAR if far else HIDE
        sj = (chest[0] - (1.0 if far else 0.0), chest[1] + 1.4)
        wrist = (paw[0] - 0.4, paw[1] - 1.2)
        elbow = ik2(sj, wrist, 3.4, 3.4, -1.0)
        cv.part()
        cv.tcapsule(sj, elbow, 1.6 if not far else 1.3, 0.9, mat)
        cv.tcapsule(elbow, wrist, 0.85, 0.75, mat)
        cv.tcapsule(wrist, (paw[0] + 0.9, paw[1]), 0.7, 0.6, mat)

    # far legs
    hind(p.paws["hf"], True)
    front(p.paws["ff"], True)
    # tail
    cv.part()
    tp = [(hip[0] - 1.0, hip[1] - 0.8)]
    a = math.pi + 0.35 + p.tail_up
    for k in range(6):
        a += -0.28 + p.tail * 0.12 * (k + 1) / 3
        tp.append((tp[-1][0] + math.cos(a) * 1.7, tp[-1][1] + math.sin(a) * 1.7))
    cv.tchain(tp, [1.2 - 0.12 * k for k in range(7)], HIDE)
    cv.part(line=False)
    cv.tdisc(tp[-1], 0.9, STRIPE, flat=0.8)
    # body
    cv.part()
    cv.tcapsule(hip, chest, 2.9, 3.5, HIDE, bias=0.02)
    # neck + head
    cv.part()
    neck0 = (chest[0] + 1.0, chest[1] - 0.6)
    cv.tcapsule(neck0, p.head, 2.3, 1.8, HIDE, bias=0.04)
    ha = p.head_ang
    ux, uy = math.cos(ha), math.sin(ha)
    snout = (p.head[0] + ux * 3.6, p.head[1] + uy * 3.6)
    cv.part()
    # ears / horns, swept back
    cv.tcapsule((p.head[0] - 0.5, p.head[1] - 1.4), (p.head[0] - 3.8, p.head[1] - 4.2), 0.9, 0.35, HIDE, bias=0.1)
    cv.part()
    cv.tellipse(p.head, 2.6, 2.0, HIDE, ang=ha, bias=0.05)
    cv.tcapsule(p.head, snout, 1.6, 1.0, HIDE, bias=0.05)
    # jaw
    if p.jaw > 0.05:
        ja = ha + 0.55 * p.jaw
        jaw_end = (p.head[0] + math.cos(ja) * 3.4, p.head[1] + 0.8 + math.sin(ja) * 3.4)
        cv.part()
        cv.tcapsule((p.head[0], p.head[1] + 0.8), jaw_end, 1.0, 0.7, HIDE, bias=-0.1)
        cv.part(line=False)
        mouth = [(p.head[0] + 0.5, p.head[1] + 0.6), (snout[0] + 0.3, snout[1] + 0.4),
                 (jaw_end[0] + 0.2, jaw_end[1] - 0.4)]
        cv.tpoly(mouth, MOUTH, flat=0.6)
        cv.tpx((snout[0] - 0.8, snout[1] + 0.6), FANG, inten=0.9)
        cv.tpx((jaw_end[0] - 1.0, jaw_end[1] - 1.0), FANG, inten=0.9)
    # stripes along the back
    if p.stripes > 0:
        cv.part(line=False)
        L = math.hypot(chest[0] - hip[0], chest[1] - hip[1])
        bx, by = (chest[0] - hip[0]) / L, (chest[1] - hip[1]) / L
        for k, t in enumerate((0.15, 0.4, 0.65, 0.88)):
            sx, sy = hip[0] + bx * L * t, hip[1] + by * L * t
            r = 2.9 + 0.6 * t
            top = (sx - by * -r * 0.0 + by * r * 0.95, sy - bx * r * 0.95)
            low = (sx + by * r * 0.1 - 0.9, sy - bx * r * 0.1 + 1.0)
            cv.tline(top, low, STRIPE, inten=0.5 + 0.3 * p.stripes, decal=True)
        # spine glow
        cv.tline((hip[0] + 1, hip[1] - 2.8), (chest[0] - 1, chest[1] - 3.3), STRIPE, inten=0.85 * p.stripes, decal=True)
    # eye
    cv.part(line=False)
    ex, ey = p.head[0] + ux * 0.6 + uy * 1.0, p.head[1] + uy * 0.6 - ux * 1.0
    cv.tcapsule((ex - 0.4, ey), (ex + 0.6, ey + 0.1), 0.7 + 0.45 * p.eye, 0.6 + 0.4 * p.eye, EYE,
                flat=0.6 + 0.3 * min(1, p.eye))
    # near legs
    hind(p.paws["hn"], False)
    front(p.paws["fn"], False)
    return cv.render()


GROUND = 23.0


def clips():
    idle = []
    for i in range(4):
        b = [0, 0, 1, 0][i] * 0.5
        idle.append(draw(S(chest=(19.5, 14.0 + b), head=(25.0, 12.0 + b), hip=(10.5, 15.0 + b * 0.5),
                           tail=math.sin(TAU * i / 4), stripes=0.8 + 0.2 * math.cos(TAU * i / 4))))
    # gallop keys: (fn, ff, hn, hf, body_dy, tilt, stretch)
    keys = [
        ((25, 21), (23, 22), (5, 22), (7, 23), -1, -0.05, 1.5),   # extended flight
        ((23, 23), (25, 22), (6, 21), (8, 22), 0, 0.0, 0.8),      # front contact
        ((20, 23), (22, 23), (9, 20), (11, 21), 1, 0.06, 0.0),     # front under, hind swing
        ((16, 22), (18, 23), (14, 23), (12, 22), 0, 0.08, -1.0),   # gathered: hind contact
        ((19, 20), (17, 21), (11, 23), (13, 23), 0, 0.02, -0.3),   # hind push
        ((23, 19), (21, 20), (8, 23), (10, 22), -1, -0.04, 0.8),   # launch
    ]
    run = []
    for i, (fn, ff, hn, hf, dy, tilt, st) in enumerate(keys):
        run.append(draw(S(hip=(10.5 - st * 0.5, 15.0 + dy), chest=(19.5 + st * 0.5, 14.0 + dy),
                          head=(25.0 + st * 0.6, 12.5 + dy), head_ang=0.2, rot=tilt,
                          paws={"fn": fn, "ff": ff, "hn": hn, "hf": hf}, tail_up=-0.25,
                          tail=math.sin(TAU * i / 6) * 0.8)))
    tele = []
    for i in range(4):
        t = (i + 1) / 4.0
        tele.append(draw(S(hip=(10.0, 15.0 - 0.6 * t), chest=(19.0, 14.0 + 2.2 * t), head=(24.5 + t, 13.0 + 2.8 * t),
                           head_ang=0.3 * t, jaw=0.3 * t, tail_up=-0.6 * t, tail=0.3 * (i % 2),
                           paws={"fn": (22.0 + t, GROUND), "ff": (20.0 + t, GROUND),
                                 "hn": (10.0 - t, GROUND), "hf": (12.0 - t, GROUND)},
                           eye=t * 1.2, stripes=1.0 + 0.3 * t)))
    FX.sparkle(tele[3], 27, 13, 2, MAG[0], MAG[1])
    attack = [
        draw(S(hip=(10.0, 13.5), chest=(19.5, 12.0), head=(25.5, 10.5), head_ang=0.0, jaw=0.7,
               paws={"fn": (25, 18), "ff": (23, 19), "hn": (5, 22), "hf": (7, 23)}, tail_up=-0.4, eye=1.0,
               rot=-0.15, offset=(1, 0))),
        draw(S(hip=(9.5, 13.0), chest=(20.0, 12.5), head=(26.0, 12.0), head_ang=0.15, jaw=1.0,
               paws={"fn": (27, 17), "ff": (26, 19), "hn": (3, 18), "hf": (5, 19)}, tail_up=-0.2, eye=1.0,
               offset=(2, -1))),
        draw(S(hip=(10.0, 14.0), chest=(20.0, 14.0), head=(25.5, 13.5), head_ang=0.35, jaw=0.6,
               paws={"fn": (26, 22), "ff": (25, 23), "hn": (5, 20), "hf": (7, 21)}, eye=0.8,
               rot=0.1, offset=(3, 0))),
        draw(S(hip=(10.5, 15.0), chest=(20.0, 15.0), head=(25.0, 14.0), head_ang=0.3, jaw=0.2,
               paws={"fn": (24, 23), "ff": (23, 23), "hn": (9, 23), "hf": (11, 22)}, eye=0.5, offset=(2, 0))),
    ]
    recover = [
        draw(S(hip=(10.5, 14.5), chest=(19.5, 15.5), head=(25.0, 14.0), head_ang=0.25,
               paws={"fn": (24, 23), "ff": (22, 23), "hn": (8, 23), "hf": (10, 23)}, offset=(1, 0))),
        draw(S(hip=(10.5, 15.0), chest=(19.5, 14.8), head=(25.0, 13.0), head_ang=0.18,
               paws={"fn": (22, 23), "ff": (20, 23), "hn": (9, 23), "hf": (11, 23)}, offset=(1, 0))),
        draw(S()),
    ]
    hurt = [FX.flash(draw(S(rot=-0.12, offset=(-1, 0), jaw=0.6, head_ang=-0.2)), amount=0.6),
            draw(S(rot=-0.06, offset=(-1, 0), jaw=0.3))]
    d0 = FX.flash(draw(S(rot=-0.15, jaw=0.8, head_ang=-0.3)), amount=0.5)
    d1 = draw(S(hip=(10.5, 16.5), chest=(19.5, 17.0), head=(25.0, 17.0), head_ang=0.5, jaw=0.4,
                paws={"fn": (23, 23), "ff": (21, 23), "hn": (8, 23), "hf": (10, 23)}, stripes=0.6))
    d2 = draw(S(hip=(10.5, 19.0), chest=(19.5, 19.5), head=(25.5, 20.0), head_ang=0.3, jaw=0.2,
                paws={"fn": (24, 23), "ff": (22, 23), "hn": (6, 23), "hf": (8, 23)}, stripes=0.3, tail_up=0.3))
    d3 = draw(S(hip=(10.5, 20.0), chest=(19.5, 20.0), head=(26.0, 21.0), head_ang=0.1,
                paws={"fn": (26, 23), "ff": (24, 23), "hn": (5, 23), "hf": (7, 23)}, stripes=0.0, tail_up=0.5))
    cols = [MAG[0], MAG[1], CYAN[1], CYAN[0]]
    death = [d0, d1, d2, d3,
             FX.dissolve(d3, 0.35, seed=8, colors=cols, rise=9, drift=3),
             FX.dissolve(d3, 0.7, seed=8, colors=cols, rise=9, drift=3),
             FX.dissolve(d3, 1.0, seed=8, colors=cols, rise=9, drift=3)]
    return dict(idle=idle, run=run, telegraph=tele, attack=attack, recover=recover, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/stalker.png", W, H, [16, 24])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("run", c["run"], fps=14, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.1, 0.1, 0.12, 0.16], loop=False)
    s.add("attack", c["attack"], durations=[0.06, 0.08, 0.08, 0.1], loop=False)
    s.add("recover", c["recover"], durations=[0.1, 0.1, 0.12], loop=False)
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.1, 0.14, 0.1, 0.1, 0.12], loop=False)
    s.save("animations/stalker.json")
