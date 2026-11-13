"""Planet-map art (ASSET_SPEC section 11):
  ui/planets/{sylvanox,pyralis,glacior,sun}.png 256x256 (transparent background)
  ui/planets/sylvanox_banner.png 480x270 (vista for the planet info panel)
  ui/map_background.png 960x540 (deep-space backdrop with nebula)

Planets are shown larger than world sprites, so they use smoother dithered sphere shading
(ordered dithering onto a per-planet palette) with a thin atmosphere halo.
"""
import json
import math
import os

import numpy as np

from helpers import (ASSETS, Noise3, bayer, col, dither_alpha, fbm, hex_arr, load_png, new, paste,
                     put, quantize_dither, rng, save_png, smoothstep)

S = 256
C = 128.0
R = 100.0
LIGHT = np.array([-0.62, -0.5, 0.6])
LIGHT = LIGHT / np.linalg.norm(LIGHT)


def sphere(size=S, cx=C, cy=C, r=R):
    yy, xx = np.mgrid[0:size, 0:size]
    dx = (xx + 0.5 - cx) / r
    dy = (yy + 0.5 - cy) / r
    d2 = dx * dx + dy * dy
    inside = d2 <= 1
    nz = np.sqrt(np.clip(1 - d2, 0, 1))
    lam = np.clip(dx * LIGHT[0] + dy * LIGHT[1] + nz * LIGHT[2], 0, 1)
    return dx, dy, nz, d2, inside, lam


def rotate(dx, dy, nz, tilt=0.35, spin=0.6):
    """Rotate sphere coordinates so features are not axis aligned."""
    x, y, z = dx, dy, nz
    c, s = math.cos(spin), math.sin(spin)
    x, z = x * c + z * s, -x * s + z * c
    c, s = math.cos(tilt), math.sin(tilt)
    x, y = x * c - y * s, x * s + y * c
    return x, y, z


def halo(img, d2, lam_dir, colour, width=7.0, strength=1.0, r=R):
    """Thin atmosphere halo outside the disc, brighter on the lit side."""
    d = np.sqrt(d2) * r
    t = np.clip(1 - (d - r) / width, 0, 1) * (d > r)
    t = t ** 1.6 * (0.35 + 0.65 * lam_dir) * strength
    a = dither_alpha(t, levels=(0, 50, 100, 160, 220))
    lay = new(img.shape[1], img.shape[0])
    lay[..., :3] = col(colour)[:3]
    lay[..., 3] = a
    out = lay.copy()
    paste(out, img, 0, 0)
    return out


def lit_dir(dx, dy):
    n = np.sqrt(dx * dx + dy * dy) + 1e-6
    return np.clip((dx / n) * LIGHT[0] / np.hypot(LIGHT[0], LIGHT[1]) +
                   (dy / n) * LIGHT[1] / np.hypot(LIGHT[0], LIGHT[1]), 0, 1)


def rim_light(rgb, d2, inside, colour, k=0.5):
    """Atmospheric scattering near the limb inside the disc."""
    edge = smoothstep(0.80, 1.0, d2) * inside
    c = hex_arr([colour])[0]
    return rgb * (1 - edge[..., None] * k) + c * edge[..., None] * k


def finish(rgb, inside, palette, extra_alpha=None):
    img = quantize_dither(rgb, palette, bayer(S, S, 4), 1.0)
    img[..., 3] = np.where(inside, 255, 0).astype(np.uint8)
    return img


