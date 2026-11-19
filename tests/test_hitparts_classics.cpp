// 2.76.2: the classic bosses of the campaign (the guardians of the planets and of Aethra, the tutorial's
// Mycélia) and their échos fight by their hit parts (kSimulationVersion 17, « partsFromVersion » 17):
// their parts follow the drawing of every frame (profile « rig »: what their sprite generator draws),
// each with the role the player validated (head, eyes, exposed heart × 2; shell, armour, tail × 0,5;
// the rest × 1; spikes, flames, weapons hurt); an écho takes its original's parts. A sample of them in
// an empty arena: the weak point takes × 2, the armour × 0,5, the parts named for an attack hurt during
// it, and their shots leave their drawing; with the rules of 16 (a replay of before) they keep their box.

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/audio/AudioSystem.h"
#include "engine/core/FileUtil.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/enemies/HitParts.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
#include "game/online/LevelFingerprint.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

ContentDatabase& content() {
    static ContentDatabase* c = nullptr;
    if (!c) {
        c = new ContentDatabase();
        c->reload();
    }
    return *c;
}

/// The 17 classic bosses (the gigaboss of the campaign and of Asterion are left out).
const std::vector<std::string> kClassics = {"archon_catacombs", "aurorax",        "crucible_heart", "cryowyrm",       "forge_core",
                                            "frost_hydra",      "glaciarch",      "howling_wyrm",   "ignivore",       "magmaroth",
                                            "mycelia",          "mycelia_tendre", "nyxara",         "prime_sentinel", "pyrogyre",
                                            "spore_queen",      "thornwarden"};

/// The classic bosses and their échos (« variantOf » one of them, « echo_<id> »).
std::vector<const EnemyDefinition*> mine() {
    std::vector<const EnemyDefinition*> out;
    for (const EnemyDefinition* d : content().enemies()) {
        if (!d->isBoss) continue;
        const bool classic = std::find(kClassics.begin(), kClassics.end(), d->id) != kClassics.end();
        const bool echo = d->id.rfind("echo_", 0) == 0 && std::find(kClassics.begin(), kClassics.end(), d->variantOf) != kClassics.end();
        if (classic || echo) out.push_back(d);
    }
    return out;
}

Vec2 centroid(const std::vector<Vec2>& p) {
    Vec2 c{0.0f, 0.0f};
    for (const Vec2& v : p) c = c + v;
    return c * (1.0f / static_cast<float>(p.size()));
}

float area(const std::vector<Vec2>& p) {
    float s = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % p.size()];
        s += a.x * b.y - b.x * a.y;
    }
    return std::abs(s) * 0.5f;
}

/// Whether an attack's shots or beam leave Enemy::attackOrigin (the others strike where the target is).
bool shoots(const AttackDef& a) {
    return a.type == AttackType::Projectile || a.type == AttackType::Spread || a.type == AttackType::Arc ||
           a.type == AttackType::Laser || a.type == AttackType::Prism;
}

