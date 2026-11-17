#include "ui/studio/StudioImage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace cosmic::studio {

std::string hexOf(Px c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

bool parseHex(const std::string& s, Px& out) {
    std::string t = s;
    if (!t.empty() && t[0] == '#') t = t.substr(1);
    if (t.size() != 6) return false;
    unsigned v[3];
    for (int i = 0; i < 3; ++i) {
        v[i] = 0;
        for (int k = 0; k < 2; ++k) {
            const char ch = t[static_cast<size_t>(i * 2 + k)];
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0) return false;
            v[i] = v[i] * 16 + static_cast<unsigned>(d);
        }
    }
    out = {static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]), 255};
    return true;
}

IRect IRect::intersect(const IRect& o) const {
    const int nx = std::max(x, o.x), ny = std::max(y, o.y);
    const int nr = std::min(right(), o.right()), nb = std::min(bottom(), o.bottom());
    if (nr <= nx || nb <= ny) return {};
    return {nx, ny, nr - nx, nb - ny};
}

IRect IRect::unite(const IRect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    const int nx = std::min(x, o.x), ny = std::min(y, o.y);
    return {nx, ny, std::max(right(), o.right()) - nx, std::max(bottom(), o.bottom()) - ny};
}

IRect IRect::between(int x0, int y0, int x1, int y1) {
    const int ax = std::min(x0, x1), ay = std::min(y0, y1);
    return {ax, ay, std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1};
}

// ------------------------------------------------------------------ the image

PixelImage PixelImage::fromRgba(int w, int h, std::vector<uint8_t> rgba) {
    PixelImage p;
    if (w <= 0 || h <= 0 || rgba.size() != static_cast<size_t>(w) * static_cast<size_t>(h) * 4) return p;
    p.w_ = w;
    p.h_ = h;
    p.px_ = std::move(rgba);
    return p;
}

