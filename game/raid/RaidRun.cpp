#include "game/raid/RaidRun.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/enemies/Enemy.h"
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
    restoreZone(zid);  // 2.71: as the group left it (before the players are placed: its doors)
    // 2.68: the players' contributions listen to this world; an event knows whose attack caused it.
    contrib_.attach(*world_);
    world_->eventObserver = [this](const std::string& src, const std::string& trig) {
        events_.push_back({src, trig, world_ ? contrib_.ownerOf(world_->attackTag()) : -1});
    };
    rules_ = rules;
    if (zid != zone_) {
        // 2.68: kept when the same zone starts again; 2.71: and when the group comes back to it.
        const auto kept = memory_.find(zid);
        lastCheckpoint_ = kept != memory_.end() ? kept->second.checkpoint : std::string();
    }
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
    coopDone_.assign(rules->coops.size(), 0);
    if (const auto kept = memory_.find(zid); kept != memory_.end()) {
        // 2.71: the plates done stay done, their doors open, their lifts running.
        for (size_t k = 0; k < coopDone_.size() && k < kept->second.coops.size(); ++k) {
            if (!kept->second.coops[k]) continue;
            coopDone_[k] = 1;
            const CoopPlate& p = rules->coops[k];
            for (const std::string& t : p.targets)
                if (LevelEntity* e = world_->entity(t)) {
                    if (auto* door = dynamic_cast<DoorEntity*>(e); door && p.action == "open") door->setOpen(*world_, true, true);
                    else e->handleAction(*world_, p.action, Json::object());
                }
        }
    }
    for (size_t k = 0; k < rules->passages.size(); ++k)
        for (int i = 0; i < world_->teamSize(); ++i)
            if (rules->passages[k].rect.intersects(world_->playerAt(i).box)) insidePassage_[k] |= static_cast<uint8_t>(1u << i);
    applyMask(branchMask_, true);
    for (const EffectInfo& e : branch_.effects)
        if (Branch::on(mask(), e) && e.inZone(zid)) applyEffect(e);
    alert("Zone : " + (branch_.zone(zid) ? branch_.zone(zid)->name : std::string("?")), Alert::Kind::Group);
    // 2.68: the gigaboss of the zone (gone when its objective is done: it already fell).
    const int fallen = rules->gigaboss.present ? branch_.objectiveIndex(rules->gigaboss.objective) : -1;
    gigaboss_.begin(*world_, rules->gigaboss, fallen >= 0 && Branch::done(mask(), fallen), contrib_);
    gigaboss_.setBranchMask(mask());  // 2.69 (phase 4B): the objectives its routine reads
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
            alert("BRANCHE · " + branch_.lockText("generators").message, Alert::Kind::Branch);
        if (!branch_.shortcut(before) && branch_.shortcut(now)) alert("BRANCHE · " + branch_.lockText("shortcut").message, Alert::Kind::Branch);
        // 2.68: what another branch did, at this step on every computer of the instance.
        for (const EffectInfo& e : branch_.effects) {
            if (Branch::on(before, e) || !Branch::on(now, e)) continue;
            alert("TITAN · " + e.label, Alert::Kind::Branch);
            if (world_ && e.inZone(zone_)) applyEffect(e);
        }
    }
    // The doors of the passages follow their locks (the same step on every computer).
    if (!world_ || !rules_) return;
    for (const Passage& p : rules_->passages) {
        if (p.door.empty()) continue;
        auto* door = dynamic_cast<DoorEntity*>(world_->entity(p.door));
        if (door && branch_.lockOpen(p.lock, now) && !door->isOpen()) door->setOpen(*world_, true, initial);
    }
}

