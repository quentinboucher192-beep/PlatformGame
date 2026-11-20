#pragma once

// 2.77: the simulated players of a raid test (docs/RAIDS.md §21) play the zone themselves, with or
// without the administrator: an AI with objectives, deterministic, that answers each step with an
// InputFrame per seat it plays, as a player's game would. It reads the world and the raid's rules
// only (never the clock, the screen or the network):
//
// * what is left to do in the zone (the same data as « Ce qu'il reste à faire »: the objectives of
//   the branch not done yet — generators, levers, arenas, guardians, relics —, the plates to hold
//   together, the ambushes under way, the gigaboss), then the way out toward the next zone of the
//   branch still to finish (the lair last); the tasks shared out each quarter of a second, the
//   nearest players first: a few per generator, the right number on each plate, the others on the
//   fight;
// * it moves on the zone's graph (raid::NavGraph: jumps, double jumps, drops, ladders, moving
//   platforms, the lasting dangers avoided), A* to its goal, a new path when the world changes;
// * it fights (melee, the wave from afar when it has energy to spare), keeps out of the blows
//   announced and flees the rising liquid to a place above its level; a gigaboss: its plates or vents
//   when it is armoured, its weak points when it is bare, its body otherwise — the states of the
//   sheets' « Où frapper »; the targets of its mechanism (restraints…) struck together;
// * it revives the players down (the administrator too), takes its powers in the armory (its wish,
//   through InputFrame::choice) and waits at the exit for the administrator when they are there and
//   playing (AFK: it goes on alone);
// * its level (Débutant, Confirmé, Expert) is its reaction time and its precision, nothing else.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid {

/// « Niveau des joueurs simulés ».
enum class BotSkill : uint8_t { Beginner = 0, Skilled = 1, Expert = 2 };
const char* botSkillName(BotSkill s);

class TestBots {
public:
    enum class Task : uint8_t { Follow, Revive, Mechanism, Dodge, Fight, Objective, Flee, Exit, Tablet, Wait };
    struct Bot {
        uint32_t prevHeld = 0;
        int jumpHold = 0;     // steps the jump stays held
        bool airJump = false; // its air jump used in this flight
        float lastX = 0.0f;
        int still = 0;        // steps it wanted to move and did not
        int attackCool = 0;
        Task task = Task::Follow;
        Vec2 goal;
        Rect mechanism;       // the plate or vent it was sent to (Task::Mechanism)
        int mechanismSteps = 0;  // steps since it was sent there without standing on it
        // 2.77
        uint32_t target = 0;  // the enemy it strikes (uid), 0 none
        int act = 0;          // what it does at its goal (Act)
        std::string thing;    // the object of its goal (a generator, a lever, a tablet)
        int goalNode = -1;    // its goal on the graph
        std::vector<int> path;
        size_t at = 0;        // the node of the path it goes to
        int planned = -100000;  // the step its path was made
        float best = 1e30f;   // its nearest distance to the node it goes to
        int noProgress = 0;   // steps without coming nearer to it
        float goalBest = 1e30f;
        int lost = 0;         // steps without coming nearer to its goal (the rescue of a test)
        int react = 0;        // steps before it reacts to a danger seen (its level)
        bool sync = false;    // strikes only together with the others on the same mechanism
        int fleeNode = -1;    // where it shelters from the rising liquid
        int chargeHold = 0;   // a charged shot held
    };

    void reset(int players);
    void setSkill(BotSkill s) { skill_ = s; }
    BotSkill skill() const { return skill_; }
    /// Fills the frames of the seats in `seats` (bit k = seat k) from the world.
    void think(RaidRun& run, InputFrame* frames, int count, uint32_t seats, bool adminActive);
    /// Compat (2.72): seats 1 .. players − 1, the administrator active.
    void think(RaidRun& run, InputFrame* frames, int count) {
        uint32_t seats = 0;
        for (int s = 1; s < players_; ++s) seats |= 1u << s;
        think(run, frames, count, seats, true);
    }
    /// The answer of seat `seat` to the power offered to it (the armory: its wish, else left there).
    uint8_t choice(const GameWorld& w, int seat) const;
    const std::vector<Bot>& bots() const { return bots_; }
    std::vector<Bot>& bots() { return bots_; }
    int players() const { return players_; }
    uint64_t hash() const;
    /// The graph of the zone now (null before the first step).
    const NavGraph* nav() const { return nav_.get(); }
    /// What the group does now, for the screen (« Générateur ouest : 2 joueurs », …).
    const std::string& plan() const { return plan_; }
    /// The next node of a seat's path (the rescue of a test puts it there), -1.
    int nextNode(int seat) const;
    /// The exit the group takes when its zone has nothing left for it (-1 none), and the zone it leads to.
    static int chooseExit(const RaidRun& run, int* toZone = nullptr);
    /// 2.78: the way out of an armory (the first one, or the lair's): its « Prêts » passage first.
    static int armoryExit(const RaidRun& run, int* toZone = nullptr);
    /// The power a seat wishes in the armory (one of the armory's tablets), "" none.
    static std::string wish(int seat);

    static constexpr int kStuckSteps = 8 * kSimHz;  // the rescue of a test (TestSession)

private:
    enum Act { None = 0, Strike, Shoot, Interact, Stand, Enter };
    void plan(RaidRun& run, uint32_t seats, bool adminActive);
    InputFrame drive(RaidRun& run, int seat, Bot& b);
    void steer(const GameWorld& w, int seat, Bot& b, InputFrame& f, uint32_t& held, int& dir, float& stop);
    void ensureNav(const RaidRun& run);
    int players_ = 1;
    BotSkill skill_ = BotSkill::Skilled;
    std::vector<Bot> bots_;   // by seat
    std::vector<Rect> dangers_;  // of the step: what is announced to strike (fists, jets, a gigaboss's marks)
    std::shared_ptr<const NavGraph> nav_;
    uint64_t navSig_ = 0;
    int navZone_ = -1;
    uint32_t tick_ = 0;
    bool syncReady_ = true;  // the players striking a mechanism together are all within reach
    size_t projectiles_ = 0;  // dangers_ from here on: the enemies' shots of the step
    std::string plan_;
};

}  // namespace cosmic::raid
