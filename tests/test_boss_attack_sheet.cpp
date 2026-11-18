// The attacks of a boss's sheet (2.60, Statistiques des boss › Fiche): each one read from the
// boss's definition (its phases, its last stand, its characteristics), folded or unfolded, one
// example at a time (the unfolded attack with the focus, else the last unfolded), and the
// example itself: a scene of the engine where the boss gives that attack in its own arena (or a
// neutral one), a training world where Nova cannot be hurt — never a real run.

#include <algorithm>
#include <cstdio>
#include <map>

#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/content/BossStories.h"
#include "game/enemies/BossAttackSheet.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossAttackDemo.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

const AttackFact* fact(const std::vector<AttackFact>& facts, const std::string& label) {
    for (const AttackFact& f : facts)
        if (f.label == label) return &f;
    return nullptr;
}

/// A boss of three phases: « a » in the first and the third, « b » in the second, « c » only in
/// its last stand, « d » in none, « e » a contact attack.
EnemyDefinition sampleBoss() {
    EnemyDefinition d;
    d.id = "essai";
    d.name = "Essai";
    d.isBoss = true;
    auto attack = [&](const char* id, AttackType t) {
        AttackDef a;
        a.id = id;
        a.name = std::string("Attaque ") + id;
        a.type = t;
        a.damage = 2.0f;
        a.telegraph = 0.8f;
        a.recover = 0.5f;
        a.cooldown = 2.0f;
        a.range = 300.0f;
        d.attacks.push_back(a);
    };
    attack("a", AttackType::Spread);
    attack("b", AttackType::Charge);
    attack("c", AttackType::Ring);
    attack("d", AttackType::Laser);
    attack("e", AttackType::Contact);
    BossPhase p1, p2, p3;
    p1.name = "Éveil";
    p1.attacks = {"a"};
    p2.name = "Colère";
    p2.hpThreshold = 0.66f;
    p2.attacks = {"b"};
    p2.telegraphMultiplier = 0.5f;
    p2.cooldownMultiplier = 0.5f;
    p3.name = "Fin";
    p3.hpThreshold = 0.33f;
    p3.attacks = {"a", "e"};
    p3.telegraphMultiplier = 0.75f;
    p3.speedMultiplier = 1.5f;
    p3.cooldownMultiplier = 0.5f;
    d.phases = {p1, p2, p3};
    d.finalAttacks = {"c"};
    return d;
}

}  // namespace

TEST_CASE("boss attack sheet: French numbers and multipliers") {
    CHECK(sheetNumber(0.6f, 2) == "0,6");
    CHECK(sheetNumber(12.0f, 1) == "12");
    CHECK(sheetNumber(1.25f, 2) == "1,25");
    CHECK(sheetNumber(0.35f, 2) == "0,35");
    CHECK(sheetNumber(-0.0f, 1) == "0");
    CHECK(sheetTimes(1.5f) == "×1,5");
}

TEST_CASE("boss attack sheet: phases, last stand and where each attack comes") {
    const EnemyDefinition d = sampleBoss();
    CHECK((attackPhases(d, "a") == std::vector<int>{0, 2}));
    CHECK((attackPhases(d, "b") == std::vector<int>{1}));
    CHECK(attackPhases(d, "c").empty());
    CHECK(attackWhere(d, "a") == "Phases 1 et 3");
    CHECK(attackWhere(d, "b") == "Phase 2");
    CHECK(attackWhere(d, "c") == "Dernier souffle");
    CHECK(attackWhere(d, "d") == "Aucune phase");
    // A boss without phases uses every attack in its only phase.
    EnemyDefinition flat = d;
    flat.phases.clear();
    flat.finalAttacks.clear();
    CHECK((attackPhases(flat, "d") == std::vector<int>{0}));
    CHECK(attackWhere(flat, "d") == "Phase unique");
}

