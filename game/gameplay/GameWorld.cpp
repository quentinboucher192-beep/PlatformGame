#include "game/gameplay/GameWorld.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/audio/AudioSystem.h"
#include "engine/core/DetMath.h"
#include "engine/core/Log.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/EntityFactory.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

GameWorld::GameWorld(WorldServices services, WorldMode mode) : services_(services), mode_(mode) {}

GameWorld::~GameWorld() = default;

void GameWorld::setKeysPerSlot(bool on) {
    if (keysPerSlot_ == on) return;
    keysPerSlot_ = on;
    player_.setKeysPerSlot(on);
    if (time_ > 0.0f || level_.startPowers.empty()) return;
    // Loaded but not started: the start powers as the new rule grants them (two powers on
    // the same old button are both kept with keys per slot). Untouched when it is the same.
    Player probe;
    probe.setKeysPerSlot(on);
    for (const auto& pid : level_.startPowers)
        if (const PowerDefinition* d = services_.content.power(pid)) probe.grantPower(*d);
    bool same = probe.powers().size() == player_.powers().size();
    for (size_t i = 0; same && i < probe.powers().size(); ++i) same = probe.powers()[i]->id() == player_.powers()[i]->id();
    if (same) return;
    player_.clearPowers();
    for (const auto& pid : level_.startPowers)
        if (const PowerDefinition* d = services_.content.power(pid)) player_.grantPower(*d);
}

bool GameWorld::load(const LevelData& level) {
    level_ = level;
    enemyBinding_ = services_.content.enemyBindingOf(level_);
    hazards_ = BossHazards{};  // its colliders go with the physics below
    physics_.clear();
    entities_.clear();
    entityById_.clear();
    enemies_.clear();
    pendingEnemies_.clear();
    projectiles_.clear();
    messages_.clear();
    stats_ = RunStats{};
    boss_ = nullptr;
    interactable_ = nullptr;
    lockedZone_.reset();
    bossZone_.reset();
    activeZone_ = nullptr;
    time_ = 0.0f;
    decoyTimer_ = 0.0f;
    decoysBroken_ = 0;
    frozenPlatforms_.clear();  // their colliders went with physics_.clear()

    particles_.init(&services_.res);
    particles_.loadPresets(services_.content.effects());
    particles_.setQuality(services_.particleQuality);

    camera_.setBounds(level_.bounds);
    camera_.setZoom(level_.cameraZoom);
    camera_.setShakeEnabled(services_.screenShake);
    timer_.configure(level_.timerMode, level_.timeLimit, level_.onExpire);
    events_.load(level_.events);

    PlayerConfig cfg = PlayerConfig::fromJson(services_.content.playerConfig());
    player_.configure(cfg, services_.res.animation("animations/nova.json"));
    player_.clearPowers();
    player_.setKeysPerSlot(keysPerSlot_);
    for (const auto& pid : level_.startPowers)
        if (const PowerDefinition* d = services_.content.power(pid)) player_.grantPower(*d);
    player_.setPowerSlots(level_.powerSlots());
    powerOffer_.reset();

    buildStatic();
    createEntities();
    for (auto& e : entities_) e->init(*this);

    // Player start
    respawn_ = {level_.bounds.x + 64, level_.bounds.y + 64};
    for (const auto& o : level_.objects)
        if (o.type == "player_start") {
            respawn_ = o.rect.bottomCenter();
            respawnFaceLeft_ = o.prop<bool>("faceLeft", false);
        }
    // Totals for records
    for (const auto& o : level_.objects) {
        if (o.type == "shard") stats_.totalShards++;
        if (o.type == "relic") stats_.totalRelics++;
        if (o.type == "secret_area") stats_.totalSecrets++;
        if (o.type == "enemy") stats_.totalEnemies++;
    }

    player_.spawn(*this, respawn_, respawnFaceLeft_, false);
    camera_.reset(respawn_ - Vec2{0, 20});
    flash_.a = 0;

    if (mode_ == WorldMode::Editor) {
        player_.setHidden(true);
        state_ = WorldState::Playing;
        return true;
    }
    setMusic(level_.music);
    if (!level_.ambience.empty()) {
        std::string amb = services_.res.soundPath(level_.ambience);
        if (!amb.empty()) services_.audio.playAmbience(amb);
    }
    if (mode_ == WorldMode::Play && !level_.intro.empty()) {
        state_ = WorldState::Intro;
        player_.setHidden(true);
        player_.controlEnabled = false;
        cinematic_.start(level_.intro, *this);
    } else {
        beginPlay();
    }
    return true;
}

void GameWorld::buildStatic() {
    // Surface grip per terrain material, read from the tileset JSON ("friction", default 1).
    std::map<std::string, float> friction;
    auto frictionOf = [&](const std::string& material) {
        auto it = friction.find(material);
        if (it != friction.end()) return it->second;
        float f = 1.0f;
        if (auto j = resources().json("textures/tiles/" + material + ".json")) f = std::clamp(jget<float>(*j, "friction", 1.0f), 0.05f, 1.0f);
        friction[material] = f;
        return f;
    };
    for (const auto& o : level_.objects) {
        if (o.type == "solid" && o.prop<bool>("collision", true)) {
            // A wall that reaches the top of the level goes on up to the ceiling: the space
            // between the top of the level and the ceiling is never a way over it.
            Rect r = o.rect;
            if (r.y <= level_.bounds.y + 0.5f) {
                r.h += r.y - (level_.bounds.y - kCeilingMargin);
                r.y = level_.bounds.y - kCeilingMargin;
            }
            uint32_t id = physics_.add(r, ColliderKind::Solid);
            float f = frictionOf(o.propStr("material", "ruin_stone"));
            if (f < 1.0f)
                if (Collider* c = physics_.get(id)) c->friction = f;
        } else if (o.type == "oneway") {
            physics_.add({o.rect.x, o.rect.y, o.rect.w, 4.0f}, ColliderKind::OneWay);
        }
    }
    // Invisible level borders (left / right / top) so nobody leaves the level: no route
    // over the top of the level.
    const Rect& b = level_.bounds;
    physics_.add({b.x - 32, b.y - 400, 32, b.h + 800}, ColliderKind::Solid);
    physics_.add({b.right(), b.y - 400, 32, b.h + 800}, ColliderKind::Solid);
    physics_.add({b.x - 32, b.y - kCeilingMargin - 64, b.w + 64, 64}, ColliderKind::Solid);
}

