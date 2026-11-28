#pragma once

// What the strategies of the portfolio share (docs/TAS.md « Le portefeuille »): a thread's own
// arena world on the course (savestates of one arena are put back only in that arena), and the
// best run studied once from the load. Search.cpp keeps its own Worker; these are the same rules
// (a death never kept, the category's rules, the player's presses and cinematics), written once
// more so that the strategies stay out of Search.cpp.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Math.h"
#include "game/gameplay/InputFrame.h"
#include "game/online/OnlineTypes.h"
#include "game/tas/ArenaWorld.h"
#include "tools/tas/Course.h"

namespace cosmic {
class ContentDatabase;
class CheckpointEntity;
class GameWorld;
struct LevelData;
}  // namespace cosmic

namespace cosmic::tas {

constexpr uint32_t keyBit(Action a) { return 1u << static_cast<int>(a); }

/// The power id held in each slot key (PowerSlot1..8; empty: none).
using SlotLayout = std::array<std::string, 8>;
/// The slot layout of `w`'s player now.
SlotLayout slotLayout(const GameWorld& w);
/// `f` (played by a run whose slots held `from`) for a world whose slots hold `to`: its power-slot
/// keys (and a power offer's answer) moved to the slots that hold the same powers there, a power
/// that world does not carry dropped; `prevHeld`: the keys sent the frame before (the presses and
/// releases of the slot keys recomputed from it). Unchanged when the layouts are the same.
InputFrame remapSlots(const InputFrame& f, const SlotLayout& from, const SlotLayout& to, uint32_t prevHeld);

/// The best run, studied once from the load: Nova and the clock after every frame, where each
/// section ends (as Search.cpp's Guide).
struct RunStudy {
    std::vector<InputFrame> frames;
    std::vector<size_t> ends;      // per section reached: the frame index just after its end
    std::vector<Vec2> feet, vel;   // Nova after i frames (i = 0..exitAt)
    std::vector<float> clockAt;    // the level's clock after i frames
    std::vector<float> potential;  // the course's progress after i frames (px along the route)
    std::vector<uint8_t> frozen;   // frame i is never edited: a cinematic plays, a skip, a power's answer
    size_t exitAt = 0;
    float clock = -1.0f;           // its time when it finishes the category, else negative
    float maxSpeedX = 0.0f, maxSpeed = 0.0f;  // the fastest Nova went (the time bound)
    /// The power in each slot key (PowerSlot1..8) after i frames: `layoutAt[i]` indexes `layouts`
    /// (they change only when a power is taken or replaced). To play this run's frames in a world
    /// that carries other powers, or the same in other slots (remapSlots).
    std::vector<SlotLayout> layouts;
    std::vector<uint16_t> layoutAt;
    bool finishes() const { return clock >= 0.0f; }
    static constexpr size_t kNone = static_cast<size_t>(-1);
    size_t sectionStart(size_t s) const { return s == 0 ? 0 : s - 1 < ends.size() ? ends[s - 1] : kNone; }
    /// The level's clock at the end of section `s` (negative: not reached).
    float splitAt(size_t s) const { return s < ends.size() && ends[s] < clockAt.size() ? clockAt[ends[s]] : -1.0f; }
    /// The `k` frame indices around `center` (± `radius`) where Nova was nearest to `f` moving as
    /// `v`, at least 3 steps apart, the nearest first.
    std::vector<size_t> nearest(Vec2 f, Vec2 v, size_t center, size_t radius, int k) const;
    /// The first frame index where the run's progress reached `potential` (exitAt when never).
    size_t frameAtPotential(float potential) const;
};

/// The presses and releases of `run` recomputed from the keys held (after an edit); in the
/// frames [lo, hi], the stick follows the direction keys held.
void repressRun(std::vector<InputFrame>& run, size_t lo, size_t hi);

/// The fastest finish of a re-join (the best run's rest played on from a state, realigned).
struct JoinTry {
    bool ok = false;
    float finish = 0.0f;
    size_t seedAt = 0, played = 0;
    int tried = 0;
};

class Sim {
public:
    Sim(const ContentDatabase& content, const LevelData& level, const Course& course, RunCategory category);
    ~Sim();
    Sim(const Sim&) = delete;
    Sim& operator=(const Sim&) = delete;

    ArenaWorld& arena() { return world_; }
    const GameWorld& world() const { return world_.world(); }
    const ArenaWorld::State& origin() const { return origin_; }
    const Course& course() const { return course_; }
    RunCategory category() const { return category_; }

    Progress measure(int fromWaypoint);
    RunStudy study(const std::vector<InputFrame>& frames);
    /// The clock where Nova is first inside a solid in `run` (a physics bug the search found: such
    /// a run is never written, docs/TAS.md « Jamais à travers un mur »); -1: never.
    float clipAt(const std::vector<InputFrame>& run);
    /// The rules broken now: a death beyond `deaths`, the category, the level failed.
    bool broken(int deaths) const;
    /// The exit reached now with the category kept (100 %: everything collected).
    bool atExit(bool* refused = nullptr) const;
    /// The checkpoints of the course lit now (bit k: the k-th checkpoint, CP1 first).
    uint32_t litCheckpoints() const;

    /// `frames[from..]` played from `at` to the exit: the clock there, or negative (broken, out of
    /// frames, past `limit`). `held`: the keys held before frames[from] (its presses recomputed),
    /// or kSame to play it as it is. `played`: the frames played.
    static constexpr uint32_t kSame = 0xFFFFFFFFu;
    float playToExit(const ArenaWorld::State& at, const std::vector<InputFrame>& frames, size_t from, float limit, size_t* played = nullptr,
                     uint32_t held = kSame);
    /// The world now (saved) played on with the best run's rest, taken up near `center` (where
    /// Nova was nearest, and center ± 2): the fastest finish under `limit`.
    JoinTry rejoin(const RunStudy& g, size_t center, uint32_t held, float limit);

    /// A run's savestates every `rung` frames from the load (the refinements start from them).
    void buildLadder(const std::vector<InputFrame>& run, size_t rung);
    /// The savestate at or before `frame`, and its frame index.
    const ArenaWorld::State& rungBefore(size_t frame, size_t& at) const;
    size_t ladderRung() const { return rung_; }

    /// A savestate from the worker's pool (given back with give()).
    std::unique_ptr<ArenaWorld::State> take();
    void give(std::unique_ptr<ArenaWorld::State> s);

private:
    const Course& course_;
    RunCategory category_;
    ArenaWorld world_;
    Course::Bound bound_;
    std::vector<const CheckpointEntity*> cps_;
    ArenaWorld::State origin_;
    std::unique_ptr<ArenaWorld::State> tmp_;
    std::vector<std::unique_ptr<ArenaWorld::State>> ladder_, pool_;
    size_t rung_ = 240;
};

}  // namespace cosmic::tas
