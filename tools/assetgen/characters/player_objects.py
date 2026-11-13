"""v2 objects for the new player abilities (CONTENT_CONTRACT section 4):
grapple_point, cracked_floor, updraft_vent, heart_container
(sprites/objects/<name>.png + animations/<name>.json)."""
import math

import numpy as np

import effects as FX
import materials as M
from common.palette import CYAN, DANGER, OUTLINE, STONE, SUIT, VIOLET, hex_rgba
from raster import Canvas, Mat, put
from sheet import Sheet

TAU = 2 * math.pi
H_ = hex_rgba
WHITE = H_('#ffffff')

CORE_ON = Mat([CYAN[3], CYAN[2], CYAN[1], CYAN[0]], emissive=True, outline=H_('#0f3b3a'), name="po_core",
              thresholds=[0.82, 0.58, 0.34])
RUNE_ON = Mat([CYAN[2], CYAN[1]], emissive=True, name="po_rune_on", thresholds=[0.5])
RUNE_DIM = Mat([CYAN[0], H_('#137a76')], emissive=True, name="po_rune_dim", thresholds=[0.5])
GRATE = Mat([H_('#5c6b80'), H_('#3d4a5e'), H_('#2a3444'), H_('#1a212d')], name="po_grate",
            thresholds=[0.8, 0.56, 0.32])
GRATE_DARK = Mat([H_('#2a3444'), H_('#1a212d'), H_('#10151e')], name="po_grate_dark", thresholds=[0.6, 0.3])
CRYSTAL_V = Mat([H_('#f6dcff'), VIOLET[2], VIOLET[1], VIOLET[0]], name="po_crys_v", thresholds=[0.82, 0.58, 0.34])
HEART_C = Mat([H_('#ffe4ec'), H_('#ff9ab4'), DANGER[1], DANGER[0], H_('#5a0f1e')], emissive=True,
              outline=H_('#3a0814'), name="po_heart", thresholds=[0.86, 0.68, 0.48, 0.28])
GOLD = Mat([H_('#ffd98a'), H_('#ffb347'), H_('#c8741f'), H_('#7a4512')], name="po_gold",
           thresholds=[0.82, 0.58, 0.34])


