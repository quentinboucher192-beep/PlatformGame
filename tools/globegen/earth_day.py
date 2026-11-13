"""earth_day.png : Terre de jour stylisée (2048x1024, RGB, équirectangulaire)."""
import numpy as np
from PIL import Image, ImageDraw

import geo_raster as gr

W, H = 2048, 1024
SEED = 20261113


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    t = np.asarray(t)[..., None] if np.ndim(t) == 2 else t
    return a + (b - a) * t


def col(*rgb):
    return np.array(rgb, dtype=np.float64) / 255.0


# Déserts (lon, lat, sigma_lon, sigma_lat, intensité) : grands foyers arides.
DESERTS = [
    (2, 22.5, 22, 7, 1.0), (27, 23, 8, 6, 1.0),                               # Sahara
    (46, 23, 7, 5, 1.0), (40, 31, 5, 2.5, 0.75),                              # Arabie, Syrie-Irak
    (58, 30, 8, 3.5, 0.75), (71, 27, 3, 2.5, 0.7),                            # Iran-Pakistan, Thar
    (61, 42, 9, 4, 0.42), (83, 39, 7, 2.4, 0.7), (103, 42, 11, 3.5, 0.48),    # Asie centrale, Taklamakan, Gobi
    (132, -25, 11, 6, 0.95), (122, -22, 4, 4, 0.6),                           # Australie
    (19, -24, 5, 4, 0.75), (15, -23, 2, 5, 0.8),                              # Kalahari, Namib
    (45, 7, 5, 4, 0.45),                                                      # Corne de l'Afrique
    (-70, -24, 2, 6, 0.85), (-68, -45, 4, 5, 0.5),                            # Atacama, Patagonie
    (-112, 34, 6, 6, 0.65), (-105, 28, 4, 4, 0.5),                            # Sud-Ouest US, Chihuahua
]

# Reliefs principaux : polylignes (lon, lat), largeur (deg), intensité, neige (0..1).
MOUNTAINS = [
    ([(73, 35), (77, 33), (81, 30), (85, 28), (89, 27.8), (93, 28.5), (96, 28.5)], 1.6, 1.0, 1.0),  # Himalaya
    ([(80, 34), (86, 33.5), (92, 33), (98, 32)], 4.0, 0.7, 0.55),                                    # Tibet
    ([(68, 35.5), (72, 36.5), (75, 36), (77, 35.5)], 1.2, 0.9, 0.9),                                 # Hindou Kouch
    ([(70, 41), (76, 42), (80, 42.5), (86, 43), (90, 43)], 1.0, 0.7, 0.6),                           # Tian Shan
    ([(85, 50), (89, 49), (93, 48), (97, 46)], 1.0, 0.55, 0.3),                                      # Altaï
    ([(-76, 8), (-75, 4), (-77, 0), (-79, -3), (-77, -8), (-74, -13), (-70, -16), (-68, -19),
      (-68, -23), (-69, -28), (-70, -33), (-71, -38), (-72, -43), (-73, -48), (-73, -52)], 1.2, 0.95, 0.8),  # Andes
    ([(-152, 62.5), (-145, 61.5), (-140, 60.5)], 1.0, 0.8, 0.9),                                     # Alaska
    ([(-132, 58), (-126, 55), (-121, 52), (-116, 50), (-114, 47), (-110, 44), (-107, 40),
      (-106, 37)], 1.6, 0.75, 0.45),                                                                 # Rocheuses
    ([(-121, 48), (-122, 44), (-120, 38.5), (-118, 36)], 0.8, 0.6, 0.4),                             # Cascades, Sierra Nevada
    ([(-106, 28), (-104, 24), (-101, 20), (-97, 17.5)], 1.0, 0.55, 0.0),                             # Sierra Madre
    ([(6, 44.5), (7, 46), (10, 46.5), (13, 47), (15, 47.3)], 0.8, 0.8, 0.85),                       # Alpes
    ([(-1.5, 43), (2.5, 42.6)], 0.5, 0.55, 0.4),                                                     # Pyrénées
    ([(18, 49.3), (22, 49), (25, 47.5), (26, 45.5), (23, 45.4)], 0.7, 0.45, 0.0),                   # Carpates
    ([(40, 43.3), (44, 42.7), (48, 41.5)], 0.7, 0.8, 0.8),                                           # Caucase
    ([(45, 35.5), (48, 33), (52, 30), (56, 28.5)], 1.0, 0.6, 0.2),                                   # Zagros
    ([(-8, 31), (-4, 32.5), (1, 34), (6, 35), (9, 35.5)], 0.8, 0.55, 0.15),                         # Atlas
    ([(37, 12), (38.5, 9.5), (39, 7)], 2.2, 0.55, 0.0),                                              # Hauts plateaux éthiopiens
    ([(6, 59.5), (8, 61.5), (12, 63.5), (15, 66), (18, 68.5), (20, 69.5)], 0.9, 0.5, 0.6),          # Alpes scandinaves
    ([(59, 51), (59, 55), (59.5, 60), (60, 64), (65, 68)], 0.7, 0.4, 0.3),                           # Oural
    ([(146, -38), (149, -35), (151, -31), (150, -26), (147, -21), (145, -17)], 0.8, 0.35, 0.0),     # Cordillère australienne
    ([(-84, 34), (-81, 36.5), (-78, 39), (-75, 41.5), (-72, 44), (-69, 46)], 0.9, 0.35, 0.0),       # Appalaches
    ([(167, -46), (170, -43.5), (173, -41.5)], 0.6, 0.7, 0.8),                                       # Alpes du Sud
    ([(-162, 68), (-152, 68.3), (-145, 68.5)], 0.8, 0.5, 0.6),                                       # chaîne Brooks
    ([(127, 67), (135, 65), (145, 63.5), (155, 62.5), (165, 63)], 1.4, 0.5, 0.5),                   # Verkhoïansk, Tcherski
    ([(158, 53), (160, 56), (162, 58)], 0.6, 0.6, 0.6),                                              # Kamtchatka
    ([(136, 35.5), (138, 36.5), (140, 39), (141, 41)], 0.5, 0.45, 0.4),                             # Alpes japonaises
    ([(29, -30), (30, -28)], 0.6, 0.4, 0.0),                                                         # Drakensberg
    ([(98, 26), (99, 22), (98.5, 18)], 1.2, 0.4, 0.0),                                               # Indochine nord
    ([(103, 31), (100, 28), (100, 33), (104, 34)], 2.0, 0.45, 0.2),                                  # Sichuan-Yunnan
]


