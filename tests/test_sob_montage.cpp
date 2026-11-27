// 2.60.3 — the Sum of Best « tout confondu » (full runs and section trainings together) and its
// montage (« Regarder »): the replays kept with the best segments (kept, replaced, cleaned up,
// one folder per level version), the best training attempts' replays and their start, the
// montage's segments and their source, the statistics (a training best lowers the Sum of Best,
// never the PB, the times nor the regularity; merged with a copy), and a section training's
// replay playing the attempt again exactly from its checkpoint.

#include <cmath>
#include <system_error>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/KeptReplays.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/ghost/PersonalBests.h"
#include "game/ghost/SobMontage.h"
#include "game/ghost/TrainingSegments.h"
#include "game/ghost/WorldRecordSplits.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/player/Player.h"
#include "game/records/PlayStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("cosmic_sob_" + makeUuidV4().substr(0, 8));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<uint8_t> bytesOf(uint8_t v, size_t n = 64) { return std::vector<uint8_t>(n, v); }

bool isFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
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

namespace {

/// A full run through `ids` (2.91: given to the ONE personal Sum of Best).
BestAttempt runOf(std::vector<float> splits, std::vector<std::string> ids, float time, const char* date, const std::vector<uint8_t>* replay = nullptr) {
    BestAttempt a;
    a.passage = BestPassage::ofSplits(splits, ids, time);
    a.date = date;
    a.replay = replay;
    return a;
}

}  // namespace

TEST_CASE("sum of best montage: each level version keeps its own best replays") {
    TempDir t;
    PersonalBests h(t.path / "bests.json", t.path / "bests", {});
    h.load();
    const std::string v1 = GhostStore::levelKey("noyau_01", std::string(64, 'a'));
    const std::string v2 = GhostStore::levelKey("noyau_01", std::string(64, 'b'));
    CHECK(v1 != v2);
    const auto a = bytesOf(1), b = bytesOf(2);
    h.add(v1, runOf({10.0f}, {"cp1"}, 20.0f, "2026-11-01 10:00", &a));
    h.add(v2, runOf({12.0f}, {"cp1"}, 25.0f, "2026-11-01 11:00", &b));
    const auto* r1 = h.record(v1, "", "cp1");
    const auto* r2 = h.record(v2, "", "cp1");
    CHECK(r1 && r2 && r1->replay.has() && r2->replay.has());
    const fs::path p1 = h.replayPath(v1, r1->replay.file), p2 = h.replayPath(v2, r2->replay.file);
    CHECK(p1.parent_path() != p2.parent_path());
    CHECK(FileUtil::readBinary(p1) == a);
    CHECK(FileUtil::readBinary(p2) == b);
    // A run of version 2 never changes version 1's bests nor their replays.
    const auto c = bytesOf(3);
    h.add(v2, runOf({5.0f}, {"cp1"}, 9.0f, "2026-11-02 10:00", &c));
    CHECK(h.record(v1, "", "cp1")->replay.file == r1->replay.file);
    CHECK_NEAR(h.record(v1, "", "cp1")->best, 10.0f, 1e-5);
    CHECK(h.save());
    CHECK(isFile(p1) && !isFile(p2));
}

TEST_CASE("sum of best montage: an old best takes the replay of a kept run that did exactly its time") {
    TempDir t;
    PersonalBests h(t.path / "bests.json", t.path / "bests", {});
    h.load();
    const std::string key = GhostStore::levelKey("sylvanox_02", std::string(64, 'c'));
    h.add(key, runOf({10.0f, 20.0f}, {"cp1", "cp2"}, 30.0f, "2026-10-01 10:00"));  // no replay
    CHECK(!h.record(key, "cp1", "cp2")->replay.has());
    const fs::path run = t.path / "pb.cprz";
    FileUtil::writeBinary(run, bytesOf(7));
    using A = PersonalBests::Adoption;
    // Not the best's time: refused; the best's time: kept (a copy), never twice.
    CHECK(!h.adoptReplay(key, run, "2026-10-01 10:00", {A{{"cp1", "cp2"}, 10.0f, 21.0f}}));
    CHECK(h.adoptReplay(key, run, "2026-10-01 10:00", {A{{"cp1", "cp2"}, 10.0f, 20.0f}, A{{"cp2", ""}, 20.0f, 30.0f}}));
    CHECK(!h.record(key, "", "cp1")->replay.has());
    CHECK(h.record(key, "cp1", "cp2")->replay.has() && h.record(key, "cp1", "cp2")->replay.file == h.record(key, "cp2", "")->replay.file);
    CHECK(!h.adoptReplay(key, run, "2026-10-01 10:00", {A{{"cp1", "cp2"}, 10.0f, 20.0f}}));
    CHECK(!h.adoptReplay("other", run, "d", {A{{"", "cp1"}, 0.0f, 10.0f}}));
    std::error_code ec;
    fs::remove(run, ec);  // the record replaced later: the best keeps its own copy
    CHECK(isFile(h.replayPath(key, h.record(key, "cp1", "cp2")->replay.file)));
}

