#pragma once

// 2.77: « Simuler un raid complet » (ADMINISTRATION › Raids › Tester Asterion, docs/RAIDS.md §21): the
// whole clan raid played LOCALLY by simulated players — the seven branches with their zones (each
// branch's armory first), then the Cœur d'Asterion with every group — with the rules of a real raid and
// what the server would do between the instances, here in the same program, step by step:
//
// * groups (§41, titan 16): ALWAYS seven instances (raid::TestSession), group k in branch k, never two in a
//   branch; the raid's 14 to 56 players spread over them as evenly as possible (2 to 8 each, seat 0 the free
//   place the administrator may take);
// * §41 (the clan's rules): a group whose branch's gigaboss fell STAYS there, « en attente »; it never goes to
//   help a group that is only slow or weakened. A waiting group leaves its branch only to REPLACE a group wiped
//   elsewhere: it takes the zone back where that group fell (the zone of most of its players), with the branch's
//   validated state (as the server's retake, raidWipe.ts: start_zone). Nobody waiting: the zone stays to take.
//   A pair of objectives (each branch's zone 4, the Torse's plates) is done by the group itself, split in two
//   (RaidRun: both halves within 3 s);
// * the end: the heart's fall is the victory; the raid lasts `minutes` (60, the standard raid): at its end with the
//   heart standing, or at once when every group is wiped, it is lost (« Défaite : temps écoulé », « Défaite : tous
//   les groupes éliminés »);
// * the branch's state: what each group did, accepted at once for every group of the branch (as the
//   server after the consensus), and the titan's links (an objective of one branch acting on another)
//   computed from it like server/src/raidMap.ts statesOf;
// * a group wiped (every player down at once): its players spectate, the zone is « à reprendre » until a
//   waiting group replaces it (no new players ever come from nowhere);
// * the seven gigaboss fallen: the heart opens, every group still playing goes to it, each in its own
//   instance (several at once); its health is ONE for all of them (each instance's part summed, what the
//   others took given to each, its phase, the relics validated: server/src/raidHeart.ts), and its fall is
//   the end of the raid;
// * world events: the titan's feed (objectives, zones finished, links, gigaboss fallen, wipes,
//   waiting groups, replacements, the heart); the titan's map (each part's state), the contributions of every group.
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

#include "game/raid/Difficulty.h"
#include "game/raid/RaidTest.h"

namespace cosmic::raid {

struct SimSetup {
    /// §41 (titan 16): the raid's players (14 to 56, the administrator's place counted), spread over ALWAYS seven groups
    /// (one per branch, 2 to 8 each, as even as possible).
    int players = 35;
    /// 2.77.3 (« Joueurs par groupe »): the administrator's seat of a group they are not in is played by a
    /// simulated player too. Off: that seat stays free.
    bool fullGroups = false;
    bool invincible = true;              // the administrator's seat
    bool botsInvincible = false;         // the simulated players too (a test only)
    BotSkill skill = BotSkill::Skilled;
    int background = 1;                  // steps of each group not watched per step of the raid (1, 2 or 4)
    int startPhase = 1;                  // of every gigaboss (tests)
    int heartStartPhase = 0;             // of the heart (tests; 0: startPhase)
    int difficulty = kDifficultyDefault;  // titan 13: of every gigaboss and the heart (1 to 5)
    /// Tests (« configuration réduite »): each branch starts at its lair, the objectives before it given
    /// as done by other groups; the heart at its arena. The real lairs, the real heart.
    bool lairs = false;
    /// Tests: the branches (parts) whose gigaboss is given as already fallen (validated, as if other groups had
    /// beaten it) — those the simulated players cannot beat yet (docs/RAIDS.md §21, limits).
    std::vector<std::string> given;
    /// 2.79: the test's two rescues (a simulated player put on its mechanism or further on its path); off for the
    /// measures (docs/RAIDS.md §28: only their inputs).
    bool rescues = true;
    /// §41: how long the raid lasts (the standard raid: 60 min): the heart still standing then, the raid is lost. 0: no end
    /// (the measures that go on).
    int minutes = 60;

