#include "ui/widgets/Charts.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cmath>

#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/Text.h"

namespace cosmic::charts {

namespace {

const Color kGrid{33, 43, 66, 255};
const Color kAxis{61, 73, 102, 255};
const Color kText2{179, 189, 214, 255};
const Color kSurface{26, 35, 56, 255};
const Color kTrack{14, 20, 36, 255};

SDL_Vertex vtx(Vec2 p, Color c) {
    SDL_Vertex v{};
    v.position = {p.x, p.y};
    v.color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    return v;
}

float textW(UIContext& ui, const std::string& t, FontFace f, float size) { return ui.text().measure(t, f, ui.s(size)).x; }
float lineH(UIContext& ui, FontFace f, float size) { return ui.text().measure("Ag", f, ui.s(size)).y; }

/// The keyboard cursor of a focused chart: ← → (the focus does not move that way).
void moveCursor(UIContext& ui, int& cursor, int count) {
    if (count <= 0) return;
    InputManager& in = ui.input();
    if (cursor < 0 || cursor >= count) cursor = count - 1;
    if (!ui.acceptsInput()) return;
    if (in.menuPressed(Action::MenuLeft)) cursor = std::max(0, cursor - 1);
    if (in.menuPressed(Action::MenuRight)) cursor = std::min(count - 1, cursor + 1);
    ui.consumeNavigation(true, false);
}

void focusFrame(UIContext& ui, const Rect& r) {
    ui.r().drawRoundedRect(r.expanded(ui.s(4)), ui.s(8), ui.theme().accent.withAlpha(170), std::max(1.0f, ui.s(1.5f)));
}

void dashedV(UIContext& ui, float x, float y0, float y1, Color c) {
    const float dash = ui.s(4), gap = ui.s(3);
    for (float y = y0; y < y1; y += dash + gap) ui.r().drawLine({x, y}, {x, std::min(y1, y + dash)}, c, 1.0f);
}

void polyline(UIContext& ui, const std::vector<Vec2>& p, Color c) {
    const float th = std::max(1.5f, ui.s(2.0f));
    for (size_t i = 1; i < p.size(); ++i) ui.r().drawLine(p[i - 1], p[i], c, th);
}

}  // namespace

float fz(const UIContext& ui, float size) { return std::max(size, 11.5f / ui.s(1.0f)); }

std::vector<float> niceTicks(float min, float max, int maxCount) {
    std::vector<float> out;
    maxCount = std::max(1, maxCount);
    const float span = max - min;
    if (!(span > 0.0f) || !std::isfinite(span)) {
        out.push_back(min);
        return out;
    }
    const float step0 = span / static_cast<float>(maxCount);
    const float mag = std::pow(10.0f, std::floor(std::log10(step0)));
    float step = mag * 10.0f;
    for (float m : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
        if (span / (m * mag) <= static_cast<float>(maxCount) + 1e-4f) {
            step = m * mag;
            break;
        }
    for (float v = std::ceil(min / step - 1e-4f) * step; v <= max + step * 1e-4f; v += step) out.push_back(std::abs(v) < step * 1e-6f ? 0.0f : v);
    return out;
}

std::vector<Vec2> monotonePath(const std::vector<Vec2>& p, int per) {
    const size_t n = p.size();
    if (n < 3) return p;
    std::vector<float> dx(n - 1), m(n - 1), t(n);
    for (size_t i = 0; i + 1 < n; ++i) {
        dx[i] = p[i + 1].x - p[i].x;
        m[i] = dx[i] != 0.0f ? (p[i + 1].y - p[i].y) / dx[i] : 0.0f;
    }
    t[0] = m[0];
    t[n - 1] = m[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) t[i] = m[i - 1] * m[i] <= 0.0f ? 0.0f : (m[i - 1] + m[i]) * 0.5f;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (m[i] == 0.0f) {
            t[i] = t[i + 1] = 0.0f;
            continue;
        }
        float a = t[i] / m[i], b = t[i + 1] / m[i], s = a * a + b * b;
        if (s > 9.0f) {
            float k = 3.0f / std::sqrt(s);
            t[i] = k * a * m[i];
            t[i + 1] = k * b * m[i];
        }
    }
    std::vector<Vec2> out;
    out.push_back(p[0]);
    for (size_t i = 0; i + 1 < n; ++i)
        for (int k = 1; k <= per; ++k) {
            float u = static_cast<float>(k) / static_cast<float>(per), u2 = u * u, u3 = u2 * u;
            float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
            out.push_back({p[i].x + u * dx[i], h00 * p[i].y + h10 * dx[i] * t[i] + h01 * p[i + 1].y + h11 * dx[i] * t[i + 1]});
        }
    return out;
}

// ------------------------------------------------------------------ tooltip

void drawTip(UIContext& ui, const Tip& t) {
    if (!t.show) return;
    const float pad = ui.s(10), tf = fz(ui, 12), rf = fz(ui, 13);
    const float th = t.title.empty() ? 0.0f : lineH(ui, FontFace::Body, tf) + ui.s(3);
    const float rh = lineH(ui, FontFace::Body, rf) + ui.s(3);
    const float sw = ui.s(8), swGap = ui.s(7);
    // The widest row (its swatch, its label, a gap, its value) sets the width.
    float w = t.title.empty() ? 0.0f : textW(ui, t.title, FontFace::Body, tf);
    for (const auto& r : t.rows) {
        float rw = (r.swatch.a ? sw + swGap : 0.0f) + textW(ui, r.label, FontFace::Body, rf) + (r.value.empty() ? 0.0f : ui.s(14) + textW(ui, r.value, FontFace::Bold, rf));
        w = std::max(w, rw);
    }
    const float maxW = ui.screenW() * 0.45f;
    w = std::min(w, maxW);
    const float bw = w + 2 * pad, bh = 2 * pad + th + rh * static_cast<float>(t.rows.size());
    float x = t.anchor.x + ui.s(14);
    if (x + bw > ui.screenW() - ui.s(8)) x = t.anchor.x - ui.s(14) - bw;
    x = std::clamp(x, ui.s(8), std::max(ui.s(8), ui.screenW() - ui.s(8) - bw));
    float y = t.anchor.y - bh - ui.s(10);
    if (y < ui.s(8)) y = t.anchor.y + ui.s(16);
    y = std::clamp(y, ui.s(8), std::max(ui.s(8), ui.screenH() - ui.s(8) - bh));
    Rect box{x, y, bw, bh};
    ui.r().fillRoundedRect(box, ui.s(10), Color{11, 17, 32, 244});
    ui.r().drawRoundedRect(box, ui.s(10), Color{39, 50, 77, 255}, std::max(1.0f, ui.s(1.0f)));
    float cy = y + pad;
    if (!t.title.empty()) {
        ui.label(ui.elide(t.title, FontFace::Body, tf, w), {x + pad, cy}, tf, ui.theme().textDim);
        cy += th;
    }
    for (const auto& r : t.rows) {
        float cx = x + pad;
        const float mid = cy + rh * 0.5f - ui.s(1.5f);
        if (r.swatch.a) {
            ui.r().fillCircle({cx + sw * 0.5f, mid}, sw * 0.5f, r.swatch);
            cx += sw + swGap;
        }
        float vw = r.value.empty() ? 0.0f : textW(ui, r.value, FontFace::Bold, rf);
        float room = x + pad + w - cx - (r.value.empty() ? 0.0f : vw + ui.s(14));
        ui.label(ui.elide(r.label, FontFace::Body, rf, room), {cx, mid}, rf, kText2, Align::Left, FontFace::Body, VAlign::Middle);
        if (!r.value.empty()) ui.label(r.value, {x + pad + w, mid}, rf, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        cy += rh;
    }
}

// ------------------------------------------------------------------ curves

void lineChart(UIContext& ui, const std::string& id, const Rect& r, const LineSpec& s, int& cursor, Tip& tip) {
    std::vector<float> xs;
    for (const auto& se : s.series)
        for (const auto& p : se.pts) xs.push_back(p.x);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    bool hovered = false;
    ui.hotspot(id, r, &hovered);
    const bool focused = ui.focused(id);
    if (xs.empty()) return;
    const float af = fz(ui, 13), lf = fz(ui, 13);
    const float lh = lineH(ui, FontFace::Body, af);
    // Room for the axes, measured.
    const float top = r.y + lh * 0.5f + ui.s(2);
    const float bottom = r.bottom() - lh * 1.5f - ui.s(8);
    const int maxY = std::clamp(static_cast<int>((bottom - top) / (lh * 2.2f)), 2, 6);
    const float yMax = s.yMax > s.yMin ? s.yMax : s.yMin + 1.0f;
    std::vector<float> yt = niceTicks(s.yMin, yMax, maxY);
    std::vector<std::string> yl;
    float lw = 0.0f;
    for (float v : yt) {
        yl.push_back(s.yLabel ? s.yLabel(v) : std::to_string(static_cast<int>(std::lround(v))));
        lw = std::max(lw, textW(ui, yl.back(), FontFace::Body, af));
    }
    float rightPad = ui.s(10);
    if (s.endLabels)
        for (const auto& se : s.series)
            if (se.kind != Series::Kind::Dots && !se.pts.empty()) rightPad = std::max(rightPad, textW(ui, se.name, FontFace::Bold, lf) + ui.s(22));
    Rect plot{r.x + lw + ui.s(10), top, 0.0f, bottom - top};
    plot.w = std::max(ui.s(40), r.right() - rightPad - plot.x);
    const float xMin = xs.front(), xMax = xs.back();
    auto X = [&](float v) { return plot.x + (xMax == xMin ? 0.5f : (v - xMin) / (xMax - xMin)) * plot.w; };
    auto Y = [&](float v) { return plot.y + (1.0f - std::clamp((v - s.yMin) / (yMax - s.yMin), -0.05f, 1.05f)) * plot.h; };
    // Grid and the values of the axis.
    for (size_t i = 0; i < yt.size(); ++i) {
        float y = Y(yt[i]);
        ui.r().drawLine({plot.x, y}, {plot.right(), y}, kGrid, 1.0f);
        ui.label(yl[i], {plot.x - ui.s(8), y}, af, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
    ui.r().drawLine({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, kAxis, 1.0f);
    // The x labels: as many as their measured width allows, never one over another.
    {
        float widest = 0.0f;
        std::vector<float> probe = {xMin, (xMin + xMax) * 0.5f, xMax};
        for (float v : probe) widest = std::max(widest, textW(ui, s.xLabel ? s.xLabel(std::round(v)) : std::to_string(static_cast<int>(v)), FontFace::Body, af));
        int n = std::max(1, static_cast<int>(plot.w / (widest + ui.s(28))));
        std::vector<float> ticks;
        if (xMax == xMin) ticks = {xMin};
        else if (s.integerX) {
            for (float v : niceTicks(xMin, xMax, std::max(1, n - 1)))
                if (v >= xMin - 1e-3f && std::abs(v - std::round(v)) < 1e-3f) ticks.push_back(std::round(v));
            if (ticks.empty()) ticks = {xMin};
        } else {
            n = std::clamp(n, 2, 7);
            for (int i = 0; i < n; ++i) ticks.push_back(std::round(xMin + (xMax - xMin) * static_cast<float>(i) / static_cast<float>(n - 1)));
            ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
        }
        float lastRight = -1e9f;
        const float ly = plot.bottom() + lh * 0.5f + ui.s(4);
        for (float v : ticks) {
            std::string t = s.xLabel ? s.xLabel(v) : std::to_string(static_cast<int>(v));
            float w = textW(ui, t, FontFace::Body, af);
            float left = std::clamp(X(v) - w * 0.5f, plot.x, r.right() - w);
            if (left < lastRight + ui.s(14)) continue;
            ui.label(t, {left, ly}, af, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Top);
            lastRight = left + w;
        }
    }
    // The series.
    ui.r().pushClip(plot.expanded(ui.s(6)));
    const float baseY = plot.bottom();
    for (const auto& se : s.series) {
        if (se.pts.empty()) continue;
        std::vector<Vec2> pts;
        for (const auto& p : se.pts) pts.push_back({X(p.x), Y(p.y)});
        if (se.kind == Series::Kind::Dots) {
            for (const auto& p : pts) ui.r().fillCircle(p, ui.s(2.6f), se.color.withAlpha(120));
            continue;
        }
        std::vector<Vec2> path;
        if (se.kind == Series::Kind::Step) {
            path.push_back(pts[0]);
            for (size_t i = 1; i < pts.size(); ++i) {
                path.push_back({pts[i].x, pts[i - 1].y});
                path.push_back(pts[i]);
            }
        } else {
            path = monotonePath(pts);
        }
        if (se.area && path.size() >= 2) {
            std::vector<SDL_Vertex> v;
            std::vector<int> idx;
            for (size_t i = 0; i < path.size(); ++i) {
                float k = std::clamp((baseY - path[i].y) / std::max(1.0f, plot.h), 0.0f, 1.0f);
                v.push_back(vtx(path[i], se.color.withAlpha(static_cast<uint8_t>(82.0f * k))));
                v.push_back(vtx({path[i].x, baseY}, se.color.withAlpha(0)));
                if (i > 0) {
                    int a = static_cast<int>(2 * (i - 1));
                    idx.insert(idx.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
                }
            }
            ui.r().drawGeometry(nullptr, v, idx);
        }
        if (path.size() == 1) ui.r().fillCircle(path[0], ui.s(3), se.color);
        polyline(ui, path, se.color);
    }
    ui.r().popClip();
    // Direct labels at the end of the curves (their names, in the text colour, never overlapping).
    if (s.endLabels) {
        struct L {
            const Series* se;
            Vec2 end;
            Rect box;
        };
        std::vector<L> ls;
        const float h = lineH(ui, FontFace::Bold, lf);
        for (const auto& se : s.series) {
            if (se.kind == Series::Kind::Dots || se.pts.empty()) continue;
            Vec2 e{X(se.pts.back().x), Y(se.pts.back().y)};
            float w = textW(ui, se.name, FontFace::Bold, lf);
            float x = e.x + ui.s(9);
            if (x + w > r.right()) x = e.x - ui.s(9) - w;
            ls.push_back({&se, e, {x, e.y - h - ui.s(2), w, h}});
        }
        std::sort(ls.begin(), ls.end(), [](const L& a, const L& b) { return a.box.y < b.box.y; });
        for (size_t i = 0; i < ls.size(); ++i) {
            ls[i].box.y = std::max(ls[i].box.y, plot.y - h * 0.5f);
            for (size_t j = 0; j < i; ++j)
                if (ls[j].box.intersects(ls[i].box.expanded(ui.s(1)))) ls[i].box.y = ls[j].box.bottom() + ui.s(2);
        }
        float over = ls.empty() ? 0.0f : ls.back().box.bottom() - plot.bottom();
        if (over > 0.0f)
            for (auto& l : ls) l.box.y -= over;
        for (const auto& l : ls) {
            ui.r().fillCircle(l.end, ui.s(4.5f), kSurface);
            ui.r().fillCircle(l.end, ui.s(3.2f), l.se->color);
            ui.label(l.se->name, {l.box.x, l.box.y}, lf, kText2, Align::Left, FontFace::Bold, VAlign::Top);
        }
    }
    // The cursor: the mouse, or ← → when the chart has the focus.
    if (focused) {
        moveCursor(ui, cursor, static_cast<int>(xs.size()));
        focusFrame(ui, r);
    }
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (hovered && m.x >= plot.x - ui.s(12) && m.x <= plot.right() + ui.s(12)) {
        float bestD = 1e9f;
        for (size_t i = 0; i < xs.size(); ++i)
            if (float d = std::abs(X(xs[i]) - m.x); d < bestD) {
                bestD = d;
                at = static_cast<int>(i);
            }
        cursor = at;
    } else if (focused) {
        at = std::clamp(cursor, 0, static_cast<int>(xs.size()) - 1);
    }
    if (at < 0 || !s.tip) return;
    const float xv = xs[static_cast<size_t>(at)];
    const float px = X(xv);
    dashedV(ui, px, plot.y, plot.bottom(), kText2.withAlpha(150));
    float topY = plot.bottom();
    for (const auto& se : s.series) {
        if (se.kind == Series::Kind::Dots) continue;
        const Vec2* hit = nullptr;
        for (const auto& p : se.pts) {
            if (p.x <= xv + 1e-4f) hit = &p;
            else break;
        }
        if (!hit || (se.kind != Series::Kind::Step && std::abs(hit->x - xv) > 1e-4f)) continue;
        Vec2 c{px, Y(hit->y)};
        ui.r().fillCircle(c, ui.s(5), kSurface);
        ui.r().fillCircle(c, ui.s(3.6f), se.color);
        topY = std::min(topY, c.y);
    }
    tip = Tip{};
    tip.show = true;
    tip.anchor = {px, topY};
    s.tip(xv, tip);
}

// ------------------------------------------------------------------ bars

float hBarsHeight(const UIContext& ui, size_t n) { return ui.s(30) * static_cast<float>(n); }

void hBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, float maxValue, int& cursor, Tip& tip) {
    bool hovered = false;
    ui.hotspot(id, r, &hovered);
    const bool focused = ui.focused(id);
    if (bars.empty()) return;
    const float f = fz(ui, 14), rowH = ui.s(30);
    float lw = 0.0f, vw = 0.0f;
    for (const auto& b : bars) {
        lw = std::max(lw, textW(ui, b.label, FontFace::Body, f));
        vw = std::max(vw, textW(ui, b.valueText, FontFace::Bold, f));
    }
    lw = std::min(lw, r.w * 0.42f);
    const float x0 = r.x + lw + ui.s(12), x1 = r.right() - vw - ui.s(12);
    const float max = maxValue > 0.0f ? maxValue : 1.0f;
    for (size_t i = 0; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        const float y = r.y + rowH * static_cast<float>(i), mid = y + rowH * 0.5f;
        ui.label(ui.elide(b.label, FontFace::Body, f, lw), {r.x, mid}, f, kText2, Align::Left, FontFace::Body, VAlign::Middle);
        if (x1 > x0) {
            Rect track{x0, mid - ui.s(7), x1 - x0, ui.s(14)};
            ui.r().fillRoundedRect(track, ui.s(4), kTrack);
            float w = std::clamp(b.value / max, 0.0f, 1.0f) * track.w;
            if (b.value > 0.0f) ui.r().fillRoundedRect({track.x, track.y, std::max(ui.s(6), w), track.h}, ui.s(4), b.color);
        }
        ui.label(b.valueText, {r.right(), mid}, f, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
    }
    if (focused) {
        moveCursor(ui, cursor, static_cast<int>(bars.size()));
        focusFrame(ui, r);
    }
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (hovered) at = std::clamp(static_cast<int>((m.y - r.y) / rowH), 0, static_cast<int>(bars.size()) - 1), cursor = at;
    else if (focused) at = std::clamp(cursor, 0, static_cast<int>(bars.size()) - 1);
    if (at < 0) return;
    const Bar& b = bars[static_cast<size_t>(at)];
    Rect row{r.x - ui.s(4), r.y + rowH * static_cast<float>(at), r.w + ui.s(8), rowH};
    ui.r().drawRoundedRect(row, ui.s(6), kText2.withAlpha(90), 1.0f);
    tip = Tip{};
    tip.show = true;
    tip.anchor = {hovered ? m.x : x0 + (x1 - x0) * std::clamp(b.value / max, 0.0f, 1.0f), row.y};
    tip.title = b.tipTitle.empty() ? b.label : b.tipTitle;
    tip.rows = b.tipRows;
}

void vBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, int& cursor, Tip& tip,
           const std::function<std::string(float)>& yLabel) {
    bool hovered = false;
    ui.hotspot(id, r, &hovered);
    const bool focused = ui.focused(id);
    if (bars.empty()) return;
    const float af = fz(ui, 13), lh = lineH(ui, FontFace::Body, af);
    float vmax = 0.0f;
    for (const auto& b : bars) vmax = std::max(vmax, b.value);
    vmax = std::max(vmax, 4.0f);
    const float top = r.y + lh * 0.5f + ui.s(2), bottom = r.bottom() - lh * 1.5f - ui.s(8);
    std::vector<float> yt = niceTicks(0.0f, vmax, std::clamp(static_cast<int>((bottom - top) / (lh * 2.2f)), 2, 5));
    const float yMax = std::max(vmax, yt.empty() ? vmax : yt.back());
    float lw = 0.0f;
    for (float v : yt) lw = std::max(lw, textW(ui, yLabel ? yLabel(v) : std::to_string(static_cast<int>(v)), FontFace::Body, af));
    Rect plot{r.x + lw + ui.s(10), top, r.right() - ui.s(4) - (r.x + lw + ui.s(10)), bottom - top};
    auto Y = [&](float v) { return plot.y + (1.0f - v / yMax) * plot.h; };
    for (float v : yt) {
        ui.r().drawLine({plot.x, Y(v)}, {plot.right(), Y(v)}, kGrid, 1.0f);
        ui.label(yLabel ? yLabel(v) : std::to_string(static_cast<int>(v)), {plot.x - ui.s(8), Y(v)}, af, ui.theme().textDim, Align::Right,
                 FontFace::Body, VAlign::Middle);
    }
    const float slot = plot.w / static_cast<float>(bars.size());
    const float bw = std::max(ui.s(4), std::min(ui.s(46), slot - ui.s(6)));
    // One label every `step` bars, so that they never touch.
    float widest = 0.0f;
    for (const auto& b : bars) widest = std::max(widest, textW(ui, b.label, FontFace::Body, af));
    int step = 1;
    while (static_cast<float>(step) * slot < widest + ui.s(8) && step < static_cast<int>(bars.size())) ++step;
    for (size_t i = 0; i < bars.size(); ++i) {
        const Bar& b = bars[i];
        float x = plot.x + slot * static_cast<float>(i) + (slot - bw) * 0.5f;
        float y = Y(b.value);
        if (b.value > 0.0f) {
            float h = plot.bottom() - y;
            ui.r().fillRoundedRect({x, y, bw, h}, std::min(ui.s(4), h * 0.5f), b.color);
            if (h > ui.s(8)) ui.r().fillRect({x, y + ui.s(4), bw, h - ui.s(4)}, b.color);
        }
        if (i % static_cast<size_t>(step) == 0)
            ui.label(b.label, {x + bw * 0.5f, plot.bottom() + lh * 0.5f + ui.s(4)}, af, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Top);
    }
    ui.r().drawLine({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, kAxis, 1.0f);
    if (focused) {
        moveCursor(ui, cursor, static_cast<int>(bars.size()));
        focusFrame(ui, r);
    }
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (hovered && m.x >= plot.x) at = std::clamp(static_cast<int>((m.x - plot.x) / slot), 0, static_cast<int>(bars.size()) - 1), cursor = at;
    else if (focused) at = std::clamp(cursor, 0, static_cast<int>(bars.size()) - 1);
    if (at < 0) return;
    const Bar& b = bars[static_cast<size_t>(at)];
    Rect col{plot.x + slot * static_cast<float>(at), plot.y, slot, plot.h};
    ui.r().drawRoundedRect(col, ui.s(6), kText2.withAlpha(90), 1.0f);
    tip = Tip{};
    tip.show = true;
    tip.anchor = {col.center().x, Y(b.value)};
    tip.title = b.tipTitle.empty() ? b.label : b.tipTitle;
    tip.rows = b.tipRows;
}

// ------------------------------------------------------------------ heat band

Color heatColor(float t) {
    static const Color ramp[] = {{42, 33, 24, 255}, {122, 79, 34, 255}, {201, 130, 53, 255}, {255, 179, 71, 255}, {255, 224, 176, 255}};
    t = std::clamp(t, 0.0f, 1.0f) * 4.0f;
    int i = std::min(3, static_cast<int>(t));
    return Color::lerp(ramp[i], ramp[i + 1], t - static_cast<float>(i));
}

void heatBand(UIContext& ui, const std::string& id, const Rect& r, const Heat& h, int& cursor, Tip& tip) {
    bool hovered = false;
    ui.hotspot(id, r, &hovered);
    const bool focused = ui.focused(id);
    const float af = fz(ui, 13), lh = lineH(ui, FontFace::Body, af);
    const float bandH = ui.s(30);
    const float reliefTop = r.y + ui.s(4);
    const float reliefH = std::max(ui.s(20), r.h - bandH - lh - ui.s(24));
    const Rect band{r.x, reliefTop + reliefH + ui.s(8), r.w, bandH};
    const float baseY = reliefTop + reliefH;
    // The relief of the level along its route (from its geometry).
    if (h.relief.size() >= 2) {
        std::vector<Vec2> pts;
        for (size_t i = 0; i < h.relief.size(); ++i)
            pts.push_back({r.x + r.w * static_cast<float>(i) / static_cast<float>(h.relief.size() - 1), baseY - std::clamp(h.relief[i], 0.0f, 1.0f) * reliefH * 0.85f});
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        for (size_t i = 0; i < pts.size(); ++i) {
            v.push_back(vtx(pts[i], Color{31, 42, 68, 255}));
            v.push_back(vtx({pts[i].x, baseY}, Color{31, 42, 68, 255}));
            if (i > 0) {
                int a = static_cast<int>(2 * (i - 1));
                idx.insert(idx.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
            }
        }
        ui.r().drawGeometry(nullptr, v, idx);
        for (size_t i = 1; i < pts.size(); ++i) ui.r().drawLine(pts[i - 1], pts[i], Color{75, 90, 128, 255}, std::max(1.0f, ui.s(1.5f)));
    } else {
        const std::string wait = "Relief du niveau en préparation…";
        float f = ui.fitFont(wait, FontFace::Body, af, r.w - ui.s(20), 0.8f);
        ui.label(ui.elide(wait, FontFace::Body, f, r.w - ui.s(20)), {r.center().x, reliefTop + reliefH * 0.5f}, f, ui.theme().textDim, Align::Center,
                 FontFace::Body, VAlign::Middle);
    }
    // Its checkpoints: a dashed line and a small flag.
    for (float c : h.checkpoints) {
        float x = r.x + r.w * std::clamp(c, 0.0f, 1.0f);
        dashedV(ui, x, reliefTop + ui.s(4), baseY, ui.theme().accent.withAlpha(130));
        ui.r().fillTriangle({x, reliefTop + ui.s(4)}, {x + ui.s(9), reliefTop + ui.s(8)}, {x, reliefTop + ui.s(12)}, ui.theme().accent.withAlpha(210));
    }
    // The 40 slices.
    const float cw = r.w / static_cast<float>(Heat::kCells);
    const float mx = h.maxValue > 0.0f ? h.maxValue : 1.0f;
    for (int i = 0; i < Heat::kCells; ++i) {
        Rect c{r.x + cw * static_cast<float>(i) + 1.0f, band.y, std::max(1.0f, cw - 2.0f), band.h};
        float v = h.values[static_cast<size_t>(i)];
        if (v >= 0.0f) ui.r().fillRoundedRect(c, ui.s(2), heatColor(v / mx));
        else ui.r().drawRoundedRect(c, ui.s(2), kAxis, 1.0f);
    }
    // « Départ » and « Arrivée », when both fit.
    const float ly = band.bottom() + ui.s(6);
    const float w1 = textW(ui, "Départ", FontFace::Body, af), w2 = textW(ui, "Arrivée", FontFace::Body, af);
    if (w1 + w2 + ui.s(20) < r.w) {
        ui.label("Départ", {r.x, ly}, af, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Top);
        ui.label("Arrivée", {r.right(), ly}, af, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Top);
    }
    if (focused) {
        moveCursor(ui, cursor, Heat::kCells);
        focusFrame(ui, r);
    }
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (hovered && m.y <= band.bottom()) at = std::clamp(static_cast<int>((m.x - r.x) / cw), 0, Heat::kCells - 1), cursor = at;
    else if (focused) at = std::clamp(cursor, 0, Heat::kCells - 1);
    if (at < 0) return;
    Rect c{r.x + cw * static_cast<float>(at), band.y - ui.s(2), cw, band.h + ui.s(4)};
    ui.r().drawRoundedRect(c, ui.s(3), ui.theme().text, std::max(1.0f, ui.s(1.5f)));
    tip = Tip{};
    tip.show = true;
    tip.anchor = {c.center().x, band.y};
    const int a = static_cast<int>(std::lround(at * 2.5f)), b = static_cast<int>(std::lround((at + 1) * 2.5f));
    tip.title = "Tranche " + std::to_string(a) + "–" + std::to_string(b) + " %";
    const size_t k = static_cast<size_t>(at);
    tip.rows.push_back({{}, "Passages", std::to_string(h.passes[k])});
    tip.rows.push_back({{}, "Morts", std::to_string(h.deaths[k])});
    if (h.values[k] >= 0.0f) tip.rows.push_back({{}, "Difficulté (récente)", std::to_string(static_cast<int>(std::lround(h.values[k] * 100.0f))) + " %"});
}

// ------------------------------------------------------------------ table

void table(UIContext& ui, const std::string& id, const Rect& r, const std::vector<std::string>& head,
           const std::vector<std::vector<std::string>>& rows, int& page) {
    const float f = fz(ui, 13), rowH = ui.s(26), pagerH = ui.s(36);
    const size_t cols = head.size();
    if (cols == 0) return;
    // Columns: their natural width (measured), shrunk together when the table is too narrow.
    std::vector<float> w(cols, 0.0f);
    for (size_t c = 0; c < cols; ++c) w[c] = textW(ui, head[c], FontFace::Bold, f);
    for (const auto& row : rows)
        for (size_t c = 0; c < cols && c < row.size(); ++c) w[c] = std::max(w[c], textW(ui, row[c], FontFace::Body, f));
    float total = 0.0f;
    for (auto& x : w) total += (x += ui.s(18));
    const float k = total > r.w ? r.w / total : 1.0f;
    for (auto& x : w) x *= k;
    if (k >= 1.0f) w.back() += r.w - total;
    const int per = std::max(1, static_cast<int>((r.h - rowH - pagerH - ui.s(6)) / rowH));
    const int pages = std::max(1, (static_cast<int>(rows.size()) + per - 1) / per);
    page = std::clamp(page, 0, pages - 1);
    ui.r().fillRect({r.x, r.y, r.w, rowH}, Color{20, 28, 48, 255});
    float x = r.x;
    for (size_t c = 0; c < cols; ++c) {
        ui.label(ui.elide(head[c], FontFace::Bold, f, w[c] - ui.s(12)), {x + ui.s(8), r.y + rowH * 0.5f}, f, ui.theme().text, Align::Left, FontFace::Bold,
                 VAlign::Middle);
        x += w[c];
    }
    const int first = page * per, last = std::min(static_cast<int>(rows.size()), first + per);
    for (int i = first; i < last; ++i) {
        const float y = r.y + rowH * static_cast<float>(i - first + 1);
        if ((i - first) % 2) ui.r().fillRect({r.x, y, r.w, rowH}, Color{16, 23, 40, 200});
        x = r.x;
        const auto& row = rows[static_cast<size_t>(i)];
        for (size_t c = 0; c < cols && c < row.size(); ++c) {
            ui.label(ui.elide(row[c], FontFace::Body, f, w[c] - ui.s(12)), {x + ui.s(8), y + rowH * 0.5f}, f, kText2, Align::Left, FontFace::Body,
                     VAlign::Middle);
            x += w[c];
        }
    }
    if (rows.empty()) ui.label("Aucune donnée", {r.x + ui.s(8), r.y + rowH * 1.5f}, f, ui.theme().textDim, Align::Left, FontFace::Body, VAlign::Middle);
    // The pages: ‹ › and where we are.
    Rect bar{r.x, r.bottom() - pagerH, r.w, pagerH};
    UIContext::ButtonStyle st;
    st.small = true;
    st.enabled = page > 0;
    const float bwid = ui.s(52);
    if (ui.button(id + "/prev", {bar.x, bar.y + ui.s(2), bwid, pagerH - ui.s(4)}, "‹", st)) page = std::max(0, page - 1);
    st.enabled = page + 1 < pages;
    if (ui.button(id + "/next", {bar.right() - bwid, bar.y + ui.s(2), bwid, pagerH - ui.s(4)}, "›", st)) page = std::min(pages - 1, page + 1);
    std::string where = rows.empty() ? std::string() : "Lignes " + std::to_string(first + 1) + "–" + std::to_string(last) + " sur " + std::to_string(rows.size());
    const float room = bar.w - 2 * bwid - ui.s(24);
    if (!where.empty() && room > ui.s(40)) {
        float wf = ui.fitFont(where, FontFace::Body, f, room, 0.8f);
        ui.label(ui.elide(where, FontFace::Body, wf, room), bar.center(), wf, ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Middle);
    }
}

}  // namespace cosmic::charts
