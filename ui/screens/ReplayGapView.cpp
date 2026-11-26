#include "ui/screens/ReplayGapView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/core/Application.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/ReplayCompare.h"

namespace cosmic::replaygap {

namespace {

/// Never below 11.5 pixels on screen (the font loses its spaces under that).
float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }
float textW(UIContext& ui, const std::string& t, FontFace f, float size) { return ui.text().measure(t, f, ui.s(size)).x; }
float lineOf(const UIContext& ui, float size) { return ui.s(fz(ui, size)) * 1.45f; }

/// A round limit for the scale, a little above `v` (never below `floor`).
float niceLimit(float v, float floor) {
    static constexpr std::array<float, 16> steps{0.5f, 1, 2, 3, 5, 10, 15, 20, 30, 60, 120, 300, 600, 1200, 3000, 7200};
    const float want = std::max(floor, v * 1.08f);
    for (float s : steps)
        if (s >= want) return s;
    return std::ceil(want / 1000.0f) * 1000.0f;
}

/// "3" or "0,5" (French decimal comma).
std::string number(float v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 1e-3f) std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::lround(v)));
    else std::snprintf(buf, sizeof buf, "%d,%d", static_cast<int>(v), static_cast<int>(std::lround((v - std::floor(v)) * 10.0f)));
    return buf;
}

/// "0:30", "1:00", "1:02:00" (the time axis).
std::string clockLabel(double s) {
    const auto t = static_cast<long>(std::lround(s));
    char buf[32];
    if (t >= 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld", t / 60, t % 60);
    return buf;
}

/// The value plotted for a sample (seconds, or metres with « behind » above zero).
bool valueOf(const GapSample& s, bool distance, float& v) {
    if (distance) {
        if (!s.hasDistance) return false;
        v = -s.metres;
        return true;
    }
    if (!s.hasTime) return false;
    v = static_cast<float>(s.timeMs) / 1000.0f;
    return true;
}

/// The sample nearest to `t` (tooltip under the pointer).
const GapSample* sampleNear(const GapCurve& c, double t) {
    if (c.samples.empty()) return nullptr;
    auto it = std::lower_bound(c.samples.begin(), c.samples.end(), t, [](const GapSample& s, double v) { return s.t < v; });
    if (it == c.samples.end()) return &c.samples.back();
    if (it != c.samples.begin() && std::abs(std::prev(it)->t - t) < std::abs(it->t - t)) --it;
    return &*it;
}

void dashedV(Renderer& r, float x, float y0, float y1, Color c, float dash, float w) {
    for (float y = y0; y < y1; y += dash * 2.0f) r.fillRect({x - w * 0.5f, y, w, std::min(dash, y1 - y)}, c);
}

/// Triangles gathered for one draw (the curve's area and its line: thousands of them).
struct Mesh {
    std::vector<SDL_Vertex> v;
    static SDL_FColor col(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}; }
    void tri(Vec2 a, Vec2 b, Vec2 c, Color k) {
        const SDL_FColor f = col(k);
        v.push_back({{a.x, a.y}, f, {0, 0}});
        v.push_back({{b.x, b.y}, f, {0, 0}});
        v.push_back({{c.x, c.y}, f, {0, 0}});
    }
    void line(Vec2 a, Vec2 b, Color k, float w) {
        const Vec2 d = b - a;
        const float len = d.length();
        if (len < 1e-4f) return;
        const Vec2 n{-d.y / len * w * 0.5f, d.x / len * w * 0.5f};
        tri(a + n, b + n, b - n, k);
        tri(a + n, b - n, a - n, k);
    }
};

/// The area between the curve and zero from `a` to `b` (screen points), red above, blue below,
/// split where the curve crosses zero.
void fillSpan(Mesh& m, Vec2 a, Vec2 b, float zeroY, Color above, Color below) {
    const bool upA = a.y < zeroY, upB = b.y < zeroY;
    if (upA == upB || std::abs(a.y - zeroY) < 0.01f || std::abs(b.y - zeroY) < 0.01f) {
        const Color c = (a.y + b.y) * 0.5f < zeroY ? above : below;
        m.tri({a.x, zeroY}, a, b, c);
        m.tri({a.x, zeroY}, b, {b.x, zeroY}, c);
        return;
    }
    const float u = (zeroY - a.y) / (b.y - a.y);
    const Vec2 mid{a.x + (b.x - a.x) * u, zeroY};
    m.tri({a.x, zeroY}, a, mid, upA ? above : below);
    m.tri(mid, b, {b.x, zeroY}, upB ? above : below);
}

}  // namespace

float axisHeight(const UIContext& ui) { return lineOf(ui, 12); }

std::string timeText(int64_t ms) { return formatDeltaMs(ms); }

