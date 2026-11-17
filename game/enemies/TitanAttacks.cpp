// The war machine (2.58, le Titan Mécanique): its machinery, its heat and its stall.
//
//  * « Presses hydrauliques » (press): the presses of its foundry (level objects « press », their
//    housings in the roof) come down onto the surface under them. Every press shows where it will
//    stamp before it falls (stripes on the ground, a shudder, a hiss), then falls, stays down a
//    moment and rises. Patterns: every other press (`arms` 0/1, `count` rounds, the other half
//    each round: step into a column that has just risen), all but one (`arms` 2: the column left
//    is lit green, never far from Nova) or a march (`arms` 3: one after the other from its side
//    across the arena: step back into a column that has risen). A press never falls on the Titan.
//  * « Missiles à verrouillage » (missile): reticles appear on Nova one after another and follow
//    her (slower than she runs), then lock (red, a beep); a missile falls on each locked spot a
//    moment later. Moving is enough; standing still is punished.
//  * « Tapis roulants » (belt): the conveyor belts of its foundry (level objects « conveyor ») start
//    running, toward it (`arms` 0) or away from it (`arms` 1); their arrows flash before. They
//    carry Nova along while she stands on them, always slower than she runs; a jump frees her.
//
// Its heat (« Surchauffe », BossPhase::heat): each attack it ends heats it; its gauge full, it
// vents its steam: low jets along the ground on both sides (jump them, or keep away), then its
// core is laid bare (it takes kVentVulnerability times the damage) until its gauge has emptied.
// Its stall (« Calage », BossPhase::stall): at the end of a phase that has one, the machine
// stalls: it cannot be hurt, and its pressure valves (enemies, data/enemies/vanne.json) appear on
// the valve spots of its arena (level objects « valve_spot »). Every valve turned (broken) before
// it restarts, its boiler bursts (a heavy blow); otherwise it restarts with a pressure wave along
// the floor (jump it). Then the next phase begins.
//
// It walks with the golem's brain of a walker (GolemAttacks.cpp: its steps, its firm floor).
// Like every attack they are announced before they can hurt, and they are deterministic:
// positions come from the simulation, the only randomness is the world's gameplay generator. The
// presses, the missiles, the belts and the jets live in BossHazards.

#include <algorithm>
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

constexpr float kVentTime = 3.6f;          // its vents open this long
constexpr float kVentJets = 1.35f;         // its jets of steam (hiss, then blast) at their start
constexpr float kVentVulnerability = 1.8f; // its bare core takes this much more damage
constexpr float kStallTime = 14.0f;        // stalled: it restarts after this long
constexpr float kBurstShare = 0.06f;       // its boiler bursting: this share of its health
constexpr float kPressHalf = 64.0f;        // the presses of a room without them: half their width
constexpr float kPressGap = 1.6f;          // between two rounds of presses (every other one)
constexpr float kMarchGap = 0.75f;         // between two presses of a march
constexpr float kBeltSpeed = 90.0f;        // its belts (Nova runs at 150 px/s)

/// Hazard stripes (yellow and black) along a strip on the ground, `a` their opacity.
void stripes(Renderer& r, float x0, float x1, float y, Color hot, uint8_t a) {
    const float h = 5.0f;
    r.fillWorldRect({x0, y - h, x1 - x0, h}, Color{20, 18, 16, static_cast<uint8_t>(a * 0.8f)});
    for (float x = x0; x < x1; x += 10.0f) {
        const float w = std::min(5.0f, x1 - x);
        r.fillWorldRect({x, y - h, w, h}, hot.withAlpha(a));
    }
}

/// A chevron (an arrow head) at `at`, pointing to `dir` (+1 right, -1 left).
void chevron(Renderer& r, Vec2 at, float dir, float size, Color c, float px) {
    r.drawWorldLine(at + Vec2{-dir * size * 0.5f, -size * 0.5f}, at + Vec2{dir * size * 0.5f, 0.0f}, c, px, BlendMode::Additive);
    r.drawWorldLine(at + Vec2{-dir * size * 0.5f, size * 0.5f}, at + Vec2{dir * size * 0.5f, 0.0f}, c, px, BlendMode::Additive);
}
}  // namespace

// ================================================================== its arena

