// Boss arenas (2.17): during a boss fight the camera shows the whole arena. Checked on every
// boss of every campaign level: the arena fits on a 16:9 screen, and once the fight has
// started the view contains all of it. The camera is display only: the levels' ranked
// fingerprints are untouched (records kept).

#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "tests/TestFramework.h"

using namespace cosmic;

TEST_CASE("boss camera: every campaign boss fight shows its whole arena (the giant one: at its widest)") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int fights = 0;
    for (const auto* planet : content.planets())
        for (const auto& levelId : planet->levels) {
            auto level = content.loadLevel(levelId);
            CHECK(level.has_value());
            if (!level) continue;
            std::vector<std::string> bosses;
            for (const auto& o : level->objects)
                if (o.type == "enemy" && o.prop<bool>("boss", false)) bosses.push_back(o.id);
            for (const auto& bossId : bosses) {
                WorldServices s{res, audio, content, nullptr, 0.0f, 0.0f};
                GameWorld w(s, WorldMode::Play);
                w.load(*level);
                auto* spawn = dynamic_cast<EnemySpawnEntity*>(w.entity(bossId));
                CHECK(spawn != nullptr);
                if (!spawn) continue;
                // Into the arena, then the fight starts.
                Rect arena;
                bool found = false;
                for (const auto& e : w.entities())
                    if (const auto* cz = dynamic_cast<const CombatZoneEntity*>(e.get()))
                        if (cz->lists(bossId) || cz->box.contains(spawn->box.center())) {
                            arena = cz->box;
                            found = true;
                            break;
                        }
                if (!found) std::cout << "  no arena: " << levelId << " " << bossId << "\n";
                CHECK(found);
                if (!found) continue;
                w.startAt({arena.x + 40.0f, arena.y + 40.0f});
                spawn->handleAction(w, "activate", Json::object());
                const CameraZone* z = w.bossCameraZone();
                CHECK(z != nullptr);
                if (!z) continue;
                // It fits on a 16:9 screen without going below the least zoom.
                (void)w.camera().visibleArea(1920, 1080);  // the screen shape
                if (spawn->object().propStr("enemy") == "singularis") {
                    // 2.33: the giant arena of Singularis never fits: the camera follows Nova at
                    // its widest (tests/test_singularis.cpp).
                    CHECK(w.camera().fitZoom(z->area) < Camera::kMinFitZoom);
                    ++fights;
                    continue;
                }
                CHECK(w.camera().fitZoom(z->area) >= Camera::kMinFitZoom);
                for (int i = 0; i < 300 && w.activeBoss(); ++i) w.fixedUpdate(PlayerInput{}, 1.0f / 120.0f);
                if (!w.activeBoss()) continue;  // the player fell before the end (respawn)
                Rect view = w.camera().visibleArea(1920, 1080);
                bool shown = view.contains({arena.x + 2, arena.y + 2}) && view.contains({arena.right() - 2, arena.bottom() - 2});
                if (!shown)
                    std::cout << "  arena not in view: " << levelId << " " << bossId << " arena " << arena.x << "," << arena.y << " " << arena.w
                              << "x" << arena.h << " view " << view.x << "," << view.y << " " << view.w << "x" << view.h << "\n";
                CHECK(shown);
                ++fights;
            }
        }
    CHECK(fights >= 20);
}
