// Ghost Racing (2.18): ghosts built from real recorded runs of a test level (two
// checkpoints, a pit), compared with the live run: faster and slower ghosts, several ghosts,
// checkpoints, deaths and respawns, powers, other level versions, damaged or hand-made
// replays, interrupted downloads, the local store and the segment history.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/Ghost.h"
#include "game/ghost/GhostRace.h"
#include "game/ghost/GhostStore.h"
#include "game/ghost/SegmentHistory.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Flat test level: start at x 160, checkpoints at 800 and 1500, exit at 2200; with `pit` a
/// hole from 1700 to 1736 (a death if not jumped).
LevelData ghostLevel(bool pit) {
    LevelData l = LevelData::makeEmpty("test_ghost", "Test fantôme", "sylvanox");
    l.intro.steps.clear();
    for (float x : {800.0f, 1500.0f}) {
        LevelObject c;
        c.id = x < 1000.0f ? "cp1" : "cp2";
        c.type = "checkpoint";
        c.layer = "gameplay";
        c.rect = {x, 736, 32, 64};
        l.objects.push_back(c);
    }
    if (pit) {
        for (auto& o : l.objects)
            if (o.id == "o1") o.rect = {0, 800, 1700, 160};
        LevelObject far;
        far.id = "o9";
        far.type = "solid";
        far.layer = "terrain";
        far.rect = {1736, 800, 664, 160};
        far.props = {{"material", "ruin_stone"}};
        l.objects.push_back(far);
    }
    return l;
}

struct Bot {
    float speed = 1.0f;     // stick to the right
    int pitFalls = 0;       // falls into the pit before jumping it
    bool shieldAt1000 = false;
};

struct Run {
    ReplayData replay;
    std::vector<float> splits;
    float time = 0.0f;
    bool completed = false;
    int deaths = 0;
};

struct GhostWorld {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    GhostWorld() {
        content.reload();
        res.init(nullptr, &audio);
    }

    Run run(const LevelData& level, const Bot& bot) {
        WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
        GameWorld w(s, WorldMode::Play);
        w.load(level);
        w.seedGameplay(7);
        if (bot.shieldAt1000)
            if (const PowerDefinition* d = content.power("shield")) w.player().grantPower(*d);
        ReplayRecorder rec;
        rec.begin(level.id, fingerprintGameplay(content, level), makeUuidV4(), w.gameplaySeed());
        w.eventObserver = [&rec](const std::string& src, const std::string& trig) { rec.onWorldEvent(src, trig); };
        int jumpHold = 0;
        bool shieldDone = false;
        Run out;
        for (int i = 0; i < 120 * 90 && w.state() != WorldState::Completed && w.state() != WorldState::Outro; ++i) {
            PlayerInput in;
            in.move = {bot.speed, 0.0f};
            if (bot.speed > 0.5f) in.held[static_cast<int>(Action::MoveRight)] = true;
            Vec2 feet = w.player().feet();
            bool jumpNow = w.stats().deaths >= bot.pitFalls && feet.x > 1672.0f && feet.x < 1697.0f && w.player().grounded();
            if (jumpNow && jumpHold == 0) {
                in.pressed[static_cast<int>(Action::Jump)] = true;
                jumpHold = 40;
            }
            if (jumpHold > 0) {
                in.held[static_cast<int>(Action::Jump)] = true;
                --jumpHold;
            }
            if (bot.shieldAt1000 && !shieldDone && feet.x > 1000.0f) {
                in.pressed[static_cast<int>(Action::Shield)] = true;
                in.held[static_cast<int>(Action::Shield)] = true;
                shieldDone = true;
            }
            if (std::getenv("GHOST_TRACE") && feet.x > 1600.0f && i % 6 == 0)
                std::printf("    %d x %.0f y %.0f g %d st %d d %d jump %d\n", i, feet.x, feet.y, w.player().grounded(), static_cast<int>(w.player().state()), w.stats().deaths, in.pressed[static_cast<int>(Action::Jump)] ? 1 : 0);
            InputFrame f = InputFrame::fromPlayerInput(in);
            rec.recordInput(f);
            stepWorld(w, f, kSimDt);
            rec.tick(w);
        }
        out.completed = w.state() == WorldState::Completed || w.state() == WorldState::Outro;
        if (std::getenv("GHOST_DEBUG"))
            std::printf("  bot: state %d deaths %d feet %.0f,%.0f t %.2f\n", static_cast<int>(w.state()), w.stats().deaths, w.player().feet().x, w.player().feet().y, w.timer().elapsed());
        out.time = w.timer().elapsed();
        out.splits = w.timer().splits();
        out.deaths = w.stats().deaths;
        if (out.completed) rec.finish(w, out.time);
        else rec.finishAttempt(w, out.time);
        out.replay = rec.take();
        return out;
    }
};

