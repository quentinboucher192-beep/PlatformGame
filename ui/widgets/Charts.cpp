#include "ui/widgets/Charts.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cmath>

#include "engine/core/I18n.h"
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
float lineH(UIContext& ui, FontFace f, float size) {
    i18n::Verbatim probe;  // a line's height, not a text (never translated, never judged)
    return ui.text().measure("Ag", f, ui.s(size)).y;
}

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

/// A dashed line through `p` (thinner: a reference line).
void dashedPolyline(UIContext& ui, const std::vector<Vec2>& p, Color c) {
    const float th = std::max(1.0f, ui.s(1.5f)), dash = std::max(2.0f, ui.s(5)), period = 2.0f * dash;
    float along = 0.0f;  // where the segment starts along the whole line: the dashes go on across its corners
    for (size_t i = 1; i < p.size(); ++i) {
        const Vec2 d = p[i] - p[i - 1];
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (!(len > 0.0f) || !std::isfinite(len)) continue;
        const Vec2 u{d.x / len, d.y / len};
        // Whole dashes from the period's start before this segment: every step moves on by half a dash at least.
        const float first = std::floor(along / period) * period - along;  // <= 0
        for (float s0 = first; s0 < len; s0 += period) {
            const float a = std::max(0.0f, s0), b = std::min(len, s0 + dash);
            if (b > a) ui.r().drawLine(p[i - 1] + u * a, p[i - 1] + u * b, c, th);
        }
        along += len;
    }
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

namespace {

/// The part of `r` the clip in force lets show (the page's scroll region): empty when scrolled out of sight.
Rect visiblePart(UIContext& ui, const Rect& r) {
    SDL_Renderer* sr = ui.r().sdl();
    if (!sr || !SDL_RenderClipEnabled(sr)) return r;
    SDL_Rect c{};
    if (!SDL_GetRenderClipRect(sr, &c)) return r;
    return r.intersection({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.w), static_cast<float>(c.h)});
}

/// The tooltip's measures: the width of its text, its title's and rows' line heights.
struct TipMetrics {
    float pad, tf, rf, th, rh, sw, swGap, w, bw, bh;
};
TipMetrics tipMetrics(UIContext& ui, const Tip& t) {
    TipMetrics k{};
    k.pad = ui.s(10);
    k.tf = fz(ui, 12);
    k.rf = fz(ui, 13);
    k.th = t.title.empty() ? 0.0f : lineH(ui, FontFace::Body, k.tf) + ui.s(3);
    k.rh = lineH(ui, FontFace::Body, k.rf) + ui.s(3);
    k.sw = ui.s(8);
    k.swGap = ui.s(7);
    // The widest row (its swatch, its label, a gap, its value) sets the width.
    float w = t.title.empty() ? 0.0f : textW(ui, t.title, FontFace::Body, k.tf);
    for (const auto& r : t.rows) {
        float rw = (r.swatch.a ? k.sw + k.swGap : 0.0f) + textW(ui, r.label, FontFace::Body, k.rf) +
                   (r.value.empty() ? 0.0f : ui.s(14) + textW(ui, r.value, FontFace::Bold, k.rf));
        w = std::max(w, rw);
    }
    const float maxW = ui.screenW() * 0.45f;
    k.w = std::min(w + ui.s(2), maxW);  // a little slack: the measured widths are rounded
    k.bw = k.w + 2 * k.pad;
    k.bh = 2 * k.pad + k.th + k.rh * static_cast<float>(t.rows.size());
    return k;
}

}  // namespace

Pointing pointing(UIContext& ui, const std::string& id, const Rect& r) {
    bool hovered = false;
    ui.hotspot(id, r, &hovered);
    Pointing p;
    if (!ui.acceptsInput()) return p;  // under a dialog: nothing
    p.mouse = hovered && ui.usingPointer();
    p.keys = !ui.usingPointer() && ui.focused(id);
    return p;
}

