#include "game/enemies/BossHazards.h"

#include <algorithm>
#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {
float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
}  // namespace

float BossHazards::Flood::level() const {
    if (age < rise) return height * smooth(age / std::max(0.05f, rise));
    if (age < rise + hold) return height;
    return height * (1.0f - smooth((age - rise - hold) / std::max(0.05f, fall)));
}

// ================================================================ creation

void BossHazards::addPull(const Pull& p) {
    for (auto& q : pulls_)
        if (q.owner == p.owner) {
            q = p;
            return;
        }
    pulls_.push_back(p);
}

void BossHazards::addIce(GameWorld& w, uint32_t owner, const Rect& area, float grip, float seconds) {
    for (auto& i : ice_)
        if (i.owner == owner) {
            i.time = std::max(i.time, seconds);
            return;
        }
    Ice ice;
    ice.owner = owner;
    ice.area = area;
    ice.time = seconds;
    grip = std::clamp(grip, 0.05f, 1.0f);
    for (const Collider& c : w.physics().colliders()) {
        if (!c.enabled || c.id == 0) continue;
        if (c.kind != ColliderKind::Solid && c.kind != ColliderKind::OneWay) continue;
        // A surface of the area: its top between the arena's ceiling and a little below its floor.
        if (c.box.y < area.y || c.box.y > area.bottom() + 4.0f) continue;
        float l = std::max(c.box.x, area.x), r = std::min(c.box.right(), area.right());
        if (r - l < 8.0f) continue;
        Collider* m = w.physics().get(c.id);
        if (!m) continue;
        ice.saved.push_back({c.id, m->friction});
        m->friction = std::min(m->friction, grip);
        ice.tops.push_back({l, c.box.y, r - l, 4.0f});
    }
    ice_.push_back(std::move(ice));
}

void BossHazards::addFlood(const Flood& f) {
    for (auto& g : floods_)
        if (g.owner == f.owner) {
            g = f;
            return;
        }
    floods_.push_back(f);
}

void BossHazards::addWall(GameWorld& w, uint32_t owner, const Rect& rect, float seconds, float damage, float inside) {
    Wall wall;
    wall.owner = owner;
    wall.rect = rect;
    wall.time = seconds;
    wall.damage = damage;
    Player& p = w.player();
    if (p.alive() && p.box.intersects(rect)) {
        // Never inside a wall: Nova is put on the cage's side of it.
        float x = inside >= 0.0f ? rect.right() + 0.5f : rect.x - p.box.w - 0.5f;
        Rect moved = p.box;
        moved.x = x;
        if (!w.physics().overlapsSolid(moved, p.moveOptions())) {
            p.box = moved;
            p.prevBox.x = x;
        }
    }
    wall.collider = w.physics().add(rect, ColliderKind::Solid);
    walls_.push_back(wall);
}

// ================================================================= removal

void BossHazards::restore(GameWorld& w, Ice& ice) {
    // Back to each surface's own grip (in reverse: overlapping frosts unwind cleanly).
    for (auto it = ice.saved.rbegin(); it != ice.saved.rend(); ++it)
        if (Collider* c = w.physics().get(it->first)) c->friction = it->second;
    ice.saved.clear();
}

void BossHazards::endChannel(GameWorld& w, uint32_t owner) {
    pulls_.erase(std::remove_if(pulls_.begin(), pulls_.end(), [&](const Pull& p) { return p.owner == owner; }), pulls_.end());
    walls_.erase(std::remove_if(walls_.begin(), walls_.end(),
                                [&](const Wall& wl) {
                                    if (wl.owner != owner) return false;
                                    w.physics().remove(wl.collider);
                                    return true;
                                }),
                 walls_.end());
}

void BossHazards::removeOwner(GameWorld& w, uint32_t owner) {
    if (empty()) return;
    endChannel(w, owner);
    for (auto it = ice_.rbegin(); it != ice_.rend(); ++it)
        if (it->owner == owner) restore(w, *it);
    ice_.erase(std::remove_if(ice_.begin(), ice_.end(), [&](const Ice& i) { return i.owner == owner; }), ice_.end());
    floods_.erase(std::remove_if(floods_.begin(), floods_.end(), [&](const Flood& f) { return f.owner == owner; }), floods_.end());
}

void BossHazards::clear(GameWorld& w) {
    for (auto it = ice_.rbegin(); it != ice_.rend(); ++it) restore(w, *it);
    for (const Wall& wl : walls_) w.physics().remove(wl.collider);
    pulls_.clear();
    ice_.clear();
    floods_.clear();
    walls_.clear();
}

