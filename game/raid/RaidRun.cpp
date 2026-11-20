#include "game/raid/RaidRun.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/enemies/Enemy.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/ZoneGoals.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic::raid {

namespace {
uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}
using P = GameWorld::Presence;
bool held(P p) { return p != P::Vacant && p != P::Gone; }
}  // namespace

const Gigaboss RaidRun::kNoGigaboss{};

RaidRun::RaidRun(WorldServices services, const Branch& branch, int players)
    : services_(services), branch_(branch), players_(std::clamp(players, 1, kMaxInstancePlayers)) {
    // 2.74 (phase 6): the heart's branch (its gigaboss's objective: the shared victory, validated).
    if (branch_.part == kHeartPart)
        for (const ObjectiveInfo& o : branch_.objectives)
            if (o.role == "gigaboss") heartObjective_ = o.index;
}

RaidRun::~RaidRun() = default;

// ------------------------------------------------------------------------------- the zones

RaidRun::ZoneWorld* RaidRun::zw(int zone) {
    for (auto& z : zones_)
        if (z->zone == zone) return z.get();
    return nullptr;
}

const RaidRun::ZoneWorld* RaidRun::zw(int zone) const {
    for (const auto& z : zones_)
        if (z->zone == zone) return z.get();
    return nullptr;
}

std::vector<int> RaidRun::zonesLoaded() const {
    std::vector<int> out;
    for (const auto& z : zones_) out.push_back(z->zone);
    return out;
}

int RaidRun::zoneOf(int seat) const { return seat >= 0 && seat < kMaxInstancePlayers ? seatZone_[seat] : -1; }

uint32_t RaidRun::seatsIn(int zone) const {
    const ZoneWorld* z = zw(zone);
    if (!z) return 0;
    uint32_t bits = 0;
    for (int i = 0; i < z->world->teamSize() && i < kMaxInstancePlayers; ++i)
        if (seatZone_[i] == zone && z->world->member(i).presence != P::Vacant) bits |= 1u << i;
    return bits;
}

GameWorld* RaidRun::worldOf(int zone) {
    ZoneWorld* z = zw(zone);
    return z ? z->world.get() : nullptr;
}

const GameWorld* RaidRun::worldOf(int zone) const {
    const ZoneWorld* z = zw(zone);
    return z ? z->world.get() : nullptr;
}

const ZoneRules* RaidRun::rulesOf(int zone) const {
    const ZoneWorld* z = zw(zone);
    return z ? z->rules : nullptr;
}

const Gigaboss* RaidRun::gigabossOf(int zone) const {
    const ZoneWorld* z = zw(zone);
    return z ? &z->gigaboss : nullptr;
}

int RaidRun::focusedZone() const {
    if (focus_ >= 0 && zw(focus_)) return focus_;
    if (follow_ >= 0 && follow_ < kMaxInstancePlayers && zw(seatZone_[follow_])) return seatZone_[follow_];
    for (const auto& z : zones_)
        for (int i = 0; i < z->world->teamSize() && i < kMaxInstancePlayers; ++i)
            if (seatZone_[i] == z->zone && z->world->member(i).presence == P::Playing) return z->zone;
    // The zone of the lowest seat held, else the first one loaded.
    for (int i = 0; i < kMaxInstancePlayers; ++i)
        if (const ZoneWorld* z = zw(seatZone_[i]); z && i < z->world->teamSize() && held(z->world->member(i).presence)) return z->zone;
    return zones_.empty() ? 0 : zones_.front()->zone;
}

const RaidRun::ZoneWorld* RaidRun::focused() const { return zw(focusedZone()); }

GameWorld& RaidRun::world() { return *zw(focusedZone())->world; }
const GameWorld& RaidRun::world() const { return *focused()->world; }
const ZoneRules& RaidRun::rules() const { return *focused()->rules; }
int RaidRun::zone() const { return focusedZone(); }
uint32_t RaidRun::zoneTicks() const {
    const ZoneWorld* z = focused();
    return z ? z->zoneTicks : 0;
}
int RaidRun::zoneSerial() const {
    const ZoneWorld* z = focused();
    return z ? z->serial : 0;
}
const Gigaboss& RaidRun::gigaboss() const {
    const ZoneWorld* z = focused();
    return z ? z->gigaboss : kNoGigaboss;
}
const Gigaboss& RaidRun::lairGigaboss() const {
    for (const auto& z : zones_)
        if (z->gigaboss.present()) return z->gigaboss;
    return gigaboss();
}
const std::string& RaidRun::lastCheckpoint() const {
    static const std::string none;
    const ZoneWorld* z = focused();
    return z ? z->lastCheckpoint : none;
}
bool RaidRun::magmaActive() const {
    const ZoneWorld* z = focused();
    return z && z->magmaOn;
}
float RaidRun::magmaIn() const {
    const ZoneWorld* z = focused();
    return z ? magmaIn(*z) : -1.0f;
}
int RaidRun::zoneFinishedNow() const {
    if (finishedNowAll_.empty()) return 0;
    const int here = focusedZone();
    for (int z : finishedNowAll_)
        if (z == here) return z;
    return finishedNowAll_.front();
}

const AsterionHeartRoutine* RaidRun::heartRoutine() const {
    for (const auto& z : zones_)
        if (const auto* hr = dynamic_cast<const AsterionHeartRoutine*>(z->gigaboss.routine())) return hr;
    return nullptr;
}

RaidRun::CoopView RaidRun::coop(size_t index) const {
    const ZoneWorld* z = focused();
    return z ? coopOf(*z, index) : CoopView{};
}

uint64_t RaidRun::zoneHash(int zone) const {
    const ZoneWorld* z = zw(zone);
    return z ? simulationStateHash(*z->world) : 0;
}

std::vector<int> RaidRun::seatZones() const {
    std::vector<int> out(static_cast<size_t>(kMaxInstancePlayers), 0);
    for (int i = 0; i < kMaxInstancePlayers; ++i) {
        const ZoneWorld* z = zw(seatZone_[i]);
        // The armories are not the server's zones (0: nowhere for it).
        if (!z || i >= z->world->teamSize() || !held(z->world->member(i).presence) || Branch::isArmory(z->zone)) continue;
        out[static_cast<size_t>(i)] = z->zone;
    }
    return out;
}

// ------------------------------------------------------------------------------- begin

