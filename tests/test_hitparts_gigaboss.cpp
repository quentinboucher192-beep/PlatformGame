// 2.76.2 (step c): the 8 Gigaboss of Asterion fight by their parts from the simulation 17 (docs/HITBOX_PARTS.md):
// their regions (data/enemies « parts », profile « regions ») name the parts of every frame; in their lair
// with the rules of 17, a weak point takes × 2 and armour × 0,5, only their parts that hurt hurt; with the rules
// of 16 (a run of before) their box, as before.

#include <algorithm>
#include <cstdio>
#include <optional>
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
#include "game/online/LevelFingerprint.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidRun.h"
#include "tests/TestFramework.h"

using namespace cosmic;

namespace {

struct Lair {
    const char* part;
    int zone;
    const char* boss;
};
const Lair kLairs[] = {{"crane", 8, "skullmaw"},     {"bras_gauche", 8, "dreadfang"}, {"bras_droit", 8, "pyroclast"}, {"torse", 8, "heartbreaker"},
                       {"abdomen", 8, "voidworm"},   {"jambe_gauche", 8, "graviton"}, {"jambe_droite", 8, "titanus"}, {"coeur", 2, "asterion_coeur"}};

Vec2 centroid(const std::vector<Vec2>& p) {
    Vec2 c{0.0f, 0.0f};
    for (const Vec2& v : p) c = c + v;
    return c * (1.0f / static_cast<float>(p.size()));
}

/// The pieces of the parts of `role` of `e`'s current pose, placed in the world.
std::vector<std::pair<std::string, std::vector<Vec2>>> piecesOf(const Enemy& e, PartRole role) {
    std::vector<std::pair<std::string, std::vector<Vec2>>> out;
    const AnimationSetPtr& set = e.animator().set();
    const HitPartsSheet* hp = set ? HitPartsSheet::of(set->sourcePath) : nullptr;
    if (!hp) return out;
    const Rect src = e.animator().sourceRect();
    const auto* pieces = hp->cell(static_cast<int>(src.y) / set->frameHeight, static_cast<int>(src.x) / set->frameWidth);
    if (!pieces) return out;
    const PartRules& rules = PartRules::of(e.def().id);
    for (const HitPartPiece& p : *pieces) {
        const std::string& name = hp->parts[static_cast<size_t>(p.part)];
        if (rules.ruleOr(name).role == role) out.push_back({name, placePiece(p, *hp, e.box.bottomCenter(), e.def().scale, e.facing < 0.0f)});
    }
    return out;
}

}  // namespace

TEST_CASE("hit parts 2.76.2: the 8 Gigaboss of Asterion name their parts (regions) and fight by them from 17") {
    ContentDatabase content;
    content.reload();
    CHECK(kSimulationVersion >= 17);
    for (const Lair& l : kLairs) {
        const PartRules& r = PartRules::of(l.boss);
        CHECK(r.fromVersion == 17 && r.profile == "regions");
        int weak = 0, armour = 0, hurt = 0;
        for (const PartRule& rule : r.rules) {
            weak += rule.role == PartRole::Weak && rule.damage == 2.0f ? 1 : 0;
            armour += rule.role == PartRole::Armour && rule.damage == 0.5f ? 1 : 0;
            hurt += rule.role == PartRole::Hurt ? 1 : 0;
        }
        if (!weak || !hurt) std::printf("  %s : %d point(s) faible(s), %d partie(s) qui blesse(nt)\n", l.boss, weak, hurt);
        CHECK(weak >= 1 && hurt >= 1);
        const EnemyDefinition* d = content.enemy(l.boss);
        CHECK(d != nullptr);
        if (!d) continue;
        const HitPartsSheet* hp = HitPartsSheet::of(d->animation);
        CHECK(hp != nullptr && hp->profile == "regions");
        if (hp)
            for (const PartRule& rule : r.rules) CHECK(std::find(hp->parts.begin(), hp->parts.end(), rule.name) != hp->parts.end());
        // hurtDuring names attacks it has.
        for (const PartRule& rule : r.rules)
            for (const std::string& a : rule.hurtDuring) CHECK(d->attack(a) != nullptr);
    }
}

TEST_CASE("hit parts 2.76.2: in its lair, a Gigaboss is hit by its parts with the rules of 17, by its box with those of 16") {
    ContentDatabase content;
    content.reload();
    ResourceManager res;
    AudioSystem audio;
    res.init(nullptr, &audio);
    for (const Lair& l : kLairs) {
        const raid::Branch* b = raid::Branch::builtin(l.part);
        CHECK(b != nullptr);
        if (!b) continue;
        for (int rules : {16, kSimulationVersion}) {
            raid::RaidRun run(WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}, *b, raid::kMaxInstancePlayers);
            raid::RaidRun::Test t;
            t.on = true;
            t.invincible = true;
            t.players = 2;
            t.startPhase = 1;
            run.setTest(t);
            CHECK(run.begin(0, l.zone));
            run.world().setSimulationVersion(rules);
            std::vector<InputFrame> f(raid::kMaxInstancePlayers);
            for (int k = 2; k < raid::kMaxInstancePlayers; ++k) f[static_cast<size_t>(k)].flags = InputFrame::Vacant;
            const raid::ArenaInfo& arena = run.rules().arena;
            Enemy* boss = nullptr;
            for (int step = 0; step < 240; ++step) {
                if (step % 30 == 0)
                    for (int i = 0; i < 2; ++i) run.world().playerAt(i).teleportTo({arena.entry.x + 420.0f + 26.0f * static_cast<float>(i), arena.floor});
                run.step(f.data(), static_cast<int>(f.size()), 0);
            }
            for (auto& e : run.world().enemies())
                if (e->objectId() == run.rules().gigaboss.boss) boss = e.get();
            CHECK(boss != nullptr);
            if (!boss) continue;
            CHECK(fightsByParts(*boss, run.world()) == (rules >= 17));
            if (rules < 17) continue;
            // Its weak point: × 2; its armour: × 0,5 (when its pose shows them).
            for (PartRole role : {PartRole::Weak, PartRole::Armour}) {
                const auto pieces = piecesOf(*boss, role);
                bool found = false;
                for (const auto& [name, poly] : pieces) {
                    const Rect probe = Rect::fromCenter(centroid(poly), {2.0f, 2.0f});
                    const auto hit = partHitBy(*boss, probe);
                    if (!hit || hit->name != name) continue;  // a part of higher priority over it
                    found = true;
                    CHECK(hit->damage == (role == PartRole::Weak ? 2.0f : 0.5f));
                    float mult = 0.0f;
                    CHECK(reaches(*boss, run.world(), probe, &mult) && mult == hit->damage);
                    break;
                }
                if (!found && !pieces.empty()) std::printf("  %s : aucune partie %s seule sous une sonde\n", l.boss, partRoleId(role));
            }
            // Only its parts that hurt hurt: its middle (a normal part) does not.
            for (const auto& [name, poly] : piecesOf(*boss, PartRole::Normal)) {
                const Rect probe = Rect::fromCenter(centroid(poly), {2.0f, 2.0f});
                bool hurtsThere = false;
                for (const auto& [hname, hpoly] : piecesOf(*boss, PartRole::Hurt)) hurtsThere = hurtsThere || convexOverlapsRect(hpoly, probe);
                if (!hurtsThere) CHECK(!hurtsWithParts(*boss, probe));
                break;
            }
        }
    }
}
