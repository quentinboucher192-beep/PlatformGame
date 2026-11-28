#pragma once

// 2.79 (docs/RAIDS.md §28): a tactic per Gigaboss for the simulated players of a raid test. Each one reads
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
    /// 2.87 (fight plan): its sweeping beams read as they really turn (Danger::angle, turn).
    bool sweepRead = false;
    /// 2.89 (fight plan): its titanic leap read as it goes (Pyroclast's meteor fist): when it lands (its shadow locked, its fall).
    bool skyfallRead = false;
    /// Graviton: the x ranges sheltered under a slab when gravity turns over in a sector.
    std::vector<Rect> shelters;
    /// Players a routine holds (swallowed, nailed): no use striking near them, a rescue to make.
    std::vector<int> held;
    /// 2.89: where no player can go (Voidworm's stomach, under the arena): a soul fallen there is nobody's task.
    std::vector<Rect> noRevive;
    std::string phase;  // what the group does now (« Voile psychique : 4 miroirs sur 4 »), for the thoughts
    /// 2.90 (the escape, Plan::escape): steps since the shadow of its leap stopped following its target (the players watch it
    /// lock: then it falls), -1 not locked. Given by the players' side (TestBots) before the tactic reads.
    int skyLocked = -1;
    int skyHang = -1;  // steps since it went out of sight over the vault (its hang), -1 not there
    /// 2.84: the fight plan of this boss at this step (game/raid/bots/FightPlan), what its tactic sets beyond what is
    /// coming: how the group stands and spends its shield.
    struct Plan {
        bool reserveShield = false;  // the shield kept for the blows marked shieldFirst (and a player at 4 hearts or less)
        float elevate = 0.0f;        // the strikers stand this much above floorY (its floor blows)
        float floorY = 0.0f;         // the floor its blows run along
        bool elevateAll = false;     // the lure too
        bool fixedHolders = false;   // each place its own player for the whole phase, revived first
        float reviveWindow = 0.0f;   // a liquid on a cycle: souls under elevate only this many seconds after it fell
        float keepOff = -1.0f;       // the strikers' distance to its body (< 0: the default)
        bool focusWeak = false;      // bare, all on one weak point at a time
        bool fireOnTheRun = false;   // fires while it gets out of a blow when a wave reaches
        bool frontOnly = false;      // bare, its strikers all in front of it (its weak points are, and it turns to them)
        bool noLure = false;         // nobody keeps close to it on purpose (its blows reach the nearest whoever it is)
        bool lureOnFloor = false;    // the lure stays on its floor (its blows aimed at the nearest go there)
        bool freeOnClosed = false;   // a player on a closed place dodges and jumps around it (else it stays on it)
        bool reviveShielded = false; // a soul in its body approached only with the shield ready
        bool hideArmoured = false;   // its body closed to blows: the strikers wait far off it (not where its weak points will be)
        bool jumpBeams = false;      // 2.86: a beam lying lower than steep (Skullmaw's gazes) jumped over as it fires, not stepped out of
        bool escape = false;         // 2.90: its blows left by the navigation alone, with their time (RaidBots::drive, nav::Pilot::escape)
        bool dashOut = false;        // 2.89: out of a blow too wide to walk out of in time, dashing (Pyroclast's meteor fist)
        float bodyBothSides = 0.0f;  // 2.89: > 0: its body read on both sides of its box (it turns), this much wider (Pyroclast)
        bool bodyInNav = false;      // 2.89: its body among what the navigation's moves keep out of (it floats over the ledges)
        bool holdersFire = false;    // 2.98: a player on its place strikes the creatures near it from there (Voidworm's parasites)
    } plan;
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
