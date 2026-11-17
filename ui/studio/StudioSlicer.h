#pragma once

// 2.61 Studio de Créations › Importer une image: how a sheet is cut into frames — the zones
// found automatically (the groups of pixels that are not transparent), a regular grid, or
// rectangles drawn by hand — then grouped in rows (one row = one animation of the game) and
// each zone placed in a frame of the creation (anchored under the feet or centred, shrunk when
// it is larger). No SDL here (tests/test_studio.cpp).

#include <string>
#include <vector>

#include "ui/studio/StudioImage.h"

namespace cosmic::studio {

struct SliceOptions {
    int alphaThreshold = 20;  // a pixel counts when its alpha is above this (8 % of 255)
    int margin = 1;           // zones closer than this (pixels) are one zone
    int minPixels = 3;        // smaller groups of pixels are dust, left out
};

/// The zones of the pixels that are not transparent (8-connected), merged when closer than
/// `margin`, from top to bottom and left to right.
std::vector<IRect> detectZones(const PixelImage& img, const SliceOptions& opt = {});
/// The zones grouped in rows (a zone joins a row when they overlap vertically by half the
/// smaller height), the rows from top to bottom, each from left to right.
std::vector<std::vector<IRect>> groupRows(std::vector<IRect> zones);

struct GridSpec {
    int cellW = 32, cellH = 32;
    int offsetX = 0, offsetY = 0;
    int gapX = 0, gapY = 0;
};
/// The cells of a regular grid inside the image, by rows (empty cells left out when
/// `skipEmpty`; rows left with no cell are dropped).
std::vector<std::vector<IRect>> gridRows(const PixelImage& img, const GridSpec& g, bool skipEmpty, int alphaThreshold = 20);

/// The same colour as the top-left pixel becomes transparent (sheets on a plain background).
PixelImage keyOutBackground(const PixelImage& img, Px* background = nullptr);

enum class Anchor { BottomCenter, Center };
/// The zone `zone` of `src` in a frame of `frameW` × `frameH`: copied as it is when the zone
/// has the frame's size, otherwise its pixels (trimmed of their transparent edges) scaled by
/// `scalePercent` (nearest pixel), shrunk to fit when larger (`shrunk` true), and anchored.
PixelImage fitZone(const PixelImage& src, const IRect& zone, int frameW, int frameH, Anchor anchor, int scalePercent = 100,
                   int alphaThreshold = 20, bool* shrunk = nullptr);

/// Why an image cannot be imported ("" = it can): its size (`maxSide` pixels on each side at
/// most) and whether anything in it is visible.
std::string importProblem(const PixelImage& img, int maxSide = 2048);

}  // namespace cosmic::studio
