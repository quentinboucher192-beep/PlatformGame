"""The people of the stories told in animated scenes (2.63): children, villagers, elders, workers —
a small pixel-art rig drawn frame by frame like the game's sprites, shared by the giga bosses'
stories (the Monolithe's children of the cave, Ossian and the people of the catacombs, the workers
of the Titan's foundry…).

Side view facing right (the scenes flip it), frame 40x56, pivot [20, 54] (between the feet, on the
ground).  An adult is 34 px tall (68 units of a story's stage), a child about 25.  A `Folk` is a look
(skin, hair, clothes, a robe or a tunic, a beard, a hood, what it holds: a torch, a candle, a
lantern, a hammer, a chisel); `clips(folk, names)` gives its clips:

  idle 6, walk 8, run 8, cheer 6 (jumps, arms up), wave 6, carve 6 (kneels, taps a rock in front of
  it), kneel 6 (on one knee, breathing), look_up 4 -> gaze 6, hammer 6 (two hands over the head,
  strikes), sad 6 (head low), flee 8 (runs, looking back), point 6 (shows something ahead, high),
  sit 6 (on a chair whose seat is 12 px high, head low).

Deterministic."""
import math

from boss_a_common import mat
from raster import Canvas, ik2

PI = math.pi
TAU = 2 * PI
FW, FH = 40, 56
PIVOT = [20, 54]
CX, FY = 20.0, 54.0


