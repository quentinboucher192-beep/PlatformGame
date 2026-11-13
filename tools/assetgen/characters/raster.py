"""Small 1x pixel-art rasteriser used by every character/object generator.

Shapes are drawn into a *material canvas*: every pixel stores a material, a
continuous light intensity (0 = darkest, 1 = brightest) and the id of the
"part" that drew it.  `Canvas.render()` then

  * darkens the pixels of a part that touch a part drawn in front of it
    (selective inner contour lines, so overlapping limbs stay readable),
  * quantises the intensity into the material's colour ramp (hard pixels,
    no anti-aliasing),
  * adds a 1px outline around the silhouette.

Lighting is top-left (x right, y down, z towards the viewer).
"""
import math

import numpy as np

from common.palette import OUTLINE, TRANSPARENT, hex_rgba

LIGHT3 = np.array([-0.55, -0.68, 0.62])
LIGHT3 = LIGHT3 / np.linalg.norm(LIGHT3)
LIGHT2 = np.array([-0.62, -0.78])


def rgba(h, a=255):
    return hex_rgba(h, a)


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def rot(v, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def mul(a, k):
    return (a[0] * k, a[1] * k)


def length(v):
    return math.hypot(v[0], v[1])


def ik2(root, target, l1, l2, bend=1.0):
    """Two-bone IK. Returns the middle joint. `bend` (+1/-1) picks the side
    (+1 = joint pushed towards +x when the limb points down)."""
    dx, dy = target[0] - root[0], target[1] - root[1]
    d = math.hypot(dx, dy)
    d = max(1e-4, min(d, l1 + l2 - 1e-4))
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = math.sqrt(max(0.0, l1 * l1 - a * a))
    ux, uy = dx / d, dy / d
    mx, my = root[0] + ux * a, root[1] + uy * a
    # perpendicular (rotate u by -90deg => (uy, -ux)); for a limb pointing down
    # (u = (0,1)) this gives (1, 0) i.e. +x.
    px, py = uy, -ux
    return (mx + px * h * bend, my + py * h * bend)


class Mat:
    """A material: colour ramp (light -> dark) + outline behaviour."""

    def __init__(self, ramp, outline=OUTLINE, emissive=False, thresholds=None, name=""):
        self.ramp = [tuple(c) for c in ramp]
        self.outline = outline
        self.emissive = emissive
        self.name = name
        n = len(self.ramp)
        if thresholds is None:
            if n == 1:
                thresholds = []
            elif n == 2:
                thresholds = [0.55]
            elif n == 3:
                thresholds = [0.72, 0.42]
            elif n == 4:
                thresholds = [0.86, 0.62, 0.36]
            else:
                thresholds = [1.0 - (i + 1) / n for i in range(n - 1)]
        self.thresholds = thresholds

    def quantise(self, inten):
        idx = np.zeros(inten.shape, np.int32)
        for t in self.thresholds:
            idx += (inten < t).astype(np.int32)
        return idx


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.mat = np.full((h, w), -1, np.int32)
        self.inten = np.zeros((h, w), np.float32)
        self.pid = np.zeros((h, w), np.int32)
        self.mats = []
        self._mat_ids = {}
        self.part_line = {0: False}
        self.cur = 0
        ys, xs = np.mgrid[0:h, 0:w]
        self.px = xs.astype(np.float32) + 0.5
        self.py = ys.astype(np.float32) + 0.5

    # ------------------------------------------------------------------ parts
    def part(self, line=True):
        """Start a new part. `line` = True/False or an int tone shift applied to
        the pixels of earlier parts that touch this one (inner contour)."""
        self.cur += 1
        self.part_line[self.cur] = (2 if line is True else int(line or 0))
        return self.cur

    def _mid(self, mat):
        k = id(mat)
        if k not in self._mat_ids:
            self._mat_ids[k] = len(self.mats)
            self.mats.append(mat)
        return self._mat_ids[k]

    def occupied(self, parts=None):
        if parts is None:
            return self.mat >= 0
        return np.isin(self.pid, list(parts)) & (self.mat >= 0)

    # ------------------------------------------------------------------ write
    def put(self, mask, inten, mat, bias=0.0, decal=False, clip=None, within=None):
        if clip is not None:
            mask = mask & clip
        if within is not None:
            mask = mask & within(self.px, self.py)
        if decal:
            mask = mask & (self.mat >= 0)
        if not mask.any():
            return mask
        m = self._mid(mat)
        self.mat[mask] = m
        if np.isscalar(inten):
            self.inten[mask] = np.clip(inten + bias, 0, 1)
        else:
            self.inten[mask] = np.clip(inten[mask] + bias, 0, 1)
        if not decal:
            self.pid[mask] = self.cur
        return mask

    def erase(self, mask):
        self.mat[mask] = -1
        self.pid[mask] = 0

    # -------------------------------------------------------------- primitives
    @staticmethod
    def light_from_normal(nx, ny, nz):
        d = nx * LIGHT3[0] + ny * LIGHT3[1] + nz * LIGHT3[2]
        return 0.5 + 0.5 * d

    def ellipse(self, c, rx, ry, mat, ang=0.0, flat=None, bias=0.0, **kw):
        dx, dy = self.px - c[0], self.py - c[1]
        ca, sa = math.cos(ang), math.sin(ang)
        u = (dx * ca + dy * sa) / max(rx, 1e-3)
        v = (-dx * sa + dy * ca) / max(ry, 1e-3)
        r2 = u * u + v * v
        mask = r2 <= 1.0
        if flat is not None:
            inten = flat
        else:
            nx = u * ca - v * sa
            ny = u * sa + v * ca
            nz = np.sqrt(np.clip(1 - r2, 0, 1))
            inten = self.light_from_normal(nx, ny, nz)
        return self.put(mask, inten, mat, bias, **kw)

    def disc(self, c, r, mat, **kw):
        return self.ellipse(c, r, r, mat, **kw)

    def capsule(self, p0, p1, r0, r1=None, mat=None, flat=None, bias=0.0, **kw):
        if r1 is None:
            r1 = r0
        dx, dy = p1[0] - p0[0], p1[1] - p0[1]
        ll = dx * dx + dy * dy
        qx, qy = self.px - p0[0], self.py - p0[1]
        if ll < 1e-6:
            t = np.zeros_like(qx)
        else:
            t = np.clip((qx * dx + qy * dy) / ll, 0, 1)
        cx, cy = p0[0] + t * dx, p0[1] + t * dy
        r = r0 + (r1 - r0) * t
        ex, ey = self.px - cx, self.py - cy
        d = np.sqrt(ex * ex + ey * ey)
        mask = d <= r
        if flat is not None:
            inten = flat
        else:
            rr = np.maximum(r, 1e-3)
            nx, ny = ex / rr, ey / rr
            nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
            inten = self.light_from_normal(nx, ny, nz)
        return self.put(mask, inten, mat, bias, **kw)

    def chain(self, pts, radii, mat, **kw):
        """A tapered polyline (scarves, vines, tails)."""
        out = np.zeros((self.h, self.w), bool)
        for i in range(len(pts) - 1):
            m = self.capsule(pts[i], pts[i + 1], radii[i], radii[i + 1], mat, **kw)
            if m is not None:
                out |= m
        return out

    def poly_mask(self, pts):
        x, y = self.px, self.py
        inside = np.zeros((self.h, self.w), bool)
        n = len(pts)
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            if y0 == y1:
                continue
            cond = (y0 > y) != (y1 > y)
            xint = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            inside ^= cond & (x < xint)
        return inside

    def poly(self, pts, mat, shade="bevel", bevel=1.2, base=0.62, flat=None, bias=0.0, **kw):
        mask = self.poly_mask(pts)
        if flat is not None:
            return self.put(mask, flat, mat, bias, **kw)
        if shade == "grad":
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            gx = (self.px - x0) / max(1e-3, x1 - x0)
            gy = (self.py - y0) / max(1e-3, y1 - y0)
            inten = base + 0.25 - 0.3 * (0.45 * gx + 0.55 * gy)
            return self.put(mask, inten, mat, bias, **kw)
        # bevel: nearest edge decides
        best_d = np.full((self.h, self.w), 1e9, np.float32)
        best_l = np.zeros((self.h, self.w), np.float32)
        n = len(pts)
        # orientation (signed area) to get outward normals
        area = 0.0
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            area += x0 * y1 - x1 * y0
        sgn = 1.0 if area > 0 else -1.0
        for i in range(n):
            a = pts[i]
            b = pts[(i + 1) % n]
            ex, ey = b[0] - a[0], b[1] - a[1]
            ll = ex * ex + ey * ey
            if ll < 1e-6:
                continue
            t = np.clip(((self.px - a[0]) * ex + (self.py - a[1]) * ey) / ll, 0, 1)
            cx, cy = a[0] + t * ex, a[1] + t * ey
            d = np.hypot(self.px - cx, self.py - cy)
            el = math.sqrt(ll)
            # outward normal for CCW (in y-down coords area>0 means clockwise visually)
            nx, ny = ey / el * sgn, -ex / el * sgn
            lit = nx * LIGHT2[0] + ny * LIGHT2[1]
            upd = d < best_d
            best_d = np.where(upd, d, best_d)
            best_l = np.where(upd, lit, best_l)
        edge = best_d < bevel
        inten = np.where(edge, base + 0.32 * best_l, base)
        return self.put(mask, inten, mat, bias, **kw)

    def rect(self, x0, y0, x1, y1, mat, **kw):
        """Axis aligned rect covering pixel columns x0..x1-1, rows y0..y1-1."""
        return self.poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], mat, **kw)

    def px_set(self, x, y, mat, inten=0.6, decal=False):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            if decal and self.mat[y, x] < 0:
                return
            self.mat[y, x] = self._mid(mat)
            self.inten[y, x] = inten
            if not decal:
                self.pid[y, x] = self.cur

    def line(self, p0, p1, mat, inten=0.6, decal=False):
        """1px Bresenham line."""
        x0, y0 = int(math.floor(p0[0])), int(math.floor(p0[1]))
        x1, y1 = int(math.floor(p1[0])), int(math.floor(p1[1]))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.px_set(x0, y0, mat, inten, decal)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    # ------------------------------------------------------------------ render
    def render(self, outline=True, inner=True, outline_diag=False, inner_shift=2):
        h, w = self.h, self.w
        occ = self.mat >= 0
        inten = self.inten.copy()
        dark = np.zeros((h, w), np.int32)
        if inner:
            emis = np.zeros((h, w), bool)
            for i, m in enumerate(self.mats):
                if m.emissive:
                    emis |= self.mat == i
            lineable = np.zeros((h, w), np.int32)
            for pid, fl in self.part_line.items():
                if fl:
                    lineable[self.pid == pid] = fl
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                nb_part = np.roll(np.roll(self.pid, dy, 0), dx, 1)
                nb_line = np.roll(np.roll(lineable, dy, 0), dx, 1)
                nb_occ = np.roll(np.roll(occ, dy, 0), dx, 1)
                nb_emis = np.roll(np.roll(emis, dy, 0), dx, 1)
                cond = occ & nb_occ & (nb_line > 0) & (nb_part > self.pid) & ~emis & ~nb_emis
                # kill wrap-around
                if dy == 1:
                    cond[0, :] = False
                if dy == -1:
                    cond[-1, :] = False
                if dx == 1:
                    cond[:, 0] = False
                if dx == -1:
                    cond[:, -1] = False
                dark = np.maximum(dark, np.where(cond, nb_line, 0))
        img = np.zeros((h, w, 4), np.uint8)
        for i, m in enumerate(self.mats):
            sel = self.mat == i
            if not sel.any():
                continue
            idx = m.quantise(inten) + dark
            ramp = np.array(m.ramp, np.uint8)
            img[sel] = ramp[np.clip(idx[sel], 0, len(m.ramp) - 1)]
        if outline:
            outline_pass(img, self, diag=outline_diag)
        return img


