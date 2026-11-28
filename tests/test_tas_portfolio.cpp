// The portfolio of the search's strategies (tools/tas/Portfolio.h, Explore.h, Timing.h, Bound.h;
// docs/TAS.md « Le portefeuille »): the bandit gives the time to the arm that improves without
// starving the others; branch-and-bound never prunes the best run's own states; the timing polish
// finds a lost frame again; Go-Explore gets past a wall where the beam's measure of progress says
// to wait at a door (a small level built here);
// the same slice gives the same run; every run they give is verified and faster.
// On the run shipped for sylvanox_01 (ANY%).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasProgress.h"
#include "tests/TestFramework.h"
#include "tools/tas/Bound.h"
#include "tools/tas/Course.h"
#include "tools/tas/Explore.h"
#include "tools/tas/Output.h"
#include "tools/tas/Portfolio.h"
#include "tools/tas/Sim.h"
#include "tools/tas/Timing.h"

using namespace cosmic;

namespace {

struct Shipped {
    ContentDatabase content;
    LevelFingerprints fps;
    std::optional<LevelData> level;
    const LevelFingerprint* fp = nullptr;
    std::vector<InputFrame> best;
    bool ok = false;
    Shipped() {
        content.reload();
        auto l = content.loadLevel("sylvanox_01");
        if (!l) return;
        level = std::move(*l);
        fp = fps.get(content, "sylvanox_01");
        std::string why;
        ok = fp && tas::readSeedReplay(Paths::toUtf8(tasRoot() / "sylvanox_01" / "ANY_PERCENT.cprz"), "sylvanox_01", best, why);
    }
    int64_t verified(const std::vector<InputFrame>& run) const {
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, RunCategory::AnyPercent, run, 0);
        return v.ok ? v.record.timeMs : -1;
    }
};

int64_t ms(float s) { return static_cast<int64_t>(std::llround(static_cast<double>(s) * 1000.0)); }

/// A slice for a test: its offers kept (the fastest), stopped after `seconds` or at the first offer.
struct TestSlice {
    std::mutex m;
    std::vector<InputFrame> run;
    float clock = 0.0f;
    int offers = 0;
    std::atomic<bool> stop{false};
    tas::Slice make(const std::vector<InputFrame>& best, float bestClock, double seconds, int firstSection, int lastSection, bool stopAtFirst) {
        tas::Slice s;
        s.best = best;
        s.bestClock = bestClock;
        s.firstSection = firstSection;
        s.lastSection = lastSection;
        s.until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
        s.threads = 1;
        s.seed = 5;
        s.stopped = [this] { return stop.load(); };
        s.offer = [this, stopAtFirst](const std::vector<InputFrame>& r, float c, const std::string&) {
            std::lock_guard<std::mutex> l(m);
            if (offers == 0 || c < clock) run = r, clock = c;
            ++offers;
            if (stopAtFirst) stop = true;
        };
        return s;
    }
};

}  // namespace

TEST_CASE("tas portfolio: the bandit gives the time to the arm that improves, and none starves") {
    tas::Bandit b(7);
    const int good = b.add("good"), dry = b.add("dry"), other = b.add("other");
    Random luck(3);
    for (int i = 0; i < 120; ++i) {
        const int a = b.pick();
        CHECK(a >= 0);
        // A slice costs 10 CPU-seconds; only « good » gains (20 ms, one slice in two).
        b.record(a, 10.0, a == good && luck.chance(0.5f) ? 20.0 : 0.0);
    }
    std::printf("  parts du temps : %.0f %% / %.0f %% / %.0f %%\n", 100.0 * b.share(good), 100.0 * b.share(dry), 100.0 * b.share(other));
    CHECK(b.share(good) > 0.6);
    CHECK(b.share(dry) >= 0.07 && b.share(other) >= 0.07);  // the floor
    CHECK(b.arms()[static_cast<size_t>(good)].wins > 0 && b.arms()[static_cast<size_t>(dry)].wins == 0);
    // The time follows a change: « dry » starts to gain, « good » stops.
    for (int i = 0; i < 150; ++i) {
        const int a = b.pick();
        b.record(a, 10.0, a == dry ? 20.0 : 0.0);
    }
    double late = 0.0, lateDry = 0.0;
    for (int i = 0; i < 40; ++i) {
        const int a = b.pick();
        b.record(a, 10.0, a == dry ? 20.0 : 0.0);
        late += 1.0;
        lateDry += a == dry ? 1.0 : 0.0;
    }
    CHECK(lateDry / late > 0.6);
    // Deterministic: the same seed and the same rewards, the same choices.
    tas::Bandit x(11), y(11);
    for (tas::Bandit* z : {&x, &y})
        for (const char* id : {"a", "b", "c"}) z->add(id);
    bool same = true;
    for (int i = 0; i < 60; ++i) {
        const int a = x.pick(), c = y.pick();
        same = same && a == c;
        x.record(a, 5.0, a == 1 ? 3.0 : 0.0);
        y.record(c, 5.0, c == 1 ? 3.0 : 0.0);
    }
    CHECK(same);
}

