#include "game/raid/Gigaboss.h"

#include <algorithm>
#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/BossTraining.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"

namespace cosmic::raid {

namespace {
uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}
int steps(float seconds) { return static_cast<int>(std::lround(seconds * static_cast<float>(kSimHz))); }
}  // namespace

float Gigaboss::healthFactor(const GigabossRules& r, int players) {
    return 1.0f + r.hpPerPlayer * static_cast<float>(std::clamp(players, 1, 8) - 1);
}

void Gigaboss::begin(GameWorld& w, const GigabossRules& rules, bool fallen, Contributions& c) {
    const int testPlayers = testPlayers_, testPhase = testPhase_;  // kept from one zone to the next
    *this = Gigaboss{};
    testPlayers_ = testPlayers;
    testPhase_ = testPhase;
    if (!rules.present) return;
    rules_ = &rules;
    for (const Rect& r : rules.vents) vents_.push_back(Vent{r});
    routine_ = GigabossRoutine::make(rules.routine);  // 2.69 (phase 4B): Voidworm, Graviton, Titanus
    if (routine_) routine_->begin(w, rules);
    stage_ = Stage::Waiting;
    fallen_ = fallen;  // the boss itself appears with the world's first step (its spawn is pending until then)
    (void)w;
    (void)c;
}

Enemy* Gigaboss::find(GameWorld& w) {
    if (!rules_) return nullptr;
    if (uid_ != 0) return w.findEnemy(uid_);
    for (auto& e : w.enemies())
        if (e->objectId() == rules_->boss) {
            uid_ = e->uid();
            return e.get();
        }
    return nullptr;
}

const Enemy* Gigaboss::boss(const GameWorld& w) const {
    if (uid_ == 0 || stage_ == Stage::Absent) return nullptr;
    return w.findEnemy(uid_);
}

void Gigaboss::before(GameWorld& w) {
    if (!rules_ || stage_ == Stage::Absent || stage_ == Stage::Fallen) return;
    Enemy* b = find(w);
    if (!b) return;
    b->setRaidGuard(stage_ == Stage::Carapace, stage_ == Stage::Exposed ? rules_->exposedMult : 1.0f);
    int playing = 0;
    for (int i = 0; i < w.teamSize(); ++i) playing += w.coop() && w.member(i).presence == GameWorld::Presence::Playing ? 1 : 0;
    inPlay_ = playing;
    b->setPressure(1.0f + rules_->pressurePerPlayer * static_cast<float>(std::max(0, playing - 1)));
}

