// The search of the machine's runs (tools/tas/Search.cpp, docs/TAS.md « Les tours », « La meilleure
// course guide la recherche »): the windows of the rounds rotate over the level, sooner again where
// a round gained; after the run has left the best run, the best run's own frames still lead it to
// the exit (re-joined, verified by the replays); a section searched after a divergence is offered
// the best run's frames; a window round keeps the sections before it exactly, re-joins the best
// run after it, and never gives a slower run. On the run shipped for sylvanox_01 (ANY%).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/tas/TasFile.h"
#include "tests/TestFramework.h"
#include "tools/tas/Output.h"
#include "tools/tas/Search.h"

using namespace cosmic;

namespace {

/// The run shipped for sylvanox_01 ANY%, verified: the best run the tests start from.
struct Shipped {
    ContentDatabase content;
    LevelFingerprints fps;
    std::optional<LevelData> level;
    const LevelFingerprint* fp = nullptr;
    std::vector<InputFrame> best;
    int64_t bestMs = 0;
    bool ok = false;
    Shipped() {
        content.reload();
        auto l = content.loadLevel("sylvanox_01");
        if (!l) return;
        level = std::move(*l);
        fp = fps.get(content, "sylvanox_01");
        std::string why;
        if (!fp || !tas::readSeedReplay(Paths::toUtf8(tasRoot() / "sylvanox_01" / "ANY_PERCENT.cprz"), "sylvanox_01", best, why)) return;
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, RunCategory::AnyPercent, best, 0);
        if (!v.ok) {
            std::printf("  AVERTISSEMENT : la course livrée de sylvanox_01 ne se vérifie plus (%s) : non testé\n", v.why.c_str());
            return;
        }
        bestMs = v.record.timeMs;
        ok = true;
    }
    /// `run` verified by the normal replay playback: its time (ms), or -1.
    int64_t verified(const std::vector<InputFrame>& run) const {
        const tas::Verified v = tas::recordAndVerify(content, *level, *fp, RunCategory::AnyPercent, run, 0);
        return v.ok ? v.record.timeMs : -1;
    }
};

int64_t ms(float s) { return static_cast<int64_t>(std::llround(static_cast<double>(s) * 1000.0)); }

bool samePrefix(const std::vector<InputFrame>& a, const std::vector<InputFrame>& b, size_t n) {
    return a.size() >= n && b.size() >= n && std::equal(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(n), b.begin());
}

}  // namespace

TEST_CASE("tas search: the windows of the rounds rotate over the level, sooner again where a round gained") {
    tas::WindowPlanner p(7);
    CHECK(p.width() == 2);
    CHECK(tas::WindowPlanner(2).width() == 1 && tas::WindowPlanner(0).next(2) == std::make_pair(0, 0));
    p.done(1, 0, 0, 0);  // round 1: the whole level
    std::set<int> seen;
    for (int r = 2; r <= 8; ++r) {
        const auto [a, b] = p.next(r);
        CHECK(a >= 1 && b == a + 1 && b <= 7);
        for (int k = a; k <= b; ++k) seen.insert(k);
        p.done(r, a, b, 0);
    }
    CHECK(seen.size() == 7);  // every section searched deeply within a few rounds
    // Without a gain, round 3 goes on to the next sections; after a gain in sections 1–2, it comes
    // back to them (section 2 with the stale section 3).
    tas::WindowPlanner plain(7), gained(7);
    for (tas::WindowPlanner* w : {&plain, &gained}) w->done(1, 0, 0, 0);
    CHECK(plain.next(2) == std::make_pair(1, 2) && gained.next(2) == std::make_pair(1, 2));
    plain.done(2, 1, 2, 0);
    gained.done(2, 1, 2, 34);
    CHECK(plain.next(3) == std::make_pair(3, 4));
    CHECK(gained.next(3).first <= 2);
    CHECK(gained.gainOf(0) == 1.0 && plain.gainOf(0) == 0.0);
}

