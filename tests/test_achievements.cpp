// Achievements (2.18): the catalogue is complete and consistent (ids, threads, stars, every
// condition reachable with the game's content), conditions read the real saves, unlocks
// follow the threads of the web, and the save file survives damage.

#include <cstdio>
#include <functional>
#include <set>

#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/cosmetics/Collection.h"
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
    CHECK(f.catalog.achievements().size() >= 221);  // 2.24: 72 more, and the Collection category; 2.33: Singularis
    CHECK(f.catalog.totalStars() >= 612);
    CHECK(f.catalog.categories().size() >= 13);
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
                                         "user_enemies", "completion_proofs", "online_account", "friends",
                                         // 2.24
                                         "medal_planet", "boss_phases_nohit", "tracks_heard", "creatures_met"};
    // Every counter, set and maximum the game really writes (GameScene, ReplayScene,
    // CollectionScene, CommentsPanel, RatingWidgets, OnlineService…): a name nobody writes
    // would never unlock.
    std::set<std::string> counters = {"kills", "deaths", "shards", "relics", "secrets", "jumps", "power_uses", "runs_completed",
                                      "extreme_attempts", "extreme_finished", "pb_improved", "ghost_races", "ghost_pack_races",
                                      "gold_segments", "pb_ghost_wins", "world_ghost_wins", "friend_ghost_wins", "world_ghost_races",
                                      "photo_finish", "exports", "completion_proofs", "race_verified", "replay_compares", "replay_aligns",
                                      "comments_posted", "menu_music"};
    for (const char* family : {"ground", "robot", "flying", "turret", "fast", "heavy", "caster", "miniboss"}) counters.insert(std::string("kills_") + family);
    for (const PowerDefinition* p : f.content.powers()) counters.insert("carried_" + p->id);
    const std::set<std::string> sets = {"cleared_levels", "no_death_levels", "no_damage_levels", "no_power_levels", "full_collect_levels",
                                        "all_secrets_levels", "powers_owned", "powers_used", "community_levels", "rated_levels",
                                        "published_levels", "published_extreme", "rated_campaign", "equipped_items", "photographed_planets"};
    const std::set<std::string> maxima = {"run_kills", "level_attempts", "outfit_categories"};
    // What the campaign holds, for the counts below.
    std::map<std::string, int> withCollectibles, withSecrets;  // per planet ("" = the whole campaign)
    std::set<std::string> bestiary;                             // creatures placed in the campaign (and what they summon)
    for (const PlanetData* p : f.content.planets())
        for (const LevelEntry* e : f.content.levelsForPlanet(p->id)) {
            auto lv = f.content.loadLevel(e->id);
            CHECK(lv.has_value());
            if (!lv) continue;
            bool col = false, sec = false;
            for (const auto& o : lv->objects) {
                col = col || o.type == "shard" || o.type == "relic";
                sec = sec || o.type == "secret_area";
                if (o.type != "enemy") continue;
                if (const EnemyDefinition* d = f.content.enemy(jstr(o.props, "enemy")); d && !d->isBoss) {
                    bestiary.insert(d->id);
                    for (const auto& atk : d->attacks)
                        if (atk.type == AttackType::Summon && !atk.summonId.empty()) bestiary.insert(atk.summonId);
                }
            }
            for (const std::string& k : {std::string(), p->id}) {
                withCollectibles[k] += col ? 1 : 0;
                withSecrets[k] += sec ? 1 : 0;
            }
        }
    int activePowers = 0;
    for (const PowerDefinition* p : f.content.powers()) activePowers += p->id == "triple_jump" || p->id == "air_dash" || p->id == "magnet" ? 0 : 1;
    int tracks = 0;
    if (auto j = FileUtil::loadJson(Paths::dataDir() / "music" / "tracks.json"); j && j->contains("tracks")) tracks = static_cast<int>((*j)["tracks"].size());
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
            CHECK(c.count <= 21);  // the campaign has 21 levels (2.19: Aethra, 2.33: the dying star, 2.48-2.49: Les Confins)
        if (!c.planet.empty() && (c.type == "levels_no_death" || c.type == "levels_full_collect" || c.type == "levels_all_secrets"))
            CHECK(c.count <= static_cast<int64_t>(f.content.levelsForPlanet(c.planet).size()));
        // Reachable: only levels with something to find count for « tout ramasser » and
        // « tous les secrets » (the boss levels and the Noyau have neither).
        if (c.type == "levels_full_collect") CHECK(c.count <= withCollectibles[c.planet]);
        if (c.type == "levels_all_secrets") CHECK(c.count <= withSecrets[c.planet]);
        if (c.type == "medal_planet") CHECK(!c.planet.empty() && c.medal >= 1);
        if (c.type == "boss_phases_nohit") {
            const EnemyDefinition* e = f.content.enemy(c.boss);
            CHECK(e != nullptr && !e->phases.empty());
        }
        if (c.type == "powers_distinct") CHECK(c.count <= activePowers);  // passive powers never fire « powerUsed »
        if (c.type == "tracks_heard") CHECK(c.count <= tracks);
        if (c.type == "creatures_met") CHECK(c.count <= static_cast<int64_t>(bestiary.size()));
        if (c.type == "stat") {
            if (!counters.count(c.key)) std::printf("  compteur inconnu : %s (%s)\n", c.key.c_str(), a.id.c_str());
            CHECK(counters.count(c.key) == 1);
        }
        if (c.type == "set") CHECK(sets.count(c.key) == 1);
        if (c.type == "max") CHECK(maxima.count(c.key) == 1);
        if (c.type == "set" && c.key == "rated_campaign") CHECK(c.count <= 21);  // 2.49: 21 levels
        if (c.type == "set" && c.key == "photographed_planets") CHECK(c.count <= static_cast<int64_t>(f.content.planets().size()));
        if (c.type == "max" && c.key == "outfit_categories") CHECK(c.count <= 5);  // the five categories of the Collection
    }
    CHECK(bestiary.size() >= 30);
    CHECK(tracks >= 19);
    // Every power of the game has its achievement: used (active powers, counted from their
    // « powerUsed » event) or owned at the end of levels (passive ones never fire it).
    const std::set<std::string> passive = {"triple_jump", "air_dash", "magnet"};
    // 2.50: the Boomerang chargé gets its achievements with the catalogue of its release (added
    // by the integration, not with the power): until then it is only listed.
    const std::set<std::string> pending = {"boomerang_charge"};
    for (const PowerDefinition* p : f.content.powers()) {
        if (pending.count(p->id)) {
            CHECK(f.catalog.find("pouvoirs_" + p->id) == nullptr);  // once added, take it out of `pending`
            continue;
        }
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
    CHECK(p.target == 21);  // 2.19: the four levels of Aethra; 2.33: Le Cœur d'une Étoile Mourante; 2.48-2.49: Les Confins
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

TEST_CASE("achievements (2.24): Aethra's medals and levels, a guardian phase by phase, enemies by family") {
    Fixture f;
    std::vector<std::string> aethra;
    for (const LevelEntry* e : f.content.levelsForPlanet("aethra")) aethra.push_back(e->id);
    CHECK(aethra.size() == 4);
    if (aethra.size() != 4) return;
    // Gold on three of the four levels of Aethra: 3 / 4, not yet.
    for (size_t i = 0; i < aethra.size(); ++i) {
        f.progression.markCompleted(aethra[i]);
        LevelRecord& r = f.records.getOrCreate(aethra[i]);
        r.completions = 1;
        r.bestMedal = i < 3 ? Medal::Gold : Medal::Silver;
    }
    const AchievementDef* gold = f.catalog.find("chrono_aethra_gold");
    CHECK(gold != nullptr);
    if (!gold) return;
    AchievementProgress p = AchievementBook::progress(*gold, f.inputs());
    CHECK(p.value == 3);
    CHECK(p.target == 4);
    auto got = f.evaluate();
    CHECK(!has(got, "chrono_aethra_gold"));
    CHECK(has(got, "voyage_aethra_first") == false);  // its thread (« Le cœur s'ouvre ») is not open
    f.records.getOrCreate(aethra[3]).bestMedal = Medal::Gold;
    got = f.evaluate();
    CHECK(has(got, "chrono_aethra_gold"));
    CHECK(!has(got, "chrono_aethra_platinum"));
    // Without dying: the levels of another planet never count for Aethra, each level once.
    AchievementStats& s = f.book.stats();
    for (const char* other : {"sylvanox_01", "sylvanox_02", "glacior_01", "pyralis_01"}) s.insert("no_death_levels", other);
    s.insert("no_death_levels", aethra[0]);
    f.records.getOrCreate(aethra[0]).fewestDeaths = 0;  // the same level, also from its record
    f.records.getOrCreate(aethra[1]).fewestDeaths = 0;
    const AchievementDef* nd = f.catalog.find("maitrise_aethra_nodeath");
    CHECK(nd != nullptr);
    if (nd) {
        p = AchievementBook::progress(*nd, f.inputs());
        CHECK(p.value == 2);
        CHECK(p.target == 4);
    }
    // Everything collected, every secret: only Aethra's levels again.
    const AchievementDef* col = f.catalog.find("tresors_aethra_full");
    CHECK(col != nullptr);
    if (col) {
        LevelRecord& r = f.records.getOrCreate(aethra[2]);
        r.totalCollectibles = 50;
        r.mostCollectibles = 50;
        s.insert("full_collect_levels", "glacior_02");
        CHECK(AchievementBook::progress(*col, f.inputs()).value == 1);
    }
    // A guardian phase by phase: each phase once without being hit, in any fight.
    const AchievementDef* ph = f.catalog.find("maitrise_phases_mycelia");
    CHECK(ph != nullptr);
    if (ph) {
        BossRecord& b = f.bosses.at("mycelia");
        b.kills = 2;
        b.phaseNoHit = {1, 0, 2};
        p = AchievementBook::progress(*ph, f.inputs());
        CHECK(p.value == 2);
        CHECK(p.target == static_cast<int64_t>(f.content.enemy("mycelia")->phases.size()));
        got = f.evaluate();
        CHECK(has(got, "gardiens_mycelia"));
        CHECK(!has(got, "maitrise_phases_mycelia"));
        b.phaseNoHit = {1, 1, 2};
        got = f.evaluate();
        CHECK(has(got, "maitrise_phases_mycelia"));
        CHECK(!has(got, "maitrise_mycelia_nohit"));  // the whole fight without a hit is the next step
        b.noHitKills = 1;
        CHECK(has(f.evaluate(), "maitrise_mycelia_nohit"));
    }
    // Enemies by family (GameWorld counts them, GameScene adds them up at the end of a run).
    s.add("kills", 150);
    s.add("kills_robot", 149);
    got = f.evaluate();
    CHECK(has(got, "combat_100"));
    CHECK(!has(got, "combat_robot_150"));
    s.add("kills_robot", 1);
    CHECK(has(f.evaluate(), "combat_robot_150"));
}

TEST_CASE("achievements (2.24): the Collection, the replays and Cosmic Online") {
    Fixture f;
    AchievementInputs in = f.inputs();
    in.tracksHeard = 9;
    in.creaturesMet = 14;
    auto run = [&](AchievementInputs i) {
        std::vector<std::string> ids;
        for (const AchievementDef* a : f.book.evaluate(f.catalog, i, "2026-11-15 10:00")) ids.push_back(a->id);
        return ids;
    };
    auto got = run(in);
    CHECK(!has(got, "collection_tracks_10"));
    CHECK(!has(got, "collection_creatures_15"));
    in.tracksHeard = 22;  // 2.33: Singularis's track; 2.48: Pyrrhax's; 2.49: Monolithe's
    in.creaturesMet = 30;
    got = run(in);
    for (const char* id : {"collection_tracks_10", "collection_tracks_all", "collection_creatures_15", "collection_creatures_30"}) CHECK(has(got, id));
    // Worn rewards: distinct items, and the most categories worn at once.
    AchievementStats& s = f.book.stats();
    s.insert("equipped_items", "suit_givre");
    s.insert("equipped_items", "suit_givre");
    s.raise("outfit_categories", 4);
    got = run(in);
    CHECK(has(got, "collection_equip"));
    CHECK(!has(got, "collection_equip_10"));
    CHECK(!has(got, "collection_outfit"));
    s.raise("outfit_categories", 5);
    CHECK(has(run(in), "collection_outfit"));
    // The jukebox's menu music, the gallery's pictures (each planet once).
    s.add("menu_music");
    for (const char* p : {"sylvanox", "glacior", "pyralis", "noyau", "noyau"}) s.insert("photographed_planets", p);
    got = run(in);
    CHECK(has(got, "collection_menu_music"));
    CHECK(has(got, "collection_photo"));
    CHECK(!has(got, "collection_photo_all"));
    s.insert("photographed_planets", "aethra");
    CHECK(has(run(in), "collection_photo_all"));
    // Replays compared and aligned.
    s.add("replay_compares");
    got = run(in);
    CHECK(has(got, "fantomes_compare"));
    CHECK(!has(got, "fantomes_align"));
    s.add("replay_aligns");
    CHECK(has(run(in), "fantomes_align"));
    // Cosmic Online: comments and the campaign's ratings need the account's achievement first.
    s.add("comments_posted");
    s.insert("rated_campaign", "sylvanox_01");
    got = run(in);
    CHECK(!has(got, "communaute_comment"));
    CHECK(!has(got, "communaute_rate_campaign"));
    in.onlineAccount = true;
    got = run(in);
    CHECK(has(got, "communaute_account"));
    CHECK(has(got, "communaute_comment"));
    CHECK(has(got, "communaute_rate_campaign"));
    CHECK(!has(got, "communaute_rate_campaign_all"));
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

TEST_CASE("collection: the items sent to the online profile, the looks of other players checked") {
    CosmeticCatalog c;
    std::string err;
    CHECK(c.loadDefault(&err));
    CollectionBook book(fs::temp_directory_path() / ("cosmic_col_" + makeUuidV4().substr(0, 8) + ".json"));
    CosmeticProgress p;
    p.totalStars = 597;
    // Nothing chosen: the default of each of the five categories.
    auto ids = book.equippedIds(c, p);
    CHECK(ids.size() == 5);
    CHECK(ids["suit"] == "suit_standard");
    CHECK(ids["trail"] == "trail_none");
    CHECK(ids["frame"] == "frame_simple");
    CHECK(ids["title"] == "title_none");
    CHECK(ids["victory"] == "victory_classic");
    // A locked or unknown choice is never sent: the default goes instead.
    book.equip("frame", "frame_gold");
    book.equip("suit", "suit_futur");
    book.equip("title", "title_explorateur");
    book.equip("trail", "frame_bronze");  // an item of another category
    p.stars = 40;
    ids = book.equippedIds(c, p);
    CHECK(ids["frame"] == "frame_simple");
    CHECK(ids["suit"] == "suit_standard");
    CHECK(ids["title"] == "title_none");  // its achievement is not unlocked
    CHECK(ids["trail"] == "trail_none");
    p.stars = 150;
    CHECK(book.equippedIds(c, p)["frame"] == "frame_gold");
    // What another player's data may make the game draw: only looks of the category.
    CHECK(c.lookOr("frame", "gold") == "gold");
    CHECK(c.lookOr("frame", "constellation") == "constellation");
    CHECK(c.lookOr("frame", "rainbow") == "simple");
    CHECK(c.lookOr("frame", "") == "simple");
    CHECK(c.lookOr("suit", "givre") == "givre");
    CHECK(c.lookOr("suit", "../givre") == "standard");
    CHECK(c.lookOr("suit", "gold") == "standard");  // a frame's look is not a suit
    CHECK(c.lookOr("nothing", "gold").empty());
    // 2.24: the new looks are known too.
    CHECK(c.lookOr("frame", "laurels") == "laurels");
    CHECK(c.lookOr("trail", "runes") == "runes");
    CHECK(c.lookOr("suit", "sanctuaire") == "sanctuaire");
    CHECK(c.lookOr("victory", "corona") == "corona");
}

TEST_CASE("collection (2.24): every reward can be won and is drawn") {
    CosmeticCatalog c;
    std::string err;
    CHECK(c.loadDefault(&err));
    AchievementCatalog a;
    CHECK(a.loadDefault(&err));
    const int total = a.totalStars();
    // Stars every player can win without Cosmic Online: every star milestone but « toutes les
    // étoiles » stays below them. Online: the Communauté category, publishing, the
    // verification race, the ghosts of the world record and of friends, and what follows them.
    std::set<std::string> online = {"createur_publish", "extreme_verify", "fantomes_world", "fantomes_friend"};
    for (const auto& d : a.achievements())
        if (d.category == "communaute") online.insert(d.id);
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& d : a.achievements())
            if (!online.count(d.id))
                for (const auto& r : d.after)
                    if (online.count(r)) grew = online.insert(d.id).second || grew;
    }
    int offline = total;
    for (const auto& d : a.achievements()) offline -= online.count(d.id) ? d.stars : 0;
    // The looks the game draws (CosmeticFx, the suits' sheets).
    const std::map<std::string, std::set<std::string>> drawn = {
        {"trail", {"none", "stardust", "comet", "aurora", "eclipse", "flame", "prism", "runes", "month_comet", "ashes", "debris"}},
        {"frame", {"simple", "bronze", "silver", "gold", "platinum", "constellation", "emerald", "orbit", "nebula", "laurels", "crown", "scales", "runic"}},
        {"victory", {"classic", "fireworks", "starfall", "supernova", "vortex", "borealis", "corona", "fire_streak", "dragon_breath", "quake"}}};
    std::map<std::string, int> perCategory, defaults;
    std::set<int> thresholds;
    for (const CosmeticItem& it : c.items()) {
        ++perCategory[it.category];
        defaults[it.category] += it.isDefault ? 1 : 0;
        CHECK(!it.name.empty());
        CHECK(!it.description.empty());
        if (!it.achievement.empty()) {
            if (!a.find(it.achievement)) std::printf("  succès inconnu : %s (%s)\n", it.achievement.c_str(), it.id.c_str());
            CHECK(a.find(it.achievement) != nullptr);
        } else if (it.challengeReward()) {
            // 2.34: won with challenge results verified by Cosmic Online.
            CHECK(it.challenge.count >= 1);
            CHECK(it.challenge.tier >= 1);
            CHECK(it.stars == 0);
        } else if (!it.isDefault && !it.allStars) {
            CHECK(it.stars > 0);
            CHECK(it.stars <= offline);
            thresholds.insert(it.stars);
        }
        if (it.category == "suit" && !it.isDefault) {
            std::error_code ec;
            CHECK(fs::exists(Paths::assetsDir() / "sprites" / "player" / "suits" / ("nova_" + it.look + ".png"), ec));
        }
        if (auto d = drawn.find(it.category); d != drawn.end()) {
            if (!d->second.count(it.look)) std::printf("  apparence non dessinée : %s (%s)\n", it.look.c_str(), it.id.c_str());
            CHECK(d->second.count(it.look) == 1);
        }
        if (it.category == "title") CHECK(it.isDefault ? it.look.empty() : !it.look.empty());
    }
    for (const auto& cat : c.categories()) CHECK(defaults[cat.id] == 1);
    CHECK(perCategory["suit"] >= 12);
    CHECK(perCategory["trail"] >= 8);
    CHECK(perCategory["frame"] >= 10);
    CHECK(perCategory["title"] >= 16);
    CHECK(perCategory["victory"] >= 7);
    // The route of the stars: one milestone per threshold, and the last one is every star.
    auto ms = c.milestones(total);
    CHECK(ms.size() == thresholds.size() + 1);
    CHECK(!ms.empty() && ms.back().stars == total);
    std::printf("  %zu récompenses, %zu paliers (jusqu'à %d étoiles, %d sans compte en ligne)\n", c.items().size(), ms.size(), total, offline);
}
