// Platforms and terrain-like entities: moving / crumbling platforms, power gates,
// bounce pads and climbable vines.

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

// ----------------------------------------------------------- AnimatedSprite

void AnimatedSprite::load(ResourceManager& res, const std::string& path, const std::string& clip) {
    set = res.animation(path);
    anim.setSet(set);
    anim.play(clip, true);
}

void AnimatedSprite::draw(RenderContext& ctx, Vec2 anchor, bool flip, Color tint, float angle, BlendMode blend) const {
    if (!set || !set->texture) return;
    Rect src = anim.sourceRect();
    Rect dst = anim.destRect(anchor, flip);
    Vec2 pivot{flip ? set->frameWidth - set->pivot.x : set->pivot.x, set->pivot.y};
    ctx.r.drawWorldTexture(*set->texture, &src, dst, tint, blend, flip, angle, angle != 0.0f ? &pivot : nullptr);
}

namespace {
/// Draws a horizontal 3-slice (left cap, repeated middle, right cap) from a row of 16 px cells.
void drawThreeSlice(RenderContext& ctx, const Texture& tex, int row, const Rect& r, Color tint = Color::white()) {
    const float cell = 16.0f;
    float y = r.y;
    int cols = std::max(1, static_cast<int>(std::round(r.w / cell)));
    for (int i = 0; i < cols; ++i) {
        int c = cols == 1 ? 1 : (i == 0 ? 0 : (i == cols - 1 ? 2 : 1));
        Rect src{c * cell, row * cell, cell, cell};
        ctx.r.drawWorldTexture(tex, &src, {r.x + i * cell, y, cell, cell}, tint);
    }
}
}  // namespace

// ----------------------------------------------------------- moving platform

void MovingPlatformEntity::init(GameWorld& w) {
    start_ = box.pos();
    end_ = start_ + Vec2{obj_.prop<float>("dx", 128.0f), obj_.prop<float>("dy", 0.0f)};
    length_ = std::max(1.0f, distance(start_, end_));
    moving_ = obj_.prop<bool>("active", true);
    triggered_ = obj_.prop<bool>("triggered", false);
    if (triggered_) moving_ = false;
    t_ = std::clamp(obj_.prop<float>("phase", 0.0f), 0.0f, 1.0f);
    box = Rect{lerpf(start_.x, end_.x, t_), lerpf(start_.y, end_.y, t_), box.w, box.h};
    prevPos_ = box.pos();
    bool oneway = obj_.prop<bool>("oneway", false);
    Rect col = oneway ? Rect{box.x, box.y, box.w, 4.0f} : box;
    collider_ = w.physics().add(col, oneway ? ColliderKind::OneWay : ColliderKind::Solid, ColliderTag::Moving, this, true);
    tex_ = w.resources().texture("sprites/objects/moving_platform.png");
}

void MovingPlatformEntity::fixedUpdate(GameWorld& w, float dt) {
    prevPos_ = box.pos();
    if (triggered_ && !moving_ && w.player().groundCollider() == collider_) moving_ = true;
    if (moving_) {
        if (wait_ > 0.0f) {
            wait_ -= dt;
        } else {
            float speed = obj_.prop<float>("speed", 60.0f);
            t_ += dir_ * speed * dt / length_;
            if (t_ >= 1.0f || t_ <= 0.0f) {
                t_ = std::clamp(t_, 0.0f, 1.0f);
                dir_ = -dir_;
                wait_ = obj_.prop<float>("wait", 0.6f);
            }
        }
    }
    // Smooth start / stop at the ends.
    float e = applyEase(Ease::InOutSine, t_);
    Vec2 p{lerpf(start_.x, end_.x, e), lerpf(start_.y, end_.y, e)};
    delta_ = p - box.pos();
    box.x = p.x;
    box.y = p.y;
    bool oneway = obj_.prop<bool>("oneway", false);
    Rect col = oneway ? Rect{box.x, box.y, box.w, 4.0f} : box;
    w.physics().setDynamicBox(collider_, col, delta_ / std::max(dt, 1e-4f));
}

void MovingPlatformEntity::render(RenderContext& ctx) {
    if (!tex_) return;
    Vec2 p = lerpv(prevPos_, box.pos(), ctx.alpha);
    Rect r{p.x, p.y, box.w, 16.0f};
    drawThreeSlice(ctx, *tex_, moving_ ? 1 : 0, r);
    // Under-glow of the anti-gravity engines.
    Color glow = moving_ ? Color{255, 179, 71, 140} : Color{120, 90, 60, 80};
    ctx.r.drawWorldGlow({r.center().x, r.bottom() + 2}, box.w * 0.45f, glow);
    ctx.lights.add({r.center().x, r.bottom()}, box.w * 0.9f, {255, 160, 80, 255});
    if (ctx.editor) {
        Vec2 a = obj_.rect.center(), b = a + Vec2{obj_.prop<float>("dx", 0), obj_.prop<float>("dy", 0)};
        ctx.r.drawWorldLine(a, b, {255, 179, 71, 160}, 1.0f);
        ctx.r.drawWorldRect(Rect::fromCenter(b, box.size()), {255, 179, 71, 90});
    }
}

