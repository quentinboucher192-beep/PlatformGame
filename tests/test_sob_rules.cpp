// 2.95 — the rules of a segment of the personal Sum of Best (docs/GHOST_RACING.md § « Règles d'un
// segment »): timed from the first entry into it, deaths included, a respawn never starting one;
// counted only when entered by playing the whole segment before it (or from the level's start),
// never in a training's run-up; after a StartPos, from the first segment entered. And the reset of
// the rules' generation: the records kept, the ranges dropped and taken again from the full runs,
// nothing of before coming back (old files, the statistics).

#include <cmath>
#include <system_error>

#include "engine/core/FileUtil.h"
#include "engine/core/Json.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/KeptReplays.h"
#include "game/ghost/PersonalBests.h"
#include "game/online/Replay.h"
#include "game/records/PlayStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct TempDir {
    fs::path dir;
    TempDir() {
        dir = fs::temp_directory_path() / ("cosmic_sob_rules_" + makeUuidV4().substr(0, 8));
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

const std::string kKey = "sylvanox_01-0123456789ab";
const std::vector<std::string> kRoute = {"a", "b", "c"};

BestAttempt attempt(BestPassage::From from, const std::string& start, std::vector<std::pair<std::string, float>> cps, float exit, BestSource src,
                    const std::string& date) {
    BestAttempt a;
    a.passage.from = from;
    a.passage.startCheckpoint = start;
    a.passage.checkpoints = std::move(cps);
    a.passage.exitTime = exit;
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

TEST_CASE("segment rules: a death inside a segment is part of its time; a range entered from where an attempt appeared never counts") {
    PersonalBests b;
    b.setRoute(kKey, kRoute);
    // A run from the level's start that fell in a → b (its chronometer kept running): a → b is the
    // whole time from the first entry into it, the fall included.
    auto added = b.add(kKey, attempt(BestPassage::From::LevelStart, "", {{"a", 10.0f}, {"b", 31.0f}}, -1.0f, BestSource::Run, "2026-11-27 10:00"));
    CHECK_NEAR(b.record(kKey, "", "a")->best, 10.0f, 1e-4);
    CHECK_NEAR(b.record(kKey, "a", "b")->best, 21.0f, 1e-4);
    CHECK(added.newBests.size() == 3);  // start → a, a → b, start → b
    // A section training that appears at a (a standing start, no run-up): a → b is not entered as
    // in a run — never counted, whatever its time.
    added = b.add(kKey, attempt(BestPassage::From::Checkpoint, "a", {{"b", 4.0f}, {"c", 9.0f}}, -1.0f, BestSource::Training, "2026-11-27 10:05"));
    CHECK_NEAR(b.record(kKey, "a", "b")->best, 21.0f, 1e-4);
    CHECK(b.record(kKey, "a", "b")->passages == 1);
    CHECK(b.record(kKey, "a", "c") == nullptr);
    // b → c was entered after playing a → b whole in that attempt: it counts.
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 5.0f, 1e-4);
    CHECK(added.newBests.size() == 1);
    // A checkpoint skipped (never lit): the segment after it was not entered by a whole one: no range.
    PersonalBests s;
    s.setRoute(kKey, kRoute);
    s.add(kKey, attempt(BestPassage::From::LevelStart, "", {{"a", 10.0f}, {"c", 20.0f}}, 25.0f, BestSource::Run, "2026-11-27 11:00"));
    CHECK(s.record(kKey, "", "a") != nullptr);
    CHECK(s.record(kKey, "a", "c") == nullptr);
    CHECK(s.record(kKey, "c", "") == nullptr);  // b → c not played whole (b's crossing unknown)
    CHECK(s.pb(kKey) != nullptr);               // still a full run (a record)
}

TEST_CASE("segment rules: training N counts only N — its run-up (the segment before) never; the first segment from the level's start counts") {
    PersonalBests b;
    b.setRoute(kKey, kRoute);
    // Training b → c (segment 3): it starts at a (the start of segment 2, its run-up), b → c timed
    // from the first entry into it.
    BestAttempt t = attempt(BestPassage::From::Checkpoint, "a", {{"b", 7.0f}, {"c", 12.5f}}, -1.0f, BestSource::Training, "2026-11-27 10:00");
    t.passage.countFrom = std::string("b");
    auto added = b.add(kKey, t);
    CHECK(added.newBests.size() == 1);
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 5.5f, 1e-4);
    CHECK(b.record(kKey, "a", "b") == nullptr);  // the run-up
    CHECK(b.record(kKey, "a", "c") == nullptr);  // through the run-up
    // What the live « SEGMENT D'OR » and the statistics read: the same ranges.
    const auto spans = b.counted(kKey, t.passage);
    CHECK(spans.size() == 1);
    CHECK((spans.front().range == PersonalBests::Range{"b", "c"}));
    CHECK_NEAR(spans.front().end - spans.front().start, 5.5f, 1e-4);
    // Training a → b (segment 2): its run-up is segment 1, from the level's start, never counted.
    t = attempt(BestPassage::From::LevelStart, "", {{"a", 9.0f}, {"b", 15.0f}}, -1.0f, BestSource::Training, "2026-11-27 10:05");
    t.passage.countFrom = std::string("a");
    added = b.add(kKey, t);
    CHECK(added.newBests.size() == 1);
    CHECK_NEAR(b.record(kKey, "a", "b")->best, 6.0f, 1e-4);
    CHECK(b.record(kKey, "", "a") == nullptr);
    CHECK(b.record(kKey, "", "b") == nullptr);
    // Training segment 1 (from the level's start, no run-up): it counts.
    t = attempt(BestPassage::From::LevelStart, "", {{"a", 8.5f}}, -1.0f, BestSource::Training, "2026-11-27 10:10");
    b.add(kKey, t);
    CHECK_NEAR(b.record(kKey, "", "a")->best, 8.5f, 1e-4);
    // An enchaînement a → exit trained from the level's start (run-up: segment 1): a → b, b → c,
    // c → exit and their ranges count, nothing from the start.
    t = attempt(BestPassage::From::LevelStart, "", {{"a", 9.0f}, {"b", 14.8f}, {"c", 20.0f}}, 24.0f, BestSource::Chain, "2026-11-27 10:15");
    t.passage.countFrom = std::string("a");
    b.add(kKey, t);
    CHECK_NEAR(b.record(kKey, "a", "b")->best, 5.8f, 1e-4);
    CHECK_NEAR(b.record(kKey, "a", "")->best, 15.0f, 1e-4);
    CHECK(b.record(kKey, "", "") == nullptr);
    CHECK(b.pb(kKey) == nullptr);  // never a full run
}