/// A sprite sheet's picture, as the game draws it (tests/test_raid_hitbox_sprites.cpp).
struct Pixels {
    std::shared_ptr<AnimationSet> set;
    SDL_Surface* pixels = nullptr;
    explicit Pixels(const std::string& rel) {
        if (auto j = FileUtil::loadJson("assets/" + rel)) set = AnimationSet::fromJson(*j, rel);
        if (!set) return;
        if (SDL_Surface* s = IMG_Load(("assets/" + set->texturePath).c_str())) {
            pixels = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
        }
    }
    ~Pixels() {
        if (pixels) SDL_DestroySurface(pixels);
    }
    Pixels(const Pixels&) = delete;
    Pixels& operator=(const Pixels&) = delete;
    /// How many pixels of frame `f` of `clip` are drawn.
    int drawn(const AnimationClip& clip, int f) const {
        const Rect src = set->frameRect(clip, f);
        int n = 0;
        for (int y = 0; y < set->frameHeight; ++y)
            for (int x = 0; x < set->frameWidth; ++x) {
                const int ax = static_cast<int>(src.x) + x, ay = static_cast<int>(src.y) + y;
                if (ax >= pixels->w || ay >= pixels->h) continue;
                const auto* row = static_cast<const uint8_t*>(pixels->pixels) + static_cast<size_t>(ay) * static_cast<size_t>(pixels->pitch);
                n += row[static_cast<size_t>(ax) * 4 + 3] > 100;
            }
        return n;
    }
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

/// The frame an attack shoots on: its clip's « fire » event, else its middle.
int fireFrame(const AnimationClip& c) {
    for (const AnimationEvent& e : c.events)
        if (e.name == "fire") return e.frame;
    return c.frameCount() / 2;
}

/// Where an attack's shots leave in the rules of 17: the origin of its parts, else its own (sprite pixels from its feet).
std::optional<Vec2> originOf(const EnemyDefinition& d, const AttackDef& a) {
    if (const Vec2* o = PartRules::of(d.id).origin(a.id)) return *o;
    if (a.hasOrigin) return a.origin;
    return std::nullopt;
}

/// An empty arena with a boss engaged, with the rules of `rules`.
struct Arena {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Enemy* boss = nullptr;
    Arena(const std::string& id, int rules) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_hitparts_classics", "Test", "pyralis");
        l.intro.steps.clear();
        for (auto& o : l.objects)
            if (o.type == "solid") o.rect = {0, 800, 2400, 160};
        WorldServices s{res, audio, content(), nullptr, 1.0f, 1.0f};
        w = std::make_unique<GameWorld>(s, WorldMode::Arena);
        w->setSimulationVersion(rules);
        w->seedGameplay(7);
        w->load(l);
        w->silent = true;
        w->player().spawn(*w, {300, 800}, false, false);
        for (int i = 0; i < 10; ++i) w->fixedUpdate({}, kSimDt);
        const EnemyDefinition* d = content().enemy(id);
        boss = w->spawnEnemy(id, {1200, d && d->flying ? 700.0f : 800.0f}, false, "boss");
        if (boss) {
            boss->facing = 1.0f;
            boss->alert(*w);
        }
        w->fixedUpdate({}, kSimDt);
    }
    /// The pieces of a part of its current pose, placed in the world.
    std::vector<std::vector<Vec2>> part(const std::string& name) const {
        std::vector<std::vector<Vec2>> out;
        const AnimationSetPtr& set = boss->animator().set();
        const HitPartsSheet* hp = set ? HitPartsSheet::of(set->sourcePath) : nullptr;
        if (!hp) return out;
        const Rect src = boss->animator().sourceRect();
        const auto* pieces = hp->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
        if (!pieces) return out;
        for (const HitPartPiece& p : *pieces)
            if (hp->parts[static_cast<size_t>(p.part)] == name)
                out.push_back(placePiece(p, *hp, boss->box.bottomCenter(), boss->def().scale, boss->facing < 0.0f));
        return out;
    }
    /// Small boxes on a part: the middle of each piece, then halfway to each of its corners.
    std::vector<Rect> probes(const std::string& name) const {
        std::vector<Rect> out;
        for (const auto& piece : part(name)) {
            const Vec2 c = centroid(piece);
            out.push_back(Rect::fromCenter(c, {2.0f, 2.0f}));
            for (const Vec2& v : piece) out.push_back(Rect::fromCenter(centroid({v, c}), {2.0f, 2.0f}));
        }
        return out;
    }
    /// A probe the blows give to `name` (no part of a higher priority there).
    std::optional<Rect> spot(const std::string& name) const {
        for (const Rect& r : probes(name))
            if (const auto hit = partHitBy(*boss, r); hit && hit->name == name) return r;
        return std::nullopt;
    }
    /// A probe on `name` where nothing hurts now (or where something does: `hurting`).
    std::optional<Rect> touch(const std::string& name, bool hurting) const {
        for (const Rect& r : probes(name))
            if (hurtsWithParts(*boss, r) == hurting) return r;
        return std::nullopt;
    }
    /// Steps until it performs `attack` (forced), at most 4 s.
    bool perform(const std::string& attack) {
        if (!boss->forceAttack(*w, attack)) return false;
        for (int i = 0; i < 480; ++i) {
            if (boss->state() == EnemyState::Attack && boss->currentAttack() && boss->currentAttack()->id == attack) return true;
            w->fixedUpdate({}, kSimDt);
        }
        return false;
    }
};

}  // namespace

