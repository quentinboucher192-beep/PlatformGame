#pragma once

// 2.69 (phase 4B): DREADFANG, « les griffes qui déchirent », gigaboss of the BRAS GAUCHE (docs/RAIDS.md
// §13). The giant left arm of Asterion and its blade-claws (data/enemies/dreadfang.json: its rush, its
// claws under the ice, its fan of blades, its hail, its grip, its frozen floor), and around it:
//
// * « Griffes du sol » (every phase): under each player in play, claws of ice burst out of the ground
//   (mech::Marks): 8 players, 8 claws, the same step;
// * « Balayage des griffes » (every phase): a blade crosses the whole arena along one, two then three
//   storeys at once (announced `warn` s before, from one wall to the other): on a swept storey, jump
//   over the blade as it passes, or change storey;
// * « Griffe clouante » (from phase `pin.from`, at least two players in play): it aims at one player
//   (two from five in play) and drives a claw down on their mark: caught, the player is nailed there
//   (hurt every `every` s) until the others break the claw (a rescue for whoever breaks it) or it
//   draws back after `hold` s;
// * « Le bras enchaîné » (phases [guard)): its frost armour: nothing hurts it until its three chains
//   are held — a player `hold` s on each anchor locks its chain, a locked chain gives way after
//   `reopen` s (longer with one or two players): the group holds them together — then it is pinned to
//   the floor `chained` s, takes `chainedMult` times the damage, and its knuckle joints (weak points)
//   can be broken, each taking its share of its health.

#include "game/raid/BossMechanic.h"

namespace cosmic::raid {

class DreadfangFight : public BossMechanic {
public:
    explicit DreadfangFight(const GigabossRules& rules);

    void step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) override;
    void stop(GameWorld& w, Contributions& c) override;
    bool guarded() const override { return guard_ && chainedTicks_ <= 0; }
    float damageMult() const override { return chainedTicks_ > 0 ? chainedMult_ : 1.0f; }
    uint64_t hash() const override;
    std::string fallText(const std::string& name) const override;
    std::string goneText(const std::string& name) const override;
    std::string goneLine(const std::string& name) const override;
    std::string fallCard() const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const GameWorld& w, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;

    // ------------------------------------------------------------ what the combat screen draws
    struct Sweep {
        std::vector<int> storeys;  // indices in storeys()
        int t = 0;                 // steps since it was announced
        int dir = 1;               // +1: from the west wall, -1: from the east wall
    };
    struct Pin {
        int seat = -1;
        Vec2 at;            // its mark (where the claw comes down)
        int t = 0;          // steps since it was aimed
        bool nailed = false;
        int left = 0;       // steps before the claw draws back (nailed)
        uint32_t claw = 0;  // the claw (an enemy to break)
    };
    const mech::Marks& stabs() const { return stabs_; }
    const std::vector<Sweep>& sweeps() const { return sweeps_; }
    const std::vector<float>& storeys() const { return storeys_; }
    float bandHeight() const { return sweepHeight_; }
    /// The blade's x now (nothing while it is announced).
    bool bladeOut(const Sweep& s) const;
    float bladeX(const Sweep& s) const;
    float sweepWarnLeft(const Sweep& s) const;
    float spanX0() const { return spanX0_; }
    float spanX1() const { return spanX1_; }
    const std::vector<Pin>& pins() const { return pins_; }
    float pinWarnRatio(const Pin& p) const;
    float pinReach() const { return pinReach_; }
    const mech::Holds& anchors() const { return anchors_; }
    bool guardUp() const { return guard_; }
    float holdRatio(int k) const;
    float relockIn(int k, uint32_t fightTicks) const;
    bool chained() const { return chainedTicks_ > 0; }
    float chainedLeft() const;
    const mech::WeakPoints& weak() const { return weak_; }

private:
    void stepSweeps(GameWorld& w, Gigaboss& g, Enemy& b, int phase);
    void stepPins(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c, int phase);
    void stepChains(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c);
    void syncWeak(Contributions& c) const;
    float slack() const { return inPlay_ <= 1 ? 2.0f : inPlay_ == 2 ? 1.35f : 1.0f; }

    // Rules (metadata.raid.gigaboss).
    mech::Marks stabs_;
    std::vector<float> sweepPeriods_;
    float sweepWarn_ = 1.6f, sweepSpeed_ = 1500.0f, sweepHeight_ = 34.0f, sweepDamage_ = 2.0f;
    std::vector<float> storeys_, sweepCount_;
    float spanX0_ = 0.0f, spanX1_ = 3200.0f;
    int pinFrom_ = 2;
    float pinPeriod_ = 15.0f, pinWarn_ = 1.2f, pinReach_ = 56.0f, pinHold_ = 8.0f, pinEvery_ = 2.5f, pinDamage_ = 1.0f;
    std::string pinEnemy_ = "dreadfang_griffe";
    float pinHpPerPlayer_ = 0.25f;
    int guardFrom_ = 2, guardUntil_ = 3;
    mech::Holds anchors_;
    float hold_ = 2.5f, reopen_ = 12.0f, chained_ = 10.0f, chainedMult_ = 2.0f;
    mech::WeakPoints weak_;
    // State.
    int phase_ = 0, inPlay_ = 0;
    std::vector<Sweep> sweeps_;
    int sweepClock_ = 0, sweepVolley_ = 0;
    std::vector<Pin> pins_;
    int pinClock_ = 0, pinVolley_ = 0;
    bool guard_ = false, guardSaid_ = false;
    int chainedTicks_ = 0;
    Vec2 chainedAt_;
    uint32_t ticks_ = 0;  // the fight's steps (its chains count from them)
};

}  // namespace cosmic::raid
