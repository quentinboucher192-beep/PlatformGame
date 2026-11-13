"""Parallax backgrounds for Glacior, "Le sanctuaire gele" (640x360, horizontally seamless).

  sky.png         0.0   deep navy night, green/violet aurora curtains, dense stars, a large
                        pale icy moon and a small one (opaque)
  far.png         0.12  distant jagged ice mountains, a colossal frozen spire with a broken
                        halo ring, snow mist
  mid.png         0.3   glacier cliffs, glowing crystal spires, a giant gear frozen in an ice
                        block, snowy pine forest
  near.png        0.55  close snow-laden pines, ice boulders, a frozen pillar ruin with icicles,
                        glowing frost lamps
  abyss.png       0.5   blizzard / ice mist rising from below (transparent at the top), flakes
  fg_icicles.png  1.2   foreground: icy overhang with icicles along the top (in front of the level)

Same construction as the Sylvanox layers (backgrounds.py): shapes are drawn on a 3x wide
canvas and folded back mod 640, noise fields and profiles are 640-periodic.
"""
import math

import numpy as np

from backgrounds import (EW, H, W, emask, fold_img, fold_mask, glow_dot, profile_mask,
                         shade_silhouette)
from helpers import (apply_ramp, bayer, bezier, col, dither_alpha, fbm, fbm1d,
                     gradient_rgb, hex_arr, new, noise1d, paste, poly_mask, put, quantize_dither,
                     rng, save_png, smoothstep, thick_path_mask)

FROST = "#7ff6ff"
AUR_G = "#5dffb0"
AUR_V = "#b48cff"
ICE = ["#2f6fb3", "#5fb0ff", "#a8e4ff", "#eefaff"]


# ---------------------------------------------------------------------------
# shared silhouettes
# ---------------------------------------------------------------------------
def pine_mask(m, x, gy, h, w, tiers=5, seed=0, lean=0.0):
    """Snow-laden conifer silhouette: irregular drooping tiers, thin trunk."""
    r = rng(seed)
    m[gy - max(2, int(h * 0.08)):gy, int(round(x - 0.5)):int(round(x + 0.5)) + 1] = True
    top = gy - h
    for t in range(tiers):
        u0 = t / tiers
        ty = top + h * 0.9 * u0
        by = top + h * 0.9 * (u0 + 1.35 / tiers) + 1
        k = (t + 1) / tiers
        hl = w / 2 * (0.25 + 0.75 * k) * (0.8 + 0.4 * r.random())
        hr = w / 2 * (0.25 + 0.75 * k) * (0.8 + 0.4 * r.random())
        cx = x + lean * (1 - u0)
        droop_l = 1 + r.random() * 3
        droop_r = 1 + r.random() * 3
        pts = [(cx, ty), (cx + hr * 0.45, ty + (by - ty) * 0.45), (cx + hr, by + droop_r),
               (cx + hr * 0.7, by - 1), (cx + hr * 0.35, by + 1), (cx, by - 1),
               (cx - hl * 0.35, by + 1), (cx - hl * 0.7, by - 1), (cx - hl, by + droop_l),
               (cx - hl * 0.45, ty + (by - ty) * 0.45)]
        m |= poly_mask(m.shape[1], H, pts)
    m[int(top) - 2:int(top) + 1, int(round(x + lean))] = True
    return m


def shard_mask(m, x, gy, h, w, lean=0.0):
    """Pointed crystal / ice shard standing on the ground."""
    tip = (x + lean, gy - h)
    pts = [(x - w / 2, gy), (x - w / 2 + lean * 0.6, gy - h * 0.7), tip,
           (x + w / 2 + lean * 0.6, gy - h * 0.72), (x + w / 2, gy)]
    m |= poly_mask(m.shape[1], H, pts)
    return tip


def jag_profile(n, base, amp, peaks, seed):
    """Periodic mountain profile: smooth noise + sharp triangular peaks."""
    prof = base + amp * fbm1d(n, 96, 4, seed=seed)
    xs = np.arange(n)
    for (px, ph, pw) in peaks:
        d = np.minimum(np.abs(xs - px), n - np.abs(xs - px))
        spike = ph * np.clip(1 - d / pw, 0, 1) ** 1.15
        prof = np.minimum(prof, base + amp * 0.5 - spike)
    jag = noise1d(n, 4, seed + 2)
    return prof + jag * 3


