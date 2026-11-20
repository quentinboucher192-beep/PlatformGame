#pragma once

// 2.77: « Simuler un raid complet » (ADMINISTRATION › Raids › Tester Asterion, docs/RAIDS.md §21): the
// whole clan raid played LOCALLY by simulated players — the seven branches with their zones (each
// branch's armory first), then the Cœur d'Asterion with every group — with the rules of a real raid and
// what the server would do between the instances, here in the same program, step by step:
//
// * groups: N instances (raid::TestSession), each of `bots` simulated players plus one free place (seat
//   0) the administrator may take; group k starts in branch k mod 7; a group with nothing left to do in
//   its branch goes where it is needed (the branch with the fewest groups whose gigaboss still stands);
// * the branch's state: what each group did, accepted at once for every group of the branch (as the
//   server after the consensus), and the titan's links (an objective of one branch acting on another)
//   computed from it like server/src/raidMap.ts statesOf;
// * a group wiped (every player down at once): its players spectate, the zone is « à reprendre », and a
//   reinforcement group takes it back from that zone (new simulated players: in a real raid, players of
//   groups that finished their zone);
// * the seven gigaboss fallen: the heart opens, every group still playing goes to it; its health is
//   shared (each instance's part summed, what the others took given to each, its phase, the relics
//   validated: server/src/raidHeart.ts), and its fall is the end of the raid;
// * world events: the titan's feed (objectives, zones finished, links, gigaboss fallen, wipes,
//   reinforcements, the heart); the titan's map (each part's state), the contributions of every group.
//
// NOTHING is sent anywhere: no network, no report (every RaidRun is a test), no reward. Deterministic:
// the same administrator's frames (and the same choices of group, at the same steps) give the same raid.
// The groups the administrator does not watch are simulated without being drawn; « accéléré » steps them
// `background` times for each step of the raid (said on the screen).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/raid/RaidTest.h"

namespace cosmic::raid {

struct SimSetup {
    int groups = 7;                      // 1 to 14
    int bots = 4;                        // per group, the administrator not counted (1 to 7)
    /// 2.77.3 (« Joueurs par groupe : N »): the administrator's seat of a group they are not in is played by a
    /// simulated player too: every group has bots + 1 players (theirs: them + bots). Off: that seat stays free.
    bool fullGroups = false;
    bool invincible = true;              // the administrator's seat
    bool botsInvincible = false;         // the simulated players too (a test only)
    BotSkill skill = BotSkill::Skilled;
    int background = 1;                  // steps of each group not watched per step of the raid (1, 2 or 4)
    int startPhase = 1;                  // of every gigaboss (tests)
    int heartStartPhase = 0;             // of the heart (tests; 0: startPhase)
    /// Tests (« configuration réduite »): each branch starts at its lair, the objectives before it given
    /// as done by other groups; the heart at its arena. The real lairs, the real heart.
    bool lairs = false;
    /// Tests: the branches (parts) whose gigaboss is given as already fallen (validated, as if other groups had
    /// beaten it) — those the simulated players cannot beat yet (docs/RAIDS.md §21, limits).
    std::vector<std::string> given;
    int maxReinforcements = 8;
};

class RaidSimulation {
public:
    struct Group {
        int id = 0;               // 1-based, in the order they were formed
        std::string name;         // « Groupe 3 », « Renfort 1 »
        int branch = 0;           // index in branches() (the heart: heartIndex())
        std::unique_ptr<TestSession> session;
        bool wiped = false;       // its players spectate
        int relieves = 0;         // a reinforcement: the id of the group it took the zone back from
        int idleSteps = 0;        // steps with nothing left to do in its branch
        int moves = 0;            // branches it went to after the first
        int heartBase = -1;       // tests started at a later phase: its part of the heart's health at that start (no damage)
        uint32_t formed = 0;      // the raid's step it was formed at
        /// 2.79 (display only, never in the hash): what the group did in each branch it left (moveGroup): its
        /// players' contributions there and its zones' tables (the contributions table shows each part's groups).
        struct Stint {
            int branch = 0;
            std::vector<ZoneContribution> rows;
            std::vector<ZoneFinish> finishes;
        };
        std::vector<Stint> past;
    };
    struct WorldEvent {
        std::string text;
        uint32_t tick = 0;
        enum class Kind : uint8_t { Group, Branch, World, Danger } kind = Kind::World;
    };
    /// A part of the titan on its map (asterion::State's words).
    enum class PartState : uint8_t { Unknown, Intact, Attacked, Weakened, Destroyed };

