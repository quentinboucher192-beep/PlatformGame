// ADMINISTRATION › TAS: the training curve of the row selected (docs/TAS.md « Courbe
// d'entraînement »), under the table. At the left, tabs (« TOUT »: the whole run, then CP1, CP2…:
// the level's checkpoints) over one axis of time along the cumulative training time of the version:
//   * « TOUT »: the best time verified, a step line going down, a dot at each improvement (a ring:
//     the run the training started from), a diamond at each run published; the shipped and the
//     online times as dashed reference lines, labelled at their right; behind them, the CLOUD of
//     the finished attempts (each one a small translucent point, the best of each window brighter);
//   * a CP tab: the best time at that checkpoint along the training (a step line) and the cloud of
//     the times at which the attempts reached it (a sample per window, its best always in it);
//   * the y axis bound to a high percentile of the cloud: the slower points sit as marks on its top
//     edge, said in a note; the rounds of cosmic_tas marked under the axis (their window of
//     sections when known), and a strip of the sections the attempts of each window reached;
//   * ZOOM on both axes (charts::ChartView): the wheel around the cursor (Maj or Ctrl: the times
//     only), a drag to move, a right drag (or Maj + drag) to zoom into a rectangle; + / − and the
//     arrows with the keyboard (a reticle; 0 or Origine resets); the triggers or the right stick,
//     the left stick or the cross, Y to reset with the gamepad; an overview while zoomed and
//     « Réinitialiser le zoom »; the default view follows the live data, a zoomed one stays.
// At the right, the counts of the whole training and of the round (or the last one), and what the
// round searches. Every point and window has its tooltip. Everything is measured: no text over
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
#include "ui/online/RaidWidgets.h"
#include "ui/screens/AdminTas.h"
#include "ui/widgets/CosmicFrame.h"
#include "ui/widgets/UIContext.h"

namespace cosmic {

using namespace tasadmin;

namespace {

const Color kGrid{33, 43, 66, 255};
const Color kAxis{61, 73, 102, 255};
const Color kSurface{14, 21, 42, 255};

/// The points of a cloud drawn at most: beyond, an even share of them (every window's best always).
constexpr size_t kDrawBudget = 16000;

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
}  // namespace

const char* tasadmin::brokenWord(RunCategory c) { return c == RunCategory::NoDamage ? "Morts ou touchés" : "Morts"; }

namespace {

/// The checkpoints a version counts (its own list, or as many as its windows count).
int checkpointsOf(const TasCurveVersion* v) {
    if (!v) return 0;
    size_t n = v->checkpoints.size();
    for (const TasCurveWindow& w : v->windows) n = std::max(n, w.stats.checkpoints.size());
    return static_cast<int>(std::min<size_t>(n, static_cast<size_t>(TasAttemptWindow::kMaxSections)));
}

/// « CP2 · Canopée brisée » (its label in the level when this computer knows it).
std::string checkpointTitle(const TasCurveVersion* v, int k) {
    const std::string cp = "CP" + std::to_string(k);
    if (v && k >= 1 && static_cast<size_t>(k) <= v->checkpoints.size() && !v->checkpoints[static_cast<size_t>(k - 1)].label.empty())
        return cp + " · " + v->checkpoints[static_cast<size_t>(k - 1)].label;
    return cp;
}

/// A point of a cloud: where (training ms), its time, its window, the best of its window.
struct CloudPt {
    double x = 0.0;
    int64_t t = 0;
    const TasCurveWindow* w = nullptr;
    bool best = false;
};

}  // namespace

charts::ChartView& AdminTas::curveView(int tab) {
    const size_t i = static_cast<size_t>(std::max(0, tab));
    if (views_.size() <= i) views_.resize(i + 1);
    return views_[i];
}

void AdminTas::drawTooltip() {
    if (rowTip_.show) charts::drawTip(g_.ui, rowTip_);
    else charts::drawTip(g_.ui, tip_);
}

void AdminTas::drawDetail(const Row& row, const Rect& area) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    Renderer& R = ui.r();
    InputManager& input = ui.input();
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
    const int liveFirst = training && job->progress ? job->progress->focusFirst : 0;
    const int liveLast = training && job->progress ? job->progress->focusLast : 0;

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

    // ---------------------------------------------------------------- the columns, the tabs
    const float top = in.y + hh + ui.s(10);
    const float chartW = (in.w - ui.s(20)) * 0.63f;
    const Rect chartCol{in.x, top, chartW, in.bottom() - top};
    const Rect statsR{chartCol.right() + ui.s(20), top, in.right() - chartCol.right() - ui.s(20), in.bottom() - top};
    layout_.stats = statsR;

    // 2.101: the TAS's mind (its thoughts, essayeurs, memory) when the training has one: under the
    // curve, or, in a short column (1280 x 720), in its own tab « IA » after the checkpoints (the
    // curve keeps its whole height).
    const TasProgress* mindOf = training && job && job->progress && job->progress->director ? &*job->progress
                                : (!training && previewProgress_ && previewProgress_->director) ? &*previewProgress_
                                                                                                 : nullptr;
    const float tabH = ui.s(28);
    const float chartTop = top + tabH + ui.s(8);
    const float chartFull = std::max(0.0f, chartCol.bottom() - chartTop);
    const bool mindApart = mindOf && chartFull < ui.s(560);
    const int nCp = checkpointsOf(v);
    cpTab_ = std::clamp(cpTab_, 0, mindApart ? nCp + 1 : nCp);
    const bool mindShown = mindApart && cpTab_ == nCp + 1;
    layout_.mindTab = mindApart ? nCp + 1 : -1;
    const int tab = mindShown ? 0 : cpTab_;
    if (!mindShown) R.drawLine({statsR.x - ui.s(10), top}, {statsR.x - ui.s(10), in.bottom()}, kGrid, 1.0f);
    if (reticles_.size() < static_cast<size_t>(nCp) + 1) reticles_.resize(static_cast<size_t>(nCp) + 1);
    charts::ChartView& view = curveView(tab);
    view.minW = 3000.0;  // a few seconds of training on screen at the deepest zoom
    view.minH = 4.0;     // a few milliseconds: 1:26.658 and 1:26.660 well apart
    Reticle& ret = reticles_[static_cast<size_t>(tab)];