TEST_CASE("tas search: after the run has left the best run, the best run's frames lead it to the exit") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    CHECK(ends.size() == 7);
    if (ends.size() < 3) return;
    // The best run to the end of section 2, then a few steps more of its last keys: not the best run any more.
    std::vector<InputFrame> prefix(S.best.begin(), S.best.begin() + static_cast<std::ptrdiff_t>(ends[1]));
    for (int i = 0; i < 3; ++i) {
        InputFrame f = prefix.back();
        f.pressed = f.released = 0;
        f.flags = 0;
        f.choice = 0;
        prefix.push_back(f);
    }
    const tas::Rejoin r = tas::rejoinBest(S.content, *S.level, RunCategory::AnyPercent, prefix, S.best, 1);
    CHECK(r.found && r.tried >= 3);
    if (!r.found) return;
    CHECK(samePrefix(r.inputs, prefix, prefix.size()));
    CHECK(r.seedAt + 120 >= ends[1] && r.seedAt <= ends[1] + 120);
    // After the seam (its presses recomputed), the best run's own frames.
    bool same = true;
    for (size_t i = 1; prefix.size() + i < r.inputs.size(); ++i) same = same && r.inputs[prefix.size() + i] == S.best[r.seedAt + i];
    CHECK(same);
    // A real run: the normal replay playback gives the time the search measured.
    CHECK(S.verified(r.inputs) == ms(r.clock));
    std::printf("  raccord : %.3f s (la course livrée : %.3f s), repris au pas %zu (fin de section %zu)\n", static_cast<double>(r.clock),
                static_cast<double>(S.bestMs) / 1000.0, r.seedAt, ends[1]);
}

TEST_CASE("tas search: a section searched after a divergence is offered the best run's frames") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    if (ends.size() < 5) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 25.0;
    o.seed = 3;
    o.seedRun = S.best;
    o.refine = false;
    // The best run's frames alone, within the share of 25 s of their section; the planner's work (its
    // own test: test_tas_planner.cpp) costs more than that.
    o.planner = false;
    // A run that has left the best run in section 5 (in the run of 2.100, a few frames late in
    // section 3 or 4 meet its moving parts out of phase: its frames no longer arrive there).
    o.startRun.assign(S.best.begin(), S.best.begin() + static_cast<std::ptrdiff_t>(ends[3]));
    for (int i = 0; i < 3; ++i) {
        InputFrame f = o.startRun.back();
        f.pressed = f.released = 0;
        o.startRun.push_back(f);
    }
    const tas::SearchOutcome out = tas::search(S.content, *S.level, o);
    std::printf("  guidée : %d section(s), %d arrivée(s) par ses images, %d raccord(s), %.3f s\n", out.guidedSections, out.guideArrivals, out.rejoins,
                static_cast<double>(out.clock));
    CHECK(out.guidedSections >= 1);
    CHECK(out.guideArrivals >= 1);
    CHECK(out.rejoins >= 1);
    // Never worse than the best run, and a real run.
    CHECK(out.found && ms(out.clock) <= S.bestMs);
    CHECK(S.verified(out.inputs) == ms(out.clock));
}

TEST_CASE("tas search: a window round keeps the sections before it, re-joins the best run, never regresses") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    if (ends.size() < 3) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 30.0;
    o.seed = 5;
    o.seedRun = S.best;
    o.windowFirst = o.windowLast = 3;
    const tas::SearchOutcome out = tas::search(S.content, *S.level, o);
    std::printf("  fenêtre %d à %d : %zu pas gardés, %d passe(s), %d raccord(s), %.3f s (livrée %.3f s)\n", out.windowFirst, out.windowLast, out.keptFrames,
                out.passes, out.rejoins, static_cast<double>(out.clock), static_cast<double>(S.bestMs) / 1000.0);
    CHECK(out.found && out.windowFirst == 3 && out.windowLast == 3);
    CHECK(out.keptFrames == ends[1]);
    // The sections before the window exactly the best run's, after the refinement too.
    CHECK(samePrefix(out.inputs, S.best, out.keptFrames));
    CHECK(samePrefix(out.beforeRefinement, S.best, out.keptFrames));
    CHECK(out.passes >= 1 && out.rejoins >= 1);
    // A run kept that ended with the best run's rest: its frames after the seam are the best run's.
    if (out.rejoined) {
        bool same = out.rejoinAt <= out.beforeRefinement.size();
        for (size_t i = 1; same && out.rejoinAt + i < out.beforeRefinement.size(); ++i) same = out.beforeRefinement[out.rejoinAt + i] == S.best[out.rejoinSeedAt + i];
        CHECK(same);
    }
    // Never worse, and verified.
    CHECK(ms(out.clock) <= S.bestMs);
    CHECK(S.verified(out.inputs) == ms(out.clock));
}

