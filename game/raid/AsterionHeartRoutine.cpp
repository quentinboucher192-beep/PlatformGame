#include "game/raid/AsterionHeartRoutine.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "game/enemies/BossHazards.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/Gigaboss.h"

namespace cosmic::raid {

namespace {
std::vector<float> floatsOf(const Json& j, std::vector<float> fallback) {
    std::vector<float> out;
    if (j.is_array())
        for (const auto& v : j)
            if (v.is_number()) out.push_back(v.get<float>());
    return out.empty() ? fallback : out;
}
float at(const std::vector<float>& v, int phase, float fallback) {
    return v.empty() ? fallback : v[static_cast<size_t>(std::clamp(phase - 1, 0, static_cast<int>(v.size()) - 1))];
}
std::string secs(int ticks) { return std::to_string(std::max(0, (ticks + kSimHz - 1) / kSimHz)) + " s"; }
std::string pct(int units) { return std::to_string(units / 100) + "," + std::to_string((units % 100) / 10) + " %"; }
const Color kGold{255, 210, 122, 255};
const Color kOrange{255, 154, 90, 255};
const Color kViolet{180, 108, 255, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 120, 120, 255};
const Color kWhite{255, 248, 230, 255};
constexpr const char* kParts[] = {"crane", "bras_gauche", "bras_droit", "torse", "abdomen", "jambe_gauche", "jambe_droite"};
constexpr const char* kEffects[] = {
    "Crâne tombé : ses lances stellaires perdent leur précision",
    "Bras gauche tombé : plus de balayages des artères",
    "Bras droit tombé : plus de pluie de magma",
    "Torse tombé : sa cage est brisée, la voie de ses ventricules s'ouvre",
    "Abdomen tombé : plus d'engeances en renfort",
    "Jambe gauche tombée : plus de pièges de gravité",
    "Jambe droite tombée : ses charges stellaires ralentissent",
};
}  // namespace

const char* AsterionHeartRoutine::partId(int p) { return p >= 0 && p < PartCount ? kParts[p] : ""; }
const char* AsterionHeartRoutine::partEffect(int p) { return p >= 0 && p < PartCount ? kEffects[p] : ""; }

void AsterionHeartRoutine::begin(GameWorld& w, const GigabossRules& r) {
    (void)w;
    *this = AsterionHeartRoutine{};
    rules_ = &r;
    const Json& g = r.extra;
    const Json heart = g.value("heart", Json::object());
    relicShare_ = std::clamp(jget<float>(heart, "relicShare", relicShare_), 0.0f, 0.1f);
    reportEvery_ = std::clamp(jget<float>(heart, "reportEvery", reportEvery_), 5.0f, 60.0f);
    cageExposed_ = std::clamp(jget<float>(g.value("cage", Json::object()), "exposed", cageExposed_), 0.2f, 1.0f);
    const Json la = g.value("lances", Json::object());
    lanceName_ = jstr(la, "name", lanceName_);
    lancePeriods_ = floatsOf(la.value("periods", Json()), {8.0f, 7.0f, 6.0f, 5.0f});
    lanceTrack_ = std::clamp(jget<float>(la, "track", lanceTrack_), 0.0f, 4.0f);
    lanceWarn_ = std::clamp(jget<float>(la, "warn", lanceWarn_), 0.4f, 4.0f);
    lanceWidth_ = std::clamp(jget<float>(la, "width", lanceWidth_), 16.0f, 160.0f);
    lanceDamage_ = std::clamp(jget<float>(la, "damage", lanceDamage_), 0.0f, 4.0f);
    const std::vector<float> miss = floatsOf(la.value("miss", Json()), {48.0f, 120.0f});
    if (miss.size() == 2 && miss[1] >= miss[0]) missMin_ = miss[0], missMax_ = miss[1];
    const Json sw = g.value("sweeps", Json::object());
    sweepName_ = jstr(sw, "name", sweepName_);
    sweepPeriods_ = floatsOf(sw.value("periods", Json()), {0.0f, 11.0f, 9.0f, 7.0f});
    sweepWarn_ = std::clamp(jget<float>(sw, "warn", sweepWarn_), 0.4f, 4.0f);
    sweepSpeed_ = std::clamp(jget<float>(sw, "speed", sweepSpeed_), 100.0f, 2400.0f);
    sweepHeight_ = std::clamp(jget<float>(sw, "height", sweepHeight_), 16.0f, 120.0f);
    sweepDamage_ = std::clamp(jget<float>(sw, "damage", sweepDamage_), 0.0f, 4.0f);
    sweepRows_ = floatsOf(sw.value("rows", Json()), {});
    const Json ra = g.value("rain", Json::object());
    rainName_ = jstr(ra, "name", rainName_);
    rainPeriods_ = floatsOf(ra.value("periods", Json()), {10.0f, 9.0f, 8.0f, 7.0f});
    for (float v : floatsOf(ra.value("count", Json()), {6, 7, 8, 10})) rainCount_.push_back(std::clamp(static_cast<int>(v), 1, 16));
    rainWarn_ = std::clamp(jget<float>(ra, "warn", rainWarn_), 0.4f, 4.0f);
    rainWidth_ = std::clamp(jget<float>(ra, "width", rainWidth_), 16.0f, 160.0f);
    rainDamage_ = std::clamp(jget<float>(ra, "damage", rainDamage_), 0.0f, 4.0f);
    const Json we = g.value("wells", Json::object());
    wellName_ = jstr(we, "name", wellName_);
    wellPeriods_ = floatsOf(we.value("periods", Json()), {0.0f, 12.0f, 10.0f, 9.0f});
    wellWarn_ = std::clamp(jget<float>(we, "warn", wellWarn_), 0.4f, 4.0f);
    wellTime_ = std::clamp(jget<float>(we, "time", wellTime_), 0.5f, 8.0f);
    wellSpeed_ = jget<float>(we, "speed", wellSpeed_);
    wellRange_ = jget<float>(we, "range", wellRange_);
    wellCore_ = jget<float>(we, "core", wellCore_);
    wellDamage_ = jget<float>(we, "damage", wellDamage_);
    const Json ch = g.value("charges", Json::object());
    chargeName_ = jstr(ch, "name", chargeName_);
    chargePeriods_ = floatsOf(ch.value("periods", Json()), {0.0f, 13.0f, 11.0f, 9.0f});
    chargeWarn_ = std::clamp(jget<float>(ch, "warn", chargeWarn_), 0.4f, 4.0f);
    chargeSpeed_ = std::clamp(jget<float>(ch, "speed", chargeSpeed_), 100.0f, 2400.0f);
    chargeHeight_ = std::clamp(jget<float>(ch, "height", chargeHeight_), 16.0f, 140.0f);
    chargeDamage_ = std::clamp(jget<float>(ch, "damage", chargeDamage_), 0.0f, 4.0f);
    chargeSlow_ = std::clamp(jget<float>(ch, "slow", chargeSlow_), 0.2f, 1.0f);
    chargeRows_ = floatsOf(ch.value("rows", Json()), {});
    const Json sp = g.value("spawns", Json::object());
    spawnPeriods_ = floatsOf(sp.value("periods", Json()), {0.0f, 22.0f, 20.0f, 18.0f});
    for (const auto& k : sp.value("kinds", Json::array()))
        if (k.is_string()) spawnKinds_.push_back(k.get<std::string>());
    spawnMax_ = std::clamp(jget<int>(sp, "max", spawnMax_), 1, 16);
    const Json ti = g.value("tide", Json::object());
    tideName_ = jstr(ti, "name", tideName_);
    tidePeriods_ = floatsOf(ti.value("periods", Json()), {0.0f, 0.0f, 24.0f, 18.0f});
    tideWarnS_ = std::clamp(jget<float>(ti, "warn", tideWarnS_), 1.0f, 6.0f);
    tideDuration_ = std::clamp(jget<float>(ti, "duration", tideDuration_), 2.0f, 12.0f);
    tideHeight_ = std::clamp(jget<float>(ti, "height", tideHeight_), 16.0f, 160.0f);
    tideDamage_ = std::clamp(jget<float>(ti, "damage", tideDamage_), 0.0f, 4.0f);
    const Json nv = g.value("supernova", Json::object());
    novaName_ = jstr(nv, "name", novaName_);
    novaPeriod_ = std::clamp(jget<float>(nv, "period", novaPeriod_), 10.0f, 90.0f);
    novaCharge_ = std::clamp(jget<float>(nv, "charge", novaCharge_), 2.0f, 15.0f);
    novaDamage_ = std::clamp(jget<float>(nv, "damage", novaDamage_), 0.0f, 6.0f);
    for (float v : floatsOf(nv.value("need", Json()), {1, 2, 2, 2, 3, 3, 4, 4})) novaNeed_.push_back(std::clamp(static_cast<int>(v), 1, 8));
    const std::vector<float> span = floatsOf(g.value("span", Json()), {});
    if (span.size() == 2 && span[1] > span[0]) spanX0_ = span[0], spanX1_ = span[1];
    floorY_ = jget<float>(g, "floor", 0.0f);
    const Json mods = g.value("modifiers", Json::object());
    for (int p = 0; p < PartCount; ++p) modifiers_.push_back(jstr(mods, kParts[p], std::string("coeur_") + kParts[p]));
    if (spawnKinds_.empty()) spawnKinds_ = {"etincelle"};
    lanceClock_ = steps(5.0f);
    sweepClock_ = steps(6.0f);
    chargeClock_ = steps(8.0f);
    rainClock_ = steps(7.0f);
    wellClock_ = steps(9.0f);
    spawnClock_ = steps(12.0f);
    tideClock_ = steps(10.0f);
    novaClock_ = steps(8.0f);
}

int AsterionHeartRoutine::clockFor(const std::vector<float>& periods, int ph, const Enemy& b) const {
    const float p = at(periods, ph, 0.0f);
    if (p <= 0.0f) return 0;
    return std::max(steps(2.5f), steps(p / std::max(1.0f, b.pressure())));
}

float AsterionHeartRoutine::bandFront(const Band& v) const { return v.x0 + static_cast<float>(v.dir) * v.speed * static_cast<float>(v.t) / static_cast<float>(kSimHz); }

int AsterionHeartRoutine::sweeps() const {
    return static_cast<int>(std::count_if(bands_.begin(), bands_.end(), [](const Band& v) { return !v.comet; }));
}

int AsterionHeartRoutine::charges() const {
    return static_cast<int>(std::count_if(bands_.begin(), bands_.end(), [](const Band& v) { return v.comet; }));
}

float AsterionHeartRoutine::novaLeft() const { return static_cast<float>(std::max(0, novaLeft_)) / static_cast<float>(kSimHz); }

void AsterionHeartRoutine::updateOwn(const Enemy& b) {
    if (b.maxHp() <= 0.0f) return;
    const int taken = static_cast<int>(std::lround((1.0f - std::clamp(b.hp() / b.maxHp(), 0.0f, 1.0f)) * static_cast<float>(kHeartUnits)));
    own_ = std::clamp(taken - applied_, own_, kHeartUnits);  // never down (its weak points' wounds included)
    remaining_ = std::clamp(kHeartUnits - applied_ - own_, 0, kHeartUnits);
}

// ------------------------------------------------------------------------ the shared health
void AsterionHeartRoutine::applyShared(GameWorld& w, Gigaboss& g, Enemy& b) {
    const HeartShared& sh = shared(g);
    if (!relicsApplied_) {
        // The relics of the seven branches validated when the fight begins: each one takes its share of
        // its health for this whole fight (for every instance alike: never counted as anyone's damage).
        relicsApplied_ = true;
        relics_ = std::clamp<int>(sh.relics, 0, 7);
        if (relics_ > 0) {
            b.scaleHealth(std::max(0.5f, 1.0f - relicShare_ * static_cast<float>(relics_)));
            alert(g, "TITAN · " + std::to_string(relics_) + (relics_ > 1 ? " reliques des branches affaiblissent" : " relique des branches affaiblit") +
                         " le Cœur (−" + std::to_string(static_cast<int>(std::lround(relicShare_ * 100.0f * static_cast<float>(relics_)))) + " % de sa vie)",
                  Branch);
        }
    }
    // What the other instances took (validated, summed by the server): taken here at this same step on
    // every computer of the instance.
    const int others = std::clamp<int>(sh.others, 0, kHeartUnits);
    if (others > applied_ && b.alive() && !b.lastStand()) {
        const int add = others - applied_;
        applied_ = others;
        b.wound(w, static_cast<float>(add) / static_cast<float>(kHeartUnits) * b.maxHp());
        if (add >= 100) alert(g, "MONDE · Les autres groupes arrachent " + pct(add) + " de sa vie au Cœur !", Branch);
    } else if (others > applied_) {
        applied_ = others;
    }
    // The shared phase: never above the health of that phase here (the same step everywhere); what it
    // takes is the others' part, never this instance's own.
    const auto& ph = b.def().phases;
    if (sh.phase >= 2 && sh.phase <= static_cast<int>(ph.size()) && b.alive() && !b.lastStand() && b.maxHp() > 0.0f) {
        const float cap = ph[static_cast<size_t>(sh.phase - 1)].hpThreshold;
        if (b.hp() / b.maxHp() > cap + 0.0005f) {
            const float cut = b.hp() - cap * b.maxHp();
            applied_ += static_cast<int>(std::lround(cut / b.maxHp() * static_cast<float>(kHeartUnits)));
            b.wound(w, cut);
        }
    }
    updateOwn(b);
}

void AsterionHeartRoutine::step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    ticks_ = ticks(g);
    phase_ = phase(g);
    players_ = static_cast<int>(playing(w).size());
    uint8_t parts = 0;
    for (int p = 0; p < PartCount; ++p)
        if (titanLinkOn(g, modifiers_[static_cast<size_t>(p)])) parts |= static_cast<uint8_t>(1u << p);
    if (parts != parts_ && ticks_ > 1) {
        for (int p = 0; p < PartCount; ++p)
            if ((parts & ~parts_) & (1u << p)) alert(g, std::string("TITAN · ") + kEffects[p], Branch);
    }
    parts_ = parts;
    applyShared(w, g, b);
    if (!b.alive()) return;
    if (blast_ > 0) ++blast_;
    if (blast_ > steps(1.2f)) blast_ = 0;
    const int ph = b.lastStand() ? 4 : std::max(1, phase_);
    stepLances(w, c, g, b, ph);
    stepBands(w, g, b, ph);
    stepRain(w, g, b, ph);
    stepWells(w, g, b, ph);
    stepSpawns(w, g, b, ph);
    stepTide(w, g, b, ph);
    if (b.lastStand()) {
        setFight(g);
        return;
    }
    if (ph >= 4) stepSupernova(w, c, g, b);
    else stepArmour(w, c, g, b, ph);
    updateOwn(b);
}

