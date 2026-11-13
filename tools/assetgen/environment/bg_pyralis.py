"""Parallax backgrounds for Pyralis, "La forge en fusion" (640x360, horizontally seamless).

  sky.png        0.0   smoky crimson sky, ash clouds lit from below, a shattered moon with a
                       molten core and floating fragments, a small crescent moon (opaque)
  far.png        0.12  distant volcanoes with lava rivulets, an eruption plume, colossal forge
                       chimneys, red haze
  mid.png        0.3   ruined forges: chimney stacks, anvil mesa, gantry crane with chains,
                       basalt column clusters, molten falls
  near.png       0.55  close basalt crags, half-buried giant gear, furnace, pipes, draped chain,
                       ember plants
  abyss.png      0.5   lava glow rising from below (transparent at the top), embers
  fg_embers.png  1.2   foreground: craggy ceiling with hanging chains, smoke wisps, embers

Same construction as the Sylvanox layers (backgrounds.py): shapes are drawn on a 3x wide
canvas and folded back mod 640, noise fields and profiles are 640-periodic.
"""
import math

import numpy as np

from backgrounds import (EW, H, W, emask, fold_img, fold_mask, glow_dot, profile_mask,
                         shade_silhouette)
from helpers import (apply_ramp, bayer, bezier, col, dither_alpha, ellipse_mask, fbm, fbm1d,
                     gradient_rgb, hex_arr, new, noise1d, paste, poly_mask, put, quantize_dither,
                     rng, save_png, smoothstep, thick_path_mask, value_noise)

LAVA = ["#b3201a", "#ff4d1e", "#ff8a3d", "#ffc56b", "#fff1c2"]


# ---------------------------------------------------------------------------
# shared drawing helpers
# ---------------------------------------------------------------------------
def chain_mask(m, x, y0, length, link=4, sway=0.0, phase=0.0, width=1):
    """Hanging chain: alternating open rings and edge-on links (mask pixels)."""
    y = y0
    k = 0
    while y < y0 + length:
        cx = x + sway * math.sin((y - y0) / 23.0 + phase) * min(1.0, (y - y0) / 30.0)
        ix = int(round(cx))
        if k % 2 == 0:
            # ring seen face-on
            for dy in range(link):
                if 0 <= y + dy < H:
                    if dy in (0, link - 1):
                        m[y + dy, ix - width + 1:ix + width] = True
                    else:
                        m[y + dy, ix - width] = True
                        m[y + dy, ix + width] = True
        else:
            for dy in range(link - 1):
                if 0 <= y + dy < H:
                    m[y + dy, ix] = True
        y += link - 1
        k += 1
    return int(round(x + sway * math.sin(length / 23.0 + phase))), y0 + length


def hex_column(m, cx, top, bottom, w):
    """Basalt column silhouette with a slanted hexagonal top."""
    for x in range(int(cx - w / 2), int(cx + w / 2)):
        u = (x + 0.5 - cx) / (w / 2)
        t = top + int(round(abs(u) * 2))
        m[max(0, t):bottom, x] = True


# ---------------------------------------------------------------------------
# sky
# ---------------------------------------------------------------------------
SKY_STOPS = [(0.0, "#0a0406"), (0.2, "#12080c"), (0.4, "#1c0c12"), (0.55, "#281017"),
             (0.66, "#36131a"), (0.76, "#4a181b"), (0.85, "#66201b"), (0.92, "#86301c"),
             (1.0, "#a2421e")]
SMOKE_D = "#170a0e"
SMOKE_M = "#26111a"
SMOKE_L = "#4e1e1c"
SMOKE_H = "#7a321e"


def sky_palette():
    g = gradient_rgb(40, SKY_STOPS)
    pal = ["#%02x%02x%02x" % tuple(int(v) for v in c) for c in g]
    pal += [SMOKE_D, SMOKE_M, "#30141c", SMOKE_L, "#62281e", SMOKE_H, "#9a4220"]
    return pal


