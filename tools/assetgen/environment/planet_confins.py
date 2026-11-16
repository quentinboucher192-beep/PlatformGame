"""Planet-map art of Les Confins (2.48), the scorched edge of the system beyond the dying star,
drawn like the other planets (planets.py, banners.py, planet_aethra.py):

  ui/planets/confins.png         256x256, transparent: a world of black glass. Faceted obsidian
                                 catching violet glints on the lit side, its facet seams glowing
                                 ember-red on the night side, grey ash plains in bands, a few long
                                 rivers of living fire, a red-violet rim light from the dying star
                                 behind it, a thin ring of embers and a small orbiting glass shard
  ui/planets/confins_banner.png  480x270 vista for the planet panel: a dim red-violet sky with the
                                 swollen dying star (and the dragon Pyrrhax crossing it), existing
                                 parallax layers recoloured as obsidian peaks and cliffs over a river
                                 of living fire, black-glass shards, a foreground ledge of the
                                 obsidian tileset (autotiled like the engine) with its decorations,
                                 a few props and drifting embers

Everything is deterministic (fixed seeds); only these two files are written.
"""
import json
import math
import os

import numpy as np

from helpers import (ASSETS, Noise3, add_glow, bayer, dither_alpha, hex_arr, load_png, new, paste,
                     poly_mask, put, quantize_dither, rng, save_png, smoothstep)
from planets import C, LIGHT, R, S, finish, halo, lit_dir, rotate, sphere

PR = R * 0.84                 # planet radius (room for the ember ring and the shard)
RING_TILT = 0.30              # the ember ring leans the other way from Aethra's Halo


# ---------------------------------------------------------------------------
# the planet
# ---------------------------------------------------------------------------
def _facets(x, y, z, n=110, seed=4801):
    """Voronoi cells on the sphere: nearest / second-nearest seed (black-glass facets)."""
    r = rng(seed)
    p = r.normal(size=(n, 3))
    p /= np.linalg.norm(p, axis=1, keepdims=True)
    pts = np.stack([x, y, z], -1).astype(np.float32)
    dot = pts @ p.T.astype(np.float32)                       # (H, W, n)
    d = np.sqrt(np.clip(2.0 - 2.0 * dot, 0, None))           # chord distance on the unit sphere
    order = np.argsort(d, axis=-1)
    i1 = order[..., 0]
    d1 = np.take_along_axis(d, order[..., :1], -1)[..., 0]
    d2 = np.take_along_axis(d, order[..., 1:2], -1)[..., 0]
    tilt = r.normal(size=(n, 3)) * 0.3       # each facet's own slant
    shade = r.random(n)                      # and its own depth of black
    return i1, d2 - d1, tilt, shade