// ------------------------------------------------------------------------ its armour: the arteries, its cage
void AsterionHeartRoutine::stepArmour(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int ph) {
    const GigabossRules& r = *rules_;
    const bool cage = !down(Torse);
    const bool armourPhase = (ph >= r.carapaceFrom && ph < r.carapaceUntil) || cage;
    if (exposed(g)) {
        if (exposedStep(g, w, c, b)) return;
        resetPlates(g);
        if (armourPhase) {
            setArmour(g, true);
            alert(g, cage ? "GIGABOSS · La cage se referme sur le Cœur : tenez de nouveau les quatre artères !"
                          : "GIGABOSS · Les artères se referment : tenez-les de nouveau ensemble !",
                  Danger);
        } else {
            setFight(g);
        }
        return;
    }
    if (!armourPhase) {
        if (armoured(g)) alert(g, "GIGABOSS · Ses artères cèdent : le Cœur bat à découvert, frappez-le !", Branch);
        setFight(g);
        return;
    }
    if (!armoured(g)) {
        setArmour(g, true);
        resetPlates(g);
        if (cage && !cageSaid_) {
            alert(g, "GIGABOSS · Le Torse tient encore : une cage d'os garde le Cœur ! Tenez ses quatre artères ensemble pour l'ouvrir.", Danger);
            cageSaid_ = true;
        } else if (!armourSaid_) {
            alert(g, "GIGABOSS · Ses artères se gonflent : un joueur sur chacune des quatre artères, ensemble (" +
                         std::to_string(static_cast<int>(std::lround(r.hold * 10.0f)) / 10) + " s) !",
                  Danger);
            armourSaid_ = true;
        }
    }
    if (holdPlates(g, w, c, b, {"GROUPE · Artère tenue", "GIGABOSS · Une artère se rouvre : tenez-la de nouveau !"})) {
        expose(g, w, c, b,
               "GIGABOSS · Les artères sont tenues : ses ventricules sont à nu (" + std::to_string(static_cast<int>(std::lround(r.exposed * (cage ? cageExposed_ : 1.0f)))) +
                   " s) ! Brisez-les !");
        if (cage) setExposedSeconds(g, r.exposed * cageExposed_);
    }
}