void Gigaboss::after(GameWorld& w, Contributions& c) {
    fellNow_ = false;
    if (!rules_ || stage_ == Stage::Absent || stage_ == Stage::Fallen) return;
    Enemy* b = find(w);
    if (b && !seen_) {
        seen_ = true;
        c.setBoss(uid_);
        if (fallen_) {
            // Another fight of the event already took it down: the hand is cold, its arena empty.
            b->despawn();
            stage_ = Stage::Absent;
            alert("GIGABOSS · " + rules_->goneText + " : " + rules_->name + " est déjà tombé.", AlertKind::Branch);
            return;
        }
    }
    if (!b || !b->alive()) {
        if (!engaged_) return;
        // It fell (its last stand survived): everyone in play was there.
        endExposure(w, c);
        stage_ = Stage::Fallen;
        fellNow_ = true;
        reached_ = std::max(reached_, 4);
        c.gigabossDown(w);
        if (routine_) routine_->fell(w, *this);
        alert("GIGABOSS · " + rules_->name + " est vaincu ! " + rules_->fallText, AlertKind::Branch);
        return;
    }
    if (!engaged_) {
        if (!b->engaged() || b->dormant()) return;
        engaged_ = true;
        stage_ = Stage::Fight;
        phase_ = reached_ = 1;
        fistClock_ = steps(4.0f);  // its first fists a few seconds in
        alert("GIGABOSS · " + rules_->name + " se réveille : restez groupés, regardez ses gestes !", AlertKind::Danger);
    }
    ++fightTicks_;
    // Its health follows the seats taken (never down: leaving never makes it easier).
    int seated = 0;
    for (int i = 0; i < w.teamSize(); ++i) seated += w.coop() && w.member(i).presence != GameWorld::Presence::Vacant ? 1 : 0;
    seated = std::clamp(std::max(seated, testPlayers_), 1, 8);
    if (seated > scaledFor_) {
        const float before = scaledFor_ > 0 ? healthFactor(*rules_, scaledFor_) : 1.0f;
        b->scaleHealth(healthFactor(*rules_, seated) / before);
        if (scaledFor_ > 0) alert("GIGABOSS · Un joueur arrive : " + rules_->name + " se renforce.", AlertKind::Group);
        scaledFor_ = seated;
    }
    if (fightTicks_ == 1 && testPhase_ > 1) {
        // An administrator's test: the fight starts at that phase (the arena as the phases before left it).
        jumpBossToPhase(w, *b, std::min(testPhase_, static_cast<int>(b->def().phases.size())) - 1);
        alert("TEST ADMIN · Départ en phase " + std::to_string(testPhase_), AlertKind::Group);
    }
    const int now = b->lastStand() ? std::max(phase_, static_cast<int>(b->def().phases.size())) : b->phaseIndex() + 1;
    if (now != phase_) {
        phase_ = now;
        if (now > reached_) {
            reached_ = now;
            const auto& ph = b->def().phases;
            const std::string name = now - 1 < static_cast<int>(ph.size()) ? ph[static_cast<size_t>(now - 1)].name : "";
            alert("GIGABOSS · Phase " + std::to_string(now) + " / " + std::to_string(ph.size()) + (name.empty() ? "" : " · " + name), AlertKind::Danger);
        }
    }
    stepFists(w, *b);  // its fists of magma, one per player in play (every phase)
    if (routine_) {
        // 2.69 (phase 4B): Voidworm, Graviton, Titanus run their own mechanisms (GigabossRoutine).
        routine_->step(w, c, *this, *b);
        return;
    }
    const bool carapacePhase = !b->lastStand() && phase_ >= rules_->carapaceFrom && phase_ < rules_->carapaceUntil && !vents_.empty();
    if (stage_ == Stage::Exposed) {
        stepExposed(w, c, *b, carapacePhase);
        return;
    }
    if (!carapacePhase) {
        if (stage_ == Stage::Carapace) alert("GIGABOSS · Sa carapace vole en éclats pour de bon !", AlertKind::Branch);
        stage_ = Stage::Fight;
        return;
    }
    if (stage_ != Stage::Carapace) {
        stage_ = Stage::Carapace;
        for (Vent& v : vents_) v = Vent{v.area};
        alert("GIGABOSS · " + rules_->name + " se cuirasse : fermez ses " + std::to_string(vents_.size()) + " évents ensemble (tenir " +
                  std::to_string(static_cast<int>(std::lround(rules_->hold))) + " s sur chacun) !",
              AlertKind::Danger);
    }
    stepVents(w, c, *b);
}

int Gigaboss::jetCycle(int k) const {
    const int period = std::max(1, steps(rules_->jetPeriod));
    const int n = std::max<int>(1, static_cast<int>(vents_.size()));
    return static_cast<int>((fightTicks_ + static_cast<uint32_t>(k * period / n)) % static_cast<uint32_t>(period));
}

bool Gigaboss::jetWarning(int k) const {
    if (!rules_ || stage_ != Stage::Carapace || k < 0 || k >= static_cast<int>(vents_.size()) || vents_[static_cast<size_t>(k)].closed) return false;
    return jetCycle(k) < steps(rules_->jetWarn);
}

bool Gigaboss::jetting(int k) const {
    if (!rules_ || stage_ != Stage::Carapace || k < 0 || k >= static_cast<int>(vents_.size()) || vents_[static_cast<size_t>(k)].closed) return false;
    const int t = jetCycle(k), warn = steps(rules_->jetWarn);
    return t >= warn && t < warn + steps(rules_->jetTime);
}

