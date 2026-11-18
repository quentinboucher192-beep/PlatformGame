#include "game/raid/GigabossRoutine.h"

#include <algorithm>
#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"

namespace cosmic::raid {

// The routines (one file each).
std::unique_ptr<GigabossRoutine> makeVoidwormRoutine();
std::unique_ptr<GigabossRoutine> makeGravitonRoutine();
std::unique_ptr<GigabossRoutine> makeTitanusRoutine();

std::unique_ptr<GigabossRoutine> GigabossRoutine::make(const std::string& name) {
    if (name == "voidworm") return makeVoidwormRoutine();
    if (name == "graviton") return makeGravitonRoutine();
    if (name == "titanus") return makeTitanusRoutine();
    return nullptr;
}

uint64_t GigabossRoutine::mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

int GigabossRoutine::steps(float seconds) { return static_cast<int>(std::lround(seconds * static_cast<float>(kSimHz))); }

float GigabossRoutine::noise(uint32_t seed) {
    uint32_t x = seed * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

std::vector<int> GigabossRoutine::playing(GameWorld& w) {
    std::vector<int> out;
    if (!w.coop()) return out;
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
        if (w.member(i).presence != GameWorld::Presence::Playing) continue;
        const Player& p = w.playerAt(i);
        if (p.alive() && !p.respawning()) out.push_back(i);
    }
    return out;
}

void GigabossRoutine::strike(GameWorld& w, Enemy& b, const Rect& area, float damage, float knockback, const std::string& name) {
    DamageInfo d;
    d.amount = damage;
    d.type = DamageType::Explosion;
    d.source = {area.center().x, area.bottom()};
    d.knockback = knockback;
    w.hurtPlayersByBoss(area, d, &b, &name);
}

void GigabossRoutine::alert(Gigaboss& g, std::string text, AlertTone tone) {
    g.alert(std::move(text), tone == Danger ? Gigaboss::AlertKind::Danger : tone == Branch ? Gigaboss::AlertKind::Branch : Gigaboss::AlertKind::Group);
}

int GigabossRoutine::phase(const Gigaboss& g) { return g.phase_; }
int GigabossRoutine::inPlay(const Gigaboss& g) { return g.inPlay_; }
int GigabossRoutine::scaledFor(const Gigaboss& g) { return g.scaledFor_; }
uint32_t GigabossRoutine::ticks(const Gigaboss& g) { return g.fightTicks_; }
bool GigabossRoutine::exposed(const Gigaboss& g) { return g.stage_ == Gigaboss::Stage::Exposed; }
bool GigabossRoutine::armoured(const Gigaboss& g) { return g.stage_ == Gigaboss::Stage::Carapace; }

void GigabossRoutine::setArmour(Gigaboss& g, bool on) {
    if (g.stage_ == Gigaboss::Stage::Fight || g.stage_ == Gigaboss::Stage::Carapace) g.stage_ = on ? Gigaboss::Stage::Carapace : Gigaboss::Stage::Fight;
}

void GigabossRoutine::setFight(Gigaboss& g) { g.stage_ = Gigaboss::Stage::Fight; }

bool GigabossRoutine::linkDone(const Gigaboss& g, const std::string& name) {
    if (!g.rules_) return false;
    const auto it = g.rules_->links.find(name);
    return it != g.rules_->links.end() && it->second >= 0 && it->second < 32 && ((g.mask_ >> it->second) & 1u) != 0;
}

void GigabossRoutine::expose(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b, const std::string& text) {
    const GigabossRules& r = *g.rules_;
    g.stage_ = Gigaboss::Stage::Exposed;
    g.exposedTicks_ = steps(r.exposed);
    g.weak_.clear();
    const float factor = 1.0f + r.weakHpPerPlayer * static_cast<float>(std::max(0, g.scaledFor_ - 1));
    for (const Vec2& s : r.weakSpots) {
        const Vec2 at{b.box.center().x + s.x * b.facing, b.box.bottom() + s.y};
        if (Enemy* e = w.spawnEnemy(r.weakEnemy, at, b.facing < 0.0f)) {
            e->summonedBy = b.uid();
            e->countsForStats = false;
            e->scaleHealth(factor);
            g.weak_.push_back(e->uid());
        }
    }
    g.weakTotal_ = static_cast<int>(g.weak_.size());
    c.setWeak(g.weak_);
    g.gluWeak(w, b);
    w.shake(0.8f);
    const Color lc = b.lightColor();
    w.flash({lc.r, lc.g, lc.b, 255}, 0.25f);
    w.particles().emit("boss_phase", b.box.center(), 2.0f);
    w.playSound("sounds/shell_shatter.wav", b.box.center(), 1.0f);
    alert(g, text, Branch);
}

bool GigabossRoutine::exposedStep(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b) {
    const GigabossRules& r = *g.rules_;
    for (size_t k = 0; k < g.weak_.size(); ++k) {
        if (g.weak_[k] == 0) continue;
        const Enemy* e = w.findEnemy(g.weak_[k]);
        if (e && e->alive()) continue;
        const int seat = c.brokeWeak(g.weak_[k]);
        if (seat >= 0) c.weakPoint(seat);
        g.weak_[k] = 0;
        b.wound(w, r.weakShare * b.maxHp());
        int broken = 0;
        for (uint32_t u : g.weak_) broken += u == 0 ? 1 : 0;
        alert(g, "GROUPE · Point faible brisé (" + std::to_string(broken) + " / " + std::to_string(g.weakTotal_) + ")", Group);
    }
    if (!b.alive()) return true;  // its fall comes with the next step (the gigaboss's own rules)
    g.gluWeak(w, b);
    if (--g.exposedTicks_ > 0) return true;
    g.endExposure(w, c);
    return false;
}

void GigabossRoutine::resetPlates(Gigaboss& g) {
    for (Gigaboss::Vent& v : g.vents_) v = Gigaboss::Vent{v.area};
}

bool GigabossRoutine::holdPlates(Gigaboss& g, GameWorld& w, Contributions& c, Enemy& b, const PlateWords& words) {
    using P = GameWorld::Presence;
    const GigabossRules& r = *g.rules_;
    const int hold = std::max(1, steps(r.hold));
    const uint32_t reopen = static_cast<uint32_t>(std::max(1, steps(r.reopen * slack(g.inPlay_))));
    for (size_t k = 0; k < g.vents_.size(); ++k) {
        Gigaboss::Vent& v = g.vents_[k];
        v.holders = 0;
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
            if (!w.coop() || w.member(i).presence != P::Playing) continue;
            const Player& p = w.playerAt(i);
            if (p.alive() && !p.respawning() && v.area.intersects(p.box)) v.holders |= static_cast<uint8_t>(1u << i);
        }
        if (v.closed) {
            if (g.fightTicks_ - v.closedAt >= reopen) {
                v.closed = false;
                v.hold = 0;
                alert(g, words.reopened, Danger);
                w.particles().emit("portal_burst", {v.area.center().x, v.area.y}, 1.0f);
            }
            continue;
        }
        if (v.holders) {
            v.hold = std::min(hold, v.hold + 1);
            if (v.hold >= hold) {
                v.closed = true;
                v.closedAt = g.fightTicks_;
                for (int i = 0; i < 8; ++i)
                    if (v.holders & (1u << i)) c.vent(i);
                w.particles().emit("impulse_ring", {v.area.center().x, v.area.y}, 1.2f);
                w.playSound("sounds/shield_block.wav", v.area.center(), 0.8f, 0.7f);
                alert(g, words.closed + " (" + std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents_.size()) + ")", Group);
            }
        } else {
            v.hold = std::max(0, v.hold - 2);
        }
        // Its jet: announced, then thrown up the column over the plate.
        if (g.jetting(static_cast<int>(k))) {
            const Rect column{v.area.x, v.area.y - r.jetHeight, v.area.w, v.area.h + r.jetHeight};
            strike(w, b, column, r.jetDamage, 260.0f, r.jetName);
            if (g.fightTicks_ % 6 == 0) w.particles().emit("acid_burst", {v.area.center().x, v.area.y}, 0.8f);
        } else if (g.jetWarning(static_cast<int>(k)) && g.fightTicks_ % 10 == 0) {
            w.particles().emit("acid_bubbles", {v.area.center().x, v.area.y}, 0.6f);
        }
    }
    return !g.vents_.empty() && g.ventsClosed() == static_cast<int>(g.vents_.size());
}

}  // namespace cosmic::raid
