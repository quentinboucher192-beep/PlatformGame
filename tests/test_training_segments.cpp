// Section training (2.30.2): which segment of a full run a training segment is, the Sum of
// Best, the best training times and the powers chosen kept on disk (a damaged file, a newer
// one), the checkpoints of the segment history.

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/SegmentHistory.h"
#include "game/ghost/TrainingSegments.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

fs::path tempFile(const char* what) { return fs::temp_directory_path() / (std::string("cosmic_") + what + "_" + makeUuidV4().substr(0, 8) + ".json"); }

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

TEST_CASE("training segments: references from the gold segments, the record and the runs") {
    using RI = SegmentReferences::RunInput;
    // Nothing known: said plainly.
    SegmentReferences none = buildSegmentReferences({}, {}, {}, RI{}, kRoute);
    CHECK(!none.anyRun);
    CHECK(none.sob < 0.0f);
    CHECK(none.pbTime < 0.0f);
    CHECK(none.bestOf("a", "b") < 0.0f);
    CHECK(none.possibleGain() < 0.0f);
    // Gold segments of the route (as SegmentHistory keeps them) and a record that was slower
    // on two segments but faster on one (its segment counts as a best one too).
    std::vector<float> gold = {10.0f, 8.0f, 7.0f, 5.0f};
    RI pb{{11.0f, 19.0f, 25.5f}, 31.0f, {}};  // segments 11, 8, 6.5, 5.5
    SegmentReferences r = buildSegmentReferences(gold, {}, {}, pb, kRoute);
    CHECK(r.anyRun);
    CHECK(r.pbOnSobRoute);
    CHECK_NEAR(r.bestOf("", "a"), 10.0f, 1e-4f);
    CHECK_NEAR(r.bestOf("b", "c"), 6.5f, 1e-4f);  // the record's own segment
    CHECK_NEAR(r.pbOf("", "a"), 11.0f, 1e-4f);
    CHECK_NEAR(r.pbOf("c", ""), 5.5f, 1e-4f);
    CHECK_NEAR(r.sob, 10.0f + 8.0f + 6.5f + 5.0f, 1e-3f);
    CHECK_NEAR(r.possibleGain(), 31.0f - 29.5f, 1e-3f);
    // A record that skipped a checkpoint: its segments match where it went straight, the Sum
    // of Best stays the gold route's, and PB − SoB is not a gain.
    RI skip{{12.0f, 20.0f}, 26.0f, {"a", "c"}};  // a then c: segments 12, 8 (a → c), 6
    SegmentReferences s = buildSegmentReferences(gold, {}, {}, skip, kRoute);
    CHECK(!s.pbOnSobRoute);
    CHECK(s.possibleGain() < 0.0f);
    CHECK_NEAR(s.sob, 30.0f, 1e-3f);
    CHECK_NEAR(s.pbOf("a", "c"), 8.0f, 1e-4f);
    CHECK(s.pbOf("a", "b") < 0.0f);
    CHECK_NEAR(s.bestOf("a", "b"), 8.0f, 1e-4f);  // from the gold route
    // The same record without its checkpoints recorded: unknown, never guessed.
    SegmentReferences u = buildSegmentReferences(gold, {}, {}, RI{{12.0f, 20.0f}, 26.0f, {}}, kRoute);
    CHECK(u.pbOf("a", "b") < 0.0f);
    CHECK(u.pbOf("", "a") < 0.0f);
    CHECK_NEAR(u.pbTime, 26.0f, 1e-4f);
    // The runs kept count for the segments they went straight through (here c → exit).
    std::vector<RI> runs = {RI{{11.0f, 19.0f, 26.0f}, 30.0f, {"a", "b", "c"}}, RI{{9.0f, 17.5f}, 22.0f, {"a", "c"}}};
    SegmentReferences k = buildSegmentReferences(gold, {}, runs, pb, kRoute);
    CHECK_NEAR(k.bestOf("c", ""), 4.0f, 1e-4f);   // 30 - 26
    CHECK_NEAR(k.bestOf("", "a"), 9.0f, 1e-4f);   // the run that skipped b still started straight to a
    CHECK_NEAR(k.bestOf("a", "c"), 8.5f, 1e-4f);
    CHECK_NEAR(k.sob, 29.5f, 1e-3f);               // the Sum of Best is the gold route's only
    // A gold route recorded with other checkpoints than the route's own order.
    SegmentReferences o = buildSegmentReferences({3.0f, 4.0f, 5.0f}, {"b", "a"}, {}, RI{}, kRoute);
    CHECK_NEAR(o.bestOf("", "b"), 3.0f, 1e-4f);
    CHECK_NEAR(o.bestOf("b", "a"), 4.0f, 1e-4f);
    CHECK(o.bestOf("a", "b") < 0.0f);
    CHECK_NEAR(o.sob, 12.0f, 1e-4f);
    CHECK(o.pbTime < 0.0f);
    // Only a record, no history: its segments are the best ones.
    SegmentReferences p = buildSegmentReferences({}, {}, {}, pb, kRoute);
    CHECK_NEAR(p.sob, 31.0f, 1e-3f);
    CHECK_NEAR(p.possibleGain(), 0.0f, 1e-3f);
    // A level without checkpoint: one segment.
    SegmentReferences w = buildSegmentReferences({40.0f}, {}, {}, RI{{}, 41.0f, {}}, {});
    CHECK_NEAR(w.bestOf("", ""), 40.0f, 1e-4f);
    CHECK_NEAR(w.pbOf("", ""), 41.0f, 1e-4f);
    CHECK_NEAR(w.sob, 40.0f, 1e-4f);
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

TEST_CASE("training segments: best training times and powers kept, damaged and newer files") {
    fs::path f = tempFile("training");
    {
        TrainingRecords tr(f);
        tr.load();  // no file yet
        CHECK(!tr.recovered());
        CHECK(tr.get("lvl-1", "a", "b") == nullptr);
        CHECK(tr.add("lvl-1", "a", "b", 12.5f, "2026-11-15 10:00"));   // the first one is a best
        CHECK(!tr.add("lvl-1", "a", "b", 13.0f, "2026-11-15 10:01"));  // slower
        CHECK(!tr.add("lvl-1", "a", "b", 12.5004f, "2026-11-15 10:02"));  // the same to the millisecond
        CHECK(tr.add("lvl-1", "a", "b", 12.25f, "2026-11-15 10:03"));
        CHECK(tr.add("lvl-1", "", "a", 9.0f, "2026-11-15 10:04"));     // from the level's start
        CHECK(tr.add("lvl-1", "c", "", 4.0f, "2026-11-15 10:05"));     // to the exit
        // Refused: no level, a time out of bounds, an id that cannot be a checkpoint's.
        CHECK(!tr.add("", "a", "b", 5.0f, "x"));
        CHECK(!tr.add("lvl-1", "a", "b", -1.0f, "x"));
        CHECK(!tr.add("lvl-1", "a", "b", NAN, "x"));
        CHECK(!tr.add("lvl-1", "a", "b", 9000.0f, "x"));
        CHECK(!tr.add("lvl-1", "a\nb", "b", 5.0f, "x"));
        const TrainingRecords::Entry* e = tr.get("lvl-1", "a", "b");
        CHECK(e != nullptr);
        if (e) {
            CHECK_NEAR(e->best, 12.25f, 1e-4f);
            CHECK(e->attempts == 4);
            CHECK(e->date == "2026-11-15 10:03");
        }
        // Powers chosen for a segment (none yet: the route's), then back to the route's.
        CHECK(tr.powers("lvl-1", "a", "b") == nullptr);
        std::vector<std::string> chosen = {"dash", "wave", "dash", ""};
        tr.setPowers("lvl-1", "a", "b", &chosen, "2026-11-15 10:06");
        CHECK(tr.powers("lvl-1", "a", "b") && *tr.powers("lvl-1", "a", "b") == std::vector<std::string>({"dash", "wave"}));
        std::vector<std::string> nothing;
        tr.setPowers("lvl-2", "b", "c", &nothing, "2026-11-15 10:07");  // no power at all: a choice too
        CHECK(tr.powers("lvl-2", "b", "c") && tr.powers("lvl-2", "b", "c")->empty());
        tr.setPowers("lvl-1", "", "a", &chosen, "2026-11-15 10:08");
        tr.setPowers("lvl-1", "", "a", nullptr, "2026-11-15 10:09");
        CHECK(tr.powers("lvl-1", "", "a") == nullptr);
        CHECK(tr.get("lvl-1", "", "a") != nullptr);  // its best time stays
        CHECK(tr.save());
    }
    {
        TrainingRecords tr(f);
        tr.load();
        CHECK(!tr.recovered());
        const TrainingRecords::Entry* e = tr.get("lvl-1", "a", "b");
        CHECK(e && std::abs(e->best - 12.25f) < 1e-4f && e->attempts == 4);
        CHECK(tr.get("lvl-1", "c", "") && std::abs(tr.get("lvl-1", "c", "")->best - 4.0f) < 1e-4f);
        CHECK(tr.powers("lvl-1", "a", "b") && *tr.powers("lvl-1", "a", "b") == std::vector<std::string>({"dash", "wave"}));
        CHECK(tr.powers("lvl-2", "b", "c") && tr.powers("lvl-2", "b", "c")->empty());
        CHECK(tr.get("lvl-2", "b", "c")->best < 0.0f);  // a choice without any attempt
        CHECK(tr.powers("lvl-1", "", "a") == nullptr);
    }
    // Entries damaged by hand: the bad ones are left out, the good ones kept.
    FileUtil::writeTextAtomic(f, R"({"format": "cosmic.training", "version": 1, "levels": {
        "lvl-1": {"touched": "x", "segments": [
            {"from": "a", "to": "b", "best": 8.5, "attempts": 3, "date": "d"},
            {"from": "a", "to": "c", "best": -4, "attempts": 3},
            {"from": 7, "to": "c", "best": 4},
            {"from": "b", "to": "c", "best": 1e9},
            {"from": "c", "to": "", "best": 2.0, "attempts": -5, "powers": ["dash", 3]},
            "nonsense"]},
        "lvl-2": "nonsense",
        "lvl\u0001bad": {"segments": [{"from": "a", "to": "b", "best": 1.0}]}}})");
    {
        TrainingRecords tr(f);
        tr.load();
        CHECK(!tr.recovered());
        CHECK(tr.get("lvl-1", "a", "b") && std::abs(tr.get("lvl-1", "a", "b")->best - 8.5f) < 1e-4f);
        CHECK(tr.get("lvl-1", "a", "c") == nullptr);
        CHECK(tr.get("lvl-1", "b", "c") == nullptr);
        const TrainingRecords::Entry* e = tr.get("lvl-1", "c", "");
        CHECK(e && e->attempts == 1);              // a negative count: at least the best one
        CHECK(tr.powers("lvl-1", "c", "") == nullptr);  // a damaged list: the route's powers
        CHECK(tr.get("lvl\x01" "bad", "a", "b") == nullptr);
        CHECK(tr.get("lvl-1", "", "c") == nullptr);  // "from": 7 is not a checkpoint
    }
    // An unreadable file: set aside (never lost), the history starts again and is saved.
    FileUtil::writeTextAtomic(f, "{\"format\": \"cosmic.training\", \"levels\": {");
    {
        TrainingRecords tr(f);
        tr.load();
        CHECK(tr.recovered());
        fs::path bad = f;
        bad += ".corrupt";
        CHECK(fs::exists(bad));
        CHECK(tr.get("lvl-1", "a", "b") == nullptr);
        CHECK(tr.add("lvl-1", "a", "b", 7.0f, "d"));
        CHECK(tr.save());
        std::error_code ec;
        fs::remove(bad, ec);
    }
    FileUtil::writeTextAtomic(f, R"({"format": "something else", "version": 1, "levels": {}})");
    {
        TrainingRecords tr(f);
        tr.load();
        CHECK(tr.recovered());
        fs::path bad = f;
        bad += ".corrupt";
        std::error_code ec;
        fs::remove(bad, ec);
    }
    // Written by a newer game: read, never overwritten.
    const std::string newer = R"({"format": "cosmic.training", "version": 9, "levels": {"lvl-1": {"segments": [{"from": "a", "to": "b", "best": 6.0, "attempts": 2}]}}})";
    FileUtil::writeTextAtomic(f, newer);
    {
        TrainingRecords tr(f);
        tr.load();
        CHECK(tr.readOnly());
        CHECK(tr.get("lvl-1", "a", "b") && std::abs(tr.get("lvl-1", "a", "b")->best - 6.0f) < 1e-4f);
        tr.add("lvl-1", "a", "b", 5.0f, "d");
        CHECK(!tr.save());
        CHECK(FileUtil::readText(f).value_or("") == newer);
    }
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("training segments: the segment history keeps each run's checkpoints, never changing the gold segments") {
    fs::path f = tempFile("segments");
    {
        SegmentHistory h(f);
        h.add("lvl", {10.0f, 18.0f, 25.0f}, 30.0f, "2026-11-15 10:00", {"a", "b", "c"});
        h.add("lvl", {9.0f, 19.0f, 24.0f}, 31.0f, "2026-11-15 10:01");                // before 2.30.2: no checkpoints
        h.add("lvl", {9.5f, 17.0f, 25.0f}, 29.0f, "2026-11-15 10:02", {"a", "b"});      // not one per split: not kept
        CHECK(h.save());
    }
    SegmentHistory h(f);
    h.load();
    const auto* runs = h.runs("lvl");
    CHECK(runs && runs->size() == 3);
    if (runs && runs->size() == 3) {
        CHECK(runs->at(0).checkpoints.empty());
        CHECK(runs->at(1).checkpoints.empty());
        CHECK(runs->at(2).checkpoints == std::vector<std::string>({"a", "b", "c"}));
    }
    // The gold segments are what they always were (the shortest of each segment).
    std::vector<float> gold = h.goldSegments("lvl");
    CHECK(gold.size() == 4);
    if (gold.size() == 4) {
        CHECK_NEAR(gold[0], 9.0f, 1e-4f);
        CHECK_NEAR(gold[1], 7.5f, 1e-4f);
        CHECK_NEAR(gold[2], 5.0f, 1e-4f);
        CHECK_NEAR(gold[3], 4.0f, 1e-4f);
    }
    CHECK(h.bestSegments("lvl", 3) == gold);
    CHECK(h.goldCheckpoints("lvl") == std::vector<std::string>({"a", "b", "c"}));
    CHECK(h.goldSegments("other").empty());
    CHECK(h.goldCheckpoints("other").empty());
    // A route with more checkpoints replaces the gold one, with its own checkpoints.
    h.add("lvl", {5.0f, 9.0f, 14.0f, 20.0f}, 26.0f, "2026-11-15 10:03", {"a", "x", "b", "c"});
    CHECK(h.goldCheckpoints("lvl") == std::vector<std::string>({"a", "x", "b", "c"}));
    CHECK(h.goldSegments("lvl").size() == 5);
    // Damaged checkpoints in the file: dropped, the run kept.
    FileUtil::writeTextAtomic(f, R"({"format": "cosmic.segments", "version": 1, "levels": {"lvl": {"touched": "t", "best": [1, 2],
        "best_split_count": 1, "best_checkpoints": ["a", 5], "runs": [{"date": "d", "time": 3, "splits": [1], "checkpoints": ["a\u0002"]}]}}})");
    SegmentHistory d(f);
    d.load();
    CHECK(d.runs("lvl") && d.runs("lvl")->size() == 1 && d.runs("lvl")->front().checkpoints.empty());
    CHECK(d.goldCheckpoints("lvl").empty());
    CHECK(d.goldSegments("lvl").size() == 2);
    std::error_code ec;
    fs::remove(f, ec);
}
