"""2.19 attack effects of the sanctuary guardians (dedicated sheets, each with its JSON):

  sprites/projectiles/root_spike.png    32x64  pivot [16, 64]  travel 4 (loop: the bump running under
                                        the ground), erupt 5 (hit f1), retract 4          (Mycelia)
  sprites/projectiles/thorn_wall.png    32x96  pivot [16, 96]  rise 5, idle 4 (loop), wither 5 (Mycelia)
  sprites/projectiles/void_well.png     64x64  pivot [32, 32]  open 5, idle 6 (loop), close 4 (Nyxara)
  sprites/projectiles/eclipse_ring.png  16x16  pivot [8, 8]    segment 4 (loop), edge 4 (loop) (Nyxara)
      The ring is drawn by the game as `segment` tiles laid along the circle (each tile rotated to the
      tangent, frame top = outside of the ring, row 8 = radius); `edge` closes the band at each side of
      the opening (band on the left half of the tile, cap facing +x; mirror it for the other side).
  sprites/projectiles/shadow_mark.png   32x32  pivot [16, 16]  appear 4, idle 4 (loop), strike 4 (Nyxara)
  sprites/objects/frost_floor.png       16x16  pivot [0, 4]    form 4, idle 6 (loop), melt 4    (Aurorax)
      Translucent ice veil laid over the floor, tiled horizontally every 16 px; row 4 of the tile is
      the floor surface (rows 0-3 are rime sticking out above it).
  sprites/objects/lava_flood.png        128x32 liquid format (as liquid_<kind>.png): row 0 = 8 surface
      frames 16x16 (crest at the top), row 1 = 8 body frames 16x16; JSON clips `surface`, `body`
      (pivot [0, 0]).                                                                    (Pyrogyre)
Deterministic."""
import math

import numpy as np

import effects as FX
from boss_a_common import C, glow_orb, mat, motes, ring, streak
from boss_c_common import dash_arc, over
from raster import TCanvas, put
from sheet import Sheet, save_json, save_png

PI = math.pi
TAU = 2 * PI

ROOT = mat("fx_root", ["#a87a5c", "#865c40", "#663f2c", "#48291e", "#2c1812"])
ROOT_DEAD = mat("fx_root_dead", ["#7a6e66", "#5c524c", "#403a36", "#26201e"])
VEIN = mat("fx_vein", ["#b4fff0", "#5df0d0", "#2fc0b0"], emissive=True, outline=None)
SOIL = mat("fx_soil", ["#6a5040", "#4e3a30", "#36282a", "#22181c"])
THORN = mat("fx_thorn", ["#f2ead8", "#d0c0a0", "#9a8670"])
BRAMBLE = mat("fx_bramble", ["#4a9a6a", "#2e7a54", "#1e5a42", "#143e32", "#0c2622"])
BRAMBLE_DK = mat("fx_bramble_dk", ["#2e7a54", "#1e5a42", "#143e32", "#0c2622"])
BRAMBLE_DEAD = mat("fx_bramble_dead", ["#8a7a60", "#6a5c48", "#4c4034", "#2e2620"])
BUD = mat("fx_bud", ["#f2fffb", "#b4fff0", "#7dffd6", "#39c8b0"], emissive=True)
VOID = mat("fx_void", ["#2a1240", "#160828", "#0a0414", "#040108"])
RIM = mat("fx_rim", ["#ffe0f0", "#ff9ab8", "#c08cff", "#8a4ee0", "#5b2a8c"], emissive=True, outline=None,
          th=[0.86, 0.68, 0.48, 0.28])

DIRT = [C("#8a6a54"), C("#6a5040"), C("#4e3a30")]
CYANS = [C("#f2fffb"), C("#b4fff0"), C("#7dffd6"), C("#39e0c0")]
PINKS = [C("#ffffff"), C("#ffd0e0"), C("#ff7696"), C("#d0305a")]
VIOLETS = [C("#f0c8ff"), C("#c08cff"), C("#8a4ee0"), C("#5b2a8c")]
SPORES = [C("#b4fff0"), C("#7dffd6"), C("#e0b0ff"), C("#c080f0")]


def blank(w, h):
    return np.zeros((h, w, 4), np.uint8)


