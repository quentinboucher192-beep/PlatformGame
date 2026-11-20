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
constexpr uint8_t kVacant = static_cast<uint8_t>(GameWorld::Presence::Vacant);
constexpr uint8_t kDown = static_cast<uint8_t>(GameWorld::Presence::Down);
constexpr uint8_t kPlaying = static_cast<uint8_t>(GameWorld::Presence::Playing);
constexpr uint8_t kStandby = static_cast<uint8_t>(GameWorld::Presence::Standby);

bool anything(const ZoneContribution& z) {
    return z.bossDamage || z.damage || z.support || z.mechanisms || z.objectives || z.deaths || z.revives || z.revived;
}
}  // namespace

Contributions::Scope* Contributions::scopeOf(const GameWorld& w) {
    if (focus_ && focus_->world == &w) return focus_;
    for (auto& s : scopes_)
        if (s->world == &w) return s.get();
    return nullptr;
}

const Contributions::Scope* Contributions::scopeOf(const GameWorld& w) const {
    for (const auto& s : scopes_)
        if (s->world == &w) return s.get();
    return nullptr;
}

void Contributions::attach(GameWorld& w, int zone, std::vector<std::string> bosses, std::vector<std::string> minis) {
    w.listener = this;
    Scope* s = scopeOf(w);
    if (!s) {
        scopes_.push_back(std::make_unique<Scope>());
        s = scopes_.back().get();
    }
    *s = Scope{};
    s->world = &w;
    s->zone = zone;
    s->bosses = std::move(bosses);
    s->minis = std::move(minis);
    for (int i = 0; i < 8; ++i) s->revivedSeen[i] = i < w.teamSize() && w.coop() ? w.member(i).revived : 0;
    focus_ = s;
}

void Contributions::detach(const GameWorld& w) {
    for (size_t k = 0; k < scopes_.size(); ++k)
        if (scopes_[k]->world == &w) {
            if (focus_ == scopes_[k].get()) focus_ = nullptr;
            scopes_.erase(scopes_.begin() + static_cast<std::ptrdiff_t>(k));
            return;
        }
}

void Contributions::focus(const GameWorld& w) { focus_ = scopeOf(w); }

ZoneContribution* Contributions::zoneLine(int zone, int seat) {
    if (zone <= 0 || !current(seat)) return nullptr;
    const size_t index = static_cast<size_t>(seatRecord_[seat]);
    std::vector<ZoneContribution>& lines = zones_[zone];
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
        if (anything(z)) out.push_back(z);
    }
    std::stable_sort(out.begin(), out.end(), [](const ZoneContribution& a, const ZoneContribution& b) { return a.seat != b.seat ? a.seat < b.seat : a.from < b.from; });
    return out;
}

std::vector<ZoneContribution> Contributions::raidTable() const {
    std::vector<ZoneContribution> sum(records_.size());
    for (size_t i = 0; i < records_.size(); ++i) {
        sum[i].seat = records_[i].seat;
        sum[i].from = records_[i].from;
    }
    for (const auto& [zone, lines] : zones_)
        for (size_t i = 0; i < lines.size() && i < sum.size(); ++i) {
            const ZoneContribution& z = lines[i];
            ZoneContribution& s = sum[i];
            s.bossDamage += z.bossDamage;
            s.damage += z.damage;
            s.support += z.support;
            s.mechanisms += z.mechanisms;
            s.objectives += z.objectives;
            s.miniDamage += z.miniDamage;
            s.deaths += z.deaths;
            s.revives += z.revives;
            s.revived += z.revived;
        }
    std::vector<ZoneContribution> out;
    for (const ZoneContribution& z : sum)
        if (anything(z)) out.push_back(z);
    std::stable_sort(out.begin(), out.end(), [](const ZoneContribution& a, const ZoneContribution& b) { return a.seat != b.seat ? a.seat < b.seat : a.from < b.from; });
    return out;
}

