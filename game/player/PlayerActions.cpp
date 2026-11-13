// Player combat, powers, damage, animation selection and rendering.

#include <cmath>

#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"

namespace cosmic {

// ------------------------------------------------------------------ powers

void Player::grantPower(const PowerDefinition& def) {
    for (auto& p : powers_)
        if (p->id() == def.id || p->def().input == def.input) {
            p = PowerRegistry::instance().create(def);
            return;
        }
    if (auto p = PowerRegistry::instance().create(def)) powers_.push_back(std::move(p));
}

bool Player::hasPower(const std::string& id) const {
    for (const auto& p : powers_)
        if (p->id() == id) return true;
    return false;
}

Power* Player::power(const std::string& id) {
    for (auto& p : powers_)
        if (p->id() == id) return p.get();
    return nullptr;
}

void Player::handlePowers(GameWorld& w, const PlayerInput& in) {
    if (state_ == PlayerState::Climb && !in.isPressed(Action::Dash)) return;
    for (auto& p : powers_) {
        if (!in.isPressed(p->def().input)) continue;
        if (!p->ready(*this)) {
            if (energy < p->def().energyCost) {
                w.playSound("sounds/energy_low.wav", 0.5f);
                w.hint("Énergie insuffisante", 1.2f);
            }
            continue;
        }
        if (!p->canActivate(*this, w)) continue;
        if (state_ == PlayerState::Climb || state_ == PlayerState::Slide) {
            setHitbox(cfg_.size);
            state_ = PlayerState::Fall;
        }
        p->activate(*this, w);
        powersUsed_.insert(p->id());
        w.stats().powersUsed.insert(p->id());
        w.emit(p->id(), "powerUsed", {{"x", center().x}, {"y", center().y}});
        break;  // one power per step
    }
}

// ------------------------------------------------------------------ combat

void Player::handleAttack(GameWorld& w, const PlayerInput& in, float dt) {
    if (attackTimer_ > 0.0f) {
        float elapsed = cfg_.attackDuration - attackTimer_;
        attackTimer_ -= dt;
        if (!attackHitDone_ && elapsed >= cfg_.attackDuration * 0.25f) {
            attackHitDone_ = true;
            Rect hit;
            if (aim_.y < -0.5f && std::abs(aim_.x) < 0.5f) {
                hit = Rect::fromCenter(center() + Vec2{0, -box.h * 0.5f - cfg_.attackRange.x * 0.4f},
                                       {cfg_.attackRange.y + 6, cfg_.attackRange.x});
            } else if (aim_.y > 0.5f && !grounded_ && std::abs(aim_.x) < 0.5f) {
                hit = Rect::fromCenter(center() + Vec2{0, box.h * 0.5f + cfg_.attackRange.x * 0.4f},
                                       {cfg_.attackRange.y + 6, cfg_.attackRange.x});
            } else {
                hit = Rect::fromCenter(center() + Vec2{facing * (box.w * 0.5f + cfg_.attackRange.x * 0.5f - 2), -2},
                                       cfg_.attackRange);
            }
            DamageInfo d;
            d.amount = cfg_.attackDamage;
            d.type = DamageType::Melee;
            d.source = center();
            d.knockback = 180.0f;
            d.fromPlayer = true;
            int hits = w.damageEnemies(hit, d, this);
            if (hits > 0) {
                w.playSound("sounds/slash_hit.wav", hit.center(), 0.9f);
                w.hitStop(0.045f);
                w.shake(0.12f);
                w.rumble(0.3f, 0.5f, 90);
                // Downward strike bounces off enemies (pogo).
                if (aim_.y > 0.5f && !grounded_) launch({vel.x, -cfg_.jumpVelocity() * 0.75f}, true);
            }
            w.powerHit("melee", hit);
        }
        return;
    }
    if (!in.isPressed(Action::Attack) || attackCooldown_ > 0.0f) return;
    if (state_ == PlayerState::Climb || state_ == PlayerState::Slide || state_ == PlayerState::WallSlide) return;
    attackTimer_ = cfg_.attackDuration;
    attackCooldown_ = cfg_.attackCooldown;
    attackHitDone_ = false;
    attackAir_ = !grounded_;
    anim.play(attackAir_ ? "air_attack" : "attack", true);
    w.playSound("sounds/slash.wav", center(), 0.8f, w.rng().range(0.93f, 1.08f));
    Vec2 fxPos = center() + Vec2{facing * 12.0f, -2.0f};
    w.particles().emit("slash_sparks", fxPos, 1.0f, {facing, 0.0f});
}

void Player::handleInteract(GameWorld& w, const PlayerInput& in) {
    if (!in.isPressed(Action::Interact)) return;
    LevelEntity* e = w.nearestInteractable();
    if (!e || !grounded_) return;
    interactTimer_ = 0.3f;
    anim.play("interact", true);
    e->interact(w);
}

bool Player::hurt(GameWorld& w, const DamageInfo& dmg) {
    if (!alive() || state_ == PlayerState::Respawn || state_ == PlayerState::Victory) return false;
    if (invulnerable() && dmg.type != DamageType::Hazard) return false;
    if (shield_ && !dmg.ignoreShield) {
        w.playSound("sounds/shield_block.wav", center(), 0.9f);
        w.particles().emit("shield_hit", center(), 1.0f);
        return false;
    }
    if (invulnTimer_ > 0.0f) return false;
    int amount = std::max(1, static_cast<int>(std::round(dmg.amount)));
    health -= amount;
    w.stats().damageTaken += amount;
    hurtFlash_ = 0.25f;
    w.flash({255, 60, 90, 255}, 0.18f);
    w.shake(0.35f);
    w.hitStop(0.06f);
    w.rumble(0.7f, 0.9f, 220);
    w.particles().emit("player_hit", center(), 1.0f);
    if (health <= 0) {
        health = 0;
        kill(w);
        return true;
    }
    w.playSound("sounds/player_hurt.wav", center(), 1.0f);
    invulnTimer_ = cfg_.invulnerability;
    if (state_ == PlayerState::Dash) endDash();
    if (state_ == PlayerState::Slide || state_ == PlayerState::Climb) setHitbox(cfg_.size);
    state_ = PlayerState::Hurt;
    hurtTimer_ = 0.32f;
    float dir = center().x >= dmg.source.x ? 1.0f : -1.0f;
    vel = {dir * cfg_.knockback.x * std::clamp(dmg.knockback / 160.0f, 0.3f, 2.0f), cfg_.knockback.y};
    attackTimer_ = 0.0f;
    anim.play("hurt", true);
    return true;
}

void Player::kill(GameWorld& w) {
    if (state_ == PlayerState::Dead) return;
    if (state_ == PlayerState::Slide || state_ == PlayerState::Climb) setHitbox(cfg_.size);
    state_ = PlayerState::Dead;
    deathTimer_ = 1.1f;
    health = 0;
    vel = {0.0f, 0.0f};
    shield_ = false;
    for (auto& p : powers_) p->cancel(*this, w);
    anim.play("death", true);
    w.playSound("sounds/player_death.wav", center(), 1.0f);
    w.particles().emit("player_death", center(), 1.0f);
    w.shake(0.5f);
    w.rumble(1.0f, 1.0f, 400);
    w.stats().deaths++;
}

// --------------------------------------------------------------- animation

void Player::chooseAnimation() {
    if (!forcedAnim_.empty()) return;
    std::string clip;
    float speed = 1.0f;
    switch (state_) {
        case PlayerState::Dead: clip = "death"; break;
        case PlayerState::Respawn: clip = "respawn"; break;
        case PlayerState::Victory: clip = "victory"; break;
        case PlayerState::Hurt: clip = "hurt"; break;
        case PlayerState::Dash: clip = "dash"; break;
        case PlayerState::Slide: clip = "slide"; break;
        case PlayerState::Climb: clip = "climb"; speed = anim.speed; break;
        case PlayerState::WallSlide: clip = "wall_slide"; break;
        case PlayerState::Hidden: return;
        default: {
            if (attackTimer_ > 0.0f) {
                clip = attackAir_ ? "air_attack" : "attack";
            } else if (interactTimer_ > 0.0f) {
                clip = "interact";
            } else if (castTimer_ > 0.0f && grounded_ && std::abs(vel.x) < 40.0f) {
                clip = "cast";
            } else if (grounded_) {
                float s = std::abs(vel.x);
                if (landTimer_ > 0.0f && s < 40.0f)
                    clip = "land";
                else if (s > cfg_.maxRunSpeed * 0.62f) {
                    clip = "run";
                    speed = std::clamp(s / cfg_.maxRunSpeed, 0.6f, 1.4f);
                } else if (s > 8.0f) {
                    clip = "walk";
                    speed = std::clamp(s / cfg_.walkSpeed, 0.6f, 1.5f);
                } else {
                    clip = "idle";
                }
            } else if (doubleJumpAnimTimer_ > 0.0f) {
                clip = "double_jump";
            } else if (inWater) {
                clip = "fall";
                speed = 0.5f;
            } else {
                clip = vel.y < -30.0f ? "jump" : "fall";
            }
        }
    }
    if (castTimer_ > 0.0f && !grounded_ && state_ != PlayerState::Dash && attackTimer_ <= 0.0f) clip = "cast";
    anim.play(clip);
    anim.speed = speed;
}

void Player::onAnimEvent(GameWorld& w, const std::string& e) {
    if (e == "footstep" && grounded_) {
        footstepAlt_ = !footstepAlt_;
        w.playSound(footstepAlt_ ? "sounds/footstep_a.wav" : "sounds/footstep_b.wav", feet(), 0.35f,
                    w.rng().range(0.9f, 1.1f));
    } else if (e.rfind("sfx:", 0) == 0) {
        w.playSound("sounds/" + e.substr(4) + ".wav", center(), 0.8f);
    }
}

void Player::update(GameWorld& w, float dt) {
    chooseAnimation();
    anim.update(dt, [&](const std::string& e) { onAnimEvent(w, e); });
    for (auto& a : afterimages_) a.life -= dt;
    while (!afterimages_.empty() && afterimages_.front().life <= 0.0f) afterimages_.pop_front();
}

void Player::render(RenderContext& ctx) const {
    if (state_ == PlayerState::Hidden || !anim.set() || !anim.set()->texture) return;
    const Texture& tex = *anim.set()->texture;
    Vec2 f = renderFeet(ctx.alpha);
    bool flip = facing < 0.0f;

    // Dash afterimages (additive, cyan)
    for (const auto& a : afterimages_) {
        float k = std::clamp(a.life / 0.25f, 0.0f, 1.0f);
        Rect dst = anim.destRect(a.feet, a.flip);
        ctx.r.drawWorldTexture(tex, &a.src, dst, Color{57, 240, 216, static_cast<uint8_t>(150 * k)}, BlendMode::Additive,
                               a.flip);
    }

    Color tint = Color::white();
    if (invulnTimer_ > 0.0f && state_ != PlayerState::Hurt) {
        if (static_cast<int>(invulnTimer_ * 14.0f) % 2 == 0) tint.a = 90;
    }
    if (hurtFlash_ > 0.0f) tint = Color{255, 140, 160, tint.a};
    if (phasedTimer_ > 0.0f) tint = Color{176, 120, 255, 120};
    Rect src = anim.sourceRect();
    Rect dst = anim.destRect(f, flip);
    ctx.r.drawWorldTexture(tex, &src, dst, tint, BlendMode::Alpha, flip);
    if (hurtFlash_ > 0.0f)
        ctx.r.drawWorldTexture(tex, &src, dst, Color{255, 80, 120, static_cast<uint8_t>(hurtFlash_ * 400)},
                               BlendMode::Additive, flip);

    // Visor / core light
    if (state_ != PlayerState::Dead) {
        ctx.lights.add(f + Vec2{facing * 2.0f, -16.0f}, 46.0f, {57, 240, 216, 255});
        ctx.lights.add(f + Vec2{0.0f, -12.0f}, 90.0f, {90, 110, 140, 255});
    }
    for (const auto& p : powers_) p->render(*this, ctx);
}

}  // namespace cosmic
