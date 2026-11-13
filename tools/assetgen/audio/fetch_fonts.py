#!/usr/bin/env python3
"""Downloads the OFL-licensed UI fonts (docs/ASSET_SPEC.md section 13) from the google/fonts
repository into assets/fonts/ and verifies them.

Usage (from the repo root):  python3 tools/assetgen/audio/fetch_fonts.py [--force]

Files produced:
  Orbitron.ttf               (variable font Orbitron[wght].ttf, default instance = Regular 400)
  ChakraPetch-Regular.ttf, ChakraPetch-SemiBold.ttf
  OFL-Orbitron.txt, OFL-ChakraPetch.txt   (licence texts)
"""
import argparse
import os
import shutil
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)

from fontcheck import check_font  # noqa: E402

FONTS_DIR = os.path.join(ROOT, 'assets', 'fonts')
MIRRORS = ['https://raw.githubusercontent.com/google/fonts/main/',
           'https://github.com/google/fonts/raw/main/']
FILES = {
    'Orbitron.ttf': 'ofl/orbitron/Orbitron%5Bwght%5D.ttf',
    'ChakraPetch-Regular.ttf': 'ofl/chakrapetch/ChakraPetch-Regular.ttf',
    'ChakraPetch-SemiBold.ttf': 'ofl/chakrapetch/ChakraPetch-SemiBold.ttf',
    'OFL-Orbitron.txt': 'ofl/orbitron/OFL.txt',
    'OFL-ChakraPetch.txt': 'ofl/chakrapetch/OFL.txt',
}
ROLES = {'Orbitron.ttf': 'title'}   # Orbitron is a display face: no ^ or guillemets


def download(rel, dest):
    last = None
    for base in MIRRORS:
        url = base + rel
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'assetgen-fetch-fonts'})
            with urllib.request.urlopen(req, timeout=60) as r, open(dest, 'wb') as f:
                shutil.copyfileobj(r, f)
            if os.path.getsize(dest) > 0:
                return url
        except Exception as e:  # try the next mirror
            last = e
    raise RuntimeError(f'could not download {rel}: {last}')


def verify(name, path):
    if name.endswith('.txt'):
        with open(path, encoding='utf-8', errors='replace') as f:
            text = f.read()
        if 'SIL OPEN FONT LICENSE' not in text.upper():
            return ['licence text does not look like the SIL OFL'], []
        return [], []
    errors, warnings, info = check_font(path, ROLES.get(name, 'ui'))
    return errors, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--force', action='store_true', help='re-download even if valid files exist')
    args = ap.parse_args()
    os.makedirs(FONTS_DIR, exist_ok=True)
    failed = False
    with tempfile.TemporaryDirectory(prefix='fonts-dl-') as tmp:
        for name, rel in FILES.items():
            dest = os.path.join(FONTS_DIR, name)
            if os.path.exists(dest) and not args.force and not verify(name, dest)[0]:
                print(f'  ok (cached)  fonts/{name}')
                continue
            tmp_path = os.path.join(tmp, name)
            url = download(rel, tmp_path)
            errors, warnings = verify(name, tmp_path)
            if errors:
                failed = True
                print(f'  FAIL fonts/{name} from {url}: ' + '; '.join(errors))
                continue
            shutil.move(tmp_path, dest)
            print(f'  ok           fonts/{name}  ({os.path.getsize(dest)} bytes from {url})')
            for w in warnings:
                print(f'     note: {w}')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
