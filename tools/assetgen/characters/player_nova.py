"""Nova v2 abilities (CONTENT_CONTRACT section 4): rows 19-25 of the player
sheet.  Built on the same puppet (`nova_rig`) as rows 0-18; the energy parts
(glider wings, charge orb, grapple line, dive trail, shockwave) are drawn on
separate layers in the cyan energy style of the existing powers (blade, wave)
and composited behind / in front of the body."""
import math

import numpy as np

import effects as FX
from common.palette import CYAN, hex_rgba
from nova_rig import Pose, Xf, draw
from raster import Canvas, Mat, put

TAU = 2 * math.pi
GROUND = 30.0
DTEAL = hex_rgba('#0f3b3a')
WHITE = hex_rgba('#ffffff')

# energy materials (cyan ramp, dark-teal rim like the player_wave projectile)
ENERGY = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=DTEAL, name="nv_energy",
             thresholds=[0.85, 0.6, 0.35])
ENERGY_NO = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=None, name="nv_energy_no",
                thresholds=[0.85, 0.6, 0.35])
MEMBRANE = Mat([CYAN[2], CYAN[1], CYAN[0], hex_rgba('#178f8a')], emissive=True, outline=DTEAL, name="nv_membrane",
               thresholds=[0.8, 0.55, 0.3])


def body_pt(p, dx, du, b=True):
    """World position of a point given in Nova's body frame (same maths as
    nova_rig.draw: dx along the torso's right, du along its up)."""
    lean = p.lean
    up = (math.sin(lean), -math.cos(lean))
    rt = (math.cos(lean), math.sin(lean))
    bb = p.bob if b else 0.0
    q = (p.hip[0] + rt[0] * dx + up[0] * du, p.hip[1] + rt[1] * dx + up[1] * du + bb)
    return Xf(p)(q)


def world(p, q):
    return Xf(p)(q)


def compose(body, back=None, front=None):
    """back layer only shows where the body is empty, front layer on top."""
    out = body.copy()
    if back is not None:
        FX.under(out, back)
    if front is not None:
        FX.overlay(out, front)
    return out


# --------------------------------------------------------------------- glide
def _wing(cv, root, a0, spread, lens, mem_inten, flutter, ph):
    """Fan wing (bat-like membrane on energy ribs) from `root`.
    a0 = angle of the leading spar, the ribs fan out by `spread` towards
    +angle; lens = rib lengths (leading spar first)."""
    n = len(lens)
    tips = []
    for k, ln in enumerate(lens):
        a = a0 + spread * k / (n - 1)
        # trailing ribs flutter more than the leading spar
        a += flutter * math.sin(ph - 0.9 * k) * (k / (n - 1))
        tips.append((root[0] + math.cos(a) * ln, root[1] + math.sin(a) * ln))
    # membrane outline: root -> tips with concave scallops between rib ends
    pts = [root]
    for k in range(n):
        pts.append(tips[k])
        if k < n - 1:
            mx = (tips[k][0] + tips[k + 1][0]) / 2
            my = (tips[k][1] + tips[k + 1][1]) / 2
            pts.append((mx + (root[0] - mx) * 0.16, my + (root[1] - my) * 0.16))
    cv.part()
    cv.poly(pts, MEMBRANE, flat=mem_inten)
    # ribs (bright energy filaments), leading spar brightest
    cv.part(line=False)
    for k, t in enumerate(tips):
        cv.line(root, t, ENERGY, inten=0.95 if k == 0 else 0.72, decal=True)
    return tips


def glide_wings(p, ph, lift=0.0):
    cv = Canvas(32, 32)
    root = body_pt(p, -4.4, 5.4)
    # far wing: raised steeply behind the helmet (darker), near wing: swept
    # back (brighter); a gap between them keeps the V readable
    _wing(cv, (root[0] + 0.8, root[1] - 0.8), -1.9 - lift * 0.10, -0.55, [16.0, 14.0, 11.5, 8.0], 0.3,
          0.10, ph)
    _wing(cv, root, -2.7 - lift * 0.08, -0.6, [15.5, 13.5, 11.0, 7.5], 0.52, 0.12, ph + 1.0)
    return cv.render()


