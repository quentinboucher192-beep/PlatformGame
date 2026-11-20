// 2.79 (docs/RAIDS.md §23): SKULLMAW, the skull that sees everything (game/raid/SkullmawRoutine). What its
// routine shows a player, read the same way:
//
// * « Regards traqueurs »: a beam per player follows it, freezes (brighter), then fires along that line: the
//   frozen and firing beams are lines to step out of (the player it aims at only has to leave it in time);
// * « Voile psychique » (phases 2 and 3, its armour): its mirrors held by as many players as it asks (one alone,
//   four at seven or eight) all the time — the floor ones first, then the high ones by the platforms —, so that
//   its « Regard de mort » is thrown back at its flash; the others hide from its brow behind a column while it
//   charges (something solid between its eyes and them). Blind, its two eyes and its brow's crystal are bare
//   (× 2) for 10 s;
// * otherwise (phase 1, its last stand) its body, from afar (it hurts on contact).

#include <algorithm>
#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"
#include "game/raid/SkullmawRoutine.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class SkullmawTactic final : public BossTactic {
public:
    const char* name() const override { return "Skullmaw"; }
    void read(const RaidRun& run, BossView& v) const override {
        readCommon(run, v, false);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const auto* rt = dynamic_cast<const SkullmawRoutine*>(gb.routine());
        if (!b || !rt) return;
        const float track = 1.5f, lock = 0.45f;
        // Its gazes: frozen or firing, a line to step out of.
        for (const SkullmawRoutine::Gaze& z : rt->gazes()) {
            const SkullmawRoutine::GazeState s = rt->gazeState(z);
            if (s == SkullmawRoutine::GazeState::Tracking && rt->gazeProgress(z) < 0.92f) continue;
            Danger d;
            d.kind = Danger::Kind::Line;
            d.from = z.from;
            d.to = z.end;
            d.width = 18.0f + 10.0f;
            d.in = s == SkullmawRoutine::GazeState::Firing ? 0
                   : s == SkullmawRoutine::GazeState::Locked ? static_cast<int>((1.0f - rt->gazeProgress(z)) * lock * static_cast<float>(kSimHz))
                                                                : static_cast<int>(lock * static_cast<float>(kSimHz) + (1.0f - rt->gazeProgress(z)) * track * kSimHz);
            d.area = {std::min(d.from.x, d.to.x) - d.width, std::min(d.from.y, d.to.y) - d.width, std::fabs(d.to.x - d.from.x) + d.width * 2.0f,
                      std::fabs(d.to.y - d.from.y) + d.width * 2.0f};
            v.dangers.push_back(d);
        }
        if (rt->veil() && !v.exposed) {
            // The mirrors it asks for, the floor ones first (the easiest to keep), then the high ones.
            std::vector<const SkullmawRoutine::Mirror*> order;
            for (const auto& m : rt->mirrors()) order.push_back(&m);
            std::stable_sort(order.begin(), order.end(), [](const auto* a, const auto* c) { return a->area.y > c->area.y + 1.0f; });
            const int need = std::min(rt->mirrorsNeeded(), static_cast<int>(order.size()));
            for (int k = 0; k < need; ++k) {
                Station s;
                s.plate = order[static_cast<size_t>(k)]->area;
                s.need = 1;
                s.priority = 15;
                s.label = "le miroir";
                v.stations.push_back(s);
            }
            v.phase = "Voile psychique : " + std::to_string(need) + " miroirs à tenir";
            if (rt->charging()) {
                v.flash = true;
                v.eye = rt->brow(*b);
                v.flashIn = static_cast<int>((1.0f - rt->chargeRatio()) * 3.5f * static_cast<float>(kSimHz));
                v.phase = "Regard de mort : miroirs ou derrière une colonne";
            }
        } else if (v.exposed) {
            v.phase = "Aveuglé : ses yeux et son cristal (× 2)";
        } else {
            v.phase = "Frapper le crâne, quitter ses regards";
        }
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Skullmaw";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeSkullmawTactic() { return std::make_unique<SkullmawTactic>(); }

}  // namespace cosmic::raid::bots