void Gigaboss::stepVents(GameWorld& w, Contributions& c, Enemy& b) {
    using P = GameWorld::Presence;
    const int hold = std::max(1, steps(rules_->hold));
    // Fewer players, more time before a vent lights again (two must still hurry, one can do it).
    const float slack = inPlay_ <= 1 ? 2.0f : inPlay_ == 2 ? 1.35f : 1.0f;
    const uint32_t reopen = static_cast<uint32_t>(std::max(1, steps(rules_->reopen * slack)));
    for (size_t k = 0; k < vents_.size(); ++k) {
        Vent& v = vents_[k];
        v.holders = 0;
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
            if (!w.coop() || w.member(i).presence != P::Playing) continue;
            const Player& p = w.playerAt(i);
            if (p.alive() && !p.respawning() && v.area.intersects(p.box)) v.holders |= static_cast<uint8_t>(1u << i);
        }
        if (v.closed) {
            if (fightTicks_ - v.closedAt >= reopen) {
                v.closed = false;
                v.hold = 0;
                alert("GIGABOSS · Un évent se rallume : refermez-le !", AlertKind::Danger);
                w.particles().emit("eruption_fire", {v.area.center().x, v.area.y}, 1.2f);
            }
            continue;
        }
        if (v.holders) {
            v.hold = std::min(hold, v.hold + 1);
            if (v.hold >= hold) {
                v.closed = true;
                v.closedAt = fightTicks_;
                for (int i = 0; i < 8; ++i)
                    if (v.holders & (1u << i)) c.vent(i);
                w.particles().emit("steam_burst", {v.area.center().x, v.area.y}, 1.4f);
                w.playSound("sounds/shield_block.wav", v.area.center(), 0.8f, 0.7f);
                alert("GROUPE · Évent fermé (" + std::to_string(ventsClosed()) + " / " + std::to_string(vents_.size()) + ")", AlertKind::Group);
            }
        } else {
            v.hold = std::max(0, v.hold - 2);
        }
        // Its jet of magma: announced (bubbles), then thrown up the column over the vent.
        if (jetting(static_cast<int>(k))) {
            const Rect column{v.area.x, v.area.y - rules_->jetHeight, v.area.w, v.area.h + rules_->jetHeight};
            DamageInfo d;
            d.amount = rules_->jetDamage;
            d.type = DamageType::Explosion;
            d.source = {v.area.center().x, v.area.bottom()};
            d.knockback = 260.0f;
            w.hurtPlayersByBoss(column, d, &b, &rules_->jetName);
            if (fightTicks_ % 6 == 0) w.particles().emit("eruption_fire", {v.area.center().x, v.area.y}, 0.8f);
        } else if (jetWarning(static_cast<int>(k)) && fightTicks_ % 10 == 0) {
            w.particles().emit("lava_bubbles", {v.area.center().x, v.area.y}, 0.6f);
        }
    }
    if (ventsClosed() == static_cast<int>(vents_.size())) expose(w, c, b);
}

void Gigaboss::expose(GameWorld& w, Contributions& c, Enemy& b) {
    stage_ = Stage::Exposed;
    exposedTicks_ = steps(rules_->exposed);
    weak_.clear();
    const float factor = 1.0f + rules_->weakHpPerPlayer * static_cast<float>(std::max(0, scaledFor_ - 1));
    for (const Vec2& s : rules_->weakSpots) {
        const Vec2 at{b.box.center().x + s.x * b.facing, b.box.bottom() + s.y};
        if (Enemy* e = w.spawnEnemy(rules_->weakEnemy, at, b.facing < 0.0f)) {
            e->summonedBy = b.uid();
            e->countsForStats = false;
            e->scaleHealth(factor);
            weak_.push_back(e->uid());
        }
    }
    weakTotal_ = static_cast<int>(weak_.size());
    c.setWeak(weak_);
    gluWeak(w, b);
    w.shake(0.8f);
    w.flash({255, 210, 140, 255}, 0.25f);
    w.particles().emit("fire_burst", b.box.center(), 3.0f);
    w.playSound("sounds/shell_shatter.wav", b.box.center(), 1.0f);
    alert("GIGABOSS · La carapace éclate : frappez le point faible (" + std::to_string(static_cast<int>(std::lround(rules_->exposed))) + " s) !",
          AlertKind::Branch);
}