def glide():
    out = []
    bob = [0, 0, 1, 1]
    lift = [0.0, 0.6, 1.0, 0.4]
    for i in range(4):
        b = bob[i]
        p = Pose(hip=(16, 25 + b), lean=0.18, foot_f=(18, 29 + b), foot_b=(14, 29.5 + b), toe_f=0.8, toe_b=1.0,
                 hand_f=(23, 21 + b), hand_b=(9, 22 + b), elbow_b=1,
                 scarf_ang=2.95, scarf_curl=0.03, scarf_amp=0.3, scarf_phase=TAU * i / 4, scarf_len=7)
        body = draw(p)
        out.append(compose(body, back=glide_wings(p, TAU * i / 4, lift[i])))
    return out


# --------------------------------------------------------------- ground pound
TRAIL = [  # (column offset from the body centre, length, phase shift)
    (-7, 5, 1), (-5, 9, 0), (-3, 12, 2), (-1, 15, 1), (1, 14, 3), (3, 12, 0), (5, 8, 2), (7, 5, 3)]


def _trail(img, ph):
    """Energy wake above the diving body: thin vertical speed streaks that
    end just above the silhouette (bright head next to the body, fading tail
    upwards, scrolling gaps), drawn behind the body."""
    layer = FX.new(32, 32)
    cols = [CYAN[3], CYAN[2], CYAN[1], CYAN[0]]
    alpha = img[:, :, 3] > 0
    xs = np.nonzero(alpha.any(axis=0))[0]
    cx = int(round((xs.min() + xs.max()) / 2))
    for (dx, ln, sh) in TRAIL:
        x = cx + dx
        col = np.nonzero(alpha[:, x])[0]
        base = int(col.min()) if len(col) else 22
        if abs(dx) >= 5:
            base -= 2 + ((sh + ph) % 2) * 2
        ln -= (sh + 2 * ph) % 3
        for y in range(max(0, base - ln), base):
            t = (base - y) / max(1, ln)  # 0 next to the body .. 1 at the tail end
            ci = 0 if t < 0.2 else (1 if t < 0.45 else (2 if t < 0.75 else 3))
            if abs(dx) >= 5:
                ci = min(3, ci + 1)
            if (y + 5 * ph + sh * 3) % 7 == 0 and t > 0.3:
                continue
            put(layer, x, y, cols[ci])
    return FX.under(img, layer)


def pound_dive():
    out = []
    for i in range(2):
        p = Pose(hip=(15, 24), lean=0.1, tilt=0.5, foot_f=(13.5, 26.5), foot_b=(11.5, 26), toe_f=1.9, toe_b=2.0,
                 hand_f=(19, 29.5), hand_b=(12, 21), elbow_f=1, fist=True, hand_glow=1.6, offset=(0, 1),
                 scarf_ang=-1.95 + 0.12 * i, scarf_curl=0.0, scarf_amp=0.35, scarf_phase=math.pi * i,
                 scarf_len=7)
        body = draw(p)
        out.append(_trail(body, i))
    return out


def _shock(img, cx, reach, fade):
    """Ground shockwave: two hollow energy crescents running left/right along
    the floor from `cx` (steep bright front, low tail); fade > 0.5 = dissolving.
    Drawn behind the body."""
    layer = FX.new(32, 32)
    for s in (-1, 1):
        for d in range(1, int(reach) + 1):
            x = int(round(cx + s * d))
            if not 0 <= x < 32:
                continue
            u = d / reach                    # 0 at the impact .. 1 at the front
            h = (1.0 + 3.2 * u ** 2) * (1 - fade * 0.6)
            if fade > 0.5 and u < 0.55:
                continue
            hh = int(round(h))
            for j in range(hh):
                y = 31 - j
                top = j == hh - 1
                # hollow crescent: rim (top 2 px + the front) and the floor line
                if 0 < j < hh - 2 and u < 0.85:
                    continue
                if u > 0.85:
                    c = CYAN[3] if top else CYAN[2]
                elif top:
                    c = CYAN[2]
                else:
                    c = CYAN[1] if u > 0.45 else CYAN[0]
                if fade > 0.5 and (x + j) % 2 == 1 and not top:
                    continue
                put(layer, x, y, c)
    FX.under(img, layer)
    return img


