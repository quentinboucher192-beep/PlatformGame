#pragma once

// 2.79 (docs/RAIDS.md, « Joueurs simulés : terrain et anticipation »): a light copy of the player's
// movement physics, for the simulated players' lookahead and for the edges of the zone's graph.
//
// It steps a Body exactly as Player::fixedUpdate steps a player (same PlayerConfig, same order of the
// operations, the same PhysicsWorld::move): running, the jump and its held height, coyote time and the
// jump buffer, the double (and triple) jump, the wall slide and the wall jump, the crouch and the slide,
// a drop through a one-way platform, ladders, water, updrafts, the dash (on the ground, in the air, a
// dash ended by a jump: the long jump) and the impulse. What it never does: hurt, attack, shoot, touch
// an enemy — the real game decides those; a deviation from the prediction is seen and planned again.
//
// The physics it collides with is the caller's (a private copy of the zone's, game/raid/nav/Terrain,
// where the moving platforms are put where they WILL be at each step): the game's world is never
// written. The fidelity is checked against the real Player in tests/test_raid_nav.cpp.

#include <cstdint>

#include "engine/core/Math.h"
#include "game/player/Player.h"

namespace cosmic {
class PhysicsWorld;
}

namespace cosmic::raid::nav {

/// What a player can do with its powers now (read from the player: its dash, its impulse, its jumps).
struct Kit {
    int airJumps = 1;  // the double jump (+ the triple jump's extra ones)
    // The dash (data/powers/dash.json), its key's bit in InputFrame::held, -1 none.
    int dashKey = -1;
    float dashCost = 5.0f, dashCooldown = 0.45f, dashSpeed = 430.0f, dashDuration = 0.16f;
    int dashAirUses = 1;
    // The impulse (data/powers/impulse.json).
    int impulseKey = -1;
    float impulseCost = 8.0f, impulseCooldown = 0.8f, impulseVelocity = 380.0f;
    int impulseAirUses = 1;
    bool impulseFirst = false;  // the impulse comes before the dash in the player's powers (one power per step)
    bool shield = false;        // its shield is up now (plasma curtains let it through)

    static Kit of(const Player& p);
    bool hasDash() const { return dashKey >= 0; }
    bool hasImpulse() const { return impulseKey >= 0; }
};

/// The movement state the copy steps (the player's, its dash's and its impulse's; the world's around
/// it — ladder, water, updraft — set before each step by the terrain).
struct Body {
    Rect box;
    Vec2 vel;
    float facing = 1.0f;
    Vec2 aim{1.0f, 0.0f};
    PlayerState state = PlayerState::Idle;
    bool grounded = false, wasGrounded = false, jumpCut = false, crouchBox = false;
    uint32_t groundId = 0;
    int airJumpsLeft = 1, wallDir = 0;
    float coyote = 0.0f, jumpBuffer = 0.0f, wallJumpLock = 0.0f, slideTimer = 0.0f, dropThrough = 0.0f;
    float attackTimer = 0.0f, attackCooldown = 0.0f, invuln = 0.0f, hurtTimer = 0.0f, phased = 0.0f;
    float slowTimer = 0.0f, slowMultiplier = 1.0f, curseTimer = 0.0f, curseRatio = 1.0f;
    Vec2 dashDir;
    float dashSpeed = 0.0f, dashTimer = 0.0f;
    float energy = 100.0f, energyRegenTimer = 0.0f;
    // Its powers.
    float dashCool = 0.0f, impulseCool = 0.0f;
    int dashAir = 1, impulseUses = 1;
    bool dashRefill = false, dashActive = false;
    // Around it (Terrain::environment, before each step).
    bool onLadder = false, inWater = false;
    Rect ladderRect;
    float updraft = 0.0f, speedMultiplier = 1.0f, groundFriction = 1.0f;
    // What happened in the copy (never in the game): the step it landed, used a jump / a power.
    uint16_t airJumpsUsed = 0, wallJumps = 0, dashes = 0, impulses = 0;

    static Body of(const Player& p);
    Vec2 feet() const { return box.bottomCenter(); }
};

/// The movement part of an InputFrame.
struct Pad {
    uint32_t held = 0, pressed = 0;
    int8_t mx = 0, my = 0;
};

class BodySim {
public:
    BodySim(const PlayerConfig& cfg, const Kit& kit) : cfg_(cfg), kit_(kit) {}
    /// One fixed step (kSimDt) of the body against `ph` (the moving platforms where they are at that step).
    void step(Body& b, const PhysicsWorld& ph, const Pad& in) const;
    const PlayerConfig& config() const { return cfg_; }
    const Kit& kit() const { return kit_; }
    /// The options the player's moves use (Player::moveOptions; the other players never block in a raid).
    MoveOptions options(const Body& b) const;

private:
    void stepMovement(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const;
    void stepClimb(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const;
    void stepSlide(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const;
    void tryJump(Body& b, const PhysicsWorld& ph) const;
    void moveAndCollide(Body& b, const PhysicsWorld& ph, float dt) const;
    void handlePowers(Body& b, const Pad& in) const;
    void endDash(Body& b) const;
    void setHitbox(Body& b, Vec2 size) const;
    int wallDirection(const Body& b, const PhysicsWorld& ph) const;
    float curseSpeed(const Body& b) const;
    PlayerConfig cfg_;
    Kit kit_;
};

}  // namespace cosmic::raid::nav
