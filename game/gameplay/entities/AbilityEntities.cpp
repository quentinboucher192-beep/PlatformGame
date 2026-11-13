// Level objects used by the v2 abilities: grapple anchors and updrafts.

#include <cmath>

#include "engine/rendering/Renderer.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/player/Player.h"
#include "game/powers/Powers.h"
#include "engine/core/DetMath.h"

namespace cosmic {

// ------------------------------------------------------------ grapple point

void GrapplePointEntity::init(GameWorld& w) {
    sprite_.load(w.resources(), "animations/grapple_point.json", "idle");
    if (const PowerDefinition* def = w.content().power("grapple")) range_ = def->param("range", 170.0f);
}

void GrapplePointEntity::fixedUpdate(GameWorld& w, float dt) {
    const Player& p = w.player();
    bool now = p.alive() && p.hasPower("grapple") && GrapplePower::findTarget(p, w, range_) == this;
    if (now != targeted_ && sprite_.set) sprite_.anim.play(now && sprite_.set->clip("active") ? "active" : "idle", true);
    targeted_ = now;
    sprite_.anim.update(dt);
}

void GrapplePointEntity::render(RenderContext& ctx) {
    Vec2 c = box.center();
    if (ctx.editor) ctx.r.drawWorldCircle(c, range_, {57, 240, 216, 60});
    sprite_.draw(ctx, c);
    float pulse = 0.8f + 0.2f * dmath::sin(ctx.time * 3.0f);
    ctx.lights.add(c, (targeted_ ? 70.0f : 42.0f) * pulse, {57, 240, 216, 255});
    if (targeted_) ctx.r.drawWorldGlow(c, 18.0f, Color{143, 255, 240, 110});
}

// ------------------------------------------------------------------ updraft

void UpdraftEntity::init(GameWorld& w) {
    on_ = obj_.prop<bool>("active", true);
    if (obj_.prop<bool>("vent", true)) vent_.load(w.resources(), "animations/updraft_vent.json", "idle");
}

void UpdraftEntity::onPlayerStay(GameWorld& w, float /*dt*/) {
    if (!on_) return;
    Player& p = w.player();
    p.updraft = std::max(p.updraft, obj_.prop<float>("strength", 230.0f));
}

void UpdraftEntity::fixedUpdate(GameWorld& w, float dt) {
    vent_.anim.update(dt);
    if (!on_) return;
    fxTimer_ -= dt;
    if (fxTimer_ <= 0.0f) {
        fxTimer_ = std::max(0.03f, 0.9f / std::max(1.0f, box.w / 16.0f));
        Vec2 pos{w.fxRng().range(box.x + 2.0f, box.right() - 2.0f), w.fxRng().range(box.y + box.h * 0.3f, box.bottom())};
        w.particles().emit("updraft_wind", pos, 1.0f);
    }
}

void UpdraftEntity::render(RenderContext& ctx) {
    if (ctx.editor) ctx.r.drawWorldRect(box, {143, 255, 240, 160});
    if (on_) {
        // Faint rising column: a vertical gradient plus slow streaks.
        ctx.r.fillWorldRect(box, {143, 255, 240, 10}, BlendMode::Additive);
        for (int i = 0; i < 4; ++i) {
            float x = box.x + box.w * (0.15f + 0.23f * static_cast<float>(i));
            float t = std::fmod(ctx.time * (0.6f + 0.17f * static_cast<float>(i)) + 0.31f * static_cast<float>(i), 1.0f);
            float y = box.bottom() - t * box.h;
            float len = std::min(28.0f, box.h * 0.2f);
            ctx.r.drawWorldLine({x, y}, {x, y - len}, Color{230, 255, 251, static_cast<uint8_t>(60 * (1.0f - t))}, 1.5f,
                                BlendMode::Additive);
        }
    }
    if (vent_.set && vent_.set->texture) {
        Rect src = vent_.anim.sourceRect();
        for (float x = box.x; x < box.right() - 1.0f; x += src.w) {
            float wdt = std::min(src.w, box.right() - x);
            Rect s{src.x, src.y, wdt, src.h};
            ctx.r.drawWorldTexture(*vent_.set->texture, &s, {x, box.bottom() - src.h, wdt, src.h});
        }
        if (on_) ctx.lights.add({box.center().x, box.bottom() - 6.0f}, std::max(40.0f, box.w), {57, 240, 216, 255});
    }
}

bool UpdraftEntity::handleAction(GameWorld& /*w*/, const std::string& action, const Json& /*params*/) {
    if (action == "enable" || action == "activate" || action == "open") on_ = true;
    else if (action == "disable" || action == "close") on_ = false;
    else if (action == "toggle") on_ = !on_;
    else return false;
    return true;
}

}  // namespace cosmic
