// The attacks of the phoenix (2.56, le Phénix Stellaire): « Colonnes solaires » (columns of
// sunlight fall from the roof onto marks shown on the ground, one after the other: a march through
// where Nova stands, or from both walls closing in on her), « Lame d'ailes » (its wings throw a
// blade of feathers across the whole arena: low, she jumps over it; high, she stays down), its
// glide between two blows (« vol plané »: wide swoops from one side of her to the other, above
// her) and its rebirth (« Cendres »: at the end of that phase it burns to ashes on the floor of its
// arena, cannot be hurt, throws embers out of them, then is reborn with the health of its phase and
// the next phase begins). Like every attack they are announced before they can hurt, and they are
// deterministic: positions come from the simulation, the only randomness is the world's gameplay
// generator. The columns live in BossHazards (as the golem's cracks), the blades are projectiles.

#include <cmath>

#include "engine/core/DetMath.h"
#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic {

namespace {
// Defaults of the parameters left at 0 (an attack made in the enemy editor works at once).
float orDefault(float v, float d) { return v > 0.0f ? v : d; }

/// std::clamp, with the middle when the range is empty (a narrow arena).
float clampIn(float v, float lo, float hi) { return lo <= hi ? std::clamp(v, lo, hi) : (lo + hi) * 0.5f; }

constexpr float kBurnTime = 1.9f;         // its death clip: the light flares, it falls and turns to ash
constexpr float kAshTime = 2.6f;          // it lies in its ashes
constexpr float kRiseTime = 1.25f;        // the rebirth (the roar of its « Cendres » sheet)
constexpr float kRebirthRadius = 110.0f;  // its rebirth burns around its ashes
constexpr float kRebirthWarn = 1.1f;      // the ring of the rebirth shown this long before
constexpr float kLaneGap = 34.0f;         // the high lane starts this far above the floor (Nova stands 22 px tall)
constexpr float kLaneHigh = 58.0f;        // ... and is this tall

void chevron(Renderer& r, Vec2 at, Vec2 dir, float size, Color c, float px) {
    Vec2 n{-dir.y, dir.x};
    Vec2 tip = at + dir * (size * 0.5f);
    Vec2 back = at - dir * (size * 0.5f);
    r.drawWorldLine(back + n * (size * 0.6f), tip, c, px, BlendMode::Additive);
    r.drawWorldLine(back - n * (size * 0.6f), tip, c, px, BlendMode::Additive);
}
}  // namespace

// ================================================================= choosing

float Enemy::phoenixFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    switch (a.type) {
        // Columns still falling: something else first.
        case AttackType::Sunbeam: return (w.hazards().crackCount(uid_) > 4 ? 0.3f : 1.2f) * (far ? 1.2f : 1.0f);
        case AttackType::Sweep: return (air ? 0.8f : 1.1f) * (near ? 1.2f : 1.0f);
        default: return 1.0f;
    }
}

bool Enemy::ashVeil() const {
    if (!def_.isBoss || !alive()) return false;
    if (inAshes()) return true;
    return phase_ < static_cast<int>(def_.phases.size()) && def_.phases[static_cast<size_t>(phase_)].ashes;
}

// ============================================================ the glide

bool Enemy::glidesDuring(const AttackDef& a) const {
    // What it throws, rains or calls down does not need it to hold still: it glides on (its
    // beams, rings, crowns, wind and pulls are aimed from where it stands: it holds still).
    if (!hasPhoenix_ || !def_.flying || !def_.isBoss || !engaged_ || lastStand_) return false;
    switch (a.type) {
        case AttackType::Spread:
        case AttackType::Projectile:
        case AttackType::Arc:
        case AttackType::Rain:
        case AttackType::Eruption:
        case AttackType::CloudShot:
        case AttackType::Sunbeam:
        case AttackType::Sweep:
        case AttackType::Summon: return true;
        default: return false;
    }
}

