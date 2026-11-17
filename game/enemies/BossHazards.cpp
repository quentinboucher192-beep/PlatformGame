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

Rect BossHazards::Cloud::current() const {
    if (spread <= 0.0f || age >= spread) return area;
    float k = smooth(age / spread);
    float o = std::clamp(originX, area.x, area.right());
    float l = o - (o - area.x) * k, r = o + (area.right() - o) * k;
    return {l, area.y, r - l, area.h};
}

Vec2 BossHazards::Crack::rockAt() const {
    float k = delay > 0.0f ? std::clamp(age / delay, 0.0f, 1.0f) : 1.0f;
    float y0 = fromY + half, y1 = at.y - half;
    return {at.x, y0 + (y1 - y0) * k * k};
}

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

void BossHazards::addCloud(const Cloud& c) {
    // Several at once (a breath, fireballs): the oldest of an enemy go first past a dozen.
    int mine = cloudCount(c.owner);
    if (mine >= 12)
        for (auto it = clouds_.begin(); it != clouds_.end(); ++it)
            if (it->owner == c.owner) {
                clouds_.erase(it);
                break;
            }
    clouds_.push_back(c);
}

void BossHazards::addWind(const Wind& wd) {
    for (auto& x : winds_)
        if (x.owner == wd.owner) {
            x = wd;
            return;
        }
    winds_.push_back(wd);
}

void BossHazards::addCrack(const Crack& c) {
    // A quake opens many: the oldest of an enemy go first past two dozen.
    if (crackCount(c.owner) >= 24)
        for (auto it = cracks_.begin(); it != cracks_.end(); ++it)
            if (it->owner == c.owner) {
                cracks_.erase(it);
                break;
            }
    cracks_.push_back(c);
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

void BossHazards::addWall(GameWorld& w, uint32_t owner, const Rect& rect, float seconds, float damage, float inside,
                          const std::string& look, Color color) {
    Wall wall;
    wall.look = look;
    wall.side = inside >= 0.0f ? 1.0f : -1.0f;
    wall.color = color;
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
    winds_.erase(std::remove_if(winds_.begin(), winds_.end(), [&](const Wind& x) { return x.owner == owner; }), winds_.end());
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
    clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(), [&](const Cloud& c) { return c.owner == owner; }), clouds_.end());
    cracks_.erase(std::remove_if(cracks_.begin(), cracks_.end(), [&](const Crack& c) { return c.owner == owner; }), cracks_.end());
}

