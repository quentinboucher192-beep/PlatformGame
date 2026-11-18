#pragma once

// 2.65: the run of a raid instance (docs/RAIDS.md): up to 8 players in lockstep, from zone to
// zone of a branch. One world at a time (the zone the instance is in); the rules of the raid
// run around it, step by step, the same on every computer of the instance:
//
// * objectives: the zone's events (an arena cleared, a guardian killed, a generator hit, a
//   relic taken) mark them done for the instance; the two objectives of a pair (the bellows of
//   the Coude-forge) are done by two distinct groups: once one is done by this instance, the
//   other refuses it;
// * passages: the first player in play who walks into an open passage (lowest number first)
//   takes the whole instance to the neighbouring zone; each player keeps their hearts, powers
//   and presence, and appears at the zone's place for where they come from;
// * the branch's state (objectives done by every group, accepted by the server after the
//   consensus of each instance) comes with the relay's frames at a given step (RAID_STATE):
//   it opens the locked passages (generators, shortcut) for everyone at that same step;
// * the magma flow: a damage zone switched on every period, announced `warn` seconds before;
// * the whole instance down: the zone starts again where it entered it (progress kept).
//
// Nothing here reads the clock, the camera or the network: only the frames and the state given
// with them. Each completion (objective, zone entered) is a report the game sends to the server
// with its step and the digest of the state: the server accepts it when the majority of the
// instance's players reported the same.

#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidBranch.h"

namespace cosmic::raid {

struct Report {
    /// 2.67: Boss (the gigaboss's phase reached: 1 the fight begins, 2 and 3 its phases, 4 its fall)
    /// and Result (the players' contributions, Contributions::canonical, at each milestone).
    enum class Kind { Objective, Zone, Boss, Result } kind = Kind::Objective;
    std::string objective;  // Kind::Objective
    int zone = 0;
    uint32_t tick = 0;
    uint64_t hash = 0;
    int phase = 0;              // Kind::Boss
    std::vector<Contribution> records;  // Kind::Result
};

/// A line of the raid's alerts (display only, made from the simulation's events).
struct Alert {
    std::string text;
    enum class Kind { Group, Branch, Danger } kind = Kind::Group;
    uint32_t tick = 0;
};

class RaidRun {
public:
    RaidRun(WorldServices services, const Branch& branch, int players);

    /// Loads the start zone (the branch's state known at step 0); `zone` another one (tests).
    bool begin(uint32_t branchMask, int zone = 0);
    /// One step: the frames of the step (one per seat) and the branch's state at this step.
    void step(const InputFrame* frames, int count, uint32_t branchMask);

    GameWorld& world() { return *world_; }
    const GameWorld& world() const { return *world_; }
    const Branch& branch() const { return branch_; }
    const ZoneRules& rules() const { return *rules_; }
    int zone() const { return zone_; }
    uint32_t tick() const { return tick_; }
    /// Steps since the zone was entered.
    uint32_t zoneTicks() const { return zoneTicks_; }
    /// What the instance knows done: the branch's state and its own completions.
    uint32_t mask() const { return branchMask_ | local_; }
    uint32_t localDone() const { return local_; }
    /// Changes each time another world is loaded (the scene builds its renderer again).
    int zoneSerial() const { return serial_; }
    /// Digest of the run (the world's simulationStateHash, the zone, the steps, the state).
    uint64_t hash() const;
    std::vector<Report> takeReports();
    const std::vector<Alert>& alerts() const { return alerts_; }

    /// The magma flow: on now, and seconds before the next one (-1 when the zone has none).
    bool magmaActive() const { return magmaOn_; }
    float magmaIn() const;
    /// Whether a passage is open now.
    bool passageOpen(const Passage& p) const { return branch_.lockOpen(p.lock, mask()); }
    /// 2.67: the group switches of the zone: steps held, done (k = index in rules().switches).
    struct SwitchState {
        int hold = 0;
        bool done = false;
        std::vector<int> on;  // players standing on each plate this step
    };
    const std::vector<SwitchState>& switches() const { return switchStates_; }
    /// The players a switch needs on each plate now (its `need`, never more than the seats taken).
    int switchNeed(const GroupSwitch& sw) const;
    /// 2.67: the gigaboss of the zone (its fight; nothing in a zone without one).
    const Gigaboss& gigaboss() const { return gigaboss_; }
    /// 2.67: what each player did since the instance started (the same on every computer).
    const Contributions& contributions() const { return contrib_; }

    /// 2.67: an administrator's test (ADMINISTRATION › Raids › « Tester Asterion »): solo, local,
    /// never counted: no report at all (takeReports stays empty), every world a training run
    /// (invincible when asked), the gigaboss scaled for `players` and started at `startPhase`.
    struct Test {
        bool on = false;
        bool invincible = false;
        int players = 2;
        int startPhase = 1;
    };
    void setTest(const Test& t);
    bool test() const { return test_.on; }
    const Test& testSetup() const { return test_; }

    /// Display / catch-up settings, kept for every world of the run.
    void setQuiet(bool on);
    void setCameraFollow(int index);

private:
    void loadZone(int zone, const std::string& from, const std::vector<GameWorld::MemberCarry>* carry);
    void applyMask(uint32_t mask, bool initial);
    void updateMagma();
    void checkPassages();
    void completed(const std::string& source, const std::string& trigger, int who);
    void reportResult();
    void stepSwitches();
    void alert(std::string text, Alert::Kind kind);

    WorldServices services_;
    const Branch& branch_;
    int players_;
    std::unique_ptr<GameWorld> world_;
    const ZoneRules* rules_ = nullptr;
    int zone_ = 0;
    std::string from_ = "start";
    uint32_t tick_ = 0, zoneTicks_ = 0;
    uint32_t branchMask_ = 0, local_ = 0;
    int serial_ = 0;
    bool magmaOn_ = false;
    bool quiet_ = false;
    int follow_ = -1;
    std::vector<uint8_t> insidePassage_;  // per passage: bits of the players inside it
    struct Event {
        std::string source, trigger;
        int who = -1;  // 2.67: the seat whose attack caused it (-1 unknown)
    };
    std::vector<Event> events_;  // of the step
    Contributions contrib_;      // 2.67
    Gigaboss gigaboss_;          // 2.67
    int bossReported_ = 0;       // 2.67: the highest phase of the gigaboss already reported
    uint32_t lastResult_ = UINT32_MAX;  // 2.67: the step of the last table reported
    Test test_;                         // 2.67: an administrator's test (never reported)
    std::vector<SwitchState> switchStates_;  // 2.67
    std::vector<Report> reports_;
    std::vector<Alert> alerts_;
};

}  // namespace cosmic::raid