TEST_CASE("tas portfolio: branch-and-bound never prunes the best run's own states") {
    Shipped S;
    if (!S.ok) return;
    tas::Course course(*S.level, S.content, RunCategory::AnyPercent);
    tas::Sim sim(S.content, *S.level, course, RunCategory::AnyPercent);
    const tas::RunStudy g = sim.study(S.best);
    CHECK(g.finishes() && g.ends.size() == course.sectionEnds().size());
    if (!g.finishes()) return;
    tas::TimeBound bound(course, sim.world());
    bound.observe(g.maxSpeedX, g.maxSpeed);
    const tas::Pruner pr = tas::Pruner::from(bound, g, 0.0f);
    CHECK(pr.active());
    // Along the best run itself: never pruned (the bound never says more than the time it took).
    auto sectionOf = [&](int wp) {
        size_t s = 0;
        for (int e : course.sectionEnds())
            if (wp > e) ++s;
        return std::min(s, course.sectionEnds().size() - 1);
    };
    sim.arena().restore(sim.origin());
    int wp = 0, pruned = 0, tight = 0;
    float worstSlack = 1e9f;
    for (size_t i = 0; i < g.exitAt; ++i) {
        sim.arena().step(S.best[i]);
        const tas::Progress p = sim.measure(wp);
        wp = p.waypoint;
        if (p.finished) break;
        const size_t s = sectionOf(wp);
        if (pr.prune(sim.world(), s, course.sectionEnds()[s], p.clock, pr.startSplit[s])) ++pruned;
        const float lb = bound.toWaypoint(sim.world(), course.sectionEnds()[s]);
        const float truth = pr.split[s] - p.clock;
        worstSlack = std::min(worstSlack, truth - lb);
        if (lb > 0.5f) ++tight;
    }
    std::printf("  borne : jamais au-dessus du vrai temps restant (écart minimal %.3f s), > 0,5 s sur %d images\n", static_cast<double>(worstSlack), tight);
    CHECK(pruned == 0);
    CHECK(worstSlack >= 0.0f);
    CHECK(tight > 100);  // a bound that says something
    // The same run stopping 2 s a second before the first checkpoint: pruned there.
    sim.arena().restore(sim.origin());
    wp = 0;
    bool cut = false;
    const size_t stopAt = g.ends[0] - 120;
    for (size_t i = 0; i < stopAt + 240 && !cut; ++i) {
        sim.arena().step(i < stopAt ? S.best[i] : InputFrame{});
        const tas::Progress p = sim.measure(wp);
        wp = p.waypoint;
        cut = sectionOf(wp) == 0 && pr.prune(sim.world(), 0, course.sectionEnds()[0], p.clock, pr.startSplit[0]);
    }
    CHECK(cut);
}

