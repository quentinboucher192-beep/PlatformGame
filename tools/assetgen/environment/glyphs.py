"""Gamepad glyph sheets (ASSET_SPEC section 11): ui/glyphs/{xbox,playstation,generic}.png,
608x32 = 19 icons of 32x32 in this order:
  0 South  1 East  2 West  3 North  4 LB/L1  5 RB/R1  6 LT/L2  7 RT/R2  8 Back/View/Share
  9 Start/Menu/Options  10 LS click  11 RS click  12 DPad up  13 down  14 left  15 right
  16 left stick  17 right stick  18 guide
Styles: xbox (coloured A/B/X/Y letters on dark round buttons), playstation (cross / circle /
square / triangle symbols), generic (neutral grey diamond with the pressed position lit).
"""
import numpy as np

from helpers import (FONT4x6, P, col, ellipse_mask, new, outline, poly_mask, put, save_png,
                     text_mask, thick_path_mask)

O = P.OUTLINE
N_ICONS = 19

BOLD = {
    "A": ["...###...", "..#####..", "..##.##..", ".##...##.", ".##...##.", "##.....##",
          "#########", "#########", "##.....##", "##.....##", "##.....##"],
    "B": ["#######..", "########.", "##....##.", "##....##.", "#######..", "########.",
          "##.....##", "##.....##", "##.....##", "#########", "########."],
    "X": ["##.....##", "##.....##", ".##...##.", "..##.##..", "...###...", "...###...",
          "...###...", "..##.##..", ".##...##.", "##.....##", "##.....##"],
    "Y": ["##.....##", "##.....##", ".##...##.", "..##.##..", "...###...", "...###...",
          "...###...", "...###...", "...###...", "...###...", "...###..."],
}


class Theme:
    def __init__(self, body, lit, dark, shadow, label, accent):
        self.body, self.lit, self.dark, self.shadow = body, lit, dark, shadow
        self.label, self.accent = label, accent


XBOX = Theme("#262b35", "#414858", "#1a1e26", "#101319", "#e8eef5", "#9fb3c8")
PS = Theme("#22263a", "#3d4462", "#181b2a", "#0f111b", "#c3d0de", "#8fa3c8")
GEN = Theme("#3b4150", "#5d667c", "#2b303c", "#1a1d25", "#e8eef5", "#c3d0de")
GEN_DIM = "#343a48"
GEN_ON = "#f0f4f8"

XBOX_COL = {"A": ("#5ecb3c", "#2f7a1a"), "B": ("#ee4a46", "#8a1a24"),
            "X": ("#3f8fe8", "#1a4a8a"), "Y": ("#f2c230", "#8a6410")}
PS_COL = {"cross": ("#86b4f4", "#2a4a8a"), "circle": ("#f0646e", "#8a1a24"),
          "square": ("#ec94dc", "#7a2a6a"), "triangle": ("#4fdcb4", "#1a6a52")}


def grid():
    yy, xx = np.mgrid[0:32, 0:32]
    return xx + 0.5, yy + 0.5


def rrect(x0, y0, x1, y1, r):
    x, y = grid()
    cx = np.clip(x, x0 + r, x1 - r)
    cy = np.clip(y, y0 + r, y1 - r)
    return ((x - cx) ** 2 + (y - cy) ** 2 <= r * r) & (x >= x0) & (x <= x1) & (y >= y0) & (y <= y1)


def body3d(img, m, th, depth=2, lit_override=None, sheen=True):
    """Draw a raised button: shadow below, body with lit top-left rim, outline."""
    sh = np.zeros_like(m)
    for d in range(1, depth + 1):
        sh |= np.roll(m, d, 0)
    sh &= ~m
    img[sh] = col(th.shadow)
    x, y = grid()
    img[m] = col(th.body)
    up = np.roll(m, 1, 0)
    left = np.roll(m, 1, 1)
    down = np.roll(m, -1, 0)
    right = np.roll(m, -1, 1)
    edge_tl = m & (~up | ~left)
    edge_br = m & (~down | ~right)
    img[edge_br] = col(th.dark)
    img[edge_tl] = col(lit_override or th.lit)
    # inner sheen on the upper part
    m2 = m & up & np.roll(up, 1, 0) & left & np.roll(left, 1, 1)
    ys = np.where(m.any(1))[0]
    if len(ys) and sheen:
        y0, y1 = ys.min(), ys.max()
        sheen = m2 & (y < y0 + (y1 - y0) * 0.35 + 1) & ((x.astype(int) + y.astype(int)) % 2 == 0)
        img[sheen] = col(th.lit)
        img[edge_tl] = col(lit_override or th.lit)
    outline(img, O)


