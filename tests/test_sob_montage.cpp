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
#include "game/ghost/SegmentHistory.h"
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

TEST_CASE("sum of best montage: a gold segment's replay is kept, replaced, then deleted once no gold refers to it") {
    TempDir t;
    const fs::path file = t.path / "segments.json", golds = t.path / "golds";
    const std::string key = GhostStore::levelKey("sylvanox_02", std::string(64, 'a'));
    const std::vector<std::string> cps{"cp1", "cp2"};
    SegmentHistory h(file, golds);
    const auto a = bytesOf(1), b = bytesOf(2), c = bytesOf(3);
    // The first run holds every gold.
    h.add(key, {10.0f, 20.0f}, 30.0f, "2026-11-01 10:00", cps, &a);
    auto s = h.goldSources(key);
    CHECK(s.size() == 3);
    CHECK(s[0].has() && s[0].replay == s[1].replay && s[1].replay == s[2].replay);
    CHECK_NEAR(s[1].start, 10.0f, 1e-5);
    CHECK_NEAR(s[1].end, 20.0f, 1e-5);
    CHECK_NEAR(s[2].end, 30.0f, 1e-5);
    CHECK(s[0].date == "2026-11-01 10:00");
    const std::string first = s[0].replay;
    CHECK(isFile(h.replayPath(key, first)));
    // A second run beats the first segment only: its replay holds that one.
    h.add(key, {9.0f, 21.0f}, 32.0f, "2026-11-02 10:00", cps, &b);
    s = h.goldSources(key);
    CHECK(s[0].replay != first && s[1].replay == first && s[2].replay == first);
    CHECK_NEAR(s[0].end, 9.0f, 1e-5);
    const std::string second = s[0].replay;
    CHECK(h.save());
    CHECK(isFile(h.replayPath(key, first)) && isFile(h.replayPath(key, second)));
    // A third run beats every segment: the two earlier replays go once saved.
    h.add(key, {8.0f, 16.0f}, 24.0f, "2026-11-03 10:00", cps, &c);
    s = h.goldSources(key);
    const std::string third = s[0].replay;
    CHECK(s[1].replay == third && s[2].replay == third);
    CHECK(isFile(h.replayPath(key, first)));  // nothing deleted before the history is written
    CHECK(h.save());
    CHECK(!isFile(h.replayPath(key, first)) && !isFile(h.replayPath(key, second)));
    CHECK(isFile(h.replayPath(key, third)));
    CHECK(filesIn(golds) == 1);
    // A gold beaten by a run without a replay: that segment has none any more (never the old one).
    h.add(key, {7.0f, 16.0f}, 24.0f, "2026-11-04 10:00", cps);
    s = h.goldSources(key);
    CHECK(!s[0].has() && s[1].replay == third && s[2].replay == third);
    CHECK(h.save());
    CHECK(isFile(h.replayPath(key, third)));
    // Read back as it was; a hand-made path is never a replay.
    SegmentHistory back(file, golds);
    back.load();
    auto r = back.goldSources(key);
    CHECK(r.size() == 3 && !r[0].has() && r[1].replay == third);
    CHECK_NEAR(r[2].start, 16.0f, 1e-5);
    CHECK(back.replayPath(key, "../segments.json").empty());
    CHECK(back.replayPath(key, "x/y.cprz").empty());
    // A source that does not describe its segment's time (file edited) is dropped.
    FileUtil::writeTextAtomic(file, R"({"format": "cosmic.segments", "version": 1, "levels": {")" + key +
                                        R"(": {"touched": "t", "best": [5, 6], "best_split_count": 1,
        "best_sources": [{"replay": "a-1.cprz", "date": "d", "start": 0, "end": 5}, {"replay": "../b.cprz", "date": "d", "start": 5, "end": 11}],
        "runs": []}}})");
    SegmentHistory edited(file, golds);
    edited.load();
    auto e = edited.goldSources(key);
    CHECK(e.size() == 2 && e[0].has() && !e[1].has());
    // Without a folder for them (a history given a file only), no replay is ever kept.
    SegmentHistory none(t.path / "other.json");
    none.add(key, {10.0f}, 20.0f, "2026-11-01 10:00", {"cp1"}, &a);
    CHECK(none.goldSources(key).size() == 2 && !none.goldSources(key)[0].has());
}

