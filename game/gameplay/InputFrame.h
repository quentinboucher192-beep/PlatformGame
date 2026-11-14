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
    /// Gameplay actions recorded: MoveLeft .. Special (menus and pause never reach the
    /// simulation).
    static constexpr int kActions = static_cast<int>(Action::Special) + 1;
    enum Flag : uint8_t { SkipCinematic = 1 };
    /// Answer to a pending power offer (GameWorld::powerOffer): 1..8 replaces that slot,
    /// kChoiceLeave leaves the new power on its tablet, 0 = no answer this step.
    static constexpr uint8_t kChoiceLeave = 255;

    uint16_t held = 0;
    uint16_t pressed = 0;
    uint16_t released = 0;
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
            if (in.held[i]) f.held |= static_cast<uint16_t>(1u << i);
            if (in.pressed[i]) f.pressed |= static_cast<uint16_t>(1u << i);
            if (in.released[i]) f.released |= static_cast<uint16_t>(1u << i);
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