Px PixelImage::get(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return {};
    const uint8_t* p = px_.data() + (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 4;
    return {p[0], p[1], p[2], p[3]};
}

void PixelImage::set(int x, int y, Px c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    uint8_t* p = px_.data() + (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
}

void PixelImage::resize(int w, int h) {
    w_ = std::max(0, w);
    h_ = std::max(0, h);
    px_.assign(static_cast<size_t>(w_) * static_cast<size_t>(h_) * 4, 0);
}

PixelImage PixelImage::crop(const IRect& r) const {
    const IRect c = r.intersect(bounds());
    PixelImage out(c.w, c.h);
    for (int y = 0; y < c.h; ++y)
        std::memcpy(out.px_.data() + static_cast<size_t>(y) * static_cast<size_t>(c.w) * 4,
                    px_.data() + (static_cast<size_t>(c.y + y) * static_cast<size_t>(w_) + static_cast<size_t>(c.x)) * 4, static_cast<size_t>(c.w) * 4);
    return out;
}

void PixelImage::paste(const PixelImage& src, int dx, int dy, const IRect& clip) {
    const IRect dst = IRect{dx, dy, src.w(), src.h()}.intersect(clip).intersect(bounds());
    for (int y = dst.y; y < dst.bottom(); ++y)
        std::memcpy(px_.data() + (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(dst.x)) * 4,
                    src.px_.data() + (static_cast<size_t>(y - dy) * static_cast<size_t>(src.w()) + static_cast<size_t>(dst.x - dx)) * 4,
                    static_cast<size_t>(dst.w) * 4);
}

void PixelImage::stamp(const PixelImage& src, int dx, int dy, const IRect& clip) {
    const IRect dst = IRect{dx, dy, src.w(), src.h()}.intersect(clip).intersect(bounds());
    for (int y = dst.y; y < dst.bottom(); ++y)
        for (int x = dst.x; x < dst.right(); ++x) {
            const Px c = src.get(x - dx, y - dy);
            if (c.a > 0) set(x, y, c);
        }
}

void PixelImage::fill(const IRect& r, Px c) {
    const IRect f = r.intersect(bounds());
    for (int y = f.y; y < f.bottom(); ++y)
        for (int x = f.x; x < f.right(); ++x) set(x, y, c);
}

void PixelImage::flipH(const IRect& r) {
    const IRect f = r.intersect(bounds());
    for (int y = f.y; y < f.bottom(); ++y)
        for (int i = 0; i < f.w / 2; ++i) {
            const Px a = get(f.x + i, y), b = get(f.right() - 1 - i, y);
            set(f.x + i, y, b);
            set(f.right() - 1 - i, y, a);
        }
}

void PixelImage::flipV(const IRect& r) {
    const IRect f = r.intersect(bounds());
    for (int i = 0; i < f.h / 2; ++i)
        for (int x = f.x; x < f.right(); ++x) {
            const Px a = get(x, f.y + i), b = get(x, f.bottom() - 1 - i);
            set(x, f.y + i, b);
            set(x, f.bottom() - 1 - i, a);
        }
}

bool PixelImage::transparent(const IRect& r) const {
    const IRect f = r.intersect(bounds());
    for (int y = f.y; y < f.bottom(); ++y)
        for (int x = f.x; x < f.right(); ++x)
            if (get(x, y).a != 0) return false;
    return true;
}

IRect PixelImage::opaqueBounds(const IRect& r, int threshold) const {
    const IRect f = r.intersect(bounds());
    int x0 = f.right(), y0 = f.bottom(), x1 = f.x - 1, y1 = f.y - 1;
    for (int y = f.y; y < f.bottom(); ++y)
        for (int x = f.x; x < f.right(); ++x)
            if (get(x, y).a > threshold) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    if (x1 < x0) return {};
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

// ------------------------------------------------------------------ the tools

namespace {

/// The points that the symmetry adds to (x, y) inside `clip` (the point itself first).
int mirrored(const Canvas& cv, int x, int y, int (&xs)[4], int (&ys)[4]) {
    const int mx = 2 * cv.clip.x + cv.clip.w - 1 - x, my = 2 * cv.clip.y + cv.clip.h - 1 - y;
    int n = 0;
    xs[n] = x;
    ys[n++] = y;
    if (cv.sym == Symmetry::Vertical || cv.sym == Symmetry::Both) {
        xs[n] = mx;
        ys[n++] = y;
    }
    if (cv.sym == Symmetry::Horizontal || cv.sym == Symmetry::Both) {
        xs[n] = x;
        ys[n++] = my;
    }
    if (cv.sym == Symmetry::Both) {
        xs[n] = mx;
        ys[n++] = my;
    }
    return n;
}

}  // namespace

IRect plot(Canvas& cv, int x, int y, Px c) {
    int xs[4], ys[4];
    const int n = mirrored(cv, x, y, xs, ys);
    IRect dirty;
    for (int i = 0; i < n; ++i) {
        if (!cv.clip.contains(xs[i], ys[i])) continue;
        if (cv.img.get(xs[i], ys[i]) == c) continue;
        cv.img.set(xs[i], ys[i], c);
        dirty = dirty.unite({xs[i], ys[i], 1, 1});
    }
    return dirty;
}

std::vector<std::pair<int, int>> linePoints(int x0, int y0, int x1, int y1) {
    std::vector<std::pair<int, int>> out;
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        out.emplace_back(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return out;
}

IRect drawLine(Canvas& cv, int x0, int y0, int x1, int y1, Px c) {
    IRect dirty;
    for (auto [x, y] : linePoints(x0, y0, x1, y1)) dirty = dirty.unite(plot(cv, x, y, c));
    return dirty;
}

IRect drawRect(Canvas& cv, int x0, int y0, int x1, int y1, Px c, bool filled) {
    const IRect r = IRect::between(x0, y0, x1, y1);
    IRect dirty;
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x)
            if (filled || y == r.y || y == r.bottom() - 1 || x == r.x || x == r.right() - 1) dirty = dirty.unite(plot(cv, x, y, c));
    return dirty;
}

IRect drawEllipse(Canvas& cv, int x0, int y0, int x1, int y1, Px c, bool filled) {
    const IRect r = IRect::between(x0, y0, x1, y1);
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5, rx = r.w * 0.5, ry = r.h * 0.5;
    auto inside = [&](int x, int y) {
        if (!r.contains(x, y)) return false;
        const double u = (x + 0.5 - cx) / rx, v = (y + 0.5 - cy) / ry;
        return u * u + v * v <= 1.0 + 1e-9;
    };
    IRect dirty;
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) {
            if (!inside(x, y)) continue;
            const bool edge = !inside(x - 1, y) || !inside(x + 1, y) || !inside(x, y - 1) || !inside(x, y + 1);
            if (filled || edge) dirty = dirty.unite(plot(cv, x, y, c));
        }
    return dirty;
}

IRect floodFill(Canvas& cv, int sx, int sy, Px c) {
    int xs[4], ys[4];
    const int n = mirrored(cv, sx, sy, xs, ys);
    IRect dirty;
    for (int i = 0; i < n; ++i) {
        const int x0 = xs[i], y0 = ys[i];
        if (!cv.clip.contains(x0, y0)) continue;
        const Px from = cv.img.get(x0, y0);
        if (from == c) continue;
        std::vector<std::pair<int, int>> stack{{x0, y0}};
        while (!stack.empty()) {
            auto [x, y] = stack.back();
            stack.pop_back();
            if (!cv.clip.contains(x, y) || !(cv.img.get(x, y) == from)) continue;
            // The run of this row, then the rows above and below it.
            int l = x, rr = x;
            while (cv.clip.contains(l - 1, y) && cv.img.get(l - 1, y) == from) --l;
            while (cv.clip.contains(rr + 1, y) && cv.img.get(rr + 1, y) == from) ++rr;
            for (int k = l; k <= rr; ++k) {
                cv.img.set(k, y, c);
                if (cv.clip.contains(k, y - 1) && cv.img.get(k, y - 1) == from) stack.emplace_back(k, y - 1);
                if (cv.clip.contains(k, y + 1) && cv.img.get(k, y + 1) == from) stack.emplace_back(k, y + 1);
            }
            dirty = dirty.unite({l, y, rr - l + 1, 1});
        }
    }
    return dirty;
}

int replaceColor(PixelImage& img, const IRect& r, Px from, Px to) {
    const IRect f = r.intersect(img.bounds());
    int n = 0;
    for (int y = f.y; y < f.bottom(); ++y)
        for (int x = f.x; x < f.right(); ++x)
            if (img.get(x, y) == from) {
                img.set(x, y, to);
                ++n;
            }
    return n;
}

std::vector<std::pair<Px, int>> colorCounts(const PixelImage& img, const IRect& r) {
    const IRect f = r.intersect(img.bounds());
    std::unordered_map<uint32_t, int> count;
    for (int y = f.y; y < f.bottom(); ++y)
        for (int x = f.x; x < f.right(); ++x) {
            const Px c = img.get(x, y);
            if (c.a != 0) ++count[c.key()];
        }
    std::vector<std::pair<Px, int>> out;
    out.reserve(count.size());
    for (auto [k, n] : count)
        out.push_back({{static_cast<uint8_t>(k >> 24), static_cast<uint8_t>(k >> 16), static_cast<uint8_t>(k >> 8), static_cast<uint8_t>(k)}, n});
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first.key() < b.first.key(); });
    return out;
}