def planet():
    dx, dy, nz, d2, inside, lam = sphere(r=PR)
    x, y, z = rotate(dx, dy, nz, -0.3, 3.3)
    fid, gap, tilt, fshade = _facets(x, y, z, n=150)
    # faceted normals in screen space -> diffuse + a sharp glassy specular
    nx = dx + tilt[fid, 0]
    ny = dy + tilt[fid, 1]
    nn = nz + np.abs(tilt[fid, 2]) * 0.5
    norm = np.sqrt(nx * nx + ny * ny + nn * nn) + 1e-6
    nx, ny, nn = nx / norm, ny / norm, nn / norm
    ndl = np.clip(nx * LIGHT[0] + ny * LIGHT[1] + nn * LIGHT[2], 0, 1)
    refl_z = 2 * ndl * nn - LIGHT[2]                 # reflected light towards the viewer (0,0,1)
    spec = np.clip(refl_z, 0, 1) ** 18 * smoothstep(0.02, 0.3, lam)
    glass_d, glass_m = hex_arr(["#0c0814"])[0], hex_arr(["#2a1c3c"])[0]
    base = glass_d + (glass_m - glass_d) * (0.25 + 0.75 * fshade[fid])[..., None] * 0.6
    rgb = base * (0.35 + 0.95 * ndl * lam ** 0.5)[..., None]
    # the glass mirrors the violet sky: a soft reflection that breaks from facet to facet
    mirror = smoothstep(0.55, 0.95, ndl) * smoothstep(0.1, 0.5, lam) * (0.15 + 0.3 * fshade[fid])
    rgb = rgb * (1 - mirror[..., None]) + hex_arr(["#4e3a6c"])[0] * mirror[..., None]
    sheen = hex_arr(["#8a6ab8"])[0]
    glint = hex_arr(["#e4d8ff"])[0]
    s1 = np.clip(spec * 1.6, 0, 1)
    rgb = rgb * (1 - s1[..., None] * 0.75) + sheen * s1[..., None] * 0.75
    s2 = smoothstep(0.55, 0.9, spec)
    rgb = rgb * (1 - s2[..., None]) + glint * s2[..., None]
    # ash plains: grey-mauve bands, warped, matte (they drown the glints)
    warp = Noise3(4802).fbm(x * 2.2, y * 2.2, z * 2.2, 4)
    band = 0.5 + 0.5 * np.sin(y * 8.0 + (warp - 0.5) * 5.0)
    patch = Noise3(4803).fbm(x * 2.5 + 7, y * 7.0, z * 2.5, 4)
    ash = smoothstep(0.66, 0.84, band) * smoothstep(0.4, 0.58, patch)
    ash_c = hex_arr(["#3c3442"])[0] + (hex_arr(["#7a7082"])[0] - hex_arr(["#3c3442"])[0]) * \
        smoothstep(0.55, 0.85, patch)[..., None]
    ash_lit = ash_c * (0.2 + 0.8 * lam ** 0.8)[..., None]
    a = ash * 0.85
    rgb = rgb * (1 - a[..., None]) + ash_lit * a[..., None]
    # rivers of living fire: a few long ridged-noise courses, the rest of the network masked off
    rv = 1 - np.abs(Noise3(4824).fbm(x * 1.25 + 3, y * 1.25, z * 1.25, 4) - 0.5) * 2
    course = smoothstep(0.4, 0.56, Noise3(4825).fbm(x * 1.1, y * 1.1 + 5, z * 1.1, 3))
    near_fire = smoothstep(0.8, 0.95, rv) * course
    # facet seams (only where the glass is cracked): catch the light by day, smoulder
    # ember-red by night and around the fire
    cracked = smoothstep(0.46, 0.6, Noise3(4807).fbm(x * 2.6, y * 2.6, z * 2.6 + 9, 3))
    seam = (1 - smoothstep(0.0, 0.026, gap)) * (1 - ash) * np.maximum(cracked, near_fire)
    night = 1 - smoothstep(0.0, 0.5, lam)
    day_edge = seam * smoothstep(0.15, 0.6, lam) * 0.6
    rgb = rgb * (1 - day_edge[..., None]) + hex_arr(["#6a4c8c"])[0] * day_edge[..., None]
    ember = seam * np.maximum(night, near_fire) * 0.85
    rgb = rgb * (1 - ember[..., None]) + hex_arr(["#8a1a3a"])[0] * ember[..., None]
    bloom = smoothstep(0.86, 0.975, rv) * course * 0.55
    rgb = rgb * (1 - bloom[..., None]) + hex_arr(["#5a1028"])[0] * bloom[..., None]
    river = smoothstep(0.965, 0.98, rv) * course
    core = smoothstep(0.987, 0.995, rv) * course
    fire = hex_arr(["#e8402a"])[0] + (hex_arr(["#ff9a3a"])[0] - hex_arr(["#e8402a"])[0]) * \
        smoothstep(0.6, 0.9, course)[..., None]
    rgb = rgb * (1 - river[..., None]) + fire * river[..., None]
    rgb = rgb * (1 - core[..., None]) + hex_arr(["#ffe08a"])[0] * core[..., None]
    # limb: violet scattering on the lit side, the dying star's crimson back-light opposite it
    edge = smoothstep(0.78, 1.0, d2) * inside
    ld = lit_dir(dx, dy)
    back = np.clip(-((dx * LIGHT[0] + dy * LIGHT[1]) / (np.sqrt(d2) + 1e-6)) /
                   np.hypot(LIGHT[0], LIGHT[1]), 0, 1)
    k1 = edge * 0.45 * (0.3 + 0.7 * ld)
    rgb = rgb * (1 - k1[..., None]) + hex_arr(["#a070d0"])[0] * k1[..., None]
    k2 = smoothstep(0.86, 1.0, d2) * inside * back ** 1.5 * 0.7
    rgb = rgb * (1 - k2[..., None]) + hex_arr(["#d0406a"])[0] * k2[..., None]
    pal = ["#050308", "#0c0814", "#140e1e", "#1e1530", "#2a1c3c", "#3a2a52", "#4e3a6c", "#6a4c8c",
           "#8a6ab8", "#b49ae0", "#e4d8ff",
           "#2c2630", "#3c3442", "#564c5c", "#7a7082",
           "#3a0a1c", "#5a1028", "#8a1a3a", "#c02a3a", "#e8402a", "#ff7a2a", "#ffa040", "#ffe08a",
           "#a070d0", "#d0406a"]
    img = finish(rgb, inside, pal)
    img = halo(img, d2, ld, "#9a5ad0", width=7, strength=0.75, r=PR)
    img = halo(img, d2, back, "#d0406a", width=9, strength=0.9, r=PR)
    _ember_ring(img)
    _shard(img)
    return img