bool RaidRun::begin(uint32_t branchMask, int zone) {
    branchMask_ = branchMask;
    heartWon_ = heartObjective_ >= 0 && Branch::done(branchMask_, heartObjective_);  // 2.72: already validated
    zones_.clear();
    const int first = zone > 0 ? zone : zone == kArmory && branch_.hasArmory() ? 0 : branch_.startZone;
    for (int i = 0; i < kMaxInstancePlayers; ++i) seatZone_[i] = first;
    ZoneWorld* z = loadZone(first, "start");
    if (!z) return false;
    // Every seat starts there (the free ones are vacant from the first step, as the relay says).
    for (int i = 0; i < z->world->teamSize(); ++i) {
        GameWorld::MemberCarry c = z->world->carryOf(i);
        c.presence = P::Playing;
        placeSeat(*z, i, "start", c);
    }
    checkFinished(true);  // 2.75: the zones already finished are not news
    return z->rules != nullptr;
}

RaidRun::ZoneWorld* RaidRun::loadZone(int zid, const std::string& from) {
    const LevelData* level = branch_.level(zid);
    const ZoneRules* rules = branch_.zoneRules(zid);
    if (!level || !rules) return nullptr;
    auto w = std::make_unique<GameWorld>(services_, WorldMode::Play);
    w->setTeamSize(players_);
    w->setRaidMode(true);
    w->setRaidGroupRule(true);  // 2.78: the run judges the group's fall across its zones
    w->setKeysPerSlot(true);
    if (!w->load(*level)) return nullptr;
    w->seedGameplay(levelSeed(level->id));
    if (test_.on) {
        // An administrator's test: a training run (nothing it does counts), invincible when asked.
        w->setTraining(true);
        w->setInvincible(test_.invincible);
    }
    w->setQuiet(quiet_);
    w->setCameraFollow(follow_);
    auto z = std::make_unique<ZoneWorld>();
    z->zone = zid;
    z->world = std::move(w);
    z->rules = rules;
    z->from = from;
    z->serial = ++serials_;
    GameWorld& world = *z->world;
    // Nobody of the instance is here yet: every seat vacant (placeSeat brings the players in).
    {
        GameWorld::MemberCarry away;
        away.presence = P::Vacant;
        away.health = world.playerAt(0).maxHealth();
        const auto it = rules->spawns.find("start");
        const Vec2 at = it != rules->spawns.end() && !it->second.empty() ? it->second.front() : world.playerAt(0).feet();
        std::vector<GameWorld::MemberCarry> fresh;
        for (int i = 0; i < world.teamSize(); ++i) fresh.push_back(world.carryOf(i));
        for (int i = 0; i < world.teamSize(); ++i) {
            GameWorld::MemberCarry c = away;
            c.powers = fresh[static_cast<size_t>(i)].powers;  // a seat that joins here later gets the zone's powers
            c.energy = fresh[static_cast<size_t>(i)].energy;
            world.restoreMember(i, c, at);
        }
    }
    restoreZone(*z);  // 2.72: as the group left it (before the players are placed: its doors)
    // 2.68: the players' contributions listen to this world; an event knows whose attack caused it.
    {
        // 2.75: the zone's table counts its boss and mini-bosses (the enemies whose fall is an objective).
        std::vector<std::string> bosses, minis;
        for (const ZoneObjective& zo : rules->objectives)
            if (zo.trigger == "killed" && !(rules->gigaboss.present && zo.source == rules->gigaboss.boss)) {
                bosses.push_back(zo.source);
                minis.push_back(zo.source);
            }
        if (rules->gigaboss.present) bosses.push_back(rules->gigaboss.boss);
        contrib_.attach(world, zid, std::move(bosses), std::move(minis));
    }
    ZoneWorld* zp = z.get();
    world.eventObserver = [this, zp](const std::string& src, const std::string& trig) {
        zp->events.push_back({src, trig, contrib_.ownerOf(*zp->world, zp->world->attackTag())});
    };
    // 2.68: kept when the same zone starts again; 2.72: and when a player comes back to it.
    const auto kept = memory_.find(zid);
    z->lastCheckpoint = kept != memory_.end() ? kept->second.checkpoint : std::string();
    z->insidePassage.assign(rules->passages.size(), 0);
    z->coopDone.assign(rules->coops.size(), 0);
    if (kept != memory_.end()) {
        // 2.72: the plates done stay done, their doors open, their lifts running.
        for (size_t k = 0; k < z->coopDone.size() && k < kept->second.coops.size(); ++k) {
            if (!kept->second.coops[k]) continue;
            z->coopDone[k] = 1;
            const CoopPlate& p = rules->coops[k];
            for (const std::string& t : p.targets)
                if (LevelEntity* e = world.entity(t)) {
                    if (auto* door = dynamic_cast<DoorEntity*>(e); door && p.action == "open") door->setOpen(world, true, true);
                    else e->handleAction(world, p.action, Json::object());
                }
        }
    }
    syncDoors(*z, true);
    for (const EffectInfo& e : branch_.effects)
        if (Branch::on(mask(), e) && e.inZone(zid)) applyEffect(*z, e);
    // 2.68: the gigaboss of the zone (gone when its objective is done: it already fell).
    const int fallen = rules->gigaboss.present ? branch_.objectiveIndex(rules->gigaboss.objective) : -1;
    z->gigaboss.setTest(test_.on ? test_.players : 0, test_.on ? test_.startPhase : 1);
    contrib_.focus(world);
    z->gigaboss.begin(world, rules->gigaboss, fallen >= 0 && Branch::done(mask(), fallen), contrib_);
    z->gigaboss.setBranchMask(mask());  // 2.69 (phase 4B): the objectives its routine reads
    for (auto& [text, kind] : z->gigaboss.takeAlerts())
        alert(std::move(text), kind == Gigaboss::AlertKind::Danger ? Alert::Kind::Danger : kind == Gigaboss::AlertKind::Branch ? Alert::Kind::Branch : Alert::Kind::Group,
              zid);
    // Stepped in a fixed order: by zone id.
    auto at = std::lower_bound(zones_.begin(), zones_.end(), zid, [](const std::unique_ptr<ZoneWorld>& a, int id) { return a->zone < id; });
    return zones_.insert(at, std::move(z))->get();
}

void RaidRun::placeSeat(ZoneWorld& z, int seat, const std::string& from, const GameWorld::MemberCarry& carry) {
    GameWorld& w = *z.world;
    if (seat < 0 || seat >= w.teamSize()) return;
    // Where the player appears: the place for where they come from, else the zone's start points
    // (seat i at point i, in turn); a free spot near it.
    auto it = z.rules->spawns.find(from);
    if (it == z.rules->spawns.end() || it->second.empty()) it = z.rules->spawns.find("start");
    const Vec2 at = it != z.rules->spawns.end() && !it->second.empty() ? it->second[static_cast<size_t>(seat) % it->second.size()] : w.playerAt(seat).feet();
    w.restoreMember(seat, carry, at);
    if (seat < kMaxInstancePlayers) seatZone_[seat] = z.zone;
    // The passages the player stands in now never take them (they must walk into one).
    const uint8_t bit = static_cast<uint8_t>(1u << seat);
    for (size_t k = 0; k < z.rules->passages.size(); ++k) {
        if (z.rules->passages[k].rect.intersects(w.playerAt(seat).box)) z.insidePassage[k] |= bit;
        else z.insidePassage[k] &= static_cast<uint8_t>(~bit);
    }
    if (seatsInPlay(z) & bit) z.inPlayBefore |= bit;  // 2.77: one who arrives is in play already
}

