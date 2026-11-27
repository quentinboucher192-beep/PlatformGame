// 2.95 — StartPos on the SOB path (game/ghost/SobPath.h, docs/GHOST_RACING.md § « Startpos sur le
// tracé de la SOB »): the path is the trace of the replays that hold the segments' bests; a
// StartPos lies on it (refused where no best replay passes, refused twice in one place), is
// anchored at use to the replay that holds its segment's best NOW (its nearest sample), and an
// attempt from it is that replay played up to there: the replay's own state at that step (its
// digest), recorded so that the attempt's own replay plays back exactly.

#include <cmath>
#include <system_error>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/PersonalBests.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/ghost/SobPath.h"
#include "game/ghost/StartPosStore.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/player/Player.h"
#include "tests/TestFramework.h"

using namespace cosmic;

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

struct TempDir {
    fs::path dir;
    TempDir() {
        dir = fs::temp_directory_path() / ("cosmic_sob_path_" + makeUuidV4().substr(0, 8));
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

/// Flat level: checkpoints at 800 and 1500.
LevelData flatLevel() {
    LevelData l = LevelData::makeEmpty("test_sob_path", "Test SOB path", "sylvanox");
    l.intro.steps.clear();
    for (float x : {800.0f, 1500.0f}) {
        LevelObject c;
        c.id = x < 1000.0f ? "cp1" : "cp2";
        c.type = "checkpoint";
        c.layer = "gameplay";
        c.rect = {x, 736, 32, 64};
        l.objects.push_back(c);
    }
    l.startPowers = {"dash"};
    return l;
}

std::unique_ptr<GameWorld> loadWorld(const LevelData& level) {
    // As a run sets it up (GameScene): its keys, the level, its seed.
    auto w = std::make_unique<GameWorld>(fx().services(), WorldMode::Play);
    w->setKeysPerSlot(true);
    w->load(level);
    w->seedGameplay(levelSeed(level.id));
    return w;
}

/// Running right with a jump now and then.
std::vector<InputFrame> runRight(int ticks, int jumpEvery = 4) {
    std::vector<InputFrame> out;
    InputFrame prev;
    for (int i = 0; i < ticks; ++i) {
        InputFrame f;
        f.held = 1u << static_cast<int>(Action::MoveRight);
        f.moveX = 127;
        if ((i / 40) % jumpEvery == 1) f.held |= 1u << static_cast<int>(Action::Jump);
        f.pressed = f.held & ~prev.held;
        f.released = prev.held & ~f.held;
        prev = f;
        out.push_back(f);
    }
    return out;
}

struct Recorded {
    ReplayData replay;
    std::vector<std::pair<std::string, float>> checkpoints;  // lit, with their split
};

/// Plays and records a run (from the level's start, or as `setup` starts it).
Recorded record(const LevelData& level, const std::vector<InputFrame>& inputs, const SectionTrainingStart* setup = nullptr) {
    Recorded out;
    auto w = loadWorld(level);
    if (setup) applySectionTraining(*w, *setup);
    ReplayRecorder rec;
    rec.begin(level.id, fingerprintGameplay(fx().content, level), makeUuidV4(), w->gameplaySeed());
    size_t splits = w->timer().splits().size();
    for (InputFrame f : inputs) {
        if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
        rec.recordInput(f);
        stepWorld(*w, f, kSimDt);
        rec.tick(*w);
        while (splits < w->timer().splits().size()) out.checkpoints.push_back({w->checkpointId(), w->timer().splits()[splits++]});
    }
    rec.finishAttempt(*w, w->timer().elapsed());
    out.replay = rec.take();
    return out;
}

std::shared_ptr<ReplayData> decodeFile(const fs::path& file) {
    auto bytes = FileUtil::readBinary(file);
    if (!bytes) return nullptr;
    auto d = ReplayData::decode(*bytes);
    return d ? std::make_shared<ReplayData>(std::move(*d)) : nullptr;
}

}  // namespace

TEST_CASE("startpos on the SOB path: on the trace of the segments' best replays, refused off it or twice in one place, anchored to the best now") {
    const LevelData level = flatLevel();
    const Recorded run = record(level, runRight(120 * 9));
    CHECK(run.replay.exact());
    CHECK(run.checkpoints.size() == 2);
    if (run.checkpoints.size() != 2) return;
    TempDir t;
    PersonalBests b(t.dir / "bests.json", t.dir / "bests");
    b.load();
    const std::string key = GhostStore::levelKey(level.id, fingerprintGameplay(fx().content, level));
    b.setRoute(key, {"cp1", "cp2"});
    // No best yet: no path, nowhere to place a StartPos.
    {
        const SobPath none = buildSobPath(sobPathRequest(b, key));
        CHECK(none.ready && none.empty());
        CHECK(!none.nearest({400.0f, 800.0f}, kSobPathSnap));
    }
    // The run (from the level's start, stopped after cp2) holds start → cp1 and cp1 → cp2 with its replay.
    const std::vector<uint8_t> bytes = run.replay.encode();
    BestAttempt a;
    a.passage.checkpoints = run.checkpoints;
    a.source = BestSource::Run;
    a.date = "2026-11-27 10:00";
    a.replay = &bytes;
    b.add(key, a);
    const SobPath path = buildSobPath(sobPathRequest(b, key));
    CHECK(path.segments.size() == 2);
    CHECK(path.missing.size() == 1);  // cp2 → exit: never crossed whole
    const SobPathSegment* seg = path.segment({"cp1", "cp2"});
    CHECK(seg != nullptr);
    if (!seg) return;
    CHECK(!seg->points.empty());
    CHECK(seg->times.front() + 1e-3f >= run.checkpoints[0].second);
    CHECK(seg->times.back() - 1e-3f <= run.checkpoints[1].second);
    // A place a little above the trace, between cp1 and cp2: the path's nearest point, in that segment.
    const size_t mid = seg->points.size() / 2;
    const auto pt = path.nearest(seg->points[mid] + Vec2{0.0f, -10.0f}, kSobPathSnap);
    CHECK(pt.has_value());
    if (!pt) return;
    CHECK(path.segments[static_cast<size_t>(pt->segment)].range == (PersonalBests::Range{"cp1", "cp2"}));
    CHECK((pt->at - seg->points[mid]).length() < 12.0f);
    // Along the path, the next point and back.
    const auto next = path.step(*pt, 1);
    CHECK(next && path.step(*next, -1) && (path.step(*next, -1)->at - pt->at).length() < 1e-3f);
    // Far from it (300 px above), or where no best passes (past cp2): refused.
    CHECK(!path.nearest(seg->points[mid] + Vec2{0.0f, -300.0f}, kSobPathSnap));
    CHECK(!path.nearest({2150.0f, 790.0f}, kSobPathSnap));
    // Kept as its segment and its place; another one at that place (a few pixels) refused.
    StartPosStore store(t.dir / "startpos.json");
    const int id = store.addOnPath(key, pt->at, "cp1", "cp2", -1.0f, "2026-11-27 10:05", false, kSobPathSame);
    CHECK(id > 0);
    CHECK(store.addOnPath(key, pt->at + Vec2{2.0f, 0.0f}, "cp1", "cp2", -1.0f, "2026-11-27 10:06", false, kSobPathSame) == 0);
    CHECK(store.occupied(key, pt->at + Vec2{3.0f, -2.0f}, kSobPathSame) == id);
    CHECK(store.list(key).size() == 1);
    CHECK(store.save());
    {
        StartPosStore disk(t.dir / "startpos.json");
        disk.load();
        const StartPosEntry* e = disk.find(key, id);
        CHECK(e && e->onPath && e->from == "cp1" && e->to == "cp2" && (e->state.feet - pt->at).length() < 1e-3f);
    }
    // At use: the sample of the replay holding cp1 → cp2 nearest to it (itself).
    const PersonalBests::Record* rec = b.record(key, "cp1", "cp2");
    CHECK(rec && rec->replay.has());
    auto rd = decodeFile(b.replayPath(key, rec->replay.file));
    CHECK(rd != nullptr);
    if (!rd) return;
    const auto anchor = sobAnchor(*rd, rec->replay, pt->at);
    CHECK(anchor.has_value());
    if (!anchor) return;
    CHECK((anchor->at - pt->at).length() < 1e-3f);
    CHECK(anchor->chrono >= rec->replay.start - 1e-3f && anchor->chrono <= rec->replay.end + 1e-3f);
    // The SOB improves cp1 → cp2 with another replay (its trace 40 px to the right): the path and
    // the anchor follow the best now.
    ReplayData shifted = run.replay;
    for (ReplayFrame& f : shifted.frames) f.x += 40.0f;
    const std::vector<uint8_t> bytes2 = shifted.encode();
    BestAttempt better;
    better.passage.checkpoints = {run.checkpoints[0], {"cp2", run.checkpoints[1].second - 0.5f}};
    better.source = BestSource::Run;
    better.date = "2026-11-27 11:00";
    better.replay = &bytes2;
    b.add(key, better);
    rec = b.record(key, "cp1", "cp2");
    CHECK(rec && rec->replay.has() && std::abs(rec->replay.end - (run.checkpoints[1].second - 0.5f)) < 1e-4f);
    const SobPath path2 = buildSobPath(sobPathRequest(b, key));
    CHECK(path2.signature != path.signature);
    const SobPathSegment* seg2 = path2.segment({"cp1", "cp2"});
    CHECK(seg2 && std::abs(seg2->points.front().x - (seg->points.front().x + 40.0f)) < 1e-3f);
    auto rd2 = decodeFile(b.replayPath(key, rec->replay.file));
    CHECK(rd2 != nullptr);
    if (rd2) {
        const auto anchor2 = sobAnchor(*rd2, rec->replay, pt->at);
        CHECK(anchor2 && anchor2->chrono < anchor->chrono);  // the same place reached earlier in the shifted trace
        CHECK(anchor2 && (anchor2->at - pt->at).length() <= kSobPathSnap);
    }
    // A better time without a replay: no path there any more, the StartPos « à replacer ».
    BestAttempt bare = better;
    bare.passage.checkpoints = {run.checkpoints[0], {"cp2", run.checkpoints[1].second - 1.0f}};
    bare.replay = nullptr;
    bare.date = "2026-11-27 12:00";
    b.add(key, bare);
    CHECK(!b.record(key, "cp1", "cp2")->replay.has());
    const SobPath path3 = buildSobPath(sobPathRequest(b, key));
    CHECK(path3.segment({"cp1", "cp2"}) == nullptr);
    CHECK(!path3.nearest(seg->points[mid], kSobPathSnap) || path3.segments[static_cast<size_t>(path3.nearest(seg->points[mid], kSobPathSnap)->segment)].range !=
                                                                (PersonalBests::Range{"cp1", "cp2"}));
    // A StartPos of before (a state taken in a run): placed on the path when it lies near it, else « à replacer ».
    StartPosState near;
    near.feet = path.segments.front().points[path.segments.front().points.size() / 2] + Vec2{0.0f, -20.0f};
    near.health = 3;
    StartPosState far = near;
    far.feet.y -= 300.0f;
    const int oldNear = store.add(key, near, -1.0f, "2026-11-20 10:00");
    const int oldFar = store.add(key, far, -1.0f, "2026-11-20 10:01");
    CHECK(reanchorStartPos(store, key, path) == 1);
    CHECK(store.find(key, oldNear)->onPath && store.find(key, oldNear)->to == "cp1");
    CHECK((store.find(key, oldNear)->state.feet - near.feet).length() <= kSobPathSnap);
    CHECK(!store.find(key, oldFar)->onPath);
}

TEST_CASE("startpos on the SOB path: an attempt from it starts in the replay's own state there (its digest), and its own replay plays back exactly") {
    const LevelData level = flatLevel();
    // From the level's start, then from a checkpoint (a training's replay, with its setup).
    SectionTrainingStart fromCp1;
    fromCp1.checkpointId = "cp1";
    fromCp1.hasSpawn = true;
    fromCp1.spawn = {816.0f, 800.0f};
    const SectionTrainingStart* setups[] = {nullptr, &fromCp1};
    for (const SectionTrainingStart* setup : setups) {
        const Recorded run = record(level, runRight(120 * 8, 3), setup);
        CHECK(run.replay.exact());
        auto rd = std::make_shared<ReplayData>(run.replay);
        BestReplay best;
        best.start = 0.0f;
        best.end = static_cast<float>(rd->durationMs) / 1000.0f;
        // A place along its trace: about 3 s in.
        const size_t k = std::min<size_t>(rd->frames.size() - 1, static_cast<size_t>(rd->sampleHz) * 3);
        const Vec2 place{rd->frames[k].x, rd->frames[k].y};
        const auto anchor = sobAnchor(*rd, best, place);
        CHECK(anchor.has_value());
        if (!anchor) continue;
        CHECK(anchor->sample == static_cast<int>(k));
        // The attempt's world, as a run sets it up; the replay played up to there, recorded.
        auto w = loadWorld(level);
        std::function<void(GameWorld&)> apply;
        if (setup) apply = [s = *setup](GameWorld& gw) { applySectionTraining(gw, s); };
        w->silent = true;
        SobPreroll pre(rd, *w, apply, anchor->chrono);
        ReplayRecorder rec;
        rec.begin(level.id, rd->levelHash, makeUuidV4(), w->gameplaySeed());
        int frames = 0;
        while (!pre.advance(50, [&](const InputFrame& f) {
            rec.recordInput(f);
            rec.tick(*w);
        }))
            ++frames;
        CHECK(pre.arrived());
        CHECK(!pre.failed());
        CHECK(frames > 3);  // a budget per call: never the whole replay at once
        // Nova where the replay's sample is; the world's digest the replay's own at that step.
        CHECK((w->player().feet() - place).length() < 1e-3f);
        ReplayPlayback p(fx().services(), level, *rd, apply ? ReplayPlayback::Setup(apply) : ReplayPlayback::Setup{});
        p.seekTick(pre.tick());
        p.finishSeek();
        CHECK(p.tick() == pre.tick());
        CHECK(simulationStateHash(p.world()) == simulationStateHash(*w));
        CHECK(p.world().player().health == w->player().health);
        CHECK(std::abs(p.world().player().energy - w->player().energy) < 1e-4f);
        // Then the player plays on (other inputs); the attempt's replay, with the same start, plays
        // back exactly: the run before it, then the player's.
        w->silent = false;
        for (InputFrame f : runRight(240, 2)) {
            rec.recordInput(f);
            stepWorld(*w, f, kSimDt);
            rec.tick(*w);
        }
        rec.finishAttempt(*w, w->timer().elapsed());
        const ReplayData mine = rec.take();
        CHECK(mine.exact());
        ReplayPlayback again(fx().services(), level, mine, apply ? ReplayPlayback::Setup(apply) : ReplayPlayback::Setup{});
        while (again.step()) {
        }
        CHECK(again.finished() && !again.desynced());
        CHECK(simulationStateHash(again.world()) == simulationStateHash(*w));
    }
}