# ---------------------------------------------------------------------------
# sky: aurora night
# ---------------------------------------------------------------------------
SKY_STOPS = [(0.0, "#03060f"), (0.18, "#060c1c"), (0.36, "#0a1428"), (0.52, "#0e1c38"),
             (0.64, "#13264a"), (0.76, "#1a3358"), (0.86, "#22426a"), (0.94, "#2c5480"),
             (1.0, "#36628e")]
AUR_PAL = ["#123a40", "#1a5a52", "#2a8a6e", "#3fc890", AUR_G, "#b0ffd8", "#2a2458", "#4a3a8a",
           "#7a5ac8", AUR_V, "#dcc8ff", "#1e4a6a", "#2a6a8a"]


def sky_palette():
    g = gradient_rgb(40, SKY_STOPS)
    return ["#%02x%02x%02x" % tuple(int(v) for v in c) for c in g] + AUR_PAL


def aurora(rgb, centre, height, seed, green_top=AUR_G, top_col=AUR_V, strength=0.75):
    """Curtain: bright lower hem along `centre` (W,), fading upwards, with fine vertical rays
    and intensity folds along x; a faint glow spills below the hem."""
    yy = np.arange(H, dtype=np.float32)[:, None]
    rays = fbm(W, 1, 4, 3, seed=seed, wrap_x=True)[0]
    folds = fbm(W, 1, 80, 3, seed=seed + 1, wrap_x=True)[0]
    hgt = height * (0.55 + 0.7 * fbm(W, 1, 32, 2, seed=seed + 2, wrap_x=True)[0])
    u = (centre[None, :] - yy) / hgt[None, :]               # 0 at the hem, 1 at the top
    hem = np.exp(-((u - 0.03) / 0.07) ** 2)
    body = np.clip(1 - u, 0, 1) ** 2.2 * (u >= 0)
    spill = np.exp(-((u + 0.05) / 0.25) ** 2) * (u < 0) * 0.25
    inten = (0.45 * body * (0.45 + 0.55 * smoothstep(0.3, 0.75, rays[None, :])) + 0.65 * hem
             + spill)
    inten *= smoothstep(0.3, 0.7, folds)[None, :] * strength
    ga, va = hex_arr([green_top])[0], hex_arr([top_col])[0]
    tcol = np.clip(u * 1.3, 0, 1)[..., None]
    c = ga * (1 - tcol) + va * tcol
    k = np.clip(inten, 0, 1)[..., None]
    return rgb * (1 - k) + c * k


def moon(img, cx, cy, r, ramp, craters=(), light=(-0.75, -0.45, 0.5)):
    L = np.array(light, np.float32)
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
    tone = 0.08 + 0.92 * lam
    for (ux, uy, ur) in craters:
        c = ((dx - ux) ** 2 + (dy - uy) ** 2) <= ur * ur
        rimc = ((dx - ux - ur * 0.35) ** 2 + (dy - uy - ur * 0.35) ** 2) <= ur * ur
        tone = np.where(c & ~rimc, tone * 0.75, tone)
        tone = np.where(c & rimc, np.minimum(1, tone * 1.06), tone)
    m = apply_ramp(tone, ramp, bayer(sz, sz, 4, x0, y0), 0.8)
    sub = img[y0:y0 + sz, x0:x0 + sz]
    sub[inside] = m[inside]
    # thin cold halo on the lit limb
    rim = (r2 > 1) & (r2 <= (1 + 1.6 / r) ** 2) & ((dx * L[0] + dy * L[1]) > 0.1)
    sub[rim, :3] = (sub[rim, :3] * 0.5 + hex_arr(["#5f88b0"])[0] * 0.5).astype(np.uint8)


