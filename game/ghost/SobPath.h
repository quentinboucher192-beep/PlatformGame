#pragma once

// 2.95 — the SOB path of a level version (docs/GHOST_RACING.md § « Startpos sur le tracé de la
// SOB »): the trace of the exact replays that hold the best of each segment of the route
// (PersonalBests, BestReplay), each cut to its segment. A StartPos lies on it: it is kept as its
// segment and its place, and an attempt from it plays that segment's best replay again, from that
// replay's own start, up to its moment nearest to the place (SobPreroll), then gives Nova to the
// player — the real state of that run there (hearts, energy, recharges, momentum, enemies),
// never a state made up. It follows the SOB: a better segment's replay anchors it anew. A
// segment never crossed (or whose best has no replay) has no path: no StartPos there.
//
// Reading the replays is done off the main thread (buildSobPath); what to read is taken from the
// bests on it (sobPathRequest).

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Paths.h"
#include "game/ghost/PersonalBests.h"
#include "game/online/Replay.h"

namespace cosmic {

class GameWorld;

/// What a path is built from: each segment of the route, its best's replay when it has one.
struct SobPathRequest {
    struct Part {
        PersonalBests::Range range;
        int route = 0;  // its place on the route (0: from the level's start)
        fs::path file;  // the replay holding its best (empty: none)
        BestReplay replay;
        BestSource source = BestSource::Run;
    };
    std::vector<Part> parts;
    /// Changes whenever a segment's best replay does (the path is then built again).
    std::string signature;
};
SobPathRequest sobPathRequest(const PersonalBests& bests, const std::string& levelKey);

struct SobPathPoint {
    int segment = -1;  // in SobPath::segments
    int index = -1;    // the replay's sample in it
    Vec2 at;
};

struct SobPathSegment {
    PersonalBests::Range range;
    int route = 0;
    fs::path file;
    BestReplay replay;
    BestSource source = BestSource::Run;
    std::vector<Vec2> points;  // Nova's feet at the replay's samples within the segment
    std::vector<float> times;  // the chronometer at each (sample k: k / its rate)
};

class SobPath {
public:
    std::vector<SobPathSegment> segments;  // in the route's order, those with a replay
    std::vector<PersonalBests::Range> missing;  // the route's segments without one (never crossed, no replay)
    std::string signature;
    bool ready = false;  // built (its replays read)

    bool empty() const { return segments.empty(); }
    /// The point of the path nearest to `p` within `radius` pixels (a tie: the earliest).
    std::optional<SobPathPoint> nearest(Vec2 p, float radius) const;
    /// The same in the segment `r` only.
    std::optional<SobPathPoint> nearestIn(const PersonalBests::Range& r, Vec2 p, float radius) const;
    /// The point before (`delta` < 0) or after it along the path, across segments; nullopt at an end.
    std::optional<SobPathPoint> step(const SobPathPoint& p, int delta) const;
    const SobPathSegment* segment(const PersonalBests::Range& r) const;
};
/// Reads the replays of `req` (files): call it off the main thread.
SobPath buildSobPath(const SobPathRequest& req);

/// A level version's path, built again off the main thread whenever a segment's best replay
/// changes (update() on the main thread, every frame of the screens that show it).
class SobPathCache {
public:
    SobPathCache() = default;
    SobPathCache(const SobPathCache&) = delete;
    SobPathCache& operator=(const SobPathCache&) = delete;
    ~SobPathCache();
    void update(const PersonalBests& bests, const std::string& levelKey);
    /// The path (nullptr while the first one is read). Valid until the next update().
    const SobPath* path() const { return path_ && path_->ready ? path_.get() : nullptr; }
    /// A new path was taken by the last update() (re-anchor what waits for it).
    bool changed() const { return changed_; }

private:
    std::string key_, wanted_;
    std::future<SobPath> future_;
    std::unique_ptr<SobPath> path_;
    bool changed_ = false;
};

class StartPosStore;
/// 2.95: the StartPos of before (a state taken or made up) that lie near the path (kSobPathSnap)
/// placed on it — on the nearest point of it, unless another StartPos is there. How many.
int reanchorStartPos(StartPosStore& store, const std::string& levelKey, const SobPath& path);

/// How near a StartPos must be to the path to lie on it (pixels): a StartPos of before within it
/// is placed on it again; F5 in a run places one at most this far from Nova.
constexpr float kSobPathSnap = 48.0f;
/// Two StartPos closer than this (pixels) are the same place: the second is refused.
constexpr float kSobPathSame = 6.0f;

/// The start of an attempt from a StartPos: the replay holding its segment's best now, how that
/// replay starts (its setup), and its moment nearest to the StartPos — the chronometer of the
/// replay's sample there (the step it was taken at), the place of that sample.
struct SobAnchor {
    float chrono = 0.0f;
    Vec2 at;
    int sample = -1;
};
/// The sample of `replay` within [best.start, best.end] nearest to `feet` (a tie: the earliest);
/// nullopt when it has none there.
std::optional<SobAnchor> sobAnchor(const ReplayData& replay, const BestReplay& best, Vec2 feet);

/// Plays a segment's best replay quietly up to its anchor, in a world just loaded and seeded the
/// way a run does (GameScene) or ReplayPlayback does: the replay's simulator first, then its
/// setup (`setup`, the replay's start), then its inputs. The state reached is the replay's own
/// state at that step (its digest: test « startpos on the SOB path… »).
class SobPreroll {
public:
    /// `replay` is kept alive by the preroll.
    SobPreroll(std::shared_ptr<const ReplayData> replay, GameWorld& world, const std::function<void(GameWorld&)>& setup, float chrono);
    ~SobPreroll();
    /// At most `budget` steps; `each(input)` after each one (the attempt's own recorder). True once
    /// over (arrived, or failed).
    bool advance(int budget, const std::function<void(const InputFrame&)>& each = {});
    bool arrived() const { return arrived_; }
    /// The replay ended before, or no longer plays the same (another version of the game).
    bool failed() const { return failed_; }
    float progress() const;
    float chrono() const { return chrono_; }
    /// Steps simulated so far.
    uint32_t tick() const { return sim_->tick(); }

private:
    std::shared_ptr<const ReplayData> replay_;
    GameWorld& world_;
    std::unique_ptr<ReplaySimulator> sim_;
    float chrono_ = 0.0f;
    bool arrived_ = false, failed_ = false;
};

}  // namespace cosmic