std::vector<int> Contributions::zonesWithTable() const {
    std::vector<int> out;
    for (const auto& [zone, lines] : zones_)
        if (!zoneTable(zone).empty()) out.push_back(zone);
    return out;
}

std::string Contributions::zoneCanonical(const std::vector<ZoneContribution>& rows) {
    std::string out;
    for (const ZoneContribution& z : rows) {
        out += std::to_string(z.seat) + "," + std::to_string(z.from) + "," + std::to_string(z.bossDamage) + "," + std::to_string(z.damage) + "," +
               std::to_string(z.support) + "," + std::to_string(z.mechanisms) + "," + std::to_string(z.objectives);
        // 2.78: the mini-bosses, the deaths, the revivals done and received (the server rebuilds the same text).
        if (z.v2)
            out += "," + std::to_string(z.miniDamage) + "," + std::to_string(z.deaths) + "," + std::to_string(z.revives) + "," + std::to_string(z.revived);
        out += ";";
    }
    return out;
}

Contribution* Contributions::current(int seat) {
    if (seat < 0 || seat >= 8 || wasVacant_[seat] || seatRecord_[seat] < 0) return nullptr;
    return &records_[static_cast<size_t>(seatRecord_[seat])];
}

int Contributions::ownerOf(const GameWorld& w, uint32_t tag) const {
    if (tag == 0) return -1;
    const Scope* s = scopeOf(w);
    if (!s) return -1;
    auto it = s->tagOwner.find(tag);
    return it == s->tagOwner.end() ? -1 : it->second;
}

int Contributions::ownerOf(uint32_t tag) const {
    if (tag == 0 || !focus_) return -1;
    auto it = focus_->tagOwner.find(tag);
    return it == focus_->tagOwner.end() ? -1 : it->second;
}

int Contributions::brokeWeak(uint32_t uid) const {
    if (!focus_) return -1;
    for (const auto& [u, seat] : focus_->broken)
        if (u == uid) return seat;
    return -1;
}

void Contributions::setBoss(uint32_t uid) {
    if (focus_) focus_->boss = uid;
}
void Contributions::setWeak(const std::vector<uint32_t>& uids) {
    if (focus_) focus_->weak = uids;
}
void Contributions::setWatched(const std::vector<uint32_t>& uids) {
    if (focus_) focus_->watched = uids;
}

void Contributions::seats(uint32_t tick, const SeatView* views, int n) {
    n = std::min(n, 8);
    for (int i = 0; i < n; ++i) {
        const SeatView& v = views[i];
        const bool vacant = v.presence == kVacant || v.presence == static_cast<uint8_t>(GameWorld::Presence::Gone);
        if (!vacant && (wasVacant_[i] || seatRecord_[i] < 0)) {
            // A player takes the seat (the start of the instance, or a late join): a new record.
            Contribution c;
            c.seat = i;
            c.from = tick;
            records_.push_back(c);
            seatRecord_[i] = static_cast<int>(records_.size()) - 1;
        }
        // A seat gone for good keeps its record until somebody else takes it (Vacant then).
        wasVacant_[i] = v.presence == kVacant;
        if (v.presence == kVacant) {
            prevPresence_[i] = v.presence;
            continue;
        }
        if (seatRecord_[i] < 0) continue;
        if (v.presence == kPlaying || v.presence == kDown) ++records_[static_cast<size_t>(seatRecord_[i])].ticks;
        // 2.78: the deaths (in play, then down) and the revivals (down, then up again: only a revival
        // brings a player down back on their feet), counted in the zone of the seat.
        const uint8_t before = prevPresence_[i];
        if (v.presence == kDown && before != kDown && before != kVacant) {
            if (ZoneContribution* z = zoneLine(v.zone, i)) ++z->deaths;
        } else if (before == kDown && (v.presence == kPlaying || v.presence == kStandby)) {
            if (ZoneContribution* z = zoneLine(v.zone, i)) ++z->revived;
            if (v.reviver >= 0 && v.reviver < n && v.reviver != i)
                if (ZoneContribution* z = zoneLine(v.zone, v.reviver)) ++z->revives;
        }
        prevPresence_[i] = v.presence;
    }
}