// ------------------------------------------------------------------ the history

bool ImageHistory::record(const std::string& label, const PixelImage& before, const PixelImage& after, const Json& metaBefore, const Json& metaAfter) {
    ImageEdit e;
    e.label = label;
    e.metaBefore = metaBefore;
    e.metaAfter = metaAfter;
    if (before.w() != after.w() || before.h() != after.h()) {
        e.resized = true;
        e.region = after.bounds();
        e.before = before;
        e.after = after;
    } else {
        // The rectangle where they differ.
        int x0 = after.w(), y0 = after.h(), x1 = -1, y1 = -1;
        const auto& a = before.data();
        const auto& b = after.data();
        const int w = after.w();
        for (int y = 0; y < after.h(); ++y) {
            const size_t row = static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
            if (std::memcmp(a.data() + row, b.data() + row, static_cast<size_t>(w) * 4) == 0) continue;
            for (int x = 0; x < w; ++x)
                if (std::memcmp(a.data() + row + static_cast<size_t>(x) * 4, b.data() + row + static_cast<size_t>(x) * 4, 4) != 0) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                }
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        if (x1 < 0 && metaBefore == metaAfter) return false;
        e.region = x1 < 0 ? IRect{} : IRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
        if (!e.region.empty()) {
            e.before = before.crop(e.region);
            e.after = after.crop(e.region);
        }
    }
    edits_.resize(at_);
    edits_.push_back(std::move(e));
    at_ = edits_.size();
    while (edits_.size() > 1 && bytes() > maxBytes_) {
        edits_.erase(edits_.begin());
        --at_;
    }
    return true;
}

