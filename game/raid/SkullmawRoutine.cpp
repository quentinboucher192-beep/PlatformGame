#include "game/raid/SkullmawRoutine.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
#include "engine/physics/Collision.h"
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
Rect rectOf(const Json& j) {
    return j.is_array() && j.size() == 4 ? Rect{j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()} : Rect{};
}
std::string secs(int ticks) { return std::to_string(std::max(0, (ticks + kSimHz - 1) / kSimHz)) + " s"; }
const std::string kGaze = "Regard traqueur";
const std::string kFlash = "Regard de mort";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 138, 138, 255};
const Color kViolet{201, 168, 255, 255};
const Color kPink{255, 106, 213, 255};
}  // namespace

void SkullmawRoutine::begin(GameWorld& w, const GigabossRules& r) {
    (void)w;
    *this = SkullmawRoutine{};
    rules_ = &r;
    const Json& g = r.extra;
    for (const auto& e : g.value("eyes", Json::array()))
        if (e.is_array() && e.size() == 2) eyes_.push_back({e[0].get<float>(), e[1].get<float>()});
    if (eyes_.empty()) eyes_ = {{-55.0f, -205.0f}, {55.0f, -205.0f}};
    const Json gaze = g.value("gaze", Json::object());
    gazePeriods_ = floatsOf(gaze.value("periods", Json()), {8.0f, 6.5f, 5.0f});
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
    for (const auto& m : g.value("mirrors", Json::array()))
        if (m.is_object()) mirrors_.push_back({rectOf(m.value("rect", Json()))});
    const Json fl = g.value("flash", Json::object());
    flashPeriods_ = floatsOf(fl.value("periods", Json()), {16.0f, 13.0f});
    for (float& p : flashPeriods_) p = std::clamp(p, 6.0f, 60.0f);
    charge_ = std::clamp(jget<float>(fl, "charge", charge_), 1.0f, 8.0f);
    flashDamage_ = std::clamp(jget<float>(fl, "damage", flashDamage_), 0.0f, 4.0f);
    needs_ = floatsOf(g.value("need", Json()), {1, 2, 2, 2, 3, 3, 4, 4});
    const Json light = g.value("searchlight", Json::object());
    searchPeriod_ = std::clamp(jget<float>(light, "period", searchPeriod_), 2.0f, 30.0f);
    searchSpread_ = std::clamp(jget<float>(light, "spread", searchSpread_), 0.05f, 1.2f);
}

Vec2 SkullmawRoutine::eyeOf(const Enemy& b, int k) const {
    const Vec2 e = eyes_[static_cast<size_t>(std::clamp(k, 0, static_cast<int>(eyes_.size()) - 1))];
    return {b.box.center().x + e.x * b.facing, b.box.bottom() + e.y};
}

Vec2 SkullmawRoutine::brow(const Enemy& b) const {
    Vec2 sum{0.0f, 0.0f};
    for (int k = 0; k < static_cast<int>(eyes_.size()); ++k) sum = sum + eyeOf(b, k);
    return sum * (1.0f / static_cast<float>(eyes_.size()));
}

int SkullmawRoutine::held() const {
    int n = 0;
    for (const Mirror& m : mirrors_) n += m.holders ? 1 : 0;
    return n;
}

