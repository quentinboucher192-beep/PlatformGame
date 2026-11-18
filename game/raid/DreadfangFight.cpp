#include "game/raid/DreadfangFight.h"

#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/Damage.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"

namespace cosmic::raid {

using mech::mix;
using mech::steps;

namespace {
const std::string kStab = "Griffes du sol";
const std::string kSweep = "Balayage des griffes";
const std::string kPin = "Griffe clouante";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 138, 138, 255};
const Color kIce{127, 246, 255, 255};
std::string secs(int ticks) { return std::to_string(std::max(0, (ticks + kSimHz - 1) / kSimHz)) + " s"; }
std::string storeyName(int k) { return k == 0 ? "le sol" : "l'étage " + std::to_string(k); }
}  // namespace

DreadfangFight::DreadfangFight(const GigabossRules& rules) {
    const Json& g = rules.params;
    stabs_.read(g.value("stabs", Json::object()));
    const Json sweep = g.value("sweep", Json::object());
    sweepPeriods_ = mech::floats(sweep.value("periods", Json()), {11.0f, 9.0f, 7.5f});
    for (float& p : sweepPeriods_) p = std::clamp(p, 4.0f, 60.0f);
    sweepWarn_ = std::clamp(jget<float>(sweep, "warn", sweepWarn_), 0.5f, 4.0f);
    sweepSpeed_ = std::clamp(jget<float>(sweep, "speed", sweepSpeed_), 300.0f, 4000.0f);
    sweepHeight_ = std::clamp(jget<float>(sweep, "height", sweepHeight_), 12.0f, 96.0f);
    sweepDamage_ = std::clamp(jget<float>(sweep, "damage", sweepDamage_), 0.0f, 4.0f);
    storeys_ = mech::floats(sweep.value("storeys", Json()));
    sweepCount_ = mech::floats(sweep.value("count", Json()), {1, 2, 3});
    const std::vector<float> span = mech::floats(sweep.value("span", Json()));
    if (span.size() == 2 && span[1] > span[0]) {
        spanX0_ = span[0];
        spanX1_ = span[1];
    }
    const Json pin = g.value("pin", Json::object());
    pinFrom_ = std::clamp(jget<int>(pin, "from", pinFrom_), 1, 9);
    pinPeriod_ = std::clamp(jget<float>(pin, "period", pinPeriod_), 5.0f, 60.0f);
    pinWarn_ = std::clamp(jget<float>(pin, "warn", pinWarn_), 0.4f, 4.0f);
    pinReach_ = std::clamp(jget<float>(pin, "reach", pinReach_), 16.0f, 160.0f);
    pinHold_ = std::clamp(jget<float>(pin, "hold", pinHold_), 2.0f, 20.0f);
    pinEvery_ = std::clamp(jget<float>(pin, "every", pinEvery_), 0.5f, 10.0f);
    pinDamage_ = std::clamp(jget<float>(pin, "damage", pinDamage_), 0.0f, 4.0f);
    pinEnemy_ = jstr(pin, "enemy", pinEnemy_);
    pinHpPerPlayer_ = std::clamp(jget<float>(g, "weakHpPerPlayer", pinHpPerPlayer_), 0.0f, 2.0f);
    const Json guard = g.value("guard", Json::array());
    if (guard.is_array() && guard.size() == 2) {
        guardFrom_ = guard[0].get<int>();
        guardUntil_ = guard[1].get<int>();
    }
    anchors_.read(g.value("anchors", Json::array()));
    hold_ = std::clamp(jget<float>(g, "hold", hold_), 0.5f, 10.0f);
    reopen_ = std::clamp(jget<float>(g, "reopen", reopen_), 3.0f, 60.0f);
    chained_ = std::clamp(jget<float>(g, "chained", chained_), 2.0f, 30.0f);
    chainedMult_ = std::clamp(jget<float>(g, "chainedMult", chainedMult_), 1.0f, 4.0f);
    weak_.read(g);
}