def write_sheet(name, folder, fw, fh, pivot, clips, extra=None):
    s = Sheet(f"sprites/{folder}/{name}.png", fw, fh, pivot)
    for clip, frames, kw in clips:
        s.add(clip, frames, **kw)
    s.save(f"animations/{name}.json")
    return s


# ======================================================================= root_spike (32x64)
RW, RH = 32, 64
RG = 63


def _mound(f, cx, w, h, seed, glow=0.5, phase=0):
    cv = TCanvas(RW, RH)
    cv.part()
    cv.ellipse((cx, RG + 1.5), w, h, SOIL, bias=0.05)
    img = cv.render()
    img[RG + 1:] = 0
    over(f, img)
    rng = np.random.default_rng(seed)
    for _ in range(int(w * 0.8)):
        x = int(cx + (rng.random() - 0.5) * 1.5 * w)
        y = int(RG - rng.random() * (h - 1))
        if 0 <= x < RW and 0 <= y < RH and f[y, x, 3] and f[y, x, 0] != 11:
            f[y, x] = DIRT[int(rng.integers(0, 3))]
    # glowing crack across the top of the bump
    if glow > 0:
        top = int(RG - h + 2)
        for k, x in enumerate(range(int(cx - w * 0.6), int(cx + w * 0.6))):
            y = top + 1 + int(1.2 * math.sin(k * 0.9 + phase))
            if 0 <= x < RW and 0 <= y <= RG and f[y, x, 3]:
                f[y, x] = CYANS[1] if (k + phase) % 4 else CYANS[0] if glow > 0.7 else CYANS[2]
    return f


def _spike(f, height, seed, dead=False, sway=0.0):
    if height <= 1:
        return f
    cv = TCanvas(RW, RH)
    m = ROOT if not dead else ROOT_DEAD
    n = 8
    pts = []
    for i in range(n + 1):
        t = i / n
        pts.append((16 + 2.0 * math.sin(t * 3.0 + seed) * (1 - t) + sway * t * t, RG + 1 - height * t))
    radii = [5.2 - 4.6 * (i / n) ** 0.9 for i in range(n + 1)]
    cv.part()
    cv.chain(pts, radii, m)
    # a second braided root
    pts2 = [(x + 2.6 * math.sin(i * 1.1 + 1.0) * (1 - i / n), y + 1) for i, (x, y) in enumerate(pts[:-2])]
    cv.part()
    cv.chain(pts2, [r * 0.55 for r in radii[:-2]], m, bias=-0.08)
    # thorns
    cv.part()
    for i in (2, 4, 6):
        if pts[i][1] > RG:
            continue
        for sgn in (-1, 1):
            if (i + (sgn > 0)) % 2:
                continue
            bx, by = pts[i][0] + sgn * radii[i] * 0.8, pts[i][1]
            cv.poly([(bx, by - 1.2), (bx + sgn * 4.0, by - 3.5), (bx, by + 1.0)], THORN, bevel=0.6, base=0.6)
    if not dead:
        cv.part(line=False)
        for i in range(1, n - 1):
            cv.px_set(pts[i][0] - 0.6, pts[i][1], VEIN, inten=0.6 + 0.3 * (i % 2), decal=True)
    img = cv.render()
    img[RG + 1:] = 0
    over(f, img)
    return f