void RaidRun::applyEffect(const EffectInfo& e) {
    if (e.kind == "disable") {
        for (const std::string& id : e.objects)
            if (LevelEntity* x = world_->entity(id)) x->handleAction(*world_, "disable", Json::object());
    } else if (e.kind == "remove") {
        // The enemies of these spawn objects (just loaded ones too: still waiting to join the world).
        for (const std::string& id : e.objects)
            if (auto* spawn = dynamic_cast<EnemySpawnEntity*>(world_->entity(id)))
                if (Enemy* enemy = spawn->enemy(*world_); enemy && enemy->alive()) enemy->despawn();
    } else if (e.kind == "hazard_off" && magmaOn_ && rules_ && rules_->magma.present) {
        magmaOn_ = false;
        if (LevelEntity* z = world_->entity(rules_->magma.zone)) z->handleAction(*world_, "disable", Json::object());
    }
    // "open": the passages locked "link:<id>" open with their doors (applyMask).
}

RaidRun::CoopView RaidRun::coop(size_t index) const {
    CoopView v;
    if (!world_ || !rules_ || index >= rules_->coops.size()) return v;
    const CoopPlate& c = rules_->coops[index];
    int playing = 0;
    for (int i = 0; i < world_->teamSize(); ++i) {
        if (world_->member(i).presence != GameWorld::Presence::Playing) continue;
        ++playing;
        const Player& pl = world_->playerAt(i);
        if (pl.alive() && !pl.respawning() && c.rect.intersects(pl.box)) ++v.on;
    }
    v.need = std::clamp(c.players, 1, std::max(1, playing));
    if (!c.group.empty()) {
        // 2.68 (phase 3): plates held together (« group »): the players in play shared between
        // them; a plate beyond the players in play is not needed (alone: the first one is enough).
        int size = 0, rank = 0;
        for (size_t j = 0; j < rules_->coops.size(); ++j) {
            if (rules_->coops[j].group != c.group) continue;
            if (j < index) ++rank;
            ++size;
        }
        v.need = rank >= std::max(1, playing) ? 0 : std::clamp(c.players, 1, std::max(1, playing / size));
    }
    v.done = index < coopDone_.size() && coopDone_[index] != 0;
    return v;
}

void RaidRun::updateCoops() {
    for (size_t k = 0; k < rules_->coops.size(); ++k) {
        if (coopDone_[k]) continue;
        const CoopPlate& c = rules_->coops[k];
        // 2.68 (phase 3): the plates of a group, held at the same step (coop(): how many on each).
        std::vector<size_t> group{k};
        if (!c.group.empty())
            for (size_t j = 0; j < rules_->coops.size(); ++j)
                if (j != k && rules_->coops[j].group == c.group) group.push_back(j);
        bool held = true;
        for (size_t g : group) {
            const CoopView v = coop(g);
            if (v.on < v.need) held = false;
        }
        if (!held) continue;
        for (size_t g : group) {
            coopDone_[g] = 1;
            const CoopPlate& p = rules_->coops[g];
            for (const std::string& t : p.targets)
                if (LevelEntity* e = world_->entity(t)) e->handleAction(*world_, p.action, Json::object());
            // A mechanism activated, by each player standing on it (the raid's contributions).
            for (int i = 0; i < std::min(world_->teamSize(), kMaxInstancePlayers); ++i)
                if (world_->member(i).presence == GameWorld::Presence::Playing && p.rect.intersects(world_->playerAt(i).box)) contrib_.vent(i);
        }
        alert("GROUPE · " + (c.message.empty() ? c.label : c.message), Alert::Kind::Group);
    }
}

void RaidRun::reachedCheckpoint(const std::string& id) {
    if (id == lastCheckpoint_ || std::find(rules_->checkpoints.begin(), rules_->checkpoints.end(), id) == rules_->checkpoints.end()) return;
    lastCheckpoint_ = id;
    alert("GROUPE · Point de contrôle : la zone reprendra ici", Alert::Kind::Group);
    // The players down get up at the checkpoint (half their hearts), all at this step.
    const auto it = rules_->spawns.find("cp:" + id);
    if (it == rules_->spawns.end() || it->second.empty()) return;
    bool up = false;
    for (int i = 0; i < world_->teamSize(); ++i) {
        if (world_->member(i).presence != GameWorld::Presence::Down) continue;
        world_->restoreMember(i, world_->carryOf(i), it->second.front());
        up = true;
    }
    if (up) alert("GROUPE · Les joueurs à terre se relèvent au point de contrôle", Alert::Kind::Group);
}