def _ring_xy(a, rx, ry):
    c, s = math.cos(RING_TILT), math.sin(RING_TILT)
    px, py = math.cos(a) * rx, math.sin(a) * ry
    return C + px * c - py * s, C + px * s + py * c


def _ember_ring(img):
    """A thin belt of embers: a faint crimson band strewn with sparks; the back half passes
    behind the planet, the front half in front of it."""
    rx, ry = R * 1.2, R * 0.27
    r = rng(4806)
    front = new(S, S)
    for k in range(1400):
        a = 2 * math.pi * k / 1400
        is_front = math.sin(a) > 0
        px, py = _ring_xy(a, rx, ry)
        lit = math.cos(a) < -0.2              # the side facing the light glows warmer
        for off, c in ((0.0, "#c02a3a" if lit else "#8a1a3a"), (1.0, "#5a1028")):
            qx, qy = int(px), int(py + off)
            if not (0 <= qx < S and 0 <= qy < S):
                continue
            if is_front:
                put(front, qx, qy, c)
            elif img[qy, qx, 3] == 0:
                put(img, qx, qy, c)
    sparks = ["#8a1a3a", "#c02a3a", "#e8402a", "#ff7a2a", "#ffa040", "#ffe08a"]
    for _ in range(420):
        a = r.random() * 2 * math.pi
        sc = 1.0 + r.normal() * 0.025
        px, py = _ring_xy(a, rx * sc, ry * sc)
        py += r.normal() * 0.6
        qx, qy = int(px), int(py)
        if not (1 <= qx < S - 1 and 1 <= qy < S - 1):
            continue
        b = r.random()
        c = sparks[min(len(sparks) - 1, int(b ** 1.6 * len(sparks)))]
        if math.sin(a) > 0:
            put(front, qx, qy, c)
            if b > 0.93:          # the brightest embers get a tiny cross of glow
                for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    if front[qy + oy, qx + ox, 3] == 0:
                        put(front, qx + ox, qy + oy, "#c02a3a")
        elif img[qy, qx, 3] == 0:
            put(img, qx, qy, c)
    paste(img, front, 0, 0)


