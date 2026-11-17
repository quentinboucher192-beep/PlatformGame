// 2.52: the suggestions of the Succès screen (an algorithm on the player's own progress: the
// same state gives the same five, never an unlocked or impossible one, at most two per
// category), the achievements followed (five at most, saved, merged by union), the pause
// menu's measured column, and the HUD's « Ennemis vaincus : X / Y » in a run.

#include <algorithm>
#include <map>
#include <set>

#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/achievements/AchievementSuggestions.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/Hud.h"
#include "game/online/ProgressMerge.h"
#include "game/online/Replay.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "saves/Settings.h"
#include "tests/TestFramework.h"
#include "ui/screens/PauseLayout.h"

using namespace cosmic;

namespace {

fs::path tempFile(const char* tag) { return fs::temp_directory_path() / (std::string("cosmic_") + tag + "_" + makeUuidV4().substr(0, 8) + ".json"); }

/// A small catalogue: three categories, counters of every kind, a thread, an online one, an
/// unknown condition.
AchievementCatalog smallCatalog() {
    Json cats = Json::array({{{"id", "combat"}, {"name", "Combat"}}, {{"id", "tresors"}, {"name", "Trésors"}}, {{"id", "communaute"}, {"name", "Communauté"}}});
    auto ach = [](const char* id, const char* cat, int stars, Json cond, Json req = Json::array()) {
        return Json{{"id", id}, {"category", cat}, {"name", id}, {"description", "…"}, {"stars", stars}, {"condition", cond}, {"requires", req}};
    };
    Json list = Json::array({
        ach("kills_10", "combat", 1, {{"type", "stat"}, {"key", "kills"}, {"count", 10}}),
        ach("kills_100", "combat", 2, {{"type", "stat"}, {"key", "kills"}, {"count", 100}}, Json::array({"kills_10"})),
        ach("kills_1000", "combat", 3, {{"type", "stat"}, {"key", "kills"}, {"count", 1000}}),
        ach("jumps_50", "combat", 1, {{"type", "stat"}, {"key", "jumps"}, {"count", 50}}),
        ach("run_kills_30", "combat", 2, {{"type", "max"}, {"key", "run_kills"}, {"count", 30}}),
        ach("shards_20", "tresors", 1, {{"type", "stat"}, {"key", "shards"}, {"count", 20}}),
        ach("shards_500", "tresors", 3, {{"type", "stat"}, {"key", "shards"}, {"count", 500}}),
        ach("relics_5", "tresors", 2, {{"type", "stat"}, {"key", "relics"}, {"count", 5}}),
        ach("secrets_3", "tresors", 2, {{"type", "stat"}, {"key", "secrets"}, {"count", 3}}),
        ach("friends_1", "communaute", 1, {{"type", "friends"}, {"count", 1}}),
        ach("comment_1", "communaute", 1, {{"type", "stat"}, {"key", "comments_posted"}, {"count", 1}}),
        ach("future", "communaute", 1, {{"type", "not_yet_invented"}, {"count", 1}}),
    });
    fs::path f = tempFile("ach_catalog");
    FileUtil::writeTextAtomic(f, Json{{"format", "cosmic.achievements"}, {"categories", cats}, {"achievements", list}}.dump());
    AchievementCatalog c;
    std::string err;
    CHECK(c.load(f, &err));
    std::error_code ec;
    fs::remove(f, ec);
    return c;
}

std::vector<std::string> ids(const std::vector<AchievementSuggestion>& v) {
    std::vector<std::string> out;
    for (const auto& s : v) out.push_back(s.def->id);
    return out;
}

bool has(const std::vector<std::string>& v, const std::string& id) { return std::find(v.begin(), v.end(), id) != v.end(); }

}  // namespace

