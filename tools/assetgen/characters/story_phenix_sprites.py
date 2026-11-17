"""The sprites of the Phénix's story (« Le Porteur d'aube », 2.63): every actor of its six animated
shots, drawn frame by frame like the game's sprites (sheets under assets/stories/phenix/, clips in
assets/animations/stories/phenix_<name>.json; data/stories/phenix.json places them).

  oiseau   the little bird of light: flies, glides, perches, looks up, waits in the dark,
           sets its feathers alight, burns, falls into ashes                         64x64
  grand    the bird reborn, bigger and brighter: rises from the flames, hovers       112x96
  cendres  its ashes: embers breathing, stirring, flames bursting out of them        64x32
  nid      the Phénix's nest on its peak (back and front rims, embers)               176x64
  rocher   the crag of black glass where it waits                                    96x64
  fumee    a puff of smoke                                                           96x48
  etoile   the star of the centre: shining, collapsing into a black hole             96x96
  astre    a twinkling star                                                          16x16

The grown Phénix is the game's own sheet (animations/phenix.json).  One sheet pixel is two units
of the story's stage.  Deterministic."""
import math

import numpy as np

from boss_a_common import mat
from raster import Canvas, TCanvas
from story_monolithe_sprites import C, putpx, twinkle_clips  # noqa: F401
from story_pyrrhax_sprites import star_clips  # noqa: F401  (the same star of the centre)

PI = math.pi
TAU = 2 * PI

GOLD = mat("ph_gold", ["#fffef0", "#fff2a0", "#ffd04a", "#f0a030", "#c86a20"], emissive=True, outline=C("#7a2a10"),
           th=[0.86, 0.66, 0.44, 0.24])
GOLD_FAR = mat("ph_gold_far", ["#ffe080", "#f0a840", "#d07028", "#a04a1a"], emissive=True, outline=C("#5a1a0c"), th=[0.8, 0.55, 0.3])
PINK = mat("ph_pink", ["#fff0f6", "#ffbede", "#ff9ac6", "#e06a9a"], emissive=True, outline=C("#7a2a40"), th=[0.8, 0.55, 0.3])
DIM = mat("ph_dim", ["#d8d0c8", "#a8a0a8", "#807888", "#5a5468"], outline=C("#1e1a28"), th=[0.82, 0.56, 0.3])
DIM_FAR = mat("ph_dim_far", ["#8a8494", "#686274", "#4a4658"], outline=C("#141220"), th=[0.66, 0.36])
BEAK = mat("ph_beak", ["#fff6d8", "#e8b860", "#a87028"], th=[0.7, 0.4])
EYE = mat("ph_eye", ["#3a1020"], outline=None)
FIRE = [mat(f"ph_f{i}", [c], emissive=True, outline=None) for i, c in enumerate(["#ffffff", "#fff2a0", "#ffc040", "#ff7a2a", "#d83a1a"])]


class BPose:
    def __init__(self, **kw):
        self.wing = 0.0     # -1: up, +1: down
        self.tail = 0.0     # sway
        self.tilt = 0.0     # body rotation (radians, clockwise)
        self.head = 0.0     # negative: looks up
        self.lit = 1.0      # 1: a bird of light, 0: dim, grey (the long night)
        self.fire = 0.0     # flames around it (0..1)
        self.perch = False
        self.fold = 0.0     # wings folded against the body (perched)
        self.k = 0          # flicker
        self.sink = 0.0     # collapsing into ashes (0..1)
        for k, v in kw.items():
            setattr(self, k, v)


