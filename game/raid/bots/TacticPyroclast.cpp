// 2.79 (docs/RAIDS.md §28): PYROCLAST, the magma hand of the right arm (raid::Gigaboss's own rules: no
// routine). Its fists fall on marks under each player, its open vents throw their jets in turn; armoured
// (its carapace, phase 2), its three vents held together (one player on each: the high centre one by the
// platforms) burst it: its knuckles' weak points bare (× 2) for 10 s, its body × 2 meanwhile. Its own
// blows (its fist, its quakes, its meteors, its tide of magma, its eruptions) are read as they come.

#include <algorithm>
#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/enemies/EnemyDefinition.h"
#include "game/gameplay/GameWorld.h"
#include "game/powers/Power.h"
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

/// 2.98: whether `p`'s shield is up when a blow lands `in` steps from now (raised then, or still up from before).
bool shieldFor(const Player& p, int in) {
    for (const auto& pw : p.powers()) {
        if (pw->id() != "shield") continue;
        const float at = static_cast<float>(in) / static_cast<float>(kSimHz);
        if (p.shieldActive()) return pw->def().duration - (pw->def().cooldown - pw->cooldownRemaining()) > at + 0.3f;
        return pw->cooldownRemaining() <= std::max(0.0f, at - 0.15f) && p.energy >= pw->activationCost(p) + 1.0f;
    }
    return false;
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
        // 2.98 (fight plan): its vents' players stood under every fist and jet on their vent (its shield), even when it was
        // not back yet (5 s): a fist every 5 s and a jet every 6 s, one of two went through (the first cause of their deaths,
        // its jet throwing them off their tower). A blow on a player on a vent that its shield will not stop is left (a step
        // along its ledge, back once it has struck: a closed vent loses nothing, an open one a little of its hold).
        if (gb.stage() == Gigaboss::Stage::Carapace) {
            const size_t fists = gb.fists().size();
            for (int k = 0; k < w.teamSize() && k < 8; ++k) {
                if (w.member(k).presence != GameWorld::Presence::Playing) continue;
                const Player& p = w.playerAt(k);
                bool onVent = false;
                for (const auto& vt : gb.vents()) onVent = onVent || vt.area.expanded(12.0f).intersects(p.box);
                if (!onVent) continue;
                for (size_t d = 0; d < v.dangers.size(); ++d) {
                    Danger& g = v.dangers[d];
                    if (g.kind != Danger::Kind::Area || g.attack || g.body || g.shot || g.unblockable || !g.area.intersects(p.box.expanded(5.0f))) continue;
                    bool jet = false;
                    for (const auto& vt : gb.vents()) jet = jet || std::fabs(g.area.bottom() - vt.area.bottom()) < 0.5f;
                    if ((d < fists || jet) && !shieldFor(p, g.in)) g.unblockable = true;
                }
            }
        }
        // 2.98 (fight plan): its tide of magma (its last phase: 150 px over the whole arena's floor for 6 s, every 10 s or so)
        // caught the group on the floor — they climbed as it rose, crowded at the edges of the few ledges above it, were thrown
        // back up into it and fell back again (most of the deaths of the measures). From that phase on, everyone fires from
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
