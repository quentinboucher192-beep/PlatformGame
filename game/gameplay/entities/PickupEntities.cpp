// Collectibles (shards, relics, orbs) and power unlock tablets.

#include <cmath>

#include "engine/input/InputTypes.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"
#include "game/powers/Powers.h"
#include "engine/core/DetMath.h"

namespace cosmic {

void CollectibleEntity::init(GameWorld& w) {
    const std::string& t = type();
    std::string anim = "animations/" + t + ".json";
    std::string clip = t == "shard" ? "spin" : "idle";
    sprite_.load(w.resources(), anim, clip);
    base_ = box.center();
    seed_ = static_cast<float>((static_cast<int>(base_.x) * 13 + static_cast<int>(base_.y) * 7) % 100) * 0.07f;
}

void CollectibleEntity::makeDrop(float vx, float vy) {
    dropping_ = true;
    vel_ = {vx, vy};
    pickupDelay_ = 0.35f;
    life_ = 12.0f;
}

void CollectibleEntity::fixedUpdate(GameWorld& w, float dt) {
    if (pickupDelay_ > 0.0f) pickupDelay_ -= dt;
    if (life_ > 0.0f) {
        life_ -= dt;
        if (life_ <= 0.0f) removed = true;
    }
    if (dropping_) {
        vel_.y = std::min(vel_.y + 600.0f * dt, 300.0f);
        MoveOptions opt;
        Rect b = box;
        MoveResult r = w.physics().move(b, vel_ * dt, opt);
        box = b;
        if (r.hitBottom) {
            vel_.y = -vel_.y * 0.35f;
            vel_.x *= 0.6f;
            if (std::abs(vel_.y) < 30.0f) {
                dropping_ = false;
                base_ = box.center() - Vec2{0, 4};
            }
        }
        if (r.hitLeft || r.hitRight) vel_.x = -vel_.x * 0.5f;
    } else {
        // Magnet toward a nearby player (drops and energy orbs).
        Player& p = w.player();
        float d = distance(p.center(), box.center());
        // The Magnet power pulls every collectible from much farther, and faster.
        float reach = 40.0f, speed = 220.0f;
        bool pulled = life_ > 0.0f || type() == "energy_orb";
        for (const auto& pw : p.powers())
            if (const auto* mg = dynamic_cast<const MagnetPower*>(pw.get())) {
                reach = mg->radius();
                speed = 380.0f;
                pulled = true;
            }
        if (pulled && d < reach && p.alive()) {
            Vec2 c = box.center() + (p.center() - box.center()).normalized() * std::min(d, speed * dt);
            box = Rect::fromCenter(c, box.size());
            base_ = c;
        } else {
            box = Rect::fromCenter(base_ + Vec2{0, dmath::sin(w.time() * 2.5f + seed_) * 2.0f}, box.size());
        }
    }
    sprite_.anim.update(dt);
}

void CollectibleEntity::onPlayerEnter(GameWorld& w) {
    if (pickupDelay_ > 0.0f || removed) {
        playerInside = false;  // re-check next step
        return;
    }
    Player& p = w.player();
    const std::string& t = type();
    if (t == "shard") {
        if (life_ < 0.0f) w.stats().shards++;
        w.stats().scoreBonus += obj_.prop<int>("value", 50);
        float pitch = 1.0f + std::fmod(w.time() * 0.7f, 0.25f);
        w.playSound("sounds/collect_shard.wav", box.center(), 0.6f, pitch);
        w.particles().emit("collect_shard", box.center(), 1.0f);
        p.addEnergy(4.0f);
    } else if (t == "relic") {
        w.stats().relics++;
        w.stats().scoreBonus += obj_.prop<int>("value", 1000);
        w.playSound("sounds/collect_relic.wav", box.center(), 0.9f);
        w.particles().emit("collect_relic", box.center(), 1.0f);
        w.flash({255, 217, 138, 255}, 0.15f);
        WorldMessage m;
        m.style = WorldMessage::Style::Secret;
        m.text = obj_.propStr("name", "Relique ancienne") + " trouvée !";
        m.duration = 2.8f;
        w.message(m);
    } else if (t == "health_orb") {
        if (p.health >= p.maxHealth() && life_ < 0.0f) {
            playerInside = false;
            return;  // keep it for later
        }
        p.heal(obj_.prop<int>("amount", 1));
        w.playSound("sounds/collect_orb.wav", box.center(), 0.8f, 0.9f);
        w.particles().emit("heal", p.center(), 1.0f);
    } else if (t == "heart_container") {
        int hearts = std::max(1, obj_.prop<int>("hearts", 1));
        p.bonusHealth += hearts;
        p.health = p.maxHealth();
        w.playSound("sounds/heart_pickup.wav", box.center(), 1.0f);
        w.particles().emit("heart_pickup", box.center(), 1.0f);
        w.flash({255, 120, 150, 255}, 0.15f);
        WorldMessage m;
        m.style = WorldMessage::Style::Secret;
        m.text = "Cœur de cristal : +" + std::to_string(hearts) + (hearts > 1 ? " cœurs maximum !" : " cœur maximum !");
        m.duration = 2.8f;
        w.message(m);
    } else if (t == "energy_orb") {
        p.addEnergy(obj_.prop<float>("amount", 40.0f));
        w.playSound("sounds/collect_orb.wav", box.center(), 0.7f, 1.2f);
        w.particles().emit("energy_gain", p.center(), 1.0f);
    }
    w.emit(id(), "collected");
    removed = true;
}

void CollectibleEntity::render(RenderContext& ctx) {
    if (life_ > 0.0f && life_ < 2.0f && static_cast<int>(life_ * 10) % 2 == 0) return;  // blink before vanishing
    if (type() == "heart_container") {
        sprite_.draw(ctx, box.bottomCenter());
        ctx.lights.add(box.center(), 44.0f, {255, 120, 150, 255});
        ctx.r.drawWorldGlow(box.center(), 13.0f, Color{255, 120, 150, 90});
        return;
    }
    sprite_.draw(ctx, box.center());
    Color c = type() == "shard"       ? Color{57, 240, 216, 255}
              : type() == "relic"     ? Color{255, 217, 138, 255}
              : type() == "health_orb" ? Color{255, 77, 109, 255}
                                       : Color{57, 240, 216, 255};
    ctx.lights.add(box.center(), type() == "relic" ? 60.0f : 32.0f, c);
    ctx.r.drawWorldGlow(box.center(), type() == "relic" ? 14.0f : 9.0f, c.withAlpha(90));
}

// ------------------------------------------------------------- power unlock

void PowerUnlockEntity::init(GameWorld& w) {
    power_ = obj_.propStr("power", "dash");
    sprite_.load(w.resources(), "animations/power_unlock.json", power_);
    // A tablet of a power already carried stays (power stations, swaps): it is shown dimmed.
}

void PowerUnlockEntity::update(GameWorld& w, float dt) {
    t_ += dt;
    owned_ = w.mode() != WorldMode::Editor && w.player().hasPower(power_);
    sprite_.anim.update(dt);
}

void PowerUnlockEntity::onPlayerEnter(GameWorld& w) {
    const PowerDefinition* def = w.content().power(power_);
    if (!def || w.mode() == WorldMode::Editor) return;
    switch (w.player().offerPower(*def)) {
        case Player::Offer::AlreadyOwned: return;
        case Player::Offer::NeedsChoice:
            // Every slot is taken (or a carried power uses the same button): the player
            // chooses which power to leave here, or leaves this one.
            w.offerPower(id(), power_, w.player().conflictingPower(*def));
            return;
        case Player::Offer::Granted: break;
    }
    w.player().addEnergy(100.0f);
    celebrate(w, *def, box.center(), obj_.propStr("message"));
    w.emit(id(), "collected");
    w.emit(power_, "unlocked");
    removed = true;
}

void PowerUnlockEntity::exchange(GameWorld& w, const std::string& dropped) {
    power_ = dropped;
    sprite_.load(w.resources(), "animations/power_unlock.json", power_);
    w.particles().emit("power_unlock", box.center(), 1.0f);
}

void PowerUnlockEntity::celebrate(GameWorld& w, const PowerDefinition& def, Vec2 at, const std::string& message,
                                  const std::string& dropped) {
    w.playSound("sounds/power_unlock.wav", 1.0f);
    w.particles().emit("power_unlock", at, 1.5f);
    w.flash(def.color, 0.35f);
    w.shake(0.25f);
    w.hitStop(0.12f);
    WorldMessage m;
    m.style = WorldMessage::Style::Title;
    m.text = "Nouveau pouvoir : " + def.name;
    if (!dropped.empty()) {
        const PowerDefinition* old = w.content().power(dropped);
        m.subtitle = (old ? old->name : dropped) + " reste ici : revenez le reprendre quand vous voulez.";
    } else {
        m.subtitle = message.empty() ? def.description : message;
    }
    m.speaker = actionId(def.input);  // the HUD shows the matching button
    m.duration = 5.0f;
    w.message(m);
}

void PowerUnlockEntity::render(RenderContext& ctx) {
    Vec2 c = box.center() + Vec2{0, dmath::sin(t_ * 2.0f) * 3.0f};
    if (owned_ && !ctx.editor) {
        // Already carried: a faint tablet, available again if the power is left elsewhere.
        sprite_.draw(ctx, c, false, Color{140, 150, 175, 140});
        return;
    }
    ctx.r.drawWorldGlow(c, 26.0f, {176, 76, 255, 90});
    sprite_.draw(ctx, c);
    ctx.lights.add(c, 90.0f, {200, 120, 255, 255});
}

}  // namespace cosmic