TEST_CASE("hit parts (2.76.2): the 17 classic bosses and their 16 échos fight by their parts from 17, with the roles of the model") {
    const auto bosses = mine();
    int classics = 0, echoes = 0;
    for (const EnemyDefinition* d : bosses) {
        (d->id.rfind("echo_", 0) == 0 ? echoes : classics) += 1;
        const PartRules& r = PartRules::of(d->id);
        if (r.fromVersion != 17) std::printf("  %s : partsFromVersion %d\n", d->id.c_str(), r.fromVersion);
        CHECK(r.fromVersion == 17);
        CHECK(r.profile == "rig");
        const HitPartsSheet* hp = HitPartsSheet::of(d->animation);
        CHECK(hp != nullptr);
        if (!hp) continue;
        CHECK(hp->profile == "rig");
        // A rule for each of its parts, a part for each rule; a weak point at least, the multipliers of the model.
        for (const std::string& n : hp->parts) {
            if (!r.rule(n)) std::printf("  %s : la partie « %s » n'a pas de règle\n", d->id.c_str(), n.c_str());
            CHECK(r.rule(n) != nullptr);
        }
        int weak = 0, hurt = 0;
        for (const PartRule& rule : r.rules) {
            CHECK(std::find(hp->parts.begin(), hp->parts.end(), rule.name) != hp->parts.end());
            if (rule.role == PartRole::Weak) CHECK(rule.damage == 2.0f);
            if (rule.role == PartRole::Armour) CHECK(rule.damage == 0.5f);
            if (rule.role == PartRole::Normal) CHECK(rule.damage == 1.0f);
            if (rule.role == PartRole::Hurt) CHECK(rule.damage == 0.0f);
            weak += rule.role == PartRole::Weak;
            hurt += rule.role == PartRole::Hurt;
            // The attacks it hurts during are its own (or its original's: the Câline has fewer of them).
            for (const std::string& a : rule.hurtDuring) {
                const EnemyDefinition* original = d->variantOf.empty() ? nullptr : content().enemy(d->variantOf);
                CHECK(d->attack(a) != nullptr || (original && original->attack(a) != nullptr));
            }
        }
        CHECK(weak >= 1);
        CHECK(hurt >= 1);
        // An écho takes its original's parts.
        if (!d->variantOf.empty()) {
            const PartRules& o = PartRules::of(d->variantOf);
            CHECK(o.rules.size() == r.rules.size());
            for (size_t i = 0; i < r.rules.size() && i < o.rules.size(); ++i)
                CHECK(o.rules[i].name == r.rules[i].name && o.rules[i].role == r.rules[i].role && o.rules[i].damage == r.rules[i].damage);
            CHECK(o.origins.size() == r.origins.size());
        }
        // Its own origins name its attacks.
        for (const auto& [id, o] : r.origins) CHECK(d->attack(id) != nullptr);
    }
    std::printf("  %d boss classiques, %d échos\n", classics, echoes);
    CHECK(classics == 17);
    CHECK(echoes == 16);
}

TEST_CASE("hit parts (2.76.2): every classic boss and écho has parts on every frame of every clip, and no part is empty") {
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int frames = 0;
    std::set<std::string> sheets;
    for (const EnemyDefinition* d : mine()) {
        const HitPartsSheet* hp = HitPartsSheet::of(d->animation);
        AnimationSetPtr set = res.animation(d->animation);
        CHECK(hp != nullptr && set != nullptr);
        if (!hp || !set) continue;
        sheets.insert(d->animation);
        const Pixels px(d->animation);
        CHECK(px.set != nullptr && px.pixels != nullptr);
        if (!px.set || !px.pixels) continue;
        std::vector<int> seen(hp->parts.size(), 0);
        for (const auto& [name, clip] : set->clips)
            for (int f = 0; f < clip.frameCount(); ++f) {
                ++frames;
                const std::vector<HitPartPiece>* pieces = hp->frame(*set, clip, f);
                const bool some = pieces && !pieces->empty();
                // A frame where nothing of it is drawn but a few embers (the end of its death) has none.
                if (!some && px.drawn(clip, f) < 16) continue;
                if (!some) std::printf("  %s : %s image %d sans parties\n", d->id.c_str(), name.c_str(), f);
                CHECK(some);
                if (!pieces) continue;
                for (const HitPartPiece& p : *pieces) {
                    CHECK(p.part >= 0 && p.part < static_cast<int>(hp->parts.size()));
                    CHECK(p.pts.size() >= 3 && area(p.pts) > 0.0f);
                    if (p.part >= 0 && p.part < static_cast<int>(seen.size())) ++seen[static_cast<size_t>(p.part)];
                }
            }
        for (size_t i = 0; i < seen.size(); ++i) {
            if (!seen[i]) std::printf("  %s : la partie « %s » n'est sur aucune image\n", d->id.c_str(), hp->parts[i].c_str());
            CHECK(seen[i] > 0);
        }
    }
    std::printf("  %zu feuilles, %d images\n", sheets.size(), frames);
    CHECK(sheets.size() == 13);
}

