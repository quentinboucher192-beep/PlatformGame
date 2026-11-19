// 2.54 — the player's statistics (game/records/PlayStats.h): the calculations of the
// « Statistiques » screen, the file (format, caps, damaged file, worker thread), and the
// recorder, which reads a run without ever changing it (same digests with or without it).
// 2.55: two copies merged (mergePlayStats) without counting anything twice, under the caps.

#include <cstdio>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/core/Random.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/LevelFingerprint.h"
#include "game/online/Replay.h"
#include "game/records/PlayStats.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::playstats;

namespace {

fs::path tempDir() {
    fs::path d = fs::temp_directory_path() / ("cosmic_stats_" + makeUuidV4().substr(0, 8));
    fs::create_directories(d);
    return d;
}

StatsCounters dayCounters(double minutes, int deaths, int threats, int dodged, int attacks, int hits) {
    StatsCounters c;
    c.playSec = minutes * 60.0;
    c.deaths = deaths;
    c.threats = threats;
    c.dodged = dodged;
    c.attacks = attacks;
    c.hits = hits;
    return c;
}

/// Years of play with every cap passed (each name prefixed by `tag`: another computer's).
PlayStatsData atCap(int today, const std::string& tag) {
    PlayStatsData full;
    const char* powers[] = {"attack", "wave", "charge_shot", "plasma_bomb", "boomerang", "boomerang_charge", "ground_pound", "dash"};
    for (int k = 0; k < 365 * 12; ++k) {
        StatsCounters& c = full.at(today - k);
        c = dayCounters(355.5, 999, 9999, 8888, 9999, 7777);
        c.trainSec = 3599.9;
        c.stars = 1234;
        for (auto& h : c.hurt) h = 999;
        for (const char* p : powers) c.powers[p] = {9999, 8888};
    }
    for (size_t l = 0; l < kMaxLevels + 10; ++l) {
        LevelSlices& s = full.levels[tag + "community_level_" + std::to_string(l) + "_abcdefghij"];
        s.name = "Un niveau de la communauté au nom assez long " + std::to_string(l);
        s.lastDay = today;
        s.stamp = today;
        for (int i = 0; i < kSlices; ++i) {
            s.passes[static_cast<size_t>(i)] = 99999;
            s.deaths[static_cast<size_t>(i)] = 9999;
            s.wPasses[static_cast<size_t>(i)] = 1234.56f;
            s.wDeaths[static_cast<size_t>(i)] = 123.45f;
        }
    }
    for (size_t bi = 0; bi < kMaxBosses + 5; ++bi) {
        BossHistory& b = full.bosses[tag + "boss_" + std::to_string(bi)];
        b.name = "Le boss numéro " + std::to_string(bi);
        b.phases = 8;
        b.reached.assign(8, 9999);
        b.defeats.assign(8, 999);
        b.attempts = 99999;
        b.lastSeq = static_cast<int>(bi);
        for (int a = 0; a < 20; ++a) b.fatal["Une attaque fatale au nom très long numéro " + std::to_string(a)] = a;
        for (int a = 0; a < 260; ++a)
            b.recent.push_back({today - 300 + a, 0.987f, 7, false, "Une attaque fatale au nom très long numéro " + std::to_string(a % 12)});
    }
    for (size_t li = 0; li < kMaxRunLevels + 4; ++li) {
        LevelRuns& r = full.runs[tag + "classic_" + std::to_string(li)];
        r.name = "Niveau classique " + std::to_string(li);
        r.version = std::string(64, 'f');
        r.attempts = r.finished = 99999;
        r.pb = 999.99f;
        r.lastDay = today;
        std::string prev;
        for (int s = 0; s < 18; ++s) {
            std::string to = "checkpoint_" + std::to_string(s);
            r.pbRoute.push_back(to);
            SegmentRec& seg = r.segmentAt(prev, to);
            for (int t = 0; t < 60; ++t) seg.times.push_back(123.45f);
            seg.best = 99.99f;
            prev = to;
        }
        for (int f = 0; f < 50; ++f) r.finishes.push_back({today - 50 + f, 999.99f});
        for (int h = 0; h < 50; ++h) r.history.push_back({today - 50 + h, 999.99f, 888.88f});
    }
    return full;
}

/// Plausible play (exactly the determinism test's): mostly running right, jumps, attacks, dashes.
std::vector<InputFrame> scripted(uint64_t seed, int ticks) {
    Random r(seed);
    std::vector<InputFrame> out;
    InputFrame prev, cur;
    int hold = 0;
    for (int i = 0; i < ticks; ++i) {
        if (hold-- <= 0) {
            cur = InputFrame{};
            float k = r.unit();
            int dir = k < 0.75f ? 1 : (k < 0.88f ? -1 : 0);
            if (dir > 0) cur.held |= 1u << static_cast<int>(Action::MoveRight);
            if (dir < 0) cur.held |= 1u << static_cast<int>(Action::MoveLeft);
            cur.moveX = static_cast<int8_t>(dir * 127);
            if (r.chance(0.45f)) cur.held |= 1u << static_cast<int>(Action::Jump);
            if (r.chance(0.2f)) cur.held |= 1u << static_cast<int>(Action::Attack);
            if (r.chance(0.08f)) cur.held |= 1u << static_cast<int>(Action::Dash);
            if (r.chance(0.05f)) {
                cur.held |= 1u << static_cast<int>(Action::MoveDown);
                cur.moveY = 127;
            }
            hold = r.rangeInt(4, 40);
        }
        InputFrame f = cur;
        f.pressed = static_cast<uint16_t>(f.held & ~prev.held);
        f.released = static_cast<uint16_t>(prev.held & ~f.held);
        out.push_back(f);
        prev = f;
    }
    return out;
}

struct Sim {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    std::optional<LevelData> level;
    std::unique_ptr<GameWorld> w;
    explicit Sim(const std::string& levelId) {
        content.reload();
        res.init(nullptr, &audio);
        level = content.loadLevel(levelId);
        CHECK(level.has_value());
        WorldServices s{res, audio, content, nullptr, 0.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Play);
        w->load(*level);
        w->seedGameplay(levelSeed(levelId));
    }
    /// The digests every 60 steps; `rec` stepped after each step like the game scene does.
    std::vector<uint64_t> run(const std::vector<InputFrame>& inputs, PlayStatsRecorder* rec = nullptr) {
        std::vector<uint64_t> checks;
        for (size_t i = 0; i < inputs.size(); ++i) {
            InputFrame f = inputs[i];
            if (w->cinematic().active() && w->cinematic().skippable()) f.flags |= InputFrame::SkipCinematic;
            stepWorld(*w, f, kSimDt);
            if (rec) rec->step(*w);
            if ((i + 1) % 60 == 0) checks.push_back(simulationStateHash(*w));
        }
        return checks;
    }
};

}  // namespace

