// 2.78 (docs/RAIDS.md §26): each player of a raid changes zone ALONE (the instance simulates every zone
// where one of its players is, one world each, in lockstep), the passages of the group (the armory's
// « Prêts ») take only the players on their plate, the lair's passage waits for every ambush of the zone
// before it, a zone whose players are all down goes on (the others come to revive them), the deaths and
// revivals of each player counted in the zones' tables, the « Tableau des contributions » measured, the
// lair's armory and the distance to its passage, and the cost of several zones at once.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Application.h"
#include "engine/core/I18n.h"
#include "engine/core/Scene.h"
#include "engine/resources/ResourceManager.h"
#include "game/GameServices.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"
#include "game/enemies/Enemy.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "game/raid/RaidTest.h"
#include "game/raid/ZoneGoals.h"
#include "saves/RaidHudLayout.h"
#include "tests/TestFramework.h"
#include "ui/online/ContribTable.h"
#include "ui/online/RaidHud.h"
#include "ui/screens/DownedView.h"
#include "ui/widgets/UIContext.h"

using namespace cosmic;

#ifdef _WIN32
#define COSMIC_NULL_DEVICE "NUL"
#else
#define COSMIC_NULL_DEVICE "/dev/null"
#endif

namespace {

struct Svc {
    ContentDatabase content;
    ResourceManager res;
    AudioSystem audio;
    Svc() {
        content.reload();
        res.init(nullptr, &audio);
    }
    WorldServices ws() { return WorldServices{res, audio, content, nullptr, 0.0f, 1.0f}; }
};

const raid::Branch& bras() {
    const raid::Branch* b = raid::Branch::builtin();
    CHECK(b != nullptr);
    return *b;
}

InputFrame idle() { return InputFrame{}; }
InputFrame interact() {
    InputFrame f;
    f.held = 1u << static_cast<int>(Action::Interact);
    return f;
}
InputFrame right() {
    InputFrame f;
    f.held = 1u << static_cast<int>(Action::MoveRight);
    f.moveX = 127;
    return f;
}

/// The frames of a step: `players` seats held (their inputs in `held`), the others vacant.
std::vector<InputFrame> seats(int players, std::vector<InputFrame> held = {}) {
    held.resize(raid::kMaxInstancePlayers);
    for (int k = players; k < raid::kMaxInstancePlayers; ++k) {
        held[static_cast<size_t>(k)] = InputFrame{};
        held[static_cast<size_t>(k)].flags = InputFrame::Vacant;
    }
    return held;
}

void stepN(raid::RaidRun& run, int players, int n, uint32_t mask = 0, std::vector<InputFrame> held = {}) {
    for (int t = 0; t < n; ++t) {
        auto f = seats(players, held);
        run.step(f.data(), static_cast<int>(f.size()), mask);
    }
}

const raid::Passage* passageTo(const raid::RaidRun& run, int zone, int to) {
    const raid::ZoneRules* r = run.rulesOf(zone);
    if (!r) return nullptr;
    for (const raid::Passage& p : r->passages)
        if (p.to == to) return &p;
    return nullptr;
}

/// Puts a seat inside a passage of its zone (it takes it at the next step, walking in).
void into(raid::RaidRun& run, int seat, const raid::Passage& p) {
    GameWorld* w = run.worldOf(run.zoneOf(seat));
    CHECK(w != nullptr);
    w->playerAt(seat).teleportTo({p.rect.center().x, p.rect.bottom() - 1.0f});
}

void killSeat(GameWorld& w, int i) {
    GameWorld::PlayerScope s(w, i);
    w.playerAt(i).kill(w);
}

/// Seat `who` revives seat `down` (in the same world): next to its soul, « Interagir » held.
void revive(raid::RaidRun& run, int players, int who, int down) {
    GameWorld& w = *run.worldOf(run.zoneOf(down));
    CHECK(w.member(down).presence == GameWorld::Presence::Down);
    w.playerAt(who).teleportTo(w.member(down).soul + Vec2{-14.0f, 0.0f});
    std::vector<InputFrame> held(raid::kMaxInstancePlayers);
    held[static_cast<size_t>(who)] = interact();
    for (int t = 0; t < 400 && w.member(down).presence == GameWorld::Presence::Down; ++t) stepN(run, players, 1, 0, held);
    CHECK(w.member(down).presence == GameWorld::Presence::Playing);
}

/// Titan 9: strikes down the enemies of the wave being fought of an ambush (as the group does, one step).
void strikeWave(GameWorld& w, const CombatZoneEntity& a) {
    for (const auto& e : w.entities())
        if (auto* es = dynamic_cast<EnemySpawnEntity*>(e.get()); es && a.lists(es->id()))
            if (Enemy* en = es->enemy(w); en && en->alive() && !en->dormant()) en->kill(w);
}

/// Titan 9: the group beats every ambush of `zone` itself (each one started, its waves struck down one after
/// the other): the objectives are the instance's own at once, before the server's.
void beatAmbushes(raid::RaidRun& run, int players, int zone, uint32_t mask = 0) {
    GameWorld& w = *run.worldOf(zone);
    for (CombatZoneEntity* a : w.ambushes()) {
        if (!a->started()) a->handleAction(w, "start", Json::object());
        for (int t = 0; t < 6000 && !a->cleared(); ++t) {
            strikeWave(w, *a);
            stepN(run, players, 1, mask);
        }
        CHECK(a->cleared());
    }
    stepN(run, players, 2, mask);
}

const raid::ZoneContribution* line(const std::vector<raid::ZoneContribution>& rows, int seat) {
    for (const raid::ZoneContribution& r : rows)
        if (r.seat == seat) return &r;
    return nullptr;
}

struct Blank : Scene {
    void render(float) override {}
};

}  // namespace