bool MovingPlatformEntity::handleAction(GameWorld& /*w*/, const std::string& action, const Json&) {
    if (action == "activate" || action == "enable" || action == "open") moving_ = true;
    else if (action == "disable" || action == "close") moving_ = false;
    else if (action == "toggle") moving_ = !moving_;
    else return false;
    return true;
}

std::string MovingPlatformEntity::debugInfo() const { return moving_ ? "en mouvement" : "arrêtée"; }

// --------------------------------------------------------- crumble platform

void CrumblePlatformEntity::init(GameWorld& w) {
    collider_ = w.physics().add(box, ColliderKind::Solid, ColliderTag::Crumble, this);
    tex_ = w.resources().texture("sprites/objects/crumble_platform.png");
}

void CrumblePlatformEntity::fixedUpdate(GameWorld& w, float dt) {
    switch (phase_) {
        case Phase::Solid:
            if (w.player().groundCollider() == collider_ && w.player().grounded()) {
                phase_ = Phase::Shaking;
                timer_ = obj_.prop<float>("delay", 0.45f);
                w.playSound("sounds/crumble.wav", box.center(), 0.5f, 1.3f);
            }
            break;
        case Phase::Shaking:
            timer_ -= dt;
            if (timer_ <= 0.0f) {
                phase_ = Phase::Fallen;
                timer_ = obj_.prop<float>("respawn", 3.0f);
                w.physics().setEnabled(collider_, false);
                w.particles().emit("crumble_debris", box.center(), std::max(1.0f, box.w / 48.0f));
                w.playSound("sounds/crumble.wav", box.center(), 0.9f);
            }
            break;
        case Phase::Fallen:
            if (timer_ > 0.0f) {
                timer_ -= dt;
                if (timer_ <= 0.0f) {
                    if (w.player().box.intersects(box)) {
                        timer_ = 0.3f;  // wait until the player moved away
                    } else {
                        phase_ = Phase::Solid;
                        w.physics().setEnabled(collider_, true);
                        w.particles().emit("reform", box.center(), 1.0f);
                    }
                }
            }
            break;
    }
}

void CrumblePlatformEntity::onPlayerRespawn(GameWorld& w) {
    phase_ = Phase::Solid;
    w.physics().setEnabled(collider_, true);
}

void CrumblePlatformEntity::render(RenderContext& ctx) {
    if (!tex_ || phase_ == Phase::Fallen) {
        if (ctx.editor && tex_) drawThreeSlice(ctx, *tex_, 0, box);
        return;
    }
    int row = 0;
    Rect r = box;
    if (phase_ == Phase::Shaking) {
        float k = 1.0f - timer_ / std::max(0.05f, obj_.prop<float>("delay", 0.45f));
        row = k > 0.5f ? 2 : 1;
        r.x += std::sin(ctx.time * 70.0f) * 1.0f;
    }
    drawThreeSlice(ctx, *tex_, row, r);
}

// --------------------------------------------------------------- power gate

void PowerGateEntity::init(GameWorld& w) {
    power_ = obj_.propStr("power", "dash");
    uint32_t tags = ColliderTag::Gate;
    if (power_ == "dash") tags |= ColliderTag::Breakable;
    if (power_ == "teleport") tags |= ColliderTag::Phase;
    if (power_ == "shield") tags |= ColliderTag::ShieldPass;
    collider_ = w.physics().add(box, ColliderKind::Solid, tags, this);
    if (power_ == "dash" || power_ == "impulse") sprite_.load(w.resources(), "animations/breakable_wall.json", "intact");
    if (power_ == "teleport") sprite_.load(w.resources(), "animations/phase_wall.json", "idle");
    if (power_ == "ground_pound") sprite_.load(w.resources(), "animations/cracked_floor.json", "intact");
    beam_ = w.resources().texture("sprites/objects/laser_beam.png");
    icons_ = w.resources().texture("ui/icons_powers.png");
}