TEST_CASE("play stats: calendar days") {
    CHECK(daysFromCivil(1970, 1, 1) == 0);
    CHECK(daysFromCivil(2026, 11, 16) - daysFromCivil(2026, 11, 15) == 1);
    CHECK(daysFromCivil(2024, 3, 1) - daysFromCivil(2024, 2, 28) == 2);  // leap year
    for (int d : {0, 365, 19000, 20408, 21000}) {
        int y, m, dd;
        civilFromDays(d, y, m, dd);
        CHECK(daysFromCivil(y, m, dd) == d);
    }
}

TEST_CASE("play stats: rates, periods and the comparison with the previous period") {
    PlayStatsData d;
    const int today = daysFromCivil(2026, 11, 16);
    // The previous 7 days: 60 % dodged, 50 % hit, 10 deaths in 2 h.
    d.at(today - 10).add(dayCounters(120, 10, 100, 60, 200, 100));
    // This week: 75 % dodged, 40 % hit, 3 deaths in 1 h, over two days played.
    d.at(today - 3).add(dayCounters(30, 2, 40, 30, 50, 20));
    d.at(today).add(dayCounters(30, 1, 40, 30, 50, 20));
    PeriodTotals cur = periodTotals(d, today - 6, today), prev = periodTotals(d, today - 13, today - 7);
    CHECK(cur.daysPlayed == 2);
    CHECK_NEAR(*cur.dodgeRate(), 0.75, 1e-9);
    CHECK_NEAR(*cur.accuracy(), 0.40, 1e-9);
    CHECK_NEAR(*cur.deathsPerHour(), 3.0, 1e-9);
    CHECK_NEAR(*prev.deathsPerHour(), 5.0, 1e-9);
    Comparison dodge = comparePeriods(cur.dodgeRate(), prev.dodgeRate(), true);
    CHECK(dodge.available && dodge.progress);
    CHECK_NEAR(dodge.delta, 0.15, 1e-9);
    Comparison acc = comparePeriods(cur.accuracy(), prev.accuracy(), true);
    CHECK(acc.available && !acc.progress);  // en recul
    Comparison dph = comparePeriods(cur.deathsPerHour(), prev.deathsPerHour(), false);
    CHECK(dph.available && dph.progress);  // fewer deaths per hour: en progrès
    // Nothing before: no comparison; nothing at all: no rate.
    CHECK(!comparePeriods(cur.dodgeRate(), periodTotals(d, 0, 10).dodgeRate(), true).available);
    CHECK(!periodTotals(d, 0, 10).deathsPerHour());
}

TEST_CASE("play stats: daily values and the pooled average of the last 7 days played") {
    PlayStatsData d;
    const int t0 = daysFromCivil(2026, 10, 1);
    // Ten days played, every other day: day k dodges k of 10.
    for (int k = 1; k <= 10; ++k) d.at(t0 + 2 * k).add(dayCounters(10, 0, 10, k, 0, 0));
    d.at(t0 + 1).add(dayCounters(10, 0, 0, 0, 0, 0));  // played, no threat: no point
    auto s = dailySeries(d, DayMetric::Dodge, t0 + 14, t0 + 40, 7);
    CHECK(s.size() == 4);  // days 7..10 of the series are in the window
    CHECK(s.front().day == t0 + 14);
    CHECK_NEAR(s.front().value, 0.7, 1e-9);
    // Its average: days 1..7 (they began before the period: the curve starts with a real average).
    CHECK_NEAR(s.front().rolling, (1 + 2 + 3 + 4 + 5 + 6 + 7) / 70.0, 1e-9);
    CHECK_NEAR(s.back().rolling, (4 + 5 + 6 + 7 + 8 + 9 + 10) / 70.0, 1e-9);
    // Pooled, not averaged: a big day weighs more.
    PlayStatsData p;
    p.at(t0).add(dayCounters(10, 0, 100, 100, 0, 0));
    p.at(t0 + 1).add(dayCounters(10, 0, 10, 0, 0, 0));
    auto ps = dailySeries(p, DayMetric::Dodge, t0, t0 + 1, 7);
    CHECK(ps.size() == 2);
    CHECK_NEAR(ps[1].value, 0.0, 1e-9);
    CHECK_NEAR(ps[1].rolling, 100.0 / 110.0, 1e-9);
    // Deaths per hour: a day under a minute has no value.
    PlayStatsData q;
    q.at(t0).add(dayCounters(0.5, 3, 0, 0, 0, 0));
    q.at(t0 + 1).add(dayCounters(30, 3, 0, 0, 0, 0));
    auto qs = dailySeries(q, DayMetric::DeathsPerHour, t0, t0 + 1, 7);
    CHECK(qs.size() == 1);
    CHECK_NEAR(qs[0].value, 6.0, 1e-9);
    // The stars: a running total (a day counted under an earlier date never makes it dip).
    PlayStatsData st;
    st.at(t0).stars = 5;
    st.at(t0 + 1).stars = 3;
    st.at(t0 + 3).stars = 8;
    auto ss = starsSeries(st, t0 + 1, t0 + 3);
    CHECK(ss.size() == 2);
    CHECK(ss[0] == std::make_pair(t0 + 1, 5));
    CHECK(ss[1] == std::make_pair(t0 + 3, 8));
}

TEST_CASE("play stats: the bosses' attempts, best progress and phases") {
    PlayStatsStore store;
    store.dayOverride = daysFromCivil(2026, 11, 16);
    PlayStatsStore::Delta d;
    d.bossNames["pyrrhax"] = "Pyrrhax";
    d.bossPhases["pyrrhax"] = 4;
    auto attempt = [](float removed, int phase, bool won, const std::string& attack) {
        BossAttemptRec r;
        r.removed = removed;
        r.phase = phase;
        r.won = won;
        r.attack = attack;
        return std::make_pair(std::string("pyrrhax"), r);
    };
    d.attempts = {attempt(0.20f, 0, false, "Piqué"), attempt(0.10f, 0, false, "Piqué"), attempt(0.55f, 2, false, "Boule de feu"),
                  attempt(0.40f, 1, false, "Piqué"), attempt(1.0f, 3, true, "")};
    for (int i = 0; i < 5; ++i) d.phaseMarks.push_back({"pyrrhax", {0, false}});
    d.phaseMarks.push_back({"pyrrhax", {0, true}});
    d.phaseMarks.push_back({"pyrrhax", {0, true}});
    d.phaseMarks.push_back({"pyrrhax", {1, false}});
    d.phaseMarks.push_back({"pyrrhax", {1, true}});
    store.merge(d);
    const BossHistory& b = store.data().bosses.at("pyrrhax");
    CHECK(b.attempts == 5 && b.wins == 1 && b.firstWin == 5);
    auto curve = bossBestCurve(b);
    CHECK(curve.size() == 5);
    CHECK(curve[0].first == 1);
    CHECK_NEAR(curve[1].second, 0.20, 1e-6);  // the best so far, never lower
    CHECK_NEAR(curve[3].second, 0.55, 1e-6);
    CHECK_NEAR(curve[4].second, 1.0, 1e-6);
    CHECK(hardestPhase(b) == 0);
    CHECK(commonFatal(b) == "Piqué");
    // 200 attempts kept: the older ones leave their best behind.
    PlayStatsStore::Delta more;
    for (int i = 0; i < 250; ++i) more.attempts.push_back(attempt(0.05f, 0, false, "Piqué"));
    store.merge(more);
    const BossHistory& b2 = store.data().bosses.at("pyrrhax");
    CHECK(b2.recent.size() == static_cast<size_t>(kBossAttemptsKept));
    CHECK(b2.attempts == 255);
    CHECK(b2.firstKept() == 56);
    CHECK_NEAR(b2.bestBefore, 1.0f, 1e-6);
    CHECK_NEAR(bossBestCurve(b2).front().second, 1.0, 1e-6);
}