TEST_CASE("raid zones (2.78): a player who takes a passage changes zone alone, the others stay; each zone the same digest at 2 and 8 seats") {
    Svc s;
    for (int players : {2, 8}) {
        uint64_t zone1[2] = {}, zone3[2] = {}, all[2] = {};
        for (int k = 0; k < 2; ++k) {
            raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
            CHECK(run.begin(0, 1));
            stepN(run, players, 120);
            beatAmbushes(run, players, 1);  // titan 9: the Épaule's ways on wait for its guard
            CHECK(run.zonesLoaded() == std::vector<int>{1});
            GameWorld& w1 = *run.worldOf(1);
            w1.playerAt(0).health = 2;  // what a player carries: their hearts, their powers
            CHECK(w1.playerAt(0).removePower("shield"));
            const std::vector<std::string> powers = w1.carryOf(0).powers;
            const raid::Passage* toVeine = passageTo(run, 1, 3);
            CHECK(toVeine != nullptr);
            run.takeReports();
            into(run, 0, *toVeine);
            stepN(run, players, 1);
            // Seat 0 alone in the Veine; the others still in the Épaule; two worlds, in the order of their zones.
            CHECK(run.zoneOf(0) == 3);
            for (int p = 1; p < players; ++p) CHECK(run.zoneOf(p) == 1);
            CHECK(run.zonesLoaded() == (players > 1 ? std::vector<int>{1, 3} : std::vector<int>{3}));
            GameWorld& w3 = *run.worldOf(3);
            CHECK(w3.member(0).presence == GameWorld::Presence::Playing);
            CHECK(w3.playerAt(0).health == 2);
            CHECK(w3.carryOf(0).powers == powers);
            CHECK(run.worldOf(1)->member(0).presence == GameWorld::Presence::Vacant);
            CHECK(w3.member(1).presence == GameWorld::Presence::Vacant);
            CHECK(run.seatsIn(3) == 1u);
            // The server is told where each seat is (« zones »): 3 for seat 0, 1 for the others, 0 for the free seats.
            bool told = false;
            for (const raid::Report& r : run.takeReports())
                if (r.kind == raid::Report::Kind::Zones) {
                    CHECK(r.zone == 3);
                    CHECK(r.seats.size() == static_cast<size_t>(raid::kMaxInstancePlayers));
                    CHECK(r.seats[0] == 3);
                    for (int p = 1; p < raid::kMaxInstancePlayers; ++p) CHECK(r.seats[static_cast<size_t>(p)] == (p < players ? 1 : 0));
                    told = true;
                }
            CHECK(told);
            // The camera follows its seat: the zone shown is its own.
            run.setCameraFollow(0);
            CHECK(run.zone() == 3);
            run.setCameraFollow(1);
            CHECK(run.zone() == 1);
            // Both zones go on together, each with its own players.
            std::vector<InputFrame> held(raid::kMaxInstancePlayers);
            held[0] = right();
            stepN(run, players, 300, 0, held);
            CHECK(run.zoneOf(0) == 3);
            for (int p = 1; p < players; ++p) CHECK(run.zoneOf(p) == 1);
            zone1[k] = run.zoneHash(1);
            zone3[k] = run.zoneHash(3);
            all[k] = run.hash();
        }
        CHECK(zone3[0] != 0);
        CHECK(zone1[0] == zone1[1]);
        CHECK(zone3[0] == zone3[1]);
        CHECK(all[0] == all[1]);
    }
}

TEST_CASE("raid zones (2.78): the last player out of a zone unloads it (what it keeps stays); a free seat that joins goes where the others are") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    stepN(run, 2, 60);
    beatAmbushes(run, 2, 1);  // titan 9: the Épaule's ways on wait for its guard
    into(run, 0, *passageTo(run, 1, 3));
    stepN(run, 2, 1);
    into(run, 1, *passageTo(run, 1, 3));
    stepN(run, 2, 1);
    CHECK(run.zonesLoaded() == std::vector<int>{3});
    CHECK(run.memory().count(1) == 1);  // the Épaule keeps what was done there
    // A third player takes a free seat: in the Veine, where the group is.
    stepN(run, 3, 30);
    CHECK(run.zoneOf(2) == 3);
    CHECK(run.worldOf(3)->member(2).presence == GameWorld::Presence::Playing);
}

TEST_CASE("raid zones (2.78): a passage of the group opens only with the players it needs, and only those on its plate take it") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(bras().hasArmory());
    CHECK(run.begin(0, raid::RaidRun::kArmory));
    CHECK(run.zone() == 0);
    stepN(run, 3, 60);
    const raid::ZoneRules& r = *run.rulesOf(0);
    CHECK(!r.coops.empty());
    const raid::CoopPlate& plate = r.coops[0];
    const raid::Passage* road = nullptr;
    for (const raid::Passage& p : r.passages)
        if (p.plate == plate.id) road = &p;
    CHECK(road != nullptr);
    GameWorld& w = *run.worldOf(0);
    // Two of the three on the plate: it wants every player in play (3): nobody goes.
    for (int i : {0, 1}) w.playerAt(i).teleportTo({plate.rect.center().x - 10.0f + 20.0f * static_cast<float>(i), plate.rect.bottom() - 1.0f});
    stepN(run, 3, 30);
    CHECK(run.coop(0).on == 2);
    CHECK(run.coop(0).need == 3);
    CHECK(!run.coop(0).done);
    for (int i = 0; i < 3; ++i) CHECK(run.zoneOf(i) == 0);
    // The third one away (standby): two players in play, both on the plate: they go, together, at that step.
    std::vector<InputFrame> held(raid::kMaxInstancePlayers);
    held[2].flags = InputFrame::Standby;
    for (int t = 0; t < 30 && run.zoneOf(0) == 0; ++t) stepN(run, 3, 1, 0, held);
    CHECK(run.zoneOf(0) == bras().startZone);
    CHECK(run.zoneOf(1) == bras().startZone);
    CHECK(run.zoneOf(2) == 0);  // not on the plate: it stays
    CHECK(run.zonesLoaded() == (std::vector<int>{0, bras().startZone}));
    // Back (in play once it has appeared again), it walks through the passage alone (the plate done: the door stays open).
    stepN(run, 3, 240);
    into(run, 2, *road);
    stepN(run, 3, 2);
    CHECK(run.zoneOf(2) == bras().startZone);
    CHECK(run.zonesLoaded() == std::vector<int>{bras().startZone});
}

