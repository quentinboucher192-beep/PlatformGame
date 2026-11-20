#include "game/raid/nav/Terrain.h"

#include <algorithm>
#include <cmath>

#include "engine/physics/Collision.h"
#include "game/gameplay/GameWorld.h"
#include "game/gameplay/InputFrame.h"
#include "game/gameplay/Projectile.h"
#include "game/gameplay/entities/Entities.h"
#include "game/raid/RaidRun.h"

namespace cosmic::raid::nav {

void Terrain::sync(const GameWorld& w, const RaidRun* run) {
    world_ = &w;
    ++step_;
    liquidHorizon_ = -1;
    shotsDone_ = false;
    const auto& real = w.physics().colliders();
    bool full = !ready_ || level_ != w.level().id || real.size() != phys_.colliders().size();
    if (!full) {
        for (const Collider& r : real) {
            Collider* c = phys_.get(r.id);
            if (!c) {
                full = true;
                break;
            }
            if (r.dynamic) {
                c->box = r.box;
                c->velocity = r.velocity;
                c->enabled = r.enabled;
            } else {
                c->enabled = r.enabled;
                if (!(c->box == r.box)) {
                    full = true;  // a wall added or removed by a boss: the copy again
                    break;
                }
            }
        }
    }
    if (full) {
        phys_ = w.physics();
        level_ = w.level().id;
        ready_ = true;
        killY_ = w.level().effectiveKillY();
        bounds_ = w.level().bounds;
    }
    // The zone's periodic liquid and its schedule (RaidRun::updateMagma, in whole steps).
    liquid_ = nullptr;
    scheduled_ = false;
    if (run && run->rules().magma.present) {
        liquid_ = dynamic_cast<const DamageZoneEntity*>(const_cast<GameWorld&>(w).entity(run->rules().magma.zone));
        if (run->magmaIn() >= 0.0f) {
            const Magma& m = run->rules().magma;
            scheduled_ = true;
            zoneTicks_ = run->zoneTicks();
            first_ = static_cast<uint32_t>(std::lround(m.first * kSimHz));
            period_ = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(m.period * kSimHz)));
            duration_ = static_cast<uint32_t>(std::lround(m.duration * kSimHz));
        }
    }
    // The entities: scanned again when one is added, removed, switched on or off; else only what moves.
    uint64_t sig = 1469598103934665603ULL;
    for (const auto& e : w.entities())
        sig = (sig ^ (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(e.get())) | (e->active ? 1u : 0u) | (e->removed ? 2u : 0u))) * 1099511628211ULL;
    if (full || sig != entitySig_ || liquid_ != liquidSeen_) {
        entitySig_ = sig;
        liquidSeen_ = liquid_;
        scan(w);
        return;
    }
    for (Crumble& c : crumbles_) {
        c.phase = c.e->crumblePhase();
        c.timer = c.e->crumbleTimer();
    }
    for (Laser& l : lasers_) l.to = l.e->beamTo();
    for (Env& e : env_)
        if (e.updraft) e.on = e.updraft->blowing();
}

