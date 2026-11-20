#pragma once

// 2.65: the run of a raid instance (docs/RAIDS.md): up to 8 players in lockstep, from zone to
// zone of a branch. 2.78: ONE WORLD PER ZONE where a player of the instance is (on their feet, down
// or away), all stepped in lockstep, in a fixed order (by zone id), the same on every computer; the
// rules of the raid run around them, step by step:
//
// * objectives: the zone's events (an arena cleared, a guardian killed, a generator hit, a
//   relic taken) mark them done for the instance; the two objectives of a pair (the bellows of
//   the Coude-forge) are done by two distinct groups: once one is done by this instance, the
//   other refuses it;
// * passages: 2.78, a player in play who walks into an open passage changes zone ALONE: the others
//   stay where they are, and the group may be spread over several zones of the branch. They keep
//   their hearts, energy, powers and presence, and appear at the zone's place for where they come
//   from. A passage of the group (the armory's « Prêts », Passage::plate) opens only when its plate
//   holds the players it needs (those in play in that zone), and only the players standing on it
//   take it, together; the others stay;
// * the branch's state (objectives done by every group, accepted by the server after the
//   consensus of each instance) comes with the relay's frames at a given step (RAID_STATE):
//   it opens the locked passages (generators, shortcut) for everyone at that same step;
// * the magma flow: a damage zone switched on every period, announced `warn` seconds before
//   (2.68: each branch's own danger: psychic wave, claw sweep, heart pulse…), each zone its own;
// * 2.68, the links of the titan (bits 24 and up of the state, given like the rest): what another
//   branch did acts here at the same step for everyone (a passage opens, lasers go dark, the
//   reinforcements are gone, the danger stops), and again each time a zone is loaded;
// * 2.75, a player down comes back as many times as a teammate revives them (no limit in a raid),
//   2.78: a teammate OF THE SAME ZONE (the soul stays in the zone of the player down). The whole
//   instance down (nobody left in play in any zone, at least one down) is a WIPE: the group is out of
//   its zones for good (no restart, no checkpoint), the worlds stop, its players can only spectate,
//   and a Wipe report tells the server (the zones « à reprendre » until another group finishes
//   them). 2.78: a zone whose players are all down while others of the instance are in play
//   elsewhere goes on (its enemies, its dangers): their souls wait there for an ally who comes to
//   revive them (GameWorld::setRaidGroupRule). A player away (standby) is not down: the group waits
//   for them. Only every player gone for good with nobody down starts the zone again as before, at
//   its last checkpoint; nobody gets up at a checkpoint any more (only a revival brings a player back);
// * 2.68, co-op mechanisms: a plate where several players must stand together opens a lock's
//   doors on every floor, or starts a goods lift (fewer players when fewer are in play in its zone);
// * 2.72, the zones keep what was done in them (docs/RAIDS.md §15): when its last player leaves a
//   zone, or it starts again, it notes its doors opened, levers pulled, generators on, arenas cleared,
//   enemies beaten, relics taken, plates done and its last checkpoint; back in it, they are as the
//   group left them. The objectives of the branch's state (every group's, RAID_STATE) are applied the
//   same way: a guardian another group killed is not there, its generator is on;
// * 2.78, the lair's door (« ambushes:<zone> »): the passage to a gigaboss's lair stays shut while one
//   wave of one ambush of the zone before it is not beaten (its ambushes are objectives of the branch).
//
// Nothing here reads the clock, the camera or the network: only the frames and the state given
// with them. Each completion (objective, zone entered) is a report the game sends to the server
// with its step and the digest of the state: the server accepts it when the majority of the
// instance's players reported the same.

#include <climits>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"
#include "game/raid/RaidBranch.h"

