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
    // §40: the branches one group alone never takes to their gigaboss. Everything a group can do alone done (of each
    // pair, only one objective: an instance never does both), is its lair reachable? The Torse's 4 plates are two pairs
    // (its lock wants all four): no. The links of the titan are left out (another branch's work).
    needsTwo_.assign(branches_.size(), 0);
    for (size_t b = 0; b < branches_.size(); ++b)
        if (static_cast<int>(b) != heart_) needsTwo_[b] = reachesAlone(static_cast<int>(b), 0) ? 0 : 1;  // nothing validated yet
}

bool RaidSimulation::needsTwoGroups(int branch) const {
    return branch >= 0 && branch < static_cast<int>(needsTwo_.size()) && needsTwo_[static_cast<size_t>(branch)] != 0;
}

int RaidSimulation::startBranch(int k) const {
    // §40: one group per branch in the titan's order; the groups after the seventh double the branches one group cannot
    // finish first (the Torse), then the others in the titan's order.
    const int seven = heart_ >= 0 ? heart_ : static_cast<int>(branches_.size());
    if (seven <= 0) return 0;
    if (k < seven) return k;
    std::vector<int> extra;
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < seven; ++b)
            if (needsTwoGroups(b) == (pass == 0)) extra.push_back(b);
    return extra[static_cast<size_t>((k - seven) % static_cast<int>(extra.size()))];
}

int RaidSimulation::startZone(int branch) const {
    const Branch& br = *branches_[static_cast<size_t>(branch)];
    return setup_.lairs ? lairZone(br) : br.hasArmory() ? RaidRun::kArmory : br.startZone;
}

int RaidSimulation::groupsIn(int branch) const {
    int n = 0;
    for (const auto& g : groups_) n += g->branch == branch && !g->wiped ? 1 : 0;
    return n;
}

