#pragma once

// 2.69 (phase 4B): HEARTBREAKER, « la cage qui garde le cœur », gigaboss of the TORSE (docs/RAIDS.md
// §13). The armoured rib cage of Asterion standing in the middle of its lair (data/enemies/
// heartbreaker.json: its heart waves, its spiral of blood, its burning clots, its crown, its
// eruptions, its tide of plasma), and around it:
//
// * « La cuirasse » (phase 1): nothing hurts it while one of its four armour plates stands (the
//   plates of the Sternum and the Lower ribs that the groups broke on the way, grown again: two high,
//   shot from the flank towers, two low, from the floor). The plates grow back unless all four fall
//   within `window` s of the first (longer with few players): several groups, together. Then the
//   armour is gone for good;
// * « Battements » (every phase): a beat every period, faster at each phase; every Nth beat is a
//   strong one: a wave runs along the floor from both its sides to the walls — jump over it;
// * « Côtes-pièges » (every phase): ribs rise on both sides of each player in play and snap shut
//   (mech::Marks): 8 players, 8 traps;
// * « La cage thoracique » (phases [guard)): its ribs close over the heart: nothing hurts it until
//   its two ribs are held open at the same time, one player at each (`hold` s, from the two flank
//   towers; one alone holds one): then the heart is bare `exposed` s, it takes `exposedMult` times
//   the damage and the light of its heart (a weak point) can be struck, taking its share of its health.

#include "game/raid/BossMechanic.h"

namespace cosmic::raid {

class HeartbreakerFight : public BossMechanic {
public:
    explicit HeartbreakerFight(const GigabossRules& rules);

    void step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) override;
    void stop(GameWorld& w, Contributions& c) override;
    bool guarded() const override { return (phase_ <= 1 && armoured_) || (cage_ && exposedTicks_ <= 0); }
    float damageMult() const override { return exposedTicks_ > 0 ? exposedMult_ : 1.0f; }
    uint64_t hash() const override;
    std::string fallText(const std::string& name) const override;
    std::string goneText(const std::string& name) const override;
    std::string goneLine(const std::string& name) const override;
    std::string fallCard() const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const GameWorld& w, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;

    // ------------------------------------------------------------ what the combat screen draws
    struct Wave {
        float x0 = 0.0f;
        int dir = 1;
        int t = 0;
        uint8_t hit = 0;  // the players it already hit
    };
    const mech::WeakPoints& plates() const { return plates_; }
    bool armoured() const { return armoured_; }
    float windowLeft() const;
    /// Its beats: how many so far, the fight step of the last one (0: none yet) and whether it was strong.
    int beats() const { return beats_; }
    uint32_t lastBeat() const { return lastBeat_; }
    bool lastStrong() const { return lastStrong_; }
    int strongEvery(int phase) const;
    const std::vector<Wave>& waves() const { return waves_; }
    float waveFront(const Wave& v) const;
    float waveHeight() const { return waveHeight_; }
    float floorY() const { return floorY_; }
    const mech::Marks& traps() const { return traps_; }
    const mech::Holds& ribs() const { return ribs_; }
    bool cage() const { return cage_; }
    float pryRatio() const;
    int ribsNeeded() const { return need_; }
    bool exposed() const { return exposedTicks_ > 0; }
    float exposedLeft() const;
    const mech::WeakPoints& heart() const { return heart_; }

private:
    void stepPlates(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c);
    void stepBeats(GameWorld& w, Enemy& b, int phase, uint32_t ticks);
    void stepCage(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c);
    void syncWeak(Contributions& c) const;
    float slack() const { return inPlay_ <= 1 ? 2.0f : inPlay_ == 2 ? 1.5f : inPlay_ <= 4 ? 1.2f : 1.0f; }

    // Rules (metadata.raid.gigaboss).
    mech::WeakPoints plates_;
    std::vector<std::string> plateNames_;
    float window_ = 20.0f;
    std::vector<float> beatPeriods_, strong_;
    float waveSpeed_ = 520.0f, waveHeight_ = 40.0f, waveDamage_ = 1.0f, floorY_ = 0.0f;
    std::vector<float> from_;
    float spanX0_ = 0.0f, spanX1_ = 3200.0f;
    mech::Marks traps_;
    int guardFrom_ = 2, guardUntil_ = 3;
    mech::Holds ribs_;
    float hold_ = 2.5f, exposed_ = 8.0f, exposedMult_ = 2.5f;
    mech::WeakPoints heart_;
    // State.
    int phase_ = 0, inPlay_ = 0, need_ = 1;
    bool armoured_ = true, platesUp_ = false;
    int windowTicks_ = 0, regrown_ = 0;
    int beatClock_ = 0, beats_ = 0;
    uint32_t lastBeat_ = 0;
    bool lastStrong_ = false;
    std::vector<Wave> waves_;
    bool cage_ = false, cageSaid_ = false;
    int pry_ = 0, exposedTicks_ = 0;
};

}  // namespace cosmic::raid