TEST_CASE("raid zones (2.78): every branch's lair waits for every ambush of the zone before it, through the lair's armory (the Cœur too)") {
    const std::vector<const raid::Branch*> list = raid::Branch::all();  // the seven branches, then the Cœur
    CHECK(list.size() == 8);
    for (const raid::Branch* b : list) {
        int lair = 0;
        for (const raid::ZoneInfo& z : b->zones)
            if (z.boss) lair = z.id;
        CHECK(lair > 1);
        const int before = lair - 1;
        const raid::Branch::AmbushCount all = b->ambushes(before, 0);
        CHECK(all.total == 4);
        CHECK(all.left == 4);
        // Its passages to the lair: locked « ambushes », through the lair's armory, a door that follows the lock.
        int ways = 0;
        for (const raid::ZoneRules& zr : b->rules)
            for (const raid::Passage& p : zr.passages)
                if (p.to == lair && zr.zone != raid::kLairArmory) {
                    CHECK(p.lock == "ambushes:" + std::to_string(before));
                    CHECK(p.via == raid::kLairArmory);
                    ++ways;
                }
        CHECK(ways >= 1);
        CHECK(!b->lockText("ambushes:" + std::to_string(before)).message.empty());
        // Its lair's armory: tablets, the « Prêts » plate that takes its players to the lair, a way back.
        CHECK(b->hasLairArmory());
        const raid::ZoneRules* ar = b->zoneRules(raid::kLairArmory);
        CHECK(ar != nullptr);
        CHECK(ar->coops.size() == 1);
        bool forward = false, back = false;
        for (const raid::Passage& p : ar->passages) {
            forward = forward || (p.to == lair && p.plate == ar->coops[0].id);
            back = back || (p.to == before && p.plate.empty());
        }
        CHECK(forward);
        CHECK(back);
        int tablets = 0;
        for (const LevelObject& o : b->level(raid::kLairArmory)->objects) tablets += o.type == "power_unlock";
        CHECK(tablets >= 10);
        // One ambush left: closed; « Embuscades restantes : 1 sur 4 » on its passage and in « Ce qu'il reste à faire ».
        uint32_t three = 0, guardOnly = 0, others = 0;
        int counted = 0;
        for (const raid::ObjectiveInfo& o : b->objectives)
            if (o.zone == before && o.role == "ambush") {
                if (counted++ < 3) three |= 1u << o.index;
                else guardOnly |= 1u << o.index;
            } else if (o.role != "gigaboss") {
                others |= 1u << o.index;  // the rest of the branch (the zone before the lair reachable)
            }
        for (const raid::EffectInfo& e : b->effects)
            if (e.bit >= 0 && e.bit < 32) others |= 1u << e.bit;  // the titan's links (passages opened from elsewhere)
        const std::string lock = "ambushes:" + std::to_string(before);
        CHECK(!b->lockOpen(lock, three));
        CHECK(!b->lockOpen(lock, guardOnly));  // the guard alone (the lock of before 2.78) no longer opens it
        CHECK(b->lockOpen(lock, three | guardOnly));
        CHECK(raid::Branch::ambushesLeftText(b->ambushes(before, three)) == "Embuscades restantes : 1 sur 4");
        bool said = false;
        for (const raid::Requirement& r : raid::zoneGoals(*b, before, three).items) said = said || r.text == "Embuscades restantes : 1 sur 4";
        CHECK(said);
        bool access = false;
        for (const raid::Requirement& r : raid::zoneGoals(*b, lair, others | three).items)
            access = access || (r.access && r.text.find("Embuscades restantes : 1 sur 4") != std::string::npos);
        CHECK(access);
        // The maps show the lair's armory, a zone of passage just before the lair.
        bool shown = false;
        for (const raid::ZoneGoals& g : raid::branchGoals(*b, others | three | guardOnly)) shown = shown || (g.zone == raid::kLairArmory && g.passage && g.reachable);
        CHECK(shown);
    }
}

TEST_CASE("raid zones (2.78): the lair's passage stays shut while one ambush wave is left; open, it leads to the lair's armory, then its plate to the lair") {
    Svc s;
    const raid::Branch& b = bras();
    uint32_t three = 0, last = 0, others = 0;
    int counted = 0;
    for (const raid::ObjectiveInfo& o : b.objectives) {
        if (o.zone == 7 && o.role == "ambush") (counted++ < 3 ? three : last) |= 1u << o.index;
        else if (o.role != "gigaboss") others |= 1u << o.index;
    }
    raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
    CHECK(run.begin(others | three, 7));
    stepN(run, 2, 30, others | three);
    const raid::Passage* toLair = passageTo(run, 7, 8);
    CHECK(toLair != nullptr);
    CHECK(!run.passageOpen(*toLair));
    auto* door = dynamic_cast<DoorEntity*>(run.worldOf(7)->entity(toLair->door));
    CHECK(door != nullptr);
    CHECK(!door->isOpen());
    into(run, 0, *toLair);
    stepN(run, 2, 2, others | three);
    CHECK(run.zoneOf(0) == 7);  // refused
    bool said = false;
    for (const WorldMessage& m : run.worldOf(7)->messages()) said = said || m.text.find("Embuscades restantes : 1 sur 4") != std::string::npos;
    CHECK(said);
    // The last ambush beaten (validated for the branch at this step): the door opens for everyone, the same step.
    stepN(run, 2, 1, others | three | last);
    CHECK(run.passageOpen(*toLair));
    CHECK(door->isOpen());
    run.worldOf(7)->playerAt(0).teleportTo({toLair->rect.x - 120.0f, toLair->rect.bottom() - 1.0f});
    stepN(run, 2, 2, others | three | last);
    into(run, 0, *toLair);
    stepN(run, 2, 2, others | three | last);
    CHECK(run.zoneOf(0) == raid::kLairArmory);
    CHECK(run.zoneOf(1) == 7);
    // Alone in the lair's armory: the plate wants the one player there; on it, it goes to the lair.
    const raid::ZoneRules& ar = *run.rulesOf(raid::kLairArmory);
    GameWorld& aw = *run.worldOf(raid::kLairArmory);
    aw.playerAt(0).teleportTo({ar.coops[0].rect.center().x, ar.coops[0].rect.bottom() - 1.0f});
    for (int t = 0; t < 20 && run.zoneOf(0) == raid::kLairArmory; ++t) stepN(run, 2, 1, others | three | last);
    CHECK(run.zoneOf(0) == 8);
    CHECK(run.zoneOf(1) == 7);
}