namespace {

/// `run` with `idle` steps of nothing held inserted just after `at` (a pause, as a player would
/// stand still there): the rest pressed again from nothing.
std::vector<InputFrame> withPause(const std::vector<InputFrame>& run, size_t at, int idle) {
    std::vector<InputFrame> out(run.begin(), run.begin() + static_cast<std::ptrdiff_t>(at));
    InputFrame none;
    none.released = out.empty() ? 0 : out.back().held;
    out.push_back(none);
    for (int i = 1; i < idle; ++i) out.push_back(InputFrame{});
    for (size_t i = at; i < run.size(); ++i) {
        InputFrame f = run[i];
        if (i == at) {
            f.pressed = f.held;
            f.released = 0;
        }
        out.push_back(f);
    }
    return out;
}

/// `run` slowed by a pause somewhere in [from, to) where it still finishes (the level's moving
/// parts do not forgive a pause anywhere): the first such run and its verified time; -1: none.
std::pair<std::vector<InputFrame>, int64_t> slowed(const Shipped& S, const std::vector<InputFrame>& run, size_t from, size_t to) {
    for (int idle : {60, 24, 8})
        for (size_t at = from; at < to && at + 1 < run.size(); at += 60) {
            std::vector<InputFrame> r = withPause(run, at, idle);
            const int64_t t = S.verified(r);
            if (t > S.bestMs) return {std::move(r), t};
        }
    return {{}, -1};
}

}  // namespace

TEST_CASE("tas guides: a guide of another version of the level or of the simulation is refused") {
    ReplayData r;
    r.levelId = "sylvanox_01";
    r.levelHash = std::string(64, 'a');
    r.attemptId = makeUuidV4();
    r.keysPerSlot = true;
    r.inputs.resize(10);
    r.frames.push_back(ReplayFrame{});
    CHECK(tas::guideRefusal(r, "sylvanox_01", std::string(64, 'a')).empty());
    CHECK(!tas::guideRefusal(r, "sylvanox_01", std::string(64, 'b')).empty());  // another version of the level
    CHECK(!tas::guideRefusal(r, "sylvanox_02", std::string(64, 'a')).empty());  // another level
    ReplayData old = r;
    old.simVersion = static_cast<uint16_t>(kSimulationVersion - 1);
    CHECK(tas::guideRefusal(old, "sylvanox_01", std::string(64, 'a')).find("simulation") != std::string::npos);
    ReplayData coarse = r;
    coarse.inputs.clear();
    CHECK(!tas::guideRefusal(coarse, "sylvanox_01", std::string(64, 'a')).empty());
    // Through the file, as cosmic_tas --guide reads it.
    const fs::path file = Paths::userDir() / "tas_guide_refused.cprz";
    r.levelHash = std::string(64, 'c');
    CHECK(FileUtil::writeBinary(file, r.encode()));
    std::vector<InputFrame> in;
    std::string id, why;
    CHECK(!tas::readGuideReplay(Paths::toUtf8(file), "sylvanox_01", std::string(64, 'a'), in, id, why));
    CHECK(why.find("version du niveau") != std::string::npos);
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("tas guides: a guide faster early and a best run faster late are spliced into a faster run, verified") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    if (ends.size() < 7) return;
    // The best run (the machine's): slower early (a pause in section 4, where the run still
    // finishes); the guide (a player's run): slower late (a pause in section 5, or 7) — the shape of
    // a player's run against the machine's (sections 1 to 3 and the boss do not forgive a pause).
    // (In the run of 2.100, sections 1 to 3 and the start of section 4 do not forgive a pause: their
    // moving parts; the rest of section 4 and the later sections do.)
    auto [tasRun, tasMs] = slowed(S, S.best, ends[2], ends[3]);
    auto [guide, guideMs] = slowed(S, S.best, ends[3], ends[4]);
    if (guideMs < 0) std::tie(guide, guideMs) = slowed(S, S.best, ends[5], ends[6]);
    std::printf("  course de la machine ralentie tôt : %lld ms ; guide ralenti tard : %lld ms ; livrée %lld ms\n", static_cast<long long>(tasMs),
                static_cast<long long>(guideMs), static_cast<long long>(S.bestMs));
    CHECK(tasMs > S.bestMs && guideMs > S.bestMs);
    if (tasMs <= 0 || guideMs <= 0) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 40.0;
    o.seed = 9;
    o.seedRun = tasRun;
    o.guides = {guide};
    o.windowFirst = o.windowLast = 7;  // a short window after the splice: the splice is the point
    o.refine = false;
    std::vector<std::pair<std::vector<InputFrame>, float>> handed;  // what the search hands over to verify
    o.onRun = [&](const std::vector<InputFrame>& run, float clock, const std::string&) { handed.push_back({run, clock}); };
    const tas::SearchOutcome out = tas::search(S.content, *S.level, o);
    std::printf("  assemblage : %.3f s, %d section(s) du guide, %d voie(s), %d arrivée(s), %d raccordée(s) ; course gardée %.3f s (%s)\n",
                static_cast<double>(out.spliceClock), out.splicedSections, out.spliceWays, out.spliceArrivals, out.spliceJoined,
                static_cast<double>(out.clock), out.bestSource.c_str());
    CHECK(out.guideSources == 1);
    CHECK(out.spliceClock > 0.0f);
    CHECK(out.splicedSections >= 1);
    // Faster than both, and a real run: the normal replay playback gives its time.
    CHECK(out.found && ms(out.clock) < std::min(tasMs, guideMs));
    CHECK(S.verified(out.inputs) == ms(out.clock));
    // Each faster run was handed over at once (to be verified while the search goes on).
    CHECK(!handed.empty());
    bool same = false;
    for (const auto& [run, clock] : handed) same = same || (ms(clock) == ms(out.clock) && S.verified(run) == ms(clock));
    CHECK(same);
}

