// 2.79 (docs/RAIDS.md §28): GRAVITON, the left leg (game/raid/GravitonRoutine, read through its marks):
//
// * its sectors: announced (« INVERSION »), then gravity turns over (« GRAVITÉ INVERSÉE ») and whoever is in one
//   falls toward the vault, whose field hurts — unless a slab is over them (a solid ceiling below the vault:
//   they stop under it). So: out of the sector, or under one of its slabs, before it turns;
// * its wells: a mark under each player, then a pull toward a hurting core: away from it;
// * its debris (« Chute de débris »): the marks under the players;
// * its field (phases 2 and 3): its generators held ALL AT THE SAME TIME, one player on each (the high ones on
//   their islands, by the platforms), held 2,5 s: it collapses, its gravity cores bare (× 2) for 12 s;
// * otherwise its body, from afar (it floats; it hurts on contact).

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/GigabossRoutine.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class GravitonTactic final : public BossTactic {
public:
    const char* name() const override { return "Graviton"; }
    void read(const RaidRun& run, BossView& v) const override {
        v.plan.escape = true;     // 2.90: its blows left by the navigation alone, with their time (docs/RAIDS.md §36)
        v.plan.bodyInNav = true;  // 2.90: its body among what the navigation keeps out of (measured with the escape)
        readCommon(run, v, false);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const GigabossRoutine* rt = gb.routine();
        if (!b || !rt) return;
        const Json se = gb.rules()->extra.value("sectors", Json::object());
        const float top = jget<float>(se, "top", 96.0f), band = jget<float>(se, "band", 40.0f), floor = jget<float>(se, "floor", 992.0f);
        const float warn = jget<float>(se, "warn", 2.0f);
        const float wellWarn = jget<float>(gb.rules()->extra.value("wells", Json::object()), "warn", 1.2f);
        std::vector<BossMark> marks;
        rt->marks(w, gb, marks);
        bool sectors = false;
        int gens = 0;
        for (const BossMark& m : marks) {
            if (m.kind == BossMark::Kind::Zone && (m.label == "INVERSION" || m.label == "GRAVITÉ INVERSÉE")) {
                Danger d;
                d.kind = Danger::Kind::Sector;
                d.area = m.area;
                d.in = m.strong ? 0 : static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * warn * static_cast<float>(kSimHz));
                v.dangers.push_back(d);
                sectors = true;
            } else if (m.kind == BossMark::Kind::Zone && m.strong && m.label.empty()) {
                Danger f{Danger::Kind::Area, m.area.expanded(6.0f)};  // the vault's field
                f.ceiling = true;
                v.dangers.push_back(f);
            } else if (m.kind == BossMark::Kind::Target && m.clip == "well") {
                Danger d;
                d.kind = Danger::Kind::Area;
                const Vec2 c = m.area.center();
                d.area = {c.x - 64.0f, c.y - 56.0f, 128.0f, 84.0f};
                d.in = static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * wellWarn * static_cast<float>(kSimHz));
                v.dangers.push_back(d);
            } else if (m.kind == BossMark::Kind::Plate && m.strong && (m.clip == "gen_on" || m.clip == "gen_held")) {
                Station s;
                s.plate = m.area;
                s.need = 1;
                s.priority = 15;
                s.label = "le générateur";
                v.stations.push_back(s);
                ++gens;
            }
        }
        // Its slabs: a solid ceiling over the floor, under the vault's field (a player rising stops there).
        if (sectors)
            for (const Collider& c : w.physics().colliders()) {
                if (!c.enabled || c.dynamic || c.kind != ColliderKind::Solid) continue;
                if (c.box.w > 600.0f || c.box.y <= top + band || c.box.bottom() >= floor - 40.0f) continue;
                v.shelters.push_back({c.box.x + 7.0f, c.box.bottom(), std::max(0.0f, c.box.w - 14.0f), floor - c.box.bottom()});
            }
        if (gb.stage() == Gigaboss::Stage::Carapace)
            v.phase = "Champ de gravité : tenir ses " + std::to_string(std::max(gens, 1)) + " générateurs en même temps";
        else if (v.exposed)
            v.phase = "Effondré : ses cœurs de gravité (× 2)";
        else
            v.phase = "Frapper la jambe, quitter les secteurs";
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Graviton";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeGravitonTactic() { return std::make_unique<GravitonTactic>(); }

}  // namespace cosmic::raid::bots
