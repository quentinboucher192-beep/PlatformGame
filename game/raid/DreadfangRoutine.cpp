#include "game/raid/DreadfangRoutine.h"

#include <algorithm>
#include <cmath>

#include "engine/core/Json.h"
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
std::string storeyName(int k) { return k == 0 ? "le sol" : "l'étage " + std::to_string(k); }
std::string storeysOf(const std::vector<int>& s) {
    std::string where;
    for (size_t k = 0; k < s.size(); ++k) where += (k ? (k + 1 == s.size() ? " et " : ", ") : "") + storeyName(s[k]);
    return where;
}
const std::string kSweep = "Balayage des griffes";
const std::string kPin = "Griffe clouante";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 120, 100, 255};
const Color kIce{127, 246, 255, 255};
}  // namespace

void DreadfangRoutine::begin(GameWorld& w, const GigabossRules& r) {
    (void)w;
    *this = DreadfangRoutine{};
    rules_ = &r;
    const Json& g = r.extra;
    const Json sweep = g.value("sweep", Json::object());
    sweepPeriods_ = floatsOf(sweep.value("periods", Json()), {11.0f, 9.0f, 7.5f});
    for (float& p : sweepPeriods_) p = std::clamp(p, 4.0f, 60.0f);
    sweepWarn_ = std::clamp(jget<float>(sweep, "warn", sweepWarn_), 0.5f, 4.0f);
    sweepSpeed_ = std::clamp(jget<float>(sweep, "speed", sweepSpeed_), 300.0f, 4000.0f);
    sweepHeight_ = std::clamp(jget<float>(sweep, "height", sweepHeight_), 12.0f, 96.0f);
    sweepDamage_ = std::clamp(jget<float>(sweep, "damage", sweepDamage_), 0.0f, 4.0f);
    storeys_ = floatsOf(sweep.value("storeys", Json()), {});
    sweepCount_ = floatsOf(sweep.value("count", Json()), {1, 2, 3});
    const std::vector<float> span = floatsOf(sweep.value("span", Json()), {});
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
}

void DreadfangRoutine::watch(Contributions& c) const {
    // Its nailing claws: whoever breaks one frees a player (a rescue).
    std::vector<uint32_t> claws;
    for (const Pin& p : pins_)
        if (p.claw) claws.push_back(p.claw);
    c.setWatched(claws);
}

bool DreadfangRoutine::spared(int seat) const {
    return std::any_of(pins_.begin(), pins_.end(), [&](const Pin& p) { return p.seat == seat && p.nailed; });
}

void DreadfangRoutine::step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    const GigabossRules& r = *rules_;
    const int ph = phase(g);
    phase_ = ph;
    stepSweeps(w, g, b, ph);
    stepPins(w, c, g, b, ph);
    if (!b.alive()) return;
    const bool guard = !b.lastStand() && ph >= r.carapaceFrom && ph < r.carapaceUntil && !r.vents.empty();
    if (exposed(g)) {
        // Chained: pinned to the floor, its knuckles bare.
        b.box = Rect::fromBottomCenter(chainedAt_, b.box.size());
        b.prevBox = b.box;
        b.vel = {0.0f, 0.0f};
        if (exposedStep(g, w, c, b)) return;
        chained_ = false;
        resetPlates(g);
        if (guard) {
            setArmour(g, true);
            alert(g, "GIGABOSS · Dreadfang brise ses chaînes : tenez-les de nouveau ensemble !", Danger);
        } else {
            setFight(g);
        }
        return;
    }
    if (!guard) {
        if (armoured(g)) alert(g, "GIGABOSS · Son armure de givre vole en éclats : ses griffes se déchaînent !", Branch);
        setFight(g);
        return;
    }
    if (!armoured(g)) {
        setArmour(g, true);
        resetPlates(g);
        if (!guardSaid_)
            alert(g, "GIGABOSS · Armure de givre : tenez ses " + std::to_string(r.vents.size()) + " chaînes ensemble (" +
                         std::to_string(static_cast<int>(std::lround(r.hold * 10.0f)) / 10) + " s sur chacune) pour l'enchaîner !",
                  Danger);
        guardSaid_ = true;
    }
    if (holdPlates(g, w, c, b, {"GROUPE · Chaîne tenue", "GIGABOSS · Une chaîne se détend : retenez-la !"})) {
        // Its chains held together: pinned to the floor, its knuckles bare.
        const float floor = storeys_.empty() ? b.box.bottom() : storeys_.front();
        chainedAt_ = {std::clamp(b.box.center().x, spanX0_ + b.box.w * 0.5f, spanX1_ - b.box.w * 0.5f), floor};
        b.box = Rect::fromBottomCenter(chainedAt_, b.box.size());
        b.prevBox = b.box;
        chained_ = true;
        w.particles().emit("frost_nova", b.box.center(), 2.5f);
        w.playSound("sounds/ice_shatter.wav", b.box.center(), 1.0f);
        expose(g, w, c, b,
               "GIGABOSS · Les chaînes tiennent : Dreadfang est plaqué au sol ! Brisez ses jointures (" +
                   std::to_string(static_cast<int>(std::lround(r.exposed))) + " s) !");
    }
}

