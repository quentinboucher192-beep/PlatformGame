// 2.76.0: the hit parts of the bosses (docs/HITPARTS.md): every boss has them for every frame of every
// clip of every sheet it is drawn with (tools/assetgen/hitparts.py, assets/hitparts), convex polygons
// inside their frame; Pyrrhax's rules (the values the player validated); the separating axes; Pyrrhax
// hit by its parts with the rules of 16 (its head × 2, its tail × 0,5, a wing beyond its old box), by
// its box with those of 15 (a replay of before); only its parts that hurt hurt on contact.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/animation/Animation.h"
#include "engine/audio/AudioSystem.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/enemies/HitParts.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/levels/LevelData.h"
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

bool convex(const std::vector<Vec2>& p) {
    int sign = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2 a = p[i], b = p[(i + 1) % p.size()], c = p[(i + 2) % p.size()];
        const float cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cr == 0.0f) continue;
        const int s = cr > 0.0f ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

Vec2 centroid(const std::vector<Vec2>& p) {
    Vec2 c{0.0f, 0.0f};
    for (const Vec2& v : p) c = c + v;
    return c * (1.0f / static_cast<float>(p.size()));
}

/// An empty arena with Pyrrhax engaged, with the rules of `rules`.
struct Lair {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Enemy* dragon = nullptr;
    explicit Lair(int rules) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_hitparts", "Test", "pyralis");
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
        for (int i = 0; i < 10; ++i) w->fixedUpdate({}, 1.0f / 120.0f);
        dragon = w->spawnEnemy("pyrrhax", {1200, 700}, false, "boss");
        if (dragon) {
            dragon->facing = 1.0f;
            dragon->alert(*w);
        }
        w->fixedUpdate({}, 1.0f / 120.0f);
    }
    /// The pieces of a part of its current pose, placed in the world.
    std::vector<std::vector<Vec2>> part(const std::string& name) const {
        std::vector<std::vector<Vec2>> out;
        const AnimationSetPtr& set = dragon->animator().set();
        const HitPartsSheet* hp = set ? HitPartsSheet::of(set->sourcePath) : nullptr;
        if (!hp) return out;
        const Rect src = dragon->animator().sourceRect();
        const auto* pieces = hp->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
        if (!pieces) return out;
        for (const HitPartPiece& p : *pieces)
            if (hp->parts[static_cast<size_t>(p.part)] == name)
                out.push_back(placePiece(p, *hp, dragon->box.bottomCenter(), dragon->def().scale, dragon->facing < 0.0f));
        return out;
    }
    /// A small box at the middle of the largest piece of a part, touching no other part's piece of higher priority.
    std::optional<Rect> spot(const std::string& name) const {
        const auto pieces = part(name);
        if (pieces.empty()) return std::nullopt;
        const auto& big = *std::max_element(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
        return Rect::fromCenter(centroid(big), {2.0f, 2.0f});
    }
};

}  // namespace

TEST_CASE("hit parts (2.76.0): every boss has its parts for every frame of every clip of every sheet, convex, inside the frame") {
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int bosses = 0, frames = 0, missing = 0;
    std::set<std::string> sheets;
    for (const EnemyDefinition* d : content().enemies()) {
        if (!d->isBoss) continue;
        ++bosses;
        std::vector<std::string> own = {d->animation};
        for (const BossPhase& p : d->phases)
            if (!p.animation.empty()) own.push_back(p.animation);
        for (const std::string& path : own) {
            if (!sheets.insert(path).second) continue;
            const HitPartsSheet* hp = HitPartsSheet::of(path);
            AnimationSetPtr set = res.animation(path);
            CHECK(set != nullptr);
            if (!hp) std::printf("  %s : pas de parties pour %s\n", d->id.c_str(), path.c_str());
            CHECK(hp != nullptr);
            if (!hp || !set) continue;
            CHECK(hp->frameWidth == set->frameWidth && hp->frameHeight == set->frameHeight);
            CHECK(!hp->parts.empty());
            for (const auto& [name, clip] : set->clips)
                for (int f = 0; f < clip.frameCount(); ++f) {
                    ++frames;
                    const std::vector<HitPartPiece>* pieces = hp->frame(*set, clip, f);
                    if (!pieces) {
                        if (++missing <= 5) std::printf("  %s : %s image %d sans parties\n", path.c_str(), name.c_str(), f);
                        continue;
                    }
                    for (const HitPartPiece& p : *pieces) {
                        CHECK(p.part >= 0 && p.part < static_cast<int>(hp->parts.size()));
                        CHECK(p.pts.size() >= 3 && p.pts.size() <= 10);
                        CHECK(convex(p.pts));
                        for (const Vec2& v : p.pts) CHECK(v.x >= 0.0f && v.y >= 0.0f && v.x <= hp->frameWidth && v.y <= hp->frameHeight);
                    }
                }
        }
    }
    std::printf("  %d boss, %zu feuilles, %d images\n", bosses, sheets.size(), frames);
    CHECK(bosses == 49);  // classic bosses, the gigaboss of the campaign and of Asterion, their « échos »
    CHECK(missing == 0);
}

