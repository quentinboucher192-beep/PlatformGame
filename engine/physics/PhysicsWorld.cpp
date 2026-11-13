#include "engine/physics/PhysicsWorld.h"

#include <cmath>

#include "engine/physics/Collision.h"

namespace cosmic {

namespace {
constexpr float kEps = 0.001f;
}

void PhysicsWorld::clear() {
    colliders_.clear();
    dynamic_.clear();
    grid_.clear();
    stamp_.clear();
}

uint32_t PhysicsWorld::add(const Rect& box, ColliderKind kind, uint32_t tags, void* owner, bool dynamic) {
    Collider c;
    c.id = static_cast<uint32_t>(colliders_.size() + 1);
    c.box = box;
    c.kind = kind;
    c.tags = tags;
    c.owner = owner;
    c.dynamic = dynamic;
    colliders_.push_back(c);
    stamp_.push_back(0);
    uint32_t index = c.id - 1;
    if (dynamic)
        dynamic_.push_back(index);
    else
        insertGrid(index);
    return c.id;
}

void PhysicsWorld::insertGrid(uint32_t index) {
    const Rect& b = colliders_[index].box;
    int x0 = static_cast<int>(std::floor(b.x / kCellSize)), x1 = static_cast<int>(std::floor((b.right() - kEps) / kCellSize));
    int y0 = static_cast<int>(std::floor(b.y / kCellSize)), y1 = static_cast<int>(std::floor((b.bottom() - kEps) / kCellSize));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) grid_[cellKey(x, y)].push_back(index);
}

void PhysicsWorld::removeGrid(uint32_t index) {
    const Rect& b = colliders_[index].box;
    int x0 = static_cast<int>(std::floor(b.x / kCellSize)), x1 = static_cast<int>(std::floor((b.right() - kEps) / kCellSize));
    int y0 = static_cast<int>(std::floor(b.y / kCellSize)), y1 = static_cast<int>(std::floor((b.bottom() - kEps) / kCellSize));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            auto it = grid_.find(cellKey(x, y));
            if (it == grid_.end()) continue;
            auto& v = it->second;
            v.erase(std::remove(v.begin(), v.end(), index), v.end());
        }
}

void PhysicsWorld::remove(uint32_t id) {
    if (id == 0 || id > colliders_.size()) return;
    Collider& c = colliders_[id - 1];
    if (!c.enabled && c.box.empty()) return;
    if (c.dynamic)
        dynamic_.erase(std::remove(dynamic_.begin(), dynamic_.end(), id - 1), dynamic_.end());
    else
        removeGrid(id - 1);
    c.enabled = false;
    c.box = {};
}

Collider* PhysicsWorld::get(uint32_t id) { return (id == 0 || id > colliders_.size()) ? nullptr : &colliders_[id - 1]; }
const Collider* PhysicsWorld::get(uint32_t id) const {
    return (id == 0 || id > colliders_.size()) ? nullptr : &colliders_[id - 1];
}

void PhysicsWorld::setEnabled(uint32_t id, bool enabled) {
    if (Collider* c = get(id)) c->enabled = enabled;
}

void PhysicsWorld::setDynamicBox(uint32_t id, const Rect& box, Vec2 velocity) {
    if (Collider* c = get(id)) {
        c->box = box;
        c->velocity = velocity;
    }
}

void PhysicsWorld::query(const Rect& area, std::vector<uint32_t>& out) const {
    out.clear();
    if (++queryStamp_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        queryStamp_ = 1;
    }
    int x0 = static_cast<int>(std::floor(area.x / kCellSize)), x1 = static_cast<int>(std::floor(area.right() / kCellSize));
    int y0 = static_cast<int>(std::floor(area.y / kCellSize)), y1 = static_cast<int>(std::floor(area.bottom() / kCellSize));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            auto it = grid_.find(cellKey(x, y));
            if (it == grid_.end()) continue;
            for (uint32_t idx : it->second) {
                if (stamp_[idx] == queryStamp_) continue;
                stamp_[idx] = queryStamp_;
                if (colliders_[idx].box.intersects(area)) out.push_back(idx);
            }
        }
    for (uint32_t idx : dynamic_)
        if (colliders_[idx].box.intersects(area)) out.push_back(idx);
}

bool PhysicsWorld::ignored(const Collider& c, const MoveOptions& opt) const {
    if (!c.enabled) return true;
    if (c.tags & opt.ignoreTags) return true;
    if ((c.tags & ColliderTag::EnemyOnly) && !opt.isEnemy) return true;
    return false;
}