// Titan 9 (the ambush gates): « certaines zones … je puisse accéder à la partie suivante sans avoir fini les
// embuscades ». Only the zone before each lair waited for its ambushes: the entry's guard (zone 1 of every
// branch) and the abdomen's nest (zone 3) left their ways on open. Every zone of every branch is played here.
TEST_CASE("raid zones (titan 9): in every branch, every way on of a zone with an ambush stays shut until all its ambushes are beaten (started or not, whatever the way in), then opens") {
    Svc s;
    int gated = 0, exits = 0;
    for (const raid::Branch* b : raid::Branch::all()) {
        for (const raid::ZoneInfo& zi : b->zones) {
            const int zone = zi.id;
            const std::string gate = "ambushes:" + std::to_string(zone);
            // Everything of the branch but this zone's ambushes (and the gigaboss): the zone reachable, its other locks open.
            uint32_t others = 0;
            for (const raid::ObjectiveInfo& o : b->objectives)
                if (!(o.zone == zone && o.role == "ambush") && o.role != "gigaboss") others |= 1u << o.index;
            for (const raid::EffectInfo& e : b->effects)
                if (e.bit >= 0 && e.bit < 32) others |= 1u << e.bit;
            raid::RaidRun run(s.ws(), *b, raid::kMaxInstancePlayers);
            run.setQuiet(true);
            CHECK(run.begin(others, zone));
            stepN(run, 4, 30, others);
            GameWorld& w = *run.worldOf(zone);
            const raid::ZoneRules& rules = *run.rulesOf(zone);
            // Every ambush of the level (an arena without a boss) is an objective « ambush » of the zone: the gate counts it.
            for (CombatZoneEntity* a : w.ambushes()) {
                bool objective = false;
                for (const raid::ZoneObjective& zo : rules.objectives) {
                    const raid::ObjectiveInfo* o = b->objective(zo.id);
                    objective = objective || (zo.source == a->id() && o && o->role == "ambush" && o->zone == zone);
                }
                CHECK(objective);
            }
            CHECK(static_cast<int>(w.ambushes().size()) == b->ambushes(zone, 0).total);
            // The way back into a gate that is not a lair's stays open (nobody is ever shut in behind it).
            if (!b->ambushesOpenLair(zone))
                for (const raid::ZoneRules& zr : b->rules)
                    for (const raid::Passage& p : zr.passages)
                        if (p.to == zone && zr.zone > zone && zr.zone != raid::kLairArmory) CHECK(p.lock != gate);
            if (w.ambushes().empty()) continue;
            ++gated;
            std::vector<const raid::Passage*> ways;  // toward a zone further on: the next zones, the secret one, the lair
            for (const raid::Passage& p : rules.passages)
                if (p.to > zone) ways.push_back(&p);
                else CHECK(p.lock != gate);  // the ways back are never this gate's
            CHECK(!ways.empty());
            CHECK(ways.size() < 4u);
            for (const raid::Passage* p : ways) {
                CHECK(p->lock == gate);
                CHECK(!run.passageOpen(*p));
                if (!p->door.empty()) {
                    auto* door = dynamic_cast<DoorEntity*>(w.entity(p->door));
                    CHECK(door != nullptr);
                    CHECK(door != nullptr && !door->isOpen());
                }
            }
            // Not started, then started and not finished (a player in it, another one at the passage): nobody goes on,
            // whatever way they came in by (put right into the passage: over its door, past the arena, dashing…).
            const Vec2 start = w.playerAt(0).feet();
            auto tryWays = [&](int seat) {
                for (const raid::Passage* p : ways) {
                    w.playerAt(seat).teleportTo(start);
                    stepN(run, 4, 2, others);
                    into(run, seat, *p);
                    stepN(run, 4, 2, others);
                    CHECK(run.zoneOf(seat) == zone);
                    bool said = false;
                    for (const WorldMessage& m : w.messages()) said = said || m.text.find("Embuscades restantes") != std::string::npos;
                    CHECK(said);
                    ++exits;
                }
                w.playerAt(seat).teleportTo(start);
            };
            tryWays(0);
            CombatZoneEntity* first = w.ambushes().front();
            first->handleAction(w, "start", Json::object());
            stepN(run, 4, 60, others);
            CHECK(first->fighting());
            tryWays(1);
            // Every wave of every ambush beaten by the group, one after the other: shut until the very last enemy is down.
            bool shutWhileLeft = true;
            for (CombatZoneEntity* a : w.ambushes()) {
                if (!a->started()) a->handleAction(w, "start", Json::object());
                for (int t = 0; t < 6000 && !a->cleared(); ++t) {
                    strikeWave(w, *a);
                    stepN(run, 4, 1, others);
                    bool left = false;
                    for (CombatZoneEntity* x : w.ambushes()) left = left || !x->cleared();
                    if (left)
                        for (const raid::Passage* p : ways) shutWhileLeft = shutWhileLeft && !run.passageOpen(*p);
                }
                CHECK(a->cleared());
            }
            CHECK(shutWhileLeft);
            stepN(run, 4, 2, others);
            CHECK(b->ambushes(zone, run.mask()).left == 0);
            // Then every way on opens (its door with it) and takes its player there.
            for (size_t k = 0; k < ways.size(); ++k) {
                const raid::Passage& p = *ways[k];
                CHECK(run.passageOpen(p));
                if (!p.door.empty()) {
                    auto* door = dynamic_cast<DoorEntity*>(w.entity(p.door));
                    CHECK(door != nullptr && door->isOpen());
                }
            }
            // Reachable on foot from the start once open, where the zone has no plate to hold together (the entry, the nest).
            if (rules.coops.empty()) {
                std::shared_ptr<const raid::NavGraph> nav = raid::NavGraph::of(w, rules.magma.present ? rules.magma.zone : std::string());
                const int from = nav->locate(start);
                CHECK(from >= 0);
                for (const raid::Passage* p : ways) {
                    const int to = nav->nearest({p->rect.center().x, p->rect.bottom()}, 48.0f);
                    std::vector<int> path;
                    CHECK(to >= 0);
                    CHECK(from >= 0 && to >= 0 && nav->path(from, to, path));
                }
            }
            for (size_t k = 0; k < ways.size(); ++k) {
                const int seat = static_cast<int>(k);
                w.playerAt(seat).teleportTo(start);
                stepN(run, 4, 2, others);
                into(run, seat, *ways[k]);
                stepN(run, 4, 2, others);
                CHECK(run.zoneOf(seat) == ways[k]->arrival());
            }
        }
    }
    // The seven branches' entries, the abdomen's nest, the seven zones before the lairs, the Cœur's aorta; each way on
    // tried twice (the ambush not started, then started): 7 × 2 × 2 + 2 × 2 + 7 × 2 + 2.
    CHECK(gated == 16);
    CHECK(exits == 48);
}

