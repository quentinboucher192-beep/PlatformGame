#include "game/enemies/BossHazards.h"

#include <algorithm>
#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/LiquidBody.h"

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

float BossHazards::Press::headY() const {
    // At rest under its housing; it sinks a little while it warns, falls (faster and faster),
    // stays down, then rises.
    const float rest = top + kHead;
    if (safe) return rest;
    if (age < warn) return rest + 6.0f * std::clamp(age / std::max(0.05f, warn), 0.0f, 1.0f);
    const float low = rest + 6.0f;
    if (age < warn + kFall) {
        const float k = (age - warn) / kFall;
        return low + (floor - low) * k * k;
    }
    if (age < warn + kFall + down) return floor;
    const float k = std::clamp((age - warn - kFall - down) / kRise, 0.0f, 1.0f);
    return floor + (rest - floor) * smooth(k);
}

float BossHazards::Belt::running() const {
    if (age < warn) return 0.0f;
    const float k = std::min(1.0f, (age - warn) / 0.25f);
    const float end = std::clamp((warn + time - age) / 0.25f, 0.0f, 1.0f);
    return std::min(k, end);
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

void BossHazards::addRune(const Rune& r) {
    // Two dozen at most per enemy: the oldest goes first.
    if (runeCount(r.owner) >= 24)
        for (auto it = runes_.begin(); it != runes_.end(); ++it)
            if (it->owner == r.owner) {
                runes_.erase(it);
                break;
            }
    runes_.push_back(r);
}

void BossHazards::addPress(const Press& p) { presses_.push_back(p); }

void BossHazards::addMissile(const Missile& m) {
    // A dozen at most per enemy: the oldest goes first.
    if (missileCount(m.owner) >= 12)
        for (auto it = missiles_.begin(); it != missiles_.end(); ++it)
            if (it->owner == m.owner) {
                missiles_.erase(it);
                break;
            }
    missiles_.push_back(m);
}

void BossHazards::addBelt(const Belt& b) {
    belts_.erase(std::remove_if(belts_.begin(), belts_.end(),
                                [&](const Belt& o) { return o.strip.x == b.strip.x && o.strip.y == b.strip.y && o.strip.w == b.strip.w; }),
                 belts_.end());
    belts_.push_back(b);
}

void BossHazards::addSteam(const Steam& s) { steams_.push_back(s); }

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
    w.forEachPlayer([&](Player& p) {  // 2.62: every player of a team (alone: the only one)
        if (!p.alive() || !p.box.intersects(rect)) return;
        // Never inside a wall: Nova is put on the cage's side of it.
        float x = inside >= 0.0f ? rect.right() + 0.5f : rect.x - p.box.w - 0.5f;
        Rect moved = p.box;
        moved.x = x;
        if (!w.physics().overlapsSolid(moved, p.moveOptions())) {
            p.box = moved;
            p.prevBox.x = x;
        }
    });
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
    runes_.erase(std::remove_if(runes_.begin(), runes_.end(), [&](const Rune& r) { return r.owner == owner; }), runes_.end());
    presses_.erase(std::remove_if(presses_.begin(), presses_.end(), [&](const Press& p) { return p.owner == owner; }), presses_.end());
    missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [&](const Missile& m) { return m.owner == owner; }), missiles_.end());
    belts_.erase(std::remove_if(belts_.begin(), belts_.end(), [&](const Belt& b) { return b.owner == owner; }), belts_.end());
    steams_.erase(std::remove_if(steams_.begin(), steams_.end(), [&](const Steam& s) { return s.owner == owner; }), steams_.end());
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
    runes_.clear();
    presses_.clear();
    missiles_.clear();
    belts_.clear();
    steams_.clear();
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
int BossHazards::runeCount(uint32_t owner) const {
    return static_cast<int>(std::count_if(runes_.begin(), runes_.end(), [&](const Rune& r) { return r.owner == owner; }));
}
int BossHazards::pressCount(uint32_t owner) const {
    return static_cast<int>(std::count_if(presses_.begin(), presses_.end(), [&](const Press& p) { return p.owner == owner; }));
}
int BossHazards::missileCount(uint32_t owner) const {
    return static_cast<int>(std::count_if(missiles_.begin(), missiles_.end(), [&](const Missile& m) { return m.owner == owner; }));
}
bool BossHazards::beltsRunning(uint32_t owner) const {
    return std::any_of(belts_.begin(), belts_.end(), [&](const Belt& b) { return b.owner == owner; });
}
const BossHazards::Belt* BossHazards::beltOn(const Rect& strip) const {
    for (const Belt& b : belts_)
        if (std::abs(b.strip.x - strip.x) < 1.0f && std::abs(b.strip.y - strip.y) < 1.0f && std::abs(b.strip.w - strip.w) < 1.0f) return &b;
    return nullptr;
}

// ================================================================= stepping

