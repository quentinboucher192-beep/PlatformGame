#include "game/raid/RaidTestRelay.h"

#include <algorithm>
#include <cstdio>

#include "game/online/LevelFingerprint.h"
#include "game/raid/AsterionHeartRoutine.h"
#include "game/raid/RaidBranch.h"
#include "game/raid/RaidSimulation.h"
#include "game/raid/RaidTest.h"

namespace cosmic::raid {

namespace {

/// A whole number of the server's copy within [lo, hi], or nothing.
std::optional<int> intIn(const Json& j, const char* key, int lo, int hi) {
    if (!j.is_object()) return std::nullopt;
    auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return std::nullopt;
    const int64_t v = it->get<int64_t>();
    if (v < lo || v > hi) return std::nullopt;
    return static_cast<int>(v);
}

std::optional<bool> boolOf(const Json& j, const char* key, std::optional<bool> def = std::nullopt) {
    if (!j.is_object()) return std::nullopt;
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return def;
    if (!it->is_boolean()) return std::nullopt;
    return it->get<bool>();
}

InputFrame vacantFrame() {
    InputFrame f;
    f.flags = InputFrame::Vacant;
    return f;
}

}  // namespace

Json RelayedTestConfig::toJson() const {
    Json c = {{"skill", std::clamp(skill, 0, 2)}, {"invincible", invincible}, {"bots_invincible", botsInvincible}};
    if (fullRaid) {
        c["groups"] = std::clamp(groups, 1, 14);
        c["bots"] = std::clamp(bots, 1, kMaxInstancePlayers - 1);
    } else {
        c["branch"] = branch;
        c["zone"] = zone;
        c["players"] = std::clamp(players, 1, kMaxInstancePlayers);
        c["start_phase"] = std::clamp(startPhase, 1, 4);
        c["parts"] = static_cast<int>(parts & 0x7Fu);
        c["relics"] = std::clamp(relics, 0, 7);
    }
    return c;
}

std::optional<RelayedTestConfig> RelayedTestConfig::fromJson(const std::string& mode, const Json& j) {
    if (mode != "zone" && mode != "raid") return std::nullopt;
    RelayedTestConfig c;
    c.fullRaid = mode == "raid";
    const auto skill = intIn(j, "skill", 0, 2);
    const auto inv = boolOf(j, "invincible");
    const auto botsInv = boolOf(j, "bots_invincible", false);
    if (!skill || !inv || !botsInv) return std::nullopt;
    c.skill = *skill;
    c.invincible = *inv;
    c.botsInvincible = *botsInv;
    if (c.fullRaid) {
        const auto groups = intIn(j, "groups", 1, 14), bots = intIn(j, "bots", 1, kMaxInstancePlayers - 1);
        if (!groups || !bots) return std::nullopt;
        c.groups = *groups;
        c.bots = *bots;
        return c;
    }
    const std::string branch = jstr(j, "branch");
    const auto zone = intIn(j, "zone", -1, 64), players = intIn(j, "players", 1, kMaxInstancePlayers), phase = intIn(j, "start_phase", 1, 4),
               parts = intIn(j, "parts", 0, 127), relics = intIn(j, "relics", 0, 7);
    if (branch.empty() || branch.size() > 40 || !zone || !players || !phase || !parts || !relics) return std::nullopt;
    c.branch = branch;
    c.zone = *zone;
    c.players = *players;
    c.startPhase = *phase;
    c.parts = static_cast<uint32_t>(*parts);
    c.relics = *relics;
    return c;
}

std::unique_ptr<TestSession> buildZoneTest(const WorldServices& services, const RelayedTestConfig& c) {
    const Branch* branch = nullptr;
    for (const Branch* b : Branch::all())
        if (b->id == c.branch) branch = b;
    if (!branch || !branch->zone(c.zone == RaidRun::kArmory ? 0 : c.zone)) return nullptr;  // 2.75: the armory too
    RaidRun::Test t;
    t.on = true;
    t.invincible = c.invincible;
    t.players = c.players;
    t.startPhase = c.startPhase;
    auto session = std::make_unique<TestSession>(services, *branch, t);
    session->setSkill(static_cast<BotSkill>(std::clamp(c.skill, 0, 2)));  // 2.77
    session->setBotsInvincible(c.botsInvincible);
    // 2.74 (phase 6): the heart's test: the parts chosen as fallen (their links on), the relics.
    uint32_t links = 0;
    for (const EffectInfo& e : branch->effects)
        for (int p = 0; p < AsterionHeartRoutine::PartCount; ++p)
            if ((c.parts >> p) & 1u && e.id == std::string("coeur_") + AsterionHeartRoutine::partId(p) && e.bit >= 0 && e.bit < 32) links |= 1u << e.bit;
    HeartShared heart;
    heart.relics = static_cast<uint8_t>(std::clamp(c.relics, 0, 7));
    session->setHeart(links, heart);
    if (!session->begin(c.zone)) return nullptr;
    return session;
}

std::unique_ptr<RaidSimulation> buildRaidSimulation(const WorldServices& services, const RelayedTestConfig& c, int background) {
    SimSetup s;
    s.groups = c.groups;
    s.bots = c.bots;
    s.invincible = c.invincible;
    s.botsInvincible = c.botsInvincible;
    s.skill = static_cast<BotSkill>(std::clamp(c.skill, 0, 2));
    s.background = background;
    auto sim = std::make_unique<RaidSimulation>(services, s);
    if (!sim->begin()) return nullptr;
    return sim;
}

std::string testFingerprint() {
    // The titan of every branch (their ids and revisions: RaidBranch::levelHash), folded in 32 bits.
    uint64_t h = 1469598103934665603ULL;
    auto mixText = [&h](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= 0xFFu;
        h *= 1099511628211ULL;
    };
    for (const Branch* b : Branch::all()) {
        mixText(b->id);
        mixText(b->levelHash());
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s|s%d|c%d|t%08x", COSMIC_VERSION_STRING, kSimulationVersion, kCoopSimulationVersion,
                  static_cast<unsigned>((h ^ (h >> 32)) & 0xFFFFFFFFu));
    return buf;
}

bool playedFrame(const InputFrame& f) { return (f.flags & (InputFrame::Vacant | InputFrame::Standby)) == 0; }

void stepRelayed(TestSession& s, const std::vector<InputFrame>& frames, const coopnet::TestSeats& seats) {
    const auto frameOf = [&frames](size_t k) { return k < frames.size() ? frames[k] : vacantFrame(); };
    const coopnet::TestSeat& a = seats[0];
    // The administrator: their frame on seat 0 (away: neutral, their autopilot takes over after 15 s).
    const bool adminHere = a.placed() && a.group == 0;
    s.setAdminSeated(adminHere);
    InputFrame admin = playedFrame(frameOf(0)) ? frameOf(0) : InputFrame{};
    admin.flags = 0;
    uint32_t invincible = adminHere && a.invincible() ? 1u : 0u;
    uint32_t humans = 0;
    std::vector<std::pair<int, InputFrame>> played;
    for (size_t k = 1; k < seats.size(); ++k) {
        const coopnet::TestSeat& e = seats[k];
        if (!e.placed() || e.group != 0 || e.seat < 1 || e.seat >= s.players()) continue;
        if (e.invincible()) invincible |= 1u << e.seat;
        const InputFrame f = frameOf(k);
        if (!playedFrame(f) || ((humans >> e.seat) & 1u)) continue;  // away: their simulated player plays the seat
        humans |= 1u << e.seat;
        InputFrame clean = f;
        clean.flags = 0;
        played.push_back({e.seat, clean});
    }
    s.setInvincibleSeats(invincible);
    s.setHumanSeats(humans);
    s.step(admin, played);
}

void stepRelayed(RaidSimulation& sim, const std::vector<InputFrame>& frames, const coopnet::TestSeats& seats) {
    const auto frameOf = [&frames](size_t k) { return k < frames.size() ? frames[k] : vacantFrame(); };
    // The administrator's place, as the relay gave it: a group's free place, or watching.
    const coopnet::TestSeat& a = seats[0];
    const int groups = static_cast<int>(sim.groups().size());
    const int want = a.placed() && a.group < groups ? a.group : -1;
    const int now = sim.seated() ? sim.viewed() : -1;
    if (want != now) {
        if (want < 0) sim.watch(sim.viewed());
        else sim.join(want);  // refused for a wiped group: the administrator stays where they are
    }
    InputFrame admin = playedFrame(frameOf(0)) ? frameOf(0) : InputFrame{};
    admin.flags = 0;
    // Each group's invincible seats: its free place as the test says (SimSetup::invincible), the invited players ticked.
    std::vector<uint32_t> invincible(static_cast<size_t>(groups), sim.setup().invincible ? 1u : 0u);
    std::vector<RaidSimulation::Human> humans;
    for (size_t k = 1; k < seats.size(); ++k) {
        const coopnet::TestSeat& e = seats[k];
        if (!e.placed() || e.group >= groups || e.seat < 1) continue;
        if (e.invincible()) invincible[e.group] |= 1u << e.seat;
        const InputFrame f = frameOf(k);
        if (!playedFrame(f)) continue;  // away: their simulated player plays the seat
        RaidSimulation::Human h;
        h.group = e.group;
        h.seat = e.seat;
        h.frame = f;
        h.frame.flags = 0;
        humans.push_back(h);
    }
    for (int g = 0; g < groups; ++g) sim.setInvincibleSeats(g, invincible[static_cast<size_t>(g)]);
    sim.step(admin, humans);
}

}  // namespace cosmic::raid
