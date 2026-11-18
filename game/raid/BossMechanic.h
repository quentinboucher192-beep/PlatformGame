#pragma once

// 2.69 (phase 4B): the rules of one gigaboss's own fight (docs/RAIDS.md §13). raid::Gigaboss runs
// what every gigaboss shares — its waking, its health for the seats taken, its pressure, its phases,
// an administrator's test, its fall and the players « present at the fall » — and hands each step
// of the fight to the boss's own rules, chosen by metadata.raid.gigaboss.kind:
//
// * "skullmaw"     game/raid/SkullmawFight     (CRÂNE: gazes on every player, flash, mirrors, darkness)
// * "dreadfang"    game/raid/DreadfangFight    (BRAS GAUCHE: storey sweeps, ground claws, pins, chains)
// * "heartbreaker" game/raid/HeartbreakerFight (TORSE: four plates, heartbeats, rib traps, ribs pried open)
//
// Pyroclast (no kind) keeps the vents and the fists of Gigaboss.cpp. Like the rest of the raid, the
// rules read only the world and the frames (never the clock, the camera or the network): every
// computer of the instance runs them the same, step by step; their state is in the run's digest.
//
// The pieces several fights share are here: marks under every player in play (a blow lands on each
// a moment later), places several players must hold (mirrors, anchors, ribs), weak points stuck to
// the gigaboss while they are bare.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"
#include "game/raid/RaidBranch.h"

namespace cosmic {
class GameWorld;
class Enemy;
}  // namespace cosmic

namespace cosmic::raid {

class Contributions;
class Gigaboss;

/// A line of the objectives panel of the combat screen (French, display only).
struct BossLine {
    std::string mark, text;
    Color color{255, 176, 74, 255};
};

class BossMechanic {
public:
    virtual ~BossMechanic() = default;
    /// One step of the fight, after the world's step (its phase is Gigaboss::phase()).
    virtual void step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) = 0;
    /// It fell: what its rules left in the world goes (weak points, claws).
    virtual void stop(GameWorld& w, Contributions& c) = 0;
    /// Nothing hurts it now (its veil, its armour, its cage); the damage it takes is multiplied.
    virtual bool guarded() const = 0;
    virtual float damageMult() const = 0;
    virtual uint64_t hash() const = 0;

    // ------------------------------------------------------------ display (French)
    /// The alert of its fall, the alert and the objectives line when another fight already took it down.
    virtual std::string fallText(const std::string& name) const = 0;
    virtual std::string goneText(const std::string& name) const = 0;
    virtual std::string goneLine(const std::string& name) const = 0;
    /// The sentence of the card of its fall (« Résultats du raid »).
    virtual std::string fallCard() const = 0;
    /// Its lines of the objectives panel during the fight.
    virtual void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const = 0;
    /// Where it threatens the players now (the arena camera keeps them in view).
    virtual void threats(const GameWorld& w, std::vector<Vec2>& out) const = 0;
    /// The enemies the arrows point at when they are off screen (its weak points while bare).
    virtual void targets(std::vector<uint32_t>& out) const = 0;
};

/// The rules of `rules.kind` (nullptr: none, Pyroclast's own code).
std::unique_ptr<BossMechanic> makeBossMechanic(const GigabossRules& rules);

// ------------------------------------------------------------------------ shared pieces
namespace mech {

int steps(float seconds);
uint64_t mix(uint64_t h, uint64_t v);
/// Seat i is in play this step (on their feet in the world: neither down, absent nor vacant).
bool inPlay(const GameWorld& w, int i);
int countInPlay(const GameWorld& w);
/// A blow of the gigaboss on the player of `seat` alone (a beam, a flash, a claw): true when it hurt.
bool hurtSeat(GameWorld& w, Enemy& b, int seat, float damage, Vec2 source, float knockback, const std::string& name);
/// The numbers of a JSON array (a fallback when absent).
std::vector<float> floats(const Json& j, std::vector<float> fallback = {});
/// The value of a per-phase list (the last one beyond it).
float byPhase(const std::vector<float>& v, int phase, float fallback);

/// Marks under every player in play, the same step (8 players: 8 marks); `warn` seconds later
/// the blow lands on each mark (`area`: a column `width` wide, `height` high, from the mark's
/// feet): whoever is still there is hit. The marks never follow anyone. A player who left their
/// own mark in time « avoided » the blow (the raid's contributions).
struct Marks {
    struct Mark {
        Vec2 at;
        int left = 0;  // steps before it strikes (0 and below: striking)
        int seat = -1;
    };
    std::vector<float> periods;  // seconds between two volleys, by phase (÷ its pressure)
    float warn = 1.2f, width = 56.0f, height = 150.0f, damage = 1.0f, knockback = 280.0f;
    std::vector<Mark> marks;
    int clock = 0;

    void read(const Json& j);
    /// One step; `name` is the blow's name (statistics). True when a volley was marked this step.
    bool step(GameWorld& w, Enemy& b, int phase, Contributions& c, const std::string& name);
    Rect area(const Mark& m) const { return {m.at.x - width * 0.5f, m.at.y - height, width, height + 4.0f}; }
    int warnSteps() const { return std::max(1, steps(warn)); }
    void hashInto(uint64_t& h) const;
};

/// Places where players must stand (mirrors, anchors of chains, ribs): who stands on each.
struct Holds {
    struct Place {
        std::string id;
        Rect area;
        uint8_t holders = 0;   // seats on it this step
        int hold = 0;          // steps held toward its goal
        bool locked = false;   // held long enough (a chain locked)
        uint32_t lockedAt = 0; // fight step it was locked at
    };
    std::vector<Place> places;

    void read(const Json& arr);
    /// Who stands on each place now (players in play, on their feet).
    void sense(const GameWorld& w);
    /// Places with at least one player on them, and the players on any.
    int held() const;
    int holders() const;
    bool holds(int seat) const;
    void reset();
    void hashInto(uint64_t& h) const;
};

/// Weak points bare on the gigaboss for a while: each an enemy stuck to it (spot: from its feet, x
/// toward where it faces); each broken takes its share of the gigaboss's health and counts for the
/// player who broke it. Their health grows with the seats its own health was scaled for.
struct WeakPoints {
    struct Spot {
        Vec2 at;
        std::string enemy;
        float share = 0.04f;
    };
    std::vector<Spot> spots;
    float hpPerPlayer = 0.25f;
    bool mirror = true;          // the spots turn with it (x toward where it faces)
    std::vector<uint32_t> uids;  // by spot (0: broken, or none)
    int total = 0;

    /// metadata: "weakSpots" [[x, y, enemy]…], "weakShares" [share…], "weakHpPerPlayer".
    void read(const Json& g);
    /// The spots `which` (all when empty) bare now.
    void spawn(GameWorld& w, Enemy& b, int scaledFor, const std::vector<int>& which = {});
    /// After the world's step: the ones broken wound the gigaboss (and count), the others stay on it.
    /// Returns how many broke this step; `seats` gets who broke them (-1: unknown).
    int step(GameWorld& w, Enemy& b, Contributions& c, std::vector<int>* seats = nullptr);
    /// Those still standing go.
    void clear(GameWorld& w);
    int left() const;
    bool bare() const { return left() > 0; }
    void glue(GameWorld& w, const Enemy& b);
    void hashInto(uint64_t& h) const;
};

}  // namespace mech
}  // namespace cosmic::raid
