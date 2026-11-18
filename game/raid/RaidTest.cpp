#include "game/raid/RaidTest.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/PhysicsWorld.h"
#include "game/enemies/Enemy.h"
#include "game/gameplay/entities/Entities.h"
#include "game/raid/GigabossRoutine.h"

namespace cosmic::raid {

namespace {

constexpr int kLeft = static_cast<int>(Action::MoveLeft);
constexpr int kRight = static_cast<int>(Action::MoveRight);
constexpr int kUp = static_cast<int>(Action::MoveUp);
constexpr int kDown = static_cast<int>(Action::MoveDown);
constexpr int kJump = static_cast<int>(Action::Jump);
constexpr int kAttack = static_cast<int>(Action::Attack);

uint64_t mix(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 1099511628211ULL;
    }
    return h;
}

bool playing(const GameWorld& w, int i) {
    return i >= 0 && i < w.teamSize() && w.member(i).presence == GameWorld::Presence::Playing && w.playerAt(i).alive() &&
           !w.playerAt(i).respawning();
}

}  // namespace

// ------------------------------------------------------------------------ the simulated players

void TestBots::reset(int players) {
    players_ = std::clamp(players, 1, kMaxInstancePlayers);
    bots_.assign(static_cast<size_t>(kMaxInstancePlayers), Bot{});
}

uint64_t TestBots::hash() const {
    uint64_t h = 1469598103934665603ULL;
    for (const Bot& b : bots_) {
        h = mix(h, b.prevHeld);
        h = mix(h, static_cast<uint64_t>(b.jumpHold) | (static_cast<uint64_t>(b.airJump) << 16) | (static_cast<uint64_t>(b.task) << 24));
        h = mix(h, static_cast<uint64_t>(b.still) | (static_cast<uint64_t>(b.mechanismSteps) << 20));
    }
    return h;
}

