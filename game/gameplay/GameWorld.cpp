#include "game/gameplay/GameWorld.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engine/audio/AudioSystem.h"
#include "engine/core/Log.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/EntityFactory.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

GameWorld::GameWorld(WorldServices services, WorldMode mode) : services_(services), mode_(mode) {}

GameWorld::~GameWorld() = default;

bool GameWorld::load(const LevelData& level) {
    level_ = level;
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
    activeZone_ = nullptr;
    time_ = 0.0f;

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
    for (const auto& pid : level_.startPowers)
        if (const PowerDefinition* d = services_.content.power(pid)) player_.grantPower(*d);

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

void GameWorld::beginPlay() {
    state_ = WorldState::Playing;
    player_.controlEnabled = true;
    if (player_.state() == PlayerState::Hidden) player_.spawn(*this, respawn_, respawnFaceLeft_, true);
    camera_.releaseScript();
    if (mode_ == WorldMode::Play) timer_.start();
    emit("level", "levelStart");
}

// ================================================================== update

void GameWorld::fixedUpdate(const PlayerInput& in, float dt) {
    time_ += dt;
    if (flashTimer_ > 0.0f) flashTimer_ = std::max(0.0f, flashTimer_ - dt);
    if (splitFlash_ > 0.0f) splitFlash_ -= dt;
    if (mode_ == WorldMode::Editor) return;

    if (hitStop_ > 0.0f) {
        hitStop_ -= dt;
        camera_.fixedUpdate(dt);
        return;
    }

    cinematic_.fixedUpdate(*this, dt);  // also animates the letterbox / fades after the end
    if (state_ == WorldState::Intro && !cinematic_.active()) beginPlay();

    PlayerInput effective = (state_ == WorldState::Playing && !cinematic_.active()) ? in : PlayerInput{};

    if (state_ == WorldState::Playing) {
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

    updateEnemies(dt);
    updateProjectiles(dt);

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
    for (auto& e : enemies_) e->update(*this, dt);
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
                                      return true;
                                  }),
                   enemies_.end());
    for (auto& e : pendingEnemies_) enemies_.push_back(std::move(e));
    pendingEnemies_.clear();
}

void GameWorld::updateProjectiles(float dt) {
    for (auto& p : pendingProjectiles_) projectiles_.push_back(std::move(p));
    pendingProjectiles_.clear();
    for (auto& p : projectiles_) {
        if (!p.alive) continue;
        p.prevPos = p.pos;
        p.age += dt;
        p.life -= dt;
        if (p.life <= 0.0f) {
            p.alive = false;
            if (p.explosionRadius > 0.0f) explodeProjectile(p, p.pos);
            else if (p.clip != "shockwave") particles_.emit(p.hitEffect, p.pos, 0.6f);
            continue;
        }
        p.vel.y += p.gravity * dt;
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
            p.alive = false;
            if (p.explosionRadius > 0.0f) {
                explodeProjectile(p, hit);
                continue;
            }
            particles_.emit(p.hitEffect, hit, 0.8f);
            if (!p.hitSound.empty()) playSound(p.hitSound, hit, 0.6f);
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
            }
        }
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const auto& p) { return !p.alive; }),
                       projectiles_.end());
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
    if (lockedZone_) {
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

Enemy* GameWorld::spawnEnemy(const std::string& defId, Vec2 feet, bool faceLeft, const std::string& objectId,
                             float patrol) {
    const EnemyDefinition* d = services_.content.enemy(defId);
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
    if (e.countsForStats) {
        stats_.kills++;
        stats_.scoreBonus += e.def().scoreValue;
    }
    for (const auto& d : e.def().drops)
        if (rng_.chance(d.chance)) spawnDrop(d.type, e.center());
    // Minions vanish with their summoner.
    for (auto& other : enemies_)
        if (other->summonedBy == e.uid() && other->alive()) other->kill(*this);
    // Splitting enemies (slimes) leave smaller ones behind.
    if (e.def().deathSpawnCount > 0 && !e.def().deathSpawnId.empty() && e.def().deathSpawnId != e.def().id) {
        particles_.emit("split_splash", e.center(), 1.0f);
        playSound("sounds/slime_split.wav", e.center(), 0.9f);
        for (int i = 0; i < e.def().deathSpawnCount; ++i) {
            float side = (i % 2 == 0) ? -1.0f : 1.0f;
            Vec2 feet = e.box.bottomCenter() + Vec2{side * (6.0f + 6.0f * static_cast<float>(i / 2)), 0.0f};
            if (Enemy* child = spawnEnemy(e.def().deathSpawnId, feet, side < 0)) {
                child->vel = {side * (70.0f + 25.0f * static_cast<float>(i / 2)), -170.0f};
                child->countsForStats = e.countsForStats;
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
        if (!e.objectId().empty()) emit(e.objectId(), "bossDefeated");
    }
}

// ================================================================ feedback

void GameWorld::playSound(const std::string& rel, Vec2 worldPos, float volume, float pitch) {
    if (mode_ == WorldMode::Editor) return;
    Vec2 c = camera_.center();
    float dx = worldPos.x - c.x;
    float dist = distance(worldPos, c);
    float att = std::clamp(1.0f - (dist - 260.0f) / 500.0f, 0.15f, 1.0f);
    float pan = std::clamp(dx / 260.0f, -1.0f, 1.0f) * 0.55f;
    services_.res.playSound(rel, volume * att, pitch, pan);
}

void GameWorld::playSound(const std::string& rel, float volume, float pitch) {
    if (mode_ == WorldMode::Editor) return;
    services_.res.playSound(rel, volume, pitch, 0.0f);
}

void GameWorld::sustainSound(const std::string& key, const std::string& rel, float volume) {
    if (mode_ == WorldMode::Editor) return;
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
        if (onPlayerDied) onPlayerDied();
        player_.spawn(*this, respawn_, respawnFaceLeft_, true);
        return;
    }
    if (level_.timerMode == TimerMode::Challenge && stats_.deaths > level_.challengeMaxDeaths) {
        failLevel("Défi échoué : trop de chutes");
        player_.setHidden(true);
        return;
    }
    respawnPlayer();
}

void GameWorld::respawnPlayer() {
    projectiles_.clear();
    pendingProjectiles_.clear();
    player_.spawn(*this, respawn_, respawnFaceLeft_, true);
    // Keep unlocked powers; refill as a fresh start.
    camera_.reset(respawn_ - Vec2{0, 20});
    lockedZone_.reset();
    for (size_t i = 0; i < entities_.size(); ++i) entities_[i]->onPlayerRespawn(*this);
    playSound("sounds/respawn.wav", 0.8f);
    if (boss_ && boss_->alive()) {
        // The boss fight restarts from the checkpoint before the arena.
        restoreLevelMusic();
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

void GameWorld::startBoss(Enemy& boss) {
    boss_ = &boss;
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

}  // namespace cosmic