void DreadfangFight::syncWeak(Contributions& c) const {
    // Its knuckles and its nailing claws: their damage is the gigaboss's, their breaking is counted.
    std::vector<uint32_t> all = weak_.uids;
    for (const Pin& p : pins_)
        if (p.claw) all.push_back(p.claw);
    c.setWeak(all);
}

void DreadfangFight::step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    const int phase = g.phase();
    phase_ = phase;
    ticks_ = g.fightTicks();
    inPlay_ = mech::countInPlay(w);
    if (stabs_.step(w, b, phase, c, kStab)) {
        for (const auto& m : stabs_.marks) w.particles().emit("eruption_warning_ice", m.at, 0.6f);
        g.say("DANGER · Griffes du sol : quittez votre marque !", Gigaboss::AlertKind::Danger);
    }
    for (const auto& m : stabs_.marks)
        if (m.left == 0) {
            w.particles().emit("ice_burst", {m.at.x, m.at.y - 8.0f}, 1.2f);
            w.shake(0.2f);
        }
    stepSweeps(w, g, b, phase);
    stepPins(w, g, b, c, phase);
    if (!b.alive()) return;
    stepChains(w, g, b, c);
}

// ------------------------------------------------------------------------ sweeps
bool DreadfangFight::bladeOut(const Sweep& s) const { return s.t > steps(sweepWarn_); }

float DreadfangFight::bladeX(const Sweep& s) const {
    const float run = static_cast<float>(std::max(0, s.t - steps(sweepWarn_))) / static_cast<float>(kSimHz) * sweepSpeed_;
    return s.dir > 0 ? spanX0_ + run : spanX1_ - run;
}

float DreadfangFight::sweepWarnLeft(const Sweep& s) const {
    return static_cast<float>(std::max(0, steps(sweepWarn_) - s.t)) / static_cast<float>(kSimHz);
}

void DreadfangFight::stepSweeps(GameWorld& w, Gigaboss& g, Enemy& b, int phase) {
    const int n = static_cast<int>(storeys_.size());
    if (n == 0 || sweepPeriods_.empty() || b.lastStand()) {
        sweeps_.clear();
        return;
    }
    if (--sweepClock_ <= 0) {
        const float period = mech::byPhase(sweepPeriods_, phase, 10.0f) / std::max(1.0f, b.pressure());
        sweepClock_ = std::max(steps(sweepWarn_) + steps((spanX1_ - spanX0_) / sweepSpeed_) + 2, steps(period));
        // One, two then three storeys at once (never all: two at least stay safe), changing each time.
        const int count = std::clamp(static_cast<int>(std::lround(mech::byPhase(sweepCount_, phase, 1.0f))), 1, std::max(1, n - 2));
        Sweep s;
        s.dir = sweepVolley_ % 2 == 0 ? 1 : -1;
        const int start = (sweepVolley_ * 3 + phase) % n;
        for (int k = 0; s.storeys.size() < static_cast<size_t>(count) && k < 2 * n; ++k) {
            const int idx = (start + 2 * k + (k >= (n + 1) / 2 ? 1 : 0)) % n;
            if (std::find(s.storeys.begin(), s.storeys.end(), idx) == s.storeys.end()) s.storeys.push_back(idx);
        }
        std::sort(s.storeys.begin(), s.storeys.end());
        std::string where;
        for (size_t k = 0; k < s.storeys.size(); ++k) where += (k ? (k + 1 == s.storeys.size() ? " et " : ", ") : "") + storeyName(s.storeys[k]);
        g.say("DANGER · Balayage des griffes " + std::string(s.dir > 0 ? "depuis l'ouest" : "depuis l'est") + " sur " + where +
                  " : sautez par-dessus la lame ou changez d'étage !",
              Gigaboss::AlertKind::Danger);
        w.playSound("sounds/boss_roar_ice.wav", b.box.center(), 0.7f, 1.1f);
        sweeps_.push_back(s);
        ++sweepVolley_;
    }
    for (Sweep& s : sweeps_) {
        ++s.t;
        if (!bladeOut(s)) continue;
        const float x = bladeX(s);
        if (s.t == steps(sweepWarn_) + 1) w.playSound("sounds/slash.wav", {x, storeys_[static_cast<size_t>(s.storeys.front())]}, 1.0f, 0.7f);
        for (int k : s.storeys) {
            const float y = storeys_[static_cast<size_t>(k)];
            const Rect band{x - 28.0f, y - sweepHeight_, 56.0f, sweepHeight_ - 2.0f};
            DamageInfo d;
            d.amount = sweepDamage_;
            d.type = DamageType::Explosion;
            d.source = {x - 40.0f * static_cast<float>(s.dir), y};
            d.knockback = 320.0f;
            w.hurtPlayersByBoss(band, d, &b, &kSweep);
            if (s.t % 4 == 0) w.particles().emit("frost_burst", {x, y - sweepHeight_ * 0.5f}, 0.5f);
        }
    }
    std::erase_if(sweeps_, [&](const Sweep& s) { return bladeOut(s) && (bladeX(s) < spanX0_ - 40.0f || bladeX(s) > spanX1_ + 40.0f); });
}

