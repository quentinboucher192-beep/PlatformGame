// Platforms and terrain-like entities: moving / crumbling platforms, power gates,
// bounce pads and climbable vines.

#include <cmath>

#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/core/DetMath.h"

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

Vec2 MovingPlatformEntity::orbitPos(float angle) const {
    float rx = std::abs(obj_.prop<float>("dx", 128.0f)), ry = std::abs(obj_.prop<float>("dy", 0.0f));
    return Vec2{center_.x + dmath::cos(angle) * rx - box.w * 0.5f, center_.y + dmath::sin(angle) * ry - box.h * 0.5f};
}

void MovingPlatformEntity::init(GameWorld& w) {
    bubble_ = obj_.type == "bubble";
    start_ = box.pos();
    end_ = start_ + Vec2{obj_.prop<float>("dx", 128.0f), obj_.prop<float>("dy", 0.0f)};
    length_ = std::max(1.0f, distance(start_, end_));
    moving_ = obj_.prop<bool>("active", true);
    triggered_ = obj_.prop<bool>("triggered", false) || bubble_;
    if (triggered_) moving_ = false;
    t_ = std::clamp(obj_.prop<float>("phase", 0.0f), 0.0f, 1.0f);
    box = Rect{lerpf(start_.x, end_.x, t_), lerpf(start_.y, end_.y, t_), box.w, box.h};
    orbit_ = obj_.prop<bool>("orbit", false);
    if (orbit_) {
        // 2.33: around its place (the centre of its rectangle), from the angle `phase` (0..1).
        center_ = obj_.rect.center();
        angle_ = t_ * 2.0f * kPi;
        Vec2 p = orbitPos(angle_);
        box.x = p.x;
        box.y = p.y;
    }
    prevPos_ = box.pos();
    bool oneway = obj_.prop<bool>("oneway", false) || bubble_;
    Rect col = oneway ? Rect{box.x, box.y, box.w, 4.0f} : box;
    collider_ = w.physics().add(col, oneway ? ColliderKind::OneWay : ColliderKind::Solid, ColliderTag::Moving, this, true);
    if (!bubble_) tex_ = w.resources().texture("sprites/objects/moving_platform.png");
}

void MovingPlatformEntity::bubbleStep(GameWorld& w, float dt) {
    prevPos_ = box.pos();
    delta_ = {};
    if (popped_) {
        reborn_ -= dt;
        if (reborn_ > 0.0f) return;
        // Born again where it was, waiting for Nova.
        popped_ = false;
        moving_ = false;
        t_ = 0.0f;
        box.x = start_.x;
        box.y = start_.y;
        prevPos_ = box.pos();
        w.physics().setEnabled(collider_, true);
        w.physics().setDynamicBox(collider_, {box.x, box.y, box.w, 4.0f}, {});
        return;
    }
    if (!moving_ && w.player().groundCollider() == collider_) moving_ = true;
    if (moving_) {
        t_ = std::min(1.0f, t_ + obj_.prop<float>("speed", 55.0f) * dt / length_);
        Vec2 p{lerpf(start_.x, end_.x, t_), lerpf(start_.y, end_.y, t_)};
        delta_ = p - box.pos();
        box.x = p.x;
        box.y = p.y;
    }
    w.physics().setDynamicBox(collider_, {box.x, box.y, box.w, 4.0f}, delta_ / std::max(dt, 1e-4f));
    if (t_ >= 1.0f) {
        // The end of its route: it pops (Nova falls if she is still on it).
        popped_ = true;
        reborn_ = std::max(0.2f, obj_.prop<float>("respawn", 1.2f));
        w.physics().setEnabled(collider_, false);
        burstAt_ = {box.center().x, box.y + box.w * 0.5f};
        burst_ = 0.0f;
        w.particles().emit("bubbles", burstAt_, 1.0f);
        w.playSound("sounds/bounce.wav", burstAt_, 0.5f, 1.6f);
        w.emit(id(), "activate");
    }
}

