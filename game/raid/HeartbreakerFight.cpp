#include "game/raid/HeartbreakerFight.h"

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
const std::string kTrap = "Côtes-pièges";
const std::string kWave = "Onde du battement";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 138, 138, 255};
const Color kPink{255, 93, 143, 255};
std::string secs(int ticks) { return std::to_string(std::max(0, (ticks + kSimHz - 1) / kSimHz)) + " s"; }
}  // namespace

HeartbreakerFight::HeartbreakerFight(const GigabossRules& rules) {
    const Json& g = rules.params;
    const Json plates = g.value("plates", Json::object());
    // The plates are its weak points of phase 1 (their own list), the heart those of the cage.
    Json pj = Json::object();
    Json spots = Json::array();
    for (const auto& s : plates.value("spots", Json::array()))
        if (s.is_array() && s.size() == 2) spots.push_back(Json::array({s[0], s[1], jstr(plates, "enemy", "heartbreaker_plaque")}));
    pj["weakSpots"] = spots;
    pj["weakShare"] = jget<float>(plates, "share", 0.02f);
    pj["weakHpPerPlayer"] = jget<float>(g, "weakHpPerPlayer", 0.25f);
    plates_.read(pj);
    plates_.mirror = false;  // its plates keep their side whichever way it faces
    for (const auto& n : plates.value("names", Json::array()))
        if (n.is_string()) plateNames_.push_back(n.get<std::string>());
    window_ = std::clamp(jget<float>(plates, "window", window_), 5.0f, 60.0f);
    const Json beats = g.value("beats", Json::object());
    beatPeriods_ = mech::floats(beats.value("periods", Json()), {2.0f, 1.6f, 1.25f});
    for (float& p : beatPeriods_) p = std::clamp(p, 0.6f, 10.0f);
    strong_ = mech::floats(beats.value("strong", Json()), {4, 3, 2});
    waveSpeed_ = std::clamp(jget<float>(beats, "speed", waveSpeed_), 100.0f, 2000.0f);
    waveHeight_ = std::clamp(jget<float>(beats, "height", waveHeight_), 12.0f, 96.0f);
    waveDamage_ = std::clamp(jget<float>(beats, "damage", waveDamage_), 0.0f, 4.0f);
    floorY_ = jget<float>(beats, "floor", 0.0f);
    from_ = mech::floats(beats.value("from", Json()), {-170.0f, 170.0f});
    const std::vector<float> span = mech::floats(beats.value("span", Json()));
    if (span.size() == 2 && span[1] > span[0]) {
        spanX0_ = span[0];
        spanX1_ = span[1];
    }
    traps_.read(g.value("traps", Json::object()));
    const Json guard = g.value("guard", Json::array());
    if (guard.is_array() && guard.size() == 2) {
        guardFrom_ = guard[0].get<int>();
        guardUntil_ = guard[1].get<int>();
    }
    ribs_.read(g.value("ribs", Json::array()));
    hold_ = std::clamp(jget<float>(g, "hold", hold_), 0.5f, 10.0f);
    exposed_ = std::clamp(jget<float>(g, "exposed", exposed_), 2.0f, 30.0f);
    exposedMult_ = std::clamp(jget<float>(g, "exposedMult", exposedMult_), 1.0f, 4.0f);
    heart_.read(g);
}

void HeartbreakerFight::syncWeak(Contributions& c) const {
    std::vector<uint32_t> all = plates_.uids;
    all.insert(all.end(), heart_.uids.begin(), heart_.uids.end());
    c.setWeak(all);
}

void HeartbreakerFight::step(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    phase_ = g.phase();
    inPlay_ = mech::countInPlay(w);
    stepPlates(w, g, b, c);
    if (!b.alive()) return;
    stepBeats(w, b, phase_, g.fightTicks());
    if (traps_.step(w, b, phase_, c, kTrap)) g.say("DANGER · Côtes-pièges : les côtes se dressent autour de vous, sortez-en !", Gigaboss::AlertKind::Danger);
    for (const auto& m : traps_.marks)
        if (m.left == 0) {
            w.particles().emit("rock_shatter", {m.at.x, m.at.y - 30.0f}, 1.0f);
            w.shake(0.15f);
        }
    stepCage(w, g, b, c);
}

