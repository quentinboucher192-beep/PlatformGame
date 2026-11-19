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