def sky():
    g = gradient_rgb(H, SKY_STOPS)
    rgb = np.repeat(g[:, None, :], W, 1).astype(np.float32)
    xs = np.arange(W)
    c1 = 160 + 30 * np.sin(2 * np.pi * xs / W + 0.4) + 14 * np.sin(6 * np.pi * xs / W + 2.0)
    c2 = 112 + 20 * np.sin(2 * np.pi * xs / W + 3.1) + 10 * np.sin(4 * np.pi * xs / W + 0.7)
    rgb = aurora(rgb, c2, 60, 4101, green_top="#7a5ac8", top_col="#2a2458", strength=0.55)
    rgb = aurora(rgb, c1, 100, 4111, strength=0.85)
    # faint high haze
    n = fbm(W, H, 128, 4, seed=4121, wrap_x=True, cell_y=48)
    yy = np.arange(H)[:, None]
    hz = smoothstep(0.55, 0.8, n) * np.exp(-((yy - 290) / 50.0) ** 2) * 0.35
    rgb = rgb * (1 - hz[..., None]) + hex_arr(["#2a5a8a"])[0] * hz[..., None]
    img = quantize_dither(rgb, sky_palette())
    r = rng(4131)
    for i in range(1300):
        x, y = int(r.random() * W), int(r.random() * H)
        if r.random() > (1 - y / H) ** 1.5:
            continue
        b = r.random()
        if b < 0.55:
            put(img, x, y, "#22365a")
        elif b < 0.82:
            put(img, x, y, "#5c7a9a")
        elif b < 0.95:
            put(img, x, y, "#c9d8ea")
        else:
            put(img, x, y, "#ffffff")
            for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                put(img, (x + ddx) % W, y + ddy, "#5f88b0")
    for (x, y, c) in [(52, 26, FROST), (240, 14, AUR_V), (330, 60, "#ffffff"), (610, 40, FROST),
                      (18, 120, AUR_G)]:
        put(img, x, y, c)
    moon(img, 478, 70, 34, ["#16243e", "#26385a", "#3c5278", "#5a7499", "#86a2c0", "#b8d0e4",
                            "#e6f2fa"],
         craters=[(-0.35, -0.3, 0.2), (0.15, 0.2, 0.16), (-0.5, 0.35, 0.12), (0.3, -0.45, 0.1),
                  (-0.05, -0.05, 0.08)])
    moon(img, 150, 48, 7.5, ["#121c30", "#24365a", "#46628a", "#7c9cc0", "#c4dcf0"],
         craters=[(-0.2, -0.1, 0.3)])
    img[..., 3] = 255
    return img


# ---------------------------------------------------------------------------
# far: ice mountains + colossal frozen spire
# ---------------------------------------------------------------------------
FAR_BODY, FAR_LIT, FAR_SHADE = "#1a2c50", "#3e5f90", "#14223f"


PEAKS_BACK = [(20, 128, 70), (140, 150, 60), (236, 118, 80), (372, 140, 70), (470, 124, 64),
              (580, 112, 74)]
PEAKS_FRONT = [(70, 196, 54), (180, 214, 44), (300, 188, 60), (420, 206, 50), (530, 192, 58),
               (620, 220, 40)]


def mountains(peaks, base_y, seed):
    """Periodic range made of jagged peaks. Returns (profile, owner peak index per column)."""
    xs = np.arange(W)
    prof = np.full(W, float(base_y))
    owner = np.full(W, -1)
    jag = noise1d(W, 5, seed) * 4 + noise1d(W, 17, seed + 1) * 7
    for k, (px, py, hw) in enumerate(peaks):
        d = (xs - px + W / 2) % W - W / 2
        h = py + np.abs(d) * (base_y - py) / hw + jag * np.clip(np.abs(d) / hw, 0.2, 1)
        # shoulder ridges
        h = np.minimum(h, py + 22 + np.abs(d - hw * 0.45) * (base_y - py) / hw * 1.4)
        better = h < prof
        prof = np.where(better, h, prof)
        owner = np.where(better, k, owner)
    return prof, owner


def paint_range(img, m, prof, owner, peaks, body, face_lit, snow_lit, snow_sh, snowline, seed):
    """Lit left faces / shaded right faces split at each peak, snow caps above `snowline`."""
    sn = noise1d(W, 6, seed) * 8
    for x in range(W):
        k = owner[x]
        if k < 0:
            continue
        px, py, hw = peaks[k]
        d = (x - px + W / 2) % W - W / 2
        y0 = int(prof[x])
        for y in range(max(0, y0), H):
            if not m[y, x]:
                continue
            ridge = d + (y - py) * 0.18          # ridge line leans slightly to the right
            lit = ridge < 0
            snow = y < py + snowline + sn[x] + (abs(d) * 0.35)
            if snow:
                c = snow_lit if lit else snow_sh
            else:
                c = face_lit if lit and (x + y) % 2 == 0 else body
                if lit and (y - y0) < 3:
                    c = face_lit
            img[y, x] = col(c)