// ------------------------------------------------------------------------ plates (phase 1)
float HeartbreakerFight::windowLeft() const { return static_cast<float>(std::max(0, windowTicks_)) / static_cast<float>(kSimHz); }

void HeartbreakerFight::stepPlates(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    using AK = Gigaboss::AlertKind;
    if (!armoured_) return;
    if (phase_ > 1 || b.lastStand() || plates_.spots.empty()) {
        // Past its first phase (an administrator's test starting later): no armour.
        armoured_ = false;
        plates_.clear(w);
        syncWeak(c);
        return;
    }
    if (!platesUp_) {
        platesUp_ = true;
        plates_.spawn(w, b, g.scaledFor());
        syncWeak(c);
        g.say("GIGABOSS · Quatre plaques d'armure gardent le cœur : brisez-les ensemble, elles repoussent " +
                  std::to_string(static_cast<int>(std::lround(window_ * slack()))) + " s après la première !",
              AK::Danger);
        return;
    }
    const int broke = plates_.step(w, b, c);
    if (broke > 0) {
        if (windowTicks_ <= 0) windowTicks_ = std::max(1, steps(window_ * slack()));  // the first of a round: the others must follow
        const int left = plates_.left();
        g.say("GROUPE · Plaque brisée (" + std::to_string(plates_.total - left) + " / " + std::to_string(plates_.total) + ")", AK::Group);
        w.playSound("sounds/shell_shatter.wav", b.box.center(), 0.9f);
        if (left == 0) {
            armoured_ = false;
            windowTicks_ = 0;
            syncWeak(c);
            w.shake(0.9f);
            w.flash({255, 200, 200, 255}, 0.25f);
            w.particles().emit("rock_shatter", b.box.center(), 3.0f);
            g.say("GIGABOSS · La cuirasse tombe : Heartbreaker est vulnérable, frappez-le !", AK::Branch);
            return;
        }
    }
    if (windowTicks_ > 0 && --windowTicks_ == 0) {
        // Too slow: the broken plates grow back (full), the round starts again.
        plates_.spawn(w, b, g.scaledFor());
        syncWeak(c);
        ++regrown_;
        g.say("GIGABOSS · Les plaques repoussent : brisez les quatre en moins de " + std::to_string(static_cast<int>(std::lround(window_ * slack()))) +
                  " s, plusieurs groupes à la fois !",
              AK::Danger);
    }
}

// ------------------------------------------------------------------------ beats
int HeartbreakerFight::strongEvery(int phase) const {
    return std::max(1, static_cast<int>(std::lround(mech::byPhase(strong_, phase, 3.0f))));
}

float HeartbreakerFight::waveFront(const Wave& v) const {
    return v.x0 + static_cast<float>(v.dir) * waveSpeed_ * static_cast<float>(v.t) / static_cast<float>(kSimHz);
}

void HeartbreakerFight::stepBeats(GameWorld& w, Enemy& b, int phase, uint32_t ticks) {
    if (--beatClock_ <= 0) {
        const float period = mech::byPhase(beatPeriods_, phase, 1.6f) / std::max(1.0f, b.pressure());
        beatClock_ = std::max(steps(0.5f), steps(period));
        ++beats_;
        lastBeat_ = std::max<uint32_t>(1, ticks);
        lastStrong_ = beats_ % strongEvery(phase) == 0 && !b.lastStand();
        const Vec2 heart{b.box.center().x, b.box.bottom() - b.box.h * 0.53f};
        w.particles().emit(lastStrong_ ? "impulse_ring" : "heart_pickup", heart, lastStrong_ ? 1.6f : 0.6f);
        w.playSound("sounds/story_heartbeat.wav", heart, lastStrong_ ? 1.0f : 0.5f, lastStrong_ ? 0.9f : 1.0f);
        if (lastStrong_) {
            // Its strong beat: a wave along the floor from both its sides, to the walls.
            for (size_t k = 0; k < from_.size(); ++k) {
                Wave v;
                v.x0 = b.box.center().x + from_[k];
                v.dir = from_[k] < 0.0f ? -1 : 1;
                waves_.push_back(v);
            }
            w.shake(0.25f);
        }
    }
    const float floor = floorY_ > 0.0f ? floorY_ : b.box.bottom();
    for (Wave& v : waves_) {
        ++v.t;
        const float x = waveFront(v);
        const Rect band{x - 16.0f, floor - waveHeight_, 32.0f, waveHeight_ - 2.0f};
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
            if ((v.hit >> i) & 1u || !mech::inPlay(w, i) || !band.intersects(w.playerAt(i).box)) continue;
            v.hit |= static_cast<uint8_t>(1u << i);
            mech::hurtSeat(w, b, i, waveDamage_, {x - 30.0f * static_cast<float>(v.dir), floor}, 260.0f, kWave);
        }
        if (v.t % 6 == 0) w.particles().emit("shockwave_dust", {x, floor - 4.0f}, 0.5f);
    }
    std::erase_if(waves_, [&](const Wave& v) { return waveFront(v) < spanX0_ - 20.0f || waveFront(v) > spanX1_ + 20.0f; });
}

