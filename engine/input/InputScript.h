#pragma once

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

private:
    struct Entry {
        int step;
        std::vector<Action> actions;
    };
    std::vector<Entry> entries_;
    std::array<bool, kActionCount> held_{};
    int step_ = 0;
    size_t index_ = 0;
};

}  // namespace cosmic
