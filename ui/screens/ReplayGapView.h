#pragma once

#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "game/ghost/ReplayGap.h"
#include "ui/widgets/UIContext.h"

namespace cosmic::replaygap {

// The drawing of the gap (2.91, replay viewer and comparison): the curve over the whole run,
// which is also the timeline (a click goes to that instant), and its colours. Every text is
// measured: axis labels spaced by their width, checkpoint names elided to the room before the
// next one, the tooltip kept inside the plot above the names (it never covers a text).

/// Behind (above zero), ahead (below), the window of a measure A → B.
inline constexpr Color kBehind{217, 89, 38, 255};
inline constexpr Color kAhead{57, 135, 229, 255};
inline constexpr Color kWindow{255, 214, 90, 255};
/// The same, as text (lighter, readable on the dark panels).
inline constexpr Color kBehindText{255, 176, 138, 255};
inline constexpr Color kAheadText{150, 196, 255, 255};

struct ChartSpec {
    const GapCurve* curve = nullptr;  // nullptr or empty: `message` in its place
    bool distance = false;            // the curve in metres (else in seconds)
    bool compare = false;             // the tooltip says « A − B »
    double end = 1.0;                 // the time axis: 0 .. end (s)
    double cursor = 0.0;              // the instant shown
    const GapSample* cursorSample = nullptr;  // its gap (the live counter's)
    struct Mark {
        double t = 0.0;
        std::string name;
    };
    std::vector<Mark> checkpoints;    // the run's checkpoints: dashed lines, named at the bottom
    std::vector<double> topTicks;     // marks above the plot (A's splits, its finish)
    std::vector<double> bottomTicks;  // marks below (B's splits on A's clock, its finish)
    Color topColor, bottomColor;
    std::vector<std::pair<double, double>> windows;  // shaded intervals (a measure A → B)
    std::string message;              // when there is no curve (loading, no reference…)
    std::string chrono;               // right of the time axis
    Color chronoColor;
};

struct ChartOut {
    Rect plot;
    bool hover = false;  // the pointer is over the plot
    double hoverT = 0.0; // the instant under it
};

/// The axis row under the plot, for a font size (the caller reserves it).
float axisHeight(const UIContext& ui);
/// The plot, the checkpoint lines and names, the curve with its two colours, the cursor and the
/// tooltip, the time axis and the chronometer, in `area` (axis row included).
ChartOut drawChart(UIContext& ui, const Rect& area, const ChartSpec& s);

/// "+0,846 s" / "-1,250 s" / "0,000 s" (formatDeltaMs).
std::string timeText(int64_t ms);
/// "6,8 m derrière" / "6,8 m devant" / "0,0 m : côte à côte" (`who`: " B" in a comparison).
std::string distanceWords(float metres, bool compare);

}  // namespace cosmic::replaygap