void MovingPlatformEntity::update(GameWorld& /*w*/, float dt) {
    if (!bubble_) return;
    // Display only: it grows back after a pop, its burst fades.
    shown_ = popped_ ? 0.0f : std::min(1.0f, shown_ + dt / 0.35f);
    if (popped_) burst_ += dt;
}

void MovingPlatformEntity::renderBubble(RenderContext& ctx) const {
    const float r = box.w * 0.5f;
    if (popped_) {
        // The burst: a ring that widens and fades.
        const float k = std::clamp(burst_ / 0.3f, 0.0f, 1.0f);
        if (k < 1.0f) ctx.r.drawWorldCircle(burstAt_, r * (1.0f + 0.6f * k), Color{230, 250, 255, 255}.withAlphaF(1.0f - k), 2.0f);
        return;
    }
    Vec2 p = lerpv(prevPos_, box.pos(), ctx.alpha);
    const float grow = 0.35f + 0.65f * applyEase(Ease::OutBack, shown_);
    const float wob = 1.0f + 0.04f * dmath::sin(ctx.time * 5.0f + start_.x * 0.01f);
    const float rx = r * grow * wob, ry = r * grow / wob;
    // The top of the bubble is its standing line (box.y): it hangs under it.
    Vec2 c{p.x + box.w * 0.5f, p.y + ry - 2.0f};
    ctx.r.fillWorldCircle(c, std::max(rx, ry), Color{150, 220, 255, 60});
    ctx.r.drawWorldCircle(c, std::max(rx, ry), Color{220, 245, 255, 190}, 1.5f);
    ctx.r.fillWorldCircle(c + Vec2{-rx * 0.4f, -ry * 0.45f}, std::max(2.0f, r * 0.18f * grow), Color{255, 255, 255, 200});
    ctx.r.drawWorldGlow(c, r * 1.2f, Color{140, 210, 255, 70});
    ctx.lights.add(c, r * 2.2f, {170, 225, 255, 255});
    if (ctx.editor) {
        Vec2 a = obj_.rect.center(), b = a + Vec2{obj_.prop<float>("dx", 0), obj_.prop<float>("dy", 0)};
        ctx.r.drawWorldLine(a, b, {150, 220, 255, 160}, 1.0f);
        ctx.r.drawWorldCircle(b, r * 0.5f, {150, 220, 255, 120}, 1.0f);
    }
}