TEST_CASE("hit parts (2.76.0): Pyrrhax's rules are those the player validated; every other boss keeps its box") {
    const PartRules& r = PartRules::of("pyrrhax");
    CHECK(r.fromVersion == 16 && r.profile == "dragon");
    struct Want {
        const char* name;
        PartRole role;
        float damage;
    };
    const Want want[] = {{"tête", PartRole::Weak, 2.0f},    {"cou", PartRole::Normal, 1.0f},  {"corps", PartRole::Normal, 1.0f},
                         {"aile gauche", PartRole::Normal, 1.0f}, {"aile droite", PartRole::Normal, 1.0f}, {"pattes", PartRole::Normal, 1.0f},
                         {"queue", PartRole::Armour, 0.5f}, {"griffes", PartRole::Hurt, 0.0f}, {"souffle", PartRole::Hurt, 0.0f}};
    for (const Want& wnt : want) {
        const PartRule* rule = r.rule(wnt.name);
        CHECK(rule != nullptr);
        if (rule) CHECK(rule->role == wnt.role && rule->damage == wnt.damage);
    }
    if (const PartRule* head = r.rule("tête")) CHECK(head->hurtDuring == std::vector<std::string>{"morsure"});
    CHECK(partDamageLabel(*r.rule("queue")) == "× 0,5");
    CHECK(partDamageLabel(*r.rule("tête")) == "× 2");
    CHECK(partDamageLabel(*r.rule("griffes")) == "—");
    const HitPartsSheet* hp = HitPartsSheet::of(content().enemy("pyrrhax")->animation);
    CHECK(hp != nullptr && hp->profile == "dragon");
    for (const EnemyDefinition* d : content().enemies())
        if (d->isBoss && d->id != "pyrrhax") CHECK(PartRules::of(d->id).fromVersion == 0 || PartRules::of(d->id).fromVersion > 16);
}

TEST_CASE("hit parts (2.76.0): separating axes between a convex polygon and a box or a circle") {
    const std::vector<Vec2> tri = {{0.0f, 0.0f}, {10.0f, 0.0f}, {0.0f, 10.0f}};
    CHECK(convexOverlapsRect(tri, {1.0f, 1.0f, 2.0f, 2.0f}));    // inside
    CHECK(!convexOverlapsRect(tri, {6.0f, 6.0f, 3.0f, 3.0f}));   // past the hypotenuse, inside its bounds
    CHECK(convexOverlapsRect(tri, {4.0f, 4.0f, 3.0f, 3.0f}));    // across the hypotenuse
    CHECK(!convexOverlapsRect(tri, {11.0f, 0.0f, 2.0f, 2.0f}));  // beside
    std::vector<Vec2> reversed(tri.rbegin(), tri.rend());
    CHECK(convexOverlapsRect(reversed, {4.0f, 4.0f, 3.0f, 3.0f}));
    CHECK(!convexOverlapsRect(reversed, {6.0f, 6.0f, 3.0f, 3.0f}));
    CHECK(convexContains(tri, {2.0f, 2.0f}) && !convexContains(tri, {6.0f, 6.0f}) && convexContains(reversed, {2.0f, 2.0f}));
    CHECK(convexOverlapsCircle(tri, {6.0f, 6.0f}, 1.5f));   // the hypotenuse is 0,71 away
    CHECK(!convexOverlapsCircle(tri, {8.0f, 8.0f}, 2.0f));  // 3,5 away
    CHECK(convexOverlapsCircle(tri, {3.0f, 3.0f}, 0.1f));   // its centre inside
    // A piece placed as the frame is: its feet at the pivot, × its scale, mirrored facing left.
    HitPartsSheet s;
    s.pivot = {10.0f, 20.0f};
    HitPartPiece p;
    p.pts = {{10.0f, 20.0f}, {14.0f, 20.0f}, {10.0f, 16.0f}};
    const auto right = placePiece(p, s, {100.0f, 50.0f}, 2.0f, false), left = placePiece(p, s, {100.0f, 50.0f}, 2.0f, true);
    CHECK(right[1].x == 108.0f && right[1].y == 50.0f && right[2].y == 42.0f);
    CHECK(left[1].x == 92.0f && left[0].x == 100.0f);
}

