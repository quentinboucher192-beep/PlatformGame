// The gap of a replay to a reference (2.91, replay viewer and « Comparer deux replays »): the
// way of a run along the route read from its recorded trajectory, the gap in time (the Ghost
// Racing estimate, exact at the checkpoints and at the exit) and in distance (1 m = 16 px),
// B's offset in every gap, and the Sum of Best: continuous when each best segment has its
// replay, at the checkpoints only otherwise.

#include <cmath>
#include <string>
#include <vector>

#include "game/gameplay/LevelProgress.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/ReplayGap.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// A straight route from x 160 to x 2200 (y 800), a point every 16 px: 2040 px long.
LevelProgressTrack lineRoute() {
    LevelProgressTrack t;
    for (float x = 160.0f; x <= 2200.0f + 0.1f; x += 16.0f) t.route.push_back({std::min(x, 2200.0f), 800.0f});
    if (t.route.back().x < 2200.0f) t.route.push_back({2200.0f, 800.0f});
    t.cumulative.resize(t.route.size());
    t.cumulative[0] = 0.0f;
    for (size_t i = 1; i < t.route.size(); ++i) t.cumulative[i] = t.cumulative[i - 1] + (t.route[i] - t.route[i - 1]).length();
    t.length = t.cumulative.back();
    return t;
}

/// A hand-made finished run along that line at `speed` px/s (no simulation), a split each
/// time it passes one of `checkpoints` (their left edge + 16 px).
ReplayData lineRun(float speed, const std::vector<float>& checkpoints = {800.0f, 1500.0f}) {
    ReplayData r;
    r.levelId = "test_gap";
    r.levelHash = std::string(64, 'b');
    r.sampleHz = 30;
    const double duration = (2200.0 - 160.0) / speed;
    r.durationMs = static_cast<uint32_t>(std::llround(duration * 1000.0));
    const size_t n = static_cast<size_t>(std::ceil(duration * 30.0)) + 1;
    for (size_t i = 0; i < n; ++i) {
        ReplayFrame f;
        const double t = std::min(duration, static_cast<double>(i) / 30.0);
        f.x = static_cast<float>(160.0 + speed * t);
        f.y = 800.0f;
        f.flags = ReplayFrame::Grounded;
        r.frames.push_back(f);
    }
    uint8_t k = 0;
    for (float cx : checkpoints) {
        const int64_t ms = std::llround((cx + 16.0 - 160.0) / speed * 1000.0);
        const auto frame = static_cast<uint32_t>(std::ceil(static_cast<double>(ms) * 30.0 / 1000.0));
        r.events.push_back({frame, ReplayEventType::Split, k++, static_cast<uint16_t>(ms & 0xFFFF)});
    }
    r.events.push_back({static_cast<uint32_t>(n - 1), ReplayEventType::Finish, 0, 0});
    return r;
}

int64_t splitMs(float speed, float checkpointX) { return std::llround((checkpointX + 16.0 - 160.0) / speed * 1000.0); }

}  // namespace

TEST_CASE("replay gap: a run along the route is read from its recorded trajectory, between the points too") {
    const LevelProgressTrack route = lineRoute();
    CHECK_NEAR(route.length, 2040.0, 0.01);
    size_t hint = 0;
    CHECK_NEAR(routeDistanceAt(route, {168.0f, 800.0f}, hint), 8.0, 0.01);  // between two points of the route
    CHECK_NEAR(routeDistanceAt(route, {1000.0f, 780.0f}, hint), 840.0, 0.01);
    CHECK(routeDistanceAt(route, {1000.0f, 1100.0f}, hint) < 0.0f);  // 300 px below: off the route

    const RouteRun a = RouteRun::fromReplay(lineRun(200.0f), route);
    CHECK(a.valid());
    CHECK_NEAR(a.duration(), 10.2, 1e-6);
    CHECK_NEAR(a.reachAt(4.0), 800.0, 0.5);
    CHECK_NEAR(a.reachAt(4.0 + 1.0 / 60.0), 800.0 + 200.0 / 60.0, 0.5);  // between two samples
    CHECK_NEAR(a.timeAt(800.0f), 4.0, 0.002);
    CHECK_NEAR(a.reachAt(20.0), 2040.0, 0.01);  // the exit, once finished
    CHECK(a.timeAt(2041.0f) < 0.0);
    // Monotonic: never back along the route (a fall back does not undo what was reached).
    for (size_t i = 1; i < a.reach().size(); ++i) CHECK(a.reach()[i] >= a.reach()[i - 1]);
}