TEST_CASE("boss attack sheet: wind-up and cooldown as the boss counts them") {
    const EnemyDefinition d = sampleBoss();
    const AttackDef& a = *d.attack("a");
    CHECK_NEAR(attackWindUp(d, a, 0), 0.8f, 1e-5);
    CHECK_NEAR(attackWindUp(d, a, 2), 0.8f * 0.75f / 1.5f, 1e-5);  // ÷ the phase's speed
    CHECK_NEAR(attackWindUp(d, *d.attack("b"), 1), 0.4f, 1e-5);
    // Never under 0.3 s for a boss.
    AttackDef quick = a;
    quick.telegraph = 0.1f;
    CHECK_NEAR(attackWindUp(d, quick, 0), 0.3f, 1e-5);
    CHECK_NEAR(attackCooldown(d, a, 0), 2.0f, 1e-5);
    CHECK_NEAR(attackCooldown(d, a, 2), 1.0f, 1e-5);
}

TEST_CASE("boss attack sheet: the characteristics read from the definition") {
    const EnemyDefinition d = sampleBoss();
    const std::vector<AttackFact> fa = attackFacts(d, *d.attack("a"));
    CHECK(fact(fa, "Type") && fact(fa, "Type")->value == attackTypeLabel(AttackType::Spread));
    CHECK(fact(fa, "Dégâts") && fact(fa, "Dégâts")->value == "2");
    CHECK(fact(fa, "Phases") && fact(fa, "Phases")->value == "1 (Éveil), 3 (Fin)");
    // Different in its two phases: each value with its phase.
    CHECK(fact(fa, "Annonce") && fact(fa, "Annonce")->value == "0,8 s (phase 1), 0,4 s (phase 3)");
    CHECK(fact(fa, "Recharge") && fact(fa, "Recharge")->value == "2 s (phase 1), 1 s (phase 3)");
    CHECK(fact(fa, "Portée") && fact(fa, "Portée")->value == "jusqu'à 300 px");
    CHECK(fact(fa, "Projectiles") != nullptr);
    CHECK(fact(fa, "Vitesse de la charge") == nullptr);
    // A charge: its speed, no projectile.
    const std::vector<AttackFact> fb = attackFacts(d, *d.attack("b"));
    CHECK(fact(fb, "Vitesse") && fact(fb, "Vitesse")->value == "260 px/s");
    CHECK(fact(fb, "Projectiles") == nullptr);
    CHECK(fact(fb, "Phase") && fact(fb, "Phase")->value == "2 (Colère)");
    // Its last stand's attack: in its last phase's rhythm.
    const std::vector<AttackFact> fc = attackFacts(d, *d.attack("c"));
    CHECK(fact(fc, "Phase") && fact(fc, "Phase")->value == "Dernier souffle");
    CHECK(fact(fc, "Annonce") && fact(fc, "Annonce")->value == "0,4 s");
    // A ring: the type's own defaults where the definition leaves 0.
    CHECK(fact(fc, "Portée de l'anneau") && fact(fc, "Portée de l'anneau")->value == "560 px");
    CHECK(fact(fc, "Ouverture") && fact(fc, "Ouverture")->value == "50°");
    // Only what exists: no « Durée de l'attaque » for a ring (its own time), one for a salvo.
    CHECK(fact(fc, "Durée de l'attaque") == nullptr);
    CHECK(fact(fa, "Durée de l'attaque") != nullptr);
}

TEST_CASE("boss attack sheet: where the example is played, or why it cannot be") {
    const EnemyDefinition d = sampleBoss();
    const AttackPreviewPlan a = attackPreviewPlan(d, *d.attack("a"));
    CHECK(a.showable && a.phase == 0 && !a.lastStand);
    const AttackPreviewPlan b = attackPreviewPlan(d, *d.attack("b"));
    CHECK(b.showable && b.phase == 1);
    const AttackPreviewPlan c = attackPreviewPlan(d, *d.attack("c"));
    CHECK(c.showable && c.lastStand && c.phase == 2);
    const AttackPreviewPlan none = attackPreviewPlan(d, *d.attack("d"));
    CHECK(!none.showable && !none.reason.empty());
    const AttackPreviewPlan contact = attackPreviewPlan(d, *d.attack("e"));
    CHECK(!contact.showable && !contact.reason.empty());
}