TEST_CASE("play stats: smoothed slices, recency, merged passages and the top 5") {
    CHECK_NEAR(sliceDifficulty(0, 0), 0.5, 1e-9);
    CHECK_NEAR(sliceDifficulty(1, 1), 2.0 / 3.0, 1e-6);  // one death never makes it « impossible »
    CHECK_NEAR(recencyWeight(14), 0.5, 1e-6);
    CHECK_NEAR(recencyWeight(28), 0.25, 1e-6);
    const int today = daysFromCivil(2026, 11, 16);
    LevelSlices l;
    l.name = "La Crevasse";
    l.stamp = today;
    for (int i = 0; i < kSlices; ++i) {
        size_t k = static_cast<size_t>(i);
        l.passes[k] = 20;
        l.wPasses[k] = 20.0f;
    }
    // Two neighbouring hard slices (merged), one hard slice with too few passages, one alone.
    auto hard = [&](int i, int deaths, int passes) {
        size_t k = static_cast<size_t>(i);
        l.deaths[k] = deaths;
        l.wDeaths[k] = static_cast<float>(deaths);
        l.passes[k] = passes;
        l.wPasses[k] = static_cast<float>(passes);
    };
    hard(10, 8, 20);
    hard(11, 6, 20);
    hard(25, 3, 3);
    hard(30, 10, 20);
    auto passages = levelPassages("glacior_02", l, today);
    CHECK(passages.size() == 2);
    CHECK(passages[0].from == 10 && passages[0].to == 11);
    CHECK(passages[0].deaths == 14 && passages[0].passes == 20);
    float d10 = sliceDifficulty(8, 20), d11 = sliceDifficulty(6, 20);
    CHECK_NEAR(passages[0].difficulty, 1.0 - (1.0 - d10) * (1.0 - d11), 1e-6);
    CHECK(passages[1].from == 30 && passages[1].to == 30);
    // Older passages weigh less: the same level 28 days later, one recent easy passage.
    LevelSlices old = l;
    old.decayTo(today + 28);
    CHECK_NEAR(old.wDeaths[10], 2.0, 1e-4);
    // The top 5 mixes levels and boss phases.
    PlayStatsData d;
    d.levels["glacior_02"] = l;
    BossHistory b;
    b.name = "Pyrrhax";
    b.phases = 3;
    b.attempts = 12;
    b.reached = {12, 9, 4};
    b.defeats = {3, 8, 4};  // phase 3: too few attempts reached it
    b.fatal["Piqué"] = 5;
    d.bosses["pyrrhax"] = b;
    auto top = hardestPassages(d, today, 5);
    CHECK(top.size() == 4);
    CHECK(top[0].kind == HardPassage::Kind::BossPhase && top[0].phase == 1);  // 8 / 9
    CHECK_NEAR(top[0].difficulty, 8.0 / 9.0, 1e-6);
    CHECK(top[0].attack == "Piqué");
    for (size_t i = 1; i < top.size(); ++i) CHECK(top[i - 1].difficulty >= top[i].difficulty);
}

TEST_CASE("play stats: classic levels — Sum of Best, zones to work on, consistency, regularity") {
    PlayStatsStore store;
    const int today = daysFromCivil(2026, 11, 16);
    store.dayOverride = today - 40;
    auto run = [&](std::vector<float> segs, bool finish) {
        PlayStatsStore::Delta d;
        d.levelId = "sylvanox_02";
        d.levelName = "Sylvanox 2";
        d.classic = true;
        d.version = "v1";
        d.attempt = true;
        std::vector<std::string> cps = {"cp1", "cp2"};
        std::string prev;
        float total = 0.0f;
        for (size_t i = 0; i < segs.size(); ++i) {
            std::string to = i < cps.size() ? cps[i] : std::string();
            d.segments.push_back({{prev, to}, segs[i]});
            total += segs[i];
            prev = to;
        }
        if (finish) {
            d.finish = total;
            d.route = cps;
        }
        store.merge(d);
    };
    // 40 days ago: irregular runs.
    run({10, 20, 30}, true);   // 60
    run({14, 22, 30}, true);   // 66
    run({10, 30, 30}, true);   // 70
    run({11, 20}, false);      // a fall in the last segment
    store.dayOverride = today;
    run({9, 21, 31}, true);    // 61: the PB stays 60, the SOB becomes 9 + 20 + 30 = 59
    run({10, 21, 30}, true);   // 61
    run({10, 21, 30}, true);   // 61
    const LevelRuns& r = store.data().runs.at("sylvanox_02");
    CHECK(r.attempts == 7 && r.finished == 6);
    CHECK_NEAR(r.pb, 60.0f, 1e-4);
    CHECK(r.pbRoute == std::vector<std::string>({"cp1", "cp2"}));
    CHECK_NEAR(r.sob(), 59.0f, 1e-4);
    CHECK(r.history.size() == 2);
    CHECK_NEAR(r.history[0].sob, 60.0f, 1e-4);
    CHECK_NEAR(r.history[1].sob, 59.0f, 1e-4);
    CHECK_NEAR(r.history[1].pb, 60.0f, 1e-4);
    auto reps = segmentReports(r);
    CHECK(reps.size() == 3);
    CHECK(reps[0].from.empty() && reps[0].to == "cp1");
    CHECK_NEAR(reps[0].best, 9.0f, 1e-4);
    CHECK_NEAR(reps[0].median, 10.0f, 1e-4);  // 10 14 10 11 9 10 10
    CHECK_NEAR(reps[0].lost, 1.0f, 1e-4);
    CHECK_NEAR(reps[1].lost, 1.0f, 1e-4);     // 20 22 30 20 21 21 21: median 21, best 20
    CHECK(reps[2].to.empty());
    CHECK_NEAR(reps[2].lost, 0.0f, 1e-4);
    // Within 5 % of the best: segment 2 (best 20 → 21): 20 22 30 20 21 21 21 → 5 of 7.
    CHECK_NEAR(reps[1].nearBest, 5.0f / 7.0f, 1e-4);
    CHECK(reps[1].spread > 0.0f);
    Regularity g = regularity(r, today);
    CHECK(g.attempts == 7 && g.finished == 6);
    CHECK(g.last.size() == 6);
    CHECK(g.sd.has_value() && g.sdBefore.has_value());
    CHECK(g.trend == 1);  // 61 61 61 after 60 66 70: more regular than 30 days ago
    CHECK(g.rollingMedian.size() == 6);
    // Another version of the level: its segments start again.
    PlayStatsStore::Delta v2;
    v2.levelId = "sylvanox_02";
    v2.classic = true;
    v2.version = "v2";
    v2.attempt = true;
    store.merge(v2);
    CHECK(store.data().runs.at("sylvanox_02").segments.empty());
    CHECK(store.data().runs.at("sylvanox_02").attempts == 1);
}