// ------------------------------------------------------------------------ pins
float DreadfangFight::pinWarnRatio(const Pin& p) const {
    return std::clamp(static_cast<float>(p.t) / static_cast<float>(std::max(1, steps(pinWarn_))), 0.0f, 1.0f);
}

void DreadfangFight::stepPins(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c, int phase) {
    using AK = Gigaboss::AlertKind;
    bool changed = false;
    // Aim: one player (two from five in play) on a mark that does not follow them.
    if (phase >= pinFrom_ && !b.lastStand() && inPlay_ >= 2) {
        if (--pinClock_ <= 0) {
            pinClock_ = std::max(steps(pinWarn_ + pinHold_) + 1, steps(pinPeriod_ / std::max(1.0f, b.pressure())));
            const int want = inPlay_ >= 5 ? 2 : 1;
            int aimed = 0;
            for (int k = 0; k < 8 && aimed < want; ++k) {
                const int seat = (pinVolley_ * 3 + k) % 8;
                if (!mech::inPlay(w, seat)) continue;
                if (std::any_of(pins_.begin(), pins_.end(), [&](const Pin& p) { return p.seat == seat; })) continue;
                Pin p;
                p.seat = seat;
                p.at = w.playerAt(seat).feet();
                pins_.push_back(p);
                ++aimed;
            }
            if (aimed > 0) g.say("DANGER · Griffe clouante : Dreadfang vise un joueur, écartez-vous de votre marque !", AK::Danger);
            ++pinVolley_;
        }
    } else {
        pinClock_ = std::max(pinClock_, steps(4.0f));
    }
    const int warnS = std::max(1, steps(pinWarn_)), every = std::max(1, steps(pinEvery_));
    for (Pin& p : pins_) {
        ++p.t;
        if (!p.nailed) {
            if (p.t < warnS) continue;
            // The claw comes down on the mark: still there, the player is nailed.
            w.particles().emit("ice_burst", p.at, 1.4f);
            w.shake(0.3f);
            const Vec2 f = mech::inPlay(w, p.seat) ? w.playerAt(p.seat).feet() : Vec2{1e9f, 1e9f};
            if (std::abs(f.x - p.at.x) <= pinReach_ && std::abs(f.y - p.at.y) <= 40.0f) {
                if (Enemy* e = w.spawnEnemy(pinEnemy_, p.at, b.facing < 0.0f)) {
                    e->summonedBy = b.uid();
                    e->countsForStats = false;
                    e->scaleHealth(1.0f + pinHpPerPlayer_ * static_cast<float>(std::max(0, g.scaledFor() - 1)));
                    p.claw = e->uid();
                }
                p.nailed = true;
                p.left = std::max(1, steps(pinHold_));
                mech::hurtSeat(w, b, p.seat, pinDamage_, {p.at.x, p.at.y - 60.0f}, 0.0f, kPin);
                changed = true;
                g.say("GROUPE · Un joueur est cloué au sol : brisez la griffe pour le libérer !", AK::Group);
            } else {
                c.dodged(p.seat, pinDamage_);
                p.left = 0;
            }
            continue;
        }
        // Nailed: held on the mark, hurt now and then, until the claw is broken or draws back.
        const Enemy* claw = p.claw ? w.findEnemy(p.claw) : nullptr;
        if (p.claw && (!claw || !claw->alive())) {
            const int by = c.brokeWeak(p.claw);
            if (by >= 0 && by != p.seat) c.rescue(by);
            p.claw = 0;
            p.left = 0;
            changed = true;
            g.say("GROUPE · La griffe est brisée : le joueur est libéré !", AK::Group);
            continue;
        }
        if (!mech::inPlay(w, p.seat)) {
            p.left = 0;
            continue;
        }
        Player& pl = w.playerAt(p.seat);
        pl.teleportTo(p.at);
        pl.vel = {0.0f, 0.0f};
        if (p.t % every == 0) mech::hurtSeat(w, b, p.seat, pinDamage_, {p.at.x, p.at.y - 60.0f}, 0.0f, kPin);
        if (--p.left <= 0) g.say("GROUPE · La griffe se retire.", AK::Group);
    }
    for (Pin& p : pins_)
        if (p.left <= 0 && (p.nailed || p.t >= warnS)) {
            if (Enemy* e = p.claw ? w.findEnemy(p.claw) : nullptr; e && e->alive()) e->despawn();
            if (p.claw) changed = true;
            p.claw = 0;
        }
    std::erase_if(pins_, [&](const Pin& p) { return (p.nailed || p.t >= warnS) && p.left <= 0; });
    if (changed) syncWeak(c);
}