void Gigaboss::gluWeak(GameWorld& w, const Enemy& b) {
    for (size_t k = 0; k < weak_.size() && k < rules_->weakSpots.size(); ++k) {
        Enemy* e = weak_[k] ? w.findEnemy(weak_[k]) : nullptr;
        if (!e || !e->alive()) continue;
        // Stuck to its knuckles wherever it goes (the order of weakSpots, those broken aside).
        const Vec2 s = rules_->weakSpots[k];
        const Vec2 at{b.box.center().x + s.x * b.facing, b.box.bottom() + s.y};
        e->box = Rect::fromBottomCenter(at, e->box.size());
        e->prevBox = e->box;
        e->vel = {0.0f, 0.0f};
    }
}

void Gigaboss::stepExposed(GameWorld& w, Contributions& c, Enemy& b, bool carapacePhase) {
    // The weak points broken this step: each takes its share of the gigaboss's health.
    for (size_t k = 0; k < weak_.size(); ++k) {
        if (weak_[k] == 0) continue;
        const Enemy* e = w.findEnemy(weak_[k]);
        if (e && e->alive()) continue;
        const int seat = c.brokeWeak(weak_[k]);
        if (seat >= 0) c.weakPoint(seat);
        weak_[k] = 0;
        b.wound(w, rules_->weakShare * b.maxHp());
        int broken = 0;
        for (uint32_t u : weak_) broken += u == 0 ? 1 : 0;
        alert("GROUPE · Point faible brisé (" + std::to_string(broken) + " / " + std::to_string(weakTotal_) + ")", AlertKind::Group);
    }
    // Broken ones keep their place (0) so the others stay on their knuckles.
    if (!b.alive()) return;
    gluWeak(w, b);
    if (--exposedTicks_ > 0 && carapacePhase) return;
    endExposure(w, c);
    stage_ = carapacePhase ? Stage::Carapace : Stage::Fight;
    for (Vent& v : vents_) v = Vent{v.area};
    if (carapacePhase) alert("GIGABOSS · La carapace se reforme : les évents se rallument !", AlertKind::Danger);
}

void Gigaboss::endExposure(GameWorld& w, Contributions& c) {
    for (uint32_t u : weak_)
        if (Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) e->despawn();
    weak_.clear();
    c.setWeak(weak_);
    exposedTicks_ = 0;
}

void Gigaboss::stepFists(GameWorld& w, Enemy& b) {
    // « Poings de magma »: every period (shorter at each phase, and with the pressure), a mark under
    // each player in play, the same step for all (8 players: 8 fists); `warn` later a fist of magma
    // falls on each mark: whoever is still there is hit. The marks never follow anyone.
    if (rules_->fistPeriods.empty() || b.lastStand()) return;
    using P = GameWorld::Presence;
    const int phaseIdx = std::clamp(phase_ - 1, 0, static_cast<int>(rules_->fistPeriods.size()) - 1);
    const float period = rules_->fistPeriods[static_cast<size_t>(phaseIdx)] / std::max(1.0f, b.pressure());
    const int warn = std::max(1, steps(rules_->fistWarn)), strike = std::max(1, steps(0.25f));
    if (fistClock_ <= 0) {
        fistClock_ = std::max(warn + strike + 1, steps(period));
        fists_.clear();
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
            if (!w.coop() || w.member(i).presence != P::Playing) continue;
            const Player& p = w.playerAt(i);
            if (!p.alive() || p.respawning()) continue;
            fists_.push_back({p.feet(), warn, i});
        }
        if (!fists_.empty()) alert("DANGER · " + rules_->fistName + " : quittez votre marque !", AlertKind::Danger);
    }
    --fistClock_;
    for (Fist& f : fists_) {
        if (f.left > -strike) --f.left;
        if (f.left > 0 || f.left <= -strike) continue;
        const Rect column = fistColumn(f);
        DamageInfo d;
        d.amount = rules_->fistDamage;
        d.type = DamageType::Explosion;
        d.source = {f.at.x, f.at.y};
        d.knockback = 300.0f;
        w.hurtPlayersByBoss(column, d, &b, &rules_->fistName);
        if (f.left == 0) {
            w.particles().emit("fire_burst", {f.at.x, f.at.y - 6.0f}, 1.4f);
            w.shake(0.25f);
        }
    }
    std::erase_if(fists_, [&](const Fist& f) { return f.left <= -strike; });
}

