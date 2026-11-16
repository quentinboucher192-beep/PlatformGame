// Difficulty of the bosses in stars (2.37): computed from their definitions, from 1 to 10, the
// same for a boss and its echo, Singularis (eight phases) the hardest at 10, and a boss with more
// health or shorter wind-ups never easier.

#include <map>

#include "game/content/ContentDatabase.h"
#include "game/enemies/BossDifficulty.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("boss difficulty: every boss of the game from 1 to 10 stars, Singularis at 10") {
    ContentDatabase content;
    content.reload();
    std::map<std::string, int> stars;
    int bosses = 0;
    for (const auto* d : content.enemies()) {
        if (!d->isBoss) continue;
        ++bosses;
        BossDifficulty b = bossDifficulty(*d);
        CHECK(b.stars >= 1);
        CHECK(b.stars <= 10);
        CHECK(b.score > 0.0);
        stars[d->id] = b.stars;
    }
    CHECK(bosses >= 18);
    CHECK(stars.count("singularis"));
    if (!(stars.count("singularis"))) return;
    CHECK(stars["singularis"] == 10);
    // The giga bosses (eight phases: Singularis, 2.48 Pyrrhax) are at 10, every other guardian below.
    for (const auto& [id, s] : stars) {
        const EnemyDefinition* d = content.enemy(id);
        if (d && d->phases.size() >= 8) CHECK(s == 10);
        else CHECK(s < 10);
    }
    // An echo is its original (same fight, another colour).
    for (const auto* d : content.enemies())
        if (d->isBoss && !d->variantOf.empty() && stars.count(d->variantOf)) CHECK(stars[d->id] == stars[d->variantOf]);
    // The scale is used: the easiest guardians are not rated like the hardest ones.
    int lo = 10, hi = 1;
    for (const auto& [id, s] : stars) {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    CHECK(lo <= 2);
    CHECK(hi == 10);
}

TEST_CASE("boss difficulty: more health or shorter wind-ups never make a boss easier") {
    ContentDatabase content;
    content.reload();
    const EnemyDefinition* base = content.enemy("astrarque");
    CHECK(base != nullptr);
    if (!(base != nullptr)) return;
    BossDifficulty ref = bossDifficulty(*base);
    EnemyDefinition tougher = *base;
    tougher.hp *= 2.0f;
    CHECK(bossDifficulty(tougher).score > ref.score);
    CHECK(bossDifficulty(tougher).stars >= ref.stars);
    EnemyDefinition faster = *base;
    for (auto& p : faster.phases) p.telegraphMultiplier *= 0.5f;
    CHECK(bossDifficulty(faster).score > ref.score);
    // A boss without attacks nor health still gets a rating (the lowest).
    EnemyDefinition empty;
    empty.isBoss = true;
    empty.hp = 0.0f;
    CHECK(bossDifficulty(empty).stars == 1);
}
