#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/gameplay/InputFrame.h"

namespace cosmic {

class GameWorld;

/// Replay of a run (format "CPRP", see docs/online/REPLAY_FORMAT.md).
///
/// Version 2 stores what is needed to play the run again exactly: the gameplay seed and the
/// input of every simulation step (run-length encoded), plus a digest of the simulation
/// state every 0.5 s to detect a desynchronisation. The viewer re-simulates the level: same
/// enemies, same attacks, same blocks breaking. It also keeps the version 1 trace (feet
/// position, state and health sampled at 30 Hz, discrete events), which the server analyses
/// and which old replays (version 1) only contain.
enum class ReplayEventType : uint8_t {
    Jump = 1,
    AirJump = 2,
    Death = 3,
    Respawn = 4,
    Pickup = 5,
    Power = 6,
    Checkpoint = 7,
    Damage = 8,
    Kill = 9,
    Teleport = 10,
    Finish = 11,
};

enum class ReplayPickup : uint8_t { Shard = 0, Relic = 1, Health = 2, Energy = 3, HeartContainer = 4, Secret = 5 };

struct ReplayFrame {
    float x = 0.0f;  // feet, world pixels
    float y = 0.0f;
    uint8_t state = 0;  // PlayerState
    uint8_t flags = 0;  // ReplayFrame::Flag
    uint8_t health = 0;
    enum Flag : uint8_t { FacingLeft = 1, Grounded = 2, Invulnerable = 4, Gliding = 8, Shield = 16 };
};

struct ReplayEvent {
    uint32_t frame = 0;
    ReplayEventType type = ReplayEventType::Jump;
    uint8_t arg = 0;
    uint16_t value = 0;
};

/// Simulation digest at a step (see simulationStateHash).
struct ReplayCheck {
    uint32_t tick = 0;  // number of steps simulated
    uint64_t hash = 0;
};

struct ReplayData {
    static constexpr uint16_t kFormatVersion = 2;
    /// One state digest every kCheckTicks steps (0.5 s).
    static constexpr uint32_t kCheckTicks = 60;
    /// Longest run accepted (2 hours of steps).
    static constexpr uint32_t kMaxTicks = 120u * 7200u;
    static constexpr uint16_t kSampleHz = 30;
    static constexpr size_t kMaxRawBytes = 4 * 1024 * 1024;

    uint16_t sampleHz = kSampleHz;
    uint32_t durationMs = 0;
    std::string levelHash;  // 64 hex characters
    std::string attemptId;  // UUID v4
    std::string levelId;
    std::string gameVersion = COSMIC_VERSION_STRING;
    std::vector<ReplayFrame> frames;
    std::vector<ReplayEvent> events;

    // Version 2: exact replay.
    uint16_t formatVersion = kFormatVersion;  // version read from the file
    uint64_t seed = 0;
    uint16_t tickHz = static_cast<uint16_t>(kSimHz);
    std::vector<InputFrame> inputs;  // one per simulation step since the level was loaded
    std::vector<ReplayCheck> checks;
    bool exact() const { return formatVersion >= 2 && !inputs.empty(); }

    /// Raw "CPRP" bytes.
    std::vector<uint8_t> serialize() const;
    static std::optional<ReplayData> parse(const std::vector<uint8_t>& raw, std::string* error = nullptr);
    /// Compressed "CPRZ" container (what is uploaded and saved).
    std::vector<uint8_t> encode() const;
    static std::optional<ReplayData> decode(const std::vector<uint8_t>& data, std::string* error = nullptr);

    size_t countEvents(ReplayEventType t) const;
    /// Interpolated frame at `seconds` (viewer).
    ReplayFrame sampleAt(float seconds) const;
};

/// Index of a power id in the replay format (stable list); 255 if unknown.
uint8_t replayPowerIndex(const std::string& powerId);
const char* replayPowerId(uint8_t index);

/// Builds the replay while the level is played: call tick() after every fixed update and
/// forward world events with onWorldEvent().
class ReplayRecorder {
public:
    void begin(const std::string& levelId, const std::string& levelHash, const std::string& attemptId, uint64_t seed);
    bool active() const { return active_; }
    /// Input of the step about to be simulated (call before stepWorld).
    void recordInput(const InputFrame& f);
    /// After the step: samples the trace and adds a state digest every kCheckTicks steps.
    void tick(const GameWorld& w);
    void onWorldEvent(const std::string& source, const std::string& trigger);
    /// Ends the recording when the level is completed (`elapsed` = timer value).
    void finish(const GameWorld& w, float elapsed);
    void abort() { active_ = false; }
    const ReplayData& data() const { return data_; }
    ReplayData take();

private:
    void push(ReplayEventType t, uint8_t arg = 0, uint16_t value = 0);
    void check(const GameWorld& w);
    void sample(const GameWorld& w);

    ReplayData data_;
    bool active_ = false;
    bool started_ = false;
    int lastDeaths_ = 0, lastShards_ = 0, lastRelics_ = 0, lastSecrets_ = 0, lastKills_ = 0, lastDamage_ = 0;
    uint8_t lastState_ = 0;
};

/// Plays a version 2 replay again: feeds the recorded inputs to a world loaded with the same
/// level (the caller loads it) and compares the state digests on the way. A mismatch means
/// the local simulation differs from the one that recorded the run (other level content,
/// other engine): the run is then no longer shown as exact.
class ReplaySimulator {
public:
    /// Seeds `world` with the replay's seed. `world` must have just loaded the level.
    ReplaySimulator(const ReplayData& replay, GameWorld& world);
    bool finished() const { return tick_ >= replay_.inputs.size(); }
    uint32_t tick() const { return tick_; }
    uint32_t totalTicks() const { return static_cast<uint32_t>(replay_.inputs.size()); }
    /// Simulates one step. Returns false once finished or desynchronised.
    bool step();
    bool desynced() const { return desyncTick_ != 0; }
    /// Step at which the first digest differed (0 = never).
    uint32_t desyncTick() const { return desyncTick_; }
    /// Digests verified so far.
    size_t checksPassed() const { return checksPassed_; }
    const InputFrame* currentInput() const { return tick_ > 0 && tick_ <= replay_.inputs.size() ? &replay_.inputs[tick_ - 1] : nullptr; }

private:
    const ReplayData& replay_;
    GameWorld& world_;
    uint32_t tick_ = 0;
    size_t nextCheck_ = 0;
    size_t checksPassed_ = 0;
    uint32_t desyncTick_ = 0;
};

/// Random UUID v4 (attempt identifiers).
std::string makeUuidV4();

}  // namespace cosmic
