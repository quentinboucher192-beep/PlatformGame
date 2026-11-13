"""countries.json, country_codes.json et carte d'index earth_countries.png."""
import json
import math

import numpy as np
from PIL import Image

import geo_raster as gr

# Noms d'affichage courts pour l'interface (le reste vient de i18n-iso-countries).
NAME_FR = {
    'AE': 'Émirats arabes unis', 'BN': 'Brunei', 'BQ': 'Pays-Bas caribéens', 'CD': 'RD Congo',
    'CF': 'Centrafrique', 'CG': 'Congo', 'CI': "Côte d'Ivoire", 'CZ': 'Tchéquie',
    'DO': 'République dominicaine', 'GS': 'Géorgie du Sud-et-Sandwich du Sud',
    'IO': "Territoire britannique de l'océan Indien", 'MF': 'Saint-Martin', 'SA': 'Arabie saoudite',
    'SX': 'Sint Maarten', 'SZ': 'Eswatini', 'TZ': 'Tanzanie', 'US': 'États-Unis', 'VA': 'Vatican',
    'VG': 'Îles Vierges britanniques', 'VI': 'Îles Vierges des États-Unis',
}
NAME_EN = {
    'BN': 'Brunei', 'BQ': 'Caribbean Netherlands', 'CD': 'DR Congo', 'CG': 'Congo', 'CI': "Côte d'Ivoire",
    'CN': 'China', 'CZ': 'Czechia', 'FK': 'Falkland Islands', 'FM': 'Micronesia', 'GM': 'Gambia',
    'IR': 'Iran', 'LA': 'Laos', 'MD': 'Moldova', 'MF': 'Saint Martin', 'MK': 'North Macedonia',
    'PN': 'Pitcairn Islands', 'PS': 'Palestine', 'RE': 'Réunion', 'RU': 'Russia', 'SX': 'Sint Maarten',
    'SY': 'Syria', 'TW': 'Taiwan', 'TZ': 'Tanzania', 'US': 'United States', 'VA': 'Vatican City',
    'VG': 'British Virgin Islands', 'VI': 'U.S. Virgin Islands',
}

IDX_W, IDX_H = 1024, 512


# ---------------------------------------------------------------------------------------
# Point représentatif : barycentre surfacique du plus grand polygone, sinon pôle
# d'inaccessibilité (recherche sur grille).
# ---------------------------------------------------------------------------------------
def _sph_area(r):
    lam = np.radians(r[:, 0])
    phi = np.radians(r[:, 1])
    return abs(np.sum((lam[1:] - lam[:-1]) * (2 + np.sin(phi[:-1]) + np.sin(phi[1:])))) / 2.0


def _primary(ring):
    return gr.unwrap_ring(ring)[0]


def _poly_area(poly):
    rings = [_primary(r) for r in poly]
    return _sph_area(rings[0]) - sum(_sph_area(h) for h in rings[1:])


def _sinu(pts, lam0):
    return np.stack([(pts[:, 0] - lam0) * np.cos(np.radians(pts[:, 1])), pts[:, 1]], axis=1)


def _centroid(xy):
    x, y = xy[:, 0], xy[:, 1]
    c = x[:-1] * y[1:] - x[1:] * y[:-1]
    a = c.sum() / 2.0
    if abs(a) < 1e-12:
        return xy[:, 0].mean(), xy[:, 1].mean()
    return ((x[:-1] + x[1:]) * c).sum() / (6 * a), ((y[:-1] + y[1:]) * c).sum() / (6 * a)


def _inside(px, py, ring):
    """Points (vecteurs) dans l'anneau, règle pair-impair."""
    x0, y0 = ring[:-1, 0], ring[:-1, 1]
    x1, y1 = ring[1:, 0], ring[1:, 1]
    px = np.asarray(px)[:, None]
    py = np.asarray(py)[:, None]
    cond = (y0 > py) != (y1 > py)
    xint = x0 + (py - y0) * (x1 - x0) / np.where(y1 == y0, 1e-30, y1 - y0)
    return (cond & (px < xint)).sum(axis=1) % 2 == 1