void SkullmawRoutine::step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    ticks_ = ticks(g);
    const int ph = phase(g);
    phase_ = ph;
    const std::vector<int> play = playing(w);
    need_ = std::clamp(static_cast<int>(std::lround(at(needs_, std::clamp(static_cast<int>(play.size()), 1, 8), 1.0f))), 1,
                       std::max(1, static_cast<int>(mirrors_.size())));
    for (Mirror& m : mirrors_) {
        m.holders = 0;
        for (int i : play)
            if (m.area.intersects(w.playerAt(i).box)) m.holders |= static_cast<uint8_t>(1u << i);
    }
    const bool veilPhase = !b.lastStand() && ph >= veilFrom_ && ph < veilUntil_ && !mirrors_.empty();
    veil_ = veilPhase;
    if (exposed(g)) {
        // Blind (its own gaze thrown back): it sees nobody, its eyes and crystal bare.
        gazes_.clear();
        chargeTicks_ = 0;
        if (exposedStep(g, w, c, b)) return;
        if (veilPhase) {
            setArmour(g, true);
            alert(g, "GIGABOSS · Skullmaw rouvre les yeux : son voile se reforme !", Danger);
        } else {
            setFight(g);
        }
        return;
    }
    if (veilPhase) {
        if (!veilSaid_) {
            veilSaid_ = true;
            flashClock_ = steps(6.0f);  // its first flash a few seconds in
            alert(g, "GIGABOSS · Skullmaw se voile : quand son regard se charge, tenez " + std::to_string(need_) + " miroirs (sur " +
                         std::to_string(mirrors_.size()) + ") ; les autres, derrière une colonne !",
                  Danger);
        }
        if (!armoured(g)) setArmour(g, true);
    } else {
        if (armoured(g)) setFight(g);
        chargeTicks_ = 0;
    }
    stepGazes(w, c, b, ph, false);
    if (!veilPhase) return;
    // « Regard de mort »: it charges, then flashes.
    if (chargeTicks_ > 0) {
        if (chargeTicks_ % 12 == 0) w.particles().emit("charge_spark", brow(b), 1.0f);
        if (--chargeTicks_ == 0) flash(w, c, g, b);
        return;
    }
    if (--flashClock_ <= 0) {
        chargeTicks_ = std::max(1, steps(charge_));
        w.playSound("sounds/laser_charge.wav", brow(b), 1.0f, 0.7f);
        alert(g, "DANGER · Son regard se charge : " + std::to_string(need_) + " miroirs tenus le renvoient, les autres derrière une colonne !", Danger);
    }
}

void SkullmawRoutine::stepGazes(GameWorld& w, Contributions& c, Enemy& b, int phase, bool blind) {
    if (blind || b.lastStand() || gazePeriods_.empty()) {
        gazes_.clear();
        return;
    }
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_)), fireS = std::max(1, steps(fire_));
    const std::vector<int> play = playing(w);
    if (gazeClock_ <= 0) {
        gazeClock_ = std::max(trackS + lockS + fireS + 1, steps(at(gazePeriods_, phase, 8.0f) / std::max(1.0f, b.pressure())));
        gazes_.clear();
        for (int i : play) {
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
    const auto inPlayNow = [&](int seat) { return std::find(play.begin(), play.end(), seat) != play.end(); };
    for (Gaze& z : gazes_) {
        ++z.t;
        z.from = eyeOf(b, z.eye);
        if (z.t <= trackS && inPlayNow(z.seat)) z.aim = w.playerAt(z.seat).box.center();  // it follows its player
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
        if (z.t > fireAt && z.t <= fireAt + fireS)
            for (int i : play) {
                const Rect pb = w.playerAt(i).box;
                if (!Collision::segmentIntersectsRect(z.from, z.end, pb.expanded(width_ * 0.5f))) continue;
                const Vec2 cc = pb.center();
                strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, gazeDamage_, 220.0f, kGaze);
                if (i == z.seat) z.hit = true;
            }
        // Its player out of the line in time: a blow avoided.
        if (z.t == fireAt + fireS && !z.hit && inPlayNow(z.seat)) c.dodged(z.seat, gazeDamage_);
    }
    std::erase_if(gazes_, [&](const Gaze& z) { return z.t >= trackS + lockS + fireS; });
}

void SkullmawRoutine::flash(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    const Vec2 eye = brow(b);
    const int h = held();
    for (int i : playing(w)) {
        bool onMirror = false;
        for (const Mirror& m : mirrors_) onMirror = onMirror || ((m.holders >> i) & 1u);
        if (onMirror) {
            c.vent(i);  // a mirror held at its flash: the mechanism, its gaze thrown back
            continue;
        }
        // Seen (nothing solid between its eyes and them): blinded and burnt; hidden: avoided.
        const Vec2 cc = w.playerAt(i).box.center();
        if (!w.physics().raycast(eye, cc).has_value())
            strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, flashDamage_, 260.0f, kFlash);
        else
            c.dodged(i, flashDamage_);
    }
    lastFlash_ = std::max<uint32_t>(1, ticks_);
    lastHeld_ = h;
    flashClock_ = std::max(steps(charge_) + 1, steps(at(flashPeriods_, phase_ - veilFrom_ + 1, 14.0f)));
    w.flash({255, 236, 255, 255}, 0.35f);
    w.shake(0.5f);
    w.playSound("sounds/star_roar.wav", eye, 1.0f, 1.2f);
    w.particles().emit("portal_burst", eye, 2.0f);
    if (h >= need_) {
        gazes_.clear();
        expose(g, w, c, b,
               "GIGABOSS · Les miroirs lui renvoient son regard : Skullmaw est aveuglé ! Crevez ses yeux et son cristal (" +
                   std::to_string(static_cast<int>(std::lround(rules_->exposed))) + " s) !");
    } else {
        alert(g, "GIGABOSS · Miroirs tenus " + std::to_string(h) + " / " + std::to_string(need_) + " : son voile tient !", Danger);
    }
}