void GameWorld::createEntities() {
    for (const auto& o : level_.objects) {
        if (o.type == "solid" || o.type == "oneway" || o.type == "player_start") continue;
        auto e = EntityFactory::create(o);
        if (!e) {
            Log::warn("Unknown object type '{}' ({})", o.type, o.id);
            continue;
        }
        if (const LevelLayer* l = level_.findLayer(o.layer)) {
            e->layerOrder = l->order * 1000 + o.z;
            e->foreground = l->foreground;
        }
        entityById_[e->id()] = e.get();
        entities_.push_back(std::move(e));
    }
    std::stable_sort(entities_.begin(), entities_.end(),
                     [](const auto& a, const auto& b) { return a->layerOrder < b->layerOrder; });
}

LevelEntity* GameWorld::entity(const std::string& id) {
    auto it = entityById_.find(id);
    return it == entityById_.end() ? nullptr : it->second;
}

void GameWorld::startAt(Vec2 feet) {
    if (cinematic_.active()) cinematic_.skip(*this);
    respawn_ = feet;
    player_.spawn(*this, feet, false, false);
    camera_.reset(feet - Vec2{0, 20});
    if (state_ != WorldState::Playing) beginPlay();
    player_.controlEnabled = true;
}

void GameWorld::startAt(Vec2 feet, bool faceLeft) {
    startAt(feet);
    respawnFaceLeft_ = faceLeft;
    player_.facing = faceLeft ? -1.0f : 1.0f;
}

void GameWorld::beginPlay() {
    state_ = WorldState::Playing;
    player_.controlEnabled = true;
    if (player_.state() == PlayerState::Hidden) player_.spawn(*this, respawn_, respawnFaceLeft_, true);
    camera_.releaseScript();
    // 2.47.1: the clock starts when Nova can move. An intro that shows her appearing (watched to
    // the end) has played her materialisation already; a skipped one plays it now: the clock
    // waits for its end (fixedUpdate), so skipping the intro never costs time, watching it never
    // gives any.
    if (mode_ == WorldMode::Play) {
        if (player_.state() == PlayerState::Respawn) timerPending_ = true;
        else timer_.start();
    }
    emit("level", "levelStart");
}

// ================================================================== update

Vec2 GameWorld::enemyAim() const { return decoyTimer_ > 0.0f ? decoyPos_ : player_.center(); }

void GameWorld::fixedUpdate(const PlayerInput& in, float dt) {
    // Waiting for the player to choose which power to keep: nothing moves (see stepWorld).
    if (powerOffer_ && mode_ != WorldMode::Editor) return;
    time_ += dt;
    if (flashTimer_ > 0.0f) flashTimer_ = std::max(0.0f, flashTimer_ - dt);
    if (splitFlash_ > 0.0f) splitFlash_ -= dt;
    if (mode_ == WorldMode::Editor) return;

    if (hitStop_ > 0.0f) {
        hitStop_ -= dt;
        camera_.fixedUpdate(dt);
        return;
    }
    if (decoyTimer_ > 0.0f) decoyTimer_ = std::max(0.0f, decoyTimer_ - dt);

    cinematic_.fixedUpdate(*this, dt);  // also animates the letterbox / fades after the end
    if (state_ == WorldState::Intro && !cinematic_.active()) beginPlay();

    PlayerInput effective = (state_ == WorldState::Playing && !cinematic_.active()) ? in : PlayerInput{};

    if (state_ == WorldState::Playing) {
        if (timerPending_ && player_.state() != PlayerState::Respawn) {
            timerPending_ = false;
            timer_.start();
        }
        timer_.fixedUpdate(dt);
        if (timer_.expired() && !expiredHandled_) {
            expiredHandled_ = true;
            switch (timer_.onExpire()) {
                case ExpireAction::GameOver: failLevel("Temps écoulé !"); break;
                case ExpireAction::RestartCheckpoint:
                    timer_.addTime(60.0f);
                    expiredHandled_ = false;
                    hint("Temps écoulé : retour au dernier point de sauvegarde (+60 s)", 3.0f);
                    player_.kill(*this);
                    break;
                case ExpireAction::Continue: hint("Temps limite dépassé", 3.0f); break;
            }
        }
    }
    events_.fixedUpdate(*this, dt, timer_.elapsed());

    for (size_t i = 0; i < entities_.size(); ++i) entities_[i]->fixedUpdate(*this, dt);
    carryBodies();

    player_.onLadder = false;
    player_.inWater = false;
    player_.speedMultiplier = 1.0f;
    player_.updraft = 0.0f;
    updatePlayerOverlaps(dt);
    player_.fixedUpdate(*this, effective, dt);
    // 2.48: the last solid ground under Nova (practice runs of hardcore levels come back there).
    if ((practiceRespawn_ || invincible()) && player_.grounded() && player_.state() != PlayerState::Dead &&
        player_.state() != PlayerState::Respawn && player_.vel.y >= 0.0f) {
        safeFeet_ = player_.feet();
        hasSafeFeet_ = true;
    }

    // Time slow power: enemies (and, in updateProjectiles, their shots) run slower.
    updateEnemies(timeSlow_ > 0.0f ? dt * timeSlowFactor_ : dt);
    if (!hazards_.empty()) hazards_.fixedUpdate(*this, timeSlow_ > 0.0f ? dt * timeSlowFactor_ : dt);
    updateProjectiles(dt);
    if (timeSlow_ > 0.0f) timeSlow_ = std::max(0.0f, timeSlow_ - dt);
    if (decoyTimer_ > 0.0f) updateDecoy();
    if (!frozenPlatforms_.empty() || !enemies_.empty()) syncFrozenPlatforms();

    entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
                                   [&](const auto& e) {
                                       if (!e->removed) return false;
                                       entityById_.erase(e->id());
                                       if (interactable_ == e.get()) interactable_ = nullptr;
                                       return true;
                                   }),
                    entities_.end());

    checkKillPlane();
    updateCamera(dt);

    if (state_ == WorldState::Outro && !cinematic_.active()) state_ = WorldState::Completed;
}

