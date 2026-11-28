// 2.98 (docs/TAS.md « Courbe d'entraînement », « Les tours »): the round's window and the clouds'
// points in cosmic_tas's progress file and in the training curve — every finished attempt up to a
// bound a window (then a sample with its best), the times at each checkpoint sampled — bounded per
// version, per heartbeat; the files written before still read; the zoom's arithmetic.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/online/LevelFingerprint.h"
#include "game/tas/TasCurve.h"
#include "game/tas/TasProgress.h"
#include "game/tas/TasTraining.h"
#include "tests/TestFramework.h"
#include "ui/screens/AdminTas.h"
#include "ui/widgets/ChartView.h"

using namespace cosmic;

namespace {

/// A window with 1 000 finished attempts and two checkpoints reached (the first 100 times).
TasAttemptWindow fullWindow() {
    TasAttemptStats st;
    st.setSections(3);
    st.setCheckpoints(2);
    for (int i = 0; i < 1000; ++i) st.addFinished(90000 - i, 5000 + i);  // the best: 89001
    for (int i = 0; i < 100; ++i) st.addCrossing(0, 12000 + i, 5000 + i);
    st.addCrossing(1, 30000, 7000);
    st.addCrossing(5, 1, 7000);  // no such checkpoint: ignored
    st.addStopped(1, 400, false);
    TasAttemptWindow w = st.take(5000, 10000);
    w.id = 1;
    w.startMs = 5000;
    w.endMs = 10000;
    w.round = 3;
    w.section = 2;
    w.focusFirst = 2;
    w.focusLast = 3;
    return w;
}

bool hasTime(const std::vector<TasAttemptWindow::Point>& pts, int64_t t) {
    return std::any_of(pts.begin(), pts.end(), [t](const auto& p) { return p.timeMs == t; });
}

}  // namespace

TEST_CASE("tas clouds: every finished attempt a point (bounded, its best kept), the checkpoints' times sampled") {
    const TasAttemptWindow w = fullWindow();
    CHECK(w.attempts == 1001 && w.finished == 1000);
    CHECK(w.finishes.size() == TasAttemptWindow::kMaxFinishPoints && hasTime(w.finishes, 89001));
    bool inside = true;
    for (const auto& p : w.finishes) inside = inside && p.atMs >= 0 && p.atMs <= 5000 && p.timeMs >= w.bestMs && p.timeMs <= w.worstMs;
    CHECK(inside);
    CHECK(w.checkpoints.size() == 2 && w.checkpoints[0].reached == 100 && w.checkpoints[0].bestMs == 12000 && w.checkpoints[0].worstMs == 12099);
    CHECK(w.checkpoints[0].sample.size() == TasAttemptWindow::kCheckpointSample && hasTime(w.checkpoints[0].sample, 12000));
    CHECK(w.checkpoints[1].reached == 1 && w.checkpoints[1].sample.size() == 1);
    // Few attempts: every one of them.
    TasAttemptStats few;
    few.setSections(1);
    for (int i = 0; i < 7; ++i) few.addFinished(86000 + i, i);
    CHECK(few.take(0, 10).finishes.size() == 7);
    // Through the JSON, and refused when inconsistent.
    auto back = TasAttemptWindow::fromJson(w.toJson());
    CHECK(back && back->focusFirst == 2 && back->focusLast == 3 && back->finishes == w.finishes && back->checkpoints == w.checkpoints);
    Json bad = w.toJson();
    bad["focusLast"] = 0;
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["finishes"][0][0] = 6000;  // after the window's end
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["finishes"][0][1] = 1;  // faster than the window's best
    CHECK(!TasAttemptWindow::fromJson(bad));
    bad = w.toJson();
    bad["checkpoints"][1]["reached"] = 5000;  // more than the attempts
    CHECK(!TasAttemptWindow::fromJson(bad));
    // Merged: placed from the merged start, bounded, both bests kept.
    TasAttemptWindow b = w;
    b.startMs = 10000;
    b.endMs = 15000;
    const TasAttemptWindow m = TasAttemptWindow::merged(w, b);
    CHECK(m.finishes.size() == TasAttemptWindow::kMaxFinishPoints && m.checkpoints[0].reached == 200 &&
          m.checkpoints[0].sample.size() == TasAttemptWindow::kCheckpointSample);
    CHECK(std::count_if(m.finishes.begin(), m.finishes.end(), [](const auto& p) { return p.timeMs == 89001; }) >= 1);
}

