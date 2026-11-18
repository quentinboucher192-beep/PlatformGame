#pragma once

// 2.69 (phase 4B): SKULLMAW, « le crâne qui voit tout », gigaboss of the CRÂNE (docs/RAIDS.md §13).
// A colossal horned skull hovering over its lair (data/enemies/skullmaw.json: its sweeping gaze, its
// psychic waves, its spiral of souls, its rain of bones, its prism, its cage of bones); around the
// rules of raid::Gigaboss (health and pressure for the seats, phases, weak points, fall), its routine:
//
// * « Regards traqueurs » (every phase): a beam per player in play, from one of its eyes: it follows
//   the player (`track` s), freezes (`lock` s, brighter), then fires along that line as far as the
//   first wall (`fire` s): whoever is in it then is hit. 8 players: 8 beams, the same step;
// * « Voile psychique » (phases [veil)): its armour (nothing hurts it). Its « Regard de mort » charges
//   (`charge` s), then flashes: the players it sees (no column nor wall between its eyes and them)
//   are hit — hide behind a column — except those standing on a mirror, which throw its gaze back.
//   Enough mirrors held at the flash (`need`, by the players in play: 1 alone, 4 at 7 or 8) and it is
//   blind: its weak points (its two eyes, the crystal of its brow) bare for the rules' `exposed`
//   seconds; it sees nobody meanwhile (no gaze);
// * « Les ténèbres » (phase 3, its darkPhase): the arena darkens (display); its eye-light sweeps the
//   arena and shows the platforms it touches (a mark « Cone » drawn by the combat screen).

#include <memory>
#include <string>
#include <vector>

#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

class SkullmawRoutine final : public GigabossRoutine {
public:
    void begin(GameWorld& w, const GigabossRules& r) override;
    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override;
    void fell(GameWorld& w, Gigaboss& g) override;
    uint64_t hash() const override;
    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override;

    struct Gaze {
        int seat = -1;
        int eye = 0;
        Vec2 from, aim, end;  // its eye, where it looks, where the beam stops (the first wall)
        int t = 0;            // steps since the volley began
        bool hit = false;     // its player was caught
    };
    enum class GazeState { Tracking, Locked, Firing };
    struct Mirror {
        Rect area;
        uint8_t holders = 0;
    };
    const std::vector<Gaze>& gazes() const { return gazes_; }
    GazeState gazeState(const Gaze& z) const;
    float gazeProgress(const Gaze& z) const;
    const std::vector<Mirror>& mirrors() const { return mirrors_; }
    int held() const;
    bool veil() const { return veil_; }
    bool charging() const { return chargeTicks_ > 0; }
    float chargeRatio() const;
    int mirrorsNeeded() const { return need_; }
    /// Fight step of its last flash (0: none yet) and the mirrors held then.
    uint32_t lastFlash() const { return lastFlash_; }
    int lastHeld() const { return lastHeld_; }
    bool dark() const { return phase_ >= 3; }
    /// Its eye-light at fight step `t`: the angle of its middle (radians, π/2 = down).
    float searchAngle(uint32_t t) const;
    Vec2 eyeOf(const Enemy& b, int k) const;
    Vec2 brow(const Enemy& b) const;

private:
    void stepGazes(GameWorld& w, Contributions& c, Enemy& b, int phase, bool blind);
    void flash(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b);

    std::vector<Vec2> eyes_;
    std::vector<float> gazePeriods_;
    float track_ = 1.5f, lock_ = 0.45f, fire_ = 0.35f, width_ = 18.0f, reach_ = 2400.0f, gazeDamage_ = 1.0f;
    int veilFrom_ = 2, veilUntil_ = 4;
    std::vector<Mirror> mirrors_;
    std::vector<float> flashPeriods_, needs_;
    float charge_ = 3.5f, flashDamage_ = 2.0f;
    float searchPeriod_ = 7.0f, searchSpread_ = 0.32f;
    int phase_ = 0;
    bool veil_ = false, veilSaid_ = false;
    std::vector<Gaze> gazes_;
    int gazeClock_ = 0, volleys_ = 0, flashClock_ = 0, chargeTicks_ = 0, need_ = 1;
    uint32_t lastFlash_ = 0, ticks_ = 0;
    int lastHeld_ = 0;
};

}  // namespace cosmic::raid
