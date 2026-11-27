// ADMINISTRATION › TAS: the training curve of the row selected (docs/TAS.md « Courbe
// d'entraînement »), under the table. At the left, one axis of time along the cumulative training
// time of the version:
//   * the best time verified, a step line going down, a dot at each improvement (a ring: the run
//     the training started from), a diamond at each run published; the shipped and the online
//     times as dashed reference lines, labelled at their right;
//   * the attempts of each window behind it: a thin whisker from the best to the worst time of
//     those that finished, a tick at their median;
//   * the rounds of cosmic_tas marked under the axis, and a strip of the sections the attempts of
//     each window reached (the darker, the fewer);
// at the right, the counts of the whole training and of the round (or the last one): attempts,
// finished and their share, best / median / worst time, the dead ones, and where the others
// stopped, section by section. Every point and window has its tooltip (the mouse; ← → on the
// curve or the strip with the keyboard or the gamepad). Everything is measured: no text over
// another, none out of its box.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "engine/core/I18n.h"
#include "engine/input/InputManager.h"
#include "engine/rendering/Renderer.h"
#include "game/GameServices.h"
#include "game/online/LevelFingerprint.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasTraining.h"
#include "ui/screens/AdminTas.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

using namespace tasadmin;

namespace {

const Color kGrid{33, 43, 66, 255};
const Color kAxis{61, 73, 102, 255};
const Color kSurface{14, 21, 42, 255};

/// « −0,042 s » (a gain in seconds).
std::string gainText(int64_t ms) {
    char b[32];
    std::snprintf(b, sizeof b, "−%.3f s", static_cast<double>(std::max<int64_t>(0, ms)) / 1000.0);
    std::string s = b;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

/// « 45 % » of a ‰ of the route.
std::string routeText(int permille) {
    char b[16];
    std::snprintf(b, sizeof b, "%d %%", (std::clamp(permille, 0, 1000) + 5) / 10);
    return b;
}

void diamond(Renderer& r, Vec2 c, float s, Color fill, Color edge) {
    r.fillConvexPolygon({{c.x, c.y - s - 1.5f}, {c.x + s + 1.5f, c.y}, {c.x, c.y + s + 1.5f}, {c.x - s - 1.5f, c.y}}, edge);
    r.fillConvexPolygon({{c.x, c.y - s}, {c.x + s, c.y}, {c.x, c.y + s}, {c.x - s, c.y}}, fill);
}

void dashedH(Renderer& r, float x0, float x1, float y, Color c, float dash, float th) {
    for (float x = x0; x < x1; x += dash * 2.0f) r.drawLine({x, y}, {std::min(x1, x + dash), y}, c, th);
}

/// The keyboard cursor of a focused chart: ← → (the focus does not move that way).
void moveCursor(UIContext& ui, int& cursor, int count) {
    if (count <= 0) return;
    if (cursor < 0 || cursor >= count) cursor = count - 1;
    if (!ui.acceptsInput()) return;
    InputManager& in = ui.input();
    if (in.menuPressed(Action::MenuLeft)) cursor = std::max(0, cursor - 1);
    if (in.menuPressed(Action::MenuRight)) cursor = std::min(count - 1, cursor + 1);
    ui.consumeNavigation(true, false);
}

/// The word of the attempts that broke the category (a death; a blow too in « Sans dégât »).
const char* brokenWord(RunCategory c) { return c == RunCategory::NoDamage ? "Morts ou touchés" : "Morts"; }

}  // namespace

void AdminTas::drawTooltip() { charts::drawTip(g_.ui, tip_); }

void AdminTas::drawDetail(const Row& row, const Rect& area) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    Renderer& R = ui.r();
    tip_ = charts::Tip{};
    layout_.detail = area;
    R.fillRoundedRect(area, ui.s(10), Color{14, 21, 42, 245});
    R.drawRoundedRect(area, ui.s(10), th.accent.withAlpha(110), std::max(1.0f, ui.s(1.0f)));
    const float pad = ui.s(14);
    const Rect in{area.x + pad, area.y + ui.s(10), area.w - 2 * pad, area.h - ui.s(20)};
    ui.pushId("curve");

    // The versions of the pair: this game's first (even empty), then the older ones (read-only).
    const TasCurve* curve = g_.tasTraining.curves().find(row.local.levelId, row.local.category);
    const TasCurveVersion* current = curve && !row.local.hash.empty() ? curve->find(row.local.hash, kSimulationVersion) : nullptr;
    std::vector<const TasCurveVersion*> versions = {current};
    if (curve)
        for (const TasCurveVersion& v : curve->versions)
            if (&v != current && (!v.points.empty() || !v.windows.empty())) versions.push_back(&v);
    selVersion_ = std::clamp(selVersion_, 0, static_cast<int>(versions.size()) - 1);
    const TasCurveVersion* v = versions[static_cast<size_t>(selVersion_)];
    const bool isCurrent = selVersion_ == 0;
    const TasTraining::Job* job = g_.tasTraining.jobFor({row.local.levelId, row.local.category});
    const bool training = isCurrent && job && (job->state == TasTraining::JobState::Running || job->state == TasTraining::JobState::Stopping);
    int64_t totalMs = isCurrent ? row.server.trainingMs : 0;
    if (training) totalMs = std::max(totalMs, job->trainingTotalMs());
    if (v) totalMs = std::max(totalMs, v->lastTrainingMs());
    const int liveRound = training && job->progress ? job->progress->round : 0;

