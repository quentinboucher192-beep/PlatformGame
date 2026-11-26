// 2.91 — the redesigned « Statistiques » screen: what it computes for its display from what the game
// already keeps (game/records/PlayStatsViews.h: « Récemment joué », the days of a period, « Répartition du
// fichier », the sources of the Sum of Best), the pictures of the levels made from the game's own renderer
// (ui/screens/StatisticsArt.h: read off the main thread, drawn a few per frame), the charts' new pieces,
// and every tab drawn with real-looking statistics at 1280 x 720 and 1920 x 1080, in French and in English
// with nothing left in French.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "game/GameServices.h"
#include "game/records/PlayStats.h"
#include "game/records/PlayStatsViews.h"
#include "tests/TestFramework.h"
#include "ui/screens/Navigation.h"
#include "ui/screens/StatisticsArt.h"
#include "ui/widgets/Charts.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;
using namespace cosmic::playstats;

namespace {

BossAttemptRec attempt(int day, float removed, int phase, bool won, const std::string& attack = {}) {
    BossAttemptRec r;
    r.day = day;
    r.removed = removed;
    r.phase = phase;
    r.won = won;
    r.attack = attack;
    r.id = static_cast<uint32_t>(day % 1000 + 1);
    return r;
}

/// Statistics as a player would have them after a few weeks (every tab has something to show).
PlayStatsData played(int today) {
    PlayStatsData d;
    for (int k = 0; k < 40; ++k) {
        if (k % 3 == 2) continue;  // some days not played
        StatsCounters& c = d.at(today - k);
        c.playSec = 1800.0 + 60.0 * (k % 7);
        c.trainSec = k % 4 == 0 ? 600.0 : 0.0;
        c.deaths = 3 + k % 5;
        c.threats = 40 + k;
        c.dodged = 28 + k / 2;
        c.attacks = 90;
        c.hits = 55 + k % 9;
        c.powers["attack"] = {60, 35};
        c.powers["wave"] = {20, 14};
        c.powers["boomerang"] = {10, 6};
        c.hurt[static_cast<size_t>(HurtOrigin::EnemyAttack)] = 6;
        c.hurt[static_cast<size_t>(HurtOrigin::EnemyShot)] = 4;
        c.hurt[static_cast<size_t>(HurtOrigin::Hazard)] = 2;
        c.hurt[static_cast<size_t>(HurtOrigin::Fall)] = 1;
        c.stars = 200 + (40 - k);
    }
    LevelSlices& s = d.levels["aethra_01"];
    s.name = "Le Bosquet Englouti";
    s.lastDay = today;
    s.stamp = today;
    for (int i = 0; i < kSlices; ++i) {
        const size_t k = static_cast<size_t>(i);
        s.passes[k] = 60 - i;
        s.deaths[k] = i == 13 ? 19 : i == 26 ? 15 : i % 4;
        s.wPasses[k] = static_cast<float>(s.passes[k]);
        s.wDeaths[k] = static_cast<float>(s.deaths[k]);
    }
    BossHistory& b = d.bosses["mycelia"];
    b.name = "Mycélia";
    b.phases = 3;
    b.reached = {30, 22, 12};
    b.defeats = {8, 10, 9};
    b.fatal["Pluie de spores"] = 12;
    for (int a = 0; a < 30; ++a) b.recent.push_back(attempt(today - 30 + a, std::min(1.0f, 0.2f + a * 0.03f), a / 11, a == 25 || a == 29, "Pluie de spores"));
    b.attempts = 30;
    b.wins = 2;
    b.firstWin = 26;
    b.lastSeq = 4;
    d.seq = 4;
    LevelRuns& r = d.runs["aethra_01"];
    r.name = "Le Bosquet Englouti";
    r.version = "v1";
    r.attempts = 31;
    r.finished = 14;
    r.pbRoute = {"o104", "o148", "o213", "o263"};
    r.pb = 158.92f;
    const char* cps[] = {"", "o104", "o148", "o213", "o263", ""};
    const float bests[] = {31.2f, 27.86f, 29.96f, 24.1f, 38.41f};
    for (int i = 0; i < 5; ++i) {
        SegmentRec& seg = r.segmentAt(cps[i], cps[i + 1]);
        for (int t = 0; t < 20; ++t) seg.times.push_back(bests[i] + 0.1f * static_cast<float>(t % 7));
        seg.best = bests[i];
        if (i == 1) seg.trainBest = bests[i] - 0.5f;
    }
    for (int f = 0; f < 14; ++f) r.finishes.push_back({today - 14 + f, 170.0f - f * 0.8f});
    r.history = {{today - 20, 171.2f, 163.8f}, {today - 10, 165.9f, 159.1f}, {today - 3, 158.92f, 151.5f}};
    r.lastDay = today - 1;
    return d;
}

}  // namespace