// ------------------------------------------------------------------------ sweeps
bool DreadfangRoutine::bladeOut(const Sweep& s) const { return s.t > steps(sweepWarn_); }

float DreadfangRoutine::bladeX(const Sweep& s) const {
    const float run = static_cast<float>(std::max(0, s.t - steps(sweepWarn_))) / static_cast<float>(kSimHz) * sweepSpeed_;
    return s.dir > 0 ? spanX0_ + run : spanX1_ - run;
}

void DreadfangRoutine::stepSweeps(GameWorld& w, Gigaboss& g, Enemy& b, int phase) {
    const int n = static_cast<int>(storeys_.size());
    if (n == 0 || sweepPeriods_.empty() || b.lastStand()) {
        sweeps_.clear();
        return;
    }
    if (--sweepClock_ <= 0) {
        sweepClock_ = std::max(steps(sweepWarn_) + steps((spanX1_ - spanX0_) / sweepSpeed_) + 2,
                               steps(at(sweepPeriods_, phase, 10.0f) / std::max(1.0f, b.pressure())));
        // One, two then three storeys at once (never all: two at least stay safe), changing each time.
        const int count = std::clamp(static_cast<int>(std::lround(at(sweepCount_, phase, 1.0f))), 1, std::max(1, n - 2));
        Sweep s;
        s.dir = sweepVolley_ % 2 == 0 ? 1 : -1;
        const int start = (sweepVolley_ * 3 + phase) % n;
        for (int k = 0; s.storeys.size() < static_cast<size_t>(count) && k < 2 * n; ++k) {
            const int idx = (start + 2 * k + (k >= (n + 1) / 2 ? 1 : 0)) % n;
            if (std::find(s.storeys.begin(), s.storeys.end(), idx) == s.storeys.end()) s.storeys.push_back(idx);
        }
        std::sort(s.storeys.begin(), s.storeys.end());
        alert(g, "DANGER · Balayage des griffes " + std::string(s.dir > 0 ? "depuis l'ouest" : "depuis l'est") + " sur " + storeysOf(s.storeys) +
                     " : sautez par-dessus la lame ou changez d'étage !",
              Danger);
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
            strike(w, b, {x - 28.0f, y - sweepHeight_, 56.0f, sweepHeight_ - 2.0f}, sweepDamage_, 320.0f, kSweep);
            if (s.t % 4 == 0) w.particles().emit("frost_burst", {x, y - sweepHeight_ * 0.5f}, 0.5f);
        }
    }
    std::erase_if(sweeps_, [&](const Sweep& s) { return bladeOut(s) && (bladeX(s) < spanX0_ - 40.0f || bladeX(s) > spanX1_ + 40.0f); });
}