# ---------------------------------------------------------------------------
def sylvanox():
    dx, dy, nz, d2, inside, lam = sphere()
    x, y, z = rotate(dx, dy, nz, 0.3, 0.9)
    n3 = Noise3(2101)
    land = n3.fbm(x * 2.2 + 5, y * 2.2, z * 2.2, 5)
    veins_n = Noise3(2102).fbm(x * 4.5, y * 4.5, z * 4.5, 4)
    ridge = 1 - np.abs(veins_n - 0.5) * 2
    veins = smoothstep(0.93, 0.975, ridge)
    clouds = smoothstep(0.55, 0.75, Noise3(2103).fbm(x * 3 + 2, y * 6, z * 3, 4))
    sea_c = hex_arr(["#0f2c3c"])[0]
    sea_l = hex_arr(["#1d5a6a"])[0]
    land_d = hex_arr(["#0f3b3a"])[0]
    land_m = hex_arr(["#1f8a6a"])[0]
    land_h = hex_arr(["#5fd0a0"])[0]
    is_land = smoothstep(0.47, 0.52, land)
    hgt = smoothstep(0.5, 0.75, land)
    sea = sea_c + (sea_l - sea_c) * smoothstep(0.3, 0.47, land)[..., None]
    lnd = land_d + (land_m - land_d) * smoothstep(0.0, 0.6, hgt)[..., None]
    lnd = lnd + (land_h - lnd) * smoothstep(0.6, 1.0, hgt)[..., None] * 0.6
    base = sea * (1 - is_land[..., None]) + lnd * is_land[..., None]
    shade = (0.08 + 0.92 * lam ** 0.9)[..., None]
    rgb = base * shade
    # bioluminescent veins: emissive, strongest on the night side
    vein_c = hex_arr(["#39f0d8"])[0]
    night = (1 - smoothstep(0.0, 0.45, lam))
    v = veins * is_land * (0.45 + 0.55 * night)
    rgb = rgb * (1 - v[..., None]) + vein_c * v[..., None]
    core = smoothstep(0.975, 0.995, ridge) * is_land * night
    rgb = rgb * (1 - core[..., None]) + hex_arr(["#8ffff0"])[0] * core[..., None]
    # clouds (lit only)
    cl = clouds * 0.7 * smoothstep(0.05, 0.4, lam)
    rgb = rgb * (1 - cl[..., None]) + hex_arr(["#c8f0e8"])[0] * (lam[..., None] * 0.9 + 0.1) * cl[..., None]
    rgb = rim_light(rgb, d2, inside, "#39f0d8", 0.35 * 1)
    pal = ["#05080f", "#0b141e", "#0f2230", "#0f2c3c", "#163f50", "#1d5a6a", "#0f3b3a", "#135c50",
           "#1f8a6a", "#35b583", "#5fd0a0", "#1fb8b0", "#39f0d8", "#8ffff0", "#c8f0e8", "#e6fffb",
           "#2a6a7a", "#0b2a2a"]
    img = finish(rgb, inside, pal)
    return halo(img, d2, lit_dir(dx, dy), "#39f0d8", width=8, strength=0.9)


def pyralis():
    dx, dy, nz, d2, inside, lam = sphere()
    x, y, z = rotate(dx, dy, nz, -0.4, 2.1)
    rock = Noise3(2201).fbm(x * 3, y * 3, z * 3, 5)
    cr = Noise3(2202).fbm(x * 5, y * 5, z * 5, 4)
    ridge = 1 - np.abs(cr - 0.5) * 2
    cracks = smoothstep(0.92, 0.975, ridge)
    seas = smoothstep(0.66, 0.70, rock)
    c0, c1, c2 = hex_arr(["#1a0e12"])[0], hex_arr(["#4a1c1c"])[0], hex_arr(["#7a3424"])[0]
    base = c0 + (c1 - c0) * smoothstep(0.2, 0.55, rock)[..., None]
    base = base + (c2 - base) * smoothstep(0.55, 0.8, rock)[..., None] * 0.5
    rgb = base * (0.12 + 0.88 * lam ** 0.9)[..., None]
    glow = np.maximum(cracks, seas * 0.9)
    lava = hex_arr(["#ff8a3d"])[0] * (1 - smoothstep(0.9, 1, ridge))[..., None] + \
        hex_arr(["#ffd98a"])[0] * smoothstep(0.9, 1, ridge)[..., None]
    lava = np.where(seas[..., None] > 0.5, hex_arr(["#ff6a2a"])[0], lava)
    rgb = rgb * (1 - glow[..., None]) + lava * glow[..., None]
    rgb = rim_light(rgb, d2, inside, "#ff8a3d", 0.3)
    pal = ["#0b0608", "#1a0e12", "#2e1418", "#4a1c1c", "#6e2a1e", "#8a3a22", "#a3123a", "#c8402a",
           "#ff6a2a", "#ff8a3d", "#ffb347", "#ffd98a", "#5a2420"]
    img = finish(rgb, inside, pal)
    return halo(img, d2, lit_dir(dx, dy), "#ff6a2a", width=7, strength=0.8)


