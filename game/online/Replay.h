#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cosmic {

class GameWorld;

/// Compact trace of a run (format "CPRP" v1, see docs/online/REPLAY_FORMAT.md): the
/// player's feet position, state and health sampled at a fixed rate, plus discrete events.
/// The engine is not bit-for-bit deterministic across compilers, so the replay stores the
/// states needed to show the run instead of re-simulating inputs. Shared by the server's
/// analysis and the in-game viewer.
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

struct ReplayData {
    static constexpr uint16_t kFormatVersion = 1;
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
    void begin(const std::string& levelId, const std::string& levelHash, const std::string& attemptId);
    bool active() const { return active_; }
    void tick(const GameWorld& w);
    void onWorldEvent(const std::string& source, const std::string& trigger);
    /// Ends the recording when the level is completed (`elapsed` = timer value).
    void finish(const GameWorld& w, float elapsed);
    void abort() { active_ = false; }
    const ReplayData& data() const { return data_; }
    ReplayData take();

private:
    void push(ReplayEventType t, uint8_t arg = 0, uint16_t value = 0);
    void sample(const GameWorld& w);

    ReplayData data_;
    bool active_ = false;
    bool started_ = false;
    int lastDeaths_ = 0, lastShards_ = 0, lastRelics_ = 0, lastSecrets_ = 0, lastKills_ = 0, lastDamage_ = 0;
    uint8_t lastState_ = 0;
};

/// Random UUID v4 (attempt identifiers).
std::string makeUuidV4();

}  // namespace cosmic
