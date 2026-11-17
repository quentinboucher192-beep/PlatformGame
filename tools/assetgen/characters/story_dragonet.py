"""The hatchlings of Pyrrhax's story (2.60): a small dragon rig drawn like the game's sprites
(1x pixel art through raster.TCanvas: shaded materials, inner contour lines, 1 px outline).

Seen from the side, facing right: a round body on four short legs, a big head (two small horns,
a big eye), two stubby bat wings on the back, a curled tail ending in a spade.  The same rig
draws « la Fumerolle » (grey, the smallest), its fire brothers and sisters (orange, crimson:
bigger, `grow`), the glowing belly of the embers it swallows (`belly`), and its change into black
glass (`obs`: obsidian plates split by magma seams spread from the belly, as Pyrrhax's scales).

Body coordinates: a 112x80 box, the feet on y = 74, x = 56 under the body (the pivot); a frame
places that point where it wants (`origin`).  Deterministic (no randomness but fixed seeds)."""
import math

import numpy as np

from boss_a_common import bezier, mat
from boss_c_common import Pose
from raster import TCanvas

PI = math.pi
TAU = 2 * PI
GROUND = 74.0
PIVOT_X = 56.0

# ------------------------------------------------------------------ palettes (light -> dark)
PALETTES = {
    # la Fumerolle: ash grey, cream belly
    "grey": dict(body=["#d4d1da", "#a9a5b3", "#817d8c", "#5d5969", "#403c4c"],
                 belly=["#f2ead9", "#d6cab4", "#ab9f8a"],
                 wing=["#a39eaf", "#7e7989", "#5c5767", "#433e4e"],
                 horn=["#efe8d8", "#bdb3a0", "#857b68"],
                 spike=["#8c8898", "#686473", "#4a4655"]),
    # the brothers: fire orange, gold belly
    "fire": dict(body=["#ffc06a", "#ff8a34", "#e2561c", "#a8320e", "#641808"],
                 belly=["#fff0b0", "#ffd270", "#eaa040"],
                 wing=["#ff7a3a", "#d24e1c", "#9a2e10", "#5e1608"],
                 horn=["#fff2d4", "#e8c890", "#b08850"],
                 spike=["#ffb050", "#d8641c", "#9a3a10"]),
    # the sisters: crimson, amber belly
    "crimson": dict(body=["#ff9a86", "#f2503e", "#c42a2c", "#86182a", "#4c0c1a"],
                    belly=["#ffe2a0", "#ffbc62", "#e08a3a"],
                    wing=["#f0605a", "#c03238", "#861c28", "#4c0c18"],
                    horn=["#fff0d8", "#e8c4a0", "#b08868"],
                    spike=["#ff8a6a", "#d2403a", "#8e2026"]),
}

EYE_W = mat("dn_eye", ["#ffffff", "#e2e0ec", "#b4b0c4"], th=[0.62, 0.3])
PUPIL = mat("dn_pupil", ["#2a1c30", "#140c18"], th=[0.5])
GLINT = mat("dn_glint", ["#ffffff"], outline=None)
MAW = mat("dn_maw", ["#9a3a4a", "#6a1e30", "#3e0c1a"], th=[0.6, 0.3])
TONGUE = mat("dn_tongue", ["#ff8a9a", "#d85068"], th=[0.5])
CLAW = mat("dn_claw", ["#f4eee2", "#c2b8a6"], th=[0.5])
# light: the swallowed embers (belly), the magma seams (black glass)
FIRE = mat("dn_fire", ["#fffbe8", "#ffe08a", "#ffb347", "#ff7a2a", "#c8401c"], emissive=True, outline=None,
           th=[0.86, 0.68, 0.48, 0.28])
HOT = mat("dn_hot", ["#ffe2b6", "#ff9a3a", "#f25a12", "#a8300e", "#561608"], emissive=True, outline=None,
          th=[0.86, 0.68, 0.48, 0.28])
# black glass (the obsidian of Pyrrhax's sheet)
OBS = mat("dn_obs", ["#928cb8", "#433e68", "#2e2a47", "#201d33", "#151322"], th=[0.86, 0.66, 0.46, 0.27])
OBS_FAR = mat("dn_obs_far", ["#3e3a5a", "#2a2740", "#1c1a2c", "#12111d"], th=[0.78, 0.52, 0.3])
OBS_MEMB = mat("dn_obs_memb", ["#56465f", "#3e3149", "#2c2337", "#1e1727"], th=[0.7, 0.5, 0.3])