void SkullmawRoutine::fell(GameWorld& w, Gigaboss& g) {
    (void)w;
    (void)g;
    gazes_.clear();
    chargeTicks_ = 0;
}

SkullmawRoutine::GazeState SkullmawRoutine::gazeState(const Gaze& z) const {
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_));
    return z.t <= trackS ? GazeState::Tracking : z.t <= trackS + lockS ? GazeState::Locked : GazeState::Firing;
}

float SkullmawRoutine::gazeProgress(const Gaze& z) const {
    const int trackS = std::max(1, steps(track_)), lockS = std::max(1, steps(lock_)), fireS = std::max(1, steps(fire_));
    switch (gazeState(z)) {
        case GazeState::Tracking: return static_cast<float>(z.t) / static_cast<float>(trackS);
        case GazeState::Locked: return static_cast<float>(z.t - trackS) / static_cast<float>(lockS);
        case GazeState::Firing: return static_cast<float>(z.t - trackS - lockS) / static_cast<float>(fireS);
    }
    return 0.0f;
}

float SkullmawRoutine::chargeRatio() const {
    return chargeTicks_ > 0 ? 1.0f - static_cast<float>(chargeTicks_) / static_cast<float>(std::max(1, steps(charge_))) : 0.0f;
}

float SkullmawRoutine::searchAngle(uint32_t t) const {
    const float s = static_cast<float>(t) / static_cast<float>(kSimHz) / searchPeriod_;
    return 1.5708f + 0.95f * std::sin(s * 6.2832f);  // down, swinging from one wall to the other
}

uint64_t SkullmawRoutine::hash() const {
    uint64_t h = 0x5c011a3a5ULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 8) | (veil_ ? 1u : 0u) | (veilSaid_ ? 2u : 0u));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(gazeClock_)) << 32) | static_cast<uint32_t>(volleys_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(flashClock_)) << 32) | static_cast<uint32_t>(chargeTicks_));
    h = mix(h, (static_cast<uint64_t>(lastFlash_) << 16) | (static_cast<uint64_t>(lastHeld_) << 8) | static_cast<uint32_t>(need_));
    for (const Gaze& z : gazes_)
        h = mix(h, (static_cast<uint64_t>(z.seat & 0xFF) << 56) ^ (static_cast<uint64_t>(z.eye) << 48) ^ (static_cast<uint64_t>(z.t) << 24) ^
                       static_cast<uint64_t>(std::lround(z.aim.x * 4.0f)) ^ (static_cast<uint64_t>(std::lround(z.aim.y * 4.0f)) << 12) ^ (z.hit ? 1ULL << 63 : 0));
    for (const Mirror& m : mirrors_) h = mix(h, m.holders);
    return h;
}