TEST_CASE("segment rules: after a StartPos, the segment it lies in never counts, the next ones entered do") {
    PersonalBests b;
    b.setRoute(kKey, kRoute);
    // A StartPos in a → b: b is the first boundary crossed, then c, then the exit.
    BestAttempt s = attempt(BestPassage::From::Elsewhere, "", {{"b", 32.0f}, {"c", 38.0f}}, 41.5f, BestSource::StartPos, "2026-11-27 10:00");
    auto added = b.add(kKey, s);
    CHECK(b.record(kKey, "a", "b") == nullptr);  // the segment it lies in: partial
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 6.0f, 1e-4);
    CHECK(b.record(kKey, "b", "c")->source == BestSource::StartPos);
    CHECK_NEAR(b.record(kKey, "c", "")->best, 3.5f, 1e-4);
    CHECK_NEAR(b.record(kKey, "b", "")->best, 9.5f, 1e-4);
    CHECK(added.newBests.size() == 3);
    CHECK(b.pb(kKey) == nullptr);
}

TEST_CASE("segment rules: a file of the rules before is reset — records kept, ranges dropped and taken again from the full runs") {
    TempDir t;
    const fs::path file = t.dir / "bests.json", replays = t.dir / "bests";
    const std::string folder = keptreplays::safeName(kKey, 96);
    std::error_code ec;
    fs::create_directories(replays / folder, ec);
    FileUtil::writeBinary(replays / folder / "pb.cprz", std::vector<uint8_t>(200, 1));     // the PB's replay
    FileUtil::writeBinary(replays / folder / "train.cprz", std::vector<uint8_t>(100, 2));  // a training's (a respawn's « 22 s »)
    // A file of 2.91 – 2.94: no « rules ». The PB (10, 8, 7, 5), an absurd b → c of 1 s from a
    // training, an enchaînement, the whole run's record with its replay.
    Json pbRun{{"date", "2026-11-20 10:00"}, {"time", 30.0f}, {"splits", {10.0f, 18.0f, 25.0f}}, {"checkpoints", kRoute}, {"source", "run"}};
    Json oldRun{{"date", "2026-11-19 10:00"}, {"time", 33.0f}, {"splits", {9.0f, 19.0f, 27.0f}}, {"checkpoints", kRoute}, {"source", "run"}};
    auto range = [](const std::string& f, const std::string& to, float best, const char* src, Json replay = Json()) {
        Json r{{"from", f}, {"to", to}, {"best", best}, {"count", 4}, {"source", src}, {"date", "2026-11-21 10:00"}};
        if (!replay.is_null()) r["replay"] = replay;
        return r;
    };
    Json j{{"format", "cosmic.bests"},
           {"version", 1},
           {"migrated", {"segments.json", "training.json", "stats.json"}},
           {"levels",
            {{kKey,
              {{"touched", "2026-11-21 10:00"},
               {"route", kRoute},
               {"route_given", true},
               {"ranges", Json::array({range("", "a", 9.0f, "run"), range("a", "b", 8.0f, "run"), range("b", "c", 1.0f, "practice", Json{{"file", "train.cprz"}, {"start", 0.0f}, {"end", 1.0f}}),
                                       range("c", "", 5.0f, "run"), range("a", "c", 9.5f, "chain"),
                                       range("", "", 30.0f, "run", Json{{"file", "pb.cprz"}, {"start", 0.0f}, {"end", 30.0f}})})},
               {"pb", pbRun},
               {"runs", Json::array({pbRun, oldRun})},
               {"history", Json::array({Json{"2026-11-20", 21.0f, 30.0f}})}}}}}};
    FileUtil::writeTextAtomic(file, j.dump());
    {
        PersonalBests b(file, replays);
        b.load();
        CHECK(b.rulesReset());
        CHECK(!b.readOnly());
        // The records stay: the PB, the runs, the whole run's record with its replay.
        CHECK(b.pb(kKey) && std::abs(b.pb(kKey)->time - 30.0f) < 1e-4f);
        CHECK(b.runs(kKey) && b.runs(kKey)->size() == 2);
        const auto* whole = b.record(kKey, "", "");
        CHECK(whole && std::abs(whole->best - 30.0f) < 1e-4f && whole->replay.has() && whole->replay.file == "pb.cprz");
        // The ranges of before are gone (the 1 s b → c, the enchaînement's 9.5 s), taken again from
        // the full runs kept: the best of each segment in them (9, 8, 7, 5).
        CHECK_NEAR(b.record(kKey, "b", "c")->best, 7.0f, 1e-4);
        CHECK(b.record(kKey, "b", "c")->source == BestSource::Run);
        CHECK_NEAR(b.record(kKey, "", "a")->best, 9.0f, 1e-4);
        CHECK_NEAR(b.record(kKey, "a", "c")->best, 15.0f, 1e-4);
        CHECK_NEAR(b.sob(kKey), 9.0f + 8.0f + 7.0f + 5.0f, 1e-4);
        // The SOB of before is forgotten in the history; the PB stays.
        CHECK(!b.sobHistory(kKey).empty() && b.sobHistory(kKey).front().sob < 0.0f && std::abs(b.sobHistory(kKey).front().pb - 30.0f) < 1e-4f);
    }
    // Written at once: the new rules' generation, the training's replay no longer named (deleted),
    // the PB's kept.
    auto saved = FileUtil::loadJson(file);
    CHECK(saved && jget<int>(*saved, "rules", 0) == PersonalBests::kRules && jget<int>(*saved, "version", 0) == PersonalBests::kVersion);
    CHECK(fs::exists(replays / folder / "pb.cprz"));
    CHECK(!fs::exists(replays / folder / "train.cprz"));
    CHECK(filesIn(replays) == 1);
    {
        // Read again: nothing more to reset.
        PersonalBests b(file, replays);
        b.load();
        CHECK(!b.rulesReset());
        CHECK_NEAR(b.sob(kKey), 29.0f, 1e-4);
    }
    // A file of a newer game is still only read.
    Json newer = *saved;
    newer["version"] = PersonalBests::kVersion + 1;
    newer["rules"] = PersonalBests::kRules + 1;
    FileUtil::writeTextAtomic(file, newer.dump());
    {
        PersonalBests b(file, replays);
        b.load();
        CHECK(b.readOnly());
        CHECK(!b.rulesReset());
        CHECK(!b.save());
    }
}

