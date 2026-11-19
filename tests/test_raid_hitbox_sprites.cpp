// 2.75.2: the « Où frapper » diagram of the raid sheets follows the sprites and the fight
// (docs/RAIDS.md §18): Heartbreaker's plates guard its FIRST phase, its ribs its second; each attack
// of a phase on the pose of its own animation with what it sends from its body (game/raid/
// BossHitboxes::hitAttack); the shots and beams of the gigaboss leave their DRAWING (the origins of
// their attacks are pixels of their sprite, × their scale: they were written in world units, up to
// 300 px above it); the crown of flames keeps its radius (every other enemy's as before).

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/core/FileUtil.h"
#include "game/achievements/Achievements.h"
#include "game/content/ContentDatabase.h"
#include "game/cosmetics/Collection.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/raid/BossHitboxes.h"
#include "game/raid/RaidSheets.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

raid::RaidSheets sheetsOf(ContentDatabase& content) {
    AchievementCatalog achievements;
    achievements.loadDefault();
    CosmeticCatalog cosmetics;
    cosmetics.loadDefault();
    return raid::loadRaidSheets(content, achievements, cosmetics);
}

/// A sprite sheet read as the game draws it (its JSON, its picture in RGBA), without a renderer.
struct Sheet {
    std::shared_ptr<AnimationSet> set;
    SDL_Surface* pixels = nullptr;
    explicit Sheet(const std::string& rel) {
        if (auto j = FileUtil::loadJson("assets/" + rel)) set = AnimationSet::fromJson(*j, rel);
        if (!set) return;
        if (SDL_Surface* s = IMG_Load(("assets/" + set->texturePath).c_str())) {
            pixels = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
        }
    }
    ~Sheet() {
        if (pixels) SDL_DestroySurface(pixels);
    }
    Sheet(const Sheet&) = delete;
    Sheet& operator=(const Sheet&) = delete;
    /// Whether a pixel of frame `f` of `clip` within `r` pixels of (x, y) of the frame is drawn.
    bool drawnNear(const AnimationClip& clip, int f, float x, float y, int r) const {
        const Rect src = set->frameRect(clip, f);
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const int px = static_cast<int>(std::lround(x)) + dx, py = static_cast<int>(std::lround(y)) + dy;
                if (px < 0 || py < 0 || px >= set->frameWidth || py >= set->frameHeight) continue;
                const int ax = static_cast<int>(src.x) + px, ay = static_cast<int>(src.y) + py;
                if (ax >= pixels->w || ay >= pixels->h) continue;
                const auto* row = static_cast<const uint8_t*>(pixels->pixels) + static_cast<size_t>(ay) * static_cast<size_t>(pixels->pitch);
                if (row[static_cast<size_t>(ax) * 4 + 3] > 100) return true;
            }
        return false;
    }
};

}  // namespace

TEST_CASE("raid sheets (2.75.2): Heartbreaker's plates guard its FIRST phase, its ribs its second; the weak points carry their sprite") {
    ContentDatabase content;
    content.reload();
    const raid::RaidSheets sheets = sheetsOf(content);
    const raid::GigabossSheet* h = sheets.find("heartbreaker");
    CHECK(h != nullptr && h->def != nullptr);
    if (!h || !h->def) return;
    const raid::BossHitboxes& hb = h->hitboxes;
    std::vector<std::string> order;
    for (const raid::HitboxView& v : hb.views) order.push_back(std::to_string(v.phase) + ":" + v.state);
    const std::vector<std::string> want = {"1:armour", "1:fight", "2:armour", "2:exposed", "3:fight"};
    CHECK(order == want);
    auto targets = [](const raid::HitboxView& v) {
        int n = 0;
        for (const raid::HitPart& p : v.parts) n += p.label.rfind("À briser", 0) == 0 ? 1 : 0;
        return n;
    };
    if (order == want) {
        const raid::HitboxView& plates = hb.views[0];
        CHECK(targets(plates) == 4);
        for (const raid::HitPart& p : plates.parts)
            if (p.label.rfind("À briser", 0) == 0) CHECK(p.enemy == "heartbreaker_plaque");  // its sprite drawn there
        CHECK(plates.plates.empty());        // no rib to hold in its first phase
        CHECK(targets(hb.views[2]) == 0);    // its ribs' phase: no plate on it
        CHECK(!hb.views[2].plates.empty());  // the ribs, in its arena
        CHECK(hb.views[1].hitWhen.find("phase suivante") != std::string::npos);
    }
    // The weak points of every bare state carry their enemy (its sprite drawn on the diagram).
    for (const raid::GigabossSheet& g : sheets.gigaboss)
        for (const raid::HitboxView& v : g.hitboxes.views)
            for (const raid::HitPart& p : v.parts)
                if (p.label.rfind("Point faible", 0) == 0) CHECK(!p.enemy.empty() && content.enemy(p.enemy) != nullptr);
}

