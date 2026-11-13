"""Hand-authored pixel sprites described with character maps.

A sprite is a list of equal-length strings plus a legend {char: colour}. '.' and ' ' are
transparent. This keeps small, important shapes crisp and fully art-directed.
"""
import numpy as np

from helpers import col, new


def from_map(rows, legend, w=None, h=None):
    rows = [r for r in rows]
    hh = len(rows)
    ww = max(len(r) for r in rows)
    img = new(w or ww, h or hh)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in ".  " or ch not in legend:
                continue
            c = legend[ch]
            if c is None:
                continue
            img[y, x] = col(c)
    return img


def stamp(dst, rows, legend, ox, oy):
    """Draw a char-map directly into dst (no blending)."""
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in ". " or ch not in legend or legend[ch] is None:
                continue
            xx, yy = ox + x, oy + y
            if 0 <= xx < dst.shape[1] and 0 <= yy < dst.shape[0]:
                dst[yy, xx] = col(legend[ch])
    return dst


def mirror_rows(rows):
    return [r[::-1] for r in rows]


def as_mask(rows, chars="#"):
    return np.array([[ch in chars for ch in r] for r in rows], bool)