TEST_CASE("tas clouds: the progress file and the curve keep the round's window and the points; files written before still read") {
    const TasAttemptWindow w = fullWindow();
    Json old = w.toJson();
    for (const char* k : {"focusFirst", "focusLast", "finishes", "checkpoints"}) old.erase(k);
    auto o = TasAttemptWindow::fromJson(old);
    CHECK(o && o->focusFirst == 0 && o->focusLast == 0 && o->finishes.empty() && o->checkpoints.empty());
    TasProgress p;
    p.levelId = "sylvanox_01";
    p.round = 3;
    p.sections = 7;
    p.section = 4;
    p.focusFirst = 4;
    p.focusLast = 5;
    p.checkpoints = {{"o63", "Seuil des Anciens"}, {"o140", ""}};
    p.windows.push_back(w);
    auto rp = TasProgress::parse(p.toJson().dump());
    CHECK(rp && rp->focusFirst == 4 && rp->focusLast == 5 && rp->checkpoints == p.checkpoints && rp->windows.size() == 1 && rp->windows[0].finishes == w.finishes);
    Json oj = p.toJson();
    for (const char* k : {"focusFirst", "focusLast", "checkpoints"}) oj.erase(k);
    oj["windows"] = Json::array({old});
    auto op = TasProgress::fromJson(oj);
    CHECK(op && op->focusFirst == 0 && op->focusLast == 0 && op->checkpoints.empty() && op->windows.size() == 1);
    // The curve: its clouds bounded per version (the oldest windows keep only their best).
    TasCurve c;
    c.levelId = "sylvanox_01";
    const std::string h(64, 'd');
    for (int i = 0; i < 120; ++i) {
        TasCurveWindow cw;
        cw.fromMs = int64_t(i) * 5000;
        cw.toMs = cw.fromMs + 5000;
        cw.local = true;
        cw.stats = w;
        cw.stats.id = i + 1;
        c.addWindow(h, kSimulationVersion, cw);
    }
    size_t finishes = 0, cps = 0;
    bool each = true;
    for (const TasCurveWindow& cw : c.find(h, kSimulationVersion)->windows) {
        finishes += cw.stats.finishes.size();
        for (const auto& k : cw.stats.checkpoints) cps += k.sample.size();
        each = each && hasTime(cw.stats.finishes, 89001) && hasTime(cw.stats.checkpoints[0].sample, 12000);
    }
    CHECK(each);
    CHECK(finishes <= TasCurve::kMaxCloudPoints && finishes > TasCurve::kMaxCloudPoints / 2 && cps <= TasCurve::kMaxCloudPoints);
    // Written and read back, with the version's checkpoints.
    c.version(h, kSimulationVersion).checkpoints = p.checkpoints;
    auto rc = TasCurve::fromJson(c.toJson());
    CHECK(rc && rc->find(h, kSimulationVersion) && rc->find(h, kSimulationVersion)->checkpoints == p.checkpoints &&
          rc->find(h, kSimulationVersion)->windows.size() == c.find(h, kSimulationVersion)->windows.size());
    // The server's windows: their points on its training time, read from the window's start.
    Json sj = {{"start_ms", 100000}, {"end_ms", 105000}, {"session", 2}, {"round", 1}, {"section", 1}, {"sections", 1}, {"attempts", 10}, {"finished", 2},
               {"best_ms", 86000}, {"median_ms", 86500}, {"worst_ms", 87000}, {"route_median", 0}, {"route_max", 0}, {"stopped", Json::array({8})},
               {"broken", Json::array({0})}, {"finishes", Json::array({Json::array({100500, 86000}), Json::array({104000, 87000})})},
               {"checkpoints", Json::array({Json{{"reached", 3}, {"best_ms", 12000}, {"median_ms", 12100}, {"worst_ms", 12500}, {"sample", Json::array({Json::array({101000, 12000})})}}})}};
    auto sw = TasCurveWindow::fromServer(sj);
    CHECK(sw && sw->stats.finishes.size() == 2 && sw->stats.finishes[0].atMs == 500 && sw->stats.checkpoints.size() == 1 &&
          sw->stats.checkpoints[0].sample[0].atMs == 1000);
}

TEST_CASE("tas clouds: a heartbeat carries them bounded, and none once the server refused them") {
    const TasAttemptWindow w = fullWindow();
    TasCurveVersion v;
    TasCurveWindow pw;
    pw.fromMs = 0;
    pw.toMs = 5000;
    pw.share = TasShare::Pending;
    pw.leaseId = "L";
    pw.stats = w;
    v.windows.push_back(pw);
    const auto sent = TasTraining::curveSend(v, "L", 100000);
    CHECK(sent.cloud && sent.windows.size() == 1 && sent.windows[0]["finishes"].size() == TasTraining::kSendFinishes);
    CHECK(sent.windows[0]["checkpoints"].size() == 2 && sent.windows[0]["checkpoints"][0]["sample"].size() == TasTraining::kSendCheckpointSample);
    bool bestSent = false;
    for (const Json& q : sent.windows[0]["finishes"]) {
        bestSent = bestSent || q[1].get<int64_t>() == 89001;
        CHECK(q[0].get<int64_t>() >= 0 && q[0].get<int64_t>() <= sent.windows[0]["duration_ms"].get<int64_t>());
    }
    CHECK(bestSent);
    const auto plain = TasTraining::curveSend(v, "L", 100000, false);
    CHECK(!plain.cloud && !plain.windows[0].contains("finishes") && !plain.windows[0].contains("checkpoints"));
}