void GameWorld::update(float dt) {
    particles_.update(dt);
    player_.update(*this, dt);
    for (auto& e : enemies_)
        if (!e->frozen()) e->update(*this, dt);  // a frozen enemy keeps its pose
    for (auto& e : entities_) e->update(*this, dt);
    for (auto& m : messages_) m.age += dt;
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(), [](const auto& m) { return m.age >= m.duration; }),
                    messages_.end());
}

void GameWorld::carryBodies() {
    for (auto& e : entities_) {
        auto* mp = dynamic_cast<MovingPlatformEntity*>(e.get());
        if (!mp) continue;
        Vec2 d = mp->lastDelta();
        if (d.lengthSq() < 1e-8f) continue;
        MoveOptions opt = player_.moveOptions();
        if (player_.groundCollider() == mp->colliderId() && player_.alive()) {
            physics_.move(player_.box, {d.x, 0.0f}, opt);
            player_.box.y += d.y;  // vertical carry is exact (platform pushes / pulls)
            player_.prevBox = player_.prevBox.translated(d);
        } else if (d.y < 0.0f && player_.alive() && player_.box.intersects(mp->box)) {
            // Rising platform pushing the player from below.
            player_.box.y = mp->box.y - player_.box.h;
        }
        for (auto& en : enemies_) {
            Rect feet{en->box.x, en->box.bottom() - 1, en->box.w, 3};
            if (feet.intersects(Rect{mp->box.x, mp->box.y - 1, mp->box.w, 3})) en->box = en->box.translated(d);
        }
    }
}

void GameWorld::updatePlayerOverlaps(float dt) {
    interactable_ = nullptr;
    float best = 1e9f;
    for (size_t i = 0; i < entities_.size(); ++i) {
        LevelEntity* e = entities_[i].get();
        if (!e->active || e->removed) continue;
        if (e->interactable() && player_.alive() && e->box.expanded(8).intersects(player_.box)) {
            float d = distance(e->box.center(), player_.center());
            if (d < best) {
                best = d;
                interactable_ = e;
            }
        }
        if (!e->wantsPlayerOverlap()) continue;
        bool inside = player_.alive() && !player_.respawning() && e->overlapBox().intersects(player_.box);
        if (inside && !e->playerInside) {
            e->playerInside = true;
            e->onPlayerEnter(*this);
        } else if (!inside && e->playerInside) {
            e->playerInside = false;
            e->onPlayerExit(*this);
        }
        if (inside) e->onPlayerStay(*this, dt);
    }
}

void GameWorld::updateEnemies(float dt) {
    for (size_t i = 0; i < enemies_.size(); ++i) enemies_[i]->fixedUpdate(*this, dt);
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                                  [&](const auto& e) {
                                      if (!e->removable()) return false;
                                      if (boss_ == e.get()) boss_ = nullptr;
                                      hazards_.removeOwner(*this, e->uid());
                                      return true;
                                  }),
                   enemies_.end());
    for (auto& e : pendingEnemies_) enemies_.push_back(std::move(e));
    pendingEnemies_.clear();
}

