#include "game/raid/RaidRun.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
}  // namespace

RaidRun::RaidRun(WorldServices services, const Branch& branch, int players)
    : services_(services), branch_(branch), players_(std::clamp(players, 1, kMaxInstancePlayers)) {}

bool RaidRun::begin(uint32_t branchMask, int zone) {
    branchMask_ = branchMask;
    loadZone(zone > 0 ? zone : branch_.startZone, "start", nullptr);
    return world_ != nullptr && rules_ != nullptr;
}

void RaidRun::setTest(const Test& t) {
    test_ = t;
    test_.players = std::clamp(t.players, 1, kMaxInstancePlayers);
    test_.startPhase = std::max(1, t.startPhase);
    gigaboss_.setTest(t.on ? test_.players : 0, t.on ? test_.startPhase : 1);
}

void RaidRun::setQuiet(bool on) {
    quiet_ = on;
    if (world_) world_->setQuiet(on);
}

void RaidRun::setCameraFollow(int index) {
    follow_ = index;
    if (world_) world_->setCameraFollow(index);
}

void RaidRun::alert(std::string text, Alert::Kind kind) {
    alerts_.push_back({std::move(text), kind, tick_});
    if (alerts_.size() > 24) alerts_.erase(alerts_.begin());
}

void RaidRun::loadZone(int zid, const std::string& from, const std::vector<GameWorld::MemberCarry>* carry) {
    const LevelData* level = branch_.level(zid);
    const ZoneRules* rules = branch_.zoneRules(zid);
    if (!level || !rules) return;
    auto w = std::make_unique<GameWorld>(services_, WorldMode::Play);
    w->setTeamSize(players_);
    w->setRaidMode(true);
    w->setKeysPerSlot(true);
    if (!w->load(*level)) return;
    w->seedGameplay(levelSeed(level->id));
    if (test_.on) {
        // An administrator's test: a training run (nothing it does counts), invincible when asked.
        w->setTraining(true);
        w->setInvincible(test_.invincible);
    }
    w->setQuiet(quiet_);
    w->setCameraFollow(follow_);
    world_ = std::move(w);
    // 2.67: the players' contributions listen to this world; an event knows whose attack caused it.
    contrib_.attach(*world_);
    world_->eventObserver = [this](const std::string& src, const std::string& trig) {
        events_.push_back({src, trig, world_ ? contrib_.ownerOf(world_->attackTag()) : -1});
    };
    rules_ = rules;
    zone_ = zid;
    from_ = from;
    zoneTicks_ = 0;
    magmaOn_ = false;
    ++serial_;
    // Where each player appears: the place for where they come from, else the zone's start
    // points (player i at point i, in turn); a free spot near it.
    auto it = rules->spawns.find(from);
    if (it == rules->spawns.end() || it->second.empty()) it = rules->spawns.find("start");
    std::vector<Vec2> points = it != rules->spawns.end() && !it->second.empty() ? it->second : std::vector<Vec2>{};
    for (int i = 0; i < world_->teamSize(); ++i) {
        const GameWorld::MemberCarry c = carry && i < static_cast<int>(carry->size()) ? (*carry)[static_cast<size_t>(i)] : world_->carryOf(i);
        const Vec2 at = points.empty() ? world_->playerAt(i).feet() : points[static_cast<size_t>(i) % points.size()];
        world_->restoreMember(i, c, at);
    }
    // The passages the players stand in now never take them (they must walk into one).
    insidePassage_.assign(rules->passages.size(), 0);
    for (size_t k = 0; k < rules->passages.size(); ++k)
        for (int i = 0; i < world_->teamSize(); ++i)
            if (rules->passages[k].rect.intersects(world_->playerAt(i).box)) insidePassage_[k] |= static_cast<uint8_t>(1u << i);
    switchStates_.assign(rules->switches.size(), SwitchState{});
    for (size_t k = 0; k < rules->switches.size(); ++k) switchStates_[k].on.assign(rules->switches[k].plates.size(), 0);
    applyMask(branchMask_, true);
    alert("Zone : " + (branch_.zone(zid) ? branch_.zone(zid)->name : std::string("?")), Alert::Kind::Group);
    // 2.67: the gigaboss of the zone (gone when its objective is done: it already fell).
    const int fallen = rules->gigaboss.present ? branch_.objectiveIndex(rules->gigaboss.objective) : -1;
    gigaboss_.begin(*world_, rules->gigaboss, fallen >= 0 && Branch::done(mask(), fallen), contrib_);
    for (auto& [text, kind] : gigaboss_.takeAlerts())
        alert(std::move(text), kind == Gigaboss::AlertKind::Danger ? Alert::Kind::Danger : kind == Gigaboss::AlertKind::Branch ? Alert::Kind::Branch : Alert::Kind::Group);
    reports_.push_back({Report::Kind::Zone, {}, zid, tick_, hash()});
}