TEST_CASE("sum of best montage: each level version keeps its own gold replays") {
    TempDir t;
    SegmentHistory h(t.path / "segments.json", t.path / "golds");
    const std::string v1 = GhostStore::levelKey("noyau_01", std::string(64, 'a'));
    const std::string v2 = GhostStore::levelKey("noyau_01", std::string(64, 'b'));
    CHECK(v1 != v2);
    const auto a = bytesOf(1), b = bytesOf(2);
    h.add(v1, {10.0f}, 20.0f, "2026-11-01 10:00", {"cp1"}, &a);
    h.add(v2, {12.0f}, 25.0f, "2026-11-01 11:00", {"cp1"}, &b);
    auto s1 = h.goldSources(v1), s2 = h.goldSources(v2);
    CHECK(s1[0].has() && s2[0].has());
    const fs::path p1 = h.replayPath(v1, s1[0].replay), p2 = h.replayPath(v2, s2[0].replay);
    CHECK(p1.parent_path() != p2.parent_path());
    CHECK(FileUtil::readBinary(p1) == a);
    CHECK(FileUtil::readBinary(p2) == b);
    // A run of version 2 never changes version 1's golds nor their replays.
    const auto c = bytesOf(3);
    h.add(v2, {5.0f}, 9.0f, "2026-11-02 10:00", {"cp1"}, &c);
    CHECK(h.goldSources(v1)[0].replay == s1[0].replay);
    CHECK_NEAR(h.goldSegments(v1)[0], 10.0f, 1e-5);
    CHECK(h.save());
    CHECK(isFile(p1) && !isFile(p2));
}

TEST_CASE("sum of best montage: an old gold takes the replay of a kept run that did exactly its time") {
    TempDir t;
    SegmentHistory h(t.path / "segments.json", t.path / "golds");
    const std::string key = GhostStore::levelKey("sylvanox_02", std::string(64, 'c'));
    h.add(key, {10.0f, 20.0f}, 30.0f, "2026-10-01 10:00", {"cp1", "cp2"});  // before 2.60.3: no replay
    CHECK(!h.goldSources(key)[1].has());
    const fs::path run = t.path / "pb.cprz";
    FileUtil::writeBinary(run, bytesOf(7));
    // Not the gold's time: refused; the gold's time: kept (a copy), never twice.
    CHECK(!h.adoptReplay(key, run, "2026-10-01 10:00", {{1, 10.0f, 21.0f}}));
    CHECK(h.adoptReplay(key, run, "2026-10-01 10:00", {{1, 10.0f, 20.0f}, {2, 20.0f, 30.0f}}));
    auto s = h.goldSources(key);
    CHECK(!s[0].has() && s[1].has() && s[1].replay == s[2].replay);
    CHECK(!h.adoptReplay(key, run, "2026-10-01 10:00", {{1, 10.0f, 20.0f}}));
    CHECK(!h.adoptReplay("other", run, "d", {{0, 0.0f, 10.0f}}));
    std::error_code ec;
    fs::remove(run, ec);  // the record replaced later: the gold keeps its own copy
    CHECK(isFile(h.replayPath(key, s[1].replay)));
}

TEST_CASE("sum of best montage: a training best lowers the Sum of Best, never the record nor the full runs' best segments") {
    const std::vector<std::string> route{"a", "b"};
    SegmentReferences::RunInput pb{{10.0f, 21.0f}, 32.0f, {"a", "b"}};
    const std::vector<float> gold{10.0f, 10.0f, 10.0f};
    SegmentReferences courses = buildSegmentReferences(gold, {"a", "b"}, {}, pb, route);
    CHECK_NEAR(courses.sob, 30.0f, 1e-4);
    TrainingBests training{{{"a", "b"}, 7.5f}, {{"", "a"}, 12.0f}};
    SegmentReferences all = buildSegmentReferences(gold, {"a", "b"}, {}, pb, route, training);
    CHECK_NEAR(all.sob, 27.5f, 1e-4);         // 10 + 7.5 + 10: the faster training counts, the slower one not
    CHECK_NEAR(all.pbTime, 32.0f, 1e-4);      // the record is the full run's
    CHECK_NEAR(all.bestOf("a", "b"), 10.0f, 1e-4);  // the full runs' best segment stays theirs
    CHECK_NEAR(all.pbOf("a", "b"), 11.0f, 1e-4);
    CHECK_NEAR(all.possibleGain(), 4.5f, 1e-4);
    // A tie to the millisecond keeps the full run's.
    std::vector<bool> fromTraining;
    auto tie = withTrainingBests(gold, route, {{{"a", "b"}, 10.0002f}}, &fromTraining);
    CHECK(!fromTraining[1]);
    CHECK_NEAR(tie[1], 10.0f, 1e-5);
    // Another level version's trainings are another map: never mixed in.
    CHECK_NEAR(buildSegmentReferences(gold, {"a", "b"}, {}, pb, route, {}).sob, 30.0f, 1e-4);
    // No full run yet: the route's segments from the trainings only, all of them or nothing.
    TrainingBests every{{{"", "a"}, 9.0f}, {{"a", "b"}, 8.0f}, {{"b", ""}, 7.0f}};
    CHECK_NEAR(buildSegmentReferences({}, {}, {}, {}, route, every).sob, 24.0f, 1e-4);
    every.erase({"b", ""});
    CHECK(buildSegmentReferences({}, {}, {}, {}, route, every).sob < 0.0f);
}

