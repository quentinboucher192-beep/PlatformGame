#include "game/raid/RaidTest.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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

// --------------------------------------------------------------------------------- the session

TestSession::TestSession(WorldServices services, const Branch& branch, const RaidRun::Test& setup)
    : run_(services, branch, kMaxInstancePlayers) {
    RaidRun::Test t = setup;
    t.on = true;
    run_.setTest(t);
    players_ = run_.testSetup().players;
}

TestBots& TestSession::botsFor(int zone) {
    auto it = bots_.find(zone);
    if (it == bots_.end()) {
        it = bots_.emplace(zone, TestBots{}).first;
        it->second.reset(players_);
        it->second.setSkill(skill_);
        it->second.setSeed(seed_);
        it->second.setNames(names_);  // 2.79: the names in their thoughts, the order of the administrator
        if (lastOrder_.kind != bots::OrderKind::None) it->second.order(lastOrder_);
    }
    return it->second;
}

const TestBots& TestSession::botsOf(int zone) const {
    static const TestBots none = [] {
        TestBots b;
        b.reset(1);
        return b;
    }();
    const auto it = bots_.find(zone);
    return it != bots_.end() ? it->second : none;
}

bool TestSession::begin(int zone) { return run_.begin(state_, zone); }

std::string TestSession::botName(int seat) { return "Joueur simulé " + std::to_string(seat + 1); }

uint64_t TestSession::hash() const {
    uint64_t h = run_.hash();
    for (const auto& [zone, b] : bots_) h = mix(mix(h, static_cast<uint64_t>(zone + 1)), b.hash());  // 2.78: each zone's
    return mix(h, state_);
}

void TestSession::step(const InputFrame& adminIn, const std::vector<std::pair<int, InputFrame>>& scripted) {
    frames_.assign(static_cast<size_t>(kMaxInstancePlayers), InputFrame{});
    // 2.79: an order to the simulated players rides on the administrator's frame (the order wheel): taken
    // here, at this step, and the world gets the frame without it — the same frames, the same test.
    bots::Order order;
    if (bots::orderOf(adminIn, order) && adminSeated_) {
        lastOrder_ = order;
        for (auto& [zone, b] : bots_) b.order(order);  // 2.78: the simulated players of every zone hear it
    }
    const InputFrame admin = bots::withoutOrder(adminIn);
    // 2.77: the administrator AFK (no key held for 15 s): the AI plays their seat until they touch one.
    if (adminSeated_) afk_ = admin.held != 0 || admin.pressed != 0 || admin.moveX != 0 || admin.moveY != 0 || admin.choice != 0 ? 0 : std::min(afk_ + 1, kAfkSteps + 1);
    else afk_ = 0;
    uint32_t seats = 0;
    for (int s = 1; s < players_; ++s) seats |= 1u << s;
    seats &= ~humans_;  // 2.80: the seats real players play (their frames come in `scripted`)
    if (seatZeroBot()) seats |= 1u;  // 2.77.3: also the administrator's seat while they are not in the group (freeSeatPlayed_)
    if (names_.empty() || namesFor_ != (adminSeated_ && !autopilot())) {
        // The names in the simulated players' thoughts (« réanime Vous », « réanime Joueur simulé 3 »).
        namesFor_ = adminSeated_ && !autopilot();
        names_.clear();
        for (int s = 0; s < kMaxInstancePlayers; ++s) names_.push_back(s == 0 && namesFor_ ? std::string("Vous") : botName(s));
        for (auto& [zone, b] : bots_) b.setNames(names_);
    }
    frames_[0] = admin;
    thinkByZone(seats);
    if (!adminSeated_ && !freeSeatPlayed_) {  // 2.77.3: or played by a simulated player
        frames_[0] = InputFrame{};
        frames_[0].flags = InputFrame::Vacant;  // the free place of a group
    }
    for (int s = players_; s < kMaxInstancePlayers; ++s) frames_[static_cast<size_t>(s)].flags = InputFrame::Vacant;
    for (const auto& [seat, f] : scripted)  // a capture's script plays its seat
        if (seat > 0 && seat < kMaxInstancePlayers) frames_[static_cast<size_t>(seat)] = f;
    run_.step(frames_.data(), static_cast<int>(frames_.size()), state_, heart_);
    // What the group did is accepted at once, as the server would after the consensus of a real
    // instance: given back with the next step (RAID_STATE's structure, the objectives' bits).
    state_ |= run_.localDone();
    if (botsInvincible_)
        for (int s = seatZeroBot() ? 0 : 1; s < players_; ++s)  // 2.77.3: the free seat played too
            if (GameWorld* w = run_.worldOf(run_.zoneOf(s)); w && s < w->teamSize())
                if (!((humans_ >> s) & 1u) && w->member(s).presence == GameWorld::Presence::Playing && w->playerAt(s).alive())  // 2.80: not a real player's
                    w->playerAt(s).health = w->playerAt(s).maxHealth();
    // 2.79: the simulated players put down (counted once each time; 2.78: each in the world of its zone).
    {
        uint8_t down = 0;
        for (int s = 0; s < players_ && s < 8; ++s)
            if (const GameWorld* w = run_.worldOf(run_.zoneOf(s)); w && s < w->teamSize() && w->member(s).presence == GameWorld::Presence::Down &&
                                                                  ((seats >> s) & 1u))
                down |= static_cast<uint8_t>(1u << s);
        for (int s = 0; s < 8; ++s)
            if (((down >> s) & 1u) && !((downBefore_ >> s) & 1u)) ++botDeaths_;
        downBefore_ = down;
    }
    // 2.78: the simulated players of the zones nobody is in any more go with them.
    const std::vector<int> loaded = run_.zonesLoaded();
    for (auto it = bots_.begin(); it != bots_.end();)
        it = std::find(loaded.begin(), loaded.end(), it->first) == loaded.end() ? bots_.erase(it) : std::next(it);
    if (rescuesOn_) rescue();
    run_.takeReports();  // empty: a test never reports anything
}

