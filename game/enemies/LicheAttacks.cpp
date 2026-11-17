// The spells of the lich (2.57, la Liche des Catacombes): « Runes funestes » (runes traced on the
// ground she stands on: they glow while they are traced, then are armed: one bursts into a pillar
// of soul fire when she steps on it, and every rune left bursts at the end of its time: jump over
// them, or keep off), « Maléfice » (a circle of runes closes around her: still inside when it
// closes, she is cursed: her jumps grow heavy for a while; a step out of it is enough), its altars
// (it floats over the altars of its arena between two spells and teleports from one to another,
// the next one shown before it appears there) and its phylactery (« Phylactère »: at the end of a
// phase that has one, it hides its soul in a crystal set on one of its altars: it cannot be hurt
// until the crystal is broken, then the next phase begins). Like every attack they are announced
// before they can hurt, and they are deterministic: positions come from the simulation, the only
// randomness is the world's gameplay generator. The runes live in BossHazards (as the golem's
// cracks); the crystal is an enemy of its own (data/enemies/phylactere.json).

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

constexpr float kHover = 40.0f;      // its feet float this high over the top of its altar
constexpr float kFadeOut = 0.55f;    // it fades out of an altar (the next one flares meanwhile)
constexpr float kFadeIn = 0.4f;      // ... and into the next one
constexpr float kCloseLimit = 1.6f;  // Nova right against it this long: it leaves
constexpr float kRuneGap = 72.0f;    // default gap between two runes
constexpr float kHexRadius = 72.0f;  // default circle of its curse
constexpr float kHexCurse = 0.72f;   // default: her jumps reach this share of their height
constexpr float kHexTime = 5.0f;     // default: for this long
}  // namespace

// ================================================================= choosing

float Enemy::licheFit(const GameWorld& w, const AttackDef& a, bool near, bool far, bool air) const {
    switch (a.type) {
        // Runes still armed on her floor: something else first.
        case AttackType::Glyph: return (w.hazards().runeCount(uid_) > 6 ? 0.3f : 1.2f) * (air ? 0.8f : 1.0f) * (far ? 1.1f : 1.0f);
        // Already cursed: rarely another curse.
        case AttackType::Hex: return (w.player().cursed() ? 0.25f : 1.2f) * (near ? 1.2f : 1.0f);
        default: return 1.0f;
    }
}

// ============================================================ its altars