TEST_CASE("hit parts (2.76.0): Pyrrhax is hit by its parts with the rules of 16, by its box with those of 15") {
    Lair now(kSimulationVersion), before(15);
    CHECK(kSimulationVersion >= 16 && kHitPartsSimulation == 16);
    CHECK(now.dragon != nullptr && before.dragon != nullptr);
    if (!now.dragon || !before.dragon) return;
    CHECK(fightsByParts(*now.dragon, *now.w));
    CHECK(!fightsByParts(*before.dragon, *before.w));
    // Its head: a weak point (× 2); its tail: armour (× 0,5).
    const auto head = now.spot("tête"), tail = now.spot("queue");
    CHECK(head.has_value() && tail.has_value());
    if (head) {
        const auto hit = partHitBy(*now.dragon, *head);
        CHECK(hit && hit->name == "tête" && hit->damage == 2.0f);
        float mult = 0.0f;
        CHECK(reaches(*now.dragon, *now.w, *head, &mult) && mult == 2.0f);
    }
    if (tail) {
        const auto hit = partHitBy(*now.dragon, *tail);
        CHECK(hit && (hit->name == "queue" || hit->role == PartRole::Weak || hit->role == PartRole::Normal));
        if (hit && hit->name == "queue") CHECK(hit->damage == 0.5f);
    }
    // A blow on its head takes twice what the same blow on its box takes in the replay of before.
    if (head) {
        DamageInfo d;
        d.amount = 1.0f;
        d.type = DamageType::Contact;
        d.fromPlayer = true;
        const float hp0 = now.dragon->hp(), hp1 = before.dragon->hp();
        d.source = head->center();
        now.w->damageEnemies(*head, d, nullptr);
        const Rect onBox = Rect::fromCenter(before.dragon->box.center(), {2.0f, 2.0f});
        d.source = onBox.center();
        before.w->damageEnemies(onBox, d, nullptr);
        const float lossNow = hp0 - now.dragon->hp(), lossBefore = hp1 - before.dragon->hp();
        CHECK(lossBefore > 0.0f);
        CHECK(std::abs(lossNow - 2.0f * lossBefore) < 0.01f);
        float mult = 0.0f;
        const bool boxHit = reaches(*before.dragon, *before.w, *head, &mult);
        CHECK(mult == 1.0f && boxHit == before.dragon->box.intersects(*head));
    }
    // A wing beyond its old box: hit now, missed before.
    bool beyond = false;
    for (const auto& piece : now.part("aile gauche"))
        for (const Vec2& v : piece) {
            const Rect probe = Rect::fromCenter(centroid({v, centroid(piece)}), {2.0f, 2.0f});
            if (now.dragon->box.intersects(probe) || beyond) continue;
            const auto hit = partHitBy(*now.dragon, probe);
            if (hit && hit->name == "aile gauche") {
                beyond = true;
                float m = 0.0f;
                CHECK(!reaches(*before.dragon, *before.w, probe, &m));
            }
        }
    CHECK(beyond);
    // Only its parts that hurt hurt on contact: its claws do, the middle of its wing does not.
    const auto claws = now.spot("griffes");
    CHECK(claws.has_value());
    if (claws) CHECK(hurtsWithParts(*now.dragon, *claws));
    if (const auto wing = now.spot("aile gauche")) CHECK(!hurtsWithParts(*now.dragon, *wing));
}

