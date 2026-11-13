"""Weaver: spider with a glowing violet crystal abdomen and 8 legs
(32x24, collision 16x12, pivot bottom-centre)."""
import math

import effects as FX
from common.palette import DANGER, SKY, STONE, SUIT, VIOLET, hex_rgba
from raster import Mat, TCanvas, put
from sheet import Sheet

W, H = 32, 24
TAU = 2 * math.pi
GROUND = 23.6

CHITIN = Mat([STONE[3], STONE[2], SKY[4], SKY[3]], name="wv_chitin", thresholds=[0.84, 0.6, 0.36])
CHITIN_FAR = Mat([SKY[4], SKY[3], SKY[2]], name="wv_chitin_far", thresholds=[0.62, 0.36])
CRYSTAL = Mat([hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], emissive=True, name="wv_crystal",
              thresholds=[0.86, 0.66, 0.4])
CRYSTAL_DEAD = Mat([hex_rgba('#8a7aa6'), hex_rgba('#5e4f80'), hex_rgba('#3b2f5c'), hex_rgba('#271e40')],
                   name="wv_crystal_dead", thresholds=[0.86, 0.62, 0.36])
FACET = Mat([hex_rgba('#3b1a6e')], name="wv_facet")
FACET_HI = Mat([hex_rgba('#f6dcff')], emissive=True, name="wv_facet_hi")
EYE = Mat([hex_rgba('#ffe0e6'), DANGER[1], DANGER[0]], emissive=True, name="wv_eye", thresholds=[0.8, 0.45])
FANG = Mat([SUIT[1], SUIT[3]], name="wv_fang", thresholds=[0.5])
WEB = [hex_rgba('#ffffff'), hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1]]
SPARK = [hex_rgba('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]]

# leg attach x on the cephalothorax (back -> front), rest foot x, knee rule
ATT_X = [18.2, 19.4, 20.6, 21.8]
FOOT_X = [6.0, 12.5, 25.5, 28.6]
KNEE_X = [0.42, 0.5, 0.5, 0.45]
KNEE_H = [5.0, 5.6, 5.6, 6.2]


class Pose:
    def __init__(self, **kw):
        self.body = (0.0, 0.0)
        self.rot = 0.0
        self.feet = None       # {(side, i): (x, y)}
        self.glow = 0.0
        self.eye = 0.0
        self.fang = 0.0
        self.dead = False
        self.flip = False
        self.curl = 0.0
        self.offset = (0.0, 0.0)
        self.__dict__.update(kw)


def rest_feet(dx=0.0):
    f = {}
    for i in range(4):
        f[(0, i)] = (FOOT_X[i] + dx, GROUND)
        f[(1, i)] = (FOOT_X[i] + dx + (1.6 if i < 2 else -1.4), GROUND - 0.6)
    return f


def draw(p):
    cv = TCanvas(W, H, rot=p.rot, center=(14.0, 20.0), offset=p.offset,
                 scale=(1.0, -1.0 if p.flip else 1.0))
    bx, by = p.body
    feet = p.feet or rest_feet()

    def leg(side, i):
        far = side == 1
        mat = CHITIN_FAR if far else CHITIN
        a = (ATT_X[i] + bx - (0.6 if far else 0.0), 17.4 + by - (0.6 if far else 0.0))
        f = feet[(side, i)]
        if p.curl:
            # dead (drawn flipped): legs fold up in the air
            f = (a[0] + (i - 1.5) * 2.2, a[1] + 2.0 + 0.8 * math.sin(i * 1.9 + p.curl * 2.0))
            k = (a[0] + (i - 1.5) * 3.2, a[1] + 4.5 + p.curl)
        elif GROUND - f[1] > 3.0:
            # raised (threat pose): femur up, tibia reaching forward
            k = (a[0] + (f[0] - a[0]) * 0.28, min(a[1], f[1]) - 2.6)
        else:
            kh = KNEE_H[i] - (0.8 if far else 0.0) + max(0.0, (GROUND - f[1])) * 0.35
            k = (a[0] + (f[0] - a[0]) * KNEE_X[i], min(a[1], f[1]) - kh)
        cv.part(line=False)
        fl = 0.3 if far else 0.62
        cv.tcapsule(a, k, 0.75, 0.6, mat, flat=fl + 0.12)
        cv.tcapsule(k, f, 0.6, 0.4, mat, flat=fl - 0.1)

    # far legs
    for i in range(4):
        leg(1, i)
    # crystal abdomen (behind / above the cephalothorax): flat lit facets
    cv.part()
    cx, cy = 10.2 + bx * 0.8, 12.4 + by * 0.9
    gem = [(cx - 7.0, cy + 0.8), (cx - 3.8, cy - 4.4), (cx + 1.8, cy - 5.6), (cx + 5.8, cy - 1.8),
           (cx + 5.0, cy + 3.4), (cx - 0.6, cy + 5.2)]
    hub = (cx + 0.2, cy - 0.4)
    cm = CRYSTAL_DEAD if p.dead else CRYSTAL
    g = 0.0 if p.dead else p.glow
    cv.tpoly(gem, cm, flat=0.5 + 0.2 * g)
    cv.tpoly([gem[0], gem[1], gem[2], hub], cm, flat=0.78 + 0.15 * g, decal=True)     # top-left facet
    cv.tpoly([gem[2], gem[3], hub], cm, flat=0.62 + 0.2 * g, decal=True)              # top-right facet
    cv.tpoly([gem[4], gem[5], hub], cm, flat=0.3 + 0.25 * g, decal=True)              # under facet
    cv.tline(hub, gem[3], FACET, decal=True)
    cv.tline(hub, gem[5], FACET, decal=True)
    if not p.dead:
        cv.tline((cx - 5.0, cy - 0.4), (cx - 2.6, cy - 3.6), FACET_HI, decal=True)
        if p.glow > 0.4:
            cv.tpx((cx + 2.6, cy + 1.4), FACET_HI, decal=True)
            cv.tpx((cx + 0.4, cy - 3.4), FACET_HI, decal=True)
    # pedicel
    cv.part()
    cv.tcapsule((cx + 4.6, cy + 2.2), (17.4 + bx, 15.8 + by), 1.1, 1.3, CHITIN)
    # cephalothorax + head
    cv.part()
    cv.tellipse((19.8 + bx, 16.2 + by), 3.6, 2.7, CHITIN, bias=0.04)
    cv.part()
    hx, hy = 23.6 + bx, 16.0 + by
    cv.tellipse((hx, hy), 2.2, 2.0, CHITIN, bias=0.08)
    # chelicerae / fangs
    cv.part()
    fo = p.fang
    cv.tcapsule((hx + 0.6, hy + 1.4), (hx + 1.4 + fo * 0.6, hy + 3.0 + fo * 0.4), 0.8, 0.6, CHITIN)
    cv.part(line=False)
    cv.tpx((hx + 1.6 + fo * 0.8, hy + 3.6 + fo * 0.5), FANG, inten=0.9)
    if fo > 0.4:
        cv.tpx((hx + 2.4 + fo, hy + 2.6), FANG, inten=0.9)
    # eye cluster
    cv.part(line=False)
    if not p.dead:
        e = min(1.0, p.eye)
        cv.tdisc((hx + 0.9, hy - 0.8), 0.75 + 0.3 * e, EYE, flat=0.6)
        cv.tpx((hx + 0.6, hy - 1.2), EYE, inten=1.0 if p.eye > 0.5 else 0.6)
        cv.tpx((hx - 0.8, hy - 1.4), EYE, inten=0.6)
        cv.tpx((hx + 2.0, hy + 0.2), EYE, inten=0.6)
    # near legs
    for i in range(4):
        leg(0, i)
    return cv.render()


def gait_feet(ph, stride=2.2, lift=1.8):
    f = {}
    for side in (0, 1):
        for i in range(4):
            g = (i + side) % 2
            q = TAU * (ph + 0.5 * g)
            base = FOOT_X[i] + ((1.6 if i < 2 else -1.4) if side == 1 else 0.0)
            gy = GROUND - (0.6 if side == 1 else 0.0)
            f[(side, i)] = (base + stride * math.cos(q), gy - max(0.0, math.sin(q)) * lift)
    return f


def web_shot(img, x0, y0, x1, y1, r):
    """Web strand from the fangs to a web ball."""
    n = max(2, int(abs(x1 - x0)))
    for k in range(n + 1):
        t = k / n
        x = int(round(x0 + (x1 - x0) * t))
        y = int(round(y0 + (y1 - y0) * t + math.sin(t * math.pi) * 0.6))
        put(img, x, y, WEB[1] if k % 2 else WEB[2])
    FX.burst(img, x1, y1, r, WEB[:3], seed=5, jag=0.35)
    for (dx, dy) in ((-1, -2), (1, 2), (2, -1)):
        put(img, int(x1 + dx * r * 0.9), int(y1 + dy * r * 0.6), WEB[1])
    return img


def clips():
    idle = []
    for i in range(4):
        ph = TAU * i / 4
        idle.append(draw(Pose(body=(0.0, 0.4 * math.sin(ph)), glow=0.1 + 0.15 * math.sin(ph),
                              feet=rest_feet(), fang=0.2 * (i % 2))))
    walk = []
    for i in range(6):
        ph = i / 6
        walk.append(draw(Pose(body=(0.0, 0.5 * abs(math.sin(TAU * ph))), feet=gait_feet(ph), glow=0.1)))
    tele = []
    for i in range(3):
        t = (i + 1) / 3.0
        f = rest_feet()
        # front legs raised and spread, mid legs braced
        f[(0, 3)] = (29.0 + 1.5 * t, GROUND - 11.0 * t)
        f[(1, 3)] = (27.5 + 1.5 * t, GROUND - 9.0 * t)
        f[(0, 2)] = (27.0, GROUND - 1.5 * t)
        img = draw(Pose(feet=f, rot=-0.28 * t, offset=(0.0, -0.5 * t), glow=0.3 + 0.7 * t, eye=t * 1.2,
                        fang=0.6 * t))
        tele.append(img)
    FX.sparkle(tele[1], 8, 4, 1, SPARK[0], SPARK[1])
    FX.sparkle(tele[2], 9, 3, 2, SPARK[0], SPARK[1])
    FX.sparkle(tele[2], 4, 9, 1, SPARK[0], SPARK[2])
    f0 = rest_feet()
    f0[(0, 3)] = (28.0, GROUND - 4.0)
    a0 = draw(Pose(feet=f0, rot=-0.12, glow=0.8, eye=1.0, fang=1.0, offset=(-1.0, 0.0)))
    f1 = rest_feet()
    a1 = draw(Pose(feet=f1, rot=0.08, glow=0.6, eye=1.0, fang=1.0, body=(1.0, 0.5)))
    web_shot(a1, 27, 20, 30, 17, 1.8)
    a2 = draw(Pose(feet=rest_feet(), rot=0.04, glow=0.3, eye=0.6, fang=0.5, body=(0.5, 0.3)))
    for k, (x, y) in enumerate(((28, 19), (29, 18), (30, 18), (31, 17))):
        put(a2, x, y, WEB[1] if k % 2 else WEB[2])
    a3 = draw(Pose(feet=rest_feet(), glow=0.15, eye=0.3, fang=0.2))
    attack = [a0, a1, a2, a3]
    hurt = [FX.flash(draw(Pose(rot=-0.12, offset=(-1, 0), fang=0.8)), amount=0.6),
            draw(Pose(rot=-0.06, offset=(-1, 0), fang=0.4))]
    d0 = FX.flash(draw(Pose(rot=-0.15, fang=1.0, eye=1.0, glow=0.6)), amount=0.5)
    fd = {k: (v[0] + (2.0 if v[0] > 16 else -2.0), v[1]) for k, v in rest_feet().items()}
    d1 = draw(Pose(feet=fd, body=(0.0, 2.5), glow=0.0, fang=0.6, eye=0.3))
    d2 = draw(Pose(flip=True, curl=0.5, dead=True, offset=(0.0, -7.0)))
    d3 = draw(Pose(flip=True, curl=1.4, dead=True, offset=(0.0, -7.0)))
    death = [d0, d1, d2, d3,
             FX.dissolve(d3, 0.5, seed=13, colors=SPARK, rise=8, drift=3),
             FX.dissolve(d3, 0.97, seed=13, colors=SPARK, rise=8, drift=3)]
    return dict(idle=idle, walk=walk, telegraph=tele, attack=attack, hurt=hurt, death=death)


def build():
    c = clips()
    s = Sheet("sprites/enemies/weaver.png", W, H, [16, 24])
    s.add("idle", c["idle"], fps=6, loop=True)
    s.add("walk", c["walk"], fps=12, loop=True)
    s.add("telegraph", c["telegraph"], durations=[0.12, 0.12, 0.18], loop=False)
    s.add("attack", c["attack"], durations=[0.08, 0.08, 0.1, 0.14], loop=False, events=[(1, "fire")])
    s.add("hurt", c["hurt"], durations=[0.08, 0.12], loop=False)
    s.add("death", c["death"], durations=[0.08, 0.1, 0.12, 0.14, 0.12, 0.14], loop=False)
    s.save("animations/weaver.json")
