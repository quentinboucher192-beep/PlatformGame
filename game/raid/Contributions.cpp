#include "game/raid/Contributions.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "game/enemies/Enemy.h"
#include "game/gameplay/GameWorld.h"

namespace cosmic::raid {

namespace {
uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}
int64_t tenths(float v) { return static_cast<int64_t>(std::lround(static_cast<double>(v) * 10.0)); }
}  // namespace

void Contributions::attach(GameWorld& w) {
    w.listener = this;
    tagOwner_.clear();
    broken_.clear();
    brokenSeen_ = 0;
    attack_ = nullptr;
    target_ = -1;
    targetHit_ = false;
    attackDamage_ = 0;
    weak_.clear();
    watched_.clear();
    boss_ = 0;
    for (int i = 0; i < 8; ++i) {
        hurtThisStep_[i] = false;
        revivedSeen_[i] = i < w.teamSize() && w.coop() ? w.member(i).revived : 0;
    }
}

void Contributions::setZone(int zone, std::vector<std::string> bosses) {
    zone_ = zone;
    zoneBosses_ = std::move(bosses);
}

ZoneContribution* Contributions::zoneLine(int seat) {
    if (zone_ <= 0 || !current(seat)) return nullptr;
    const size_t index = static_cast<size_t>(seatRecord_[seat]);
    std::vector<ZoneContribution>& lines = zones_[zone_];
    if (lines.size() < records_.size()) lines.resize(records_.size());
    ZoneContribution& z = lines[index];
    z.seat = records_[index].seat;
    z.from = records_[index].from;
    return &z;
}

std::vector<ZoneContribution> Contributions::zoneTable(int zone) const {
    std::vector<ZoneContribution> out;
    const auto it = zones_.find(zone);
    if (it == zones_.end()) return out;
    for (size_t i = 0; i < it->second.size() && i < records_.size(); ++i) {
        ZoneContribution z = it->second[i];
        z.seat = records_[i].seat;
        z.from = records_[i].from;
        if (z.bossDamage || z.damage || z.support || z.mechanisms || z.objectives) out.push_back(z);
    }
    std::stable_sort(out.begin(), out.end(), [](const ZoneContribution& a, const ZoneContribution& b) { return a.seat != b.seat ? a.seat < b.seat : a.from < b.from; });
    return out;
}

std::string Contributions::zoneCanonical(const std::vector<ZoneContribution>& rows) {
    std::string out;
    for (const ZoneContribution& z : rows)
        out += std::to_string(z.seat) + "," + std::to_string(z.from) + "," + std::to_string(z.bossDamage) + "," + std::to_string(z.damage) + "," +
               std::to_string(z.support) + "," + std::to_string(z.mechanisms) + "," + std::to_string(z.objectives) + ";";
    return out;
}

Contribution* Contributions::current(int seat) {
    if (seat < 0 || seat >= 8 || wasVacant_[seat] || seatRecord_[seat] < 0) return nullptr;
    return &records_[static_cast<size_t>(seatRecord_[seat])];
}

int Contributions::ownerOf(uint32_t tag) const {
    if (tag == 0) return -1;
    auto it = tagOwner_.find(tag);
    return it == tagOwner_.end() ? -1 : it->second;
}

int Contributions::brokeWeak(uint32_t uid) const {
    for (const auto& [u, seat] : broken_)
        if (u == uid) return seat;
    return -1;
}