TEST_CASE("play stats: the file — written by its worker, read back, damaged, hostile, capped") {
    fs::path dir = tempDir();
    fs::path file = dir / "stats.json";
    {
        PlayStatsStore s;
        s.open(file);
        CHECK(s.data().empty() && !s.damaged());
        s.dayOverride = daysFromCivil(2026, 11, 16);
        PlayStatsStore::Delta d;
        d.counters = dayCounters(12, 2, 10, 7, 20, 9);
        d.counters.powers["attack"] = {15, 6};
        d.counters.powers["wave"] = {5, 3};
        d.counters.hurt[static_cast<size_t>(HurtOrigin::Fall)] = 2;
        d.levelId = "sylvanox_01";
        d.levelName = "Sylvanox 1";
        d.passes[3] = 2;
        d.deaths[3] = 1;
        s.merge(d);
        s.saveAsync();
        s.flush();
        CHECK(FileUtil::exists(file));
    }
    {
        PlayStatsStore s;
        s.open(file);
        CHECK(!s.damaged());
        const StatsDay* day = s.data().find(daysFromCivil(2026, 11, 16));
        CHECK(day != nullptr);
        CHECK_NEAR(day->c.playSec, 720.0, 0.11);
        CHECK(day->c.deaths == 2 && day->c.threats == 10 && day->c.dodged == 7);
        CHECK(day->c.powers.at("wave")[0] == 5 && day->c.powers.at("wave")[1] == 3);
        CHECK(day->c.hurt[static_cast<size_t>(HurtOrigin::Fall)] == 2);
        CHECK(s.data().levels.at("sylvanox_01").deaths[3] == 1);
        // « Réinitialiser les statistiques »
        s.reset();
        s.flush();
    }
    {
        PlayStatsStore s;
        s.open(file);
        CHECK(s.data().empty());
    }
    // A damaged file: the tab starts empty, the file is kept as stats.json.bak.
    CHECK(FileUtil::writeTextAtomic(file, "{ \"format\": \"cosmic.stats\", \"version\": 1, \"data\": {"));
    {
        PlayStatsStore s;
        s.open(file);
        CHECK(s.damaged());
        CHECK(s.data().empty());
        fs::path bak = file;
        bak += ".bak";
        CHECK(FileUtil::exists(bak));
        CHECK(!FileUtil::exists(file));
    }
    // Hostile content: wrong types, absurd numbers — read without failing, values kept sane.
    const int recent = localDay() - 1;  // kept day by day (not merged into a month)
    Json bad = {{"format", "cosmic.stats"},
                {"version", 1},
                {"data",
                 {{"names", {1, 2}},
                  {"days", {"x", {recent, -5, "a", 3, 2, 99, 4, 9}, {recent - 1, 1e300}}},
                  {"levels", {{"a", {{"p", "no"}, {"d", {1, -4}}}}, {"b", 7}}},
                  {"bosses", {{"z", {{"ph", 1000000}, {"an", {"Piqué", 4}}, {"fc", {{0, 3}, {9, 9}, "x"}}, {"r", "____AABA!"}}}}},
                  {"runs", {{"r", {{"seg", {{{"f", 3}, {"x", "AAB<<<"}}}}, {"fin", 17}, {"h", "~~~~~~~~"}}}}}}}};
    CHECK(FileUtil::writeTextAtomic(file, bad.dump()));
    {
        PlayStatsStore s;
        s.open(file);
        CHECK(!s.damaged());
        const StatsDay* day = s.data().find(recent);
        CHECK(day != nullptr);
        CHECK(day->c.playSec == 0.0);
        CHECK(day->c.dodged <= day->c.threats);
        CHECK(day->c.hits <= day->c.attacks);
        CHECK(s.data().bosses.at("z").phases <= 64);
        const BossHistory& z = s.data().bosses.at("z");
        CHECK(z.recent.size() == 2);  // "AABA" is the second; "!" ends the list
        CHECK(z.recent.at(0).won && z.recent.at(0).removed == 1.0f);
        CHECK(z.recent.at(0).phase < z.phases);
        CHECK(z.recent.at(1).removed == 0.0f && z.recent.at(1).attack.empty());
        CHECK(z.fatal.size() == 1 && z.fatal.at("Piqué") == 3);
        CHECK(s.data().runs.at("r").segments.at(0).times.size() == 1);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);

    // Years of play with every cap reached: under 300 KB.
    const int today = daysFromCivil(2036, 11, 16);
    PlayStatsData full = atCap(today, "");
    full.compact(today);
    CHECK(full.days.size() == static_cast<size_t>(kDaysKept));
    CHECK(full.months.size() <= static_cast<size_t>(kMonthsKept));
    CHECK(full.levels.size() == kMaxLevels);
    CHECK(full.bosses.size() == kMaxBosses);
    CHECK(full.runs.size() == kMaxRunLevels);
    const size_t bytes = PlayStatsStore::serialize(full).size();
    std::printf("  statistiques au plafond : %zu octets (", bytes);
    const Json parts = full.toJson();
    for (const auto& [k, v] : parts.items()) std::printf(" %s %zu", k.c_str(), v.dump().size());
    std::printf(" )\n");
    CHECK(bytes < kMaxFileBytes);
}

// ------------------------------------------------------------------ 2.55: two copies merged