void RaidRun::leaveWorld(ZoneWorld& z, int seat) {
    GameWorld& w = *z.world;
    if (seat < 0 || seat >= w.teamSize()) return;
    GameWorld::MemberCarry away = w.carryOf(seat);
    away.presence = P::Vacant;
    away.absent = false;
    away.standbyTicks = 0;
    w.restoreMember(seat, away, w.playerAt(seat).feet());
    const uint8_t bit = static_cast<uint8_t>(1u << seat);
    for (uint8_t& b : z.insidePassage) b &= static_cast<uint8_t>(~bit);
    z.inPlayBefore &= static_cast<uint8_t>(~bit);
}

void RaidRun::unloadEmpty() {
    for (size_t k = 0; k < zones_.size() && zones_.size() > 1;) {
        ZoneWorld& z = *zones_[k];
        bool someone = false;
        for (int i = 0; i < z.world->teamSize() && i < kMaxInstancePlayers; ++i)
            someone = someone || (seatZone_[i] == z.zone && held(z.world->member(i).presence));
        if (someone) {
            ++k;
            continue;
        }
        remember(z);  // 2.72: what was done there stays done
        contrib_.detach(*z.world);
        zones_.erase(zones_.begin() + static_cast<std::ptrdiff_t>(k));
    }
}

void RaidRun::placeJoiners(const InputFrame* frames, int count) {
    // A seat taken (its frames no longer vacant) whose zone has no world (everybody left it): it joins
    // in the zone of the lowest seat in play (else the first one loaded), near an ally there.
    for (int i = 0; i < count && i < kMaxInstancePlayers; ++i) {
        if (frames[i].flags & InputFrame::Vacant) continue;
        if (zw(seatZone_[i])) continue;
        int to = -1;
        for (int j = 0; j < kMaxInstancePlayers && to < 0; ++j)
            if (const ZoneWorld* z = zw(seatZone_[j]); z && j < z->world->teamSize() && z->world->member(j).presence == P::Playing) to = z->zone;
        if (to < 0 && !zones_.empty()) to = zones_.front()->zone;
        if (to >= 0) seatZone_[i] = to;  // vacant in that world: it joins there (GameWorld: near the nearest ally)
    }
}

// ------------------------------------------------------------------------------- settings

void RaidRun::setTest(const Test& t) {
    test_ = t;
    test_.players = std::clamp(t.players, 1, kMaxInstancePlayers);
    test_.startPhase = std::max(1, t.startPhase);
    for (auto& z : zones_) z->gigaboss.setTest(t.on ? test_.players : 0, t.on ? test_.startPhase : 1);
}

void RaidRun::setQuiet(bool on) {
    quiet_ = on;
    for (auto& z : zones_) z->world->setQuiet(on);
}

void RaidRun::setCameraFollow(int index) {
    follow_ = index;
    for (auto& z : zones_) z->world->setCameraFollow(index);
}

void RaidRun::alert(std::string text, Alert::Kind kind, int zone, int seat) {
    alerts_.push_back({std::move(text), kind, tick_, zone, seat});
    if (alerts_.size() > 32) alerts_.erase(alerts_.begin());
}

// ------------------------------------------------------------------------------- the state of the branch

void RaidRun::applyMask(uint32_t mask, bool initial) {
    const uint32_t before = this->mask();
    branchMask_ = mask;
    const uint32_t now = this->mask();
    if (!initial && before != now) {
        for (const auto& o : branch_.objectives)
            if (!Branch::done(before, o.index) && Branch::done(now, o.index))
                alert("BRANCHE · " + o.label.substr(0, o.label.find(" (")) + " : accompli par un autre groupe", Alert::Kind::Branch);
        if (branch_.generators(before) < branch_.generatorsNeeded && branch_.generators(now) >= branch_.generatorsNeeded)
            alert("BRANCHE · " + branch_.lockText("generators").message, Alert::Kind::Branch);
        if (!branch_.shortcut(before) && branch_.shortcut(now)) alert("BRANCHE · " + branch_.lockText("shortcut").message, Alert::Kind::Branch);
        // 2.68: what another branch did, at this step on every computer of the instance.
        for (const EffectInfo& e : branch_.effects) {
            if (Branch::on(before, e) || !Branch::on(now, e)) continue;
            alert("TITAN · " + e.label, Alert::Kind::Branch);
            for (auto& z : zones_)
                if (e.inZone(z->zone)) applyEffect(*z, e);
        }
    }
    for (auto& z : zones_) syncDoors(*z, initial);
}

void RaidRun::syncDoors(ZoneWorld& z, bool initial) {
    // The doors of the passages follow their locks (the same step on every computer).
    for (const Passage& p : z.rules->passages) {
        if (p.door.empty()) continue;
        auto* door = dynamic_cast<DoorEntity*>(z.world->entity(p.door));
        if (!door || door->isOpen() || !branch_.lockOpen(p.lock, mask())) continue;
        door->setOpen(*z.world, true, initial);
        // 2.78: the lair's door opens with the last ambush of the zone beaten: told in the zone.
        if (!initial && p.lock.rfind("ambushes:", 0) == 0) {
            const std::string text = branch_.lockText(p.lock).message;
            z.world->hint(text, 4.0f);
            alert("GROUPE · " + text, Alert::Kind::Group, z.zone);
        }
    }
}

void RaidRun::applyEffect(ZoneWorld& z, const EffectInfo& e) {
    GameWorld& w = *z.world;
    if (e.kind == "disable") {
        for (const std::string& id : e.objects)
            if (LevelEntity* x = w.entity(id)) x->handleAction(w, "disable", Json::object());
    } else if (e.kind == "remove") {
        // The enemies of these spawn objects (just loaded ones too: still waiting to join the world).
        for (const std::string& id : e.objects)
            if (auto* spawn = dynamic_cast<EnemySpawnEntity*>(w.entity(id)))
                if (Enemy* enemy = spawn->enemy(w); enemy && enemy->alive()) enemy->despawn();
    } else if (e.kind == "hazard_off" && z.magmaOn && z.rules->magma.present) {
        z.magmaOn = false;
        if (LevelEntity* x = w.entity(z.rules->magma.zone)) x->handleAction(w, "disable", Json::object());
    }
    // "open": the passages locked "link:<id>" open with their doors (syncDoors).
}

