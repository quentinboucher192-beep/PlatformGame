// Achievements (2.18): the catalogue is complete and consistent (ids, threads, stars, every
// condition reachable with the game's content), conditions read the real saves, unlocks
// follow the threads of the web, and the save file survives damage.

#include <cstdio>
#include <functional>
#include <set>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/online/Replay.h"
#include "game/progression/Progression.h"
#include "game/records/BossStats.h"
#include "game/records/Records.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct Fixture {
    ContentDatabase content;
    AchievementCatalog catalog;
    Progression progression;
    RecordsManager records;
    BossStats bosses;
    AchievementBook book{fs::temp_directory_path() / ("cosmic_ach_" + makeUuidV4().substr(0, 8) + ".json")};
    Fixture() {
        content.reload();
        std::string err;
        catalog.loadDefault(&err);
    }
    AchievementInputs inputs() {
        AchievementInputs in;
        in.content = &content;
        in.progression = &progression;
        in.records = &records;
        in.bosses = &bosses;
        in.stats = &book.stats();
        for (const PlanetData* p : content.planets())
            for (const LevelEntry* e : content.levelsForPlanet(p->id)) in.campaignLevels.push_back(e->id);
        return in;
    }
    std::vector<std::string> evaluate() {
        std::vector<std::string> ids;
        for (const AchievementDef* a : book.evaluate(catalog, inputs(), "2026-11-14 21:00")) ids.push_back(a->id);
        return ids;
    }
};

bool has(const std::vector<std::string>& v, const std::string& id) { return std::find(v.begin(), v.end(), id) != v.end(); }

}  // namespace

TEST_CASE("achievements: a large catalogue, every category, every star level") {
    Fixture f;
    CHECK(f.catalog.achievements().size() >= 120);
    CHECK(f.catalog.categories().size() >= 10);
    std::set<std::string> ids;
    std::map<std::string, int> perCategory;
    std::set<int> stars;
    for (const auto& a : f.catalog.achievements()) {
        CHECK(ids.insert(a.id).second);  // unique
        CHECK(!a.name.empty());
        CHECK(!a.description.empty());
        CHECK(a.stars >= 1 && a.stars <= 5);
        stars.insert(a.stars);
        ++perCategory[a.category];
        CHECK(f.catalog.category(a.category) != nullptr);
    }
    CHECK(stars.size() == 5);
    for (const auto& c : f.catalog.categories()) CHECK(perCategory[c.id] >= 5);
    std::printf("  %zu succès, %d étoiles, %zu catégories\n", f.catalog.achievements().size(), f.catalog.totalStars(), f.catalog.categories().size());
}

TEST_CASE("achievements: threads lead somewhere and never loop") {
    Fixture f;
    for (const auto& a : f.catalog.achievements())
        for (const auto& r : a.after) {
            CHECK(f.catalog.find(r) != nullptr);
            CHECK(r != a.id);
        }
    // No cycle: a depth-first walk never meets an achievement it is still visiting.
    std::map<std::string, int> state;
    bool cycle = false;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (state[id] == 2) return;
        if (state[id] == 1) {
            cycle = true;
            return;
        }
        state[id] = 1;
        if (const AchievementDef* d = f.catalog.find(id))
            for (const auto& r : d->after) visit(r);
        state[id] = 2;
    };
    for (const auto& a : f.catalog.achievements()) visit(a.id);
    CHECK(!cycle);
}