void BossHazards::fixedUpdate(GameWorld& w, float dt) {
    if (empty()) return;
    // 2.62: every player in play (alone: the only one), each exposed or not as at the start of
    // the step (whatever happens to it during the step, as it always was).
    bool exposedAt[GameWorld::kMaxTeam] = {};
    w.forEachPlayer([&](Player& p) {
        exposedAt[w.currentPlayer()] = p.alive() && !p.respawning() && w.state() == WorldState::Playing;
    });
    auto exposed = [&]() { return exposedAt[w.currentPlayer()]; };

    // Pulls: Nova drifts toward the centre (outside her own movement, so her controls stay
    // whole: running the other way always wins).
    for (Pull& pl : pulls_) {
        pl.age += dt;
        pl.time -= dt;
        w.forEachPlayer([&](Player& p) {
            if (!exposed()) return;
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
        });
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
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || f.level() < 2.0f) return;
            float cx = p.center().x;
            if (cx < f.left || cx > f.right) return;
            float surface = f.surfaceY();
            if (p.box.bottom() <= surface + 3.0f) return;
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
        });
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
        w.forEachPlayer([&](Player& p) {
            if (exposed() && wl.damage > 0.0f && p.box.expanded(1.5f).intersects(wl.rect)) {
                DamageInfo dmg;
                dmg.amount = wl.damage;
                dmg.type = DamageType::Contact;
                dmg.source = {wl.rect.center().x, p.center().y};
                dmg.knockback = 200.0f;
                w.damagePlayer(dmg);
            }
        });
    }
    walls_.erase(std::remove_if(walls_.begin(), walls_.end(), [](const Wall& wl) { return wl.time <= 0.0f; }), walls_.end());

    // The wind of the dragon's wings (2.50): Nova drifts away from it, horizontally (outside her
    // own movement: running against it still wins, a jump or a dash too).
    for (Wind& wd : winds_) {
        wd.age += dt;
        wd.time -= dt;
        w.forEachPlayer([&](Player& p) {
            if (!exposed()) return;
            Vec2 pc = p.center();
            float dist = distance(pc, wd.center);
            if (dist >= wd.range) return;
            float side = pc.x >= wd.center.x ? 1.0f : -1.0f;
            float k = (0.5f + 0.5f * (1.0f - dist / wd.range)) * std::min(1.0f, wd.age / 0.25f);
            w.physics().move(p.box, Vec2{side * wd.speed * k * dt, 0.0f}, p.moveOptions());
        });
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
                // 2.56: the Phénix's ash sheds ash and a few embers.
                w.particles().emit(c.ink ? "bubbles" : (c.ash ? "ash_fall" : "fire_cloud"), at, 1.0f, {}, &c.color);
                if (w.fxRng().chance(0.35f) && !c.ink) w.particles().emit(c.ash ? "embers_rising" : "fire_smoke", at + Vec2{0.0f, -r.h * 0.3f}, 1.0f);
            }
        }
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || c.damage <= 0.0f || !c.burning() || !c.current().intersects(p.box)) return;
            DamageInfo dmg;
            dmg.amount = c.damage;
            dmg.type = DamageType::Contact;
            dmg.source = {c.area.center().x, p.center().y + 6.0f};  // out of it by the nearest side
            dmg.knockback = 150.0f;
            if (w.damagePlayer(dmg)) w.particles().emit("fire_burst", p.center(), 0.8f);
        });
    }
    clouds_.erase(std::remove_if(clouds_.begin(), clouds_.end(), [](const Cloud& c) { return c.time <= 0.0f; }), clouds_.end());

    // The golem's quake (2.52): the cracks burst out of the ground, the rocks land on their marks.
    for (Crack& c : cracks_) {
        const bool was = c.bursting();
        c.age += dt;
        if (!was && c.bursting()) {
            if (c.sun) {
                // 2.56: the column of sunlight strikes the ground.
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
        if (c.hit || c.damage <= 0.0f) continue;
        Rect hurt;
        if (c.rock && !c.bursting()) {
            hurt = Rect::fromCenter(c.rockAt(), {c.half * 1.6f, c.half * 1.6f});  // the stone on its way down
        } else if (c.bursting() && c.age < c.delay + c.burst) {
            const float w2 = c.rock ? c.half * 1.3f : c.half;
            hurt = {c.at.x - w2, c.at.y - c.height, w2 * 2.0f, c.height};
        } else {
            continue;
        }
        w.forEachPlayer([&](Player& p) {  // every player it reaches in this step, once
            if (!exposed() || !hurt.intersects(p.box)) return;
            DamageInfo dmg;
            dmg.amount = c.damage;
            dmg.type = DamageType::Contact;
            dmg.source = c.rock && !c.bursting() ? c.rockAt() + Vec2{0.0f, -c.half} : Vec2{c.at.x, c.at.y + 12.0f};
            dmg.knockback = c.knockback;
            if (w.damagePlayer(dmg)) c.hit = true;
        });
    }
    cracks_.erase(std::remove_if(cracks_.begin(), cracks_.end(), [](const Crack& c) { return c.done(); }), cracks_.end());

    // 2.57, the Liche's runes: armed, Nova's feet make one burst; at their end they all burst.
    for (Rune& r : runes_) {
        r.age += dt;
        if (r.bursting()) {
            r.burstAge += dt;
        } else {
            const bool stepped = r.armed() && w.anyPlayerWhere([&](const Player& p) { return exposed() && r.pad().intersects(p.box); });
            if (stepped || r.age >= r.arm + r.time) {
                r.burstAge = 0.0f;
                w.particles().emit("eruption_fire", r.at, 1.0f, {}, &r.color);
                w.particles().emit("rune_glow", r.at + Vec2{0.0f, -10.0f}, 2.0f, {}, &r.color);
                w.playSound("sounds/root_burst.wav", r.at, 0.55f, 1.35f);
                w.shake(0.08f);
            }
        }
        if (r.hit || r.damage <= 0.0f || !r.bursting() || r.burstAge > Rune::kBurst) continue;
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || !r.pillar().intersects(p.box)) return;
            DamageInfo dmg;
            dmg.amount = r.damage;
            dmg.type = DamageType::Contact;
            dmg.source = {r.at.x, r.at.y + 14.0f};  // from under her: thrown up
            dmg.knockback = r.knockback;
            if (w.damagePlayer(dmg)) r.hit = true;
        });
    }
    runes_.erase(std::remove_if(runes_.begin(), runes_.end(), [](const Rune& r) { return r.done(); }), runes_.end());

    // 2.58, the Titan's presses: falling and down, the column under the head crushes (once).
    for (Press& pr : presses_) {
        const bool wasFalling = pr.age < pr.warn + Press::kFall;
        pr.age += dt;
        if (!pr.safe && wasFalling && pr.age >= pr.warn + Press::kFall) {
            w.particles().emit("slam_dust", {pr.x, pr.floor}, 1.6f);
            w.particles().emit("steam_burst", {pr.x, pr.top + 10.0f}, 0.6f);
            w.playSound("sounds/golem_slam.wav", {pr.x, pr.floor}, 0.75f, 1.25f);
            w.shake(0.18f);
        }
        if (pr.hit || pr.damage <= 0.0f || !pr.crushing()) continue;
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || !pr.column().intersects(p.box)) return;
            DamageInfo dmg;
            dmg.amount = pr.damage;
            dmg.type = DamageType::Contact;
            dmg.source = {pr.x + (p.center().x >= pr.x ? -1.0f : 1.0f) * pr.half * 0.5f, p.center().y + 6.0f};  // thrown out sideways
            dmg.knockback = pr.knockback;
            if (w.damagePlayer(dmg)) pr.hit = true;
        });
    }
    presses_.erase(std::remove_if(presses_.begin(), presses_.end(), [](const Press& pr) { return pr.done(); }), presses_.end());

    // 2.58, its missiles: the reticle follows Nova (slower than she runs), locks, the missile falls.
    // 2.62: in a team, the nearest player in play.
    for (Missile& m : missiles_) {
        const bool wasFlying = !m.landed();
        {
            GameWorld::PlayerScope target(w, w.targetIndex(m.at));
            const Player& p = w.player();
            if (!m.locked() && exposed()) {
                const Vec2 d = p.center() - m.at;
                const float len = d.length();
                const float step = Missile::kTrackSpeed * dt;
                m.at = len <= step ? p.center() : m.at + d / len * step;
            }
        }
        const bool wasTracking = !m.locked();
        m.age += dt;
        if (wasTracking && m.locked()) w.playSound("sounds/timer_tick.wav", m.at, 0.6f, 1.5f);
        if (!(wasFlying && m.landed())) continue;
        w.particles().emit("explosion", m.at, std::max(0.8f, m.radius / 30.0f));
        w.playSound("sounds/bomb_explode.wav", m.at, 0.7f, 1.1f);
        w.shake(0.2f);
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || m.damage <= 0.0f || !Collision::circleIntersectsRect(m.at, m.radius, p.box)) return;
            DamageInfo dmg;
            dmg.amount = m.damage;
            dmg.type = DamageType::Explosion;
            dmg.source = m.at + Vec2{0.0f, 8.0f};
            dmg.knockback = m.knockback;
            if (w.damagePlayer(dmg)) m.hit = true;
        });
    }
    missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [](const Missile& m) { return m.done(); }), missiles_.end());

    // 2.58, its belts: Nova standing on one is carried along (outside her own movement: running
    // against it still wins, a jump frees her).
    for (Belt& b : belts_) {
        b.age += dt;
        const float k = b.running();
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || k <= 0.0f || !p.grounded()) return;
            const float feet = p.box.bottom();
            const float cx = p.center().x;
            if (std::abs(feet - b.strip.y) > 3.0f || cx < b.strip.x || cx > b.strip.right()) return;
            w.physics().move(p.box, Vec2{b.speed * k * dt, 0.0f}, p.moveOptions());
        });
    }
    belts_.erase(std::remove_if(belts_.begin(), belts_.end(), [](const Belt& b) { return b.done(); }), belts_.end());

    // 2.58, the jets of its vents: low along the ground, they scald (once) and push away.
    for (Steam& s : steams_) {
        const bool wasWarning = s.age < s.warn;
        s.age += dt;
        if (wasWarning && s.blasting()) w.playSound("sounds/lava_rise.wav", s.from, 0.7f, 1.6f);
        if (s.hit || s.damage <= 0.0f || !s.blasting()) continue;
        w.forEachPlayer([&](Player& p) {
            if (!exposed() || !s.area().intersects(p.box)) return;
            DamageInfo dmg;
            dmg.amount = s.damage;
            dmg.type = DamageType::Contact;
            dmg.source = {s.from.x - s.dir * 20.0f, p.center().y + 4.0f};
            dmg.knockback = s.knockback;
            if (w.damagePlayer(dmg)) s.hit = true;
        });
    }
    steams_.erase(std::remove_if(steams_.begin(), steams_.end(), [](const Steam& s) { return s.done(); }), steams_.end());
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