GhostWorld& world() {
    static GhostWorld g;
    return g;
}

Rect bounds() { return ghostLevel(false).bounds; }

}  // namespace

TEST_CASE("ghost: a recorded run becomes a ghost with exact checkpoint splits") {
    LevelData level = ghostLevel(false);
    Run r = world().run(level, {});
    CHECK(r.completed);
    CHECK(r.splits.size() == 2);
    GhostCheck c = checkGhostReplay(r.replay, r.replay.levelHash, level.bounds);
    CHECK(c.ok);
    if (!c.ok) std::printf("  refus : %s\n", c.reason.c_str());
    GhostTrack t;
    t.load(r.replay);
    CHECK(t.splitsExact());
    CHECK(t.splits().size() == 2);
    // The Split events give the timer's splits to the millisecond.
    for (size_t i = 0; i < t.splits().size() && i < r.splits.size(); ++i) CHECK(std::abs(t.splits()[i] - r.splits[i]) < 0.0015f);
    CHECK(std::abs(t.duration() - r.time) < 0.002f);
    // Through the file format.
    auto back = ReplayData::decode(r.replay.encode());
    CHECK(back.has_value());
    if (back) {
        GhostTrack t2;
        t2.load(*back);
        CHECK(t2.splits() == t.splits());
    }
    // The ghost is where the run was, at the same time (recorded positions, interpolated).
    GhostTrack::Pose start = t.poseAt(0.0f), mid = t.poseAt(r.splits[0]), end = t.poseAt(t.duration());
    CHECK(start.visible);
    CHECK(start.feet.x < 260.0f);
    CHECK(std::abs(mid.feet.x - 816.0f) < 60.0f);
    CHECK(end.feet.x > 2100.0f);
}

TEST_CASE("ghost: faster and slower ghosts give the right gap, live and at checkpoints") {
    LevelData level = ghostLevel(false);
    Run fast = world().run(level, {1.0f});
    Run slow = world().run(level, {0.55f});
    CHECK(fast.completed);
    CHECK(slow.completed);
    CHECK(slow.time > fast.time + 1.0f);
    LevelProgressTrack track = buildProgressTrack(level, world().content);
    CHECK(track.valid());

    // The player runs the slow run against the fast ghost: always behind, more and more.
    GhostRace race;
    race.add(GhostMeta{}, fast.replay, true, Color{120, 220, 255, 255});
    race.setRoute(track);
    GhostTrack slowTrack;
    slowTrack.load(slow.replay);
    float lastDelta = -1.0f;
    bool grows = true;
    for (float t = 0.5f; t < slow.time; t += 0.5f) {
        race.update(t, slowTrack.poseAt(t).feet, 0.5f);
        const auto& g = race.racer(0);
        if (!g.hasLiveDelta) continue;
        if (g.liveDelta + 0.05f < lastDelta) grows = false;
        lastDelta = g.liveDelta;
    }
    CHECK(lastDelta > 0.5f);
    CHECK(grows);
    for (size_t i = 0; i < slow.splits.size(); ++i) race.onSplit(static_cast<int>(i), slow.splits[i], slowTrack.poseAt(slow.splits[i]).feet);
    race.finish(slow.time);
    CHECK(race.splits().size() == 3);
    for (size_t i = 0; i < 2 && i < race.splits().size(); ++i) {
        const auto& s = race.splits()[i];
        CHECK(s.comparable);
        CHECK(std::abs(s.delta - (slow.splits[i] - fast.splits[i])) < 0.002f);
        CHECK(s.delta > 0.0f);
        CHECK(s.ghostSegment > 0.0f);
    }
    CHECK(std::abs(race.splits().back().delta - (slow.time - fast.time)) < 0.003f);

    // The other way round: ahead of a slower ghost (negative gap), estimate of the finish.
    GhostRace ahead;
    ahead.add(GhostMeta{}, slow.replay, true, Color{255, 200, 80, 255});
    ahead.setRoute(track);
    GhostTrack fastTrack;
    fastTrack.load(fast.replay);
    ahead.update(fast.splits[1], fastTrack.poseAt(fast.splits[1]).feet, 0.016f);
    CHECK(ahead.racer(0).hasLiveDelta);
    CHECK(ahead.racer(0).liveDelta < -0.3f);
    CHECK(ahead.estimatedRemaining() > 0.0f);
    CHECK(ahead.estimatedFinish() > fast.splits[1]);
    ahead.onSplit(0, fast.splits[0], fastTrack.poseAt(fast.splits[0]).feet);
    CHECK(ahead.splits().back().delta < 0.0f);
}

