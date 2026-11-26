// Section training (2.30.2): which segment of a full run a training segment is, the Sum of
// Best, the checkpoints of a ghost's splits. 2.91: the bests and the powers chosen are kept by
// PersonalBests (tests/test_personal_bests.cpp).

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/TrainingSegments.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const std::vector<std::string> kRoute = {"a", "b", "c"};

}  // namespace

TEST_CASE("training segments: a training segment is found in a run's checkpoints") {
    // Segment 0: start → a, 1: a → b, 2: b → c, 3: c → exit.
    CHECK(segmentIndexIn(kRoute, "", "a") == 0);
    CHECK(segmentIndexIn(kRoute, "a", "b") == 1);
    CHECK(segmentIndexIn(kRoute, "b", "c") == 2);
    CHECK(segmentIndexIn(kRoute, "c", "") == 3);
    // Not straight from one to the other: no match.
    CHECK(segmentIndexIn({"a", "c"}, "a", "b") == -1);   // b skipped
    CHECK(segmentIndexIn({"a", "c"}, "b", "c") == -1);   // b never reached
    CHECK(segmentIndexIn({"a", "c"}, "a", "c") == 1);    // a → c is a segment of that run
    CHECK(segmentIndexIn({"b", "a", "c"}, "a", "b") == -1);  // another order
    CHECK(segmentIndexIn(kRoute, "", "b") == -1);
    CHECK(segmentIndexIn(kRoute, "a", "") == -1);         // a is not the last one
    CHECK(segmentIndexIn(kRoute, "zz", "a") == -1);
    // A level (or a run) without checkpoint: one segment, from the start to the exit.
    CHECK(segmentIndexIn({}, "", "") == 0);
    CHECK(segmentIndexIn({}, "", "a") == -1);
    CHECK(segmentIndexIn(kRoute, "", "") == -1);
}

TEST_CASE("training segments: the checkpoints of a run, recorded or told by the route") {
    auto rec = resolveRunCheckpoints({"a", "c"}, 2, kRoute);
    CHECK(rec && *rec == std::vector<std::string>({"a", "c"}));  // recorded with it (2.30.2+)
    auto byRoute = resolveRunCheckpoints({}, 3, kRoute);
    CHECK(byRoute && *byRoute == kRoute);  // an older run with one split per checkpoint
    CHECK(!resolveRunCheckpoints({}, 2, kRoute));  // fewer splits, none recorded: unknown
    CHECK(!resolveRunCheckpoints({"a"}, 2, kRoute));  // damaged list, not the route's count
    auto none = resolveRunCheckpoints({}, 0, kRoute);
    CHECK(none && none->empty());  // no split: straight from the start to the exit
}

TEST_CASE("training segments: Sum of Best") {
    CHECK(sumOfBest({}) < 0.0f);
    CHECK(sumOfBest({5.0f, -1.0f, 3.0f}) < 0.0f);  // one segment unknown: no sum
    CHECK(sumOfBest({5.0f, NAN, 3.0f}) < 0.0f);
    CHECK_NEAR(sumOfBest({5.0f, 4.5f, 3.25f}), 12.75f, 1e-4f);
    CHECK_NEAR(sumOfBest({0.0f, 2.0f}), 2.0f, 1e-4f);
}

TEST_CASE("training segments: a ghost's splits are matched to the checkpoints where they were taken") {
    // A ghost walking right at 300 px/s (30 samples per second), splits at x = 600 and 1500.
    ReplayData rep;
    rep.sampleHz = 30;
    for (int i = 0; i <= 300; ++i) rep.frames.push_back({static_cast<float>(i) * 10.0f, 800.0f, 0, ReplayFrame::Grounded, 5});
    rep.durationMs = 10000;
    rep.events.push_back({60, ReplayEventType::Split, 0, static_cast<uint16_t>(2000)});
    rep.events.push_back({150, ReplayEventType::Split, 1, static_cast<uint16_t>(5000)});
    GhostTrack t;
    t.load(rep);
    CHECK(t.splits().size() == 2);
    std::vector<std::pair<std::string, Vec2>> cps = {{"far", {3000.0f, 800.0f}}, {"one", {610.0f, 800.0f}}, {"two", {1490.0f, 800.0f}}};
    CHECK(checkpointsOfTrack(t, cps) == std::vector<std::string>({"one", "two"}));
    // A split far from every checkpoint: nothing guessed.
    std::vector<std::pair<std::string, Vec2>> elsewhere = {{"one", {610.0f, 800.0f}}, {"two", {2500.0f, 800.0f}}};
    CHECK(checkpointsOfTrack(t, elsewhere).empty());
}