bool showTip(UIContext& ui, Tip& tip, Vec2 anchor, const Rect& item) {
    const Rect shown = visiblePart(ui, item);
    if (shown.empty()) return false;
    tip = Tip{};
    tip.show = true;
    tip.item = shown;
    tip.anchor = {std::clamp(anchor.x, shown.x, shown.right()), std::clamp(anchor.y, shown.y, shown.bottom())};
    return true;
}

Rect tipBox(UIContext& ui, const Tip& t) {
    if (!t.show) return {};
    const TipMetrics k = tipMetrics(ui, t);
    const float bw = k.bw, bh = k.bh, mg = ui.s(8), gap = ui.s(10);
    const Rect screen{0.0f, 0.0f, ui.screenW(), ui.screenH()};
    const Rect b = t.bounds.empty() ? screen : t.bounds.intersection(screen);
    const Rect lim{b.x + mg, b.y + mg, std::max(0.0f, b.w - 2 * mg), std::max(0.0f, b.h - 2 * mg)};
    const Rect item = t.item.empty() ? Rect{t.anchor.x, t.anchor.y, 0.0f, 0.0f} : t.item;
    auto clampX = [&](float x) { return std::clamp(x, lim.x, std::max(lim.x, lim.right() - bw)); };
    auto clampY = [&](float y) { return std::clamp(y, lim.y, std::max(lim.y, lim.bottom() - bh)); };
    // Its left from the anchor (at its right, else at its left), inside.
    float x = t.anchor.x + ui.s(14);
    if (x + bw > lim.right()) x = t.anchor.x - ui.s(14) - bw;
    x = clampX(x);
    // Above the item, else under it.
    const float above = item.y - gap - bh, below = t.item.empty() ? t.anchor.y + ui.s(16) : item.bottom() + gap;
    if (above >= lim.y) return {x, above, bw, bh};
    if (below + bh <= lim.bottom()) return {x, below, bw, bh};
    // Beside it (at its right, else at its left), at the anchor's height.
    const float y = clampY(t.anchor.y - bh * 0.5f);
    if (item.right() + gap + bw <= lim.right()) return {item.right() + gap, y, bw, bh};
    if (item.x - gap - bw >= lim.x) return {item.x - gap - bw, y, bw, bh};
    // No room around it (a box taller than the room left): where it covers the least of it.
    const Rect cand[4] = {{x, clampY(above), bw, bh}, {x, clampY(below), bw, bh}, {clampX(item.right() + gap), y, bw, bh}, {clampX(item.x - gap - bw), y, bw, bh}};
    Rect best = cand[0];
    float least = 1e30f;
    for (const Rect& c : cand) {
        const Rect o = c.intersection(item);
        const float a = o.w * o.h;
        if (a < least) least = a, best = c;
    }
    return best;
}

Rect drawTip(UIContext& ui, const Tip& t) {
    if (!t.show) return {};
    const TipMetrics k = tipMetrics(ui, t);
    const float pad = k.pad, tf = k.tf, rf = k.rf, th = k.th, rh = k.rh, sw = k.sw, swGap = k.swGap, w = k.w;
    const Rect box = tipBox(ui, t);
    const float x = box.x, y = box.y;
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
    return box;
}

// ------------------------------------------------------------------ curves

