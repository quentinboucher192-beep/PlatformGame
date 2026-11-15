// Replay comparison and advanced replay viewer (2.19): B's time offset and its alignment on
// a checkpoint to the millisecond, the segment table with an offset, typed offsets, and
// jumps in time in an exact replay giving the same state as a continuous playback.

#include <chrono>
#include <cmath>
#include <cstdio>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/LevelProgress.h"
#include "game/ghost/ReplayCompare.h"
#include "game/ghost/ReplayPlayback.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

/// Flat level: start at x 160, checkpoints at 800 and 1500, exit at 2200.
LevelData compareLevel() {
    LevelData l = LevelData::makeEmpty("test_compare", "Test comparaison", "sylvanox");
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

struct Fixture {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Fixture() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices services() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }

    /// A finished run of the flat level at `speed` (stick to the right).
    ReplayData run(const LevelData& level, float speed) {
        GameWorld w(services(), WorldMode::Play);
        w.load(level);
        w.seedGameplay(levelSeed(level.id));
        ReplayRecorder rec;
        rec.begin(level.id, fingerprintGameplay(content, level), makeUuidV4(), w.gameplaySeed());
        w.eventObserver = [&rec](const std::string& src, const std::string& trig) { rec.onWorldEvent(src, trig); };
        for (int i = 0; i < 120 * 90 && w.state() != WorldState::Completed && w.state() != WorldState::Outro; ++i) {
            PlayerInput in;
            in.move = {speed, 0.0f};
            if (speed > 0.5f) in.held[static_cast<int>(Action::MoveRight)] = true;
            InputFrame f = InputFrame::fromPlayerInput(in);
            rec.recordInput(f);
            stepWorld(w, f, kSimDt);
            rec.tick(w);
        }
        rec.finish(w, w.timer().elapsed());
        return rec.take();
    }
};

Fixture& fx() {
    static Fixture f;
    return f;
}

/// A hand-made run along a line (no simulation): feet moving right at `speed` px/s from x
/// 160, a split each time it passes one of `checkpoints`, finished at x 2200.
ReplayData lineRun(float speed, const std::vector<float>& checkpoints) {
    ReplayData r;
    r.levelId = "test_compare";
    r.levelHash = std::string(64, 'a');
    r.sampleHz = 30;
    double duration = (2200.0 - 160.0) / speed;
    r.durationMs = static_cast<uint32_t>(std::llround(duration * 1000.0));
    size_t n = static_cast<size_t>(std::ceil(duration * 30.0)) + 1;
    for (size_t i = 0; i < n; ++i) {
        ReplayFrame f;
        double t = std::min(duration, static_cast<double>(i) / 30.0);
        f.x = static_cast<float>(160.0 + speed * t);
        f.y = 800.0f;
        f.flags = ReplayFrame::Grounded;
        r.frames.push_back(f);
    }
    uint8_t k = 0;
    for (float cx : checkpoints) {
        int64_t ms = std::llround((cx + 16.0 - 160.0) / speed * 1000.0);
        auto frame = static_cast<uint32_t>(std::ceil(static_cast<double>(ms) * 30.0 / 1000.0));
        r.events.push_back({frame, ReplayEventType::Split, k++, static_cast<uint16_t>(ms & 0xFFFF)});
    }
    r.events.push_back({static_cast<uint32_t>(n - 1), ReplayEventType::Finish, 0, 0});
    return r;
}

}  // namespace

