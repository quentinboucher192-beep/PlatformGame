"""Parallax backgrounds for Aethra, "La planete des sanctuaires" (2.19), 640x360 each,
horizontally seamless.  Sanctuary islands floating on an ocean of nebula, in violet and gold
light, where the four elements meet (roots, frost crystal, magma, void).

  sky.png       0.0   violet night turning gold at the horizon, nebula clouds, stars, and the
                      great golden Halo of the sanctuaries with its four elemental lights (opaque)
  far.png       0.12  distant floating islands with tiny temples and threads of falling light, mist
  mid.png       0.3   the four sanctuary islands: the root island and its mother mushroom, the
                      frost island and its crystal spires, the magma island and its glowing arch,
                      the void island and its ringed monolith
  near.png      0.55  a terrace of the sanctuary: golden-capped pillars and arches, lanterns,
                      grass and hanging roots
  abyss.png     0.5   the nebula ocean rising from below (transparent at the top)
  fg_petals.png 1.2   foreground: drifting violet petals and golden motes

Same construction as the other planets (backgrounds.py): shapes are drawn on a 3x wide canvas
and folded back mod 640; noise fields and profiles are 640-periodic.  Also writes the planet
banner ui/planets/aethra_banner.png (composed from these layers and the Aethra tileset).
Suggested level colours: sky top "#0a0618", abyss fill "#3a1c5e".
"""
import math

import numpy as np

from backgrounds import EW, H, W, emask, fold_img, fold_mask, glow_dot, profile_mask, shade_silhouette
from helpers import (add_glow, bayer, col, dither_alpha, fbm, fbm1d, gradient_rgb, hex_arr, new, noise1d,
                     paste, poly_mask, put, quantize_dither, rng, save_png, smoothstep, thick_path_mask,
                     bezier)

GOLD = ["#6a4220", "#a8702a", "#e0a848", "#ffd27a", "#fff0c0"]
VIO = ["#0c0718", "#160c2a", "#21123c", "#2e1a50", "#3e2466", "#56327e", "#74469a", "#9a64b8"]
PINK = ["#a8407a", "#d86aa8", "#ff9ac8"]
ELEM = {"root": "#7dffd6", "frost": "#9fe8ff", "magma": "#ffb13b", "void": "#ff7696"}


# ---------------------------------------------------------------------------
# sky
# ---------------------------------------------------------------------------
SKY_STOPS = [(0.0, "#0a0618"), (0.22, "#140b2c"), (0.45, "#221240"), (0.62, "#341a56"),
             (0.74, "#4c2464"), (0.84, "#6e3468"), (0.92, "#9a4e5c"), (1.0, "#c87a4e")]


def sky_palette():
    g = gradient_rgb(36, SKY_STOPS)
    pal = ["#%02x%02x%02x" % tuple(int(v) for v in c) for c in g]
    pal += ["#3a1e5e", "#5a2e7e", "#7a429a", "#a05aa8", "#c86aa0", "#e898b8", "#e0a848", "#ffd27a",
            "#fff0c0", "#ffffff"]
    return pal