namespace cosmic::raid {

class AsterionHeartRoutine;  // 2.74 (phase 6)

struct Report {
    /// 2.68: Boss (the gigaboss's phase reached: 1 the fight begins, 2 and 3 its phases, 4 its fall)
    /// and Result (the players' contributions, Contributions::canonical, at each milestone).
    /// 2.74 (phase 6): Heart (the ten-thousandths of the Cœur d'Asterion's shared health this instance
    /// took, `damage`: every reportEvery seconds while it changes, and at its fall).
    /// 2.75: ZoneTable (the « Tableau des contributions » of a zone finished while a player of the
    /// instance was in it: `zoneRecords`, Contributions::zoneCanonical), once per zone; Wipe (every player
    /// of the instance down at once; `zone` the zone of the most players, 2.78: `seats` the zone of
    /// each seat: the group is out of them for good, its players spectators; they stay to be taken
    /// again). 2.78: Zones (a player changed zone: `seats` the zone of each seat, 0 for a free seat;
    /// `zone` the zone entered).
    enum class Kind { Objective, Zone, Boss, Result, Heart, ZoneTable, Wipe, Zones } kind = Kind::Objective;
    std::string objective;  // Kind::Objective
    int zone = 0;
    uint32_t tick = 0;
    uint64_t hash = 0;
    int phase = 0;              // Kind::Boss
    std::vector<Contribution> records;  // Kind::Result
    int damage = 0;                     // Kind::Heart (0..10000)
    std::vector<ZoneContribution> zoneRecords;  // 2.75: Kind::ZoneTable
    std::vector<int> seats;                     // 2.78: Kind::Zones and Kind::Wipe (one per seat)
};

/// 2.75: a zone of the branch finished while a player of the instance was in it (by this group or
/// another, the branch's state at a step): every player of the instance sees it at that step (banner,
/// sound), with what each one did there (Contributions::zoneTable).
struct ZoneFinish {
    int zone = 0;
    uint32_t tick = 0;
    bool byUs = false;  // this instance did its last objective (else another group, RAID_STATE)
    std::vector<ZoneContribution> rows;
};

/// A line of the raid's alerts (display only, made from the simulation's events). 2.78: `zone`, the
/// zone it happened in (0: the whole instance or the branch), `seat`, the player it is about (-1 none).
struct Alert {
    std::string text;
    enum class Kind { Group, Branch, Danger } kind = Kind::Group;
    uint32_t tick = 0;
    int zone = 0;
    int seat = -1;
};

class RaidRun {
public:
    RaidRun(WorldServices services, const Branch& branch, int players);
    ~RaidRun();
    RaidRun(const RaidRun&) = delete;
    RaidRun& operator=(const RaidRun&) = delete;

    /// Loads the start zone (the branch's state known at step 0); `zone` another one (tests, a group
    /// taking a wiped zone again); 2.75: kArmory, the branch's armory first (its start zone without one).
    static constexpr int kArmory = -1;
    bool begin(uint32_t branchMask, int zone = 0);
    /// One step: the frames of the step (one per seat) and the branch's state at this step; 2.72 (phase
    /// 6): in the heart's zones, the shared state of the Cœur d'Asterion given with it (RAID_STATE).
    void step(const InputFrame* frames, int count, uint32_t branchMask, const HeartShared& heart = {});

    // ------------------------------------------------------------ 2.78: the zones of the instance
    /// The zones with a world now (a player of the instance in each: on their feet, down or away), in
    /// the fixed order they are stepped (by zone id; the armory, 0, first).
    std::vector<int> zonesLoaded() const;
    /// The zone of a seat (the world it is in; a free seat: the zone it was last in).
    int zoneOf(int seat) const;
    /// The seats in `zone` now (bit k = seat k), free seats aside.
    uint32_t seatsIn(int zone) const;
    /// The world of a zone (null when nobody is there).
    GameWorld* worldOf(int zone);
    const GameWorld* worldOf(int zone) const;
    const ZoneRules* rulesOf(int zone) const;
    const Gigaboss* gigabossOf(int zone) const;
    /// The zone the accessors below are about (the world shown, the zone of the simulated players
    /// thinking): -1 (the default) the zone of the seat the camera follows (setCameraFollow), else of
    /// the lowest seat in play. Display and the simulated players only: nothing of the run reads it.
    void focusZone(int zone) const { focus_ = zone; }
    int focusedZone() const;

    /// The focused zone's world, rules, zone and steps since it was loaded.
    GameWorld& world();
    const GameWorld& world() const;
    const Branch& branch() const { return branch_; }
    const ZoneRules& rules() const;
    int zone() const;
    uint32_t tick() const { return tick_; }
    uint32_t zoneTicks() const;
    /// What the instance knows done: the branch's state and its own completions.
    uint32_t mask() const { return branchMask_ | local_; }
    uint32_t localDone() const { return local_; }
    /// 2.75: the branch's state as the server validated it (RAID_STATE at this step).
    uint32_t validated() const { return branchMask_; }
    /// 2.75: the zones finished while a player of the instance was in them (oldest first), the one
    /// finished at this very step in the focused zone (0: none), 2.78: all of those of this step;
    /// zonesFinished(): every zone finished in mask(), one bit each.
    const std::vector<ZoneFinish>& finishes() const { return finishes_; }
    int zoneFinishedNow() const;
    const std::vector<int>& zonesFinishedNow() const { return finishedNowAll_; }
    uint32_t zonesFinished() const { return finishedSeen_; }
    /// Changes each time the focused zone's world changes (another world loaded, or the focus moved to
    /// another zone): the scene builds its renderer again.
    int zoneSerial() const;
    /// Digest of the run (every world's simulationStateHash, the zones of the seats, the steps, the state).
    uint64_t hash() const;
    /// 2.78: the digest of one zone's world (its simulationStateHash, 0 when nobody is there).
    uint64_t zoneHash(int zone) const;
    std::vector<Report> takeReports();
    const std::vector<Alert>& alerts() const { return alerts_; }

