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
// * it revives the players down (the administrator too), takes its powers in the armory (its wish,
//   through InputFrame::choice) and waits at the exit for the administrator when they are there and
//   playing (AFK: it goes on alone).
//
// 2.79 (docs/RAIDS.md §28), « des GOAT »:
//
// * a tactic per Gigaboss (game/raid/bots/BossTactic: its blows announced, its places to hold together,
//   its targets, its weak points while bare, what to finish together), read every step;
// * roles in the group, given by the plan and changed when someone falls: the lure (the nearest to the
//   boss, keeps its attention), the strikers (the weak points first), the reviver (the nearest soul,
//   when it is safe, its decoy over it), the runner(s) (the mechanisms, the objectives); the group
//   gathers at the plates that need several players;
// * how it fights: from afar with the wave aimed (eight directions, as a pad's stick) from a place of
//   the graph that sees its target, out of the boss's body (it hurts on contact) and of what is coming;
//   its energy kept for the shield and the weak points; it steps out of beams, jumps the waves running
//   along its floor, leaves the marks, climbs above liquids, leaves a sector whose gravity turns over or
//   stands under a slab, hides from a flash behind a column; its shield when a blow cannot be avoided
//   (or while it holds a plate); its decoy over an ally it revives;
// * the administrator's orders (« Suivez-moi », « Attendez ici », « Tous sur l'objectif », « Réanimez-le »,
//   « Libres », a place marked) ride on their frame of a step (bots::withOrder): replayed, the same raid;
// * what each one thinks (its role, its intention, how it moves) for the bubbles over it and the raid's
//   menu (display only: never in the digest);
// * its level (Débutant, Confirmé, Expert) is its reaction time, its precision (its timing, its aim) and its
//   choices (its shield, its reading of the beams), never anything a player cannot do.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidNav.h"
#include "game/raid/RaidRun.h"
#include "game/raid/nav/Pilot.h"
#include "game/raid/bots/BossTactic.h"
#include "game/raid/bots/BotTypes.h"

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
        // 2.79
        bots::Role role = bots::Role::None;
        int syncGroup = 0;    // the sync group of its target (0 none)
        float holdAt = 0.0f;  // its target's share of health to hold at until the group is ready
        bool ranged = false;  // strikes its target from afar
        int spot = -1;        // the node it shoots from
        int spotDir = 0;      // the direction it shoots in from there (0..7, kDirs)
        int spotAt = -100000; // the step the spot was chosen
        bool jumpShot = false; // its target is out of reach from the ground there: it fires at the top of a double jump
        int hideNode = -1;    // where it hides from a flash
        int escapeDir = 0;    // the side it leaves a danger by (sticky while it lasts)
        int escapeSteps = 0;
        int waveJumpCool = 0; // steps before it jumps a wave again
        bool onStation = false;
        int shieldCool = 0;
        uint32_t revivee = 0; // the seat it revives + 1 (0 none)
        bots::Thought thought;
        std::string intent;   // its task's intention (the plan's), shown unless it reacts to something
        std::string moveText; // how it moves now (the navigation's intention)
    };

    void reset(int players);
    void setSkill(BotSkill s) {
        skill_ = s;
        navi_.setSkill(static_cast<int>(s));
    }
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
    /// The graph of the zone now (null before the first step, and while a new zone's is not due yet).
    const NavGraph* nav() const { return navi_.graph(); }
    /// 2.79: how the seats move (game/raid/nav: the graph, the lookahead, the cost measured).
    const nav::Navigator& navigation() const { return navi_; }
    /// 2.79: what a seat thinks of its moves (« Double saut au-dessus du vide »…), for the screen.
    const nav::Thought& moveThought(int seat) const { return navi_.pilot(seat).thought(); }
    /// What the group does now, for the screen (« Générateur ouest : 2 joueurs », …).
    const std::string& plan() const { return plan_; }
    /// The next node of a seat's path (the rescue of a test puts it there), -1.
    int nextNode(int seat) const;
    /// The exit the group takes when its zone has nothing left for it (-1 none), and the zone it leads to.
    static int chooseExit(const RaidRun& run, int* toZone = nullptr);
    /// 2.78: the way out of an armory (the first one, or the lair's): its « Prêts » passage first.
    static int armoryExit(const RaidRun& run, int* toZone = nullptr);
    /// The power a seat wishes in the armory (one of the armory's tablets), "" none. 2.79: its role's, from the
    /// tactic of its branch's gigaboss (`routine`).
    static std::string wish(int seat);
    std::string wishOf(int seat) const;

    // ------------------------------------------------------------------ 2.79
    /// The administrator's order (from their frame, TestSession): applied from this step.
    void order(const bots::Order& o);
    const bots::Order& currentOrder() const { return order_; }
    /// What a seat thinks now (display only).
    const bots::Thought& thought(int seat) const;
    /// The seats it plays (the last step's).
    uint32_t seats() const { return seats_; }
    /// What its tactic reads now (display, tests): the boss's view of the last step.
    const bots::BossView& view() const { return view_; }
    const bots::BossTactic* tactic() const { return tactic_.get(); }
    /// The names of the seats in the thoughts (« Joueur simulé 3 », « Vous »): given by the session.
    void setNames(std::vector<std::string> names) { names_ = std::move(names); }

    static constexpr int kStuckSteps = 8 * kSimHz;  // the rescue of a test (TestSession)