def _sparks(img, pts):
    """Energy sparks thrown up by the impact (x, y, size): planet-neutral,
    unlike stone debris."""
    for (x, y, s) in pts:
        if s > 1:
            FX.sparkle(img, x, y, 1, CYAN[3], CYAN[1])
        elif img[y, x, 3] == 0:
            img[y, x] = CYAN[2]
    return img


def pound_land():
    ps = [
        Pose(hip=(16, 28.5), lean=0.4, tilt=0.3, foot_f=(20, 30), foot_b=(12, 30), knee_b=1,
             hand_f=(21, 30), hand_b=(11, 26), fist=True, hand_glow=1.4, squash=0.9,
             scarf_ang=-1.9, scarf_curl=0.25, scarf_amp=0.3, scarf_phase=0.0),
        Pose(hip=(16, 28), lean=0.32, tilt=0.2, foot_f=(20, 30), foot_b=(12, 30),
             hand_f=(21, 29.5), hand_b=(11, 26.5), fist=True, squash=0.94,
             scarf_ang=-2.4, scarf_curl=0.25, scarf_phase=1.0),
        Pose(hip=(16, 27), lean=0.15, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 26), hand_b=(12, 26),
             scarf_ang=2.5, scarf_curl=0.15, scarf_phase=2.0),
        Pose(hip=(16, 26.5), lean=0.05, foot_f=(18, 30), foot_b=(14, 30), hand_f=(19, 25.5), hand_b=(13, 25.5),
             scarf_ang=2.75, scarf_curl=0.12, scarf_phase=3.0),
    ]
    out = [draw(p) for p in ps]
    _shock(out[0], 16.0, 12, 0.0)
    _sparks(out[0], [(4, 25, 2), (28, 24, 2), (8, 22, 1), (25, 21, 1)])
    _shock(out[1], 16.0, 16, 0.7)
    _sparks(out[1], [(2, 21, 1), (29, 19, 1), (6, 17, 1)])
    # impact flash around the fist
    hx, hy = world(ps[0], ps[0].hand_f)
    fl = FX.new(32, 32)
    FX.sparkle(fl, int(hx) + 2, int(hy) - 2, 2, WHITE, CYAN[2])
    FX.overlay(out[0], fl)
    # a few settling sparks
    for (x, y, c) in ((3, 28, CYAN[1]), (29, 27, CYAN[0]), (6, 25, CYAN[0])):
        if out[2][y, x, 3] == 0:
            out[2][y, x] = c
    return out


# --------------------------------------------------------------- charge/shoot
def _orb_layer(c, r, ph, sparks=True, spark_r=7.0):
    cv = Canvas(32, 32)
    cv.part()
    cv.disc(c, r, ENERGY, bias=0.15)
    cv.part(line=False)
    cv.disc((c[0] - r * 0.25, c[1] - r * 0.25), max(0.8, r * 0.45), ENERGY_NO, flat=1.0, decal=True)
    img = cv.render()
    if sparks:
        for k in range(4):
            a = ph + TAU * k / 4 + 0.4
            d = spark_r
            x, y = c[0] + math.cos(a) * d, c[1] + math.sin(a) * d * 0.9
            put(img, int(x), int(y), CYAN[2] if k % 2 else CYAN[3])
            x2, y2 = c[0] + math.cos(a) * (d + 1.2), c[1] + math.sin(a) * (d + 1.2) * 0.9
            put(img, int(x2), int(y2), CYAN[0])
    return img


