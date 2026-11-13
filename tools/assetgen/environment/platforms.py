"""One-way platforms atlas (ASSET_SPEC section 7): textures/tiles/platforms.png 48x48.

Rows: 0 ruin_bridge, 1 tech_ledge, 2 root_branch. Columns: left cap, middle (tileable), right cap.
The walkable top is on the first 2 pixel rows of each cell.

Each style is painted into a long strip (cap + 3 periodic middles + cap) so the outline pass
sees real neighbours; cells are then cut from it. The middle texture is 16-periodic, so the
middle cell tiles with itself and with both caps.
"""
from helpers import P, col, new, outline, put, save_png, save_json

TS = 16
SW = TS * 5           # strip width: L | M | M | M | R
O = P.OUTLINE
ST = ["#1d2b3a", "#2a3e4f", "#3b5466", "#577286", "#7d97a8"]
MT = ["#0e131d", "#1a2230", "#2c3846", "#45556a", "#66798f", "#9fb3c8"]
BK = ["#120c18", "#221827", "#362538", "#4d3549", "#6d4d62", "#906f82"]
MOSS = ["#0f3b3a", "#135c50", "#1f8a6a", "#35b583"]
CY = ["#1fb8b0", "#39f0d8", "#8ffff0", "#e6fffb"]
AM = ["#c8741f", "#ffb347", "#ffd98a"]


def end_dist(x):
    """Distance from the strip's outer ends (cap side)."""
    return min(x, SW - 1 - x)


# ---------------------------------------------------------------------------
def ruin_bridge():
    s = new(SW, TS)
    for x in range(SW):
        m = x % TS
        e = end_dist(x)
        left_end = x < TS
        # slab: rows 0-6, rounded/chipped ends
        top = 0
        bot = 6 if e > 1 else 5
        if e == 0:
            continue
        for y in range(top, bot + 1):
            if y == 0:
                c = ST[4]
                if m in (3, 4, 11):
                    c = MOSS[3]
                if m == 4:
                    c = CY[1]
            elif y == 1:
                c = ST[3] if m not in (3, 4, 10, 11, 12) else MOSS[2]
            elif y == 6:
                c = ST[0]
            elif y >= 4:
                c = ST[1]
            else:
                c = ST[2]
            if m == 7 and 1 <= y <= 5:
                c = ST[0]
            elif m == 8 and 2 <= y <= 5:
                c = ST[3]
            if y == 2 and m in (3, 10, 11):
                c = MOSS[1]
            if e == 1:
                c = ST[3] if (left_end and y < 4) else (ST[1] if y < 5 else ST[0])
            s[y, x] = col(c)
        # moss drip from the moss patches
        if m in (10, 11) and e > 2:
            s[3, x] = col(MOSS[1])
        if m == 11 and e > 2:
            s[4, x] = col(MOSS[0])
        # under-truss rows 8-11 (iron), not under the very ends
        if e >= 3:
            s[7, x] = col(MT[1])
            s[8, x] = col(MT[4])
            s[11, x] = col(MT[2])
            k = m % 8
            if k == 0:
                s[9, x] = col(MT[3])
                s[10, x] = col(MT[2])
            elif k in (1, 7):
                s[9, x] = col(MT[3])
            elif k in (2, 3, 5, 6):
                s[10, x] = col(MT[2])
            if m == 4:
                s[8, x] = col(MT[5])   # rivet glint
        elif e == 2:
            # bracket end plates of the truss
            for y in range(7, 12):
                s[y, x] = col(MT[3] if y < 10 else MT[2])
    # hanging vines (periodic, in the middle only and partially on caps)
    # (x in period, length, glowing pod at the end)
    vines = [(2, 7, False), (12, 5, True)]
    leaf_l = {1: MOSS[3], 4: MOSS[2]}
    leaf_r = {2: MOSS[2], 5: MOSS[3]}
    for x in range(SW):
        m = x % TS
        if end_dist(x) < 3:
            continue
        for vx, ln, pod in vines:
            if m != vx:
                continue
            for i in range(ln):
                y = 7 + i
                xx = x + (1 if 3 <= i <= 4 else 0)
                s[y, xx] = col(MOSS[1] if i % 2 else MOSS[2])
                if i in leaf_l:
                    s[y, xx - 1] = col(leaf_l[i])
                if i in leaf_r and i < ln - 1:
                    s[y, xx + 1] = col(leaf_r[i])
            if pod:
                y = 7 + ln
                s[y, x] = col(CY[1])
                s[y, x + 1] = col(CY[0])
                s[y + 1, x] = col(CY[0])
                s[y - 1, x + 1] = col(CY[2])
    outline(s, O)
    return s