TEST_CASE("boss attack sheet: every boss of the game, every attack listed and read") {
    ContentDatabase content;
    content.reload();
    int bosses = 0, giga = 0, attacks = 0, finals = 0;
    for (const EnemyDefinition* d : content.enemies()) {
        if (!d->isBoss) continue;
        ++bosses;
        giga += isGigaBoss(*d) ? 1 : 0;
        const std::vector<BossAttackEntry> entries = bossAttackEntries(*d);
        CHECK(entries.size() == d->attacks.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            const BossAttackEntry& e = entries[i];
            CHECK(e.attack == &d->attacks[i]);
            CHECK(!e.summary.empty() && !e.description.empty() && !e.where.empty());
            CHECK(fact(e.facts, "Dégâts") && fact(e.facts, "Dégâts")->value == sheetNumber(e.attack->damage));
            // Every attack of the game's bosses is used by a phase or by its last stand: each
            // one can be shown.
            CHECK(e.plan.showable);
            CHECK(!e.phases.empty() || e.final);
            if (e.final && e.phases.empty()) {
                CHECK(e.plan.lastStand);
                ++finals;
            } else {
                CHECK(e.plan.phase == e.phases.front());
            }
            ++attacks;
        }
    }
    std::printf("  %d boss (%d giga-boss), %d attaques, %d du dernier souffle seulement\n", bosses, giga, attacks, finals);
    CHECK(bosses >= 30 && giga >= 6 && finals > 0);
}

TEST_CASE("boss attack sheet: folding, unfolding and the one example shown") {
    AttackFold f;
    CHECK(!f.anyExpanded() && f.preview().empty());
    f.toggle("a");
    CHECK(f.expanded("a") && f.preview() == "a");
    f.toggle("b");  // the last unfolded plays its example
    CHECK(f.expanded("b") && f.preview() == "b");
    // The focus on another unfolded attack: its example after a moment only.
    f.update("a", 0.0f);
    CHECK(f.preview() == "b");
    f.update("a", AttackFold::kDwell * 0.5f);
    CHECK(f.preview() == "b");
    f.update("a", AttackFold::kDwell);
    CHECK(f.preview() == "a");
    // A pointer passing over a row and leaving it: nothing changes.
    f.update("b", 0.05f);
    f.update("", 0.05f);
    CHECK(f.preview() == "a");
    // The focus on a folded attack, or elsewhere: the example stays.
    f.update("c", 1.0f);
    CHECK(f.preview() == "a");
    // Folding the attack shown: the last unfolded still open takes over.
    f.toggle("a");
    CHECK(!f.expanded("a") && f.preview() == "b");
    f.collapse("b");
    CHECK(f.preview().empty() && !f.anyExpanded());
    // « Tout déplier »: every attack; the example shown stays (or the first one).
    const std::vector<std::string> ids{"a", "b", "c"};
    f.expandAll(ids);
    CHECK(f.allExpanded(ids) && f.preview() == "a");
    f.collapse("c");
    CHECK(!f.allExpanded(ids));
    f.update("b", AttackFold::kDwell);  // first frame on it
    f.update("b", AttackFold::kDwell);
    CHECK(f.preview() == "b");
    f.expandAll(ids);
    CHECK(f.preview() == "b");
    // « Tout replier »: none, no example.
    f.collapseAll();
    CHECK(!f.anyExpanded() && f.preview().empty() && !f.allExpanded(ids));
    CHECK(!AttackFold{}.allExpanded({}));
}

TEST_CASE("boss attack example: the boss knows that attack only, without rage") {
    const EnemyDefinition d = sampleBoss();
    EnemyDefinition withRage = d;
    withRage.enrageTime = 90.0f;
    withRage.phases[2].enrageTime = 30.0f;
    const EnemyDefinition a = demoDefinition(withRage, "a", attackPreviewPlan(d, *d.attack("a")));
    for (const BossPhase& p : a.phases) {
        CHECK((p.attacks == std::vector<std::string>{"a"}));
        CHECK(p.enrageTime == 0.0f);
    }
    CHECK(a.enrageTime == 0.0f);
    CHECK((a.finalAttacks == std::vector<std::string>{"c"}));  // not in its last stand: untouched
    CHECK(a.attacks.size() == d.attacks.size());               // every attack still defined
    const EnemyDefinition c = demoDefinition(d, "c", attackPreviewPlan(d, *d.attack("c")));
    CHECK((c.finalAttacks == std::vector<std::string>{"c"}));
}

