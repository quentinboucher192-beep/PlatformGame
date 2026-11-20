#pragma once

// 2.68: what each player of a raid instance did (docs/RAIDS.md §6), computed INSIDE the
// simulation: every game of the instance steps the same frames, so every game counts the same
// numbers and reports the same table to the server (its consensus decides, as for the
// objectives). Nothing here reads the clock, the camera or the network.
//
// A record per player who held a seat: a seat taken again by someone else (after 60 s of
// absence, the seat freed) starts a new record from that step (`from`), which the server
// matches with the member the relay wrote for that seat (raid_instance_members.seat_from).
//
// Categories (never only the damage, docs/RAIDS.md §7): damage dealt (to any enemy, and to the
// gigaboss with its weak points), damage avoided (the gigaboss's blows aimed at the player and
// dodged, the blows their shield stopped), support (shield blocks, allies revived), mechanisms
// (vents closed), weak points broken, objectives of the branch, steps in play, and whether they
// were in play when the gigaboss fell.
//
// 2.78: the players of an instance may be in several zones at once (one world per zone occupied,
// raid::RaidRun): the records and the zones' tables are the instance's, what follows a world (the
// owners of its attacks, its gigaboss, its weak points) is kept per world (`attach`, `focus`). The
// zones' tables count each player's deaths (times down), the revivals they did and those they got.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/gameplay/WorldListener.h"

namespace cosmic {
class GameWorld;
class Enemy;
struct AttackDef;
}  // namespace cosmic

namespace cosmic::raid {

struct Contribution {
    int seat = 0;
    uint32_t from = 0;        // the step this player took the seat
    int64_t damage = 0;       // tenths of health taken from enemies (the gigaboss included)
    int64_t bossDamage = 0;   // tenths taken from the gigaboss (its weak points included)
    int64_t avoided = 0;      // tenths: the gigaboss's blows aimed at them and dodged, the blows their shield stopped
    int blocks = 0;           // blows and shots stopped by their shield
    int rescues = 0;          // allies revived
    int vents = 0;            // vents of the gigaboss closed (mechanisms)
    int weakPoints = 0;       // weak points of the gigaboss broken
    int objectives = 0;       // objectives of the branch accomplished
    uint32_t ticks = 0;       // steps in play (on their feet or down, not absent)
    bool gigaboss = false;    // in play when the gigaboss fell
};

/// 2.75: what one player did in ONE zone of the branch (its « Tableau des contributions » at the end
/// of the zone, docs/RAIDS.md §19): the damage dealt to the zone's boss and mini-bosses (its guardians,
/// the enemies whose fall is an objective, its gigaboss and weak points), the damage dealt in the zone,
/// support (allies revived, blows blocked), mechanisms (plates held when they worked, vents, weak
/// points) and objectives. Counted inside the simulation like the records: every game agrees.
/// 2.78: `miniDamage` (the part of `bossDamage` dealt to the mini-bosses: the enemies whose fall is an
/// objective of the zone, never its gigaboss nor a boss of the bestiary), `deaths` (times down in the
/// zone), `revives` (allies revived: the one holding « Interagir » by the soul when it got up) and
/// `revived` (times revived). Absent from the tables of before 2.78 (`v2` false): shown « — ».
struct ZoneContribution {
    int seat = 0;
    uint32_t from = 0;
    int64_t bossDamage = 0;  // tenths
    int64_t damage = 0;      // tenths
    int support = 0, mechanisms = 0, objectives = 0;
    int64_t miniDamage = 0;  // 2.78: tenths (part of bossDamage)
    int deaths = 0, revives = 0, revived = 0;  // 2.78
    bool v2 = true;  // 2.78: the four fields above are known (a table of before reads false)
};

class Contributions : public WorldListener {
public:
    /// 2.78: a world of the instance (a zone loaded): its zone, the object ids of its boss and mini-bosses
    /// (the sources of its « killed » objectives, its gigaboss) and of its mini-bosses alone (the
    /// « killed » objectives). It becomes the focused one; its attacks' tags start again.
    void attach(GameWorld& w, int zone, std::vector<std::string> bosses, std::vector<std::string> minis);
    /// 2.78: a world unloaded (its zone left by every player).
    void detach(const GameWorld& w);
    /// 2.78: the world the next calls without a world are about (its gigaboss, its weak points, its zone).
    void focus(const GameWorld& w);
    /// 2.75: the table of a zone (one line per record that did something there, by seat then step).
    std::vector<ZoneContribution> zoneTable(int zone) const;
    /// 2.78: the whole raid (« Raid entier »): every zone's lines added up, per record.
    std::vector<ZoneContribution> raidTable() const;
    /// 2.78: the zones with a table (their ids, in order).
    std::vector<int> zonesWithTable() const;
    /// 2.75: « s,f,b,d,u,m,o;… » of zoneTable(zone): what the server's consensus compares; 2.78:
    /// « s,f,b,d,u,m,o,n,x,y,z;… » (mini-bosses, deaths, revivals done and received).
    static std::string zoneCanonical(const std::vector<ZoneContribution>& rows);
    /// 2.78: the seats of the instance after a step, each in its own zone's world: its presence
    /// there (Vacant when nowhere), its zone and the ally that revived it this step (-1 none). New
    /// holders of a seat, steps in play, deaths and revivals (done and received).
    struct SeatView {
        uint8_t presence = 4;  // GameWorld::Presence (4: Vacant)
        int zone = 0;
        int reviver = -1;
    };
    void seats(uint32_t tick, const SeatView* views, int n);
    /// After each step of a world (step `tick`): the allies revived there (whoever stood by the soul),
    /// the gigaboss's blows dodged. The seats not in that world are Vacant in it (ignored).
    void afterStep(const GameWorld& w, uint32_t tick, const Enemy* boss);
    /// The record of the player in seat `seat` now (nullptr when nobody holds it).
    Contribution* current(int seat);
    const std::vector<Contribution>& records() const { return records_; }
    /// The seat whose attack `tag` is (-1 unknown), in the focused world.
    int ownerOf(uint32_t tag) const;
    int ownerOf(const GameWorld& w, uint32_t tag) const;
    /// An objective accomplished: by `seat` when known, else by every player in play in `w`.
    void objective(const GameWorld& w, int seat);
    void vent(int seat);
    void weakPoint(int seat);
    void gigabossDown(const GameWorld& w);
    /// The uid of the gigaboss (its damage, its blows), and of its weak points (focused world).
    void setBoss(uint32_t uid);
    void setWeak(const std::vector<uint32_t>& uids);
    /// 2.69 (phase 4B): other targets of a gigaboss's mechanisms whose breaker counts (Voidworm's
    /// stomach, Titanus's restraints, Graviton's cores): brokeWeak() names who broke them.
    void setWatched(const std::vector<uint32_t>& uids);
    /// 2.69: an ally freed (Voidworm's stomach broken, Dreadfang's claw broken): a rescue for `seat`.
    void rescue(int seat);
    /// 2.69: a blow of the gigaboss aimed at `seat` and dodged (a gaze left in time, a flash hidden
    /// from, a claw stepped away from), `amount` in health.
    void dodged(int seat, float amount);
    /// The seat that broke weak point `uid` this step (-1 none), in the focused world.
    int brokeWeak(uint32_t uid) const;
    uint64_t hash() const;
    /// « s,f,d,b,a,k,r,v,w,o,t,g;… » sorted by seat then step: what the server's consensus compares.
    std::string canonical() const;

