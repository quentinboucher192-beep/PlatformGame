"""The pictures of « Le Balancier » and « Encore un tour » (2.82, tools/assetgen/environment/story_horloger.py): every
animated element of the two transition films drawn here as layered pixel art, each sheet a set of clips.

  * l'Échappement, a pocket watch the size of a moon, in parts that share one point (the centre of its dial) so that a
    scene moves them together: its crown (a toothed rim with docks: the teeth turn, `tourne`), its glass dome over six
    wedge-shaped worlds laid out like the hours of a dial (dark, flickering, waking one wedge after another, lit), the
    lights above them (a sprite of its own: drawn at another depth, they float over the dome when the camera moves),
    two hands drawn in perspective on the dial (24 angles: `folle`, `ralentit`, `tic`), its glass and its brass ribs,
    the gears of its back (`tourne`, `folle`), its balance rod (whole or snapped, and the piece that broke off) and,
    after a defeat, Asterion chained under it as a living balance wheel;
  * the time tear (opening, shimmering, closing);
  * the mechanical hand (its palm and its fingers apart, so that the fingers close in front of what it holds) and its arm;
  * the golden threads of stolen time (a long thread, the stitches) and Asterion's wounds (open, closing, a scar);
  * an old man's silhouette, backlit, whose eyes are gears (they light up, then turn), and the close-up of those eyes;
  * frost taking on frozen debris, a ghost dial and its needle, near stars, a veil, the wave of frozen time, black.

Nothing here names anyone: the old man is a silhouette. Deterministic (fixed seeds, no clock)."""
import math

import numpy as np
from PIL import Image, ImageDraw

import story_asterion as A

TAU = 2 * math.pi
BRASS = ["#1e1008", "#3e240e", "#6a4218", "#966424", "#c08a32", "#e2b24a", "#fbe08a", "#fff6cc"]
VERDI = ["#16302c", "#25574c", "#3f8a76", "#74c8ac"]
CYAN = ["#06222a", "#0e5a64", "#1fa8a8", "#39f0d8", "#9ff6ff", "#e8ffff"]
INK = "#0c0604"

# The Échappement: one canvas for its parts, the centre of its dial at (OX, OY).
CW, CH = 272, 172
OX, OY = 136, 86
RX, RY = 124, 56          # the crown's outer edge
RXI, RYI = 108, 49        # the dome's edge (the dial)
BAND = 14                 # the crown's side
TEETH = 48
HD = 40                   # the dome's height (projected)
RS = math.sqrt(RYI ** 2 + HD ** 2)  # the dome's silhouette above the dial
# The six worlds of the dial, clockwise from twelve o'clock (docs/RAIDS.md §34).
BIOMES = [  # name, dark, base, light, its lights
    ("serre", "#1a5a30", "#3fa85a", "#8fe8a0", "#c8ffb0"),
    ("glacier", "#2f6aa8", "#80c8f2", "#e0f6ff", "#e6fffb"),
    ("forge", "#6a2208", "#e0701e", "#ffd070", "#ffe6a0"),
    ("ruines", "#3a2062", "#8a52c8", "#d6a8ff", "#f0c8ff"),
    ("archives", "#4a4e5c", "#9aa0ae", "#e6eaf2", "#ffffff"),
    ("cadran", "#9a8048", "#e8d49a", "#fff8dc", "#fff2c0"),
]
NIGHT = np.array([10, 14, 30], np.float32)


# =================================================================== little tools
def C(h, a=255):
    h = h.lstrip("#")
    return np.array([int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a], np.float32)


def blank(w, h):
    return np.zeros((h, w, 4), np.float32)


def grid(w, h):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    return xx + 0.5, yy + 0.5


def dilate(m):
    return m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)


def edge(m):
    return m & ~(np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1))


def lit_edge(m):
    """Pixels of `m` whose upper-left neighbour is empty (they catch the light)."""
    return m & ~np.roll(np.roll(m, 1, 0), 1, 1)


def dark_edge(m):
    return m & ~np.roll(np.roll(m, -1, 0), -1, 1)


def paint(img, m, h, a=255):
    img[m] = C(h, a)


def ramp(img, m, val, cols, lo=0, hi=None):
    """Paints `m` with the colours `cols[lo:hi]`, picked by `val` (0..1): pixel-art shading."""
    cols = cols[lo:hi]
    idx = np.clip((np.clip(val, 0, 0.999) * len(cols)).astype(int), 0, len(cols) - 1)
    for i, h in enumerate(cols):
        img[m & (idx == i)] = C(h)


def outline(img, m, h=INK, a=255):
    o = dilate(m) & ~m
    img[o] = C(h, a)


def poly_mask(w, h, pts):
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).polygon([(float(x), float(y)) for x, y in pts], fill=255)
    return np.array(im) > 0


def line_mask(w, h, pts, width=1):
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).line([(float(x), float(y)) for x, y in pts], fill=255, width=int(width))
    return np.array(im) > 0


def ell_mask(w, h, cx, cy, rx, ry):
    X, Y = grid(w, h)
    return ((X - cx) / max(rx, 1e-3)) ** 2 + ((Y - cy) / max(ry, 1e-3)) ** 2 <= 1.0


def u8(img):
    return np.clip(img, 0, 255).astype(np.uint8)


def add_glow(img, m, color, radius, strength=1.0):
    """An additive sprite's soft light around `m` (its alpha falls off over `radius` pixels)."""
    h, w = m.shape
    d = np.full((h, w), 1e9, np.float32)
    ys, xs = np.nonzero(m)
    X, Y = grid(w, h)
    for y, x in zip(ys[::3], xs[::3]):
        d = np.minimum(d, (X - x - 0.5) ** 2 + (Y - y - 0.5) ** 2)
    a = np.clip(1.0 - np.sqrt(d) / radius, 0, 1) ** 2 * 255 * strength
    c = C(color)
    keep = a > img[..., 3]
    img[keep, :3] = c[:3]
    img[keep, 3] = np.maximum(img[keep, 3], a[keep])


