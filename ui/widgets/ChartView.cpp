#include "ui/widgets/ChartView.h"

#include <algorithm>
#include <cmath>

namespace cosmic::charts {

namespace {

/// One axis: the span within [minSpan, the extent's] (a smaller extent: minSpan around its middle),
/// the view inside the extent.
void clampAxis(double& a, double& b, double lo, double hi, double minSpan) {
    if (hi < lo) std::swap(hi, lo);
    const double full = hi - lo;
    double span = std::clamp(b - a, std::min(minSpan, std::max(full, 1e-9)), std::max(full, 1e-9));
    if (!std::isfinite(span) || span <= 0.0) span = std::max(full, 1e-9);
    double mid = (a + b) * 0.5;
    if (!std::isfinite(mid)) mid = (lo + hi) * 0.5;
    a = mid - span * 0.5;
    b = mid + span * 0.5;
    if (a < lo) b += lo - a, a = lo;
    if (b > hi) a -= b - hi, b = hi;
    a = std::max(a, lo);
}

}  // namespace

void ChartView::setExtent(double ax0, double ax1, double ay0, double ay1) {
    fx0 = std::min(ax0, ax1);
    fx1 = std::max(ax0, ax1);
    fy0 = std::min(ay0, ay1);
    fy1 = std::max(ay0, ay1);
    if (!zoomed) {
        x0 = fx0, x1 = fx1, y0 = fy0, y1 = fy1;
        return;
    }
    clamp();
}

void ChartView::reset() {
    zoomed = false;
    x0 = fx0, x1 = fx1, y0 = fy0, y1 = fy1;
}

void ChartView::zoomAt(double px, double py, double kx, double ky) {
    kx = std::clamp(kx, 1e-4, 1e4);
    ky = std::clamp(ky, 1e-4, 1e4);
    x0 = px - (px - x0) * kx;
    x1 = px + (x1 - px) * kx;
    y0 = py - (py - y0) * ky;
    y1 = py + (y1 - py) * ky;
    // At the deepest zoom the point stays put: the span grows back around it.
    if (x1 - x0 < minW) {
        const double f = (px - x0) / std::max(1e-12, x1 - x0);
        x0 = px - minW * f, x1 = x0 + minW;
    }
    if (y1 - y0 < minH) {
        const double f = (py - y0) / std::max(1e-12, y1 - y0);
        y0 = py - minH * f, y1 = y0 + minH;
    }
    zoomed = true;
    clamp();
}

void ChartView::pan(double dx, double dy) {
    if (!zoomed) return;  // the whole extent: nowhere to go
    x0 += dx, x1 += dx, y0 += dy, y1 += dy;
    clamp();
}

void ChartView::zoomTo(double ax0, double ax1, double ay0, double ay1) {
    x0 = std::min(ax0, ax1), x1 = std::max(ax0, ax1);
    y0 = std::min(ay0, ay1), y1 = std::max(ay0, ay1);
    if (x1 - x0 < minW) {
        const double m = (x0 + x1) * 0.5;
        x0 = m - minW * 0.5, x1 = m + minW * 0.5;
    }
    if (y1 - y0 < minH) {
        const double m = (y0 + y1) * 0.5;
        y0 = m - minH * 0.5, y1 = m + minH * 0.5;
    }
    zoomed = true;
    clamp();
}

void ChartView::clamp() {
    clampAxis(x0, x1, fx0, fx1, minW);
    clampAxis(y0, y1, fy0, fy1, minH);
    const double ex = std::max(1e-9, (fx1 - fx0) * 1e-6), ey = std::max(1e-9, (fy1 - fy0) * 1e-6);
    if (std::abs(x0 - fx0) <= ex && std::abs(x1 - fx1) <= ex && std::abs(y0 - fy0) <= ey && std::abs(y1 - fy1) <= ey) {
        zoomed = false;
        x0 = fx0, x1 = fx1, y0 = fy0, y1 = fy1;
    }
}

float ChartView::sx(double x, const Rect& r) const { return r.x + static_cast<float>((x - x0) / std::max(1e-12, x1 - x0)) * r.w; }
float ChartView::sy(double y, const Rect& r) const { return r.bottom() - static_cast<float>((y - y0) / std::max(1e-12, y1 - y0)) * r.h; }
double ChartView::dx(float px, const Rect& r) const { return x0 + static_cast<double>(px - r.x) / std::max(1e-6, static_cast<double>(r.w)) * (x1 - x0); }
double ChartView::dy(float py, const Rect& r) const { return y0 + static_cast<double>(r.bottom() - py) / std::max(1e-6, static_cast<double>(r.h)) * (y1 - y0); }

}  // namespace cosmic::charts