void Enemy::phoenixGlide(GameWorld& w, float dt, float pace, bool steer) {
    // A pendulum of swoops centred on the target: high at both ends of the swing, lowest over
    // her (a figure of eight), its speed carried from one moment to the next (it glides, it does
    // not hover). Kept off the walls, the corners and the roof of its arena.
    glideClock_ += dt;
    const Rect arena = arenaArea(w);
    const float k = glideClock_ * 0.5f;
    const float swing = std::min(320.0f, arena.w * 0.3f);
    float x = playerPos_.x + swing * dmath::sin(k);
    float y = playerPos_.y - 205.0f + 55.0f * dmath::cos(2.0f * k);
    const float mx = box.w * 0.5f + 70.0f;
    x = clampIn(x, arena.x + mx, arena.right() - mx);
    y = clampIn(y, arena.y + box.h * 0.5f + 28.0f, arena.bottom() - box.h * 0.5f - 64.0f);
    const Vec2 to = Vec2{x, y} - box.center();
    const float d = to.length();
    const float sp = def_.speed * def_.chaseSpeedMultiplier * phaseSpeed() * pace;
    const Vec2 want = d > 2.0f ? to / d * std::min(sp * 1.6f, d * 2.5f) : Vec2{};
    vel = lerpv(vel, want, damp(2.2f, dt));
    if (!steer) return;  // during a blow: its pose and its aim stay
    face(std::abs(vel.x) > 25.0f ? vel.x : playerPos_.x - box.center().x);
    playClip("move");
}

void Enemy::phoenixKeepOff(GameWorld& w) {
    // Back toward the middle when it comes within reach of a wall or of the roof (after a dive
    // toward a wall, a blow begun there): the closer, the harder, never as fast as its dives.
    const Rect arena = arenaArea(w);
    constexpr float kWall = 120.0f, kRoof = 60.0f;
    const float l = box.x - arena.x, r = arena.right() - box.right(), top = box.y - arena.y;
    if (l < kWall) vel.x = std::max(vel.x, 170.0f * (1.0f - std::max(0.0f, l) / kWall));
    if (r < kWall) vel.x = std::min(vel.x, -170.0f * (1.0f - std::max(0.0f, r) / kWall));
    if (top < kRoof) vel.y = std::max(vel.y, 120.0f * (1.0f - std::max(0.0f, top) / kRoof));
}

// ============================================================ the wind-up

void Enemy::planColumns(GameWorld& w) {
    // Where the columns will fall: a march from its side through where she will be and beyond,
    // or (two arms) pairs closing in from both walls, the last one on her. Each column from the
    // roof down to the floor (through the one-way ledges and its perches of embers).
    const AttackDef& a = *attack_;
    const Rect arena = arenaArea(w);
    const int n = std::clamp(a.count, 1, 11);
    const float gap = orDefault(a.spread, 90.0f);
    const float first = orDefault(a.burstInterval, 0.6f);
    const Vec2 aim = a.lead > 0.0f ? predictPlayer(w, (std::max(0.0f, stateTimer_) + first) * a.lead) : playerPos_;
    std::vector<float> xs;
    if (a.arms >= 2) {
        for (int i = 0; i < n; ++i) {
            const float d = gap * static_cast<float>(n - i);
            xs.push_back(aim.x - d);
            xs.push_back(aim.x + d);
        }
        xs.push_back(aim.x);
    } else {
        float dir = signf(aim.x - box.center().x);
        if (dir == 0.0f) dir = facing;
        const float start = aim.x - dir * gap * std::floor(static_cast<float>(n - 1) * 0.5f);
        for (int i = 0; i < n; ++i) xs.push_back(start + dir * gap * static_cast<float>(i));
    }
    const float y0 = std::min(aim.y, box.center().y) - 20.0f;
    path_.clear();
    path2_.clear();
    for (float x : xs) {
        x = clampIn(x, arena.x + 24.0f, arena.right() - 24.0f);
        float floor = std::max(arena.bottom(), aim.y + 12.0f);
        float roof = std::min(arena.y, y0);
        const Vec2 from{x, y0};
        const Vec2 down{x, floor + 400.0f}, up{x, roof - 400.0f};
        if (auto t = w.physics().raycast(from, down, false, ColliderTag::Crumble)) floor = from.y + (down.y - from.y) * *t;
        if (auto t = w.physics().raycast(from, up, false, ColliderTag::Crumble)) roof = from.y + (up.y - from.y) * *t;
        roof = std::min(roof, floor - 40.0f);
        path_.push_back({x, floor});
        path2_.push_back({x, roof});
    }
    markPos_ = aim;
}