    RaidSimulation(WorldServices services, const SimSetup& setup);
    bool begin();
    /// One step of the raid: the administrator's frame on their seat (when they hold one).
    void step(const InputFrame& admin);

    const SimSetup& setup() const { return setup_; }
    const std::vector<std::unique_ptr<Group>>& groups() const { return groups_; }
    Group& group(int g) { return *groups_[static_cast<size_t>(g)]; }
    const Group& group(int g) const { return *groups_[static_cast<size_t>(g)]; }
    const std::vector<const Branch*>& branches() const { return branches_; }
    int heartIndex() const { return heart_; }
    /// The branch's state given to its instances now (objectives validated, links of the titan on).
    uint32_t mask(int branch) const;
    bool gigabossDown(int branch) const;
    int gigabossesDown() const;
    bool heartOpen() const { return heartOpen_; }
    bool heartFallen() const { return heartFallen_; }
    /// Ten-thousandths of the heart's shared health taken (every heart instance), its phase (1-4, 5 fallen).
    int heartTaken() const { return heartTaken_; }
    int heartPhase() const;
    int relics() const;
    uint32_t tick() const { return tick_; }
    /// The zones « à reprendre » of a branch: zone → the group that fell there.
    std::map<int, std::string> retake(int branch) const;
    PartState partState(int branch) const;
    const std::vector<WorldEvent>& feed() const { return feed_; }

    // ---------------------------------------------------------------- the administrator
    /// The group shown (played or watched), and whether the administrator holds its free place.
    int viewed() const { return viewed_; }
    bool seated() const { return seated_; }
    /// Takes the free place of group `g` (false: wiped, or no place); watch(): spectator of `g`.
    bool join(int g);
    void watch(int g);

    // ---------------------------------------------------------------- measures (display only)
    double lastStepMs() const { return lastMs_; }
    double avgStepMs() const { return avgMs_; }
    double maxStepMs() const { return maxMs_; }
    /// World steps done by all the groups since the start.
    uint64_t worldSteps() const { return worldSteps_; }
    uint64_t hash() const;

private:
    Group& form(int branch, int zone, const std::string& name, uint32_t extraMask);
    void aggregate();
    void moveGroup(Group& g, int branch, int zone);
    static bool zoneHasWork(const RaidRun& run);  // 2.78: in the zone focused
    bool hasWork(const Group& g) const;
    int realPart(Group& g, int own) const;
    void event(std::string text, WorldEvent::Kind kind);
    int seedBranch() const;

    WorldServices services_;
    SimSetup setup_;
    std::vector<const Branch*> branches_;  // the seven, then the heart
    int heart_ = -1;
    std::vector<std::unique_ptr<Group>> groups_;
    std::vector<uint32_t> done_;           // per branch: the objectives validated (bits below 24)
    std::vector<uint32_t> finished_;       // per branch: the zones finished (told once)
    std::vector<uint32_t> links_;          // per branch: the links on (told once)
    std::vector<uint8_t> visited_;
    std::map<std::pair<int, int>, std::string> retake_;  // (branch, zone) → the group wiped there
    bool heartOpen_ = false, heartFallen_ = false;
    int heartTaken_ = 0;
    int reinforcements_ = 0;
    int viewed_ = 0;
    bool seated_ = true;
    uint32_t tick_ = 0;
    std::vector<WorldEvent> feed_;
    double lastMs_ = 0.0, avgMs_ = 0.0, maxMs_ = 0.0;
    uint64_t worldSteps_ = 0;
};

}  // namespace cosmic::raid