void PowerGateEntity::fixedUpdate(GameWorld& w, float dt) {
    hintCooldown_ = std::max(0.0f, hintCooldown_ - dt);
    if (broken_) return;
    sprite_.anim.update(dt);
    // Plasma curtains hurt when crossed without the shield.
    if (power_ == "shield" && w.player().alive() && !w.player().shieldActive() &&
        box.expanded(1).intersects(w.player().box)) {
        DamageInfo d;
        d.amount = 1;
        d.type = DamageType::Hazard;
        d.source = box.center();
        w.damagePlayer(d);
        if (hintCooldown_ <= 0.0f) {
            w.hint("Un rideau de plasma : activez le Bouclier pour le traverser.", 3.0f);
            hintCooldown_ = 6.0f;
        }
    }
    if (power_ == "teleport" && hintCooldown_ <= 0.0f && box.expanded(4).intersects(w.player().box) &&
        !w.player().hasPower("teleport")) {
        w.hint("Cette barrière de phase semble traversable... par téléportation.", 3.0f);
        hintCooldown_ = 8.0f;
    }
}

void PowerGateEntity::destroy(GameWorld& w) {
    broken_ = true;
    w.physics().setEnabled(collider_, false);
    const char* fx = power_ == "dash" ? "crystal_shatter"
                     : (power_ == "impulse" || power_ == "ground_pound") ? "rock_shatter"
                                                                        : "barrier_dissolve";
    w.particles().emit(fx, box.center(), std::max(1.0f, box.h / 48.0f));
    w.playSound(power_ == "wave" ? "sounds/door_open.wav" : "sounds/explosion.wav", box.center(), 0.8f);
    w.shake(0.3f);
    w.hitStop(0.05f);
    w.emit(id(), "activate");
}

bool PowerGateEntity::onPowerHit(GameWorld& w, const std::string& power, const Rect& area) {
    if (broken_ || !area.intersects(box.expanded(2))) return false;
    if (power == power_ && (power_ == "dash" || power_ == "wave" || power_ == "impulse" || power_ == "ground_pound")) {
        destroy(w);
        return true;
    }
    if (power == "charge_shot" && power_ == "wave") {
        destroy(w);
        return true;
    }
    if (power == "melee" && (power_ == "dash" || power_ == "impulse" || power_ == "ground_pound") && hintCooldown_ <= 0.0f) {
        crack_ = 1.0f;
        w.particles().emit("block_sparks", area.center(), 0.7f);
        w.playSound("sounds/shield_block.wav", area.center(), 0.6f);
        w.hint(power_ == "dash"     ? "Ce cristal est trop solide... un Dash pourrait le briser."
               : power_ == "impulse" ? "Cette roche fissurée cédera sous une Impulsion."
                                     : "Ce sol fissuré cédera sous une Frappe sismique (Bas + Attaque en l'air).",
               3.0f);
        hintCooldown_ = 5.0f;
    }
    return false;
}

bool PowerGateEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "break" || action == "open" || action == "disable") {
        if (!broken_) destroy(w);
        return true;
    }
    if (action == "close" || action == "enable") {
        broken_ = false;
        w.physics().setEnabled(collider_, true);
        return true;
    }
    return false;
}

void PowerGateEntity::onPlayerRespawn(GameWorld& w) {
    if (broken_ && obj_.prop<bool>("respawn", false)) {
        broken_ = false;
        w.physics().setEnabled(collider_, true);
    }
}

