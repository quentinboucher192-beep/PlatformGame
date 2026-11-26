#pragma once

// 2.91 — measure points (« points de mesure », docs/GHOST_RACING.md § « Startpos et points de
// mesure »): two fictitious checkpoints A and B placed in a level version; crossing A starts a
// chronometer, crossing B stops it. Placed in training (InputFrame::PlaceMeasure: A, then B),
// kept per level version (saves/measures.json), and timed in training and in every replay (the
// player's and the online ones) from the stream of Nova's positions (MeasureTimer). An A → B
// time stays personal: it touches neither the Sum of Best nor any record.
//
// A gate is vertical: the line x = at.x from kAbove pixels above the feet of Nova when it was
// placed to kBelow below. Crossing it, either way, between two positions of the stream counts,
// at the exact moment interpolated between them (sub-step precision, whatever the stream's rate).
// Distances are told in metres: 1 m = 1 tile = 16 pixels.

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/Math.h"
#include "engine/core/Paths.h"

namespace cosmic {

/// 1 m = 1 tile (16 pixels).
constexpr float kPixelsPerMeter = 16.0f;
inline float metersOf(float pixels) { return pixels / kPixelsPerMeter; }

struct MeasureGate {
    static constexpr float kAbove = 192.0f;  // 12 m above the feet
    static constexpr float kBelow = 32.0f;   // 2 m below
    Vec2 at;                                 // Nova's feet when it was placed
    /// Where the move from `a` to `b` crosses the gate (0..1 of the move), nullopt when it does
    /// not. A move that ends exactly on the line crosses it; one that starts there does not.
    std::optional<float> crossing(Vec2 a, Vec2 b) const;
    float top() const { return at.y - kAbove; }
    float bottom() const { return at.y + kBelow; }
};

/// Times A → B along a stream of positions.
class MeasureTimer {
public:
    /// Two positions further apart than this are a jump (a respawn, a seek in a replay): the
    /// chronometer stops, nothing is crossed between them.
    static constexpr float kJump = 320.0f;
    MeasureTimer() = default;
    MeasureTimer(MeasureGate a, MeasureGate b) : a_(a), b_(b) {}
    /// The next position (Nova's feet) at `t` (seconds, on any clock that never goes back).
    /// Returns the A → B time when this move crosses B while the chronometer runs.
    std::optional<float> feed(float t, Vec2 feet);
    /// A new attempt, or a jump in the stream: no previous position, the chronometer stopped.
    void reset();
    bool running() const { return started_ >= 0.0f; }
    /// Seconds since A was crossed (0 when not running).
    float elapsed(float now) const { return running() ? std::max(0.0f, now - started_) : 0.0f; }

private:
    MeasureGate a_, b_;
    bool hasPrev_ = false;
    float prevT_ = 0.0f;
    Vec2 prev_;
    float started_ = -1.0f;
};

/// Every A → B time along a whole stream of (time, feet), in order (a replay's track).
std::vector<float> measureTimes(const std::vector<std::pair<float, Vec2>>& samples, const MeasureGate& a, const MeasureGate& b);

/// A pair of measure points and the player's times on it (training).
struct MeasurePair {
    int id = 0;                 // « A{id} → B{id} », stable in the level version
    std::string name;           // given by the player; empty: « Mesure {id} »
    MeasureGate a;
    std::optional<MeasureGate> b;  // not placed yet: the next PlaceMeasure places it
    float best = -1.0f;         // seconds (-1: none yet)
    std::string bestDate;
    std::vector<float> last;    // the last times, oldest first (at most kLastKept)
    int attempts = 0;           // A → B times measured in training
    std::string date;           // when it was placed
    static constexpr size_t kLastKept = 20;
    bool complete() const { return b.has_value(); }
};

class MeasureStore {
public:
    static constexpr int kVersion = 1;
    static constexpr size_t kPerLevel = 16;
    static constexpr size_t kLevelsKept = 200;
    static constexpr size_t kNameMax = 40;

    /// `file` empty: saves/measures.json.
    explicit MeasureStore(fs::path file = {});
    void load();
    bool save();

    /// The pairs of a level version, by id.
    std::vector<MeasurePair> list(const std::string& levelKey) const;
    const MeasurePair* find(const std::string& levelKey, int id) const;
    /// InputFrame::PlaceMeasure: B of the pair waiting for it, else A of a new pair. The pair's
    /// id; 0 when the level already has kPerLevel pairs or the key is not valid.
    int place(const std::string& levelKey, Vec2 feet, const std::string& date);
    void rename(const std::string& levelKey, int id, const std::string& name);
    /// « Déplacer A / B »: the gate placed again at `feet` (its times no longer compare: forgotten).
    void move(const std::string& levelKey, int id, bool gateB, Vec2 feet);
    bool remove(const std::string& levelKey, int id);
    void clear(const std::string& levelKey);
    /// A time measured in training: true when it is the pair's new best (to the millisecond).
    bool addTime(const std::string& levelKey, int id, float seconds, const std::string& date);

    /// « Montrer les portes de mesure », « Mesurer aussi dans les replays ».
    bool showGates = true;
    bool inReplays = true;
    bool readOnly() const { return readOnly_; }

private:
    MeasurePair* get(const std::string& levelKey, int id);
    struct Level {
        std::string touched;
        std::vector<MeasurePair> pairs;
        int nextId = 1;
    };
    fs::path file_;
    std::map<std::string, Level> levels_;
    bool readOnly_ = false;
};

}  // namespace cosmic