void GameWorld::updateProjectiles(float frameDt) {
    for (auto& p : pendingProjectiles_) projectiles_.push_back(std::move(p));
    pendingProjectiles_.clear();
    for (auto& p : projectiles_) {
        if (!p.alive) continue;
        // Enemy shots follow the time slow power; the player's never do.
        const float dt = (!p.fromPlayer && timeSlow_ > 0.0f) ? frameDt * timeSlowFactor_ : frameDt;
        p.prevPos = p.pos;
        p.age += dt;
        p.life -= dt;
        if (p.life <= 0.0f) {
            p.alive = false;
            if (p.explosionRadius > 0.0f) explodeProjectile(p, p.pos);
            else if (p.clip != "shockwave") particles_.emit(p.hitEffect, p.pos, 0.6f);
            if (p.cloudRadius > 0.0f && !p.fromPlayer) cloudFromShot(p, p.pos);
            continue;
        }
        p.vel.y += p.gravity * dt;
        if (p.fromPlayer && !hazards_.empty()) hazards_.bendShot(p, dt);  // gravity wells (2.19)
        if (p.homing > 0.0f && p.age < p.homingTime && !p.fromPlayer && player_.alive()) {
            // Seeker: turns toward the player at a bounded rate (deterministic math).
            Vec2 to = player_.center() - p.pos;
            float want = dmath::atan2(to.y, to.x);
            float cur = dmath::atan2(p.vel.y, p.vel.x);
            float diff = want - cur;
            while (diff > kPi) diff -= 2.0f * kPi;
            while (diff < -kPi) diff += 2.0f * kPi;
            float step = std::clamp(diff, -p.homing * dt, p.homing * dt);
            float speed = p.vel.length();
            p.vel = Vec2{dmath::cos(cur + step), dmath::sin(cur + step)} * speed;
        }
        Vec2 np = p.pos + p.vel * dt;
        if (p.clip == "shockwave") {
            // Ground shockwave: dies at walls or ledges.
            Rect ahead = Rect::fromCenter(np, {4, 4});
            MoveOptions opt;
            opt.isEnemy = true;
            Rect below{np.x - 1, np.y + 4, 2, 6};
            if (physics_.overlapsSolid(ahead, opt) || !physics_.firstSolid(below, opt)) {
                bool hasGround = false;
                physics_.forEachIn(below, [&](const Collider& c) {
                    if (c.enabled && c.box.intersects(below)) hasGround = true;
                });
                if (physics_.overlapsSolid(ahead, opt) || !hasGround) {
                    p.alive = false;
                    continue;
                }
            }
            if (static_cast<int>(p.age * 30) % 2 == 0) particles_.emit("shockwave_dust", np + Vec2{0, 4}, 0.6f);
        } else if (auto t = physics_.raycast(p.pos, np, false, ColliderTag::EnemyOnly)) {
            Vec2 hit = p.pos + (np - p.pos) * *t;
            if (p.bounces > 0) {
                // Bounce: the axis whose move alone hits the scenery is reflected.
                --p.bounces;
                bool hx = physics_.raycast(p.pos, {np.x, p.pos.y}, false, ColliderTag::EnemyOnly).has_value();
                bool hy = physics_.raycast(p.pos, {p.pos.x, np.y}, false, ColliderTag::EnemyOnly).has_value();
                if (hy || !hx) p.vel.y = -p.vel.y * 0.55f;
                if (hx) p.vel.x = -p.vel.x * 0.6f;
                p.vel.x *= 0.85f;
                p.pos = hit - (np - p.pos).normalized() * 1.5f;
                playSound("sounds/bomb_drop.wav", p.pos, 0.4f, 1.3f);
                continue;
            }
            p.alive = false;
            if (p.explosionRadius > 0.0f) {
                explodeProjectile(p, hit);
                continue;
            }
            particles_.emit(p.hitEffect, hit, 0.8f);
            if (!p.hitSound.empty()) playSound(p.hitSound, hit, 0.6f);
            if (p.cloudRadius > 0.0f && !p.fromPlayer) cloudFromShot(p, hit);
            if (p.fromPlayer && p.type == DamageType::Wave) powerHit("wave", Rect::fromCenter(hit, {12, 12}));
            if (p.fromPlayer && p.type == DamageType::ChargeShot) powerHit("charge_shot", Rect::fromCenter(hit, {16, 16}));
            continue;
        }
        p.pos = np;
        Rect pr = Rect::fromCenter(p.pos, {p.radius * 2, p.radius * 2});
        if (p.fromPlayer) {
            if (p.type == DamageType::Wave) powerHit("wave", pr);
            if (p.type == DamageType::ChargeShot) powerHit("charge_shot", pr);
            for (auto& e : enemies_) {
                if (!e->alive() || e->dormant()) continue;
                if (std::find(p.hitTargets.begin(), p.hitTargets.end(), e.get()) != p.hitTargets.end()) continue;
                if (!Collision::circleIntersectsRect(p.pos, p.radius, e->box)) continue;
                if (p.explosionRadius > 0.0f) {  // plasma bomb: bursts on the first enemy
                    p.alive = false;
                    explodeProjectile(p, p.pos);
                    break;
                }
                DamageInfo d;
                d.amount = p.damage;
                d.type = p.type;
                d.source = p.pos - p.vel.normalized() * 8.0f;
                d.knockback = p.knockback;
                d.fromPlayer = true;
                e->takeDamage(*this, d);
                p.hitTargets.push_back(e.get());
                particles_.emit(p.hitEffect, p.pos, 0.8f);
                if (!p.hitSound.empty()) playSound(p.hitSound, p.pos, 0.7f);
                hitStop(0.03f);
                if (--p.pierce < 0) {
                    p.alive = false;
                    break;
                }
            }
        } else if (player_.alive() && Collision::circleIntersectsRect(p.pos, p.radius, player_.box)) {
            if (player_.shieldActive() && p.clip != "shockwave") {
                // Reflect back at the enemies.
                p.vel = -p.vel * 1.25f;
                p.fromPlayer = true;
                p.reflected = true;
                p.type = DamageType::Wave;
                p.damage *= 2.0f;
                p.life = 2.0f;
                playSound("sounds/shield_block.wav", p.pos, 0.9f);
                particles_.emit("shield_hit", p.pos, 1.0f);
                continue;
            }
            if (p.explosionRadius > 0.0f) {
                p.alive = false;
                explodeProjectile(p, p.pos);
                continue;
            }
            DamageInfo d;
            d.amount = p.damage;
            d.type = p.type;
            d.source = p.pos - p.vel.normalized() * 8.0f;
            d.knockback = p.knockback;
            d.slow = p.slow;
            d.slowDuration = p.slowDuration;
            damagePlayer(d);
            if (p.clip != "shockwave") {
                p.alive = false;
                particles_.emit(p.hitEffect, p.pos, 0.8f);
                if (p.cloudRadius > 0.0f) cloudFromShot(p, p.pos);
            }
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const auto& p) { return !p.alive; }),
                       projectiles_.end());
}

void GameWorld::cloudFromShot(const Projectile& p, Vec2 at) {
    // A puff of burning smoke where it burst; on the ground when it burst just above it.
    const float r = p.cloudRadius;
    const float h = std::max(24.0f, r * 1.1f);
    BossHazards::Cloud c;
    c.owner = p.cloudOwner;
    c.area = Rect::fromCenter(at, {r * 2.0f, h});
    Vec2 from{at.x, at.y - 4.0f}, to{at.x, at.y + h};
    if (auto t = physics_.raycast(from, to, true)) {
        float g = from.y + (to.y - from.y) * *t;
        c.area = {at.x - r, g - h, r * 2.0f, h};
        c.ground = true;
    }
    c.originX = at.x;
    c.spread = 0.25f;
    c.time = p.cloudTime > 0.0f ? p.cloudTime : 3.0f;
    c.damage = p.cloudDamage;
    c.color = p.cloudColor;
    hazards_.addCloud(c);
    particles_.emit("fire_burst", at, 1.4f);
    playSound("sounds/lava_bubble.wav", at, 0.6f, 0.8f);
}

void GameWorld::explodeProjectile(const Projectile& p, Vec2 at) {
    float r = p.explosionRadius;
    particles_.emit("explosion", at, std::max(0.8f, r / 32.0f));
    playSound("sounds/bomb_explode.wav", at, 0.9f);
    shake(0.3f);
    rumble(0.4f, 0.6f, 160);
    DamageInfo d;
    d.amount = p.damage;
    d.type = DamageType::Explosion;
    d.source = at;
    d.knockback = std::max(p.knockback, 220.0f);
    d.slow = p.slow;
    d.slowDuration = p.slowDuration;
    if (p.fromPlayer) {
        d.fromPlayer = true;
        damageEnemies(Rect::fromCenter(at, {r * 2, r * 2}), d, nullptr);
        powerHit("impulse", Rect::fromCenter(at, {r * 2, r * 2}));  // breaks cracked rock
    } else if (player_.alive() && Collision::circleIntersectsRect(at, r, player_.box)) {
        damagePlayer(d);
    }
}