// ------------------------------------------------------------------------ pins
void DreadfangRoutine::stepPins(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b, int phase) {
    const std::vector<int> play = playing(w);
    bool changed = false;
    // Aim: one player (two from five in play) on a mark that does not follow them.
    if (phase >= pinFrom_ && !b.lastStand() && play.size() >= 2) {
        if (--pinClock_ <= 0) {
            pinClock_ = std::max(steps(pinWarn_ + pinHold_) + 1, steps(pinPeriod_ / std::max(1.0f, b.pressure())));
            const int want = play.size() >= 5 ? 2 : 1;
            int aimed = 0;
            for (int k = 0; k < 8 && aimed < want; ++k) {
                const int seat = (pinVolley_ * 3 + k) % 8;
                if (std::find(play.begin(), play.end(), seat) == play.end()) continue;
                if (std::any_of(pins_.begin(), pins_.end(), [&](const Pin& p) { return p.seat == seat; })) continue;
                Pin p;
                p.seat = seat;
                p.at = w.playerAt(seat).feet();
                pins_.push_back(p);
                ++aimed;
            }
            if (aimed > 0) alert(g, "DANGER · Griffe clouante : Dreadfang vise un joueur, écartez-vous de votre marque !", Danger);
            ++pinVolley_;
        }
    } else {
        pinClock_ = std::max(pinClock_, steps(4.0f));
    }
    const auto here = [&](int seat) { return std::find(play.begin(), play.end(), seat) != play.end(); };
    const int warnS = std::max(1, steps(pinWarn_)), every = std::max(1, steps(pinEvery_));
    for (Pin& p : pins_) {
        ++p.t;
        if (!p.nailed) {
            if (p.t < warnS) continue;
            // The claw comes down on the mark: still there, the player is nailed.
            w.particles().emit("ice_burst", p.at, 1.4f);
            w.shake(0.3f);
            const Vec2 f = here(p.seat) ? w.playerAt(p.seat).feet() : Vec2{1e9f, 1e9f};
            if (std::abs(f.x - p.at.x) <= pinReach_ && std::abs(f.y - p.at.y) <= 40.0f) {
                if (Enemy* e = w.spawnEnemy(pinEnemy_, p.at, b.facing < 0.0f)) {
                    e->summonedBy = b.uid();
                    e->countsForStats = false;
                    e->scaleHealth(1.0f + rules_->weakHpPerPlayer * static_cast<float>(std::max(0, scaledFor(g) - 1)));
                    p.claw = e->uid();
                }
                p.nailed = true;
                p.left = std::max(1, steps(pinHold_));
                const Vec2 cc = w.playerAt(p.seat).box.center();
                strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, pinDamage_, 0.0f, kPin);
                changed = true;
                alert(g, "GROUPE · Un joueur est cloué au sol : brisez la griffe pour le libérer !", Group);
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
            alert(g, "GROUPE · La griffe est brisée : le joueur est libéré !", Group);
            continue;
        }
        if (!here(p.seat)) {
            p.left = 0;
            continue;
        }
        Player& pl = w.playerAt(p.seat);
        pl.teleportTo(p.at);
        pl.vel = {0.0f, 0.0f};
        if (p.t % every == 0) {
            const Vec2 cc = pl.box.center();
            strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, pinDamage_, 0.0f, kPin);
        }
        if (--p.left <= 0) alert(g, "GROUPE · La griffe se retire.", Group);
    }
    for (Pin& p : pins_)
        if (p.left <= 0 && (p.nailed || p.t >= warnS)) {
            if (Enemy* e = p.claw ? w.findEnemy(p.claw) : nullptr; e && e->alive()) e->despawn();
            if (p.claw) changed = true;
            p.claw = 0;
        }
    std::erase_if(pins_, [&](const Pin& p) { return (p.nailed || p.t >= warnS) && p.left <= 0; });
    if (changed) watch(c);
}

void DreadfangRoutine::fell(GameWorld& w, Gigaboss& g) {
    (void)g;
    for (Pin& p : pins_)
        if (Enemy* e = p.claw ? w.findEnemy(p.claw) : nullptr; e && e->alive()) e->despawn();
    pins_.clear();
    sweeps_.clear();
    chained_ = false;
}

uint64_t DreadfangRoutine::hash() const {
    uint64_t h = 0xd4eadfa9ULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 8) | (guardSaid_ ? 1u : 0u) | (chained_ ? 2u : 0u));
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
    h = mix(h, static_cast<uint64_t>(std::lround(chainedAt_.x * 4.0f)) ^ (static_cast<uint64_t>(std::lround(chainedAt_.y * 4.0f)) << 24));
    return h;
}