def shattered_moon(img, cx, cy, R, seed=31):
    L = np.array([-0.7, -0.45, 0.55])
    L /= np.linalg.norm(L)
    x0, y0 = int(cx - R * 2.2), int(cy - R * 1.6)
    x1, y1 = int(cx + R * 2.2), int(cy + R * 1.8)
    bw, bh = x1 - x0, y1 - y0
    yy, xx = np.mgrid[y0:y1, x0:x1]
    dx = (xx + 0.5 - cx) / R
    dy = (yy + 0.5 - cy) / R
    r2 = dx * dx + dy * dy
    inside = r2 <= 1
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    lam = np.clip(dx * L[0] + dy * L[1] + nz * L[2], 0, 1)
    n = value_noise(bw, bh, 10, 10, seed=seed, wrap_x=False)
    n2 = value_noise(bw, bh, 5, 5, seed=seed + 1, wrap_x=False)
    ang = np.arctan2(dy, dx)
    # the break: a jagged plane cutting off the lower-right part of the sphere
    jag = 0.05 * np.sin(ang * 5 + 1.0) + 0.04 * np.sin(ang * 11 + 2.0) + 0.06 * (n2 - 0.5)
    cutv = dx * 0.62 + dy * 0.78
    gone = inside & (cutv > 0.3 + jag)
    rim = inside & ~gone & (cutv > 0.3 + jag - 0.1)
    body = inside & ~gone
    tone = 0.1 + 0.9 * lam ** 0.85
    # craters
    for (ux, uy, ur) in [(-0.45, -0.3, 0.2), (-0.1, -0.6, 0.12), (0.1, 0.05, 0.16),
                         (-0.6, 0.3, 0.13), (0.35, -0.35, 0.1)]:
        c = ((dx - ux) ** 2 + (dy - uy) ** 2) <= ur * ur
        lit_rim = ((dx - ux - ur * 0.3) ** 2 + (dy - uy - ur * 0.3) ** 2) <= ur * ur
        tone = np.where(c & ~lit_rim, tone * 0.7, tone)
        tone = np.where(c & lit_rim, np.minimum(1, tone * 1.08), tone)
    tone = np.clip(tone + (n - 0.5) * 0.15, 0, 1)
    ramp = ["#120a0e", "#1e1216", "#2e1e20", "#42302e", "#5a423c", "#76584c", "#96725e"]
    out = img[y0:y1, x0:x1]
    surf = apply_ramp(tone, ramp, bayer(bh, bw, 4, x0, y0), 0.8)
    out[body] = surf[body]
    # thin glowing fissures (ridged noise), denser towards the break
    cr = value_noise(bw, bh, 9, 9, seed=seed + 2, wrap_x=False)
    ridge = 1 - np.abs(cr - 0.5) * 2
    near_break = np.clip(cutv / 0.34, 0, 1)
    fis = body & (ridge > 0.955 - 0.03 * near_break) & (cutv > -0.45)
    out[fis] = col("#7a2414")
    out[fis & (ridge > 0.975) & (cutv > -0.1)] = col(LAVA[1])
    # the missing chunk reveals the molten interior: cavity walls + glowing core
    rr = np.sqrt(r2)
    cav = gone & (rr < 0.97)
    out[cav] = col("#3a0e0c")
    out[cav & (rr < 0.85)] = col("#6e1a12")
    core = gone & (rr < 0.66)
    out[core] = col(LAVA[1])
    out[gone & (rr < 0.52)] = col(LAVA[2])
    out[gone & (rr < 0.36)] = col(LAVA[3])
    out[gone & (rr < 0.2)] = col(LAVA[4])
    # dithered transitions inside the core
    b4 = bayer(bh, bw, 4, x0, y0)
    for r0, c0 in ((0.66, "#6e1a12"), (0.52, LAVA[1]), (0.36, LAVA[2]), (0.2, LAVA[3])):
        band = gone & (rr >= r0) & (rr < r0 + 0.05) & (b4 > 0.5)
        out[band] = col(c0) if r0 > 0.2 else col(LAVA[3])
    # molten rim along the break edge (lit)
    out[rim] = col(LAVA[0])
    edge = inside & ~gone & (cutv > 0.3 + jag - 0.045)
    out[edge] = col(LAVA[2])
    # floating fragments beyond the break
    r = rng(seed + 5)
    frags = [(0.95, 0.75, 0.15), (1.25, 0.45, 0.09), (0.55, 1.15, 0.12), (1.45, 1.0, 0.06),
             (1.1, 1.25, 0.07), (0.3, 1.4, 0.05), (1.6, 0.6, 0.045)]
    for (fx, fy, fr) in frags:
        pts = []
        for k in range(6):
            a = k / 6 * 2 * math.pi + r.random() * 0.6
            rr = fr * (0.7 + r.random() * 0.5)
            pts.append(((cx + (fx + math.cos(a) * rr) * R) - x0, (cy + (fy + math.sin(a) * rr) * R) - y0))
        fm = poly_mask(bw, bh, pts)
        fxx = (xx + 0.5 - (cx + fx * R)) / (fr * R)
        fyy = (yy + 0.5 - (cy + fy * R)) / (fr * R)
        lit = fm & ((fxx + fyy) < -0.25)
        molten = fm & ((fxx * 0.62 + fyy * 0.78) < -0.45) & ~lit
        out[fm] = col("#2a1a1c")
        out[lit] = col("#6a4c46")
        out[molten] = col(LAVA[0])
    img[y0:y1, x0:x1] = out
    return img


