// Where to fight each boss (boss statistics → « Où l'affronter »): read from the game's own
// levels (game/content/CreaturePlaces.h), never written by hand. Every guardian has its
// campaign level and its echo in L'Arène des Échos; the places say the checkpoint before the
// fight, and why a level cannot be played yet.

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "game/content/ContentDatabase.h"
#include "game/content/CreaturePlaces.h"
#include "game/progression/Progression.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const CreatureAtlas& atlas() {
    static ContentDatabase content;
    static CreatureAtlas a = [] {
        content.reload();
        CreatureScanOptions opt;
        opt.routes = true;
        opt.officialOnly = true;
        return scanCreatures(content, opt);
    }();
    return a;
}

const ContentDatabase& content() {
    static ContentDatabase c = [] {
        ContentDatabase d;
        d.reload();
        return d;
    }();
    return c;
}

bool hasPlace(const std::vector<CreaturePlace>& v, const std::string& level, const std::string& enemy) {
    return std::any_of(v.begin(), v.end(), [&](const CreaturePlace& p) { return p.levelId == level && p.enemyId == enemy; });
}

}  // namespace

TEST_CASE("boss places: every boss of the game can be fought in at least one of its levels") {
    int bosses = 0;
    for (const EnemyDefinition* d : content().enemies()) {
        // 2.64: the tutorial's boss is fought in the tutorial only (outside every level list).
        if (!d->isBoss || !d->builtin || d->tutorial) continue;
        ++bosses;
        auto places = atlas().fightsOf(content(), d->id);
        if (places.empty()) std::printf("  sans lieu : %s\n", d->id.c_str());
        CHECK(!places.empty());
        for (const CreaturePlace& p : places) {
            CHECK(p.official);
            CHECK(content().level(p.levelId) != nullptr);
            CHECK(!p.levelName.empty());
        }
    }
    CHECK(bosses >= 33);  // 16 guardians, their 16 echoes, the Astrarque
}

TEST_CASE("boss places: each guardian in its campaign level, then its echo in L'Arène des Échos") {
    int guardians = 0;
    for (const EnemyDefinition* d : content().enemies()) {
        // The Astrarque and the giga bosses (eight phases: Singularis 2.33, Pyrrhax 2.48) have no
        // echo in L'Arène des Échos.
        if (!d->isBoss || !d->builtin || !d->variantOf.empty() || d->id == "astrarque" || d->phases.size() >= 8) continue;
        ++guardians;
        auto places = atlas().fightsOf(content(), d->id);
        CHECK(places.size() >= 2);
        if (places.size() < 2) continue;
        // Its own fight first: a campaign level of a planet, with its rank in that campaign.
        CHECK(places.front().enemyId == d->id);
        CHECK(!places.front().planetId.empty());
        CHECK(places.front().levelNumber >= 1);
        CHECK(hasPlace(places, "noyau_01", "echo_" + d->id));
    }
    CHECK(guardians == 16);
    // The four guardians of Aethra: their Aethra level, and their echo in the arena.
    const char* aethra[4][2] = {{"mycelia", "aethra_01"}, {"aurorax", "aethra_02"}, {"pyrogyre", "aethra_03"}, {"nyxara", "aethra_04"}};
    for (auto& [boss, level] : aethra) {
        auto places = atlas().fightsOf(content(), boss);
        CHECK(hasPlace(places, level, boss));
        CHECK(hasPlace(places, "noyau_01", std::string("echo_") + boss));
        CHECK(!places.empty() && places.front().planetId == "aethra");
    }
    // Three echoes come back in Le Cœur de l'Étoile too: every place is listed.
    auto forge = atlas().fightsOf(content(), "forge_core");
    CHECK(hasPlace(forge, "pyralis_03", "forge_core"));
    CHECK(hasPlace(forge, "noyau_01", "echo_forge_core"));
    CHECK(hasPlace(forge, "noyau_02", "echo_forge_core"));
}