TEST_CASE("hit parts (2.76.2): the shots and beams of the classic bosses and échos leave their drawing") {
    int origins = 0;
    for (const EnemyDefinition* d : mine()) {
        const Pixels s(d->animation);
        CHECK(s.set != nullptr && s.pixels != nullptr);
        if (!s.set || !s.pixels) continue;
        for (const AttackDef& a : d->attacks) {
            if (!shoots(a)) continue;
            const auto o = originOf(*d, a);
            if (!o) std::printf("  %s · %s : pas d'origine sur son dessin\n", d->id.c_str(), a.id.c_str());
            CHECK(o.has_value());
            // A prism traces its path when its wind-up begins (GuardianAttacks: the beams follow it): the first
            // frame of its wind-up; the others, the frame they shoot on.
            const bool prism = a.type == AttackType::Prism;
            const AnimationClip* c = s.set->clip(prism ? a.telegraphAnim : a.attackAnim);
            CHECK(c != nullptr);
            if (!o || !c) continue;
            const bool drawn = s.drawnNear(*c, prism ? 0 : fireFrame(*c), s.set->pivot.x + o->x, s.set->pivot.y + o->y, 3);
            if (!drawn) std::printf("  %s · %s : ses tirs partent de (%.0f, %.0f), hors de son dessin\n", d->id.c_str(), a.id.c_str(), o->x, o->y);
            CHECK(drawn);
            ++origins;
        }
    }
    std::printf("  %d attaques qui tirent\n", origins);
    CHECK(origins > 100);
}