void Enemy::findAltars(GameWorld& w) {
    if (altarsKnown_) return;
    altarsKnown_ = true;
    altars_.clear();
    const Rect arena = arenaArea(w);
    for (const auto& e : w.entities())
        if (e->type() == "altar" && arena.expanded(16.0f).contains(e->box.center())) altars_.push_back({e->box.center().x, e->box.y});
    if (altars_.empty()) {
        // No altar (a test room, the enemy editor): four spots of the floor of its arena.
        for (float f : {0.2f, 0.4f, 0.6f, 0.8f}) {
            const float x = arena.x + arena.w * f;
            float y = arena.bottom();
            if (auto g = groundAt(w, x, std::min(box.bottom(), arena.bottom() - 8.0f))) y = *g;
            altars_.push_back({x, y});
        }
    }
    std::sort(altars_.begin(), altars_.end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    if (altar_ < 0) {
        float best = 1e9f;
        for (size_t k = 0; k < altars_.size(); ++k) {
            const float d = distance(altarSpot(static_cast<int>(k)), box.bottomCenter());
            if (d < best) {
                best = d;
                altar_ = static_cast<int>(k);
            }
        }
    }
}

Vec2 Enemy::altarSpot(int k) const {
    if (k < 0 || k >= static_cast<int>(altars_.size())) return box.bottomCenter();
    return altars_[static_cast<size_t>(k)] - Vec2{0.0f, kHover};
}

int Enemy::chooseAltar(GameWorld& w) const {
    // Another altar, rather one at a fair distance from her (never the crystal's).
    struct Cand {
        int k;
        float weight;
    };
    std::vector<Cand> cands;
    float total = 0.0f;
    for (int k = 0; k < static_cast<int>(altars_.size()); ++k) {
        if (k == altar_ || k == phylacteryAltar_) continue;
        const float d = std::abs(altars_[static_cast<size_t>(k)].x - playerPos_.x);
        const float weight = d < 150.0f ? 0.35f : (d < 720.0f ? 1.6f : 0.9f);
        cands.push_back({k, weight});
        total += weight;
    }
    if (cands.empty()) return -1;
    float r = w.rng().range(0.0f, total);
    for (const Cand& c : cands) {
        r -= c.weight;
        if (r <= 0.0f) return c.k;
    }
    return cands.back().k;
}

void Enemy::beginTeleport(GameWorld& w, int k) {
    teleportDue_ = false;
    castsHere_ = 0;
    closeTime_ = 0.0f;
    altarNext_ = k;
    teleportTarget_ = altarSpot(k);
    state_ = EnemyState::TeleportOut;
    stateTimer_ = kFadeOut;
    playClip("telegraph_special", true);
    const Color soul = lightColor();
    w.particles().emit("teleport_out_enemy", box.center(), 1.6f);
    w.particles().emit("embers_rising", box.center(), 1.2f, {}, &soul);
    w.particles().emit("rune_glow", teleportTarget_ + Vec2{0.0f, kHover - 6.0f}, 2.0f, {}, &soul);
    w.playSound("sounds/wisp_teleport.wav", box.center(), 0.9f, 0.7f);
}

void Enemy::licheBlink(GameWorld& w, float dt) {
    vel = lerpv(vel, {}, damp(8.0f, dt));
    if (stateTimer_ > 0.0f) return;
    // It appears over its new altar.
    box = Rect::fromBottomCenter(teleportTarget_, box.size());
    prevBox = box;
    vel = {};
    if (altarNext_ >= 0) altar_ = altarNext_;
    altarNext_ = -1;
    state_ = EnemyState::TeleportIn;
    stateTimer_ = kFadeIn;
    playClip("summon", true);
    const Color soul = lightColor();
    w.particles().emit("teleport_in_enemy", box.center(), 1.6f);
    w.particles().emit("embers_rising", box.bottomCenter(), 1.4f, {}, &soul);
    w.playSound("sounds/teleport_in.wav", box.center(), 0.8f, 0.8f);
    face(playerPos_.x - box.center().x);
}

void Enemy::licheHover(GameWorld& w, float dt) {
    // It floats over its altar, swaying, leaning a little toward her.
    findAltars(w);
    const Vec2 spot = altarSpot(altar_);
    const float lean = std::clamp((playerPos_.x - spot.x) * 0.12f, -26.0f, 26.0f);
    const Vec2 target{spot.x + lean + 8.0f * dmath::sin(hoverPhase_ * 0.9f), spot.y - box.h * 0.5f + 5.0f * dmath::sin(hoverPhase_ * 1.7f)};
    const Vec2 d = target - box.center();
    const float len = d.length();
    const float sp = std::max(40.0f, def_.speed * phaseSpeed());
    const Vec2 want = len > 1.0f ? d / len * std::min(sp, len * 3.0f) : Vec2{};
    vel = lerpv(vel, want, damp(4.0f, dt));
    face(playerPos_.x - box.center().x);
    playClip("idle");
    // Nova right against it: after a moment it leaves for another altar.
    const bool close = std::abs(playerPos_.x - box.center().x) < box.w * 0.5f + 60.0f && playerPos_.y > box.y - 40.0f &&
                       playerPos_.y < box.bottom() + 140.0f;
    closeTime_ = close ? closeTime_ + dt : std::max(0.0f, closeTime_ - dt);
    if (closeTime_ > kCloseLimit) {
        closeTime_ = 0.0f;
        teleportDue_ = true;
    }
}

void Enemy::licheThink(GameWorld& w, float dt) {
    findAltars(w);
    if (teleportDue_ && altars_.size() > 1) {
        const int k = chooseAltar(w);
        if (k >= 0) {
            beginTeleport(w, k);
            return;
        }
    }
    teleportDue_ = false;
    if (playerVisible_ && chooseAttack(w)) return;
    licheHover(w, dt);
}

// ============================================================ its phylactery

void Enemy::beginWard(GameWorld& w, int next) {
    // « Phylactère »: it hides its soul in a crystal set on the altar of its floor farthest from
    // her; it cannot be hurt until the crystal is broken.
    if (attack_ && attack_->isGuardianType()) guardianEnd(w);
    if (attack_ && attack_->isDragonType()) dragonEnd(w);
    if (attack_ && attack_->isGolemType()) golemEnd(w);
    if (attack_ && attack_->isPhoenixType()) phoenixEnd(w);
    if (attack_ && attack_->isLicheType()) licheEnd(w);
    findAltars(w);
    float floorY = -1e9f;
    for (const Vec2& a : altars_) floorY = std::max(floorY, a.y);
    int best = -1;
    float far = -1.0f;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
        for (int k = 0; k < static_cast<int>(altars_.size()); ++k) {
            const Vec2 a = altars_[static_cast<size_t>(k)];
            if (pass == 0 && (a.y < floorY - 8.0f || k == altar_)) continue;  // its floor first, not where it floats
            const float d = std::abs(a.x - playerPos_.x);
            if (d > far) {
                far = d;
                best = k;
            }
        }
    const std::string& id = def_.phases[static_cast<size_t>(phase_)].phylactery;
    Enemy* crystal = best >= 0 ? w.spawnEnemy(id, altars_[static_cast<size_t>(best)], playerPos_.x < altars_[static_cast<size_t>(best)].x)
                               : nullptr;
    if (!crystal) {  // no crystal to hide in: the phase ends as any other
        enterPhase(w, next);
        return;
    }
    crystal->summonedBy = uid_;
    crystal->countsForStats = false;
    crystal->alert(w);
    phylactery_ = crystal->uid();
    phylacteryAltar_ = best;
    warded_ = true;
    wardNext_ = next;
    if (altar_ == best) teleportDue_ = true;
    attack_ = nullptr;
    staged_.clear();
    comboLeft_ = 0;
    burstsLeft_ = 0;
    state_ = EnemyState::PhaseTransition;
    stateTimer_ = 1.6f;
    playClip("roar", true);
    const Color soul = lightColor();
    w.playSound("sounds/boss_phase.wav", 0.9f);
    w.playSound("sounds/summon.wav", box.center(), 1.0f, 0.6f);
    w.flash({170, 240, 255, 255}, 0.25f);
    w.shake(0.6f);
    w.particles().emit("boss_phase", box.center(), 2.0f);
    w.particles().emit("rune_glow", altars_[static_cast<size_t>(best)] + Vec2{0.0f, -20.0f}, 3.0f, {}, &soul);
    WorldMessage m;
    m.style = WorldMessage::Style::Boss;
    m.text = "La Liche cache son âme dans son phylactère !";
    m.subtitle = "Brisez le phylactère";
    m.duration = 3.0f;
    w.message(m);  // then, while the crystal stands, the HUD says so (Hud::drawStatus)
    if (!objectId_.empty()) w.emit(objectId_, "bossWard");
}

void Enemy::stepWard(GameWorld& w) {
    const Enemy* crystal = w.findEnemy(phylactery_);
    if (crystal && crystal->alive()) return;
    // The crystal is broken: its soul is back, it can be hurt, the next phase begins.
    warded_ = false;
    phylactery_ = 0;
    phylacteryAltar_ = -1;
    if (state_ == EnemyState::TeleportOut || state_ == EnemyState::TeleportIn) {
        altarNext_ = -1;  // its teleport is cut short: it shows where it stands
    }
    w.flash({255, 120, 110, 255}, 0.35f);
    w.shake(0.9f);
    w.rumble(0.8f, 0.6f, 300);
    w.playSound("sounds/ice_shatter.wav", box.center(), 1.0f, 0.6f);
    enterPhase(w, wardNext_);
}

// ============================================================ the wind-up

void Enemy::planRunes(GameWorld& w) {
    // Where the runes will be traced: on the floor (or the ledge) she stands on, a row through
    // her, or (two arms) her spot and as many on each side of it; never over a pit.
    const AttackDef& a = *attack_;
    const Rect arena = arenaArea(w);
    const int n = std::clamp(a.count, 1, 12);
    const float gap = orDefault(a.spread, kRuneGap);
    const float half = orDefault(a.radius, 18.0f);
    const Vec2 aim = a.lead > 0.0f ? predictPlayer(w, std::max(0.0f, stateTimer_) * a.lead) : playerPos_;
    float surface = arena.bottom();
    if (auto g = groundAt(w, aim.x, aim.y)) surface = *g;
    std::vector<float> xs;
    if (a.arms >= 2) {
        for (int k = -n; k <= n; ++k) xs.push_back(aim.x + gap * static_cast<float>(k));
    } else {
        for (int i = 0; i < n; ++i) xs.push_back(aim.x + gap * (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f));
    }
    path_.clear();
    for (float x : xs) {
        x = std::clamp(x, arena.x + half + 8.0f, arena.right() - half - 8.0f);
        if (!path_.empty() && std::abs(x - path_.back().x) < half * 2.0f) continue;  // squeezed against a wall
        auto g = groundAt(w, x, surface);
        if (!g || std::abs(*g - surface) > 20.0f) continue;  // not her floor (a pit, another level)
        path_.push_back({x, *g});
    }
    markPos_ = aim;
}

void Enemy::licheBegin(GameWorld& w) {
    const AttackDef& a = *attack_;
    path_.clear();
    path2_.clear();
    hexLanded_ = false;
    switch (a.type) {
        case AttackType::Glyph:
            planRunes(w);
            if (a.sound.empty()) w.playSound("sounds/eclipse_ring.wav", box.center(), 0.6f, 1.2f);
            break;
        case AttackType::Hex: {
            // The circle around her, where she will be (it does not follow her afterwards).
            const Vec2 aim = a.lead > 0.0f ? predictPlayer(w, std::max(0.0f, stateTimer_) * a.lead * 0.5f) : playerPos_;
            markPos_ = aim + Vec2{0.0f, -4.0f};
            ringRadius_ = orDefault(a.radius, kHexRadius);
            if (a.sound.empty()) w.playSound("sounds/gravity_well.wav", markPos_, 0.7f, 1.4f);
            break;
        }
        default: break;
    }
}

// =========================================================== the spell

void Enemy::licheStart(GameWorld& w) {
    const AttackDef& a = *attack_;
    attackClock_ = 0.0f;
    switch (a.type) {
        case AttackType::Glyph: {
            const float arm = a.burstInterval;
            const float step = a.stagger;
            const float side = box.center().x <= markPos_.x ? 1.0f : -1.0f;  // traced from its side
            for (size_t k = 0; k < path_.size(); ++k) {
                const size_t order = side > 0.0f ? k : path_.size() - 1 - k;
                BossHazards::Rune r;
                r.owner = uid_;
                r.at = path_[k];
                r.half = orDefault(a.radius, 18.0f);
                r.height = orDefault(a.height, 56.0f);
                r.arm = arm + step * static_cast<float>(order);
                r.time = orDefault(a.duration, 4.0f);
                r.damage = a.damage;
                r.knockback = a.knockback;
                r.color = lightColor();
                r.glyph = static_cast<int>(k) * 5 + patternIndex_;
                w.hazards().addRune(r);
            }
            playClip(a.attackAnim, true);
            w.playSound("sounds/summon.wav", box.center(), 0.7f, 1.3f);
            break;
        }
        case AttackType::Hex:
            // The circle closes during the active time; the curse falls at its end (licheEnd).
            playClip(a.attackAnim, true);
            w.playSound("sounds/eclipse_ring.wav", markPos_, 0.8f, 0.8f);
            break;
        default: break;
    }
}

void Enemy::licheActive(GameWorld&, float dt) {
    if (def_.flying) vel = lerpv(vel, {}, damp(5.0f, dt));
    attackClock_ += dt;
}

void Enemy::licheEnd(GameWorld& w, bool cut) {
    // Its runes keep their own time; the curse falls only if its circle closed to its end.
    if (cut || !attack_ || attack_->type != AttackType::Hex) return;
    const AttackDef& a = *attack_;
    Player& p = w.player();
    const Color soul = lightColor();
    w.particles().emit("rune_glow", markPos_, 2.5f, {}, &soul);
    w.playSound("sounds/shadow_step.wav", markPos_, 0.9f, 0.7f);
    if (!p.alive() || p.respawning() || distance(p.center(), markPos_) > ringRadius_) return;
    hexLanded_ = true;
    p.applyCurse(orDefault(a.curse, kHexCurse), orDefault(a.duration, kHexTime));  // the HUD shows it (Hud::drawStatus)
    if (a.damage > 0.0f) {
        DamageInfo dmg;
        dmg.amount = a.damage;
        dmg.type = DamageType::Contact;
        dmg.source = markPos_ + Vec2{0.0f, -10.0f};
        dmg.knockback = a.knockback;
        w.damagePlayer(dmg);
    }
    w.particles().emit("boss_phase", p.center(), 0.6f);
    w.flash({150, 80, 230, 255}, 0.15f);
}

// ================================================================== render

void Enemy::renderAltarFx(RenderContext& ctx) const {
    // The altar it floats over (a column of soul light up to its feet), the next one it will
    // appear over (its runes flaring, a shade gathering), the thread binding it to its phylactery.
    if (!alive() || altars_.empty()) return;
    const Color soul = lightColor();
    const float s = ctx.r.view().scale / 3.0f;
    const float pulse = 0.5f + 0.5f * dmath::sin(ctx.time * 4.0f);
    if (altar_ >= 0 && state_ != EnemyState::TeleportOut) {
        const Vec2 a = altars_[static_cast<size_t>(altar_)];
        const Vec2 feet = lerpv(prevBox.bottomCenter(), box.bottomCenter(), ctx.alpha);
        if (distance(feet, a) < kHover * 3.0f) {
            ctx.r.fillWorldRect({a.x - 10.0f, feet.y, 20.0f, std::max(0.0f, a.y - feet.y)}, soul.withAlpha(static_cast<uint8_t>(30 + 30 * pulse)),
                                BlendMode::Additive);
            ctx.r.drawWorldGlow(a + Vec2{0.0f, -4.0f}, 22.0f + 6.0f * pulse, soul.withAlpha(static_cast<uint8_t>(80 + 60 * pulse)));
        }
    }
    if (state_ == EnemyState::TeleportOut && altarNext_ >= 0) {
        const float k = 1.0f - std::clamp(stateTimer_ / kFadeOut, 0.0f, 1.0f);
        const Vec2 a = altars_[static_cast<size_t>(altarNext_)];
        const Vec2 c = a - Vec2{0.0f, kHover + box.h * 0.5f};
        ctx.r.fillWorldRect({a.x - 14.0f * (0.5f + k), c.y - box.h * 0.5f, 28.0f * (0.5f + k), a.y - (c.y - box.h * 0.5f)},
                            soul.withAlpha(static_cast<uint8_t>(30 + 90 * k)), BlendMode::Additive);
        ctx.r.drawWorldGlow(c, box.w * (0.6f + 0.6f * k), soul.withAlpha(static_cast<uint8_t>(60 + 120 * k)));
        ctx.r.drawWorldCircle(a + Vec2{0.0f, -6.0f}, 18.0f + 30.0f * (1.0f - k), soul.withAlpha(static_cast<uint8_t>(120 + 120 * k)),
                              std::max(1.0f, 2.0f * s));
        ctx.lights.add(c, 120.0f * k + 40.0f, soul);
    }
    if (warded_ && phylacteryAltar_ >= 0) {
        // A thread of its soul from it to the crystal, beads of light running along it.
        const Vec2 from = lerpv(prevBox.center(), box.center(), ctx.alpha);
        const Vec2 to = altars_[static_cast<size_t>(phylacteryAltar_)] + Vec2{0.0f, -26.0f};
        const Color thread{170, 240, 255, static_cast<uint8_t>(70 + 50 * pulse)};
        ctx.r.drawWorldLine(from, to, thread, std::max(1.0f, 1.5f * s), BlendMode::Additive);
        for (int i = 0; i < 5; ++i) {
            const float ph = std::fmod(ctx.time * 0.6f + static_cast<float>(i) * 0.2f, 1.0f);
            ctx.r.drawWorldGlow(lerpv(from, to, ph), 4.0f, Color{220, 250, 255, static_cast<uint8_t>(200 * (1.0f - ph))});
        }
    }
}

void Enemy::renderLiche(RenderContext& ctx, float prog) const {
    const AttackDef& a = *attack_;
    const bool tele = state_ == EnemyState::Telegraph;
    const float s = ctx.r.view().scale / 3.0f;
    const Color soul = lightColor();
    switch (a.type) {
        case AttackType::Glyph:
            if (!tele) break;
            for (size_t k = 0; k < path_.size(); ++k) {
                // Where each rune will be traced: a faint ellipse on the ground, drawn as it comes.
                const Vec2 at = path_[k];
                const float h = orDefault(a.radius, 18.0f);
                const float ry = std::max(3.0f, h * 0.3f);
                const Vec2 o = at + Vec2{0.0f, -ry * 0.6f};
                const int seg = 16;
                const int shown = std::max(1, static_cast<int>(static_cast<float>(seg) * prog));
                const Color c = soul.withAlpha(static_cast<uint8_t>(60 + 110 * prog));
                for (int i = 0; i < shown; ++i) {
                    const float a0 = 6.2831853f * static_cast<float>(i) / seg, a1 = 6.2831853f * static_cast<float>(i + 1) / seg;
                    ctx.r.drawWorldLine(o + Vec2{dmath::cos(a0) * h, dmath::sin(a0) * ry}, o + Vec2{dmath::cos(a1) * h, dmath::sin(a1) * ry}, c,
                                        std::max(1.0f, 1.5f * s), BlendMode::Additive);
                }
                ctx.r.drawWorldGlow(o, h * (0.5f + 0.5f * prog), soul.withAlpha(static_cast<uint8_t>(20 + 60 * prog)));
            }
            break;
        case AttackType::Hex: {
            // The circle of the curse around her: runes turning on it, a second ring closing onto
            // it; while it closes (the active time) chains reach for its middle.
            const float closing = tele ? 0.0f : std::clamp(attackClock_ / std::max(0.05f, a.active), 0.0f, 1.0f);
            const float r = ringRadius_;
            const Color hex{200, 130, 255, static_cast<uint8_t>(110 + 120 * std::max(prog * 0.7f, closing))};
            ctx.r.drawWorldCircle(markPos_, r, hex, std::max(1.0f, (1.5f + 1.5f * closing) * s));
            ctx.r.drawWorldGlow(markPos_, r * 0.9f, Color{140, 70, 220, static_cast<uint8_t>(30 + 50 * prog + 60 * closing)});
            const float outer = r * (1.6f - 0.6f * (tele ? prog : 1.0f)) * (1.0f - 0.25f * closing);
            ctx.r.drawWorldCircle(markPos_, outer, soul.withAlpha(static_cast<uint8_t>(60 + 100 * prog)), std::max(1.0f, s));
            for (int k = 0; k < 8; ++k) {
                const float ang = ctx.time * 1.4f + static_cast<float>(k) * 0.7853982f;
                const Vec2 p = markPos_ + Vec2{dmath::cos(ang), dmath::sin(ang)} * r;
                ctx.r.fillWorldRect(Rect::fromCenter(p, {4.0f, 4.0f}), Color{235, 200, 255, hex.a}, BlendMode::Additive);
                if (closing > 0.0f) {
                    const Vec2 in = lerpv(p, markPos_, 0.85f * closing);
                    ctx.r.drawWorldLine(p, in, Color{210, 150, 255, static_cast<uint8_t>(200 * closing)}, std::max(1.0f, 1.5f * s),
                                        BlendMode::Additive);
                }
            }
            ctx.lights.add(markPos_, r * 1.2f, Color{170, 90, 255, 255});
            break;
        }
        default: break;
    }
}

}  // namespace cosmic