void lineChart(UIContext& ui, const std::string& id, const Rect& r, const LineSpec& s, int& cursor, Tip& tip) {
    std::vector<float> xs;
    for (const auto& se : s.series)
        for (const auto& p : se.pts) xs.push_back(p.x);
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    const Pointing pt = pointing(ui, id, r);
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
    auto endText = [](const Series& se) -> const std::string& { return se.endLabel.empty() ? se.name : se.endLabel; };
    float rightPad = ui.s(10);
    if (s.endLabels)
        for (const auto& se : s.series)
            if (se.kind != Series::Kind::Dots && !se.pts.empty()) rightPad = std::max(rightPad, textW(ui, endText(se), FontFace::Bold, lf) + ui.s(22));
    const float markRoom = s.marks.empty() ? 0.0f : ui.s(22);  // 2.91: the marks above their point
    Rect plot{r.x + lw + ui.s(10), top + markRoom, 0.0f, bottom - top - markRoom};
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
    // 2.91: the bands between two step curves (each x where either changes: the area between them).
    for (const auto& b : s.bands) {
        if (b.upper >= s.series.size() || b.lower >= s.series.size()) continue;
        const auto& up = s.series[b.upper].pts;
        const auto& lo = s.series[b.lower].pts;
        if (up.empty() || lo.empty()) continue;
        std::vector<float> bx;
        for (const auto& p : up) bx.push_back(p.x);
        for (const auto& p : lo) bx.push_back(p.x);
        std::sort(bx.begin(), bx.end());
        bx.erase(std::unique(bx.begin(), bx.end()), bx.end());
        auto valueAt = [](const std::vector<Vec2>& pts, float x, float& out) {
            bool any = false;
            for (const auto& p : pts) {
                if (p.x > x + 1e-4f) break;
                out = p.y;
                any = true;
            }
            return any;
        };
        const float endX = std::min(up.back().x, lo.back().x);
        for (size_t i = 0; i + 1 < bx.size(); ++i) {
            if (bx[i] >= endX) break;
            float yu = 0.0f, yl = 0.0f;
            if (!valueAt(up, bx[i], yu) || !valueAt(lo, bx[i], yl)) continue;
            const float x0 = X(bx[i]), x1 = X(std::min(bx[i + 1], endX));
            const float y0 = std::min(Y(yu), Y(yl)), y1 = std::max(Y(yu), Y(yl));
            if (x1 > x0 && y1 > y0) ui.r().fillRect({x0, y0, x1 - x0, y1 - y0}, b.fill);
        }
    }
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
        if (se.dashed) dashedPolyline(ui, path, se.color);
        else polyline(ui, path, se.color);
    }
    ui.r().popClip();
    // 2.91: the marks over their points.
    for (const auto& mk : s.marks) {
        if (!mk.draw) continue;
        const float sz = ui.s(20);
        mk.draw(Rect::fromCenter({X(mk.at.x), Y(mk.at.y) - ui.s(8) - sz * 0.5f}, {sz, sz}));
    }
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
            float w = textW(ui, endText(se), FontFace::Bold, lf);
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
            ui.label(endText(*l.se), {l.box.x, l.box.y}, lf, kText2, Align::Left, FontFace::Bold, VAlign::Top);
        }
    }
    // The cursor: the mouse over the chart, or ← → when it has the focus (keyboard, gamepad).
    if (pt.keys) moveCursor(ui, cursor, static_cast<int>(xs.size()));
    if (pt.any()) focusFrame(ui, r);
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse && m.x >= plot.x - ui.s(12) && m.x <= plot.right() + ui.s(12)) {
        float bestD = 1e9f;
        for (size_t i = 0; i < xs.size(); ++i)
            if (float d = std::abs(X(xs[i]) - m.x); d < bestD) {
                bestD = d;
                at = static_cast<int>(i);
            }
        cursor = at;
    } else if (pt.keys) {
        at = std::clamp(cursor, 0, static_cast<int>(xs.size()) - 1);
    }
    if (at < 0 || !s.tip) return;
    const float xv = xs[static_cast<size_t>(at)];
    const float px = X(xv);
    dashedV(ui, px, plot.y, plot.bottom(), kText2.withAlpha(150));
    float topY = plot.bottom(), lowY = plot.y;
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
        lowY = std::max(lowY, c.y);
    }
    // The cursor's points: the box keeps clear of them.
    if (lowY < topY) lowY = topY;
    const Rect item{px - ui.s(7), topY - ui.s(7), ui.s(14), lowY - topY + ui.s(14)};
    if (showTip(ui, tip, {px, topY}, item)) s.tip(xv, tip);
}

// ------------------------------------------------------------------ bars

float hBarsHeight(const UIContext& ui, size_t n) { return ui.s(30) * static_cast<float>(n); }

void hBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, float maxValue, int& cursor, Tip& tip) {
    const Pointing pt = pointing(ui, id, r);
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
    if (pt.keys) moveCursor(ui, cursor, static_cast<int>(bars.size()));
    if (pt.any()) focusFrame(ui, r);
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse) at = std::clamp(static_cast<int>((m.y - r.y) / rowH), 0, static_cast<int>(bars.size()) - 1), cursor = at;
    else if (pt.keys) at = std::clamp(cursor, 0, static_cast<int>(bars.size()) - 1);
    if (at < 0) return;
    const Bar& b = bars[static_cast<size_t>(at)];
    Rect row{r.x - ui.s(4), r.y + rowH * static_cast<float>(at), r.w + ui.s(8), rowH};
    ui.r().drawRoundedRect(row, ui.s(6), kText2.withAlpha(90), 1.0f);
    if (!showTip(ui, tip, {pt.mouse ? m.x : x0 + (x1 - x0) * std::clamp(b.value / max, 0.0f, 1.0f), row.y}, row)) return;
    tip.title = b.tipTitle.empty() ? b.label : b.tipTitle;
    tip.rows = b.tipRows;
}

void vBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Bar>& bars, int& cursor, Tip& tip,
           const std::function<std::string(float)>& yLabel) {
    const Pointing pt = pointing(ui, id, r);
    if (bars.empty()) return;
    const float af = fz(ui, 13), lh = lineH(ui, FontFace::Body, af);
    float vmax = 0.0f;
    bool tops = false, subs = false;  // 2.91: texts above the bars, second lines under their labels
    for (const auto& b : bars) {
        vmax = std::max(vmax, b.value);
        tops = tops || !b.topText.empty();
        subs = subs || !b.subLabel.empty();
    }
    vmax = std::max(vmax, 4.0f);
    const float tf = fz(ui, 14), th = lineH(ui, FontFace::Bold, tf);
    const float top = r.y + lh * 0.5f + ui.s(2) + (tops ? th + ui.s(8) : 0.0f);
    const float bottom = r.bottom() - lh * 1.5f - ui.s(8) - (subs ? lh + ui.s(2) : 0.0f);
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
    const float bw = std::max(ui.s(4), tops ? std::min(ui.s(150), slot * 0.7f) : std::min(ui.s(46), slot - ui.s(6)));
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
        const float room = static_cast<float>(step) * slot - ui.s(8);
        if (i % static_cast<size_t>(step) == 0) {
            ui.label(ui.elide(b.label, FontFace::Body, af, room), {x + bw * 0.5f, plot.bottom() + lh * 0.5f + ui.s(4)}, af,
                     tops ? ui.theme().text : ui.theme().textDim, Align::Center, FontFace::Body, VAlign::Top);
            if (!b.subLabel.empty())
                ui.label(ui.elide(b.subLabel, FontFace::Body, af, room), {x + bw * 0.5f, plot.bottom() + lh * 1.5f + ui.s(6)}, af, ui.theme().textDim,
                         Align::Center, FontFace::Body, VAlign::Top);
        }
        if (!b.topText.empty()) {
            const float f = ui.fitFont(b.topText, FontFace::Bold, tf, slot - ui.s(6), 0.75f);
            ui.label(ui.elide(b.topText, FontFace::Bold, f, slot - ui.s(6)), {x + bw * 0.5f, y - ui.s(6)}, f, ui.theme().text, Align::Center,
                     FontFace::Bold, VAlign::Bottom);
        }
    }
    ui.r().drawLine({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, kAxis, 1.0f);
    if (pt.keys) moveCursor(ui, cursor, static_cast<int>(bars.size()));
    if (pt.any()) focusFrame(ui, r);
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse && m.x >= plot.x) at = std::clamp(static_cast<int>((m.x - plot.x) / slot), 0, static_cast<int>(bars.size()) - 1), cursor = at;
    else if (pt.keys) at = std::clamp(cursor, 0, static_cast<int>(bars.size()) - 1);
    if (at < 0) return;
    const Bar& b = bars[static_cast<size_t>(at)];
    Rect col{plot.x + slot * static_cast<float>(at), plot.y, slot, plot.h};
    ui.r().drawRoundedRect(col, ui.s(6), kText2.withAlpha(90), 1.0f);
    // The column lit (its bar, the words above it): the box keeps clear of it.
    const float itemTop = std::min(col.y, Y(b.value) - (tops ? th + ui.s(8) : 0.0f));
    const Rect item{col.x, itemTop, col.w, col.bottom() - itemTop};
    if (!showTip(ui, tip, {col.center().x, Y(b.value)}, item)) return;
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
    const Pointing pt = pointing(ui, id, r);
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
    if (pt.keys) moveCursor(ui, cursor, Heat::kCells);
    if (pt.any()) focusFrame(ui, r);
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse && m.y <= band.bottom()) at = std::clamp(static_cast<int>((m.x - r.x) / cw), 0, Heat::kCells - 1), cursor = at;
    else if (pt.keys) at = std::clamp(cursor, 0, Heat::kCells - 1);
    if (at < 0) return;
    Rect c{r.x + cw * static_cast<float>(at), band.y - ui.s(2), cw, band.h + ui.s(4)};
    ui.r().drawRoundedRect(c, ui.s(3), ui.theme().text, std::max(1.0f, ui.s(1.5f)));
    if (!showTip(ui, tip, {c.center().x, band.y}, c)) return;
    const int a = static_cast<int>(std::lround(at * 2.5f)), b = static_cast<int>(std::lround((at + 1) * 2.5f));
    tip.title = "Tranche " + std::to_string(a) + "–" + std::to_string(b) + " %";
    const size_t k = static_cast<size_t>(at);
    tip.rows.push_back({kNoSwatch, "Passages", std::to_string(h.passes[k])});
    tip.rows.push_back({kNoSwatch, "Morts", std::to_string(h.deaths[k])});
    if (h.values[k] >= 0.0f) tip.rows.push_back({kNoSwatch, "Difficulté (récente)", std::to_string(static_cast<int>(std::lround(h.values[k] * 100.0f))) + " %"});
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