def root_spike():
    travel = []
    for i in range(4):
        f = blank(RW, RH)
        _mound(f, 16 + (i % 2) * 0.5, 11 + (i % 2), 6 + (i in (1, 2)), seed=500 + i, glow=0.6, phase=i * 2)
        # root tips poking out and clods hopping
        rng = np.random.default_rng(510 + i)
        for k in range(3):
            x = 7 + rng.random() * 18
            y = RG - 7 - ((i + k) % 4) * 2
            FX.chunk(f, x, y, 1 + (k % 2), DIRT[0], DIRT[2])
        put(f, 12 + i % 3, RG - 7, C("#866040"))
        motes(f, 16, RG - 6, 4, 6, 12, CYANS[1:], seed=520 + i, arc=(-PI, 0))
        travel.append(f)
    heights = [0, 26, 52, 54, 50]
    erupt = []
    for i, h in enumerate(heights):
        f = blank(RW, RH)
        _mound(f, 16, 13 - i * 1.5 if i < 3 else 8, 7 if i == 0 else 5, seed=530 + i, glow=1.0 if i == 0 else 0.4,
               phase=i)
        _spike(f, h, seed=1.3)
        if i == 0:
            for x in range(6, 27):
                if (x + 1) % 3 == 0:
                    for y in range(RG - 9, RG - 7):
                        pass
            FX.sparkle(f, 16, RG - 10, 3, CYANS[0], CYANS[2])
        if i in (1, 2):
            FX.burst(f, 8, RG - 2, 4 - i, DIRT, seed=540 + i, jag=0.5)
            FX.burst(f, 24, RG - 2, 4 - i, DIRT, seed=545 + i, jag=0.5)
            for (x, y, s) in ((4, RG - 14 - 6 * i, 2), (27, RG - 10 - 8 * i, 1), (6, RG - 24 - 4 * i, 1),
                              (25, RG - 20 - 6 * i, 2)):
                FX.chunk(f, x, y, s, DIRT[0], DIRT[2])
            motes(f, 16, RG - 30, 10, 6, 26, CYANS[1:] + SPORES[2:], seed=550 + i)
        erupt.append(f)
    retract = []
    for i, h in enumerate((40, 26, 12, 0)):
        f = blank(RW, RH)
        _mound(f, 16, 9 - i, 4 if i < 3 else 3, seed=560 + i, glow=0.0)
        _spike(f, h, seed=1.3)
        if i == 3:
            FX.burst(f, 16, RG - 1, 3, DIRT, seed=566, jag=0.5)
        retract.append(f)
    write_sheet("root_spike", "projectiles", RW, RH, [16, 64], [
        ("travel", travel, dict(fps=12, loop=True)),
        ("erupt", erupt, dict(durations=[0.08, 0.05, 0.12, 0.2, 0.2], loop=False, events=[(1, "hit")])),
        ("retract", retract, dict(durations=[0.08, 0.08, 0.08, 0.12], loop=False)),
    ])


# ======================================================================= thorn_wall (32x96)
TW, TH = 32, 96
TG = 95


def _wall(height, sway, seed, dead=False, glow=0.6):
    """A braid of thorny vines: three strands winding around each other with gaps between them,
    curling tendrils on the sides, pale thorns and glowing cyan buds."""
    f = blank(TW, TH)
    if height <= 1:
        return f
    cv = TCanvas(TW, TH)
    m_front = BRAMBLE if not dead else BRAMBLE_DEAD
    m_back = BRAMBLE_DK if not dead else BRAMBLE_DEAD
    n = 20
    strands = []
    for j in range(3):
        ph = TAU * j / 3
        pts = []
        for i in range(n + 1):
            t = i / n
            x = 16 + 7.5 * math.sin(t * height / 9.0 + ph) * (1 - 0.25 * t) + sway * t * t * 2.0
            y = TG + 1 - height * t
            pts.append((x, y))
        strands.append(pts)
    # back halves first (where the strand moves behind), then the front halves
    for front in (False, True):
        for j, pts in enumerate(strands):
            ph = TAU * j / 3
            for i in range(n):
                t = (i + 0.5) / n
                is_front = math.cos(t * height / 9.0 + ph) > 0
                if is_front != front:
                    continue
                r0 = 3.4 * (1.0 - 0.45 * (i / n) ** 1.5)
                r1 = 3.4 * (1.0 - 0.45 * ((i + 1) / n) ** 1.5)
                cv.part()
                cv.capsule(pts[i], pts[i + 1], r0, r1, m_front if front else m_back)
    # curling tendrils on the sides
    cv.part()
    for k in range(3):
        y0 = TG - height * (0.25 + 0.25 * k)
        if y0 < TG - height + 6:
            continue
        sgn = 1 if k % 2 else -1
        base = (16 + sgn * 7, y0)
        pts = [base]
        a = -PI / 2 + sgn * 1.2
        for i in range(6):
            a -= sgn * 0.45
            pts.append((pts[-1][0] + math.cos(a) * 2.2, pts[-1][1] + math.sin(a) * 2.2))
        cv.chain(pts, [1.3, 1.1, 1.0, 0.9, 0.8, 0.7, 0.6], m_front)
    # thorns all along
    cv.part()
    for j, pts in enumerate(strands):
        for i in range(1, n, 2):
            if (i + j) % 3:
                continue
            bx, by = pts[i]
            sgn = 1 if bx >= 16 else -1
            cv.poly([(bx + sgn * 2.0, by - 1.3), (bx + sgn * 6.5, by - 3.6 + (i % 3)), (bx + sgn * 2.0, by + 1.1)],
                    THORN if not dead else BRAMBLE_DEAD, bevel=0.6, base=0.62)
    if not dead:
        cv.part(line=False)
        for j, pts in enumerate(strands):
            for i in range(3 + j, n, 5):
                cv.disc((pts[i][0], pts[i][1]), 1.3, BUD, flat=min(1.0, 0.5 + 0.4 * glow))
    f = cv.render()
    f[TG + 1:] = 0
    return f