    // WorldListener
    void attackStarted(const GameWorld& w, uint32_t tag, const std::string& power) override;
    void enemyDamaged(const GameWorld& w, const Enemy& e, uint32_t tag, float amount) override;
    void blowBlocked(const GameWorld& w, float amount) override;
    void playerHurt(const GameWorld& w, const DamageInfo& d, const HurtSource& src) override;

private:
    /// 2.78: what follows one world of the instance.
    struct Scope {
        const GameWorld* world = nullptr;
        int zone = 0;
        std::vector<std::string> bosses, minis;
        std::map<uint32_t, int> tagOwner;
        uint32_t boss = 0;
        std::vector<uint32_t> weak, watched;
        // Weak points broken this step, and by whom (2.69: kept until the gigaboss's rules read them,
        // after Contributions::afterStep; the entries of an older step are dropped at the next one).
        std::vector<std::pair<uint32_t, int>> broken;
        size_t brokenSeen = 0;
        // The gigaboss's attack being followed: its target and whether it was hit.
        const AttackDef* attack = nullptr;
        bool prevTelegraph = false;
        int target = -1;
        bool targetHit = false;
        int64_t attackDamage = 0;
        bool hurtThisStep[8] = {};
        int revivedSeen[8] = {};
    };
    Scope* scopeOf(const GameWorld& w);
    const Scope* scopeOf(const GameWorld& w) const;
    /// The line of `seat`'s record in `zone`'s table (nullptr when nobody holds it).
    ZoneContribution* zoneLine(int zone, int seat);
    std::vector<Contribution> records_;
    std::map<int, std::vector<ZoneContribution>> zones_;  // 2.75: by zone, by record index
    int seatRecord_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool wasVacant_[8] = {true, true, true, true, true, true, true, true};
    uint8_t prevPresence_[8] = {4, 4, 4, 4, 4, 4, 4, 4};  // 2.78: GameWorld::Presence after the last step
    std::vector<std::unique_ptr<Scope>> scopes_;  // 2.78: by world
    Scope* focus_ = nullptr;
};

}  // namespace cosmic::raid