// ------------------------------------------------------------------------ display
void SkullmawRoutine::marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const {
    const Enemy* b = g.boss(w);
    if (!b || !b->alive()) return;
    const Vec2 eye = brow(*b);
    // Its eye-light in the darkness (phase 3): the platforms it touches show.
    if (dark()) {
        const float ang = searchAngle(ticks_);
        BossMark m;
        m.kind = BossMark::Kind::Cone;
        m.area = {eye.x, eye.y, 0.0f, 0.0f};
        m.to = {eye.x + std::cos(ang) * 1500.0f, eye.y + std::sin(ang) * 1500.0f};
        m.width = searchSpread_;
        m.color = kViolet;
        out.push_back(m);
    }
    // Its mirrors: lit while held, a burst at its flash; « MIROIR » on the free ones while it charges.
    const bool burst = lastFlash_ > 0 && ticks_ - lastFlash_ < static_cast<uint32_t>(steps(0.4f));
    for (const Mirror& mi : mirrors_) {
        BossMark m;
        m.kind = BossMark::Kind::Plate;
        m.area = mi.area;
        m.clip = burst ? "mirror_flash" : mi.holders ? "mirror_lit" : "mirror";
        m.color = kPink;
        if (chargeTicks_ > 0 && !mi.holders) m.label = "MIROIR";
        out.push_back(m);
        if (chargeTicks_ > 0) {
            BossMark l;  // toward each mirror, faint: where to throw its gaze back
            l.kind = BossMark::Kind::Beam;
            l.area = {eye.x, eye.y, 0.0f, 0.0f};
            l.to = mi.area.center();
            l.width = 2.0f;
            l.progress = 0.2f * chargeRatio();
            l.color = kPink;
            out.push_back(l);
        }
        if (burst && (lastHeld_ >= need_ || mi.holders)) {
            BossMark l;  // its gaze thrown back by the mirrors held
            l.kind = BossMark::Kind::Beam;
            l.area = {mi.area.center().x, mi.area.center().y, 0.0f, 0.0f};
            l.to = eye;
            l.width = 6.0f;
            l.strong = true;
            l.color = Color{255, 236, 255, 255};
            out.push_back(l);
        }
    }
    if (chargeTicks_ > 0) {
        BossMark m;  // its brow glowing as its gaze charges
        m.kind = BossMark::Kind::Target;
        m.area = {eye.x - 60.0f, eye.y - 60.0f, 120.0f, 120.0f};
        m.progress = chargeRatio();
        m.strong = m.progress > 0.7f;
        m.color = kPink;
        m.label = "REGARD DE MORT";
        out.push_back(m);
    }
    // Its gazes: following (thin), frozen (bright), firing (a thick beam).
    for (const Gaze& z : gazes_) {
        BossMark m;
        m.kind = BossMark::Kind::Beam;
        m.area = {z.from.x, z.from.y, 0.0f, 0.0f};
        m.to = z.end;
        const GazeState s = gazeState(z);
        m.width = s == GazeState::Tracking ? 2.0f : s == GazeState::Locked ? 4.0f : width_;
        m.progress = s == GazeState::Tracking ? 0.3f * gazeProgress(z) : 0.6f + 0.4f * gazeProgress(z);
        m.strong = s == GazeState::Firing;
        m.color = s == GazeState::Tracking ? kViolet : kPink;
        out.push_back(m);
    }
}

void SkullmawRoutine::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    if (exposed(g)) {
        out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s",
                       "Skullmaw est aveuglé (" + ph + ") : crevez ses yeux et son cristal · points faibles " + std::to_string(g.weakTotal() - g.weakLeft()) +
                           " / " + std::to_string(g.weakTotal()),
                       kGreen});
    } else if (chargeTicks_ > 0) {
        out.push_back({secs(chargeTicks_), "Son regard se charge : miroirs tenus " + std::to_string(held()) + " / " + std::to_string(need_) +
                                               " · les autres, cachez-vous derrière une colonne !",
                       kRed});
    } else if (veil_) {
        out.push_back({std::to_string(need_) + " / " + std::to_string(mirrors_.size()),
                       "Voile psychique (" + ph + ") : rien ne le blesse. À son flash, " + std::to_string(need_) +
                           " miroirs tenus lui renvoient son regard et l'aveuglent.",
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Skullmaw · " + ph + " : chaque regard laser vous suit puis se fige : quittez sa ligne à cet instant.", kOrange});
    }
    if (dark()) out.push_back({"✦", "Ténèbres : la lumière de son œil balaie l'arène et révèle les plateformes ; les miroirs brillent.", kViolet});
}

void SkullmawRoutine::threats(const Gigaboss& g, std::vector<Vec2>& out) const {
    (void)g;
    for (const Gaze& z : gazes_)
        if (gazeState(z) != GazeState::Tracking) out.push_back(z.aim);
    if (chargeTicks_ > 0)
        for (const Mirror& m : mirrors_) out.push_back(m.area.center());
}

std::unique_ptr<GigabossRoutine> makeSkullmawRoutine() { return std::make_unique<SkullmawRoutine>(); }

}  // namespace cosmic::raid
