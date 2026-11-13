"""Game icon: Nova's helmet with its glowing cyan visor on a dark round badge.

ui/app_icon.png (256x256) is drawn as 64x64 pixel art upscaled 4x; the
installer/app.ico gets natively rasterised 16/32/48 versions plus the 256."""
import math
import os

import numpy as np
from PIL import Image

import materials as M
from common.palette import CYAN, OUTLINE, SCARF, SKY, STONE, hex_rgba
from raster import Canvas, Mat
from sheet import REPO, save_png

BADGE = Mat([SKY[3], SKY[2], SKY[1], SKY[0]], name="badge", thresholds=[0.86, 0.6, 0.3])
RIM = Mat([CYAN[2], CYAN[1], CYAN[0], hex_rgba('#0f3b3a')], emissive=True, name="rim", thresholds=[0.8, 0.55, 0.3])


def draw_icon(n):
    """Rasterise the icon at n x n pixels (geometry defined on a 64 grid)."""
    s = n / 64.0
    cv = Canvas(n, n)

    def P(x, y):
        return (x * s, y * s)

    # badge with a glowing rim
    cv.part(line=False)
    cv.disc(P(32, 32), 31.0 * s, RIM, bias=0.05)
    cv.part(line=False)
    cv.disc(P(32, 32), 28.6 * s, BADGE, bias=-0.1)
    # soft cyan halo behind the helmet (hard bands)
    halo = Mat([hex_rgba('#22365a'), hex_rgba('#1f4f63')], name="halo", thresholds=[0.5])
    cv.disc(P(33, 31), 22.0 * s, halo, flat=0.3, decal=True)
    cv.disc(P(33, 31), 19.5 * s, halo, flat=0.8, decal=True)
    # stars
    if n >= 32:
        for (x, y) in ((12, 18), (50, 14), (16, 44), (52, 46), (44, 9), (9, 31)):
            cv.px_set(x * s, y * s, Mat([hex_rgba('#e6fffb')], name="star"), inten=1.0, decal=True)
    ink = Mat([OUTLINE], name="ink")
    # scarf tail fluttering left (outlined)
    tail = [P(22, 46), P(16, 48.5), P(11, 46.5), P(6.5, 49.5)]
    cv.part(line=False)
    cv.chain(tail, [4.4 * s, 3.9 * s, 3.3 * s, 2.5 * s], ink, flat=0.5)
    cv.part()
    cv.chain(tail, [3.2 * s, 2.7 * s, 2.1 * s, 1.4 * s], M.SCARF_M)
    # antenna (behind the helmet)
    cv.part(line=False)
    cv.capsule(P(24, 16), P(18.5, 7.5), 1.9 * s, 1.7 * s, ink, flat=0.5)
    cv.disc(P(18.0, 6.5), 3.4 * s, ink, flat=0.5)
    cv.capsule(P(24, 16), P(18.5, 7.5), 0.9 * s, 0.7 * s, M.SUIT_DARK, flat=0.6)
    cv.disc(P(18.0, 6.5), 2.3 * s, M.CORE, flat=0.95)
    # helmet with a solid dark rim
    hc = P(33, 30)
    cv.part(line=False)
    cv.disc(hc, 19.3 * s, ink, flat=0.5)
    cv.part()
    cv.disc(hc, 18.0 * s, M.SUIT_M, bias=0.04)
    # visor (3/4 view, facing right) with a dark seal
    vc = P(37.5, 31.5)
    inner = lambda px, py: (px - hc[0]) ** 2 + (py - hc[1]) ** 2 <= (14.6 * s) ** 2
    seal = lambda px, py: (px - hc[0]) ** 2 + (py - hc[1]) ** 2 <= (15.8 * s) ** 2
    cv.ellipse(vc, 13.7 * s, 11.7 * s, M.SUIT_DARK, ang=-0.15, flat=0.2, decal=True, within=seal)
    cv.ellipse(vc, 12.5 * s, 10.5 * s, M.VISOR_M, ang=-0.15, decal=True, within=inner)
    if n >= 32:
        glint = Mat([hex_rgba('#ffffff')], name="glint")
        cv.capsule(P(31, 26), P(35, 23.5), 1.0 * s, 0.8 * s, glint, flat=1.0, decal=True, within=inner)
        cv.capsule(P(30, 30), P(31, 29.4), 0.9 * s, 0.7 * s, glint, flat=1.0, decal=True, within=inner)
    else:
        cv.px_set(*P(32, 25), M.VISOR_M, inten=1.0, decal=True)
    # ear piece
    cv.disc(P(18.8, 32.5), 2.6 * s, M.SUIT_DARK, bias=-0.15, decal=True)
    # scarf collar, curving under the helmet
    collar = [P(19, 44), P(26, 48.5), P(35, 49.5), P(44, 46.5)]
    cv.part(line=False)
    cv.chain(collar, [4.4 * s] * 4, ink, flat=0.5)
    cv.part()
    cv.chain(collar, [3.2 * s] * 4, M.SCARF_M)
    img = cv.render()
    return img


def build():
    art = draw_icon(64)
    big = Image.fromarray(art, "RGBA").resize((256, 256), Image.NEAREST)
    save_png(big, "ui/app_icon.png")
    sizes = {16: draw_icon(16), 32: draw_icon(32), 48: draw_icon(48)}
    imgs = [Image.fromarray(sizes[k], "RGBA") for k in (16, 32, 48)]
    path = os.path.join(REPO, "installer", "app.ico")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    big.save(path, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (256, 256)], append_images=imgs)
    return big, sizes
