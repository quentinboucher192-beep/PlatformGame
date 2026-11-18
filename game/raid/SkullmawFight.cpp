#include "game/raid/SkullmawFight.h"

#include <cmath>

#include "engine/physics/Collision.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"

namespace cosmic::raid {

using mech::mix;
using mech::steps;

namespace {
const std::string kGaze = "Regard traqueur";
const std::string kFlash = "Regard de mort";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 138, 138, 255};
const Color kViolet{201, 168, 255, 255};
std::string secs(int ticks) { return std::to_string(std::max(0, (ticks + kSimHz - 1) / kSimHz)) + " s"; }
}  // namespace

SkullmawFight::SkullmawFight(const GigabossRules& rules) {
    const Json& g = rules.params;
    for (const auto& e : g.value("eyes", Json::array()))
        if (e.is_array() && e.size() == 2) eyes_.push_back({e[0].get<float>(), e[1].get<float>()});
    if (eyes_.empty()) eyes_ = {{-52.0f, -150.0f}, {52.0f, -150.0f}};
    const Json gaze = g.value("gaze", Json::object());
    gazePeriods_ = mech::floats(gaze.value("periods", Json()), {8.0f, 6.5f, 5.0f});
    for (float& p : gazePeriods_) p = std::clamp(p, 2.5f, 60.0f);
    track_ = std::clamp(jget<float>(gaze, "track", track_), 0.3f, 4.0f);
    lock_ = std::clamp(jget<float>(gaze, "lock", lock_), 0.2f, 2.0f);
    fire_ = std::clamp(jget<float>(gaze, "fire", fire_), 0.1f, 2.0f);
    width_ = std::clamp(jget<float>(gaze, "width", width_), 4.0f, 64.0f);
    reach_ = std::clamp(jget<float>(gaze, "reach", reach_), 200.0f, 6000.0f);
    gazeDamage_ = std::clamp(jget<float>(gaze, "damage", gazeDamage_), 0.0f, 4.0f);
    const Json veil = g.value("veil", Json::array());
    if (veil.is_array() && veil.size() == 2) {
        veilFrom_ = veil[0].get<int>();
        veilUntil_ = veil[1].get<int>();
    }
    mirrors_.read(g.value("mirrors", Json::array()));
    const Json flash = g.value("flash", Json::object());
    flashPeriods_ = mech::floats(flash.value("periods", Json()), {16.0f, 13.0f});
    for (float& p : flashPeriods_) p = std::clamp(p, 6.0f, 60.0f);
    charge_ = std::clamp(jget<float>(flash, "charge", charge_), 1.0f, 8.0f);
    flashDamage_ = std::clamp(jget<float>(flash, "damage", flashDamage_), 0.0f, 4.0f);
    needs_ = mech::floats(g.value("need", Json()), {1, 2, 2, 2, 3, 3, 4, 4});
    blind_ = std::clamp(jget<float>(g, "blind", blind_), 2.0f, 30.0f);
    blindMult_ = std::clamp(jget<float>(g, "blindMult", blindMult_), 1.0f, 4.0f);
    weak_.read(g);
    const Json light = g.value("searchlight", Json::object());
    searchPeriod_ = std::clamp(jget<float>(light, "period", searchPeriod_), 2.0f, 30.0f);
    searchSpread_ = std::clamp(jget<float>(light, "spread", searchSpread_), 0.05f, 1.2f);
}

Vec2 SkullmawFight::eyeOf(const Enemy& b, int k) const {
    const Vec2 e = eyes_[static_cast<size_t>(std::clamp(k, 0, static_cast<int>(eyes_.size()) - 1))];
    return {b.box.center().x + e.x * b.facing, b.box.bottom() + e.y};
}

Vec2 SkullmawFight::brow(const Enemy& b) const {
    Vec2 sum{0.0f, 0.0f};
    for (int k = 0; k < static_cast<int>(eyes_.size()); ++k) sum = sum + eyeOf(b, k);
    return sum * (1.0f / static_cast<float>(eyes_.size()));
}

