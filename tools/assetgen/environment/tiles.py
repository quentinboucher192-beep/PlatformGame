"""Terrain tilesets (ASSET_SPEC section 7) - autotile engine + atlas writer.

Atlas 256x64 of 16x16 cells:
  row 0: autotile cell for 4-bit mask (bit0 UP, bit1 RIGHT, bit2 DOWN, bit3 LEFT), column = mask
  row 1: 16 interior variants (mask 15)
  row 2: cols 0-3 inner-corner overlays TL, TR, BL, BR (diagonal neighbour empty)
  row 3: 8 top-decoration overlays (drawn with offset -12 above exposed tops)

Seamlessness strategy: every material has a 16-periodic interior texture expressed as indices
into a colour ramp, and 16-periodic edge profiles (functions of the coordinate along the edge
only), so two cells that share an edge always continue each other's pattern exactly. Edges
facing air get rim shading (lit top/left, shaded right/bottom) by shifting ramp indices.
"""
import numpy as np

from helpers import P, T, col, new, save_png, save_json

TS = 16
INF = 99
OUT = col(P.OUTLINE)


class Geo:
    """Solid mask + distance-to-exposed-edge fields of one autotile cell."""

    def __init__(self, mask):
        self.mask = mask
        self.et = not (mask & 1)
        self.er = not (mask & 2)
        self.eb = not (mask & 4)
        self.el = not (mask & 8)
        s = np.ones((TS, TS), bool)
        if self.et and self.el:
            s[0, 0] = s[0, 1] = s[1, 0] = False
        if self.et and self.er:
            s[0, 15] = s[0, 14] = s[1, 15] = False
        if self.eb and self.el:
            s[15, 0] = s[15, 1] = s[14, 0] = False
        if self.eb and self.er:
            s[15, 15] = s[15, 14] = s[14, 15] = False
        self.solid = s
        ar = np.arange(TS)
        self.dt = np.full((TS, TS), INF)
        self.db = np.full((TS, TS), INF)
        self.dl = np.full((TS, TS), INF)
        self.dr = np.full((TS, TS), INF)
        for i in range(TS):
            c = np.where(s[:, i])[0]
            r = np.where(s[i, :])[0]
            if self.et:
                self.dt[c.min():, i] = ar[c.min():] - c.min()
            if self.eb:
                self.db[:c.max() + 1, i] = c.max() - ar[:c.max() + 1]
            if self.el:
                self.dl[i, r.min():] = ar[r.min():] - r.min()
            if self.er:
                self.dr[i, :r.max() + 1] = r.max() - ar[:r.max() + 1]
        for d in (self.dt, self.db, self.dl, self.dr):
            d[~s] = INF

    def rim(self, x, y):
        return min(self.dl[y, x], self.dr[y, x], self.db[y, x]) == 0


def checker(x, y, phase=0):
    return (x + y + phase) % 2 == 0