MoveResult PhysicsWorld::move(Rect& box, Vec2 delta, const MoveOptions& opt) const {
    MoveResult res;
    std::vector<uint32_t> hits;

    // ---- X axis
    if (delta.x != 0.0f) {
        Rect target = box.translated({delta.x, 0.0f});
        Rect sweep = box.united(target);
        query(sweep, hits);
        float nx = target.x;
        for (uint32_t idx : hits) {
            const Collider& c = colliders_[idx];
            if (ignored(c, opt) || c.kind != ColliderKind::Solid) continue;
            // Only colliders that vertically overlap the box.
            if (c.box.y >= box.bottom() - kEps || c.box.bottom() <= box.y + kEps) continue;
            if (delta.x > 0 && c.box.x >= box.right() - kEps && c.box.x < nx + box.w) {
                nx = c.box.x - box.w;
                res.hitRight = true;
                res.wallId = c.id;
            } else if (delta.x < 0 && c.box.right() <= box.x + kEps && c.box.right() > nx) {
                nx = c.box.right();
                res.hitLeft = true;
                res.wallId = c.id;
            }
        }
        box.x = nx;
    }

    // ---- Y axis
    if (delta.y != 0.0f) {
        Rect target = box.translated({0.0f, delta.y});
        Rect sweep = box.united(target);
        query(sweep, hits);
        float ny = target.y;
        for (uint32_t idx : hits) {
            const Collider& c = colliders_[idx];
            if (ignored(c, opt)) continue;
            if (c.box.x >= box.right() - kEps || c.box.right() <= box.x + kEps) continue;
            if (c.kind == ColliderKind::OneWay) {
                if (delta.y <= 0 || opt.dropThrough) continue;
                if (box.bottom() > c.box.y + 0.5f) continue;  // was not above the platform
            }
            if (delta.y > 0 && c.box.y >= box.bottom() - (c.kind == ColliderKind::OneWay ? 0.5f : kEps) &&
                c.box.y < ny + box.h) {
                ny = c.box.y - box.h;
                res.hitBottom = true;
                res.groundId = c.id;
            } else if (delta.y < 0 && c.kind == ColliderKind::Solid && c.box.bottom() <= box.y + kEps &&
                       c.box.bottom() > ny) {
                ny = c.box.bottom();
                res.hitTop = true;
                res.ceilingId = c.id;
            }
        }
        box.y = ny;
    }
    return res;
}

const Collider* PhysicsWorld::firstSolid(const Rect& box, const MoveOptions& opt) const {
    query(box, scratch_);
    for (uint32_t idx : scratch_) {
        const Collider& c = colliders_[idx];
        if (ignored(c, opt) || c.kind != ColliderKind::Solid) continue;
        if (c.box.intersects(box)) return &c;
    }
    return nullptr;
}

bool PhysicsWorld::overlapsSolid(const Rect& box, const MoveOptions& opt) const { return firstSolid(box, opt) != nullptr; }

const Collider* PhysicsWorld::groundBelow(const Rect& box, const MoveOptions& opt) const {
    Rect probe{box.x + 0.5f, box.bottom(), box.w - 1.0f, 1.5f};
    query(probe, scratch_);
    const Collider* best = nullptr;
    for (uint32_t idx : scratch_) {
        const Collider& c = colliders_[idx];
        if (ignored(c, opt)) continue;
        if (c.kind == ColliderKind::OneWay && (opt.dropThrough || std::abs(c.box.y - box.bottom()) > 1.0f)) continue;
        if (c.box.y < box.bottom() - 0.5f && c.kind == ColliderKind::OneWay) continue;
        if (!best || c.box.y < best->box.y) best = &c;
    }
    return best;
}

std::optional<float> PhysicsWorld::raycast(Vec2 from, Vec2 to, bool includeOneWay, uint32_t ignoreTags) const {
    Rect area{std::min(from.x, to.x), std::min(from.y, to.y), std::abs(to.x - from.x) + 1, std::abs(to.y - from.y) + 1};
    query(area, scratch_);
    std::optional<float> best;
    for (uint32_t idx : scratch_) {
        const Collider& c = colliders_[idx];
        if (!c.enabled || (c.tags & ignoreTags) || (c.tags & ColliderTag::EnemyOnly)) continue;
        if (c.kind == ColliderKind::OneWay && !includeOneWay) continue;
        float t;
        if (Collision::segmentIntersectsRect(from, to, c.box, &t)) {
            if (!best || t < *best) best = t;
        }
    }
    return best;
}

}  // namespace cosmic
