#pragma once

// 2.53: charts of the « Statistiques » screen, reusable: a smooth monotone curve with a gradient
// area, a cloud of points, steps, horizontal and vertical bars with rounded ends, a heat band
// over a level's relief, and the table that replaces a chart (« Tableau »). Thin strokes (2 px),
// a discreet grid, axes in dim text, values in the text colours. Every text is measured: the
// axis labels are spaced by their measured width, never one over another.
//
// Each chart is focusable: the mouse shows the tooltip of what it points at; focused (keyboard,
// gamepad), ← → move a cursor over the points with the same tooltip. The tooltip is gathered
// in a Tip and drawn last, over everything (drawTip).

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::charts {

/// What a chart's cursor (or the mouse) points at.
struct Tip {
    struct Row {
        Color swatch{0, 0, 0, 0};  // alpha 0: none
        std::string label, value;
    };
    bool show = false;
    Vec2 anchor;  // the point the box keeps clear of
    std::string title;
    std::vector<Row> rows;
};
/// Draws the tooltip (inside the screen, measured).
void drawTip(UIContext& ui, const Tip& t);

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size);

/// Round numbers between `min` and `max`, at most `maxCount` of them.
std::vector<float> niceTicks(float min, float max, int maxCount);

struct Series {
    enum class Kind { Line, Dots, Step };
    std::string name;
    Color color;
    Kind kind = Kind::Line;
    bool area = false;
    std::vector<Vec2> pts;  // data space, x increasing
};

struct LineSpec {
    std::vector<Series> series;
    float yMin = 0.0f, yMax = 1.0f;
    std::function<std::string(float)> yLabel;
    std::function<std::string(float)> xLabel;
    bool integerX = false;   // x are counts (attempts, runs): whole ticks
    bool endLabels = false;  // the name of each curve at its end (no overlap)
    /// The tooltip at x (the cursor's): its title and rows.
    std::function<void(float x, Tip&)> tip;
};
/// A curve chart in `r`. `cursor`: the point the keyboard cursor is on (index in the x values
/// of every series), kept by the caller.
void lineChart(UIContext& ui, const std::string& id, const Rect& r, const LineSpec& s, int& cursor, Tip& tip);

/// A smooth curve through `pts` (monotone cubic: never over- or undershoots), sampled.
std::vector<Vec2> monotonePath(const std::vector<Vec2>& pts, int perSegment = 10);

struct Bar {
    std::string label;
    float value = 0.0f;  // drawn against the chart's maximum
    std::string valueText;
    Color color;
    std::string tipTitle;
    std::vector<Tip::Row> tipRows;
};
/// Height of `n` rows of horizontal bars.
float hBarsHeight(const UIContext& ui, size_t n);
/// Horizontal bars, one per row: its label (measured, shortened with « … » if it must), the bar
/// with a rounded end, its value at the right.
void hBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, float maxValue, int& cursor, Tip& tip);
/// Vertical bars with a rounded top, their labels under them (spaced by their measured width).
void vBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, int& cursor, Tip& tip,
           const std::function<std::string(float)>& yLabel);

/// The colour of a heat value 0..1 (one hue, lighter = more).
Color heatColor(float t);
struct Heat {
    static constexpr int kCells = 40;
    std::array<float, kCells> values{};  // -1: no data
    std::array<int, kCells> passes{}, deaths{};
    float maxValue = 1.0f;
    std::vector<float> relief;             // heights 0..1 along the route (empty: unknown yet)
    std::vector<float> checkpoints;        // positions 0..1
};
/// The level's relief with its checkpoints, the band of the 40 slices under it, « Départ » and
/// « Arrivée » under the band.
void heatBand(UIContext& ui, const std::string& id, const Rect& r, const Heat& h, int& cursor, Tip& tip);

/// The « Tableau » view of a chart: its rows a page at a time (‹ › buttons, focusable).
void table(UIContext& ui, const std::string& id, const Rect& r, const std::vector<std::string>& head,
           const std::vector<std::vector<std::string>>& rows, int& page);

}  // namespace cosmic::charts