TEST_CASE("boss places: the checkpoint before the fight, and the order of the arena's bosses") {
    // Thornwarden waits after « Sanctuaire » (the route's last checkpoint before it).
    auto thorn = atlas().fightsOf(content(), "thornwarden");
    CHECK(!thorn.empty());
    if (!thorn.empty()) {
        CHECK(thorn.front().levelId == "sylvanox_01");
        CHECK(thorn.front().checkpointLabel == "Sanctuaire");
        CHECK(thorn.front().bossCount == 1);
        CHECK(thorn.front().at > 0.0f && thorn.front().at <= 1.0f);
    }
    // L'Arène des Échos: no checkpoint, sixteen bosses, each with its own rank.
    std::set<int> ranks;
    for (const auto& [id, places] : atlas().places)
        for (const CreaturePlace& p : places)
            if (p.levelId == "noyau_01" && p.boss && !p.summoned) {
                CHECK(p.bossCount == 16);
                CHECK(p.checkpointId.empty());
                ranks.insert(p.bossRank);
            }
    CHECK(ranks.size() == 16);
    CHECK(*ranks.begin() == 1 && *ranks.rbegin() == 16);
    // Singularis, in its own level only (no echo in the arena).
    auto giga = atlas().fightsOf(content(), "singularis");
    CHECK(giga.size() == 1);
    if (!giga.empty()) {
        CHECK(giga.front().levelId == "noyau_03");
        CHECK(giga.front().planetId == "noyau");
        CHECK(giga.front().checkpointId.empty());
    }
    // The Astrarque, at the heart of the star.
    auto astra = atlas().fightsOf(content(), "astrarque");
    CHECK(astra.size() == 1);
    if (!astra.empty()) {
        CHECK(astra.front().levelId == "noyau_02");
        CHECK(astra.front().checkpointLabel == "Le Cœur");
    }
}

TEST_CASE("boss places: the gallery's planets come from the same reading") {
    auto planets = scanCreatures(content()).planetsByCreature();
    CHECK(planets["thornwarden"] == std::vector<std::string>{"sylvanox"});
    CHECK(planets["echo_thornwarden"] == std::vector<std::string>{"noyau"});
    CHECK(!planets["glowmite"].empty());  // a creature of the bestiary
}

TEST_CASE("boss places: why a level cannot be played yet") {
    Progression p;
    p.unlockAll = false;
    const LevelEntry* first = content().level("sylvanox_01");
    const LevelEntry* second = content().level("sylvanox_02");
    const LevelEntry* arena = content().level("noyau_01");
    const LevelEntry* glacior = content().level("glacior_01");
    CHECK(first && second && arena && glacior);
    if (!first || !second || !arena || !glacior) return;
    CHECK(levelLockReason(*first, content(), p).empty());
    CHECK(levelPlanetOpen(*first, content(), p));
    CHECK(levelLockReason(*second, content(), p) == "Verrouillé : terminez le niveau précédent");
    CHECK(levelPlanetOpen(*second, content(), p));  // its page can be opened (shown locked)
    CHECK(!levelPlanetOpen(*arena, content(), p));
    CHECK(levelLockReason(*arena, content(), p) == "Verrouillé : terminez les planètes Sylvanox, Glacior, Pyralis" ||
          levelLockReason(*arena, content(), p).rfind("Verrouillé : terminez les planètes ", 0) == 0);
    // Glacior opens after Pyralis.
    CHECK(levelLockReason(*glacior, content(), p).rfind("Verrouillé : terminez la planète ", 0) == 0);
    for (const auto& id : content().planet("sylvanox")->levels) p.markCompleted(id);
    for (const auto& id : content().planet("pyralis")->levels) p.markCompleted(id);
    CHECK(levelLockReason(*glacior, content(), p).empty());
    CHECK(levelLockReason(*arena, content(), p) == "Verrouillé : terminez la planète Glacior");
}
