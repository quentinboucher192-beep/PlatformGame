#pragma once

// Titan 13: the « Difficulté » of a raid event (1 to 5, chosen by the administrator who programs it) made
// real. It scales the fights of the Gigaboss and of the Cœur d'Asterion, deterministically, the same on every
// computer of an instance (it reaches the games with the relay's GO: game/raid/RaidRun::setDifficulty):
// their health (weak points included) and the damage they deal. 3/5 is the base the fights are tuned for
// (a good group of 8 beats a Gigaboss in about ten minutes, docs/RAIDS.md §37). The server checks that every
// report of an instance was played at its event's difficulty (server/src/raidZones.ts).

#include <algorithm>
#include <string>

namespace cosmic::raid {

inline constexpr int kDifficultyMin = 1, kDifficultyMax = 5, kDifficultyDefault = 3;

struct DifficultyScale {
    float health = 1.0f;  // the Gigaboss' and the Heart's health, their weak points' too
    float damage = 1.0f;  // the damage they deal to the players (their blows, shots, fists, sweeps…)
    int minutes = 10;     // the fight aimed at for a good group of 8 (display: « combat visé ≈ 10 min »)
};

/// The scale of a level of difficulty (clamped to 1..5).
inline DifficultyScale difficultyScale(int level) {
    switch (std::clamp(level, kDifficultyMin, kDifficultyMax)) {
        case 1: return {0.6f, 0.6f, 6};
        case 2: return {0.8f, 0.8f, 8};
        case 4: return {1.25f, 1.25f, 13};
        case 5: return {1.5f, 1.5f, 15};
        default: return {1.0f, 1.0f, 10};
    }
}

/// The relay's level hash of an instance (server/src/raidMap.ts: levelHashOf): the branch's own (`own`: « 3.13 », its
/// revision and the titan's) then the event's difficulty (« 3.13.d4 »); without it, the base. False: another version
/// of the branch or of the titan (the game refuses: « mettez le jeu à jour »). `level`: the difficulty read.
inline bool readLevelHash(const std::string& hash, const std::string& own, int& level) {
    level = kDifficultyDefault;
    if (hash == own) return true;
    if (hash.size() != own.size() + 3 || hash.compare(0, own.size(), own) != 0 || hash[own.size()] != '.' || hash[own.size() + 1] != 'd') return false;
    const char c = hash[own.size() + 2];
    if (c < '0' + kDifficultyMin || c > '0' + kDifficultyMax) return false;
    level = c - '0';
    return true;
}
/// The same, written (« 3.13.d4 »).
inline std::string levelHashWith(const std::string& own, int level) {
    return own + ".d" + std::to_string(std::clamp(level, kDifficultyMin, kDifficultyMax));
}

}  // namespace cosmic::raid