# ------------------------------------------------------------ grapple point
def grapple_frame(level, ph, bob=0):
    """Floating ancient anchor ring: stone torus with four metal clamps, runes
    and a cyan energy core held in the middle.  level 0..1 = core brightness
    (1 = targeted)."""
    S = 24
    cx, cy = 12.0, 12.0 + bob
    cv = Canvas(S, S)
    cv.part()
    d = np.sqrt((cv.px - cx) ** 2 + (cv.py - cy) ** 2)
    ring = (d <= 9.4) & (d > 5.9)
    mid = (d - 7.65) / 1.75
    nx, ny = (cv.px - cx) / np.maximum(d, 1e-3), (cv.py - cy) / np.maximum(d, 1e-3)
    nz = np.sqrt(np.clip(1 - mid ** 2, 0, 1))
    cv.put(ring, cv.light_from_normal(nx * mid, ny * mid, nz), M.STONE_M)
    # segment joints
    for k in range(8):
        a = TAU * k / 8 + TAU / 16
        cv.line((cx + math.cos(a) * 6.2, cy + math.sin(a) * 6.2), (cx + math.cos(a) * 9.0, cy + math.sin(a) * 9.0),
                M.STONE_DARK, inten=0.2, decal=True)
    # four metal clamps (N, E, S, W) gripping the ring
    for k in range(4):
        a = TAU * k / 4 - math.pi / 2
        ca, sa = math.cos(a), math.sin(a)
        cv.part()
        pts = []
        for (r, w) in ((5.2, 1.3), (10.6, 2.0)):
            pts.append((cx + ca * r - sa * w, cy + sa * r + ca * w))
        for (r, w) in ((10.6, -2.0), (5.2, -1.3)):
            pts.append((cx + ca * r - sa * w, cy + sa * r + ca * w))
        pts = [pts[0], pts[1], pts[2], pts[3]]
        cv.poly(pts, M.METAL, bevel=1.0, base=0.55)
    # runes between the clamps
    cv.part(line=False)
    for k in range(4):
        a = TAU * k / 4 + TAU / 8
        on = level > 0.5 or (k + int(ph / TAU * 6)) % 4 == 0
        cv.px_set(cx + math.cos(a) * 7.6, cy + math.sin(a) * 7.6, RUNE_ON if on else RUNE_DIM,
                  inten=0.9, decal=True)
    # energy core
    cv.part()
    r = 2.5 + 0.35 * math.sin(ph) + 0.6 * level
    cv.disc((cx, cy), r, CORE_ON, bias=-0.15 + 0.3 * level + 0.08 * math.sin(ph))
    cv.part(line=False)
    cv.disc((cx - 0.6, cy - 0.6), max(0.8, r * 0.45), CORE_ON, flat=1.0, decal=True)
    img = cv.render()
    # energy tethers from the core to the clamps (only in the gap)
    for k in range(4):
        a = TAU * k / 4 - math.pi / 2 + (ph * 0.0)
        for t in np.linspace(r + 0.6, 5.4, 4):
            x, y = int(cx + math.cos(a) * t), int(cy + math.sin(a) * t)
            if img[y, x, 3] == 0 and (level > 0.5 or int(t * 2 + ph) % 2 == 0):
                img[y, x] = CYAN[1] if level > 0.5 else CYAN[0]
    if level > 0.5:
        # targeted: bright halo + orbiting motes
        for k in range(3):
            a = ph + TAU * k / 3
            x, y = int(cx + math.cos(a) * 11.2), int(cy + math.sin(a) * 11.2)
            if 0 <= x < S and 0 <= y < S and img[y, x, 3] == 0:
                img[y, x] = CYAN[3] if k == 0 else CYAN[2]
        put(img, int(cx), int(cy - 1), WHITE)
    return img


def grapple_point_clips():
    bob = [0, 0, -1, -1, -1, 0]
    idle = [grapple_frame(0.0, TAU * i / 6, bob[i]) for i in range(6)]
    act = []
    for i in range(4):
        f = grapple_frame(1.0, TAU * i / 4, 0)
        if i % 2 == 0:
            FX.sparkle(f, 12, 11, 1, WHITE, CYAN[2])
        act.append(f)
    return [("idle", idle, dict(fps=6, loop=True)), ("active", act, dict(fps=10, loop=True))]


# ------------------------------------------------------------ cracked floor
# hairline cracks radiating from the impact point at the top centre
CRACKS_THIN = [((16, 1), (13, 4)), ((13, 4), (9, 5)), ((9, 5), (6, 4)), ((16, 1), (19, 4)), ((19, 4), (23, 5)),
               ((23, 5), (26, 4)), ((16, 1), (16, 6)), ((16, 6), (14, 9))]
# the cracked state: same cracks, widened, plus new branches reaching the edges
CRACKS_WIDE = CRACKS_THIN + [((14, 9), (15, 13)), ((9, 5), (7, 9)), ((7, 9), (3, 11)), ((23, 5), (24, 9)),
                             ((24, 9), (29, 11)), ((16, 6), (19, 9)), ((19, 9), (20, 13)), ((6, 4), (2, 5)),
                             ((26, 4), (30, 3))]


