// The TAS's mind (tools/tas/Trajectory.h, Memory.h, Director.h; docs/TAS.md « L'IA du TAS »): the
// memory keeps the best maneuver of a configuration (a larger lead replaces it, a smaller one does
// not), on disk and back, a broken file being an empty memory; on the run shipped for sylvanox_01
// (ANY%), the trajectories find the double jump at 7.9 s (more than a second ahead), and the second
// time the memory plays it at once; the shipped run never goes through a solid; every thought is
// worded, and the progress file keeps the mind.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/FileUtil.h"
#include "engine/core/Paths.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "game/tas/TasFile.h"
#include "game/tas/TasProgress.h"
#include "tests/TestFramework.h"
#include "tools/tas/Course.h"
#include "tools/tas/Memory.h"
#include "tools/tas/Output.h"
#include "tools/tas/Sim.h"
#include "tools/tas/Trajectory.h"

using namespace cosmic;
namespace fs = std::filesystem;

namespace {

tas::Configuration config(uint64_t key, int rise) {
    tas::Configuration c;
    c.key = key;
    c.speed = 400;
    c.grounded = true;
    c.airJumps = 1;
    c.rise = rise;
    c.riseAt = 64;
    return c;
}

tas::Maneuver doubleJump(int16_t hold) {
    tas::Maneuver m;
    m.pressAt = 0;
    m.hold = hold;
    m.gap = 19;
    m.hold2 = 44;
    return m;
}

/// No text left with a format's mark, none empty.
bool worded(const std::string& s) {
    if (s.empty() || s.find("{0}") != std::string::npos) return false;
    for (const char* mark : {"%s", "%d", "%.", "%l", "%z", "%f"})
        if (s.find(mark) != std::string::npos) return false;
    return true;
}

}  // namespace

TEST_CASE("tas mind: the memory keeps the best maneuver of a configuration, on disk and back") {
    const fs::path dir = fs::temp_directory_path() / ("cosmic_tas_mind_" + std::to_string(reinterpret_cast<uintptr_t>(&dir) & 0xffffff));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "memory.json";
    {
        tas::TrajectoryMemory mem(file);
        mem.load();  // no file yet: empty
        CHECK(mem.size() == 0);
        mem.remember(config(0x1234, 96), doubleJump(13), 148, "sylvanox_01");
        CHECK(mem.size() == 1);
        CHECK(mem.find(0x1234).has_value() && mem.find(0x1234)->leadFrames == 148 && mem.find(0x1234)->maneuver == doubleJump(13));
        mem.remember(config(0x1234, 96), doubleJump(10), 100, "sylvanox_02");  // smaller lead: kept as it was
        CHECK(mem.find(0x1234)->maneuver == doubleJump(13));
        mem.remember(config(0x1234, 96), doubleJump(10), 160, "sylvanox_02");  // larger: replaces it
        CHECK(mem.find(0x1234)->maneuver == doubleJump(10) && mem.find(0x1234)->leadFrames == 160);
        CHECK(mem.find(0x1234)->level == "sylvanox_01");  // where it was found first
        mem.remember(config(0x99, 48), doubleJump(20), 12, "sylvanox_03");
        mem.used(0x1234, 50);
        mem.used(0x1234, 70);
        CHECK(mem.find(0x1234)->uses == 2 && mem.find(0x1234)->gainMs == 120);
        CHECK(!mem.find(0x4242).has_value());
        const auto top = mem.top(1);
        CHECK(top.size() == 1 && top[0].config.key == 0x1234);  // the one that gained the most first
    }
    {
        tas::TrajectoryMemory back(file);
        back.load();
        CHECK(back.size() == 2);
        const auto r = back.find(0x1234);
        CHECK(r.has_value());
        if (r) {
            CHECK(r->maneuver == doubleJump(10) && r->leadFrames == 160 && r->uses == 2 && r->gainMs == 120);
            CHECK(r->config.rise == 96 && r->config.speed == 400 && r->config.airJumps == 1 && r->level == "sylvanox_01");
        }
    }
    // A broken file, another format: an empty memory (never a crash).
    for (const char* text : {"{oops", "{\"format\":\"other\",\"entries\":[]}", "[1,2,3]"}) {
        FileUtil::writeTextAtomic(file, text);
        tas::TrajectoryMemory broken(file);
        broken.load();
        CHECK(broken.size() == 0);
    }
    fs::remove_all(dir, ec);
}

