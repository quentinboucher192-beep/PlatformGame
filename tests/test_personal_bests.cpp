// 2.91 — ONE personal Sum of Best per level version (game/ghost/PersonalBests.h): every source
// merged (full runs, section trainings, enchaînements, StartPos, verified online runs), the
// records of every range of segments (the enchaînements' grid), the file and its replays, and
// the merge of the old files (segments.json, training.json) and of the statistics.
// 2.95: the rules of a segment (tests/test_sob_rules.cpp): a training counts its range only (its
// run-up never), after a StartPos from the first segment entered; the old files give their runs.

#include <cmath>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/KeptReplays.h"
#include "game/ghost/PersonalBests.h"
#include "game/online/Replay.h"
#include "game/records/PlayStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// A fresh folder of its own (removed by the test).
struct TempDir {
    fs::path dir;
    TempDir() {
        dir = fs::temp_directory_path() / ("cosmic_bests_" + makeUuidV4().substr(0, 8));
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

const std::string kKey = "sylvanox_01-abcdef123456";
const std::vector<std::string> kRoute = {"a", "b", "c"};

/// A full run through a, b, c: cumulative splits and final time.
BestAttempt fullRun(std::vector<float> splits, float time, const std::string& date = "2026-11-20 10:00", BestSource src = BestSource::Run) {
    BestAttempt a;
    a.passage = BestPassage::ofSplits(splits, kRoute, time);
    a.source = src;
    a.date = date;
    return a;
}

size_t filesIn(const fs::path& dir) {
    std::error_code ec;
    size_t n = 0;
    if (!fs::is_directory(dir, ec)) return 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file()) ++n;
    return n;
}

}  // namespace

TEST_CASE("personal bests: a full run gives every segment, every enchaînement and the PB") {
    PersonalBests b;  // memory only
    // Segments 10, 8, 7, 5: start → a → b → c → exit.
    auto added = b.add(kKey, fullRun({10.0f, 18.0f, 25.0f}, 30.0f));
    CHECK(added.newPb);
    CHECK(added.sobBefore < 0.0f);
    CHECK_NEAR(added.sobAfter, 30.0f, 1e-4);
    // The route learned from it.
    CHECK(b.route(kKey) && *b.route(kKey) == kRoute);
    // The grid: (n + 2)·(n + 1)/2 = 10 ranges for 3 checkpoints, one passage each.
    CHECK(added.newBests.size() == 10);
    CHECK_NEAR(b.record(kKey, "", "a")->best, 10.0f, 1e-4);
    CHECK_NEAR(b.record(kKey, "a", "c")->best, 15.0f, 1e-4);
    CHECK_NEAR(b.record(kKey, "b", "")->best, 12.0f, 1e-4);
    CHECK_NEAR(b.record(kKey, "", "")->best, 30.0f, 1e-4);
    CHECK(b.record(kKey, "a", "b")->passages == 1);
    CHECK(b.record(kKey, "a", "b")->source == BestSource::Run);
    CHECK(b.record(kKey, "b", "a") == nullptr);
    CHECK(b.pb(kKey) && std::abs(b.pb(kKey)->time - 30.0f) < 1e-4f);
    CHECK(b.runs(kKey) && b.runs(kKey)->size() == 1);
    // The segments of the route, in order, and sums between boundaries.
    auto segs = b.segments(kKey);
    CHECK(segs.size() == 4);
    CHECK(segs[0].from.empty() && segs[0].to == "a" && segs[3].from == "c" && segs[3].to.empty());
    CHECK_NEAR(segs[2].best(), 7.0f, 1e-4);
    CHECK_NEAR(b.sumOfBests(kKey, 1, 3), 15.0f, 1e-4);  // a → c
    CHECK_NEAR(b.sumOfBests(kKey, 0, 4), 30.0f, 1e-4);
    CHECK(b.sumOfBests(kKey, 2, 2) < 0.0f);
    CHECK(b.sumOfBests(kKey, 0, 5) < 0.0f);

    // A second run: slower overall, but a gold on b → c (6 s). Every range it crossed gets a
    // passage; the SOB takes the gold at once.
    added = b.add(kKey, fullRun({11.0f, 20.0f, 26.0f}, 32.0f, "2026-11-21 10:00"));
    CHECK(!added.newPb);
    CHECK_NEAR(added.sobAfter, 29.0f, 1e-4);
    CHECK(b.record(kKey, "b", "c")->passages == 2);
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 6.0f, 1e-4);
    CHECK(b.record(kKey, "b", "c")->date == "2026-11-21 10:00");
    // An enchaînement can only equal the sum of its bests, never beat it.
    CHECK(b.record(kKey, "a", "c")->best >= b.sumOfBests(kKey, 1, 3) - 1e-4f);
    CHECK(b.runs(kKey)->size() == 2);
    CHECK(b.runs(kKey)->front().time == 32.0f);  // most recent first
    // A tie (to the millisecond) keeps the older best.
    added = b.add(kKey, fullRun({10.0004f, 18.5f, 26.0f}, 31.0f, "2026-11-22 10:00"));
    CHECK(b.record(kKey, "", "a")->date == "2026-11-20 10:00");
    // The SOB history: one point per day it changed.
    auto h = b.sobHistory(kKey);
    CHECK(h.size() == 2);
    CHECK(h[0].day == "2026-11-20" && std::abs(h[0].sob - 30.0f) < 1e-4f);
    CHECK(h[1].day == "2026-11-21" && std::abs(h[1].sob - 29.0f) < 1e-4f);
}