def floor_slab(cracked):
    """Two tiles of ancient stone slab (walkable top), veined with the violet
    crystal of the breakable walls.  Intact: hairline cracks radiating from
    the centre; cracked: the fissures are wide and glow, the edge is chipped
    and the middle block has sunk."""
    W, H = 32, 16
    cv = Canvas(W, H)
    rng = np.random.default_rng(23)
    blocks = [(0.5, 10.5, 0.0), (10.5, 21.5, 1.0 if cracked else 0.0), (21.5, 31.5, 0.0)]
    for k, (x0, x1, sink) in enumerate(blocks):
        cv.part(line=2 if k else True)
        cl = 1.4 if k == 0 else 0.0
        cr = 1.4 if k == 2 else 0.0
        pts = [(x0, cl + sink), (x0 + cl, sink), (x1 - cr, sink), (x1, cr + sink),
               (x1, 14.6 - cr), (x1 - cr * 1.2, 15.9), (x0 + cl * 1.2, 15.9), (x0, 14.6 - cl)]
        cv.poly(pts, M.STONE_M, bevel=1.4, base=0.56 - 0.04 * k % 2)
        # worn top lip
        cv.line((x0 + 1.5, 1.0 + sink), (x1 - 1.5, 1.0 + sink), M.STONE_M, inten=0.9, decal=True)
    cv.part(line=False)
    # speckles + carved band
    for _ in range(14):
        x, y = int(rng.integers(2, 30)), int(rng.integers(3, 14))
        cv.px_set(x, y, M.STONE_DARK, inten=0.45, decal=True)
    for (x0, x1) in ((2, 8), (24, 30)):
        cv.line((x0, 12), (x1, 12), M.STONE_DARK, inten=0.25, decal=True)
    cv.px_set(5, 10, M.STONE_DARK, inten=0.25, decal=True)
    cv.px_set(27, 10, M.STONE_DARK, inten=0.25, decal=True)
    # violet crystal inclusions (shared look with breakable_wall)
    for (x, y, r) in ((5.5, 7.0, 1.5), (27.0, 7.5, 1.4), (12.5, 12.0, 1.0)):
        cv.disc((x, y), r, CRYSTAL_V, bias=0.05, decal=True)
    img = cv.render()
    segs = CRACKS_WIDE if cracked else CRACKS_THIN
    cr = Canvas(W, H)
    cr.part(line=False)
    dk = Mat([OUTLINE], name="po_crack_dk")
    glow = Mat([WHITE], emissive=True, name="po_crack_glow")
    glow2 = Mat([VIOLET[2]], emissive=True, name="po_crack_glow2")
    for a, b in segs:
        cr.line(a, b, dk)
    if cracked:
        # fissures 2 px wide: dark lower lip + glowing core (like breakable_wall)
        for i, (a, b) in enumerate(segs):
            cr.line((a[0] + 1, a[1]), (b[0] + 1, b[1]), glow if i % 3 == 0 or i < 3 else glow2)
    c = cr.render(outline=False)
    sel = (c[:, :, 3] > 0) & (img[:, :, 3] > 0)
    img[sel] = c[sel]
    if cracked:
        # chipped top edge around the impact point
        for (x, y) in ((15, 1), (16, 1), (17, 1), (16, 2)):
            img[y, x] = (0, 0, 0, 0)
        for (x, y) in ((14, 1), (18, 1), (15, 2), (17, 2), (16, 3)):
            if img[y, x, 3]:
                img[y, x] = OUTLINE
        FX.sparkle(img, 16, 6, 1, WHITE, VIOLET[2])
        put(img, 8, 6, VIOLET[2])
        put(img, 24, 7, VIOLET[2])
    else:
        # faint violet glints in the cracks: hints that it can break
        put(img, 13, 5, VIOLET[1])
        put(img, 20, 5, VIOLET[1])
    return img


def cracked_floor_clips():
    return [("intact", [floor_slab(False)], dict(fps=1, loop=False)),
            ("cracked", [floor_slab(True)], dict(fps=1, loop=False))]


