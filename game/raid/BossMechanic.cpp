#include "game/raid/BossMechanic.h"

#include <cmath>

#include "game/enemies/Enemy.h"
#include "game/gameplay/Damage.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/raid/Contributions.h"
#include "game/raid/DreadfangFight.h"
#include "game/raid/HeartbreakerFight.h"
#include "game/raid/SkullmawFight.h"

namespace cosmic::raid {

std::unique_ptr<BossMechanic> makeBossMechanic(const GigabossRules& rules) {
    if (rules.kind == "skullmaw") return std::make_unique<SkullmawFight>(rules);
    if (rules.kind == "dreadfang") return std::make_unique<DreadfangFight>(rules);
    if (rules.kind == "heartbreaker") return std::make_unique<HeartbreakerFight>(rules);
    return nullptr;
}

namespace mech {

int steps(float seconds) { return static_cast<int>(std::lround(seconds * static_cast<float>(kSimHz))); }

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

bool inPlay(const GameWorld& w, int i) {
    if (!w.coop() || i < 0 || i >= std::min(w.teamSize(), 8)) return false;
    if (w.member(i).presence != GameWorld::Presence::Playing) return false;
    const Player& p = w.playerAt(i);
    return p.alive() && !p.respawning();
}

int countInPlay(const GameWorld& w) {
    int n = 0;
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i) n += inPlay(w, i) ? 1 : 0;
    return n;
}

bool hurtSeat(GameWorld& w, Enemy& b, int seat, float damage, Vec2 source, float knockback, const std::string& name) {
    if (!inPlay(w, seat) || damage <= 0.0f) return false;
    // The middle of its box: nobody else is hit by a blow aimed at this player alone.
    const Vec2 c = w.playerAt(seat).box.center();
    DamageInfo d;
    d.amount = damage;
    d.type = DamageType::Explosion;
    d.source = source;
    d.knockback = knockback;
    return w.hurtPlayersByBoss({c.x - 1.0f, c.y - 1.0f, 2.0f, 2.0f}, d, &b, &name);
}

std::vector<float> floats(const Json& j, std::vector<float> fallback) {
    if (!j.is_array()) return fallback;
    std::vector<float> out;
    for (const auto& v : j)
        if (v.is_number()) out.push_back(v.get<float>());
    return out.empty() ? fallback : out;
}

float byPhase(const std::vector<float>& v, int phase, float fallback) {
    if (v.empty()) return fallback;
    return v[static_cast<size_t>(std::clamp(phase - 1, 0, static_cast<int>(v.size()) - 1))];
}

namespace {
Rect rectOf(const Json& j) {
    if (!j.is_array() || j.size() != 4) return {};
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}
uint64_t px(float v) { return static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * 4.0f)))); }
}  // namespace

// ------------------------------------------------------------------------ marks
void Marks::read(const Json& j) {
    if (!j.is_object()) return;
    periods = floats(j.value("periods", Json()), {8.0f});
    for (float& p : periods) p = std::clamp(p, 2.0f, 60.0f);
    warn = std::clamp(jget<float>(j, "warn", warn), 0.4f, 4.0f);
    width = std::clamp(jget<float>(j, "width", width), 16.0f, 200.0f);
    height = std::clamp(jget<float>(j, "height", height), 16.0f, 320.0f);
    damage = std::clamp(jget<float>(j, "damage", damage), 0.0f, 4.0f);
    knockback = jget<float>(j, "knockback", knockback);
}

bool Marks::step(GameWorld& w, Enemy& b, int phase, Contributions& c, const std::string& name) {
    if (periods.empty() || b.lastStand()) {
        marks.clear();
        return false;
    }
    bool volley = false;
    const float period = byPhase(periods, phase, 8.0f) / std::max(1.0f, b.pressure());
    const int warnS = warnSteps(), strike = std::max(1, steps(0.25f));
    if (clock <= 0) {
        clock = std::max(warnS + strike + 1, steps(period));
        marks.clear();
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i)
            if (inPlay(w, i)) marks.push_back({w.playerAt(i).feet(), warnS, i});
        volley = !marks.empty();
    }
    --clock;
    for (Mark& m : marks) {
        if (m.left > -strike) --m.left;
        if (m.left > 0 || m.left <= -strike) continue;
        const Rect a = area(m);
        DamageInfo d;
        d.amount = damage;
        d.type = DamageType::Explosion;
        d.source = {m.at.x, m.at.y};
        d.knockback = knockback;
        w.hurtPlayersByBoss(a, d, &b, &name);
        // Whoever left their own mark in time avoided the blow (the raid's contributions).
        if (m.left == 0 && inPlay(w, m.seat) && !w.playerAt(m.seat).box.intersects(a)) c.dodged(m.seat, damage);
    }
    std::erase_if(marks, [&](const Mark& m) { return m.left <= -strike; });
    return volley;
}

void Marks::hashInto(uint64_t& h) const {
    h = mix(h, static_cast<uint64_t>(static_cast<uint32_t>(clock)));
    for (const Mark& m : marks) h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(m.left)) << 32) ^ px(m.at.x) ^ (px(m.at.y) << 16) ^ (static_cast<uint64_t>(m.seat & 0xFF) << 56));
}

// ------------------------------------------------------------------------ holds
void Holds::read(const Json& arr) {
    places.clear();
    if (!arr.is_array()) return;
    for (const auto& p : arr) {
        if (!p.is_object()) continue;
        Place pl;
        pl.id = jstr(p, "id");
        pl.area = rectOf(p.value("rect", Json()));
        if (pl.area.w > 0.0f && pl.area.h > 0.0f) places.push_back(pl);
    }
}

