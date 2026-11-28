// The phase realignment (tools/tas/Phase.h, docs/TAS.md « Le réalignement de phase »): an arrival
// earlier than the best run no longer plays the best run's next sections (what moves with time is
// not where it was for them); a short wait puts the world back in phase and keeps most of the lead.
// A small level built here: an elevator over a deadly shaft, which Nova must ride up to the exit.
// The branch of a training (tools/tas/Branch.h): its prefix kept, its windows after it, its own best.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/content/ContentDatabase.h"
#include "game/levels/LevelData.h"
#include "tests/TestFramework.h"
#include "tools/tas/Branch.h"
#include "tools/tas/Course.h"
#include "tools/tas/Phase.h"
#include "tools/tas/Search.h"
#include "tools/tas/Sim.h"

using namespace cosmic;

namespace {

/// The floor, a checkpoint on it, then a shaft (death below) with an elevator against a wall, and
/// the exit on the ledge at the top: the elevator must be at the bottom when Nova gets there.
LevelData elevatorLevel() {
    LevelData L = LevelData::makeEmpty("tas_ascenseur", "Ascenseur", "sylvanox");
    L.objects.clear();
    L.bounds = {0, 0, 1800, 1100};
    L.killY = 1000.0f;
    auto add = [&](const std::string& id, const std::string& type, Rect r, Json props = Json::object()) {
        LevelObject o;
        o.id = id;
        o.type = type;
        o.layer = type == "solid" ? "terrain" : "gameplay";
        o.rect = r;
        o.props = std::move(props);
        L.objects.push_back(std::move(o));
    };
    add("sol", "solid", {0, 800, 1000, 200});
    add("bord", "solid", {0, 0, 32, 800});
    add("depart", "player_start", {160, 778, 10, 22});
    add("cp", "checkpoint", {560, 752, 24, 48}, {{"label", "Pied du puits"}});
    add("ascenseur", "moving_platform", {1000, 800, 48, 16},
        {{"dx", 0}, {"dy", -300}, {"speed", 150}, {"wait", 0.4}, {"phase", 0.0}, {"active", true}, {"triggered", false}, {"oneway", false}});
    add("mur", "solid", {1048, 516, 32, 600});
    add("corniche", "solid", {1048, 500, 700, 16});
    add("sortie", "exit", {1400, 436, 40, 64}, {{"locked", false}});
    return L;
}

std::vector<InputFrame> idleThenRight(int idle, int total) {
    std::vector<InputFrame> run(static_cast<size_t>(total));
    for (size_t i = static_cast<size_t>(std::max(0, idle)); i < run.size(); ++i) run[i].held = tas::keyBit(Action::MoveRight), run[i].moveX = 127;
    tas::repressRun(run, 0, run.size());
    return run;
}

}  // namespace

TEST_CASE("tas phase: the periods of the level's moving platforms and lasers") {
    LevelData L = elevatorLevel();
    LevelObject laser;
    laser.id = "laser";
    laser.type = "laser";
    laser.rect = {300, 600, 16, 200};
    laser.props = {{"onTime", 1.2}, {"offTime", 1.4}, {"active", true}};
    L.objects.push_back(laser);
    const auto e = tas::periodicElements(L);
    CHECK(e.size() == 2);
    if (e.size() != 2) return;
    CHECK(e[0].id == "ascenseur" && std::abs(e[0].period - 2.0f * (300.0f / 150.0f + 0.4f)) < 1e-4f);  // there and back, a wait at each end
    CHECK(e[0].sweep.y <= 500.0f && e[0].sweep.bottom() >= 816.0f);
    CHECK(e[1].id == "laser" && std::abs(e[1].period - 2.6f) < 1e-4f);
}