void GameWorld::checkKillPlane() {
    if (!player_.alive()) return;
    if (player_.box.y > level_.effectiveKillY()) {
        player_.kill(*this);
        // Falling death: no point waiting at the bottom of the screen.
        hitStop_ = 0.0f;
    }
}

void GameWorld::updateCamera(float dt) {
    activeZone_ = nullptr;
    if (const CameraZone* bz = bossCameraZone()) {
        activeZone_ = bz;  // a boss fight: the whole arena in view
    } else if (lockedZone_) {
        activeZone_ = &*lockedZone_;
    } else {
        for (auto& e : entities_) {
            auto* cz = dynamic_cast<CameraZoneEntity*>(e.get());
            if (cz && cz->active && cz->box.contains(player_.center())) {
                activeZone_ = &cz->zone();
                break;
            }
        }
    }
    camera_.setZone(activeZone_);
    // A giant arena (2.33: Singularis): it cannot be seen whole even at the widest, so the
    // camera follows Nova at its widest, leaning toward the boss (Nova always well inside the
    // view; the boss arrow shows it when it is out of sight). Other arenas fit: no lean.
    Vec2 lean;
    if (const CameraZone* bz = bossCameraZone(); bz && bz == activeZone_ && boss_ && boss_->alive() &&
                                                  camera_.fitZoom(bz->area) < Camera::kMinFitZoom - 0.001f) {
        Vec2 d = boss_->center() - player_.center();
        const float hw = Camera::kViewWidth / Camera::kMinFitZoom * 0.5f, hh = Camera::kViewHeight / Camera::kMinFitZoom * 0.5f;
        lean = {std::clamp(d.x * 0.4f, -hw * 0.45f, hw * 0.45f), std::clamp(d.y * 0.25f, -hh * 0.3f, hh * 0.3f)};
    }
    camera_.setLean(lean);
    camera_.setTarget(player_.center(), player_.vel, player_.facing, player_.grounded());
    camera_.fixedUpdate(dt);
}

// ================================================================ spawning

Enemy* GameWorld::spawnEnemy(const EnemyDefinition& def, Vec2 feet, bool faceLeft, const std::string& objectId,
                             float patrol) {
    if (def.assetRoot.empty() == false) services_.res.pushMount(def.assetRoot);
    auto anim = services_.res.animation(def.animation);
    if (!def.assetRoot.empty()) services_.res.popMount();
    // Per-world counter: a run must not depend on what was played before (replays).
    auto e = std::make_unique<Enemy>(def, anim, feet, faceLeft, objectId, patrol, nextEnemyUid_++);
    Enemy* raw = e.get();
    pendingEnemies_.push_back(std::move(e));
    return raw;
}

const EnemyDefinition* GameWorld::enemyDefinition(const std::string& id) const {
    if (const EnemyDefinition* d = services_.content.enemyFor(id, enemyBinding_)) return d;
    // A revision this game does not have (never for a level it ranks: its fingerprint needs it).
    if (auto b = enemyBinding_.find(id); b != enemyBinding_.end())
        Log::warn("Enemy '{}': revision {} unknown, the game's own definition is used", id, b->second);
    return services_.content.enemy(id);
}

Enemy* GameWorld::spawnEnemy(const std::string& defId, Vec2 feet, bool faceLeft, const std::string& objectId,
                             float patrol) {
    const EnemyDefinition* d = enemyDefinition(defId);
    if (!d) {
        Log::warn("Unknown enemy '{}'", defId);
        return nullptr;
    }
    return spawnEnemy(*d, feet, faceLeft, objectId, patrol);
}

void GameWorld::spawnProjectile(const Projectile& p) { pendingProjectiles_.push_back(p); }

Enemy* GameWorld::findEnemy(uint32_t uid) const {
    if (uid == 0) return nullptr;
    for (const auto& e : enemies_)
        if (e->uid() == uid) return e.get();
    for (const auto& e : pendingEnemies_)
        if (e->uid() == uid) return e.get();
    return nullptr;
}

void GameWorld::spawnDrop(const std::string& type, Vec2 pos) {
    LevelObject o;
    o.id = "drop_" + std::to_string(++dropCounter_);
    o.type = type;
    o.layer = "gameplay";
    o.rect = Rect::fromCenter(pos, {12, 12});
    auto e = EntityFactory::create(o);
    if (!e) return;
    if (auto* c = dynamic_cast<CollectibleEntity*>(e.get())) c->makeDrop(rng_.range(-60.0f, 60.0f), -160.0f);
    e->layerOrder = 20000;
    e->init(*this);
    entityById_[e->id()] = e.get();
    entities_.push_back(std::move(e));
}

// =================================================================== combat

int GameWorld::damageEnemies(const Rect& area, const DamageInfo& dmg, const void* /*source*/) {
    int hits = 0;
    for (auto& e : enemies_) {
        if (!e->alive() || e->dormant() || !e->box.intersects(area)) continue;
        if (e->takeDamage(*this, dmg)) ++hits;
    }
    return hits;
}

bool GameWorld::powerHit(const std::string& power, const Rect& area) {
    bool any = false;
    for (size_t i = 0; i < entities_.size(); ++i)
        if (entities_[i]->active && entities_[i]->onPowerHit(*this, power, area)) any = true;
    return any;
}

bool GameWorld::damagePlayer(const DamageInfo& dmg) { return player_.hurt(*this, dmg); }

