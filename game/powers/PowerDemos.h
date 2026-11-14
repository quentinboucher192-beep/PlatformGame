#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine/input/InputManager.h"
#include "game/levels/LevelData.h"

namespace cosmic {

class GameWorld;

/// A short scripted scene showing one power of the player (power guide of the help).
/// The guide plays it in a loop with the game engine; the tests check that it really
/// shows the power working (`success` becomes true before the end of the loop).
struct PowerDemo {
    std::string power;
    std::vector<std::string> extra;  // other powers the example needs
    Vec2 cam{240, 755};              // world point at the centre of the frame
    float viewH = 150.0f;            // world pixels shown vertically
    int period = 360;                // fixed steps (120 Hz) before the loop restarts
    std::function<void(LevelData&)> layout;
    std::function<PlayerInput(int step)> script;
    std::function<bool(const GameWorld&)> success;
};

/// Every power of the game that has a demo, in the order of the guide.
std::vector<PowerDemo> powerDemos();
/// Level of a demo: a flat floor, its objects, the demo's powers, no limit of powers.
LevelData powerDemoLevel(const PowerDemo& d);
/// Whether a power works on its own (no button).
bool powerIsPassive(const std::string& power);

}  // namespace cosmic