// ------------------------------------------------------------------------------- co-op plates

RaidRun::CoopView RaidRun::coopOf(const ZoneWorld& z, size_t index) const {
    CoopView v;
    if (index >= z.rules->coops.size()) return v;
    const GameWorld& w = *z.world;
    const CoopPlate& c = z.rules->coops[index];
    int playing = 0;
    for (int i = 0; i < w.teamSize(); ++i) {
        if (w.member(i).presence != P::Playing) continue;  // 2.78: the players of this zone only
        ++playing;
        const Player& pl = w.playerAt(i);
        if (pl.alive() && !pl.respawning() && c.rect.intersects(pl.box)) ++v.on;
    }
    v.need = std::clamp(c.players, 1, std::max(1, playing));
    if (!c.group.empty()) {
        // 2.68 (phase 3): plates held together (« group »): the players in play shared between
        // them; a plate beyond the players in play is not needed (alone: the first one is enough).
        int size = 0, rank = 0;
        for (size_t j = 0; j < z.rules->coops.size(); ++j) {
            if (z.rules->coops[j].group != c.group) continue;
            if (j < index) ++rank;
            ++size;
        }
        v.need = rank >= std::max(1, playing) ? 0 : std::clamp(c.players, 1, std::max(1, playing / size));
    }
    v.done = index < z.coopDone.size() && z.coopDone[index] != 0;
    return v;
}

void RaidRun::updateCoops(ZoneWorld& z, std::vector<std::pair<int, int>>& moves) {
    GameWorld& w = *z.world;
    for (size_t k = 0; k < z.rules->coops.size(); ++k) {
        if (z.coopDone[k]) continue;
        const CoopPlate& c = z.rules->coops[k];
        // 2.68 (phase 3): the plates of a group, held at the same step (coop(): how many on each).
        std::vector<size_t> group{k};
        if (!c.group.empty())
            for (size_t j = 0; j < z.rules->coops.size(); ++j)
                if (j != k && z.rules->coops[j].group == c.group) group.push_back(j);
        bool on = true;
        for (size_t g : group) {
            const CoopView v = coopOf(z, g);
            if (v.on < v.need) on = false;
        }
        if (!on) continue;
        for (size_t g : group) {
            z.coopDone[g] = 1;
            const CoopPlate& p = z.rules->coops[g];
            for (const std::string& t : p.targets)
                if (LevelEntity* e = w.entity(t)) e->handleAction(w, p.action, Json::object());
            // A mechanism activated, by each player standing on it (the raid's contributions).
            contrib_.focus(w);
            for (int i = 0; i < std::min(w.teamSize(), kMaxInstancePlayers); ++i)
                if (w.member(i).presence == P::Playing && p.rect.intersects(w.playerAt(i).box)) contrib_.vent(i);
            // 2.78: a passage of the group (the armory's « Prêts »): the players on its plate take it
            // together, at this step; nobody else.
            for (size_t q = 0; q < z.rules->passages.size(); ++q) {
                if (z.rules->passages[q].plate != p.id) continue;
                for (int i = 0; i < std::min(w.teamSize(), kMaxInstancePlayers); ++i) {
                    const Player& pl = w.playerAt(i);
                    if (w.member(i).presence == P::Playing && pl.alive() && !pl.respawning() && p.rect.intersects(pl.box))
                        moves.emplace_back(i, static_cast<int>(q));
                }
            }
        }
        alert("GROUPE · " + (c.message.empty() ? c.label : c.message), Alert::Kind::Group, z.zone);
    }
}

void RaidRun::reachedCheckpoint(ZoneWorld& z, const std::string& id) {
    if (id == z.lastCheckpoint || std::find(z.rules->checkpoints.begin(), z.rules->checkpoints.end(), id) == z.rules->checkpoints.end()) return;
    z.lastCheckpoint = id;
    alert("GROUPE · Point de contrôle atteint", Alert::Kind::Group, z.zone);
    // 2.75: nobody comes back at a checkpoint: a player down gets up only when a teammate revives them,
    // near their soul (before, the players down got up here); nobody else is moved.
}

// ------------------------------------------------------------------------------- the periodic danger

float RaidRun::magmaIn(const ZoneWorld& z) const {
    if (!z.rules->magma.present || branch_.hazardOff(z.zone, mask())) return -1.0f;
    const Magma& m = z.rules->magma;
    const float t = static_cast<float>(z.zoneTicks) / static_cast<float>(kSimHz);
    if (t < m.first) return m.first - t;
    const float k = std::fmod(t - m.first, m.period);
    return m.period - k;
}

void RaidRun::updateMagma(ZoneWorld& z) {
    if (!z.rules->magma.present || branch_.hazardOff(z.zone, mask())) return;  // 2.68: stopped from another branch
    GameWorld& w = *z.world;
    const Magma& m = z.rules->magma;
    // Whole steps only (no float drift): on during `duration` from each `first + k × period`.
    const uint32_t first = static_cast<uint32_t>(std::lround(m.first * kSimHz));
    const uint32_t period = static_cast<uint32_t>(std::lround(m.period * kSimHz));
    const uint32_t duration = static_cast<uint32_t>(std::lround(m.duration * kSimHz));
    const uint32_t warn = static_cast<uint32_t>(std::lround(m.warn * kSimHz));
    const uint32_t t = z.zoneTicks;
    const bool on = t >= first && (t - first) % period < duration;
    const uint32_t next = t < first ? first : first + ((t - first) / period + 1) * period;
    if (next - t == warn) {
        alert("DANGER · " + m.name + " dans " + std::to_string(static_cast<int>(std::lround(m.warn))) + " s : " + m.hint + " !", Alert::Kind::Danger, z.zone);
        // 2.72: the rising magma shows its telegraph (nothing for the dangers without one).
        if (LevelEntity* x = w.entity(m.zone)) x->handleAction(w, "warn", Json{{"seconds", m.warn}});
    }
    if (on == z.magmaOn) return;
    z.magmaOn = on;
    if (LevelEntity* x = w.entity(m.zone)) x->handleAction(w, on ? "enable" : "disable", Json::object());
}

// ------------------------------------------------------------------------------- objectives and reports