def small_moon(img, cx, cy, r):
    L = np.array([-0.8, -0.3, 0.5])
    L /= np.linalg.norm(L)
    x0, y0 = int(cx - r - 2), int(cy - r - 2)
    sz = int(2 * r + 5)
    yy, xx = np.mgrid[y0:y0 + sz, x0:x0 + sz]
    dx = (xx + 0.5 - cx) / r
    dy = (yy + 0.5 - cy) / r
    r2 = dx * dx + dy * dy
    inside = r2 <= 1
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    lam = np.clip(dx * L[0] + dy * L[1] + nz * L[2], 0, 1)
    m = apply_ramp(0.05 + 0.95 * lam ** 1.3, ["#1a0e12", "#3a2224", "#6a4840", "#a8826e", "#e0c0a0"],
                   bayer(sz, sz, 4, x0, y0), 0.7)
    sub = img[y0:y0 + sz, x0:x0 + sz]
    sub[inside] = m[inside]


def sky():
    g = gradient_rgb(H, SKY_STOPS)
    rgb = np.repeat(g[:, None, :], W, 1).astype(np.float32)
    yy = np.arange(H, dtype=np.float32)[:, None]
    # ash clouds: stretched fbm, two layers; lit from below by the lava glow
    n1 = fbm(W, H, 160, 5, seed=3101, wrap_x=True, cell_y=40)
    n2 = fbm(W, H, 80, 4, seed=3102, wrap_x=True, cell_y=24)
    band = np.exp(-((yy - 150) / 95.0) ** 2) + 0.6 * np.exp(-((yy - 285) / 30.0) ** 2)
    dens = smoothstep(0.46, 0.72, n1 * 0.75 + n2 * 0.25) * band
    below = np.roll(dens, -5, axis=0)
    under = np.clip(dens - below, 0, 1)              # bottom edges of the clouds
    above = np.roll(dens, 4, axis=0)
    topedge = np.clip(dens - above, 0, 1)
    depth = smoothstep(60, 330, yy)                  # lower clouds catch more lava light
    sd, sm, sl, sh = (hex_arr([c])[0] for c in (SMOKE_D, SMOKE_M, SMOKE_L, SMOKE_H))
    cloud = sd * (1 - depth[..., None]) + sm * depth[..., None]
    k = np.clip(dens * 1.4, 0, 1)[..., None] * 0.85
    rgb = rgb * (1 - k) + cloud * k
    lit = np.clip(under * 2.2, 0, 1) * (0.35 + 0.65 * depth)
    litc = sl * (1 - depth[..., None]) + sh * depth[..., None]
    rgb = rgb * (1 - lit[..., None] * 0.8) + litc * lit[..., None] * 0.8
    rgb = rgb * (1 - topedge[..., None] * 0.25) + hex_arr(["#30141c"])[0] * topedge[..., None] * 0.25
    img = quantize_dither(rgb, sky_palette())
    # stars only through the thin top smoke
    r = rng(3103)
    for i in range(700):
        x, y = int(r.random() * W), int(r.random() * H)
        if r.random() > (1 - y / H) ** 2.6 or dens[y, x] > 0.25:
            continue
        b = r.random()
        if b < 0.6:
            put(img, x, y, "#3a2430")
        elif b < 0.86:
            put(img, x, y, "#7a5a62")
        elif b < 0.97:
            put(img, x, y, "#d0b4aa")
        else:
            put(img, x, y, "#fff1c2")
            for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                put(img, (x + ddx) % W, y + ddy, "#8a5a50")
    small_moon(img, 112, 52, 8.5)
    shattered_moon(img, 430, 92, 46)
    # drifting embers high in the sky
    for i in range(60):
        x, y = int(r.random() * W), int(160 + r.random() * 190)
        put(img, x, y, ["#ff8a3d", "#ffc56b", "#b3201a"][i % 3])
    img[..., 3] = 255
    return img


# ---------------------------------------------------------------------------
# far: volcanoes, eruption plume, colossal chimneys, red haze
# ---------------------------------------------------------------------------
FAR_BODY, FAR_LIT, FAR_SHADE = "#220f19", "#46212a", "#1a0b13"


def volcano_profile(top, cx, peak, half_w, crater=10, seed=0):
    jag = noise1d(W, 12, seed + 1)
    for x in range(W):
        d = min(abs(x - cx), W - abs(x - cx))
        if d > half_w * 1.6:
            continue
        u = d / half_w
        y = peak + (u ** 1.35) * (H - peak) * 0.9 + jag[x] * 4
        if d < crater / 2:
            y = peak + 3 + (1 - (d / (crater / 2)) ** 2) * 4
        top[x] = min(top[x], y)
    return top


