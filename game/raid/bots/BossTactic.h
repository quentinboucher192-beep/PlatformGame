#pragma once

// 2.79 (docs/RAIDS.md §24): a tactic per Gigaboss for the simulated players of a raid test. Each one reads
// its boss's real routine (game/raid/*Routine*, the rules of raid::Gigaboss) as a player reads the screen —
// the marks over the arena, the vents, the enemies of its mechanism, its weak points, its blows announced —
// and tells the group, at each step:
//
// * what is coming and how to get out of it (Danger: leave an area, step out of a beam, jump a wave along a
//   floor, climb above a liquid, leave a sector whose gravity turns over, run from a well);
// * the places to hold together (Station: vents, mirrors, chains' anchors, generators, ribs, arteries, the
//   supernova's anchors), with how many players;
// * what to strike (Target), the weak points first when they are bare (× 2), the targets of its mechanism
//   finished together when its rules want it (Titanus's two restraints within 1,5 s);
// * whether its body takes damage now (not armoured), and its flash to hide from (Skullmaw's gaze);
// * the power each role wishes in the armory of its branch.
//
// A tactic never acts: the simulated players (raid::TestBots) answer with their frames, as players would.

#include <memory>
#include <string>
#include <vector>

#include "game/raid/bots/BotTypes.h"

namespace cosmic {
class GameWorld;
class Enemy;
}  // namespace cosmic

namespace cosmic::raid {
class RaidRun;
class Gigaboss;
}  // namespace cosmic::raid

namespace cosmic::raid::bots {

struct BossView {
    std::vector<Danger> dangers;
    std::vector<Station> stations;
    std::vector<Target> targets;
    bool bodyTakesDamage = true;  // its body is worth striking now
    bool exposed = false;         // its weak points bare
    // Skullmaw's « Regard de mort »: hide from `eye` (something solid between) unless on a station.
    bool flash = false;
    Vec2 eye;
    int flashIn = 0;
    /// Graviton: the x ranges sheltered under a slab when gravity turns over in a sector.
    std::vector<Rect> shelters;
    /// Players a routine holds (swallowed, nailed): no use striking near them, a rescue to make.
    std::vector<int> held;
    std::string phase;  // what the group does now (« Voile psychique : 4 miroirs sur 4 »), for the thoughts
};

class BossTactic {
public:
    virtual ~BossTactic() = default;
    /// The tactic of the gigaboss of `routine` (its rules' routine: "" Pyroclast, "skullmaw", "dreadfang",
    /// "titanus", "voidworm", "graviton", "heartbreaker", "coeur").
    static std::unique_ptr<BossTactic> make(const std::string& routine);
    virtual const char* name() const = 0;
    /// What the group must do and avoid at this step (the gigaboss is present and fighting).
    virtual void read(const RaidRun& run, BossView& v) const = 0;
    /// The power a player of role `r` takes in the armory of its branch ("" none: the default wish).
    virtual std::string wish(Role r) const;

    // ------------------------------------------------------------------ shared readings
    /// Its fists or marks under the players, its vents' jets, the world's hazards, the enemies' shots, its
    /// own blows announced (Enemy's attack wind-up), and its vents to hold while it is armoured; its weak
    /// points while bare.
    static void readCommon(const RaidRun& run, BossView& v, bool vents = true);
    /// The boss's own attack wound up or under way (a slam, a charge, a ring…): the area it covers.
    static void readBossAttack(const GameWorld& w, const Enemy& boss, BossView& v);
    /// The enemies' projectiles: where each one will be in the next half second (shots: the shield stops them).
    static void readShots(const GameWorld& w, BossView& v);
    /// Its creatures (summoned, not its mechanism): targets of low priority.
    static void readAdds(const GameWorld& w, const Enemy& boss, BossView& v, int priority = 1);
};

std::unique_ptr<BossTactic> makePyroclastTactic();
std::unique_ptr<BossTactic> makeSkullmawTactic();
std::unique_ptr<BossTactic> makeDreadfangTactic();
std::unique_ptr<BossTactic> makeTitanusTactic();
std::unique_ptr<BossTactic> makeVoidwormTactic();
std::unique_ptr<BossTactic> makeGravitonTactic();
std::unique_ptr<BossTactic> makeHeartbreakerTactic();
std::unique_ptr<BossTactic> makeHeartTactic();

}  // namespace cosmic::raid::bots