    float tabsRight = chartCol.right();
    if (view.zoomed) {
        const std::string rl = "Réinitialiser le zoom";
        const float rw = std::min(bw(rl), chartCol.w * 0.42f);
        const Rect rr{chartCol.right() - rw, top, rw, tabH};
        layout_.zoomReset = rr;
        if (button("zoom_reset", rr, rl, true)) view.reset();
        tabsRight = rr.x - ui.s(10);
    }
    {
        // « TOUT », then CP1, CP2…: as wide as their labels need, the same width each; smaller
        // letters (then « … ») when many checkpoints share the row.
        std::vector<std::string> labels = {"TOUT"};
        for (int k = 1; k <= nCp; ++k) labels.push_back("CP" + std::to_string(k));
        if (mindApart) labels.push_back("IA");
        const Rect rowR{chartCol.x, top, std::max(0.0f, tabsRight - chartCol.x), tabH};
        float want = 0.0f;
        for (const std::string& l : labels) want = std::max(want, ui.text().measure(l, FontFace::Bold, ui.s(14)).x + ui.s(24));
        const float tw = std::min(want, rowR.w / static_cast<float>(labels.size()));
        const charts::Pointing tp = charts::pointing(ui, "cptabs", {rowR.x, rowR.y, tw * static_cast<float>(labels.size()), rowR.h});
        tabsFocused_ = tp.keys;
        if (tp.keys) {
            int t = cpTab_;
            if (input.menuPressed(Action::MenuLeft) || input.menuPressed(Action::MenuTabPrev)) t = (t + static_cast<int>(labels.size()) - 1) % static_cast<int>(labels.size());
            if (input.menuPressed(Action::MenuRight) || input.menuPressed(Action::MenuTabNext)) t = (t + 1) % static_cast<int>(labels.size());
            ui.consumeNavigation(true, false);
            if (t != cpTab_) cpTab_ = t, tip_ = charts::Tip{};
        }
        for (size_t i = 0; i < labels.size(); ++i) {
            const Rect tr{rowR.x + tw * static_cast<float>(i) + ui.s(1), rowR.y, tw - ui.s(2), rowR.h};
            const bool on = static_cast<int>(i) == cpTab_;
            const bool hover = ui.acceptsInput() && ui.usingPointer() && tr.contains(input.mousePos());
            if (hover && input.mousePressed(SDL_BUTTON_LEFT)) cpTab_ = static_cast<int>(i);
            R.fillRoundedRect(tr, ui.s(6), on ? Color{28, 64, 84, 240} : hover ? Color{22, 40, 64, 235} : Color{16, 22, 42, 220});
            if (on) R.fillRect({tr.x + ui.s(6), tr.bottom() - ui.s(3), std::max(0.0f, tr.w - ui.s(12)), ui.s(3)}, th.accent);
            if (on && tp.keys) R.drawRoundedRect(tr.expanded(ui.s(2)), ui.s(7), th.text.withAlpha(200), std::max(1.0f, ui.s(1.5f)));
            text(labels[i], {tr.x + ui.s(4), tr.y, std::max(0.0f, tr.w - ui.s(8)), tr.h - ui.s(2)}, 14, on ? th.text : th.textDim, FontFace::Bold, Align::Center);
            layout_.tabs.push_back(tr);
        }
    }
    if (mindShown) {
        // The tab « IA »: the mind on the whole width of the panel.
        drawMind(*mindOf, {chartCol.x, chartTop, in.right() - chartCol.x, chartFull});
        ui.popId();
        return;
    }
    const float mindH = mindOf && !mindApart ? std::clamp(chartFull * 0.42f, ui.s(220), ui.s(420)) : 0.0f;
    const Rect chart{chartCol.x, chartTop, chartCol.w, std::max(0.0f, chartFull - (mindH > 0.0f ? mindH + ui.s(10) : 0.0f))};
    if (mindH > 0.0f) drawMind(*mindOf, {chartCol.x, chart.bottom() + ui.s(10), chartCol.w, mindH});

    std::vector<const TasCurvePoint*> marks;  // every point, by training time
    std::vector<const TasCurveWindow*> wins;
    if (v) {
        for (const TasCurvePoint& p : v->points) marks.push_back(&p);
        for (const TasCurveWindow& w : v->windows) wins.push_back(&w);
    }
    drawStats(row, v, statsR, training, liveRound, liveFirst, liveLast);

    // ---------------------------------------------------------------- the data of the tab
    struct Ref {
        std::string label;
        int64_t ms;
        Color color;
    };
    std::vector<Ref> refs;
    if (isCurrent && tab == 0) {
        if (row.local.shippedMs > 0) refs.push_back({"Livrée " + formatRunTime(row.local.shippedMs), row.local.shippedMs, th.textDim});
        if (row.server.publishedMs > 0) refs.push_back({"En ligne " + formatRunTime(row.server.publishedMs), row.server.publishedMs, th.success});
    }
    // The players' runs that guide the training (docs/TAS.md « Les guides »): their time at the
    // exit (« TOUT ») or at the checkpoint (CPk), and the sum of the best segments of them and of
    // the machine's best run — where the machine loses shows at once.
    if (v && !v->references.empty()) {
        for (const TasProgress::Reference& g : v->references) {
            const int64_t ms = tab == 0 ? g.timeMs : static_cast<size_t>(tab) <= g.splits.size() ? g.splits[static_cast<size_t>(tab - 1)] : 0;
            if (ms > 0)
                refs.push_back({g.oldVersion ? i18n::keep(g.player) + " (ancienne version) " + formatRunTime(ms) : i18n::keep(g.player + " " + formatRunTime(ms)), ms, th.warm});
        }
        if (const int64_t sob = tasSumOfBest(*v, tab); sob > 0) refs.push_back({"Meilleurs segments " + formatRunTime(sob), sob, th.violet});
    }
    // The cloud: the finished attempts (« TOUT »), or the times at checkpoint `tab`.
    std::vector<CloudPt> cloud;
    int64_t cloudStands = 0;  // the attempts the cloud stands for (finished, or that reached the checkpoint)
    std::vector<std::pair<double, int64_t>> line;  // a CP tab: the best time at it along the training
    for (const TasCurveWindow* w : wins) {
        const int64_t span = std::max<int64_t>(0, w->toMs - w->fromMs);
        auto add = [&](const TasAttemptWindow::Point& p, int64_t best) {
            cloud.push_back({static_cast<double>(w->fromMs + std::clamp<int64_t>(p.atMs, 0, span)), p.timeMs, w, p.timeMs == best});
        };
        if (tab == 0) {
            cloudStands += w->stats.finished;
            for (const auto& p : w->stats.finishes) add(p, w->stats.bestMs);
        } else if (static_cast<size_t>(tab) <= w->stats.checkpoints.size()) {
            const auto& c = w->stats.checkpoints[static_cast<size_t>(tab - 1)];
            cloudStands += c.reached;
            for (const auto& p : c.sample) add(p, c.bestMs);
            if (c.reached > 0 && (line.empty() || c.bestMs < line.back().second)) line.push_back({static_cast<double>(w->toMs), c.bestMs});
        }
    }
    layout_.cloudKept = static_cast<int>(cloud.size());
    int64_t yLo = INT64_MAX, yHi = 0;
    auto take = [&](int64_t ms) {
        if (ms <= 0) return;
        yLo = std::min(yLo, ms);
        yHi = std::max(yHi, ms);
    };
    size_t bestCount = 0;
    if (tab == 0) {
        for (const TasCurvePoint* p : marks) {
            take(p->timeMs);
            if (p->best()) ++bestCount;
        }
        for (const Ref& r : refs) take(r.ms);
        for (const TasCurveWindow* w : wins)
            if (w->stats.finished > 0 && w->stats.finishes.empty()) take(w->stats.bestMs), take(w->stats.medianMs);
    } else {
        // A CP tab: the references there, and the best runs' times at it (the improvements marked).
        for (const Ref& r : refs) take(r.ms);
        for (const TasCurvePoint* p : marks)
            if (p->best() && static_cast<size_t>(tab) <= p->splits.size()) take(p->splits[static_cast<size_t>(tab - 1)]);
    }
    for (const auto& [x, t] : line) take(t);
    if (!cloud.empty()) {
        // The scale bound to the cloud's 95th percentile: the slower ones marked on the top edge.
        std::vector<int64_t> ts;
        ts.reserve(cloud.size());
        for (const CloudPt& p : cloud) ts.push_back(p.t);
        const size_t k = std::min(ts.size() - 1, ts.size() * 95 / 100);
        std::nth_element(ts.begin(), ts.begin() + static_cast<std::ptrdiff_t>(k), ts.end());
        take(ts[k]);
        take(*std::min_element(ts.begin(), ts.end()));
    }
    const float fs = 12.0f;
    float lh = 0.0f;
    {
        i18n::Verbatim probe;  // a line's height, not a text
        lh = std::max(ui.text().measure("Ag", FontFace::Body, ui.s(fs)).y, ui.s(fs) + ui.s(2));
    }