TEST_CASE("sum of best montage: the montage follows the Sum of Best and says where each segment was done") {
    const std::vector<std::string> route{"a", "b"};
    const std::vector<float> gold{10.0f, 10.0f, 10.0f};
    std::vector<SegmentHistory::GoldSource> sources(3);
    sources[0] = {"/golds/run-1.cprz", "2026-11-12 18:00", 0.0f, 10.0f};
    MontageRun pb{"/local/pb.cprz", "2026-11-10 09:00", {10.0f, 21.0f}, 32.0f, {"a", "b"}};
    MontageRun last{"/local/last.cprz", "2026-11-11 09:00", {12.0f, 22.0f}, 32.0f, {}};
    MontageTrainings trainings;
    SectionTrainingStart start;
    start.checkpointId = "a";
    start.hasSpawn = true;
    start.spawn = {816.0f, 800.0f};
    trainings[{"a", "b"}] = {7.5f, "2026-11-17 20:00", "/training/t-1.cprz", start};
    trainings[{"b", ""}] = {11.0f, "2026-11-17 20:05", "/training/t-2.cprz", start};  // slower than the gold: not used
    MontagePlan p = buildMontagePlan(gold, route, sources, pb, {last}, route, trainings);
    TrainingBests bests{{{"a", "b"}, 7.5f}, {{"b", ""}, 11.0f}};
    SegmentReferences refs = buildSegmentReferences(gold, route, {}, {pb.splits, pb.time, pb.checkpoints}, route, bests);
    CHECK_NEAR(p.total, refs.sob, 1e-5);  // the same Sum of Best as the training screen
    CHECK_NEAR(p.total, 27.5f, 1e-4);
    CHECK(p.segments.size() == 3);
    CHECK(p.withReplay == 3);
    // Segment 1: a full run's gold, with its own replay.
    CHECK(!p.segments[0].training && p.segments[0].file == "/golds/run-1.cprz" && !p.segments[0].adopt);
    CHECK(p.segments[0].from.empty() && p.segments[0].to == "a");
    // Segment 2: the section training did better — its attempt, from its start, with its setup.
    const MontageSegment& s1 = p.segments[1];
    CHECK(s1.training && s1.file == "/training/t-1.cprz" && s1.date == "2026-11-17 20:00");
    CHECK_NEAR(s1.start, 0.0f, 1e-6);
    CHECK_NEAR(s1.end, 7.5f, 1e-6);
    CHECK(s1.trainingStart.checkpointId == "a" && s1.trainingStart.hasSpawn);
    CHECK(s1.from == "a" && s1.to == "b");
    // Segment 3: a gold of before 2.60.3 the last run did in exactly that time (to be adopted).
    const MontageSegment& s2 = p.segments[2];
    CHECK(!s2.training && s2.adopt && s2.file == "/local/last.cprz");
    CHECK_NEAR(s2.start, 22.0f, 1e-6);
    CHECK_NEAR(s2.end, 32.0f, 1e-6);
    CHECK_NEAR(p.possibleGain(), 4.5f, 1e-4);
    // Without that run: said to have no replay (never another run's time instead).
    MontagePlan q = buildMontagePlan(gold, route, sources, pb, {}, route, trainings);
    CHECK(q.withReplay == 2 && !q.segments[2].hasReplay() && !q.segments[2].training);
    CHECK_NEAR(q.total, 27.5f, 1e-4);
    // A training best of before 2.60.3 (no replay): its time counts, the montage says it has no replay.
    trainings[{"a", "b"}].file.clear();
    MontagePlan r = buildMontagePlan(gold, route, sources, pb, {last}, route, trainings);
    CHECK(r.segments[1].training && !r.segments[1].hasReplay());
    CHECK_NEAR(r.total, 27.5f, 1e-4);
    // Only trainings (no full run): every segment from them, the total their sum.
    MontageTrainings only;
    only[{"", "a"}] = {9.0f, "d", "/t/0.cprz", {}};
    only[{"a", "b"}] = {8.0f, "d", "/t/1.cprz", start};
    only[{"b", ""}] = {7.0f, "d", "", {}};
    MontagePlan o = buildMontagePlan({}, {}, {}, {}, {}, route, only);
    CHECK(o.segments.size() == 3 && o.withReplay == 2);
    CHECK(o.segments[0].training && o.segments[2].training && !o.segments[2].hasReplay());
    CHECK_NEAR(o.total, 24.0f, 1e-4);
    CHECK(o.pbTime < 0.0f && o.possibleGain() < 0.0f);
}