def _dist_to_rings(px, py, rings):
    best = np.full(len(px), np.inf)
    for ring in rings:
        ax, ay = ring[:-1, 0][None, :], ring[:-1, 1][None, :]
        bx, by = ring[1:, 0][None, :], ring[1:, 1][None, :]
        qx, qy = px[:, None], py[:, None]
        dx, dy = bx - ax, by - ay
        l2 = np.where(dx * dx + dy * dy == 0, 1e-30, dx * dx + dy * dy)
        t = np.clip(((qx - ax) * dx + (qy - ay) * dy) / l2, 0, 1)
        d = np.hypot(qx - (ax + t * dx), qy - (ay + t * dy)).min(axis=1)
        best = np.minimum(best, d)
    return best


def _inside_poly(px, py, rings):
    ins = _inside(px, py, rings[0])
    for h in rings[1:]:
        ins &= ~_inside(px, py, h)
    return ins


def representative_point(polys):
    poly = max(polys, key=_poly_area)
    rings = [_primary(r) for r in poly]
    ext = rings[0]
    lam0 = 0.5 * (ext[:, 0].min() + ext[:, 0].max())
    srings = [_sinu(r, lam0) for r in rings]
    cx, cy = _centroid(srings[0])
    method = 'centroid'
    if not _inside_poly(np.array([cx]), np.array([cy]), srings)[0]:
        method = 'inaccessibility'
        x0, x1 = srings[0][:, 0].min(), srings[0][:, 0].max()
        y0, y1 = srings[0][:, 1].min(), srings[0][:, 1].max()
        best, bestd = None, -1.0
        span_x, span_y, n = x1 - x0, y1 - y0, 48
        ccx, ccy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
        for it in range(5):
            gx, gy = np.meshgrid(np.linspace(ccx - span_x / 2, ccx + span_x / 2, n),
                                 np.linspace(ccy - span_y / 2, ccy + span_y / 2, n))
            gx, gy = gx.ravel(), gy.ravel()
            ins = _inside_poly(gx, gy, srings)
            if ins.any():
                d = _dist_to_rings(gx[ins], gy[ins], srings)
                k = int(np.argmax(d))
                if d[k] > bestd:
                    bestd, best = d[k], (gx[ins][k], gy[ins][k])
            if best is None:  # polygone très fin : grille plus dense
                n = 160
                continue
            ccx, ccy = best
            span_x, span_y, n = span_x * 4 / (n - 1), span_y * 4 / (n - 1), 24
        cx, cy = best
    lat = cy
    lon = lam0 + cx / max(math.cos(math.radians(cy)), 1e-6)
    lon = (lon + 180.0) % 360.0 - 180.0
    return lat, lon, method


def _px_of(lat, lon, W, H):
    c = min(W - 1, max(0, int(math.floor((lon + 180.0) / 360.0 * W))))
    r = min(H - 1, max(0, int(math.floor((90.0 - lat) / 180.0 * H))))
    return r, c


def _r1(v):
    v = round(v + 0.0, 1)
    return 0.0 if v == 0 else v