def _shard(img):
    """A small sliver of black glass orbiting the planet (top right), lit on its upper-left
    faces, a seam of ember inside it."""
    cx, cy = 214, 46
    pts = [(cx - 2, cy - 15), (cx + 6, cy - 5), (cx + 4, cy + 9), (cx - 3, cy + 14), (cx - 7, cy + 2)]
    m = poly_mask(S, S, pts)
    yy, xx = np.mgrid[0:S, 0:S]
    # two faces split along the long axis: the left one faces the light
    left = (xx - (cx - 2)) * (cy + 14 - (cy - 15)) - (yy - (cy - 15)) * (cx - 3 - (cx - 2)) < 0
    shard = new(S, S)
    shard[m & left] = (78, 58, 108, 255)        # "#4e3a6c"
    shard[m & ~left] = (20, 14, 30, 255)        # "#140e1e"
    for (x0, y0), (x1, y1), c in [((cx - 2, cy - 15), (cx - 7, cy + 2), "#b49ae0"),
                                  ((cx - 2, cy - 15), (cx + 6, cy - 5), "#8a6ab8"),
                                  ((cx - 2, cy - 14), (cx - 3, cy + 13), "#8a1a3a")]:
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n):
            t = i / max(n - 1, 1)
            qx, qy = int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t))
            if m[qy, qx]:
                put(shard, qx, qy, c)
    put(shard, cx - 3, cy - 11, "#e4d8ff")
    put(shard, cx - 2, cy + 3, "#e8402a")
    paste(img, shard, 0, 0)


# ---------------------------------------------------------------------------
# the banner
# ---------------------------------------------------------------------------
BW, BH = 480, 270
STAR = (150.0, 92.0, 46.0)            # the dying star in the banner sky: centre x, y, radius


def remap(img, ramp, gain=1.0):
    """Luminance of `img` mapped onto a colour ramp (dark -> light); alpha kept."""
    rgb = img[..., :3].astype(np.float32)
    lum = np.clip((0.3 * rgb[..., 0] + 0.59 * rgb[..., 1] + 0.11 * rgb[..., 2]) / 255.0 * gain, 0, 1)
    cols = hex_arr(ramp)
    xs = np.linspace(0, 1, len(ramp))
    out = img.copy()
    for i in range(3):
        out[..., i] = np.clip(np.interp(lum, xs, cols[:, i]), 0, 255).astype(np.uint8)
    return out


