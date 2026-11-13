"""Sky layer: dusk gradient, nebula, stars, ringed gas giant and two moons."""
import math

import numpy as np

from helpers import (apply_ramp, bayer, fbm, gradient_rgb, hex_arr, put, rng, smoothstep,
                     quantize_dither, value_noise)

W, H = 640, 360

SKY_STOPS = [(0.0, "#0b0f1e"), (0.2, "#101830"), (0.4, "#172342"), (0.56, "#1b3150"),
             (0.68, "#1d4458"), (0.78, "#284a6c"), (0.86, "#3c3f78"), (0.93, "#553a86"),
             (1.0, "#683e90")]
NEB_A = "#4a2a80"
NEB_B = "#1d6070"
NEB_HI = "#7a56b8"


def sky_palette():
    g = gradient_rgb(44, SKY_STOPS)
    pal = ["#%02x%02x%02x" % tuple(int(v) for v in c) for c in g]
    pal += ["#2a2052", "#3a2a6a", NEB_A, "#5c3a92", NEB_HI, "#1b4a60", NEB_B, "#2d7a84",
            "#22305a", "#2b2a5e"]
    return pal


def gas_giant(img, cx, cy, R, tilt_deg=-16.0, seed=7):
    """Ringed gas giant with banded atmosphere, lit from the upper left."""
    t = math.radians(tilt_deg)
    ca, sa = math.cos(t), math.sin(t)
    L = np.array([-0.62, -0.42, 0.66])
    L /= np.linalg.norm(L)
    x0, x1 = int(cx - R * 2.3), int(cx + R * 2.3)
    y0, y1 = int(cy - R * 1.2), int(cy + R * 1.2)
    bw, bh = x1 - x0, y1 - y0
    yy, xx = np.mgrid[y0:y1, x0:x1]
    dx = (xx + 0.5 - cx) / R
    dy = (yy + 0.5 - cy) / R
    rx = dx * ca + dy * sa
    ry = -dx * sa + dy * ca
    r2 = dx * dx + dy * dy
    inside = r2 <= 1.0
    nz = np.sqrt(np.clip(1 - r2, 0, 1))
    lam = np.clip(dx * L[0] + dy * L[1] + nz * L[2], 0, 1)
    n = value_noise(bw, bh, 18, 6, seed=seed, wrap_x=False)
    n2 = value_noise(bw, bh, 9, 4, seed=seed + 1, wrap_x=False)
    lat = ry + 0.06 * (n - 0.5) + 0.03 * (n2 - 0.5)
    bands = np.sin(lat * 31.0 + 4 * n) * 0.5 + 0.5
    zone = (np.floor((lat + 1.2) * 5.3 + 0.8 * np.sin(lat * 9.0)).astype(int)) % 4
    ramps = [
        ["#0d1428", "#17243f", "#22385a", "#2f5274", "#43728f", "#6b9cb0", "#a2c8d0"],   # teal
        ["#120f2a", "#1f1a42", "#2f2860", "#43387d", "#5e4c98", "#8370b4", "#b3a2d4"],   # lilac
        ["#0d1428", "#18233f", "#253a5c", "#36577a", "#4f7d9a", "#7aa8bc", "#b6d6dc"],   # pale teal
        ["#120f2a", "#211a40", "#33285a", "#4a3a76", "#6a5694", "#9580b4", "#c7b8de"],   # violet
    ]
    shade = lam ** 0.8 * (0.78 + 0.22 * bands)
    tone = np.clip(0.06 + 0.94 * shade, 0, 1)
    dith = bayer(bh, bw, 4, x0, y0)
    planet = np.zeros((bh, bw, 4), np.uint8)
    for z in range(4):
        layer = apply_ramp(tone, ramps[z], dith, 0.8)
        sel = inside & (zone == z)
        planet[sel] = layer[sel]
    # great storm spot
    spot = ((rx - 0.28) / 0.16) ** 2 + ((ry - 0.32) / 0.07) ** 2 <= 1
    spot_tone = np.clip(tone * 0.9 + 0.05, 0, 1)
    sl = apply_ramp(spot_tone, ["#1a1030", "#3a2050", "#6a3a6a", "#9a5a7a", "#c88a90"], dith, 0.7)
    planet[spot & inside] = sl[spot & inside]
    # ring geometry (ring plane seen from slightly above)
    rho = np.sqrt(rx * rx + (ry / 0.24) ** 2)
    ring_bands = [(1.32, 1.55, 3), (1.58, 1.86, 4), (1.9, 1.97, 2), (2.0, 2.12, 1)]
    ring_ramp = ["#2a3358", "#3f4d78", "#5d6f98", "#8496bc", "#b0c0da"]
    ring = np.zeros((bh, bw), int) - 1
    for (a, b, lvl) in ring_bands:
        ring[(rho >= a) & (rho <= b)] = lvl
    front = ry > 0
    # light on the ring: darker on the night side (right), shadowed behind the planet
    ring_lvl = ring.astype(np.float32)
    ring_lvl -= np.where(rx > 0.2, 1.0, 0.0)
    in_shadow = (rx > 0) & (np.abs(ry - rx * 0.0) < 0.9) & (~front) & (rx < 1.2) & (rho < 1.7)
    ring_lvl -= np.where(in_shadow, 1.0, 0)
    ring_px = np.clip(np.round(ring_lvl), 0, 4).astype(int)
    ring_col = hex_arr(ring_ramp)[ring_px]
    has_ring = ring >= 0
    # thin outer band rendered translucent via checker
    outer = (rho >= 2.0) & (rho <= 2.12)
    checker = ((xx + yy) % 2) == 0
    has_ring &= ~outer | checker
    # planet shadow across the ring behind it is implicit; ring shadow on the planet:
    ring_shadow = inside & (np.abs(ry - 0.24 * 1.45 * 0.0 - (0.30)) < 0.035) & (rx < 0.95)
    # compose: back ring, planet, front ring
    out = img[y0:y1, x0:x1]
    back = has_ring & ~front & ~inside
    out[back, :3] = ring_col[back].astype(np.uint8)
    out[inside] = planet[inside]
    sh = ring_shadow & (planet[..., 3] > 0)
    out[sh, :3] = (out[sh, :3] * 0.55).astype(np.uint8)
    fr = has_ring & front
    out[fr, :3] = ring_col[fr].astype(np.uint8)
    # atmosphere rim on the lit limb
    rim = (r2 > 1.0) & (r2 <= (1 + 2.2 / R) ** 2)
    lit_side = (dx * L[0] + dy * L[1]) > 0.05
    a1 = rim & lit_side & ~(has_ring & front)
    out[a1, :3] = hex_arr(["#5a9ab8"])[0].astype(np.uint8)
    rim2 = (r2 > (1 + 2.2 / R) ** 2) & (r2 <= (1 + 4.0 / R) ** 2) & lit_side & checker
    rim2 &= ~(has_ring & front)
    out[rim2, :3] = (out[rim2, :3] * 0.5 + hex_arr(["#3a6a94"])[0] * 0.5).astype(np.uint8)
    img[y0:y1, x0:x1] = out
    return img