TEST_CASE("tas phase: an early arrival keeps its lead through a moving platform by waiting for its phase") {
    ContentDatabase content;
    content.reload();
    const LevelData L = elevatorLevel();
    tas::Course course(L, content, RunCategory::AnyPercent);
    CHECK(course.sectionEnds().size() == 2);
    if (course.sectionEnds().size() != 2) return;
    tas::Sim sim(content, L, course, RunCategory::AnyPercent);
    // The best run: Nova waits at the start, long enough to meet the elevator at the bottom after
    // more than one of its periods (4.8 s), then runs right — the first such wait from 6 s on.
    tas::RunStudy best;
    int wait = -1;
    for (int w = 720; w < 720 + 700 && wait < 0; w += 2) {
        tas::RunStudy g = sim.study(idleThenRight(w, w + 1800));
        if (g.finishes() && g.ends.size() == 2) best = std::move(g), wait = w;
    }
    CHECK(wait > 0);
    if (wait < 0) return;
    best.frames.resize(best.exitAt);
    // The early arrival: Nova runs at once, at the checkpoint `wait` frames sooner; the best run's
    // frames from there, as they are, fall into the shaft (the elevator is not there for them).
    const tas::RunStudy early = sim.study(idleThenRight(0, 1800));
    CHECK(!early.ends.empty());
    if (early.ends.empty()) return;
    const std::vector<InputFrame> prefix(early.frames.begin(), early.frames.begin() + static_cast<std::ptrdiff_t>(early.ends[0]));
    const float lead = best.splitAt(0) - early.splitAt(0);
    CHECK(lead > 5.9f);
    {
        auto at = sim.take();
        sim.arena().restore(sim.origin());
        for (const InputFrame& f : prefix) sim.arena().step(f);
        sim.arena().save(*at);
        CHECK(sim.playToExit(*at, best.frames, best.ends[0], 1e9f) < 0.0f);  // out of phase: it does not finish
        sim.give(std::move(at));
    }
    const auto elements = tas::periodicElements(L);
    CHECK(std::abs(tas::periodMet(best, 1, elements) - 4.8f) < 1e-3f);
    CHECK(tas::periodMet(best, 0, elements) == 0.0f);  // the first section meets nothing that moves
    tas::PhaseOptions po;
    tas::PhaseRun first;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const tas::PhaseRun r = tas::realign(sim, best, elements, prefix, 1, po);
        CHECK(r.found);
        if (!r.found) return;
        // A wait shorter than the lead (the elevator's period sooner), the run replayed from the load.
        CHECK(r.sections.size() == 1);
        CHECK(r.sections[0].wait > 0 && r.sections[0].wait / 120.0f < lead - 4.0f);
        CHECK(r.clock < best.clock - 4.0f);
        const tas::RunStudy again = sim.study(r.run);
        CHECK(again.finishes() && std::abs(again.clock - r.clock) < 1e-4f);
        CHECK(std::equal(prefix.begin(), prefix.end(), r.run.begin()));  // the early prefix kept as it is
        if (attempt == 0) first = r;
        else CHECK(r.run == first.run);  // deterministic
        std::printf("  ascenseur : la meilleure course %.3f s (attente %.2f s au départ), l'arrivée en avance de %.2f s réalignée : %.3f s (attente %.3f s), %lld essais\n",
                    static_cast<double>(best.clock), wait / 120.0, static_cast<double>(lead), static_cast<double>(r.clock), r.sections[0].wait / 120.0,
                    static_cast<long long>(r.tried));
    }
    // Without a lead on the best run (its own arrival), nothing is lost: the best run's own time.
    const std::vector<InputFrame> own(best.frames.begin(), best.frames.begin() + static_cast<std::ptrdiff_t>(best.ends[0]));
    const tas::PhaseRun same = tas::realign(sim, best, elements, own, 1, po);
    CHECK(same.found && std::abs(same.clock - best.clock) < 1e-4f && same.sections[0].wait == 0);
}

TEST_CASE("tas phase: several sources, each section from the one that gets there first in phase") {
    ContentDatabase content;
    content.reload();
    const LevelData L = elevatorLevel();
    tas::Course course(L, content, RunCategory::AnyPercent);
    tas::Sim sim(content, L, course, RunCategory::AnyPercent);
    // Two runs that finish: a slow one, and a faster one; the early prefix realigned on both keeps
    // the faster finish (a source that cannot be put in phase is only skipped).
    std::vector<tas::RunStudy> runs;
    for (int w = 0; w < 1500 && runs.size() < 2; w += 2) {
        tas::RunStudy g = sim.study(idleThenRight(w, w + 1800));
        if (g.finishes() && g.ends.size() == 2 && (runs.empty() || g.splitAt(0) > runs.back().splitAt(0) + 2.0f)) {
            g.frames.resize(g.exitAt);
            runs.push_back(std::move(g));
        }
    }
    CHECK(runs.size() == 2);
    if (runs.size() != 2) return;
    const tas::RunStudy early = sim.study(idleThenRight(0, 1800));
    const std::vector<InputFrame> prefix(early.frames.begin(), early.frames.begin() + static_cast<std::ptrdiff_t>(early.ends[0]));
    const auto elements = tas::periodicElements(L);
    tas::PhaseOptions po;
    const tas::PhaseRun one = tas::realign(sim, std::vector<const tas::RunStudy*>{&runs[1]}, elements, prefix, 1, po);
    const tas::PhaseRun both = tas::realign(sim, std::vector<const tas::RunStudy*>{&runs[1], &runs[0]}, elements, prefix, 1, po);
    CHECK(one.found && both.found);
    if (!one.found || !both.found) return;
    CHECK(both.clock <= one.clock + 1e-4f);
    CHECK(both.clock <= runs[0].clock + 1e-4f);
    const tas::RunStudy again = sim.study(both.run);
    CHECK(again.finishes() && std::abs(again.clock - both.clock) < 1e-4f);
}