TEST_CASE("segment rules: nothing of before comes back — old segment and training files, the statistics of the rules before") {
    TempDir t;
    const fs::path segs = t.dir / "segments.json", golds = t.dir / "golds", train = t.dir / "training.json", trainReplays = t.dir / "training";
    // segments.json (2.30 – 2.90): its runs come in, its gold segments (« best ») do not.
    Json segJ = {{"format", "cosmic.segments"},
                 {"version", 1},
                 {"levels",
                  {{kKey,
                    {{"touched", "2026-11-20 10:00"},
                     {"best", {1.0f, 1.0f, 1.0f, 1.0f}},
                     {"best_split_count", 3},
                     {"best_checkpoints", kRoute},
                     {"runs", Json::array({Json{{"date", "2026-11-20 10:00"}, {"time", 30.0f}, {"splits", {10.0f, 18.0f, 25.0f}}, {"checkpoints", kRoute}}})}}}}}};
    FileUtil::writeTextAtomic(segs, segJ.dump());
    // training.json: its best times (trained from a standstill) do not come in, its powers do.
    Json trJ = {{"format", "cosmic.training"},
                {"version", 1},
                {"levels",
                 {{kKey,
                   {{"touched", "2026-11-21 10:00"},
                    {"segments", Json::array({Json{{"from", "b"}, {"to", "c"}, {"best", 0.5f}, {"attempts", 12}, {"date", "2026-11-21 10:00"}},
                                              Json{{"from", "a"}, {"to", "b"}, {"best", -1.0f}, {"powers", {"glide"}}}})}}}}}};
    FileUtil::writeTextAtomic(train, trJ.dump());
    PersonalBests b(t.dir / "bests.json", t.dir / "bests", {segs, golds, train, trainReplays});
    b.load();
    CHECK_NEAR(b.sob(kKey), 30.0f, 1e-4);  // the run's own segments (10, 8, 7, 5)
    CHECK_NEAR(b.record(kKey, "b", "c")->best, 7.0f, 1e-4);
    CHECK(b.powers(kKey, {"a", "b"}) && *b.powers(kKey, {"a", "b"}) == std::vector<std::string>{"glide"});
    // The statistics (stats.json, a restored copy of the progression): segments of the rules
    // before (no « sr ») never read; the PB, a full run's, is.
    PlayStatsData d;
    LevelRuns& r = d.runs["sylvanox_02"];
    r.version = "0123456789abcdef0123";
    r.pb = 50.0f;
    r.pbRoute = {"p", "q"};
    r.segments = {SegmentRec{"", "p", {20.0f}, 3.0f, 2.0f}, SegmentRec{"p", "q", {16.0f}, 16.0f, -1.0f}, SegmentRec{"q", "", {14.0f}, 14.0f, -1.0f}};
    PlayStatsData read = PlayStatsData::fromJson(d.toJson());
    CHECK(read.runs.at("sylvanox_02").sobRules == kSegmentRules);  // written with its generation
    Json old = d.toJson();
    old["runs"]["sylvanox_02"].erase("sr");  // a file of before
    read = PlayStatsData::fromJson(old);
    CHECK(read.runs.at("sylvanox_02").sobRules == 0);
    const std::string key = GhostStore::levelKey("sylvanox_02", r.version);
    b.importStats(read);
    CHECK(b.record(key, "", "p") == nullptr);
    CHECK(b.sob(key) < 0.0f);
    CHECK_NEAR(b.record(key, "", "")->best, 50.0f, 1e-4);
    // Of these rules: taken.
    read.runs.at("sylvanox_02").sobRules = kSegmentRules;
    b.importStats(read);
    CHECK_NEAR(b.record(key, "", "p")->best, 2.0f, 1e-4);
    CHECK_NEAR(b.sob(key), 2.0f + 16.0f + 14.0f, 1e-4);
    // The statistics themselves: a level's bests of before go at its next segments (its times stay).
    PlayStatsStore store;
    store.setData(PlayStatsData::fromJson(old));
    PlayStatsStore::Delta delta;
    delta.levelId = "sylvanox_02";
    delta.classic = true;
    delta.version = r.version;
    delta.segments = {{{"p", "q"}, 17.0f}};
    store.merge(delta);
    const LevelRuns& after = store.data().runs.at("sylvanox_02");
    CHECK(after.sobRules == kSegmentRules);
    CHECK(after.segment("", "p")->best < 0.0f && after.segment("", "p")->trainBest < 0.0f);
    CHECK(after.segment("", "p")->times.size() == 1);
    CHECK_NEAR(after.segment("p", "q")->best, 17.0f, 1e-3);  // its times of before are not a best any more
    // Merged with a copy of before (the progression restored): its bests are not taken.
    PlayStatsData mine = store.data();
    PlayStatsData theirs = PlayStatsData::fromJson(old);
    const PlayStatsData merged = mergePlayStats(mine, theirs, store.today());
    CHECK(merged.runs.at("sylvanox_02").segment("", "p")->best < 0.0f);
    CHECK_NEAR(merged.runs.at("sylvanox_02").segment("p", "q")->best, 17.0f, 1e-3);
}