TEST_CASE("tas mind: the trajectories find the double jump at 7.9 s, the memory plays it again at once") {
    ContentDatabase content;
    content.reload();
    LevelFingerprints fps;
    auto level = content.loadLevel("sylvanox_01");
    if (!level) return;
    std::vector<InputFrame> best;
    std::string why;
    if (!tas::readSeedReplay(Paths::toUtf8(tasRoot() / "sylvanox_01" / "ANY_PERCENT.cprz"), "sylvanox_01", best, why)) return;
    tas::Course course(*level, content, RunCategory::AnyPercent);
    std::vector<std::unique_ptr<tas::Sim>> own;
    std::vector<tas::Sim*> sims;
    for (int i = 0; i < 2; ++i) {
        own.push_back(std::make_unique<tas::Sim>(content, *level, course, RunCategory::AnyPercent));
        sims.push_back(own.back().get());
    }
    // The shipped run never goes through a solid (the check every kept run passes).
    CHECK(sims[0]->clipAt(best) < 0.0f);
    const float bestClock = sims[0]->study(best).clock;
    CHECK(bestClock > 80.0f);

    tas::TrajectoryMemory mem;  // kept in memory only
    std::mutex m;
    std::vector<TasThought> thoughts;
    size_t leads = 0;
    auto slice = [&] {
        tas::Slice s;
        s.best = best;
        s.bestClock = bestClock;
        s.until = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        s.threads = static_cast<int>(sims.size());
        s.seed = 3;
        s.offer = [](const std::vector<InputFrame>&, float, const std::string&) {};
        s.onLead = [&](const std::vector<InputFrame>& prefix, size_t through, float lead, const std::string&) {
            std::lock_guard<std::mutex> l(m);
            if (!prefix.empty() && through >= 1 && lead > 1.0f) ++leads;
        };
        s.think = [&](const tas::Thought& t) {
            std::lock_guard<std::mutex> l(m);
            TasThought out;
            out.kind = t.kind, out.code = t.code, out.n = t.n, out.s = t.s;
            thoughts.push_back(std::move(out));
        };
        return s;
    };
    tas::TrajectoryOptimizer::Options o;
    o.fromClock = 7.6f;
    o.toClock = 8.0f;
    o.memory = &mem;
    o.level = "sylvanox_01";

    tas::TrajectoryOptimizer first(o);
    const tas::TrajectoryStats a = first.run(sims, slice());
    CHECK(first.exhausted());
    CHECK(!first.spots().empty());
    if (first.spots().empty()) return;
    const tas::TrajectorySpot sp = first.spots().front();
    std::printf("  trajectoires : %.3f s, %lld variations, %d images d'avance (%s)\n", static_cast<double>(sp.clock), static_cast<long long>(sp.tried), sp.lead,
                sp.best.describe().c_str());
    CHECK(sp.clock > 7.8f && sp.clock < 8.0f);
    CHECK(sp.joined && sp.lead >= 120);         // more than a second ahead where it re-joins the run
    CHECK(sp.best.secondAt() > sp.best.pressAt);  // a double jump
    CHECK(sp.leadThrough >= 1 && sp.leadSeconds > 1.0f);  // still ahead at CP1
    CHECK(leads >= 1);                           // offered to a branch
    CHECK(a.learned >= 1 && mem.size() >= 1);    // remembered

    // The same place again, the memory helping: played at once, the same lead, far fewer attempts.
    tas::TrajectoryOptimizer second(o);
    const tas::TrajectoryStats b = second.run(sims, slice());
    CHECK(b.recalled >= 1);
    CHECK(!second.spots().empty());
    if (!second.spots().empty()) {
        CHECK(second.spots().front().lead == sp.lead);
        CHECK(second.spots().front().tried * 10 < sp.tried);
        std::printf("  de mémoire : %lld variation(s) au lieu de %lld\n", static_cast<long long>(second.spots().front().tried), static_cast<long long>(sp.tried));
    }
    // Every thought said is worded in the player's language, none left with a format's mark.
    bool sawTrajectory = false, sawRecall = false;
    for (const TasThought& t : thoughts) {
        CHECK(worded(tasThoughtText(t)));
        sawTrajectory = sawTrajectory || t.code == "spot" || t.code == "lead" || t.code == "trajectory";
        sawRecall = sawRecall || t.code == "recall";
    }
    CHECK(sawTrajectory && sawRecall);
}