TEST_CASE("replay compare: B's offset and its alignment on a checkpoint are exact to the millisecond") {
    LevelData level = compareLevel();
    ReplayData slow = fx().run(level, 0.55f), fast = fx().run(level, 1.0f);
    bool exact = false;
    std::vector<int64_t> sa = replaySplitsMs(slow, &exact);
    CHECK(exact);
    std::vector<int64_t> sb = replaySplitsMs(fast);
    CHECK(sa.size() == 2);
    CHECK(sb.size() == 2);
    ReplayComparison cmp;
    cmp.set(CompareRun::fromReplay(slow, GhostMeta{}), CompareRun::fromReplay(fast, GhostMeta{}));
    cmp.setRoute(buildProgressTrack(level, fx().content));
    CHECK(cmp.sameVersion());
    CHECK(cmp.rows().size() == 3);  // two checkpoints and the exit
    CHECK(cmp.rows()[0].bSplit == 0);
    CHECK(cmp.rows()[1].bSplit == 1);
    // Without offset: the gaps are the differences of the splits, to the millisecond.
    for (size_t i = 0; i < 2; ++i) CHECK(cmp.rows()[i].gapMs == sa[i] - sb[i]);
    CHECK(cmp.totals().gapMs == static_cast<int64_t>(slow.durationMs) - static_cast<int64_t>(fast.durationMs));

    // Alignment on the second checkpoint: B passes it at the same moment as A.
    CHECK(cmp.alignOn(1));
    CHECK(cmp.alignedRow() == 1);
    CHECK(cmp.offsetMs() == sa[1] - sb[1]);
    CHECK(cmp.rows()[1].gapMs == 0);
    CHECK(cmp.rows()[1].bCumMs == cmp.rows()[1].aCumMs);
    CHECK(cmp.rows()[0].gapMs == (sa[0] - sb[0]) - cmp.offsetMs());
    CHECK(cmp.totals().gapMs ==
          static_cast<int64_t>(slow.durationMs) - (static_cast<int64_t>(fast.durationMs) + cmp.offsetMs()));
    // B's clock follows A's, shifted.
    CHECK(std::abs(cmp.bTime(10.0) - (10.0 - static_cast<double>(cmp.offsetMs()) / 1000.0)) < 1e-9);
    CHECK(cmp.endTime() >= static_cast<double>(slow.durationMs) / 1000.0);
    // At the checkpoint, the live gap (route estimate) agrees with the exact one.
    auto live = cmp.liveGapMs(static_cast<double>(sa[1]) / 1000.0);
    CHECK(live.has_value());
    if (live) CHECK(std::llabs(*live - cmp.rows()[1].gapMs) < 120);
    // Once both have finished, the live gap is the exact final gap.
    auto fin = cmp.liveGapMs(cmp.endTime() + 1.0);
    CHECK(fin.has_value() && *fin == cmp.totals().gapMs);

    // Fine and coarse steps, a typed value, back to zero.
    int64_t base = cmp.offsetMs();
    cmp.nudge(10);
    CHECK(cmp.offsetMs() == base + 10);
    CHECK(cmp.alignedRow() == -1);
    cmp.nudge(-100);
    cmp.nudge(1000);
    CHECK(cmp.offsetMs() == base + 910);
    CHECK(cmp.rows()[1].gapMs == -910);
    cmp.setOffsetMs(1250);
    CHECK(formatCompareOffset(cmp.offsetMs()) == "décalage +1,250 s");
    cmp.setOffsetMs(-50);
    CHECK(formatCompareOffset(cmp.offsetMs()) == "décalage -0,050 s");
    cmp.resetOffset();
    CHECK(formatCompareOffset(cmp.offsetMs()) == "sans décalage");
    CHECK(cmp.rows()[0].gapMs == sa[0] - sb[0]);
    cmp.setOffsetMs(1000000000);
    CHECK(cmp.offsetMs() == cmp.maxOffsetMs());
    // The exit: both finish together.
    CHECK(cmp.alignOn(2));
    CHECK(cmp.totals().gapMs == 0);
    CHECK(cmp.offsetMs() == static_cast<int64_t>(slow.durationMs) - static_cast<int64_t>(fast.durationMs));

    // Offsets typed by the player.
    CHECK(parseCompareOffset("+1,250") == std::optional<int64_t>(1250));
    CHECK(parseCompareOffset("-0.5 s") == std::optional<int64_t>(-500));
    CHECK(parseCompareOffset("250 ms") == std::optional<int64_t>(250));
    CHECK(parseCompareOffset("1:02,5") == std::optional<int64_t>(62500));
    CHECK(parseCompareOffset("\xE2\x88\x92" "1,2") == std::optional<int64_t>(-1200));
    CHECK(parseCompareOffset(",5") == std::optional<int64_t>(500));
    CHECK(parseCompareOffset("1,2345") == std::optional<int64_t>(1235));
    CHECK(!parseCompareOffset("").has_value());
    CHECK(!parseCompareOffset("+").has_value());
    CHECK(!parseCompareOffset("abc").has_value());
    CHECK(!parseCompareOffset("1,2,3").has_value());
    CHECK(formatRunMs(62358) == "1:02,358");
    CHECK(formatDeltaMs(-1230) == "-1,230 s");
    CHECK(formatDeltaMs(75500, false) == "+1:15,500");
}