bool ImageHistory::recordRegion(const std::string& label, const IRect& region, const PixelImage& before, const PixelImage& image,
                                const Json& metaBefore, const Json& metaAfter) {
    const IRect r = region.intersect(image.bounds());
    if (before.w() != r.w || before.h() != r.h) return record(label, image, image, metaBefore, metaAfter);
    // The rectangle where they differ, row by row inside the region only.
    int x0 = r.right(), y0 = r.bottom(), x1 = r.x - 1, y1 = r.y - 1;
    for (int y = 0; y < r.h; ++y) {
        const uint8_t* a = before.data().data() + static_cast<size_t>(y) * static_cast<size_t>(r.w) * 4;
        const uint8_t* b = image.data().data() + (static_cast<size_t>(r.y + y) * static_cast<size_t>(image.w()) + static_cast<size_t>(r.x)) * 4;
        if (std::memcmp(a, b, static_cast<size_t>(r.w) * 4) == 0) continue;
        for (int x = 0; x < r.w; ++x)
            if (std::memcmp(a + x * 4, b + x * 4, 4) != 0) {
                x0 = std::min(x0, r.x + x);
                x1 = std::max(x1, r.x + x);
            }
        y0 = std::min(y0, r.y + y);
        y1 = std::max(y1, r.y + y);
    }
    if (x1 < x0 && metaBefore == metaAfter) return false;
    ImageEdit e;
    e.label = label;
    e.metaBefore = metaBefore;
    e.metaAfter = metaAfter;
    e.region = x1 < x0 ? IRect{} : IRect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    if (!e.region.empty()) {
        e.before = before.crop({e.region.x - r.x, e.region.y - r.y, e.region.w, e.region.h});
        e.after = image.crop(e.region);
    }
    edits_.resize(at_);
    edits_.push_back(std::move(e));
    at_ = edits_.size();
    while (edits_.size() > 1 && bytes() > maxBytes_) {
        edits_.erase(edits_.begin());
        --at_;
    }
    return true;
}

const ImageEdit* ImageHistory::undo(PixelImage& img) {
    if (!canUndo()) return nullptr;
    const ImageEdit& e = edits_[--at_];
    if (e.resized) img = e.before;
    else if (!e.region.empty()) img.paste(e.before, e.region.x, e.region.y);
    return &e;
}

const ImageEdit* ImageHistory::redo(PixelImage& img) {
    if (!canRedo()) return nullptr;
    const ImageEdit& e = edits_[at_++];
    if (e.resized) img = e.after;
    else if (!e.region.empty()) img.paste(e.after, e.region.x, e.region.y);
    return &e;
}

const std::string& ImageHistory::undoLabel() const {
    static const std::string none;
    return canUndo() ? edits_[at_ - 1].label : none;
}

const std::string& ImageHistory::redoLabel() const {
    static const std::string none;
    return canRedo() ? edits_[at_].label : none;
}

size_t ImageHistory::bytes() const {
    size_t n = 0;
    for (const ImageEdit& e : edits_) n += e.bytes();
    return n;
}

}  // namespace cosmic::studio