// 2.76.2, the player's rule: with the rules of 17 touching ANY part of a boss by parts hurts on contact (its precise
// shape, not its box); with those of 16 (Pyrrhax's replays of 2.76.0) only its parts that hurt did.
TEST_CASE("hit parts (2.76.2): with the rules of 17 its whole body hurts on contact, with those of 16 only its red parts") {
    CHECK(kWholeBodyContactSimulation == 17 && kSimulationVersion >= kWholeBodyContactSimulation);
    Lair now(kSimulationVersion), v16(16);
    CHECK(now.dragon != nullptr && v16.dragon != nullptr);
    if (!now.dragon || !v16.dragon) return;
    // The middle of a wing: a part that does not hurt by itself, and a point beyond its box.
    const auto wing = now.spot("aile gauche"), wing16 = v16.spot("aile gauche");
    CHECK(wing.has_value() && wing16.has_value());
    if (!wing || !wing16) return;
    CHECK(touchesWithParts(*now.dragon, *wing));
    CHECK(!hurtsWithParts(*now.dragon, *wing) && !hurtsWithParts(*v16.dragon, *wing16));
    // Nova against it for one step: hurt with its contact damage in 17, untouched in 16.
    auto against = [](Lair& l, const Rect& spot) {
        Player& p = l.w->player();
        p.health = p.maxHealth();
        p.box = Rect::fromCenter(spot.center(), {4.0f, 4.0f});
        if (!touchesWithParts(*l.dragon, p.box) || hurtsWithParts(*l.dragon, p.box)) return -1;
        const int before = p.health;
        l.w->fixedUpdate({}, 1.0f / 120.0f);
        return before - p.health;
    };
    const int lost17 = against(now, *wing), lost16 = against(v16, *wing16);
    std::printf("  aile de Pyrrhax touchée : %d PV perdus en 17, %d en 16\n", lost17, lost16);
    CHECK(lost17 > 0);
    CHECK(lost16 == 0);
    // Far from it, nothing (its box alone never hurts).
    CHECK(!touchesWithParts(*now.dragon, Rect::fromCenter(now.dragon->box.center() + Vec2{0.0f, -2000.0f}, {4.0f, 4.0f})));
}

// ------------------------------------------------------------------ 2.76.2: the gigaboss of the campaign

namespace {

const char* const kCampaign[] = {"singularis", "monolithe", "phenix", "liche", "titan", "kraken", "astrarque"};

/// 2.76.2: an empty arena with a gigaboss engaged, its feet on the floor, with the rules of `rules`.
struct Den {
    ResourceManager res;
    AudioSystem audio;
    std::unique_ptr<GameWorld> w;
    Enemy* boss = nullptr;
    Den(const std::string& id, int rules) {
        res.init(nullptr, &audio);
        LevelData l = LevelData::makeEmpty("test_hitparts", "Test", "pyralis");
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
        for (int i = 0; i < 10; ++i) w->fixedUpdate({}, 1.0f / 120.0f);
        boss = w->spawnEnemy(id, {1200, 800}, false, "boss");
        if (boss) {
            boss->facing = 1.0f;
            boss->alert(*w);
        }
        w->fixedUpdate({}, 1.0f / 120.0f);
    }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) w->fixedUpdate({}, 1.0f / 120.0f);
    }
    /// Its pieces of a part, in its current pose, placed in the world.
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
    /// The pieces of its parts that always hurt, in its current pose.
    std::vector<std::vector<Vec2>> hurting() const {
        std::vector<std::vector<Vec2>> out;
        for (const PartRule& r : PartRules::of(boss->def().id).rules)
            if (r.role == PartRole::Hurt)
                for (auto& p : part(r.name)) out.push_back(std::move(p));
        return out;
    }
    /// A small box on `name`: the part a blow there touches first (a part that only hurts: inside it);
    /// `outside`: beyond its box; `calm`: inside none of its parts that always hurt.
    std::optional<Rect> probe(const std::string& name, bool outside = false, bool calm = false) const {
        const PartRule rule = PartRules::of(boss->def().id).ruleOr(name);
        const auto hurt = hurting();
        for (const auto& piece : part(name)) {
            const Vec2 c = centroid(piece);
            std::vector<Vec2> tries = {c};
            for (const Vec2& v : piece) {
                tries.push_back(centroid({v, c}));
                tries.push_back(centroid({v, c, v, v}));
            }
            for (const Vec2& t : tries) {
                const Rect r = Rect::fromCenter(t, {2.0f, 2.0f});
                if (outside && boss->box.intersects(r)) continue;
                if (calm && std::any_of(hurt.begin(), hurt.end(), [&](const auto& h) { return convexOverlapsRect(h, r); })) continue;
                if (rule.role == PartRole::Hurt) {
                    if (convexContains(piece, t)) return r;
                    continue;
                }
                const auto hit = partHitBy(*boss, r);
                if (hit && hit->name == name) return r;
            }
        }
        return std::nullopt;
    }
};