def outline_pass(img, cv=None, color=OUTLINE, diag=False):
    """Adds a 1px outline around opaque pixels. Uses each material's outline
    colour when a canvas is given (None = no outline for that material)."""
    h, w = img.shape[:2]
    alpha = img[:, :, 3] > 0
    if cv is not None:
        oc = np.zeros((h, w, 4), np.uint8)
        has = np.zeros((h, w), bool)
        for i, m in enumerate(cv.mats):
            if m.outline is None:
                continue
            oc[(cv.mat == i)] = m.outline
            has |= cv.mat == i
    else:
        oc = np.zeros((h, w, 4), np.uint8)
        oc[alpha] = color
        has = alpha
    out = img.copy()
    dirs = [(0, 1), (0, -1), (1, 0), (-1, 0)]
    if diag:
        dirs += [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    filled = alpha.copy()
    for dy, dx in dirs:
        src_has = np.zeros((h, w), bool)
        src_col = np.zeros((h, w, 4), np.uint8)
        ys0, ys1 = max(0, dy), h + min(0, dy)
        xs0, xs1 = max(0, dx), w + min(0, dx)
        src_has[ys0:ys1, xs0:xs1] = has[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
        src_col[ys0:ys1, xs0:xs1] = oc[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
        sel = src_has & ~filled
        out[sel] = src_col[sel]
        filled |= sel
    img[:] = out
    return img


def outline_rgba(img, color=OUTLINE, diag=False):
    return outline_pass(img, None, color, diag)


# ---------------------------------------------------------------- RGBA helpers
def blank(w, h):
    return np.zeros((h, w, 4), np.uint8)


def paste(dst, src, x, y):
    """Alpha-over paste of src onto dst at (x, y) (clipped)."""
    h, w = src.shape[:2]
    H, W = dst.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(W, x + w), min(H, y + h)
    if x0 >= x1 or y0 >= y1:
        return dst
    s = src[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32)
    d = dst[y0:y1, x0:x1].astype(np.float32)
    sa = s[:, :, 3:4] / 255.0
    da = d[:, :, 3:4] / 255.0
    oa = sa + da * (1 - sa)
    oc = np.where(oa > 0, (s[:, :, :3] * sa + d[:, :, :3] * da * (1 - sa)) / np.maximum(oa, 1e-6), 0)
    dst[y0:y1, x0:x1, :3] = np.clip(oc + 0.5, 0, 255).astype(np.uint8)
    dst[y0:y1, x0:x1, 3] = np.clip(oa[:, :, 0] * 255 + 0.5, 0, 255).astype(np.uint8)
    return dst


def put(img, x, y, c):
    x, y = int(x), int(y)
    if 0 <= y < img.shape[0] and 0 <= x < img.shape[1]:
        img[y, x] = c


def shift(img, dx, dy):
    out = np.zeros_like(img)
    h, w = img.shape[:2]
    ys0, ys1 = max(0, dy), h + min(0, dy)
    xs0, xs1 = max(0, dx), w + min(0, dx)
    if ys0 < ys1 and xs0 < xs1:
        out[ys0:ys1, xs0:xs1] = img[ys0 - dy:ys1 - dy, xs0 - dx:xs1 - dx]
    return out


def flip_h(img):
    return img[:, ::-1].copy()


def recolor(img, mapping):
    """Replace exact RGBA colours."""
    out = img.copy()
    for a, b in mapping.items():
        sel = np.all(img == np.array(a, np.uint8), axis=2)
        out[sel] = b
    return out


def tint_toward(img, color, t, mask=None):
    out = img.copy()
    sel = img[:, :, 3] > 0
    if mask is not None:
        sel &= mask
    c = np.array(color[:3], np.float32)
    out[sel, :3] = np.clip(img[sel, :3].astype(np.float32) * (1 - t) + c * t + 0.5, 0, 255).astype(np.uint8)
    return out


def nearest_palette(img, palette):
    """Snap every opaque pixel to the closest palette colour (keeps alpha)."""
    pal = np.array([p[:3] for p in palette], np.float32)
    out = img.copy()
    sel = img[:, :, 3] > 0
    px = img[sel][:, :3].astype(np.float32)
    d = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(-1)
    out_px = pal[np.argmin(d, axis=1)].astype(np.uint8)
    tmp = out[sel]
    tmp[:, :3] = out_px
    out[sel] = tmp
    return out


__all__ = [
    "Mat", "Canvas", "outline_pass", "outline_rgba", "blank", "paste", "put", "shift",
    "flip_h", "recolor", "tint_toward", "nearest_palette", "rgba", "lerp", "smooth", "rot",
    "add", "sub", "mul", "length", "ik2", "OUTLINE", "TRANSPARENT",
]


class TCanvas(Canvas):
    """Canvas whose primitives go through a 2D transform:
    scale (sx, sy) about `center` -> rotate by `rot` about `center` -> translate `offset`.
    Lets a whole creature be tilted, squashed or moved per frame."""

    def __init__(self, w, h, rot=0.0, center=(0.0, 0.0), offset=(0.0, 0.0), scale=(1.0, 1.0)):
        super().__init__(w, h)
        self.t_rot = rot
        self.t_c = center
        self.t_off = offset
        self.t_s = scale

    def T(self, p):
        x = self.t_c[0] + (p[0] - self.t_c[0]) * self.t_s[0]
        y = self.t_c[1] + (p[1] - self.t_c[1]) * self.t_s[1]
        if self.t_rot:
            c, s = math.cos(self.t_rot), math.sin(self.t_rot)
            dx, dy = x - self.t_c[0], y - self.t_c[1]
            x, y = self.t_c[0] + dx * c - dy * s, self.t_c[1] + dx * s + dy * c
        return (x + self.t_off[0], y + self.t_off[1])

    def _r(self, r):
        return r * math.sqrt(abs(self.t_s[0] * self.t_s[1]))

    def tellipse(self, c, rx, ry, mat, ang=0.0, **kw):
        return self.ellipse(self.T(c), rx * abs(self.t_s[0]), ry * abs(self.t_s[1]), mat, ang=ang + self.t_rot, **kw)

    def tdisc(self, c, r, mat, **kw):
        return self.tellipse(c, r, r, mat, **kw)

    def tcapsule(self, p0, p1, r0, r1=None, mat=None, **kw):
        if r1 is None:
            r1 = r0
        return self.capsule(self.T(p0), self.T(p1), self._r(r0), self._r(r1), mat, **kw)

    def tchain(self, pts, radii, mat, **kw):
        return self.chain([self.T(p) for p in pts], [self._r(r) for r in radii], mat, **kw)

    def tpoly(self, pts, mat, **kw):
        return self.poly([self.T(p) for p in pts], mat, **kw)

    def tline(self, p0, p1, mat, inten=0.6, decal=False):
        return self.line(self.T(p0), self.T(p1), mat, inten, decal)

    def tpx(self, p, mat, inten=0.6, decal=False):
        q = self.T((p[0] + 0.5, p[1] + 0.5))
        return self.px_set(math.floor(q[0]), math.floor(q[1]), mat, inten, decal)
