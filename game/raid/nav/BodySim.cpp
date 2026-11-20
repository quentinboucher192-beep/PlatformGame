#include "game/raid/nav/BodySim.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/PhysicsWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/powers/Powers.h"

namespace cosmic::raid::nav {

namespace {

bool isHeld(const Pad& in, Action a) { return (in.held >> static_cast<int>(a)) & 1u; }
bool isPressed(const Pad& in, Action a) { return (in.pressed >> static_cast<int>(a)) & 1u; }
bool keyPressed(const Pad& in, int bit) { return bit >= 0 && ((in.pressed >> bit) & 1u); }
Vec2 moveOf(const Pad& in) { return {static_cast<float>(in.mx) / 127.0f, static_cast<float>(in.my) / 127.0f}; }

int bitOf(const Power& p) {
    const int k = static_cast<int>(p.key());
    return k >= 0 && k < InputFrame::kActions ? k : -1;
}

}  // namespace

// ----------------------------------------------------------------------------- what it can do

Kit Kit::of(const Player& p) {
    Kit k;
    k.airJumps = p.airJumpCount();
    k.shield = p.shieldActive();
    int extraDashes = 0;
    for (const auto& pw : p.powers())
        if (const auto* ad = dynamic_cast<const AirDashPower*>(pw.get())) extraDashes += ad->extraDashes();
    int order = 0, dashAt = -1, impulseAt = -1;
    for (const auto& pw : p.powers()) {
        if (const auto* d = dynamic_cast<const DashPower*>(pw.get()); d && k.dashKey < 0) {
            k.dashKey = bitOf(*d);
            k.dashCost = d->def().energyCost;
            k.dashCooldown = d->def().cooldown;
            k.dashSpeed = d->def().param("speed", 430.0f);
            k.dashDuration = d->def().param("duration", 0.16f);
            k.dashAirUses = static_cast<int>(d->def().param("airUses", 1)) + extraDashes;
            dashAt = order;
        } else if (const auto* i = dynamic_cast<const ImpulsePower*>(pw.get()); i && k.impulseKey < 0) {
            k.impulseKey = bitOf(*i);
            k.impulseCost = i->def().energyCost;
            k.impulseCooldown = i->def().cooldown;
            k.impulseVelocity = i->def().param("velocity", 380.0f);
            k.impulseAirUses = static_cast<int>(i->def().param("airUses", 1));
            impulseAt = order;
        }
        ++order;
    }
    k.impulseFirst = impulseAt >= 0 && (dashAt < 0 || impulseAt < dashAt);
    return k;
}

Body Body::of(const Player& p) {
    Body b;
    const Player::Motion m = p.motion();
    b.box = p.box;
    b.vel = p.vel;
    b.facing = p.facing;
    b.aim = p.aimDirection();
    b.state = m.state;
    b.grounded = m.grounded;
    b.wasGrounded = m.grounded;
    b.jumpCut = m.jumpCut;
    b.crouchBox = m.crouchBox;
    b.groundId = m.groundId;
    b.airJumpsLeft = m.airJumpsLeft;
    b.wallDir = m.wallDir;
    b.coyote = m.coyote;
    b.jumpBuffer = m.jumpBuffer;
    b.wallJumpLock = m.wallJumpLock;
    b.slideTimer = m.slideTimer;
    b.dropThrough = m.dropThrough;
    b.attackTimer = m.attackTimer;
    b.attackCooldown = m.attackCooldown;
    b.invuln = m.invuln;
    b.hurtTimer = m.hurtTimer;
    b.phased = m.phased;
    b.slowTimer = m.slowTimer;
    b.slowMultiplier = m.slowMultiplier;
    b.curseTimer = m.curseTimer;
    b.curseRatio = m.curseRatio;
    b.dashDir = m.dashDir;
    b.dashSpeed = m.dashSpeed;
    b.dashTimer = m.dashTimer;
    b.energy = p.energy;
    b.energyRegenTimer = m.energyRegenTimer;
    for (const auto& pw : p.powers()) {
        if (const auto* d = dynamic_cast<const DashPower*>(pw.get())) {
            b.dashCool = d->cooldownRemaining();
            b.dashAir = d->airUsesLeft();
            b.dashRefill = d->refillPending();
            b.dashActive = d->dashFlag();
        } else if (const auto* i = dynamic_cast<const ImpulsePower*>(pw.get())) {
            b.impulseCool = i->cooldownRemaining();
            b.impulseUses = i->usesLeft();
        }
    }
    b.onLadder = p.onLadder;
    b.ladderRect = p.ladderRect;
    b.inWater = p.inWater;
    b.updraft = p.updraft;
    b.speedMultiplier = p.speedMultiplier;
    b.groundFriction = p.groundFriction;
    return b;
}

// ----------------------------------------------------------------------------- the copy of the physics

MoveOptions BodySim::options(const Body& b) const {
    MoveOptions o;
    o.dropThrough = b.dropThrough > 0.0f;
    o.ignoreTags = ColliderTag::Teammate;  // a raid: the players pass through each other (GameWorld::teamBodies)
    if (kit_.shield) o.ignoreTags |= ColliderTag::ShieldPass;
    return o;
}

float BodySim::curseSpeed(const Body& b) const { return b.curseTimer > 0.0f ? std::sqrt(b.curseRatio) : 1.0f; }

void BodySim::setHitbox(Body& b, Vec2 size) const {
    Vec2 feetPos = b.box.bottomCenter();
    b.box = Rect::fromBottomCenter(feetPos, size);
}

int BodySim::wallDirection(const Body& b, const PhysicsWorld& ph) const {
    MoveOptions o = options(b);
    o.ignoreTags |= ColliderTag::Teammate;
    Rect r = b.box.translated({1.5f, 0.0f});
    r.y += 2;
    r.h -= 6;
    if (ph.overlapsSolid(r, o)) return 1;
    Rect l = b.box.translated({-1.5f, 0.0f});
    l.y += 2;
    l.h -= 6;
    if (ph.overlapsSolid(l, o)) return -1;
    return 0;
}

void BodySim::endDash(Body& b) const {
    if (b.state != PlayerState::Dash) return;
    b.state = b.grounded ? PlayerState::Idle : PlayerState::Fall;
    b.vel.x = b.dashDir.x * std::min(b.dashSpeed * 0.45f, cfg_.maxRunSpeed);
    b.vel.y = b.dashDir.y < -0.1f ? b.dashDir.y * b.dashSpeed * 0.35f : std::max(0.0f, b.vel.y * 0.3f);
}

void BodySim::handlePowers(Body& b, const Pad& in) const {
    // Player::handlePowers: on a ladder only the dash; one power per step, in the order of its powers.
    if (b.state == PlayerState::Climb && !keyPressed(in, kit_.dashKey) && !isPressed(in, Action::Dash) && !isPressed(in, Action::Interact)) return;
    auto tryDash = [&]() -> bool {
        if (!keyPressed(in, kit_.dashKey)) return false;
        if (!(b.dashCool <= 0.0f && b.energy + 0.001f >= kit_.dashCost)) return false;
        if (b.state == PlayerState::Dash || !(b.grounded || b.dashAir > 0)) return false;  // DashPower::canActivate
        if (b.state == PlayerState::Climb || b.state == PlayerState::Slide) {
            setHitbox(b, cfg_.size);
            b.state = PlayerState::Fall;
        }
        if (b.energy + 0.001f >= kit_.dashCost) {
            b.energy -= kit_.dashCost;
            b.energyRegenTimer = cfg_.energyRegenDelay;
        }
        b.dashCool = kit_.dashCooldown;
        // DashPower::onActivate.
        Vec2 dir = b.aim;
        if (b.grounded && dir.y > 0.1f) dir = {b.facing, 0.0f};
        if (dir.lengthSq() < 0.01f) dir = {b.facing, 0.0f};
        if (!b.grounded) --b.dashAir;
        // Player::beginDash.
        if (b.state == PlayerState::Slide || b.state == PlayerState::Climb) setHitbox(b, cfg_.size);
        b.state = PlayerState::Dash;
        b.dashDir = dir.normalized();
        b.dashSpeed = kit_.dashSpeed;
        b.dashTimer = kit_.dashDuration;
        b.vel = b.dashDir * kit_.dashSpeed;
        if (dir.x != 0.0f) b.facing = signf(dir.x);
        b.invuln = std::max(b.invuln, kit_.dashDuration + 0.05f);
        b.dashActive = true;
        ++b.dashes;
        return true;
    };
    auto tryImpulse = [&]() -> bool {
        if (b.state == PlayerState::Climb) return false;
        if (!keyPressed(in, kit_.impulseKey)) return false;
        if (!(b.impulseCool <= 0.0f && b.energy + 0.001f >= kit_.impulseCost)) return false;
        if (!(b.grounded || b.impulseUses > 0)) return false;  // ImpulsePower::canActivate
        if (b.state == PlayerState::Slide) {
            setHitbox(b, cfg_.size);
            b.state = PlayerState::Fall;
        }
        if (b.energy + 0.001f >= kit_.impulseCost) {
            b.energy -= kit_.impulseCost;
            b.energyRegenTimer = cfg_.energyRegenDelay;
        }
        b.impulseCool = kit_.impulseCooldown;
        if (!b.grounded) --b.impulseUses;
        // Player::launch({vel.x * 0.8, -v}, false).
        b.vel = {b.vel.x * 0.8f, -kit_.impulseVelocity};
        b.jumpCut = false;
        b.grounded = false;
        b.coyote = 0.0f;
        if (b.state == PlayerState::Slide || b.state == PlayerState::Climb || b.state == PlayerState::WallSlide) {
            setHitbox(b, cfg_.size);
            b.state = PlayerState::Jump;
        }
        ++b.impulses;
        return true;
    };
    if (kit_.impulseFirst) {
        if (!tryImpulse()) tryDash();
    } else {
        if (!tryDash()) tryImpulse();
    }
}

void BodySim::step(Body& b, const PhysicsWorld& ph, const Pad& in) const {
    const float dt = kSimDt;
    auto tick = [dt](float& t) { t = std::max(0.0f, t - dt); };
    tick(b.invuln);
    tick(b.coyote);
    tick(b.jumpBuffer);
    tick(b.wallJumpLock);
    tick(b.attackCooldown);
    tick(b.dropThrough);
    tick(b.phased);
    tick(b.slowTimer);
    tick(b.curseTimer);
    if (b.state == PlayerState::Dead || b.state == PlayerState::Hidden || b.state == PlayerState::Respawn) return;
    if (b.crouchBox && b.state != PlayerState::Crouch) {
        Rect stand = Rect::fromBottomCenter(b.box.bottomCenter(), cfg_.size);
        if (!ph.overlapsSolid(stand, options(b))) {
            setHitbox(b, cfg_.size);
            b.crouchBox = false;
        }
    }
    if (b.energyRegenTimer > 0)
        b.energyRegenTimer -= dt;
    else
        b.energy = std::min(cfg_.maxEnergy, b.energy + cfg_.energyRegen * dt);
    const Vec2 move = moveOf(in);
    if (std::abs(move.x) > 0.3f || std::abs(move.y) > 0.3f) {
        Vec2 a{std::abs(move.x) > 0.3f ? signf(move.x) : 0.0f, std::abs(move.y) > 0.5f ? signf(move.y) : 0.0f};
        b.aim = a.normalized();
    } else {
        b.aim = {b.facing, 0.0f};
    }
    // The powers' own step (Power::fixedUpdate: cooldowns; DashPower: the refill after a landing).
    b.dashCool = std::max(0.0f, b.dashCool - dt);
    if (b.dashRefill) {
        b.dashRefill = false;
        b.dashAir = kit_.dashAirUses;
    }
    if (b.dashActive && b.state != PlayerState::Dash) b.dashActive = false;
    b.impulseCool = std::max(0.0f, b.impulseCool - dt);
    if (b.state == PlayerState::Hurt) {
        b.hurtTimer -= dt;
        b.vel.y = std::min(b.vel.y + cfg_.gravity() * dt, cfg_.maxFallSpeed);
        b.vel.x = approach(b.vel.x, 0.0f, 500.0f * dt);
        moveAndCollide(b, ph, dt);
        if (b.hurtTimer <= 0.0f) b.state = b.grounded ? PlayerState::Idle : PlayerState::Fall;
        return;
    }
    if (isPressed(in, Action::Jump)) b.jumpBuffer = cfg_.jumpBuffer;
    handlePowers(b, in);
    if (b.state == PlayerState::Dash) {
        b.dashTimer -= dt;
        b.vel = b.dashDir * b.dashSpeed * b.speedMultiplier;
        moveAndCollide(b, ph, dt);
        if (b.dashTimer <= 0.0f) endDash(b);
        // A buffered jump during a ground dash becomes a long jump.
        if (b.state == PlayerState::Dash && b.grounded && b.jumpBuffer > 0 && std::abs(b.dashDir.y) < 0.1f) {
            endDash(b);
            tryJump(b, ph);
        }
        return;
    }
    // Player::handleAttack: an attack under way runs out (the copy never starts one).
    if (b.attackTimer > 0.0f) b.attackTimer -= dt;
    if (b.state == PlayerState::Climb) {
        stepClimb(b, ph, in, dt);
        return;
    }
    if (b.state == PlayerState::Slide) {
        stepSlide(b, ph, in, dt);
        return;
    }
    stepMovement(b, ph, in, dt);
}

void BodySim::stepMovement(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const {
    const Vec2 move = moveOf(in);
    float mx = move.x;
    // Enter climbing
    if (b.onLadder && (move.y < -0.5f || (move.y > 0.5f && !b.grounded)) && b.attackTimer <= 0.0f) {
        b.state = PlayerState::Climb;
        b.vel = {};
        b.box.x = std::clamp(b.box.x, b.ladderRect.x - b.box.w * 0.25f, b.ladderRect.right() - b.box.w * 0.75f);
        b.airJumpsLeft = kit_.airJumps;
        return;
    }
    // Enter slide (down while running on the ground)
    if (b.grounded && isPressed(in, Action::MoveDown) && std::abs(b.vel.x) > cfg_.maxRunSpeed * 0.55f && !b.inWater) {
        const Collider* ground = ph.groundBelow(b.box, options(b));
        bool onOneWay = ground && ground->kind == ColliderKind::OneWay;
        if (!onOneWay) {
            b.state = PlayerState::Slide;
            b.slideTimer = cfg_.slideDuration;
            b.facing = signf(b.vel.x);
            b.vel.x = b.facing * cfg_.slideSpeed;
            setHitbox(b, cfg_.slideSize);
            return;
        }
    }
    // Crouch
    {
        bool down = move.y > 0.5f && std::abs(mx) < 0.35f;
        bool canCrouch = b.grounded && down && !b.inWater && !b.onLadder && b.attackTimer <= 0.0f && b.jumpBuffer <= 0.0f;
        if (b.state == PlayerState::Crouch) {
            bool stay = canCrouch;
            if (!stay) {
                Rect stand = Rect::fromBottomCenter(b.box.bottomCenter(), cfg_.size);
                if (!ph.overlapsSolid(stand, options(b))) {
                    setHitbox(b, cfg_.size);
                    b.crouchBox = false;
                    b.state = PlayerState::Idle;
                } else if (b.grounded) {
                    stay = true;
                    b.jumpBuffer = 0.0f;
                }
            }
            if (stay) {
                b.vel.x = approach(b.vel.x, 0.0f, cfg_.groundDeceleration * dt);
                b.vel.y = std::min(b.vel.y + cfg_.gravity() * dt, cfg_.maxFallSpeed);
                moveAndCollide(b, ph, dt);
                return;
            }
        } else if (canCrouch && std::abs(b.vel.x) <= cfg_.maxRunSpeed * 0.55f) {
            b.state = PlayerState::Crouch;
            setHitbox(b, {cfg_.size.x, std::max(8.0f, std::round(cfg_.size.y * 0.58f))});
            b.crouchBox = true;
            b.vel.x = approach(b.vel.x, 0.0f, cfg_.groundDeceleration * dt);
            moveAndCollide(b, ph, dt);
            return;
        }
    }
    // Horizontal
    float maxSpeed = cfg_.maxRunSpeed;
    float target = mx * maxSpeed;
    if (std::abs(mx) > 0.05f && std::abs(mx) < 0.6f) target = signf(mx) * std::max(cfg_.walkSpeed * std::abs(mx) / 0.6f, 20.0f);
    if (b.attackTimer > 0.0f && b.grounded) target *= 0.35f;
    target *= b.speedMultiplier;
    if (b.slowTimer > 0.0f) target *= b.slowMultiplier;
    if (b.inWater) target *= 0.7f;
    float accel;
    if (b.grounded) {
        if (std::abs(target) < 0.01f)
            accel = cfg_.groundDeceleration;
        else if (b.vel.x != 0.0f && signf(target) != signf(b.vel.x))
            accel = cfg_.turnAcceleration;
        else
            accel = std::abs(target) > std::abs(b.vel.x) ? cfg_.groundAcceleration : cfg_.groundDeceleration;
        if (b.groundFriction < 0.999f) accel *= std::max(0.08f, b.groundFriction);
    } else {
        accel = (std::abs(target) > 0.01f ? cfg_.airAcceleration : cfg_.airDeceleration) * cfg_.airControl;
        if (b.wallJumpLock > 0.0f) accel *= 0.15f;
    }
    b.vel.x = approach(b.vel.x, target, accel * dt);
    if (std::abs(mx) > 0.1f && b.attackTimer <= 0.0f && b.wallJumpLock <= 0.0f) b.facing = signf(mx);

    // Gravity
    float g = cfg_.gravity();
    if (b.vel.y > 0.0f)
        g *= cfg_.fallGravityMultiplier;
    else if (std::abs(b.vel.y) < cfg_.apexThreshold && isHeld(in, Action::Jump))
        g *= cfg_.apexGravityMultiplier;
    float maxFall = move.y > 0.5f ? cfg_.fastFallSpeed : cfg_.maxFallSpeed;
    if (b.inWater) {
        g *= cfg_.waterGravityMultiplier;
        maxFall = 70.0f;
        if (isHeld(in, Action::Jump) || move.y < -0.5f) b.vel.y = approach(b.vel.y, -cfg_.swimSpeed * 0.8f, 600.0f * dt);
    }
    b.vel.y = std::min(b.vel.y + g * dt, maxFall);
    if (b.updraft > 0.0f && !b.inWater) {
        if (b.vel.y > 70.0f) b.vel.y = approach(b.vel.y, 70.0f, 900.0f * dt);
    }

    // Variable jump height
    if (b.jumpCut && !isHeld(in, Action::Jump) && b.vel.y < 0.0f) {
        b.vel.y *= cfg_.jumpCutMultiplier;
        b.jumpCut = false;
    }
    if (b.vel.y >= 0.0f) b.jumpCut = false;

    // Wall slide
    b.wallDir = b.grounded ? 0 : wallDirection(b, ph);
    bool pushingWall = b.wallDir != 0 && signf(mx) == static_cast<float>(b.wallDir);
    if (!b.grounded && b.wallDir != 0 && b.vel.y > 0.0f && pushingWall && !b.inWater) {
        if (b.state != PlayerState::WallSlide) b.airJumpsLeft = kit_.airJumps;
        b.state = PlayerState::WallSlide;
        b.vel.y = std::min(b.vel.y, cfg_.wallSlideSpeed);
        b.facing = static_cast<float>(b.wallDir);
    } else if (b.state == PlayerState::WallSlide) {
        b.state = PlayerState::Fall;
    }

    // Drop through one-way platforms: Down + Jump
    if (b.grounded && b.jumpBuffer > 0.0f && move.y > 0.5f) {
        const Collider* ground = ph.groundBelow(b.box, options(b));
        if (ground && ground->kind == ColliderKind::OneWay) {
            b.dropThrough = 0.22f;
            b.jumpBuffer = 0.0f;
            b.grounded = false;
            b.box.y += 1.0f;
        }
    }

    tryJump(b, ph);
    moveAndCollide(b, ph, dt);

    if (b.state != PlayerState::WallSlide && b.state != PlayerState::Dash) {
        if (b.grounded)
            b.state = std::abs(b.vel.x) > 8.0f ? PlayerState::Run : PlayerState::Idle;
        else
            b.state = b.inWater ? PlayerState::Swim : (b.vel.y < 0 ? PlayerState::Jump : PlayerState::Fall);
    }
}

void BodySim::tryJump(Body& b, const PhysicsWorld& ph) const {
    if (b.jumpBuffer <= 0.0f) return;
    if (b.grounded || b.coyote > 0.0f) {
        b.vel.y = -cfg_.jumpVelocity() * curseSpeed(b);
        if (b.state == PlayerState::Slide) {
            setHitbox(b, cfg_.size);
            b.vel.x = b.facing * std::max(std::abs(b.vel.x), cfg_.maxRunSpeed * 1.1f);
        }
        b.grounded = false;
        b.coyote = 0.0f;
        b.jumpBuffer = 0.0f;
        b.jumpCut = true;
        b.state = PlayerState::Jump;
        return;
    }
    int wd = b.state == PlayerState::WallSlide ? b.wallDir : (b.vel.y >= -20.0f ? wallDirection(b, ph) : 0);
    if (wd != 0 && !b.inWater && (b.state == PlayerState::WallSlide || b.airJumpsLeft == 0 || b.vel.y > 40.0f)) {
        b.vel = {-static_cast<float>(wd) * cfg_.wallJumpVelocity.x, cfg_.wallJumpVelocity.y * curseSpeed(b)};
        b.facing = -static_cast<float>(wd);
        b.wallJumpLock = cfg_.wallJumpLock;
        b.jumpBuffer = 0.0f;
        b.jumpCut = true;
        b.state = PlayerState::Jump;
        ++b.wallJumps;
        return;
    }
    if (b.inWater) {
        b.vel.y = -cfg_.swimSpeed * 1.5f;
        b.jumpBuffer = 0.0f;
        return;
    }
    if (b.airJumpsLeft > 0) {
        --b.airJumpsLeft;
        b.vel.y = -cfg_.doubleJumpVelocity() * curseSpeed(b);
        b.jumpBuffer = 0.0f;
        b.jumpCut = true;
        b.state = PlayerState::Jump;
        ++b.airJumpsUsed;
    }
}

void BodySim::stepClimb(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const {
    if (!b.onLadder) {
        b.state = PlayerState::Fall;
        return;
    }
    const Vec2 move = moveOf(in);
    b.vel.x = move.x * cfg_.climbSpeed * 0.6f;
    b.vel.y = move.y * cfg_.climbSpeed;
    if (std::abs(move.x) > 0.3f) b.facing = signf(move.x);
    if (b.jumpBuffer > 0.0f) {
        b.jumpBuffer = 0.0f;
        b.state = PlayerState::Jump;
        b.vel.y = -cfg_.jumpVelocity() * 0.85f;
        b.vel.x = move.x * cfg_.maxRunSpeed;
        b.jumpCut = true;
        return;
    }
    moveAndCollide(b, ph, dt);
    if (b.box.center().x < b.ladderRect.x || b.box.center().x > b.ladderRect.right()) b.state = PlayerState::Fall;
    if (b.grounded && move.y > 0.3f) b.state = PlayerState::Idle;
}

void BodySim::stepSlide(Body& b, const PhysicsWorld& ph, const Pad& in, float dt) const {
    (void)in;
    b.slideTimer -= dt;
    b.vel.x = approach(b.vel.x, b.facing * cfg_.maxRunSpeed * 0.3f, 380.0f * dt);
    b.vel.y = std::min(b.vel.y + cfg_.gravity() * cfg_.fallGravityMultiplier * dt, cfg_.maxFallSpeed);
    if (b.jumpBuffer > 0.0f && (b.grounded || b.coyote > 0)) {
        Rect stand = Rect::fromBottomCenter(b.box.bottomCenter(), cfg_.size);
        if (!ph.overlapsSolid(stand, options(b))) {
            tryJump(b, ph);
            return;
        }
    }
    moveAndCollide(b, ph, dt);
    bool endTime = b.slideTimer <= 0.0f || !b.grounded || std::abs(b.vel.x) < 30.0f;
    if (endTime) {
        Rect stand = Rect::fromBottomCenter(b.box.bottomCenter(), cfg_.size);
        if (!ph.overlapsSolid(stand, options(b))) {
            setHitbox(b, cfg_.size);
            b.state = b.grounded ? PlayerState::Run : PlayerState::Fall;
        } else {
            b.slideTimer = 0.05f;
            b.vel.x = b.facing * std::max(std::abs(b.vel.x), 60.0f);
        }
    }
}

void BodySim::moveAndCollide(Body& b, const PhysicsWorld& ph, float dt) const {
    b.wasGrounded = b.grounded;
    MoveOptions opt = options(b);
    Vec2 delta = b.vel * dt;
    float preVy = b.vel.y;
    MoveResult res = ph.move(b.box, delta, opt);
    // Ceiling corner correction: slide around ledges when a jump barely clips them.
    if (res.hitTop && preVy < 0.0f) {
        for (int nudge = 1; nudge <= 5; ++nudge) {
            bool fixed = false;
            for (float dir : {-1.0f, 1.0f}) {
                Rect test = b.box.translated({dir * nudge, delta.y});
                if (!ph.overlapsSolid(test, opt)) {
                    b.box = test;
                    res.hitTop = false;
                    fixed = true;
                    break;
                }
            }
            if (fixed) break;
        }
        if (res.hitTop) {
            b.vel.y = 0.0f;
            b.jumpCut = false;
        }
    }
    if (res.hitLeft || res.hitRight) {
        if (b.state != PlayerState::Dash) b.vel.x = 0.0f;
    }
    auto landed = [&]() {
        b.airJumpsLeft = kit_.airJumps;  // Player::onLanded: restoreAirJumps, the powers' onLand
        b.dashRefill = true;
        b.impulseUses = kit_.impulseAirUses;
    };
    if (res.hitBottom) {
        b.grounded = true;
        b.groundId = res.groundId;
        if (b.vel.y > 0.0f) b.vel.y = 0.0f;
        if (!b.wasGrounded) landed();
    } else if (b.vel.y >= 0.0f) {
        const Collider* gr = ph.groundBelow(b.box, opt);
        b.grounded = gr != nullptr;
        b.groundId = gr ? gr->id : 0;
        if (b.grounded && !b.wasGrounded) landed();
    } else {
        b.grounded = false;
        b.groundId = 0;
    }
    b.groundFriction = 1.0f;
    if (b.grounded && b.groundId)
        if (const Collider* gr = ph.get(b.groundId)) b.groundFriction = gr->friction;
    if (b.wasGrounded && !b.grounded && b.vel.y >= 0.0f) b.coyote = cfg_.coyoteTime;
    if (b.grounded) {
        b.airJumpsLeft = kit_.airJumps;
        b.jumpCut = b.vel.y < 0.0f && b.jumpCut;
    }
}

}  // namespace cosmic::raid::nav