void Terrain::scan(const GameWorld& w) {
    movers_.clear();
    crumbles_.clear();
    lasers_.clear();
    env_.clear();
    lasting_.clear();
    deadly_.clear();
    for (const auto& up : w.entities()) {
        const LevelEntity* e = up.get();
        if (e->removed) continue;
        if (auto* mp = dynamic_cast<const MovingPlatformEntity*>(e)) {
            Mover m;
            m.collider = mp->colliderId();
            m.e = mp;
            const LevelObject& o = e->object();
            m.oneway = o.prop<bool>("oneway", false) || o.type == "bubble";
            m.box = e->box;
            const Vec2 a = o.rect.pos(), b = a + Vec2{o.prop<float>("dx", 128.0f), o.prop<float>("dy", 0.0f)};
            m.span = Rect{std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x) + o.rect.w, std::fabs(b.y - a.y) + o.rect.h}.united(e->box);
            if (o.prop<bool>("orbit", false)) m.span = e->box.expanded(std::fabs(o.prop<float>("dx", 128.0f)) + std::fabs(o.prop<float>("dy", 0.0f)));
            movers_.push_back(std::move(m));
            continue;
        }
        if (auto* cp = dynamic_cast<const CrumblePlatformEntity*>(e)) {
            Crumble c;
            c.e = cp;
            c.collider = cp->colliderId();
            c.box = e->box;
            c.phase = cp->crumblePhase();
            c.timer = cp->crumbleTimer();
            c.delay = e->object().prop<float>("delay", 0.45f);
            c.respawn = e->object().prop<float>("respawn", 3.0f);
            c.onEvent = e->object().prop<bool>("onEvent", false);
            crumbles_.push_back(c);
            continue;
        }
        if (auto* l = dynamic_cast<const LaserEntity*>(e)) {
            lasers_.push_back({l, l->beamFrom(), l->beamTo()});
            continue;
        }
        if (!e->active) continue;
        if (dynamic_cast<const LadderEntity*>(e)) {
            env_.push_back({e->overlapBox(), 0, 0.0f, nullptr, true});
        } else if (auto* lq = dynamic_cast<const LiquidEntity*>(e)) {
            if (lq->swimmable()) {
                env_.push_back({e->overlapBox(), 1, 0.0f, nullptr, true});
            } else {
                lasting_.push_back(e->overlapBox());
                deadly_.push_back(1);
            }
        } else if (auto* u = dynamic_cast<const UpdraftEntity*>(e)) {
            env_.push_back({e->overlapBox(), 2, e->object().prop<float>("strength", 230.0f), u, u->blowing()});
        } else if (dynamic_cast<const SpikesEntity*>(e)) {
            lasting_.push_back(e->overlapBox());
            deadly_.push_back(e->object().prop<bool>("instantKill", false) ? 1 : 0);
        } else if (auto* dz = dynamic_cast<const DamageZoneEntity*>(e)) {
            if (dz == liquid_) continue;
            const Rect r = e->overlapBox();
            if (r.h > 0.5f) {
                lasting_.push_back(r);
                deadly_.push_back(0);
            }
        }
    }
}

Rect Terrain::moverCollider(const Mover& m, Vec2 topLeft) const {
    return m.oneway ? Rect{topLeft.x, topLeft.y, m.box.w, 4.0f} : Rect{topLeft.x, topLeft.y, m.box.w, m.box.h};
}

Vec2 Terrain::moverAt(const Mover& m, int k) const {
    if (k <= 0 || m.route.empty() || m.step != step_ || !m.near) return m.box.pos();
    return m.route[static_cast<size_t>(std::min<int>(k, static_cast<int>(m.route.size())) - 1)];
}