void Enemy::findMachinery(GameWorld& w) {
    if (machineryKnown_) return;
    machineryKnown_ = true;
    pressSpots_.clear();
    beltStrips_.clear();
    valveSpots_.clear();
    const Rect arena = arenaArea(w);
    const Rect near = arena.expanded(16.0f);
    for (const auto& e : w.entities()) {
        if (!near.intersects(e->box)) continue;
        if (e->type() == "press") {
            PressSpot s;
            s.x = e->box.center().x;
            s.half = e->box.w * 0.5f;
            s.top = e->box.bottom();
            s.floor = arena.bottom();
            Vec2 from{s.x, s.top + 2.0f}, to{s.x, arena.bottom() + 200.0f};
            if (auto t = w.physics().raycast(from, to, true)) s.floor = from.y + (to.y - from.y) * *t;
            pressSpots_.push_back(s);
        } else if (e->type() == "conveyor") {
            beltStrips_.push_back(e->box);
        } else if (e->type() == "valve_spot") {
            valveSpots_.push_back(e->box.bottomCenter());
        }
    }
    const float floorY = groundAt(w, box.center().x, box.bottom() - 8.0f).value_or(box.bottom());
    if (pressSpots_.empty()) {
        // No press (a test room, the enemy editor): presses side by side across its arena, hung
        // from its roof.
        for (float x = arena.x + kPressHalf + 16.0f; x + kPressHalf <= arena.right() - 16.0f; x += kPressHalf * 2.0f) {
            PressSpot s;
            s.x = x;
            s.half = kPressHalf;
            s.top = arena.y + 24.0f;
            s.floor = floorY;
            Vec2 from{x, s.top + 2.0f}, to{x, arena.bottom() + 200.0f};
            if (auto t = w.physics().raycast(from, to, true)) s.floor = from.y + (to.y - from.y) * *t;
            pressSpots_.push_back(s);
        }
    }
    if (beltStrips_.empty()) beltStrips_.push_back({arena.x + 16.0f, floorY, arena.w - 32.0f, 8.0f});  // its whole floor
    if (valveSpots_.empty()) {
        for (float f : {0.2f, 0.5f, 0.8f}) {
            const float x = arena.x + arena.w * f;
            valveSpots_.push_back({x, groundAt(w, x, floorY - 8.0f).value_or(floorY)});
        }
    }
    std::sort(pressSpots_.begin(), pressSpots_.end(), [](const PressSpot& a, const PressSpot& b) { return a.x < b.x; });
    std::sort(beltStrips_.begin(), beltStrips_.end(), [](const Rect& a, const Rect& b) { return a.x < b.x; });
    std::sort(valveSpots_.begin(), valveSpots_.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
}

// ================================================================= choosing

float Enemy::titanFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    // What it does depends on where she is: perched over its floor, far away, sheltered under a
    // ledge, on a belt.
    const float above = box.bottom() - (playerPos_.y + 12.0f);
    const bool perched = above > 48.0f;
    const bool hfar = std::abs(playerPos_.x - box.center().x) > 420.0f;
    const BossHazards& hz = w.hazards();
    switch (a.type) {
        case AttackType::Press:
            if (hz.pressCount(uid_) > 0) return 0.1f;
            return (near ? 0.8f : 1.3f) * (sheltered_ ? 0.6f : 1.0f);
        case AttackType::Missile:
            if (hz.missileCount(uid_) > 0) return 0.2f;
            return (far ? 1.4f : 1.0f) * (perched ? 1.6f : 1.0f) * (air ? 1.2f : 1.0f);
        case AttackType::Belt: {
            if (beltStrips_.empty() || hz.beltsRunning(uid_)) return 0.05f;
            return (perched ? 0.2f : 1.0f) * (hfar ? 1.5f : 0.9f);
        }
        case AttackType::Slam: return (perched ? 0.3f : 1.0f) * (hfar ? 0.4f : (near ? 1.6f : 1.0f));
        case AttackType::Laser:
        case AttackType::Arc:
        case AttackType::Prism:
        case AttackType::Rain:
        case AttackType::Eruption: return perched ? 1.5f : 1.0f;
        default: return 1.0f;
    }
}

// ============================================================== its brain