TEST_CASE("sum of best montage: the montage follows the ONE Sum of Best and says where each segment was done") {
    TempDir t;
    PersonalBests h(t.path / "bests.json", t.path / "bests", {});
    h.load();
    const std::string key = GhostStore::levelKey("sylvanox_03", std::string(64, 'd'));
    h.setRoute(key, {"a", "b"});
    // A full run with its replay (segments 10, 11, 11), then one without (before 2.60.3: 12, 10, 10).
    const auto run1 = bytesOf(1);
    h.add(key, runOf({10.0f, 21.0f}, {"a", "b"}, 32.0f, "2026-11-10 09:00", &run1));
    h.add(key, runOf({12.0f, 22.0f}, {"a", "b"}, 32.0f, "2026-11-11 09:00"));
    // A section training of a → b did better (7.5): its attempt (2.95: from the level's start with
    // its chosen powers, segment 1 its run-up), with its setup.
    SectionTrainingStart start;
    start.powers = {"dash"};
    start.powersChosen = true;
    const auto tr = bytesOf(2);
    BestAttempt train;
    train.passage.from = BestPassage::From::LevelStart;
    train.passage.checkpoints = {{"a", 10.5f}, {"b", 18.0f}};
    train.passage.countFrom = std::string("a");
    train.source = BestSource::Training;
    train.date = "2026-11-17 20:00";
    train.replay = &tr;
    train.setup = start;
    h.add(key, train);
    MontageRun last{"/local/last.cprz", "2026-11-11 09:00", {12.0f, 22.0f}, 32.0f, {}};
    MontagePlan p = buildMontagePlan(h, key, {last});
    CHECK_NEAR(p.total, h.sob(key), 1e-5);  // the same Sum of Best as every screen
    CHECK_NEAR(p.total, 10.0f + 7.5f + 10.0f, 1e-4);
    CHECK(p.segments.size() == 3);
    CHECK(p.withReplay == 3);
    // Segment 1: the first run's best, with its own replay (a copy kept with the bests).
    CHECK(!p.segments[0].training && p.segments[0].source == BestSource::Run && p.segments[0].hasReplay() && !p.segments[0].adopt);
    CHECK(p.segments[0].from.empty() && p.segments[0].to == "a");
    // Segment 2: the training's attempt, from its start, with its setup.
    const MontageSegment& s1 = p.segments[1];
    CHECK(s1.training && s1.source == BestSource::Training && s1.date == "2026-11-17 20:00");
    CHECK_NEAR(s1.start, 10.5f, 1e-6);  // from its entry into a → b (after its run-up)
    CHECK_NEAR(s1.end, 18.0f, 1e-6);
    CHECK(s1.trainingStart.powersChosen && s1.trainingStart.powers == std::vector<std::string>{"dash"});
    CHECK(s1.from == "a" && s1.to == "b");
    // Segment 3: a best without a replay the last run did in exactly that time (to be adopted).
    const MontageSegment& s2 = p.segments[2];
    CHECK(!s2.training && s2.adopt && s2.file == "/local/last.cprz");
    CHECK_NEAR(s2.start, 22.0f, 1e-6);
    CHECK_NEAR(s2.end, 32.0f, 1e-6);
    CHECK_NEAR(p.pbTime, 32.0f, 1e-4);
    CHECK_NEAR(p.possibleGain(), 32.0f - 27.5f, 1e-4);
    // Without that run: said to have no replay (never another run's time instead).
    MontagePlan q = buildMontagePlan(h, key);
    CHECK(q.withReplay == 2 && !q.segments[2].hasReplay());
    CHECK_NEAR(q.total, 27.5f, 1e-4);
    // A range of the route (an enchaînement's « Regarder »): its segments, its record.
    MontagePlan r = buildMontagePlan(h, key, {}, 1, 3);
    CHECK(r.segments.size() == 2 && r.segments[0].from == "a" && r.segments[1].to.empty());
    CHECK_NEAR(r.total, 17.5f, 1e-4);
    CHECK_NEAR(r.pbTime, h.record(key, "a", "")->best, 1e-5);
    // Unknown route: nothing.
    CHECK(buildMontagePlan(h, "nothing-here").segments.empty());
}

