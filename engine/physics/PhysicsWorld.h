#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "engine/core/Math.h"

namespace cosmic {

enum class ColliderKind { Solid, OneWay };

/// Collider tags (bit flags) used by gameplay rules.
namespace ColliderTag {
constexpr uint32_t None = 0;
constexpr uint32_t Phase = 1u << 0;      // passable by the teleport power
constexpr uint32_t Breakable = 1u << 1;  // destroyed by dash
constexpr uint32_t Crumble = 1u << 2;    // crumbling platform
constexpr uint32_t Moving = 1u << 3;     // moving platform (carries bodies)
constexpr uint32_t Door = 1u << 4;
constexpr uint32_t Gate = 1u << 5;       // power gate
constexpr uint32_t Bounce = 1u << 6;
constexpr uint32_t EnemyOnly = 1u << 7;  // invisible wall for enemies only
}  // namespace ColliderTag

struct Collider {
    uint32_t id = 0;
    Rect box;
    ColliderKind kind = ColliderKind::Solid;
    uint32_t tags = 0;
    bool enabled = true;
    bool dynamic = false;
    Vec2 velocity;  // world px / s, for moving platforms
    void* owner = nullptr;
};

struct MoveOptions {
    bool dropThrough = false;   // fall through one-way platforms
    uint32_t ignoreTags = 0;    // colliders with any of these tags are ignored
    bool isEnemy = false;       // enemies collide with EnemyOnly walls
};

struct MoveResult {
    bool hitLeft = false;
    bool hitRight = false;
    bool hitTop = false;
    bool hitBottom = false;
    uint32_t groundId = 0;
    uint32_t wallId = 0;
    uint32_t ceilingId = 0;
};

/// Axis-aligned collision world for the level: static solids in a uniform grid,
/// dynamic solids (moving platforms) checked linearly.
class PhysicsWorld {
public:
    static constexpr float kCellSize = 64.0f;

    void clear();
    uint32_t add(const Rect& box, ColliderKind kind, uint32_t tags = 0, void* owner = nullptr, bool dynamic = false);
    void remove(uint32_t id);
    Collider* get(uint32_t id);
    const Collider* get(uint32_t id) const;
    void setEnabled(uint32_t id, bool enabled);
    /// Moves a dynamic collider (moving platforms); `velocity` is stored for carrying.
    void setDynamicBox(uint32_t id, const Rect& box, Vec2 velocity);

    /// Moves `box` by `delta`, resolving collisions axis by axis.
    MoveResult move(Rect& box, Vec2 delta, const MoveOptions& opt = {}) const;

    bool overlapsSolid(const Rect& box, const MoveOptions& opt = {}) const;
    /// First solid overlapping `box` (or nullptr).
    const Collider* firstSolid(const Rect& box, const MoveOptions& opt = {}) const;
    /// Ground probe: collider directly under `box` (1 px), including one-way platforms.
    const Collider* groundBelow(const Rect& box, const MoveOptions& opt = {}) const;
    /// Raycast against solids. Returns hit fraction in [0, 1].
    std::optional<float> raycast(Vec2 from, Vec2 to, bool includeOneWay = false, uint32_t ignoreTags = 0) const;
    bool lineOfSight(Vec2 a, Vec2 b) const { return !raycast(a, b).has_value(); }

    template <typename F>
    void forEachIn(const Rect& area, F&& fn) const {
        std::vector<uint32_t> found;
        query(area, found);
        for (uint32_t idx : found) fn(colliders_[idx]);
    }

    const std::vector<Collider>& colliders() const { return colliders_; }

private:
    void query(const Rect& area, std::vector<uint32_t>& out) const;
    bool ignored(const Collider& c, const MoveOptions& opt) const;
    int64_t cellKey(int cx, int cy) const { return (static_cast<int64_t>(cx) << 32) ^ static_cast<uint32_t>(cy); }
    void insertGrid(uint32_t index);
    void removeGrid(uint32_t index);

    std::vector<Collider> colliders_;  // index = id - 1 (ids are never reused until clear)
    std::vector<uint32_t> dynamic_;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
    mutable std::vector<uint32_t> scratch_;
    mutable std::vector<uint32_t> stamp_;
    mutable uint32_t queryStamp_ = 0;
};

}  // namespace cosmic