void Enemy::planLane(GameWorld& w, bool low) {
    // The lane of the blade, at the level she stands on (a ledge too): along it, or just above
    // her head.
    const Rect arena = arenaArea(w);
    laneLow_ = low;
    float floor = arena.bottom();
    if (auto g = groundAt(w, playerPos_.x, playerPos_.y)) floor = *g;
    laneFloor_ = floor;
    const float h = orDefault(attack_ ? attack_->height : 0.0f, 30.0f);
    lane_ = low ? Rect{arena.x, floor - h, arena.w, h} : Rect{arena.x, floor - kLaneGap - kLaneHigh, arena.w, kLaneHigh};
}

void Enemy::phoenixBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    path_.clear();
    path2_.clear();
    bladesLeft_ = 0;
    switch (a.type) {
        case AttackType::Sunbeam:
            planColumns(w);
            if (a.sound.empty()) w.playSound("sounds/laser_charge.wav", box.center(), 0.6f, 0.8f);
            break;
        case AttackType::Sweep:
            bladesLeft_ = std::clamp(a.count, 1, 4);
            planLane(w, w.rng().chance(0.5f));
            if (a.sound.empty()) w.playSound("sounds/inhale.wav", box.center(), 0.8f, 1.2f);
            break;
        default: break;
    }
}

// =========================================================== the attack

void Enemy::throwBlade(GameWorld& w) {
    // A blade of feathers stacked over the lane, from the wall on its side across the arena.
    const AttackDef& a = *attack_;
    const Rect arena = arenaArea(w);
    const float side = box.center().x <= arena.center().x ? -1.0f : 1.0f;
    const float dir = -side;
    const float x0 = side < 0.0f ? arena.x + 12.0f : arena.right() - 12.0f;
    const float r = a.projectileRadius > 0.0f ? a.projectileRadius : 6.0f;
    const int n = std::max(2, static_cast<int>(std::ceil(lane_.h / (r * 1.7f))));
    const float speed = std::max(120.0f, a.projectileSpeed);
    for (int i = 0; i < n; ++i) {
        Projectile p;
        const float y = lane_.y + r + (lane_.h - 2.0f * r) * static_cast<float>(i) / static_cast<float>(n - 1);
        p.pos = p.prevPos = {x0, y};
        p.vel = {dir * speed, 0.0f};
        p.damage = a.damage;
        p.type = DamageType::Projectile;
        p.clip = a.projectile;
        p.radius = r;
        p.life = arena.w / speed + 0.6f;
        p.light = lightColor();
        p.lightRadius = 30.0f;
        p.knockback = a.knockback;
        p.hitEffect = a.hitEffect.empty() ? "fire_burst" : a.hitEffect;
        w.spawnProjectile(p);
    }
    playClip(a.attackAnim, true);
    w.particles().emit("wing_ash", box.center(), 1.2f, {dir, 0.0f});
    w.playSound("sounds/dash.wav", box.center(), 1.0f, 0.55f);
    w.shake(0.2f);
}

void Enemy::phoenixStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    switch (a.type) {
        case AttackType::Sunbeam: {
            const float first = orDefault(a.burstInterval, 0.6f);
            const float step = a.stagger > 0.0f ? a.stagger : 0.18f;
            float last = first;
            for (size_t k = 0; k < path_.size(); ++k) {
                // A march: one after the other; from both walls: two by two, then the last one.
                const float order = a.arms >= 2 ? static_cast<float>(k / 2) : static_cast<float>(k);
                BossHazards::Crack c;
                c.owner = uid_;
                c.sun = true;
                c.at = path_[k];
                c.fromY = k < path2_.size() ? path2_[k].y : c.at.y - 400.0f;
                c.height = c.at.y - c.fromY;
                c.half = orDefault(a.radius, 22.0f);
                c.delay = first + order * step;
                c.burst = orDefault(a.duration, 0.35f);
                c.damage = a.damage;
                c.knockback = a.knockback;
                c.color = lightColor();
                w.hazards().addCrack(c);
                last = std::max(last, c.delay);
            }
            // It holds its wings open while its light falls (the next blow may come before the last).
            stateTimer_ = std::max(stateTimer_, std::min(last, a.active + 0.6f));
            w.playSound("sounds/star_roar.wav", box.center(), 0.6f, 1.3f);
            break;
        }
        case AttackType::Sweep:
            throwBlade(w);
            --bladesLeft_;
            burstTimer_ = orDefault(a.burstInterval, 1.1f);
            if (bladesLeft_ > 0) {
                planLane(w, !laneLow_);  // the next one on the other lane
                stateTimer_ = std::max(stateTimer_, static_cast<float>(bladesLeft_) * burstTimer_ + 0.1f);
            }
            break;
        default: break;
    }
}