// ------------------------------------------------------------------------ display
void DreadfangRoutine::marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const {
    const Enemy* b = g.boss(w);
    // Its sweeps: the storeys announced (a band across the arena), then its blade crossing them.
    for (const Sweep& s : sweeps_) {
        bool first = true;
        for (int k : s.storeys) {
            const float y = storeys_[static_cast<size_t>(k)];
            BossMark m;
            if (!bladeOut(s)) {
                m.kind = BossMark::Kind::Zone;
                m.area = {spanX0_, y - sweepHeight_, spanX1_ - spanX0_, sweepHeight_};
                m.progress = std::clamp(static_cast<float>(s.t) / static_cast<float>(std::max(1, steps(sweepWarn_))), 0.0f, 1.0f);
                m.color = kRed;
                if (first) m.label = s.dir > 0 ? "BALAYAGE →" : "← BALAYAGE";
            } else {
                m.kind = BossMark::Kind::Wave;
                m.area = {bladeX(s) - 24.0f, y - 4.0f, 48.0f, 4.0f};
                m.clip = "blade";
                m.flip = s.dir < 0;
                m.strong = true;
                m.color = kIce;
            }
            first = false;
            out.push_back(m);
        }
    }
    // Its nailing claws: a mark closing on the player aimed at, then the nailed one to free.
    for (const Pin& p : pins_) {
        BossMark m;
        m.kind = BossMark::Kind::Target;
        m.area = {p.at.x - 40.0f, p.at.y - 60.0f, 80.0f, 80.0f};
        m.color = p.nailed ? kIce : kRed;
        m.progress = p.nailed ? static_cast<float>(p.left) / static_cast<float>(std::max(1, steps(pinHold_)))
                              : std::clamp(static_cast<float>(p.t) / static_cast<float>(std::max(1, steps(pinWarn_))), 0.0f, 1.0f);
        m.strong = p.nailed;
        m.label = p.nailed ? "LIBÉREZ-LE" : "GRIFFE";
        out.push_back(m);
    }
    // Its chains, taut to its arm once held.
    if (b && b->alive())
        for (const Gigaboss::Vent& v : g.vents())
            if (v.closed || chained_) {
                BossMark m;
                m.kind = BossMark::Kind::Beam;
                m.area = {v.area.center().x, v.area.y - 40.0f, 0.0f, 0.0f};
                m.to = b->box.center();
                m.width = 5.0f;
                m.progress = 0.9f;
                m.color = Color{170, 220, 240, 255};
                out.push_back(m);
            }
}

void DreadfangRoutine::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    for (const Pin& p : pins_)
        if (p.nailed) {
            out.push_back({"✚", "Un allié est cloué au sol : brisez la griffe qui le tient (" + secs(p.left) + ").", kIce});
            break;
        }
    if (exposed(g)) {
        out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s",
                       "Dreadfang est enchaîné (" + ph + ") : brisez ses jointures · " + std::to_string(g.weakTotal() - g.weakLeft()) + " / " +
                           std::to_string(g.weakTotal()),
                       kGreen});
    } else if (armoured(g)) {
        out.push_back({std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents().size()),
                       "Armure de givre (" + ph + ") : tenez les " + std::to_string(g.vents().size()) +
                           " chaînes ensemble (une ancre chacun) : une chaîne tenue cède au bout de quelques secondes.",
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Dreadfang · " + ph + " : sautez ses lames de balayage, quittez les marques des griffes du sol.", kOrange});
    }
    for (const Sweep& s : sweeps_)
        if (!bladeOut(s)) {
            out.push_back({secs(steps(sweepWarn_) - s.t), "Balayage sur " + storeysOf(s.storeys) + " : sautez la lame ou changez d'étage.", kRed});
            break;
        }
}

void DreadfangRoutine::threats(const Gigaboss& g, std::vector<Vec2>& out) const {
    (void)g;
    for (const Pin& p : pins_) out.push_back(p.at);
}

void DreadfangRoutine::targets(std::vector<uint32_t>& out) const {
    for (const Pin& p : pins_)
        if (p.claw) out.push_back(p.claw);
}

std::unique_ptr<GigabossRoutine> makeDreadfangRoutine() { return std::make_unique<DreadfangRoutine>(); }

}  // namespace cosmic::raid