TEST_CASE("achievement suggestions: five at most, none unlocked or impossible, the same state gives the same list") {
    AchievementCatalog cat = smallCatalog();
    AchievementBook book;
    book.stats().add("kills", 10);  // kills_10 done (unlocked below), kills_100 at 10 %
    book.stats().add("jumps", 45);  // 90 %
    book.stats().add("shards", 18);
    AchievementInputs in;
    in.stats = &book.stats();
    book.evaluate(cat, in, "2026-11-16 10:00");
    CHECK(book.unlocked("kills_10"));
    SuggestionContext ctx;
    auto a = suggestAchievements(cat, book, in, ctx);
    auto b = suggestAchievements(cat, book, in, ctx);
    CHECK(a.size() == kSuggestionCount);
    CHECK(ids(a) == ids(b));  // deterministic
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].reason == b[i].reason);
        CHECK(!book.unlocked(a[i].def->id));
        CHECK(a[i].def->id != "future");  // an unknown condition is never suggested
        CHECK(!a[i].reason.empty());
    }
    // At most two per category, even though « combat » has the most candidates.
    std::map<std::string, int> per;
    for (const auto& s : a) per[s.def->category]++;
    for (const auto& [k, n] : per) CHECK(n <= kSuggestionsPerCategory);
    // The closest ones come first, with their real numbers.
    CHECK(has(ids(a), "jumps_50"));
    CHECK(has(ids(a), "shards_20"));
    for (const auto& s : a)
        if (s.def->id == "jumps_50") CHECK(s.reason == "Plus que 5 sauts");
        else if (s.def->id == "shards_20") CHECK(s.reason == "Plus que 2 éclats");
    // Offline, what needs Cosmic Online comes after the rest (here only to fill the fifth place:
    // the other categories have given their two).
    bool online = false;
    for (const auto& s : a) {
        if (online) CHECK(achievementNeedsOnline(s.def->condition));
        online = online || achievementNeedsOnline(s.def->condition);
    }
    CHECK(online);
    // A smaller count, and nothing left to suggest.
    CHECK(suggestAchievements(cat, book, in, ctx, 2).size() == 2);
    AchievementBook done;
    done.fromJson(Json{{"unlocked", {{"kills_10", ""}, {"kills_100", ""}, {"kills_1000", ""}, {"jumps_50", ""}, {"run_kills_30", ""},
                                     {"shards_20", ""}, {"shards_500", ""}, {"relics_5", ""}, {"secrets_3", ""}, {"friends_1", ""},
                                     {"comment_1", ""}}}});
    CHECK(suggestAchievements(cat, done, in, ctx).empty());
}

TEST_CASE("achievement suggestions: requirements, progress, online and what was played last") {
    AchievementCatalog cat = smallCatalog();
    AchievementBook book;
    book.stats().add("kills", 95);  // kills_100 would be 95 %, but it needs kills_10 unlocked first
    AchievementInputs in;
    in.stats = &book.stats();
    SuggestionContext ctx;
    auto s = ids(suggestAchievements(cat, book, in, ctx, 20));
    CHECK(!has(s, "kills_100"));
    CHECK(!has(s, "kills_10"));  // done (unlocked at the next check): nothing to aim at
    book.evaluate(cat, in, "2026-11-16 10:00");
    s = ids(suggestAchievements(cat, book, in, ctx, 20));
    CHECK(has(s, "kills_100"));
    for (const auto& x : suggestAchievements(cat, book, in, ctx, 20))
        if (x.def->id == "kills_100") {
            CHECK(x.reason == "Plus que 5 ennemis");
            CHECK(x.progress.value == 95);
            CHECK(x.progress.target == 100);
        }
    // Connected: the online ones may come (the friends one with a real reason).
    ctx.online = true;
    for (const auto& x : suggestAchievements(cat, book, in, ctx, 20))
        if (x.def->id == "friends_1") CHECK(x.reason == "Pas encore commencé");
    ctx.online = false;
    for (const auto& x : suggestAchievements(cat, book, in, ctx, 20))
        if (x.def->id == "friends_1") CHECK(x.reason == "Avec Cosmic Online (compte gratuit)");
    // A percentage when less than half is done.
    book.stats().add("shards", 100);  // shards_500: 20 %
    for (const auto& x : suggestAchievements(cat, book, in, ctx, 20))
        if (x.def->id == "shards_500") CHECK(x.reason == "20 % fait");
}