TEST_CASE("tas guides: a splice slower than the best run is never kept") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    if (ends.size() < 7) return;
    // The guide: the best run with a pause after CP3 — slower from there on, never faster.
    const auto [guide, guideMs] = slowed(S, S.best, ends[2], ends[3]);
    CHECK(guideMs > S.bestMs);
    if (guideMs <= 0) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 20.0;
    o.seed = 11;
    o.seedRun = S.best;
    o.guides = {guide};
    o.windowFirst = o.windowLast = 7;
    o.refine = false;
    int slower = 0;
    o.onRun = [&](const std::vector<InputFrame>&, float clock, const std::string&) { slower += ms(clock) < S.bestMs ? 0 : 1; };
    const tas::SearchOutcome out = tas::search(S.content, *S.level, o);
    CHECK(out.found && ms(out.clock) <= S.bestMs);
    CHECK(slower == 0);  // nothing slower is ever handed over to be written
    CHECK(S.verified(out.inputs) == ms(out.clock));
}

TEST_CASE("tas guides: a run of an older version of the level guides by its route only, never by its inputs") {
    Shipped S;
    if (!S.ok) return;
    const auto ends = tas::sectionEndsOf(S.content, *S.level, RunCategory::AnyPercent, S.best);
    if (ends.size() < 7) return;
    // The player's run, recorded on an older version of the level (another fingerprint).
    const tas::Verified v = tas::recordAndVerify(S.content, *S.level, *S.fp, RunCategory::AnyPercent, S.best, 0);
    CHECK(v.ok);
    if (!v.ok) return;
    ReplayData old = v.replay;
    old.levelHash = std::string(64, 'f');
    const fs::path file = Paths::userDir() / "tas_route_old_version.cprz";
    CHECK(FileUtil::writeBinary(file, old.encode()));
    // Never its inputs: refused as a guide (another version of the level)...
    std::vector<InputFrame> in;
    std::string id, why;
    CHECK(!tas::readGuideReplay(Paths::toUtf8(file), "sylvanox_01", S.fp->hash, in, id, why));
    // ... its route instead: its positions and its checkpoint times.
    std::vector<tas::RouteHint> route;
    std::vector<int64_t> splits;
    CHECK(tas::readRouteReplay(Paths::toUtf8(file), "sylvanox_01", route, splits, why));
    CHECK(route.size() > 1000 && splits.size() == 6);
    std::error_code ec;
    fs::remove(file, ec);
    // The machine's run slower from section 5 on (a pause): the route is ahead there, it is followed.
    // (In the run of 2.100, section 4 forgives a few frames of pause only.)
    const auto [slow, slowMs] = slowed(S, S.best, ends[3], ends[4]);
    CHECK(slowMs > S.bestMs);
    if (slowMs <= 0) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 20.0;
    o.seed = 13;
    o.seedRun = slow;
    o.routeHints = route;
    o.windowFirst = o.windowLast = 5;
    o.refine = false;
    const tas::SearchOutcome out = tas::search(S.content, *S.level, o);
    std::printf("  tracé : %lld états gardés parce qu'ils le suivent ; %.3f s (départ %.3f s)\n", static_cast<long long>(out.routeKept), static_cast<double>(out.clock),
                static_cast<double>(slowMs) / 1000.0);
    CHECK(out.guideSources == 0 && out.splicedSections == 0);  // nothing of its inputs
    CHECK(out.routeKept > 0);
    // Never worse, and a real run: verified by the normal replay playback.
    CHECK(out.found && ms(out.clock) <= slowMs);
    CHECK(S.verified(out.inputs) == ms(out.clock));
}