def bird(W, H, cx, cy, s, p):
    """The bird of light facing right, its body's centre at (cx, cy), size s."""
    cv = TCanvas(W, H, rot=p.tilt, center=(cx, cy), scale=(s, s), offset=(0, 0))
    lit = p.lit > 0.5
    body = GOLD if lit else DIM
    far = GOLD_FAR if lit else DIM_FAR
    tail_m = PINK if lit else DIM_FAR

    def X(dx, dy):
        return (cx + dx, cy + dy)

    sh = X(-1.0, -2.6)
    # wings: angle of the tip from the shoulder (back and up when -1, down when +1)
    a = -0.35 + 1.15 * p.wing
    span = 15.0 * (1 - 0.55 * p.fold)

    def wing(m, off, k):
        base = (sh[0] + off[0], sh[1] + off[1])
        tip = (base[0] - math.cos(a) * span * k, base[1] + math.sin(a) * span * 0.9 * k)
        elbow = (base[0] - math.cos(a - 0.5) * span * 0.45 * k, base[1] + math.sin(a - 0.5) * span * 0.45 * k)
        trail = (tip[0] + 4.0 * k, tip[1] + 2.5 + 2.0 * p.fold)
        cv.part()
        cv.tpoly([base, elbow, tip, trail, (base[0] - 3.0, base[1] + 3.6)], m, bevel=1.2)
        for j in range(3):  # long feathers at its edge
            q = (tip[0] + j * 2.2 * k, tip[1] + 1.2 + j * 1.2)
            cv.tcapsule(q, (q[0] - 3.0, q[1] + 1.6 + 0.6 * j), 1.0, 0.4, m)

    wing(far, (2.0, -1.0), 0.86)
    # tail: three long feathers, curling up at their end
    for j in range(3):
        ang = PI + 0.15 + 0.22 * j + 0.18 * math.sin(p.tail + j)
        pts = [X(-5.0, 1.0)]
        x, y = pts[0]
        for q in range(6):
            ang2 = ang - 0.12 * q * (1 if j != 1 else 0.6)
            x += math.cos(ang2) * 3.2
            y += math.sin(ang2) * 3.2 * 0.6 - 0.25 * q
            pts.append((x, y))
        cv.part()
        cv.tchain(pts, [1.8 - 0.22 * q for q in range(7)], tail_m)
    cv.part()
    cv.tellipse(X(0, 0), 7.2, 5.0 - 1.6 * p.sink, body, ang=-0.25)
    cv.part()
    cv.tdisc(X(6.8, -4.6 + 2.0 * p.head + 3.0 * p.sink), 3.6, body)
    hx, hy = cv.T(X(6.8, -4.6 + 2.0 * p.head + 3.0 * p.sink))
    cv.part()
    bk = [(9.2, -5.4), (13.6, -3.6 + 1.6 * p.head), (9.4, -3.0)]
    cv.tpoly([X(dx, dy + 2.0 * p.head + 3.0 * p.sink) for dx, dy in bk], BEAK, bevel=0.6)
    cv.part()  # the crest
    for j in range(3):
        b0 = X(5.4 - j * 1.2, -7.4 + 2.0 * p.head + 3.0 * p.sink)
        cv.tcapsule(b0, (b0[0] - 3.0 - j, b0[1] - 2.2 + j * 0.6), 0.9, 0.4, tail_m)
    if p.perch:
        cv.part()
        for dx in (-1.0, 1.6):
            cv.tline(X(dx, 4.0), X(dx + 0.6, 8.5), BEAK)
            cv.tline(X(dx + 0.6, 8.5), X(dx + 2.4, 8.5), BEAK)
    wing(body, (0.0, 0.0), 1.0)
    cv.part(line=False)
    ex, ey = cv.T(X(7.8, -5.4 + 2.0 * p.head + 3.0 * p.sink))
    cv.px_set(ex, ey, EYE, 0.5)
    f = cv.render()
    if p.fire > 0:
        _fire(f, cx, cy, s, p.fire, p.k)
    if lit and p.fire <= 0:  # a few sparkles falling off it
        rng = np.random.default_rng(900 + p.k)
        for _ in range(4):
            putpx(f, cx - rng.uniform(4, 18) * s, cy + rng.uniform(-6, 8) * s, C("#fff2a0" if rng.random() < 0.5 else "#ffd04a"), True)
    del hx, hy
    return f