// ------------------------------------------------------------------------ chains
float DreadfangFight::holdRatio(int k) const {
    if (k < 0 || k >= static_cast<int>(anchors_.places.size())) return 0.0f;
    const auto& p = anchors_.places[static_cast<size_t>(k)];
    return p.locked ? 1.0f : static_cast<float>(p.hold) / static_cast<float>(std::max(1, steps(hold_)));
}

float DreadfangFight::relockIn(int k, uint32_t fightTicks) const {
    if (k < 0 || k >= static_cast<int>(anchors_.places.size()) || !anchors_.places[static_cast<size_t>(k)].locked) return 0.0f;
    const int reopen = std::max(1, steps(reopen_ * slack()));
    const int left = reopen - static_cast<int>(fightTicks - anchors_.places[static_cast<size_t>(k)].lockedAt);
    return static_cast<float>(std::max(0, left)) / static_cast<float>(kSimHz);
}

float DreadfangFight::chainedLeft() const { return static_cast<float>(std::max(0, chainedTicks_)) / static_cast<float>(kSimHz); }

void DreadfangFight::stepChains(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    using AK = Gigaboss::AlertKind;
    const bool guardPhase = !b.lastStand() && phase_ >= guardFrom_ && phase_ < guardUntil_ && !anchors_.places.empty();
    if (chainedTicks_ > 0) {
        // Pinned to the floor: its knuckles bare.
        if (weak_.step(w, b, c) > 0)
            g.say("GROUPE · Jointure brisée (" + std::to_string(weak_.total - weak_.left()) + " / " + std::to_string(weak_.total) + ")", AK::Group);
        if (!b.alive()) return;
        b.box = Rect::fromBottomCenter(chainedAt_, b.box.size());
        b.prevBox = b.box;
        b.vel = {0.0f, 0.0f};
        weak_.glue(w, b);
        if (--chainedTicks_ > 0 && guardPhase) return;
        chainedTicks_ = 0;
        weak_.clear(w);
        syncWeak(c);
        anchors_.reset();
        if (guardPhase) g.say("GIGABOSS · Dreadfang brise ses chaînes : tenez-les de nouveau ensemble !", AK::Danger);
    }
    if (guardPhase && !guardSaid_) {
        guardSaid_ = true;
        anchors_.reset();
        g.say("GIGABOSS · Armure de givre : tenez ses " + std::to_string(anchors_.places.size()) + " chaînes ensemble (" +
                  std::to_string(static_cast<int>(std::lround(hold_ * 10.0f)) / 10) + " s sur chacune) pour l'enchaîner !",
              AK::Danger);
    }
    if (guard_ && !guardPhase) g.say("GIGABOSS · Son armure de givre vole en éclats : ses griffes se déchaînent !", AK::Branch);
    guard_ = guardPhase;
    if (!guard_) return;
    anchors_.sense(w);
    const int holdS = std::max(1, steps(hold_));
    const uint32_t reopen = static_cast<uint32_t>(std::max(1, steps(reopen_ * slack())));
    int locked = 0;
    for (auto& p : anchors_.places) {
        if (p.locked) {
            if (ticks_ - p.lockedAt >= reopen) {
                p.locked = false;
                p.hold = 0;
                g.say("GIGABOSS · Une chaîne se détend : retenez-la !", AK::Danger);
            } else {
                ++locked;
                continue;
            }
        }
        if (p.holders) {
            p.hold = std::min(holdS, p.hold + 1);
            if (p.hold >= holdS) {
                p.locked = true;
                p.lockedAt = ticks_;
                ++locked;
                for (int i = 0; i < 8; ++i)
                    if ((p.holders >> i) & 1u) c.vent(i);
                w.particles().emit("frost_burst", p.area.center(), 1.2f);
                w.playSound("sounds/shield_block.wav", p.area.center(), 0.8f, 0.6f);
                g.say("GROUPE · Chaîne tenue (" + std::to_string(locked) + " / " + std::to_string(anchors_.places.size()) + ")", AK::Group);
            }
        } else {
            p.hold = std::max(0, p.hold - 2);
        }
    }
    if (locked < static_cast<int>(anchors_.places.size())) return;
    // Its three chains held: pinned to the floor, its knuckles bare.
    chainedTicks_ = std::max(1, steps(chained_));
    const float floor = storeys_.empty() ? b.box.bottom() : storeys_.front();
    chainedAt_ = {std::clamp(b.box.center().x, spanX0_ + b.box.w * 0.5f, spanX1_ - b.box.w * 0.5f), floor};
    b.box = Rect::fromBottomCenter(chainedAt_, b.box.size());
    b.prevBox = b.box;
    weak_.spawn(w, b, g.scaledFor());
    syncWeak(c);
    w.shake(0.9f);
    w.flash({200, 250, 255, 255}, 0.25f);
    w.particles().emit("frost_nova", b.box.center(), 2.5f);
    w.playSound("sounds/ice_shatter.wav", b.box.center(), 1.0f);
    g.say("GIGABOSS · Les chaînes tiennent : Dreadfang est plaqué au sol ! Brisez ses jointures (" + std::to_string(static_cast<int>(std::lround(chained_))) +
              " s) !",
          AK::Branch);
}

