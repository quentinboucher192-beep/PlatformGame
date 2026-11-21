// 2.79 (docs/RAIDS.md §28): VOIDWORM, the worm of the abdomen (game/raid/VoidwormRoutine, read through its
// marks):
//
// * its « Fouissement »: a mark under each player (the gigaboss's fists): left before it bursts;
// * its maw: a green ring follows a player, then locks (« GUEULE »): that one leaves it before it snaps;
//   swallowed, a player waits in its stomach: the pocket on its neck (« CREVEZ LA POCHE ») is the group's first
//   target, a rescue;
// * its acid (phase 3): announced over the whole arena, its level known: everyone above it (the high roots);
// * its chitin (phase 2): its three hooks held together (the crown one by the platforms): its rings bare (× 2);
// * its parasites: struck when they come near; otherwise its head, from afar (it never moves, its body hurts).

#include <algorithm>
#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/GigabossRoutine.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class VoidwormTactic final : public BossTactic {
public:
    const char* name() const override { return "Voidworm"; }
    void read(const RaidRun& run, BossView& v) const override {
        readCommon(run, v);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const GigabossRoutine* rt = gb.routine();
        if (!b || !rt) return;
        // 2.84, its fight plan: once its acid floods the gut (from its 3rd phase), the strikers fire from above the
        // acid's height — never caught on the floor when it rises (3 s of warning, a climb of two storeys).
        if (gb.rules()) {
            const Json ac = gb.rules()->extra.value("acid", Json::object());
            if (gb.phase() >= jget<int>(ac, "from", 3)) {
                v.plan.floorY = jget<float>(ac, "floor", b->box.bottom());
                v.plan.elevate = jget<float>(ac, "height", 150.0f) + 12.0f;
                v.plan.reviveWindow = 5.0f;  // its acid every 22 s, 13 s up: 8 s dry
                v.plan.elevateAll = true;
            }
        }
        std::vector<BossMark> marks;
        rt->marks(w, gb, marks);
        for (const BossMark& m : marks) {
            if (m.kind == BossMark::Kind::Target && m.label.rfind("CREVEZ", 0) != 0 && m.area.h <= 24.0f) {
                // Its maw's ring: following its player (nothing to do yet), locked: leave it before it snaps.
                if (!m.strong) continue;
                Danger d;
                d.kind = Danger::Kind::Area;
                d.area = {m.area.x - 6.0f, m.area.y - 150.0f, m.area.w + 12.0f, 150.0f + m.area.h};
                d.in = static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * 1.8f * static_cast<float>(kSimHz));  // its track + lock
                d.unblockable = true;  // 2.84: a shield does not keep it from swallowing (a hook or a soul left for it)
                v.dangers.push_back(d);
            } else if (m.kind == BossMark::Kind::Zone && m.label.rfind("L'ACIDE", 0) == 0) {
                Danger d;
                d.kind = Danger::Kind::Flood;
                d.area = {m.area.x, m.area.y - 10.0f, m.area.w, m.area.h + 18.0f};
                d.in = static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * 3.0f * static_cast<float>(kSimHz));
                v.dangers.push_back(d);
            }
        }
        for (int s = 0; s < w.teamSize(); ++s)
            if (rt->spared(s)) v.held.push_back(s);
        // The pocket of its stomach: burst it to free the swallowed (a rescue).
        std::vector<uint32_t> mech;
        rt->targets(mech);
        for (uint32_t u : mech)
            if (const Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) {
                Target t;
                t.uid = u;
                t.want = 3;
                t.priority = 30;
                t.melee = false;
                t.label = "Poche de l'estomac";
                v.targets.push_back(t);
            }
        if (gb.stage() == Gigaboss::Stage::Carapace)
            v.phase = "Chitine : tenir ses " + std::to_string(gb.vents().size()) + " crochets ensemble";
        else if (v.exposed)
            v.phase = "Épinglé : ses anneaux (× 2)";
        else
            v.phase = "Frapper la tête, sortir des anneaux verts";
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Voidworm";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v, 6);  // its parasites: before its head (they come for the players)
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeVoidwormTactic() { return std::make_unique<VoidwormTactic>(); }

}  // namespace cosmic::raid::bots