def _sky():
    yy, xx = np.mgrid[0:BH, 0:BW].astype(np.float32)
    t = yy / (BH - 1)
    stops = hex_arr(["#0c0614", "#1e0c26", "#3a1232", "#5a1a30"])
    rgb = np.zeros((BH, BW, 3), np.float32)
    for i in range(3):
        rgb[..., i] = np.interp(t, [0.0, 0.35, 0.7, 1.0], stops[:, i])
    # ash haze: long horizontal streaks
    n = Noise3(4810)
    haze = n.fbm(xx / 90.0, yy / 9.0, np.full_like(xx, 0.5), 4)
    hz = smoothstep(0.5, 0.75, haze) * smoothstep(0.15, 0.75, t) * 0.45
    rgb = rgb * (1 - hz[..., None]) + hex_arr(["#4a2a40"])[0] * hz[..., None]
    # the dying star: a swollen, dim crimson giant with a darker spotted face
    sx, sy, sr = STAR
    ddx, ddy = xx + 0.5 - sx, yy + 0.5 - sy
    d = np.hypot(ddx, ddy)
    disc = d <= sr
    nzs = np.sqrt(np.clip(1 - (d / sr) ** 2, 0, 1))
    spots = n.fbm(ddx / 11.0 + 20, ddy / 11.0, nzs * 2.0, 4)
    tt = np.clip(0.2 + 0.6 * nzs ** 0.6 + (spots - 0.5) * 0.6, 0, 1)
    sc = hex_arr(["#4a0e24", "#8a1a3a", "#c8384a", "#ff7a5a", "#ffc0a0"])
    for i in range(3):
        rgb[..., i] = np.where(disc, np.interp(tt, [0, 0.3, 0.55, 0.8, 1.0], sc[:, i]), rgb[..., i])
    glow = np.clip(1 - (d - sr) / 110.0, 0, 1) ** 2.4 * (d > sr)
    rgb = rgb * (1 - glow[..., None] * 0.55) + hex_arr(["#a02a4a"])[0] * glow[..., None] * 0.55
    # stars, fewer in the star's glow
    r = rng(4811)
    for _ in range(320):
        x, y = int(r.integers(0, BW)), int(r.integers(0, int(BH * 0.7)))
        if d[y, x] < sr + 40:
            continue
        b = r.uniform(0.35, 1.0) * (1 - t[y, x])
        rgb[y, x] = rgb[y, x] * (1 - b) + np.array([235, 220, 255]) * b
    pal = ["#0c0614", "#160a20", "#1e0c26", "#2a0e2c", "#3a1232", "#4a1632", "#5a1a30", "#4a2a40",
           "#4a0e24", "#6a1430", "#8a1a3a", "#a02a4a", "#c8384a", "#e85a52", "#ff7a5a", "#ffa080",
           "#ffc0a0", "#8a7aa0", "#d8ccf0"]
    img = quantize_dither(rgb, pal, bayer(BH, BW, 4), 1.0)
    _dragon(img, sx + 6, sy + 4)
    return img


def _dragon(img, cx, cy):
    """Pyrrhax gliding across the face of the dying star: a black silhouette, wings raised,
    long neck and horned head forward (right), whip tail with a spade behind."""
    def P(pts):
        return [(cx + px, cy + py) for px, py in pts]
    body = P([(-16, 1), (-8, -3), (4, -4), (11, -6), (16, -10), (20, -13), (23, -13), (27, -11),
              (31, -10), (27, -8), (22, -8), (18, -5), (12, -1), (4, 3), (-6, 4), (-16, 4)])
    horn = P([(21, -13), (17, -18), (23, -14)])
    tail = P([(-15, 1), (-26, 3), (-36, 7), (-44, 8), (-41, 10), (-34, 10), (-25, 7), (-15, 4)])
    spade = P([(-43, 8), (-49, 5), (-47, 10), (-51, 13), (-42, 11)])
    far_wing = P([(-2, -3), (-6, -14), (-14, -24), (-24, -30), (-20, -22), (-30, -24), (-23, -16),
                  (-31, -15), (-20, -9), (-24, -5), (-10, -1)])
    near_wing = P([(2, -4), (4, -16), (2, -30), (0, -42), (8, -33), (12, -36), (14, -24), (20, -26),
                   (17, -15), (22, -14), (10, -3)])
    m = np.zeros((BH, BW), bool)
    for part in (body, horn, tail, spade, far_wing, near_wing):
        m |= poly_mask(BW, BH, part)
    img[m] = (10, 4, 12, 255)
    put(img, int(cx + 27), int(cy - 11), "#ff7a2a")     # the ember of its eye
    put(img, int(cx + 31), int(cy - 9), "#c02a3a")      # and of its breath


