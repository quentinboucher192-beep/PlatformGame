// 2.79 (docs/RAIDS.md §28): TITANUS, the right leg (game/raid/TitanusRoutine: read through its marks, as a
// player reads them):
//
// * its stomps: the heel raised (« ONDE DE CHOC »), then two shockwaves run along the floor from its feet to the
//   walls (430 px/s, 40 px high): jumped as they pass, or watched from a platform;
// * its boulders and its « Écrasement »: marks on the ground (the world's cracks, the gigaboss's fists);
// * its armour (phases 2 and 3): its two ankle restraints, 150 px up its legs (out of reach of a blade from the
//   floor), struck with the wave aimed upward from the floor or from the platforms, both brought low, then
//   finished together (a restraint broken alone forges again after 1,5 s): it falls to its knee, its knee
//   and seams bare (× 2) for 11 s;
// * otherwise its legs, from afar (its charges and leaps are its own blows).

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/GigabossRoutine.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class TitanusTactic final : public BossTactic {
public:
    const char* name() const override { return "Titanus"; }
    void read(const RaidRun& run, BossView& v) const override {
        v.plan.escape = true;     // 2.90: its blows left by the navigation alone, with their time (docs/RAIDS.md §36)
        v.plan.bodyInNav = true;  // 2.90: its body among what the navigation keeps out of (measured with the escape)
        readCommon(run, v, false);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        if (!b) return;
        const Json wv = gb.rules()->extra.value("waves", Json::object());
        const float speed = jget<float>(wv, "speed", 430.0f), height = jget<float>(wv, "height", 40.0f), warn = jget<float>(wv, "warn", 0.9f);
        if (const GigabossRoutine* rt = gb.routine()) {
            std::vector<BossMark> marks;
            rt->marks(w, gb, marks);
            for (const BossMark& m : marks) {
                if (m.kind == BossMark::Kind::Wave) {
                    // A shockwave running from where it stomped (it walks slower than its waves: away from it).
                    Danger d;
                    d.kind = Danger::Kind::FloorWave;
                    d.floorY = m.area.bottom();
                    d.height = height;
                    const float x = m.area.center().x;
                    d.speed = (x >= b->box.center().x ? 1.0f : -1.0f) * speed;
                    d.area = {x - 14.0f, d.floorY - height, 28.0f, height};
                    v.dangers.push_back(d);
                } else if (m.kind == BossMark::Kind::Zone && m.label == "ONDE DE CHOC") {
                    // Its heel raised: two waves from its feet in `warn`.
                    for (float dir : {-1.0f, 1.0f}) {
                        Danger d;
                        d.kind = Danger::Kind::FloorWave;
                        d.floorY = m.area.bottom();
                        d.height = height;
                        d.speed = dir * speed;
                        const float x = m.area.center().x + dir * 30.0f;
                        d.area = {x - 14.0f, d.floorY - height, 28.0f, height};
                        d.in = static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * warn * static_cast<float>(kSimHz));
                        v.dangers.push_back(d);
                    }
                }
            }
        }
        if (gb.stage() == Gigaboss::Stage::Carapace) {
            // Its two restraints: struck together, each brought low, then finished within the window.
            const std::string id = jstr(gb.rules()->extra.value("restraints", Json::object()), "enemy", "titanus_entrave");
            int k = 0;
            for (const auto& e : w.enemies()) {
                if (!e->alive() || e->dormant() || e->def().id != id) continue;
                Target t;
                t.uid = e->uid();
                t.want = 4;
                t.priority = 25;
                t.sync = 1;
                t.holdAt = std::clamp(5.0f / std::max(1.0f, e->maxHp()), 0.12f, 0.5f);
                // 2.87: one brought low is held by one player (its strikers waited there doing nothing): the others on the
                // one still high.
                if (e->hpRatio() <= t.holdAt + 0.001f) {
                    t.want = 1;
                    t.priority = 24;
                }
                t.melee = false;
                t.label = e->center().x < b->center().x ? "Entrave ouest" : "Entrave est";
                v.targets.push_back(t);
                ++k;
            }
            v.phase = "Blindé : briser ses " + std::to_string(std::max(k, 2)) + " entraves ensemble";
        } else if (v.exposed) {
            v.phase = "À genou : sa rotule et ses coutures (× 2)";
        } else {
            v.phase = "Frapper ses jambes, sauter ses ondes";
        }
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Titanus";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeTitanusTactic() { return std::make_unique<TitanusTactic>(); }

}  // namespace cosmic::raid::bots