void Contributions::afterStep(const GameWorld& w, uint32_t, const Enemy* boss) {
    using P = GameWorld::Presence;
    Scope* s = scopeOf(w);
    if (!s) return;
    // 2.69: the weak points broken during this step stay for the gigaboss's rules (they run after
    // this); those of the step before go.
    s->broken.erase(s->broken.begin(), s->broken.begin() + static_cast<std::ptrdiff_t>(std::min(s->brokenSeen, s->broken.size())));
    s->brokenSeen = s->broken.size();
    if (!w.coop()) return;
    const int n = std::min(w.teamSize(), 8);
    // Allies revived: whoever stood by the soul (the world's own rule) gets the rescue.
    for (int i = 0; i < n; ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        if (m.revived <= s->revivedSeen[i]) {
            s->revivedSeen[i] = m.revived;
            continue;
        }
        s->revivedSeen[i] = m.revived;
        for (int j = 0; j < n; ++j) {
            if (j == i || w.member(j).presence != P::Playing || !w.playerAt(j).alive()) continue;
            const Vec2 f = w.playerAt(j).feet();
            if (std::abs(f.x - m.soul.x) <= GameWorld::kReviveReachX + 8.0f && std::abs(f.y - m.soul.y) <= GameWorld::kReviveReachY + 8.0f)
                if (Contribution* c = current(j)) {
                    ++c->rescues;
                    if (ZoneContribution* z = zoneLine(s->zone, j)) ++z->support;  // 2.75
                }
        }
    }
    // The gigaboss's blows: the player it aimed at, dodged or not.
    const AttackDef* a = boss && boss->alive() ? boss->currentAttack() : nullptr;
    const bool windUp = boss && boss->alive() && boss->state() == EnemyState::Telegraph && !s->prevTelegraph;
    if (a != s->attack || windUp) {
        if (s->attack && s->target >= 0 && s->target < n && !s->targetHit && s->attackDamage > 0 && w.member(s->target).presence == P::Playing &&
            w.playerAt(s->target).alive())
            if (Contribution* c = current(s->target)) c->avoided += s->attackDamage;
        s->attack = a;
        s->targetHit = false;
        s->target = a ? w.targetIndex(boss->center()) : -1;
        s->attackDamage = a ? tenths(a->damage) : 0;
    }
    s->prevTelegraph = boss && boss->alive() && boss->state() == EnemyState::Telegraph;
    if (s->target >= 0 && s->target < 8 && s->hurtThisStep[s->target]) s->targetHit = true;
    for (bool& h : s->hurtThisStep) h = false;
}

void Contributions::objective(const GameWorld& w, int seat) {
    const Scope* s = scopeOf(w);
    const int zone = s ? s->zone : 0;
    if (Contribution* c = current(seat)) {
        ++c->objectives;
        if (ZoneContribution* z = zoneLine(zone, seat)) ++z->objectives;  // 2.75
        return;
    }
    for (int i = 0; i < std::min(w.teamSize(), 8); ++i)
        if (w.coop() && w.member(i).presence == GameWorld::Presence::Playing)
            if (Contribution* c = current(i)) {
                ++c->objectives;
                if (ZoneContribution* z = zoneLine(zone, i)) ++z->objectives;
            }
}

void Contributions::vent(int seat) {
    if (Contribution* c = current(seat)) ++c->vents;
    if (ZoneContribution* z = zoneLine(focus_ ? focus_->zone : 0, seat)) ++z->mechanisms;  // 2.75
}

void Contributions::rescue(int seat) {
    if (Contribution* c = current(seat)) ++c->rescues;
    if (ZoneContribution* z = zoneLine(focus_ ? focus_->zone : 0, seat)) ++z->support;  // 2.75
}

