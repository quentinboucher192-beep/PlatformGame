#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

class GameWorld;
struct Projectile;
struct RenderContext;

/// Lasting effects of the attacks of the guardians of Aethra (2.19) on the arena. They are
/// part of the simulation (deterministic, stepped by the world after the enemies):
///  * pulls toward a point (Inspiration, Puits gravitationnel): Nova drifts toward it at a
///    speed always below her run speed (running or dashing away is enough); a well also
///    bends her shots and its core hurts;
///  * frozen ground (Lac gelé): the floors and ledges of an area lose their grip for a while;
///  * a lava tide (Marée de lave): rises to a height above the arena floor, holds, goes down;
///    below its surface Nova is hurt and thrown up;
///  * walls of thorns (Cage de ronces): solid colliders that hurt on contact;
///  * (2.50, the dragon) clouds of fire that linger where its breath or its fireballs left them
///    (they hurt inside), and the wind of its wings that pushes Nova away from it (a limited
///    push, always below her run speed: running against it still moves her forward);
///  * (2.52, the golem's quake) cracks in the ground that glow, then burst out of it, and rocks
///    falling from the roof onto marked spots; (2.56, the Phénix) columns of sunlight falling
///    from the roof onto marked spots, the same way;
///  * (2.57, the Liche) runes traced on the ground: armed after a while, a rune bursts into a
///    pillar of soul fire when Nova steps on it, and every rune left bursts at the end of its time.
/// An effect belongs to the enemy that made it (its uid): it ends with its time, with the
/// enemy (death, removal) or when the player respawns.
class BossHazards {
public:
    struct Pull {
        uint32_t owner = 0;
        Vec2 center;
        float speed = 80.0f;   // drift of the player at the centre (half of it at the edge)
        float range = 400.0f;  // no pull beyond
        float core = 0.0f;     // radius of the hurting core (0 = none)
        float damage = 0.0f;
        float time = 2.0f;     // seconds left
        float age = 0.0f;
        bool bendShots = false;  // the player's shots curve toward the centre
    };
    struct Ice {
        uint32_t owner = 0;
        Rect area;
        float time = 5.0f;
        float age = 0.0f;
        std::vector<std::pair<uint32_t, float>> saved;  // collider id, its own grip
        std::vector<Rect> tops;                         // frozen surfaces (drawn)
    };
    struct Flood {
        uint32_t owner = 0;
        float left = 0.0f, right = 0.0f;  // horizontal extent
        float floorY = 0.0f;              // arena floor
        float height = 64.0f;             // highest surface above the floor
        float rise = 1.0f, hold = 4.0f, fall = 1.0f;
        float damage = 2.0f;
        float age = 0.0f;
        std::string look;  // display: "" = lava, "plasma" = star plasma (2.33), "tide" = dark water (2.53)
        Color color{122, 230, 255, 255};  // display: the light of its crests ("tide": the boss's phase)
        /// Surface height above the floor (0 .. height) at the current age.
        float level() const;
        float surfaceY() const { return floorY - level(); }
        bool done() const { return age >= rise + hold + fall; }
    };
    struct Wall {
        uint32_t owner = 0;
        Rect rect;
        uint32_t collider = 0;
        float time = 4.0f;
        float age = 0.0f;
        float damage = 1.0f;
        std::string look;  // display: "" = thorns, "star" = starlight (2.33), "tentacle" = the Kraken's (2.53), "bone" = the Liche's (2.57)
        float side = 1.0f;                // display: the cage's side of it (a tentacle curls over it)
        Color color{122, 230, 255, 255};  // display: the light of its suckers ("tentacle")
    };