// ------------------------------------------------------------------------ its supernova (phase 4)
void AsterionHeartRoutine::stepSupernova(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    const GigabossRules& r = *rules_;
    if (armoured(g)) setFight(g);
    if (exposed(g)) {
        if (exposedStep(g, w, c, b)) return;
        setFight(g);
    }
    if (!novaSaid_) {
        novaSaid_ = true;
        novaClock_ = steps(6.0f);
        alert(g, "GIGABOSS · SUPERNOVA : le Cœur s'effondre ! Quand il se charge, montez sur les ancres (les artères), assez de joueurs à la fois !",
              Danger);
    }
    const int inPlayNow = std::max(1, players_);
    anchorsNeed_ = novaNeed_.empty() ? 1 : novaNeed_[static_cast<size_t>(std::clamp(inPlayNow, 1, static_cast<int>(novaNeed_.size())) - 1)];
    anchorsNeed_ = std::min(anchorsNeed_, std::max(1, static_cast<int>(r.vents.size())));
    // The anchors held now: a plate with a player standing on it.
    anchorMask_ = 0;
    anchorsHeld_ = 0;
    uint8_t holders = 0;
    for (size_t k = 0; k < r.vents.size() && k < 8; ++k)
        for (int seat : playing(w))
            if (r.vents[k].intersects(w.playerAt(seat).box)) {
                anchorMask_ |= static_cast<uint8_t>(1u << k);
                holders |= static_cast<uint8_t>(1u << seat);
            }
    for (size_t k = 0; k < 8; ++k) anchorsHeld_ += (anchorMask_ >> k) & 1u;
    if (novaLeft_ > 0) {
        if (--novaLeft_ > 0) {
            if (novaLeft_ % steps(1.0f) == 0) w.shake(0.2f);
            return;
        }
        if (anchorsHeld_ >= anchorsNeed_) {
            // Contained: everyone on an anchor counts a mechanism; its ventricles are bare.
            ++contained_;
            for (int i = 0; i < 8; ++i)
                if (holders & (1u << i)) c.vent(i);
            w.flash({255, 236, 180, 255}, 0.35f);
            w.playSound("sounds/shield_block.wav", b.box.center(), 1.0f, 0.6f);
            alert(g, "GROUPE · Supernova contenue sur " + std::to_string(anchorsHeld_) + " ancres !", Group);
            expose(g, w, c, b, "GIGABOSS · La supernova contenue déchire le Cœur : ses ventricules sont à nu (" +
                                   std::to_string(static_cast<int>(std::lround(r.exposed))) + " s) !");
        } else {
            // Not contained: it burns the whole arena (those on an anchor are sheltered).
            blast_ = 1;
            w.shake(1.2f);
            w.flash({255, 255, 255, 255}, 0.6f);
            w.playSound("sounds/supernova.wav", b.box.center(), 1.0f, 0.9f);
            for (int seat : playing(w)) {
                if (holders & (1u << seat)) continue;
                const Vec2 cc = w.playerAt(seat).box.center();
                strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, novaDamage_, 320.0f, novaName_);
            }
            alert(g, "DANGER · La supernova n'est pas contenue (" + std::to_string(anchorsHeld_) + " / " + std::to_string(anchorsNeed_) +
                         " ancres) : l'arène brûle !",
                  Danger);
        }
        novaClock_ = std::max(steps(10.0f), steps(novaPeriod_ / std::max(1.0f, b.pressure())));
        return;
    }
    if (exposed(g)) return;
    if (--novaClock_ > 0) return;
    novaLeft_ = steps(novaCharge_ * slack(inPlayNow));
    w.playSound("sounds/laser_charge.wav", b.box.center(), 1.0f, 0.5f);
    alert(g, "DANGER · SUPERNOVA dans " + secs(novaLeft_) + " : " + std::to_string(anchorsNeed_) + (anchorsNeed_ > 1 ? " joueurs sur les ancres !" : " joueur sur une ancre !"),
          Danger);
}