def build(geo, codes_info, out_assets, out_data, log):
    info = {c['code']: c for c in codes_info}
    ctrs = sorted(geo['countries'], key=lambda c: c['code'])
    for c in ctrs:
        if c['code'] not in info:
            raise RuntimeError('code absent de i18n-iso-countries : ' + c['code'])

    # Carte d'index (plus proche voisin, centres de pixels)
    rings = []
    for i, c in enumerate(ctrs, start=1):
        rings += gr.collect_rings(c['polys'], IDX_W, IDX_H, label=i)
    idx, overlaps = gr.fill_labels(rings, IDX_W, IDX_H)
    log(f'carte d\'index : {overlaps} pixels en conflit')

    entries, stamped, snapped, methods = [], [], [], {}
    taken = set()
    for i, c in enumerate(ctrs, start=1):
        lat, lon, method = representative_point(c['polys'])
        methods[method] = methods.get(method, 0) + 1
        lat, lon = _r1(lat), _r1(lon)
        r, col = _px_of(lat, lon, IDX_W, IDX_H)
        if idx[r, col] != i:
            own = np.argwhere(idx == i)
            if len(own):
                # recaler sur le pixel du pays le plus proche
                rr = own[:, 0]
                cc = own[:, 1]
                plat = 90.0 - (rr + 0.5) * 180.0 / IDX_H
                plon = -180.0 + (cc + 0.5) * 360.0 / IDX_W
                dlon = (plon - lon + 180.0) % 360.0 - 180.0
                d = (plat - lat) ** 2 + (dlon * math.cos(math.radians(lat))) ** 2
                k = int(np.argmin(d))
                lat, lon = _r1(plat[k]), _r1(plon[k])
                snapped.append(c['code'])
            else:
                # pays trop petit pour la grille : on « tamponne » un pixel pour qu'il reste cliquable
                counts = np.bincount(idx.ravel(), minlength=len(ctrs) + 1)
                cand = [(r + dr, (col + dc) % IDX_W) for d in range(6) for dr in range(-d, d + 1)
                        for dc in range(-d, d + 1) if max(abs(dr), abs(dc)) == d and 0 <= r + dr < IDX_H]
                rr, cc = next(p for p in cand if p not in taken and (idx[p] == 0 or counts[idx[p]] > 8))
                idx[rr, cc] = i
                if (rr, cc) != (r, col):
                    lat = _r1(90.0 - (rr + 0.5) * 180.0 / IDX_H)
                    lon = _r1(-180.0 + (cc + 0.5) * 360.0 / IDX_W)
                stamped.append(c['code'])
            r, col = _px_of(lat, lon, IDX_W, IDX_H)
        taken.add((r, col))
        nfo = info[c['code']]
        entries.append({
            'index': i, 'code': c['code'], 'alpha3': nfo['alpha3'], 'numeric': nfo['numeric'],
            'name_fr': NAME_FR.get(c['code'], nfo['name_fr']), 'name_en': NAME_EN.get(c['code'], nfo['name_en']),
            'lat': float(lat), 'lon': float(lon),
        })
    # vérification finale : chaque point représentatif retombe sur son pays
    for e in entries:
        r, col = _px_of(e['lat'], e['lon'], IDX_W, IDX_H)
        assert idx[r, col] == e['index'], e
    log(f'points représentatifs : {methods}, recalés : {snapped}, pixels tamponnés : {stamped}')

    rgb = np.zeros((IDX_H, IDX_W, 3), dtype=np.uint8)
    rgb[..., 0] = idx & 255
    rgb[..., 1] = idx >> 8
    Image.fromarray(rgb, 'RGB').save(out_assets / 'earth_countries.png', optimize=True)
    with open(out_data / 'countries.json', 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'countries': entries}, f, ensure_ascii=False, indent=1)
        f.write('\n')

    by_code = {e['code']: e for e in entries}
    all_codes = []
    for c in sorted(codes_info, key=lambda c: c['code']):
        e = {'code': c['code'], 'alpha3': c['alpha3'], 'numeric': c['numeric'],
             'name_fr': NAME_FR.get(c['code'], c['name_fr']), 'name_en': NAME_EN.get(c['code'], c['name_en']),
             'index': by_code[c['code']]['index'] if c['code'] in by_code else 0}
        if c['code'] in by_code:
            e['lat'], e['lon'] = by_code[c['code']]['lat'], by_code[c['code']]['lon']
        elif c['largest_city']:
            # territoire absent de la carte 1:50m : position de sa plus grande localité
            e['lat'], e['lon'] = _r1(c['largest_city']['lat']), _r1(c['largest_city']['lon'])
        all_codes.append(e)
    with open(out_data / 'country_codes.json', 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'codes': all_codes}, f, ensure_ascii=False, indent=1)
        f.write('\n')
    return entries, idx