bool RaidSimulation::reachesAlone(int branch, uint32_t mine) const {
    // What the branch has validated, and everything this group can still do there alone (of a pair, never its second
    // half): is its lair reachable then? A group that is only slow can (it is never helped); the Torse's lone group whose
    // plates are half done cannot.
    const Branch& br = *branches_[static_cast<size_t>(branch)];
    uint32_t m = mask(branch);
    for (const ObjectiveInfo& o : br.objectives) {
        if (o.optional || o.role == "gigaboss" || o.index < 0 || o.index >= kLinkBit0 || Branch::done(m, o.index)) continue;
        if (!br.pairDone(o, mine).empty()) continue;
        mine |= 1u << o.index;
        m |= 1u << o.index;
    }
    const int lair = lairZone(br);
    const std::vector<bool> r = br.reachable(m);
    return lair >= 1 && lair < static_cast<int>(r.size()) && r[static_cast<size_t>(lair)];
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
    t.difficulty = setup_.difficulty;  // titan 13
    g->session = std::make_unique<TestSession>(services_, *branches_[static_cast<size_t>(branch)], t);
    g->session->setSkill(setup_.skill);
    // §40: a second group of the same branch from the same start would play the very same steps as the first (the same
    // seed): both did the same half of each pair, and the Torse's lock stayed shut for ever. Its own seed (the first
    // group of a branch keeps 0: its game is the one of before).
    g->session->setBotSeed(static_cast<uint32_t>(groupsIn(branch)));
    g->session->setBotsInvincible(setup_.botsInvincible);
    g->session->setRescues(setup_.rescues);
    g->session->setFreeSeatPlayed(setup_.fullGroups);  // 2.77.3
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
    t.difficulty = setup_.difficulty;  // titan 13
    if (g.session) g.past.push_back({g.branch, g.session->run().contributions().raidTable(), g.session->run().finishes()});  // 2.79: display only
    g.session = std::make_unique<TestSession>(services_, *branches_[static_cast<size_t>(branch)], t);
    g.session->setSkill(setup_.skill);
    int others = 0;  // §40: its seed (form()): the groups already there (all of them at the heart's opening, one by one)
    for (const auto& o : groups_) others += o.get() != &g && o->branch == branch && !o->wiped ? 1 : 0;
    g.session->setBotSeed(static_cast<uint32_t>(others));
    g.session->setBotsInvincible(setup_.botsInvincible);
    g.session->setRescues(setup_.rescues);
    g.session->setFreeSeatPlayed(setup_.fullGroups);  // 2.77.3
    g.session->setAdminSeated(seatedHere);
    g.session->setBranchState(mask(branch));
    g.session->begin(zone);
    g.session->run().setQuiet(&group(viewed_) != &g);
    g.branch = branch;
    g.waiting = false;
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
    for (int k = 0; k < setup_.groups; ++k) form(startBranch(k), startZone(startBranch(k)), "Groupe " + std::to_string(k + 1), 0);
    viewed_ = 0;
    seated_ = true;
    group(0).session->setAdminSeated(true);
    group(0).session->run().setQuiet(false);
    if (setup_.fullGroups)  // 2.77.3: every group the same size, the administrator's included
        event("MONDE · Le raid commence : " + std::to_string(setup_.groups) + (setup_.groups > 1 ? " groupes" : " groupe") + " de " +
                  std::to_string(setup_.bots + 1) + " joueurs (le vôtre : vous et " + std::to_string(setup_.bots) + " joueurs simulés ; test local)",
              WorldEvent::Kind::World);
    else
        event("MONDE · Le raid commence : " + std::to_string(setup_.groups) + (setup_.groups > 1 ? " groupes" : " groupe") + " de " +
                  std::to_string(setup_.bots) + " joueurs simulés (test local)",
              WorldEvent::Kind::World);
    // §40: the groups after the seventh double a branch (said once).
    for (int k = seven; k < setup_.groups && seven > 0; ++k) {
        const Group& g = group(k);
        if (needsTwoGroups(g.branch))
            event(g.name + " double la branche " + partName(*branches_[static_cast<size_t>(g.branch)]) + " : un groupe seul n'en atteint jamais le Gigaboss",
                  WorldEvent::Kind::Group);
        else
            event(g.name + " double la branche " + partName(*branches_[static_cast<size_t>(g.branch)]), WorldEvent::Kind::Group);
    }
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

void RaidSimulation::step(const InputFrame& admin) { step(admin, {}); }

void RaidSimulation::setInvincibleSeats(int group, uint32_t seats) {
    if (group >= 0) invincible_[group] = seats;
}

void RaidSimulation::step(const InputFrame& admin, const std::vector<Human>& humans) {
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
        // 2.80: the invited players of this group (their seats and frames), its invincible seats.
        std::vector<std::pair<int, InputFrame>> own;
        uint32_t held = 0;
        for (const Human& h : humans)
            if (h.group == static_cast<int>(i) && h.seat >= 1 && h.seat < g.session->players() && !((held >> h.seat) & 1u)) {
                held |= 1u << h.seat;
                own.push_back({h.seat, h.frame});
            }
        g.session->setHumanSeats(held);
        if (auto it = invincible_.find(static_cast<int>(i)); it != invincible_.end()) g.session->setInvincibleSeats(it->second);
        for (int k = 0; k < times; ++k) {
            g.session->setBranchState(mask(g.branch));
            g.session->setHeartShared(hs);
            g.session->step(static_cast<int>(i) == viewed_ && seated_ ? admin : InputFrame{}, own);
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
    // A group wiped: its players spectate; its zone waits for a waiting group to replace it (§40: never new players
    // from nowhere). At the heart nobody waits: its part of the heart's health stays counted.
    for (size_t i = 0; i < groups_.size(); ++i) {
        Group& g = *groups_[i];
        if (g.wiped || !g.session->run().wiped()) continue;
        g.wiped = true;
        g.waiting = false;
        const int zone = g.session->run().wipedZone();
        const Branch& br = *branches_[static_cast<size_t>(g.branch)];
        const ZoneInfo* z = br.zone(zone);
        const std::string where = (z ? z->name : std::string("?")) + " (" + partName(br) + ")";
        if (static_cast<int>(i) == viewed_ && seated_) {
            seated_ = false;  // the administrator spectates with the group
            g.session->setAdminSeated(false);
        }
        if (g.branch == heart_) {
            event("GROUPE ÉLIMINÉ · " + g.name + " dans " + where + " : ses joueurs regardent, sa part du Cœur reste comptée", WorldEvent::Kind::Danger);
            continue;
        }
        retake_[{g.branch, zone}] = g.name;
        event("GROUPE ÉLIMINÉ · " + g.name + " dans " + where + " : ses joueurs regardent, la zone est à reprendre", WorldEvent::Kind::Danger);
        if (!gigabossDown(g.branch)) vacancies_.push_back({g.branch, zone, g.id, tick_});
    }
    // §40: a group whose branch's gigaboss fell stays there, waiting (it never leaves to help a group that is only slow).
    for (auto& gp : groups_) {
        Group& g = *gp;
        if (g.wiped || g.waiting || g.branch == heart_ || !gigabossDown(g.branch)) continue;
        g.waiting = true;
        g.waitingSince = tick_;
        const Branch& br = *branches_[static_cast<size_t>(g.branch)];
        if (heart_ >= 0 && gigabossesDown() >= heart_) continue;  // the seventh: everyone goes to the heart at once
        event("EN ATTENTE · " + g.name + " reste dans la branche " + partName(br) + " (" + br.gigaboss + " tombé) : il n'en part que pour remplacer un groupe éliminé",
              WorldEvent::Kind::Group);
    }
    // The heart opens once the seven gigaboss fell: every group still playing goes to it.
    if (heart_ >= 0 && !heartOpen_ && gigabossesDown() >= heart_) {
        heartOpen_ = true;
        const Branch& hb = *branches_[static_cast<size_t>(heart_)];
        if (setup_.lairs)
            for (const ObjectiveInfo& o : hb.objectives)
                if (o.role != "gigaboss" && !o.optional) done_[static_cast<size_t>(heart_)] |= 1u << o.index;
        event("MONDE · Les sept Gigaboss sont tombés : le Cœur d'Asterion est ouvert, tous les groupes y vont !", WorldEvent::Kind::World);
        vacancies_.clear();  // the branches are all fallen: nothing left to take back
        int fighters = 0;
        for (auto& g : groups_)
            if (!g->wiped && g->branch != heart_) {
                moveGroup(*g, heart_, startZone(heart_));
                ++fighters;
            }
        if (fighters > 1)
            event("CŒUR · " + std::to_string(fighters) + " groupes le combattent, chacun dans son instance : une seule vie pour tous, leurs dégâts additionnés",
                  WorldEvent::Kind::World);
        return;
    }
    // §40: the waiting groups where the rules send them (twice a second).
    if (tick_ % 60 == 0) dispatch();
}

RaidSimulation::Group* RaidSimulation::longestWaiting() {
    Group* best = nullptr;
    for (auto& gp : groups_) {
        Group& g = *gp;
        if (!g.waiting || g.wiped || g.branch == heart_) continue;
        if (!best || g.waitingSince < best->waitingSince) best = &g;  // ties: the lowest id (the order of groups_)
    }
    return best;
}

void RaidSimulation::dispatch() {
    if (heartOpen_) return;
    // 1. A wiped group's place (oldest first): a waiting group replaces it where it fell, the branch's validated state
    //    kept (as the server's retake: start_zone). Its branch fallen since: nothing to replace.
    for (auto it = vacancies_.begin(); it != vacancies_.end();) {
        if (gigabossDown(it->branch)) {
            it = vacancies_.erase(it);
            continue;
        }
        Group* w = longestWaiting();
        if (!w) return;  // nobody waits yet: the place stays to take
        const Branch& br = *branches_[static_cast<size_t>(it->branch)];
        const ZoneInfo* z = br.zone(it->zone);
        std::string wiped = "?";
        for (const auto& g : groups_)
            if (g->id == it->group) wiped = g->name;
        event("REMPLACEMENT · " + w->name + " quitte sa branche pour remplacer " + wiped + ", éliminé dans " + (z ? z->name : std::string("?")) + " (" +
                  partName(br) + ") : il reprend cette zone",
              WorldEvent::Kind::World);
        moveGroup(*w, it->branch, it->zone);
        w->relieves = it->group;
        it = vacancies_.erase(it);
    }
    const int seven = heart_ >= 0 ? heart_ : static_cast<int>(branches_.size());
    // 2. A branch no group plays (fewer groups than branches): a waiting group takes it from its start.
    for (int b = 0; b < seven; ++b) {
        if (gigabossDown(b) || groupsIn(b) > 0) continue;
        bool pending = false;
        for (const Vacancy& v : vacancies_) pending = pending || v.branch == b;
        if (pending) continue;
        Group* w = longestWaiting();
        if (!w) return;
        event(w->name + " quitte sa branche pour la branche " + partName(*branches_[static_cast<size_t>(b)]) + ", qu'aucun groupe ne joue", WorldEvent::Kind::Group);
        moveGroup(*w, b, startZone(b));
    }
    // 3. A branch whose only group can never reach its gigaboss alone (the Torse's pairs, half done by it): a waiting group
    //    becomes its second group. A group that is only slow is never helped.
    for (int b = 0; b < seven; ++b) {
        if (gigabossDown(b) || groupsIn(b) != 1) continue;
        const Group* lone = nullptr;
        for (const auto& g : groups_)
            if (g->branch == b && !g->wiped) lone = g.get();
        if (!lone || reachesAlone(b, lone->session->run().localDone())) continue;
        Group* w = longestWaiting();
        if (!w) return;
        event(w->name + " quitte sa branche pour la branche " + partName(*branches_[static_cast<size_t>(b)]) + " : un groupe seul n'en atteint jamais le Gigaboss",
              WorldEvent::Kind::Group);
        moveGroup(*w, b, startZone(b));
    }
}

uint64_t RaidSimulation::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const auto& g : groups_) {
        h = mix(h, g->session->hash());
        h = mix(h, (static_cast<uint64_t>(g->branch) << 8) | (g->wiped ? 1u : 0u) | (g->waiting ? 2u : 0u));
    }
    for (uint32_t d : done_) h = mix(h, d);
    h = mix(h, static_cast<uint64_t>(heartTaken_));
    return mix(h, tick_);
}

int RaidSimulation::botDeaths() const {
    int k = 0;
    for (const auto& g : groups_) k += g->session ? g->session->botDeaths() : 0;
    return k;
}

int RaidSimulation::rescues() const {
    int k = 0;
    for (const auto& g : groups_) k += g->session ? g->session->rescues() : 0;
    return k;
}

}  // namespace cosmic::raid