TEST_CASE("personal bests: trainings, enchaînements, StartPos and online runs join the same SOB") {
    PersonalBests b;
    b.setRoute(kKey, kRoute);
    b.add(kKey, fullRun({10.0f, 18.0f, 25.0f}, 30.0f));
    // A section training of a → b (2.95: from the level's start, segment 1 its run-up).
    BestAttempt t;
    t.passage.from = BestPassage::From::LevelStart;
    t.passage.checkpoints = {{"a", 11.0f}, {"b", 18.5f}};
    t.passage.countFrom = std::string("a");
    t.source = BestSource::Training;
    t.date = "2026-11-21 09:00";
    auto added = b.add(kKey, t);
    CHECK(added.newBests.size() == 1);
    CHECK(b.record(kKey, "a", "b")->source == BestSource::Training);
    CHECK(b.record(kKey, "a", "b")->passages == 2);
    CHECK_NEAR(b.sob(kKey), 29.5f, 1e-4);
    CHECK(!added.newPb);
    // An enchaînement b → exit (2.95: from a, its run-up): each segment entered is a segment time,
    // each range a record.
    BestAttempt c;
    c.passage.from = BestPassage::From::Checkpoint;
    c.passage.startCheckpoint = "a";
    c.passage.checkpoints = {{"a", 0.0f}, {"b", 7.8f}, {"c", 14.0f}};  // the start checkpoint itself is ignored
    c.passage.exitTime = 18.5f;
    c.passage.countFrom = std::string("b");
    c.source = BestSource::Chain;
    c.date = "2026-11-22 09:00";
    added = b.add(kKey, c);
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 6.2f, 1e-4);
    CHECK(b.record(kKey, "b", "c")->source == BestSource::Chain);
    CHECK_NEAR(b.record(kKey, "c", "")->best, 4.5f, 1e-4);
    CHECK_NEAR(b.record(kKey, "b", "")->best, 10.7f, 1e-4);
    CHECK(b.record(kKey, "b", "")->source == BestSource::Chain);
    CHECK(b.record(kKey, "a", "")->source == BestSource::Run);  // through the run-up: not counted
    CHECK(b.record(kKey, "a", "b")->passages == 2);
    CHECK(b.runs(kKey)->size() == 1);  // never a full run
    CHECK_NEAR(b.pb(kKey)->time, 30.0f, 1e-4);
    // A StartPos between a and b (2.95: on the SOB path, the real state there): a → b, where it
    // lies, never; b → c, entered after it, does.
    BestAttempt s;
    s.passage.from = BestPassage::From::Elsewhere;
    s.passage.checkpoints = {{"b", 2.0f}, {"c", 7.9f}};
    s.passage.exitTime = 12.0f;
    s.source = BestSource::StartPos;
    s.date = "2026-11-23 09:00";
    added = b.add(kKey, s);
    CHECK(b.record(kKey, "b", "c")->best < 6.0f);
    CHECK(b.record(kKey, "b", "c")->source == BestSource::StartPos);
    CHECK_NEAR(b.record(kKey, "c", "")->best, 4.1f, 1e-4);
    CHECK(b.record(kKey, "a", "b")->passages == 2);  // a → b not crossed whole: no passage
    // The player's run verified online: a full run, counted once however often it is read.
    BestAttempt o = fullRun({9.5f, 17.0f, 23.0f}, 27.0f, "2026-11-24 09:00", BestSource::Online);
    o.onlineId = "rec-1";
    added = b.add(kKey, o);
    CHECK(added.newPb);
    CHECK(b.pb(kKey)->source == BestSource::Online);
    CHECK_NEAR(b.record(kKey, "", "a")->best, 9.5f, 1e-4);
    CHECK(b.record(kKey, "", "a")->source == BestSource::Online);
    const int passages = b.record(kKey, "", "")->passages;
    b.add(kKey, o);
    CHECK(b.record(kKey, "", "")->passages == passages);
    CHECK(b.runs(kKey)->size() == 2);
    // SOB = 9.5 + 7.5 + 5.9 + 4.0 (online c → exit).
    CHECK_NEAR(b.sob(kKey), 9.5f + 7.5f + 5.9f + 4.0f, 1e-3);
}

