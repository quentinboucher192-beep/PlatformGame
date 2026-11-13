"""VFX textures (docs/ASSET_SPEC.md section 10) in assets/textures/fx/.
White / near-white so the engine can tint them, unless noted (explosion,
shield, teleport, slash edges and warning are coloured)."""
import math

import numpy as np

from common.palette import AMBER, CYAN, DANGER, OUTLINE, SKY, VIOLET, hex_rgba
from raster import outline_rgba
from sheet import save_png, strip

TAU = 2 * math.pi


def _grid(w, h):
    ys, xs = np.mgrid[0:h, 0:w]
    return xs + 0.5, ys + 0.5


def rgba_from(alpha, rgb=(255, 255, 255)):
    h, w = alpha.shape
    img = np.zeros((h, w, 4), np.uint8)
    img[..., 0], img[..., 1], img[..., 2] = rgb
    img[..., 3] = np.clip(alpha * 255 + 0.5, 0, 255).astype(np.uint8)
    img[img[..., 3] == 0] = 0
    return img


def glow():
    x, y = _grid(64, 64)
    r = np.hypot(x - 32, y - 32) / 32.0
    a = np.clip(1 - r, 0, 1) ** 2.2
    return rgba_from(a)


def particle():
    x, y = _grid(8, 8)
    r = np.hypot(x - 4, y - 4) / 4.0
    a = np.clip(1.15 - r, 0, 1)
    a = np.clip(a * 1.6, 0, 1) ** 1.4
    return rgba_from(a)


def spark():
    x, y = _grid(8, 8)
    d = np.sqrt(((x - 4) / 4.0) ** 2 + ((y - 4) / 1.1) ** 2)
    a = np.clip(1.05 - d, 0, 1) ** 0.8
    return rgba_from(np.clip(a * 1.3, 0, 1))


def star():
    img = np.zeros((8, 8, 4), np.uint8)
    W = (255, 255, 255)
    pts = {(3, 3): 255, (4, 3): 255, (3, 4): 255, (4, 4): 255,
           (3, 2): 230, (4, 2): 200, (3, 5): 200, (4, 5): 230, (2, 3): 230, (2, 4): 200, (5, 3): 200, (5, 4): 230,
           (3, 1): 150, (4, 6): 150, (1, 4): 150, (6, 3): 150, (3, 0): 70, (4, 7): 70, (0, 4): 70, (7, 3): 70}
    for (x, y), a in pts.items():
        img[y, x] = (*W, a)
    return img


def ring():
    x, y = _grid(64, 64)
    r = np.hypot(x - 32, y - 32)
    d = np.abs(r - 29.0)
    a = np.clip(1.0 - d / 1.8, 0, 1)
    inner = np.clip(1.0 - np.abs(r - 26.5) / 3.0, 0, 1) * 0.25 * (r < 29)
    return rgba_from(np.clip(a + inner, 0, 1))


def _blobs(w, h, blobs, falloff=1.6):
    x, y = _grid(w, h)
    a = np.zeros((h, w))
    for (cx, cy, r, k) in blobs:
        d = np.hypot(x - cx, y - cy) / r
        a = np.maximum(a, k * np.clip(1 - d, 0, 1) ** (1 / falloff))
    return a


def smoke():
    frames = []
    for i in range(4):
        t = i / 3
        s = 0.75 + 0.6 * t
        k = 1.0 - 0.6 * t
        blobs = [(8, 9 - 2 * t, 4.6 * s, k), (5.2, 10 - t, 3.4 * s, k * 0.9), (11, 9.5 - 1.5 * t, 3.6 * s, k * 0.9),
                 (8.5, 6 - 2 * t, 3.2 * s, k * 0.8)]
        a = _blobs(16, 16, blobs)
        # quantise into 4 alpha levels for a pixel-art feel
        a = np.round(a * 4) / 4
        img = rgba_from(np.clip(a, 0, 1), (236, 240, 246))
        frames.append(img)
    return strip(frames)