TEST_CASE("sum of best montage: in the statistics a training best lowers the Sum of Best, never the PB, the times nor the regularity") {
    PlayStatsStore store;
    const int today = daysFromCivil(2026, 11, 17);
    store.dayOverride = today - 1;
    PlayStatsStore::Delta run;
    run.levelId = "sylvanox_02";
    run.levelName = "Sylvanox 2";
    run.classic = true;
    run.version = "v1";
    run.attempt = true;
    run.segments = {{{"", "cp1"}, 10.0f}, {{"cp1", "cp2"}, 20.0f}, {{"cp2", ""}, 30.0f}};
    run.finish = 60.0f;
    run.route = {"cp1", "cp2"};
    store.merge(run);
    const LevelRuns before = store.data().runs.at("sylvanox_02");
    CHECK_NEAR(before.sob(), 60.0f, 1e-4);
    // A section training of the same version: 15 s on the second segment.
    store.dayOverride = today;
    PlayStatsStore::Delta tr;
    tr.levelId = "sylvanox_02";
    tr.trainingLevel = true;
    tr.version = "v1";
    tr.training = {{{"cp1", "cp2"}, 15.0f}, {{"cp2", ""}, 31.0f}};
    store.merge(tr);
    const LevelRuns& r = store.data().runs.at("sylvanox_02");
    CHECK_NEAR(r.sob(), 55.0f, 1e-4);  // 10 + 15 + 30
    CHECK_NEAR(r.pb, 60.0f, 1e-4);
    CHECK(r.attempts == 1 && r.finished == 1 && r.finishes.size() == 1);
    const SegmentRec* s = r.segment("cp1", "cp2");
    CHECK(s && s->times.size() == 1 && std::abs(s->best - 20.0f) < 1e-4f && std::abs(s->trainBest - 15.0f) < 1e-4f);
    CHECK(segmentReports(r)[1].best == s->best);  // the zones to work on stay the full runs'
    CHECK(r.history.size() == 2);
    CHECK_NEAR(r.history.back().sob, 55.0f, 1e-4);
    CHECK_NEAR(r.history.back().pb, 60.0f, 1e-4);
    // Read back from the file's form.
    PlayStatsData d = PlayStatsData::fromJson(store.data().toJson());
    CHECK_NEAR(d.runs.at("sylvanox_02").sob(), 55.0f, 1e-4);
    // Another computer's copy with a better training time: merged (the better one), counted once.
    PlayStatsData theirs = d;
    for (auto& seg : theirs.runs.at("sylvanox_02").segments)
        if (seg.from == "cp2" && seg.to.empty()) seg.trainBest = 28.0f;
    const PlayStatsData m = mergePlayStats(d, theirs, today);
    CHECK_NEAR(m.runs.at("sylvanox_02").sob(), 53.0f, 1e-4);
    CHECK_NEAR(m.runs.at("sylvanox_02").pb, 60.0f, 1e-4);
    CHECK(m.runs.at("sylvanox_02").finishes.size() == 1);
    CHECK(mergePlayStats(m, theirs, today).toJson().dump() == m.toJson().dump());
    // A training of another version never touches this one's: that level's stats start again.
    PlayStatsStore::Delta v2 = tr;
    v2.version = "v2";
    store.merge(v2);
    CHECK(store.data().runs.at("sylvanox_02").pb < 0.0f);
    CHECK(store.data().runs.at("sylvanox_02").finishes.empty());
}

