// Titan 12 (docs/RAIDS.md « Aucune embuscade derrière soi »): the ambushes of Asterion — the proof, for EVERY zone of
// the seven branches and of the Cœur that has an ambush:
//
// * every ambush of the level (an arena whose waves hold no boss) is an objective of role « ambush » of its zone, and no
//   pack of enemies (4 or more close together) is left outside an ambush: what looks like an ambush is one (it has its
//   check on the map, the ways on wait for it);
// * with ambush k not beaten (those before it beaten, their gates open and their mechanisms working, those after it not),
//   no move the simulated players know (raid::NavGraph: jumps, double and triple jump, dash, impulse, wall jumps, lifts
//   that run) reaches what lies past it: the next ambush, or the zone's ways on (its passages toward a zone further on);
//   once it is beaten, they are reached (the proof is not empty);
// * every gate (metadata.raid.gates) has a wall or its door on every row up to the top of the level, and waits for every
//   ambush of the zone laid before it.
//
// COSMIC_AMBUSH_AUDIT=1 prints every fault without stopping (the audit of the zones that were leaky).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/online/Raids.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "tests/TestFramework.h"

using namespace cosmic;
using namespace cosmic::raid;

namespace {

struct GateSvc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    GateSvc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

uint32_t everyBit(const Branch& br) {
    uint32_t s = 0;
    for (const ObjectiveInfo& o : br.objectives) s |= 1u << o.index;
    for (const EffectInfo& e : br.effects)
        if (e.bit >= 0 && e.bit < 32) s |= 1u << e.bit;
    return s;
}

std::unique_ptr<RaidRun> gateRun(GateSvc& s, const Branch& br, int zone, uint32_t state) {
    RaidRun::Test t;
    t.on = true;
    t.invincible = true;
    t.players = 2;
    auto run = std::make_unique<RaidRun>(s.ws(), br, kMaxInstancePlayers);
    run->setTest(t);
    run->setQuiet(true);
    if (!run->begin(state, zone)) return nullptr;
    return run;
}

/// An ambush of a zone: its arena, its objective (-1: none: a fault).
struct Ambush {
    std::string id;
    Rect box;
    int objective = -1;
};

/// The ambushes of a zone's level, west to east (an arena without a boss: CombatZoneEntity::ambush()).
std::vector<Ambush> ambushesOf(const Branch& br, const ZoneRules& rules, GameWorld& w) {
    std::vector<Ambush> out;
    for (const auto& e : w.entities()) {
        auto* cz = dynamic_cast<CombatZoneEntity*>(e.get());
        if (!cz || !cz->ambush()) continue;
        Ambush a;
        a.id = cz->id();
        a.box = cz->box;
        for (const ZoneObjective& zo : rules.objectives)
            if (zo.source == a.id && zo.trigger == "cleared" && zo.index >= 0 && br.objectives[static_cast<size_t>(zo.index)].role == "ambush")
                a.objective = zo.index;
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const Ambush& a, const Ambush& b) { return a.box.x < b.box.x; });
    return out;
}

/// The enemies of the level outside every arena's waves, in packs (4 or more, each within 24 tiles of the next).
std::vector<std::vector<const LevelObject*>> loosePacks(GameWorld& w) {
    std::vector<const CombatZoneEntity*> arenas;
    for (const auto& e : w.entities())
        if (auto* cz = dynamic_cast<const CombatZoneEntity*>(e.get())) arenas.push_back(cz);
    std::vector<const LevelObject*> loose;
    for (const LevelObject& o : w.level().objects) {
        if (o.type != "enemy") continue;
        bool listed = false;
        for (const CombatZoneEntity* a : arenas) listed = listed || a->lists(o.id);
        if (!listed) loose.push_back(&o);
    }
    std::sort(loose.begin(), loose.end(), [](const LevelObject* a, const LevelObject* b) { return a->rect.x < b->rect.x; });
    std::vector<std::vector<const LevelObject*>> packs;
    for (const LevelObject* o : loose) {
        if (packs.empty() || o->rect.x - packs.back().back()->rect.x > 24.0f * 32.0f) packs.emplace_back();
        packs.back().push_back(o);
    }
    packs.erase(std::remove_if(packs.begin(), packs.end(), [](const auto& p) { return p.size() < 4; }), packs.end());
    return packs;
}

/// Makes a mechanism work as its plate does (its doors open, its lift running).
void workPlate(GameWorld& w, const CoopPlate& c) {
    for (const std::string& t : c.targets)
        if (LevelEntity* e = w.entity(t)) {
            if (auto* d = dynamic_cast<DoorEntity*>(e)) d->setOpen(w, true, true);
            else e->handleAction(w, c.action, Json::object());
        }
}