TEST_CASE("tas clouds: the rounds' labels say their window and their session") {
    CHECK(tasadmin::focusText(4, 5, false) == "sections 4 à 5" && tasadmin::focusText(3, 3, true) == "section 3 (le reste suit la meilleure course)" &&
          tasadmin::focusText(0, 0, false) == "tout le niveau");
    CHECK(tasadmin::roundText(3, true) == "Tour 3 (en cours)" && tasadmin::roundText(2, false, 4) == "S4 · Tour 2");
    CHECK(tasadmin::durationTick(18133000) == "5 h 02:13" && tasadmin::durationTick(5400000) == "1 h 30");
    std::vector<TasCurveWindow> ws(3);
    ws[0].session = 1, ws[0].stats.round = 1, ws[0].local = true;
    ws[1].session = 1, ws[1].stats.round = 2, ws[1].stats.focusFirst = 4, ws[1].stats.focusLast = 5, ws[1].local = true, ws[1].fromMs = 600000;
    ws[2].session = 2, ws[2].stats.round = 1, ws[2].fromMs = 900000;
    const auto starts = tasadmin::roundStarts(ws);
    CHECK(starts.size() == 3 && starts[1].focusFirst == 4 && starts[1].known && starts[2].session == 2 && !starts[2].known);
}

// The zoom of the curve, its arithmetic: screen ↔ data, the clamps, the reset, following the live data.
TEST_CASE("tas clouds: the zoom keeps the point under the cursor, stays in the data, resets, follows the live data") {
    charts::ChartView v;
    v.minW = 3000.0;
    v.minH = 4.0;
    v.setExtent(0.0, 3600000.0, 85000.0, 92000.0);
    CHECK(!v.zoomed && v.x0 == 0.0 && v.x1 == 3600000.0);
    const Rect r{100.0f, 50.0f, 800.0f, 300.0f};
    for (float px : {100.0f, 333.0f, 900.0f})
        for (float py : {50.0f, 200.0f, 350.0f}) {
            CHECK(std::abs(v.sx(v.dx(px, r), r) - px) < 0.01f);
            CHECK(std::abs(v.sy(v.dy(py, r), r) - py) < 0.01f);
        }
    CHECK(v.sy(92000.0, r) == r.y && v.sy(85000.0, r) == r.bottom());  // the slower at the top
    // Zoomed around a point: that point stays where it was on screen.
    const double px = v.dx(500.0f, r), py = v.dy(120.0f, r);
    v.zoomAt(px, py, 0.1, 0.05);
    CHECK(v.zoomed && std::abs(v.sx(px, r) - 500.0f) < 0.05f && std::abs(v.sy(py, r) - 120.0f) < 0.05f);
    CHECK(std::abs(v.width() - 360000.0) < 1.0 && std::abs(v.height() - 350.0) < 0.01);
    // As deep as the minimum: a few seconds, a few milliseconds (a millisecond is 75 px here).
    for (int i = 0; i < 60; ++i) v.zoomAt(px, py, 0.5, 0.5);
    CHECK(std::abs(v.width() - 3000.0) < 1e-6 && std::abs(v.height() - 4.0) < 1e-6);
    CHECK(std::abs(v.sy(v.y0 + 1.0, r) - v.sy(v.y0, r)) >= 70.0f);
    // Panned: never out of the data.
    v.pan(-1e9, 1e9);
    CHECK(v.x0 == 0.0 && v.y1 == 92000.0 && std::abs(v.width() - 3000.0) < 1e-6);
    // A rectangle (any corners' order), at least the minimum.
    v.zoomTo(2000000.0, 1000000.0, 86700.0, 86600.0);
    CHECK(v.zoomed && v.x0 == 1000000.0 && v.x1 == 2000000.0 && v.y0 == 86600.0 && v.y1 == 86700.0);
    v.zoomTo(10.0, 11.0, 86000.0, 86000.5);
    CHECK(std::abs(v.width() - 3000.0) < 1e-6 && std::abs(v.height() - 4.0) < 1e-6 && v.x0 >= 0.0);
    // Live data: a zoomed view stays; the default view follows.
    const double keep0 = v.x0, keep1 = v.x1;
    v.setExtent(0.0, 7200000.0, 84000.0, 92000.0);
    CHECK(v.zoomed && v.x0 == keep0 && v.x1 == keep1);
    v.reset();
    CHECK(!v.zoomed && v.x1 == 7200000.0 && v.y0 == 84000.0);
    v.setExtent(0.0, 8000000.0, 84000.0, 93000.0);
    CHECK(!v.zoomed && v.x1 == 8000000.0 && v.y1 == 93000.0);
    // Zoomed out past the data: back to following it; the whole extent does not pan.
    v.zoomAt(4000000.0, 88000.0, 0.5, 0.5);
    v.zoomAt(4000000.0, 88000.0, 10.0, 10.0);
    CHECK(!v.zoomed && v.x0 == 0.0 && v.x1 == 8000000.0);
    v.pan(1000.0, 10.0);
    CHECK(v.x0 == 0.0 && v.y0 == 84000.0);
}