std::string distanceWords(float metres, bool compare) {
    const std::string m = formatGapMetres(std::abs(metres));
    if (m == "0,0 m") return "0,0 m : côte à côte";
    if (compare) return metres < 0.0f ? m + " derrière B" : m + " devant B";
    return metres < 0.0f ? m + " derrière" : m + " devant";
}

ChartOut drawChart(UIContext& ui, const Rect& area, const ChartSpec& s) {
    ChartOut out;
    const UITheme& th = ui.theme();
    Renderer& r = ui.r();
    const float af = fz(ui, 12);
    const float axisH = axisHeight(ui);
    const double end = std::max(0.001, s.end);

    // The scale: one for the whole run, symmetric, round.
    bool have = false;
    if (s.curve)
        for (const GapSample& g : s.curve->samples)
            if (s.distance ? g.hasDistance : g.hasTime) {
                have = true;
                break;
            }
    const float lim = s.distance ? niceLimit(s.curve ? s.curve->maxMetres : 0.0f, 2.0f)
                                 : niceLimit(s.curve ? static_cast<float>(s.curve->maxTimeMs) / 1000.0f : 0.0f, 1.0f);
    const std::string unit = s.distance ? " m" : " s";
    // Behind is above zero in both units: in distance, behind is a negative number of metres.
    const std::string top = (s.distance ? "-" : "+") + number(lim) + unit;
    const std::string bottom = (s.distance ? "+" : "-") + number(lim) + unit;
    const std::string zero = "0" + unit;
    const float lw = std::max({textW(ui, top, FontFace::Body, af), textW(ui, bottom, FontFace::Body, af), textW(ui, zero, FontFace::Body, af)}) + ui.s(10);
    const Rect plot{area.x + lw, area.y + ui.s(2), std::max(ui.s(40), area.w - lw), std::max(ui.s(30), area.h - axisH - ui.s(2))};
    out.plot = plot;
    // The checkpoint names keep a band at the bottom of the plot: the curve, its area and the
    // cursor stay above it (nothing is drawn over a name).
    const float lh = lineOf(ui, 12);
    const float namesBand = lh + ui.s(2);
    const bool names = plot.h >= namesBand * 3.5f;
    const float curveTop = plot.y + ui.s(4), curveBottom = plot.bottom() - (names ? namesBand : 0.0f) - ui.s(2);
    const float zeroY = (curveTop + curveBottom) * 0.5f;
    const float half = std::max(1.0f, (curveBottom - curveTop) * 0.5f);
    auto xOf = [&](double t) { return plot.x + plot.w * static_cast<float>(std::clamp(t / end, 0.0, 1.0)); };
    auto yOf = [&](float v) { return zeroY - std::clamp(v / lim, -1.0f, 1.0f) * half; };

    r.fillRoundedRect(plot, ui.s(6), Color{10, 16, 32, 215});
    // Axis labels (the extremes only when they have room apart from zero's).
    ui.label(zero, {plot.x - ui.s(6), zeroY}, af, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
    if (half >= lh * 1.1f) {
        ui.label(top, {plot.x - ui.s(6), zeroY - half + lh * 0.4f}, af, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
        ui.label(bottom, {plot.x - ui.s(6), zeroY + half - lh * 0.4f}, af, th.textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
    // Shaded windows (a measure A → B), under everything.
    for (const auto& [a, b] : s.windows) {
        const float x0 = xOf(a), x1 = xOf(b);
        if (x1 > x0) r.fillRect({x0, plot.y, x1 - x0, curveBottom - plot.y}, kWindow.withAlpha(48));
    }
    r.fillRect({plot.x, zeroY - std::max(0.5f, ui.s(0.5f)), plot.w, std::max(1.0f, ui.s(1))}, th.textDim.withAlpha(110));

    // Checkpoints: dashed lines, their names at the bottom, each in the room before the next.
    for (size_t i = 0; i < s.checkpoints.size(); ++i) {
        const float x = xOf(s.checkpoints[i].t);
        dashedV(r, x, plot.y + ui.s(2), plot.bottom() - ui.s(2), th.textDim.withAlpha(80), ui.s(4), std::max(1.0f, ui.s(1)));
        if (!names || s.checkpoints[i].name.empty()) continue;
        float limit = plot.right() - ui.s(4);
        for (size_t j = i + 1; j < s.checkpoints.size(); ++j) {
            const float nx = xOf(s.checkpoints[j].t);
            if (nx > x + ui.s(2)) {
                limit = std::min(limit, nx - ui.s(6));
                break;
            }
        }
        const float room = limit - (x + ui.s(4));
        if (room < ui.s(28)) continue;
        ui.label(ui.elide(s.checkpoints[i].name, FontFace::Body, af, room), {x + ui.s(4), plot.bottom() - ui.s(2)}, af, th.textDim, Align::Left,
                 FontFace::Body, VAlign::Bottom);
    }
    const float tick = ui.s(6), tw = std::max(1.0f, ui.s(2));
    for (double t : s.topTicks) r.fillRect({xOf(t) - tw * 0.5f, plot.y, tw, tick}, s.topColor);
    for (double t : s.bottomTicks) r.fillRect({xOf(t) - tw * 0.5f, curveBottom - tick, tw, tick}, s.bottomColor);

    // The curve.
    if (have) {
        const Color above = kBehind.withAlpha(120), below = kAhead.withAlpha(120);
        const Color line{226, 236, 245, 235};
        const float lw2 = std::max(1.0f, ui.s(1.8f));
        if (s.curve->continuous) {
            Mesh area, stroke;
            area.v.reserve(static_cast<size_t>(plot.w) * 6 + 64);
            stroke.v.reserve(static_cast<size_t>(plot.w) * 6 + 64);
            bool prevOk = false;
            Vec2 prev;
            for (const GapSample& g : s.curve->samples) {
                float v = 0.0f;
                if (!valueOf(g, s.distance, v)) {
                    prevOk = false;
                    continue;
                }
                const Vec2 p{xOf(g.t), yOf(v)};
                if (prevOk && p.x - prev.x < 0.75f && !g.exact) continue;  // about one point per pixel
                if (prevOk) {
                    fillSpan(area, prev, p, zeroY, above, below);
                    stroke.line(prev, p, line, lw2);
                }
                prev = p;
                prevOk = true;
            }
            r.drawGeometry(nullptr, area.v, {});
            r.drawGeometry(nullptr, stroke.v, {});
        } else {
            // Checkpoints only: a dot at each one, joined by a faint dashed line (nothing is known in between).
            bool prevOk = false;
            Vec2 prev;
            for (const GapSample& g : s.curve->samples) {
                float v = 0.0f;
                if (!valueOf(g, s.distance, v)) continue;
                const Vec2 p{xOf(g.t), yOf(v)};
                if (prevOk) {
                    const float len = (p - prev).length();
                    const Vec2 d = len > 0.0f ? (p - prev) / len : Vec2{};
                    for (float a = 0.0f; a < len; a += ui.s(10)) r.drawLine(prev + d * a, prev + d * std::min(len, a + ui.s(5)), line.withAlpha(110), 1.0f);
                }
                r.fillCircle(p, ui.s(4.5f), v > 0.0f ? kBehind : v < 0.0f ? kAhead : th.text);
                r.drawCircle(p, ui.s(4.5f), line, 1.0f);
                prev = p;
                prevOk = true;
            }
        }
    }

    // The pointer over the plot: the instant under it.
    const InputManager& in = ui.app().input();
    if (ui.acceptsInput() && plot.contains(in.mousePos())) {
        out.hover = true;
        out.hoverT = std::clamp(static_cast<double>((in.mousePos().x - plot.x) / plot.w), 0.0, 1.0) * end;
    }

    // The cursor: the instant shown.
    const float cx = xOf(s.cursor);
    r.fillRect({cx - std::max(0.75f, ui.s(0.75f)), plot.y, std::max(1.5f, ui.s(1.5f)), curveBottom - plot.y}, Color{255, 255, 255, 170});
    float cv = 0.0f;
    if (s.cursorSample && valueOf(*s.cursorSample, s.distance, cv)) {
        const Vec2 p{cx, yOf(cv)};
        r.fillCircle(p, ui.s(6), Color{255, 255, 255, 235});
        r.fillCircle(p, ui.s(4), cv > 0.0f ? kBehindText : cv < 0.0f ? kAheadText : th.text);
    }
    if (!have && !s.message.empty()) {
        // No curve: the message on a plate over the zero line's middle (no line crosses it).
        const float mf = fz(ui, 14);
        const std::string m = ui.elide(s.message, FontFace::Body, mf, plot.w - ui.s(40));
        const Vec2 sz = ui.text().measure(m, FontFace::Body, ui.s(mf));
        const Rect plate{plot.center().x - sz.x * 0.5f - ui.s(10), zeroY - sz.y * 0.5f - ui.s(4), sz.x + ui.s(20), sz.y + ui.s(8)};
        r.fillRoundedRect(plate, ui.s(5), Color{10, 16, 32, 255});
        r.drawRoundedRect(plate, ui.s(5), th.border, 1.0f);
        ui.label(m, {plot.center().x, zeroY}, mf, th.textDim, Align::Center, FontFace::Body, VAlign::Middle);
    }

    // The tooltip: the instant under the pointer, else the cursor's; inside the plot, above
    // the checkpoint names (never on a text), left out when there is no room.
    const GapSample* tipSample = nullptr;
    double tipT = s.cursor;
    if (out.hover && s.curve && !s.curve->empty()) {
        tipT = out.hoverT;
        tipSample = s.curve->continuous ? sampleNear(*s.curve, tipT) : nullptr;
        if (tipSample) tipT = tipSample->t;
    } else {
        tipSample = s.cursorSample;
    }
    if (tipSample && (tipSample->hasTime || tipSample->hasDistance)) {
        const float tf = fz(ui, 13);
        const std::string title = formatGhostTime(static_cast<float>(tipT));
        std::string l1 = s.compare ? "A − B en temps" : "En temps";
        if (tipSample->exact) l1 = s.compare ? "A − B en temps (exact)" : "En temps (exact)";
        const std::string v1 = tipSample->hasTime ? formatDeltaMs(tipSample->timeMs) : "—";
        const std::string l2 = "En distance";
        const std::string v2 = tipSample->hasDistance ? formatGapMetres(tipSample->metres) : "—";
        const float gap = ui.s(18);
        const float w = std::max(textW(ui, title, FontFace::Bold, tf),
                                 std::max(textW(ui, l1, FontFace::Body, tf) + textW(ui, v1, FontFace::Bold, tf),
                                          textW(ui, l2, FontFace::Body, tf) + textW(ui, v2, FontFace::Bold, tf)) + gap) +
                        ui.s(20);
        const float rowH = lineOf(ui, 13);
        const float h = rowH * 3.0f + ui.s(10);
        const float x = xOf(tipT);
        Rect box{x + ui.s(10), plot.y + ui.s(4), w, h};
        if (box.right() > plot.right() - ui.s(4)) box.x = x - ui.s(10) - w;
        if (box.x >= plot.x + ui.s(4) && box.bottom() <= curveBottom) {
            r.fillRoundedRect(box, ui.s(6), Color{8, 12, 26, 240});
            r.drawRoundedRect(box, ui.s(6), th.accent.withAlpha(150), std::max(1.0f, ui.s(1.2f)));
            float ty = box.y + ui.s(5) + rowH * 0.5f;
            const float lx = box.x + ui.s(10), rx = box.right() - ui.s(10);
            ui.label(title, {lx, ty}, tf, th.text, Align::Left, FontFace::Bold, VAlign::Middle);
            ty += rowH;
            auto row = [&](const std::string& l, const std::string& v, Color vc) {
                ui.label(l, {lx, ty}, tf, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
                ui.label(v, {rx, ty}, tf, vc, Align::Right, FontFace::Bold, VAlign::Middle);
                ty += rowH;
            };
            const Color c1 = !tipSample->hasTime ? th.textDim : tipSample->timeMs > 0 ? kBehindText : tipSample->timeMs < 0 ? kAheadText : th.text;
            const Color c2 = !tipSample->hasDistance ? th.textDim : tipSample->metres < -0.05f ? kBehindText : tipSample->metres > 0.05f ? kAheadText : th.text;
            row(l1, v1, c1);
            row(l2, v2, c2);
        }
    }

    // The time axis under the plot, the chronometer at its right (labels never on it nor on each other).
    const float ay = plot.bottom() + axisH * 0.5f + ui.s(1);
    const float cf = fz(ui, 14);
    float chronoLeft = area.right();
    if (!s.chrono.empty()) {
        chronoLeft = area.right() - textW(ui, s.chrono, FontFace::Bold, cf);
        ui.label(s.chrono, {area.right(), ay}, cf, s.chronoColor, Align::Right, FontFace::Bold, VAlign::Middle);
    }
    static constexpr std::array<double, 12> steps{5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200};
    const float labelW = textW(ui, clockLabel(end), FontFace::Body, af) + ui.s(24);
    double step = steps.back();
    for (double st : steps)
        if (static_cast<double>(plot.w) * st / end >= labelW) {
            step = st;
            break;
        }
    float lastRight = -1e9f;
    for (double t = 0.0; t <= end + 1e-6; t += step) {
        const std::string l = clockLabel(t);
        const float w = textW(ui, l, FontFace::Body, af);
        const float x = xOf(t);
        const float left = t == 0.0 ? x : x - w * 0.5f;
        if (left < lastRight + ui.s(12) || left + w > chronoLeft - ui.s(16)) continue;
        r.fillRect({x - 0.5f, plot.bottom(), 1.0f, ui.s(3)}, th.textDim.withAlpha(140));
        ui.label(l, {left, ay}, af, th.textDim, Align::Left, FontFace::Body, VAlign::Middle);
        lastRight = left + w;
    }
    return out;
}

}  // namespace cosmic::replaygap