void SkullmawFight::step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    using AK = Gigaboss::AlertKind;
    const int phase = g.phase();
    phase_ = phase;
    const int playing = mech::countInPlay(w);
    const int places = static_cast<int>(mirrors_.places.size());
    need_ = std::clamp(static_cast<int>(std::lround(mech::byPhase(needs_, std::clamp(playing, 1, 8), 1.0f))), 1, std::max(1, places));
    mirrors_.sense(w);
    const bool veilPhase = !b.lastStand() && phase >= veilFrom_ && phase < veilUntil_ && places > 0;

    // Blind: its weak points bare (each put out takes its share of its health), its veil down.
    if (blindTicks_ > 0) {
        if (weak_.step(w, b, c) > 0)
            g.say("GROUPE · Point faible crevé (" + std::to_string(weak_.total - weak_.left()) + " / " + std::to_string(weak_.total) + ")", AK::Group);
        if (!b.alive()) return;
        if (--blindTicks_ <= 0 || !veilPhase) endBlind(w, g, c, veilPhase);
    }
    if (veilPhase && !veil_ && !veilSaid_) {
        veilSaid_ = true;
        flashClock_ = steps(6.0f);  // its first flash a few seconds in
        g.say("GIGABOSS · Skullmaw se voile : quand son regard se charge, tenez " + std::to_string(need_) + " miroirs (sur " + std::to_string(places) +
                  ") ; les autres, derrière une colonne !",
              AK::Danger);
    }
    veil_ = veilPhase;

    stepGazes(w, b, c, phase);

    // « Regard de mort »: it charges, then flashes (its veil up, not blind).
    if (!veil_ || blindTicks_ > 0) {
        chargeTicks_ = 0;
        return;
    }
    if (chargeTicks_ > 0) {
        if (chargeTicks_ % 12 == 0) w.particles().emit("charge_spark", brow(b), 1.0f);
        if (--chargeTicks_ == 0) flash(w, g, b, c);
        return;
    }
    if (--flashClock_ <= 0) {
        chargeTicks_ = std::max(1, steps(charge_));
        w.playSound("sounds/laser_charge.wav", brow(b), 1.0f, 0.7f);
        g.say("DANGER · Son regard se charge : " + std::to_string(need_) + " miroirs tenus le renvoient, les autres derrière une colonne !", AK::Danger);
    }
}

void SkullmawFight::stepGazes(GameWorld& w, Enemy& b, Contributions& c, int phase) {
    if (blindTicks_ > 0 || b.lastStand() || gazePeriods_.empty()) {
        gazes_.clear();  // blind, it sees nobody
        return;
    }
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_)), fireS = std::max(1, steps(fire_));
    if (gazeClock_ <= 0) {
        const float period = mech::byPhase(gazePeriods_, phase, 8.0f) / std::max(1.0f, b.pressure());
        gazeClock_ = std::max(trackS + lockS + fireS + 1, steps(period));
        gazes_.clear();
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
            if (!mech::inPlay(w, i)) continue;
            Gaze z;
            z.seat = i;
            z.eye = (i + volleys_) % static_cast<int>(eyes_.size());
            z.from = eyeOf(b, z.eye);
            z.aim = w.playerAt(i).box.center();
            z.end = z.aim;
            gazes_.push_back(z);
        }
        ++volleys_;
    }
    --gazeClock_;
    for (Gaze& z : gazes_) {
        ++z.t;
        z.from = eyeOf(b, z.eye);
        if (z.t <= trackS && mech::inPlay(w, z.seat)) z.aim = w.playerAt(z.seat).box.center();  // it follows its player
        Vec2 dir = z.aim - z.from;
        const float len = dir.length();
        dir = len > 0.5f ? dir * (1.0f / len) : Vec2{0.0f, 1.0f};
        const Vec2 to = z.from + dir * reach_;
        const auto wall = w.physics().raycast(z.from, to);  // as far as the first wall
        z.end = wall ? z.from + (to - z.from) * *wall : to;
        const int fireAt = trackS + lockS;
        if (z.t == fireAt + 1) {
            w.playSound("sounds/laser_fire.wav", z.from, 0.55f, 1.1f);
            w.particles().emit("laser_sparks", z.end, 1.0f);
        }
        if (z.t > fireAt && z.t <= fireAt + fireS) {
            for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
                if (!mech::inPlay(w, i) || !Collision::segmentIntersectsRect(z.from, z.end, w.playerAt(i).box.expanded(width_ * 0.5f))) continue;
                mech::hurtSeat(w, b, i, gazeDamage_, z.from, 220.0f, kGaze);
                if (i == z.seat) z.hit = true;
            }
        }
        // Its player out of the line in time: a blow avoided.
        if (z.t == fireAt + fireS && !z.hit && mech::inPlay(w, z.seat)) c.dodged(z.seat, gazeDamage_);
    }
    std::erase_if(gazes_, [&](const Gaze& z) { return z.t >= trackS + lockS + fireS; });
}