void TestSession::thinkByZone(uint32_t seats) {
    // 2.78: the simulated players think in their own zone (each zone where players are has its own,
    // in the order of the zones: deterministic), its world the one they read.
    for (int z : run_.zonesLoaded()) {
        TestBots& b = botsFor(z);
        const uint32_t here = run_.seatsIn(z);
        for (int s = 0; s < kMaxInstancePlayers; ++s)
            if ((here >> s) & 1u) {
                if (lastZone_[s] != z) b.bots()[static_cast<size_t>(s)] = TestBots::Bot{};  // it just came: a fresh mind
                lastZone_[s] = z;
            }
        if (!(seats & here)) continue;
        run_.focusZone(z);
        b.think(run_, frames_.data(), static_cast<int>(frames_.size()), seats & here, adminSeated_ && !autopilot() && run_.zoneOf(0) == z);
        // 2.75: a power offered to a simulated player (the armory's tablets): its answer, through its frame.
        const GameWorld& w = *run_.worldOf(z);
        if (const auto& offer = w.powerOffer(); offer && offer->player >= 0 && offer->player < players_ && (((seats & here) >> offer->player) & 1u))
            frames_[static_cast<size_t>(offer->player)].choice = b.choice(w, offer->player);
    }
    run_.focusZone(-1);
}

void TestSession::rescue() {
    // 2.79: a simulated player is never moved by the test any more (it would be a cheat: only a player's
    // inputs move it, docs/RAIDS.md « Joueurs simulés : terrain et anticipation »). What is counted is how
    // often one stayed stuck — sent to a mechanism it has not reached in 6 s, or no nearer to its goal for
    // 8 s —; its navigation looks for another way meanwhile (an edge it failed is left for a while).
    for (int s = 0; s < players_; ++s) {
        GameWorld* wp = run_.worldOf(run_.zoneOf(s));
        if (!wp || s >= wp->teamSize()) continue;
        GameWorld& w = *wp;
        TestBots::Bot& b = botsFor(run_.zoneOf(s)).bots()[static_cast<size_t>(s)];
        if (s == 0 && !seatZeroBot()) continue;  // the administrator plays their seat (or it is free)
        if ((humans_ >> s) & 1u) continue;       // 2.80: a real player plays it
        if (w.member(s).presence != GameWorld::Presence::Playing || !w.playerAt(s).alive() || w.playerAt(s).respawning()) continue;
        if (b.task == TestBots::Task::Mechanism && b.mechanismSteps > kMechanismSteps) {
            b.mechanismSteps = 0;
            b.path.clear();
            ++rescues_;
            continue;
        }
        if (b.lost > TestBots::kStuckSteps) {
            b.lost = 0;
            b.goalBest = 1e30f;
            b.path.clear();
            ++rescues_;
        }
    }
}

}  // namespace cosmic::raid
