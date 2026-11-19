// 2.62: cooperation on the EXTRÊME levels, 2 or 3 players in one world (docs/COOP.md).
// Everything here only runs for a team (GameWorld::setTeamSize > 1): a solo world never
// reaches it and plays exactly as before.

#include <algorithm>
#include <cmath>

#include "engine/physics/Collision.h"
#include "engine/resources/ResourceManager.h"
#include "game/content/ContentDatabase.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/entities/Entities.h"

namespace cosmic {

void GameWorld::setTeamSize(int n) { teamWanted_ = std::clamp(n, 1, kMaxTeam); }

bool GameWorld::teamIncomplete() const {
    return std::any_of(team_.begin(), team_.end(), [](const TeamMember& m) { return m.presence == Presence::Gone; });
}

int GameWorld::playerIndex(const Player& p) const {
    for (size_t i = 0; i < team_.size(); ++i)
        if (team_[i].player == &p) return static_cast<int>(i);
    return 0;
}

bool GameWorld::solidMember(int i) const {
    const TeamMember& m = team_[static_cast<size_t>(i)];
    if (m.presence != Presence::Playing) return false;  // down (its soul), standby, gone: not solid
    const PlayerState s = m.player->state();
    return s != PlayerState::Dead && s != PlayerState::Hidden;
}

bool GameWorld::teammateBody(uint32_t collider) const {
    if (collider == 0) return false;
    return std::any_of(team_.begin(), team_.end(), [&](const TeamMember& m) { return m.body == collider; });
}

int GameWorld::targetIndex(Vec2 from) const {
    if (team_.empty()) return 0;
    // The nearest player in play: alive first, then materialising, then falling; ties to the
    // lowest number (strict comparisons in index order).
    int best = -1, bestTier = 99;
    float bestD = 0.0f;
    for (size_t i = 0; i < team_.size(); ++i) {
        const TeamMember& m = team_[i];
        if (m.presence != Presence::Playing) continue;
        const Player& p = *m.player;
        int tier = p.alive() ? (p.respawning() ? 1 : 0) : (p.state() == PlayerState::Dead ? 2 : 3);
        if (tier >= 3) continue;
        const Vec2 d = p.center() - from;
        const float d2 = d.x * d.x + d.y * d.y;
        if (best < 0 || tier < bestTier || (tier == bestTier && d2 < bestD)) {
            best = static_cast<int>(i);
            bestTier = tier;
            bestD = d2;
        }
    }
    return best < 0 ? 0 : best;
}

Player* GameWorld::shotTarget(const Projectile& p) {
    if (team_.empty())
        return player_.alive() && Collision::circleIntersectsRect(p.pos, p.radius, player_.box) ? &player_ : nullptr;
    for (const TeamMember& m : team_) {
        if (m.presence != Presence::Playing || !m.player->alive()) continue;
        if (Collision::circleIntersectsRect(p.pos, p.radius, m.player->box)) return m.player;
    }
    return nullptr;
}

Vec2 GameWorld::freeSpot(Vec2 feet, int self) const {
    const Vec2 size = player_.config().size;
    const float step = size.x + 4.0f;
    MoveOptions scenery;
    scenery.ignoreTags = ColliderTag::Teammate;
    auto blocked = [&](const Rect& r) {
        if (physics_.overlapsSolid(r, scenery)) return true;
        for (size_t j = 0; j < team_.size(); ++j) {
            if (static_cast<int>(j) == self || !solidMember(static_cast<int>(j))) continue;
            if (team_[j].player->box.intersects(r)) return true;
        }
        return false;
    };
    // From the wanted place, then step by step to the right and to the left: the first place
    // free of scenery and allies with ground under the feet; else the first free one.
    bool haveAir = false;
    Vec2 air;
    for (int k = 0; k <= 24; ++k) {
        const float side = k % 2 == 1 ? 1.0f : -1.0f;
        const float dx = k == 0 ? 0.0f : side * step * static_cast<float>((k + 1) / 2);
        const Vec2 at{feet.x + dx, feet.y};
        const Rect r = Rect::fromBottomCenter(at, size);
        if (r.x < level_.bounds.x || r.right() > level_.bounds.right()) continue;
        if (blocked(r)) continue;
        if (physics_.groundBelow(r, scenery)) return at;
        if (!haveAir) {
            haveAir = true;
            air = at;
        }
    }
    return haveAir ? air : feet;
}

void GameWorld::loadTeam() {
    const int n = std::clamp(teamWanted_, 2, kMaxTeam);
    PlayerConfig cfg = PlayerConfig::fromJson(services_.content.playerConfig());
    team_.assign(static_cast<size_t>(n), TeamMember{});
    team_[0].player = &player_;
    for (int i = 1; i < n; ++i) {
        auto p = std::make_unique<Player>();
        p->configure(cfg, services_.res.animation("animations/nova.json"));
        p->setKeysPerSlot(keysPerSlot_);
        for (const auto& pid : level_.startPowers)
            if (const PowerDefinition* d = services_.content.power(pid)) p->grantPower(*d);
        p->setPowerSlots(level_.powerSlots());
        p->setHidden(true);  // not solid until placed
        team_[static_cast<size_t>(i)].player = p.get();
        extraPlayers_.push_back(std::move(p));
    }
    // Side by side from the level's start, in order (never one inside another).
    for (int i = 1; i < n; ++i) team_[static_cast<size_t>(i)].player->spawn(*this, freeSpot(respawn_, i), respawnFaceLeft_, false);
    for (auto& m : team_) {
        // The body: a solid for the other players only, switched on during their steps.
        m.body = physics_.add(m.player->box, ColliderKind::Solid, ColliderTag::Teammate, m.player, true);
        physics_.setEnabled(m.body, false);
        m.safeFeet = m.player->feet();
        m.hasSafeFeet = true;
    }
}

void GameWorld::teamBodies(int self, bool on) {
    // 2.75: the players pass through each other (a raid; a team from cooperation version 2).
    if (!solidTeammates()) on = false;
    for (size_t j = 0; j < team_.size(); ++j) {
        const TeamMember& m = team_[j];
        if (on && static_cast<int>(j) != self && solidMember(static_cast<int>(j))) {
            // Where it stands now, at rest: an ally never carries nor pushes anybody.
            physics_.setDynamicBox(m.body, m.player->box, {0.0f, 0.0f});
            physics_.setEnabled(m.body, true);
        } else {
            physics_.setEnabled(m.body, false);
        }
    }
}

void GameWorld::fixedUpdateTeam(const PlayerInput* inputs, const bool* standby, int n, float dt, const bool* vacant) {
    if (team_.empty()) {
        fixedUpdate(n > 0 ? inputs[0] : PlayerInput{}, dt);
        return;
    }
    teamInputs_.assign(team_.size(), PlayerInput{});
    for (size_t i = 0; i < team_.size(); ++i) {
        const bool have = static_cast<int>(i) < n;
        team_[i].standbyInput = have ? standby[i] : true;  // no input at all: absent
        // 2.65 (raids): a seat nobody holds; never in a cooperation world (the relay sends none).
        team_[i].vacantInput = raidMode_ && (have ? vacant != nullptr && vacant[i] : true);
        if (have && !standby[i]) teamInputs_[i] = inputs[i];
    }
    fixedUpdate(teamInputs_[0], dt);
}

void GameWorld::stepTeamPlayers(float dt) {
    const bool control = state_ == WorldState::Playing && !cinematic_.active();
    for (size_t i = 0; i < team_.size(); ++i) {
        TeamMember& m = team_[i];
        Player& p = *m.player;
        PlayerScope scope(*this, static_cast<int>(i));
        p.onLadder = false;
        p.inWater = false;
        p.speedMultiplier = 1.0f;
        p.updraft = 0.0f;
        {
            // A player out of play is no longer inside anything: its « inside » bits go.
            HurtScope traps(*this, HurtOrigin::Hazard);
            updatePlayerOverlaps(dt);
        }
        if (m.presence != Presence::Playing) continue;
        teamBodies(static_cast<int>(i), true);  // the others' bodies where they stand now
        p.fixedUpdate(*this, control && i < teamInputs_.size() ? teamInputs_[i] : PlayerInput{}, dt);
        // Its last solid ground (an ally's head is no ground to come back to). 2.77 (rules 3): a SAFE
        // place only — firm ground (no moving or crumbling platform), out of every liquid and danger.
        if (m.presence == Presence::Playing && p.grounded() && p.state() != PlayerState::Dead &&
            p.state() != PlayerState::Respawn && p.state() != PlayerState::Hidden && p.vel.y >= 0.0f &&
            !teammateBody(p.groundCollider()) && (coopVersion_ < 3 || safeStand(p))) {
            m.safeFeet = p.feet();
            m.hasSafeFeet = true;
        }
    }
    teamBodies(-1, false);
}

void GameWorld::vacateMember(int i) {
    // 2.65 (raids): the seat is free; whoever takes it next starts fresh (the zone's start powers,
    // every heart).
    TeamMember& m = team_[static_cast<size_t>(i)];
    Player& p = *m.player;
    m.presence = Presence::Vacant;
    m.absent = false;
    m.standbyTicks = 0;
    m.revive = 0.0f;
    m.offFrame = 0.0f;
    p.setHidden(true);
    p.clearPowers();
    for (const auto& pid : level_.startPowers)
        if (const PowerDefinition* d = services_.content.power(pid)) p.grantPower(*d);
    p.health = p.maxHealth();
}

void GameWorld::updateTeamStandby() {
    for (size_t i = 0; i < team_.size(); ++i) {
        TeamMember& m = team_[i];
        Player& p = *m.player;
        if (m.vacantInput) {
            if (m.presence != Presence::Vacant) vacateMember(static_cast<int>(i));
            continue;
        }
        if (m.presence == Presence::Vacant) {
            if (m.standbyInput) continue;  // taken, not caught up yet
            // Joined: near the nearest player in play, else at the zone's start, every heart.
            int toward = -1;
            float best = 0.0f;
            for (size_t j = 0; j < team_.size(); ++j) {
                if (j == i || !solidMember(static_cast<int>(j))) continue;
                const Vec2 d = team_[j].player->center() - p.center();
                const float d2 = d.x * d.x + d.y * d.y;
                if (toward < 0 || d2 < best) {
                    toward = static_cast<int>(j);
                    best = d2;
                }
            }
            const Vec2 at = toward >= 0 ? (team_[static_cast<size_t>(toward)].hasSafeFeet ? team_[static_cast<size_t>(toward)].safeFeet
                                                                                          : team_[static_cast<size_t>(toward)].player->feet())
                                        : respawn_;
            m.presence = Presence::Playing;
            PlayerScope scope(*this, static_cast<int>(i));
            p.spawn(*this, freeSpot(at, static_cast<int>(i)), false, true);
            p.setInvulnerable(1.75f);
            m.safeFeet = p.feet();
            m.hasSafeFeet = true;
            playSound("sounds/respawn.wav", p.center(), 0.7f);
            continue;
        }
        if (m.presence == Presence::Gone) continue;
        if (m.standbyInput) {
            if (!m.absent) {
                m.absent = true;
                m.standbyTicks = 0;
            }
            if (++m.standbyTicks > kStandbyLimitTicks) {
                // 60 s without coming back: gone for good, the team goes on « incomplète ».
                m.presence = Presence::Gone;
                m.revive = 0.0f;
                p.setHidden(true);
                continue;
            }
            if (m.presence == Presence::Playing && p.state() != PlayerState::Dead) {
                // Standby: where it was, untouchable, not solid, ignored by everyone.
                m.presence = Presence::Standby;
                m.offFrame = 0.0f;
                p.setHidden(true);
            }
        } else if (m.absent) {
            m.absent = false;
            m.standbyTicks = 0;
            if (m.presence != Presence::Standby) continue;  // down: it stays down
            // Back: near the nearest ally in play, in a free spot.
            int toward = -1;
            float best = 0.0f;
            for (size_t j = 0; j < team_.size(); ++j) {
                if (j == i || !solidMember(static_cast<int>(j))) continue;
                const Vec2 d = team_[j].player->center() - p.center();
                const float d2 = d.x * d.x + d.y * d.y;
                if (toward < 0 || d2 < best) {
                    toward = static_cast<int>(j);
                    best = d2;
                }
            }
            if (toward < 0 && !raidMode_) continue;  // nobody in play: the attempt is over anyway
            m.presence = Presence::Playing;
            ++m.returned;
            PlayerScope scope(*this, static_cast<int>(i));
            // 2.75 (raids): nobody in play (the others down): back where it stopped, to revive them.
            const Vec2 back = toward < 0 ? (m.hasSafeFeet ? m.safeFeet : p.feet())
                                         : (team_[static_cast<size_t>(toward)].hasSafeFeet ? team_[static_cast<size_t>(toward)].safeFeet
                                                                                           : team_[static_cast<size_t>(toward)].player->feet());
            p.spawn(*this, freeSpot(back, static_cast<int>(i)), p.facing < 0.0f, true);
            p.setInvulnerable(1.75f);
            m.safeFeet = p.feet();
            m.hasSafeFeet = true;
            playSound("sounds/respawn.wav", p.center(), 0.7f);
        }
    }
    checkTeamDown();
}

void GameWorld::memberDown(int i) {
    TeamMember& m = team_[static_cast<size_t>(i)];
    Player& p = *m.player;
    if (m.presence == Presence::Gone) return;
    m.presence = Presence::Down;
    // Its soul (rules 1 and 2): where it fell, or on its last solid ground after a fall into the void.
    // 2.77 (rules of cooperation 3): the soul FALLS straight down from where its player died (in a jump, hit
    // in the air…), like a dropped object, to the first firm ground under it, where an ally can walk to it.
    // It goes to the player's last SAFE place instead (firm ground out of danger, kept step by step) when
    // under that point there is the void or the level's way out, a deadly liquid, spikes or another
    // danger, ground more than 6 tiles down, ground that moves, crumbles, breaks or opens, a door, or no
    // room to stand: a player who dies anywhere can always be reached and revived.
    const auto rest = [&]() -> Vec2 {
        const Vec2 from = p.feet();
        const Vec2 safe = m.hasSafeFeet ? m.safeFeet : from;
        constexpr float kMaxDrop = 6.0f * 16.0f;  // 6 tiles
        const Collider* ground = nullptr;
        physics_.forEachIn({from.x - 0.5f, from.y - 1.0f, 1.0f, kMaxDrop + 2.0f}, [&](const Collider& c) {
            if (!c.enabled || (c.tags & (ColliderTag::Teammate | ColliderTag::EnemyOnly))) return;
            // Above its feet (a ceiling, or what crushed it) or beside it: not under it.
            if (c.box.y < from.y - 0.5f || from.x < c.box.x || from.x > c.box.right()) return;
            if (!ground || c.box.y < ground->box.y) ground = &c;
        });
        constexpr uint32_t kNotFirm = ColliderTag::Moving | ColliderTag::Crumble | ColliderTag::Door | ColliderTag::Gate | ColliderTag::Breakable |
                                      ColliderTag::Bounce | ColliderTag::ShieldPass;
        if (!ground || ground->dynamic || (ground->tags & kNotFirm) || ground->box.y - from.y > kMaxDrop ||
            ground->box.y > level_.bounds.bottom() || ground->box.y >= level_.effectiveKillY())
            return safe;
        const Vec2 at{from.x, ground->box.y};
        const Rect body{at.x - p.box.w * 0.5f, at.y - p.box.h, p.box.w, p.box.h - 0.5f};
        const Rect fall{at.x - 5.0f, std::min(from.y, at.y) - 22.0f, 10.0f, std::abs(at.y - from.y) + 21.5f};  // its way down
        if (inDanger(fall, true)) return safe;
        MoveOptions mo;
        mo.ignoreTags = ColliderTag::Teammate;
        if (physics_.overlapsSolid(body, mo)) return safe;
        for (const auto& e : entities_)
            if (!e->removed && dynamic_cast<const DoorEntity*>(e.get()) && e->box.intersects(body)) return safe;
        return at;
    };
    if (coopVersion_ >= 3) m.soul = rest();
    else m.soul = m.fell && m.hasSafeFeet ? m.safeFeet : p.feet();
    m.fell = false;
    m.revive = 0.0f;
    m.offFrame = 0.0f;
    p.setHidden(true);
    checkTeamDown();
}

void GameWorld::reviveMember(int i) {
    TeamMember& m = team_[static_cast<size_t>(i)];
    Player& p = *m.player;
    if (!revivesUnlimited()) --revivesLeft_;  // 2.75: a raid counts them without a limit
    ++m.revived;
    m.revive = 0.0f;
    if (m.absent) {
        // Revived while disconnected: it comes back in standby, where its soul was.
        m.presence = Presence::Standby;
        p.teleportTo(m.soul);
        return;
    }
    m.presence = Presence::Playing;
    PlayerScope scope(*this, i);
    p.spawn(*this, freeSpot(m.soul, i), p.facing < 0.0f, true);
    p.health = std::max(1, (p.maxHealth() + 1) / 2);
    p.setInvulnerable(kReviveSeconds + 0.75f);  // 2 s once materialised
    m.safeFeet = p.feet();
    m.hasSafeFeet = true;
    particles_.emit("shield_up", p.center(), 1.2f);
    playSound("sounds/respawn.wav", p.center(), 0.9f);
}

void GameWorld::regroupMember(int i, int toward) {
    TeamMember& m = team_[static_cast<size_t>(i)];
    const TeamMember& t = team_[static_cast<size_t>(toward)];
    Player& p = *m.player;
    particles_.emit("teleport_out", p.center(), 1.0f);
    p.teleportTo(freeSpot(t.hasSafeFeet ? t.safeFeet : t.player->feet(), i));
    p.vel = {0.0f, 0.0f};
    p.setInvulnerable(1.0f);
    m.offFrame = 0.0f;
    ++m.regrouped;
    particles_.emit("teleport_in", p.center(), 1.0f);
    playSound("sounds/teleport.wav", p.center(), 0.6f);
}

// 2.77 (rules of cooperation 3): the places a soul may wait on.
const std::vector<LevelEntity*>& GameWorld::harmful() const {
    if (harmfulFor_ != entities_.size() + (entityGen_ << 32)) {
        harmful_.clear();
        for (const auto& e : entities_)
            if (dynamic_cast<const DamageZoneEntity*>(e.get()) || dynamic_cast<const SpikesEntity*>(e.get()) ||
                (dynamic_cast<const LiquidEntity*>(e.get()) && !static_cast<const LiquidEntity*>(e.get())->swimmable()))
                harmful_.push_back(e.get());
        harmfulFor_ = entities_.size() + (entityGen_ << 32);
    }
    return harmful_;
}

bool GameWorld::inDanger(const Rect& body, bool whole) const {
    for (const LevelEntity* e : harmful()) {
        if (e->removed || !e->active) continue;
        // `whole`: the box a rising liquid fills at its highest (a place below it is not safe from it).
        const Rect r = whole && dynamic_cast<const DamageZoneEntity*>(e) ? e->box : e->overlapBox();
        if (r.h > 0.5f && r.intersects(body)) return true;
    }
    return false;
}

bool GameWorld::safeStand(const Player& p) const {
    const Collider* g = physics_.get(p.groundCollider());
    if (!g || g->dynamic ||
        (g->tags & (ColliderTag::Moving | ColliderTag::Crumble | ColliderTag::Teammate | ColliderTag::Door | ColliderTag::Gate | ColliderTag::Breakable)))
        return false;
    if (p.inWater) return false;
    return !inDanger(p.box.expanded(2.0f), false);
}

void GameWorld::liftSoul(TeamMember& m) {
    const Rect body{m.soul.x - 5.0f, m.soul.y - 22.0f, 10.0f, 21.5f};
    const LevelEntity* over = nullptr;
    for (const LevelEntity* e : harmful())
        if (!e->removed && e->active && e->overlapBox().h > 0.5f && e->overlapBox().intersects(body)) {
            over = e;
            break;
        }
    if (!over) return;
    // The nearest firm ground above the whole liquid (its highest level), with room and out of danger.
    const float limit = over->box.y - 2.0f;
    Vec2 best = m.soul;
    float bestD = -1.0f;
    for (const Collider& c : physics_.colliders()) {
        if (!c.enabled || c.dynamic || (c.tags & (ColliderTag::Moving | ColliderTag::Crumble | ColliderTag::Teammate | ColliderTag::EnemyOnly))) continue;
        if (c.box.y >= limit || c.box.w < 12.0f || c.box.y < level_.bounds.y + 24.0f) continue;
        const Vec2 at{std::clamp(m.soul.x, c.box.x + 6.0f, c.box.right() - 6.0f), c.box.y};
        const float d = (at - m.soul).lengthSq();
        if (bestD >= 0.0f && d >= bestD) continue;
        const Rect b{at.x - 5.0f, at.y - 22.0f, 10.0f, 21.5f};
        if (physics_.overlapsSolid(b) || inDanger(b, true)) continue;
        best = at;
        bestD = d;
    }
    if (bestD >= 0.0f) {
        m.soul = best;
        m.revive = 0.0f;
    }
}

void GameWorld::checkTeamDown() {
    if (state_ == WorldState::Failed || state_ == WorldState::Completed || state_ == WorldState::Outro) return;
    for (const TeamMember& m : team_)
        if (m.presence == Presence::Playing || (raidMode_ && m.presence == Presence::Standby)) return;  // 2.75: a raid waits for its absent
    failLevel("Toute l'équipe est à terre");
}

void GameWorld::updateTeamRules(float dt) {
    // Revivals: an ally in play staying near a soul (2 s without a break), 2 per attempt; 2.75: in
    // a raid, as many as wanted (while a teammate is in play to stay near the soul).
    for (size_t i = 0; i < team_.size(); ++i) {
        TeamMember& m = team_[i];
        if (m.presence != Presence::Down) continue;
        if (!canRevive()) {
            m.revive = 0.0f;
            continue;
        }
        // 2.77 (rules 3): a liquid rising over its soul lifts it to the nearest safe place above it.
        if (coopVersion_ >= 3) liftSoul(m);
        bool near = false;
        m.reviver = -1;
        for (size_t j = 0; j < team_.size() && !near; ++j) {
            if (j == i || !solidMember(static_cast<int>(j)) || team_[j].player->respawning()) continue;
            const Vec2 f = team_[j].player->feet();
            near = std::abs(f.x - m.soul.x) <= kReviveReachX && std::abs(f.y - m.soul.y) <= kReviveReachY;
            // 2.77 (rules 3): « Interagir » held near the soul (its frames: replays stay exact); let go,
            // the revival stops.
            if (near && coopVersion_ >= 3) near = j < teamInputs_.size() && teamInputs_[j].isHeld(Action::Interact);
            if (near) m.reviver = static_cast<int>(j);
        }
        m.revive = near ? m.revive + dt : 0.0f;
        if (m.revive >= kReviveSeconds) reviveMember(static_cast<int>(i));
    }

    if (raidMode_) {
        // 2.65 (raids): every player has their own camera; no team frame, nobody is brought back.
        for (auto& m : team_) {
            m.inFrame = m.presence == Presence::Playing;
            m.offFrame = 0.0f;
        }
        checkTeamDown();
        return;
    }
    // The team's frame: the largest group of players in play that fits in it (ties: the group
    // holding the lowest number); the others are out of it and come back after 3 s.
    int in[kMaxTeam];  // no allocation in the step
    int k = 0;
    for (size_t i = 0; i < team_.size(); ++i)
        if (solidMember(static_cast<int>(i))) in[k++] = static_cast<int>(i);
    for (auto& m : team_) m.inFrame = m.presence == Presence::Playing;
    if (k > 1) {
        int bestMask = 0, bestCount = 0;
        for (int mask = 1; mask < (1 << k); ++mask) {
            float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
            int count = 0;
            for (int b = 0; b < k; ++b) {
                if (!(mask & (1 << b))) continue;
                const Vec2 c = team_[static_cast<size_t>(in[b])].player->center();
                if (count++ == 0) {
                    x0 = x1 = c.x;
                    y0 = y1 = c.y;
                } else {
                    x0 = std::min(x0, c.x);
                    x1 = std::max(x1, c.x);
                    y0 = std::min(y0, c.y);
                    y1 = std::max(y1, c.y);
                }
            }
            if (x1 - x0 > kFrameWidth || y1 - y0 > kFrameHeight) continue;
            // Larger first; same size: the lexicographically smallest list of numbers, i.e.
            // the mask whose lowest differing bit is set.
            bool better = count > bestCount;
            if (!better && count == bestCount) {
                const int diff = mask ^ bestMask;
                better = diff != 0 && (mask & (diff & -diff)) != 0;
            }
            if (better) {
                bestMask = mask;
                bestCount = count;
            }
        }
        for (int b = 0; b < k; ++b) {
            TeamMember& m = team_[static_cast<size_t>(in[b])];
            m.inFrame = (bestMask & (1 << b)) != 0;
        }
        for (int b = 0; b < k; ++b) {
            const int i = in[b];
            TeamMember& m = team_[static_cast<size_t>(i)];
            if (m.inFrame) {
                m.offFrame = 0.0f;
                continue;
            }
            m.offFrame += dt;
            if (m.offFrame < kOffFrameSeconds) continue;
            // Back near the nearest player of the group (ties: the lowest number).
            int toward = -1;
            float best = 0.0f;
            for (int c = 0; c < k; ++c) {
                if (!(bestMask & (1 << c))) continue;
                const int j = in[c];
                const Vec2 d = team_[static_cast<size_t>(j)].player->center() - m.player->center();
                const float d2 = d.x * d.x + d.y * d.y;
                if (toward < 0 || d2 < best) {
                    toward = j;
                    best = d2;
                }
            }
            if (toward >= 0) regroupMember(i, toward);
        }
    } else {
        for (auto& m : team_) m.offFrame = 0.0f;
    }
    checkTeamDown();
}

void GameWorld::updateTeamCamera(float dt) {
    // The players the frame keeps (in play), else everybody still there (all down: where
    // they fell). Display only: nothing of it is read by the simulation.
    Rect bb;
    bool any = false;
    Vec2 vel;
    bool grounded = true;
    int n = 0;
    auto add = [&](const Player& p) {
        bb = any ? bb.united(p.box) : p.box;
        any = true;
        vel += p.vel;
        grounded = grounded && p.grounded();
        ++n;
    };
    // 2.65 (raids, display only): this computer's player alone, while in play.
    const bool follow = raidMode_ && cameraFollow_ >= 0 && cameraFollow_ < static_cast<int>(team_.size()) &&
                        team_[static_cast<size_t>(cameraFollow_)].presence == Presence::Playing;
    if (follow) add(*team_[static_cast<size_t>(cameraFollow_)].player);
    for (const TeamMember& m : team_)
        if (!follow && m.presence == Presence::Playing && m.inFrame && m.player->state() != PlayerState::Hidden) add(*m.player);
    if (!any)
        for (const TeamMember& m : team_)
            if (m.presence != Presence::Gone && m.presence != Presence::Vacant) add(*m.player);
    if (!any) add(player_);
    const Vec2 c = bb.center();
    activeZone_ = nullptr;
    if (const CameraZone* bz = bossCameraZone()) {
        activeZone_ = bz;
    } else if (lockedZone_) {
        activeZone_ = &*lockedZone_;
    } else {
        for (auto& e : entities_) {
            auto* cz = dynamic_cast<CameraZoneEntity*>(e.get());
            if (cz && cz->active && cz->box.contains(c)) {
                activeZone_ = &cz->zone();
                break;
            }
        }
    }
    // A zone that would hide part of the team gives way to the team.
    if (activeZone_ && (activeZone_->lockCenter || activeZone_->clampToArea) && !activeZone_->area.containsRect(bb))
        activeZone_ = nullptr;
    camera_.setZone(activeZone_);
    camera_.setLean({});
    const Rect frame{bb.x - 56.0f, bb.y - 64.0f, bb.w + 112.0f, bb.h + 96.0f};
    camera_.setTeamArea(&frame);
    camera_.setTarget(c, n > 0 ? vel / static_cast<float>(n) : Vec2{}, 0.0f, grounded);
    camera_.fixedUpdate(dt);
}

GameWorld::MemberCarry GameWorld::carryOf(int i) const {
    MemberCarry c;
    const Player& p = playerAt(i);
    c.health = p.health;
    c.energy = p.energy;
    for (const auto& pw : p.powers()) c.powers.push_back(pw->id());
    if (!team_.empty()) {
        const TeamMember& m = team_[static_cast<size_t>(i)];
        c.presence = m.presence;
        c.absent = m.absent;
        c.standbyTicks = m.standbyTicks;
    }
    return c;
}

void GameWorld::restoreMember(int i, const MemberCarry& c, Vec2 feet) {
    Player& p = playerAt(i);
    p.clearPowers();
    for (const auto& pid : c.powers)
        if (const PowerDefinition* d = services_.content.power(pid)) p.grantPower(*d);
    int health = std::clamp(c.health, 1, p.maxHealth());
    if (team_.empty()) {
        p.teleportTo(feet);
        p.health = health;
        p.energy = c.energy;
        return;
    }
    TeamMember& m = team_[static_cast<size_t>(i)];
    m.absent = c.absent;
    m.standbyTicks = c.standbyTicks;
    m.revive = 0.0f;
    m.offFrame = 0.0f;
    if (c.presence == Presence::Down && raidMode_) {
        // 2.75 (raids): a player down stays down until a teammate revives them, even when the group
        // moves on: their soul waits at the place where the group arrives.
        m.presence = Presence::Down;
        m.soul = feet;
        m.fell = false;
        p.teleportTo(feet);
        p.setHidden(true);
        p.health = health;
        p.energy = c.energy;
        return;
    }
    // A player down when the group left comes back on its feet with half its hearts.
    m.presence = c.presence == Presence::Down ? Presence::Playing : c.presence;
    if (c.presence == Presence::Down) health = std::max(1, (p.maxHealth() + 1) / 2);
    if (m.presence == Presence::Playing) {
        PlayerScope scope(*this, i);
        p.spawn(*this, freeSpot(feet, i), false, false);
        m.safeFeet = p.feet();
        m.hasSafeFeet = true;
    } else {
        p.teleportTo(feet);
        p.setHidden(true);
    }
    p.health = health;  // after spawn(), which fills them
    p.energy = c.energy;
}

}  // namespace cosmic