void RaidRun::completed(ZoneWorld& z, const std::string& source, const std::string& trigger, int who) {
    for (const ZoneObjective& zo : z.rules->objectives) {
        if (zo.source != source || zo.trigger != trigger) continue;
        const ObjectiveInfo* info = zo.index >= 0 && zo.index < static_cast<int>(branch_.objectives.size()) ? &branch_.objectives[static_cast<size_t>(zo.index)] : nullptr;
        if (!info || Branch::done(mask(), info->index)) continue;
        // A pair (the bellows): never both by one instance.
        if (!branch_.pairDone(*info, local_).empty()) {
            z.world->hint("Un autre groupe doit accomplir celui-ci : votre groupe a déjà fait l'autre.", 4.0f);
            continue;
        }
        local_ |= 1u << info->index;
        alert("GROUPE · Objectif accompli : " + info->label, Alert::Kind::Group, z.zone);
        // 2.68: an action of one player (a generator hit, a relic taken) is theirs; a fight won
        // (an arena cleared, a guardian or the gigaboss down) is everyone's in play (in that zone).
        contrib_.objective(*z.world, trigger == "activate" || trigger == "collected" ? who : -1);
        Report rep;
        rep.kind = Report::Kind::Objective;
        rep.objective = info->id;
        rep.zone = z.zone;
        rep.tick = tick_;
        reports_.push_back(std::move(rep));
        reportResult(z.zone);
        // 2.78: an ambush of the zone before a lair: how many are left (the passage says it too).
        if (info->role == "ambush") {
            const Branch::AmbushCount c = branch_.ambushes(z.zone, mask());
            if (c.left > 0) {
                const std::string text = Branch::ambushesLeftText(c);
                z.world->hint(text, 3.5f);
                alert("GROUPE · " + text, Alert::Kind::Group, z.zone);
            }
        }
    }
}

void RaidRun::reportResult(int zone) {
    if (lastResult_ == tick_) return;  // one table per step (an objective and a phase together)
    lastResult_ = tick_;
    Report rep;
    rep.kind = Report::Kind::Result;
    rep.zone = zone;
    rep.tick = tick_;
    rep.records = contrib_.records();
    reports_.push_back(std::move(rep));
}

void RaidRun::checkFinished(bool initial) {
    const uint32_t m = mask();
    for (const ZoneInfo& z : branch_.zones) {
        if (z.id <= 0 || z.id >= 32 || (finishedSeen_ >> z.id) & 1u || !zoneFinished(branch_, z.id, m)) continue;
        finishedSeen_ |= 1u << z.id;
        if (initial || z.boss) continue;  // the lair: its gigaboss's fall has its own card
        // Done by this instance when its own completions finish it (else another group's, validated).
        const bool byUs = !zoneFinished(branch_, z.id, branchMask_);
        // 2.78: a player of the instance is there (on their feet, down or away): its table, its report.
        bool here = false;
        if (const ZoneWorld* w = zw(z.id))
            for (int i = 0; i < w->world->teamSize() && i < kMaxInstancePlayers; ++i)
                here = here || (seatZone_[i] == z.id && held(w->world->member(i).presence));
        if (!here) {
            alert("BRANCHE · Zone terminée : " + z.name, Alert::Kind::Branch);
            continue;
        }
        alert("ZONE TERMINÉE · " + z.name + (byUs ? "" : " (par un autre groupe)"), Alert::Kind::Branch, z.id);
        finishedNowAll_.push_back(z.id);
        ZoneFinish f;
        f.zone = z.id;
        f.tick = tick_;
        f.byUs = byUs;
        f.rows = contrib_.zoneTable(z.id);
        Report rep;
        rep.kind = Report::Kind::ZoneTable;
        rep.zone = z.id;
        rep.tick = tick_;
        rep.zoneRecords = f.rows;
        reports_.push_back(std::move(rep));
        finishes_.push_back(std::move(f));
        if (finishes_.size() > 16) finishes_.erase(finishes_.begin());
    }
}

// ------------------------------------------------------------------------------- souls and passages

uint8_t RaidRun::seatsInPlay(const ZoneWorld& z) const {
    uint8_t bits = 0;
    const GameWorld& w = *z.world;
    for (int i = 0; i < w.teamSize() && i < 8; ++i) {
        const Player& pl = w.playerAt(i);
        if (w.member(i).presence == P::Playing && pl.alive() && !pl.respawning()) bits |= static_cast<uint8_t>(1u << i);
    }
    return bits;
}

void RaidRun::keepSoulsOffPassages(ZoneWorld& z) {
    GameWorld& w = *z.world;
    // What changes the zone (its passages) or would stand over a soul (a door): a soul never waits there.
    auto forbidden = [&](const Rect& body) {
        for (const Passage& p : z.rules->passages)
            if (p.rect.expanded(8.0f).intersects(body)) return true;
        for (const auto& e : w.entities())
            if (!e->removed && dynamic_cast<const DoorEntity*>(e.get()) && e->box.intersects(body)) return true;
        return false;
    };
    // The dangers as for a soul's safe place (a rising liquid: up to its highest level).
    auto danger = [&](const Rect& body) {
        for (const auto& e : w.entities()) {
            if (e->removed || !e->active) continue;
            const bool zone = dynamic_cast<const DamageZoneEntity*>(e.get()) != nullptr;
            const auto* liquid = dynamic_cast<const LiquidEntity*>(e.get());
            if (!zone && !dynamic_cast<const SpikesEntity*>(e.get()) && !(liquid && !liquid->swimmable())) continue;
            const Rect r = zone ? e->box : e->overlapBox();
            if (r.h > 0.5f && r.intersects(body)) return true;
        }
        return false;
    };
    for (int i = 0; i < w.teamSize(); ++i) {
        const GameWorld::TeamMember& m = w.member(i);
        if (m.presence != P::Down) continue;
        if (!forbidden({m.soul.x - 5.0f, m.soul.y - 22.0f, 10.0f, 21.5f})) continue;
        const Rect size = w.playerAt(i).box;
        // The player's last safe place when it is clear of them all (2.77: as for a death over the void).
        const Rect safe{m.safeFeet.x - size.w * 0.5f, m.safeFeet.y - size.h, size.w, size.h - 0.5f};
        if (m.hasSafeFeet && !forbidden(safe) && !w.physics().overlapsSolid(safe) && !danger(safe)) {
            w.restoreMember(i, w.carryOf(i), m.safeFeet);  // still down: its soul waits there
            continue;
        }
        // Else the nearest safe floor of the zone: firm ground with room above, out of every danger, passage and door.
        Vec2 best = m.soul;
        float bestD = -1.0f;
        for (const Collider& c : w.physics().colliders()) {
            if (!c.enabled || c.dynamic ||
                (c.tags & (ColliderTag::Moving | ColliderTag::Crumble | ColliderTag::Teammate | ColliderTag::EnemyOnly | ColliderTag::Door |
                           ColliderTag::Gate | ColliderTag::Breakable)))
                continue;
            if (c.box.w < 12.0f || c.box.y < w.level().bounds.y + 24.0f) continue;
            for (float x = c.box.x + 6.0f; x <= c.box.right() - 6.0f; x += 16.0f) {
                const Vec2 at{x, c.box.y};
                const float d = (at - m.soul).lengthSq();
                if (bestD >= 0.0f && d >= bestD) continue;
                const Rect body{at.x - size.w * 0.5f, at.y - size.h, size.w, size.h - 0.5f};
                if (forbidden(body) || w.physics().overlapsSolid(body) || danger(body)) continue;
                best = at;
                bestD = d;
            }
        }
        if (bestD >= 0.0f) w.restoreMember(i, w.carryOf(i), best);  // still down: its soul waits there
    }
}