bool BossHazards::hasIce(uint32_t owner) const {
    return std::any_of(ice_.begin(), ice_.end(), [&](const Ice& i) { return i.owner == owner; });
}
bool BossHazards::hasFlood(uint32_t owner) const {
    return std::any_of(floods_.begin(), floods_.end(), [&](const Flood& f) { return f.owner == owner; });
}
bool BossHazards::hasPull(uint32_t owner) const {
    return std::any_of(pulls_.begin(), pulls_.end(), [&](const Pull& p) { return p.owner == owner; });
}

// ================================================================= stepping

void BossHazards::fixedUpdate(GameWorld& w, float dt) {
    if (empty()) return;
    Player& p = w.player();
    const bool exposed = p.alive() && !p.respawning() && w.state() == WorldState::Playing;

    // Pulls: Nova drifts toward the centre (outside her own movement, so her controls stay
    // whole: running the other way always wins).
    for (Pull& pl : pulls_) {
        pl.age += dt;
        pl.time -= dt;
        if (!exposed) continue;
        Vec2 d = pl.center - p.center();
        float dist = d.length();
        if (dist > 1.0f && dist < pl.range) {
            float k = 0.5f + 0.5f * (1.0f - dist / pl.range);
            Vec2 drift = d / dist * (pl.speed * k);
            if (p.grounded()) drift.y = 0.0f;
            else drift.y = std::clamp(drift.y, -pl.speed * 0.6f, pl.speed * 0.6f);
            w.physics().move(p.box, drift * dt, p.moveOptions());
        }
        if (pl.core > 0.0f && pl.damage > 0.0f && Collision::circleIntersectsRect(pl.center, pl.core, p.box)) {
            DamageInfo dmg;
            dmg.amount = pl.damage;
            dmg.type = DamageType::Contact;
            dmg.source = pl.center;
            dmg.knockback = 240.0f;
            w.damagePlayer(dmg);
        }
    }
    pulls_.erase(std::remove_if(pulls_.begin(), pulls_.end(), [](const Pull& pl) { return pl.time <= 0.0f; }), pulls_.end());

    // Frozen ground: its time, then the grip comes back.
    for (Ice& ice : ice_) {
        ice.age += dt;
        ice.time -= dt;
        if (ice.time <= 0.0f) restore(w, ice);
    }
    ice_.erase(std::remove_if(ice_.begin(), ice_.end(), [](const Ice& i) { return i.time <= 0.0f; }), ice_.end());

    // Lava tide: under its surface Nova is hurt and thrown back up.
    for (Flood& f : floods_) {
        f.age += dt;
        if (!exposed || f.level() < 2.0f) continue;
        float cx = p.center().x;
        if (cx < f.left || cx > f.right) continue;
        float surface = f.surfaceY();
        if (p.box.bottom() <= surface + 3.0f) continue;
        DamageInfo dmg;
        dmg.amount = f.damage;
        dmg.type = DamageType::Hazard;
        dmg.source = {cx, p.box.bottom() + 24.0f};
        dmg.knockback = 120.0f;
        bool hurt = w.damagePlayer(dmg);
        if (p.alive() && p.vel.y > -200.0f) {
            p.launch({p.vel.x * 0.5f, -390.0f}, false);
            if (hurt) w.particles().emit("fire_burst", {cx, surface}, 1.0f);
        }
    }
    floods_.erase(std::remove_if(floods_.begin(), floods_.end(), [](const Flood& f) { return f.done(); }), floods_.end());

    // Walls of thorns: solid, and they prick.
    for (Wall& wl : walls_) {
        wl.age += dt;
        wl.time -= dt;
        if (wl.time <= 0.0f) {
            w.physics().remove(wl.collider);
            continue;
        }
        if (exposed && wl.damage > 0.0f && p.box.expanded(1.5f).intersects(wl.rect)) {
            DamageInfo dmg;
            dmg.amount = wl.damage;
            dmg.type = DamageType::Contact;
            dmg.source = {wl.rect.center().x, p.center().y};
            dmg.knockback = 200.0f;
            w.damagePlayer(dmg);
        }
    }
    walls_.erase(std::remove_if(walls_.begin(), walls_.end(), [](const Wall& wl) { return wl.time <= 0.0f; }), walls_.end());
}

void BossHazards::bendShot(Projectile& p, float dt) const {
    for (const Pull& pl : pulls_) {
        if (!pl.bendShots) continue;
        Vec2 to = pl.center - p.pos;
        float dist = to.length();
        if (dist < 1.0f || dist > pl.range) continue;
        float speed = p.vel.length();
        if (speed < 1.0f) continue;
        float want = dmath::atan2(to.y, to.x);
        float cur = dmath::atan2(p.vel.y, p.vel.x);
        float diff = want - cur;
        while (diff > kPi) diff -= 2.0f * kPi;
        while (diff < -kPi) diff += 2.0f * kPi;
        float rate = 3.6f * (1.0f - dist / pl.range) + 0.6f;  // rad/s, stronger near the hole
        float step = std::clamp(diff, -rate * dt, rate * dt);
        p.vel = Vec2{dmath::cos(cur + step), dmath::sin(cur + step)} * speed;
    }
}

