#pragma once

// 2.69 (phase 4B): SKULLMAW, « le crâne qui voit tout », gigaboss of the CRÂNE (docs/RAIDS.md §13).
// A colossal horned skull hovering over its lair (data/enemies/skullmaw.json: its sweeping gaze, its
// psychic waves, its spiral of souls, its rain of bones, its prism, its cage of bones), and around it:
//
// * « Regards traqueurs » (every phase): a beam per player in play, from one of its eyes: it follows
//   the player (`track` s), freezes (`lock` s, brighter), then fires along that line as far as the
//   first wall (`fire` s): whoever is in it then is hit. 8 players: 8 beams, the same step;
// * « Voile psychique » (phases [veil)): nothing hurts it. Its « Regard de mort » charges (`charge` s),
//   then flashes: the players it sees (no column nor wall between its eyes and them) are hit — hide
//   behind a column — except those standing on a mirror: the mirrors throw its gaze back. Enough
//   mirrors held at the flash (`need`, by the players in play: 1 alone, 4 at 7 or 8) and it is blind:
//   its veil falls for `blind` s, it takes `blindMult` times the damage, and its two eyes and the
//   crystal of its forehead (weak points) can be put out, each taking its share of its health; it
//   sees nobody meanwhile (no gaze);
// * « Les ténèbres » (phase 3, its darkPhase): the arena darkens (display); its eye-light sweeps the
//   arena and shows the platforms it touches (the combat screen draws it from the fight's steps).

#include "game/raid/BossMechanic.h"

namespace cosmic::raid {

class SkullmawFight : public BossMechanic {
public:
    explicit SkullmawFight(const GigabossRules& rules);

    void step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) override;
    void stop(GameWorld& w, Contributions& c) override;
    bool guarded() const override { return veil_ && blindTicks_ <= 0; }
    float damageMult() const override { return blindTicks_ > 0 ? blindMult_ : 1.0f; }
    uint64_t hash() const override;
    std::string fallText(const std::string& name) const override;
    std::string goneText(const std::string& name) const override;
    std::string goneLine(const std::string& name) const override;
    std::string fallCard() const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const GameWorld& w, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;

    // ------------------------------------------------------------ what the combat screen draws
    struct Gaze {
        int seat = -1;
        int eye = 0;
        Vec2 from, aim, end;  // its eye, where it looks, where the beam stops (the first wall)
        int t = 0;            // steps since the volley began
        bool hit = false;     // its player was caught
    };
    enum class GazeState { Tracking, Locked, Firing };
    const std::vector<Gaze>& gazes() const { return gazes_; }
    GazeState gazeState(const Gaze& z) const;
    /// 0..1 through the current state (tracking, locked, firing).
    float gazeProgress(const Gaze& z) const;
    float gazeWidth() const { return width_; }
    const mech::Holds& mirrors() const { return mirrors_; }
    bool veil() const { return veil_; }
    bool charging() const { return chargeTicks_ > 0; }
    float chargeLeft() const;
    float chargeRatio() const;
    int mirrorsNeeded() const { return need_; }
    bool blind() const { return blindTicks_ > 0; }
    float blindLeft() const;
    const mech::WeakPoints& weak() const { return weak_; }
    /// Phase 3: the arena in darkness, its eye-light sweeping it.
    bool dark() const { return phase_ >= 3; }
    /// Its eye-light at fight step `t`: the angle of its middle (radians, 0 = right, π/2 = down) and its half-width.
    float searchAngle(uint32_t t) const;
    float searchSpread() const { return searchSpread_; }
    /// Where its gazes leave from (eye k), and between its eyes (its flash).
    Vec2 eyeOf(const Enemy& b, int k) const;
    Vec2 brow(const Enemy& b) const;
    /// The fight step of its last flash (0: none yet) and the mirrors held then.
    uint32_t lastFlash() const { return lastFlash_; }
    int lastHeld() const { return lastHeld_; }

private:
    void stepGazes(GameWorld& w, Enemy& b, Contributions& c, int phase);
    void flash(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c);
    void endBlind(GameWorld& w, Gigaboss& g, Contributions& c, bool veilAgain);

    // Rules (metadata.raid.gigaboss).
    std::vector<Vec2> eyes_;
    std::vector<float> gazePeriods_;
    float track_ = 1.5f, lock_ = 0.45f, fire_ = 0.35f, width_ = 18.0f, reach_ = 2400.0f, gazeDamage_ = 1.0f;
    int veilFrom_ = 2, veilUntil_ = 4;
    mech::Holds mirrors_;
    std::vector<float> flashPeriods_;
    float charge_ = 3.5f, flashDamage_ = 2.0f;
    std::vector<float> needs_;
    float blind_ = 10.0f, blindMult_ = 2.0f;
    mech::WeakPoints weak_;
    float searchPeriod_ = 7.0f, searchSpread_ = 0.32f;
    // State.
    int phase_ = 0;
    bool veil_ = false, veilSaid_ = false;
    std::vector<Gaze> gazes_;
    int gazeClock_ = 0, volleys_ = 0;
    int flashClock_ = 0, chargeTicks_ = 0, blindTicks_ = 0, need_ = 1;
    uint32_t lastFlash_ = 0;
    int lastHeld_ = 0;
};

}  // namespace cosmic::raid