TEST_CASE("ghost: the gap text says ahead, behind or level") {
    CHECK(formatGhostDelta(2.45f) == "+2,450 s");
    CHECK(formatGhostDelta(-1.23f) == "-1,230 s");
    CHECK(formatGhostDelta(0.0f) == "0,000 s");
    CHECK(formatGhostDelta(0.0004f) == "0,000 s");
    CHECK(formatGhostDelta(75.5f) == "+1:15,500");
    // Segments are durations, not cumulative times.
    auto seg = segmentDurations({10.0f, 25.0f}, 40.0f);
    CHECK(seg.size() == 3);
    CHECK(std::abs(seg[0] - 10.0f) < 1e-4f);
    CHECK(std::abs(seg[1] - 15.0f) < 1e-4f);
    CHECK(std::abs(seg[2] - 15.0f) < 1e-4f);
    CHECK(segmentDurations({10.0f}, 0.0f).size() == 1);
}

TEST_CASE("ghost: deaths and respawns are shown without sliding across the level") {
    LevelData level = ghostLevel(true);
    Run r = world().run(level, {1.0f, 1});
    CHECK(r.completed);
    CHECK(r.deaths == 1);
    GhostCheck c = checkGhostReplay(r.replay, r.replay.levelHash, level.bounds);
    CHECK(c.ok);
    if (!c.ok) std::printf("  refus : %s\n", c.reason.c_str());
    GhostTrack t;
    t.load(r.replay);
    // Between the fall and the respawn at the second checkpoint the ghost never shows a
    // position between the two (no interpolation across the jump back).
    int jumps = 0;
    float maxStep = 0.0f;
    Vec2 prev = t.poseAt(0.0f).feet;
    for (float s = 1.0f / 120.0f; s < t.duration(); s += 1.0f / 120.0f) {
        Vec2 p = t.poseAt(s).feet;
        float step = (p - prev).length();
        if (step > 25.0f) ++jumps;  // the respawn: one instant move back to the checkpoint
        else maxStep = std::max(maxStep, step);
        prev = p;
    }
    CHECK(jumps == 1);
    CHECK(maxStep < 10.0f);  // everything else is smooth
    bool sawDeath = false;
    for (float s = 0.0f; s < t.duration(); s += 1.0f / 30.0f)
        if (static_cast<PlayerState>(t.poseAt(s).state) == PlayerState::Dead) sawDeath = true;
    CHECK(sawDeath);
    // A run with a death against one without: the gap includes the time lost.
    Run clean = world().run(level, {1.0f, 0});
    CHECK(clean.completed);
    CHECK(clean.deaths == 0);
    CHECK(r.time > clean.time + 1.0f);
}

TEST_CASE("ghost: powers in use are part of the ghost (shield)") {
    LevelData level = ghostLevel(false);
    Bot bot;
    bot.shieldAt1000 = true;
    Run r = world().run(level, bot);
    CHECK(r.completed);
    GhostTrack t;
    t.load(r.replay);
    bool shield = false;
    for (float s = 0.0f; s < t.duration(); s += 1.0f / 30.0f)
        if (t.poseAt(s).flags & ReplayFrame::Shield) shield = true;
    CHECK(shield);
    CHECK(r.replay.countEvents(ReplayEventType::Power) >= 1);
}