namespace {

struct Fixture {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Fixture() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices services() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

Fixture& fx() {
    static Fixture f;
    return f;
}

/// Flat level: checkpoints at 800 and 1500, exit at the end.
LevelData flatLevel() {
    LevelData l = LevelData::makeEmpty("test_sob_montage", "Test somme des meilleurs", "sylvanox");
    l.intro.steps.clear();
    for (float x : {800.0f, 1500.0f}) {
        LevelObject c;
        c.id = x < 1000.0f ? "cp1" : "cp2";
        c.type = "checkpoint";
        c.layer = "gameplay";
        c.rect = {x, 736, 32, 64};
        l.objects.push_back(c);
    }
    return l;
}

}  // namespace

TEST_CASE("sum of best montage: a section training's replay plays the attempt again exactly from its checkpoint") {
    const LevelData level = flatLevel();
    SectionTrainingStart start;
    start.checkpointId = "cp1";
    start.hasSpawn = true;
    start.spawn = {816.0f, 800.0f};
    start.powers = {"dash"};
    start.powersChosen = true;
    // The attempt as the game plays it: the level loaded and seeded, then its start (GameScene).
    ReplayData replay;
    float time = 0.0f;
    Vec2 feet;
    {
        GameWorld w(fx().services(), WorldMode::Play);
        w.setKeysPerSlot(true);
        w.load(level);
        w.seedGameplay(levelSeed(level.id));
        applySectionTraining(w, start);
        CHECK(w.checkpointId() == "cp1");
        CHECK(w.player().hasPower("dash"));
        ReplayRecorder rec;
        rec.begin(level.id, fingerprintGameplay(fx().content, level), makeUuidV4(), w.gameplaySeed());
        InputFrame prev;
        for (int i = 0; i < 120 * 20 && w.checkpointId() != "cp2"; ++i) {
            InputFrame f;
            f.held = 1u << static_cast<int>(Action::MoveRight);
            f.moveX = 127;
            if ((i / 30) % 3 == 0) f.held |= 1u << static_cast<int>(Action::Jump);
            f.pressed = f.held & ~prev.held;
            f.released = prev.held & ~f.held;
            prev = f;
            rec.recordInput(f);
            stepWorld(w, f, kSimDt);
            rec.tick(w);
        }
        CHECK(w.checkpointId() == "cp2");
        time = w.timer().elapsed();
        feet = w.player().feet();
        rec.finish(w, time);
        replay = rec.take();
    }
    CHECK(replay.exact());
    CHECK(time > 0.5f);
    // Played with the same start: the same run, every digest checked, the same time.
    {
        ReplayPlayback p(fx().services(), level, replay, [start](GameWorld& w) { applySectionTraining(w, start); });
        while (p.step()) {
        }
        CHECK(p.finished());
        CHECK(!p.desynced());
        CHECK(p.checksPassed() >= 2);
        CHECK_NEAR(p.runTime(), time, 1e-5);
        CHECK((p.world().player().feet() - feet).length() < 0.01f);
        CHECK(p.world().checkpointId() == "cp2");
    }
    // The montage's way: a jump to the attempt's first step, then played to its end.
    {
        ReplayPlayback p(fx().services(), level, replay, [start](GameWorld& w) { applySectionTraining(w, start); });
        p.seekTime(0.004);
        p.finishSeek();
        CHECK(p.tick() > 0 && p.runTime() > 0.0f && p.runTime() < 0.02f);
        while (p.runTime() + 0.0005f < time && p.step()) {
        }
        CHECK(!p.desynced());
        CHECK_NEAR(p.runTime(), time, 1e-5);
    }
    // Without its start, the replay is another run (from the level's start): never shown as this one.
    {
        ReplayPlayback p(fx().services(), level, replay);
        while (p.step()) {
        }
        CHECK(p.desynced() || (p.world().player().feet() - feet).length() > 1.0f);
    }
}

TEST_CASE("segments: the world record's splits come from a verified record of this version, never a pending one") {
    const Json entries = Json::parse(R"([
        {"rank": 2, "record_id": "r2", "time_ms": 61000, "has_replay": true, "player": {"username": "Bea"}},
        {"rank": 1, "record_id": "r1", "time_ms": 60000, "has_replay": false, "player": {"username": "Ana"}},
        {"rank": 3, "record_id": "", "time_ms": 62000, "has_replay": true},
        {"rank": 4, "record_id": "r4", "time_ms": 63000, "has_replay": true, "player": {"username": "Cy"}}
    ])");
    auto c = worldRecordCandidates(entries);
    CHECK(c.size() == 2);  // the replay of n°1 is not verified yet: never its splits
    CHECK(c[0].recordId == "r2" && c[0].player == "Bea" && c[0].timeMs == 61000);
    CHECK(c[1].recordId == "r4");
    CHECK(worldRecordCandidates(Json::object()).empty());
    // Its splits from its replay, only on this exact version.
    ReplayData r;
    r.levelHash = std::string(64, 'a');
    r.frames.resize(120);
    r.events.push_back({30, ReplayEventType::Split, 0, 1000});
    r.events.push_back({60, ReplayEventType::Split, 1, 2000});
    auto s = worldRecordSplits(r, std::string(64, 'a'));
    CHECK(s && s->size() == 2);
    CHECK(s && std::abs((*s)[0] - 1.0f) < 0.05f && std::abs((*s)[1] - 2.0f) < 0.05f);
    CHECK(!worldRecordSplits(r, std::string(64, 'b')));
    // The cumulative gaps of this run against it (the exit after the last split).
    std::vector<float> mine{10.0f, 21.0f}, wr{9.5f, 22.0f};
    CHECK_NEAR(*cumulativeGap(mine, 30.0f, wr, 29.0f, 0), 0.5f, 1e-5);
    CHECK_NEAR(*cumulativeGap(mine, 30.0f, wr, 29.0f, 1), -1.0f, 1e-5);
    CHECK_NEAR(*cumulativeGap(mine, 30.0f, wr, 29.0f, 2), 1.0f, 1e-5);
    CHECK(!cumulativeGap(mine, 30.0f, {9.5f}, 29.0f, 0));  // another route: no gap
    CHECK(!cumulativeGap(mine, 30.0f, wr, 29.0f, 3));
}