void BossHazards::clear(GameWorld& w) {
    for (auto it = ice_.rbegin(); it != ice_.rend(); ++it) restore(w, *it);
    for (const Wall& wl : walls_) w.physics().remove(wl.collider);
    pulls_.clear();
    ice_.clear();
    floods_.clear();
    walls_.clear();
    clouds_.clear();
    winds_.clear();
    cracks_.clear();
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
bool BossHazards::hasWind(uint32_t owner) const {
    return std::any_of(winds_.begin(), winds_.end(), [&](const Wind& x) { return x.owner == owner; });
}
int BossHazards::cloudCount(uint32_t owner) const {
    return static_cast<int>(std::count_if(clouds_.begin(), clouds_.end(), [&](const Cloud& c) { return c.owner == owner; }));
}
int BossHazards::crackCount(uint32_t owner) const {
    return static_cast<int>(std::count_if(cracks_.begin(), cracks_.end(), [&](const Crack& c) { return c.owner == owner; }));
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

    // The wind of the dragon's wings (2.50): Nova drifts away from it, horizontally (outside her
    // own movement: running against it still wins, a jump or a dash too).
    for (Wind& wd : winds_) {
        wd.age += dt;
        wd.time -= dt;
        if (!exposed) continue;
        Vec2 pc = p.center();
        float dist = distance(pc, wd.center);
        if (dist >= wd.range) continue;
        float side = pc.x >= wd.center.x ? 1.0f : -1.0f;
        float k = (0.5f + 0.5f * (1.0f - dist / wd.range)) * std::min(1.0f, wd.age / 0.25f);
        w.physics().move(p.box, Vec2{side * wd.speed * k * dt, 0.0f}, p.moveOptions());
    }
    winds_.erase(std::remove_if(winds_.begin(), winds_.end(), [](const Wind& x) { return x.time <= 0.0f; }), winds_.end());

    // Clouds of fire (2.50): they burn inside, then fade.
    for (Cloud& c : clouds_) {
        c.age += dt;
        c.time -= dt;
        // Display only: smoke and flames rising from it (the effects' own random generator).
        c.emitTimer -= dt;
        if (c.emitTimer <= 0.0f && c.time > 0.3f) {
            c.emitTimer = c.ground ? 0.05f : 0.08f;
            Rect r = c.current();
            if (r.w > 2.0f) {
                Vec2 at{w.fxRng().range(r.x, r.right()), w.fxRng().range(r.y + r.h * 0.3f, r.bottom() - 2.0f)};
                // 2.53: the Kraken's ink sheds bubbles of its light, no smoke.
                // 2.55: the Phénix's ash sheds ash and a few embers.
                w.particles().emit(c.ink ? "bubbles" : (c.ash ? "ash_fall" : "fire_cloud"), at, 1.0f, {}, &c.color);
                if (w.fxRng().chance(0.35f) && !c.ink) w.particles().emit(c.ash ? "embers_rising" : "fire_smoke", at + Vec2{0.0f, -r.h * 0.3f}, 1.0f);
            }
        }
        if (!exposed || c.damage <= 0.0f || !c.burning() || !c.current().intersects(p.box)) continue;
        DamageInfo dmg;
        dmg.amount = c.damage;
        dmg.type = DamageType::Contact;
        dmg.source = {c.area.center().x, p.center().y + 6.0f};  // out of it by the nearest side
        dmg.knockback = 150.0f;
        if (w.damagePlayer(dmg)) w.particles().emit("fire_burst", p.center(), 0.8f);
    }
    clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(), [](const Cloud& c) { return c.time <= 0.0f; }), clouds_.end());

    // The golem's quake (2.52): the cracks burst out of the ground, the rocks land on their marks.
    for (Crack& c : cracks_) {
        const bool was = c.bursting();
        c.age += dt;
        if (!was && c.bursting()) {
            if (c.sun) {
                // 2.55: the column of sunlight strikes the ground.
                w.particles().emit("eruption_fire", c.at, 1.1f, {}, &c.color);
                w.particles().emit("embers_rising", c.at + Vec2{0.0f, -8.0f}, 1.0f, {}, &c.color);
                w.playSound("sounds/laser_fire.wav", c.at, 0.45f, 1.3f);
                w.shake(0.08f);
            } else if (c.rock) {
                w.particles().emit("rock_shatter", c.at + Vec2{0.0f, -c.half}, 0.9f);
                w.particles().emit("slam_dust", c.at, 0.8f);
                w.playSound("sounds/golem_slam.wav", c.at, 0.5f, 1.5f);
                w.shake(0.12f);
            } else {
                w.particles().emit("quake_rocks", c.at, 1.0f);
                w.particles().emit("rune_glow", c.at + Vec2{0.0f, -6.0f}, 2.0f, {}, &c.color);
                w.particles().emit("slam_dust", c.at, 0.9f);
                w.playSound("sounds/root_burst.wav", c.at, 0.7f, 0.7f);
                w.shake(0.18f);
            }
        }
        if (!exposed || c.hit || c.damage <= 0.0f) continue;
        Rect hurt;
        if (c.rock && !c.bursting()) {
            hurt = Rect::fromCenter(c.rockAt(), {c.half * 1.6f, c.half * 1.6f});  // the stone on its way down
        } else if (c.bursting() && c.age < c.delay + c.burst) {
            const float w2 = c.rock ? c.half * 1.3f : c.half;
            hurt = {c.at.x - w2, c.at.y - c.height, w2 * 2.0f, c.height};
        } else {
            continue;
        }
        if (!hurt.intersects(p.box)) continue;
        DamageInfo dmg;
        dmg.amount = c.damage;
        dmg.type = DamageType::Contact;
        dmg.source = c.rock && !c.bursting() ? c.rockAt() + Vec2{0.0f, -c.half} : Vec2{c.at.x, c.at.y + 12.0f};
        dmg.knockback = c.knockback;
        if (w.damagePlayer(dmg)) c.hit = true;
    }
    cracks_.erase(std::remove_if(cracks_.begin(), cracks_.end(), [](const Crack& c) { return c.done(); }), cracks_.end());
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

