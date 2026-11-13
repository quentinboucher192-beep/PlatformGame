// Integrity of the shipped content: every enemy, power, planet and level references
// assets, clips, projectiles and ids that exist.

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/levels/ObjectTypes.h"
#include "game/planets/PlanetData.h"
#include "game/powers/Power.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {
struct Env {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Env() {
        content.reload();
        res.init(nullptr, &audio);
    }
};

// Problems are collected so that one run lists all of them.
std::vector<std::string> g_problems;
void fail(const std::string& what) { g_problems.push_back(what); }
void report() {
    for (const auto& p : g_problems) std::cout << "  " << p << "\n";
    bool ok = g_problems.empty();
    g_problems.clear();
    CHECK(ok);
}
}  // namespace

TEST_CASE("content: every enemy has its sheet, clips, projectiles and references") {
    Env env;
    auto projectiles = env.res.animation("animations/projectiles.json");
    CHECK(projectiles != nullptr);
    int bosses = 0;
    for (const EnemyDefinition* d : env.content.enemies()) {
        if (!d->builtin) continue;
        auto anim = env.res.animation(d->animation);
        if (!anim || !env.res.exists(anim->texturePath)) {
            fail("animation manquante pour " + d->id + " : " + d->animation);
            continue;
        }
        if (!anim->clip("idle")) fail(d->id + " : clip idle manquant");
        if (!anim->clip("hurt")) fail(d->id + " : clip hurt manquant");
        if (!anim->clip("death")) fail(d->id + " : clip death manquant");
        if (d->canTeleport && (!anim->clip("teleport_out") || !anim->clip("teleport_in")) && !d->flying)
            fail(d->id + " : clips de téléportation manquants");
        if (d->canDefend && !anim->clip("defend")) fail(d->id + " : clip defend manquant");
        for (const AttackDef& a : d->attacks) {
            if (a.type != AttackType::Contact && !anim->clip(a.telegraphAnim) && a.telegraphAnim != "telegraph")
                fail(d->id + "/" + a.id + " : clip " + a.telegraphAnim + " manquant");
            if (a.type != AttackType::Contact && !anim->clip(a.attackAnim) && a.attackAnim != "attack")
                fail(d->id + "/" + a.id + " : clip " + a.attackAnim + " manquant");
            if (a.isProjectileType() && projectiles && !projectiles->clip(a.projectile))
                fail(d->id + "/" + a.id + " : projectile " + a.projectile + " inconnu");
            if (a.type == AttackType::Summon && !env.content.enemy(a.summonId))
                fail(d->id + "/" + a.id + " : renfort " + a.summonId + " inconnu");
            if (!a.sound.empty() && !env.res.exists(a.sound)) fail(d->id + " : son " + a.sound + " manquant");
        }
        if (d->deathSpawnCount > 0 && !env.content.enemy(d->deathSpawnId))
            fail(d->id + " : division en " + d->deathSpawnId + " inconnue");
        for (const auto& s : {d->hurtSound, d->deathSound, d->alertSound})
            if (!s.empty() && !env.res.exists(s)) fail(d->id + " : son " + s + " manquant");
        if (d->isBoss) {
            ++bosses;
            CHECK(!d->phases.empty());
            for (const BossPhase& ph : d->phases)
                for (const auto& id : ph.attacks)
                    if (!d->attack(id)) fail(d->id + " : phase " + ph.name + " → attaque " + id + " inconnue");
        }
    }
    report();
    CHECK(env.content.enemies().size() >= 48);
    CHECK(bosses >= 9);
}

TEST_CASE("content: every power has a behaviour, an icon and its sounds") {
    Env env;
    for (const char* id : {"dash", "wave", "shield", "impulse", "teleport", "glide", "ground_pound", "charge_shot",
                           "grapple", "triple_jump"}) {
        const PowerDefinition* p = env.content.power(id);
        if (!p) {
            fail(std::string("pouvoir manquant : ") + id);
            continue;
        }
        CHECK(PowerRegistry::instance().has(p->behavior));
        if (!p->sound.empty() && !env.res.exists(p->sound)) fail(std::string(id) + " : son " + p->sound + " manquant");
        CHECK(p->icon >= 0 && p->icon < 10);
    }
    auto unlock = env.res.animation("animations/power_unlock.json");
    CHECK(unlock != nullptr);
    for (const char* id : {"glide", "ground_pound", "charge_shot", "grapple", "triple_jump"})
        if (unlock && !unlock->clip(id)) fail(std::string("tablette sans symbole : ") + id);
    auto nova = env.res.animation("animations/nova.json");
    CHECK(nova != nullptr);
    for (const char* c : {"glide", "pound_dive", "pound_land", "charge", "shoot", "grapple", "grapple_pull"})
        if (nova && !nova->clip(c)) fail(std::string("animation du joueur manquante : ") + c);
    report();
}

TEST_CASE("content: object types reference existing assets") {
    Env env;
    for (const auto& t : ObjectTypes::all())
        for (const auto& a : t.assets)
            if (!env.res.exists(a)) fail("type " + t.id + " : asset manquant " + a);
    report();
}