void GameWorld::onEnemyKilled(Enemy& e) {
    hazards_.removeOwner(*this, e.uid());  // its cage, frost, lava and pulls go with it
    if (e.countsForStats && !e.vanishing) {
        stats_.kills++;
        stats_.familyKills[familyId(e.def().family)]++;  // achievements only (2.24)
        stats_.scoreBonus += e.def().scoreValue;
    }
    if (!e.vanishing)
        for (const auto& d : e.def().drops)
            if (rng_.chance(d.chance)) spawnDrop(d.type, e.center());
    // Minions vanish with their summoner (2.17: those just summoned too, and without
    // leaving rewards or split children behind: a beaten boss takes all its creatures).
    auto vanish = [&](Enemy& o) {
        if (o.summonedBy != e.uid() || !o.alive()) return;
        o.vanishing = true;
        o.countsForStats = false;
        o.kill(*this);
    };
    for (size_t i = 0; i < enemies_.size(); ++i) vanish(*enemies_[i]);
    for (size_t i = 0; i < pendingEnemies_.size(); ++i) vanish(*pendingEnemies_[i]);
    // Splitting enemies (slimes) leave smaller ones behind (a minion's belong to its summoner).
    if (!e.vanishing && e.def().deathSpawnCount > 0 && !e.def().deathSpawnId.empty() && e.def().deathSpawnId != e.def().id) {
        particles_.emit("split_splash", e.center(), 1.0f);
        playSound("sounds/slime_split.wav", e.center(), 0.9f);
        for (int i = 0; i < e.def().deathSpawnCount; ++i) {
            float side = (i % 2 == 0) ? -1.0f : 1.0f;
            Vec2 feet = e.box.bottomCenter() + Vec2{side * (6.0f + 6.0f * static_cast<float>(i / 2)), 0.0f};
            if (Enemy* child = spawnEnemy(e.def().deathSpawnId, feet, side < 0)) {
                child->vel = {side * (70.0f + 25.0f * static_cast<float>(i / 2)), -170.0f};
                child->countsForStats = e.countsForStats;
                child->summonedBy = e.summonedBy;
                child->alert(*this);
            }
        }
    }
    if (!e.objectId().empty()) emit(e.objectId(), "killed");
    if (boss_ == &e) {
        boss_ = nullptr;
        WorldMessage m;
        m.style = WorldMessage::Style::Boss;
        m.text = e.def().bossTitle.empty() ? e.def().name : e.def().bossTitle;
        m.subtitle = "VAINCU";
        m.duration = 3.0f;
        message(m);
        flash({255, 255, 255, 255}, 0.4f);
        restoreLevelMusic();
        lockedZone_.reset();
        bossZone_.reset();
        if (!e.objectId().empty()) emit(e.objectId(), "bossDefeated");
    }
}

// ================================================================ feedback

void GameWorld::playSound(const std::string& rel, Vec2 worldPos, float volume, float pitch) {
    if (mode_ == WorldMode::Editor || silent) return;
    Vec2 c = camera_.center();
    float dx = worldPos.x - c.x;
    float dist = distance(worldPos, c);
    float att = std::clamp(1.0f - (dist - 260.0f) / 500.0f, 0.15f, 1.0f);
    float pan = std::clamp(dx / 260.0f, -1.0f, 1.0f) * 0.55f;
    services_.res.playSound(rel, volume * att, pitch, pan);
}

void GameWorld::playSound(const std::string& rel, float volume, float pitch) {
    if (mode_ == WorldMode::Editor || silent) return;
    services_.res.playSound(rel, volume, pitch, 0.0f);
}

void GameWorld::sustainSound(const std::string& key, const std::string& rel, float volume) {
    if (mode_ == WorldMode::Editor || silent) return;
    services_.res.sustainLoop(key, rel, volume);
}

void GameWorld::shake(float trauma) { camera_.addTrauma(trauma); }

void GameWorld::flash(Color c, float duration) {
    flash_ = c;
    flashTimer_ = flashDuration_ = duration;
}

Color GameWorld::flashColor() const {
    if (flashTimer_ <= 0.0f || flashDuration_ <= 0.0f) return Color::transparent();
    float k = flashTimer_ / flashDuration_;
    return flash_.withAlphaF(k * 0.55f);
}

void GameWorld::rumble(float low, float high, int ms) {
    if (services_.input && mode_ != WorldMode::Editor) services_.input->rumble(low, high, ms);
}

void GameWorld::message(WorldMessage msg) {
    // Replace a message of the same style (keeps the screen readable).
    for (auto& m : messages_)
        if (m.style == msg.style && msg.style != WorldMessage::Style::Hint) {
            m = msg;
            return;
        }
    messages_.push_back(std::move(msg));
}

void GameWorld::hint(const std::string& text, float duration) {
    for (auto& m : messages_)
        if (m.style == WorldMessage::Style::Hint && m.text == text) {
            m.age = std::min(m.age, 0.3f);
            return;
        }
    WorldMessage m;
    m.style = WorldMessage::Style::Hint;
    m.text = text;
    m.duration = duration;
    messages_.push_back(m);
}

// ================================================================== logic

void GameWorld::emit(const std::string& source, const std::string& trigger, const Json& params) {
    if (mode_ == WorldMode::Editor) return;
    if (eventObserver) eventObserver(source, trigger);
    events_.signal(*this, source, trigger, params);
}

void GameWorld::setCheckpoint(const std::string& id, Vec2 feet) {
    if (checkpointId_ == id) return;
    checkpointId_ = id;
    respawn_ = feet;
    respawnFaceLeft_ = false;
    int idx = timer_.split();
    if (idx >= 0 && idx < static_cast<int>(referenceSplits_.size()) && level_.timerMode != TimerMode::Unlimited) {
        lastSplitDelta_ = timer_.elapsed() - referenceSplits_[idx];
        splitFlash_ = 3.0f;
    }
    emit(id, "checkpoint");
}

void GameWorld::onPlayerDeathAnimationDone() {
    if (mode_ == WorldMode::Arena) {
        hazards_.clear(*this);
        if (onPlayerDied) onPlayerDied();
        player_.spawn(*this, respawn_, respawnFaceLeft_, true);
        return;
    }
    if (practiceRespawn_) {  // 2.48: a practice run of a hardcore level goes on where Nova fell
        practiceRespawnNow();
        return;
    }
    if (training_) {
        // A boss training ends at the first fall (its end screen comes): the player does not
        // come back, never into an arena that would wake its boss again behind that screen.
        player_.setHidden(true);
        return;
    }
    if (level_.timerMode == TimerMode::Challenge && stats_.deaths > level_.challengeMaxDeaths) {
        failLevel("Défi échoué : trop de chutes");
        player_.setHidden(true);
        return;
    }
    if (level_.lives > 0 && stats_.deaths >= level_.lives) {
        failLevel(level_.lives == 1 ? "Plus de vie" : "Plus de vies");
        player_.setHidden(true);
        return;
    }
    respawnPlayer();
}