/// What one blow of 1 on `area` takes off `e` in `w`.
float blow(GameWorld& w, Enemy& e, const Rect& area) {
    DamageInfo d;
    d.amount = 1.0f;
    d.type = DamageType::Contact;
    d.fromPlayer = true;
    d.source = area.center();
    const float hp = e.hp();
    w.damageEnemies(area, d, nullptr);
    return hp - e.hp();
}

}  // namespace

TEST_CASE("hit parts (2.76.2): the gigaboss of the campaign have the rules of Pyrrhax's model, from 17") {
    struct Want {
        const char* boss;
        const char* part;
        PartRole role;
        float damage;
    };
    const PartRole W = PartRole::Weak, N = PartRole::Normal, A = PartRole::Armour, H = PartRole::Hurt;
    const Want want[] = {
        {"singularis", "œil", W, 2.0f},    {"singularis", "coquille", A, 0.5f}, {"singularis", "mains", N, 1.0f},
        {"singularis", "épines", H, 0.0f}, {"singularis", "couronne", H, 0.0f},
        {"monolithe", "tête", W, 2.0f},    {"monolithe", "carapace", A, 0.5f},  {"monolithe", "torse", N, 1.0f},
        {"monolithe", "bras", N, 1.0f},    {"monolithe", "poings", N, 1.0f},    {"monolithe", "jambes", N, 1.0f},
        {"phenix", "tête", W, 2.0f},       {"phenix", "corps", N, 1.0f},        {"phenix", "ailes", N, 1.0f},
        {"phenix", "queue", A, 0.5f},      {"phenix", "serres", H, 0.0f},
        {"liche", "crâne", W, 2.0f},       {"liche", "phylactère", W, 2.0f},    {"liche", "robe", N, 1.0f},
        {"liche", "bâton", N, 1.0f},       {"liche", "traîne", A, 0.5f},        {"liche", "flammes d'âme", H, 0.0f},
        {"titan", "tête", W, 2.0f},        {"titan", "réacteur", W, 2.0f},      {"titan", "blindage", A, 0.5f},
        {"titan", "torse", N, 1.0f},       {"titan", "canon", N, 1.0f},         {"titan", "poing", N, 1.0f},
        {"titan", "jambes", N, 1.0f},      {"titan", "cheminées", H, 0.0f},
        {"kraken", "yeux", W, 2.0f},       {"kraken", "manteau", N, 1.0f},      {"kraken", "tentacules", N, 1.0f},
        {"kraken", "bec", H, 0.0f},
        {"astrarque", "cœur", W, 2.0f},    {"astrarque", "anneaux", A, 0.5f},   {"astrarque", "plaques", A, 0.5f},
        {"astrarque", "coque", N, 1.0f},   {"astrarque", "bras", N, 1.0f},      {"astrarque", "chaînes", N, 1.0f},
        {"astrarque", "creusets", H, 0.0f}, {"astrarque", "pointes", H, 0.0f},
    };
    for (const char* id : kCampaign) {
        const PartRules& r = PartRules::of(id);
        CHECK(r.fromVersion == 17 && r.profile == "regions");
        size_t wanted = 0;
        for (const Want& w : want) {
            if (std::string(w.boss) != id) continue;
            ++wanted;
            const PartRule* rule = r.rule(w.part);
            if (!rule) std::printf("  %s : pas de règle pour « %s »\n", id, w.part);
            CHECK(rule != nullptr);
            if (rule) CHECK(rule->role == w.role && rule->damage == w.damage);
        }
        CHECK(r.rules.size() == wanted);
        // Every part of its sheets has its rule, every rule its part; every attack named exists.
        const EnemyDefinition* d = content().enemy(id);
        CHECK(d != nullptr);
        if (!d) continue;
        std::vector<std::string> sheets = {d->animation};
        for (const BossPhase& p : d->phases)
            if (!p.animation.empty()) sheets.push_back(p.animation);
        for (const std::string& path : sheets) {
            const HitPartsSheet* hp = HitPartsSheet::of(path);
            // The Astrarque wears the sheet of the Forgeron primordial (forge_core): its rig (2.76.2, step e; the
            // « mine » rule of hitparts.py), with the same part names; its own roles.
            const std::string wantProfile = std::string(id) == "astrarque" ? "rig" : "regions";
            CHECK(hp != nullptr && hp->profile == wantProfile);
            if (!hp) continue;
            CHECK(hp->parts.size() == r.rules.size());
            for (const std::string& n : hp->parts) CHECK(r.rule(n) != nullptr);
        }
        for (const PartRule& rule : r.rules)
            for (const std::string& a : rule.hurtDuring) CHECK(d->attack(a) != nullptr);
        // Its rushes (lunge) hurt by its parts only: some part hurts during each.
        for (const AttackDef& a : d->attacks) {
            if (a.type != AttackType::Lunge) continue;
            bool by = false;
            for (const PartRule& rule : r.rules) by = by || std::find(rule.hurtDuring.begin(), rule.hurtDuring.end(), a.id) != rule.hurtDuring.end();
            if (!by) std::printf("  %s : sa ruée « %s » ne blesse par aucune partie\n", id, a.id.c_str());
            CHECK(by);
        }
        for (const auto& [attack, o] : r.origins) CHECK(d->attack(attack) != nullptr);
    }
    // The parts that hurt during an attack (its fists striking, its tentacles lashing…).
    auto during = [](const char* boss, const char* part) { return PartRules::of(boss).ruleOr(part).hurtDuring; };
    CHECK(during("singularis", "coquille") == std::vector<std::string>{"chute"});
    CHECK(during("monolithe", "poings") == (std::vector<std::string>{"poings", "seisme", "derniere_secousse"}));
    CHECK(during("titan", "poing") == std::vector<std::string>{"poing"});
    CHECK(during("kraken", "tentacules") == (std::vector<std::string>{"etreinte", "fouet", "double_fouet", "ronde_fouets", "dernier_fouet"}));
    for (const char* p : {"coque", "plaques"}) CHECK(during("astrarque", p) == (std::vector<std::string>{"slam", "v3_dive", "a_ignivore_dive"}));
}