def glacior():
    dx, dy, nz, d2, inside, lam = sphere()
    x, y, z = rotate(dx, dy, nz, 0.25, 4.0)
    ice = Noise3(2301).fbm(x * 2.5, y * 2.5, z * 2.5, 5)
    cr = Noise3(2302).fbm(x * 6, y * 6, z * 6, 3)
    ridge = 1 - np.abs(cr - 0.5) * 2
    cracks = smoothstep(0.9, 0.97, ridge)
    pole = smoothstep(0.55, 0.85, np.abs(y))
    c0, c1, c2 = hex_arr(["#3a5a80"])[0], hex_arr(["#7fa8c8"])[0], hex_arr(["#d8ecf6"])[0]
    base = c0 + (c1 - c0) * smoothstep(0.3, 0.6, ice)[..., None]
    base = base + (c2 - base) * np.maximum(smoothstep(0.6, 0.8, ice), pole)[..., None]
    base = base * (1 - cracks[..., None] * 0.5) + hex_arr(["#2a4a7a"])[0] * cracks[..., None] * 0.5
    rgb = base * (0.12 + 0.88 * lam ** 0.85)[..., None]
    spec = smoothstep(0.92, 1.0, lam) * 0.5
    rgb = rgb * (1 - spec[..., None]) + 255 * spec[..., None]
    rgb = rim_light(rgb, d2, inside, "#b8d8ec", 0.35)
    pal = ["#080c18", "#121a33", "#1d2c4a", "#2a3e5a", "#3a5a80", "#4a6a90", "#5f88b0", "#7fa8c8",
           "#a0c4dc", "#b8d8ec", "#d8ecf6", "#f0f8fc", "#ffffff", "#2a4a7a"]
    img = finish(rgb, inside, pal)
    return halo(img, d2, lit_dir(dx, dy), "#a0c4dc", width=8, strength=0.85)


def sun():
    yy, xx = np.mgrid[0:S, 0:S]
    dx = (xx + 0.5 - C)
    dy = (yy + 0.5 - C)
    d = np.hypot(dx, dy)
    rs = 70.0
    inside = d <= rs
    ang = np.arctan2(dy, dx)
    nz = np.sqrt(np.clip(1 - (d / rs) ** 2, 0, 1))
    gran = Noise3(2401).fbm(dx / 9.0, dy / 9.0, nz * 3.0, 4)
    limb = nz ** 0.45
    t = np.clip(0.25 + 0.75 * limb + (gran - 0.5) * 0.35, 0, 1)
    c_lo, c_mid, c_hi, c_core = (hex_arr(["#c8741f"])[0], hex_arr(["#ffb347"])[0],
                                 hex_arr(["#ffd98a"])[0], hex_arr(["#fff6d0"])[0])
    rgb = np.zeros((S, S, 3), np.float32)
    for i in range(3):
        rgb[..., i] = np.interp(t, [0, 0.45, 0.75, 1.0], [c_lo[i], c_mid[i], c_hi[i], c_core[i]])
    # corona: angular streamers + glow falloff, transparent towards the edges
    streak = np.zeros_like(d)
    for k, (f, ph, amp) in enumerate([(5, 0.3, 0.35), (9, 1.7, 0.25), (14, 4.1, 0.15)]):
        streak += amp * (0.5 + 0.5 * np.sin(ang * f + ph))
    ray_len = 22 + 34 * streak
    ca = np.clip(1 - (d - rs) / ray_len, 0, 1) ** 1.8 * (d > rs)
    glow = np.clip(1 - (d - rs) / 58.0, 0, 1) ** 2.4 * (d > rs) * 0.8
    alpha = np.clip(np.maximum(ca, glow), 0, 1)
    cc = np.zeros((S, S, 3), np.float32)
    tt = np.clip(alpha, 0, 1)
    for i in range(3):
        cc[..., i] = np.interp(tt, [0, 0.4, 0.8, 1.0], [c_lo[i] * 0.8, c_lo[i], c_mid[i], c_hi[i]])
    rgb = np.where(inside[..., None], rgb, cc)
    pal = ["#8a3a12", "#c8741f", "#e8902a", "#ffb347", "#ffc86a", "#ffd98a", "#ffe8b0", "#fff6d0",
           "#ffffff", "#a34a1a"]
    img = quantize_dither(rgb, pal, bayer(S, S, 4), 1.0)
    a = dither_alpha(alpha, levels=(0, 40, 90, 150, 210, 255))
    img[..., 3] = np.where(inside, 255, a)
    return img