/// Every node reached from `from` (the lifts in `stopped` not ridden).
std::vector<char> reachable(const NavGraph& g, const std::vector<int>& from, const std::set<uint32_t>& stopped) {
    std::vector<char> seen(static_cast<size_t>(g.size()), 0);
    auto blocked = [&](int n) {
        const int m = g.node(n).mover;
        return m >= 0 && stopped.count(g.movers()[static_cast<size_t>(m)].collider) != 0;
    };
    std::vector<int> stack;
    for (int f : from)
        if (f >= 0 && !seen[static_cast<size_t>(f)]) {
            seen[static_cast<size_t>(f)] = 1;
            stack.push_back(f);
        }
    while (!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        for (const NavGraph::Edge& e : g.edges(n)) {
            if (seen[static_cast<size_t>(e.to)] || blocked(n) || blocked(e.to)) continue;
            seen[static_cast<size_t>(e.to)] = 1;
            stack.push_back(e.to);
        }
    }
    return seen;
}

/// Where a player stands at `feet` (10 × 22, the simulated players' body).
Rect bodyAt(Vec2 feet) { return {feet.x - 5.0f, feet.y - 22.0f, 10.0f, 22.0f}; }

}  // namespace

TEST_CASE("raid ambush gates (titan 12): nothing past an ambush not beaten is reachable, in every zone of every branch and of the heart") {
    GateSvc s;
    const char* auditEnv = std::getenv("COSMIC_AMBUSH_AUDIT");
    const bool audit = auditEnv && *auditEnv;
    int zones = 0, proofs = 0, faults = 0;
    std::set<std::string> leaky;
    auto fault = [&](const Branch& br, int zone, const std::string& what) {
        std::printf("  %s zone %d : %s\n", br.part.c_str(), zone, what.c_str());
        leaky.insert(br.part + " " + std::to_string(zone));
        ++faults;
    };
    for (const Branch* b : Branch::all()) {
        const Branch& br = *b;
        for (const ZoneInfo& zi : br.zones) {
            const int zone = zi.id;
            const uint32_t all = everyBit(br);
            auto probe = gateRun(s, br, zone, all);
            if (!probe) continue;
            const ZoneRules& rules = probe->rules();
            const std::vector<Ambush> amb = ambushesOf(br, rules, probe->world());
            if (amb.empty()) continue;
            ++zones;
            // 1. Listed: every ambush is an objective « ambush » of the zone; no pack of enemies outside an ambush.
            for (const Ambush& a : amb)
                if (a.objective < 0) fault(br, zone, "embuscade " + a.id + " absente des objectifs « ambush »");
            for (const auto& pack : loosePacks(probe->world()))
                fault(br, zone, std::to_string(pack.size()) + " ennemis hors embuscade vers x = " + std::to_string(static_cast<int>(pack.front()->rect.x)) +
                                    " (rien n'attend qu'ils soient vaincus)");
            // 2. Every gate: its wall up to the top of the level, its list = the ambushes west of it.
            {
                GameWorld& w = probe->world();
                MoveOptions o;
                for (const AmbushGate& gt : rules.gates) {
                    for (const std::string& d : gt.doors) {
                        auto* door = dynamic_cast<DoorEntity*>(w.entity(d));
                        if (!door) {
                            fault(br, zone, "porte " + d + " du barrage " + gt.id + " introuvable");
                            continue;
                        }
                    }
                    for (float y = 8.0f; y < gt.rect.bottom(); y += 16.0f) {
                        bool wall = w.physics().overlapsSolid({gt.rect.center().x - 1.0f, y - 1.0f, 2.0f, 2.0f}, o);
                        for (const std::string& d : gt.doors)
                            if (LevelEntity* e = w.entity(d); e && e->box.contains({gt.rect.center().x, y})) wall = true;
                        if (!wall) fault(br, zone, "barrage " + gt.id + " : passage au-dessus à y = " + std::to_string(static_cast<int>(y)));
                    }
                    std::set<int> want, have(gt.ambushIndex.begin(), gt.ambushIndex.end());
                    for (const Ambush& a : amb)
                        if (a.objective >= 0 && a.box.right() <= gt.rect.x + 0.5f) want.insert(a.objective);
                    if (want != have || have.count(-1)) fault(br, zone, "barrage " + gt.id + " : liste d'embuscades fausse");
                }
            }
            // 3. Reach: with ambush k not beaten, nothing past it.
            std::vector<const Passage*> forward;
            for (const Passage& p : rules.passages)
                if (p.to > zone && p.plate.empty()) forward.push_back(&p);
            for (size_t k = 0; k < amb.size(); ++k) {
                if (k + 1 >= amb.size() && forward.empty()) continue;
                for (int beaten = 0; beaten < 2; ++beaten) {
                    uint32_t state = all;
                    for (size_t j = beaten ? k + 1 : k; j < amb.size(); ++j)
                        if (amb[j].objective >= 0) state &= ~(1u << amb[j].objective);
                    auto run = gateRun(s, br, zone, state);
                    if (!run) continue;
                    GameWorld& w = run->world();
                    // The mechanisms whose ambushes are beaten work (their players on them); the others do not.
                    std::set<uint32_t> stopped;
                    for (const CoopPlate& c : rules.coops) {
                        bool ready = true;
                        for (int a : c.ambushIndex) ready = ready && (a < 0 || Branch::done(state, a));
                        bool passage = false;  // the armory's « Prêts »: a passage of the group
                        for (const Passage& p : rules.passages) passage = passage || p.plate == c.id;
                        if (passage) continue;
                        if (ready) {
                            workPlate(w, c);
                            continue;
                        }
                        for (const std::string& t : c.targets)
                            if (auto* mp = dynamic_cast<MovingPlatformEntity*>(w.entity(t))) stopped.insert(mp->colliderId());
                    }
                    NavGraph g;
                    g.build(w, rules.magma.present ? rules.magma.zone : std::string());
                    // From where a player enters the zone: its start, and the ways from the zones before it.
                    std::vector<int> from;
                    for (const auto& [key, pts] : rules.spawns) {
                        if (key.rfind("cp:", 0) == 0) continue;
                        const bool start = key == "start";
                        const int src = start ? 0 : std::atoi(key.c_str());
                        if (!start && (src >= zone || Branch::isArmory(src))) continue;  // from further on: only once it is open
                        for (const Vec2& p : pts) from.push_back(g.locate(p));
                    }
                    const std::vector<char> seen = reachable(g, from, stopped);
                    // What lies past ambush k: the next ambush's arena; after the last one, the ways on that let a player
                    // through (a passage checks its lock as one walks into it: shut, it is no way on, wherever one stands).
                    std::vector<Rect> past;
                    if (k + 1 < amb.size()) past.push_back(amb[k + 1].box);
                    else
                        for (const Passage* p : forward)
                            if (run->passageOpen(*p)) past.push_back(p->rect.expanded(16.0f));
                    bool reached = false;
                    Vec2 where{};
                    for (int n = 0; n < g.size() && !reached; ++n) {
                        if (!seen[static_cast<size_t>(n)] || g.node(n).mover >= 0) continue;
                        const Rect body = bodyAt(g.node(n).feet);
                        for (const Rect& r : past) reached = reached || r.intersects(body);
                        if (reached) where = g.node(n).feet;
                    }
                    // The arena's trigger: never passed by (every place reached across its width is inside it).
                    if (!beaten) {
                        const Rect trig = amb[k].box.expanded(-12.0f);
                        for (int n = 0; n < g.size(); ++n) {
                            if (!seen[static_cast<size_t>(n)] || g.node(n).mover >= 0) continue;
                            const Vec2 f = g.node(n).feet;
                            if (f.x < trig.x || f.x > trig.right() || trig.intersects(bodyAt(f))) continue;
                            fault(br, zone, "embuscade " + amb[k].id + " : déclencheur évité à (" + std::to_string(static_cast<int>(f.x)) + ", " +
                                                std::to_string(static_cast<int>(f.y)) + ")");
                            break;
                        }
                    }
                    if (beaten && !reached) fault(br, zone, "après l'embuscade " + amb[k].id + " vaincue : la suite n'est pas atteinte");
                    if (!beaten && reached)
                        fault(br, zone, "embuscade " + amb[k].id + " non vaincue : la suite est atteinte (contournée) en (" + std::to_string(static_cast<int>(where.x)) +
                                            ", " + std::to_string(static_cast<int>(where.y)) + ")");
                    ++proofs;
                }
            }
        }
    }
    std::printf("  %d zones à embuscades, %d preuves, %d défauts", zones, proofs, faults);
    for (const std::string& z : leaky) std::printf(" · %s", z.c_str());
    std::printf("\n");
    if (!audit) CHECK(faults == 0);
    CHECK(zones >= 15);
}