// ------------------------------------------------------------------------ star lances (every player)
void AsterionHeartRoutine::stepLances(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int ph) {
    const bool blind = down(Crane);
    for (Lance& l : lances_) {
        if (l.track > 0) {
            --l.track;
            if (l.seat >= 0 && l.seat < w.teamSize()) l.at = w.playerAt(l.seat).feet();
            continue;
        }
        if (l.left > -steps(0.3f)) --l.left;
        if (l.left > 0) continue;
        const Rect col{l.at.x - lanceWidth_ * 0.5f, l.at.y - 260.0f, lanceWidth_, 264.0f};
        if (l.left == 0) {
            l.hit = l.seat >= 0 && l.seat < w.teamSize() && col.intersects(w.playerAt(l.seat).box);
            if (!l.hit && l.seat >= 0) c.dodged(l.seat, lanceDamage_);
            w.particles().emit("impulse_ring", {l.at.x, l.at.y - 4.0f}, 0.8f);
        }
        strike(w, b, col, lanceDamage_, 240.0f, lanceName_);
    }
    std::erase_if(lances_, [&](const Lance& l) { return l.track <= 0 && l.left <= -steps(0.3f); });
    if (--lanceClock_ > 0) return;
    lanceClock_ = clockFor(lancePeriods_, ph, b);
    if (lanceClock_ <= 0) lanceClock_ = steps(6.0f);
    ++lanceRound_;
    for (int seat : playing(w)) {
        if (spared(seat)) continue;
        Lance l;
        l.seat = seat;
        l.at = w.playerAt(seat).feet();
        if (blind) {
            // Blinded by the Skull's fall: no tracking, and it lands beside its player.
            const float n = noise(static_cast<uint32_t>(lanceRound_ * 131 + seat * 17 + 7));
            const float off = missMin_ + (missMax_ - missMin_) * n;
            l.at.x += (noise(static_cast<uint32_t>(lanceRound_ * 71 + seat * 29 + 3)) < 0.5f ? -off : off);
            l.track = 0;
            l.left = steps(lanceWarn_ + 0.4f);
        } else {
            l.track = steps(lanceTrack_);
            l.left = steps(lanceWarn_);
        }
        lances_.push_back(l);
    }
    if (!lances_.empty())
        alert(g, blind ? "DANGER · " + lanceName_ + " : aveuglé, le Cœur vise à côté, éloignez-vous des marques !"
                       : "DANGER · " + lanceName_ + " : une marque suit chacun, puis se fige — quittez-la !",
              Danger);
}