class Palette:
    def __init__(self, key):
        p = PALETTES[key]
        self.key = key
        self.body = mat(f"dn_{key}_body", p["body"])
        self.far = mat(f"dn_{key}_far", p["body"][1:], th=[0.74, 0.5, 0.28])
        self.belly = mat(f"dn_{key}_belly", p["belly"], th=[0.66, 0.36])
        self.wing = mat(f"dn_{key}_wing", p["wing"], th=[0.72, 0.5, 0.3])
        self.wing_far = mat(f"dn_{key}_wing_far", p["wing"][1:], th=[0.6, 0.34])
        self.horn = mat(f"dn_{key}_horn", p["horn"], th=[0.66, 0.36])
        self.spike = mat(f"dn_{key}_spike", p["spike"], th=[0.66, 0.36])


PAL = {k: Palette(k) for k in PALETTES}


class DPose(Pose):
    DEFAULTS = dict(
        t=0.0,            # time (sways)
        bob=0.0,          # body up (-) / down (+), px
        tilt=0.0,         # the whole hatchling rotated about its feet (+ = nose down)
        dx=0.0,           # the whole hatchling moved along x
        head=0.0,         # head angle (- = looks up)
        hx=0.0, hy=0.0,   # head moved (px)
        jaw=0.0,          # mouth 0 closed .. 1 wide open
        eye=1.0,          # 1 open .. 0 closed
        look=(0.6, 0.0),  # pupil offset (x forward, y down), -1..1
        happy=0.0,        # > 0.5: eyes closed in a smile (laughing)
        wing=0.25,        # near wing raised 0 (folded) .. 1 (up)
        wing_far=None,    # far wing (default: the near one, a little behind)
        tail=0.0,         # tail sway phase
        tail_lift=0.0,    # tail raised (+) / dragged (-)
        step=None,        # walk phase (radians) or None (standing)
        crouch=0.0,       # legs bent 0..1
        chest=0.0,        # chest puffed (breathing in) 0..1
        belly=0.0,        # belly glowing (swallowed embers) 0..1.4
        throat=-1.0,      # a swallowed ember going down the neck (0 mouth .. 1 belly), -1: none
        obs=0.0,          # black glass spread from the belly 0..1
        seam=0.6,         # brightness of the magma seams of the black glass
        grow=1.0,         # scale of the whole hatchling (about its feet)
        tremble=0.0,      # shaking (px)
        frill=0.0,        # cheek spikes raised
    )


def _hash(a, b, salt=0.0):
    x = np.sin(np.asarray(a, np.float64) * 12.9898 + np.asarray(b, np.float64) * 78.233 + salt * 37.719) * 43758.5453
    return x - np.floor(x)


def _plate_seeds(seed=6173, n=46):
    rng = np.random.default_rng(seed)
    pts = []
    for j, y in enumerate(range(18, 80, 6)):
        for x in range(0, 112, 7):
            pts.append((x + (j % 2) * 3.5 + rng.uniform(-1.6, 1.6), y + rng.uniform(-1.4, 1.4)))
    return np.array(pts, np.float32)


PLATES = _plate_seeds()
BELLY_C = (63.0, 61.0)


