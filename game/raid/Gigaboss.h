#pragma once

// 2.68: the fight against a gigaboss of a raid (docs/RAIDS.md §6), phase 3: PYROCLAST, the magma
// hand of Asterion's right arm, in the Main de Pyroclast (zone 8 of « BRAS DROIT »).
//
// The gigaboss itself is an enemy of the game (data/enemies/pyroclast.json: three phases, its
// telegraphed attacks, its last stand); this class runs the raid's rules around it, step by step,
// the same on every computer of the instance (only the world and the frames are read):
//
// * its health grows with the players seated in the instance when the fight starts (and when a
//   late join takes a seat during it; never down), its cooldowns shorten with the players in play;
// * its carapace (phases [carapaceFrom, carapaceUntil)): nothing hurts it while one of its vents
//   is open. A player standing on a vent for `hold` seconds closes it; a closed vent lights again
//   after `reopen` seconds (longer with one or two players in play): the group must hold the
//   vents together. Each open vent throws a jet of magma in turn (announced, then on: a danger);
//   every vent closed at once: the carapace bursts, its weak point is bare for `exposed` seconds
//   (it takes `exposedMult` times the damage) and its weak points (enemies stuck to its knuckles)
//   can be broken, each taking `weakShare` of its health. Then the vents light again;
// * its fall: the players in play are « present at the fall ».
//
// The players' contributions (raid::Contributions) are told here what only the fight knows: the
// vents closed (who held them), the weak points broken (who broke them), the gigaboss down.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/raid/GigabossRoutine.h"
#include "game/raid/RaidBranch.h"

namespace cosmic {
class GameWorld;
class Enemy;
}  // namespace cosmic

namespace cosmic::raid {

class Contributions;

class Gigaboss {
public:
    enum class Stage : uint8_t { Absent, Waiting, Fight, Carapace, Exposed, Fallen };
    struct Vent {
        Rect area;
        int hold = 0;            // steps held toward closing it
        bool closed = false;
        uint32_t closedAt = 0;   // fight step it was closed at
        uint8_t holders = 0;     // seats standing on it this step
    };
    enum class AlertKind : uint8_t { Group, Branch, Danger };

    /// A zone was loaded. `fallen`: the gigaboss already fell (the branch's state): it is gone.
    void begin(GameWorld& w, const GigabossRules& rules, bool fallen, Contributions& c);
    /// Before the world's step: its guard (carapace, weak point bare) and its pressure.
    void before(GameWorld& w);
    /// After the world's step.
    void after(GameWorld& w, Contributions& c);

    bool present() const { return rules_ != nullptr; }
    const GigabossRules* rules() const { return rules_; }
    Stage stage() const { return stage_; }
    /// The gigaboss (nullptr before it appears and once it is gone).
    const Enemy* boss(const GameWorld& w) const;
    uint32_t bossUid() const { return uid_; }
    /// Its phase now (1-based, 0 before the fight) and the highest reached in this run.
    int phase() const { return phase_; }
    int phaseReached() const { return reached_; }
    bool engaged() const { return engaged_; }
    /// It fell during the last step (once).
    bool fellNow() const { return fellNow_; }
    const std::vector<Vent>& vents() const { return vents_; }
    int ventsClosed() const;
    float holdRatio(int k) const;
    /// Seconds before closed vent `k` lights again.
    float reopenIn(int k) const;
    /// Vent `k`'s jet: announced (warning) or throwing magma now.
    bool jetWarning(int k) const;
    bool jetting(int k) const;
    /// Seconds its weak point stays bare; its weak points left and broken in this exposure.
    float exposedLeft() const;
    int weakLeft() const;
    int weakTotal() const { return weakTotal_; }
    /// 2.69: its weak points bare now (0: broken), in the order of its rules' spots.
    const std::vector<uint32_t>& weakUids() const { return weak_; }
    /// The seats its health was scaled for, and the players in play this step.
    int scaledFor() const { return scaledFor_; }
    int inPlay() const { return inPlay_; }
    /// « Poings de magma »: the marks under the players (where a fist falls when `left` reaches 0).
    struct Fist {
        Vec2 at;       // the feet of the player marked when the volley began
        int left = 0;  // steps before it strikes (0 and below: striking)
        int seat = -1;
    };
    const std::vector<Fist>& fists() const { return fists_; }
    float fistWarnSeconds() const { return rules_ ? rules_->fistWarn : 1.0f; }
    /// The column a fist hits (world).
    Rect fistColumn(const Fist& f) const;
    /// Messages for the raid's alerts (taken by the run once a step).
    std::vector<std::pair<std::string, AlertKind>> takeAlerts();
    uint64_t hash() const;

