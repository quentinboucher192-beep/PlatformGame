"""Parallax backgrounds for Sylvanox (ASSET_SPEC section 8), each 640x360, horizontally seamless.

  sky.png   0.0  dusk gradient, stars, nebula, huge ringed gas giant, two small moons (opaque)
  far.png   0.12 distant floating ruins + megastructure silhouettes, low contrast, mist
  mid.png   0.3  giant bioluminescent mushrooms / alien trees with glowing spots
  near.png  0.55 closer ruins and foliage, hanging vines with glowing pods
  abyss.png 0.5  glowing toxic mist rising from below
  fg_vines.png 1.2 dark foreground vines/leaves along the top

Seamlessness: shapes are drawn on a 3x wide canvas around the middle copy and folded back
(mod 640); noise fields and terrain profiles are 640-periodic; edge lighting uses wrapped
neighbours.
"""
import math

import numpy as np

from helpers import (bayer, bezier, col, dither_alpha, ellipse_mask, fbm, fbm1d, hex_arr,
                     light_edges, new, noise1d, paste, put, rng, save_png, shift_mask, smoothstep,
                     thick_path_mask, quantize_dither)
from bg_sky import sky

W, H = 640, 360
EW = 3 * W


# ---------------------------------------------------------------------------
# fold helpers
# ---------------------------------------------------------------------------
def emask():
    return np.zeros((H, EW), bool)


def fold_mask(m):
    return m[:, :W] | m[:, W:2 * W] | m[:, 2 * W:]


def fold_img(ext):
    out = new(W, H)
    for k in range(3):
        paste(out, ext[:, k * W:(k + 1) * W], 0, 0)
    return out


def profile_mask(top):
    """Mask filled from a periodic top profile (length W) down to the bottom."""
    yy = np.arange(H)[:, None]
    return yy >= np.round(top)[None, :]


def shade_silhouette(m, body, lit, shade=None, lit_w=1, shade_w=1, fog=None):
    """Colour a folded mask: body + lit top/left edge + optional shaded bottom/right edge.
    fog = [(y, colour), ...] gives the body a dithered vertical colour gradient."""
    img = new(W, H)
    img[m] = col(body)
    if fog:
        ys = np.array([f[0] for f in fog], np.float32)
        cs = [col(f[1]) for f in fog]
        b = bayer(H, W, 4)
        for y in range(H):
            k = int(np.clip(np.searchsorted(ys, y, side="right") - 1, 0, len(fog) - 2))
            t = float(np.clip((y - ys[k]) / max(1.0, ys[k + 1] - ys[k]), 0, 1))
            row = np.where((t > b[y])[:, None], np.array(cs[k + 1]), np.array(cs[k]))
            sel = m[y]
            img[y, sel] = row[sel]
    cur = m.copy()
    for _ in range(lit_w):
        tl, br = light_edges(cur, wrap_x=True)
        up = shift_mask(cur, 0, 1, True)
        top_edge = cur & ~up
        img[top_edge | (tl & ~br)] = col(lit)
        cur = cur & ~top_edge
    if shade:
        cur = m.copy()
        for _ in range(shade_w):
            down = shift_mask(cur, 0, -1, True)
            bot = cur & ~down
            img[bot] = col(shade)
            cur = cur & ~bot
    return img


def glow_dot(ext, x, y, core, halo, r=1):
    """Glowing spot: bright core and a dithered halo ring (pixel-art glow)."""
    x, y = int(round(x)), int(round(y))
    for dy in range(-r - 2, r + 3):
        for dx in range(-r - 2, r + 3):
            d = math.hypot(dx, dy)
            xx, yy = x + dx, y + dy
            if not (0 <= xx < ext.shape[1] and 0 <= yy < H):
                continue
            if d <= r - 0.3:
                ext[yy, xx] = col(core)
            elif d <= r + 0.8:
                ext[yy, xx] = col(halo)
            elif d <= r + 1.8 and (xx + yy) % 2 == 0 and ext[yy, xx, 3] == 0:
                ext[yy, xx] = col(halo, 150)


# ---------------------------------------------------------------------------
# far: floating ruins, megastructure, distant cliffs, mist
# ---------------------------------------------------------------------------
FAR_BODY, FAR_LIT, FAR_SHADE = "#222b54", "#2e3b69", "#1d2449"
FAR_LIGHT = "#4d7f9e"


