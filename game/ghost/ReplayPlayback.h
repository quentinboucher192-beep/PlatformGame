#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "game/gameplay/GameWorld.h"
#include "game/online/Replay.h"

namespace cosmic {

/// An exact replay (format 2) played with free access to any instant: the advanced replay
/// viewer and the replay comparison.
///
/// The simulation only runs forward and a world cannot be copied. Going back therefore
/// starts again from a « point de reprise »: another copy of the level simulated with the
/// same inputs, kept a little behind the step shown (about 0.75 s, 3 s and 12 s behind,
/// maintained while the replay plays), or, without one, the level loaded again. From there
/// the recorded inputs are simulated up to the step asked for (quietly, a few thousand
/// steps per frame). What is shown after a jump is therefore always the real simulation at
/// that step, never an estimate: the same state as a continuous playback (test « replay: a
/// jump in time gives the same state as playing continuously »).
///
/// The run's chronometer pauses during the intro and hit stops: the chronometer value of
/// every step already simulated is kept, so that a jump to a time (a checkpoint split, the
/// timeline) lands on the first step showing it.
class ReplayPlayback {
public:
    /// `replay` must outlive the playback. `level` is the installed level (same version).
    ReplayPlayback(WorldServices services, LevelData level, const ReplayData& replay);
    ~ReplayPlayback();

    GameWorld& world() { return *main_.world; }
    const GameWorld& world() const { return *main_.world; }
    /// Changes each time the world shown is another one (a jump back): the scene starts
    /// again what depends on it (HUD).
    uint32_t generation() const { return generation_; }

    uint32_t tick() const;
    uint32_t totalTicks() const;
    bool finished() const;
    bool desynced() const;
    uint32_t desyncTick() const;
    size_t checksPassed() const;
    /// Input of the last step simulated (input display).
    const InputFrame* currentInput() const;
    /// The run's chronometer now (seconds, the level timer).
    float runTime() const;

    /// Simulates the next step. False at the end or once desynchronised.
    bool step();

    // ------------------------------------------------------------ jumps
    /// Goes to the state after `tick` steps (0: the level just loaded). Returns at once;
    /// advance() does the work.
    void seekTick(uint32_t tick);
    /// Goes to the first step whose chronometer reaches `seconds` (the end if never).
    void seekTime(double seconds);
    bool seeking() const { return mode_ != Mode::None; }
    /// Works on a pending jump with at most `budget` steps. True when there (or impossible:
    /// end of the replay, desynchronisation).
    bool advance(int budget);
    /// The whole jump at once (tests, short jumps).
    void finishSeek();
    /// 0..1 of the pending jump.
    float seekProgress() const;
    /// Chronometer value after `tick` steps when it is known (simulated once), else -1.
    float chronoAt(uint32_t tick) const;
    /// Step the last jump back started from (a point de reprise, or 0: the level's start).
    uint32_t lastResume() const { return lastResume_; }

    // ---------------------------------------------------- points de reprise
    /// Keeps the points de reprise behind the step shown, with at most `budget` steps (call
    /// every frame when no jump is pending; a new copy of the level is loaded at most once
    /// per call).
    void maintain(int budget);
    /// Steps of the points de reprise kept (tests, display).
    std::vector<uint32_t> resumePoints() const;

private:
    struct Run {
        std::unique_ptr<GameWorld> world;
        std::unique_ptr<ReplaySimulator> sim;
        explicit operator bool() const { return world != nullptr; }
    };
    Run makeRun() const;
    void note();
    bool arrived() const;
    bool nearTarget() const;

    WorldServices services_;
    LevelData level_;
    const ReplayData& replay_;
    Run main_;
    std::vector<Run> spares_;  // points de reprise, behind main_
    static constexpr std::array<uint32_t, 3> kLags{90, 360, 1440};
    uint32_t generation_ = 0;
    uint32_t lastResume_ = 0;
    std::vector<float> chrono_;  // chronometer after k steps (k = index), -1 unknown
    uint32_t known_ = 0;         // chrono_ is known for 0..known_
    enum class Mode { None, Tick, Time } mode_ = Mode::None;
    uint32_t targetTick_ = 0;
    double targetTime_ = 0.0;
    uint32_t fromTick_ = 0;
    bool aged_ = false;  // the particles of the fast-forward were cleared near the target
};

}  // namespace cosmic