void Contributions::afterStep(const GameWorld& w, uint32_t tick, const Enemy* boss) {
    using P = GameWorld::Presence;
    // 2.69: the weak points broken during this step stay for the gigaboss's rules (they run after
    // this); those of the step before go.
    broken_.erase(broken_.begin(), broken_.begin() + static_cast<std::ptrdiff_t>(std::min(brokenSeen_, broken_.size())));
    brokenSeen_ = broken_.size();
    if (!w.coop()) return;
    const int n = std::min(w.teamSize(), 8);
    for (int i = 0; i < n; ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        const bool vacant = m.presence == P::Vacant;
        if (!vacant && (wasVacant_[i] || seatRecord_[i] < 0)) {
            // A player takes the seat (the start of the instance, or a late join): a new record.
            Contribution c;
            c.seat = i;
            c.from = tick;
            records_.push_back(c);
            seatRecord_[i] = static_cast<int>(records_.size()) - 1;
        }
        wasVacant_[i] = vacant;
        if (vacant) continue;
        if (m.presence == P::Playing || m.presence == P::Down) ++records_[static_cast<size_t>(seatRecord_[i])].ticks;
    }
    // Allies revived: whoever stood by the soul (the world's own rule) gets the rescue.
    for (int i = 0; i < n; ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        if (m.revived <= revivedSeen_[i]) continue;
        revivedSeen_[i] = m.revived;
        for (int j = 0; j < n; ++j) {
            if (j == i || w.member(j).presence != P::Playing || !w.playerAt(j).alive()) continue;
            const Vec2 f = w.playerAt(j).feet();
            if (std::abs(f.x - m.soul.x) <= GameWorld::kReviveReachX + 8.0f && std::abs(f.y - m.soul.y) <= GameWorld::kReviveReachY + 8.0f)
                if (Contribution* c = current(j)) {
                    ++c->rescues;
                    if (ZoneContribution* z = zoneLine(j)) ++z->support;  // 2.75
                }
        }
    }
    // The gigaboss's blows: the player it aimed at, dodged or not.
    const AttackDef* a = boss && boss->alive() ? boss->currentAttack() : nullptr;
    const bool windUp = boss && boss->alive() && boss->state() == EnemyState::Telegraph && !prevTelegraph_;
    if (a != attack_ || windUp) {
        if (attack_ && target_ >= 0 && target_ < n && !targetHit_ && attackDamage_ > 0 && w.member(target_).presence == P::Playing && w.playerAt(target_).alive())
            if (Contribution* c = current(target_)) c->avoided += attackDamage_;
        attack_ = a;
        targetHit_ = false;
        target_ = a ? w.targetIndex(boss->center()) : -1;
        attackDamage_ = a ? tenths(a->damage) : 0;
    }
    prevTelegraph_ = boss && boss->alive() && boss->state() == EnemyState::Telegraph;
    if (target_ >= 0 && target_ < 8 && hurtThisStep_[target_]) targetHit_ = true;
    for (bool& h : hurtThisStep_) h = false;
}

void Contributions::objective(const GameWorld& w, int seat) {
    if (Contribution* c = current(seat)) {
        ++c->objectives;
        if (ZoneContribution* z = zoneLine(seat)) ++z->objectives;  // 2.75
        return;
    }
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i)
        if (w.coop() && w.member(i).presence == GameWorld::Presence::Playing)
            if (Contribution* c = current(i)) {
                ++c->objectives;
                if (ZoneContribution* z = zoneLine(i)) ++z->objectives;
            }
}

void Contributions::vent(int seat) {
    if (Contribution* c = current(seat)) ++c->vents;
    if (ZoneContribution* z = zoneLine(seat)) ++z->mechanisms;  // 2.75
}

void Contributions::rescue(int seat) {
    if (Contribution* c = current(seat)) ++c->rescues;
    if (ZoneContribution* z = zoneLine(seat)) ++z->support;  // 2.75
}

void Contributions::weakPoint(int seat) {
    if (Contribution* c = current(seat)) ++c->weakPoints;
    if (ZoneContribution* z = zoneLine(seat)) ++z->mechanisms;  // 2.75
}

void Contributions::dodged(int seat, float amount) {
    if (Contribution* c = current(seat)) c->avoided += tenths(amount);
}

void Contributions::gigabossDown(const GameWorld& w) {
    using P = GameWorld::Presence;
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i) {
        const P p = w.member(i).presence;
        if (p == P::Playing || p == P::Down || p == P::Standby)
            if (Contribution* c = current(i)) c->gigaboss = true;
    }
}