def sky():
    g = gradient_rgb(H, SKY_STOPS)
    rgb = np.repeat(g[:, None, :], W, 1).astype(np.float32)
    yy = np.arange(H, dtype=np.float32)[:, None]
    # nebula: violet / rose clouds in the upper half, golden veils near the horizon
    n1 = fbm(W, H, 160, 5, seed=5101, wrap_x=True, cell_y=80)
    n2 = fbm(W, H, 64, 4, seed=5102, wrap_x=True, cell_y=40)
    band = np.exp(-((yy - 110) / 70.0) ** 2)
    neb = smoothstep(0.45, 0.8, n1 * 0.7 + n2 * 0.3) * band * 0.75
    rose = smoothstep(0.62, 0.85, n2) * band * 0.35
    vio, ros = hex_arr(["#6a3a9a"])[0], hex_arr(["#c86aa0"])[0]
    rgb = rgb * (1 - neb[..., None]) + vio * neb[..., None]
    rgb = rgb * (1 - rose[..., None]) + ros * rose[..., None]
    gold_band = np.exp(-((yy - 300) / 34.0) ** 2) * smoothstep(0.5, 0.8, fbm(W, H, 128, 4, seed=5103,
                                                                             wrap_x=True, cell_y=16)) * 0.5
    rgb = rgb * (1 - gold_band[..., None]) + hex_arr(["#e0a848"])[0] * gold_band[..., None]
    # the Halo: a vast golden ring of light (seamless: drawn once, it never crosses the edges)
    hx, hy, hr = 452.0, 118.0, 72.0
    xx = np.arange(W, dtype=np.float32)[None, :]
    d = np.sqrt((xx + 0.5 - hx) ** 2 + (yy + 0.5 - hy) ** 2)
    inner = np.clip(1 - d / hr, 0, 1) ** 1.5 * 0.35
    rgb = rgb * (1 - inner[..., None]) + hex_arr(["#4a2a6e"])[0] * inner[..., None]
    halo = np.exp(-((d - hr) / 7.0) ** 2) * 0.55
    rgb = rgb * (1 - halo[..., None]) + hex_arr(["#e0a848"])[0] * halo[..., None]
    img = quantize_dither(rgb, sky_palette())
    # stars
    r = rng(5104)
    for _ in range(900):
        x, y = int(r.random() * W), int(r.random() * H)
        if r.random() > (1 - y / H) ** 1.3:
            continue
        b = r.random()
        if b < 0.5:
            put(img, x, y, "#3a2a62")
        elif b < 0.8:
            put(img, x, y, "#8a74b8")
        elif b < 0.95:
            put(img, x, y, "#e8dcff")
        else:
            put(img, x, y, "#ffffff")
            for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                put(img, (x + ddx) % W, y + ddy, "#ffd27a")
    # the ring itself: crisp 2px golden line with a bright upper-left arc
    for k in range(720):
        a = 2 * math.pi * k / 720
        for rr, c in ((hr - 0.5, "#ffd27a"), (hr + 0.7, "#e0a848")):
            x, y = hx + math.cos(a) * rr, hy + math.sin(a) * rr
            lit = math.cos(a - 3.9) > 0.3
            put(img, int(x), int(y), "#fff0c0" if (lit and rr < hr) else c)
    # the four elemental lights on the ring (root, frost, magma, void)
    for a, key in ((-2.36, "root"), (-0.79, "frost"), (0.79, "magma"), (2.36, "void")):
        x, y = hx + math.cos(a) * hr, hy + math.sin(a) * hr
        cx, cy = int(round(x)), int(round(y))
        for ddx in range(-3, 4):
            for ddy in range(-3, 4):
                dd = math.hypot(ddx, ddy)
                if dd <= 1.2:
                    put(img, cx + ddx, cy + ddy, "#ffffff")
                elif dd <= 2.3:
                    put(img, cx + ddx, cy + ddy, ELEM[key])
                elif dd <= 3.3 and (ddx + ddy) % 2 == 0:
                    put(img, cx + ddx, cy + ddy, ELEM[key])
    img[..., 3] = 255
    return img


# ---------------------------------------------------------------------------
# shared: floating island silhouettes
# ---------------------------------------------------------------------------
def island(m, cx, ty, w, depth, seed, flat=True):
    """Floating island: a flat (or gently domed) top and a jagged inverted-cone underside."""
    r = rng(seed)
    x0, x1 = int(cx - w / 2), int(cx + w / 2)
    jag = noise1d(max(8, x1 - x0), 4, seed) if x1 - x0 >= 8 else np.zeros(max(1, x1 - x0))
    for i, x in enumerate(range(x0, x1)):
        u = abs(x + 0.5 - cx) / (w / 2)
        top = ty + (int(round(u ** 4 * 4)) if flat else int(round(u ** 2 * 6)))
        cone = (1 - u ** 1.15) * depth
        bot = ty + 2 + int(round(cone * (0.75 + 0.5 * jag[i % len(jag)]) + r.random() * 1.5))
        xx = x % EW
        m[max(0, top):min(H, bot), xx] = True
    return m