TEST_CASE("achievement suggestions: levels, planets and bosses of the real campaign") {
    ContentDatabase content;
    content.reload();
    AchievementCatalog cat;
    CHECK(cat.loadDefault());
    Progression progression;
    progression.unlockAll = false;
    RecordsManager records;
    BossStats bosses;
    AchievementBook book;
    book.fromJson(Json{{"unlocked", {{"gardiens_first", "2026-11-16 09:00"}}}});  // the bosses' thread is open
    AchievementInputs in;
    in.content = &content;
    in.progression = &progression;
    in.records = &records;
    in.bosses = &bosses;
    in.stats = &book.stats();
    for (const PlanetData* p : content.planets())
        for (const LevelEntry* e : content.levelsForPlanet(p->id)) in.campaignLevels.push_back(e->id);
    SuggestionContext ctx;
    ctx.recentLevel = "sylvanox_01";
    ctx.recentPlanet = "sylvanox";
    ctx.bossLevelsKnown = true;  // no boss located: those never met are out
    auto list = suggestAchievements(cat, book, in, ctx, 40);
    CHECK(!list.empty());
    for (const auto& s : list) {
        const AchievementCondition& c = s.def->condition;
        // A level suggested is playable now; a boss never met and nowhere found is never suggested.
        if (!c.level.empty()) {
            const LevelEntry* e = content.level(c.level);
            CHECK(e != nullptr);
            if (e) CHECK(progression.levelUnlocked(*e, content));
        }
        if (c.type == "boss") CHECK(false);
        CHECK(book.available(*s.def));
    }
    std::map<std::string, int> per;
    for (const auto& s : suggestAchievements(cat, book, in, ctx)) per[s.def->category]++;
    for (const auto& [k, n] : per) CHECK(n <= kSuggestionsPerCategory);
    // A boss already fought is suggested with its own numbers.
    bosses.at("thornwarden").fights = 3;
    bool found = false;
    for (const auto& s : suggestAchievements(cat, book, in, ctx, 300))
        if (s.def->id == "gardiens_thornwarden") {
            found = true;
            CHECK(s.reason == "Affronté 3 fois, pas encore vaincu");
        }
    CHECK(found);
}

TEST_CASE("achievement suggestions: what moves during a run, what needs Cosmic Online, the numbers") {
    AchievementCondition c;
    c.type = "stat";
    c.key = "kills";
    CHECK(achievementMovesDuringRun(c));
    c.key = "kills_flying";
    CHECK(achievementMovesDuringRun(c));
    c.key = "runs_completed";  // counted when the level is finished
    CHECK(!achievementMovesDuringRun(c));
    c.type = "boss";
    CHECK(achievementMovesDuringRun(c));  // the bosses' records are written as each fight ends
    c.type = "medal_count";
    CHECK(!achievementMovesDuringRun(c));
    CHECK(!achievementNeedsOnline(c));
    c.type = "friends";
    CHECK(achievementNeedsOnline(c));
    c.type = "set";
    c.key = "community_levels";
    CHECK(achievementNeedsOnline(c));
    AchievementDef d;
    d.condition.type = "stat";
    CHECK(achievementProgressText(d, {1234, 5000}) == "1 234 / 5 000");
    CHECK(achievementProgressText(d, {7000, 5000}) == "5 000 / 5 000");  // never above the target
    d.condition.type = "playtime";
    CHECK(achievementProgressText(d, {3600, 7200}) == playTimeText(3600) + " / " + playTimeText(7200));
}

TEST_CASE("achievements followed: five at most, unlocked ones let go, saved and read back safely") {
    AchievementCatalog cat = smallCatalog();
    fs::path f = tempFile("ach_tracked");
    {
        AchievementBook book(f);
        for (const char* id : {"kills_10", "kills_100", "jumps_50", "shards_20", "relics_5"}) CHECK(book.track(id));
        CHECK(book.tracked().size() == AchievementBook::kMaxTracked);
        CHECK(!book.track("secrets_3"));  // a sixth: refused
        CHECK(!book.isTracked("secrets_3"));
        CHECK(book.track("jumps_50"));  // already followed: still fine, not twice
        CHECK(book.tracked().size() == 5);
        book.untrack("kills_100");
        CHECK(!book.isTracked("kills_100"));
        CHECK(book.track("secrets_3"));
        CHECK(book.tracked() == std::vector<std::string>({"kills_10", "jumps_50", "shards_20", "relics_5", "secrets_3"}));
        // Unlocked: no longer followed, and cannot be ticked again.
        book.stats().add("jumps", 50);
        AchievementInputs in;
        in.stats = &book.stats();
        book.evaluate(cat, in, "2026-11-16 11:00");
        CHECK(book.unlocked("jumps_50"));
        CHECK(!book.isTracked("jumps_50"));
        CHECK(!book.track("jumps_50"));
        CHECK(book.tracked().size() == 4);
        CHECK(book.save());
    }
    {
        AchievementBook back(f);
        back.load();
        CHECK(back.tracked() == std::vector<std::string>({"kills_10", "shards_20", "relics_5", "secrets_3"}));
    }
    // A damaged save: wrong types, duplicates, too many, unlocked ones, too long ids.
    AchievementBook junk;
    junk.fromJson(Json{{"unlocked", {{"a", "2026-11-01"}}},
                       {"tracked", Json::array({"a", 3, "b", "b", "c", std::string(80, 'x'), "d", "e", "f", "g"})}});
    CHECK(junk.tracked() == std::vector<std::string>({"b", "c", "d", "e", "f"}));
    AchievementBook none;
    none.fromJson(Json{{"tracked", "kills_10"}});
    CHECK(none.tracked().empty());
    std::error_code ec;
    fs::remove(f, ec);
}