def _no_towers(lay, width=23):
    """Remove the opaque shapes narrower than `width` px (the forge chimneys of a recycled
    layer): a horizontal morphological opening of its opaque pixels (with wrap-around); what
    it removes is refilled from the same row a little to the side (sky, haze or slope)."""
    m = lay[..., 3] > 200
    h = width // 2
    W = m.shape[1]

    def runs(mask, k):           # per pixel: number of True in the centred window of width k
        p = np.concatenate([mask[:, -h:], mask, mask[:, :h]], 1).astype(np.int32)
        c = np.concatenate([np.zeros((mask.shape[0], 1), np.int32), np.cumsum(p, 1)], 1)
        return c[:, k:] - c[:, :-k]

    eroded = runs(m, width) == width
    removed = m & ~(runs(eroded, width) > 0)
    out = lay.copy()
    todo = removed.copy()
    cols = np.arange(W)
    for see_through in (True, False):        # prefer refilling with haze / sky over solid rock
        for shift in (24, -24, 36, -36, 48, -48, 64, -64, 96, -96, 128, -128, 160, -160):
            src = (cols - shift) % W
            ok = todo & ~removed[:, src] & (~m[:, src] if see_through else True)
            out[ok] = lay[:, src][ok]
            todo &= ~ok
    out[todo] = 0
    return out


def _layer(lay, ox, oy):
    tiled = np.concatenate([lay, lay], axis=1)[:, ox:ox + BW]
    crop = new(BW, BH)
    src0, dst0 = max(0, oy), max(0, -oy)
    n = min(BH - dst0, tiled.shape[0] - src0)
    crop[dst0:dst0 + n] = tiled[src0:src0 + n]
    if dst0 + n < BH:          # extend the bottom row (opaque ground / glow) downwards
        crop[dst0 + n:] = crop[dst0 + n - 1]
    return crop