def _desert_field(lat, lon):
    A = np.zeros_like(lat)
    for (lo, la, sx, sy, k) in DESERTS:
        dl = (lon - lo + 180.0) % 360.0 - 180.0
        r2 = (dl / sx) ** 2 + ((lat - la) / sy) ** 2
        A = np.maximum(A, k * np.exp(-0.5 * r2 ** 1.3))   # profil « plateau »
    return A


def _mountain_fields():
    """Deux champs : relief (intensité) et propension à la neige."""
    ss = 2
    relief = np.zeros((H, W))
    snow = np.zeros((H, W))
    for pts, width, strength, snowk in MOUNTAINS:
        im = Image.new('L', (W * ss, H * ss), 0)
        dr = ImageDraw.Draw(im)
        px = [((lo + 180) / 360 * W * ss, (90 - la) / 180 * H * ss) for lo, la in pts]
        wpx = max(2, int(width / 360 * W * ss * 0.8))
        dr.line(px, fill=255, width=wpx, joint='curve')
        for x, y in px:
            dr.ellipse([x - wpx / 2, y - wpx / 2, x + wpx / 2, y + wpx / 2], fill=255)
        m = np.asarray(im, dtype=np.float64).reshape(H, ss, W, ss).mean(axis=(1, 3)) / 255.0
        m = gr.sphere_blur(m, width * 0.5)
        m = np.clip(m / max(m.max(), 1e-6), 0, 1)
        relief = np.maximum(relief, strength * m)
        snow = np.maximum(snow, snowk * m)
    return relief, snow


def _coast_skip(a):
    lat = a[:, 1]
    lon = a[:, 0]
    pole = (lat[:-1] < -89.5) & (lat[1:] < -89.5)
    merid = (np.abs(lon[:-1]) > 179.99) & (np.abs(lon[1:]) > 179.99)
    return pole | merid


