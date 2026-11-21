#include "game/raid/HeartbreakerRoutine.h"

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
const std::string kWave = "Onde du battement";
const Color kOrange{255, 176, 74, 255};
const Color kGold{255, 210, 122, 255};
const Color kGreen{124, 240, 168, 255};
const Color kRed{255, 138, 138, 255};
const Color kPink{255, 93, 143, 255};
}  // namespace

void HeartbreakerRoutine::begin(GameWorld& w, const GigabossRules& r) {
    (void)w;
    *this = HeartbreakerRoutine{};
    rules_ = &r;
    const Json& g = r.extra;
    const Json plates = g.value("plates", Json::object());
    for (const auto& s : plates.value("spots", Json::array()))
        if (s.is_array() && s.size() == 2) plateSpots_.push_back({s[0].get<float>(), s[1].get<float>()});
    plateEnemy_ = jstr(plates, "enemy", plateEnemy_);
    window_ = std::clamp(jget<float>(plates, "window", window_), 5.0f, 60.0f);
    plateShare_ = std::clamp(jget<float>(plates, "share", plateShare_), 0.0f, 0.2f);
    const Json beats = g.value("beats", Json::object());
    beatPeriods_ = floatsOf(beats.value("periods", Json()), {2.0f, 1.6f, 1.25f});
    for (float& p : beatPeriods_) p = std::clamp(p, 0.6f, 10.0f);
    strong_ = floatsOf(beats.value("strong", Json()), {4, 3, 2});
    waveSpeed_ = std::clamp(jget<float>(beats, "speed", waveSpeed_), 100.0f, 2000.0f);
    waveHeight_ = std::clamp(jget<float>(beats, "height", waveHeight_), 12.0f, 96.0f);
    waveDamage_ = std::clamp(jget<float>(beats, "damage", waveDamage_), 0.0f, 4.0f);
    floorY_ = jget<float>(beats, "floor", 0.0f);
    from_ = floatsOf(beats.value("from", Json()), {-170.0f, 170.0f});
    const std::vector<float> span = floatsOf(beats.value("span", Json()), {});
    if (span.size() == 2 && span[1] > span[0]) {
        spanX0_ = span[0];
        spanX1_ = span[1];
    }
    plates_.assign(plateSpots_.size(), 0);
    plated_ = !plateSpots_.empty();
}

int HeartbreakerRoutine::platesLeft() const {
    int n = 0;
    for (uint32_t u : plates_) n += u != 0 ? 1 : 0;
    return n;
}

float HeartbreakerRoutine::windowLeft() const { return static_cast<float>(std::max(0, windowTicks_)) / static_cast<float>(kSimHz); }

int HeartbreakerRoutine::strongEvery(int phase) const { return std::max(1, static_cast<int>(std::lround(at(strong_, phase, 3.0f)))); }

float HeartbreakerRoutine::waveFront(const Wave& v) const {
    return v.x0 + static_cast<float>(v.dir) * waveSpeed_ * static_cast<float>(v.t) / static_cast<float>(kSimHz);
}