TEST_CASE("raid zones (titan 9): an ambush another group beat is beaten in the zone loaded here too, the way on opens at that step; the way back stays open") {
    Svc s;
    const raid::Branch& b = bras();
    uint32_t guard = 0;
    for (const raid::ObjectiveInfo& o : b.objectives)
        if (o.zone == 1 && o.role == "ambush") guard |= 1u << o.index;
    CHECK(guard != 0);
    raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    stepN(run, 2, 30);
    GameWorld& w = *run.worldOf(1);
    CHECK(w.ambushes().size() == 1u);
    CombatZoneEntity* a = w.ambushes().front();
    a->handleAction(w, "start", Json::object());
    stepN(run, 2, 60);
    CHECK(a->fighting());
    const raid::Passage* way = passageTo(run, 1, 2);
    CHECK(way != nullptr);
    CHECK(!run.passageOpen(*way));
    // « Ce qu'il reste à faire » says what is left, the closed passage too.
    bool said = false;
    for (const raid::Requirement& r : raid::zoneGoals(b, 1, 0).items) said = said || r.text == "Embuscades restantes : 1 sur 1";
    CHECK(said);
    // The server says another group beat it (RAID_STATE, this step): it is beaten here too, at that step.
    stepN(run, 2, 1, guard);
    CHECK(a->cleared());
    int alive = 0;
    for (const auto& e : w.entities())
        if (auto* es = dynamic_cast<EnemySpawnEntity*>(e.get()); es && a->lists(es->id()))
            if (Enemy* en = es->enemy(w); en && en->alive()) ++alive;
    CHECK(alive == 0);
    CHECK(run.passageOpen(*way));
    auto* door = dynamic_cast<DoorEntity*>(w.entity(way->door));
    CHECK(door != nullptr && door->isOpen());
    bool open = false;
    for (const raid::Requirement& r : raid::zoneGoals(b, 1, guard).items)
        open = open || (r.done && r.text == "Toutes les embuscades sont vaincues : les passages vers la suite sont ouverts");
    CHECK(open);
    CHECK(b.lockText("ambushes:1").message == "L'embuscade est vaincue : les passages vers la suite s'ouvrent");
    into(run, 0, *way);
    stepN(run, 2, 2, guard);
    CHECK(run.zoneOf(0) == 2);
    // The way back is never shut.
    const raid::Passage* back = passageTo(run, 2, 1);
    CHECK(back != nullptr);
    CHECK(back->lock.empty());
    stepN(run, 2, 60, guard);
    into(run, 0, *back);
    stepN(run, 2, 2, guard);
    CHECK(run.zoneOf(0) == 1);
}

TEST_CASE("raid zones (2.78): each player's deaths, the revivals they did and received, counted exactly in the zone's table") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    stepN(run, 3, 120);
    GameWorld& w = *run.worldOf(1);
    // Seat 1 down twice (revived by 0, then by 2), seat 0 once (revived by 1): only the one holding
    // « Interagir » by the soul revives (the others stand near, their hands free).
    killSeat(w, 1);
    stepN(run, 3, 200);
    revive(run, 3, 0, 1);
    stepN(run, 3, 60);
    killSeat(w, 1);
    stepN(run, 3, 200);
    revive(run, 3, 2, 1);
    stepN(run, 3, 60);
    killSeat(w, 0);
    stepN(run, 3, 200);
    revive(run, 3, 1, 0);
    stepN(run, 3, 30);
    const auto rows = run.contributions().zoneTable(1);
    const raid::ZoneContribution* a = line(rows, 0);
    const raid::ZoneContribution* b = line(rows, 1);
    const raid::ZoneContribution* c = line(rows, 2);
    CHECK(a && b && c);
    CHECK(a->deaths == 1);
    CHECK(a->revives == 1);
    CHECK(a->revived == 1);
    CHECK(b->deaths == 2);
    CHECK(b->revives == 1);
    CHECK(b->revived == 2);
    CHECK(c->deaths == 0);
    CHECK(c->revives == 1);
    CHECK(c->revived == 0);
    // In the canonical text the server rebuilds (« …,mini,deaths,revives,revived; »), and in the whole raid.
    const std::string canon = raid::Contributions::zoneCanonical(rows);
    CHECK(canon.find(",0,2,1,2;") != std::string::npos);
    const auto raidRows = run.contributions().raidTable();
    CHECK(line(raidRows, 1) && line(raidRows, 1)->deaths == 2 && line(raidRows, 1)->revived == 2);
    // A table of before 2.78 (v2 false): the four numbers are not in its text.
    std::vector<raid::ZoneContribution> old = rows;
    for (raid::ZoneContribution& z : old) z.v2 = false;
    CHECK(raid::Contributions::zoneCanonical(old).find(",0,2,1,2;") == std::string::npos);
}