TEST_CASE("sum of best montage: the best training attempt keeps its replay and its start, a slower one never replaces it") {
    TempDir t;
    const fs::path file = t.path / "training.json", dir = t.path / "training";
    const std::string key = GhostStore::levelKey("sylvanox_02", std::string(64, 'd'));
    TrainingRecords tr(file, dir);
    SectionTrainingStart start;
    start.checkpointId = "cp1";
    start.hasSpawn = true;
    start.spawn = {816.0f, 800.0f};
    start.powers = {"dash"};
    start.powersChosen = true;
    const auto a = bytesOf(1), b = bytesOf(2), c = bytesOf(3);
    CHECK(tr.add(key, "cp1", "cp2", 8.0f, "2026-11-17 20:00", &a, &start));
    const std::string first = tr.get(key, "cp1", "cp2")->replay;
    CHECK(!first.empty() && isFile(tr.replayPath(key, first)));
    CHECK(!tr.add(key, "cp1", "cp2", 9.0f, "2026-11-17 20:01", &b, &start));
    CHECK(tr.get(key, "cp1", "cp2")->replay == first);
    CHECK(tr.add(key, "cp1", "cp2", 7.0f, "2026-11-17 20:02", &c, &start));
    const std::string second = tr.get(key, "cp1", "cp2")->replay;
    CHECK(second != first);
    CHECK(tr.save());
    CHECK(!isFile(tr.replayPath(key, first)) && isFile(tr.replayPath(key, second)));
    // A start that is not this segment's: no replay kept with that best.
    SectionTrainingStart other = start;
    other.checkpointId = "cp2";
    CHECK(tr.add(key, "cp1", "cp2", 6.0f, "2026-11-17 20:03", &a, &other));
    CHECK(tr.get(key, "cp1", "cp2")->replay.empty());
    CHECK(tr.add(key, "", "cp1", 5.0f, "2026-11-17 20:04", &b, &other));
    CHECK(tr.get(key, "", "cp1")->replay.empty());
    SectionTrainingStart fromStart;
    CHECK(tr.add(key, "", "cp1", 4.0f, "2026-11-17 20:05", &b, &fromStart));
    CHECK(!tr.get(key, "", "cp1")->replay.empty());
    CHECK(tr.add(key, "cp1", "cp2", 5.5f, "2026-11-17 20:06", &c, &start));
    CHECK(tr.save());
    // Read back: the replay and the start as they were.
    TrainingRecords back(file, dir);
    back.load();
    const TrainingRecords::Entry* e = back.get(key, "cp1", "cp2");
    CHECK(e && !e->replay.empty() && e->start.checkpointId == "cp1" && e->start.hasSpawn && e->start.powersChosen);
    CHECK(e && e->start.powers == std::vector<std::string>({"dash"}));
    CHECK(e && std::abs(e->start.spawn.x - 816.0f) < 1e-3f && std::abs(e->start.spawn.y - 800.0f) < 1e-3f);
    TrainingBests bests = back.bests(key);
    CHECK(bests.size() == 2);
    CHECK_NEAR(bests[std::make_pair(std::string("cp1"), std::string("cp2"))], 5.5f, 1e-5);
    CHECK(back.bests("other").empty());
    CHECK(filesIn(dir) == 2);
    // A damaged start in the file: the best stays, without its replay.
    auto j = FileUtil::loadJson(file);
    CHECK(j.has_value());
    for (auto& s : (*j)["levels"][key]["segments"]) s["start"]["spawn"] = "ici";
    FileUtil::writeTextAtomic(file, j->dump());
    TrainingRecords damaged(file, dir);
    damaged.load();
    CHECK(damaged.get(key, "cp1", "cp2") && damaged.get(key, "cp1", "cp2")->best > 0.0f && damaged.get(key, "cp1", "cp2")->replay.empty());
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
