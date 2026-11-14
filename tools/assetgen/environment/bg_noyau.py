"""Parallax backgrounds of the Stellar Core (Noyau stellaire, version 2.7), 640x360 each:

  sky.png    the star itself, huge, its corona filling the top of the sky, on deep violet
  far.png    Glacior's far ice spires, recoloured in cold starlight (the echoes of the planets)
  mid.png    Pyralis' forges, recoloured in gold
  near.png   Sylvanox's ruins, recoloured in violet with golden rims
  abyss.png  plasma glow rising from the star's heart
  fg_motes.png light motes drifting in front of the camera

The far / mid / near / abyss / fg layers are the planets' own layers (already horizontally
seamless) with their luminance mapped onto a new gradient: the three worlds seen again in
the light of the star.
"""
import numpy as np

from helpers import Noise3, bayer, dither_alpha, hex_arr, load_png, new, quantize_dither, save_png

W, H = 640, 360


def remap(img, ramp, gain=1.0):
    """Luminance of `img` mapped onto the colour ramp (dark -> light); alpha kept."""
    rgb = img[..., :3].astype(np.float32)
    lum = (0.3 * rgb[..., 0] + 0.59 * rgb[..., 1] + 0.11 * rgb[..., 2]) / 255.0
    lum = np.clip(lum * gain, 0, 1)
    cols = hex_arr(ramp)
    xs = np.linspace(0, 1, len(ramp))
    out = img.copy()
    for i in range(3):
        out[..., i] = np.clip(np.interp(lum, xs, cols[:, i]), 0, 255).astype(np.uint8)
    return out


def sky():
    yy, xx = np.mgrid[0:H, 0:W]
    # deep violet towards the bottom, warmer near the star
    t = yy / (H - 1)
    top, bottom = hex_arr(["#2a1440"])[0], hex_arr(["#0a0716"])[0]
    rgb = np.zeros((H, W, 3), np.float32)
    for i in range(3):
        rgb[..., i] = top[i] * (1 - t) + bottom[i] * t
    # the star: centred high, its disc partly above the frame
    cx, cy, rs = W * 0.8, H * 0.5, 80.0  # right of the sky, below the HUD (timer, progress bar)
    dx, dy = xx + 0.5 - cx, yy + 0.5 - cy
    d = np.hypot(dx, dy)
    ang = np.arctan2(dy, dx)
    nz = np.sqrt(np.clip(1 - (d / rs) ** 2, 0, 1))
    gran = Noise3(2701).fbm(dx / 14.0, dy / 14.0, nz * 3.0, 4)
    disc = d <= rs
    td = np.clip(0.35 + 0.65 * nz ** 0.4 + (gran - 0.5) * 0.3, 0, 1)
    c = hex_arr(["#c8741f", "#ffb347", "#ffd98a", "#fff6d0"])
    for i in range(3):
        rgb[..., i] = np.where(disc, np.interp(td, [0, 0.45, 0.75, 1], c[:, i]), rgb[..., i])
    streak = np.zeros_like(d)
    for f, ph, amp in [(7, 0.4, 0.35), (13, 2.1, 0.25), (21, 4.7, 0.15)]:
        streak += amp * (0.5 + 0.5 * np.sin(ang * f + ph))
    corona = np.clip(1 - (d - rs) / (40 + 80 * streak), 0, 1) ** 1.6 * (d > rs)
    glow = np.clip(1 - (d - rs) / 220.0, 0, 1) ** 2.2 * (d > rs)
    gold = hex_arr(["#ffc86a"])[0]
    rose = hex_arr(["#d36ad8"])[0]
    for i in range(3):
        rgb[..., i] = rgb[..., i] * (1 - 0.85 * corona) + gold[i] * 0.85 * corona
        rgb[..., i] = rgb[..., i] * (1 - 0.35 * glow) + rose[i] * 0.35 * glow
    # stars (fewer near the star's light)
    r = np.random.default_rng(2702)
    for _ in range(260):
        x, y = int(r.integers(0, W)), int(r.integers(0, H))
        if d[y, x] < rs + 90:
            continue
        b = r.uniform(0.5, 1.0)
        rgb[y, x] = rgb[y, x] * (1 - b) + np.array([255, 245, 225]) * b
    pal = ["#0a0716", "#160d2a", "#2a1440", "#4a1f5c", "#7a2f6e", "#a34a1a", "#c8741f", "#e8902a",
           "#ffb347", "#ffc86a", "#ffd98a", "#ffe8b0", "#fff6d0", "#ffffff", "#d36ad8"]
    img = quantize_dither(rgb, pal, bayer(H, W, 4), 1.0)
    img[..., 3] = 255
    return img