TEST_CASE("hit parts (2.76.2): each gigaboss of the campaign is hit by its parts with the rules of 17, by its box with those of 16") {
    CHECK(kSimulationVersion >= 17);
    for (const char* id : kCampaign) {
        Den now(id, kSimulationVersion), before(id, 16);
        CHECK(now.boss != nullptr && before.boss != nullptr);
        if (!now.boss || !before.boss) continue;
        CHECK(fightsByParts(*now.boss, *now.w));
        CHECK(!fightsByParts(*before.boss, *before.w));
        const PartRules& rules = PartRules::of(id);
        // Its weak point (× 2): twice what the same blow on its box takes in a replay of before.
        std::optional<Rect> weak;
        for (const PartRule& r : rules.rules)
            if (r.role == PartRole::Weak && !weak) weak = now.probe(r.name, false, true);
        if (!weak) std::printf("  %s : point faible introuvable dans sa pose\n", id);
        CHECK(weak.has_value());
        if (weak) {
            float mult = 0.0f;
            CHECK(reaches(*now.boss, *now.w, *weak, &mult) && mult == 2.0f);
            // Only its parts that hurt hurt on contact: its weak point does not.
            CHECK(!hurtsWithParts(*now.boss, *weak));
            const float a = blow(*now.w, *now.boss, *weak);
            const float b = blow(*before.w, *before.boss, Rect::fromCenter(before.boss->box.center(), {2.0f, 2.0f}));
            CHECK(b > 0.0f);
            if (std::abs(a - 2.0f * b) >= 0.01f) std::printf("  %s : point faible %.2f contre %.2f par la boîte\n", id, a, b);
            CHECK(std::abs(a - 2.0f * b) < 0.01f);
        }
        // Its armour (× 0,5), on fresh worlds (a blow leaves it untouchable for a moment).
        for (const PartRule& r : rules.rules) {
            if (r.role != PartRole::Armour) continue;
            Den now2(id, kSimulationVersion), before2(id, 16);
            const auto armour = now2.probe(r.name);
            if (!armour) std::printf("  %s : blindage « %s » introuvable dans sa pose\n", id, r.name.c_str());
            CHECK(armour.has_value());
            if (!armour) continue;
            const float a = blow(*now2.w, *now2.boss, *armour);
            const float b = blow(*before2.w, *before2.boss, Rect::fromCenter(before2.boss->box.center(), {2.0f, 2.0f}));
            if (std::abs(a - 0.5f * b) >= 0.01f) std::printf("  %s : blindage %.2f contre %.2f par la boîte\n", id, a, b);
            CHECK(std::abs(a - 0.5f * b) < 0.01f);
        }
        // A part beyond its old box: hit now, missed before.
        bool beyond = false;
        for (const PartRule& r : rules.rules) {
            if (r.role == PartRole::Hurt || beyond) continue;
            if (const auto out = now.probe(r.name, true)) {
                beyond = true;
                float m = 0.0f;
                CHECK(reaches(*now.boss, *now.w, *out, &m) && m == r.damage);
                CHECK(!reaches(*before.boss, *before.w, *out, &m));
            }
        }
        if (!beyond) std::printf("  %s : aucune partie hors de sa boîte\n", id);
        CHECK(beyond);
        // Its parts that hurt (spikes, flames, beak, talons…) hurt on contact, and are never hit.
        for (const PartRule& r : rules.rules) {
            if (r.role != PartRole::Hurt) continue;
            const auto spot = now.probe(r.name);
            if (!spot) continue;  // not drawn in this pose
            CHECK(hurtsWithParts(*now.boss, *spot));
            const auto hit = partHitBy(*now.boss, *spot);
            CHECK(!hit || hit->name != r.name);
        }
    }
}