void Enemy::titanThink(GameWorld& w, float dt) {
    // A walker (the golem's steps): a beat of steps between two blows, none in a combo.
    findMachinery(w);
    const float want = golemSpot(w, dt);
    const float gap = std::abs(want - box.center().x);
    float beat = 0.3f * phaseCooldown();
    if (gap > 90.0f) beat += std::min(0.4f, gap / 350.0f);
    if (comboLeft_ > 0 || edgeTime_ > 0.5f) beat = 0.0f;
    walkTime_ += dt;
    if (playerVisible_ && walkTime_ >= beat && chooseAttack(w)) return;
    golemWalk(dt, want, 1.0f);
    playClip(std::abs(vel.x) > 5.0f ? "walk" : "idle");
}

void Enemy::titanAfterAttack(GameWorld& w) {
    // Its heat: each attack it ends fills the gauge of its phase; full (its combo over), it vents.
    walkTime_ = 0.0f;
    if (!def_.isBoss || phase_ >= static_cast<int>(def_.phases.size()) || lastStand_) return;
    const float h = def_.phases[static_cast<size_t>(phase_)].heat;
    if (h <= 0.0f) return;
    heat_ = std::min(1.0f, heat_ + h);
    if (heat_ >= 0.999f && comboLeft_ <= 0) beginVent(w);
}

// ================================================================ its vents

bool Enemy::coreExposed() const { return venting(); }

void Enemy::beginVent(GameWorld& w) {
    titan_ = TitanStage::Vent;
    titanTimer_ = kVentTime;
    ++vents_;
    heat_ = 1.0f;
    state_ = EnemyState::Recover;
    stateTimer_ = kVentTime + 0.05f;
    vel.x = 0.0f;
    playClip("telegraph_special", true);  // its reactor blazes, its vents open
    // Low jets of steam along the ground on both sides of it.
    const float floorY = box.bottom();
    for (float dir : {-1.0f, 1.0f}) {
        BossHazards::Steam s;
        s.owner = uid_;
        s.from = {dir < 0.0f ? box.x + 8.0f : box.right() - 8.0f, floorY};
        s.dir = dir;
        s.length = 150.0f;
        s.height = 30.0f;
        s.warn = 0.35f;
        s.time = kVentJets - s.warn;
        s.damage = 1.0f;
        s.knockback = 260.0f;
        w.hazards().addSteam(s);
    }
    w.playSound("sounds/mech_alarm.wav", box.center(), 0.9f, 0.8f);
    w.playSound("sounds/lava_rise.wav", box.center(), 0.8f, 1.5f);
    w.shake(0.25f);
    if (!objectId_.empty()) w.emit(objectId_, "bossVent");
}

void Enemy::stepVent(GameWorld& w, float dt) {
    vel.x = approach(vel.x, 0.0f, 900.0f * dt);
    titanTimer_ -= dt;
    heat_ = std::clamp(titanTimer_ / kVentTime, 0.0f, 1.0f);
    face(playerPos_.x - box.center().x);
    // Display only: steam out of its stacks and its open chest (the effects' own generator).
    hopTimer_ -= dt;
    if (hopTimer_ <= 0.0f) {
        hopTimer_ = 0.08f;
        w.particles().emit("steam_puff", box.center() + Vec2{-facing * box.w * 0.3f, -box.h * 0.62f}, 1.0f);
        w.particles().emit("steam_puff", box.center() + Vec2{facing * box.w * 0.2f, -box.h * 0.15f}, 0.6f);
    }
    if (titanTimer_ > 0.0f) return;
    titan_ = TitanStage::None;
    heat_ = 0.0f;
    state_ = EnemyState::Chase;
    walkTime_ = 0.0f;
    w.playSound("sounds/door_open.wav", box.center(), 0.7f, 0.6f);  // its plates close again
}

// ================================================================ its stall

int Enemy::valvesLeft() const { return valvesLeft_; }

void Enemy::clearValves(GameWorld& w) {
    for (uint32_t id : valves_)
        if (Enemy* v = w.findEnemy(id); v && v->alive()) {
            v->vanishing = true;
            v->despawn();
        }
    valves_.clear();
    valvesLeft_ = 0;
}

