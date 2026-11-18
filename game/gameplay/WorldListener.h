#pragma once

// 2.54 (« Statistiques »): what a run tells its listener (game/records/PlayStats.h). The world
// calls these hooks while it steps; nothing they do is ever read back by the simulation (no
// return value, no state of the world changed), so a run with a listener plays exactly as a
// run without one (tests/test_play_stats.cpp checks the digests).

#include <cstdint>
#include <string>

namespace cosmic {

class Enemy;
class GameWorld;
struct DamageInfo;

/// Where a blow that reached Nova came from (« Ce qui vous touche »).
enum class HurtOrigin : uint8_t {
    EnemyShot,    // a shot of an enemy (not a boss)
    EnemyAttack,  // an attack of an enemy (blow, charge, beam…), not a boss
    BossAttack,   // anything a boss did (its blows, shots, lasting effects), contact aside
    Contact,      // touching an enemy or a boss
    Hazard,       // the level's traps and dangers (spikes, lava, crushers, turrets…)
    Fall,         // a fall into the void
    Other,        // time out, a scripted event
    Count
};

/// What the world knows about a blow or a fall when it happens.
struct HurtSource {
    HurtOrigin origin = HurtOrigin::Other;
    const Enemy* enemy = nullptr;  // the enemy whose step dealt it, or whose shot it is (may be gone: compare only)
    uint32_t shot = 0;             // the enemy shot that hit (Projectile::uid), 0 otherwise
    std::string attack;            // the enemy's attack (its name, as the boss sheet shows it); may be empty
};

/// A listener of a run (statistics). Every hook is optional.
class WorldListener {
public:
    virtual ~WorldListener() = default;
    /// A blow took health from Nova (after it was applied).
    virtual void playerHurt(const GameWorld& /*w*/, const DamageInfo& /*d*/, const HurtSource& /*src*/) {}
    /// Nova fell without a blow before it: the void, an instant hazard, the clock.
    virtual void playerKilled(const GameWorld& /*w*/, const HurtSource& /*src*/) {}
    /// An attack of Nova starts (a swing of her attack: "attack"; else the power used): its tag.
    virtual void attackStarted(const GameWorld& /*w*/, uint32_t /*tag*/, const std::string& /*power*/) {}
    /// Damage dealt to an enemy by the attack `tag` (once per blow that took health).
    virtual void enemyHit(const GameWorld& /*w*/, const Enemy& /*e*/, uint32_t /*tag*/) {}
    /// 2.67 (the raids' contributions): the same blow with the health it took (after the enemy's
    /// multipliers); `tag` 0 when no attack of a player is current.
    virtual void enemyDamaged(const GameWorld& /*w*/, const Enemy& /*e*/, uint32_t /*tag*/, float /*amount*/) {}
    /// 2.67: a blow (or a shot, reflected) stopped by the shield of the current player.
    virtual void blowBlocked(const GameWorld& /*w*/, float /*amount*/) {}
};

}  // namespace cosmic