def _fire(f, cx, cy, s, amt, k):
    """Flames licking up around the bird (drawn over it, mostly where it is not)."""
    rng = np.random.default_rng(950 + k)
    cols = [C("#fff2a0"), C("#ffc040"), C("#ff7a2a"), C("#d83a1a")]
    n = int(5 + 9 * amt)
    for j in range(n):
        x0 = cx + rng.uniform(-11, 11) * s
        base = cy + rng.uniform(2, 7) * s
        ht = (6 + 14 * amt * rng.uniform(0.6, 1.0)) * s
        wd = (1.5 + 2.2 * amt) * s
        sway = rng.uniform(-2, 2) * s
        for yy in range(int(ht)):
            t = yy / max(1.0, ht)
            half = wd * (1 - t)
            xc = x0 + sway * t * t
            for xx in range(int(xc - half), int(xc + half) + 1):
                d = abs(xx + 0.5 - xc) / max(0.5, half)
                ci = min(3, int(d * 1.6 + t * 2.4))
                over = f[int(base - yy), xx, 3] == 0 if 0 <= int(base - yy) < f.shape[0] and 0 <= xx < f.shape[1] else False
                if over or ci <= 1 or (xx + yy + k) % 3 == 0:
                    putpx(f, xx, base - yy, cols[ci])


def bird_clips():
    S = 1.0
    W = H = 64
    cx, cy = 32, 34
    clips = []
    n = 8
    clips.append(("fly", [bird(W, H, cx, cy, S, BPose(wing=math.sin(TAU * i / n), tail=TAU * i / n, k=i, tilt=-0.08))
                          for i in range(n)], dict(fps=12)))
    clips.append(("glide", [bird(W, H, cx, cy, S, BPose(wing=-0.25 + 0.12 * math.sin(TAU * i / 6), tail=TAU * i / 6, k=i, tilt=0.06))
                            for i in range(6)], dict(fps=8)))
    clips.append(("perch", [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0, wing=0.6, tail=0.6 * math.sin(TAU * i / 6), k=i,
                                                        head=0.15 * math.sin(TAU * i / 6))) for i in range(6)], dict(fps=5)))
    clips.append(("look_up", [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0, wing=0.6, head=-0.45 * i / 3, k=i)) for i in range(4)],
                  dict(durations=[0.1, 0.1, 0.12, 0.2], loop=False, next="gaze")))
    clips.append(("gaze", [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0, wing=0.6, head=-0.45 + 0.05 * math.sin(TAU * i / 6),
                                                       tail=0.4 * math.sin(TAU * i / 6), k=i)) for i in range(6)], dict(fps=5)))
    # wait: the light gone out of it, grey in the dark, head low
    clips.append(("dim", [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0, wing=0.7, lit=0.0, head=0.35 + 0.05 * math.sin(TAU * i / 6),
                                                      tail=0.3 * math.sin(TAU * i / 6), k=i)) for i in range(6)], dict(fps=4)))
    clips.append(("fade", [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0, wing=0.6, lit=1.0 if i in (0, 2) else 0.0, head=-0.3 + 0.2 * i,
                                                       k=i)) for i in range(6)], dict(durations=[0.3, 0.12, 0.2, 0.3, 0.3, 0.4],
                                                                                    loop=False, next="dim")))
    # ignite: it spreads its wings and sets its feathers alight
    fr = [bird(W, H, cx, cy, S, BPose(perch=True, fold=1.0 - i / 5, wing=-0.6 * i / 5, head=-0.2, fire=0.2 * i, k=i)) for i in range(6)]
    clips.append(("ignite", fr, dict(durations=[0.12, 0.1, 0.1, 0.12, 0.12, 0.14], loop=False, next="burn")))
    clips.append(("burn", [bird(W, H, cx, cy, S, BPose(perch=True, fold=0.0, wing=-0.6 + 0.3 * math.sin(TAU * i / 6), head=-0.25,
                                                       fire=1.0, k=i + 10)) for i in range(6)], dict(fps=10)))
    # collapse: it sinks into its own fire (then the ash pile, its own sprite, is left)
    fr = [bird(W, H, cx, cy + 3 * i, S, BPose(perch=True, fold=0.4 * i / 5, wing=0.4 * i / 5, head=0.3 * i / 5, lit=1.0 if i < 3 else 0.0,
                                               fire=1.0 - 0.12 * i, sink=i / 5, k=i + 20)) for i in range(6)]
    clips.append(("collapse", fr, dict(durations=[0.12, 0.12, 0.14, 0.14, 0.16, 0.2], loop=False)))
    return clips