def moon(img, cx, cy, r, ramp, craters=(), light=(-0.7, -0.5, 0.5)):
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
        tone = np.where(c & ~rimc, tone * 0.72, tone)
        tone = np.where(c & rimc, np.minimum(1, tone * 1.05), tone)
    m = apply_ramp(tone, ramp, bayer(sz, sz, 4, x0, y0), 0.8)
    sub = img[y0:y0 + sz, x0:x0 + sz]
    sub[inside] = m[inside]


def sky():
    g = gradient_rgb(H, SKY_STOPS)
    rgb = np.repeat(g[:, None, :], W, 1).astype(np.float32)
    n1 = fbm(W, H, 160, 5, seed=11, wrap_x=True)
    n2 = fbm(W, H, 80, 4, seed=23, wrap_x=True)
    xs = np.arange(W)
    centre = 112 + 42 * np.sin(2 * np.pi * xs / W + 0.6) + 16 * np.sin(4 * np.pi * xs / W + 1.3)
    yy = np.arange(H)[:, None]
    band = np.exp(-((yy - centre[None, :]) / 58.0) ** 2)
    dens = smoothstep(0.40, 0.78, n1) * band
    dens *= 0.55 + 0.45 * smoothstep(0.3, 0.7, n2)
    na, nb = hex_arr([NEB_A])[0], hex_arr([NEB_B])[0]
    mixf = smoothstep(0.35, 0.65, n2)[..., None]
    neb = na * (1 - mixf) + nb * mixf
    k = (dens * 0.7)[..., None]
    rgb = rgb * (1 - k) + neb * k
    fil = smoothstep(0.66, 0.82, n1) * band * smoothstep(0.5, 0.7, n2)
    rgb = rgb * (1 - fil[..., None] * 0.5) + hex_arr([NEB_HI])[0] * fil[..., None] * 0.5
    img = quantize_dither(rgb, sky_palette())
    # stars (denser high up, none in the bright horizon)
    r = rng(5)
    stars = []
    for i in range(900):
        x, y = int(r.random() * W), int(r.random() * H)
        if r.random() > (1 - y / H) ** 1.8:
            continue
        b = r.random()
        stars.append((x, y, b))
    for (x, y, b) in stars:
        if b < 0.55:
            put(img, x, y, "#2e3f66")
        elif b < 0.82:
            put(img, x, y, "#6c819a")
        elif b < 0.95:
            put(img, x, y, "#c3d0de")
        else:
            put(img, x, y, "#e6fffb")
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                put(img, (x + dx) % W, y + dy, "#6c8aa8")
    # a few coloured stars
    for (x, y, c) in [(47, 30, "#8ffff0"), (233, 18, "#e08aff"), (312, 66, "#ffd98a"),
                      (598, 96, "#8ffff0"), (12, 150, "#e08aff")]:
        put(img, x, y, c)
    moon(img, 128, 62, 10.5, ["#151a33", "#262c4c", "#3c4268", "#5d6488", "#8a8fb0", "#c3c6dc"],
         craters=[(-0.3, -0.2, 0.25), (0.2, 0.35, 0.18), (-0.45, 0.4, 0.14)])
    moon(img, 586, 44, 5.5, ["#10202a", "#1d3a44", "#2f5d66", "#4f8a8e", "#8fc8c4"],
         craters=[(-0.2, -0.1, 0.3)])
    gas_giant(img, 404, 118, 70)
    img[..., 3] = 255
    return img
