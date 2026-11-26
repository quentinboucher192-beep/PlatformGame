// 2.79 (docs/RAIDS.md §28): PYROCLAST, the magma hand of the right arm (raid::Gigaboss's own rules: no
// routine). Its fists fall on marks under each player, its open vents throw their jets in turn; armoured
// (its carapace, phase 2), its three vents held together (one player on each: the high centre one by the
// platforms) burst it: its knuckles' weak points bare (× 2) for 10 s, its body × 2 meanwhile. Its own
// blows (its fist, its quakes, its meteors, its tide of magma, its eruptions) are read as they come.

#include <algorithm>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

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