void TestBots::think(RaidRun& run, InputFrame* frames, int count) {
    GameWorld& w = run.world();
    const int n = std::min({players_, count, w.teamSize()});
    // The tasks of the step, the same order on every run: first the players down (the nearest
    // simulated player goes to each soul), then the mechanisms the administrator stands on, then
    // the dangers, the enemies near each one, and following the administrator.
    for (int s = 1; s < n; ++s) {
        Bot& b = bots_[static_cast<size_t>(s)];
        b.task = Task::Follow;
        b.mechanism = {};
    }
    std::vector<bool> busy(static_cast<size_t>(kMaxInstancePlayers), false);
    if (w.revivesLeft() > 0)
        for (int d = 0; d < w.teamSize(); ++d) {
            if (w.member(d).presence != GameWorld::Presence::Down) continue;
            const Vec2 soul = w.member(d).soul;
            int best = -1;
            float bestD = 1e30f;
            for (int s = 1; s < n; ++s) {
                if (busy[static_cast<size_t>(s)] || !playing(w, s)) continue;
                const float dd = (w.playerAt(s).feet() - soul).lengthSq();
                if (dd < bestD) {
                    bestD = dd;
                    best = s;
                }
            }
            if (best < 0) continue;
            Bot& b = bots_[static_cast<size_t>(best)];
            b.task = Task::Revive;
            b.goal = soul;
            busy[static_cast<size_t>(best)] = true;
        }
    // The mechanism the administrator stands on: a co-op plate (and the others of its group), or a
    // vent of the gigaboss (the other open vents).
    std::vector<Rect> places;
    std::vector<int> need;
    if (playing(w, 0)) {
        const Rect admin = w.playerAt(0).box;
        const ZoneRules& rules = run.rules();
        for (size_t k = 0; k < rules.coops.size(); ++k) {
            if (!rules.coops[k].rect.intersects(admin) || run.coop(k).done) continue;
            for (size_t g = 0; g < rules.coops.size(); ++g) {
                const bool same = g == k || (!rules.coops[k].group.empty() && rules.coops[g].group == rules.coops[k].group);
                if (!same) continue;
                const RaidRun::CoopView v = run.coop(g);
                // The administrator counts on their own plate; the others need all of theirs.
                const int missing = g == k ? std::max(0, v.need - 1) : std::max(0, v.need);
                places.push_back(rules.coops[g].rect);
                need.push_back(missing);
            }
            break;
        }
        const Gigaboss& gb = run.gigaboss();
        // A gigaboss's own mechanism (the plates of its routine: hooks, generators…): the others.
        if (places.empty() && gb.present())
            if (const GigabossRoutine* rt = gb.routine()) {
                std::vector<BossMark> marks;
                rt->marks(w, gb, marks);
                std::vector<Rect> plates;
                for (const BossMark& m : marks)
                    if (m.kind == BossMark::Kind::Plate) plates.push_back(m.area);
                for (size_t k = 0; k < plates.size(); ++k)
                    if (plates[k].expanded(4.0f).intersects(admin)) {
                        for (size_t o = 0; o < plates.size(); ++o)
                            if (o != k) {
                                places.push_back(plates[o]);
                                need.push_back(1);
                            }
                        break;
                    }
            }
        if (places.empty() && gb.present() && gb.stage() == Gigaboss::Stage::Carapace)
            for (size_t k = 0; k < gb.vents().size(); ++k)
                if (gb.vents()[k].area.expanded(4.0f).intersects(admin) && !gb.vents()[k].closed) {
                    for (size_t o = 0; o < gb.vents().size(); ++o)
                        if (o != k && !gb.vents()[o].closed) {
                            places.push_back(gb.vents()[o].area);
                            need.push_back(1);
                        }
                    break;
                }
    }
    for (size_t p = 0; p < places.size(); ++p) {
        int slot = 0;
        for (int s = 1; s < n && need[p] > 0; ++s) {
            if (busy[static_cast<size_t>(s)] || !playing(w, s)) continue;
            Bot& b = bots_[static_cast<size_t>(s)];
            b.task = Task::Mechanism;
            b.mechanism = places[p];
            // Spread over the plate (never all on one pixel), feet on its floor.
            const float span = std::max(0.0f, places[p].w - 16.0f);
            const float off = span <= 0.0f ? 0.0f : std::fmod(static_cast<float>(slot) * 13.0f + 6.0f, span) - span * 0.5f;
            b.goal = {places[p].center().x + off, places[p].bottom()};
            busy[static_cast<size_t>(s)] = true;
            --need[p];
            ++slot;
        }
    }
    for (int s = 1; s < n; ++s) {
        Bot& b = bots_[static_cast<size_t>(s)];
        if (b.task != Task::Mechanism) b.mechanismSteps = 0;
        else if (playing(w, s) && b.mechanism.intersects(w.playerAt(s).box)) b.mechanismSteps = 0;
        else ++b.mechanismSteps;
    }
    // What is announced to strike this step (a simulated player keeps out of it): the fists marked
    // under the players, the vents about to throw their jet, a gigaboss's blows and dangers announced.
    dangers_.clear();
    const Gigaboss& gb = run.gigaboss();
    if (gb.present()) {
        for (const auto& f : gb.fists())
            if (f.left > 0) dangers_.push_back(gb.fistColumn(f));
        for (int k = 0; k < static_cast<int>(gb.vents().size()); ++k)
            if (gb.jetWarning(k) || gb.jetting(k)) {
                const Rect v = gb.vents()[static_cast<size_t>(k)].area;
                const float h = gb.rules() ? gb.rules()->jetHeight : 112.0f;
                dangers_.push_back({v.x, v.y - h, v.w, h + v.h});
            }
        if (const GigabossRoutine* rt = gb.routine()) {
            std::vector<BossMark> marks;
            rt->marks(w, gb, marks);
            for (const BossMark& m : marks)
                if (m.kind == BossMark::Kind::Target || (m.kind == BossMark::Kind::Zone && (m.strong || m.progress > 0.0f)))
                    dangers_.push_back(m.area);
        }
    }
    for (int s = 1; s < n; ++s) frames[s] = drive(w, s, bots_[static_cast<size_t>(s)]);
}

