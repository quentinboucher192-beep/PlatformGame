#include "game/raid/RaidSimulation.h"

#include <algorithm>
#include <chrono>

#include "game/enemies/Enemy.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/ZoneGoals.h"

namespace cosmic::raid {

namespace {

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

constexpr uint32_t kObjectiveBits = (1u << kLinkBit0) - 1u;

int gigabossIndex(const Branch& b) {
    for (const ObjectiveInfo& o : b.objectives)
        if (o.role == "gigaboss") return o.index;
    return -1;
}

int lairZone(const Branch& b) {
    for (const ZoneInfo& z : b.zones)
        if (z.boss) return z.id;
    return b.startZone;
}

std::string partName(const Branch& b) { return b.name; }

/// The server's shared phase of the heart (raidHeart.ts phaseOf).
int phaseOf(int taken) { return taken >= 7500 ? 4 : taken >= 5000 ? 3 : taken >= 2500 ? 2 : 1; }

}  // namespace

RaidSimulation::RaidSimulation(WorldServices services, const SimSetup& setup) : services_(services), setup_(setup) {
    setup_.groups = std::clamp(setup_.groups, 1, 14);
    setup_.bots = std::clamp(setup_.bots, 1, kMaxInstancePlayers - 1);
    setup_.background = setup_.background >= 4 ? 4 : setup_.background >= 2 ? 2 : 1;
    setup_.startPhase = std::max(1, setup_.startPhase);
    for (const Branch* b : Branch::all()) {
        if (b->part == kHeartPart) continue;
        branches_.push_back(b);
    }
    for (const Branch* b : Branch::all())
        if (b->part == kHeartPart) {
            heart_ = static_cast<int>(branches_.size());
            branches_.push_back(b);
        }
    done_.assign(branches_.size(), 0);
    finished_.assign(branches_.size(), 0);
    links_.assign(branches_.size(), 0);
    visited_.assign(branches_.size(), 0);
}

int RaidSimulation::relics() const {
    // The relics of the seven branches (their secret zones' optional objectives), as raidHeart.ts RELICS.
    int n = 0;
    for (size_t b = 0; b < branches_.size(); ++b) {
        if (static_cast<int>(b) == heart_) continue;
        for (const ObjectiveInfo& o : branches_[b]->objectives) {
            const ZoneInfo* z = branches_[b]->zone(o.zone);
            if (o.optional && z && z->secret && Branch::done(done_[b], o.index)) ++n;
        }
    }
    return std::min(n, 7);
}

uint32_t RaidSimulation::mask(int branch) const {
    if (branch < 0 || branch >= static_cast<int>(branches_.size())) return 0;
    // statesOf (raidMap.ts): the objectives validated, and each link of the titan whose sources are all done.
    uint32_t m = done_[static_cast<size_t>(branch)];
    for (const EffectInfo& e : branches_[static_cast<size_t>(branch)]->effects) {
        if (e.bit < 0 || e.bit >= 32 || e.from.empty()) continue;
        bool on = true;
        for (const auto& [bid, oid] : e.from) {
            bool found = false;
            for (size_t k = 0; k < branches_.size() && !found; ++k)
                if (branches_[k]->id == bid) {
                    found = true;
                    on = on && Branch::done(done_[k], branches_[k]->objectiveIndex(oid));
                }
            on = on && found;
        }
        if (on) m |= 1u << e.bit;
    }
    return m;
}

bool RaidSimulation::gigabossDown(int branch) const {
    if (branch < 0 || branch >= static_cast<int>(branches_.size())) return false;
    return Branch::done(done_[static_cast<size_t>(branch)], gigabossIndex(*branches_[static_cast<size_t>(branch)]));
}

int RaidSimulation::gigabossesDown() const {
    int n = 0;
    for (int b = 0; b < static_cast<int>(branches_.size()); ++b)
        if (b != heart_ && gigabossDown(b)) ++n;
    return n;
}

int RaidSimulation::heartPhase() const { return heartFallen_ ? 5 : phaseOf(heartTaken_); }

std::map<int, std::string> RaidSimulation::retake(int branch) const {
    std::map<int, std::string> out;
    for (const auto& [key, who] : retake_)
        if (key.first == branch) out[key.second] = who;
    return out;
}

RaidSimulation::PartState RaidSimulation::partState(int branch) const {
    if (branch == heart_) {
        if (!heartOpen_) return PartState::Unknown;
        if (heartFallen_) return PartState::Destroyed;
        return PartState::Attacked;
    }
    if (gigabossDown(branch)) return PartState::Destroyed;
    bool here = false;
    for (const auto& g : groups_) here = here || (g->branch == branch && !g->wiped);
    int finished = 0, zones = 0;
    for (const ZoneInfo& z : branches_[static_cast<size_t>(branch)]->zones) {
        ++zones;
        if (zoneFinished(*branches_[static_cast<size_t>(branch)], z.id, mask(branch))) ++finished;
    }
    if (finished * 2 >= zones && zones > 0) return PartState::Weakened;
    if (here) return PartState::Attacked;
    return visited_[static_cast<size_t>(branch)] ? PartState::Intact : PartState::Unknown;
}

void RaidSimulation::event(std::string text, WorldEvent::Kind kind) {
    feed_.push_back({std::move(text), tick_, kind});
    if (feed_.size() > 60) feed_.erase(feed_.begin());
}

int RaidSimulation::realPart(Group& g, int own) const {
    // A test started at a later phase: the heart's health that jump removed is not damage (each instance made it
    // alone; summed it would end the fight at once): only what the group took afterwards is shared.
    const int start = setup_.heartStartPhase > 0 ? setup_.heartStartPhase : setup_.startPhase;
    if (start <= 1) return own;
    if (g.heartBase < 0) {
        if (g.session->run().lairGigaboss().phaseReached() < std::min(start, 4)) return 0;
        g.heartBase = own;
    }
    return std::max(0, own - g.heartBase);
}

RaidSimulation::Group& RaidSimulation::form(int branch, int zone, const std::string& name, uint32_t extraMask) {
    auto g = std::make_unique<Group>();
    g->id = static_cast<int>(groups_.size()) + 1;
    g->name = name;
    g->branch = branch;
    g->formed = tick_;
    RaidRun::Test t;
    t.on = true;
    t.invincible = setup_.invincible;
    t.players = 1 + setup_.bots;  // the administrator's free place and the simulated players
    t.startPhase = branch == heart_ && setup_.heartStartPhase > 0 ? setup_.heartStartPhase : setup_.startPhase;
    g->session = std::make_unique<TestSession>(services_, *branches_[static_cast<size_t>(branch)], t);
    g->session->setSkill(setup_.skill);
    g->session->setBotsInvincible(setup_.botsInvincible);
    g->session->setAdminSeated(false);
    g->session->setBranchState(mask(branch) | extraMask);
    g->session->begin(zone);
    g->session->run().setQuiet(true);
    visited_[static_cast<size_t>(branch)] = 1;
    groups_.push_back(std::move(g));
    return *groups_.back();
}

void RaidSimulation::moveGroup(Group& g, int branch, int zone) {
    const bool seatedHere = seated_ && &group(viewed_) == &g;
    RaidRun::Test t;
    t.on = true;
    t.invincible = setup_.invincible;
    t.players = 1 + setup_.bots;
    t.startPhase = branch == heart_ && setup_.heartStartPhase > 0 ? setup_.heartStartPhase : setup_.startPhase;
    g.session = std::make_unique<TestSession>(services_, *branches_[static_cast<size_t>(branch)], t);
    g.session->setSkill(setup_.skill);
    g.session->setBotsInvincible(setup_.botsInvincible);
    g.session->setAdminSeated(seatedHere);
    g.session->setBranchState(mask(branch));
    g.session->begin(zone);
    g.session->run().setQuiet(&group(viewed_) != &g);
    g.branch = branch;
    g.idleSteps = 0;
    ++g.moves;
    visited_[static_cast<size_t>(branch)] = 1;
}

bool RaidSimulation::begin() {
    if (branches_.empty()) return false;
    const int seven = heart_ >= 0 ? heart_ : static_cast<int>(branches_.size());
    if (setup_.lairs)
        for (int b = 0; b < seven; ++b)
            for (const ObjectiveInfo& o : branches_[static_cast<size_t>(b)]->objectives)
                if (o.role != "gigaboss" && !o.optional) done_[static_cast<size_t>(b)] |= 1u << o.index;
    for (int b = 0; b < seven; ++b)
        if (std::find(setup_.given.begin(), setup_.given.end(), branches_[static_cast<size_t>(b)]->part) != setup_.given.end())
            for (const ObjectiveInfo& o : branches_[static_cast<size_t>(b)]->objectives)
                if (!o.optional) done_[static_cast<size_t>(b)] |= 1u << o.index;
    for (int b = 0; b < static_cast<int>(branches_.size()); ++b)
        for (const ZoneInfo& z : branches_[static_cast<size_t>(b)]->zones)
            if (zoneFinished(*branches_[static_cast<size_t>(b)], z.id, mask(b)) && z.id < 32) finished_[static_cast<size_t>(b)] |= 1u << z.id;
    for (int b = 0; b < static_cast<int>(branches_.size()); ++b) links_[static_cast<size_t>(b)] = mask(b) & ~kObjectiveBits;
    for (int k = 0; k < setup_.groups; ++k) {
        const int b = k % std::max(1, seven);
        const Branch& br = *branches_[static_cast<size_t>(b)];
        form(b, setup_.lairs ? lairZone(br) : br.hasArmory() ? RaidRun::kArmory : br.startZone, "Groupe " + std::to_string(k + 1), 0);
    }
    viewed_ = 0;
    seated_ = true;
    group(0).session->setAdminSeated(true);
    group(0).session->run().setQuiet(false);
    event("MONDE · Le raid commence : " + std::to_string(setup_.groups) + (setup_.groups > 1 ? " groupes" : " groupe") + " de " +
              std::to_string(setup_.bots) + " joueurs simulés (test local)",
          WorldEvent::Kind::World);
    return !groups_.empty();
}

bool RaidSimulation::join(int g) {
    if (g < 0 || g >= static_cast<int>(groups_.size()) || group(g).wiped) return false;
    if (seated_) group(viewed_).session->setAdminSeated(false);
    group(viewed_).session->run().setQuiet(true);
    viewed_ = g;
    seated_ = true;
    group(g).session->setAdminSeated(true);
    group(g).session->run().setQuiet(false);
    return true;
}

void RaidSimulation::watch(int g) {
    if (g < 0 || g >= static_cast<int>(groups_.size())) return;
    if (seated_) group(viewed_).session->setAdminSeated(false);
    group(viewed_).session->run().setQuiet(true);
    viewed_ = g;
    seated_ = false;
    group(g).session->run().setQuiet(false);
}

bool RaidSimulation::hasWork(const Group& g) const {
    // 2.78: its players may be in several zones: work in any of them.
    const RaidRun& run = g.session->run();
    bool work = false;
    for (int z : run.zonesLoaded()) {
        run.focusZone(z);
        work = zoneHasWork(run);
        if (work) break;
    }
    run.focusZone(-1);
    return work;
}

bool RaidSimulation::zoneHasWork(const RaidRun& run) {
    if (Branch::isArmory(run.zone())) return true;  // an armory: its powers and its plate
    const Gigaboss& gb = run.gigaboss();
    if (gb.present() && gb.stage() != Gigaboss::Stage::Fallen && gb.boss(run.world())) return true;
    for (size_t k = 0; k < run.rules().coops.size(); ++k)
        if (!run.coop(k).done) return true;
    const Branch& br = run.branch();
    for (const ZoneObjective& zo : run.rules().objectives) {
        if (zo.index < 0 || zo.index >= static_cast<int>(br.objectives.size())) continue;
        const ObjectiveInfo& o = br.objectives[static_cast<size_t>(zo.index)];
        if (!Branch::done(run.mask(), o.index) && br.pairDone(o, run.localDone()).empty()) return true;
    }
    return TestBots::chooseExit(run) >= 0;
}

void RaidSimulation::step(const InputFrame& admin) {
    const auto t0 = std::chrono::steady_clock::now();
    // What the heart's instances give each other (raidHeart.ts heartPackFor): the others' part, the
    // shared phase, the relics validated.
    std::vector<int> own(groups_.size(), 0);
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i]->branch == heart_)
            if (const AsterionHeartRoutine* hr = groups_[i]->session->run().heartRoutine()) own[i] = realPart(*groups_[i], hr->own());
    int raw = 0;
    for (int o : own) raw += o;
    for (size_t i = 0; i < groups_.size(); ++i) {
        Group& g = *groups_[i];
        if (g.wiped) continue;  // its players spectate: nothing more is played there
        const int times = static_cast<int>(i) == viewed_ ? 1 : setup_.background;
        HeartShared hs;
        if (g.branch == heart_) {
            hs.others = static_cast<uint16_t>(std::clamp(raw - own[i], 0, kHeartUnits));
            hs.phase = static_cast<uint8_t>(heartPhase());
            hs.relics = static_cast<uint8_t>(relics());
        }
        for (int k = 0; k < times; ++k) {
            g.session->setBranchState(mask(g.branch));
            g.session->setHeartShared(hs);
            g.session->step(static_cast<int>(i) == viewed_ && seated_ ? admin : InputFrame{});
            ++worldSteps_;
            if (g.session->run().wiped()) break;
        }
    }
    ++tick_;
    aggregate();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    lastMs_ = ms;
    avgMs_ = avgMs_ <= 0.0 ? ms : avgMs_ * 0.98 + ms * 0.02;
    maxMs_ = std::max(maxMs_, ms);
}