/// A wall of bones (2.57, the Liche's « Murs d'os »): bones stacked crosswise, a skull every so
/// often, ribs curling out of its sides, the soul fire of the phase glowing between them. Display
/// only (time-driven).
void drawBoneWall(RenderContext& ctx, const BossHazards::Wall& wl, const Rect& vis, float t) {
    if (vis.h < 1.0f) return;
    const Color soul = wl.color;
    const float line = std::max(1.0f, 0.9f * ctx.r.view().scale);
    const float cx = vis.center().x;
    ctx.r.drawWorldGlow(vis.center(), std::max(vis.w * 2.0f, vis.h * 0.5f), soul.withAlpha(60));
    ctx.r.fillWorldRect({cx - vis.w * 0.35f, vis.y, vis.w * 0.7f, vis.h}, Color{40, 34, 30, 255});
    int k = 0;
    for (float y = vis.bottom() - 4.0f; y > vis.y + 2.0f; y -= 9.0f, ++k) {
        // A bone across the stack (alternating slants), its two knobs.
        const float slant = (k % 2 ? 1.0f : -1.0f) * 3.0f;
        const Vec2 a{cx - vis.w * 0.75f, y + slant}, b{cx + vis.w * 0.75f, y - slant};
        ctx.r.drawWorldLine(a, b, Color{120, 108, 86, 255}, line * 3.2f);
        ctx.r.drawWorldLine(a, b, Color{226, 214, 184, 255}, line * 1.8f);
        for (const Vec2& e : {a, b}) ctx.r.fillWorldCircle(e, 2.4f, Color{214, 202, 172, 255});
        if (k % 4 == 2) {
            // A skull wedged in the stack, its eyes burning with the phase.
            const Vec2 sk{cx + (k % 8 == 2 ? -1.5f : 1.5f), y - 4.0f};
            ctx.r.fillWorldCircle(sk, 4.6f, Color{232, 222, 196, 255});
            ctx.r.fillWorldRect({sk.x - 2.6f, sk.y + 2.0f, 5.2f, 3.2f}, Color{232, 222, 196, 255});
            const float flick = 0.7f + 0.3f * dmath::sin(t * 9.0f + y);
            ctx.r.fillWorldCircle(sk + Vec2{-1.7f, -0.3f}, 1.2f, soul.withAlpha(static_cast<uint8_t>(255 * flick)));
            ctx.r.fillWorldCircle(sk + Vec2{1.7f, -0.3f}, 1.2f, soul.withAlpha(static_cast<uint8_t>(255 * flick)));
        }
        if (k % 3 == 1) {
            // A rib curling out of the side it faces (the cage's inside).
            const float dir = wl.side >= 0.0f ? 1.0f : -1.0f;
            const Vec2 r0{cx + dir * vis.w * 0.4f, y}, r1{cx + dir * vis.w * 1.1f, y - 5.0f}, r2{cx + dir * vis.w * 1.3f, y - 11.0f};
            ctx.r.drawWorldLine(r0, r1, Color{220, 208, 178, 255}, line * 1.4f);
            ctx.r.drawWorldLine(r1, r2, Color{220, 208, 178, 255}, line * 1.1f);
        }
    }
    // Soul fire seeping between the bones.
    for (int i = 0; i < 3; ++i) {
        const float ph = std::fmod(t * 0.8f + static_cast<float>(i) / 3.0f, 1.0f);
        ctx.r.drawWorldGlow({cx, vis.bottom() - ph * vis.h}, 5.0f, soul.withAlpha(static_cast<uint8_t>(180 * (1.0f - ph))));
    }
    ctx.lights.add(vis.center(), 70.0f, soul);
}