TEST_CASE("tas mind: every thought is worded, the progress file keeps the mind") {
    TasProgress p;
    p.levelId = "sylvanox_01";
    p.category = RunCategory::AnyPercent;
    p.phase = TasPhase::Searching;
    p.director = true;
    const std::vector<std::pair<std::string, std::vector<double>>> codes = {
        {"spot", {1236, 7.908, 0.5}},
        {"no_gain", {1236, 1412}},
        {"places_done", {83.9}},
        {"recall", {1236, 96, 400}},
        {"recall_miss", {1236, 96, 400}},
        {"learn", {96, 64, 400, 1.233}},
        {"trajectory", {1.233, 1236, 0, 13, 32, 44}},
        {"trajectory", {1.233, 1236, 0, 13, 32, -1}},
        {"trajectory", {0.5, 1236, 2, -1, -1, -1}},
        {"trajectory", {0.5, 1236, 2, 20, -1, -1}},
        {"lead", {1.225, 1, 1236}},
        {"branch", {1, 1.225}},
        {"plan_places_left", {3, 1, 1, -1}},
        {"plan_places_left", {1, 1, 1, -1}},
        {"plan_places_left", {4, 0, 1, -1}},
        {"plan_places_left", {1, 0, 1, -1}},
        {"plan_after_gain", {4, 1, -1}},
        {"plan_bandit", {42, 3, 1, -1}},
        {"verified", {83.4}},
        {"clip", {49.567, 76.85}},
    };
    int64_t at = 0;
    for (const auto& [code, n] : codes) {
        TasThought t;
        t.atMs = at += 1000;
        t.kind = "observe";
        t.code = code;
        t.n = n;
        t.s = code == "plan_bandit" ? "trajectory:3,explore:1" : "";
        const std::string text = tasThoughtText(t);
        CHECK(worded(text));
        if (!worded(text)) std::printf("  pensée %s : « %s »\n", code.c_str(), text.c_str());
        p.thoughts.push_back(t);
    }
    p.workers = {{"trajectory", 2, -1, 1200}, {"explore", 0, 3, 40}};
    for (const TasWorker& w : p.workers) CHECK(worded(tasWorkerText(w)));
    TasMemoryEntry e;
    e.rise = 96, e.riseAt = 64, e.speed = 400, e.powers = "dash", e.leadFrames = 148, e.uses = 2, e.gainMs = 1233, e.level = "sylvanox_01";
    p.memoryTop = {e};
    p.memorySize = 12;
    CHECK(worded(tasMemoryText(e)));
    const auto back = TasProgress::fromJson(p.toJson());
    CHECK(back.has_value());
    if (back) {
        CHECK(back->director && back->memorySize == 12 && back->memoryTop == p.memoryTop && back->workers == p.workers);
        CHECK(back->thoughts.size() == p.thoughts.size());
        if (back->thoughts.size() == p.thoughts.size())
            for (size_t i = 0; i < p.thoughts.size(); ++i) CHECK(back->thoughts[i].code == p.thoughts[i].code && back->thoughts[i].atMs == p.thoughts[i].atMs);
    }
    // Bounded: never more than the last kMax thoughts kept.
    for (int i = 0; i < 100; ++i) p.thoughts.push_back(p.thoughts.front());
    const auto bounded = TasProgress::fromJson(p.toJson());
    CHECK(bounded && bounded->thoughts.size() <= TasThought::kMax);
}