void RaidRun::checkPassages(ZoneWorld& z, std::vector<std::pair<int, int>>& moves) {
    GameWorld& w = *z.world;
    // 2.77: a player takes a passage by walking into it, never by coming into play inside it (revived,
    // back from an absence, reappearing): they must step out of it first. 2.78: each player who walks into
    // an open passage changes zone alone; the others stay.
    const uint8_t inPlayBefore = z.inPlayBefore;
    z.inPlayBefore = seatsInPlay(z);
    uint8_t taken = 0;
    for (size_t k = 0; k < z.rules->passages.size(); ++k) {
        const Passage& p = z.rules->passages[k];
        uint8_t inside = 0;
        bool locked = false;
        for (int i = 0; i < w.teamSize() && i < kMaxInstancePlayers; ++i) {
            const GameWorld::TeamMember& m = w.member(i);
            const Player& pl = w.playerAt(i);
            if (m.presence != P::Playing || !pl.alive() || pl.respawning() || !p.rect.intersects(pl.box)) continue;
            const uint8_t bit = static_cast<uint8_t>(1u << i);
            inside |= bit;
            if ((z.insidePassage[k] & bit) || !(inPlayBefore & bit) || (taken & bit)) continue;
            if (!passageOpen(p)) {
                locked = true;
                continue;
            }
            // 2.78: a passage of the group only opens with its plate (the players on it take it together).
            if (!p.plate.empty()) {
                bool done = false;
                for (size_t c = 0; c < z.rules->coops.size(); ++c) done = done || (z.rules->coops[c].id == p.plate && z.coopDone[c]);
                if (!done) {
                    locked = true;
                    continue;
                }
            }
            taken |= bit;
            moves.emplace_back(i, static_cast<int>(k));
        }
        z.insidePassage[k] = inside;
        if (!locked) continue;
        const ZoneInfo* to = branch_.zone(p.to);
        std::string hint = branch_.lockText(p.lock).hint;
        // 2.78: the lair's passage says how many ambushes are left (« Embuscades restantes : 2 sur 4 »).
        if (p.lock.rfind("ambushes:", 0) == 0) hint = Branch::ambushesLeftText(branch_.ambushes(std::atoi(p.lock.c_str() + 9), mask()));
        else if (!p.plate.empty()) hint = "tous les joueurs présents sur la plaque « Prêts »";
        const std::string name = to ? to->name : "?";
        w.hint(hint.empty() ? "Passage verrouillé vers " + name + "." : "Passage verrouillé vers " + name + " : " + hint + ".", 3.0f);
    }
}

// ------------------------------------------------------------------------------- the step