// ------------------------------------------------------------------------ the cage (phase 2)
float HeartbreakerFight::pryRatio() const { return std::clamp(static_cast<float>(pry_) / static_cast<float>(std::max(1, steps(hold_))), 0.0f, 1.0f); }

float HeartbreakerFight::exposedLeft() const { return static_cast<float>(std::max(0, exposedTicks_)) / static_cast<float>(kSimHz); }

void HeartbreakerFight::stepCage(GameWorld& w, Gigaboss& g, Enemy& b, Contributions& c) {
    using AK = Gigaboss::AlertKind;
    const bool cagePhase = !b.lastStand() && phase_ >= guardFrom_ && phase_ < guardUntil_ && !ribs_.places.empty();
    need_ = std::clamp(inPlay_, 1, std::max(1, static_cast<int>(ribs_.places.size())));
    if (exposedTicks_ > 0) {
        if (heart_.step(w, b, c) > 0) g.say("GROUPE · La lumière du cœur vacille : coup au but !", AK::Group);
        if (!b.alive()) return;
        if (--exposedTicks_ > 0 && cagePhase) return;
        exposedTicks_ = 0;
        heart_.clear(w);
        syncWeak(c);
        pry_ = 0;
        ribs_.reset();
        if (cagePhase) g.say("GIGABOSS · Les côtes se referment sur le cœur : écartez-les de nouveau, des deux côtés !", AK::Danger);
    }
    if (cagePhase && !cageSaid_) {
        cageSaid_ = true;
        g.say("GIGABOSS · La cage thoracique se referme : un joueur sur chaque côte, tenez-les ouvertes ensemble (" +
                  std::to_string(static_cast<int>(std::lround(hold_ * 10.0f)) / 10) + " s) !",
              AK::Danger);
    }
    if (cage_ && !cagePhase) g.say("GIGABOSS · La cage se brise : le cœur bat à découvert !", AK::Branch);
    cage_ = cagePhase;
    if (!cage_) return;
    ribs_.sense(w);
    const int holdS = std::max(1, steps(hold_));
    if (ribs_.held() >= need_) {
        ++pry_;
        if (pry_ % 20 == 0) w.particles().emit("rock_shatter", b.box.center(), 0.4f);
    } else {
        pry_ = std::max(0, pry_ - 2);
    }
    if (pry_ < holdS) return;
    // Held open from both sides at once: the heart is bare.
    for (const auto& p : ribs_.places)
        for (int i = 0; i < 8; ++i)
            if ((p.holders >> i) & 1u) c.vent(i);
    exposedTicks_ = std::max(1, steps(exposed_));
    heart_.spawn(w, b, g.scaledFor());
    syncWeak(c);
    w.shake(0.7f);
    w.flash({255, 190, 160, 255}, 0.2f);
    w.particles().emit("fire_burst", b.box.center(), 2.5f);
    w.playSound("sounds/story_heartbeat.wav", b.box.center(), 1.0f, 0.8f);
    g.say("GIGABOSS · Les côtes s'écartent : la lumière du cœur est à nu (" + std::to_string(static_cast<int>(std::lround(exposed_))) +
              " s) ! Frappez-la !",
          AK::Branch);
}