void Contributions::attackStarted(const GameWorld& w, uint32_t tag, const std::string&) {
    tagOwner_[tag] = w.currentPlayer();
    // A shot lives a few seconds: the oldest tags go (bounded memory, the same on every computer).
    while (tagOwner_.size() > 4096) tagOwner_.erase(tagOwner_.begin());
}

void Contributions::enemyDamaged(const GameWorld&, const Enemy& e, uint32_t tag, float amount) {
    const int seat = ownerOf(tag);
    Contribution* c = current(seat);
    if (!c) return;
    const int64_t t = tenths(amount);
    c->damage += t;
    const bool weak = std::find(weak_.begin(), weak_.end(), e.uid()) != weak_.end();
    if (boss_ != 0 && (e.uid() == boss_ || weak)) c->bossDamage += t;
    // 2.75: the zone's table: every enemy, and its boss and mini-bosses (a boss of the bestiary, an
    // enemy whose fall is an objective of the zone, its gigaboss and weak points).
    if (ZoneContribution* z = zoneLine(seat)) {
        z->damage += t;
        if ((boss_ != 0 && (e.uid() == boss_ || weak)) || e.def().isBoss ||
            (!e.objectId().empty() && std::find(zoneBosses_.begin(), zoneBosses_.end(), e.objectId()) != zoneBosses_.end()))
            z->bossDamage += t;
    }
    if (weak && e.hp() <= 0.0f && brokeWeak(e.uid()) < 0) broken_.emplace_back(e.uid(), seat);
    // 2.69 (phase 4B): a target of a mechanism (a stomach, a restraint, a core) broken by this seat.
    if (!weak && e.hp() <= 0.0f && brokeWeak(e.uid()) < 0 && std::find(watched_.begin(), watched_.end(), e.uid()) != watched_.end())
        broken_.emplace_back(e.uid(), seat);
}

void Contributions::blowBlocked(const GameWorld& w, float amount) {
    if (Contribution* c = current(w.currentPlayer())) {
        ++c->blocks;
        c->avoided += tenths(amount);
        if (ZoneContribution* z = zoneLine(w.currentPlayer())) ++z->support;  // 2.75
    }
}

void Contributions::playerHurt(const GameWorld& w, const DamageInfo&, const HurtSource& src) {
    if (boss_ == 0 || !src.enemy || src.enemy->uid() != boss_) return;
    const int i = w.currentPlayer();
    if (i >= 0 && i < 8) hurtThisStep_[i] = true;
}

uint64_t Contributions::hash() const {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, records_.size());
    for (const Contribution& c : records_) {
        h = mix(h, static_cast<uint64_t>(c.seat));
        h = mix(h, c.from);
        h = mix(h, static_cast<uint64_t>(c.damage));
        h = mix(h, static_cast<uint64_t>(c.bossDamage));
        h = mix(h, static_cast<uint64_t>(c.avoided));
        h = mix(h, (static_cast<uint64_t>(c.blocks) << 32) | static_cast<uint32_t>(c.rescues));
        h = mix(h, (static_cast<uint64_t>(c.vents) << 32) | static_cast<uint32_t>(c.weakPoints));
        h = mix(h, (static_cast<uint64_t>(c.objectives) << 32) | c.ticks);
        h = mix(h, c.gigaboss ? 1u : 0u);
    }
    return h;
}

std::string Contributions::canonical() const {
    std::vector<Contribution> list = records_;
    std::stable_sort(list.begin(), list.end(), [](const Contribution& a, const Contribution& b) { return a.seat != b.seat ? a.seat < b.seat : a.from < b.from; });
    std::string out;
    for (const Contribution& c : list) {
        out += std::to_string(c.seat) + "," + std::to_string(c.from) + "," + std::to_string(c.damage) + "," + std::to_string(c.bossDamage) + "," +
               std::to_string(c.avoided) + "," + std::to_string(c.blocks) + "," + std::to_string(c.rescues) + "," + std::to_string(c.vents) + "," +
               std::to_string(c.weakPoints) + "," + std::to_string(c.objectives) + "," + std::to_string(c.ticks) + "," + (c.gigaboss ? "1" : "0") + ";";
    }
    return out;
}

}  // namespace cosmic::raid