void Enemy::beginStall(GameWorld& w, int next) {
    // « Calage »: the machine stalls; its pressure valves appear on the valve spots of its arena.
    if (attack_ && attack_->isGuardianType()) guardianEnd(w);
    if (attack_ && attack_->isTitanType()) titanEnd(w);
    findMachinery(w);
    clearValves(w);
    const std::string& id = def_.phases[static_cast<size_t>(phase_)].stall;
    for (const Vec2& s : valveSpots_)
        if (Enemy* v = w.spawnEnemy(id, s, s.x > box.center().x)) {
            v->summonedBy = uid_;
            v->countsForStats = false;
            v->alert(w);
            valves_.push_back(v->uid());
        }
    if (valves_.empty()) {  // no valve to turn: the phase ends as any other
        enterPhase(w, next);
        return;
    }
    valvesTotal_ = valvesLeft_ = static_cast<int>(valves_.size());
    titan_ = TitanStage::Stall;
    titanTimer_ = kStallTime;
    stallNext_ = next;
    boilerBurst_ = false;
    heat_ = 0.0f;
    attack_ = nullptr;
    staged_.clear();
    comboLeft_ = 0;
    burstsLeft_ = 0;
    state_ = EnemyState::PhaseTransition;
    stateTimer_ = kStallTime + 1.0f;
    vel.x = 0.0f;
    playClip("hurt", true);  // it lurches, its lights die
    w.playSound("sounds/boss_phase.wav", 0.9f);
    w.playSound("sounds/mech_alarm.wav", box.center(), 1.0f, 0.6f);
    w.shake(0.6f);
    w.flash({255, 200, 120, 255}, 0.2f);
    for (const Vec2& s : valveSpots_) w.particles().emit("steam_burst", s + Vec2{0.0f, -12.0f}, 1.2f);
    WorldMessage m;
    m.style = WorldMessage::Style::Boss;
    m.text = "Le Titan cale !";
    m.subtitle = "Tournez les vannes";
    m.duration = 3.0f;
    w.message(m);  // then, while it is stalled, the HUD says what is left (Hud::drawStatus)
    if (!objectId_.empty()) w.emit(objectId_, "bossStall");
}

void Enemy::stepStall(GameWorld& w, float dt) {
    vel.x = approach(vel.x, 0.0f, 900.0f * dt);
    titanTimer_ -= dt;
    int left = 0;
    for (uint32_t id : valves_)
        if (const Enemy* v = w.findEnemy(id); v && v->alive()) ++left;
    if (left < valvesLeft_) {
        // A valve turned: a jet of steam into its boiler.
        w.playSound("sounds/lava_rise.wav", box.center(), 0.8f, 1.3f);
        w.particles().emit("steam_burst", box.center() + Vec2{0.0f, -box.h * 0.2f}, 1.0f);
        w.shake(0.2f);
    }
    valvesLeft_ = left;
    // Display only: the starter cranks, faster as it is about to restart.
    hopTimer_ -= dt;
    if (hopTimer_ <= 0.0f) {
        hopTimer_ = titanTimer_ < 3.0f ? 0.25f : 0.6f;
        w.playSound("sounds/lever.wav", box.center(), 0.5f, titanTimer_ < 3.0f ? 1.2f : 0.8f);
        w.particles().emit("steam_puff", box.center() + Vec2{-facing * box.w * 0.3f, -box.h * 0.62f}, 0.5f);
    }
    if (left == 0) endStall(w, true);
    else if (titanTimer_ <= 0.0f) endStall(w, false);
}

void Enemy::endStall(GameWorld& w, bool burst) {
    titan_ = TitanStage::None;
    boilerBurst_ = burst;
    clearValves(w);
    const Vec2 base = box.bottomCenter();
    if (burst) {
        // Every valve turned: its boiler bursts (never past the phase after the next one).
        float dmg = maxHp_ * kBurstShare;
        if (stallNext_ + 1 < static_cast<int>(def_.phases.size()))
            dmg = std::min(dmg, hp_ - (def_.phases[static_cast<size_t>(stallNext_ + 1)].hpThreshold + 0.01f) * maxHp_);
        if (dmg > 0.0f) hp_ = std::max(1.0f, hp_ - dmg);
        hurtFlash_ = 0.3f;
        w.particles().emit("explosion", box.center() + Vec2{0.0f, -box.h * 0.15f}, 2.5f);
        w.particles().emit("steam_burst", box.center(), 2.0f);
        w.playSound("sounds/bomb_explode.wav", box.center(), 1.0f, 0.7f);
        w.flash({255, 220, 160, 255}, 0.35f);
        w.shake(1.0f);
        w.rumble(0.9f, 0.7f, 400);
        WorldMessage m;
        m.text = "Surpression : sa chaudière explose !";
        m.duration = 2.6f;
        w.message(m);
    } else {
        // It restarts on its own: a pressure wave runs along the floor both ways (jump it).
        w.shake(0.8f);
        w.rumble(0.7f, 0.5f, 300);
        w.playSound("sounds/golem_slam.wav", base, 1.0f, 0.8f);
        w.particles().emit("steam_burst", base, 2.0f);
        for (float dir : {-1.0f, 1.0f}) {
            Projectile p;
            p.pos = p.prevPos = base + Vec2{dir * box.w * 0.5f, -6.0f};
            p.vel = {dir * 340.0f, 0.0f};
            p.damage = 2.0f;
            p.type = DamageType::Explosion;
            p.clip = "shockwave";
            p.radius = 9.0f;
            p.life = 1100.0f / 340.0f;
            p.light = lightColor();
            p.knockback = 300.0f;
            p.hitEffect = "slam_dust";
            w.spawnProjectile(p);
        }
        WorldMessage m;
        m.text = "Le Titan redémarre : sautez l'onde !";
        m.duration = 2.4f;
        w.message(m);
    }
    enterPhase(w, stallNext_);
}