def round_button(img, th, r=12.6, cx=16, cy=15.5, lit=None):
    x, y = grid()
    m = (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    body3d(img, m, th, lit_override=lit)
    return m


def stamp_mask(img, m, ox, oy, c, shadow=None):
    h, w = m.shape
    for yy in range(h):
        for xx in range(w):
            if m[yy, xx]:
                if shadow is not None and not (yy + 1 < h and m[yy + 1, xx]):
                    put(img, ox + xx, oy + yy + 1, shadow)
                put(img, ox + xx, oy + yy, c)


def label(img, text, cx, cy, c, shadow="#0b0f1e"):
    m = text_mask(text, FONT4x6, spacing=1)
    h, w = m.shape
    stamp_mask(img, m, int(round(cx - w / 2)), int(round(cy - h / 2)), c, shadow)


# ---------------------------------------------------------------------------
# face buttons
# ---------------------------------------------------------------------------
def xbox_face(letter):
    img = new(32, 32)
    round_button(img, XBOX)
    c, d = XBOX_COL[letter]
    m = np.array([[ch == "#" for ch in r] for r in BOLD[letter]])
    stamp_mask(img, m, 12, 10, c, shadow=d)
    # small glint on the button
    put(img, 9, 7, "#5a6274")
    put(img, 10, 6, "#5a6274")
    return img


def ps_face(kind):
    img = new(32, 32)
    round_button(img, PS)
    c, d = PS_COL[kind]
    x, y = grid()
    cx, cy = 16, 15.5
    if kind == "cross":
        m = thick_path_mask(32, 32, [(10.5 + i * 0.5, 10 + i * 0.5) for i in range(23)], 1.25)
        m |= thick_path_mask(32, 32, [(21.5 - i * 0.5, 10 + i * 0.5) for i in range(23)], 1.25)
    elif kind == "circle":
        r = np.hypot(x - cx, y - cy)
        m = (r <= 6.6) & (r > 4.4)
    elif kind == "square":
        m = (np.abs(x - cx) <= 6) & (np.abs(y - cy) <= 6) & ~((np.abs(x - cx) <= 4) & (np.abs(y - cy) <= 4))
    else:
        outer = poly_mask(32, 32, [(16, 7.5), (24.5, 22), (7.5, 22)])
        inner = poly_mask(32, 32, [(16, 11.5), (21.0, 20.0), (11.0, 20.0)])
        m = outer & ~inner
    sh = np.roll(m, 1, 0) & ~m
    img[sh] = col(d)
    img[m] = col(c)
    put(img, 9, 7, "#525a7a")
    put(img, 10, 6, "#525a7a")
    return img


def generic_face(pos):
    """Diamond of four small buttons; pos 0=S 1=E 2=W 3=N is lit."""
    img = new(32, 32)
    centres = {0: (16, 24.5), 1: (24.5, 16), 2: (7.5, 16), 3: (16, 7.5)}
    x, y = grid()
    dim_th = Theme("#4a5162", "#6a7388", "#363c4a", "#1a1d25", "", "")
    for k, (cx, cy) in centres.items():
        if k == pos:
            continue
        m = (x - cx) ** 2 + (y - cy) ** 2 <= 4.6 ** 2
        lay = new(32, 32)
        body3d(lay, m, dim_th, depth=1, sheen=False)
        sel = lay[..., 3] > 0
        img[sel] = lay[sel]
    cx, cy = centres[pos]
    m = (x - cx) ** 2 + (y - cy) ** 2 <= 5.4 ** 2
    lay = new(32, 32)
    body3d(lay, m, Theme("#c3d0de", GEN_ON, "#7d8aa0", "#1a1d25", "", ""), depth=1)
    sel = lay[..., 3] > 0
    img[sel] = lay[sel]
    put(img, int(cx) - 2, int(cy) - 3, "#ffffff")
    return img


# ---------------------------------------------------------------------------
# shoulders, triggers, small buttons, sticks, dpad, guide
# ---------------------------------------------------------------------------
def bumper(th, text, side):
    img = new(32, 32)
    m = rrect(3, 10, 28, 21, 4.5)
    # slanted outer end
    x, y = grid()
    if side == "L":
        m &= ~((x < 9) & (y < 10 + (9 - x) * 0.6))
    else:
        m &= ~((x > 23) & (y < 10 + (x - 23) * 0.6))
    body3d(img, m, th)
    label(img, text, 16, 15.5, th.label)
    return img


def trigger(th, text, side):
    img = new(32, 32)
    x, y = grid()
    m = rrect(8, 4, 24, 28, 5.5)
    top = ellipse_mask(32, 32, 16, 9, 8, 6)
    m |= top & (y < 12)
    if side == "L":
        m &= ~((x > 20) & (y < 4 + (x - 20) * 1.2))
    else:
        m &= ~((x < 12) & (y < 4 + (12 - x) * 1.2))
    body3d(img, m, th, sheen=False)
    # grip ridges
    for yy in (21, 24):
        for xx in range(11, 22):
            if m[yy, xx]:
                put(img, xx, yy, th.dark)
    label(img, text, 16, 13.5, th.label)
    return img


def small_round(th, icon):
    img = new(32, 32)
    round_button(img, th, r=10.6, cx=16, cy=16)
    icon(img, th)
    return img


def capsule(th, icon):
    img = new(32, 32)
    m = rrect(10, 4, 22, 27, 6)
    body3d(img, m, th)
    icon(img, th)
    return img


def ic_view(img, th):
    c = th.label
    for (x0, y0) in [(10, 10), (14, 14)]:
        for x in range(x0, x0 + 8):
            put(img, x, y0, c)
            put(img, x, y0 + 6, c)
        for y in range(y0, y0 + 7):
            put(img, x0, y, c)
            put(img, x0 + 7, y, c)
    # clear overlap interior of the front square
    for y in range(15, 20):
        for x in range(15, 21):
            put(img, x, y, th.body)


def ic_menu(img, th):
    for y in (11, 15, 19):
        for x in range(10, 22):
            put(img, x, y, th.label)
            put(img, x, y + 1, th.dark)


def ic_share(img, th):
    c = th.label
    # arrow up out of a tray
    for y in range(8, 18):
        put(img, 16, y, c)
    for d in range(1, 4):
        put(img, 16 - d, 8 + d, c)
        put(img, 16 + d, 8 + d, c)
    for x in range(12, 21):
        put(img, x, 22, c)
    put(img, 12, 21, c)
    put(img, 12, 20, c)
    put(img, 20, 21, c)
    put(img, 20, 20, c)


def ic_options(img, th):
    for y in (10, 14, 18):
        for x in range(13, 20):
            put(img, x, y, th.label)
            put(img, x, y + 1, th.dark)


def ic_back_generic(img, th):
    c = th.label
    # left-pointing arrow with a hook
    for x in range(12, 21):
        put(img, x, 16, c)
    for d in range(1, 4):
        put(img, 12 + d, 16 - d, c)
        put(img, 12 + d, 16 + d, c)
    for y in range(11, 16):
        put(img, 20, y, c)
    for x in range(17, 21):
        put(img, x, 11, c)


def ic_start_generic(img, th):
    m = poly_mask(32, 32, [(13, 10), (21.5, 16), (13, 22)])
    img[m] = col(th.label)


def ic_home(img, th, c=None):
    c = c or th.label
    m = poly_mask(32, 32, [(16, 8), (23.5, 15), (21.5, 15), (21.5, 22.5), (10.5, 22.5), (10.5, 15),
                           (8.5, 15)])
    door = (np.abs(grid()[0] - 16) <= 1.6) & (grid()[1] >= 18) & (grid()[1] <= 22.5)
    img[m & ~door] = col(c)


def stick(th, text, click):
    img = new(32, 32)
    x, y = grid()
    cx, cy = 16, 17.5 if click else 16
    base = (x - cx) ** 2 + (y - cy) ** 2 <= 11.6 ** 2
    img[base] = col(th.shadow)
    outline(img, O)
    cap = (x - cx) ** 2 + (y - cy - 0.5) ** 2 <= 9.4 ** 2
    lay = new(32, 32)
    body3d(lay, cap, th, depth=1)
    sel = lay[..., 3] > 0
    img[sel] = lay[sel]
    # concave rim ring
    ring = ((x - cx) ** 2 + (y - cy - 0.5) ** 2 <= 7.4 ** 2) & ~((x - cx) ** 2 + (y - cy - 0.5) ** 2 <= 6.4 ** 2)
    img[ring & ((x - cx) + (y - cy) > 0)] = col(th.lit)
    img[ring & ((x - cx) + (y - cy) <= 0)] = col(th.dark)
    label(img, text, cx, cy + 0.5, th.label)
    if click:
        # press chevron above the stick
        for k in range(4):
            for xx in (12 + k, 13 + k, 19 - k, 18 - k):
                put(img, xx, 1 + k, th.accent)
    else:
        # four movement arrows
        a = th.accent
        for k in range(3):
            for j in range(15 - k, 17 + k):
                put(img, j, k, a)            # up
                put(img, j, 31 - k, a)       # down
                put(img, k, j, a)            # left
                put(img, 31 - k, j, a)       # right
    return img


def dpad(th, pressed, on=None, dim=None):
    img = new(32, 32)
    x, y = grid()
    arm = 4.6
    m = ((np.abs(x - 16) <= arm) & (np.abs(y - 16) <= 13)) | ((np.abs(y - 16) <= arm) & (np.abs(x - 16) <= 13))
    body3d(img, m, Theme(dim or th.body, th.lit, th.dark, th.shadow, "", ""), sheen=False)
    # centre dimple
    put(img, 15, 15, th.dark)
    put(img, 16, 16, th.lit)
    regions = {"up": (np.abs(x - 16) <= arm - 1) & (y < 11) & (y > 3.5),
               "down": (np.abs(x - 16) <= arm - 1) & (y > 21) & (y < 28.5),
               "left": (np.abs(y - 16) <= arm - 1) & (x < 11) & (x > 3.5),
               "right": (np.abs(y - 16) <= arm - 1) & (x > 21) & (x < 28.5)}
    hl = on or "#e8eef5"
    img[regions[pressed]] = col(hl)
    # arrow triangle in the pressed arm
    tri = {"up": [(16, 5.5), (19, 9.5), (13, 9.5)], "down": [(16, 26.5), (19, 22.5), (13, 22.5)],
           "left": [(5.5, 16), (9.5, 13), (9.5, 19)], "right": [(26.5, 16), (22.5, 13), (22.5, 19)]}
    t = poly_mask(32, 32, tri[pressed])
    img[t] = col(th.shadow)
    return img


def guide(th, ring_c, home_c):
    img = new(32, 32)
    round_button(img, th, r=12.6)
    x, y = grid()
    r = np.hypot(x - 16, y - 15.5)
    ring = (r <= 10.6) & (r > 9.4)
    img[ring] = col(ring_c)
    ic_home(img, th, home_c)
    return img


# ---------------------------------------------------------------------------
# sheets
# ---------------------------------------------------------------------------
def sheet_xbox():
    t = XBOX
    icons = [xbox_face("A"), xbox_face("B"), xbox_face("X"), xbox_face("Y"),
             bumper(t, "LB", "L"), bumper(t, "RB", "R"), trigger(t, "LT", "L"), trigger(t, "RT", "R"),
             small_round(t, ic_view), small_round(t, ic_menu),
             stick(t, "LS", True), stick(t, "RS", True),
             dpad(t, "up"), dpad(t, "down"), dpad(t, "left"), dpad(t, "right"),
             stick(t, "L", False), stick(t, "R", False),
             guide(t, "#c3d0de", "#e8eef5")]
    return icons


def sheet_ps():
    t = PS
    icons = [ps_face("cross"), ps_face("circle"), ps_face("square"), ps_face("triangle"),
             bumper(t, "L1", "L"), bumper(t, "R1", "R"), trigger(t, "L2", "L"), trigger(t, "R2", "R"),
             capsule(t, ic_share), capsule(t, ic_options),
             stick(t, "L3", True), stick(t, "R3", True),
             dpad(t, "up"), dpad(t, "down"), dpad(t, "left"), dpad(t, "right"),
             stick(t, "L", False), stick(t, "R", False),
             guide(t, "#86b4f4", "#c3d0de")]
    return icons


def sheet_generic():
    t = GEN
    icons = [generic_face(0), generic_face(1), generic_face(2), generic_face(3),
             bumper(t, "L1", "L"), bumper(t, "R1", "R"), trigger(t, "L2", "L"), trigger(t, "R2", "R"),
             small_round(t, ic_back_generic), small_round(t, ic_start_generic),
             stick(t, "L3", True), stick(t, "R3", True),
             dpad(t, "up", on=GEN_ON, dim=GEN_DIM), dpad(t, "down", on=GEN_ON, dim=GEN_DIM),
             dpad(t, "left", on=GEN_ON, dim=GEN_DIM), dpad(t, "right", on=GEN_ON, dim=GEN_DIM),
             stick(t, "L", False), stick(t, "R", False),
             guide(t, "#c3d0de", "#e8eef5")]
    return icons


def build(icons):
    assert len(icons) == N_ICONS
    img = new(32 * N_ICONS, 32)
    for i, ic in enumerate(icons):
        img[:, i * 32:(i + 1) * 32] = ic
    return img


def generate():
    return [save_png(build(sheet_xbox()), "ui/glyphs/xbox.png"),
            save_png(build(sheet_ps()), "ui/glyphs/playstation.png"),
            save_png(build(sheet_generic()), "ui/glyphs/generic.png")]