def thorn_wall():
    rise = []
    for i, h in enumerate((18, 42, 66, 86, 92)):
        f = _wall(h, 0.6 * math.sin(i), seed=600 + i)
        FX.burst(f, 8, TG - 1, 4 - i * 0.5, DIRT, seed=610 + i, jag=0.5)
        FX.burst(f, 24, TG - 1, 4 - i * 0.5, DIRT, seed=615 + i, jag=0.5)
        if i < 3:
            for (x, y, s) in ((3, TG - 10 - 10 * i, 2), (28, TG - 14 - 8 * i, 1), (5, TG - 24 - 12 * i, 1)):
                FX.chunk(f, x, y, s, DIRT[0], DIRT[2])
        rise.append(f)
    idle = []
    for i in range(4):
        f = _wall(92, 0.8 * math.sin(TAU * i / 4), seed=620, glow=0.5 + 0.4 * math.sin(TAU * i / 4))
        idle.append(f)
    wither = []
    for i, (h, dead) in enumerate(((90, False), (84, True), (64, True), (36, True), (10, True))):
        f = _wall(h, -0.5 * i, seed=630, dead=dead, glow=0.2)
        FX.burst(f, 16, TG - 1, 3 + i * 0.6, DIRT, seed=640 + i, jag=0.5)
        motes(f, 16, TG - h * 0.5, 6, 4, 14, [C("#8a7a60"), C("#6a5c48")], seed=645 + i)
        wither.append(f)
    write_sheet("thorn_wall", "projectiles", TW, TH, [16, 96], [
        ("rise", rise, dict(durations=[0.05, 0.05, 0.05, 0.06, 0.1], loop=False)),
        ("idle", idle, dict(fps=6, loop=True)),
        ("wither", wither, dict(durations=[0.08, 0.08, 0.08, 0.08, 0.12], loop=False)),
    ])


# ======================================================================= void_well (64x64)
VW = 64
VC = 32.0


def _well(f, r, rot, arms=1.0, lens=1.0):
    """Black hole: dark core, rotating pink/violet spiral arms, lensing ring."""
    h, w = f.shape[:2]
    if r <= 0.5:
        return f
    # spiral arms (behind the core)
    for k in range(3):
        a0 = rot + TAU * k / 3
        for i in range(40):
            t = i / 39
            a = a0 + t * 3.4
            d = r * (1.0 + 1.6 * t) * arms
            x, y = VC + math.cos(a) * d, VC + math.sin(a) * d * 0.9
            col = [PINKS[1], PINKS[2], VIOLETS[1], VIOLETS[2], VIOLETS[3]][min(4, int(t * 5))]
            put(f, int(x), int(y), col)
            if t < 0.45:
                put(f, int(x + 1), int(y), col)
    # lensing ring
    if lens > 0:
        ring(f, VC, VC, r + 2.5, PINKS[2], only_empty=False)
        ring(f, VC, VC, r + 4.5, VIOLETS[2], dotted=2, only_empty=False)
    # core
    for y in range(h):
        for x in range(w):
            d = math.hypot(x + 0.5 - VC, y + 0.5 - VC)
            if d <= r:
                f[y, x] = C("#05020a") if d < r - 1.2 else C("#2a1240")
            elif d <= r + 1.0:
                f[y, x] = PINKS[1]
    return f