void RaidRun::step(const InputFrame* frames, int count, uint32_t branchMask, const HeartShared& heart) {
    if (zones_.empty()) return;
    wipedNow_ = false;
    finishedNowAll_.clear();
    if (wiped_) {
        // 2.75: a group wiped stays out of its zones: the steps go on (the relay still gives them), the
        // worlds stay as they fell, nothing more is done nor reported.
        ++tick_;
        return;
    }
    const size_t firstReport = reports_.size();
    placeJoiners(frames, count);
    if (branchMask != branchMask_) applyMask(branchMask, false);
    heartWonNow_ = false;
    if (heartObjective_ >= 0 && !heartWon_ && Branch::done(branchMask_, heartObjective_)) {
        // 2.74 (phase 6): the server validated the heart's fall (the shared health gone): every computer
        // of the instance learns it at this step.
        heartWon_ = heartWonNow_ = true;
        alert("MONDE · Asterion est tombé : le Cœur a cédé sous les coups de tous les groupes !", Alert::Kind::Branch);
    }
    heart_ = heart;
    const uint32_t maskBefore = mask();
    // 2.78: every zone where a player of the instance is, in the order of their ids: its world steps with
    // the frames of its players (the others are vacant in it).
    scratch_.assign(frames, frames + count);
    const int shown = focusedZone();
    for (auto& zp : zones_) {
        ZoneWorld& z = *zp;
        GameWorld& w = *z.world;
        w.setQuiet(quiet_ || z.zone != shown);  // display only: the zone shown is heard, the others are silent
        contrib_.focus(w);
        z.gigaboss.setShared(heart_);  // 2.72: the shared state of the Cœur d'Asterion
        updateMagma(z);
        z.gigaboss.setBranchMask(mask());  // 2.69 (phase 4B)
        z.gigaboss.before(w);  // 2.68: its carapace, its weak point bare, its pressure
        for (int i = 0; i < count; ++i) {
            if (i < kMaxInstancePlayers && seatZone_[i] == z.zone) {
                scratch_[static_cast<size_t>(i)] = frames[i];
            } else {
                scratch_[static_cast<size_t>(i)] = InputFrame{};
                scratch_[static_cast<size_t>(i)].flags = InputFrame::Vacant;
            }
        }
        stepTeam(w, scratch_.data(), count, kSimDt);
        keepSoulsOffPassages(z);  // 2.77: a revival never takes a player to another zone
        // 2.68: the players' contributions of the step, then the gigaboss's rules (vents, jets, weak
        // points, its fall) and what they tell the raid.
        contrib_.afterStep(w, tick_, z.gigaboss.boss(w));
        z.gigaboss.after(w, contrib_);
        for (auto& [text, kind] : z.gigaboss.takeAlerts())
            alert(std::move(text), kind == Gigaboss::AlertKind::Danger ? Alert::Kind::Danger : kind == Gigaboss::AlertKind::Branch ? Alert::Kind::Branch : Alert::Kind::Group,
                  z.zone);
    }
    // 2.78: the seats of the instance, each in its own zone: new holders, steps in play, deaths, revivals.
    {
        Contributions::SeatView views[kMaxInstancePlayers];
        for (int i = 0; i < kMaxInstancePlayers; ++i) {
            const ZoneWorld* z = zw(seatZone_[i]);
            if (!z || i >= z->world->teamSize()) continue;
            const GameWorld::TeamMember& m = z->world->member(i);
            views[i].presence = static_cast<uint8_t>(m.presence);
            views[i].zone = z->zone;
            views[i].reviver = m.reviver;
        }
        contrib_.seats(tick_, views, kMaxInstancePlayers);
    }
    ++tick_;
    for (auto& z : zones_) ++z->zoneTicks;
    for (auto& zp : zones_) {
        ZoneWorld& z = *zp;
        for (const Event& e : z.events) {
            if (e.trigger == "checkpoint") reachedCheckpoint(z, e.source);
            completed(z, e.source, e.trigger, e.who);
        }
        z.events.clear();
    }
    std::vector<std::pair<int, int>> groupMoves;  // (seat, passage) per zone, in order
    std::vector<std::pair<int, std::pair<int, int>>> moves;  // zone → (seat, passage)
    for (auto& zp : zones_) {
        groupMoves.clear();
        updateCoops(*zp, groupMoves);
        for (const auto& mv : groupMoves) moves.push_back({zp->zone, mv});
    }
    if (mask() != maskBefore)
        for (auto& z : zones_) syncDoors(*z, false);  // 2.78: this instance's own completions open doors too (the lair's)
    checkFinished(false);  // 2.75: a zone finished (by this group, or another's validated objective)
    for (auto& zp : zones_) {
        ZoneWorld& z = *zp;
        if (z.gigaboss.reportedPhase() <= z.bossReported) continue;  // 2.72: a gigaboss of four phases reports 1 to 3, then 4 at its fall
        for (int p = z.bossReported + 1; p <= z.gigaboss.reportedPhase(); ++p) {
            Report rep;
            rep.kind = Report::Kind::Boss;
            rep.zone = z.zone;
            rep.tick = tick_;
            rep.phase = p;
            reports_.push_back(std::move(rep));
        }
        z.bossReported = z.gigaboss.reportedPhase();
        reportResult(z.zone);
    }
    for (auto& zp : zones_) {
        ZoneWorld& z = *zp;
        const auto* hr = dynamic_cast<const AsterionHeartRoutine*>(z.gigaboss.routine());
        if (!hr || !z.gigaboss.engaged()) continue;
        // 2.74 (phase 6): this instance's part of the heart's shared health, while it changes and at its fall.
        const bool fell = z.gigaboss.fellNow();
        const uint32_t every = static_cast<uint32_t>(std::lround(hr->reportEvery() * static_cast<float>(kSimHz)));
        if (hr->own() != heartReported_ && hr->own() > 0 && (fell || heartReported_ < 0 || tick_ - heartReportTick_ >= every)) {
            Report rep;
            rep.kind = Report::Kind::Heart;
            rep.zone = z.zone;
            rep.tick = tick_;
            rep.damage = hr->own();
            reports_.push_back(std::move(rep));
            heartReported_ = hr->own();
            heartReportTick_ = tick_;
        }
        if (fell && test_.on && !heartWon_) heartWon_ = heartWonNow_ = true;  // a test: its fall is its victory (never reported)
    }
    {
        const uint64_t h = hash();
        for (size_t i = firstReport; i < reports_.size(); ++i) reports_[i].hash = h;
    }
    // 2.75, 2.78: the group's fall, every zone at once. Nobody in play anywhere (on their feet or away)
    // and somebody down: the group is wiped, out of its zones for good, its players spectate from now on
    // (no restart, no checkpoint). A zone whose players are all down while others are in play elsewhere
    // goes on: their souls wait there for an ally.
    bool inPlay = false, down = false, gone = false;
    int most = -1, mostN = 0;
    std::vector<int> occupied;
    for (auto& zp : zones_) {
        const GameWorld& w = *zp->world;
        int n = 0;
        for (int i = 0; i < w.teamSize() && i < kMaxInstancePlayers; ++i) {
            if (seatZone_[i] != zp->zone) continue;
            const P p = w.member(i).presence;
            inPlay = inPlay || p == P::Playing || p == P::Standby;
            down = down || p == P::Down;
            gone = gone || p == P::Gone;
            n += p == P::Playing || p == P::Standby || p == P::Down ? 1 : 0;
            if (p == P::Playing) zp->restarted = false;
        }
        if (n > 0) occupied.push_back(zp->zone);
        if (n > mostN) {
            most = zp->zone;
            mostN = n;
        }
    }
    if (!inPlay && down) {
        wiped_ = wipedNow_ = true;
        wipedTick_ = tick_;
        wipedZone_ = most;
        wipedZones_ = occupied;
        for (auto& z : zones_) z->world->failLevel("Toute l'équipe est à terre");
        alert("GROUPE ÉLIMINÉ · Tout le groupe est à terre : mode spectateur. La zone reste à reprendre par un autre groupe.", Alert::Kind::Danger);
        Report rep;
        rep.kind = Report::Kind::Wipe;
        rep.zone = Branch::isArmory(wipedZone_) ? 0 : wipedZone_;
        rep.tick = tick_;
        rep.hash = hash();
        rep.seats = seatZones();
        if (rep.zone > 0) reports_.push_back(std::move(rep));
        if (test_.on) reports_.clear();  // an administrator's test never reports anything
        return;
    }
    if (!inPlay && !down && gone) {
        // Every player gone for good at once (nobody down): their zones again from where their players
        // entered them (objectives kept), at the last checkpoint reached.
        for (auto& zp : zones_) {
            ZoneWorld& z = *zp;
            const GameWorld& w = *z.world;
            uint32_t here = 0;
            for (int i = 0; i < w.teamSize() && i < kMaxInstancePlayers; ++i)
                if (seatZone_[i] == z.zone && w.member(i).presence == P::Gone) here |= 1u << i;
            if (!here || z.restarted) continue;  // started again already: it waits for its players
            std::vector<GameWorld::MemberCarry> carry;
            for (int i = 0; i < w.teamSize(); ++i) carry.push_back(w.carryOf(i));
            alert("GROUPE · Tout le groupe est parti : la zone reprend", Alert::Kind::Danger, z.zone);
            const int zid = z.zone;
            const std::string from = z.lastCheckpoint.empty() ? z.from : "cp:" + z.lastCheckpoint;
            remember(z);  // 2.72: what the group did in it before stays done
            contrib_.detach(*z.world);
            zones_.erase(std::find_if(zones_.begin(), zones_.end(), [&](const std::unique_ptr<ZoneWorld>& x) { return x.get() == &z; }));
            if (ZoneWorld* fresh = loadZone(zid, from)) {
                for (int i = 0; i < fresh->world->teamSize(); ++i) {
                    if (!((here >> i) & 1u)) continue;
                    GameWorld::MemberCarry c = carry[static_cast<size_t>(i)];
                    c.health = fresh->world->playerAt(i).maxHealth();
                    placeSeat(*fresh, i, from, c);
                }
                fresh->restarted = true;
            }
            break;  // zones_ changed: the others (if any) at the next step
        }
    } else {
        // 2.78: the players who walked into a passage, then those a plate of the group takes: each one
        // changes zone alone (the lowest zone, then the lowest seat first).
        for (auto& zp : zones_) {
            groupMoves.clear();
            checkPassages(*zp, groupMoves);
            for (const auto& mv : groupMoves) moves.push_back({zp->zone, mv});
        }
        std::stable_sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : a.second.first < b.second.first;
        });
        uint32_t movedSeats = 0;
        for (const auto& [fromZone, mv] : moves) {
            const int seat = mv.first;
            if ((movedSeats >> seat) & 1u) continue;
            ZoneWorld* from = zw(fromZone);
            if (!from || seatZone_[seat] != fromZone) continue;
            const Passage& p = from->rules->passages[static_cast<size_t>(mv.second)];
            const int to = p.arrival();
            if (!branch_.level(to)) continue;
            movedSeats |= 1u << seat;
            const GameWorld::MemberCarry carry = from->world->carryOf(seat);
            leaveWorld(*from, seat);
            ZoneWorld* target = zw(to);
            if (!target) target = loadZone(to, std::to_string(fromZone));
            if (!target) {
                placeSeat(*from, seat, std::to_string(fromZone), carry);  // cannot happen with the generated zones
                continue;
            }
            placeSeat(*target, seat, std::to_string(fromZone), carry);
            const ZoneInfo* info = branch_.zone(to);
            alert("Entre dans la zone : " + (info ? info->name : std::to_string(to)), Alert::Kind::Group, to, seat);
        }
        unloadEmpty();
    }
    // 2.78: where each player is, when it changed (the server places them on its maps).
    const std::vector<int> now = seatZones();
    if (now != reportedSeats_) {
        bool any = false;
        for (int z : now) any = any || z > 0;
        int entered = 0;
        for (size_t i = 0; i < now.size(); ++i)
            if (now[i] > 0 && (i >= reportedSeats_.size() || reportedSeats_[i] != now[i])) {
                entered = now[i];
                break;
            }
        if (any) {
            Report rep;
            rep.kind = Report::Kind::Zones;
            rep.zone = entered > 0 ? entered : *std::max_element(now.begin(), now.end());
            rep.tick = tick_;
            rep.hash = hash();
            rep.seats = now;
            reports_.push_back(std::move(rep));
        }
        reportedSeats_ = now;
    }
    if (test_.on) reports_.clear();  // 2.68: an administrator's test never reports anything
}