void Terrain::plan(int horizon, uint32_t rider, Vec2 focus) {
    horizon_ = std::max(1, horizon);
    const Rect window{focus.x - 720.0f, focus.y - 600.0f, 1440.0f, 1200.0f};
    for (Mover& m : movers_) {
        if (m.step != step_) {
            m.near = false;
            m.box = m.e->box;
        }
        if (!m.span.intersects(window)) continue;
        const bool rides = rider != 0 && rider == m.collider;
        if (m.step == step_ && m.near && m.rider == rides && static_cast<int>(m.route.size()) >= horizon_) continue;
        m.box = m.e->box;
        m.e->predict(horizon_, kSimDt, rides, m.route);
        m.step = step_;
        m.near = true;
        m.rider = rides;
    }
    // The enemies' shots: their path (gravity, life), until a wall; 2 s ahead.
    if (!shotsDone_) {
        shotsDone_ = true;
        shots_.clear();
        if (world_)
            for (const Projectile& p : world_->projectiles()) {
                if (!p.alive || p.fromPlayer || p.reflected) continue;
                Shot s;
                s.radius = p.radius + 2.0f + p.explosionRadius * 0.6f;
                Vec2 pos = p.pos, vel = p.vel;
                float life = p.life;
                MoveOptions opt;
                opt.ignoreTags = ColliderTag::EnemyOnly | ColliderTag::Teammate;
                for (int k = 1; k <= 240; ++k) {
                    life -= kSimDt;
                    if (life <= 0.0f) break;
                    vel.y += p.gravity * kSimDt;
                    pos = pos + vel * kSimDt;
                    if (p.clip != "shockwave" && (k % 4) == 0 && phys_.overlapsSolid(Rect::fromCenter(pos, {2.0f, 2.0f}), opt)) break;
                    s.at.push_back(pos);
                }
                if (!s.at.empty()) shots_.push_back(std::move(s));
            }
    }
    // The zone's periodic liquid: up when its schedule says, rising (a magma) or at once.
    if (liquidHorizon_ >= horizon_) return;
    liquidHorizon_ = horizon_;
    liquidAt_.clear();
    liquidFrom_ = -1;
    if (!liquid_) return;
    const Rect box = liquid_->box;
    const bool magma = liquid_->magma();
    const float rise = std::max(0.1f, liquid_->object().prop<float>("rise", 1.4f));
    const float fall = std::max(0.1f, liquid_->object().prop<float>("fall", 1.2f));
    float level = liquid_->magmaLevel();
    bool on = liquid_->active;
    for (int k = 0; k <= horizon_; ++k) {
        if (k > 0) {
            if (scheduled_) {
                const uint32_t t = zoneTicks_ + static_cast<uint32_t>(k);
                on = t >= first_ && (t - first_) % period_ < duration_;
            }
            if (magma) level = on ? std::min(1.0f, level + kSimDt / rise) : std::max(0.0f, level - kSimDt / fall);
        }
        Rect r;
        if (magma) {
            if (level > 0.02f) {
                const float h = box.h * std::clamp(level, 0.0f, 1.0f);
                r = box.w >= box.h ? Rect{box.x, box.bottom() - h, box.w, h} : Rect{box.x, box.y, box.w, h};
            }
        } else if (on) {
            r = box;
        }
        liquidAt_.push_back(r);
        if (r.h > 0.5f && liquidFrom_ < 0) liquidFrom_ = k;
    }
}

Rect Terrain::liquidAt(int k) const {
    if (k < 0 || liquidAt_.empty()) return {};
    return liquidAt_[static_cast<size_t>(std::min<int>(k, static_cast<int>(liquidAt_.size()) - 1))];
}

bool Terrain::liquidIsMagma() const { return liquid_ && liquid_->magma(); }

int Terrain::liquidIn() const {
    if (!liquid_) return -1;
    if (liquid_->active || liquid_->magmaLevel() > 0.02f) return 0;
    if (!scheduled_) return -1;
    const uint32_t t = zoneTicks_;
    if (t < first_) return static_cast<int>(first_ - t);
    const uint32_t into = (t - first_) % period_;
    if (into < duration_) return 0;
    return static_cast<int>(period_ - into);
}

Rect Terrain::liquidBox() const { return liquid_ ? liquid_->box : Rect{}; }

void Terrain::begin(Run& r) {
    r.k = 0;
    r.crumbles.resize(crumbles_.size());
    for (size_t i = 0; i < crumbles_.size(); ++i) r.crumbles[i] = {crumbles_[i].phase, crumbles_[i].timer};
}