def build(geo, out_assets, log):
    lat = gr.lat_of_rows(H)[:, None] * np.ones((1, W))
    lon = (-180.0 + (np.arange(W) + 0.5) * 360.0 / W)[None, :] * np.ones((H, 1))
    alat = np.abs(lat)
    xyz = gr.sphere_xyz(W, H)

    log('  terres...')
    L = gr.coverage(geo['land'], W, H, ss=4)
    polys = {c['code']: c['polys'] for c in geo['countries']}
    AQ = gr.coverage(polys['AQ'], W, H, ss=2)
    GL = gr.coverage(polys['GL'], W, H, ss=2)

    log('  bruits...')
    n_big = gr.fbm(xyz, 2.2, 4, SEED + 1)
    n_mid = gr.fbm(xyz, 9.0, 4, SEED + 2)
    n_fine = gr.fbm(xyz, 48.0, 3, SEED + 3)
    n_ridge = (gr.fbm(xyz, 34.0, 5, SEED + 4, ridged=True) + 1) * 0.5
    n_ocean = gr.fbm(xyz, 4.0, 4, SEED + 5)
    n_ice = gr.fbm(xyz, 30.0, 4, SEED + 6)
    n_wx = gr.fbm(xyz, 3.5, 4, SEED + 7)
    n_wy = gr.fbm(xyz, 3.5, 4, SEED + 8)
    n_wx2 = gr.fbm(xyz, 11.0, 3, SEED + 10)
    n_wy2 = gr.fbm(xyz, 11.0, 3, SEED + 11)
    n_hum = gr.fbm(xyz, 6.0, 5, SEED + 9)

    # ------------------------------------------------------------------ océans
    log('  océans...')
    b1 = gr.sphere_blur(L, 0.18)
    b2 = gr.sphere_blur(L, 0.8)
    b3 = gr.sphere_blur(L, 2.6)
    shelf = np.clip(b1 * 2.2, 0, 1)
    near = np.clip(0.9 * b2 + 1.1 * b3, 0, 1)
    deep = col(4, 12, 44)
    mid = col(8, 32, 88)
    shallow = col(20, 84, 142)
    coastal = col(38, 146, 178)
    ocean = lerp(deep, mid, smoothstep(0.0, 0.55, near) * (0.85 + 0.15 * n_ocean))
    ocean = lerp(ocean, shallow, smoothstep(0.08, 0.9, shelf) * 0.85)
    ocean = lerp(ocean, coastal, smoothstep(0.45, 1.0, shelf) * 0.45)
    ocean = ocean * (1.0 + 0.07 * n_ocean[..., None] + 0.03 * n_mid[..., None])
    ocean = lerp(ocean, col(22, 16, 72), smoothstep(48, 82, alat) * 0.35)   # reflet violet aux hautes latitudes

    # -------------------------------------------------------------------- terres
    log('  biomes...')
    lw = alat + 3.5 * n_big + 1.2 * n_mid
    stops = [0, 9, 15, 22, 30, 38, 46, 54, 61, 67, 73, 80, 90]
    cols = np.array([
        (22, 76, 50), (28, 84, 54), (92, 108, 60), (128, 122, 74), (104, 108, 64), (66, 98, 58),
        (46, 84, 58), (30, 66, 54), (40, 64, 52), (66, 72, 56), (98, 100, 88), (196, 206, 218),
        (230, 237, 246)], dtype=np.float64) / 255.0
    land = np.stack([np.interp(lw, stops, cols[:, k]) for k in range(3)], axis=-1)
    # marbrures d'humidité : forêts plus sombres / zones plus sèches (olive)
    hum = np.clip(n_hum * 1.6, -1, 1)[..., None]
    land = land * (1.0 - 0.10 * hum) + (col(118, 116, 70) - land) * np.clip(-hum, 0, 1) * 0.22

    # aridité : foyers désertiques déformés par du bruit pour casser les ellipses
    A = _desert_field(lat + 4.0 * n_wy + 1.6 * n_wy2, lon + 7.0 * n_wx + 2.5 * n_wx2)
    A = A + 0.18 * np.exp(-0.5 * ((alat - 25) / 7.0) ** 2)
    A = np.clip(A * (1 + 0.55 * n_mid + 0.25 * n_hum) + 0.08 * n_big, 0, 1)
    steppe = col(150, 140, 88)
    sand = col(214, 178, 120)
    ochre = col(182, 124, 72)
    desert = lerp(sand, ochre, np.clip(0.5 + 1.1 * n_mid + 0.5 * n_fine, 0, 1))
    land = lerp(land, steppe, smoothstep(0.1, 0.45, A) * 0.8)
    land = lerp(land, desert, smoothstep(0.3, 0.92, A))

    log('  reliefs...')
    relief, snowk = _mountain_fields()
    relief = relief * (0.8 + 0.2 * n_mid)

    def slope_light(h):
        gy, gx = np.gradient(h)
        gx = gx / np.maximum(np.cos(np.radians(lat)), 0.15)
        d = (gx + gy) * 0.7071          # éclairage venant du nord-ouest
        ref = np.percentile(np.abs(d[L > 0.5]), 95) + 1e-9
        return np.clip(d / ref, -1.5, 1.5)

    s_global = slope_light(n_mid + 0.5 * n_fine)
    s_mount = slope_light(n_ridge + 0.15 * n_fine)   # gradient du bruit seul : pas de trait au bord du masque
    shade = (1.0 + 0.14 * s_global) * (1.0 + 0.7 * s_mount * np.clip(relief * 1.6, 0, 1))
    rock = col(100, 88, 78)
    land = lerp(land, rock, np.clip(relief * (0.35 + 0.6 * n_ridge), 0, 0.75))
    land = land * (1.0 - 0.12 * np.clip(relief, 0, 1))[..., None]
    snow_amt = smoothstep(0.62, 0.8, n_ridge + 0.05 * n_fine) * smoothstep(0.3, 0.75, snowk * relief)
    land = lerp(land, col(232, 238, 248), snow_amt * 0.85)
    land = land * (1.0 + 0.06 * n_fine[..., None])

    # ------------------------------------------------------------------- glaces
    log('  glaces...')
    ice = lerp(col(234, 241, 250), col(186, 204, 232), np.clip(0.45 - 0.8 * n_mid - 0.25 * n_ice, 0, 1))
    gl_in = smoothstep(0.55, 0.9, gr.sphere_blur(GL, 0.45))
    arctic = smoothstep(70, 81, lat + 4 * n_mid) * L
    ice_amt = np.clip(np.maximum.reduce([AQ, GL * gl_in, arctic * 0.85]), 0, 1)
    land = lerp(land, ice, ice_amt)
    shade = 1.0 + (shade - 1.0) * (1.0 - 0.6 * ice_amt)
    land = land * shade[..., None]

    img = lerp(ocean, land, L)

    # Banquise : plaques nettes dans l'océan Arctique, frange discrète le long de l'Antarctique
    pack = smoothstep(75, 89, lat + 4 * n_mid)
    pack = smoothstep(0.46, 0.52, pack + 0.28 * n_ice + 0.12 * n_fine) * (0.7 + 0.2 * pack)
    fringe = smoothstep(-64, -70, lat) * smoothstep(0.1, 0.5, b2) * smoothstep(0.5, 0.62, 0.5 + 0.3 * n_ice) * 0.5
    seaice = np.clip(np.maximum(pack, fringe), 0, 1) * (1 - L)
    img = lerp(img, lerp(col(196, 220, 242), col(150, 182, 220), np.clip(0.5 - n_ice, 0, 1)), seaice)

    # --------------------------------------------------- côtes et frontières
    log('  côtes et frontières...')
    C = gr.line_coverage(geo['coast'], W, H, width_px=0.75, ss=4, skip=_coast_skip)
    Bd = gr.line_coverage(geo['borders'], W, H, width_px=0.6, ss=4)
    glow = gr.sphere_blur(C, 0.22)
    img = img + col(40, 210, 240) * (np.clip(glow * 2.2, 0, 1) * 0.22 * (1 - L))[..., None]
    img = lerp(img, col(150, 236, 255), np.clip(C, 0, 1) * 0.6)
    img = lerp(img, col(232, 240, 255), np.clip(Bd, 0, 1) * 0.38)

    # étalonnage léger : contraste doux, saturation un peu relevée
    img = np.clip(img, 0, 1)
    lum = img @ np.array([0.2126, 0.7152, 0.0722])
    img = lerp(lum[..., None] * np.ones(3), img, 1.12)
    img = np.clip(img, 0, 1) ** 0.96

    out = (np.clip(img, 0, 1) * 255.0 + 0.5).astype(np.uint8)
    Image.fromarray(out, 'RGB').save(out_assets / 'earth_day.png', optimize=True)
    return L