void DreadfangFight::stop(GameWorld& w, Contributions& c) {
    weak_.clear(w);
    for (Pin& p : pins_)
        if (Enemy* e = p.claw ? w.findEnemy(p.claw) : nullptr; e && e->alive()) e->despawn();
    pins_.clear();
    sweeps_.clear();
    stabs_.marks.clear();
    chainedTicks_ = 0;
    syncWeak(c);
}

uint64_t DreadfangFight::hash() const {
    uint64_t h = 0xd4eadfa9ULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 8) | (guard_ ? 1u : 0u) | (guardSaid_ ? 2u : 0u));
    stabs_.hashInto(h);
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(sweepClock_)) << 32) | static_cast<uint32_t>(sweepVolley_));
    for (const Sweep& s : sweeps_) {
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(s.t)) << 8) | static_cast<uint64_t>(s.dir + 1));
        for (int k : s.storeys) h = mix(h, static_cast<uint64_t>(k));
    }
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(pinClock_)) << 32) | static_cast<uint32_t>(pinVolley_));
    for (const Pin& p : pins_)
        h = mix(h, (static_cast<uint64_t>(p.seat & 0xFF) << 56) ^ (static_cast<uint64_t>(static_cast<uint32_t>(p.t)) << 24) ^
                       (static_cast<uint64_t>(static_cast<uint32_t>(p.left)) << 4) ^ (p.nailed ? 1u : 0u) ^ (static_cast<uint64_t>(p.claw) << 32) ^
                       static_cast<uint64_t>(std::lround(p.at.x * 4.0f)));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(chainedTicks_)) << 32) | static_cast<uint32_t>(std::lround(chainedAt_.x)));
    anchors_.hashInto(h);
    weak_.hashInto(h);
    return h;
}

