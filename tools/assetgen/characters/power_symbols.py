"""Power-unlock tablets: a floating ancient stone tablet engraved with the
power's glowing symbol, motes orbiting around it (32x32, pivot centre)."""
import math

import numpy as np

import materials as M
from common.palette import AMBER, CYAN, VIOLET, hex_rgba
from raster import Canvas, put

TAU = 2 * math.pi

# 11x11 glyph bitmaps: '#' = symbol, '+' = bright core
GLYPHS = {
    "dash": [
        "...........",
        "#...#......",
        ".#...#.....",
        "..#...#....",
        "...#...#...",
        "....++..+++",
        "...#...#...",
        "..#...#....",
        ".#...#.....",
        "#...#......",
        "...........",
    ],
    "wave": [
        "....###....",
        "......##...",
        ".......##..",
        "..#.....#..",
        ".........#.",
        "+.#.....++.",
        ".........#.",
        "..#.....#..",
        ".......##..",
        "......##...",
        "....###....",
    ],
    "shield": [
        "...#####...",
        "..#.....#..",
        ".#.......#.",
        "#.........#",
        "#....+....#",
        "#...+++...#",
        "#....+....#",
        ".#.......#.",
        "..#.....#..",
        "...#...#...",
        ".....#.....",
    ],
    "impulse": [
        ".....#.....",
        ".#...#...#.",
        "..#.....#..",
        "...........",
        "....+++....",
        "##..+++..##",
        "....+++....",
        "...........",
        "..#.....#..",
        ".#...#...#.",
        ".....#.....",
    ],
    "teleport": [
        "...####....",
        "..#....#...",
        ".#......#..",
        "#...##...#.",
        "#..#..#..#.",
        "#..#.+#..#.",
        "#...#.#..#.",
        ".#.....#...",
        "..#....#...",
        "...####....",
        "...........",
    ],
}

COLORS = {
    "dash": (CYAN[1], CYAN[3], CYAN[0]),
    "wave": (CYAN[2], CYAN[3], CYAN[0]),
    "shield": (CYAN[1], CYAN[3], CYAN[0]),
    "impulse": (AMBER[1], AMBER[2], AMBER[0]),
    "teleport": (VIOLET[1], VIOLET[2], VIOLET[0]),
}


def tablet(name, i):
    bob = [0, 0, -1, -1, -1, -1, 0, 0][i]
    cv = Canvas(32, 32)
    cy = 16 + bob
    cv.part()
    cv.poly([(7.5, cy - 9), (10.5, cy - 12), (21.5, cy - 12), (24.5, cy - 9), (24.5, cy + 9), (21.5, cy + 12),
             (10.5, cy + 12), (7.5, cy + 9)], M.STONE_M, bevel=1.8, base=0.58)
    cv.ellipse((11, cy - 11), 3.5, 1.2, M.MOSS_M, decal=True)
    img = cv.render()
    main, bright, dark = COLORS[name]
    g = GLYPHS[name]
    pulse = 0.5 + 0.5 * math.sin(TAU * i / 8)
    ox, oy = 11, cy - 5
    for y, row in enumerate(g):
        for x, ch in enumerate(row):
            if ch == "#":
                put(img, ox + x, oy + y, bright if pulse > 0.75 and (x + y) % 3 == 0 else main)
            elif ch == "+":
                put(img, ox + x, oy + y, bright)
    # recessed glyph shadow (engraving feel): darker pixel under each stroke
    for y, row in enumerate(g):
        for x, ch in enumerate(row):
            if ch in "#+" and y + 1 < len(g) and g[y + 1][x] == ".":
                yy, xx = oy + y + 1, ox + x
                if img[yy, xx, 3]:
                    img[yy, xx] = dark if pulse > 0.5 else M.STONE_DARK.ramp[3]
    # orbiting motes
    for k in range(3):
        a = TAU * i / 8 + TAU * k / 3
        x = 16 + math.cos(a) * 13.5
        y = cy + math.sin(a) * 4.5
        front = math.sin(a) > 0
        xi, yi = int(x), int(y)
        if front or img[yi, xi, 3] == 0:
            put(img, xi, yi, bright if k == 0 else main)
    return img