TEST_CASE("hit parts (2.76.2): the parts of a gigaboss that hurt during an attack hurt during it") {
    struct During {
        const char* boss;
        const char* part;
        const char* attack;
    };
    const During during[] = {{"singularis", "coquille", "chute"}, {"monolithe", "poings", "poings"}, {"titan", "poing", "poing"},
                             {"kraken", "tentacules", "etreinte"}, {"astrarque", "coque", "slam"}};
    for (const During& d : during) {
        Den den(d.boss, kSimulationVersion);
        CHECK(den.boss != nullptr);
        if (!den.boss) continue;
        // At rest it does not hurt there (outside its parts that always hurt).
        if (const auto calm = den.probe(d.part, false, true)) CHECK(!hurtsWithParts(*den.boss, *calm));
        CHECK(den.boss->forceAttack(*den.w, d.attack));
        bool attacked = false, hurt = false;
        for (int i = 0; i < 600 && !hurt; ++i) {
            den.step();
            if (den.boss->state() != EnemyState::Attack || !den.boss->currentAttack() || den.boss->currentAttack()->id != d.attack) continue;
            attacked = true;
            if (const auto spot = den.probe(d.part, false, true)) hurt = hurtsWithParts(*den.boss, *spot);
        }
        if (!hurt) std::printf("  %s : « %s » ne blesse pas pendant « %s » (attaque %s)\n", d.boss, d.part, d.attack, attacked ? "vue" : "jamais vue");
        CHECK(attacked);
        CHECK(hurt);
    }
}

