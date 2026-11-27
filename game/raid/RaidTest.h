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
#include <map>
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
    /// The simulated players of the zone shown (2.78: each zone where players are has its own, thinking
    /// for the seats in it: the players of an instance may be spread over several zones).
    const TestBots& bots() const { return botsOf(run_.zone()); }
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
    /// 2.77.3 (« Simuler un raid complet », « Joueurs par groupe »): the administrator's seat, while they are
    /// not in this group, played by a simulated player (every group the same size; taking it back, they
    /// replace that player). Off: the seat stays free (the zone's test, the captures).
    void setFreeSeatPlayed(bool on) { freeSeatPlayed_ = on; }
    /// The seats played by the AI now (the administrator's included when it plays it).
    int simulatedPlayers() const { return players_ - 1 + (seatZeroBot() ? 1 : 0); }
    /// The level of the simulated players (their reaction and precision).
    void setSkill(BotSkill s) {
        skill_ = s;
        for (auto& [zone, b] : bots_) b.setSkill(s);
    }
    /// 2.84: the seed of the simulated players' starts (TestBots::setSeed): the measures over several starts.
    void setBotSeed(uint32_t seed) {
        seed_ = seed;
        for (auto& [zone, b] : bots_) b.setSeed(seed);
    }
    /// The administrator AFK (no key for 15 s): their seat is played by the AI until they touch one.
    bool autopilot() const { return adminSeated_ && (adminBot_ || afk_ >= kAfkSteps); }
    /// 2.95: the AI plays seat 0 now (the administrator's autopilot, or the free seat played).
    bool aiPlaysSeatZero() const { return seatZeroBot(); }
    TestBots& botsMut() { return botsFor(run_.zone()); }
    /// The branch's state given with each step (the full raid's simulation: every group's validated work).
    void setBranchState(uint32_t state) { state_ = state; }
    void setHeartShared(const HeartShared& h) { heart_ = h; }
    /// The times a simulated player stayed stuck so far (sent to a mechanism it did not reach in 6 s, or no
    /// nearer to its goal for 8 s). 2.79: it is never moved for it (only its inputs move it): its path is
    /// made again.
    int rescues() const { return rescues_; }
    /// « Joueurs simulés invincibles » (a test's setting, never a real raid): their hearts come back
    /// after each step (a fall into the void still puts them down, to be revived).
    void setBotsInvincible(bool on) { botsInvincible_ = on; }
    bool botsInvincible() const { return botsInvincible_; }

    // 2.80 (docs/RAIDS.md « Tester avec des joueurs invités »): real players invited by the administrator, the test relayed to every
    // computer (the same steps, the same frames, the same digest).
    /// The seats a real player plays at the next steps (bit = seat, never 0): their frames come with
    /// step() (`scripted`), their simulated player neither thinks nor is rescued, and « Joueurs simulés
    /// invincibles » leaves them alone. A seat left (the player gone) is its simulated player's again.
    void setHumanSeats(uint32_t seats) { humans_ = seats & ~1u; }
    uint32_t humanSeats() const { return humans_; }
    /// The invincible seats (bit = seat): the administrator's (seat 0) and those of the invited players
    /// the administrator ticked « Invincible ».
    void setInvincibleSeats(uint32_t seats) { run_.setInvincibleSeats(seats); }
    // 2.79 (docs/RAIDS.md §28).
    /// The administrator's seat played by the AI from the start (a test of 8 simulated players).
    void setAdminBot(bool on) { adminBot_ = on; }
    bool adminBot() const { return adminBot_; }
    /// The two rescues of a test (a simulated player put on its mechanism or further on its path); off: the
    /// simulated players only ever play with their inputs (the measures of docs/RAIDS.md §28).
    void setRescues(bool on) { rescuesOn_ = on; }
    bool rescuesOn() const { return rescuesOn_; }
    /// The last order the administrator gave (from their frame: bots::withOrder), its step.
    const bots::Order& lastOrder() const { return lastOrder_; }
    /// The deaths of the simulated players so far (a player of a seat played by the AI put down).
    int botDeaths() const { return botDeaths_; }

private:
    void rescue();
    /// 2.78: the frames of the simulated players, zone by zone.
    void thinkByZone(uint32_t seats);
    /// 2.78: the simulated players thinking in `zone` (made the first time a player is there).
    TestBots& botsFor(int zone);
    const TestBots& botsOf(int zone) const;
    RaidRun run_;
    std::map<int, TestBots> bots_;  // 2.78: by zone
    BotSkill skill_ = BotSkill::Skilled;
    uint32_t seed_ = 0;  // 2.84: setBotSeed
    int lastZone_[kMaxInstancePlayers] = {-2, -2, -2, -2, -2, -2, -2, -2};  // 2.78: the zone of each seat at the last step
    int players_ = 1;
    uint32_t state_ = 0;
    HeartShared heart_;  // 2.74: the heart's test (its relics)
    std::vector<InputFrame> frames_;
    bool seatZeroBot() const { return autopilot() || (!adminSeated_ && freeSeatPlayed_); }
    bool adminSeated_ = true;
    bool freeSeatPlayed_ = false;
    bool botsInvincible_ = false;
    uint32_t humans_ = 0;  // 2.80: the seats real players play
    bool adminBot_ = false;    // 2.79
    bool rescuesOn_ = true;    // 2.79
    int botDeaths_ = 0;        // 2.79
    uint8_t downBefore_ = 0;   // 2.79: the seats down at the last step
    std::vector<std::string> names_;  // 2.79: the seats' names in the thoughts
    bots::Order lastOrder_;           // 2.79: the administrator's last order (2.78: given to every zone's simulated players)
    bool namesFor_ = false;
    int afk_ = 0;
    int rescues_ = 0;
};

}  // namespace cosmic::raid