void Enemy::phoenixActive(GameWorld& w, float dt) {
    const AttackDef& a = *attack_;
    if (def_.flying && !glidesDuring(a)) vel = lerpv(vel, {}, damp(5.0f, dt));
    if (a.type == AttackType::Sweep && bladesLeft_ > 0) {
        burstTimer_ -= dt;
        if (burstTimer_ <= 0.0f) {
            throwBlade(w);
            --bladesLeft_;
            burstTimer_ = orDefault(a.burstInterval, 1.1f);
            if (bladesLeft_ > 0) planLane(w, !laneLow_);
        }
    }
}

void Enemy::phoenixEnd(GameWorld&, bool cut) {
    // Its columns keep their own time (as the golem's cracks); its blades fly on.
    bladesLeft_ = 0;
    if (cut) vel = {};
}

// ============================================================ its rebirth

void Enemy::beginAshes(GameWorld& w, int next) {
    // « Cendres »: the light flares, it falls onto the floor of its arena (toward the middle) and
    // turns to ash; it cannot be hurt until it is reborn.
    if (attack_ && attack_->isGuardianType()) guardianEnd(w);
    if (attack_ && attack_->isDragonType()) dragonEnd(w);
    if (attack_ && attack_->isGolemType()) golemEnd(w);
    if (attack_ && attack_->isPhoenixType()) phoenixEnd(w);
    attack_ = nullptr;
    staged_.clear();
    comboLeft_ = 0;
    burstsLeft_ = 0;
    ashes_ = AshStage::Burn;
    ashTimer_ = kBurnTime;
    ashNext_ = next;
    state_ = EnemyState::PhaseTransition;
    stateTimer_ = kBurnTime + kAshTime + kRiseTime;
    const Rect arena = arenaArea(w);
    const float x = clampIn(box.center().x, arena.x + arena.w * 0.32f, arena.right() - arena.w * 0.32f);
    float y = arena.bottom();
    const Vec2 from{x, std::min(box.center().y, arena.bottom() - 40.0f)}, to{x, arena.bottom() + 300.0f};
    if (auto t = w.physics().raycast(from, to, false, ColliderTag::Crumble)) y = from.y + (to.y - from.y) * *t;
    ashSpot_ = {x, y};
    dropThrough_ = true;
    emberTimer_ = 0.5f;
    playClip("death", true);
    w.playSound("sounds/boss_roar_fire.wav", box.center(), 1.0f, 0.8f);
    w.playSound("sounds/boss_phase.wav", 0.9f);
    w.flash({255, 190, 120, 255}, 0.3f);
    w.shake(0.6f);
    w.particles().emit("boss_phase", box.center(), 2.0f);
    WorldMessage m;
    m.style = WorldMessage::Style::Boss;
    m.text = "Le Phénix se consume…";
    m.subtitle = def_.phases[static_cast<size_t>(phase_)].name;
    m.duration = 2.4f;
    w.message(m);
    if (!objectId_.empty()) w.emit(objectId_, "bossAshes");
}