TEST_CASE("statistics screen: « Récemment joué » — the most recent first, levels before bosses on a day, unknown days last") {
    const int today = daysFromCivil(2026, 11, 26);
    PlayStatsData d;
    CHECK(recentlyPlayed(d, 4).empty());
    d.levels["a"].passes[0] = 1;
    d.levels["a"].lastDay = today - 2;
    d.runs["b"].attempts = 1;
    d.runs["b"].lastDay = today;
    d.levels["b"].passes[3] = 2;
    d.levels["b"].lastDay = today - 5;  // the later of both days counts
    d.levels["empty"].lastDay = today;  // no passage: not played
    BossHistory& m = d.bosses["m"];
    m.attempts = 2;
    m.recent = {attempt(today - 9, 0.1f, 0, false), attempt(today, 0.4f, 1, false)};
    m.lastSeq = 1;
    BossHistory& n = d.bosses["n"];
    n.attempts = 1;
    n.recent = {attempt(today, 0.5f, 0, false)};
    n.lastSeq = 2;  // fought after « m » the same day
    BossHistory& old = d.bosses["old"];
    old.attempts = 3;
    old.recent = {attempt(0, 0.3f, 0, false)};  // kept by 2.54: its day unknown
    old.lastSeq = 0;
    CHECK(levelLastDay(d, "b") == today);
    CHECK(levelLastDay(d, "empty") == -1);
    CHECK(bossLastDay(m) == today);
    CHECK(bossLastDay(old) == 0);
    auto r = recentlyPlayed(d, 10);
    CHECK(r.size() == 5);
    CHECK(r[0].kind == RecentPlay::Kind::Level && r[0].id == "b");
    CHECK(r[1].kind == RecentPlay::Kind::Boss && r[1].id == "n");  // the last fight first
    CHECK(r[2].id == "m");
    CHECK(r[3].id == "a" && r[3].day == today - 2);
    CHECK(r[4].id == "old" && r[4].day == 0);
    CHECK(recentlyPlayed(d, 2).size() == 2);
}

TEST_CASE("statistics screen: the days of a period and their sums over 7 days") {
    PlayStatsData d;
    d.at(100).playSec = 60.0;
    d.at(102).playSec = 120.0;
    d.at(110).playSec = 30.0;
    const auto v = dailyPlaySeconds(d, 99, 103);
    CHECK(v.size() == 5);
    CHECK(v[0] == 0.0 && v[1] == 60.0 && v[2] == 0.0 && v[3] == 120.0 && v[4] == 0.0);
    CHECK(dailyPlaySeconds(d, 5, 4).empty());
    const auto s = trailingSums({1, 2, 3, 4, 5}, 3);
    CHECK(s.size() == 5);
    CHECK(s[0] == 1 && s[1] == 3 && s[2] == 6 && s[3] == 9 && s[4] == 12);
    CHECK(trailingSums({}, 7).empty());
}

TEST_CASE("statistics screen: « Répartition du fichier » — each part of stats.json, its size and what it holds") {
    const int today = daysFromCivil(2026, 11, 26);
    PlayStatsData d = played(today);
    d.months.push_back({2026 * 12 + 1, {}});
    const std::string text = PlayStatsStore::serialize(d);
    const FileBreakdown b = fileBreakdown(Json::parse(text), text.size());
    CHECK(b.ok);
    CHECK(b.total == text.size());
    CHECK(b.parts.size() == 4);
    size_t sum = 0;
    for (const auto& p : b.parts) {
        CHECK(p.bytes > 0);
        sum += p.bytes;
    }
    CHECK(sum + b.other == b.total);
    CHECK(b.other < 400);  // only the envelope
    CHECK(b.parts[0].kind == FilePart::Kind::Days && b.parts[0].count == static_cast<int>(d.days.size()) && b.parts[0].count2 == 1);
    CHECK(b.parts[1].kind == FilePart::Kind::Bosses && b.parts[1].count == 1 && b.parts[1].count2 == 30);
    CHECK(b.parts[2].kind == FilePart::Kind::Levels && b.parts[2].count == 1 && b.parts[2].count2 == kSlices);
    CHECK(b.parts[3].kind == FilePart::Kind::Segments && b.parts[3].count == 1 && b.parts[3].count2 == 5);
    // Each part's size is its text in the file.
    const Json j = Json::parse(text);
    CHECK(b.parts[1].bytes == j["data"]["bosses"].dump().size());
    // Not a statistics file: nothing.
    CHECK(!fileBreakdown(Json::parse("{\"format\":\"cosmic.other\"}"), 25).ok);
    CHECK(!fileBreakdown(Json::array(), 2).ok);
    // An empty file of statistics: every part, empty.
    const std::string empty = PlayStatsStore::serialize(PlayStatsData{});
    const FileBreakdown e = fileBreakdown(Json::parse(empty), empty.size());
    CHECK(e.ok && e.parts.size() == 4 && e.parts[1].count == 0 && e.parts[3].count2 == 0);
}