def light_fall(ext, x, y0, y1, c, step=2):
    """A thread of falling light under an island (dithered, fading)."""
    for y in range(int(y0), int(y1)):
        t = (y - y0) / max(1, y1 - y0)
        if (y + int(x)) % step == 0 and (t < 0.5 or (y * 7 + int(x)) % 3 == 0):
            if 0 <= y < H:
                ext[y, int(x) % EW] = col(c, int(220 * (1 - t)))


# ---------------------------------------------------------------------------
# far
# ---------------------------------------------------------------------------
def far():
    m = emask()
    glow = new(EW, H)
    r = rng(5201)
    isl = [(30, 200, 70, 34), (130, 236, 46, 22), (210, 190, 90, 40), (330, 250, 56, 24), (420, 214, 80, 30),
           (520, 182, 64, 32), (600, 240, 50, 20)]
    for k, (cx, ty, w, dp) in enumerate(isl):
        island(m, W + cx, ty, w, dp, seed=5210 + k)
        # tiny temples / obelisks on top
        for j in range(int(w / 22) + 1):
            tx = W + cx - w / 3 + j * 18 + r.random() * 6
            kind = (k + j) % 3
            if kind == 0:      # obelisk
                m[ty - 12:ty + 1, int(tx):int(tx) + 2] = True
                m[ty - 14:ty - 12, int(tx)] = True
            elif kind == 1:    # small domed shrine
                yy, xx = np.mgrid[0:H, 0:EW]
                m |= ((xx + 0.5 - tx) ** 2 / 25 + (yy + 0.5 - (ty - 4)) ** 2 / 16 <= 1) & (yy < ty)
                m[ty - 4:ty, int(tx) - 5:int(tx) + 6] = True
            else:              # two pillars and a lintel
                m[ty - 9:ty, int(tx) - 4] = True
                m[ty - 9:ty, int(tx) + 4] = True
                m[ty - 10:ty - 8, int(tx) - 5:int(tx) + 6] = True
            glow_dot(glow, tx, ty - 6 if kind != 0 else ty - 13, "#ffd27a", "#6a4a6e", r=0)
        light_fall(glow, W + cx + (r.random() - 0.5) * w * 0.3, ty + dp * 0.8, ty + dp * 0.8 + 50,
                   "#e0a848" if k % 2 else "#9a64b8")
    fm = fold_mask(m)
    img = shade_silhouette(fm, "#34245a", "#56428a", "#2c1d4c",
                           fog=[(0, "#34245a"), (200, "#34245a"), (260, "#3c2a66"), (360, "#443070")])
    paste(img, fold_img(glow), 0, 0)
    # nebula mist below
    yy = np.arange(H)[:, None] * np.ones((1, W))
    n = fbm(W, H, 128, 4, seed=5230, wrap_x=True, cell_y=32)
    band = np.exp(-((yy - 310) / 30.0) ** 2) * 0.9 + np.exp(-((yy - 262) / 12.0) ** 2) * 0.3
    a = band * smoothstep(0.35, 0.7, n)
    mist = new(W, H)
    mist[..., :3] = col("#6a4a8e")[:3]
    mist[..., 3] = dither_alpha(a, levels=(0, 64, 110, 150))
    paste(img, mist, 0, 0)
    return img