// ------------------------------------------------------------------------ sweeps and charges along storeys
void AsterionHeartRoutine::stepBands(GameWorld& w, Gigaboss& g, Enemy& b, int ph) {
    for (Band& v : bands_) {
        if (v.warn > 0) {
            --v.warn;
            continue;
        }
        ++v.t;
        const float x = bandFront(v);
        const Rect band{x - 24.0f, v.y - v.height, 48.0f, v.height - 2.0f};
        for (int i : playing(w)) {
            if ((v.hit >> i) & 1u || !band.intersects(w.playerAt(i).box)) continue;
            v.hit |= static_cast<uint8_t>(1u << i);
            const Vec2 cc = w.playerAt(i).box.center();
            strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, v.damage, 300.0f, v.comet ? chargeName_ : sweepName_);
        }
        if (v.t % 5 == 0) w.particles().emit(v.comet ? "portal_burst" : "fire_burst", {x, v.y - v.height * 0.5f}, 0.5f);
    }
    std::erase_if(bands_, [&](const Band& v) { return v.warn <= 0 && (bandFront(v) < spanX0_ - 40.0f || bandFront(v) > spanX1_ + 40.0f); });
    const Vec2 heart = b.box.center();
    // Artery sweeps (Dreadfang standing): from the heart to both walls, along one storey, then two, then three.
    if (!down(BrasGauche) && !sweepRows_.empty() && at(sweepPeriods_, ph, 0.0f) > 0.0f && --sweepClock_ <= 0) {
        sweepClock_ = clockFor(sweepPeriods_, ph, b);
        const int rows = std::clamp(ph - 1, 1, static_cast<int>(sweepRows_.size()));
        for (int k = 0; k < rows; ++k) {
            const float y = sweepRows_[static_cast<size_t>((bandRound_ + k) % static_cast<int>(sweepRows_.size()))];
            for (int dir : {-1, 1}) {
                Band v;
                v.y = y;
                v.x0 = heart.x + static_cast<float>(dir) * 40.0f;
                v.dir = dir;
                v.warn = steps(sweepWarn_);
                v.speed = sweepSpeed_;
                v.height = sweepHeight_;
                v.damage = sweepDamage_;
                bands_.push_back(v);
            }
        }
        ++bandRound_;
        alert(g, "DANGER · " + sweepName_ + " sur " + std::to_string(rows) + (rows > 1 ? " étages" : " étage") + " : sautez la lame ou changez d'étage !", Danger);
    }
    // Star charges (from its 2nd phase): a comet runs a storey from one wall to the other.
    if (!chargeRows_.empty() && at(chargePeriods_, ph, 0.0f) > 0.0f && --chargeClock_ <= 0) {
        chargeClock_ = clockFor(chargePeriods_, ph, b);
        const bool slow = down(JambeDroite);
        Band v;
        v.comet = true;
        v.y = chargeRows_[static_cast<size_t>(bandRound_ % static_cast<int>(chargeRows_.size()))];
        v.dir = bandRound_ % 2 == 0 ? 1 : -1;
        v.x0 = v.dir > 0 ? spanX0_ : spanX1_;
        v.warn = steps(chargeWarn_ * (slow ? 1.6f : 1.0f));
        v.speed = chargeSpeed_ * (slow ? chargeSlow_ : 1.0f);
        v.height = chargeHeight_;
        v.damage = chargeDamage_;
        bands_.push_back(v);
        ++bandRound_;
        alert(g, "DANGER · " + chargeName_ + (slow ? " (ralentie : la Jambe droite est tombée)" : "") + " : quittez l'étage marqué !", Danger);
    }
}

