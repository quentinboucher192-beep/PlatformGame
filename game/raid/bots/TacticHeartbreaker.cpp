// 2.79 (docs/RAIDS.md §28): HEARTBREAKER, the rib cage of the torso (game/raid/HeartbreakerRoutine):
//
// * « La cuirasse » (phase 1): its four armour plates (two high, shot from the flank towers or upward from the
//   floor; two low): 2.85, each broken as soon as it can be (each takes 5 % of its health; once all four are broken
//   within the window after the first, its cuirasse falls);
// * « Battements »: every strong beat a wave runs along the floor from both its sides (520 px/s): jumped;
// * its « Côtes-pièges »: the marks under the players;
// * « La cage thoracique » (phase 2): its two ribs (the plates on the flank towers) held together: the light of
//   its heart bare (× 2,5) for 8 s;
// * otherwise its cage, from afar (it never moves; it hurts on contact).

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/HeartbreakerRoutine.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class HeartbreakerTactic final : public BossTactic {
public:
    const char* name() const override { return "Heartbreaker"; }
    void read(const RaidRun& run, BossView& v) const override {
        readCommon(run, v);
        // 2.85 (fight plan): its rib traps (the marks under every player, the first dangers read): under the shield when it
        // is ready rather than a step aside (into another's mark, off a platform, down among its waves).
        for (size_t k = 0; k < run.gigaboss().fists().size() && k < v.dangers.size(); ++k) v.dangers[k].shieldFirst = true;

        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const auto* rt = dynamic_cast<const HeartbreakerRoutine*>(gb.routine());
        if (!b || !rt) return;
        const Json be = gb.rules()->extra.value("beats", Json::object());
        const float speed = jget<float>(be, "speed", 520.0f), height = jget<float>(be, "height", 40.0f), floor = jget<float>(be, "floor", b->box.bottom());
        // 2.84, its fight plan: its beats throw waves along its floor only, every 1,25 to 2 s — the strikers fire from the
        // platforms above it.
        v.plan.elevate = 40.0f;
        v.plan.floorY = floor;
        v.plan.elevateAll = true;  // its lure too: above its waves, still the nearest
        // Where its strong beats throw their waves (both its sides, along the floor): too close to jump them in time,
        // a place to keep away from (a strong beat every few seconds).
        std::vector<float> from;
        for (const auto& f : be.value("from", Json::array()))
            if (f.is_number()) from.push_back(f.get<float>());
        if (from.empty()) from = {-170.0f, 170.0f};
        for (float f : from) {
            Danger d;
            d.kind = Danger::Kind::Area;
            const float x = b->box.center().x + f;
            d.area = {x - 120.0f, floor - height - 8.0f, 240.0f, height + 8.0f};
            d.in = kSimHz / 2;  // always near: never a place to stand
            v.dangers.push_back(d);
        }
        for (const HeartbreakerRoutine::Wave& wv : rt->waves()) {
            Danger d;
            d.kind = Danger::Kind::FloorWave;
            d.floorY = floor;
            d.height = height;
            d.speed = static_cast<float>(wv.dir) * speed;
            const float x = rt->waveFront(wv);
            d.area = {x - 16.0f, floor - height, 32.0f, height};
            v.dangers.push_back(d);
        }
        if (rt->plated()) {
            // 2.84: its ribs count only once its plates are broken (its cage): nobody waits on them before.
            v.stations.clear();
            int left = 0;
            for (uint32_t u : rt->plates()) {
                const Enemy* e = u ? w.findEnemy(u) : nullptr;
                if (!e || !e->alive()) continue;
                Target t;
                t.uid = u;
                t.want = 2;
                t.priority = 25;
                // 2.85 (fight plan): each plate broken as soon as it can be — every one takes its share of its health (5 %) and
                // the four together within their window were never reached (they grew back while held low together: its
                // cuirasse lasted 340 s, the group worn out before its cage). 3 / 8 → 5 / 8.
                t.melee = false;
                t.label = e->box.bottom() < b->box.center().y ? "Plaque haute" : "Plaque basse";
                v.targets.push_back(t);
                ++left;
            }
            v.phase = "Cuirasse : ses " + std::to_string(left) + " plaques ensemble";
        } else if (gb.stage() == Gigaboss::Stage::Carapace) {
            v.phase = "Cage thoracique : tenir ses 2 côtes ensemble";
        } else if (v.exposed) {
            v.phase = "Côtes écartées : la lumière de son cœur (× 2,5)";
        } else {
            v.phase = "Frapper la cage, sauter ses ondes";
        }
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Heartbreaker";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeHeartbreakerTactic() { return std::make_unique<HeartbreakerTactic>(); }

}  // namespace cosmic::raid::bots
