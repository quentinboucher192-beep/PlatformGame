// 2.79 (docs/RAIDS.md §28): LE CŒUR D'ASTERION, the last fight (game/raid/AsterionHeartRoutine):
//
// * « Lances stellaires »: a mark follows each player, locks, then the lance falls: left once locked;
// * « Balayage des artères » and « Charge stellaire »: a blade or a comet along a storey (760 / 900 px/s,
//   52 / 64 px high: the comet takes the double jump), jumped as it passes;
// * « Pluie de magma », « Piège de gravité »: marks on the ground, wells pulling: away from them;
// * « Marée de plasma » (phases 3 and 4): the floor burns: onto the platforms and ledges before it rises;
// * « Les artères » (phases 2 and 3, its armour): its four artery plates held together (two on high balconies):
//   its three ventricles bare (× 2);
// * « Supernova » (phase 4): while it charges, enough players on its anchors (the arteries) to contain it;
// * its spawn, struck as it comes; otherwise the heart itself, from afar (it floats; it hurts on contact).

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidRun.h"
#include "game/raid/bots/BossTactic.h"

namespace cosmic::raid::bots {

namespace {

class HeartTactic final : public BossTactic {
public:
    const char* name() const override { return "Cœur d'Asterion"; }
    void read(const RaidRun& run, BossView& v) const override {
        v.sweepRead = true;  // 2.87: its « Rayon du cœur » read as it turns (the shield as it comes)
        // 2.89 (fight plan): its body floats over the ledges, drifting onto its nearest player as its crown turns: no jump of
        // the navigation goes through it (a striker on its way to a ventricle jumped into it and died there, again and again);
        // each artery its own player for the whole of its armour, revived first.
        v.plan.bodyInNav = true;
        v.plan.fixedHolders = true;
        v.plan.noLure = true;  // its crown drags it toward its nearest player: nobody stays close to it on purpose
        readCommon(run, v);
        const GameWorld& w = run.world();
        const Gigaboss& gb = run.gigaboss();
        const Enemy* b = gb.boss(w);
        const auto* rt = dynamic_cast<const AsterionHeartRoutine*>(gb.routine());
        if (!b || !rt) return;
        const Json& x = gb.rules()->extra;
        const float lanceWarn = jget<float>(x.value("lances", Json::object()), "warn", 1.3f);
        const float rainWarn = jget<float>(x.value("rain", Json::object()), "warn", 1.2f);
        const Json sw = x.value("sweeps", Json::object()), ch = x.value("charges", Json::object());
        std::vector<BossMark> marks;
        rt->marks(w, gb, marks);
        for (const BossMark& m : marks) {
            if (m.kind == BossMark::Kind::Target && m.label.empty()) {
                const bool well = m.clip == "well";
                const bool tracking = !well && m.progress <= 0.0f && !m.strong && m.area.w < 60.0f && m.area.h < 60.0f && m.area.w == m.area.h;
                if (tracking) continue;  // a lance still following its player: nothing to do yet
                Danger d;
                d.kind = Danger::Kind::Area;
                d.area = m.area.expanded(well ? 20.0f : 8.0f);
                d.area.h += 10.0f;
                const float warn = well ? 1.2f : m.area.w > 60.0f ? rainWarn : lanceWarn;
                // 2.87: a lance falls as a column (264 px over its mark, AsterionHeartRoutine::stepLances), not on its mark
                // alone: whoever jumps over the mark or stands on a ledge above it is struck too.
                if (!well && m.area.w <= 60.0f) {
                    const float bottom = m.area.bottom();
                    d.area = {m.area.x - 8.0f, bottom - 268.0f, m.area.w + 16.0f, 268.0f + 14.0f};
                }
                // Its magma rain the same: a column 900 px over the floor (stepRain), its ledges and platforms too.
                if (!well && m.area.w > 60.0f) d.area = {m.area.x - 8.0f, m.area.bottom() - 908.0f, m.area.w + 16.0f, 908.0f + 14.0f};
                d.in = m.strong ? 0 : static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * warn * static_cast<float>(kSimHz));
                v.dangers.push_back(d);
            } else if (m.kind == BossMark::Kind::Wave && (m.clip == "blade" || m.clip == "comet")) {
                const bool comet = m.clip == "comet";
                const Json& j = comet ? ch : sw;
                Danger d;
                d.kind = Danger::Kind::FloorWave;
                d.floorY = m.area.bottom();
                d.height = jget<float>(j, "height", comet ? 64.0f : 52.0f);
                d.speed = (m.flip ? -1.0f : 1.0f) * jget<float>(j, "speed", comet ? 900.0f : 760.0f);
                d.area = {m.area.center().x - 20.0f, d.floorY - d.height, 40.0f, d.height};
                v.dangers.push_back(d);
            } else if (m.kind == BossMark::Kind::Wave && m.clip == "rain") {
                v.dangers.push_back({Danger::Kind::Area, Rect{m.area.center().x - 36.0f, m.area.bottom() - 908.0f, 72.0f, 912.0f}});
            } else if (m.kind == BossMark::Kind::Zone && m.label.rfind("Marée", 0) == 0) {
                Danger d;
                d.kind = Danger::Kind::Flood;
                d.area = {m.area.x, m.area.y - 12.0f, m.area.w, m.area.h + 20.0f};
                d.in = static_cast<int>((1.0f - std::clamp(m.progress, 0.0f, 1.0f)) * 3.0f * static_cast<float>(kSimHz));
                v.dangers.push_back(d);
            }
        }
        // Its supernova charging: its anchors (the arteries) held by as many players as it asks.
        if (rt->novaLeft() > 0.0f && gb.rules()) {
            std::vector<Rect> anchors = gb.rules()->vents;
            std::stable_sort(anchors.begin(), anchors.end(), [](const Rect& a, const Rect& c) { return a.y > c.y + 1.0f; });
            const int need = std::min(rt->anchorsNeed(), static_cast<int>(anchors.size()));
            for (int k = 0; k < need; ++k) {
                Station s;
                s.plate = anchors[static_cast<size_t>(k)];
                s.need = 1;
                s.priority = 30;
                s.label = "l'ancre";
                v.stations.push_back(s);
            }
            v.phase = "Supernova : " + std::to_string(need) + " ancres à tenir";
            // 2.90 (fight plan): not contained, it burns everyone off its anchors (3 hearts; a quarter of the deaths of its
            // last phase): each of them takes it under the shield as it bursts.
            if (rt->anchorsHeld() < need && rt->novaLeft() * static_cast<float>(kSimHz) <= 30.0f)
                for (int k = 0; k < w.teamSize(); ++k) {
                    if (w.member(k).presence != GameWorld::Presence::Playing) continue;
                    const Rect box = w.playerAt(k).box;
                    bool on = false;
                    for (int a = 0; a < need; ++a) on = on || anchors[static_cast<size_t>(a)].intersects(box);
                    if (on) continue;
                    Danger d;
                    d.kind = Danger::Kind::Area;
                    d.area = box.expanded(24.0f);
                    d.seat = k;
                    d.in = static_cast<int>(rt->novaLeft() * static_cast<float>(kSimHz));
                    d.shieldFirst = true;
                    v.dangers.push_back(d);
                }
        } else if (gb.stage() == Gigaboss::Stage::Carapace) {
            v.phase = "Artères : tenir ses " + std::to_string(gb.vents().size()) + " plaques ensemble";
        } else if (v.exposed) {
            v.phase = "Ventricules à nu (× 2)";
            // 2.89 (fight plan): its arteries are held again as soon as its ventricles are covered (its armour comes back with
            // them open): their players wait on them instead of crossing the arena to its ventricles and back (half of each
            // cycle was that walk).
            if (gb.rules() && gb.phase() >= gb.rules()->carapaceFrom && gb.phase() < gb.rules()->carapaceUntil)
                for (const auto& vt : gb.vents()) {
                    Station s;
                    s.plate = vt.area;
                    s.need = 1;
                    s.priority = 9;
                    s.closed = true;  // nothing to hold yet: kept
                    s.label = "l'artère";
                    v.stations.push_back(s);
                }
        } else {
            v.phase = "Frapper le Cœur, quitter ses lances";
        }
        if (v.bodyTakesDamage) {
            Target t;
            t.uid = b->uid();
            t.want = 8;
            t.priority = v.exposed ? 8 : 5;
            t.melee = false;
            t.label = "Cœur d'Asterion";
            v.targets.push_back(t);
        }
        readAdds(w, *b, v, 6);
    }
};

}  // namespace

std::unique_ptr<BossTactic> makeHeartTactic() { return std::make_unique<HeartTactic>(); }

}  // namespace cosmic::raid::bots