def grand_clips():
    S = 1.8
    W, H = 112, 96
    cx, cy = 56, 52
    clips = []
    fr = []
    for i in range(8):  # rising from the flames: a pillar of fire, the bird taking shape, wings opening
        u = i / 7
        f = bird(W, H, cx, cy + 10 * (1 - u), S, BPose(fold=max(0.0, 1 - u * 1.4), wing=0.6 - 1.6 * u, head=-0.2, fire=1.0 - 0.8 * u,
                                                       k=i + 30, tail=u * 3))
        if i < 3:  # mostly flames at first
            f[(f[:, :, 3] > 0) & ((np.indices(f.shape[:2]).sum(0) + i) % 2 == 0)] = C("#ffc040")
        fr.append(f)
    clips.append(("rise", fr, dict(durations=[0.12, 0.12, 0.12, 0.12, 0.12, 0.14, 0.14, 0.2], loop=False, next="hover")))
    n = 8
    clips.append(("hover", [bird(W, H, cx, cy, S, BPose(wing=math.sin(TAU * i / n), tail=TAU * i / n, k=i + 40)) for i in range(n)],
                  dict(fps=10)))
    return clips


# ======================================================================= ashes, nest, crag, smoke
ASH = mat("as_ash", ["#b0a8a8", "#8a8282", "#646060", "#424040"], th=[0.8, 0.55, 0.3])
EMB = [C("#fff2a0"), C("#ffc040"), C("#ff7a2a"), C("#a03a1a")]


def _ashes(i, glow, fire=0.0):
    cv = Canvas(64, 32)
    cv.part()
    cv.ellipse((32, 30), 16, 9, ASH, within=lambda x, y: y < 31)
    cv.part(line=False)
    cv.ellipse((26, 27), 6, 3, ASH, bias=0.15, decal=True)
    f = cv.render()
    rng = np.random.default_rng(400)
    for j in range(18):
        x, y = rng.uniform(19, 45), rng.uniform(23, 30)
        ph = rng.uniform(0, TAU)
        b = glow * (0.5 + 0.5 * math.sin(TAU * i / 6 + ph))
        if b > 0.25 and f[int(y), int(x), 3]:
            putpx(f, x, y, EMB[3 - min(3, int(b * 3.5))])
    if fire > 0:
        _fire(f, 32, 26, 1.0, fire, i + 60)
    return f


def ashes_clips():
    return [("idle", [_ashes(i, 0.6) for i in range(6)], dict(fps=5)),
            ("stir", [_ashes(i, 1.0 + 0.1 * i) for i in range(6)], dict(fps=8)),
            ("burst", [_ashes(i, 1.2, fire=0.3 + 0.12 * i) for i in range(6)], dict(fps=10))]


GLASS = mat("gl_glass", ["#8a7aa8", "#4e4470", "#2e284a", "#1c1830", "#100c1c"], th=[0.86, 0.64, 0.42, 0.22])
TWIG = mat("ns_twig", ["#c8a06a", "#9a7444", "#6a4c2c", "#3e2c1a"], th=[0.8, 0.55, 0.3])