// ============================================================ the wind-up

void Enemy::planPresses(GameWorld& w) {
    // Which presses fall, round by round (indices in pressSpots_; never one over the Titan).
    const AttackDef& a = *attack_;
    pressRounds_.clear();
    path_.clear();
    const int n = static_cast<int>(pressSpots_.size());
    if (n == 0) return;
    auto overMe = [&](int k) {
        const PressSpot& s = pressSpots_[static_cast<size_t>(k)];
        return s.x + s.half > box.x + 6.0f && s.x - s.half < box.right() - 6.0f;
    };
    auto nearest = [&](float x) {
        int best = 0;
        for (int k = 1; k < n; ++k)
            if (std::abs(pressSpots_[static_cast<size_t>(k)].x - x) < std::abs(pressSpots_[static_cast<size_t>(best)].x - x)) best = k;
        return best;
    };
    const int her = nearest(playerPos_.x);
    if (a.arms == 2) {
        // All but one: the column spared is lit green, close enough to reach (2 to 3 columns away
        // at most, never her own).
        std::vector<int> cands;
        for (int k = 0; k < n; ++k)
            if (k != her && std::abs(k - her) <= 2 && !overMe(k)) cands.push_back(k);
        if (cands.empty())
            for (int k = 0; k < n; ++k)
                if (k != her) cands.push_back(k);
        const int safe = cands.empty() ? -1 : cands[static_cast<size_t>(w.rng().rangeInt(0, static_cast<int>(cands.size()) - 1))];
        std::vector<int> round;
        for (int k = 0; k < n; ++k)
            if (k != safe && !overMe(k)) round.push_back(k);
        round.push_back(-1 - std::max(0, safe));  // the spared one, encoded (-1 - index)
        pressRounds_.push_back(round);
    } else if (a.arms == 3) {
        // A march from its side across the arena: one round, in order.
        std::vector<int> order;
        for (int k = 0; k < n; ++k)
            if (!overMe(k)) order.push_back(k);
        const float from = box.center().x;
        std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
            return std::abs(pressSpots_[static_cast<size_t>(x)].x - from) < std::abs(pressSpots_[static_cast<size_t>(y)].x - from);
        });
        pressRounds_.push_back(order);
    } else {
        // Every other one, the half over her first, then the other half (`count` rounds).
        const int rounds = std::clamp(a.count, 1, 4);
        for (int r = 0; r < rounds; ++r) {
            std::vector<int> round;
            for (int k = 0; k < n; ++k)
                if ((k % 2) == ((her + r) % 2) && !overMe(k)) round.push_back(k);
            pressRounds_.push_back(round);
        }
    }
    for (int k : pressRounds_.front())
        if (k >= 0) path_.push_back({pressSpots_[static_cast<size_t>(k)].x, pressSpots_[static_cast<size_t>(k)].floor});
}

void Enemy::titanBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    findMachinery(w);
    path_.clear();
    switch (a.type) {
        case AttackType::Press:
            planPresses(w);
            if (a.sound.empty()) w.playSound("sounds/mech_alarm.wav", box.center(), 0.6f, 1.1f);
            break;
        case AttackType::Missile:
            markPos_ = playerPos_;
            if (a.sound.empty()) w.playSound("sounds/laser_charge.wav", box.center(), 0.7f, 0.7f);
            break;
        case AttackType::Belt:
            if (a.sound.empty()) w.playSound("sounds/mech_alarm.wav", box.center(), 0.5f, 0.7f);
            break;
        default: break;
    }
}