# ------------------------------------------------------------- updraft vent
def vent_frame(i, n=6):
    """Low metal housing with a slotted grate; cyan light from inside and
    wisps of air rising and fading (scroll period = n frames)."""
    W, H = 32, 16
    img = np.zeros((H, W, 4), np.uint8)
    # faint column of rising air (stronger near the grate)
    for y in range(0, 11):
        t = y / 10.0
        for x in range(5, 27):
            edge = min(x - 5, 26 - x)
            a = int(48 * t ** 2 * min(1.0, edge / 3.0))
            if a > 6:
                img[y, x] = (CYAN[1][0], CYAN[1][1], CYAN[1][2], a)
    # rising air streaks: wavy dashes scrolling up 2 px per frame
    streams = [(7, 0), (11, 7), (16, 3), (21, 9), (25, 5)]
    for k, (x, off) in enumerate(streams):
        for j in range(2):
            y0 = 11 - ((i * 2 + off + j * 6) % 12)
            for d in range(3):
                y = y0 + d
                if not 0 <= y <= 10:
                    continue
                t = y / 10.0  # 1 near the grate .. 0 at the top
                sway = int(round(math.sin(y * 0.55 + k * 1.7 + i * TAU / n) * 1.0))
                a = int((90 + 140 * t) if d < 2 else (50 + 80 * t)) * min(1.0, 0.25 + y / 4.0)
                c = CYAN[3] if (d == 0 and t > 0.5) else (CYAN[2] if t > 0.35 else CYAN[1])
                img[y, x + sway] = (c[0], c[1], c[2], min(255, int(a)))
    cv = Canvas(W, H)
    cv.part()
    cv.poly([(1.0, 15.9), (31.0, 15.9), (29.0, 11.0), (27.0, 10.0), (5.0, 10.0), (3.0, 11.0)], GRATE, bevel=1.3,
            base=0.55)
    # grate slots with light inside
    cv.part(line=False)
    pulse = 0.5 + 0.5 * math.sin(TAU * i / n)
    for x in range(7, 26, 3):
        cv.line((x, 11), (x, 13), GRATE_DARK, inten=0.2, decal=True)
        cv.px_set(x, 11, CORE_ON, inten=0.75 + 0.2 * pulse, decal=True)
        cv.px_set(x, 12, CORE_ON, inten=0.45 + 0.2 * pulse, decal=True)
    # side lights
    for x in (3, 28):
        cv.px_set(x, 13, CORE_ON, inten=0.9 if (i % 3) else 0.6, decal=True)
    # bolts
    for x in (5, 26):
        cv.px_set(x, 14, GRATE, inten=0.95, decal=True)
    frame = cv.render()
    FX.overlay(img, frame)
    return img


def updraft_vent_clips():
    return [("idle", [vent_frame(i) for i in range(6)], dict(fps=10, loop=True))]


# ---------------------------------------------------------- heart container
def heart_frame(i, n=8):
    """Faceted crystal heart (two lobes + point) bobbing above a soft pink
    glow; facets catch the light from the top-left and a glint sweeps
    across it."""
    bob = [0, 0, -1, -1, -1, -1, 0, 0][i]
    cv = Canvas(16, 16)
    cy = 6.0 + bob
    lobe_l, lobe_r, r = (5.3, cy), (10.7, cy), 3.3
    tip = (8.0, cy + 7.6)
    cv.part()
    dl = (cv.px - lobe_l[0]) ** 2 + (cv.py - lobe_l[1]) ** 2 <= r * r
    dr = (cv.px - lobe_r[0]) ** 2 + (cv.py - lobe_r[1]) ** 2 <= r * r
    tri = cv.poly_mask([(lobe_l[0] - r * 0.96, cy + 0.9), (lobe_r[0] + r * 0.96, cy + 0.9), tip])
    mask = dl | dr | tri
    # facets: lit left lobe, mid right lobe, darker lower halves
    girdle = cy + 1.2
    left = cv.px < 8.0 + (cv.py - girdle) * 0.12
    inten = np.where(cv.py < girdle, np.where(left, 0.8, 0.6), np.where(left, 0.45, 0.25)).astype(np.float32)
    cv.put(mask, inten, HEART_C)
    cv.part(line=False)
    # facet lines (girdle + keel), highlights on the left lobe
    cv.line((3.0, girdle), (13.0, girdle), HEART_C, inten=0.95, decal=True)
    cv.line((8.0, cy - 1.0), (8.0, cy + 6.0), HEART_C, inten=0.68, decal=True)
    cv.px_set(4, cy - 2, HEART_C, inten=1.0, decal=True)
    cv.px_set(5, cy - 2, HEART_C, inten=1.0, decal=True)
    cv.px_set(4, cy - 1, HEART_C, inten=1.0, decal=True)
    img = cv.render()
    # glint sweep (diagonal)
    gx = [-1, -1, 3, 5, 7, 9, 11, -1][i]
    if gx > 0:
        for k in range(3):
            xx, yy = gx + k - 1, int(cy) + 2 - k
            if 0 <= yy < 16 and img[yy, xx, 3] and tuple(img[yy, xx]) != OUTLINE:
                img[yy, xx] = WHITE if k == 1 else H_('#ffe4ec')
    if i == 5:
        FX.sparkle(img, 13, int(cy) - 3, 1, WHITE, H_('#ff9ab4'))
    # soft glow on the ground under it (pivot = bottom-centre)
    for xx in range(6, 10):
        if img[15, xx, 3] == 0:
            img[15, xx] = (DANGER[1][0], DANGER[1][1], DANGER[1][2], 90 - 25 * abs(bob))
    return img