void MovingPlatformEntity::fixedUpdate(GameWorld& w, float dt) {
    if (bubble_) {
        bubbleStep(w, dt);
        return;
    }
    prevPos_ = box.pos();
    if (triggered_ && !moving_ && w.player().groundCollider() == collider_) moving_ = true;
    if (orbit_) {
        // In orbit: the speed along the ellipse (px/s), the sign of dx giving the direction.
        if (moving_) {
            float rx = std::abs(obj_.prop<float>("dx", 128.0f)), ry = std::abs(obj_.prop<float>("dy", 0.0f));
            float r = std::max(8.0f, (rx + ry) * 0.5f);
            float dir = obj_.prop<float>("dx", 128.0f) < 0.0f ? -1.0f : 1.0f;
            angle_ += dir * obj_.prop<float>("speed", 60.0f) / r * dt;
            if (angle_ > 2.0f * kPi) angle_ -= 2.0f * kPi;
            if (angle_ < 0.0f) angle_ += 2.0f * kPi;
        }
        Vec2 p = orbitPos(angle_);
        delta_ = p - box.pos();
        box.x = p.x;
        box.y = p.y;
        bool oneway = obj_.prop<bool>("oneway", false);
        Rect col = oneway ? Rect{box.x, box.y, box.w, 4.0f} : box;
        w.physics().setDynamicBox(collider_, col, delta_ / std::max(dt, 1e-4f));
        return;
    }
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
    if (bubble_) {
        renderBubble(ctx);
        return;
    }
    if (!tex_) return;
    Vec2 p = lerpv(prevPos_, box.pos(), ctx.alpha);
    Rect r{p.x, p.y, box.w, 16.0f};
    drawThreeSlice(ctx, *tex_, moving_ ? 1 : 0, r);
    // Under-glow of the anti-gravity engines.
    Color glow = moving_ ? Color{255, 179, 71, 140} : Color{120, 90, 60, 80};
    ctx.r.drawWorldGlow({r.center().x, r.bottom() + 2}, box.w * 0.45f, glow);
    ctx.lights.add({r.center().x, r.bottom()}, box.w * 0.9f, {255, 160, 80, 255});
    if (ctx.editor && orbit_) {
        float rx = std::abs(obj_.prop<float>("dx", 0)), ry = std::abs(obj_.prop<float>("dy", 0));
        Vec2 c = obj_.rect.center(), last = c + Vec2{rx, 0.0f};
        for (int k = 1; k <= 32; ++k) {
            float a = 2.0f * kPi * static_cast<float>(k) / 32.0f;
            Vec2 q = c + Vec2{dmath::cos(a) * rx, dmath::sin(a) * ry};
            ctx.r.drawWorldLine(last, q, {255, 179, 71, 160}, 1.0f);
            last = q;
        }
    } else if (ctx.editor) {
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

std::string MovingPlatformEntity::debugInfo() const {
    if (bubble_) return popped_ ? "éclatée" : moving_ ? "en route" : "attend Nova";
    return moving_ ? "en mouvement" : "arrêtée";
}

// --------------------------------------------------------- crumble platform

void CrumblePlatformEntity::init(GameWorld& w) {
    collider_ = w.physics().add(box, ColliderKind::Solid, ColliderTag::Crumble, this);
    // Planet look: ruins (Sylvanox), basalt (Pyralis) or ice (Glacior).
    std::string style = obj_.propStr("style", "ruins");
    ember_ = style == "ember";  // 2.56: drawn by the game (the basalt sheet only in the editor)
    tex_ = ember_ ? w.resources().texture("sprites/objects/crumble_platform_basalt.png")
                  : w.resources().texture(style == "ruins" ? std::string("sprites/objects/crumble_platform.png")
                                                           : "sprites/objects/crumble_platform_" + style + ".png");
    if (!tex_) tex_ = w.resources().texture("sprites/objects/crumble_platform.png");
}

void CrumblePlatformEntity::giveWay(GameWorld& w) {
    phase_ = Phase::Fallen;
    timer_ = obj_.prop<float>("respawn", 3.0f);
    w.physics().setEnabled(collider_, false);
    w.particles().emit(ember_ ? "ash_cloud" : "crumble_debris", box.center(), std::max(1.0f, box.w / 48.0f));
    w.playSound("sounds/crumble.wav", box.center(), 0.9f);
}

bool CrumblePlatformEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    // 2.33: an event breaks it at once (Singularis's floor collapsing).
    if (action != "break" && action != "activate" && action != "open") return false;
    if (phase_ != Phase::Fallen) giveWay(w);
    return true;
}

void CrumblePlatformEntity::fixedUpdate(GameWorld& w, float dt) {
    switch (phase_) {
        case Phase::Solid:
            if (obj_.prop<bool>("onEvent", false)) break;  // 2.33: gives way to an event only
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
                // 2.56: a perch of embers burns to ash (display only).
                w.particles().emit(ember_ ? "ash_cloud" : "crumble_debris", box.center(), std::max(1.0f, box.w / 48.0f));
                if (ember_) w.particles().emit("fire_burst", box.center(), 1.2f);
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
                        if (ember_) w.particles().emit("embers_rising", box.center(), 1.2f);  // 2.56: reborn from its ash
                    }
                }
            }
            break;
    }
}

void CrumblePlatformEntity::onPlayerRespawn(GameWorld& w) {
    if (obj_.prop<bool>("onEvent", false)) return;  // broken by the fight: stays broken (2.33)
    phase_ = Phase::Solid;
    w.physics().setEnabled(collider_, true);
}