# ---------------------------------------------------------------------------
# banner + map background
# ---------------------------------------------------------------------------
def banner():
    """480x270 vista: parallax layers composed with offsets + a foreground ruin ledge."""
    W, H = 480, 270
    img = new(W, H, "#0b0f1e")
    # (layer, x offset, y offset, visible x-range in the banner)
    layers = [("sky", 104, 23, None), ("far", 150, 40, None), ("mid", 300, -48, None),
              ("abyss", 0, 64, None), ("near", 380, 92, (318, W))]
    for name, ox, oy, xr in layers:
        lay = load_png(f"backgrounds/sylvanox/{name}.png")
        tiled = np.concatenate([lay, lay], axis=1)[:, ox:ox + W]
        crop = new(W, H)
        src0, dst0 = max(0, oy), max(0, -oy)
        n = min(H - dst0, tiled.shape[0] - src0)
        crop[dst0:dst0 + n] = tiled[src0:src0 + n]
        if dst0 + n < H:   # extend the bottom row (opaque ground / mist) downwards
            crop[dst0 + n:] = crop[dst0 + n - 1]
        if xr is not None:
            crop[:, :xr[0]] = 0
            crop[:, xr[1]:] = 0
        paste(img, crop, 0, 0)
    # foreground ledge from the ruin_stone tileset (bottom-left), with decor
    meta = json.load(open(os.path.join(ASSETS, "textures/tiles/ruin_stone.json")))
    atlas = load_png(meta["texture"])
    grid = ["#########.....",
            "##########....",
            "############..",
            "#############."]
    gy0 = H - 16 * len(grid) + 6
    rows, cols = len(grid), len(grid[0])

    def solid(i, j):
        if j >= rows:
            return 0 <= i < cols and grid[rows - 1][i] == "#"
        return 0 <= j < rows and 0 <= i < cols and grid[j][i] == "#"

    for j in range(rows):
        for i in range(cols):
            if not solid(i, j):
                continue
            m = solid(i, j - 1) | (solid(i + 1, j) << 1) | (solid(i, j + 1) << 2) | (
                (solid(i - 1, j) or i == 0) << 3)
            if m == 15:
                cell = atlas[16:32, ((i * 7 + j * 3) % 16) * 16:((i * 7 + j * 3) % 16 + 1) * 16]
            else:
                cell = atlas[0:16, m * 16:(m + 1) * 16]
            paste(img, cell, i * 16, gy0 + j * 16)
            if m == 15:
                for k, (di, dj) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
                    if not solid(i + di, j + dj) and not (i + di < 0):
                        paste(img, atlas[32:48, k * 16:(k + 1) * 16], i * 16, gy0 + j * 16)
    for (i, j, d) in [(1, 0, 3), (3, 0, 1), (6, 0, 0), (8, 0, 2), (9, 1, 5), (11, 2, 1), (12, 3, 0)]:
        if solid(i, j) and not solid(i, j - 1):
            paste(img, atlas[48:64, d * 16:(d + 1) * 16], i * 16, gy0 + j * 16 - 12)
    dmeta = json.load(open(os.path.join(ASSETS, "sprites/decor/sylvanox_decor.json")))
    datlas = load_png(dmeta["texture"])

    def place(name, px, py, frame=0):
        e = dmeta["sprites"][name]
        x, y, w, h = e["rect"]
        spr = datlas[y:y + h, x + frame * w:x + (frame + 1) * w]
        paste(img, spr, px - e["pivot"][0], py - e["pivot"][1])

    place("ruin_pillar_broken", 72, gy0)
    place("mushroom_tall", 30, gy0)
    place("statue_head", 124, gy0 + 16)
    place("glow_fern", 104, gy0)
    place("mushroom_small", 150, gy0 + 16 + 16)
    fg = load_png("backgrounds/sylvanox/fg_vines.png")
    canopy = np.concatenate([fg, fg], 1)[0:44, 330:330 + W].copy()
    canopy[36:, :, 3] = np.where(canopy[36:, :, 3] > 0, canopy[36:, :, 3], 0)
    paste(img, canopy, 0, -8)
    return img