TEST_CASE("tas portfolio: the timing polish finds a lost frame again (and the same edit twice)") {
    Shipped S;
    if (!S.ok) return;
    tas::Course course(*S.level, S.content, RunCategory::AnyPercent);
    tas::Sim sim(S.content, *S.level, course, RunCategory::AnyPercent);
    const tas::RunStudy g = sim.study(S.best);
    if (!g.finishes() || g.ends.empty()) return;
    // A run a few frames slower: Right let go for 3 frames while Nova runs to the exit in the last
    // section. (Earlier, the run's time is set by what comes after: a few frames lost in section 1
    // are absorbed, the re-join after it even gains 17 ms.)
    const size_t last = g.ends.size() - 1, from = g.ends[last - 1];
    std::vector<InputFrame> slow;
    float slowClock = 0.0f;
    const uint32_t right = tas::keyBit(Action::MoveRight);
    for (size_t i = g.exitAt - 20; i > from + 10 && slow.empty(); i -= 7) {
        bool running = true;
        for (size_t k = i - 6; k < i + 8 && k < g.exitAt; ++k) running = running && (S.best[k].held & right) && !g.frozen[k];
        if (!running || std::abs(g.vel[i].x) < 100.0f) continue;
        std::vector<InputFrame> e = S.best;
        for (size_t k = i; k < i + 3; ++k) e[k].held &= ~right;
        tas::repressRun(e, i, i + 3);
        const float c = sim.playToExit(sim.origin(), e, 0, 1e9f);
        if (c > g.clock + 0.5f / 120.0f && c < g.clock + 0.1f) {
            slow = e;
            slowClock = c;
        }
    }
    CHECK(!slow.empty());
    if (slow.empty()) return;
    std::vector<std::unique_ptr<tas::Sim>> own;
    for (int i = 0; i < 2; ++i) own.push_back(std::make_unique<tas::Sim>(S.content, *S.level, course, RunCategory::AnyPercent));
    std::vector<InputFrame> first;
    for (int attempt = 0; attempt < 2; ++attempt) {
        tas::TimingPolish polish;
        TestSlice T;
        tas::Slice sl = T.make(slow, slowClock, 120.0, static_cast<int>(last), static_cast<int>(last), true);
        sl.keep = from;
        polish.run({own[static_cast<size_t>(attempt)].get()}, sl);
        CHECK(T.offers > 0);
        if (T.offers == 0) return;
        CHECK(T.clock + 0.5f / 120.0f < slowClock);  // at least a frame found again
        CHECK(S.verified(T.run) == ms(T.clock));     // a real run, at the time claimed
        if (attempt == 0) first = T.run;
        else CHECK(T.run == first);  // deterministic: the same first gain
        std::printf("  polissage : %.3f s -> %.3f s (la course livrée : %.3f s), %lld essais\n", static_cast<double>(slowClock), static_cast<double>(T.clock),
                    static_cast<double>(g.clock), static_cast<long long>(polish.total().tried));
    }
}

TEST_CASE("tas portfolio: the progress file keeps the strategies, the curve's line names them") {
    TasProgress p;
    p.levelId = "sylvanox_01";
    p.strategies = {{"beam", 120000, 0, 0}, {"explore", 60000, 42, 2}, {"timing", 20000, 8, 1}};
    const auto back = TasProgress::fromJson(p.toJson());
    CHECK(back.has_value());
    if (!back) return;
    CHECK(back->strategies == p.strategies);
    CHECK(tasFoundByText(back->strategies) == "Trouvé par : exploration 2 · polissage 1");
    CHECK(tasTimeShareText(back->strategies) == "Part du temps : faisceau 60 % · exploration 30 % · polissage 10 %");
    CHECK(tasFoundByText({}).empty());
}