void CrumblePlatformEntity::render(RenderContext& ctx) {
    if (ember_ && !ctx.editor) {
        renderEmber(ctx);
        return;
    }
    if (!tex_ || phase_ == Phase::Fallen) {
        if (ctx.editor && tex_) drawThreeSlice(ctx, *tex_, 0, box);
        return;
    }
    int row = 0;
    Rect r = box;
    if (phase_ == Phase::Shaking) {
        float k = 1.0f - timer_ / std::max(0.05f, obj_.prop<float>("delay", 0.45f));
        row = k > 0.5f ? 2 : 1;
        r.x += dmath::sin(ctx.time * 70.0f) * 1.0f;
    }
    drawThreeSlice(ctx, *tex_, row, r);
}

void CrumblePlatformEntity::renderEmber(RenderContext& ctx) const {
    // A perch of embers (2.56, the Phénix's nest): charcoal veined with fire. Under Nova it
    // catches fire (flames rising, it trembles), burns to ash; while it is gone a glowing outline
    // of ash gathers where it was, brighter as it is about to be reborn. Display only.
    const float t = ctx.time;
    const float respawn = obj_.prop<float>("respawn", 3.0f);
    if (phase_ == Phase::Fallen) {
        if (respawn <= 0.0f) return;
        const float k = 1.0f - std::clamp(timer_ / std::max(0.05f, respawn), 0.0f, 1.0f);
        const float pulse = 0.5f + 0.5f * dmath::sin(t * (4.0f + 10.0f * k));
        const Color ash{255, 140, 70, static_cast<uint8_t>((30.0f + 150.0f * k) * (0.6f + 0.4f * pulse))};
        ctx.r.drawWorldRect(box, ash, 1.0f + 1.5f * k);
        for (float x = box.x + 6.0f; x < box.right() - 2.0f; x += 12.0f)
            ctx.r.fillWorldRect({x, box.y + box.h * 0.5f - 1.0f, 4.0f, 2.0f}, ash, BlendMode::Additive);
        if (k > 0.6f) ctx.lights.add(box.center(), 50.0f * k, Color{255, 150, 80, 255});
        return;
    }
    Rect r = box;
    float burn = 0.0f;
    if (phase_ == Phase::Shaking) {
        burn = 1.0f - timer_ / std::max(0.05f, obj_.prop<float>("delay", 0.45f));
        r.x += dmath::sin(t * 70.0f) * 1.0f;
    }
    // Charcoal, a lighter top edge, fiery seams pulsing slowly.
    ctx.r.fillWorldRect(r, Color{40, 30, 30, 255});
    ctx.r.fillWorldRect({r.x, r.y, r.w, 3.0f}, Color{92, 70, 62, 255});
    ctx.r.fillWorldRect({r.x, r.bottom() - 2.0f, r.w, 2.0f}, Color{22, 16, 18, 255});
    const float glow = 0.55f + 0.25f * dmath::sin(t * 2.2f + box.x * 0.05f) + 0.45f * burn;
    const Color seam{255, static_cast<uint8_t>(120 + 80 * burn), 50, static_cast<uint8_t>(std::clamp(150.0f * glow, 0.0f, 255.0f))};
    for (float x = r.x + 5.0f; x < r.right() - 4.0f; x += 11.0f) {
        const float y0 = r.y + 4.0f + std::fmod(x * 0.37f, 5.0f);
        ctx.r.drawWorldLine({x, y0}, {x + 6.0f, std::min(r.bottom() - 2.0f, y0 + 5.0f)}, seam, std::max(1.0f, ctx.r.view().scale * 0.5f),
                            BlendMode::Additive);
    }
    ctx.lights.add(r.center(), 40.0f + 50.0f * burn, Color{255, 140, 60, static_cast<uint8_t>(150 + 100 * burn)});
    if (burn > 0.0f) {
        // On fire: tongues of flame along its top.
        for (float x = r.x + 4.0f; x < r.right() - 2.0f; x += 8.0f) {
            const float h = (6.0f + 10.0f * burn) * (0.6f + 0.4f * dmath::sin(t * 18.0f + x));
            ctx.r.drawWorldLine({x, r.y}, {x + dmath::sin(t * 9.0f + x) * 2.0f, r.y - h}, Color{255, 170, 60, static_cast<uint8_t>(200 * burn)},
                                std::max(1.0f, ctx.r.view().scale * 1.2f), BlendMode::Additive);
        }
        ctx.r.drawWorldGlow(r.center() + Vec2{0.0f, -4.0f}, r.w * 0.6f, Color{255, 120, 50, static_cast<uint8_t>(120 * burn)});
    }
}

