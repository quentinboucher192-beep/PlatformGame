#include "game/player/Player.h"

#include <cmath>

#include "game/gameplay/GameWorld.h"

namespace cosmic {

const char* playerStateName(PlayerState s) {
    switch (s) {
        case PlayerState::Idle: return "Idle";
        case PlayerState::Run: return "Run";
        case PlayerState::Jump: return "Jump";
        case PlayerState::Fall: return "Fall";
        case PlayerState::WallSlide: return "WallSlide";
        case PlayerState::Climb: return "Climb";
        case PlayerState::Slide: return "Slide";
        case PlayerState::Dash: return "Dash";
        case PlayerState::Swim: return "Swim";
        case PlayerState::Hurt: return "Hurt";
        case PlayerState::Dead: return "Dead";
        case PlayerState::Respawn: return "Respawn";
        case PlayerState::Victory: return "Victory";
        case PlayerState::Hidden: return "Hidden";
    }
    return "?";
}

void Player::configure(const PlayerConfig& cfg, AnimationSetPtr animSet) {
    cfg_ = cfg;
    anim.setSet(std::move(animSet));
    health = cfg_.maxHealth;
    energy = cfg_.maxEnergy;
    airJumpsLeft_ = cfg_.airJumps;
}

void Player::spawn(GameWorld& w, Vec2 feet, bool faceLeft, bool materialize) {
    world_ = &w;
    box = Rect::fromBottomCenter(feet, cfg_.size);
    prevBox = box;
    vel = {};
    facing = faceLeft ? -1.0f : 1.0f;
    health = cfg_.maxHealth;
    energy = cfg_.maxEnergy;
    airJumpsLeft_ = cfg_.airJumps;
    grounded_ = wasGrounded_ = false;
    invulnTimer_ = hurtTimer_ = attackTimer_ = castTimer_ = 0.0f;
    afterimages_.clear();
    shield_ = false;
    for (auto& p : powers_) p->cancel(*this, w);
    state_ = PlayerState::Idle;
    if (materialize) startRespawnAnimation();
    anim.play(materialize ? "respawn" : "idle", true);
}

void Player::setHidden(bool hidden) {
    if (hidden) {
        state_ = PlayerState::Hidden;
        vel = {};
    } else if (state_ == PlayerState::Hidden) {
        state_ = PlayerState::Idle;
    }
}

void Player::startRespawnAnimation() {
    state_ = PlayerState::Respawn;
    respawnTimer_ = 0.75f;
    vel = {};
    anim.play("respawn", true);
}

void Player::startVictory() {
    state_ = PlayerState::Victory;
    vel.x = 0.0f;
    anim.play("victory", true);
}

void Player::forceAnimation(const std::string& clip, float seconds) {
    forcedAnim_ = clip;
    forcedAnimTimer_ = seconds;
    anim.play(clip, true);
}

bool Player::invulnerable() const {
    return invulnTimer_ > 0.0f || state_ == PlayerState::Respawn || state_ == PlayerState::Victory ||
           state_ == PlayerState::Dead || state_ == PlayerState::Hidden || phasedTimer_ > 0.0f;
}

bool Player::spendEnergy(float amount) {
    if (energy + 0.001f < amount) return false;
    energy -= amount;
    energyRegenTimer_ = cfg_.energyRegenDelay;
    return true;
}

void Player::setHitbox(Vec2 size) {
    Vec2 feetPos = box.bottomCenter();
    box = Rect::fromBottomCenter(feetPos, size);
}

MoveOptions Player::moveOptions() const {
    MoveOptions o;
    o.dropThrough = dropThroughTimer_ > 0.0f;
    if (shield_) o.ignoreTags |= ColliderTag::ShieldPass;
    return o;
}

int Player::wallDirection(const GameWorld& w) const {
    MoveOptions o = moveOptions();
    Rect r = box.translated({1.5f, 0.0f});
    r.y += 2;
    r.h -= 6;
    if (w.physics().overlapsSolid(r, o)) return 1;
    Rect l = box.translated({-1.5f, 0.0f});
    l.y += 2;
    l.h -= 6;
    if (w.physics().overlapsSolid(l, o)) return -1;
    return 0;
}

void Player::launch(Vec2 velocity, bool resetJumps) {
    vel = velocity;
    jumpCutAvailable_ = false;
    grounded_ = false;
    coyoteTimer_ = 0.0f;
    if (resetJumps) restoreAirJumps();
    if (state_ == PlayerState::Slide || state_ == PlayerState::Climb || state_ == PlayerState::WallSlide) {
        setHitbox(cfg_.size);
        state_ = PlayerState::Jump;
    }
}

void Player::teleportTo(Vec2 feetPos) {
    box = Rect::fromBottomCenter(feetPos, box.size());
    prevBox = box;
    vel.y = std::min(vel.y, 0.0f);
}

void Player::beginDash(Vec2 dir, float speed, float duration) {
    if (state_ == PlayerState::Slide || state_ == PlayerState::Climb) setHitbox(cfg_.size);
    state_ = PlayerState::Dash;
    dashDir_ = dir.normalized();
    dashSpeed_ = speed;
    dashTimer_ = duration;
    vel = dashDir_ * speed;
    afterimageTimer_ = 0.0f;
}

void Player::endDash() {
    if (state_ != PlayerState::Dash) return;
    state_ = grounded_ ? PlayerState::Idle : PlayerState::Fall;
    // Keep some momentum but never the full dash speed.
    vel.x = dashDir_.x * std::min(dashSpeed_ * 0.45f, cfg_.maxRunSpeed);
    vel.y = dashDir_.y < -0.1f ? dashDir_.y * dashSpeed_ * 0.35f : std::max(0.0f, vel.y * 0.3f);
}

// ===================================================================== update

void Player::fixedUpdate(GameWorld& w, const PlayerInput& inRaw, float dt) {
    world_ = &w;
    prevBox = box;
    PlayerInput in = inRaw;
    if (!controlEnabled) {
        in = PlayerInput{};
        in.move.x = cinematicWalk_ > 0 ? 1.0f : (cinematicWalk_ < 0 ? -1.0f : 0.0f);
    }

    // Timers
    auto tick = [dt](float& t) { t = std::max(0.0f, t - dt); };
    tick(invulnTimer_);
    tick(coyoteTimer_);
    tick(jumpBufferTimer_);
    tick(wallJumpLockTimer_);
    tick(attackCooldown_);
    tick(castTimer_);
    tick(interactTimer_);
    tick(landTimer_);
    tick(doubleJumpAnimTimer_);
    tick(dropThroughTimer_);
    tick(phasedTimer_);
    tick(hurtFlash_);
    if (forcedAnimTimer_ > 0) {
        forcedAnimTimer_ -= dt;
        if (forcedAnimTimer_ <= 0) forcedAnim_.clear();
    }

    if (state_ == PlayerState::Hidden) return;
    if (state_ == PlayerState::Dead) {
        deathTimer_ -= dt;
        vel.x = approach(vel.x, 0.0f, 600.0f * dt);
        if (deathTimer_ <= 0.0f) w.onPlayerDeathAnimationDone();
        return;
    }
    if (state_ == PlayerState::Respawn) {
        respawnTimer_ -= dt;
        if (respawnTimer_ <= 0.0f) state_ = PlayerState::Idle;
        return;
    }

    // Energy regeneration
    if (energyRegenTimer_ > 0)
        energyRegenTimer_ -= dt;
    else
        energy = std::min(cfg_.maxEnergy, energy + cfg_.energyRegen * dt);

    // Aim direction: stick / keys, defaults to facing.
    if (std::abs(in.move.x) > 0.3f || std::abs(in.move.y) > 0.3f) {
        Vec2 a{std::abs(in.move.x) > 0.3f ? signf(in.move.x) : 0.0f, std::abs(in.move.y) > 0.5f ? signf(in.move.y) : 0.0f};
        aim_ = a.normalized();
    } else {
        aim_ = {facing, 0.0f};
    }

    for (auto& p : powers_) p->fixedUpdate(*this, w, dt);

    if (state_ == PlayerState::Victory) {
        vel.x = approach(vel.x, cinematicWalk_, 900.0f * dt);
        vel.y = std::min(vel.y + cfg_.gravity() * cfg_.fallGravityMultiplier * dt, cfg_.maxFallSpeed);
        moveAndCollide(w, dt);
        return;
    }

    if (state_ == PlayerState::Hurt) {
        hurtTimer_ -= dt;
        vel.y = std::min(vel.y + cfg_.gravity() * dt, cfg_.maxFallSpeed);
        vel.x = approach(vel.x, 0.0f, 500.0f * dt);
        moveAndCollide(w, dt);
        if (hurtTimer_ <= 0.0f) state_ = grounded_ ? PlayerState::Idle : PlayerState::Fall;
        return;
    }

    if (in.isPressed(Action::Jump)) jumpBufferTimer_ = cfg_.jumpBuffer;

    if (controlEnabled) handlePowers(w, in);

    if (state_ == PlayerState::Dash) {
        dashTimer_ -= dt;
        vel = dashDir_ * dashSpeed_ * speedMultiplier;
        afterimageTimer_ -= dt;
        if (afterimageTimer_ <= 0.0f) {
            afterimageTimer_ = 0.035f;
            afterimages_.push_back({box.bottomCenter(), anim.sourceRect(), facing < 0, 0.25f});
        }
        moveAndCollide(w, dt);
        if (dashTimer_ <= 0.0f) endDash();
        // A buffered jump during a ground dash becomes a long jump.
        if (state_ == PlayerState::Dash && grounded_ && jumpBufferTimer_ > 0 && std::abs(dashDir_.y) < 0.1f) {
            endDash();
            tryJump(w, in);
        }
        return;
    }

    if (controlEnabled) {
        handleAttack(w, in, dt);
        handleInteract(w, in);
    }

    if (state_ == PlayerState::Climb) {
        stepClimb(w, in, dt);
        return;
    }
    if (state_ == PlayerState::Slide) {
        stepSlide(w, in, dt);
        return;
    }
    stepMovement(w, in, dt);
}

void Player::stepMovement(GameWorld& w, const PlayerInput& in, float dt) {
    float mx = in.move.x;
    // Enter climbing
    if (onLadder && controlEnabled && (in.move.y < -0.5f || (in.move.y > 0.5f && !grounded_)) &&
        attackTimer_ <= 0.0f) {
        state_ = PlayerState::Climb;
        vel = {};
        box.x = std::clamp(box.x, ladderRect.x - box.w * 0.25f, ladderRect.right() - box.w * 0.75f);
        restoreAirJumps();
        return;
    }
    // Enter slide (down while running on the ground)
    if (grounded_ && controlEnabled && in.isPressed(Action::MoveDown) && std::abs(vel.x) > cfg_.maxRunSpeed * 0.55f &&
        !inWater) {
        const Collider* ground = w.physics().groundBelow(box, moveOptions());
        bool onOneWay = ground && ground->kind == ColliderKind::OneWay;
        if (!onOneWay) {
            state_ = PlayerState::Slide;
            slideTimer_ = cfg_.slideDuration;
            facing = signf(vel.x);
            vel.x = facing * cfg_.slideSpeed;
            setHitbox(cfg_.slideSize);
            w.playSound("sounds/slide.wav", box.center(), 0.7f);
            w.particles().emit("dust_run", box.bottomCenter(), 1.0f, {-facing, -0.3f});
            return;
        }
    }

    // Horizontal
    float maxSpeed = cfg_.maxRunSpeed;
    float target = mx * maxSpeed;
    if (std::abs(mx) > 0.05f && std::abs(mx) < 0.6f) target = signf(mx) * std::max(cfg_.walkSpeed * std::abs(mx) / 0.6f, 20.0f);
    if (attackTimer_ > 0.0f && grounded_) target *= 0.35f;
    target *= speedMultiplier;
    if (inWater) target *= 0.7f;
    float accel;
    if (grounded_) {
        if (std::abs(target) < 0.01f)
            accel = cfg_.groundDeceleration;
        else if (vel.x != 0.0f && signf(target) != signf(vel.x))
            accel = cfg_.turnAcceleration;
        else
            accel = std::abs(target) > std::abs(vel.x) ? cfg_.groundAcceleration : cfg_.groundDeceleration;
    } else {
        accel = (std::abs(target) > 0.01f ? cfg_.airAcceleration : cfg_.airDeceleration) * cfg_.airControl;
        if (wallJumpLockTimer_ > 0.0f) accel *= 0.15f;
    }
    vel.x = approach(vel.x, target, accel * dt);
    if (std::abs(mx) > 0.1f && attackTimer_ <= 0.0f && wallJumpLockTimer_ <= 0.0f) facing = signf(mx);

    // Gravity
    float g = cfg_.gravity();
    if (vel.y > 0.0f)
        g *= cfg_.fallGravityMultiplier;
    else if (std::abs(vel.y) < cfg_.apexThreshold && in.isHeld(Action::Jump))
        g *= cfg_.apexGravityMultiplier;
    float maxFall = (in.move.y > 0.5f && controlEnabled) ? cfg_.fastFallSpeed : cfg_.maxFallSpeed;
    if (inWater) {
        g *= cfg_.waterGravityMultiplier;
        maxFall = 70.0f;
        if (in.isHeld(Action::Jump) || in.move.y < -0.5f) vel.y = approach(vel.y, -cfg_.swimSpeed * 0.8f, 600.0f * dt);
    }
    vel.y = std::min(vel.y + g * dt, maxFall);

    // Variable jump height
    if (jumpCutAvailable_ && !in.isHeld(Action::Jump) && vel.y < 0.0f) {
        vel.y *= cfg_.jumpCutMultiplier;
        jumpCutAvailable_ = false;
    }
    if (vel.y >= 0.0f) jumpCutAvailable_ = false;

    // Wall slide
    wallDir_ = grounded_ ? 0 : wallDirection(w);
    bool pushingWall = wallDir_ != 0 && signf(mx) == static_cast<float>(wallDir_);
    if (!grounded_ && wallDir_ != 0 && vel.y > 0.0f && pushingWall && !inWater) {
        if (state_ != PlayerState::WallSlide) restoreAirJumps();
        state_ = PlayerState::WallSlide;
        vel.y = std::min(vel.y, cfg_.wallSlideSpeed);
        facing = static_cast<float>(wallDir_);
        runDustTimer_ -= dt;
        if (runDustTimer_ <= 0.0f) {
            runDustTimer_ = 0.09f;
            w.particles().emit("wall_dust", {wallDir_ > 0 ? box.right() : box.x, box.y + 4}, 1.0f);
        }
    } else if (state_ == PlayerState::WallSlide) {
        state_ = PlayerState::Fall;
    }

    // Drop through one-way platforms: Down + Jump
    if (grounded_ && jumpBufferTimer_ > 0.0f && in.move.y > 0.5f) {
        const Collider* ground = w.physics().groundBelow(box, moveOptions());
        if (ground && ground->kind == ColliderKind::OneWay) {
            dropThroughTimer_ = 0.22f;
            jumpBufferTimer_ = 0.0f;
            grounded_ = false;
            box.y += 1.0f;
        }
    }

    tryJump(w, in);
    moveAndCollide(w, dt);

    if (state_ != PlayerState::WallSlide && state_ != PlayerState::Dash) {
        if (grounded_)
            state_ = std::abs(vel.x) > 8.0f ? PlayerState::Run : PlayerState::Idle;
        else
            state_ = inWater ? PlayerState::Swim : (vel.y < 0 ? PlayerState::Jump : PlayerState::Fall);
    }

    // Running dust
    if (grounded_ && std::abs(vel.x) > cfg_.maxRunSpeed * 0.8f) {
        runDustTimer_ -= dt;
        if (runDustTimer_ <= 0.0f) {
            runDustTimer_ = 0.16f;
            w.particles().emit("dust_run", box.bottomCenter(), 0.7f, {-facing, -0.4f});
        }
    }
}

void Player::tryJump(GameWorld& w, const PlayerInput& in) {
    if (jumpBufferTimer_ <= 0.0f || !controlEnabled) return;
    if (grounded_ || coyoteTimer_ > 0.0f) {
        vel.y = -cfg_.jumpVelocity();
        if (state_ == PlayerState::Slide) {
            setHitbox(cfg_.size);
            vel.x = facing * std::max(std::abs(vel.x), cfg_.maxRunSpeed * 1.1f);
        }
        grounded_ = false;
        coyoteTimer_ = 0.0f;
        jumpBufferTimer_ = 0.0f;
        jumpCutAvailable_ = true;
        state_ = PlayerState::Jump;
        w.playSound("sounds/jump.wav", box.center(), 0.75f, w.rng().range(0.95f, 1.05f));
        w.particles().emit("jump_puff", box.bottomCenter(), 1.0f);
        w.emit("player", "jump");
        return;
    }
    int wd = state_ == PlayerState::WallSlide ? wallDir_ : wallDirection(w);
    if (wd != 0 && !inWater) {
        vel = {-static_cast<float>(wd) * cfg_.wallJumpVelocity.x, cfg_.wallJumpVelocity.y};
        facing = -static_cast<float>(wd);
        wallJumpLockTimer_ = cfg_.wallJumpLock;
        jumpBufferTimer_ = 0.0f;
        jumpCutAvailable_ = true;
        state_ = PlayerState::Jump;
        w.playSound("sounds/wall_jump.wav", box.center(), 0.75f);
        w.particles().emit("wall_dust", {wd > 0 ? box.right() : box.x, box.center().y}, 1.4f);
        return;
    }
    if (inWater) {
        vel.y = -cfg_.swimSpeed * 1.5f;
        jumpBufferTimer_ = 0.0f;
        w.particles().emit("bubbles", box.center(), 1.0f);
        return;
    }
    if (airJumpsLeft_ > 0) {
        --airJumpsLeft_;
        vel.y = -cfg_.doubleJumpVelocity();
        jumpBufferTimer_ = 0.0f;
        jumpCutAvailable_ = true;
        doubleJumpAnimTimer_ = 0.36f;
        anim.play("double_jump", true);
        state_ = PlayerState::Jump;
        w.playSound("sounds/double_jump.wav", box.center(), 0.75f);
        w.particles().emit("double_jump_ring", box.bottomCenter(), 1.0f);
        (void)in;
    }
}

void Player::stepClimb(GameWorld& w, const PlayerInput& in, float dt) {
    if (!onLadder) {
        state_ = PlayerState::Fall;
        return;
    }
    vel.x = in.move.x * cfg_.climbSpeed * 0.6f;
    vel.y = in.move.y * cfg_.climbSpeed;
    if (std::abs(in.move.x) > 0.3f) facing = signf(in.move.x);
    if (jumpBufferTimer_ > 0.0f) {
        jumpBufferTimer_ = 0.0f;
        state_ = PlayerState::Jump;
        vel.y = -cfg_.jumpVelocity() * 0.85f;
        vel.x = in.move.x * cfg_.maxRunSpeed;
        jumpCutAvailable_ = true;
        w.playSound("sounds/jump.wav", box.center(), 0.6f);
        return;
    }
    moveAndCollide(w, dt);
    // Keep within the vines horizontally
    if (box.center().x < ladderRect.x || box.center().x > ladderRect.right()) state_ = PlayerState::Fall;
    if (grounded_ && in.move.y > 0.3f) state_ = PlayerState::Idle;
    anim.speed = (std::abs(vel.y) > 1.0f || std::abs(vel.x) > 1.0f) ? 1.0f : 0.0f;
}

void Player::stepSlide(GameWorld& w, const PlayerInput& in, float dt) {
    slideTimer_ -= dt;
    vel.x = approach(vel.x, facing * cfg_.maxRunSpeed * 0.3f, 380.0f * dt);
    vel.y = std::min(vel.y + cfg_.gravity() * cfg_.fallGravityMultiplier * dt, cfg_.maxFallSpeed);
    if (jumpBufferTimer_ > 0.0f && (grounded_ || coyoteTimer_ > 0)) {
        // Check head room before jumping out of a slide.
        Rect stand = Rect::fromBottomCenter(box.bottomCenter(), cfg_.size);
        if (!w.physics().overlapsSolid(stand, moveOptions())) {
            tryJump(w, in);
            return;
        }
    }
    moveAndCollide(w, dt);
    bool endTime = slideTimer_ <= 0.0f || !grounded_ || std::abs(vel.x) < 30.0f;
    if (endTime) {
        Rect stand = Rect::fromBottomCenter(box.bottomCenter(), cfg_.size);
        if (!w.physics().overlapsSolid(stand, moveOptions())) {
            setHitbox(cfg_.size);
            state_ = grounded_ ? PlayerState::Run : PlayerState::Fall;
        } else {
            // Stuck under a low ceiling: keep crawling forward.
            slideTimer_ = 0.05f;
            vel.x = facing * std::max(std::abs(vel.x), 60.0f);
        }
    }
}

void Player::moveAndCollide(GameWorld& w, float dt) {
    wasGrounded_ = grounded_;
    MoveOptions opt = moveOptions();
    Vec2 delta = vel * dt;
    float preVy = vel.y;
    MoveResult res = w.physics().move(box, delta, opt);

    // Ceiling corner correction: slide around ledges when a jump barely clips them.
    if (res.hitTop && preVy < 0.0f) {
        for (int nudge = 1; nudge <= 5; ++nudge) {
            bool fixed = false;
            for (float dir : {-1.0f, 1.0f}) {
                Rect test = box.translated({dir * nudge, delta.y});
                if (!w.physics().overlapsSolid(test, opt)) {
                    box = test;
                    res.hitTop = false;
                    fixed = true;
                    break;
                }
            }
            if (fixed) break;
        }
        if (res.hitTop) {
            vel.y = 0.0f;
            jumpCutAvailable_ = false;
        }
    }

    // Dash into a breakable wall.
    if ((res.hitLeft || res.hitRight) && state_ == PlayerState::Dash) {
        if (const Collider* c = w.physics().get(res.wallId); c && (c->tags & ColliderTag::Breakable)) {
            w.powerHit("dash", c->box.expanded(2));
        }
    }
    if (res.hitLeft || res.hitRight) {
        if (state_ != PlayerState::Dash) vel.x = 0.0f;
    }

    if (res.hitBottom) {
        float impact = preVy;
        grounded_ = true;
        groundId_ = res.groundId;
        if (vel.y > 0.0f) vel.y = 0.0f;
        if (!wasGrounded_) onLanded(w, impact);
    } else if (vel.y >= 0.0f) {
        const Collider* g = w.physics().groundBelow(box, opt);
        grounded_ = g != nullptr;
        groundId_ = g ? g->id : 0;
    } else {
        grounded_ = false;
        groundId_ = 0;
    }
    if (wasGrounded_ && !grounded_ && vel.y >= 0.0f) coyoteTimer_ = cfg_.coyoteTime;
    if (grounded_) {
        airJumpsLeft_ = cfg_.airJumps;
        jumpCutAvailable_ = vel.y < 0.0f && jumpCutAvailable_;
    }
}

void Player::onLanded(GameWorld& w, float impactSpeed) {
    restoreAirJumps();
    for (auto& p : powers_) p->onLand();
    if (impactSpeed > 120.0f) {
        landTimer_ = 0.12f;
        float k = std::clamp(impactSpeed / cfg_.maxFallSpeed, 0.3f, 1.2f);
        w.particles().emit("dust_land", box.bottomCenter(), k);
        w.playSound("sounds/land.wav", box.center(), 0.4f + 0.4f * k, w.rng().range(0.92f, 1.05f));
        if (impactSpeed > cfg_.maxFallSpeed * 0.95f) w.shake(0.12f);
    }
}

}  // namespace cosmic