// ================================================================== render

void BossHazards::render(RenderContext& ctx) const {
    if (empty()) return;
    // The art of 2.19 gives each effect its own sheet (animations/<name>.json).
    auto sheetOf = [&](const char* name) { return ctx.res.animation(std::string("animations/") + name + ".json"); };
    auto clipIn = [](const AnimationSetPtr& set, const char* clip) -> const AnimationClip* { return set && set->texture ? set->clip(clip) : nullptr; };
    const float t = ctx.time;
    const float px = 1.0f / std::max(0.05f, ctx.r.view().scale);  // one screen pixel in world units

    // Frozen ground: a pale veil on every frozen surface.
    AnimationSetPtr fx = sheetOf("frost_floor");
    const AnimationClip* veil = clipIn(fx, "idle");
    for (const Ice& ice : ice_) {
        float a = std::min({1.0f, ice.age / 0.35f, ice.time / 0.6f});
        if (a <= 0.0f) continue;
        for (const Rect& top : ice.tops) {
            if (veil) {
                int frame = static_cast<int>(t * 8.0f) % std::max(1, veil->frameCount());
                Rect src = fx->frameRect(*veil, frame);
                float fw = static_cast<float>(fx->frameWidth), fh = static_cast<float>(fx->frameHeight);
                for (float x = top.x; x < top.right(); x += fw) {
                    float wdt = std::min(fw, top.right() - x);
                    Rect s = src;
                    s.w = src.w * (wdt / fw);
                    ctx.r.drawWorldTexture(*fx->texture, &s, {x, top.y - fx->pivot.y, wdt, fh}, Color{255, 255, 255, static_cast<uint8_t>(230 * a)});
                }
            } else {
                ctx.r.fillWorldRect({top.x, top.y - 1.0f, top.w, 6.0f}, Color{190, 240, 255, static_cast<uint8_t>(150 * a)});
                ctx.r.fillWorldRect({top.x, top.y - 1.0f, top.w, 2.0f}, Color{240, 252, 255, static_cast<uint8_t>(220 * a)}, BlendMode::Additive);
                for (float x = top.x + 6.0f; x < top.right() - 10.0f; x += 23.0f) {
                    float sh = 0.5f + 0.5f * dmath::sin(t * 3.0f + x * 0.05f);
                    ctx.r.fillWorldRect({x, top.y + 1.0f, 9.0f, 1.5f}, Color{255, 255, 255, static_cast<uint8_t>(160 * a * sh)}, BlendMode::Additive);
                }
            }
            ctx.lights.add(top.center(), std::min(160.0f, top.w * 0.5f), Color{150, 220, 255, static_cast<uint8_t>(120 * a)});
        }
    }

    // Lava tide.
    AnimationSetPtr lavaFx = sheetOf("lava_flood");
    const AnimationClip* lava = clipIn(lavaFx, "surface");
    for (const Flood& f : floods_) {
        float level = f.level();
        if (level < 0.5f) continue;
        float top = f.surfaceY();
        Rect body{f.left, top, f.right - f.left, f.floorY + 64.0f - top};
        ctx.r.fillWorldRect(body, Color{170, 40, 18, 225});
        ctx.r.fillWorldRect({body.x, top + 6.0f, body.w, std::max(0.0f, body.h - 6.0f)}, Color{255, 110, 40, 60}, BlendMode::Additive);
        if (lava) {
            int frame = static_cast<int>(t * 10.0f) % std::max(1, lava->frameCount());
            Rect src = lavaFx->frameRect(*lava, frame);
            float fw = static_cast<float>(lavaFx->frameWidth), fh = static_cast<float>(lavaFx->frameHeight);
            for (float x = f.left; x < f.right; x += fw) {
                float wdt = std::min(fw, f.right - x);
                Rect s = src;
                s.w = src.w * (wdt / fw);
                ctx.r.drawWorldTexture(*lavaFx->texture, &s, {x, top - fh * 0.5f, wdt, fh});
            }
        } else {
            for (float x = f.left; x < f.right; x += 12.0f) {
                float wave = 2.0f * dmath::sin(t * 4.0f + x * 0.08f);
                ctx.r.fillWorldRect({x, top - 2.0f + wave, 12.0f, 4.0f}, Color{255, 200, 90, 240}, BlendMode::Additive);
            }
        }
        for (float x = f.left + 48.0f; x < f.right; x += 120.0f) ctx.lights.add({x, top}, 110.0f, Color{255, 120, 50, 255});
    }

    // Walls of thorns, rising out of the ground.
    AnimationSetPtr thornFx = sheetOf("thorn_wall");
    const AnimationClip* thorn = clipIn(thornFx, "idle");
    for (const Wall& wl : walls_) {
        float grow = std::min(1.0f, wl.age / 0.2f) * std::min(1.0f, wl.time / 0.25f);
        Rect r = wl.rect;
        float h = r.h * grow;
        Rect vis{r.x, r.bottom() - h, r.w, h};
        if (thorn) {
            int frame = static_cast<int>(t * 8.0f) % std::max(1, thorn->frameCount());
            Rect src = thornFx->frameRect(*thorn, frame);
            float fw = static_cast<float>(thornFx->frameWidth), fh = static_cast<float>(thornFx->frameHeight);
            float scale = r.w * 1.6f / fw;
            for (float y = vis.bottom(); y > vis.y + 0.5f; y -= fh * scale) {
                float seg = std::min(fh * scale, y - vis.y);
                Rect s = src;
                s.y = src.y + src.h * (1.0f - seg / (fh * scale));
                s.h = src.h * (seg / (fh * scale));
                ctx.r.drawWorldTexture(*thornFx->texture, &s, {r.center().x - fw * scale * 0.5f, y - seg, fw * scale, seg});
            }
        } else {
            ctx.r.fillWorldRect(vis, Color{46, 88, 40, 255});
            ctx.r.fillWorldRect({vis.x + 3.0f, vis.y, 4.0f, vis.h}, Color{93, 138, 58, 255});
            for (float y = vis.y + 6.0f; y < vis.bottom() - 4.0f; y += 13.0f) {
                bool left = static_cast<int>((y - vis.y) / 13.0f) % 2 == 0;
                Vec2 base{left ? vis.x : vis.right(), y};
                Vec2 tip{base.x + (left ? -7.0f : 7.0f), y - 3.0f};
                ctx.r.drawWorldLine(base, tip, Color{170, 210, 120, 255}, std::max(1.0f, 2.0f * ctx.r.view().scale / 3.0f));
            }
        }
        ctx.lights.add(vis.center(), 46.0f, Color{120, 255, 140, 160});
    }

    // Pulls: a black hole (well) or air rushing in (vortex).
    AnimationSetPtr holeFx = sheetOf("void_well");
    const AnimationClip* hole = clipIn(holeFx, "idle");
    for (const Pull& pl : pulls_) {
        float a = std::min({1.0f, pl.age / 0.3f, pl.time / 0.3f});
        if (pl.core > 0.0f) {
            float r = pl.core * (0.6f + 0.4f * a);
            if (hole) {
                int frame = static_cast<int>(t * 10.0f) % std::max(1, hole->frameCount());
                Rect src = holeFx->frameRect(*hole, frame);
                float s = r * 3.2f;
                ctx.r.drawWorldTexture(*holeFx->texture, &src, Rect::fromCenter(pl.center, {s, s}), Color{255, 255, 255, static_cast<uint8_t>(255 * a)});
            } else {
                ctx.r.drawWorldGlow(pl.center, r * 3.0f, Color{120, 60, 200, static_cast<uint8_t>(110 * a)});
                ctx.r.fillWorldCircle(pl.center, r, Color{4, 0, 10, static_cast<uint8_t>(250 * a)});
                ctx.r.drawWorldCircle(pl.center, r + 2.0f, Color{180, 140, 255, static_cast<uint8_t>(220 * a)}, 2.0f);
            }
            ctx.lights.add(pl.center, r * 4.0f, Color{180, 120, 255, static_cast<uint8_t>(200 * a)});
        }
        // Streaks spiralling in (both kinds): where the pull reaches.
        float reach = std::min(pl.range, 260.0f);
        for (int k = 0; k < 14; ++k) {
            float phase = std::fmod(t * 0.9f + k * 0.37f, 1.0f);
            float rad = reach * (1.0f - phase) + (pl.core > 0.0f ? pl.core : 16.0f);
            float ang = k * 2.39996f + phase * 2.4f;
            Vec2 dir{dmath::cos(ang), dmath::sin(ang)};
            Vec2 p0 = pl.center + dir * rad;
            Vec2 p1 = pl.center + dir * (rad - 14.0f) + Vec2{-dir.y, dir.x} * 5.0f;
            Color c = pl.core > 0.0f ? Color{190, 150, 255, static_cast<uint8_t>(170 * a * phase)}
                                     : Color{215, 190, 255, static_cast<uint8_t>(150 * a * phase)};
            ctx.r.drawWorldLine(p0, p1, c, std::max(1.0f, 2.0f * px * ctx.r.view().scale), BlendMode::Additive);
        }
    }
}

}  // namespace cosmic