TEST_CASE("raid zones (2.78): a zone whose players are all down goes on while others play elsewhere (no wipe); every player down, whatever the zone, wipes the group") {
    Svc s;
    raid::RaidRun run(s.ws(), bras(), raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    stepN(run, 2, 120);
    beatAmbushes(run, 2, 1);  // titan 9: the Épaule's ways on wait for its guard
    into(run, 1, *passageTo(run, 1, 3));
    stepN(run, 2, 2);
    CHECK(run.zoneOf(1) == 3);
    killSeat(*run.worldOf(3), 1);
    stepN(run, 2, 300);
    // Its soul waits in the Veine; the group plays on in the Épaule: no wipe, the Veine is still simulated.
    CHECK(!run.wiped());
    CHECK(run.worldOf(3) != nullptr);
    CHECK(run.worldOf(3)->member(1).presence == GameWorld::Presence::Down);
    CHECK(run.worldOf(3)->state() != WorldState::Failed);
    // An ally comes from the Épaule and revives it there (only a player of the same zone can).
    into(run, 0, *passageTo(run, 1, 3));
    stepN(run, 2, 2);
    CHECK(run.zoneOf(0) == 3);
    revive(run, 2, 0, 1);
    const auto rows = run.contributions().zoneTable(3);
    CHECK(line(rows, 1) && line(rows, 1)->deaths == 1 && line(rows, 1)->revived == 1);
    CHECK(line(rows, 0) && line(rows, 0)->revives == 1);
    // Apart again, both down at once in two zones: the group is wiped, out of both zones.
    stepN(run, 2, 240);
    into(run, 1, *passageTo(run, 3, 1));
    stepN(run, 2, 2);
    CHECK(run.zoneOf(1) == 1);
    run.takeReports();
    killSeat(*run.worldOf(1), 1);
    killSeat(*run.worldOf(3), 0);
    bool wipe = false;
    for (int t = 0; t < 400 && !run.wiped(); ++t) {
        stepN(run, 2, 1);
        for (const raid::Report& r : run.takeReports())
            if (r.kind == raid::Report::Kind::Wipe) {
                wipe = true;
                CHECK(r.seats.size() == static_cast<size_t>(raid::kMaxInstancePlayers));
            }
    }
    CHECK(run.wiped());
    CHECK(wipe);
    CHECK(run.wipedZones() == (std::vector<int>{1, 3}));
}

TEST_CASE("raid zones (2.78): the simulated players follow the same rule (each one its own zone); the test session thinks zone by zone") {
    Svc s;
    raid::RaidRun::Test t;
    t.on = true;
    t.players = 3;
    raid::TestSession a(s.ws(), bras(), t), b(s.ws(), bras(), t);
    a.setSkill(raid::BotSkill::Expert);
    b.setSkill(raid::BotSkill::Expert);
    CHECK(a.begin(1));
    CHECK(b.begin(1));
    // The administrator alone into the Veine: the simulated players stay in the Épaule, playing there.
    a.step(InputFrame{});
    b.step(InputFrame{});
    for (raid::TestSession* x : {&a, &b}) {
        const raid::Passage* p = passageTo(x->run(), 1, 3);
        x->run().worldOf(1)->playerAt(0).teleportTo({p->rect.center().x, p->rect.bottom() - 1.0f});
    }
    for (int k = 0; k < 600; ++k) {
        a.step(InputFrame{});
        b.step(InputFrame{});
    }
    CHECK(a.run().zoneOf(0) == 3);
    CHECK(a.hash() == b.hash());
}

TEST_CASE("raid zones (2.78): the distance to the lair's passage along the expedition's route, in metres (one tile)") {
    const raid::Branch* crane = raid::Branch::builtin("crane");
    CHECK(crane != nullptr);
    const raid::ZoneRules& z7 = *crane->zoneRules(7);
    CHECK(z7.route.size() >= 10);
    const raid::Passage* to = nullptr;
    for (const raid::Passage& p : z7.passages)
        if (p.via == raid::kLairArmory) to = &p;
    CHECK(to != nullptr);
    const Vec2 start = z7.route.front();
    const raid::RouteLeft a = raid::routeLeft(z7.route, start, to->rect.center());
    const raid::RouteLeft b = raid::routeLeft(z7.route, start + Vec2{800.0f, 0.0f}, to->rect.center());
    CHECK(a.along);
    CHECK(a.metres() > 900);  // the whole expedition: more than 900 tiles
    CHECK(b.metres() < a.metres());
    CHECK(a.metres() - b.metres() >= 45);  // 800 px further along: about 50 m nearer
    CHECK(a.dir.x > 0.5f);  // eastward at the start
    const raid::RouteLeft end = raid::routeLeft(z7.route, z7.route.back(), to->rect.center());
    CHECK(end.metres() <= 1);
    // Without a route: horizontally.
    const raid::RouteLeft h = raid::routeLeft({}, {0.0f, 0.0f}, {320.0f, -500.0f});
    CHECK(!h.along);
    CHECK(h.metres() == 20);
}

TEST_CASE("raid zones (2.78): the « Tableau des contributions » and the distance indicator measured into their places, nothing over anything, French and English") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    Application app;
    AppConfig cfg;
    cfg.headless = true;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.vsync = false;
    CHECK(app.init(cfg));
    {
        GameServices g(app);
        g.loadAll();
        app.pushScene(std::make_unique<Blank>());
        contrib::Model m;
        m.legend = "Chiffres comptés par la simulation (les mêmes sur tous les jeux du groupe), validés par la majorité.";
        m.tooltipNote = "Compté par la simulation, validé par la majorité du groupe.";
        m.tabKey = "Tab";
        contrib::Table t;
        t.title = "Zone 7 · Voûte crânienne";
        t.sub = "terminée par votre groupe · 12:34";
        const char* names[8] = {"Vous", "Kaëlis-de-la-Nébuleuse-Infinie", "Joueur simulé 3", "Orion", "Joueur simulé 5", "Lyra", "Joueur simulé 7", "Vega"};
        for (int i = 0; i < 8; ++i) {
            contrib::Row r;
            r.name = names[i];
            r.me = i == 0;
            r.bossTenths = 3100 - 300 * i;
            r.miniTenths = 400 + 50 * i;
            r.damageTenths = 9000 - 700 * i;
            r.support = 120 * (i % 3);
            r.mechanisms = i;
            r.objectives = 3 - i % 4;
            r.deaths = i % 3;
            r.revives = (7 - i) % 4;
            r.revived = i % 2;
            r.v2 = i != 7;  // the last one: a table of before 2.78 (« — »)
            t.rows.push_back(r);
        }
        m.zones = {t, t};
        m.raid = t;
        m.raid.title = "Raid entier";
        for (Language lang : {Language::French, Language::English}) {
            i18n::setLanguage(lang);
            for (const Vec2 size : {Vec2{1280, 720}, Vec2{1920, 1080}}) {
                app.setWindowSize(static_cast<int>(size.x), static_cast<int>(size.y));
                app.renderer().refreshOutputSize();
                app.runFrames(1);
                app.renderer().refreshOutputSize();
                UIContext& ui = g.ui;
                ui.begin(app, 1.0f / 60.0f);
                const float W = ui.screenW(), H = ui.screenH();
                const Rect panel{ui.s(24), ui.s(24), W - ui.s(48), H - ui.s(48)};
                for (int tab : {0, 1})
                    for (int sort = 0; sort < contrib::kCols; ++sort) {
                        contrib::View v;
                        v.tab = tab;
                        v.sort = static_cast<contrib::Col>(sort);
                        const contrib::Layout L = contrib::layout(ui, panel, m, v);
                        // Every part inside the panel, one after the other.
                        for (const Rect& r : L.texts) CHECK(panel.containsRect(r));
                        CHECK(panel.containsRect(L.best));
                        CHECK(panel.containsRect(L.keys));
                        CHECK(L.rows.size() == 8);  // eight players fit at 1280 × 720
                        CHECK(L.hidden == 0);
                        CHECK(!L.header.intersects(L.tabs[0]));
                        CHECK(!L.header.intersects(L.sortLine));
                        CHECK(!L.best.intersects(L.keys));
                        for (size_t i = 0; i < L.rows.size(); ++i) {
                            CHECK(!L.rows[i].intersects(L.header));
                            CHECK(!L.rows[i].intersects(L.best));
                            if (i + 1 < L.rows.size()) CHECK(!L.rows[i].intersects(L.rows[i + 1]));
                        }
                        CHECK(!L.tabs[0].intersects(L.tabs[1]));
                        CHECK(L.tabs[1].right() <= L.sortLine.x + 0.5f);
                        if (!L.prevZone.empty()) CHECK(L.nextZone.right() <= L.sortLine.x + 0.5f);
                        // The columns side by side, each header (its icon, its name at the font chosen, ▼) inside its column.
                        const contrib::Prepared p = contrib::prepare(tab == 1 ? m.raid : m.zones[0], v.sort);
                        for (int c = 0; c < contrib::kCols; ++c) {
                            const Rect& cr = L.cols[static_cast<size_t>(c)];
                            CHECK(cr.right() <= L.header.right() + 0.5f);
                            if (c + 1 < contrib::kCols) CHECK(cr.right() <= L.cols[static_cast<size_t>(c + 1)].x + 0.5f);
                            const float iconW = c == 0 ? 0.0f : ui.s(28);
                            const float tw = ui.text().measure(p.headers[static_cast<size_t>(c)], FontFace::Bold, ui.s(L.headFont)).x;
                            if (iconW + tw + ui.s(16) > cr.w + 0.5f)
                                std::printf("  en-tête « %s » serré (%.0f > %.0f) : abrégé avec « … »\n", p.headers[static_cast<size_t>(c)].c_str(),
                                            iconW + tw + ui.s(16), cr.w);
                        }
                        CHECK(L.headFont >= 17.0f * 0.62f - 0.01f);
                        // Each number's tooltip inside the screen, whatever the cell.
                        for (size_t i = 0; i < L.rows.size(); ++i)
                            for (int c = 0; c < contrib::kCols; ++c) {
                                const Rect& cr = L.cols[static_cast<size_t>(c)];
                                const contrib::Tooltip tt = contrib::tooltip(ui, m, v, p, p.order[i], static_cast<contrib::Col>(c), {cr.x, L.rows[i].y, cr.w, L.rows[i].h});
                                CHECK(tt.box.x >= 0.0f && tt.box.y >= 0.0f && tt.box.right() <= W + 0.5f && tt.box.bottom() <= H + 0.5f);
                                CHECK(!tt.lines.empty());
                            }
                        // The old table's line reads « — » where its numbers are unknown, sorted last.
                        for (size_t i = 0; i < p.cells.size(); ++i)
                            if (!(tab == 1 ? m.raid : m.zones[0]).rows[i].v2) {
                                CHECK(p.cells[i][static_cast<size_t>(contrib::Col::Deaths)].text == "—");
                                CHECK(p.cells[i][static_cast<size_t>(contrib::Col::Mini)].text == "—");
                            }
                        contrib::draw(ui, panel, m, v);
                    }
                // The best of a category wears the crown; the bars are proportional to the best.
                const contrib::Prepared p = contrib::prepare(t, contrib::Col::Boss);
                CHECK(p.cells[0][static_cast<size_t>(contrib::Col::Boss)].best);
                CHECK(std::abs(p.cells[0][static_cast<size_t>(contrib::Col::Boss)].bar - 1.0f) < 0.001f);
                CHECK(p.order[0] == 0);
                // The distance indicator: its default place inside the screen and over no other raid element.
                const RaidHudLayout l;
                const raidhud::HudAreas hud{};
                const Rect lp = raidhud::area(ui, l, RaidHudElement::LairPassage, hud);
                CHECK(lp.x >= 0.0f && lp.y >= 0.0f && lp.right() <= W + 0.5f && lp.bottom() <= H + 0.5f);
                for (int i = 0; i < kRaidHudElements; ++i) {
                    const auto e = static_cast<RaidHudElement>(i);
                    if (e == RaidHudElement::LairPassage || RaidHudLayout::hudElement(e) != HudElement::Count) continue;
                    const Rect o = raidhud::area(ui, l, e, hud);
                    if (o.w <= 0.0f) continue;
                    if (lp.intersects(o)) std::printf("  « Distance au passage » touche « %s » en %.0f x %.0f\n", RaidHudLayout::label(e), size.x, size.y);
                    CHECK(!lp.intersects(o));
                }
                const raidhud::LairPassageLayout pl = raidhud::layoutLairPassage(ui, lp, 1.0f);
                CHECK(lp.containsRect(pl.arrow));
                CHECK(lp.containsRect(pl.title));
                CHECK(lp.containsRect(pl.state));
                CHECK(!pl.title.intersects(pl.state));
                CHECK(!pl.arrow.intersects(pl.title));
                raidhud::drawLairPassage(ui, lp, 1.0f, {"Passage vers l'antre : 124 m", "fermé — Embuscades restantes : 2 sur 4", false, {1.0f, 0.0f}});
                ui.end();
            }
        }
        i18n::setLanguage(Language::French);
    }
    app.shutdown();
}