TEST_CASE("achievements followed: a migration unites them (this computer's first, none unlocked, five at most)") {
    Json mine = {{"unlocked", {{"a", "2026-11-01"}}}, {"tracked", {"x", "y"}}};
    Json theirs = {{"unlocked", {{"y", "2026-11-02"}}}, {"tracked", {"z", "x", "a", "w", "v", "u"}}};
    Json m = mergeAchievementsJson(mine, theirs);
    // y is now unlocked (theirs), a was unlocked here: both left out.
    CHECK(m["tracked"] == Json::array({"x", "z", "w", "v", "u"}));
    AchievementBook b;
    b.fromJson(m);
    CHECK(b.tracked().size() == 5);
    CHECK(mergeAchievementsJson(Json::object(), Json::object())["tracked"].empty());
}

TEST_CASE("pause menu: the column is measured, shrinks on short windows and never overflows") {
    // 1080p, the usual six buttons, nothing followed: the comfortable sizes.
    PauseLayout l = pauseLayout(6, 0, false, 1.0f, 1080.0f);
    CHECK_NEAR(l.buttonH, 64.0f, 1e-3);
    CHECK_NEAR(l.gap, 16.0f, 1e-3);
    CHECK_NEAR(l.height, l.sectionTop(6) + l.sectionHeight(0, false) + l.footer, 1e-3);
    CHECK(l.height <= 1040.0f);
    // Every button, five achievements and the note: everything shrinks, the whole column fits.
    for (float h : {1080.0f, 900.0f, 720.0f}) {
        const float scale = std::max(0.5f, std::min(1.0f, h / 1080.0f));
        PauseLayout t = pauseLayout(9, 5, true, scale, h);
        CHECK(t.height * scale <= h - 40.0f * scale + 0.01f);
        CHECK(t.buttonH >= 48.0f - 1e-3);
        CHECK(t.buttonH < 64.0f);
        CHECK(t.rowH >= 36.0f - 1e-3);
        CHECK_NEAR(t.height, t.sectionTop(9) + t.sectionHeight(5, true) + t.footer, 1e-3);
        // The rows start after the last button, the hints after the rows.
        CHECK(t.sectionTop(9) >= t.top + 9.0f * (t.buttonH + t.gap) - 1e-3);
    }
    // A taller window keeps the comfortable sizes for the same content.
    PauseLayout tall = pauseLayout(9, 5, true, 1.0f, 1600.0f);
    CHECK_NEAR(tall.buttonH, 64.0f, 1e-3);
    // More rows never make a shorter panel.
    CHECK(pauseLayout(6, 3, false, 1.0f, 1600.0f).height > pauseLayout(6, 0, false, 1.0f, 1600.0f).height);
    // A comfortable column keeps a line of description under each name; the tightest does not.
    CHECK(pauseLayout(6, 5, true, 1.0f, 1600.0f).rowDescription());
    CHECK_FALSE(pauseLayout(9, 5, true, 0.6667f, 720.0f).rowDescription());
    // Fewer than five followed: a line says how many places are free.
    PauseLayout four = pauseLayout(6, 4, false, 1.0f, 1600.0f);
    CHECK_NEAR(four.sectionHeight(4, false), four.headerH + 4.0f * four.rowH + four.emptyH, 1e-3);
}

TEST_CASE("pause menu (2.60): under the map, the five places and their descriptions fit with the map") {
    // With the map on the right, the column keeps only the buttons.
    PauseLayout col = pauseLayout(9, 5, true, 1.0f, 1080.0f, false);
    CHECK_NEAR(col.sectionHeight(5, true), 0.0f, 1e-3);
    CHECK_NEAR(col.height, col.sectionTop(9) + col.footer, 1e-3);
    // Five achievements on two lines each, the note: the map keeps 160 px, its hint and its
    // legend on two lines, in a 1080p window (1040 px of panel at most).
    PauseSideTracked M;
    const float five = M.height({2, 2, 2, 2, 2}, true);
    CHECK(96.0f + 160.0f + 40.0f + 52.0f + 22.0f + 24.0f + five <= 1040.0f);
    // Every place is drawn: a free place takes room too, never more than a followed one.
    CHECK(M.height({}, false) > M.headerH);
    CHECK(M.height({1}, false) >= M.height({}, false) - (M.freeH + 8.0f) + M.rowHeight(1) - 1e-3);
    CHECK(M.freeH + 8.0f <= M.rowHeight(1));
    // Six rows are never asked; only five places exist.
    CHECK_NEAR(M.height({1, 1, 1, 1, 1, 1}, false), M.height({1, 1, 1, 1, 1}, false), 1e-3);
}