def dust():
    frames = []
    rng = np.random.default_rng(3)
    for i in range(5):
        t = i / 4
        img = np.zeros((16, 16, 4), np.uint8)
        k = 1.0 - 0.75 * t
        for side in (-1, 1):
            for j in range(3):
                cx = 8 + side * (1.5 + 5.5 * t + j * 1.2 * t)
                cy = 13.5 - 2.5 * t - j * 0.8
                r = 1.4 + 1.8 * t - j * 0.3
                for y in range(16):
                    for x in range(16):
                        if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r:
                            a = int(255 * k * (0.9 if j == 0 else 0.7))
                            if img[y, x, 3] < a:
                                img[y, x] = (240, 236, 228, a)
        if i < 4:
            for _ in range(3 - i // 2):
                x = int(rng.integers(2, 14))
                y = int(rng.integers(6, 12))
                img[y, x] = (255, 255, 255, int(200 * k))
        frames.append(img)
    return strip(frames)


def slash():
    """Crescent slash (elliptic arc) sweeping top -> bottom in front of the hero."""
    frames = []
    W, H = 48, 32
    x, y = _grid(W, H)
    cx, cy = 6.0, 16.0
    rx, ry = 38.0, 15.2
    dx, dy = (x - cx) / rx, (y - cy) / ry
    rn = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    spans = [(-1.45, -0.55, 0.45), (-1.45, 1.35, 1.0), (-0.75, 1.42, 0.7), (0.2, 1.45, 0.4)]
    cols = [(255, 255, 255, 255), CYAN[3], CYAN[2], CYAN[1], (CYAN[0][0], CYAN[0][1], CYAN[0][2], 170)]
    for i, (a0, a1, k) in enumerate(spans):
        img = np.zeros((H, W, 4), np.uint8)
        t = np.clip((th - a0) / (a1 - a0), 0, 1)
        within = (th >= a0) & (th <= a1)
        thick = 0.36 * k * np.sin(np.pi * np.clip(t * 0.8 + 0.2, 0, 1)) ** 0.6
        m = within & (rn <= 0.98) & (rn >= 0.98 - thick)
        rel = np.clip((0.98 - rn) / np.maximum(thick, 0.03), 0, 1)
        idx = np.clip((rel * 3.2 + (1 - t) * 1.4 + (1 - k) * 1.0).astype(int), 0, 4)
        if i == 3:
            m &= ((x.astype(int) + y.astype(int)) % 3 != 0)
        for j, c in enumerate(cols):
            img[m & (idx == j)] = c
        frames.append(img)
    return strip(frames)


def hit():
    frames = []
    for i in range(5):
        img = np.zeros((24, 24, 4), np.uint8)
        c = 12
        W_ = (255, 255, 255, 255)
        if i == 0:
            for d in range(-2, 3):
                img[c, c + d] = W_
                img[c + d, c] = W_
            img[c - 1:c + 1, c - 1:c + 1] = W_
        elif i in (1, 2):
            L = 9 if i == 1 else 11
            for d in range(-L, L + 1):
                a = 255 if abs(d) < L - 3 else 160
                img[c, c + d] = (255, 255, 255, a)
                img[c + d, c] = (255, 255, 255, a)
            dl = 5 if i == 1 else 7
            for d in range(-dl, dl + 1):
                for s in (1, -1):
                    x, y = c + d, c + s * d
                    if 0 <= x < 24 and 0 <= y < 24:
                        img[y, x] = (255, 255, 255, 220 if abs(d) < dl - 1 else 120)
            rr = 3 if i == 1 else 2
            for y in range(24):
                for x in range(24):
                    if math.hypot(x + 0.5 - c, y + 0.5 - c) < rr + 0.6:
                        img[y, x] = W_
            if i == 2:
                for y in range(24):
                    for x in range(24):
                        if abs(math.hypot(x + 0.5 - c, y + 0.5 - c) - 8.5) < 0.6:
                            img[y, x] = (255, 255, 255, 170)
        elif i == 3:
            for y in range(24):
                for x in range(24):
                    if abs(math.hypot(x + 0.5 - c, y + 0.5 - c) - 10.5) < 0.6 and (x + y) % 2 == 0:
                        img[y, x] = (255, 255, 255, 140)
            for k in range(8):
                a = TAU * k / 8 + 0.2
                for t in (7, 8):
                    img[int(c + math.sin(a) * t), int(c + math.cos(a) * t)] = (255, 255, 255, 200)
        else:
            for k in range(8):
                a = TAU * k / 8 + 0.2
                t = 10
                img[int(c + math.sin(a) * t), int(c + math.cos(a) * t)] = (255, 255, 255, 110)
        frames.append(img)
    return strip(frames)


def explosion():
    """Amber fireball with a white-hot core that breaks up into smoke (coloured)."""
    frames = []
    rng = np.random.default_rng(7)
    jit = rng.random(24)
    hot = [(255, 255, 255, 255), hex_rgba('#fff4d6'), AMBER[2], AMBER[1], DANGER[2], DANGER[1]]
    smoke_c = [SKY[4], SKY[3], SKY[2]]
    # smooth noise for smoke break-up
    x, y = _grid(48, 48)
    noise = np.zeros((48, 48))
    for _ in range(26):
        cx_, cy_, r_ = rng.uniform(4, 44), rng.uniform(4, 44), rng.uniform(3, 7)
        noise = np.maximum(noise, np.clip(1 - np.hypot(x - cx_, y - cy_) / r_, 0, 1))
    R_ = [6, 12, 17, 20, 20, 19, 18, 16]
    heat_ = [1.0, 1.0, 0.85, 0.6, 0.38, 0.22, 0.1, 0.0]
    for i in range(8):
        img = np.zeros((48, 48, 4), np.uint8)
        t = i / 7
        R, heat = R_[i], heat_[i]
        dx, dy = x - 24, y - 24 + 3 * t * t
        d = np.hypot(dx, dy)
        a = (np.arctan2(dy, dx) / TAU) % 1.0
        k = a * 24
        k0 = k.astype(int) % 24
        f = k - np.floor(k)
        j = jit[k0] * (1 - f) + jit[(k0 + 1) % 24] * f
        rr = R * (0.82 + 0.32 * j)
        rel = d / rr
        body = rel <= 1
        if i >= 4:
            body &= noise > (t - 0.45) * 0.9 + (rel - 0.4) * 0.4
        fire_r = heat * 0.95
        is_fire = body & (rel < fire_r)
        is_smoke = body & ~is_fire
        fi = np.clip((rel / np.maximum(fire_r, 1e-3)) * (len(hot) - 1) + (1 - heat) * 1.5, 0, len(hot) - 1).astype(int)
        for c_i, c in enumerate(hot):
            img[is_fire & (fi == c_i)] = c
        si = np.clip(((rel - fire_r) / np.maximum(1 - fire_r, 1e-3)) * 3, 0, 2).astype(int)
        for c_i, c in enumerate(smoke_c):
            img[is_smoke & (si == c_i)] = c
        # embers
        for e in range(10):
            ang = TAU * e / 10 + jit[e]
            dist = 6 + t * 24 * (0.7 + 0.5 * jit[e + 10])
            if 1 <= i <= 6 and dist < 23:
                ex, ey = int(24 + math.cos(ang) * dist), int(24 + math.sin(ang) * dist + t * t * 6)
                if 0 <= ex < 48 and 0 <= ey < 48 and img[ey, ex, 3] == 0:
                    img[ey, ex] = AMBER[2] if i < 4 else DANGER[2]
        frames.append(outline_soft(img) if i < 4 else img)
    return strip(frames)


def outline_soft(img):
    """Thin dark-red rim around the fireball for pixel-art readability."""
    o = outline_rgba(img.copy(), color=(90, 15, 30, 200))
    return o


def shield():
    frames = []
    x, y = _grid(48, 48)
    dx, dy = x - 24, y - 24
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    for i in range(6):
        ph = TAU * i / 6
        img = np.zeros((48, 48, 4), np.uint8)
        inside = r <= 21.5
        # translucent fill with a faint hex lattice
        hexv = (np.sin(dx * 0.75) + np.sin(dx * 0.375 + dy * 0.65) + np.sin(-dx * 0.375 + dy * 0.65 + ph))
        fill_a = 0.16 + 0.1 * (r / 21.5) ** 2 + 0.08 * (hexv > 2.2)
        img[inside] = np.stack([np.full(inside.sum(), CYAN[1][0]), np.full(inside.sum(), CYAN[1][1]),
                                np.full(inside.sum(), CYAN[1][2]), (fill_a[inside] * 255).astype(int)], -1)
        rim = (r <= 22.5) & (r > 20.5)
        img[rim] = (*CYAN[2][:3], 220)
        rim_in = (r <= 20.5) & (r > 19.5)
        img[rim_in] = (*CYAN[1][:3], 120)
        # rotating highlight arc on the rim + fixed top-left gloss
        hl = rim & (np.cos(th - ph) > 0.82)
        img[hl] = (255, 255, 255, 255)
        gloss = (np.hypot(dx + 8, dy + 9) < 4.2) & (np.hypot(dx + 7, dy + 8) > 2.6)
        img[gloss] = (*CYAN[3][:3], 200)
        frames.append(img)
    return strip(frames)


def teleport():
    frames = []
    for i in range(8):
        img = np.zeros((32, 32, 4), np.uint8)
        t = i / 7
        w = [1, 3, 5, 6, 5, 3, 2, 1][i]
        top = [24, 10, 2, 0, 0, 2, 8, 16][i]
        for y in range(top, 32):
            for x in range(16 - w, 16 + w):
                d = abs(x + 0.5 - 16) / max(1, w)
                if d < 0.35:
                    c = (255, 255, 255, 255)
                elif d < 0.65:
                    c = CYAN[2]
                else:
                    c = VIOLET[1] if (y + i) % 3 else VIOLET[2]
                fade = 1.0 if y > top + 3 else 0.6
                img[y, x] = (c[0], c[1], c[2], int(c[3] * fade))
        # ground flare
        if 1 <= i <= 5:
            for x in range(16 - 2 * w - 2, 16 + 2 * w + 2):
                if 0 <= x < 32:
                    img[31, x] = CYAN[2] if abs(x - 16) < w else VIOLET[1]
        # sparkles rising
        rng = np.random.default_rng(50 + i)
        for k in range(6 + 2 * (i in (2, 3, 4))):
            sx = int(16 + (rng.random() - 0.5) * 22)
            sy = int(32 - ((rng.random() * 32 + i * 5) % 32))
            if 0 <= sx < 32 and img[sy, sx, 3] == 0:
                img[sy, sx] = VIOLET[2] if k % 2 else CYAN[2]
        frames.append(img)
    return strip(frames)


def warning():
    """Red circular alert badge with a white '!' (coloured)."""
    img = np.zeros((16, 16, 4), np.uint8)
    x, y = _grid(16, 16)
    d = np.hypot(x - 8, y - 8)
    badge = d <= 6.6
    lit = (x - 8) * -0.6 + (y - 8) * -0.8
    img[badge] = DANGER[1]
    img[badge & (lit < -3.2)] = DANGER[0]
    img[badge & (lit > 3.6)] = hex_rgba('#ff8a9e')
    W_ = (255, 255, 255, 255)
    for yy in range(3, 10):
        img[yy, 7] = W_
        img[yy, 8] = W_ if yy < 8 else hex_rgba('#ffd0c0')
    img[3, 7] = img[3, 8] = W_
    for yy in (11, 12):
        img[yy, 7] = W_
        img[yy, 8] = hex_rgba('#ffd0c0')
    return outline_rgba(img, color=OUTLINE)


def build():
    out = {
        "glow.png": glow(), "particle.png": particle(), "spark.png": spark(), "star.png": star(),
        "ring.png": ring(), "smoke.png": smoke(), "dust.png": dust(), "slash.png": slash(), "hit.png": hit(),
        "explosion.png": explosion(), "shield.png": shield(), "teleport.png": teleport(), "warning.png": warning(),
    }
    for name, img in out.items():
        save_png(img, "textures/fx/" + name)
    return out