// ------------------------------------------------------------------------------- the digest and memory

uint64_t RaidRun::hash() const {
    uint64_t h = 1469598103934665603ULL;
    // 2.78: every world, in the order they are stepped, with what the raid's rules keep for it.
    for (const auto& zp : zones_) {
        const ZoneWorld& z = *zp;
        h = mix(h, 0x5A4F4E4500000000ULL | static_cast<uint32_t>(z.zone));
        h = mix(h, simulationStateHash(*z.world));
        h = mix(h, z.zoneTicks);
        h = mix(h, z.gigaboss.hash());
        for (unsigned char c : z.lastCheckpoint) h = mix(h, c);
        for (size_t k = 0; k < z.coopDone.size(); ++k)
            if (z.coopDone[k]) h = mix(h, 0x100u + k);
    }
    for (int i = 0; i < kMaxInstancePlayers; ++i) h = mix(h, static_cast<uint64_t>(seatZone_[i] + 1));
    h = mix(h, tick_);
    h = mix(h, (static_cast<uint64_t>(branchMask_) << 32) | local_);
    h = mix(h, contrib_.hash());  // 2.68 (phase 3)
    if (heart_.pack() != 0) h = mix(h, 0x4845415254000000ULL | heart_.pack());  // 2.72: the heart's shared state (zero elsewhere)
    if (wiped_) h = mix(h, 0x5749504500000000ULL | wipedTick_);                  // 2.75: the group wiped
    // 2.72: what the zones left keep.
    for (const auto& [zid, m] : memory_) {
        h = mix(h, 0x200u + static_cast<uint64_t>(zid));
        for (const auto& [id, st] : m.objects) {
            for (unsigned char c : id) h = mix(h, c);
            for (unsigned char c : st.dump()) h = mix(h, c);
        }
        for (uint8_t c : m.coops) h = mix(h, c);
        for (unsigned char c : m.checkpoint) h = mix(h, c);
    }
    return h;
}

void RaidRun::remember(ZoneWorld& z) {
    if (z.zone <= 0) return;  // the first armory keeps nothing
    GameWorld& w = *z.world;
    ZoneMemory& m = memory_[z.zone];
    for (const auto& e : w.entities()) {
        if (e->id().empty()) continue;
        Json st = e->keepState(w);
        if (!st.is_null()) m.objects[e->id()] = std::move(st);
    }
    // The pickups taken are no longer in the world: what the level had and the world has not.
    for (const LevelObject& o : w.level().objects)
        if ((o.type == "relic" || o.type == "health_orb" || o.type == "energy_orb" || o.type == "shard") && !w.entity(o.id))
            m.objects[o.id] = Json{{"taken", true}};
    m.coops.resize(std::max(m.coops.size(), z.coopDone.size()), 0);
    for (size_t k = 0; k < z.coopDone.size(); ++k) m.coops[k] = static_cast<uint8_t>(m.coops[k] | z.coopDone[k]);
    m.checkpoint = z.lastCheckpoint;
}

void RaidRun::restoreZone(ZoneWorld& z) {
    GameWorld& w = *z.world;
    // The objectives of the branch's state done in this zone (by this group or another): their
    // objects as their completion left them (the gigaboss's own is its fight's: Gigaboss).
    for (const ZoneObjective& zo : z.rules->objectives) {
        if (!Branch::done(mask(), zo.index) || (z.rules->gigaboss.present && zo.source == z.rules->gigaboss.boss)) continue;
        LevelEntity* e = w.entity(zo.source);
        if (!e) continue;
        if (zo.trigger == "cleared") e->restoreKept(w, Json{{"cleared", true}});
        else if (zo.trigger == "killed") e->restoreKept(w, Json{{"gone", true}});
        else if (zo.trigger == "activate") e->restoreKept(w, Json{{"on", true}});
        else if (zo.trigger == "collected") e->restoreKept(w, Json{{"taken", true}});
    }
    const auto kept = memory_.find(z.zone);
    if (kept == memory_.end()) return;
    for (const auto& [id, st] : kept->second.objects)
        if (LevelEntity* e = w.entity(id)) e->restoreKept(w, st);
}

std::vector<Report> RaidRun::takeReports() {
    std::vector<Report> out;
    out.swap(reports_);
    if (test_.on) out.clear();  // 2.68: an administrator's test never reports anything
    return out;
}

}  // namespace cosmic::raid