Rect Gigaboss::fistColumn(const Fist& f) const {
    const float half = rules_->fistWidth * 0.5f;
    return {f.at.x - half, f.at.y - 220.0f, half * 2.0f, 224.0f};
}

int Gigaboss::weakLeft() const {
    int n = 0;
    for (uint32_t u : weak_) n += u != 0 ? 1 : 0;
    return n;
}

int Gigaboss::ventsClosed() const {
    int n = 0;
    for (const Vent& v : vents_) n += v.closed ? 1 : 0;
    return n;
}

float Gigaboss::holdRatio(int k) const {
    if (!rules_ || k < 0 || k >= static_cast<int>(vents_.size())) return 0.0f;
    const Vent& v = vents_[static_cast<size_t>(k)];
    return v.closed ? 1.0f : static_cast<float>(v.hold) / static_cast<float>(std::max(1, steps(rules_->hold)));
}

float Gigaboss::reopenIn(int k) const {
    if (!rules_ || k < 0 || k >= static_cast<int>(vents_.size()) || !vents_[static_cast<size_t>(k)].closed) return 0.0f;
    const float slack = inPlay_ <= 1 ? 2.0f : inPlay_ == 2 ? 1.35f : 1.0f;
    const int reopen = std::max(1, steps(rules_->reopen * slack));
    const int left = reopen - static_cast<int>(fightTicks_ - vents_[static_cast<size_t>(k)].closedAt);
    return static_cast<float>(std::max(0, left)) / static_cast<float>(kSimHz);
}

float Gigaboss::exposedLeft() const { return stage_ == Stage::Exposed ? static_cast<float>(exposedTicks_) / static_cast<float>(kSimHz) : 0.0f; }

std::vector<std::pair<std::string, Gigaboss::AlertKind>> Gigaboss::takeAlerts() {
    std::vector<std::pair<std::string, AlertKind>> out;
    out.swap(alerts_);
    return out;
}

uint64_t Gigaboss::hash() const {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, static_cast<uint64_t>(stage_));
    h = mix(h, uid_);
    h = mix(h, (engaged_ ? 1u : 0u) | (static_cast<uint64_t>(reached_) << 8) | (static_cast<uint64_t>(phase_) << 16));
    h = mix(h, fightTicks_);
    h = mix(h, static_cast<uint64_t>(exposedTicks_));
    h = mix(h, (static_cast<uint64_t>(scaledFor_) << 8) | static_cast<uint64_t>(inPlay_));
    for (const Vent& v : vents_) h = mix(h, (static_cast<uint64_t>(v.hold) << 40) | (static_cast<uint64_t>(v.closed) << 32) | v.closedAt);
    for (uint32_t u : weak_) h = mix(h, u);
    h = mix(h, static_cast<uint64_t>(fistClock_));
    for (const Fist& f : fists_)
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(f.left)) << 32) ^ static_cast<uint64_t>(std::lround(f.at.x * 4.0f)) ^
                       (static_cast<uint64_t>(std::lround(f.at.y * 4.0f)) << 16));
    h = mix(h, (seen_ ? 1u : 0u) | (fallen_ ? 2u : 0u));
    if (routine_) h = mix(h, routine_->hash() ^ (static_cast<uint64_t>(mask_) << 32));  // 2.69 (phase 4B)
    return h;
}

}  // namespace cosmic::raid