InputFrame TestBots::drive(const GameWorld& w, int seat, Bot& b) {
    InputFrame f;
    const GameWorld::TeamMember& m = w.member(seat);
    if (m.presence != GameWorld::Presence::Playing) {
        b.prevHeld = 0;
        b.jumpHold = 0;
        return f;  // down (its soul waits), or not in play
    }
    const Player& me = w.playerAt(seat);
    const Vec2 feet = me.feet();
    const Player& admin = w.playerAt(0);
    const bool adminIn = w.member(0).presence == GameWorld::Presence::Playing;
    float stop = 18.0f;
    if (b.task == Task::Revive || b.task == Task::Mechanism) stop = 6.0f;
    if (b.task == Task::Follow) {
        // Beside the administrator, each simulated player at its own distance and side.
        const float side = (seat % 2) != 0 ? -1.0f : 1.0f;
        const float dist = 30.0f + 22.0f * static_cast<float>((seat - 1) / 2);
        b.goal = adminIn ? Vec2{admin.feet().x + side * dist, admin.feet().y} : feet;
        // An enemy near: strike it (from its side, facing it).
        const Enemy* target = nullptr;
        float best = 1e30f;
        for (const auto& e : w.enemies()) {
            if (!e->alive() || e->dormant()) continue;
            const Vec2 d = e->center() - me.center();
            if (std::fabs(d.x) > 220.0f || std::fabs(d.y) > 90.0f) continue;
            if (!w.physics().lineOfSight(me.center(), e->center())) continue;
            if (d.lengthSq() < best) {
                best = d.lengthSq();
                target = e.get();
            }
        }
        if (target) {
            b.task = Task::Fight;
            const float from = target->center().x < me.center().x ? 1.0f : -1.0f;
            b.goal = {target->center().x + from * (target->box.w * 0.5f + 14.0f), target->box.bottom()};
            stop = 6.0f;
        }
    }
    // Dangers first: the rising magma about to come (or there), a fist marked on it.
    float dodge = 0.0f;
    for (const auto& e : w.entities())
        if (auto* dz = dynamic_cast<const DamageZoneEntity*>(e.get()); dz && dz->magma() && (dz->warning() || dz->magmaLevel() > 0.0f)) {
            const Rect zone = dz->object().rect;
            if (feet.x > zone.x - 6.0f && feet.x < zone.right() + 6.0f && feet.y > zone.y - 2.0f) {
                // Out of it: toward the administrator when they are safe, else the nearest edge.
                const bool adminSafe = adminIn && !(admin.feet().x > zone.x && admin.feet().x < zone.right() && admin.feet().y > zone.y - 2.0f);
                const float toLeft = feet.x - zone.x, toRight = zone.right() - feet.x;
                const float exitX = adminSafe ? admin.feet().x : (toLeft < toRight ? zone.x - 24.0f : zone.right() + 24.0f);
                dodge = exitX - feet.x;
                b.goal = {exitX, adminSafe ? admin.feet().y : zone.y};
            }
        }
    // A blow announced on it (a fist, a jet, a gigaboss's mark): out of it by the nearest side (the
    // players holding a mechanism stay: it is the administrator's call).
    if (b.task != Task::Revive && b.task != Task::Mechanism)
        for (const Rect& d : dangers_)
            if (d.expanded(6.0f).intersects(me.box)) {
                const float left = me.center().x - d.x, right = d.right() - me.center().x;
                b.goal = {left < right ? d.x - 22.0f : d.right() + 22.0f, feet.y};
                dodge = b.goal.x - feet.x;
                break;
            }
    if (dodge != 0.0f && b.task != Task::Revive) {
        b.task = Task::Dodge;
        stop = 6.0f;
    }
    // Moving toward the goal.
    const float dx = b.goal.x - feet.x;
    const int dir = std::fabs(dx) > stop ? (dx > 0.0f ? 1 : -1) : 0;
    uint32_t held = 0;
    if (dir > 0) held |= 1u << kRight;
    if (dir < 0) held |= 1u << kLeft;
    f.moveX = static_cast<int8_t>(dir * 127);
    // Stuck against something while wanting to move: jump.
    if (dir != 0 && std::fabs(me.box.x - b.lastX) < 0.25f) ++b.still;
    else b.still = 0;
    b.lastX = me.box.x;
    MoveOptions opt;
    bool wantJump = false;
    const float up = feet.y - b.goal.y;  // > 0: the goal is higher
    if (me.grounded()) {
        b.airJump = false;
        if (dir != 0) {
            Rect ahead = me.box;
            ahead.x += static_cast<float>(dir) * 6.0f;
            ahead.y -= 2.0f;
            const bool wall = w.physics().overlapsSolid(ahead, opt);
            // A gap: nothing to stand on within 120 px ahead (a step down is just walked off).
            const Vec2 probe{feet.x + static_cast<float>(dir) * 16.0f, feet.y - 2.0f};
            const bool gap = !w.physics().raycast(probe, {probe.x, probe.y + 120.0f}, true).has_value();
            if (wall || gap || b.still > 18) wantJump = true;
        }
        if (up > 28.0f && std::fabs(dx) < 150.0f) wantJump = true;
        // The goal below, close: through the one-way floor.
        const Collider* ground = w.physics().groundBelow(me.box, opt);
        if (up < -40.0f && std::fabs(dx) < 70.0f && ground && ground->kind == ColliderKind::OneWay) {
            held |= 1u << kDown;
            f.moveY = 127;
            if (!(b.prevHeld & (1u << kJump))) held |= 1u << kJump;
        }
    } else if (me.vel.y > -40.0f && !b.airJump && (up > 20.0f || b.still > 6)) {
        // Near the top of the jump, still under the goal or against a wall: the air jump.
        b.airJump = true;
        b.jumpHold = 0;
        if (!(b.prevHeld & (1u << kJump))) {
            held |= 1u << kJump;
            b.jumpHold = 14;
        }
    }
    if (wantJump && b.jumpHold <= 0 && !(b.prevHeld & (1u << kJump))) b.jumpHold = 18;
    if (b.jumpHold > 0) {
        held |= 1u << kJump;
        --b.jumpHold;
    }
    // A ladder: climb toward the goal.
    if (me.onLadder && std::fabs(up) > 12.0f) {
        held |= 1u << (up > 0.0f ? kUp : kDown);
        f.moveY = static_cast<int8_t>(up > 0.0f ? -127 : 127);
    }
    // Strike the enemy it faces.
    if (b.attackCool > 0) --b.attackCool;
    if (b.task == Task::Fight && b.attackCool <= 0 && std::fabs(dx) < 30.0f) {
        held |= 1u << kAttack;
        b.attackCool = 22;
    }
    f.held = held;
    f.pressed = held & ~b.prevHeld;
    f.released = b.prevHeld & ~held;
    b.prevHeld = held;
    if (const auto& offer = w.powerOffer(); offer && offer->player == seat) f.choice = InputFrame::kChoiceLeave;
    return f;
}