// ------------------------------------------------------------------- altar

void AltarEntity::init(GameWorld& w) {
    // Its top can be stood on, as a ledge (2.57).
    collider_ = w.physics().add({box.x, box.y, box.w, 4.0f}, ColliderKind::OneWay, 0, this);
}

void AltarEntity::update(GameWorld& w, float dt) {
    // Display only: the light of the Liche's phase, brighter when it floats over this altar,
    // brightest when it is about to appear here or when the phylactery rests on it.
    float want = 0.15f;
    crystal_ = false;
    if (const Enemy* b = w.activeBoss(); b && b->alive() && b->isLiche()) {
        color_ = b->lightColor();
        const auto& alt = b->altars();
        for (int k = 0; k < static_cast<int>(alt.size()); ++k) {
            if (std::abs(alt[static_cast<size_t>(k)].x - box.center().x) > 1.0f || std::abs(alt[static_cast<size_t>(k)].y - box.y) > 1.0f) continue;
            if (k == b->altarIndex()) want = 0.8f;
            if (k == b->altarNext()) want = 1.5f;
            if (k == b->phylacteryAltar()) {
                want = 1.3f;
                crystal_ = true;
            }
        }
    }
    glow_ = approach(glow_, want, dt * 2.5f);
}

void AltarEntity::render(RenderContext& ctx) {
    // A block of dark carved stone: a slab on top, a plinth, a skull carved in its front between
    // two columns of runes, two candles of soul fire on its top. Display only.
    const Rect b = box;
    const float t = ctx.time;
    const float g = ctx.editor ? 0.5f : glow_;
    const Color soul = color_;
    const float px = std::max(1.0f, ctx.r.view().scale * 0.5f);
    ctx.r.fillWorldRect({b.x + 3.0f, b.y + 5.0f, b.w - 6.0f, b.h - 9.0f}, Color{40, 36, 48, 255});
    ctx.r.fillWorldRect({b.x + 3.0f, b.y + 5.0f, 3.0f, b.h - 9.0f}, Color{62, 56, 72, 255});
    ctx.r.fillWorldRect({b.right() - 6.0f, b.y + 5.0f, 3.0f, b.h - 9.0f}, Color{26, 22, 32, 255});
    ctx.r.fillWorldRect({b.x, b.y, b.w, 6.0f}, Color{70, 64, 82, 255});           // the slab
    ctx.r.fillWorldRect({b.x, b.y, b.w, 2.0f}, Color{104, 96, 118, 255});
    ctx.r.fillWorldRect({b.x - 1.0f, b.bottom() - 5.0f, b.w + 2.0f, 5.0f}, Color{52, 46, 60, 255});  // the plinth
    ctx.r.fillWorldRect({b.x - 1.0f, b.bottom() - 5.0f, b.w + 2.0f, 1.0f}, Color{84, 76, 96, 255});
    // The skull carved in its front.
    const Vec2 sk{b.center().x, b.y + b.h * 0.48f};
    ctx.r.fillWorldCircle(sk, 6.5f, Color{112, 104, 122, 255});
    ctx.r.fillWorldRect({sk.x - 4.0f, sk.y + 3.0f, 8.0f, 5.0f}, Color{112, 104, 122, 255});
    const Color eye{static_cast<uint8_t>(std::min(255.0f, soul.r * (0.4f + 0.6f * std::min(1.0f, g)))),
                    static_cast<uint8_t>(std::min(255.0f, soul.g * (0.4f + 0.6f * std::min(1.0f, g)))),
                    static_cast<uint8_t>(std::min(255.0f, soul.b * (0.4f + 0.6f * std::min(1.0f, g)))), 255};
    ctx.r.fillWorldCircle(sk + Vec2{-2.6f, -0.5f}, 1.8f, eye);
    ctx.r.fillWorldCircle(sk + Vec2{2.6f, -0.5f}, 1.8f, eye);
    for (float x = -3.0f; x <= 3.0f; x += 2.0f) ctx.r.fillWorldRect({sk.x + x - 0.5f, sk.y + 5.0f, 1.0f, 3.0f}, Color{40, 36, 48, 255});
    // Columns of runes on both sides of the skull.
    const uint8_t ra = static_cast<uint8_t>(std::clamp(60.0f + 150.0f * g + 30.0f * dmath::sin(t * 3.0f + b.x), 0.0f, 255.0f));
    for (float side : {-1.0f, 1.0f}) {
        const float x = b.center().x + side * (b.w * 0.5f - 9.0f);
        for (int k = 0; k < 3; ++k) {
            const float y = b.y + 11.0f + static_cast<float>(k) * 7.0f;
            const int kind = (static_cast<int>(b.x / 16.0f) + k + (side > 0 ? 1 : 0)) % 3;
            const Color rc = soul.withAlpha(ra);
            if (kind == 0) ctx.r.drawWorldLine({x - 2.0f, y}, {x + 2.0f, y + 4.0f}, rc, px, BlendMode::Additive);
            else if (kind == 1) ctx.r.drawWorldCircle({x, y + 2.0f}, 2.0f, rc, px);
            else ctx.r.drawWorldLine({x, y}, {x, y + 4.0f}, rc, px, BlendMode::Additive);
        }
    }
    // Two candles of soul fire on its top.
    for (float side : {-1.0f, 1.0f}) {
        const float x = b.center().x + side * (b.w * 0.5f - 6.0f);
        ctx.r.fillWorldRect({x - 1.5f, b.y - 7.0f, 3.0f, 7.0f}, Color{214, 206, 184, 255});
        const float fl = 1.0f + 0.25f * dmath::sin(t * 13.0f + x);
        ctx.r.drawWorldGlow({x, b.y - 9.5f}, 3.0f * fl + 2.0f * g, soul.withAlpha(220));
        ctx.r.fillWorldCircle({x, b.y - 9.0f}, 1.2f, Color{240, 255, 244, 255});
    }
    if (g > 0.2f) ctx.r.drawWorldGlow({b.center().x, b.y + b.h * 0.4f}, b.w * 0.5f * g, soul.withAlpha(static_cast<uint8_t>(std::min(255.0f, 70.0f * g))));
    if (crystal_) {
        const float pulse = 0.5f + 0.5f * dmath::sin(t * 6.0f);
        ctx.r.drawWorldCircle({b.center().x, b.y - 2.0f}, b.w * (0.55f + 0.15f * pulse), soul.withAlpha(static_cast<uint8_t>(120 + 100 * pulse)), px);
    }
    ctx.lights.add({b.center().x, b.y - 6.0f}, 50.0f + 50.0f * g, soul);
}