def charge():
    out = []
    rad = [2.3, 2.7, 3.1, 3.4]      # the orb swells while the sparks converge
    sp = [8.0, 6.6, 5.4, 9.0]
    for i in range(4):
        b = [0, 0, 1, 1][i]
        p = Pose(hip=(15.5, 26.5 + b * 0.5), lean=-0.06, foot_f=(20, 30), foot_b=(12, 30), knee_b=1,
                 hand_f=(22, 23), hand_b=(20, 24.5), elbow_b=-1,
                 scarf_ang=3.0, scarf_curl=0.02, scarf_amp=0.35, scarf_phase=TAU * i / 4, scarf_len=6)
        body = draw(p)
        hx, hy = world(p, p.hand_f)
        orb = _orb_layer((hx + 3.0, hy - 0.5), rad[i], TAU * i / 16, spark_r=sp[i])
        out.append(compose(body, front=orb))
    return out


def shoot():
    ps = [
        Pose(hip=(16.5, 26), lean=0.18, foot_f=(21, 30), foot_b=(12, 30), knee_b=1, hand_f=(24, 22),
             hand_b=(12, 24.5), scarf_ang=3.05, scarf_curl=0.02, scarf_phase=0.0, scarf_len=7),
        Pose(hip=(15.5, 26), lean=-0.2, tilt=-0.2, foot_f=(20, 30), foot_b=(12, 30), knee_b=1, hand_f=(23, 18.5),
             hand_b=(11, 23), scarf_ang=0.6, scarf_curl=0.2, scarf_amp=0.3, scarf_phase=1.5),
        Pose(hip=(16, 26.5), lean=0.0, foot_f=(19, 30), foot_b=(13, 30), hand_f=(20, 23), hand_b=(12.5, 25),
             scarf_ang=2.4, scarf_curl=0.15, scarf_phase=3.0),
    ]
    out = [draw(p) for p in ps]
    # muzzle burst on the release frame
    hx, hy = world(ps[0], ps[0].hand_f)
    burst = FX.new(32, 32)
    FX.burst(burst, hx + 4.0, hy, 3.4, [CYAN[3], CYAN[2], CYAN[1]], rays=0, seed=5)
    FX.sparkle(burst, int(hx + 4), int(hy), 4, WHITE, CYAN[2])
    FX.overlay(out[0], burst)
    # recoil: fading ring of sparks
    sm = FX.new(32, 32)
    for (dx, dy) in ((3, -2), (5, 0), (4, 2), (6, -3)):
        put(sm, 23 + dx, 19 + dy, CYAN[1])
    FX.under(out[1], sm)
    return out


# ------------------------------------------------------------------ grapple
def _line(img, p0, p1, hook=True, wob=0.0, ph=0):
    """Energy line from p0 to p1 (2px core + glow), drawn behind the body."""
    layer = FX.new(32, 32)
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    ln = math.hypot(dx, dy)
    n = max(2, int(ln * 2))
    nx, ny = -dy / max(ln, 1e-3), dx / max(ln, 1e-3)
    for k in range(n + 1):
        t = k / n
        w = wob * math.sin(t * math.pi * 2 + ph) * math.sin(t * math.pi)
        x = p0[0] + dx * t + nx * w
        y = p0[1] + dy * t + ny * w
        put(layer, int(x), int(y), CYAN[3] if (k + ph) % 6 else CYAN[2])
        # soft glow on one side
        if layer[int(y) + 1 if int(y) + 1 < 32 else int(y), int(x), 3] == 0:
            put(layer, int(x), int(y) + 1, CYAN[0])
    if hook:
        hx, hy = p1
        FX.sparkle(layer, int(hx), int(hy), 1, WHITE, CYAN[2])
    FX.under(img, layer)
    return img


