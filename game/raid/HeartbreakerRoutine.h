#pragma once

// 2.69 (phase 4B): HEARTBREAKER, « la cage qui garde le cœur », gigaboss of the TORSE (docs/RAIDS.md
// §13). The armoured rib cage of Asterion standing in the middle of its lair (data/enemies/
// heartbreaker.json: its heart waves, its spiral of blood, its burning clots, its crown, its
// eruptions, its tide of plasma); around the rules of raid::Gigaboss (health and pressure for the
// seats, its « Côtes-pièges » = the marks under every player in play, phases, weak points, fall):
//
// * « La cuirasse » (phase 1): its armour (nothing hurts it) while one of its four armour plates
//   stands — the plates of the Sternum and the Lower ribs that the groups broke on the way, grown
//   again: two high (shot from the flank towers), two low. They grow back unless all four fall within
//   `window` s of the first (longer with few players): several groups, together. Then it is gone;
// * « Battements » (every phase): a beat every period, faster at each phase; every Nth beat is a
//   strong one: a wave runs along the floor from both its sides to the walls — jump over it;
// * « La cage thoracique » (phases [carapace)): its ribs close over the heart (its armour) until its two
//   ribs (the rules' vents, from the two flank towers) are pried open at the same time (each held
//   `hold` s, a rib let go closes again within `reopen` s): the light of its heart (its weak point) is
//   bare for the rules' `exposed` seconds.

#include <memory>
#include <string>
#include <vector>

#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

class HeartbreakerRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override;
    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override;
    void fell(GameWorld& w, Gigaboss& g) override;
    uint64_t hash() const override;
    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;

    struct Wave {
        float x0 = 0.0f;
        int dir = 1;
        int t = 0;
        uint8_t hit = 0;  // the players it already hit
    };
    /// Its four plates (0: broken), whether they still guard it, the seconds before broken ones grow back.
    const std::vector<uint32_t>& plates() const { return plates_; }
    int platesLeft() const;
    bool plated() const { return plated_; }
    float windowLeft() const;
    int beats() const { return beats_; }
    int strongEvery(int phase) const;
    const std::vector<Wave>& waves() const { return waves_; }
    float waveFront(const Wave& v) const;

private:
    void stepPlates(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b);
    void spawnPlates(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b);
    void stepBeats(GameWorld& w, Enemy& b, int phase);
    float slack4(int n) const { return n <= 1 ? 2.0f : n == 2 ? 1.5f : n <= 4 ? 1.2f : 1.0f; }

    std::vector<Vec2> plateSpots_;
    std::string plateEnemy_ = "heartbreaker_plaque";
    float window_ = 20.0f, plateShare_ = 0.02f;
    std::vector<float> beatPeriods_, strong_, from_;
    float waveSpeed_ = 520.0f, waveHeight_ = 40.0f, waveDamage_ = 1.0f, floorY_ = 0.0f;
    float spanX0_ = 0.0f, spanX1_ = 3200.0f;
    int phase_ = 0, players_ = 0;
    std::vector<uint32_t> plates_;
    bool plated_ = true, platesUp_ = false, cageSaid_ = false;
    int windowTicks_ = 0, regrown_ = 0;
    int beatClock_ = 0, beats_ = 0;
    uint32_t lastBeat_ = 0, ticks_ = 0;
    bool lastStrong_ = false;
    std::vector<Wave> waves_;
};

}  // namespace cosmic::raid