TEST_CASE("achievements: every condition is known and names real content") {
    Fixture f;
    const std::set<std::string> known = {"levels_completed", "level", "planet", "planets", "all_levels", "medal_count", "medal_level",
                                         "medal_planet_any", "stat", "max", "set", "levels_no_death", "level_no_death",
                                         "levels_full_collect", "levels_all_secrets", "boss", "boss_distinct", "boss_nohit_distinct",
                                         "boss_total", "boss_phase_nohit", "power_uses", "powers_distinct", "playtime", "user_levels",
                                         "user_enemies", "completion_proofs", "online_account", "friends"};
    for (const auto& a : f.catalog.achievements()) {
        const AchievementCondition& c = a.condition;
        if (!known.count(c.type)) std::printf("  condition inconnue : %s (%s)\n", c.type.c_str(), a.id.c_str());
        CHECK(known.count(c.type) == 1);
        if (!c.level.empty()) CHECK(f.content.level(c.level) != nullptr);
        if (!c.planet.empty()) CHECK(f.content.planet(c.planet) != nullptr);
        for (const auto& p : c.planets) CHECK(f.content.planet(p) != nullptr);
        if (!c.boss.empty()) {
            const EnemyDefinition* e = f.content.enemy(c.boss);
            CHECK(e != nullptr && e->isBoss);
        }
        if (!c.power.empty()) CHECK(f.content.power(c.power) != nullptr);
        if (c.type == "medal_count" || c.type == "levels_no_death" || c.type == "levels_full_collect" || c.type == "levels_all_secrets")
            CHECK(c.count <= 18);  // the campaign has 18 levels (2.19: Aethra)
    }
    // Every power of the game has its achievement: used (active powers, counted from their
    // « powerUsed » event) or owned at the end of levels (passive ones never fire it).
    const std::set<std::string> passive = {"triple_jump", "air_dash", "magnet"};
    for (const PowerDefinition* p : f.content.powers()) {
        const AchievementDef* a = f.catalog.find("pouvoirs_" + p->id);
        CHECK(a != nullptr);
        if (!a) continue;
        if (passive.count(p->id)) CHECK(a->condition.type == "stat" && a->condition.key == "carried_" + p->id);
        else CHECK(a->condition.type == "power_uses" && a->condition.power == p->id);
    }
}

TEST_CASE("achievements: nothing is unlocked without having done it") {
    Fixture f;
    auto got = f.evaluate();
    CHECK(got.empty());
    CHECK(f.book.stars(f.catalog) == 0);
}

TEST_CASE("achievements: campaign, medals and bosses come from the saves") {
    Fixture f;
    for (const char* id : {"sylvanox_01", "sylvanox_02", "sylvanox_03", "sylvanox_04"}) f.progression.markCompleted(id);
    LevelRecord& r = f.records.getOrCreate("sylvanox_01");
    r.completions = 1;
    r.bestTime = 100.0f;
    r.bestMedal = Medal::Gold;
    r.fewestDeaths = 0;
    BossRecord& b = f.bosses.at("thornwarden");
    b.kills = 1;
    b.noHitKills = 1;
    b.phaseNoHit = {1, 1, 0};
    auto got = f.evaluate();
    for (const char* id : {"voyage_first", "voyage_sylvanox", "chrono_bronze", "chrono_silver", "chrono_gold", "gardiens_first",
                           "gardiens_thornwarden", "maitrise_nodeath", "maitrise_phase", "maitrise_boss_nohit"})
        CHECK(has(got, id));
    CHECK(!has(got, "voyage_glacior"));
    CHECK(!has(got, "chrono_platinum"));
    // Threads: the three planets open « Le cœur s'ouvre » only once each planet is done.
    CHECK(!f.book.unlocked("voyage_core_open"));
    for (const char* p : {"glacior", "pyralis"})
        for (const LevelEntry* e : f.content.levelsForPlanet(p)) f.progression.markCompleted(e->id);
    got = f.evaluate();
    CHECK(has(got, "voyage_glacior"));
    CHECK(has(got, "voyage_pyralis"));
    CHECK(has(got, "voyage_core_open"));
    CHECK(f.book.unlockDate("voyage_core_open") == "2026-11-14 21:00");
    // Progress of what is left.
    const AchievementDef* all = f.catalog.find("voyage_all");
    AchievementProgress p = AchievementBook::progress(*all, f.inputs());
    CHECK(p.value == 12);
    CHECK(p.target == 18);  // 2.19: the four levels of Aethra
}