TEST_CASE("tas phase: the search keeps its early arrivals that do not re-join, and realigned they carry their lead") {
    ContentDatabase content;
    content.reload();
    const LevelData L = elevatorLevel();
    tas::Course course(L, content, RunCategory::AnyPercent);
    tas::Sim sim(content, L, course, RunCategory::AnyPercent);
    tas::RunStudy best;
    for (int w = 720; w < 720 + 700 && !best.finishes(); w += 2) {
        tas::RunStudy g = sim.study(idleThenRight(w, w + 1800));
        if (g.finishes() && g.ends.size() == 2) best = std::move(g);
    }
    CHECK(best.finishes());
    if (!best.finishes()) return;
    best.frames.resize(best.exitAt);
    // A round on the first section: the beam gets to the checkpoint seconds before the best run, and
    // the best run's next frames do not follow from there (the elevator is not in phase).
    tas::SearchOptions opt;
    opt.category = RunCategory::AnyPercent;
    opt.threads = 1;
    opt.budgetSeconds = 4.0;
    opt.seedRun = best.frames;
    opt.windowFirst = opt.windowLast = 1;
    opt.refine = false;
    const tas::SearchOutcome out = tas::search(content, L, opt);
    const tas::SearchOutcome::EarlyArrival* e = nullptr;
    for (const auto& x : out.early)
        if (x.sections == 1 && (!e || x.lead > e->lead)) e = &x;
    CHECK(e != nullptr);
    if (!e) return;
    CHECK(e->lead > 4.0f && std::abs(e->clock - (best.splitAt(0) - e->lead)) < 1e-3f);
    CHECK(out.early.size() <= 2);  // the earliest two of the section
    CHECK(!out.found || out.clock + 1e-4f >= best.clock - 1e-3f);  // the round alone keeps no faster run
    const tas::PhaseRun r = tas::realign(sim, best, tas::periodicElements(L), e->run, 1, tas::PhaseOptions{});
    CHECK(r.found && r.clock < best.clock - 3.0f);
    const tas::RunStudy again = sim.study(r.run);
    CHECK(again.finishes() && std::abs(again.clock - r.clock) < 1e-4f);
}

TEST_CASE("tas branch: the prefix kept, the windows after it, the branch's own best kept even when slower") {
    std::vector<InputFrame> prefix(10);
    for (size_t i = 0; i < prefix.size(); ++i) prefix[i].held = static_cast<uint32_t>(i);
    tas::BranchPlan b(prefix, 2, 7, 0.5);
    CHECK(b.active() && b.through() == 2);
    std::vector<InputFrame> run = prefix, other = prefix;
    run.resize(30);
    other[3].held = 99;
    other.resize(30);
    CHECK(b.owns(run) && !b.owns(other) && !b.owns(std::vector<InputFrame>(prefix.begin(), prefix.begin() + 5)));
    // Half the rounds, spread evenly; none or all at the extremes.
    int branchRounds = 0;
    for (int r = 1; r <= 10; ++r) branchRounds += b.roundIsBranch(r) ? 1 : 0;
    CHECK(branchRounds == 5);
    CHECK(!tas::BranchPlan(prefix, 2, 7, 0.0).roundIsBranch(3) && tas::BranchPlan(prefix, 2, 7, 1.0).roundIsBranch(3));
    // Its windows: only after the prefix, and over the rounds every section after it.
    std::vector<int> seen(8, 0);
    for (int r = 1; r <= 12; ++r) {
        const auto [first, last] = b.window(r);
        CHECK(first >= 3 && last >= first && last <= 7);
        for (int s = first; s <= last && s >= 0 && s < 8; ++s) seen[static_cast<size_t>(s)] = 1;
        b.done(r, first, last, 0);
    }
    for (int s = 3; s <= 7; ++s) CHECK(seen[static_cast<size_t>(s)] == 1);
    CHECK(seen[1] == 0 && seen[2] == 0);
    // Its own best: the first run kept whatever its time, then only a faster one; never a run off the prefix.
    CHECK(b.offer(run, 95000) && b.bestMs() == 95000);
    std::vector<InputFrame> slower = run;
    slower[20].held = 7;
    CHECK(!b.offer(slower, 96000) && b.bestMs() == 95000);
    CHECK(b.offer(slower, 94000) && b.best() == slower);
    CHECK(!b.offer(other, 1000) && b.bestMs() == 94000);
    // A prefix that leaves no section after it: no branch.
    CHECK(!tas::BranchPlan(prefix, 7, 7, 0.5).active() && !tas::BranchPlan({}, 2, 7, 0.5).active());
}

