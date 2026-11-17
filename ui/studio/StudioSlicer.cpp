#include "ui/studio/StudioSlicer.h"

#include <algorithm>
#include <cmath>

namespace cosmic::studio {

std::vector<IRect> detectZones(const PixelImage& img, const SliceOptions& opt) {
    std::vector<IRect> out;
    if (img.empty()) return out;
    const int w = img.w(), h = img.h();
    // One pass over the pixels: each row cut in runs of solid pixels; a run joins (union-find)
    // the runs of the row above it touches, diagonals included. Each group keeps its box and
    // its count, merged when two groups join.
    struct Group {
        int parent;
        int x0, y0, x1, y1, n;
    };
    std::vector<Group> groups;
    auto root = [&groups](int g) {
        while (groups[static_cast<size_t>(g)].parent != g) {
            groups[static_cast<size_t>(g)].parent = groups[static_cast<size_t>(groups[static_cast<size_t>(g)].parent)].parent;
            g = groups[static_cast<size_t>(g)].parent;
        }
        return g;
    };
    auto join = [&](int a, int b) {
        a = root(a);
        b = root(b);
        if (a == b) return;
        Group& A = groups[static_cast<size_t>(a)];
        const Group& B = groups[static_cast<size_t>(b)];
        A.x0 = std::min(A.x0, B.x0), A.y0 = std::min(A.y0, B.y0), A.x1 = std::max(A.x1, B.x1), A.y1 = std::max(A.y1, B.y1), A.n += B.n;
        groups[static_cast<size_t>(b)].parent = a;
    };
    struct Run {
        int x0, x1, group;
    };
    std::vector<Run> above, here;
    const uint8_t* px = img.data().data();
    const int threshold = opt.alphaThreshold;
    for (int y = 0; y < h; ++y) {
        here.clear();
        const uint8_t* row = px + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
        size_t k = 0;  // the first run above that may touch the current one
        for (int x = 0; x < w;) {
            if (row[x * 4 + 3] <= threshold) {
                ++x;
                continue;
            }
            const int x0 = x;
            while (x < w && row[x * 4 + 3] > threshold) ++x;
            const int x1 = x - 1;
            const int g = static_cast<int>(groups.size());
            groups.push_back({g, x0, y, x1, y, x1 - x0 + 1});
            // Runs above from x0 - 1 to x1 + 1 (8-connected).
            while (k < above.size() && above[k].x1 < x0 - 1) ++k;
            for (size_t j = k; j < above.size() && above[j].x0 <= x1 + 1; ++j) join(g, above[j].group);
            here.push_back({x0, x1, g});
        }
        std::swap(above, here);
    }
    // The groups found.
    std::vector<IRect> boxes;
    std::vector<int> counts;
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].parent == static_cast<int>(i)) {
            const Group& g = groups[i];
            boxes.push_back({g.x0, g.y0, g.x1 - g.x0 + 1, g.y1 - g.y0 + 1});
            counts.push_back(g.n);
        }
    // Groups closer than the margin are one zone (a sword apart from its hand, a hair...): the
    // boxes sorted by their left edge, each compared only with those that can still be near.
    const int m = std::max(0, opt.margin);
    std::vector<size_t> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    bool merged = true;
    while (merged) {
        merged = false;
        std::sort(order.begin(), order.end(), [&boxes](size_t a, size_t b) { return boxes[a].x < boxes[b].x; });
        for (size_t i = 0; i < order.size(); ++i) {
            IRect& a = boxes[order[i]];
            if (a.empty()) continue;
            for (size_t j = i + 1; j < order.size() && boxes[order[j]].x <= a.right() + m; ++j) {
                IRect& b = boxes[order[j]];
                if (b.empty()) continue;
                const int gapX = std::max(a.x, b.x) - std::min(a.right(), b.right());
                const int gapY = std::max(a.y, b.y) - std::min(a.bottom(), b.bottom());
                if (gapX > m || gapY > m) continue;
                a = a.unite(b);
                counts[order[i]] += counts[order[j]];
                b = {};
                merged = true;
            }
        }
    }
    for (size_t i = 0; i < boxes.size(); ++i)
        if (!boxes[i].empty() && counts[i] >= opt.minPixels) out.push_back(boxes[i]);
    std::sort(out.begin(), out.end(), [](const IRect& a, const IRect& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return out;
}

std::vector<std::vector<IRect>> groupRows(std::vector<IRect> zones) {
    std::sort(zones.begin(), zones.end(), [](const IRect& a, const IRect& b) {
        const int ca = a.y * 2 + a.h, cb = b.y * 2 + b.h;  // twice the centre
        return ca != cb ? ca < cb : a.x < b.x;
    });
    struct Row {
        int top, bottom;
        std::vector<IRect> zones;
    };
    std::vector<Row> rows;
    for (const IRect& z : zones) {
        Row* best = nullptr;
        int bestOverlap = 0;
        for (Row& r : rows) {
            const int overlap = std::min(r.bottom, z.bottom()) - std::max(r.top, z.y);
            const int need = (std::min(z.h, r.bottom - r.top) + 1) / 2;
            if (overlap >= need && overlap > bestOverlap) {
                best = &r;
                bestOverlap = overlap;
            }
        }
        if (!best) {
            rows.push_back({z.y, z.bottom(), {z}});
            continue;
        }
        best->zones.push_back(z);
        best->top = std::min(best->top, z.y);
        best->bottom = std::max(best->bottom, z.bottom());
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.top < b.top; });
    std::vector<std::vector<IRect>> out;
    for (Row& r : rows) {
        std::sort(r.zones.begin(), r.zones.end(), [](const IRect& a, const IRect& b) { return a.x < b.x; });
        out.push_back(std::move(r.zones));
    }
    return out;
}