void Holds::sense(const GameWorld& w) {
    for (Place& p : places) {
        p.holders = 0;
        for (int i = 0; i < std::min(w.teamSize(), 8); ++i)
            if (inPlay(w, i) && p.area.intersects(w.playerAt(i).box)) p.holders |= static_cast<uint8_t>(1u << i);
    }
}

int Holds::held() const {
    int n = 0;
    for (const Place& p : places) n += p.holders ? 1 : 0;
    return n;
}

int Holds::holders() const {
    uint8_t all = 0;
    for (const Place& p : places) all |= p.holders;
    int n = 0;
    for (int i = 0; i < 8; ++i) n += (all >> i) & 1u;
    return n;
}

bool Holds::holds(int seat) const {
    for (const Place& p : places)
        if (seat >= 0 && seat < 8 && (p.holders >> seat) & 1u) return true;
    return false;
}

void Holds::reset() {
    for (Place& p : places) {
        p.hold = 0;
        p.locked = false;
        p.lockedAt = 0;
    }
}

void Holds::hashInto(uint64_t& h) const {
    for (const Place& p : places)
        h = mix(h, (static_cast<uint64_t>(p.holders) << 56) ^ (static_cast<uint64_t>(static_cast<uint32_t>(p.hold)) << 24) ^
                       (static_cast<uint64_t>(p.locked) << 20) ^ p.lockedAt);
}

// ------------------------------------------------------------------------ weak points
void WeakPoints::read(const Json& g) {
    spots.clear();
    const std::string fallback = jstr(g, "weakEnemy");
    const float share = std::clamp(jget<float>(g, "weakShare", 0.04f), 0.0f, 0.2f);
    const std::vector<float> shares = floats(g.value("weakShares", Json()));
    for (const auto& p : g.value("weakSpots", Json::array())) {
        if (!p.is_array() || p.size() < 2) continue;
        Spot s;
        s.at = {p[0].get<float>(), p[1].get<float>()};
        s.enemy = p.size() >= 3 && p[2].is_string() ? p[2].get<std::string>() : fallback;
        const size_t k = spots.size();
        s.share = std::clamp(k < shares.size() ? shares[k] : share, 0.0f, 0.2f);
        if (!s.enemy.empty()) spots.push_back(s);
    }
    hpPerPlayer = std::clamp(jget<float>(g, "weakHpPerPlayer", hpPerPlayer), 0.0f, 2.0f);
    uids.assign(spots.size(), 0);
}

void WeakPoints::spawn(GameWorld& w, Enemy& b, int scaledFor, const std::vector<int>& which) {
    uids.resize(spots.size(), 0);
    const float factor = 1.0f + hpPerPlayer * static_cast<float>(std::max(0, scaledFor - 1));
    for (size_t k = 0; k < spots.size(); ++k) {
        if (!which.empty() && std::find(which.begin(), which.end(), static_cast<int>(k)) == which.end()) continue;
        if (uids[k] != 0) continue;
        const Vec2 at{b.box.center().x + spots[k].at.x * (mirror ? b.facing : 1.0f), b.box.bottom() + spots[k].at.y};
        if (Enemy* e = w.spawnEnemy(spots[k].enemy, at, b.facing < 0.0f)) {
            e->summonedBy = b.uid();
            e->countsForStats = false;
            e->scaleHealth(factor);
            uids[k] = e->uid();
        }
    }
    total = static_cast<int>(spots.size());
    glue(w, b);
}

int WeakPoints::step(GameWorld& w, Enemy& b, Contributions& c, std::vector<int>* seats) {
    int n = 0;
    for (size_t k = 0; k < uids.size() && k < spots.size(); ++k) {
        if (uids[k] == 0) continue;
        const Enemy* e = w.findEnemy(uids[k]);
        if (e && e->alive()) continue;
        const int seat = c.brokeWeak(uids[k]);
        if (seat >= 0) c.weakPoint(seat);
        if (seats) seats->push_back(seat);
        uids[k] = 0;
        ++n;
        if (spots[k].share > 0.0f && b.alive()) b.wound(w, spots[k].share * b.maxHp());
    }
    if (b.alive()) glue(w, b);
    return n;
}

void WeakPoints::glue(GameWorld& w, const Enemy& b) {
    for (size_t k = 0; k < uids.size() && k < spots.size(); ++k) {
        Enemy* e = uids[k] ? w.findEnemy(uids[k]) : nullptr;
        if (!e || !e->alive()) continue;
        const Vec2 at{b.box.center().x + spots[k].at.x * (mirror ? b.facing : 1.0f), b.box.bottom() + spots[k].at.y};
        e->box = Rect::fromBottomCenter(at, e->box.size());
        e->prevBox = e->box;
        e->vel = {0.0f, 0.0f};
    }
}

void WeakPoints::clear(GameWorld& w) {
    for (uint32_t& u : uids) {
        if (Enemy* e = u ? w.findEnemy(u) : nullptr; e && e->alive()) e->despawn();
        u = 0;
    }
    total = 0;
}

int WeakPoints::left() const {
    int n = 0;
    for (uint32_t u : uids) n += u != 0 ? 1 : 0;
    return n;
}

void WeakPoints::hashInto(uint64_t& h) const {
    h = mix(h, static_cast<uint64_t>(total));
    for (uint32_t u : uids) h = mix(h, u);
}

}  // namespace mech
}  // namespace cosmic::raid