/// A column of sunlight (2.56, the Phénix's « Colonnes solaires »): first a thread of light from
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

    // 2.60: what does not hurt (frozen ground, the wind of wings, conveyor belts) is no threat
    // the HUD should fade over (Renderer::listener, RenderOptions::threats).
    ShapeListener* const threats = ctx.r.listener();
    struct Heard {
        Renderer& r;
        ShapeListener* l;
        ~Heard() { r.setListener(l); }
    } heard{ctx.r, threats};

    // Frozen ground: a pale veil on every frozen surface.
    AnimationSetPtr fx = sheetOf("frost_floor");
    const AnimationClip* veil = clipIn(fx, "idle");
    ctx.r.setListener(nullptr);
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

    ctx.r.setListener(threats);

    // Lava tide (or star plasma 2.33, the dark water of the abyss 2.53, Voidworm's acid): a body of
    // liquid filling its span wall to wall, from its swell down into the ground under its floor
    // (game/gameplay/LiquidBody.h); the lava and the plasma keep their sheet's surface over it.
    AnimationSetPtr lavaSheet = sheetOf("lava_flood");
    AnimationSetPtr plasmaFx = sheetOf("plasma_flood");
    for (const Flood& f : floods_) {
        if (f.level() < 0.5f) continue;
        const Rect body = f.bodyRect();
        drawLiquidBody(ctx, body, f.floorY + Flood::kSolidBelow, liquidLook(f.look, f.color), t);
        if (f.look == "tide" || f.look == "acid") continue;
        const bool plasma = f.look == "plasma";
        const AnimationSetPtr& lavaFx = plasma && clipIn(plasmaFx, "surface") ? plasmaFx : lavaSheet;
        const AnimationClip* lava = clipIn(lavaFx, "surface");
        if (!lava) continue;
        const int frame = static_cast<int>(t * 10.0f) % std::max(1, lava->frameCount());
        const Rect src = lavaFx->frameRect(*lava, frame);
        const float fw = static_cast<float>(lavaFx->frameWidth), fh = static_cast<float>(lavaFx->frameHeight);
        const Rect vis = ctx.r.view().visibleWorld().expanded(fw);
        for (float x = f.left + std::max(0.0f, std::floor((vis.x - f.left) / fw) * fw); x < std::min(f.right, vis.right()); x += fw) {
            const float wdt = std::min(fw, f.right - x);
            Rect s = src;
            s.w = src.w * (wdt / fw);
            ctx.r.drawWorldTexture(*lavaFx->texture, &s, {x, body.y - fh * 0.5f, wdt, fh});
        }
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
        if (wl.look == "bone") {  // 2.57: the Liche's walls of bones
            drawBoneWall(ctx, wl, vis, t);
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
                    // 2.56, the Phénix: thick grey ash billowing, embers glinting in it.
                    ctx.r.drawWorldGlow(pc, rad * 1.6f, fire.withAlpha(static_cast<uint8_t>(40 * a)));
                    // Thick, but Nova stays visible inside it (the ash veils, it never hides her).
                    ctx.r.fillWorldCircle(pc, rad, Color{92, 84, 80, static_cast<uint8_t>(90 * a)});
                    ctx.r.fillWorldCircle(pc + Vec2{-rad * 0.25f, -rad * 0.3f}, rad * 0.55f, Color{150, 140, 132, static_cast<uint8_t>(45 * a)});
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
    ctx.r.setListener(nullptr);
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

    ctx.r.setListener(threats);

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

    // 2.57, the Liche's runes: a circle traced on the ground (seen slantwise), faint while it is
    // being traced, bright once armed, flickering before its end; its burst, a pillar of soul fire.
    for (const Rune& r : runes_) {
        const Color col = r.color;
        const float line = std::max(1.0f, 1.6f * px * ctx.r.view().scale);
        if (r.bursting()) {
            const float e = r.burstAge;
            const float up = std::clamp(e / 0.05f, 0.0f, 1.0f) * (1.0f - std::clamp((e - Rune::kBurst) / 0.25f, 0.0f, 1.0f));
            if (up <= 0.0f) continue;
            const Rect pil = r.pillar();
            ctx.r.fillWorldRect(pil.expanded(4.0f), col.withAlpha(static_cast<uint8_t>(60 * up)), BlendMode::Additive);
            ctx.r.fillWorldRect(pil, col.withAlpha(static_cast<uint8_t>(170 * up)), BlendMode::Additive);
            ctx.r.fillWorldRect(Rect::fromCenter(pil.center(), {pil.w * 0.35f, pil.h}), Color{240, 255, 236, static_cast<uint8_t>(210 * up)},
                                BlendMode::Additive);
            for (int i = 0; i < 4; ++i) {
                const float ph = std::fmod(t * 2.6f + static_cast<float>(i) * 0.25f, 1.0f);
                const float y = pil.bottom() - ph * pil.h;
                const float xo = dmath::sin(t * 9.0f + static_cast<float>(i) * 1.7f) * pil.w * 0.3f;
                ctx.r.drawWorldGlow({pil.center().x + xo, y}, 5.0f + 4.0f * (1.0f - ph), col.withAlpha(static_cast<uint8_t>(200 * up * (1.0f - ph))));
            }
            ctx.r.drawWorldGlow(r.at + Vec2{0.0f, -4.0f}, r.half * 2.0f, col.withAlpha(static_cast<uint8_t>(200 * up)));
            ctx.lights.add(r.at + Vec2{0.0f, -r.height * 0.5f}, 90.0f, col.withAlpha(static_cast<uint8_t>(255 * up)));
            continue;
        }
        const float k = r.arm > 0.0f ? std::clamp(r.age / r.arm, 0.0f, 1.0f) : 1.0f;
        float a = 0.25f + 0.55f * k;
        const float left = r.arm + r.time - r.age;
        if (r.armed()) {
            a = 0.8f + 0.2f * dmath::sin(t * 6.0f + r.at.x * 0.05f);
            if (left < Rune::kWarn) a = (static_cast<int>(t * 18.0f) % 2) ? 1.0f : 0.35f;  // about to burst
        }
        const Color c = col.withAlpha(static_cast<uint8_t>(255 * a));
        // The circle (an ellipse on the ground) traced as it is drawn: more of it as it arms.
        const float rx = r.half, ry = std::max(3.0f, r.half * 0.3f);
        const Vec2 o = r.at + Vec2{0.0f, -ry * 0.6f};
        constexpr int kSeg = 20;
        const int shown = r.armed() ? kSeg : std::max(2, static_cast<int>(static_cast<float>(kSeg) * k));
        for (int i = 0; i < shown; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / kSeg + static_cast<float>(r.glyph);
            const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSeg + static_cast<float>(r.glyph);
            ctx.r.drawWorldLine(o + Vec2{dmath::cos(a0) * rx, dmath::sin(a0) * ry}, o + Vec2{dmath::cos(a1) * rx, dmath::sin(a1) * ry}, c, line,
                                BlendMode::Additive);
        }
        // Its signs: three strokes across it, different from one rune to the next.
        for (int j = 0; j < 3 && r.armed(); ++j) {
            const float u = static_cast<float>((r.glyph * 7 + j * 3) % 5) / 4.0f - 0.5f;
            const Vec2 p0 = o + Vec2{(u - 0.25f) * rx, -ry * 0.4f + static_cast<float>(j) * ry * 0.4f};
            const Vec2 p1 = o + Vec2{(u + 0.25f) * rx, ry * 0.3f - static_cast<float>(j) * ry * 0.2f};
            ctx.r.drawWorldLine(p0, p1, c, line, BlendMode::Additive);
        }
        ctx.r.drawWorldGlow(o, r.half * (0.8f + 0.5f * a), col.withAlpha(static_cast<uint8_t>(30 + 90 * a)));
        if (r.armed()) {
            // Wisps rising from it, faster as it is about to burst.
            const float speed = left < Rune::kWarn ? 3.0f : 1.2f;
            for (int i = 0; i < 2; ++i) {
                const float ph = std::fmod(t * speed + static_cast<float>(i) * 0.5f + r.at.x * 0.013f, 1.0f);
                ctx.r.drawWorldGlow(o + Vec2{(static_cast<float>(i) - 0.5f) * rx * 0.8f, -ph * 22.0f}, 3.0f,
                                    col.withAlpha(static_cast<uint8_t>(160 * (1.0f - ph))));
            }
            ctx.lights.add(o, 45.0f, col);
        }
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

    renderTitan(ctx);
}

void BossHazards::renderTitan(RenderContext& ctx) const {
    // 2.58, the Titan's machinery. Display only (time-driven).
    const float t = ctx.time;
    const float line = std::max(1.0f, ctx.r.view().scale * 0.6f);

    // Its belts: chevrons running along them (flashing before they start). 2.60: they do not
    // hurt, no threat the HUD should fade over.
    ShapeListener* const threats = ctx.r.listener();
    ctx.r.setListener(nullptr);
    for (const Belt& b : belts_) {
        const float k = b.running();
        const float dir = b.speed >= 0.0f ? 1.0f : -1.0f;
        const bool warn = b.age < b.warn;
        const uint8_t a = warn ? ((static_cast<int>(t * 10.0f) % 2) ? 230 : 70) : static_cast<uint8_t>(120 + 110 * k);
        const float shift = std::fmod(t * std::abs(b.speed) * (warn ? 0.0f : std::max(k, 0.3f)), 48.0f) * dir;
        for (float x = b.strip.x + 16.0f; x < b.strip.right() - 8.0f; x += 48.0f) {
            float cx = x + shift;
            if (cx < b.strip.x + 6.0f) cx += 48.0f;
            if (cx > b.strip.right() - 6.0f) cx -= 48.0f;
            const Vec2 at{cx, b.strip.y - 9.0f};
            ctx.r.drawWorldLine(at + Vec2{-dir * 4.0f, -4.0f}, at + Vec2{dir * 4.0f, 0.0f}, b.color.withAlpha(a), line * 1.6f, BlendMode::Additive);
            ctx.r.drawWorldLine(at + Vec2{-dir * 4.0f, 4.0f}, at + Vec2{dir * 4.0f, 0.0f}, b.color.withAlpha(a), line * 1.6f, BlendMode::Additive);
        }
    }

    ctx.r.setListener(threats);

    // Its presses: stripes where they will stamp, the rod and the head coming down.
    for (const Press& pr : presses_) {
        const float x0 = pr.x - pr.half, w = pr.half * 2.0f;
        if (pr.safe) {
            // The column spared: a green light on the ground and up its column.
            const float pulse = 0.5f + 0.5f * dmath::sin(t * 6.0f);
            const float fade = std::min({1.0f, pr.age / 0.2f, (pr.warn + Press::kFall + pr.down + Press::kRise - pr.age) / 0.3f});
            if (fade <= 0.0f) continue;
            const uint8_t ga = static_cast<uint8_t>((120 + 100 * pulse) * fade);
            ctx.r.fillWorldRect({x0 + 6.0f, pr.floor - 4.0f, w - 12.0f, 4.0f}, Color{90, 255, 140, ga}, BlendMode::Additive);
            ctx.r.fillWorldRect({x0 + 10.0f, pr.top, w - 20.0f, pr.floor - pr.top}, Color{90, 255, 140, static_cast<uint8_t>(26 * fade)},
                                BlendMode::Additive);
            ctx.r.drawWorldGlow({pr.x, pr.floor - 6.0f}, pr.half * 0.7f, Color{90, 255, 140, static_cast<uint8_t>(90 * fade)});
            ctx.lights.add({pr.x, pr.floor - 20.0f}, 90.0f, Color{90, 255, 140, 255});
            continue;
        }
        if (pr.age < pr.warn) {
            // Stripes on the ground, faster as it is about to fall; its column lit.
            const float left = pr.warn - pr.age;
            const bool on = left > 0.45f ? (static_cast<int>(t * 6.0f) % 2 == 0) : (static_cast<int>(t * 16.0f) % 2 == 0);
            const uint8_t a = static_cast<uint8_t>(on ? 230 : 120);
            const float y = pr.floor;
            ctx.r.fillWorldRect({x0 + 4.0f, y - 5.0f, w - 8.0f, 5.0f}, Color{20, 18, 16, 200});
            for (float x = x0 + 4.0f; x < x0 + w - 4.0f; x += 10.0f)
                ctx.r.fillWorldRect({x, y - 5.0f, std::min(5.0f, x0 + w - 4.0f - x), 5.0f}, Color{255, 200, 40, a});
            const float k = std::clamp(pr.age / std::max(0.05f, pr.warn), 0.0f, 1.0f);
            ctx.r.fillWorldRect({x0 + 4.0f, pr.top, w - 8.0f, y - pr.top}, Color{255, 120, 40, static_cast<uint8_t>(10 + 24 * k)}, BlendMode::Additive);
            ctx.r.drawWorldGlow({pr.x, y - 4.0f}, pr.half * 0.6f, pr.color.withAlpha(static_cast<uint8_t>(40 + 80 * k)));
        }
        // The rod (chrome) and the head (dark steel, a band of stripes, bolts).
        float hy = pr.headY();
        if (pr.age < pr.warn) hy += dmath::sin(t * 60.0f + pr.x) * 1.2f;  // it shudders
        const float headTop = hy - Press::kHead;
        if (headTop > pr.top + 1.0f) {
            ctx.r.fillWorldRect({pr.x - 8.0f, pr.top, 16.0f, headTop - pr.top}, Color{36, 40, 48, 255});
            ctx.r.fillWorldRect({pr.x - 6.0f, pr.top, 12.0f, headTop - pr.top}, Color{104, 112, 126, 255});
            ctx.r.fillWorldRect({pr.x - 3.0f, pr.top, 2.0f, headTop - pr.top}, Color{176, 184, 196, 255});
        }
        ctx.r.fillWorldRect({x0, headTop, w, Press::kHead}, Color{40, 44, 52, 255});
        ctx.r.fillWorldRect({x0 + 2.0f, headTop + 2.0f, w - 4.0f, Press::kHead - 4.0f}, Color{92, 100, 114, 255});
        ctx.r.fillWorldRect({x0 + 2.0f, headTop + 2.0f, w - 4.0f, 3.0f}, Color{150, 160, 176, 255});
        for (float x = x0 + 4.0f; x < x0 + w - 4.0f; x += 12.0f)
            ctx.r.fillWorldRect({x, hy - 9.0f, std::min(6.0f, x0 + w - 4.0f - x), 5.0f}, Color{240, 190, 40, 255});
        for (float bx : {x0 + 6.0f, x0 + w - 6.0f}) ctx.r.fillWorldCircle({bx, headTop + 7.0f}, 2.0f, Color{200, 170, 90, 255});
        if (pr.crushing()) {
            ctx.r.fillWorldRect({x0 - 2.0f, hy - 3.0f, w + 4.0f, 3.0f}, pr.color.withAlpha(180), BlendMode::Additive);
            ctx.lights.add({pr.x, hy - 8.0f}, 70.0f, pr.color);
        }
    }

    // Its missiles: the reticle (yellow while it follows, red once locked, the ring of the time
    // left), the missile falling in the last part of its flight, the flash of its blast.
    for (const Missile& m : missiles_) {
        const bool locked = m.locked();
        const float spin = t * (locked ? 1.5f : 4.0f) + static_cast<float>(m.index);
        const Color c = locked ? Color{255, 70, 60, 255} : Color{255, 210, 60, 255};
        if (!m.landed()) {
            const float appear = std::min(1.0f, m.age / 0.2f);
            const uint8_t a = static_cast<uint8_t>(230 * appear);
            const float r = m.radius;
            ctx.r.drawWorldCircle(m.at, r, c.withAlpha(static_cast<uint8_t>(a * 0.8f)), line * 1.4f);
            ctx.r.drawWorldCircle(m.at, r * 0.35f, c.withAlpha(a), line);
            for (int k = 0; k < 4; ++k) {
                const float ang = spin + static_cast<float>(k) * 1.5707963f;
                const Vec2 d{dmath::cos(ang), dmath::sin(ang)};
                ctx.r.drawWorldLine(m.at + d * (r * 0.55f), m.at + d * (r * 1.15f), c.withAlpha(a), line * 1.6f, BlendMode::Additive);
            }
            if (locked) {
                const float left = std::clamp(1.0f - (m.age - m.track) / std::max(0.05f, m.flight), 0.0f, 1.0f);
                const int seg = 24, shown = std::max(1, static_cast<int>(seg * left));
                for (int i = 0; i < shown; ++i) {
                    const float a0 = -1.5707963f + 6.2831853f * static_cast<float>(i) / seg;
                    const float a1 = -1.5707963f + 6.2831853f * static_cast<float>(i + 1) / seg;
                    ctx.r.drawWorldLine(m.at + Vec2{dmath::cos(a0), dmath::sin(a0)} * (r + 5.0f),
                                        m.at + Vec2{dmath::cos(a1), dmath::sin(a1)} * (r + 5.0f), Color{255, 90, 70, 220}, line * 1.3f,
                                        BlendMode::Additive);
                }
                ctx.r.fillWorldCircle(m.at, r, Color{255, 60, 40, static_cast<uint8_t>(26 + 30 * (1.0f - left))});
                // The missile comes down in the last 0.45 s of its flight.
                const float fall = m.age - (m.track + m.flight - 0.45f);
                if (fall > 0.0f) {
                    const float k = std::clamp(fall / 0.45f, 0.0f, 1.0f);
                    const Vec2 tip = m.at + Vec2{-40.0f * (1.0f - k), -340.0f * (1.0f - k * k)};
                    const Vec2 tail = tip + Vec2{-12.0f, -40.0f};
                    ctx.r.drawWorldLine(tail, tip, Color{60, 64, 74, 255}, std::max(2.0f, 3.6f * line));
                    ctx.r.drawWorldLine(tail, tip, Color{170, 178, 190, 255}, std::max(1.0f, 1.6f * line));
                    ctx.r.drawWorldGlow(tail, 9.0f, Color{255, 170, 70, 220});
                    ctx.r.drawWorldGlow(tail + Vec2{-6.0f, -18.0f}, 7.0f, Color{200, 200, 210, 120});
                    ctx.lights.add(tail, 60.0f, Color{255, 170, 70, 255});
                }
            }
            // Its launch: a streak rising out of the Titan's back.
            if (m.age < 0.5f) {
                const float k = m.age / 0.5f;
                const Vec2 a0 = m.from + Vec2{0.0f, -260.0f * k * k}, a1 = a0 + Vec2{0.0f, 26.0f};
                ctx.r.drawWorldLine(a0, a1, Color{220, 224, 232, static_cast<uint8_t>(230 * (1.0f - k))}, std::max(1.0f, 2.4f * line));
                ctx.r.drawWorldGlow(a1, 8.0f, Color{255, 170, 70, static_cast<uint8_t>(220 * (1.0f - k))});
            }
            ctx.lights.add(m.at, 40.0f, c);
        } else {
            const float k = std::clamp((m.age - m.track - m.flight) / Missile::kBoom, 0.0f, 1.0f);
            ctx.r.drawWorldGlow(m.at, m.radius * (1.0f + k), Color{255, 200, 120, static_cast<uint8_t>(220 * (1.0f - k))});
            ctx.r.drawWorldCircle(m.at, m.radius * (0.6f + 0.8f * k), Color{255, 140, 60, static_cast<uint8_t>(200 * (1.0f - k))}, line * 2.0f);
            ctx.lights.add(m.at, m.radius * 3.0f, Color{255, 160, 80, static_cast<uint8_t>(255 * (1.0f - k))});
        }
    }

    // The jets of its vents: puffs while they hiss, then a roaring jet of white steam.
    for (const Steam& s : steams_) {
        const Rect r = s.area();
        if (s.age < s.warn) {
            const float k = s.age / std::max(0.05f, s.warn);
            for (int i = 0; i < 3; ++i) {
                const Vec2 at = s.from + Vec2{s.dir * (8.0f + 10.0f * static_cast<float>(i)) * k, -6.0f - 3.0f * static_cast<float>(i)};
                ctx.r.drawWorldGlow(at, 6.0f + 4.0f * k, Color{240, 240, 245, static_cast<uint8_t>(90 + 60 * k)});
            }
            continue;
        }
        const float end = s.warn + s.time;
        const float fade = s.age < end ? std::min(1.0f, (s.age - s.warn) / 0.1f) : std::clamp(1.0f - (s.age - end) / 0.4f, 0.0f, 1.0f);
        if (fade <= 0.0f) continue;
        for (int i = 0; i < 9; ++i) {
            const float u = (static_cast<float>(i) + 0.5f) / 9.0f;
            const float wob = dmath::sin(t * 30.0f + static_cast<float>(i) * 1.7f) * 2.5f;
            const Vec2 at{s.from.x + s.dir * u * s.length, s.from.y - s.height * (0.35f + 0.25f * u) + wob};
            ctx.r.drawWorldGlow(at, s.height * (0.45f + 0.5f * u), Color{245, 245, 250, static_cast<uint8_t>(150 * fade * (1.0f - 0.5f * u))});
        }
        ctx.r.fillWorldRect(r, Color{255, 255, 255, static_cast<uint8_t>(40 * fade)}, BlendMode::Additive);
        ctx.lights.add(r.center(), 70.0f, Color{230, 230, 240, static_cast<uint8_t>(200 * fade)});
    }
}

}  // namespace cosmic