TEST_CASE("personal bests: only segments that follow each other on the route make a range") {
    PersonalBests b;
    b.setRoute(kKey, kRoute);
    // A run that skipped b: start → a → c → exit.
    BestAttempt a;
    a.passage = BestPassage::ofSplits({10.0f, 20.0f}, {"a", "c"}, 26.0f);
    a.source = BestSource::Run;
    a.date = "2026-11-20 10:00";
    b.add(kKey, a);
    CHECK(b.record(kKey, "", "a") != nullptr);
    CHECK(b.record(kKey, "a", "c") == nullptr);  // b skipped: not the grid's a → c
    CHECK(b.record(kKey, "", "") == nullptr);
    CHECK(b.record(kKey, "c", "") == nullptr);  // 2.95: b → c not played whole (b's crossing unknown)
    CHECK(b.pb(kKey) != nullptr);  // still a full run
    // Checkpoints unknown (an old run): only the whole run.
    BestAttempt u;
    u.passage = BestPassage::ofSplits({10.0f, 18.0f, 25.0f}, {}, 30.0f);
    u.date = "2026-11-21 10:00";
    auto added = b.add(kKey, u);
    CHECK(added.newBests.size() == 1);
    CHECK_NEAR(b.record(kKey, "", "")->best, 30.0f, 1e-4);
    // A damaged attempt (time going back): only what comes before the damage.
    BestAttempt d;
    d.passage = BestPassage::ofSplits({10.0f, 9.0f, 25.0f}, kRoute, 30.0f);
    d.date = "2026-11-22 10:00";
    added = b.add(kKey, d);
    CHECK(added.newBests.empty());
    CHECK(b.record(kKey, "", "a")->passages == 2);
    // A route without checkpoints: the SOB is the whole run.
    PersonalBests one;
    BestAttempt w;
    w.passage = BestPassage::ofSplits({}, {}, 12.0f);
    w.date = "2026-11-20 10:00";
    one.add("x-1", w);
    CHECK(one.route("x-1") && one.route("x-1")->empty());
    CHECK_NEAR(one.sob("x-1"), 12.0f, 1e-4);
}