    const bool nothing = tab == 0 ? marks.empty() && wins.empty() : cloud.empty() && line.empty();
    if (nothing) {
        layout_.emptyCurve = true;
        const bool trained = totalMs > 0 || training;
        const std::string msg = !isCurrent ? "Rien d'enregistré pour cette ancienne version."
                                : tab > 0  ? "Aucun passage compté à ce point de passage pour l'instant."
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
    const int64_t xMax = std::max<int64_t>(totalMs, 1000);
    view.setExtent(0.0, static_cast<double>(xMax), static_cast<double>(yLo), static_cast<double>(yHi));
    const int maxY = std::clamp(static_cast<int>((plotBottom - plotTop) / (lh * 2.2f)), 2, 8);
    auto yTicksOf = [&](const charts::ChartView& vw) {
        const auto lo = static_cast<int64_t>(std::ceil(vw.y0)), hi = static_cast<int64_t>(std::floor(vw.y1));
        return hi >= lo ? axisTicks(lo, hi, maxY, runTimeSteps()) : std::vector<int64_t>{};
    };
    // The values' gutter: the widest of what the axis can show (the whole extent and the view).
    float yLabW = std::max(ui.text().measure(formatRunTime(yHi), FontFace::Body, ui.s(fs)).x, ui.text().measure(formatRunTime(yLo), FontFace::Body, ui.s(fs)).x);
    for (int64_t t : yTicksOf(view)) yLabW = std::max(yLabW, ui.text().measure(formatRunTime(t), FontFace::Body, ui.s(fs)).x);
    const std::string stripTitle = "Sections";
    const float gutterL = std::max(yLabW, ui.s(44)) + ui.s(10);
    float gutterR = ui.s(10);
    for (const Ref& r : refs) gutterR = std::max(gutterR, ui.text().measure(r.label, FontFace::Body, ui.s(fs)).x + ui.s(14));
    gutterR = std::min(gutterR, chart.w * 0.3f);
    const Rect plot{chart.x + gutterL, plotTop, std::max(ui.s(60), chart.w - gutterL - gutterR), plotBottom - plotTop};
    const Rect strip{plot.x, stripY, plot.w, stripH};
    layout_.plot = plot;
    layout_.strip = strip;

    // ---------------------------------------------------------------- zoom and reticle
    const charts::Pointing pp = charts::pointing(ui, "plot", plot);
    ui.claimWheel(plot);
    if (!ret.set) {
        // The reticle starts on the latest best time.
        ret.set = true;
        ret.x = static_cast<double>(xMax);
        ret.y = static_cast<double>(yHi);
        if (tab == 0) {
            for (const TasCurvePoint* p : marks)
                if (p->best()) ret.x = static_cast<double>(p->trainingMs), ret.y = static_cast<double>(p->timeMs);
        } else if (!line.empty()) {
            ret.x = line.back().first, ret.y = static_cast<double>(line.back().second);
            // On the latest window's best time at the checkpoint (a point of the cloud).
            double lastX = -1.0;
            for (const CloudPt& p : cloud)
                if (p.best && p.x >= lastX) lastX = p.x, ret.x = p.x, ret.y = static_cast<double>(p.t);
        }
    }
    if (pp.mouse) {
        const Vec2 m = input.mousePos();
        if (input.wheel() != 0.0f) {
            const double k = std::pow(0.8, static_cast<double>(input.wheel()));
            const bool yOnly = input.shift() || input.ctrl();
            view.zoomAt(view.dx(m.x, plot), view.dy(m.y, plot), yOnly ? 1.0 : k, k);
        }
        if (input.mousePressed(SDL_BUTTON_LEFT)) drag_ = input.shift() ? Drag::Box : Drag::Pan, dragFrom_ = m, dragView_ = view;
        if (input.mousePressed(SDL_BUTTON_RIGHT)) drag_ = Drag::Box, dragFrom_ = m, dragView_ = view;
    }
    Rect box;  // the rectangle being drawn to zoom into
    if (drag_ != Drag::None) {
        const Vec2 m = input.mousePos();
        if (drag_ == Drag::Pan) {
            if (input.mouseDown(SDL_BUTTON_LEFT)) {
                charts::ChartView moved = dragView_;
                moved.setExtent(view.fx0, view.fx1, view.fy0, view.fy1);
                moved.pan(-static_cast<double>(m.x - dragFrom_.x) / std::max(1.0f, plot.w) * moved.width(),
                          static_cast<double>(m.y - dragFrom_.y) / std::max(1.0f, plot.h) * moved.height());
                if (moved.zoomed) view = moved;
            } else {
                drag_ = Drag::None;
            }
        } else {
            const Vec2 a{std::clamp(dragFrom_.x, plot.x, plot.right()), std::clamp(dragFrom_.y, plot.y, plot.bottom())};
            const Vec2 b{std::clamp(m.x, plot.x, plot.right()), std::clamp(m.y, plot.y, plot.bottom())};
            box = {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y)};
            if (!input.mouseDown(SDL_BUTTON_RIGHT) && !input.mouseDown(SDL_BUTTON_LEFT)) {
                if (box.w >= ui.s(6) && box.h >= ui.s(6)) view.zoomTo(view.dx(box.x, plot), view.dx(box.right(), plot), view.dy(box.bottom(), plot), view.dy(box.y, plot));
                drag_ = Drag::None;
                box = {};
            }
        }
    }
    if (pp.keys) {
        // The reticle: ← → ↑ ↓ (the cross, the left stick) move it, the view follows it when
        // zoomed; + / − (the triggers, the right stick) zoom around it; 0 / Origine (Y) reset.
        const double stepX = view.width() / 40.0, stepY = view.height() / 20.0;
        const float dt = std::clamp(ui.dt(), 0.0f, 0.1f);
        if (input.menuPressed(Action::MenuLeft)) ret.x -= stepX;
        if (input.menuPressed(Action::MenuRight)) ret.x += stepX;
        ui.consumeNavigation(true, false);
        // Up and down move it until the edge of the data, then the focus leaves the chart.
        if (input.menuPressed(Action::MenuUp) && ret.y < view.fy1 - 1e-6) ret.y += stepY, ui.consumeNavigation(false, true);
        if (input.menuPressed(Action::MenuDown) && ret.y > view.fy0 + 1e-6) ret.y -= stepY, ui.consumeNavigation(false, true);
        const Vec2 ls = input.leftStick();
        if (std::abs(ls.x) > 0.2f || std::abs(ls.y) > 0.2f) {
            ret.x += static_cast<double>(ls.x) * view.width() * 0.6 * dt;
            ret.y -= static_cast<double>(ls.y) * view.height() * 0.6 * dt;
            ui.consumeNavigation(true, true);
        }
        if (input.commandPressed(Action::ViewZoomIn)) view.zoomAt(ret.x, ret.y, 0.8, 0.8);
        if (input.commandPressed(Action::ViewZoomOut)) view.zoomAt(ret.x, ret.y, 1.25, 1.25);
        const float lt = input.leftTrigger() > 0.08f ? input.leftTrigger() : 0.0f, rt = input.rightTrigger() > 0.08f ? input.rightTrigger() : 0.0f;
        const float ry = std::abs(input.rightStick().y) > 0.2f ? input.rightStick().y : 0.0f;
        const float rate = (rt - lt) * 1.6f - ry * 2.0f;
        if (rate != 0.0f) {
            const double k = std::exp(-static_cast<double>(rate * dt));
            view.zoomAt(ret.x, ret.y, k, k);
        }
        if (input.keyPressed(SDL_SCANCODE_0) || input.keyPressed(SDL_SCANCODE_KP_0) || input.keyPressed(SDL_SCANCODE_HOME) || input.padPressed(Action::ViewPartNext))
            view.reset();
        ret.x = std::clamp(ret.x, view.fx0, view.fx1);
        ret.y = std::clamp(ret.y, view.fy0, view.fy1);
        // The view keeps the reticle in it (zoomed), or the reticle stays in the view.
        const double ox = ret.x < view.x0 ? ret.x - view.x0 : ret.x > view.x1 ? ret.x - view.x1 : 0.0;
        const double oy = ret.y < view.y0 ? ret.y - view.y0 : ret.y > view.y1 ? ret.y - view.y1 : 0.0;
        if (view.zoomed && (ox != 0.0 || oy != 0.0)) view.pan(ox, oy);
        ret.x = std::clamp(ret.x, view.x0, view.x1);
        ret.y = std::clamp(ret.y, view.y0, view.y1);
    }
    layout_.view = view;
    auto X = [&](double x) { return view.sx(x, plot); };
    auto Y = [&](double t) { return view.sy(t, plot); };
    auto inX = [&](double x) { return x >= view.x0 && x <= view.x1; };

