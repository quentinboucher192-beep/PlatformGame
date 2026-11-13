"""earth_night.png : lumières des villes (2048x1024, RGBA).

Chaque localité GeoNames (>= 1000 hab.) dépose une quantité de lumière ~ population^0.72,
puis on additionne plusieurs flous gaussiens « sphériques » de rayons croissants : plus
une ville est peuplée, plus son halo dépasse le seuil de visibilité loin du centre.

Convention de sortie : alpha = intensité lumineuse ; RGB = couleur de la lumière NON
prémultipliée (0 là où alpha = 0). En SDL : SDL_BLENDMODE_ADD donne couleur * alpha.
"""
import numpy as np
from PIL import Image

import geo_raster as gr

W, H = 2048, 1024
LAYERS = [  # (sigma en degrés, poids)
    (0.05, 1.00),
    (0.16, 0.55),
    (0.45, 0.24),
    (1.20, 0.10),
]


def _splat(lon, lat, w):
    """Dépôt bilinéaire (précision sous-pixel), périodique en longitude."""
    x = (lon + 180.0) / 360.0 * W - 0.5
    y = (90.0 - lat) / 180.0 * H - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx, fy = x - x0, y - y0
    img = np.zeros((H, W))
    for dx, wx in ((0, 1 - fx), (1, fx)):
        for dy, wy in ((0, 1 - fy), (1, fy)):
            yy = np.clip(y0 + dy, 0, H - 1)
            np.add.at(img, (yy, (x0 + dx) % W), w * wx * wy)
    return img


def build(cities_path, land, out_assets, log):
    raw = np.fromfile(cities_path, dtype='<f4').reshape(-1, 3).astype(np.float64)
    lon, lat, pop = raw[:, 0], raw[:, 1], raw[:, 2]
    pop = np.where(pop > 0, pop, 800.0)   # localités sans population renseignée
    light = pop ** 0.72
    base = _splat(lon, lat, light)
    F = np.zeros((H, W))
    for sigma, weight in LAYERS:
        F += weight * gr.sphere_blur(base, sigma)
    F = np.maximum(F, 0)
    # normalisation sur un centile élevé : seuls les cœurs des grandes métropoles saturent
    ref = np.percentile(F[F > 1e-3], 99.5)
    I = 1.0 - np.exp(-1.25 * F / ref)
    I = I ** 0.8
    # les halos s'estompent au-dessus de la mer
    near_land = np.clip(gr.sphere_blur(land, 0.12) * 1.6, 0, 1)
    I = I * (0.45 + 0.55 * near_land)
    I[I < 2.0 / 255.0] = 0.0
    log(f'  {len(pop)} localités, {int((I > 0).sum())} pixels éclairés')

    t = I[..., None]
    deep = np.array([255, 128, 40], dtype=np.float64)
    amber = np.array([255, 179, 71], dtype=np.float64)     # #FFB347
    pale = np.array([255, 238, 190], dtype=np.float64)
    rgb = deep + (amber - deep) * np.clip((t - 0.05) / 0.35, 0, 1)
    rgb = rgb + (pale - rgb) * np.clip((t - 0.55) / 0.4, 0, 1)
    a = np.clip(I * 255.0 + 0.5, 0, 255).astype(np.uint8)
    rgb = np.where(a[..., None] > 0, rgb, 0.0)
    out = np.dstack([np.clip(rgb + 0.5, 0, 255).astype(np.uint8), a])
    Image.fromarray(out, 'RGBA').save(out_assets / 'earth_night.png', optimize=True)