def island_mask(m, cx, ty, w, depth, seed):
    r = rng(seed)
    n = len(range(int(cx - w / 2), int(cx + w / 2)))
    jag = r.random(n)
    for i, x in enumerate(range(int(cx - w / 2), int(cx + w / 2))):
        u = abs(x + 0.5 - cx) / (w / 2)
        top = ty + int(round(u ** 3 * 3 + jag[i] * 1.2))
        bot = ty + int(round((1 - u ** 1.3) * depth + 2 + jag[i] * depth * 0.15))
        m[top:bot, x + W] = True


def far():
    m = emask()
    # distant cliffs / mesas
    top = 268 + 34 * fbm1d(W, 160, 4, seed=301)
    mesa = noise1d(W, 64, seed=302)
    top = np.where(mesa > 0.6, np.floor(top / 6) * 6 - 8, top)
    base = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= base
    # megastructure: a colossal broken ring gate standing on the cliffs
    yy, xx = np.mgrid[0:H, 0:EW]
    cx, cy = 455 + W, 318
    d = np.sqrt((xx + 0.5 - cx) ** 2 + (yy + 0.5 - cy) ** 2)
    ang = np.degrees(np.arctan2(cy - (yy + 0.5), xx + 0.5 - cx))
    ring = (d >= 150) & (d <= 160) & (yy < cy)
    ring &= ~((ang > 52) & (ang < 61)) & ~((ang > 122) & (ang < 128))
    ring |= (d >= 150) & (d <= 154) & (ang > 52) & (ang < 56) & (yy < cy)
    m |= ring
    # ring gate segments ribs
    ribs = (d >= 146) & (d <= 164) & (yy < cy) & ((np.round(ang) % 15) == 0)
    m |= ribs & ~((ang > 50) & (ang < 63)) & ~((ang > 120) & (ang < 130))
    # spires
    for (sx, sy, sw) in [(88, 150, 7), (112, 190, 5), (604, 120, 9), (626, 172, 5), (262, 200, 6)]:
        for y in range(sy, H):
            t = (y - sy) / 40.0
            ww = sw if t > 1 else max(1, int(round(sw * (0.3 + 0.7 * t))))
            m[y, W + sx - ww // 2: W + sx - ww // 2 + ww] = True
        m[sy - 8:sy, W + sx] = True              # antenna
        m[sy + 18, W + sx - sw // 2 - 3: W + sx + sw // 2 + 4] = True   # crossbar
    # floating islands with tiny ruins
    islands = [(60, 172, 48, 26, 1), (236, 132, 64, 34, 2), (372, 214, 38, 20, 3),
               (548, 108, 54, 30, 4), (300, 250, 26, 14, 5), (150, 236, 30, 16, 6)]
    for (ix, iy, iw, dp, sd) in islands:
        island_mask(m, ix, iy, iw, dp, 400 + sd)
        r = rng(500 + sd)
        for k in range(r.integers(2, 4)):
            px = int(ix - iw / 3 + r.random() * iw * 0.66)
            ph = int(4 + r.random() * 10)
            m[iy - ph:iy + 1, W + px:W + px + 2] = True
        if iw > 45:   # small arch
            ax = ix + 6
            m[iy - 9:iy + 1, W + ax:W + ax + 2] = True
            m[iy - 9:iy + 1, W + ax + 7:W + ax + 9] = True
            m[iy - 11:iy - 8, W + ax:W + ax + 9] = True
    fm = fold_mask(m)
    img = shade_silhouette(fm, FAR_BODY, FAR_LIT, FAR_SHADE,
                           fog=[(0, FAR_BODY), (230, FAR_BODY), (300, "#272f5c"), (360, "#2d3466")])
    # faint lights in the distance
    ext = new(EW, H)
    for (x, y) in [(88 + W, 152), (604 + W, 124), (236 + W, 128), (548 + W, 104), (455 + W, 170),
                   (380 + W, 300), (60 + W, 168), (520 + W, 290)]:
        put(ext, x, y, FAR_LIGHT)
    paste(img, fold_img(ext), 0, 0)
    # mist bands (in front of the silhouettes)
    n = fbm(W, H, 128, 4, seed=310, wrap_x=True, cell_y=32)
    yy = np.arange(H)[:, None]
    band = np.exp(-((yy - 292) / 26.0) ** 2) * 0.75 + np.exp(-((yy - 205) / 10.0) ** 2) * 0.35
    a = band * smoothstep(0.35, 0.7, n)
    mist_a = dither_alpha(a, levels=(0, 64, 110, 150))
    mist = new(W, H)
    mist[..., :3] = col("#3e4a84")[:3]
    mist[..., 3] = mist_a
    paste(img, mist, 0, 0)
    # fade the very bottom into haze so the abyss layer blends
    return img


# ---------------------------------------------------------------------------
# mid: giant bioluminescent mushrooms and alien trees
# ---------------------------------------------------------------------------
MID_BODY, MID_LIT, MID_SHADE = "#121a36", "#22335c", "#0d1329"
GLOW_HALO_C = "#1b5a6a"
GLOW_HALO_V = "#3d2470"


def mid():
    m = emask()
    glow = new(EW, H)
    top = 306 + 16 * fbm1d(W, 128, 4, seed=601)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    mushrooms = [(70, 175, 92, 30, 10, 1, "c"), (258, 128, 66, 22, -7, 2, "v"),
                 (428, 205, 124, 38, 12, 3, "c"), (588, 112, 58, 20, -5, 4, "c"),
                 (180, 60, 30, 10, 3, 5, "v")]
    for (mx, ht, cw, ch, bend, sd, kind) in mushrooms:
        r = rng(700 + sd)
        bx = mx + W
        gy = int(top[mx % W]) + 2
        cxp, cyp = bx + bend, gy - ht
        pts = bezier((bx, gy), (bx - bend * 0.8, gy - ht * 0.55), (cxp, cyp + ch * 0.3), 60)
        sw = max(3.0, ht / 16.0)
        m |= thick_path_mask(EW, H, pts, [sw * (1.0 - 0.35 * i / 59) for i in range(60)])
        # flared base
        m |= ellipse_mask(EW, H, bx, gy, sw * 2.2, sw * 0.9)
        # cap: dome with a concave underside
        ux = (xx + 0.5 - cxp) / (cw / 2)
        dome = (ux ** 2 + ((yy + 0.5 - cyp) / ch) ** 2 <= 1) & (yy + 0.5 <= cyp)
        droop = (np.abs(ux) <= 1.0) & (yy + 0.5 > cyp) & \
            (yy + 0.5 <= cyp + 2 + ch * 0.45 * ux ** 4) & (yy + 0.5 <= cyp + ch * 0.5)
        cap = dome | droop
        m |= cap
        core, halo = ("#39f0d8", GLOW_HALO_C) if kind == "c" else ("#b04cff", GLOW_HALO_V)
        bright = "#8ffff0" if kind == "c" else "#e08aff"
        # gills glow along the underside rim
        ys_rim = []
        for x in range(int(cxp - cw / 2) + 2, int(cxp + cw / 2) - 1):
            col_ = np.where(cap[:, x])[0]
            if len(col_):
                yb = col_.max()
                ys_rim.append((x, yb))
                if x % 3 == 0:
                    put(glow, x, yb, halo)
                if x % 3 == 1:
                    put(glow, x, yb, core if x % 6 == 1 else halo)
        # glowing spots on the cap
        for _ in range(int(cw / 7)):
            for _try in range(20):
                sx = cxp + (r.random() - 0.5) * cw * 0.8
                sy = cyp - r.random() * ch * 0.8
                if cap[int(sy), int(sx)] and cap[int(sy) - 3, int(sx)] and cap[int(sy) + 2, int(sx)]:
                    glow_dot(glow, sx, sy, bright if r.random() < 0.3 else core, halo,
                             r=1 if r.random() < 0.7 else 2)
                    break
        # hanging glow strands
        for k in range(max(2, int(cw / 22))):
            x, yb = ys_rim[int(r.random() * len(ys_rim))]
            ln = int(6 + r.random() * 18)
            for i in range(ln):
                m[yb + i, x] = True
            glow_dot(glow, x, yb + ln, core, halo, r=1)
        # spots on the stem
        for k in range(3):
            p = pts[int(10 + r.random() * 35)]
            put(glow, p[0] - 1, p[1], halo)
    # alien trees: twisted trunks with bulb canopies and violet pods
    trees = [(150, 120, 2), (342, 150, 3), (512, 100, 4), (24, 90, 5)]
    for (tx, ht, sd) in trees:
        r = rng(800 + sd)
        bx = tx + W
        gy = int(top[tx % W]) + 2
        trunk = bezier((bx, gy), (bx + 14, gy - ht * 0.5), (bx - 4, gy - ht), 50)
        m |= thick_path_mask(EW, H, trunk, [4.5 - 2.5 * i / 49 for i in range(50)])
        ex, ey = trunk[-1]
        for (ang, ln) in [(-140, ht * 0.35), (-60, ht * 0.4), (-95, ht * 0.25)]:
            a = math.radians(ang + (r.random() - 0.5) * 20)
            px, py = ex + math.cos(a) * ln, ey + math.sin(a) * ln
            br = bezier((ex, ey), ((ex + px) / 2, (ey + py) / 2 - 6), (px, py), 24)
            m |= thick_path_mask(EW, H, br, [2.2 - 1.2 * i / 23 for i in range(24)])
            rad = 8 + r.random() * 7
            blob = ellipse_mask(EW, H, px, py - rad * 0.3, rad * 1.25, rad * 0.85)
            for j in range(3):
                ox = (r.random() - 0.5) * rad * 1.8
                oy = -r.random() * rad * 0.7
                blob |= ellipse_mask(EW, H, px + ox, py - rad * 0.3 + oy, rad * 0.75, rad * 0.6)
            m |= blob
            # pods hanging under the canopy
            for k in range(4):
                qx = px + (r.random() - 0.5) * rad * 2
                col_ = np.where(blob[:, int(qx)])[0]
                if len(col_):
                    qy = col_.max() + 2
                    m[col_.max():qy, int(qx)] = True
                    glow_dot(glow, qx, qy + 1, "#e08aff" if k == 0 else "#b04cff", GLOW_HALO_V, r=1)
    # small glowing bulbs along the ground
    r = rng(901)
    for k in range(26):
        x = int(r.random() * W)
        y = int(top[x]) - 1
        h = int(2 + r.random() * 5)
        for i in range(h):
            m[y - i, x + W] = True
        glow_dot(glow, x + W, y - h, "#39f0d8" if k % 3 else "#b04cff",
                 GLOW_HALO_C if k % 3 else GLOW_HALO_V, r=0 if k % 2 else 1)
    fm = fold_mask(m)
    img = shade_silhouette(fm, MID_BODY, MID_LIT, MID_SHADE,
                           fog=[(0, MID_BODY), (200, MID_BODY), (290, "#15223f"), (360, "#1a2d4a")])
    gl = fold_img(glow)
    # halos only over the body, cores everywhere
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 90)
    paste(img, gl, 0, 0)
    # thin haze over the lower part (atmospheric depth)
    return img


# ---------------------------------------------------------------------------
# near: closer ruins and foliage + hanging vines
# ---------------------------------------------------------------------------
NEAR_BODY, NEAR_LIT, NEAR_SHADE = "#0a0f1c", "#1b2742", "#070a14"


def frond_mask(m, base, tip, ctrl, width, leaf=5, seed=0):
    r = rng(seed)
    pts = bezier(base, ctrl, tip, 40)
    m |= thick_path_mask(EW, H, pts, [width * (1 - 0.7 * i / 39) for i in range(40)])
    for i in range(4, 38, 3):
        x, y = pts[i]
        nx, ny = pts[i + 1]
        dx, dy = nx - x, ny - y
        L = math.hypot(dx, dy) or 1
        px, py = -dy / L, dx / L
        ln = leaf * (1 - i / 45.0) * (0.8 + 0.4 * r.random())
        for s in (-1, 1):
            lp = bezier((x, y), (x + px * s * ln * 0.6 + dx / L * ln * 0.3, y + py * s * ln * 0.6 + 2),
                        (x + px * s * ln + dx / L * ln * 0.5, y + py * s * ln + ln * 0.5), 10)
            m |= thick_path_mask(EW, H, lp, [1.1 - 0.6 * j / 9 for j in range(10)])
    return pts


def near():
    m = emask()
    glow = new(EW, H)
    top = 334 + 10 * fbm1d(W, 96, 4, seed=1001)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    # ruined wall with arched windows
    wx0, wx1, wy = 70 + W, 220 + W, 246
    wall = (xx >= wx0) & (xx < wx1) & (yy >= wy)
    steps = [0, 0, 0, 0, 0, 2, 2, 2, 6, 6, 4, 10, 10, 16, 16, 22, 22, 30, 30, 30]
    for i, x in enumerate(range(wx0, wx1)):
        cut = steps[min(len(steps) - 1, i * len(steps) // (wx1 - wx0))]
        wall[wy:wy + cut, x] = False
    # block courses (thin lit joints) are added after folding
    for (ax, aw) in [(98, 22), (150, 22)]:
        acx = ax + W + aw / 2
        hole = ((xx >= ax + W) & (xx < ax + W + aw) & (yy >= 278) & (yy < 330)) | \
               ellipse_mask(EW, H, acx, 278, aw / 2, aw / 2)
        wall &= ~hole
    m |= wall
    # amber tech lights on the wall
    for (x, y) in [(84, 262), (84, 270), (136, 262), (190, 300), (205, 262)]:
        glow_dot(glow, x + W, y, "#ffb347", "#6a3f1a", r=0)
    put(glow, 136 + W, 263, "#ffd98a")
    # broken columns
    for (cx, ch, cw, lean) in [(292, 92, 14, 0), (322, 58, 12, 0), (566, 118, 16, 4)]:
        bx = cx + W
        gy = int(top[cx % W]) + 2
        for y in range(gy - ch, gy):
            t = (gy - y) / ch
            off = int(round(lean * t))
            m[y, bx - cw // 2 + off:bx + cw // 2 + off] = True
        m[gy - ch - 3:gy - ch, bx - cw // 2 + lean:bx + 2 + lean] = True
        m[gy - ch - 6:gy - ch - 3, bx - cw // 2 + lean:bx - cw // 2 + lean + 2] = True
        # capital / base flare
        m[gy - 6:gy, bx - cw // 2 - 3:bx + cw // 2 + 3] = True
        glow_dot(glow, bx - 2 + lean // 2, gy - ch // 2, "#39f0d8", "#135c50", r=0)
    # big fern / leaf silhouettes
    seeds = 0
    for (fx, specs) in [(250, [((-30, -70), (-8, -60), 3.2), ((22, -80), (6, -60), 3.0),
                               ((-4, -95), (-10, -50), 3.2)]),
                        (440, [((-40, -60), (-16, -58), 3.0), ((30, -66), (14, -56), 3.0)]),
                        (480, [((-12, -88), (2, -50), 3.4), ((26, -50), (16, -44), 2.6)]),
                        (628, [((-26, -76), (-6, -58), 3.0), ((18, -64), (8, -52), 2.8)]),
                        (16, [((20, -54), (8, -40), 2.6)])]:
        bx = fx + W
        gy = int(top[fx % W]) + 4
        for (tip, ctrl, wd) in specs:
            seeds += 1
            frond_mask(m, (bx, gy), (bx + tip[0], gy + tip[1]), (bx + ctrl[0], gy + ctrl[1]), wd,
                       leaf=7, seed=1100 + seeds)
    # tall grass blades
    r = rng(1200)
    for k in range(70):
        x = r.random() * W
        gy = top[int(x) % W] + 3
        hgt = 8 + r.random() * 22
        lean = (r.random() - 0.5) * 12
        pts = bezier((x + W, gy), (x + W, gy - hgt * 0.6), (x + W + lean, gy - hgt), 12)
        m |= thick_path_mask(EW, H, pts, [1.2 - 0.7 * i / 11 for i in range(12)])
    # glowing pods on stems among the foliage
    for k, (x, hgt) in enumerate([(232, 40), (268, 26), (410, 34), (500, 22), (600, 30), (40, 28),
                                  (350, 18), (520, 44)]):
        gy = int(top[x % W]) + 2
        pts = bezier((x + W, gy), (x + W + 4, gy - hgt * 0.5), (x + W - 2, gy - hgt), 16)
        m |= thick_path_mask(EW, H, pts, 0.7)
        ex, ey = pts[-1]
        core = "#39f0d8" if k % 3 else "#b04cff"
        halo = "#135c50" if k % 3 else "#3d2470"
        glow_dot(glow, ex, ey - 2, core, halo, r=2)
        put(glow, ex - 1, ey - 3, "#8ffff0" if k % 3 else "#e08aff")
    # hanging vines from the top
    r = rng(1300)
    for k, (vx, ln) in enumerate([(176, 120), (402, 54), (540, 88)]):
        bx = vx + W
        pts = [(bx + 4 * math.sin(y / 17.0 + k) * min(1, y / 30), y) for y in range(0, ln)]
        m |= thick_path_mask(EW, H, pts[::2], 1.0)
        for i in range(8, ln - 4, 9):
            x, y = pts[i]
            s = 1 if (i // 9) % 2 else -1
            leafm = ellipse_mask(EW, H, x + s * 3, y + 1, 3, 1.5)
            m |= leafm
        x, y = pts[-1]
        if k % 2 == 0:
            glow_dot(glow, x, y + 3, "#39f0d8", "#135c50", r=2)
            put(glow, x - 1, y + 2, "#e6fffb")
        else:
            glow_dot(glow, x, y + 2, "#8ffff0", "#135c50", r=1)
        for i in range(30, ln - 10, 37):
            glow_dot(glow, pts[i][0] + 1, pts[i][1], "#1fb8b0", "#0f3b3a", r=0)
    fm = fold_mask(m)
    img = shade_silhouette(fm, NEAR_BODY, NEAR_LIT, NEAR_SHADE)
    # masonry joints on the ruined wall (faint lit lines)
    for y in range(254, 340, 9):
        for x in range(72, 218):
            if fm[y, x] and fm[y - 1, x] and fm[y + 1, x] and (x + (y // 9) * 7) % 22 != 0:
                img[y, x] = col("#111a2e")
        for x in range(72 + (y // 9 % 2) * 11, 218, 22):
            for yy_ in range(y + 1, y + 9):
                if 0 <= yy_ < H and fm[yy_, x] and fm[yy_, x - 1] and fm[yy_, x + 1]:
                    img[yy_, x] = col("#111a2e")
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 110)
    paste(img, gl, 0, 0)
    return img


# ---------------------------------------------------------------------------
# abyss: toxic glowing mist rising from below
# ---------------------------------------------------------------------------
ABYSS_PAL = ["#0f3b2a", "#1d5a2a", "#2f7a1a", "#4fbf26", "#9dff3a", "#e4ffb0", "#1fb8b0",
             "#39f0d8", "#135c50", "#8ffff0"]


def abyss():
    yy = np.arange(H, dtype=np.float32)[:, None] * np.ones((1, W), np.float32)
    n1 = fbm(W, H, 128, 5, seed=1401, wrap_x=True, cell_y=96)
    n2 = fbm(W, H, 64, 4, seed=1402, wrap_x=True, cell_y=160)
    base = smoothstep(190, 360, yy) ** 2.2
    wisps = smoothstep(0.38, 0.85, n1 + (yy - 260) / 400.0)
    a = base * (0.25 + 0.95 * wisps)
    a = np.where(yy > 346, np.maximum(a, 0.9), a)
    a = np.clip(a, 0, 1)
    # colour: dark lime/teal high up -> bright lime -> pale near the bottom; cyan veins
    t = smoothstep(250, 360, yy) * (0.55 + 0.5 * n1)
    c_low = hex_arr(["#2f7a1a"])[0]
    c_mid = hex_arr(["#4fbf26"])[0]
    c_hi = hex_arr(["#9dff3a"])[0]
    c_top = hex_arr(["#e4ffb0"])[0]
    rgb = np.zeros((H, W, 3), np.float32)
    for i in range(3):
        rgb[..., i] = np.interp(t, [0, 0.45, 0.75, 1.0], [c_low[i], c_mid[i], c_hi[i], c_top[i]])
    # thin cyan filaments (ridged noise) drifting through the mist
    ridge = 1 - np.abs(n2 - 0.5) / 0.035
    cy = np.clip(ridge, 0, 1) * smoothstep(230, 300, yy) * (1 - smoothstep(335, 360, yy))
    cyan = hex_arr(["#39f0d8"])[0]
    rgb = rgb * (1 - cy[..., None]) + cyan * cy[..., None]
    a = np.maximum(a, cy * 0.7)
    img = quantize_dither(rgb, ABYSS_PAL)
    img[..., 3] = dither_alpha(a, levels=(0, 40, 80, 128, 176, 224, 255))
    # rising spores / bubbles
    r = rng(1403)
    for k in range(140):
        x = int(r.random() * W)
        y = int(230 + (r.random() ** 0.6) * 130)
        c = ["#e4ffb0", "#9dff3a", "#8ffff0"][k % 3]
        put(img, x, y, c)
        if k % 5 == 0:
            put(img, (x + 1) % W, y, col(c, 160))
            put(img, x, y - 1, col(c, 160))
    img[:170] = 0
    return img


# ---------------------------------------------------------------------------
# fg_vines: dark foreground canopy along the top
# ---------------------------------------------------------------------------
FG_BODY, FG_LIT = "#05080f", "#111a30"


def fg_vines():
    m = emask()
    glow = new(EW, H)
    prof = 4 + 26 * fbm1d(W, 80, 4, seed=1501) ** 1.6
    for k in range(3):
        for x in range(W):
            m[:int(prof[x]), k * W + x] = True
    r = rng(1502)
    # leaf clusters hanging from the canopy edge
    for k in range(70):
        x = r.random() * W
        y = prof[int(x)] - 2
        ln = 6 + r.random() * 12
        ang = math.radians(70 + (r.random() - 0.5) * 80)
        tip = (x + W + math.cos(ang) * ln, y + math.sin(ang) * ln)
        mid = (x + W + math.cos(ang) * ln * 0.5 + 2, y + math.sin(ang) * ln * 0.5)
        pts = bezier((x + W, y), mid, tip, 14)
        m |= thick_path_mask(EW, H, pts, [2.6 * math.sin(math.pi * (0.15 + 0.85 * i / 13)) + 0.4
                                          for i in range(14)])
    # big leaf clumps hanging lower in a few places (broad drooping leaves)
    for (cx, cy, rx, ry) in [(60, 18, 30, 12), (250, 14, 22, 9), (420, 20, 34, 13),
                             (575, 16, 26, 10)]:
        m |= ellipse_mask(EW, H, cx + W, cy, rx, ry)
        for j in range(6):
            u = -0.85 + 1.7 * j / 5 + (r.random() - 0.5) * 0.2
            bx = cx + W + u * rx * 0.85
            by = cy + ry * 0.55 * math.sqrt(max(0.0, 1 - u * u))
            ln = ry * (1.1 + r.random() * 0.8)
            out = u * 0.9
            pts = bezier((bx, by), (bx + out * ln * 0.5, by + ln * 0.55),
                         (bx + out * ln * 0.75 + (r.random() - 0.5) * 3, by + ln), 14)
            m |= thick_path_mask(EW, H, pts, [3.6 * math.sin(math.pi * (0.12 + 0.88 * i / 13)) ** 0.8
                                              + 0.4 for i in range(14)])
    # long vines (irregular leaves)
    for k, (vx, ln) in enumerate([(40, 150), (150, 210), (232, 64), (352, 120), (440, 236),
                                  (530, 90), (604, 170)]):
        bx = vx + W
        y0 = int(prof[vx % W]) - 2
        ph = r.random() * 6
        pts = [(bx + 6 * math.sin(y / (19.0 + k * 3) + ph) * min(1, y / 40), y0 + y)
               for y in range(ln)]
        m |= thick_path_mask(EW, H, pts[::2], [1.7 - 0.8 * i / (ln / 2) for i in range(len(pts[::2]))])
        i = 6
        while i < ln - 4:
            x, y = pts[i]
            s_ = 1 if r.random() < 0.5 else -1
            sz = 5 + r.random() * 5
            leaf = bezier((x, y), (x + s_ * sz * 0.5, y - 2), (x + s_ * sz, y + sz * 0.5), 10)
            m |= thick_path_mask(EW, H, leaf, [1.9 * math.sin(math.pi * (0.2 + 0.8 * j / 9)) + 0.3
                                               for j in range(10)])
            i += int(7 + r.random() * 14)
        if k % 2 == 0:
            x, y = pts[-1]
            glow_dot(glow, x, y + 2, "#1fb8b0", "#0f3b3a", r=1)
    fm = fold_mask(m)
    img = shade_silhouette(fm, FG_BODY, FG_LIT)
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 120)
    paste(img, gl, 0, 0)
    return img


LAYERS = [("sky", sky), ("far", far), ("mid", mid), ("near", near), ("abyss", abyss),
          ("fg_vines", fg_vines)]
PARALLAX = {"sky": 0.0, "far": 0.12, "mid": 0.3, "near": 0.55, "abyss": 0.5, "fg_vines": 1.2}


def generate():
    out = []
    for name, fn in LAYERS:
        img = fn()
        assert img.shape == (H, W, 4)
        out.append(save_png(img, f"backgrounds/sylvanox/{name}.png"))
    return out