// ------------------------------------------------------------------------ magma rain (Pyroclast standing)
void AsterionHeartRoutine::stepRain(GameWorld& w, Gigaboss& g, Enemy& b, int ph) {
    const int strikeT = steps(0.35f);
    for (Drop& d : drops_) {
        if (d.left > -strikeT) --d.left;
        if (d.left > 0) continue;
        const Rect col{d.x - rainWidth_ * 0.5f, floorY_ - 900.0f, rainWidth_, 904.0f};
        strike(w, b, col, rainDamage_, 200.0f, rainName_);
        if (d.left == 0) w.particles().emit("fire_burst", {d.x, floorY_ - 6.0f}, 1.0f);
    }
    std::erase_if(drops_, [&](const Drop& d) { return d.left <= -strikeT; });
    if (down(BrasDroit) || at(rainPeriods_, ph, 0.0f) <= 0.0f || --rainClock_ > 0) return;
    rainClock_ = clockFor(rainPeriods_, ph, b);
    const int n = rainCount_.empty() ? 6 : rainCount_[static_cast<size_t>(std::clamp(ph - 1, 0, static_cast<int>(rainCount_.size()) - 1))];
    ++rainRound_;
    for (int k = 0; k < n; ++k) {
        const float u = (static_cast<float>(k) + noise(static_cast<uint32_t>(rainRound_ * 977 + k * 31))) / static_cast<float>(n);
        drops_.push_back({spanX0_ + 40.0f + (spanX1_ - spanX0_ - 80.0f) * u, steps(rainWarn_)});
    }
    alert(g, "DANGER · " + rainName_ + " : " + std::to_string(n) + " colonnes de magma vont tomber !", Danger);
}

// ------------------------------------------------------------------------ gravity traps (Graviton standing)
void AsterionHeartRoutine::stepWells(GameWorld& w, Gigaboss& g, Enemy& b, int ph) {
    for (size_t k = 0; k < wells_.size();) {
        if (--wells_[k].left > 0) {
            ++k;
            continue;
        }
        BossHazards::Pull p;
        p.owner = b.uid();
        p.center = wells_[k].at;
        p.speed = wellSpeed_;
        p.range = wellRange_;
        p.core = wellCore_;
        p.damage = wellDamage_;
        p.time = wellTime_;
        w.hazards().addPull(p);
        w.particles().emit("portal_burst", wells_[k].at, 1.0f);
        wells_.erase(wells_.begin() + static_cast<std::ptrdiff_t>(k));
    }
    if (down(JambeGauche) || at(wellPeriods_, ph, 0.0f) <= 0.0f || exposed(g) || --wellClock_ > 0) return;
    wellClock_ = clockFor(wellPeriods_, ph, b);
    for (int seat : playing(w))
        if (!spared(seat)) wells_.push_back({w.playerAt(seat).feet() - Vec2{0.0f, 18.0f}, steps(wellWarn_)});
    if (!wells_.empty()) alert(g, "DANGER · " + wellName_ + " sous chacun : éloignez-vous !", Danger);
}

// ------------------------------------------------------------------------ spawn (Voidworm standing)
void AsterionHeartRoutine::stepSpawns(GameWorld& w, Gigaboss& g, Enemy& b, int ph) {
    std::erase_if(spawned_, [&](uint32_t u) {
        const Enemy* e = w.findEnemy(u);
        return !e || !e->alive();
    });
    if (down(Abdomen) || at(spawnPeriods_, ph, 0.0f) <= 0.0f || --spawnClock_ > 0) return;
    spawnClock_ = clockFor(spawnPeriods_, ph, b);
    const int n = std::min(1 + std::max(1, players_) / 2, spawnMax_ - static_cast<int>(spawned_.size()));
    if (n <= 0) return;
    ++spawnRound_;
    const Vec2 heart = b.box.center();
    for (int k = 0; k < n; ++k) {
        const float side = (k % 2 == 0) ? -1.0f : 1.0f;
        const Vec2 spot{heart.x + side * (260.0f + 60.0f * static_cast<float>(k / 2)), heart.y + 40.0f * noise(static_cast<uint32_t>(spawnRound_ * 53 + k))};
        const std::string& kind = spawnKinds_[static_cast<size_t>((spawnRound_ + k) % static_cast<int>(spawnKinds_.size()))];
        if (Enemy* e = w.spawnEnemy(kind, spot, side < 0.0f)) {
            e->summonedBy = b.uid();
            e->countsForStats = false;
            spawned_.push_back(e->uid());
            w.particles().emit("portal_burst", spot, 1.0f);
        }
    }
    alert(g, "DANGER · Des engeances jaillissent du Cœur !", Danger);
}

// ------------------------------------------------------------------------ the plasma tide (phases 3 and 4)
void AsterionHeartRoutine::stepTide(GameWorld& w, Gigaboss& g, Enemy& b, int ph) {
    if (tideLeft_ > 0) --tideLeft_;
    if (tideWarn_ > 0 && --tideWarn_ == 0) {
        tideLeft_ = steps(tideDuration_);
        BossHazards::Flood f;
        f.owner = b.uid();
        f.left = spanX0_;
        f.right = spanX1_;
        f.floorY = floorY_;
        f.height = tideHeight_;
        f.rise = 0.8f;
        f.hold = std::max(0.5f, tideDuration_ - 1.6f);
        f.fall = 0.8f;
        f.damage = tideDamage_;
        f.look = "plasma";
        f.color = Color{255, 196, 122, 255};
        w.hazards().addFlood(f);
        w.shake(0.4f);
        w.playSound("sounds/lava_bubble.wav", b.box.center(), 1.0f, 0.7f);
        alert(g, "DANGER · " + tideName_ + " : le sol brûle, restez sur les plateformes !", Danger);
    }
    if (at(tidePeriods_, ph, 0.0f) <= 0.0f || tideWarn_ > 0 || tideLeft_ > 0 || --tideClock_ > 0) return;
    tideClock_ = clockFor(tidePeriods_, ph, b);
    tideWarn_ = steps(tideWarnS_);
    alert(g, "DANGER · " + tideName_ + " dans " + secs(tideWarn_) + " : montez sur les plateformes !", Danger);
}