// ------------------------------------------------------------------ 2.91

void sparkline(UIContext& ui, const Rect& r, const std::vector<float>& v, Color color) {
    if (v.empty() || r.w <= 2.0f || r.h <= 2.0f) return;
    auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    const float span = *hi - *lo;
    const float pad = ui.s(5);
    const Rect in{r.x + ui.s(2), r.y + pad, r.w - ui.s(2) - pad * 1.6f, r.h - 2 * pad};
    std::vector<Vec2> pts;
    for (size_t i = 0; i < v.size(); ++i) {
        const float x = in.x + (v.size() > 1 ? in.w * static_cast<float>(i) / static_cast<float>(v.size() - 1) : in.w);
        const float y = span > 1e-6f ? in.bottom() - (v[i] - *lo) / span * in.h : in.center().y;
        pts.push_back({x, y});
    }
    polyline(ui, pts, color);
    const Vec2 e = pts.back();
    ui.r().drawRing(e, ui.s(7), std::max(1.0f, ui.s(1.6f)), color.withAlpha(100));
    ui.r().fillCircle(e, ui.s(4.2f), kSurface);
    ui.r().fillCircle(e, ui.s(3.2f), color);
}

Color heatOver(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float a = std::clamp(1.1f * t - 0.18f, 0.0f, 0.92f);
    return {255, static_cast<uint8_t>(200.0f - 150.0f * t), static_cast<uint8_t>(90.0f - 30.0f * t), static_cast<uint8_t>(a * 255.0f)};
}

Color heatSolid(float t) {
    // heatOver laid over the card's dark surface.
    const Color o = heatOver(t), base{26, 30, 46, 255};
    return Color::lerp(base, o.withAlpha(255), std::max(0.12f, o.a / 255.0f));
}