void RaidRun::applyMask(uint32_t mask, bool initial) {
    const uint32_t before = this->mask();
    branchMask_ = mask;
    const uint32_t now = this->mask();
    if (!initial && before != now) {
        for (const auto& o : branch_.objectives)
            if (!Branch::done(before, o.index) && Branch::done(now, o.index))
                alert("BRANCHE · " + o.label.substr(0, o.label.find(" (")) + " : accompli par un autre groupe", Alert::Kind::Branch);
        if (branch_.generators(before) < branch_.generatorsNeeded && branch_.generators(now) >= branch_.generatorsNeeded)
            alert("BRANCHE · Les deux générateurs tournent : le Poignet-verrou s'ouvre", Alert::Kind::Branch);
        if (!branch_.shortcut(before) && branch_.shortcut(now)) alert("BRANCHE · Le raccourci Veine ⇄ Avant-bras est ouvert", Alert::Kind::Branch);
    }
    // The doors of the passages follow their locks (the same step on every computer).
    if (!world_ || !rules_) return;
    for (const Passage& p : rules_->passages) {
        if (p.door.empty()) continue;
        auto* door = dynamic_cast<DoorEntity*>(world_->entity(p.door));
        if (door && branch_.lockOpen(p.lock, now) && !door->isOpen()) door->setOpen(*world_, true, initial);
    }
}

float RaidRun::magmaIn() const {
    if (!rules_ || !rules_->magma.present) return -1.0f;
    const Magma& m = rules_->magma;
    const float t = static_cast<float>(zoneTicks_) / static_cast<float>(kSimHz);
    if (t < m.first) return m.first - t;
    const float k = std::fmod(t - m.first, m.period);
    return m.period - k;
}

void RaidRun::updateMagma() {
    if (!rules_->magma.present) return;
    const Magma& m = rules_->magma;
    // Whole steps only (no float drift): on during `duration` from each `first + k × period`.
    const uint32_t first = static_cast<uint32_t>(std::lround(m.first * kSimHz));
    const uint32_t period = static_cast<uint32_t>(std::lround(m.period * kSimHz));
    const uint32_t duration = static_cast<uint32_t>(std::lround(m.duration * kSimHz));
    const uint32_t warn = static_cast<uint32_t>(std::lround(m.warn * kSimHz));
    const bool on = zoneTicks_ >= first && (zoneTicks_ - first) % period < duration;
    const uint32_t next = zoneTicks_ < first ? first : first + ((zoneTicks_ - first) / period + 1) * period;
    if (next - zoneTicks_ == warn) alert("DANGER · Coulée de magma dans " + std::to_string(static_cast<int>(std::lround(m.warn))) + " s : montez !", Alert::Kind::Danger);
    if (on == magmaOn_) return;
    magmaOn_ = on;
    if (LevelEntity* z = world_->entity(m.zone)) z->handleAction(*world_, on ? "enable" : "disable", Json::object());
}

void RaidRun::completed(const std::string& source, const std::string& trigger, int who) {
    for (const ZoneObjective& zo : rules_->objectives) {
        if (zo.source != source || zo.trigger != trigger) continue;
        const ObjectiveInfo* info = zo.index >= 0 && zo.index < static_cast<int>(branch_.objectives.size()) ? &branch_.objectives[static_cast<size_t>(zo.index)] : nullptr;
        if (!info || Branch::done(mask(), info->index)) continue;
        // A pair (the bellows): never both by one instance.
        if (!branch_.pairDone(*info, local_).empty()) {
            world_->hint("Un autre groupe doit accomplir celui-ci : votre groupe a déjà fait l'autre.", 4.0f);
            continue;
        }
        local_ |= 1u << info->index;
        alert("GROUPE · Objectif accompli : " + info->label, Alert::Kind::Group);
        // 2.67: an action of one player (a generator hit, a relic taken) is theirs; a fight won
        // (an arena cleared, a guardian or the gigaboss down) is everyone's in play.
        contrib_.objective(*world_, trigger == "activate" || trigger == "collected" ? who : -1);
        Report rep;
        rep.kind = Report::Kind::Objective;
        rep.objective = info->id;
        rep.zone = zone_;
        rep.tick = tick_;
        reports_.push_back(std::move(rep));
        reportResult();
    }
}