    /// The danger of the focused zone: on now, and seconds before the next one (-1 when the zone has none).
    bool magmaActive() const;
    float magmaIn() const;
    /// Whether a passage is open now (in mask(): its lock).
    bool passageOpen(const Passage& p) const { return branch_.lockOpen(p.lock, mask()); }
    /// 2.68: the gigaboss of the focused zone (its fight; nothing in a zone without one).
    const Gigaboss& gigaboss() const;
    /// 2.78: the gigaboss of the instance's lair when a player is there (else the focused zone's).
    const Gigaboss& lairGigaboss() const;
    /// 2.68: what each player did since the instance started (the same on every computer).
    const Contributions& contributions() const { return contrib_; }
    /// 2.74 (phase 6): the Cœur d'Asterion. The shared state given at this step; its routine (null
    /// outside its arena); whether its fall is validated for the event (the server set its objective in
    /// the branch's state; an administrator's test: its fall here), and whether that happened this step
    /// (every computer of the instance sees it at the same step: the victory film starts there).
    const HeartShared& heart() const { return heart_; }
    const AsterionHeartRoutine* heartRoutine() const;
    bool heartBranch() const { return heartObjective_ >= 0; }
    bool heartWon() const { return heartWon_; }
    bool heartWonNow() const { return heartWonNow_; }

    /// 2.68: an administrator's test (ADMINISTRATION › Raids › « Tester Asterion »): solo, local,
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
    /// 2.68: a co-op mechanism of the focused zone (index in rules().coops): the players on its plate
    /// now, how many it needs (its count, fewer when fewer players are in play IN THAT ZONE), done.
    struct CoopView {
        int on = 0, need = 1;
        bool done = false;
    };
    CoopView coop(size_t index) const;
    /// 2.75: the group was wiped (every player of the instance down at once, 2.78: whatever their zone):
    /// out of its zones for good, nothing steps any more (its players spectate); wipedNow(): this very
    /// step. wipedZone(): the zone of the most players then (ties: the lowest); wipedZones(): all of them.
    bool wiped() const { return wiped_; }
    bool wipedNow() const { return wipedNow_; }
    int wipedZone() const { return wiped_ ? wipedZone_ : 0; }
    const std::vector<int>& wipedZones() const { return wipedZones_; }
    uint32_t wipedTick() const { return wipedTick_; }
    /// 2.68: the last checkpoint reached in the focused zone (its players start again there), empty when none.
    const std::string& lastCheckpoint() const;

    /// 2.72: what a zone keeps of what was done in it (noted when its last player leaves it or it starts
    /// again, given back when it is loaded): object id → its state (LevelEntity::keepState), the co-op
    /// plates done, its last checkpoint.
    struct ZoneMemory {
        std::map<std::string, Json> objects;
        std::vector<uint8_t> coops;
        std::string checkpoint;
    };
    const std::map<int, ZoneMemory>& memory() const { return memory_; }

