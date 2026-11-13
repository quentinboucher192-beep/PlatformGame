// Interactive objects: checkpoints, exit portal, doors, levers, energy receptors, signs.

#include <cmath>
#include <sstream>

#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "engine/core/DetMath.h"

namespace cosmic {

namespace {
std::vector<std::string> splitIds(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ';' || c == ' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void actOnTargets(GameWorld& w, const std::string& targets, const std::string& action) {
    for (const auto& t : splitIds(targets))
        if (LevelEntity* e = w.entity(t)) e->handleAction(w, action, Json::object());
}
}  // namespace

// --------------------------------------------------------------- checkpoint

void CheckpointEntity::init(GameWorld& w) { sprite_.load(w.resources(), "animations/checkpoint.json", "inactive"); }

void CheckpointEntity::update(GameWorld& /*w*/, float dt) { sprite_.anim.update(dt); }

void CheckpointEntity::activate(GameWorld& w, bool silent) {
    if (activated_) return;
    activated_ = true;
    sprite_.anim.play("activate", true);
    w.setCheckpoint(id(), box.bottomCenter());
    if (silent) return;
    w.player().heal(w.player().maxHealth());
    w.playSound("sounds/checkpoint.wav", box.center(), 0.9f);
    w.particles().emit("checkpoint", box.center() + Vec2{0, -10}, 1.0f);
    std::string label = obj_.propStr("label");
    w.hint(label.empty() ? "Point de sauvegarde activé" : "Point de sauvegarde : " + label, 2.2f);
}

void CheckpointEntity::onPlayerEnter(GameWorld& w) { activate(w, false); }

bool CheckpointEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "activate") {
        activate(w, true);
        return true;
    }
    return false;
}

void CheckpointEntity::render(RenderContext& ctx) {
    sprite_.draw(ctx, box.bottomCenter());
    Vec2 crystal = box.bottomCenter() + Vec2{0, -38};
    if (activated_) {
        float pulse = 0.85f + 0.15f * dmath::sin(ctx.time * 3.0f);
        ctx.lights.add(crystal, 110.0f * pulse, {57, 240, 216, 255});
        ctx.r.drawWorldGlow(crystal, 16.0f * pulse, {57, 240, 216, 110});
    } else {
        ctx.lights.add(crystal, 40.0f, {60, 90, 110, 255});
    }
}

// -------------------------------------------------------------------- exit

void ExitEntity::init(GameWorld& w) {
    sprite_.load(w.resources(), "animations/exit_portal.json", "idle");
    locked_ = obj_.prop<bool>("locked", false);
}

void ExitEntity::update(GameWorld& w, float dt) {
    sprite_.anim.update(dt);
    bool near = !locked_ && distance(w.player().center(), box.center()) < 110.0f;
    if (near != near_) {
        near_ = near;
        sprite_.anim.play(near ? "active" : "idle");
    }
}

void ExitEntity::onPlayerEnter(GameWorld& w) {
    if (locked_) {
        w.hint("Le portail est scellé...", 2.0f);
        return;
    }
    w.particles().emit("portal_burst", box.center(), 1.5f);
    w.reachExit(*this);
}

bool ExitEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "open" || action == "enable" || action == "activate") {
        if (locked_) {
            locked_ = false;
            w.playSound("sounds/portal.wav", box.center(), 0.7f);
            w.particles().emit("portal_burst", box.center(), 1.0f);
        }
        return true;
    }
    if (action == "close" || action == "disable") {
        locked_ = true;
        return true;
    }
    return false;
}

void ExitEntity::render(RenderContext& ctx) {
    Color tint = locked_ ? Color{120, 120, 140, 255} : Color::white();
    sprite_.draw(ctx, box.bottomCenter(), false, tint);
    if (!locked_) {
        float pulse = 0.8f + 0.2f * dmath::sin(ctx.time * 2.0f);
        ctx.lights.add(box.center(), 160.0f * pulse, {255, 217, 138, 255});
        ctx.lights.add(box.center(), 90.0f, {57, 240, 216, 255});
        ctx.r.drawWorldGlow(box.center(), 30.0f * pulse, {255, 217, 138, 70});
    }
}

// -------------------------------------------------------------------- door

void DoorEntity::init(GameWorld& w) {
    initialOpen_ = open_ = obj_.prop<bool>("open", false);
    collider_ = w.physics().add(box, ColliderKind::Solid, ColliderTag::Door, this);
    w.physics().setEnabled(collider_, !open_);
    sprite_.load(w.resources(), "animations/door.json", open_ ? "open" : "closed");
}

void DoorEntity::update(GameWorld& /*w*/, float dt) { sprite_.anim.update(dt); }

void DoorEntity::setOpen(GameWorld& w, bool open, bool silent) {
    if (open == open_) return;
    open_ = open;
    w.physics().setEnabled(collider_, !open_);
    sprite_.anim.play(open ? "opening" : "closing", true);
    if (!silent) {
        w.playSound("sounds/door_open.wav", box.center(), 0.8f, open ? 1.0f : 0.85f);
        w.particles().emit("door_sparks", box.center(), 1.0f);
    }
    // Never close a door on the player: push them out.
    if (!open && w.player().box.intersects(box)) {
        float dir = w.player().center().x < box.center().x ? -1.0f : 1.0f;
        Rect pb = w.player().box;
        pb.x = dir < 0 ? box.x - pb.w - 0.5f : box.right() + 0.5f;
        w.player().box = pb;
    }
}