TEST_CASE("personal bests: the file, the replays of the bests and a damaged file") {
    TempDir t;
    const fs::path file = t.dir / "bests.json", replays = t.dir / "bests";
    std::vector<uint8_t> bytes(300, 7);
    {
        PersonalBests b(file, replays);
        b.load();
        BestAttempt a = fullRun({10.0f, 18.0f, 25.0f}, 30.0f);
        a.replay = &bytes;
        b.add(kKey, a);
        const auto* r = b.record(kKey, "a", "b");
        CHECK(r && r->replay.has() && r->replay.fromLevelStart);
        CHECK_NEAR(r->replay.start, 10.0f, 1e-4);
        CHECK_NEAR(r->replay.end, 18.0f, 1e-4);
        CHECK(fs::exists(b.replayPath(kKey, r->replay.file)));
        // A training attempt that beats b → c (2.95: from a, its run-up): its own replay, with its start.
        BestAttempt tr;
        tr.passage.from = BestPassage::From::Checkpoint;
        tr.passage.startCheckpoint = "a";
        tr.passage.checkpoints = {{"b", 7.5f}, {"c", 14.45f}};
        tr.passage.countFrom = std::string("b");
        tr.source = BestSource::Training;
        tr.date = "2026-11-21 10:00";
        tr.setup.checkpointId = "a";
        tr.setup.powers = {"dash"};
        std::vector<uint8_t> other(200, 9);
        tr.replay = &other;
        b.add(kKey, tr);
        std::vector<std::string> powers = {"dash", "glide"};
        b.setPowers(kKey, {"a", "b"}, &powers, "2026-11-21 10:00");
        CHECK(b.save());
        CHECK(filesIn(replays) == 2);
    }
    {
        PersonalBests b(file, replays);
        b.load();
        CHECK(!b.recovered());
        CHECK_NEAR(b.sob(kKey), 29.95f, 1e-4);
        const auto* r = b.record(kKey, "b", "c");
        CHECK(r && r->source == BestSource::Training && r->replay.has() && !r->replay.fromLevelStart);
        CHECK(r->replay.setup.checkpointId == "a" && r->replay.setup.powers == std::vector<std::string>{"dash"});
        CHECK_NEAR(r->replay.start, 7.5f, 1e-4);  // the range on its chronometer: from its entry, after the run-up
        CHECK_NEAR(r->replay.end - r->replay.start, 6.95f, 1e-4);
        CHECK(b.record(kKey, "", "")->replay.has());
        CHECK(b.pb(kKey) && b.runs(kKey)->size() == 1);
        CHECK(b.powers(kKey, {"a", "b"}) && b.powers(kKey, {"a", "b"})->size() == 2);
        CHECK(b.route(kKey) == std::optional<std::vector<std::string>>(kRoute));
        CHECK(b.sobHistory(kKey).size() == 2);
        // A best beaten without a replay: the old replay is no longer referenced by that range,
        // but the full run's still holds others; nothing referenced is ever deleted.
        b.add(kKey, fullRun({9.0f, 15.9f, 22.8f}, 27.7f, "2026-11-22 10:00"));
        CHECK(b.save());
        CHECK(filesIn(replays) == 0);  // every best now comes from the run without a replay
    }
    // A damaged file: set aside, the store starts again (never lost).
    FileUtil::writeTextAtomic(file, "{ pas du json");
    {
        PersonalBests b(file, replays);
        b.load();
        CHECK(b.recovered());
        CHECK(b.levels().empty());
        std::error_code ec;
        CHECK(fs::exists(fs::path(file.string() + ".corrupt"), ec));
    }
    // A file of a newer game: read, never written.
    FileUtil::writeTextAtomic(file, R"({"format":"cosmic.bests","version":99,"levels":{}})");
    {
        PersonalBests b(file, replays);
        b.load();
        CHECK(b.readOnly());
        CHECK(!b.save());
    }
}

