#pragma once

// 2.79 (docs/RAIDS.md §26): what the simulated players of a raid test share between their senses, their
// tactics against each Gigaboss, their roles in the group, their orders and their thoughts.
//
// * a Danger is what they see coming (read from the world only: the marks a player sees, the hazards, the
//   enemies' shots, a gigaboss's routine): where it strikes, how (an area to leave, a line to step out of, a
//   wave along a floor to jump, a liquid to climb above, a sector whose gravity turns over, a flash to hide
//   from) and in how many steps;
// * a Station is a place to hold (a vent, a mirror, a chain's anchor, a generator, a rib, an artery): how many
//   players it needs and whether it must be held all the time or only when asked;
// * a Target is what to strike (a weak point, a restraint, a claw, a stomach, a plate…), how many players and
//   whether those striking it must finish it together with the other targets of its group (a « sync »);
// * an Order comes from the administrator's frame (InputFrame, docs/RAIDS.md §26), never from the screen:
//   the same frames give the same raid.
//
// Nothing here reads the clock, the screen or the network.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"

namespace cosmic::raid::bots {

/// A simulated player's place in the group (given by the group's plan, changed when someone falls).
enum class Role : uint8_t { None = 0, Lure, Striker, Reviver, Runner };
/// « Appât », « Frappeur », « Réanimateur », « Coureur » (French: the display translates it).
const char* roleName(Role r);

struct Danger {
    enum class Kind : uint8_t {
        Area,       // a blow on an area (a mark, a shot's path, a column): leave it
        Line,       // a beam along a line (from `from` to `to`, `width`): step out of it
        FloorWave,  // a wave running along a floor (`floorY`, its front at area's centre, `speed` signed): jump it
        Flood,      // a liquid rising over the area: get above its top
        Sector,     // gravity turning over in the area: leave it or stand under a slab
        Pull,       // a well pulling toward `from`: run away from it
        Ring,       // a ring growing from `from` (its radius now, its speed), one opening: its shield as it comes
    };
    Kind kind = Kind::Area;
    Rect area;
    int in = 0;          // steps before it strikes (0: striking now)
    Vec2 from, to;       // Line: its ends; Pull: its core
    float width = 0.0f;  // Line: its thickness
    float speed = 0.0f;  // FloorWave: px/s along x (signed)
    float floorY = 0.0f; // FloorWave: the floor it runs on
    float height = 0.0f; // FloorWave: how high it rises over the floor
    bool shot = false;   // an enemy's projectile (the shield stops it)
    int seat = -1;       // only this seat (a mark aimed at one player), -1: everyone
    bool body = false;   // the boss's own body (it hurts on contact)
    float radius = 0.0f, reach = 0.0f;  // Ring: its radius now, the farthest it goes
    float gap = 0.0f, gapHalf = 0.0f;   // Ring: the angle of its opening and its half-width (radians)
};

struct Station {
    Rect plate;
    int need = 1;            // players on it
    int priority = 0;        // the higher first
    bool always = true;      // held all the time (else only when `now`)
    bool now = true;         // needed at this step
    std::string label;       // « Miroir haut est »
};

struct Target {
    uint32_t uid = 0;
    int want = 1;            // players on it
    int priority = 0;        // the higher first
    bool weak = false;       // a weak point (× 2): the strikers' first choice
    int sync = 0;            // > 0: its group (finished together with the others of that group)
    float holdAt = 0.0f;     // sync: stop striking under this share of its health until all are there
    bool melee = true;       // struck from close (no contact damage)
    std::string label;       // « Entrave ouest »
};

/// What the administrator orders (the order wheel of a test, docs/RAIDS.md §26).
enum class OrderKind : uint8_t { None = 0, Follow, Wait, Objective, Revive, Free, Mark };
const char* orderName(OrderKind k);  // « Suivez-moi », « Attendez ici »…

struct Order {
    OrderKind kind = OrderKind::None;
    Vec2 at;                 // where (the administrator's feet, the place marked, the soul to raise)
    uint32_t tick = 0;       // the step it was given
};

/// An order rides on the administrator's frame of a step: InputFrame::flags bit kOrderFlag set, the order and
/// its place packed in choice / moveX / moveY (TestSession reads it and gives the world a clean frame). A
/// real raid never sees one (its relay never sets this bit; the bots exist only in a test).
constexpr uint8_t kOrderFlag = 8;
/// The frame `f` carrying order `k` at `at` (4 px precision, 0..8188 × 0..4092).
InputFrame withOrder(InputFrame f, OrderKind k, Vec2 at);
/// The order a frame carries (false: none).
bool orderOf(const InputFrame& f, Order& out);
/// The frame without its order (what the world steps with).
InputFrame withoutOrder(InputFrame f);

/// What a simulated player thinks now (display: the bubbles over it and the raid menu's « Groupe »).
struct Thought {
    Role role = Role::None;
    std::string intent;  // « réanime Joueur simulé 3 », « frappe le point faible (× 2) »
    std::string move;    // how it goes there (the navigation's intention): « saute vers la plateforme »
    bool order = false;  // it obeys an order of the administrator
};

}  // namespace cosmic::raid::bots
