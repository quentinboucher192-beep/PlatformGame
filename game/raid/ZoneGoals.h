#pragma once

// 2.75: what is left to do in the zones of a branch (docs/RAIDS.md §19), from the branch's state
// alone (the same bits as the game and the server): the HUD's « Ce qu'il reste à faire » panel, the
// large map (M) and the tests read it. Pure: no world, no clock, no network.
//
// A zone is FINISHED when every objective of it that is not optional is done (a zone with optional
// objectives only, the secret one: all of them). Its requirements, in order: what opens it when it is
// still locked (the generators, the lever, another branch, an objective), then each objective (a
// pair: « k sur 2 », the generators: « k générateurs sur N »), the optional ones last. Each one is
// ticked from the state VALIDATED by the server, or « en attente de validation » when only this
// instance did it so far.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic::raid {

class Branch;

struct Requirement {
    std::string text;      // « Allumer le générateur ouest (un pouvoir de tir) »
    std::string progress;  // « 1 / 2 », "" when none
    bool done = false;     // validated by the server (or done by this instance: `pending`)
    bool pending = false;  // done by this instance, its validation still to come
    bool optional = false;
    bool access = false;   // what opens the zone (it is still locked)
};

struct ZoneGoals {
    int zone = 0;
    std::string name;
    bool finished = false;
    bool reachable = false;
    bool optional = false;  // nothing in it is required (the secret zone)
    bool lair = false;      // the gigaboss's zone (its fight has its own card)
    bool passage = false;   // 2.78: the lair's armory (« zone de passage »: nothing to finish there)
    std::vector<Requirement> items;
    /// What it still misses on one line (« 1 générateur sur 2 · Vaincre le golem »), "" when finished.
    std::string missing() const;
};

/// 2.78: what is left from `feet` to the end of `route` (the way through the zone before a lair, pixels):
/// along it from the nearest point of it (ties: the first segment), plus the way to that point; `dir` is
/// where to go now (toward the next point of the route). Without a route (fewer than two points): the
/// horizontal distance to `target`. One tile (16 px) is one metre (« 124 m »).
struct RouteLeft {
    float pixels = 0.0f;
    Vec2 dir{1.0f, 0.0f};
    bool along = false;  // measured along the route (else horizontally)
    int metres() const;
};
RouteLeft routeLeft(const std::vector<Vec2>& route, Vec2 feet, Vec2 target);

/// Whether `zone` is finished in `mask` (see above; a zone without any objective: never).
bool zoneFinished(const Branch& b, int zone, uint32_t mask);
/// The requirements of `zone`: `validated` is the server's state (RAID_STATE), `own` what this
/// instance did itself (RaidRun::localDone).
ZoneGoals zoneGoals(const Branch& b, int zone, uint32_t validated, uint32_t own = 0);
/// Every zone of the branch in order (the lair last), each with its requirements; 2.78: the lair's armory
/// (a zone of passage, `passage`) just before the lair when the branch has one.
std::vector<ZoneGoals> branchGoals(const Branch& b, uint32_t validated, uint32_t own = 0);

}  // namespace cosmic::raid