    /// Display / catch-up settings, kept for every world of the run.
    void setQuiet(bool on);
    void setCameraFollow(int index);
    int cameraFollow() const { return follow_; }

private:
    struct Event {
        std::string source, trigger;
        int who = -1;  // 2.68: the seat whose attack caused it (-1 unknown)
    };
    /// 2.78: one zone where a player of the instance is: its world and the rules of the raid around it.
    struct ZoneWorld {
        int zone = 0;
        std::unique_ptr<GameWorld> world;
        const ZoneRules* rules = nullptr;
        std::string from = "start";    // where its first players came from (it starts again there)
        uint32_t zoneTicks = 0;
        int serial = 0;
        bool magmaOn = false;
        std::vector<uint8_t> insidePassage;  // per passage: bits of the players inside it
        uint8_t inPlayBefore = 0;            // 2.77: the seats in play at the last passage check
        std::vector<uint8_t> coopDone;       // 2.68: per co-op mechanism of the zone
        std::string lastCheckpoint;          // 2.68: kept when the zone starts again
        Gigaboss gigaboss;                   // 2.68
        int bossReported = 0;                // 2.68: the highest phase of its gigaboss already reported
        std::vector<Event> events;           // of the step
        bool restarted = false;              // started again with every player gone (until one plays)
    };
    ZoneWorld* zw(int zone);
    const ZoneWorld* zw(int zone) const;
    const ZoneWorld* focused() const;
    /// 2.78: loads `zone` (its world, what it kept), every seat vacant in it but those placed after;
    /// `everyone` (the instance's first zone): every seat placed there at once, as before 2.78.
    ZoneWorld* loadZone(int zone, const std::string& from, bool everyone = false);
    /// 2.78: places seat `seat` in `z` coming from `from` (the zone's place for it), with its carry.
    void placeSeat(ZoneWorld& z, int seat, const std::string& from, const GameWorld::MemberCarry& carry);
    /// 2.78: a seat leaves its world (vacant there from now on).
    void leaveWorld(ZoneWorld& z, int seat);
    /// 2.78: the zones nobody is in any more are noted (what they keep) and unloaded.
    void unloadEmpty();
    /// 2.72: notes what a zone keeps (before it is left or started again).
    void remember(ZoneWorld& z);
    /// 2.72: gives a zone just loaded what it kept and what the branch's state says is done there.
    void restoreZone(ZoneWorld& z);
    void applyMask(uint32_t mask, bool initial);
    /// 2.78: the doors of a zone's passages follow their locks (the same step on every computer).
    void syncDoors(ZoneWorld& z, bool initial);
    /// 2.68: a link of the titan on, in a zone loaded (objects off, enemies gone, danger stopped).
    void applyEffect(ZoneWorld& z, const EffectInfo& e);
    void updateCoops(ZoneWorld& z, std::vector<std::pair<int, int>>& moves);
    void reachedCheckpoint(ZoneWorld& z, const std::string& id);
    void updateMagma(ZoneWorld& z);
    float magmaIn(const ZoneWorld& z) const;
    /// 2.78: who walked into an open passage of the zone this step: (seat, passage index).
    void checkPassages(ZoneWorld& z, std::vector<std::pair<int, int>>& moves);
    /// 2.77: a soul never waits in a passage or a door (a revival there would take the player to another
    /// zone): it is moved to the nearest safe floor of this zone, outside every one of them.
    void keepSoulsOffPassages(ZoneWorld& z);
    /// 2.77: the seats in play now in a zone (on their feet, not reappearing).
    uint8_t seatsInPlay(const ZoneWorld& z) const;
    CoopView coopOf(const ZoneWorld& z, size_t index) const;
    void completed(ZoneWorld& z, const std::string& source, const std::string& trigger, int who);
    void reportResult(int zone);
    /// 2.75: the zones finished since the last step (once each): alert, and for those a player of the
    /// instance is in, their table and their report.
    void checkFinished(bool initial);
    void alert(std::string text, Alert::Kind kind, int zone = 0, int seat = -1);
    /// 2.78: seats newly held whose zone has nobody else to join go to the zone of the lowest seat in play.
    void placeJoiners(const InputFrame* frames, int count);
    /// 2.78: the zone of each seat, 0 for a free one (Report::seats).
    std::vector<int> seatZones() const;

    WorldServices services_;
    const Branch& branch_;
    int players_;
    std::vector<std::unique_ptr<ZoneWorld>> zones_;  // 2.78: by zone id
    int seatZone_[kMaxInstancePlayers] = {};         // 2.78: the zone of each seat
    mutable int focus_ = -1;                         // 2.78: display / simulated players only
    int serials_ = 0;
    uint32_t tick_ = 0;
    uint32_t branchMask_ = 0, local_ = 0;
    bool quiet_ = false;
    int follow_ = -1;
    Contributions contrib_;      // 2.68
    uint32_t lastResult_ = UINT32_MAX;  // 2.68: the step of the last table reported
    Test test_;                         // 2.68: an administrator's test (never reported)
    HeartShared heart_;                 // 2.74 (phase 6): given with the step (RAID_STATE)
    int heartObjective_ = -1;           // 2.72: the heart's objective (its branch only)
    bool heartWon_ = false, heartWonNow_ = false;
    int heartReported_ = -1;            // 2.72: the last own damage reported, and its step
    uint32_t heartReportTick_ = 0;
    bool wiped_ = false, wipedNow_ = false;  // 2.75: the group wiped (spectators from then on)
    int wipedZone_ = 0;                       // 2.78
    std::vector<int> wipedZones_;             // 2.78
    uint32_t wipedTick_ = 0;
    std::map<int, ZoneMemory> memory_;    // 2.72: what each zone left keeps
    std::vector<Report> reports_;
    std::vector<Alert> alerts_;
    uint32_t finishedSeen_ = 0;           // 2.75: zones finished (bit = zone id)
    std::vector<int> finishedNowAll_;     // 2.78
    std::vector<ZoneFinish> finishes_;    // 2.75
    std::vector<InputFrame> scratch_;     // 2.78: the frames of a zone's world (no allocation per step)
    std::vector<int> reportedSeats_;      // 2.78: the zones of the seats last reported
    uint8_t joined_ = 0;                  // 2.78: the seats held at the last step (not vacant)
    static const Gigaboss kNoGigaboss;
};

}  // namespace cosmic::raid