TEST_CASE("statistics screen: the sources of the Sum of Best, segment by segment") {
    const int today = daysFromCivil(2026, 11, 26);
    const PlayStatsData d = played(today);
    const LevelRuns& r = d.runs.at("aethra_01");
    const auto parts = sobParts(r);
    CHECK(parts.size() == 5);
    float sum = 0.0f;
    for (const auto& p : parts) sum += p.best;
    CHECK_NEAR(sum, r.sob(), 1e-3);
    CHECK(parts[0].source == SobSource::Run && parts[0].from.empty() && parts[0].to == "o104");
    CHECK(parts[1].source == SobSource::Training);  // half a second better at the training
    CHECK_NEAR(parts[1].best, 27.36f, 1e-4);
    CHECK(parts[4].to.empty() && parts[4].index == 4);
    LevelRuns none;
    CHECK(sobParts(none).empty());  // no record: no route
}

TEST_CASE("statistics screen: the charts' new pieces — the heat ramp, a level's things between two points, the window of a stretch") {
    // One hue, from transparent to hot: the opacity and the red grow with the value.
    float lastA = -1.0f;
    for (int i = 0; i <= 10; ++i) {
        const Color c = charts::heatOver(static_cast<float>(i) / 10.0f);
        CHECK(c.a >= lastA);
        CHECK(c.r == 255);
        lastA = c.a;
    }
    CHECK(charts::heatOver(0.0f).a == 0);
    CHECK(charts::heatSolid(1.0f).a == 255);
    statsart::LevelInfo li;
    using K = statsart::LevelInfo::Thing::Kind;
    li.things = {{K::Enemy, "wisp", 0.1f}, {K::Enemy, "wisp", 0.2f}, {K::Tablet, "boomerang", 0.05f}, {K::Enemy, "stalker", 0.3f},
                 {K::Crumble, "crumble", 0.45f}, {K::Boss, "mycelia", 0.9f}};
    std::stable_sort(li.things.begin(), li.things.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
    li.checkpoints = {{0.3f, "c1"}, {0.6f, "c2"}};
    li.labels = {{"c1", "Racines"}, {"c2", ""}};
    const auto first = li.between(0.0f, 0.3f);
    CHECK(first.size() == 2);  // the stalker at 0.3 is the next segment's
    CHECK(first[0].kind == K::Tablet && first[1].id == "wisp" && first[1].count == 2);
    const auto second = li.between(0.3f, 0.6f);
    CHECK(second.size() == 2 && second[0].id == "stalker" && second[1].kind == K::Crumble);
    const auto last = li.between(0.6f, 1.0f);
    CHECK(last.size() == 1 && last[0].kind == K::Boss);
    CHECK(li.checkpointName("", false) == "Départ");
    CHECK(li.checkpointName("", true) == "Arrivée");
    CHECK(li.checkpointName("c1", false) == "Racines");
    CHECK(li.checkpointName("c2", false) == "Point de passage 2");
    CHECK_NEAR(li.at("c2", false), 0.6, 1e-6);
    CHECK_NEAR(li.at("", true), 1.0, 1e-6);
    // The window of a stretch: at the picture's proportions, inside the level.
    li.bounds = {0, 0, 4000, 900};
    li.track.route = {{100, 600}, {2000, 600}, {2000, 300}, {3900, 300}};
    li.track.cumulative = {0, 1900, 2200, 4100};
    li.track.length = 4100;
    const Rect w = statsart::stretchWindow(li, 0.0f, 0.4f, 320.0f, 112.0f);
    CHECK_NEAR(w.w / w.h, 320.0 / 112.0, 0.01);
    CHECK(w.x >= -0.01f && w.right() <= 4000.01f && w.y >= -0.01f && w.bottom() <= 900.01f);
    CHECK(w.x <= 100.0f && w.right() >= 1640.0f);  // the stretch of the route is in it
}

namespace {

struct ScreenRig {
    Application app;
    std::unique_ptr<GameServices> g;
    ScreenRig(int w, int h, Language l) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        AppConfig cfg;
        cfg.headless = true;
        cfg.width = w;
        cfg.height = h;
        cfg.vsync = false;
        app.init(cfg);
        app.headlessDt = 1.0f / 30.0f;
        g = std::make_unique<GameServices>(app);
        g->loadAll();
        app.onSceneRender = [this](bool covered) { g->ui.sceneInput = !covered; };
        g->progression.tutorial = "finished";
        g->settings.gameplay.attractTrailer = false;
        g->progression.asterionFilmSeen = true;
        g->settings.language = i18n::code(l);
        g->applySettings();
    }
    ~ScreenRig() {
        i18n::stopAudit();
        app.shutdown();
        app.onSceneRender = nullptr;
        i18n::setLanguage(Language::French);
    }
};

}  // namespace