TEST_CASE("tas portfolio: Go-Explore gets past a wall the beam's distance says to wait behind") {
    // A small level: the exit behind a wall; a door at its foot opens at 12 s. The level's geometry
    // (LevelNavigator: doors counted open) says « through the door »: the beam's measure of
    // progress is deceptive there — the states waiting at the door look the furthest. Steps on the
    // left lead over the wall, seconds sooner.
    ContentDatabase content;
    content.reload();
    LevelData L = LevelData::makeEmpty("tas_mur", "Mur", "sylvanox");
    L.objects.clear();
    auto add = [&](const std::string& id, const std::string& type, Rect r, Json props = Json::object()) {
        LevelObject o;
        o.id = id;
        o.type = type;
        o.layer = type == "solid" ? "terrain" : "gameplay";
        o.rect = r;
        o.props = std::move(props);
        L.objects.push_back(std::move(o));
    };
    add("sol", "solid", {0, 800, 2400, 160});
    add("bord", "solid", {0, 0, 32, 800});
    add("depart", "player_start", {160, 778, 10, 22});
    add("sortie", "exit", {1500, 736, 40, 64}, {{"locked", false}});
    add("mur", "solid", {900, 520, 16, 152});
    add("rebord", "solid", {900, 500, 60, 20});
    add("porte", "door", {900, 672, 16, 128}, {{"open", false}});
    // The last step (to the ledge) as close as the others: the slide under the closed door, which
    // the archive found first before simulation 20, no longer goes through it.
    const float steps[][2] = {{600, 750}, {680, 700}, {760, 650}, {680, 600}, {760, 550}, {830, 525}};
    for (size_t k = 0; k < std::size(steps); ++k) add("marche" + std::to_string(k), "solid", {steps[k][0], steps[k][1], 60, 16});
    LevelEvent ev;
    ev.id = "ev1";
    ev.source = "level";
    ev.trigger = "time";
    ev.params = {{"seconds", 12}};
    EventAction open;
    open.type = "open";
    open.target = "porte";
    ev.actions.push_back(open);
    L.events.push_back(ev);

    tas::Course course(L, content, RunCategory::AnyPercent);
    tas::Sim sim(content, L, course, RunCategory::AnyPercent);
    // The run given: Right held — to the door, waiting there, through it when it opens.
    std::vector<InputFrame> door(2600);
    for (InputFrame& f : door) f.held = tas::keyBit(Action::MoveRight), f.moveX = 127;
    tas::repressRun(door, 0, door.size());
    const tas::RunStudy g = sim.study(door);
    CHECK(g.finishes());
    if (!g.finishes()) return;
    door.resize(g.exitAt);
    std::vector<std::unique_ptr<tas::Sim>> own;
    own.push_back(std::make_unique<tas::Sim>(content, L, course, RunCategory::AnyPercent));
    tas::GoExplore ex;
    TestSlice T;
    tas::Slice sl = T.make(door, g.clock, 150.0, 0, -1, false);
    // Stop as soon as a run beats the door by 2 s.
    sl.stopped = [&T, &g] {
        std::lock_guard<std::mutex> l(T.m);
        return T.offers > 0 && T.clock + 2.0f < g.clock;
    };
    const tas::ExploreStats st = ex.run({own[0].get()}, sl);
    std::printf("  le mur : par la porte %.3f s, exploration %.3f s (%zu cellules, %lld visites)\n", static_cast<double>(g.clock),
                static_cast<double>(T.offers ? T.clock : 0.0f), ex.cells(), static_cast<long long>(st.visits));
    CHECK(T.offers > 0);
    if (T.offers == 0) return;
    CHECK(T.clock + 2.0f < g.clock);
    // Past the wall long before the door opens (over it: simulation 20 no longer lets a slide go
    // through the closed door, kSlideInWallsSimulation), and the run replays to the time claimed.
    const tas::RunStudy r = sim.study(T.run);
    size_t past = r.feet.size();
    for (size_t i = 1; i < r.feet.size() && past == r.feet.size(); ++i)
        if (r.feet[i - 1].x < 916.0f && r.feet[i].x >= 916.0f) past = i;
    CHECK(past < r.feet.size());
    if (past < r.feet.size())
        std::printf("    le mur passé à %.3f s (la porte s'ouvre à 12 s), pieds à y = %.0f (le rebord : 500, le sol : 800)\n", static_cast<double>(r.clockAt[past]),
                    static_cast<double>(r.feet[past].y));
    CHECK(past < r.feet.size() && r.clockAt[past] < 11.5f);
    CHECK(r.finishes() && std::abs(r.clock - T.clock) < 1e-4f);
}