namespace {
/// A tide of the abyss (2.53, the Kraken): dark water, its crests lit with the boss's light,
/// foam where the swell peaks, bubbles rising inside. Display only (time-driven).
void drawTide(RenderContext& ctx, const BossHazards::Flood& f, float t) {
    const float top = f.surfaceY();
    const Rect body{f.left, top, f.right - f.left, f.floorY + 64.0f - top};
    const Color c = f.color;
    ctx.r.fillWorldRect(body, Color{10, 22, 52, 228});
    ctx.r.fillWorldRect({body.x, top + 10.0f, body.w, std::max(0.0f, body.h - 10.0f)}, Color{c.r, c.g, c.b, 30}, BlendMode::Additive);
    for (float x = f.left; x < f.right; x += 8.0f) {
        const float wdt = std::min(8.0f, f.right - x);
        const float swell = 3.0f * dmath::sin(t * 1.8f + x * 0.025f) + 1.5f * dmath::sin(t * 4.3f + x * 0.11f);
        ctx.r.fillWorldRect({x, top - 3.0f + swell, wdt, 5.0f}, Color{30, 70, 120, 240});
        ctx.r.fillWorldRect({x, top - 3.0f + swell, wdt, 2.0f}, c.withAlpha(210), BlendMode::Additive);
        if (swell < -2.6f) ctx.r.fillWorldRect({x, top - 5.0f + swell, wdt, 2.0f}, Color{235, 250, 255, 190}, BlendMode::Additive);
    }
    const float depth = std::min(f.level(), 220.0f);
    const int bubbles = static_cast<int>(body.w / 40.0f);
    for (int k = 0; k < bubbles && depth > 12.0f; ++k) {
        const float ph = std::fmod(t * 0.45f + static_cast<float>(k) * 0.618f, 1.0f);
        const float x = f.left + std::fmod(static_cast<float>(k) * 97.3f, body.w) + 4.0f * dmath::sin(t * 3.0f + static_cast<float>(k));
        const float y = top + 8.0f + (1.0f - ph) * (depth - 8.0f);
        ctx.r.drawWorldCircle({x, y}, 1.5f + 1.5f * ph, Color{200, 240, 255, static_cast<uint8_t>(150 * ph)}, 1.0f);
    }
    for (float x = f.left + 48.0f; x < f.right; x += 120.0f) ctx.lights.add({x, top}, 110.0f, c);
}

/// A tentacle out of the floor (2.53, the Kraken's « Étau de tentacules »): it sways, its tip
/// curls over the cage, a row of suckers lit with the boss's light along its inner side.
void drawTentacle(RenderContext& ctx, const BossHazards::Wall& wl, const Rect& vis, float t) {
    const float h = vis.h;
    if (h < 1.0f) return;
    const int n = std::max(4, static_cast<int>(h / 6.0f));
    const float base = vis.bottom(), cx = vis.center().x, side = wl.side, seed = wl.rect.x * 0.037f;
    auto at = [&](int i, float& rad) {
        const float s = static_cast<float>(i) / static_cast<float>(n);
        const float sway = (2.0f + 7.0f * s) * dmath::sin(t * 2.6f + s * 3.6f + seed);
        rad = 10.5f * (1.0f - 0.78f * s) + 1.2f;
        return Vec2{cx + sway + side * 16.0f * s * s * s, base - h * s};
    };
    const Color outline{22, 14, 44, 255}, skin{67, 64, 126, 255}, sheen{95, 138, 168, 255}, belly{157, 147, 194, 255};
    float rad = 0.0f;
    for (int i = 0; i <= n; ++i) {
        const Vec2 p = at(i, rad);
        ctx.r.fillWorldCircle(p, rad + 1.5f, outline);
    }
    for (int i = 0; i <= n; ++i) {
        const Vec2 p = at(i, rad);
        ctx.r.fillWorldCircle(p, rad, skin);
    }
    for (int i = 0; i <= n; ++i) {
        const Vec2 p = at(i, rad);
        ctx.r.fillWorldCircle(p + Vec2{-side * rad * 0.45f, -rad * 0.2f}, rad * 0.35f, sheen);
    }
    for (int i = 1; i < n; i += 3) {
        const Vec2 p = at(i, rad);
        const Vec2 q = p + Vec2{side * rad * 0.62f, 0.0f};
        const float pulse = 0.55f + 0.45f * dmath::sin(t * 5.0f - static_cast<float>(i) * 0.7f);
        ctx.r.fillWorldCircle(q, std::max(1.2f, rad * 0.32f), belly);
        ctx.r.drawWorldGlow(q, 5.0f + rad * 0.4f, wl.color.withAlpha(static_cast<uint8_t>(150 * pulse)));
    }
    const Vec2 tip = at(n, rad);
    ctx.r.drawWorldGlow(tip, 9.0f, wl.color.withAlpha(160));
    ctx.lights.add(vis.center(), 60.0f, wl.color.withAlpha(200));
}

/// A column of sunlight (2.55, the Phénix's « Colonnes solaires »): first a thread of light from
/// the roof and a mark burning brighter on the ground (it trembles at the end), then a beam of
/// white-hot light with the phase's colour around it, sparks at its foot, which fades. Display
/// only (time-driven).
void drawSunColumn(RenderContext& ctx, const BossHazards::Crack& c, float t) {
    const Color col = c.color;
    const float px = ctx.r.view().scale / 3.0f;
    const float top = c.at.y - c.height;
    const float h = c.half;
    if (!c.bursting()) {
        const float k = c.delay > 0.0f ? std::clamp(c.age / c.delay, 0.0f, 1.0f) : 1.0f;
        const float shake = k > 0.75f ? dmath::sin(t * 60.0f + c.at.x) * 1.0f : 0.0f;
        const float x = c.at.x + shake;
        // The thread from the roof, its edges where the column will burn.
        ctx.r.drawWorldLine({x, top}, {x, c.at.y}, col.withAlpha(static_cast<uint8_t>(40 + 120 * k)),
                            std::max(1.0f, (1.0f + 2.0f * k) * px), BlendMode::Additive);
        if (k > 0.35f) {
            const uint8_t ea = static_cast<uint8_t>(60 * (k - 0.35f) / 0.65f);
            ctx.r.drawWorldLine({c.at.x - h, top}, {c.at.x - h, c.at.y}, col.withAlpha(ea), std::max(1.0f, px), BlendMode::Additive);
            ctx.r.drawWorldLine({c.at.x + h, top}, {c.at.x + h, c.at.y}, col.withAlpha(ea), std::max(1.0f, px), BlendMode::Additive);
        }
        // The mark on the ground: a disc of light widening, a red line under it.
        const float wdt = h * (0.6f + 0.8f * k);
        ctx.r.fillWorldRect(Rect::fromCenter(c.at + Vec2{0.0f, -2.0f}, {wdt * 2.0f, 4.0f}), col.withAlpha(static_cast<uint8_t>(80 + 150 * k)),
                            BlendMode::Additive);
        ctx.r.fillWorldRect(Rect::fromCenter(c.at + Vec2{0.0f, -1.0f}, {h * 2.6f, 2.0f}), Color{255, 80, 60, static_cast<uint8_t>(90 + 130 * k)},
                            BlendMode::Additive);
        ctx.r.drawWorldGlow(c.at + Vec2{0.0f, -4.0f}, h * (0.8f + 1.2f * k), col.withAlpha(static_cast<uint8_t>(40 + 120 * k)));
        if (k > 0.5f) ctx.lights.add(c.at, 60.0f * k, col);
        return;
    }
    // The beam: it flashes in at once, burns, then thins out and fades.
    const float e = c.age - c.delay;
    const float in = std::clamp(e / 0.05f, 0.0f, 1.0f);
    const float out = 1.0f - std::clamp((e - c.burst) / 0.3f, 0.0f, 1.0f);
    const float a = in * out;
    if (a <= 0.0f) return;
    const float wdt = h * (0.55f + 0.45f * out);
    const Rect beam{c.at.x - wdt, top, wdt * 2.0f, c.height};
    ctx.r.fillWorldRect(beam.expanded(6.0f), col.withAlpha(static_cast<uint8_t>(50 * a)), BlendMode::Additive);
    ctx.r.fillWorldRect(beam, col.withAlpha(static_cast<uint8_t>(170 * a)), BlendMode::Additive);
    const float core = wdt * 0.42f;
    ctx.r.fillWorldRect({c.at.x - core, top, core * 2.0f, c.height}, Color{255, 252, 236, static_cast<uint8_t>(230 * a)}, BlendMode::Additive);
    // Ripples running down the beam (the light pouring from the roof).
    for (int i = 0; i < 4; ++i) {
        const float ph = std::fmod(t * 2.2f + static_cast<float>(i) * 0.25f, 1.0f);
        const float y = top + ph * c.height;
        ctx.r.fillWorldRect({c.at.x - wdt - 3.0f, y, wdt * 2.0f + 6.0f, 3.0f}, Color{255, 255, 255, static_cast<uint8_t>(90 * a * (1.0f - ph))},
                            BlendMode::Additive);
    }
    ctx.r.drawWorldGlow(c.at + Vec2{0.0f, -6.0f}, h * 2.2f, col.withAlpha(static_cast<uint8_t>(200 * a)));
    ctx.r.drawWorldGlow({c.at.x, top + 8.0f}, h * 1.6f, col.withAlpha(static_cast<uint8_t>(120 * a)));
    for (float y = top + 40.0f; y < c.at.y; y += 120.0f) ctx.lights.add({c.at.x, y}, 90.0f, col.withAlpha(static_cast<uint8_t>(255 * a)));
    ctx.lights.add(c.at, 120.0f, col);
}
}  // namespace

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

    // Lava tide (or star plasma, 2.33).
    AnimationSetPtr lavaSheet = sheetOf("lava_flood");
    AnimationSetPtr plasmaFx = sheetOf("plasma_flood");
    for (const Flood& f : floods_) {
        float level = f.level();
        if (level < 0.5f) continue;
        if (f.look == "tide") {
            drawTide(ctx, f, t);
            continue;
        }
        const bool plasma = f.look == "plasma";
        const AnimationSetPtr& lavaFx = plasma && clipIn(plasmaFx, "surface") ? plasmaFx : lavaSheet;
        const AnimationClip* lava = clipIn(lavaFx, "surface");
        float top = f.surfaceY();
        Rect body{f.left, top, f.right - f.left, f.floorY + 64.0f - top};
        ctx.r.fillWorldRect(body, plasma ? Color{120, 20, 92, 225} : Color{170, 40, 18, 225});
        ctx.r.fillWorldRect({body.x, top + 6.0f, body.w, std::max(0.0f, body.h - 6.0f)},
                            plasma ? Color{255, 110, 210, 60} : Color{255, 110, 40, 60}, BlendMode::Additive);
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
        for (float x = f.left + 48.0f; x < f.right; x += 120.0f)
            ctx.lights.add({x, top}, 110.0f, plasma ? Color{255, 120, 220, 255} : Color{255, 120, 50, 255});
    }

    // Walls of thorns (or of starlight, 2.33), rising out of the ground.
    AnimationSetPtr thornSheet = sheetOf("thorn_wall");
    AnimationSetPtr starFx = sheetOf("star_wall");
    for (const Wall& wl : walls_) {
        float grow = std::min(1.0f, wl.age / 0.2f) * std::min(1.0f, wl.time / 0.25f);
        Rect r = wl.rect;
        float h = r.h * grow;
        Rect vis{r.x, r.bottom() - h, r.w, h};
        if (wl.look == "tentacle") {
            drawTentacle(ctx, wl, vis, t);
            continue;
        }
        const bool star = wl.look == "star";
        const AnimationSetPtr& thornFx = star && clipIn(starFx, "idle") ? starFx : thornSheet;
        const AnimationClip* thorn = clipIn(thornFx, "idle");
        if (star) ctx.r.drawWorldGlow(vis.center(), std::max(vis.w * 2.5f, vis.h * 0.6f), Color{120, 255, 190, 70});
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
        ctx.lights.add(vis.center(), star ? 90.0f : 46.0f, star ? Color{150, 255, 200, 220} : Color{120, 255, 140, 160});
    }

    // Clouds of fire (2.50): puffs of smoke lit from inside, rolling in place, a hot edge below.
    for (const Cloud& c : clouds_) {
        Rect r = c.current();
        float a = std::clamp(std::min(c.age / 0.2f, c.time / 0.7f), 0.0f, 1.0f);
        if (r.w < 1.0f || a <= 0.0f) continue;
        const Color fire = c.color;
        const int rows = std::max(1, static_cast<int>(std::lround(r.h / 30.0f)));
        const float step = 20.0f;
        for (int row = 0; row < rows; ++row) {
            float y = r.bottom() - (static_cast<float>(row) + 0.5f) * r.h / static_cast<float>(rows);
            for (float x = r.x + step * 0.5f; x < r.right(); x += step) {
                float seed = x * 0.071f + static_cast<float>(row) * 1.7f;
                float rad = r.h / static_cast<float>(rows) * (0.55f + 0.12f * dmath::sin(t * 2.3f + seed * 3.1f));
                Vec2 pc{x + 3.0f * dmath::sin(t * 1.7f + seed), y - 2.0f * dmath::sin(t * 2.9f + seed * 2.0f)};
                if (c.ash) {
                    // 2.55, the Phénix: thick grey ash billowing, embers glinting in it.
                    ctx.r.drawWorldGlow(pc, rad * 1.6f, fire.withAlpha(static_cast<uint8_t>(40 * a)));
                    ctx.r.fillWorldCircle(pc, rad, Color{92, 84, 80, static_cast<uint8_t>(215 * a)});
                    ctx.r.fillWorldCircle(pc + Vec2{-rad * 0.25f, -rad * 0.3f}, rad * 0.55f, Color{150, 140, 132, static_cast<uint8_t>(120 * a)});
                    const float glint = 0.5f + 0.5f * dmath::sin(t * 7.0f + seed * 5.0f);
                    ctx.r.fillWorldCircle(pc + Vec2{rad * 0.2f, rad * 0.25f}, std::max(1.0f, rad * 0.14f),
                                          Color{255, 150, 70, static_cast<uint8_t>(200 * a * glint)}, BlendMode::Additive);
                    continue;
                }
                if (c.ink) {
                    // 2.53, the Kraken: dense ink, only its rim lit by the light of its phase.
                    ctx.r.drawWorldGlow(pc, rad * 1.7f, fire.withAlpha(static_cast<uint8_t>(50 * a)));
                    ctx.r.fillWorldCircle(pc, rad, Color{16, 10, 34, static_cast<uint8_t>(210 * a)});
                    ctx.r.fillWorldCircle(pc + Vec2{-rad * 0.3f, -rad * 0.3f}, rad * 0.35f, Color{fire.r, fire.g, fire.b, static_cast<uint8_t>(45 * a)},
                                          BlendMode::Additive);
                    continue;
                }
                ctx.r.drawWorldGlow(pc, rad * 1.9f, fire.withAlpha(static_cast<uint8_t>(95 * a)));
                ctx.r.fillWorldCircle(pc, rad, Color{58, 20, 16, static_cast<uint8_t>(120 * a)});
                ctx.r.fillWorldCircle(pc + Vec2{0.0f, rad * 0.25f}, rad * 0.55f,
                                      Color{fire.r, fire.g, fire.b, static_cast<uint8_t>(90 * a)}, BlendMode::Additive);
            }
        }
        if (c.ground)
            ctx.r.fillWorldRect({r.x, r.bottom() - 3.0f, r.w, 3.0f},
                                c.ink ? fire.withAlpha(static_cast<uint8_t>(150 * a)) : Color{255, 214, 120, static_cast<uint8_t>(200 * a)},
                                BlendMode::Additive);
        for (float x = r.x + 40.0f; x < r.right(); x += 110.0f)
            ctx.lights.add({x, r.center().y}, 90.0f, fire.withAlpha(static_cast<uint8_t>(220 * a)));
        if (r.w < 80.0f) ctx.lights.add(r.center(), 80.0f, fire.withAlpha(static_cast<uint8_t>(220 * a)));
    }

    // The wind of its wings (2.50): streaks of ash blowing away from it on both sides.
    for (const Wind& wd : winds_) {
        float a = std::clamp(std::min(wd.age / 0.25f, wd.time / 0.4f), 0.0f, 1.0f);
        float reach = std::min(wd.range, 720.0f);
        for (int k = 0; k < 28; ++k) {
            float side = k % 2 == 0 ? 1.0f : -1.0f;
            float ph = std::fmod(t * 1.5f + static_cast<float>(k) * 0.291f, 1.0f);
            float x = wd.center.x + side * (40.0f + ph * reach);
            float y = wd.center.y - 70.0f + std::fmod(static_cast<float>(k) * 37.0f, 300.0f);
            float len = 16.0f + 30.0f * (1.0f - ph);
            Color c{214, 200, 188, static_cast<uint8_t>(150 * a * (1.0f - ph))};
            ctx.r.drawWorldLine({x, y}, {x - side * len, y + 2.0f}, c, std::max(1.0f, 2.0f * px * ctx.r.view().scale));
        }
    }

    // The golem's quake (2.52): glowing cracks that burst into stone spikes, rocks falling from
    // the roof onto their marks.
    for (const Crack& c : cracks_) {
        const Color col = c.color;
        const float line = std::max(1.0f, 2.0f * px * ctx.r.view().scale);
        if (c.sun) {
            drawSunColumn(ctx, c, t);
            continue;
        }
        if (c.rock) {
            const float k = c.delay > 0.0f ? std::clamp(c.age / c.delay, 0.0f, 1.0f) : 1.0f;
            if (!c.bursting()) {
                // Its mark: a shadow widening as it comes, a red line under it.
                const float wdt = c.half * (1.2f + 1.4f * k);
                ctx.r.fillWorldRect(Rect::fromCenter(c.at + Vec2{0.0f, -2.0f}, {wdt * 2.0f, 4.0f}), Color{14, 8, 6, static_cast<uint8_t>(80 + 140 * k)});
                ctx.r.fillWorldRect(Rect::fromCenter(c.at + Vec2{0.0f, -1.0f}, {c.half * 3.0f, 2.0f}),
                                    Color{255, 80, 60, static_cast<uint8_t>(100 + 120 * k)}, BlendMode::Additive);
                // The stone: granite, a rune glowing on it, dust trailing above.
                const Vec2 rp = c.rockAt();
                const float rr = c.half;
                ctx.r.drawWorldLine(rp + Vec2{0.0f, -rr}, rp + Vec2{0.0f, -rr - 26.0f * k - 6.0f}, Color{200, 190, 170, 70},
                                    std::max(1.0f, rr * 0.8f * ctx.r.view().scale), BlendMode::Additive);
                ctx.r.fillWorldCircle(rp, rr + 1.5f, Color{36, 32, 28, 255});
                ctx.r.fillWorldCircle(rp, rr, Color{108, 98, 85, 255});
                ctx.r.fillWorldCircle(rp + Vec2{-rr * 0.25f, -rr * 0.3f}, rr * 0.62f, Color{142, 130, 112, 255});
                ctx.r.fillWorldCircle(rp + Vec2{-rr * 0.35f, -rr * 0.45f}, rr * 0.25f, Color{179, 166, 145, 255});
                ctx.r.drawWorldLine(rp + Vec2{-rr * 0.3f, rr * 0.35f}, rp + Vec2{rr * 0.1f, -rr * 0.3f}, col, line, BlendMode::Additive);
                ctx.r.drawWorldLine(rp + Vec2{rr * 0.1f, -rr * 0.3f}, rp + Vec2{rr * 0.4f, rr * 0.25f}, col, line, BlendMode::Additive);
                ctx.r.drawWorldGlow(rp, rr * 1.6f, col.withAlpha(70));
            } else {
                const float f = std::clamp((c.age - c.delay) / 0.35f, 0.0f, 1.0f);
                ctx.r.drawWorldGlow(c.at + Vec2{0.0f, -c.half}, c.half * (1.5f + f), col.withAlpha(static_cast<uint8_t>(150 * (1.0f - f))));
            }
            continue;
        }
        const float h = c.half;
        if (!c.bursting()) {
            // A crack opening in the ground, brighter as it is about to burst; it trembles at the end.
            const float k = c.delay > 0.0f ? std::clamp(c.age / c.delay, 0.0f, 1.0f) : 1.0f;
            const float shake = k > 0.7f ? dmath::sin(t * 70.0f + c.at.x) * 1.2f : 0.0f;
            const Vec2 base = c.at + Vec2{shake, 0.0f};
            const float open = h * (0.4f + 0.6f * k);
            ctx.r.fillWorldRect({base.x - open, base.y - 3.0f, open * 2.0f, 3.0f}, Color{20, 12, 10, static_cast<uint8_t>(160 + 80 * k)});
            const float thick = std::max(1.0f, (1.0f + 1.5f * k) * px * ctx.r.view().scale);
            Vec2 prev{base.x - open, base.y - 1.5f};
            for (int i = 1; i <= 6; ++i) {
                float x = base.x - open + open * 2.0f * static_cast<float>(i) / 6.0f;
                Vec2 next{x, base.y - 1.5f - ((i % 2) ? 2.0f : 0.0f) * k};
                ctx.r.drawWorldLine(prev, next, col.withAlpha(static_cast<uint8_t>(110 + 140 * k)), thick, BlendMode::Additive);
                prev = next;
            }
            ctx.r.drawWorldGlow(base + Vec2{0.0f, -2.0f}, h * (0.8f + 0.8f * k), col.withAlpha(static_cast<uint8_t>(40 + 110 * k)));
            if (k > 0.5f) ctx.lights.add(base, 50.0f * k, col);
            continue;
        }
        // The burst: stone spikes out of the ground, light under them; they sink back.
        const float e = c.age - c.delay;
        const float up = std::clamp(e / 0.06f, 0.0f, 1.0f) * (1.0f - std::clamp((e - c.burst) / 0.25f, 0.0f, 1.0f));
        if (up <= 0.0f) continue;
        const float top = c.height * up;
        ctx.r.drawWorldGlow(c.at + Vec2{0.0f, -top * 0.5f}, h + top * 0.5f, col.withAlpha(static_cast<uint8_t>(140 * up)));
        const auto& v = ctx.r.view();
        for (int i = 0; i < 3; ++i) {
            const float off = static_cast<float>(i - 1) * h * 0.62f;
            const float sh = top * (i == 1 ? 1.0f : 0.68f);
            const float hw = h * (i == 1 ? 0.5f : 0.38f);
            Vec2 l{c.at.x + off - hw, c.at.y}, r{c.at.x + off + hw, c.at.y}, tip{c.at.x + off * 1.15f, c.at.y - sh};
            ctx.r.fillTriangle(v.toScreen(l + Vec2{-1.0f, 0.0f}), v.toScreen(r + Vec2{1.0f, 0.0f}), v.toScreen(tip + Vec2{0.0f, -1.5f}), Color{36, 32, 28, 255});
            ctx.r.fillTriangle(v.toScreen(l), v.toScreen(r), v.toScreen(tip), Color{108, 98, 85, 255});
            ctx.r.fillTriangle(v.toScreen(l), v.toScreen({(l.x + r.x) * 0.5f, l.y}), v.toScreen(tip), Color{142, 130, 112, 255});
            ctx.r.drawWorldLine({(l.x + r.x) * 0.5f, l.y - 2.0f}, tip + Vec2{0.0f, sh * 0.25f}, col.withAlpha(static_cast<uint8_t>(220 * up)), line,
                                BlendMode::Additive);
        }
        ctx.lights.add(c.at + Vec2{0.0f, -top * 0.5f}, 70.0f, col);
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
