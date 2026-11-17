#pragma once

// 2.61 Studio de Créations: the pixels being drawn (RGBA8), the drawing tools that change them
// (pencil, eraser, fill, line, rectangle, ellipse, mirrors, symmetry, recolour) and the history
// of the changes (undo / redo by the rectangle each change touched). No SDL here: everything is
// tested in tests/test_studio.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic::studio {

/// One pixel (straight alpha).
struct Px {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Px&) const = default;
    uint32_t key() const { return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | a; }
    static Px rgb(uint8_t r, uint8_t g, uint8_t b) { return {r, g, b, 255}; }
};

/// "#rrggbb" (alpha left out), and back (opaque); nullopt-like {false} on a malformed code.
std::string hexOf(Px c);
bool parseHex(const std::string& s, Px& out);

/// A rectangle of whole pixels.
struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    IRect intersect(const IRect& o) const;
    /// The smallest rectangle holding both (an empty one is ignored).
    IRect unite(const IRect& o) const;
    bool operator==(const IRect&) const = default;
    /// The rectangle between two corners (inclusive), whichever order they come in.
    static IRect between(int x0, int y0, int x1, int y1);
};

/// RGBA8 pixels, rows one after the other.
class PixelImage {
public:
    PixelImage() = default;
    PixelImage(int w, int h) { resize(w, h); }
    static PixelImage fromRgba(int w, int h, std::vector<uint8_t> rgba);

    int w() const { return w_; }
    int h() const { return h_; }
    bool empty() const { return w_ <= 0 || h_ <= 0; }
    IRect bounds() const { return {0, 0, w_, h_}; }
    const std::vector<uint8_t>& data() const { return px_; }
    std::vector<uint8_t>& data() { return px_; }
    /// Transparent black outside.
    Px get(int x, int y) const;
    void set(int x, int y, Px c);
    /// Every pixel transparent, the size changed.
    void resize(int w, int h);
    PixelImage crop(const IRect& r) const;
    /// `src` copied at (dx, dy), its pixels replacing these (transparent ones too) inside `clip`.
    void paste(const PixelImage& src, int dx, int dy, const IRect& clip);
    void paste(const PixelImage& src, int dx, int dy) { paste(src, dx, dy, bounds()); }
    /// `src` drawn over at (dx, dy): only its non-transparent pixels, inside `clip`.
    void stamp(const PixelImage& src, int dx, int dy, const IRect& clip);
    void fill(const IRect& r, Px c);
    void flipH(const IRect& r);
    void flipV(const IRect& r);
    /// Whether every pixel of `r` is transparent.
    bool transparent(const IRect& r) const;
    /// The smallest rectangle of `r` holding its pixels whose alpha is above `threshold` (empty: none).
    IRect opaqueBounds(const IRect& r, int threshold = 0) const;
    bool operator==(const PixelImage& o) const { return w_ == o.w_ && h_ == o.h_ && px_ == o.px_; }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> px_;
};

/// Drawing mirrored as it is drawn, inside the frame being edited.
enum class Symmetry { None, Vertical, Horizontal, Both };

/// What a tool draws into: the image, the frame it may change (nothing outside) and the symmetry.
struct Canvas {
    PixelImage& img;
    IRect clip;
    Symmetry sym = Symmetry::None;
};

/// Each tool returns the rectangle it changed (empty: nothing).
IRect plot(Canvas& cv, int x, int y, Px c);
IRect drawLine(Canvas& cv, int x0, int y0, int x1, int y1, Px c);
IRect drawRect(Canvas& cv, int x0, int y0, int x1, int y1, Px c, bool filled);
/// The ellipse inscribed in the rectangle between the two corners.
IRect drawEllipse(Canvas& cv, int x0, int y0, int x1, int y1, Px c, bool filled);
/// The area of the colour under (x, y) (4-connected, inside the frame) becomes `c`.
IRect floodFill(Canvas& cv, int x, int y, Px c);
/// Every pixel of `from` inside `r` becomes `to` (exact match); returns how many changed.
int replaceColor(PixelImage& img, const IRect& r, Px from, Px to);
/// The colours of `r` (transparent pixels left out), the most used first.
std::vector<std::pair<Px, int>> colorCounts(const PixelImage& img, const IRect& r);
/// The pixels of a straight line (Bresenham), both ends included.
std::vector<std::pair<int, int>> linePoints(int x0, int y0, int x1, int y1);

/// One change kept for undo / redo: the rectangle it touched, as it was and as it became; or the
/// whole image when its size changed. `metaBefore` / `metaAfter`: the document's other settings
/// (frames, emitter...) when they changed with it (null otherwise).
struct ImageEdit {
    std::string label;
    IRect region;
    PixelImage before, after;
    bool resized = false;
    Json metaBefore, metaAfter;
    size_t bytes() const { return before.data().size() + after.data().size() + 64; }
};

/// The changes of a document, by difference with the image as it was before each one.
class ImageHistory {
public:
    explicit ImageHistory(size_t maxBytes = 48u << 20) : maxBytes_(maxBytes) {}
    void clear() {
        edits_.clear();
        at_ = 0;
    }
    /// Keeps the change from `before` to `after` (nothing when they are the same and the
    /// settings did not change): only the rectangle that differs is stored. True when kept.
    bool record(const std::string& label, const PixelImage& before, const PixelImage& after, const Json& metaBefore = Json(),
                const Json& metaAfter = Json());
    /// The same for a change inside `region` only (the image's size unchanged): `before` is that
    /// region as it was, `image` the whole image now. Only the pixels of `region` are compared.
    bool recordRegion(const std::string& label, const IRect& region, const PixelImage& before, const PixelImage& image,
                      const Json& metaBefore = Json(), const Json& metaAfter = Json());
    bool canUndo() const { return at_ > 0; }
    bool canRedo() const { return at_ < edits_.size(); }
    /// The change to take back (applied to `img` here); nullptr when there is none.
    const ImageEdit* undo(PixelImage& img);
    const ImageEdit* redo(PixelImage& img);
    const std::string& undoLabel() const;
    const std::string& redoLabel() const;
    size_t size() const { return edits_.size(); }
    size_t position() const { return at_; }
    size_t bytes() const;

private:
    std::vector<ImageEdit> edits_;
    size_t at_ = 0;  // the changes applied: edits_[0 .. at_)
    size_t maxBytes_;
};

}  // namespace cosmic::studio
