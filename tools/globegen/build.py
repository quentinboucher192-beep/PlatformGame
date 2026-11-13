#!/usr/bin/env python3
"""Régénère toutes les données du globe du classement en ligne.

    cd tools/globegen && npm ci && npm run build
    # ou, depuis la racine du dépôt : python3 -I tools/globegen/build.py

Étapes : extract.js (Node) lit les paquets npm et écrit tools/globegen/build/, puis les
modules Python produisent assets/online/*.png et data/online/*.json et vérifient le tout.
"""
import json
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # lancé avec « python3 -I », le dossier du script n'est pas dans sys.path
sys.dont_write_bytecode = True

import avatars       # noqa: E402
import countries     # noqa: E402
import earth_day     # noqa: E402
import earth_night   # noqa: E402
import flags         # noqa: E402

ROOT = HERE.parent.parent
ASSETS = ROOT / 'assets' / 'online'
DATA = ROOT / 'data' / 'online'
BUILD = HERE / 'build'

SIZE_LIMITS = {'earth_day.png': 3.5e6, 'earth_night.png': 2.0e6, 'earth_countries.png': 0.3e6,
               'flags.png': 0.5e6, 'avatars.png': 0.6e6}
# Pays de contrôle : (code, lat min, lat max, lon min, lon max) plausibles
CHECKS = [('FR', 44, 49, 0, 5), ('US', 35, 42, -102, -94), ('BR', -14, -6, -57, -48), ('JP', 34, 39, 135, 141),
          ('AU', -28, -22, 130, 138), ('ZA', -31, -27, 22, 27), ('CN', 32, 39, 98, 108), ('IN', 19, 25, 76, 82),
          ('RU', 55, 66, 85, 105), ('CA', 52, 62, -106, -92), ('GB', 52, 55, -4, 0), ('DE', 49.5, 52.5, 8, 12)]

T0 = time.time()


def log(msg):
    print(f'[{time.time() - T0:6.1f}s] {msg}', flush=True)


def run_extract():
    if not (HERE / 'node_modules').is_dir():
        log('node_modules absent : npm ci')
        subprocess.run(['npm', 'ci', '--no-audit', '--no-fund'], cwd=HERE, check=True)
    subprocess.run(['node', str(HERE / 'extract.js'), str(BUILD)], cwd=HERE, check=True)


def verify(entries):
    from PIL import Image
    ok = True
    for name, limit in SIZE_LIMITS.items():
        size = (ASSETS / name).stat().st_size
        flag = 'OK' if size <= limit else 'TROP GROS'
        ok &= size <= limit
        log(f'  {name:20s} {size / 1e6:6.2f} Mo  {flag}')
    expected = {'earth_day.png': ((2048, 1024), 'RGB'), 'earth_night.png': ((2048, 1024), 'RGBA'),
                'earth_countries.png': ((1024, 512), 'RGB'), 'avatars.png': ((512, 512), 'RGBA')}
    for name, (size, mode) in expected.items():
        im = Image.open(ASSETS / name)
        if im.size != size or im.mode != mode:
            log(f'  ERREUR {name} : {im.size} {im.mode}')
            ok = False
    idx = Image.open(ASSETS / 'earth_countries.png').convert('RGB').load()
    flags_json = json.loads((DATA / 'flags.json').read_text(encoding='utf-8'))
    by_code = {e['code']: e for e in entries}
    for code, la0, la1, lo0, lo1 in CHECKS:
        e = by_code[code]
        x = min(1023, int((e['lon'] + 180) / 360 * 1024))
        y = min(511, int((90 - e['lat']) / 180 * 512))
        r, g, b = idx[x, y]
        hit = r + 256 * g
        good = la0 <= e['lat'] <= la1 and lo0 <= e['lon'] <= lo1 and hit == e['index'] and b == 0 \
            and flags_json['cells'].get(code, 0) > 0
        ok &= good
        log(f'  {code} #{e["index"]:3d} lat {e["lat"]:6.1f} lon {e["lon"]:7.1f} -> pixel #{hit:3d}, '
            f'drapeau {flags_json["cells"].get(code)} {"OK" if good else "ÉCHEC"}')
    if not ok:
        raise SystemExit('vérification échouée')
    log('vérification : OK')


def main():
    ASSETS.mkdir(parents=True, exist_ok=True)
    DATA.mkdir(parents=True, exist_ok=True)
    log('extraction (Node)...')
    run_extract()
    geo = json.loads((BUILD / 'geo.json').read_text(encoding='utf-8'))
    codes_info = json.loads((BUILD / 'codes.json').read_text(encoding='utf-8'))

    log('pays : countries.json, country_codes.json, earth_countries.png')
    entries, _ = countries.build(geo, codes_info, ASSETS, DATA, log)
    log('earth_day.png')
    land = earth_day.build(geo, ASSETS, log)
    log('earth_night.png')
    earth_night.build(BUILD / 'cities.f32', land, ASSETS, log)
    log('flags.png, flags.json')
    flags.build(codes_info, BUILD / 'flags', ASSETS, DATA, log)
    log('avatars.png, avatars.json')
    avatars.build(ASSETS, DATA, log)
    log('vérification...')
    verify(entries)


if __name__ == '__main__':
    main()