TEST_CASE("ghost: several ghosts at once, each with its own gap, allocation-free updates") {
    LevelData level = ghostLevel(false);
    Run a = world().run(level, {1.0f});
    Run b = world().run(level, {0.8f});
    Run c = world().run(level, {0.6f});
    LevelProgressTrack track = buildProgressTrack(level, world().content);
    GhostRace race;
    race.add(GhostMeta{}, a.replay, true, Color{});
    race.add(GhostMeta{}, b.replay, true, Color{});
    race.add(GhostMeta{}, c.replay, true, Color{});
    race.setRoute(track);
    CHECK(race.size() == 3);
    GhostTrack me;
    me.load(b.replay);
    float t = b.splits[1];
    race.update(t, me.poseAt(t).feet, 0.016f);
    CHECK(race.racer(0).liveDelta > 0.1f);              // the faster one is ahead
    CHECK(std::abs(race.racer(1).liveDelta) < 0.25f);   // the same run
    CHECK(race.racer(2).liveDelta < -0.1f);             // the slower one is behind
    // The reference ghost can change.
    race.setReference(2);
    CHECK(race.reference() == 2);
    race.setReference(9);
    CHECK(race.reference() == 2);
    // At most kMaxGhosts.
    for (int i = 0; i < GhostRace::kMaxGhosts; ++i) race.add(GhostMeta{}, a.replay, true, Color{});
    CHECK(race.size() == GhostRace::kMaxGhosts);
    // Performance: 8 ghosts updated for 10 minutes of frames at 60 fps.
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 60 * 600; ++i) {
        float s = static_cast<float>(i % 600) / 60.0f;
        race.update(s, me.poseAt(s).feet, 1.0f / 60.0f);
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  8 fantômes, 36 000 images : %.1f ms (%.4f ms par image)\n", ms, ms / 36000.0);
    CHECK(ms / 36000.0 < 0.5);
}

TEST_CASE("ghost: a modified level is not an official comparison, a damaged replay is refused") {
    LevelData level = ghostLevel(false);
    Run r = world().run(level, {});
    // Published then modified (another gameplay hash): refused as this version's ghost.
    LevelData changed = ghostLevel(true);
    std::string otherHash = fingerprintGameplay(world().content, changed);
    CHECK(otherHash != r.replay.levelHash);
    GhostCheck c = checkGhostReplay(r.replay, otherHash, level.bounds);
    CHECK(!c.ok);
    CHECK(c.reason == "Autre version du niveau.");
    // Unfinished run.
    Run partial = r;
    partial.replay.events.erase(std::remove_if(partial.replay.events.begin(), partial.replay.events.end(),
                                               [](const ReplayEvent& e) { return e.type == ReplayEventType::Finish; }),
                                partial.replay.events.end());
    CHECK(!checkGhostReplay(partial.replay, "", level.bounds).ok);
    // Teleporting across the level without a reason.
    ReplayData jump = r.replay;
    for (size_t i = 10; i < 20 && i < jump.frames.size(); ++i) jump.frames[i].x += (i % 2 ? 1500.0f : -1500.0f);
    CHECK(!checkGhostReplay(jump, "", level.bounds).ok);
    // Out of the level, non-finite, wrong duration.
    ReplayData out = r.replay;
    out.frames[5].y = 1e9f;
    CHECK(!checkGhostReplay(out, "", level.bounds).ok);
    ReplayData nan = r.replay;
    nan.frames[5].x = std::nanf("");
    CHECK(!checkGhostReplay(nan, "", level.bounds).ok);
    ReplayData longer = r.replay;
    longer.durationMs *= 3;
    CHECK(!checkGhostReplay(longer, "", level.bounds).ok);
    ReplayData empty;
    CHECK(!checkGhostReplay(empty, "", level.bounds).ok);
    // Splits in the wrong order.
    ReplayData splits = r.replay;
    for (auto& e : splits.events)
        if (e.type == ReplayEventType::Split && e.arg == 1) e.value = 1;
    CHECK(!checkGhostReplay(splits, "", level.bounds).ok);
    CHECK(checkGhostReplay(r.replay, "", level.bounds).ok);
}

