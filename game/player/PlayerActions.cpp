// Player combat, powers, damage, animation selection and rendering.

#include <algorithm>
#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"

namespace cosmic {

// ------------------------------------------------------------------ powers

bool Player::sharesExclusiveInput(const Power& owned, const PowerDefinition& def, const Power& created) const {
    // Keys per slot: every slot has its own key, two powers never compete for one.
    if (keysPerSlot_) return false;
    return owned.def().input == def.input && owned.exclusiveInput() && created.exclusiveInput();
}

void Player::setKeysPerSlot(bool on) {
    keysPerSlot_ = on;
    assignPowerKeys();
}

void Player::assignPowerKeys() {
    for (size_t i = 0; i < powers_.size(); ++i) {
        Power& p = *powers_[i];
        bool slotKey = keysPerSlot_ && p.trigger() == PowerTrigger::SlotKey;
        p.setKey(slotKey ? powerSlotAction(static_cast<int>(i)) : p.def().input);
    }
}

void Player::grantPower(const PowerDefinition& def) {
    auto created = PowerRegistry::instance().create(def);
    if (!created) return;
    bool replaced = false;
    for (auto& p : powers_) {
        if (p->id() == def.id || sharesExclusiveInput(*p, def, *created)) {
            p = std::move(created);
            replaced = true;
            break;
        }
    }
    if (!replaced) powers_.push_back(std::move(created));
    assignPowerKeys();
    refreshPassivePowers();
}

int Player::conflictingPower(const PowerDefinition& def) const {
    auto probe = PowerRegistry::instance().create(def);
    if (!probe) return -1;
    for (size_t i = 0; i < powers_.size(); ++i)
        if (powers_[i]->id() != def.id && sharesExclusiveInput(*powers_[i], def, *probe)) return static_cast<int>(i);
    return -1;
}

Player::Offer Player::offerPower(const PowerDefinition& def) {
    if (hasPower(def.id)) return Offer::AlreadyOwned;
    bool full = powerSlots_ > 0 && static_cast<int>(powers_.size()) >= powerSlots_;
    if (full || conflictingPower(def) >= 0) return Offer::NeedsChoice;
    auto created = PowerRegistry::instance().create(def);
    if (!created) return Offer::AlreadyOwned;
    powers_.push_back(std::move(created));
    assignPowerKeys();
    refreshPassivePowers();
    return Offer::Granted;
}

std::string Player::replacePower(int slot, const PowerDefinition& def) {
    if (slot < 0 || slot >= static_cast<int>(powers_.size())) return {};
    auto created = PowerRegistry::instance().create(def);
    if (!created) return {};
    std::string removed = powers_[static_cast<size_t>(slot)]->id();
    powers_[static_cast<size_t>(slot)] = std::move(created);
    assignPowerKeys();
    refreshPassivePowers();
    return removed;
}

bool Player::removePower(const std::string& id) {
    auto it = std::find_if(powers_.begin(), powers_.end(), [&](const auto& p) { return p->id() == id; });
    if (it == powers_.end()) return false;
    powers_.erase(it);
    assignPowerKeys();  // the powers after it move down a slot
    refreshPassivePowers();
    return true;
}

void Player::refreshPassivePowers() {
    // Passive upgrades
    int before = extraAirJumps_;
    extraAirJumps_ = 0;
    for (const auto& p : powers_)
        if (const auto* aj = dynamic_cast<const AirJumpPower*>(p.get())) extraAirJumps_ += aj->extraJumps();
    if (grounded_) restoreAirJumps();
    else if (extraAirJumps_ >= before) airJumpsLeft_ = std::max(airJumpsLeft_, std::min(airJumpCount(), airJumpsLeft_ + extraAirJumps_));
    else airJumpsLeft_ = std::min(airJumpsLeft_, airJumpCount());
}

Power* Player::movementPower() const {
    for (const auto& p : powers_)
        if (p->overridesMovement()) return p.get();
    return nullptr;
}

void Player::applySlow(float multiplier, float seconds) {
    slowMultiplier_ = std::clamp(multiplier, 0.2f, 1.0f);
    slowTimer_ = std::max(slowTimer_, seconds);
}

void Player::applyCurse(float heightRatio, float seconds) {
    if (seconds <= 0.0f) return;
    const float r = std::clamp(heightRatio, 0.3f, 1.0f);
    curseRatio_ = curseTimer_ > 0.0f ? std::min(curseRatio_, r) : r;
    curseTimer_ = std::max(curseTimer_, seconds);
}

float Player::curseSpeed() const { return curseTimer_ > 0.0f ? std::sqrt(curseRatio_) : 1.0f; }

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

void Player::handlePowers(GameWorld& w, const PlayerInput& in, float dt) {
    for (auto& p : powers_) {
        GameWorld::AttackScope tag(w, p->attackTag());  // 2.54 (statistics)
        p->handleInput(*this, w, in, dt);
    }
    if (movementPower()) return;
    if (state_ == PlayerState::Climb) {
        // On a ladder only the dash (a power on the Dash button) and the interact button
        // (grapple) can be used: with keys per slot, the key of the slot holding a dash.
        bool allowed = in.isPressed(Action::Dash) || in.isPressed(Action::Interact);
        for (const auto& p : powers_)
            if (p->def().input == Action::Dash && p->key() != Action::Count && in.isPressed(p->key())) allowed = true;
        if (!allowed) return;
    }
    for (auto& p : powers_) {
        if (p->passive() || !p->wantsActivation(*this, w, in)) continue;
        if (!p->ready(*this)) {
            if (energy < p->activationCost(*this)) {
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

SlashDirection Player::slashDirectionFor(Vec2 aim, bool grounded) const {
    if (aim.y < -0.5f && std::abs(aim.x) < 0.5f) return SlashDirection::Up;
    if (aim.y > 0.5f && !grounded && std::abs(aim.x) < 0.5f) return SlashDirection::Down;
    return SlashDirection::Forward;
}

Rect Player::slashArea(SlashDirection dir) const {
    switch (dir) {
        case SlashDirection::Up:
            return Rect::fromCenter(center() + Vec2{0, -box.h * 0.5f - cfg_.attackRange.x * 0.4f},
                                    {cfg_.attackRange.y + 6, cfg_.attackRange.x});
        case SlashDirection::Down:
            return Rect::fromCenter(center() + Vec2{0, box.h * 0.5f + cfg_.attackRange.x * 0.4f},
                                    {cfg_.attackRange.y + 6, cfg_.attackRange.x});
        case SlashDirection::Forward: break;
    }
    return Rect::fromCenter(center() + Vec2{facing * (box.w * 0.5f + cfg_.attackRange.x * 0.5f - 2), -2}, cfg_.attackRange);
}

const char* Player::attackClip() const {
    const char* clip = attackAir_ ? "air_attack" : "attack";
    if (slashDir_ == SlashDirection::Forward) return clip;
    // 2.71: the strike upwards or downwards has its own animation (rows 27-29 of Nova's sheet).
    // A sheet without it, or a suit drawn before it (a suit created online covers the first 27
    // rows only: CreationImages kSuitSheetH), plays the usual one. Display only.
    const char* dir = slashDir_ == SlashDirection::Up ? (attackAir_ ? "air_attack_up" : "attack_up") : "air_attack_down";
    const AnimationClip* c = anim.set() ? anim.set()->clip(dir) : nullptr;
    if (c && (!skin || (c->row + 1) * anim.set()->frameHeight <= skin->height())) clip = dir;
    return clip;
}

void Player::handleAttack(GameWorld& w, const PlayerInput& in, float dt) {
    if (attackTimer_ > 0.0f) {
        float elapsed = cfg_.attackDuration - attackTimer_;
        attackTimer_ -= dt;
        if (!attackHitDone_ && elapsed >= cfg_.attackDuration * 0.25f) {
            attackHitDone_ = true;
            // 2.71 (simulation 15): the strike goes where the attack started (its animation): the
            // aim is the one held when it started. A replay recorded before plays with the rules of
            // its time: the aim read now.
            const bool aimed = w.simulationVersion() >= kAimedSlashSimulation;
            const Vec2 aim = aimed ? slashAim_ : aim_;
            const Rect hit = slashArea(aimed ? slashDir_ : slashDirectionFor(aim_, grounded_));
            DamageInfo d;
            d.amount = cfg_.attackDamage;
            d.type = DamageType::Melee;
            d.source = center();
            d.knockback = 180.0f;
            d.fromPlayer = true;
            int hits = 0;
            {
                GameWorld::AttackScope tag(w, meleeTag_);  // 2.54 (statistics): this swing
                hits = w.damageEnemies(hit, d, this);
            }
            if (hits > 0) {
                w.playSound("sounds/slash_hit.wav", hit.center(), 0.9f);
                w.hitStop(0.045f);
                w.shake(0.12f);
                w.rumble(0.3f, 0.5f, 90);
                // A strike aimed downwards in the air bounces off the enemies it hits (pogo), as
                // always (2.71: with the aim the attack started with).
                if (aim.y > 0.5f && !grounded_) launch({vel.x, -cfg_.jumpVelocity() * 0.75f}, true);
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
    meleeTag_ = w.newAttackTag("attack");  // 2.54: statistics only
    attackAir_ = !grounded_;
    // 2.71: Up alone strikes above (on the ground or in the air), Down alone below (in the air).
    slashAim_ = aim_;
    slashDir_ = slashDirectionFor(aim_, grounded_);
    anim.play(attackClip(), true);
    w.playSound("sounds/slash.wav", center(), 0.8f, w.fxRng().range(0.93f, 1.08f));
    Vec2 fxPos = center() + Vec2{facing * 12.0f, -2.0f};
    Vec2 fxDir{facing, 0.0f};
    if (slashDir_ == SlashDirection::Up) {
        fxPos = center() + Vec2{facing * 2.0f, -box.h * 0.5f - 6.0f};
        fxDir = {0.0f, -1.0f};
    } else if (slashDir_ == SlashDirection::Down) {
        fxPos = center() + Vec2{facing * 2.0f, box.h * 0.5f + 6.0f};
        fxDir = {0.0f, 1.0f};
    }
    w.particles().emit("slash_sparks", fxPos, 1.0f, fxDir);
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
    if (w.invinciblePlayer()) return false;  // 2.51: administrators' training runs (2.75: a raid test: the administrator only)
    if (invulnerable() && dmg.type != DamageType::Hazard) return false;
    if (shield_ && !dmg.ignoreShield) {
        w.noteBlocked(dmg.amount);  // 2.68: the raids' contributions (statistics only)
        w.playSound("sounds/shield_block.wav", center(), 0.9f);
        w.particles().emit("shield_hit", center(), 1.0f);
        return false;
    }
    if (invulnTimer_ > 0.0f) return false;
    // The level's damage factor (shown before playing: "Dégâts reçus : x1.5").
    int amount = std::max(1, static_cast<int>(std::round(dmg.amount * w.level().damageFactor)));
    if (w.hurtScale() != 1.0f) {
        // Titan 13: a raid's difficulty (its Gigaboss' and its Heart's damage × 0.6 to 1.5): the exact total, the
        // fractions carried from one blow to the next (a blow of 1 at × 0.6: 0, 1, 1, 0, 1…), never more at once.
        hurtCarry += dmg.amount * w.level().damageFactor * w.hurtScale();
        amount = static_cast<int>(std::floor(hurtCarry + 1e-4f));
        hurtCarry = std::max(0.0f, hurtCarry - static_cast<float>(amount));
    }
    health -= amount;
    if (dmg.slow > 0.0f && dmg.slowDuration > 0.0f) {
        applySlow(dmg.slow, dmg.slowDuration);
        w.particles().emit("frost_burst", center(), 1.0f);
        w.playSound("sounds/freeze.wav", center(), 0.8f);
    }
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
    if (w.rescueInvincible()) return;  // 2.51: an invincible Nova goes back to solid ground
    w.notePlayerKilled();              // 2.54: statistics only
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
    if (state_ != PlayerState::Dead && state_ != PlayerState::Respawn && state_ != PlayerState::Victory &&
        state_ != PlayerState::Hurt && state_ != PlayerState::Hidden && attackTimer_ <= 0.0f) {
        for (const auto& p : powers_)
            if (const char* c = p->animationClip(*this); c && anim.set() && anim.set()->clip(c)) {
                anim.play(c);
                anim.speed = 1.0f;
                return;
            }
    }
    switch (state_) {
        case PlayerState::Dead: clip = "death"; break;
        case PlayerState::Respawn: clip = "respawn"; break;
        case PlayerState::Victory: clip = "victory"; break;
        case PlayerState::Hurt: clip = "hurt"; break;
        case PlayerState::Dash: clip = "dash"; break;
        case PlayerState::Slide: clip = "slide"; break;
        case PlayerState::Crouch: clip = "crouch"; break;
        case PlayerState::Climb: clip = "climb"; speed = anim.speed; break;
        case PlayerState::WallSlide: clip = "wall_slide"; break;
        case PlayerState::Hidden: return;
        default: {
            if (attackTimer_ > 0.0f) {
                clip = attackClip();
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
                    w.fxRng().range(0.9f, 1.1f));
    } else if (e.rfind("sfx:", 0) == 0) {
        w.playSound("sounds/" + e.substr(4) + ".wav", center(), 0.8f);
    }
}

void Player::update(GameWorld& w, float dt) {
    chooseAnimation();
    anim.update(dt, [&](const std::string& e) { onAnimEvent(w, e); });
}

void Player::render(RenderContext& ctx) const {
    if (state_ == PlayerState::Hidden || !anim.set() || !anim.set()->texture) return;
    const Texture& tex = skin ? *skin : *anim.set()->texture;
    Vec2 f = renderFeet(ctx.alpha);
    bool flip = facing < 0.0f;

    // Dash afterimages (additive, cyan)
    for (const auto& a : afterimages_) {
        float k = std::clamp(a.life / kAfterimageLife, 0.0f, 1.0f);
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
    if (curseTimer_ > 0.0f && state_ != PlayerState::Dead) {
        // 2.57, the Liche's « Maléfice »: a ring of violet runes turns around her, a shackle of soul
        // fire weighs on her feet, and an arc shows the time the curse has left. Display only.
        const float s = ctx.r.view().scale / 3.0f;
        const float fade = std::clamp(curseTimer_ / 0.4f, 0.0f, 1.0f);
        const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 7.0f);
        const Color hex{196, 128, 255, static_cast<uint8_t>((150 + 80 * pulse) * fade)};
        const Vec2 mid = f + Vec2{0.0f, -11.0f};
        ctx.r.drawWorldGlow(mid, 20.0f + 3.0f * pulse, Color{150, 70, 230, static_cast<uint8_t>(70 * fade)});
        for (int k = 0; k < 6; ++k) {
            const float a = ctx.time * 2.4f + static_cast<float>(k) * 1.0471976f;
            const Vec2 p = mid + Vec2{dmath::cos(a) * 14.0f, dmath::sin(a) * 5.0f};
            ctx.r.fillWorldRect(Rect::fromCenter(p, {3.0f, 3.0f}), hex, BlendMode::Additive);
        }
        ctx.r.drawWorldLine(f + Vec2{-7.0f, -1.0f}, f + Vec2{7.0f, -1.0f}, hex, std::max(1.0f, 1.6f * s), BlendMode::Additive);
        ctx.r.drawWorldCircle(f + Vec2{-7.0f, -1.5f}, 2.0f, hex, std::max(1.0f, s));
        ctx.r.drawWorldCircle(f + Vec2{7.0f, -1.5f}, 2.0f, hex, std::max(1.0f, s));
        // The time left: an arc over her head that shrinks.
        const float left = std::clamp(curseTimer_ / 5.0f, 0.0f, 1.0f);
        const Vec2 top = f + Vec2{0.0f, -30.0f};
        const int seg = std::max(1, static_cast<int>(16.0f * left));
        for (int k = 0; k < seg; ++k) {
            const float a0 = -1.5707963f - 3.1415927f * left + 6.2831853f * left * static_cast<float>(k) / static_cast<float>(seg);
            const float a1 = -1.5707963f - 3.1415927f * left + 6.2831853f * left * static_cast<float>(k + 1) / static_cast<float>(seg);
            ctx.r.drawWorldLine(top + Vec2{dmath::cos(a0), dmath::sin(a0)} * 4.0f, top + Vec2{dmath::cos(a1), dmath::sin(a1)} * 4.0f, hex,
                                std::max(1.0f, 1.2f * s), BlendMode::Additive);
        }
        ctx.lights.add(mid, 50.0f, Color{170, 90, 255, 255});
    }
    for (const auto& p : powers_) p->render(*this, ctx);
}

}  // namespace cosmic