def void_well():
    opn = []
    for i, r in enumerate((1.5, 4.0, 7.0, 9.5, 11.0)):
        f = blank(VW, VW)
        _well(f, r, rot=0.5 * i, arms=0.6 + 0.1 * i, lens=0.0 if i < 2 else 1.0)
        motes(f, VC, VC, 10, r + 6, r + 18, PINKS + VIOLETS[:2], seed=700 + i)
        if i == 0:
            FX.sparkle(f, 32, 32, 6, PINKS[0], VIOLETS[1])
        opn.append(f)
    idle = []
    for i in range(6):
        f = blank(VW, VW)
        _well(f, 11.0 + 0.4 * math.sin(TAU * i / 6), rot=TAU / 3 * i / 6, arms=1.0)
        rng = np.random.default_rng(710)
        for k in range(8):
            a0 = rng.random() * TAU
            off = rng.random()
            t = (i / 6 + off) % 1.0
            a = a0 + 2.0 * t
            d = 30 * (1 - t) + 12 * t
            put(f, int(VC + math.cos(a) * d), int(VC + math.sin(a) * d), [C("#ffffff"), PINKS[1], VIOLETS[0]][k % 3])
        idle.append(f)
    close = []
    for i, r in enumerate((9.0, 6.0, 3.0, 1.0)):
        f = blank(VW, VW)
        _well(f, r, rot=1.0 + 0.6 * i, arms=1.0 - 0.2 * i, lens=1.0 if i < 2 else 0.0)
        if i == 3:
            FX.sparkle(f, 32, 32, 5, PINKS[0], VIOLETS[1])
            ring(f, VC, VC, 14, VIOLETS[2], dotted=2)
        close.append(f)
    write_sheet("void_well", "projectiles", VW, VW, [32, 32], [
        ("open", opn, dict(durations=[0.06, 0.06, 0.08, 0.08, 0.1], loop=False)),
        ("idle", idle, dict(fps=12, loop=True)),
        ("close", close, dict(durations=[0.06, 0.06, 0.08, 0.1], loop=False)),
    ])


# ======================================================================= eclipse_ring (16x16)
def _band_px(x, y, i):
    """Ring band: outside edge at y=3 (bright rim), dark void core, inside edge at y=12."""
    if y < 3 or y > 12:
        return None
    if y == 3:
        return PINKS[1] if (x + i * 2) % 8 < 5 else PINKS[0]
    if y == 4:
        return PINKS[2]
    if y == 12:
        return VIOLETS[1]
    if y == 11:
        return VIOLETS[2]
    # core with drifting sparkles (period 16 in x, 4 px per frame)
    sp = (x - 4 * i) % 16
    if (y == 7 and sp == 2) or (y == 9 and sp == 10):
        return VIOLETS[0]
    if (y == 6 and sp in (5, 13)) or (y == 10 and sp in (7, 15)):
        return VIOLETS[3]
    return C("#12061f") if 6 <= y <= 9 else C("#24103a")


def eclipse_ring():
    seg, edge = [], []
    for i in range(4):
        f = blank(16, 16)
        for y in range(16):
            for x in range(16):
                c = _band_px(x, y, i)
                if c is not None:
                    f[y, x] = c
        # soft outer glow pixels (above the rim)
        for x in range(16):
            if (x + i * 3) % 5 == 0:
                f[2, x] = VIOLETS[2]
        seg.append(f)
        g = blank(16, 16)
        for y in range(16):
            for x in range(16):
                c = _band_px(x, y, i)
                if c is None:
                    continue
                # rounded frayed cap facing +x around x = 9
                cap = 9.0 - 3.2 * (abs(y - 7.5) / 4.5) ** 2
                if x <= cap:
                    g[y, x] = c
                elif x <= cap + 1.2:
                    g[y, x] = PINKS[1] if (y + i) % 2 else PINKS[2]
        for k in range(3):
            put(g, 11 + (k + i) % 4, 5 + 3 * k, PINKS[1 + (k % 2)])
        edge.append(g)
    write_sheet("eclipse_ring", "projectiles", 16, 16, [8, 8], [
        ("segment", seg, dict(fps=12, loop=True)),
        ("edge", edge, dict(fps=12, loop=True)),
    ])