TEST_CASE("boss attack example: the part of the arena shown") {
    const Rect arena{1264, 64, 1536, 640};
    const Rect boss{2300, 480, 100, 160}, nova{1900, 618, 10, 22};
    const float aspect = 16.0f / 9.0f;
    const Rect w = demoWindow(arena, boss, nova, aspect);
    CHECK_NEAR(w.w / w.h, aspect, 1e-3);
    CHECK(w.containsRect(boss) && w.containsRect(nova));
    CHECK(w.x >= arena.x - 32.5f && w.right() <= arena.right() + 32.5f);
    CHECK(w.w < arena.w);  // the action, not the whole arena: Nova stays visible
    // A small arena: all of it (and a little beyond), never more than needed.
    const Rect small{0, 0, 960, 400};
    const Rect ws = demoWindow(small, Rect{600, 260, 60, 60}, Rect{300, 298, 10, 22}, aspect);
    CHECK(ws.containsRect(Rect{300, 298, 10, 22}) && ws.containsRect(Rect{600, 260, 60, 60}));
    CHECK(ws.w <= small.w + 64.5f);
    // A boss high above a giga arena (the Kraken): Nova kept in view, the frame never too wide.
    const Rect kraken{2319, 110, 130, 210}, low{1700, 618, 10, 22};
    const Rect wk = demoWindow(arena, kraken, low, aspect);
    CHECK(wk.w <= 1000.5f);
    CHECK(wk.containsRect(low));
    CHECK(wk.intersects(kraken));
}

namespace {

/// The game's content, its resources (no window), and where each boss is placed in the levels
/// of the game (its level, its level object): read once per test.
struct Bench {
    std::shared_ptr<ContentDatabase> content = std::make_shared<ContentDatabase>();
    ResourceManager res;
    AudioSystem audio;
    std::map<std::string, std::pair<std::string, std::string>> places;  // boss -> level, object
    Bench() {
        content->reload();
        res.init(nullptr, &audio);
        for (const LevelEntry* e : content->levels()) {
            if (e->source != ContentSource::Builtin) continue;
            auto l = content->loadLevel(e->id);
            if (!l) continue;
            for (const LevelObject& o : l->objects)
                if (o.type == "enemy" && !places.count(o.propStr("enemy"))) places[o.propStr("enemy")] = {e->id, o.id};
        }
    }
};

/// The example of `attackId` of `bossId` in its own level (or a neutral arena when `ownLevel`
/// is false), brought to its first wind-up by warm().
struct Example {
    Bench& bench;
    BossAttackDemo demo;
    bool setUp = false;
    Example(Bench& b, const std::string& bossId, const std::string& attackId, bool ownLevel = true, const EnemyDefinition* custom = nullptr)
        : bench(b) {
        const EnemyDefinition* d = custom ? custom : bench.content->enemy(bossId);
        if (!d) return;
        std::optional<LevelData> level;
        std::string object;
        if (auto it = bench.places.find(bossId); ownLevel && it != bench.places.end()) {
            level = bench.content->loadLevel(it->second.first);
            object = it->second.second;
        }
        std::string error;
        setUp = demo.setup(bench.content, bench.res, *d, attackId, std::move(level), object, &error);
        if (!setUp) std::printf("    %s / %s : %s\n", bossId.c_str(), attackId.c_str(), error.c_str());
    }
    bool warm() {
        while (!demo.ready() && !demo.failed()) demo.warmUp(120);
        if (demo.failed()) std::printf("    échec : %s\n", demo.failure().c_str());
        return demo.ready();
    }
};

}  // namespace