TEST_CASE("tas phase: the splice finds the leads worth realigning, the same on one thread or two") {
    ContentDatabase content;
    content.reload();
    const LevelData L = elevatorLevel();
    tas::Course course(L, content, RunCategory::AnyPercent);
    tas::Sim a(content, L, course, RunCategory::AnyPercent), b(content, L, course, RunCategory::AnyPercent);
    tas::RunStudy best;
    for (int w = 720; w < 720 + 700 && !best.finishes(); w += 2) {
        tas::RunStudy g = a.study(idleThenRight(w, w + 1800));
        if (g.finishes() && g.ends.size() == 2) best = std::move(g);
    }
    CHECK(best.finishes());
    if (!best.finishes()) return;
    best.frames.resize(best.exitAt);
    // A run early at the checkpoint that does not finish: its prefix plus the best run's last section
    // could beat the best run (the sum of the best segments), so it is a lead; the best run's own is not.
    tas::RunStudy early = a.study(idleThenRight(0, 1800));
    const std::vector<const tas::RunStudy*> sources = {&best, &early};
    const std::vector<tas::PhaseLead> leads = tas::phaseLeads(sources, course.sectionEnds().size(), 0.1f);
    CHECK(leads.size() == 1 && leads[0].source == 1 && leads[0].through == 1 && leads[0].lead > 5.9f);
    if (leads.empty()) return;
    const auto elements = tas::periodicElements(L);
    const tas::PhaseSplice one = tas::phaseSplice(a, sources, elements, leads, tas::PhaseOptions{}, {});
    const tas::PhaseSplice two = tas::phaseSplice(std::vector<tas::Sim*>{&a, &b}, sources, elements, leads, tas::PhaseOptions{}, {});
    CHECK(one.run.found && two.run.found && one.run.run == two.run.run && one.run.clock == two.run.clock);
    CHECK(one.run.found && one.run.clock < best.clock - 3.0f && one.leads == 1);
}

TEST_CASE("tas phase: a run's power keys follow its powers into a world that carries them in other slots") {
    const tas::SlotLayout from = {"dash", "wave", "impulse", "", "", "", "", ""};
    const tas::SlotLayout to = {"dash", "wave", "shield", "impulse", "", "", "", ""};
    const uint32_t s1 = tas::keyBit(Action::PowerSlot1), s3 = tas::keyBit(Action::PowerSlot3), s4 = tas::keyBit(Action::PowerSlot4);
    const uint32_t right = tas::keyBit(Action::MoveRight);
    InputFrame f;
    f.held = right | s1 | s3;
    f.pressed = s3;
    f.choice = 3;
    // The impulse of slot 3 is in slot 4 here; the dash stays in slot 1; the other keys untouched.
    const InputFrame g = tas::remapSlots(f, from, to, right | s1);
    CHECK(g.held == (right | s1 | s4));
    CHECK(g.pressed == s4 && g.released == 0);
    CHECK(g.choice == 4);
    // Let go on the next frame: from slot 4.
    InputFrame r;
    r.held = right;
    r.released = s1 | s3;
    const InputFrame h = tas::remapSlots(r, from, to, g.held);
    CHECK(h.held == right && h.released == (s1 | s4) && h.pressed == 0);
    // A power this world does not carry: its key dropped.
    const tas::SlotLayout none = {"dash", "wave", "", "", "", "", "", ""};
    CHECK(tas::remapSlots(f, from, none, right | s1).held == (right | s1));
    // The same layout: the frame as it is.
    CHECK(tas::remapSlots(f, from, from, 0) == f);
}