TEST_CASE("personal bests: the old segment and training files are merged once, with their replays") {
    TempDir t;
    const fs::path segs = t.dir / "segments.json", golds = t.dir / "golds", train = t.dir / "training.json", trainReplays = t.dir / "training";
    // segments.json (2.60.3): two runs, gold segments with their checkpoints and one replay.
    const std::string folder = keptreplays::safeName(kKey, 96);
    std::error_code ec;
    fs::create_directories(golds / folder, ec);
    FileUtil::writeBinary(golds / folder / "20261120-1.cprz", std::vector<uint8_t>(120, 1));
    Json segJ = {{"format", "cosmic.segments"},
                 {"version", 1},
                 {"levels",
                  {{kKey,
                    {{"touched", "2026-11-20 10:00"},
                     {"best", {9.0f, 8.0f, 7.0f, 5.0f}},
                     {"best_split_count", 3},
                     {"best_checkpoints", kRoute},
                     {"best_sources", Json::array({Json(nullptr), Json{{"replay", "20261120-1.cprz"}, {"date", "2026-11-20 10:00"}, {"start", 10.0f}, {"end", 18.0f}},
                                                   Json(nullptr), Json(nullptr)})},
                     {"runs", Json::array({Json{{"date", "2026-11-20 10:00"}, {"time", 30.0f}, {"splits", {10.0f, 18.0f, 25.0f}}, {"checkpoints", kRoute}},
                                           Json{{"date", "2026-11-19 10:00"}, {"time", 33.0f}, {"splits", {9.0f, 19.0f, 27.0f}}, {"checkpoints", kRoute}}})}}},
                   // A level whose gold checkpoints were never known: kept until its route is.
                   {"other-1", {{"touched", "2026-11-18 10:00"}, {"best", {4.0f, 3.0f}}, {"best_split_count", 1}, {"runs", Json::array()}}}}}};
    FileUtil::writeTextAtomic(segs, segJ.dump());
    // training.json: a better b → c with its replay and start, the powers chosen for a → b.
    fs::create_directories(trainReplays / folder, ec);
    FileUtil::writeBinary(trainReplays / folder / "20261121-1.cprz", std::vector<uint8_t>(90, 2));
    SectionTrainingStart start;
    start.checkpointId = "b";
    start.powers = {"dash"};
    Json trJ = {{"format", "cosmic.training"},
                {"version", 1},
                {"levels",
                 {{kKey,
                   {{"touched", "2026-11-21 10:00"},
                    {"segments", Json::array({Json{{"from", "b"}, {"to", "c"}, {"best", 6.5f}, {"attempts", 12}, {"date", "2026-11-21 10:00"},
                                                    {"replay", "20261121-1.cprz"}, {"start", start.toJson()}},
                                              Json{{"from", "a"}, {"to", "b"}, {"best", -1.0f}, {"attempts", 0}, {"date", ""}, {"powers", {"glide"}}}})}}}}}};
    FileUtil::writeTextAtomic(train, trJ.dump());
    const fs::path file = t.dir / "bests.json", replays = t.dir / "bests";
    {
        PersonalBests b(file, replays, {segs, golds, train, trainReplays});
        b.load();
        // 2.95: their full runs only (from the level's start, they follow the rules of now): 9 + 8
        // + 7 + 5 from the two runs; the golds and the training's best (the rules before) not read.
        CHECK_NEAR(b.sob(kKey), 29.0f, 1e-4);
        CHECK(b.record(kKey, "b", "c")->source == BestSource::Run);
        CHECK_NEAR(b.record(kKey, "b", "c")->best, 7.0f, 1e-4);
        CHECK(b.record(kKey, "", "a")->source == BestSource::Run);
        CHECK_NEAR(b.record(kKey, "", "a")->best, 9.0f, 1e-4);
        CHECK(b.runs(kKey) && b.runs(kKey)->size() == 2 && b.runs(kKey)->front().time == 30.0f);
        CHECK_NEAR(b.pb(kKey)->time, 30.0f, 1e-4);
        CHECK(b.powers(kKey, {"a", "b"}) && *b.powers(kKey, {"a", "b"}) == std::vector<std::string>{"glide"});
        CHECK(!b.record(kKey, "a", "b")->replay.has());
        // The old replay folders are gone once the new file is written; the old files stay.
        CHECK(!fs::exists(golds) && !fs::exists(trainReplays));
        CHECK(fs::exists(segs) && fs::exists(train));
        // A gold route whose checkpoints are unknown: nothing of it (the rules before).
        CHECK(b.sob("other-1") < 0.0f);
        b.setRoute("other-1", {"m"});
        CHECK(b.sob("other-1") < 0.0f);
        b.save();
    }
    {
        // Read once: changing the old files changes nothing any more.
        segJ["levels"][kKey]["best"] = {1.0f, 1.0f, 1.0f, 1.0f};
        segJ["levels"][kKey]["runs"] = Json::array();
        FileUtil::writeTextAtomic(segs, segJ.dump());
        PersonalBests b(file, replays, {segs, golds, train, trainReplays});
        b.load();
        CHECK_NEAR(b.sob(kKey), 29.0f, 1e-4);
        CHECK(!b.rulesReset());
    }
}

TEST_CASE("personal bests: the statistics' segment bests (stats.json, the copy of the progression) are merged") {
    PlayStatsData d;
    LevelRuns& r = d.runs["sylvanox_02"];
    r.version = "0123456789abcdef0123";
    r.pb = 50.0f;
    r.pbRoute = {"p", "q"};
    SegmentRec s1{"", "p", {20.0f, 19.0f}, 19.0f, 18.0f};
    SegmentRec s2{"p", "q", {16.0f}, 16.0f, -1.0f};
    SegmentRec s3{"q", "", {14.0f}, 14.0f, -1.0f};
    r.segments = {s1, s2, s3};
    CHECK(r.sobRules == kSegmentRules);  // 2.95: statistics of these rules (those of before: tests/test_sob_rules.cpp)
    PersonalBests b;
    CHECK(!b.statsImported());
    b.importStats(d);
    CHECK(b.statsImported());
    const std::string key = GhostStore::levelKey("sylvanox_02", r.version);
    CHECK(b.route(key) && *b.route(key) == r.pbRoute);
    CHECK_NEAR(b.record(key, "", "p")->best, 18.0f, 1e-4);
    CHECK(b.record(key, "", "p")->source == BestSource::Training);
    CHECK(b.record(key, "", "p")->passages == 2);
    CHECK_NEAR(b.sob(key), 48.0f, 1e-4);
    CHECK_NEAR(b.record(key, "", "")->best, 50.0f, 1e-4);
    // Again (a restored copy): nothing counted twice, a better time taken.
    d.runs["sylvanox_02"].segments[1].best = 15.0f;
    b.importStats(d);
    CHECK_NEAR(b.sob(key), 47.0f, 1e-4);
    CHECK(b.record(key, "", "p")->passages == 2);
}