TEST_CASE("raid zones (2.78): the icons of the table are the generator's (same bytes)") {
    if (std::system("python3 -c \"import numpy, PIL\" > " COSMIC_NULL_DEVICE " 2>&1") != 0) {
        std::printf("  (python3 + numpy + Pillow absents : vérification des icônes du tableau sautée)\n");
        return;
    }
    CHECK(std::system("python3 tools/assetgen/environment/raid_contrib_icons.py --check > " COSMIC_NULL_DEVICE " 2>&1") == 0);
}

TEST_CASE("raid zones (2.78): the cost of several zones at once (8 players in 4 zones), measured") {
    Svc s;
    const raid::Branch& b = bras();
    raid::RaidRun run(s.ws(), b, raid::kMaxInstancePlayers);
    CHECK(run.begin(0, 1));
    stepN(run, 8, 60);
    beatAmbushes(run, 8, 1);  // titan 9: the Épaule's ways on wait for its guard
    // Two by two: the Épaule (0, 1), the Biceps (2, 3), the Veine (4, 5), the Coude-forge by the Biceps (6, 7).
    for (int seat : {2, 3, 6, 7}) into(run, seat, *passageTo(run, 1, 2));
    for (int seat : {4, 5}) into(run, seat, *passageTo(run, 1, 3));
    stepN(run, 8, 2);
    stepN(run, 8, 60);
    for (int seat : {6, 7}) into(run, seat, *passageTo(run, 2, 4));
    stepN(run, 8, 2);
    CHECK(run.zonesLoaded() == (std::vector<int>{1, 2, 3, 4}));
    std::vector<InputFrame> held(raid::kMaxInstancePlayers);
    for (InputFrame& f : held) f = right();
    double total = 0.0, worst = 0.0;
    const int steps = 1200;
    for (int t = 0; t < steps; ++t) {
        auto f = seats(8, held);
        const auto t0 = std::chrono::steady_clock::now();
        run.step(f.data(), static_cast<int>(f.size()), 0);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total += ms;
        worst = std::max(worst, ms);
    }
    const double avg = total / steps;
    // The same 8 players in one zone, for comparison.
    raid::RaidRun one(s.ws(), b, raid::kMaxInstancePlayers);
    CHECK(one.begin(0, 1));
    double oneTotal = 0.0;
    for (int t = 0; t < steps; ++t) {
        auto f = seats(8, held);
        const auto t0 = std::chrono::steady_clock::now();
        one.step(f.data(), static_cast<int>(f.size()), 0);
        oneTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    // Loading the largest zone (the Poignet-verrou: what a player entering it alone costs, once).
    const auto l0 = std::chrono::steady_clock::now();
    raid::RaidRun big(s.ws(), b, raid::kMaxInstancePlayers);
    CHECK(big.begin(0, 7));
    const double load = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l0).count();
    std::printf("  4 zones simulées à la fois (8 joueurs) : %.3f ms par pas en moyenne, %.2f ms au pire (%d pas) ; une seule zone : %.3f ms ; "
                "charger le Poignet-verrou : %.1f ms\n",
                avg, worst, steps, oneTotal / steps, load);
    CHECK(avg < 8.33 * 0.5);  // half a step's budget at 120 Hz (the other half for the display)
}

TEST_CASE("2.78 raids: a player down with no ally in their zone, allies elsewhere: the « À TERRE » banner says one can come back") {
    downed::DownInfo d;
    d.others = false;     // nobody in play in this zone
    d.canRevive = false;  // so nobody revives here now
    downed::Banner b = downed::downBanner(d);
    CHECK(!b.lines.empty());
    if (b.lines.empty()) return;
    CHECK(b.lines[0] == "Aucun allié en jeu pour vous relever pour l'instant");
    d.elsewhere = 2;  // two allies in play in other zones of the branch
    b = downed::downBanner(d);
    CHECK(!b.lines.empty());
    if (b.lines.empty()) return;
    CHECK(b.lines[0] == "Aucun allié dans cette zone : un allié d'une autre zone peut revenir vous relever");
    d.canRevive = true;  // one came back
    d.others = true;
    b = downed::downBanner(d);
    CHECK(b.lines[0] == "Un allié doit maintenir Interagir 2 s près de votre âme");
}
