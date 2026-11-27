#pragma once

// The zoomable view of a chart (2.98: the training curve of ADMINISTRATION › TAS, docs/TAS.md
// « Courbe d'entraînement »): the extent of its data and the part of it shown, on two independent
// axes. Not zoomed, the view follows the extent (live data keeps showing everything); zoomed, it
// stays where it is (clamped into the extent) until it is reset. Pure arithmetic: the screens
// read their mouse, keys and gamepad and call it; the tests check the round trips.

#include "engine/core/Math.h"

namespace cosmic::charts {

struct ChartView {
    double x0 = 0.0, x1 = 1.0, y0 = 0.0, y1 = 1.0;      // shown
    double fx0 = 0.0, fx1 = 1.0, fy0 = 0.0, fy1 = 1.0;  // the whole extent
    double minW = 1.0, minH = 1.0;                      // the deepest zoom (the smallest span shown)
    bool zoomed = false;                                // false: the view is the extent and follows it

    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
    /// The extent of the data now; the view follows it while not zoomed, else is kept inside it.
    void setExtent(double ax0, double ax1, double ay0, double ay1);
    /// Back to the whole extent (following it again).
    void reset();
    /// Zoomed by `kx` and `ky` (below 1: closer) keeping the data point (px, py) where it is.
    void zoomAt(double px, double py, double kx, double ky);
    /// Moved by (dx, dy) in data units (kept inside the extent).
    void pan(double dx, double dy);
    /// The rectangle (any corners' order) shown, at least minW × minH.
    void zoomTo(double ax0, double ax1, double ay0, double ay1);

    /// Screen ↔ data in `r` (the larger y at the top).
    float sx(double x, const Rect& r) const;
    float sy(double y, const Rect& r) const;
    double dx(float px, const Rect& r) const;
    double dy(float py, const Rect& r) const;

private:
    /// The spans within [min, extent], the view inside the extent; zoomed unless it is the extent.
    void clamp();
};

}  // namespace cosmic::charts
