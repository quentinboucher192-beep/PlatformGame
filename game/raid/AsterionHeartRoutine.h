#pragma once

// 2.72 (phase 6): LE CŒUR D'ASTERION, the final fight of the clan raid (docs/RAIDS.md §15). A star held
// captive in a heart of flesh and crystal, floating in the middle of a gigantic arena (zone 2 of the
// heart, tools/levelgen/raid_coeur.py; data/enemies/asterion_coeur.json: four phases, its last beat), around
// the rules of raid::Gigaboss (health and pressure for the seats, phases, weak points, fall):
//
// * its health is SHARED by every instance of the event: each instance runs its own simulation, the
//   health the OTHER instances took from it (validated by their consensus, summed by the server) comes
//   with the branch's state at a step (HeartShared, RAID_STATE) and is taken from this heart at that
//   step on every computer of the instance; what this instance took itself (own(), ten-thousandths of
//   its health) is reported to the server (raid::Report::Kind::Heart). The heart falls everywhere once
//   the shared health is gone; the victory is the server's (the heart's objective in the branch's state);
// * the parts of the titan fallen act on it (the titan's links « modifier »): the Skull blinds it (its
//   star lances lose their precision), the Left arm takes its sweeps, the Right arm its magma rain, the
//   Torso breaks its cage (the way to its ventricles opens), the Abdomen its spawn, the Left leg its
//   gravity traps, the Right leg slows its charges; each relic of the branches validated weakens it;
// * « Lances stellaires » on every player in play at once (a mark that follows them, locks, a lance of
//   light falls on it); « Charges stellaires » along a storey; its own attacks over the whole arena;
// * « Les artères » (phases 2 and 3, its armour): its four artery plates, far apart (two on the floor
//   at the walls, two on high balconies the shuttles reach), held together bare its three ventricles;
// * « La couronne d'étoiles » (phase 3) and « Supernova » (phase 4): the plasma tide (the floor burns,
//   the moving platforms and the ledges are safe); in phase 4 the heart charges a supernova: enough
//   players on the anchors (the artery plates) at its end contain it (its ventricles bare), else it
//   burns the whole arena.
//
// It reads only the world, the frames' effects and the state given with them; what it shows (marks,
// lines) is display only.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

class AsterionHeartRoutine final : public GigabossRoutine {
public:
    /// The parts of the titan whose fall acts on the heart (the order of `kParts`).
    enum Part : int { Crane = 0, BrasGauche, BrasDroit, Torse, Abdomen, JambeGauche, JambeDroite, PartCount };
    static const char* partId(int p);
    /// What its fall does, as the heart's HUD and the titan's map say it.
    static const char* partEffect(int p);

    void begin(GameWorld& w, const GigabossRules& r) override;
    void step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) override;
    void fell(GameWorld& w, Gigaboss& g) override;
    uint64_t hash() const override;
    void marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const override;
    void lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const override;
    void threats(const Gigaboss& g, std::vector<Vec2>& out) const override;
    void targets(std::vector<uint32_t>& out) const override;

    /// Ten-thousandths of its health this instance took from it (what its reports carry).
    int own() const { return own_; }
    /// Ten-thousandths the other instances took (applied), and what is left of it for everyone.
    int others() const { return applied_; }
    int remaining() const { return remaining_; }
    /// The parts fallen (bit p = Part p) as read at the last step, the relics applied.
    uint8_t partsDown() const { return parts_; }
    bool down(int p) const { return (parts_ >> p) & 1u; }
    int relics() const { return relics_; }
    /// Seconds between two of its reports of `own` (the rules' heart.reportEvery).
    float reportEvery() const { return reportEvery_; }
    /// Its supernova: charging (seconds left), the anchors held now and needed.
    float novaLeft() const;
    int anchorsHeld() const { return anchorsHeld_; }
    int anchorsNeed() const { return anchorsNeed_; }
    bool tideOn() const { return tideLeft_ > 0; }
    int supernovasContained() const { return contained_; }
    /// What threatens the players now (the tests, the camera): its star lances (where, on whom,
    /// following them or not), its magma drops, its sweeps and charges, its gravity traps, its spawn.
    struct LanceView {
        Vec2 at;
        int seat = -1;
        bool tracking = false;
    };
    std::vector<LanceView> lanceViews() const {
        std::vector<LanceView> out;
        for (const Lance& l : lances_) out.push_back({l.at, l.seat, l.track > 0});
        return out;
    }
    int drops() const { return static_cast<int>(drops_.size()); }
    int sweeps() const;
    int charges() const;
    int wells() const { return static_cast<int>(wells_.size()); }
    int spawned() const { return static_cast<int>(spawned_.size()); }

