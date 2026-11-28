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

// 2.98: a closed hook is let go while it stays closed longer than this (s): the time to run to another (150 px/s), climb, hold it.
constexpr float kLetGo = 16.0f;

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
        v.plan.reviveShielded = true;  // its body bites who kneels in it
        v.plan.hideArmoured = true;  // armoured, nothing to strike: off its body (its bites, its spit), not where its rings will be
        v.plan.bodyInNav = true;  // 2.89: its body among what the navigation's jumps keep out of (its ledges climb past it)
        v.plan.dashOut = true;  // 2.90: out of its maw, its spikes, its marks dashing when too far for a walk (the dash guards 0,31 s)
        v.plan.noLure = true;  // 2.89: it never moves: nobody keeps close to it on purpose (its lure died there, its revivers after)
        v.plan.holdersFire = true;  // 2.98: its parasites come out near its hooks (their bites, their orbs): its holders shoot them
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
        int free = 0;  // 2.98: the players in play out of its stomach
        for (int s = 0; s < w.teamSize(); ++s) {
            if (rt->spared(s)) v.held.push_back(s);
            else if (w.member(s).presence == GameWorld::Presence::Playing) ++free;
        }
        // 2.98: its hooks need not be held at once — each one closed stays closed `reopen` s (longer for one or two players in
        // play): fewer free players than hooks (a small group, the others down or swallowed), a hook closed with time left
        // is let go and its player closes the next one (two players stood on two hooks for ever, the third never closed).
        if (gb.stage() == Gigaboss::Stage::Carapace && free < static_cast<int>(gb.vents().size()))
            for (Station& st : v.stations)
                for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
                    if (st.closed && st.plate.x == gb.vents()[static_cast<size_t>(k)].area.x && st.plate.y == gb.vents()[static_cast<size_t>(k)].area.y &&
                        gb.reopenIn(k) > kLetGo)
                        st.now = false;
        // 2.89: its stomach, a closed pocket under the arena: a player digested there stays down (nobody reaches its soul).
        if (gb.rules())
            if (const Json st = gb.rules()->extra.value("swallow", Json::object()).value("stomach", Json()); st.is_array() && st.size() == 4) {
                const Rect stomach{st[0].get<float>(), st[1].get<float>(), st[2].get<float>(), st[3].get<float>()};
                v.noRevive.push_back(stomach);
                // 2.98: its acid floods the stomach too (under the floor, under its surface): who is held there takes its
                // tide under its shield (nowhere to climb, no way out but the pocket burst).
                for (const auto& fl : w.hazards().floods())
                    if (fl.left < stomach.right() && fl.right > stomach.x && fl.age < fl.rise + fl.hold + fl.fall)
                        for (int s : v.held) {
                            Danger d;
                            d.kind = Danger::Kind::Area;
                            d.area = stomach.expanded(8.0f);
                            d.seat = s;
                            d.last = static_cast<int>((fl.rise + fl.hold + fl.fall - fl.age) * static_cast<float>(kSimHz));
                            v.dangers.push_back(d);
                        }
            }
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
