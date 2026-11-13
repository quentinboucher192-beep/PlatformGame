"""Outils géométriques communs : projection équirectangulaire, remplissage de polygones
par balayage (règle pair-impair, sans anticrénelage), flou « sphérique » et bruit 3D.

Convention : x = (lon + 180) / 360 * W, y = (90 - lat) / 180 * H ; le centre du pixel
(c, r) est (c + 0.5, r + 0.5).
"""
import math

import numpy as np
from PIL import Image, ImageDraw


# ---------------------------------------------------------------------------------------
# Anneaux : déroulage de l'antiméridien et fermeture par le pôle
# ---------------------------------------------------------------------------------------
def unwrap_ring(ring):
    """Retourne une liste d'anneaux (N,2) lon/lat continus, prêts pour le plan.

    world-atlas 2 est « recousu » sur l'antiméridien : un anneau peut sauter de +180 à
    -180. On déroule les longitudes ; un anneau qui fait le tour d'un pôle (Antarctique)
    est fermé en passant par ce pôle. Une copie décalée de 360° est ajoutée si l'anneau
    déborde du planisphère.
    """
    a = np.asarray(ring, dtype=np.float64)
    if len(a) < 3:
        return []
    lon = a[:, 0].copy()
    d = np.diff(lon)
    d -= 360.0 * np.round(d / 360.0)
    lon = np.concatenate([[lon[0]], lon[0] + np.cumsum(d)])
    lat = a[:, 1]
    wind = lon[-1] - lon[0]
    pts = np.stack([lon, lat], axis=1)
    if abs(wind) > 180.0:  # anneau autour d'un pôle
        pole = -90.0 if lat.mean() < 0 else 90.0
        pts = np.concatenate([pts, [[lon[-1], pole], [lon[0], pole], [lon[0], lat[0]]]])
    shift = 360.0 * math.floor((pts[:, 0].min() + 180.0) / 360.0)
    pts[:, 0] -= shift
    out = [pts]
    if pts[:, 0].max() > 180.0:
        cp = pts.copy()
        cp[:, 0] -= 360.0
        out.append(cp)
    return out


def to_px(pts, W, H):
    return np.stack([(pts[:, 0] + 180.0) / 360.0 * W, (90.0 - pts[:, 1]) / 180.0 * H], axis=1)


# ---------------------------------------------------------------------------------------
# Remplissage par balayage
# ---------------------------------------------------------------------------------------
def _edges(rings_px):
    xs0, ys0, xs1, ys1, lab = [], [], [], [], []
    for label, ring in rings_px:
        p0 = ring[:-1]
        p1 = ring[1:]
        xs0.append(p0[:, 0]); ys0.append(p0[:, 1])
        xs1.append(p1[:, 0]); ys1.append(p1[:, 1])
        lab.append(np.full(len(p0), label, dtype=np.int64))
    x0 = np.concatenate(xs0); y0 = np.concatenate(ys0)
    x1 = np.concatenate(xs1); y1 = np.concatenate(ys1)
    lab = np.concatenate(lab)
    # Ordre canonique (y croissant) pour que deux pays partageant une arête calculent
    # exactement la même intersection.
    swap = y1 < y0
    x0, x1 = np.where(swap, x1, x0), np.where(swap, x0, x1)
    y0, y1 = np.where(swap, y1, y0), np.where(swap, y0, y1)
    return x0, y0, x1, y1, lab


def scan_spans(rings_px, H):
    """Intersections des arêtes avec les lignes de centres de pixels, groupées en
    segments [xa, xb) par (étiquette, ligne) selon la règle pair-impair."""
    x0, y0, x1, y1, lab = _edges(rings_px)
    r0 = np.ceil(y0 - 0.5).astype(np.int64)
    r1 = np.ceil(y1 - 0.5).astype(np.int64)
    r0 = np.clip(r0, 0, H)
    r1 = np.clip(r1, 0, H)
    n = np.maximum(r1 - r0, 0)
    keep = n > 0
    x0, y0, x1, y1, lab, r0, n = x0[keep], y0[keep], x1[keep], y1[keep], lab[keep], r0[keep], n[keep]
    idx = np.repeat(np.arange(len(n)), n)
    off = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
    rows = r0[idx] + off
    yc = rows + 0.5
    slope = (x1 - x0) / (y1 - y0)
    xi = x0[idx] + (yc - y0[idx]) * slope[idx]
    li = lab[idx]
    order = np.lexsort((xi, rows, li))
    xi, rows, li = xi[order], rows[order], li[order]
    if len(xi) % 2:
        raise RuntimeError('nombre impair d\'intersections')
    xa, xb = xi[0::2], xi[1::2]
    ra, rb = rows[0::2], rows[1::2]
    la, lb = li[0::2], li[1::2]
    if np.any(ra != rb) or np.any(la != lb):
        raise RuntimeError('appariement des intersections incohérent')
    return la, ra, xa, xb


