// 2.79 (docs/RAIDS.md §28): PYROCLAST, the magma hand of the right arm (raid::Gigaboss's own rules: no
// routine). Its fists fall on marks under each player, its open vents throw their jets in turn; armoured
// (its carapace, phase 2), its three vents held together (one player on each: the high centre one by the
// platforms) burst it: its knuckles' weak points bare (× 2) for 10 s, its body × 2 meanwhile. Its own
// blows (its fist, its quakes, its meteors, its tide of magma, its eruptions) are read as they come.

#include <algorithm>

#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

/// 2.98: the height of its tide of magma in its phase now (« Marée de magma », its attacks of its current phase; 0: none).
float tideHeight(const Enemy& b) {
    const auto& phases = b.def().phases;
    const int k = b.phaseIndex();
    if (k < 0 || k >= static_cast<int>(phases.size())) return 0.0f;
    float h = 0.0f;
    for (const std::string& id : phases[static_cast<size_t>(k)].attacks)
        for (const AttackDef& a : b.def().attacks)
            if (a.id == id && a.type == AttackType::Flood) h = std::max(h, a.height);
    return h;
}

class PyroclastTactic final : public BossTactic {
public:
    const char* name() const override { return "Pyroclast"; }
    void read(const RaidRun& run, BossView& v) const override {
        // 2.89 (fight plan): its meteor fist read as it comes (its shadow locked, its fall), left dashing when too wide for a
        // walk; its body read on both sides of it (it turns toward its target and its arm swings over at once), 16 px wider
        // (it walks), and kept out of by the navigation's jumps (its body contacts were half of what hit the group).
        v.skyfallRead = true;
        v.plan.dashOut = true;
        v.plan.bodyBothSides = 16.0f;
        v.plan.bodyInNav = true;
        readCommon(run, v);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        if (!b) return;
        // 2.98 (fight plan): fewer players free to hold them than vents (a group of 2 or 4, or 8 with souls down): who stands on
        // a closed vent keeps it no more — it stays closed 15 s (20 s at two), the time to close the others in turn. Before, each
        // one stood on its own for ever, the last vent never closed and its carapace never burst.
        if (gb.stage() == Gigaboss::Stage::Carapace) {
            int inPlay = 0, down = 0;
            for (int k = 0; k < w.teamSize() && k < 8; ++k) {
                inPlay += w.member(k).presence == GameWorld::Presence::Playing ? 1 : 0;
                down += w.member(k).presence == GameWorld::Presence::Down ? 1 : 0;
            }
            const int revivers = down > 0 && inPlay >= 3 ? (inPlay >= 6 && down >= 2 ? 2 : 1) : 0;  // RaidBots' roles
            if (inPlay - revivers < static_cast<int>(gb.vents().size()))
                for (Station& st : v.stations)
                    if (st.closed) st.now = false;
        }
        // 2.98 (fight plan): its tide of magma (its last phase: 150 px over the whole arena's floor for 6 s, every 10 s or so)
        // caught the group on the floor — they climbed as it rose, crowded at the edges of the few ledges above it, were thrown
        // back up into it and fell back again (the first cause of deaths in that phase). From that phase on, everyone fires from
        // above its height, as against Voidworm's acid; a soul under it is raised in the first seconds after it falls.
        if (const float tide = tideHeight(*b); tide > 0.0f && run.rules().arena.present) {
            v.plan.floorY = run.rules().arena.floor;
            v.plan.elevate = tide;
            v.plan.elevateAll = true;
            v.plan.reviveWindow = 5.0f;
            // Its tide seen as it is wound up (0,9 s before it rises), not once it rises: those on the floor go up then.
            const AttackDef* a = b->currentAttack();
            if (a && a->type == AttackType::Flood && (b->state() == EnemyState::Telegraph || b->state() == EnemyState::Attack)) {
                const Rect ar = run.rules().arena.rect;
                Danger d;
                d.kind = Danger::Kind::Flood;
                d.area = {ar.x, v.plan.floorY - tide, ar.w, tide + 8.0f};
                v.dangers.push_back(d);
            }
        }
        // 2.98: from its 2nd phase the basalt under its towers gives way to magma — the antechamber beyond it (a health orb) is
        // no place to go any more: a player low on hearts crossed the pit for it and fell in.
        if (gb.phase() >= 2 && run.rules().arena.present) {
            const Rect ar = run.rules().arena.rect;
            Danger d;
            d.kind = Danger::Kind::Area;
            d.area = {ar.x - 2000.0f, ar.y - 400.0f, 2000.0f - 8.0f, ar.h + 800.0f};
            d.last = 1 << 20;
            v.dangers.push_back(d);
            d.area.x = ar.right() + 8.0f;
            v.dangers.push_back(d);
        }
        if (gb.stage() == Gigaboss::Stage::Carapace)
            v.phase = "Carapace : tenir ses " + std::to_string(gb.vents().size()) + " évents ensemble";
        else if (v.exposed)
            v.phase = "Carapace éclatée : ses points faibles (× 2)";
        else
            v.phase = "Frapper ses phalanges et son bras";
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Pyroclast";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makePyroclastTactic() { return std::make_unique<PyroclastTactic>(); }

}  // namespace cosmic::raid::bots