def far():
    glow = new(EW, H)
    img = new(W, H)
    yy = np.arange(H)[:, None] * np.ones((1, W))
    # back range (hazier)
    pb, ob = mountains(PEAKS_BACK, 300, 4201)
    mb = yy >= pb[None, :]
    paint_range(img, mb, pb, ob, PEAKS_BACK, "#1c3156", "#22406a", "#3a5f92", "#2a4a78", 34, 4202)
    # colossal frozen spire with a broken halo ring (between the ranges)
    m = emask()
    sx, top_y = 330 + W, 60
    for y in range(top_y, H):
        t = (y - top_y) / (H - top_y)
        half = 2 + 16 * t ** 1.4
        m[y, int(sx - half):int(sx + half) + 1] = True
    eyy, exx = np.mgrid[0:H, 0:EW]
    ang = np.degrees(np.arctan2(-(eyy + 0.5 - 150), exx + 0.5 - sx))
    rx = (exx + 0.5 - sx) / 58.0
    ry = (eyy + 0.5 - 150) / 16.0
    d = np.sqrt(rx * rx + ry * ry)
    ring = (d >= 0.86) & (d <= 1.0) & ~((ang > 20) & (ang < 48)) & ~((ang < -120) & (ang > -150))
    m |= ring
    for (gx, gy) in [(sx, top_y + 4), (sx, 150), (sx - 56, 150), (sx + 56, 150), (sx, 112)]:
        put(glow, gx, gy, FROST)
    fm = fold_mask(m)
    sp = shade_silhouette(fm, "#1a2c50", "#4a72a8", "#14223f",
                          fog=[(0, "#1a2c50"), (240, "#1a2c50"), (300, "#22406a"), (360, "#2a4c7c")])
    paste(img, sp, 0, 0)
    # front range
    pf, of = mountains(PEAKS_FRONT, 318, 4203)
    mf = yy >= pf[None, :]
    paint_range(img, mf, pf, of, PEAKS_FRONT, "#162848", "#1d3560", "#35578a", "#25416e", 22, 4204)
    img[mf & (yy > 318)] = col("#1d3560")
    gl = fold_img(glow)
    paste(img, gl, 0, 0)
    n = fbm(W, H, 128, 4, seed=4210, wrap_x=True, cell_y=32)
    band = np.exp(-((yy - 300) / 26.0) ** 2) * 0.85 + np.exp(-((yy - 226) / 10.0) ** 2) * 0.3
    a = band * smoothstep(0.35, 0.7, n)
    mist = new(W, H)
    mist[..., :3] = col("#4a6a9a")[:3]
    mist[..., 3] = dither_alpha(a, levels=(0, 64, 110, 150))
    paste(img, mist, 0, 0)
    return img


# ---------------------------------------------------------------------------
# mid: glacier cliffs, crystal spires, frozen gear, pine forest
# ---------------------------------------------------------------------------
MID_BODY, MID_LIT, MID_SHADE = "#0e1a34", "#28487a", "#0a1428"
HALO_C = "#1f5a6a"
HALO_G = "#1f5a48"