def _nest(front, i):
    cv = Canvas(176, 64)
    rng = np.random.default_rng(410 + (1 if front else 0))
    for j in range(16):  # twigs and shards of glass woven in a ring
        a = rng.uniform(0, PI) if front else rng.uniform(PI, TAU)
        x0, y0 = 88 + math.cos(a) * 64, 40 + math.sin(a) * 12
        ang = rng.uniform(-0.5, 0.5)
        cv.part()
        m = TWIG if j % 3 else GLASS
        cv.capsule((x0 - math.cos(ang) * 16, y0 - math.sin(ang) * 4), (x0 + math.cos(ang) * 16, y0 + math.sin(ang) * 4), 3.0, 2.0, m)
    f = cv.render()
    rng = np.random.default_rng(420)
    for j in range(16):
        a = rng.uniform(0, TAU)
        x, y = 88 + math.cos(a) * rng.uniform(40, 66), 40 + math.sin(a) * 10
        if (math.sin(a) > 0) == front and f[int(y), int(x), 3]:
            b = 0.5 + 0.5 * math.sin(TAU * i / 6 + j)
            putpx(f, x, y, EMB[3 - min(3, int(b * 3.5))])
    return f


def nest_clips():
    return [("back", [_nest(False, i) for i in range(6)], dict(fps=6)), ("front", [_nest(True, i) for i in range(6)], dict(fps=6))]


def crag_clips():
    fr = []
    for i in range(4):
        cv = Canvas(96, 64)
        rng = np.random.default_rng(430)
        for j in range(4):
            x = 20 + j * 18 + rng.uniform(-4, 4)
            ht = rng.uniform(26, 50)
            cv.part()
            cv.poly([(x - 16, 63), (x - 4, 63 - ht), (x + 6, 63 - ht * 0.8), (x + 16, 63)], GLASS, bevel=1.8)
        f = cv.render()
        for j in range(5):  # a glint sliding over the glass
            putpx(f, 18 + j * 15 + i * 3, 30 + (j * 7) % 18 - i, C("#e8e0ff"))
        fr.append(f)
    return [("idle", fr, dict(fps=3))]


SMOKE = mat("sm_ph", ["#c8bcc8", "#9a8e9e", "#706678", "#4e4658"], outline=None, th=[0.8, 0.55, 0.3])


def smoke_clips():
    fr = []
    for i in range(9):
        k = i / 8
        cv = Canvas(96, 48)
        rng = np.random.default_rng(440)
        for j in range(8):
            x = 48 + (j - 3.5) * 4.5 * (0.4 + 0.8 * k) + rng.uniform(-3, 3)
            r = (4 + rng.uniform(0, 4)) * (0.5 + 0.9 * k)
            y = 40 - r * 0.6 - k * 14 - j % 3 * 2
            cv.part(line=False)
            cv.disc((x, y), r, SMOKE)
        f = cv.render(outline=False)
        if k > 0.4:
            holes = np.random.default_rng(450 + i).random(f.shape[:2]) < (k - 0.4) * 1.5
            f[holes] = 0
        fr.append(f)
    return [("puff", fr, dict(durations=[0.06, 0.07, 0.08, 0.1, 0.12, 0.14, 0.16, 0.18, 0.22], loop=False))]


SHEETS = {
    "oiseau": (64, 64, [32, 42], bird_clips),
    "grand": (112, 96, [56, 70], grand_clips),
    "cendres": (64, 32, [32, 31], ashes_clips),
    "nid": (176, 64, [88, 52], nest_clips),
    "rocher": (96, 64, [48, 63], crag_clips),
    "fumee": (96, 48, [48, 44], smoke_clips),
    "etoile": (96, 96, [48, 48], star_clips),
    "astre": (16, 16, [8, 8], twinkle_clips),
}