void RaidSimulation::aggregate() {
    // What every group did, accepted for its branch (the consensus of a real instance, here at once).
    for (const auto& gp : groups_) {
        const Group& g = *gp;
        const uint32_t mine = g.session->run().localDone() & kObjectiveBits;
        uint32_t& d = done_[static_cast<size_t>(g.branch)];
        const uint32_t fresh = mine & ~d;
        if (!fresh) continue;
        d |= fresh;
        const Branch& br = *branches_[static_cast<size_t>(g.branch)];
        for (const ObjectiveInfo& o : br.objectives)
            if ((fresh >> o.index) & 1u) {
                const std::string label = o.label.substr(0, o.label.find(" ("));
                if (o.role == "gigaboss") continue;  // its fall has its own line below
                event(partName(br) + " · " + label + " (" + g.name + ")", WorldEvent::Kind::Branch);
            }
    }
    // The heart's shared health (the sum of each instance's part, raidHeart.ts), and its fall.
    if (heart_ >= 0) {
        int raw = 0;
        for (const auto& g : groups_)
            if (g->branch == heart_)
                if (const AsterionHeartRoutine* hr = g->session->run().heartRoutine()) raw += realPart(*g, hr->own());
        const int before = phaseOf(heartTaken_);
        int base = 0;  // a test started at a later phase: the health that start removed, the same for every instance
        for (const auto& g : groups_) base = std::max(base, g->heartBase);
        heartTaken_ = std::min(kHeartUnits, std::max(heartTaken_, base + raw));
        if (!heartFallen_ && phaseOf(heartTaken_) > before)
            event("CŒUR · Phase " + std::to_string(phaseOf(heartTaken_)) + " : " + std::to_string(heartTaken_ / 100) + " % de sa vie ôtés par tous les groupes",
                  WorldEvent::Kind::World);
        bool fell = false;
        for (const auto& g : groups_) fell = fell || (g->branch == heart_ && g->session->run().heartWon());
        if (!heartFallen_ && (heartTaken_ >= kHeartUnits || fell)) {
            heartFallen_ = true;
            heartTaken_ = kHeartUnits;
            const int gi = gigabossIndex(*branches_[static_cast<size_t>(heart_)]);
            if (gi >= 0) done_[static_cast<size_t>(heart_)] |= 1u << gi;
            event("MONDE · ASTERION EST TOMBÉ : le Cœur a cédé sous les coups de tous les groupes !", WorldEvent::Kind::World);
        }
    }
    // Zones finished, links of the titan, gigaboss fallen: the titan's feed.
    for (int b = 0; b < static_cast<int>(branches_.size()); ++b) {
        const Branch& br = *branches_[static_cast<size_t>(b)];
        const uint32_t m = mask(b);
        for (const ZoneInfo& z : br.zones) {
            if (z.id >= 32 || (finished_[static_cast<size_t>(b)] >> z.id) & 1u || !zoneFinished(br, z.id, m)) continue;
            finished_[static_cast<size_t>(b)] |= 1u << z.id;
            if (auto it = retake_.find({b, z.id}); it != retake_.end()) {
                event("ZONE REPRISE · " + z.name + " (" + partName(br) + ") : terminée après la chute de " + it->second, WorldEvent::Kind::World);
                retake_.erase(it);
            } else if (!z.boss) {
                event(partName(br) + " · Zone terminée : " + z.name, WorldEvent::Kind::Branch);
            }
        }
        const uint32_t on = m & ~kObjectiveBits;
        for (const EffectInfo& e : br.effects)
            if (e.bit >= 0 && e.bit < 32 && ((on >> e.bit) & 1u) && !((links_[static_cast<size_t>(b)] >> e.bit) & 1u))
                event("TITAN · " + e.label, WorldEvent::Kind::World);
        links_[static_cast<size_t>(b)] = on;
    }
    for (int b = 0; b < static_cast<int>(branches_.size()); ++b) {
        if (b == heart_ || !gigabossDown(b) || ((finished_[static_cast<size_t>(b)] >> 31) & 1u)) continue;
        finished_[static_cast<size_t>(b)] |= 1u << 31;  // told once
        const Branch& br = *branches_[static_cast<size_t>(b)];
        event("MONDE · " + br.gigaboss + " est tombé — " + partName(br) + " détruit (" + std::to_string(gigabossesDown()) + " / 7 Gigaboss)",
              WorldEvent::Kind::World);
    }
    // A group wiped: its players spectate; a reinforcement takes the zone back.
    for (size_t i = 0; i < groups_.size(); ++i) {
        Group& g = *groups_[i];
        if (g.wiped || !g.session->run().wiped()) continue;
        g.wiped = true;
        const int zone = g.session->run().wipedZone();
        const Branch& br = *branches_[static_cast<size_t>(g.branch)];
        const ZoneInfo* z = br.zone(zone);
        const std::string where = (z ? z->name : std::string("?")) + " (" + partName(br) + ")";
        retake_[{g.branch, zone}] = g.name;
        event("GROUPE ÉLIMINÉ · " + g.name + " dans " + where + " : ses joueurs regardent, la zone est à reprendre", WorldEvent::Kind::Danger);
        if (static_cast<int>(i) == viewed_ && seated_) {
            seated_ = false;  // the administrator spectates with the group
            g.session->setAdminSeated(false);
        }
        if (reinforcements_ < setup_.maxReinforcements && !(g.branch == heart_ && heartFallen_)) {
            ++reinforcements_;
            const int branch = g.branch, id = g.id;
            Group& r = form(branch, zone, "Renfort " + std::to_string(reinforcements_), 0);
            r.relieves = id;
            event("RENFORT · " + r.name + " part reprendre " + where, WorldEvent::Kind::World);
        }
    }
    // The heart opens once the seven gigaboss fell: every group still playing goes to it.
    if (heart_ >= 0 && !heartOpen_ && gigabossesDown() >= heart_) {
        heartOpen_ = true;
        const Branch& hb = *branches_[static_cast<size_t>(heart_)];
        if (setup_.lairs)
            for (const ObjectiveInfo& o : hb.objectives)
                if (o.role != "gigaboss" && !o.optional) done_[static_cast<size_t>(heart_)] |= 1u << o.index;
        event("MONDE · Les sept Gigaboss sont tombés : le Cœur d'Asterion est ouvert, tous les groupes y vont !", WorldEvent::Kind::World);
        for (auto& g : groups_)
            if (!g->wiped && g->branch != heart_) moveGroup(*g, heart_, setup_.lairs ? lairZone(hb) : hb.hasArmory() ? RaidRun::kArmory : hb.startZone);
        return;
    }
    // A group with nothing left to do in its branch for 5 s: where a group is needed.
    if (tick_ % 60 != 0) return;
    for (auto& gp : groups_) {
        Group& g = *gp;
        if (g.wiped || g.branch == heart_) continue;
        g.idleSteps = hasWork(g) ? 0 : g.idleSteps + 60;
        if (g.idleSteps < 5 * kSimHz) continue;
        int best = -1, bestCount = 1 << 20;
        for (int b = 0; b < static_cast<int>(branches_.size()); ++b) {
            if (b == heart_ || gigabossDown(b)) continue;
            int count = 0;
            for (const auto& o : groups_) count += o->branch == b && !o->wiped && o.get() != &g ? 1 : 0;
            // Where a group is missing: the groups its unfinished zones ask for (a pair: 2), at least one.
            int need = 1;
            for (const ZoneInfo& z : branches_[static_cast<size_t>(b)]->zones)
                if (!zoneFinished(*branches_[static_cast<size_t>(b)], z.id, mask(b))) need = std::max(need, z.groupsRequired);
            if (count >= need) continue;
            if (count < bestCount) {
                bestCount = count;
                best = b;
            }
        }
        g.idleSteps = 0;
        if (best < 0 || best == g.branch) continue;
        const Branch& to = *branches_[static_cast<size_t>(best)];
        event(g.name + " rejoint la branche " + partName(to) + " (plus rien à faire dans la sienne)", WorldEvent::Kind::Group);
        moveGroup(g, best, to.hasArmory() ? RaidRun::kArmory : to.startZone);
    }
}

uint64_t RaidSimulation::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const auto& g : groups_) {
        h = mix(h, g->session->hash());
        h = mix(h, (static_cast<uint64_t>(g->branch) << 8) | (g->wiped ? 1u : 0u));
    }
    for (uint32_t d : done_) h = mix(h, d);
    h = mix(h, static_cast<uint64_t>(heartTaken_));
    return mix(h, tick_);
}

}  // namespace cosmic::raid
