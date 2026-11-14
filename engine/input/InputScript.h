#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/input/InputManager.h"

namespace cosmic {

/// Scripted gameplay input for automated runs: "120:right;180:right,jump;240:" means
/// from fixed step 120 hold Right, from 180 hold Right + Jump, from 240 release all.
/// Action names are the ids of InputTypes (move_left...) or the short forms left,
/// right, up, down. Pressed edges are generated when an action starts being held.
class InputScript {
public:
    bool parse(const std::string& script);
    bool active() const { return !entries_.empty(); }
    PlayerInput next();
    int step() const { return step_; }
    /// Answer given by the script to a power offer ("choice2" in an entry = replace the
    /// second power, "leave" = leave the new power), 0 if none yet.
    uint8_t choice() const { return choice_; }

private:
    struct Entry {
        int step;
        std::vector<Action> actions;
        uint8_t choice = 0;
    };
    std::vector<Entry> entries_;
    std::array<bool, kActionCount> held_{};
    int step_ = 0;
    uint8_t choice_ = 0;
    size_t index_ = 0;
};

}  // namespace cosmic