void Enemy::stepAshes(GameWorld& w, float dt) {
    ashTimer_ -= dt;
    const Color fire = lightColor();
    switch (ashes_) {
        case AshStage::Burn: {
            // Down onto its spot (through the one-way ledges), shedding embers.
            const Vec2 to = ashSpot_ - box.bottomCenter();
            const float d = to.length();
            const float sp = 340.0f;
            if (d <= sp * dt + 1.0f) vel = {};
            else vel = to / d * sp;
            face(playerPos_.x - box.center().x);
            if (w.fxRng().chance(0.35f)) w.particles().emit("embers_rising", box.center(), 0.6f, {}, &fire);
            if (ashTimer_ <= 0.0f) {
                ashes_ = AshStage::Ash;
                ashTimer_ = kAshTime;
                vel = {};
                MoveOptions opt;
                opt.isEnemy = true;
                opt.dropThrough = true;
                opt.ignoreTags = ColliderTag::Crumble;
                const Rect spot = Rect::fromBottomCenter(ashSpot_, box.size());
                if (!w.physics().overlapsSolid(spot, opt)) box = spot;
                ashSpot_ = box.bottomCenter();
                w.shake(0.4f);
                w.particles().emit("ash_cloud", ashSpot_ + Vec2{0.0f, -12.0f}, 1.6f);
                w.playSound("sounds/crumble.wav", ashSpot_, 0.9f, 0.7f);
            }
            break;
        }
        case AshStage::Ash: {
            vel = {};
            // Embers thrown out of its ashes, falling back around them.
            emberTimer_ -= dt;
            if (emberTimer_ <= 0.0f) {
                emberTimer_ = 0.55f;
                for (float side : {-1.0f, 1.0f}) {
                    Projectile p;
                    p.pos = p.prevPos = ashSpot_ + Vec2{side * 10.0f, -18.0f};
                    p.vel = {side * w.rng().range(60.0f, 210.0f), -w.rng().range(280.0f, 390.0f)};
                    p.gravity = 600.0f;
                    p.damage = 1.0f;
                    p.type = DamageType::Projectile;
                    p.clip = "ember";
                    p.radius = 5.0f;
                    p.life = 3.0f;
                    p.light = fire;
                    p.knockback = 160.0f;
                    p.hitEffect = "fire_burst";
                    w.spawnProjectile(p);
                }
                w.playSound("sounds/lava_bubble.wav", ashSpot_, 0.5f, 1.2f);
            }
            if (ashTimer_ <= 0.0f) {
                // Reborn: with the health of its phase, its fire bursting around its ashes.
                ashes_ = AshStage::Rise;
                ashTimer_ = kRiseTime;
                const float share = def_.phases[static_cast<size_t>(phase_)].rebirth;
                hp_ = std::max(hp_, std::min(maxHp_, maxHp_ * share));
                playClip("roar", true);
                const Vec2 heart = ashSpot_ + Vec2{0.0f, -30.0f};
                Player& p = w.player();
                if (p.alive() && Collision::circleIntersectsRect(heart, kRebirthRadius, p.box)) {
                    DamageInfo dmg;
                    dmg.amount = 2.0f;
                    dmg.type = DamageType::Explosion;
                    dmg.source = heart;
                    dmg.knockback = 320.0f;
                    w.damagePlayer(dmg);
                }
                w.flash({255, 240, 200, 255}, 0.4f);
                w.shake(0.8f);
                w.rumble(0.8f, 0.6f, 300);
                w.particles().emit("boss_phase", heart, 2.5f);
                w.particles().emit("fire_burst", heart, 2.4f);
                w.playSound("sounds/supernova.wav", heart, 1.0f, 1.1f);
                if (!objectId_.empty()) w.emit(objectId_, "bossRebirth");
            }
            break;
        }
        case AshStage::Rise:
            // It spreads its wings and lifts off its ashes.
            vel = lerpv(vel, {0.0f, -80.0f}, damp(3.0f, dt));
            if (ashTimer_ <= 0.0f) {
                ashes_ = AshStage::None;
                dropThrough_ = false;
                enterPhase(w, ashNext_);
            }
            break;
        default: break;
    }
}

// ================================================================== render

void Enemy::renderAshes(RenderContext& ctx) const {
    // Its ashes glow (embers breathing in them); before its rebirth, a ring of fire on the floor
    // shows how far it will burn.
    const Color fire = lightColor();
    const float s = ctx.r.view().scale / 3.0f;
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 6.0f);
    if (ashes_ == AshStage::Ash) {
        ctx.r.drawWorldGlow(ashSpot_ + Vec2{0.0f, -8.0f}, 34.0f + 8.0f * pulse, fire.withAlpha(static_cast<uint8_t>(90 + 60 * pulse)));
        ctx.lights.add(ashSpot_ + Vec2{0.0f, -10.0f}, 110.0f, fire);
        if (ashTimer_ < kRebirthWarn) {
            const float k = 1.0f - ashTimer_ / kRebirthWarn;
            const Vec2 heart = ashSpot_ + Vec2{0.0f, -30.0f};
            const Color warn{255, 90, 60, static_cast<uint8_t>(90 + 150 * k)};
            ctx.r.drawWorldCircle(heart, kRebirthRadius, warn, (1.5f + 2.0f * k) * s);
            ctx.r.drawWorldGlow(heart, kRebirthRadius * (0.5f + 0.5f * k), fire.withAlpha(static_cast<uint8_t>(40 + 90 * k)));
            ctx.r.fillWorldRect(Rect::fromCenter(ashSpot_ + Vec2{0.0f, -1.0f}, {kRebirthRadius * 2.0f, 3.0f}), warn, BlendMode::Additive);
        }
    } else if (ashes_ == AshStage::Rise) {
        const float k = std::clamp(ashTimer_ / kRiseTime, 0.0f, 1.0f);
        ctx.r.drawWorldGlow(box.center(), 90.0f + 60.0f * (1.0f - k), fire.withAlpha(static_cast<uint8_t>(200 * k)));
        ctx.lights.add(box.center(), 220.0f, fire);
    }
}