    /// Health factor for `players` seated (1 to 8): hpScale × (1 + hpPerPlayer × (players − 1)) (titan 13: hpScale, the
    /// raid's base of its definition's health; the event's difficulty apart: setDifficulty).
    static float healthFactor(const GigabossRules& r, int players);
    /// 2.68, an administrator's test (ADMINISTRATION › Raids, never counted): its health scaled
    /// for at least `players` seated, and the fight started at phase `startPhase` (1-based).
    void setTest(int players, int startPhase) {
        testPlayers_ = players;
        testPhase_ = startPhase;
    }
    /// Titan 13: the event's difficulty (game/raid/Difficulty.h): its health and its parts' × `health`, the damage
    /// the players take in its arena × `damage`. Kept from one zone to the next, as the test's.
    void setDifficulty(float health, float damage) {
        healthScale_ = health;
        damageScale_ = damage;
    }
    float healthScale() const { return healthScale_; }
    float damageScale() const { return damageScale_; }
    /// 2.69 (phase 4B): the routine of the gigaboss (Voidworm, Graviton, Titanus), null for
    /// Pyroclast; the state of the branch at this step (the objectives its routine reads).
    const GigabossRoutine* routine() const { return routine_.get(); }
    void setBranchMask(uint32_t mask) { mask_ = mask; }
    /// 2.74 (phase 6): the shared state of the Cœur d'Asterion at this step (RAID_STATE; zero elsewhere).
    void setShared(const HeartShared& s) { shared_ = s; }
    const HeartShared& shared() const { return shared_; }
    /// 2.72: the phase to report (raid::Report::Kind::Boss): 1 to 3 while it fights (a gigaboss of four
    /// phases, the Cœur, reports its 4th as its 3rd), 4 once it fell — the server's meaning.
    int reportedPhase() const { return stage_ == Stage::Fallen ? 4 : std::min(reached_, 3); }

private:
    friend class GigabossRoutine;  // 2.69: the routines run these rules their own way
    Enemy* find(GameWorld& w);
    void alert(std::string text, AlertKind kind) { alerts_.emplace_back(std::move(text), kind); }
    void stepVents(GameWorld& w, Contributions& c, Enemy& b);
    void expose(GameWorld& w, Contributions& c, Enemy& b);
    void stepExposed(GameWorld& w, Contributions& c, Enemy& b, bool carapacePhase);
    void endExposure(GameWorld& w, Contributions& c);
    void gluWeak(GameWorld& w, const Enemy& b);
    void stepFists(GameWorld& w, Enemy& b);
    int jetCycle(int k) const;  // step of vent k in its jet cycle

    const GigabossRules* rules_ = nullptr;
    Stage stage_ = Stage::Absent;
    uint32_t uid_ = 0;
    bool engaged_ = false, fellNow_ = false;
    int phase_ = 0, reached_ = 0;
    uint32_t fightTicks_ = 0;
    int exposedTicks_ = 0;
    int scaledFor_ = 0, inPlay_ = 0;
    std::vector<Vent> vents_;
    std::vector<uint32_t> weak_;
    int weakTotal_ = 0;
    std::vector<std::pair<std::string, AlertKind>> alerts_;
    int testPlayers_ = 0, testPhase_ = 1;  // 2.68: an administrator's test only
    float healthScale_ = 1.0f, damageScale_ = 1.0f;  // titan 13: setDifficulty
    bool seen_ = false, fallen_ = false;   // the boss found in the world; it already fell (branch state)
    std::vector<Fist> fists_;
    int fistClock_ = 0;
    std::unique_ptr<GigabossRoutine> routine_;  // 2.69 (phase 4B)
    uint32_t mask_ = 0;
    HeartShared shared_;  // 2.74 (phase 6): given each step, not part of its own state
};

}  // namespace cosmic::raid