void HeartbreakerRoutine::step(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    const GigabossRules& r = *rules_;
    ticks_ = ticks(g);
    const int ph = phase(g);
    phase_ = ph;
    players_ = static_cast<int>(playing(w).size());
    stepPlates(w, c, g, b);
    if (!b.alive()) return;
    stepBeats(w, b, ph);
    const bool cage = !b.lastStand() && ph >= r.carapaceFrom && ph < r.carapaceUntil && !r.vents.empty();
    if (exposed(g)) {
        // Its ribs held open: the light of its heart bare.
        if (exposedStep(g, w, c, b)) return;
        resetPlates(g);
        if (cage) {
            setArmour(g, true);
            alert(g, "GIGABOSS · Les côtes se referment sur le cœur : écartez-les de nouveau, des deux côtés !", Danger);
        } else {
            setFight(g);
        }
        return;
    }
    if (plated_) {  // its first phase: the plates are its armour
        if (!armoured(g)) setArmour(g, true);
        return;
    }
    if (!cage) {
        if (armoured(g) && ph >= r.carapaceUntil) alert(g, "GIGABOSS · La cage se brise : le cœur bat à découvert !", Branch);
        setFight(g);
        return;
    }
    if (!armoured(g)) {
        setArmour(g, true);
        resetPlates(g);
        if (!cageSaid_)
            alert(g, "GIGABOSS · La cage thoracique se referme : un joueur sur chaque côte, tenez-les ouvertes ensemble (" +
                         std::to_string(static_cast<int>(std::lround(r.hold * 10.0f)) / 10) + " s) !",
                  Danger);
        cageSaid_ = true;
    }
    if (holdPlates(g, w, c, b, {"GROUPE · Côte écartée", "GIGABOSS · Une côte se referme : écartez-la de nouveau !"}))
        expose(g, w, c, b,
               "GIGABOSS · Les côtes s'écartent : la lumière du cœur est à nu (" + std::to_string(static_cast<int>(std::lround(r.exposed))) +
                   " s) ! Frappez-la !");
}

// ------------------------------------------------------------------------ plates (phase 1)
void HeartbreakerRoutine::spawnPlates(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    const float factor = (1.0f + rules_->weakHpPerPlayer * static_cast<float>(std::max(0, scaledFor(g) - 1))) * g.healthScale();
    for (size_t k = 0; k < plateSpots_.size(); ++k) {
        if (plates_[k] != 0) continue;
        const Vec2 spot{b.box.center().x + plateSpots_[k].x, b.box.bottom() + plateSpots_[k].y};
        if (Enemy* e = w.spawnEnemy(plateEnemy_, spot, false)) {
            e->summonedBy = b.uid();
            e->countsForStats = false;
            e->scaleHealth(factor);
            plates_[k] = e->uid();
        }
    }
    std::vector<uint32_t> watched;
    for (uint32_t u : plates_)
        if (u) watched.push_back(u);
    c.setWatched(watched);
}

void HeartbreakerRoutine::stepPlates(GameWorld& w, Contributions& c, Gigaboss& g, Enemy& b) {
    if (!plated_) return;
    if (phase_ > 1 || b.lastStand()) {
        // Past its first phase (an administrator's test starting later): no armour of plates.
        for (uint32_t& u : plates_) {
            if (Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) e->despawn();
            u = 0;
        }
        plated_ = false;
        c.setWatched({});
        return;
    }
    if (!platesUp_) {
        platesUp_ = true;
        spawnPlates(w, c, g, b);
        alert(g, "GIGABOSS · Quatre plaques d'armure gardent le cœur : brisez-les ensemble, elles repoussent " +
                     std::to_string(static_cast<int>(std::lround(window_ * slack4(players_)))) + " s après la première !",
              Danger);
        return;
    }
    int broke = 0;
    for (size_t k = 0; k < plates_.size(); ++k) {
        if (plates_[k] == 0) continue;
        Enemy* e = w.findEnemy(plates_[k]);
        if (e && e->alive()) {
            // Stuck to its chest.
            e->box = Rect::fromBottomCenter({b.box.center().x + plateSpots_[k].x, b.box.bottom() + plateSpots_[k].y}, e->box.size());
            e->prevBox = e->box;
            e->vel = {0.0f, 0.0f};
            continue;
        }
        const int seat = c.brokeWeak(plates_[k]);
        if (seat >= 0) c.weakPoint(seat);
        plates_[k] = 0;
        ++broke;
        if (plateShare_ > 0.0f) b.wound(w, plateShare_ * b.maxHp());
    }
    if (broke > 0) {
        if (windowTicks_ <= 0) windowTicks_ = std::max(1, steps(window_ * slack4(players_)));  // the first of a round: the others must follow
        const int left = platesLeft();
        alert(g, "GROUPE · Plaque brisée (" + std::to_string(static_cast<int>(plates_.size()) - left) + " / " + std::to_string(plates_.size()) + ")", Group);
        w.playSound("sounds/shell_shatter.wav", b.box.center(), 0.9f);
        if (left == 0) {
            plated_ = false;
            windowTicks_ = 0;
            c.setWatched({});
            setFight(g);
            w.shake(0.9f);
            w.flash({255, 200, 200, 255}, 0.25f);
            w.particles().emit("rock_shatter", b.box.center(), 3.0f);
            alert(g, "GIGABOSS · La cuirasse tombe : Heartbreaker est vulnérable, frappez-le !", Branch);
            return;
        }
    }
    if (windowTicks_ > 0 && --windowTicks_ == 0) {
        // Too slow: the broken plates grow back (whole), the round starts again.
        spawnPlates(w, c, g, b);
        ++regrown_;
        alert(g, "GIGABOSS · Les plaques repoussent : brisez les quatre en moins de " + std::to_string(static_cast<int>(std::lround(window_ * slack4(players_)))) +
                     " s, plusieurs groupes à la fois !",
              Danger);
    }
}

