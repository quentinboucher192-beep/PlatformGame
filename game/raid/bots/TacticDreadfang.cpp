// 2.79 (docs/RAIDS.md §28): DREADFANG, the claws of the left arm (game/raid/DreadfangRoutine):
//
// * « Balayage des griffes »: a blade along one, two, three storeys at once, announced from one wall: a wave
//   along each storey to jump as it passes (its front and speed are known: from its wall, 1500 px/s);
// * « Griffe clouante »: the mark under the player it aims at (that player only leaves it); a player nailed:
//   its claw (no contact damage) is the group's first target, a rescue;
// * « Le bras enchaîné » (phase 2, its frost armour): its three chains' anchors held together (the high one by
//   the platforms), then it is pinned to the floor: its knuckle joints bare (× 2) — low on its arm, struck
//   from the floor beside it, never from inside its body;
// * otherwise its arm, from afar (its rush, its grip and its claws under the ice are its own blows).

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/DreadfangRoutine.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class DreadfangTactic final : public BossTactic {
public:
    const char* name() const override { return "Dreadfang"; }
    void read(const RaidRun& run, BossView& v) const override {
        readCommon(run, v);
        // 2.84, its fight plan: the shield kept for its sweeps (a sweep every 7,5 to 11 s, the shield back in 5 s) and
        // its touch; another storey before each sweep (its Storey dangers below); nobody kept close to it; bare, all in
        // front of it.
        v.plan.reserveShield = true;
        v.plan.elevate = 0.0f;    // measured: up on its storeys, fewer places to fire from, no fewer players down
        // (a wider distance to its arm, 170 px, measured the same: the default)
        v.plan.frontOnly = false;  // measured: all in front of it, its joints broken more slowly
        v.plan.noLure = true;     // measured: fewer players down without one keeping close to it
        v.plan.lureOnFloor = true;
        v.plan.freeOnClosed = true;  // its chains on the swept floor: off them when closed, back when they open
        v.plan.hideArmoured = false;  // measured: waiting far off cost more than it saved (groups wiped sooner)
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const auto* rt = dynamic_cast<const DreadfangRoutine*>(gb.routine());
        if (!b || !rt) return;
        const Json& x = gb.rules()->extra;
        const Json sw = x.value("sweep", Json::object());
        const float warn = jget<float>(sw, "warn", 1.6f), speed = jget<float>(sw, "speed", 1500.0f), height = jget<float>(sw, "height", 34.0f);
        std::vector<float> span;
        for (const auto& s : sw.value("span", Json::array()))
            if (s.is_number()) span.push_back(s.get<float>());
        const float x0 = span.size() == 2 ? span[0] : b->box.center().x - 1600.0f, x1 = span.size() == 2 ? span[1] : b->box.center().x + 1600.0f;
        for (const DreadfangRoutine::Sweep& s : rt->sweeps())
            for (int k : s.storeys) {
                if (k < 0 || k >= static_cast<int>(rt->storeys().size())) continue;
                const float y = rt->storeys()[static_cast<size_t>(k)];
                const bool out = rt->bladeOut(s);
                const float front = out ? rt->bladeX(s) : (s.dir > 0 ? x0 : x1);
                Danger d;
                d.kind = Danger::Kind::FloorWave;
                d.floorY = y;
                d.height = height;
                d.speed = static_cast<float>(s.dir) * speed;
                d.area = {front - 28.0f, y - height, 56.0f, height};
                d.in = out ? 0 : std::max(0, static_cast<int>(warn * static_cast<float>(kSimHz)) - s.t);
                d.shieldFirst = true;  // 1 500 px/s: its shield as the blade comes (kept for it), a jump only without it
                v.dangers.push_back(d);
                if (!out) {
                    // 2.84 (fight plan): announced, the storey itself — « changez d'étage » — while there is time.
                    Danger st;
                    st.kind = Danger::Kind::Storey;
                    st.floorY = y;
                    st.area = {x0, y - 40.0f, x1 - x0, 46.0f};
                    st.in = d.in;
                    v.dangers.push_back(st);
                }
            }
        // Its nailing claw: the mark under its player (leave it), then the claw to break (a rescue).
        const float reach = jget<float>(x.value("pin", Json::object()), "reach", 56.0f);
        const float pinWarn = jget<float>(x.value("pin", Json::object()), "warn", 1.2f);
        for (const DreadfangRoutine::Pin& p : rt->pins()) {
            if (!p.nailed) {
                Danger d;
                d.kind = Danger::Kind::Area;
                d.seat = p.seat;
                d.area = {p.at.x - reach - 10.0f, p.at.y - 44.0f, reach * 2.0f + 20.0f, 48.0f};
                d.in = std::max(0, static_cast<int>(pinWarn * static_cast<float>(kSimHz)) - p.t);
                v.dangers.push_back(d);
                continue;
            }
            v.held.push_back(p.seat);
            if (const Enemy* c = p.claw ? w.findEnemy(p.claw) : nullptr; c && c->alive()) {
                Target t;
                t.uid = p.claw;
                t.want = 3;
                t.priority = 30;
                t.melee = true;
                t.label = "Griffe clouante";
                v.targets.push_back(t);
            }
        }
        if (gb.stage() == Gigaboss::Stage::Carapace)
            v.phase = "Armure de givre : tenir ses " + std::to_string(gb.vents().size()) + " chaînes ensemble";
        else if (v.exposed)
            v.phase = "Enchaîné : ses jointures (× 2)";
        else
            v.phase = "Frapper le bras, sauter ses lames";
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Dreadfang";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeDreadfangTactic() { return std::make_unique<DreadfangTactic>(); }

}  // namespace cosmic::raid::bots