TEST_CASE("replay compare: segment table with an offset (durations, passages, gaps, fastest)") {
    // A slower than B on every segment; B skipped the first checkpoint in a second run.
    ReplayData a = lineRun(200.0f, {800.0f, 1500.0f});
    ReplayData b = lineRun(250.0f, {800.0f, 1500.0f});
    ReplayComparison cmp;
    cmp.set(CompareRun::fromReplay(a, GhostMeta{}), CompareRun::fromReplay(b, GhostMeta{}));
    std::vector<int64_t> sa = replaySplitsMs(a), sb = replaySplitsMs(b);
    CHECK(sa.size() == 2 && sb.size() == 2);
    auto check = [&](int64_t offset) {
        cmp.setOffsetMs(offset);
        const auto& rows = cmp.rows();
        CHECK(rows.size() == 3);
        int64_t prevA = 0, prevB = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            int64_t aAt = r.exit ? a.durationMs : sa[i];
            int64_t bAt = r.exit ? b.durationMs : sb[i];
            // Durations: what each run took on the segment, whatever the offset.
            CHECK(r.aSegMs == aAt - prevA);
            CHECK(r.bSegMs == bAt - prevB);
            CHECK(r.hasSegDiff && r.segDiffMs == r.aSegMs - r.bSegMs);
            CHECK(r.faster == 2);  // B faster everywhere: its duration in gold
            // Passages on A's clock: B's split plus the offset; the gap includes it.
            CHECK(r.aCumMs == aAt);
            CHECK(r.bCumMs == bAt + offset);
            CHECK(r.hasGap && r.gapMs == aAt - (bAt + offset));
            prevA = aAt;
            prevB = bAt;
        }
        CHECK(cmp.totals().diffMs == static_cast<int64_t>(a.durationMs) - static_cast<int64_t>(b.durationMs));
        CHECK(cmp.totals().gapMs == static_cast<int64_t>(a.durationMs) - (static_cast<int64_t>(b.durationMs) + offset));
    };
    check(0);
    check(1250);
    check(-730);
    check(10);
    CHECK(cmp.alignOn(0));
    check(cmp.offsetMs());
    CHECK(cmp.rows()[0].gapMs == 0);
    CHECK(cmp.rowAt(0.0) == -1);
    CHECK(cmp.rowAt(static_cast<double>(sa[0]) / 1000.0) == 0);
    CHECK(cmp.rowAt(static_cast<double>(a.durationMs) / 1000.0 + 0.5) == 2);

    // B took a shortcut past the first checkpoint: that row has no B, the next segment is
    // unknown for B (it covers two of A's), the passages and the exit still compare.
    ReplayData shortcut = lineRun(250.0f, {1500.0f});
    ReplayComparison cut;
    cut.set(CompareRun::fromReplay(a, GhostMeta{}), CompareRun::fromReplay(shortcut, GhostMeta{}));
    cut.setOffsetMs(400);
    const auto& rows = cut.rows();
    CHECK(rows.size() == 3);
    CHECK(rows[0].bSplit == -1);
    CHECK(!rows[0].hasGap && rows[0].bSegMs == -1 && !rows[0].hasSegDiff);
    CHECK(rows[1].bSplit == 0);
    CHECK(rows[1].bSegMs == -1);
    CHECK(rows[1].hasGap && rows[1].gapMs == sa[1] - (replaySplitsMs(shortcut)[0] + 400));
    CHECK(rows[2].bSegMs == static_cast<int64_t>(shortcut.durationMs) - replaySplitsMs(shortcut)[0]);
    CHECK(!cut.alignOn(0));
    CHECK(cut.offsetMs() == 400);
    CHECK(cut.alignOn(1));
    CHECK(cut.rows()[1].gapMs == 0);

    // Another version of the level: still compared, never officially.
    ReplayData other = b;
    other.levelHash = std::string(64, 'b');
    ReplayComparison ov;
    ov.set(CompareRun::fromReplay(a, GhostMeta{}), CompareRun::fromReplay(other, GhostMeta{}));
    CHECK(!ov.sameVersion());
    CHECK(ov.rows().size() == 3);
}

