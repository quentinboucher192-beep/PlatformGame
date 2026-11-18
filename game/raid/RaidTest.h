#pragma once

// 2.71: the administrators' test of a raid (ADMINISTRATION › Raids › « Tester Asterion »,
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
// Two rules of the test only (never in a real raid): a simulated player left far behind the
// administrator for 5 s comes back next to them, and one sent to a mechanism it cannot reach in
// 6 s is put on it — so the mechanisms for several players work in a test alone.

#include <cstdint>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid {

/// The simulated players of a test: the frames of seats 1 .. players − 1 from the world alone.
class TestBots {
public:
    enum class Task : uint8_t { Follow, Revive, Mechanism, Dodge, Fight };
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
    };

    void reset(int players);
    /// Fills frames[1 .. players − 1] (frames[0]: the administrator's, already there).
    void think(RaidRun& run, InputFrame* frames, int count);
    const std::vector<Bot>& bots() const { return bots_; }
    std::vector<Bot>& bots() { return bots_; }
    int players() const { return players_; }
    uint64_t hash() const;

private:
    InputFrame drive(const GameWorld& w, int seat, Bot& b);
    int players_ = 1;
    std::vector<Bot> bots_;   // by seat (0 unused)
    std::vector<Rect> dangers_;  // of the step: what is announced to strike (fists, jets, a gigaboss's marks)
};

class TestSession {
public:
    TestSession(WorldServices services, const Branch& branch, const RaidRun::Test& setup);

    bool begin(int zone);
    /// One step: the administrator's frame on seat 0, the simulated players on theirs, the free
    /// seats vacant; the branch's state given as RAID_STATE would give it.
    void step(const InputFrame& admin);

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

    static constexpr int kRegroupSteps = 5 * kSimHz;
    static constexpr int kMechanismSteps = 6 * kSimHz;

private:
    void regroup();
    RaidRun run_;
    TestBots bots_;
    int players_ = 1;
    uint32_t state_ = 0;
    std::vector<InputFrame> frames_;
    std::vector<int> far_;  // per seat: steps far from the administrator
};

}  // namespace cosmic::raid