int RaidRun::switchNeed(const GroupSwitch& sw) const {
    int seated = 0;
    if (world_ && world_->coop())
        for (int i = 0; i < world_->teamSize(); ++i) seated += world_->member(i).presence != GameWorld::Presence::Vacant ? 1 : 0;
    // Each plate needs its players, and every plate together never more than the seats taken.
    const int plates = std::max<int>(1, static_cast<int>(sw.plates.size()));
    return std::clamp(std::min(sw.need, std::max(1, seated) / plates), 1, sw.need);
}

void RaidRun::stepSwitches() {
    // 2.67: a door or a lift that needs the group: enough players on each plate at once, held.
    for (size_t k = 0; k < rules_->switches.size() && k < switchStates_.size(); ++k) {
        const GroupSwitch& sw = rules_->switches[k];
        SwitchState& st = switchStates_[k];
        if (st.done) continue;
        const int need = switchNeed(sw);
        int seats = 0;
        for (int i = 0; i < world_->teamSize(); ++i) seats += world_->coop() && world_->member(i).presence != GameWorld::Presence::Vacant ? 1 : 0;
        bool all = true;
        uint8_t holders = 0;
        for (size_t p = 0; p < sw.plates.size(); ++p) {
            int n = 0;
            for (int i = 0; i < std::min(world_->teamSize(), kMaxInstancePlayers); ++i) {
                if (world_->member(i).presence != GameWorld::Presence::Playing) continue;
                const Player& pl = world_->playerAt(i);
                if (!pl.alive() || pl.respawning() || !sw.plates[p].intersects(pl.box)) continue;
                ++n;
                holders |= static_cast<uint8_t>(1u << i);
            }
            st.on[p] = n;
            all = all && (n >= need || static_cast<int>(p) >= std::max(1, seats));  // alone: the first plate only
        }
        const int hold = std::max(1, static_cast<int>(std::lround(sw.hold * static_cast<float>(kSimHz))));
        st.hold = all ? std::min(hold, st.hold + 1) : std::max(0, st.hold - 2);
        if (st.hold < hold) continue;
        st.done = true;
        for (const std::string& t : sw.targets)
            if (LevelEntity* e = world_->entity(t)) e->handleAction(*world_, sw.action, Json::object());
        for (int i = 0; i < kMaxInstancePlayers; ++i)
            if (holders & (1u << i)) contrib_.vent(i);  // a mechanism activated, by each player on it
        alert("GROUPE · " + sw.label + " : c'est fait !", Alert::Kind::Group);
    }
}

void RaidRun::reportResult() {
    if (lastResult_ == tick_) return;  // one table per step (an objective and a phase together)
    lastResult_ = tick_;
    Report rep;
    rep.kind = Report::Kind::Result;
    rep.zone = zone_;
    rep.tick = tick_;
    rep.records = contrib_.records();
    reports_.push_back(std::move(rep));
}

void RaidRun::checkPassages() {
    for (size_t k = 0; k < rules_->passages.size(); ++k) {
        const Passage& p = rules_->passages[k];
        uint8_t inside = 0;
        int taker = -1;
        for (int i = 0; i < world_->teamSize(); ++i) {
            const GameWorld::TeamMember& m = world_->member(i);
            const Player& pl = world_->playerAt(i);
            if (m.presence != GameWorld::Presence::Playing || !pl.alive() || pl.respawning() || !p.rect.intersects(pl.box)) continue;
            inside |= static_cast<uint8_t>(1u << i);
            if (taker < 0 && !(insidePassage_[k] & (1u << i))) taker = i;
        }
        insidePassage_[k] = inside;
        if (taker < 0) continue;
        if (!passageOpen(p)) {
            const ZoneInfo* to = branch_.zone(p.to);
            world_->hint(std::string("Passage verrouillé vers ") + (to ? to->name : "?") +
                             (p.lock == "generators" ? " : il faut les deux générateurs." : p.lock == "shortcut" ? " : le levier de l'avant-bras l'ouvre." : "."),
                         3.0f);
            continue;
        }
        std::vector<GameWorld::MemberCarry> carry;
        for (int i = 0; i < world_->teamSize(); ++i) carry.push_back(world_->carryOf(i));
        loadZone(p.to, std::to_string(zone_), &carry);
        return;
    }
}