void Enemy::renderPhoenix(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 14.0f);
    const Color fire = lightColor();
    switch (a.type) {
        case AttackType::Sunbeam:
            if (tele)
                for (size_t k = 0; k < path_.size(); ++k) {
                    // Where its light will fall: a mark on the ground, a faint thread from the roof.
                    const Vec2 at = path_[k];
                    const float roof = k < path2_.size() ? path2_[k].y : at.y - 300.0f;
                    const float h = orDefault(a.radius, 22.0f);
                    ctx.r.drawWorldLine({at.x, roof}, at, fire.withAlpha(static_cast<uint8_t>(20 + 60 * prog)), std::max(1.0f, s), BlendMode::Additive);
                    ctx.r.fillWorldRect(Rect::fromCenter(at + Vec2{0.0f, -2.0f}, {h * (0.6f + 0.8f * prog) * 2.0f, 3.0f}),
                                        fire.withAlpha(static_cast<uint8_t>(60 + 120 * prog)), BlendMode::Additive);
                    ctx.r.drawWorldGlow(at, h * (0.6f + 0.6f * prog), fire.withAlpha(static_cast<uint8_t>(30 + 70 * prog)));
                }
            break;
        case AttackType::Sweep: {
            if (!(tele || bladesLeft_ > 0) || lane_.w <= 0.0f) break;
            // The lane of the next blade across the arena: a band of light, its edges, chevrons
            // running the way it will go; above a low lane, arrows up (jump), over a high one,
            // arrows down (stay down).
            const float k = tele ? prog : std::clamp(1.0f - burstTimer_ / std::max(0.1f, orDefault(a.burstInterval, 1.1f)), 0.0f, 1.0f);
            const Color band = fire.withAlpha(static_cast<uint8_t>(26 + 40 * k + 20 * pulse));
            ctx.r.fillWorldRect(lane_, band, BlendMode::Additive);
            const Color edge{255, 110, 80, static_cast<uint8_t>(110 + 120 * k)};
            ctx.r.drawWorldLine(lane_.pos(), {lane_.right(), lane_.y}, edge, 2.0f * s, BlendMode::Additive);
            ctx.r.drawWorldLine({lane_.x, lane_.bottom()}, {lane_.right(), lane_.bottom()}, edge, 2.0f * s, BlendMode::Additive);
            const Rect arena = lane_;
            const float side = box.center().x <= arena.center().x ? -1.0f : 1.0f;
            const Vec2 dir{-side, 0.0f};
            const float gap = 70.0f;
            const float run = std::fmod(ctx.time * 160.0f, gap);
            const Color arrow{255, 230, 180, static_cast<uint8_t>(120 + 120 * k)};
            for (float x = lane_.x + run; x < lane_.right(); x += gap) {
                const float cx = side < 0.0f ? x : lane_.right() - (x - lane_.x);
                chevron(ctx.r, {cx, lane_.center().y}, dir, std::min(16.0f, lane_.h * 0.6f), arrow, 2.5f * s);
            }
            // Around her: what to do.
            const Vec2 c{playerPos_.x, laneLow_ ? lane_.y - 22.0f : lane_.bottom() + 6.0f};
            const Vec2 v = laneLow_ ? Vec2{0.0f, -1.0f} : Vec2{0.0f, 1.0f};
            for (int i = 0; i < 2; ++i)
                chevron(ctx.r, c + v * (static_cast<float>(i) * 9.0f + 4.0f * pulse), v, 12.0f, Color{255, 240, 200, static_cast<uint8_t>(140 + 100 * k)},
                        2.5f * s);
            break;
        }
        default: break;
    }
}

}  // namespace cosmic