TEST_CASE("gigaboss (2.75.2): their shots and beams leave their drawing; the diagram has every attack of a phase on its own pose") {
    ContentDatabase content;
    content.reload();
    const raid::RaidSheets sheets = sheetsOf(content);
    CHECK(sheets.gigaboss.size() >= 7);
    int origins = 0;
    for (const raid::GigabossSheet& g : sheets.gigaboss) {
        if (!g.def) continue;
        const raid::BossHitboxes& hb = g.hitboxes;
        for (const AttackDef& a : g.def->attacks) {
            if (!a.hasOrigin) continue;
            // The sheet of the first phase that uses it (else its own), the frames of its wind-up and blow.
            std::string sheetPath = g.def->animation;
            for (const BossPhase& ph : g.def->phases)
                if (std::find(ph.attacks.begin(), ph.attacks.end(), a.id) != ph.attacks.end()) {
                    if (!ph.animation.empty()) sheetPath = ph.animation;
                    break;
                }
            const Sheet s(sheetPath);
            CHECK(s.set != nullptr && s.pixels != nullptr);
            if (!s.set || !s.pixels) continue;
            // Where Enemy::attackOrigin puts it, in the pixels of its frame (its feet at the pivot).
            const Vec2 o = attackOriginOf(a, hb.body, 1.0f, g.def->scale);
            const float fx = s.set->pivot.x + o.x / g.def->scale, fy = s.set->pivot.y + o.y / g.def->scale;
            bool drawn = false;
            for (const std::string& clipName : {a.attackAnim, a.telegraphAnim, std::string("idle")})
                if (const AnimationClip* c = s.set->clip(clipName))
                    for (int f = 0; f < c->frameCount() && !drawn; ++f) drawn = s.drawnNear(*c, f, fx, fy, 8);
            if (!drawn) std::printf("  %s · %s : ses tirs partent de (%.0f, %.0f), hors de son dessin\n", g.name.c_str(), a.id.c_str(), o.x, o.y);
            CHECK(drawn);
            ++origins;
        }
        // The diagram: every attack of its phases, its pose's clips, the origin where the fight puts it.
        for (size_t p = 0; p < g.def->phases.size(); ++p)
            for (const std::string& id : g.def->phases[p].attacks) {
                const raid::HitAttack* seen = nullptr;
                for (const raid::HitAttack& h : hb.attacks)
                    if (h.id == id) seen = &h;
                CHECK(seen != nullptr);
                if (!seen) continue;
                CHECK(std::find(seen->phases.begin(), seen->phases.end(), static_cast<int>(p) + 1) != seen->phases.end());
                CHECK(!seen->reach.empty());
                const AttackDef* a = g.def->attack(id);
                CHECK(a != nullptr);
                if (!a) continue;
                CHECK(seen->attackClip == a->attackAnim);
                CHECK(seen->telegraphClip == a->telegraphAnim);
                for (const raid::HitShape& s : seen->shapes)
                    if (s.kind == raid::HitShape::Kind::Origin) CHECK(s.at == attackOriginOf(*a, hb.body, 1.0f, g.def->scale));
            }
    }
    CHECK(origins >= 12);  // Skullmaw 4, Dreadfang 2, Pyroclast 1, Heartbreaker 3, Voidworm 2
    // Skullmaw's gaze leaves between its eyes (the eyes of its rules, SkullmawRoutine).
    if (const raid::GigabossSheet* sk = sheets.find("skullmaw"); sk && sk->def)
        if (const AttackDef* gaze = sk->def->attack("regard")) {
            const Vec2 o = attackOriginOf(*gaze, sk->hitboxes.body, 1.0f, sk->def->scale);
            CHECK(std::abs(o.x) < 1.0f);
            CHECK(std::abs(o.y - -205.0f) < 1.0f);
        }
}

TEST_CASE("orbit (2.75.2): the crown keeps its radius; every other enemy's crown as before") {
    ContentDatabase content;
    content.reload();
    int crowns = 0, fixed = 0;
    for (const EnemyDefinition* dp : content.enemies()) {
        const EnemyDefinition& d = *dp;
        for (const AttackDef& a : d.attacks) {
            if (a.type != AttackType::Orbit) continue;
            ++crowns;
            const std::pair<float, float> r = orbitRadii(a, d.size, d.scale);
            const float reach = a.radius > 0.0f ? a.radius : 150.0f;
            const float before = std::max(d.size.x, d.size.y) * 0.5f * d.scale + 14.0f;  // the formula until 2.75.1
            CHECK(r.second == reach);  // its radius, always
            CHECK(r.first + 20.0f <= r.second + 0.001f);
            if (before + 20.0f <= reach) {
                CHECK(r.first == before);  // the same flames as before
            } else {
                ++fixed;  // its box × its scale went past its radius: just outside its box, never past it
                CHECK(r.first <= reach - 20.0f + 0.001f);
            }
        }
    }
    CHECK(crowns > 5);
    CHECK(fixed == 3);  // Heartbreaker, Graviton, the Cœur: their radius was ignored
    // attackOriginOf: the formula of Enemy::attackOrigin (its origin × its scale, x toward where it faces).
    AttackDef a;
    a.hasOrigin = true;
    a.origin = {10.0f, -40.0f};
    const Rect box{100.0f, 200.0f, 60.0f, 80.0f};
    const Vec2 left{130.0f - 20.0f, 280.0f - 80.0f};
    CHECK(attackOriginOf(a, box, -1.0f, 2.0f) == left);
    a.hasOrigin = false;
    a.type = AttackType::Laser;
    const Vec2 laser = box.center() + Vec2{60.0f * 0.3f, -80.0f * 0.2f};
    CHECK(attackOriginOf(a, box, 1.0f, 2.0f) == laser);
    a.type = AttackType::Spread;
    const Vec2 other = box.center() + Vec2{60.0f * 0.4f, -80.0f * 0.1f};
    CHECK(attackOriginOf(a, box, 1.0f, 2.0f) == other);
}