TEST_CASE("hud: « Ennemis vaincus » counts the run's kills against the level's enemies, as « Aucun survivant »") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    LevelData l = LevelData::makeEmpty("test_enemies_hud", "Test", "sylvanox");
    l.intro.steps.clear();
    for (int i = 0; i < 3; ++i) {
        LevelObject o;
        o.id = "e" + std::to_string(i);
        o.type = "enemy";
        o.layer = "gameplay";
        o.rect = {600.0f + 120.0f * static_cast<float>(i), 790.0f, 14.0f, 10.0f};
        o.props = {{"enemy", "glowmite"}, {"patrol", 16}};
        l.objects.push_back(o);
    }
    WorldServices s{res, audio, content, nullptr, 1.0f, 1.0f};
    GameWorld w(s, WorldMode::Arena);
    w.load(l);
    constexpr float dt = 1.0f / 120.0f;
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, dt);
    CHECK(w.stats().totalEnemies == 3);
    CHECK(w.stats().kills == 0);
    CHECK(Hud::enemiesText(w.stats().kills, w.stats().totalEnemies) == "Ennemis vaincus : 0 / 3");
    CHECK(!Hud::enemiesCleared(w.stats().kills, w.stats().totalEnemies));
    // Two of them beaten.
    DamageInfo big;
    big.amount = 999.0f;
    big.fromPlayer = true;
    w.damageEnemies({560.0f, 600.0f, 200.0f, 400.0f}, big, nullptr);  // the first two (x 600 and 720)
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, dt);
    CHECK(w.stats().kills == 2);
    CHECK(Hud::enemiesText(w.stats().kills, w.stats().totalEnemies) == "Ennemis vaincus : 2 / 3");
    CHECK(!Hud::enemiesCleared(w.stats().kills, w.stats().totalEnemies));
    // The last one: every enemy beaten, as the achievements' « cleared_levels » rule says.
    w.damageEnemies({0.0f, 0.0f, 4000.0f, 2000.0f}, big, nullptr);
    for (int i = 0; i < 30; ++i) w.fixedUpdate(PlayerInput{}, dt);
    const RunStats& st = w.stats();
    CHECK(st.kills == 3);
    CHECK(Hud::enemiesText(st.kills, st.totalEnemies) == "Ennemis vaincus : 3 / 3");
    CHECK(Hud::enemiesCleared(st.kills, st.totalEnemies) == (st.totalEnemies > 0 && st.kills >= st.totalEnemies));
    CHECK(Hud::enemiesCleared(st.kills, st.totalEnemies));
    // A level without enemies has nothing to count.
    CHECK(!Hud::enemiesCleared(0, 0));
}

TEST_CASE("hud: « Ennemis vaincus » is an element of the HUD editor, saved like the others") {
    GameplaySettings g;
    CHECK(std::string(HudLayout::id(HudElement::Enemies)) == "enemies");
    CHECK(std::string(HudLayout::label(HudElement::Enemies)) == "Ennemis vaincus");
    CHECK(hudVisible(g, HudElement::Enemies));        // shown by default
    CHECK(!g.hud[HudElement::Enemies].focus);          // not in « Mode concentré » unless chosen
    g.focusMode = true;
    CHECK(!hudVisible(g, HudElement::Enemies));
    g.focusMode = false;
    Settings s;
    s.gameplay.hud[HudElement::Enemies] = {true, 0.1f, 0.4f, 1.25f, true, true};
    Settings r = Settings::fromJson(s.toJson());
    const HudPlace& p = r.gameplay.hud[HudElement::Enemies];
    CHECK(p.custom);
    CHECK_NEAR(p.x, 0.1f, 1e-5);
    CHECK_NEAR(p.y, 0.4f, 1e-5);
    CHECK_NEAR(p.scale, 1.25f, 1e-5);
    CHECK(p.focus);
    s.gameplay.hud[HudElement::Enemies].visible = false;
    CHECK(!hudVisible(Settings::fromJson(s.toJson()).gameplay, HudElement::Enemies));
    // A settings file from before 2.52: the element at its default place, shown.
    Settings old = Settings::fromJson(Json::parse(R"({"gameplay":{"hud":{"timer":{"scale":1.0,"visible":true}}}})"));
    CHECK(!old.gameplay.hud[HudElement::Enemies].custom);
    CHECK(hudVisible(old.gameplay, HudElement::Enemies));
}