void Contributions::weakPoint(int seat) {
    if (Contribution* c = current(seat)) ++c->weakPoints;
    if (ZoneContribution* z = zoneLine(focus_ ? focus_->zone : 0, seat)) ++z->mechanisms;  // 2.75
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
    Scope* s = scopeOf(w);
    if (!s) return;
    s->tagOwner[tag] = w.currentPlayer();
    // A shot lives a few seconds: the oldest tags go (bounded memory, the same on every computer).
    while (s->tagOwner.size() > 4096) s->tagOwner.erase(s->tagOwner.begin());
}

void Contributions::enemyDamaged(const GameWorld& w, const Enemy& e, uint32_t tag, float amount) {
    Scope* s = scopeOf(w);
    if (!s) return;
    const int seat = ownerOf(w, tag);
    Contribution* c = current(seat);
    if (!c) return;
    const int64_t t = tenths(amount);
    c->damage += t;
    const bool weak = std::find(s->weak.begin(), s->weak.end(), e.uid()) != s->weak.end();
    if (s->boss != 0 && (e.uid() == s->boss || weak)) c->bossDamage += t;
    // 2.75: the zone's table: every enemy, and its boss and mini-bosses (a boss of the bestiary, an
    // enemy whose fall is an objective of the zone, its gigaboss and weak points).
    if (ZoneContribution* z = zoneLine(s->zone, seat)) {
        z->damage += t;
        const bool listed = !e.objectId().empty() && std::find(s->bosses.begin(), s->bosses.end(), e.objectId()) != s->bosses.end();
        const bool big = (s->boss != 0 && (e.uid() == s->boss || weak)) || e.def().isBoss;
        if (big || listed) z->bossDamage += t;
        // 2.78: a mini-boss (an objective « killed » of the zone that is no boss): its own column.
        if (!big && !e.objectId().empty() && std::find(s->minis.begin(), s->minis.end(), e.objectId()) != s->minis.end()) z->miniDamage += t;
    }
    bool known = false;
    for (const auto& [u, who] : s->broken) known = known || u == e.uid();
    if (weak && e.hp() <= 0.0f && !known) s->broken.emplace_back(e.uid(), seat);
    // 2.69 (phase 4B): a target of a mechanism (a stomach, a restraint, a core) broken by this seat.
    if (!weak && e.hp() <= 0.0f && !known && std::find(s->watched.begin(), s->watched.end(), e.uid()) != s->watched.end())
        s->broken.emplace_back(e.uid(), seat);
}

void Contributions::blowBlocked(const GameWorld& w, float amount) {
    const Scope* s = scopeOf(w);
    if (Contribution* c = current(w.currentPlayer())) {
        ++c->blocks;
        c->avoided += tenths(amount);
        if (ZoneContribution* z = zoneLine(s ? s->zone : 0, w.currentPlayer())) ++z->support;  // 2.75
    }
}

void Contributions::playerHurt(const GameWorld& w, const DamageInfo&, const HurtSource& src) {
    Scope* s = scopeOf(w);
    if (!s || s->boss == 0 || !src.enemy || src.enemy->uid() != s->boss) return;
    const int i = w.currentPlayer();
    if (i >= 0 && i < 8) s->hurtThisStep[i] = true;
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
    // 2.78: the zones' tables (their deaths and revivals with the rest): the same on every computer.
    for (const auto& [zone, lines] : zones_) {
        h = mix(h, 0x5A00u + static_cast<uint64_t>(zone));
        for (const ZoneContribution& z : lines) {
            h = mix(h, static_cast<uint64_t>(z.bossDamage) ^ (static_cast<uint64_t>(z.miniDamage) << 40));
            h = mix(h, static_cast<uint64_t>(z.damage));
            h = mix(h, (static_cast<uint64_t>(z.support) << 32) | static_cast<uint32_t>(z.mechanisms));
            h = mix(h, (static_cast<uint64_t>(z.objectives) << 32) | static_cast<uint32_t>(z.deaths));
            h = mix(h, (static_cast<uint64_t>(z.revives) << 32) | static_cast<uint32_t>(z.revived));
        }
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