bool DoorEntity::handleAction(GameWorld& w, const std::string& action, const Json&) {
    if (action == "open") setOpen(w, true);
    else if (action == "close") setOpen(w, false);
    else if (action == "toggle" || action == "activate") setOpen(w, !open_);
    else return false;
    return true;
}

void DoorEntity::render(RenderContext& ctx) {
    if (!sprite_.set || !sprite_.set->texture) return;
    bool horizontal = std::abs(std::fmod(obj_.rotation, 180.0f)) > 45.0f;
    Rect src = sprite_.anim.sourceRect();
    if (!horizontal) {
        // Stretch the 16x64 door to the object height.
        Rect dst{box.center().x - src.w * 0.5f, box.y, src.w, box.h};
        ctx.r.drawWorldTexture(*sprite_.set->texture, &src, dst);
    } else {
        Rect dst = Rect::fromCenter(box.center(), {src.w, box.w});
        ctx.r.drawWorldTexture(*sprite_.set->texture, &src, dst, Color::white(), BlendMode::Alpha, false, 90.0f);
    }
    if (!open_) ctx.lights.add(box.center(), std::max(box.w, box.h) * 0.7f, {255, 179, 71, 255});
}

// ------------------------------------------------------------------- lever

void LeverEntity::init(GameWorld& w) {
    sprite_.load(w.resources(), "animations/lever.json", "off");
    oneShot_ = obj_.prop<bool>("oneShot", true);
}

void LeverEntity::update(GameWorld& /*w*/, float dt) { sprite_.anim.update(dt); }

void LeverEntity::interact(GameWorld& w) {
    if (used_ && oneShot_) return;
    used_ = true;
    on_ = !on_;
    sprite_.anim.play(on_ ? "switching" : "off", true);
    w.playSound("sounds/lever.wav", box.center(), 0.9f);
    actOnTargets(w, obj_.propStr("targets"), "toggle");
    w.emit(id(), "activate", {{"on", on_}});
}

void LeverEntity::render(RenderContext& ctx) {
    sprite_.draw(ctx, box.bottomCenter(), obj_.flipX);
    ctx.lights.add(box.center(), 30.0f, on_ ? Color{57, 240, 216, 255} : Color{255, 179, 71, 255});
}

// ---------------------------------------------------------------- receptor

void ReceptorEntity::init(GameWorld& w) { sprite_.load(w.resources(), "animations/receptor.json", "off"); }

void ReceptorEntity::update(GameWorld& /*w*/, float dt) { sprite_.anim.update(dt); }

bool ReceptorEntity::onPowerHit(GameWorld& w, const std::string& power, const Rect& area) {
    if ((power != "wave" && power != "charge_shot") || !area.intersects(box.expanded(2))) return false;
    float duration = obj_.prop<float>("duration", 0.0f);
    if (on_) {
        timer_ = duration;
        return true;
    }
    on_ = true;
    timer_ = duration;
    sprite_.anim.play("on", true);
    w.playSound("sounds/checkpoint.wav", box.center(), 0.7f, 1.3f);
    w.particles().emit("receptor_on", box.center(), 1.0f);
    actOnTargets(w, obj_.propStr("targets"), "open");
    w.emit(id(), "activate");
    return true;
}

void ReceptorEntity::fixedUpdate(GameWorld& w, float dt) {
    if (!on_ || obj_.prop<float>("duration", 0.0f) <= 0.0f) return;
    timer_ -= dt;
    if (timer_ <= 0.0f) {
        on_ = false;
        sprite_.anim.play("off", true);
        actOnTargets(w, obj_.propStr("targets"), "close");
        w.emit(id(), "deactivate");
    }
}

void ReceptorEntity::render(RenderContext& ctx) {
    sprite_.draw(ctx, box.bottomCenter(), false, Color::white(), obj_.rotation);
    ctx.lights.add(box.center(), on_ ? 70.0f : 26.0f, on_ ? Color{57, 240, 216, 255} : Color{70, 100, 120, 255});
    if (on_ && obj_.prop<float>("duration", 0.0f) > 0.0f) {
        float k = std::clamp(timer_ / obj_.prop<float>("duration", 1.0f), 0.0f, 1.0f);
        ctx.r.drawWorldLine({box.x, box.y - 4}, {box.x + box.w * k, box.y - 4}, {57, 240, 216, 220}, 2.0f);
    }
}

// -------------------------------------------------------------------- sign

void SignEntity::init(GameWorld& w) {
    sprite_.load(w.resources(), "animations/sign.json", "idle");
    auto_ = obj_.prop<bool>("auto", true);
}

void SignEntity::onPlayerEnter(GameWorld& w) {
    if (auto_) w.hint(obj_.propStr("text"), 4.5f);
}

void SignEntity::interact(GameWorld& w) {
    WorldMessage m;
    m.style = WorldMessage::Style::Dialogue;
    m.speaker = "Tablette ancienne";
    m.text = obj_.propStr("text");
    m.duration = 5.0f;
    w.message(m);
}

void SignEntity::render(RenderContext& ctx) {
    sprite_.draw(ctx, box.bottomCenter());
    float pulse = 0.7f + 0.3f * dmath::sin(ctx.time * 2.2f + box.x);
    ctx.lights.add(box.center(), 36.0f * pulse, {57, 240, 216, 255});
}

}  // namespace cosmic