// =========================================================== the attack

void Enemy::titanStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    playClip(a.attackAnim, true);
    switch (a.type) {
        case AttackType::Press: {
            // Each press of each round shows where it will stamp, then falls.
            const float warn = a.burstInterval;
            const float down = orDefault(a.duration, 0.6f);
            const bool march = a.arms == 3;
            const float gap = orDefault(a.stagger, march ? kMarchGap : kPressGap);
            for (size_t r = 0; r < pressRounds_.size(); ++r) {
                const auto& round = pressRounds_[r];
                for (size_t i = 0; i < round.size(); ++i) {
                    const int code = round[i];
                    const int k = code >= 0 ? code : -1 - code;
                    if (k < 0 || k >= static_cast<int>(pressSpots_.size())) continue;
                    const PressSpot& s = pressSpots_[static_cast<size_t>(k)];
                    BossHazards::Press p;
                    p.owner = uid_;
                    p.x = s.x;
                    p.half = orDefault(a.radius, s.half);
                    p.top = s.top;
                    p.floor = s.floor;
                    p.warn = warn + gap * static_cast<float>(march ? i : r);
                    p.down = down;
                    p.damage = a.damage;
                    p.knockback = a.knockback;
                    p.safe = code < 0;
                    p.color = lightColor();
                    w.hazards().addPress(p);
                }
            }
            w.playSound("sounds/lava_rise.wav", box.center(), 0.7f, 1.4f);
            break;
        }
        case AttackType::Missile: {
            // Its back pod opens: one reticle after another on her.
            const int n = std::clamp(a.count, 1, 12);
            const Vec2 pod = box.center() + Vec2{-facing * box.w * 0.35f, -box.h * 0.5f};
            for (int i = 0; i < n; ++i) {
                BossHazards::Missile m;
                m.owner = uid_;
                m.at = playerPos_ + Vec2{static_cast<float>((i % 3) - 1) * 14.0f, 0.0f};
                m.from = pod;
                m.track = a.burstInterval + orDefault(a.stagger, 0.3f) * static_cast<float>(i);
                m.flight = orDefault(a.duration, 0.8f);
                m.radius = orDefault(a.radius, 34.0f);
                m.damage = a.damage;
                m.knockback = a.knockback;
                m.index = i;
                m.color = lightColor();
                w.hazards().addMissile(m);
            }
            w.particles().emit("steam_puff", pod, 1.0f);
            w.playSound("sounds/bomb_drop.wav", pod, 0.7f, 0.8f);
            break;
        }
        case AttackType::Belt: {
            // The belts start: toward it (arms 0) or away from it (arms 1), each from its side.
            const float speed = std::min(orDefault(a.pull, kBeltSpeed), 130.0f);
            for (const Rect& s : beltStrips_) {
                float toward = signf(box.center().x - s.center().x);
                if (s.x <= box.center().x && box.center().x <= s.right()) toward = signf(box.center().x - playerPos_.x);
                if (toward == 0.0f) toward = facing;
                BossHazards::Belt b;
                b.owner = uid_;
                b.strip = s;
                b.speed = speed * (a.arms == 1 ? -toward : toward);
                b.warn = a.burstInterval;
                b.time = orDefault(a.duration, 5.0f);
                b.color = lightColor();
                w.hazards().addBelt(b);
            }
            w.playSound("sounds/lever.wav", box.center(), 0.9f, 0.7f);
            break;
        }
        default: break;
    }
}

void Enemy::titanActive(GameWorld&, float dt) {
    vel.x = approach(vel.x, 0.0f, 900.0f * dt);  // it stands while its machinery works
    attackClock_ += dt;
}

void Enemy::titanEnd(GameWorld&, bool) {
    // Its presses, its missiles and its belts keep their own time.
}

// ================================================================== render

