#pragma once

// 2.69 (phase 4B): DREADFANG, « les griffes qui déchirent », gigaboss of the BRAS GAUCHE (docs/RAIDS.md
// §13). The giant left arm of Asterion and its blade-claws (data/enemies/dreadfang.json: its rush, its
// claws under the ice, its fan of blades, its hail, its grip, its frozen floor); around the rules of
// raid::Gigaboss (health and pressure for the seats, its « Griffes du sol » = the marks under every
// player in play, phases, weak points, fall), its routine:
//
// * « Balayage des griffes » (every phase): a blade crosses the whole arena along one, two then three
//   storeys at once (announced `warn` s before, from one wall to the other): on a swept storey, jump
//   over the blade as it passes, or change storey;
// * « Griffe clouante » (from phase `pin.from`, at least two players in play): it aims at one player
//   (two from five in play) and drives a claw down on their mark: caught, the player is nailed there
//   (hurt every `every` s, spared by its ground claws) until the others break the claw (a rescue for
//   whoever breaks it) or it draws back after `hold` s;
// * « Le bras enchaîné » (phases [carapace)): its frost armour (nothing hurts it) until its three
//   chains are held (the rules' vents: a player `hold` s on an anchor locks its chain, a locked chain
//   gives way after `reopen` s, longer with one or two players): then it is pinned to the floor, its
//   knuckle joints (weak points) bare for the rules' `exposed` seconds.

#include <memory>
#include <string>
#include <vector>

#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

class DreadfangRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override;
    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override;
    void fell(GameWorld& w, Gigaboss& g) override;
    uint64_t hash() const override;
    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;
    bool spared(int seat) const override;

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
    const std::vector<Sweep>& sweeps() const { return sweeps_; }
    const std::vector<float>& storeys() const { return storeys_; }
    bool bladeOut(const Sweep& s) const;
    float bladeX(const Sweep& s) const;
    const std::vector<Pin>& pins() const { return pins_; }
    bool chained() const { return chained_; }

private:
    void stepSweeps(GameWorld& w, Gigaboss& g, Enemy& b, int phase);
    void stepPins(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int phase);
    void watch(Contributions& c) const;

    std::vector<float> sweepPeriods_, storeys_, sweepCount_;
    float sweepWarn_ = 1.6f, sweepSpeed_ = 1500.0f, sweepHeight_ = 34.0f, sweepDamage_ = 2.0f;
    float spanX0_ = 0.0f, spanX1_ = 3200.0f;
    int pinFrom_ = 2;
    float pinPeriod_ = 15.0f, pinWarn_ = 1.2f, pinReach_ = 56.0f, pinHold_ = 8.0f, pinEvery_ = 2.5f, pinDamage_ = 1.0f;
    std::string pinEnemy_ = "dreadfang_griffe";
    int phase_ = 0;
    std::vector<Sweep> sweeps_;
    int sweepClock_ = 0, sweepVolley_ = 0;
    std::vector<Pin> pins_;
    int pinClock_ = 0, pinVolley_ = 0;
    bool guardSaid_ = false, chained_ = false;
    Vec2 chainedAt_;
};

}  // namespace cosmic::raid