// --------------------------------------------------------------------------------- the session

TestSession::TestSession(WorldServices services, const Branch& branch, const RaidRun::Test& setup)
    : run_(services, branch, kMaxInstancePlayers) {
    RaidRun::Test t = setup;
    t.on = true;
    run_.setTest(t);
    players_ = run_.testSetup().players;
    bots_.reset(players_);
    far_.assign(static_cast<size_t>(kMaxInstancePlayers), 0);
}

bool TestSession::begin(int zone) { return run_.begin(state_, zone); }

std::string TestSession::botName(int seat) { return "Joueur simulé " + std::to_string(seat + 1); }

uint64_t TestSession::hash() const { return mix(mix(run_.hash(), bots_.hash()), state_); }

void TestSession::step(const InputFrame& admin, const std::vector<std::pair<int, InputFrame>>& scripted) {
    frames_.assign(static_cast<size_t>(kMaxInstancePlayers), InputFrame{});
    frames_[0] = admin;
    bots_.think(run_, frames_.data(), static_cast<int>(frames_.size()));
    for (int s = players_; s < kMaxInstancePlayers; ++s) frames_[static_cast<size_t>(s)].flags = InputFrame::Vacant;
    for (const auto& [seat, f] : scripted)  // a capture's script plays its seat
        if (seat > 0 && seat < kMaxInstancePlayers) frames_[static_cast<size_t>(seat)] = f;
    run_.step(frames_.data(), static_cast<int>(frames_.size()), state_);
    // What the group did is accepted at once, as the server would after the consensus of a real
    // instance: given back with the next step (RAID_STATE's structure, the objectives' bits).
    state_ |= run_.localDone();
    regroup();
    run_.takeReports();  // empty: a test never reports anything
}

void TestSession::regroup() {
    GameWorld& w = run_.world();
    if (!playing(w, 0)) return;
    const Player& admin = w.playerAt(0);
    for (int s = 1; s < players_ && s < w.teamSize(); ++s) {
        int& far = far_[static_cast<size_t>(s)];
        TestBots::Bot& b = bots_.bots()[static_cast<size_t>(s)];
        if (!playing(w, s)) {
            far = 0;
            continue;
        }
        // Sent to a mechanism it cannot reach: put on it (a test alone needs its helpers there).
        if (b.task == TestBots::Task::Mechanism && b.mechanismSteps > kMechanismSteps) {
            w.restoreMember(s, w.carryOf(s), w.freeSpot({b.mechanism.center().x, b.mechanism.bottom()}, s));
            b.mechanismSteps = 0;
            far = 0;
            continue;
        }
        const Vec2 d = w.playerAt(s).feet() - admin.feet();
        far = std::fabs(d.x) > 640.0f || std::fabs(d.y) > 360.0f ? far + 1 : 0;
        if (far > kRegroupSteps && admin.grounded()) {
            w.restoreMember(s, w.carryOf(s), w.freeSpot(admin.feet(), s));
            far = 0;
        }
    }
}

}  // namespace cosmic::raid
