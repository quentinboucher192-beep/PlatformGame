"""flags.png (atlas 48x36, 32 colonnes) et flags.json."""
import json
import math

from PIL import Image, ImageDraw, ImageFilter

CELL_W, CELL_H, COLUMNS, SS = 48, 36, 32, 4


def unknown_flag():
    """Cellule 0 : fond bleu nuit et petit globe filaire cyan."""
    w, h = CELL_W * SS, CELL_H * SS
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            t = (x / w * 0.35 + y / h * 0.65)
            px[x, y] = (int(14 + 10 * (1 - t)), int(24 + 18 * (1 - t)), int(62 + 30 * (1 - t)), 255)
    glow = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    cx, cy, r = w / 2, h / 2, h * 0.34
    gd.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(57, 240, 216, 160), width=10)
    glow = glow.filter(ImageFilter.GaussianBlur(8))
    im = Image.alpha_composite(im, glow)
    d = ImageDraw.Draw(im)
    line = (190, 250, 255, 255)
    lw = 7
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=line, width=lw)
    d.ellipse([cx - r * 0.45, cy - r, cx + r * 0.45, cy + r], outline=line, width=lw - 2)
    d.line([cx, cy - r, cx, cy + r], fill=line, width=lw - 2)
    d.line([cx - r, cy, cx + r, cy], fill=line, width=lw - 2)
    for k in (-0.55, 0.55):
        yy = cy + k * r
        half = r * math.sqrt(1 - k * k)
        d.line([cx - half, yy, cx + half, yy], fill=line, width=lw - 3)
    return im.reduce(SS)


def build(codes_info, flags_dir, out_assets, out_data, log):
    cells = {}
    tiles = [unknown_flag()]
    for c in sorted(codes_info, key=lambda c: c['code']):
        path = flags_dir / (c['code'] + '.png')
        if not c['flag_svg'] or not path.exists():
            cells[c['code']] = 0
            continue
        im = Image.open(path).convert('RGBA')
        if im.size != (CELL_W * SS, CELL_H * SS):
            im = im.resize((CELL_W * SS, CELL_H * SS), Image.LANCZOS)
        cells[c['code']] = len(tiles)
        tiles.append(im.reduce(SS))
    rows = (len(tiles) + COLUMNS - 1) // COLUMNS
    atlas = Image.new('RGBA', (COLUMNS * CELL_W, rows * CELL_H), (0, 0, 0, 0))
    for i, t in enumerate(tiles):
        atlas.paste(t, ((i % COLUMNS) * CELL_W, (i // COLUMNS) * CELL_H))
    atlas.save(out_assets / 'flags.png', optimize=True)
    with open(out_data / 'flags.json', 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'cell_w': CELL_W, 'cell_h': CELL_H, 'columns': COLUMNS, 'rows': rows,
                   'unknown': 0, 'cells': cells}, f, ensure_ascii=False, indent=1)
        f.write('\n')
    missing = sorted(k for k, v in cells.items() if v == 0)
    log(f'  {len(tiles) - 1} drapeaux, atlas {atlas.size[0]}x{atlas.size[1]}, sans SVG : {missing or "aucun"}')