def _shards():
    """Near layer: a cluster of black-glass shards on the right, lit violet on their left
    faces, an ember glow at their feet."""
    img = new(BW, BH)
    r = rng(4812)
    base = BH - 22
    yy, xx = np.mgrid[0:BH, 0:BW]
    for x, w, h, lean in [(330, 26, 120, -6), (352, 18, 78, 8), (372, 30, 150, 4), (402, 20, 96, -10),
                          (420, 34, 132, 6), (448, 22, 84, -4), (466, 28, 110, 10), (312, 14, 52, 12)]:
        tip = (x + lean, base - h)
        pts = [(x - w / 2, base + 30), tip, (x + w / 2, base + 30)]
        m = poly_mask(BW, BH, pts)
        # split the shard along its axis: the lit (left) face and the dark (right) face
        lit = xx < tip[0] + (yy - tip[1]) * ((x - tip[0]) / max(base + 30 - tip[1], 1))
        shade = r.uniform(0.0, 1.0)
        img[m & ~lit] = (14, 9, 22, 255)
        img[m & lit] = (40, 28, 60, 255) if shade > 0.4 else (30, 21, 46, 255)
        # bright edge down the lit side and a thin seam
        for i in range(h + 30):
            t = i / (h + 30)
            ex = tip[0] + ((x - w / 2) - tip[0]) * t
            put(img, int(round(ex)), int(tip[1] + i), "#6a4c8c" if t > 0.15 else "#b49ae0")
            sx = tip[0] + (x - tip[0]) * t
            if 0.1 < t < 0.85 and m[min(BH - 1, int(tip[1] + i)), int(round(sx))]:
                put(img, int(round(sx)), int(tip[1] + i), "#2a1c3c")
    glow = new(BW, BH)
    for gx in (340, 390, 440, 470):
        add_glow(glow, gx, BH - 8, 46, "#c02a3a", strength=0.55)
    g = glow[..., :3].astype(np.int32)
    a = img[..., 3] > 0
    img[..., :3] = np.where(a[..., None], np.clip(img[..., :3].astype(np.int32) + g // 2, 0, 255),
                            img[..., :3]).astype(np.uint8)
    return img


BANNER = {
    "material": "obsidian",
    "grid": ["#########.....",
             "##########....",
             "############..",
             "#############."],
    "top_decor": [(1, 0, 0), (3, 0, 4), (6, 0, 1), (8, 0, 6), (9, 1, 5), (11, 2, 0), (12, 3, 7)],
    "decor": [("obsidian_spire", 74, 0), ("skull_rock", 30, 0), ("ember_crystal", 104, 0),
              ("lava_vent", 132, 16), ("ash_rock_big", 160, 32)],
}


def banner():
    img = _sky()
    # distant glass spires on the horizon (Glacior's far peaks, darkened into the haze)
    spires = remap(load_png("backgrounds/glacior/far.png"), ["#1a0c22", "#24102a", "#321834", "#4a2442"], 1.2)
    paste(img, _layer(spires, 400, -12), 0, 0)
    # obsidian volcanoes with fire trickling from their craters (Pyralis' far layer, chimneys removed)
    cones = remap(_no_towers(load_png("backgrounds/pyralis/far.png")),
                  ["#0e0818", "#22142e", "#3a2448", "#6a3a6a", "#ff7a2a", "#ffe08a"], 1.15)
    paste(img, _layer(cones, 300, 40), 0, 0)
    # the river of living fire below (Pyralis' abyss glow, crimson to white-gold)
    abyss = remap(load_png("backgrounds/pyralis/abyss.png"),
                  ["#2a0820", "#8a1a4a", "#e8402a", "#ffa040", "#ffe8b0"], 1.0)
    paste(img, _layer(abyss, 200, 84), 0, 0)
    paste(img, _shards(), 0, 0)
    # foreground ledge (bottom-left) from the obsidian tileset, autotiled like the engine
    cfg = BANNER
    meta = json.load(open(os.path.join(ASSETS, f"textures/tiles/{cfg['material']}.json")))
    atlas = load_png(meta["texture"])
    grid = cfg["grid"]
    gy0 = BH - 16 * len(grid) + 6
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
                v = (i * 7 + j * 3) % 16
                cell = atlas[16:32, v * 16:(v + 1) * 16]
            else:
                cell = atlas[0:16, m * 16:(m + 1) * 16]
            paste(img, cell, i * 16, gy0 + j * 16)
            if m == 15:
                for k, (di, dj) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
                    if not solid(i + di, j + dj) and not (i + di < 0):
                        paste(img, atlas[32:48, k * 16:(k + 1) * 16], i * 16, gy0 + j * 16)
    for (i, j, d) in cfg["top_decor"]:
        if solid(i, j) and not solid(i, j - 1):
            paste(img, atlas[48:64, d * 16:(d + 1) * 16], i * 16, gy0 + j * 16 - 12)
    # props (+ their glow, baked softly like the engine's additive lights)
    dmeta = json.load(open(os.path.join(ASSETS, "sprites/decor/pyralis_decor.json")))
    datlas = load_png(dmeta["texture"])
    glow = new(BW, BH)
    for name, px, dy in cfg["decor"]:
        e = dmeta["sprites"][name]
        x, y, w, h = e["rect"]
        py = gy0 + dy
        paste(img, datlas[y:y + h, x:x + w], px - e["pivot"][0], py - e["pivot"][1])
        if "glow" in e:
            add_glow(glow, px + e["glowOffset"][0], py + e["glowOffset"][1], e["glowRadius"],
                     e["glow"], strength=0.3)
    g = glow[..., :3].astype(np.int32)
    img[..., :3] = np.clip(img[..., :3].astype(np.int32) + g, 0, 255).astype(np.uint8)
    _embers(img)
    img[..., 3] = 255
    return img


def _embers(img):
    """Sparks of living fire drifting up from the river (denser low in the frame)."""
    r = rng(4813)
    for _ in range(90):
        x = int(r.integers(0, BW))
        y = int(BH - 1 - (r.random() ** 1.8) * BH * 0.85)
        c = ["#ffe08a", "#ffa040", "#ff7a2a", "#e8402a", "#d0406a"][int(r.integers(0, 5))]
        put(img, x, y, c)
        if r.random() < 0.25:
            put(img, x, y - 1, "#8a1a3a")


def generate():
    return [save_png(planet(), "ui/planets/confins.png"), save_png(banner(), "ui/planets/confins_banner.png")]


if __name__ == "__main__":
    for p in generate():
        print(p)
