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
///  * walls of thorns (Cage de ronces): solid colliders that hurt on contact.
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
        std::string look;  // display: "" = lava, "plasma" = star plasma (2.33)
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
        std::string look;  // display: "" = thorns, "star" = starlight (2.33)
    };

    void addPull(const Pull& p);
    /// Freezes the floors, ledges and platforms whose top lies in `area` (grip < 1).
    void addIce(GameWorld& w, uint32_t owner, const Rect& area, float grip, float seconds);
    void addFlood(const Flood& f);
    /// A solid wall; Nova standing where it rises is pushed to the `inside` side (+1: right).
    void addWall(GameWorld& w, uint32_t owner, const Rect& rect, float seconds, float damage, float inside,
                 const std::string& look = {});
    /// Ends what an enemy channels (pulls and walls); the ice and the lava keep their time.
    void endChannel(GameWorld& w, uint32_t owner);
    /// Ends every effect of an enemy (it died or left the world).
    void removeOwner(GameWorld& w, uint32_t owner);
    void clear(GameWorld& w);
    void fixedUpdate(GameWorld& w, float dt);
    /// The player's shots near a well curve toward it (turn rate bounded, speed kept).
    void bendShot(Projectile& p, float dt) const;

    bool empty() const { return pulls_.empty() && ice_.empty() && floods_.empty() && walls_.empty(); }
    bool hasIce(uint32_t owner) const;
    bool hasFlood(uint32_t owner) const;
    bool hasPull(uint32_t owner) const;
    const std::vector<Pull>& pulls() const { return pulls_; }
    const std::vector<Ice>& ice() const { return ice_; }
    const std::vector<Flood>& floods() const { return floods_; }
    const std::vector<Wall>& walls() const { return walls_; }

    /// Display only.
    void render(RenderContext& ctx) const;

private:
    void restore(GameWorld& w, Ice& ice);
    std::vector<Pull> pulls_;
    std::vector<Ice> ice_;
    std::vector<Flood> floods_;
    std::vector<Wall> walls_;
};

}  // namespace cosmic