float RaidRun::magmaIn() const {
    if (!rules_ || !rules_->magma.present || branch_.hazardOff(zone_, mask())) return -1.0f;
    const Magma& m = rules_->magma;
    const float t = static_cast<float>(zoneTicks_) / static_cast<float>(kSimHz);
    if (t < m.first) return m.first - t;
    const float k = std::fmod(t - m.first, m.period);
    return m.period - k;
}

void RaidRun::updateMagma() {
    if (!rules_->magma.present || branch_.hazardOff(zone_, mask())) return;  // 2.68: stopped from another branch
    const Magma& m = rules_->magma;
    // Whole steps only (no float drift): on during `duration` from each `first + k × period`.
    const uint32_t first = static_cast<uint32_t>(std::lround(m.first * kSimHz));
    const uint32_t period = static_cast<uint32_t>(std::lround(m.period * kSimHz));
    const uint32_t duration = static_cast<uint32_t>(std::lround(m.duration * kSimHz));
    const uint32_t warn = static_cast<uint32_t>(std::lround(m.warn * kSimHz));
    const bool on = zoneTicks_ >= first && (zoneTicks_ - first) % period < duration;
    const uint32_t next = zoneTicks_ < first ? first : first + ((zoneTicks_ - first) / period + 1) * period;
    if (next - zoneTicks_ == warn) {
        alert("DANGER · " + m.name + " dans " + std::to_string(static_cast<int>(std::lround(m.warn))) + " s : " + m.hint + " !", Alert::Kind::Danger);
        // 2.71: the rising magma shows its telegraph (nothing for the dangers without one).
        if (LevelEntity* z = world_->entity(m.zone)) z->handleAction(*world_, "warn", Json{{"seconds", m.warn}});
    }
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
        // 2.68: an action of one player (a generator hit, a relic taken) is theirs; a fight won
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
            const std::string hint = branch_.lockText(p.lock).hint;
            const std::string name = to ? to->name : "?";
            world_->hint(hint.empty() ? "Passage verrouillé vers " + name + "." : "Passage verrouillé vers " + name + " : " + hint + ".", 3.0f);
            continue;
        }
        std::vector<GameWorld::MemberCarry> carry;
        for (int i = 0; i < world_->teamSize(); ++i) carry.push_back(world_->carryOf(i));
        remember();  // 2.71: the zone left keeps what was done in it
        loadZone(p.to, std::to_string(zone_), &carry);
        return;
    }
}

void RaidRun::step(const InputFrame* frames, int count, uint32_t branchMask) {
    if (!world_) return;
    if (branchMask != branchMask_) applyMask(branchMask, false);
    updateMagma();
    gigaboss_.setBranchMask(mask());  // 2.69 (phase 4B)
    gigaboss_.before(*world_);  // 2.68: its carapace, its weak point bare, its pressure
    // The events since the last step (a world just loaded, this step): all of them, in order.
    stepTeam(*world_, frames, count, kSimDt);
    const size_t firstReport = reports_.size();
    // 2.68: the players' contributions of the step, then the gigaboss's rules (vents, jets, weak
    // points, its fall) and what they tell the raid.
    contrib_.afterStep(*world_, tick_, gigaboss_.boss(*world_));
    gigaboss_.after(*world_, contrib_);
    for (auto& [text, kind] : gigaboss_.takeAlerts())
        alert(std::move(text), kind == Gigaboss::AlertKind::Danger ? Alert::Kind::Danger : kind == Gigaboss::AlertKind::Branch ? Alert::Kind::Branch : Alert::Kind::Group);
    ++tick_;
    ++zoneTicks_;
    for (const Event& e : events_) {
        if (e.trigger == "checkpoint") reachedCheckpoint(e.source);
        completed(e.source, e.trigger, e.who);
    }
    events_.clear();
    updateCoops();
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
        alert("GROUPE · Tout le groupe est à terre : la zone reprend", Alert::Kind::Danger);
        // 2.68: at the last checkpoint reached in the zone, when there is one.
        const std::string from = lastCheckpoint_.empty() ? from_ : "cp:" + lastCheckpoint_;
        remember();  // 2.71: what the group did in it before falling stays done
        loadZone(zone_, from, &carry);
        return;
    }
    checkPassages();
    if (test_.on) reports_.clear();  // 2.68: an administrator's test never reports anything
}