void GameWorld::offerPower(const std::string& tabletId, const std::string& powerId, int conflictSlot) {
    if (powerOffer_ || mode_ == WorldMode::Editor) return;
    powerOffer_ = PowerOffer{tabletId, powerId, conflictSlot};
    if (autoPowerChoice) {
        resolvePowerOffer(autoPowerChoice);
        return;
    }
    player_.vel = {0.0f, std::min(player_.vel.y, 0.0f)};
    playSound("sounds/power_unlock.wav", 0.6f, 0.8f);
}

bool GameWorld::resolvePowerOffer(uint8_t choice) {
    if (!powerOffer_) return false;
    PowerOffer offer = *powerOffer_;
    powerOffer_.reset();
    const PowerDefinition* def = services_.content.power(offer.powerId);
    auto* tablet = dynamic_cast<PowerUnlockEntity*>(entity(offer.tabletId));
    int slot = static_cast<int>(choice) - 1;
    if (offer.conflictSlot >= 0) slot = offer.conflictSlot;  // a power on the same button must go
    bool leave = choice == 0 || choice == InputFrame::kChoiceLeave || !def || !tablet ||
                 slot < 0 || slot >= static_cast<int>(player_.powers().size());
    if (leave) {
        hint("Pouvoir laissé ici : vous pourrez revenir le prendre.", 2.5f);
        return true;
    }
    std::string dropped = player_.replacePower(slot, *def);
    player_.addEnergy(100.0f);
    tablet->exchange(*this, dropped);
    PowerUnlockEntity::celebrate(*this, *def, tablet->box.center(), {}, dropped);
    emit(offer.tabletId, "collected");
    emit(offer.powerId, "unlocked");
    return true;
}

void GameWorld::practiceRespawnNow() {
    // The level stays as it is (enemies, bosses and their wounds, doors): only the shots in the
    // air and the hazards about to hit go, so that Nova does not fall again at once.
    hazards_.clear(*this);
    projectiles_.clear();
    pendingProjectiles_.clear();
    const Vec2 at = hasSafeFeet_ ? safeFeet_ : respawn_;
    player_.spawn(*this, at, player_.facing < 0.0f, true);
    player_.setInvulnerable(2.5f);  // after the materialisation (0.75 s): blinking, no damage
    camera_.reset(at - Vec2{0, 20});
    playSound("sounds/respawn.wav", 0.8f);
    hint("Entraînement : réapparition sur place (hors classements)", 2.5f);
}

bool GameWorld::rescueInvincible() {
    if (!invincible()) return false;
    const Vec2 at = hasSafeFeet_ ? safeFeet_ : respawn_;
    player_.box.x = at.x - player_.box.w * 0.5f;
    player_.box.y = at.y - player_.box.h;
    player_.prevBox = player_.box;
    player_.vel = {0.0f, 0.0f};
    player_.setInvulnerable(1.0f);
    particles_.emit("shield_up", player_.center(), 0.8f);
    return true;
}

void GameWorld::respawnPlayer() {
    hazards_.clear(*this);
    projectiles_.clear();
    pendingProjectiles_.clear();
    decoyTimer_ = 0.0f;
    player_.spawn(*this, respawn_, respawnFaceLeft_, true);
    // Keep unlocked powers; refill as a fresh start.
    camera_.reset(respawn_ - Vec2{0, 20});
    lockedZone_.reset();
    bossZone_.reset();
    for (size_t i = 0; i < entities_.size(); ++i) entities_[i]->onPlayerRespawn(*this);
    // A challenge without checkpoints: back to its start state (2.32, simulation 11).
    if (challenge_ && !challenge_->checkpoints) restartChallenge();
    playSound("sounds/respawn.wav", 0.8f);
    if (boss_ && boss_->alive()) {
        // The boss fight restarts from the checkpoint before the arena.
        restoreLevelMusic();
    }
}

void GameWorld::setChallenge(const ChallengeRules& rules) {
    challenge_ = rules;
    challenge_->powers = rules.sortedPowers();  // as in the canonical form
    applyChallengeStart(*this, *challenge_);
}

void GameWorld::restartChallenge() {
    applyChallengeStart(*this, *challenge_);
    // The power tablets as the level placed them: a tablet taken comes back, a tablet holding
    // a power left in exchange offers its own again (the powers carried were reset above).
    for (const LevelObject& o : level_.objects) {
        if (o.type != "power_unlock") continue;
        if (auto* t = dynamic_cast<PowerUnlockEntity*>(entity(o.id))) {
            t->restore(*this);
            continue;
        }
        auto e = EntityFactory::create(o);
        if (!e) continue;
        if (const LevelLayer* l = level_.findLayer(o.layer)) {
            e->layerOrder = l->order * 1000 + o.z;
            e->foreground = l->foreground;
        }
        e->init(*this);
        LevelEntity* raw = e.get();
        auto at = std::upper_bound(entities_.begin(), entities_.end(), raw->layerOrder,
                                   [](int order, const auto& x) { return order < x->layerOrder; });
        entities_.insert(at, std::move(e));
        entityById_[raw->id()] = raw;
    }
}