private:
    struct Lance {
        Vec2 at;
        int seat = -1;
        int track = 0;  // steps it still follows its player
        int left = 0;   // steps before it falls (after tracking); <= 0: striking
        bool hit = false;
    };
    struct Band {  // a sweep or a charge running along a storey
        float y = 0.0f;   // its floor
        float x0 = 0.0f;  // where it starts
        int dir = 1;
        int warn = 0;     // steps before it runs
        int t = 0;        // steps it has run
        float speed = 0.0f, height = 52.0f, damage = 1.0f;
        bool comet = false;
        uint8_t hit = 0;
    };
    struct Drop {
        float x = 0.0f;
        int left = 0;
    };
    struct Well {
        Vec2 at;
        int left = 0;
    };

    void applyShared(GameWorld& w, Gigaboss& g, Enemy& b);
    void stepArmour(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int ph);
    void stepSupernova(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b);
    void stepLances(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int ph);
    void stepBands(GameWorld& w, Gigaboss& g, Enemy& b, int ph);
    void stepRain(GameWorld& w, Gigaboss& g, Enemy& b, int ph);
    void stepWells(GameWorld& w, Gigaboss& g, Enemy& b, int ph);
    void stepSpawns(GameWorld& w, Gigaboss& g, Enemy& b, int ph);
    void stepTide(GameWorld& w, Gigaboss& g, Enemy& b, int ph);
    float bandFront(const Band& v) const;
    int clockFor(const std::vector<float>& periods, int ph, const Enemy& b) const;
    void updateOwn(const Enemy& b);

    // Its rules (metadata.raid.gigaboss of the heart's arena).
    float relicShare_ = 0.03f, reportEvery_ = 15.0f;
    float cageExposed_ = 0.6f;
    std::string lanceName_ = "Lances stellaires", sweepName_ = "Balayage des artères", rainName_ = "Pluie de magma",
                wellName_ = "Piège de gravité", chargeName_ = "Charge stellaire", tideName_ = "Marée de plasma", novaName_ = "Supernova";
    std::vector<float> lancePeriods_, sweepPeriods_, rainPeriods_, wellPeriods_, chargePeriods_, spawnPeriods_, tidePeriods_;
    float lanceTrack_ = 1.2f, lanceWarn_ = 1.3f, lanceWidth_ = 56.0f, lanceDamage_ = 1.0f, missMin_ = 48.0f, missMax_ = 120.0f;
    float sweepWarn_ = 1.4f, sweepSpeed_ = 760.0f, sweepHeight_ = 52.0f, sweepDamage_ = 1.0f;
    std::vector<float> sweepRows_, chargeRows_;
    std::vector<int> rainCount_;
    float rainWarn_ = 1.2f, rainWidth_ = 64.0f, rainDamage_ = 1.0f;
    float wellWarn_ = 1.2f, wellTime_ = 3.0f, wellSpeed_ = 95.0f, wellRange_ = 220.0f, wellCore_ = 30.0f, wellDamage_ = 1.0f;
    float chargeWarn_ = 1.5f, chargeSpeed_ = 900.0f, chargeHeight_ = 64.0f, chargeDamage_ = 2.0f, chargeSlow_ = 0.55f;
    std::vector<std::string> spawnKinds_;
    int spawnMax_ = 8;
    float tideWarnS_ = 3.0f, tideDuration_ = 6.0f, tideHeight_ = 48.0f, tideDamage_ = 1.0f;
    float novaPeriod_ = 26.0f, novaCharge_ = 6.0f, novaDamage_ = 3.0f;
    std::vector<int> novaNeed_;
    float spanX0_ = 0.0f, spanX1_ = 4000.0f, floorY_ = 0.0f;
    std::vector<std::string> modifiers_;  // the titan's link of each part (Part order)

    // Its state.
    uint32_t ticks_ = 0;
    int phase_ = 0, players_ = 0;
    uint8_t parts_ = 0;
    int relics_ = 0;
    bool relicsApplied_ = false, cageSaid_ = false, armourSaid_ = false, novaSaid_ = false;
    int applied_ = 0, own_ = 0, remaining_ = kHeartUnitsLocal;
    std::vector<Lance> lances_;
    int lanceClock_ = 0, lanceRound_ = 0;
    std::vector<Band> bands_;
    int sweepClock_ = 0, chargeClock_ = 0, bandRound_ = 0;
    std::vector<Drop> drops_;
    int rainClock_ = 0, rainRound_ = 0;
    std::vector<Well> wells_;
    int wellClock_ = 0;
    std::vector<uint32_t> spawned_;
    int spawnClock_ = 0, spawnRound_ = 0;
    int tideClock_ = 0, tideWarn_ = 0, tideLeft_ = 0;
    int novaClock_ = 0, novaLeft_ = 0, anchorsHeld_ = 0, anchorsNeed_ = 1;
    uint8_t anchorMask_ = 0;  // the plates held now (supernova)
    int blast_ = 0;           // display: steps since its supernova burnt the arena (0: none)
    int contained_ = 0;       // supernovas contained

    static constexpr int kHeartUnitsLocal = 10000;
};

}  // namespace cosmic::raid