# ---------------------------------------------------------------------------
# mid: the four sanctuary islands
# ---------------------------------------------------------------------------
def mid():
    m = emask()
    glow = new(EW, H)
    yy, xx = np.mgrid[0:H, 0:EW]
    r = rng(5301)
    # 1. root island: the mother mushroom and hanging roots
    cx, ty = W + 84, 216
    island(m, cx, ty, 124, 66, seed=5310)
    m |= ((xx + 0.5 - cx) ** 2 / 46 ** 2 + (yy + 0.5 - (ty - 54)) ** 2 / 26 ** 2 <= 1) & (yy <= ty - 50)
    m |= (np.abs(xx + 0.5 - cx) <= 7 + (yy - (ty - 52)) * 0.12) & (yy >= ty - 54) & (yy <= ty)
    for k in range(6):
        x0 = cx - 40 + k * 15
        pts = bezier((x0, ty + 20), (x0 + (r.random() - 0.5) * 20, ty + 60), (x0 + (r.random() - 0.5) * 30, ty + 95), 18)
        m |= thick_path_mask(EW, H, pts, [2.4 - 1.6 * i / 17 for i in range(18)])
    for (u, v) in ((-24, -62), (-6, -72), (16, -66), (30, -56), (-34, -50), (6, -58)):
        glow_dot(glow, cx + u, ty + v, "#e6fffb", "#2a8a7e", r=1)
    for k in range(5):
        glow_dot(glow, cx - 34 + k * 17, ty + 70 + (k % 2) * 10, ELEM["root"], "#1f5a52", r=0)
    # 2. frost island: crystal spires
    cx, ty = W + 250, 238
    island(m, cx, ty, 108, 52, seed=5320)
    for k in range(6):
        x = cx - 40 + k * 15 + r.random() * 5
        h = 26 + r.random() * 46 * (1 - abs(k - 2.5) / 3.5)
        w = 6 + r.random() * 6
        lean = (r.random() - 0.5) * 10
        pts = [(x - w / 2, ty + 2), (x - w / 2 + lean * 0.6, ty - h * 0.7), (x + lean, ty - h),
               (x + w / 2 + lean * 0.6, ty - h * 0.72), (x + w / 2, ty + 2)]
        m |= poly_mask(EW, H, pts)
        glow_dot(glow, x + lean * 0.8, ty - h * 0.7, "#ffffff", "#3a6a9a", r=1)
    # 3. magma island: basalt arch with a glowing heart
    cx, ty = W + 412, 222
    island(m, cx, ty, 120, 60, seed=5330)
    arch_out = ((xx + 0.5 - cx) ** 2 / 34 ** 2 + (yy + 0.5 - ty) ** 2 / 46 ** 2 <= 1) & (yy <= ty)
    arch_in = ((xx + 0.5 - cx) ** 2 / 24 ** 2 + (yy + 0.5 - ty) ** 2 / 36 ** 2 <= 1)
    m |= arch_out & ~arch_in
    lava = new(EW, H)
    for k in range(9):
        a = math.pi * (0.1 + 0.8 * k / 8)
        x = cx + math.cos(a) * 29
        y = ty - math.sin(a) * 41
        glow_dot(lava, x, y, "#ffd27a", "#a8402a", r=0)
    glow_dot(lava, cx, ty - 14, "#fff0c0", "#ff7a2a", r=3)
    for k in range(4):
        light_fall(lava, cx - 30 + k * 20, ty + 30, ty + 30 + 34 + k * 6, "#ff9a3d", step=1)
    # 4. void island: a monolith girdled by a pink ring
    cx, ty = W + 566, 206
    island(m, cx, ty, 100, 58, seed=5340)
    m |= (np.abs(xx + 0.5 - cx) <= 7 - (ty - yy) * 0.04) & (yy >= ty - 78) & (yy <= ty)
    m |= (np.abs(xx + 0.5 - cx) <= 2) & (yy >= ty - 86) & (yy <= ty)
    ring_m = np.abs(np.sqrt((xx + 0.5 - cx) ** 2 / 26 ** 2 + (yy + 0.5 - (ty - 52)) ** 2 / 7 ** 2) - 1) <= 0.1
    for k in range(4):
        glow_dot(glow, cx + (k - 1.5) * 18, ty + 46 + (k % 2) * 8, ELEM["void"], "#5a2a6a", r=0)
    fm = fold_mask(m)
    img = shade_silhouette(fm, "#1a0f30", "#6a4aa0", "#120a24", lit_w=2,
                           fog=[(0, "#1a0f30"), (220, "#1a0f30"), (280, "#22143e"), (360, "#2c1c4c")])
    # golden rims on the island tops (the sanctuaries catch the gold light)
    top_edge = fm & ~np.roll(fm, 1, axis=0)
    gold_lit = top_edge & (fbm(W, H, 32, 2, seed=5350, wrap_x=True) > 0.45)
    img[gold_lit] = col("#c89048")
    paste(img, fold_img(lava), 0, 0)
    ring_img = new(EW, H)
    ring_img[ring_m & (yy > (ty - 52))] = col("#ff7696")
    ring_img[ring_m & (yy <= (ty - 52))] = col("#c05a9a")
    paste(img, fold_img(ring_img), 0, 0)
    paste(img, fold_img(glow), 0, 0)
    return img


