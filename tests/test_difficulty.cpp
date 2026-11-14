// Difficulty rating (game/levels/DifficultyRating.h): the campaign's stored ratings match the
// formula, and the formula reacts in the right direction to what makes a level harder.

#include <cmath>

#include "engine/core/FileUtil.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/DifficultyRating.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("difficulty: data/levels/ratings.json is up to date with the formula") {
    ContentDatabase content;
    content.reload();
    auto j = FileUtil::loadJson(Paths::dataDir() / "levels" / "ratings.json");
    CHECK(j.has_value());
    if (!j) return;
    CHECK(jget<int>(*j, "version", 0) == DifficultyRating::kRatingVersion);
    int n = 0;
    for (const LevelEntry* e : content.levels()) {
        if (e->source != ContentSource::Builtin) continue;
        auto level = content.loadLevel(e->id);
        CHECK(level.has_value());
        if (!level) continue;
        DifficultyRating r = rateDifficulty(*level, content);
        const Json& stored = (*j)["levels"][e->id];
        CHECK(stored.is_object());
        float score = jget<float>(stored, "score", -1.0f);
        if (std::abs(score - r.score) > 0.01f || std::abs(jget<float>(stored, "raw", -1.0f) - r.raw) > 0.02f)
            std::printf("  %s : stocké %.1f / calculé %.1f (brut %.2f) -> CosmicPlatformer --rate-levels data/levels/ratings.json\n",
                        e->id.c_str(), score, r.score, r.raw);
        CHECK(std::abs(score - r.score) < 0.01f);
        CHECK(r.score >= 0.5f && r.score <= 5.0f);
        CHECK(r.reachable);
        ++n;
    }
    CHECK(n >= 12);
}

TEST_CASE("difficulty: deadly hazards, enemies and fewer checkpoints raise the rating; healing lowers it") {
    ContentDatabase content;
    content.reload();
    auto base = content.loadLevel("sylvanox_02");
    CHECK(base.has_value());
    if (!base) return;
    const LevelAnalysis a = analyzeLevel(*base, content);
    DifficultyRating r0 = rateDifficulty(*base, content, a);

    // Spikes along the intended route (beside it, so the route itself is unchanged).
    LevelData spiky = *base;
    for (size_t i = 0; i < a.intendedPath.size(); i += 40) {
        LevelObject o;
        o.id = "test_spikes_" + std::to_string(i);
        o.type = "spikes";
        o.rect = {a.intendedPath[i].x + 20.0f, a.intendedPath[i].y - 120.0f, 48.0f, 16.0f};
        o.props = {{"damage", 2}};
        spiky.objects.push_back(o);
    }
    DifficultyRating r1 = rateDifficulty(spiky, content, a);
    CHECK(r1.hazards > r0.hazards);
    CHECK(r1.raw > r0.raw);

    // Plenty of healing.
    LevelData healthy = *base;
    for (int i = 0; i < 30; ++i) {
        LevelObject o;
        o.id = "test_heal_" + std::to_string(i);
        o.type = "health_orb";
        o.rect = {200.0f + 100.0f * static_cast<float>(i), 100.0f, 12.0f, 12.0f};
        o.props = {{"amount", 2}};
        healthy.objects.push_back(o);
    }
    DifficultyRating r2 = rateDifficulty(healthy, content, a);
    CHECK(r2.healing < r0.healing);
    CHECK(r2.raw < r0.raw);

    // No checkpoint at all: every mistake sends back to the start.
    LevelData bare = *base;
    std::erase_if(bare.objects, [](const LevelObject& o) { return o.type == "checkpoint"; });
    DifficultyRating r3 = rateDifficulty(bare, content);
    CHECK(r3.checkpoints > r0.checkpoints);

    // The serialised form gives the same rating back.
    DifficultyRating back = DifficultyRating::fromJson(r0.toJson());
    CHECK(std::abs(back.score - r0.score) < 1e-4f);
    CHECK(back.preciseJumps == r0.preciseJumps);
    CHECK(!r0.summary().empty());
    CHECK(r0.label().find(",") != std::string::npos);
}

#include "game/online/LevelFingerprint.h"

TEST_CASE("community: the gameplay fingerprint ignores names and renames, not the gameplay") {
    ContentDatabase content;
    content.reload();
    auto base = content.loadLevel("sylvanox_02");
    CHECK(base.has_value());
    if (!base) return;
    const std::string h = fingerprintGameplay(content, *base);
    CHECK(h.size() == 64);
    // Another id, name, author, description, music and metadata: same gameplay.
    LevelData renamed = *base;
    renamed.id = "copie_importee";
    renamed.name = "Autre nom";
    renamed.author = "Quelqu'un";
    renamed.description = "…";
    renamed.music = "music/menu_theme.ogg";
    renamed.metadata["community"] = {{"id", "x"}};
    CHECK(fingerprintGameplay(content, renamed) == h);
    // The importer may rename an enemy definition: same definition, same fingerprint.
    ContentDatabase other;
    other.reload();
    std::string enemyId;
    for (const auto& o : base->objects)
        if (o.type == "enemy") {
            enemyId = o.propStr("enemy");
            break;
        }
    CHECK(!enemyId.empty());
    EnemyDefinition copy = *content.enemy(enemyId);
    copy.id = enemyId + "_perso";
    copy.builtin = false;
    CHECK(other.saveUserEnemy(copy));
    LevelData remapped = *base;
    for (auto& o : remapped.objects)
        if (o.type == "enemy" && o.propStr("enemy") == enemyId) o.props["enemy"] = copy.id;
    CHECK(fingerprintGameplay(other, remapped) == h);
    other.deleteUserEnemy(copy.id);
    // A moved platform, a different timer or a stronger enemy: another fingerprint.
    LevelData moved = *base;
    for (auto& o : moved.objects)
        if (o.type == "solid") {
            o.rect.x += 16.0f;
            break;
        }
    CHECK(fingerprintGameplay(content, moved) != h);
    LevelData timed = *base;
    timed.timeLimit += 10.0f;
    CHECK(fingerprintGameplay(content, timed) != h);
}