void HeartbreakerFight::stop(GameWorld& w, Contributions& c) {
    plates_.clear(w);
    heart_.clear(w);
    waves_.clear();
    traps_.marks.clear();
    exposedTicks_ = 0;
    syncWeak(c);
}

uint64_t HeartbreakerFight::hash() const {
    uint64_t h = 0x4ea27b2eULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 16) | (armoured_ ? 1u : 0u) | (platesUp_ ? 2u : 0u) | (cage_ ? 4u : 0u) | (cageSaid_ ? 8u : 0u) |
                   (lastStrong_ ? 16u : 0u));
    plates_.hashInto(h);
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(windowTicks_)) << 32) | static_cast<uint32_t>(regrown_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(beatClock_)) << 32) | static_cast<uint32_t>(beats_));
    for (const Wave& v : waves_)
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(v.t)) << 32) ^ static_cast<uint64_t>(std::lround(v.x0 * 4.0f)) ^
                       (static_cast<uint64_t>(v.hit) << 24) ^ (v.dir > 0 ? 1ULL << 62 : 0));
    traps_.hashInto(h);
    ribs_.hashInto(h);
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(pry_)) << 32) | static_cast<uint32_t>(exposedTicks_));
    heart_.hashInto(h);
    return h;
}

// ------------------------------------------------------------------------ display
std::string HeartbreakerFight::fallText(const std::string& name) const {
    return "GIGABOSS · " + name + " est vaincu ! La cage du torse s'ouvre : la voie du cœur d'Asterion est libre.";
}
std::string HeartbreakerFight::goneText(const std::string& name) const { return "GIGABOSS · La cage est ouverte : " + name + " est déjà tombé."; }
std::string HeartbreakerFight::goneLine(const std::string& name) const { return name + " est déjà tombé : la cage du torse est ouverte."; }
std::string HeartbreakerFight::fallCard() const {
    return "La cage du torse s'ouvre : la voie du cœur d'Asterion est libre. Les résultats comptent dès que la majorité de votre instance les a "
           "confirmés au serveur.";
}

void HeartbreakerFight::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    if (phase_ <= 1 && armoured_) {
        const int total = std::max(1, plates_.total);
        out.push_back({windowTicks_ > 0 ? secs(windowTicks_) : std::to_string(total - plates_.left()) + " / " + std::to_string(total),
                       "Cuirasse (" + ph + ") : brisez ses " + std::to_string(total) +
                           " plaques ensemble (les hautes depuis les tours) : elles repoussent si la dernière tarde.",
                       windowTicks_ > 0 ? kRed : kGold});
    } else if (exposedTicks_ > 0) {
        out.push_back({secs(exposedTicks_), "Le cœur est à nu (" + ph + ") : frappez sa lumière entre les côtes.", kGreen});
    } else if (cage_) {
        out.push_back({std::to_string(static_cast<int>(std::lround(pryRatio() * 100.0f))) + " %",
                       "Cage thoracique (" + ph + ") : un joueur sur chaque côte, des deux côtés à la fois · côtes tenues " +
                           std::to_string(ribs_.held()) + " / " + std::to_string(need_),
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Heartbreaker · " + ph + " : frappez la cage, quittez les côtes-pièges.", kOrange});
    }
    out.push_back({"◆", "Battements : une onde court au sol à chaque battement fort (un sur " + std::to_string(strongEvery(std::max(1, g.phase()))) +
                            ") : sautez-la.",
                   kPink});
}

void HeartbreakerFight::threats(const GameWorld& w, std::vector<Vec2>& out) const {
    (void)w;
    for (const auto& m : traps_.marks) out.push_back(m.at);
    const float floor = floorY_;
    for (const Wave& v : waves_) out.push_back({waveFront(v), floor});
}

void HeartbreakerFight::targets(std::vector<uint32_t>& out) const {
    for (uint32_t u : plates_.uids)
        if (u) out.push_back(u);
    for (uint32_t u : heart_.uids)
        if (u) out.push_back(u);
}

}  // namespace cosmic::raid