def _rot(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


class Pose:
    def __init__(self, **kw):
        self.hip = (0.0, -14.0)    # model coords: x forward, y down, origin between the feet
        self.lean = 0.0            # radians, forward
        self.ff, self.fb = (2.0, 0.0), (-2.0, 0.0)  # feet
        self.hf, self.hb = None, None               # hands (None: hanging)
        self.tilt = 0.0            # head: negative looks up
        self.mouth = 0.0
        self.blink = False
        self.item_k = 0            # the item's flicker
        self.item_ang = None       # a hammer's angle (radians from the hand)
        self.knee_b = None         # a knee on the ground (kneeling)
        for k, v in kw.items():
            setattr(self, k, v)


class Folk:
    def __init__(self, skin="#e8b890", hair="#3a2618", cloth="#4a6a9a", trim="#c8a050", legs="#4a3a30", shoes="#2a1e18",
                 size=1.0, child=False, robe=False, beard=None, hood=None, hairstyle="short", item=None, bald=False,
                 apron=None, helmet=None, flame=("#fff6c8", "#ffd060", "#ff8a2a")):
        self.s = size * (0.72 if child else 1.0)
        self.child, self.robe, self.hairstyle, self.item, self.bald = child, robe, hairstyle, item, bald
        self.skin = mat("fk_skin", [skin, _dk(skin, 0.82), _dk(skin, 0.64)], th=[0.7, 0.4])
        self.hair = mat("fk_hair", [_lt(hair, 1.3), hair, _dk(hair, 0.7)], th=[0.75, 0.42])
        self.cloth = mat("fk_cloth", [_lt(cloth, 1.25), cloth, _dk(cloth, 0.72), _dk(cloth, 0.52)], th=[0.82, 0.58, 0.34])
        self.cloth_b = mat("fk_cloth_b", [_dk(cloth, 0.8), _dk(cloth, 0.6), _dk(cloth, 0.45)], th=[0.7, 0.4])
        self.trim = mat("fk_trim", [_lt(trim, 1.2), trim, _dk(trim, 0.7)], th=[0.72, 0.4])
        self.legs = mat("fk_legs", [_lt(legs, 1.2), legs, _dk(legs, 0.7)], th=[0.72, 0.4])
        self.legs_b = mat("fk_legs_b", [_dk(legs, 0.75), _dk(legs, 0.55)], th=[0.5])
        self.shoes = mat("fk_shoes", [_lt(shoes, 1.3), shoes], th=[0.6])
        self.beard = mat("fk_beard", [beard, _dk(beard, 0.8), _dk(beard, 0.62)], th=[0.72, 0.4]) if beard else None
        self.hood = mat("fk_hood", [_lt(hood, 1.2), hood, _dk(hood, 0.7), _dk(hood, 0.5)], th=[0.8, 0.55, 0.3]) if hood else None
        self.apron = mat("fk_apron", [_lt(apron, 1.15), apron, _dk(apron, 0.7)], th=[0.72, 0.4]) if apron else None
        self.helmet = mat("fk_helmet", [_lt(helmet, 1.35), helmet, _dk(helmet, 0.7), _dk(helmet, 0.5)], th=[0.8, 0.55, 0.3]) if helmet else None
        self.eye = mat("fk_eye", ["#1a1420"], outline=None)
        self.wood = mat("fk_wood", ["#a07a4a", "#7a5634", "#523820"], th=[0.7, 0.4])
        self.iron = mat("fk_iron", ["#d8dce8", "#9aa0b4", "#646a80", "#3c4054"], th=[0.82, 0.58, 0.34])
        self.wax = mat("fk_wax", ["#fff6e0", "#e8d8b4", "#c0ac88"], th=[0.7, 0.4])
        self.fire = [mat("fk_f0", [flame[0]], emissive=True, outline=None), mat("fk_f1", [flame[1]], emissive=True, outline=None),
                     mat("fk_f2", [flame[2]], emissive=True, outline=None)]

    def P(self, p):
        return (CX + p[0] * self.s, FY + p[1] * self.s)

    def r(self, v):
        return v * self.s

    def draw(self, p):
        cv = Canvas(FW, FH)
        s = self.s
        hip = p.hip
        lean = p.lean

        def T(v):  # torso local -> model
            q = _rot(v, lean)
            return (hip[0] + q[0], hip[1] + q[1])

        sh_f, sh_b = T((1.0, -12.0)), T((-0.6, -12.0))
        neck = T((0.6, -13.0))
        headc = (neck[0] + 0.8 + math.sin(p.tilt) * 1.2, neck[1] - 5.0 * (1.1 if self.child else 1.0))
        hr = 5.0 * (1.15 if self.child else 1.0)

        def hand(default_side, h):
            return h if h is not None else (default_side[0] + 0.5, default_side[1] + 10.5)

        hf, hb = hand(sh_f, p.hf), hand(sh_b, p.hb)
        # back arm, back leg
        self._arm(cv, sh_b, hb, self.cloth_b, back=True)
        self._leg(cv, hip, p.fb, p.knee_b, self.legs_b, back=True)
        # front leg
        self._leg(cv, hip, p.ff, None, self.legs, back=False)
        # torso: tunic or robe
        cv.part()
        if self.robe:
            pts = [T((-3.4, -12.6)), T((3.6, -12.6)), T((4.0, -4)), (hip[0] + 6.4 + p.ff[0] * 0.3, -0.8),
                   (hip[0] - 5.6 + p.fb[0] * 0.3, -0.8), T((-3.8, -3))]
        else:
            pts = [T((-3.4, -12.6)), T((3.6, -12.6)), T((3.4, -2)), T((4.6, 3.2)), T((-4.2, 3.2)), T((-3.4, -2))]
        cv.poly([self.P(q) for q in pts], self.cloth, bevel=1.4)
        cv.part(line=False)
        belt = [T((-3.6, -3.4)), T((3.7, -3.4)), T((3.7, -2.2)), T((-3.6, -2.2))]
        cv.poly([self.P(q) for q in belt], self.trim, flat=0.6)
        if self.apron:
            ap = [T((1.6, -9)), T((3.9, -9)), T((4.6, 2.5 if not self.robe else 8)), T((1.0, 2.5 if not self.robe else 8))]
            cv.poly([self.P(q) for q in ap], self.apron, bevel=1.0)
        # head
        cv.part()
        cv.disc(self.P(headc), self.r(hr), self.skin)
        hcx, hcy = self.P(headc)
        if self.hood:
            cv.part()
            cv.ellipse((hcx - self.r(1.2), hcy - self.r(0.6)), self.r(hr + 1.2), self.r(hr + 1.0), self.hood)
            cv.part(line=False)
            cv.ellipse((hcx + self.r(1.6), hcy + self.r(0.8)), self.r(hr - 1.4), self.r(hr - 1.0), self.skin)
        elif self.helmet:
            cv.part()
            cv.ellipse((hcx - self.r(0.4), hcy - self.r(1.6)), self.r(hr + 0.8), self.r(hr * 0.75), self.helmet,
                       within=lambda x, y: y < hcy - self.r(0.4))
            cv.rect(int(hcx - self.r(hr + 1.5)), int(hcy - self.r(0.9)), int(hcx + self.r(hr + 1.8)) + 1, int(hcy - self.r(0.9)) + 1,
                    self.helmet, flat=0.5)
        elif not self.bald:
            cv.part(line=1)
            cv.ellipse((hcx - self.r(1.0), hcy - self.r(1.4)), self.r(hr + 0.4), self.r(hr * 0.86), self.hair,
                       within=lambda x, y: (y < hcy + self.r(0.2)) | (x < hcx - self.r(1.8)))
            if self.hairstyle == "long":
                cv.capsule((hcx - self.r(3.4), hcy), (hcx - self.r(3.8), hcy + self.r(6.5)), self.r(2.2), self.r(1.6), self.hair)
            elif self.hairstyle == "tuft":
                cv.capsule((hcx - self.r(0.5), hcy - self.r(hr)), (hcx + self.r(1.2), hcy - self.r(hr + 2.2)), self.r(1.1), self.r(0.6), self.hair)
        else:  # a white fringe around the back of the head
            cv.part(line=1)
            cv.ellipse((hcx - self.r(2.6), hcy + self.r(0.4)), self.r(2.4), self.r(3.0), self.beard or self.hair)
        if self.beard:
            cv.part(line=1)
            chin = (hcx + self.r(2.0), hcy + self.r(2.6))
            cv.poly([(chin[0] - self.r(3.5), chin[1] - self.r(1.8)), (chin[0] + self.r(2.4), chin[1] - self.r(1.2)),
                     (chin[0] + self.r(0.8), chin[1] + self.r(4.5)), (chin[0] - self.r(1.6), chin[1] + self.r(3.0))], self.beard,
                    bevel=1.0)
        # face
        cv.part(line=False)
        ex, ey = hcx + self.r(2.6), hcy - self.r(0.2) + math.sin(p.tilt) * self.r(1.2)
        if p.blink:
            cv.px_set(ex, ey, self.skin, 0.2)
            cv.px_set(ex - 1, ey, self.skin, 0.2)
        else:
            cv.px_set(ex, ey, self.eye, 0.5)
        if p.mouth > 0.3:
            cv.px_set(ex + self.r(0.6), ey + self.r(2.4), self.eye, 0.5)
        # front arm, item
        self._arm(cv, sh_f, hf, self.cloth, back=False)
        self._item(cv, hf, p)
        return cv.render()

    def _leg(self, cv, hip, foot, knee_ground, m, back):
        cv.part()
        hp = (hip[0] + (-0.8 if back else 0.6), hip[1])
        if knee_ground is not None:
            knee = knee_ground
        else:
            knee = ik2(hp, foot, 7.0, 7.4, bend=1.0)
        cv.capsule(self.P(hp), self.P(knee), self.r(1.9), self.r(1.6), m)
        cv.capsule(self.P(knee), self.P((foot[0], foot[1] - 0.8)), self.r(1.6), self.r(1.3), m)
        cv.part(line=1)
        f0 = self.P((foot[0] - 0.6, foot[1] - 1.0))
        cv.ellipse((f0[0] + self.r(1.0), f0[1] + self.r(0.1)), self.r(2.4), self.r(1.3), self.shoes)

    def _arm(self, cv, sh, hand, m, back):
        cv.part()
        el = ik2(sh, hand, 5.6, 5.6, bend=-1.0)
        cv.capsule(self.P(sh), self.P(el), self.r(1.6), self.r(1.4), m)
        cv.capsule(self.P(el), self.P(hand), self.r(1.4), self.r(1.2), m)
        cv.part(line=1)
        cv.disc(self.P(hand), self.r(1.35), self.skin)

    def _item(self, cv, hand, p):
        it = self.item
        if not it:
            return
        hx, hy = hand
        k = p.item_k
        if it in ("torch", "candle"):
            if it == "torch":
                cv.part()
                cv.capsule(self.P((hx - 0.6, hy + 2.5)), self.P((hx + 0.6, hy - 6.0)), self.r(0.9), self.r(1.1), self.wood)
                top = (hx + 0.7, hy - 7.0)
                size = 1.0
            else:
                cv.part()
                cv.rect(*[int(v) for v in self.P((hx - 0.8, hy - 4.5))], *[int(v) + 1 for v in self.P((hx + 0.8, hy - 0.5))], self.wax)
                top = (hx, hy - 5.0)
                size = 0.6
            cv.part(line=False)
            fl = [(0.0, 0.0), (0.6, -0.4), (-0.5, -0.2), (0.3, 0.3)][k % 4]
            tip = (top[0] + fl[0] * 1.5, top[1] - 4.5 * size + fl[1] * 2)
            for i, (m, wf) in enumerate(zip(self.fire[::-1], (1.0, 0.7, 0.4))):
                w = 2.0 * size * wf + (0.3 if k % 2 else 0)
                pts = [(top[0] - w, top[1] + 0.8), (top[0] + w, top[1] + 0.8), (top[0] + w * 0.5, top[1] - 1.5 * size),
                       (tip[0], tip[1] + (1.0 - wf) * 2.0)]
                cv.poly([self.P(q) for q in pts], m, flat=0.6)
        elif it == "lantern":
            cv.part()
            cv.line(self.P((hx, hy)), self.P((hx, hy + 2.0)), self.iron, 0.4)
            box = [(hx - 1.8, hy + 2.0), (hx + 1.8, hy + 2.0), (hx + 1.8, hy + 6.0), (hx - 1.8, hy + 6.0)]
            cv.poly([self.P(q) for q in box], self.iron, bevel=0.8)
            cv.part(line=False)
            inner = [(hx - 1.0, hy + 3.0), (hx + 1.0, hy + 3.0), (hx + 1.0, hy + 5.2), (hx - 1.0, hy + 5.2)]
            cv.poly([self.P(q) for q in inner], self.fire[k % 2], flat=0.6)
        elif it in ("hammer", "chisel"):
            ang = p.item_ang if p.item_ang is not None else -0.6
            ln = 8.0 if it == "hammer" else 4.0
            end = (hx + math.cos(ang) * ln, hy + math.sin(ang) * ln)
            cv.part()
            cv.capsule(self.P((hx - math.cos(ang) * 1.0, hy - math.sin(ang) * 1.0)), self.P(end), self.r(0.8), self.r(0.8),
                       self.wood if it == "hammer" else self.iron)
            if it == "hammer":
                n = (-math.sin(ang), math.cos(ang))
                cv.part()
                cv.capsule(self.P((end[0] + n[0] * 2.4, end[1] + n[1] * 2.4)), self.P((end[0] - n[0] * 2.4, end[1] - n[1] * 2.4)),
                           self.r(1.7), self.r(1.7), self.iron)


def _lt(h, k):
    h = h.lstrip("#")
    c = [min(255, int(int(h[i:i + 2], 16) * k + 18 * (k - 1))) for i in (0, 2, 4)]
    return "#%02x%02x%02x" % tuple(c)


def _dk(h, k):
    h = h.lstrip("#")
    c = [int(int(h[i:i + 2], 16) * k) for i in (0, 2, 4)]
    return "#%02x%02x%02x" % tuple(c)


# ======================================================================= clips
def _walk_feet(ph, stride=4.0, lift=2.2):
    f = (math.sin(ph) * stride, -max(0.0, math.cos(ph)) * lift)
    b = (-math.sin(ph) * stride, -max(0.0, -math.cos(ph)) * lift)
    return f, b


def clips(folk, names=None):
    d = folk
    out = []
    want = (lambda n: names is None or n in names)
    if want("idle"):
        fr = []
        for i in range(6):
            br = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hip=(0, -14 + 0.25 * br), hf=(3.6, -18.5 + 0.3 * br) if d.item else None,
                                  blink=i == 4, item_k=i)))
        out.append(("idle", fr, dict(fps=6)))
    if want("walk"):
        fr = []
        for i in range(8):
            ph = TAU * i / 8
            ff, fb = _walk_feet(ph)
            bob = -0.6 * abs(math.sin(ph))
            sw = math.sin(ph)
            hf = (3.8, -19.0 + bob) if d.item else (1.0 - 2.6 * sw, -15.6 + bob)
            fr.append(d.draw(Pose(hip=(0, -14 + bob), lean=0.05, ff=ff, fb=fb, hf=hf, hb=(-0.6 + 2.6 * sw, -15.8 + bob), item_k=i)))
        out.append(("walk", fr, dict(fps=10)))
    if want("run"):
        fr = []
        for i in range(8):
            ph = TAU * i / 8
            ff, fb = _walk_feet(ph, 6.0, 3.6)
            bob = -1.2 * abs(math.sin(ph))
            sw = math.sin(ph)
            hf = (4.4, -21.0 + bob) if d.item else (3.4 - 3.6 * sw, -19.5 + bob)
            fr.append(d.draw(Pose(hip=(0.6, -14 + bob), lean=0.22, ff=ff, fb=fb, hf=hf, hb=(1.0 + 3.6 * sw, -19.5 + bob), item_k=i)))
        out.append(("run", fr, dict(fps=14)))
    if want("flee"):
        fr = []
        for i in range(8):
            ph = TAU * i / 8
            ff, fb = _walk_feet(ph, 6.0, 3.6)
            bob = -1.2 * abs(math.sin(ph))
            fr.append(d.draw(Pose(hip=(0.6, -14 + bob), lean=0.18, ff=ff, fb=fb, tilt=-0.2, mouth=1.0,
                                  hf=(3.0, -33.0 + bob + math.sin(ph) * 1.5), hb=(-4.0, -31.0 + bob - math.sin(ph) * 1.5), item_k=i)))
        out.append(("flee", fr, dict(fps=14)))
    if want("cheer"):
        fr = []
        for i in range(6):
            u = math.sin(PI * i / 6)
            y = -14 - 4.0 * u
            fr.append(d.draw(Pose(hip=(0, y), ff=(2.0, -4.0 * u), fb=(-2.0, -4.0 * u + (0.8 if u > 0.3 else 0)), tilt=-0.15,
                                  mouth=1.0, hf=(3.2, y - 24.0), hb=(-2.6, y - 23.0), item_k=i)))
        out.append(("cheer", fr, dict(durations=[0.08, 0.08, 0.1, 0.12, 0.1, 0.12])))
    if want("wave"):
        fr = []
        for i in range(6):
            w = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hf=(4.6 + 1.6 * w, -35.0 + abs(w)), mouth=0.6 if i % 3 == 0 else 0.0, blink=i == 5, item_k=i)))
        out.append(("wave", fr, dict(fps=8)))
    if want("point"):
        fr = []
        for i in range(6):
            br = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hip=(0, -14 + 0.2 * br), tilt=-0.25, hf=(8.4, -30.0 + 0.4 * br), mouth=0.6 if i < 3 else 0.0,
                                  item_k=i)))
        out.append(("point", fr, dict(fps=6)))
    if want("carve"):
        fr = []
        for i in range(6):
            hit = [0.0, 0.5, 1.0, 0.2, 0.0, 0.0][i]
            hy = -11.0 - 3.0 * (1 - hit)
            hx = 6.6 + 1.6 * hit
            fr.append(d.draw(Pose(hip=(0.4, -9.6), lean=0.25, ff=(5.0, 0.0), fb=(-3.0, 0.0), knee_b=(-1.4, -0.6),
                                  hf=(hx, hy), hb=(6.2, -12.8), item_ang=-1.2 + 1.4 * hit, item_k=i, blink=i == 2)))
        out.append(("carve", fr, dict(durations=[0.12, 0.06, 0.08, 0.1, 0.12, 0.14])))
    if want("kneel"):
        fr = []
        for i in range(6):
            br = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hip=(0.2, -9.4 + 0.2 * br), lean=0.1, ff=(4.6, 0.0), fb=(-3.4, 0.0), knee_b=(-1.2, -0.6),
                                  hf=(4.4, -11.0 + 0.2 * br), tilt=0.2, blink=i == 3, item_k=i)))
        out.append(("kneel", fr, dict(fps=5)))
    if want("look_up"):
        fr = [d.draw(Pose(tilt=-0.45 * i / 3, hip=(-0.2 * i, -14), hf=(3.6, -18.5) if d.item else None, item_k=i)) for i in range(4)]
        out.append(("look_up", fr, dict(durations=[0.1, 0.1, 0.12, 0.2], loop=False, next="gaze")))
        fr = [d.draw(Pose(tilt=-0.45 + 0.04 * math.sin(TAU * i / 6), hip=(-0.6, -14 + 0.2 * math.sin(TAU * i / 6)),
                          hf=(3.6, -18.5) if d.item else None, blink=i == 4, item_k=i)) for i in range(6)]
        out.append(("gaze", fr, dict(fps=5)))
    if want("hammer"):
        fr = []
        for i in range(6):
            u = [0.0, 0.45, 1.0, 0.75, 0.2, 0.0][i]  # 1: the hammer over the head
            hx, hy = 5.5 - 5.5 * u, -17.0 - 20.0 * u
            ang = 0.4 - 2.6 * u
            fr.append(d.draw(Pose(hip=(0, -13.2 - 0.6 * u), lean=0.1 - 0.12 * u, ff=(3.6, 0), fb=(-3.0, 0), hf=(hx, hy),
                                  hb=(hx - 0.8, hy + 1.2), item_ang=ang, mouth=0.6 if i == 0 else 0.0, item_k=i)))
        out.append(("hammer", fr, dict(durations=[0.1, 0.12, 0.16, 0.06, 0.05, 0.14])))
    if want("sit"):  # seated on a chair (its seat 12 px high), hands on the knees, head low
        fr = []
        for i in range(6):
            br = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hip=(-1.5, -12.4 + 0.2 * br), lean=-0.04, ff=(5.2, 0.0), fb=(4.0, 0.0), hf=(4.6, -13.4 + 0.2 * br),
                                  hb=(3.6, -13.6 + 0.2 * br), tilt=0.3, blink=i in (3, 4), item_k=i)))
        out.append(("sit", fr, dict(fps=4)))
    if want("sad"):
        fr = []
        for i in range(6):
            br = math.sin(TAU * i / 6)
            fr.append(d.draw(Pose(hip=(-0.2, -13.6 + 0.2 * br), lean=0.08, tilt=0.35, hf=(1.6, -15.4) if not d.item else (3.6, -18.5),
                                  hb=(-0.4, -15.6), blink=i in (2, 3), item_k=i)))
        out.append(("sad", fr, dict(fps=4)))
    return out