private:
    enum Act { None = 0, Strike, Shoot, Interact, Stand, Enter };
    void sense(RaidRun& run);
    void plan(RaidRun& run, uint32_t seats, bool adminActive);
    void assignRoles(const RaidRun& run, const std::vector<int>& drivers, bool fight);
    InputFrame drive(RaidRun& run, int seat, Bot& b);
    void steer(const GameWorld& w, int seat, Bot& b, InputFrame& f, uint32_t& held, int& dir, float& stop);
    void ensureNav(const RaidRun& run);
    /// 2.79: the place it shoots `target` from (a node of the graph) and the direction; false: none.
    bool chooseSpot(const GameWorld& w, int seat, Bot& b, const Rect& target, const Enemy* body);
    /// Whether a wave from `center` in direction `d` (0..7) reaches `target` (in range, nothing solid between).
    bool shotReaches(const GameWorld& w, Vec2 center, int d, const Rect& target) const;
    /// The direction from `center` that reaches `target` now (-1 none).
    int shotFrom(const GameWorld& w, Vec2 center, const Rect& target, bool grounded) const;
    /// A danger of the step on this seat within `horizon` steps (its box, a line, a wave on its floor).
    bool threatened(const GameWorld& w, int seat, const Rect& box, int horizon, const bots::Danger** out = nullptr) const;
    bool safeSpot(const GameWorld& w, int seat, Vec2 feet, int horizon) const;
    std::string nameOf(int seat) const;
    void think(Bot& b, const std::string& intent);

    int players_ = 1;
    BotSkill skill_ = BotSkill::Skilled;
    std::vector<Bot> bots_;   // by seat
    std::vector<bots::Danger> dangers_;  // of the step: what is coming (the tactic's reading, else the world's)
    nav::Navigator navi_;  // 2.79: the graph, the copy of the zone, a pilot per seat (game/raid/nav)
    int navZone_ = -1;
    uint32_t tick_ = 0;
    bool syncReady_ = true;  // the players striking a mechanism together are all within reach
    std::string plan_;
    // 2.79
    std::unique_ptr<bots::BossTactic> tactic_;
    std::string tacticFor_ = "-";
    bots::BossView view_;
    bool fight_ = false;
    bots::Order order_;
    uint32_t seats_ = 0;
    std::vector<std::string> names_;
    std::vector<uint8_t> syncLow_;  // per sync group: all its targets at or under their hold share
    uint32_t bossUid_ = 0;
    Rect bossBody_;  // its whole shape this step (every part, its box): kept out of
};

}  // namespace cosmic::raid