# ---------------------------------------------------------------------------
# near: a sanctuary terrace
# ---------------------------------------------------------------------------
def near():
    m = emask()
    glow = new(EW, H)
    gold = emask()
    flute = emask()
    top = 312 + 12 * fbm1d(W, 160, 4, seed=5401)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    r = rng(5402)
    # stone pillars (base, shaft, golden capital), some broken, some joined by arches; bushes
    for (px, ph, arch, broken) in ((40, 84, 70, False), (110, 84, 0, False), (250, 58, 0, True),
                                   (330, 96, 62, False), (392, 96, 0, False), (520, 66, 0, True),
                                   (600, 78, 0, False)):
        gy = int(top[px % W]) + 2
        x = W + px
        m[gy - 6:gy, x - 10:x + 11] = True                    # base
        m[gy - ph:gy - 6, x - 7:x + 8] = True                 # shaft
        if broken:
            m[gy - ph - 4:gy - ph, x - 7:x + 1] = True
            m[gy - ph - 7:gy - ph - 4, x - 7:x - 3] = True
        else:
            m[gy - ph - 6:gy - ph, x - 10:x + 11] = True      # capital
            gold[gy - ph - 6:gy - ph - 3, x - 10:x + 11] = True
            glow_dot(glow, x, gy - ph - 11, "#fff0c0", "#a8702a", r=1)
        gold[gy - 6:gy - 4, x - 10:x + 11] = True
        flute[gy - ph:gy - 6, x - 3] = True
        flute[gy - ph:gy - 6, x + 3] = True
        if arch:
            ax = x + arch / 2
            ay = gy - ph - 3
            ring = (np.abs(np.sqrt((xx + 0.5 - ax) ** 2 / (arch / 2 + 3) ** 2 + (yy + 0.5 - ay) ** 2 / 24 ** 2) - 1)
                    <= 0.14) & (yy <= ay)
            m |= ring
            gold |= ring & (yy <= ay - 20)
    for (bx, br) in ((70, 14), (150, 10), (290, 16), (440, 12), (560, 15), (630, 9)):
        gy = int(top[bx % W]) + 2
        for k in range(3):
            cx = W + bx + (k - 1) * br * 0.7
            cy = gy - br * (0.55 + 0.25 * (k == 1))
            m |= (xx + 0.5 - cx) ** 2 + (yy + 0.5 - cy) ** 2 <= (br * (0.7 if k != 1 else 0.9)) ** 2
        glow_dot(glow, W + bx - 2, gy - br, "#e08aff", "#5a2e7e", r=0)
        glow_dot(glow, W + bx + br * 0.6, gy - br * 0.6, "#ffd27a", "#6a4220", r=0)
    # lanterns hanging on posts
    for lx in (170, 460, 560):
        gy = int(top[lx % W]) + 2
        x = W + lx
        m[gy - 46:gy, x:x + 2] = True
        m[gy - 46:gy - 44, x:x + 9] = True
        m[gy - 44:gy - 36, x + 7:x + 8] = True
        glow_dot(glow, x + 7, gy - 33, "#fff0c0", "#c88a3a", r=2)
    # grass and hanging roots along the edge
    for k in range(70):
        x = int(r.random() * W)
        gy = int(top[x])
        h = 3 + int(r.random() * 7)
        m[gy - h:gy, W + x] = True
    fm = fold_mask(m)
    img = shade_silhouette(fm, "#150b26", "#4a3478", "#0e0618", lit_w=2,
                           fog=[(0, "#150b26"), (300, "#150b26"), (360, "#1c1032")])
    img[fold_mask(flute) & fm] = col("#100820")
    gm = fold_mask(gold) & fm
    img[gm] = col("#a8702a")
    gt = gm & ~np.roll(gm, 1, axis=0)
    img[gt] = col("#ffd27a")
    paste(img, fold_img(glow), 0, 0)
    return img


