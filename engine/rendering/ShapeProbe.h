#pragma once

#include <cstdint>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

/// 2.60, display only: told about every shape the renderer draws while it is set
/// (Renderer::setListener), in screen pixels. A soft glow comes as its centre and radius: only
/// its bright core counts (kGlowCore of the radius), its faint halo does not. Shapes fainter
/// than kMinAlpha are not reported (washes, fading trails).
class ShapeListener {
public:
    static constexpr uint8_t kMinAlpha = 16;
    static constexpr float kGlowCore = 0.35f;
    virtual ~ShapeListener() = default;
    virtual void triangle(Vec2 a, Vec2 b, Vec2 c) = 0;
    virtual void segment(Vec2 a, Vec2 b) = 0;
    virtual void rect(const Rect& r) = 0;
    virtual void glow(Vec2 center, float radius) = 0;
};

/// The rectangle a group of shapes covers on screen (what a block of the HUD really draws).
class ShapeBounds final : public ShapeListener {
public:
    void triangle(Vec2 a, Vec2 b, Vec2 c) override;
    void segment(Vec2 a, Vec2 b) override;
    void rect(const Rect& r) override;
    void glow(Vec2 center, float radius) override;
    /// Empty (w = h = 0) when nothing was drawn.
    Rect area() const { return any_ ? Rect{x0_, y0_, x1_ - x0_, y1_ - y0_} : Rect{}; }

private:
    void add(float x0, float y0, float x1, float y1);
    bool any_ = false;
    float x0_ = 0.0f, y0_ = 0.0f, x1_ = 0.0f, y1_ = 0.0f;
};

/// Which of a few rectangles of the screen (the blocks of the HUD) the shapes drawn touch, each
/// shape tested exactly (a thin diagonal beam touches only what it crosses).
class CoverageProbe final : public ShapeListener {
public:
    /// The rectangles watched (an empty one is never touched); every one untouched. The storage
    /// is kept from one frame to the next (no allocation once it has grown).
    void watch(const Rect* zones, size_t count);
    void watch(const std::vector<Rect>& zones) { watch(zones.data(), zones.size()); }
    /// A zone no shape can touch any more (emptied).
    void ignore(size_t i);
    size_t size() const { return zones_.size(); }
    const Rect& zone(size_t i) const { return zones_[i]; }
    bool touched(size_t i) const { return i < hit_.size() && hit_[i] == 1; }

    void triangle(Vec2 a, Vec2 b, Vec2 c) override;
    void segment(Vec2 a, Vec2 b) override;
    void rect(const Rect& r) override;
    void glow(Vec2 center, float radius) override;

private:
    /// Marks the zones a shape whose bounding box is [x0, x1] x [y0, y1] touches (`exact` tells
    /// whether it really does, the bounding boxes overlapping).
    template <class Exact>
    void test(float x0, float y0, float x1, float y1, Exact&& exact);
    std::vector<Rect> zones_;
    std::vector<uint8_t> hit_;
    size_t left_ = 0;  // zones not touched yet (none: nothing to test)
};

/// Exact tests of a shape against a rectangle (shapes touching only along an edge do not count;
/// an empty rectangle is touched by nothing).
namespace ShapeTest {
bool triangleTouchesRect(Vec2 a, Vec2 b, Vec2 c, const Rect& r);
bool segmentTouchesRect(Vec2 a, Vec2 b, const Rect& r);
bool circleTouchesRect(Vec2 center, float radius, const Rect& r);
}  // namespace ShapeTest

}  // namespace cosmic