TEST_CASE("statistics screen: a level's pictures are drawn off the frame's path, then kept") {
    ScreenRig rig(1280, 720, Language::French);
    GameServices& g = *rig.g;
    statsart::LevelArt art(g);
    const Texture* seg = nullptr;
    const Texture* strip = nullptr;
    const statsart::LevelInfo* info = nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    while ((!seg || !strip) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30)) {
        art.update();
        info = art.info("aethra_01");
        seg = art.stretch("aethra_01", 0.0f, 0.25f, 160, 64);
        strip = art.sliceStrip("aethra_01", 12, 48);
        if (!seg || !strip) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(info && info->ok);
    CHECK(info->checkpoints.size() == 4);
    CHECK(info->labels.at(info->checkpoints.front().second) == "Racines");
    bool tablet = false, boss = false;
    for (const auto& t : info->things) {
        tablet = tablet || (t.kind == statsart::LevelInfo::Thing::Kind::Tablet && t.id == "boomerang");
        boss = boss || (t.kind == statsart::LevelInfo::Thing::Kind::Boss && t.id == "mycelia");
    }
    CHECK(tablet && boss);
    CHECK(seg && seg->width() == 160 && seg->height() == 64);
    CHECK(strip && strip->width() == 12 * kSlices && strip->height() == 48);
    // Asked again: the same picture, nothing drawn again.
    CHECK(art.stretch("aethra_01", 0.0f, 0.25f, 160, 64) == seg);
    CHECK(!art.failed("aethra_01"));
    // A level that does not exist: said so, never waited for.
    for (int i = 0; i < 2000 && !art.failed("no_such_level"); ++i) {
        art.info("no_such_level");
        art.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(art.failed("no_such_level"));
}

TEST_CASE("statistics screen: every tab drawn with statistics, 1280 x 720 and 1920 x 1080, French and English (nothing left in French)") {
    for (int size = 0; size < 2; ++size)
        for (Language lang : {Language::French, Language::English}) {
            ScreenRig rig(size ? 1920 : 1280, size ? 1080 : 720, lang);
            GameServices& g = *rig.g;
            const int today = g.playStats.today();
            g.playStats.setData(played(today));
            if (lang == Language::English) i18n::startAudit();
            rig.app.pushScene(makeMainMenu(g));
            rig.app.runFrames(2);
            for (int tab = 0; tab < 6; ++tab) {
                g.onlineOpen = "stats:runs=aethra_01,level=aethra_01";
                rig.app.pushScene(makeStatistics(g, tab));
                rig.app.runFrames(30);
                CHECK(rig.app.sceneCount() == 2);
                rig.app.popScene();
                rig.app.runFrames(1);
            }
            // The tables instead of the charts.
            g.onlineOpen = "stats:table=dph,table=stars";
            rig.app.pushScene(makeStatistics(g, 0));
            rig.app.runFrames(4);
            rig.app.popScene();
            g.onlineOpen = "stats:table=heat,level=aethra_01";
            rig.app.pushScene(makeStatistics(g, 4));
            rig.app.runFrames(4);
            rig.app.popScene();
            g.onlineOpen = "stats:table=phases,table=bosscurve,boss=mycelia";
            rig.app.pushScene(makeStatistics(g, 2));
            rig.app.runFrames(4);
            rig.app.popScene();
            rig.app.runFrames(1);
            if (lang == Language::English) {
                std::vector<i18n::AuditFinding> wrong = i18n::auditFindings();
                for (size_t i = 0; i < wrong.size() && i < 30; ++i)
                    std::fprintf(stderr, "  [%s] %s -> %s\n", wrong[i].reason.c_str(), wrong[i].text.c_str(), wrong[i].shown.c_str());
                CHECK(wrong.empty());
                i18n::stopAudit();
            }
        }
}