// ------------------------------------------------------- the Titan's foundry (2.58)

namespace {
/// The light of the Titan's phase when it fights in this level (amber otherwise).
Color foundryLight(const GameWorld& w) {
    if (const Enemy* b = w.activeBoss(); b && b->alive() && b->isTitan()) return b->lightColor();
    return {255, 194, 60, 255};
}
}  // namespace

void ConveyorEntity::update(GameWorld& w, float dt) {
    // Display only: its slats run with the belt the Titan set going on this strip.
    color_ = foundryLight(w);
    speed_ = 0.0f;
    warn_ = 0.0f;
    if (const BossHazards::Belt* b = w.hazards().beltOn(box)) {
        speed_ = b->speed * b->running();
        warn_ = b->age < b->warn ? 1.0f : 0.0f;
    }
    scroll_ = std::fmod(scroll_ + speed_ * dt + 1600.0f, 16.0f);
}

void ConveyorEntity::render(RenderContext& ctx) {
    // A belt of dark rubber over the floor (its top at the surface), steel slats running on it,
    // a roller at each end, lamps along its edge (lit and flashing before it starts).
    const Rect b = box;
    const float y = b.y;
    ctx.r.fillWorldRect({b.x, y - 1.0f, b.w, 7.0f}, Color{26, 26, 30, 255});
    for (float x = b.x - 16.0f + scroll_; x < b.right(); x += 16.0f) {
        const float x0 = std::max(b.x + 2.0f, x), x1 = std::min(b.right() - 2.0f, x + 9.0f);
        if (x1 > x0) ctx.r.fillWorldRect({x0, y, x1 - x0, 3.0f}, Color{86, 92, 104, 255});
    }
    ctx.r.fillWorldRect({b.x, y + 3.0f, b.w, 1.0f}, Color{58, 60, 68, 255});
    for (float rx : {b.x + 3.0f, b.right() - 3.0f}) {
        ctx.r.fillWorldCircle({rx, y + 3.0f}, 4.0f, Color{120, 126, 138, 255});
        ctx.r.fillWorldCircle({rx, y + 3.0f}, 1.6f, Color{200, 170, 90, 255});
    }
    const bool on = warn_ > 0.0f ? (static_cast<int>(ctx.time * 10.0f) % 2 == 0) : std::abs(speed_) > 1.0f;
    for (float x = b.x + 24.0f; x < b.right() - 16.0f; x += 64.0f) {
        const Color lamp = on ? color_ : Color{70, 60, 40, 255};
        ctx.r.fillWorldRect({x - 2.0f, y + 4.0f, 4.0f, 2.0f}, lamp);
        if (on) ctx.r.drawWorldGlow({x, y + 5.0f}, 6.0f, color_.withAlpha(120));
    }
}