TEST_CASE("hit parts (2.76.2): the shots of the gigaboss of the campaign leave their drawing (eye, hands, crystal, beak, cannon)") {
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    int origins = 0;
    const std::pair<const char*, const char*> where[] = {
        {"singularis/rayon", "œil"},    {"singularis/eventail", "mains"}, {"liche/ames", "phylactère"}, {"liche/rayon", "phylactère"},
        {"phenix/souffle", "tête"},     {"phenix/plumes", "tête"},        {"titan/laser", "canon"},      {"titan/mitraille", "canon"},
        {"kraken/jet", "bec"},          {"kraken/encre", "tentacules"},   {"monolithe/regard", "tête"},  {"monolithe/jet", "poings"},
        {"astrarque/a_cryowyrm_breath", "cœur"}};
    for (const char* id : kCampaign) {
        const EnemyDefinition* d = content().enemy(id);
        CHECK(d != nullptr);
        if (!d) continue;
        const PartRules& rules = PartRules::of(id);
        for (const AttackDef& a : d->attacks) {
            const Vec2* byParts = rules.origin(a.id);
            if (!a.hasOrigin && !byParts) continue;
            const Vec2 o = byParts ? *byParts : a.origin;
            std::string path = d->animation;
            for (const BossPhase& ph : d->phases)
                if (std::find(ph.attacks.begin(), ph.attacks.end(), a.id) != ph.attacks.end()) {
                    if (!ph.animation.empty()) path = ph.animation;
                    break;
                }
            AnimationSetPtr set = res.animation(path);
            const HitPartsSheet* hp = HitPartsSheet::of(path);
            const AnimationClip* clip = set ? set->clip(a.attackAnim) : nullptr;
            CHECK(set != nullptr && hp != nullptr && clip != nullptr);
            if (!set || !hp || !clip) continue;
            // In the pixels of its frames (its feet at the pivot): on one of its parts, on an image of its blow.
            const Vec2 at = hp->pivot + o;
            std::set<std::string> on;
            for (int f = 0; f < clip->frameCount(); ++f)
                if (const auto* pieces = hp->frame(*set, *clip, f))
                    for (const HitPartPiece& p : *pieces)
                        if (convexOverlapsCircle(p.pts, at, 6.0f)) on.insert(hp->parts[static_cast<size_t>(p.part)]);
            if (on.empty()) std::printf("  %s · %s : ses tirs partent de (%.0f, %.0f), hors de son dessin\n", id, a.id.c_str(), o.x, o.y);
            CHECK(!on.empty());
            ++origins;
            for (const auto& [key, part] : where)
                if (std::string(id) + "/" + a.id == key) {
                    if (!on.count(part)) std::printf("  %s · %s : ses tirs ne partent pas de « %s »\n", id, a.id.c_str(), part);
                    CHECK(on.count(part) == 1);
                }
        }
    }
    CHECK(origins >= 35);
    // Astrarque by its parts: the bolts it took from Ignivore leave its arm, no longer the empty air above
    // it; its definition and the fingerprint of its level are unchanged (a replay of 16 keeps its origin).
    const AttackDef* bolts = content().enemy("astrarque")->attack("a_ignivore_bolts");
    const Vec2* mine = PartRules::of("astrarque").origin("a_ignivore_bolts");
    CHECK(bolts != nullptr && mine != nullptr);
    if (!bolts || !mine) return;
    auto firstShot = [&](int rules, Vec2 o) {
        Den den("astrarque", rules);
        if (!den.boss || !den.boss->forceAttack(*den.w, "a_ignivore_bolts")) return -1.0f;
        for (int i = 0; i < 600; ++i) {
            const size_t shots = den.w->projectiles().size();
            den.step();
            if (den.w->projectiles().size() <= shots) continue;
            const Vec2 want = den.boss->box.bottomCenter() + Vec2{den.boss->facing * o.x * den.boss->def().scale, o.y * den.boss->def().scale};
            float best = 1e9f;
            for (const Projectile& p : den.w->projectiles())
                if (!p.fromPlayer) best = std::min(best, (p.prevPos - want).length());
            return best;
        }
        return -1.0f;
    };
    const float now = firstShot(kSimulationVersion, *mine), old = firstShot(16, bolts->origin);
    std::printf("  Astrarque · a_ignivore_bolts : à %.1f px de son bras (règles 17), à %.1f px de l'ancienne origine (règles 16)\n", now, old);
    CHECK(now >= 0.0f && now < 12.0f);
    CHECK(old >= 0.0f && old < 12.0f);
}

TEST_CASE("hit parts (2.76.2): a boss fighting by parts takes the sheet of its phase in the fixed step, its pose kept") {
    Den now("singularis", kSimulationVersion), before("singularis", 16);
    CHECK(now.boss != nullptr && before.boss != nullptr);
    if (!now.boss || !before.boss) return;
    now.boss->setWounds(now.boss->maxHp() * 0.45f, 4);
    before.boss->setWounds(before.boss->maxHp() * 0.45f, 4);
    now.step();
    before.step();
    // No display update: the fixed step alone gives a boss by parts the sheet of its phase 5
    // (« Effondrement »); with the rules of 16 it is left to the display.
    CHECK(now.boss->animator().set()->sourcePath.find("singularis_p5") != std::string::npos);
    CHECK(before.boss->animator().set()->sourcePath.find("singularis_p5") == std::string::npos);
    const HitPartsSheet* hp = HitPartsSheet::of(now.boss->animator().set()->sourcePath);
    CHECK(hp != nullptr && hp->sheet.find("singularis_p5") != std::string::npos);
    // Its pose is kept on the new sheet: the same clip, the same image (Animator::swapSet).
    AnimationSetPtr a = now.res.animation("animations/singularis.json"), b = now.res.animation("animations/singularis_p5.json");
    CHECK(a != nullptr && b != nullptr);
    if (!a || !b) return;
    Animator anim;
    anim.setSet(a);
    anim.play("attack");
    for (int i = 0; i < 30; ++i) anim.update(1.0f / 120.0f);
    const int frame = anim.frame();
    CHECK(frame > 0);
    anim.swapSet(b);
    CHECK(anim.set() == b && anim.current() == "attack" && anim.frame() == frame);
}