def collect_rings(polys, W, H, label=1):
    """polys : liste de polygones (liste d'anneaux lon/lat). -> [(label, anneau_px)]"""
    out = []
    for poly in polys:
        for ring in poly:
            for r in unwrap_ring(ring):
                out.append((label, to_px(r, W, H)))
    return out


def fill_labels(rings_px, W, H, dtype=np.int32):
    """Image d'étiquettes (0 = rien). Les étiquettes ne doivent pas se chevaucher."""
    la, ra, xa, xb = scan_spans(rings_px, H)
    c0 = np.clip(np.ceil(xa - 0.5).astype(np.int64), 0, W)
    c1 = np.clip(np.ceil(xb - 0.5).astype(np.int64), 0, W)
    ok = c1 > c0
    la, ra, c0, c1 = la[ok], ra[ok], c0[ok], c1[ok]
    acc = np.zeros((H, W + 1), dtype=np.int64)
    cnt = np.zeros((H, W + 1), dtype=np.int32)
    np.add.at(acc, (ra, c0), la)
    np.add.at(acc, (ra, c1), -la)
    np.add.at(cnt, (ra, c0), 1)
    np.add.at(cnt, (ra, c1), -1)
    img = np.cumsum(acc, axis=1)[:, :W]
    c = np.cumsum(cnt, axis=1)[:, :W]
    overlaps = int((c > 1).sum())
    img[c > 1] = 0
    return img.astype(dtype), overlaps


def coverage(polys, W, H, ss=4):
    """Masque anticrénelé (0..1) par suréchantillonnage ss x ss."""
    rings = collect_rings(polys, W * ss, H * ss)
    la, ra, xa, xb = scan_spans(rings, H * ss)
    Ws = W * ss
    c0 = np.clip(np.ceil(xa - 0.5).astype(np.int64), 0, Ws)
    c1 = np.clip(np.ceil(xb - 0.5).astype(np.int64), 0, Ws)
    acc = np.zeros((H * ss, Ws + 1), dtype=np.int16)
    np.add.at(acc, (ra, c0), 1)
    np.add.at(acc, (ra, c1), -1)
    m = (np.cumsum(acc, axis=1)[:, :Ws] > 0).astype(np.float32)
    return m.reshape(H, ss, W, ss).mean(axis=(1, 3))


# ---------------------------------------------------------------------------------------
# Lignes (frontières, côtes)
# ---------------------------------------------------------------------------------------
def line_coverage(lines, W, H, width_px, ss=4, skip=None):
    """Trace des polylignes lon/lat ; width_px est exprimé en pixels finaux."""
    Ws, Hs = W * ss, H * ss
    im = Image.new('L', (Ws, Hs), 0)
    dr = ImageDraw.Draw(im)
    w = max(1, int(round(width_px * ss)))
    for line in lines:
        a = np.asarray(line, dtype=np.float64)
        if len(a) < 2:
            continue
        # découpe aux sauts d'antiméridien puis déroulage local
        lon = a[:, 0].copy()
        d = np.diff(lon)
        d -= 360.0 * np.round(d / 360.0)
        lon = np.concatenate([[lon[0]], lon[0] + np.cumsum(d)])
        pts = np.stack([lon, a[:, 1]], axis=1)
        if skip is not None:
            bad = skip(a)
        else:
            bad = np.zeros(len(a) - 1, dtype=bool)
        for shift in (-360.0, 0.0, 360.0):
            p = pts.copy()
            p[:, 0] += shift
            if p[:, 0].max() < -180 or p[:, 0].min() > 180:
                continue
            px = to_px(p, Ws, Hs)
            # segments consécutifs valides
            start = 0
            for i in range(len(bad) + 1):
                if i == len(bad) or bad[i]:
                    if i - start >= 1:
                        seg = [tuple(v) for v in px[start:i + 1]]
                        dr.line(seg, fill=255, width=w, joint='curve')
                    start = i + 1
    arr = np.asarray(im, dtype=np.float32) / 255.0
    return arr.reshape(H, ss, W, ss).mean(axis=(1, 3))


