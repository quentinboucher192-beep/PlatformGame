#include "game/gameplay/BossAttackDemo.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Log.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/player/Player.h"

namespace cosmic {

namespace {

constexpr float kDt = 1.0f / 120.0f;

/// The size of the neutral arena for `d`: the enemy editor's test arena (960 × 400); 2.73, taller
/// and wider for a giant (a gigaboss of the raids, up to 420 px high, shown on the raid sheets): its
/// whole body above the floor, room around it for Nova.
Vec2 neutralArenaSize(const EnemyDefinition& d) {
    if (d.size.y <= 200.0f) return {960.0f, 400.0f};
    return {std::ceil(std::max(960.0f, d.size.x * 5.0f) / 32.0f) * 32.0f, std::ceil((d.size.y + 300.0f) / 32.0f) * 32.0f};
}

/// A neutral arena (a boss of the enemy editor or of a package, in no level of the game): the
/// test arena of the enemy editor, `size` wide and high (its floor 80 px thick).
LevelData neutralArena(Vec2 size) {
    LevelData l = LevelData::makeEmpty("boss_attack_demo", "Arène neutre", "sylvanox");
    l.objects.clear();
    l.intro.steps.clear();
    l.outro.steps.clear();
    l.events.clear();
    l.bounds = {0, 0, size.x, size.y};
    const float floor = size.y - 80.0f;
    l.timerMode = TimerMode::Unlimited;
    auto add = [&](const char* type, Rect r, Json props) {
        LevelObject o;
        o.id = l.nextObjectId();
        o.type = type;
        o.layer = std::string(type) == "solid" || std::string(type) == "oneway" ? "terrain" : "gameplay";
        o.rect = r;
        o.props = std::move(props);
        l.objects.push_back(o);
    };
    add("solid", {0, floor, size.x, 80}, {{"material", "ruin_stone"}});
    add("solid", {0, 0, 32, floor}, {{"material", "ancient_metal"}});
    add("solid", {size.x - 32, 0, 32, floor}, {{"material", "ancient_metal"}});
    add("oneway", {240, floor - 64, 96, 16}, {{"style", "tech_ledge"}});
    add("oneway", {size.x - 340, floor - 64, 96, 16}, {{"style", "tech_ledge"}});
    add("player_start", {155, floor - 22, 10, 22}, Json::object());
    add("light", {size.x * 0.5f - 8, floor - 120, 16, 16}, {{"color", "#39f0d8"}, {"radius", 260}});
    l.ambientLight = {215, 220, 235, 255};
    return l;
}

PlayerInput press(int dir, bool jumpPressed, bool jumpHeld, bool down) {
    PlayerInput p;
    if (dir > 0) {
        p.held[static_cast<int>(Action::MoveRight)] = true;
        p.move.x = 1.0f;
    } else if (dir < 0) {
        p.held[static_cast<int>(Action::MoveLeft)] = true;
        p.move.x = -1.0f;
    }
    if (jumpPressed) p.pressed[static_cast<int>(Action::Jump)] = true;
    if (jumpPressed || jumpHeld) p.held[static_cast<int>(Action::Jump)] = true;
    if (down) {
        p.held[static_cast<int>(Action::MoveDown)] = true;
        p.move.y = 1.0f;
    }
    return p;
}

}  // namespace

EnemyDefinition demoDefinition(const EnemyDefinition& d, const std::string& attackId, const AttackPreviewPlan& plan) {
    EnemyDefinition out = d;
    for (BossPhase& p : out.phases) {
        p.attacks = {attackId};
        p.enrageTime = 0.0f;
    }
    out.enrageTime = 0.0f;
    if (plan.lastStand) out.finalAttacks = {attackId};
    return out;
}

float demoNovaDistance(const AttackDef& a) {
    switch (a.type) {
        // Close blows: near enough to be reached, so the dodge shows.
        case AttackType::Melee:
        case AttackType::Contact:
        case AttackType::Blink:
        case AttackType::Hex:
        case AttackType::Vortex: return std::clamp(a.range * 0.8f, 70.0f, 170.0f);
        case AttackType::Slam:
        case AttackType::Leap:
        case AttackType::Lunge:
        case AttackType::Charge:
        case AttackType::Shell:
        case AttackType::Dive:
        case AttackType::Gust: return std::clamp(a.range * 0.7f, 140.0f, 260.0f);
        default: return std::clamp(a.range * 0.75f, 160.0f, 320.0f);
    }
}

Rect demoWindow(const Rect& arena, const Rect& boss, const Rect& nova, float aspect, float minWidth, float maxWidth) {
    aspect = std::max(0.3f, aspect);
    const float margin = 96.0f;
    const float left = std::min(boss.x, nova.x) - margin, right = std::max(boss.right(), nova.right()) + margin;
    const float top = std::min(boss.y, nova.y) - 64.0f;
    const float bottom = std::max(boss.bottom(), nova.bottom()) + 40.0f;
    float w = std::max({right - left, minWidth, (bottom - top) * aspect});
    w = std::min(w, std::max(minWidth, maxWidth));  // never so wide that Nova is lost in it
    if (arena.w > 0.0f) w = std::min(w, arena.w + 64.0f);
    const float h = w / aspect;
    Vec2 c{(left + right) * 0.5f, (top + bottom) * 0.5f};
    // Nova always in view (with some room): when both do not fit, as much of the boss as can be.
    const float mx = std::min(64.0f, w * 0.15f), my = std::min(40.0f, h * 0.15f);
    c.x = std::clamp(c.x, nova.right() + mx - w * 0.5f, std::max(nova.right() + mx - w * 0.5f, nova.x - mx + w * 0.5f));
    c.y = std::clamp(c.y, nova.bottom() + my - h * 0.5f, std::max(nova.bottom() + my - h * 0.5f, nova.y - my + h * 0.5f));
    // Inside the arena (a little beyond its walls when the frame is wider or taller).
    if (arena.w > 0.0f && arena.h > 0.0f) {
        const Rect a = arena.expanded(32.0f);
        if (w >= a.w) c.x = a.center().x;
        else c.x = std::clamp(c.x, a.x + w * 0.5f, a.right() - w * 0.5f);
        if (h >= a.h) c.y = a.bottom() - h * 0.5f;  // the floor in view first
        else c.y = std::clamp(c.y, a.y + h * 0.5f, a.bottom() - h * 0.5f);
    }
    return Rect::fromCenter(c, {w, h});
}

BossAttackDemo::BossAttackDemo() = default;
BossAttackDemo::~BossAttackDemo() {
    world_.reset();  // before the definitions and the content it reads
}

bool BossAttackDemo::setup(std::shared_ptr<ContentDatabase> content, ResourceManager& res, const EnemyDefinition& def, const std::string& attackId,
                           std::optional<LevelData> level, const std::string& bossObject, std::string* error) {
    auto fail = [&](const std::string& why) {
        failure_ = why;
        if (error) *error = why;
        world_.reset();
        return false;
    };
    world_.reset();
    content_ = std::move(content);
    attackId_ = attackId;
    ready_ = false;
    failure_.clear();
    warmSteps_ = shown_ = 0;
    time_ = freeTime_ = weave_ = 0.0f;
    wasAttacking_ = false;
    jumpHold_ = crouch_ = dodgeDir_ = dodgeSteps_ = 0;
    const AttackDef* attack = def.attack(attackId);
    if (!content_ || !attack) return fail("Cette attaque est introuvable dans la définition du boss.");
    attackName_ = attack->name;
    plan_ = attackPreviewPlan(def, *attack);
    if (!plan_.showable) return fail(plan_.reason);
    const EnemyDefinition restricted = demoDefinition(def, attackId, plan_);
    // Its own level only for a native boss placed in a level of the game (an enemy revision of
    // the private content, bound by the level's copy); else the neutral arena.
    const LevelEntry* entry = level ? content_->level(level->id) : nullptr;
    const bool own = level && entry && entry->source == ContentSource::Builtin && !bossObject.empty() && content_->isNativeEnemy(def.id) &&
                     content_->setEnemyRevision(kRevision, restricted);
    WorldServices ws{res, silence_, *content_, nullptr, 0.0f, 0.6f};
    world_ = std::make_unique<GameWorld>(ws, own ? WorldMode::Play : WorldMode::Arena);
    GameWorld& w = *world_;
    w.silent = true;
    w.setKeysPerSlot(true);
    own_ = own;
    bossId_ = def.id;
    bossObject_ = bossObject;
    dist_ = demoNovaDistance(*attack);
    arranged_ = false;
    if (own) {
        LevelData l = std::move(*level);
        if (!l.metadata.is_object()) l.metadata = Json::object();
        l.metadata["enemyRevisions"][def.id] = kRevision;
        if (!w.load(l)) return fail("Son niveau ne peut pas être chargé.");
        w.seedGameplay(levelSeed(l.id));
    } else {
        neutralDef_ = std::make_unique<EnemyDefinition>(restricted);
        if (!w.load(neutralArena(neutralArenaSize(restricted)))) return fail("L'arène neutre ne peut pas être chargée.");
        w.seedGameplay(levelSeed("boss_attack_demo"));
    }
    return true;
}

bool BossAttackDemo::arrange() {
    if (arranged_) return true;
    if (!world_ || failed()) return false;
    auto fail = [&](const std::string& why) {
        failure_ = why;
        world_.reset();
        return false;
    };
    arranged_ = true;
    GameWorld& w = *world_;
    if (own_) {
        BossTrainingSetup s;
        s.bossId = bossId_;
        s.levelId = w.level().id;
        s.bossObject = bossObject_;
        s.hearts = kTrainingMaxHearts;
        s.slots = 1;
        s.phase = plan_.phase;
        // The arena as a training run finds it (the boss at its phase), its safe places.
        std::optional<TrainingArena> ta = probeTrainingArena(w, s);
        const Enemy* b = bossTrainingEnemy(w, s);
        if (!ta || !b) return fail("Son arène est introuvable dans son niveau.");
        bossUid_ = b->uid();
        arena_ = ta->zone;
        // Nova: on firm ground of the arena, as near the distance wanted as can be (the lowest
        // surface first: the floor of the fight).
        const Vec2 bc = b->center();
        int best = -1;
        float bestScore = 1e30f;
        float lowest = -1e30f;
        for (const TrainingSpot& sp : ta->spots) lowest = std::max(lowest, sp.feet.y);
        for (size_t i = 0; i < ta->spots.size(); ++i) {
            const TrainingSpot& sp = ta->spots[i];
            const float dx = std::abs(sp.feet.x - bc.x);
            float score = std::abs(dx - dist_) + (sp.ledge ? 140.0f : 0.0f) + (lowest - sp.feet.y) * 0.8f + (sp.feet.x > bc.x ? 24.0f : 0.0f);
            if (score < bestScore) {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
        home_ = best >= 0 ? ta->spots[static_cast<size_t>(best)].feet : ta->usualStart;
        w.setTraining(true);
        w.setInvincible(true);
        w.startAt(home_, bc.x < home_.x);
    } else {
        w.setTraining(true);
        w.setInvincible(true);
        const float floorY = w.level().bounds.h - 80.0f;  // 320 in the editor's arena
        const float bx = std::round(w.level().bounds.w * 2.0f / 3.0f);  // 640 there
        Enemy* b = w.spawnEnemy(*neutralDef_, {bx, neutralDef_->flying ? floorY - 70.0f : floorY}, true, "", 120.0f);
        if (!b) return fail("Le boss ne peut pas être placé dans l'arène.");
        bossUid_ = b->uid();
        arena_ = w.level().bounds;
        home_ = {std::clamp(bx - dist_, 60.0f, bx - 60.0f), floorY};
        w.startAt(home_, false);
        w.fixedUpdate(PlayerInput{}, kDt);  // the boss joins the world
        if (Enemy* e = bossMut()) {
            w.startBoss(*e);
            jumpBossToPhase(w, *e, plan_.phase);
        }
    }
    Log::info("Fiche des boss : exemple de {} / {} ({}, phase {}{})", bossId_, attackId_, own_ ? "son arène" : "arène neutre", plan_.phase + 1,
              plan_.lastStand ? ", dernier souffle" : "");
    return true;
}

Enemy* BossAttackDemo::bossMut() const { return world_ ? world_->findEnemy(bossUid_) : nullptr; }
const Enemy* BossAttackDemo::boss() const { return bossMut(); }

int BossAttackDemo::phase() const {
    const Enemy* b = boss();
    return b ? b->phaseIndex() : 0;
}

bool BossAttackDemo::lastStand() const {
    const Enemy* b = boss();
    return b && b->lastStand();
}

bool BossAttackDemo::over() const {
    if (!world_) return true;
    const WorldState s = world_->state();
    const Enemy* b = boss();
    return s == WorldState::Failed || s == WorldState::Completed || s == WorldState::Outro || !b || !b->alive();
}

bool BossAttackDemo::bossFree(const Enemy& b) const {
    switch (b.state()) {
        case EnemyState::Chase:
        case EnemyState::Alert:
        case EnemyState::Idle:
        case EnemyState::Patrol: break;
        default: return false;
    }
    // The Titan's presses and missiles one pattern at a time, as in the fight (and the captures).
    if (b.isTitan() && (world_->hazards().pressCount(b.uid()) > 0 || world_->hazards().missileCount(b.uid()) > 0 || b.venting())) return false;
    return true;
}

void BossAttackDemo::tryForce(Enemy& b) {
    GameWorld& w = *world_;
    // Its last stand first (an attack of its « Dernier souffle »): out of health, as when the
    // last blow of the fight lands (it cannot be hurt any more and gives its last attacks).
    if (plan_.lastStand && !b.lastStand()) {
        if (b.dormant() || b.state() == EnemyState::TeleportIn || b.state() == EnemyState::PhaseTransition) return;
        if (b.inAshes() || b.warded() || b.stalled()) return;
        const int n = static_cast<int>(b.def().phases.size());
        b.setWounds(1.0f, std::max(0, n - 1));
        DamageInfo d;
        d.amount = 1000.0f;
        d.type = DamageType::Projectile;
        d.source = b.center();
        b.takeDamage(w, d);
        return;
    }
    if (!bossFree(b)) {
        freeTime_ = 0.0f;
        return;
    }
    // In its last stand at once (it would end it otherwise); else a short breath between two
    // (the boss may also give it by itself: it knows no other attack).
    const float wait = b.lastStand() || shown_ == 0 ? 0.0f : 0.6f;
    if (freeTime_ < wait) return;
    if (b.forceAttack(w, attackId_)) freeTime_ = 0.0f;
}

void BossAttackDemo::lookAround() {
    const Enemy* b = boss();
    const bool attacking = b && (b->state() == EnemyState::Telegraph || b->state() == EnemyState::Attack) && b->currentAttackName() == attackName_;
    if (attacking && !wasAttacking_) ++shown_;
    wasAttacking_ = attacking;
}

bool BossAttackDemo::warmUp(int steps) {
    if (!arranged_ && !arrange()) return false;
    if (!world_ || ready_ || failed()) return ready_;
    for (int i = 0; i < steps && !ready_; ++i) {
        step();
        if (shown_ > 0) ready_ = true;
        if (++warmSteps_ >= kMaxWarmSteps && !ready_) {
            failure_ = "Le boss ne lance pas cette attaque dans son arène : l'exemple ne peut pas être montré.";
            Log::warn("Fiche des boss : {} jamais lancée en {} pas", attackId_, warmSteps_);
            return false;
        }
        if (over()) {
            failure_ = "Le combat de l'exemple s'est arrêté avant l'attaque.";
            return false;
        }
    }
    return ready_;
}

void BossAttackDemo::step() {
    if (!world_) return;
    GameWorld& w = *world_;
    if (Enemy* b = bossMut(); b && b->alive()) {
        if (bossFree(*b)) freeTime_ += kDt;
        tryForce(*b);
    }
    PlayerInput in = novaInput();
    w.fixedUpdate(in, kDt);
    Player& p = w.player();
    p.health = p.maxHealth();
    lookAround();
    if (ready_) time_ += kDt;
}

// ------------------------------------------------------------------------- Nova's moves

PlayerInput BossAttackDemo::novaInput() {
    GameWorld& w = *world_;
    const Player& p = w.player();
    if (!p.alive() || p.state() == PlayerState::Respawn) return {};
    const Rect body = p.box;
    const Vec2 c = body.center();
    const bool grounded = p.grounded();
    // A jump under way: held to its full height.
    if (jumpHold_ > 0) {
        --jumpHold_;
        return press(dodgeSteps_ > 0 ? dodgeDir_ : 0, false, true, false);
    }
    if (dodgeSteps_ > 0) --dodgeSteps_;
    else dodgeDir_ = 0;

    // What comes at her in the next moments: a shot (its arc) or the boss itself.
    bool jump = false, duck = false;
    int away = 0;
    const Rect wide = body.expanded(4.0f);
    for (const Projectile& q : w.projectiles()) {
        if (q.fromPlayer || !q.alive) continue;
        Vec2 pos = q.pos, vel = q.vel;
        for (float t = 0.0f; t <= 0.36f; t += 1.0f / 30.0f) {
            if (wide.intersects(Rect::fromCenter(pos, {q.radius * 2.0f + 2.0f, q.radius * 2.0f + 2.0f}))) {
                if (t <= 0.3f) {
                    if (std::abs(vel.x) >= std::abs(vel.y)) {
                        // Along the ground: over it; at head height: under it.
                        if (pos.y < c.y - body.h * 0.15f && grounded) duck = true;
                        else jump = true;
                    } else {
                        away = q.pos.x <= c.x ? 1 : -1;
                    }
                }
                break;
            }
            vel.y += q.gravity * (1.0f / 30.0f);
            pos += vel * (1.0f / 30.0f);
        }
    }
    if (const Enemy* b = boss(); b && b->alive() && b->state() == EnemyState::Attack) {
        // The boss rushing along the ground at her (a charge, a lunge, a roll, a dive…).
        const Vec2 bv = b->vel;
        if (std::abs(bv.x) > 120.0f && signf(bv.x) == signf(c.x - b->center().x)) {
            const Rect ahead = b->box.translated({bv.x * 0.28f, bv.y * 0.28f}).united(b->box);
            if (ahead.intersects(wide)) jump = true;
        }
    }
    // The marks on the ground: presses about to fall, cracks and runes about to burst, burning
    // clouds, steam, locked missiles. Out of them, to the nearest side that is clear.
    std::vector<std::pair<float, float>> unsafe;
    const BossHazards& h = w.hazards();
    for (const auto& pr : h.presses())
        if (!pr.safe && pr.age < pr.warn + BossHazards::Press::kFall + pr.down) unsafe.push_back({pr.x - pr.half - 4.0f, pr.x + pr.half + 4.0f});
    for (const auto& cr : h.cracks())
        if (cr.age < cr.delay + cr.burst) unsafe.push_back({cr.at.x - cr.half - 4.0f, cr.at.x + cr.half + 4.0f});
    for (const auto& ru : h.runes()) unsafe.push_back({ru.at.x - ru.half - 4.0f, ru.at.x + ru.half + 4.0f});
    for (const auto& cl : h.clouds())
        if (cl.time > 0.0f && cl.area.y < body.bottom() + 4.0f && cl.area.bottom() > body.y) unsafe.push_back({cl.area.x - 4.0f, cl.area.right() + 4.0f});
    for (const auto& st : h.steams())
        if (!st.done() && st.area().intersects(wide) && st.age < st.warn + 0.05f) jump = true;
    bool missiles = false;
    for (const auto& m : h.missiles()) {
        if (m.done()) continue;
        missiles = true;
        if (m.locked()) unsafe.push_back({m.at.x - m.radius - 6.0f, m.at.x + m.radius + 6.0f});
    }
    auto blocked = [&](float x) {
        for (const auto& [a, b] : unsafe)
            if (x + body.w * 0.5f > a && x - body.w * 0.5f < b) return true;
        return false;
    };
    if (away == 0 && blocked(c.x)) {
        // The nearest clear place on her floor, inside the arena.
        for (float d = 8.0f; d < arena_.w; d += 8.0f) {
            const bool l = c.x - d > arena_.x + 16.0f && !blocked(c.x - d);
            const bool r = c.x + d < arena_.right() - 16.0f && !blocked(c.x + d);
            if (l || r) {
                away = l && r ? (home_.x <= c.x ? -1 : 1) : (l ? -1 : 1);
                break;
            }
        }
    }
    if (jump && grounded) {
        jumpHold_ = 24;
        return press(away != 0 ? away : dodgeDir_, true, true, false);
    }
    if (duck) {
        crouch_ = 30;
        return press(0, false, false, true);
    }
    if (crouch_ > 0) {
        --crouch_;
        return press(0, false, false, true);
    }
    if (away != 0) {
        dodgeDir_ = away;
        dodgeSteps_ = 10;
        return press(away, false, false, false);
    }
    if (dodgeSteps_ > 0) return press(dodgeDir_, false, false, false);
    // Under the missiles: never standing still (a reticle locks where she stands).
    float target = home_.x;
    if (missiles) {
        weave_ += kDt;
        target = home_.x + (std::fmod(weave_, 2.4f) < 1.2f ? -70.0f : 70.0f);
    } else {
        weave_ = 0.0f;
    }
    // Back to her place, then facing the boss.
    if (grounded && std::abs(c.x - target) > 6.0f && !blocked(target)) return press(target < c.x ? -1 : 1, false, false, false);
    if (const Enemy* b = boss(); b && grounded && std::abs(p.vel.x) < 1.0f) {
        const float want = b->center().x < c.x ? -1.0f : 1.0f;
        if (p.facing != want) return press(want < 0.0f ? -1 : 1, false, false, false);
    }
    return {};
}

}  // namespace cosmic