// ------------------------------------------------------------------------ beats
void HeartbreakerRoutine::stepBeats(GameWorld& w, Enemy& b, int phase) {
    if (--beatClock_ <= 0) {
        beatClock_ = std::max(steps(0.5f), steps(at(beatPeriods_, phase, 1.6f) / std::max(1.0f, b.pressure())));
        ++beats_;
        lastBeat_ = std::max<uint32_t>(1, ticks_);
        lastStrong_ = beats_ % strongEvery(phase) == 0 && !b.lastStand();
        const Vec2 heart{b.box.center().x, b.box.bottom() - b.box.h * 0.53f};
        w.particles().emit(lastStrong_ ? "impulse_ring" : "heart_pickup", heart, lastStrong_ ? 1.6f : 0.6f);
        w.playSound("sounds/story_heartbeat.wav", heart, lastStrong_ ? 1.0f : 0.5f, lastStrong_ ? 0.9f : 1.0f);
        if (lastStrong_) {
            // Its strong beat: a wave along the floor from both its sides, to the walls.
            for (float f : from_) {
                Wave v;
                v.x0 = b.box.center().x + f;
                v.dir = f < 0.0f ? -1 : 1;
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
        for (int i : playing(w)) {
            if ((v.hit >> i) & 1u || !band.intersects(w.playerAt(i).box)) continue;
            v.hit |= static_cast<uint8_t>(1u << i);
            const Vec2 cc = w.playerAt(i).box.center();
            strike(w, b, {cc.x - 1.0f, cc.y - 1.0f, 2.0f, 2.0f}, waveDamage_, 260.0f, kWave);
        }
        if (v.t % 6 == 0) w.particles().emit("shockwave_dust", {x, floor - 4.0f}, 0.5f);
    }
    std::erase_if(waves_, [&](const Wave& v) { return waveFront(v) < spanX0_ - 20.0f || waveFront(v) > spanX1_ + 20.0f; });
}

void HeartbreakerRoutine::fell(GameWorld& w, Gigaboss& g) {
    (void)g;
    for (uint32_t& u : plates_) {
        if (Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) e->despawn();
        u = 0;
    }
    waves_.clear();
}

uint64_t HeartbreakerRoutine::hash() const {
    uint64_t h = 0x4ea27b2eULL;
    h = mix(h, (static_cast<uint64_t>(phase_) << 16) | (plated_ ? 1u : 0u) | (platesUp_ ? 2u : 0u) | (cageSaid_ ? 4u : 0u) | (lastStrong_ ? 8u : 0u));
    for (uint32_t u : plates_) h = mix(h, u);
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(windowTicks_)) << 32) | static_cast<uint32_t>(regrown_));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(beatClock_)) << 32) | static_cast<uint32_t>(beats_));
    for (const Wave& v : waves_)
        h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(v.t)) << 32) ^ static_cast<uint64_t>(std::lround(v.x0 * 4.0f)) ^
                       (static_cast<uint64_t>(v.hit) << 24) ^ (v.dir > 0 ? 1ULL << 62 : 0));
    return h;
}