    // ---------------------------------------------------------------- header
    const float hh = ui.s(32);
    auto bw = [&](const std::string& label) { return ui.text().measure(label, FontFace::Bold, ui.s(15)).x + ui.s(30); };
    float bx = in.right();
    {
        const std::string close = "Fermer";
        bx -= bw(close);
        if (button("close", {bx, in.y, bw(close), hh}, close, true)) {
            const std::string level = selLevel_;
            select(level, selCat_);  // the same pair again: closed
        }
        bx -= ui.s(12);
    }
    if (versions.size() > 1) {
        const float sb = ui.s(36);
        bx -= sb;
        if (button("ver_next", {bx, in.y, sb, hh}, "›", selVersion_ + 1 < static_cast<int>(versions.size()))) {
            ++selVersion_;
            pointCursor_ = windowCursor_ = -1;
        }
        const std::string vl = isCurrent ? std::string("Version actuelle")
                                         : "Ancienne version " + std::to_string(selVersion_) + "/" + std::to_string(versions.size() - 1) + " · lecture seule";
        const float vw = std::min(ui.text().measure(vl, FontFace::Body, ui.s(13)).x + ui.s(14), in.w * 0.35f);
        bx -= vw + ui.s(6);
        text(vl, {bx, in.y, vw, hh}, 13, isCurrent ? th.textDim : th.warm, FontFace::Body, Align::Center);
        bx -= ui.s(6) + sb;
        if (button("ver_prev", {bx, in.y, sb, hh}, "‹", selVersion_ > 0)) {
            --selVersion_;
            pointCursor_ = windowCursor_ = -1;
        }
        bx -= ui.s(12);
    }
    text("Courbe d'entraînement · " + row.local.levelName + " · " + categoryLabel(row.local.category), {in.x, in.y, std::max(0.0f, bx - in.x), hh}, 17,
         th.text, FontFace::Bold);
    if (selLevel_.empty()) {  // closed by its button
        ui.popId();
        return;
    }

    // ---------------------------------------------------------------- the data
    const float top = in.y + hh + ui.s(10);
    const float chartW = (in.w - ui.s(20)) * 0.63f;
    const Rect chart{in.x, top, chartW, in.bottom() - top};
    const Rect statsR{chart.right() + ui.s(20), top, in.right() - chart.right() - ui.s(20), in.bottom() - top};
    R.drawLine({statsR.x - ui.s(10), top}, {statsR.x - ui.s(10), in.bottom()}, kGrid, 1.0f);
    layout_.stats = statsR;

    std::vector<const TasCurvePoint*> marks;  // every point, by training time
    std::vector<const TasCurveWindow*> wins;
    if (v) {
        for (const TasCurvePoint& p : v->points) marks.push_back(&p);
        for (const TasCurveWindow& w : v->windows) wins.push_back(&w);
    }
    drawStats(row, v, statsR, training, liveRound);

    struct Ref {
        std::string label;
        int64_t ms;
        Color color;
    };
    std::vector<Ref> refs;
    if (isCurrent) {
        if (row.local.shippedMs > 0) refs.push_back({"Livrée " + formatRunTime(row.local.shippedMs), row.local.shippedMs, th.textDim});
        if (row.server.publishedMs > 0) refs.push_back({"En ligne " + formatRunTime(row.server.publishedMs), row.server.publishedMs, th.success});
    }
    int64_t yLo = INT64_MAX, yHi = 0;
    auto take = [&](int64_t ms) {
        if (ms <= 0) return;
        yLo = std::min(yLo, ms);
        yHi = std::max(yHi, ms);
    };
    size_t bestCount = 0;
    for (const TasCurvePoint* p : marks) {
        take(p->timeMs);
        if (p->best()) ++bestCount;
    }
    for (const Ref& r : refs) take(r.ms);
    for (const TasCurveWindow* w : wins)
        if (w->stats.finished > 0) take(w->stats.bestMs), take(w->stats.medianMs);
    const float fs = 12.0f;
    float lh = 0.0f;
    {
        i18n::Verbatim probe;  // a line's height, not a text
        lh = std::max(ui.text().measure("Ag", FontFace::Body, ui.s(fs)).y, ui.s(fs) + ui.s(2));
    }