void GameWorld::reachExit(const LevelEntity& exitEntity) {
    if (state_ != WorldState::Playing || mode_ != WorldMode::Play) return;
    state_ = WorldState::Outro;
    timer_.stop();
    player_.controlEnabled = false;
    player_.vel.x = 0.0f;
    player_.startVictory();
    emit(exitEntity.id(), "exit");
    services_.audio.stopMusic(600);
    playSound("sounds/portal.wav", 1.0f);
    CinematicDef outro = level_.outro;
    if (outro.empty()) {
        outro.letterbox = true;
        CinematicStep z;
        z.type = "zoom";
        z.duration = 1.2f;
        z.wait = false;
        z.params = {{"zoom", 1.35}};
        CinematicStep c;
        c.type = "camera";
        c.duration = 1.2f;
        c.params = {{"player", true}};
        CinematicStep fl;
        fl.type = "flash";
        fl.duration = 0.5f;
        fl.params = {{"color", "#e6fffb"}};
        CinematicStep f;
        f.type = "fade";
        f.duration = 0.8f;
        f.params = {{"to", 1.0}};
        outro.steps = {z, c, fl, f};
    }
    cinematic_.start(outro, *this);
}

void GameWorld::failLevel(const std::string& reason) {
    if (state_ == WorldState::Failed || state_ == WorldState::Completed) return;
    state_ = WorldState::Failed;
    failReason_ = reason;
    timer_.stop();
    player_.controlEnabled = false;
}

std::optional<Rect> GameWorld::arenaOf(const Enemy& e) const {
    const CombatZoneEntity* inside = nullptr;
    for (const auto& ent : entities_)
        if (const auto* cz = dynamic_cast<const CombatZoneEntity*>(ent.get())) {
            if (cz->lists(e.objectId())) return cz->box;
            if (!inside && cz->box.contains(e.box.center())) inside = cz;
        }
    if (inside) return inside->box;
    return std::nullopt;
}

void GameWorld::startBoss(Enemy& boss) {
    boss_ = &boss;
    // The camera shows the whole arena (walls and floor included) during the fight.
    bossZone_.reset();
    if (auto arena = arenaOf(boss)) {
        CameraZone z;
        z.area = {arena->x - 24.0f, arena->y - 24.0f, arena->w + 48.0f, arena->h + 64.0f};
        z.clampToArea = true;
        z.fitArea = true;
        bossZone_ = z;
    }
    if (boss.dormant()) boss.wake(*this);
    boss.alert(*this);
    if (!level_.bossMusic.empty()) setMusic(level_.bossMusic);
    WorldMessage m;
    m.style = WorldMessage::Style::Boss;
    m.text = boss.def().bossTitle.empty() ? boss.def().name : boss.def().bossTitle;
    m.subtitle = boss.def().description;
    m.duration = 3.2f;
    message(m);
    shake(0.5f);
    if (!boss.objectId().empty()) emit(boss.objectId(), "bossStart");
}

void GameWorld::setMusic(const std::string& rel) {
    if (rel.empty() || mode_ == WorldMode::Editor) return;
    currentMusic_ = rel;
    std::string p = services_.res.soundPath(rel);
    if (!p.empty()) services_.audio.playMusic(p, 1500);
}

void GameWorld::restoreLevelMusic() { setMusic(level_.music); }

void GameWorld::lockCameraZone(const CameraZone* zone) {
    if (zone)
        lockedZone_ = *zone;
    else
        lockedZone_.reset();
}

void GameWorld::addSecretFound(const std::string& id, const std::string& text) {
    if (!stats_.secretIds.insert(id).second) return;
    stats_.secrets++;
    WorldMessage m;
    m.style = WorldMessage::Style::Secret;
    m.text = text.empty() ? "Secret découvert !" : text;
    m.duration = 2.5f;
    message(m);
    playSound("sounds/collect_relic.wav", 0.8f);
}

// ============================================================= 2.19 powers

Rect GameWorld::decoyBox() const { return Rect::fromCenter(decoyPos_, player_.config().size); }

void GameWorld::updateDecoy() {
    // The hologram absorbs one attack: the first enemy shot or blow that reaches it.
    Rect box = decoyBox();
    bool broken = false;
    for (auto& p : projectiles_) {
        if (!p.alive || p.fromPlayer || !Collision::circleIntersectsRect(p.pos, p.radius, box)) continue;
        p.alive = false;
        particles_.emit(p.hitEffect, p.pos, 0.8f);
        broken = true;
        break;
    }
    for (size_t i = 0; i < enemies_.size() && !broken; ++i) {
        const Enemy& e = *enemies_[i];
        if (!e.alive() || e.dormant() || e.frozen()) continue;
        bool attacking = e.state() == EnemyState::Attack;
        if (!attacking && e.def().contactDamage <= 0.0f) continue;
        if (e.state() == EnemyState::TeleportOut || e.state() == EnemyState::TeleportIn) continue;
        if (e.box.expanded(attacking ? 10.0f : 0.0f).intersects(box)) broken = true;
    }
    if (!broken) return;
    decoyTimer_ = 0.0f;
    ++decoysBroken_;
    particles_.emit("hologram_break", decoyPos_, 1.0f);
    playSound("sounds/shield_down.wav", decoyPos_, 0.7f, 1.3f);
}

void GameWorld::syncFrozenPlatforms() {
    // Platforms of the enemies no longer frozen (thawed, dead, gone) are removed...
    frozenPlatforms_.erase(std::remove_if(frozenPlatforms_.begin(), frozenPlatforms_.end(),
                                          [&](const FrozenPlatform& f) {
                                              Enemy* e = findEnemy(f.enemy);
                                              if (e && e->alive() && e->frozen()) return false;
                                              physics_.remove(f.collider);
                                              if (e) particles_.emit("frost_burst", e->center(), 0.7f);
                                              return true;
                                          }),
                           frozenPlatforms_.end());
    // ...and every frozen enemy carries one on the top of its box (it may fall or be pushed).
    for (const auto& e : enemies_) {
        if (!e->alive() || !e->frozen()) continue;
        Rect top{e->box.x, e->box.y, e->box.w, 4.0f};
        auto it = std::find_if(frozenPlatforms_.begin(), frozenPlatforms_.end(),
                               [&](const FrozenPlatform& f) { return f.enemy == e->uid(); });
        if (it == frozenPlatforms_.end())
            frozenPlatforms_.push_back({e->uid(), physics_.add(top, ColliderKind::OneWay, 0, e.get(), true)});
        else
            physics_.setDynamicBox(it->collider, top, e->vel);
    }
}

}  // namespace cosmic