// ------------------------------------------------------------------------ display
void HeartbreakerRoutine::marks(const GameWorld& w, const Gigaboss& g, std::vector<BossMark>& out) const {
    const Enemy* b = g.boss(w);
    const float floor = floorY_ > 0.0f ? floorY_ : (b ? b->box.bottom() : 0.0f);
    for (const Wave& v : waves_) {
        BossMark m;
        m.kind = BossMark::Kind::Wave;
        m.area = {waveFront(v) - 24.0f, floor - 4.0f, 48.0f, 4.0f};
        m.clip = "wave";
        m.flip = v.dir < 0;
        m.strong = true;
        m.color = kPink;
        out.push_back(m);
    }
    if (b && b->alive() && lastBeat_ > 0 && ticks_ - lastBeat_ < static_cast<uint32_t>(steps(0.3f))) {
        BossMark m;  // its beat, a ring around its heart
        const Vec2 heart{b->box.center().x, b->box.bottom() - b->box.h * 0.53f};
        const float rad = lastStrong_ ? 110.0f : 70.0f;
        m.kind = BossMark::Kind::Target;
        m.area = {heart.x - rad, heart.y - rad, rad * 2.0f, rad * 2.0f};
        m.progress = 1.0f;
        m.strong = lastStrong_;
        m.color = kPink;
        out.push_back(m);
    }
    if (plated_ && windowTicks_ > 0 && b && b->alive()) {
        BossMark m;  // the plates' round: the seconds before the broken ones grow back
        m.kind = BossMark::Kind::Target;
        m.area = b->box.expanded(30.0f);
        m.progress = windowLeft() / std::max(1.0f, window_ * slack4(players_));
        m.color = kRed;
        m.label = "PLAQUES · " + secs(windowTicks_);
        m.strong = true;
        out.push_back(m);
    }
}

void HeartbreakerRoutine::lines(const GameWorld& w, const Gigaboss& g, std::vector<BossLine>& out) const {
    (void)w;
    const std::string ph = "phase " + std::to_string(std::max(1, g.phase())) + " / 3";
    if (plated_) {
        const int total = std::max(1, static_cast<int>(plates_.size()));
        out.push_back({windowTicks_ > 0 ? secs(windowTicks_) : std::to_string(total - platesLeft()) + " / " + std::to_string(total),
                       "Cuirasse (" + ph + ") : brisez ses " + std::to_string(total) + " plaques ensemble (les hautes depuis les tours) : elles repoussent si la dernière tarde.",
                       windowTicks_ > 0 ? kRed : kGold});
    } else if (exposed(g)) {
        out.push_back({std::to_string(static_cast<int>(std::ceil(g.exposedLeft()))) + " s", "Le cœur est à nu (" + ph + ") : frappez sa lumière entre les côtes.", kGreen});
    } else if (armoured(g)) {
        out.push_back({std::to_string(g.ventsClosed()) + " / " + std::to_string(g.vents().size()),
                       "Cage thoracique (" + ph + ") : un joueur sur chaque côte, des deux côtés à la fois · côtes tenues " + std::to_string(g.ventsClosed()) +
                           " / " + std::to_string(g.vents().size()),
                       kGold});
    } else {
        out.push_back({"→", "Vaincre Heartbreaker · " + ph + " : frappez la cage, quittez les côtes-pièges.", kOrange});
    }
    out.push_back({"◆", "Battements : une onde court au sol à chaque battement fort (un sur " + std::to_string(strongEvery(std::max(1, g.phase()))) + ") : sautez-la.",
                   kPink});
}

void HeartbreakerRoutine::threats(const Gigaboss& g, std::vector<Vec2>& out) const {
    (void)g;
    for (const Wave& v : waves_) out.push_back({waveFront(v), floorY_});
}

void HeartbreakerRoutine::targets(std::vector<uint32_t>& out) const {
    for (uint32_t u : plates_)
        if (u) out.push_back(u);
}

std::unique_ptr<GigabossRoutine> makeHeartbreakerRoutine() { return std::make_unique<HeartbreakerRoutine>(); }

}  // namespace cosmic::raid
