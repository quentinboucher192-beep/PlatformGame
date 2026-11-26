#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "engine/input/InputManager.h"

namespace cosmic {

class GameWorld;

/// Input of one fixed simulation step, in the exact form the simulation consumes it.
/// Live play goes through this type too (analog stick quantised to 1/127), so a run
/// replayed from its recorded frames receives bit-identical inputs.
struct InputFrame {
    /// Gameplay actions recorded: MoveLeft .. PowerSlot8, one bit each in the order of
    /// Action (menus and pause never reach the simulation). Bits 13 to 20, the keys of the
    /// power slots, exist since 2.24 (replay format 3); before, the powers had their own
    /// buttons (bits 6 to 10 and 12), which old replays still use.
    static constexpr int kActions = static_cast<int>(Action::PowerSlot8) + 1;
    static_assert(kActions <= 32, "the recorded actions fit in 32 bits");
    /// Standby (2.62, a team): set by the relay only, on the inputs of a player who is absent
    /// (disconnected); never accepted from a game (docs/COOP.md).
    /// Vacant (2.65, raids): set by the relay only, on the frames of a seat nobody holds.
    /// 2.91, a training run only (docs/GHOST_RACING.md): marks the player asks for at this
    /// step — a StartPos placed here, the previous or the next one chosen (for the next
    /// attempt), a measure point placed here (A, then B). The world reads Nova's state after
    /// the step (GameWorld::noteMarks); nothing of the simulation changes: a run plays the same
    /// with or without them, and its replay keeps them (the flags byte is stored as it is).
    enum Flag : uint8_t {
        SkipCinematic = 1,
        Standby = 2,
        Vacant = 4,
        PlaceStartPos = 8,
        PrevStartPos = 16,
        NextStartPos = 32,
        PlaceMeasure = 64
    };
    static constexpr uint8_t kMarkFlags = PlaceStartPos | PrevStartPos | NextStartPos | PlaceMeasure;
    /// Answer to a pending power offer (GameWorld::powerOffer): 1..8 replaces that slot,
    /// kChoiceLeave leaves the new power on its tablet, 0 = no answer this step.
    static constexpr uint8_t kChoiceLeave = 255;

    uint32_t held = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
    int8_t moveX = 0;  // -127..127
    int8_t moveY = 0;
    uint8_t flags = 0;
    uint8_t choice = 0;

    bool operator==(const InputFrame&) const = default;

    static int8_t quantize(float v) {
        float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<int8_t>(std::lround(c * 127.0f));
    }

    static InputFrame fromPlayerInput(const PlayerInput& in) {
        InputFrame f;
        for (int i = 0; i < kActions; ++i) {
            if (in.held[i]) f.held |= 1u << i;
            if (in.pressed[i]) f.pressed |= 1u << i;
            if (in.released[i]) f.released |= 1u << i;
        }
        f.moveX = quantize(in.move.x);
        f.moveY = quantize(in.move.y);
        return f;
    }

    PlayerInput toPlayerInput() const {
        PlayerInput in;
        for (int i = 0; i < kActions; ++i) {
            in.held[i] = (held >> i) & 1u;
            in.pressed[i] = (pressed >> i) & 1u;
            in.released[i] = (released >> i) & 1u;
        }
        in.move = {static_cast<float>(moveX) / 127.0f, static_cast<float>(moveY) / 127.0f};
        return in;
    }
};

/// One fixed step of a run: applies the frame (cinematic skip, power choice) to the world.
/// While a power offer waits for an answer, a frame without one simulates nothing.
/// Live play, the replay viewer and the tests all advance the simulation through here.
void stepWorld(GameWorld& world, const InputFrame& frame, float dt);

/// 2.62: one step of a team (docs/COOP.md): one frame per player, in the order of the players.
/// A cinematic is skipped when any player skips it; a power offer waits for the choice of the
/// player it was offered to (left on its tablet when that player is in standby).
void stepTeam(GameWorld& world, const InputFrame* frames, int count, float dt);

/// Gameplay seed of a level: the same for every player, so a level always offers the
/// same randomness for the same actions (speedrun fairness), and stored in replays.
uint64_t levelSeed(const std::string& levelId);

/// 64-bit digest of the simulation state (player, enemies, projectiles, entities, timer,
/// gameplay random generator): two runs that compare equal at every check played out
/// identically. Used by replays to detect a desynchronisation.
uint64_t simulationStateHash(const GameWorld& world);

/// Fixed step of the simulation (seconds).
constexpr float kSimDt = 1.0f / 120.0f;
constexpr int kSimHz = 120;

}  // namespace cosmic