void SkullmawFight::flash(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    using AK = Gigaboss::AlertKind;
    const Vec2 eye = brow(b);
    const int held = mirrors_.held();
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
        if (!mech::inPlay(w, i)) continue;
        if (mirrors_.holds(i)) {
            c.vent(i);  // a mirror held at its flash: the mechanism, and its gaze thrown back
            continue;
        }
        // Seen (nothing solid between its eyes and them): blinded and burnt; hidden: avoided.
        const bool seen = !w.physics().raycast(eye, w.playerAt(i).box.center()).has_value();
        if (seen)
            mech::hurtSeat(w, b, i, flashDamage_, eye, 260.0f, kFlash);
        else
            c.dodged(i, flashDamage_);
    }
    lastFlash_ = std::max<uint32_t>(1, g.fightTicks());
    lastHeld_ = held;
    flashClock_ = std::max(steps(charge_) + 1, steps(mech::byPhase(flashPeriods_, phase_ - veilFrom_ + 1, 14.0f)));
    w.flash({255, 236, 255, 255}, 0.35f);
    w.shake(0.5f);
    w.playSound("sounds/star_roar.wav", eye, 1.0f, 1.2f);
    w.particles().emit("portal_burst", eye, 2.0f);
    if (held >= need_) {
        // Its own gaze thrown back by the mirrors: blind, its eyes and crystal bare.
        blindTicks_ = std::max(1, steps(blind_));
        weak_.spawn(w, b, g.scaledFor());
        c.setWeak(weak_.uids);
        gazes_.clear();
        w.shake(0.8f);
        w.playSound("sounds/shell_shatter.wav", eye, 1.0f);
        g.say("GIGABOSS · Les miroirs lui renvoient son regard : Skullmaw est aveuglé ! Crevez ses yeux et son cristal (" +
                  std::to_string(static_cast<int>(std::lround(blind_))) + " s) !",
              AK::Branch);
    } else {
        g.say("GIGABOSS · Miroirs tenus " + std::to_string(held) + " / " + std::to_string(need_) + " : son voile tient !", AK::Danger);
    }
}

void SkullmawFight::endBlind(GameWorld& w, Gigaboss& g, Contributions& c, bool veilAgain) {
    weak_.clear(w);
    c.setWeak(weak_.uids);
    blindTicks_ = 0;
    if (veilAgain) g.say("GIGABOSS · Skullmaw rouvre les yeux : son voile se reforme !", Gigaboss::AlertKind::Danger);
}

void SkullmawFight::stop(GameWorld& w, Contributions& c) {
    weak_.clear(w);
    c.setWeak(weak_.uids);
    blindTicks_ = chargeTicks_ = 0;
    gazes_.clear();
}

SkullmawFight::GazeState SkullmawFight::gazeState(const Gaze& z) const {
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_));
    return z.t <= trackS ? GazeState::Tracking : z.t <= trackS + lockS ? GazeState::Locked : GazeState::Firing;
}

float SkullmawFight::gazeProgress(const Gaze& z) const {
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_)), fireS = std::max(1, steps(fire_));
    switch (gazeState(z)) {
        case GazeState::Tracking: return static_cast<float>(z.t) / static_cast<float>(trackS);
        case GazeState::Locked: return static_cast<float>(z.t - trackS) / static_cast<float>(lockS);
        case GazeState::Firing: return static_cast<float>(z.t - trackS - lockS) / static_cast<float>(fireS);
    }
    return 0.0f;
}

float SkullmawFight::chargeLeft() const { return static_cast<float>(std::max(0, chargeTicks_)) / static_cast<float>(kSimHz); }

float SkullmawFight::chargeRatio() const {
    const int all = std::max(1, steps(charge_));
    return chargeTicks_ > 0 ? 1.0f - static_cast<float>(chargeTicks_) / static_cast<float>(all) : 0.0f;
}

