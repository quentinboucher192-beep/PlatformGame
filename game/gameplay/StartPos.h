#pragma once

// StartPos (training, docs/GHOST_RACING.md § « Startpos et points de mesure »): Nova's state
// taken where the player places it during a training run, to start the next attempts from
// there (the principle of Geometry Dash's practice mode). The level itself starts again as at
// its beginning (enemies, mechanisms); only Nova is set: her place and direction, her powers,
// hearts and energy as they were. Her recharges and her momentum start again (a standing start).
//
// Placing it goes through the inputs (InputFrame::PlaceStartPos): the state is read after that
// step, by the run and by its replay alike, so that a replay plays back exactly what was done.
// Reading it never changes the simulation; applying it is a start of the run, applied before
// its first step (as a section training's checkpoint), by the run and by its replay.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/core/Json.h"
#include "engine/core/Math.h"

namespace cosmic {

class GameWorld;

struct StartPosState {
    Vec2 feet;                          // Nova's feet
    bool faceLeft = false;
    std::vector<std::string> powers;    // carried, in the order of the slots
    int health = 0;                     // hearts
    int bonusHealth = 0;                // heart containers collected before
    float energy = 0.0f;
    /// The checkpoints already lit when it was placed: lit again, silently, at the start (no
    /// split, no respawn there), so that going back through one never starts a segment from
    /// the wrong side.
    std::vector<std::string> reached;

    static constexpr size_t kMaxIds = 64;
    Json toJson() const;
    /// Never trusts the file: nullopt when a field is damaged.
    static std::optional<StartPosState> fromJson(const Json& j);
    bool operator==(const StartPosState&) const = default;
};

/// A mark the player's inputs asked for at a step of a training run (the mark flags of
/// InputFrame), with Nova's state after that step. Kept by the world (GameWorld::takeMarks).
struct TrainingMark {
    uint8_t flag = 0;      // one of InputFrame::PlaceStartPos, PrevStartPos, NextStartPos, PlaceMeasure
    float chrono = 0.0f;   // the run's chronometer after that step
    bool valid = false;    // Nova could be read (alive, playing, alone): `state` is set
    StartPosState state;
};

/// Nova's state now, nullopt when it cannot be a start (not playing, dead, hidden, appearing,
/// in a cinematic, a team).
std::optional<StartPosState> captureStartPos(const GameWorld& w);
/// Sets a world just loaded and seeded, before its first step: Nova at the place, facing that
/// way (her respawn point too), its checkpoints lit silently, her powers (at most the level's
/// slots), hearts and energy.
void applyStartPos(GameWorld& w, const StartPosState& s);

}  // namespace cosmic