    if (marks.empty() && wins.empty()) {
        layout_.emptyCurve = true;
        const bool trained = totalMs > 0 || training;
        const std::string msg = !isCurrent ? "Rien d'enregistré pour cette ancienne version."
                                : trained  ? "Aucune course vérifiée ni essai compté pour l'instant : la courbe commence à la première course vérifiée."
                                           : "Cette paire n'a jamais été entraînée pour cette version du niveau.";
        text(msg, {chart.x, chart.y + chart.h * 0.5f - ui.s(14), chart.w, ui.s(28)}, 15, th.textDim, FontFace::Body, Align::Center);
        ui.popId();
        return;
    }

    // ---------------------------------------------------------------- the layout of the chart
    const float legendH = ui.s(20), stripH = ui.s(44), roundsH = lh + ui.s(2), xlabH = lh + ui.s(4);
    const float legendY = chart.bottom() - legendH;
    const float stripY = legendY - ui.s(8) - stripH;
    const float roundsY = stripY - ui.s(6) - roundsH;
    const float xlabY = roundsY - xlabH;
    const float plotTop = chart.y + lh * 0.5f + ui.s(2);
    const float plotBottom = xlabY - ui.s(2);
    if (plotBottom - plotTop < ui.s(40)) {
        ui.popId();
        return;  // no room (never at 1280 × 720: the table keeps its three rows, the panel the rest)
    }
    if (yHi == 0) yLo = yHi = 1000;  // only windows without a finished attempt
    {
        const int64_t span = std::max<int64_t>(yHi - yLo, 20);
        const int64_t margin = std::max<int64_t>(span / 10, 10);
        yLo = std::max<int64_t>(0, yLo - margin);
        yHi += margin;
    }
    const int maxY = std::clamp(static_cast<int>((plotBottom - plotTop) / (lh * 2.2f)), 2, 6);
    const std::vector<int64_t> yt = axisTicks(yLo, yHi, maxY, runTimeSteps());
    float yLabW = 0.0f;
    for (int64_t t : yt) yLabW = std::max(yLabW, ui.text().measure(formatRunTime(t), FontFace::Body, ui.s(fs)).x);
    const std::string stripTitle = "Sections";
    const float gutterL = std::max(yLabW, ui.s(44)) + ui.s(10);
    float gutterR = ui.s(10);
    for (const Ref& r : refs) gutterR = std::max(gutterR, ui.text().measure(r.label, FontFace::Body, ui.s(fs)).x + ui.s(14));
    gutterR = std::min(gutterR, chart.w * 0.3f);
    const Rect plot{chart.x + gutterL, plotTop, std::max(ui.s(60), chart.w - gutterL - gutterR), plotBottom - plotTop};
    const Rect strip{plot.x, stripY, plot.w, stripH};
    layout_.plot = plot;
    layout_.strip = strip;
    const int64_t xMax = std::max<int64_t>(totalMs, 1000);
    auto X = [&](int64_t t) { return plot.x + static_cast<float>(std::clamp<int64_t>(t, 0, xMax)) / static_cast<float>(xMax) * plot.w; };
    auto Y = [&](int64_t ms) {
        const float k = static_cast<float>(ms - yLo) / static_cast<float>(std::max<int64_t>(1, yHi - yLo));
        return plot.y + (1.0f - std::clamp(k, -0.02f, 1.02f)) * plot.h;
    };