# ---------------------------------------------------------------------------------------
# Flou gaussien respectant la sphère (horizontal élargi de 1/cos(lat), périodique en x)
# ---------------------------------------------------------------------------------------
def lat_of_rows(H):
    return 90.0 - (np.arange(H) + 0.5) * 180.0 / H


def sphere_blur(img, sigma_deg, max_stretch=8.0):
    img = np.asarray(img, dtype=np.float64)
    H, W = img.shape
    lat = lat_of_rows(H)
    stretch = np.minimum(1.0 / np.maximum(np.cos(np.radians(lat)), 1e-6), max_stretch)
    sx = sigma_deg * W / 360.0 * stretch  # par ligne, en pixels
    k = np.arange(W // 2 + 1)
    G = np.exp(-2.0 * (math.pi ** 2) * (sx[:, None] ** 2) * (k[None, :] / W) ** 2)
    out = np.fft.irfft(np.fft.rfft(img, axis=1) * G, n=W, axis=1)
    sy = sigma_deg * H / 180.0
    pad = int(math.ceil(4 * sy)) + 1
    padded = np.pad(out, ((pad, pad), (0, 0)), mode='reflect')
    n = padded.shape[0]
    ky = np.arange(n // 2 + 1)
    Gy = np.exp(-2.0 * (math.pi ** 2) * sy ** 2 * (ky / n) ** 2)
    out = np.fft.irfft(np.fft.rfft(padded, axis=0) * Gy[:, None], n=n, axis=0)[pad:pad + H]
    return out


# ---------------------------------------------------------------------------------------
# Bruit de valeur 3D échantillonné sur la sphère (sans couture ni pincement aux pôles)
# ---------------------------------------------------------------------------------------
def sphere_xyz(W, H):
    lat = np.radians(lat_of_rows(H))[:, None]
    lon = np.radians(-180.0 + (np.arange(W) + 0.5) * 360.0 / W)[None, :]
    x = np.cos(lat) * np.cos(lon)
    y = np.cos(lat) * np.sin(lon)
    z = np.sin(lat) * np.ones_like(lon)
    return x, y, z


def _hash3(ix, iy, iz, seed):
    h = (ix.astype(np.uint32) * np.uint32(0x8DA6B343)) ^ (iy.astype(np.uint32) * np.uint32(0xD8163841)) \
        ^ (iz.astype(np.uint32) * np.uint32(0xCB1AB31F)) ^ np.uint32(seed * 0x9E3779B1 & 0xFFFFFFFF)
    h ^= h >> np.uint32(15)
    h *= np.uint32(0x2C1B3C6D)
    h ^= h >> np.uint32(12)
    h *= np.uint32(0x297A2D39)
    h ^= h >> np.uint32(15)
    return (h & np.uint32(0xFFFFFF)).astype(np.float32) / float(0xFFFFFF)


def value_noise3(x, y, z, freq, seed):
    px, py, pz = x * freq + 1000.0, y * freq + 1000.0, z * freq + 1000.0
    ix, iy, iz = np.floor(px), np.floor(py), np.floor(pz)
    fx, fy, fz = px - ix, py - iy, pz - iz
    ux, uy, uz = (fx * fx * (3 - 2 * fx)), (fy * fy * (3 - 2 * fy)), (fz * fz * (3 - 2 * fz))
    ix, iy, iz = ix.astype(np.int64), iy.astype(np.int64), iz.astype(np.int64)
    acc = 0.0
    for dx in (0, 1):
        wx = ux if dx else 1 - ux
        for dy in (0, 1):
            wy = uy if dy else 1 - uy
            for dz in (0, 1):
                wz = uz if dz else 1 - uz
                acc = acc + wx * wy * wz * _hash3(ix + dx, iy + dy, iz + dz, seed)
    return acc * 2.0 - 1.0


def fbm(xyz, base_freq, octaves, seed, gain=0.5, lacunarity=2.0, ridged=False):
    x, y, z = xyz
    total = np.zeros(np.broadcast(x, y, z).shape, dtype=np.float64)
    amp, freq, norm = 1.0, base_freq, 0.0
    for o in range(octaves):
        n = value_noise3(x, y, z, freq, seed + 101 * o)
        if ridged:
            n = 1.0 - np.abs(n)
            n = n * n * 2.0 - 1.0
        total += amp * n
        norm += amp
        amp *= gain
        freq *= lacunarity
    return total / norm