# =================================================================== gears
def gear(R, teeth, spokes, rot, td=4, glass=True, seed=0, light=1.0):
    """A brass gear of radius R turned by `rot` (radians): its teeth, a rim, spokes, a hub with a cyan glass, verdigris
    and rivets that turn with it, lit from the top left whatever its angle (so it reads as turning)."""
    size = 2 * R + 6
    c = size / 2.0
    X, Y = grid(size, size)
    dx, dy = X - c, Y - c
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx) - rot
    u = (th * teeth / TAU) % 1.0
    body = R - td
    tooth = np.abs(u - 0.5) < (0.3 - 0.1 * np.clip((r - body) / td, 0, 1))
    m_teeth = (r <= R) & (r > body - 0.5) & tooth
    rim_in = body - max(3, R // 8)
    hub = max(4.0, R * 0.26)
    v = (th * spokes / TAU) % 1.0
    half = max(1.6, R * 0.07) / np.maximum(r * TAU / spokes, 1e-3)
    spoke = np.abs(v - 0.5) < half
    m_body = r <= body
    hole = m_body & (r < rim_in) & (r > hub + 1) & ~spoke
    m = (m_body | m_teeth) & ~hole
    img = blank(size, size)
    val = 0.5 - 0.3 * (dx * 0.6 + dy * 0.8) / R
    val = val + np.where(r > rim_in, 0.12, 0.0) - np.where(r < hub + 1, 0.05, 0.0)
    pat = np.sin(th * spokes * 2 + seed) * np.sin(r * 0.55 + seed * 1.7) + 0.5 * np.sin(th * spokes * 5 + r * 0.3 + seed)
    ramp(img, m, val * light, BRASS, 1, 7)
    ver = m & (pat > 0.95) & (r < body - 1)
    ramp(img, ver, 0.3 + 0.4 * (val - 0.3), VERDI, 1, 4)
    img[lit_edge(m) & (r < body - 1)] = C(BRASS[6])
    img[dark_edge(m)] = C(BRASS[1])
    rv = np.zeros_like(m)
    for k in range(spokes):
        a = (k + 0.5) / spokes * TAU + rot
        rv |= (np.hypot(X - c - math.cos(a) * (rim_in + 1.5), Y - c - math.sin(a) * (rim_in + 1.5)) < 1.0)
    img[rv & m] = C(BRASS[7])
    if glass:
        g = r < hub * 0.62
        ramp(img, g, 0.35 + 0.5 * np.clip(-(dx + dy) / (hub * 1.4), -0.4, 0.6), CYAN, 1, 6)
        img[(r >= hub * 0.62) & (r < hub * 0.62 + 1.2)] = C(BRASS[2])
    outline(img, m)
    return u8(img)


def gear_sheet(R, teeth, spokes, n=16, seed=0, light=1.0):
    """One turn of 1/spokes (the gear's own symmetry: the loop never jumps), `n` images."""
    return [gear(R, teeth, spokes, k * TAU / spokes / n, seed=seed, light=light) for k in range(n)]


# =================================================================== the Échappement
def _crown_coords():
    X, Y = grid(CW, CH)
    nx, ny = (X - OX) / RX, (Y - OY) / RY
    return X, Y, np.hypot(nx, ny), np.arctan2(ny, nx)


RHO_I = RXI / RX


def crown(phase):
    """The crown (its top ring, its side and its teeth, the docks of the Nova), its teeth turned by `phase` (0..1 of a
    tooth)."""
    X, Y, rho, phi = _crown_coords()
    tooth = ((phi * TEETH / TAU + phase) % 1.0) < 0.5
    outer = rho <= 1.0 + np.where(tooth, 0.03, 0.0)
    ring = outer & (rho > RHO_I)
    img = blank(CW, CH)
    band = np.zeros_like(outer)
    for dy in range(1, BAND + 1):
        band |= np.roll(outer, dy, 0)
    band &= ~outer & (Y > OY)
    # the side: its teeth seen edge-on (lighter faces, dark gaps), darker downwards
    fx = np.clip((X - OX) / RX, -1, 1)
    phib = np.arccos(fx)
    tb = ((phib * TEETH / TAU + phase) % 1.0) < 0.5
    ybase = OY + RY * np.sqrt(np.clip(1 - fx * fx, 0, 1))
    depth = np.clip((Y - ybase) / BAND, 0, 1)
    sval = 0.62 - 0.45 * depth + np.where(tb, 0.12, -0.12) - 0.15 * np.abs(fx) ** 3
    ramp(img, band, sval, BRASS, 0, 6)
    img[band & (np.abs(Y - ybase - 1.0) < 0.6)] = C(BRASS[6])
    # the top ring: an engraved bezel
    rv = 0.55 - 0.28 * np.sin(phi) - 0.2 * (X - OX) / RX * 0.5
    rv = rv + np.where(rho > 0.97, 0.18, 0.0) * (np.sin(phi) < 0) - np.where(rho < RHO_I + 0.025, 0.3, 0.0)
    ramp(img, ring, rv, BRASS, 1, 8)
    for k in range(12):  # the hours, engraved
        a = -math.pi / 2 + k * TAU / 12
        d = np.abs(np.angle(np.exp(1j * (phi - a))))
        mk = ring & (d < (0.03 if k % 3 else 0.05)) & (rho > RHO_I + 0.025) & (rho < 0.97)
        img[mk] = C(BRASS[1])
    # the docks of the Nova: four gates on the side and the ring, their lights cyan
    for a in (0.95, 2.19):
        d = np.abs(phib - a)
        gate = band & (d < 0.07) & (Y > ybase + 2) & (Y < ybase + BAND - 2)
        img[gate] = C("#120a06")
        img[gate & (np.abs(Y - ybase - BAND * 0.5) < 1.0)] = C(CYAN[3])
        img[band & (d >= 0.07) & (d < 0.085)] = C(BRASS[6])
    for a in (-0.95, -2.19):
        d = np.abs(np.angle(np.exp(1j * (phi - a))))
        dock = ring & (d < 0.06)
        img[dock] = C("#120a06")
        img[dock & (np.abs(rho - (RHO_I + 1) / 2) < 0.02)] = C(CYAN[4])
    shape = ring | band
    outline(img, shape)
    return u8(img)


def _dial_coords():
    X, Y = grid(CW, CH)
    u, v = (X - OX) / RXI, (Y - OY) / RYI
    rd = np.hypot(u, v)
    ang = (np.degrees(np.arctan2(v, u)) + 90.0) % 360.0  # 0 at twelve o'clock, clockwise
    return X, Y, u, v, rd, ang


def biome_colours():
    """The six worlds of the dome, lit: each wedge its ground and its pattern (per pixel of the canvas)."""
    X, Y, u, v, rd, ang = _dial_coords()
    out = np.zeros((CH, CW, 3), np.float32)
    n1 = A.value_noise(CW, CH, 9, 311)
    n2 = A.value_noise(CW, CH, 4, 312)
    for k, (name, dk, base, light, lamp) in enumerate(BIOMES):
        cols = [C(dk)[:3], C(base)[:3], C(light)[:3]]
        w = (ang >= k * 60) & (ang < (k + 1) * 60)
        if name == "serre":  # rows of greenhouses, little trees
            idx = np.where(n1 > 0.62, 2, np.where(n1 > 0.4, 1, 0))
            rows = (np.abs(rd - 0.5) < 0.025) | (np.abs(rd - 0.76) < 0.025)
            idx = np.where(rows, 2, idx)
        elif name == "glacier":  # crevasses, white peaks
            idx = np.where(n1 > 0.7, 2, 1)
            idx = np.where(np.abs(n2 - 0.5) < 0.05, 0, idx)
        elif name == "forge":  # rivers of lava turning in loops
            s = np.sin(rd * 30 + n1 * TAU * 1.5)
            idx = np.where(np.abs(s) < 0.28, 2, np.where(n2 > 0.55, 1, 0))
        elif name == "ruines":  # columns, flagstones, runes
            col = (np.abs(((ang - k * 60) % 7.5) - 3.75) < 0.9) & (rd > 0.4) & (np.sin(rd * 40) > 0.2)
            idx = np.where(col, 2, np.where(n1 > 0.5, 1, 0))
        elif name == "archives":  # shelves in rings, books
            idx = np.where((np.floor(rd * 16) % 2) == 0, 1, 0)
            idx = np.where(((ang * 3) % 4 < 1) & (idx == 1), 2, idx)
        else:  # the central dial: its rings and its ticks
            idx = np.where(np.abs((rd * 6) % 1 - 0.5) < 0.08, 0, 1)
            idx = np.where((((ang + 3) % 6) < 1.2) & (rd > 0.55), 2, idx)
        for i in range(3):
            out[w & (idx == i)] = cols[i]
    return out


def dome(bright, twinkle=0, hub=1.0):
    """The six worlds under the glass, each lit `bright[k]` (0.2 dark .. 1 lit), the spokes between them, the Salle du
    Balancier at the centre."""
    X, Y, u, v, rd, ang = _dial_coords()
    m = rd <= 1.0
    lit = biome_colours()
    b = np.zeros((CH, CW), np.float32)
    for k in range(6):
        b[(ang >= k * 60) & (ang < (k + 1) * 60)] = bright[k]
    rgb = lit * b[..., None] + NIGHT * (1 - b[..., None]) * 0.6
    rgb *= np.where(rd > 0.9, 0.62, 1.0)[..., None]  # the shadow of the crown's wall
    rgb = np.floor(rgb / 8.0) * 8.0
    img = blank(CW, CH)
    img[m, :3] = rgb[m]
    img[m, 3] = 255
    # the spokes between the worlds
    for k in range(6):
        a = math.radians(k * 60 - 90)
        dist = np.abs(-(X - OX) * math.sin(a) + (Y - OY) * math.cos(a) * (RXI / RYI)) * (RYI / RXI)
        front = ((X - OX) * math.cos(a) + (Y - OY) * math.sin(a) * (RXI / RYI)) > 0
        sp = m & front & (dist < 1.1) & (rd > 0.16)
        img[sp] = C(BRASS[5])
        img[m & front & (dist >= 1.1) & (dist < 1.9) & (rd > 0.16)] = C(BRASS[2])
    # the Salle du Balancier: a brass ring, its glass heart
    ring = (rd <= 0.17) & (rd > 0.115)
    ramp(img, ring, 0.55 - 0.5 * v, BRASS, 2, 7)
    core = rd <= 0.115
    ramp(img, core, (0.25 + 0.6 * hub) - 0.3 * (u + v), CYAN, 0, 6)
    sw = int(twinkle) % 4
    px = (np.abs(X - OX - (sw - 1.5) * 1.5) < 0.6) & (np.abs(Y - OY - 1) < 1.6) & core
    img[px] = C(BRASS[7] if hub > 0.5 else BRASS[3])
    img[edge(m)] = C(INK)
    return u8(img)


def lights(on, frame, seed=40):
    """The lights over the six worlds (an additive sprite): `on[k]` each wedge's (0..1), twinkling with `frame`; drawn a
    little above the dome, at another depth, they float over it as the camera moves."""
    img = blank(CW, CH)
    rng = np.random.default_rng(seed)
    X, Y = grid(CW, CH)
    for k, (name, dk, base, light, lamp) in enumerate(BIOMES):
        pts = []
        while len(pts) < 11:
            a = math.radians(k * 60 - 90 + float(rng.uniform(6, 54)))
            r = float(rng.uniform(0.25, 0.9))
            pts.append((OX + math.cos(a) * r * RXI, OY + math.sin(a) * r * RYI - float(rng.uniform(2, 7)), int(rng.integers(0, 4))))
        if on[k] <= 0.01:
            continue
        haze_a = math.radians(k * 60 - 60)
        hx, hy = OX + math.cos(haze_a) * 0.58 * RXI, OY + math.sin(haze_a) * 0.58 * RYI - 6
        d = np.hypot((X - hx) / 26.0, (Y - hy) / 13.0)
        ha = np.clip(1 - d, 0, 1) ** 2 * 70 * on[k]
        c = C(lamp)
        keep = ha > img[..., 3]
        img[keep, :3] = c[:3]
        img[keep, 3] = ha[keep]
        for i, (x, y, ph) in enumerate(pts):
            if (frame + ph) % 4 == 3:
                continue
            xi, yi = int(x), int(y)
            img[yi, xi] = C(lamp, int(255 * on[k]))
            if i % 3 == 0:
                for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    img[yi + ddy, xi + ddx] = C(lamp, int(130 * on[k]))
    return u8(img)


HW, HH = 236, 124  # the hands' canvas (the dial and a margin), their pivot the dial's centre
HPX, HPY = 118, 64


def hand(alpha_deg, kind):
    """A hand of the dial at `alpha_deg` (clockwise from twelve), drawn in perspective on the dial with its shadow:
    the long one (minutes: a slender needle, a cyan glass tip, a ring of counterweight) or the short one (hours: a
    spade with a hole)."""
    a = math.radians(alpha_deg)
    d = (math.sin(a), -math.cos(a))
    p = (math.cos(a), math.sin(a))

    def proj(s, w, lift):
        return (HPX + (d[0] * s + p[0] * w) * RXI, HPY + (d[1] * s + p[1] * w) * RYI - lift)

    if kind == "m":
        prof = [(-0.18, 0.012), (-0.06, 0.03), (0.0, 0.032), (0.5, 0.018), (0.84, 0.01), (0.93, 0.0)]
    else:
        prof = [(-0.12, 0.016), (0.0, 0.05), (0.3, 0.036), (0.42, 0.03), (0.5, 0.075), (0.6, 0.0)]
    img = blank(HW, HH)
    for lift, shadow in ((0.0, True), (3.0, False)):
        left = [proj(s, w, lift) for s, w in prof]
        right = [proj(s, -w, lift) for s, w in reversed(prof)]
        m = poly_mask(HW, HH, left + right)
        if kind == "m":
            ring = ell_mask(HW, HH, *proj(-0.15, 0, lift), 0.045 * RXI, 0.045 * RYI) & ~ell_mask(HW, HH, *proj(-0.15, 0, lift),
                                                                                               0.022 * RXI, 0.022 * RYI)
            m |= ring
        if shadow:
            sh = np.roll(m, 2, 0)
            img[sh] = np.array([4, 6, 16, 110], np.float32)
            continue
        X, Y = grid(HW, HH)
        along = ((X - HPX) / RXI * d[0] + (Y - HPY + lift) / RYI * d[1])
        val = 0.55 + 0.25 * np.sign(d[0] + 1e-6) * ((X - HPX) / RXI * p[0] + (Y - HPY + lift) / RYI * p[1]) * 8 + 0.1 * along
        ramp(img, m, val, BRASS, 2, 8)
        spine = line_mask(HW, HH, [proj(0.0, 0, lift), proj(prof[-2][0], 0, lift)])
        img[spine & m] = C(BRASS[7])
        if kind == "m":
            tip = poly_mask(HW, HH, [proj(0.8, 0, lift), proj(0.86, 0.02, lift), proj(0.93, 0, lift), proj(0.86, -0.02, lift)])
            ramp(img, tip, 0.6 + 0.2 * along, CYAN, 2, 6)
        else:
            hole = ell_mask(HW, HH, *proj(0.5, 0, lift), 0.028 * RXI, 0.028 * RYI)
            img[hole] = 0
            m &= ~hole
            jew = ell_mask(HW, HH, *proj(0.5, 0, lift), 0.012 * RXI + 0.6, 0.012 * RYI + 0.6)
            img[jew] = C(CYAN[4])
            m |= jew
        outline(img, m)
    if kind == "m":  # the cap over the centre (on the top hand)
        cap = ell_mask(HW, HH, HPX, HPY - 3, 6.5, 3.6)
        ramp(img, cap, 0.7 - 0.12 * (grid(HW, HH)[1] - HPY + 3), BRASS, 3, 8)
        img[ell_mask(HW, HH, HPX, HPY - 3.5, 2.2, 1.3)] = C(CYAN[4])
        outline(img, cap)
    return u8(img)


def glass(sheen):
    """The glass of the dome (additive): its silhouette rimmed in cyan, brighter at its edges, a highlight at the top
    left and a sheen sweeping across (`sheen`: its place, None: none)."""
    X, Y = grid(CW, CH)
    nx = (X - OX) / RXI
    s_up = np.hypot(nx, (Y - OY) / RS)
    s_lo = np.hypot(nx, (Y - OY) / RYI)
    s = np.where(Y < OY, s_up, s_lo)
    m = s <= 1.0
    img = blank(CW, CH)
    a = (14 + 120 * np.clip(s, 0, 1) ** 7)
    c = C(CYAN[4])
    img[m, :3] = c[:3]
    img[m, 3] = a[m]
    sil = edge(m) & (Y < OY + 2)
    img[sil] = C(CYAN[5], 210)
    ang = np.degrees(np.arctan2((Y - OY) / RS, nx))
    spec = m & (s > 0.8) & (s < 0.88) & (ang > -160) & (ang < -112)
    img[spec] = C("#ffffff", 190)
    spec2 = m & (s > 0.62) & (s < 0.66) & (ang > -150) & (ang < -128)
    img[spec2] = C("#ffffff", 120)
    if sheen is not None:
        bx = (X - OX) + (Y - OY) * 0.9
        sh = m & (bx > sheen) & (bx < sheen + 9) & (Y < OY + 8)
        img[sh, :3] = C("#e8ffff")[:3]
        img[sh, 3] = np.maximum(img[sh, 3], 90)
        sh2 = m & (bx > sheen + 13) & (bx < sheen + 16) & (Y < OY + 8)
        img[sh2, :3] = C("#e8ffff")[:3]
        img[sh2, 3] = np.maximum(img[sh2, 3], 60)
    return u8(img)


def ribs():
    """The brass ribs of the dome (meridians between the worlds, a ring, the finial at its top): those at the back dimmer,
    seen through the glass."""
    img = blank(CW, CH)
    im = Image.new("RGBA", (CW, CH), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    back = Image.new("RGBA", (CW, CH), (0, 0, 0, 0))
    db = ImageDraw.Draw(back)

    def pt(e, phi):
        return (OX + RXI * math.cos(e) * math.cos(phi), OY + RYI * math.cos(e) * math.sin(phi) - HD * math.sin(e))

    for k in range(6):
        phi = math.radians(k * 60 - 90)
        pts = [pt(math.radians(j * 3), phi) for j in range(31)]
        target = db if math.sin(phi) < -0.05 else dr
        target.line(pts, fill=tuple(int(x) for x in C(BRASS[5] if target is dr else BRASS[3])), width=1)
    ring = [pt(math.radians(38), math.radians(j * 4)) for j in range(91)]
    for i in range(90):
        seg = ring[i:i + 2]
        mid = math.radians(i * 4 + 2)
        (db if math.sin(mid) < 0 else dr).line(seg, fill=tuple(int(x) for x in C(BRASS[4] if math.sin(mid) >= 0 else BRASS[3])), width=1)
    top = pt(math.pi / 2, 0)
    dr.polygon([(top[0] - 3, top[1] + 1), (top[0] + 3, top[1] + 1), (top[0], top[1] - 9)], fill=tuple(int(x) for x in C(BRASS[5])))
    dr.ellipse([top[0] - 2, top[1] - 2, top[0] + 2, top[1] + 2], fill=tuple(int(x) for x in C(CYAN[4])))
    b = np.array(back).astype(np.float32)
    b[..., 3] *= 0.55
    f = np.array(im).astype(np.float32)
    img = np.where(f[..., 3:4] > 0, f, b)
    m = f[..., 3] > 0
    o = dilate(m) & ~m & (np.array(back)[..., 3] == 0)
    img[o] = C(INK, 160)
    return u8(img)


# --- the balance rod
RW, RH = 48, 164
RPX, RPY = 24, 3


def rod(broken=False, spark=0):
    """The balance rod hanging under the dial: a brass stem with collars ending in a balance wheel (a ring, its spokes,
    its glass); snapped, only its stump remains, its torn end bright, sparks crackling (`spark`)."""
    img = blank(RW, RH)
    X, Y = grid(RW, RH)
    stem = (np.abs(X - RPX) < 2.6) & (Y > 2) & (Y < 132)
    for y in (6, 36, 66, 96):
        stem |= (np.abs(X - RPX) < 5.0) & (np.abs(Y - y - 2) < 2.2)
    mount = poly_mask(RW, RH, [(RPX - 9, 0), (RPX + 9, 0), (RPX + 4, 7), (RPX - 4, 7)])
    wheel_o = ell_mask(RW, RH, RPX, 141, 21, 15)
    wheel_i = ell_mask(RW, RH, RPX, 141, 16.5, 11)
    wheel = wheel_o & ~wheel_i
    spokes = np.zeros_like(wheel)
    for k in range(3):
        a = k * TAU / 3 + 0.3
        spokes |= line_mask(RW, RH, [(RPX, 141), (RPX + math.cos(a) * 17, 141 + math.sin(a) * 11.5)], 2)
    core = ell_mask(RW, RH, RPX, 141, 5, 3.6)
    m = stem | mount | wheel | (spokes & wheel_i) | core
    if broken:
        cut = 46 + 3 * np.sin(X * 1.9) + 2 * np.cos(X * 3.1)
        m &= Y < cut
        tear = m & (Y > cut - 2.5)
    val = 0.6 - 0.35 * (X - RPX) / 6.0
    ramp(img, m, val, BRASS, 1, 8)
    if not broken:
        ramp(img, wheel_i & ~spokes & ~core, 0.45 + 0.4 * (-(X - RPX) / 16 - (Y - 141) / 11) * 0.5, CYAN, 1, 4)
        img[core] = C(CYAN[4])
        img[ell_mask(RW, RH, RPX - 1, 140, 1.5, 1.1)] = C("#ffffff")
        m |= wheel_i
    else:
        img[tear] = C(BRASS[7])
        rng = np.random.default_rng(55 + spark)
        for _ in range(7):
            x = int(RPX + rng.integers(-7, 8))
            y = int(46 + rng.integers(-2, 9))
            if 0 <= x < RW and 0 <= y < RH:
                img[y, x] = C(CYAN[5] if rng.random() < 0.5 else "#fff6cc")
    outline(img, m)
    return u8(img)


def rod_piece():
    """The piece that broke off: the rest of the stem and the balance wheel, its top torn."""
    full = rod(False).astype(np.float32)
    X, Y = grid(RW, RH)
    cut = 46 + 3 * np.sin(X * 1.9) + 2 * np.cos(X * 3.1)
    full[Y < cut] = 0
    torn = (Y >= cut) & (Y < cut + 2) & (full[..., 3] > 0)
    full[torn] = C(BRASS[7])
    return u8(full)


# --- Asterion chained under the dial (a defeat)
AW, AH = 208, 280
APX, APY = 104, 3


def asterion_parts():
    """The titan's layers (assets/ui/raids/asterion.png), each look's cells, and the canvas they are laid on."""
    meta = A.manifest()
    sheet = np.array(Image.open(A.SPRITE_PNG).convert("RGBA"))
    return meta, sheet


def compose_titan(heart_frame=0, look="affaibli"):
    meta, sheet = asterion_parts()
    cw, chh = meta["canvas"]
    can = np.zeros((chh, cw, 4), np.float32)
    for lay in meta["layers"]:
        if lay["id"] == "aura":
            continue
        lk = "intact" if lay["id"] == "coeur" else look
        cells = lay["cells"].get(lk) or lay["cells"]["intact"]
        cx, cy = cells[heart_frame % len(cells)] if lay["id"] == "coeur" else cells[0]
        x, y, w, h = lay["box"]
        src = sheet[cy:cy + h, cx:cx + w].astype(np.float32)
        a = src[..., 3:4] / 255.0
        dst = can[y:y + h, x:x + w]
        dst[..., :3] = src[..., :3] * a + dst[..., :3] * (1 - a)
        dst[..., 3:4] = np.maximum(dst[..., 3:4], src[..., 3:4])
    return can


def _chain(img, p0, p1, glint):
    """Brass links from p0 to p1, every third one catching the light where `glint` (0..1) runs along it."""
    n = max(2, int(math.hypot(p1[0] - p0[0], p1[1] - p0[1]) / 4.0))
    h, w = img.shape[:2]
    for i in range(n + 1):
        t = i / n
        x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
        rx, ry = (2.6, 1.6) if i % 2 == 0 else (1.4, 2.6)
        mk = ell_mask(w, h, x, y, rx, ry) & ~ell_mask(w, h, x, y, rx - 1.1, ry - 1.1)
        near = abs(t - glint) < 0.07
        img[mk] = C("#fff6cc" if near else ("#ffd27a" if i % 2 == 0 else "#e2a84a"))
        outline(img, mk, INK, 200)


def chained(frame):
    """Asterion as a living balance wheel: the rod, its cross bar, chains to the titan's shoulders and around its waist,
    the golden scars of the threads that sewed it, its heart beating (`frame`)."""
    img = blank(AW, AH)
    X, Y = grid(AW, AH)
    stem = (np.abs(X - APX) < 2.6) & (Y > 2) & (Y < 64)
    for y in (8, 34):
        stem |= (np.abs(X - APX) < 5.0) & (np.abs(Y - y - 2) < 2.2)
    bar = (np.abs(Y - 66) < 3.2) & (np.abs(X - APX) < 54)
    ends = ell_mask(AW, AH, APX - 54, 66, 5, 5) | ell_mask(AW, AH, APX + 54, 66, 5, 5)
    m = stem | bar | ends
    ramp(img, m, 0.6 - 0.3 * (Y - 60) / 10.0, BRASS, 1, 8)
    outline(img, m)
    titan = compose_titan(frame)
    tx, ty = APX - 80, 104
    a = titan[..., 3:4] / 255.0
    dst = img[ty:ty + 128, tx:tx + 160]
    dst[..., :3] = titan[..., :3] * a + dst[..., :3] * (1 - a)
    dst[..., 3:4] = np.maximum(dst[..., 3:4], titan[..., 3:4])
    sh_l = (tx + A.JOINT["bras_gauche"][0] + 6, ty + A.JOINT["bras_gauche"][1] - 12)
    sh_r = (tx + A.JOINT["bras_droit"][0] - 6, ty + A.JOINT["bras_droit"][1] - 12)
    g = (frame % 4) / 4.0
    _chain(img, (APX - 54, 70), sh_r, g)
    _chain(img, (APX + 54, 70), sh_l, (g + 0.5) % 1.0)
    wy = ty + A.JOINT["abdomen"][1] - 6
    _chain(img, (tx + 46, wy - 4), (tx + 114, wy + 4), (g + 0.25) % 1.0)
    # the golden scars, sewn
    for (sx, sy), ln in (((tx + 74, ty + 46), 12), ((tx + 92, ty + 62), 10), ((tx + 60, ty + 70), 8)):
        for j in range(ln):
            x, y = sx + j, sy + (1 if (j // 2) % 2 else -1)
            img[int(y), int(x)] = C("#ffd27a" if (j + frame) % 5 else "#fff6cc")
    return u8(img)


# =================================================================== the time tear
TW, TH = 128, 232


def tear(openness, frame):
    """The tear in time (drawn opaque: its depths) — a jagged lens of `openness` (0..1), its edges shivering with
    `frame`, its inside dark teal crossed by flowing cyan bands and tick marks."""
    img = blank(TW, TH)
    X, Y = grid(TW, TH)
    cx, cy = TW / 2, TH / 2
    yy = (Y - cy) / (TH / 2 - 6)
    rng = np.random.default_rng(900 + frame)
    jag = np.interp(Y[:, 0], np.arange(0, TH, 6), rng.uniform(-1, 1, (TH + 5) // 6 + 1)[: len(np.arange(0, TH, 6))])
    half = (TW / 2 - 10) * openness * np.clip(1 - yy ** 2, 0, 1) ** 0.75 + jag[:, None] * 3.0 * openness
    m = np.abs(X - cx - jag[:, None] * 2.0) < np.maximum(half, 0) + (0.8 if openness > 0 else 0)
    m &= np.abs(yy) < 0.98 * min(1.0, 0.25 + openness)
    inside = np.zeros((TH, TW, 3), np.float32)
    inside[:] = C("#04121a")[:3]
    bands = np.sin(Y * 0.35 - frame * 1.1 + np.sin(X * 0.2) * 0.8)
    inside[bands > 0.82] = C(CYAN[1])[:3]
    inside[bands > 0.95] = C(CYAN[2])[:3]
    ticks = ((Y.astype(int) + frame * 3) % 14 == 0) & (np.abs(X - cx) < half * 0.5)
    inside[ticks] = C(CYAN[3])[:3]
    img[m, :3] = inside[m]
    img[m, 3] = 255
    e = edge(m)
    img[e] = C(CYAN[4])
    img[edge(m & ~e) & m] = C(CYAN[3])
    return u8(img)


def tear_glow(openness, frame):
    """The tear's light (additive): a halo around it, shards of light leaping from its edges."""
    base = tear(openness, frame)[..., 3] > 0
    img = blank(TW, TH)
    if openness <= 0:
        return u8(img)
    add_glow(img, edge(base), CYAN[3], 10, 0.8 * min(1.0, openness * 1.5))
    rng = np.random.default_rng(1300 + frame)
    ys, xs = np.nonzero(edge(base))
    for i in rng.choice(len(xs), size=min(len(xs), 10), replace=False) if len(xs) else []:
        x, y = int(xs[i]), int(ys[i])
        dx = 1 if x > TW / 2 else -1
        for j in range(int(rng.integers(2, 6))):
            xx = x + dx * j
            if 0 <= xx < TW:
                img[y, xx] = C(CYAN[5], 230 - j * 35)
    return u8(img)


# =================================================================== shapes
def _capsule(w, h, p0, p1, r):
    X, Y = grid(w, h)
    vx, vy = p1[0] - p0[0], p1[1] - p0[1]
    L2 = max(vx * vx + vy * vy, 1e-6)
    t = np.clip(((X - p0[0]) * vx + (Y - p0[1]) * vy) / L2, 0, 1)
    return np.hypot(X - p0[0] - t * vx, Y - p0[1] - t * vy) <= r


def _metal(img, m, cx, cy, r, lo=1, hi=8, bias=0.0):
    X, Y = grid(img.shape[1], img.shape[0])
    val = 0.55 + bias - 0.35 * ((X - cx) * 0.7 + (Y - cy) * 0.5) / max(r, 1)
    ramp(img, m, val, BRASS, lo, hi)
    img[lit_edge(m)] = C(BRASS[6])
    img[dark_edge(m)] = C(BRASS[1])


# =================================================================== golden threads, wounds
def thread(n_on, glint):
    """A golden thread of stolen time (additive), `n_on` (0..1) of it drawn from its root, its head a spark, glints
    running along it."""
    w, h = 200, 16
    img = blank(w, h)
    L = int(round((w - 4) * n_on))
    for x in range(L):
        y = 8 + int(round(math.sin(x * 0.12 + glint * 0.8) * 1.2))
        img[y, x] = C("#ffd27a", 220)
        img[y - 1, x] = C("#ffb84a", 80)
        img[y + 1, x] = C("#ffb84a", 80)
        if (x + glint * 14) % 37 < 3:
            img[y, x] = C("#fff6cc", 255)
    if 0 < n_on < 1:
        x = L
        for dx, dy, a in ((0, 0, 255), (1, 0, 200), (-1, 0, 200), (0, 1, 200), (0, -1, 200), (2, 0, 90), (-2, 0, 90), (0, 2, 90), (0, -2, 90)):
            if 0 <= x + dx < w:
                img[8 + dy, x + dx] = C("#ffffff", a)
    return u8(img)


SW_, SH_ = 44, 26


def wound(state, frame):
    """A wound of the titan: a glowing crack (`open`, flickering), closing as it is sewn (`state` 0..1), a gold scar."""
    img = blank(SW_, SH_)
    pts = [(4, 13), (11, 10), (17, 15), (24, 9), (31, 14), (39, 11)]
    m = line_mask(SW_, SH_, pts, 3)
    core = line_mask(SW_, SH_, pts, 1)
    if state >= 1.0:
        img[core] = C("#ffd27a", 200)
        return u8(img)
    fl = 0.8 + 0.2 * ((frame * 7) % 3) / 2
    halo = dilate(dilate(m)) & ~m
    img[halo] = C("#ff5a2a", int(90 * fl * (1 - state)))
    img[m] = C("#c8401a", int(255 * (1 - state * 0.7)))
    img[core] = C("#ffe6a8" if state < 0.5 else "#ffd27a", int(255 * fl))
    return u8(img)


def stitches(k):
    """The stitches of golden thread across a wound (additive), `k` of six done, the newest sparkling."""
    img = blank(SW_, SH_)
    for i in range(min(k, 6)):
        x = 6 + i * 6
        for a, b in (((x - 2, 7), (x + 2, 19)), ((x + 2, 7), (x - 2, 19))):
            m = line_mask(SW_, SH_, [a, b], 1)
            img[m] = C("#ffd27a", 230)
        if i == k - 1 and k <= 6:
            img[ell_mask(SW_, SH_, x, 13, 2.2, 2.2)] = C("#fff6cc", 255)
    return u8(img)


# =================================================================== the old man
OMW, OMH = 156, 184
EYE_L, EYE_R = (69, 64), (85, 64)


def old_man(breath=0):
    """A silhouette, backlit by the dome: an old man in a coat with a high collar, gear-shaped epaulettes, a watch chain,
    wild hair and a loupe on his brow. Seen from the front, his eyes overlaid (eyes_small)."""
    w, h = OMW, OMH
    img = blank(w, h)
    sy = breath
    body = poly_mask(w, h, [(0, h), (6, 150 + sy), (16, 124 + sy), (34, 110 + sy), (52, 104 + sy), (56, 84 + sy), (66, 94 + sy),
                            (90, 94 + sy), (100, 84 + sy), (104, 104 + sy), (122, 110 + sy), (140, 124 + sy), (150, 150 + sy), (w, h)])
    neck = poly_mask(w, h, [(70, 84), (86, 84), (88, 100 + sy), (68, 100 + sy)])
    head = ell_mask(w, h, 77, 62, 15.5, 20)
    jaw = poly_mask(w, h, [(63, 66), (91, 66), (84, 84), (70, 84)])
    m = body | neck | head | jaw
    rng = np.random.default_rng(71)
    hair = np.zeros_like(m)
    for i in range(18):  # wild hair: tufts standing out above the ears, a few wisps on top
        side = -1 if i % 2 == 0 else 1
        top = i >= 12
        a = math.radians((-90 + side * float(rng.uniform(10, 40))) if top else (90 - side * 90 + side * float(rng.uniform(-70, -15))))
        r0 = 15.5
        L = float(rng.uniform(6, 10)) if top else float(rng.uniform(9, 17))
        p0 = (77 + math.cos(a) * r0, 60 + math.sin(a) * r0 * 1.15)
        bend = side * float(rng.uniform(0.2, 0.6))
        p1 = (p0[0] + math.cos(a) * L * 0.6, p0[1] + math.sin(a) * L * 0.6 - 2)
        p2 = (p1[0] + math.cos(a + bend) * L * 0.5, p1[1] + math.sin(a + bend) * L * 0.5 - 1)
        hair |= line_mask(w, h, [p0, p1, p2], 2 if i % 3 else 3)
    m |= ell_mask(w, h, 61, 64, 3, 5) | ell_mask(w, h, 93, 64, 3, 5)  # ears
    for side in (-1, 1):  # epaulettes like the halves of gears
        cx = 77 + side * 48
        ep = ell_mask(w, h, cx, 116 + sy, 15, 7)
        X, Y = grid(w, h)
        th = np.arctan2(Y - 116 - sy, X - cx)
        teeth = ell_mask(w, h, cx, 116 + sy, 18, 9) & (((th * 10 / TAU) % 1) < 0.5) & (Y < 116 + sy)
        m |= ep | teeth
    m |= hair
    img[m] = C("#0b0a14")
    img[m & (grid(w, h)[1] > 130)] = C("#08070f")
    X, Y = grid(w, h)
    rim_top = m & ~np.roll(m, 1, 0)
    rim_l = m & ~np.roll(m, 1, 1)
    rim_r = m & ~np.roll(m, -1, 1)
    img[rim_l & (Y < 150)] = C("#c08a32")
    img[rim_r & (Y < 150)] = C(CYAN[3])
    img[rim_top] = C(CYAN[4])
    # the loupe on his brow, the watch chain on his coat
    lo = ell_mask(w, h, 90, 49, 4.5, 4.5) & ~ell_mask(w, h, 90, 49, 3, 3)
    img[lo] = C(BRASS[4])
    img[ell_mask(w, h, 89, 48, 1.0, 1.0)] = C(CYAN[5])
    for j in range(22):
        t = j / 21
        x = 58 + 40 * t
        y = 136 + sy + 10 * math.sin(t * math.pi)
        if j % 2 == 0:
            img[int(y), int(x)] = C(BRASS[5] if j % 6 else BRASS[7])
    return u8(img)


def eyes_small(k, level=1.0):
    """The old man's eyes (additive): two little gears of light, turned by `k` (0..5 of a tooth's sixth)."""
    w, h = 32, 14
    img = blank(w, h)
    X, Y = grid(w, h)
    for ex in (8.5, 23.5):
        dx, dy = X - ex, Y - 7
        r = np.hypot(dx, dy)
        th = np.arctan2(dy, dx) - k * TAU / 6 / 6
        teeth = (r < 4.6) & (r >= 3.2) & (((th * 6 / TAU) % 1) < 0.5)
        ring = (r < 3.2) & (r >= 2.0)
        pupil = r < 1.2
        img[teeth] = C("#ffd27a", int(230 * level))
        img[ring] = C("#ffb84a", int(200 * level))
        img[pupil] = C(CYAN[5], int(255 * level))
    return u8(img)


GW_, GH_ = 260, 132
GEL, GER = (94, 70), (166, 70)


def gaze(open_, k):
    """The close-up: the upper half of a face in shadow, its eyes two gears (an outer toothed ring turning one way, an
    inner glass ring the other), a monocle over one of them where the six worlds of the dome are reflected, white
    bushy brows, crow's feet; `open_` (0..1) its lids, `k` (0..7) the gears' turn."""
    w, h = GW_, GH_
    img = blank(w, h)
    X, Y = grid(w, h)
    face = np.ones((h, w), bool)
    val = 0.42 + 0.25 * np.exp(-((X - 130) / 30) ** 2 - ((Y - 84) / 34) ** 2) - 0.42 * np.clip(np.hypot((X - 130) / 130, (Y - 70) / 70) - 0.55, 0, 1) * 2.2
    skin = ["#08070c", "#100e16", "#1a1722", "#26222e", "#34303c"]
    ramp(img, face, val, skin)
    for ex, ey in (GEL, GER):  # sockets, wrinkles
        sock = ell_mask(w, h, ex, ey, 30, 20)
        img[sock & face] = C(skin[0])
        for j in range(3):
            side = 1 if ex > 130 else -1
            ln = line_mask(w, h, [(ex + side * 30, ey - 4 + j * 6), (ex + side * 44, ey - 10 + j * 9)], 1)
            img[ln & face] = C(skin[0])
    nose = poly_mask(w, h, [(125, 62), (135, 62), (142, h), (118, h)])
    ramp(img, nose & face, 0.35 + 0.2 * (130 - X) / 16 - 0.25 * np.clip((Y - 100) / 32, 0, 1), skin, 1, 4)
    img[line_mask(w, h, [(125, 66), (121, 112)], 1)] = C(CYAN[1])  # the cold light on its ridge
    for ex, ey in (GEL, GER):
        dx, dy = X - ex, Y - ey
        r = np.hypot(dx, dy)
        th = np.arctan2(dy, dx)
        to = th - k * TAU / 16 / 8
        ti = th + k * TAU / 6 / 8
        lid = np.abs(dy) < 17 * max(0.08, open_)
        outer = (r < 17) & (r >= 12.5) & ((((to * 16 / TAU) % 1) < 0.55) | (r < 14.5)) & lid
        inner = (r < 12.5) & (r >= 6) & lid
        spoke = inner & ((((ti * 6 / TAU) % 1) < 0.22))
        pupil = (r < 6) & lid
        lvl = 0.35 + 0.65 * open_
        ramp(img, outer, (0.45 + 0.4 * (-(dx + dy) / 24)) * lvl + 0.1, BRASS, 2, 8)
        ramp(img, inner & ~spoke, (0.4 + 0.3 * (-(dx + dy) / 18)) * lvl + 0.1, CYAN, 1, 5)
        img[spoke] = C(BRASS[5])
        img[pupil] = C("#020206")
        img[ell_mask(w, h, ex - 2, ey - 2, 1.5, 1.5) & lid] = C("#ffffff")
        lidline = (np.abs(np.abs(dy) - 17 * max(0.08, open_)) < 1.0) & (np.abs(dx) < 19)
        img[lidline] = C(skin[0])
    # the monocle over the right eye: brass ring, the six worlds reflected in its glass
    mx, my = GER
    mr = np.hypot(X - mx, Y - my)
    ring = (mr < 23) & (mr >= 20.5)
    ramp(img, ring, 0.55 - 0.3 * (Y - my) / 23, BRASS, 2, 8)
    ang = (np.degrees(np.arctan2(Y - my, X - mx)) + 90) % 360
    refl = (mr < 20.5) & (mr > 13) & (ang > 260) | (mr < 20.5) & (mr > 13) & (ang < 30)
    for k2 in range(6):
        wd = refl & (((ang + 100) % 360) // 22 == k2)
        if wd.any():
            c = C(BIOMES[k2][3])
            img[wd, :3] = img[wd, :3] * 0.55 + c[:3] * 0.45
    img[line_mask(w, h, [(mx + 21, my + 6), (mx + 34, h)], 1)] = C(BRASS[4])  # its chain
    # white bushy brows
    rng = np.random.default_rng(17)
    for ex, ey in (GEL, GER):
        for i in range(30):
            x0 = ex - 30 + i * 2 + float(rng.uniform(-1, 1))
            y0 = ey - 24 - 6 * math.sin(i / 29 * math.pi) + float(rng.uniform(-1, 1))
            ln = line_mask(w, h, [(x0, y0 + 2), (x0 + float(rng.uniform(3, 8)) * (1 if ex > 130 else -1) * 0.6 + 2, y0 - float(rng.uniform(2, 7)))], 2 if i % 4 else 1)
            img[ln] = C("#8a8e9a" if i % 3 else ("#e6eaf2" if i % 2 else "#b8bcc8"))
    return u8(img)


# =================================================================== frost, dial, sky, black
def frost(k):
    """Frost taking on a frozen chunk (additive): a crystal growing (`k` 0..4), then glinting (5, 6)."""
    w = 22
    img = blank(w, w)
    c = w / 2
    grow = min(k, 4) / 4.0
    for i in range(6):
        a = i * TAU / 6 + 0.3
        L = 2 + 8 * grow
        img[line_mask(w, w, [(c, c), (c + math.cos(a) * L, c + math.sin(a) * L)], 1)] = C(CYAN[4], 200)
        if grow > 0.5:
            bx, by = c + math.cos(a) * L * 0.6, c + math.sin(a) * L * 0.6
            for s in (-0.7, 0.7):
                img[line_mask(w, w, [(bx, by), (bx + math.cos(a + s) * 3, by + math.sin(a + s) * 3)], 1)] = C(CYAN[3], 160)
    if k == 5:
        img[int(c), int(c)] = C("#ffffff")
        img[int(c) - 1:int(c) + 2, int(c)] = C("#ffffff", 220)
        img[int(c), int(c) - 1:int(c) + 2] = C("#ffffff", 220)
    return u8(img)


def ghost_dial(size=320):
    """A dial of light (additive): rings, sixty ticks, twelve hours, a heart."""
    img = blank(size, size)
    X, Y = grid(size, size)
    c = size / 2
    r = np.hypot(X - c, Y - c)
    th = (np.degrees(np.arctan2(Y - c, X - c)) + 90) % 360
    for rr, a in ((150, 200), (146, 120), (118, 90), (20, 150)):
        img[np.abs(r - rr) < 0.7] = C(CYAN[4], a)
    tick = (np.abs(((th + 3) % 6) - 3) < 0.35) & (r > 138) & (r < 146)
    hour = (np.abs(((th + 15) % 30) - 15) < 0.8) & (r > 124) & (r < 146)
    img[tick] = C(CYAN[3], 170)
    img[hour] = C(CYAN[5], 230)
    for k in range(12):
        a = math.radians(k * 30 - 90)
        x, y = c + math.cos(a) * 108, c + math.sin(a) * 108
        img[poly_mask(size, size, [(x, y - 4), (x + 3, y), (x, y + 4), (x - 3, y)])] = C(CYAN[4], 200)
    return u8(img)


def ghost_needle():
    w, h = 16, 150
    img = blank(w, h)
    m = poly_mask(w, h, [(8, 2), (9.5, 120), (8, 140), (6.5, 120)])
    ring = ell_mask(w, h, 8, 130, 5, 5) & ~ell_mask(w, h, 8, 130, 3, 3)
    img[m | ring] = C(CYAN[4], 220)
    img[line_mask(w, h, [(8, 4), (8, 126)], 1)] = C(CYAN[5], 255)
    return u8(img)


def near_stars(w=480, h=270, seed=511):
    """Near stars for the parallax (transparent): a few bright ones with their crosses, many faint."""
    img = blank(w, h)
    rng = np.random.default_rng(seed)
    for i in range(170):
        x, y = int(rng.integers(2, w - 2)), int(rng.integers(2, h - 2))
        c = ["#ffffff", "#cfe8ff", "#fff0c8", "#e0d0ff"][i % 4]
        if i < 16:
            for dx, dy, a in ((0, 0, 255), (1, 0, 150), (-1, 0, 150), (0, 1, 150), (0, -1, 150), (2, 0, 60), (-2, 0, 60), (0, 2, 60), (0, -2, 60)):
                img[y + dy, x + dx] = C(c, a)
        else:
            img[y, x] = C(c, int(rng.integers(90, 200)))
    return u8(img)


def veil(w=320, h=180, seed=611):
    """A veil of cold light (additive): cyan and brass wisps."""
    n = A.value_noise(w, h, 40, seed)
    m2 = A.value_noise(w, h, 14, seed + 1)
    out = np.zeros((h, w, 4), np.float32)
    a = np.clip((n - 0.45) * 2.4, 0, 1) * (0.5 + 0.5 * m2)
    mix = np.clip((m2 - 0.4) * 2, 0, 1)[..., None]
    out[..., :3] = C("#1fa8a8")[:3] * (1 - mix) + C("#c08a32")[:3] * mix
    out[..., 3] = np.floor(a * 6) / 6 * 150
    out[..., :3] = np.floor(out[..., :3] / 16) * 16
    return u8(out)


def wave(w=40, h=270):
    """The wave of frozen time (additive): a band of cyan light, brighter at its middle."""
    img = blank(w, h)
    X, Y = grid(w, h)
    a = np.clip(1 - np.abs(X - w / 2) / (w / 2), 0, 1) ** 2
    img[..., :3] = C(CYAN[4])[:3]
    img[..., 3] = np.floor(a * 8) / 8 * 200
    img[np.abs(X - w / 2) < 1, :3] = C("#ffffff")[:3]
    return u8(img)


def black():
    img = np.zeros((46, 80, 4), np.uint8)
    img[..., 3] = 255
    return img


# =================================================================== 2.82, redone after review: the defeat film's own pictures
def titan_flee(frame):
    """Asterion fleeing: its own layers, leaning into its flight and turned three quarters (squashed, sheared), the cracks
    of its wounds glowing through its armour (flickering with `frame`), a reddish heat on its back."""
    can = compose_titan(frame % 4)
    src = Image.fromarray(u8(can), "RGBA")
    w, h = 200, 150
    out = src.transform((w, h), Image.AFFINE, (1 / 0.86, 0.32 / 0.86, -24 / 0.86 - 0.32 * 136 / 0.86, 0, 1, -14), resample=Image.NEAREST)
    img = np.array(out).astype(np.float32)
    m = img[..., 3] > 0
    rng = np.random.default_rng(31)
    cracks = np.zeros((h, w), bool)
    for _ in range(9):
        x, y = float(rng.uniform(70, 140)), float(rng.uniform(40, 120))
        pts = [(x, y)]
        for _ in range(5):
            x += float(rng.uniform(-5, 5))
            y += float(rng.uniform(-6, 6))
            pts.append((x, y))
        cracks |= line_mask(w, h, pts, 1)
    cracks &= m & ~edge(m)
    fl = (0.75, 1.0, 0.85, 0.95)[frame % 4]
    rim = dilate(cracks) & m & ~cracks
    img[rim] = img[rim] * 0.4 + C("#ff5a2a") * 0.6 * fl
    img[rim, 3] = 255
    img[cracks, :3] = C("#ffe6a8" if frame % 2 else "#ffb84a")[:3]
    X, Y = grid(w, h)
    back = m & (X > 120 + (Y - 75) * 0.3)
    img[back & edge(m), :3] = C("#ff8a3a")[:3]
    return u8(img)


def streaks(w=480, h=270, seed=733):
    """Stars streaking past (additive): the trail a fast flight leaves on the sky."""
    img = blank(w, h)
    rng = np.random.default_rng(seed)
    for i in range(90):
        x, y = int(rng.integers(0, w)), int(rng.integers(1, h - 1))
        L = int(rng.integers(6, 40))
        c = ["#ffffff", "#cfe8ff", "#fff0c8"][i % 3]
        for k in range(L):
            if x + k < w:
                img[y, x + k] = C(c, int(220 * (1 - k / L)))
    return u8(img)


SHW, SHH = 290, 196
SHX, SHY = 18, 134          # the wrist (the pivot)
SIDE_FINGERS = [(116, 0.0, 1.0), (121, 0.08, 0.9), (126, 0.16, 0.8), (131, 0.24, 0.7)]  # root y, delay, light (index first)
SIDE_SEG = [(44, 15), (34, 13), (26, 11)]


def _joint_gear(img, p, r, turn, m_all):
    g = gear(int(r), 8, 4, turn, td=2, glass=True, seed=5)
    gh, gw = g.shape[:2]
    x0, y0 = int(round(p[0] - gw / 2)), int(round(p[1] - gh / 2))
    H_, W_ = img.shape[:2]
    for yy in range(gh):
        for xx in range(gw):
            if g[yy, xx, 3] and 0 <= y0 + yy < H_ and 0 <= x0 + xx < W_:
                img[y0 + yy, x0 + xx] = g[yy, xx]
                m_all[y0 + yy, x0 + xx] = True


def side_hand(c):
    """The immense hand, seen from the side, palm up, reaching right: its palm and its far fingers (first image, behind
    what it holds), its near fingers and its thumb (second image, in front); `c` (0 open .. 1 closed) curls each finger
    joint by joint, the index first, the little gears of the joints turning as they bend."""
    back, front = blank(SHW, SHH), blank(SHW, SHH)
    X, Y = grid(SHW, SHH)
    palm = poly_mask(SHW, SHH, [(SHX, SHY - 16), (150, SHY - 22), (162, SHY - 12), (160, SHY + 24), (SHX + 4, SHY + 26)])
    pv = 0.75 - 0.5 * (Y - SHY + 22) / 48.0 - 0.1 * (X - 80) / 80
    ramp(back, palm, pv, BRASS, 1, 7)
    back[palm & (np.abs(Y - SHY + 19) < 0.8)] = C(BRASS[6])  # the palm's face catches the light
    for x in (56, 96, 130):
        back[palm & (np.abs(X - x) < 0.6) & (Y > SHY - 14)] = C(BRASS[1])
    back[palm & (A.value_noise(SHW, SHH, 5, 93) > 0.78)] = C(VERDI[1])
    back[palm & (np.abs(Y - SHY - 6) < 2.0) & (np.abs(X - 110) < 22)] = C(CYAN[2])  # a slit of cyan glass
    m_back = palm.copy()
    m_front = np.zeros_like(palm)
    for i, (ry, delay, light) in reversed(list(enumerate(SIDE_FINGERS))):
        ci = float(np.clip((c - delay) / (1 - 0.24), 0, 1))
        ci = ci * ci * (3 - 2 * ci)
        target, m_t = (front, m_front) if i < 2 else (back, m_back)
        p = (152.0 + i * 2, float(ry))
        a = math.radians(-6 - 2 * i)
        bends = (-64, -80, -72)
        for j, (L, wdt) in enumerate(SIDE_SEG):
            a += math.radians(bends[j] * ci + (-3 if j else 0))
            q = (p[0] + math.cos(a) * L, p[1] + math.sin(a) * L)
            seg = _capsule(SHW, SHH, p, q, wdt / 2)
            _metal(target, seg, (p[0] + q[0]) / 2, (p[1] + q[1]) / 2, wdt, bias=-0.18 * (1 - light))
            ln = line_mask(SHW, SHH, [((p[0] + q[0]) / 2 - math.sin(a) * 2, (p[1] + q[1]) / 2 + math.cos(a) * 2),
                                      (q[0] - math.cos(a) * 4, q[1] - math.sin(a) * 4)], 1) & seg
            target[ln] = C(BRASS[2])
            outline(target, seg)
            m_t |= seg
            if j == 2:
                tip = poly_mask(SHW, SHH, [(q[0] - math.sin(a) * 4, q[1] + math.cos(a) * 4), (q[0] + math.sin(a) * 4, q[1] - math.cos(a) * 4),
                                           (q[0] + math.cos(a) * 8, q[1] + math.sin(a) * 8)])
                _metal(target, tip, q[0], q[1], 5, 3, 8, 0.15)
                outline(target, tip)
                m_t |= tip
            _joint_gear(target, p, wdt / 2 + 1, ci * 2.4 + j, m_t)
            p = q
    # the thumb, near side, closing over what the hand holds
    ct = float(np.clip(c / 0.85, 0, 1))
    ct = ct * ct * (3 - 2 * ct)
    p = (70.0, float(SHY - 14))
    a = math.radians(-70 + 34 * ct)
    for j, (L, wdt) in enumerate(((34, 15), (28, 13))):
        a += math.radians((10 + 40 * ct) * (1 if j else 0))
        q = (p[0] + math.cos(a) * L, p[1] + math.sin(a) * L)
        seg = _capsule(SHW, SHH, p, q, wdt / 2)
        _metal(front, seg, (p[0] + q[0]) / 2, (p[1] + q[1]) / 2, wdt, bias=0.05)
        outline(front, seg)
        m_front |= seg
        _joint_gear(front, p, wdt / 2 + 1, ct * 2.0 + j, m_front)
        p = q
    outline(back, palm)
    return u8(back), u8(front)


def side_arm():
    """Its forearm, coming out of the tear: a brass sleeve and its plates, pistons, and at the wrist a ring of gears and a
    band of cyan glass."""
    w, h = 300, 120
    img = blank(w, h)
    X, Y = grid(w, h)
    sleeve = (np.abs(Y - 62) < 30 - (X / w) * 6) & (X < w - 14)
    val = 0.78 - np.abs(Y - 50) / 34.0
    ramp(img, sleeve, val, BRASS, 1, 7)
    for x in range(30, w - 20, 46):
        img[sleeve & (np.abs(X - x) < 1.0)] = C(BRASS[1])
        img[sleeve & (np.abs(X - x - 2) < 0.6)] = C(BRASS[6])
    for y in (36, 90):
        pist = (np.abs(Y - y) < 2.5) & (X > 20) & (X < w - 40)
        ramp(img, pist, 0.7 - (Y - y) * 0.1, BRASS, 3, 8)
        sleeve |= pist
    img[sleeve & (A.value_noise(w, h, 7, 79) > 0.75)] = C(VERDI[2])
    band = (X > w - 34) & (X < w - 22) & (np.abs(Y - 62) < 32)
    ramp(img, band, 0.5 - (Y - 62) / 64.0, CYAN, 1, 5)
    sleeve |= band
    outline(img, sleeve)
    for gy in (40, 62, 84):
        _joint_gear(img, (w - 12, gy), 9, gy * 0.1, sleeve)
    return u8(img)


def light_needle(k):
    """A needle of light (additive), its glowing eye, a spark flickering at its tip (`k`)."""
    w, h = 28, 9
    img = blank(w, h)
    img[line_mask(w, h, [(3, 4), (24, 4)], 1)] = C("#fff6cc", 255)
    img[line_mask(w, h, [(6, 3), (20, 3)], 1)] = C("#ffd27a", 150)
    img[line_mask(w, h, [(6, 5), (20, 5)], 1)] = C("#ffd27a", 150)
    img[ell_mask(w, h, 5, 4.5, 2.2, 2.2) & ~ell_mask(w, h, 5, 4.5, 1.0, 1.0)] = C("#ffd27a", 230)
    if k % 2 == 0:
        for dx, dy in ((0, 0), (1, 0), (0, 1), (0, -1)):
            img[4 + dy, 25 + dx] = C("#ffffff", 255)
    return u8(img)


PMW, PMH = 150, 200
PM_EYE = (95, 47)


def old_man_profile(breath=0, reach=None):
    """The old man in profile, a clean silhouette backlit by warm light: hunched, a long coat with a high collar, a few
    wisps of hair, a monocle catching the light, his hands behind his back — or (`reach` 0..1) one arm reaching out."""
    w, h = PMW, PMH
    img = blank(w, h)
    b = breath
    coat = poly_mask(w, h, [(28, h), (30, 168), (32, 128), (36, 100 + b), (44, 80 + b), (54, 68 + b), (66, 62 + b), (78, 64 + b),
                            (86, 70 + b), (94, 84 + b), (100, 104), (104, 134), (110, 168), (116, h)])
    collar = poly_mask(w, h, [(56, 70 + b), (60, 52 + b), (68, 56 + b), (74, 66 + b)])
    neck = poly_mask(w, h, [(70, 66 + b), (78, 56), (88, 58), (86, 70 + b)])
    head = ell_mask(w, h, 86, 46, 12.5, 14.5)
    nose = poly_mask(w, h, [(96, 42), (103, 50), (97, 53)])
    brow = poly_mask(w, h, [(84, 36), (99, 38), (97, 42), (84, 41)])
    chin = poly_mask(w, h, [(80, 54), (95, 55), (90, 61), (82, 60)])
    m = coat | collar | neck | head | nose | brow | chin
    hair = np.zeros_like(m)
    for pts, wd in ((((75, 37), (68, 33), (62, 34)), 2), (((78, 34), (71, 28), (64, 28)), 1), (((74, 43), (67, 44), (63, 48)), 2),
                    (((84, 33), (80, 27), (74, 25)), 1)):  # a few wisps, swept back
        hair |= line_mask(w, h, list(pts), wd)
    m |= hair
    if reach is not None:
        u = reach * reach * (3 - 2 * reach)
        sh = (82, 82 + b)
        el = (sh[0] + 14 + 10 * u, sh[1] + 26 - 30 * u)
        wr = (el[0] + 12 + 16 * u, el[1] + 18 - 32 * u)
        m |= _capsule(w, h, sh, el, 6.5) | _capsule(w, h, el, wr, 5.5)
        for k, fa in enumerate((-40, -20, 0, 18)):
            a = math.radians(fa - 30 * u)
            m |= line_mask(w, h, [wr, (wr[0] + math.cos(a) * 11, wr[1] + math.sin(a) * 11)], 2)
    img[m] = C("#0a0910")
    rim_up = m & ~np.roll(m, 1, 0)
    rim_side = m & (~np.roll(m, 1, 1) | ~np.roll(m, -1, 1)) & ~rim_up
    X, Y = grid(w, h)
    img[rim_side & (Y < 175)] = C("#8a5a22")
    img[rim_up] = C("#ffd27a")
    # the monocle: a ring at the eye, its glint, its chain down the coat
    ring = ell_mask(w, h, PM_EYE[0], PM_EYE[1], 3.6, 3.6) & ~ell_mask(w, h, PM_EYE[0], PM_EYE[1], 2.2, 2.2)
    img[ring] = C(BRASS[5])
    img[int(PM_EYE[1]) - 1, int(PM_EYE[0]) - 1] = C("#ffffff")
    for j in range(16):
        t = j / 15
        x = PM_EYE[0] - 4 - 8 * t
        y = PM_EYE[1] + 4 + 46 * t + 6 * math.sin(t * math.pi)
        if j % 2 == 0 and m[int(y), int(x)]:
            img[int(y), int(x)] = C(BRASS[5])
    return u8(img)


def lyse_dead(k):
    """Lyse, the dead star at the centre, as « Après Asterion » left her: a dim red ember of a sphere, no face, a spark
    that quivers on her (`k`)."""
    s = 72
    img = blank(s, s)
    X, Y = grid(s, s)
    c = s / 2
    r = np.hypot(X - c, Y - c)
    m = r <= 22
    val = 0.55 - 0.4 * ((X - c) * 0.6 + (Y - c) * 0.6) / 22 - 0.15 * (r / 22) ** 2
    cols = ["#1a0a10", "#3a1416", "#6a2a22", "#9a4a30", "#c8784a"]
    ramp(img, m, val + 0.05 * (A.value_noise(s, s, 5, 41) - 0.5), cols)
    halo = (r > 22) & (r < 34)
    img[halo, :3] = C("#ff9a6a")[:3]
    img[halo, 3] = np.clip(1 - (r[halo] - 22) / 12, 0, 1) ** 2 * 70
    sx, sy = c - 9, c - 8
    lv = (1.0, 0.6, 0.85, 0.4)[k % 4]
    img[ell_mask(s, s, sx, sy, 1.4, 1.4)] = C("#fff2c0", int(255 * lv))
    for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
        img[int(sy) + dy, int(sx) + dx] = C("#ffd27a", int(160 * lv))
    return u8(img)


# =================================================================== 2.82, longer films: the workshop, the empty rod, the six worlds
def clock_face(r, hands=(305.0, 62.0), seed=0, dim=1.0):
    """A small clock (its brass rim, its face, twelve marks, two hands at the same time as every other one)."""
    s = 2 * r + 3
    img = blank(s, s)
    X, Y = grid(s, s)
    c = s / 2
    d = np.hypot(X - c, Y - c)
    rim = (d <= r) & (d > r - max(2, r // 5))
    face = d <= r - max(2, r // 5)
    ramp(img, rim, (0.6 - 0.4 * ((X - c) + (Y - c)) / (2 * r)) * dim, BRASS, 1, 7)
    tone = ["#d8c8a0", "#e8dcc0", "#c8b890", "#bfc8d0"][seed % 4]
    ramp(img, face, (0.7 - 0.25 * (Y - c) / r) * dim, ["#6a5a48", "#a08c6c", tone], 0, 3)
    for k in range(12):
        a = k * TAU / 12
        if r >= 6:
            img[int(c + math.sin(a) * (r - 3.5)), int(c + math.cos(a) * (r - 3.5))] = C("#2a1a10")
    for ang, ln in ((hands[0], 0.5), (hands[1], 0.75)):
        a = math.radians(ang - 90)
        img[line_mask(s, s, [(c, c), (c + math.cos(a) * r * ln, c + math.sin(a) * r * ln)], 1)] = C("#1a0e08")
    outline(img, d <= r)
    return img


def clock_wall(w=480, h=270, seed=21, dim=0.55, sizes=(5, 18)):
    """A wall of clocks, a thousand of them stopped at the same second (a layer)."""
    img = blank(w, h)
    rng = np.random.default_rng(seed)
    img[..., :3] = C("#1a120c")[:3] * dim
    img[..., 3] = 255
    for i in range(900):
        r = int(rng.integers(sizes[0], sizes[1]))
        x, y = int(rng.integers(0, w)), int(rng.integers(0, h))
        f = clock_face(r, seed=i, dim=dim * float(rng.uniform(0.7, 1.0)))
        fh, fw = f.shape[:2]
        x0, y0 = x - fw // 2, y - fh // 2
        xs, ys = max(0, x0), max(0, y0)
        xe, ye = min(w, x0 + fw), min(h, y0 + fh)
        if xe <= xs or ye <= ys:
            continue
        sub = f[ys - y0:ye - y0, xs - x0:xe - x0]
        a = sub[..., 3:4] / 255.0
        img[ys:ye, xs:xe, :3] = sub[..., :3] * a + img[ys:ye, xs:xe, :3] * (1 - a)
    return u8(img)


def bench(w=480, h=96):
    """The workbench: a dark wooden top edged with brass, its drawers, scattered tools (a layer)."""
    img = blank(w, h)
    X, Y = grid(w, h)
    top = (Y > 6) & (Y < 22)
    body = Y >= 22
    ramp(img, top, 0.6 - (Y - 6) / 30 + 0.1 * np.sin(X * 0.07), ["#2a160c", "#4a2a16", "#6a3e20", "#8a5a30"])
    ramp(img, body, 0.35 - (Y - 22) / 160, ["#100806", "#1e100a", "#2e1a10"])
    img[np.abs(Y - 6.5) < 1.0] = C(BRASS[5])
    img[np.abs(Y - 22) < 1.0] = C(BRASS[3])
    for x in range(30, w, 110):
        dr = body & (np.abs(X - x - 40) < 38) & (Y > 32) & (Y < 70)
        img[dr & edge(dr)] = C(BRASS[3])
        img[ell_mask(w, h, x + 40, 51, 3, 3)] = C(BRASS[5])
    return u8(img)


def blueprint(k):
    """The plans on the bench: Asterion drawn as the balance wheel of a watch — the dial, the rod, chains, the titan's
    outline, measures — white lines on blue paper (`k`: the line being traced glows)."""
    w, h = 180, 120
    img = blank(w, h)
    X, Y = grid(w, h)
    paper = (X > 2) & (X < w - 3) & (Y > 2) & (Y < h - 3)
    img[paper] = C("#1a3c6a")
    img[paper & ((X.astype(int) % 10 == 0) | (Y.astype(int) % 10 == 0))] = C("#2a4e80")
    ink = C("#dff0ff")
    dial = ell_mask(w, h, 90, 20, 30, 10) & ~ell_mask(w, h, 90, 20, 28.5, 8.6)
    img[dial] = ink
    img[line_mask(w, h, [(90, 30), (90, 52)], 1)] = ink
    img[line_mask(w, h, [(60, 52), (120, 52)], 1)] = ink
    titan = compose_titan(0)[..., 3] > 0
    small = np.array(Image.fromarray(titan.astype(np.uint8) * 255).resize((80, 64), Image.NEAREST)) > 0
    sil = np.zeros((h, w), bool)
    sil[54:118, 50:130] = small
    img[edge(sil)] = ink
    img[line_mask(w, h, [(62, 52), (70, 66)], 1) | line_mask(w, h, [(118, 52), (110, 66)], 1)] = ink
    for y in (20, 52, 86):  # measures
        img[line_mask(w, h, [(150, y - 3), (150, y + 3)], 1) | line_mask(w, h, [(146, y), (170, y)], 1)] = C("#9fc0e0")
    img[line_mask(w, h, [(20, 100), (40, 80), (60, 96)], 1)] = C("#9fc0e0")
    if k % 2:
        img[ell_mask(w, h, 90, 20, 30, 10) & ~ell_mask(w, h, 90, 20, 28.5, 8.6) & (X > 100)] = C("#ffffff")
    outline(img, paper, "#0a1020")
    return u8(img)


def brass_bits(kind):
    """Shards of brass drifting from the broken rod: a broken gear, a screw, a plate, a spring (24 px)."""
    s = 24
    img = blank(s, s)
    X, Y = grid(s, s)
    c = s / 2
    if kind == "rouage":
        g = gear(9, 10, 3, 0.4, td=2, glass=False, seed=8)
        cut = poly_mask(g.shape[1], g.shape[0], [(0, 0), (g.shape[1], 0), (g.shape[1], g.shape[0] * 0.45), (0, g.shape[0] * 0.7)])
        g = g.copy()
        g[~cut] = 0
        gh, gw = g.shape[:2]
        img[(s - gh) // 2:(s - gh) // 2 + gh, (s - gw) // 2:(s - gw) // 2 + gw] = g
        return u8(img)
    if kind == "vis":
        m = (np.abs(X - c) < 2) & (np.abs(Y - c) < 8)
        head = ell_mask(s, s, c, c - 8, 4, 2)
        m |= head
        ramp(img, m, 0.6 - (X - c) / 6, BRASS, 2, 8)
        img[m & (np.abs(Y - c) < 8) & ((Y.astype(int) % 3) == 0)] = C(BRASS[2])
    elif kind == "plaque":
        m = poly_mask(s, s, [(4, 8), (18, 4), (21, 15), (7, 20)])
        ramp(img, m, 0.65 - (X + Y - 2 * c) / 30, BRASS, 1, 7)
        img[ell_mask(s, s, 8, 9, 1, 1) | ell_mask(s, s, 16, 7, 1, 1)] = C(BRASS[7])
    else:
        m = np.zeros((s, s), bool)
        for k in range(5):
            m |= ell_mask(s, s, c, 5 + k * 3.2, 5, 1.8) & ~ell_mask(s, s, c, 5 + k * 3.2, 3.6, 0.8)
        ramp(img, m, 0.6 - (X - c) / 10, BRASS, 2, 8)
    outline(img, m)
    return u8(img)


# --- the six worlds seen through the glass, each with its own law of time, and the shadow that keeps it
def world_backdrop(kind, w=480, h=270):
    """A world of the dial seen from inside (a layer): its sky, its ground, its shapes."""
    i = [b[0] for b in BIOMES].index(kind)
    _, dk, base, light, lamp = BIOMES[i]
    X, Y = grid(w, h)
    n = A.value_noise(w, h, 24, 500 + i)
    n2 = A.value_noise(w, h, 8, 520 + i)
    img = blank(w, h)
    sky = Y / h
    ramp(img, np.ones((h, w), bool), 0.15 + 0.6 * sky * 0 + 0.25 * n + 0.2 * (1 - sky),
         ["#06060c", dk, base], 0, 3)
    ground = Y > h * 0.72 + 10 * np.sin(X * 0.03 + i)
    ramp(img, ground, 0.3 + 0.4 * n2 - 0.2 * (Y - h * 0.72) / (h * 0.3), ["#06060c", dk, base, light], 0, 4)
    rng = np.random.default_rng(540 + i)
    if kind == "serre":  # greenhouse arches
        for x in range(0, w, 80):
            img[line_mask(w, h, [(x, h * 0.75), (x + 20, h * 0.2), (x + 60, h * 0.2), (x + 80, h * 0.75)], 2)] = C(light, 140)
    elif kind == "glacier":  # ice spires
        for _ in range(9):
            x = float(rng.uniform(0, w))
            hh = float(rng.uniform(60, 160))
            sp = poly_mask(w, h, [(x - 14, h * 0.8), (x, h * 0.8 - hh), (x + 14, h * 0.8)])
            ramp(img, sp, 0.4 + 0.5 * (x - X) / 28, [dk, base, light, "#ffffff"], 0, 4)
    elif kind == "forge":  # rivers of lava, chimneys
        lava = ground & (np.abs(np.sin(X * 0.05 + n * 6)) < 0.18)
        img[lava] = C(light)
        for x in (60, 200, 380):
            ch = (np.abs(X - x) < 12) & (Y > h * 0.25) & (Y < h * 0.8)
            ramp(img, ch, 0.3 + (X - x) / 40, ["#140806", dk, base], 0, 3)
    elif kind == "ruines":  # broken colonnades
        for x in range(30, w, 70):
            top = h * 0.3 + float(rng.uniform(0, 60))
            col = (np.abs(X - x) < 9) & (Y > top) & (Y < h * 0.8)
            ramp(img, col, 0.45 + (x - X) / 30, ["#1a0e2a", dk, base, light], 0, 4)
    elif kind == "archives":  # shelves to the ceiling
        for x in range(0, w, 60):
            shelf = (X > x + 4) & (X < x + 54) & (Y < h * 0.82)
            img[shelf] = C(dk)
            for y in range(10, int(h * 0.82), 18):
                img[shelf & (np.abs(Y - y) < 1.0)] = C(base)
                bk = shelf & (Y > y - 14) & (Y < y) & (((X.astype(int) * 7 + y) % 5) < 3)
                img[bk] = C(["#5a5e6c", "#7a7e8a", "#3e4250"][(x // 60 + y) % 3])
    else:  # the central dial: a floor of rings and marks, columns of light
        dial = ell_mask(w, h, w / 2, h * 0.86, w * 0.48, h * 0.16)
        img[dial] = C(base)
        rr = np.hypot((X - w / 2) / (w * 0.48), (Y - h * 0.86) / (h * 0.16))
        img[dial & (np.abs((rr * 5) % 1 - 0.5) < 0.06)] = C(dk)
        for x in (90, 240, 390):
            beam = (np.abs(X - x) < 16 - Y * 0.02) & (Y < h * 0.8)
            img[beam] = img[beam] * 0.6 + C(lamp) * 0.4
    img[..., 3] = 255
    return u8(img)


def vines(k, n=8):
    """Vines growing too fast (`k` of `n`): stems spiralling up from the ground, leaves popping out, flowers at the end."""
    w, h = 140, 180
    img = blank(w, h)
    g = (k + 1) / n
    rng = np.random.default_rng(611)
    for v in range(4):
        x0 = 14 + v * 34 + float(rng.uniform(-6, 6))
        pts = []
        for j in range(int(40 * g) + 1):
            t = j / 40
            pts.append((x0 + math.sin(t * 9 + v) * (8 + 10 * t), h - 2 - t * (h - 20)))
        if len(pts) > 1:
            img[line_mask(w, h, pts, 2)] = C("#2a7a3a")
            img[line_mask(w, h, [(p[0] - 1, p[1]) for p in pts], 1)] = C("#5fd07a")
        for j in range(3, len(pts), 6):
            x, y = pts[j]
            side = 1 if (j // 6) % 2 else -1
            leaf = poly_mask(w, h, [(x, y), (x + side * 9, y - 5), (x + side * 5, y + 2)])
            img[leaf] = C("#4ccf6a")
            img[edge(leaf)] = C("#1a5a30")
        if g >= 0.99 and pts:
            x, y = pts[-1]
            img[ell_mask(w, h, x, y, 4, 4)] = C("#ffb0e0")
            img[ell_mask(w, h, x, y, 1.5, 1.5)] = C("#fff2c0")
    return u8(img)


def snowflake(k):
    """A big snowflake, falling in slow motion (it glints with `k`)."""
    s = 20
    img = blank(s, s)
    c = s / 2
    for i in range(6):
        a = i * TAU / 6
        img[line_mask(s, s, [(c, c), (c + math.cos(a) * 8, c + math.sin(a) * 8)], 1)] = C("#e6fffb", 230)
        bx, by = c + math.cos(a) * 5, c + math.sin(a) * 5
        for d in (-0.8, 0.8):
            img[line_mask(s, s, [(bx, by), (bx + math.cos(a + d) * 3, by + math.sin(a + d) * 3)], 1)] = C("#bff6ff", 200)
    if k % 2:
        img[int(c), int(c)] = C("#ffffff")
    return u8(img)


def anvil(k):
    """The forge's anvil and its hammer, the same blow struck again and again (`k` of 6: raised, falling, striking,
    sparks, then pulled back to where it was by a hand of time)."""
    w, h = 120, 100
    img = blank(w, h)
    X, Y = grid(w, h)
    base = poly_mask(w, h, [(30, 98), (40, 78), (80, 78), (90, 98)]) | poly_mask(w, h, [(20, 66), (100, 66), (88, 78), (32, 78)])
    ramp(img, base, 0.5 - (Y - 66) / 60 - (X - 60) / 200, ["#141010", "#2a2226", "#4a3e44", "#6a5e64"], 0, 4)
    hot = poly_mask(w, h, [(46, 66), (74, 66), (72, 62), (48, 62)])
    img[hot] = C("#ffd070" if k in (2, 3) else "#e0701e")
    ang = [-70, -40, 0, 0, -30, -70][k % 6]
    a = math.radians(ang)
    pivot = (96, 40)
    tip = (pivot[0] - math.cos(a) * 40, pivot[1] + math.sin(a) * 40 * -1 + 20 * (ang == 0))
    img[line_mask(w, h, [pivot, tip], 3)] = C("#3a2a20")
    head = ell_mask(w, h, tip[0], tip[1], 8, 6)
    ramp(img, head, 0.5 - (Y - tip[1]) / 12, ["#1a1414", "#3a3034", "#6a5e64"], 0, 3)
    if k in (2, 3):
        rng = np.random.default_rng(80 + k)
        for _ in range(16 if k == 2 else 10):
            ex, ey = 60 + float(rng.uniform(-30, 30)), 62 - float(rng.uniform(0, 30 if k == 3 else 14))
            img[int(ey), int(ex)] = C("#ffe6a0" if rng.random() < 0.5 else "#ff8a2a")
    if k == 5:
        img[line_mask(w, h, [(pivot[0], pivot[1]), (tip[0], tip[1])], 1)] = C(CYAN[4])
    outline(img, base | head)
    return u8(img)


def page(k):
    """A page torn from the archives, frozen in the air (grey-silver, a glint `k`)."""
    w, h = 22, 18
    img = blank(w, h)
    m = poly_mask(w, h, [(2, 4), (18, 1), (20, 14), (4, 17)])
    X, Y = grid(w, h)
    ramp(img, m, 0.7 - (X + Y) / 60, ["#6a6e7a", "#b8bcc8", "#e6eaf2"], 0, 3)
    for y in (6, 9, 12):
        img[m & (np.abs(Y - y - (X - 2) * -0.15) < 0.5) & (X > 5) & (X < 16)] = C("#4a4e5c")
    if k % 2:
        img[3, 17] = C("#ffffff")
    outline(img, m, "#2a2e3a")
    return u8(img)


# =================================================================== 2.82, after review: the guardians of the six worlds, half there
SHADE = ["#030306", "#07070d", "#0c0c16", "#13131f", "#1b1b2a"]
GW2, GH2 = 150, 180
GUARD_FRAMES = 12


def _shade_part(img, m, top=None, bot=None, bias=0.0):
    """A dark part with its own volume: a vertical gradient and a lighter core (several parts read as one body with
    inner contours)."""
    h, w = m.shape
    if not m.any():
        return
    X, Y = grid(w, h)
    ys = np.where(m.any(1))[0]
    top = ys.min() if top is None else top
    bot = ys.max() if bot is None else bot
    core = m.copy()
    depth = np.zeros((h, w), np.float32)
    for _ in range(4):
        core = core & np.roll(core, 1, 0) & np.roll(core, -1, 0) & np.roll(core, 1, 1) & np.roll(core, -1, 1)
        depth += core
    val = 0.5 + bias - 0.4 * (Y - top) / max(1, bot - top) + 0.06 * depth / 4
    ramp(img, m, val, SHADE)
    img[edge(m) & ~np.roll(m, 1, 0)] = C(SHADE[4])


def _gear_mask(w, h, cx, cy, R, teeth, turn):
    X, Y = grid(w, h)
    r = np.hypot(X - cx, Y - cy)
    th = np.arctan2(Y - cy, X - cx) - turn
    tooth = ((th * teeth / TAU) % 1.0) < 0.5
    return ((r <= R - 2) | ((r <= R) & tooth)) & (r > R * 0.35)


def _guard_parts(kind, w, h, b, f):
    """The parts of a guardian at breath `b` (pixels) and frame `f`: [(mask, role)] back to front (role: body, detail,
    glow) and its eyes. Shapes only: the renderer gives them volume, light and the shimmer of a half-materialised thing."""
    P_ = []
    turn = f * TAU / GUARD_FRAMES / 3
    cap = lambda p, q, r: _capsule(w, h, p, q, r)  # noqa: E731
    if kind == "serre":  # the gardener: a body of coiled springs, roots for legs, shears for a hand, branches for antlers
        P_ += [(cap((60, 120), (42, 176), 6) | cap((90, 120), (110, 176), 6), "body")]
        P_ += [(line_mask(w, h, [(42, 176), (30, 178)], 2) | line_mask(w, h, [(110, 176), (124, 178)], 2), "body")]
        torso = poly_mask(w, h, [(52, 66 + b), (98, 66 + b), (104, 128), (46, 128)])
        P_ += [(torso, "body")]
        P_ += [(ell_mask(w, h, 75, 70 + 8 * k + b, 24 - k, 3) & ~ell_mask(w, h, 75, 70 + 8 * k + b, 22 - k, 1.5), "detail") for k in range(7)]
        P_ += [(cap((52, 74 + b), (24, 100 + b), 4) | line_mask(w, h, [(24, 100 + b), (14, 118 + b), (22, 128 + b), (30, 120 + b)], 2), "body")]
        P_ += [(cap((98, 74 + b), (120, 98 + b), 4) | poly_mask(w, h, [(118, 94 + b), (140, 84 + b), (124, 100 + b)])
                | poly_mask(w, h, [(118, 100 + b), (142, 108 + b), (122, 104 + b)]), "body")]
        head = ell_mask(w, h, 75, 46 + b, 15, 19)
        P_ += [(head, "body")]
        for side in (-1, 1):
            br = line_mask(w, h, [(75 + side * 8, 30 + b), (75 + side * 22, 12 + b), (75 + side * 30, 4 + b)], 3)
            br |= line_mask(w, h, [(75 + side * 22, 12 + b), (75 + side * 16, 2 + b)], 2)
            br |= ell_mask(w, h, 75 + side * 30, 4 + b, 4, 2.5) | ell_mask(w, h, 75 + side * 16, 2 + b, 3, 2)
            P_ += [(br, "body")]
        P_ += [(ell_mask(w, h, 75, 54 + b, 6, 6) & ~ell_mask(w, h, 75, 54 + b, 4.5, 4.5), "glow")]
        eyes = [(69, 44 + b), (81, 44 + b)]
    elif kind == "glacier":  # the frost keeper: a hunched mass, horns of ice, an hourglass of frozen sand in its chest
        P_ += [(cap((56, 140), (50, 176), 9) | cap((96, 140), (104, 176), 9), "body")]
        torso = ell_mask(w, h, 76, 112 + b, 40, 42)
        P_ += [(torso, "body")]
        for side in (-1, 1):
            P_ += [(cap((76 + side * 34, 90 + b), (76 + side * 48, 160), 10), "body")]
            P_ += [(_gear_mask(w, h, 76 + side * 34, 88 + b, 10, 10, turn * side), "detail")]
            for k in range(3):
                x = 76 + side * (22 + 8 * k)
                P_ += [(poly_mask(w, h, [(x - 4, 76 + b), (x + 4, 76 + b), (x + side * 3, 58 - 6 * k + b)]), "body")]
        head = ell_mask(w, h, 76, 72 + b, 13, 12)
        P_ += [(head, "body")]
        for side in (-1, 1):
            P_ += [(line_mask(w, h, [(76 + side * 9, 64 + b), (76 + side * 22, 48 + b), (76 + side * 26, 24 + b)], 4), "body")]
        hg = poly_mask(w, h, [(64, 98 + b), (88, 98 + b), (77, 112 + b)]) | poly_mask(w, h, [(64, 126 + b), (88, 126 + b), (77, 112 + b)])
        P_ += [(hg & ~poly_mask(w, h, [(67, 100 + b), (85, 100 + b), (77, 110 + b)]) & ~poly_mask(w, h, [(67, 124 + b), (85, 124 + b), (77, 114 + b)]), "detail")]
        P_ += [(poly_mask(w, h, [(70, 118 + b), (84, 118 + b), (84, 124 + b), (70, 124 + b)]), "glow")]
        eyes = [(71, 72 + b), (81, 72 + b)]
    elif kind == "forge":  # the looping smith: a furnace for a body, a hammer for an arm, an escape wheel glowing in its chest
        P_ += [(cap((58, 140), (58, 176), 7) | cap((92, 140), (92, 176), 7), "body")]
        P_ += [(ell_mask(w, h, 58, 152, 9, 3) | ell_mask(w, h, 92, 152, 9, 3), "detail")]
        P_ += [(poly_mask(w, h, [(104, 50 + b), (116, 50 + b), (118, 96), (102, 96)]), "body")]  # its chimney
        body = poly_mask(w, h, [(40, 70 + b), (110, 70 + b), (116, 140), (34, 140)])
        P_ += [(body, "body")]
        for k in range(4):
            P_ += [(poly_mask(w, h, [(52, 120 + 4 * k), (98, 120 + 4 * k), (98, 121.5 + 4 * k), (52, 121.5 + 4 * k)]), "glow")]
        P_ += [(_gear_mask(w, h, 75, 96 + b, 13, 12, -turn * 2), "glow")]
        P_ += [(cap((112, 82 + b), (132, 112 + b), 6), "body")]
        P_ += [(cap((132, 112 + b), (132, 140 + b), 3) | poly_mask(w, h, [(120, 138 + b), (146, 138 + b), (146, 156 + b), (120, 156 + b)]), "body")]
        P_ += [(cap((38, 82 + b), (20, 116 + b), 6) | line_mask(w, h, [(20, 116 + b), (10, 132 + b)], 2) | line_mask(w, h, [(20, 116 + b), (24, 134 + b)], 2), "body")]
        head = poly_mask(w, h, [(60, 44 + b), (90, 44 + b), (94, 72 + b), (56, 72 + b)])
        P_ += [(head, "body")]
        P_ += [(poly_mask(w, h, [(62, 56 + b), (88, 56 + b), (88, 59 + b), (62, 59 + b)]), "glow")]
        eyes = [(68, 57 + b), (82, 57 + b)]
    elif kind == "ruines":  # the pilgrim in reverse: a hooded robe, a halo of clock hands, a staff with its pendulum, stones that float
        robe = poly_mask(w, h, [(50, 70 + b), (100, 70 + b), (114, 178), (36, 178)])
        P_ += [(robe, "body")]
        P_ += [(line_mask(w, h, [(75, 80 + b), (66, 176)], 1) | line_mask(w, h, [(75, 80 + b), (86, 176)], 1), "glow")]
        P_ += [(cap((100, 84 + b), (118, 112 + b), 5), "body")]
        P_ += [(line_mask(w, h, [(122, 16), (122, 176)], 3), "body")]
        sw = math.sin(f * TAU / GUARD_FRAMES) * 6
        P_ += [(line_mask(w, h, [(122, 20), (122 + sw, 52)], 1) | ell_mask(w, h, 122 + sw, 56, 4, 4), "detail")]
        hood = ell_mask(w, h, 75, 56 + b, 16, 19) | poly_mask(w, h, [(58, 60 + b), (92, 60 + b), (100, 78 + b), (50, 78 + b)])
        P_ += [(hood, "body")]
        P_ += [(ell_mask(w, h, 75, 60 + b, 9, 11), "detail")]
        halo = ell_mask(w, h, 75, 26 + b, 22, 7) & ~ell_mask(w, h, 75, 26 + b, 19, 5)
        a = -f * TAU / GUARD_FRAMES
        halo |= line_mask(w, h, [(75, 26 + b), (75 + math.cos(a) * 16, 26 + b + math.sin(a) * 5)], 1)
        P_ += [(halo, "glow")]
        for k, (x, y) in enumerate(((24, 120), (16, 92), (34, 70), (132, 150))):
            y2 = y - (f + 3 * k) % GUARD_FRAMES * 0.5
            P_ += [(poly_mask(w, h, [(x - 5, y2), (x + 4, y2 - 3), (x + 6, y2 + 4), (x - 3, y2 + 6)]), "body")]
        eyes = [(71, 60 + b), (79, 60 + b)]
    elif kind == "archives":  # the librarian: a tall ribbed body, a lantern clock for a head, six thin arms holding books
        P_ += [(cap((66, 130), (60, 176), 3) | cap((84, 130), (90, 176), 3), "body")]
        body = poly_mask(w, h, [(62, 58 + b), (88, 58 + b), (94, 136), (56, 136)])
        P_ += [(body, "body")]
        P_ += [(line_mask(w, h, [(x, 62 + b), (x + (x - 75) * 0.2, 134)], 1), "detail") for x in (66, 72, 78, 84)]
        for j in range(3):
            for side in (-1, 1):
                y0 = 70 + 18 * j + b
                ang = math.radians(-30 + 25 * j + 10 * math.sin((f + 4 * j) * TAU / GUARD_FRAMES)) if side > 0 else math.radians(210 - 25 * j - 10 * math.sin((f + 2 * j) * TAU / GUARD_FRAMES))
                q = (75 + side * 12 + math.cos(ang) * 34, y0 + math.sin(ang) * 34)
                P_ += [(cap((75 + side * 12, y0), q, 2), "body")]
                P_ += [(poly_mask(w, h, [(q[0] - 6, q[1] - 4), (q[0] + 6, q[1] - 4), (q[0] + 6, q[1] + 4), (q[0] - 6, q[1] + 4)]), "body")]
        head = poly_mask(w, h, [(63, 28 + b), (87, 28 + b), (87, 56 + b), (63, 56 + b)]) | poly_mask(w, h, [(60, 28 + b), (90, 28 + b), (75, 16 + b)])
        P_ += [(head, "body")]
        P_ += [(ell_mask(w, h, 75, 42 + b, 9, 9) & ~ell_mask(w, h, 75, 42 + b, 7.5, 7.5), "glow")]
        eyes = [(71, 42 + b), (79, 42 + b)]
    else:  # the sentinel of the dial: a face that is a clock, a balance wheel for wings, two hands of a clock for spears
        wheel = ell_mask(w, h, 75, 70 + b, 52, 52) & ~ell_mask(w, h, 75, 70 + b, 48, 48)
        for k in range(4):
            a = k * TAU / 4 + math.sin(f * TAU / GUARD_FRAMES) * 0.5
            wheel |= line_mask(w, h, [(75, 70 + b), (75 + math.cos(a) * 50, 70 + b + math.sin(a) * 50)], 2)
        P_ += [(wheel, "detail")]
        P_ += [(poly_mask(w, h, [(54, 92 + b), (96, 92 + b), (110, 178), (40, 178)]), "body")]
        P_ += [(poly_mask(w, h, [(58, 86 + b), (92, 86 + b), (98, 132), (52, 132)]), "body")]
        for side in (-1, 1):
            P_ += [(line_mask(w, h, [(75 + side * 30, 176), (75 + side * 40, 20)], 3) | poly_mask(w, h, [(75 + side * 40 - 5, 26), (75 + side * 40 + 5, 26), (75 + side * 41, 10)]),
                    "body")]
        face = ell_mask(w, h, 75, 66 + b, 17, 17)
        P_ += [(face, "body")]
        marks = np.zeros((h, w), bool)
        for k in range(12):
            a = k * TAU / 12
            marks |= ell_mask(w, h, 75 + math.cos(a) * 13, 66 + b + math.sin(a) * 13, 0.9, 0.9)
        a = f * TAU / GUARD_FRAMES
        marks |= line_mask(w, h, [(75, 66 + b), (75 + math.cos(a) * 10, 66 + b + math.sin(a) * 10)], 1)
        P_ += [(marks, "glow")]
        eyes = [(70, 63 + b), (80, 63 + b)]
    return P_, eyes


def guardian2(kind, f):
    """A guardian of the dial (`kind`), frame `f` of its loop: a silhouette with volume (inner shading, the contours of
    its parts), rim-lit by its world, its seams and eyes glowing — and half there: rippling like heat over time, its
    colours split at its edges, flickering."""
    i = [bb[0] for bb in BIOMES].index(kind)
    lamp = C(BIOMES[i][4])
    w, h = GW2, GH2
    b = int(round(math.sin(f * TAU / GUARD_FRAMES) * 1.5))
    parts, eyes = _guard_parts(kind, w, h, b, f)
    img = blank(w, h)
    allm = np.zeros((h, w), bool)
    for m, role in parts:
        if role == "body":
            _shade_part(img, m, bias=0.05 * (m & allm).any())
            img[edge(m) & allm & ~np.roll(m, 1, 0)] = C(SHADE[0])  # where it overlaps: a contour
            allm |= m
    for m, role in parts:
        if role == "detail":
            img[m & ~edge(m)] = C(SHADE[3])
            img[edge(m)] = C(SHADE[4])
            allm |= m
    rim_up = allm & ~np.roll(allm, 1, 0)
    rim_side = allm & (~np.roll(allm, 1, 1) | ~np.roll(allm, -1, 1)) & ~rim_up
    img[rim_side] = np.concatenate([np.broadcast_to(lamp[:3] * 0.6, (int(rim_side.sum()), 3)), np.full((int(rim_side.sum()), 1), 255.0)], 1)
    img[rim_up] = np.concatenate([np.broadcast_to(lamp[:3], (int(rim_up.sum()), 3)), np.full((int(rim_up.sum()), 1), 255.0)], 1)
    pulse = 0.55 + 0.45 * math.sin(f * TAU / GUARD_FRAMES * 2)
    for m, role in parts:
        if role == "glow":
            img[m] = np.concatenate([np.broadcast_to(lamp[:3] * (0.6 + 0.4 * pulse), (int(m.sum()), 3)), np.full((int(m.sum()), 1), 255.0)], 1)
            allm |= m
    for ex, ey in eyes:
        img[ell_mask(w, h, ex, ey, 1.8, 1.3)] = C("#ffffff")
        img[ell_mask(w, h, ex, ey, 2.8, 2.0) & ~ell_mask(w, h, ex, ey, 1.8, 1.3)] = lamp
    # half there: each row slides with a ripple of time, the colours split at the edges
    out = np.zeros_like(img)
    for y in range(h):
        dx = int(round(1.6 * math.sin(y * 0.19 + f * TAU / GUARD_FRAMES) + 0.8 * math.sin(y * 0.07 - f * TAU / GUARD_FRAMES * 2)))
        out[y] = np.roll(img[y], dx, 0)
    a = out[..., 3] > 0
    sp = 1 + (f % 3 == 0)
    red = np.roll(a, -sp, 1) & ~a
    cyan = np.roll(a, sp, 1) & ~a
    out[red] = C("#ff3a5a", 120)
    out[cyan] = C("#3af0ff", 120)
    if f % 5 == 2:  # a band where it is not quite there
        y0 = 30 + (f * 37) % (h - 60)
        band = (np.arange(h)[:, None] >= y0) & (np.arange(h)[:, None] < y0 + 4)
        out[band[:, 0], :, 3] *= 0.35
    return u8(out)


# =================================================================== 2.82: the clockmaker at work (both films)
HW2, HH2 = 280, 210


def tinker(f):
    """The old man at his bench, mad and brilliant: his silhouette (front, backlit), his own two hands and four arms of
    brass bolted to his shoulders, each with its tool — tweezers, a loupe, a screwdriver, a torch spitting sparks —
    all moving at once, jerkily (`f` of 10)."""
    w, h = HW2, HH2
    img = blank(w, h)
    rng = np.random.default_rng(900 + f)
    man = old_man(f % 2)
    mh, mw = man.shape[:2]
    x0, y0 = (w - mw) // 2, h - mh
    sub = img[y0:y0 + mh, x0:x0 + mw]
    sub[man[..., 3] > 0] = man[man[..., 3] > 0]
    sh = [(x0 + 30, y0 + 116), (x0 + 126, y0 + 116), (x0 + 44, y0 + 108), (x0 + 112, y0 + 108)]
    tools = ["pince", "loupe", "tournevis", "chalumeau"]
    jerk = [(math.sin(f * 2.1 + k) * 0.5 + float(rng.uniform(-0.25, 0.25))) for k in range(6)]
    for k, (sp, tool) in enumerate(zip(sh, tools)):
        side = -1 if sp[0] < w / 2 else 1
        a1 = math.radians((-150 if side < 0 else -30) + 35 * jerk[k])
        el = (sp[0] + math.cos(a1) * 42, sp[1] + math.sin(a1) * 30)
        a2 = math.radians((60 if side < 0 else 120) + 50 * jerk[(k + 2) % 6])
        hd = (el[0] + math.cos(a2) * 38, el[1] + math.sin(a2) * 38)
        arm = _capsule(w, h, sp, el, 3.2) | _capsule(w, h, el, hd, 2.6)
        _metal(img, arm, (sp[0] + hd[0]) / 2, (sp[1] + hd[1]) / 2, 6, 1, 7)
        img[ell_mask(w, h, el[0], el[1], 3.5, 3.5)] = C(VERDI[2])
        img[ell_mask(w, h, el[0], el[1], 1.2, 1.2)] = C(CYAN[4])
        tx, ty = hd
        if tool == "pince":
            img[line_mask(w, h, [(tx, ty), (tx + 9, ty + 12)], 1) | line_mask(w, h, [(tx + 2, ty), (tx + 12, ty + 10)], 1)] = C(BRASS[6])
        elif tool == "loupe":
            lo = ell_mask(w, h, tx, ty + 6, 7, 7) & ~ell_mask(w, h, tx, ty + 6, 5, 5)
            img[lo] = C(BRASS[5])
            img[ell_mask(w, h, tx, ty + 6, 5, 5)] = C(CYAN[3], 150)
            img[ell_mask(w, h, tx - 2, ty + 4, 1.2, 1.2)] = C("#ffffff")
        elif tool == "tournevis":
            img[line_mask(w, h, [(tx, ty), (tx + 4, ty + 16)], 2)] = C("#b8bcc8")
            img[ell_mask(w, h, tx, ty, 3, 3)] = C("#8a3a2a")
        else:
            img[line_mask(w, h, [(tx, ty), (tx - 6, ty + 14)], 2)] = C(BRASS[3])
            if f % 2 == 0:
                for _ in range(8):
                    sx, sy = tx - 6 + float(rng.uniform(-8, 8)), ty + 14 + float(rng.uniform(-6, 8))
                    if 0 <= sx < w and 0 <= sy < h:
                        img[int(sy), int(sx)] = C("#fff2c0" if rng.random() < 0.5 else CYAN[5])
        outline(img, arm, INK, 200)
    # his own hands, quick and thin, at the work
    for k, base in enumerate(((x0 + 58, y0 + 150), (x0 + 98, y0 + 150))):
        a = math.radians(-90 + (k * 2 - 1) * 30 + 40 * jerk[4 + k])
        hd = (base[0] + math.cos(a) * 26, base[1] + math.sin(a) * 22)
        m = _capsule(w, h, base, hd, 3.5)
        for fa in (-30, 0, 30):
            aa = a + math.radians(fa)
            m |= line_mask(w, h, [hd, (hd[0] + math.cos(aa) * 9, hd[1] + math.sin(aa) * 9)], 1)
        img[m] = C("#0b0a14")
        img[m & ~np.roll(m, 1, 0)] = C("#c08a32")
    return u8(img)


def escapement(f):
    """A giant escapement being regulated: an escape wheel with hooked teeth that steps on, its anchor rocking (`f` of 8)."""
    w, h = 150, 130
    img = blank(w, h)
    X, Y = grid(w, h)
    cx, cy, R = 75, 82, 40
    r = np.hypot(X - cx, Y - cy)
    th = np.arctan2(Y - cy, X - cx) - (f // 2) * TAU / 15
    u = (th * 15 / TAU) % 1.0
    teeth = (r <= R) & (r > R - 9) & (u < 0.35 + 0.5 * (R - r) / 9)
    wheel = ((r <= R - 8) & (r > R - 13)) | teeth | (r < 6)
    for k in range(5):
        a = k * TAU / 5 - (f // 2) * TAU / 15
        wheel |= line_mask(w, h, [(cx, cy), (cx + math.cos(a) * (R - 10), cy + math.sin(a) * (R - 10))], 2)
    _metal(img, wheel, cx, cy, R, 1, 8)
    outline(img, wheel)
    rock = (1 if f % 2 == 0 else -1) * 0.22
    piv = (cx, cy - R - 16)
    arms = np.zeros((h, w), bool)
    for side in (-1, 1):
        a = math.pi / 2 + side * 0.62 + rock
        q = (piv[0] + math.cos(a) * 38, piv[1] + math.sin(a) * 38)
        arms |= _capsule(w, h, piv, q, 2.6)
        img[ell_mask(w, h, q[0], q[1], 2.4, 2.4)] = C("#e0405a")  # the pallet stones, ruby
    arms |= _capsule(w, h, piv, (piv[0] + math.sin(rock) * 6, piv[1] - 14), 2)
    _metal(img, arms, piv[0], piv[1], 20, 2, 8)
    img[ell_mask(w, h, piv[0], piv[1], 3, 3)] = C(CYAN[4])
    return u8(img)


def hairspring(f):
    """A hairspring bent by hand: a spiral of fine brass wire, its outer end pulled out of shape (`f` of 6), glinting."""
    w, h = 90, 90
    img = blank(w, h)
    pull = math.sin(f * TAU / 6) * 10
    pts = []
    for j in range(240):
        t = j / 240 * 5 * TAU
        r = 3 + t * 1.15
        k = max(0.0, (r - 24) / 12)
        pts.append((45 + math.cos(t) * r + pull * k, 45 + math.sin(t) * r * 0.9 - pull * k * 0.4))
    img[line_mask(w, h, pts, 1)] = C(BRASS[5])
    gl = int((f * 40) % len(pts))
    for p in pts[gl:gl + 6]:
        img[int(p[1]) % h, int(p[0]) % w] = C("#fff6cc")
    img[ell_mask(w, h, 45, 45, 3, 3)] = C(CYAN[4])
    return u8(img)


def galaxy_watch(f):
    """A pocket watch opened onto a galaxy in miniature: its lid thrown back, inside a spiral of stars turning (`f` of 8)."""
    w, h = 96, 110
    img = blank(w, h)
    X, Y = grid(w, h)
    lid = ell_mask(w, h, 48, 28, 30, 22) & ~ell_mask(w, h, 48, 28, 26, 18)
    ramp(img, lid, 0.6 - (Y - 6) / 44, BRASS, 2, 8)
    img[ell_mask(w, h, 48, 28, 26, 18)] = C("#2a1a10")
    case = ell_mask(w, h, 48, 74, 32, 32)
    ramp(img, case & ~ell_mask(w, h, 48, 74, 27, 27), 0.6 - (Y - 42) / 64 - (X - 48) / 128, BRASS, 1, 8)
    inside = ell_mask(w, h, 48, 74, 27, 27)
    img[inside] = C("#05030c")
    rng = np.random.default_rng(47)
    rot = f * TAU / 8 / 2
    for k in range(150):
        arm = k % 2
        t = float(rng.uniform(0, 1))
        a = t * 3.4 + arm * math.pi + rot + float(rng.normal(0, 0.18))
        rr = 3 + t * 22
        x, y = 48 + math.cos(a) * rr, 74 + math.sin(a) * rr * 0.8
        if inside[int(y), int(x)]:
            img[int(y), int(x)] = C(["#ffffff", "#bff6ff", "#f0c8ff", "#ffd27a"][k % 4], int(150 + 100 * (1 - t)))
    img[ell_mask(w, h, 48, 74, 3, 2.5)] = C("#fff2c0")
    img[ell_mask(w, h, 48, 42, 4, 3)] = C(BRASS[6])
    outline(img, lid | case)
    return u8(img)


def socket(f):
    """The machine Asterion's old balance wheel is plugged into: a brass cradle, cables, pulses of cyan running along them (`f` of 6)."""
    w, h = 130, 80
    img = blank(w, h)
    X, Y = grid(w, h)
    base = poly_mask(w, h, [(10, 78), (16, 46), (114, 46), (120, 78)])
    ramp(img, base, 0.6 - (Y - 46) / 40, BRASS, 1, 7)
    for x in range(22, 112, 18):
        img[ell_mask(w, h, x, 52, 1.5, 1.5)] = C(BRASS[7])
    cables = np.zeros((h, w), bool)
    paths = [[(20, 50), (8, 30), (30, 10), (60, 20)], [(110, 50), (124, 26), (96, 8), (66, 18)]]
    for p in paths:
        cables |= line_mask(w, h, p, 3)
    img[cables] = C("#1a2a30")
    for k, p in enumerate(paths):
        t = ((f + 3 * k) % 6) / 6
        seg = int(t * (len(p) - 1))
        u = t * (len(p) - 1) - seg
        q = (p[seg][0] + (p[seg + 1][0] - p[seg][0]) * u, p[seg][1] + (p[seg + 1][1] - p[seg][1]) * u)
        img[ell_mask(w, h, q[0], q[1], 2.5, 2.5)] = C(CYAN[4])
    win = (np.abs(X - 65) < 22) & (np.abs(Y - 64) < 6)
    img[win] = C(CYAN[2] if f % 2 else CYAN[3])
    outline(img, base | cables)
    return u8(img)