void Enemy::renderTitanFx(RenderContext& ctx) const {
    // Display only: its reactor glows with its heat; venting, its core blazes; stalled, a ring
    // shows the time before it restarts and threads of steam run from its valves to it.
    if (!alive()) return;
    const float s = ctx.r.view().scale / 3.0f;
    const Vec2 c = lerpv(prevBox.center(), box.center(), ctx.alpha);
    const Vec2 core = c + Vec2{facing * box.w * 0.18f, -box.h * 0.2f};
    const Color hot = lightColor();
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * (venting() ? 12.0f : 4.0f + 6.0f * heat_));
    if (stalled()) {
        const float k = std::clamp(1.0f - titanTimer_ / kStallTime, 0.0f, 1.0f);
        const int seg = 32;
        const int shown = std::max(1, static_cast<int>(seg * k));
        for (int i = 0; i < shown; ++i) {
            const float a0 = -1.5707963f + 6.2831853f * static_cast<float>(i) / seg;
            const float a1 = -1.5707963f + 6.2831853f * static_cast<float>(i + 1) / seg;
            ctx.r.drawWorldLine(core + Vec2{dmath::cos(a0), dmath::sin(a0)} * 26.0f, core + Vec2{dmath::cos(a1), dmath::sin(a1)} * 26.0f,
                                Color{255, 90, 60, static_cast<uint8_t>(140 + 100 * pulse)}, std::max(1.0f, 2.0f * s), BlendMode::Additive);
        }
        ctx.r.drawWorldGlow(core, 10.0f, Color{120, 60, 40, 120});
        return;
    }
    if (venting()) {
        ctx.r.drawWorldGlow(core, 26.0f + 8.0f * pulse, hot.withAlpha(static_cast<uint8_t>(150 + 90 * pulse)));
        ctx.r.drawWorldGlow(core, 10.0f + 4.0f * pulse, Color{255, 255, 255, 230});
        ctx.r.drawWorldCircle(core, 18.0f + 6.0f * pulse, hot.withAlpha(200), std::max(1.0f, 1.5f * s));
        ctx.lights.add(core, 140.0f, hot);
        return;
    }
    if (heat_ > 0.0f) {
        ctx.r.drawWorldGlow(core, 8.0f + 16.0f * heat_, hot.withAlpha(static_cast<uint8_t>(40 + 150 * heat_ * (0.6f + 0.4f * pulse))));
        ctx.lights.add(core, 50.0f + 80.0f * heat_, hot);
    }
}

void Enemy::renderTitan(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;
    const Color hot = lightColor();
    switch (a.type) {
        case AttackType::Press:
            // The first presses to fall: their stripes fade in on the ground during the wind-up.
            if (!tele) break;
            for (const Vec2& p : path_) {
                float half = kPressHalf;
                for (const PressSpot& sp : pressSpots_)
                    if (std::abs(sp.x - p.x) < 1.0f) half = orDefault(a.radius, sp.half);
                stripes(ctx.r, p.x - half + 4.0f, p.x + half - 4.0f, p.y, Color{255, 200, 40, 255}, static_cast<uint8_t>(40 + 120 * prog));
            }
            break;
        case AttackType::Missile:
            if (!tele) break;
            // Its targeting laser sweeps toward her from its back pod.
            {
                const Vec2 pod = box.center() + Vec2{-facing * box.w * 0.35f, -box.h * 0.5f};
                ctx.r.drawWorldLine(pod, lerpv(pod, playerPos_, prog), Color{255, 60, 60, static_cast<uint8_t>(60 + 120 * prog)},
                                    std::max(1.0f, s), BlendMode::Additive);
                ctx.r.drawWorldCircle(playerPos_, 40.0f - 20.0f * prog, Color{255, 80, 60, static_cast<uint8_t>(80 + 120 * prog)}, std::max(1.0f, 1.5f * s));
            }
            break;
        case AttackType::Belt:
            if (!tele) break;
            for (const Rect& st : beltStrips_) {
                float toward = signf(box.center().x - st.center().x);
                if (st.x <= box.center().x && box.center().x <= st.right()) toward = signf(box.center().x - playerPos_.x);
                if (toward == 0.0f) toward = facing;
                const float dir = a.arms == 1 ? -toward : toward;
                for (float x = st.x + 24.0f; x < st.right() - 12.0f; x += 64.0f)
                    chevron(ctx.r, {x, st.y - 10.0f}, dir, 10.0f, hot.withAlpha(static_cast<uint8_t>(60 + 140 * prog)), std::max(1.0f, 2.0f * s));
            }
            break;
        default: break;
    }
}

}  // namespace cosmic