std::vector<std::vector<IRect>> gridRows(const PixelImage& img, const GridSpec& g, bool skipEmpty, int alphaThreshold) {
    std::vector<std::vector<IRect>> out;
    if (g.cellW <= 0 || g.cellH <= 0) return out;
    for (int y = g.offsetY; y + g.cellH <= img.h(); y += g.cellH + std::max(0, g.gapY)) {
        std::vector<IRect> row;
        for (int x = g.offsetX; x + g.cellW <= img.w(); x += g.cellW + std::max(0, g.gapX)) {
            const IRect cell{x, y, g.cellW, g.cellH};
            if (skipEmpty && img.opaqueBounds(cell, alphaThreshold).empty()) continue;
            row.push_back(cell);
        }
        if (!row.empty()) out.push_back(std::move(row));
        if (g.cellH + std::max(0, g.gapY) <= 0) break;
    }
    return out;
}

PixelImage keyOutBackground(const PixelImage& img, Px* background) {
    PixelImage out = img;
    if (img.empty()) return out;
    const Px bg = img.get(0, 0);
    if (background) *background = bg;
    replaceColor(out, out.bounds(), bg, Px{});
    return out;
}

PixelImage fitZone(const PixelImage& src, const IRect& zone, int frameW, int frameH, Anchor anchor, int scalePercent, int alphaThreshold, bool* shrunk) {
    if (shrunk) *shrunk = false;
    PixelImage frame(frameW, frameH);
    if (zone.w == frameW && zone.h == frameH && scalePercent == 100) {
        frame.paste(src.crop(zone), 0, 0);
        return frame;
    }
    const IRect content = src.opaqueBounds(zone, alphaThreshold);
    if (content.empty()) return frame;
    double k = std::max(1, scalePercent) / 100.0;
    if (content.w * k > frameW || content.h * k > frameH) {
        k = std::min(static_cast<double>(frameW) / content.w, static_cast<double>(frameH) / content.h);
        if (shrunk) *shrunk = true;
    }
    const int w = std::max(1, std::min(frameW, static_cast<int>(std::floor(content.w * k + 1e-9))));
    const int h = std::max(1, std::min(frameH, static_cast<int>(std::floor(content.h * k + 1e-9))));
    const int ox = (frameW - w) / 2;
    const int oy = anchor == Anchor::BottomCenter ? frameH - h : (frameH - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // The source pixel at the centre of this one (nearest pixel: no blur in pixel art).
            const int sx = content.x + std::min(content.w - 1, static_cast<int>((x + 0.5) * content.w / w));
            const int sy = content.y + std::min(content.h - 1, static_cast<int>((y + 0.5) * content.h / h));
            const Px c = src.get(sx, sy);
            if (c.a > 0) frame.set(ox + x, oy + y, c);
        }
    return frame;
}

std::string importProblem(const PixelImage& img, int maxSide) {
    if (img.empty()) return "Image illisible.";
    if (img.w() > maxSide || img.h() > maxSide)
        return "Image trop grande : " + std::to_string(img.w()) + " × " + std::to_string(img.h()) + " pixels (" + std::to_string(maxSide) + " × " +
               std::to_string(maxSide) + " au plus).";
    if (img.opaqueBounds(img.bounds()).empty()) return "Image entièrement transparente : rien à découper.";
    return "";
}

}  // namespace cosmic::studio