# ======================================================================= shadow_mark (32x32)
def _mark(f, prog, rot, pulse=0.0, cross=1.0):
    c = 15.5
    r = 10.5 + pulse
    # shadow pool
    for y in range(32):
        for x in range(32):
            d = math.hypot(x + 0.5 - 16, y + 0.5 - 16)
            if d < r - 1.5 and prog > 0.5:
                if (x + y) % 2 == 0 or d < r * 0.6:
                    f[y, x] = C("#1a0a2a") if d < r * 0.6 else C("#2a1240")
    dash_arc(f, 16, 16, r, r, -PI / 2 + rot, -PI / 2 + rot + TAU * min(1.0, prog), PINKS[2], dash=4, gap=2,
             only_empty=False)
    if prog > 0.4:
        dash_arc(f, 16, 16, r + 3, r + 3, rot * -1.3, rot * -1.3 + TAU * min(1.0, prog), VIOLETS[2], dash=2, gap=3,
                 only_empty=False)
    if cross > 0 and prog >= 0.75:
        ln = 6.0 * cross
        for sgn in (-1, 1):
            for t in np.linspace(-ln, ln, int(ln * 4)):
                x, y = 16 + t, 16 + sgn * t
                put(f, int(x), int(y), PINKS[1])
                put(f, int(x) + 1, int(y), PINKS[2])
    return f


def shadow_mark():
    appear = []
    for i, pr in enumerate((0.3, 0.6, 0.85, 1.0)):
        f = blank(32, 32)
        _mark(f, pr, rot=0.0, cross=1.0 if i == 3 else 0.5)
        motes(f, 16, 16, 4 + i * 2, 6, 15, PINKS[1:] + VIOLETS[1:3], seed=800 + i, inward=0.3)
        appear.append(f)
    idle = []
    for i in range(4):
        f = blank(32, 32)
        _mark(f, 1.0, rot=TAU / 8 * i / 4 * 2, pulse=0.6 * math.sin(TAU * i / 4))
        idle.append(f)
    strike = []
    for i in range(4):
        f = blank(32, 32)
        if i < 2:
            FX.burst(f, 16, 16, 11 - 3 * i, [C("#ffffff"), PINKS[1], PINKS[2], VIOLETS[2], C("#1a0a2a")], seed=810 + i,
                     jag=0.4, rays=8)
        else:
            ring(f, 16, 16, 8 + 3 * (i - 2) + 3, VIOLETS[i - 1], dotted=2 if i == 3 else 0)
            motes(f, 16, 16, 10, 4, 14, PINKS[1:] + VIOLETS[1:3], seed=815 + i)
        strike.append(f)
    write_sheet("shadow_mark", "projectiles", 32, 32, [16, 16], [
        ("appear", appear, dict(durations=[0.05, 0.05, 0.05, 0.08], loop=False)),
        ("idle", idle, dict(fps=10, loop=True)),
        ("strike", strike, dict(durations=[0.05, 0.06, 0.08, 0.1], loop=False)),
    ])


# ======================================================================= frost_floor (16x16)
def _frost(f, i, cover=1.0, melt=0.0):
    """Ice veil tile (periodic in x).  Row 4 = floor surface.  `cover` < 1 = the frost is still
    spreading (dithered, fainter), `melt` > 0 = thinning out."""
    for x in range(16):
        for y in range(4, 16):
            h = ((x * 7 + y * 13) % 16) / 16.0          # ordered dither threshold
            if h > cover:
                continue
            if y == 4:
                f[y, x] = (240, 252, 255, int((235 - 80 * melt) * min(1.0, 0.4 + cover)))
                continue
            a = (170 - 9 * (y - 5)) * (1 - 0.7 * melt) * min(1.0, 0.35 + 0.65 * cover)
            base = (190, 232, 250) if y < 8 else (150, 206, 238)
            f[y, x] = (*base, max(0, int(a)))
    return f