def mid():
    m = emask()
    glow = new(EW, H)
    top = 304 + 14 * fbm1d(W, 128, 4, seed=4301)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    r = rng(4302)
    # glacier cliffs: flat-topped walls with slanted faces
    for (cx, wd, ht) in [(70, 70, 150), (452, 90, 120)]:
        gy = int(top[cx % W]) + 4
        pts = [(W + cx - wd / 2 - 10, gy)]
        steps = 7
        for i in range(steps + 1):
            u = i / steps
            x = W + cx - wd / 2 + wd * u
            h = ht * (0.82 + 0.18 * math.sin(i * 2.1 + cx)) * (1 - 0.25 * abs(u - 0.4))
            pts.append((x, gy - h))
        pts.append((W + cx + wd / 2 + 12, gy))
        m |= poly_mask(EW, H, pts)
    # crystal spires with glowing cores
    for (cx, n, h0) in [(180, 5, 110), (330, 4, 80), (590, 5, 120)]:
        for i in range(n):
            x = cx + (i - n / 2) * 9 + r.random() * 4
            h = h0 * (0.45 + 0.55 * r.random())
            w = 6 + r.random() * 6
            gy = int(top[int(x) % W]) + 4
            tip = shard_mask(m, W + x, gy, h, w, lean=(r.random() - 0.5) * 12)
            if r.random() < 0.7:
                glow_dot(glow, tip[0], tip[1] + h * 0.3, FROST if i % 2 else ICE[2], HALO_C, r=1)
    # head of a colossal ancient automaton, half buried and frozen (tilted)
    hx, hy = 262 + W, int(top[262]) - 16
    ca, sa = math.cos(math.radians(-12)), math.sin(math.radians(-12))
    dxh, dyh = xx + 0.5 - hx, yy + 0.5 - hy
    u = dxh * ca + dyh * sa
    v = -dxh * sa + dyh * ca
    head = (np.abs(u) <= 30) & (np.abs(v) <= 24) & ((np.abs(u) - 22) ** 2 + (np.abs(v) - 16) ** 2 <= 64
                                                   | (np.abs(u) <= 22) | (np.abs(v) <= 16))
    crest = (np.abs(u) <= 5) & (v >= -34) & (v < -24)
    horn = (u > 14) & (u < 19) & (v >= -31) & (v < -24)
    m |= head | crest | horn
    visor = (np.abs(u) <= 20) & (np.abs(v + 4) <= 1.2)
    # icicles hanging from the jaw
    for i, ln in enumerate([5, 9, 4, 11, 6, 8, 3]):
        bx_ = hx - 20 + i * 6
        by_ = hy + 22 + int(i * 6 * sa)
        m |= poly_mask(EW, H, [(bx_ - 1.5, by_), (bx_ + 1.5, by_), (bx_, by_ + ln)])
    # snowy pine forest along the ground
    for k in range(34):
        x = r.random() * W
        h = 24 + r.random() * 40
        gy = int(top[int(x) % W]) + 3
        pine_mask(m, W + x, gy, h, h * 0.42, tiers=4 + int(r.random() * 2), seed=4320 + k)
    # ancient beacon lights on the cliffs
    for (x, y) in [(64 + W, 190), (470 + W, 222), (430 + W, 230)]:
        glow_dot(glow, x, y, AUR_G, HALO_G, r=1)
    fm = fold_mask(m)
    img = shade_silhouette(fm, MID_BODY, MID_LIT, MID_SHADE, lit_w=2,
                           fog=[(0, MID_BODY), (200, MID_BODY), (290, "#12244a"), (360, "#1a335e")])
    # glowing visor slit of the frozen colossus (one long eye)
    vm = fold_mask(visor)
    img[vm] = col("#2fb8c8")
    img[vm & (np.roll(vm, 1, axis=1) & np.roll(vm, -1, axis=1)) & (xx[:, :W] % 5 == 0)] = col(FROST)
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 90)
    paste(img, gl, 0, 0)
    return img


# ---------------------------------------------------------------------------
# near: snow-laden pines, ice boulders, frozen pillar ruin, frost lamps
# ---------------------------------------------------------------------------
NEAR_BODY, NEAR_LIT, NEAR_SHADE = "#070e1d", "#28426a", "#04080f"