def far():
    return remap(load_png("backgrounds/glacior/far.png"), ["#120c24", "#2b2350", "#5a4c8e", "#a99ce0", "#e8e2ff"], 1.1)


def mid():
    return remap(load_png("backgrounds/pyralis/mid.png"), ["#140a1c", "#3a1a2e", "#7a3e2a", "#c88a3a", "#ffd98a"], 1.05)


def near():
    return remap(load_png("backgrounds/sylvanox/near.png"), ["#0c0818", "#22143a", "#4a2a6a", "#b07a3a", "#ffe0a0"], 1.0)


def abyss():
    return remap(load_png("backgrounds/pyralis/abyss.png"), ["#3a1a40", "#a35a8a", "#ffb347", "#ffe8b0", "#ffffff"], 1.0)


def fg_motes():
    img = new(W, H)
    r = np.random.default_rng(2703)
    alpha = np.zeros((H, W), np.float32)
    colour = np.zeros((H, W, 3), np.float32)
    yy, xx = np.mgrid[0:H, 0:W]
    for _ in range(70):
        cx, cy = r.uniform(0, W), r.uniform(0, H * 0.75)
        rad = r.uniform(1.0, 3.2)
        dxw = np.minimum(np.abs(xx - cx), W - np.abs(xx - cx))  # seamless horizontally
        dist = np.hypot(dxw, yy - cy)
        a = np.clip(1 - dist / rad, 0, 1) ** 1.5 * r.uniform(0.5, 1.0)
        tint = hex_arr([r.choice(["#fff1c9", "#ffd98a", "#e8d6ff"])])[0]
        mask = a > alpha
        alpha = np.where(mask, a, alpha)
        colour[mask] = tint
    img[..., :3] = colour.astype(np.uint8)
    img[..., 3] = dither_alpha(alpha, levels=(0, 60, 120, 190, 255))
    return img


def banner():
    """480x270 vista for the planet panel: the star's layers composed with offsets."""
    BW, BH = 480, 270
    img = new(BW, BH)
    img[..., 3] = 255
    for name, ox, oy in [("sky", 80, 0), ("far", 120, 40), ("mid", 260, -30), ("abyss", 0, 70), ("near", 340, 40)]:
        lay = load_png(f"backgrounds/noyau/{name}.png")
        tiled = np.concatenate([lay, lay], axis=1)[:, ox:ox + BW]
        crop = new(BW, BH)
        src0, dst0 = max(0, oy), max(0, -oy)
        n = min(BH - dst0, tiled.shape[0] - src0)
        crop[dst0:dst0 + n] = tiled[src0:src0 + n]
        if dst0 + n < BH:
            crop[dst0 + n:] = crop[dst0 + n - 1]
        a = crop[..., 3:4].astype(np.float32) / 255.0
        img[..., :3] = (img[..., :3] * (1 - a) + crop[..., :3] * a).astype(np.uint8)
    return img


LAYERS = [("sky", sky), ("far", far), ("mid", mid), ("near", near), ("abyss", abyss), ("fg_motes", fg_motes)]


def generate():
    out = []
    for name, fn in LAYERS:
        img = fn()
        assert img.shape == (H, W, 4), name
        out.append(save_png(img, f"backgrounds/noyau/{name}.png"))
    out.append(save_png(banner(), "ui/planets/noyau_banner.png"))
    return out


if __name__ == "__main__":
    for p in generate():
        print(p)