TEST_CASE("ghost: damaged files and interrupted downloads never reach the race") {
    fs::path root = fs::temp_directory_path() / ("cosmic_ghost_test_" + makeUuidV4().substr(0, 8));
    GhostStore store(root);
    LevelData level = ghostLevel(false);
    Run r = world().run(level, {});
    std::vector<uint8_t> bytes = r.replay.encode();
    GhostMeta m;
    m.recordId = "rec_1";
    m.levelId = level.id;
    m.levelHash = r.replay.levelHash;
    m.label = "Astrid";
    m.source = "world";
    // Interrupted download: the bytes stop half way.
    std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2));
    std::string err;
    CHECK(!store.storeDownload(cut, m, &err));
    CHECK(!err.empty());
    CHECK(!store.cached("rec_1").has_value());
    // Random bytes, empty, too big, another level's replay under this record.
    CHECK(!store.storeDownload(std::vector<uint8_t>(5000, 0x5A), m));
    CHECK(!store.storeDownload({}, m));
    CHECK(!store.storeDownload(std::vector<uint8_t>(GhostStore::kMaxReplayBytes + 1, 0), m));
    GhostMeta wrong = m;
    wrong.levelHash = std::string(64, 'b');
    CHECK(!store.storeDownload(bytes, wrong));
    // Unsafe record ids are never used as file names.
    GhostMeta evil = m;
    evil.recordId = "../../evil";
    CHECK(store.storeDownload(bytes, evil));  // stored as "evil" inside the cache
    CHECK(!fs::exists(root.parent_path() / "evil.cprz"));
    // The real download.
    CHECK(store.storeDownload(bytes, m));
    auto e = store.cached("rec_1");
    CHECK(e.has_value());
    if (e) {
        CHECK(e->meta.label == "Astrid");
        auto back = GhostStore::loadReplay(e->file);
        CHECK(back.has_value());
    }
    CHECK(store.cachedFor(level.id).size() >= 1);
    // A file damaged on disk afterwards.
    {
        std::vector<uint8_t> broken = bytes;
        for (size_t i = 20; i < broken.size(); i += 7) broken[i] ^= 0xFF;
        FileUtil::writeBinary(root / "cache" / "rec_1.cprz", broken);
        CHECK(!GhostStore::loadReplay(root / "cache" / "rec_1.cprz").has_value());
    }
    // A hand-written description is bounded and checked.
    FileUtil::writeTextAtomic(root / "cache" / "rec_1.json",
                              R"({"format":"cosmic.ghost","label":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","time_ms":-5,"splits":[1,"x",3],"record_id":"rec_1"})");
    auto e2 = store.cached("rec_1");
    CHECK(e2.has_value());
    if (e2) {
        CHECK(e2->meta.label.size() <= 48);
        CHECK(e2->meta.timeMs == 0);
        CHECK(e2->meta.splits.size() == 1);
    }
    // Favourites stay in the cache when it is trimmed.
    store.setFavorite("rec_1", true);
    CHECK(store.isFavorite("rec_1"));
    store.trimCache(0);
    CHECK(fs::exists(root / "cache" / "rec_1.cprz"));
    CHECK(!fs::exists(root / "cache" / "evil.cprz"));
    GhostStore again(root);
    CHECK(again.isFavorite("rec_1"));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("ghost: personal best, previous record and last run are kept per level version") {
    fs::path root = fs::temp_directory_path() / ("cosmic_ghost_test_" + makeUuidV4().substr(0, 8));
    GhostStore store(root);
    LevelData level = ghostLevel(false);
    Run slow = world().run(level, {0.6f});
    Run fast = world().run(level, {1.0f});
    GhostMeta m;
    m.levelId = level.id;
    m.levelHash = slow.replay.levelHash;
    m.label = "Moi";
    m.timeMs = static_cast<int64_t>(slow.time * 1000.0f);
    CHECK(store.recordRun(slow.replay, m, true));
    m.timeMs = static_cast<int64_t>(fast.time * 1000.0f);
    CHECK(store.recordRun(fast.replay, m, true));
    m.timeMs = static_cast<int64_t>(slow.time * 1000.0f);
    CHECK(store.recordRun(slow.replay, m, false));  // a slower run: only the last one changes
    auto ghosts = store.localGhosts(level.id);
    CHECK(ghosts.size() == 3);
    for (const auto& g : ghosts) {
        if (g.meta.source == "pb") CHECK(g.meta.timeMs == static_cast<int64_t>(fast.time * 1000.0f));
        if (g.meta.source == "prev") CHECK(g.meta.timeMs == static_cast<int64_t>(slow.time * 1000.0f));
        if (g.meta.source == "last") CHECK(g.meta.timeMs == static_cast<int64_t>(slow.time * 1000.0f));
        CHECK(GhostStore::loadReplay(g.file).has_value());
    }
    // Another version of the level has its own folder.
    GhostMeta other = m;
    other.levelHash = std::string(64, 'c');
    CHECK(GhostStore::levelKey(level.id, m.levelHash) != GhostStore::levelKey(level.id, other.levelHash));
    // A replay chosen by the player is copied (and checked first).
    fs::path src = root / "mon_replay.cprz";
    FileUtil::writeBinary(src, fast.replay.encode());
    auto imported = store.importFile(src);
    CHECK(imported.has_value());
    CHECK(store.files().size() == 1);
    FileUtil::writeBinary(root / "junk.cprz", std::vector<uint8_t>(300, 1));
    std::string err;
    CHECK(!store.importFile(root / "junk.cprz", &err).has_value());
    CHECK(!err.empty());
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("ghost: segment history keeps the best segments and the last runs") {
    fs::path file = fs::temp_directory_path() / ("cosmic_segments_" + makeUuidV4().substr(0, 8) + ".json");
    {
        SegmentHistory h(file);
        h.add("lvl-abc", {10.0f, 25.0f}, 40.0f, "2026-11-01 10:00");
        h.add("lvl-abc", {9.0f, 26.0f}, 39.0f, "2026-11-02 10:00");
        h.add("lvl-abc", {-1.0f}, 5.0f, "bad");            // refused
        h.add("lvl-abc", {10.0f, 5.0f}, 40.0f, "bad");     // not in order: refused
        CHECK(h.save());
    }
    SegmentHistory h(file);
    h.load();
    const auto* runs = h.runs("lvl-abc");
    CHECK(runs != nullptr);
    if (runs) {
        CHECK(runs->size() == 2);
        CHECK(std::abs(runs->front().time - 39.0f) < 1e-4f);  // most recent first
    }
    auto best = h.bestSegments("lvl-abc", 2);
    CHECK(best.size() == 3);
    CHECK(std::abs(best[0] - 9.0f) < 1e-4f);
    CHECK(std::abs(best[1] - 15.0f) < 1e-4f);
    CHECK(std::abs(best[2] - 13.0f) < 1e-4f);
    CHECK(h.bestSegments("other", 2)[0] < 0.0f);
    // Gold segments and "ahead of the record" during a race.
    LevelData level = ghostLevel(false);
    Run a = world().run(level, {1.0f});
    GhostRace race;
    race.add(GhostMeta{}, a.replay, true, Color{});
    race.setPersonal({a.splits[0] + 1.0f, 100.0f, 100.0f}, {a.splits[0] + 1.0f, a.splits[1] - 1.0f}, a.time + 1.0f);
    GhostTrack me;
    me.load(a.replay);
    race.onSplit(0, a.splits[0], me.poseAt(a.splits[0]).feet);
    CHECK(race.splits().back().goldSegment);
    CHECK(race.splits().back().aheadOfRecord);
    race.onSplit(1, a.splits[1], me.poseAt(a.splits[1]).feet);
    CHECK(race.splits().back().goldSegment);
    CHECK(!race.splits().back().aheadOfRecord);
    race.finish(a.time);
    CHECK(race.splits().back().aheadOfRecord);  // a new record
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("ghost: training from a checkpoint compares from there") {
    LevelData level = ghostLevel(false);
    Run fast = world().run(level, {1.0f});
    Run slow = world().run(level, {0.6f});
    GhostTrack slowTrack;
    slowTrack.load(slow.replay);
    // The player starts at the first checkpoint (run time 0 there); the ghost starts at its
    // own split at that checkpoint.
    GhostRace race;
    race.add(GhostMeta{}, fast.replay, true, Color{});
    race.setRoute(buildProgressTrack(level, world().content));
    race.setOffset(fast.splits[0], 0);
    GhostTrack::Pose ghostAtStart = race.racer(0).track.poseAt(fast.splits[0]);
    race.update(0.0f, slowTrack.poseAt(slow.splits[0]).feet, 0.0f);
    CHECK(std::abs(race.racer(0).pose.feet.x - ghostAtStart.feet.x) < 1.0f);
    // At the second checkpoint: the gap is the difference of the segments only.
    float myRun = slow.splits[1] - slow.splits[0];
    race.onSplit(1, myRun, slowTrack.poseAt(slow.splits[1]).feet);
    CHECK(race.splits().size() == 1);
    const auto& s = race.splits().back();
    CHECK(s.comparable);
    CHECK(s.ghostIndex == 1);
    float expected = (slow.splits[1] - slow.splits[0]) - (fast.splits[1] - fast.splits[0]);
    CHECK(std::abs(s.delta - expected) < 0.003f);
    CHECK(std::abs(s.segment - myRun) < 0.002f);
}