uint64_t RaidRun::hash() const {
    uint64_t h = 1469598103934665603ULL;
    h = mix(h, world_ ? simulationStateHash(*world_) : 0);
    h = mix(h, static_cast<uint64_t>(zone_));
    h = mix(h, zoneTicks_);
    h = mix(h, tick_);
    h = mix(h, (static_cast<uint64_t>(branchMask_) << 32) | local_);
    h = mix(h, contrib_.hash());   // 2.68 (phase 3)
    h = mix(h, gigaboss_.hash());  // 2.68 (phase 3)
    // 2.68: the expeditions' state (nothing for the zones without checkpoints or plates).
    for (unsigned char c : lastCheckpoint_) h = mix(h, c);
    for (size_t k = 0; k < coopDone_.size(); ++k)
        if (coopDone_[k]) h = mix(h, 0x100u + k);
    // 2.71: what the zones left keep.
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

void RaidRun::remember() {
    if (!world_ || !rules_ || zone_ <= 0) return;
    ZoneMemory& m = memory_[zone_];
    for (const auto& e : world_->entities()) {
        if (e->id().empty()) continue;
        Json st = e->keepState(*world_);
        if (!st.is_null()) m.objects[e->id()] = std::move(st);
    }
    // The pickups taken are no longer in the world: what the level had and the world has not.
    for (const LevelObject& o : world_->level().objects)
        if ((o.type == "relic" || o.type == "health_orb" || o.type == "energy_orb" || o.type == "shard") && !world_->entity(o.id))
            m.objects[o.id] = Json{{"taken", true}};
    m.coops.resize(std::max(m.coops.size(), coopDone_.size()), 0);
    for (size_t k = 0; k < coopDone_.size(); ++k) m.coops[k] = static_cast<uint8_t>(m.coops[k] | coopDone_[k]);
    m.checkpoint = lastCheckpoint_;
}

void RaidRun::restoreZone(int zid) {
    if (!world_) return;
    const ZoneRules* rules = branch_.zoneRules(zid);
    // The objectives of the branch's state done in this zone (by this group or another): their
    // objects as their completion left them (the gigaboss's own is its fight's: Gigaboss).
    if (rules)
        for (const ZoneObjective& zo : rules->objectives) {
            if (!Branch::done(mask(), zo.index) || (rules->gigaboss.present && zo.source == rules->gigaboss.boss)) continue;
            LevelEntity* e = world_->entity(zo.source);
            if (!e) continue;
            if (zo.trigger == "cleared") e->restoreKept(*world_, Json{{"cleared", true}});
            else if (zo.trigger == "killed") e->restoreKept(*world_, Json{{"gone", true}});
            else if (zo.trigger == "activate") e->restoreKept(*world_, Json{{"on", true}});
            else if (zo.trigger == "collected") e->restoreKept(*world_, Json{{"taken", true}});
        }
    const auto kept = memory_.find(zid);
    if (kept == memory_.end()) return;
    for (const auto& [id, st] : kept->second.objects)
        if (LevelEntity* e = world_->entity(id)) e->restoreKept(*world_, st);
}

std::vector<Report> RaidRun::takeReports() {
    std::vector<Report> out;
    out.swap(reports_);
    if (test_.on) out.clear();  // 2.68: an administrator's test never reports anything
    return out;
}

}  // namespace cosmic::raid