def near():
    m = emask()
    glow = new(EW, H)
    top = 334 + 10 * fbm1d(W, 96, 4, seed=4401)
    g = profile_mask(top)
    for k in range(3):
        m[:, k * W:(k + 1) * W] |= g
    yy, xx = np.mgrid[0:H, 0:EW]
    r = rng(4402)
    # big snow-laden pines
    for k, (x, h) in enumerate([(24, 150), (90, 96), (232, 170), (272, 104), (432, 126),
                                (560, 158), (606, 92)]):
        gy = int(top[x % W]) + 4
        pine_mask(m, W + x, gy, h, h * 0.5, tiers=6, seed=4410 + k, lean=(r.random() - 0.5) * 6)
    # snow-capped boulders
    for (x, rx_, ry_) in [(150, 26, 18), (512, 20, 14)]:
        gy = int(top[x % W]) + 4
        pts = []
        for i in range(9):
            a = math.pi + i / 8 * math.pi
            rr = 1 + 0.18 * math.sin(i * 2.7 + x)
            pts.append((W + x + math.cos(a) * rx_ * rr, gy + math.sin(a) * ry_ * 2 * rr))
        m |= poly_mask(EW, H, pts)
    # ice crystal cluster with a glowing heart
    cx, gy = 350 + W, int(top[350]) + 4
    for (dx, h, w, ln) in [(-14, 40, 9, -8), (-4, 74, 13, -2), (8, 56, 11, 6), (18, 30, 8, 10),
                           (-22, 22, 7, -10)]:
        shard_mask(m, cx + dx, gy, h, w, lean=ln)
    glow_dot(glow, cx - 2, gy - 30, FROST, HALO_C, r=2)
    put(glow, cx - 3, gy - 31, "#eefaff")
    # frozen ruin gate: two pillars + lintel with icicles
    for px in (170 + W, 206 + W):
        gyp = int(top[(px - W) % W]) + 4
        m[gyp - 70:gyp, px - 5:px + 5] = True
        m[gyp - 74:gyp - 70, px - 7:px + 7] = True
    ly = int(top[188]) + 4 - 80
    m[ly:ly + 7, 160 + W:216 + W] = True
    for i, ln in enumerate([4, 9, 5, 12, 6, 3, 8, 5]):
        x = 165 + W + i * 6
        m |= poly_mask(EW, H, [(x - 1.5, ly + 7), (x + 1.5, ly + 7), (x, ly + 7 + ln)])
    glow_dot(glow, 188 + W, ly + 3, AUR_G, HALO_G, r=1)
    # frost lamps (glowing bulbs on curved stems)
    for k, (x, hgt) in enumerate([(300, 24), (462, 40), (530, 26), (626, 30), (60, 22), (130, 18)]):
        gy = int(top[x % W]) + 2
        pts = bezier((x + W, gy), (x + W + 5, gy - hgt * 0.6), (x + W - 1, gy - hgt), 16)
        m |= thick_path_mask(EW, H, pts, 0.7)
        ex, ey = pts[-1]
        core = FROST if k % 3 else AUR_G
        glow_dot(glow, ex, ey - 2, core, HALO_C if k % 3 else HALO_G, r=2)
        put(glow, ex - 1, ey - 3, "#eefaff")
    # frozen grass blades
    for k in range(50):
        x = r.random() * W
        gy0 = top[int(x) % W] + 3
        hgt = 5 + r.random() * 12
        lean = (r.random() - 0.5) * 8
        pts = bezier((x + W, gy0), (x + W, gy0 - hgt * 0.6), (x + W + lean, gy0 - hgt), 10)
        m |= thick_path_mask(EW, H, pts, [1.0 - 0.6 * i / 9 for i in range(10)])
    fm = fold_mask(m)
    img = shade_silhouette(fm, NEAR_BODY, NEAR_LIT, NEAR_SHADE, lit_w=2)
    # first lit row brighter: fresh snow on every upward-facing edge
    up = np.roll(fm, 1, axis=0)
    snow = fm & ~up
    img[snow] = col("#4a6a98")
    gl = fold_img(glow)
    halo_mask = (gl[..., 3] < 255) & ~fm
    gl[halo_mask, 3] = np.minimum(gl[halo_mask, 3], 110)
    paste(img, gl, 0, 0)
    return img


# ---------------------------------------------------------------------------
# abyss: blizzard / ice mist rising from below
# ---------------------------------------------------------------------------
ABYSS_PAL = ["#1a335e", "#2a4c7c", "#3e6a9a", "#5f88b0", "#8aaed0", "#b8d4ec", "#dfeefa",
             "#ffffff", "#5fb0ff", "#a8e4ff"]