def far():
    m = emask()
    glow = new(EW, H)
    top = 286 + 26 * fbm1d(W, 160, 4, seed=3201)
    volcanoes = [(96, 150, 120, 14), (338, 196, 92, 10), (548, 176, 104, 12)]
    for k, (cx, peak, hw, cw) in enumerate(volcanoes):
        top = volcano_profile(top, cx, peak, hw, cw, seed=3210 + k)
    base = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= base
    # colossal forge chimneys standing on the plain
    for (sx, sy, sw) in [(222, 128, 12), (244, 150, 9), (452, 140, 11), (628, 168, 8)]:
        for y in range(sy, H):
            t = (y - sy) / 60.0
            ww = sw + int(round(max(0.0, t - 1) * 3))
            m[y, W + sx - ww // 2: W + sx - ww // 2 + ww] = True
        m[sy - 3:sy + 1, W + sx - sw // 2 - 2: W + sx + sw // 2 + 2] = True   # lip
        for wy in range(sy + 10, int(top[sx % W]) - 6, 9):
            put(glow, W + sx - 1, wy, "#c8401a")
            put(glow, W + sx, wy, "#8a2a16")
    fm = fold_mask(m)
    img = shade_silhouette(fm, FAR_BODY, FAR_LIT, FAR_SHADE,
                           fog=[(0, FAR_BODY), (230, FAR_BODY), (300, "#36141c"), (360, "#50201f")])
    # lava flows down the volcano flanks (meandering streams, bright near the crater)
    r = rng(3220)
    for vi, (cx, peak, hw, cw) in enumerate(volcanoes):
        glow_dot(glow, W + cx, peak + 4, LAVA[3], "#8a2a16", r=2)
        for k, side in enumerate((-1, 1) if vi != 1 else (1,)):
            x, y = cx + side * 3.0, peak + 6.0
            drift = side * (0.35 + 0.25 * r.random())
            n = int(34 + r.random() * 40)
            for i in range(n):
                y += 1.0
                drift += (r.random() - 0.5) * 0.25
                drift = max(-1.1, min(1.1, drift))
                x += drift
                tt = i / n
                core = LAVA[2] if tt < 0.25 else ("#e0602a" if tt < 0.55 else "#a8381a")
                put(glow, W + int(round(x)), int(y), core)
                if tt < 0.7:
                    put(glow, W + int(round(x)) - side, int(y), "#6a1e14")
    gl = fold_img(glow)
    paste(img, gl, 0, 0)
    # eruption plume from the tallest volcano: billowing puffs, lit from below by the crater
    pm = emask()
    r2 = rng(3231)
    cx0, peak = 96, 150
    for i in range(26):
        t = i / 25
        px = W + cx0 + 46 * t ** 1.5 + (r2.random() - 0.5) * 6 * t
        py = peak - 4 - 128 * t
        rad = 4 + 15 * t ** 0.8
        pm |= ellipse_mask(EW, H, px, py, rad * (1.1 + 0.3 * r2.random()), rad * 0.8)
    pf = fold_mask(pm)
    yy, xx = np.mgrid[0:H, 0:W]
    pl = new(W, H)
    pl[pf] = col("#24101a")
    under = pf & ~np.roll(pf, -2, axis=0)
    pl[under] = col("#4a1c1a")
    near_base = pf & (yy > peak - 40)
    pl[near_base & under] = col("#8a3018")
    fade = np.clip((yy - 20) / 110.0, 0, 1)
    a = np.where(pf, 0.55 + 0.4 * fade, 0)
    pl[..., 3] = np.where(pf, dither_alpha(a, levels=(0, 120, 180, 230)), 0)
    paste(img, pl, 0, 0)
    # haze band (in front of the silhouettes)
    yy, xx = np.mgrid[0:H, 0:W]
    hz = fbm(W, H, 128, 4, seed=3240, wrap_x=True, cell_y=32)
    band = np.exp(-((yy - 300) / 30.0) ** 2) * 0.8 + np.exp(-((yy - 230) / 12.0) ** 2) * 0.3
    ha = band * smoothstep(0.35, 0.7, hz)
    mist = new(W, H)
    mist[..., :3] = col("#5a2020")[:3]
    mist[..., 3] = dither_alpha(ha, levels=(0, 64, 110, 150))
    paste(img, mist, 0, 0)
    return img


# ---------------------------------------------------------------------------
# mid: ruined forges
# ---------------------------------------------------------------------------
MID_BODY, MID_LIT, MID_SHADE = "#170a0e", "#2c1418", "#100508"
HALO_O = "#5a1c10"


def lava_fall(glow, x, y0, y1, w=3, seed=0):
    """Glowing vertical lava fall (emissive, drawn into the glow layer)."""
    r = rng(seed)
    for y in range(int(y0), int(y1)):
        wob = int(round(math.sin(y / 9.0 + seed) * 0.8))
        for dx in range(-w // 2 - 1, w // 2 + 2):
            xx = x + dx + wob
            e = abs(dx) / (w / 2 + 1)
            if e < 0.35:
                c = LAVA[2] if (y + seed) % 9 else LAVA[3]
            elif e < 0.75:
                c = LAVA[1]
            elif e <= 1.0:
                c = "#d8341a"
            else:
                c = LAVA[0]
            put(glow, xx, y, c)
        if r.random() < 0.08:
            put(glow, x + wob + (1 if r.random() < 0.5 else -1) * (w // 2 + 2), y, LAVA[1])
    # splash at the bottom
    for k in range(9):
        a = math.pi * (0.1 + 0.8 * k / 8)
        put(glow, x - int(math.cos(a) * 5), int(y1) - int(math.sin(a) * 3), LAVA[2])
    glow_dot(glow, x, int(y1), LAVA[3], LAVA[0], r=2)


def mid():
    m = emask()
    glow = new(EW, H)
    top = 304 + 16 * fbm1d(W, 128, 4, seed=3301)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    # chimney stacks with bands, broken tops and glowing slits
    for (cx, ht, w0, w1, broken, sd) in [(60, 190, 26, 18, False, 1), (300, 150, 22, 15, True, 2),
                                        (520, 210, 30, 20, False, 3)]:
        bx = cx + W
        gy = int(top[cx % W]) + 2
        y_top = gy - ht
        for y in range(y_top, gy):
            t = (y - y_top) / ht
            ww = w1 + (w0 - w1) * t
            m[y, int(bx - ww / 2):int(bx + ww / 2)] = True
        for by in range(y_top + 14, gy - 10, 26):
            m[by:by + 3, int(bx - w1 / 2 - 3 - (by - y_top) / ht * (w0 - w1) / 2):
              int(bx + w1 / 2 + 3 + (by - y_top) / ht * (w0 - w1) / 2)] = True
        if broken:
            for x in range(int(bx - w1 / 2), int(bx + w1 / 2)):
                cut = int(6 + 8 * abs(math.sin((x - bx) * 0.7)) + ((x - bx) > 0) * 10)
                m[y_top:y_top + cut, x] = False
        else:
            m[y_top - 5:y_top, int(bx - w1 / 2 - 3):int(bx + w1 / 2 + 3)] = True
            glow_dot(glow, bx, y_top - 6, LAVA[2], HALO_O, r=1)
        for wy in range(y_top + 22, gy - 14, 13):
            for dx in (-3, 3):
                put(glow, bx + dx, wy, LAVA[1])
                put(glow, bx + dx, wy + 1, LAVA[0])
    # anvil mesa: wide top, narrow waist (a colossal ancient anvil-forge)
    ax, aw, ay = 190 + W, 92, 214
    for y in range(ay, H):
        t = (y - ay) / (H - ay)
        if t < 0.18:
            half = aw / 2
        elif t < 0.5:
            half = aw / 2 * (1 - 0.55 * smoothstep(0.18, 0.5, t))
        else:
            half = aw / 2 * (0.45 + 0.5 * (t - 0.5))
        horn = 18 if t < 0.12 else 0
        m[y, int(ax - half - horn):int(ax + half)] = True
    m[ay - 4:ay, ax - 30:ax + 20] = True
    for dx in range(-14, 12):
        put(glow, ax + dx, ay + 52, LAVA[1] if abs(dx + 1) > 8 else LAVA[2])
        put(glow, ax + dx, ay + 53, LAVA[0] if abs(dx + 1) > 8 else LAVA[1])
    # gantry crane: posts, beam, truss and hanging chains with a crucible
    gx0, gx1, gy_ = 360 + W, 470 + W, 128
    for px in (gx0, gx1):
        m[gy_:H, px - 3:px + 3] = True
    m[gy_ - 6:gy_, gx0 - 10:gx1 + 10] = True
    for i in range(0, gx1 - gx0, 12):
        for (a, b) in [((gx0 + i, gy_), (gx0 + i + 12, gy_ + 10)),
                       ((gx0 + i + 12, gy_), (gx0 + i, gy_ + 10))]:
            m |= thick_path_mask(EW, H, [a, b], 0.6) & (yy >= gy_) & (yy <= gy_ + 10)
    m[gy_ + 10:gy_ + 12, gx0:gx1] = True
    for (cxh, ln) in [(gx0 + 30, 70), (gx0 + 72, 44), (gx0 + 86, 96)]:
        ex, ey = chain_mask(m, cxh, gy_ + 12, ln, link=4, sway=1.5, phase=cxh * 0.1)
        if ln == 70:
            # crucible with molten metal
            m |= ellipse_mask(EW, H, ex, ey + 6, 9, 7) & (yy >= ey + 2)
            m[ey:ey + 3, ex - 9:ex + 10] = True
            for dx in range(-6, 7):
                put(glow, ex + dx, ey + 1, LAVA[2] if abs(dx) < 4 else LAVA[1])
            glow_dot(glow, ex, ey, LAVA[3], HALO_O, r=1)
        else:
            m |= thick_path_mask(EW, H, bezier((ex, ey), (ex + 6, ey + 6), (ex, ey + 10), 12), 1.0)
    # basalt column clusters
    r = rng(3310)
    for (cx, n, h0) in [(118, 6, 70), (430, 4, 50), (610, 5, 86)]:
        for i in range(n):
            x = cx + (i - n / 2) * 7
            h = h0 * (0.5 + 0.5 * math.sin(i * 1.7 + cx)) + 14
            gy = int(top[int(x) % W]) + 4
            hex_column(m, W + x, int(gy - h), gy, 7)
    # molten falls pouring from the chimneys / cliffs
    lava_fall(glow, 300 + W + 2, int(top[300] - 120), int(top[300]) + 2, w=3, seed=4)
    lava_fall(glow, 582 + W, 196, int(top[582]) + 2, w=2, seed=7)
    m[192:198, 572 + W:592 + W] = True   # spout lip
    # small glowing ember bulbs along the ground
    for k in range(22):
        x = int(r.random() * W)
        y = int(top[x]) - 1
        h = int(2 + r.random() * 5)
        for i in range(h):
            m[y - i, x + W] = True
        glow_dot(glow, x + W, y - h, LAVA[2] if k % 3 else LAVA[3], HALO_O, r=0 if k % 2 else 1)
    fm = fold_mask(m)
    img = shade_silhouette(fm, MID_BODY, MID_LIT, MID_SHADE,
                           fog=[(0, MID_BODY), (190, MID_BODY), (280, "#1e0c10"), (360, "#2e1214")])
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 90)
    paste(img, gl, 0, 0)
    return img


# ---------------------------------------------------------------------------
# near: crags, giant gear, furnace, pipes, draped chain, ember plants
# ---------------------------------------------------------------------------
NEAR_BODY, NEAR_LIT, NEAR_SHADE = "#0c0406", "#2a1210", "#070203"


def gear_mask(m, cx, cy, R, teeth=12, hub=0.3, spokes=5):
    yy, xx = np.mgrid[0:H, 0:EW]
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    d = np.sqrt(dx * dx + dy * dy)
    ang = np.arctan2(dy, dx)
    tooth = (np.cos(ang * teeth) > 0.2)
    ring = (d <= R * 0.86) & (d >= R * 0.66)
    teeth_m = (d > R * 0.86) & (d <= R) & tooth
    hubm = d <= R * hub
    sp = np.zeros_like(ring)
    for k in range(spokes):
        a = k / spokes * 2 * math.pi + 0.3
        sx, sy = math.cos(a), math.sin(a)
        along = dx * sx + dy * sy
        perp = np.abs(-dx * sy + dy * sx)
        sp |= (along > 0) & (along < R * 0.7) & (perp < R * 0.07 + 1)
    m |= ring | teeth_m | hubm | sp
    return m


def near():
    m = emask()
    glow = new(EW, H)
    top = 334 + 10 * fbm1d(W, 96, 4, seed=3401)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    # jagged basalt crags (multi-peak profile + stepped columns at the foot)
    r = rng(3402)
    for ci, (cx, ht, wd) in enumerate([(30, 120, 34), (262, 84, 26), (402, 140, 40), (600, 96, 30)]):
        gy = int(top[cx % W]) + 4
        jag = noise1d(2 * wd + 1, 5, 3410 + ci, wrap=False)
        peaks = [(r.random() * 1.6 - 0.8, 0.3 + 0.7 * r.random(), 0.12 + 0.12 * r.random())
                 for _ in range(3)]
        for i in range(2 * wd + 1):
            u = (i - wd) / wd
            h = ht * max(0.0, 1 - abs(u - 0.1) ** 1.3) * (0.55 + 0.25 * jag[i])
            for (pu, ph, pw) in peaks:
                h = max(h, ht * ph * max(0.0, 1 - abs(u - pu) / pw) * (1 - abs(u) ** 2))
            if h > 1:
                m[int(gy - h):gy, W + cx - wd + i] = True
        for i in range(3):
            hex_column(m, W + cx + wd + 2 + i * 6, gy - 24 + i * 7, gy, 6)
            hex_column(m, W + cx - wd - 4 - i * 6, gy - 18 + i * 6, gy, 6)
    # half-buried giant gear
    gear_mask(m, 150 + W, 330, 52, teeth=14)
    # ruined furnace: dome with a glowing mouth
    fx, fy = 480 + W, int(top[480]) + 2
    dome = ellipse_mask(EW, H, fx, fy, 38, 46) & (yy <= fy)
    m |= dome
    m[fy - 58:fy - 40, fx - 6:fx + 6] = True       # flue
    m[fy - 62:fy - 58, fx - 9:fx + 9] = True
    mouth = ellipse_mask(EW, H, fx + 4, fy - 6, 13, 12) & (yy <= fy - 1)
    for y in range(H):
        for x in range(fx - 14, fx + 20):
            if mouth[y, x]:
                d = math.hypot((x - fx - 4) / 13, (y - fy + 6) / 12)
                c = LAVA[4] if d < 0.35 else LAVA[3] if d < 0.6 else LAVA[2] if d < 0.85 else LAVA[1]
                put(glow, x, y, c)
    # pipes along the ground with flanges and a glowing leak
    for (x0, x1, py) in [(200, 330, 308), (520, 640 + 40, 316)]:
        for x in range(x0, x1):
            m[py:py + 9, W + x] = True
        for fxl in range(x0 + 10, x1 - 4, 34):
            m[py - 2:py + 11, W + fxl:W + fxl + 3] = True
        for x in (x0 + 26, x1 - 20):
            glow_dot(glow, W + x, py + 4, LAVA[2], "#5a1c10", r=1)
    m[290:330, W + 330:W + 336] = True   # pipe elbow up
    # chain draped between two posts (catenary)
    for (ax_, bx_) in [(344, 396)]:
        for px in (ax_, bx_):
            gy = int(top[px]) + 2
            m[gy - 46:gy, W + px - 2:W + px + 2] = True
        x0, x1 = W + ax_, W + bx_
        for x in range(x0, x1, 2):
            u = (x - x0) / (x1 - x0)
            y = int(top[ax_]) - 42 + 16 * math.sin(math.pi * u)
            m[int(y):int(y) + 2, x:x + 2] = True
            if (x // 2) % 2:
                m[int(y) - 1, x] = True
    # ember / fire plants
    for k, (x, hgt) in enumerate([(228, 30), (250, 18), (372, 26), (556, 34), (590, 20), (70, 24),
                                  (120, 16), (440, 22)]):
        gy = int(top[x % W]) + 2
        pts = bezier((x + W, gy), (x + W + 4, gy - hgt * 0.5), (x + W - 2, gy - hgt), 16)
        m |= thick_path_mask(EW, H, pts, 0.7)
        for j in (6, 10):
            lx, ly = pts[j]
            m |= ellipse_mask(EW, H, lx + 3, ly, 3, 1.2)
        ex, ey = pts[-1]
        glow_dot(glow, ex, ey - 2, LAVA[2] if k % 3 else LAVA[3], "#5a1c10", r=2)
        put(glow, ex - 1, ey - 3, LAVA[4])
    fm = fold_mask(m)
    img = shade_silhouette(fm, NEAR_BODY, NEAR_LIT, NEAR_SHADE)
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 110)
    paste(img, gl, 0, 0)
    # underlighting: lava light catching the lower rims of the crags (warm rim)
    up = np.roll(fm, 1, axis=0)
    rim = fm & ~np.roll(fm, -1, axis=0)
    del up
    lower = rim & (np.arange(H)[:, None] > 200)
    img[lower] = col("#4a1a12")
    return img


# ---------------------------------------------------------------------------
# abyss: lava glow rising from below
# ---------------------------------------------------------------------------
ABYSS_PAL = ["#2a0806", "#4a0e0a", "#6e1a12", "#b3201a", "#d8341a", "#ff4d1e", "#ff8a3d",
             "#ffc56b", "#fff1c2"]


def abyss():
    yy = np.arange(H, dtype=np.float32)[:, None] * np.ones((1, W), np.float32)
    n1 = fbm(W, H, 128, 5, seed=3501, wrap_x=True, cell_y=96)
    n2 = fbm(W, H, 64, 4, seed=3502, wrap_x=True, cell_y=160)
    base = smoothstep(180, 360, yy) ** 2.0
    wisps = smoothstep(0.38, 0.85, n1 + (yy - 260) / 400.0)
    a = base * (0.3 + 0.95 * wisps)
    a = np.where(yy > 344, np.maximum(a, 0.92), a)
    a = np.clip(a, 0, 1)
    t = smoothstep(240, 360, yy) * (0.55 + 0.5 * n1)
    stops = hex_arr(["#4a0e0a", "#b3201a", "#ff4d1e", "#ff8a3d", "#ffc56b"])
    rgb = np.zeros((H, W, 3), np.float32)
    for i in range(3):
        rgb[..., i] = np.interp(t, [0, 0.3, 0.55, 0.8, 1.0], stops[:, i])
    # bright heat currents (ridged noise)
    ridge = 1 - np.abs(n2 - 0.5) / 0.035
    cur = np.clip(ridge, 0, 1) * smoothstep(220, 300, yy) * (1 - smoothstep(330, 360, yy))
    hot = hex_arr(["#ffc56b"])[0]
    rgb = rgb * (1 - cur[..., None]) + hot * cur[..., None]
    a = np.maximum(a, cur * 0.75)
    img = quantize_dither(rgb, ABYSS_PAL)
    img[..., 3] = dither_alpha(a, levels=(0, 40, 80, 128, 176, 224, 255))
    # rising embers
    r = rng(3503)
    for k in range(160):
        x = int(r.random() * W)
        y = int(200 + (r.random() ** 0.6) * 160)
        c = ["#fff1c2", "#ffc56b", "#ff8a3d", "#ff4d1e"][k % 4]
        put(img, x, y, c)
        if k % 5 == 0:
            put(img, x, y - 1, col(c, 150))
    img[:160] = 0
    return img


# ---------------------------------------------------------------------------
# fg_embers: craggy ceiling, hanging chains, smoke wisps, embers (in front of the level)
# ---------------------------------------------------------------------------
FG_BODY, FG_LIT = "#060203", "#1e0c0c"


def fg_embers():
    m = emask()
    glow = new(EW, H)
    prof = 3 + 20 * fbm1d(W, 80, 4, seed=3601) ** 1.7
    for k in range(3):
        for x in range(W):
            m[:int(prof[x]), k * W + x] = True
    r = rng(3602)
    # stalactite-like rock fangs
    for k in range(26):
        x = r.random() * W
        y = prof[int(x)] - 1
        ln = 4 + r.random() * 14
        wd = 2 + r.random() * 3
        m |= poly_mask(EW, H, [(W + x - wd, y), (W + x + wd, y), (W + x + (r.random() - 0.5) * 2,
                                                                  y + ln)])
    # hanging chains (thin, sparse: must not hide gameplay)
    for k, (cx, ln) in enumerate([(52, 150), (188, 96), (236, 210), (404, 120), (560, 176),
                                  (618, 70)]):
        y0 = int(prof[cx % W]) - 2
        ex, ey = chain_mask(m, W + cx, y0, ln, link=5, sway=2.0, phase=k * 1.3)
        if k % 2 == 0:
            # hook
            hook = bezier((ex, ey), (ex + 7, ey + 4), (ex + 3, ey + 11), 14)
            m |= thick_path_mask(EW, H, hook, 1.1)
            m |= thick_path_mask(EW, H, bezier((ex + 3, ey + 11), (ex - 3, ey + 10),
                                               (ex - 3, ey + 6), 8), 1.0)
        else:
            # broken end with a dangling ring
            m |= ellipse_mask(EW, H, ex, ey + 3, 3, 3.5) & ~ellipse_mask(EW, H, ex, ey + 3, 1.5, 2)
    # a long sagging chain between two ceiling anchors
    x0, x1 = W + 300, W + 372
    for x in range(x0, x1):
        u = (x - x0) / (x1 - x0)
        y = int(prof[300] + 4 + 30 * math.sin(math.pi * u))
        m[y:y + 2, x] = True
        if x % 4 == 0:
            m[y - 1:y + 3, x] = True
    fm = fold_mask(m)
    img = shade_silhouette(fm, FG_BODY, FG_LIT)
    # smoke wisps under the ceiling (translucent)
    n = fbm(W, H, 96, 4, seed=3603, wrap_x=True, cell_y=20)
    yy = np.arange(H)[:, None]
    band = np.exp(-((yy - (prof[None, :] + 10)) / 12.0) ** 2)
    sa = band * smoothstep(0.45, 0.75, n) * 0.8
    smoke = new(W, H)
    smoke[..., :3] = col("#1e0e10")[:3]
    smoke[..., 3] = dither_alpha(sa, levels=(0, 70, 120, 170))
    smoke[fm] = 0
    out = smoke
    paste(out, img, 0, 0)
    # a few glowing embers drifting in the foreground
    for k in range(18):
        x = int(r.random() * W)
        y = int(30 + r.random() * 300)
        glow_dot(glow, W + x, y, LAVA[3] if k % 3 else LAVA[4], "#7a2414", r=0)
    gl = fold_img(glow)
    gl[(gl[..., 3] < 255), 3] = np.minimum(gl[(gl[..., 3] < 255), 3], 120)
    paste(out, gl, 0, 0)
    return out


LAYERS = [("sky", sky), ("far", far), ("mid", mid), ("near", near), ("abyss", abyss),
          ("fg_embers", fg_embers)]
PARALLAX = {"sky": 0.0, "far": 0.12, "mid": 0.3, "near": 0.55, "abyss": 0.5, "fg_embers": 1.2}


def generate():
    out = []
    for name, fn in LAYERS:
        img = fn()
        assert img.shape == (H, W, 4), name
        out.append(save_png(img, f"backgrounds/pyralis/{name}.png"))
    return out