namespace {

BossAttemptRec attemptOn(int day, uint32_t id, float removed, int phase, bool won, const std::string& attack) {
    BossAttemptRec r;
    r.day = day;
    r.id = id;
    r.removed = removed;
    r.phase = phase;
    r.won = won;
    r.attack = attack;
    return r;
}

/// A boss history whose list is `all[from..to)` and which counted `to` attempts.
BossHistory bossOf(const std::vector<BossAttemptRec>& all, size_t from, size_t to) {
    BossHistory b;
    b.name = "Pyrrhax";
    b.phases = 3;
    b.reached.assign(3, 0);
    b.defeats.assign(3, 0);
    for (size_t i = 0; i < to; ++i) {
        const BossAttemptRec& r = all[i];
        b.attempts++;
        if (r.won) {
            b.wins++;
            if (b.firstWin == 0) b.firstWin = b.attempts;
        }
        for (int p = 0; p <= r.phase; ++p) b.reached[static_cast<size_t>(p)]++;
        if (!r.won && !r.attack.empty()) {
            b.defeats[static_cast<size_t>(r.phase)]++;
            b.fatal[r.attack]++;
        }
        if (i >= from) b.recent.push_back(r);
        else b.bestBefore = std::max(b.bestBefore, r.won ? 1.0f : r.removed);
    }
    b.lastSeq = static_cast<int>(to);
    return b;
}

std::string dumpOf(const PlayStatsData& d) { return d.toJson().dump(); }

}  // namespace

TEST_CASE("play stats: two computers merged without counting anything twice (2.55)") {
    const int today = daysFromCivil(2026, 11, 17);
    PlayStatsData mine, theirs;
    // Days: one on each side only, one played on both computers.
    mine.at(today - 2) = dayCounters(30, 3, 10, 6, 20, 8);
    mine.at(today - 1) = dayCounters(20, 5, 8, 7, 10, 9);
    mine.at(today - 1).powers["wave"] = {4, 1};
    mine.at(today - 1).hurt[static_cast<size_t>(HurtOrigin::Fall)] = 2;
    mine.at(today - 1).stars = 40;
    theirs.at(today - 1) = dayCounters(50, 2, 12, 3, 15, 4);
    theirs.at(today - 1).powers["wave"] = {2, 2};
    theirs.at(today - 1).powers["boomerang"] = {6, 5};
    theirs.at(today - 1).hurt[static_cast<size_t>(HurtOrigin::Fall)] = 1;
    theirs.at(today - 1).stars = 55;
    theirs.at(today - 10) = dayCounters(15, 1, 4, 4, 4, 4);
    theirs.at(today + 400) = dayCounters(600, 99, 99, 99, 99, 99);  // far in the future: left out
    theirs.months.push_back({2020 * 12, dayCounters(90, 9, 9, 9, 9, 9)});
    // A level's slices.
    LevelSlices sa, sb;
    sa.name = "Sylvanox 1";
    sa.stamp = sa.lastDay = today - 1;
    sa.passes[3] = 10;
    sa.deaths[3] = 4;
    sa.wPasses[3] = 10.0f;
    sb.stamp = sb.lastDay = today - 1;
    sb.passes[3] = 6;
    sb.deaths[3] = 5;
    sb.passes[7] = 2;
    sb.wPasses[3] = 6.0f;
    mine.levels["sylvanox_01"] = sa;
    theirs.levels["sylvanox_01"] = sb;
    theirs.levels["glacior_01"] = sb;
    // A boss: the old computer's attempts, some of them already on this one (restored before).
    std::vector<BossAttemptRec> all = {attemptOn(today - 9, 11, 0.20f, 0, false, "Piqué"), attemptOn(today - 9, 12, 0.35f, 1, false, "Piqué"),
                                       attemptOn(today - 5, 13, 0.50f, 1, false, "Boule de feu"), attemptOn(today - 5, 14, 1.0f, 2, true, ""),
                                       attemptOn(today - 3, 15, 0.10f, 0, false, "")};
    mine.bosses["pyrrhax"] = bossOf(all, 0, 3);  // 11, 12, 13
    // The old computer: 11 … 15, and 11 already older than its list (it keeps 12 … 15).
    theirs.bosses["pyrrhax"] = bossOf(all, 1, 5);
    // This computer played it too, on its own.
    BossHistory& mb = mine.bosses["pyrrhax"];
    mb.recent.push_back(attemptOn(today - 4, 21, 0.60f, 2, false, "Piqué"));
    mb.attempts++;
    mb.reached = {4, 3, 1};
    mb.defeats = {1, 2, 1};
    mb.fatal = {{"Piqué", 3}, {"Boule de feu", 1}};
    theirs.bosses["astrarque"] = bossOf({attemptOn(today - 20, 31, 0.4f, 0, false, "Rayon")}, 0, 1);
    // A classic level: the same version on both sides.
    LevelRuns ra, rb;
    ra.name = "Sylvanox 2";
    ra.version = rb.version = "v1";
    ra.lastDay = today - 1;
    rb.lastDay = today - 2;
    ra.attempts = 6;
    ra.finished = 2;
    rb.attempts = 9;
    rb.finished = 3;
    ra.pb = 60.0f;
    ra.pbRoute = {"cp1"};
    rb.pb = 58.5f;
    rb.pbRoute = {"cp1"};
    ra.segmentAt("", "cp1").times = {30.0f, 31.0f, 30.0f};
    ra.segmentAt("", "cp1").best = 29.5f;
    ra.segmentAt("cp1", "").times = {29.0f};
    ra.segmentAt("cp1", "").best = 29.0f;
    rb.segmentAt("", "cp1").times = {30.0f, 28.75f};
    rb.segmentAt("", "cp1").best = 28.75f;
    rb.segmentAt("cp1", "").times = {29.75f, 30.5f};
    rb.segmentAt("cp1", "").best = 29.75f;
    rb.segmentAt("cp1", "cp2").times = {12.0f};  // only there
    rb.segmentAt("cp1", "cp2").best = 12.0f;
    ra.finishes = {{today - 6, 61.0f}, {today - 1, 60.0f}};
    rb.finishes = {{today - 8, 63.0f}, {today - 6, 61.0f}, {today - 2, 58.5f}};  // the run of today - 6 is the same
    ra.history = {{today - 6, 61.0f, 59.5f}, {today - 1, 60.0f, 58.5f}};
    rb.history = {{today - 8, 63.0f, 60.0f}, {today - 2, 58.5f, 58.5f}};
    mine.runs["sylvanox_02"] = ra;
    theirs.runs["sylvanox_02"] = rb;
    // Another version of a level: the side played the most recently is kept whole.
    LevelRuns va = ra, vb = rb;
    vb.version = "v2";
    vb.lastDay = today;
    mine.runs["sylvanox_03"] = va;
    theirs.runs["sylvanox_03"] = vb;
    mine.seq = 7;
    theirs.seq = 5;

    const PlayStatsData m = mergePlayStats(mine, theirs, today);
    // Days: each counter the highest of both sides; a day of one side kept as it is.
    const StatsDay* both = m.find(today - 1);
    CHECK(both != nullptr);
    CHECK_NEAR(both->c.playSec, 50 * 60.0, 1e-6);
    CHECK(both->c.deaths == 5 && both->c.threats == 12 && both->c.dodged == 7 && both->c.attacks == 15 && both->c.hits == 9);
    CHECK(both->c.dodged <= both->c.threats && both->c.hits <= both->c.attacks);
    CHECK(both->c.powers.at("wave")[0] == 4 && both->c.powers.at("wave")[1] == 2);
    CHECK(both->c.powers.at("boomerang")[0] == 6);
    CHECK(both->c.hurt[static_cast<size_t>(HurtOrigin::Fall)] == 2);
    CHECK(both->c.stars == 55);
    CHECK(m.find(today - 2) && m.find(today - 2)->c.deaths == 3);
    CHECK(m.find(today - 10) && m.find(today - 10)->c.deaths == 1);
    CHECK(m.find(today + 400) == nullptr);
    CHECK(m.days.size() == 3);
    CHECK(m.months.size() == 1);
    // Slices: the highest count.
    const LevelSlices& sl = m.levels.at("sylvanox_01");
    CHECK(sl.passes[3] == 10 && sl.deaths[3] == 5 && sl.passes[7] == 2);
    CHECK(sl.name == "Sylvanox 1");
    CHECK(m.levels.count("glacior_01") == 1);
    // The boss: 12 … 15 from the old computer, 21 from this one, 12 and 13 once each; 11 is older
    // than the old computer's list (it might be one it counted): counted once, with the rest.
    const BossHistory& b = m.bosses.at("pyrrhax");
    std::vector<uint32_t> ids;
    for (const auto& r : b.recent) ids.push_back(r.id);
    CHECK(ids == (std::vector<uint32_t>{12, 13, 14, 21, 15}));  // by day (on a day, this computer's first)
    CHECK(b.attempts == 6);
    CHECK(b.firstKept() == 2);
    CHECK(b.wins == 1);
    CHECK(b.firstWin == 4);
    CHECK(b.defeats == (std::vector<int>{1, 2, 1}));
    CHECK(b.reached == (std::vector<int>{6, 4, 2}));
    CHECK(b.fatal.at("Piqué") == 3 && b.fatal.at("Boule de feu") == 1);
    CHECK_NEAR(b.best(), 1.0f, 1e-6);
    CHECK(m.bosses.count("astrarque") == 1);
    CHECK(bossesByRecency(m).front() == "pyrrhax");  // fought 3 days ago, the other one 20
    // The classic level: times united (an equal time once), the better best and PB.
    const LevelRuns& r = m.runs.at("sylvanox_02");
    const SegmentRec* first = r.segment("", "cp1");
    CHECK(first != nullptr);
    CHECK(first->times == (std::vector<float>{30.0f, 28.75f, 31.0f, 30.0f}));  // the older side first
    CHECK_NEAR(first->best, 28.75f, 1e-6);
    CHECK_NEAR(r.segment("cp1", "")->best, 29.0f, 1e-6);
    CHECK(r.segment("cp1", "cp2") != nullptr);
    CHECK_NEAR(r.pb, 58.5f, 1e-6);
    CHECK_NEAR(r.sob(), 28.75f + 29.0f, 1e-4);
    CHECK(r.finishes.size() == 4);  // 61.0 of today - 6 once
    CHECK(r.finished == 4);
    CHECK(r.attempts == 4 + std::max(6 - 2, 9 - 3));
    CHECK(r.history.size() == 5);  // both histories, sorted, then the merged PB / SOB
    for (size_t i = 1; i < r.history.size(); ++i) CHECK(r.history[i - 1].day <= r.history[i].day);
    CHECK_NEAR(r.history.back().pb, 58.5f, 1e-6);
    CHECK_NEAR(r.history.back().sob, 57.75f, 1e-4);
    CHECK(m.runs.at("sylvanox_03").version == "v2");
    CHECK(m.seq >= 7);

    // The same copy merged again (a restoration whose end did not reach the server): nothing changes.
    CHECK(dumpOf(mergePlayStats(m, theirs, today)) == dumpOf(m));
    CHECK(dumpOf(mergePlayStats(m, mine, today)) == dumpOf(m));
    CHECK(dumpOf(mergePlayStats(m, m, today)) == dumpOf(m));
    // Through the file's text too (what the copy carries).
    const PlayStatsData read = PlayStatsData::fromJson(m.toJson());
    CHECK(dumpOf(mergePlayStats(read, PlayStatsData::fromJson(theirs.toJson()), today)) == dumpOf(read));
    // Nothing from the other side: this computer's statistics as they are.
    PlayStatsData alone = mine;
    alone.assignIds();
    alone.compact(today);
    CHECK(dumpOf(mergePlayStats(mine, PlayStatsData{}, today)) == dumpOf(alone));
}

