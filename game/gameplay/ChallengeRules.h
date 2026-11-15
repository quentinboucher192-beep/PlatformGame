#pragma once

// Rules of a daily challenge (« Défis du jour », 2.32; contract: docs/online/DAILY_CHALLENGES.md):
// the hearts of the run, the power slots, the powers carried from the start, whether the
// power stations offer anything and whether the checkpoints activate. They are part of the
// simulation (kSimulationVersion 11): GameWorld::setChallenge applies them right after the
// level is loaded, and again at each respawn when the checkpoints are off. A run without a
// challenge plays exactly as before.

#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"

namespace cosmic {

class GameWorld;

struct ChallengeRules {
    static constexpr int kMinHearts = 1;
    static constexpr int kMaxHearts = 20;
    static constexpr int kMinSlots = 1;
    static constexpr int kMaxSlots = 8;
    /// Longest canonical form a replay may carry (format 4).
    static constexpr size_t kMaxCanonicalBytes = 1024;

    std::string challengeId;          // "daily-2026-11-16"
    int hearts = 5;                   // maximum hearts of the run, full at the start
    int slots = 4;                    // power slots
    std::vector<std::string> powers;  // carried from the start (sorted in the canonical form)
    bool stations = true;             // false: the level's power stations offer nothing
    bool checkpoints = true;          // false: no checkpoint activates, a death goes back to the start

    /// The exact string the game and the server hash (keys in this order, powers sorted,
    /// no space): {"challenge":"…","hearts":2,"slots":3,"powers":["dash"],"stations":true,"checkpoints":false}
    std::string canonical() const;
    /// SHA-256 (lower-case hex) of canonical(): the challenge's `rules_hash`.
    std::string hash() const;
    /// The powers in the order they are granted (ascending, without duplicates).
    std::vector<std::string> sortedPowers() const;

    /// Reads the `rules` object of a challenge (`challengeId` from the challenge). nullopt
    /// with `error` set when a value is missing or out of range (never clamped: a rule the
    /// game cannot apply exactly makes the challenge unplayable, not a different one).
    static std::optional<ChallengeRules> fromJson(const std::string& challengeId, const Json& rules, std::string* error = nullptr);
    /// Reads a canonical form (replay section); only a string that is exactly the canonical
    /// form of what it holds is accepted.
    static std::optional<ChallengeRules> fromCanonical(const std::string& text, std::string* error = nullptr);

    bool operator==(const ChallengeRules&) const = default;
};

/// Applies the start state of a challenge to a world whose level was just loaded (also used
/// at each respawn when the checkpoints are off): hearts (maximum and full), power slots, the
/// imposed powers granted in canonical order (the level's own start powers are not given).
/// Prefer GameWorld::setChallenge, which keeps the rules for the rest of the run.
void applyChallengeStart(GameWorld& w, const ChallengeRules& rules);

}  // namespace cosmic