float iconBarsHeight(const UIContext& ui, size_t n, const IconBarsStyle& st) { return ui.s(st.rowH) * static_cast<float>(n); }

void iconBars(UIContext& ui, const std::string& id, const Rect& r, const std::vector<IconBar>& bars, float maxValue, int& cursor, Tip& tip,
              const IconBarsStyle& st) {
    const Pointing pt = pointing(ui, id, r);
    if (bars.empty()) return;
    const float rowH = ui.s(st.rowH), iw = ui.s(st.iconW), ih = std::min(ui.s(st.iconH), rowH - ui.s(4));
    const float nf = fz(ui, 15), vf = fz(ui, 17), cf = fz(ui, 13);
    float vw = 0.0f, cw = 0.0f;
    for (const auto& b : bars) {
        vw = std::max(vw, textW(ui, b.valueText, FontFace::Bold, vf));
        cw = std::max(cw, textW(ui, b.countText, FontFace::Body, cf));
    }
    // Columns from the right: the count, the value; the name and its bar in what is left.
    const float countX = r.right();
    const float valueX = countX - (cw > 0.0f ? cw + ui.s(14) : 0.0f);
    const float barL = r.x + (st.iconW > 0.0f ? iw + ui.s(12) : 0.0f);
    const float barR = valueX - vw - ui.s(14);
    const float max = maxValue > 0.0f ? maxValue : 1.0f;
    const float nh = lineH(ui, FontFace::Body, nf);
    int at = -1;
    if (pt.keys) moveCursor(ui, cursor, static_cast<int>(bars.size()));
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse) at = std::clamp(static_cast<int>((m.y - r.y) / rowH), 0, static_cast<int>(bars.size()) - 1), cursor = at;
    else if (pt.keys) at = std::clamp(cursor, 0, static_cast<int>(bars.size()) - 1);
    for (size_t i = 0; i < bars.size(); ++i) {
        const IconBar& b = bars[i];
        const Rect row{r.x, r.y + rowH * static_cast<float>(i), r.w, rowH};
        if (static_cast<int>(i) == at) {
            ui.r().fillRoundedRect(row.expanded(ui.s(3)), ui.s(8), ui.theme().accent.withAlpha(18));
            ui.r().drawRoundedRect(row.expanded(ui.s(3)), ui.s(8), ui.theme().accent.withAlpha(100), std::max(1.0f, ui.s(1.0f)));
        }
        if (st.iconW > 0.0f) {
            const Rect box{row.x, row.center().y - ih * 0.5f, iw, ih};
            if (st.iconFrame) {
                ui.r().fillRoundedRect(box, ui.s(7), Color{5, 8, 18, 180});
                ui.r().drawRoundedRect(box, ui.s(7), Color{38, 53, 90, 255}, std::max(1.0f, ui.s(1.0f)));
            }
            if (b.icon) {
                ui.r().pushClip(box);
                b.icon(st.iconFrame ? box.expanded(-ui.s(3)) : box);
                ui.r().popClip();
            }
        }
        const float room = std::max(ui.s(20), barR - barL);
        const float ny = row.y + (rowH - nh - ui.s(16)) * 0.5f;
        ui.label(ui.elide(b.label, FontFace::Body, nf, room), {barL, ny}, nf, ui.theme().text);
        const Rect track{barL, ny + nh + ui.s(5), room, ui.s(9)};
        ui.r().fillRoundedRect(track, ui.s(4), Color{26, 36, 66, 255});
        if (b.value > 0.0f)
            ui.r().fillRoundedRect({track.x, track.y, std::max(ui.s(6), track.w * std::clamp(b.value / max, 0.0f, 1.0f)), track.h}, ui.s(4), b.color);
        ui.label(b.valueText, {valueX, row.center().y}, vf, ui.theme().text, Align::Right, FontFace::Bold, VAlign::Middle);
        if (!b.countText.empty()) ui.label(b.countText, {countX, row.center().y}, cf, ui.theme().textDim, Align::Right, FontFace::Body, VAlign::Middle);
    }
    if (pt.any()) focusFrame(ui, r);
    if (at < 0) return;
    const IconBar& b = bars[static_cast<size_t>(at)];
    const Rect row{r.x, r.y + rowH * static_cast<float>(at), r.w, rowH};
    if (!showTip(ui, tip, {pt.mouse ? m.x : barL + (barR - barL) * std::clamp(b.value / max, 0.0f, 1.0f), row.y}, row.expanded(ui.s(3)))) return;
    tip.title = b.tipTitle.empty() ? b.label : b.tipTitle;
    tip.rows = b.tipRows;
}