void PressEntity::update(GameWorld& w, float dt) {
    // Display only: the Titan's press there draws its own head while it is out.
    color_ = foundryLight(w);
    busy_ = false;
    for (const BossHazards::Press& p : w.hazards().presses())
        if (!p.safe && std::abs(p.x - box.center().x) < 2.0f) busy_ = true;
    hiss_ = std::max(0.0f, hiss_ - dt);
    if (busy_) hiss_ = 0.3f;
}

void PressEntity::render(RenderContext& ctx) {
    // Its housing in the roof: a block of riveted steel, two hydraulic cylinders, a lamp; its head
    // at rest under it (when the Titan's press is not out).
    const Rect b = box;
    ctx.r.fillWorldRect(b, Color{34, 36, 42, 255});
    ctx.r.fillWorldRect({b.x + 2.0f, b.y, b.w - 4.0f, b.h - 3.0f}, Color{72, 78, 90, 255});
    ctx.r.fillWorldRect({b.x + 2.0f, b.bottom() - 6.0f, b.w - 4.0f, 3.0f}, Color{240, 190, 40, 255});
    for (float x = b.x + 6.0f; x < b.right() - 4.0f; x += 12.0f) ctx.r.fillWorldRect({x, b.bottom() - 6.0f, 6.0f, 3.0f}, Color{30, 30, 34, 255});
    for (float cx : {b.x + b.w * 0.25f, b.right() - b.w * 0.25f}) {
        ctx.r.fillWorldRect({cx - 5.0f, b.y + 3.0f, 10.0f, b.h - 10.0f}, Color{110, 118, 132, 255});
        ctx.r.fillWorldRect({cx - 2.0f, b.y + 3.0f, 3.0f, b.h - 10.0f}, Color{200, 208, 220, 255});
    }
    for (float rx : {b.x + 4.0f, b.right() - 4.0f}) ctx.r.fillWorldCircle({rx, b.y + 4.0f}, 1.5f, Color{200, 170, 90, 255});
    const bool lit = busy_ && static_cast<int>(ctx.time * 8.0f) % 2 == 0;
    const Vec2 lamp{b.center().x, b.y + b.h * 0.45f};
    ctx.r.fillWorldCircle(lamp, 3.0f, lit ? Color{255, 80, 50, 255} : Color{90, 50, 40, 255});
    if (lit) {
        ctx.r.drawWorldGlow(lamp, 12.0f, Color{255, 80, 50, 200});
        ctx.lights.add(lamp, 60.0f, Color{255, 80, 50, 255});
    }
    if (!busy_) {
        // Its head at rest.
        const float head = 22.0f;
        ctx.r.fillWorldRect({b.x, b.bottom(), b.w, head}, Color{40, 44, 52, 255});
        ctx.r.fillWorldRect({b.x + 2.0f, b.bottom() + 2.0f, b.w - 4.0f, head - 4.0f}, Color{92, 100, 114, 255});
        ctx.r.fillWorldRect({b.x + 2.0f, b.bottom() + 2.0f, b.w - 4.0f, 3.0f}, Color{150, 160, 176, 255});
        for (float x = b.x + 4.0f; x < b.right() - 4.0f; x += 12.0f)
            ctx.r.fillWorldRect({x, b.bottom() + head - 9.0f, std::min(6.0f, b.right() - 4.0f - x), 5.0f}, Color{240, 190, 40, 255});
    }
}