TEST_CASE("hit parts (2.76.2): a sample of the classic bosses and échos by their parts in 17, by their box in 16") {
    struct Sample {
        const char* id;
        const char* weak;
        const char* armour;
        const char* hurt;     // a part that always hurts
        const char* striker;  // a part that also hurts during `attack`
        const char* attack;
    };
    const Sample samples[] = {
        {"thornwarden", "tête", "épaulière", "ronces", "poing", "slam"},
        {"magmaroth", "cœur", "bosse", "marteau", "bras", "slam"},
        {"cryowyrm", "tête", "queue", "pointes", "tête", "dive"},
        {"prime_sentinel", "œil", "plaques", "réacteur", "plaques", "slam"},
        {"mycelia", "visage", "chapeau", "racines", "visage", "inhale"},
        {"pyrogyre", "œil", "anneau", "flammes", "anneau", "crown"},
        {"echo_glaciarch", "cœur", "épaulière", "épée", "bras", "slam"},
        {"echo_spore_queen", "têtes", nullptr, "pétales", "têtes", "bite"},
    };
    CHECK(kSimulationVersion >= 17);
    for (const Sample& sm : samples) {
        Arena now(sm.id, 17), before(sm.id, 16);
        CHECK(now.boss != nullptr && before.boss != nullptr);
        if (!now.boss || !before.boss) continue;
        CHECK(fightsByParts(*now.boss, *now.w));
        CHECK(!fightsByParts(*before.boss, *before.w));
        // A blow on its weak point takes twice, on its armour half, what the same blow on its box takes before.
        auto blow = [](Arena& a, const Rect& at) {
            DamageInfo d;
            d.amount = 1.0f;
            d.type = DamageType::Contact;
            d.fromPlayer = true;
            d.source = at.center();
            const float hp0 = a.boss->hp();
            a.w->damageEnemies(at, d, nullptr);
            return hp0 - a.boss->hp();
        };
        const float onBox = blow(before, Rect::fromCenter(before.boss->box.center(), {2.0f, 2.0f}));
        CHECK(onBox > 0.0f);
        const auto weak = now.spot(sm.weak);
        if (!weak) std::printf("  %s : pas de point faible « %s » à viser\n", sm.id, sm.weak);
        CHECK(weak.has_value());
        if (weak) {
            const auto hit = partHitBy(*now.boss, *weak);
            CHECK(hit && hit->role == PartRole::Weak && hit->damage == 2.0f);
            float mult = 0.0f;
            CHECK(reaches(*now.boss, *now.w, *weak, &mult) && mult == 2.0f);
            // The rules of 16: its box, × 1, exactly as before 2.76.
            CHECK(reaches(*before.boss, *before.w, *weak, &mult) == before.boss->box.intersects(*weak) && mult == 1.0f);
            const float loss = blow(now, *weak);
            if (std::abs(loss - 2.0f * onBox) >= 0.01f) std::printf("  %s : point faible %.2f, boîte %.2f\n", sm.id, loss, onBox);
            CHECK(std::abs(loss - 2.0f * onBox) < 0.01f);
        }
        if (sm.armour) {
            Arena fresh(sm.id, 17);
            const auto armour = fresh.spot(sm.armour);
            if (!armour) std::printf("  %s : pas de blindage « %s » à viser\n", sm.id, sm.armour);
            CHECK(armour.has_value());
            if (armour) {
                const auto hit = partHitBy(*fresh.boss, *armour);
                CHECK(hit && hit->role == PartRole::Armour && hit->damage == 0.5f);
                const float loss = blow(fresh, *armour);
                CHECK(std::abs(loss - 0.5f * onBox) < 0.01f);
            }
        }
        // Only its parts that hurt hurt: the part that hurts does, a part of its body does not.
        Arena calm(sm.id, 17);
        CHECK(calm.touch(sm.hurt, true).has_value());
        CHECK(calm.touch(sm.striker, false).has_value());
        // The part named for an attack hurts during it.
        CHECK(calm.perform(sm.attack));
        const auto striking = calm.touch(sm.striker, true);
        if (!striking) std::printf("  %s : « %s » ne blesse pas pendant %s\n", sm.id, sm.striker, sm.attack);
        CHECK(striking.has_value());
        // Its shots: from the origin of its parts (or its attack's own) in 17, from the attack's own in 16.
        for (const AttackDef& a : now.boss->def().attacks) {
            if (!shoots(a) || !originOf(now.boss->def(), a)) continue;
            Arena s17(sm.id, 17), s16(sm.id, 16);
            for (int i = 0; i < 30; ++i) {  // engaged for a moment (its rules read at each fixed step)
                s17.w->fixedUpdate({}, kSimDt);
                s16.w->fixedUpdate({}, kSimDt);
            }
            if (!s17.boss->forceAttack(*s17.w, a.id) || !s16.boss->forceAttack(*s16.w, a.id)) continue;
            const Vec2 o = *originOf(s17.boss->def(), a);
            const float k = s17.boss->def().scale;
            const Vec2 want = s17.boss->box.bottomCenter() + Vec2{s17.boss->facing * o.x * k, o.y * k};
            const Vec2 got = s17.boss->attackOrigin();
            if (std::abs(got.x - want.x) >= 0.01f || std::abs(got.y - want.y) >= 0.01f)
                std::printf("  %s · %s : tir de (%.1f, %.1f), attendu (%.1f, %.1f)\n", sm.id, a.id.c_str(), got.x, got.y, want.x, want.y);
            CHECK(std::abs(got.x - want.x) < 0.01f && std::abs(got.y - want.y) < 0.01f);
            const Vec2 old = attackOriginOf(a, s16.boss->box, s16.boss->facing, s16.boss->def().scale);
            const Vec2 got16 = s16.boss->attackOrigin();
            CHECK(got16.x == old.x && got16.y == old.y);
        }
    }
}

TEST_CASE("hit parts (2.76.2): with the rules of 15 and 16 every classic boss and écho keeps its box and its shots") {
    for (int rules : {15, 16}) {
        for (const EnemyDefinition* d : mine()) {
            Arena a(d->id, rules);
            CHECK(a.boss != nullptr);
            if (!a.boss) continue;
            CHECK(!fightsByParts(*a.boss, *a.w));
            const Rect probe = Rect::fromCenter(a.boss->box.center(), {2.0f, 2.0f});
            float mult = 0.0f;
            CHECK(reaches(*a.boss, *a.w, probe, &mult) && mult == 1.0f);
            for (const AttackDef& atk : d->attacks) {
                if (!shoots(atk) || !PartRules::of(d->id).origin(atk.id)) continue;
                if (!a.boss->forceAttack(*a.w, atk.id)) continue;
                const Vec2 old = attackOriginOf(atk, a.boss->box, a.boss->facing, d->scale);
                const Vec2 got = a.boss->attackOrigin();
                CHECK(got.x == old.x && got.y == old.y);
                break;
            }
        }
    }
}