def abyss():
    yy = np.arange(H, dtype=np.float32)[:, None] * np.ones((1, W), np.float32)
    n1 = fbm(W, H, 128, 5, seed=4501, wrap_x=True, cell_y=64)
    base = smoothstep(180, 360, yy) ** 2.0
    # blizzard streaks: noise sheared sideways (wind)
    wisps = smoothstep(0.36, 0.82, n1 + (yy - 260) / 380.0)
    a = base * (0.3 + 0.9 * wisps)
    a = np.where(yy > 346, np.maximum(a, 0.88), a)
    a = np.clip(a, 0, 1)
    t = smoothstep(240, 360, yy) * (0.55 + 0.5 * n1)
    stops = hex_arr(["#2a4c7c", "#5f88b0", "#8aaed0", "#b8d4ec", "#ffffff"])
    rgb = np.zeros((H, W, 3), np.float32)
    for i in range(3):
        rgb[..., i] = np.interp(t, [0, 0.3, 0.55, 0.8, 1.0], stops[:, i])
    # wind-blown snow: thin streaks sheared diagonally (row roll keeps the wrap seamless)
    n3 = fbm(W, H, 40, 3, seed=4504, wrap_x=True, cell_y=2)
    n3 = np.stack([np.roll(n3[y], int(y * 1.7)) for y in range(H)])
    streak = smoothstep(0.66, 0.72, n3) * smoothstep(200, 270, yy) * (1 - smoothstep(330, 356, yy))
    rgb = rgb * (1 - streak[..., None]) + hex_arr(["#dfeefa"])[0] * streak[..., None]
    a = np.maximum(a, streak * 0.5)
    img = quantize_dither(rgb, ABYSS_PAL)
    img[..., 3] = dither_alpha(a, levels=(0, 40, 80, 128, 176, 224, 255))
    # snow flakes swirling up
    r = rng(4503)
    for k in range(220):
        x = int(r.random() * W)
        y = int(170 + (r.random() ** 0.7) * 190)
        c = ["#ffffff", "#dfeefa", "#a8e4ff"][k % 3]
        put(img, x, y, c)
        if k % 6 == 0:
            put(img, (x + 1) % W, y, col(c, 150))
    img[:150] = 0
    return img


# ---------------------------------------------------------------------------
# fg_icicles: icy overhang with icicles along the top (in front of the level)
# ---------------------------------------------------------------------------
FG_BODY, FG_LIT, FG_SHADE = "#11233f", "#3a6496", "#081428"


def fg_icicles():
    m = emask()
    prof = 3 + 16 * fbm1d(W, 80, 4, seed=4601) ** 1.6
    for k in range(3):
        for x in range(W):
            m[:int(prof[x]), k * W + x] = True
    r = rng(4602)
    icicles = []
    # clusters of tapered icicles under the overhang, a few long ones
    for c in range(14):
        cx = r.random() * W
        n = 2 + int(r.random() * 3)
        for i in range(n):
            x = cx + (i - n / 2) * (8 + r.random() * 5)
            thick = prof[int(x) % W]
            ln = (10 + r.random() * 14) * (0.7 + thick / 16) * (1.5 if i == n // 2 else 1.0)
            if c % 4 == 0 and i == n // 2:
                ln = 44 + r.random() * 36
            wd = 2.5 + min(3.5, ln / 12) * (0.8 + 0.4 * r.random())
            y0 = thick - 1
            pts = [(W + x - wd, y0), (W + x + wd, y0), (W + x + wd * 0.45, y0 + ln * 0.55),
                   (W + x + 0.5, y0 + ln), (W + x - wd * 0.4, y0 + ln * 0.5)]
            m |= poly_mask(EW, H, pts)
            icicles.append((x % W, y0, ln, wd))
    fm = fold_mask(m)
    img = shade_silhouette(fm, FG_BODY, FG_LIT)
    right = fm & ~np.roll(fm, -1, axis=1)
    img[right & (np.arange(H)[:, None] > prof[None, :])] = col(FG_SHADE)
    # translucent ice: a soft inner highlight in the upper half of each icicle
    for (x, y0, ln, wd) in icicles:
        for y in range(int(y0 + 2), int(y0 + ln * 0.5)):
            t = (y - y0) / ln
            xx = int(round(x - wd * 0.35 * (1 - t)))
            if 0 <= y < H and fm[y, xx % W]:
                img[y, xx % W] = col("#4f7fb8" if t < 0.3 else "#2c5288")
        put(img, int(round(x)) % W, int(y0 + ln) - 1, "#8fb8e0")
    # drifting snowflakes in front (sparse)
    for k in range(40):
        x = int(r.random() * W)
        y = int(10 + r.random() * 340)
        put(img, x, y, col("#dfeefa", 170 if k % 3 else 230))
    return img


LAYERS = [("sky", sky), ("far", far), ("mid", mid), ("near", near), ("abyss", abyss),
          ("fg_icicles", fg_icicles)]
PARALLAX = {"sky": 0.0, "far": 0.12, "mid": 0.3, "near": 0.55, "abyss": 0.5, "fg_icicles": 1.2}


def generate():
    out = []
    for name, fn in LAYERS:
        img = fn()
        assert img.shape == (H, W, 4), name
        out.append(save_png(img, f"backgrounds/glacior/{name}.png"))
    return out