def grapple():
    ps = [
        Pose(hip=(15.5, 26.5), lean=-0.1, tilt=-0.3, foot_f=(19, 30), foot_b=(12, 30), hand_f=(13, 26),
             hand_b=(11, 25), elbow_f=1, hand_glow=1.6,
             scarf_ang=2.8, scarf_phase=0.0),
        Pose(hip=(16.5, 26), lean=0.12, tilt=-0.35, foot_f=(20, 30), foot_b=(13, 30), hand_f=(23, 16),
             hand_b=(12, 24), scarf_ang=3.0, scarf_curl=0.05, scarf_phase=1.2, scarf_len=7),
        Pose(hip=(16.5, 26), lean=0.1, tilt=-0.35, foot_f=(20, 30), foot_b=(13, 30), hand_f=(23, 16),
             hand_b=(12, 24), scarf_ang=2.95, scarf_curl=0.05, scarf_phase=2.4, scarf_len=7),
        Pose(hip=(16, 26), lean=0.05, tilt=-0.3, foot_f=(20, 30), foot_b=(13, 30), hand_f=(22, 17),
             hand_b=(13, 24), fist=True, scarf_ang=2.9, scarf_curl=0.05, scarf_phase=3.6),
    ]
    out = [draw(p) for p in ps]
    h1 = world(ps[1], ps[1].hand_f)
    _line(out[1], (h1[0] + 1, h1[1] - 1), (h1[0] + 5, h1[1] - 5), wob=0.6)
    h2 = world(ps[2], ps[2].hand_f)
    _line(out[2], (h2[0] + 1, h2[1] - 1), (31.5, h2[1] - 9.5), hook=False, wob=0.8, ph=2)
    h3 = world(ps[3], ps[3].hand_f)
    _line(out[3], (h3[0] + 1, h3[1] - 1), (31.5, h3[1] - 10.5), hook=False)
    return out


def grapple_pull():
    out = []
    for i in range(2):
        s = 0.5 * i
        p = Pose(hip=(14, 25), lean=0.4 + 0.04 * i, tilt=-0.2, foot_f=(12 - s, 30), foot_b=(9 - s, 28.5 + s),
                 toe_f=1.3, toe_b=1.5, hand_f=(23.5 + s, 17.5 - s), hand_b=(22.5 + s, 19.5 - s), fist=True,
                 scarf_ang=2.6, scarf_curl=0.0, scarf_amp=0.35, scarf_phase=math.pi * i, scarf_len=7)
        body = draw(p)
        hx, hy = world(p, p.hand_f)
        _line(body, (hx + 1, hy - 1), (31.5, max(0.5, hy - 9.0)), hook=False, ph=i * 3)
        # speed streaks trailing behind (lower left)
        rng = np.random.default_rng(60 + i)
        for k in range(3):
            y = int(rng.integers(18, 30))
            x0 = int(rng.integers(0, 4))
            for j in range(int(rng.integers(3, 6))):
                x, yy = x0 + j, y - j // 2
                if body[yy, x, 3] == 0:
                    body[yy, x] = CYAN[1] if j > 1 else CYAN[0]
        out.append(body)
    return out


CLIPS = [
    ("glide", glide, dict(fps=10, loop=True)),
    ("pound_dive", pound_dive, dict(fps=14, loop=True)),
    ("pound_land", pound_land, dict(durations=[0.08, 0.08, 0.08, 0.1], loop=False, next="idle",
                                     events=[(0, "impact")])),
    ("charge", charge, dict(fps=12, loop=True)),
    ("shoot", shoot, dict(durations=[0.06, 0.08, 0.12], loop=False, next="idle", events=[(0, "fire")])),
    ("grapple", grapple, dict(durations=[0.06, 0.06, 0.06, 0.1], loop=False, events=[(1, "fire")])),
    ("grapple_pull", grapple_pull, dict(fps=12, loop=True)),
]


def add_rows(sheet):
    for name, fn, kw in CLIPS:
        sheet.add(name, fn(), **kw)
    return sheet