void PowerGateEntity::render(RenderContext& ctx) {
    if (broken_ && !ctx.editor) return;
    Color iconTint{255, 255, 255, 150};
    if (power_ == "dash" || power_ == "impulse" || power_ == "ground_pound") {
        // Tile the crystal wall / cracked slab vertically and horizontally to the gate size.
        if (sprite_.set && sprite_.set->texture) {
            const auto* clip = sprite_.set->clip(crack_ > 0 ? "cracked" : "intact");
            if (!clip) clip = sprite_.set->clip("intact");
            Rect src = clip ? sprite_.set->frameRect(*clip, 0) : Rect{0, 0, 32, 64};
            Color tint = power_ == "impulse" ? Color{255, 200, 150, 255} : Color::white();
            for (float y = box.y; y < box.bottom() - 1; y += src.h) {
                float h = std::min(src.h, box.bottom() - y);
                for (float x = box.x; x < box.right() - 1; x += src.w) {
                    float wdt = std::min(src.w, box.right() - x);
                    Rect s{src.x, src.y + src.h - h, wdt, h};
                    ctx.r.drawWorldTexture(*sprite_.set->texture, &s, {x, y, wdt, h}, tint);
                }
            }
            Color glow = power_ == "dash" ? Color{176, 76, 255, 70} : Color{255, 179, 71, 70};
            ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.8f, glow);
        }
    } else if (power_ == "teleport") {
        if (sprite_.set && sprite_.set->texture) {
            Rect src = sprite_.anim.sourceRect();
            for (float y = box.y; y < box.bottom() - 1; y += src.h) {
                float h = std::min(src.h, box.bottom() - y);
                Rect s{src.x, src.y, src.w, h};
                ctx.r.drawWorldTexture(*sprite_.set->texture, &s, {box.center().x - src.w * 0.5f, y, src.w, h},
                                       Color::white(), BlendMode::Additive);
            }
        }
        ctx.lights.add(box.center(), box.h * 0.7f, {176, 76, 255, 255});
    } else if (beam_) {
        // Energy barrier (wave) or plasma curtain (shield): vertical animated beams.
        bool shield = power_ == "shield";
        Color c = shield ? Color{255, 100, 200, 220} : Color{57, 240, 216, 220};
        int frame = static_cast<int>(ctx.time * 12.0f) % 4;
        Rect src{frame * 16.0f, 0, 16, 8};
        float cx = box.center().x;
        for (float y = box.y; y < box.bottom(); y += 16.0f) {
            float h = std::min(16.0f, box.bottom() - y);
            Vec2 pivot{8.0f * ctx.r.view().scale, 4.0f * ctx.r.view().scale};
            (void)pivot;
            ctx.r.drawWorldTexture(*beam_, &src, {cx - h * 0.5f, y + h * 0.5f - 4.0f, h, std::min(box.w, 8.0f)}, c,
                                   BlendMode::Additive, false, 90.0f);
        }
        ctx.r.fillWorldRect(box, c.withAlpha(40), BlendMode::Additive);
        ctx.lights.add(box.center(), box.h * 0.6f, c);
        iconTint = c;
    }
    // Power symbol hovering over the gate.
    if (icons_ && !broken_) {
        int idx = power_ == "dash" ? 0 : power_ == "wave" ? 1 : power_ == "shield" ? 2 : power_ == "impulse" ? 3 : 4;
        Rect src{idx * 32.0f, 0, 32, 32};
        float pulse = 0.6f + 0.4f * std::sin(ctx.time * 3.0f);
        ctx.r.drawWorldTexture(*icons_, &src, Rect::fromCenter(box.center(), {12, 12}), iconTint.withAlphaF(0.55f * pulse));
    }
}

// --------------------------------------------------------------- bounce pad

void BouncePadEntity::init(GameWorld& w) { sprite_.load(w.resources(), "animations/bounce_pad.json", "idle"); }

void BouncePadEntity::update(GameWorld& /*w*/, float dt) { sprite_.anim.update(dt); }

void BouncePadEntity::onPlayerStay(GameWorld& w, float dt) {
    cooldown_ = std::max(0.0f, cooldown_ - dt);
    Player& p = w.player();
    if (cooldown_ > 0.0f || p.vel.y < -10.0f) return;
    if (p.box.bottom() > box.y + 10.0f) return;
    float power = obj_.prop<float>("power", 430.0f);
    p.launch({p.vel.x, -power}, true);
    cooldown_ = 0.2f;
    sprite_.anim.play("bounce", true);
    w.playSound("sounds/bounce.wav", box.center(), 0.9f, w.rng().range(0.95f, 1.05f));
    w.particles().emit("bounce_spores", {box.center().x, box.y + 4}, 1.0f);
    w.emit(id(), "activate");
}

void BouncePadEntity::render(RenderContext& ctx) {
    sprite_.draw(ctx, box.bottomCenter());
    ctx.lights.add(box.center(), 40.0f, {224, 138, 255, 255});
}

// ------------------------------------------------------------------ ladder

void LadderEntity::init(GameWorld& w) {
    auto& atlas = DecorAtlas::shared(w.resources());
    vine_ = atlas.find("hanging_vine_long");
    tex_ = vine_ ? vine_->texture : atlas.texture();
}

void LadderEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    w.player().onLadder = true;
    w.player().ladderRect = box;
}

void LadderEntity::render(RenderContext& ctx) {
    if (tex_ && vine_) {
        Rect src = vine_->rect;
        int strands = std::max(1, static_cast<int>(box.w / 10.0f));
        for (int s = 0; s < strands; ++s) {
            float cx = box.x + (s + 0.5f) * box.w / strands;
            for (float y = box.y; y < box.bottom() - 1; y += src.h) {
                float h = std::min(src.h, box.bottom() - y);
                Rect sr{src.x, src.y, src.w, h};
                ctx.r.drawWorldTexture(*tex_, &sr, {cx - src.w * 0.5f, y, src.w, h});
            }
        }
    } else {
        ctx.r.fillWorldRect(box, {53, 181, 131, 120});
    }
    ctx.lights.add(box.center(), box.h * 0.4f, {31, 138, 106, 255});
}

}  // namespace cosmic