# ---------------------------------------------------------------------------
# abyss: the nebula ocean
# ---------------------------------------------------------------------------
def abyss():
    yy = np.arange(H, dtype=np.float32)[:, None] * np.ones((1, W), np.float32)
    n1 = fbm(W, H, 128, 5, seed=5501, wrap_x=True, cell_y=48)
    n2 = fbm(W, H, 64, 4, seed=5502, wrap_x=True, cell_y=24)
    rise = smoothstep(150, 340, yy + (n1 - 0.5) * 70)
    t = np.clip(rise * (0.55 + 0.6 * n2), 0, 1)
    ramp = hex_arr(["#24103e", "#3a1c5e", "#5a2a7e", "#8a3a8e", "#c05a9a", "#e8a050", "#ffd27a"])
    xs = np.linspace(0, 1, len(ramp))
    rgb = np.stack([np.interp(t, xs, ramp[:, i]) for i in range(3)], -1)
    pal = ["#24103e", "#2e1650", "#3a1c5e", "#4a226e", "#5a2a7e", "#723286", "#8a3a8e", "#a44a94", "#c05a9a",
           "#d47a7a", "#e8a050", "#ffd27a", "#fff0c0"]
    img = quantize_dither(rgb, pal)
    a = np.clip(rise * 1.15, 0, 1)
    img[..., 3] = dither_alpha(a, levels=(0, 64, 128, 192, 255))
    # sparkles of starlight on the ocean
    r = rng(5503)
    for _ in range(140):
        x, y = int(r.random() * W), int(250 + r.random() * 110)
        if img[min(y, H - 1), x, 3] > 128:
            put(img, x, min(y, H - 1), "#fff0c0" if r.random() < 0.4 else "#ff9ac8")
    return img


# ---------------------------------------------------------------------------
# fg_petals: drifting petals and golden motes
# ---------------------------------------------------------------------------
def fg_petals():
    img = new(W, H)
    r = rng(5601)
    yy, xx = np.mgrid[0:H, 0:W]
    alpha = np.zeros((H, W), np.float32)
    colour = np.zeros((H, W, 3), np.float32)
    for _ in range(60):                       # soft golden motes
        cx, cy = r.uniform(0, W), r.uniform(0, H * 0.8)
        rad = r.uniform(1.0, 3.0)
        dxw = np.minimum(np.abs(xx - cx), W - np.abs(xx - cx))
        a = np.clip(1 - np.hypot(dxw, yy - cy) / rad, 0, 1) ** 1.5 * r.uniform(0.5, 1.0)
        tint = hex_arr([r.choice(["#fff0c0", "#ffd27a", "#e8dcff"])])[0]
        mask = a > alpha
        alpha = np.where(mask, a, alpha)
        colour[mask] = tint
    img[..., :3] = colour.astype(np.uint8)
    img[..., 3] = dither_alpha(alpha, levels=(0, 60, 120, 190, 255))
    # crisp petals (3-5 px, outlined, two tones) drifting at various angles
    shapes = [[".ab", "abb", "bb."], ["ab.", "bbb", ".b."], [".a.", "abb", ".bb", "..b"], ["aab", ".bb"]]
    cols = [("#e08aff", "#9a5ac8"), ("#ff9ac8", "#c05a9a"), ("#ffd27a", "#c88a3a"), ("#c8a0f8", "#7a52b8")]
    for k in range(34):
        sh = shapes[k % len(shapes)]
        ca, cb = cols[int(r.integers(0, len(cols)))]
        x0, y0 = int(r.uniform(0, W)), int(r.uniform(0, H * 0.85))
        for j, row in enumerate(sh):
            for i, ch in enumerate(row):
                if ch == ".":
                    continue
                put(img, (x0 + i) % W, y0 + j, ca if ch == "a" else cb)
    return img


LAYERS = [("sky", sky), ("far", far), ("mid", mid), ("near", near), ("abyss", abyss), ("fg_petals", fg_petals)]


def generate():
    out = []
    for name, fn in LAYERS:
        img = fn()
        assert img.shape == (H, W, 4), name
        out.append(save_png(img, f"backgrounds/aethra/{name}.png"))
    return out


if __name__ == "__main__":
    for p in generate():
        print(p)