class Dragonet:
    """Draws a hatchling into a W x H frame, its feet point (56, 74) of body space at `origin`."""

    def __init__(self, palette="grey", w=128, h=88, origin=(64.0, 82.0)):
        self.pal = PAL[palette]
        self.W, self.H = w, h
        self.origin = origin

    # ------------------------------------------------------------ geometry
    def canvas(self, p):
        ox = self.origin[0] - PIVOT_X + p.dx
        oy = self.origin[1] - GROUND
        tx = p.tremble * math.sin(p.t * 61.0) if p.tremble else 0.0
        ty = p.tremble * 0.6 * math.cos(p.t * 47.0) if p.tremble else 0.0
        return TCanvas(self.W, self.H, rot=p.tilt, center=(PIVOT_X, GROUND), offset=(ox + tx, oy + ty),
                       scale=(p.grow, p.grow))

    @staticmethod
    def local(cv):
        """Body coordinates of every pixel centre (inverse of the canvas transform)."""
        x = cv.px - cv.t_off[0] - cv.t_c[0]
        y = cv.py - cv.t_off[1] - cv.t_c[1]
        c, s = math.cos(-cv.t_rot), math.sin(-cv.t_rot)
        x, y = x * c - y * s, x * s + y * c
        return cv.t_c[0] + x / cv.t_s[0], cv.t_c[1] + y / cv.t_s[1]

    def head_fn(self, p):
        b = p.bob
        hx, hy = 71.0 + p.hx, 38.0 + p.hy + b * 0.8
        a = p.head
        ca, sa = math.cos(a), math.sin(a)

        def HL(x, y):
            return (hx + x * ca - y * sa, hy + x * sa + y * ca)

        ja = p.jaw * 0.75
        jc, js = math.cos(ja), math.sin(ja)

        def JL(x, y):  # lower jaw, hinged at (1, 4)
            dx, dy = x - 1.0, y - 4.0
            return HL(1.0 + dx * jc - dy * js, 4.0 + dx * js + dy * jc)

        return HL, JL

    def mouth(self, p):
        """Body coordinates of the mouth (where breath comes out) and its direction."""
        HL, JL = self.head_fn(p)
        a, b = HL(19.0, 3.0), JL(16.0, 5.5)
        return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2), p.head + 0.35 * p.jaw

    def frame_point(self, p, q):
        """Body coordinates -> frame pixels (the canvas transform)."""
        cv = self.canvas(p)
        return cv.T(q)

    # ------------------------------------------------------------ drawing
    def draw(self, p):
        cv = self.canvas(p)
        self._wing(cv, p, far=True)
        self._leg_hind(cv, p, far=True)
        self._leg_fore(cv, p, far=True)
        self._tail(cv, p)
        self._body(cv, p)
        self._leg_hind(cv, p, far=False)
        self._neck(cv, p)
        self._head(cv, p)
        self._leg_fore(cv, p, far=False)
        self._wing(cv, p, far=False)
        if p.belly > 0.02:
            self._belly_glow(cv, p)
        if p.obs > 0.0:
            self._glass(cv, p)
        return cv.render()

    def m(self, p, normal, glass):
        return glass if p.obs >= 0.999 else normal

    # tail: a tapered chain curling up behind, a spade at its tip
    def tail_pts(self, p):
        b = p.bob
        sw = math.sin(p.tail) * 3.0
        lift = p.tail_lift
        pts = bezier((41.0, 60.0 + b * 0.5), (30.0, 68.0), (16.0 + sw * 0.4, 68.0 - lift * 4),
                     (10.0 + sw, 52.0 - lift * 7), 9)
        return pts

    def _tail(self, cv, p):
        pts = self.tail_pts(p)
        n = len(pts)
        radii = [6.4 - 4.9 * (i / (n - 1)) ** 0.8 for i in range(n)]
        cv.part()
        cv.tchain(pts, radii, self.pal.body)
        # the spade
        tip, prev = pts[-1], pts[-2]
        d = math.atan2(tip[1] - prev[1], tip[0] - prev[0])
        c, s = math.cos(d), math.sin(d)

        def L(u, v):
            return (tip[0] + c * u - s * v, tip[1] + s * u + c * v)
        cv.part()
        cv.tpoly([L(-1.5, 0), L(1.5, -3.2), L(5.5, 0), L(1.5, 3.2)], self.pal.spike, bevel=1.0, base=0.6)
        # dorsal ridge along the tail: small dark scutes
        cv.part(line=False)
        for i in range(1, n - 2, 2):
            a, bq = pts[i], pts[i + 1]
            dd = math.atan2(bq[1] - a[1], bq[0] - a[0])
            nx, ny = math.sin(dd), -math.cos(dd)
            r = radii[i]
            q = (a[0] + nx * (r - 0.8), a[1] + ny * (r - 0.8))
            cv.tpx(q, self.pal.spike, inten=0.5, decal=True)

    def _body(self, cv, p):
        b = p.bob
        puff = 1.0 + 0.12 * p.chest
        # back spikes (behind the body)
        cv.part()
        for (x, y, ln, a) in ((40, 50, 4.0, -2.35), (47, 47, 5.0, -2.1), (54, 46, 5.0, -1.95), (61, 47.5, 4.0, -1.8)):
            base = (x, y + b + 2.0)
            c, s = math.cos(a), math.sin(a)
            nx, ny = -s, c
            cv.tpoly([(base[0] + nx * 2.2, base[1] + ny * 2.2), (base[0] + c * ln, base[1] + s * ln),
                      (base[0] - nx * 2.2, base[1] - ny * 2.2)], self.pal.spike, bevel=0.8, base=0.6)
        cv.part()
        cv.tellipse((49.0, 58.5 + b), 13.5, 10.5, self.pal.body, ang=-0.12)
        cv.tellipse((60.5, 57.0 + b), 9.5 * puff, 9.5 * puff, self.pal.body, ang=-0.3)
        body = cv.pid == cv.cur
        # belly scales: the lighter front of the chest and underside of the belly, with plate lines
        cv.part(line=False)
        lx, ly = self.local(cv)
        side = (lx - 58.0) * 0.62 + (ly - (55.0 + b)) * 0.78  # towards the front-bottom
        q = ((lx - 61.5) / (11.0 * puff)) ** 2 + ((ly - (61.5 + b)) / 9.5) ** 2
        bm = body & (q < 1.0) & (side > 3.0)
        inten = np.clip(0.55 + 0.06 * (side - 3.0), 0, 1)
        cv.put(bm, inten, self.pal.belly, decal=True)
        rows = bm & (np.abs((lx * 0.62 - ly * 0.78 + 200.0) % 3.4 - 1.7) < 0.42) & (side > 4.2)
        cv.put(rows, 0.32, self.pal.belly, decal=True)
        if 0.0 <= p.throat <= 1.0 and p.throat > 0.75:  # the ember reaches the belly
            k = (p.throat - 0.75) / 0.25
            cm = body & (((lx - 58) ** 2 + (ly - (58 + b)) ** 2) < (2.5 + 3 * k) ** 2)
            cv.put(cm, 0.9, FIRE, decal=True)

    def _belly_glow(self, cv, p):
        """The swallowed embers shine through the belly (and the legs in front of it)."""
        g = min(1.4, p.belly)
        b = p.bob
        lx, ly = self.local(cv)
        names = {self.pal.body.name, self.pal.belly.name, self.pal.far.name}
        skin = np.zeros(cv.mat.shape, bool)
        for i, mm in enumerate(cv.mats):
            if mm.name in names:
                skin |= cv.mat == i
        d = np.hypot((lx - BELLY_C[0]) / 1.2, ly - (BELLY_C[1] + b))
        r = (2.5 + 8.0 * min(1.0, g)) * (1.0 - 0.3 * min(1.0, p.obs))
        core = skin & (d < r)
        inten = np.clip(0.2 + 0.8 * g * (1.0 - d / r) + 0.06 * math.sin(p.t * 9.0), 0, 1)
        cv.part(line=False)
        cv.put(core, inten, FIRE, decal=True)
        if g > 0.3 and p.obs < 0.5:  # the light shows between the scales around it
            halo = skin & (d >= r) & (d < r + 2.0 + 4.0 * g)
            hv = halo & (np.abs((lx * 0.7 + ly) % 4.5 - 2.25) < 0.45)
            cv.put(hv, 0.12 + 0.22 * min(1.0, g), HOT, decal=True)

    def _leg_hind(self, cv, p, far):
        b = p.bob
        m = self.pal.far if far else self.pal.body
        ox = 7.0 if far else 0.0
        oy = -1.5 if far else 0.0
        hip = (43.0 + ox, 62.0 + oy + b)
        foot = [45.0 + ox, GROUND - 1.5]
        if p.step is not None:
            ph = p.step + (PI if far else 0.0)
            foot[0] += 4.5 * math.cos(ph)
            foot[1] -= max(0.0, 3.0 * math.sin(ph))
        cr = p.crouch * 2.0
        cv.part()
        cv.tellipse((hip[0], hip[1] + 1.0 + cr * 0.5), 6.2, 7.2 - cr * 0.4, m, ang=0.3)
        knee = (hip[0] + 2.0, hip[1] + 5.5 + cr * 0.4)
        cv.part()
        cv.tcapsule(knee, (foot[0], foot[1] - 1.0), 3.4, 2.6, m)
        cv.part()
        cv.tellipse((foot[0] + 2.0, foot[1]), 4.4, 2.1, m, ang=0.0)
        if not far:
            for k in range(3):
                cv.tpx((foot[0] + 5.0 + k * 0.0, foot[1] - 1.0 + k * 1.2), CLAW, inten=0.8)

    def _leg_fore(self, cv, p, far):
        b = p.bob
        m = self.pal.far if far else self.pal.body
        ox = 6.0 if far else 0.0
        oy = -1.5 if far else 0.0
        sh = (58.0 + ox, 61.0 + oy + b * 0.9)
        paw = [60.5 + ox, GROUND - 1.5]
        if p.step is not None:
            ph = p.step + (0.0 if far else PI)
            paw[0] += 4.0 * math.cos(ph)
            paw[1] -= max(0.0, 3.0 * math.sin(ph))
        cv.part()
        el = ((sh[0] + paw[0]) / 2 - 1.0 - p.crouch, (sh[1] + paw[1]) / 2)
        cv.tchain([sh, el, (paw[0], paw[1] - 1.0)], [3.8, 3.0, 2.5], m)
        cv.part()
        cv.tellipse((paw[0] + 1.5, paw[1]), 3.6, 1.9, m)
        if not far:
            for k in range(2):
                cv.tpx((paw[0] + 4.2, paw[1] - 0.6 + k * 1.2), CLAW, inten=0.8)

    def _neck(self, cv, p):
        b = p.bob
        HL, _ = self.head_fn(p)
        top = HL(-5.0, 4.0)
        cv.part()
        cv.tchain([(61.0, 53.0 + b), ((61.0 + top[0]) / 2 + 1.0, (53.0 + b + top[1]) / 2), top], [6.6, 5.6, 5.0],
                  self.pal.body)
        # the throat: lighter scales along the front of the neck
        cv.part(line=False)
        t0, t1 = (66.0, 57.0 + b), HL(-0.5, 7.5)
        cv.tcapsule((t0[0] + 1.2, t0[1]), (t1[0] + 1.0, t1[1]), 2.0, 1.6, self.pal.belly, flat=0.7, decal=True)
        if 0.0 <= p.throat <= 0.8:  # the swallowed ember going down
            k = p.throat / 0.8
            mq = HL(2.0, 6.5)
            q = (mq[0] + (60.0 - mq[0]) * k, mq[1] + (56.0 + b - mq[1]) * k)
            cv.part(line=False)
            cv.tdisc(q, 2.2, FIRE, flat=0.75)

    def _head(self, cv, p):
        HL, JL = self.head_fn(p)
        pal = self.pal
        a = p.head
        # far horn
        cv.part()
        cv.tchain([HL(-1.0, -9.0), HL(-6.0, -14.0), HL(-11.0, -15.5)], [2.2, 1.5, 0.6], pal.far)
        # mouth interior (behind the jaws)
        if p.jaw > 0.08:
            cv.part(line=False)
            cv.tpoly([HL(0.0, 3.0), HL(18.0, 2.5), JL(16.0, 5.0), JL(1.0, 5.5)], MAW, flat=0.4)
            cv.tellipse(JL(7.0, 5.2), 4.0, 1.4, TONGUE, ang=a + p.jaw * 0.6, flat=0.7)
        # lower jaw
        cv.part()
        cv.tpoly([JL(-2.0, 3.5), JL(8.0, 4.0), JL(16.5, 5.2), JL(15.0, 7.2), JL(8.0, 8.6), JL(0.0, 8.0)], pal.body,
                 bevel=1.2, base=0.5)
        cv.part(line=False)
        cv.tline(JL(2.0, 7.0), JL(13.0, 6.5), pal.belly, inten=0.75, decal=True)
        # skull: a big round cranium and a short snout
        cv.part()
        cv.tellipse(HL(1.5, -2.0), 10.5, 9.3, pal.body, ang=a)
        cv.tellipse(HL(11.5, 1.2), 8.0, 5.0, pal.body, ang=a + 0.08)
        # frill / cheek spikes at the back of the head
        cv.part()
        fr = p.frill
        for (u, v, ln, d) in ((-8.0, -2.0, 4.0 + 2 * fr, PI - 0.25 - 0.3 * fr), (-7.0, 3.0, 3.5 + 1.5 * fr, PI + 0.35)):
            base = HL(u, v)
            dd = d + a
            c, s = math.cos(dd), math.sin(dd)
            cv.tpoly([(base[0] - s * 1.8, base[1] + c * 1.8), (base[0] + c * ln, base[1] + s * ln),
                      (base[0] + s * 1.8, base[1] - c * 1.8)], pal.spike, bevel=0.8, base=0.6)
        # mouth line when closed, smile corner
        cv.part(line=False)
        if p.jaw <= 0.08:
            cv.tline(HL(9.0, 4.2), HL(18.0, 3.4), pal.spike, inten=0.1, decal=True)
            if p.happy > 0.5:
                cv.tpx(HL(8.5, 3.4), pal.spike, inten=0.1, decal=True)
        cv.tpx(HL(18.0, -0.5), pal.spike, inten=0.05, decal=True)  # nostril
        # brow
        cv.tline(HL(2.0, -7.6), HL(8.0, -7.0), pal.spike, inten=0.25, decal=True)
        # the eye
        self._eye(cv, p, HL)
        # near horn
        cv.part()
        cv.tchain([HL(-3.0, -8.0), HL(-8.0, -12.5), HL(-13.5, -13.0)], [2.6, 1.8, 0.7], pal.horn)
        # nose bump
        cv.part()
        cv.tchain([HL(13.0, -3.2), HL(15.5, -4.6)], [1.2, 0.5], pal.horn)

    def _eye(self, cv, p, HL):
        ec = HL(5.5, -2.5)
        if p.happy > 0.5:  # laughing: a closed, curved eye (^)
            cv.part(line=False)
            cv.tline(HL(3.0, -1.4), HL(5.5, -3.8), PUPIL, inten=0.2)
            cv.tline(HL(5.5, -3.8), HL(8.0, -1.4), PUPIL, inten=0.2)
            return
        if p.eye < 0.25:  # closed: a dark lid line
            cv.part(line=False)
            cv.tline(HL(3.0, -2.0), HL(8.0, -2.2), PUPIL, inten=0.2)
            return
        cv.part()
        ry = 4.2 * max(0.35, p.eye)
        if p.obs >= 0.75:  # black glass: the eye burns like Pyrrhax's
            cv.tellipse(ec, 3.3, ry * 0.8, FIRE, ang=p.head, flat=0.7)
            cv.part(line=False)
            cv.tcapsule(HL(6.0, -4.0), HL(6.4, -0.8), 0.6, 0.6, PUPIL, flat=0.3, decal=True)
            return
        cv.tellipse(ec, 3.3, ry, EYE_W, ang=p.head)
        cv.part(line=False)
        lx, ly = p.look
        pc = HL(5.5 + 0.9 * lx, -2.5 + 0.9 * ly)
        cv.tellipse(pc, 1.6, min(2.6, ry * 0.75), PUPIL, ang=p.head, decal=True)
        if p.eye > 0.6:
            cv.tpx(HL(4.6 + 0.6 * lx, -4.2 + 0.4 * ly), GLINT, inten=1.0, decal=True)
        if p.eye < 0.95:  # the lid coming down
            lid = HL(5.5, -2.5 - ry + 1.0)
            cv.tellipse((lid[0], lid[1] - 1.2), 3.6, 2.2 * (1.2 - p.eye), self.pal.body, ang=p.head, decal=True)

    def _wing(self, cv, p, far):
        b = p.bob
        if far:
            k = p.wing_far if p.wing_far is not None else p.wing * 0.9
            root = (57.0, 47.0 + b)
            m, bone = self.pal.wing_far, self.pal.far
            sc = 0.86
        else:
            k = p.wing
            root = (51.0, 48.5 + b)
            m, bone = self.pal.wing, self.pal.body
            sc = 1.0
        k = max(-0.2, min(1.2, k))
        # arm up (and back) when raised, back along the body when folded; the fingers fan backwards
        a0 = -2.7 + 0.95 * k
        a1 = -2.95 + 0.5 * k
        el = (root[0] + math.cos(a0) * 8.0 * sc, root[1] + math.sin(a0) * 8.0 * sc)
        wr = (el[0] + math.cos(a1) * 7.0 * sc, el[1] + math.sin(a1) * 7.0 * sc)
        tips = []
        for da, ln in ((0.0, 10.0), (0.62, 10.5), (1.25, 8.5)):
            aa = a1 - da * (0.75 + 0.35 * k)
            tips.append((wr[0] + math.cos(aa) * ln * sc, wr[1] + math.sin(aa) * ln * sc))
        back = (root[0] - 12.0 * sc, root[1] + 5.0)
        pts = [root, el, wr, tips[0]]
        for j in range(2):
            mid = ((tips[j][0] + tips[j + 1][0]) / 2, (tips[j][1] + tips[j + 1][1]) / 2)
            pts.append((mid[0] + (wr[0] - mid[0]) * 0.28, mid[1] + (wr[1] - mid[1]) * 0.28))
            pts.append(tips[j + 1])
        mid = ((tips[2][0] + back[0]) / 2, (tips[2][1] + back[1]) / 2)
        pts.append((mid[0] + (wr[0] - mid[0]) * 0.22, mid[1] + (wr[1] - mid[1]) * 0.22))
        pts.append(back)
        cv.part()
        cv.tpoly(pts, m, shade="grad", base=0.52 + 0.08 * k)
        cv.part()
        cv.tchain([root, el, wr], [2.3 * sc, 1.8 * sc, 1.4 * sc], bone)
        for t in tips:
            cv.tchain([wr, t], [1.0 * sc, 0.45 * sc], bone)
        cv.tpx((wr[0], wr[1] - 1.5), CLAW, inten=0.8)

    # ------------------------------------------------------------ black glass
    def _glass(self, cv, p):
        """Obsidian plates split by magma seams, spreading from the belly as p.obs grows (the
        materials of the body are swapped for black glass behind the front)."""
        lx, ly = self.local(cv)
        d = np.hypot(lx - BELLY_C[0], (ly - BELLY_C[1] - p.bob) * 1.2)
        front = 4.0 + 96.0 * p.obs
        wav = 3.0 * np.sin(np.arctan2(ly - BELLY_C[1], lx - BELLY_C[0]) * 5.0 + 1.3)
        inside = d < front + wav
        swap = {self.pal.body.name: OBS, self.pal.far.name: OBS_FAR, self.pal.belly.name: OBS,
                self.pal.spike.name: OBS, self.pal.wing.name: OBS_MEMB, self.pal.wing_far.name: OBS_FAR,
                self.pal.horn.name: OBS, FIRE.name: HOT}
        occ = cv.mat >= 0
        for i, mm in enumerate(list(cv.mats)):
            tgt = swap.get(mm.name)
            if tgt is None:
                continue
            sel = inside & (cv.mat == i)
            if sel.any():
                cv.mat[sel] = cv._mid(tgt)
        # plates: Voronoi cells, the joints between them glow
        pts = np.stack([lx[occ], ly[occ]], 1)
        dd = np.hypot(pts[:, None, 0] - PLATES[None, :, 0], pts[:, None, 1] - PLATES[None, :, 1])
        order = np.argsort(dd, axis=1)[:, :2]
        r = np.arange(len(pts))
        gap = dd[r, order[:, 1]] - dd[r, order[:, 0]]
        pg = _hash(np.minimum(order[:, 0], order[:, 1]), np.maximum(order[:, 0], order[:, 1]), 3)
        seam = np.zeros(occ.shape, bool)
        seam[occ] = (gap < 0.9) & (pg > 0.55)
        dark = np.zeros(occ.shape, bool)
        dark[occ] = (gap < 0.9) & (pg <= 0.55)
        gloss = np.zeros(occ.shape, bool)
        gloss[occ] = (gap >= 0.9) & (gap < 1.8) & (pts[:, 1] - PLATES[order[:, 0], 1] < -1.0)
        glass = np.zeros(occ.shape, bool)
        for nm in (OBS.name, OBS_FAR.name, OBS_MEMB.name):
            for i, mm in enumerate(cv.mats):
                if mm.name == nm:
                    glass |= cv.mat == i
        cv.part(line=False)
        cv.put(inside & glass & gloss, np.clip(cv.inten + 0.28, 0, 1), OBS, decal=True)
        cv.inten[inside & glass & dark] = 0.05
        cv.put(inside & glass & seam, np.clip(0.3 + 0.45 * p.seam, 0, 0.84), HOT, decal=True)
        # the front itself: grey scales cracking, a hot rim
        if p.obs < 0.999:
            rim = occ & ~inside & (d < front + wav + 3.5)
            cracks = rim & (np.abs(((lx * 0.9 - ly * 0.5) % 5.0) - 2.5) < 0.55)
            cv.put(cracks, 0.55 + 0.4 * p.seam, HOT, decal=True)