def tech_ledge():
    s = new(SW, TS)
    for x in range(SW):
        m = x % TS
        e = end_dist(x)
        if e == 0:
            continue
        rows = {0: MT[5], 1: MT[4], 2: MT[1], 3: None, 4: MT[3], 5: MT[2]}
        bottom = 5 if e > 2 else (4 if e == 2 else 3)
        for y in range(0, bottom + 1):
            c = rows[y]
            if y == 3:
                c = MT[0]
                if 2 <= m <= 5 or 10 <= m <= 13:
                    c = AM[2] if m in (3, 4, 11, 12) else AM[1]
            if e == 1:
                c = MT[4] if y < 2 else (AM[1] if y == 3 else MT[2])
            s[y, x] = col(c)
        if m == 8 and e >= 3:
            s[2, x] = col(MT[3])
        # hanging sensor pods under the middle
        if e >= 4 and m in (7, 8):
            s[6, x] = col(MT[2])
            s[7, x] = col(MT[3] if m == 7 else MT[2])
            s[8, x] = col(AM[1] if m == 7 else AM[0])
    outline(s, O)
    # amber cap beacons (lit side, no outline needed)
    for x in (1, SW - 2):
        put(s, x, 3, AM[2])
    return s


def root_branch():
    s = new(SW, TS)
    for x in range(SW):
        m = x % TS
        e = end_dist(x)
        if e == 0:
            continue
        # tapering thickness at the caps (top stays flat: walkable)
        th = 8 if e >= 10 else max(3, 3 + (e * 5) // 10)
        for y in range(th):
            frac = y / max(1, th - 1)
            if y == 0:
                c = MOSS[3] if (m <= 5 or 9 <= m <= 12) else BK[5]
                if m == 2 or m == 11:
                    c = CY[1]
            elif y == 1:
                c = MOSS[2] if (m <= 4 or 10 <= m <= 12) else BK[5]
            elif frac < 0.45:
                c = BK[4]
            elif frac < 0.75:
                c = BK[3]
            else:
                c = BK[2]
            # grain dashes
            if y == 3 and m in (6, 7, 8, 14):
                c = BK[3]
            if y == 5 and th >= 8 and m in (0, 1, 9, 10, 11):
                c = BK[2]
            # cyan vein
            if th >= 8 and y == 4 and 4 <= m <= 9:
                c = CY[1] if m in (6, 7) else CY[0]
            if y == 2 and m in (1, 2, 11):
                c = MOSS[1]
            s[y, x] = col(c)
    # leaves and a glowing bud hanging under the middle
    for x in range(SW):
        m = x % TS
        if end_dist(x) < 6:
            continue
        if m == 3:
            for i, c in enumerate([MOSS[2], MOSS[1], MOSS[2], MOSS[1]]):
                s[8 + i, x] = col(c)
            s[9, x - 1] = col(MOSS[3])
            s[10, x + 1] = col(MOSS[2])
        if m == 12:
            s[8, x] = col(BK[3])
            s[9, x] = col(BK[3])
            s[10, x] = col(CY[0])
            s[11, x] = col(CY[1])
            s[11, x + 1] = col(CY[2])
            s[12, x] = col(CY[0])
    outline(s, O)
    return s


STYLES = [("ruin_bridge", ruin_bridge), ("tech_ledge", tech_ledge), ("root_branch", root_branch)]


def generate():
    atlas = new(48, 48)
    for row, (name, fn) in enumerate(STYLES):
        strip = fn()
        cells = [strip[:, 0:16], strip[:, 32:48], strip[:, SW - 16:SW]]
        for c, cell in enumerate(cells):
            atlas[row * 16:(row + 1) * 16, c * 16:(c + 1) * 16] = cell
    out = [save_png(atlas, "textures/tiles/platforms.png")]
    out.append(save_json({"texture": "textures/tiles/platforms.png", "tileSize": 16,
                          "styles": {name: i for i, (name, _) in enumerate(STYLES)}},
                         "textures/tiles/platforms.json"))
    return out
