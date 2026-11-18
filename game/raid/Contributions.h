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

#include <cstdint>
#include <map>
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

class Contributions : public WorldListener {
public:
    /// A new world (a zone loaded): the tags of the players' attacks start again.
    void attach(GameWorld& w);
    /// After each step of the world (step `tick`): new holders of a seat, steps in play, allies
    /// revived (whoever stood by the soul), the gigaboss's blows dodged.
    void afterStep(const GameWorld& w, uint32_t tick, const Enemy* boss);
    /// The record of the player in seat `seat` now (nullptr when nobody holds it).
    Contribution* current(int seat);
    const std::vector<Contribution>& records() const { return records_; }
    /// The seat whose attack `tag` is (-1 unknown).
    int ownerOf(uint32_t tag) const;
    /// An objective accomplished: by `seat` when known, else by every player in play.
    void objective(const GameWorld& w, int seat);
    void vent(int seat);
    void weakPoint(int seat);
    void gigabossDown(const GameWorld& w);
    /// The uid of the gigaboss (its damage, its blows), and of its weak points.
    void setBoss(uint32_t uid) { boss_ = uid; }
    void setWeak(const std::vector<uint32_t>& uids) { weak_ = uids; }
    /// 2.69 (phase 4B): other targets of a gigaboss's mechanisms whose breaker counts (Voidworm's
    /// stomach, Titanus's restraints, Graviton's cores): brokeWeak() names who broke them.
    void setWatched(const std::vector<uint32_t>& uids) { watched_ = uids; }
    /// 2.69: an ally freed (Voidworm's stomach broken): a rescue for `seat`.
    void rescue(int seat);
    /// The seat that broke weak point `uid` this step (-1 none).
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
    std::vector<Contribution> records_;
    int seatRecord_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    bool wasVacant_[8] = {true, true, true, true, true, true, true, true};
    int revivedSeen_[8] = {};
    std::map<uint32_t, int> tagOwner_;
    uint32_t boss_ = 0;
    std::vector<uint32_t> weak_;
    std::vector<uint32_t> watched_;  // 2.69
    std::vector<std::pair<uint32_t, int>> broken_;  // weak points broken this step, and by whom
    // The gigaboss's attack being followed: its target and whether it was hit.
    const AttackDef* attack_ = nullptr;
    bool prevTelegraph_ = false;
    int target_ = -1;
    bool targetHit_ = false;
    int64_t attackDamage_ = 0;
    bool hurtThisStep_[8] = {};
};

}  // namespace cosmic::raid