void RaidRun::step(const InputFrame* frames, int count, uint32_t branchMask) {
    if (!world_) return;
    if (branchMask != branchMask_) applyMask(branchMask, false);
    updateMagma();
    gigaboss_.before(*world_);  // 2.67: its carapace, its weak point bare, its pressure
    // The events since the last step (a world just loaded, this step): all of them, in order.
    stepTeam(*world_, frames, count, kSimDt);
    const size_t firstReport = reports_.size();
    // 2.67: the players' contributions of the step, then the gigaboss's rules (vents, jets, weak
    // points, its fall) and what they tell the raid.
    contrib_.afterStep(*world_, tick_, gigaboss_.boss(*world_));
    gigaboss_.after(*world_, contrib_);
    for (auto& [text, kind] : gigaboss_.takeAlerts())
        alert(std::move(text), kind == Gigaboss::AlertKind::Danger ? Alert::Kind::Danger : kind == Gigaboss::AlertKind::Branch ? Alert::Kind::Branch : Alert::Kind::Group);
    ++tick_;
    ++zoneTicks_;
    for (const Event& e : events_) completed(e.source, e.trigger, e.who);
    events_.clear();
    stepSwitches();  // 2.67
    if (gigaboss_.phaseReached() > bossReported_) {
        for (int p = bossReported_ + 1; p <= gigaboss_.phaseReached(); ++p) {
            Report rep;
            rep.kind = Report::Kind::Boss;
            rep.zone = zone_;
            rep.tick = tick_;
            rep.phase = p;
            reports_.push_back(std::move(rep));
        }
        bossReported_ = gigaboss_.phaseReached();
        reportResult();
    }
    const uint64_t h = hash();
    for (size_t i = firstReport; i < reports_.size(); ++i) reports_[i].hash = h;
    if (world_->state() == WorldState::Failed) {
        // Everybody down: the zone again from where the instance entered it (objectives kept),
        // every player on their feet with every heart.
        std::vector<GameWorld::MemberCarry> carry;
        for (int i = 0; i < world_->teamSize(); ++i) {
            GameWorld::MemberCarry c = world_->carryOf(i);
            if (c.presence == GameWorld::Presence::Down) c.presence = GameWorld::Presence::Playing;
            c.health = world_->playerAt(i).maxHealth();
            carry.push_back(c);
        }
        // 2.67: from the last checkpoint reached in the zone, when there is one.
        const std::string cpId = world_->checkpointId();
        const Vec2 cpAt = world_->respawnPoint();
        alert(cpId.empty() ? "GROUPE · Tout le groupe est à terre : la zone reprend" : "GROUPE · Tout le groupe est à terre : reprise au point de contrôle",
              Alert::Kind::Danger);
        const std::string from = from_;
        loadZone(zone_, from, &carry);
        if (!cpId.empty() && world_) {
            for (int i = 0; i < world_->teamSize(); ++i)
                if (world_->member(i).presence != GameWorld::Presence::Vacant) world_->playerAt(i).teleportTo(world_->freeSpot(cpAt, i));
            world_->setCheckpoint(cpId, cpAt);
            for (size_t k = 0; k < rules_->passages.size(); ++k) {
                insidePassage_[k] = 0;
                for (int i = 0; i < world_->teamSize(); ++i)
                    if (rules_->passages[k].rect.intersects(world_->playerAt(i).box)) insidePassage_[k] |= static_cast<uint8_t>(1u << i);
            }
        }
        return;
    }
    checkPassages();
    if (test_.on) reports_.clear();  // 2.67: an administrator's test never reports anything
}

uint64_t RaidRun::hash() const {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, world_ ? simulationStateHash(*world_) : 0);
    h = mix(h, static_cast<uint64_t>(zone_));
    h = mix(h, zoneTicks_);
    h = mix(h, tick_);
    h = mix(h, (static_cast<uint64_t>(branchMask_) << 32) | local_);
    h = mix(h, contrib_.hash());   // 2.67
    h = mix(h, gigaboss_.hash());  // 2.67
    for (const SwitchState& st : switchStates_) h = mix(h, (static_cast<uint64_t>(st.hold) << 1) | (st.done ? 1u : 0u));
    return h;
}

std::vector<Report> RaidRun::takeReports() {
    std::vector<Report> out;
    out.swap(reports_);
    if (test_.on) out.clear();  // 2.67: an administrator's test never reports anything
    return out;
}

}  // namespace cosmic::raid
