#include "game/raid/RaidTest.h"

#include <algorithm>
#include <cmath>

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
    bots_.reset(players_);
}

bool TestSession::begin(int zone) { return run_.begin(state_, zone); }

std::string TestSession::botName(int seat) { return "Joueur simulé " + std::to_string(seat + 1); }

uint64_t TestSession::hash() const { return mix(mix(run_.hash(), bots_.hash()), state_); }

void TestSession::step(const InputFrame& admin, const std::vector<std::pair<int, InputFrame>>& scripted) {
    frames_.assign(static_cast<size_t>(kMaxInstancePlayers), InputFrame{});
    // 2.77: the administrator AFK (no key held for 15 s): the AI plays their seat until they touch one.
    if (adminSeated_) afk_ = admin.held != 0 || admin.pressed != 0 || admin.moveX != 0 || admin.moveY != 0 || admin.choice != 0 ? 0 : std::min(afk_ + 1, kAfkSteps + 1);
    else afk_ = 0;
    uint32_t seats = 0;
    for (int s = 1; s < players_; ++s) seats |= 1u << s;
    if (autopilot()) seats |= 1u;
    frames_[0] = admin;
    bots_.think(run_, frames_.data(), static_cast<int>(frames_.size()), seats, adminSeated_ && !autopilot());
    if (!adminSeated_) {
        frames_[0] = InputFrame{};
        frames_[0].flags = InputFrame::Vacant;  // the free place of a group
    }
    for (int s = players_; s < kMaxInstancePlayers; ++s) frames_[static_cast<size_t>(s)].flags = InputFrame::Vacant;
    // 2.75: a power offered to a simulated player (the armory's tablets): its answer, through its frame.
    if (const auto& offer = run_.world().powerOffer(); offer && offer->player >= 0 && offer->player < players_ && ((seats >> offer->player) & 1u))
        frames_[static_cast<size_t>(offer->player)].choice = bots_.choice(run_.world(), offer->player);
    for (const auto& [seat, f] : scripted)  // a capture's script plays its seat
        if (seat > 0 && seat < kMaxInstancePlayers) frames_[static_cast<size_t>(seat)] = f;
    run_.step(frames_.data(), static_cast<int>(frames_.size()), state_, heart_);
    // What the group did is accepted at once, as the server would after the consensus of a real
    // instance: given back with the next step (RAID_STATE's structure, the objectives' bits).
    state_ |= run_.localDone();
    if (botsInvincible_) {
        GameWorld& w = run_.world();
        for (int s = 1; s < players_ && s < w.teamSize(); ++s)
            if (w.member(s).presence == GameWorld::Presence::Playing && w.playerAt(s).alive()) w.playerAt(s).health = w.playerAt(s).maxHealth();
    }
    rescue();
    run_.takeReports();  // empty: a test never reports anything
}

void TestSession::rescue() {
    GameWorld& w = run_.world();
    const NavGraph* nav = bots_.nav();
    for (int s = 0; s < players_ && s < w.teamSize(); ++s) {
        TestBots::Bot& b = bots_.bots()[static_cast<size_t>(s)];
        if (s == 0 && !autopilot()) continue;  // the administrator plays their seat
        if (w.member(s).presence != GameWorld::Presence::Playing || !w.playerAt(s).alive() || w.playerAt(s).respawning()) continue;
        // Sent to a mechanism it cannot reach: put on it (a test alone needs its helpers there).
        if (b.task == TestBots::Task::Mechanism && b.mechanismSteps > kMechanismSteps) {
            w.restoreMember(s, w.carryOf(s), w.freeSpot({b.mechanism.center().x, b.mechanism.bottom()}, s));
            b.mechanismSteps = 0;
            b.path.clear();
            ++rescues_;
            continue;
        }
        // 2.77: no nearer to its goal for 8 s on its path: on the next place of that path.
        if (b.lost > TestBots::kStuckSteps && nav) {
            const int next = bots_.nextNode(s);
            b.lost = 0;
            b.goalBest = 1e30f;
            if (next < 0 || nav->node(next).mover >= 0) continue;
            w.restoreMember(s, w.carryOf(s), w.freeSpot(nav->node(next).feet, s));
            b.path.clear();
            ++rescues_;
        }
    }
}

}  // namespace cosmic::raid