def frost_floor():
    def glints(f, phase, alpha=255):
        for k in range(2):
            gx = (phase + 8 * k) % 16
            for d in range(3):
                x, y = (gx + d) % 16, 6 + d
                if f[y, x, 3]:
                    f[y, x] = (255, 255, 255, min(255, alpha))

    def rime(f, i, h=1.0):
        for (x, ln) in ((2, 3), (7, 2), (11, 4), (14, 2)):
            for k in range(int(round(ln * h))):
                y = 3 - k
                if y >= 0:
                    f[y, x] = (230, 248, 255, 230) if k < ln - 1 else (255, 255, 255, 255)
            if h > 0.6 and (x + i) % 3 == 0:
                f[1, x + 1 if x < 15 else x] = (200, 240, 255, 200)

    form = []
    for i, cv_ in enumerate((0.25, 0.5, 0.75, 1.0)):
        f = blank(16, 16)
        _frost(f, i, cover=cv_)
        rime(f, i, h=cv_)
        form.append(f)
    idle = []
    for i in range(6):
        f = blank(16, 16)
        _frost(f, i)
        rime(f, i)
        glints(f, int(round(i * 16 / 6)))
        if i % 2 == 0:
            f[2 + (i // 2) % 2, (5 + i * 3) % 16] = (255, 255, 255, 255)
        idle.append(f)
    melt = []
    for i, m in enumerate((0.25, 0.5, 0.75, 0.92)):
        f = blank(16, 16)
        _frost(f, i, melt=m)
        rime(f, i, h=1.0 - m)
        for x in (3, 9, 13):
            y = 6 + (i * 2 + x) % 8
            f[y, x] = (200, 240, 255, 220)
        melt.append(f)
    write_sheet("frost_floor", "objects", 16, 16, [0, 4], [
        ("form", form, dict(durations=[0.06, 0.06, 0.08, 0.1], loop=False)),
        ("idle", idle, dict(fps=8, loop=True)),
        ("melt", melt, dict(durations=[0.1, 0.1, 0.12, 0.14], loop=False)),
    ])


# ======================================================================= lava_flood (liquid format)
LF_CREST = C("#fff6c2")
LF_FOAM = C("#ffd86a")
LF_BODY = [C("#ffb13b"), C("#ff7a2a"), C("#e2451e"), C("#a8261a")]
LF_CRUST = [C("#5a2c24"), C("#3a1a18")]
STREAKS = [(1, 2, 5, 1, 0), (9, 5, 4, -1, 2), (4, 8, 6, 1, 0), (12, 11, 3, 1, 2), (0, 13, 5, -1, 0), (7, 15, 3, 1, 2)]


def _flood_body(x, y, t):
    c = LF_BODY[1]
    for (x0, sy, ln, d, tone) in STREAKS:
        if y != sy:
            continue
        if (x - x0 - 2 * t * d) % 16 < ln:
            c = LF_BODY[tone]
    if (x * 7 + y * 3) % 23 == 0:
        c = LF_BODY[3]
    return c


def lava_flood():
    img = np.zeros((32, 128, 4), np.uint8)
    for f in range(8):
        ox = f * 16
        for x in range(16):
            crest = 3.0 + 1.4 * math.sin(TAU * x / 16 - TAU * f / 8) + 0.7 * math.sin(TAU * 2 * x / 16 + TAU * f / 4)
            cy = int(round(crest))
            for y in range(16):
                if y < cy - 2:
                    continue
                if y < cy:
                    # heat shimmer above the crest
                    if (x + f + y) % 5 == 0:
                        img[y, ox + x] = (*LF_FOAM[:3], 150)
                    continue
                if y == cy:
                    c = LF_CREST
                elif y == cy + 1:
                    c = LF_FOAM
                elif y == cy + 2:
                    c = LF_BODY[0]
                else:
                    c = _flood_body(x, y, f)
                img[y, ox + x] = c
            for y in range(16):
                img[16 + y, ox + x] = _flood_body(x, y, f)
        # drifting crust floes riding the crest
        for k in range(2):
            x0 = (3 + 8 * k + 2 * f) % 16
            for dx in range(3):
                x = (x0 + dx) % 16
                crest = 3.0 + 1.4 * math.sin(TAU * x / 16 - TAU * f / 8) + 0.7 * math.sin(TAU * 2 * x / 16 + TAU * f / 4)
                y = int(round(crest)) + 1
                img[y, ox + x] = LF_CRUST[dx % 2]
        # rising bubbles
        for (bx, by) in ((4, 9), (11, 3), (14, 12)):
            y = (by - 2 * f) % 16
            img[16 + y, ox + bx] = LF_CREST
            if y >= 7:
                img[y, ox + bx] = LF_CREST
    save_png(img, "sprites/objects/lava_flood.png")
    save_json({"texture": "sprites/objects/lava_flood.png", "frameWidth": 16, "frameHeight": 16, "pivot": [0, 0],
               "clips": {"surface": {"row": 0, "count": 8, "fps": 8, "loop": True},
                         "body": {"row": 1, "count": 8, "fps": 8, "loop": True}}},
              "animations/lava_flood.json")


def build():
    root_spike()
    thorn_wall()
    void_well()
    eclipse_ring()
    shadow_mark()
    frost_floor()
    lava_flood()