void ValveSpotEntity::update(GameWorld& w, float dt) {
    // Display only: a valve of the stalled Titan waits here.
    color_ = foundryLight(w);
    float want = 0.0f;
    if (const Enemy* b = w.activeBoss(); b && b->alive() && b->isTitan() && b->stalled())
        for (uint32_t id : b->valves())
            if (const Enemy* v = w.findEnemy(id); v && v->alive() && v->box.intersects(box.expanded(8.0f))) want = 1.0f;
    glow_ = approach(glow_, want, dt * 3.0f);
}

void ValveSpotEntity::render(RenderContext& ctx) {
    // A pipe coming out of the wall (or the floor of its ledge), a flange, a pressure gauge.
    const Rect b = box;
    const float cx = b.center().x;
    ctx.r.fillWorldRect({cx - 5.0f, b.y + 4.0f, 10.0f, b.h - 4.0f}, Color{70, 74, 84, 255});
    ctx.r.fillWorldRect({cx - 3.0f, b.y + 4.0f, 3.0f, b.h - 4.0f}, Color{130, 138, 150, 255});
    ctx.r.fillWorldRect({cx - 9.0f, b.bottom() - 4.0f, 18.0f, 4.0f}, Color{150, 120, 60, 255});
    ctx.r.fillWorldRect({cx - 8.0f, b.y + 2.0f, 16.0f, 4.0f}, Color{150, 120, 60, 255});
    const Vec2 gauge{cx + 9.0f, b.y + b.h * 0.55f};
    ctx.r.fillWorldCircle(gauge, 4.0f, Color{200, 170, 90, 255});
    ctx.r.fillWorldCircle(gauge, 3.0f, Color{235, 230, 215, 255});
    const float ang = -2.4f + 3.2f * std::min(1.0f, glow_ + 0.2f * dmath::sin(ctx.time * 9.0f) * glow_);
    ctx.r.drawWorldLine(gauge, gauge + Vec2{dmath::cos(ang), dmath::sin(ang)} * 2.6f, Color{200, 40, 30, 255}, std::max(1.0f, ctx.r.view().scale * 0.4f));
    if (glow_ > 0.01f) {
        const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 6.0f);
        ctx.r.drawWorldGlow({cx, b.y + b.h * 0.3f}, 18.0f + 6.0f * pulse, color_.withAlpha(static_cast<uint8_t>(110 * glow_)));
        ctx.lights.add({cx, b.y}, 70.0f * glow_, color_);
    }
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
        int idx = power_ == "dash" ? 0 : power_ == "wave" ? 1 : power_ == "shield" ? 2 : power_ == "impulse" ? 3 : power_ == "ground_pound" ? 6 : 4;
        Rect src{idx * 32.0f, 0, 32, 32};
        float pulse = 0.6f + 0.4f * dmath::sin(ctx.time * 3.0f);
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
    w.playSound("sounds/bounce.wav", box.center(), 0.9f, w.fxRng().range(0.95f, 1.05f));
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
