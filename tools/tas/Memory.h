#pragma once

// The memory of the TAS (docs/TAS.md « La mémoire »): the configurations the trajectory optimizer
// (Trajectory.h) has already solved — what is around Nova where a jump starts (the solids of a
// grid in front of her), how fast she goes, her jumps and energy left, her powers ready — with the
// best sequence of keys found there and what it gained. Met again, in this level or another, the
// configuration is played at once (« j'ai déjà fait cette configuration de saut ») instead of the
// thousands of variations of a search. Kept in one file of this computer, for every level.

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "tools/tas/Trajectory.h"

namespace cosmic {
class GameWorld;
}

namespace cosmic::tas {

/// What a configuration looks like (its key, and what a person reads of it).
struct Configuration {
    static constexpr int kCols = 16, kRows = 10, kCell = 16;
    uint64_t key = 0;
    int8_t dir = 1;
    int speed = 0;        // px/s along `dir`, by 25
    bool grounded = true;
    int airJumps = 0;
    int energy = 0;       // by 25
    std::string powers;   // the keys of the powers ready (« dash,wave »)
    int rise = 0;         // the highest solid in front of her above her feet (px; 0: none)
    int riseAt = 0;       // how far in front (px)
    int gap = 0;          // the widest run without ground in front of her (px)
    /// Read from the world where Nova is now, facing `dir`.
    static Configuration of(const GameWorld& w, int8_t dir);
};

/// One configuration solved.
struct Remembered {
    Configuration config;
    Maneuver maneuver;
    int leadFrames = 0;   // what it gained where it was found (frames ahead of the run then)
    int uses = 0;         // played again from memory
    int64_t gainMs = 0;   // the time it made the runs gain in all
    std::string level;    // where it was found first
    Json toJson() const;
    static std::optional<Remembered> fromJson(const Json& j);
};

class TrajectoryMemory {
public:
    static constexpr size_t kMax = 4000;
    /// The file (empty: kept in memory only).
    explicit TrajectoryMemory(std::filesystem::path file = {});
    /// Read from the file (a missing or broken file: an empty memory).
    void load();
    /// The configuration's entry (nullopt: never solved).
    std::optional<Remembered> find(uint64_t key) const;
    /// Solved (or solved better: a larger lead): kept, and the file written.
    void remember(const Configuration& c, const Maneuver& m, int leadFrames, const std::string& level);
    /// Played again from memory, and what it gained.
    void used(uint64_t key, int64_t gainMs);
    size_t size() const;
    /// The entries that gained the most (for the screen).
    std::vector<Remembered> top(size_t n) const;

private:
    void save() const;
    std::filesystem::path file_;
    mutable std::mutex m_;
    std::vector<Remembered> entries_;
};

}  // namespace cosmic::tas