    static constexpr int kGroups = 7;
    static constexpr int kMinPlayers = 14;
    static constexpr int kMaxPlayers = 56;
    /// §41: the players of group k (0-based): as even as possible (sizes differ by one at most).
    static int groupSize(int players, int k) { return players / kGroups + (k < players % kGroups ? 1 : 0); }
};

class RaidSimulation {
public:
    struct Group {
        int id = 0;               // 1-based, in the order they were formed
        std::string name;         // « Groupe 3 »
        int branch = 0;           // index in branches() (the heart: heartIndex())
        std::unique_ptr<TestSession> session;
        bool wiped = false;       // its players spectate
        /// §40: « en attente »: its branch's gigaboss fell; it stays there until a group is wiped (it replaces
        /// it) or the heart opens.
        bool waiting = false;
        uint32_t waitingSince = 0;  // the raid's step it began to wait at
        int relieves = 0;         // §40: the id of the wiped group it replaced (0: none)
        int seats = 1;            // §41: its places (2 to 8: the administrator's free place and the simulated players)
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
    /// 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): a real player invited by the administrator on a seat (1 and up) of a
    /// group (an index in groups()), with the frame they played at this step.
    struct Human {
        int group = 0;
        int seat = 1;
        InputFrame frame;
    };
    /// The same step with the invited players' frames: their seats are theirs (their simulated players
    /// neither think nor are rescued); a seat absent from `humans` is its simulated player's again.
    /// The test relayed to every computer gives every one of them the same steps (the same digest).
    void step(const InputFrame& admin, const std::vector<Human>& humans);
    /// 2.80: the invincible seats of a group (bit = seat): the administrator's seat 0 (SimSetup::invincible)
    /// and the invited players ticked « Invincible »; applied from the next step on.
    void setInvincibleSeats(int group, uint32_t seats);

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
    /// §40: a wiped group's place, until a waiting group replaces it (oldest first): its branch, the zone where it
    /// fell, its id, the step.
    struct Vacancy {
        int branch = 0, zone = 0, group = 0;
        uint32_t tick = 0;
    };
    const std::vector<Vacancy>& vacancies() const { return vacancies_; }
    /// The branch where group k (0-based) starts (§41: branch k).
    int startBranch(int k) const { return k; }
    /// §41: how the raid ends (the heart's fall, the time over, every group wiped), and the step it did.
    enum class Outcome : uint8_t { Running, Victory, DefeatTime, DefeatWiped };
    Outcome outcome() const { return outcome_; }
    uint32_t endedAt() const { return endedAt_; }
    /// The step the raid ends at with the heart standing (0: none).
    uint32_t deadline() const { return setup_.minutes > 0 ? static_cast<uint32_t>(setup_.minutes) * 60u * static_cast<uint32_t>(kSimHz) : 0u; }
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
    /// 2.79: the simulated players put down so far (every group), and the test's rescues.
    int botDeaths() const;
    int rescues() const;
    uint64_t hash() const;

private:
    Group& form(int branch, int zone, const std::string& name, uint32_t extraMask, int seats);
    void aggregate();
    void moveGroup(Group& g, int branch, int zone);
    /// §41: the waiting groups sent where the rules allow: to replace a wiped group, nothing else.
    void dispatch();
    Group* longestWaiting();
    int startZone(int branch) const;
    int groupsIn(int branch) const;  // not wiped
    int realPart(Group& g, int own) const;
    void event(std::string text, WorldEvent::Kind kind);

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
    std::vector<Vacancy> vacancies_;       // §40
    Outcome outcome_ = Outcome::Running;  // §41
    uint32_t endedAt_ = 0;
    bool heartOpen_ = false, heartFallen_ = false;
    int heartTaken_ = 0;
    int viewed_ = 0;
    bool seated_ = true;
    uint32_t tick_ = 0;
    std::vector<WorldEvent> feed_;
    std::map<int, uint32_t> invincible_;  // 2.80: per group (index), when set
    double lastMs_ = 0.0, avgMs_ = 0.0, maxMs_ = 0.0;
    uint64_t worldSteps_ = 0;
};

}  // namespace cosmic::raid
