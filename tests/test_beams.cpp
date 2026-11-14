// Textured beams (2.8): every boss beam uses a texture of animations/beams.json, and the look
// of a beam never changes a level's fingerprint (records stay valid).

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/BeamRenderer.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("beams: every beam texture exists, every guardian's beam has one") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    auto set = res.animation("animations/beams.json");
    CHECK(set != nullptr);
    if (!set) return;
    for (const auto& s : BeamRenderer::styles())
        if (!s.empty()) CHECK(set->clip(s) != nullptr);
    CHECK(BeamRenderer::styles().size() == BeamRenderer::styleLabels().size());
    for (const EnemyDefinition* e : content.enemies())
        for (const AttackDef& a : e->attacks) {
            if (a.type != AttackType::Laser) continue;
            if (!a.beamTexture.empty()) CHECK(set->clip(a.beamTexture) != nullptr);
            if (e->isBoss) CHECK(!a.beamTexture.empty());
        }
}

TEST_CASE("beams: the look of a beam is not part of the fingerprints") {
    ContentDatabase content;
    content.reload();
    const EnemyDefinition* base = content.enemy("forge_core");
    CHECK(base != nullptr);
    if (!base) return;
    EnemyDefinition a = *base, b = *base;
    for (auto& atk : b.attacks)
        if (atk.type == AttackType::Laser) {
            atk.beamTexture = atk.beamTexture == "frost" ? "flame" : "frost";
            atk.beamWidth = 31.0f;
        }
    CHECK(a.toJson() != b.toJson());  // saved with the enemy...
    CHECK(gameplayEnemyJson(a) == gameplayEnemyJson(b));  // ...but not in the fingerprints
}