TEST_CASE("replay: a jump in time gives the same state as playing continuously") {
    auto level = fx().content.loadLevel("sylvanox_01");
    CHECK(level.has_value());
    // A run with enemies, jumps and attacks, recorded like in a game.
    ReplayData replay;
    {
        GameWorld w(fx().services(), WorldMode::Play);
        w.load(*level);
        w.seedGameplay(levelSeed(level->id));
        ReplayRecorder rec;
        rec.begin(level->id, fingerprintGameplay(fx().content, *level), makeUuidV4(), w.gameplaySeed());
        w.eventObserver = [&rec](const std::string& src, const std::string& trig) { rec.onWorldEvent(src, trig); };
        Random r(42);
        InputFrame prev, cur;
        int hold = 0;
        for (int i = 0; i < 120 * 25; ++i) {
            if (hold-- <= 0) {
                cur = InputFrame{};
                bool right = r.unit() < 0.8f;
                cur.held |= static_cast<uint16_t>(1u << static_cast<int>(right ? Action::MoveRight : Action::MoveLeft));
                cur.moveX = static_cast<int8_t>(right ? 127 : -127);
                if (r.chance(0.5f)) cur.held |= 1u << static_cast<int>(Action::Jump);
                if (r.chance(0.3f)) cur.held |= 1u << static_cast<int>(Action::Attack);
                hold = r.rangeInt(6, 40);
            }
            InputFrame f = cur;
            if (w.cinematic().active() && w.cinematic().skippable() && i > 90) f.flags |= InputFrame::SkipCinematic;
            f.pressed = static_cast<uint16_t>(f.held & ~prev.held);
            f.released = static_cast<uint16_t>(prev.held & ~f.held);
            prev = f;
            rec.recordInput(f);
            stepWorld(w, f, kSimDt);
            rec.tick(w);
        }
        rec.finishAttempt(w, w.timer().elapsed());
        replay = rec.take();
    }
    CHECK(replay.exact());
    const uint32_t total = static_cast<uint32_t>(replay.inputs.size());

    // Continuous playback: the state digest and the chronometer after every step.
    std::vector<uint64_t> digest(total + 1);
    std::vector<float> chrono(total + 1);
    auto t0 = std::chrono::steady_clock::now();
    {
        ReplayPlayback p(fx().services(), *level, replay);
        digest[0] = simulationStateHash(p.world());
        chrono[0] = p.runTime();
        while (p.step()) {
            digest[p.tick()] = simulationStateHash(p.world());
            chrono[p.tick()] = p.runTime();
        }
        CHECK(p.finished());
        CHECK(!p.desynced());
        CHECK(p.checksPassed() > 10);
    }
    double fullMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    // Jumps forward, back (the level is loaded again and simulated up to there), one step
    // back, by time: always the state of the continuous playback.
    ReplayPlayback p(fx().services(), *level, replay);
    uint32_t gen = p.generation();
    const uint32_t jumps[] = {2200, 1500, 1499, 1800, 10, total, 0, 2999};
    for (uint32_t target : jumps) {
        target = std::min(target, total);
        p.seekTick(target);
        p.finishSeek();
        CHECK(p.tick() == target);
        CHECK(simulationStateHash(p.world()) == digest[target]);
        CHECK(!p.desynced());
    }
    CHECK(p.generation() > gen);  // went back: the level was loaded again
    // Step by step, after a jump back, like continuous play.
    p.seekTick(1200);
    p.finishSeek();
    for (int i = 0; i < 30; ++i) {
        CHECK(p.step());
        CHECK(simulationStateHash(p.world()) == digest[p.tick()]);
    }
    // Points de reprise: copies kept behind the step shown make the steps back short, and
    // give the same state.
    p.seekTick(2400);
    p.finishSeek();
    for (int i = 0; i < 40; ++i) p.maintain(100000);
    std::vector<uint32_t> points = p.resumePoints();
    CHECK(points.size() == 3);
    CHECK(points.back() < 2400 && points.back() >= 2300);
    uint32_t backTo = 2399;
    for (int k = 0; k < 3; ++k, --backTo) {
        uint32_t before = p.generation();
        p.seekTick(backTo);
        CHECK(p.generation() == before + 1);
        CHECK(p.lastResume() > 0 && p.lastResume() <= backTo);  // not from the level's start
        p.finishSeek();
        CHECK(simulationStateHash(p.world()) == digest[backTo]);
    }
    p.seekTick(backTo);  // every point used: the level is loaded again
    CHECK(p.lastResume() == 0);
    p.finishSeek();
    CHECK(simulationStateHash(p.world()) == digest[backTo]);
    // While it plays, the points follow.
    for (int i = 0; i < 300; ++i) {
        CHECK(p.step());
        p.maintain(400);
    }
    points = p.resumePoints();
    CHECK(points.size() == 3);
    CHECK(points.back() + 200 > p.tick());
    p.seekTick(p.tick() - 1);
    CHECK(p.lastResume() == points.back());
    p.finishSeek();
    CHECK(simulationStateHash(p.world()) == digest[p.tick()]);

    // By chronometer: the first step showing that time (here a time already simulated,
    // then one beyond what this playback had simulated, in budgeted pieces).
    double at = chrono[1700];
    p.seekTime(at);
    p.finishSeek();
    CHECK(p.runTime() + 1e-4 >= at);
    CHECK(p.tick() == 0 || chrono[p.tick() - 1] + 1e-4 < at);
    CHECK(simulationStateHash(p.world()) == digest[p.tick()]);
    ReplayPlayback q(fx().services(), *level, replay);
    q.seekTime(chrono[2600]);
    int pieces = 0;
    while (!q.advance(500)) ++pieces;
    CHECK(pieces >= 4);
    CHECK(simulationStateHash(q.world()) == digest[q.tick()]);
    CHECK(std::abs(q.runTime() - chrono[2600]) < 1e-4 || chrono[q.tick() - 1] + 1e-4 < chrono[2600]);
    auto t1 = std::chrono::steady_clock::now();
    ReplayPlayback full(fx().services(), *level, replay);
    full.seekTick(total);
    full.finishSeek();
    double seekMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    CHECK(simulationStateHash(full.world()) == digest[total]);
    std::printf("  replay: %u pas simulés en %.0f ms avec une empreinte par pas, saut jusqu'à la fin en %.0f ms (%.1f µs par pas)\n", total,
                fullMs, seekMs, seekMs * 1000.0 / total);
}