TEST_CASE("boss attack example: the boss gives the attack again and again, never a real run") {
    struct Case {
        const char* boss;
        const char* attack;
    };
    Bench bench;
    // A classic boss, a guardian of Aethra, giga bosses (one of them at its last stand).
    for (const Case& c : {Case{"thornwarden", "charge"}, Case{"aurorax", "mirror"}, Case{"titan", "presses"}, Case{"singularis", "derniere_lumiere"},
                          Case{"phenix", "lame"}}) {
        Example ex(bench, c.boss, c.attack);
        CHECK(ex.setUp);
        CHECK(ex.warm());
        GameWorld& w = *ex.demo.world();
        // A training world where Nova cannot be hurt: every recorder ignores it.
        CHECK(w.training());
        CHECK(w.invincible());
        const EnemyDefinition& def = *bench.content->enemy(c.boss);
        const AttackPreviewPlan plan = attackPreviewPlan(def, *def.attack(c.attack));
        CHECK(ex.demo.lastStand() == plan.lastStand);
        if (!plan.lastStand) CHECK(ex.demo.phase() == plan.phase);
        // The boss plays the example's definition (this attack only): the game's own is untouched.
        CHECK(ex.demo.boss() != nullptr);
        CHECK(&ex.demo.boss()->def() != &def);
        CHECK(ex.demo.boss()->def().phases.size() == def.phases.size());
        // A while later: given again, Nova whole and in the arena.
        for (int i = 0; i < 120 * 14; ++i) ex.demo.step();
        std::printf("  %-12s %-18s %d fois en %.0f s\n", c.boss, c.attack, ex.demo.shown(), ex.demo.time());
        CHECK(ex.demo.shown() >= 2);
        CHECK(w.player().health == w.player().maxHealth());
        CHECK(ex.demo.arena().contains(w.player().box.center()));
        CHECK(!ex.demo.over());
    }
}

TEST_CASE("boss attack example: a boss in no level of the game, in a neutral arena") {
    Bench bench;
    EnemyDefinition custom = *bench.content->enemy("glaciarch");
    custom.id = "boss_de_l_editeur";
    custom.builtin = false;
    Example ex(bench, "boss_de_l_editeur", "slam", false, &custom);
    CHECK(ex.setUp);
    CHECK(ex.warm());
    CHECK(ex.demo.world()->training());
    for (int i = 0; i < 120 * 8; ++i) ex.demo.step();
    CHECK(ex.demo.shown() >= 2);
    // An attack its phases never use: no example, the reason given.
    EnemyDefinition unused = custom;
    for (BossPhase& p : unused.phases) p.attacks.erase(std::remove(p.attacks.begin(), p.attacks.end(), "slam"), p.attacks.end());
    Example none(bench, "boss_de_l_editeur", "slam", false, &unused);
    CHECK(!none.setUp);
    CHECK(none.demo.failed() && !none.demo.failure().empty());
}

TEST_CASE("boss attack example: every boss of the game gets its examples, in its own arena") {
    // For each boss with a sheet (the planets' bosses and their echoes, the guardians of Aethra,
    // the giga bosses of Les Confins, Singularis): the example of its first attack, and of the
    // first attack of its last stand, starts in its arena.
    Bench bench;
    std::vector<std::pair<std::string, std::string>> cases;
    int neutral = 0;
    for (const EnemyDefinition* d : bench.content->enemies()) {
        if (!d->isBoss || !d->builtin || d->attacks.empty()) continue;
        // In no level of the campaign (the tutorial's guardian): its examples in a neutral arena.
        if (!bench.places.count(d->id)) {
            std::printf("  hors des niveaux, arène neutre : %s\n", d->id.c_str());
            // 2.69: a raid's gigaboss (Pyroclast, Skullmaw, Dreadfang…) is fought in its lair, never in a campaign level.
            if (d->raid.empty()) ++neutral;
        }
        cases.push_back({d->id, d->attacks.front().id});
        if (!d->finalAttacks.empty()) cases.push_back({d->id, d->finalAttacks.front()});
    }
    int failed = 0;
    for (const auto& [boss, attack] : cases) {
        Example ex(bench, boss, attack);
        const bool ok = ex.setUp && ex.warm() && ex.demo.world()->training() && ex.demo.world()->invincible() && ex.demo.shown() >= 1;
        if (!ok) {
            std::printf("  exemple RATÉ : %s / %s\n", boss.c_str(), attack.c_str());
            ++failed;
        }
    }
    std::printf("  %zu exemples préparés, %d ratés\n", cases.size(), failed);
    CHECK(cases.size() >= 40);
    CHECK(neutral <= 2);  // every boss of the campaign in its own arena
    CHECK(failed == 0);
}