class Material:
    """Base class. Subclasses define ramp, base indices, lip colours, variants, decor."""
    name = "material"
    map_color = "#3b5466"
    ramp = []
    # (offset, checker?) per distance 1..3 from an exposed edge (distance 0 = outline)
    left_band = [(2, False), (1, False), (1, True)]
    right_band = [(-1, False), (-1, False), (-1, True)]
    bottom_band = [(-2, False), (-1, False), (-1, True)]
    under_lip = [(-1, False)]          # rows right below the lip
    top_th = [4] * 16                  # lip thickness per column (periodic)
    drape_l = (0, 0)                   # lip drape length down the exposed left side (x=0, x=1)
    drape_r = (0, 0)
    outline_color = OUT

    def __init__(self):
        self.base_idx = self.make_base_idx()
        self.base = self.colorize(self.base_idx)
        self.add_base_details(self.base)

    # -- to override ---------------------------------------------------
    def make_base_idx(self):
        raise NotImplementedError

    def add_base_details(self, img):
        """Non-ramp colour details baked into every cell (glints etc.)."""

    def top_color(self, x, d, th):
        raise NotImplementedError

    def drape_color(self, side, k, d, n):
        return None

    def variants(self):
        return [self.base.copy() for _ in range(16)]

    def top_decor(self):
        return [new(TS, TS) for _ in range(8)]

    def post_cell(self, img, g):
        pass

    # -- helpers ---------------------------------------------------------
    def colorize(self, idx):
        pal = np.array([col(c) for c in self.ramp], np.uint8)
        return pal[np.clip(idx, 0, len(self.ramp) - 1)]

    def offsets(self, g):
        off = np.zeros((TS, TS), int)
        for y in range(TS):
            for x in range(TS):
                if not g.solid[y, x]:
                    continue
                o = 0
                for d, band in ((g.dl[y, x], self.left_band), (g.dr[y, x], self.right_band),
                                (g.db[y, x], self.bottom_band)):
                    if 1 <= d <= len(band):
                        v, chk = band[d - 1]
                        if not chk or checker(x, y):
                            o += v
                if g.et:
                    d = g.dt[y, x] - self.top_th[x]
                    if 0 <= d < len(self.under_lip):
                        v, chk = self.under_lip[d]
                        if not chk or checker(x, y):
                            o += v
                off[y, x] = max(-2, min(2, o))
        return off

    # -- composition -----------------------------------------------------
    def paint_top(self, img, g, th_fn=None, drapes=True):
        if not g.et:
            return img
        for x in range(TS):
            th = self.top_th[x] if th_fn is None else th_fn(x)
            for y in range(TS):
                if g.solid[y, x] and g.dt[y, x] < th:
                    c = self.top_color(x, g.dt[y, x], th)
                    if c is not None:
                        img[y, x] = col(c)
        if not drapes:
            return img
        for side, lens, xs in (("l", self.drape_l, (0, 1)), ("r", self.drape_r, (15, 14))):
            if (side == "l" and not g.el) or (side == "r" and not g.er):
                continue
            for k, x in enumerate(xs):
                n = lens[k]
                ys = np.where(g.solid[:, x])[0]
                top = ys[0]
                for i in range(n):
                    y = top + i
                    if y < TS and g.solid[y, x] and g.dt[y, x] >= self.top_th[x]:
                        c = self.drape_color(side, k, i, n)
                        if c is not None:
                            img[y, x] = col(c)
        return img

    def compose(self, mask):
        g = Geo(mask)
        idx = self.base_idx + self.offsets(g)
        img = self.colorize(idx)
        # keep baked details only where no shading happened
        same = (self.offsets(g) == 0)[..., None]
        img = np.where(same, self.base, img).astype(np.uint8)
        for y in range(TS):
            for x in range(TS):
                if g.solid[y, x] and g.rim(x, y):
                    img[y, x] = col(self.outline_color)
        img[~g.solid] = T
        self.paint_top(img, g)
        self.post_cell(img, g)
        return img

    def inner_corner(self, which):
        ov = new(TS, TS)
        if which in ("TL", "TR"):
            g = Geo(15 & ~1)
            reach = 6

            def kx(x):
                return x if which == "TL" else 15 - x

            def taper(x):
                return max(0, min(self.top_th[x], reach - kx(x)))

            full = self.base.copy()
            # shadow under the continued lip
            for x in range(TS):
                t = taper(x)
                if t > 0:
                    for i, (v, chk) in enumerate(self.under_lip):
                        y = t + i
                        if y < TS and (not chk or checker(x, y)):
                            full[y, x] = self.colorize(np.array([[self.base_idx[y, x] + v]]))[0, 0]
            self.paint_top(full, g, th_fn=taper, drapes=False)
            diff = (full != self.base).any(axis=-1)
            ov[diff] = full[diff]
        else:
            idx = self.base_idx.copy()
            m = np.zeros((TS, TS), bool)
            for y in range(TS):
                for x in range(TS):
                    k = x if which == "BL" else 15 - x
                    d = 15 - y
                    if k + d <= 1:
                        idx[y, x] -= 2
                        m[y, x] = True
                    elif k + d <= 3 and (k + d == 2 or checker(x, y)):
                        idx[y, x] -= 1
                        m[y, x] = True
            c = self.colorize(idx)
            ov[m] = c[m]
        return ov

    def build(self):
        atlas = new(256, 64)
        for m in range(16):
            atlas[0:16, m * 16:(m + 1) * 16] = self.compose(m)
        vs = self.variants()
        assert len(vs) == 16
        for i, v in enumerate(vs):
            atlas[16:32, i * 16:(i + 1) * 16] = v
        for i, w in enumerate(("TL", "TR", "BL", "BR")):
            atlas[32:48, i * 16:(i + 1) * 16] = self.inner_corner(w)
        ds = self.top_decor()
        assert len(ds) == 8
        for i, d in enumerate(ds):
            atlas[48:64, i * 16:(i + 1) * 16] = d
        return atlas


def generate():
    from tile_materials import MATERIALS
    out = []
    for M in MATERIALS:
        m = M()
        atlas = m.build()
        out.append(save_png(atlas, f"textures/tiles/{m.name}.png"))
        out.append(save_json({
            "texture": f"textures/tiles/{m.name}.png", "tileSize": 16,
            "maskRow": 0, "variantRow": 1, "variantCount": 16,
            "innerCornerRow": 2, "topDecorRow": 3, "topDecorCount": 8, "topDecorOffsetY": -12,
            "mapColor": m.map_color,
        }, f"textures/tiles/{m.name}.json"))
    return out