TEST_CASE("play stats: a boss's attempts beyond the 200 kept — counted once, the 200 most recent kept") {
    const int today = daysFromCivil(2026, 11, 17);
    std::vector<BossAttemptRec> all;
    for (int i = 0; i < 350; ++i) {
        const bool won = i % 50 == 49;
        all.push_back(attemptOn(today - 40 + i / 10, static_cast<uint32_t>(1000 + i), 0.001f * static_cast<float>(i % 900), i % 3, won,
                                won ? std::string() : (i % 2 ? "Piqué" : "Boule de feu")));
    }
    // This computer has the old one's attempts 0 … 299 (its last 200 kept); the old one played 50
    // more (0 … 349, its last 200 kept).
    const BossHistory a = bossOf(all, 100, 300), b = bossOf(all, 150, 350);
    PlayStatsData mine, theirs;
    mine.bosses["pyrrhax"] = a;
    theirs.bosses["pyrrhax"] = b;
    const PlayStatsData m = mergePlayStats(mine, theirs, today);
    const BossHistory& x = m.bosses.at("pyrrhax");
    CHECK(x.attempts == 350);  // not 550
    CHECK(x.wins == b.wins);
    CHECK(x.defeats == b.defeats);
    CHECK(x.reached == b.reached);
    CHECK(x.fatal == b.fatal);
    CHECK(x.recent.size() == static_cast<size_t>(kBossAttemptsKept));
    CHECK(x.recent.front().id == 1150 && x.recent.back().id == 1349);
    CHECK(x.firstWin == b.firstWin);
    CHECK(dumpOf(mergePlayStats(m, theirs, today)) == dumpOf(m));
    CHECK(dumpOf(mergePlayStats(m, mine, today)) == dumpOf(m));
    // Two computers that never shared anything: every attempt counted.
    std::vector<BossAttemptRec> other;
    for (int i = 0; i < 120; ++i) other.push_back(attemptOn(today - 5 + i / 30, static_cast<uint32_t>(5000 + i), 0.3f, 1, false, "Rayon"));
    PlayStatsData third;
    third.bosses["pyrrhax"] = bossOf(other, 0, 120);
    const PlayStatsData d = mergePlayStats(theirs, third, today);
    const BossHistory& y = d.bosses.at("pyrrhax");
    CHECK(y.attempts == 350 + 120);
    CHECK(y.recent.size() == static_cast<size_t>(kBossAttemptsKept));
    CHECK(y.recent.back().id == 5119);
    CHECK(y.fatal.at("Rayon") == 120);
    CHECK(y.defeats[1] == b.defeats[1] + 120);
    CHECK(dumpOf(mergePlayStats(d, third, today)) == dumpOf(d));
    CHECK(dumpOf(mergePlayStats(d, theirs, today)) == dumpOf(d));
}