    // Grid, the values of the axis (text colours), the axis.
    for (int64_t t : yTicksOf(view)) {
        const float y = Y(static_cast<double>(t));
        R.drawLine({plot.x, y}, {plot.right(), y}, kGrid, 1.0f);
        const std::string l = formatRunTime(t);
        const float w = std::min(gutterL - ui.s(10), ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2));
        text(l, {plot.x - ui.s(8) - w, y - lh * 0.5f, w, lh}, fs, th.textDim, FontFace::Body, Align::Right);
        ++layout_.yTicks;
    }
    R.drawLine({plot.x, plot.bottom()}, {plot.right(), plot.bottom()}, kAxis, 1.0f);
    {
        // The ticks of the training time: as many as the labels allow, never one over another.
        const float widest = ui.text().measure(view.width() < 120000.0 ? "10 h 30:30" : "10 h 30", FontFace::Body, ui.s(fs)).x;
        const int maxX = std::clamp(static_cast<int>(plot.w / (widest + ui.s(28))), 1, 10);
        float lastRight = -1e9f;
        const auto lo = static_cast<int64_t>(std::ceil(view.x0)), hi = static_cast<int64_t>(std::floor(view.x1));
        for (int64_t t : hi >= lo ? axisTicks(lo, hi, maxX, durationSteps()) : std::vector<int64_t>{}) {
            const float x = X(static_cast<double>(t));
            R.drawLine({x, plot.bottom()}, {x, plot.bottom() + ui.s(4)}, kAxis, 1.0f);
            const std::string l = durationTick(t);
            const float w = ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2);
            const float left = std::clamp(x - w * 0.5f, plot.x, plot.right() - w);
            if (left < lastRight + ui.s(12)) continue;
            text(l, {left, xlabY + ui.s(4), w, lh}, fs, th.textDim);
            lastRight = left + w;
            ++layout_.xTicks;
        }
    }

    // The rounds of cosmic_tas: a tick under the axis and « Tour N » (with what it searched when
    // known and when there is room); the one under way in the text colour.
    {
        const auto starts = roundStarts(v ? v->windows : std::vector<TasCurveWindow>{});
        // Several sessions: each restarts its rounds at 1, so each mark says its session (« S2 · Tour 3 »).
        bool sessions = false;
        for (const RoundStart& st : starts) sessions = sessions || st.session != starts.front().session;
        float lastRight = -1e9f;
        for (size_t i = 0; i < starts.size(); ++i) {
            const RoundStart& st = starts[i];
            if (!inX(static_cast<double>(st.atMs))) continue;
            const float x = X(static_cast<double>(st.atMs));
            R.drawLine({x, plot.y}, {x, plot.bottom()}, th.textDim.withAlpha(36), 1.0f);
            R.drawLine({x, roundsY}, {x, roundsY + roundsH * 0.8f}, th.textDim.withAlpha(150), 1.0f);
            R.drawLine({x, strip.y}, {x, strip.bottom()}, th.textDim.withAlpha(90), 1.0f);
            const bool live = training && i + 1 == starts.size() && st.round == liveRound;
            const int first = live ? liveFirst : st.focusFirst, last = live ? liveLast : st.focusLast;
            const std::string head = roundText(st.round, live, sessions ? st.session : 0);
            std::vector<std::string> options;
            if (live || st.known) options.push_back(head + " · " + focusText(first, last, false));
            options.push_back(head);
            // Up to the next round's tick when one of them fits there, else the shortest as far as it goes.
            const float left = x + ui.s(4);
            float room = plot.right() - left;
            if (i + 1 < starts.size() && inX(static_cast<double>(starts[i + 1].atMs))) room = std::min(room, X(static_cast<double>(starts[i + 1].atMs)) - ui.s(6) - left);
            std::string l;
            float w = 0.0f;
            for (const std::string& o : options) {
                const float ow = ui.text().measure(o, FontFace::Body, ui.s(fs)).x + ui.s(2);
                if (ow <= room) {
                    l = o, w = ow;
                    break;
                }
            }
            if (l.empty()) l = options.back(), w = ui.text().measure(l, FontFace::Body, ui.s(fs)).x + ui.s(2);
            if (left < lastRight + ui.s(10) || left + w > plot.right()) continue;  // crowded: its details in the strip's tooltip
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
            const float y = Y(static_cast<double>(r.ms));
            if (y < plot.y - 0.5f || y > plot.bottom() + 0.5f) continue;
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

    // The cloud, behind the best time: the points in the view (an even share beyond kDrawBudget,
    // every window's best always), small and translucent; those beyond the view as marks on its
    // edge. Then the windows without points (before 2.98, other computers'): best–worst, median.
    R.pushClip(plot.expanded(ui.s(1)));
    std::vector<const CloudPt*> shown;  // the points drawn (the tooltips)
    int above = 0, below = 0;
    {
        std::vector<const CloudPt*> inView;
        for (const CloudPt& p : cloud) {
            if (!inX(p.x)) continue;
            if (static_cast<double>(p.t) > view.y1) {
                ++above;
                R.fillRect({X(p.x) - 0.5f, plot.y, std::max(1.0f, ui.s(1.0f)), ui.s(5)}, th.warm.withAlpha(150));
                continue;
            }
            if (static_cast<double>(p.t) < view.y0) {
                ++below;
                R.fillRect({X(p.x) - 0.5f, plot.bottom() - ui.s(5), std::max(1.0f, ui.s(1.0f)), ui.s(5)}, th.success.withAlpha(150));
                continue;
            }
            inView.push_back(&p);
        }
        const size_t stride = inView.size() > kDrawBudget ? (inView.size() + kDrawBudget - 1) / kDrawBudget : 1;
        const float ps = std::max(2.0f, ui.s(2.2f));
        const Color dot = tab == 0 ? th.accent.withAlpha(70) : th.violet.withAlpha(80);
        for (size_t i = 0; i < inView.size(); ++i) {
            const CloudPt& p = *inView[i];
            if (p.best || i % stride == 0) {
                if (!p.best) R.fillRect({X(p.x) - ps * 0.5f, Y(static_cast<double>(p.t)) - ps * 0.5f, ps, ps}, dot);
                shown.push_back(&p);
            }
        }
        for (const CloudPt* p : shown)
            if (p->best) {
                const Vec2 c{X(p->x), Y(static_cast<double>(p->t))};
                R.fillCircle(c, ui.s(3.0f), kSurface.withAlpha(200));
                R.fillCircle(c, ui.s(2.2f), th.warm.withAlpha(230));
            }
        layout_.cloudDrawn = static_cast<int>(shown.size());
        layout_.offView = above + below;
    }
    if (tab == 0)
        for (const TasCurveWindow* w : wins) {
            if (w->stats.finished <= 0 || !w->stats.finishes.empty()) continue;
            const double xm = (static_cast<double>(w->fromMs) + static_cast<double>(w->toMs)) * 0.5;
            if (!inX(xm)) continue;
            const float x = X(xm);
            R.drawLine({x, Y(static_cast<double>(w->stats.bestMs))}, {x, Y(static_cast<double>(w->stats.worstMs))}, th.textDim.withAlpha(95), std::max(1.0f, ui.s(1.0f)));
            const float ym = Y(static_cast<double>(w->stats.medianMs));
            R.drawLine({x - ui.s(2.5f), ym}, {x + ui.s(2.5f), ym}, th.text.withAlpha(150), std::max(1.0f, ui.s(1.0f)));
        }
    // The best time: a step line going down to the training time now.
    std::vector<Vec2> stepPts;
    if (tab == 0) {
        for (const TasCurvePoint* p : marks)
            if (p->best()) stepPts.push_back({X(static_cast<double>(p->trainingMs)), Y(static_cast<double>(p->timeMs))});
    } else {
        for (const auto& [x, t] : line) stepPts.push_back({X(x), Y(static_cast<double>(t))});
    }
    const float lw = std::max(1.5f, ui.s(2.0f));
    for (size_t i = 1; i < stepPts.size(); ++i) {
        R.drawLine(stepPts[i - 1], {stepPts[i].x, stepPts[i - 1].y}, th.accent, lw);
        R.drawLine({stepPts[i].x, stepPts[i - 1].y}, stepPts[i], th.accent, lw);
    }
    if (!stepPts.empty()) R.drawLine(stepPts.back(), {X(static_cast<double>(xMax)), stepPts.back().y}, th.accent, lw);
    R.popClip();
    // The points: a dot per improvement, a ring for the start, a diamond per run published.
    std::vector<Vec2> at(marks.size());
    std::vector<bool> visible(marks.size(), false);
    if (tab == 0)
        for (size_t i = 0; i < marks.size(); ++i) {
            const TasCurvePoint& p = *marks[i];
            const Vec2 c{X(static_cast<double>(p.trainingMs)), Y(static_cast<double>(p.timeMs))};
            at[i] = c;
            if (!plot.expanded(ui.s(1)).contains(c)) continue;
            visible[i] = true;
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
    else if (v)
        // A CP tab: each improvement whose segment ending at this checkpoint changed (its splits
        // against the best point before it), at its time there — where the gain is.
        for (size_t i = 0; i < marks.size(); ++i) {
            const TasCurvePoint& p = *marks[i];
            const size_t vi = static_cast<size_t>(marks[i] - v->points.data());
            if (p.kind != TasCurveKind::Improvement || vi >= v->points.size() || static_cast<size_t>(tab) > p.splits.size()) continue;
            const TasCurvePoint* prev = v->previousBest(vi);
            const std::vector<int64_t> d = prev ? tasSegmentDeltas(prev->splits, prev->timeMs, p.splits, p.timeMs) : std::vector<int64_t>{};
            if (d.empty() || d[static_cast<size_t>(tab - 1)] == 0) continue;
            const Vec2 c{X(static_cast<double>(p.trainingMs)), Y(static_cast<double>(p.splits[static_cast<size_t>(tab - 1)]))};
            at[i] = c;
            if (!plot.expanded(ui.s(1)).contains(c)) continue;
            visible[i] = true;
            R.fillCircle(c, ui.s(4.6f), kSurface);
            R.fillCircle(c, ui.s(3.4f), d[static_cast<size_t>(tab - 1)] < 0 ? th.success : th.danger);
            ++layout_.pointsDrawn;
            ++layout_.gainMarks;
        }
    // What the plot does not show, said in its top-left corner (one line each, never over another).
    {
        float ny = plot.y + ui.s(4);
        auto note = [&](const std::string& s) {
            text(s, {plot.x + ui.s(8), ny, std::max(0.0f, plot.w - ui.s(16) - (view.zoomed ? std::min(ui.s(150), plot.w * 0.3f) + ui.s(8) : 0.0f)), lh}, fs,
                 th.textDim);
            ny += lh + ui.s(2);
        };
        if (tab == 0 && bestCount == 0) note("Aucune course vérifiée pour l'instant.");
        if (above > 0 || below > 0) {
            const std::string a = compactCount(above), b = compactCount(below);
            note(above > 0 && below > 0 ? "Hors de la vue : " + a + " plus lents, " + b + " plus rapides"
                 : above > 0            ? "Hors de la vue : " + a + " plus lents"
                                        : "Hors de la vue : " + b + " plus rapides");
        }
        if (cloudStands > static_cast<int64_t>(cloud.size()) && !cloud.empty())
            note((tab == 0 ? "Nuage : " : "Échantillon : ") + compactCount(static_cast<int64_t>(cloud.size())) + " points sur " + compactCount(cloudStands));
    }
    // The overview while zoomed: the whole extent, the best time in small, the part shown.
    layout_.minimap = {};
    if (view.zoomed && plot.w > ui.s(240) && plot.h > ui.s(90)) {
        const Rect mm{plot.right() - std::min(ui.s(150), plot.w * 0.3f) - ui.s(6), plot.y + ui.s(6), std::min(ui.s(150), plot.w * 0.3f), std::min(ui.s(52), plot.h * 0.35f)};
        layout_.minimap = mm;
        R.fillRoundedRect(mm, ui.s(4), Color{8, 12, 26, 235});
        R.drawRoundedRect(mm, ui.s(4), kAxis, 1.0f);
        charts::ChartView whole = view;
        whole.reset();
        std::vector<Vec2> mini;
        if (tab == 0) {
            for (const TasCurvePoint* p : marks)
                if (p->best()) mini.push_back({whole.sx(static_cast<double>(p->trainingMs), mm), whole.sy(static_cast<double>(p->timeMs), mm)});
        } else {
            for (const auto& [x, t] : line) mini.push_back({whole.sx(x, mm), whole.sy(static_cast<double>(t), mm)});
        }
        R.pushClip(mm);
        for (size_t i = 1; i < mini.size(); ++i) {
            R.drawLine(mini[i - 1], {mini[i].x, mini[i - 1].y}, th.accent.withAlpha(200), 1.0f);
            R.drawLine({mini[i].x, mini[i - 1].y}, mini[i], th.accent.withAlpha(200), 1.0f);
        }
        if (!mini.empty()) R.drawLine(mini.back(), {mm.right(), mini.back().y}, th.accent.withAlpha(200), 1.0f);
        const float vx0 = whole.sx(view.x0, mm), vx1 = whole.sx(view.x1, mm), vy0 = whole.sy(view.y1, mm), vy1 = whole.sy(view.y0, mm);
        const Rect vr{vx0, vy0, std::max(2.0f, vx1 - vx0), std::max(2.0f, vy1 - vy0)};
        R.fillRect(vr, th.text.withAlpha(40));
        R.drawRect(vr, th.text, 1.0f);
        R.popClip();
    }
    if (box.w > 0.0f && box.h > 0.0f) {
        R.fillRect(box, th.accent.withAlpha(40));
        R.drawRect(box, th.accent, 1.0f);
    }

    // The strip of the sections reached: per window, a column; per section (from the bottom, the
    // exit at the top), the share of its attempts that reached it.
    R.fillRect(strip, Color{10, 15, 30, 255});
    {
        const float w = ui.text().measure(stripTitle, FontFace::Body, ui.s(fs)).x + ui.s(2);
        text(stripTitle, {strip.x - ui.s(8) - std::min(w, gutterL - ui.s(10)), strip.y, std::min(w, gutterL - ui.s(10)), lh}, fs, th.textDim);
        if (wins.empty()) text("Aucun essai compté pour cette version.", {strip.x + ui.s(8), strip.center().y - lh * 0.5f, strip.w - ui.s(16), lh}, fs, th.textDim);
    }
    R.pushClip(strip);
    for (const TasCurveWindow* w : wins) {
        const TasAttemptWindow& s = w->stats;
        if (s.attempts <= 0) continue;
        if (static_cast<double>(w->toMs) < view.x0 || static_cast<double>(w->fromMs) > view.x1) continue;
        const float x0 = X(static_cast<double>(w->fromMs)), x1 = std::max(x0 + 1.0f, X(static_cast<double>(w->toMs)));
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
    R.popClip();

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
        auto stepSwatch = [&](Vec2 c) {
            R.drawLine({c.x - ui.s(7), c.y + ui.s(3)}, {c.x, c.y + ui.s(3)}, th.accent, lw);
            R.drawLine({c.x, c.y + ui.s(3)}, {c.x, c.y - ui.s(3)}, th.accent, lw);
            R.drawLine({c.x, c.y - ui.s(3)}, {c.x + ui.s(7), c.y - ui.s(3)}, th.accent, lw);
        };
        auto cloudSwatch = [&](Vec2 c) {
            const Color d = tab == 0 ? th.accent.withAlpha(150) : th.violet.withAlpha(160);
            for (const Vec2 o : {Vec2{-4, 2}, Vec2{-1, -3}, Vec2{2, 1}, Vec2{5, -2}, Vec2{0, 4}})
                R.fillRect({c.x + ui.s(o.x) - 1.0f, c.y + ui.s(o.y) - 1.0f, 2.0f, 2.0f}, d);
        };
        if (tab == 0) {
            item("Meilleur temps vérifié", stepSwatch);
            item("Amélioration", [&](Vec2 c) { R.fillCircle(c, ui.s(3.4f), th.accent); });
            item("Départ", [&](Vec2 c) { R.drawRing(c, ui.s(4.0f), std::max(1.2f, ui.s(1.6f)), th.accent); });
            item("Publication", [&](Vec2 c) { diamond(R, c, ui.s(5.0f), th.warm, kSurface); });
            item("Essais finis", cloudSwatch);
        } else {
            item("Meilleur temps à ce point de passage", stepSwatch);
            item("Passages (échantillon)", cloudSwatch);
        }
        item("Meilleur de sa fenêtre", [&](Vec2 c) { R.fillCircle(c, ui.s(2.6f), th.warm); });
        item("Sections atteintes", [&](Vec2 c) { R.fillRect({c.x - ui.s(6), c.y - ui.s(5), ui.s(12), ui.s(10)}, th.accent.withAlpha(140)); });
    }

    // ---------------------------------------------------------------- tooltips
    // On the curve: the point the mouse is near (a mark first, then a point of the cloud, else the
    // window of attempts under it); with the keyboard or the gamepad, the point nearest the reticle.
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
        // Where the gain is: each segment against the best run before it (2.99: their splits).
        if (v && p.kind == TasCurveKind::Improvement) {
            const size_t vi = static_cast<size_t>(marks[i] - v->points.data());
            const TasCurvePoint* before = vi < v->points.size() ? v->previousBest(vi) : nullptr;
            const std::vector<int64_t> d = before ? tasSegmentDeltas(before->splits, before->timeMs, p.splits, p.timeMs) : std::vector<int64_t>{};
            int same = 0;
            for (size_t k = 0; k < d.size(); ++k) {
                if (d[k] == 0) {
                    ++same;
                    continue;
                }
                tip_.rows.push_back({charts::kNoSwatch, tasSegmentName(k, d.size() - 1), (d[k] < 0 ? "−" : "+") + std::to_string(d[k] < 0 ? -d[k] : d[k]) + " ms"});
            }
            if (!d.empty() && same > 0) tip_.rows.push_back({charts::kNoSwatch, "Autres segments", "="});
            if (!p.source.empty()) tip_.rows.push_back({charts::kNoSwatch, "Trouvée par", tasSourceLabel(p.source)});
        }
        tip_.rows.push_back({charts::kNoSwatch, "Entraînement", durationText(p.trainingMs)});
        tip_.rows.push_back({charts::kNoSwatch, "Essais", countText(p.explored)});
        tip_.rows.push_back({charts::kNoSwatch, "Date", stamp(p.at)});
        if (!p.by.empty()) tip_.rows.push_back({charts::kNoSwatch, p.kind == TasCurveKind::Publish ? "Publiée par" : "Ordinateur de", i18n::keep(p.by)});
        if (p.share == TasShare::Pending) tip_.rows.push_back({charts::kNoSwatch, "Partage", "au prochain battement"});
        else if (p.share == TasShare::Unshared) tip_.rows.push_back({charts::kNoSwatch, "Partage", "cet ordinateur seulement"});
    };
    auto cloudTip = [&](const CloudPt& p) {
        const Vec2 c{X(p.x), Y(static_cast<double>(p.t))};
        if (!charts::showTip(ui, tip_, c, Rect::fromCenter(c, {ui.s(10), ui.s(10)}))) return;
        tip_.bounds = area;
        tip_.title = tab == 0 ? std::string("Essai fini") : "Passage au CP" + std::to_string(tab);
        tip_.rows.push_back({charts::kNoSwatch, "Temps", formatRunTime(p.t)});
        tip_.rows.push_back({charts::kNoSwatch, "Entraînement", durationText(static_cast<int64_t>(p.x))});
        const TasAttemptWindow& s = p.w->stats;
        const std::string which = "S" + std::to_string(p.w->session) + " · " + std::to_string(s.round);
        tip_.rows.push_back({charts::kNoSwatch, "Session · tour", p.w->local ? which + " · " + focusText(s.focusFirst, s.focusLast, false) : which});
        if (p.best) tip_.rows.push_back({charts::kNoSwatch, "Fenêtre", "son meilleur essai"});
        if (!p.w->by.empty()) tip_.rows.push_back({charts::kNoSwatch, "Ordinateur de", i18n::keep(p.w->by)});
        R.drawRing(c, ui.s(6), std::max(1.2f, ui.s(1.4f)), th.text);
    };
    auto windowTip = [&](const TasCurveWindow& w, Vec2 anchor, const Rect& item) {
        if (!charts::showTip(ui, tip_, anchor, item)) return;
        tip_.bounds = area;
        const TasAttemptWindow& s = w.stats;
        tip_.title = "Essais de " + durationText(w.fromMs) + " à " + durationText(w.toMs);
        const std::string which = "S" + std::to_string(w.session) + " · " + std::to_string(s.round);
        tip_.rows.push_back({charts::kNoSwatch, "Session · tour", s.section > 0 ? which + " · section " + std::to_string(s.section) + "/" + std::to_string(s.sections)
                                                                              : which + " · affinage de la course"});
        if (w.local) tip_.rows.push_back({charts::kNoSwatch, "Sections cherchées", focusText(s.focusFirst, s.focusLast, true)});
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
        if (pp.any() && drag_ == Drag::None) R.drawRoundedRect(plot.expanded(ui.s(4)), ui.s(6), th.accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        // The nearest of the marks and of the cloud's points drawn to `m` (screen), within `radius`.
        auto nearestTo = [&](Vec2 m, float markRadius, float cloudRadius, int& mark, const CloudPt*& pt) {
            mark = -1;
            pt = nullptr;
            float bestD = markRadius;
            for (size_t i = 0; i < at.size(); ++i)
                if (visible[i])
                    if (const float d = std::hypot(at[i].x - m.x, at[i].y - m.y); d < bestD) bestD = d, mark = static_cast<int>(i);
            if (mark >= 0) return;
            bestD = cloudRadius;
            for (const CloudPt* p : shown)
                if (const float d = std::hypot(X(p->x) - m.x, Y(static_cast<double>(p->t)) - m.y); d < bestD) bestD = d, pt = p;
        };
        if (pp.keys) {
            const Vec2 r{X(ret.x), Y(ret.y)};
            R.drawLine({r.x, plot.y}, {r.x, plot.bottom()}, th.text.withAlpha(70), 1.0f);
            R.drawLine({plot.x, r.y}, {plot.right(), r.y}, th.text.withAlpha(70), 1.0f);
            int mark = -1;
            const CloudPt* pt = nullptr;
            nearestTo(r, 1e9f, 1e9f, mark, pt);
            if (mark >= 0) {
                pointCursor_ = mark;
                R.drawRing(at[static_cast<size_t>(mark)], ui.s(8), std::max(1.2f, ui.s(1.6f)), th.text);
                pointTip(static_cast<size_t>(mark));
            } else if (pt) {
                cloudTip(*pt);
            }
        } else if (pp.mouse && drag_ == Drag::None) {
            const Vec2 m = input.mousePos();
            int mark = -1;
            const CloudPt* pt = nullptr;
            nearestTo(m, ui.s(14), ui.s(8), mark, pt);
            if (mark >= 0) {
                pointCursor_ = mark;
                R.drawRing(at[static_cast<size_t>(mark)], ui.s(8), std::max(1.2f, ui.s(1.6f)), th.text);
                pointTip(static_cast<size_t>(mark));
            } else if (pt) {
                cloudTip(*pt);
            } else if (tab == 0) {
                for (const TasCurveWindow* w : wins) {
                    if (w->stats.finished <= 0 || !w->stats.finishes.empty()) continue;
                    const float x = (X(static_cast<double>(w->fromMs)) + X(static_cast<double>(w->toMs))) * 0.5f;
                    const float y0 = Y(static_cast<double>(w->stats.worstMs)), y1 = Y(static_cast<double>(w->stats.bestMs));
                    if (std::abs(m.x - x) > std::max(ui.s(4), (X(static_cast<double>(w->toMs)) - X(static_cast<double>(w->fromMs))) * 0.5f) || m.y < y0 - ui.s(6) ||
                        m.y > y1 + ui.s(6))
                        continue;
                    windowTip(*w, {x, std::clamp(m.y, y0, y1)}, Rect{x - ui.s(4), y0, ui.s(8), std::max(ui.s(4), y1 - y0)});
                    break;
                }
            }
        }
    }
    {
        const charts::Pointing sp = charts::pointing(ui, "strip", strip);
        if (sp.any()) R.drawRoundedRect(strip.expanded(ui.s(4)), ui.s(6), th.accent.withAlpha(150), std::max(1.0f, ui.s(1.5f)));
        // The windows in the view (the keyboard goes from one to the next among them).
        std::vector<const TasCurveWindow*> seen;
        for (const TasCurveWindow* w : wins)
            if (!(static_cast<double>(w->toMs) < view.x0 || static_cast<double>(w->fromMs) > view.x1)) seen.push_back(w);
        const TasCurveWindow* hit = nullptr;
        if (sp.keys && !seen.empty()) {
            moveCursor(ui, windowCursor_, static_cast<int>(seen.size()));
            hit = seen[static_cast<size_t>(std::clamp(windowCursor_, 0, static_cast<int>(seen.size()) - 1))];
        } else if (sp.mouse) {
            const float mx = input.mousePos().x;
            float bestD = 1e9f;
            for (size_t i = 0; i < seen.size(); ++i) {
                const float x0 = X(static_cast<double>(seen[i]->fromMs)), x1 = std::max(x0 + 1.0f, X(static_cast<double>(seen[i]->toMs)));
                const float d = mx < x0 ? x0 - mx : mx > x1 ? mx - x1 : 0.0f;
                if (d < bestD && d <= ui.s(6)) bestD = d, hit = seen[i], windowCursor_ = static_cast<int>(i);
            }
        }
        if (hit) {
            const float x0 = std::max(strip.x, X(static_cast<double>(hit->fromMs))), x1 = std::min(strip.right(), std::max(x0 + 2.0f, X(static_cast<double>(hit->toMs))));
            R.drawRect({x0 - 1.0f, strip.y - 1.0f, x1 - x0 + 2.0f, strip.h + 2.0f}, th.text, 1.0f);
            windowTip(*hit, {(x0 + x1) * 0.5f, strip.y}, Rect{x0, strip.y, x1 - x0, strip.h});
        }
    }
    ui.popId();
}

void AdminTas::drawStats(const Row& row, const TasCurveVersion* v, const Rect& r, bool training, int round, int focusFirst, int focusLast) {
    UIContext& ui = g_.ui;
    const UITheme& th = ui.theme();
    const TasTraining::Job* job = g_.tasTraining.jobFor({row.local.levelId, row.local.category});
    const int tab = cpTab_;
    if (tab == 0) {
        // 2.101: « TOUT »: the progress of the training (AdminTasMind.cpp).
        const TasProgress* live = training && job && job->progress ? &*job->progress : previewProgress_ ? &*previewProgress_ : nullptr;
        drawProgressPanel(row, v, r, training || (previewProgress_ && !training), round, focusFirst, focusLast, live);
        return;
    }
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
    if (tab > 0) {
        text(checkpointTitle(v, tab), {r.x, y, r.w, rh}, 13, th.text, FontFace::Bold);
        y += rh + ui.s(2);
    }
    if (all.empty()) {
        text("Aucun essai compté pour cette version.", {r.x, y, r.w, rh}, 13, th.textDim);
        y += rh;
        text("Les essais se comptent pendant l'entraînement.", {r.x, y, r.w, rh}, fs, th.textDim);
        return;
    }
    const TasAttemptWindow T = tasAttemptTotal(all), Rr = tasAttemptTotal(inRound);
    const bool hasRound = !inRound.empty();
    // What the round searches: the one under way (the progress file), else the last one's windows.
    const int shownRound = training ? round : hasRound ? Rr.round : 0;
    const bool knownFocus = training || (hasRound && inRound.back()->local);
    const int fFirst = training ? focusFirst : Rr.focusFirst, fLast = training ? focusLast : Rr.focusLast;
    const float c0 = r.w * 0.38f, c1 = r.w * 0.31f;
    auto cell = [&](int col, float yy) {
        const float x = col == 0 ? r.x : col == 1 ? r.x + c0 : r.x + c0 + c1;
        const float w = col == 0 ? c0 : col == 1 ? c1 : r.w - c0 - c1;
        return Rect{x, yy, std::max(0.0f, w - ui.s(8)), rh};
    };
    // The counts: the whole training, the round (under way here, else the last one).
    const std::string roundTitle = training ? roundText(round, true) : hasRound ? "Dernier tour (" + std::to_string(Rr.round) + ")" : "Tour";
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
    auto timeOr = [](int64_t ms) { return ms > 0 ? formatRunTime(ms) : std::string("—"); };
    // A note under the counts, wrapped to the column (at most `lines` lines).
    auto paragraph = [&](const std::string& s, int lines, Color c) {
        const float lineH = ui.s(15);
        for (const std::string& l : raidui::wrap(ui, s, FontFace::Body, 11.5f, r.w, lines)) {
            if (y + lineH > r.bottom()) break;
            text(l, {r.x, y, r.w, lineH}, 11.5f, c, FontFace::Body, Align::Left, true);
            y += lineH;
        }
    };
    if (tab > 0) {
        // A checkpoint: the attempts that reached it, their times.
        const size_t k = static_cast<size_t>(tab - 1);
        static const TasAttemptWindow::Checkpoint none;
        const TasAttemptWindow::Checkpoint& a = k < T.checkpoints.size() ? T.checkpoints[k] : none;
        const TasAttemptWindow::Checkpoint& b = k < Rr.checkpoints.size() ? Rr.checkpoints[k] : none;
        line("Atteint", compactCount(a.reached), compactCount(b.reached));
        line("Meilleur", timeOr(a.bestMs), timeOr(b.bestMs));
        line("Médian", timeOr(a.medianMs), timeOr(b.medianMs));
        line("Pire", timeOr(a.worstMs), timeOr(b.worstMs));
        // The gap at this checkpoint: the machine's best run against each guide (a player's run).
        if (v) {
            const TasCurvePoint* bp = nullptr;
            for (const TasCurvePoint& p : v->points)
                if (p.best() && k < p.splits.size() && p.splits[k] > 0 && (!bp || p.timeMs < bp->timeMs)) bp = &p;
            for (const TasProgress::Reference& g : v->references) {
                if (!bp || k >= g.splits.size() || g.splits[k] <= 0) continue;
                const int64_t gap = bp->splits[k] - g.splits[k];
                char sec[32];
                std::snprintf(sec, sizeof sec, "%s%.3f s", gap > 0 ? "+" : gap < 0 ? "−" : "", static_cast<double>(gap < 0 ? -gap : gap) / 1000.0);
                std::string t = sec;
                std::replace(t.begin(), t.end(), '.', ',');
                // The words translated, the player's name as written (two boxes, measured).
                const std::string lead = "CP" + std::to_string(tab) + " : TAS " + t + (g.oldVersion ? " (ancienne version) sur" : " sur");
                const float lw = std::min(r.w * 0.75f, ui.text().measure(lead, FontFace::Body, ui.s(fs)).x + ui.s(2));
                text(lead, {r.x, y, lw, rh}, fs, gap > 0 ? th.warm : th.success);
                text(g.player, {r.x + lw + ui.s(5), y, std::max(0.0f, r.w - lw - ui.s(5)), rh}, fs, gap > 0 ? th.warm : th.success, FontFace::Body, Align::Left, true);
                y += rh;
                ++layout_.referenceGaps;
            }
        }
        int64_t kept = 0;
        for (const TasCurveWindow* w : all)
            if (k < w->stats.checkpoints.size()) kept += static_cast<int64_t>(w->stats.checkpoints[k].sample.size());
        y += ui.s(4);
        paragraph("Échantillon : " + compactCount(kept) + " temps sur " + compactCount(a.reached) +
                      " passages (chaque fenêtre de 5 s en garde au plus 32, son meilleur toujours ; la médiane vient de l'échantillon).",
                  3, th.textDim);
        if (shownRound > 0 && knownFocus) {
            y += ui.s(4);
            // Each part translated alone (a slot holding a model of its own is not resolved), the line kept as it is.
            text(i18n::keep(i18n::tr(roundText(shownRound, training)) + " · " + i18n::tr(focusText(fFirst, fLast, true))), {r.x, y, r.w, rh}, fs, th.text, FontFace::Bold);
            y += rh;
        }
        return;
    }
}

}  // namespace cosmic