TEST_CASE("replay gap: in time and in distance, exact at each checkpoint and at the exit") {
    const LevelProgressTrack route = lineRoute();
    const ReplayData slow = lineRun(200.0f), fast = lineRun(250.0f);
    const RouteRun ra = RouteRun::fromReplay(slow, route), rb = RouteRun::fromReplay(fast, route);
    ReplayComparison cmp;
    cmp.set(CompareRun::fromReplay(slow, GhostMeta{}), CompareRun::fromReplay(fast, GhostMeta{}));
    const GapRun run = gapRunOf(cmp.a(), &ra);
    const GapReference ref = gapReferenceOf(cmp, rb);
    CHECK(ref.atSplitsMs.size() == 2);
    CHECK(ref.atSplitsMs[0] == splitMs(250.0f, 800.0f));
    CHECK(ref.finishMs == 8160);

    // At 4 s, the slow run is 800 px along: the fast one was there at 3,2 s, and is at 1000 px.
    GapSample s = gapAt(run, ref, 4.0);
    CHECK(s.hasTime && s.hasDistance && !s.exact);
    CHECK_NEAR(static_cast<double>(s.timeMs), 800.0, 4.0);
    CHECK_NEAR(s.metres, -200.0 / 16.0, 0.05);  // behind: negative
    // At a checkpoint: both splits to the millisecond.
    s = gapAt(run, ref, static_cast<double>(splitMs(200.0f, 800.0f)) / 1000.0);
    CHECK(s.exact && s.split == 0);
    CHECK(s.timeMs == splitMs(200.0f, 800.0f) - splitMs(250.0f, 800.0f));
    // At the exit: the two final times; both runs there, no distance between them.
    s = gapAt(run, ref, 10.2);
    CHECK(s.exact && s.split == 2);
    CHECK(s.timeMs == 10200 - 8160);
    CHECK_NEAR(s.metres, 0.0, 1e-4);
    // Before anything moved: no gap yet.
    CHECK(!gapAt(run, ref, 0.0).hasTime);

    const GapCurve c = gapCurve(run, ref, 10.2);
    CHECK(c.continuous);
    CHECK(c.samples.size() > 300);  // every 1/30 s
    for (size_t i = 1; i < c.samples.size(); ++i) CHECK(c.samples[i].t > c.samples[i - 1].t);
    CHECK(c.atSplit(0) && c.atSplit(0)->timeMs == splitMs(200.0f, 800.0f) - splitMs(250.0f, 800.0f));
    CHECK(c.atSplit(1) && c.atSplit(1)->timeMs == splitMs(200.0f, 1500.0f) - splitMs(250.0f, 1500.0f));
    CHECK(c.atSplit(2) && c.atSplit(2)->timeMs == 2040);
    CHECK(c.maxTimeMs == 2040);
    CHECK_NEAR(c.maxMetres, 408.0 / 16.0, 0.3);  // when the fast one finishes (8,16 s): 1632 px against 2040
    // A run that stopped before the exit (an extreme level's attempt): its last time is no finish.
    GapRun stopped = run;
    stopped.finished = false;
    CHECK(!gapCurve(stopped, ref, 10.2).atSplit(2));
    CHECK(!gapAt(stopped, ref, 10.2).exact);
    // Same run, same reference: zero all along.
    const GapCurve same = gapCurve(run, gapReferenceOf([&] {
        ReplayComparison self;
        self.set(CompareRun::fromReplay(slow, GhostMeta{}), CompareRun::fromReplay(slow, GhostMeta{}));
        return self;
    }(), ra), 10.2);
    CHECK(same.maxTimeMs <= 1);
    CHECK(same.maxMetres < 0.01f);
}

