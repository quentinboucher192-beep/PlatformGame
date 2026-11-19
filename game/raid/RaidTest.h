#pragma once

// 2.72: the administrators' test of a raid (ADMINISTRATION › Raids › « Tester Asterion »,
// docs/RAIDS.md §15) played like a real instance, alone on one computer:
//
// * the branch's state is kept across the zones in the structure the server sends with RAID_STATE
//   (the objectives done, a bit each): what the group does is « accepted » at once and given back
//   with the next step, as the relay would after the consensus — the zones it leaves keep what was
//   done in them (raid::RaidRun);
// * the simulated players (seats 1 .. players − 1) are real players of the instance, visible, named
//   « Joueur simulé N »: a simple AI reads the world and answers with an InputFrame each step, as a
//   player's game would (follow the administrator, jump the gaps, strike the enemies near them,
//   stand on the plates, the lifts and the vents with them, relieve the players down, keep out of
//   the rising magma and the marked fists). Deterministic: the same steps give the same digest;
// * a test never reports anything (RaidRun::Test).
//
// Two rules of the test only (never in a real raid): a simulated player sent to a mechanism it cannot
// reach in 6 s is put on it, and (2.77) one that comes no nearer to its goal for 8 s on its path is put
// on the next place of that path — so a test alone always goes on. 2.77: the simulated players play the
// zone themselves (raid::TestBots, game/raid/RaidBots.h): they no longer follow the administrator, nobody
// is brought back to them; the administrator's seat can be free (a group of the full raid's simulation
// they are not in) and, AFK for 15 s, is played by the same AI until they touch a key again.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidBots.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid {

class TestSession {
public:
    TestSession(WorldServices services, const Branch& branch, const RaidRun::Test& setup);

    /// 2.74 (phase 6): the Cœur d'Asterion's test (before begin): the links of the parts of the titan
    /// chosen as fallen (their bits in the branch's state, as RAID_STATE gives them) and its shared
    /// state (the relics), given with every step.
    void setHeart(uint32_t links, const HeartShared& heart) {
        state_ |= links;
        heart_ = heart;
    }
    bool begin(int zone);
    /// One step: the administrator's frame on seat 0, the simulated players on theirs, the free
    /// seats vacant; the branch's state given as RAID_STATE would give it. `scripted` (2.70, the
    /// captures: --raid-test): seats played by a script, in place of their simulated player.
    void step(const InputFrame& admin, const std::vector<std::pair<int, InputFrame>>& scripted = {});

    RaidRun& run() { return run_; }
    const RaidRun& run() const { return run_; }
    const TestBots& bots() const { return bots_; }
    /// The branch's state of the test (the objectives done, RAID_STATE's u32).
    uint32_t branchState() const { return state_; }
    int players() const { return players_; }
    /// The frames of the last step (one per seat).
    const std::vector<InputFrame>& frames() const { return frames_; }
    /// « Joueur simulé N » (N = seat + 1): the names of the simulated players.
    static std::string botName(int seat);
    /// The digest of the test (the run's and the simulated players').
    uint64_t hash() const;

    static constexpr int kMechanismSteps = 6 * kSimHz;
    static constexpr int kAfkSteps = 15 * kSimHz;  // 2.77: the administrator's seat on autopilot after this

    // 2.77 (docs/RAIDS.md §21).
    /// The administrator's seat (0): played by this computer, or free (a group of the full raid they
    /// are not in: vacant, the group plays without them).
    void setAdminSeated(bool on) { adminSeated_ = on; }
    bool adminSeated() const { return adminSeated_; }
    /// The level of the simulated players (their reaction and precision).
    void setSkill(BotSkill s) { bots_.setSkill(s); }
    /// The administrator AFK (no key for 15 s): their seat is played by the AI until they touch one.
    bool autopilot() const { return adminSeated_ && afk_ >= kAfkSteps; }
    TestBots& botsMut() { return bots_; }
    /// The branch's state given with each step (the full raid's simulation: every group's validated work).
    void setBranchState(uint32_t state) { state_ = state; }
    void setHeartShared(const HeartShared& h) { heart_ = h; }
    /// The rescues of the test so far (a simulated player put on its mechanism or further on its path).
    int rescues() const { return rescues_; }

private:
    void rescue();
    RaidRun run_;
    TestBots bots_;
    int players_ = 1;
    uint32_t state_ = 0;
    HeartShared heart_;  // 2.74: the heart's test (its relics)
    std::vector<InputFrame> frames_;
    bool adminSeated_ = true;
    int afk_ = 0;
    int rescues_ = 0;
};

}  // namespace cosmic::raid
