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

bool RaidRun::begin(uint32_t branchMask) {
    branchMask_ = branchMask;
    loadZone(branch_.startZone, "start", nullptr);
    return world_ != nullptr && rules_ != nullptr;
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
    w->setQuiet(quiet_);
    w->setCameraFollow(follow_);
    w->eventObserver = [this](const std::string& src, const std::string& trig) { events_.emplace_back(src, trig); };
    world_ = std::move(w);
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
    applyMask(branchMask_, true);
    alert("Zone : " + (branch_.zone(zid) ? branch_.zone(zid)->name : std::string("?")), Alert::Kind::Group);
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

void RaidRun::completed(const std::string& source, const std::string& trigger) {
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
        reports_.push_back({Report::Kind::Objective, info->id, zone_, tick_, 0});
    }
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
    // The events since the last step (a world just loaded, this step): all of them, in order.
    stepTeam(*world_, frames, count, kSimDt);
    ++tick_;
    ++zoneTicks_;
    const size_t firstReport = reports_.size();
    for (const auto& [src, trig] : events_) completed(src, trig);
    events_.clear();
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
        alert("GROUPE · Tout le groupe est à terre : la zone reprend", Alert::Kind::Danger);
        const std::string from = from_;
        loadZone(zone_, from, &carry);
        return;
    }
    checkPassages();
}

uint64_t RaidRun::hash() const {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, world_ ? simulationStateHash(*world_) : 0);
    h = mix(h, static_cast<uint64_t>(zone_));
    h = mix(h, zoneTicks_);
    h = mix(h, tick_);
    h = mix(h, (static_cast<uint64_t>(branchMask_) << 32) | local_);
    return h;
}

std::vector<Report> RaidRun::takeReports() {
    std::vector<Report> out;
    out.swap(reports_);
    return out;
}

}  // namespace cosmic::raid