TEST_CASE("replay gap: B's offset is in every gap, an alignment gives exactly zero at its checkpoint") {
    const LevelProgressTrack route = lineRoute();
    const ReplayData slow = lineRun(200.0f), fast = lineRun(250.0f);
    const RouteRun ra = RouteRun::fromReplay(slow, route), rb = RouteRun::fromReplay(fast, route);
    ReplayComparison cmp;
    cmp.set(CompareRun::fromReplay(slow, GhostMeta{}), CompareRun::fromReplay(fast, GhostMeta{}));
    cmp.setOffsetMs(500);  // B shown 0,5 s later than it ran
    const GapRun run = gapRunOf(cmp.a(), &ra);
    GapReference ref = gapReferenceOf(cmp, rb);
    CHECK(ref.offsetMs == 500);
    GapSample s = gapAt(run, ref, 4.0);
    CHECK_NEAR(static_cast<double>(s.timeMs), 300.0, 4.0);
    CHECK_NEAR(s.metres, (800.0 - 875.0) / 16.0, 0.05);  // B where it was at 3,5 s
    s = gapAt(run, ref, static_cast<double>(splitMs(200.0f, 800.0f)) / 1000.0);
    CHECK(s.exact);
    CHECK(s.timeMs == splitMs(200.0f, 800.0f) - splitMs(250.0f, 800.0f) - 500);
    // The exit of the comparison: B (offset included) against A's final time, as the table says.
    CHECK(gapAt(run, ref, 10.2).timeMs == cmp.totals().gapMs);
    // After A's finish (the comparison goes on while B runs with a big offset): A ahead.
    cmp.setOffsetMs(4000);
    ref = gapReferenceOf(cmp, rb);
    s = gapAt(run, ref, 11.0);
    CHECK(s.exact && s.timeMs == 10200 - (8160 + 4000));
    CHECK(s.metres > 0.0f);
    // Aligned on the first checkpoint: B passes it with A, 0,000 s there.
    CHECK(cmp.alignOn(0));
    ref = gapReferenceOf(cmp, rb);
    const GapCurve c = gapCurve(run, ref, cmp.endTime());
    CHECK(c.atSplit(0) && c.atSplit(0)->timeMs == 0);
    CHECK(c.atSplit(1) && c.atSplit(1)->timeMs != 0);
}