void Terrain::advance(Run& r, Body& b, const BodySim& sim) {
    const int k = ++r.k;
    // The entities' step: the moving platforms move, the crumbling platforms under the body.
    for (const Mover& m : movers_) {
        if (!m.near || m.step != step_) continue;
        const Vec2 cur = moverAt(m, k), prev = moverAt(m, k - 1);
        if (Collider* c = phys_.get(m.collider)) {
            c->box = moverCollider(m, cur);
            c->velocity = (cur - prev) / kSimDt;
        }
    }
    for (size_t i = 0; i < crumbles_.size(); ++i) {
        const Crumble& c = crumbles_[i];
        Run::Crumble& s = r.crumbles[i];
        Collider* col = phys_.get(c.collider);
        switch (s.phase) {
            case 0:
                if (!c.onEvent && b.grounded && b.groundId == c.collider) {
                    s.phase = 1;
                    s.timer = c.delay;
                }
                break;
            case 1:
                s.timer -= kSimDt;
                if (s.timer <= 0.0f) {
                    s.phase = 2;
                    s.timer = c.respawn;
                    if (col) col->enabled = false;
                }
                break;
            default:
                if (s.timer > 0.0f) {
                    s.timer -= kSimDt;
                    if (s.timer <= 0.0f) {
                        if (b.box.intersects(c.box)) {
                            s.timer = 0.3f;
                        } else {
                            s.phase = 0;
                            if (col) col->enabled = true;
                        }
                    }
                }
                break;
        }
    }
    // GameWorld::carryBodies: the platform under the body carries it, a rising one pushes it up.
    for (const Mover& m : movers_) {
        if (!m.near || m.step != step_) continue;
        const Vec2 cur = moverAt(m, k), prev = moverAt(m, k - 1);
        const Vec2 d = cur - prev;
        if (d.lengthSq() < 1e-8f) continue;
        if (b.groundId == m.collider) {
            phys_.move(b.box, {d.x, 0.0f}, sim.options(b));
            b.box.y += d.y;
        } else if (d.y < 0.0f && b.box.intersects(Rect{cur.x, cur.y, m.box.w, m.box.h})) {
            b.box.y = cur.y - b.box.h;
        }
    }
    // Its surroundings (GameWorld::updatePlayerOverlaps).
    b.onLadder = false;
    b.inWater = false;
    b.updraft = 0.0f;
    b.speedMultiplier = 1.0f;
    for (const Env& e : env_) {
        if (!e.box.intersects(b.box)) continue;
        if (e.kind == 0) {
            b.onLadder = true;
            b.ladderRect = e.box;
        } else if (e.kind == 1) {
            b.inWater = true;
        } else if (e.on) {
            b.updraft = std::max(b.updraft, e.strength);
        }
    }
}

void Terrain::end(Run& r) {
    (void)r;
    if (!world_) return;
    const PhysicsWorld& real = world_->physics();
    for (const Mover& m : movers_)
        if (const Collider* rc = real.get(m.collider))
            if (Collider* c = phys_.get(m.collider)) {
                c->box = rc->box;
                c->velocity = rc->velocity;
                c->enabled = rc->enabled;
            }
    for (const Crumble& cr : crumbles_)
        if (const Collider* rc = real.get(cr.collider))
            if (Collider* c = phys_.get(cr.collider)) c->enabled = rc->enabled;
}

Harm Terrain::harm(const Rect& box, int k) const {
    if (box.y > killY_) return Harm::Void;
    for (size_t i = 0; i < keepOut_.size(); ++i)
        if (static_cast<int>(i) + 1 != allowOut_ && keepOut_[i].intersects(box)) return Harm::Void;
    Harm worst = Harm::None;
    for (size_t i = 0; i < lasting_.size(); ++i)
        if (lasting_[i].intersects(box)) {
            if (deadly_[i]) return Harm::Liquid;
            worst = Harm::Hazard;
        }
    if (k >= 0 && !liquidAt_.empty()) {
        const Rect l = liquidAt(k);
        if (l.h > 0.5f && l.intersects(box)) return Harm::Liquid;
    }
    if (worst != Harm::None) return worst;
    for (const Laser& l : lasers_)
        if (l.e->firesIn(static_cast<float>(k) * kSimDt) && Collision::segmentIntersectsRect(l.from, l.to, box.expanded(-1.0f))) return Harm::Beam;
    if (k > 0)
        for (const Shot& s : shots_)
            if (k - 1 < static_cast<int>(s.at.size()) && Collision::circleIntersectsRect(s.at[static_cast<size_t>(k - 1)], s.radius, box)) return Harm::Shot;
    for (const Rect& r : blows_)
        if (r.intersects(box)) return Harm::Blow;
    return Harm::None;
}

bool Terrain::moverBox(uint32_t id, int k, Rect& out) const {
    for (const Mover& m : movers_) {
        if (m.collider != id) continue;
        const Vec2 p = moverAt(m, k);
        out = {p.x, p.y, m.box.w, m.box.h};
        return true;
    }
    return false;
}

}  // namespace cosmic::raid::nav