    struct Cloud {
        uint32_t owner = 0;
        Rect area;             // its whole extent
        float originX = 0.0f;  // it spreads from here to the two ends of its area
        float spread = 0.0f;   // seconds to cover its area (0 = at once)
        float time = 3.0f;     // seconds left
        float age = 0.0f;
        float damage = 1.0f;
        bool ground = false;   // display: a carpet of fire along the ground (else a puff)
        Color color{255, 122, 46, 255};  // display: the fire of the phase
        bool ink = false;                // display (2.53, the Kraken): a cloud of ink lit by the phase
        bool ash = false;                // display (2.56, the Phénix): a cloud of ash, embers glowing in it
        float emitTimer = 0.0f;          // display: its smoke
        /// The part of its area reached at its age.
        Rect current() const;
        /// It hurts after a short flare and until its last quarter second (it fades harmless).
        bool burning() const { return age >= 0.1f && time > 0.25f; }
    };
    /// A crack of the golem's quake (2.52): marked on the ground, it bursts after `delay` (stone
    /// spikes up to `height` for `burst` seconds); or a rock falling from the roof (`rock`): its
    /// mark on the ground until it lands there after `delay`.
    struct Crack {
        uint32_t owner = 0;
        Vec2 at;                 // on the ground: its middle (x) and the surface (y)
        float half = 22.0f;      // half its width (a rock: its radius)
        float height = 46.0f;    // how high the burst reaches (a rock: its impact)
        float delay = 0.75f;     // seconds before it bursts (a rock: before it lands)
        float burst = 0.3f;      // the burst hurts this long
        float age = 0.0f;
        float damage = 1.0f;
        float knockback = 220.0f;
        bool rock = false;
        float fromY = 0.0f;      // a rock: where it falls from (the roof)
        bool hit = false;        // it has hurt (once)
        Color color{255, 179, 71, 255};  // display: the light of the phase
        /// 2.56, the Phénix's « Colonnes solaires »: a column of sunlight from the roof (`fromY`)
        /// down to its mark; it burns `burst` seconds over its whole `height` (display: a beam
        /// of light, no stone; the hurting is a crack's).
        bool sun = false;
        bool bursting() const { return age >= delay; }
        bool done() const { return age >= delay + burst + (rock ? 0.35f : 0.25f); }
        /// A rock: the centre of the falling stone at its age (it speeds up).
        Vec2 rockAt() const;
    };
    /// 2.57, a rune of the Liche's « Runes funestes »: traced on the ground (`at`: its middle, on the
    /// surface), it glows faintly for `arm` seconds (it cannot hurt yet), then it is armed for
    /// `time` seconds: Nova's feet on it make it burst; at the end of its time it bursts on its own
    /// (it flickers kWarn seconds before). Its burst: a pillar of soul fire `height` tall for
    /// kBurst seconds, which hurts once and throws her up.
    struct Rune {
        static constexpr float kWarn = 0.6f;
        static constexpr float kBurst = 0.35f;
        uint32_t owner = 0;
        Vec2 at;
        float half = 18.0f;     // half its width
        float height = 56.0f;   // its pillar
        float arm = 0.8f;
        float time = 4.0f;
        float age = 0.0f;
        float burstAge = -1.0f;  // >= 0: bursting for this long
        float damage = 1.0f;
        float knockback = 260.0f;
        bool hit = false;
        int glyph = 0;           // display: which signs are traced in it
        Color color{150, 245, 110, 255};  // display: the soul fire of the phase
        bool armed() const { return burstAge < 0.0f && age >= arm; }
        bool bursting() const { return burstAge >= 0.0f; }
        bool done() const { return burstAge >= kBurst + 0.25f; }
        /// What her feet touch (a strip on the surface) and the pillar of its burst.
        Rect pad() const { return {at.x - half, at.y - 6.0f, half * 2.0f, 8.0f}; }
        Rect pillar() const { return {at.x - half * 0.85f, at.y - height, half * 1.7f, height}; }
    };
    struct Wind {
        uint32_t owner = 0;
        Vec2 center;           // Nova is pushed away from it, horizontally
        float speed = 100.0f;  // drift at the centre (half of it at the edge of its range)
        float range = 900.0f;
        float time = 2.0f;
        float age = 0.0f;
    };

    void addPull(const Pull& p);
    void addCloud(const Cloud& c);
    void addWind(const Wind& wd);
    void addCrack(const Crack& c);
    void addRune(const Rune& r);
    /// Freezes the floors, ledges and platforms whose top lies in `area` (grip < 1).
    void addIce(GameWorld& w, uint32_t owner, const Rect& area, float grip, float seconds);
    void addFlood(const Flood& f);
    /// A solid wall; Nova standing where it rises is pushed to the `inside` side (+1: right).
    void addWall(GameWorld& w, uint32_t owner, const Rect& rect, float seconds, float damage, float inside,
                 const std::string& look = {}, Color color = Color{122, 230, 255, 255});
    /// Ends what an enemy channels (pulls and walls); the ice and the lava keep their time.
    void endChannel(GameWorld& w, uint32_t owner);
    /// Ends every effect of an enemy (it died or left the world).
    void removeOwner(GameWorld& w, uint32_t owner);
    void clear(GameWorld& w);
    void fixedUpdate(GameWorld& w, float dt);
    /// The player's shots near a well curve toward it (turn rate bounded, speed kept).
    void bendShot(Projectile& p, float dt) const;

    bool empty() const {
        return pulls_.empty() && ice_.empty() && floods_.empty() && walls_.empty() && clouds_.empty() && winds_.empty() &&
               cracks_.empty() && runes_.empty();
    }
    bool hasIce(uint32_t owner) const;
    bool hasFlood(uint32_t owner) const;
    bool hasPull(uint32_t owner) const;
    bool hasWind(uint32_t owner) const;
    int cloudCount(uint32_t owner) const;
    int crackCount(uint32_t owner) const;
    int runeCount(uint32_t owner) const;
    const std::vector<Pull>& pulls() const { return pulls_; }
    const std::vector<Ice>& ice() const { return ice_; }
    const std::vector<Flood>& floods() const { return floods_; }
    const std::vector<Wall>& walls() const { return walls_; }
    const std::vector<Cloud>& clouds() const { return clouds_; }
    const std::vector<Wind>& winds() const { return winds_; }
    const std::vector<Crack>& cracks() const { return cracks_; }
    const std::vector<Rune>& runes() const { return runes_; }

    /// Display only.
    void render(RenderContext& ctx) const;

private:
    void restore(GameWorld& w, Ice& ice);
    std::vector<Pull> pulls_;
    std::vector<Ice> ice_;
    std::vector<Flood> floods_;
    std::vector<Wall> walls_;
    std::vector<Cloud> clouds_;
    std::vector<Wind> winds_;
    std::vector<Crack> cracks_;
    std::vector<Rune> runes_;
};

}  // namespace cosmic