    // Grid, the values of the axis (text colours), the axis.
    for (int64_t t : yt) {
        const float y = Y(t);
        R.drawLine({plot.x, y}, {plot.right(), y}, kGrid, 1.0f);
        const std::string l = formatRunTime(t);
        const float w = ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2);
        text(l, {plot.x - ui.s(8) - w, y - lh * 0.5f, w, lh}, fs, th.textDim, FontFace::Body, Align::Right);
    }
    R.drawLine({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, kAxis, 1.0f);
    {
        const float widest = ui.text().measure("10 h 30", FontFace::Body, ui.s(fs)).x;
        const int maxX = std::clamp(static_cast<int>(plot.w / (widest + ui.s(28))), 1, 10);
        float lastRight = -1e9f;
        for (int64_t t : axisTicks(0, xMax, maxX, durationSteps())) {
            const float x = X(t);
            R.drawLine({x, plot.bottom()}, {x, plot.bottom() + ui.s(4)}, kAxis, 1.0f);
            const std::string l = durationTick(t);
            const float w = ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2);
            const float left = std::clamp(x - w * 0.5f, plot.x, plot.right() - w);
            if (left < lastRight + ui.s(12)) continue;
            text(l, {left, xlabY + ui.s(4), w, lh}, fs, th.textDim);
            lastRight = left + w;
        }
    }

    // The rounds of cosmic_tas: a tick under the axis and « Tour N » where there is room; the one
    // under way in the text colour.
    {
        const auto starts = roundStarts(v ? v->windows : std::vector<TasCurveWindow>{});
        float lastRight = -1e9f;
        for (size_t i = 0; i < starts.size(); ++i) {
            const float x = X(starts[i].first);
            R.drawLine({x, plot.y}, {x, plot.bottom()}, th.textDim.withAlpha(36), 1.0f);
            R.drawLine({x, roundsY}, {x, roundsY + roundsH * 0.8f}, th.textDim.withAlpha(150), 1.0f);
            R.drawLine({x, strip.y}, {x, strip.bottom()}, th.textDim.withAlpha(90), 1.0f);
            const bool live = training && i + 1 == starts.size() && starts[i].second == liveRound;
            const std::string l = live ? "Tour " + std::to_string(starts[i].second) + " (en cours)" : "Tour " + std::to_string(starts[i].second);
            const float w = ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2);
            const float left = x + ui.s(4);
            if (left < lastRight + ui.s(10) || left + w > plot.right()) continue;
            text(l, {left, roundsY, w, roundsH}, fs, live ? th.text : th.textDim);
            lastRight = left + w;
            ++layout_.roundsDrawn;
        }
    }

    // The reference lines (dashed, thin) and their labels at the right, never one over the other.
    {
        struct L {
            float y;
            const Ref* ref;
        };
        std::vector<L> ls;
        for (const Ref& r : refs) {
            const float y = Y(r.ms);
            dashedH(R, plot.x, plot.right(), y, r.color.withAlpha(170), ui.s(5), std::max(1.0f, ui.s(1.2f)));
            ls.push_back({y - lh * 0.5f, &r});
            ++layout_.referenceLines;
        }
        std::sort(ls.begin(), ls.end(), [](const L& a, const L& b) { return a.y < b.y; });
        for (size_t i = 1; i < ls.size(); ++i) ls[i].y = std::max(ls[i].y, ls[i - 1].y + lh + ui.s(2));
        const float maxBottom = plot.bottom() + lh * 0.5f;
        if (!ls.empty() && ls.back().y + lh > maxBottom) {
            const float over = ls.back().y + lh - maxBottom;
            for (L& l : ls) l.y -= over;
        }
        for (const L& l : ls) {
            const float w = std::min(gutterR - ui.s(8), ui.text().measure(l.ref->label, FontFace::Body, ui.s(fs)).x + ui.s(2));
            text(l.ref->label, {plot.right() + ui.s(8), l.y, w, lh}, fs, th.textDim);
        }
    }

    // The attempts of each window: best–worst of those that finished, their median.
    R.pushClip(plot.expanded(ui.s(1)));
    for (const TasCurveWindow* w : wins) {
        if (w->stats.finished <= 0) continue;
        const float x = (X(w->fromMs) + X(w->toMs)) * 0.5f;
        R.drawLine({x, Y(w->stats.bestMs)}, {x, Y(w->stats.worstMs)}, th.textDim.withAlpha(95), std::max(1.0f, ui.s(1.0f)));
        const float ym = Y(w->stats.medianMs);
        R.drawLine({x - ui.s(2.5f), ym}, {x + ui.s(2.5f), ym}, th.text.withAlpha(150), std::max(1.0f, ui.s(1.0f)));
    }
    // The best time: a step line going down to the training time now.
    std::vector<Vec2> stepPts;
    for (const TasCurvePoint* p : marks)
        if (p->best()) stepPts.push_back({X(p->trainingMs), Y(p->timeMs)});
    const float lw = std::max(1.5f, ui.s(2.0f));
    for (size_t i = 1; i < stepPts.size(); ++i) {
        R.drawLine(stepPts[i - 1], {stepPts[i].x, stepPts[i - 1].y}, th.accent, lw);
        R.drawLine({stepPts[i].x, stepPts[i - 1].y}, stepPts[i], th.accent, lw);
    }
    if (!stepPts.empty()) R.drawLine(stepPts.back(), {plot.right(), stepPts.back().y}, th.accent, lw);
    R.popClip();
    // The points: a dot per improvement, a ring for the start, a diamond per run published.
    std::vector<Vec2> at(marks.size());
    for (size_t i = 0; i < marks.size(); ++i) {
        const TasCurvePoint& p = *marks[i];
        const Vec2 c{X(p.trainingMs), std::clamp(Y(p.timeMs), plot.y, plot.bottom())};
        at[i] = c;
        if (p.kind == TasCurveKind::Publish) diamond(R, c, ui.s(5.0f), th.warm, kSurface);
        else if (p.kind == TasCurveKind::Start) {
            R.fillCircle(c, ui.s(4.6f), kSurface);
            R.drawRing(c, ui.s(4.0f), std::max(1.2f, ui.s(1.6f)), th.accent);
        } else {
            R.fillCircle(c, ui.s(4.6f), kSurface);
            R.fillCircle(c, ui.s(3.4f), p.share == TasShare::Shared ? th.accent : th.accent.withAlpha(150));
        }
        ++layout_.pointsDrawn;
    }
    if (bestCount == 0) text("Aucune course vérifiée pour l'instant.", {plot.x + ui.s(8), plot.y + ui.s(4), plot.w - ui.s(16), lh}, fs, th.textDim);

    // The strip of the sections reached: per window, a column; per section (from the bottom, the
    // exit at the top), the share of its attempts that reached it.
    R.fillRect(strip, Color{10, 15, 30, 255});
    {
        const float w = ui.text().measure(stripTitle, FontFace::Body, ui.s(fs)).x + ui.s(2);
        text(stripTitle, {strip.x - ui.s(8) - std::min(w, gutterL - ui.s(10)), strip.y, std::min(w, gutterL - ui.s(10)), lh}, fs, th.textDim);
        if (wins.empty()) text("Aucun essai compté pour cette version.", {strip.x + ui.s(8), strip.center().y - lh * 0.5f, strip.w - ui.s(16), lh}, fs, th.textDim);
    }
    for (const TasCurveWindow* w : wins) {
        const TasAttemptWindow& s = w->stats;
        if (s.attempts <= 0) continue;
        const float x0 = X(w->fromMs), x1 = std::max(x0 + 1.0f, X(w->toMs));
        const int rowsN = s.sections + 1;
        const float rh = strip.h / static_cast<float>(rowsN);
        for (int k = 0; k <= s.sections; ++k) {
            const float frac = static_cast<float>(s.reached(k)) / static_cast<float>(s.attempts);
            if (frac <= 0.0f) continue;
            const float y1 = strip.bottom() - rh * static_cast<float>(k);
            const Color c = k == s.sections ? th.success : th.accent;
            R.fillRect({x0, y1 - rh + 0.5f, x1 - x0, rh - 0.5f}, c.withAlpha(static_cast<uint8_t>(30.0f + 200.0f * frac)));
        }
        ++layout_.windowsDrawn;
    }

    // The legend: what each mark is (the reference lines are labelled where they are).
    {
        float x = chart.x;
        const float cy = legendY + legendH * 0.5f;
        auto item = [&](const std::string& label, auto&& swatch) {
            const float w = ui.text().measure(label, FontFace::Body, ui.s(fs)).x + ui.s(2);
            if (x + ui.s(18) + w > chart.right()) return;
            swatch(Vec2{x + ui.s(7), cy});
            text(label, {x + ui.s(18), legendY, w, legendH}, fs, th.textDim);
            x += ui.s(18) + w + ui.s(16);
        };
        item("Meilleur temps vérifié", [&](Vec2 c) { R.drawLine({c.x - ui.s(7), c.y + ui.s(3)}, {c.x, c.y + ui.s(3)}, th.accent, lw), R.drawLine({c.x, c.y + ui.s(3)}, {c.x, c.y - ui.s(3)}, th.accent, lw), R.drawLine({c.x, c.y - ui.s(3)}, {c.x + ui.s(7), c.y - ui.s(3)}, th.accent, lw); });
        item("Amélioration", [&](Vec2 c) { R.fillCircle(c, ui.s(3.4f), th.accent); });
        item("Départ", [&](Vec2 c) { R.drawRing(c, ui.s(4.0f), std::max(1.2f, ui.s(1.6f)), th.accent); });
        item("Publication", [&](Vec2 c) { diamond(R, c, ui.s(5.0f), th.warm, kSurface); });
        item("Essais finis : meilleur–pire, médian", [&](Vec2 c) {
            R.drawLine({c.x, c.y - ui.s(6)}, {c.x, c.y + ui.s(6)}, th.textDim, 1.0f);
            R.drawLine({c.x - ui.s(3), c.y}, {c.x + ui.s(3), c.y}, th.text, 1.0f);
        });
        item("Sections atteintes", [&](Vec2 c) { R.fillRect({c.x - ui.s(6), c.y - ui.s(5), ui.s(12), ui.s(10)}, th.accent.withAlpha(140)); });
    }

    // ---------------------------------------------------------------- tooltips
    // On the curve: the point the mouse is near (else the window of attempts under it); ← → move
    // from point to point with the keyboard or the gamepad.
    auto pointTip = [&](size_t i) {
        const TasCurvePoint& p = *marks[i];
        if (!charts::showTip(ui, tip_, at[i], Rect::fromCenter(at[i], {ui.s(14), ui.s(14)}))) return;
        tip_.bounds = area;
        int n = 0;
        int64_t prev = 0;
        for (size_t k = 0; k < i; ++k)
            if (marks[k]->best()) {
                prev = marks[k]->timeMs;
                if (marks[k]->kind == TasCurveKind::Improvement) ++n;
            }
        if (p.kind == TasCurveKind::Publish) tip_.title = "Course publiée (révision " + std::to_string(p.revision) + ")";
        else if (p.kind == TasCurveKind::Start) tip_.title = "Course de départ, vérifiée";
        else tip_.title = "Amélioration n° " + std::to_string(n + 1);
        tip_.rows.push_back({charts::kNoSwatch, "Temps", formatRunTime(p.timeMs)});
        tip_.rows.push_back({charts::kNoSwatch, "Gain", prev > 0 && p.timeMs < prev ? gainText(prev - p.timeMs) : std::string("—")});
        tip_.rows.push_back({charts::kNoSwatch, "Entraînement", durationText(p.trainingMs)});
        tip_.rows.push_back({charts::kNoSwatch, "Essais", countText(p.explored)});
        tip_.rows.push_back({charts::kNoSwatch, "Date", stamp(p.at)});
        if (!p.by.empty()) tip_.rows.push_back({charts::kNoSwatch, p.kind == TasCurveKind::Publish ? "Publiée par" : "Ordinateur de", i18n::keep(p.by)});
        if (p.share == TasShare::Pending) tip_.rows.push_back({charts::kNoSwatch, "Partage", "au prochain battement"});
        else if (p.share == TasShare::Unshared) tip_.rows.push_back({charts::kNoSwatch, "Partage", "cet ordinateur seulement"});
    };
    auto windowTip = [&](const TasCurveWindow& w, Vec2 anchor, const Rect& item) {
        if (!charts::showTip(ui, tip_, anchor, item)) return;
        tip_.bounds = area;
        const TasAttemptWindow& s = w.stats;
        tip_.title = "Essais de " + durationText(w.fromMs) + " à " + durationText(w.toMs);
        tip_.rows.push_back({charts::kNoSwatch, "Tour", s.section > 0 ? std::to_string(s.round) + " · section " + std::to_string(s.section) + "/" + std::to_string(s.sections)
                                                                    : std::to_string(s.round) + " · affinage de la course"});
        tip_.rows.push_back({charts::kNoSwatch, "Essais", countText(s.attempts)});
        tip_.rows.push_back({charts::kNoSwatch, "Finis", countText(s.finished) + " (" + percentText(s.finished, s.attempts) + ")"});
        if (s.finished > 0)
            tip_.rows.push_back({charts::kNoSwatch, "Meilleur · médian · pire", formatRunTime(s.bestMs) + " · " + formatRunTime(s.medianMs) + " · " + formatRunTime(s.worstMs)});
        if (s.unfinished() > 0) {
            tip_.rows.push_back({charts::kNoSwatch, "Non finis : parcours", "médian " + routeText(s.routeMedian) + " · max " + routeText(s.routeMax)});
            tip_.rows.push_back({charts::kNoSwatch, brokenWord(row.local.category), countText(s.brokenTotal())});
        }
        // Where the attempts went: the sections not reached by all of them (at most four), the exit.
        int shown = 0;
        for (int k = 1; k < s.sections && shown < 4; ++k)
            if (s.reached(k) < s.attempts) {
                tip_.rows.push_back({charts::kNoSwatch, "Section " + std::to_string(k + 1) + " atteinte", percentText(s.reached(k), s.attempts)});
                ++shown;
            }
        tip_.rows.push_back({charts::kNoSwatch, "Arrivée atteinte", percentText(s.finished, s.attempts)});
        if (!w.by.empty()) tip_.rows.push_back({charts::kNoSwatch, "Ordinateur de", i18n::keep(w.by)});
    };
    {
        const charts::Pointing pp = charts::pointing(ui, "plot", plot);
        if (pp.any()) R.drawRoundedRect(plot.expanded(ui.s(4)), ui.s(6), th.accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        if (pp.keys && !marks.empty()) {
            moveCursor(ui, pointCursor_, static_cast<int>(marks.size()));
            const size_t i = static_cast<size_t>(std::clamp(pointCursor_, 0, static_cast<int>(marks.size()) - 1));
            R.drawRing(at[i], ui.s(8), std::max(1.2f, ui.s(1.6f)), th.text);
            pointTip(i);
        } else if (pp.mouse) {
            const Vec2 m = ui.input().mousePos();
            int best = -1;
            float bestD = ui.s(14);
            for (size_t i = 0; i < at.size(); ++i)
                if (const float d = std::hypot(at[i].x - m.x, at[i].y - m.y); d < bestD) bestD = d, best = static_cast<int>(i);
            if (best >= 0) {
                pointCursor_ = best;
                R.drawRing(at[static_cast<size_t>(best)], ui.s(8), std::max(1.2f, ui.s(1.6f)), th.text);
                pointTip(static_cast<size_t>(best));
            } else {
                for (const TasCurveWindow* w : wins) {
                    if (w->stats.finished <= 0) continue;
                    const float x = (X(w->fromMs) + X(w->toMs)) * 0.5f;
                    const float y0 = Y(w->stats.worstMs), y1 = Y(w->stats.bestMs);
                    if (std::abs(m.x - x) > std::max(ui.s(4), (X(w->toMs) - X(w->fromMs)) * 0.5f) || m.y < y0 - ui.s(6) || m.y > y1 + ui.s(6)) continue;
                    windowTip(*w, {x, std::clamp(m.y, y0, y1)}, Rect{x - ui.s(4), y0, ui.s(8), std::max(ui.s(4), y1 - y0)});
                    break;
                }
            }
        }
    }
    {
        const charts::Pointing sp = charts::pointing(ui, "strip", strip);
        if (sp.any()) R.drawRoundedRect(strip.expanded(ui.s(4)), ui.s(6), th.accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        const TasCurveWindow* hit = nullptr;
        if (sp.keys && !wins.empty()) {
            moveCursor(ui, windowCursor_, static_cast<int>(wins.size()));
            hit = wins[static_cast<size_t>(std::clamp(windowCursor_, 0, static_cast<int>(wins.size()) - 1))];
        } else if (sp.mouse) {
            const float mx = ui.input().mousePos().x;
            float bestD = 1e9f;
            for (size_t i = 0; i < wins.size(); ++i) {
                const float x0 = X(wins[i]->fromMs), x1 = std::max(x0 + 1.0f, X(wins[i]->toMs));
                const float d = mx < x0 ? x0 - mx : mx > x1 ? mx - x1 : 0.0f;
                if (d < bestD && d <= ui.s(6)) bestD = d, hit = wins[i], windowCursor_ = static_cast<int>(i);
            }
        }
        if (hit) {
            const float x0 = X(hit->fromMs), x1 = std::max(x0 + 2.0f, X(hit->toMs));
            R.drawRect({x0 - 1.0f, strip.y - 1.0f, x1 - x0 + 2.0f, strip.h + 2.0f}, th.text, 1.0f);
            windowTip(*hit, {(x0 + x1) * 0.5f, strip.y}, Rect{x0, strip.y, x1 - x0, strip.h});
        }
    }
    ui.popId();
}

void AdminTas::drawStats(const Row& row, const TasCurveVersion* v, const Rect& r, bool training, int round) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const TasTraining::Job* job = g_.tasTraining.jobFor({row.local.levelId, row.local.category});
    std::vector<const TasCurveWindow*> all, inRound;
    if (v) {
        for (const TasCurveWindow& w : v->windows) all.push_back(&w);
        if (!all.empty()) {
            if (training && job) {
                for (const TasCurveWindow* w : all)
                    if (w->local && w->session == job->session && w->stats.round == round) inRound.push_back(w);
            } else {
                const TasCurveWindow* last = all.back();
                for (const TasCurveWindow* w : all)
                    if (w->session == last->session && w->stats.round == last->stats.round) inRound.push_back(w);
            }
        }
    }
    const float rh = ui.s(19), fs = 12.0f;
    float y = r.y;
    if (all.empty()) {
        text("Aucun essai compté pour cette version.", {r.x, y, r.w, rh}, 13, th.textDim);
        y += rh;
        text("Les essais se comptent pendant l'entraînement.", {r.x, y, r.w, rh}, fs, th.textDim);
        return;
    }
    const TasAttemptWindow T = tasAttemptTotal(all), Rr = tasAttemptTotal(inRound);
    const bool hasRound = !inRound.empty();
    const float c0 = r.w * 0.38f, c1 = r.w * 0.31f;
    auto cell = [&](int col, float yy) {
        const float x = col == 0 ? r.x : col == 1 ? r.x + c0 : r.x + c0 + c1;
        const float w = col == 0 ? c0 : col == 1 ? c1 : r.w - c0 - c1;
        return Rect{x, yy, std::max(0.0f, w - ui.s(8)), rh};
    };
    // The counts: the whole training, the round (under way here, else the last one).
    const std::string roundTitle = training ? "Tour " + std::to_string(round) + " (en cours)" : hasRound ? "Dernier tour (" + std::to_string(Rr.round) + ")" : "Tour";
    text("Tout l'entraînement", cell(1, y), fs, th.textDim, FontFace::Bold);
    text(roundTitle, cell(2, y), fs, th.textDim, FontFace::Bold);
    y += rh + ui.s(2);
    ui.r().drawLine({r.x, y - ui.s(1)}, {r.right(), y - ui.s(1)}, kGrid, 1.0f);
    auto line = [&](const std::string& label, const std::string& a, const std::string& b) {
        text(label, cell(0, y), fs, th.textDim);
        text(a, cell(1, y), 13, th.text);
        text(hasRound ? b : std::string("—"), cell(2, y), 13, th.text);
        y += rh;
    };
    auto finishedText = [](const TasAttemptWindow& w) { return compactCount(w.finished) + " (" + percentText(w.finished, w.attempts) + ")"; };
    auto timeOr = [](int64_t ms) { return ms > 0 ? formatRunTime(ms) : std::string("—"); };
    line("Essais", countText(T.attempts), countText(Rr.attempts));
    line("Finis", finishedText(T), finishedText(Rr));
    line("Meilleur fini", timeOr(T.bestMs), timeOr(Rr.bestMs));
    line("Médian", timeOr(T.medianMs), timeOr(Rr.medianMs));
    line("Pire", timeOr(T.worstMs), timeOr(Rr.worstMs));
    {
        const std::string word = brokenWord(row.local.category);
        line(word, compactCount(T.brokenTotal()) + " (" + percentText(T.brokenTotal(), T.attempts) + ")",
             compactCount(Rr.brokenTotal()) + " (" + percentText(Rr.brokenTotal(), Rr.attempts) + ")");
    }
    // The attempts counted before the windows existed (or not sent by another computer): said.
    int64_t explored = row.server.explored;
    if (training && job) explored = std::max(explored, job->exploredAt(job->progress ? job->progress->explored : 0));
    if (explored > T.attempts + T.attempts / 100 + 1000) {
        text("Détail de " + compactCount(T.attempts) + " essais sur " + compactCount(explored) + " (les autres ne sont pas comptés par fenêtre).", {r.x, y, r.w, rh}, 11.5f,
             th.textDim);
        y += rh;
    }
    y += ui.s(8);

    // Where the others stopped, section by section (a bar: its share of the stops, the dead part in red).
    const int n = T.sections;
    if (n <= 0) return;
    const float b0 = r.w * 0.22f, b1 = r.w * 0.30f, b2 = r.w * 0.22f;
    auto scell = [&](int col, float yy) {
        const float xs[] = {r.x, r.x + b0, r.x + b0 + b1, r.x + b0 + b1 + b2, r.right()};
        return Rect{xs[col], yy, std::max(0.0f, xs[col + 1] - xs[col] - ui.s(8)), rh};
    };
    text("Arrêts par section", scell(0, y), fs, th.textDim, FontFace::Bold);
    text("Tout", scell(1, y), fs, th.textDim, FontFace::Bold);
    text(brokenWord(row.local.category), scell(2, y), fs, th.textDim, FontFace::Bold);
    text("Ce tour", scell(3, y), fs, th.textDim, FontFace::Bold);
    y += rh + ui.s(2);
    ui.r().drawLine({r.x, y - ui.s(1)}, {r.right(), y - ui.s(1)}, kGrid, 1.0f);
    // The rows that fit (the exit included): the sections with the most stops when not all of them do.
    const int room = std::max(1, static_cast<int>((r.bottom() - y) / rh));
    std::vector<int> secs(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) secs[static_cast<size_t>(k)] = k;
    int hidden = 0;
    if (n + 1 > room) {
        const int keep = std::max(0, room - 2);  // the exit, and « … autres »
        std::stable_sort(secs.begin(), secs.end(), [&](int a, int b) { return T.stopped[static_cast<size_t>(a)] > T.stopped[static_cast<size_t>(b)]; });
        hidden = n - keep;
        secs.resize(static_cast<size_t>(keep));
        std::sort(secs.begin(), secs.end());
    }
    int64_t most = 1;
    for (int64_t s : T.stopped) most = std::max(most, s);
    most = std::max(most, T.finished);
    auto bar = [&](const Rect& c, int64_t value, int64_t dead, Color col) -> Rect {
        // A bar in the left part of the cell, its count in the rest (measured apart: never over it).
        const float bw = c.w * 0.42f;
        const Rect track{c.x, c.y + rh * 0.3f, bw, rh * 0.4f};
        ui.r().fillRect(track, Color{20, 28, 52, 255});
        const float w = bw * static_cast<float>(value) / static_cast<float>(most);
        if (w > 0.0f) ui.r().fillRect({track.x, track.y, std::max(1.0f, w), track.h}, col.withAlpha(190));
        if (dead > 0 && value > 0) ui.r().fillRect({track.x, track.y, std::max(1.0f, w * static_cast<float>(dead) / static_cast<float>(value)), track.h}, th.danger.withAlpha(210));
        return Rect{c.x + bw + ui.s(6), c.y, std::max(0.0f, c.w - bw - ui.s(6)), c.h};
    };
    for (int k : secs) {
        const size_t i = static_cast<size_t>(k);
        text("Section " + std::to_string(k + 1), scell(0, y), fs, th.textDim);
        text(compactCount(T.stopped[i]), bar(scell(1, y), T.stopped[i], T.broken[i], th.accent), 13, th.text);
        text(compactCount(T.broken[i]), scell(2, y), 13, th.text);
        text(hasRound && i < Rr.stopped.size() ? compactCount(Rr.stopped[i]) : std::string("—"), scell(3, y), 13, th.text);
        y += rh;
    }
    if (hidden > 0) {
        text("… " + std::to_string(hidden) + " autres sections", scell(0, y), fs, th.textDim);
        y += rh;
    }
    text("Arrivée", scell(0, y), fs, th.textDim);
    text(compactCount(T.finished), bar(scell(1, y), T.finished, 0, th.success), 13, th.text);
    text("—", scell(2, y), 13, th.textDim);
    text(hasRound ? compactCount(Rr.finished) : std::string("—"), scell(3, y), 13, th.text);
}

}  // namespace cosmic