float SkullmawFight::blindLeft() const { return static_cast<float>(std::max(0, blindTicks_)) / static_cast<float>(kSimHz); }

float SkullmawFight::searchAngle(uint32_t t) const {
    const float s = static_cast<float>(t) / static_cast<float>(kSimHz) / searchPeriod_;
    return 1.5708f + 0.95f * std::sin(s * 6.2832f);  // down, swinging from one wall to the other
}

uint64_t SkullmawFight::hash() const {
    uint64_t h = 0x5c011a3a5ULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 8) | (veil_ ? 1u : 0u) | (veilSaid_ ? 2u : 0u));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(gazeClock_)) << 32) | static_cast<uint32_t>(volleys_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(flashClock_)) << 32) | static_cast<uint32_t>(chargeTicks_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(blindTicks_)) << 32) | static_cast<uint32_t>(need_));
    h = mix(h, (static_cast<uint64_t>(lastFlash_) << 8) | static_cast<uint32_t>(lastHeld_));
    for (const Gaze& z : gazes_)
        h = mix(h, (static_cast<uint64_t>(z.seat & 0xFF) << 56) ^ (static_cast<uint64_t>(z.eye) << 48) ^ (static_cast<uint64_t>(z.t) << 24) ^
                       static_cast<uint64_t>(std::lround(z.aim.x * 4.0f)) ^ (static_cast<uint64_t>(std::lround(z.aim.y * 4.0f)) << 12) ^ (z.hit ? 1ULL << 63 : 0));
    mirrors_.hashInto(h);
    weak_.hashInto(h);
    return h;
}

// ------------------------------------------------------------------------ display
std::string SkullmawFight::fallText(const std::string& name) const {
    return "GIGABOSS · " + name + " est vaincu ! Le crâne d'Asterion s'éteint : le titan est aveuglé.";
}
std::string SkullmawFight::goneText(const std::string& name) const { return "GIGABOSS · L'antre est silencieux : " + name + " est déjà tombé."; }
std::string SkullmawFight::goneLine(const std::string& name) const { return name + " est déjà tombé : le crâne est éteint."; }
std::string SkullmawFight::fallCard() const {
    return "Le crâne d'Asterion s'éteint : le titan est aveuglé. Les résultats comptent dès que la majorité de votre instance les a confirmés au "
           "serveur.";
}

void SkullmawFight::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const int places = static_cast<int>(mirrors_.places.size());
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    if (blindTicks_ > 0) {
        out.push_back({secs(blindTicks_), "Skullmaw est aveuglé (" + ph + ") : crevez ses yeux et son cristal · points faibles " +
                                              std::to_string(weak_.total - weak_.left()) + " / " + std::to_string(weak_.total),
                       kGreen});
    } else if (chargeTicks_ > 0) {
        out.push_back({secs(chargeTicks_), "Son regard se charge : miroirs tenus " + std::to_string(mirrors_.held()) + " / " + std::to_string(need_) +
                                               " · les autres, cachez-vous derrière une colonne !",
                       kRed});
    } else if (veil_) {
        out.push_back({std::to_string(need_) + " / " + std::to_string(places),
                       "Voile psychique (" + ph + ") : rien ne le blesse. À son flash, " + std::to_string(need_) +
                           " miroirs tenus lui renvoient son regard et l'aveuglent.",
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Skullmaw · " + ph + " : chaque regard laser vous suit puis se fige : quittez sa ligne à cet instant.", kOrange});
    }
    if (dark()) out.push_back({"✦", "Ténèbres : la lumière de son œil balaie l'arène et révèle les plateformes ; les miroirs brillent.", kViolet});
}

void SkullmawFight::threats(const GameWorld& w, std::vector<Vec2>& out) const {
    (void)w;
    for (const Gaze& z : gazes_)
        if (gazeState(z) != GazeState::Tracking) out.push_back(z.aim);
    if (chargeTicks_ > 0)
        for (const auto& m : mirrors_.places) out.push_back(m.area.center());
}

void SkullmawFight::targets(std::vector<uint32_t>& out) const {
    for (uint32_t u : weak_.uids)
        if (u) out.push_back(u);
}

}  // namespace cosmic::raid