TEST_CASE("play stats: attempts keep their day and identifier; a 2.54 file gets them once") {
    fs::path dir = tempDir();
    fs::path file = dir / "stats.json";
    const int today = daysFromCivil(2026, 11, 17);
    PlayStatsData d;
    BossHistory& b = d.bosses["pyrrhax"];
    b.phases = 2;
    b.reached = {2, 1};
    b.defeats = {1, 0};
    b.attempts = 2;
    b.recent = {attemptOn(today - 3, 77, 0.25f, 0, false, "Piqué"), attemptOn(today, 0x3FFFF, 0.5f, 1, false, "")};
    const PlayStatsData back = PlayStatsData::fromJson(d.toJson());
    CHECK(back.bosses.at("pyrrhax").recent.size() == 2);
    CHECK(back.bosses.at("pyrrhax").recent[0].day == today - 3 && back.bosses.at("pyrrhax").recent[0].id == 77);
    CHECK(back.bosses.at("pyrrhax").recent[1].id == 0x3FFFFu);
    // A 2.54 file (no "rk"): identifiers given when it is opened, and written at once.
    Json j = d.toJson();
    j["bosses"]["pyrrhax"].erase("rk");
    Json env = {{"format", "cosmic.stats"}, {"version", 1}, {"data", j}};
    CHECK(FileUtil::writeTextAtomic(file, env.dump()));
    std::vector<uint32_t> first;
    {
        PlayStatsStore s;
        s.dayOverride = today;
        s.open(file);
        s.flush();
        for (const auto& r : s.data().bosses.at("pyrrhax").recent) {
            CHECK(r.id != 0 && r.day == 0);
            first.push_back(r.id);
        }
        CHECK(first.size() == 2 && first[0] != first[1]);
    }
    {
        PlayStatsStore s;
        s.dayOverride = today;
        s.open(file);
        std::vector<uint32_t> again;
        for (const auto& r : s.data().bosses.at("pyrrhax").recent) again.push_back(r.id);
        CHECK(again == first);  // the same ones: they were written
        // A new attempt gets its day and its own identifier.
        PlayStatsStore::Delta delta;
        delta.attempts.push_back({"pyrrhax", attemptOn(0, 0, 0.9f, 1, true, "")});
        s.merge(delta);
        const BossAttemptRec& n = s.data().bosses.at("pyrrhax").recent.back();
        CHECK(n.day == today && n.id != 0);
        s.flush();
    }
    // The same identifier twice on a day (a file edited by hand): the second one gets another.
    PlayStatsData dup = PlayStatsData::fromJson(d.toJson());
    dup.bosses.at("pyrrhax").recent[1].day = today - 3;
    dup.bosses.at("pyrrhax").recent[1].id = 77;
    PlayStatsData dup2 = PlayStatsData::fromJson(dup.toJson());
    CHECK(dup2.bosses.at("pyrrhax").recent[0].id == 77 && dup2.bosses.at("pyrrhax").recent[1].id == 0);
    CHECK(dup2.assignIds() && dup2.bosses.at("pyrrhax").recent[1].id != 77);
    // A name cut at 120 bytes never ends in the middle of a character (the file stays writable).
    PlayStatsData accents;
    accents.levels["sylvanox_01"].name = std::string(119, 'a') + "é";
    accents.levels["sylvanox_01"].passes[0] = 1;
    const PlayStatsData cutBack = PlayStatsData::fromJson(accents.toJson());
    CHECK(cutBack.levels.at("sylvanox_01").name == std::string(119, 'a'));
    CHECK(PlayStatsStore::serialize(cutBack).find(std::string(119, 'a')) != std::string::npos);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("play stats: two files at every cap merged — still under 300 KB, even from a hostile copy") {
    const int today = daysFromCivil(2036, 11, 16);
    PlayStatsData mine = atCap(today, "a_"), theirs = atCap(today - 3, "b_");
    // A hostile copy: thousands of days far in the future, months too.
    for (int k = 1; k <= 3000; ++k) theirs.at(today + 40 + k) = dayCounters(600, 9, 9, 9, 9, 9);
    for (int k = 1; k <= 500; ++k) theirs.months.push_back({(2040 + k) * 12, dayCounters(600, 9, 9, 9, 9, 9)});
    const PlayStatsData m = mergePlayStats(mine, theirs, today);
    CHECK(m.days.size() <= static_cast<size_t>(kDaysKept));
    CHECK(m.months.size() <= static_cast<size_t>(kMonthsKept));
    CHECK(m.levels.size() == kMaxLevels);
    CHECK(m.bosses.size() == kMaxBosses);
    CHECK(m.runs.size() == kMaxRunLevels);
    for (const auto& [id, b] : m.bosses) CHECK(b.recent.size() <= static_cast<size_t>(kBossAttemptsKept));
    const size_t bytes = PlayStatsStore::serialize(m).size();
    std::printf("  statistiques fusionnées au plafond : %zu octets\n", bytes);
    CHECK(bytes < kMaxFileBytes);
    // The same names on both sides (one player, two computers): every list at its cap.
    const PlayStatsData same = mergePlayStats(atCap(today, ""), atCap(today - 1, ""), today);
    for (const auto& [id, b] : same.bosses) CHECK(b.recent.size() == static_cast<size_t>(kBossAttemptsKept));
    for (const auto& [id, r] : same.runs)
        for (const auto& s : r.segments) CHECK(s.times.size() <= kSegmentTimesKept);
    CHECK(PlayStatsStore::serialize(same).size() < kMaxFileBytes);
    // Every name at its longest (120 characters) in the copy: over the cap once merged, so what
    // was played the least recently goes until the file fits.
    PlayStatsData longNames = atCap(today, "c_");  // played more recently: kept by the caps
    const std::string longText(120, 'x');
    for (auto& [id, b] : longNames.bosses) {
        b.name = longText;
        std::map<std::string, int> fatal;
        for (int a = 0; a < 12; ++a) fatal[longText.substr(0, 117) + std::to_string(100 + a)] = a + 1;
        b.fatal = fatal;
        for (size_t i = 0; i < b.recent.size(); ++i) {
            b.recent[i].attack = longText.substr(0, 117) + std::to_string(100 + i % 12);
            b.recent[i].day += 20;
        }
    }
    for (auto& [id, l] : longNames.levels) l.name = longText;
    for (auto& [id, r] : longNames.runs) r.name = longText;
    CHECK(PlayStatsStore::serialize(mergePlayStats(PlayStatsData{}, longNames, today)).size() < kMaxFileBytes);
    const PlayStatsData fitted = mergePlayStats(mine, longNames, today);
    std::printf("  noms les plus longs : %zu octets, %zu mois et %zu niveaux classiques gardés\n", PlayStatsStore::serialize(fitted).size(),
                fitted.months.size(), fitted.runs.size());
    CHECK(PlayStatsStore::serialize(fitted).size() < kMaxFileBytes);
    CHECK(fitted.months.size() < m.months.size());  // the oldest went first
    CHECK(fitted.bosses.size() == kMaxBosses && fitted.days.size() == static_cast<size_t>(kDaysKept));  // only older parts went
}

TEST_CASE("play stats: the recorder reads a run without changing it") {
    const auto inputs = scripted(42, 120 * 40);
    Sim plain("sylvanox_01");
    auto ref = plain.run(inputs);
    Sim watched("sylvanox_01");
    PlayStatsStore store;
    store.dayOverride = daysFromCivil(2026, 11, 16);
    PlayStatsRun run;
    run.levelId = "sylvanox_01";
    run.level = &*watched.level;
    run.classic = true;
    run.levelVersion = "test";
    auto rec = store.beginRun(*watched.w, run, watched.content);
    CHECK(rec != nullptr);
    CHECK(watched.w->listener == rec.get());
    CHECK(watched.run(inputs, rec.get()) == ref);  // the same digests, step after step
    rec->finish();
    CHECK(watched.w->listener == nullptr);
    const StatsDay* day = store.data().find(daysFromCivil(2026, 11, 16));
    CHECK(day != nullptr);
    CHECK_NEAR(day->c.playSec, 40.0, 0.05);
    CHECK(day->c.attacks > 0);
    CHECK(day->c.powers.count("attack") == 1);
    CHECK(day->c.hits <= day->c.attacks && day->c.dodged <= day->c.threats);
    CHECK(day->c.deaths == plain.w->stats().deaths);
    CHECK(store.data().runs.count("sylvanox_01") == 1);
    CHECK(store.data().runs.at("sylvanox_01").attempts == 1);
}

TEST_CASE("play stats: a boss fight followed, its digest unchanged; Invincible and training never counted") {
    auto setup = [](Sim& s) {
        std::string bossObject;
        Rect zone;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "enemy" && o.propStr("enemy") == "singularis") bossObject = o.id;
        for (const LevelObject& o : s.w->level().objects)
            if (o.type == "combat_zone" && !bossObject.empty() && o.propStr("waves").find(bossObject) != std::string::npos) zone = o.rect;
        auto* spawn = dynamic_cast<EnemySpawnEntity*>(s.w->entity(bossObject));
        Enemy* boss = spawn ? spawn->enemy(*s.w) : nullptr;
        CHECK(boss != nullptr);
        s.w->startAt({zone.x + 18.0f * 16.0f, zone.bottom() - 14.0f * 16.0f});
        s.w->startBoss(*boss);
        jumpBossToPhase(*s.w, *boss, 4);
    };
    const auto inputs = scripted(11, 30 * 120);
    Sim plain("noyau_03");
    setup(plain);
    auto ref = plain.run(inputs);
    Sim watched("noyau_03");
    setup(watched);
    PlayStatsStore store;
    store.dayOverride = daysFromCivil(2026, 11, 16);
    PlayStatsRun run;
    run.levelId = "noyau_03";
    run.level = &*watched.level;
    auto rec = store.beginRun(*watched.w, run, watched.content);
    CHECK(rec != nullptr);
    auto got = watched.run(inputs, rec.get());
    CHECK(got == ref);
    rec->finish();
    // The golden digest of tests/test_determinism.cpp, with the recorder listening (2.76.2: Singularis by
    // its parts, kSimulationVersion 17).
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(got.empty() ? 0 : got.back()));
    CHECK(std::string(hex) == "1cbd3f4c5440aa1a");
    CHECK(store.data().bosses.count("singularis") == 1);
    const BossHistory& b = store.data().bosses.at("singularis");
    CHECK(b.attempts >= 1);
    CHECK(b.recent.front().phase >= 4);
    const StatsDay* day = store.data().find(daysFromCivil(2026, 11, 16));
    CHECK(day != nullptr);
    int blows = 0;
    for (int h : day->c.hurt) blows += h;
    CHECK(blows >= day->c.deaths);

    // « Invincible » (administrators, training only): nothing recorded.
    Sim inv("sylvanox_01");
    inv.w->setTraining(true);
    inv.w->setInvincible(true);
    PlayStatsStore none;
    PlayStatsRun r2;
    r2.levelId = "sylvanox_01";
    r2.level = &*inv.level;
    CHECK(none.beginRun(*inv.w, r2, inv.content) == nullptr);
    // A training run: its time only, apart.
    Sim tr("sylvanox_01");
    tr.w->setTraining(true);
    PlayStatsStore ts;
    ts.dayOverride = daysFromCivil(2026, 11, 16);
    auto trec = ts.beginRun(*tr.w, r2, tr.content);
    CHECK(trec != nullptr);
    tr.run(scripted(3, 120 * 10), trec.get());
    trec->finish();
    const StatsDay* td = ts.data().find(daysFromCivil(2026, 11, 16));
    CHECK(td != nullptr);
    CHECK(td->c.playSec == 0.0);
    CHECK_NEAR(td->c.trainSec, 10.0, 0.05);
    CHECK(td->c.attacks == 0 && td->c.deaths == 0);
    // Turned off in the settings: nothing.
    PlayStatsStore off;
    off.enabled = false;
    Sim o("sylvanox_01");
    CHECK(off.beginRun(*o.w, r2, o.content) == nullptr);
}
