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
#include <vector>

#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
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
    if (ends.size() < 3) return;
    tas::SearchOptions o;
    o.category = RunCategory::AnyPercent;
    o.threads = 2;
    o.budgetSeconds = 25.0;
    o.seed = 3;
    o.seedRun = S.best;
    o.refine = false;
    // A run that has left the best run in section 3.
    o.startRun.assign(S.best.begin(), S.best.begin() + static_cast<std::ptrdiff_t>(ends[1]));
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