TEST_CASE("achievements: counters, sets and the required threads") {
    Fixture f;
    AchievementStats& s = f.book.stats();
    s.add("kills", 120);
    s.add("power_uses", 3);
    s.powerUses["dash"] = 60;
    s.insert("powers_used", "dash");
    s.raise("run_kills", 55);
    auto got = f.evaluate();
    for (const char* id : {"combat_10", "combat_100", "combat_run_50", "pouvoirs_first", "pouvoirs_dash"}) CHECK(has(got, id));
    CHECK(!has(got, "combat_500"));
    CHECK(!has(got, "pouvoirs_wave"));
    // A condition met before its thread is open waits for it: « Tornade » needs « Nettoyage ».
    Fixture g;
    g.book.stats().raise("run_kills", 120);
    auto got2 = g.evaluate();
    CHECK(!has(got2, "combat_run_50"));  // « Chasseur » (100 kills) is still missing
    CHECK(!g.book.unlocked("combat_run_100"));
    g.book.stats().add("kills", 100);
    got2 = g.evaluate();
    CHECK(has(got2, "combat_100"));
    CHECK(has(got2, "combat_run_50"));
    CHECK(has(got2, "combat_run_100"));
    // An achievement once unlocked stays unlocked (announced once).
    auto ann = g.book.takeAnnouncements();
    CHECK(!ann.empty());
    CHECK(g.book.takeAnnouncements().empty());
    CHECK(g.evaluate().empty());
}

TEST_CASE("achievements: the save survives a restart and resists damage") {
    fs::path file = fs::temp_directory_path() / ("cosmic_ach_" + makeUuidV4().substr(0, 8) + ".json");
    {
        Fixture f;
        AchievementBook book(file);
        book.stats().add("kills", 15);
        book.stats().insert("community_levels", "abc");
        book.stats().powerUses["wave"] = 7;
        AchievementInputs in = f.inputs();
        in.stats = &book.stats();
        book.evaluate(f.catalog, in, "2026-11-14 20:00");
        CHECK(book.unlocked("combat_10"));
        CHECK(book.save());
    }
    {
        AchievementBook back(file);
        back.load();
        CHECK(back.unlocked("combat_10"));
        CHECK(back.stats().counter("kills") == 15);
        CHECK(back.stats().setHas("community_levels", "abc"));
        CHECK(back.stats().powerUses["wave"] == 7);
    }
    // Hand-made values: negative, huge, wrong types, overlong keys.
    FileUtil::writeTextAtomic(file, R"({"format":"cosmic.achievement_book","unlocked":{"x":5,"combat_10":"2026"},
        "counters":{"kills":-50,"jumps":1e300,"deaths":"lots"},"sets":{"a":[1,2,"ok"]},"power_uses":{"dash":-3}})");
    AchievementBook bad(file);
    bad.load();
    CHECK(bad.stats().counter("kills") == 0);
    CHECK(bad.stats().counter("jumps") <= static_cast<int64_t>(1e15));
    CHECK(bad.stats().counter("deaths") == 0);
    CHECK(bad.stats().setSize("a") == 1);
    CHECK(bad.stats().powerUses["dash"] == 0);
    // Not JSON at all: an empty book, no crash.
    FileUtil::writeTextAtomic(file, "\x01\x02 pas du json");
    AchievementBook junk(file);
    junk.load();
    CHECK(junk.unlockedCount() == 0);
    std::error_code ec;
    fs::remove(file, ec);
}

TEST_CASE("achievements: an unknown condition (newer catalogue) never unlocks") {
    AchievementDef d;
    d.id = "futur";
    d.condition.type = "something_new";
    d.condition.count = 1;
    AchievementInputs in;
    CHECK(!AchievementBook::progress(d, in).done());
}