def map_background():
    W, H = 960, 540
    n1 = fbm(W, H, 240, 6, seed=2501, wrap_x=True, cell_y=240)
    n2 = fbm(W, H, 120, 5, seed=2502, wrap_x=True, cell_y=120)
    n3 = fbm(W, H, 60, 4, seed=2503, wrap_x=True, cell_y=60)
    yy, xx = np.mgrid[0:H, 0:W]
    # diagonal galactic band
    band = np.exp(-(((yy - 0.42 * xx - 150) / 150.0) ** 2))
    base = hex_arr(["#070a16"])[0]
    rgb = np.ones((H, W, 3), np.float32) * base
    neb = smoothstep(0.36, 0.66, n1) * (0.2 + 0.8 * band)
    violet, teal, deep = hex_arr(["#5a32a0"])[0], hex_arr(["#1f7a8a"])[0], hex_arr(["#1a2747"])[0]
    mixf = smoothstep(0.35, 0.65, n2)[..., None]
    nc = violet * (1 - mixf) + teal * mixf
    rgb = rgb * (1 - neb[..., None] * 0.8) + nc * neb[..., None] * 0.8
    dust = smoothstep(0.5, 0.75, n3) * band * 0.6
    rgb = rgb * (1 - dust[..., None] * 0.6) + deep * dust[..., None] * 0.6
    hot = smoothstep(0.6, 0.8, n1) * smoothstep(0.5, 0.7, n2) * band
    rgb = rgb * (1 - hot[..., None] * 0.7) + hex_arr(["#a080e0"])[0] * hot[..., None] * 0.7
    hot2 = smoothstep(0.62, 0.8, n1) * (1 - smoothstep(0.3, 0.5, n2)) * band
    rgb = rgb * (1 - hot2[..., None] * 0.6) + hex_arr(["#39c8c0"])[0] * hot2[..., None] * 0.6
    amber = smoothstep(0.72, 0.85, n3) * smoothstep(0.6, 0.75, n1) * 0.5
    rgb = rgb * (1 - amber[..., None]) + hex_arr(["#6a3a3a"])[0] * amber[..., None]
    # dark dust lanes cutting the band
    lane = smoothstep(0.62, 0.7, 1 - np.abs(n2 - 0.5) * 2) * band * 0.7
    rgb = rgb * (1 - lane[..., None] * 0.7)
    pal = ["#05070f", "#070a16", "#0b0f1e", "#101830", "#14203c", "#1a2747", "#22305a", "#2a2052",
           "#3a2a6a", "#4a2a80", "#5c3a92", "#7a56b8", "#8a64c8", "#14404e", "#1d6070", "#2d7a84",
           "#3a2a3a", "#6a3a3a", "#b09ad8", "#a080e0", "#2a9a9a", "#39c8c0", "#6a4aa8"]
    img = quantize_dither(rgb, pal, bayer(H, W, 8), 1.0)
    r = rng(2504)
    for i in range(2600):
        x, y = int(r.random() * W), int(r.random() * H)
        b = r.random()
        in_band = band[y, x]
        if b < 0.6 - 0.2 * in_band:
            put(img, x, y, "#2e3f66")
        elif b < 0.86:
            put(img, x, y, "#6c819a")
        elif b < 0.97:
            put(img, x, y, ["#c3d0de", "#8ffff0", "#e08aff", "#ffd98a"][i % 4])
        else:
            put(img, x, y, "#ffffff")
            for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                put(img, x + ddx, y + ddy, "#8fa0c8")
            if i % 3 == 0:
                for ddx, ddy in ((2, 0), (-2, 0), (0, 2), (0, -2)):
                    put(img, x + ddx, y + ddy, "#3a4a70")
    # a distant spiral galaxy
    for i in range(500):
        t = i / 499 * 4 * math.pi
        for arm in (0, math.pi):
            rr = 2 + t * 2.4
            gx = 760 + rr * math.cos(t + arm)
            gy = 110 + rr * math.sin(t + arm) * 0.45
            if r.random() < 0.6:
                put(img, gx + r.normal() * 1.2, gy + r.normal() * 0.8,
                    "#8a64c8" if rr > 14 else "#c8b8f0")
    for (gx, gy, c) in [(760, 110, "#ffffff"), (759, 110, "#e8e0ff"), (761, 110, "#e8e0ff"),
                        (760, 109, "#c8b8f0"), (760, 111, "#c8b8f0")]:
        put(img, gx, gy, c)
    img[..., 3] = 255
    return img


def generate():
    out = []
    for name, fn in [("sylvanox", sylvanox), ("pyralis", pyralis), ("glacior", glacior),
                     ("sun", sun)]:
        img = fn()
        assert img.shape == (S, S, 4)
        out.append(save_png(img, f"ui/planets/{name}.png"))
    out.append(save_png(banner(), "ui/planets/sylvanox_banner.png"))
    out.append(save_png(map_background(), "ui/map_background.png"))
    return out