def heart_container_clips():
    return [("idle", [heart_frame(i) for i in range(8)], dict(fps=8, loop=True))]


# name: (frame w, frame h, pivot, clip builder)
OBJECTS = {
    "grapple_point": (24, 24, [12, 12], grapple_point_clips),
    "cracked_floor": (32, 16, [16, 16], cracked_floor_clips),
    "updraft_vent": (32, 16, [16, 16], updraft_vent_clips),
    "heart_container": (16, 16, [8, 16], heart_container_clips),
}


def build():
    for name, (fw, fh, pivot, clips) in OBJECTS.items():
        s = Sheet(f"sprites/objects/{name}.png", fw, fh, pivot)
        for clip, frames, kw in clips():
            s.add(clip, frames, **kw)
        s.save(f"animations/{name}.json")
    build_icewater()


# ------------------------------------------------------------- icy water (Glacior)
# Palette swap of the water strip (same layout, same animation) for the frozen planet.
ICEWATER = {
    (19, 92, 80): (28, 74, 128), (31, 138, 106): (47, 111, 179), (143, 255, 240): (238, 250, 255),
    (31, 184, 176): (95, 176, 255), (15, 59, 58): (19, 38, 74), (57, 240, 216): (168, 228, 255),
}


def build_icewater():
    from PIL import Image
    from sheet import asset_path, save_png
    src = np.array(Image.open(asset_path("sprites/objects/liquid_water.png")).convert("RGBA"))
    out = src.copy()
    rgb = src[..., :3].astype(np.int32)
    for (r, g, b), (nr, ng, nb) in ICEWATER.items():
        mask = (rgb[..., 0] == r) & (rgb[..., 1] == g) & (rgb[..., 2] == b)
        out[mask, 0], out[mask, 1], out[mask, 2] = nr, ng, nb
    # Any other colour (anti-aliased edges) is shifted toward ice blue keeping its luminance.
    known = np.zeros(src.shape[:2], bool)
    for (r, g, b) in ICEWATER:
        known |= (rgb[..., 0] == r) & (rgb[..., 1] == g) & (rgb[..., 2] == b)
    other = (~known) & (src[..., 3] > 0)
    lum = (0.3 * rgb[..., 0] + 0.59 * rgb[..., 1] + 0.11 * rgb[..., 2])
    out[other, 0] = np.clip(lum[other] * 0.55, 0, 255)
    out[other, 1] = np.clip(lum[other] * 0.85, 0, 255)
    out[other, 2] = np.clip(lum[other] * 1.35 + 20, 0, 255)
    save_png(out, "sprites/objects/liquid_icewater.png")