void stackedBar(UIContext& ui, const std::string& id, const Rect& r, const std::vector<Part>& parts, int& cursor, Tip& tip) {
    const Pointing pt = pointing(ui, id, r);
    float total = 0.0f;
    for (const auto& p : parts) total += std::max(0.0f, p.value);
    if (parts.empty() || total <= 0.0f) {
        ui.r().fillRoundedRect(r, ui.s(6), kTrack);
        return;
    }
    const float f = fz(ui, 14);
    std::vector<Rect> cells;
    float x = r.x;
    for (size_t i = 0; i < parts.size(); ++i) {
        const float w = r.w * std::max(0.0f, parts[i].value) / total;
        cells.push_back({x, r.y, w, r.h});
        x += w;
    }
    ui.r().pushClip(r);
    // The whole in the last part's colour (its rounded right end), the first part rounded at the left,
    // the others square; a thin seam between two parts.
    const float rad = ui.s(6);
    ui.r().fillRoundedRect(r, rad, parts.back().color);
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const Rect& c = cells[i];
        if (c.w <= 0.0f) continue;
        if (i == 0) ui.r().fillRoundedRect({c.x, c.y, c.w + std::min(rad, cells[1].w), c.h}, rad, parts[i].color);
        else ui.r().fillRect(c, parts[i].color);
    }
    for (size_t i = 1; i < parts.size(); ++i)
        if (cells[i].w > 0.0f) ui.r().fillRect({cells[i].x, cells[i].y, std::max(1.0f, ui.s(1.5f)), cells[i].h}, Color{8, 11, 24, 255});
    for (size_t i = 0; i < parts.size(); ++i) {
        const Rect& c = cells[i];
        if (c.w <= 0.0f) continue;
        // Its text inside when it fits (white on the colour).
        const float room = c.w - ui.s(12);
        if (!parts[i].text.empty() && room > ui.s(24)) {
            const float tf = ui.fitFont(parts[i].text, FontFace::Bold, f, room, 0.8f);
            if (textW(ui, parts[i].text, FontFace::Bold, tf) <= room)
                ui.label(parts[i].text, c.center(), tf, Color::white(), Align::Center, FontFace::Bold, VAlign::Middle);
        }
    }
    ui.r().popClip();
    if (pt.keys) moveCursor(ui, cursor, static_cast<int>(parts.size()));
    if (pt.any()) focusFrame(ui, r);
    int at = -1;
    const Vec2 m = ui.input().mousePos();
    if (pt.mouse) {
        for (size_t i = 0; i < cells.size(); ++i)
            if (m.x >= cells[i].x && m.x < cells[i].right() + 0.5f) at = static_cast<int>(i);
        if (at >= 0) cursor = at;
    } else if (pt.keys) {
        at = std::clamp(cursor, 0, static_cast<int>(parts.size()) - 1);
    }
    if (at < 0) return;
    const Rect& c = cells[static_cast<size_t>(at)];
    ui.r().drawRoundedRect(c.expanded(ui.s(2)), ui.s(5), ui.theme().text, std::max(1.0f, ui.s(1.5f)));
    if (!showTip(ui, tip, {c.center().x, c.y}, c.expanded(ui.s(2)))) return;
    tip.title = parts[static_cast<size_t>(at)].tipTitle;
    tip.rows = parts[static_cast<size_t>(at)].tipRows;
}

}  // namespace cosmic::charts