TEST_CASE("replay gap: the Sum of Best is continuous with every replay, else compared at the checkpoints only") {
    const LevelProgressTrack route = lineRoute();
    const ReplayData x = lineRun(300.0f), y = lineRun(150.0f), a = lineRun(200.0f);
    const RouteRun rx = RouteRun::fromReplay(x, route), ry = RouteRun::fromReplay(y, route), ra = RouteRun::fromReplay(a, route);
    const double x0 = splitMs(300.0f, 800.0f) / 1000.0, y0 = splitMs(150.0f, 800.0f) / 1000.0, y1 = splitMs(150.0f, 1500.0f) / 1000.0;
    const double x1 = splitMs(300.0f, 1500.0f) / 1000.0, xEnd = x.durationMs / 1000.0;
    // Segment 1 from x, segment 2 from y (pretend it was its best), segment 3 from x.
    std::vector<SobPiece> pieces = {{x0, &rx, 0.0, x0}, {y1 - y0, &ry, y0, y1}, {xEnd - x1, &rx, x1, xEnd}};
    const std::vector<float> reach = {0.0f, 656.0f, 1356.0f, route.length};
    SobRun sob = buildSobRun(pieces, reach, route.length);
    CHECK(sob.run.valid());
    CHECK(sob.withReplay == 3);
    CHECK(sob.boundaryMs.size() == 4);
    CHECK(sob.boundaryMs[1] == splitMs(300.0f, 800.0f));
    CHECK(sob.boundaryMs[3] == std::llround((x0 + (y1 - y0) + (xEnd - x1)) * 1000.0));
    // The montage on its own clock: its second segment at the slow run's pace.
    CHECK_NEAR(sob.run.timeAt(1000.0f), x0 + (1000.0 - 656.0) / 150.0, 0.02);
    CHECK_NEAR(sob.run.reachAt(x0 + 1.0), 656.0 + 150.0, 1.0);

    GapReference ref;
    ref.run = sob.run;
    ref.atSplitsMs = sobAtSplits(sob, 2, {}, {});  // the same route: matched in order
    ref.finishMs = sob.boundaryMs[3];
    GapRun run;
    run.run = &ra;
    run.splitsMs = {splitMs(200.0f, 800.0f), splitMs(200.0f, 1500.0f)};
    run.durationMs = a.durationMs;
    const GapCurve c = gapCurve(run, ref, a.durationMs / 1000.0);
    CHECK(c.continuous);
    CHECK(c.atSplit(0) && c.atSplit(0)->timeMs == splitMs(200.0f, 800.0f) - sob.boundaryMs[1]);
    CHECK(c.atSplit(2) && c.atSplit(2)->timeMs == static_cast<int64_t>(a.durationMs) - sob.boundaryMs[3]);
    CHECK(gapAt(run, ref, 5.0).hasDistance);

    // A best segment without its replay here: no way along the route, the checkpoints only.
    pieces[1].run = nullptr;
    sob = buildSobRun(pieces, reach, route.length);
    CHECK(!sob.run.valid());
    CHECK(sob.withReplay == 2);
    CHECK(sob.boundaryMs[2] == std::llround((x0 + (y1 - y0)) * 1000.0));
    ref.run = sob.run;
    ref.atSplitsMs = sobAtSplits(sob, 2, {}, {});
    const GapCurve only = gapCurve(run, ref, a.durationMs / 1000.0);
    CHECK(!only.continuous);
    CHECK(only.samples.size() == 3);  // two checkpoints and the exit, nothing in between
    for (const GapSample& s : only.samples) CHECK(s.exact && s.hasTime && !s.hasDistance);
    CHECK(only.samples[1].timeMs == splitMs(200.0f, 1500.0f) - sob.boundaryMs[2]);
    // Live: the gap at the last checkpoint passed, said as such (its time).
    GapSample live = gapAt(run, ref, 5.0);
    CHECK(live.exact && live.split == 0);
    CHECK_NEAR(live.t, splitMs(200.0f, 800.0f) / 1000.0, 1e-9);
    CHECK(!gapAt(run, ref, 1.0).hasTime);  // before the first checkpoint: nothing to compare
    CHECK(gapAt(run, ref, 10.2).split == 2);

    // A segment never done: the sum stops there (no exit to compare).
    pieces[2].duration = -1.0;
    sob = buildSobRun(pieces, reach, route.length);
    CHECK(sob.boundaryMs[3] == -1);
    CHECK(sob.boundaryMs[2] >= 0);

    // Matched by the checkpoints' ids: a run that skipped the first one.
    const std::vector<int64_t> byId = sobAtSplits(sob, 1, {"cp2"}, {"cp1", "cp2"});
    CHECK(byId.size() == 1 && byId[0] == sob.boundaryMs[2]);
    CHECK(sobAtSplits(sob, 1, {}, {})[0] == -1);  // neither the ids nor the same route: not compared
}

TEST_CASE("replay gap: a measure A → B read on a replay's trajectory, between two samples") {
    MeasureGate a, b;
    a.at = {565.0f, 800.0f};  // crossed at 2,025 s: between two samples (1/30 s apart)
    b.at = {965.0f, 800.0f};  // at 4,025 s
    std::vector<MeasurePass> p = measurePasses(lineRun(200.0f), a, b);
    CHECK(p.size() == 1);
    CHECK(p[0].done());
    CHECK_NEAR(p[0].a, 2.025, 0.001);
    CHECK_NEAR(p[0].b, 4.025, 0.001);
    CHECK_NEAR(p[0].time(), 2.0, 0.001);
    // B behind A on the way: crossed A, never B after it.
    p = measurePasses(lineRun(200.0f), b, a);
    CHECK(p.size() == 1 && !p[0].done());
    CHECK_NEAR(p[0].a, 4.025, 0.001);
    // A jump of the trajectory (a teleport) between A and B stops the chronometer.
    ReplayData jump = lineRun(200.0f);
    for (size_t i = 100; i < jump.frames.size(); ++i) jump.frames[i].x += 600.0f;  // at 3,33 s: 600 px further at once
    p = measurePasses(jump, a, b);
    CHECK(p.empty());
}

TEST_CASE("replay gap: metres written to the tenth, never minus zero") {
    CHECK(formatGapMetres(6.84f) == "6,8 m");
    CHECK(formatGapMetres(-6.84f) == "-6,8 m");
    CHECK(formatGapMetres(-0.04f) == "0,0 m");
    CHECK(formatGapMetres(12.06f) == "12,1 m");
    CHECK(metersOf(16.0f) == 1.0f);  // 1 m = 1 tile
}