void AsterionHeartRoutine::fell(GameWorld& w, Gigaboss& g) {
    (void)g;
    for (uint32_t u : spawned_)
        if (Enemy* e = w.findEnemy(u); e && e->alive()) e->despawn();
    spawned_.clear();
    lances_.clear();
    bands_.clear();
    drops_.clear();
    wells_.clear();
    novaLeft_ = tideWarn_ = tideLeft_ = 0;
    own_ = std::clamp(kHeartUnits - applied_, own_, kHeartUnits);
    remaining_ = 0;
}

uint64_t AsterionHeartRoutine::hash() const {
    uint64_t h = 0xc0e0a57eULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 8) | parts_ | (static_cast<uint64_t>(relics_) << 16) | (relicsApplied_ ? 1ULL << 24 : 0) |
                   (cageSaid_ ? 1ULL << 25 : 0) | (armourSaid_ ? 1ULL << 26 : 0) | (novaSaid_ ? 1ULL << 27 : 0));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(applied_)) << 32) | static_cast<uint32_t>(own_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(lanceClock_)) << 32) | static_cast<uint32_t>(lanceRound_));
    for (const Lance& l : lances_)
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(l.left)) << 40) ^ (static_cast<uint64_t>(static_cast<uint32_t>(l.track)) << 24) ^
                       static_cast<uint64_t>(std::lround(l.at.x * 4.0f)) ^ (static_cast<uint64_t>(l.seat + 1) << 56));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(sweepClock_)) << 32) | static_cast<uint32_t>(chargeClock_));
    h = mix(h, static_cast<uint64_t>(bandRound_));
    for (const Band& v : bands_)
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(v.t)) << 32) ^ static_cast<uint64_t>(std::lround(v.x0 * 4.0f)) ^
                       (static_cast<uint64_t>(v.hit) << 24) ^ (static_cast<uint64_t>(static_cast<uint32_t>(v.warn)) << 44) ^ (v.dir > 0 ? 1ULL << 62 : 0) ^
                       (v.comet ? 1ULL << 63 : 0));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(rainClock_)) << 32) | static_cast<uint32_t>(rainRound_));
    for (const Drop& d : drops_) h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(d.left)) << 32) ^ static_cast<uint64_t>(std::lround(d.x * 4.0f)));
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(wellClock_)));
    for (const Well& q : wells_) h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(q.left)) << 32) ^ static_cast<uint64_t>(std::lround(q.at.x * 4.0f)));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(spawnClock_)) << 32) | static_cast<uint32_t>(spawnRound_));
    for (uint32_t u : spawned_) h = mix(h, u);
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(tideClock_)) << 40) ^ (static_cast<uint64_t>(static_cast<uint32_t>(tideWarn_)) << 20) ^
                   static_cast<uint32_t>(tideLeft_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(novaClock_)) << 32) | static_cast<uint32_t>(novaLeft_));
    h = mix(h, (static_cast<uint64_t>(anchorMask_) << 16) | (static_cast<uint64_t>(contained_) << 24) | static_cast<uint32_t>(blast_));
    return h;
}