// ------------------------------------------------------------------------ display
std::string DreadfangFight::fallText(const std::string& name) const {
    return "GIGABOSS · " + name + " est vaincu ! Le bras gauche d'Asterion retombe, ses griffes arrachées.";
}
std::string DreadfangFight::goneText(const std::string& name) const { return "GIGABOSS · Les griffes sont froides : " + name + " est déjà tombé."; }
std::string DreadfangFight::goneLine(const std::string& name) const { return name + " est déjà tombé : le bras gauche ne griffe plus."; }
std::string DreadfangFight::fallCard() const {
    return "Le bras gauche d'Asterion retombe, ses griffes arrachées. Les résultats comptent dès que la majorité de votre instance les a confirmés "
           "au serveur.";
}

void DreadfangFight::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    const int total = static_cast<int>(anchors_.places.size());
    if (chainedTicks_ > 0) {
        out.push_back({secs(chainedTicks_), "Dreadfang est enchaîné (" + ph + ") : brisez ses jointures · " + std::to_string(weak_.total - weak_.left()) +
                                                " / " + std::to_string(weak_.total),
                       kGreen});
    } else if (guard_) {
        int locked = 0;
        for (const auto& p : anchors_.places) locked += p.locked ? 1 : 0;
        out.push_back({std::to_string(locked) + " / " + std::to_string(total),
                       "Armure de givre (" + ph + ") : tenez les " + std::to_string(total) +
                           " chaînes ensemble (une ancre chacun) : une chaîne tenue cède au bout de quelques secondes.",
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Dreadfang · " + ph + " : sautez ses lames de balayage, quittez les marques des griffes du sol.", kOrange});
    }
    for (const Sweep& s : sweeps_)
        if (!bladeOut(s)) {
            std::string where;
            for (size_t k = 0; k < s.storeys.size(); ++k) where += (k ? (k + 1 == s.storeys.size() ? " et " : ", ") : "") + storeyName(s.storeys[k]);
            out.push_back({secs(steps(sweepWarnLeft(s))), "Balayage sur " + where + " : sautez la lame ou changez d'étage.", kRed});
        }
    for (const Pin& p : pins_)
        if (p.nailed && p.seat >= 0 && p.seat < w.teamSize())
            out.push_back({"✚", "Un allié est cloué au sol : brisez la griffe qui le tient (" + secs(p.left) + ").", kIce});
}

void DreadfangFight::threats(const GameWorld& w, std::vector<Vec2>& out) const {
    (void)w;
    for (const auto& m : stabs_.marks) out.push_back(m.at);
    for (const Pin& p : pins_) out.push_back(p.at);
}

void DreadfangFight::targets(std::vector<uint32_t>& out) const {
    for (uint32_t u : weak_.uids)
        if (u) out.push_back(u);
    for (const Pin& p : pins_)
        if (p.claw) out.push_back(p.claw);
}

}  // namespace cosmic::raid