// ------------------------------------------------------------------------ display
void AsterionHeartRoutine::marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const {
    const Enemy* b = g.boss(w);
    const float lanceWarnSteps = static_cast<float>(std::max(1, steps(lanceWarn_)));
    for (const Lance& l : lances_) {
        BossMark m;
        m.kind = BossMark::Kind::Target;
        const float half = lanceWidth_ * 0.5f;
        m.area = {l.at.x - half, l.at.y - half * 2.0f, half * 2.0f, half * 2.0f};
        m.progress = l.track > 0 ? 0.0f : std::clamp(1.0f - static_cast<float>(l.left) / lanceWarnSteps, 0.0f, 1.0f);
        m.color = l.track > 0 ? kViolet : kGold;
        m.strong = l.left <= 0 && l.track <= 0;
        if (l.track <= 0 && l.left <= steps(0.35f)) m.clip = "lance";
        out.push_back(m);
    }
    for (const Band& v : bands_) {
        if (v.warn > 0) {  // its storey announced: a band along it
            BossMark m;
            m.kind = BossMark::Kind::Zone;
            const float x0 = v.comet ? spanX0_ : (v.dir < 0 ? spanX0_ : v.x0), x1 = v.comet ? spanX1_ : (v.dir < 0 ? v.x0 : spanX1_);
            m.area = {x0, v.y - v.height, std::max(1.0f, x1 - x0), v.height};
            m.color = v.comet ? kViolet : kOrange;
            m.progress = 1.0f - static_cast<float>(v.warn) / static_cast<float>(std::max(1, steps(v.comet ? chargeWarn_ : sweepWarn_)));
            m.label = v.comet ? chargeName_ : sweepName_;
            out.push_back(m);
            continue;
        }
        BossMark m;
        m.kind = BossMark::Kind::Wave;
        const float x = bandFront(v);
        m.area = {x - 24.0f, v.y - 4.0f, 48.0f, 4.0f};
        m.clip = v.comet ? "comet" : "blade";
        m.flip = v.dir < 0;
        m.strong = true;
        m.color = v.comet ? kViolet : kOrange;
        out.push_back(m);
    }
    for (const Drop& d : drops_) {
        BossMark m;
        m.kind = d.left > 0 ? BossMark::Kind::Target : BossMark::Kind::Wave;
        m.area = d.left > 0 ? Rect{d.x - rainWidth_ * 0.5f, floorY_ - rainWidth_, rainWidth_, rainWidth_} : Rect{d.x - 24.0f, floorY_ - 4.0f, 48.0f, 4.0f};
        m.progress = std::clamp(1.0f - static_cast<float>(d.left) / static_cast<float>(std::max(1, steps(rainWarn_))), 0.0f, 1.0f);
        m.color = kOrange;
        m.clip = d.left <= steps(0.35f) ? "rain" : "";
        m.strong = d.left <= 0;
        out.push_back(m);
    }
    for (const Well& q : wells_) {
        BossMark m;
        m.kind = BossMark::Kind::Target;
        m.area = {q.at.x - 40.0f, q.at.y - 40.0f, 80.0f, 80.0f};
        m.progress = 1.0f - static_cast<float>(q.left) / static_cast<float>(std::max(1, steps(wellWarn_)));
        m.color = kViolet;
        m.clip = "well";
        out.push_back(m);
    }
    if (tideWarn_ > 0) {
        BossMark m;
        m.kind = BossMark::Kind::Zone;
        m.area = {spanX0_, floorY_ - tideHeight_, spanX1_ - spanX0_, tideHeight_};
        m.progress = 1.0f - static_cast<float>(tideWarn_) / static_cast<float>(std::max(1, steps(tideWarnS_)));
        m.color = kOrange;
        m.label = tideName_ + " · " + secs(tideWarn_);
        out.push_back(m);
    }
    // The anchors of its supernova (phase 4): lit while it charges, the ring of its charge around it.
    if (rules_ && (novaLeft_ > 0 || blast_ > 0) && b && b->alive()) {
        for (size_t k = 0; k < rules_->vents.size() && k < 8; ++k) {
            BossMark m;
            m.kind = BossMark::Kind::Plate;
            m.area = rules_->vents[k];
            m.clip = "anchor";
            m.progress = (anchorMask_ >> k) & 1u ? 1.0f : 0.0f;
            m.color = (anchorMask_ >> k) & 1u ? kGreen : kGold;
            out.push_back(m);
        }
        BossMark m;
        m.kind = BossMark::Kind::Target;
        m.area = b->box.expanded(60.0f);
        m.progress = novaLeft_ > 0 ? 1.0f - static_cast<float>(novaLeft_) / static_cast<float>(std::max(1, steps(novaCharge_ * slack(std::max(1, players_)))))
                                   : 1.0f;
        m.color = novaLeft_ > 0 ? kWhite : kRed;
        m.strong = true;
        m.label = novaLeft_ > 0 ? novaName_ + " · " + secs(novaLeft_) + " · ancres " + std::to_string(anchorsHeld_) + " / " + std::to_string(anchorsNeed_)
                                : novaName_ + " !";
        out.push_back(m);
    }
}

void AsterionHeartRoutine::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const int ph = std::max(1, g.phase());
    const std::string phs = "phase " + std::to_string(ph) + " / 4";
    out.push_back({pct(remaining_), "Vie du Cœur pour tous les groupes · ce groupe : " + pct(own_) + " · les autres : " + pct(applied_), kGold});
    if (exposed(g)) {
        out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s", "Ventricules à nu (" + phs + ") : brisez-les !", kGreen});
    } else if (novaLeft_ > 0) {
        out.push_back({std::to_string(anchorsHeld_) + " / " + std::to_string(anchorsNeed_),
                       "Supernova dans " + secs(novaLeft_) + " : " + std::to_string(anchorsNeed_) + " joueurs sur les ancres (les artères) pour la contenir !", kRed});
    } else if (ph >= 4) {
        out.push_back({"!", "Supernova (" + phs + ") : à chaque charge, montez sur les ancres ; la marée de plasma brûle le sol.", kOrange});
    } else if (armoured(g)) {
        out.push_back({std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents().size()),
                       std::string(down(Torse) ? "Artères" : "Cage du Cœur") + " (" + phs + ") : un joueur sur chacune des quatre artères, ensemble · tenues " +
                           std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents().size()),
                       kGold});
    } else {
        out.push_back({"→", "Vaincre le Cœur d'Asterion · " + phs + " : frappez-le, quittez les marques de ses lances.", kOrange});
    }
    int fallen = 0;
    for (int p = 0; p < PartCount; ++p) fallen += down(p) ? 1 : 0;
    out.push_back({std::to_string(fallen) + " / 7", "Parties du titan tombées : chacune affaiblit le Cœur (voir ses effets)" +
                                                        (relics_ > 0 ? " · " + std::to_string(relics_) + " relique" + (relics_ > 1 ? "s" : "") : std::string()),
                   kViolet});
}

void AsterionHeartRoutine::threats(const Gigaboss& g, std::vector<Vec2>& out) const {
    (void)g;
    for (const Band& v : bands_)
        if (v.warn <= 0) out.push_back({bandFront(v), v.y});
    for (const Lance& l : lances_) out.push_back(l.at);
}

void AsterionHeartRoutine::targets(std::vector<uint32_t>& out) const {
    for (uint32_t u : spawned_) out.push_back(u);
}

std::unique_ptr<GigabossRoutine> makeAsterionHeartRoutine() { return std::make_unique<AsterionHeartRoutine>(); }

}  // namespace cosmic::raid
